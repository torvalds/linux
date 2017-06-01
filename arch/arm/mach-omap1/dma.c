/*
 * OMAP1/OMAP7xx - specific DMA driver
 *
 * Copyright (C) 2003 - 2008 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 * DMA channel linking for 1610 by Samuel Ortiz <samuel.ortiz@nokia.com>
 * Graphics DMA and LCD DMA graphics tranformations
 * by Imre Deak <imre.deak@nokia.com>
 * OMAP2/3 support Copyright (C) 2004-2007 Texas Instruments, Inc.
 * Some functions based on earlier dma-omap.c Copyright (C) 2001 RidgeRun, Inc.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Converted DMA library into platform driver
 *                   - G, Manjunath Kondaiah <manjugk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/omap-dma.h>
#include <mach/tc.h>

#include "soc.h"

#define OMAP1_DMA_BASE			(0xfffed800)

static u32 enable_1510_mode;

static const struct omap_dma_reg reg_map[] = {
	[GCR]		= { 0x0400, 0x00, OMAP_DMA_REG_16BIT },
	[GSCR]		= { 0x0404, 0x00, OMAP_DMA_REG_16BIT },
	[GRST1]		= { 0x0408, 0x00, OMAP_DMA_REG_16BIT },
	[HW_ID]		= { 0x0442, 0x00, OMAP_DMA_REG_16BIT },
	[PCH2_ID]	= { 0x0444, 0x00, OMAP_DMA_REG_16BIT },
	[PCH0_ID]	= { 0x0446, 0x00, OMAP_DMA_REG_16BIT },
	[PCH1_ID]	= { 0x0448, 0x00, OMAP_DMA_REG_16BIT },
	[PCHG_ID]	= { 0x044a, 0x00, OMAP_DMA_REG_16BIT },
	[PCHD_ID]	= { 0x044c, 0x00, OMAP_DMA_REG_16BIT },
	[CAPS_0]	= { 0x044e, 0x00, OMAP_DMA_REG_2X16BIT },
	[CAPS_1]	= { 0x0452, 0x00, OMAP_DMA_REG_2X16BIT },
	[CAPS_2]	= { 0x0456, 0x00, OMAP_DMA_REG_16BIT },
	[CAPS_3]	= { 0x0458, 0x00, OMAP_DMA_REG_16BIT },
	[CAPS_4]	= { 0x045a, 0x00, OMAP_DMA_REG_16BIT },
	[PCH2_SR]	= { 0x0460, 0x00, OMAP_DMA_REG_16BIT },
	[PCH0_SR]	= { 0x0480, 0x00, OMAP_DMA_REG_16BIT },
	[PCH1_SR]	= { 0x0482, 0x00, OMAP_DMA_REG_16BIT },
	[PCHD_SR]	= { 0x04c0, 0x00, OMAP_DMA_REG_16BIT },

	/* Common Registers */
	[CSDP]		= { 0x0000, 0x40, OMAP_DMA_REG_16BIT },
	[CCR]		= { 0x0002, 0x40, OMAP_DMA_REG_16BIT },
	[CICR]		= { 0x0004, 0x40, OMAP_DMA_REG_16BIT },
	[CSR]		= { 0x0006, 0x40, OMAP_DMA_REG_16BIT },
	[CEN]		= { 0x0010, 0x40, OMAP_DMA_REG_16BIT },
	[CFN]		= { 0x0012, 0x40, OMAP_DMA_REG_16BIT },
	[CSFI]		= { 0x0014, 0x40, OMAP_DMA_REG_16BIT },
	[CSEI]		= { 0x0016, 0x40, OMAP_DMA_REG_16BIT },
	[CPC]		= { 0x0018, 0x40, OMAP_DMA_REG_16BIT },	/* 15xx only */
	[CSAC]		= { 0x0018, 0x40, OMAP_DMA_REG_16BIT },
	[CDAC]		= { 0x001a, 0x40, OMAP_DMA_REG_16BIT },
	[CDEI]		= { 0x001c, 0x40, OMAP_DMA_REG_16BIT },
	[CDFI]		= { 0x001e, 0x40, OMAP_DMA_REG_16BIT },
	[CLNK_CTRL]	= { 0x0028, 0x40, OMAP_DMA_REG_16BIT },

	/* Channel specific register offsets */
	[CSSA]		= { 0x0008, 0x40, OMAP_DMA_REG_2X16BIT },
	[CDSA]		= { 0x000c, 0x40, OMAP_DMA_REG_2X16BIT },
	[COLOR]		= { 0x0020, 0x40, OMAP_DMA_REG_2X16BIT },
	[CCR2]		= { 0x0024, 0x40, OMAP_DMA_REG_16BIT },
	[LCH_CTRL]	= { 0x002a, 0x40, OMAP_DMA_REG_16BIT },
};

