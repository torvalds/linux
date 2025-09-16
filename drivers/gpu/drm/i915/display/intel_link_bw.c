// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/int_log.h>
#include <linux/math.h>

#include <drm/drm_fixed.h>
#include <drm/drm_print.h>

#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_mst.h"
#include "intel_dp_tunnel.h"
#include "intel_fdi.h"
#include "intel_link_bw.h"

static int get_forced_link_bpp_x16(struct intel_atomic_state *state,
				   const struct intel_crtc *crtc)
{
	struct intel_digital_connector_state *conn_state;
	struct intel_connector *connector;
	int force_bpp_x16 = INT_MAX;
	int i;

	for_each_new_intel_connector_in_state(state, connector, conn_state, i) {
		if (conn_state->base.crtc != &crtc->base)
			continue;

		if (!connector->link.force_bpp_x16)
			continue;

		force_bpp_x16 = min(force_bpp_x16, connector->link.force_bpp_x16);
	}

	return force_bpp_x16 < INT_MAX ? force_bpp_x16 : 0;
}

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
		struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
		const struct intel_crtc_state *crtc_state =
			intel_atomic_get_new_crtc_state(state, crtc);
		int forced_bpp_x16 = get_forced_link_bpp_x16(state, crtc);

		if (state->base.duplicated && crtc_state) {
			limits->max_bpp_x16[pipe] = crtc_state->max_link_bpp_x16;
			if (crtc_state->fec_enable)
				limits->force_fec_pipes |= BIT(pipe);
		} else {
			limits->max_bpp_x16[pipe] = INT_MAX;
		}

		if (forced_bpp_x16)
			limits->max_bpp_x16[pipe] = min(limits->max_bpp_x16[pipe], forced_bpp_x16);
	}
}

/**
 * __intel_link_bw_reduce_bpp - reduce maximum link bpp for a selected pipe
 * @state: atomic state
 * @limits: link BW limits
 * @pipe_mask: mask of pipes to select from
 * @reason: explanation of why bpp reduction is needed
 * @reduce_forced_bpp: allow reducing bpps below their forced link bpp
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
static int __intel_link_bw_reduce_bpp(struct intel_atomic_state *state,
				      struct intel_link_bw_limits *limits,
				      u8 pipe_mask,
				      const char *reason,
				      bool reduce_forced_bpp)
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

		if (!reduce_forced_bpp &&
		    link_bpp_x16 <= get_forced_link_bpp_x16(state, crtc))
			continue;

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

int intel_link_bw_reduce_bpp(struct intel_atomic_state *state,
			     struct intel_link_bw_limits *limits,
			     u8 pipe_mask,
			     const char *reason)
{
	int ret;

	/* Try to keep any forced link BPP. */
	ret = __intel_link_bw_reduce_bpp(state, limits, pipe_mask, reason, false);
	if (ret == -ENOSPC)
		ret = __intel_link_bw_reduce_bpp(state, limits, pipe_mask, reason, true);

	return ret;
}

/**
 * intel_link_bw_compute_pipe_bpp - compute pipe bpp limited by max link bpp
 * @crtc_state: the crtc state
 *
 * Compute the pipe bpp limited by the CRTC's maximum link bpp. Encoders can
 * call this function during state computation in the simple case where the
 * link bpp will always match the pipe bpp. This is the case for all non-DP
 * encoders, while DP encoders will use a link bpp lower than pipe bpp in case
 * of DSC compression.
 *
 * Returns %true in case of success, %false if pipe bpp would need to be
 * reduced below its valid range.
 */
bool intel_link_bw_compute_pipe_bpp(struct intel_crtc_state *crtc_state)
{
	int pipe_bpp = min(crtc_state->pipe_bpp,
			   fxp_q4_to_int(crtc_state->max_link_bpp_x16));

	pipe_bpp = rounddown(pipe_bpp, 2 * 3);

	if (pipe_bpp < 6 * 3)
		return false;

	crtc_state->pipe_bpp = pipe_bpp;

	return true;
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
 *   - 0 if the configuration is valid
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

static int force_link_bpp_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;

	seq_printf(m, FXP_Q4_FMT "\n", FXP_Q4_ARGS(connector->link.force_bpp_x16));

	return 0;
}

