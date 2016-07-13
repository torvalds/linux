/*
 * vsp1_dl.h  --  R-Car VSP1 Display List
 *
 * Copyright (C) 2015 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#include "vsp1.h"
#include "vsp1_dl.h"

#define VSP1_DL_NUM_ENTRIES		256
#define VSP1_DL_NUM_LISTS		3

#define VSP1_DLH_INT_ENABLE		(1 << 1)
#define VSP1_DLH_AUTO_START		(1 << 0)

struct vsp1_dl_header_list {
	u32 num_bytes;
	u32 addr;
} __attribute__((__packed__));

struct vsp1_dl_header {
	u32 num_lists;
	struct vsp1_dl_header_list lists[8];
	u32 next_header;
	u32 flags;
} __attribute__((__packed__));

struct vsp1_dl_entry {
	u32 addr;
	u32 data;
} __attribute__((__packed__));

/**
 * struct vsp1_dl_body - Display list body
 * @list: entry in the display list list of bodies
 * @vsp1: the VSP1 device
 * @entries: array of entries
 * @dma: DMA address of the entries
 * @size: size of the DMA memory in bytes
 * @num_entries: number of stored entries
 */
struct vsp1_dl_body {
	struct list_head list;
	struct vsp1_device *vsp1;

	struct vsp1_dl_entry *entries;
	dma_addr_t dma;
	size_t size;

	unsigned int num_entries;
};

/**
 * struct vsp1_dl_list - Display list
 * @list: entry in the display list manager lists
 * @dlm: the display list manager
 * @header: display list header, NULL for headerless lists
 * @dma: DMA address for the header
 * @body0: first display list body
 * @fragments: list of extra display list bodies
 */
struct vsp1_dl_list {
	struct list_head list;
	struct vsp1_dl_manager *dlm;

	struct vsp1_dl_header *header;
	dma_addr_t dma;

	struct vsp1_dl_body body0;
	struct list_head fragments;
};

enum vsp1_dl_mode {
	VSP1_DL_MODE_HEADER,
	VSP1_DL_MODE_HEADERLESS,
};

/**
 * struct vsp1_dl_manager - Display List manager
 * @index: index of the related WPF
 * @mode: display list operation mode (header or headerless)
 * @vsp1: the VSP1 device
 * @lock: protects the active, queued and pending lists
 * @free: array of all free display lists
 * @active: list currently being processed (loaded) by hardware
 * @queued: list queued to the hardware (written to the DL registers)
 * @pending: list waiting to be queued to the hardware
 */
struct vsp1_dl_manager {
	unsigned int index;
	enum vsp1_dl_mode mode;
	struct vsp1_device *vsp1;

	spinlock_t lock;
	struct list_head free;
	struct vsp1_dl_list *active;
	struct vsp1_dl_list *queued;
	struct vsp1_dl_list *pending;
};

/* -----------------------------------------------------------------------------
 * Display List Body Management
 */

/*
 * Initialize a display list body object and allocate DMA memory for the body
 * data. The display list body object is expected to have been initialized to
 * 0 when allocated.
 */
static int vsp1_dl_body_init(struct vsp1_device *vsp1,
			     struct vsp1_dl_body *dlb, unsigned int num_entries,
			     size_t extra_size)
{
	size_t size = num_entries * sizeof(*dlb->entries) + extra_size;

	dlb->vsp1 = vsp1;
	dlb->size = size;

	dlb->entries = dma_alloc_wc(vsp1->dev, dlb->size, &dlb->dma,
				    GFP_KERNEL);
	if (!dlb->entries)
		return -ENOMEM;

	return 0;
}

/*
 * Cleanup a display list body and free allocated DMA memory allocated.
 */
static void vsp1_dl_body_cleanup(struct vsp1_dl_body *dlb)
{
	dma_free_wc(dlb->vsp1->dev, dlb->size, dlb->entries, dlb->dma);
}

/**
 * vsp1_dl_fragment_alloc - Allocate a display list fragment
 * @vsp1: The VSP1 device
 * @num_entries: The maximum number of entries that the fragment can contain
 *
 * Allocate a display list fragment with enough memory to contain the requested
 * number of entries.
 *
 * Return a pointer to a fragment on success or NULL if memory can't be
 * allocated.
 */
struct vsp1_dl_body *vsp1_dl_fragment_alloc(struct vsp1_device *vsp1,
					    unsigned int num_entries)
{
	struct vsp1_dl_body *dlb;
	int ret;

	dlb = kzalloc(sizeof(*dlb), GFP_KERNEL);
	if (!dlb)
		return NULL;

	ret = vsp1_dl_body_init(vsp1, dlb, num_entries, 0);
	if (ret < 0) {
		kfree(dlb);
		return NULL;
	}

	return dlb;
}

