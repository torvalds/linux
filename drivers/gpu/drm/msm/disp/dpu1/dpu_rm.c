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

#define RESERVED_BY_OTHER(h, r)  \
		((h)->enc_id && (h)->enc_id != r)

/**
 * struct dpu_rm_requirements - Reservation requirements parameter bundle
 * @topology:  selected topology for the display
 * @hw_res:	   Hardware resources required as reported by the encoders
 */
struct dpu_rm_requirements {
	struct msm_display_topology topology;
	struct dpu_encoder_hw_resources hw_res;
};


/**
 * struct dpu_rm_hw_blk - hardware block tracking list member
 * @list:	List head for list of all hardware blocks tracking items
 * @id:		Hardware ID number, within it's own space, ie. LM_X
 * @enc_id:	Encoder id to which this blk is binded
 * @hw:		Pointer to the hardware register access object for this block
 */
struct dpu_rm_hw_blk {
	struct list_head list;
	uint32_t id;
	uint32_t enc_id;
	struct dpu_hw_blk *hw;
};

void dpu_rm_init_hw_iter(
		struct dpu_rm_hw_iter *iter,
		uint32_t enc_id,
		enum dpu_hw_blk_type type)
{
	memset(iter, 0, sizeof(*iter));
	iter->enc_id = enc_id;
	iter->type = type;
}

static bool _dpu_rm_get_hw_locked(struct dpu_rm *rm, struct dpu_rm_hw_iter *i)
{
	struct list_head *blk_list;

	if (!rm || !i || i->type >= DPU_HW_BLK_MAX) {
		DPU_ERROR("invalid rm\n");
		return false;
	}

	i->hw = NULL;
	blk_list = &rm->hw_blks[i->type];

	if (i->blk && (&i->blk->list == blk_list)) {
		DPU_DEBUG("attempt resume iteration past last\n");
		return false;
	}

	i->blk = list_prepare_entry(i->blk, blk_list, list);

	list_for_each_entry_continue(i->blk, blk_list, list) {
		if (i->enc_id == i->blk->enc_id) {
			i->hw = i->blk->hw;
			DPU_DEBUG("found type %d id %d for enc %d\n",
					i->type, i->blk->id, i->enc_id);
			return true;
		}
	}

	DPU_DEBUG("no match, type %d for enc %d\n", i->type, i->enc_id);

	return false;
}

bool dpu_rm_get_hw(struct dpu_rm *rm, struct dpu_rm_hw_iter *i)
{
	bool ret;

	mutex_lock(&rm->rm_lock);
	ret = _dpu_rm_get_hw_locked(rm, i);
	mutex_unlock(&rm->rm_lock);

	return ret;
}

static void _dpu_rm_hw_destroy(enum dpu_hw_blk_type type, void *hw)
{
	switch (type) {
	case DPU_HW_BLK_LM:
		dpu_hw_lm_destroy(hw);
		break;
	case DPU_HW_BLK_CTL:
		dpu_hw_ctl_destroy(hw);
		break;
	case DPU_HW_BLK_PINGPONG:
		dpu_hw_pingpong_destroy(hw);
		break;
	case DPU_HW_BLK_INTF:
		dpu_hw_intf_destroy(hw);
		break;
	case DPU_HW_BLK_SSPP:
		/* SSPPs are not managed by the resource manager */
	case DPU_HW_BLK_TOP:
		/* Top is a singleton, not managed in hw_blks list */
	case DPU_HW_BLK_MAX:
	default:
		DPU_ERROR("unsupported block type %d\n", type);
		break;
	}
}

int dpu_rm_destroy(struct dpu_rm *rm)
{
	struct dpu_rm_hw_blk *hw_cur, *hw_nxt;
	enum dpu_hw_blk_type type;

	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry_safe(hw_cur, hw_nxt, &rm->hw_blks[type],
				list) {
			list_del(&hw_cur->list);
			_dpu_rm_hw_destroy(type, hw_cur->hw);
			kfree(hw_cur);
		}
	}

	mutex_destroy(&rm->rm_lock);

	return 0;
}

