// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"

#include <drm/display/drm_dp_tunnel.h>

#include "intel_atomic.h"
#include "intel_display_limits.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dp_tunnel.h"
#include "intel_link_bw.h"

struct intel_dp_tunnel_inherited_state {
	struct drm_dp_tunnel_ref ref[I915_MAX_PIPES];
};

/**
 * intel_dp_tunnel_disconnect - Disconnect a DP tunnel from a port
 * @intel_dp: DP port object the tunnel is connected to
 *
 * Disconnect a DP tunnel from @intel_dp, destroying any related state. This
 * should be called after detecting a sink-disconnect event from the port.
 */
void intel_dp_tunnel_disconnect(struct intel_dp *intel_dp)
{
	drm_dp_tunnel_destroy(intel_dp->tunnel);
	intel_dp->tunnel = NULL;
}

/**
 * intel_dp_tunnel_destroy - Destroy a DP tunnel
 * @intel_dp: DP port object the tunnel is connected to
 *
 * Destroy a DP tunnel connected to @intel_dp, after disabling the BW
 * allocation mode on the tunnel. This should be called while destroying the
 * port.
 */
void intel_dp_tunnel_destroy(struct intel_dp *intel_dp)
{
	if (intel_dp_tunnel_bw_alloc_is_enabled(intel_dp))
		drm_dp_tunnel_disable_bw_alloc(intel_dp->tunnel);

	intel_dp_tunnel_disconnect(intel_dp);
}

static int kbytes_to_mbits(int kbytes)
{
	return DIV_ROUND_UP(kbytes * 8, 1000);
}

static int get_current_link_bw(struct intel_dp *intel_dp,
			       bool *below_dprx_bw)
{
	int rate = intel_dp_max_common_rate(intel_dp);
	int lane_count = intel_dp_max_common_lane_count(intel_dp);
	int bw;

	bw = intel_dp_max_link_data_rate(intel_dp, rate, lane_count);
	*below_dprx_bw = bw < drm_dp_max_dprx_data_rate(rate, lane_count);

	return bw;
}

static int update_tunnel_state(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	bool old_bw_below_dprx;
	bool new_bw_below_dprx;
	int old_bw;
	int new_bw;
	int ret;

	old_bw = get_current_link_bw(intel_dp, &old_bw_below_dprx);

	ret = drm_dp_tunnel_update_state(intel_dp->tunnel);
	if (ret < 0) {
		drm_dbg_kms(&i915->drm,
			    "[DPTUN %s][ENCODER:%d:%s] State update failed (err %pe)\n",
			    drm_dp_tunnel_name(intel_dp->tunnel),
			    encoder->base.base.id, encoder->base.name,
			    ERR_PTR(ret));

		return ret;
	}

	if (ret == 0 ||
	    !drm_dp_tunnel_bw_alloc_is_enabled(intel_dp->tunnel))
		return 0;

	intel_dp_update_sink_caps(intel_dp);

	new_bw = get_current_link_bw(intel_dp, &new_bw_below_dprx);

	/* Suppress the notification if the mode list can't change due to bw. */
	if (old_bw_below_dprx == new_bw_below_dprx &&
	    !new_bw_below_dprx)
		return 0;

	drm_dbg_kms(&i915->drm,
		    "[DPTUN %s][ENCODER:%d:%s] Notify users about BW change: %d -> %d\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    encoder->base.base.id, encoder->base.name,
		    kbytes_to_mbits(old_bw), kbytes_to_mbits(new_bw));

	return 1;
}

/*
 * Allocate the BW for a tunnel on a DP connector/port if the connector/port
 * was already active when detecting the tunnel. The allocated BW must be
 * freed by the next atomic modeset, storing the BW in the
 * intel_atomic_state::inherited_dp_tunnels, and calling
 * intel_dp_tunnel_atomic_free_bw().
 */
