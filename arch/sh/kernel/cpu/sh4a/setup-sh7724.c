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
#include <linux/sh_dma.h>
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <linux/io.h>
#include <linux/notifier.h>

#include <asm/suspend.h>
#include <asm/clock.h>
#include <asm/mmzone.h>

#include <cpu/dma-register.h>
#include <cpu/sh7724.h>

/* DMA */
static const struct sh_dmae_slave_config sh7724_dmae_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_SCIF0_TX,
		.addr		= 0xffe0000c,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x21,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF0_RX,
		.addr		= 0xffe00014,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x22,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_TX,
		.addr		= 0xffe1000c,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x25,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_RX,
		.addr		= 0xffe10014,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x26,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_TX,
		.addr		= 0xffe2000c,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x29,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_RX,
		.addr		= 0xffe20014,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x2a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_TX,
		.addr		= 0xa4e30020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x2d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_RX,
		.addr		= 0xa4e30024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x2e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_TX,
		.addr		= 0xa4e40020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x31,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_RX,
		.addr		= 0xa4e40024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x32,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_TX,
		.addr		= 0xa4e50020,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x35,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_RX,
		.addr		= 0xa4e50024,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_8BIT),
		.mid_rid	= 0x36,
	}, {
		.slave_id	= SHDMA_SLAVE_USB0D0_TX,
		.addr		= 0xA4D80100,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0x73,
	}, {
		.slave_id	= SHDMA_SLAVE_USB0D0_RX,
		.addr		= 0xA4D80100,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0x73,
	}, {
		.slave_id	= SHDMA_SLAVE_USB0D1_TX,
		.addr		= 0xA4D80120,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0x77,
	}, {
		.slave_id	= SHDMA_SLAVE_USB0D1_RX,
		.addr		= 0xA4D80120,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0x77,
	}, {
		.slave_id	= SHDMA_SLAVE_USB1D0_TX,
		.addr		= 0xA4D90100,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xab,
	}, {
		.slave_id	= SHDMA_SLAVE_USB1D0_RX,
		.addr		= 0xA4D90100,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xab,
	}, {
		.slave_id	= SHDMA_SLAVE_USB1D1_TX,
		.addr		= 0xA4D90120,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xaf,
	}, {
		.slave_id	= SHDMA_SLAVE_USB1D1_RX,
		.addr		= 0xA4D90120,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_32BIT),
		.mid_rid	= 0xaf,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_TX,
		.addr		= 0x04ce0030,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xc1,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_RX,
		.addr		= 0x04ce0030,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xc2,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_TX,
		.addr		= 0x04cf0030,
		.chcr		= DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xc9,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_RX,
		.addr		= 0x04cf0030,
		.chcr		= DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL(XMIT_SZ_16BIT),
		.mid_rid	= 0xca,
	},
};

static const struct sh_dmae_channel sh7724_dmae_channels[] = {
	{
		.offset = 0,
		.dmars = 0,
		.dmars_bit = 0,
	}, {
		.offset = 0x10,
		.dmars = 0,
		.dmars_bit = 8,
	}, {
		.offset = 0x20,
		.dmars = 4,
		.dmars_bit = 0,
	}, {
		.offset = 0x30,
		.dmars = 4,
		.dmars_bit = 8,
	}, {
		.offset = 0x50,
		.dmars = 8,
		.dmars_bit = 0,
	}, {
		.offset = 0x60,
		.dmars = 8,
		.dmars_bit = 8,
	}
};

static const unsigned int ts_shift[] = TS_SHIFT;

static struct sh_dmae_pdata dma_platform_data = {
	.slave		= sh7724_dmae_slaves,
	.slave_num	= ARRAY_SIZE(sh7724_dmae_slaves),
	.channel	= sh7724_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7724_dmae_channels),
	.ts_low_shift	= CHCR_TS_LOW_SHIFT,
	.ts_low_mask	= CHCR_TS_LOW_MASK,
	.ts_high_shift	= CHCR_TS_HIGH_SHIFT,
	.ts_high_mask	= CHCR_TS_HIGH_MASK,
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
	.dmaor_init	= DMAOR_INIT,
};

/* Resource order important! */
static struct resource sh7724_dmae0_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xfe008020,
		.end	= 0xfe00808f,
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
		.start	= evt2irq(0xbc0),
		.end	= evt2irq(0xbc0),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-3 */
		.start	= evt2irq(0x800),
		.end	= evt2irq(0x860),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 4-5 */
		.start	= evt2irq(0xb80),
		.end	= evt2irq(0xba0),
		.flags	= IORESOURCE_IRQ,
	},
};

