// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <drm/drm_atomic_state_helper.h>

#include "intel_bw.h"
#include "intel_display_types.h"
#include "intel_sideband.h"

/* Parameters for Qclk Geyserville (QGV) */
struct intel_qgv_point {
	u16 dclk, t_rp, t_rdpre, t_rc, t_ras, t_rcd;
};

struct intel_qgv_info {
	struct intel_qgv_point points[I915_NUM_QGV_POINTS];
	u8 num_points;
	u8 num_channels;
	u8 t_bl;
	enum intel_dram_type dram_type;
};

static int icl_pcode_read_mem_global_info(struct drm_i915_private *dev_priv,
					  struct intel_qgv_info *qi)
{
	u32 val = 0;
	int ret;

	ret = sandybridge_pcode_read(dev_priv,
				     ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
				     ICL_PCODE_MEM_SS_READ_GLOBAL_INFO,
				     &val, NULL);
	if (ret)
		return ret;

	if (IS_GEN(dev_priv, 12)) {
		switch (val & 0xf) {
		case 0:
			qi->dram_type = INTEL_DRAM_DDR4;
			break;
		case 3:
			qi->dram_type = INTEL_DRAM_LPDDR4;
			break;
		case 4:
			qi->dram_type = INTEL_DRAM_DDR3;
			break;
		case 5:
			qi->dram_type = INTEL_DRAM_LPDDR3;
			break;
		default:
			MISSING_CASE(val & 0xf);
			break;
		}
	} else if (IS_GEN(dev_priv, 11)) {
		switch (val & 0xf) {
		case 0:
			qi->dram_type = INTEL_DRAM_DDR4;
			break;
		case 1:
			qi->dram_type = INTEL_DRAM_DDR3;
			break;
		case 2:
			qi->dram_type = INTEL_DRAM_LPDDR3;
			break;
		case 3:
			qi->dram_type = INTEL_DRAM_LPDDR4;
			break;
		default:
			MISSING_CASE(val & 0xf);
			break;
		}
	} else {
		MISSING_CASE(INTEL_GEN(dev_priv));
		qi->dram_type = INTEL_DRAM_LPDDR3; /* Conservative default */
	}

	qi->num_channels = (val & 0xf0) >> 4;
	qi->num_points = (val & 0xf00) >> 8;

	if (IS_GEN(dev_priv, 12))
		qi->t_bl = qi->dram_type == INTEL_DRAM_DDR4 ? 4 : 16;
	else if (IS_GEN(dev_priv, 11))
		qi->t_bl = qi->dram_type == INTEL_DRAM_DDR4 ? 4 : 8;

	return 0;
}

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

static int icl_get_qgv_points(struct drm_i915_private *dev_priv,
			      struct intel_qgv_info *qi)
{
	int i, ret;

	ret = icl_pcode_read_mem_global_info(dev_priv, qi);
	if (ret)
		return ret;

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

static int icl_get_bw_info(struct drm_i915_private *dev_priv, const struct intel_sa_info *sa)
{
	struct intel_qgv_info qi = {};
	bool is_y_tile = true; /* assume y tile may be used */
	int num_channels;
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
	num_channels = qi.num_channels;

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

	return 0;
}

static unsigned int icl_max_bw(struct drm_i915_private *dev_priv,
			       int num_planes, int qgv_point)
{
	int i;

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

	if (IS_GEN(dev_priv, 12))
		icl_get_bw_info(dev_priv, &tgl_sa_info);
	else if (IS_GEN(dev_priv, 11))
		icl_get_bw_info(dev_priv, &icl_sa_info);
}

static unsigned int intel_max_data_rate(struct drm_i915_private *dev_priv,
					int num_planes)
{
	if (INTEL_GEN(dev_priv) >= 11) {
		/*
		 * Any bw group has same amount of QGV points
		 */
		const struct intel_bw_info *bi =
			&dev_priv->max_bw[0];
		unsigned int min_bw = UINT_MAX;
		int i;

		/*
		 * FIXME with SAGV disabled maybe we can assume
		 * point 1 will always be used? Seems to match
		 * the behaviour observed in the wild.
		 */
		for (i = 0; i < bi->num_qgv_points; i++) {
			unsigned int bw = icl_max_bw(dev_priv, num_planes, i);

			min_bw = min(bw, min_bw);
		}
		return min_bw;
	} else {
		return UINT_MAX;
	}
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

	bw_state->data_rate[crtc->pipe] =
		intel_bw_crtc_data_rate(crtc_state);
	bw_state->num_active_planes[crtc->pipe] =
		intel_bw_crtc_num_active_planes(crtc_state);

	DRM_DEBUG_KMS("pipe %c data rate %u num active planes %u\n",
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

	return data_rate;
}

static struct intel_bw_state *
intel_atomic_get_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_global_obj_state(state, &dev_priv->bw_obj);
	if (IS_ERR(bw_state))
		return ERR_CAST(bw_state);

	return to_intel_bw_state(bw_state);
}

int intel_bw_atomic_check(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_bw_state *bw_state = NULL;
	unsigned int data_rate, max_data_rate;
	unsigned int num_active_planes;
	struct intel_crtc *crtc;
	int i, ret;

	/* FIXME earlier gens need some checks too */
	if (INTEL_GEN(dev_priv) < 11)
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

		bw_state  = intel_atomic_get_bw_state(state);
		if (IS_ERR(bw_state))
			return PTR_ERR(bw_state);

		bw_state->data_rate[crtc->pipe] = new_data_rate;
		bw_state->num_active_planes[crtc->pipe] = new_active_planes;

		drm_dbg_kms(&dev_priv->drm,
			    "pipe %c data rate %u num active planes %u\n",
			    pipe_name(crtc->pipe),
			    bw_state->data_rate[crtc->pipe],
			    bw_state->num_active_planes[crtc->pipe]);
	}

	if (!bw_state)
		return 0;

	ret = intel_atomic_lock_global_state(&bw_state->base);
	if (ret)
		return ret;

	data_rate = intel_bw_data_rate(dev_priv, bw_state);
	num_active_planes = intel_bw_num_active_planes(dev_priv, bw_state);

	max_data_rate = intel_max_data_rate(dev_priv, num_active_planes);

	data_rate = DIV_ROUND_UP(data_rate, 1000);

	if (data_rate > max_data_rate) {
		drm_dbg_kms(&dev_priv->drm,
			    "Bandwidth %u MB/s exceeds max available %d MB/s (%d active planes)\n",
			    data_rate, max_data_rate, num_active_planes);
		return -EINVAL;
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
