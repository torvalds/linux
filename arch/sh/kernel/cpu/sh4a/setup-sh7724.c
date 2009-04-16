/*
 * SH7724 Setup
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on SH7723 Setup
 * Copyright (C) 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/mm.h>
#include <linux/serial_sci.h>
#include <linux/uio_driver.h>
#include <linux/sh_cmt.h>
#include <linux/io.h>
#include <asm/clock.h>
#include <asm/mmzone.h>

/* Serial */
static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase        = 0xffe00000,
		.flags          = UPF_BOOT_AUTOCONF,
		.type           = PORT_SCIF,
		.irqs           = { 80, 80, 80, 80 },
	}, {
		.mapbase        = 0xffe10000,
		.flags          = UPF_BOOT_AUTOCONF,
		.type           = PORT_SCIF,
		.irqs           = { 81, 81, 81, 81 },
	}, {
		.mapbase        = 0xffe20000,
		.flags          = UPF_BOOT_AUTOCONF,
		.type           = PORT_SCIF,
		.irqs           = { 82, 82, 82, 82 },
	}, {
		.mapbase	= 0xa4e30000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIFA,
		.irqs		= { 56, 56, 56, 56 },
	}, {
		.mapbase	= 0xa4e40000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIFA,
		.irqs		= { 88, 88, 88, 88 },
	}, {
		.mapbase	= 0xa4e50000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIFA,
		.irqs		= { 109, 109, 109, 109 },
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

/* RTC */
static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xa465fec0,
		.end	= 0xa465fec0 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Period IRQ */
		.start	= 69,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= 70,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= 68,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct platform_device *sh7724_devices[] __initdata = {
	&sci_device,
	&rtc_device,
};

static int __init sh7724_devices_setup(void)
{
	clk_always_enable("rtc0");   /* RTC */

	return platform_add_devices(sh7724_devices,
				    ARRAY_SIZE(sh7724_devices));
}
device_initcall(sh7724_devices_setup);

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	HUDI,
	DMAC1A_DEI0, DMAC1A_DEI1, DMAC1A_DEI2, DMAC1A_DEI3,
	_2DG_TRI, _2DG_INI, _2DG_CEI, _2DG_BRK,
	DMAC0A_DEI0, DMAC0A_DEI1, DMAC0A_DEI2, DMAC0A_DEI3,
	VIO_CEU20I, VIO_BEU20I, VIO_VEU3F1, VIO_VOUI,
	SCIFA_SCIFA0,
	VPU_VPUI,
	TPU_TPUI,
	CEU21I,
	BEU21I,
	USB_USI0,
	ATAPI,
	RTC_ATI, RTC_PRI, RTC_CUI,
	DMAC1B_DEI4, DMAC1B_DEI5, DMAC1B_DADERR,
	DMAC0B_DEI4, DMAC0B_DEI5, DMAC0B_DADERR,
	KEYSC_KEYI,
	SCIF_SCIF0, SCIF_SCIF1, SCIF_SCIF2,
	VEU3F0I,
	MSIOF_MSIOFI0, MSIOF_MSIOFI1,
	SPU_SPUI0, SPU_SPUI1,
	SCIFA_SCIFA1,
/*	ICB_ICBI, */
	ETHI,
	I2C1_ALI, I2C1_TACKI, I2C1_WAITI, I2C1_DTEI,
	I2C0_ALI, I2C0_TACKI, I2C0_WAITI, I2C0_DTEI,
	SDHI0_SDHII0, SDHI0_SDHII1, SDHI0_SDHII2,
	CMT_CMTI,
	TSIF_TSIFI,
/*	ICB_LMBI, */
	FSI_FSI,
	SCIFA_SCIFA2,
	TMU0_TUNI0, TMU0_TUNI1, TMU0_TUNI2,
	IRDA_IRDAI,
	SDHI1_SDHII0, SDHI1_SDHII1, SDHI1_SDHII2,
	JPU_JPUI,
	MMC_MMCI0, MMC_MMCI1, MMC_MMCI2,
	LCDC_LCDCI,
	TMU1_TUNI0, TMU1_TUNI1, TMU1_TUNI2,

	/* interrupt groups */
	DMAC1A, _2DG, DMAC0A, VIO, RTC,
	DMAC1B, DMAC0B, I2C0, I2C1, SDHI0, SDHI1, SPU, MMC,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(IRQ0, 0x600), INTC_VECT(IRQ1, 0x620),
	INTC_VECT(IRQ2, 0x640), INTC_VECT(IRQ3, 0x660),
	INTC_VECT(IRQ4, 0x680), INTC_VECT(IRQ5, 0x6a0),
	INTC_VECT(IRQ6, 0x6c0), INTC_VECT(IRQ7, 0x6e0),

	INTC_VECT(DMAC1A_DEI0, 0x700),
	INTC_VECT(DMAC1A_DEI1, 0x720),
	INTC_VECT(DMAC1A_DEI2, 0x740),
	INTC_VECT(DMAC1A_DEI3, 0x760),

	INTC_VECT(_2DG_TRI, 0x780),
	INTC_VECT(_2DG_INI, 0x7A0),
	INTC_VECT(_2DG_CEI, 0x7C0),
	INTC_VECT(_2DG_BRK, 0x7E0),

	INTC_VECT(DMAC0A_DEI0, 0x800),
	INTC_VECT(DMAC0A_DEI1, 0x820),
	INTC_VECT(DMAC0A_DEI2, 0x840),
	INTC_VECT(DMAC0A_DEI3, 0x860),

	INTC_VECT(VIO_CEU20I, 0x880),
	INTC_VECT(VIO_BEU20I, 0x8A0),
	INTC_VECT(VIO_VEU3F1, 0x8C0),
	INTC_VECT(VIO_VOUI, 0x8E0),

	INTC_VECT(SCIFA_SCIFA0, 0x900),
	INTC_VECT(VPU_VPUI, 0x980),
	INTC_VECT(TPU_TPUI, 0x9A0),
	INTC_VECT(CEU21I, 0x9E0),
	INTC_VECT(BEU21I, 0xA00),
	INTC_VECT(USB_USI0, 0xA20),
	INTC_VECT(ATAPI, 0xA60),

	INTC_VECT(RTC_ATI, 0xA80),
	INTC_VECT(RTC_PRI, 0xAA0),
	INTC_VECT(RTC_CUI, 0xAC0),

	INTC_VECT(DMAC1B_DEI4, 0xB00),
	INTC_VECT(DMAC1B_DEI5, 0xB20),
	INTC_VECT(DMAC1B_DADERR, 0xB40),

	INTC_VECT(DMAC0B_DEI4, 0xB80),
	INTC_VECT(DMAC0B_DEI5, 0xBA0),
	INTC_VECT(DMAC0B_DADERR, 0xBC0),

	INTC_VECT(KEYSC_KEYI, 0xBE0),
	INTC_VECT(SCIF_SCIF0, 0xC00),
	INTC_VECT(SCIF_SCIF1, 0xC20),
	INTC_VECT(SCIF_SCIF2, 0xC40),
	INTC_VECT(VEU3F0I, 0xC60),
	INTC_VECT(MSIOF_MSIOFI0, 0xC80),
	INTC_VECT(MSIOF_MSIOFI1, 0xCA0),
	INTC_VECT(SPU_SPUI0, 0xCC0),
	INTC_VECT(SPU_SPUI1, 0xCE0),
	INTC_VECT(SCIFA_SCIFA1, 0xD00),

/*	INTC_VECT(ICB_ICBI, 0xD20), */
	INTC_VECT(ETHI, 0xD60),

	INTC_VECT(I2C1_ALI, 0xD80),
	INTC_VECT(I2C1_TACKI, 0xDA0),
	INTC_VECT(I2C1_WAITI, 0xDC0),
	INTC_VECT(I2C1_DTEI, 0xDE0),

	INTC_VECT(I2C0_ALI, 0xE00),
	INTC_VECT(I2C0_TACKI, 0xE20),
	INTC_VECT(I2C0_WAITI, 0xE40),
	INTC_VECT(I2C0_DTEI, 0xE60),

	INTC_VECT(SDHI0_SDHII0, 0xE80),
	INTC_VECT(SDHI0_SDHII1, 0xEA0),
	INTC_VECT(SDHI0_SDHII2, 0xEC0),

	INTC_VECT(CMT_CMTI, 0xF00),
	INTC_VECT(TSIF_TSIFI, 0xF20),
/*	INTC_VECT(ICB_LMBI, 0xF60), */
	INTC_VECT(FSI_FSI, 0xF80),
	INTC_VECT(SCIFA_SCIFA2, 0xFA0),

	INTC_VECT(TMU0_TUNI0, 0x400),
	INTC_VECT(TMU0_TUNI1, 0x420),
	INTC_VECT(TMU0_TUNI2, 0x440),

	INTC_VECT(IRDA_IRDAI, 0x480),

	INTC_VECT(SDHI1_SDHII0, 0x4E0),
	INTC_VECT(SDHI1_SDHII1, 0x500),
	INTC_VECT(SDHI1_SDHII2, 0x520),

	INTC_VECT(JPU_JPUI, 0x560),

	INTC_VECT(MMC_MMCI0, 0x580),
	INTC_VECT(MMC_MMCI1, 0x5A0),
	INTC_VECT(MMC_MMCI2, 0x5C0),

	INTC_VECT(LCDC_LCDCI, 0xF40),

	INTC_VECT(TMU1_TUNI0, 0x920),
	INTC_VECT(TMU1_TUNI1, 0x940),
	INTC_VECT(TMU1_TUNI2, 0x960),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(DMAC1A, DMAC1A_DEI0, DMAC1A_DEI1, DMAC1A_DEI2, DMAC1A_DEI3),
	INTC_GROUP(_2DG, _2DG_TRI, _2DG_INI, _2DG_CEI, _2DG_BRK),
	INTC_GROUP(DMAC0A, DMAC0A_DEI0, DMAC0A_DEI1, DMAC0A_DEI2, DMAC0A_DEI3),
	INTC_GROUP(VIO, VIO_CEU20I, VIO_BEU20I, VIO_VEU3F1, VIO_VOUI),
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(DMAC1B, DMAC1B_DEI4, DMAC1B_DEI5, DMAC1B_DADERR),
	INTC_GROUP(DMAC0B, DMAC0B_DEI4, DMAC0B_DEI5, DMAC0B_DADERR),
	INTC_GROUP(I2C0, I2C0_ALI, I2C0_TACKI, I2C0_WAITI, I2C0_DTEI),
	INTC_GROUP(I2C1, I2C1_ALI, I2C1_TACKI, I2C1_WAITI, I2C1_DTEI),
	INTC_GROUP(SDHI0, SDHI0_SDHII0, SDHI0_SDHII1, SDHI0_SDHII2),
	INTC_GROUP(SDHI1, SDHI1_SDHII0, SDHI1_SDHII1, SDHI1_SDHII2),
	INTC_GROUP(SPU, SPU_SPUI0, SPU_SPUI1),
	INTC_GROUP(MMC, MMC_MMCI0, MMC_MMCI1, MMC_MMCI2),
};

