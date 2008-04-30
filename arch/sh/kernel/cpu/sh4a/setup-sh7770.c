/*
 * SH7770 Setup
 *
 *  Copyright (C) 2006 - 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xff923000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 61, 61, 61, 61 },
	}, {
		.mapbase	= 0xff924000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 62, 62, 62, 62 },
	}, {
		.mapbase	= 0xff925000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 63, 63, 63, 63 },
	}, {
		.mapbase	= 0xff926000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 64, 64, 64, 64 },
	}, {
		.mapbase	= 0xff927000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 65, 65, 65, 65 },
	}, {
		.mapbase	= 0xff928000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 66, 66, 66, 66 },
	}, {
		.mapbase	= 0xff929000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 67, 67, 67, 67 },
	}, {
		.mapbase	= 0xff92a000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 68, 68, 68, 68 },
	}, {
		.mapbase	= 0xff92b000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 69, 69, 69, 69 },
	}, {
		.mapbase	= 0xff92c000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 70, 70, 70, 70 },
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

static struct platform_device *sh7770_devices[] __initdata = {
	&sci_device,
};

static int __init sh7770_devices_setup(void)
{
	return platform_add_devices(sh7770_devices,
				    ARRAY_SIZE(sh7770_devices));
}
__initcall(sh7770_devices_setup);

void __init plat_irq_setup(void)
{
}
