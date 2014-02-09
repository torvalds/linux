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
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <cpu/serial.h>

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
		.start	= evt2irq(0x480),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct plat_sci_port scif0_platform_data = {
	.port_reg	= 0xa4000136,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_TE | SCSCR_RE,
	.type		= PORT_SCI,
	.ops		= &sh770x_sci_port_ops,
	.regshift	= 1,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xfffffe80, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x4e0)),
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.resource	= scif0_resources,
	.num_resources	= ARRAY_SIZE(scif0_resources),
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
static struct plat_sci_port scif1_platform_data = {
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_TE | SCSCR_RE,
	.type		= PORT_SCIF,
	.ops		= &sh770x_sci_port_ops,
	.regtype	= SCIx_SH3_SCIF_REGTYPE,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xa4000150, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x900)),
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.resource	= scif1_resources,
	.num_resources	= ARRAY_SIZE(scif1_resources),
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
static struct plat_sci_port scif2_platform_data = {
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_TE | SCSCR_RE,
	.type		= PORT_IRDA,
	.ops		= &sh770x_sci_port_ops,
	.regshift	= 1,
};

static struct resource scif2_resources[] = {
	DEFINE_RES_MEM(0xa4000140, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x880)),
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.resource	= scif2_resources,
	.num_resources	= ARRAY_SIZE(scif2_resources),
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};
#endif

static struct sh_timer_config tmu0_platform_data = {
	.channel_offset = 0x02,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource tmu0_resources[] = {
	[0] = {
		.start	= 0xfffffe94,
		.end	= 0xfffffe9f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x400),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu0_device = {
	.name		= "sh_tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

static struct sh_timer_config tmu1_platform_data = {
	.channel_offset = 0xe,
	.timer_bit = 1,
	.clocksource_rating = 200,
};

static struct resource tmu1_resources[] = {
	[0] = {
		.start	= 0xfffffea0,
		.end	= 0xfffffeab,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x420),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu1_device = {
	.name		= "sh_tmu",
	.id		= 1,
	.dev = {
		.platform_data	= &tmu1_platform_data,
	},
	.resource	= tmu1_resources,
	.num_resources	= ARRAY_SIZE(tmu1_resources),
};

static struct sh_timer_config tmu2_platform_data = {
	.channel_offset = 0x1a,
	.timer_bit = 2,
};

static struct resource tmu2_resources[] = {
	[0] = {
		.start	= 0xfffffeac,
		.end	= 0xfffffebb,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x440),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu2_device = {
	.name		= "sh_tmu",
	.id		= 2,
	.dev = {
		.platform_data	= &tmu2_platform_data,
	},
	.resource	= tmu2_resources,
	.num_resources	= ARRAY_SIZE(tmu2_resources),
};

static struct platform_device *sh770x_devices[] __initdata = {
	&scif0_device,
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	&scif1_device,
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	&scif2_device,
#endif
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&rtc_device,
};

static int __init sh770x_devices_setup(void)
{
	return platform_add_devices(sh770x_devices,
		ARRAY_SIZE(sh770x_devices));
}
arch_initcall(sh770x_devices_setup);

static struct platform_device *sh770x_early_devices[] __initdata = {
	&scif0_device,
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	&scif1_device,
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	&scif2_device,
#endif
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh770x_early_devices,
				   ARRAY_SIZE(sh770x_early_devices));
}

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	plat_irq_setup_sh3();
#endif
}
