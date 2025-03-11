// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s] " fmt, __func__
#include "dpu_kms.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_ctl.h"
#include "dpu_hw_cdm.h"
#include "dpu_hw_cwb.h"
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
				     uint32_t crtc_id)
{
	return res_map[idx] && res_map[idx] != crtc_id;
}

/**
 * dpu_rm_init - Read hardware catalog and create reservation tracking objects
 *	for all HW blocks.
 * @dev:  Corresponding device for devres management
 * @rm: DPU Resource Manager handle
 * @cat: Pointer to hardware catalog
 * @mdss_data: Pointer to MDSS / UBWC configuration
 * @mmio: mapped register io address of MDP
 * @return: 0 on Success otherwise -ERROR
 */
int dpu_rm_init(struct drm_device *dev,
		struct dpu_rm *rm,
		const struct dpu_mdss_cfg *cat,
		const struct msm_mdss_data *mdss_data,
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

		hw = dpu_hw_lm_init(dev, lm, mmio);
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

		hw = dpu_hw_merge_3d_init(dev, merge_3d, mmio);
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

		hw = dpu_hw_pingpong_init(dev, pp, mmio, cat->mdss_ver);
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

		hw = dpu_hw_intf_init(dev, intf, mmio, cat->mdss_ver);
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

		hw = dpu_hw_wb_init(dev, wb, mmio, cat->mdss_ver);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed wb object creation: err %d\n", rc);
			goto fail;
		}
		rm->hw_wb[wb->id - WB_0] = hw;
	}

	for (i = 0; i < cat->cwb_count; i++) {
		struct dpu_hw_cwb *hw;
		const struct dpu_cwb_cfg *cwb = &cat->cwb[i];

		hw = dpu_hw_cwb_init(dev, cwb, mmio);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed cwb object creation: err %d\n", rc);
			goto fail;
		}
		rm->cwb_blks[cwb->id - CWB_0] = &hw->base;
	}

	for (i = 0; i < cat->ctl_count; i++) {
		struct dpu_hw_ctl *hw;
		const struct dpu_ctl_cfg *ctl = &cat->ctl[i];

		hw = dpu_hw_ctl_init(dev, ctl, mmio, cat->mixer_count, cat->mixer);
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

		hw = dpu_hw_dspp_init(dev, dspp, mmio);
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
			hw = dpu_hw_dsc_init_1_2(dev, dsc, mmio);
		else
			hw = dpu_hw_dsc_init(dev, dsc, mmio);

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

		hw = dpu_hw_sspp_init(dev, sspp, mmio, mdss_data, cat->mdss_ver);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed sspp object creation: err %d\n", rc);
			goto fail;
		}
		rm->hw_sspp[sspp->id - SSPP_NONE] = hw;
	}

	if (cat->cdm) {
		struct dpu_hw_cdm *hw;

		hw = dpu_hw_cdm_init(dev, cat->cdm, mmio, cat->mdss_ver);
		if (IS_ERR(hw)) {
			rc = PTR_ERR(hw);
			DPU_ERROR("failed cdm object creation: err %d\n", rc);
			goto fail;
		}
		rm->cdm_blk = &hw->base;
	}

	return 0;

fail:
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
 *
 * Returns: lm peer mixed id on success or %-EINVAL on error
 */
static int _dpu_rm_get_lm_peer(struct dpu_rm *rm, int primary_idx)
{
	const struct dpu_lm_cfg *prim_lm_cfg;

	prim_lm_cfg = to_dpu_hw_mixer(rm->mixer_blks[primary_idx])->cap;

	if (prim_lm_cfg->lm_pair >= LM_0 && prim_lm_cfg->lm_pair < LM_MAX)
		return prim_lm_cfg->lm_pair - LM_0;
	return -EINVAL;
}

