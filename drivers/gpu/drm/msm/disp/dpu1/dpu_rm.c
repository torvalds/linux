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
#include "dpu_hw_cdm.h"
#include "dpu_hw_pingpong.h"
#include "dpu_hw_intf.h"
#include "dpu_encoder.h"
#include "dpu_trace.h"

#define RESERVED_BY_OTHER(h, r) \
	((h)->rsvp && ((h)->rsvp->enc_id != (r)->enc_id))

#define RM_RQ_LOCK(r) ((r)->top_ctrl & BIT(DPU_RM_TOPCTL_RESERVE_LOCK))
#define RM_RQ_CLEAR(r) ((r)->top_ctrl & BIT(DPU_RM_TOPCTL_RESERVE_CLEAR))
#define RM_RQ_DS(r) ((r)->top_ctrl & BIT(DPU_RM_TOPCTL_DS))
#define RM_IS_TOPOLOGY_MATCH(t, r) ((t).num_lm == (r).num_lm && \
				(t).num_comp_enc == (r).num_enc && \
				(t).num_intf == (r).num_intf)

struct dpu_rm_topology_def {
	enum dpu_rm_topology_name top_name;
	int num_lm;
	int num_comp_enc;
	int num_intf;
	int num_ctl;
	int needs_split_display;
};

static const struct dpu_rm_topology_def g_top_table[] = {
	{   DPU_RM_TOPOLOGY_NONE,                 0, 0, 0, 0, false },
	{   DPU_RM_TOPOLOGY_SINGLEPIPE,           1, 0, 1, 1, false },
	{   DPU_RM_TOPOLOGY_DUALPIPE,             2, 0, 2, 2, true  },
	{   DPU_RM_TOPOLOGY_DUALPIPE_3DMERGE,     2, 0, 1, 1, false },
};

/**
 * struct dpu_rm_requirements - Reservation requirements parameter bundle
 * @top_ctrl:  topology control preference from kernel client
 * @top:       selected topology for the display
 * @hw_res:	   Hardware resources required as reported by the encoders
 */
struct dpu_rm_requirements {
	uint64_t top_ctrl;
	const struct dpu_rm_topology_def *topology;
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
 * @topology	DRM<->HW topology use case
 */
struct dpu_rm_rsvp {
	struct list_head list;
	uint32_t seq;
	uint32_t enc_id;
	enum dpu_rm_topology_name topology;
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
		DRM_DEBUG_KMS("%d rsvp[s%ue%u] topology %d\n", stage, rsvp->seq,
			      rsvp->enc_id, rsvp->topology);
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

enum dpu_rm_topology_name
dpu_rm_get_topology_name(struct msm_display_topology topology)
{
	int i;

	for (i = 0; i < DPU_RM_TOPOLOGY_MAX; i++)
		if (RM_IS_TOPOLOGY_MATCH(g_top_table[i], topology))
			return g_top_table[i].top_name;

	return DPU_RM_TOPOLOGY_NONE;
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
	case DPU_HW_BLK_CDM:
		dpu_hw_cdm_destroy(hw);
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
	case DPU_HW_BLK_CDM:
		hw = dpu_hw_cdm_init(id, mmio, cat, hw_mdp);
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

	for (i = 0; i < cat->cdm_count; i++) {
		rc = _dpu_rm_hw_blk_create(rm, cat, mmio, DPU_HW_BLK_CDM,
				cat->cdm[i].id, &cat->cdm[i]);
		if (rc) {
			DPU_ERROR("failed: cdm hw not available\n");
			goto fail;
		}
	}

	return 0;

fail:
	dpu_rm_destroy(rm);

	return rc;
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

	if (!reqs->topology->num_lm) {
		DPU_ERROR("invalid number of lm: %d\n", reqs->topology->num_lm);
		return -EINVAL;
	}

	/* Find a primary mixer */
	dpu_rm_init_hw_iter(&iter_i, 0, DPU_HW_BLK_LM);
	while (lm_count != reqs->topology->num_lm &&
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

		while (lm_count != reqs->topology->num_lm &&
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

	if (lm_count != reqs->topology->num_lm) {
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
		const struct dpu_rm_topology_def *top)
{
	struct dpu_rm_hw_blk *ctls[MAX_BLOCKS];
	struct dpu_rm_hw_iter iter;
	int i = 0;

	memset(&ctls, 0, sizeof(ctls));

	dpu_rm_init_hw_iter(&iter, 0, DPU_HW_BLK_CTL);
	while (_dpu_rm_get_hw_locked(rm, &iter)) {
		const struct dpu_hw_ctl *ctl = to_dpu_hw_ctl(iter.blk->hw);
		unsigned long features = ctl->caps->features;
		bool has_split_display;

		if (RESERVED_BY_OTHER(iter.blk, rsvp))
			continue;

		has_split_display = BIT(DPU_CTL_SPLIT_DISPLAY) & features;

		DPU_DEBUG("ctl %d caps 0x%lX\n", iter.blk->id, features);

		if (top->needs_split_display != has_split_display)
			continue;

		ctls[i] = iter.blk;
		DPU_DEBUG("ctl %d match\n", iter.blk->id);

		if (++i == top->num_ctl)
			break;
	}

	if (i != top->num_ctl)
		return -ENAVAIL;

	for (i = 0; i < ARRAY_SIZE(ctls) && i < top->num_ctl; i++) {
		ctls[i]->rsvp_nxt = rsvp;
		trace_dpu_rm_reserve_ctls(ctls[i]->id, ctls[i]->type,
					  rsvp->enc_id);
	}

	return 0;
}

static int _dpu_rm_reserve_cdm(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
		uint32_t id,
		enum dpu_hw_blk_type type)
{
	struct dpu_rm_hw_iter iter;

	DRM_DEBUG_KMS("type %d id %d\n", type, id);

	dpu_rm_init_hw_iter(&iter, 0, DPU_HW_BLK_CDM);
	while (_dpu_rm_get_hw_locked(rm, &iter)) {
		const struct dpu_hw_cdm *cdm = to_dpu_hw_cdm(iter.blk->hw);
		const struct dpu_cdm_cfg *caps = cdm->caps;
		bool match = false;

		if (RESERVED_BY_OTHER(iter.blk, rsvp))
			continue;

		if (type == DPU_HW_BLK_INTF && id != INTF_MAX)
			match = test_bit(id, &caps->intf_connect);

		DRM_DEBUG_KMS("iter: type:%d id:%d enc:%d cdm:%lu match:%d\n",
			      iter.blk->type, iter.blk->id, rsvp->enc_id,
			      caps->intf_connect, match);

		if (!match)
			continue;

		trace_dpu_rm_reserve_cdm(iter.blk->id, iter.blk->type,
					 rsvp->enc_id);
		iter.blk->rsvp_nxt = rsvp;
		break;
	}

	if (!iter.hw) {
		DPU_ERROR("couldn't reserve cdm for type %d id %d\n", type, id);
		return -ENAVAIL;
	}

	return 0;
}

static int _dpu_rm_reserve_intf(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
		uint32_t id,
		enum dpu_hw_blk_type type,
		bool needs_cdm)
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

	if (needs_cdm)
		ret = _dpu_rm_reserve_cdm(rm, rsvp, id, type);

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
				DPU_HW_BLK_INTF, hw_res->needs_cdm);
		if (ret)
			return ret;
	}

	return ret;
}

static int _dpu_rm_make_next_rsvp(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		struct dpu_rm_rsvp *rsvp,
		struct dpu_rm_requirements *reqs)
{
	int ret;
	struct dpu_rm_topology_def topology;

