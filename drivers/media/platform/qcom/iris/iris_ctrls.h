/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_CTRLS_H__
#define __IRIS_CTRLS_H__

#include "iris_platform_common.h"

struct iris_core;
struct iris_inst;

int iris_ctrls_init(struct iris_inst *inst);
void iris_session_init_caps(struct iris_core *core);
int iris_set_u32_enum(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_stage(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_pipe(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_u32(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_profile(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_level(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_profile_level_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_header_mode_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_header_mode_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_bitrate_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_bitrate_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_peak_bitrate(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_bitrate_mode_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_bitrate_mode_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_entropy_mode_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_entropy_mode_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_min_qp(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_max_qp(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_frame_qp(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_qp_range(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_rotation(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_flip(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_ir_period_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_ir_period_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_ltr_count_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_ltr_count_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_use_ltr(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_mark_ltr(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_use_and_mark_ltr(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_intra_period(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_layer_type(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_layer_count_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_layer_count_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_layer_bitrate(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id);
int iris_set_properties(struct iris_inst *inst, u32 plane);

#endif
