/*
 * sh7372 processor support
 *
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
#include <linux/of_platform.h>
#include <linux/uio_driver.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/serial_sci.h>
#include <linux/sh_dma.h>
#include <linux/sh_intc.h>
#include <linux/sh_timer.h>
#include <linux/pm_domain.h>
#include <linux/dma-mapping.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/sh7372.h>
#include <mach/common.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

static struct map_desc sh7372_io_desc[] __initdata = {
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

void __init sh7372_map_io(void)
{
	iotable_init(sh7372_io_desc, ARRAY_SIZE(sh7372_io_desc));

	/*
	 * DMA memory at 0xff200000 - 0xffdfffff. The default 2MB size isn't
	 * enough to allocate the frame buffer memory.
	 */
	init_consistent_dma_size(12 << 20);
}

/* SCIFA0 */
static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe6c40000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0x0c00), evt2irq(0x0c00),
			    evt2irq(0x0c00), evt2irq(0x0c00) },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

/* SCIFA1 */
static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xe6c50000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0x0c20), evt2irq(0x0c20),
			    evt2irq(0x0c20), evt2irq(0x0c20) },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

/* SCIFA2 */
static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xe6c60000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0x0c40), evt2irq(0x0c40),
			    evt2irq(0x0c40), evt2irq(0x0c40) },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

/* SCIFA3 */
static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xe6c70000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0x0c60), evt2irq(0x0c60),
			    evt2irq(0x0c60), evt2irq(0x0c60) },
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

/* SCIFA4 */
static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xe6c80000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0x0d20), evt2irq(0x0d20),
			    evt2irq(0x0d20), evt2irq(0x0d20) },
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

/* SCIFA5 */
static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xe6cb0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0x0d40), evt2irq(0x0d40),
			    evt2irq(0x0d40), evt2irq(0x0d40) },
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

/* SCIFB */
static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xe6c30000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFB,
	.irqs		= { evt2irq(0x0d60), evt2irq(0x0d60),
			    evt2irq(0x0d60), evt2irq(0x0d60) },
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

/* CMT */
static struct sh_timer_config cmt2_platform_data = {
	.name = "CMT2",
	.channel_offset = 0x40,
	.timer_bit = 5,
	.clockevent_rating = 125,
	.clocksource_rating = 125,
};

static struct resource cmt2_resources[] = {
	[0] = {
		.name	= "CMT2",
		.start	= 0xe6130040,
		.end	= 0xe613004b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0b80), /* CMT2 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt2_device = {
	.name		= "sh_cmt",
	.id		= 2,
	.dev = {
		.platform_data	= &cmt2_platform_data,
	},
	.resource	= cmt2_resources,
	.num_resources	= ARRAY_SIZE(cmt2_resources),
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
		.start	= intcs_evt2irq(0xe80), /* TMU_TUNI0 */
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
		.start	= intcs_evt2irq(0xea0), /* TMU_TUNI1 */
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

/* I2C */
static struct resource iic0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start  = 0xFFF20000,
		.end    = 0xFFF20425 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = intcs_evt2irq(0xe00), /* IIC0_ALI0 */
		.end    = intcs_evt2irq(0xe60), /* IIC0_DTEI0 */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device iic0_device = {
	.name           = "i2c-sh_mobile",
	.id             = 0, /* "i2c0" clock */
	.num_resources  = ARRAY_SIZE(iic0_resources),
	.resource       = iic0_resources,
};

static struct resource iic1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start  = 0xE6C20000,
		.end    = 0xE6C20425 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x780), /* IIC1_ALI1 */
		.end    = evt2irq(0x7e0), /* IIC1_DTEI1 */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device iic1_device = {
	.name           = "i2c-sh_mobile",
	.id             = 1, /* "i2c1" clock */
	.num_resources  = ARRAY_SIZE(iic1_resources),
	.resource       = iic1_resources,
};

/* DMA */
/* Transmit sizes and respective CHCR register values */
enum {
	XMIT_SZ_8BIT		= 0,
	XMIT_SZ_16BIT		= 1,
	XMIT_SZ_32BIT		= 2,
	XMIT_SZ_64BIT		= 7,
	XMIT_SZ_128BIT		= 3,
	XMIT_SZ_256BIT		= 4,
	XMIT_SZ_512BIT		= 5,
};

/* log2(size / 8) - used to calculate number of transfers */
#define TS_SHIFT {			\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_64BIT]		= 3,	\
	[XMIT_SZ_128BIT]	= 4,	\
	[XMIT_SZ_256BIT]	= 5,	\
	[XMIT_SZ_512BIT]	= 6,	\
}

