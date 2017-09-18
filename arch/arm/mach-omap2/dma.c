/*
 * OMAP2+ DMA driver
 *
 * Copyright (C) 2003 - 2008 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 * DMA channel linking for 1610 by Samuel Ortiz <samuel.ortiz@nokia.com>
 * Graphics DMA and LCD DMA graphics tranformations
 * by Imre Deak <imre.deak@nokia.com>
 * OMAP2/3 support Copyright (C) 2004-2007 Texas Instruments, Inc.
 * Some functions based on earlier dma-omap.c Copyright (C) 2001 RidgeRun, Inc.
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Converted DMA library into platform driver
 *	- G, Manjunath Kondaiah <manjugk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/of.h>
#include <linux/omap-dma.h>

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"

static enum omap_reg_offsets dma_common_ch_end;

static const struct omap_dma_reg reg_map[] = {
	[REVISION]	= { 0x0000, 0x00, OMAP_DMA_REG_32BIT },
	[GCR]		= { 0x0078, 0x00, OMAP_DMA_REG_32BIT },
	[IRQSTATUS_L0]	= { 0x0008, 0x00, OMAP_DMA_REG_32BIT },
	[IRQSTATUS_L1]	= { 0x000c, 0x00, OMAP_DMA_REG_32BIT },
	[IRQSTATUS_L2]	= { 0x0010, 0x00, OMAP_DMA_REG_32BIT },
	[IRQSTATUS_L3]	= { 0x0014, 0x00, OMAP_DMA_REG_32BIT },
	[IRQENABLE_L0]	= { 0x0018, 0x00, OMAP_DMA_REG_32BIT },
	[IRQENABLE_L1]	= { 0x001c, 0x00, OMAP_DMA_REG_32BIT },
	[IRQENABLE_L2]	= { 0x0020, 0x00, OMAP_DMA_REG_32BIT },
	[IRQENABLE_L3]	= { 0x0024, 0x00, OMAP_DMA_REG_32BIT },
	[SYSSTATUS]	= { 0x0028, 0x00, OMAP_DMA_REG_32BIT },
	[OCP_SYSCONFIG]	= { 0x002c, 0x00, OMAP_DMA_REG_32BIT },
	[CAPS_0]	= { 0x0064, 0x00, OMAP_DMA_REG_32BIT },
	[CAPS_2]	= { 0x006c, 0x00, OMAP_DMA_REG_32BIT },
	[CAPS_3]	= { 0x0070, 0x00, OMAP_DMA_REG_32BIT },
	[CAPS_4]	= { 0x0074, 0x00, OMAP_DMA_REG_32BIT },

	/* Common register offsets */
	[CCR]		= { 0x0080, 0x60, OMAP_DMA_REG_32BIT },
	[CLNK_CTRL]	= { 0x0084, 0x60, OMAP_DMA_REG_32BIT },
	[CICR]		= { 0x0088, 0x60, OMAP_DMA_REG_32BIT },
	[CSR]		= { 0x008c, 0x60, OMAP_DMA_REG_32BIT },
	[CSDP]		= { 0x0090, 0x60, OMAP_DMA_REG_32BIT },
	[CEN]		= { 0x0094, 0x60, OMAP_DMA_REG_32BIT },
	[CFN]		= { 0x0098, 0x60, OMAP_DMA_REG_32BIT },
	[CSEI]		= { 0x00a4, 0x60, OMAP_DMA_REG_32BIT },
	[CSFI]		= { 0x00a8, 0x60, OMAP_DMA_REG_32BIT },
	[CDEI]		= { 0x00ac, 0x60, OMAP_DMA_REG_32BIT },
	[CDFI]		= { 0x00b0, 0x60, OMAP_DMA_REG_32BIT },
	[CSAC]		= { 0x00b4, 0x60, OMAP_DMA_REG_32BIT },
	[CDAC]		= { 0x00b8, 0x60, OMAP_DMA_REG_32BIT },

	/* Channel specific register offsets */
	[CSSA]		= { 0x009c, 0x60, OMAP_DMA_REG_32BIT },
	[CDSA]		= { 0x00a0, 0x60, OMAP_DMA_REG_32BIT },
	[CCEN]		= { 0x00bc, 0x60, OMAP_DMA_REG_32BIT },
	[CCFN]		= { 0x00c0, 0x60, OMAP_DMA_REG_32BIT },
	[COLOR]		= { 0x00c4, 0x60, OMAP_DMA_REG_32BIT },

	/* OMAP4 specific registers */
	[CDP]		= { 0x00d0, 0x60, OMAP_DMA_REG_32BIT },
	[CNDP]		= { 0x00d4, 0x60, OMAP_DMA_REG_32BIT },
	[CCDN]		= { 0x00d8, 0x60, OMAP_DMA_REG_32BIT },
};