/**
 * vsp1_dl_fragment_free - Free a display list fragment
 * @dlb: The fragment
 *
 * Free the given display list fragment and the associated DMA memory.
 *
 * Fragments must only be freed explicitly if they are not added to a display
 * list, as the display list will take ownership of them and free them
 * otherwise. Manual free typically happens at cleanup time for fragments that
 * have been allocated but not used.
 *
 * Passing a NULL pointer to this function is safe, in that case no operation
 * will be performed.
 */
void vsp1_dl_fragment_free(struct vsp1_dl_body *dlb)
{
	if (!dlb)
		return;

	vsp1_dl_body_cleanup(dlb);
	kfree(dlb);
}

/**
 * vsp1_dl_fragment_write - Write a register to a display list fragment
 * @dlb: The fragment
 * @reg: The register address
 * @data: The register value
 *
 * Write the given register and value to the display list fragment. The maximum
 * number of entries that can be written in a fragment is specified when the
 * fragment is allocated by vsp1_dl_fragment_alloc().
 */
void vsp1_dl_fragment_write(struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	dlb->entries[dlb->num_entries].addr = reg;
	dlb->entries[dlb->num_entries].data = data;
	dlb->num_entries++;
}

/* -----------------------------------------------------------------------------
 * Display List Transaction Management
 */

static struct vsp1_dl_list *vsp1_dl_list_alloc(struct vsp1_dl_manager *dlm)
{
	struct vsp1_dl_list *dl;
	size_t header_size;
	int ret;

	dl = kzalloc(sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return NULL;

	INIT_LIST_HEAD(&dl->fragments);
	dl->dlm = dlm;

	/* Initialize the display list body and allocate DMA memory for the body
	 * and the optional header. Both are allocated together to avoid memory
	 * fragmentation, with the header located right after the body in
	 * memory.
	 */
	header_size = dlm->mode == VSP1_DL_MODE_HEADER
		    ? ALIGN(sizeof(struct vsp1_dl_header), 8)
		    : 0;

	ret = vsp1_dl_body_init(dlm->vsp1, &dl->body0, VSP1_DL_NUM_ENTRIES,
				header_size);
	if (ret < 0) {
		kfree(dl);
		return NULL;
	}

	if (dlm->mode == VSP1_DL_MODE_HEADER) {
		size_t header_offset = VSP1_DL_NUM_ENTRIES
				     * sizeof(*dl->body0.entries);

		dl->header = ((void *)dl->body0.entries) + header_offset;
		dl->dma = dl->body0.dma + header_offset;

		memset(dl->header, 0, sizeof(*dl->header));
		dl->header->lists[0].addr = dl->body0.dma;
		dl->header->flags = VSP1_DLH_INT_ENABLE;
	}

	return dl;
}

static void vsp1_dl_list_free_fragments(struct vsp1_dl_list *dl)
{
	struct vsp1_dl_body *dlb, *next;

	list_for_each_entry_safe(dlb, next, &dl->fragments, list) {
		list_del(&dlb->list);
		vsp1_dl_body_cleanup(dlb);
		kfree(dlb);
	}
}

static void vsp1_dl_list_free(struct vsp1_dl_list *dl)
{
	vsp1_dl_body_cleanup(&dl->body0);
	vsp1_dl_list_free_fragments(dl);
	kfree(dl);
}

/**
 * vsp1_dl_list_get - Get a free display list
 * @dlm: The display list manager
 *
 * Get a display list from the pool of free lists and return it.
 *
 * This function must be called without the display list manager lock held.
 */
struct vsp1_dl_list *vsp1_dl_list_get(struct vsp1_dl_manager *dlm)
{
	struct vsp1_dl_list *dl = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dlm->lock, flags);

	if (!list_empty(&dlm->free)) {
		dl = list_first_entry(&dlm->free, struct vsp1_dl_list, list);
		list_del(&dl->list);
	}

	spin_unlock_irqrestore(&dlm->lock, flags);

	return dl;
}

/* This function must be called with the display list manager lock held.*/
static void __vsp1_dl_list_put(struct vsp1_dl_list *dl)
{
	if (!dl)
		return;

	vsp1_dl_list_free_fragments(dl);
	dl->body0.num_entries = 0;

	list_add_tail(&dl->list, &dl->dlm->free);
}

/**
 * vsp1_dl_list_put - Release a display list
 * @dl: The display list
 *
 * Release the display list and return it to the pool of free lists.
 *
 * Passing a NULL pointer to this function is safe, in that case no operation
 * will be performed.
 */