#define TS_INDEX2VAL(i) ((((i) & 3) << 3) | \
			 (((i) & 0xc) << (20 - 2)))

static const struct sh_dmae_slave_config sh7372_dmae_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_SCIF0_TX,
		.addr		= 0xe6c40020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x21,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF0_RX,
		.addr		= 0xe6c40024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x22,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_TX,
		.addr		= 0xe6c50020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x25,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_RX,
		.addr		= 0xe6c50024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x26,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_TX,
		.addr		= 0xe6c60020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x29,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_RX,
		.addr		= 0xe6c60024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x2a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_TX,
		.addr		= 0xe6c70020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x2d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_RX,
		.addr		= 0xe6c70024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x2e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_TX,
		.addr		= 0xe6c80020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x39,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_RX,
		.addr		= 0xe6c80024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x3a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_TX,
		.addr		= 0xe6cb0020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x35,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_RX,
		.addr		= 0xe6cb0024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x36,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_TX,
		.addr		= 0xe6c30040,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x3d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_RX,
		.addr		= 0xe6c30060,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x3e,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_TX,
		.addr		= 0xe6850030,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xc1,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_RX,
		.addr		= 0xe6850030,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xc2,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_TX,
		.addr		= 0xe6860030,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xc9,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_RX,
		.addr		= 0xe6860030,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xca,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_TX,
		.addr		= 0xe6870030,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xcd,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_RX,
		.addr		= 0xe6870030,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xce,
	}, {
		.slave_id	= SHDMA_SLAVE_FSIA_TX,
		.addr		= 0xfe1f0024,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xb1,
	}, {
		.slave_id	= SHDMA_SLAVE_FSIA_RX,
		.addr		= 0xfe1f0020,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xb2,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF_TX,
		.addr		= 0xe6bd0034,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xd1,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF_RX,
		.addr		= 0xe6bd0034,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xd2,
	},
};

#define SH7372_CHCLR 0x220

static const struct sh_dmae_channel sh7372_dmae_channels[] = {
	{
		.offset = 0,
		.dmars = 0,
		.dmars_bit = 0,
		.chclr_offset = SH7372_CHCLR + 0,
	}, {
		.offset = 0x10,
		.dmars = 0,
		.dmars_bit = 8,
		.chclr_offset = SH7372_CHCLR + 0x10,
	}, {
		.offset = 0x20,
		.dmars = 4,
		.dmars_bit = 0,
		.chclr_offset = SH7372_CHCLR + 0x20,
	}, {
		.offset = 0x30,
		.dmars = 4,
		.dmars_bit = 8,
		.chclr_offset = SH7372_CHCLR + 0x30,
	}, {
		.offset = 0x50,
		.dmars = 8,
		.dmars_bit = 0,
		.chclr_offset = SH7372_CHCLR + 0x50,
	}, {
		.offset = 0x60,
		.dmars = 8,
		.dmars_bit = 8,
		.chclr_offset = SH7372_CHCLR + 0x60,
	}
};

static const unsigned int ts_shift[] = TS_SHIFT;

static struct sh_dmae_pdata dma_platform_data = {
	.slave		= sh7372_dmae_slaves,
	.slave_num	= ARRAY_SIZE(sh7372_dmae_slaves),
	.channel	= sh7372_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7372_dmae_channels),
	.ts_low_shift	= 3,
	.ts_low_mask	= 0x18,
	.ts_high_shift	= (20 - 2),	/* 2 bits for shifted low TS */
	.ts_high_mask	= 0x00300000,
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
	.dmaor_init	= DMAOR_DME,
	.chclr_present	= 1,
};

/* Resource order important! */
static struct resource sh7372_dmae0_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xfe008020,
		.end	= 0xfe00828f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* DMARSx */
		.start	= 0xfe009000,
		.end	= 0xfe00900b,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "error_irq",
		.start	= evt2irq(0x20c0),
		.end	= evt2irq(0x20c0),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= evt2irq(0x2000),
		.end	= evt2irq(0x20a0),
		.flags	= IORESOURCE_IRQ,
	},
};

/* Resource order important! */
static struct resource sh7372_dmae1_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xfe018020,
		.end	= 0xfe01828f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* DMARSx */
		.start	= 0xfe019000,
		.end	= 0xfe01900b,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "error_irq",
		.start	= evt2irq(0x21c0),
		.end	= evt2irq(0x21c0),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= evt2irq(0x2100),
		.end	= evt2irq(0x21a0),
		.flags	= IORESOURCE_IRQ,
	},
};

