// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

#define TOTAL_SLOTS (VCHIQ_SLOT_ZERO_SLOTS + 2 * 32)

#include "vchiq_arm.h"
#include "vchiq_connected.h"
#include "vchiq_pagelist.h"

#define MAX_FRAGMENTS (VCHIQ_NUM_CURRENT_BULKS * 2)

#define VCHIQ_PLATFORM_FRAGMENTS_OFFSET_IDX 0
#define VCHIQ_PLATFORM_FRAGMENTS_COUNT_IDX  1

#define BELL0	0x00
#define BELL2	0x08

struct vchiq_2835_state {
	int inited;
	struct vchiq_arm_state arm_state;
};

struct vchiq_pagelist_info {
	struct pagelist *pagelist;
	size_t pagelist_buffer_size;
	dma_addr_t dma_addr;
	enum dma_data_direction dma_dir;
	unsigned int num_pages;
	unsigned int pages_need_release;
	struct page **pages;
	struct scatterlist *scatterlist;
	unsigned int scatterlist_mapped;
};

static void __iomem *g_regs;
/* This value is the size of the L2 cache lines as understood by the
 * VPU firmware, which determines the required alignment of the
 * offsets/sizes in pagelists.
 *
 * Modern VPU firmware looks for a DT "cache-line-size" property in
 * the VCHIQ node and will overwrite it with the actual L2 cache size,
 * which the kernel must then respect.  That property was rejected
 * upstream, so we have to use the VPU firmware's compatibility value
 * of 32.
 */
static unsigned int g_cache_line_size = 32;
static unsigned int g_fragments_size;
static char *g_fragments_base;
static char *g_free_fragments;
static struct semaphore g_free_fragments_sema;
static struct device *g_dev;

static DEFINE_SEMAPHORE(g_free_fragments_mutex);

static irqreturn_t
vchiq_doorbell_irq(int irq, void *dev_id);

static struct vchiq_pagelist_info *
create_pagelist(char __user *buf, size_t count, unsigned short type);

static void
free_pagelist(struct vchiq_pagelist_info *pagelistinfo,
	      int actual);

int vchiq_platform_init(struct platform_device *pdev, struct vchiq_state *state)
{
	struct device *dev = &pdev->dev;
	struct vchiq_drvdata *drvdata = platform_get_drvdata(pdev);
	struct rpi_firmware *fw = drvdata->fw;
	struct vchiq_slot_zero *vchiq_slot_zero;
	struct resource *res;
	void *slot_mem;
	dma_addr_t slot_phys;
	u32 channelbase;
	int slot_mem_size, frag_mem_size;
	int err, irq, i;

	/*
	 * VCHI messages between the CPU and firmware use
	 * 32-bit bus addresses.
	 */
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));

	if (err < 0)
		return err;

	g_cache_line_size = drvdata->cache_line_size;
	g_fragments_size = 2 * g_cache_line_size;

	/* Allocate space for the channels in coherent memory */
	slot_mem_size = PAGE_ALIGN(TOTAL_SLOTS * VCHIQ_SLOT_SIZE);
	frag_mem_size = PAGE_ALIGN(g_fragments_size * MAX_FRAGMENTS);

	slot_mem = dmam_alloc_coherent(dev, slot_mem_size + frag_mem_size,
				       &slot_phys, GFP_KERNEL);
	if (!slot_mem) {
		dev_err(dev, "could not allocate DMA memory\n");
		return -ENOMEM;
	}

	WARN_ON(((unsigned long)slot_mem & (PAGE_SIZE - 1)) != 0);

	vchiq_slot_zero = vchiq_init_slots(slot_mem, slot_mem_size);
	if (!vchiq_slot_zero)
		return -EINVAL;

	vchiq_slot_zero->platform_data[VCHIQ_PLATFORM_FRAGMENTS_OFFSET_IDX] =
		(int)slot_phys + slot_mem_size;
	vchiq_slot_zero->platform_data[VCHIQ_PLATFORM_FRAGMENTS_COUNT_IDX] =
		MAX_FRAGMENTS;

	g_fragments_base = (char *)slot_mem + slot_mem_size;

	g_free_fragments = g_fragments_base;
	for (i = 0; i < (MAX_FRAGMENTS - 1); i++) {
		*(char **)&g_fragments_base[i*g_fragments_size] =
			&g_fragments_base[(i + 1)*g_fragments_size];
	}
	*(char **)&g_fragments_base[i * g_fragments_size] = NULL;
	sema_init(&g_free_fragments_sema, MAX_FRAGMENTS);

	if (vchiq_init_state(state, vchiq_slot_zero) != VCHIQ_SUCCESS)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	g_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(g_regs))
		return PTR_ERR(g_regs);

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "failed to get IRQ\n");
		return irq;
	}

	err = devm_request_irq(dev, irq, vchiq_doorbell_irq, IRQF_IRQPOLL,
			       "VCHIQ doorbell", state);
	if (err) {
		dev_err(dev, "failed to register irq=%d\n", irq);
		return err;
	}

	/* Send the base address of the slots to VideoCore */
	channelbase = slot_phys;
	err = rpi_firmware_property(fw, RPI_FIRMWARE_VCHIQ_INIT,
				    &channelbase, sizeof(channelbase));
	if (err || channelbase) {
		dev_err(dev, "failed to set channelbase\n");
		return err ? : -ENXIO;
	}

	g_dev = dev;
	vchiq_log_info(vchiq_arm_log_level,
		"vchiq_init - done (slots %pK, phys %pad)",
		vchiq_slot_zero, &slot_phys);

	vchiq_call_connected_callbacks();

	return 0;
}