static int allocate_initial_tunnel_bw_for_pipes(struct intel_dp *intel_dp, u8 pipe_mask)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct intel_crtc *crtc;
	int tunnel_bw = 0;
	int err;

	for_each_intel_crtc_in_pipe_mask(&i915->drm, crtc, pipe_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		int stream_bw = intel_dp_config_required_rate(crtc_state);

		tunnel_bw += stream_bw;

		drm_dbg_kms(&i915->drm,
			    "[DPTUN %s][ENCODER:%d:%s][CRTC:%d:%s] Initial BW for stream %d: %d/%d Mb/s\n",
			    drm_dp_tunnel_name(intel_dp->tunnel),
			    encoder->base.base.id, encoder->base.name,
			    crtc->base.base.id, crtc->base.name,
			    crtc->pipe,
			    kbytes_to_mbits(stream_bw), kbytes_to_mbits(tunnel_bw));
	}

	err = drm_dp_tunnel_alloc_bw(intel_dp->tunnel, tunnel_bw);
	if (err) {
		drm_dbg_kms(&i915->drm,
			    "[DPTUN %s][ENCODER:%d:%s] Initial BW allocation failed (err %pe)\n",
			    drm_dp_tunnel_name(intel_dp->tunnel),
			    encoder->base.base.id, encoder->base.name,
			    ERR_PTR(err));

		return err;
	}

	return update_tunnel_state(intel_dp);
}

static int allocate_initial_tunnel_bw(struct intel_dp *intel_dp,
				      struct drm_modeset_acquire_ctx *ctx)
{
	u8 pipe_mask;
	int err;

	err = intel_dp_get_active_pipes(intel_dp, ctx, &pipe_mask);
	if (err)
		return err;

	return allocate_initial_tunnel_bw_for_pipes(intel_dp, pipe_mask);
}

static int detect_new_tunnel(struct intel_dp *intel_dp, struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_dp_tunnel *tunnel;
	int ret;

	tunnel = drm_dp_tunnel_detect(i915->display.dp_tunnel_mgr,
				      &intel_dp->aux);
	if (IS_ERR(tunnel))
		return PTR_ERR(tunnel);

	intel_dp->tunnel = tunnel;

	ret = drm_dp_tunnel_enable_bw_alloc(intel_dp->tunnel);
	if (ret) {
		if (ret == -EOPNOTSUPP)
			return 0;

		drm_dbg_kms(&i915->drm,
			    "[DPTUN %s][ENCODER:%d:%s] Failed to enable BW allocation mode (ret %pe)\n",
			    drm_dp_tunnel_name(intel_dp->tunnel),
			    encoder->base.base.id, encoder->base.name,
			    ERR_PTR(ret));

		/* Keep the tunnel with BWA disabled */
		return 0;
	}

	ret = allocate_initial_tunnel_bw(intel_dp, ctx);
	if (ret < 0)
		intel_dp_tunnel_destroy(intel_dp);

	return ret;
}

/**
 * intel_dp_tunnel_detect - Detect a DP tunnel on a port
 * @intel_dp: DP port object
 * @ctx: lock context acquired by the connector detection handler
 *
 * Detect a DP tunnel on the @intel_dp port, enabling the BW allocation mode
 * on it if supported and allocating the BW required on an already active port.
 * The BW allocated this way must be freed by the next atomic modeset calling
 * intel_dp_tunnel_atomic_free_bw().
 *
 * If @intel_dp has already a tunnel detected on it, update the tunnel's state
 * wrt. its support for BW allocation mode and the available BW via the
 * tunnel. If the tunnel's state change requires this - for instance the
 * tunnel's group ID has changed - the tunnel will be dropped and recreated.
 *
 * Return 0 in case of success - after any tunnel detected and added to
 * @intel_dp - 1 in case the BW on an already existing tunnel has changed in a
 * way that requires notifying user space.
 */
int intel_dp_tunnel_detect(struct intel_dp *intel_dp, struct drm_modeset_acquire_ctx *ctx)
{
	int ret;

	if (intel_dp_is_edp(intel_dp))
		return 0;

	if (intel_dp->tunnel) {
		ret = update_tunnel_state(intel_dp);
		if (ret >= 0)
			return ret;

		/* Try to recreate the tunnel after an update error. */
		intel_dp_tunnel_destroy(intel_dp);
	}

	return detect_new_tunnel(intel_dp, ctx);
}

/**
 * intel_dp_tunnel_bw_alloc_is_enabled - Query the BW allocation support on a tunnel
 * @intel_dp: DP port object
 *
 * Query whether a DP tunnel is connected on @intel_dp and the tunnel supports
 * the BW allocation mode.
 *
 * Returns %true if the BW allocation mode is supported on @intel_dp.
 */
bool intel_dp_tunnel_bw_alloc_is_enabled(struct intel_dp *intel_dp)
{
	return drm_dp_tunnel_bw_alloc_is_enabled(intel_dp->tunnel);
}

/**
 * intel_dp_tunnel_suspend - Suspend a DP tunnel connected on a port
 * @intel_dp: DP port object
 *
 * Suspend a DP tunnel on @intel_dp with BW allocation mode enabled on it.
 */