/* Resource order important! */
static struct resource sh7372_dmae2_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xfe028020,
		.end	= 0xfe02828f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* DMARSx */
		.start	= 0xfe029000,
		.end	= 0xfe02900b,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "error_irq",
		.start	= evt2irq(0x22c0),
		.end	= evt2irq(0x22c0),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= evt2irq(0x2200),
		.end	= evt2irq(0x22a0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 0,
	.resource	= sh7372_dmae0_resources,
	.num_resources	= ARRAY_SIZE(sh7372_dmae0_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

static struct platform_device dma1_device = {
	.name		= "sh-dma-engine",
	.id		= 1,
	.resource	= sh7372_dmae1_resources,
	.num_resources	= ARRAY_SIZE(sh7372_dmae1_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

static struct platform_device dma2_device = {
	.name		= "sh-dma-engine",
	.id		= 2,
	.resource	= sh7372_dmae2_resources,
	.num_resources	= ARRAY_SIZE(sh7372_dmae2_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

/*
 * USB-DMAC
 */

unsigned int usbts_shift[] = {3, 4, 5};

enum {
	XMIT_SZ_8BYTE		= 0,
	XMIT_SZ_16BYTE		= 1,
	XMIT_SZ_32BYTE		= 2,
};

#define USBTS_INDEX2VAL(i) (((i) & 3) << 6)

static const struct sh_dmae_channel sh7372_usb_dmae_channels[] = {
	{
		.offset = 0,
	}, {
		.offset = 0x20,
	},
};

/* USB DMAC0 */
static const struct sh_dmae_slave_config sh7372_usb_dmae0_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_USB0_TX,
		.chcr		= USBTS_INDEX2VAL(XMIT_SZ_8BYTE),
	}, {
		.slave_id	= SHDMA_SLAVE_USB0_RX,
		.chcr		= USBTS_INDEX2VAL(XMIT_SZ_8BYTE),
	},
};

static struct sh_dmae_pdata usb_dma0_platform_data = {
	.slave		= sh7372_usb_dmae0_slaves,
	.slave_num	= ARRAY_SIZE(sh7372_usb_dmae0_slaves),
	.channel	= sh7372_usb_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7372_usb_dmae_channels),
	.ts_low_shift	= 6,
	.ts_low_mask	= 0xc0,
	.ts_high_shift	= 0,
	.ts_high_mask	= 0,
	.ts_shift	= usbts_shift,
	.ts_shift_num	= ARRAY_SIZE(usbts_shift),
	.dmaor_init	= DMAOR_DME,
	.chcr_offset	= 0x14,
	.chcr_ie_bit	= 1 << 5,
	.dmaor_is_32bit	= 1,
	.needs_tend_set	= 1,
	.no_dmars	= 1,
	.slave_only	= 1,
};

static struct resource sh7372_usb_dmae0_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xe68a0020,
		.end	= 0xe68a0064 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* VCR/SWR/DMICR */
		.start	= 0xe68a0000,
		.end	= 0xe68a0014 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* IRQ for channels */
		.start	= evt2irq(0x0a00),
		.end	= evt2irq(0x0a00),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb_dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 3,
	.resource	= sh7372_usb_dmae0_resources,
	.num_resources	= ARRAY_SIZE(sh7372_usb_dmae0_resources),
	.dev		= {
		.platform_data	= &usb_dma0_platform_data,
	},
};

/* USB DMAC1 */
static const struct sh_dmae_slave_config sh7372_usb_dmae1_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_USB1_TX,
		.chcr		= USBTS_INDEX2VAL(XMIT_SZ_8BYTE),
	}, {
		.slave_id	= SHDMA_SLAVE_USB1_RX,
		.chcr		= USBTS_INDEX2VAL(XMIT_SZ_8BYTE),
	},
};

static struct sh_dmae_pdata usb_dma1_platform_data = {
	.slave		= sh7372_usb_dmae1_slaves,
	.slave_num	= ARRAY_SIZE(sh7372_usb_dmae1_slaves),
	.channel	= sh7372_usb_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7372_usb_dmae_channels),
	.ts_low_shift	= 6,
	.ts_low_mask	= 0xc0,
	.ts_high_shift	= 0,
	.ts_high_mask	= 0,
	.ts_shift	= usbts_shift,
	.ts_shift_num	= ARRAY_SIZE(usbts_shift),
	.dmaor_init	= DMAOR_DME,
	.chcr_offset	= 0x14,
	.chcr_ie_bit	= 1 << 5,
	.dmaor_is_32bit	= 1,
	.needs_tend_set	= 1,
	.no_dmars	= 1,
	.slave_only	= 1,
};