static int _dpu_rm_reserve_cwb_mux_and_pingpongs(struct dpu_rm *rm,
						 struct dpu_global_state *global_state,
						 uint32_t crtc_id,
						 struct msm_display_topology *topology)
{
	int num_cwb_mux = topology->num_lm, cwb_mux_count = 0;
	int cwb_pp_start_idx = PINGPONG_CWB_0 - PINGPONG_0;
	int cwb_pp_idx[MAX_BLOCKS];
	int cwb_mux_idx[MAX_BLOCKS];

	/*
	 * Reserve additional dedicated CWB PINGPONG blocks and muxes for each
	 * mixer
	 *
	 * TODO: add support reserving resources for platforms with no
	 *       PINGPONG_CWB
	 */
	for (int i = 0; i < ARRAY_SIZE(rm->mixer_blks) &&
	     cwb_mux_count < num_cwb_mux; i++) {
		for (int j = 0; j < ARRAY_SIZE(rm->cwb_blks); j++) {
			/*
			 * Odd LMs must be assigned to odd CWB muxes and even
			 * LMs with even CWB muxes.
			 *
			 * Since the RM HW block array index is based on the HW
			 * block ids, we can also use the array index to enforce
			 * the odd/even rule. See dpu_rm_init() for more
			 * information
			 */
			if (reserved_by_other(global_state->cwb_to_crtc_id, j, crtc_id) ||
			    i % 2 != j % 2)
				continue;

			cwb_mux_idx[cwb_mux_count] = j;
			cwb_pp_idx[cwb_mux_count] = j + cwb_pp_start_idx;
			cwb_mux_count++;
			break;
		}
	}

	if (cwb_mux_count != num_cwb_mux) {
		DPU_ERROR("Unable to reserve all CWB PINGPONGs\n");
		return -ENAVAIL;
	}

	for (int i = 0; i < cwb_mux_count; i++) {
		global_state->pingpong_to_crtc_id[cwb_pp_idx[i]] = crtc_id;
		global_state->cwb_to_crtc_id[cwb_mux_idx[i]] = crtc_id;
	}

	return 0;
}

/**
 * _dpu_rm_check_lm_and_get_connected_blks - check if proposed layer mixer meets
 *	proposed use case requirements, incl. hardwired dependent blocks like
 *	pingpong
 * @rm: dpu resource manager handle
 * @global_state: resources shared across multiple kms objects
 * @crtc_id: crtc id requesting for allocation
 * @lm_idx: index of proposed layer mixer in rm->mixer_blks[], function checks
 *      if lm, and all other hardwired blocks connected to the lm (pp) is
 *      available and appropriate
 * @pp_idx: output parameter, index of pingpong block attached to the layer
 *      mixer in rm->pingpong_blks[].
 * @dspp_idx: output parameter, index of dspp block attached to the layer
 *      mixer in rm->dspp_blks[].
 * @topology:  selected topology for the display
 * Return: true if lm matches all requirements, false otherwise
 */
static bool _dpu_rm_check_lm_and_get_connected_blks(struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		uint32_t crtc_id, int lm_idx, int *pp_idx, int *dspp_idx,
		struct msm_display_topology *topology)
{
	const struct dpu_lm_cfg *lm_cfg;
	int idx;

	/* Already reserved? */
	if (reserved_by_other(global_state->mixer_to_crtc_id, lm_idx, crtc_id)) {
		DPU_DEBUG("lm %d already reserved\n", lm_idx + LM_0);
		return false;
	}

	lm_cfg = to_dpu_hw_mixer(rm->mixer_blks[lm_idx])->cap;
	idx = lm_cfg->pingpong - PINGPONG_0;
	if (idx < 0 || idx >= ARRAY_SIZE(rm->pingpong_blks)) {
		DPU_ERROR("failed to get pp on lm %d\n", lm_cfg->pingpong);
		return false;
	}

	if (reserved_by_other(global_state->pingpong_to_crtc_id, idx, crtc_id)) {
		DPU_DEBUG("lm %d pp %d already reserved\n", lm_cfg->id,
				lm_cfg->pingpong);
		return false;
	}
	*pp_idx = idx;

	if (!topology->num_dspp)
		return true;