/* Resource order important! */
static struct resource sh7724_dmae1_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xfdc08020,
		.end	= 0xfdc0808f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* DMARSx */
		.start	= 0xfdc09000,
		.end	= 0xfdc0900b,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "error_irq",
		.start	= evt2irq(0xb40),
		.end	= evt2irq(0xb40),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-3 */
		.start	= evt2irq(0x700),
		.end	= evt2irq(0x760),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 4-5 */
		.start	= evt2irq(0xb00),
		.end	= evt2irq(0xb20),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 0,
	.resource	= sh7724_dmae0_resources,
	.num_resources	= ARRAY_SIZE(sh7724_dmae0_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

static struct platform_device dma1_device = {
	.name		= "sh-dma-engine",
	.id		= 1,
	.resource	= sh7724_dmae1_resources,
	.num_resources	= ARRAY_SIZE(sh7724_dmae1_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

/* Serial */
static struct plat_sci_port scif0_platform_data = {
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.type           = PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_NO_SCSPTR_REGTYPE,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xffe00000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xc00)),
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
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.type           = PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_NO_SCSPTR_REGTYPE,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xffe10000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xc20)),
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
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.type           = PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_NO_SCSPTR_REGTYPE,
};

static struct resource scif2_resources[] = {
	DEFINE_RES_MEM(0xffe20000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xc40)),
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
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.sampling_rate	= 8,
	.type           = PORT_SCIFA,
};

static struct resource scif3_resources[] = {
	DEFINE_RES_MEM(0xa4e30000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x900)),
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

static struct plat_sci_port scif4_platform_data = {
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.sampling_rate	= 8,
	.type           = PORT_SCIFA,
};

static struct resource scif4_resources[] = {
	DEFINE_RES_MEM(0xa4e40000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xd00)),
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.resource	= scif4_resources,
	.num_resources	= ARRAY_SIZE(scif4_resources),
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.port_reg	= SCIx_NOT_SUPPORTED,
	.flags          = UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.sampling_rate	= 8,
	.type           = PORT_SCIFA,
};

static struct resource scif5_resources[] = {
	DEFINE_RES_MEM(0xa4e50000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xfa0)),
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.resource	= scif5_resources,
	.num_resources	= ARRAY_SIZE(scif5_resources),
	.dev		= {
		.platform_data	= &scif5_platform_data,
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
		.start	= evt2irq(0xaa0),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= evt2irq(0xac0),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= evt2irq(0xa80),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

/* I2C0 */
static struct resource iic0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start  = 0x04470000,
		.end    = 0x04470018 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0xe00),
		.end    = evt2irq(0xe60),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device iic0_device = {
	.name           = "i2c-sh_mobile",
	.id             = 0, /* "i2c0" clock */
	.num_resources  = ARRAY_SIZE(iic0_resources),
	.resource       = iic0_resources,
};

/* I2C1 */
static struct resource iic1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start  = 0x04750000,
		.end    = 0x04750018 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0xd80),
		.end    = evt2irq(0xde0),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device iic1_device = {
	.name           = "i2c-sh_mobile",
	.id             = 1, /* "i2c1" clock */
	.num_resources  = ARRAY_SIZE(iic1_resources),
	.resource       = iic1_resources,
};

/* VPU */
static struct uio_info vpu_platform_data = {
	.name = "VPU5F",
	.version = "0",
	.irq = evt2irq(0x980),
};

