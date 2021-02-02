// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/debugfs.h>
#include <linux/delay.h>

#include "dpu_vbif.h"
#include "dpu_hw_vbif.h"
#include "dpu_trace.h"

/**
 * _dpu_vbif_wait_for_xin_halt - wait for the xin to halt
 * @vbif:	Pointer to hardware vbif driver
 * @xin_id:	Client interface identifier
 * @return:	0 if success; error code otherwise
 */
static int _dpu_vbif_wait_for_xin_halt(struct dpu_hw_vbif *vbif, u32 xin_id)
{
	ktime_t timeout;
	bool status;
	int rc;

	if (!vbif || !vbif->cap || !vbif->ops.get_halt_ctrl) {
		DPU_ERROR("invalid arguments vbif %d\n", vbif != NULL);
		return -EINVAL;
	}

	timeout = ktime_add_us(ktime_get(), vbif->cap->xin_halt_timeout);
	for (;;) {
		status = vbif->ops.get_halt_ctrl(vbif, xin_id);
		if (status)
			break;
		if (ktime_compare_safe(ktime_get(), timeout) > 0) {
			status = vbif->ops.get_halt_ctrl(vbif, xin_id);
			break;
		}
		usleep_range(501, 1000);
	}

	if (!status) {
		rc = -ETIMEDOUT;
		DPU_ERROR("VBIF %d client %d not halting. TIMEDOUT.\n",
				vbif->idx - VBIF_0, xin_id);
	} else {
		rc = 0;
		DPU_DEBUG("VBIF %d client %d is halted\n",
				vbif->idx - VBIF_0, xin_id);
	}

	return rc;
}

/**
 * _dpu_vbif_apply_dynamic_ot_limit - determine OT based on usecase parameters
 * @vbif:	Pointer to hardware vbif driver
 * @ot_lim:	Pointer to OT limit to be modified
 * @params:	Pointer to usecase parameters
 */
static void _dpu_vbif_apply_dynamic_ot_limit(struct dpu_hw_vbif *vbif,
		u32 *ot_lim, struct dpu_vbif_set_ot_params *params)
{
	u64 pps;
	const struct dpu_vbif_dynamic_ot_tbl *tbl;
	u32 i;

	if (!vbif || !(vbif->cap->features & BIT(DPU_VBIF_QOS_OTLIM)))
		return;

	/* Dynamic OT setting done only for WFD */
	if (!params->is_wfd)
		return;

	pps = params->frame_rate;
	pps *= params->width;
	pps *= params->height;

	tbl = params->rd ? &vbif->cap->dynamic_ot_rd_tbl :
			&vbif->cap->dynamic_ot_wr_tbl;

	for (i = 0; i < tbl->count; i++) {
		if (pps <= tbl->cfg[i].pps) {
			*ot_lim = tbl->cfg[i].ot_limit;
			break;
		}
	}

	DPU_DEBUG("vbif:%d xin:%d w:%d h:%d fps:%d pps:%llu ot:%u\n",
			vbif->idx - VBIF_0, params->xin_id,
			params->width, params->height, params->frame_rate,
			pps, *ot_lim);
}

/**
 * _dpu_vbif_get_ot_limit - get OT based on usecase & configuration parameters
 * @vbif:	Pointer to hardware vbif driver
 * @params:	Pointer to usecase parameters
 * @return:	OT limit
 */
static u32 _dpu_vbif_get_ot_limit(struct dpu_hw_vbif *vbif,
	struct dpu_vbif_set_ot_params *params)
{
	u32 ot_lim = 0;
	u32 val;

	if (!vbif || !vbif->cap) {
		DPU_ERROR("invalid arguments vbif %d\n", vbif != NULL);
		return -EINVAL;
	}

	if (vbif->cap->default_ot_wr_limit && !params->rd)
		ot_lim = vbif->cap->default_ot_wr_limit;
	else if (vbif->cap->default_ot_rd_limit && params->rd)
		ot_lim = vbif->cap->default_ot_rd_limit;

	/*
	 * If default ot is not set from dt/catalog,
	 * then do not configure it.
	 */
	if (ot_lim == 0)
		goto exit;

	/* Modify the limits if the target and the use case requires it */
	_dpu_vbif_apply_dynamic_ot_limit(vbif, &ot_lim, params);