void vsp1_dl_list_put(struct vsp1_dl_list *dl)
{
	unsigned long flags;

	if (!dl)
		return;

	spin_lock_irqsave(&dl->dlm->lock, flags);
	__vsp1_dl_list_put(dl);
	spin_unlock_irqrestore(&dl->dlm->lock, flags);
}

/**
 * vsp1_dl_list_write - Write a register to the display list
 * @dl: The display list
 * @reg: The register address
 * @data: The register value
 *
 * Write the given register and value to the display list. Up to 256 registers
 * can be written per display list.
 */
void vsp1_dl_list_write(struct vsp1_dl_list *dl, u32 reg, u32 data)
{
	vsp1_dl_fragment_write(&dl->body0, reg, data);
}

/**
 * vsp1_dl_list_add_fragment - Add a fragment to the display list
 * @dl: The display list
 * @dlb: The fragment
 *
 * Add a display list body as a fragment to a display list. Registers contained
 * in fragments are processed after registers contained in the main display
 * list, in the order in which fragments are added.
 *
 * Adding a fragment to a display list passes ownership of the fragment to the
 * list. The caller must not touch the fragment after this call, and must not
 * free it explicitly with vsp1_dl_fragment_free().
 *
 * Fragments are only usable for display lists in header mode. Attempt to
 * add a fragment to a header-less display list will return an error.
 */
int vsp1_dl_list_add_fragment(struct vsp1_dl_list *dl,
			      struct vsp1_dl_body *dlb)
{
	/* Multi-body lists are only available in header mode. */
	if (dl->dlm->mode != VSP1_DL_MODE_HEADER)
		return -EINVAL;

	list_add_tail(&dlb->list, &dl->fragments);
	return 0;
}

void vsp1_dl_list_commit(struct vsp1_dl_list *dl)
{
	struct vsp1_dl_manager *dlm = dl->dlm;
	struct vsp1_device *vsp1 = dlm->vsp1;
	unsigned long flags;
	bool update;

	spin_lock_irqsave(&dlm->lock, flags);

	if (dl->dlm->mode == VSP1_DL_MODE_HEADER) {
		struct vsp1_dl_header_list *hdr = dl->header->lists;
		struct vsp1_dl_body *dlb;
		unsigned int num_lists = 0;

		/* Fill the header with the display list bodies addresses and
		 * sizes. The address of the first body has already been filled
		 * when the display list was allocated.
		 *
		 * In header mode the caller guarantees that the hardware is
		 * idle at this point.
		 */
		hdr->num_bytes = dl->body0.num_entries
			       * sizeof(*dl->header->lists);

		list_for_each_entry(dlb, &dl->fragments, list) {
			num_lists++;
			hdr++;

			hdr->addr = dlb->dma;
			hdr->num_bytes = dlb->num_entries
				       * sizeof(*dl->header->lists);
		}

		dl->header->num_lists = num_lists;
		vsp1_write(vsp1, VI6_DL_HDR_ADDR(dlm->index), dl->dma);

		dlm->active = dl;
		goto done;
	}

	/* Once the UPD bit has been set the hardware can start processing the
	 * display list at any time and we can't touch the address and size
	 * registers. In that case mark the update as pending, it will be
	 * queued up to the hardware by the frame end interrupt handler.
	 */
	update = !!(vsp1_read(vsp1, VI6_DL_BODY_SIZE) & VI6_DL_BODY_SIZE_UPD);
	if (update) {
		__vsp1_dl_list_put(dlm->pending);
		dlm->pending = dl;
		goto done;
	}

	/* Program the hardware with the display list body address and size.
	 * The UPD bit will be cleared by the device when the display list is
	 * processed.
	 */
	vsp1_write(vsp1, VI6_DL_HDR_ADDR(0), dl->body0.dma);
	vsp1_write(vsp1, VI6_DL_BODY_SIZE, VI6_DL_BODY_SIZE_UPD |
		   (dl->body0.num_entries * sizeof(*dl->header->lists)));

	__vsp1_dl_list_put(dlm->queued);
	dlm->queued = dl;

done:
	spin_unlock_irqrestore(&dlm->lock, flags);
}

/* -----------------------------------------------------------------------------
 * Display List Manager
 */

/* Interrupt Handling */
void vsp1_dlm_irq_display_start(struct vsp1_dl_manager *dlm)
{
	spin_lock(&dlm->lock);

	/* The display start interrupt signals the end of the display list
	 * processing by the device. The active display list, if any, won't be
	 * accessed anymore and can be reused.
	 */
	__vsp1_dl_list_put(dlm->active);
	dlm->active = NULL;

	spin_unlock(&dlm->lock);
}

