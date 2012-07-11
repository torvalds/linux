/*
 * arch/sh/kernel/cpu/sh4a/setup-sh7734.c

 * SH7734 Setup
 *
 * Copyright (C) 2011,2012 Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>
 * Copyright (C) 2011,2012 Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <linux/io.h>
#include <asm/clock.h>
#include <asm/irq.h>
#include <cpu/sh7734.h>

/* SCIF */
static struct plat_sci_port scif0_platform_data = {
	.mapbase        = 0xFFE40000,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr          = SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id  = SCBRR_ALGO_2,
	.type           = PORT_SCIF,
	.irqs           = SCIx_IRQ_MUXED(evt2irq(0x8C0)),
	.regtype        = SCIx_SH4_SCIF_REGTYPE,
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id			= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase        = 0xFFE41000,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr          = SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type           = PORT_SCIF,
	.irqs           = SCIx_IRQ_MUXED(evt2irq(0x8E0)),
	.regtype        = SCIx_SH4_SCIF_REGTYPE,
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id         = 1,
	.dev		= {
		.platform_data = &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.mapbase        = 0xFFE42000,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr          = SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id  = SCBRR_ALGO_2,
	.type           = PORT_SCIF,
	.irqs           = SCIx_IRQ_MUXED(evt2irq(0x900)),
	.regtype        = SCIx_SH4_SCIF_REGTYPE,
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id         = 2,
	.dev		= {
		.platform_data = &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.mapbase        = 0xFFE43000,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr          = SCSCR_RE | SCSCR_TE | SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id  = SCBRR_ALGO_2,
	.type           = PORT_SCIF,
	.irqs           = SCIx_IRQ_MUXED(evt2irq(0x920)),
	.regtype        = SCIx_SH4_SCIF_REGTYPE,
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id	        = 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.mapbase        = 0xFFE44000,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr          = SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type           = PORT_SCIF,
	.irqs           = SCIx_IRQ_MUXED(evt2irq(0x940)),
	.regtype        = SCIx_SH4_SCIF_REGTYPE,
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id	        = 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.mapbase        = 0xFFE43000,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr          = SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type           = PORT_SCIF,
	.irqs           = SCIx_IRQ_MUXED(evt2irq(0x960)),
	.regtype		= SCIx_SH4_SCIF_REGTYPE,
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id	        = 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

/* RTC */
static struct resource rtc_resources[] = {
	[0] = {
		.name	= "rtc",
		.start	= 0xFFFC5000,
		.end	= 0xFFFC5000 + 0x26 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.start	= evt2irq(0xC00),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

/* I2C 0 */
static struct resource i2c0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start  = 0xFFC70000,
		.end    = 0xFFC7000A - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x860),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c0_device = {
	.name           = "i2c-sh7734",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(i2c0_resources),
	.resource       = i2c0_resources,
};

/* TMU */
static struct sh_timer_config tmu0_platform_data = {
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource tmu0_resources[] = {
	[0] = {
		.start	= 0xFFD80008,
		.end	= 0xFFD80014 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x400),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu0_device = {
	.name	= "sh_tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

static struct sh_timer_config tmu1_platform_data = {
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clocksource_rating = 200,
};

static struct resource tmu1_resources[] = {
	[0] = {
		.start	= 0xFFD80014,
		.end	= 0xFFD80020 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x420),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu1_device = {
	.name		= "sh_tmu",
	.id			= 1,
	.dev = {
		.platform_data	= &tmu1_platform_data,
	},
	.resource	= tmu1_resources,
	.num_resources	= ARRAY_SIZE(tmu1_resources),
};

static struct sh_timer_config tmu2_platform_data = {
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu2_resources[] = {
	[0] = {
		.start	= 0xFFD80020,
		.end	= 0xFFD80030 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x440),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu2_device = {
	.name		= "sh_tmu",
	.id			= 2,
	.dev = {
		.platform_data	= &tmu2_platform_data,
	},
	.resource	= tmu2_resources,
	.num_resources	= ARRAY_SIZE(tmu2_resources),
};


static struct sh_timer_config tmu3_platform_data = {
	.channel_offset = 0x04,
	.timer_bit = 0,
};

static struct resource tmu3_resources[] = {
	[0] = {
		.start	= 0xFFD81008,
		.end	= 0xFFD81014 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x480),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu3_device = {
	.name		= "sh_tmu",
	.id			= 3,
	.dev = {
		.platform_data	= &tmu3_platform_data,
	},
	.resource	= tmu3_resources,
	.num_resources	= ARRAY_SIZE(tmu3_resources),
};

static struct sh_timer_config tmu4_platform_data = {
	.channel_offset = 0x10,
	.timer_bit = 1,
};

static struct resource tmu4_resources[] = {
	[0] = {
		.start	= 0xFFD81014,
		.end	= 0xFFD81020 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x4A0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu4_device = {
	.name		= "sh_tmu",
	.id			= 4,
	.dev = {
		.platform_data	= &tmu4_platform_data,
	},
	.resource	= tmu4_resources,
	.num_resources	= ARRAY_SIZE(tmu4_resources),
};

static struct sh_timer_config tmu5_platform_data = {
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu5_resources[] = {
	[0] = {
		.start	= 0xFFD81020,
		.end	= 0xFFD81030 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x4C0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu5_device = {
	.name		= "sh_tmu",
	.id			= 5,
	.dev = {
		.platform_data	= &tmu5_platform_data,
	},
	.resource	= tmu5_resources,
	.num_resources	= ARRAY_SIZE(tmu5_resources),
};

static struct sh_timer_config tmu6_platform_data = {
	.channel_offset = 0x4,
	.timer_bit = 0,
};

static struct resource tmu6_resources[] = {
	[0] = {
		.start	= 0xFFD82008,
		.end	= 0xFFD82014 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x500),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu6_device = {
	.name		= "sh_tmu",
	.id			= 6,
	.dev = {
		.platform_data	= &tmu6_platform_data,
	},
	.resource	= tmu6_resources,
	.num_resources	= ARRAY_SIZE(tmu6_resources),
};

static struct sh_timer_config tmu7_platform_data = {
	.channel_offset = 0x10,
	.timer_bit = 1,
};

static struct resource tmu7_resources[] = {
	[0] = {
		.start	= 0xFFD82014,
		.end	= 0xFFD82020 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x520),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu7_device = {
	.name		= "sh_tmu",
	.id			= 7,
	.dev = {
		.platform_data	= &tmu7_platform_data,
	},
	.resource	= tmu7_resources,
	.num_resources	= ARRAY_SIZE(tmu7_resources),
};

static struct sh_timer_config tmu8_platform_data = {
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu8_resources[] = {
	[0] = {
		.start	= 0xFFD82020,
		.end	= 0xFFD82030 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x540),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu8_device = {
	.name		= "sh_tmu",
	.id			= 8,
	.dev = {
		.platform_data	= &tmu8_platform_data,
	},
	.resource	= tmu8_resources,
	.num_resources	= ARRAY_SIZE(tmu8_resources),
};

static struct platform_device *sh7734_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
	&tmu6_device,
	&tmu7_device,
	&tmu8_device,
	&rtc_device,
};

static struct platform_device *sh7734_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
	&tmu6_device,
	&tmu7_device,
	&tmu8_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh7734_early_devices,
		ARRAY_SIZE(sh7734_early_devices));
}

#define GROUP 0
enum {
	UNUSED = 0,

	/* interrupt sources */

	IRL0_LLLL, IRL0_LLLH, IRL0_LLHL, IRL0_LLHH,
	IRL0_LHLL, IRL0_LHLH, IRL0_LHHL, IRL0_LHHH,
	IRL0_HLLL, IRL0_HLLH, IRL0_HLHL, IRL0_HLHH,
	IRL0_HHLL, IRL0_HHLH, IRL0_HHHL,

	IRQ0, IRQ1, IRQ2, IRQ3,
	DU,
	TMU00, TMU10, TMU20, TMU21,
	TMU30, TMU40, TMU50, TMU51,
	TMU60, TMU70, TMU80,
	RESET_WDT,
	USB,
	HUDI,
	SHDMAC,
	SSI0, SSI1,	SSI2, SSI3,
	VIN0,
	RGPVG,
	_2DG,
	MMC,
	HSPI,
	LBSCATA,
	I2C0,
	RCAN0,
	MIMLB,
	SCIF0, SCIF1, SCIF2, SCIF3, SCIF4, SCIF5,
	LBSCDMAC0, LBSCDMAC1, LBSCDMAC2,
	RCAN1,
	SDHI0, SDHI1,
	IEBUS,
	HPBDMAC0_3, HPBDMAC4_10, HPBDMAC11_18, HPBDMAC19_22, HPBDMAC23_25_27_28,
	RTC,
	VIN1,
	LCDC,
	SRC0, SRC1,
	GETHER,
	SDHI2,
	GPIO0_3, GPIO4_5,
	STIF0, STIF1,
	ADMAC,
	HIF,
	FLCTL,
	ADC,
	MTU2,
	RSPI,
	QSPI,
	HSCIF,
	VEU3F_VE3,

	/* Group */
	/* Mask */
	STIF_M,
	GPIO_M,
	HPBDMAC_M,
	LBSCDMAC_M,
	RCAN_M,
	SRC_M,
	SCIF_M,
	LCDC_M,
	_2DG_M,
	VIN_M,
	TMU_3_M,
	TMU_0_M,

	/* Priority */
	RCAN_P,
	LBSCDMAC_P,

	/* Common */
	SDHI,
	SSI,
	SPI,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(DU, 0x3E0),
	INTC_VECT(TMU00, 0x400),
	INTC_VECT(TMU10, 0x420),
	INTC_VECT(TMU20, 0x440),
	INTC_VECT(TMU30, 0x480),
	INTC_VECT(TMU40, 0x4A0),
	INTC_VECT(TMU50, 0x4C0),
	INTC_VECT(TMU51, 0x4E0),
	INTC_VECT(TMU60, 0x500),
	INTC_VECT(TMU70, 0x520),
	INTC_VECT(TMU80, 0x540),
	INTC_VECT(RESET_WDT, 0x560),
	INTC_VECT(USB, 0x580),
	INTC_VECT(HUDI, 0x600),
	INTC_VECT(SHDMAC, 0x620),
	INTC_VECT(SSI0, 0x6C0),
	INTC_VECT(SSI1, 0x6E0),
	INTC_VECT(SSI2, 0x700),
	INTC_VECT(SSI3, 0x720),
	INTC_VECT(VIN0, 0x740),
	INTC_VECT(RGPVG, 0x760),
	INTC_VECT(_2DG, 0x780),
	INTC_VECT(MMC, 0x7A0),
	INTC_VECT(HSPI, 0x7E0),
	INTC_VECT(LBSCATA, 0x840),
	INTC_VECT(I2C0, 0x860),
	INTC_VECT(RCAN0, 0x880),
	INTC_VECT(SCIF0, 0x8A0),
	INTC_VECT(SCIF1, 0x8C0),
	INTC_VECT(SCIF2, 0x900),
	INTC_VECT(SCIF3, 0x920),
	INTC_VECT(SCIF4, 0x940),
	INTC_VECT(SCIF5, 0x960),
	INTC_VECT(LBSCDMAC0, 0x9E0),
	INTC_VECT(LBSCDMAC1, 0xA00),
	INTC_VECT(LBSCDMAC2, 0xA20),
	INTC_VECT(RCAN1, 0xA60),
	INTC_VECT(SDHI0, 0xAE0),
	INTC_VECT(SDHI1, 0xB00),
	INTC_VECT(IEBUS, 0xB20),
	INTC_VECT(HPBDMAC0_3, 0xB60),
	INTC_VECT(HPBDMAC4_10, 0xB80),
	INTC_VECT(HPBDMAC11_18, 0xBA0),
	INTC_VECT(HPBDMAC19_22, 0xBC0),
	INTC_VECT(HPBDMAC23_25_27_28, 0xBE0),
	INTC_VECT(RTC, 0xC00),
	INTC_VECT(VIN1, 0xC20),
	INTC_VECT(LCDC, 0xC40),
	INTC_VECT(SRC0, 0xC60),
	INTC_VECT(SRC1, 0xC80),
	INTC_VECT(GETHER, 0xCA0),
	INTC_VECT(SDHI2, 0xCC0),
	INTC_VECT(GPIO0_3, 0xCE0),
	INTC_VECT(GPIO4_5, 0xD00),
	INTC_VECT(STIF0, 0xD20),
	INTC_VECT(STIF1, 0xD40),
	INTC_VECT(ADMAC, 0xDA0),
	INTC_VECT(HIF, 0xDC0),
	INTC_VECT(FLCTL, 0xDE0),
	INTC_VECT(ADC, 0xE00),
	INTC_VECT(MTU2, 0xE20),
	INTC_VECT(RSPI, 0xE40),
	INTC_VECT(QSPI, 0xE60),
	INTC_VECT(HSCIF, 0xFC0),
	INTC_VECT(VEU3F_VE3, 0xF40),
};

static struct intc_group groups[] __initdata = {
	/* Common */
	INTC_GROUP(SDHI, SDHI0, SDHI1, SDHI2),
	INTC_GROUP(SPI, HSPI, RSPI, QSPI),
	INTC_GROUP(SSI, SSI0, SSI1, SSI2, SSI3),

	/* Mask group */
	INTC_GROUP(STIF_M, STIF0, STIF1), /* 22 */
	INTC_GROUP(GPIO_M, GPIO0_3, GPIO4_5), /* 21 */
	INTC_GROUP(HPBDMAC_M, HPBDMAC0_3, HPBDMAC4_10, HPBDMAC11_18,
			HPBDMAC19_22, HPBDMAC23_25_27_28), /* 19 */
	INTC_GROUP(LBSCDMAC_M, LBSCDMAC0, LBSCDMAC1, LBSCDMAC2), /* 18 */
	INTC_GROUP(RCAN_M, RCAN0, RCAN1, IEBUS), /* 17 */
	INTC_GROUP(SRC_M, SRC0, SRC1), /* 16 */
	INTC_GROUP(SCIF_M, SCIF0, SCIF1, SCIF2, SCIF3, SCIF4, SCIF5,
			HSCIF), /* 14 */
	INTC_GROUP(LCDC_M, LCDC, MIMLB), /* 13 */
	INTC_GROUP(_2DG_M, _2DG, RGPVG), /* 12 */
	INTC_GROUP(VIN_M, VIN0, VIN1), /* 10 */
	INTC_GROUP(TMU_3_M, TMU30, TMU40, TMU50, TMU51,
			TMU60, TMU60, TMU70, TMU80), /* 2 */
	INTC_GROUP(TMU_0_M, TMU00, TMU10, TMU20, TMU21), /* 1 */

	/* Priority group*/
	INTC_GROUP(RCAN_P, RCAN0, RCAN1), /* INT2PRI5 */
	INTC_GROUP(LBSCDMAC_P, LBSCDMAC0, LBSCDMAC1), /* INT2PRI5 */
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xFF804040, 0xFF804044, 32, /* INT2MSKRG / INT2MSKCR */
	  { 0,
		VEU3F_VE3,
		SDHI, /* SDHI 0-2 */
		ADMAC,
		FLCTL,
		RESET_WDT,
		HIF,
		ADC,
		MTU2,
		STIF_M, /* STIF 0,1 */
		GPIO_M, /* GPIO 0-5*/
		GETHER,
		HPBDMAC_M, /* HPBDMAC 0_3 - 23_25_27_28 */
		LBSCDMAC_M, /* LBSCDMAC 0 - 2 */
		RCAN_M, /* RCAN, IEBUS */
		SRC_M,	/* SRC 0,1 */
		LBSCATA,
		SCIF_M, /* SCIF 0-5, HSCIF */
		LCDC_M, /* LCDC, MIMLB */
		_2DG_M,	/* 2DG, RGPVG */
		SPI, /* HSPI, RSPI, QSPI */
		VIN_M,	/* VIN0, 1 */
		SSI,	/* SSI 0-3 */
		USB,
		SHDMAC,
		HUDI,
		MMC,
		RTC,
		I2C0, /* I2C */ /* I2C 0, 1*/
		TMU_3_M, /* TMU30 - TMU80 */
		TMU_0_M, /* TMU00 - TMU21 */
		DU } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xFF804000, 0, 32, 8, /* INT2PRI0 */
		{ DU, TMU00, TMU10, TMU20 } },
	{ 0xFF804004, 0, 32, 8, /* INT2PRI1 */
		{ TMU30, TMU60, RTC, SDHI } },
	{ 0xFF804008, 0, 32, 8, /* INT2PRI2 */
		{ HUDI, SHDMAC, USB, SSI } },
	{ 0xFF80400C, 0, 32, 8, /* INT2PRI3 */
		{ VIN0, SPI, _2DG, LBSCATA } },
	{ 0xFF804010, 0, 32, 8, /* INT2PRI4 */
		{ SCIF0, SCIF3, HSCIF, LCDC } },
	{ 0xFF804014, 0, 32, 8, /* INT2PRI5 */
		{ RCAN_P, LBSCDMAC_P, LBSCDMAC2, MMC } },
	{ 0xFF804018, 0, 32, 8, /* INT2PRI6 */
		{ HPBDMAC0_3, HPBDMAC4_10, HPBDMAC11_18, HPBDMAC19_22 } },
	{ 0xFF80401C, 0, 32, 8, /* INT2PRI7 */
		{ HPBDMAC23_25_27_28, I2C0, SRC0, SRC1 } },
	{ 0xFF804020, 0, 32, 8, /* INT2PRI8 */
		{ 0 /* ADIF */, VIN1, RESET_WDT, HIF } },
	{ 0xFF804024, 0, 32, 8, /* INT2PRI9 */
		{ ADMAC, FLCTL, GPIO0_3, GPIO4_5 } },
	{ 0xFF804028, 0, 32, 8, /* INT2PRI10 */
		{ STIF0, STIF1, VEU3F_VE3, GETHER } },
	{ 0xFF80402C, 0, 32, 8, /* INT2PRI11 */
		{ MTU2, RGPVG, MIMLB, IEBUS } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7734", vectors, groups,
	mask_registers, prio_registers, NULL);

/* Support for external interrupt pins in IRQ mode */

static struct intc_vect irq3210_vectors[] __initdata = {
	INTC_VECT(IRQ0, 0x240), INTC_VECT(IRQ1, 0x280),
	INTC_VECT(IRQ2, 0x2C0), INTC_VECT(IRQ3, 0x300),
};

static struct intc_sense_reg irq3210_sense_registers[] __initdata = {
	{ 0xFF80201C, 32, 2, /* ICR1 */
	{ IRQ0, IRQ1, IRQ2, IRQ3, } },
};

static struct intc_mask_reg irq3210_ack_registers[] __initdata = {
	{ 0xFF802024, 0, 32, /* INTREQ */
	{ IRQ0, IRQ1, IRQ2, IRQ3, } },
};

static struct intc_mask_reg irq3210_mask_registers[] __initdata = {
	{ 0xFF802044, 0xFF802064, 32, /* INTMSK0 / INTMSKCLR0 */
	{ IRQ0, IRQ1, IRQ2, IRQ3, } },
};

static struct intc_prio_reg irq3210_prio_registers[] __initdata = {
	{ 0xFF802010, 0, 32, 4, /* INTPRI */
	{ IRQ0, IRQ1, IRQ2, IRQ3, } },
};

static DECLARE_INTC_DESC_ACK(intc_desc_irq3210, "sh7734-irq3210",
	irq3210_vectors, NULL,
	irq3210_mask_registers, irq3210_prio_registers,
	irq3210_sense_registers, irq3210_ack_registers);

/* External interrupt pins in IRL mode */

static struct intc_vect vectors_irl3210[] __initdata = {
	INTC_VECT(IRL0_LLLL, 0x200), INTC_VECT(IRL0_LLLH, 0x220),
	INTC_VECT(IRL0_LLHL, 0x240), INTC_VECT(IRL0_LLHH, 0x260),
	INTC_VECT(IRL0_LHLL, 0x280), INTC_VECT(IRL0_LHLH, 0x2a0),
	INTC_VECT(IRL0_LHHL, 0x2c0), INTC_VECT(IRL0_LHHH, 0x2e0),
	INTC_VECT(IRL0_HLLL, 0x300), INTC_VECT(IRL0_HLLH, 0x320),
	INTC_VECT(IRL0_HLHL, 0x340), INTC_VECT(IRL0_HLHH, 0x360),
	INTC_VECT(IRL0_HHLL, 0x380), INTC_VECT(IRL0_HHLH, 0x3a0),
	INTC_VECT(IRL0_HHHL, 0x3c0),
};

static DECLARE_INTC_DESC(intc_desc_irl3210, "sh7734-irl3210",
	vectors_irl3210, NULL, mask_registers, NULL, NULL);

#define INTC_ICR0		0xFF802000
#define INTC_INTMSK0    0xFF802044
#define INTC_INTMSK1    0xFF802048
#define INTC_INTMSKCLR0 0xFF802064
#define INTC_INTMSKCLR1 0xFF802068

void __init plat_irq_setup(void)
{
	/* disable IRQ3-0 */
	__raw_writel(0xF0000000, INTC_INTMSK0);

	/* disable IRL3-0 */
	__raw_writel(0x80000000, INTC_INTMSK1);

	/* select IRL mode for IRL3-0 */
	__raw_writel(__raw_readl(INTC_ICR0) & ~0x00800000, INTC_ICR0);

	/* disable holding function, ie enable "SH-4 Mode (LVLMODE)" */
	__raw_writel(__raw_readl(INTC_ICR0) | 0x00200000, INTC_ICR0);

	register_intc_controller(&intc_desc);
}

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ3210:
		/* select IRQ mode for IRL3-0 */
		__raw_writel(__raw_readl(INTC_ICR0) | 0x00800000, INTC_ICR0);
		register_intc_controller(&intc_desc_irq3210);
		break;
	case IRQ_MODE_IRL3210:
		/* enable IRL0-3 but don't provide any masking */
		__raw_writel(0x80000000, INTC_INTMSKCLR1);
		__raw_writel(0xf0000000, INTC_INTMSKCLR0);
		break;
	case IRQ_MODE_IRL3210_MASK:
		/* enable IRL0-3 and mask using cpu intc controller */
		__raw_writel(0x80000000, INTC_INTMSKCLR0);
		register_intc_controller(&intc_desc_irl3210);
		break;
	default:
		BUG();
	}
}
