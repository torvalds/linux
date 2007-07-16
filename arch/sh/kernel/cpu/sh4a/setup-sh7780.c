/*
 * SH7780 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <asm/sci.h>

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xffe80000,
		.end	= 0xffe80000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Period IRQ */
		.start	= 21,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= 22,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= 23,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
	}, {
		.mapbase	= 0xffe10000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 76, 77, 79, 78 },
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

static struct platform_device *sh7780_devices[] __initdata = {
	&rtc_device,
	&sci_device,
};

static int __init sh7780_devices_setup(void)
{
	return platform_add_devices(sh7780_devices,
				    ARRAY_SIZE(sh7780_devices));
}
__initcall(sh7780_devices_setup);

static struct intc2_data intc2_irq_table[] = {
	{ 28, 0, 24, 0, 0, 2 },		/* TMU0 */

	{ 21, 1,  0, 0, 2, 2 },
	{ 22, 1,  1, 0, 2, 2 },
	{ 23, 1,  2, 0, 2, 2 },

	{ 40, 8, 24, 0, 3, 3 },		/* SCIF0 ERI */
	{ 41, 8, 24, 0, 3, 3 },		/* SCIF0 RXI */
	{ 42, 8, 24, 0, 3, 3 },		/* SCIF0 BRI */
	{ 43, 8, 24, 0, 3, 3 },		/* SCIF0 TXI */

	{ 76, 8, 16, 0, 4, 3 },		/* SCIF1 ERI */
	{ 77, 8, 16, 0, 4, 3 },		/* SCIF1 RXI */
	{ 78, 8, 16, 0, 4, 3 },		/* SCIF1 BRI */
	{ 79, 8, 16, 0, 4, 3 },		/* SCIF1 TXI */

	{ 64, 0x10,  8, 0, 14, 2 },	/* PCIC0 */
	{ 65, 0x10,  0, 0, 15, 2 },	/* PCIC1 */
	{ 66, 0x14, 24, 0, 16, 2 },	/* PCIC2 */
	{ 67, 0x14, 16, 0, 17, 2 },	/* PCIC3 */
	{ 68, 0x14,  8, 0, 18, 2 },	/* PCIC4 */
};

static struct intc2_desc intc2_irq_desc __read_mostly = {
	.prio_base	= 0xffd40000,
	.msk_base	= 0xffd40038,
	.mskclr_base	= 0xffd4003c,

	.intc2_data	= intc2_irq_table,
	.nr_irqs	= ARRAY_SIZE(intc2_irq_table),

	.chip = {
		.name	= "INTC2-sh7780",
	},
};

void __init init_IRQ_intc2(void)
{
	register_intc2_controller(&intc2_irq_desc);
}
