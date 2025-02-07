/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_GEN2_H__
#define __IRIS_HFI_GEN2_H__

#include "iris_instance.h"

struct iris_core;

/**
 * struct iris_inst_hfi_gen2 - holds per video instance parameters for hfi_gen2
 *
 * @inst: pointer to iris_instance structure
 */
struct iris_inst_hfi_gen2 {
	struct iris_inst		inst;
};

void iris_hfi_gen2_command_ops_init(struct iris_core *core);
void iris_hfi_gen2_response_ops_init(struct iris_core *core);
struct iris_inst *iris_hfi_gen2_get_instance(void);

#endif
