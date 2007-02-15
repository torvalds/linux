/*
 * SH7760 Setup
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

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xfe600000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 53, 55, 54 },
	}, {
		.mapbase	= 0xfe610000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 72, 73, 75, 74 },
	}, {
		.mapbase	= 0xfe620000,
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

static struct platform_device *sh7760_devices[] __initdata = {
	&sci_device,
};

static int __init sh7760_devices_setup(void)
{
	return platform_add_devices(sh7760_devices,
				    ARRAY_SIZE(sh7760_devices));
}
__initcall(sh7760_devices_setup);

static struct intc2_data intc2_irq_table[] = {
	{48,  0, 28, 0, 31,  3},	/* IRQ 4 */
	{49,  0, 24, 0, 30,  3},	/* IRQ 3 */
	{50,  0, 20, 0, 29,  3},	/* IRQ 2 */
	{51,  0, 16, 0, 28,  3},	/* IRQ 1 */
	{56,  4, 28, 0, 25,  3},	/* HCAN2_CHAN0 */
	{57,  4, 24, 0, 24,  3},	/* HCAN2_CHAN1 */
	{58,  4, 20, 0, 23,  3},	/* I2S_CHAN0   */
	{59,  4, 16, 0, 22,  3},	/* I2S_CHAN1   */
	{60,  4, 12, 0, 21,  3},	/* AC97_CHAN0  */
	{61,  4,  8, 0, 20,  3},	/* AC97_CHAN1  */
	{62,  4,  4, 0, 19,  3},	/* I2C_CHAN0   */
	{63,  4,  0, 0, 18,  3},	/* I2C_CHAN1   */
	{52,  8, 16, 0, 11,  3},	/* SCIF0_ERI_IRQ */
	{53,  8, 16, 0, 10,  3},	/* SCIF0_RXI_IRQ */
	{54,  8, 16, 0,  9,  3},	/* SCIF0_BRI_IRQ */
	{55,  8, 16, 0,  8,  3},	/* SCIF0_TXI_IRQ */
	{64,  8, 28, 0, 17,  3},	/* USBHI_IRQ */
	{65,  8, 24, 0, 16,  3},	/* LCDC      */
	{68,  8, 20, 0, 14, 13},	/* DMABRGI0_IRQ */
	{69,  8, 20, 0, 13, 13},	/* DMABRGI1_IRQ */
	{70,  8, 20, 0, 12, 13},	/* DMABRGI2_IRQ */
	{72,  8, 12, 0,  7,  3},	/* SCIF1_ERI_IRQ */
	{73,  8, 12, 0,  6,  3},	/* SCIF1_RXI_IRQ */
	{74,  8, 12, 0,  5,  3},	/* SCIF1_BRI_IRQ */
	{75,  8, 12, 0,  4,  3},	/* SCIF1_TXI_IRQ */
	{76,  8,  8, 0,  3,  3},	/* SCIF2_ERI_IRQ */
	{77,  8,  8, 0,  2,  3},	/* SCIF2_RXI_IRQ */
	{78,  8,  8, 0,  1,  3},	/* SCIF2_BRI_IRQ */
	{79,  8,  8, 0,  0,  3},	/* SCIF2_TXI_IRQ */
	{80,  8,  4, 4, 23,  3},	/* SIM_ERI */
	{81,  8,  4, 4, 22,  3},	/* SIM_RXI */
	{82,  8,  4, 4, 21,  3},	/* SIM_TXI */
	{83,  8,  4, 4, 20,  3},	/* SIM_TEI */
	{84,  8,  0, 4, 19,  3},	/* HSPII */
	{88, 12, 20, 4, 18,  3},	/* MMCI0 */
	{89, 12, 20, 4, 17,  3},	/* MMCI1 */
	{90, 12, 20, 4, 16,  3},	/* MMCI2 */
	{91, 12, 20, 4, 15,  3},	/* MMCI3 */
	{92, 12, 12, 4,  6,  3},	/* MFI */
	{108,12,  4, 4,  1,  3},	/* ADC  */
	{109,12,  0, 4,  0,  3},	/* CMTI */
};

static struct ipr_data sh7760_ipr_map[] = {
	/* IRQ, IPR-idx, shift, priority */
	{ 16, 0, 12, 2 }, /* TMU0 TUNI*/
	{ 17, 0,  8, 2 }, /* TMU1 TUNI */
	{ 18, 0,  4, 2 }, /* TMU2 TUNI */
	{ 19, 0,  4, 2 }, /* TMU2 TIPCI */
	{ 27, 1, 12, 2 }, /* WDT ITI */
	{ 28, 1,  8, 2 }, /* REF RCMI */
	{ 29, 1,  8, 2 }, /* REF ROVI */
	{ 32, 2,  0, 7 }, /* HUDI */
	{ 33, 2, 12, 7 }, /* GPIOI */
	{ 34, 2,  8, 7 }, /* DMAC DMTE0 */
	{ 35, 2,  8, 7 }, /* DMAC DMTE1 */
	{ 36, 2,  8, 7 }, /* DMAC DMTE2 */
	{ 37, 2,  8, 7 }, /* DMAC DMTE3 */
	{ 38, 2,  8, 7 }, /* DMAC DMAE */
	{ 44, 2,  8, 7 }, /* DMAC DMTE4 */
	{ 45, 2,  8, 7 }, /* DMAC DMTE5 */
	{ 46, 2,  8, 7 }, /* DMAC DMTE6 */
	{ 47, 2,  8, 7 }, /* DMAC DMTE7 */
/* these here are only valid if INTC_ICR bit 7 is set to 1!
 * XXX: maybe CONFIG_SH_IRLMODE symbol? SH7751 could use it too */
#if 0
	{  2, 3, 12, 3 }, /* IRL0 */
	{  5, 3,  8, 3 }, /* IRL1 */
	{  8, 3,  4, 3 }, /* IRL2 */
	{ 11, 3,  0, 3 }, /* IRL3 */
#endif
};

static unsigned long ipr_offsets[] = {
	0xffd00004UL,	/* 0: IPRA */
	0xffd00008UL,	/* 1: IPRB */
	0xffd0000cUL,	/* 2: IPRC */
	0xffd00010UL,	/* 3: IPRD */
};

/* given the IPR index return the address of the IPR register */
unsigned int map_ipridx_to_addr(int idx)
{
	if (idx >= ARRAY_SIZE(ipr_offsets))
		return 0;
	return ipr_offsets[idx];
}

void __init init_IRQ_intc2(void)
{
	make_intc2_irq(intc2_irq_table, ARRAY_SIZE(intc2_irq_table));
}

void __init  init_IRQ_ipr(void)
{
	make_ipr_irq(sh7760_ipr_map, ARRAY_SIZE(sh7760_ipr_map));
}
