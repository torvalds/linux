// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/pm_opp.h>
#include <linux/sort.h>
#include <linux/clk.h>
#include <linux/bitmap.h>

#include "dpu_kms.h"
#include "dpu_trace.h"
#include "dpu_crtc.h"
#include "dpu_core_perf.h"

/**
 * enum dpu_perf_mode - performance tuning mode
 * @DPU_PERF_MODE_NORMAL: performance controlled by user mode client
 * @DPU_PERF_MODE_MINIMUM: performance bounded by minimum setting
 * @DPU_PERF_MODE_FIXED: performance bounded by fixed setting
 * @DPU_PERF_MODE_MAX: maximum value, used for error checking
 */
enum dpu_perf_mode {
	DPU_PERF_MODE_NORMAL,
	DPU_PERF_MODE_MINIMUM,
	DPU_PERF_MODE_FIXED,
	DPU_PERF_MODE_MAX
};

/**
 * _dpu_core_perf_calc_bw() - to calculate BW per crtc
 * @perf_cfg: performance configuration
 * @crtc: pointer to a crtc
 * Return: returns aggregated BW for all planes in crtc.
 */
static u64 _dpu_core_perf_calc_bw(const struct dpu_perf_cfg *perf_cfg,
		struct drm_crtc *crtc)
{
	struct drm_plane *plane;
	struct dpu_plane_state *pstate;
	u64 crtc_plane_bw = 0;
	u32 bw_factor;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		pstate = to_dpu_plane_state(plane->state);
		if (!pstate)
			continue;

		crtc_plane_bw += pstate->plane_fetch_bw;
	}

	bw_factor = perf_cfg->bw_inefficiency_factor;
	if (bw_factor) {
		crtc_plane_bw *= bw_factor;
		do_div(crtc_plane_bw, 100);
	}

	return crtc_plane_bw;
}

/**
 * _dpu_core_perf_calc_clk() - to calculate clock per crtc
 * @perf_cfg: performance configuration
 * @crtc: pointer to a crtc
 * @state: pointer to a crtc state
 * Return: returns max clk for all planes in crtc.
 */
static u64 _dpu_core_perf_calc_clk(const struct dpu_perf_cfg *perf_cfg,
		struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct drm_plane *plane;
	struct dpu_plane_state *pstate;
	struct drm_display_mode *mode;
	u64 crtc_clk;
	u32 clk_factor;

	mode = &state->adjusted_mode;

	crtc_clk = mode->vtotal * mode->hdisplay * drm_mode_vrefresh(mode);

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		pstate = to_dpu_plane_state(plane->state);
		if (!pstate)
			continue;

		crtc_clk = max(pstate->plane_clk, crtc_clk);
	}

	clk_factor = perf_cfg->clk_inefficiency_factor;
	if (clk_factor) {
		crtc_clk *= clk_factor;
		do_div(crtc_clk, 100);
	}

	return crtc_clk;
}

static struct dpu_kms *_dpu_crtc_get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv;
	priv = crtc->dev->dev_private;
	return to_dpu_kms(priv->kms);
}

static void _dpu_core_perf_calc_crtc(const struct dpu_core_perf *core_perf,
				     struct drm_crtc *crtc,
				     struct drm_crtc_state *state,
				     struct dpu_core_perf_params *perf)
{
	const struct dpu_perf_cfg *perf_cfg = core_perf->perf_cfg;

	if (!perf_cfg || !crtc || !state || !perf) {
		DPU_ERROR("invalid parameters\n");
		return;
	}

	memset(perf, 0, sizeof(struct dpu_core_perf_params));

	if (core_perf->perf_tune.mode == DPU_PERF_MODE_MINIMUM) {
		perf->bw_ctl = 0;
		perf->max_per_pipe_ib = 0;
		perf->core_clk_rate = 0;
	} else if (core_perf->perf_tune.mode == DPU_PERF_MODE_FIXED) {
		perf->bw_ctl = core_perf->fix_core_ab_vote;
		perf->max_per_pipe_ib = core_perf->fix_core_ib_vote;
		perf->core_clk_rate = core_perf->fix_core_clk_rate;
	} else {
		perf->bw_ctl = _dpu_core_perf_calc_bw(perf_cfg, crtc);
		perf->max_per_pipe_ib = perf_cfg->min_dram_ib;
		perf->core_clk_rate = _dpu_core_perf_calc_clk(perf_cfg, crtc, state);
	}

