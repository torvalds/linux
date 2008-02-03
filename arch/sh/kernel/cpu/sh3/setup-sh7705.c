/*
 * SH7705 Setup
 *
 *  Copyright (C) 2006, 2007  Paul Mundt
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
#include <asm/sci.h>
#include <asm/rtc.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,
	PINT07, PINT815,
	DMAC_DEI0, DMAC_DEI1, DMAC_DEI2, DMAC_DEI3,
	SCIF0_ERI, SCIF0_RXI, SCIF0_TXI,
	SCIF2_ERI, SCIF2_RXI, SCIF2_TXI,
	ADC_ADI,
	USB_USI0, USB_USI1,
	TPU0, TPU1, TPU2, TPU3,
	TMU0, TMU1, TMU2_TUNI, TMU2_TICPI,
	RTC_ATI, RTC_PRI, RTC_CUI,
	WDT,
	REF_RCMI,

	/* interrupt groups */
	RTC, TMU2, DMAC, USB, SCIF2, SCIF0,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(IRQ4, 0x680), INTC_VECT(IRQ5, 0x6a0),
	INTC_VECT(PINT07, 0x700), INTC_VECT(PINT815, 0x720),
	INTC_VECT(DMAC_DEI0, 0x800), INTC_VECT(DMAC_DEI1, 0x820),
	INTC_VECT(DMAC_DEI2, 0x840), INTC_VECT(DMAC_DEI3, 0x860),
	INTC_VECT(SCIF0_ERI, 0x880), INTC_VECT(SCIF0_RXI, 0x8a0),
	INTC_VECT(SCIF0_TXI, 0x8e0),
	INTC_VECT(SCIF2_ERI, 0x900), INTC_VECT(SCIF2_RXI, 0x920),
	INTC_VECT(SCIF2_TXI, 0x960),
	INTC_VECT(ADC_ADI, 0x980),
	INTC_VECT(USB_USI0, 0xa20), INTC_VECT(USB_USI1, 0xa40),
	INTC_VECT(TPU0, 0xc00), INTC_VECT(TPU1, 0xc20),
	INTC_VECT(TPU3, 0xc80), INTC_VECT(TPU1, 0xca0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2_TUNI, 0x440), INTC_VECT(TMU2_TICPI, 0x460),
	INTC_VECT(RTC_ATI, 0x480), INTC_VECT(RTC_PRI, 0x4a0),
	INTC_VECT(RTC_CUI, 0x4c0),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF_RCMI, 0x580),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(TMU2, TMU2_TUNI, TMU2_TICPI),
	INTC_GROUP(DMAC, DMAC_DEI0, DMAC_DEI1, DMAC_DEI2, DMAC_DEI3),
	INTC_GROUP(USB, USB_USI0, USB_USI1),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_TXI),
	INTC_GROUP(SCIF2, SCIF2_ERI, SCIF2_RXI, SCIF2_TXI),
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

static DECLARE_INTC_DESC(intc_desc, "sh7705", vectors, groups,
			 NULL, prio_registers, NULL);

static struct intc_vect vectors_irq[] __initdata = {
	INTC_VECT(IRQ0, 0x600), INTC_VECT(IRQ1, 0x620),
	INTC_VECT(IRQ2, 0x640), INTC_VECT(IRQ3, 0x660),
};

static DECLARE_INTC_DESC(intc_desc_irq, "sh7705-irq", vectors_irq, NULL,
			 NULL, prio_registers, NULL);

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xa4410000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 56, 57, 59 },
	}, {
		.mapbase	= 0xa4400000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 53, 55 },
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
	[2] =	{
		.start	= 21,
		.flags	= IORESOURCE_IRQ,
	},
	[3] =	{
		.start	= 22,
		.flags  = IORESOURCE_IRQ,
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

void __init plat_irq_setup_pins(int mode)
{
	if (mode == IRQ_MODE_IRQ) {
		register_intc_controller(&intc_desc_irq);
		return;
	}
	BUG();
}

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}