static struct resource sh7372_usb_dmae1_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xe68c0020,
		.end	= 0xe68c0064 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* VCR/SWR/DMICR */
		.start	= 0xe68c0000,
		.end	= 0xe68c0014 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* IRQ for channels */
		.start	= evt2irq(0x1d00),
		.end	= evt2irq(0x1d00),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb_dma1_device = {
	.name		= "sh-dma-engine",
	.id		= 4,
	.resource	= sh7372_usb_dmae1_resources,
	.num_resources	= ARRAY_SIZE(sh7372_usb_dmae1_resources),
	.dev		= {
		.platform_data	= &usb_dma1_platform_data,
	},
};

/* VPU */
static struct uio_info vpu_platform_data = {
	.name = "VPU5HG",
	.version = "0",
	.irq = intcs_evt2irq(0x980),
};

static struct resource vpu_resources[] = {
	[0] = {
		.name	= "VPU",
		.start	= 0xfe900000,
		.end	= 0xfe900157,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device vpu_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 0,
	.dev = {
		.platform_data	= &vpu_platform_data,
	},
	.resource	= vpu_resources,
	.num_resources	= ARRAY_SIZE(vpu_resources),
};

/* VEU0 */
static struct uio_info veu0_platform_data = {
	.name = "VEU0",
	.version = "0",
	.irq = intcs_evt2irq(0x700),
};

static struct resource veu0_resources[] = {
	[0] = {
		.name	= "VEU0",
		.start	= 0xfe920000,
		.end	= 0xfe9200cb,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu0_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 1,
	.dev = {
		.platform_data	= &veu0_platform_data,
	},
	.resource	= veu0_resources,
	.num_resources	= ARRAY_SIZE(veu0_resources),
};

/* VEU1 */
static struct uio_info veu1_platform_data = {
	.name = "VEU1",
	.version = "0",
	.irq = intcs_evt2irq(0x720),
};

static struct resource veu1_resources[] = {
	[0] = {
		.name	= "VEU1",
		.start	= 0xfe924000,
		.end	= 0xfe9240cb,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu1_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 2,
	.dev = {
		.platform_data	= &veu1_platform_data,
	},
	.resource	= veu1_resources,
	.num_resources	= ARRAY_SIZE(veu1_resources),
};

/* VEU2 */
static struct uio_info veu2_platform_data = {
	.name = "VEU2",
	.version = "0",
	.irq = intcs_evt2irq(0x740),
};

static struct resource veu2_resources[] = {
	[0] = {
		.name	= "VEU2",
		.start	= 0xfe928000,
		.end	= 0xfe928307,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu2_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 3,
	.dev = {
		.platform_data	= &veu2_platform_data,
	},
	.resource	= veu2_resources,
	.num_resources	= ARRAY_SIZE(veu2_resources),
};

/* VEU3 */
static struct uio_info veu3_platform_data = {
	.name = "VEU3",
	.version = "0",
	.irq = intcs_evt2irq(0x760),
};

static struct resource veu3_resources[] = {
	[0] = {
		.name	= "VEU3",
		.start	= 0xfe92c000,
		.end	= 0xfe92c307,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu3_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 4,
	.dev = {
		.platform_data	= &veu3_platform_data,
	},
	.resource	= veu3_resources,
	.num_resources	= ARRAY_SIZE(veu3_resources),
};

/* JPU */
static struct uio_info jpu_platform_data = {
	.name = "JPU",
	.version = "0",
	.irq = intcs_evt2irq(0x560),
};

static struct resource jpu_resources[] = {
	[0] = {
		.name	= "JPU",
		.start	= 0xfe980000,
		.end	= 0xfe9902d3,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device jpu_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 5,
	.dev = {
		.platform_data	= &jpu_platform_data,
	},
	.resource	= jpu_resources,
	.num_resources	= ARRAY_SIZE(jpu_resources),
};

/* SPU2DSP0 */
static struct uio_info spu0_platform_data = {
	.name = "SPU2DSP0",
	.version = "0",
	.irq = evt2irq(0x1800),
};

static struct resource spu0_resources[] = {
	[0] = {
		.name	= "SPU2DSP0",
		.start	= 0xfe200000,
		.end	= 0xfe2fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spu0_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 6,
	.dev = {
		.platform_data	= &spu0_platform_data,
	},
	.resource	= spu0_resources,
	.num_resources	= ARRAY_SIZE(spu0_resources),
};

/* SPU2DSP1 */
static struct uio_info spu1_platform_data = {
	.name = "SPU2DSP1",
	.version = "0",
	.irq = evt2irq(0x1820),
};

static struct resource spu1_resources[] = {
	[0] = {
		.name	= "SPU2DSP1",
		.start	= 0xfe300000,
		.end	= 0xfe3fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spu1_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 7,
	.dev = {
		.platform_data	= &spu1_platform_data,
	},
	.resource	= spu1_resources,
	.num_resources	= ARRAY_SIZE(spu1_resources),
};

static struct platform_device *sh7372_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&cmt2_device,
	&tmu00_device,
	&tmu01_device,
};

static struct platform_device *sh7372_late_devices[] __initdata = {
	&iic0_device,
	&iic1_device,
	&dma0_device,
	&dma1_device,
	&dma2_device,
	&usb_dma0_device,
	&usb_dma1_device,
	&vpu_device,
	&veu0_device,
	&veu1_device,
	&veu2_device,
	&veu3_device,
	&jpu_device,
	&spu0_device,
	&spu1_device,
};

void __init sh7372_add_standard_devices(void)
{
	sh7372_init_pm_domain(&sh7372_a4lc);
	sh7372_init_pm_domain(&sh7372_a4mp);
	sh7372_init_pm_domain(&sh7372_d4);
	sh7372_init_pm_domain(&sh7372_a4r);
	sh7372_init_pm_domain(&sh7372_a3rv);
	sh7372_init_pm_domain(&sh7372_a3ri);
	sh7372_init_pm_domain(&sh7372_a4s);
	sh7372_init_pm_domain(&sh7372_a3sp);
	sh7372_init_pm_domain(&sh7372_a3sg);

	sh7372_pm_add_subdomain(&sh7372_a4lc, &sh7372_a3rv);
	sh7372_pm_add_subdomain(&sh7372_a4r, &sh7372_a4lc);

	sh7372_pm_add_subdomain(&sh7372_a4s, &sh7372_a3sg);
	sh7372_pm_add_subdomain(&sh7372_a4s, &sh7372_a3sp);

	platform_add_devices(sh7372_early_devices,
			    ARRAY_SIZE(sh7372_early_devices));

	platform_add_devices(sh7372_late_devices,
			    ARRAY_SIZE(sh7372_late_devices));

	sh7372_add_device_to_domain(&sh7372_a3rv, &vpu_device);
	sh7372_add_device_to_domain(&sh7372_a4mp, &spu0_device);
	sh7372_add_device_to_domain(&sh7372_a4mp, &spu1_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif0_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif1_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif2_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif3_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif4_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif5_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &scif6_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &iic1_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &dma0_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &dma1_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &dma2_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &usb_dma0_device);
	sh7372_add_device_to_domain(&sh7372_a3sp, &usb_dma1_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &iic0_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &veu0_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &veu1_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &veu2_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &veu3_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &jpu_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &tmu00_device);
	sh7372_add_device_to_domain(&sh7372_a4r, &tmu01_device);
}

static void __init sh7372_earlytimer_init(void)
{
	sh7372_clock_init();
	shmobile_earlytimer_init();
}

void __init sh7372_add_early_devices(void)
{
	early_platform_add_devices(sh7372_early_devices,
				   ARRAY_SIZE(sh7372_early_devices));

	/* setup early console here as well */
	shmobile_setup_console();

	/* override timer setup with soc-specific code */
	shmobile_timer.init = sh7372_earlytimer_init;
}

#ifdef CONFIG_USE_OF

void __init sh7372_add_early_devices_dt(void)
{
	shmobile_setup_delay(800, 1, 3); /* Cortex-A8 @ 800MHz */

	early_platform_add_devices(sh7372_early_devices,
				   ARRAY_SIZE(sh7372_early_devices));

	/* setup early console here as well */
	shmobile_setup_console();
}

static const struct of_dev_auxdata sh7372_auxdata_lookup[] __initconst = {
	{ }
};

void __init sh7372_add_standard_devices_dt(void)
{
	/* clocks are setup late during boot in the case of DT */
	sh7372_clock_init();

	platform_add_devices(sh7372_early_devices,
			    ARRAY_SIZE(sh7372_early_devices));

	of_platform_populate(NULL, of_default_bus_match_table,
			     sh7372_auxdata_lookup, NULL);
}

static const char *sh7372_boards_compat_dt[] __initdata = {
	"renesas,sh7372",
	NULL,
};

DT_MACHINE_START(SH7372_DT, "Generic SH7372 (Flattened Device Tree)")
	.map_io		= sh7372_map_io,
	.init_early	= sh7372_add_early_devices_dt,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= sh7372_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= sh7372_add_standard_devices_dt,
	.timer		= &shmobile_timer,
	.dt_compat	= sh7372_boards_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