static struct resource res[] __initdata = {
	[0] = {
		.start	= OMAP1_DMA_BASE,
		.end	= OMAP1_DMA_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name   = "0",
		.start  = INT_DMA_CH0_6,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.name   = "1",
		.start  = INT_DMA_CH1_7,
		.flags  = IORESOURCE_IRQ,
	},
	[3] = {
		.name   = "2",
		.start  = INT_DMA_CH2_8,
		.flags  = IORESOURCE_IRQ,
	},
	[4] = {
		.name   = "3",
		.start  = INT_DMA_CH3,
		.flags  = IORESOURCE_IRQ,
	},
	[5] = {
		.name   = "4",
		.start  = INT_DMA_CH4,
		.flags  = IORESOURCE_IRQ,
	},
	[6] = {
		.name   = "5",
		.start  = INT_DMA_CH5,
		.flags  = IORESOURCE_IRQ,
	},
	/* Handled in lcd_dma.c */
	[7] = {
		.name   = "6",
		.start  = INT_1610_DMA_CH6,
		.flags  = IORESOURCE_IRQ,
	},
	/* irq's for omap16xx and omap7xx */
	[8] = {
		.name   = "7",
		.start  = INT_1610_DMA_CH7,
		.flags  = IORESOURCE_IRQ,
	},
	[9] = {
		.name   = "8",
		.start  = INT_1610_DMA_CH8,
		.flags  = IORESOURCE_IRQ,
	},
	[10] = {
		.name  = "9",
		.start = INT_1610_DMA_CH9,
		.flags = IORESOURCE_IRQ,
	},
	[11] = {
		.name  = "10",
		.start = INT_1610_DMA_CH10,
		.flags = IORESOURCE_IRQ,
	},
	[12] = {
		.name  = "11",
		.start = INT_1610_DMA_CH11,
		.flags = IORESOURCE_IRQ,
	},
	[13] = {
		.name  = "12",
		.start = INT_1610_DMA_CH12,
		.flags = IORESOURCE_IRQ,
	},
	[14] = {
		.name  = "13",
		.start = INT_1610_DMA_CH13,
		.flags = IORESOURCE_IRQ,
	},
	[15] = {
		.name  = "14",
		.start = INT_1610_DMA_CH14,
		.flags = IORESOURCE_IRQ,
	},
	[16] = {
		.name  = "15",
		.start = INT_1610_DMA_CH15,
		.flags = IORESOURCE_IRQ,
	},
	[17] = {
		.name  = "16",
		.start = INT_DMA_LCD,
		.flags = IORESOURCE_IRQ,
	},
};

static void __iomem *dma_base;
static inline void dma_write(u32 val, int reg, int lch)
{
	void __iomem *addr = dma_base;

	addr += reg_map[reg].offset;
	addr += reg_map[reg].stride * lch;

	__raw_writew(val, addr);
	if (reg_map[reg].type == OMAP_DMA_REG_2X16BIT)
		__raw_writew(val >> 16, addr + 2);
}

