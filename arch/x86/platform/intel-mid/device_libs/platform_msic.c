/*
 * platform_msic.c: MSIC platform data initilization file
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
#include <asm/intel_scu_ipc.h>
#include <asm/intel-mid.h>
#include "platform_msic.h"

struct intel_msic_platform_data msic_pdata;

static struct resource msic_resources[] = {
	{
		.start	= INTEL_MSIC_IRQ_PHYS_BASE,
		.end	= INTEL_MSIC_IRQ_PHYS_BASE + 64 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device msic_device = {
	.name		= "intel_msic",
	.id		= -1,
	.dev		= {
		.platform_data	= &msic_pdata,
	},
	.num_resources	= ARRAY_SIZE(msic_resources),
	.resource	= msic_resources,
};

static int msic_scu_status_change(struct notifier_block *nb,
				  unsigned long code, void *data)
{
	if (code == SCU_DOWN) {
		platform_device_unregister(&msic_device);
		return 0;
	}

	return platform_device_register(&msic_device);
}

static int __init msic_init(void)
{
	static struct notifier_block msic_scu_notifier = {
		.notifier_call	= msic_scu_status_change,
	};

	/*
	 * We need to be sure that the SCU IPC is ready before MSIC device
	 * can be registered.
	 */
	if (intel_mid_has_msic())
		intel_scu_notifier_add(&msic_scu_notifier);

	return 0;
}
arch_initcall(msic_init);

/*
 * msic_generic_platform_data - sets generic platform data for the block
 * @info: pointer to the SFI device table entry for this block
 * @block: MSIC block
 *
 * Function sets IRQ number from the SFI table entry for given device to
 * the MSIC platform data.
 */
void *msic_generic_platform_data(void *info, enum intel_msic_block block)
{
	struct sfi_device_table_entry *entry = info;

	BUG_ON(block < 0 || block >= INTEL_MSIC_BLOCK_LAST);
	msic_pdata.irq[block] = entry->irq;

	return NULL;
}
