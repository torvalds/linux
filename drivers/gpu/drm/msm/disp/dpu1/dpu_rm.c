// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s] " fmt, __func__
#include "dpu_kms.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_pingpong.h"
#include "dpu_hw_sspp.h"
#include "dpu_hw_intf.h"
#include "dpu_hw_wb.h"
#include "dpu_hw_dspp.h"
#include "dpu_hw_merge3d.h"
#include "dpu_hw_dsc.h"
#include "dpu_encoder.h"
#include "dpu_trace.h"


static inline bool reserved_by_other(uint32_t *res_map, int idx,
				     uint32_t enc_id)
{
	return res_map[idx] && res_map[idx] != enc_id;
}

/**
 * struct dpu_rm_requirements - Reservation requirements parameter bundle
 * @topology:  selected topology for the display
 * @hw_res:	   Hardware resources required as reported by the encoders
 */
struct dpu_rm_requirements {
	struct msm_display_topology topology;
};

int dpu_rm_destroy(struct dpu_rm *rm)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rm->dspp_blks); i++) {
		struct dpu_hw_dspp *hw;

		if (rm->dspp_blks[i]) {
			hw = to_dpu_hw_dspp(rm->dspp_blks[i]);
			dpu_hw_dspp_destroy(hw);
		}
	}
	for (i = 0; i < ARRAY_SIZE(rm->pingpong_blks); i++) {
		struct dpu_hw_pingpong *hw;

		if (rm->pingpong_blks[i]) {
			hw = to_dpu_hw_pingpong(rm->pingpong_blks[i]);
			dpu_hw_pingpong_destroy(hw);
		}
	}
	for (i = 0; i < ARRAY_SIZE(rm->merge_3d_blks); i++) {
		struct dpu_hw_merge_3d *hw;

		if (rm->merge_3d_blks[i]) {
			hw = to_dpu_hw_merge_3d(rm->merge_3d_blks[i]);
			dpu_hw_merge_3d_destroy(hw);
		}
	}
	for (i = 0; i < ARRAY_SIZE(rm->mixer_blks); i++) {
		struct dpu_hw_mixer *hw;

		if (rm->mixer_blks[i]) {
			hw = to_dpu_hw_mixer(rm->mixer_blks[i]);
			dpu_hw_lm_destroy(hw);
		}
	}
	for (i = 0; i < ARRAY_SIZE(rm->ctl_blks); i++) {
		struct dpu_hw_ctl *hw;

		if (rm->ctl_blks[i]) {
			hw = to_dpu_hw_ctl(rm->ctl_blks[i]);
			dpu_hw_ctl_destroy(hw);
		}
	}
	for (i = 0; i < ARRAY_SIZE(rm->hw_intf); i++)
		dpu_hw_intf_destroy(rm->hw_intf[i]);

	for (i = 0; i < ARRAY_SIZE(rm->dsc_blks); i++) {
		struct dpu_hw_dsc *hw;

		if (rm->dsc_blks[i]) {
			hw = to_dpu_hw_dsc(rm->dsc_blks[i]);
			dpu_hw_dsc_destroy(hw);
		}
	}

	for (i = 0; i < ARRAY_SIZE(rm->hw_wb); i++)
		dpu_hw_wb_destroy(rm->hw_wb[i]);

	for (i = 0; i < ARRAY_SIZE(rm->hw_sspp); i++)
		dpu_hw_sspp_destroy(rm->hw_sspp[i]);

	return 0;
}