static inline u32 dma_read(int reg, int lch)
{
	void __iomem *addr = dma_base;
	uint32_t val;

	addr += reg_map[reg].offset;
	addr += reg_map[reg].stride * lch;

	val = __raw_readw(addr);
	if (reg_map[reg].type == OMAP_DMA_REG_2X16BIT)
		val |= __raw_readw(addr + 2) << 16;

	return val;
}

static void omap1_clear_lch_regs(int lch)
{
	int i;

	for (i = CPC; i <= COLOR; i += 1)
		dma_write(0, i, lch);
}

static void omap1_clear_dma(int lch)
{
	u32 l;

	l = dma_read(CCR, lch);
	l &= ~OMAP_DMA_CCR_EN;
	dma_write(l, CCR, lch);

	/* Clear pending interrupts */
	l = dma_read(CSR, lch);
}

static void omap1_show_dma_caps(void)
{
	if (enable_1510_mode) {
		printk(KERN_INFO "DMA support for OMAP15xx initialized\n");
	} else {
		u16 w;
		printk(KERN_INFO "OMAP DMA hardware version %d\n",
							dma_read(HW_ID, 0));
		printk(KERN_INFO "DMA capabilities: %08x:%08x:%04x:%04x:%04x\n",
			dma_read(CAPS_0, 0), dma_read(CAPS_1, 0),
			dma_read(CAPS_2, 0), dma_read(CAPS_3, 0),
			dma_read(CAPS_4, 0));

		/* Disable OMAP 3.0/3.1 compatibility mode. */
		w = dma_read(GSCR, 0);
		w |= 1 << 3;
		dma_write(w, GSCR, 0);
	}
	return;
}

static unsigned configure_dma_errata(void)
{
	unsigned errata = 0;

	/*
	 * Erratum 3.2/3.3: sometimes 0 is returned if CSAC/CDAC is
	 * read before the DMA controller finished disabling the channel.
	 */
	if (!cpu_is_omap15xx())
		SET_DMA_ERRATA(DMA_ERRATA_3_3);

	return errata;
}

static const struct platform_device_info omap_dma_dev_info = {
	.name = "omap-dma-engine",
	.id = -1,
	.dma_mask = DMA_BIT_MASK(32),
	.res = res,
	.num_res = 1,
};

/* OMAP730, OMAP850 */
static const struct dma_slave_map omap7xx_sdma_map[] = {
	{ "omap-mcbsp.1", "tx", SDMA_FILTER_PARAM(8) },
	{ "omap-mcbsp.1", "rx", SDMA_FILTER_PARAM(9) },
	{ "omap-mcbsp.2", "tx", SDMA_FILTER_PARAM(10) },
	{ "omap-mcbsp.2", "rx", SDMA_FILTER_PARAM(11) },
	{ "mmci-omap.0", "tx", SDMA_FILTER_PARAM(21) },
	{ "mmci-omap.0", "rx", SDMA_FILTER_PARAM(22) },
	{ "omap_udc", "rx0", SDMA_FILTER_PARAM(26) },
	{ "omap_udc", "rx1", SDMA_FILTER_PARAM(27) },
	{ "omap_udc", "rx2", SDMA_FILTER_PARAM(28) },
	{ "omap_udc", "tx0", SDMA_FILTER_PARAM(29) },
	{ "omap_udc", "tx1", SDMA_FILTER_PARAM(30) },
	{ "omap_udc", "tx2", SDMA_FILTER_PARAM(31) },
};