	idx = lm_cfg->dspp - DSPP_0;
	if (idx < 0 || idx >= ARRAY_SIZE(rm->dspp_blks)) {
		DPU_ERROR("failed to get dspp on lm %d\n", lm_cfg->dspp);
		return false;
	}

	if (reserved_by_other(global_state->dspp_to_crtc_id, idx, crtc_id)) {
		DPU_DEBUG("lm %d dspp %d already reserved\n", lm_cfg->id,
				lm_cfg->dspp);
		return false;
	}
	*dspp_idx = idx;

	return true;
}

static int _dpu_rm_reserve_lms(struct dpu_rm *rm,
			       struct dpu_global_state *global_state,
			       uint32_t crtc_id,
			       struct msm_display_topology *topology)

{
	int lm_idx[MAX_BLOCKS];
	int pp_idx[MAX_BLOCKS];
	int dspp_idx[MAX_BLOCKS] = {0};
	int i, lm_count = 0;

	if (!topology->num_lm) {
		DPU_ERROR("invalid number of lm: %d\n", topology->num_lm);
		return -EINVAL;
	}

	/* Find a primary mixer */
	for (i = 0; i < ARRAY_SIZE(rm->mixer_blks) &&
			lm_count < topology->num_lm; i++) {
		if (!rm->mixer_blks[i])
			continue;

		lm_count = 0;
		lm_idx[lm_count] = i;

		if (!_dpu_rm_check_lm_and_get_connected_blks(rm, global_state,
				crtc_id, i, &pp_idx[lm_count],
				&dspp_idx[lm_count], topology)) {
			continue;
		}

		++lm_count;

		/* Valid primary mixer found, find matching peers */
		if (lm_count < topology->num_lm) {
			int j = _dpu_rm_get_lm_peer(rm, i);

			/* ignore the peer if there is an error or if the peer was already processed */
			if (j < 0 || j < i)
				continue;

			if (!rm->mixer_blks[j])
				continue;

			if (!_dpu_rm_check_lm_and_get_connected_blks(rm,
					global_state, crtc_id, j,
					&pp_idx[lm_count], &dspp_idx[lm_count],
					topology)) {
				continue;
			}

			lm_idx[lm_count] = j;
			++lm_count;
		}
	}

	if (lm_count != topology->num_lm) {
		DPU_DEBUG("unable to find appropriate mixers\n");
		return -ENAVAIL;
	}

	for (i = 0; i < lm_count; i++) {
		global_state->mixer_to_crtc_id[lm_idx[i]] = crtc_id;
		global_state->pingpong_to_crtc_id[pp_idx[i]] = crtc_id;
		global_state->dspp_to_crtc_id[dspp_idx[i]] =
			topology->num_dspp ? crtc_id : 0;

		trace_dpu_rm_reserve_lms(lm_idx[i] + LM_0, crtc_id,
					 pp_idx[i] + PINGPONG_0);
	}

	return 0;
}

static int _dpu_rm_reserve_ctls(
		struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		uint32_t crtc_id,
		const struct msm_display_topology *top)
{
	int ctl_idx[MAX_BLOCKS];
	int i = 0, j, num_ctls;
	bool needs_split_display;

	/*
	 * For non-CWB mode, each hw_intf needs its own hw_ctl to program its
	 * control path.
	 *
	 * Hardcode num_ctls to 1 if CWB is enabled because in CWB, both the
	 * writeback and real-time encoders must be driven by the same control
	 * path
	 */
	if (top->cwb_enabled)
		num_ctls = 1;
	else
		num_ctls = top->num_intf;

	needs_split_display = _dpu_rm_needs_split_display(top);

	for (j = 0; j < ARRAY_SIZE(rm->ctl_blks); j++) {
		const struct dpu_hw_ctl *ctl;
		unsigned long features;
		bool has_split_display;

		if (!rm->ctl_blks[j])
			continue;
		if (reserved_by_other(global_state->ctl_to_crtc_id, j, crtc_id))
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
		global_state->ctl_to_crtc_id[ctl_idx[i]] = crtc_id;
		trace_dpu_rm_reserve_ctls(i + CTL_0, crtc_id);
	}

	return 0;
}

