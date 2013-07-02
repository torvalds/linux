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
#include <linux/omap-dma.h>
#include <mach/tc.h>

#include <mach/irqs.h>

#include "dma.h"

#define OMAP1_DMA_BASE			(0xfffed800)
#define OMAP1_LOGICAL_DMA_CH_COUNT	17
#define OMAP1_DMA_STRIDE		0x40

static u32 errata;
static u32 enable_1510_mode;
static u8 dma_stride;
static enum omap_reg_offsets dma_common_ch_start, dma_common_ch_end;

static u16 reg_map[] = {
	[GCR]		= 0x400,
	[GSCR]		= 0x404,
	[GRST1]		= 0x408,
	[HW_ID]		= 0x442,
	[PCH2_ID]	= 0x444,
	[PCH0_ID]	= 0x446,
	[PCH1_ID]	= 0x448,
	[PCHG_ID]	= 0x44a,
	[PCHD_ID]	= 0x44c,
	[CAPS_0]	= 0x44e,
	[CAPS_1]	= 0x452,
	[CAPS_2]	= 0x456,
	[CAPS_3]	= 0x458,
	[CAPS_4]	= 0x45a,
	[PCH2_SR]	= 0x460,
	[PCH0_SR]	= 0x480,
	[PCH1_SR]	= 0x482,
	[PCHD_SR]	= 0x4c0,

	/* Common Registers */
	[CSDP]		= 0x00,
	[CCR]		= 0x02,
	[CICR]		= 0x04,
	[CSR]		= 0x06,
	[CEN]		= 0x10,
	[CFN]		= 0x12,
	[CSFI]		= 0x14,
	[CSEI]		= 0x16,
	[CPC]		= 0x18,	/* 15xx only */
	[CSAC]		= 0x18,
	[CDAC]		= 0x1a,
	[CDEI]		= 0x1c,
	[CDFI]		= 0x1e,
	[CLNK_CTRL]	= 0x28,

	/* Channel specific register offsets */
	[CSSA]		= 0x08,
	[CDSA]		= 0x0c,
	[COLOR]		= 0x20,
	[CCR2]		= 0x24,
	[LCH_CTRL]	= 0x2a,
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
	u8  stride;
	u32 offset;

	stride = (reg >= dma_common_ch_start) ? dma_stride : 0;
	offset = reg_map[reg] + (stride * lch);

	__raw_writew(val, dma_base + offset);
	if ((reg > CLNK_CTRL && reg < CCEN) ||
			(reg > PCHD_ID && reg < CAPS_2)) {
		u32 offset2 = reg_map[reg] + 2 + (stride * lch);
		__raw_writew(val >> 16, dma_base + offset2);
	}
}

static inline u32 dma_read(int reg, int lch)
{
	u8 stride;
	u32 offset, val;

	stride = (reg >= dma_common_ch_start) ? dma_stride : 0;
	offset = reg_map[reg] + (stride * lch);

	val = __raw_readw(dma_base + offset);
	if ((reg > CLNK_CTRL && reg < CCEN) ||
			(reg > PCHD_ID && reg < CAPS_2)) {
		u16 upper;
		u32 offset2 = reg_map[reg] + 2 + (stride * lch);
		upper = __raw_readw(dma_base + offset2);
		val |= (upper << 16);
	}
	return val;
}

static void omap1_clear_lch_regs(int lch)
{
	int i = dma_common_ch_start;

	for (; i <= dma_common_ch_end; i += 1)
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

static u32 configure_dma_errata(void)
{

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
};

static int __init omap1_system_dma_init(void)
{
	struct omap_system_dma_plat_info	*p;
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

	p = kzalloc(sizeof(struct omap_system_dma_plat_info), GFP_KERNEL);
	if (!p) {
		dev_err(&pdev->dev, "%s: Unable to allocate 'p' for %s\n",
			__func__, pdev->name);
		ret = -ENOMEM;
		goto exit_iounmap;
	}

	d = kzalloc(sizeof(struct omap_dma_dev_attr), GFP_KERNEL);
	if (!d) {
		dev_err(&pdev->dev, "%s: Unable to allocate 'd' for %s\n",
			__func__, pdev->name);
		ret = -ENOMEM;
		goto exit_release_p;
	}

	d->lch_count		= OMAP1_LOGICAL_DMA_CH_COUNT;

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


	d->chan = kzalloc(sizeof(struct omap_dma_lch) *
					(d->lch_count), GFP_KERNEL);
	if (!d->chan) {
		dev_err(&pdev->dev,
			"%s: Memory allocation failed for d->chan!\n",
			__func__);
		ret = -ENOMEM;
		goto exit_release_d;
	}

	if (cpu_is_omap15xx())
		d->chan_count = 9;
	else if (cpu_is_omap16xx() || cpu_is_omap7xx()) {
		if (!(d->dev_caps & ENABLE_1510_MODE))
			d->chan_count = 16;
		else
			d->chan_count = 9;
	}

	p->dma_attr = d;

	p->show_dma_caps	= omap1_show_dma_caps;
	p->clear_lch_regs	= omap1_clear_lch_regs;
	p->clear_dma		= omap1_clear_dma;
	p->dma_write		= dma_write;
	p->dma_read		= dma_read;
	p->disable_irq_lch	= NULL;

	p->errata = configure_dma_errata();

	ret = platform_device_add_data(pdev, p, sizeof(*p));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_release_chan;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_release_chan;
	}

	dma_stride		= OMAP1_DMA_STRIDE;
	dma_common_ch_start	= CPC;
	dma_common_ch_end	= COLOR;

	dma_pdev = platform_device_register_full(&omap_dma_dev_info);
	if (IS_ERR(dma_pdev)) {
		ret = PTR_ERR(dma_pdev);
		goto exit_release_pdev;
	}

	return ret;

exit_release_pdev:
	platform_device_del(pdev);
exit_release_chan:
	kfree(d->chan);
exit_release_d:
	kfree(d);
exit_release_p:
	kfree(p);
exit_iounmap:
	iounmap(dma_base);
exit_device_put:
	platform_device_put(pdev);

	return ret;
}
arch_initcall(omap1_system_dma_init);
