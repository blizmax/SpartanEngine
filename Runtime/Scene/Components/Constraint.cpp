/*
Copyright(c) 2016-2018 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =======================================================
#include "Constraint.h"
#include "RigidBody.h"
#include "Transform.h"
#include "../Actor.h"
#include "../../IO/FileStream.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"
#include "BulletDynamics/ConstraintSolver/btSliderConstraint.h"
#include "BulletDynamics/ConstraintSolver/btConeTwistConstraint.h"
#include "BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h"
#include "../../Logging/Log.h"
#pragma warning(pop)
//==================================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Constraint::Constraint(Context* context, Actor* actor, Transform* transform) : IComponent(context, actor, transform)
	{
		m_constraint				= nullptr;
		m_isDirty					= false;
		m_enabledEffective			= true;
		m_collisionWithLinkedBody	= false;
		m_errorReduction			= 0.0f;
		m_constraintForceMixing		= 0.0f;
		m_constraintType			= ConstraintType_Point;
	}

	Constraint::~Constraint()
	{
		ReleaseConstraint();
	}

	void Constraint::OnInitialize()
	{

	}

	void Constraint::OnStart()
	{

	}

	void Constraint::OnStop()
	{

	}

	void Constraint::OnRemove()
	{
		ReleaseConstraint();
	}

	void Constraint::OnUpdate()
	{
		if (!m_isDirty)
			return;

		ConstructConstraint();

		m_isDirty = false;
	}

	void Constraint::Serialize(FileStream* stream)
	{
		stream->Write(!m_bodyOther.expired() ? m_bodyOther.lock()->GetID() : (unsigned int)0);
	}

	void Constraint::Deserialize(FileStream* stream)
	{
		unsigned int bodyOtherID = stream->ReadUInt();
		m_bodyOther = GetContext()->GetSubsystem<Scene>()->GetActorByID(bodyOtherID);
		m_isDirty = true;
	}

	void Constraint::SetConstraintType(ConstraintType type)
	{
		if (m_type != type || !m_constraint)
		{
			m_constraintType = type;
			ConstructConstraint();
		}
	}

	void Constraint::SetPosition(const Vector3& position)
	{
		if (m_position != position)
		{
			m_position = position;
			ApplyFrames();
		}
	}

	void Constraint::SetRotation(const Quaternion& rotation)
	{
		if (m_rotation != rotation)
		{
			m_rotation = rotation;
			ApplyFrames();
		}
	}

	void Constraint::SetPositionOther(const Math::Vector3& position)
	{
		if (position != m_positionOther)
		{
			m_positionOther = position;
			ApplyFrames();
		}
	}

	void Constraint::SetRotationOther(const Math::Quaternion& rotation)
	{
		if (rotation != m_rotationOther)
		{
			m_rotationOther = rotation;
			ApplyFrames();
		}
	}

	void Constraint::SetBodyOther(std::weak_ptr<Actor> bodyOther)
	{
		if (bodyOther.expired())
			return;

		if (!bodyOther.expired() && bodyOther.lock()->GetID() == m_actor->GetID())
		{
			LOG_WARNING("Constraint::SetBodyOther: You can't connect a body to itself.");
			return;
		}

		m_bodyOther = bodyOther;
		ConstructConstraint();
	}

	void Constraint::SetHighLimit(const Vector2& limit)
	{
		if (m_highLimit != limit)
		{
			m_highLimit = limit;
			ApplyLimits();
		}
	}

	void Constraint::SetLowLimit(const Vector2& limit)
	{
		if (m_lowLimit != limit)
		{
			m_lowLimit = limit;
			ApplyLimits();
		}
	}

	/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void Constraint::ConstructConstraint()
	{
		ReleaseConstraint();

		RigidBody* rigidBodyOwn		= m_actor->GetComponent<RigidBody>().lock().get();
		RigidBody* rigidBodyOther	= !m_bodyOther.expired() ? m_bodyOther.lock()->GetComponent<RigidBody>().lock().get() : nullptr;
		btRigidBody* btOwnBody		= rigidBodyOwn ? rigidBodyOwn->GetBtRigidBody() : nullptr;
		btRigidBody* btOtherBody	= rigidBodyOther ? rigidBodyOther->GetBtRigidBody() : nullptr;

		if (!btOwnBody)
		    return;

		if (!btOtherBody)
		{
		    btOtherBody = &btTypedConstraint::getFixedBody();
		}	
		
		Vector3 ownBodyScaledPosition	= m_position * m_transform->GetScale() - rigidBodyOwn->GetColliderCenter();
		Vector3 otherBodyScaledPosition = rigidBodyOther ? m_positionOther * rigidBodyOther->GetTransform()->GetScale() - rigidBodyOther->GetColliderCenter() : m_positionOther;

		switch (m_constraintType)
		{
			case ConstraintType_Point:
			    {
			        m_constraint = make_unique<btPoint2PointConstraint>(*btOwnBody, *btOtherBody, ToBtVector3(ownBodyScaledPosition), ToBtVector3(otherBodyScaledPosition));
			    }
			    break;

			case ConstraintType_Hinge:
			    {
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        m_constraint = make_unique<btHingeConstraint>(*btOwnBody, *btOtherBody, ownFrame, otherFrame);
			    }
			    break;

			case ConstraintType_Slider:
			    {
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        m_constraint = make_unique<btSliderConstraint>(*btOwnBody, *btOtherBody, ownFrame, otherFrame, false);
			    }
			    break;

			case ConstraintType_ConeTwist:
			    {
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        m_constraint = make_unique<btConeTwistConstraint>(*btOwnBody, *btOtherBody, ownFrame, otherFrame);
			    }
			    break;

			default:
			    break;
		}

		if (m_constraint)
		{
		    m_constraint->setUserConstraintPtr(this);
		    m_constraint->setEnabled(m_enabledEffective);
		    rigidBodyOwn->AddConstraint(this);
		    if (!m_bodyOther.expired())
			{
		        rigidBodyOther->AddConstraint(this);
			}

		    ApplyLimits();

		    GetContext()->GetSubsystem<Physics>()->GetWorld()->addConstraint(m_constraint.get(), !m_collisionWithLinkedBody);
		}
	}

	void Constraint::ApplyLimits()
	{
		if (!m_constraint)
			return;

		switch (m_constraint->getConstraintType())
		{
			case HINGE_CONSTRAINT_TYPE:
			    {
			        auto* hingeConstraint = static_cast<btHingeConstraint*>(m_constraint.get());
			        hingeConstraint->setLimit(m_lowLimit.x * DEG_TO_RAD, m_highLimit.x * DEG_TO_RAD);
			    }
			    break;

			case SLIDER_CONSTRAINT_TYPE:
			    {
			        auto* sliderConstraint = static_cast<btSliderConstraint*>(m_constraint.get());
			        sliderConstraint->setUpperLinLimit(m_highLimit.x);
			        sliderConstraint->setUpperAngLimit(m_highLimit.y * DEG_TO_RAD);
			        sliderConstraint->setLowerLinLimit(m_lowLimit.x);
			        sliderConstraint->setLowerAngLimit(m_lowLimit.y * DEG_TO_RAD);
			    }
			    break;

			case CONETWIST_CONSTRAINT_TYPE:
			    {
			        auto* coneTwistConstraint = static_cast<btConeTwistConstraint*>(m_constraint.get());
			        coneTwistConstraint->setLimit(m_highLimit.y * DEG_TO_RAD, m_highLimit.y * DEG_TO_RAD, m_highLimit.x * DEG_TO_RAD);
			    }
			    break;

			default:
			    break;
		}

		if (m_errorReduction != 0.0f)
		{
		    m_constraint->setParam(BT_CONSTRAINT_STOP_ERP, m_errorReduction);
		}

		if (m_constraintForceMixing != 0.0f)
		{
			m_constraint->setParam(BT_CONSTRAINT_STOP_CFM, m_constraintForceMixing);
		}
	}

	void Constraint::ApplyFrames()
	{
		if (!m_constraint || m_bodyOther.expired())
			return;

		RigidBody* rigidBodyOwn		= m_actor->GetComponent<RigidBody>().lock().get();
		RigidBody* rigidBodyOther	= !m_bodyOther.expired() ? m_bodyOther.lock()->GetComponent<RigidBody>().lock().get() : nullptr;
		btRigidBody* btOwnBody		= rigidBodyOwn ? rigidBodyOwn->GetBtRigidBody() : nullptr;
		btRigidBody* btOtherBody	= rigidBodyOther ? rigidBodyOther->GetBtRigidBody() : nullptr;

		Vector3 ownBodyScaledPosition = m_position * m_transform->GetScale() - rigidBodyOwn->GetColliderCenter();
		Vector3 otherBodyScaledPosition = !m_bodyOther.expired() ? m_positionOther * rigidBodyOther->GetTransform()->GetScale() - rigidBodyOther->GetColliderCenter() : m_positionOther;

		switch (m_constraint->getConstraintType())
		{
			case POINT2POINT_CONSTRAINT_TYPE:
			    {
			        auto* pointConstraint = static_cast<btPoint2PointConstraint*>(m_constraint.get());
			        pointConstraint->setPivotA(ToBtVector3(ownBodyScaledPosition));
			        pointConstraint->setPivotB(ToBtVector3(otherBodyScaledPosition));
			    }
			    break;

			case HINGE_CONSTRAINT_TYPE:
			    {
			        auto* hingeConstraint = static_cast<btHingeConstraint*>(m_constraint.get());
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        hingeConstraint->setFrames(ownFrame, otherFrame);
			    }
			    break;

			case SLIDER_CONSTRAINT_TYPE:
			    {
			        auto* sliderConstraint = static_cast<btSliderConstraint*>(m_constraint.get());
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        sliderConstraint->setFrames(ownFrame, otherFrame);
			    }
			    break;

			case CONETWIST_CONSTRAINT_TYPE:
			    {
			        auto* coneTwistConstraint = static_cast<btConeTwistConstraint*>(m_constraint.get());
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        coneTwistConstraint->setFrames(ownFrame, otherFrame);
			    }
			    break;

			default:
			    break;
		}
	}

	void Constraint::ReleaseConstraint()
	{
		if (m_constraint)
		{
			RigidBody* rigidBodyOwn		= m_actor->GetComponent<RigidBody>().lock().get();
			RigidBody* rigidBodyOther	= !m_bodyOther.expired() ? m_bodyOther.lock()->GetComponent<RigidBody>().lock().get() : nullptr;

			if (rigidBodyOwn)
			{
				rigidBodyOwn->RemoveConstraint(this);
			}

			if (rigidBodyOther)
			{
				rigidBodyOther->RemoveConstraint(this);
			}

			GetContext()->GetSubsystem<Physics>()->GetWorld()->removeConstraint(m_constraint.get());
			m_constraint.reset();
		}	
	}
}