	/* Create reservation info, tag reserved blocks with it as we go */
	rsvp->seq = ++rm->rsvp_next_seq;
	rsvp->enc_id = enc->base.id;
	rsvp->topology = reqs->topology->top_name;
	list_add_tail(&rsvp->list, &rm->rsvps);

	ret = _dpu_rm_reserve_lms(rm, rsvp, reqs);
	if (ret) {
		DPU_ERROR("unable to find appropriate mixers\n");
		return ret;
	}

	/*
	 * Do assignment preferring to give away low-resource CTLs first:
	 * - Check mixers without Split Display
	 * - Only then allow to grab from CTLs with split display capability
	 */
	_dpu_rm_reserve_ctls(rm, rsvp, reqs->topology);
	if (ret && !reqs->topology->needs_split_display) {
		memcpy(&topology, reqs->topology, sizeof(topology));
		topology.needs_split_display = true;
		_dpu_rm_reserve_ctls(rm, rsvp, &topology);
	}
	if (ret) {
		DPU_ERROR("unable to find appropriate CTL\n");
		return ret;
	}

	/* Assign INTFs and blks whose usage is tied to them: CTL & CDM */
	ret = _dpu_rm_reserve_intf_related_hw(rm, rsvp, &reqs->hw_res);
	if (ret)
		return ret;

	return ret;
}

static int _dpu_rm_populate_requirements(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		struct dpu_rm_requirements *reqs,
		struct msm_display_topology req_topology)
{
	int i;

	memset(reqs, 0, sizeof(*reqs));

	dpu_encoder_get_hw_resources(enc, &reqs->hw_res, conn_state);

	for (i = 0; i < DPU_RM_TOPOLOGY_MAX; i++) {
		if (RM_IS_TOPOLOGY_MATCH(g_top_table[i],
					req_topology)) {
			reqs->topology = &g_top_table[i];
			break;
		}
	}

	if (!reqs->topology) {
		DPU_ERROR("invalid topology for the display\n");
		return -EINVAL;
	}

	/**
	 * Set the requirement based on caps if not set from user space
	 * This will ensure to select LM tied with DS blocks
	 * Currently, DS blocks are tied with LM 0 and LM 1 (primary display)
	 */
	if (!RM_RQ_DS(reqs) && rm->hw_mdp->caps->has_dest_scaler &&
		conn_state->connector->connector_type == DRM_MODE_CONNECTOR_DSI)
		reqs->top_ctrl |= BIT(DPU_RM_TOPCTL_DS);