static int _dpu_rm_pingpong_next_index(struct dpu_global_state *global_state,
				       int start,
				       uint32_t crtc_id)
{
	int i;

	for (i = start; i < (PINGPONG_MAX - PINGPONG_0); i++) {
		if (global_state->pingpong_to_crtc_id[i] == crtc_id)
			return i;
	}

	return -ENAVAIL;
}

static int _dpu_rm_pingpong_dsc_check(int dsc_idx, int pp_idx)
{
	/*
	 * DSC with even index must be used with the PINGPONG with even index
	 * DSC with odd index must be used with the PINGPONG with odd index
	 */
	if ((dsc_idx & 0x01) != (pp_idx & 0x01))
		return -ENAVAIL;

	return 0;
}

static int _dpu_rm_dsc_alloc(struct dpu_rm *rm,
			     struct dpu_global_state *global_state,
			     uint32_t crtc_id,
			     const struct msm_display_topology *top)
{
	int num_dsc = 0;
	int pp_idx = 0;
	int dsc_idx;
	int ret;

	for (dsc_idx = 0; dsc_idx < ARRAY_SIZE(rm->dsc_blks) &&
	     num_dsc < top->num_dsc; dsc_idx++) {
		if (!rm->dsc_blks[dsc_idx])
			continue;

		if (reserved_by_other(global_state->dsc_to_crtc_id, dsc_idx, crtc_id))
			continue;

		pp_idx = _dpu_rm_pingpong_next_index(global_state, pp_idx, crtc_id);
		if (pp_idx < 0)
			return -ENAVAIL;

		ret = _dpu_rm_pingpong_dsc_check(dsc_idx, pp_idx);
		if (ret)
			return -ENAVAIL;

		global_state->dsc_to_crtc_id[dsc_idx] = crtc_id;
		num_dsc++;
		pp_idx++;
	}

	if (num_dsc < top->num_dsc) {
		DPU_ERROR("DSC allocation failed num_dsc=%d required=%d\n",
			   num_dsc, top->num_dsc);
		return -ENAVAIL;
	}

	return 0;
}

static int _dpu_rm_dsc_alloc_pair(struct dpu_rm *rm,
				  struct dpu_global_state *global_state,
				  uint32_t crtc_id,
				  const struct msm_display_topology *top)
{
	int num_dsc = 0;
	int dsc_idx, pp_idx = 0;
	int ret;

	/* only start from even dsc index */
	for (dsc_idx = 0; dsc_idx < ARRAY_SIZE(rm->dsc_blks) &&
	     num_dsc < top->num_dsc; dsc_idx += 2) {
		if (!rm->dsc_blks[dsc_idx] ||
		    !rm->dsc_blks[dsc_idx + 1])
			continue;

		/* consective dsc index to be paired */
		if (reserved_by_other(global_state->dsc_to_crtc_id, dsc_idx, crtc_id) ||
		    reserved_by_other(global_state->dsc_to_crtc_id, dsc_idx + 1, crtc_id))
			continue;

		pp_idx = _dpu_rm_pingpong_next_index(global_state, pp_idx, crtc_id);
		if (pp_idx < 0)
			return -ENAVAIL;

		ret = _dpu_rm_pingpong_dsc_check(dsc_idx, pp_idx);
		if (ret) {
			pp_idx = 0;
			continue;
		}

		pp_idx = _dpu_rm_pingpong_next_index(global_state, pp_idx + 1, crtc_id);
		if (pp_idx < 0)
			return -ENAVAIL;

		ret = _dpu_rm_pingpong_dsc_check(dsc_idx + 1, pp_idx);
		if (ret) {
			pp_idx = 0;
			continue;
		}

		global_state->dsc_to_crtc_id[dsc_idx] = crtc_id;
		global_state->dsc_to_crtc_id[dsc_idx + 1] = crtc_id;
		num_dsc += 2;
		pp_idx++;	/* start for next pair */
	}

	if (num_dsc < top->num_dsc) {
		DPU_ERROR("DSC allocation failed num_dsc=%d required=%d\n",
			   num_dsc, top->num_dsc);
		return -ENAVAIL;
	}

	return 0;
}

