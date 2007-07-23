/*
 * SH7710 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *  Copyright (C) 2007  Nobuhiro Iwamatsu
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
		.mapbase	= 0xa4400000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 53, 55, 54 },
	}, {
		.mapbase	= 0xa4420000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs           = { 56, 57, 59, 58 },
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

static struct platform_device *sh7710_devices[] __initdata = {
	&sci_device,
};

static int __init sh7710_devices_setup(void)
{
	return platform_add_devices(sh7710_devices,
				    ARRAY_SIZE(sh7710_devices));
}
__initcall(sh7710_devices_setup);

static struct ipr_data ipr_irq_table[] = {
	/* IRQ, IPR-idx, shift, priority */
	{ 16, 0, 12, 2 }, /* TMU0 TUNI*/
	{ 17, 0,  8, 2 }, /* TMU1 TUNI */
	{ 18, 0,  4, 2 }, /* TMU2 TUNI */
	{ 27, 1, 12, 2 }, /* WDT ITI */
	{ 20, 0,  0, 2 }, /* RTC ATI (alarm) */
	{ 21, 0,  0, 2 }, /* RTC PRI (period) */
	{ 22, 0,  0, 2 }, /* RTC CUI (carry) */
	{ 48, 4, 12, 7 }, /* DMAC DMTE0 */
	{ 49, 4, 12, 7 }, /* DMAC DMTE1 */
	{ 50, 4, 12, 7 }, /* DMAC DMTE2 */
	{ 51, 4, 12, 7 }, /* DMAC DMTE3 */
	{ 52, 4,  8, 3 }, /* SCIF0 ERI */
	{ 53, 4,  8, 3 }, /* SCIF0 RXI */
	{ 54, 4,  8, 3 }, /* SCIF0 BRI */
	{ 55, 4,  8, 3 }, /* SCIF0 TXI */
	{ 56, 4,  4, 3 }, /* SCIF1 ERI */
	{ 57, 4,  4, 3 }, /* SCIF1 RXI */
	{ 58, 4,  4, 3 }, /* SCIF1 BRI */
	{ 59, 4,  4, 3 }, /* SCIF1 TXI */
	{ 76, 5,  8, 7 }, /* DMAC DMTE4 */
	{ 77, 5,  8, 7 }, /* DMAC DMTE5 */
	{ 80, 6, 12, 5 }, /* EDMAC EINT0 */
	{ 81, 6,  8, 5 }, /* EDMAC EINT1 */
	{ 82, 6,  4, 5 }, /* EDMAC EINT2 */
};

static unsigned long ipr_offsets[] = {
	0xA414FEE2,	/* 0: IPRA */
	0xA414FEE4,	/* 1: IPRB */
	0xA4140016,	/* 2: IPRC */
	0xA4140018,	/* 3: IPRD */
	0xA414001A,	/* 4: IPRE */
	0xA4080000,	/* 5: IPRF */
	0xA4080002,	/* 6: IPRG */
	0xA4080004,	/* 7: IPRH */
	0xA4080006,	/* 8: IPRI */
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),

	.chip = {
		.name	= "IPR-sh7710",
	},
};

void __init plat_irq_setup(void)
{
	register_ipr_controller(&ipr_irq_desc);
}
