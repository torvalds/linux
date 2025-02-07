// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen2.h"

struct iris_inst *iris_hfi_gen2_get_instance(void)
{
	return kzalloc(sizeof(struct iris_inst_hfi_gen2), GFP_KERNEL);
}