static int _dpu_rm_reserve_dsc(struct dpu_rm *rm,
			       struct dpu_global_state *global_state,
			       uint32_t crtc_id,
			       const struct msm_display_topology *top)
{
	if (!top->num_dsc || !top->num_intf)
		return 0;

	/*
	 * Facts:
	 * 1) no pingpong split (two layer mixers shared one pingpong)
	 * 2) DSC pair starts from even index, such as index(0,1), (2,3), etc
	 * 3) even PINGPONG connects to even DSC
	 * 4) odd PINGPONG connects to odd DSC
	 * 5) pair: encoder +--> pp_idx_0 --> dsc_idx_0
	 *                  +--> pp_idx_1 --> dsc_idx_1
	 */

	/* num_dsc should be either 1, 2 or 4 */
	if (top->num_dsc > top->num_intf)	/* merge mode */
		return _dpu_rm_dsc_alloc_pair(rm, global_state, crtc_id, top);
	else
		return _dpu_rm_dsc_alloc(rm, global_state, crtc_id, top);

	return 0;
}

static int _dpu_rm_reserve_cdm(struct dpu_rm *rm,
			       struct dpu_global_state *global_state,
			       uint32_t crtc_id,
			       int num_cdm)
{
	/* try allocating only one CDM block */
	if (!rm->cdm_blk) {
		DPU_ERROR("CDM block does not exist\n");
		return -EIO;
	}

	if (num_cdm > 1) {
		DPU_ERROR("More than 1 INTF requesting CDM\n");
		return -EINVAL;
	}

	if (global_state->cdm_to_crtc_id) {
		DPU_ERROR("CDM_0 is already allocated\n");
		return -EIO;
	}

	global_state->cdm_to_crtc_id = crtc_id;

	return 0;
}

static int _dpu_rm_make_reservation(
		struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		uint32_t crtc_id,
		struct msm_display_topology *topology)
{
	int ret;

	ret = _dpu_rm_reserve_lms(rm, global_state, crtc_id, topology);
	if (ret) {
		DPU_ERROR("unable to find appropriate mixers\n");
		return ret;
	}

	if (topology->cwb_enabled) {
		ret = _dpu_rm_reserve_cwb_mux_and_pingpongs(rm, global_state,
							    crtc_id, topology);
		if (ret)
			return ret;
	}

	ret = _dpu_rm_reserve_ctls(rm, global_state, crtc_id,
			topology);
	if (ret) {
		DPU_ERROR("unable to find appropriate CTL\n");
		return ret;
	}

	ret  = _dpu_rm_reserve_dsc(rm, global_state, crtc_id, topology);
	if (ret)
		return ret;

	if (topology->num_cdm > 0) {
		ret = _dpu_rm_reserve_cdm(rm, global_state, crtc_id, topology->num_cdm);
		if (ret) {
			DPU_ERROR("unable to find CDM blk\n");
			return ret;
		}
	}

	return ret;
}

static void _dpu_rm_clear_mapping(uint32_t *res_mapping, int cnt,
				  uint32_t crtc_id)
{
	int i;

	for (i = 0; i < cnt; i++) {
		if (res_mapping[i] == crtc_id)
			res_mapping[i] = 0;
	}
}

/**
 * dpu_rm_release - Given the encoder for the display chain, release any
 *	HW blocks previously reserved for that use case.
 * @global_state: resources shared across multiple kms objects
 * @crtc: DRM CRTC handle
 * @return: 0 on Success otherwise -ERROR
 */
void dpu_rm_release(struct dpu_global_state *global_state,
		    struct drm_crtc *crtc)
{
	uint32_t crtc_id = crtc->base.id;

