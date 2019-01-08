/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DPU_VBIF_H__
#define __DPU_VBIF_H__

#include "dpu_kms.h"

struct dpu_vbif_set_ot_params {
	u32 xin_id;
	u32 num;
	u32 width;
	u32 height;
	u32 frame_rate;
	bool rd;
	bool is_wfd;
	u32 vbif_idx;
	u32 clk_ctrl;
};

struct dpu_vbif_set_memtype_params {
	u32 xin_id;
	u32 vbif_idx;
	u32 clk_ctrl;
	bool is_cacheable;
};

/**
 * struct dpu_vbif_set_qos_params - QoS remapper parameter
 * @vbif_idx: vbif identifier
 * @xin_id: client interface identifier
 * @clk_ctrl: clock control identifier of the xin
 * @num: pipe identifier (debug only)
 * @is_rt: true if pipe is used in real-time use case
 */
struct dpu_vbif_set_qos_params {
	u32 vbif_idx;
	u32 xin_id;
	u32 clk_ctrl;
	u32 num;
	bool is_rt;
};

/**
 * dpu_vbif_set_ot_limit - set OT limit for vbif client
 * @dpu_kms:	DPU handler
 * @params:	Pointer to OT configuration parameters
 */
void dpu_vbif_set_ot_limit(struct dpu_kms *dpu_kms,
		struct dpu_vbif_set_ot_params *params);

/**
 * dpu_vbif_set_qos_remap - set QoS priority level remap
 * @dpu_kms:	DPU handler
 * @params:	Pointer to QoS configuration parameters
 */
void dpu_vbif_set_qos_remap(struct dpu_kms *dpu_kms,
		struct dpu_vbif_set_qos_params *params);

/**
 * dpu_vbif_clear_errors - clear any vbif errors
 * @dpu_kms:	DPU handler
 */
void dpu_vbif_clear_errors(struct dpu_kms *dpu_kms);

/**
 * dpu_vbif_init_memtypes - initialize xin memory types for vbif
 * @dpu_kms:	DPU handler
 */
void dpu_vbif_init_memtypes(struct dpu_kms *dpu_kms);

void dpu_debugfs_vbif_init(struct dpu_kms *dpu_kms, struct dentry *debugfs_root);

#endif /* __DPU_VBIF_H__ */