static void __iomem *dma_base;
static inline void dma_write(u32 val, int reg, int lch)
{
	void __iomem *addr = dma_base;

	addr += reg_map[reg].offset;
	addr += reg_map[reg].stride * lch;

	writel_relaxed(val, addr);
}

static inline u32 dma_read(int reg, int lch)
{
	void __iomem *addr = dma_base;

	addr += reg_map[reg].offset;
	addr += reg_map[reg].stride * lch;

	return readl_relaxed(addr);
}

static void omap2_clear_dma(int lch)
{
	int i;

	for (i = CSDP; i <= dma_common_ch_end; i += 1)
		dma_write(0, i, lch);
}

static void omap2_show_dma_caps(void)
{
	u8 revision = dma_read(REVISION, 0) & 0xff;
	printk(KERN_INFO "OMAP DMA hardware revision %d.%d\n",
				revision >> 4, revision & 0xf);
}

static unsigned configure_dma_errata(void)
{
	unsigned errata = 0;

	/*
	 * Errata applicable for OMAP2430ES1.0 and all omap2420
	 *
	 * I.
	 * Erratum ID: Not Available
	 * Inter Frame DMA buffering issue DMA will wrongly
	 * buffer elements if packing and bursting is enabled. This might
	 * result in data gets stalled in FIFO at the end of the block.
	 * Workaround: DMA channels must have BUFFERING_DISABLED bit set to
	 * guarantee no data will stay in the DMA FIFO in case inter frame
	 * buffering occurs
	 *
	 * II.
	 * Erratum ID: Not Available
	 * DMA may hang when several channels are used in parallel
	 * In the following configuration, DMA channel hanging can occur:
	 * a. Channel i, hardware synchronized, is enabled
	 * b. Another channel (Channel x), software synchronized, is enabled.
	 * c. Channel i is disabled before end of transfer
	 * d. Channel i is reenabled.
	 * e. Steps 1 to 4 are repeated a certain number of times.
	 * f. A third channel (Channel y), software synchronized, is enabled.
	 * Channel x and Channel y may hang immediately after step 'f'.
	 * Workaround:
	 * For any channel used - make sure NextLCH_ID is set to the value j.
	 */
	if (cpu_is_omap2420() || (cpu_is_omap2430() &&
				(omap_type() == OMAP2430_REV_ES1_0))) {

		SET_DMA_ERRATA(DMA_ERRATA_IFRAME_BUFFERING);
		SET_DMA_ERRATA(DMA_ERRATA_PARALLEL_CHANNELS);
	}

	/*
	 * Erratum ID: i378: OMAP2+: sDMA Channel is not disabled
	 * after a transaction error.
	 * Workaround: SW should explicitely disable the channel.
	 */
	if (cpu_class_is_omap2())
		SET_DMA_ERRATA(DMA_ERRATA_i378);

	/*
	 * Erratum ID: i541: sDMA FIFO draining does not finish
	 * If sDMA channel is disabled on the fly, sDMA enters standby even
	 * through FIFO Drain is still in progress
	 * Workaround: Put sDMA in NoStandby more before a logical channel is
	 * disabled, then put it back to SmartStandby right after the channel
	 * finishes FIFO draining.
	 */
	if (cpu_is_omap34xx())
		SET_DMA_ERRATA(DMA_ERRATA_i541);

	/*
	 * Erratum ID: i88 : Special programming model needed to disable DMA
	 * before end of block.
	 * Workaround: software must ensure that the DMA is configured in No
	 * Standby mode(DMAx_OCP_SYSCONFIG.MIDLEMODE = "01")
	 */
	if (omap_type() == OMAP3430_REV_ES1_0)
		SET_DMA_ERRATA(DMA_ERRATA_i88);

	/*
	 * Erratum 3.2/3.3: sometimes 0 is returned if CSAC/CDAC is
	 * read before the DMA controller finished disabling the channel.
	 */
	SET_DMA_ERRATA(DMA_ERRATA_3_3);

	/*
	 * Erratum ID: Not Available
	 * A bug in ROM code leaves IRQ status for channels 0 and 1 uncleared
	 * after secure sram context save and restore.
	 * Work around: Hence we need to manually clear those IRQs to avoid
	 * spurious interrupts. This affects only secure devices.
	 */
	if (cpu_is_omap34xx() && (omap_type() != OMAP2_DEVICE_TYPE_GP))
		SET_DMA_ERRATA(DMA_ROMCODE_BUG);

	return errata;
}