/* FIXMEEEEEEEEEEEEEEEEEEE !!!!! */
/* very bad manual !! */
static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa4080080, 0xa40800c0, 8, /* IMR0 / IMCR0 */
	  { 0, TMU1_TUNI2, TMU1_TUNI1, TMU1_TUNI0,
	    /*SDHII3?*/0, SDHI1_SDHII2, SDHI1_SDHII1, SDHI1_SDHII0 } },
	{ 0xa4080084, 0xa40800c4, 8, /* IMR1 / IMCR1 */
	  { VIO_VOUI, VIO_VEU3F1, VIO_BEU20I, VIO_CEU20I,
	    DMAC0A_DEI3, DMAC0A_DEI2, DMAC0A_DEI1, DMAC0A_DEI0 } },
	{ 0xa4080088, 0xa40800c8, 8, /* IMR2 / IMCR2 */
	  { 0, 0, 0, VPU_VPUI, ATAPI, ETHI, 0, /*SCIFA3*/SCIFA_SCIFA0 } },
	{ 0xa408008c, 0xa40800cc, 8, /* IMR3 / IMCR3 */
	  { DMAC1A_DEI3, DMAC1A_DEI2, DMAC1A_DEI1, DMAC1A_DEI0,
	    SPU_SPUI1, SPU_SPUI0, BEU21I, IRDA_IRDAI } },
	{ 0xa4080090, 0xa40800d0, 8, /* IMR4 / IMCR4 */
	  { 0, TMU0_TUNI2, TMU0_TUNI1, TMU0_TUNI0,
	    JPU_JPUI, 0, 0, LCDC_LCDCI } },
	{ 0xa4080094, 0xa40800d4, 8, /* IMR5 / IMCR5 */
	  { KEYSC_KEYI, DMAC0B_DADERR, DMAC0B_DEI5, DMAC0B_DEI4,
	    VEU3F0I, SCIF_SCIF2, SCIF_SCIF1, SCIF_SCIF0 } },
	{ 0xa4080098, 0xa40800d8, 8, /* IMR6 / IMCR6 */
	  { 0, 0, /*ICB_ICBI*/0, /*SCIFA4*/SCIFA_SCIFA1,
	    CEU21I, 0, MSIOF_MSIOFI1, MSIOF_MSIOFI0 } },
	{ 0xa408009c, 0xa40800dc, 8, /* IMR7 / IMCR7 */
	  { I2C0_DTEI, I2C0_WAITI, I2C0_TACKI, I2C0_ALI,
	    I2C1_DTEI, I2C1_WAITI, I2C1_TACKI, I2C1_ALI } },
	{ 0xa40800a0, 0xa40800e0, 8, /* IMR8 / IMCR8 */
	  { /*SDHII3*/0, SDHI0_SDHII2, SDHI0_SDHII1, SDHI0_SDHII0,
	    0, 0, /*SCIFA5*/SCIFA_SCIFA2, FSI_FSI } },
	{ 0xa40800a4, 0xa40800e4, 8, /* IMR9 / IMCR9 */
	  { 0, 0, 0, CMT_CMTI, 0, /*USB1*/0, USB_USI0, 0 } },
	{ 0xa40800a8, 0xa40800e8, 8, /* IMR10 / IMCR10 */
	  { 0, DMAC1B_DADERR, DMAC1B_DEI5, DMAC1B_DEI4,
	    0, RTC_ATI, RTC_PRI, RTC_CUI } },
	{ 0xa40800ac, 0xa40800ec, 8, /* IMR11 / IMCR11 */
	  { _2DG_BRK, _2DG_CEI, _2DG_INI, _2DG_TRI,
	    0, TPU_TPUI, /*ICB_LMBI*/0, TSIF_TSIFI } },
	{ 0xa40800b0, 0xa40800f0, 8, /* IMR12 / IMCR12 */
	  { 0, 0, 0, 0, 0, 0, 0, 0/*2DDMAC*/ } },
	{ 0xa4140044, 0xa4140064, 8, /* INTMSK00 / INTMSKCLR00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xa4080000, 0, 16, 4, /* IPRA */ { TMU0_TUNI0, TMU0_TUNI1,
					     TMU0_TUNI2, IRDA_IRDAI } },
	{ 0xa4080004, 0, 16, 4, /* IPRB */ { JPU_JPUI, LCDC_LCDCI,
					     DMAC1A, BEU21I } },
	{ 0xa4080008, 0, 16, 4, /* IPRC */ { TMU1_TUNI0, TMU1_TUNI1,
					     TMU1_TUNI2, SPU } },
	{ 0xa408000c, 0, 16, 4, /* IPRD */ { 0, MMC, 0, ATAPI } },
	{ 0xa4080010, 0, 16, 4, /* IPRE */
	  { DMAC0A, /*BEU?VEU?*/VIO, /*SCIFA3*/SCIFA_SCIFA0, /*VPU5F*/
	    VPU_VPUI } },
	{ 0xa4080014, 0, 16, 4, /* IPRF */ { KEYSC_KEYI, DMAC0B,
					     USB_USI0, CMT_CMTI } },
	{ 0xa4080018, 0, 16, 4, /* IPRG */ { SCIF_SCIF0, SCIF_SCIF1,
					     SCIF_SCIF2, VEU3F0I } },
	{ 0xa408001c, 0, 16, 4, /* IPRH */ { MSIOF_MSIOFI0, MSIOF_MSIOFI1,
					     I2C1, I2C0 } },
	{ 0xa4080020, 0, 16, 4, /* IPRI */ { /*SCIFA4*/SCIFA_SCIFA1, /*ICB*/0,
					     TSIF_TSIFI, _2DG/*ICB?*/ } },
	{ 0xa4080024, 0, 16, 4, /* IPRJ */ { CEU21I, ETHI, FSI_FSI, SDHI1 } },
	{ 0xa4080028, 0, 16, 4, /* IPRK */ { RTC, DMAC1B, /*ICB?*/0, SDHI0 } },
	{ 0xa408002c, 0, 16, 4, /* IPRL */ { /*SCIFA5*/SCIFA_SCIFA2, 0,
					     TPU_TPUI, /*2DDMAC*/0 } },
	{ 0xa4140010, 0, 32, 4, /* INTPRI00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xa414001c, 16, 2, /* ICR1 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_mask_reg ack_registers[] __initdata = {
	{ 0xa4140024, 0, 8, /* INTREQ00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC_ACK(intc_desc, "sh7724", vectors, groups,
			     mask_registers, prio_registers, sense_registers,
			     ack_registers);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}
