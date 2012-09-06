/*
 * sh73a0 processor support
 *
 * Copyright (C) 2010  Takashi Yoshii
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/serial_sci.h>
#include <linux/sh_dma.h>
#include <linux/sh_intc.h>
#include <linux/sh_timer.h>
#include <mach/dma-register.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

static struct map_desc sh73a0_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

void __init sh73a0_map_io(void)
{
	iotable_init(sh73a0_io_desc, ARRAY_SIZE(sh73a0_io_desc));
}

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe6c40000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(72), gic_spi(72),
			    gic_spi(72), gic_spi(72) },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xe6c50000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(73), gic_spi(73),
			    gic_spi(73), gic_spi(73) },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xe6c60000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(74), gic_spi(74),
			    gic_spi(74), gic_spi(74) },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xe6c70000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(75), gic_spi(75),
			    gic_spi(75), gic_spi(75) },
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xe6c80000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(78), gic_spi(78),
			    gic_spi(78), gic_spi(78) },
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xe6cb0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(79), gic_spi(79),
			    gic_spi(79), gic_spi(79) },
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xe6cc0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(156), gic_spi(156),
			    gic_spi(156), gic_spi(156) },
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

static struct plat_sci_port scif7_platform_data = {
	.mapbase	= 0xe6cd0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(143), gic_spi(143),
			    gic_spi(143), gic_spi(143) },
};

static struct platform_device scif7_device = {
	.name		= "sh-sci",
	.id		= 7,
	.dev		= {
		.platform_data	= &scif7_platform_data,
	},
};

static struct plat_sci_port scif8_platform_data = {
	.mapbase	= 0xe6c30000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFB,
	.irqs		= { gic_spi(80), gic_spi(80),
			    gic_spi(80), gic_spi(80) },
};

static struct platform_device scif8_device = {
	.name		= "sh-sci",
	.id		= 8,
	.dev		= {
		.platform_data	= &scif8_platform_data,
	},
};

static struct sh_timer_config cmt10_platform_data = {
	.name = "CMT10",
	.channel_offset = 0x10,
	.timer_bit = 0,
	.clockevent_rating = 125,
	.clocksource_rating = 125,
};

static struct resource cmt10_resources[] = {
	[0] = {
		.name	= "CMT10",
		.start	= 0xe6138010,
		.end	= 0xe613801b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(65),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt10_device = {
	.name		= "sh_cmt",
	.id		= 10,
	.dev = {
		.platform_data	= &cmt10_platform_data,
	},
	.resource	= cmt10_resources,
	.num_resources	= ARRAY_SIZE(cmt10_resources),
};

/* TMU */
static struct sh_timer_config tmu00_platform_data = {
	.name = "TMU00",
	.channel_offset = 0x4,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource tmu00_resources[] = {
	[0] = {
		.name	= "TMU00",
		.start	= 0xfff60008,
		.end	= 0xfff60013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x0e80), /* TMU0_TUNI00 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu00_device = {
	.name		= "sh_tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu00_platform_data,
	},
	.resource	= tmu00_resources,
	.num_resources	= ARRAY_SIZE(tmu00_resources),
};

static struct sh_timer_config tmu01_platform_data = {
	.name = "TMU01",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clocksource_rating = 200,
};

static struct resource tmu01_resources[] = {
	[0] = {
		.name	= "TMU01",
		.start	= 0xfff60014,
		.end	= 0xfff6001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x0ea0), /* TMU0_TUNI01 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu01_device = {
	.name		= "sh_tmu",
	.id		= 1,
	.dev = {
		.platform_data	= &tmu01_platform_data,
	},
	.resource	= tmu01_resources,
	.num_resources	= ARRAY_SIZE(tmu01_resources),
};

static struct resource i2c0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start	= 0xe6820000,
		.end	= 0xe6820425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(167),
		.end	= gic_spi(170),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start	= 0xe6822000,
		.end	= 0xe6822425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(51),
		.end	= gic_spi(54),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c2_resources[] = {
	[0] = {
		.name	= "IIC2",
		.start	= 0xe6824000,
		.end	= 0xe6824425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(171),
		.end	= gic_spi(174),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c3_resources[] = {
	[0] = {
		.name	= "IIC3",
		.start	= 0xe6826000,
		.end	= 0xe6826425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(183),
		.end	= gic_spi(186),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c4_resources[] = {
	[0] = {
		.name	= "IIC4",
		.start	= 0xe6828000,
		.end	= 0xe6828425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(187),
		.end	= gic_spi(190),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device i2c0_device = {
	.name		= "i2c-sh_mobile",
	.id		= 0,
	.resource	= i2c0_resources,
	.num_resources	= ARRAY_SIZE(i2c0_resources),
};

static struct platform_device i2c1_device = {
	.name		= "i2c-sh_mobile",
	.id		= 1,
	.resource	= i2c1_resources,
	.num_resources	= ARRAY_SIZE(i2c1_resources),
};

static struct platform_device i2c2_device = {
	.name		= "i2c-sh_mobile",
	.id		= 2,
	.resource	= i2c2_resources,
	.num_resources	= ARRAY_SIZE(i2c2_resources),
};

static struct platform_device i2c3_device = {
	.name		= "i2c-sh_mobile",
	.id		= 3,
	.resource	= i2c3_resources,
	.num_resources	= ARRAY_SIZE(i2c3_resources),
};

static struct platform_device i2c4_device = {
	.name		= "i2c-sh_mobile",
	.id		= 4,
	.resource	= i2c4_resources,
	.num_resources	= ARRAY_SIZE(i2c4_resources),
};

static const struct sh_dmae_slave_config sh73a0_dmae_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_SCIF0_TX,
		.addr		= 0xe6c40020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x21,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF0_RX,
		.addr		= 0xe6c40024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x22,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_TX,
		.addr		= 0xe6c50020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x25,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_RX,
		.addr		= 0xe6c50024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x26,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_TX,
		.addr		= 0xe6c60020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x29,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_RX,
		.addr		= 0xe6c60024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_TX,
		.addr		= 0xe6c70020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_RX,
		.addr		= 0xe6c70024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_TX,
		.addr		= 0xe6c80020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x39,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_RX,
		.addr		= 0xe6c80024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_TX,
		.addr		= 0xe6cb0020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x35,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_RX,
		.addr		= 0xe6cb0024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x36,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_TX,
		.addr		= 0xe6cc0020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_RX,
		.addr		= 0xe6cc0024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF7_TX,
		.addr		= 0xe6cd0020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x19,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF7_RX,
		.addr		= 0xe6cd0024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF8_TX,
		.addr		= 0xe6c30040,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF8_RX,
		.addr		= 0xe6c30060,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3e,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_TX,
		.addr		= 0xee100030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc1,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_RX,
		.addr		= 0xee100030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc2,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_TX,
		.addr		= 0xee120030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc9,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_RX,
		.addr		= 0xee120030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xca,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_TX,
		.addr		= 0xee140030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xcd,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_RX,
		.addr		= 0xee140030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xce,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF_TX,
		.addr		= 0xe6bd0034,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd1,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF_RX,
		.addr		= 0xe6bd0034,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd2,
	},
};

#define DMAE_CHANNEL(_offset)					\
	{							\
		.offset         = _offset - 0x20,		\
		.dmars          = _offset - 0x20 + 0x40,	\
	}

static const struct sh_dmae_channel sh73a0_dmae_channels[] = {
	DMAE_CHANNEL(0x8000),
	DMAE_CHANNEL(0x8080),
	DMAE_CHANNEL(0x8100),
	DMAE_CHANNEL(0x8180),
	DMAE_CHANNEL(0x8200),
	DMAE_CHANNEL(0x8280),
	DMAE_CHANNEL(0x8300),
	DMAE_CHANNEL(0x8380),
	DMAE_CHANNEL(0x8400),
	DMAE_CHANNEL(0x8480),
	DMAE_CHANNEL(0x8500),
	DMAE_CHANNEL(0x8580),
	DMAE_CHANNEL(0x8600),
	DMAE_CHANNEL(0x8680),
	DMAE_CHANNEL(0x8700),
	DMAE_CHANNEL(0x8780),
	DMAE_CHANNEL(0x8800),
	DMAE_CHANNEL(0x8880),
	DMAE_CHANNEL(0x8900),
	DMAE_CHANNEL(0x8980),
};

static struct sh_dmae_pdata sh73a0_dmae_platform_data = {
	.slave          = sh73a0_dmae_slaves,
	.slave_num      = ARRAY_SIZE(sh73a0_dmae_slaves),
	.channel        = sh73a0_dmae_channels,
	.channel_num    = ARRAY_SIZE(sh73a0_dmae_channels),
	.ts_low_shift   = TS_LOW_SHIFT,
	.ts_low_mask    = TS_LOW_BIT << TS_LOW_SHIFT,
	.ts_high_shift  = TS_HI_SHIFT,
	.ts_high_mask   = TS_HI_BIT << TS_HI_SHIFT,
	.ts_shift       = dma_ts_shift,
	.ts_shift_num   = ARRAY_SIZE(dma_ts_shift),
	.dmaor_init     = DMAOR_DME,
};

static struct resource sh73a0_dmae_resources[] = {
	{
		/* Registers including DMAOR and channels including DMARSx */
		.start  = 0xfe000020,
		.end    = 0xfe008a00 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name	= "error_irq",
		.start  = gic_spi(129),
		.end    = gic_spi(129),
		.flags  = IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-19 */
		.start  = gic_spi(109),
		.end    = gic_spi(128),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 0,
	.resource	= sh73a0_dmae_resources,
	.num_resources	= ARRAY_SIZE(sh73a0_dmae_resources),
	.dev		= {
		.platform_data	= &sh73a0_dmae_platform_data,
	},
};

/* MPDMAC */
static const struct sh_dmae_slave_config sh73a0_mpdma_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_FSI2A_RX,
		.addr		= 0xec230020,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd6, /* CHECK ME */
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2A_TX,
		.addr		= 0xec230024,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd5, /* CHECK ME */
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2C_RX,
		.addr		= 0xec230060,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xda, /* CHECK ME */
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2C_TX,
		.addr		= 0xec230064,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd9, /* CHECK ME */
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2B_RX,
		.addr		= 0xec240020,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x8e, /* CHECK ME */
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2B_TX,
		.addr		= 0xec240024,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x8d, /* CHECK ME */
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2D_RX,
		.addr		=  0xec240060,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x9a, /* CHECK ME */
	},
};