static const struct dma_slave_map omap24xx_sdma_dt_map[] = {
	/* external DMA requests when tusb6010 is used */
	{ "musb-hdrc.1.auto", "dmareq0", SDMA_FILTER_PARAM(2) },
	{ "musb-hdrc.1.auto", "dmareq1", SDMA_FILTER_PARAM(3) },
	{ "musb-hdrc.1.auto", "dmareq2", SDMA_FILTER_PARAM(14) }, /* OMAP2420 only */
	{ "musb-hdrc.1.auto", "dmareq3", SDMA_FILTER_PARAM(15) }, /* OMAP2420 only */
	{ "musb-hdrc.1.auto", "dmareq4", SDMA_FILTER_PARAM(16) }, /* OMAP2420 only */
	{ "musb-hdrc.1.auto", "dmareq5", SDMA_FILTER_PARAM(64) }, /* OMAP2420 only */
};

static struct omap_system_dma_plat_info dma_plat_info __initdata = {
	.reg_map	= reg_map,
	.channel_stride	= 0x60,
	.show_dma_caps	= omap2_show_dma_caps,
	.clear_dma	= omap2_clear_dma,
	.dma_write	= dma_write,
	.dma_read	= dma_read,
};

static struct platform_device_info omap_dma_dev_info = {
	.name = "omap-dma-engine",
	.id = -1,
	.dma_mask = DMA_BIT_MASK(32),
};

/* One time initializations */
static int __init omap2_system_dma_init_dev(struct omap_hwmod *oh, void *unused)
{
	struct platform_device			*pdev;
	struct omap_system_dma_plat_info	p;
	struct omap_dma_dev_attr		*d;
	struct resource				*mem;
	char					*name = "omap_dma_system";

	p = dma_plat_info;
	p.dma_attr = (struct omap_dma_dev_attr *)oh->dev_attr;
	p.errata = configure_dma_errata();

	if (soc_is_omap24xx()) {
		/* DMA slave map for drivers not yet converted to DT */
		p.slave_map = omap24xx_sdma_dt_map;
		p.slavecnt = ARRAY_SIZE(omap24xx_sdma_dt_map);
	}

	pdev = omap_device_build(name, 0, oh, &p, sizeof(p));
	if (IS_ERR(pdev)) {
		pr_err("%s: Can't build omap_device for %s:%s.\n",
			__func__, name, oh->name);
		return PTR_ERR(pdev);
	}

	omap_dma_dev_info.res = pdev->resource;
	omap_dma_dev_info.num_res = pdev->num_resources;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "%s: no mem resource\n", __func__);
		return -EINVAL;
	}

	dma_base = ioremap(mem->start, resource_size(mem));
	if (!dma_base) {
		dev_err(&pdev->dev, "%s: ioremap fail\n", __func__);
		return -ENOMEM;
	}

	d = oh->dev_attr;

	if (cpu_is_omap34xx() && (omap_type() != OMAP2_DEVICE_TYPE_GP))
		d->dev_caps |= HS_CHANNELS_RESERVED;

	if (platform_get_irq_byname(pdev, "0") < 0)
		d->dev_caps |= DMA_ENGINE_HANDLE_IRQ;

	/* Check the capabilities register for descriptor loading feature */
	if (dma_read(CAPS_0, 0) & DMA_HAS_DESCRIPTOR_CAPS)
		dma_common_ch_end = CCDN;
	else
		dma_common_ch_end = CCFN;

	return 0;
}

static int __init omap2_system_dma_init(void)
{
	return omap_hwmod_for_each_by_class("dma",
			omap2_system_dma_init_dev, NULL);
}
omap_arch_initcall(omap2_system_dma_init);
