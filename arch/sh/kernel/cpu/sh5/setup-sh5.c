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
#include <asm/addrspace.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= PHYS_PERIPHERAL_BLOCK + 0x01030000,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.type		= PORT_SCIF,
		.irqs		= { 39, 40, 42, 0 },
	}, {
		.flags = 0,
	}
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= -1,
	.dev		= {
		.platform_data	= sci_platform_data,
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

static struct platform_device *sh5_devices[] __initdata = {
	&sci_device,
	&rtc_device,
};

static int __init sh5_devices_setup(void)
{
	return platform_add_devices(sh5_devices,
				    ARRAY_SIZE(sh5_devices));
}
__initcall(sh5_devices_setup);
