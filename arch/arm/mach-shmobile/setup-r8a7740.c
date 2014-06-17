/*
 * R8A7740 processor support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/platform_data/irq-renesas-intc-irqpin.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/serial_sci.h>
#include <linux/sh_dma.h>
#include <linux/sh_timer.h>
#include <linux/platform_data/sh_ipmmu.h>
#include <mach/r8a7740.h>
#include <mach/pm-rmobile.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include "dma-register.h"
#include "irqs.h"

static struct map_desc r8a7740_io_desc[] __initdata = {
	 /*
	  * for CPGA/INTC/PFC
	  * 0xe6000000-0xefffffff -> 0xe6000000-0xefffffff
	  */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 160 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
#ifdef CONFIG_CACHE_L2X0
	/*
	 * for l2x0_init()
	 * 0xf0100000-0xf0101000 -> 0xf0002000-0xf0003000
	 */
	{
		.virtual	= 0xf0002000,
		.pfn		= __phys_to_pfn(0xf0100000),
		.length		= PAGE_SIZE,
		.type		= MT_DEVICE_NONSHARED
	},
#endif
};

void __init r8a7740_map_io(void)
{
	iotable_init(r8a7740_io_desc, ARRAY_SIZE(r8a7740_io_desc));
}

/* PFC */
static const struct resource pfc_resources[] = {
	DEFINE_RES_MEM(0xe6050000, 0x8000),
	DEFINE_RES_MEM(0xe605800c, 0x0020),
};

void __init r8a7740_pinmux_init(void)
{
	platform_device_register_simple("pfc-r8a7740", -1, pfc_resources,
					ARRAY_SIZE(pfc_resources));
}

static struct renesas_intc_irqpin_config irqpin0_platform_data = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ7 */
};

static struct resource irqpin0_resources[] = {
	DEFINE_RES_MEM(0xe6900000, 4), /* ICR1A */
	DEFINE_RES_MEM(0xe6900010, 4), /* INTPRI00A */
	DEFINE_RES_MEM(0xe6900020, 1), /* INTREQ00A */
	DEFINE_RES_MEM(0xe6900040, 1), /* INTMSK00A */
	DEFINE_RES_MEM(0xe6900060, 1), /* INTMSKCLR00A */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ3 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ4 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ5 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ6 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ7 */
};

static struct platform_device irqpin0_device = {
	.name		= "renesas_intc_irqpin",
	.id		= 0,
	.resource	= irqpin0_resources,
	.num_resources	= ARRAY_SIZE(irqpin0_resources),
	.dev		= {
		.platform_data  = &irqpin0_platform_data,
	},
};

static struct renesas_intc_irqpin_config irqpin1_platform_data = {
	.irq_base = irq_pin(8), /* IRQ8 -> IRQ15 */
};

static struct resource irqpin1_resources[] = {
	DEFINE_RES_MEM(0xe6900004, 4), /* ICR2A */
	DEFINE_RES_MEM(0xe6900014, 4), /* INTPRI10A */
	DEFINE_RES_MEM(0xe6900024, 1), /* INTREQ10A */
	DEFINE_RES_MEM(0xe6900044, 1), /* INTMSK10A */
	DEFINE_RES_MEM(0xe6900064, 1), /* INTMSKCLR10A */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ8 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ9 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ10 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ11 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ12 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ13 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ14 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ15 */
};

static struct platform_device irqpin1_device = {
	.name		= "renesas_intc_irqpin",
	.id		= 1,
	.resource	= irqpin1_resources,
	.num_resources	= ARRAY_SIZE(irqpin1_resources),
	.dev		= {
		.platform_data  = &irqpin1_platform_data,
	},
};

static struct renesas_intc_irqpin_config irqpin2_platform_data = {
	.irq_base = irq_pin(16), /* IRQ16 -> IRQ23 */
};

static struct resource irqpin2_resources[] = {
	DEFINE_RES_MEM(0xe6900008, 4), /* ICR3A */
	DEFINE_RES_MEM(0xe6900018, 4), /* INTPRI30A */
	DEFINE_RES_MEM(0xe6900028, 1), /* INTREQ30A */
	DEFINE_RES_MEM(0xe6900048, 1), /* INTMSK30A */
	DEFINE_RES_MEM(0xe6900068, 1), /* INTMSKCLR30A */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ16 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ17 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ18 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ19 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ20 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ21 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ22 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ23 */
};

static struct platform_device irqpin2_device = {
	.name		= "renesas_intc_irqpin",
	.id		= 2,
	.resource	= irqpin2_resources,
	.num_resources	= ARRAY_SIZE(irqpin2_resources),
	.dev		= {
		.platform_data  = &irqpin2_platform_data,
	},
};

