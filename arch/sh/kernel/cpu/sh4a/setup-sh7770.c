/*
 * SH7770 Setup
 *
 *  Copyright (C) 2006 - 2008  Paul Mundt
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

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xff923000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 61, 61, 61, 61 },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xff924000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 62, 62, 62, 62 },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xff925000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 63, 63, 63, 63 },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xff926000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 64, 64, 64, 64 },
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xff927000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 65, 65, 65, 65 },
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xff928000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 66, 66, 66, 66 },
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xff929000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 67, 67, 67, 67 },
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

static struct plat_sci_port scif7_platform_data = {
	.mapbase	= 0xff92a000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 68, 68, 68, 68 },
};

static struct platform_device scif7_device = {
	.name		= "sh-sci",
	.id		= 7,
	.dev		= {
		.platform_data	= &scif7_platform_data,
	},
};

static struct plat_sci_port scif8_platform_data = {
	.mapbase	= 0xff92b000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 69, 69, 69, 69 },
};

static struct platform_device scif8_device = {
	.name		= "sh-sci",
	.id		= 8,
	.dev		= {
		.platform_data	= &scif8_platform_data,
	},
};

static struct plat_sci_port scif9_platform_data = {
	.mapbase	= 0xff92c000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 70, 70, 70, 70 },
};

static struct platform_device scif9_device = {
	.name		= "sh-sci",
	.id		= 9,
	.dev		= {
		.platform_data	= &scif9_platform_data,
	},
};

static struct sh_timer_config tmu0_platform_data = {
	.name = "TMU0",
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clk = "peripheral_clk",
	.clockevent_rating = 200,
};

static struct resource tmu0_resources[] = {
	[0] = {
		.name	= "TMU0",
		.start	= 0xffd80008,
		.end	= 0xffd80013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 16,
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
	.name = "TMU1",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clk = "peripheral_clk",
	.clocksource_rating = 200,
};

static struct resource tmu1_resources[] = {
	[0] = {
		.name	= "TMU1",
		.start	= 0xffd80014,
		.end	= 0xffd8001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 17,
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
	.name = "TMU2",
	.channel_offset = 0x1c,
	.timer_bit = 2,
	.clk = "peripheral_clk",
};

static struct resource tmu2_resources[] = {
	[0] = {
		.name	= "TMU2",
		.start	= 0xffd80020,
		.end	= 0xffd8002f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 18,
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

static struct sh_timer_config tmu3_platform_data = {
	.name = "TMU3",
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clk = "peripheral_clk",
};

static struct resource tmu3_resources[] = {
	[0] = {
		.name	= "TMU3",
		.start	= 0xffd81008,
		.end	= 0xffd81013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 19,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu3_device = {
	.name		= "sh_tmu",
	.id		= 3,
	.dev = {
		.platform_data	= &tmu3_platform_data,
	},
	.resource	= tmu3_resources,
	.num_resources	= ARRAY_SIZE(tmu3_resources),
};

static struct sh_timer_config tmu4_platform_data = {
	.name = "TMU4",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clk = "peripheral_clk",
};

static struct resource tmu4_resources[] = {
	[0] = {
		.name	= "TMU4",
		.start	= 0xffd81014,
		.end	= 0xffd8101f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 20,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu4_device = {
	.name		= "sh_tmu",
	.id		= 4,
	.dev = {
		.platform_data	= &tmu4_platform_data,
	},
	.resource	= tmu4_resources,
	.num_resources	= ARRAY_SIZE(tmu4_resources),
};

static struct sh_timer_config tmu5_platform_data = {
	.name = "TMU5",
	.channel_offset = 0x1c,
	.timer_bit = 2,
	.clk = "peripheral_clk",
};

static struct resource tmu5_resources[] = {
	[0] = {
		.name	= "TMU5",
		.start	= 0xffd81020,
		.end	= 0xffd8102f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 21,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu5_device = {
	.name		= "sh_tmu",
	.id		= 5,
	.dev = {
		.platform_data	= &tmu5_platform_data,
	},
	.resource	= tmu5_resources,
	.num_resources	= ARRAY_SIZE(tmu5_resources),
};

static struct sh_timer_config tmu6_platform_data = {
	.name = "TMU6",
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clk = "peripheral_clk",
};

static struct resource tmu6_resources[] = {
	[0] = {
		.name	= "TMU6",
		.start	= 0xffd82008,
		.end	= 0xffd82013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 22,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu6_device = {
	.name		= "sh_tmu",
	.id		= 6,
	.dev = {
		.platform_data	= &tmu6_platform_data,
	},
	.resource	= tmu6_resources,
	.num_resources	= ARRAY_SIZE(tmu6_resources),
};

static struct sh_timer_config tmu7_platform_data = {
	.name = "TMU7",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clk = "peripheral_clk",
};

static struct resource tmu7_resources[] = {
	[0] = {
		.name	= "TMU7",
		.start	= 0xffd82014,
		.end	= 0xffd8201f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 23,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu7_device = {
	.name		= "sh_tmu",
	.id		= 7,
	.dev = {
		.platform_data	= &tmu7_platform_data,
	},
	.resource	= tmu7_resources,
	.num_resources	= ARRAY_SIZE(tmu7_resources),
};

static struct sh_timer_config tmu8_platform_data = {
	.name = "TMU8",
	.channel_offset = 0x1c,
	.timer_bit = 2,
	.clk = "peripheral_clk",
};

static struct resource tmu8_resources[] = {
	[0] = {
		.name	= "TMU8",
		.start	= 0xffd82020,
		.end	= 0xffd8202b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 24,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu8_device = {
	.name		= "sh_tmu",
	.id		= 8,
	.dev = {
		.platform_data	= &tmu8_platform_data,
	},
	.resource	= tmu8_resources,
	.num_resources	= ARRAY_SIZE(tmu8_resources),
};

static struct platform_device *sh7770_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&scif8_device,
	&scif9_device,
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

static int __init sh7770_devices_setup(void)
{
	return platform_add_devices(sh7770_devices,
				    ARRAY_SIZE(sh7770_devices));
}
arch_initcall(sh7770_devices_setup);

static struct platform_device *sh7770_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&scif8_device,
	&scif9_device,
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
	early_platform_add_devices(sh7770_early_devices,
				   ARRAY_SIZE(sh7770_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	IRL_HHLL, IRL_HHLH, IRL_HHHL,

	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,

	GPIO,
	TMU0, TMU1, TMU2, TMU2_TICPI,
	TMU3, TMU4, TMU5, TMU5_TICPI,
	TMU6, TMU7, TMU8,
	HAC, IPI, SPDIF, HUDI, I2C,
	DMAC0_DMINT0, DMAC0_DMINT1, DMAC0_DMINT2,
	I2S0, I2S1, I2S2, I2S3,
	SRC_RX, SRC_TX, SRC_SPDIF,
	DU, VIDEO_IN, REMOTE, YUV, USB, ATAPI, CAN, GPS, GFX2D,
	GFX3D_MBX, GFX3D_DMAC,
	EXBUS_ATA,
	SPI0, SPI1,
	SCIF089, SCIF1234, SCIF567,
	ADC,
	BBDMAC_0_3, BBDMAC_4_7, BBDMAC_8_10, BBDMAC_11_14,
	BBDMAC_15_18, BBDMAC_19_22, BBDMAC_23_26, BBDMAC_27,
	BBDMAC_28, BBDMAC_29, BBDMAC_30, BBDMAC_31,

	/* interrupt groups */
	TMU, DMAC, I2S, SRC, GFX3D, SPI, SCIF, BBDMAC,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(GPIO, 0x3e0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2_TICPI, 0x460),
	INTC_VECT(TMU3, 0x480), INTC_VECT(TMU4, 0x4a0),
	INTC_VECT(TMU5, 0x4c0), INTC_VECT(TMU5_TICPI, 0x4e0),
	INTC_VECT(TMU6, 0x500), INTC_VECT(TMU7, 0x520),
	INTC_VECT(TMU8, 0x540),
	INTC_VECT(HAC, 0x580), INTC_VECT(IPI, 0x5c0),
	INTC_VECT(SPDIF, 0x5e0),
	INTC_VECT(HUDI, 0x600), INTC_VECT(I2C, 0x620),
	INTC_VECT(DMAC0_DMINT0, 0x640), INTC_VECT(DMAC0_DMINT1, 0x660),
	INTC_VECT(DMAC0_DMINT2, 0x680),
	INTC_VECT(I2S0, 0x6a0), INTC_VECT(I2S1, 0x6c0),
	INTC_VECT(I2S2, 0x6e0), INTC_VECT(I2S3, 0x700),
	INTC_VECT(SRC_RX, 0x720), INTC_VECT(SRC_TX, 0x740),
	INTC_VECT(SRC_SPDIF, 0x760),
	INTC_VECT(DU, 0x780), INTC_VECT(VIDEO_IN, 0x7a0),
	INTC_VECT(REMOTE, 0x7c0), INTC_VECT(YUV, 0x7e0),
	INTC_VECT(USB, 0x840), INTC_VECT(ATAPI, 0x860),
	INTC_VECT(CAN, 0x880), INTC_VECT(GPS, 0x8a0),
	INTC_VECT(GFX2D, 0x8c0),
	INTC_VECT(GFX3D_MBX, 0x900), INTC_VECT(GFX3D_DMAC, 0x920),
	INTC_VECT(EXBUS_ATA, 0x940),
	INTC_VECT(SPI0, 0x960), INTC_VECT(SPI1, 0x980),
	INTC_VECT(SCIF089, 0x9a0), INTC_VECT(SCIF1234, 0x9c0),
	INTC_VECT(SCIF1234, 0x9e0), INTC_VECT(SCIF1234, 0xa00),
	INTC_VECT(SCIF1234, 0xa20), INTC_VECT(SCIF567, 0xa40),
	INTC_VECT(SCIF567, 0xa60), INTC_VECT(SCIF567, 0xa80),
	INTC_VECT(SCIF089, 0xaa0), INTC_VECT(SCIF089, 0xac0),
	INTC_VECT(ADC, 0xb20),
	INTC_VECT(BBDMAC_0_3, 0xba0), INTC_VECT(BBDMAC_0_3, 0xbc0),
	INTC_VECT(BBDMAC_0_3, 0xbe0), INTC_VECT(BBDMAC_0_3, 0xc00),
	INTC_VECT(BBDMAC_4_7, 0xc20), INTC_VECT(BBDMAC_4_7, 0xc40),
	INTC_VECT(BBDMAC_4_7, 0xc60), INTC_VECT(BBDMAC_4_7, 0xc80),
	INTC_VECT(BBDMAC_8_10, 0xca0), INTC_VECT(BBDMAC_8_10, 0xcc0),
	INTC_VECT(BBDMAC_8_10, 0xce0), INTC_VECT(BBDMAC_11_14, 0xd00),
	INTC_VECT(BBDMAC_11_14, 0xd20), INTC_VECT(BBDMAC_11_14, 0xd40),
	INTC_VECT(BBDMAC_11_14, 0xd60), INTC_VECT(BBDMAC_15_18, 0xd80),
	INTC_VECT(BBDMAC_15_18, 0xda0), INTC_VECT(BBDMAC_15_18, 0xdc0),
	INTC_VECT(BBDMAC_15_18, 0xde0), INTC_VECT(BBDMAC_19_22, 0xe00),
	INTC_VECT(BBDMAC_19_22, 0xe20), INTC_VECT(BBDMAC_19_22, 0xe40),
	INTC_VECT(BBDMAC_19_22, 0xe60), INTC_VECT(BBDMAC_23_26, 0xe80),
	INTC_VECT(BBDMAC_23_26, 0xea0), INTC_VECT(BBDMAC_23_26, 0xec0),
	INTC_VECT(BBDMAC_23_26, 0xee0), INTC_VECT(BBDMAC_27, 0xf00),
	INTC_VECT(BBDMAC_28, 0xf20), INTC_VECT(BBDMAC_29, 0xf40),
	INTC_VECT(BBDMAC_30, 0xf60), INTC_VECT(BBDMAC_31, 0xf80),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(TMU, TMU0, TMU1, TMU2, TMU2_TICPI, TMU3, TMU4, TMU5,
		   TMU5_TICPI, TMU6, TMU7, TMU8),
	INTC_GROUP(DMAC, DMAC0_DMINT0, DMAC0_DMINT1, DMAC0_DMINT2),
	INTC_GROUP(I2S, I2S0, I2S1, I2S2, I2S3),
	INTC_GROUP(SRC, SRC_RX, SRC_TX, SRC_SPDIF),
	INTC_GROUP(GFX3D, GFX3D_MBX, GFX3D_DMAC),
	INTC_GROUP(SPI, SPI0, SPI1),
	INTC_GROUP(SCIF, SCIF089, SCIF1234, SCIF567),
	INTC_GROUP(BBDMAC,
		   BBDMAC_0_3, BBDMAC_4_7, BBDMAC_8_10, BBDMAC_11_14,
		   BBDMAC_15_18, BBDMAC_19_22, BBDMAC_23_26, BBDMAC_27,
		   BBDMAC_28, BBDMAC_29, BBDMAC_30, BBDMAC_31),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xffe00040, 0xffe00044, 32, /* INT2MSKR / INT2MSKCR */
	  { 0, BBDMAC, ADC, SCIF, SPI, EXBUS_ATA, GFX3D, GFX2D,
	    GPS, CAN, ATAPI, USB, YUV, REMOTE, VIDEO_IN, DU, SRC, I2S,
	    DMAC, I2C, HUDI, SPDIF, IPI, HAC, TMU, GPIO } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffe00000, 0, 32, 8, /* INT2PRI0 */ { GPIO, TMU0, 0, HAC } },
	{ 0xffe00004, 0, 32, 8, /* INT2PRI1 */ { IPI, SPDIF, HUDI, I2C } },
	{ 0xffe00008, 0, 32, 8, /* INT2PRI2 */ { DMAC, I2S, SRC, DU } },
	{ 0xffe0000c, 0, 32, 8, /* INT2PRI3 */ { VIDEO_IN, REMOTE, YUV, USB } },
	{ 0xffe00010, 0, 32, 8, /* INT2PRI4 */ { ATAPI, CAN, GPS, GFX2D } },
	{ 0xffe00014, 0, 32, 8, /* INT2PRI5 */ { 0, GFX3D, EXBUS_ATA, SPI } },
	{ 0xffe00018, 0, 32, 8, /* INT2PRI6 */ { SCIF1234, SCIF567, SCIF089 } },
	{ 0xffe0001c, 0, 32, 8, /* INT2PRI7 */ { ADC, 0, 0, BBDMAC_0_3 } },
	{ 0xffe00020, 0, 32, 8, /* INT2PRI8 */
	  { BBDMAC_4_7, BBDMAC_8_10, BBDMAC_11_14, BBDMAC_15_18 } },
	{ 0xffe00024, 0, 32, 8, /* INT2PRI9 */
	  { BBDMAC_19_22, BBDMAC_23_26, BBDMAC_27, BBDMAC_28 } },
	{ 0xffe00028, 0, 32, 8, /* INT2PRI10 */
	  { BBDMAC_29, BBDMAC_30, BBDMAC_31 } },
	{ 0xffe0002c, 0, 32, 8, /* INT2PRI11 */
	  { TMU1, TMU2, TMU2_TICPI, TMU3 } },
	{ 0xffe00030, 0, 32, 8, /* INT2PRI12 */
	  { TMU4, TMU5, TMU5_TICPI, TMU6 } },
	{ 0xffe00034, 0, 32, 8, /* INT2PRI13 */
	  { TMU7, TMU8 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7770", vectors, groups,
			 mask_registers, prio_registers, NULL);

/* Support for external interrupt pins in IRQ mode */
static struct intc_vect irq_vectors[] __initdata = {
	INTC_VECT(IRQ0, 0x240), INTC_VECT(IRQ1, 0x280),
	INTC_VECT(IRQ2, 0x2c0), INTC_VECT(IRQ3, 0x300),
	INTC_VECT(IRQ4, 0x340), INTC_VECT(IRQ5, 0x380),
};

static struct intc_mask_reg irq_mask_registers[] __initdata = {
	{ 0xffd00044, 0xffd00064, 32, /* INTMSK0 / INTMSKCLR0 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, } },
};

static struct intc_prio_reg irq_prio_registers[] __initdata = {
	{ 0xffd00010, 0, 32, 4, /* INTPRI */ { IRQ0, IRQ1, IRQ2, IRQ3,
					       IRQ4, IRQ5, } },
};

static struct intc_sense_reg irq_sense_registers[] __initdata = {
	{ 0xffd0001c, 32, 2, /* ICR1 */   { IRQ0, IRQ1, IRQ2, IRQ3,
					    IRQ4, IRQ5, } },
};

static DECLARE_INTC_DESC(intc_irq_desc, "sh7770-irq", irq_vectors,
			 NULL, irq_mask_registers, irq_prio_registers,
			 irq_sense_registers);

/* External interrupt pins in IRL mode */
static struct intc_vect irl_vectors[] __initdata = {
	INTC_VECT(IRL_LLLL, 0x200), INTC_VECT(IRL_LLLH, 0x220),
	INTC_VECT(IRL_LLHL, 0x240), INTC_VECT(IRL_LLHH, 0x260),
	INTC_VECT(IRL_LHLL, 0x280), INTC_VECT(IRL_LHLH, 0x2a0),
	INTC_VECT(IRL_LHHL, 0x2c0), INTC_VECT(IRL_LHHH, 0x2e0),
	INTC_VECT(IRL_HLLL, 0x300), INTC_VECT(IRL_HLLH, 0x320),
	INTC_VECT(IRL_HLHL, 0x340), INTC_VECT(IRL_HLHH, 0x360),
	INTC_VECT(IRL_HHLL, 0x380), INTC_VECT(IRL_HHLH, 0x3a0),
	INTC_VECT(IRL_HHHL, 0x3c0),
};

static struct intc_mask_reg irl3210_mask_registers[] __initdata = {
	{ 0xffd40080, 0xffd40084, 32, /* INTMSK2 / INTMSKCLR2 */
	  { IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	    IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	    IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	    IRL_HHLL, IRL_HHLH, IRL_HHHL, } },
};

static struct intc_mask_reg irl7654_mask_registers[] __initdata = {
	{ 0xffd40080, 0xffd40084, 32, /* INTMSK2 / INTMSKCLR2 */
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	    IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	    IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	    IRL_HHLL, IRL_HHLH, IRL_HHHL, } },
};

