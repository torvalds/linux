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

/*
 * Global resources
 *
 * - Display-related interrupts (can be used for vblank evasion ?)
 * - Display-list enable
 * - Header-less for WPF0
 * - DL swap
 */

#define VSP1_DL_HEADER_SIZE		76
#define VSP1_DL_BODY_SIZE		(2 * 4 * 256)
#define VSP1_DL_NUM_LISTS		3

#define VSP1_DLH_INT_ENABLE		(1 << 1)
#define VSP1_DLH_AUTO_START		(1 << 0)

struct vsp1_dl_header {
	u32 num_lists;
	struct {
		u32 num_bytes;
		u32 addr;
	} lists[8];
	u32 next_header;
	u32 flags;
} __attribute__((__packed__));

struct vsp1_dl_entry {
	u32 addr;
	u32 data;
} __attribute__((__packed__));

struct vsp1_dl_list {
	struct list_head list;

	struct vsp1_dl_manager *dlm;

	struct vsp1_dl_header *header;
	struct vsp1_dl_entry *body;
	dma_addr_t dma;
	size_t size;

	unsigned int reg_count;
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
 * Display List Transaction Management
 */

static struct vsp1_dl_list *vsp1_dl_list_alloc(struct vsp1_dl_manager *dlm)
{
	struct vsp1_dl_list *dl;
	size_t header_size;

	/* The body needs to be aligned on a 8 bytes boundary, pad the header
	 * size to allow allocating both in a single operation.
	 */
	header_size = dlm->mode == VSP1_DL_MODE_HEADER
		    ? ALIGN(sizeof(struct vsp1_dl_header), 8)
		    : 0;

	dl = kzalloc(sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return NULL;

	dl->dlm = dlm;
	dl->size = header_size + VSP1_DL_BODY_SIZE;

	dl->header = dma_alloc_wc(dlm->vsp1->dev, dl->size, &dl->dma,
				  GFP_KERNEL);
	if (!dl->header) {
		kfree(dl);
		return NULL;
	}

	if (dlm->mode == VSP1_DL_MODE_HEADER) {
		memset(dl->header, 0, sizeof(*dl->header));
		dl->header->lists[0].addr = dl->dma + header_size;
		dl->header->flags = VSP1_DLH_INT_ENABLE;
	}

	dl->body = ((void *)dl->header) + header_size;

	return dl;
}

static void vsp1_dl_list_free(struct vsp1_dl_list *dl)
{
	dma_free_wc(dl->dlm->vsp1->dev, dl->size, dl->header, dl->dma);
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

/**
 * vsp1_dl_list_put - Release a display list
 * @dl: The display list
 *
 * Release the display list and return it to the pool of free lists.
 *
 * This function must be called with the display list manager lock held.
 *
 * Passing a NULL pointer to this function is safe, in that case no operation
 * will be performed.
 */
void vsp1_dl_list_put(struct vsp1_dl_list *dl)
{
	if (!dl)
		return;

	dl->reg_count = 0;

	list_add_tail(&dl->list, &dl->dlm->free);
}

void vsp1_dl_list_write(struct vsp1_dl_list *dl, u32 reg, u32 data)
{
	dl->body[dl->reg_count].addr = reg;
	dl->body[dl->reg_count].data = data;
	dl->reg_count++;
}

void vsp1_dl_list_commit(struct vsp1_dl_list *dl)
{
	struct vsp1_dl_manager *dlm = dl->dlm;
	struct vsp1_device *vsp1 = dlm->vsp1;
	unsigned long flags;
	bool update;

	spin_lock_irqsave(&dlm->lock, flags);

	if (dl->dlm->mode == VSP1_DL_MODE_HEADER) {
		/* Program the hardware with the display list body address and
		 * size. In header mode the caller guarantees that the hardware
		 * is idle at this point.
		 */
		dl->header->lists[0].num_bytes = dl->reg_count * 8;
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
		vsp1_dl_list_put(dlm->pending);
		dlm->pending = dl;
		goto done;
	}

	/* Program the hardware with the display list body address and size.
	 * The UPD bit will be cleared by the device when the display list is
	 * processed.
	 */
	vsp1_write(vsp1, VI6_DL_HDR_ADDR(0), dl->dma);
	vsp1_write(vsp1, VI6_DL_BODY_SIZE, VI6_DL_BODY_SIZE_UPD |
		   (dl->reg_count * 8));

	vsp1_dl_list_put(dlm->queued);
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
	vsp1_dl_list_put(dlm->active);
	dlm->active = NULL;

	spin_unlock(&dlm->lock);
}

void vsp1_dlm_irq_frame_end(struct vsp1_dl_manager *dlm)
{
	struct vsp1_device *vsp1 = dlm->vsp1;

	spin_lock(&dlm->lock);

	vsp1_dl_list_put(dlm->active);
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

		vsp1_write(vsp1, VI6_DL_HDR_ADDR(0), dl->dma);
		vsp1_write(vsp1, VI6_DL_BODY_SIZE, VI6_DL_BODY_SIZE_UPD |
			   (dl->reg_count * 8));

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
	vsp1_dl_list_put(dlm->active);
	vsp1_dl_list_put(dlm->queued);
	vsp1_dl_list_put(dlm->pending);

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
