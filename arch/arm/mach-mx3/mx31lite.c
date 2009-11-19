/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <mach/board-mx31lite.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/irqs.h>
#include <mach/mxc_nand.h>
#include "devices.h"

/*
 * This file contains the module-specific initialization routines.
 */

static unsigned int mx31lite_pins[] = {
	/* LAN9117 IRQ pin */
	IOMUX_MODE(MX31_PIN_SFS6, IOMUX_CONFIG_GPIO),
};

static struct mxc_nand_platform_data mx31lite_nand_board_info = {
	.width = 1,
	.hw_ecc = 1,
};

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_16BIT,
};

static struct resource smsc911x_resources[] = {
	{
		.start		= CS4_BASE_ADDR,
		.end		= CS4_BASE_ADDR + 0x100,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IOMUX_TO_IRQ(MX31_PIN_SFS6),
		.end		= IOMUX_TO_IRQ(MX31_PIN_SFS6),
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &smsc911x_config,
	},
};

/*
 * This structure defines the MX31 memory map.
 */
static struct map_desc mx31lite_io_desc[] __initdata = {
	{
		.virtual = SPBA0_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(SPBA0_BASE_ADDR),
		.length = SPBA0_SIZE,
		.type = MT_DEVICE_NONSHARED
	}, {
		.virtual = CS4_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(CS4_BASE_ADDR),
		.length = CS4_SIZE,
		.type = MT_DEVICE
	}
};

/*
 * Set up static virtual mappings.
 */
void __init mx31lite_map_io(void)
{
	mx31_map_io();
	iotable_init(mx31lite_io_desc, ARRAY_SIZE(mx31lite_io_desc));
}

static int mx31lite_baseboard;
core_param(mx31lite_baseboard, mx31lite_baseboard, int, 0444);

static void __init mxc_board_init(void)
{
	int ret;

	switch (mx31lite_baseboard) {
	case MX31LITE_NOBOARD:
		break;
	case MX31LITE_DB:
		mx31lite_db_init();
		break;
	default:
		printk(KERN_ERR "Illegal mx31lite_baseboard type %d\n",
				mx31lite_baseboard);
	}

	mxc_iomux_setup_multiple_pins(mx31lite_pins, ARRAY_SIZE(mx31lite_pins),
				      "mx31lite");

	mxc_register_device(&mxc_nand_device, &mx31lite_nand_board_info);

	/* SMSC9117 IRQ pin */
	ret = gpio_request(IOMUX_TO_GPIO(MX31_PIN_SFS6), "sms9117-irq");
	if (ret)
		pr_warning("could not get LAN irq gpio\n");
	else {
		gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_SFS6));
		platform_device_register(&smsc911x_device);
	}
}

static void __init mx31lite_timer_init(void)
{
	mx31_clocks_init(26000000);
}

struct sys_timer mx31lite_timer = {
	.init	= mx31lite_timer_init,
};

MACHINE_START(MX31LITE, "LogicPD i.MX31 SOM")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.phys_io        = AIPS1_BASE_ADDR,
	.io_pg_offst    = ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx31lite_map_io,
	.init_irq       = mx31_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx31lite_timer,
MACHINE_END
