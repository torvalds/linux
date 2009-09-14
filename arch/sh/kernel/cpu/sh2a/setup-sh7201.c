/*
 *  SH7201 setup
 *
 *  Copyright (C) 2008  Peter Griffin pgriffin@mpc-data.co.uk
 *  Copyright (C) 2009  Paul Mundt
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

	ADC_ADI,

	MTU20_ABCD, MTU20_VEF, MTU21_AB, MTU21_VU, MTU22_AB, MTU22_VU,
	MTU23_ABCD, MTU24_ABCD, MTU25_UVW, MTU2_TCI3V, MTU2_TCI4V,

	RTC, WDT,

	IIC30, IIC31, IIC32,

	DMAC0_DMINT0, DMAC1_DMINT1,
	DMAC2_DMINT2, DMAC3_DMINT3,

	SCIF0, SCIF1, SCIF2, SCIF3, SCIF4, SCIF5, SCIF6, SCIF7,

	DMAC0_DMINTA, DMAC4_DMINT4, DMAC5_DMINT5, DMAC6_DMINT6,
	DMAC7_DMINT7,

	RCAN0, RCAN1,

	SSI0_SSII, SSI1_SSII,

	TMR0, TMR1,

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

	INTC_IRQ(ADC_ADI, 92),

	INTC_IRQ(MTU20_ABCD, 108), INTC_IRQ(MTU20_ABCD, 109),
	INTC_IRQ(MTU20_ABCD, 110), INTC_IRQ(MTU20_ABCD, 111),

	INTC_IRQ(MTU20_VEF, 112), INTC_IRQ(MTU20_VEF, 113),
	INTC_IRQ(MTU20_VEF, 114),

	INTC_IRQ(MTU21_AB, 116), INTC_IRQ(MTU21_AB, 117),
	INTC_IRQ(MTU21_VU, 120), INTC_IRQ(MTU21_VU, 121),

	INTC_IRQ(MTU22_AB, 124), INTC_IRQ(MTU22_AB, 125),
	INTC_IRQ(MTU22_VU, 128), INTC_IRQ(MTU22_VU, 129),

	INTC_IRQ(MTU23_ABCD, 132), INTC_IRQ(MTU23_ABCD, 133),
	INTC_IRQ(MTU23_ABCD, 134), INTC_IRQ(MTU23_ABCD, 135),

	INTC_IRQ(MTU2_TCI3V, 136),

	INTC_IRQ(MTU24_ABCD, 140), INTC_IRQ(MTU24_ABCD, 141),
	INTC_IRQ(MTU24_ABCD, 142), INTC_IRQ(MTU24_ABCD, 143),

	INTC_IRQ(MTU2_TCI4V, 144),

	INTC_IRQ(MTU25_UVW, 148), INTC_IRQ(MTU25_UVW, 149),
	INTC_IRQ(MTU25_UVW, 150),

	INTC_IRQ(RTC, 152), INTC_IRQ(RTC, 153),
	INTC_IRQ(RTC, 154),

	INTC_IRQ(WDT, 156),

	INTC_IRQ(IIC30, 157), INTC_IRQ(IIC30, 158),
	INTC_IRQ(IIC30, 159), INTC_IRQ(IIC30, 160),
	INTC_IRQ(IIC30, 161),

	INTC_IRQ(IIC31, 164), INTC_IRQ(IIC31, 165),
	INTC_IRQ(IIC31, 166), INTC_IRQ(IIC31, 167),
	INTC_IRQ(IIC31, 168),

	INTC_IRQ(IIC32, 170), INTC_IRQ(IIC32, 171),
	INTC_IRQ(IIC32, 172), INTC_IRQ(IIC32, 173),
	INTC_IRQ(IIC32, 174),

	INTC_IRQ(DMAC0_DMINT0, 176), INTC_IRQ(DMAC1_DMINT1, 177),
	INTC_IRQ(DMAC2_DMINT2, 178), INTC_IRQ(DMAC3_DMINT3, 179),

	INTC_IRQ(SCIF0, 180), INTC_IRQ(SCIF0, 181),
	INTC_IRQ(SCIF0, 182), INTC_IRQ(SCIF0, 183),
	INTC_IRQ(SCIF1, 184), INTC_IRQ(SCIF1, 185),
	INTC_IRQ(SCIF1, 186), INTC_IRQ(SCIF1, 187),
	INTC_IRQ(SCIF2, 188), INTC_IRQ(SCIF2, 189),
	INTC_IRQ(SCIF2, 190), INTC_IRQ(SCIF2, 191),
	INTC_IRQ(SCIF3, 192), INTC_IRQ(SCIF3, 193),
	INTC_IRQ(SCIF3, 194), INTC_IRQ(SCIF3, 195),
	INTC_IRQ(SCIF4, 196), INTC_IRQ(SCIF4, 197),
	INTC_IRQ(SCIF4, 198), INTC_IRQ(SCIF4, 199),
	INTC_IRQ(SCIF5, 200), INTC_IRQ(SCIF5, 201),
	INTC_IRQ(SCIF5, 202), INTC_IRQ(SCIF5, 203),
	INTC_IRQ(SCIF6, 204), INTC_IRQ(SCIF6, 205),
	INTC_IRQ(SCIF6, 206), INTC_IRQ(SCIF6, 207),
	INTC_IRQ(SCIF7, 208), INTC_IRQ(SCIF7, 209),
	INTC_IRQ(SCIF7, 210), INTC_IRQ(SCIF7, 211),

	INTC_IRQ(DMAC0_DMINTA, 212), INTC_IRQ(DMAC4_DMINT4, 216),
	INTC_IRQ(DMAC5_DMINT5, 217), INTC_IRQ(DMAC6_DMINT6, 218),
	INTC_IRQ(DMAC7_DMINT7, 219),

	INTC_IRQ(RCAN0, 228), INTC_IRQ(RCAN0, 229),
	INTC_IRQ(RCAN0, 230),
	INTC_IRQ(RCAN0, 231), INTC_IRQ(RCAN0, 232),

	INTC_IRQ(RCAN1, 234), INTC_IRQ(RCAN1, 235),
	INTC_IRQ(RCAN1, 236),
	INTC_IRQ(RCAN1, 237), INTC_IRQ(RCAN1, 238),

	INTC_IRQ(SSI0_SSII, 244), INTC_IRQ(SSI1_SSII, 245),

	INTC_IRQ(TMR0, 246), INTC_IRQ(TMR0, 247),
	INTC_IRQ(TMR0, 248),

	INTC_IRQ(TMR1, 252), INTC_IRQ(TMR1, 253),
	INTC_IRQ(TMR1, 254),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(PINT, PINT0, PINT1, PINT2, PINT3,
		   PINT4, PINT5, PINT6, PINT7),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffe9418, 0, 16, 4, /* IPR01 */ { IRQ0, IRQ1, IRQ2, IRQ3 } },
	{ 0xfffe941a, 0, 16, 4, /* IPR02 */ { IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xfffe9420, 0, 16, 4, /* IPR05 */ { PINT, 0, ADC_ADI, 0 } },
	{ 0xfffe9800, 0, 16, 4, /* IPR06 */ { 0, MTU20_ABCD, MTU20_VEF, MTU21_AB } },
	{ 0xfffe9802, 0, 16, 4, /* IPR07 */ { MTU21_VU, MTU22_AB, MTU22_VU,  MTU23_ABCD } },
	{ 0xfffe9804, 0, 16, 4, /* IPR08 */ { MTU2_TCI3V, MTU24_ABCD, MTU2_TCI4V, MTU25_UVW } },

	{ 0xfffe9806, 0, 16, 4, /* IPR09 */ { RTC, WDT, IIC30, 0 } },
	{ 0xfffe9808, 0, 16, 4, /* IPR10 */ { IIC31, IIC32, DMAC0_DMINT0, DMAC1_DMINT1 } },
	{ 0xfffe980a, 0, 16, 4, /* IPR11 */ { DMAC2_DMINT2, DMAC3_DMINT3, SCIF0, SCIF1 } },
	{ 0xfffe980c, 0, 16, 4, /* IPR12 */ { SCIF2, SCIF3, SCIF4, SCIF5 } },
	{ 0xfffe980e, 0, 16, 4, /* IPR13 */ { SCIF6, SCIF7, DMAC0_DMINTA, DMAC4_DMINT4  } },
	{ 0xfffe9810, 0, 16, 4, /* IPR14 */ { DMAC5_DMINT5, DMAC6_DMINT6, DMAC7_DMINT7, 0 } },
	{ 0xfffe9812, 0, 16, 4, /* IPR15 */ { 0, RCAN0, RCAN1, 0 } },
	{ 0xfffe9814, 0, 16, 4, /* IPR16 */ { SSI0_SSII, SSI1_SSII, TMR0, TMR1 } },
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfffe9408, 0, 16, /* PINTER */
	  { 0, 0, 0, 0, 0, 0, 0, 0,
	    PINT7, PINT6, PINT5, PINT4, PINT3, PINT2, PINT1, PINT0 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7201", vectors, groups,
			 mask_registers, prio_registers, NULL);

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xfffe8000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 180, 180, 180, 180 }
	}, {
		.mapbase	= 0xfffe8800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 184, 184, 184, 184 }
	}, {
		.mapbase	= 0xfffe9000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 188, 188, 188, 188 }
	}, {
		.mapbase	= 0xfffe9800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 192, 192, 192, 192 }
	}, {
		.mapbase	= 0xfffea000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 196, 196, 196, 196 }
	}, {
		.mapbase	= 0xfffea800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 200, 200, 200, 200 }
	}, {
		.mapbase	= 0xfffeb000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 204, 204, 204, 204 }
	}, {
		.mapbase	= 0xfffeb800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 208, 208, 208, 208 }
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
	[0] = {
		.start	= 0xffff0800,
		.end	= 0xffff2000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Shared Period/Carry/Alarm IRQ */
		.start	= 152,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct sh_timer_config mtu2_0_platform_data = {
	.name = "MTU2_0",
	.channel_offset = -0x80,
	.timer_bit = 0,
	.clk = "peripheral_clk",
	.clockevent_rating = 200,
};

static struct resource mtu2_0_resources[] = {
	[0] = {
		.name	= "MTU2_0",
		.start	= 0xfffe4300,
		.end	= 0xfffe4326,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 108,
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
	.name = "MTU2_1",
	.channel_offset = -0x100,
	.timer_bit = 1,
	.clk = "peripheral_clk",
	.clockevent_rating = 200,
};

static struct resource mtu2_1_resources[] = {
	[0] = {
		.name	= "MTU2_1",
		.start	= 0xfffe4380,
		.end	= 0xfffe4390,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 116,
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

static struct sh_timer_config mtu2_2_platform_data = {
	.name = "MTU2_2",
	.channel_offset = 0x80,
	.timer_bit = 2,
	.clk = "peripheral_clk",
	.clockevent_rating = 200,
};

static struct resource mtu2_2_resources[] = {
	[0] = {
		.name	= "MTU2_2",
		.start	= 0xfffe4000,
		.end	= 0xfffe400a,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 124,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mtu2_2_device = {
	.name		= "sh_mtu2",
	.id		= 2,
	.dev = {
		.platform_data	= &mtu2_2_platform_data,
	},
	.resource	= mtu2_2_resources,
	.num_resources	= ARRAY_SIZE(mtu2_2_resources),
};

static struct platform_device *sh7201_devices[] __initdata = {
	&sci_device,
	&rtc_device,
	&mtu2_0_device,
	&mtu2_1_device,
	&mtu2_2_device,
};

static int __init sh7201_devices_setup(void)
{
	return platform_add_devices(sh7201_devices,
				    ARRAY_SIZE(sh7201_devices));
}
arch_initcall(sh7201_devices_setup);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

static struct platform_device *sh7201_early_devices[] __initdata = {
	&mtu2_0_device,
	&mtu2_1_device,
	&mtu2_2_device,
};

#define STBCR3 0xfffe0408

void __init plat_early_device_setup(void)
{
	/* enable MTU2 clock */
	__raw_writeb(__raw_readb(STBCR3) & ~0x20, STBCR3);

	early_platform_add_devices(sh7201_early_devices,
				   ARRAY_SIZE(sh7201_early_devices));
}
