/*
 * SH7269 Setup
 *
 * Copyright (C) 2012  Renesas Electronics Europe Ltd
 * Copyright (C) 2012  Phil Edworthy
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/usb/r8a66597.h>
#include <linux/sh_timer.h>
#include <linux/io.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	PINT0, PINT1, PINT2, PINT3, PINT4, PINT5, PINT6, PINT7,

	DMAC0, DMAC1, DMAC2, DMAC3, DMAC4, DMAC5, DMAC6, DMAC7,
	DMAC8, DMAC9, DMAC10, DMAC11, DMAC12, DMAC13, DMAC14, DMAC15,
	USB, VDC4, CMT0, CMT1, BSC, WDT,
	MTU0_ABCD, MTU0_VEF, MTU1_AB, MTU1_VU, MTU2_AB, MTU2_VU,
	MTU3_ABCD, MTU3_TCI3V, MTU4_ABCD, MTU4_TCI4V,
	PWMT1, PWMT2, ADC_ADI,
	SSIF0, SSII1, SSII2, SSII3, SSII4, SSII5,
	RSPDIF,
	IIC30, IIC31, IIC32, IIC33,
	SCIF0_BRI, SCIF0_ERI, SCIF0_RXI, SCIF0_TXI,
	SCIF1_BRI, SCIF1_ERI, SCIF1_RXI, SCIF1_TXI,
	SCIF2_BRI, SCIF2_ERI, SCIF2_RXI, SCIF2_TXI,
	SCIF3_BRI, SCIF3_ERI, SCIF3_RXI, SCIF3_TXI,
	SCIF4_BRI, SCIF4_ERI, SCIF4_RXI, SCIF4_TXI,
	SCIF5_BRI, SCIF5_ERI, SCIF5_RXI, SCIF5_TXI,
	SCIF6_BRI, SCIF6_ERI, SCIF6_RXI, SCIF6_TXI,
	SCIF7_BRI, SCIF7_ERI, SCIF7_RXI, SCIF7_TXI,
	RCAN0, RCAN1, RCAN2,
	RSPIC0, RSPIC1,
	IEBC, CD_ROMD,
	NFMC,
	SDHI0, SDHI1,
	RTC,
	SRCC0, SRCC1, SRCC2,

	/* interrupt groups */
	PINT, SCIF0, SCIF1, SCIF2, SCIF3, SCIF4, SCIF5, SCIF6, SCIF7,
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(IRQ0, 64), INTC_IRQ(IRQ1, 65),
	INTC_IRQ(IRQ2, 66), INTC_IRQ(IRQ3, 67),
	INTC_IRQ(IRQ4, 68), INTC_IRQ(IRQ5, 69),
	INTC_IRQ(IRQ6, 70), INTC_IRQ(IRQ7, 71),

	INTC_IRQ(PINT0, 80), INTC_IRQ(PINT1, 81),
	INTC_IRQ(PINT2, 82), INTC_IRQ(PINT3, 83),
	INTC_IRQ(PINT4, 84), INTC_IRQ(PINT5, 85),
	INTC_IRQ(PINT6, 86), INTC_IRQ(PINT7, 87),

	INTC_IRQ(DMAC0, 108), INTC_IRQ(DMAC0, 109),
	INTC_IRQ(DMAC1, 112), INTC_IRQ(DMAC1, 113),
	INTC_IRQ(DMAC2, 116), INTC_IRQ(DMAC2, 117),
	INTC_IRQ(DMAC3, 120), INTC_IRQ(DMAC3, 121),
	INTC_IRQ(DMAC4, 124), INTC_IRQ(DMAC4, 125),
	INTC_IRQ(DMAC5, 128), INTC_IRQ(DMAC5, 129),
	INTC_IRQ(DMAC6, 132), INTC_IRQ(DMAC6, 133),
	INTC_IRQ(DMAC7, 136), INTC_IRQ(DMAC7, 137),
	INTC_IRQ(DMAC8, 140), INTC_IRQ(DMAC8, 141),
	INTC_IRQ(DMAC9, 144), INTC_IRQ(DMAC9, 145),
	INTC_IRQ(DMAC10, 148), INTC_IRQ(DMAC10, 149),
	INTC_IRQ(DMAC11, 152), INTC_IRQ(DMAC11, 153),
	INTC_IRQ(DMAC12, 156), INTC_IRQ(DMAC12, 157),
	INTC_IRQ(DMAC13, 160), INTC_IRQ(DMAC13, 161),
	INTC_IRQ(DMAC14, 164), INTC_IRQ(DMAC14, 165),
	INTC_IRQ(DMAC15, 168), INTC_IRQ(DMAC15, 169),

	INTC_IRQ(USB, 170),

	INTC_IRQ(VDC4, 171), INTC_IRQ(VDC4, 172),
	INTC_IRQ(VDC4, 173), INTC_IRQ(VDC4, 174),
	INTC_IRQ(VDC4, 175), INTC_IRQ(VDC4, 176),
	INTC_IRQ(VDC4, 177), INTC_IRQ(VDC4, 177),

	INTC_IRQ(CMT0, 188), INTC_IRQ(CMT1, 189),

	INTC_IRQ(BSC, 190), INTC_IRQ(WDT, 191),

	INTC_IRQ(MTU0_ABCD, 192), INTC_IRQ(MTU0_ABCD, 193),
	INTC_IRQ(MTU0_ABCD, 194), INTC_IRQ(MTU0_ABCD, 195),
	INTC_IRQ(MTU0_VEF, 196), INTC_IRQ(MTU0_VEF, 197),
	INTC_IRQ(MTU0_VEF, 198),
	INTC_IRQ(MTU1_AB, 199), INTC_IRQ(MTU1_AB, 200),
	INTC_IRQ(MTU1_VU, 201), INTC_IRQ(MTU1_VU, 202),
	INTC_IRQ(MTU2_AB, 203), INTC_IRQ(MTU2_AB, 204),
	INTC_IRQ(MTU2_VU, 205), INTC_IRQ(MTU2_VU, 206),
	INTC_IRQ(MTU3_ABCD, 207), INTC_IRQ(MTU3_ABCD, 208),
	INTC_IRQ(MTU3_ABCD, 209), INTC_IRQ(MTU3_ABCD, 210),
	INTC_IRQ(MTU3_TCI3V, 211),
	INTC_IRQ(MTU4_ABCD, 212), INTC_IRQ(MTU4_ABCD, 213),
	INTC_IRQ(MTU4_ABCD, 214), INTC_IRQ(MTU4_ABCD, 215),
	INTC_IRQ(MTU4_TCI4V, 216),

	INTC_IRQ(PWMT1, 217), INTC_IRQ(PWMT2, 218),

	INTC_IRQ(ADC_ADI, 223),

	INTC_IRQ(SSIF0, 224), INTC_IRQ(SSIF0, 225),
	INTC_IRQ(SSIF0, 226),
	INTC_IRQ(SSII1, 227), INTC_IRQ(SSII1, 228),
	INTC_IRQ(SSII2, 229), INTC_IRQ(SSII2, 230),
	INTC_IRQ(SSII3, 231), INTC_IRQ(SSII3, 232),
	INTC_IRQ(SSII4, 233), INTC_IRQ(SSII4, 234),
	INTC_IRQ(SSII5, 235), INTC_IRQ(SSII5, 236),

	INTC_IRQ(RSPDIF, 237),

	INTC_IRQ(IIC30, 238), INTC_IRQ(IIC30, 239),
	INTC_IRQ(IIC30, 240), INTC_IRQ(IIC30, 241),
	INTC_IRQ(IIC30, 242),
	INTC_IRQ(IIC31, 243), INTC_IRQ(IIC31, 244),
	INTC_IRQ(IIC31, 245), INTC_IRQ(IIC31, 246),
	INTC_IRQ(IIC31, 247),
	INTC_IRQ(IIC32, 248), INTC_IRQ(IIC32, 249),
	INTC_IRQ(IIC32, 250), INTC_IRQ(IIC32, 251),
	INTC_IRQ(IIC32, 252),
	INTC_IRQ(IIC33, 253), INTC_IRQ(IIC33, 254),
	INTC_IRQ(IIC33, 255), INTC_IRQ(IIC33, 256),
	INTC_IRQ(IIC33, 257),

	INTC_IRQ(SCIF0_BRI, 258), INTC_IRQ(SCIF0_ERI, 259),
	INTC_IRQ(SCIF0_RXI, 260), INTC_IRQ(SCIF0_TXI, 261),
	INTC_IRQ(SCIF1_BRI, 262), INTC_IRQ(SCIF1_ERI, 263),
	INTC_IRQ(SCIF1_RXI, 264), INTC_IRQ(SCIF1_TXI, 265),
	INTC_IRQ(SCIF2_BRI, 266), INTC_IRQ(SCIF2_ERI, 267),
	INTC_IRQ(SCIF2_RXI, 268), INTC_IRQ(SCIF2_TXI, 269),
	INTC_IRQ(SCIF3_BRI, 270), INTC_IRQ(SCIF3_ERI, 271),
	INTC_IRQ(SCIF3_RXI, 272), INTC_IRQ(SCIF3_TXI, 273),
	INTC_IRQ(SCIF4_BRI, 274), INTC_IRQ(SCIF4_ERI, 275),
	INTC_IRQ(SCIF4_RXI, 276), INTC_IRQ(SCIF4_TXI, 277),
	INTC_IRQ(SCIF5_BRI, 278), INTC_IRQ(SCIF5_ERI, 279),
	INTC_IRQ(SCIF5_RXI, 280), INTC_IRQ(SCIF5_TXI, 281),
	INTC_IRQ(SCIF6_BRI, 282), INTC_IRQ(SCIF6_ERI, 283),
	INTC_IRQ(SCIF6_RXI, 284), INTC_IRQ(SCIF6_TXI, 285),
	INTC_IRQ(SCIF7_BRI, 286), INTC_IRQ(SCIF7_ERI, 287),
	INTC_IRQ(SCIF7_RXI, 288), INTC_IRQ(SCIF7_TXI, 289),

	INTC_IRQ(RCAN0, 291), INTC_IRQ(RCAN0, 292),
	INTC_IRQ(RCAN0, 293), INTC_IRQ(RCAN0, 294),
	INTC_IRQ(RCAN0, 295),
	INTC_IRQ(RCAN1, 296), INTC_IRQ(RCAN1, 297),
	INTC_IRQ(RCAN1, 298), INTC_IRQ(RCAN1, 299),
	INTC_IRQ(RCAN1, 300),
	INTC_IRQ(RCAN2, 301), INTC_IRQ(RCAN2, 302),
	INTC_IRQ(RCAN2, 303), INTC_IRQ(RCAN2, 304),
	INTC_IRQ(RCAN2, 305),

	INTC_IRQ(RSPIC0, 306), INTC_IRQ(RSPIC0, 307),
	INTC_IRQ(RSPIC0, 308),
	INTC_IRQ(RSPIC1, 309), INTC_IRQ(RSPIC1, 310),
	INTC_IRQ(RSPIC1, 311),

	INTC_IRQ(IEBC, 318),

	INTC_IRQ(CD_ROMD, 319), INTC_IRQ(CD_ROMD, 320),
	INTC_IRQ(CD_ROMD, 321), INTC_IRQ(CD_ROMD, 322),
	INTC_IRQ(CD_ROMD, 323), INTC_IRQ(CD_ROMD, 324),

	INTC_IRQ(NFMC, 325), INTC_IRQ(NFMC, 326),
	INTC_IRQ(NFMC, 327), INTC_IRQ(NFMC, 328),

	INTC_IRQ(SDHI0, 332), INTC_IRQ(SDHI0, 333),
	INTC_IRQ(SDHI0, 334),
	INTC_IRQ(SDHI1, 335), INTC_IRQ(SDHI1, 336),
	INTC_IRQ(SDHI1, 337),

	INTC_IRQ(RTC, 338), INTC_IRQ(RTC, 339),
	INTC_IRQ(RTC, 340),

	INTC_IRQ(SRCC0, 341), INTC_IRQ(SRCC0, 342),
	INTC_IRQ(SRCC0, 343), INTC_IRQ(SRCC0, 344),
	INTC_IRQ(SRCC0, 345),
	INTC_IRQ(SRCC1, 346), INTC_IRQ(SRCC1, 347),
	INTC_IRQ(SRCC1, 348), INTC_IRQ(SRCC1, 349),
	INTC_IRQ(SRCC1, 350),
	INTC_IRQ(SRCC2, 351), INTC_IRQ(SRCC2, 352),
	INTC_IRQ(SRCC2, 353), INTC_IRQ(SRCC2, 354),
	INTC_IRQ(SRCC2, 355),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(PINT, PINT0, PINT1, PINT2, PINT3,
		   PINT4, PINT5, PINT6, PINT7),
	INTC_GROUP(SCIF0, SCIF0_BRI, SCIF0_ERI, SCIF0_RXI, SCIF0_TXI),
	INTC_GROUP(SCIF1, SCIF1_BRI, SCIF1_ERI, SCIF1_RXI, SCIF1_TXI),
	INTC_GROUP(SCIF2, SCIF2_BRI, SCIF2_ERI, SCIF2_RXI, SCIF2_TXI),
	INTC_GROUP(SCIF3, SCIF3_BRI, SCIF3_ERI, SCIF3_RXI, SCIF3_TXI),
	INTC_GROUP(SCIF4, SCIF4_BRI, SCIF4_ERI, SCIF4_RXI, SCIF4_TXI),
	INTC_GROUP(SCIF5, SCIF5_BRI, SCIF5_ERI, SCIF5_RXI, SCIF5_TXI),
	INTC_GROUP(SCIF6, SCIF6_BRI, SCIF6_ERI, SCIF6_RXI, SCIF6_TXI),
	INTC_GROUP(SCIF7, SCIF7_BRI, SCIF7_ERI, SCIF7_RXI, SCIF7_TXI),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffe0818, 0, 16, 4, /* IPR01 */ { IRQ0, IRQ1, IRQ2, IRQ3 } },
	{ 0xfffe081a, 0, 16, 4, /* IPR02 */ { IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xfffe0820, 0, 16, 4, /* IPR05 */ { PINT, 0, 0, 0 } },
	{ 0xfffe0c00, 0, 16, 4, /* IPR06 */ { DMAC0,  DMAC1, DMAC2,  DMAC3 } },
	{ 0xfffe0c02, 0, 16, 4, /* IPR07 */ { DMAC4,  DMAC5, DMAC6,  DMAC7 } },
	{ 0xfffe0c04, 0, 16, 4, /* IPR08 */ { DMAC8,  DMAC9,
					      DMAC10, DMAC11 } },
	{ 0xfffe0c06, 0, 16, 4, /* IPR09 */ { DMAC12, DMAC13,
					      DMAC14, DMAC15 } },
	{ 0xfffe0c08, 0, 16, 4, /* IPR10 */ { USB, VDC4, VDC4, VDC4 } },
	{ 0xfffe0c0a, 0, 16, 4, /* IPR11 */ { 0, 0, 0, 0 } },
	{ 0xfffe0c0c, 0, 16, 4, /* IPR12 */ { CMT0, CMT1, BSC, WDT } },
	{ 0xfffe0c0e, 0, 16, 4, /* IPR13 */ { MTU0_ABCD, MTU0_VEF,
					      MTU1_AB, MTU1_VU } },
	{ 0xfffe0c10, 0, 16, 4, /* IPR14 */ { MTU2_AB, MTU2_VU,
					      MTU3_ABCD, MTU3_TCI3V } },
	{ 0xfffe0c12, 0, 16, 4, /* IPR15 */ { MTU4_ABCD, MTU4_TCI4V,
					      PWMT1, PWMT2 } },
	{ 0xfffe0c14, 0, 16, 4, /* IPR16 */ { 0, 0, 0, 0 } },
	{ 0xfffe0c16, 0, 16, 4, /* IPR17 */ { ADC_ADI, SSIF0, SSII1, SSII2 } },
	{ 0xfffe0c18, 0, 16, 4, /* IPR18 */ { SSII3, SSII4, SSII5,  RSPDIF} },
	{ 0xfffe0c1a, 0, 16, 4, /* IPR19 */ { IIC30, IIC31, IIC32, IIC33 } },
	{ 0xfffe0c1c, 0, 16, 4, /* IPR20 */ { SCIF0, SCIF1, SCIF2, SCIF3 } },
	{ 0xfffe0c1e, 0, 16, 4, /* IPR21 */ { SCIF4, SCIF5, SCIF6, SCIF7 } },
	{ 0xfffe0c20, 0, 16, 4, /* IPR22 */ { 0, RCAN0, RCAN1, RCAN2 } },
	{ 0xfffe0c22, 0, 16, 4, /* IPR23 */ { RSPIC0, RSPIC1, 0, 0 } },
	{ 0xfffe0c24, 0, 16, 4, /* IPR24 */ { IEBC, CD_ROMD, NFMC, 0 } },
	{ 0xfffe0c26, 0, 16, 4, /* IPR25 */ { SDHI0, SDHI1, RTC, 0 } },
	{ 0xfffe0c28, 0, 16, 4, /* IPR26 */ { SRCC0, SRCC1, SRCC2, 0 } },
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfffe0808, 0, 16, /* PINTER */
	  { 0, 0, 0, 0, 0, 0, 0, 0,
	    PINT7, PINT6, PINT5, PINT4, PINT3, PINT2, PINT1, PINT0 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7269", vectors, groups,
			 mask_registers, prio_registers, NULL);

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe8007000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 259, 260, 261, 258 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xe8007800,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 263, 264, 265, 262 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xe8008000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 267, 268, 269, 266 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xe8008800,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 271, 272, 273, 270 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xe8009000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 275, 276, 277, 274 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xe8009800,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 279, 280, 281, 278 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xe800a000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 283, 284, 285, 282 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

static struct plat_sci_port scif7_platform_data = {
	.mapbase	= 0xe800a800,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE | SCSCR_TOIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		=  { 287, 288, 289, 286 },
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct platform_device scif7_device = {
	.name		= "sh-sci",
	.id		= 7,
	.dev		= {
		.platform_data	= &scif7_platform_data,
	},
};

static struct sh_timer_config cmt0_platform_data = {
	.channel_offset = 0x02,
	.timer_bit = 0,
	.clockevent_rating = 125,
	.clocksource_rating = 0, /* disabled due to code generation issues */
};

static struct resource cmt0_resources[] = {
	[0] = {
		.start	= 0xfffec002,
		.end	= 0xfffec007,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 188,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt0_device = {
	.name		= "sh_cmt",
	.id		= 0,
	.dev = {
		.platform_data	= &cmt0_platform_data,
	},
	.resource	= cmt0_resources,
	.num_resources	= ARRAY_SIZE(cmt0_resources),
};

static struct sh_timer_config cmt1_platform_data = {
	.channel_offset = 0x08,
	.timer_bit = 1,
	.clockevent_rating = 125,
	.clocksource_rating = 0, /* disabled due to code generation issues */
};

static struct resource cmt1_resources[] = {
	[0] = {
		.start	= 0xfffec008,
		.end	= 0xfffec00d,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 189,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt1_device = {
	.name		= "sh_cmt",
	.id		= 1,
	.dev = {
		.platform_data	= &cmt1_platform_data,
	},
	.resource	= cmt1_resources,
	.num_resources	= ARRAY_SIZE(cmt1_resources),
};

static struct sh_timer_config mtu2_0_platform_data = {
	.channel_offset = -0x80,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource mtu2_0_resources[] = {
	[0] = {
		.start	= 0xfffe4300,
		.end	= 0xfffe4326,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 192,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mtu2_0_device = {
	.name		= "sh_mtu2",
	.id		= 0,
	.dev = {
		.platform_data	= &mtu2_0_platform_data,
	},
	.resource	= mtu2_0_resources,
	.num_resources	= ARRAY_SIZE(mtu2_0_resources),
};

static struct sh_timer_config mtu2_1_platform_data = {
	.channel_offset = -0x100,
	.timer_bit = 1,
	.clockevent_rating = 200,
};

static struct resource mtu2_1_resources[] = {
	[0] = {
		.start	= 0xfffe4380,
		.end	= 0xfffe4390,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 203,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mtu2_1_device = {
	.name		= "sh_mtu2",
	.id		= 1,
	.dev = {
		.platform_data	= &mtu2_1_platform_data,
	},
	.resource	= mtu2_1_resources,
	.num_resources	= ARRAY_SIZE(mtu2_1_resources),
};

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xfffe6000,
		.end	= 0xfffe6000 + 0x30 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Shared Period/Carry/Alarm IRQ */
		.start	= 338,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

/* USB Host */
static struct r8a66597_platdata r8a66597_data = {
	.on_chip = 1,
	.endian = 1,
};

static struct resource r8a66597_usb_host_resources[] = {
	[0] = {
		.start	= 0xe8010000,
		.end	= 0xe80100e4,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 170,
		.end	= 170,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device r8a66597_usb_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 0,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &r8a66597_data,
	},
	.num_resources	= ARRAY_SIZE(r8a66597_usb_host_resources),
	.resource	= r8a66597_usb_host_resources,
};

static struct platform_device *sh7269_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&cmt0_device,
	&cmt1_device,
	&mtu2_0_device,
	&mtu2_1_device,
	&rtc_device,
	&r8a66597_usb_host_device,
};

static int __init sh7269_devices_setup(void)
{
	return platform_add_devices(sh7269_devices,
				    ARRAY_SIZE(sh7269_devices));
}
arch_initcall(sh7269_devices_setup);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

static struct platform_device *sh7269_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&cmt0_device,
	&cmt1_device,
	&mtu2_0_device,
	&mtu2_1_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh7269_early_devices,
				   ARRAY_SIZE(sh7269_early_devices));
}
