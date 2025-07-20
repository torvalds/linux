/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_FIRMWARE_H__
#define __IRIS_FIRMWARE_H__

struct iris_core;

int iris_fw_load(struct iris_core *core);
int iris_fw_unload(struct iris_core *core);
int iris_set_hw_state(struct iris_core *core, bool resume);

#endif
