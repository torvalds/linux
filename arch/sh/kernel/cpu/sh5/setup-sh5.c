/*
 * SH5-101/SH5-103 CPU Setup
 *
 *  Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/sh_timer.h>
#include <asm/addrspace.h>

static struct plat_sci_port scif0_platform_data = {
	.flags		= UPF_IOREMAP,
	.scscr		= SCSCR_REIE,
	.type		= PORT_SCIF,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(PHYS_PERIPHERAL_BLOCK + 0x01030000, 0x100),
	DEFINE_RES_IRQ(39),
	DEFINE_RES_IRQ(40),
	DEFINE_RES_IRQ(42),
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.resource	= scif0_resources,
	.num_resources	= ARRAY_SIZE(scif0_resources),
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct resource rtc_resources[] = {
	[0] = {
		.start	= PHYS_PERIPHERAL_BLOCK + 0x01040000,
		.end	= PHYS_PERIPHERAL_BLOCK + 0x01040000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Period IRQ */
		.start	= IRQ_PRI,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= IRQ_CUI,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= IRQ_ATI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

#define	TMU_BLOCK_OFF	0x01020000
#define TMU_BASE	PHYS_PERIPHERAL_BLOCK + TMU_BLOCK_OFF

static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(TMU_BASE, 0x30),
	DEFINE_RES_IRQ(IRQ_TUNI0),
	DEFINE_RES_IRQ(IRQ_TUNI1),
	DEFINE_RES_IRQ(IRQ_TUNI2),
};

static struct platform_device tmu0_device = {
	.name		= "sh-tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

static struct platform_device *sh5_early_devices[] __initdata = {
	&scif0_device,
	&tmu0_device,
};

static struct platform_device *sh5_devices[] __initdata = {
	&rtc_device,
};

static int __init sh5_devices_setup(void)
{
	int ret;

	ret = platform_add_devices(sh5_early_devices,
				   ARRAY_SIZE(sh5_early_devices));
	if (unlikely(ret != 0))
		return ret;

	return platform_add_devices(sh5_devices,
				    ARRAY_SIZE(sh5_devices));
}
arch_initcall(sh5_devices_setup);

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh5_early_devices,
				   ARRAY_SIZE(sh5_early_devices));
}
