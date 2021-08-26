// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <drm/drm_atomic_state_helper.h>

#include "intel_atomic.h"
#include "intel_bw.h"
#include "intel_cdclk.h"
#include "intel_display_types.h"
#include "intel_pm.h"
#include "intel_sideband.h"

/* Parameters for Qclk Geyserville (QGV) */
struct intel_qgv_point {
	u16 dclk, t_rp, t_rdpre, t_rc, t_ras, t_rcd;
};

struct intel_qgv_info {
	struct intel_qgv_point points[I915_NUM_QGV_POINTS];
	u8 num_points;
	u8 t_bl;
};

static int icl_pcode_read_qgv_point_info(struct drm_i915_private *dev_priv,
					 struct intel_qgv_point *sp,
					 int point)
{
	u32 val = 0, val2 = 0;
	int ret;

	ret = sandybridge_pcode_read(dev_priv,
				     ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
				     ICL_PCODE_MEM_SS_READ_QGV_POINT_INFO(point),
				     &val, &val2);
	if (ret)
		return ret;

	sp->dclk = val & 0xffff;
	sp->t_rp = (val & 0xff0000) >> 16;
	sp->t_rcd = (val & 0xff000000) >> 24;

	sp->t_rdpre = val2 & 0xff;
	sp->t_ras = (val2 & 0xff00) >> 8;

	sp->t_rc = sp->t_rp + sp->t_ras;

	return 0;
}

