/*
 * SH7785 Setup
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
#include <asm/sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffea0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
	}, {
		.mapbase	= 0xffeb0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 44, 45, 47, 46 },
	},

	/*
	 * The rest of these all have multiplexed IRQs
	 */
	{
		.mapbase	= 0xffec0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 60, 60, 60, 60 },
	}, {
		.mapbase	= 0xffed0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 61, 61, 61, 61 },
	}, {
		.mapbase	= 0xffee0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 62, 62, 62, 62 },
	}, {
		.mapbase	= 0xffef0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 63, 63, 63, 63 },
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

static struct platform_device *sh7785_devices[] __initdata = {
	&sci_device,
};

static int __init sh7785_devices_setup(void)
{
	return platform_add_devices(sh7785_devices,
				    ARRAY_SIZE(sh7785_devices));
}
__initcall(sh7785_devices_setup);

static struct intc2_data intc2_irq_table[] = {
	{ 28, 0, 24, 0, 0, 2 },		/* TMU0 */

	{ 40, 8, 24, 0, 2, 3 },		/* SCIF0 ERI */
	{ 41, 8, 24, 0, 2, 3 },		/* SCIF0 RXI */
	{ 42, 8, 24, 0, 2, 3 },		/* SCIF0 BRI */
	{ 43, 8, 24, 0, 2, 3 },		/* SCIF0 TXI */

	{ 44, 8, 16, 0, 3, 3 },		/* SCIF1 ERI */
	{ 45, 8, 16, 0, 3, 3 },		/* SCIF1 RXI */
	{ 46, 8, 16, 0, 3, 3 },		/* SCIF1 BRI */
	{ 47, 8, 16, 0, 3, 3 },		/* SCIF1 TXI */

	{ 64, 0x14,  8, 0, 14, 2 },	/* PCIC0 */
	{ 65, 0x14,  0, 0, 15, 2 },	/* PCIC1 */
	{ 66, 0x18, 24, 0, 16, 2 },	/* PCIC2 */
	{ 67, 0x18, 16, 0, 17, 2 },	/* PCIC3 */
	{ 68, 0x18,  8, 0, 18, 2 },	/* PCIC4 */

	{ 60,  8,  8, 0, 4, 3 },	/* SCIF2 ERI, RXI, BRI, TXI */
	{ 60,  8,  0, 0, 5, 3 },	/* SCIF3 ERI, RXI, BRI, TXI */
	{ 60, 12, 24, 0, 6, 3 },	/* SCIF4 ERI, RXI, BRI, TXI */
	{ 60, 12, 16, 0, 7, 3 },	/* SCIF5 ERI, RXI, BRI, TXI */
};

static struct intc2_desc intc2_irq_desc __read_mostly = {
	.prio_base	= 0xffd40000,
	.msk_base	= 0xffd40038,
	.mskclr_base	= 0xffd4003c,

	.intc2_data	= intc2_irq_table,
	.nr_irqs	= ARRAY_SIZE(intc2_irq_table),

	.chip = {
		.name	= "INTC2-sh7785",
	},
};

void __init plat_irq_setup(void)
{
	register_intc2_controller(&intc2_irq_desc);
}