void intel_dp_tunnel_suspend(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;

	if (!intel_dp_tunnel_bw_alloc_is_enabled(intel_dp))
		return;

	drm_dbg_kms(&i915->drm, "[DPTUN %s][CONNECTOR:%d:%s][ENCODER:%d:%s] Suspend\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name);

	drm_dp_tunnel_disable_bw_alloc(intel_dp->tunnel);

	intel_dp->tunnel_suspended = true;
}

/**
 * intel_dp_tunnel_resume - Resume a DP tunnel connected on a port
 * @intel_dp: DP port object
 * @crtc_state: CRTC state
 * @dpcd_updated: the DPCD DPRX capabilities got updated during resume
 *
 * Resume a DP tunnel on @intel_dp with BW allocation mode enabled on it.
 */
void intel_dp_tunnel_resume(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state,
			    bool dpcd_updated)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 pipe_mask;
	int err = 0;

	if (!intel_dp->tunnel_suspended)
		return;

	intel_dp->tunnel_suspended = false;

	drm_dbg_kms(&i915->drm, "[DPTUN %s][CONNECTOR:%d:%s][ENCODER:%d:%s] Resume\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name);

	/*
	 * The TBT Connection Manager requires the GFX driver to read out
	 * the sink's DPRX caps to be able to service any BW requests later.
	 * During resume overriding the caps in @intel_dp cached before
	 * suspend must be avoided, so do here only a dummy read, unless the
	 * capabilities were updated already during resume.
	 */
	if (!dpcd_updated) {
		err = intel_dp_read_dprx_caps(intel_dp, dpcd);

		if (err) {
			drm_dp_tunnel_set_io_error(intel_dp->tunnel);
			goto out_err;
		}
	}

	err = drm_dp_tunnel_enable_bw_alloc(intel_dp->tunnel);
	if (err)
		goto out_err;

	pipe_mask = 0;
	if (crtc_state) {
		struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

		/* TODO: Add support for MST */
		pipe_mask |= BIT(crtc->pipe);
	}

	err = allocate_initial_tunnel_bw_for_pipes(intel_dp, pipe_mask);
	if (err < 0)
		goto out_err;

	return;

out_err:
	drm_dbg_kms(&i915->drm,
		    "[DPTUN %s][CONNECTOR:%d:%s][ENCODER:%d:%s] Tunnel can't be resumed, will drop and reject it (err %pe)\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name,
		    ERR_PTR(err));
}

static struct drm_dp_tunnel *
get_inherited_tunnel(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	if (!state->inherited_dp_tunnels)
		return NULL;

	return state->inherited_dp_tunnels->ref[crtc->pipe].tunnel;
}

static int
add_inherited_tunnel(struct intel_atomic_state *state,
		     struct drm_dp_tunnel *tunnel,
		     struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct drm_dp_tunnel *old_tunnel;

	old_tunnel = get_inherited_tunnel(state, crtc);
	if (old_tunnel) {
		drm_WARN_ON(&i915->drm, old_tunnel != tunnel);
		return 0;
	}

	if (!state->inherited_dp_tunnels) {
		state->inherited_dp_tunnels = kzalloc(sizeof(*state->inherited_dp_tunnels),
						      GFP_KERNEL);
		if (!state->inherited_dp_tunnels)
			return -ENOMEM;
	}

	drm_dp_tunnel_ref_get(tunnel, &state->inherited_dp_tunnels->ref[crtc->pipe]);

	return 0;
}

static int check_inherited_tunnel_state(struct intel_atomic_state *state,
					struct intel_dp *intel_dp,
					const struct intel_digital_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->base.connector);
	struct intel_crtc *old_crtc;
	const struct intel_crtc_state *old_crtc_state;

	/*
	 * If a BWA tunnel gets detected only after the corresponding
	 * connector got enabled already without a BWA tunnel, or a different
	 * BWA tunnel (which was removed meanwhile) the old CRTC state won't
	 * contain the state of the current tunnel. This tunnel still has a
	 * reserved BW, which needs to be released, add the state for such
	 * inherited tunnels separately only to this atomic state.
	 */
	if (!intel_dp_tunnel_bw_alloc_is_enabled(intel_dp))
		return 0;

	if (!old_conn_state->base.crtc)
		return 0;

	old_crtc = to_intel_crtc(old_conn_state->base.crtc);
	old_crtc_state = intel_atomic_get_old_crtc_state(state, old_crtc);

	if (!old_crtc_state->hw.active ||
	    old_crtc_state->dp_tunnel_ref.tunnel == intel_dp->tunnel)
		return 0;

	drm_dbg_kms(&i915->drm,
		    "[DPTUN %s][CONNECTOR:%d:%s][ENCODER:%d:%s][CRTC:%d:%s] Adding state for inherited tunnel %p\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name,
		    old_crtc->base.base.id, old_crtc->base.name,
		    intel_dp->tunnel);

	return add_inherited_tunnel(state, intel_dp->tunnel, old_crtc);
}