int icl_pcode_restrict_qgv_points(struct drm_i915_private *dev_priv,
				  u32 points_mask)
{
	int ret;

	/* bspec says to keep retrying for at least 1 ms */
	ret = skl_pcode_request(dev_priv, ICL_PCODE_SAGV_DE_MEM_SS_CONFIG,
				points_mask,
				ICL_PCODE_POINTS_RESTRICTED_MASK,
				ICL_PCODE_POINTS_RESTRICTED,
				1);

	if (ret < 0) {
		drm_err(&dev_priv->drm, "Failed to disable qgv points (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int icl_get_qgv_points(struct drm_i915_private *dev_priv,
			      struct intel_qgv_info *qi)
{
	const struct dram_info *dram_info = &dev_priv->dram_info;
	int i, ret;

	qi->num_points = dram_info->num_qgv_points;

	if (DISPLAY_VER(dev_priv) == 12)
		switch (dram_info->type) {
		case INTEL_DRAM_DDR4:
			qi->t_bl = 4;
			break;
		case INTEL_DRAM_DDR5:
			qi->t_bl = 8;
			break;
		default:
			qi->t_bl = 16;
			break;
		}
	else if (DISPLAY_VER(dev_priv) == 11)
		qi->t_bl = dev_priv->dram_info.type == INTEL_DRAM_DDR4 ? 4 : 8;

	if (drm_WARN_ON(&dev_priv->drm,
			qi->num_points > ARRAY_SIZE(qi->points)))
		qi->num_points = ARRAY_SIZE(qi->points);

	for (i = 0; i < qi->num_points; i++) {
		struct intel_qgv_point *sp = &qi->points[i];

		ret = icl_pcode_read_qgv_point_info(dev_priv, sp, i);
		if (ret)
			return ret;

		drm_dbg_kms(&dev_priv->drm,
			    "QGV %d: DCLK=%d tRP=%d tRDPRE=%d tRAS=%d tRCD=%d tRC=%d\n",
			    i, sp->dclk, sp->t_rp, sp->t_rdpre, sp->t_ras,
			    sp->t_rcd, sp->t_rc);
	}

	return 0;
}

static int icl_calc_bw(int dclk, int num, int den)
{
	/* multiples of 16.666MHz (100/6) */
	return DIV_ROUND_CLOSEST(num * dclk * 100, den * 6);
}

static int icl_sagv_max_dclk(const struct intel_qgv_info *qi)
{
	u16 dclk = 0;
	int i;

	for (i = 0; i < qi->num_points; i++)
		dclk = max(dclk, qi->points[i].dclk);

	return dclk;
}

struct intel_sa_info {
	u16 displayrtids;
	u8 deburst, deprogbwlimit;
};

static const struct intel_sa_info icl_sa_info = {
	.deburst = 8,
	.deprogbwlimit = 25, /* GB/s */
	.displayrtids = 128,
};

static const struct intel_sa_info tgl_sa_info = {
	.deburst = 16,
	.deprogbwlimit = 34, /* GB/s */
	.displayrtids = 256,
};

static const struct intel_sa_info rkl_sa_info = {
	.deburst = 16,
	.deprogbwlimit = 20, /* GB/s */
	.displayrtids = 128,
};

static const struct intel_sa_info adls_sa_info = {
	.deburst = 16,
	.deprogbwlimit = 38, /* GB/s */
	.displayrtids = 256,
};

static int icl_get_bw_info(struct drm_i915_private *dev_priv, const struct intel_sa_info *sa)
{
	struct intel_qgv_info qi = {};
	bool is_y_tile = true; /* assume y tile may be used */
	int num_channels = max_t(u8, 1, dev_priv->dram_info.num_channels);
	int deinterleave;
	int ipqdepth, ipqdepthpch;
	int dclk_max;
	int maxdebw;
	int i, ret;

	ret = icl_get_qgv_points(dev_priv, &qi);
	if (ret) {
		drm_dbg_kms(&dev_priv->drm,
			    "Failed to get memory subsystem information, ignoring bandwidth limits");
		return ret;
	}

	deinterleave = DIV_ROUND_UP(num_channels, is_y_tile ? 4 : 2);
	dclk_max = icl_sagv_max_dclk(&qi);

	ipqdepthpch = 16;

	maxdebw = min(sa->deprogbwlimit * 1000,
		      icl_calc_bw(dclk_max, 16, 1) * 6 / 10); /* 60% */
	ipqdepth = min(ipqdepthpch, sa->displayrtids / num_channels);

	for (i = 0; i < ARRAY_SIZE(dev_priv->max_bw); i++) {
		struct intel_bw_info *bi = &dev_priv->max_bw[i];
		int clpchgroup;
		int j;

		clpchgroup = (sa->deburst * deinterleave / num_channels) << i;
		bi->num_planes = (ipqdepth - clpchgroup) / clpchgroup + 1;

		bi->num_qgv_points = qi.num_points;

		for (j = 0; j < qi.num_points; j++) {
			const struct intel_qgv_point *sp = &qi.points[j];
			int ct, bw;

			/*
			 * Max row cycle time
			 *
			 * FIXME what is the logic behind the
			 * assumed burst length?
			 */
			ct = max_t(int, sp->t_rc, sp->t_rp + sp->t_rcd +
				   (clpchgroup - 1) * qi.t_bl + sp->t_rdpre);
			bw = icl_calc_bw(sp->dclk, clpchgroup * 32 * num_channels, ct);

			bi->deratedbw[j] = min(maxdebw,
					       bw * 9 / 10); /* 90% */

			drm_dbg_kms(&dev_priv->drm,
				    "BW%d / QGV %d: num_planes=%d deratedbw=%u\n",
				    i, j, bi->num_planes, bi->deratedbw[j]);
		}

		if (bi->num_planes == 1)
			break;
	}

	/*
	 * In case if SAGV is disabled in BIOS, we always get 1
	 * SAGV point, but we can't send PCode commands to restrict it
	 * as it will fail and pointless anyway.
	 */
	if (qi.num_points == 1)
		dev_priv->sagv_status = I915_SAGV_NOT_CONTROLLED;
	else
		dev_priv->sagv_status = I915_SAGV_ENABLED;

	return 0;
}

static unsigned int icl_max_bw(struct drm_i915_private *dev_priv,
			       int num_planes, int qgv_point)
{
	int i;

	/*
	 * Let's return max bw for 0 planes
	 */
	num_planes = max(1, num_planes);

	for (i = 0; i < ARRAY_SIZE(dev_priv->max_bw); i++) {
		const struct intel_bw_info *bi =
			&dev_priv->max_bw[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= bi->num_qgv_points)
			return UINT_MAX;

		if (num_planes >= bi->num_planes)
			return bi->deratedbw[qgv_point];
	}

	return 0;
}

void intel_bw_init_hw(struct drm_i915_private *dev_priv)
{
	if (!HAS_DISPLAY(dev_priv))
		return;

	if (IS_ALDERLAKE_S(dev_priv) || IS_ALDERLAKE_P(dev_priv))
		icl_get_bw_info(dev_priv, &adls_sa_info);
	else if (IS_ROCKETLAKE(dev_priv))
		icl_get_bw_info(dev_priv, &rkl_sa_info);
	else if (DISPLAY_VER(dev_priv) == 12)
		icl_get_bw_info(dev_priv, &tgl_sa_info);
	else if (DISPLAY_VER(dev_priv) == 11)
		icl_get_bw_info(dev_priv, &icl_sa_info);
}

static unsigned int intel_bw_crtc_num_active_planes(const struct intel_crtc_state *crtc_state)
{
	/*
	 * We assume cursors are small enough
	 * to not not cause bandwidth problems.
	 */
	return hweight8(crtc_state->active_planes & ~BIT(PLANE_CURSOR));
}

static unsigned int intel_bw_crtc_data_rate(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	unsigned int data_rate = 0;
	enum plane_id plane_id;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		/*
		 * We assume cursors are small enough
		 * to not not cause bandwidth problems.
		 */
		if (plane_id == PLANE_CURSOR)
			continue;

		data_rate += crtc_state->data_rate[plane_id];
	}

	return data_rate;
}

void intel_bw_crtc_update(struct intel_bw_state *bw_state,
			  const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	bw_state->data_rate[crtc->pipe] =
		intel_bw_crtc_data_rate(crtc_state);
	bw_state->num_active_planes[crtc->pipe] =
		intel_bw_crtc_num_active_planes(crtc_state);

	drm_dbg_kms(&i915->drm, "pipe %c data rate %u num active planes %u\n",
		    pipe_name(crtc->pipe),
		    bw_state->data_rate[crtc->pipe],
		    bw_state->num_active_planes[crtc->pipe]);
}

static unsigned int intel_bw_num_active_planes(struct drm_i915_private *dev_priv,
					       const struct intel_bw_state *bw_state)
{
	unsigned int num_active_planes = 0;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		num_active_planes += bw_state->num_active_planes[pipe];

	return num_active_planes;
}

static unsigned int intel_bw_data_rate(struct drm_i915_private *dev_priv,
				       const struct intel_bw_state *bw_state)
{
	unsigned int data_rate = 0;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		data_rate += bw_state->data_rate[pipe];

	if (DISPLAY_VER(dev_priv) >= 13 && intel_vtd_active())
		data_rate = data_rate * 105 / 100;

	return data_rate;
}

struct intel_bw_state *
intel_atomic_get_old_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_old_global_obj_state(state, &dev_priv->bw_obj);

	return to_intel_bw_state(bw_state);
}

