/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"[drm:%s] " fmt, __func__
#include "dpu_kms.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_pingpong.h"
#include "dpu_hw_intf.h"
#include "dpu_encoder.h"
#include "dpu_trace.h"

#define RESERVED_BY_OTHER(h, r) \
	((h)->rsvp && ((h)->rsvp->enc_id != (r)->enc_id))

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
 * struct dpu_rm_rsvp - Use Case Reservation tagging structure
 *	Used to tag HW blocks as reserved by a CRTC->Encoder->Connector chain
 *	By using as a tag, rather than lists of pointers to HW blocks used
 *	we can avoid some list management since we don't know how many blocks
 *	of each type a given use case may require.
 * @list:	List head for list of all reservations
 * @seq:	Global RSVP sequence number for debugging, especially for
 *		differentiating differenct allocations for same encoder.
 * @enc_id:	Reservations are tracked by Encoder DRM object ID.
 *		CRTCs may be connected to multiple Encoders.
 *		An encoder or connector id identifies the display path.
 */
struct dpu_rm_rsvp {
	struct list_head list;
	uint32_t seq;
	uint32_t enc_id;
};

/**
 * struct dpu_rm_hw_blk - hardware block tracking list member
 * @list:	List head for list of all hardware blocks tracking items
 * @rsvp:	Pointer to use case reservation if reserved by a client
 * @rsvp_nxt:	Temporary pointer used during reservation to the incoming
 *		request. Will be swapped into rsvp if proposal is accepted
 * @type:	Type of hardware block this structure tracks
 * @id:		Hardware ID number, within it's own space, ie. LM_X
 * @catalog:	Pointer to the hardware catalog entry for this block
 * @hw:		Pointer to the hardware register access object for this block
 */
struct dpu_rm_hw_blk {
	struct list_head list;
	struct dpu_rm_rsvp *rsvp;
	struct dpu_rm_rsvp *rsvp_nxt;
	enum dpu_hw_blk_type type;
	uint32_t id;
	struct dpu_hw_blk *hw;
};

/**
 * dpu_rm_dbg_rsvp_stage - enum of steps in making reservation for event logging
 */
enum dpu_rm_dbg_rsvp_stage {
	DPU_RM_STAGE_BEGIN,
	DPU_RM_STAGE_AFTER_CLEAR,
	DPU_RM_STAGE_AFTER_RSVPNEXT,
	DPU_RM_STAGE_FINAL
};

static void _dpu_rm_print_rsvps(
		struct dpu_rm *rm,
		enum dpu_rm_dbg_rsvp_stage stage)
{
	struct dpu_rm_rsvp *rsvp;
	struct dpu_rm_hw_blk *blk;
	enum dpu_hw_blk_type type;

	DPU_DEBUG("%d\n", stage);

	list_for_each_entry(rsvp, &rm->rsvps, list) {
		DRM_DEBUG_KMS("%d rsvp[s%ue%u]\n", stage, rsvp->seq,
			      rsvp->enc_id);
	}

	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (!blk->rsvp && !blk->rsvp_nxt)
				continue;

			DRM_DEBUG_KMS("%d rsvp[s%ue%u->s%ue%u] %d %d\n", stage,
				(blk->rsvp) ? blk->rsvp->seq : 0,
				(blk->rsvp) ? blk->rsvp->enc_id : 0,
				(blk->rsvp_nxt) ? blk->rsvp_nxt->seq : 0,
				(blk->rsvp_nxt) ? blk->rsvp_nxt->enc_id : 0,
				blk->type, blk->id);
		}
	}
}

