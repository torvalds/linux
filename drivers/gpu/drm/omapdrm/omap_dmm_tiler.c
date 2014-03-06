/*
 * DMM IOMMU driver support functions for TI OMAP processors.
 *
 * Author: Rob Clark <rob@ti.com>
 *         Andy Gross <andy.gross@ti.com>
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h> /* platform_device() */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/list.h>

#include "omap_dmm_tiler.h"
#include "omap_dmm_priv.h"

#define DMM_DRIVER_NAME "dmm"

/* mappings for associating views to luts */
static struct tcm *containers[TILFMT_NFORMATS];
static struct dmm *omap_dmm;

/* global spinlock for protecting lists */
static DEFINE_SPINLOCK(list_lock);

/* Geometry table */
#define GEOM(xshift, yshift, bytes_per_pixel) { \
		.x_shft = (xshift), \
		.y_shft = (yshift), \
		.cpp    = (bytes_per_pixel), \
		.slot_w = 1 << (SLOT_WIDTH_BITS - (xshift)), \
		.slot_h = 1 << (SLOT_HEIGHT_BITS - (yshift)), \
	}

static const struct {
	uint32_t x_shft;	/* unused X-bits (as part of bpp) */
	uint32_t y_shft;	/* unused Y-bits (as part of bpp) */
	uint32_t cpp;		/* bytes/chars per pixel */
	uint32_t slot_w;	/* width of each slot (in pixels) */
	uint32_t slot_h;	/* height of each slot (in pixels) */
} geom[TILFMT_NFORMATS] = {
		[TILFMT_8BIT]  = GEOM(0, 0, 1),
		[TILFMT_16BIT] = GEOM(0, 1, 2),
		[TILFMT_32BIT] = GEOM(1, 1, 4),
		[TILFMT_PAGE]  = GEOM(SLOT_WIDTH_BITS, SLOT_HEIGHT_BITS, 1),
};


/* lookup table for registers w/ per-engine instances */
static const uint32_t reg[][4] = {
		[PAT_STATUS] = {DMM_PAT_STATUS__0, DMM_PAT_STATUS__1,
				DMM_PAT_STATUS__2, DMM_PAT_STATUS__3},
		[PAT_DESCR]  = {DMM_PAT_DESCR__0, DMM_PAT_DESCR__1,
				DMM_PAT_DESCR__2, DMM_PAT_DESCR__3},
};

/* simple allocator to grab next 16 byte aligned memory from txn */
static void *alloc_dma(struct dmm_txn *txn, size_t sz, dma_addr_t *pa)
{
	void *ptr;
	struct refill_engine *engine = txn->engine_handle;

	/* dmm programming requires 16 byte aligned addresses */
	txn->current_pa = round_up(txn->current_pa, 16);
	txn->current_va = (void *)round_up((long)txn->current_va, 16);

	ptr = txn->current_va;
	*pa = txn->current_pa;

	txn->current_pa += sz;
	txn->current_va += sz;

	BUG_ON((txn->current_va - engine->refill_va) > REFILL_BUFFER_SIZE);

	return ptr;
}

/* check status and spin until wait_mask comes true */
static int wait_status(struct refill_engine *engine, uint32_t wait_mask)
{
	struct dmm *dmm = engine->dmm;
	uint32_t r = 0, err, i;

	i = DMM_FIXED_RETRY_COUNT;
	while (true) {
		r = readl(dmm->base + reg[PAT_STATUS][engine->id]);
		err = r & DMM_PATSTATUS_ERR;
		if (err)
			return -EFAULT;

		if ((r & wait_mask) == wait_mask)
			break;

		if (--i == 0)
			return -ETIMEDOUT;

		udelay(1);
	}

	return 0;
}

static void release_engine(struct refill_engine *engine)
{
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	list_add(&engine->idle_node, &omap_dmm->idle_head);
	spin_unlock_irqrestore(&list_lock, flags);

	atomic_inc(&omap_dmm->engine_counter);
	wake_up_interruptible(&omap_dmm->engine_queue);
}

