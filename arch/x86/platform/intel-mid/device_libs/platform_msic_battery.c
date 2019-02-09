/*
 * platform_msic_battery.c: MSIC battery platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/mfd/intel_msic.h>
#include <asm/intel-mid.h>

#include "platform_msic.h"

static void __init *msic_battery_platform_data(void *info)
{
	return msic_generic_platform_data(info, INTEL_MSIC_BLOCK_BATTERY);
}

static const struct devs_id msic_battery_dev_id __initconst = {
	.name = "msic_battery",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.msic = 1,
	.get_platform_data = &msic_battery_platform_data,
};

sfi_device(msic_battery_dev_id);