struct dpu_hw_mdp *dpu_rm_get_mdp(struct dpu_rm *rm)
{
	return rm->hw_mdp;
}

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
		struct dpu_rm_rsvp *rsvp = i->blk->rsvp;

		if (i->blk->type != i->type) {
			DPU_ERROR("found incorrect block type %d on %d list\n",
					i->blk->type, i->type);
			return false;
		}

		if ((i->enc_id == 0) || (rsvp && rsvp->enc_id == i->enc_id)) {
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

	struct dpu_rm_rsvp *rsvp_cur, *rsvp_nxt;
	struct dpu_rm_hw_blk *hw_cur, *hw_nxt;
	enum dpu_hw_blk_type type;

	if (!rm) {
		DPU_ERROR("invalid rm\n");
		return -EINVAL;
	}

	list_for_each_entry_safe(rsvp_cur, rsvp_nxt, &rm->rsvps, list) {
		list_del(&rsvp_cur->list);
		kfree(rsvp_cur);
	}


	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry_safe(hw_cur, hw_nxt, &rm->hw_blks[type],
				list) {
			list_del(&hw_cur->list);
			_dpu_rm_hw_destroy(hw_cur->type, hw_cur->hw);
			kfree(hw_cur);
		}
	}

	dpu_hw_mdp_destroy(rm->hw_mdp);
	rm->hw_mdp = NULL;

	mutex_destroy(&rm->rm_lock);

	return 0;
}

static int _dpu_rm_hw_blk_create(
		struct dpu_rm *rm,
		struct dpu_mdss_cfg *cat,
		void __iomem *mmio,
		enum dpu_hw_blk_type type,
		uint32_t id,
		void *hw_catalog_info)
{
	struct dpu_rm_hw_blk *blk;
	struct dpu_hw_mdp *hw_mdp;
	void *hw;

	hw_mdp = rm->hw_mdp;

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

	blk->type = type;
	blk->id = id;
	blk->hw = hw;
	list_add_tail(&blk->list, &rm->hw_blks[type]);

	return 0;
}

int dpu_rm_init(struct dpu_rm *rm,
		struct dpu_mdss_cfg *cat,
		void __iomem *mmio,
		struct drm_device *dev)
{
	int rc, i;
	enum dpu_hw_blk_type type;

	if (!rm || !cat || !mmio || !dev) {
		DPU_ERROR("invalid kms\n");
		return -EINVAL;
	}

	/* Clear, setup lists */
	memset(rm, 0, sizeof(*rm));

	mutex_init(&rm->rm_lock);

	INIT_LIST_HEAD(&rm->rsvps);
	for (type = 0; type < DPU_HW_BLK_MAX; type++)
		INIT_LIST_HEAD(&rm->hw_blks[type]);

	rm->dev = dev;

	/* Some of the sub-blocks require an mdptop to be created */
	rm->hw_mdp = dpu_hw_mdptop_init(MDP_TOP, mmio, cat);
	if (IS_ERR_OR_NULL(rm->hw_mdp)) {
		rc = PTR_ERR(rm->hw_mdp);
		rm->hw_mdp = NULL;
		DPU_ERROR("failed: mdp hw not available\n");
		goto fail;
	}

	/* Interrogate HW catalog and create tracking items for hw blocks */
	for (i = 0; i < cat->mixer_count; i++) {
		struct dpu_lm_cfg *lm = &cat->mixer[i];

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
 * @rsvp: reservation currently being created
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
		struct dpu_rm_rsvp *rsvp,
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
	if (RESERVED_BY_OTHER(lm, rsvp)) {
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

	if (RESERVED_BY_OTHER(*pp, rsvp)) {
		DPU_DEBUG("lm %d pp %d already reserved\n", lm->id,
				(*pp)->id);
		return false;
	}

	return true;
}

static int _dpu_rm_reserve_lms(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
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
				rm, rsvp, reqs, lm[lm_count],
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
					rm, rsvp, reqs, iter_j.blk,
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

		lm[i]->rsvp_nxt = rsvp;
		pp[i]->rsvp_nxt = rsvp;

		trace_dpu_rm_reserve_lms(lm[i]->id, lm[i]->type, rsvp->enc_id,
					 pp[i]->id);
	}

	return rc;
}

static int _dpu_rm_reserve_ctls(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
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

		if (RESERVED_BY_OTHER(iter.blk, rsvp))
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
		ctls[i]->rsvp_nxt = rsvp;
		trace_dpu_rm_reserve_ctls(ctls[i]->id, ctls[i]->type,
					  rsvp->enc_id);
	}