struct intel_bw_state *
intel_atomic_get_new_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_new_global_obj_state(state, &dev_priv->bw_obj);

	return to_intel_bw_state(bw_state);
}

struct intel_bw_state *
intel_atomic_get_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_global_obj_state(state, &dev_priv->bw_obj);
	if (IS_ERR(bw_state))
		return ERR_CAST(bw_state);

	return to_intel_bw_state(bw_state);
}

int skl_bw_calc_min_cdclk(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_bw_state *new_bw_state = NULL;
	struct intel_bw_state *old_bw_state = NULL;
	const struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	int max_bw = 0;
	enum pipe pipe;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		enum plane_id plane_id;
		struct intel_dbuf_bw *crtc_bw;

		new_bw_state = intel_atomic_get_bw_state(state);
		if (IS_ERR(new_bw_state))
			return PTR_ERR(new_bw_state);

		old_bw_state = intel_atomic_get_old_bw_state(state);

		crtc_bw = &new_bw_state->dbuf_bw[crtc->pipe];

		memset(&crtc_bw->used_bw, 0, sizeof(crtc_bw->used_bw));

		if (!crtc_state->hw.active)
			continue;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			const struct skl_ddb_entry *plane_alloc =
				&crtc_state->wm.skl.plane_ddb_y[plane_id];
			const struct skl_ddb_entry *uv_plane_alloc =
				&crtc_state->wm.skl.plane_ddb_uv[plane_id];
			unsigned int data_rate = crtc_state->data_rate[plane_id];
			unsigned int dbuf_mask = 0;
			enum dbuf_slice slice;

			dbuf_mask |= skl_ddb_dbuf_slice_mask(dev_priv, plane_alloc);
			dbuf_mask |= skl_ddb_dbuf_slice_mask(dev_priv, uv_plane_alloc);

			/*
			 * FIXME: To calculate that more properly we probably
			 * need to to split per plane data_rate into data_rate_y
			 * and data_rate_uv for multiplanar formats in order not
			 * to get accounted those twice if they happen to reside
			 * on different slices.
			 * However for pre-icl this would work anyway because
			 * we have only single slice and for icl+ uv plane has
			 * non-zero data rate.
			 * So in worst case those calculation are a bit
			 * pessimistic, which shouldn't pose any significant
			 * problem anyway.
			 */
			for_each_dbuf_slice_in_mask(dev_priv, slice, dbuf_mask)
				crtc_bw->used_bw[slice] += data_rate;
		}
	}

	if (!old_bw_state)
		return 0;

	for_each_pipe(dev_priv, pipe) {
		struct intel_dbuf_bw *crtc_bw;
		enum dbuf_slice slice;

		crtc_bw = &new_bw_state->dbuf_bw[pipe];

		for_each_dbuf_slice(dev_priv, slice) {
			/*
			 * Current experimental observations show that contrary
			 * to BSpec we get underruns once we exceed 64 * CDCLK
			 * for slices in total.
			 * As a temporary measure in order not to keep CDCLK
			 * bumped up all the time we calculate CDCLK according
			 * to this formula for  overall bw consumed by slices.
			 */
			max_bw += crtc_bw->used_bw[slice];
		}
	}

	new_bw_state->min_cdclk = max_bw / 64;

	if (new_bw_state->min_cdclk != old_bw_state->min_cdclk) {
		int ret = intel_atomic_lock_global_state(&new_bw_state->base);

		if (ret)
			return ret;
	}

	return 0;
}

