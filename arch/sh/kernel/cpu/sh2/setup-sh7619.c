/*
 * SH7619 Setup
 *
 *  Copyright (C) 2006  Yoshinori Sato
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <asm/sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xf8400000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 88, 89, 91, 90},
	}, {
		.mapbase	= 0xf8410000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 92, 93, 95, 94},
	}, {
		.mapbase	= 0xf8420000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 96, 97, 99, 98},
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

static struct platform_device *sh7619_devices[] __initdata = {
	&sci_device,
};

static int __init sh7619_devices_setup(void)
{
	return platform_add_devices(sh7619_devices,
				    ARRAY_SIZE(sh7619_devices));
}
__initcall(sh7619_devices_setup);