#define MPDMA_CHANNEL(a, b, c)			\
{						\
	.offset		= a,			\
	.dmars		= b,			\
	.dmars_bit	= c,			\
	.chclr_offset	= (0x220 - 0x20) + a	\
}

static const struct sh_dmae_channel sh73a0_mpdma_channels[] = {
	MPDMA_CHANNEL(0x00, 0, 0),
	MPDMA_CHANNEL(0x10, 0, 8),
	MPDMA_CHANNEL(0x20, 4, 0),
	MPDMA_CHANNEL(0x30, 4, 8),
	MPDMA_CHANNEL(0x50, 8, 0),
	MPDMA_CHANNEL(0x70, 8, 8),
};

static struct sh_dmae_pdata sh73a0_mpdma_platform_data = {
	.slave		= sh73a0_mpdma_slaves,
	.slave_num	= ARRAY_SIZE(sh73a0_mpdma_slaves),
	.channel	= sh73a0_mpdma_channels,
	.channel_num	= ARRAY_SIZE(sh73a0_mpdma_channels),
	.ts_low_shift	= TS_LOW_SHIFT,
	.ts_low_mask	= TS_LOW_BIT << TS_LOW_SHIFT,
	.ts_high_shift	= TS_HI_SHIFT,
	.ts_high_mask	= TS_HI_BIT << TS_HI_SHIFT,
	.ts_shift	= dma_ts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_ts_shift),
	.dmaor_init	= DMAOR_DME,
	.chclr_present	= 1,
};