static int _dpu_rm_hw_blk_create(
		struct dpu_rm *rm,
		const struct dpu_mdss_cfg *cat,
		void __iomem *mmio,
		enum dpu_hw_blk_type type,
		uint32_t id,
		const void *hw_catalog_info)
{
	struct dpu_rm_hw_blk *blk;
	void *hw;

	switch (type) {
	case DPU_HW_BLK_LM:
		hw = dpu_hw_lm_init(id, mmio, cat);
		break;
	case DPU_HW_BLK_CTL:
		hw = dpu_hw_ctl_init(id, mmio, cat);
		break;
	case DPU_HW_BLK_PINGPONG:
		hw = dpu_hw_pingpong_init(id, mmio, cat);
		break;
	case DPU_HW_BLK_INTF:
		hw = dpu_hw_intf_init(id, mmio, cat);
		break;
	case DPU_HW_BLK_SSPP:
		/* SSPPs are not managed by the resource manager */
	case DPU_HW_BLK_TOP:
		/* Top is a singleton, not managed in hw_blks list */
	case DPU_HW_BLK_MAX:
	default:
		DPU_ERROR("unsupported block type %d\n", type);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(hw)) {
		DPU_ERROR("failed hw object creation: type %d, err %ld\n",
				type, PTR_ERR(hw));
		return -EFAULT;
	}

	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk) {
		_dpu_rm_hw_destroy(type, hw);
		return -ENOMEM;
	}

	blk->id = id;
	blk->hw = hw;
	blk->enc_id = 0;
	list_add_tail(&blk->list, &rm->hw_blks[type]);

	return 0;
}

