/*
 * SH-X3 Setup
 *
 *  Copyright (C) 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <asm/sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffc30000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
	}, {
		.mapbase	= 0xffc40000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 44, 45, 47, 46 },
	}, {
		.mapbase	= 0xffc50000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 48, 49, 51, 50 },
	}, {
		.mapbase	= 0xffc60000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 53, 55, 54 },
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

static struct platform_device *shx3_devices[] __initdata = {
	&sci_device,
};

static int __init shx3_devices_setup(void)
{
	return platform_add_devices(shx3_devices,
				    ARRAY_SIZE(shx3_devices));
}
__initcall(shx3_devices_setup);

static struct intc2_data intc2_irq_table[] = {
	{ 16, 0, 0, 0, 1, 2 },		/* TMU0 */
	{ 40, 4, 0, 0x20, 0, 3 },	/* SCIF0 ERI */
	{ 41, 4, 0, 0x20, 1, 3 },	/* SCIF0 RXI */
	{ 42, 4, 0, 0x20, 2, 3 },	/* SCIF0 BRI */
	{ 43, 4, 0, 0x20, 3, 3 },	/* SCIF0 TXI */
};

static struct intc2_desc intc2_irq_desc __read_mostly = {
	.prio_base	= 0xfe410000,
	.msk_base	= 0xfe410820,
	.mskclr_base	= 0xfe410850,

	.intc2_data	= intc2_irq_table,
	.nr_irqs	= ARRAY_SIZE(intc2_irq_table),

	.chip = {
		.name	= "INTC2-SHX3",
	},
};

void __init plat_irq_setup(void)
{
	register_intc2_controller(&intc2_irq_desc);
}
