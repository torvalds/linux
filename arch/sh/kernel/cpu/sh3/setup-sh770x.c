/*
 * SH3 Setup code for SH7706, SH7707, SH7708, SH7709
 *
 *  Copyright (C) 2007  Magnus Damm
 *  Copyright (C) 2009  Paul Mundt
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
#include <linux/serial_sci.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,
	PINT07, PINT815,
	DMAC, SCIF0, SCIF2, SCI, ADC_ADI,
	LCDC, PCC0, PCC1,
	TMU0, TMU1, TMU2,
	RTC, WDT, REF,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2, 0x460),
	INTC_VECT(RTC, 0x480), INTC_VECT(RTC, 0x4a0),
	INTC_VECT(RTC, 0x4c0),
	INTC_VECT(SCI, 0x4e0), INTC_VECT(SCI, 0x500),
	INTC_VECT(SCI, 0x520), INTC_VECT(SCI, 0x540),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF, 0x580),
	INTC_VECT(REF, 0x5a0),
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	/* IRQ0->5 are handled in setup-sh3.c */
	INTC_VECT(DMAC, 0x800), INTC_VECT(DMAC, 0x820),
	INTC_VECT(DMAC, 0x840), INTC_VECT(DMAC, 0x860),
	INTC_VECT(ADC_ADI, 0x980),
	INTC_VECT(SCIF2, 0x900), INTC_VECT(SCIF2, 0x920),
	INTC_VECT(SCIF2, 0x940), INTC_VECT(SCIF2, 0x960),
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	INTC_VECT(PINT07, 0x700), INTC_VECT(PINT815, 0x720),
	INTC_VECT(SCIF0, 0x880), INTC_VECT(SCIF0, 0x8a0),
	INTC_VECT(SCIF0, 0x8c0), INTC_VECT(SCIF0, 0x8e0),
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
	INTC_VECT(LCDC, 0x9a0),
	INTC_VECT(PCC0, 0x9c0), INTC_VECT(PCC1, 0x9e0),
#endif
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

static DECLARE_INTC_DESC(intc_desc, "sh770x", vectors, NULL,
			 NULL, prio_registers, NULL);

static struct resource rtc_resources[] = {
	[0] =	{
		.start	= 0xfffffec0,
		.end	= 0xfffffec0 + 0x1e,
		.flags  = IORESOURCE_IO,
	},
	[1] =	{
		.start	= 20,
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
		.irqs		= { 23, 23, 23, 0 },
	},
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	{
		.mapbase	= 0xa4000150,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 56, 56, 56, 56 },
	},
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	{
		.mapbase	= 0xa4000140,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_IRDA,
		.irqs		= { 52, 52, 52, 52 },
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

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	plat_irq_setup_sh3();
#endif
}
