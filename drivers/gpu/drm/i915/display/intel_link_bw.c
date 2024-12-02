// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_fixed.h>

#include "i915_drv.h"

#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_display_types.h"
#include "intel_dp_mst.h"
#include "intel_dp_tunnel.h"
#include "intel_fdi.h"
#include "intel_link_bw.h"

/**
 * intel_link_bw_init_limits - initialize BW limits
 * @state: Atomic state
 * @limits: link BW limits
 *
 * Initialize @limits.
 */
void intel_link_bw_init_limits(struct intel_atomic_state *state,
			       struct intel_link_bw_limits *limits)
{
	struct intel_display *display = to_intel_display(state);
	enum pipe pipe;

	limits->force_fec_pipes = 0;
	limits->bpp_limit_reached_pipes = 0;
	for_each_pipe(display, pipe) {
		const struct intel_crtc_state *crtc_state =
			intel_atomic_get_new_crtc_state(state,
							intel_crtc_for_pipe(display, pipe));

		if (state->base.duplicated && crtc_state) {
			limits->max_bpp_x16[pipe] = crtc_state->max_link_bpp_x16;
			if (crtc_state->fec_enable)
				limits->force_fec_pipes |= BIT(pipe);
		} else {
			limits->max_bpp_x16[pipe] = INT_MAX;
		}
	}
}

/**
 * intel_link_bw_reduce_bpp - reduce maximum link bpp for a selected pipe
 * @state: atomic state
 * @limits: link BW limits
 * @pipe_mask: mask of pipes to select from
 * @reason: explanation of why bpp reduction is needed
 *
 * Select the pipe from @pipe_mask with the biggest link bpp value and set the
 * maximum of link bpp in @limits below this value. Modeset the selected pipe,
 * so that its state will get recomputed.
 *
 * This function can be called to resolve a link's BW overallocation by reducing
 * the link bpp of one pipe on the link and hence reducing the total link BW.
 *
 * Returns
 *   - 0 in case of success
 *   - %-ENOSPC if no pipe can further reduce its link bpp
 *   - Other negative error, if modesetting the selected pipe failed
 */
int intel_link_bw_reduce_bpp(struct intel_atomic_state *state,
			     struct intel_link_bw_limits *limits,
			     u8 pipe_mask,
			     const char *reason)
{
	struct intel_display *display = to_intel_display(state);
	enum pipe max_bpp_pipe = INVALID_PIPE;
	struct intel_crtc *crtc;
	int max_bpp_x16 = 0;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, pipe_mask) {
		struct intel_crtc_state *crtc_state;
		int link_bpp_x16;

		if (limits->bpp_limit_reached_pipes & BIT(crtc->pipe))
			continue;

		crtc_state = intel_atomic_get_crtc_state(&state->base,
							 crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (crtc_state->dsc.compression_enable)
			link_bpp_x16 = crtc_state->dsc.compressed_bpp_x16;
		else
			/*
			 * TODO: for YUV420 the actual link bpp is only half
			 * of the pipe bpp value. The MST encoder's BW allocation
			 * is based on the pipe bpp value, set the actual link bpp
			 * limit here once the MST BW allocation is fixed.
			 */
			link_bpp_x16 = fxp_q4_from_int(crtc_state->pipe_bpp);

		if (link_bpp_x16 > max_bpp_x16) {
			max_bpp_x16 = link_bpp_x16;
			max_bpp_pipe = crtc->pipe;
		}
	}

	if (max_bpp_pipe == INVALID_PIPE)
		return -ENOSPC;

	limits->max_bpp_x16[max_bpp_pipe] = max_bpp_x16 - 1;

	return intel_modeset_pipes_in_mask_early(state, reason,
						 BIT(max_bpp_pipe));
}

