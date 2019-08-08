// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform_msic_power_btn.c: MSIC power btn platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/sfi.h>
#include <linux/init.h>
#include <linux/mfd/intel_msic.h>
#include <asm/intel-mid.h>

#include "platform_msic.h"

static void __init *msic_power_btn_platform_data(void *info)
{
	return msic_generic_platform_data(info, INTEL_MSIC_BLOCK_POWER_BTN);
}

static const struct devs_id msic_power_btn_dev_id __initconst = {
	.name = "msic_power_btn",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.msic = 1,
	.get_platform_data = &msic_power_btn_platform_data,
};

sfi_device(msic_power_btn_dev_id);