/* Resource order important! */
static struct resource sh73a0_mpdma_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xec618020,
		.end	= 0xec61828f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* DMARSx */
		.start	= 0xec619000,
		.end	= 0xec61900b,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "error_irq",
		.start	= gic_spi(181),
		.end	= gic_spi(181),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= gic_spi(175),
		.end	= gic_spi(180),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mpdma0_device = {
	.name		= "sh-dma-engine",
	.id		= 1,
	.resource	= sh73a0_mpdma_resources,
	.num_resources	= ARRAY_SIZE(sh73a0_mpdma_resources),
	.dev		= {
		.platform_data	= &sh73a0_mpdma_platform_data,
	},
};

static struct resource pmu_resources[] = {
	[0] = {
		.start	= gic_spi(55),
		.end	= gic_spi(55),
		.flags	= IORESOURCE_IRQ,
	},
	[1] = {
		.start	= gic_spi(56),
		.end	= gic_spi(56),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device pmu_device = {
	.name		= "arm-pmu",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pmu_resources),
	.resource	= pmu_resources,
};

static struct platform_device *sh73a0_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&scif8_device,
	&cmt10_device,
	&tmu00_device,
	&tmu01_device,
};

static struct platform_device *sh73a0_late_devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
	&i2c2_device,
	&i2c3_device,
	&i2c4_device,
	&dma0_device,
	&mpdma0_device,
	&pmu_device,
};

#define SRCR2          0xe61580b0

void __init sh73a0_add_standard_devices(void)
{
	/* Clear software reset bit on SY-DMAC module */
	__raw_writel(__raw_readl(SRCR2) & ~(1 << 18), SRCR2);

	platform_add_devices(sh73a0_early_devices,
			    ARRAY_SIZE(sh73a0_early_devices));
	platform_add_devices(sh73a0_late_devices,
			    ARRAY_SIZE(sh73a0_late_devices));
}

/* do nothing for !CONFIG_SMP or !CONFIG_HAVE_TWD */
void __init __weak sh73a0_register_twd(void) { }

static void __init sh73a0_earlytimer_init(void)
{
	sh73a0_clock_init();
	shmobile_earlytimer_init();
	sh73a0_register_twd();
}

void __init sh73a0_add_early_devices(void)
{
	early_platform_add_devices(sh73a0_early_devices,
				   ARRAY_SIZE(sh73a0_early_devices));

	/* setup early console here as well */
	shmobile_setup_console();

	/* override timer setup with soc-specific code */
	shmobile_timer.init = sh73a0_earlytimer_init;
}