/* OMAP1510, OMAP1610*/
static const struct dma_slave_map omap1xxx_sdma_map[] = {
	{ "omap-mcbsp.1", "tx", SDMA_FILTER_PARAM(8) },
	{ "omap-mcbsp.1", "rx", SDMA_FILTER_PARAM(9) },
	{ "omap-mcbsp.3", "tx", SDMA_FILTER_PARAM(10) },
	{ "omap-mcbsp.3", "rx", SDMA_FILTER_PARAM(11) },
	{ "omap-mcbsp.2", "tx", SDMA_FILTER_PARAM(16) },
	{ "omap-mcbsp.2", "rx", SDMA_FILTER_PARAM(17) },
	{ "mmci-omap.0", "tx", SDMA_FILTER_PARAM(21) },
	{ "mmci-omap.0", "rx", SDMA_FILTER_PARAM(22) },
	{ "omap_udc", "rx0", SDMA_FILTER_PARAM(26) },
	{ "omap_udc", "rx1", SDMA_FILTER_PARAM(27) },
	{ "omap_udc", "rx2", SDMA_FILTER_PARAM(28) },
	{ "omap_udc", "tx0", SDMA_FILTER_PARAM(29) },
	{ "omap_udc", "tx1", SDMA_FILTER_PARAM(30) },
	{ "omap_udc", "tx2", SDMA_FILTER_PARAM(31) },
	{ "mmci-omap.1", "tx", SDMA_FILTER_PARAM(54) },
	{ "mmci-omap.1", "rx", SDMA_FILTER_PARAM(55) },
};

static struct omap_system_dma_plat_info dma_plat_info __initdata = {
	.reg_map	= reg_map,
	.channel_stride	= 0x40,
	.show_dma_caps	= omap1_show_dma_caps,
	.clear_lch_regs	= omap1_clear_lch_regs,
	.clear_dma	= omap1_clear_dma,
	.dma_write	= dma_write,
	.dma_read	= dma_read,
};

static int __init omap1_system_dma_init(void)
{
	struct omap_system_dma_plat_info	p;
	struct omap_dma_dev_attr		*d;
	struct platform_device			*pdev, *dma_pdev;
	int ret;

	pdev = platform_device_alloc("omap_dma_system", 0);
	if (!pdev) {
		pr_err("%s: Unable to device alloc for dma\n",
			__func__);
		return -ENOMEM;
	}

	dma_base = ioremap(res[0].start, resource_size(&res[0]));
	if (!dma_base) {
		pr_err("%s: Unable to ioremap\n", __func__);
		ret = -ENODEV;
		goto exit_device_put;
	}

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_iounmap;
	}

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto exit_iounmap;
	}

	/* Valid attributes for omap1 plus processors */
	if (cpu_is_omap15xx())
		d->dev_caps = ENABLE_1510_MODE;
	enable_1510_mode = d->dev_caps & ENABLE_1510_MODE;

	if (cpu_is_omap16xx())
		d->dev_caps = ENABLE_16XX_MODE;

	d->dev_caps		|= SRC_PORT;
	d->dev_caps		|= DST_PORT;
	d->dev_caps		|= SRC_INDEX;
	d->dev_caps		|= DST_INDEX;
	d->dev_caps		|= IS_BURST_ONLY4;
	d->dev_caps		|= CLEAR_CSR_ON_READ;
	d->dev_caps		|= IS_WORD_16;

	/* available logical channels */
	if (cpu_is_omap15xx()) {
		d->lch_count = 9;
	} else {
		if (d->dev_caps & ENABLE_1510_MODE)
			d->lch_count = 9;
		else
			d->lch_count = 16;
	}

	p = dma_plat_info;
	p.dma_attr = d;
	p.errata = configure_dma_errata();

	if (cpu_is_omap7xx()) {
		p.slave_map = omap7xx_sdma_map;
		p.slavecnt = ARRAY_SIZE(omap7xx_sdma_map);
	} else {
		p.slave_map = omap1xxx_sdma_map;
		p.slavecnt = ARRAY_SIZE(omap1xxx_sdma_map);
	}

	ret = platform_device_add_data(pdev, &p, sizeof(p));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_release_d;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_release_d;
	}

	dma_pdev = platform_device_register_full(&omap_dma_dev_info);
	if (IS_ERR(dma_pdev)) {
		ret = PTR_ERR(dma_pdev);
		goto exit_release_pdev;
	}

	return ret;

exit_release_pdev:
	platform_device_del(pdev);
exit_release_d:
	kfree(d);
exit_iounmap:
	iounmap(dma_base);
exit_device_put:
	platform_device_put(pdev);

	return ret;
}
arch_initcall(omap1_system_dma_init);
