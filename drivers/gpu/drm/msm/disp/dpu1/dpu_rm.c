// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s] " fmt, __func__
#include "dpu_kms.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_pingpong.h"
#include "dpu_hw_intf.h"
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
	struct dpu_encoder_hw_resources hw_res;
};

int dpu_rm_destroy(struct dpu_rm *rm)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rm->pingpong_blks); i++) {
		struct dpu_hw_pingpong *hw;

		if (rm->pingpong_blks[i]) {
			hw = to_dpu_hw_pingpong(rm->pingpong_blks[i]);
			dpu_hw_pingpong_destroy(hw);
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
	for (i = 0; i < ARRAY_SIZE(rm->intf_blks); i++) {
		struct dpu_hw_intf *hw;

		if (rm->intf_blks[i]) {
			hw = to_dpu_hw_intf(rm->intf_blks[i]);
			dpu_hw_intf_destroy(hw);
		}
	}

	mutex_destroy(&rm->rm_lock);

	return 0;
}

int dpu_rm_init(struct dpu_rm *rm,
		struct dpu_mdss_cfg *cat,
		void __iomem *mmio)
{
	int rc, i;

	if (!rm || !cat || !mmio) {
		DPU_ERROR("invalid kms\n");
		return -EINVAL;
	}

	/* Clear, setup lists */
	memset(rm, 0, sizeof(*rm));

	mutex_init(&rm->rm_lock);

	/* Interrogate HW catalog and create tracking items for hw blocks */
	for (i = 0; i < cat->mixer_count; i++) {
		struct dpu_hw_mixer *hw;
		const struct dpu_lm_cfg *lm = &cat->mixer[i];

		if (lm->pingpong == PINGPONG_MAX) {
			DPU_DEBUG("skip mixer %d without pingpong\n", lm->id);
			continue;
		}

		if (lm->id < LM_0 || lm->id >= LM_MAX) {
			DPU_ERROR("skip mixer %d with invalid id\n", lm->id);
			continue;
		}
		hw = dpu_hw_lm_init(lm->id, mmio, cat);
		if (IS_ERR_OR_NULL(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed lm object creation: err %d\n", rc);
			goto fail;
		}
		rm->mixer_blks[lm->id - LM_0] = &hw->base;

		if (!rm->lm_max_width) {
			rm->lm_max_width = lm->sblk->maxwidth;
		} else if (rm->lm_max_width != lm->sblk->maxwidth) {
			/*
			 * Don't expect to have hw where lm max widths differ.
			 * If found, take the min.
			 */
			DPU_ERROR("unsupported: lm maxwidth differs\n");
			if (rm->lm_max_width > lm->sblk->maxwidth)
				rm->lm_max_width = lm->sblk->maxwidth;
		}
	}

	for (i = 0; i < cat->pingpong_count; i++) {
		struct dpu_hw_pingpong *hw;
		const struct dpu_pingpong_cfg *pp = &cat->pingpong[i];

		if (pp->id < PINGPONG_0 || pp->id >= PINGPONG_MAX) {
			DPU_ERROR("skip pingpong %d with invalid id\n", pp->id);
			continue;
		}
		hw = dpu_hw_pingpong_init(pp->id, mmio, cat);
		if (IS_ERR_OR_NULL(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed pingpong object creation: err %d\n",
				rc);
			goto fail;
		}
		rm->pingpong_blks[pp->id - PINGPONG_0] = &hw->base;
	}

	for (i = 0; i < cat->intf_count; i++) {
		struct dpu_hw_intf *hw;
		const struct dpu_intf_cfg *intf = &cat->intf[i];

		if (intf->type == INTF_NONE) {
			DPU_DEBUG("skip intf %d with type none\n", i);
			continue;
		}
		if (intf->id < INTF_0 || intf->id >= INTF_MAX) {
			DPU_ERROR("skip intf %d with invalid id\n", intf->id);
			continue;
		}
		hw = dpu_hw_intf_init(intf->id, mmio, cat);
		if (IS_ERR_OR_NULL(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed intf object creation: err %d\n", rc);
			goto fail;
		}
		rm->intf_blks[intf->id - INTF_0] = &hw->base;
	}

	for (i = 0; i < cat->ctl_count; i++) {
		struct dpu_hw_ctl *hw;
		const struct dpu_ctl_cfg *ctl = &cat->ctl[i];

		if (ctl->id < CTL_0 || ctl->id >= CTL_MAX) {
			DPU_ERROR("skip ctl %d with invalid id\n", ctl->id);
			continue;
		}
		hw = dpu_hw_ctl_init(ctl->id, mmio, cat);
		if (IS_ERR_OR_NULL(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed ctl object creation: err %d\n", rc);
			goto fail;
		}
		rm->ctl_blks[ctl->id - CTL_0] = &hw->base;
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
 * _dpu_rm_check_lm_peer - check if a mixer is a peer of the primary
 * @rm: dpu resource manager handle
 * @primary_idx: index of primary mixer in rm->mixer_blks[]
 * @peer_idx: index of other mixer in rm->mixer_blks[]
 * @Return: true if rm->mixer_blks[peer_idx] is a peer of
 *          rm->mixer_blks[primary_idx]
 */
static bool _dpu_rm_check_lm_peer(struct dpu_rm *rm, int primary_idx,
		int peer_idx)
{
	const struct dpu_lm_cfg *prim_lm_cfg;
	const struct dpu_lm_cfg *peer_cfg;

	prim_lm_cfg = to_dpu_hw_mixer(rm->mixer_blks[primary_idx])->cap;
	peer_cfg = to_dpu_hw_mixer(rm->mixer_blks[peer_idx])->cap;

	if (!test_bit(peer_cfg->id, &prim_lm_cfg->lm_pair_mask)) {
		DPU_DEBUG("lm %d not peer of lm %d\n", peer_cfg->id,
				peer_cfg->id);
		return false;
	}
	return true;
}

/**
 * _dpu_rm_check_lm_and_get_connected_blks - check if proposed layer mixer meets
 *	proposed use case requirements, incl. hardwired dependent blocks like
 *	pingpong
 * @rm: dpu resource manager handle
 * @enc_id: encoder id requesting for allocation
 * @lm_idx: index of proposed layer mixer in rm->mixer_blks[], function checks
 *      if lm, and all other hardwired blocks connected to the lm (pp) is
 *      available and appropriate
 * @pp_idx: output parameter, index of pingpong block attached to the layer
 *      mixer in rm->pongpong_blks[].
 * @Return: true if lm matches all requirements, false otherwise
 */
static bool _dpu_rm_check_lm_and_get_connected_blks(struct dpu_rm *rm,
		uint32_t enc_id, int lm_idx, int *pp_idx)
{
	const struct dpu_lm_cfg *lm_cfg;
	int idx;

	/* Already reserved? */
	if (reserved_by_other(rm->mixer_to_enc_id, lm_idx, enc_id)) {
		DPU_DEBUG("lm %d already reserved\n", lm_idx + LM_0);
		return false;
	}

	lm_cfg = to_dpu_hw_mixer(rm->mixer_blks[lm_idx])->cap;
	idx = lm_cfg->pingpong - PINGPONG_0;
	if (idx < 0 || idx >= ARRAY_SIZE(rm->pingpong_blks)) {
		DPU_ERROR("failed to get pp on lm %d\n", lm_cfg->pingpong);
		return false;
	}

	if (reserved_by_other(rm->pingpong_to_enc_id, idx, enc_id)) {
		DPU_DEBUG("lm %d pp %d already reserved\n", lm_cfg->id,
				lm_cfg->pingpong);
		return false;
	}
	*pp_idx = idx;
	return true;
}

static int _dpu_rm_reserve_lms(struct dpu_rm *rm, uint32_t enc_id,
			       struct dpu_rm_requirements *reqs)

{
	int lm_idx[MAX_BLOCKS];
	int pp_idx[MAX_BLOCKS];
	int i, j, lm_count = 0;

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

		if (!_dpu_rm_check_lm_and_get_connected_blks(
				rm, enc_id, i, &pp_idx[lm_count])) {
			continue;
		}

		++lm_count;

		/* Valid primary mixer found, find matching peers */
		for (j = i + 1; j < ARRAY_SIZE(rm->mixer_blks) &&
				lm_count < reqs->topology.num_lm; j++) {
			if (!rm->mixer_blks[j])
				continue;

			if (!_dpu_rm_check_lm_peer(rm, i, j)) {
				DPU_DEBUG("lm %d not peer of lm %d\n", LM_0 + j,
						LM_0 + i);
				continue;
			}

			if (!_dpu_rm_check_lm_and_get_connected_blks(
					rm, enc_id, j, &pp_idx[lm_count])) {
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
		rm->mixer_to_enc_id[lm_idx[i]] = enc_id;
		rm->pingpong_to_enc_id[pp_idx[i]] = enc_id;

		trace_dpu_rm_reserve_lms(lm_idx[i] + LM_0, enc_id,
					 pp_idx[i] + PINGPONG_0);
	}

	return 0;
}

static int _dpu_rm_reserve_ctls(
		struct dpu_rm *rm,
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
		if (reserved_by_other(rm->ctl_to_enc_id, j, enc_id))
			continue;

		ctl = to_dpu_hw_ctl(rm->ctl_blks[j]);
		features = ctl->caps->features;
		has_split_display = BIT(DPU_CTL_SPLIT_DISPLAY) & features;

		DPU_DEBUG("ctl %d caps 0x%lX\n", rm->ctl_blks[j]->id, features);

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
		rm->ctl_to_enc_id[ctl_idx[i]] = enc_id;
		trace_dpu_rm_reserve_ctls(i + CTL_0, enc_id);
	}

	return 0;
}

static int _dpu_rm_reserve_intf(
		struct dpu_rm *rm,
		uint32_t enc_id,
		uint32_t id)
{
	int idx = id - INTF_0;

	if (!rm->intf_blks[idx]) {
		DPU_ERROR("couldn't find intf id %d\n", id);
		return -EINVAL;
	}

	if (reserved_by_other(rm->intf_to_enc_id, idx, enc_id)) {
		DPU_ERROR("intf id %d already reserved\n", id);
		return -ENAVAIL;
	}

	rm->intf_to_enc_id[idx] = enc_id;
	return 0;
}

static int _dpu_rm_reserve_intf_related_hw(
		struct dpu_rm *rm,
		uint32_t enc_id,
		struct dpu_encoder_hw_resources *hw_res)
{
	int i, ret = 0;
	u32 id;

	for (i = 0; i < ARRAY_SIZE(hw_res->intfs); i++) {
		if (hw_res->intfs[i] == INTF_MODE_NONE)
			continue;
		id = i + INTF_0;
		ret = _dpu_rm_reserve_intf(rm, enc_id, id);
		if (ret)
			return ret;
	}

	return ret;
}

static int _dpu_rm_make_reservation(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct dpu_rm_requirements *reqs)
{
	int ret;

	ret = _dpu_rm_reserve_lms(rm, enc->base.id, reqs);
	if (ret) {
		DPU_ERROR("unable to find appropriate mixers\n");
		return ret;
	}

	ret = _dpu_rm_reserve_ctls(rm, enc->base.id, &reqs->topology);
	if (ret) {
		DPU_ERROR("unable to find appropriate CTL\n");
		return ret;
	}

	ret = _dpu_rm_reserve_intf_related_hw(rm, enc->base.id, &reqs->hw_res);
	if (ret)
		return ret;

	return ret;
}

static int _dpu_rm_populate_requirements(
		struct drm_encoder *enc,
		struct dpu_rm_requirements *reqs,
		struct msm_display_topology req_topology)
{
	dpu_encoder_get_hw_resources(enc, &reqs->hw_res);

	reqs->topology = req_topology;

	DRM_DEBUG_KMS("num_lm: %d num_enc: %d num_intf: %d\n",
		      reqs->topology.num_lm, reqs->topology.num_enc,
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

static void _dpu_rm_release_reservation(struct dpu_rm *rm, uint32_t enc_id)
{
	_dpu_rm_clear_mapping(rm->pingpong_to_enc_id,
		ARRAY_SIZE(rm->pingpong_to_enc_id), enc_id);
	_dpu_rm_clear_mapping(rm->mixer_to_enc_id,
		ARRAY_SIZE(rm->mixer_to_enc_id), enc_id);
	_dpu_rm_clear_mapping(rm->ctl_to_enc_id,
		ARRAY_SIZE(rm->ctl_to_enc_id), enc_id);
	_dpu_rm_clear_mapping(rm->intf_to_enc_id,
		ARRAY_SIZE(rm->intf_to_enc_id), enc_id);
}

void dpu_rm_release(struct dpu_rm *rm, struct drm_encoder *enc)
{
	mutex_lock(&rm->rm_lock);

	_dpu_rm_release_reservation(rm, enc->base.id);

	mutex_unlock(&rm->rm_lock);
}

int dpu_rm_reserve(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct msm_display_topology topology,
		bool test_only)
{
	struct dpu_rm_requirements reqs;
	int ret;

	/* Check if this is just a page-flip */
	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return 0;

	DRM_DEBUG_KMS("reserving hw for enc %d crtc %d test_only %d\n",
		      enc->base.id, crtc_state->crtc->base.id, test_only);

	mutex_lock(&rm->rm_lock);

	ret = _dpu_rm_populate_requirements(enc, &reqs, topology);
	if (ret) {
		DPU_ERROR("failed to populate hw requirements\n");
		goto end;
	}

	ret = _dpu_rm_make_reservation(rm, enc, &reqs);
	if (ret) {
		DPU_ERROR("failed to reserve hw resources: %d\n", ret);
		_dpu_rm_release_reservation(rm, enc->base.id);
	} else if (test_only) {
		 /* test_only: test the reservation and then undo */
		DPU_DEBUG("test_only: discard test [enc: %d]\n",
				enc->base.id);
		_dpu_rm_release_reservation(rm, enc->base.id);
	}

end:
	mutex_unlock(&rm->rm_lock);

	return ret;
}

int dpu_rm_get_assigned_resources(struct dpu_rm *rm, uint32_t enc_id,
	enum dpu_hw_blk_type type, struct dpu_hw_blk **blks, int blks_size)
{
	struct dpu_hw_blk **hw_blks;
	uint32_t *hw_to_enc_id;
	int i, num_blks, max_blks;

	switch (type) {
	case DPU_HW_BLK_PINGPONG:
		hw_blks = rm->pingpong_blks;
		hw_to_enc_id = rm->pingpong_to_enc_id;
		max_blks = ARRAY_SIZE(rm->pingpong_blks);
		break;
	case DPU_HW_BLK_LM:
		hw_blks = rm->mixer_blks;
		hw_to_enc_id = rm->mixer_to_enc_id;
		max_blks = ARRAY_SIZE(rm->mixer_blks);
		break;
	case DPU_HW_BLK_CTL:
		hw_blks = rm->ctl_blks;
		hw_to_enc_id = rm->ctl_to_enc_id;
		max_blks = ARRAY_SIZE(rm->ctl_blks);
		break;
	case DPU_HW_BLK_INTF:
		hw_blks = rm->intf_blks;
		hw_to_enc_id = rm->intf_to_enc_id;
		max_blks = ARRAY_SIZE(rm->intf_blks);
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
		blks[num_blks++] = hw_blks[i];
	}

	return num_blks;
}