/**
 * intel_dp_tunnel_atomic_cleanup_inherited_state - Free any inherited DP tunnel state
 * @state: Atomic state
 *
 * Free the inherited DP tunnel state in @state.
 */
void intel_dp_tunnel_atomic_cleanup_inherited_state(struct intel_atomic_state *state)
{
	enum pipe pipe;

	if (!state->inherited_dp_tunnels)
		return;

	for_each_pipe(to_i915(state->base.dev), pipe)
		if (state->inherited_dp_tunnels->ref[pipe].tunnel)
			drm_dp_tunnel_ref_put(&state->inherited_dp_tunnels->ref[pipe]);

	kfree(state->inherited_dp_tunnels);
	state->inherited_dp_tunnels = NULL;
}

static int intel_dp_tunnel_atomic_add_group_state(struct intel_atomic_state *state,
						  struct drm_dp_tunnel *tunnel)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	u32 pipe_mask;
	int err;

	err = drm_dp_tunnel_atomic_get_group_streams_in_state(&state->base,
							      tunnel, &pipe_mask);
	if (err)
		return err;

	drm_WARN_ON(&i915->drm, pipe_mask & ~((1 << I915_MAX_PIPES) - 1));

	return intel_modeset_pipes_in_mask_early(state, "DPTUN", pipe_mask);
}

/**
 * intel_dp_tunnel_atomic_add_state_for_crtc - Add CRTC specific DP tunnel state
 * @state: Atomic state
 * @crtc: CRTC to add the tunnel state for
 *
 * Add the DP tunnel state for @crtc if the CRTC (aka DP tunnel stream) is enabled
 * via a DP tunnel.
 *
 * Return 0 in case of success, a negative error code otherwise.
 */
int intel_dp_tunnel_atomic_add_state_for_crtc(struct intel_atomic_state *state,
					      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct drm_dp_tunnel_state *tunnel_state;
	struct drm_dp_tunnel *tunnel = new_crtc_state->dp_tunnel_ref.tunnel;

	if (!tunnel)
		return 0;

	tunnel_state = drm_dp_tunnel_atomic_get_state(&state->base, tunnel);
	if (IS_ERR(tunnel_state))
		return PTR_ERR(tunnel_state);

	return 0;
}

static int check_group_state(struct intel_atomic_state *state,
			     struct intel_dp *intel_dp,
			     struct intel_connector *connector,
			     struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->dp_tunnel_ref.tunnel)
		return 0;

	drm_dbg_kms(&i915->drm,
		    "[DPTUN %s][CONNECTOR:%d:%s][ENCODER:%d:%s][CRTC:%d:%s] Adding group state for tunnel %p\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name,
		    crtc->base.base.id, crtc->base.name,
		    crtc_state->dp_tunnel_ref.tunnel);

	return intel_dp_tunnel_atomic_add_group_state(state, crtc_state->dp_tunnel_ref.tunnel);
}

/**
 * intel_dp_tunnel_atomic_check_state - Check a connector's DP tunnel specific state
 * @state: Atomic state
 * @intel_dp: DP port object
 * @connector: connector using @intel_dp
 *
 * Check and add the DP tunnel atomic state for @intel_dp/@connector to
 * @state, if there is a DP tunnel detected on @intel_dp with BW allocation
 * mode enabled on it, or if @intel_dp/@connector was previously enabled via a
 * DP tunnel.
 *
 * Returns 0 in case of success, or a negative error code otherwise.
 */
int intel_dp_tunnel_atomic_check_state(struct intel_atomic_state *state,
				       struct intel_dp *intel_dp,
				       struct intel_connector *connector)
{
	const struct intel_digital_connector_state *old_conn_state =
		intel_atomic_get_old_connector_state(state, connector);
	const struct intel_digital_connector_state *new_conn_state =
		intel_atomic_get_new_connector_state(state, connector);
	int err;