int dpu_rm_init(struct dpu_rm *rm,
		const struct dpu_mdss_cfg *cat,
		void __iomem *mmio)
{
	int rc, i;

	if (!rm || !cat || !mmio) {
		DPU_ERROR("invalid kms\n");
		return -EINVAL;
	}

	/* Clear, setup lists */
	memset(rm, 0, sizeof(*rm));

	/* Interrogate HW catalog and create tracking items for hw blocks */
	for (i = 0; i < cat->mixer_count; i++) {
		struct dpu_hw_mixer *hw;
		const struct dpu_lm_cfg *lm = &cat->mixer[i];

		hw = dpu_hw_lm_init(lm, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed lm object creation: err %d\n", rc);
			goto fail;
		}
		rm->mixer_blks[lm->id - LM_0] = &hw->base;
	}

	for (i = 0; i < cat->merge_3d_count; i++) {
		struct dpu_hw_merge_3d *hw;
		const struct dpu_merge_3d_cfg *merge_3d = &cat->merge_3d[i];

		hw = dpu_hw_merge_3d_init(merge_3d, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed merge_3d object creation: err %d\n",
				rc);
			goto fail;
		}
		rm->merge_3d_blks[merge_3d->id - MERGE_3D_0] = &hw->base;
	}

	for (i = 0; i < cat->pingpong_count; i++) {
		struct dpu_hw_pingpong *hw;
		const struct dpu_pingpong_cfg *pp = &cat->pingpong[i];

		hw = dpu_hw_pingpong_init(pp, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed pingpong object creation: err %d\n",
				rc);
			goto fail;
		}
		if (pp->merge_3d && pp->merge_3d < MERGE_3D_MAX)
			hw->merge_3d = to_dpu_hw_merge_3d(rm->merge_3d_blks[pp->merge_3d - MERGE_3D_0]);
		rm->pingpong_blks[pp->id - PINGPONG_0] = &hw->base;
	}

	for (i = 0; i < cat->intf_count; i++) {
		struct dpu_hw_intf *hw;
		const struct dpu_intf_cfg *intf = &cat->intf[i];

		hw = dpu_hw_intf_init(intf, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed intf object creation: err %d\n", rc);
			goto fail;
		}
		rm->hw_intf[intf->id - INTF_0] = hw;
	}

	for (i = 0; i < cat->wb_count; i++) {
		struct dpu_hw_wb *hw;
		const struct dpu_wb_cfg *wb = &cat->wb[i];

		hw = dpu_hw_wb_init(wb, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed wb object creation: err %d\n", rc);
			goto fail;
		}
		rm->hw_wb[wb->id - WB_0] = hw;
	}

	for (i = 0; i < cat->ctl_count; i++) {
		struct dpu_hw_ctl *hw;
		const struct dpu_ctl_cfg *ctl = &cat->ctl[i];

		hw = dpu_hw_ctl_init(ctl, mmio, cat->mixer_count, cat->mixer);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed ctl object creation: err %d\n", rc);
			goto fail;
		}
		rm->ctl_blks[ctl->id - CTL_0] = &hw->base;
	}

	for (i = 0; i < cat->dspp_count; i++) {
		struct dpu_hw_dspp *hw;
		const struct dpu_dspp_cfg *dspp = &cat->dspp[i];

		hw = dpu_hw_dspp_init(dspp, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed dspp object creation: err %d\n", rc);
			goto fail;
		}
		rm->dspp_blks[dspp->id - DSPP_0] = &hw->base;
	}

	for (i = 0; i < cat->dsc_count; i++) {
		struct dpu_hw_dsc *hw;
		const struct dpu_dsc_cfg *dsc = &cat->dsc[i];

		if (test_bit(DPU_DSC_HW_REV_1_2, &dsc->features))
			hw = dpu_hw_dsc_init_1_2(dsc, mmio);
		else
			hw = dpu_hw_dsc_init(dsc, mmio);

		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed dsc object creation: err %d\n", rc);
			goto fail;
		}
		rm->dsc_blks[dsc->id - DSC_0] = &hw->base;
	}

	for (i = 0; i < cat->sspp_count; i++) {
		struct dpu_hw_sspp *hw;
		const struct dpu_sspp_cfg *sspp = &cat->sspp[i];

		hw = dpu_hw_sspp_init(sspp, mmio, cat->ubwc);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed sspp object creation: err %d\n", rc);
			goto fail;
		}
		rm->hw_sspp[sspp->id - SSPP_NONE] = hw;
	}

	return 0;

fail:
	dpu_rm_destroy(rm);

	return rc ? rc : -EFAULT;
}

static bool _dpu_rm_needs_split_display(const struct msm_display_topology *top)
{
	return top->num_intf > 1;
}

/**
 * _dpu_rm_get_lm_peer - get the id of a mixer which is a peer of the primary
 * @rm: dpu resource manager handle
 * @primary_idx: index of primary mixer in rm->mixer_blks[]
 */
static int _dpu_rm_get_lm_peer(struct dpu_rm *rm, int primary_idx)
{
	const struct dpu_lm_cfg *prim_lm_cfg;

	prim_lm_cfg = to_dpu_hw_mixer(rm->mixer_blks[primary_idx])->cap;

	if (prim_lm_cfg->lm_pair >= LM_0 && prim_lm_cfg->lm_pair < LM_MAX)
		return prim_lm_cfg->lm_pair - LM_0;
	return -EINVAL;
}