	_dpu_rm_clear_mapping(global_state->pingpong_to_crtc_id,
			ARRAY_SIZE(global_state->pingpong_to_crtc_id), crtc_id);
	_dpu_rm_clear_mapping(global_state->mixer_to_crtc_id,
			ARRAY_SIZE(global_state->mixer_to_crtc_id), crtc_id);
	_dpu_rm_clear_mapping(global_state->ctl_to_crtc_id,
			ARRAY_SIZE(global_state->ctl_to_crtc_id), crtc_id);
	_dpu_rm_clear_mapping(global_state->dsc_to_crtc_id,
			ARRAY_SIZE(global_state->dsc_to_crtc_id), crtc_id);
	_dpu_rm_clear_mapping(global_state->dspp_to_crtc_id,
			ARRAY_SIZE(global_state->dspp_to_crtc_id), crtc_id);
	_dpu_rm_clear_mapping(&global_state->cdm_to_crtc_id, 1, crtc_id);
	_dpu_rm_clear_mapping(global_state->cwb_to_crtc_id,
			ARRAY_SIZE(global_state->cwb_to_crtc_id), crtc_id);
}

/**
 * dpu_rm_reserve - Given a CRTC->Encoder->Connector display chain, analyze
 *	the use connections and user requirements, specified through related
 *	topology control properties, and reserve hardware blocks to that
 *	display chain.
 *	HW blocks can then be accessed through dpu_rm_get_* functions.
 *	HW Reservations should be released via dpu_rm_release_hw.
 * @rm: DPU Resource Manager handle
 * @global_state: resources shared across multiple kms objects
 * @crtc: DRM CRTC handle
 * @topology: Pointer to topology info for the display
 * @return: 0 on Success otherwise -ERROR
 */
int dpu_rm_reserve(
		struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		struct drm_crtc *crtc,
		struct msm_display_topology *topology)
{
	int ret;

	if (IS_ERR(global_state)) {
		DPU_ERROR("failed to global state\n");
		return PTR_ERR(global_state);
	}

	DRM_DEBUG_KMS("reserving hw for crtc %d\n", crtc->base.id);

	DRM_DEBUG_KMS("num_lm: %d num_dsc: %d num_intf: %d\n",
		      topology->num_lm, topology->num_dsc,
		      topology->num_intf);

	ret = _dpu_rm_make_reservation(rm, global_state, crtc->base.id, topology);
	if (ret)
		DPU_ERROR("failed to reserve hw resources: %d\n", ret);

	return ret;
}

static struct dpu_hw_sspp *dpu_rm_try_sspp(struct dpu_rm *rm,
					   struct dpu_global_state *global_state,
					   struct drm_crtc *crtc,
					   struct dpu_rm_sspp_requirements *reqs,
					   unsigned int type)
{
	uint32_t crtc_id = crtc->base.id;
	struct dpu_hw_sspp *hw_sspp;
	int i;

	for (i = 0; i < ARRAY_SIZE(rm->hw_sspp); i++) {
		if (!rm->hw_sspp[i])
			continue;

		if (global_state->sspp_to_crtc_id[i])
			continue;

		hw_sspp = rm->hw_sspp[i];

		if (hw_sspp->cap->type != type)
			continue;

		if (reqs->scale && !hw_sspp->cap->sblk->scaler_blk.len)
			continue;

		// TODO: QSEED2 and RGB scalers are not yet supported
		if (reqs->scale && !hw_sspp->ops.setup_scaler)
			continue;

		if (reqs->yuv && !hw_sspp->cap->sblk->csc_blk.len)
			continue;

		if (reqs->rot90 && !(hw_sspp->cap->features & DPU_SSPP_INLINE_ROTATION))
			continue;

		global_state->sspp_to_crtc_id[i] = crtc_id;

		return rm->hw_sspp[i];
	}

	return NULL;
}

/**
 * dpu_rm_reserve_sspp - Reserve the required SSPP for the provided CRTC
 * @rm: DPU Resource Manager handle
 * @global_state: private global state
 * @crtc: DRM CRTC handle
 * @reqs: SSPP required features
 */