static DECLARE_INTC_DESC(intc_irl7654_desc, "sh7780-irl7654", irl_vectors,
			 NULL, irl7654_mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_irl3210_desc, "sh7780-irl3210", irl_vectors,
			 NULL, irl3210_mask_registers, NULL, NULL);

#define INTC_ICR0	0xffd00000
#define INTC_INTMSK0	0xffd00044
#define INTC_INTMSK1	0xffd00048
#define INTC_INTMSK2	0xffd40080
#define INTC_INTMSKCLR1	0xffd00068
#define INTC_INTMSKCLR2	0xffd40084

void __init plat_irq_setup(void)
{
	/* disable IRQ7-0 */
	ctrl_outl(0xff000000, INTC_INTMSK0);

	/* disable IRL3-0 + IRL7-4 */
	ctrl_outl(0xc0000000, INTC_INTMSK1);
	ctrl_outl(0xfffefffe, INTC_INTMSK2);

	/* select IRL mode for IRL3-0 + IRL7-4 */
	ctrl_outl(ctrl_inl(INTC_ICR0) & ~0x00c00000, INTC_ICR0);

	/* disable holding function, ie enable "SH-4 Mode" */
	ctrl_outl(ctrl_inl(INTC_ICR0) | 0x00200000, INTC_ICR0);

	register_intc_controller(&intc_desc);
}

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ:
		/* select IRQ mode for IRL3-0 + IRL7-4 */
		ctrl_outl(ctrl_inl(INTC_ICR0) | 0x00c00000, INTC_ICR0);
		register_intc_controller(&intc_irq_desc);
		break;
	case IRQ_MODE_IRL7654:
		/* enable IRL7-4 but don't provide any masking */
		ctrl_outl(0x40000000, INTC_INTMSKCLR1);
		ctrl_outl(0x0000fffe, INTC_INTMSKCLR2);
		break;
	case IRQ_MODE_IRL3210:
		/* enable IRL0-3 but don't provide any masking */
		ctrl_outl(0x80000000, INTC_INTMSKCLR1);
		ctrl_outl(0xfffe0000, INTC_INTMSKCLR2);
		break;
	case IRQ_MODE_IRL7654_MASK:
		/* enable IRL7-4 and mask using cpu intc controller */
		ctrl_outl(0x40000000, INTC_INTMSKCLR1);
		register_intc_controller(&intc_irl7654_desc);
		break;
	case IRQ_MODE_IRL3210_MASK:
		/* enable IRL0-3 and mask using cpu intc controller */
		ctrl_outl(0x80000000, INTC_INTMSKCLR1);
		register_intc_controller(&intc_irl3210_desc);
		break;
	default:
		BUG();
	}
}