static struct resource vpu_resources[] = {
	[0] = {
		.name	= "VPU",
		.start	= 0xfe900000,
		.end	= 0xfe902807,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
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
	.name = "VEU3F0",
	.version = "0",
	.irq = evt2irq(0xc60),
};

static struct resource veu0_resources[] = {
	[0] = {
		.name	= "VEU3F0",
		.start	= 0xfe920000,
		.end	= 0xfe9200cb,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
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
	.name = "VEU3F1",
	.version = "0",
	.irq = evt2irq(0x8c0),
};

static struct resource veu1_resources[] = {
	[0] = {
		.name	= "VEU3F1",
		.start	= 0xfe924000,
		.end	= 0xfe9240cb,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
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

/* BEU0 */
static struct uio_info beu0_platform_data = {
	.name = "BEU0",
	.version = "0",
	.irq = evt2irq(0x8A0),
};

static struct resource beu0_resources[] = {
	[0] = {
		.name	= "BEU0",
		.start	= 0xfe930000,
		.end	= 0xfe933400,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device beu0_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 6,
	.dev = {
		.platform_data	= &beu0_platform_data,
	},
	.resource	= beu0_resources,
	.num_resources	= ARRAY_SIZE(beu0_resources),
};

/* BEU1 */
static struct uio_info beu1_platform_data = {
	.name = "BEU1",
	.version = "0",
	.irq = evt2irq(0xA00),
};

static struct resource beu1_resources[] = {
	[0] = {
		.name	= "BEU1",
		.start	= 0xfe940000,
		.end	= 0xfe943400,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device beu1_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 7,
	.dev = {
		.platform_data	= &beu1_platform_data,
	},
	.resource	= beu1_resources,
	.num_resources	= ARRAY_SIZE(beu1_resources),
};

static struct sh_timer_config cmt_platform_data = {
	.channel_offset = 0x60,
	.timer_bit = 5,
	.clockevent_rating = 125,
	.clocksource_rating = 200,
};

static struct resource cmt_resources[] = {
	[0] = {
		.start	= 0x044a0060,
		.end	= 0x044a006b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xf00),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt_device = {
	.name		= "sh_cmt",
	.id		= 0,
	.dev = {
		.platform_data	= &cmt_platform_data,
	},
	.resource	= cmt_resources,
	.num_resources	= ARRAY_SIZE(cmt_resources),
};

static struct sh_timer_config tmu0_platform_data = {
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource tmu0_resources[] = {
	[0] = {
		.start	= 0xffd80008,
		.end	= 0xffd80013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x400),
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
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clocksource_rating = 200,
};

static struct resource tmu1_resources[] = {
	[0] = {
		.start	= 0xffd80014,
		.end	= 0xffd8001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x420),
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
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu2_resources[] = {
	[0] = {
		.start	= 0xffd80020,
		.end	= 0xffd8002b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x440),
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
	.channel_offset = 0x04,
	.timer_bit = 0,
};

static struct resource tmu3_resources[] = {
	[0] = {
		.start	= 0xffd90008,
		.end	= 0xffd90013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x920),
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
	.channel_offset = 0x10,
	.timer_bit = 1,
};

static struct resource tmu4_resources[] = {
	[0] = {
		.start	= 0xffd90014,
		.end	= 0xffd9001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x940),
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
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu5_resources[] = {
	[0] = {
		.start	= 0xffd90020,
		.end	= 0xffd9002b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x920),
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

/* JPU */
static struct uio_info jpu_platform_data = {
	.name = "JPU",
	.version = "0",
	.irq = evt2irq(0x560),
};

static struct resource jpu_resources[] = {
	[0] = {
		.name	= "JPU",
		.start	= 0xfe980000,
		.end	= 0xfe9902d3,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device jpu_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 3,
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
	.irq = evt2irq(0xcc0),
};

static struct resource spu0_resources[] = {
	[0] = {
		.name	= "SPU2DSP0",
		.start	= 0xFE200000,
		.end	= 0xFE2FFFFF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device spu0_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 4,
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
	.irq = evt2irq(0xce0),
};

static struct resource spu1_resources[] = {
	[0] = {
		.name	= "SPU2DSP1",
		.start	= 0xFE300000,
		.end	= 0xFE3FFFFF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device spu1_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 5,
	.dev = {
		.platform_data	= &spu1_platform_data,
	},
	.resource	= spu1_resources,
	.num_resources	= ARRAY_SIZE(spu1_resources),
};

static struct platform_device *sh7724_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&cmt_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
	&dma0_device,
	&dma1_device,
	&rtc_device,
	&iic0_device,
	&iic1_device,
	&vpu_device,
	&veu0_device,
	&veu1_device,
	&beu0_device,
	&beu1_device,
	&jpu_device,
	&spu0_device,
	&spu1_device,
};

static int __init sh7724_devices_setup(void)
{
	platform_resource_setup_memory(&vpu_device, "vpu", 2 << 20);
	platform_resource_setup_memory(&veu0_device, "veu0", 2 << 20);
	platform_resource_setup_memory(&veu1_device, "veu1", 2 << 20);
	platform_resource_setup_memory(&jpu_device,  "jpu",  2 << 20);
	platform_resource_setup_memory(&spu0_device, "spu0", 2 << 20);
	platform_resource_setup_memory(&spu1_device, "spu1", 2 << 20);

	return platform_add_devices(sh7724_devices,
				    ARRAY_SIZE(sh7724_devices));
}
arch_initcall(sh7724_devices_setup);

static struct platform_device *sh7724_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&cmt_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh7724_early_devices,
				   ARRAY_SIZE(sh7724_early_devices));
}

#define RAMCR_CACHE_L2FC	0x0002
#define RAMCR_CACHE_L2E		0x0001
#define L2_CACHE_ENABLE		(RAMCR_CACHE_L2E|RAMCR_CACHE_L2FC)

void l2_cache_init(void)
{
	/* Enable L2 cache */
	__raw_writel(L2_CACHE_ENABLE, RAMCR);
}

enum {
	UNUSED = 0,
	ENABLED,
	DISABLED,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	HUDI,
	DMAC1A_DEI0, DMAC1A_DEI1, DMAC1A_DEI2, DMAC1A_DEI3,
	_2DG_TRI, _2DG_INI, _2DG_CEI,
	DMAC0A_DEI0, DMAC0A_DEI1, DMAC0A_DEI2, DMAC0A_DEI3,
	VIO_CEU0, VIO_BEU0, VIO_VEU1, VIO_VOU,
	SCIFA3,
	VPU,
	TPU,
	CEU1,
	BEU1,
	USB0, USB1,
	ATAPI,
	RTC_ATI, RTC_PRI, RTC_CUI,
	DMAC1B_DEI4, DMAC1B_DEI5, DMAC1B_DADERR,
	DMAC0B_DEI4, DMAC0B_DEI5, DMAC0B_DADERR,
	KEYSC,
	SCIF_SCIF0, SCIF_SCIF1, SCIF_SCIF2,
	VEU0,
	MSIOF_MSIOFI0, MSIOF_MSIOFI1,
	SPU_SPUI0, SPU_SPUI1,
	SCIFA4,
	ICB,
	ETHI,
	I2C1_ALI, I2C1_TACKI, I2C1_WAITI, I2C1_DTEI,
	I2C0_ALI, I2C0_TACKI, I2C0_WAITI, I2C0_DTEI,
	CMT,
	TSIF,
	FSI,
	SCIFA5,
	TMU0_TUNI0, TMU0_TUNI1, TMU0_TUNI2,
	IRDA,
	JPU,
	_2DDMAC,
	MMC_MMC2I, MMC_MMC3I,
	LCDC,
	TMU1_TUNI0, TMU1_TUNI1, TMU1_TUNI2,

	/* interrupt groups */
	DMAC1A, _2DG, DMAC0A, VIO, USB, RTC,
	DMAC1B, DMAC0B, I2C0, I2C1, SDHI0, SDHI1, SPU, MMCIF,
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

	INTC_VECT(DMAC0A_DEI0, 0x800),
	INTC_VECT(DMAC0A_DEI1, 0x820),
	INTC_VECT(DMAC0A_DEI2, 0x840),
	INTC_VECT(DMAC0A_DEI3, 0x860),

	INTC_VECT(VIO_CEU0, 0x880),
	INTC_VECT(VIO_BEU0, 0x8A0),
	INTC_VECT(VIO_VEU1, 0x8C0),
	INTC_VECT(VIO_VOU,  0x8E0),

	INTC_VECT(SCIFA3, 0x900),
	INTC_VECT(VPU,    0x980),
	INTC_VECT(TPU,    0x9A0),
	INTC_VECT(CEU1,   0x9E0),
	INTC_VECT(BEU1,   0xA00),
	INTC_VECT(USB0,   0xA20),
	INTC_VECT(USB1,   0xA40),
	INTC_VECT(ATAPI,  0xA60),

	INTC_VECT(RTC_ATI, 0xA80),
	INTC_VECT(RTC_PRI, 0xAA0),
	INTC_VECT(RTC_CUI, 0xAC0),

	INTC_VECT(DMAC1B_DEI4, 0xB00),
	INTC_VECT(DMAC1B_DEI5, 0xB20),
	INTC_VECT(DMAC1B_DADERR, 0xB40),

	INTC_VECT(DMAC0B_DEI4, 0xB80),
	INTC_VECT(DMAC0B_DEI5, 0xBA0),
	INTC_VECT(DMAC0B_DADERR, 0xBC0),

	INTC_VECT(KEYSC,      0xBE0),
	INTC_VECT(SCIF_SCIF0, 0xC00),
	INTC_VECT(SCIF_SCIF1, 0xC20),
	INTC_VECT(SCIF_SCIF2, 0xC40),
	INTC_VECT(VEU0,       0xC60),
	INTC_VECT(MSIOF_MSIOFI0, 0xC80),
	INTC_VECT(MSIOF_MSIOFI1, 0xCA0),
	INTC_VECT(SPU_SPUI0, 0xCC0),
	INTC_VECT(SPU_SPUI1, 0xCE0),
	INTC_VECT(SCIFA4,    0xD00),

	INTC_VECT(ICB,  0xD20),
	INTC_VECT(ETHI, 0xD60),

	INTC_VECT(I2C1_ALI, 0xD80),
	INTC_VECT(I2C1_TACKI, 0xDA0),
	INTC_VECT(I2C1_WAITI, 0xDC0),
	INTC_VECT(I2C1_DTEI, 0xDE0),

	INTC_VECT(I2C0_ALI, 0xE00),
	INTC_VECT(I2C0_TACKI, 0xE20),
	INTC_VECT(I2C0_WAITI, 0xE40),
	INTC_VECT(I2C0_DTEI, 0xE60),

	INTC_VECT(SDHI0, 0xE80),
	INTC_VECT(SDHI0, 0xEA0),
	INTC_VECT(SDHI0, 0xEC0),
	INTC_VECT(SDHI0, 0xEE0),

	INTC_VECT(CMT,    0xF00),
	INTC_VECT(TSIF,   0xF20),
	INTC_VECT(FSI,    0xF80),
	INTC_VECT(SCIFA5, 0xFA0),

	INTC_VECT(TMU0_TUNI0, 0x400),
	INTC_VECT(TMU0_TUNI1, 0x420),
	INTC_VECT(TMU0_TUNI2, 0x440),

	INTC_VECT(IRDA,    0x480),

	INTC_VECT(SDHI1, 0x4E0),
	INTC_VECT(SDHI1, 0x500),
	INTC_VECT(SDHI1, 0x520),

	INTC_VECT(JPU, 0x560),
	INTC_VECT(_2DDMAC, 0x4A0),

	INTC_VECT(MMC_MMC2I, 0x5A0),
	INTC_VECT(MMC_MMC3I, 0x5C0),

	INTC_VECT(LCDC, 0xF40),

	INTC_VECT(TMU1_TUNI0, 0x920),
	INTC_VECT(TMU1_TUNI1, 0x940),
	INTC_VECT(TMU1_TUNI2, 0x960),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(DMAC1A, DMAC1A_DEI0, DMAC1A_DEI1, DMAC1A_DEI2, DMAC1A_DEI3),
	INTC_GROUP(_2DG, _2DG_TRI, _2DG_INI, _2DG_CEI),
	INTC_GROUP(DMAC0A, DMAC0A_DEI0, DMAC0A_DEI1, DMAC0A_DEI2, DMAC0A_DEI3),
	INTC_GROUP(VIO, VIO_CEU0, VIO_BEU0, VIO_VEU1, VIO_VOU),
	INTC_GROUP(USB, USB0, USB1),
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(DMAC1B, DMAC1B_DEI4, DMAC1B_DEI5, DMAC1B_DADERR),
	INTC_GROUP(DMAC0B, DMAC0B_DEI4, DMAC0B_DEI5, DMAC0B_DADERR),
	INTC_GROUP(I2C0, I2C0_ALI, I2C0_TACKI, I2C0_WAITI, I2C0_DTEI),
	INTC_GROUP(I2C1, I2C1_ALI, I2C1_TACKI, I2C1_WAITI, I2C1_DTEI),
	INTC_GROUP(SPU, SPU_SPUI0, SPU_SPUI1),
	INTC_GROUP(MMCIF, MMC_MMC2I, MMC_MMC3I),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa4080080, 0xa40800c0, 8, /* IMR0 / IMCR0 */
	  { 0, TMU1_TUNI2, TMU1_TUNI1, TMU1_TUNI0,
	    0, ENABLED, ENABLED, ENABLED } },
	{ 0xa4080084, 0xa40800c4, 8, /* IMR1 / IMCR1 */
	  { VIO_VOU, VIO_VEU1, VIO_BEU0, VIO_CEU0,
	    DMAC0A_DEI3, DMAC0A_DEI2, DMAC0A_DEI1, DMAC0A_DEI0 } },
	{ 0xa4080088, 0xa40800c8, 8, /* IMR2 / IMCR2 */
	  { 0, 0, 0, VPU, ATAPI, ETHI, 0, SCIFA3 } },
	{ 0xa408008c, 0xa40800cc, 8, /* IMR3 / IMCR3 */
	  { DMAC1A_DEI3, DMAC1A_DEI2, DMAC1A_DEI1, DMAC1A_DEI0,
	    SPU_SPUI1, SPU_SPUI0, BEU1, IRDA } },
	{ 0xa4080090, 0xa40800d0, 8, /* IMR4 / IMCR4 */
	  { 0, TMU0_TUNI2, TMU0_TUNI1, TMU0_TUNI0,
	    JPU, 0, 0, LCDC } },
	{ 0xa4080094, 0xa40800d4, 8, /* IMR5 / IMCR5 */
	  { KEYSC, DMAC0B_DADERR, DMAC0B_DEI5, DMAC0B_DEI4,
	    VEU0, SCIF_SCIF2, SCIF_SCIF1, SCIF_SCIF0 } },
	{ 0xa4080098, 0xa40800d8, 8, /* IMR6 / IMCR6 */
	  { 0, 0, ICB, SCIFA4,
	    CEU1, 0, MSIOF_MSIOFI1, MSIOF_MSIOFI0 } },
	{ 0xa408009c, 0xa40800dc, 8, /* IMR7 / IMCR7 */
	  { I2C0_DTEI, I2C0_WAITI, I2C0_TACKI, I2C0_ALI,
	    I2C1_DTEI, I2C1_WAITI, I2C1_TACKI, I2C1_ALI } },
	{ 0xa40800a0, 0xa40800e0, 8, /* IMR8 / IMCR8 */
	  { DISABLED, ENABLED, ENABLED, ENABLED,
	    0, 0, SCIFA5, FSI } },
	{ 0xa40800a4, 0xa40800e4, 8, /* IMR9 / IMCR9 */
	  { 0, 0, 0, CMT, 0, USB1, USB0, 0 } },
	{ 0xa40800a8, 0xa40800e8, 8, /* IMR10 / IMCR10 */
	  { 0, DMAC1B_DADERR, DMAC1B_DEI5, DMAC1B_DEI4,
	    0, RTC_CUI, RTC_PRI, RTC_ATI } },
	{ 0xa40800ac, 0xa40800ec, 8, /* IMR11 / IMCR11 */
	  { 0, _2DG_CEI, _2DG_INI, _2DG_TRI,
	    0, TPU, 0, TSIF } },
	{ 0xa40800b0, 0xa40800f0, 8, /* IMR12 / IMCR12 */
	  { 0, 0, MMC_MMC3I, MMC_MMC2I, 0, 0, 0, _2DDMAC } },
	{ 0xa4140044, 0xa4140064, 8, /* INTMSK00 / INTMSKCLR00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xa4080000, 0, 16, 4, /* IPRA */ { TMU0_TUNI0, TMU0_TUNI1,
					     TMU0_TUNI2, IRDA } },
	{ 0xa4080004, 0, 16, 4, /* IPRB */ { JPU, LCDC, DMAC1A, BEU1 } },
	{ 0xa4080008, 0, 16, 4, /* IPRC */ { TMU1_TUNI0, TMU1_TUNI1,
					     TMU1_TUNI2, SPU } },
	{ 0xa408000c, 0, 16, 4, /* IPRD */ { 0, MMCIF, 0, ATAPI } },
	{ 0xa4080010, 0, 16, 4, /* IPRE */ { DMAC0A, VIO, SCIFA3, VPU } },
	{ 0xa4080014, 0, 16, 4, /* IPRF */ { KEYSC, DMAC0B, USB, CMT } },
	{ 0xa4080018, 0, 16, 4, /* IPRG */ { SCIF_SCIF0, SCIF_SCIF1,
					     SCIF_SCIF2, VEU0 } },
	{ 0xa408001c, 0, 16, 4, /* IPRH */ { MSIOF_MSIOFI0, MSIOF_MSIOFI1,
					     I2C1, I2C0 } },
	{ 0xa4080020, 0, 16, 4, /* IPRI */ { SCIFA4, ICB, TSIF, _2DG } },
	{ 0xa4080024, 0, 16, 4, /* IPRJ */ { CEU1, ETHI, FSI, SDHI1 } },
	{ 0xa4080028, 0, 16, 4, /* IPRK */ { RTC, DMAC1B, 0, SDHI0 } },
	{ 0xa408002c, 0, 16, 4, /* IPRL */ { SCIFA5, 0, TPU, _2DDMAC } },
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

static struct intc_desc intc_desc __initdata = {
	.name = "sh7724",
	.force_enable = ENABLED,
	.force_disable = DISABLED,
	.hw = INTC_HW_DESC(vectors, groups, mask_registers,
			   prio_registers, sense_registers, ack_registers),
};

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

static struct {
	/* BSC */
	unsigned long mmselr;
	unsigned long cs0bcr;
	unsigned long cs4bcr;
	unsigned long cs5abcr;
	unsigned long cs5bbcr;
	unsigned long cs6abcr;
	unsigned long cs6bbcr;
	unsigned long cs4wcr;
	unsigned long cs5awcr;
	unsigned long cs5bwcr;
	unsigned long cs6awcr;
	unsigned long cs6bwcr;
	/* INTC */
	unsigned short ipra;
	unsigned short iprb;
	unsigned short iprc;
	unsigned short iprd;
	unsigned short ipre;
	unsigned short iprf;
	unsigned short iprg;
	unsigned short iprh;
	unsigned short ipri;
	unsigned short iprj;
	unsigned short iprk;
	unsigned short iprl;
	unsigned char imr0;
	unsigned char imr1;
	unsigned char imr2;
	unsigned char imr3;
	unsigned char imr4;
	unsigned char imr5;
	unsigned char imr6;
	unsigned char imr7;
	unsigned char imr8;
	unsigned char imr9;
	unsigned char imr10;
	unsigned char imr11;
	unsigned char imr12;
	/* RWDT */
	unsigned short rwtcnt;
	unsigned short rwtcsr;
	/* CPG */
	unsigned long irdaclk;
	unsigned long spuclk;
} sh7724_rstandby_state;

static int sh7724_pre_sleep_notifier_call(struct notifier_block *nb,
					  unsigned long flags, void *unused)
{
	if (!(flags & SUSP_SH_RSTANDBY))
		return NOTIFY_DONE;

	/* BCR */
	sh7724_rstandby_state.mmselr = __raw_readl(0xff800020); /* MMSELR */
	sh7724_rstandby_state.mmselr |= 0xa5a50000;
	sh7724_rstandby_state.cs0bcr = __raw_readl(0xfec10004); /* CS0BCR */
	sh7724_rstandby_state.cs4bcr = __raw_readl(0xfec10010); /* CS4BCR */
	sh7724_rstandby_state.cs5abcr = __raw_readl(0xfec10014); /* CS5ABCR */
	sh7724_rstandby_state.cs5bbcr = __raw_readl(0xfec10018); /* CS5BBCR */
	sh7724_rstandby_state.cs6abcr = __raw_readl(0xfec1001c); /* CS6ABCR */
	sh7724_rstandby_state.cs6bbcr = __raw_readl(0xfec10020); /* CS6BBCR */
	sh7724_rstandby_state.cs4wcr = __raw_readl(0xfec10030); /* CS4WCR */
	sh7724_rstandby_state.cs5awcr = __raw_readl(0xfec10034); /* CS5AWCR */
	sh7724_rstandby_state.cs5bwcr = __raw_readl(0xfec10038); /* CS5BWCR */
	sh7724_rstandby_state.cs6awcr = __raw_readl(0xfec1003c); /* CS6AWCR */
	sh7724_rstandby_state.cs6bwcr = __raw_readl(0xfec10040); /* CS6BWCR */

	/* INTC */
	sh7724_rstandby_state.ipra = __raw_readw(0xa4080000); /* IPRA */
	sh7724_rstandby_state.iprb = __raw_readw(0xa4080004); /* IPRB */
	sh7724_rstandby_state.iprc = __raw_readw(0xa4080008); /* IPRC */
	sh7724_rstandby_state.iprd = __raw_readw(0xa408000c); /* IPRD */
	sh7724_rstandby_state.ipre = __raw_readw(0xa4080010); /* IPRE */
	sh7724_rstandby_state.iprf = __raw_readw(0xa4080014); /* IPRF */
	sh7724_rstandby_state.iprg = __raw_readw(0xa4080018); /* IPRG */
	sh7724_rstandby_state.iprh = __raw_readw(0xa408001c); /* IPRH */
	sh7724_rstandby_state.ipri = __raw_readw(0xa4080020); /* IPRI */
	sh7724_rstandby_state.iprj = __raw_readw(0xa4080024); /* IPRJ */
	sh7724_rstandby_state.iprk = __raw_readw(0xa4080028); /* IPRK */
	sh7724_rstandby_state.iprl = __raw_readw(0xa408002c); /* IPRL */
	sh7724_rstandby_state.imr0 = __raw_readb(0xa4080080); /* IMR0 */
	sh7724_rstandby_state.imr1 = __raw_readb(0xa4080084); /* IMR1 */
	sh7724_rstandby_state.imr2 = __raw_readb(0xa4080088); /* IMR2 */
	sh7724_rstandby_state.imr3 = __raw_readb(0xa408008c); /* IMR3 */
	sh7724_rstandby_state.imr4 = __raw_readb(0xa4080090); /* IMR4 */
	sh7724_rstandby_state.imr5 = __raw_readb(0xa4080094); /* IMR5 */
	sh7724_rstandby_state.imr6 = __raw_readb(0xa4080098); /* IMR6 */
	sh7724_rstandby_state.imr7 = __raw_readb(0xa408009c); /* IMR7 */
	sh7724_rstandby_state.imr8 = __raw_readb(0xa40800a0); /* IMR8 */
	sh7724_rstandby_state.imr9 = __raw_readb(0xa40800a4); /* IMR9 */
	sh7724_rstandby_state.imr10 = __raw_readb(0xa40800a8); /* IMR10 */
	sh7724_rstandby_state.imr11 = __raw_readb(0xa40800ac); /* IMR11 */
	sh7724_rstandby_state.imr12 = __raw_readb(0xa40800b0); /* IMR12 */

	/* RWDT */
	sh7724_rstandby_state.rwtcnt = __raw_readb(0xa4520000); /* RWTCNT */
	sh7724_rstandby_state.rwtcnt |= 0x5a00;
	sh7724_rstandby_state.rwtcsr = __raw_readb(0xa4520004); /* RWTCSR */
	sh7724_rstandby_state.rwtcsr |= 0xa500;
	__raw_writew(sh7724_rstandby_state.rwtcsr & 0x07, 0xa4520004);

	/* CPG */
	sh7724_rstandby_state.irdaclk = __raw_readl(0xa4150018); /* IRDACLKCR */
	sh7724_rstandby_state.spuclk = __raw_readl(0xa415003c); /* SPUCLKCR */

	return NOTIFY_DONE;
}

static int sh7724_post_sleep_notifier_call(struct notifier_block *nb,
					   unsigned long flags, void *unused)
{
	if (!(flags & SUSP_SH_RSTANDBY))
		return NOTIFY_DONE;

	/* BCR */
	__raw_writel(sh7724_rstandby_state.mmselr, 0xff800020); /* MMSELR */
	__raw_writel(sh7724_rstandby_state.cs0bcr, 0xfec10004); /* CS0BCR */
	__raw_writel(sh7724_rstandby_state.cs4bcr, 0xfec10010); /* CS4BCR */
	__raw_writel(sh7724_rstandby_state.cs5abcr, 0xfec10014); /* CS5ABCR */
	__raw_writel(sh7724_rstandby_state.cs5bbcr, 0xfec10018); /* CS5BBCR */
	__raw_writel(sh7724_rstandby_state.cs6abcr, 0xfec1001c); /* CS6ABCR */
	__raw_writel(sh7724_rstandby_state.cs6bbcr, 0xfec10020); /* CS6BBCR */
	__raw_writel(sh7724_rstandby_state.cs4wcr, 0xfec10030); /* CS4WCR */
	__raw_writel(sh7724_rstandby_state.cs5awcr, 0xfec10034); /* CS5AWCR */
	__raw_writel(sh7724_rstandby_state.cs5bwcr, 0xfec10038); /* CS5BWCR */
	__raw_writel(sh7724_rstandby_state.cs6awcr, 0xfec1003c); /* CS6AWCR */
	__raw_writel(sh7724_rstandby_state.cs6bwcr, 0xfec10040); /* CS6BWCR */

	/* INTC */
	__raw_writew(sh7724_rstandby_state.ipra, 0xa4080000); /* IPRA */
	__raw_writew(sh7724_rstandby_state.iprb, 0xa4080004); /* IPRB */
	__raw_writew(sh7724_rstandby_state.iprc, 0xa4080008); /* IPRC */
	__raw_writew(sh7724_rstandby_state.iprd, 0xa408000c); /* IPRD */
	__raw_writew(sh7724_rstandby_state.ipre, 0xa4080010); /* IPRE */
	__raw_writew(sh7724_rstandby_state.iprf, 0xa4080014); /* IPRF */
	__raw_writew(sh7724_rstandby_state.iprg, 0xa4080018); /* IPRG */
	__raw_writew(sh7724_rstandby_state.iprh, 0xa408001c); /* IPRH */
	__raw_writew(sh7724_rstandby_state.ipri, 0xa4080020); /* IPRI */
	__raw_writew(sh7724_rstandby_state.iprj, 0xa4080024); /* IPRJ */
	__raw_writew(sh7724_rstandby_state.iprk, 0xa4080028); /* IPRK */
	__raw_writew(sh7724_rstandby_state.iprl, 0xa408002c); /* IPRL */
	__raw_writeb(sh7724_rstandby_state.imr0, 0xa4080080); /* IMR0 */
	__raw_writeb(sh7724_rstandby_state.imr1, 0xa4080084); /* IMR1 */
	__raw_writeb(sh7724_rstandby_state.imr2, 0xa4080088); /* IMR2 */
	__raw_writeb(sh7724_rstandby_state.imr3, 0xa408008c); /* IMR3 */
	__raw_writeb(sh7724_rstandby_state.imr4, 0xa4080090); /* IMR4 */
	__raw_writeb(sh7724_rstandby_state.imr5, 0xa4080094); /* IMR5 */
	__raw_writeb(sh7724_rstandby_state.imr6, 0xa4080098); /* IMR6 */
	__raw_writeb(sh7724_rstandby_state.imr7, 0xa408009c); /* IMR7 */
	__raw_writeb(sh7724_rstandby_state.imr8, 0xa40800a0); /* IMR8 */
	__raw_writeb(sh7724_rstandby_state.imr9, 0xa40800a4); /* IMR9 */
	__raw_writeb(sh7724_rstandby_state.imr10, 0xa40800a8); /* IMR10 */
	__raw_writeb(sh7724_rstandby_state.imr11, 0xa40800ac); /* IMR11 */
	__raw_writeb(sh7724_rstandby_state.imr12, 0xa40800b0); /* IMR12 */

	/* RWDT */
	__raw_writew(sh7724_rstandby_state.rwtcnt, 0xa4520000); /* RWTCNT */
	__raw_writew(sh7724_rstandby_state.rwtcsr, 0xa4520004); /* RWTCSR */

	/* CPG */
	__raw_writel(sh7724_rstandby_state.irdaclk, 0xa4150018); /* IRDACLKCR */
	__raw_writel(sh7724_rstandby_state.spuclk, 0xa415003c); /* SPUCLKCR */

	return NOTIFY_DONE;
}

static struct notifier_block sh7724_pre_sleep_notifier = {
	.notifier_call = sh7724_pre_sleep_notifier_call,
	.priority = SH_MOBILE_PRE(SH_MOBILE_SLEEP_CPU),
};

static struct notifier_block sh7724_post_sleep_notifier = {
	.notifier_call = sh7724_post_sleep_notifier_call,
	.priority = SH_MOBILE_POST(SH_MOBILE_SLEEP_CPU),
};

static int __init sh7724_sleep_setup(void)
{
	atomic_notifier_chain_register(&sh_mobile_pre_sleep_notifier_list,
				       &sh7724_pre_sleep_notifier);

	atomic_notifier_chain_register(&sh_mobile_post_sleep_notifier_list,
				       &sh7724_post_sleep_notifier);
	return 0;
}
arch_initcall(sh7724_sleep_setup);