static int str_to_fxp_q4_nonneg_int(const char *str, int *val_x16)
{
	unsigned int val;
	int err;

	err = kstrtouint(str, 10, &val);
	if (err)
		return err;

	if (val > INT_MAX >> 4)
		return -ERANGE;

	*val_x16 = fxp_q4_from_int(val);

	return 0;
}

/* modifies str */
static int str_to_fxp_q4_nonneg(char *str, int *val_x16)
{
	const char *int_str;
	char *frac_str;
	int frac_digits;
	int frac_val;
	int err;

	int_str = strim(str);
	frac_str = strchr(int_str, '.');

	if (frac_str)
		*frac_str++ = '\0';

	err = str_to_fxp_q4_nonneg_int(int_str, val_x16);
	if (err)
		return err;

	if (!frac_str)
		return 0;

	/* prevent negative number/leading +- sign mark */
	if (!isdigit(*frac_str))
		return -EINVAL;

	err = str_to_fxp_q4_nonneg_int(frac_str, &frac_val);
	if (err)
		return err;

	frac_digits = strlen(frac_str);
	if (frac_digits > intlog10(INT_MAX) >> 24 ||
	    frac_val > INT_MAX - int_pow(10, frac_digits) / 2)
		return -ERANGE;

	frac_val = DIV_ROUND_CLOSEST(frac_val, (int)int_pow(10, frac_digits));

	if (*val_x16 > INT_MAX - frac_val)
		return -ERANGE;

	*val_x16 += frac_val;

	return 0;
}

static int user_str_to_fxp_q4_nonneg(const char __user *ubuf, size_t len, int *val_x16)
{
	char *kbuf;
	int err;

	kbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	err = str_to_fxp_q4_nonneg(kbuf, val_x16);

	kfree(kbuf);

	return err;
}

static bool connector_supports_dsc(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);

	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_eDP:
		return intel_dp_has_dsc(connector);
	case DRM_MODE_CONNECTOR_DisplayPort:
		if (connector->mst.dp)
			return HAS_DSC_MST(display);

		return HAS_DSC(display);
	default:
		return false;
	}
}

static ssize_t
force_link_bpp_write(struct file *file, const char __user *ubuf, size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	int min_bpp;
	int bpp_x16;
	int err;

	err = user_str_to_fxp_q4_nonneg(ubuf, len, &bpp_x16);
	if (err)
		return err;

	/* TODO: Make the non-DSC min_bpp value connector specific. */
	if (connector_supports_dsc(connector))
		min_bpp = intel_dp_dsc_min_src_compressed_bpp();
	else
		min_bpp = intel_display_min_pipe_bpp();

	if (bpp_x16 &&
	    (bpp_x16 < fxp_q4_from_int(min_bpp) ||
	     bpp_x16 > fxp_q4_from_int(intel_display_max_pipe_bpp(display))))
		return -EINVAL;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	connector->link.force_bpp_x16 = bpp_x16;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	*offp += len;

	return len;
}
DEFINE_SHOW_STORE_ATTRIBUTE(force_link_bpp);

void intel_link_bw_connector_debugfs_add(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct dentry *root = connector->base.debugfs_entry;

	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
	case DRM_MODE_CONNECTOR_HDMIA:
		break;
	case DRM_MODE_CONNECTOR_VGA:
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_LVDS:
	case DRM_MODE_CONNECTOR_DVID:
		if (HAS_FDI(display))
			break;

		return;
	default:
		return;
	}

	debugfs_create_file("intel_force_link_bpp", 0644, root,
			    connector, &force_link_bpp_fops);
}