static struct renesas_intc_irqpin_config irqpin3_platform_data = {
	.irq_base = irq_pin(24), /* IRQ24 -> IRQ31 */
};

static struct resource irqpin3_resources[] = {
	DEFINE_RES_MEM(0xe690000c, 4), /* ICR3A */
	DEFINE_RES_MEM(0xe690001c, 4), /* INTPRI30A */
	DEFINE_RES_MEM(0xe690002c, 1), /* INTREQ30A */
	DEFINE_RES_MEM(0xe690004c, 1), /* INTMSK30A */
	DEFINE_RES_MEM(0xe690006c, 1), /* INTMSKCLR30A */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ24 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ25 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ26 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ27 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ28 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ29 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ30 */
	DEFINE_RES_IRQ(gic_spi(149)), /* IRQ31 */
};

static struct platform_device irqpin3_device = {
	.name		= "renesas_intc_irqpin",
	.id		= 3,
	.resource	= irqpin3_resources,
	.num_resources	= ARRAY_SIZE(irqpin3_resources),
	.dev		= {
		.platform_data  = &irqpin3_platform_data,
	},
};

/* SCIF */
#define R8A7740_SCIF(scif_type, index, baseaddr, irq)		\
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

R8A7740_SCIF(PORT_SCIFA, 0, 0xe6c40000, gic_spi(100));
R8A7740_SCIF(PORT_SCIFA, 1, 0xe6c50000, gic_spi(101));
R8A7740_SCIF(PORT_SCIFA, 2, 0xe6c60000, gic_spi(102));
R8A7740_SCIF(PORT_SCIFA, 3, 0xe6c70000, gic_spi(103));
R8A7740_SCIF(PORT_SCIFA, 4, 0xe6c80000, gic_spi(104));
R8A7740_SCIF(PORT_SCIFA, 5, 0xe6cb0000, gic_spi(105));
R8A7740_SCIF(PORT_SCIFA, 6, 0xe6cc0000, gic_spi(106));
R8A7740_SCIF(PORT_SCIFA, 7, 0xe6cd0000, gic_spi(107));
R8A7740_SCIF(PORT_SCIFB, 8, 0xe6c30000, gic_spi(108));

/* CMT */
static struct sh_timer_config cmt1_platform_data = {
	.channels_mask = 0x3f,
};

static struct resource cmt1_resources[] = {
	DEFINE_RES_MEM(0xe6138000, 0x170),
	DEFINE_RES_IRQ(gic_spi(58)),
};

static struct platform_device cmt1_device = {
	.name		= "sh-cmt-48",
	.id		= 1,
	.dev = {
		.platform_data	= &cmt1_platform_data,
	},
	.resource	= cmt1_resources,
	.num_resources	= ARRAY_SIZE(cmt1_resources),
};

/* TMU */
static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(0xfff80000, 0x2c),
	DEFINE_RES_IRQ(gic_spi(198)),
	DEFINE_RES_IRQ(gic_spi(199)),
	DEFINE_RES_IRQ(gic_spi(200)),
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

static struct platform_device *r8a7740_devices_dt[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&scif8_device,
	&cmt1_device,
};

static struct platform_device *r8a7740_early_devices[] __initdata = {
	&irqpin0_device,
	&irqpin1_device,
	&irqpin2_device,
	&irqpin3_device,
	&tmu0_device,
	&ipmmu_device,
};

