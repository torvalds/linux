/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
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
};

struct dpu_vbif_set_memtype_params {
	u32 xin_id;
	u32 vbif_idx;
	bool is_cacheable;
};

/**
 * struct dpu_vbif_set_qos_params - QoS remapper parameter
 * @vbif_idx: vbif identifier
 * @xin_id: client interface identifier
 * @num: pipe identifier (debug only)
 * @is_rt: true if pipe is used in real-time use case
 */
struct dpu_vbif_set_qos_params {
	u32 vbif_idx;
	u32 xin_id;
	u32 num;
	bool is_rt;
};

void dpu_vbif_set_ot_limit(struct dpu_kms *dpu_kms,
		struct dpu_vbif_set_ot_params *params);

void dpu_vbif_set_qos_remap(struct dpu_kms *dpu_kms,
		struct dpu_vbif_set_qos_params *params);

void dpu_vbif_clear_errors(struct dpu_kms *dpu_kms);

void dpu_vbif_init_memtypes(struct dpu_kms *dpu_kms);

void dpu_debugfs_vbif_init(struct dpu_kms *dpu_kms, struct dentry *debugfs_root);

#endif /* __DPU_VBIF_H__ */