static irqreturn_t omap_dmm_irq_handler(int irq, void *arg)
{
	struct dmm *dmm = arg;
	uint32_t status = readl(dmm->base + DMM_PAT_IRQSTATUS);
	int i;

	/* ack IRQ */
	writel(status, dmm->base + DMM_PAT_IRQSTATUS);

	for (i = 0; i < dmm->num_engines; i++) {
		if (status & DMM_IRQSTAT_LST) {
			wake_up_interruptible(&dmm->engines[i].wait_for_refill);

			if (dmm->engines[i].async)
				release_engine(&dmm->engines[i]);
		}

		status >>= 8;
	}

	return IRQ_HANDLED;
}

/**
 * Get a handle for a DMM transaction
 */
static struct dmm_txn *dmm_txn_init(struct dmm *dmm, struct tcm *tcm)
{
	struct dmm_txn *txn = NULL;
	struct refill_engine *engine = NULL;
	int ret;
	unsigned long flags;


	/* wait until an engine is available */
	ret = wait_event_interruptible(omap_dmm->engine_queue,
		atomic_add_unless(&omap_dmm->engine_counter, -1, 0));
	if (ret)
		return ERR_PTR(ret);

	/* grab an idle engine */
	spin_lock_irqsave(&list_lock, flags);
	if (!list_empty(&dmm->idle_head)) {
		engine = list_entry(dmm->idle_head.next, struct refill_engine,
					idle_node);
		list_del(&engine->idle_node);
	}
	spin_unlock_irqrestore(&list_lock, flags);

	BUG_ON(!engine);

	txn = &engine->txn;
	engine->tcm = tcm;
	txn->engine_handle = engine;
	txn->last_pat = NULL;
	txn->current_va = engine->refill_va;
	txn->current_pa = engine->refill_pa;

	return txn;
}

/**
 * Add region to DMM transaction.  If pages or pages[i] is NULL, then the
 * corresponding slot is cleared (ie. dummy_pa is programmed)
 */
static void dmm_txn_append(struct dmm_txn *txn, struct pat_area *area,
		struct page **pages, uint32_t npages, uint32_t roll)
{
	dma_addr_t pat_pa = 0;
	uint32_t *data;
	struct pat *pat;
	struct refill_engine *engine = txn->engine_handle;
	int columns = (1 + area->x1 - area->x0);
	int rows = (1 + area->y1 - area->y0);
	int i = columns*rows;

	pat = alloc_dma(txn, sizeof(struct pat), &pat_pa);

	if (txn->last_pat)
		txn->last_pat->next_pa = (uint32_t)pat_pa;

	pat->area = *area;

	/* adjust Y coordinates based off of container parameters */
	pat->area.y0 += engine->tcm->y_offset;
	pat->area.y1 += engine->tcm->y_offset;

	pat->ctrl = (struct pat_ctrl){
			.start = 1,
			.lut_id = engine->tcm->lut_id,
		};

	data = alloc_dma(txn, 4*i, &pat->data_pa);

	while (i--) {
		int n = i + roll;
		if (n >= npages)
			n -= npages;
		data[i] = (pages && pages[n]) ?
			page_to_phys(pages[n]) : engine->dmm->dummy_pa;
	}

	txn->last_pat = pat;

	return;
}

/**
 * Commit the DMM transaction.
 */
static int dmm_txn_commit(struct dmm_txn *txn, bool wait)
{
	int ret = 0;
	struct refill_engine *engine = txn->engine_handle;
	struct dmm *dmm = engine->dmm;

	if (!txn->last_pat) {
		dev_err(engine->dmm->dev, "need at least one txn\n");
		ret = -EINVAL;
		goto cleanup;
	}

	txn->last_pat->next_pa = 0;

	/* write to PAT_DESCR to clear out any pending transaction */
	writel(0x0, dmm->base + reg[PAT_DESCR][engine->id]);

	/* wait for engine ready: */
	ret = wait_status(engine, DMM_PATSTATUS_READY);
	if (ret) {
		ret = -EFAULT;
		goto cleanup;
	}

	/* mark whether it is async to denote list management in IRQ handler */
	engine->async = wait ? false : true;

	/* kick reload */
	writel(engine->refill_pa,
		dmm->base + reg[PAT_DESCR][engine->id]);

	if (wait) {
		if (wait_event_interruptible_timeout(engine->wait_for_refill,
				wait_status(engine, DMM_PATSTATUS_READY) == 0,
				msecs_to_jiffies(1)) <= 0) {
			dev_err(dmm->dev, "timed out waiting for done\n");
			ret = -ETIMEDOUT;
		}
	}

cleanup:
	/* only place engine back on list if we are done with it */
	if (ret || wait)
		release_engine(engine);

	return ret;
}