	if (old_conn_state->base.crtc) {
		err = check_group_state(state, intel_dp, connector,
					to_intel_crtc(old_conn_state->base.crtc));
		if (err)
			return err;
	}

	if (new_conn_state->base.crtc &&
	    new_conn_state->base.crtc != old_conn_state->base.crtc) {
		err = check_group_state(state, intel_dp, connector,
					to_intel_crtc(new_conn_state->base.crtc));
		if (err)
			return err;
	}

	return check_inherited_tunnel_state(state, intel_dp, old_conn_state);
}

/**
 * intel_dp_tunnel_atomic_compute_stream_bw - Compute the BW required by a DP tunnel stream
 * @state: Atomic state
 * @intel_dp: DP object
 * @connector: connector using @intel_dp
 * @crtc_state: state of CRTC of the given DP tunnel stream
 *
 * Compute the required BW of CRTC (aka DP tunnel stream), storing this BW to
 * the DP tunnel state containing the stream in @state. Before re-calculating a
 * BW requirement in the crtc_state state the old BW requirement computed by this
 * function must be cleared by calling intel_dp_tunnel_atomic_clear_stream_bw().
 *
 * Returns 0 in case of success, a negative error code otherwise.
 */
int intel_dp_tunnel_atomic_compute_stream_bw(struct intel_atomic_state *state,
					     struct intel_dp *intel_dp,
					     const struct intel_connector *connector,
					     struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int required_rate = intel_dp_config_required_rate(crtc_state);
	int ret;

	if (!intel_dp_tunnel_bw_alloc_is_enabled(intel_dp))
		return 0;

	drm_dbg_kms(&i915->drm,
		    "[DPTUN %s][CONNECTOR:%d:%s][ENCODER:%d:%s][CRTC:%d:%s] Stream %d required BW %d Mb/s\n",
		    drm_dp_tunnel_name(intel_dp->tunnel),
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name,
		    crtc->base.base.id, crtc->base.name,
		    crtc->pipe,
		    kbytes_to_mbits(required_rate));

	ret = drm_dp_tunnel_atomic_set_stream_bw(&state->base, intel_dp->tunnel,
						 crtc->pipe, required_rate);
	if (ret < 0)
		return ret;

	drm_dp_tunnel_ref_get(intel_dp->tunnel,
			      &crtc_state->dp_tunnel_ref);

	return 0;
}

/**
 * intel_dp_tunnel_atomic_clear_stream_bw - Clear any DP tunnel stream BW requirement
 * @state: Atomic state
 * @crtc_state: state of CRTC of the given DP tunnel stream
 *
 * Clear any DP tunnel stream BW requirement set by
 * intel_dp_tunnel_atomic_compute_stream_bw().
 */
void intel_dp_tunnel_atomic_clear_stream_bw(struct intel_atomic_state *state,
					    struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!crtc_state->dp_tunnel_ref.tunnel)
		return;

	drm_dp_tunnel_atomic_set_stream_bw(&state->base,
					   crtc_state->dp_tunnel_ref.tunnel,
					   crtc->pipe, 0);
	drm_dp_tunnel_ref_put(&crtc_state->dp_tunnel_ref);
}

/**
 * intel_dp_tunnel_atomic_check_link - Check the DP tunnel atomic state
 * @state: intel atomic state
 * @limits: link BW limits
 *
 * Check the link configuration for all DP tunnels in @state. If the
 * configuration is invalid @limits will be updated if possible to
 * reduce the total BW, after which the configuration for all CRTCs in
 * @state must be recomputed with the updated @limits.
 *
 * Returns:
 *   - 0 if the confugration is valid
 *   - %-EAGAIN, if the configuration is invalid and @limits got updated
 *     with fallback values with which the configuration of all CRTCs in
 *     @state must be recomputed
 *   - Other negative error, if the configuration is invalid without a
 *     fallback possibility, or the check failed for another reason
 */
int intel_dp_tunnel_atomic_check_link(struct intel_atomic_state *state,
				      struct intel_link_bw_limits *limits)
{
	u32 failed_stream_mask;
	int err;

	err = drm_dp_tunnel_atomic_check_stream_bws(&state->base,
						    &failed_stream_mask);
	if (err != -ENOSPC)
		return err;

	err = intel_link_bw_reduce_bpp(state, limits,
				       failed_stream_mask, "DP tunnel link BW");

	return err ? : -EAGAIN;
}

