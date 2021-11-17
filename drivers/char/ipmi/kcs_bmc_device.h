/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, IBM Corp. */

#ifndef __KCS_BMC_DEVICE_H__
#define __KCS_BMC_DEVICE_H__

#include <linux/irqreturn.h>

#include "kcs_bmc.h"

struct kcs_bmc_device_ops {
	void (*irq_mask_update)(struct kcs_bmc_device *kcs_bmc, u8 mask, u8 enable);
	u8 (*io_inputb)(struct kcs_bmc_device *kcs_bmc, u32 reg);
	void (*io_outputb)(struct kcs_bmc_device *kcs_bmc, u32 reg, u8 b);
	void (*io_updateb)(struct kcs_bmc_device *kcs_bmc, u32 reg, u8 mask, u8 b);
};

irqreturn_t kcs_bmc_handle_event(struct kcs_bmc_device *kcs_bmc);
int kcs_bmc_add_device(struct kcs_bmc_device *kcs_bmc);
void kcs_bmc_remove_device(struct kcs_bmc_device *kcs_bmc);

#endif
