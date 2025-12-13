/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Linaro Ltd.
 */
#ifndef __VENUS_FIRMWARE_H__
#define __VENUS_FIRMWARE_H__

struct device;

int venus_firmware_init(struct venus_core *core);
void venus_firmware_deinit(struct venus_core *core);
int venus_firmware_check(struct venus_core *core);
int venus_firmware_cfg(struct venus_core *core);
int venus_boot(struct venus_core *core);
int venus_shutdown(struct venus_core *core);
int venus_set_hw_state(struct venus_core *core, bool suspend);

static inline int venus_set_hw_state_suspend(struct venus_core *core)
{
	return venus_set_hw_state(core, false);
}

static inline int venus_set_hw_state_resume(struct venus_core *core)
{
	return venus_set_hw_state(core, true);
}

#endif
