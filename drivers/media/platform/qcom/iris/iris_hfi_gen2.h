/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_GEN2_H__
#define __IRIS_HFI_GEN2_H__

#include "iris_instance.h"

struct iris_core;

#define to_iris_inst_hfi_gen2(ptr) \
	container_of(ptr, struct iris_inst_hfi_gen2, inst)

/**
 * struct iris_inst_hfi_gen2 - holds per video instance parameters for hfi_gen2
 *
 * @inst: pointer to iris_instance structure
 * @packet: HFI packet
 * @src_subcr_params: subscription params to fw on input port
 */
struct iris_inst_hfi_gen2 {
	struct iris_inst		inst;
	struct iris_hfi_header		*packet;
	struct hfi_subscription_params	src_subcr_params;
};

void iris_hfi_gen2_command_ops_init(struct iris_core *core);
void iris_hfi_gen2_response_ops_init(struct iris_core *core);
struct iris_inst *iris_hfi_gen2_get_instance(void);

#endif