/**
 * _dpu_rm_check_lm_and_get_connected_blks - check if proposed layer mixer meets
 *	proposed use case requirements, incl. hardwired dependent blocks like
 *	pingpong
 * @rm: dpu resource manager handle
 * @global_state: resources shared across multiple kms objects
 * @enc_id: encoder id requesting for allocation
 * @lm_idx: index of proposed layer mixer in rm->mixer_blks[], function checks
 *      if lm, and all other hardwired blocks connected to the lm (pp) is
 *      available and appropriate
 * @pp_idx: output parameter, index of pingpong block attached to the layer
 *      mixer in rm->pingpong_blks[].
 * @dspp_idx: output parameter, index of dspp block attached to the layer
 *      mixer in rm->dspp_blks[].
 * @reqs: input parameter, rm requirements for HW blocks needed in the
 *      datapath.
 * Return: true if lm matches all requirements, false otherwise
 */
static bool _dpu_rm_check_lm_and_get_connected_blks(struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		uint32_t enc_id, int lm_idx, int *pp_idx, int *dspp_idx,
		struct dpu_rm_requirements *reqs)
{
	const struct dpu_lm_cfg *lm_cfg;
	int idx;

	/* Already reserved? */
	if (reserved_by_other(global_state->mixer_to_enc_id, lm_idx, enc_id)) {
		DPU_DEBUG("lm %d already reserved\n", lm_idx + LM_0);
		return false;
	}

	lm_cfg = to_dpu_hw_mixer(rm->mixer_blks[lm_idx])->cap;
	idx = lm_cfg->pingpong - PINGPONG_0;
	if (idx < 0 || idx >= ARRAY_SIZE(rm->pingpong_blks)) {
		DPU_ERROR("failed to get pp on lm %d\n", lm_cfg->pingpong);
		return false;
	}

	if (reserved_by_other(global_state->pingpong_to_enc_id, idx, enc_id)) {
		DPU_DEBUG("lm %d pp %d already reserved\n", lm_cfg->id,
				lm_cfg->pingpong);
		return false;
	}
	*pp_idx = idx;

	if (!reqs->topology.num_dspp)
		return true;

	idx = lm_cfg->dspp - DSPP_0;
	if (idx < 0 || idx >= ARRAY_SIZE(rm->dspp_blks)) {
		DPU_ERROR("failed to get dspp on lm %d\n", lm_cfg->dspp);
		return false;
	}

	if (reserved_by_other(global_state->dspp_to_enc_id, idx, enc_id)) {
		DPU_DEBUG("lm %d dspp %d already reserved\n", lm_cfg->id,
				lm_cfg->dspp);
		return false;
	}
	*dspp_idx = idx;

	return true;
}

static int _dpu_rm_reserve_lms(struct dpu_rm *rm,
			       struct dpu_global_state *global_state,
			       uint32_t enc_id,
			       struct dpu_rm_requirements *reqs)

{
	int lm_idx[MAX_BLOCKS];
	int pp_idx[MAX_BLOCKS];
	int dspp_idx[MAX_BLOCKS] = {0};
	int i, lm_count = 0;

	if (!reqs->topology.num_lm) {
		DPU_ERROR("invalid number of lm: %d\n", reqs->topology.num_lm);
		return -EINVAL;
	}

	/* Find a primary mixer */
	for (i = 0; i < ARRAY_SIZE(rm->mixer_blks) &&
			lm_count < reqs->topology.num_lm; i++) {
		if (!rm->mixer_blks[i])
			continue;

		lm_count = 0;
		lm_idx[lm_count] = i;

		if (!_dpu_rm_check_lm_and_get_connected_blks(rm, global_state,
				enc_id, i, &pp_idx[lm_count],
				&dspp_idx[lm_count], reqs)) {
			continue;
		}

		++lm_count;

		/* Valid primary mixer found, find matching peers */
		if (lm_count < reqs->topology.num_lm) {
			int j = _dpu_rm_get_lm_peer(rm, i);

			/* ignore the peer if there is an error or if the peer was already processed */
			if (j < 0 || j < i)
				continue;

			if (!rm->mixer_blks[j])
				continue;

			if (!_dpu_rm_check_lm_and_get_connected_blks(rm,
					global_state, enc_id, j,
					&pp_idx[lm_count], &dspp_idx[lm_count],
					reqs)) {
				continue;
			}

			lm_idx[lm_count] = j;
			++lm_count;
		}
	}

