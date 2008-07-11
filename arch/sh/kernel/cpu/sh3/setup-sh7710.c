/*
 * SH3 Setup code for SH7710, SH7712
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
#include <linux/serial_sci.h>
#include <asm/rtc.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,
	DMAC_DEI0, DMAC_DEI1, DMAC_DEI2, DMAC_DEI3,
	SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI,
	SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI,
	DMAC_DEI4, DMAC_DEI5,
	IPSEC,
	EDMAC0, EDMAC1, EDMAC2,
	SIOF0_ERI, SIOF0_TXI, SIOF0_RXI, SIOF0_CCI,
	SIOF1_ERI, SIOF1_TXI, SIOF1_RXI, SIOF1_CCI,
	TMU0, TMU1, TMU2,
	RTC_ATI, RTC_PRI, RTC_CUI,
	WDT,
	REF,

	/* interrupt groups */
	RTC, DMAC1, SCIF0, SCIF1, DMAC2, SIOF0, SIOF1,
};

static struct intc_vect vectors[] __initdata = {
	/* IRQ0->5 are handled in setup-sh3.c */
	INTC_VECT(DMAC_DEI0, 0x800), INTC_VECT(DMAC_DEI1, 0x820),
	INTC_VECT(DMAC_DEI2, 0x840), INTC_VECT(DMAC_DEI3, 0x860),
	INTC_VECT(SCIF0_ERI, 0x880), INTC_VECT(SCIF0_RXI, 0x8a0),
	INTC_VECT(SCIF0_BRI, 0x8c0), INTC_VECT(SCIF0_TXI, 0x8e0),
	INTC_VECT(SCIF1_ERI, 0x900), INTC_VECT(SCIF1_RXI, 0x920),
	INTC_VECT(SCIF1_BRI, 0x940), INTC_VECT(SCIF1_TXI, 0x960),
	INTC_VECT(DMAC_DEI4, 0xb80), INTC_VECT(DMAC_DEI5, 0xba0),
#ifdef CONFIG_CPU_SUBTYPE_SH7710
	INTC_VECT(IPSEC, 0xbe0),
#endif
	INTC_VECT(EDMAC0, 0xc00), INTC_VECT(EDMAC1, 0xc20),
	INTC_VECT(EDMAC2, 0xc40),
	INTC_VECT(SIOF0_ERI, 0xe00), INTC_VECT(SIOF0_TXI, 0xe20),
	INTC_VECT(SIOF0_RXI, 0xe40), INTC_VECT(SIOF0_CCI, 0xe60),
	INTC_VECT(SIOF1_ERI, 0xe80), INTC_VECT(SIOF1_TXI, 0xea0),
	INTC_VECT(SIOF1_RXI, 0xec0), INTC_VECT(SIOF1_CCI, 0xee0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440),
	INTC_VECT(RTC_ATI, 0x480), INTC_VECT(RTC_PRI, 0x4a0),
	INTC_VECT(RTC_CUI, 0x4c0),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF, 0x580),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(DMAC1, DMAC_DEI0, DMAC_DEI1, DMAC_DEI2, DMAC_DEI3),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI),
	INTC_GROUP(SCIF1, SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI),
	INTC_GROUP(DMAC2, DMAC_DEI4, DMAC_DEI5),
	INTC_GROUP(SIOF0, SIOF0_ERI, SIOF0_TXI, SIOF0_RXI, SIOF0_CCI),
	INTC_GROUP(SIOF1, SIOF1_ERI, SIOF1_TXI, SIOF1_RXI, SIOF1_CCI),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffffee2, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, RTC } },
	{ 0xfffffee4, 0, 16, 4, /* IPRB */ { WDT, REF, 0, 0 } },
	{ 0xa4000016, 0, 16, 4, /* IPRC */ { IRQ3, IRQ2, IRQ1, IRQ0 } },
	{ 0xa4000018, 0, 16, 4, /* IPRD */ { 0, 0, IRQ5, IRQ4 } },
	{ 0xa400001a, 0, 16, 4, /* IPRE */ { DMAC1, SCIF0, SCIF1 } },
	{ 0xa4080000, 0, 16, 4, /* IPRF */ { IPSEC, DMAC2 } },
	{ 0xa4080002, 0, 16, 4, /* IPRG */ { EDMAC0, EDMAC1, EDMAC2 } },
	{ 0xa4080004, 0, 16, 4, /* IPRH */ { 0, 0, 0, SIOF0 } },
	{ 0xa4080006, 0, 16, 4, /* IPRI */ { 0, 0, SIOF1 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7710", vectors, groups,
			 NULL, prio_registers, NULL);

static struct resource rtc_resources[] = {
	[0] =	{
		.start	= 0xa413fec0,
		.end	= 0xa413fec0 + 0x1e,
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

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xa4400000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 53, 55, 54 },
	}, {
		.mapbase	= 0xa4410000,
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
	&rtc_device,
};

static int __init sh7710_devices_setup(void)
{
	return platform_add_devices(sh7710_devices,
				    ARRAY_SIZE(sh7710_devices));
}
__initcall(sh7710_devices_setup);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
	plat_irq_setup_sh3();
}