/*
 * DMM programming
 */
static int fill(struct tcm_area *area, struct page **pages,
		uint32_t npages, uint32_t roll, bool wait)
{
	int ret = 0;
	struct tcm_area slice, area_s;
	struct dmm_txn *txn;

	txn = dmm_txn_init(omap_dmm, area->tcm);
	if (IS_ERR_OR_NULL(txn))
		return -ENOMEM;

	tcm_for_each_slice(slice, *area, area_s) {
		struct pat_area p_area = {
				.x0 = slice.p0.x,  .y0 = slice.p0.y,
				.x1 = slice.p1.x,  .y1 = slice.p1.y,
		};

		dmm_txn_append(txn, &p_area, pages, npages, roll);

		roll += tcm_sizeof(slice);
	}

	ret = dmm_txn_commit(txn, wait);

	return ret;
}

/*
 * Pin/unpin
 */

/* note: slots for which pages[i] == NULL are filled w/ dummy page
 */
int tiler_pin(struct tiler_block *block, struct page **pages,
		uint32_t npages, uint32_t roll, bool wait)
{
	int ret;

	ret = fill(&block->area, pages, npages, roll, wait);

	if (ret)
		tiler_unpin(block);

	return ret;
}

int tiler_unpin(struct tiler_block *block)
{
	return fill(&block->area, NULL, 0, 0, false);
}

/*
 * Reserve/release
 */
struct tiler_block *tiler_reserve_2d(enum tiler_fmt fmt, uint16_t w,
		uint16_t h, uint16_t align)
{
	struct tiler_block *block = kzalloc(sizeof(*block), GFP_KERNEL);
	u32 min_align = 128;
	int ret;
	unsigned long flags;

	BUG_ON(!validfmt(fmt));

	/* convert width/height to slots */
	w = DIV_ROUND_UP(w, geom[fmt].slot_w);
	h = DIV_ROUND_UP(h, geom[fmt].slot_h);

	/* convert alignment to slots */
	min_align = max(min_align, (geom[fmt].slot_w * geom[fmt].cpp));
	align = ALIGN(align, min_align);
	align /= geom[fmt].slot_w * geom[fmt].cpp;

	block->fmt = fmt;

	ret = tcm_reserve_2d(containers[fmt], w, h, align, &block->area);
	if (ret) {
		kfree(block);
		return ERR_PTR(-ENOMEM);
	}

	/* add to allocation list */
	spin_lock_irqsave(&list_lock, flags);
	list_add(&block->alloc_node, &omap_dmm->alloc_head);
	spin_unlock_irqrestore(&list_lock, flags);

	return block;
}

struct tiler_block *tiler_reserve_1d(size_t size)
{
	struct tiler_block *block = kzalloc(sizeof(*block), GFP_KERNEL);
	int num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long flags;

	if (!block)
		return ERR_PTR(-ENOMEM);

	block->fmt = TILFMT_PAGE;

