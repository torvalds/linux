/*
 * SH7705 Setup
 *
 *  Copyright (C) 2006 - 2009  Paul Mundt
 *  Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <asm/rtc.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,
	PINT07, PINT815,

	DMAC, SCIF0, SCIF2, ADC_ADI, USB,

	TPU0, TPU1, TPU2, TPU3,
	TMU0, TMU1, TMU2,

	RTC, WDT, REF_RCMI,
};

static struct intc_vect vectors[] __initdata = {
	/* IRQ0->5 are handled in setup-sh3.c */
	INTC_VECT(PINT07, 0x700), INTC_VECT(PINT815, 0x720),
	INTC_VECT(DMAC, 0x800), INTC_VECT(DMAC, 0x820),
	INTC_VECT(DMAC, 0x840), INTC_VECT(DMAC, 0x860),
	INTC_VECT(SCIF0, 0x880), INTC_VECT(SCIF0, 0x8a0),
	INTC_VECT(SCIF0, 0x8e0),
	INTC_VECT(SCIF2, 0x900), INTC_VECT(SCIF2, 0x920),
	INTC_VECT(SCIF2, 0x960),
	INTC_VECT(ADC_ADI, 0x980),
	INTC_VECT(USB, 0xa20), INTC_VECT(USB, 0xa40),
	INTC_VECT(TPU0, 0xc00), INTC_VECT(TPU1, 0xc20),
	INTC_VECT(TPU2, 0xc80), INTC_VECT(TPU3, 0xca0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2, 0x460),
	INTC_VECT(RTC, 0x480), INTC_VECT(RTC, 0x4a0),
	INTC_VECT(RTC, 0x4c0),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF_RCMI, 0x580),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffffee2, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, RTC } },
	{ 0xfffffee4, 0, 16, 4, /* IPRB */ { WDT, REF_RCMI, 0, 0 } },
	{ 0xa4000016, 0, 16, 4, /* IPRC */ { IRQ3, IRQ2, IRQ1, IRQ0 } },
	{ 0xa4000018, 0, 16, 4, /* IPRD */ { PINT07, PINT815, IRQ5, IRQ4 } },
	{ 0xa400001a, 0, 16, 4, /* IPRE */ { DMAC, SCIF0, SCIF2, ADC_ADI } },
	{ 0xa4080000, 0, 16, 4, /* IPRF */ { 0, 0, USB } },
	{ 0xa4080002, 0, 16, 4, /* IPRG */ { TPU0, TPU1 } },
	{ 0xa4080004, 0, 16, 4, /* IPRH */ { TPU2, TPU3 } },

};

static DECLARE_INTC_DESC(intc_desc, "sh7705", vectors, NULL,
			 NULL, prio_registers, NULL);

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xa4410000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 56, 56, 56 },
	}, {
		.mapbase	= 0xa4400000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 52, 52 },
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
};

static struct sh_rtc_platform_info rtc_info = {
	.capabilities	= RTC_CAP_4_DIGIT_YEAR,
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
	.dev		= {
		.platform_data = &rtc_info,
	},
};

static struct platform_device *sh7705_devices[] __initdata = {
	&sci_device,
	&rtc_device,
};

static int __init sh7705_devices_setup(void)
{
	return platform_add_devices(sh7705_devices,
				    ARRAY_SIZE(sh7705_devices));
}
__initcall(sh7705_devices_setup);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
	plat_irq_setup_sh3();
}