int dpu_rm_init(struct dpu_rm *rm,
		struct dpu_mdss_cfg *cat,
		void __iomem *mmio)
{
	int rc, i;
	enum dpu_hw_blk_type type;

	if (!rm || !cat || !mmio) {
		DPU_ERROR("invalid kms\n");
		return -EINVAL;
	}

	/* Clear, setup lists */
	memset(rm, 0, sizeof(*rm));

	mutex_init(&rm->rm_lock);

	for (type = 0; type < DPU_HW_BLK_MAX; type++)
		INIT_LIST_HEAD(&rm->hw_blks[type]);

	/* Interrogate HW catalog and create tracking items for hw blocks */
	for (i = 0; i < cat->mixer_count; i++) {
		const struct dpu_lm_cfg *lm = &cat->mixer[i];

		if (lm->pingpong == PINGPONG_MAX) {
			DPU_DEBUG("skip mixer %d without pingpong\n", lm->id);
			continue;
		}

		rc = _dpu_rm_hw_blk_create(rm, cat, mmio, DPU_HW_BLK_LM,
				cat->mixer[i].id, &cat->mixer[i]);
		if (rc) {
			DPU_ERROR("failed: lm hw not available\n");
			goto fail;
		}

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
		rc = _dpu_rm_hw_blk_create(rm, cat, mmio, DPU_HW_BLK_PINGPONG,
				cat->pingpong[i].id, &cat->pingpong[i]);
		if (rc) {
			DPU_ERROR("failed: pp hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->intf_count; i++) {
		if (cat->intf[i].type == INTF_NONE) {
			DPU_DEBUG("skip intf %d with type none\n", i);
			continue;
		}

		rc = _dpu_rm_hw_blk_create(rm, cat, mmio, DPU_HW_BLK_INTF,
				cat->intf[i].id, &cat->intf[i]);
		if (rc) {
			DPU_ERROR("failed: intf hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->ctl_count; i++) {
		rc = _dpu_rm_hw_blk_create(rm, cat, mmio, DPU_HW_BLK_CTL,
				cat->ctl[i].id, &cat->ctl[i]);
		if (rc) {
			DPU_ERROR("failed: ctl hw not available\n");
			goto fail;
		}
	}

	return 0;

fail:
	dpu_rm_destroy(rm);

	return rc;
}

static bool _dpu_rm_needs_split_display(const struct msm_display_topology *top)
{
	return top->num_intf > 1;
}

/**
 * _dpu_rm_check_lm_and_get_connected_blks - check if proposed layer mixer meets
 *	proposed use case requirements, incl. hardwired dependent blocks like
 *	pingpong
 * @rm: dpu resource manager handle
 * @enc_id: encoder id requesting for allocation
 * @reqs: proposed use case requirements
 * @lm: proposed layer mixer, function checks if lm, and all other hardwired
 *      blocks connected to the lm (pp) is available and appropriate
 * @pp: output parameter, pingpong block attached to the layer mixer.
 *      NULL if pp was not available, or not matching requirements.
 * @primary_lm: if non-null, this function check if lm is compatible primary_lm
 *              as well as satisfying all other requirements
 * @Return: true if lm matches all requirements, false otherwise
 */
static bool _dpu_rm_check_lm_and_get_connected_blks(
		struct dpu_rm *rm,
		uint32_t enc_id,
		struct dpu_rm_requirements *reqs,
		struct dpu_rm_hw_blk *lm,
		struct dpu_rm_hw_blk **pp,
		struct dpu_rm_hw_blk *primary_lm)
{
	const struct dpu_lm_cfg *lm_cfg = to_dpu_hw_mixer(lm->hw)->cap;
	struct dpu_rm_hw_iter iter;

	*pp = NULL;

	DPU_DEBUG("check lm %d pp %d\n",
			   lm_cfg->id, lm_cfg->pingpong);

	/* Check if this layer mixer is a peer of the proposed primary LM */
	if (primary_lm) {
		const struct dpu_lm_cfg *prim_lm_cfg =
				to_dpu_hw_mixer(primary_lm->hw)->cap;

		if (!test_bit(lm_cfg->id, &prim_lm_cfg->lm_pair_mask)) {
			DPU_DEBUG("lm %d not peer of lm %d\n", lm_cfg->id,
					prim_lm_cfg->id);
			return false;
		}
	}

	/* Already reserved? */
	if (RESERVED_BY_OTHER(lm, enc_id)) {
		DPU_DEBUG("lm %d already reserved\n", lm_cfg->id);
		return false;
	}

	dpu_rm_init_hw_iter(&iter, 0, DPU_HW_BLK_PINGPONG);
	while (_dpu_rm_get_hw_locked(rm, &iter)) {
		if (iter.blk->id == lm_cfg->pingpong) {
			*pp = iter.blk;
			break;
		}
	}

	if (!*pp) {
		DPU_ERROR("failed to get pp on lm %d\n", lm_cfg->pingpong);
		return false;
	}

	if (RESERVED_BY_OTHER(*pp, enc_id)) {
		DPU_DEBUG("lm %d pp %d already reserved\n", lm->id,
				(*pp)->id);
		return false;
	}

	return true;
}

static int _dpu_rm_reserve_lms(struct dpu_rm *rm, uint32_t enc_id,
			       struct dpu_rm_requirements *reqs)

{
	struct dpu_rm_hw_blk *lm[MAX_BLOCKS];
	struct dpu_rm_hw_blk *pp[MAX_BLOCKS];
	struct dpu_rm_hw_iter iter_i, iter_j;
	int lm_count = 0;
	int i, rc = 0;

	if (!reqs->topology.num_lm) {
		DPU_ERROR("invalid number of lm: %d\n", reqs->topology.num_lm);
		return -EINVAL;
	}

	/* Find a primary mixer */
	dpu_rm_init_hw_iter(&iter_i, 0, DPU_HW_BLK_LM);
	while (lm_count != reqs->topology.num_lm &&
			_dpu_rm_get_hw_locked(rm, &iter_i)) {
		memset(&lm, 0, sizeof(lm));
		memset(&pp, 0, sizeof(pp));

		lm_count = 0;
		lm[lm_count] = iter_i.blk;

		if (!_dpu_rm_check_lm_and_get_connected_blks(
				rm, enc_id, reqs, lm[lm_count],
				&pp[lm_count], NULL))
			continue;

		++lm_count;

		/* Valid primary mixer found, find matching peers */
		dpu_rm_init_hw_iter(&iter_j, 0, DPU_HW_BLK_LM);

		while (lm_count != reqs->topology.num_lm &&
				_dpu_rm_get_hw_locked(rm, &iter_j)) {
			if (iter_i.blk == iter_j.blk)
				continue;

			if (!_dpu_rm_check_lm_and_get_connected_blks(
					rm, enc_id, reqs, iter_j.blk,
					&pp[lm_count], iter_i.blk))
				continue;

			lm[lm_count] = iter_j.blk;
			++lm_count;
		}
	}

	if (lm_count != reqs->topology.num_lm) {
		DPU_DEBUG("unable to find appropriate mixers\n");
		return -ENAVAIL;
	}

	for (i = 0; i < ARRAY_SIZE(lm); i++) {
		if (!lm[i])
			break;

		lm[i]->enc_id = enc_id;
		pp[i]->enc_id = enc_id;

		trace_dpu_rm_reserve_lms(lm[i]->id, enc_id, pp[i]->id);
	}

	return rc;
}

static int _dpu_rm_reserve_ctls(
		struct dpu_rm *rm,
		uint32_t enc_id,
		const struct msm_display_topology *top)
{
	struct dpu_rm_hw_blk *ctls[MAX_BLOCKS];
	struct dpu_rm_hw_iter iter;
	int i = 0, num_ctls = 0;
	bool needs_split_display = false;

	memset(&ctls, 0, sizeof(ctls));

	/* each hw_intf needs its own hw_ctrl to program its control path */
	num_ctls = top->num_intf;

	needs_split_display = _dpu_rm_needs_split_display(top);

	dpu_rm_init_hw_iter(&iter, 0, DPU_HW_BLK_CTL);
	while (_dpu_rm_get_hw_locked(rm, &iter)) {
		const struct dpu_hw_ctl *ctl = to_dpu_hw_ctl(iter.blk->hw);
		unsigned long features = ctl->caps->features;
		bool has_split_display;

		if (RESERVED_BY_OTHER(iter.blk, enc_id))
			continue;

		has_split_display = BIT(DPU_CTL_SPLIT_DISPLAY) & features;

		DPU_DEBUG("ctl %d caps 0x%lX\n", iter.blk->id, features);

		if (needs_split_display != has_split_display)
			continue;

		ctls[i] = iter.blk;
		DPU_DEBUG("ctl %d match\n", iter.blk->id);

		if (++i == num_ctls)
			break;
	}

	if (i != num_ctls)
		return -ENAVAIL;

	for (i = 0; i < ARRAY_SIZE(ctls) && i < num_ctls; i++) {
		ctls[i]->enc_id = enc_id;
		trace_dpu_rm_reserve_ctls(ctls[i]->id, enc_id);
	}

	return 0;
}

static int _dpu_rm_reserve_intf(
		struct dpu_rm *rm,
		uint32_t enc_id,
		uint32_t id,
		enum dpu_hw_blk_type type)
{
	struct dpu_rm_hw_iter iter;
	int ret = 0;

	/* Find the block entry in the rm, and note the reservation */
	dpu_rm_init_hw_iter(&iter, 0, type);
	while (_dpu_rm_get_hw_locked(rm, &iter)) {
		if (iter.blk->id != id)
			continue;

		if (RESERVED_BY_OTHER(iter.blk, enc_id)) {
			DPU_ERROR("type %d id %d already reserved\n", type, id);
			return -ENAVAIL;
		}

		iter.blk->enc_id = enc_id;
		trace_dpu_rm_reserve_intf(iter.blk->id, enc_id);
		break;
	}

	/* Shouldn't happen since intfs are fixed at probe */
	if (!iter.hw) {
		DPU_ERROR("couldn't find type %d id %d\n", type, id);
		return -EINVAL;
	}

	return ret;
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
		ret = _dpu_rm_reserve_intf(rm, enc_id, id,
				DPU_HW_BLK_INTF);
		if (ret)
			return ret;
	}

	return ret;
}

static int _dpu_rm_make_reservation(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
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
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
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

static void _dpu_rm_release_reservation(struct dpu_rm *rm, uint32_t enc_id)
{
	struct dpu_rm_hw_blk *blk;
	enum dpu_hw_blk_type type;

	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (blk->enc_id == enc_id) {
				blk->enc_id = 0;
				DPU_DEBUG("rel enc %d %d %d\n", enc_id,
					  type, blk->id);
			}
		}
	}
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

	ret = _dpu_rm_populate_requirements(rm, enc, crtc_state, &reqs,
					    topology);
	if (ret) {
		DPU_ERROR("failed to populate hw requirements\n");
		goto end;
	}

	ret = _dpu_rm_make_reservation(rm, enc, crtc_state, &reqs);
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