	DRM_DEBUG_KMS("top_ctrl: 0x%llX num_h_tiles: %d\n", reqs->top_ctrl,
		      reqs->hw_res.display_num_of_h_tiles);
	DRM_DEBUG_KMS("num_lm: %d num_ctl: %d topology: %d split_display: %d\n",
		      reqs->topology->num_lm, reqs->topology->num_ctl,
		      reqs->topology->top_name,
		      reqs->topology->needs_split_display);

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

static struct drm_connector *_dpu_rm_get_connector(
		struct drm_encoder *enc)
{
	struct drm_connector *conn = NULL;
	struct list_head *connector_list =
			&enc->dev->mode_config.connector_list;

	list_for_each_entry(conn, connector_list, head)
		if (conn->encoder == enc)
			return conn;

	return NULL;
}

/**
 * _dpu_rm_release_rsvp - release resources and release a reservation
 * @rm:	KMS handle
 * @rsvp:	RSVP pointer to release and release resources for
 */
static void _dpu_rm_release_rsvp(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
		struct drm_connector *conn)
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
	struct drm_connector *conn;

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

	conn = _dpu_rm_get_connector(enc);
	if (!conn) {
		DPU_ERROR("failed to get connector for enc %d\n", enc->base.id);
		goto end;
	}

	_dpu_rm_release_rsvp(rm, rsvp, conn);
end:
	mutex_unlock(&rm->rm_lock);
}

static int _dpu_rm_commit_rsvp(
		struct dpu_rm *rm,
		struct dpu_rm_rsvp *rsvp,
		struct drm_connector_state *conn_state)
{
	struct dpu_rm_hw_blk *blk;
	enum dpu_hw_blk_type type;
	int ret = 0;

	/* Swap next rsvp to be the active */
	for (type = 0; type < DPU_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (blk->rsvp_nxt) {
				blk->rsvp = blk->rsvp_nxt;
				blk->rsvp_nxt = NULL;
			}
		}
	}

	if (!ret)
		DRM_DEBUG_KMS("rsrv enc %d topology %d\n", rsvp->enc_id,
			      rsvp->topology);

	return ret;
}

int dpu_rm_reserve(
		struct dpu_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		struct msm_display_topology topology,
		bool test_only)
{
	struct dpu_rm_rsvp *rsvp_cur, *rsvp_nxt;
	struct dpu_rm_requirements reqs;
	int ret;

	if (!rm || !enc || !crtc_state || !conn_state) {
		DPU_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	/* Check if this is just a page-flip */
	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return 0;

	DRM_DEBUG_KMS("reserving hw for conn %d enc %d crtc %d test_only %d\n",
		      conn_state->connector->base.id, enc->base.id,
		      crtc_state->crtc->base.id, test_only);

	mutex_lock(&rm->rm_lock);

	_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_BEGIN);

	ret = _dpu_rm_populate_requirements(rm, enc, crtc_state,
			conn_state, &reqs, topology);
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

	/*
	 * User can request that we clear out any reservation during the
	 * atomic_check phase by using this CLEAR bit
	 */
	if (rsvp_cur && test_only && RM_RQ_CLEAR(&reqs)) {
		DPU_DEBUG("test_only & CLEAR: clear rsvp[s%de%d]\n",
				rsvp_cur->seq, rsvp_cur->enc_id);
		_dpu_rm_release_rsvp(rm, rsvp_cur, conn_state->connector);
		rsvp_cur = NULL;
		_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_AFTER_CLEAR);
	}

	/* Check the proposed reservation, store it in hw's "next" field */
	ret = _dpu_rm_make_next_rsvp(rm, enc, crtc_state, conn_state,
			rsvp_nxt, &reqs);

	_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_AFTER_RSVPNEXT);

	if (ret) {
		DPU_ERROR("failed to reserve hw resources: %d\n", ret);
		_dpu_rm_release_rsvp(rm, rsvp_nxt, conn_state->connector);
	} else if (test_only && !RM_RQ_LOCK(&reqs)) {
		/*
		 * Normally, if test_only, test the reservation and then undo
		 * However, if the user requests LOCK, then keep the reservation
		 * made during the atomic_check phase.
		 */
		DPU_DEBUG("test_only: discard test rsvp[s%de%d]\n",
				rsvp_nxt->seq, rsvp_nxt->enc_id);
		_dpu_rm_release_rsvp(rm, rsvp_nxt, conn_state->connector);
	} else {
		if (test_only && RM_RQ_LOCK(&reqs))
			DPU_DEBUG("test_only & LOCK: lock rsvp[s%de%d]\n",
					rsvp_nxt->seq, rsvp_nxt->enc_id);

		_dpu_rm_release_rsvp(rm, rsvp_cur, conn_state->connector);

		ret = _dpu_rm_commit_rsvp(rm, rsvp_nxt, conn_state);
	}

	_dpu_rm_print_rsvps(rm, DPU_RM_STAGE_FINAL);

end:
	mutex_unlock(&rm->rm_lock);

	return ret;
}