int intel_bw_calc_min_cdclk(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_bw_state *new_bw_state = NULL;
	struct intel_bw_state *old_bw_state = NULL;
	const struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	int min_cdclk = 0;
	enum pipe pipe;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		new_bw_state = intel_atomic_get_bw_state(state);
		if (IS_ERR(new_bw_state))
			return PTR_ERR(new_bw_state);

		old_bw_state = intel_atomic_get_old_bw_state(state);
	}

	if (!old_bw_state)
		return 0;

	for_each_pipe(dev_priv, pipe) {
		struct intel_cdclk_state *cdclk_state;

		cdclk_state = intel_atomic_get_new_cdclk_state(state);
		if (!cdclk_state)
			return 0;

		min_cdclk = max(cdclk_state->min_cdclk[pipe], min_cdclk);
	}

	new_bw_state->min_cdclk = min_cdclk;

	if (new_bw_state->min_cdclk != old_bw_state->min_cdclk) {
		int ret = intel_atomic_lock_global_state(&new_bw_state->base);

		if (ret)
			return ret;
	}

	return 0;
}

int intel_bw_atomic_check(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_bw_state *new_bw_state = NULL;
	const struct intel_bw_state *old_bw_state = NULL;
	unsigned int data_rate;
	unsigned int num_active_planes;
	struct intel_crtc *crtc;
	int i, ret;
	u32 allowed_points = 0;
	unsigned int max_bw_point = 0, max_bw = 0;
	unsigned int num_qgv_points = dev_priv->max_bw[0].num_qgv_points;
	u32 mask = (1 << num_qgv_points) - 1;

	/* FIXME earlier gens need some checks too */
	if (DISPLAY_VER(dev_priv) < 11)
		return 0;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		unsigned int old_data_rate =
			intel_bw_crtc_data_rate(old_crtc_state);
		unsigned int new_data_rate =
			intel_bw_crtc_data_rate(new_crtc_state);
		unsigned int old_active_planes =
			intel_bw_crtc_num_active_planes(old_crtc_state);
		unsigned int new_active_planes =
			intel_bw_crtc_num_active_planes(new_crtc_state);

		/*
		 * Avoid locking the bw state when
		 * nothing significant has changed.
		 */
		if (old_data_rate == new_data_rate &&
		    old_active_planes == new_active_planes)
			continue;

		new_bw_state = intel_atomic_get_bw_state(state);
		if (IS_ERR(new_bw_state))
			return PTR_ERR(new_bw_state);

		new_bw_state->data_rate[crtc->pipe] = new_data_rate;
		new_bw_state->num_active_planes[crtc->pipe] = new_active_planes;

		drm_dbg_kms(&dev_priv->drm,
			    "pipe %c data rate %u num active planes %u\n",
			    pipe_name(crtc->pipe),
			    new_bw_state->data_rate[crtc->pipe],
			    new_bw_state->num_active_planes[crtc->pipe]);
	}

	if (!new_bw_state)
		return 0;

	ret = intel_atomic_lock_global_state(&new_bw_state->base);
	if (ret)
		return ret;

	data_rate = intel_bw_data_rate(dev_priv, new_bw_state);
	data_rate = DIV_ROUND_UP(data_rate, 1000);

	num_active_planes = intel_bw_num_active_planes(dev_priv, new_bw_state);

	for (i = 0; i < num_qgv_points; i++) {
		unsigned int max_data_rate;

		max_data_rate = icl_max_bw(dev_priv, num_active_planes, i);
		/*
		 * We need to know which qgv point gives us
		 * maximum bandwidth in order to disable SAGV
		 * if we find that we exceed SAGV block time
		 * with watermarks. By that moment we already
		 * have those, as it is calculated earlier in
		 * intel_atomic_check,
		 */
		if (max_data_rate > max_bw) {
			max_bw_point = i;
			max_bw = max_data_rate;
		}
		if (max_data_rate >= data_rate)
			allowed_points |= BIT(i);
		drm_dbg_kms(&dev_priv->drm, "QGV point %d: max bw %d required %d\n",
			    i, max_data_rate, data_rate);
	}

	/*
	 * BSpec states that we always should have at least one allowed point
	 * left, so if we couldn't - simply reject the configuration for obvious
	 * reasons.
	 */
	if (allowed_points == 0) {
		drm_dbg_kms(&dev_priv->drm, "No QGV points provide sufficient memory"
			    " bandwidth %d for display configuration(%d active planes).\n",
			    data_rate, num_active_planes);
		return -EINVAL;
	}

	/*
	 * Leave only single point with highest bandwidth, if
	 * we can't enable SAGV due to the increased memory latency it may
	 * cause.
	 */
	if (!intel_can_enable_sagv(dev_priv, new_bw_state)) {
		allowed_points = BIT(max_bw_point);
		drm_dbg_kms(&dev_priv->drm, "No SAGV, using single QGV point %d\n",
			    max_bw_point);
	}
	/*
	 * We store the ones which need to be masked as that is what PCode
	 * actually accepts as a parameter.
	 */
	new_bw_state->qgv_points_mask = ~allowed_points & mask;

	old_bw_state = intel_atomic_get_old_bw_state(state);
	/*
	 * If the actual mask had changed we need to make sure that
	 * the commits are serialized(in case this is a nomodeset, nonblocking)
	 */
	if (new_bw_state->qgv_points_mask != old_bw_state->qgv_points_mask) {
		ret = intel_atomic_serialize_global_state(&new_bw_state->base);
		if (ret)
			return ret;
	}

	return 0;
}

static struct intel_global_state *
intel_bw_duplicate_state(struct intel_global_obj *obj)
{
	struct intel_bw_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	return &state->base;
}

static void intel_bw_destroy_state(struct intel_global_obj *obj,
				   struct intel_global_state *state)
{
	kfree(state);
}

static const struct intel_global_state_funcs intel_bw_funcs = {
	.atomic_duplicate_state = intel_bw_duplicate_state,
	.atomic_destroy_state = intel_bw_destroy_state,
};

int intel_bw_init(struct drm_i915_private *dev_priv)
{
	struct intel_bw_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	intel_atomic_global_obj_init(dev_priv, &dev_priv->bw_obj,
				     &state->base, &intel_bw_funcs);

	return 0;
}
