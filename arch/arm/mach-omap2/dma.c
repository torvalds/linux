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
#include <linux/omap-dma.h>

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"

#define OMAP2_DMA_STRIDE	0x60

static u32 errata;
static u8 dma_stride;

static struct omap_dma_dev_attr *d;

static enum omap_reg_offsets dma_common_ch_start, dma_common_ch_end;

static u16 reg_map[] = {
	[REVISION]		= 0x00,
	[GCR]			= 0x78,
	[IRQSTATUS_L0]		= 0x08,
	[IRQSTATUS_L1]		= 0x0c,
	[IRQSTATUS_L2]		= 0x10,
	[IRQSTATUS_L3]		= 0x14,
	[IRQENABLE_L0]		= 0x18,
	[IRQENABLE_L1]		= 0x1c,
	[IRQENABLE_L2]		= 0x20,
	[IRQENABLE_L3]		= 0x24,
	[SYSSTATUS]		= 0x28,
	[OCP_SYSCONFIG]		= 0x2c,
	[CAPS_0]		= 0x64,
	[CAPS_2]		= 0x6c,
	[CAPS_3]		= 0x70,
	[CAPS_4]		= 0x74,

	/* Common register offsets */
	[CCR]			= 0x80,
	[CLNK_CTRL]		= 0x84,
	[CICR]			= 0x88,
	[CSR]			= 0x8c,
	[CSDP]			= 0x90,
	[CEN]			= 0x94,
	[CFN]			= 0x98,
	[CSEI]			= 0xa4,
	[CSFI]			= 0xa8,
	[CDEI]			= 0xac,
	[CDFI]			= 0xb0,
	[CSAC]			= 0xb4,
	[CDAC]			= 0xb8,

	/* Channel specific register offsets */
	[CSSA]			= 0x9c,
	[CDSA]			= 0xa0,
	[CCEN]			= 0xbc,
	[CCFN]			= 0xc0,
	[COLOR]			= 0xc4,

	/* OMAP4 specific registers */
	[CDP]			= 0xd0,
	[CNDP]			= 0xd4,
	[CCDN]			= 0xd8,
};

static void __iomem *dma_base;
static inline void dma_write(u32 val, int reg, int lch)
{
	u8  stride;
	u32 offset;

	stride = (reg >= dma_common_ch_start) ? dma_stride : 0;
	offset = reg_map[reg] + (stride * lch);
	__raw_writel(val, dma_base + offset);
}

static inline u32 dma_read(int reg, int lch)
{
	u8 stride;
	u32 offset, val;

	stride = (reg >= dma_common_ch_start) ? dma_stride : 0;
	offset = reg_map[reg] + (stride * lch);
	val = __raw_readl(dma_base + offset);
	return val;
}

static inline void omap2_disable_irq_lch(int lch)
{
	u32 val;

	val = dma_read(IRQENABLE_L0, lch);
	val &= ~(1 << lch);
	dma_write(val, IRQENABLE_L0, lch);
}

static void omap2_clear_dma(int lch)
{
	int i = dma_common_ch_start;

	for (; i <= dma_common_ch_end; i += 1)
		dma_write(0, i, lch);
}

static void omap2_show_dma_caps(void)
{
	u8 revision = dma_read(REVISION, 0) & 0xff;
	printk(KERN_INFO "OMAP DMA hardware revision %d.%d\n",
				revision >> 4, revision & 0xf);
	return;
}

static u32 configure_dma_errata(void)
{

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

/* One time initializations */
static int __init omap2_system_dma_init_dev(struct omap_hwmod *oh, void *unused)
{
	struct platform_device			*pdev;
	struct omap_system_dma_plat_info	*p;
	struct resource				*mem;
	char					*name = "omap_dma_system";

	dma_stride		= OMAP2_DMA_STRIDE;
	dma_common_ch_start	= CSDP;

	p = kzalloc(sizeof(struct omap_system_dma_plat_info), GFP_KERNEL);
	if (!p) {
		pr_err("%s: Unable to allocate pdata for %s:%s\n",
			__func__, name, oh->name);
		return -ENOMEM;
	}

	p->dma_attr		= (struct omap_dma_dev_attr *)oh->dev_attr;
	p->disable_irq_lch	= omap2_disable_irq_lch;
	p->show_dma_caps	= omap2_show_dma_caps;
	p->clear_dma		= omap2_clear_dma;
	p->dma_write		= dma_write;
	p->dma_read		= dma_read;

	p->clear_lch_regs	= NULL;

	p->errata		= configure_dma_errata();

	pdev = omap_device_build(name, 0, oh, p, sizeof(*p), NULL, 0, 0);
	kfree(p);
	if (IS_ERR(pdev)) {
		pr_err("%s: Can't build omap_device for %s:%s.\n",
			__func__, name, oh->name);
		return PTR_ERR(pdev);
	}

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
	d->chan = kzalloc(sizeof(struct omap_dma_lch) *
					(d->lch_count), GFP_KERNEL);

	if (!d->chan) {
		dev_err(&pdev->dev, "%s: kzalloc fail\n", __func__);
		return -ENOMEM;
	}

	if (cpu_is_omap34xx() && (omap_type() != OMAP2_DEVICE_TYPE_GP))
		d->dev_caps |= HS_CHANNELS_RESERVED;

	/* Check the capabilities register for descriptor loading feature */
	if (dma_read(CAPS_0, 0) & DMA_HAS_DESCRIPTOR_CAPS)
		dma_common_ch_end = CCDN;
	else
		dma_common_ch_end = CCFN;

	return 0;
}

static const struct platform_device_info omap_dma_dev_info = {
	.name = "omap-dma-engine",
	.id = -1,
	.dma_mask = DMA_BIT_MASK(32),
};

static int __init omap2_system_dma_init(void)
{
	struct platform_device *pdev;
	int res;

	res = omap_hwmod_for_each_by_class("dma",
			omap2_system_dma_init_dev, NULL);
	if (res)
		return res;

	pdev = platform_device_register_full(&omap_dma_dev_info);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return res;
}
omap_arch_initcall(omap2_system_dma_init);