struct dpu_hw_sspp *dpu_rm_reserve_sspp(struct dpu_rm *rm,
					struct dpu_global_state *global_state,
					struct drm_crtc *crtc,
					struct dpu_rm_sspp_requirements *reqs)
{
	struct dpu_hw_sspp *hw_sspp = NULL;

	if (!reqs->scale && !reqs->yuv)
		hw_sspp = dpu_rm_try_sspp(rm, global_state, crtc, reqs, SSPP_TYPE_DMA);
	if (!hw_sspp && reqs->scale)
		hw_sspp = dpu_rm_try_sspp(rm, global_state, crtc, reqs, SSPP_TYPE_RGB);
	if (!hw_sspp)
		hw_sspp = dpu_rm_try_sspp(rm, global_state, crtc, reqs, SSPP_TYPE_VIG);

	return hw_sspp;
}

/**
 * dpu_rm_release_all_sspp - Given the CRTC, release all SSPP
 *	blocks previously reserved for that use case.
 * @global_state: resources shared across multiple kms objects
 * @crtc: DRM CRTC handle
 */
void dpu_rm_release_all_sspp(struct dpu_global_state *global_state,
			     struct drm_crtc *crtc)
{
	uint32_t crtc_id = crtc->base.id;

	_dpu_rm_clear_mapping(global_state->sspp_to_crtc_id,
		ARRAY_SIZE(global_state->sspp_to_crtc_id), crtc_id);
}

/**
 * dpu_rm_get_assigned_resources - Get hw resources of the given type that are
 *     assigned to this encoder
 * @rm: DPU Resource Manager handle
 * @global_state: resources shared across multiple kms objects
 * @crtc: DRM CRTC handle
 * @type: resource type to return data for
 * @blks: pointer to the array to be filled by HW resources
 * @blks_size: size of the @blks array
 */
int dpu_rm_get_assigned_resources(struct dpu_rm *rm,
	struct dpu_global_state *global_state, struct drm_crtc *crtc,
	enum dpu_hw_blk_type type, struct dpu_hw_blk **blks, int blks_size)
{
	uint32_t crtc_id = crtc->base.id;
	struct dpu_hw_blk **hw_blks;
	uint32_t *hw_to_crtc_id;
	int i, num_blks, max_blks;

	switch (type) {
	case DPU_HW_BLK_PINGPONG:
	case DPU_HW_BLK_DCWB_PINGPONG:
		hw_blks = rm->pingpong_blks;
		hw_to_crtc_id = global_state->pingpong_to_crtc_id;
		max_blks = ARRAY_SIZE(rm->pingpong_blks);
		break;
	case DPU_HW_BLK_LM:
		hw_blks = rm->mixer_blks;
		hw_to_crtc_id = global_state->mixer_to_crtc_id;
		max_blks = ARRAY_SIZE(rm->mixer_blks);
		break;
	case DPU_HW_BLK_CTL:
		hw_blks = rm->ctl_blks;
		hw_to_crtc_id = global_state->ctl_to_crtc_id;
		max_blks = ARRAY_SIZE(rm->ctl_blks);
		break;
	case DPU_HW_BLK_DSPP:
		hw_blks = rm->dspp_blks;
		hw_to_crtc_id = global_state->dspp_to_crtc_id;
		max_blks = ARRAY_SIZE(rm->dspp_blks);
		break;
	case DPU_HW_BLK_DSC:
		hw_blks = rm->dsc_blks;
		hw_to_crtc_id = global_state->dsc_to_crtc_id;
		max_blks = ARRAY_SIZE(rm->dsc_blks);
		break;
	case DPU_HW_BLK_CDM:
		hw_blks = &rm->cdm_blk;
		hw_to_crtc_id = &global_state->cdm_to_crtc_id;
		max_blks = 1;
		break;
	case DPU_HW_BLK_CWB:
		hw_blks = rm->cwb_blks;
		hw_to_crtc_id = global_state->cwb_to_crtc_id;
		max_blks = ARRAY_SIZE(rm->cwb_blks);
		break;
	default:
		DPU_ERROR("blk type %d not managed by rm\n", type);
		return 0;
	}