	if (lm_count != reqs->topology.num_lm) {
		DPU_DEBUG("unable to find appropriate mixers\n");
		return -ENAVAIL;
	}

	for (i = 0; i < lm_count; i++) {
		global_state->mixer_to_enc_id[lm_idx[i]] = enc_id;
		global_state->pingpong_to_enc_id[pp_idx[i]] = enc_id;
		global_state->dspp_to_enc_id[dspp_idx[i]] =
			reqs->topology.num_dspp ? enc_id : 0;

		trace_dpu_rm_reserve_lms(lm_idx[i] + LM_0, enc_id,
					 pp_idx[i] + PINGPONG_0);
	}

	return 0;
}

static int _dpu_rm_reserve_ctls(
		struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		uint32_t enc_id,
		const struct msm_display_topology *top)
{
	int ctl_idx[MAX_BLOCKS];
	int i = 0, j, num_ctls;
	bool needs_split_display;

	/* each hw_intf needs its own hw_ctrl to program its control path */
	num_ctls = top->num_intf;

	needs_split_display = _dpu_rm_needs_split_display(top);

	for (j = 0; j < ARRAY_SIZE(rm->ctl_blks); j++) {
		const struct dpu_hw_ctl *ctl;
		unsigned long features;
		bool has_split_display;

		if (!rm->ctl_blks[j])
			continue;
		if (reserved_by_other(global_state->ctl_to_enc_id, j, enc_id))
			continue;

		ctl = to_dpu_hw_ctl(rm->ctl_blks[j]);
		features = ctl->caps->features;
		has_split_display = BIT(DPU_CTL_SPLIT_DISPLAY) & features;

		DPU_DEBUG("ctl %d caps 0x%lX\n", j + CTL_0, features);

		if (needs_split_display != has_split_display)
			continue;

		ctl_idx[i] = j;
		DPU_DEBUG("ctl %d match\n", j + CTL_0);

		if (++i == num_ctls)
			break;

	}

	if (i != num_ctls)
		return -ENAVAIL;

	for (i = 0; i < ARRAY_SIZE(ctl_idx) && i < num_ctls; i++) {
		global_state->ctl_to_enc_id[ctl_idx[i]] = enc_id;
		trace_dpu_rm_reserve_ctls(i + CTL_0, enc_id);
	}

	return 0;
}

static int _dpu_rm_reserve_dsc(struct dpu_rm *rm,
			       struct dpu_global_state *global_state,
			       struct drm_encoder *enc,
			       const struct msm_display_topology *top)
{
	int num_dsc = top->num_dsc;
	int i;

	/* check if DSC required are allocated or not */
	for (i = 0; i < num_dsc; i++) {
		if (!rm->dsc_blks[i]) {
			DPU_ERROR("DSC %d does not exist\n", i);
			return -EIO;
		}

		if (global_state->dsc_to_enc_id[i]) {
			DPU_ERROR("DSC %d is already allocated\n", i);
			return -EIO;
		}
	}

	for (i = 0; i < num_dsc; i++)
		global_state->dsc_to_enc_id[i] = enc->base.id;

	return 0;
}

static int _dpu_rm_make_reservation(
		struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		struct drm_encoder *enc,
		struct dpu_rm_requirements *reqs)
{
	int ret;

	ret = _dpu_rm_reserve_lms(rm, global_state, enc->base.id, reqs);
	if (ret) {
		DPU_ERROR("unable to find appropriate mixers\n");
		return ret;
	}

	ret = _dpu_rm_reserve_ctls(rm, global_state, enc->base.id,
				&reqs->topology);
	if (ret) {
		DPU_ERROR("unable to find appropriate CTL\n");
		return ret;
	}

	ret  = _dpu_rm_reserve_dsc(rm, global_state, enc, &reqs->topology);
	if (ret)
		return ret;

	return ret;
}

static int _dpu_rm_populate_requirements(
		struct drm_encoder *enc,
		struct dpu_rm_requirements *reqs,
		struct msm_display_topology req_topology)
{
	reqs->topology = req_topology;

	DRM_DEBUG_KMS("num_lm: %d num_dsc: %d num_intf: %d\n",
		      reqs->topology.num_lm, reqs->topology.num_dsc,
		      reqs->topology.num_intf);

