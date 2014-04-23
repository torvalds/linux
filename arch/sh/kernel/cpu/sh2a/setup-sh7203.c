/*
 * SH7203 and SH7263 Setup
 *
 *  Copyright (C) 2007 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <linux/io.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	PINT0, PINT1, PINT2, PINT3, PINT4, PINT5, PINT6, PINT7,
	DMAC0, DMAC1, DMAC2, DMAC3, DMAC4, DMAC5, DMAC6, DMAC7,
	USB, LCDC, CMT0, CMT1, BSC, WDT,

	MTU0_ABCD, MTU0_VEF, MTU1_AB, MTU1_VU, MTU2_AB, MTU2_VU,
	MTU3_ABCD, MTU4_ABCD, MTU2_TCI3V, MTU2_TCI4V,

	ADC_ADI,

	IIC30, IIC31, IIC32, IIC33,
	SCIF0, SCIF1, SCIF2, SCIF3,

	SSU0, SSU1,

	SSI0_SSII, SSI1_SSII, SSI2_SSII, SSI3_SSII,

	/* ROM-DEC, SDHI, SRC, and IEB are SH7263 specific */
	ROMDEC, FLCTL, SDHI, RTC, RCAN0, RCAN1,
	SRC, IEBI,

	/* interrupt groups */
	PINT,
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
	INTC_IRQ(USB, 140), INTC_IRQ(LCDC, 141),
	INTC_IRQ(CMT0, 142), INTC_IRQ(CMT1, 143),
	INTC_IRQ(BSC, 144), INTC_IRQ(WDT, 145),
	INTC_IRQ(MTU0_ABCD, 146), INTC_IRQ(MTU0_ABCD, 147),
	INTC_IRQ(MTU0_ABCD, 148), INTC_IRQ(MTU0_ABCD, 149),
	INTC_IRQ(MTU0_VEF, 150),
	INTC_IRQ(MTU0_VEF, 151), INTC_IRQ(MTU0_VEF, 152),
	INTC_IRQ(MTU1_AB, 153), INTC_IRQ(MTU1_AB, 154),
	INTC_IRQ(MTU1_VU, 155), INTC_IRQ(MTU1_VU, 156),
	INTC_IRQ(MTU2_AB, 157), INTC_IRQ(MTU2_AB, 158),
	INTC_IRQ(MTU2_VU, 159), INTC_IRQ(MTU2_VU, 160),
	INTC_IRQ(MTU3_ABCD, 161), INTC_IRQ(MTU3_ABCD, 162),
	INTC_IRQ(MTU3_ABCD, 163), INTC_IRQ(MTU3_ABCD, 164),
	INTC_IRQ(MTU2_TCI3V, 165),
	INTC_IRQ(MTU4_ABCD, 166), INTC_IRQ(MTU4_ABCD, 167),
	INTC_IRQ(MTU4_ABCD, 168), INTC_IRQ(MTU4_ABCD, 169),
	INTC_IRQ(MTU2_TCI4V, 170),
	INTC_IRQ(ADC_ADI, 171),
	INTC_IRQ(IIC30, 172), INTC_IRQ(IIC30, 173),
	INTC_IRQ(IIC30, 174), INTC_IRQ(IIC30, 175),
	INTC_IRQ(IIC30, 176),
	INTC_IRQ(IIC31, 177), INTC_IRQ(IIC31, 178),
	INTC_IRQ(IIC31, 179), INTC_IRQ(IIC31, 180),
	INTC_IRQ(IIC31, 181),
	INTC_IRQ(IIC32, 182), INTC_IRQ(IIC32, 183),
	INTC_IRQ(IIC32, 184), INTC_IRQ(IIC32, 185),
	INTC_IRQ(IIC32, 186),
	INTC_IRQ(IIC33, 187), INTC_IRQ(IIC33, 188),
	INTC_IRQ(IIC33, 189), INTC_IRQ(IIC33, 190),
	INTC_IRQ(IIC33, 191),
	INTC_IRQ(SCIF0, 192), INTC_IRQ(SCIF0, 193),
	INTC_IRQ(SCIF0, 194), INTC_IRQ(SCIF0, 195),
	INTC_IRQ(SCIF1, 196), INTC_IRQ(SCIF1, 197),
	INTC_IRQ(SCIF1, 198), INTC_IRQ(SCIF1, 199),
	INTC_IRQ(SCIF2, 200), INTC_IRQ(SCIF2, 201),
	INTC_IRQ(SCIF2, 202), INTC_IRQ(SCIF2, 203),
	INTC_IRQ(SCIF3, 204), INTC_IRQ(SCIF3, 205),
	INTC_IRQ(SCIF3, 206), INTC_IRQ(SCIF3, 207),
	INTC_IRQ(SSU0, 208), INTC_IRQ(SSU0, 209),
	INTC_IRQ(SSU0, 210),
	INTC_IRQ(SSU1, 211), INTC_IRQ(SSU1, 212),
	INTC_IRQ(SSU1, 213),
	INTC_IRQ(SSI0_SSII, 214), INTC_IRQ(SSI1_SSII, 215),
	INTC_IRQ(SSI2_SSII, 216), INTC_IRQ(SSI3_SSII, 217),
	INTC_IRQ(FLCTL, 224), INTC_IRQ(FLCTL, 225),
	INTC_IRQ(FLCTL, 226), INTC_IRQ(FLCTL, 227),
	INTC_IRQ(RTC, 231), INTC_IRQ(RTC, 232),
	INTC_IRQ(RTC, 233),
	INTC_IRQ(RCAN0, 234), INTC_IRQ(RCAN0, 235),
	INTC_IRQ(RCAN0, 236), INTC_IRQ(RCAN0, 237),
	INTC_IRQ(RCAN0, 238),
	INTC_IRQ(RCAN1, 239), INTC_IRQ(RCAN1, 240),
	INTC_IRQ(RCAN1, 241), INTC_IRQ(RCAN1, 242),
	INTC_IRQ(RCAN1, 243),

	/* SH7263-specific trash */
