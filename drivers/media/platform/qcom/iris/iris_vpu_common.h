/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VPU_COMMON_H__
#define __IRIS_VPU_COMMON_H__

struct iris_core;

int iris_vpu_boot_firmware(struct iris_core *core);
void iris_vpu_raise_interrupt(struct iris_core *core);
void iris_vpu_clear_interrupt(struct iris_core *core);
int iris_vpu_watchdog(struct iris_core *core, u32 intr_status);

#endif