VCHIQ_STATUS_T
vchiq_platform_init_state(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;
	struct vchiq_2835_state *platform_state;

	state->platform_state = kzalloc(sizeof(*platform_state), GFP_KERNEL);
	if (!state->platform_state)
		return VCHIQ_ERROR;

	platform_state = (struct vchiq_2835_state *)state->platform_state;

	platform_state->inited = 1;
	status = vchiq_arm_init_state(state, &platform_state->arm_state);

	if (status != VCHIQ_SUCCESS)
		platform_state->inited = 0;

	return status;
}

struct vchiq_arm_state*
vchiq_platform_get_arm_state(struct vchiq_state *state)
{
	struct vchiq_2835_state *platform_state;

	platform_state   = (struct vchiq_2835_state *)state->platform_state;

	WARN_ON_ONCE(!platform_state->inited);

	return &platform_state->arm_state;
}

void
remote_event_signal(struct remote_event *event)
{
	wmb();

	event->fired = 1;

	dsb(sy);         /* data barrier operation */

	if (event->armed)
		writel(0, g_regs + BELL2); /* trigger vc interrupt */
}

VCHIQ_STATUS_T
vchiq_prepare_bulk_data(struct vchiq_bulk *bulk, void *offset, int size,
			int dir)
{
	struct vchiq_pagelist_info *pagelistinfo;

	pagelistinfo = create_pagelist((char __user *)offset, size,
				       (dir == VCHIQ_BULK_RECEIVE)
				       ? PAGELIST_READ
				       : PAGELIST_WRITE);

	if (!pagelistinfo)
		return VCHIQ_ERROR;

	bulk->data = (void *)(unsigned long)pagelistinfo->dma_addr;

	/*
	 * Store the pagelistinfo address in remote_data,
	 * which isn't used by the slave.
	 */
	bulk->remote_data = pagelistinfo;

	return VCHIQ_SUCCESS;
}

void
vchiq_complete_bulk(struct vchiq_bulk *bulk)
{
	if (bulk && bulk->remote_data && bulk->actual)
		free_pagelist((struct vchiq_pagelist_info *)bulk->remote_data,
			      bulk->actual);
}