	return 0;
}

static int _dpu_rm_reserve_intf(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
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

		if (RESERVED_BY_OTHER(iter.blk, rsvp)) {
			DPU_ERROR("type %d id %d already reserved\n", type, id);
			return -ENAVAIL;
		}

		iter.blk->rsvp_nxt = rsvp;
		trace_dpu_rm_reserve_intf(iter.blk->id, iter.blk->type,
					  rsvp->enc_id);
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
		struct dpu_rm_rsvp *rsvp,
		struct dpu_encoder_hw_resources *hw_res)
{
	int i, ret = 0;
	u32 id;

	for (i = 0; i < ARRAY_SIZE(hw_res->intfs); i++) {
		if (hw_res->intfs[i] == INTF_MODE_NONE)
			continue;
		id = i + INTF_0;
		ret = _dpu_rm_reserve_intf(rm, rsvp, id,
				DPU_HW_BLK_INTF);
		if (ret)
			return ret;
	}

	return ret;
}

static int _dpu_rm_make_next_rsvp(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct dpu_rm_rsvp *rsvp,
		struct dpu_rm_requirements *reqs)
{
	int ret;

	/* Create reservation info, tag reserved blocks with it as we go */
	rsvp->seq = ++rm->rsvp_next_seq;
	rsvp->enc_id = enc->base.id;
	list_add_tail(&rsvp->list, &rm->rsvps);

	ret = _dpu_rm_reserve_lms(rm, rsvp, reqs);
	if (ret) {
		DPU_ERROR("unable to find appropriate mixers\n");
		return ret;
	}

	ret = _dpu_rm_reserve_ctls(rm, rsvp, &reqs->topology);
	if (ret) {
		DPU_ERROR("unable to find appropriate CTL\n");
		return ret;
	}

	ret = _dpu_rm_reserve_intf_related_hw(rm, rsvp, &reqs->hw_res);
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

static struct dpu_rm_rsvp *_dpu_rm_get_rsvp(
		struct dpu_rm *rm,
		struct drm_encoder *enc)
{
	struct dpu_rm_rsvp *i;

	if (!rm || !enc) {
		DPU_ERROR("invalid params\n");
		return NULL;
	}

	if (list_empty(&rm->rsvps))
		return NULL;

	list_for_each_entry(i, &rm->rsvps, list)
		if (i->enc_id == enc->base.id)
			return i;

	return NULL;
}

/**
 * _dpu_rm_release_rsvp - release resources and release a reservation
 * @rm:	KMS handle
 * @rsvp:	RSVP pointer to release and release resources for
 */
static void _dpu_rm_release_rsvp(struct dpu_rm *rm, struct dpu_rm_rsvp *rsvp)
{
	struct dpu_rm_rsvp *rsvp_c, *rsvp_n;
	struct dpu_rm_hw_blk *blk;
	enum dpu_hw_blk_type type;

	if (!rsvp)
		return;

	DPU_DEBUG("rel rsvp %d enc %d\n", rsvp->seq, rsvp->enc_id);

	list_for_each_entry_safe(rsvp_c, rsvp_n, &rm->rsvps, list) {
		if (rsvp == rsvp_c) {
			list_del(&rsvp_c->list);
			break;
		}
	}

	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (blk->rsvp == rsvp) {
				blk->rsvp = NULL;
				DPU_DEBUG("rel rsvp %d enc %d %d %d\n",
						rsvp->seq, rsvp->enc_id,
						blk->type, blk->id);
			}
			if (blk->rsvp_nxt == rsvp) {
				blk->rsvp_nxt = NULL;
				DPU_DEBUG("rel rsvp_nxt %d enc %d %d %d\n",
						rsvp->seq, rsvp->enc_id,
						blk->type, blk->id);
			}
		}
	}

	kfree(rsvp);
}