#ifdef CONFIG_CPU_SUBTYPE_SH7263
	INTC_IRQ(ROMDEC, 218), INTC_IRQ(ROMDEC, 219),
	INTC_IRQ(ROMDEC, 220), INTC_IRQ(ROMDEC, 221),
	INTC_IRQ(ROMDEC, 222), INTC_IRQ(ROMDEC, 223),

	INTC_IRQ(SDHI, 228), INTC_IRQ(SDHI, 229),
	INTC_IRQ(SDHI, 230),

	INTC_IRQ(SRC, 244), INTC_IRQ(SRC, 245),
	INTC_IRQ(SRC, 246),

	INTC_IRQ(IEBI, 247),
#endif
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(PINT, PINT0, PINT1, PINT2, PINT3,
		   PINT4, PINT5, PINT6, PINT7),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffe0818, 0, 16, 4, /* IPR01 */ { IRQ0, IRQ1, IRQ2, IRQ3 } },
	{ 0xfffe081a, 0, 16, 4, /* IPR02 */ { IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xfffe0820, 0, 16, 4, /* IPR05 */ { PINT, 0, 0, 0 } },
	{ 0xfffe0c00, 0, 16, 4, /* IPR06 */ { DMAC0, DMAC1, DMAC2, DMAC3 } },
	{ 0xfffe0c02, 0, 16, 4, /* IPR07 */ { DMAC4, DMAC5, DMAC6, DMAC7 } },
	{ 0xfffe0c04, 0, 16, 4, /* IPR08 */ { USB, LCDC, CMT0, CMT1 } },
	{ 0xfffe0c06, 0, 16, 4, /* IPR09 */ { BSC, WDT, MTU0_ABCD, MTU0_VEF } },
	{ 0xfffe0c08, 0, 16, 4, /* IPR10 */ { MTU1_AB, MTU1_VU, MTU2_AB,
					      MTU2_VU } },
	{ 0xfffe0c0a, 0, 16, 4, /* IPR11 */ { MTU3_ABCD, MTU2_TCI3V, MTU4_ABCD,
					      MTU2_TCI4V } },
	{ 0xfffe0c0c, 0, 16, 4, /* IPR12 */ { ADC_ADI, IIC30, IIC31, IIC32 } },
	{ 0xfffe0c0e, 0, 16, 4, /* IPR13 */ { IIC33, SCIF0, SCIF1, SCIF2 } },
	{ 0xfffe0c10, 0, 16, 4, /* IPR14 */ { SCIF3, SSU0, SSU1, SSI0_SSII } },
#ifdef CONFIG_CPU_SUBTYPE_SH7203
	{ 0xfffe0c12, 0, 16, 4, /* IPR15 */ { SSI1_SSII, SSI2_SSII,
					      SSI3_SSII, 0 } },
	{ 0xfffe0c14, 0, 16, 4, /* IPR16 */ { FLCTL, 0, RTC, RCAN0 } },
	{ 0xfffe0c16, 0, 16, 4, /* IPR17 */ { RCAN1, 0, 0, 0 } },
#else
	{ 0xfffe0c12, 0, 16, 4, /* IPR15 */ { SSI1_SSII, SSI2_SSII,
					      SSI3_SSII, ROMDEC } },
	{ 0xfffe0c14, 0, 16, 4, /* IPR16 */ { FLCTL, SDHI, RTC, RCAN0 } },
	{ 0xfffe0c16, 0, 16, 4, /* IPR17 */ { RCAN1, SRC, IEBI, 0 } },
#endif
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfffe0808, 0, 16, /* PINTER */
	  { 0, 0, 0, 0, 0, 0, 0, 0,
	    PINT7, PINT6, PINT5, PINT4, PINT3, PINT2, PINT1, PINT0 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7203", vectors, groups,
			 mask_registers, prio_registers, NULL);

static struct plat_sci_port scif0_platform_data = {
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xfffe8000, 0x100),
	DEFINE_RES_IRQ(192),
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

static struct plat_sci_port scif1_platform_data = {
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xfffe8800, 0x100),
	DEFINE_RES_IRQ(196),
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

static struct plat_sci_port scif2_platform_data = {
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif2_resources[] = {
	DEFINE_RES_MEM(0xfffe9000, 0x100),
	DEFINE_RES_IRQ(200),
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

static struct plat_sci_port scif3_platform_data = {
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |
			  SCSCR_REIE,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif3_resources[] = {
	DEFINE_RES_MEM(0xfffe9800, 0x100),
	DEFINE_RES_IRQ(204),
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.resource	= scif3_resources,
	.num_resources	= ARRAY_SIZE(scif3_resources),
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct sh_timer_config cmt_platform_data = {
	.channels_mask = 3,
};

static struct resource cmt_resources[] = {
	DEFINE_RES_MEM(0xfffec000, 0x10),
	DEFINE_RES_IRQ(142),
	DEFINE_RES_IRQ(143),
};

static struct platform_device cmt_device = {
	.name		= "sh-cmt-16",
	.id		= 0,
	.dev = {
		.platform_data	= &cmt_platform_data,
	},
	.resource	= cmt_resources,
	.num_resources	= ARRAY_SIZE(cmt_resources),
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
		.start	= 146,
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
		.start	= 153,
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
		.start	= 0xffff2000,
		.end	= 0xffff2000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Shared Period/Carry/Alarm IRQ */
		.start	= 231,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct platform_device *sh7203_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&cmt_device,
	&mtu2_0_device,
	&mtu2_1_device,
	&rtc_device,
};

static int __init sh7203_devices_setup(void)
{
	return platform_add_devices(sh7203_devices,
				    ARRAY_SIZE(sh7203_devices));
}
arch_initcall(sh7203_devices_setup);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

static struct platform_device *sh7203_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&cmt_device,
	&mtu2_0_device,
	&mtu2_1_device,
};

#define STBCR3 0xfffe0408
#define STBCR4 0xfffe040c

void __init plat_early_device_setup(void)
{
	/* enable CMT clock */
	__raw_writeb(__raw_readb(STBCR4) & ~0x04, STBCR4);

	/* enable MTU2 clock */
	__raw_writeb(__raw_readb(STBCR3) & ~0x20, STBCR3);

	early_platform_add_devices(sh7203_early_devices,
				   ARRAY_SIZE(sh7203_early_devices));
}
