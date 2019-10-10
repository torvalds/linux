// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform_msic_audio.c: MSIC audio platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/platform_device.h>
#include <linux/mfd/intel_msic.h>
#include <asm/intel-mid.h>

#include "platform_msic.h"

static void *msic_audio_platform_data(void *info)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple("sst-platform", -1, NULL, 0);

	if (IS_ERR(pdev)) {
		pr_err("failed to create audio platform device\n");
		return NULL;
	}

	return msic_generic_platform_data(info, INTEL_MSIC_BLOCK_AUDIO);
}

static const struct devs_id msic_audio_dev_id __initconst = {
	.name = "msic_audio",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.msic = 1,
	.get_platform_data = &msic_audio_platform_data,
};

sfi_device(msic_audio_dev_id);
