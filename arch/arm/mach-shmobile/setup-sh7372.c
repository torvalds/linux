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
#include <linux/sh_timer.h>
#include <linux/pm_domain.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/sh_ipmmu.h>

#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "dma-register.h"
#include "intc.h"
#include "irqs.h"
#include "pm-rmobile.h"
#include "sh7372.h"

static struct map_desc sh7372_io_desc[] __initdata = {
	/* create a 1:1 identity mapping for 0xe6xxxxxx
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
}

/* PFC */
static struct resource sh7372_pfc_resources[] = {
	[0] = {
		.start	= 0xe6050000,
		.end	= 0xe6057fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0xe605800c,
		.end	= 0xe6058027,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device sh7372_pfc_device = {
	.name		= "pfc-sh7372",
	.id		= -1,
	.resource	= sh7372_pfc_resources,
	.num_resources	= ARRAY_SIZE(sh7372_pfc_resources),
};

void __init sh7372_pinmux_init(void)
{
	platform_device_register(&sh7372_pfc_device);
}

/* SCIF */
#define SH7372_SCIF(scif_type, index, baseaddr, irq)		\
static struct plat_sci_port scif##index##_platform_data = {	\
	.type		= scif_type,				\
	.flags		= UPF_BOOT_AUTOCONF,			\
	.scscr		= SCSCR_RE | SCSCR_TE,			\
};								\
								\
static struct resource scif##index##_resources[] = {		\
	DEFINE_RES_MEM(baseaddr, 0x100),			\
	DEFINE_RES_IRQ(irq),					\
};								\
								\
static struct platform_device scif##index##_device = {		\
	.name		= "sh-sci",				\
	.id		= index,				\
	.resource	= scif##index##_resources,		\
	.num_resources	= ARRAY_SIZE(scif##index##_resources),	\
	.dev		= {					\
		.platform_data	= &scif##index##_platform_data,	\
	},							\
}

SH7372_SCIF(PORT_SCIFA, 0, 0xe6c40000, evt2irq(0x0c00));
SH7372_SCIF(PORT_SCIFA, 1, 0xe6c50000, evt2irq(0x0c20));
SH7372_SCIF(PORT_SCIFA, 2, 0xe6c60000, evt2irq(0x0c40));
SH7372_SCIF(PORT_SCIFA, 3, 0xe6c70000, evt2irq(0x0c60));
SH7372_SCIF(PORT_SCIFA, 4, 0xe6c80000, evt2irq(0x0d20));
SH7372_SCIF(PORT_SCIFA, 5, 0xe6cb0000, evt2irq(0x0d40));
SH7372_SCIF(PORT_SCIFB, 6, 0xe6c30000, evt2irq(0x0d60));

/* CMT */
static struct sh_timer_config cmt2_platform_data = {
	.channels_mask = 0x20,
};

static struct resource cmt2_resources[] = {
	DEFINE_RES_MEM(0xe6130000, 0x50),
	DEFINE_RES_IRQ(evt2irq(0x0b80)),
};

static struct platform_device cmt2_device = {
	.name		= "sh-cmt-32-fast",
	.id		= 2,
	.dev = {
		.platform_data	= &cmt2_platform_data,
	},
	.resource	= cmt2_resources,
	.num_resources	= ARRAY_SIZE(cmt2_resources),
};

/* TMU */
static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(0xfff60000, 0x2c),
	DEFINE_RES_IRQ(intcs_evt2irq(0xe80)),
	DEFINE_RES_IRQ(intcs_evt2irq(0xea0)),
	DEFINE_RES_IRQ(intcs_evt2irq(0xec0)),
};

static struct platform_device tmu0_device = {
	.name		= "sh-tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
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
static const struct sh_dmae_slave_config sh7372_dmae_slaves[] = {
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
		.addr		= 0xe6c30040,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_RX,
		.addr		= 0xe6c30060,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3e,
	}, {
		.slave_id	= SHDMA_SLAVE_FLCTL0_TX,
		.addr		= 0xe6a30050,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x83,
	}, {
		.slave_id	= SHDMA_SLAVE_FLCTL0_RX,
		.addr		= 0xe6a30050,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x83,
	}, {
		.slave_id	= SHDMA_SLAVE_FLCTL1_TX,
		.addr		= 0xe6a30060,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x87,
	}, {
		.slave_id	= SHDMA_SLAVE_FLCTL1_RX,
		.addr		= 0xe6a30060,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x87,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_TX,
		.addr		= 0xe6850030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc1,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_RX,
		.addr		= 0xe6850030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc2,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_TX,
		.addr		= 0xe6860030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc9,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_RX,
		.addr		= 0xe6860030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xca,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_TX,
		.addr		= 0xe6870030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xcd,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_RX,
		.addr		= 0xe6870030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xce,
	}, {
		.slave_id	= SHDMA_SLAVE_FSIA_TX,
		.addr		= 0xfe1f0024,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xb1,
	}, {
		.slave_id	= SHDMA_SLAVE_FSIA_RX,
		.addr		= 0xfe1f0020,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xb2,
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

#define SH7372_CHCLR (0x220 - 0x20)

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

static struct sh_dmae_pdata dma_platform_data = {
	.slave		= sh7372_dmae_slaves,
	.slave_num	= ARRAY_SIZE(sh7372_dmae_slaves),
	.channel	= sh7372_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7372_dmae_channels),
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
		.chcr		= USBTS_INDEX2VAL(USBTS_XMIT_SZ_8BYTE),
	}, {
		.slave_id	= SHDMA_SLAVE_USB0_RX,
		.chcr		= USBTS_INDEX2VAL(USBTS_XMIT_SZ_8BYTE),
	},
};