	DRM_DEBUG_ATOMIC(
		"crtc=%d clk_rate=%llu core_ib=%llu core_ab=%llu\n",
			crtc->base.id, perf->core_clk_rate,
			perf->max_per_pipe_ib, perf->bw_ctl);
}

int dpu_core_perf_crtc_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	u32 bw, threshold;
	u64 bw_sum_of_intfs = 0;
	enum dpu_crtc_client_type curr_client_type;
	struct dpu_crtc_state *dpu_cstate;
	struct drm_crtc *tmp_crtc;
	struct dpu_kms *kms;

	if (!crtc || !state) {
		DPU_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	kms = _dpu_crtc_get_kms(crtc);

	/* we only need bandwidth check on real-time clients (interfaces) */
	if (dpu_crtc_get_client_type(crtc) == NRT_CLIENT)
		return 0;

	dpu_cstate = to_dpu_crtc_state(state);

	/* obtain new values */
	_dpu_core_perf_calc_crtc(&kms->perf, crtc, state, &dpu_cstate->new_perf);

	bw_sum_of_intfs = dpu_cstate->new_perf.bw_ctl;
	curr_client_type = dpu_crtc_get_client_type(crtc);

	drm_for_each_crtc(tmp_crtc, crtc->dev) {
		if (tmp_crtc->enabled &&
		    dpu_crtc_get_client_type(tmp_crtc) == curr_client_type &&
		    tmp_crtc != crtc) {
			struct dpu_crtc_state *tmp_cstate =
				to_dpu_crtc_state(tmp_crtc->state);

			DRM_DEBUG_ATOMIC("crtc:%d bw:%llu ctrl:%d\n",
					 tmp_crtc->base.id, tmp_cstate->new_perf.bw_ctl,
					 tmp_cstate->bw_control);

			bw_sum_of_intfs += tmp_cstate->new_perf.bw_ctl;
		}

		/* convert bandwidth to kb */
		bw = DIV_ROUND_UP_ULL(bw_sum_of_intfs, 1000);
		DRM_DEBUG_ATOMIC("calculated bandwidth=%uk\n", bw);

		threshold = kms->perf.perf_cfg->max_bw_high;

		DRM_DEBUG_ATOMIC("final threshold bw limit = %d\n", threshold);

		if (!threshold) {
			DPU_ERROR("no bandwidth limits specified\n");
			return -E2BIG;
		} else if (bw > threshold) {
			DPU_ERROR("exceeds bandwidth: %ukb > %ukb\n", bw,
					threshold);
			return -E2BIG;
		}
	}

	return 0;
}

static int _dpu_core_perf_crtc_update_bus(struct dpu_kms *kms,
		struct drm_crtc *crtc)
{
	struct dpu_core_perf_params perf = { 0 };
	enum dpu_crtc_client_type curr_client_type
					= dpu_crtc_get_client_type(crtc);
	struct drm_crtc *tmp_crtc;
	struct dpu_crtc_state *dpu_cstate;
	int i, ret = 0;
	u64 avg_bw;

	if (!kms->num_paths)
		return 0;

	drm_for_each_crtc(tmp_crtc, crtc->dev) {
		if (tmp_crtc->enabled &&
			curr_client_type ==
				dpu_crtc_get_client_type(tmp_crtc)) {
			dpu_cstate = to_dpu_crtc_state(tmp_crtc->state);

			perf.max_per_pipe_ib = max(perf.max_per_pipe_ib,
					dpu_cstate->new_perf.max_per_pipe_ib);

			perf.bw_ctl += dpu_cstate->new_perf.bw_ctl;

			DRM_DEBUG_ATOMIC("crtc=%d bw=%llu paths:%d\n",
				  tmp_crtc->base.id,
				  dpu_cstate->new_perf.bw_ctl, kms->num_paths);
		}
	}

	avg_bw = perf.bw_ctl;
	do_div(avg_bw, (kms->num_paths * 1000)); /*Bps_to_icc*/

	for (i = 0; i < kms->num_paths; i++)
		icc_set_bw(kms->path[i], avg_bw, perf.max_per_pipe_ib);

	return ret;
}

/**
 * dpu_core_perf_crtc_release_bw() - request zero bandwidth
 * @crtc: pointer to a crtc
 *
 * Function checks a state variable for the crtc, if all pending commit
 * requests are done, meaning no more bandwidth is needed, release
 * bandwidth request.
 */