	num_blks = 0;
	for (i = 0; i < max_blks; i++) {
		if (hw_to_crtc_id[i] != crtc_id)
			continue;

		if (type == DPU_HW_BLK_PINGPONG) {
			struct dpu_hw_pingpong *pp = to_dpu_hw_pingpong(hw_blks[i]);

			if (pp->idx >= PINGPONG_CWB_0)
				continue;
		}

		if (type == DPU_HW_BLK_DCWB_PINGPONG) {
			struct dpu_hw_pingpong *pp = to_dpu_hw_pingpong(hw_blks[i]);

			if (pp->idx < PINGPONG_CWB_0)
				continue;
		}

		if (num_blks == blks_size) {
			DPU_ERROR("More than %d resources assigned to crtc %d\n",
				  blks_size, crtc_id);
			break;
		}
		if (!hw_blks[i]) {
			DPU_ERROR("Allocated resource %d unavailable to assign to crtc %d\n",
				  type, crtc_id);
			break;
		}
		blks[num_blks++] = hw_blks[i];
	}

	return num_blks;
}

static void dpu_rm_print_state_helper(struct drm_printer *p,
					    struct dpu_hw_blk *blk,
					    uint32_t mapping)
{
	if (!blk)
		drm_puts(p, "- ");
	else if (!mapping)
		drm_puts(p, "# ");
	else
		drm_printf(p, "%d ", mapping);
}


/**
 * dpu_rm_print_state - output the RM private state
 * @p: DRM printer
 * @global_state: global state
 */
void dpu_rm_print_state(struct drm_printer *p,
			const struct dpu_global_state *global_state)
{
	const struct dpu_rm *rm = global_state->rm;
	int i;

	drm_puts(p, "resource mapping:\n");
	drm_puts(p, "\tpingpong=");
	for (i = 0; i < ARRAY_SIZE(global_state->pingpong_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->pingpong_blks[i],
					  global_state->pingpong_to_crtc_id[i]);
	drm_puts(p, "\n");

	drm_puts(p, "\tmixer=");
	for (i = 0; i < ARRAY_SIZE(global_state->mixer_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->mixer_blks[i],
					  global_state->mixer_to_crtc_id[i]);
	drm_puts(p, "\n");

	drm_puts(p, "\tctl=");
	for (i = 0; i < ARRAY_SIZE(global_state->ctl_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->ctl_blks[i],
					  global_state->ctl_to_crtc_id[i]);
	drm_puts(p, "\n");

	drm_puts(p, "\tdspp=");
	for (i = 0; i < ARRAY_SIZE(global_state->dspp_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->dspp_blks[i],
					  global_state->dspp_to_crtc_id[i]);
	drm_puts(p, "\n");

	drm_puts(p, "\tdsc=");
	for (i = 0; i < ARRAY_SIZE(global_state->dsc_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->dsc_blks[i],
					  global_state->dsc_to_crtc_id[i]);
	drm_puts(p, "\n");

	drm_puts(p, "\tcdm=");
	dpu_rm_print_state_helper(p, rm->cdm_blk,
				  global_state->cdm_to_crtc_id);
	drm_puts(p, "\n");

	drm_puts(p, "\tsspp=");
	/* skip SSPP_NONE and start from the next index */
	for (i = SSPP_NONE + 1; i < ARRAY_SIZE(global_state->sspp_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->hw_sspp[i] ? &rm->hw_sspp[i]->base : NULL,
					  global_state->sspp_to_crtc_id[i]);
	drm_puts(p, "\n");

	drm_puts(p, "\tcwb=");
	for (i = 0; i < ARRAY_SIZE(global_state->cwb_to_crtc_id); i++)
		dpu_rm_print_state_helper(p, rm->cwb_blks[i],
					  global_state->cwb_to_crtc_id[i]);
	drm_puts(p, "\n");
}