/**
 * intel_link_bw_set_bpp_limit_for_pipe - set link bpp limit for a pipe to its minimum
 * @state: atomic state
 * @old_limits: link BW limits
 * @new_limits: link BW limits
 * @pipe: pipe
 *
 * Set the link bpp limit for @pipe in @new_limits to its value in
 * @old_limits and mark this limit as the minimum. This function must be
 * called after a pipe's compute config function failed, @old_limits
 * containing the bpp limit with which compute config previously passed.
 *
 * The function will fail if setting a minimum is not possible, either
 * because the old and new limits match (and so would lead to a pipe compute
 * config failure) or the limit is already at the minimum.
 *
 * Returns %true in case of success.
 */
bool
intel_link_bw_set_bpp_limit_for_pipe(struct intel_atomic_state *state,
				     const struct intel_link_bw_limits *old_limits,
				     struct intel_link_bw_limits *new_limits,
				     enum pipe pipe)
{
	struct intel_display *display = to_intel_display(state);

	if (pipe == INVALID_PIPE)
		return false;

	if (new_limits->max_bpp_x16[pipe] ==
	    old_limits->max_bpp_x16[pipe])
		return false;

	if (drm_WARN_ON(display->drm,
			new_limits->bpp_limit_reached_pipes & BIT(pipe)))
		return false;

	new_limits->max_bpp_x16[pipe] =
		old_limits->max_bpp_x16[pipe];
	new_limits->bpp_limit_reached_pipes |= BIT(pipe);

	return true;
}

static int check_all_link_config(struct intel_atomic_state *state,
				 struct intel_link_bw_limits *limits)
{
	/* TODO: Check additional shared display link configurations like MST */
	int ret;

	ret = intel_dp_mst_atomic_check_link(state, limits);
	if (ret)
		return ret;

	ret = intel_dp_tunnel_atomic_check_link(state, limits);
	if (ret)
		return ret;

	ret = intel_fdi_atomic_check_link(state, limits);
	if (ret)
		return ret;

	return 0;
}

static bool
assert_link_limit_change_valid(struct intel_display *display,
			       const struct intel_link_bw_limits *old_limits,
			       const struct intel_link_bw_limits *new_limits)
{
	bool bpps_changed = false;
	enum pipe pipe;

	/* FEC can't be forced off after it was forced on. */
	if (drm_WARN_ON(display->drm,
			(old_limits->force_fec_pipes & new_limits->force_fec_pipes) !=
			old_limits->force_fec_pipes))
		return false;

	for_each_pipe(display, pipe) {
		/* The bpp limit can only decrease. */
		if (drm_WARN_ON(display->drm,
				new_limits->max_bpp_x16[pipe] >
				old_limits->max_bpp_x16[pipe]))
			return false;

		if (new_limits->max_bpp_x16[pipe] <
		    old_limits->max_bpp_x16[pipe])
			bpps_changed = true;
	}

	/* At least one limit must change. */
	if (drm_WARN_ON(display->drm,
			!bpps_changed &&
			new_limits->force_fec_pipes ==
			old_limits->force_fec_pipes))
		return false;

	return true;
}

/**
 * intel_link_bw_atomic_check - check display link states and set a fallback config if needed
 * @state: atomic state
 * @new_limits: link BW limits
 *
 * Check the configuration of all shared display links in @state and set new BW
 * limits in @new_limits if there is a BW limitation.
 *
 * Returns:
 *   - 0 if the confugration is valid
 *   - %-EAGAIN, if the configuration is invalid and @new_limits got updated
 *     with fallback values with which the configuration of all CRTCs
 *     in @state must be recomputed
 *   - Other negative error, if the configuration is invalid without a
 *     fallback possibility, or the check failed for another reason
 */
int intel_link_bw_atomic_check(struct intel_atomic_state *state,
			       struct intel_link_bw_limits *new_limits)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_link_bw_limits old_limits = *new_limits;
	int ret;

	ret = check_all_link_config(state, new_limits);
	if (ret != -EAGAIN)
		return ret;

	if (!assert_link_limit_change_valid(display, &old_limits, new_limits))
		return -EINVAL;

	return -EAGAIN;
}
