/*
 * platform_ipc.c: IPC platform library file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/sfi.h>
#include <linux/gpio.h>
#include <asm/intel-mid.h>
#include "platform_ipc.h"

void __init ipc_device_handler(struct sfi_device_table_entry *pentry,
				struct devs_id *dev)
{
	struct platform_device *pdev;
	void *pdata = NULL;
	static struct resource res __initdata = {
		.name = "IRQ",
		.flags = IORESOURCE_IRQ,
	};

	pr_debug("IPC bus, name = %16.16s, irq = 0x%2x\n",
		pentry->name, pentry->irq);

	/*
	 * We need to call platform init of IPC devices to fill misc_pdata
	 * structure. It will be used in msic_init for initialization.
	 */
	if (dev != NULL)
		pdata = dev->get_platform_data(pentry);

	/*
	 * On Medfield the platform device creation is handled by the MSIC
	 * MFD driver so we don't need to do it here.
	 */
	if (intel_mid_has_msic())
		return;

	pdev = platform_device_alloc(pentry->name, 0);
	if (pdev == NULL) {
		pr_err("out of memory for SFI platform device '%s'.\n",
			pentry->name);
		return;
	}
	res.start = pentry->irq;
	platform_device_add_resources(pdev, &res, 1);

	pdev->dev.platform_data = pdata;
	intel_scu_device_register(pdev);
}

static const struct devs_id pmic_audio_dev_id __initconst = {
	.name = "pmic_audio",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.device_handler = &ipc_device_handler,
};

sfi_device(pmic_audio_dev_id);