	if (tcm_reserve_1d(containers[TILFMT_PAGE], num_pages,
				&block->area)) {
		kfree(block);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_irqsave(&list_lock, flags);
	list_add(&block->alloc_node, &omap_dmm->alloc_head);
	spin_unlock_irqrestore(&list_lock, flags);

	return block;
}

/* note: if you have pin'd pages, you should have already unpin'd first! */
int tiler_release(struct tiler_block *block)
{
	int ret = tcm_free(&block->area);
	unsigned long flags;

	if (block->area.tcm)
		dev_err(omap_dmm->dev, "failed to release block\n");

	spin_lock_irqsave(&list_lock, flags);
	list_del(&block->alloc_node);
	spin_unlock_irqrestore(&list_lock, flags);

	kfree(block);
	return ret;
}

/*
 * Utils
 */

/* calculate the tiler space address of a pixel in a view orientation...
 * below description copied from the display subsystem section of TRM:
 *
 * When the TILER is addressed, the bits:
 *   [28:27] = 0x0 for 8-bit tiled
 *             0x1 for 16-bit tiled
 *             0x2 for 32-bit tiled
 *             0x3 for page mode
 *   [31:29] = 0x0 for 0-degree view
 *             0x1 for 180-degree view + mirroring
 *             0x2 for 0-degree view + mirroring
 *             0x3 for 180-degree view
 *             0x4 for 270-degree view + mirroring
 *             0x5 for 270-degree view
 *             0x6 for 90-degree view
 *             0x7 for 90-degree view + mirroring
 * Otherwise the bits indicated the corresponding bit address to access
 * the SDRAM.
 */
static u32 tiler_get_address(enum tiler_fmt fmt, u32 orient, u32 x, u32 y)
{
	u32 x_bits, y_bits, tmp, x_mask, y_mask, alignment;

	x_bits = CONT_WIDTH_BITS - geom[fmt].x_shft;
	y_bits = CONT_HEIGHT_BITS - geom[fmt].y_shft;
	alignment = geom[fmt].x_shft + geom[fmt].y_shft;

	/* validate coordinate */
	x_mask = MASK(x_bits);
	y_mask = MASK(y_bits);

	if (x < 0 || x > x_mask || y < 0 || y > y_mask) {
		DBG("invalid coords: %u < 0 || %u > %u || %u < 0 || %u > %u",
				x, x, x_mask, y, y, y_mask);
		return 0;
	}

	/* account for mirroring */
	if (orient & MASK_X_INVERT)
		x ^= x_mask;
	if (orient & MASK_Y_INVERT)
		y ^= y_mask;

	/* get coordinate address */
	if (orient & MASK_XY_FLIP)
		tmp = ((x << y_bits) + y);
	else
		tmp = ((y << x_bits) + x);

	return TIL_ADDR((tmp << alignment), orient, fmt);
}

dma_addr_t tiler_ssptr(struct tiler_block *block)
{
	BUG_ON(!validfmt(block->fmt));

	return TILVIEW_8BIT + tiler_get_address(block->fmt, 0,
			block->area.p0.x * geom[block->fmt].slot_w,
			block->area.p0.y * geom[block->fmt].slot_h);
}

dma_addr_t tiler_tsptr(struct tiler_block *block, uint32_t orient,
		uint32_t x, uint32_t y)
{
	struct tcm_pt *p = &block->area.p0;
	BUG_ON(!validfmt(block->fmt));

	return tiler_get_address(block->fmt, orient,
			(p->x * geom[block->fmt].slot_w) + x,
			(p->y * geom[block->fmt].slot_h) + y);
}

void tiler_align(enum tiler_fmt fmt, uint16_t *w, uint16_t *h)
{
	BUG_ON(!validfmt(fmt));
	*w = round_up(*w, geom[fmt].slot_w);
	*h = round_up(*h, geom[fmt].slot_h);
}

uint32_t tiler_stride(enum tiler_fmt fmt, uint32_t orient)
{
	BUG_ON(!validfmt(fmt));

	if (orient & MASK_XY_FLIP)
		return 1 << (CONT_HEIGHT_BITS + geom[fmt].x_shft);
	else
		return 1 << (CONT_WIDTH_BITS + geom[fmt].y_shft);
}

size_t tiler_size(enum tiler_fmt fmt, uint16_t w, uint16_t h)
{
	tiler_align(fmt, &w, &h);
	return geom[fmt].cpp * w * h;
}

size_t tiler_vsize(enum tiler_fmt fmt, uint16_t w, uint16_t h)
{
	BUG_ON(!validfmt(fmt));
	return round_up(geom[fmt].cpp * w, PAGE_SIZE) * h;
}

bool dmm_is_available(void)
{
	return omap_dmm ? true : false;
}

static int omap_dmm_remove(struct platform_device *dev)
{
	struct tiler_block *block, *_block;
	int i;
	unsigned long flags;

	if (omap_dmm) {
		/* free all area regions */
		spin_lock_irqsave(&list_lock, flags);
		list_for_each_entry_safe(block, _block, &omap_dmm->alloc_head,
					alloc_node) {
			list_del(&block->alloc_node);
			kfree(block);
		}
		spin_unlock_irqrestore(&list_lock, flags);

		for (i = 0; i < omap_dmm->num_lut; i++)
			if (omap_dmm->tcm && omap_dmm->tcm[i])
				omap_dmm->tcm[i]->deinit(omap_dmm->tcm[i]);
		kfree(omap_dmm->tcm);

		kfree(omap_dmm->engines);
		if (omap_dmm->refill_va)
			dma_free_writecombine(omap_dmm->dev,
				REFILL_BUFFER_SIZE * omap_dmm->num_engines,
				omap_dmm->refill_va,
				omap_dmm->refill_pa);
		if (omap_dmm->dummy_page)
			__free_page(omap_dmm->dummy_page);

		if (omap_dmm->irq > 0)
			free_irq(omap_dmm->irq, omap_dmm);

		iounmap(omap_dmm->base);
		kfree(omap_dmm);
		omap_dmm = NULL;
	}

	return 0;
}

static int omap_dmm_probe(struct platform_device *dev)
{
	int ret = -EFAULT, i;
	struct tcm_area area = {0};
	u32 hwinfo, pat_geom;
	struct resource *mem;

	omap_dmm = kzalloc(sizeof(*omap_dmm), GFP_KERNEL);
	if (!omap_dmm)
		goto fail;

	/* initialize lists */
	INIT_LIST_HEAD(&omap_dmm->alloc_head);
	INIT_LIST_HEAD(&omap_dmm->idle_head);

	init_waitqueue_head(&omap_dmm->engine_queue);

	/* lookup hwmod data - base address and irq */
	mem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&dev->dev, "failed to get base address resource\n");
		goto fail;
	}

	omap_dmm->base = ioremap(mem->start, SZ_2K);

	if (!omap_dmm->base) {
		dev_err(&dev->dev, "failed to get dmm base address\n");
		goto fail;
	}

	omap_dmm->irq = platform_get_irq(dev, 0);
	if (omap_dmm->irq < 0) {
		dev_err(&dev->dev, "failed to get IRQ resource\n");
		goto fail;
	}

	omap_dmm->dev = &dev->dev;

	hwinfo = readl(omap_dmm->base + DMM_PAT_HWINFO);
	omap_dmm->num_engines = (hwinfo >> 24) & 0x1F;
	omap_dmm->num_lut = (hwinfo >> 16) & 0x1F;
	omap_dmm->container_width = 256;
	omap_dmm->container_height = 128;

	atomic_set(&omap_dmm->engine_counter, omap_dmm->num_engines);

	/* read out actual LUT width and height */
	pat_geom = readl(omap_dmm->base + DMM_PAT_GEOMETRY);
	omap_dmm->lut_width = ((pat_geom >> 16) & 0xF) << 5;
	omap_dmm->lut_height = ((pat_geom >> 24) & 0xF) << 5;

	/* increment LUT by one if on OMAP5 */
	/* LUT has twice the height, and is split into a separate container */
	if (omap_dmm->lut_height != omap_dmm->container_height)
		omap_dmm->num_lut++;

	/* initialize DMM registers */
	writel(0x88888888, omap_dmm->base + DMM_PAT_VIEW__0);
	writel(0x88888888, omap_dmm->base + DMM_PAT_VIEW__1);
	writel(0x80808080, omap_dmm->base + DMM_PAT_VIEW_MAP__0);
	writel(0x80000000, omap_dmm->base + DMM_PAT_VIEW_MAP_BASE);
	writel(0x88888888, omap_dmm->base + DMM_TILER_OR__0);
	writel(0x88888888, omap_dmm->base + DMM_TILER_OR__1);

	ret = request_irq(omap_dmm->irq, omap_dmm_irq_handler, IRQF_SHARED,
				"omap_dmm_irq_handler", omap_dmm);

	if (ret) {
		dev_err(&dev->dev, "couldn't register IRQ %d, error %d\n",
			omap_dmm->irq, ret);
		omap_dmm->irq = -1;
		goto fail;
	}

	/* Enable all interrupts for each refill engine except
	 * ERR_LUT_MISS<n> (which is just advisory, and we don't care
	 * about because we want to be able to refill live scanout
	 * buffers for accelerated pan/scroll) and FILL_DSC<n> which
	 * we just generally don't care about.
	 */
	writel(0x7e7e7e7e, omap_dmm->base + DMM_PAT_IRQENABLE_SET);

	omap_dmm->dummy_page = alloc_page(GFP_KERNEL | __GFP_DMA32);
	if (!omap_dmm->dummy_page) {
		dev_err(&dev->dev, "could not allocate dummy page\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* set dma mask for device */
	ret = dma_set_coherent_mask(&dev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto fail;

	omap_dmm->dummy_pa = page_to_phys(omap_dmm->dummy_page);

	/* alloc refill memory */
	omap_dmm->refill_va = dma_alloc_writecombine(&dev->dev,
				REFILL_BUFFER_SIZE * omap_dmm->num_engines,
				&omap_dmm->refill_pa, GFP_KERNEL);
	if (!omap_dmm->refill_va) {
		dev_err(&dev->dev, "could not allocate refill memory\n");
		goto fail;
	}

	/* alloc engines */
	omap_dmm->engines = kcalloc(omap_dmm->num_engines,
				    sizeof(struct refill_engine), GFP_KERNEL);
	if (!omap_dmm->engines) {
		ret = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < omap_dmm->num_engines; i++) {
		omap_dmm->engines[i].id = i;
		omap_dmm->engines[i].dmm = omap_dmm;
		omap_dmm->engines[i].refill_va = omap_dmm->refill_va +
						(REFILL_BUFFER_SIZE * i);
		omap_dmm->engines[i].refill_pa = omap_dmm->refill_pa +
						(REFILL_BUFFER_SIZE * i);
		init_waitqueue_head(&omap_dmm->engines[i].wait_for_refill);

		list_add(&omap_dmm->engines[i].idle_node, &omap_dmm->idle_head);
	}

	omap_dmm->tcm = kcalloc(omap_dmm->num_lut, sizeof(*omap_dmm->tcm),
				GFP_KERNEL);
	if (!omap_dmm->tcm) {
		ret = -ENOMEM;
		goto fail;
	}

	/* init containers */
	/* Each LUT is associated with a TCM (container manager).  We use the
	   lut_id to denote the lut_id used to identify the correct LUT for
	   programming during reill operations */
	for (i = 0; i < omap_dmm->num_lut; i++) {
		omap_dmm->tcm[i] = sita_init(omap_dmm->container_width,
						omap_dmm->container_height,
						NULL);

		if (!omap_dmm->tcm[i]) {
			dev_err(&dev->dev, "failed to allocate container\n");
			ret = -ENOMEM;
			goto fail;
		}

		omap_dmm->tcm[i]->lut_id = i;
	}

	/* assign access mode containers to applicable tcm container */
	/* OMAP 4 has 1 container for all 4 views */
	/* OMAP 5 has 2 containers, 1 for 2D and 1 for 1D */
	containers[TILFMT_8BIT] = omap_dmm->tcm[0];
	containers[TILFMT_16BIT] = omap_dmm->tcm[0];
	containers[TILFMT_32BIT] = omap_dmm->tcm[0];

	if (omap_dmm->container_height != omap_dmm->lut_height) {
		/* second LUT is used for PAGE mode.  Programming must use
		   y offset that is added to all y coordinates.  LUT id is still
		   0, because it is the same LUT, just the upper 128 lines */
		containers[TILFMT_PAGE] = omap_dmm->tcm[1];
		omap_dmm->tcm[1]->y_offset = OMAP5_LUT_OFFSET;
		omap_dmm->tcm[1]->lut_id = 0;
	} else {
		containers[TILFMT_PAGE] = omap_dmm->tcm[0];
	}

	area = (struct tcm_area) {
		.tcm = NULL,
		.p1.x = omap_dmm->container_width - 1,
		.p1.y = omap_dmm->container_height - 1,
	};

	/* initialize all LUTs to dummy page entries */
	for (i = 0; i < omap_dmm->num_lut; i++) {
		area.tcm = omap_dmm->tcm[i];
		if (fill(&area, NULL, 0, 0, true))
			dev_err(omap_dmm->dev, "refill failed");
	}

	dev_info(omap_dmm->dev, "initialized all PAT entries\n");

	return 0;

fail:
	if (omap_dmm_remove(dev))
		dev_err(&dev->dev, "cleanup failed\n");
	return ret;
}

/*
 * debugfs support
 */

#ifdef CONFIG_DEBUG_FS

static const char *alphabet = "abcdefghijklmnopqrstuvwxyz"
				"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char *special = ".,:;'\"`~!^-+";

static void fill_map(char **map, int xdiv, int ydiv, struct tcm_area *a,
							char c, bool ovw)
{
	int x, y;
	for (y = a->p0.y / ydiv; y <= a->p1.y / ydiv; y++)
		for (x = a->p0.x / xdiv; x <= a->p1.x / xdiv; x++)
			if (map[y][x] == ' ' || ovw)
				map[y][x] = c;
}

static void fill_map_pt(char **map, int xdiv, int ydiv, struct tcm_pt *p,
									char c)
{
	map[p->y / ydiv][p->x / xdiv] = c;
}

static char read_map_pt(char **map, int xdiv, int ydiv, struct tcm_pt *p)
{
	return map[p->y / ydiv][p->x / xdiv];
}

static int map_width(int xdiv, int x0, int x1)
{
	return (x1 / xdiv) - (x0 / xdiv) + 1;
}

static void text_map(char **map, int xdiv, char *nice, int yd, int x0, int x1)
{
	char *p = map[yd] + (x0 / xdiv);
	int w = (map_width(xdiv, x0, x1) - strlen(nice)) / 2;
	if (w >= 0) {
		p += w;
		while (*nice)
			*p++ = *nice++;
	}
}

static void map_1d_info(char **map, int xdiv, int ydiv, char *nice,
							struct tcm_area *a)
{
	sprintf(nice, "%dK", tcm_sizeof(*a) * 4);
	if (a->p0.y + 1 < a->p1.y) {
		text_map(map, xdiv, nice, (a->p0.y + a->p1.y) / 2 / ydiv, 0,
							256 - 1);
	} else if (a->p0.y < a->p1.y) {
		if (strlen(nice) < map_width(xdiv, a->p0.x, 256 - 1))
			text_map(map, xdiv, nice, a->p0.y / ydiv,
					a->p0.x + xdiv,	256 - 1);
		else if (strlen(nice) < map_width(xdiv, 0, a->p1.x))
			text_map(map, xdiv, nice, a->p1.y / ydiv,
					0, a->p1.y - xdiv);
	} else if (strlen(nice) + 1 < map_width(xdiv, a->p0.x, a->p1.x)) {
		text_map(map, xdiv, nice, a->p0.y / ydiv, a->p0.x, a->p1.x);
	}
}

static void map_2d_info(char **map, int xdiv, int ydiv, char *nice,
							struct tcm_area *a)
{
	sprintf(nice, "(%d*%d)", tcm_awidth(*a), tcm_aheight(*a));
	if (strlen(nice) + 1 < map_width(xdiv, a->p0.x, a->p1.x))
		text_map(map, xdiv, nice, (a->p0.y + a->p1.y) / 2 / ydiv,
							a->p0.x, a->p1.x);
}

int tiler_map_show(struct seq_file *s, void *arg)
{
	int xdiv = 2, ydiv = 1;
	char **map = NULL, *global_map;
	struct tiler_block *block;
	struct tcm_area a, p;
	int i;
	const char *m2d = alphabet;
	const char *a2d = special;
	const char *m2dp = m2d, *a2dp = a2d;
	char nice[128];
	int h_adj;
	int w_adj;
	unsigned long flags;
	int lut_idx;


	if (!omap_dmm) {
		/* early return if dmm/tiler device is not initialized */
		return 0;
	}

	h_adj = omap_dmm->container_height / ydiv;
	w_adj = omap_dmm->container_width / xdiv;

	map = kmalloc(h_adj * sizeof(*map), GFP_KERNEL);
	global_map = kmalloc((w_adj + 1) * h_adj, GFP_KERNEL);

	if (!map || !global_map)
		goto error;

	for (lut_idx = 0; lut_idx < omap_dmm->num_lut; lut_idx++) {
		memset(map, 0, h_adj * sizeof(*map));
		memset(global_map, ' ', (w_adj + 1) * h_adj);

		for (i = 0; i < omap_dmm->container_height; i++) {
			map[i] = global_map + i * (w_adj + 1);
			map[i][w_adj] = 0;
		}

		spin_lock_irqsave(&list_lock, flags);

		list_for_each_entry(block, &omap_dmm->alloc_head, alloc_node) {
			if (block->area.tcm == omap_dmm->tcm[lut_idx]) {
				if (block->fmt != TILFMT_PAGE) {
					fill_map(map, xdiv, ydiv, &block->area,
						*m2dp, true);
					if (!*++a2dp)
						a2dp = a2d;
					if (!*++m2dp)
						m2dp = m2d;
					map_2d_info(map, xdiv, ydiv, nice,
							&block->area);
				} else {
					bool start = read_map_pt(map, xdiv,
						ydiv, &block->area.p0) == ' ';
					bool end = read_map_pt(map, xdiv, ydiv,
							&block->area.p1) == ' ';

					tcm_for_each_slice(a, block->area, p)
						fill_map(map, xdiv, ydiv, &a,
							'=', true);
					fill_map_pt(map, xdiv, ydiv,
							&block->area.p0,
							start ? '<' : 'X');
					fill_map_pt(map, xdiv, ydiv,
							&block->area.p1,
							end ? '>' : 'X');
					map_1d_info(map, xdiv, ydiv, nice,
							&block->area);
				}
			}
		}

		spin_unlock_irqrestore(&list_lock, flags);

		if (s) {
			seq_printf(s, "CONTAINER %d DUMP BEGIN\n", lut_idx);
			for (i = 0; i < 128; i++)
				seq_printf(s, "%03d:%s\n", i, map[i]);
			seq_printf(s, "CONTAINER %d DUMP END\n", lut_idx);
		} else {
			dev_dbg(omap_dmm->dev, "CONTAINER %d DUMP BEGIN\n",
				lut_idx);
			for (i = 0; i < 128; i++)
				dev_dbg(omap_dmm->dev, "%03d:%s\n", i, map[i]);
			dev_dbg(omap_dmm->dev, "CONTAINER %d DUMP END\n",
				lut_idx);
		}
	}

error:
	kfree(map);
	kfree(global_map);

	return 0;
}
#endif

#ifdef CONFIG_PM
static int omap_dmm_resume(struct device *dev)
{
	struct tcm_area area;
	int i;

	if (!omap_dmm)
		return -ENODEV;

	area = (struct tcm_area) {
		.tcm = NULL,
		.p1.x = omap_dmm->container_width - 1,
		.p1.y = omap_dmm->container_height - 1,
	};

	/* initialize all LUTs to dummy page entries */
	for (i = 0; i < omap_dmm->num_lut; i++) {
		area.tcm = omap_dmm->tcm[i];
		if (fill(&area, NULL, 0, 0, true))
			dev_err(dev, "refill failed");
	}

	return 0;
}

static const struct dev_pm_ops omap_dmm_pm_ops = {
	.resume = omap_dmm_resume,
};
#endif

#if defined(CONFIG_OF)
static const struct of_device_id dmm_of_match[] = {
	{ .compatible = "ti,omap4-dmm", },
	{ .compatible = "ti,omap5-dmm", },
	{},
};
#endif

struct platform_driver omap_dmm_driver = {
	.probe = omap_dmm_probe,
	.remove = omap_dmm_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DMM_DRIVER_NAME,
		.of_match_table = of_match_ptr(dmm_of_match),
#ifdef CONFIG_PM
		.pm = &omap_dmm_pm_ops,
#endif
	},
};

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andy Gross <andy.gross@ti.com>");
MODULE_DESCRIPTION("OMAP DMM/Tiler Driver");
MODULE_ALIAS("platform:" DMM_DRIVER_NAME);