	if (vbif && vbif->ops.get_limit_conf) {
		val = vbif->ops.get_limit_conf(vbif,
				params->xin_id, params->rd);
		if (val == ot_lim)
			ot_lim = 0;
	}

exit:
	DPU_DEBUG("vbif:%d xin:%d ot_lim:%d\n",
			vbif->idx - VBIF_0, params->xin_id, ot_lim);
	return ot_lim;
}

/**
 * dpu_vbif_set_ot_limit - set OT based on usecase & configuration parameters
 * @dpu_kms:	DPU handler
 * @params:	Pointer to usecase parameters
 *
 * Note this function would block waiting for bus halt.
 */
void dpu_vbif_set_ot_limit(struct dpu_kms *dpu_kms,
		struct dpu_vbif_set_ot_params *params)
{
	struct dpu_hw_vbif *vbif = NULL;
	struct dpu_hw_mdp *mdp;
	bool forced_on = false;
	u32 ot_lim;
	int ret, i;

	mdp = dpu_kms->hw_mdp;

	for (i = 0; i < ARRAY_SIZE(dpu_kms->hw_vbif); i++) {
		if (dpu_kms->hw_vbif[i] &&
				dpu_kms->hw_vbif[i]->idx == params->vbif_idx)
			vbif = dpu_kms->hw_vbif[i];
	}

	if (!vbif || !mdp) {
		DPU_DEBUG("invalid arguments vbif %d mdp %d\n",
				vbif != NULL, mdp != NULL);
		return;
	}

	if (!mdp->ops.setup_clk_force_ctrl ||
			!vbif->ops.set_limit_conf ||
			!vbif->ops.set_halt_ctrl)
		return;

	/* set write_gather_en for all write clients */
	if (vbif->ops.set_write_gather_en && !params->rd)
		vbif->ops.set_write_gather_en(vbif, params->xin_id);

	ot_lim = _dpu_vbif_get_ot_limit(vbif, params) & 0xFF;

	if (ot_lim == 0)
		return;

	trace_dpu_perf_set_ot(params->num, params->xin_id, ot_lim,
		params->vbif_idx);

	forced_on = mdp->ops.setup_clk_force_ctrl(mdp, params->clk_ctrl, true);

	vbif->ops.set_limit_conf(vbif, params->xin_id, params->rd, ot_lim);

	vbif->ops.set_halt_ctrl(vbif, params->xin_id, true);

	ret = _dpu_vbif_wait_for_xin_halt(vbif, params->xin_id);
	if (ret)
		trace_dpu_vbif_wait_xin_halt_fail(vbif->idx, params->xin_id);

	vbif->ops.set_halt_ctrl(vbif, params->xin_id, false);

	if (forced_on)
		mdp->ops.setup_clk_force_ctrl(mdp, params->clk_ctrl, false);
}

void dpu_vbif_set_qos_remap(struct dpu_kms *dpu_kms,
		struct dpu_vbif_set_qos_params *params)
{
	struct dpu_hw_vbif *vbif = NULL;
	struct dpu_hw_mdp *mdp;
	bool forced_on = false;
	const struct dpu_vbif_qos_tbl *qos_tbl;
	int i;

	if (!params || !dpu_kms->hw_mdp) {
		DPU_ERROR("invalid arguments\n");
		return;
	}
	mdp = dpu_kms->hw_mdp;

	for (i = 0; i < ARRAY_SIZE(dpu_kms->hw_vbif); i++) {
		if (dpu_kms->hw_vbif[i] &&
				dpu_kms->hw_vbif[i]->idx == params->vbif_idx) {
			vbif = dpu_kms->hw_vbif[i];
			break;
		}
	}

	if (!vbif || !vbif->cap) {
		DPU_ERROR("invalid vbif %d\n", params->vbif_idx);
		return;
	}

	if (!vbif->ops.set_qos_remap || !mdp->ops.setup_clk_force_ctrl) {
		DPU_DEBUG("qos remap not supported\n");
		return;
	}

	qos_tbl = params->is_rt ? &vbif->cap->qos_rt_tbl :
			&vbif->cap->qos_nrt_tbl;

	if (!qos_tbl->npriority_lvl || !qos_tbl->priority_lvl) {
		DPU_DEBUG("qos tbl not defined\n");
		return;
	}

	forced_on = mdp->ops.setup_clk_force_ctrl(mdp, params->clk_ctrl, true);