void dpu_rm_release(struct dpu_rm *rm, struct drm_encoder *enc)
{
	struct dpu_rm_rsvp *rsvp;

	if (!rm || !enc) {
		DPU_ERROR("invalid params\n");
		return;
	}

	mutex_lock(&rm->rm_lock);

	rsvp = _dpu_rm_get_rsvp(rm, enc);
	if (!rsvp) {
		DPU_ERROR("failed to find rsvp for enc %d\n", enc->base.id);
		goto end;
	}

	_dpu_rm_release_rsvp(rm, rsvp);
end:
	mutex_unlock(&rm->rm_lock);
}

static void _dpu_rm_commit_rsvp(struct dpu_rm *rm, struct dpu_rm_rsvp *rsvp)
{
	struct dpu_rm_hw_blk *blk;
	enum dpu_hw_blk_type type;

	/* Swap next rsvp to be the active */
	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (blk->rsvp_nxt) {
				blk->rsvp = blk->rsvp_nxt;
				blk->rsvp_nxt = NULL;
			}
		}
	}
}

int dpu_rm_reserve(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct msm_display_topology topology,
		bool test_only)
{
	struct dpu_rm_rsvp *rsvp_cur, *rsvp_nxt;
	struct dpu_rm_requirements reqs;
	int ret;

	/* Check if this is just a page-flip */
	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return 0;

	DRM_DEBUG_KMS("reserving hw for enc %d crtc %d test_only %d\n",
		      enc->base.id, crtc_state->crtc->base.id, test_only);

	mutex_lock(&rm->rm_lock);

	_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_BEGIN);

	ret = _dpu_rm_populate_requirements(rm, enc, crtc_state, &reqs,
					    topology);
	if (ret) {
		DPU_ERROR("failed to populate hw requirements\n");
		goto end;
	}

	/*
	 * We only support one active reservation per-hw-block. But to implement
	 * transactional semantics for test-only, and for allowing failure while
	 * modifying your existing reservation, over the course of this
	 * function we can have two reservations:
	 * Current: Existing reservation
	 * Next: Proposed reservation. The proposed reservation may fail, or may
	 *       be discarded if in test-only mode.
	 * If reservation is successful, and we're not in test-only, then we
	 * replace the current with the next.
	 */
	rsvp_nxt = kzalloc(sizeof(*rsvp_nxt), GFP_KERNEL);
	if (!rsvp_nxt) {
		ret = -ENOMEM;
		goto end;
	}

	rsvp_cur = _dpu_rm_get_rsvp(rm, enc);

	/* Check the proposed reservation, store it in hw's "next" field */
	ret = _dpu_rm_make_next_rsvp(rm, enc, crtc_state, rsvp_nxt, &reqs);

	_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_AFTER_RSVPNEXT);

	if (ret) {
		DPU_ERROR("failed to reserve hw resources: %d\n", ret);
		_dpu_rm_release_rsvp(rm, rsvp_nxt);
	} else if (test_only) {
		/*
		 * Normally, if test_only, test the reservation and then undo
		 * However, if the user requests LOCK, then keep the reservation
		 * made during the atomic_check phase.
		 */
		DPU_DEBUG("test_only: discard test rsvp[s%de%d]\n",
				rsvp_nxt->seq, rsvp_nxt->enc_id);
		_dpu_rm_release_rsvp(rm, rsvp_nxt);
	} else {
		_dpu_rm_release_rsvp(rm, rsvp_cur);

		_dpu_rm_commit_rsvp(rm, rsvp_nxt);
	}

	_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_FINAL);

end:
	mutex_unlock(&rm->rm_lock);

	return ret;
}