/* DMA */
static const struct sh_dmae_slave_config r8a7740_dmae_slaves[] = {
	{
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
		.slave_id	= SHDMA_SLAVE_FSIB_TX,
		.addr		= 0xfe1f0064,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xb5,
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

#define DMA_CHANNEL(a, b, c)			\
{						\
	.offset		= a,			\
	.dmars		= b,			\
	.dmars_bit	= c,			\
	.chclr_offset	= (0x220 - 0x20) + a	\
}

static const struct sh_dmae_channel r8a7740_dmae_channels[] = {
	DMA_CHANNEL(0x00, 0, 0),
	DMA_CHANNEL(0x10, 0, 8),
	DMA_CHANNEL(0x20, 4, 0),
	DMA_CHANNEL(0x30, 4, 8),
	DMA_CHANNEL(0x50, 8, 0),
	DMA_CHANNEL(0x60, 8, 8),
};

static struct sh_dmae_pdata dma_platform_data = {
	.slave		= r8a7740_dmae_slaves,
	.slave_num	= ARRAY_SIZE(r8a7740_dmae_slaves),
	.channel	= r8a7740_dmae_channels,
	.channel_num	= ARRAY_SIZE(r8a7740_dmae_channels),
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
static struct resource r8a7740_dmae0_resources[] = {
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
		.start	= gic_spi(34),
		.end	= gic_spi(34),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= gic_spi(28),
		.end	= gic_spi(33),
		.flags	= IORESOURCE_IRQ,
	},
};

/* Resource order important! */
static struct resource r8a7740_dmae1_resources[] = {
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
		.start	= gic_spi(41),
		.end	= gic_spi(41),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= gic_spi(35),
		.end	= gic_spi(40),
		.flags	= IORESOURCE_IRQ,
	},
};

/* Resource order important! */
static struct resource r8a7740_dmae2_resources[] = {
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
		.start	= gic_spi(48),
		.end	= gic_spi(48),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-5 */
		.start	= gic_spi(42),
		.end	= gic_spi(47),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 0,
	.resource	= r8a7740_dmae0_resources,
	.num_resources	= ARRAY_SIZE(r8a7740_dmae0_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

static struct platform_device dma1_device = {
	.name		= "sh-dma-engine",
	.id		= 1,
	.resource	= r8a7740_dmae1_resources,
	.num_resources	= ARRAY_SIZE(r8a7740_dmae1_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

static struct platform_device dma2_device = {
	.name		= "sh-dma-engine",
	.id		= 2,
	.resource	= r8a7740_dmae2_resources,
	.num_resources	= ARRAY_SIZE(r8a7740_dmae2_resources),
	.dev		= {
		.platform_data	= &dma_platform_data,
	},
};

/* USB-DMAC */
static const struct sh_dmae_channel r8a7740_usb_dma_channels[] = {
	{
		.offset = 0,
	}, {
		.offset = 0x20,
	},
};

static const struct sh_dmae_slave_config r8a7740_usb_dma_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_USBHS_TX,
		.chcr		= USBTS_INDEX2VAL(USBTS_XMIT_SZ_8BYTE),
	}, {
		.slave_id	= SHDMA_SLAVE_USBHS_RX,
		.chcr		= USBTS_INDEX2VAL(USBTS_XMIT_SZ_8BYTE),
	},
};

static struct sh_dmae_pdata usb_dma_platform_data = {
	.slave		= r8a7740_usb_dma_slaves,
	.slave_num	= ARRAY_SIZE(r8a7740_usb_dma_slaves),
	.channel	= r8a7740_usb_dma_channels,
	.channel_num	= ARRAY_SIZE(r8a7740_usb_dma_channels),
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

static struct resource r8a7740_usb_dma_resources[] = {
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
		.start	= gic_spi(49),
		.end	= gic_spi(49),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb_dma_device = {
	.name		= "sh-dma-engine",
	.id		= 3,
	.resource	= r8a7740_usb_dma_resources,
	.num_resources	= ARRAY_SIZE(r8a7740_usb_dma_resources),
	.dev		= {
		.platform_data	= &usb_dma_platform_data,
	},
};

/* I2C */
static struct resource i2c0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start	= 0xfff20000,
		.end	= 0xfff20425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(201),
		.end	= gic_spi(204),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start	= 0xe6c20000,
		.end	= 0xe6c20425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(70), /* IIC1_ALI1 */
		.end    = gic_spi(73), /* IIC1_DTEI1 */
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

static struct resource pmu_resources[] = {
	[0] = {
		.start	= gic_spi(83),
		.end	= gic_spi(83),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device pmu_device = {
	.name	= "arm-pmu",
	.id	= -1,
	.num_resources = ARRAY_SIZE(pmu_resources),
	.resource = pmu_resources,
};

static struct platform_device *r8a7740_late_devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
	&dma0_device,
	&dma1_device,
	&dma2_device,
	&usb_dma_device,
	&pmu_device,
};

/*
 * r8a7740 chip has lasting errata on MERAM buffer.
 * this is work-around for it.
 * see
 *	"Media RAM (MERAM)" on r8a7740 documentation
 */
#define MEBUFCNTR	0xFE950098
void __init r8a7740_meram_workaround(void)
{
	void __iomem *reg;

	reg = ioremap_nocache(MEBUFCNTR, 4);
	if (reg) {
		iowrite32(0x01600164, reg);
		iounmap(reg);
	}
}

#define ICCR	0x0004
#define ICSTART	0x0070

#define i2c_read(reg, offset)		ioread8(reg + offset)
#define i2c_write(reg, offset, data)	iowrite8(data, reg + offset)

/*
 * r8a7740 chip has lasting errata on I2C I/O pad reset.
 * this is work-around for it.
 */
static void r8a7740_i2c_workaround(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *reg;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		pr_err("r8a7740 i2c workaround fail (cannot find resource)\n");
		return;
	}

	reg = ioremap(res->start, resource_size(res));
	if (unlikely(!reg)) {
		pr_err("r8a7740 i2c workaround fail (cannot map IO)\n");
		return;
	}

	i2c_write(reg, ICCR, i2c_read(reg, ICCR) | 0x80);
	i2c_read(reg, ICCR); /* dummy read */

	i2c_write(reg, ICSTART, i2c_read(reg, ICSTART) | 0x10);
	i2c_read(reg, ICSTART); /* dummy read */

	udelay(10);

	i2c_write(reg, ICCR, 0x01);
	i2c_write(reg, ICSTART, 0x00);

	udelay(10);

	i2c_write(reg, ICCR, 0x10);
	udelay(10);
	i2c_write(reg, ICCR, 0x00);
	udelay(10);
	i2c_write(reg, ICCR, 0x10);
	udelay(10);

	iounmap(reg);
}

void __init r8a7740_add_standard_devices(void)
{
	/* I2C work-around */
	r8a7740_i2c_workaround(&i2c0_device);
	r8a7740_i2c_workaround(&i2c1_device);

	r8a7740_init_pm_domains();

	/* add devices */
	platform_add_devices(r8a7740_early_devices,
			    ARRAY_SIZE(r8a7740_early_devices));
	platform_add_devices(r8a7740_devices_dt,
			    ARRAY_SIZE(r8a7740_devices_dt));
	platform_add_devices(r8a7740_late_devices,
			     ARRAY_SIZE(r8a7740_late_devices));

	/* add devices to PM domain  */

	rmobile_add_device_to_domain("A3SP",	&scif0_device);
	rmobile_add_device_to_domain("A3SP",	&scif1_device);
	rmobile_add_device_to_domain("A3SP",	&scif2_device);
	rmobile_add_device_to_domain("A3SP",	&scif3_device);
	rmobile_add_device_to_domain("A3SP",	&scif4_device);
	rmobile_add_device_to_domain("A3SP",	&scif5_device);
	rmobile_add_device_to_domain("A3SP",	&scif6_device);
	rmobile_add_device_to_domain("A3SP",	&scif7_device);
	rmobile_add_device_to_domain("A3SP",	&scif8_device);
	rmobile_add_device_to_domain("A3SP",	&i2c1_device);
}

void __init r8a7740_add_early_devices(void)
{
	early_platform_add_devices(r8a7740_early_devices,
				   ARRAY_SIZE(r8a7740_early_devices));
	early_platform_add_devices(r8a7740_devices_dt,
				   ARRAY_SIZE(r8a7740_devices_dt));

	/* setup early console here as well */
	shmobile_setup_console();
}

#ifdef CONFIG_USE_OF

void __init r8a7740_add_standard_devices_dt(void)
{
	platform_add_devices(r8a7740_devices_dt,
			    ARRAY_SIZE(r8a7740_devices_dt));
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

void __init r8a7740_init_irq_of(void)
{
	void __iomem *intc_prio_base = ioremap_nocache(0xe6900010, 0x10);
	void __iomem *intc_msk_base = ioremap_nocache(0xe6900040, 0x10);
	void __iomem *pfc_inta_ctrl = ioremap_nocache(0xe605807c, 0x4);

	irqchip_init();

	/* route signals to GIC */
	iowrite32(0x0, pfc_inta_ctrl);

	/*
	 * To mask the shared interrupt to SPI 149 we must ensure to set
	 * PRIO *and* MASK. Else we run into IRQ floods when registering
	 * the intc_irqpin devices
	 */
	iowrite32(0x0, intc_prio_base + 0x0);
	iowrite32(0x0, intc_prio_base + 0x4);
	iowrite32(0x0, intc_prio_base + 0x8);
	iowrite32(0x0, intc_prio_base + 0xc);
	iowrite8(0xff, intc_msk_base + 0x0);
	iowrite8(0xff, intc_msk_base + 0x4);
	iowrite8(0xff, intc_msk_base + 0x8);
	iowrite8(0xff, intc_msk_base + 0xc);

	iounmap(intc_prio_base);
	iounmap(intc_msk_base);
	iounmap(pfc_inta_ctrl);
}

static void __init r8a7740_generic_init(void)
{
	r8a7740_clock_init(0);
	r8a7740_add_standard_devices_dt();
}

static const char *r8a7740_boards_compat_dt[] __initdata = {
	"renesas,r8a7740",
	NULL,
};

DT_MACHINE_START(R8A7740_DT, "Generic R8A7740 (Flattened Device Tree)")
	.map_io		= r8a7740_map_io,
	.init_early	= shmobile_init_delay,
	.init_irq	= r8a7740_init_irq_of,
	.init_machine	= r8a7740_generic_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a7740_boards_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
