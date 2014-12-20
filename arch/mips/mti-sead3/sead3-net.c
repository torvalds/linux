/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/platform_device.h>
#include <linux/smsc911x.h>

#include <asm/mips-boards/sead3int.h>

static struct smsc911x_platform_config sead3_smsc911x_data = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type = SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags	= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface = PHY_INTERFACE_MODE_MII,
};

struct resource sead3_net_resources[] = {
	{
		.start			= 0x1f010000,
		.end			= 0x1f01ffff,
		.flags			= IORESOURCE_MEM
	},
	{
		.flags			= IORESOURCE_IRQ
	}
};

static struct platform_device sead3_net_device = {
	.name			= "smsc911x",
	.id			= 0,
	.dev			= {
		.platform_data	= &sead3_smsc911x_data,
	},
	.num_resources		= ARRAY_SIZE(sead3_net_resources),
	.resource		= sead3_net_resources
};

static int __init sead3_net_init(void)
{
	if (gic_present)
		sead3_net_resources[1].start = MIPS_GIC_IRQ_BASE + GIC_INT_NET;
	else
		sead3_net_resources[1].start = MIPS_CPU_IRQ_BASE + CPU_INT_NET;
	return platform_device_register(&sead3_net_device);
}

module_init(sead3_net_init);

MODULE_AUTHOR("Chris Dearman <chris@mips.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network probe driver for SEAD-3");
