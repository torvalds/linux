// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_instance.h"
#include "iris_vpu_buffer.h"

u32 iris_vpu_dec_dpb_size(struct iris_inst *inst)
{
	if (iris_split_mode_enabled(inst))
		return iris_get_buffer_size(inst, BUF_DPB);
	else
		return 0;
}

static inline int iris_vpu_dpb_count(struct iris_inst *inst)
{
	if (iris_split_mode_enabled(inst)) {
		return inst->fw_min_count ?
			inst->fw_min_count : inst->buffers[BUF_OUTPUT].min_count;
	}

	return 0;
}

int iris_vpu_buf_count(struct iris_inst *inst, enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return MIN_BUFFERS;
	case BUF_OUTPUT:
		return inst->fw_min_count;
	case BUF_DPB:
		return iris_vpu_dpb_count(inst);
	default:
		return 0;
	}
}