	return 0;
}

static void _dpu_rm_clear_mapping(uint32_t *res_mapping, int cnt,
				  uint32_t enc_id)
{
	int i;

	for (i = 0; i < cnt; i++) {
		if (res_mapping[i] == enc_id)
			res_mapping[i] = 0;
	}
}

void dpu_rm_release(struct dpu_global_state *global_state,
		    struct drm_encoder *enc)
{
	_dpu_rm_clear_mapping(global_state->pingpong_to_enc_id,
		ARRAY_SIZE(global_state->pingpong_to_enc_id), enc->base.id);
	_dpu_rm_clear_mapping(global_state->mixer_to_enc_id,
		ARRAY_SIZE(global_state->mixer_to_enc_id), enc->base.id);
	_dpu_rm_clear_mapping(global_state->ctl_to_enc_id,
		ARRAY_SIZE(global_state->ctl_to_enc_id), enc->base.id);
	_dpu_rm_clear_mapping(global_state->dsc_to_enc_id,
		ARRAY_SIZE(global_state->dsc_to_enc_id), enc->base.id);
	_dpu_rm_clear_mapping(global_state->dspp_to_enc_id,
		ARRAY_SIZE(global_state->dspp_to_enc_id), enc->base.id);
}

int dpu_rm_reserve(
		struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct msm_display_topology topology)
{
	struct dpu_rm_requirements reqs;
	int ret;

	/* Check if this is just a page-flip */
	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return 0;

	if (IS_ERR(global_state)) {
		DPU_ERROR("failed to global state\n");
		return PTR_ERR(global_state);
	}

	DRM_DEBUG_KMS("reserving hw for enc %d crtc %d\n",
		      enc->base.id, crtc_state->crtc->base.id);

	ret = _dpu_rm_populate_requirements(enc, &reqs, topology);
	if (ret) {
		DPU_ERROR("failed to populate hw requirements\n");
		return ret;
	}

	ret = _dpu_rm_make_reservation(rm, global_state, enc, &reqs);
	if (ret)
		DPU_ERROR("failed to reserve hw resources: %d\n", ret);



	return ret;
}

int dpu_rm_get_assigned_resources(struct dpu_rm *rm,
	struct dpu_global_state *global_state, uint32_t enc_id,
	enum dpu_hw_blk_type type, struct dpu_hw_blk **blks, int blks_size)
{
	struct dpu_hw_blk **hw_blks;
	uint32_t *hw_to_enc_id;
	int i, num_blks, max_blks;

	switch (type) {
	case DPU_HW_BLK_PINGPONG:
		hw_blks = rm->pingpong_blks;
		hw_to_enc_id = global_state->pingpong_to_enc_id;
		max_blks = ARRAY_SIZE(rm->pingpong_blks);
		break;
	case DPU_HW_BLK_LM:
		hw_blks = rm->mixer_blks;
		hw_to_enc_id = global_state->mixer_to_enc_id;
		max_blks = ARRAY_SIZE(rm->mixer_blks);
		break;
	case DPU_HW_BLK_CTL:
		hw_blks = rm->ctl_blks;
		hw_to_enc_id = global_state->ctl_to_enc_id;
		max_blks = ARRAY_SIZE(rm->ctl_blks);
		break;
	case DPU_HW_BLK_DSPP:
		hw_blks = rm->dspp_blks;
		hw_to_enc_id = global_state->dspp_to_enc_id;
		max_blks = ARRAY_SIZE(rm->dspp_blks);
		break;
	case DPU_HW_BLK_DSC:
		hw_blks = rm->dsc_blks;
		hw_to_enc_id = global_state->dsc_to_enc_id;
		max_blks = ARRAY_SIZE(rm->dsc_blks);
		break;
	default:
		DPU_ERROR("blk type %d not managed by rm\n", type);
		return 0;
	}

	num_blks = 0;
	for (i = 0; i < max_blks; i++) {
		if (hw_to_enc_id[i] != enc_id)
			continue;

		if (num_blks == blks_size) {
			DPU_ERROR("More than %d resources assigned to enc %d\n",
				  blks_size, enc_id);
			break;
		}
		if (!hw_blks[i]) {
			DPU_ERROR("Allocated resource %d unavailable to assign to enc %d\n",
				  type, enc_id);
			break;
		}
		blks[num_blks++] = hw_blks[i];
	}

	return num_blks;
}