	for (i = 0; i < qos_tbl->npriority_lvl; i++) {
		DPU_DEBUG("vbif:%d xin:%d lvl:%d/%d\n",
				params->vbif_idx, params->xin_id, i,
				qos_tbl->priority_lvl[i]);
		vbif->ops.set_qos_remap(vbif, params->xin_id, i,
				qos_tbl->priority_lvl[i]);
	}

	if (forced_on)
		mdp->ops.setup_clk_force_ctrl(mdp, params->clk_ctrl, false);
}

void dpu_vbif_clear_errors(struct dpu_kms *dpu_kms)
{
	struct dpu_hw_vbif *vbif;
	u32 i, pnd, src;

	for (i = 0; i < ARRAY_SIZE(dpu_kms->hw_vbif); i++) {
		vbif = dpu_kms->hw_vbif[i];
		if (vbif && vbif->ops.clear_errors) {
			vbif->ops.clear_errors(vbif, &pnd, &src);
			if (pnd || src) {
				DRM_DEBUG_KMS("VBIF %d: pnd 0x%X, src 0x%X\n",
					      vbif->idx - VBIF_0, pnd, src);
			}
		}
	}
}

void dpu_vbif_init_memtypes(struct dpu_kms *dpu_kms)
{
	struct dpu_hw_vbif *vbif;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(dpu_kms->hw_vbif); i++) {
		vbif = dpu_kms->hw_vbif[i];
		if (vbif && vbif->cap && vbif->ops.set_mem_type) {
			for (j = 0; j < vbif->cap->memtype_count; j++)
				vbif->ops.set_mem_type(
						vbif, j, vbif->cap->memtype[j]);
		}
	}
}

#ifdef CONFIG_DEBUG_FS

void dpu_debugfs_vbif_init(struct dpu_kms *dpu_kms, struct dentry *debugfs_root)
{
	char vbif_name[32];
	struct dentry *entry, *debugfs_vbif;
	int i, j;

	entry = debugfs_create_dir("vbif", debugfs_root);

	for (i = 0; i < dpu_kms->catalog->vbif_count; i++) {
		const struct dpu_vbif_cfg *vbif = &dpu_kms->catalog->vbif[i];

		snprintf(vbif_name, sizeof(vbif_name), "%d", vbif->id);

		debugfs_vbif = debugfs_create_dir(vbif_name, entry);

		debugfs_create_u32("features", 0600, debugfs_vbif,
			(u32 *)&vbif->features);

		debugfs_create_u32("xin_halt_timeout", 0400, debugfs_vbif,
			(u32 *)&vbif->xin_halt_timeout);

		debugfs_create_u32("default_rd_ot_limit", 0400, debugfs_vbif,
			(u32 *)&vbif->default_ot_rd_limit);

		debugfs_create_u32("default_wr_ot_limit", 0400, debugfs_vbif,
			(u32 *)&vbif->default_ot_wr_limit);

		for (j = 0; j < vbif->dynamic_ot_rd_tbl.count; j++) {
			const struct dpu_vbif_dynamic_ot_cfg *cfg =
					&vbif->dynamic_ot_rd_tbl.cfg[j];

			snprintf(vbif_name, sizeof(vbif_name),
					"dynamic_ot_rd_%d_pps", j);
			debugfs_create_u64(vbif_name, 0400, debugfs_vbif,
					(u64 *)&cfg->pps);
			snprintf(vbif_name, sizeof(vbif_name),
					"dynamic_ot_rd_%d_ot_limit", j);
			debugfs_create_u32(vbif_name, 0400, debugfs_vbif,
					(u32 *)&cfg->ot_limit);
		}

		for (j = 0; j < vbif->dynamic_ot_wr_tbl.count; j++) {
			const struct dpu_vbif_dynamic_ot_cfg *cfg =
					&vbif->dynamic_ot_wr_tbl.cfg[j];

			snprintf(vbif_name, sizeof(vbif_name),
					"dynamic_ot_wr_%d_pps", j);
			debugfs_create_u64(vbif_name, 0400, debugfs_vbif,
					(u64 *)&cfg->pps);
			snprintf(vbif_name, sizeof(vbif_name),
					"dynamic_ot_wr_%d_ot_limit", j);
			debugfs_create_u32(vbif_name, 0400, debugfs_vbif,
					(u32 *)&cfg->ot_limit);
		}
	}
}
#endif