void dpu_core_perf_crtc_release_bw(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_kms *kms;

	if (!crtc) {
		DPU_ERROR("invalid crtc\n");
		return;
	}

	kms = _dpu_crtc_get_kms(crtc);
	dpu_crtc = to_dpu_crtc(crtc);

	if (atomic_dec_return(&kms->bandwidth_ref) > 0)
		return;

	/* Release the bandwidth */
	if (kms->perf.enable_bw_release) {
		trace_dpu_cmd_release_bw(crtc->base.id);
		DRM_DEBUG_ATOMIC("Release BW crtc=%d\n", crtc->base.id);
		dpu_crtc->cur_perf.bw_ctl = 0;
		_dpu_core_perf_crtc_update_bus(kms, crtc);
	}
}

static u64 _dpu_core_perf_get_core_clk_rate(struct dpu_kms *kms)
{
	u64 clk_rate;
	struct drm_crtc *crtc;
	struct dpu_crtc_state *dpu_cstate;

	if (kms->perf.perf_tune.mode == DPU_PERF_MODE_FIXED)
		return kms->perf.fix_core_clk_rate;

	if (kms->perf.perf_tune.mode == DPU_PERF_MODE_MINIMUM)
		return kms->perf.max_core_clk_rate;

	clk_rate = 0;
	drm_for_each_crtc(crtc, kms->dev) {
		if (crtc->enabled) {
			dpu_cstate = to_dpu_crtc_state(crtc->state);
			clk_rate = max(dpu_cstate->new_perf.core_clk_rate,
							clk_rate);
		}
	}

	return clk_rate;
}

