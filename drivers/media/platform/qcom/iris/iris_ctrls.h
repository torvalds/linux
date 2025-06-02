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
int iris_set_properties(struct iris_inst *inst, u32 plane);

#endif
