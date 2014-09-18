/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

struct resource ehci_resources[] = {
	{
		.start			= 0x1b200000,
		.end			= 0x1b200fff,
		.flags			= IORESOURCE_MEM
	},
	{
		.start			= MIPS_CPU_IRQ_BASE + 2,
		.flags			= IORESOURCE_IRQ
	}
};

u64 sead3_usbdev_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ehci_device = {
	.name		= "sead3-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &sead3_usbdev_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32)
	},
	.num_resources	= ARRAY_SIZE(ehci_resources),
	.resource	= ehci_resources
};

static int __init ehci_init(void)
{
	return platform_device_register(&ehci_device);
}

module_init(ehci_init);

MODULE_AUTHOR("Chris Dearman <chris@mips.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EHCI probe driver for SEAD3");