int dpu_core_perf_crtc_update(struct drm_crtc *crtc,
			      int params_changed)
{
	struct dpu_core_perf_params *new, *old;
	bool update_bus = false, update_clk = false;
	u64 clk_rate = 0;
	struct dpu_crtc *dpu_crtc;
	struct dpu_crtc_state *dpu_cstate;
	struct dpu_kms *kms;
	int ret;

	if (!crtc) {
		DPU_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	kms = _dpu_crtc_get_kms(crtc);

	dpu_crtc = to_dpu_crtc(crtc);
	dpu_cstate = to_dpu_crtc_state(crtc->state);

	DRM_DEBUG_ATOMIC("crtc:%d enabled:%d core_clk:%llu\n",
			crtc->base.id, crtc->enabled, kms->perf.core_clk_rate);

	old = &dpu_crtc->cur_perf;
	new = &dpu_cstate->new_perf;

	if (crtc->enabled) {
		/*
		 * cases for bus bandwidth update.
		 * 1. new bandwidth vote - "ab or ib vote" is higher
		 *    than current vote for update request.
		 * 2. new bandwidth vote - "ab or ib vote" is lower
		 *    than current vote at end of commit or stop.
		 */
		if ((params_changed && ((new->bw_ctl > old->bw_ctl) ||
			(new->max_per_pipe_ib > old->max_per_pipe_ib)))	||
			(!params_changed && ((new->bw_ctl < old->bw_ctl) ||
			(new->max_per_pipe_ib < old->max_per_pipe_ib)))) {
			DRM_DEBUG_ATOMIC("crtc=%d p=%d new_bw=%llu,old_bw=%llu\n",
				crtc->base.id, params_changed,
				new->bw_ctl, old->bw_ctl);
			old->bw_ctl = new->bw_ctl;
			old->max_per_pipe_ib = new->max_per_pipe_ib;
			update_bus = true;
		}

		if ((params_changed && new->core_clk_rate > old->core_clk_rate) ||
		    (!params_changed && new->core_clk_rate < old->core_clk_rate)) {
			old->core_clk_rate = new->core_clk_rate;
			update_clk = true;
		}
	} else {
		DRM_DEBUG_ATOMIC("crtc=%d disable\n", crtc->base.id);
		memset(old, 0, sizeof(*old));
		update_bus = true;
		update_clk = true;
	}

	trace_dpu_perf_crtc_update(crtc->base.id, new->bw_ctl,
		new->core_clk_rate, !crtc->enabled, update_bus, update_clk);

	if (update_bus) {
		ret = _dpu_core_perf_crtc_update_bus(kms, crtc);
		if (ret) {
			DPU_ERROR("crtc-%d: failed to update bus bw vote\n",
				  crtc->base.id);
			return ret;
		}
	}

	/*
	 * Update the clock after bandwidth vote to ensure
	 * bandwidth is available before clock rate is increased.
	 */
	if (update_clk) {
		clk_rate = _dpu_core_perf_get_core_clk_rate(kms);

		DRM_DEBUG_ATOMIC("clk:%llu\n", clk_rate);

		trace_dpu_core_perf_update_clk(kms->dev, !crtc->enabled, clk_rate);

		clk_rate = min(clk_rate, kms->perf.max_core_clk_rate);
		ret = dev_pm_opp_set_rate(&kms->pdev->dev, clk_rate);
		if (ret) {
			DPU_ERROR("failed to set core clock rate %llu\n", clk_rate);
			return ret;
		}

		kms->perf.core_clk_rate = clk_rate;
		DRM_DEBUG_ATOMIC("update clk rate = %lld HZ\n", clk_rate);
	}
	return 0;
}

#ifdef CONFIG_DEBUG_FS

static ssize_t _dpu_core_perf_mode_write(struct file *file,
		    const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dpu_core_perf *perf = file->private_data;
	u32 perf_mode = 0;
	int ret;

	ret = kstrtouint_from_user(user_buf, count, 0, &perf_mode);
	if (ret)
		return ret;

	if (perf_mode >= DPU_PERF_MODE_MAX)
		return -EINVAL;

	if (perf_mode == DPU_PERF_MODE_FIXED) {
		DRM_INFO("fix performance mode\n");
	} else if (perf_mode == DPU_PERF_MODE_MINIMUM) {
		/* run the driver with max clk and BW vote */
		DRM_INFO("minimum performance mode\n");
	} else if (perf_mode == DPU_PERF_MODE_NORMAL) {
		/* reset the perf tune params to 0 */
		DRM_INFO("normal performance mode\n");
	}
	perf->perf_tune.mode = perf_mode;

	return count;
}

static ssize_t _dpu_core_perf_mode_read(struct file *file,
			char __user *buff, size_t count, loff_t *ppos)
{
	struct dpu_core_perf *perf = file->private_data;
	int len;
	char buf[128];

	len = scnprintf(buf, sizeof(buf),
			"mode %d\n",
			perf->perf_tune.mode);

	return simple_read_from_buffer(buff, count, ppos, buf, len);
}

static const struct file_operations dpu_core_perf_mode_fops = {
	.open = simple_open,
	.read = _dpu_core_perf_mode_read,
	.write = _dpu_core_perf_mode_write,
};

int dpu_core_perf_debugfs_init(struct dpu_kms *dpu_kms, struct dentry *parent)
{
	struct dpu_core_perf *perf = &dpu_kms->perf;
	struct dentry *entry;

	entry = debugfs_create_dir("core_perf", parent);

	debugfs_create_u64("max_core_clk_rate", 0600, entry,
			&perf->max_core_clk_rate);
	debugfs_create_u64("core_clk_rate", 0600, entry,
			&perf->core_clk_rate);
	debugfs_create_u32("enable_bw_release", 0600, entry,
			(u32 *)&perf->enable_bw_release);
	debugfs_create_u32("threshold_low", 0600, entry,
			(u32 *)&perf->perf_cfg->max_bw_low);
	debugfs_create_u32("threshold_high", 0600, entry,
			(u32 *)&perf->perf_cfg->max_bw_high);
	debugfs_create_u32("min_core_ib", 0600, entry,
			(u32 *)&perf->perf_cfg->min_core_ib);
	debugfs_create_u32("min_llcc_ib", 0600, entry,
			(u32 *)&perf->perf_cfg->min_llcc_ib);
	debugfs_create_u32("min_dram_ib", 0600, entry,
			(u32 *)&perf->perf_cfg->min_dram_ib);
	debugfs_create_file("perf_mode", 0600, entry,
			(u32 *)perf, &dpu_core_perf_mode_fops);
	debugfs_create_u64("fix_core_clk_rate", 0600, entry,
			&perf->fix_core_clk_rate);
	debugfs_create_u64("fix_core_ib_vote", 0600, entry,
			&perf->fix_core_ib_vote);
	debugfs_create_u64("fix_core_ab_vote", 0600, entry,
			&perf->fix_core_ab_vote);

	return 0;
}
#endif

int dpu_core_perf_init(struct dpu_core_perf *perf,
		const struct dpu_perf_cfg *perf_cfg,
		unsigned long max_core_clk_rate)
{
	perf->perf_cfg = perf_cfg;
	perf->max_core_clk_rate = max_core_clk_rate;

	return 0;
}
