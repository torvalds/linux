/*
 * arch/sh/boards/renesas/x3proto/setup.c
 *
 * Renesas SH-X3 Prototype Board Support.
 *
 * Copyright (C) 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/io.h>

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= 0xb8140020,
		.end	= 0xb8140020 + 8 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *x3proto_devices[] __initdata = {
	&heartbeat_device,
};

static int __init x3proto_devices_setup(void)
{
	return platform_add_devices(x3proto_devices,
				    ARRAY_SIZE(x3proto_devices));
}
device_initcall(x3proto_devices_setup);

static void __init x3proto_init_irq(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRL3210);

	/* Set ICR0.LVLMODE */
	ctrl_outl(ctrl_inl(0xfe410000) | (1 << 21), 0xfe410000);
}

static struct sh_machine_vector mv_x3proto __initmv = {
	.mv_name		= "x3proto",
	.mv_init_irq		= x3proto_init_irq,
};
