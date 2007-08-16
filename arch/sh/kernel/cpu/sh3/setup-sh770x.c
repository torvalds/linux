/*
 * SH3 Setup code for SH7706, SH7707, SH7708, SH7709
 *
 *  Copyright (C) 2007  Magnus Damm
 *
 * Based on setup-sh7709.c
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <asm/sci.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,
	PINT07, PINT815,
	DMAC_DEI0, DMAC_DEI1, DMAC_DEI2, DMAC_DEI3,
	SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI,
	SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI,
	SCI_ERI, SCI_RXI, SCI_TXI, SCI_TEI,
	ADC_ADI,
	LCDC, PCC0, PCC1,
	TMU0, TMU1, TMU2_TUNI, TMU2_TICPI,
	RTC_ATI, RTC_PRI, RTC_CUI,
	WDT,
	REF_RCMI, REF_ROVI,

	/* interrupt groups */
	RTC, REF, TMU2, DMAC, SCI, SCIF2, SCIF0,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2_TUNI, 0x440), INTC_VECT(TMU2_TICPI, 0x460),
	INTC_VECT(RTC_ATI, 0x480), INTC_VECT(RTC_PRI, 0x4a0),
	INTC_VECT(RTC_CUI, 0x4c0),
	INTC_VECT(SCI_ERI, 0x4e0), INTC_VECT(SCI_RXI, 0x500),
	INTC_VECT(SCI_TXI, 0x520), INTC_VECT(SCI_TEI, 0x540),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF_RCMI, 0x580),
	INTC_VECT(REF_ROVI, 0x5a0),
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	INTC_VECT(IRQ4, 0x680), INTC_VECT(IRQ5, 0x6a0),
	INTC_VECT(DMAC_DEI0, 0x800), INTC_VECT(DMAC_DEI1, 0x820),
	INTC_VECT(DMAC_DEI2, 0x840), INTC_VECT(DMAC_DEI3, 0x860),
	INTC_VECT(ADC_ADI, 0x980),
	INTC_VECT(SCIF2_ERI, 0x900), INTC_VECT(SCIF2_RXI, 0x920),
	INTC_VECT(SCIF2_BRI, 0x940), INTC_VECT(SCIF2_TXI, 0x960),
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	INTC_VECT(PINT07, 0x700), INTC_VECT(PINT815, 0x720),
	INTC_VECT(SCIF0_ERI, 0x880), INTC_VECT(SCIF0_RXI, 0x8a0),
	INTC_VECT(SCIF0_BRI, 0x8c0), INTC_VECT(SCIF0_TXI, 0x8e0),
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
	INTC_VECT(LCDC, 0x9a0),
	INTC_VECT(PCC0, 0x9c0), INTC_VECT(PCC1, 0x9e0),
#endif
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(TMU2, TMU2_TUNI, TMU2_TICPI),
	INTC_GROUP(REF, REF_RCMI, REF_ROVI),
	INTC_GROUP(DMAC, DMAC_DEI0, DMAC_DEI1, DMAC_DEI2, DMAC_DEI3),
	INTC_GROUP(SCI, SCI_ERI, SCI_RXI, SCI_TXI, SCI_TEI),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI),
	INTC_GROUP(SCIF2, SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI),
};

static struct intc_prio priorities[] __initdata = {
	INTC_PRIO(DMAC, 7),
	INTC_PRIO(SCI, 3),
	INTC_PRIO(SCIF2, 3),
	INTC_PRIO(SCIF0, 3),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffffee2, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, RTC } },
	{ 0xfffffee4, 0, 16, 4, /* IPRB */ { WDT, REF, SCI, 0 } },
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	{ 0xa4000016, 0, 16, 4, /* IPRC */ { IRQ3, IRQ2, IRQ1, IRQ0 } },
	{ 0xa4000018, 0, 16, 4, /* IPRD */ { 0, 0, IRQ5, IRQ4 } },
	{ 0xa400001a, 0, 16, 4, /* IPRE */ { DMAC, 0, SCIF2, ADC_ADI } },
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	{ 0xa4000018, 0, 16, 4, /* IPRD */ { PINT07, PINT815, } },
	{ 0xa400001a, 0, 16, 4, /* IPRE */ { 0, SCIF0 } },
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
	{ 0xa400001c, 0, 16, 4, /* IPRF */ { 0, LCDC, PCC0, PCC1, } },
#endif
};

static DECLARE_INTC_DESC(intc_desc, "sh770x", vectors, groups,
			 priorities, NULL, prio_registers, NULL);

#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
static struct intc_vect vectors_irq[] __initdata = {
	INTC_VECT(IRQ0, 0x600), INTC_VECT(IRQ1, 0x620),
	INTC_VECT(IRQ2, 0x640), INTC_VECT(IRQ3, 0x660),
};

static DECLARE_INTC_DESC(intc_desc_irq, "sh770x-irq", vectors_irq, NULL,
			 priorities, NULL, prio_registers, NULL);
#endif

static struct resource rtc_resources[] = {
	[0] =	{
		.start	= 0xfffffec0,
		.end	= 0xfffffec0 + 0x1e,
		.flags  = IORESOURCE_IO,
	},
	[1] =	{
		.start  = 20,
		.flags	= IORESOURCE_IRQ,
	},
	[2] =	{
		.start	= 21,
		.flags	= IORESOURCE_IRQ,
	},
	[3] =	{
		.start	= 22,
		.flags  = IORESOURCE_IRQ,
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
		.mapbase	= 0xfffffe80,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCI,
		.irqs		= { 23, 24, 25, 0 },
	},
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	{
		.mapbase	= 0xa4000150,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 56, 57, 59, 58 },
	},
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	{
		.mapbase	= 0xa4000140,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_IRDA,
		.irqs		= { 52, 53, 55, 54 },
	},
#endif
	{
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

static struct platform_device *sh770x_devices[] __initdata = {
	&sci_device,
	&rtc_device,
};

static int __init sh770x_devices_setup(void)
{
	return platform_add_devices(sh770x_devices,
		ARRAY_SIZE(sh770x_devices));
}
__initcall(sh770x_devices_setup);

#define INTC_ICR1		0xa4000010UL
#define INTC_ICR1_IRQLVL	(1<<14)

void __init plat_irq_setup_pins(int mode)
{
	if (mode == IRQ_MODE_IRQ) {
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
		ctrl_outw(ctrl_inw(INTC_ICR1) & ~INTC_ICR1_IRQLVL, INTC_ICR1);
		register_intc_controller(&intc_desc_irq);
		return;
#endif
	}
	BUG();
}

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}