static void atomic_decrease_bw(struct intel_atomic_state *state)
{
	struct intel_crtc *crtc;
	const struct intel_crtc_state *old_crtc_state;
	const struct intel_crtc_state *new_crtc_state;
	int i;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_dp_tunnel_state *new_tunnel_state;
		struct drm_dp_tunnel *tunnel;
		int old_bw;
		int new_bw;

		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		tunnel = get_inherited_tunnel(state, crtc);
		if (!tunnel)
			tunnel = old_crtc_state->dp_tunnel_ref.tunnel;

		if (!tunnel)
			continue;

		old_bw = drm_dp_tunnel_get_allocated_bw(tunnel);

		new_tunnel_state = drm_dp_tunnel_atomic_get_new_state(&state->base, tunnel);
		new_bw = drm_dp_tunnel_atomic_get_required_bw(new_tunnel_state);

		if (new_bw >= old_bw)
			continue;

		drm_dp_tunnel_alloc_bw(tunnel, new_bw);
	}
}

static void queue_retry_work(struct intel_atomic_state *state,
			     struct drm_dp_tunnel *tunnel,
			     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_encoder *encoder;

	encoder = intel_get_crtc_new_encoder(state, crtc_state);

	if (!intel_digital_port_connected(encoder))
		return;

	drm_dbg_kms(&i915->drm,
		    "[DPTUN %s][ENCODER:%d:%s] BW allocation failed on a connected sink\n",
		    drm_dp_tunnel_name(tunnel),
		    encoder->base.base.id,
		    encoder->base.name);

	intel_dp_queue_modeset_retry_for_link(state, encoder, crtc_state);
}

static void atomic_increase_bw(struct intel_atomic_state *state)
{
	struct intel_crtc *crtc;
	const struct intel_crtc_state *crtc_state;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		struct drm_dp_tunnel_state *tunnel_state;
		struct drm_dp_tunnel *tunnel = crtc_state->dp_tunnel_ref.tunnel;
		int bw;

		if (!intel_crtc_needs_modeset(crtc_state))
			continue;

		if (!tunnel)
			continue;

		tunnel_state = drm_dp_tunnel_atomic_get_new_state(&state->base, tunnel);

		bw = drm_dp_tunnel_atomic_get_required_bw(tunnel_state);

		if (drm_dp_tunnel_alloc_bw(tunnel, bw) != 0)
			queue_retry_work(state, tunnel, crtc_state);
	}
}

/**
 * intel_dp_tunnel_atomic_alloc_bw - Allocate the BW for all modeset tunnels
 * @state: Atomic state
 *
 * Allocate the required BW for all tunnels in @state.
 */
void intel_dp_tunnel_atomic_alloc_bw(struct intel_atomic_state *state)
{
	atomic_decrease_bw(state);
	atomic_increase_bw(state);
}

/**
 * intel_dp_tunnel_mgr_init - Initialize the DP tunnel manager
 * @i915: i915 device object
 *
 * Initialize the DP tunnel manager. The tunnel manager will support the
 * detection/management of DP tunnels on all DP connectors, so the function
 * must be called after all these connectors have been registered already.
 *
 * Return 0 in case of success, a negative error code otherwise.
 */
int intel_dp_tunnel_mgr_init(struct drm_i915_private *i915)
{
	struct drm_dp_tunnel_mgr *tunnel_mgr;
	struct drm_connector_list_iter connector_list_iter;
	struct intel_connector *connector;
	int dp_connectors = 0;

	drm_connector_list_iter_begin(&i915->drm, &connector_list_iter);
	for_each_intel_connector_iter(connector, &connector_list_iter) {
		if (connector->base.connector_type != DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		dp_connectors++;
	}
	drm_connector_list_iter_end(&connector_list_iter);

	tunnel_mgr = drm_dp_tunnel_mgr_create(&i915->drm, dp_connectors);
	if (IS_ERR(tunnel_mgr))
		return PTR_ERR(tunnel_mgr);

	i915->display.dp_tunnel_mgr = tunnel_mgr;

	return 0;
}

/**
 * intel_dp_tunnel_mgr_cleanup - Clean up the DP tunnel manager state
 * @i915: i915 device object
 *
 * Clean up the DP tunnel manager state.
 */
void intel_dp_tunnel_mgr_cleanup(struct drm_i915_private *i915)
{
	drm_dp_tunnel_mgr_destroy(i915->display.dp_tunnel_mgr);
	i915->display.dp_tunnel_mgr = NULL;
}