static struct sh_dmae_pdata usb_dma0_platform_data = {
	.slave		= sh7372_usb_dmae0_slaves,
	.slave_num	= ARRAY_SIZE(sh7372_usb_dmae0_slaves),
	.channel	= sh7372_usb_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7372_usb_dmae_channels),
	.ts_low_shift	= USBTS_LOW_SHIFT,
	.ts_low_mask	= USBTS_LOW_BIT << USBTS_LOW_SHIFT,
	.ts_high_shift	= USBTS_HI_SHIFT,
	.ts_high_mask	= USBTS_HI_BIT << USBTS_HI_SHIFT,
	.ts_shift	= dma_usbts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_usbts_shift),
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
		.chcr		= USBTS_INDEX2VAL(USBTS_XMIT_SZ_8BYTE),
	}, {
		.slave_id	= SHDMA_SLAVE_USB1_RX,
		.chcr		= USBTS_INDEX2VAL(USBTS_XMIT_SZ_8BYTE),
	},
};

static struct sh_dmae_pdata usb_dma1_platform_data = {
	.slave		= sh7372_usb_dmae1_slaves,
	.slave_num	= ARRAY_SIZE(sh7372_usb_dmae1_slaves),
	.channel	= sh7372_usb_dmae_channels,
	.channel_num	= ARRAY_SIZE(sh7372_usb_dmae_channels),
	.ts_low_shift	= USBTS_LOW_SHIFT,
	.ts_low_mask	= USBTS_LOW_BIT << USBTS_LOW_SHIFT,
	.ts_high_shift	= USBTS_HI_SHIFT,
	.ts_high_mask	= USBTS_HI_BIT << USBTS_HI_SHIFT,
	.ts_shift	= dma_usbts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_usbts_shift),
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

/* IPMMUI (an IPMMU module for ICB/LMB) */
static struct resource ipmmu_resources[] = {
	[0] = {
		.name	= "IPMMUI",
		.start	= 0xfe951000,
		.end	= 0xfe9510ff,
		.flags	= IORESOURCE_MEM,
	},
};

static const char * const ipmmu_dev_names[] = {
	"sh_mobile_lcdc_fb.0",
	"sh_mobile_lcdc_fb.1",
	"sh_mobile_ceu.0",
	"uio_pdrv_genirq.0",
	"uio_pdrv_genirq.1",
	"uio_pdrv_genirq.2",
	"uio_pdrv_genirq.3",
	"uio_pdrv_genirq.4",
	"uio_pdrv_genirq.5",
};

static struct shmobile_ipmmu_platform_data ipmmu_platform_data = {
	.dev_names = ipmmu_dev_names,
	.num_dev_names = ARRAY_SIZE(ipmmu_dev_names),
};

static struct platform_device ipmmu_device = {
	.name           = "ipmmu",
	.id             = -1,
	.dev = {
		.platform_data = &ipmmu_platform_data,
	},
	.resource       = ipmmu_resources,
	.num_resources  = ARRAY_SIZE(ipmmu_resources),
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
	&tmu0_device,
	&ipmmu_device,
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
	static struct pm_domain_device domain_devices[] __initdata = {
		{ "A3RV", &vpu_device, },
		{ "A4MP", &spu0_device, },
		{ "A4MP", &spu1_device, },
		{ "A3SP", &scif0_device, },
		{ "A3SP", &scif1_device, },
		{ "A3SP", &scif2_device, },
		{ "A3SP", &scif3_device, },
		{ "A3SP", &scif4_device, },
		{ "A3SP", &scif5_device, },
		{ "A3SP", &scif6_device, },
		{ "A3SP", &iic1_device, },
		{ "A3SP", &dma0_device, },
		{ "A3SP", &dma1_device, },
		{ "A3SP", &dma2_device, },
		{ "A3SP", &usb_dma0_device, },
		{ "A3SP", &usb_dma1_device, },
		{ "A4R", &iic0_device, },
		{ "A4R", &veu0_device, },
		{ "A4R", &veu1_device, },
		{ "A4R", &veu2_device, },
		{ "A4R", &veu3_device, },
		{ "A4R", &jpu_device, },
		{ "A4R", &tmu0_device, },
	};

	sh7372_init_pm_domains();

	platform_add_devices(sh7372_early_devices,
			    ARRAY_SIZE(sh7372_early_devices));

	platform_add_devices(sh7372_late_devices,
			    ARRAY_SIZE(sh7372_late_devices));

	rmobile_add_devices_to_domains(domain_devices,
				       ARRAY_SIZE(domain_devices));
}

void __init sh7372_earlytimer_init(void)
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
}

#ifdef CONFIG_USE_OF

void __init sh7372_add_early_devices_dt(void)
{
	shmobile_init_delay();

	sh7372_add_early_devices();
}

void __init sh7372_add_standard_devices_dt(void)
{
	/* clocks are setup late during boot in the case of DT */
	sh7372_clock_init();

	platform_add_devices(sh7372_early_devices,
			    ARRAY_SIZE(sh7372_early_devices));

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *sh7372_boards_compat_dt[] __initdata = {
	"renesas,sh7372",
	NULL,
};

DT_MACHINE_START(SH7372_DT, "Generic SH7372 (Flattened Device Tree)")
	.map_io		= sh7372_map_io,
	.init_early	= sh7372_add_early_devices_dt,
	.init_irq	= sh7372_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= sh7372_add_standard_devices_dt,
	.dt_compat	= sh7372_boards_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