void
vchiq_dump_platform_state(void *dump_context)
{
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf),
		"  Platform: 2835 (VC master)");
	vchiq_dump(dump_context, buf, len + 1);
}

VCHIQ_STATUS_T
vchiq_platform_suspend(struct vchiq_state *state)
{
	return VCHIQ_ERROR;
}

VCHIQ_STATUS_T
vchiq_platform_resume(struct vchiq_state *state)
{
	return VCHIQ_SUCCESS;
}

void
vchiq_platform_paused(struct vchiq_state *state)
{
}

void
vchiq_platform_resumed(struct vchiq_state *state)
{
}

int
vchiq_platform_videocore_wanted(struct vchiq_state *state)
{
	return 1; // autosuspend not supported - videocore always wanted
}

int
vchiq_platform_use_suspend_timer(void)
{
	return 0;
}
void
vchiq_dump_platform_use_state(struct vchiq_state *state)
{
	vchiq_log_info(vchiq_arm_log_level, "Suspend timer not in use");
}
void
vchiq_platform_handle_timeout(struct vchiq_state *state)
{
	(void)state;
}
/*
 * Local functions
 */

static irqreturn_t
vchiq_doorbell_irq(int irq, void *dev_id)
{
	struct vchiq_state *state = dev_id;
	irqreturn_t ret = IRQ_NONE;
	unsigned int status;

	/* Read (and clear) the doorbell */
	status = readl(g_regs + BELL0);

	if (status & 0x4) {  /* Was the doorbell rung? */
		remote_event_pollall(state);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static void
cleanup_pagelistinfo(struct vchiq_pagelist_info *pagelistinfo)
{
	if (pagelistinfo->scatterlist_mapped) {
		dma_unmap_sg(g_dev, pagelistinfo->scatterlist,
			     pagelistinfo->num_pages, pagelistinfo->dma_dir);
	}

	if (pagelistinfo->pages_need_release) {
		unsigned int i;

		for (i = 0; i < pagelistinfo->num_pages; i++)
			put_page(pagelistinfo->pages[i]);
	}

	dma_free_coherent(g_dev, pagelistinfo->pagelist_buffer_size,
			  pagelistinfo->pagelist, pagelistinfo->dma_addr);
}

/* There is a potential problem with partial cache lines (pages?)
 * at the ends of the block when reading. If the CPU accessed anything in
 * the same line (page?) then it may have pulled old data into the cache,
 * obscuring the new data underneath. We can solve this by transferring the
 * partial cache lines separately, and allowing the ARM to copy into the
 * cached area.
 */

static struct vchiq_pagelist_info *
create_pagelist(char __user *buf, size_t count, unsigned short type)
{
	struct pagelist *pagelist;
	struct vchiq_pagelist_info *pagelistinfo;
	struct page **pages;
	u32 *addrs;
	unsigned int num_pages, offset, i, k;
	int actual_pages;
	size_t pagelist_size;
	struct scatterlist *scatterlist, *sg;
	int dma_buffers;
	dma_addr_t dma_addr;

	if (count >= INT_MAX - PAGE_SIZE)
		return NULL;

	offset = ((unsigned int)(unsigned long)buf & (PAGE_SIZE - 1));
	num_pages = DIV_ROUND_UP(count + offset, PAGE_SIZE);

	if (num_pages > (SIZE_MAX - sizeof(struct pagelist) -
			 sizeof(struct vchiq_pagelist_info)) /
			(sizeof(u32) + sizeof(pages[0]) +
			 sizeof(struct scatterlist)))
		return NULL;

	pagelist_size = sizeof(struct pagelist) +
			(num_pages * sizeof(u32)) +
			(num_pages * sizeof(pages[0]) +
			(num_pages * sizeof(struct scatterlist))) +
			sizeof(struct vchiq_pagelist_info);

	/* Allocate enough storage to hold the page pointers and the page
	 * list
	 */
	pagelist = dma_alloc_coherent(g_dev, pagelist_size, &dma_addr,
				      GFP_KERNEL);

	vchiq_log_trace(vchiq_arm_log_level, "%s - %pK", __func__, pagelist);

	if (!pagelist)
		return NULL;

	addrs		= pagelist->addrs;
	pages		= (struct page **)(addrs + num_pages);
	scatterlist	= (struct scatterlist *)(pages + num_pages);
	pagelistinfo	= (struct vchiq_pagelist_info *)
			  (scatterlist + num_pages);

	pagelist->length = count;
	pagelist->type = type;
	pagelist->offset = offset;

	/* Populate the fields of the pagelistinfo structure */
	pagelistinfo->pagelist = pagelist;
	pagelistinfo->pagelist_buffer_size = pagelist_size;
	pagelistinfo->dma_addr = dma_addr;
	pagelistinfo->dma_dir =  (type == PAGELIST_WRITE) ?
				  DMA_TO_DEVICE : DMA_FROM_DEVICE;
	pagelistinfo->num_pages = num_pages;
	pagelistinfo->pages_need_release = 0;
	pagelistinfo->pages = pages;
	pagelistinfo->scatterlist = scatterlist;
	pagelistinfo->scatterlist_mapped = 0;

	if (is_vmalloc_addr(buf)) {
		unsigned long length = count;
		unsigned int off = offset;

		for (actual_pages = 0; actual_pages < num_pages;
		     actual_pages++) {
			struct page *pg = vmalloc_to_page(buf + (actual_pages *
								 PAGE_SIZE));
			size_t bytes = PAGE_SIZE - off;

			if (!pg) {
				cleanup_pagelistinfo(pagelistinfo);
				return NULL;
			}

			if (bytes > length)
				bytes = length;
			pages[actual_pages] = pg;
			length -= bytes;
			off = 0;
		}
		/* do not try and release vmalloc pages */
	} else {
		actual_pages = get_user_pages_fast(
					  (unsigned long)buf & PAGE_MASK,
					  num_pages,
					  type == PAGELIST_READ,
					  pages);

		if (actual_pages != num_pages) {
			vchiq_log_info(vchiq_arm_log_level,
				       "%s - only %d/%d pages locked",
				       __func__, actual_pages, num_pages);

			/* This is probably due to the process being killed */
			while (actual_pages > 0) {
				actual_pages--;
				put_page(pages[actual_pages]);
			}
			cleanup_pagelistinfo(pagelistinfo);
			return NULL;
		}
		 /* release user pages */
		pagelistinfo->pages_need_release = 1;
	}

	/*
	 * Initialize the scatterlist so that the magic cookie
	 *  is filled if debugging is enabled
	 */
	sg_init_table(scatterlist, num_pages);
	/* Now set the pages for each scatterlist */
	for (i = 0; i < num_pages; i++)	{
		unsigned int len = PAGE_SIZE - offset;

		if (len > count)
			len = count;
		sg_set_page(scatterlist + i, pages[i], len, offset);
		offset = 0;
		count -= len;
	}

	dma_buffers = dma_map_sg(g_dev,
				 scatterlist,
				 num_pages,
				 pagelistinfo->dma_dir);

	if (dma_buffers == 0) {
		cleanup_pagelistinfo(pagelistinfo);
		return NULL;
	}

	pagelistinfo->scatterlist_mapped = 1;

	/* Combine adjacent blocks for performance */
	k = 0;
	for_each_sg(scatterlist, sg, dma_buffers, i) {
		u32 len = sg_dma_len(sg);
		u32 addr = sg_dma_address(sg);

		/* Note: addrs is the address + page_count - 1
		 * The firmware expects blocks after the first to be page-
		 * aligned and a multiple of the page size
		 */
		WARN_ON(len == 0);
		WARN_ON(i && (i != (dma_buffers - 1)) && (len & ~PAGE_MASK));
		WARN_ON(i && (addr & ~PAGE_MASK));
		if (k > 0 &&
		    ((addrs[k - 1] & PAGE_MASK) +
		     (((addrs[k - 1] & ~PAGE_MASK) + 1) << PAGE_SHIFT))
		    == (addr & PAGE_MASK))
			addrs[k - 1] += ((len + PAGE_SIZE - 1) >> PAGE_SHIFT);
		else
			addrs[k++] = (addr & PAGE_MASK) |
				(((len + PAGE_SIZE - 1) >> PAGE_SHIFT) - 1);
	}

	/* Partial cache lines (fragments) require special measures */
	if ((type == PAGELIST_READ) &&
		((pagelist->offset & (g_cache_line_size - 1)) ||
		((pagelist->offset + pagelist->length) &
		(g_cache_line_size - 1)))) {
		char *fragments;

		if (down_interruptible(&g_free_fragments_sema) != 0) {
			cleanup_pagelistinfo(pagelistinfo);
			return NULL;
		}

		WARN_ON(g_free_fragments == NULL);

		down(&g_free_fragments_mutex);
		fragments = g_free_fragments;
		WARN_ON(fragments == NULL);
		g_free_fragments = *(char **) g_free_fragments;
		up(&g_free_fragments_mutex);
		pagelist->type = PAGELIST_READ_WITH_FRAGMENTS +
			(fragments - g_fragments_base) / g_fragments_size;
	}

	return pagelistinfo;
}

static void
free_pagelist(struct vchiq_pagelist_info *pagelistinfo,
	      int actual)
{
	struct pagelist *pagelist = pagelistinfo->pagelist;
	struct page **pages = pagelistinfo->pages;
	unsigned int num_pages = pagelistinfo->num_pages;

	vchiq_log_trace(vchiq_arm_log_level, "%s - %pK, %d",
			__func__, pagelistinfo->pagelist, actual);

	/*
	 * NOTE: dma_unmap_sg must be called before the
	 * cpu can touch any of the data/pages.
	 */
	dma_unmap_sg(g_dev, pagelistinfo->scatterlist,
		     pagelistinfo->num_pages, pagelistinfo->dma_dir);
	pagelistinfo->scatterlist_mapped = 0;

	/* Deal with any partial cache lines (fragments) */
	if (pagelist->type >= PAGELIST_READ_WITH_FRAGMENTS) {
		char *fragments = g_fragments_base +
			(pagelist->type - PAGELIST_READ_WITH_FRAGMENTS) *
			g_fragments_size;
		int head_bytes, tail_bytes;

		head_bytes = (g_cache_line_size - pagelist->offset) &
			(g_cache_line_size - 1);
		tail_bytes = (pagelist->offset + actual) &
			(g_cache_line_size - 1);

		if ((actual >= 0) && (head_bytes != 0)) {
			if (head_bytes > actual)
				head_bytes = actual;

			memcpy((char *)kmap(pages[0]) +
				pagelist->offset,
				fragments,
				head_bytes);
			kunmap(pages[0]);
		}
		if ((actual >= 0) && (head_bytes < actual) &&
			(tail_bytes != 0)) {
			memcpy((char *)kmap(pages[num_pages - 1]) +
				((pagelist->offset + actual) &
				(PAGE_SIZE - 1) & ~(g_cache_line_size - 1)),
				fragments + g_cache_line_size,
				tail_bytes);
			kunmap(pages[num_pages - 1]);
		}

		down(&g_free_fragments_mutex);
		*(char **)fragments = g_free_fragments;
		g_free_fragments = fragments;
		up(&g_free_fragments_mutex);
		up(&g_free_fragments_sema);
	}

	/* Need to mark all the pages dirty. */
	if (pagelist->type != PAGELIST_WRITE &&
	    pagelistinfo->pages_need_release) {
		unsigned int i;

		for (i = 0; i < num_pages; i++)
			set_page_dirty(pages[i]);
	}

	cleanup_pagelistinfo(pagelistinfo);
}