void vsp1_dlm_irq_frame_end(struct vsp1_dl_manager *dlm)
{
	struct vsp1_device *vsp1 = dlm->vsp1;

	spin_lock(&dlm->lock);

	__vsp1_dl_list_put(dlm->active);
	dlm->active = NULL;

	/* Header mode is used for mem-to-mem pipelines only. We don't need to
	 * perform any operation as there can't be any new display list queued
	 * in that case.
	 */
	if (dlm->mode == VSP1_DL_MODE_HEADER)
		goto done;

	/* The UPD bit set indicates that the commit operation raced with the
	 * interrupt and occurred after the frame end event and UPD clear but
	 * before interrupt processing. The hardware hasn't taken the update
	 * into account yet, we'll thus skip one frame and retry.
	 */
	if (vsp1_read(vsp1, VI6_DL_BODY_SIZE) & VI6_DL_BODY_SIZE_UPD)
		goto done;

	/* The device starts processing the queued display list right after the
	 * frame end interrupt. The display list thus becomes active.
	 */
	if (dlm->queued) {
		dlm->active = dlm->queued;
		dlm->queued = NULL;
	}

	/* Now that the UPD bit has been cleared we can queue the next display
	 * list to the hardware if one has been prepared.
	 */
	if (dlm->pending) {
		struct vsp1_dl_list *dl = dlm->pending;

		vsp1_write(vsp1, VI6_DL_HDR_ADDR(0), dl->body0.dma);
		vsp1_write(vsp1, VI6_DL_BODY_SIZE, VI6_DL_BODY_SIZE_UPD |
			   (dl->body0.num_entries *
			    sizeof(*dl->header->lists)));

		dlm->queued = dl;
		dlm->pending = NULL;
	}

done:
	spin_unlock(&dlm->lock);
}

/* Hardware Setup */
void vsp1_dlm_setup(struct vsp1_device *vsp1)
{
	u32 ctrl = (256 << VI6_DL_CTRL_AR_WAIT_SHIFT)
		 | VI6_DL_CTRL_DC2 | VI6_DL_CTRL_DC1 | VI6_DL_CTRL_DC0
		 | VI6_DL_CTRL_DLE;

	/* The DRM pipeline operates with display lists in Continuous Frame
	 * Mode, all other pipelines use manual start.
	 */
	if (vsp1->drm)
		ctrl |= VI6_DL_CTRL_CFM0 | VI6_DL_CTRL_NH0;

	vsp1_write(vsp1, VI6_DL_CTRL, ctrl);
	vsp1_write(vsp1, VI6_DL_SWAP, VI6_DL_SWAP_LWS);
}

void vsp1_dlm_reset(struct vsp1_dl_manager *dlm)
{
	unsigned long flags;

	spin_lock_irqsave(&dlm->lock, flags);

	__vsp1_dl_list_put(dlm->active);
	__vsp1_dl_list_put(dlm->queued);
	__vsp1_dl_list_put(dlm->pending);

	spin_unlock_irqrestore(&dlm->lock, flags);

	dlm->active = NULL;
	dlm->queued = NULL;
	dlm->pending = NULL;
}

struct vsp1_dl_manager *vsp1_dlm_create(struct vsp1_device *vsp1,
					unsigned int index,
					unsigned int prealloc)
{
	struct vsp1_dl_manager *dlm;
	unsigned int i;

	dlm = devm_kzalloc(vsp1->dev, sizeof(*dlm), GFP_KERNEL);
	if (!dlm)
		return NULL;

	dlm->index = index;
	dlm->mode = index == 0 && !vsp1->info->uapi
		  ? VSP1_DL_MODE_HEADERLESS : VSP1_DL_MODE_HEADER;
	dlm->vsp1 = vsp1;

	spin_lock_init(&dlm->lock);
	INIT_LIST_HEAD(&dlm->free);

	for (i = 0; i < prealloc; ++i) {
		struct vsp1_dl_list *dl;

		dl = vsp1_dl_list_alloc(dlm);
		if (!dl)
			return NULL;

		list_add_tail(&dl->list, &dlm->free);
	}

	return dlm;
}

void vsp1_dlm_destroy(struct vsp1_dl_manager *dlm)
{
	struct vsp1_dl_list *dl, *next;

	if (!dlm)
		return;

	list_for_each_entry_safe(dl, next, &dlm->free, list) {
		list_del(&dl->list);
		vsp1_dl_list_free(dl);
	}
}
