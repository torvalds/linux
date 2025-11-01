/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VPU_COMMON_H__
#define __IRIS_VPU_COMMON_H__

struct iris_core;

extern const struct vpu_ops iris_vpu2_ops;
extern const struct vpu_ops iris_vpu3_ops;
extern const struct vpu_ops iris_vpu33_ops;
extern const struct vpu_ops iris_vpu35_ops;

struct vpu_ops {
	void (*power_off_hw)(struct iris_core *core);
	int (*power_on_hw)(struct iris_core *core);
	int (*power_off_controller)(struct iris_core *core);
	int (*power_on_controller)(struct iris_core *core);
	void (*program_bootup_registers)(struct iris_core *core);
	u64 (*calc_freq)(struct iris_inst *inst, size_t data_size);
};

int iris_vpu_boot_firmware(struct iris_core *core);
void iris_vpu_raise_interrupt(struct iris_core *core);
void iris_vpu_clear_interrupt(struct iris_core *core);
int iris_vpu_watchdog(struct iris_core *core, u32 intr_status);
int iris_vpu_prepare_pc(struct iris_core *core);
int iris_vpu_power_on_controller(struct iris_core *core);
int iris_vpu_power_on_hw(struct iris_core *core);
int iris_vpu_power_on(struct iris_core *core);
int iris_vpu_power_off_controller(struct iris_core *core);
void iris_vpu_power_off_hw(struct iris_core *core);
void iris_vpu_power_off(struct iris_core *core);

#endif
