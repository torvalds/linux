// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#include <drm/drm_print.h>
#include <linux/clk.h>
#include "komeda_dev.h"
#include "komeda_kms.h"
#include "komeda_pipeline.h"

static inline bool is_switching_user(void *old, void *new)
{
	if (!old || !new)
		return false;

	return old != new;
}

struct komeda_pipeline_state *
komeda_pipeline_get_state(struct komeda_pipeline *pipe,
			  struct drm_atomic_state *state)
{
	struct drm_private_state *priv_st;

	priv_st = drm_atomic_get_private_obj_state(state, &pipe->obj);
	if (IS_ERR(priv_st))
		return ERR_CAST(priv_st);

	return priv_to_pipe_st(priv_st);
}

/* Assign pipeline for crtc */
struct komeda_pipeline_state *
komeda_pipeline_get_state_and_set_crtc(struct komeda_pipeline *pipe,
				       struct drm_atomic_state *state,
				       struct drm_crtc *crtc)
{
	struct komeda_pipeline_state *st;

	st = komeda_pipeline_get_state(pipe, state);
	if (IS_ERR(st))
		return st;

	if (is_switching_user(crtc, st->crtc)) {
		DRM_DEBUG_ATOMIC("CRTC%d required pipeline%d is busy.\n",
				 drm_crtc_index(crtc), pipe->id);
		return ERR_PTR(-EBUSY);
	}

	/* pipeline only can be disabled when the it is free or unused */
	if (!crtc && st->active_comps) {
		DRM_DEBUG_ATOMIC("Disabling a busy pipeline:%d.\n", pipe->id);
		return ERR_PTR(-EBUSY);
	}

	st->crtc = crtc;

	if (crtc) {
		struct komeda_crtc_state *kcrtc_st;

		kcrtc_st = to_kcrtc_st(drm_atomic_get_new_crtc_state(state,
								     crtc));

		kcrtc_st->active_pipes |= BIT(pipe->id);
		kcrtc_st->affected_pipes |= BIT(pipe->id);
	}
	return st;
}

static struct komeda_component_state *
komeda_component_get_state(struct komeda_component *c,
			   struct drm_atomic_state *state)
{
	struct drm_private_state *priv_st;

	WARN_ON(!drm_modeset_is_locked(&c->pipeline->obj.lock));

	priv_st = drm_atomic_get_private_obj_state(state, &c->obj);
	if (IS_ERR(priv_st))
		return ERR_CAST(priv_st);

	return priv_to_comp_st(priv_st);
}

/**
 * komeda_component_get_state_and_set_user()
 *
 * @c: component to get state and set user
 * @state: global atomic state
 * @user: direct user, the binding user
 * @crtc: the CRTC user, the big boss :)
 *
 * This function accepts two users:
 * -   The direct user: can be plane/crtc/wb_connector depends on component
 * -   The big boss (CRTC)
 * CRTC is the big boss (the final user), because all component resources
 * eventually will be assigned to CRTC, like the layer will be binding to
 * kms_plane, but kms plane will be binding to a CRTC eventually.
 *
 * The big boss (CRTC) is for pipeline assignment, since &komeda_component isn't
 * independent and can be assigned to CRTC freely, but belongs to a specific
 * pipeline, only pipeline can be shared between crtc, and pipeline as a whole
 * (include all the internal components) assigned to a specific CRTC.
 *
 * So when set a user to komeda_component, need first to check the status of
 * component->pipeline to see if the pipeline is available on this specific
 * CRTC. if the pipeline is busy (assigned to another CRTC), even the required
 * component is free, the component still cannot be assigned to the direct user.
 */
struct komeda_component_state *
komeda_component_get_state_and_set_user(struct komeda_component *c,
					struct drm_atomic_state *state,
					void *user,
					struct drm_crtc *crtc)
{
	struct komeda_pipeline_state *pipe_st;
	struct komeda_component_state *st;

	/* First check if the pipeline is available */
	pipe_st = komeda_pipeline_get_state_and_set_crtc(c->pipeline,
							 state, crtc);
	if (IS_ERR(pipe_st))
		return ERR_CAST(pipe_st);

	st = komeda_component_get_state(c, state);
	if (IS_ERR(st))
		return st;

	/* check if the component has been occupied */
	if (is_switching_user(user, st->binding_user)) {
		DRM_DEBUG_ATOMIC("required %s is busy.\n", c->name);
		return ERR_PTR(-EBUSY);
	}

	st->binding_user = user;
	/* mark the component as active if user is valid */
	if (st->binding_user)
		pipe_st->active_comps |= BIT(c->id);

	return st;
}
