/* mga_dma.c -- DMA support for mga g200/g400 -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file mga_dma.c
 * DMA support for MGA G200 / G400.
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Jeff Hartmann <jhartmann@valinux.com>
 * \author Keith Whitwell <keith@tungstengraphics.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "mga_drm.h"
#include "mga_drv.h"

#define MGA_DEFAULT_USEC_TIMEOUT	10000
#define MGA_FREELIST_DEBUG		0

#define MINIMAL_CLEANUP 0
#define FULL_CLEANUP 1
static int mga_do_cleanup_dma(struct drm_device *dev, int full_cleanup);

/* ================================================================
 * Engine control
 */

int mga_do_wait_for_idle(drm_mga_private_t *dev_priv)
{
	u32 status = 0;
	int i;
	DRM_DEBUG("\n");

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		status = MGA_READ(MGA_STATUS) & MGA_ENGINE_IDLE_MASK;
		if (status == MGA_ENDPRDMASTS) {
			MGA_WRITE8(MGA_CRTC_INDEX, 0);
			return 0;
		}
		DRM_UDELAY(1);
	}

#if MGA_DMA_DEBUG
	DRM_ERROR("failed!\n");
	DRM_INFO("   status=0x%08x\n", status);
#endif
	return -EBUSY;
}

static int mga_do_dma_reset(drm_mga_private_t *dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;

	DRM_DEBUG("\n");

	/* The primary DMA stream should look like new right about now.
	 */
	primary->tail = 0;
	primary->space = primary->size;
	primary->last_flush = 0;

	sarea_priv->last_wrap = 0;

	/* FIXME: Reset counters, buffer ages etc...
	 */

	/* FIXME: What else do we need to reinitialize?  WARP stuff?
	 */

	return 0;
}

/* ================================================================
 * Primary DMA stream
 */

void mga_do_dma_flush(drm_mga_private_t *dev_priv)
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	u32 status = 0;
	int i;
	DMA_LOCALS;
	DRM_DEBUG("\n");

	/* We need to wait so that we can do an safe flush */
	for (i = 0; i < dev_priv->usec_timeout; i++) {
		status = MGA_READ(MGA_STATUS) & MGA_ENGINE_IDLE_MASK;
		if (status == MGA_ENDPRDMASTS)
			break;
		DRM_UDELAY(1);
	}

	if (primary->tail == primary->last_flush) {
		DRM_DEBUG("   bailing out...\n");
		return;
	}

	tail = primary->tail + dev_priv->primary->offset;

	/* We need to pad the stream between flushes, as the card
	 * actually (partially?) reads the first of these commands.
	 * See page 4-16 in the G400 manual, middle of the page or so.
	 */
	BEGIN_DMA(1);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

	ADVANCE_DMA();

	primary->last_flush = primary->tail;

	head = MGA_READ(MGA_PRIMADDRESS);

	if (head <= tail)
		primary->space = primary->size - primary->tail;
	else
		primary->space = head - tail;

	DRM_DEBUG("   head = 0x%06lx\n", (unsigned long)(head - dev_priv->primary->offset));
	DRM_DEBUG("   tail = 0x%06lx\n", (unsigned long)(tail - dev_priv->primary->offset));
	DRM_DEBUG("  space = 0x%06x\n", primary->space);

	mga_flush_write_combine();
	MGA_WRITE(MGA_PRIMEND, tail | dev_priv->dma_access);

	DRM_DEBUG("done.\n");
}

void mga_do_dma_wrap_start(drm_mga_private_t *dev_priv)
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	DMA_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_DMA_WRAP();

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

	ADVANCE_DMA();

	tail = primary->tail + dev_priv->primary->offset;

	primary->tail = 0;
	primary->last_flush = 0;
	primary->last_wrap++;

	head = MGA_READ(MGA_PRIMADDRESS);

	if (head == dev_priv->primary->offset)
		primary->space = primary->size;
	else
		primary->space = head - dev_priv->primary->offset;

	DRM_DEBUG("   head = 0x%06lx\n", (unsigned long)(head - dev_priv->primary->offset));
	DRM_DEBUG("   tail = 0x%06x\n", primary->tail);
	DRM_DEBUG("   wrap = %d\n", primary->last_wrap);
	DRM_DEBUG("  space = 0x%06x\n", primary->space);

	mga_flush_write_combine();
	MGA_WRITE(MGA_PRIMEND, tail | dev_priv->dma_access);

	set_bit(0, &primary->wrapped);
	DRM_DEBUG("done.\n");
}

void mga_do_dma_wrap_end(drm_mga_private_t *dev_priv)
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 head = dev_priv->primary->offset;
	DRM_DEBUG("\n");

	sarea_priv->last_wrap++;
	DRM_DEBUG("   wrap = %d\n", sarea_priv->last_wrap);

	mga_flush_write_combine();
	MGA_WRITE(MGA_PRIMADDRESS, head | MGA_DMA_GENERAL);

	clear_bit(0, &primary->wrapped);
	DRM_DEBUG("done.\n");
}

/* ================================================================
 * Freelist management
 */

#define MGA_BUFFER_USED		(~0)
#define MGA_BUFFER_FREE		0

#if MGA_FREELIST_DEBUG
static void mga_freelist_print(struct drm_device *dev)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;

	DRM_INFO("\n");
	DRM_INFO("current dispatch: last=0x%x done=0x%x\n",
		 dev_priv->sarea_priv->last_dispatch,
		 (unsigned int)(MGA_READ(MGA_PRIMADDRESS) -
				dev_priv->primary->offset));
	DRM_INFO("current freelist:\n");

	for (entry = dev_priv->head->next; entry; entry = entry->next) {
		DRM_INFO("   %p   idx=%2d  age=0x%x 0x%06lx\n",
			 entry, entry->buf->idx, entry->age.head,
			 (unsigned long)(entry->age.head - dev_priv->primary->offset));
	}
	DRM_INFO("\n");
}
#endif

static int mga_freelist_init(struct drm_device *dev, drm_mga_private_t *dev_priv)
{
	struct drm_device_dma *dma = dev->dma;
	struct drm_buf *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_freelist_t *entry;
	int i;
	DRM_DEBUG("count=%d\n", dma->buf_count);

	dev_priv->head = kzalloc(sizeof(drm_mga_freelist_t), GFP_KERNEL);
	if (dev_priv->head == NULL)
		return -ENOMEM;

	SET_AGE(&dev_priv->head->age, MGA_BUFFER_USED, 0);

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;

		entry = kzalloc(sizeof(drm_mga_freelist_t), GFP_KERNEL);
		if (entry == NULL)
			return -ENOMEM;

		entry->next = dev_priv->head->next;
		entry->prev = dev_priv->head;
		SET_AGE(&entry->age, MGA_BUFFER_FREE, 0);
		entry->buf = buf;

		if (dev_priv->head->next != NULL)
			dev_priv->head->next->prev = entry;
		if (entry->next == NULL)
			dev_priv->tail = entry;

		buf_priv->list_entry = entry;
		buf_priv->discard = 0;
		buf_priv->dispatched = 0;

		dev_priv->head->next = entry;
	}

	return 0;
}

static void mga_freelist_cleanup(struct drm_device *dev)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;
	drm_mga_freelist_t *next;
	DRM_DEBUG("\n");

	entry = dev_priv->head;
	while (entry) {
		next = entry->next;
		kfree(entry);
		entry = next;
	}

	dev_priv->head = dev_priv->tail = NULL;
}

#if 0
/* FIXME: Still needed?
 */
static void mga_freelist_reset(struct drm_device *dev)
{
	struct drm_device_dma *dma = dev->dma;
	struct drm_buf *buf;
	drm_mga_buf_priv_t *buf_priv;
	int i;

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;
		SET_AGE(&buf_priv->list_entry->age, MGA_BUFFER_FREE, 0);
	}
}
#endif

static struct drm_buf *mga_freelist_get(struct drm_device * dev)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *next;
	drm_mga_freelist_t *prev;
	drm_mga_freelist_t *tail = dev_priv->tail;
	u32 head, wrap;
	DRM_DEBUG("\n");

	head = MGA_READ(MGA_PRIMADDRESS);
	wrap = dev_priv->sarea_priv->last_wrap;

	DRM_DEBUG("   tail=0x%06lx %d\n",
		  tail->age.head ?
		  (unsigned long)(tail->age.head - dev_priv->primary->offset) : 0,
		  tail->age.wrap);
	DRM_DEBUG("   head=0x%06lx %d\n",
		  (unsigned long)(head - dev_priv->primary->offset), wrap);

	if (TEST_AGE(&tail->age, head, wrap)) {
		prev = dev_priv->tail->prev;
		next = dev_priv->tail;
		prev->next = NULL;
		next->prev = next->next = NULL;
		dev_priv->tail = prev;
		SET_AGE(&next->age, MGA_BUFFER_USED, 0);
		return next->buf;
	}

	DRM_DEBUG("returning NULL!\n");
	return NULL;
}

int mga_freelist_put(struct drm_device *dev, struct drm_buf *buf)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_freelist_t *head, *entry, *prev;

	DRM_DEBUG("age=0x%06lx wrap=%d\n",
		  (unsigned long)(buf_priv->list_entry->age.head -
				  dev_priv->primary->offset),
		  buf_priv->list_entry->age.wrap);

	entry = buf_priv->list_entry;
	head = dev_priv->head;

	if (buf_priv->list_entry->age.head == MGA_BUFFER_USED) {
		SET_AGE(&entry->age, MGA_BUFFER_FREE, 0);
		prev = dev_priv->tail;
		prev->next = entry;
		entry->prev = prev;
		entry->next = NULL;
	} else {
		prev = head->next;
		head->next = entry;
		prev->prev = entry;
		entry->prev = head;
		entry->next = prev;
	}

	return 0;
}

/* ================================================================
 * DMA initialization, cleanup
 */

int mga_driver_load(struct drm_device *dev, unsigned long flags)
{
	drm_mga_private_t *dev_priv;
	int ret;

	dev_priv = kzalloc(sizeof(drm_mga_private_t), GFP_KERNEL);
	if (!dev_priv)
		return -ENOMEM;

	dev->dev_private = (void *)dev_priv;

	dev_priv->usec_timeout = MGA_DEFAULT_USEC_TIMEOUT;
	dev_priv->chipset = flags;

	dev_priv->mmio_base = pci_resource_start(dev->pdev, 1);
	dev_priv->mmio_size = pci_resource_len(dev->pdev, 1);

	dev->counters += 3;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;

	ret = drm_vblank_init(dev, 1);

	if (ret) {
		(void) mga_driver_unload(dev);
		return ret;
	}

	return 0;
}

#if __OS_HAS_AGP
/**
 * Bootstrap the driver for AGP DMA.
 *
 * \todo
 * Investigate whether there is any benifit to storing the WARP microcode in
 * AGP memory.  If not, the microcode may as well always be put in PCI
 * memory.
 *
 * \todo
 * This routine needs to set dma_bs->agp_mode to the mode actually configured
 * in the hardware.  Looking just at the Linux AGP driver code, I don't see
 * an easy way to determine this.
 *
 * \sa mga_do_dma_bootstrap, mga_do_pci_dma_bootstrap
 */
static int mga_do_agp_dma_bootstrap(struct drm_device *dev,
				    drm_mga_dma_bootstrap_t *dma_bs)
{
	drm_mga_private_t *const dev_priv =
	    (drm_mga_private_t *) dev->dev_private;
	unsigned int warp_size = MGA_WARP_UCODE_SIZE;
	int err;
	unsigned offset;
	const unsigned secondary_size = dma_bs->secondary_bin_count
	    * dma_bs->secondary_bin_size;
	const unsigned agp_size = (dma_bs->agp_size << 20);
	struct drm_buf_desc req;
	struct drm_agp_mode mode;
	struct drm_agp_info info;
	struct drm_agp_buffer agp_req;
	struct drm_agp_binding bind_req;

	/* Acquire AGP. */
	err = drm_agp_acquire(dev);
	if (err) {
		DRM_ERROR("Unable to acquire AGP: %d\n", err);
		return err;
	}

	err = drm_agp_info(dev, &info);
	if (err) {
		DRM_ERROR("Unable to get AGP info: %d\n", err);
		return err;
	}

	mode.mode = (info.mode & ~0x07) | dma_bs->agp_mode;
	err = drm_agp_enable(dev, mode);
	if (err) {
		DRM_ERROR("Unable to enable AGP (mode = 0x%lx)\n", mode.mode);
		return err;
	}

	/* In addition to the usual AGP mode configuration, the G200 AGP cards
	 * need to have the AGP mode "manually" set.
	 */

	if (dev_priv->chipset == MGA_CARD_TYPE_G200) {
		if (mode.mode & 0x02)
			MGA_WRITE(MGA_AGP_PLL, MGA_AGP2XPLL_ENABLE);
		else
			MGA_WRITE(MGA_AGP_PLL, MGA_AGP2XPLL_DISABLE);
	}

	/* Allocate and bind AGP memory. */
	agp_req.size = agp_size;
	agp_req.type = 0;
	err = drm_agp_alloc(dev, &agp_req);
	if (err) {
		dev_priv->agp_size = 0;
		DRM_ERROR("Unable to allocate %uMB AGP memory\n",
			  dma_bs->agp_size);
		return err;
	}

	dev_priv->agp_size = agp_size;
	dev_priv->agp_handle = agp_req.handle;

	bind_req.handle = agp_req.handle;
	bind_req.offset = 0;
	err = drm_agp_bind(dev, &bind_req);
	if (err) {
		DRM_ERROR("Unable to bind AGP memory: %d\n", err);
		return err;
	}

	/* Make drm_addbufs happy by not trying to create a mapping for less
	 * than a page.
	 */
	if (warp_size < PAGE_SIZE)
		warp_size = PAGE_SIZE;

	offset = 0;
	err = drm_addmap(dev, offset, warp_size,
			 _DRM_AGP, _DRM_READ_ONLY, &dev_priv->warp);
	if (err) {
		DRM_ERROR("Unable to map WARP microcode: %d\n", err);
		return err;
	}

	offset += warp_size;
	err = drm_addmap(dev, offset, dma_bs->primary_size,
			 _DRM_AGP, _DRM_READ_ONLY, &dev_priv->primary);
	if (err) {
		DRM_ERROR("Unable to map primary DMA region: %d\n", err);
		return err;
	}

	offset += dma_bs->primary_size;
	err = drm_addmap(dev, offset, secondary_size,
			 _DRM_AGP, 0, &dev->agp_buffer_map);
	if (err) {
		DRM_ERROR("Unable to map secondary DMA region: %d\n", err);
		return err;
	}

	(void)memset(&req, 0, sizeof(req));
	req.count = dma_bs->secondary_bin_count;
	req.size = dma_bs->secondary_bin_size;
	req.flags = _DRM_AGP_BUFFER;
	req.agp_start = offset;

	err = drm_addbufs_agp(dev, &req);
	if (err) {
		DRM_ERROR("Unable to add secondary DMA buffers: %d\n", err);
		return err;
	}

	{
		struct drm_map_list *_entry;
		unsigned long agp_token = 0;

		list_for_each_entry(_entry, &dev->maplist, head) {
			if (_entry->map == dev->agp_buffer_map)
				agp_token = _entry->user_token;
		}
		if (!agp_token)
			return -EFAULT;

		dev->agp_buffer_token = agp_token;
	}

	offset += secondary_size;
	err = drm_addmap(dev, offset, agp_size - offset,
			 _DRM_AGP, 0, &dev_priv->agp_textures);
	if (err) {
		DRM_ERROR("Unable to map AGP texture region %d\n", err);
		return err;
	}

	drm_core_ioremap(dev_priv->warp, dev);
	drm_core_ioremap(dev_priv->primary, dev);
	drm_core_ioremap(dev->agp_buffer_map, dev);

	if (!dev_priv->warp->handle ||
	    !dev_priv->primary->handle || !dev->agp_buffer_map->handle) {
		DRM_ERROR("failed to ioremap agp regions! (%p, %p, %p)\n",
			  dev_priv->warp->handle, dev_priv->primary->handle,
			  dev->agp_buffer_map->handle);
		return -ENOMEM;
	}

	dev_priv->dma_access = MGA_PAGPXFER;
	dev_priv->wagp_enable = MGA_WAGP_ENABLE;

	DRM_INFO("Initialized card for AGP DMA.\n");
	return 0;
}
#else
static int mga_do_agp_dma_bootstrap(struct drm_device *dev,
				    drm_mga_dma_bootstrap_t *dma_bs)
{
	return -EINVAL;
}
#endif

/**
 * Bootstrap the driver for PCI DMA.
 *
 * \todo
 * The algorithm for decreasing the size of the primary DMA buffer could be
 * better.  The size should be rounded up to the nearest page size, then
 * decrease the request size by a single page each pass through the loop.
 *
 * \todo
 * Determine whether the maximum address passed to drm_pci_alloc is correct.
 * The same goes for drm_addbufs_pci.
 *
 * \sa mga_do_dma_bootstrap, mga_do_agp_dma_bootstrap
 */
static int mga_do_pci_dma_bootstrap(struct drm_device *dev,
				    drm_mga_dma_bootstrap_t *dma_bs)
{
	drm_mga_private_t *const dev_priv =
	    (drm_mga_private_t *) dev->dev_private;
	unsigned int warp_size = MGA_WARP_UCODE_SIZE;
	unsigned int primary_size;
	unsigned int bin_count;
	int err;
	struct drm_buf_desc req;

	if (dev->dma == NULL) {
		DRM_ERROR("dev->dma is NULL\n");
		return -EFAULT;
	}

	/* Make drm_addbufs happy by not trying to create a mapping for less
	 * than a page.
	 */
	if (warp_size < PAGE_SIZE)
		warp_size = PAGE_SIZE;

	/* The proper alignment is 0x100 for this mapping */
	err = drm_addmap(dev, 0, warp_size, _DRM_CONSISTENT,
			 _DRM_READ_ONLY, &dev_priv->warp);
	if (err != 0) {
		DRM_ERROR("Unable to create mapping for WARP microcode: %d\n",
			  err);
		return err;
	}

	/* Other than the bottom two bits being used to encode other
	 * information, there don't appear to be any restrictions on the
	 * alignment of the primary or secondary DMA buffers.
	 */

	for (primary_size = dma_bs->primary_size; primary_size != 0;
	     primary_size >>= 1) {
		/* The proper alignment for this mapping is 0x04 */
		err = drm_addmap(dev, 0, primary_size, _DRM_CONSISTENT,
				 _DRM_READ_ONLY, &dev_priv->primary);
		if (!err)
			break;
	}

	if (err != 0) {
		DRM_ERROR("Unable to allocate primary DMA region: %d\n", err);
		return -ENOMEM;
	}

	if (dev_priv->primary->size != dma_bs->primary_size) {
		DRM_INFO("Primary DMA buffer size reduced from %u to %u.\n",
			 dma_bs->primary_size,
			 (unsigned)dev_priv->primary->size);
		dma_bs->primary_size = dev_priv->primary->size;
	}

	for (bin_count = dma_bs->secondary_bin_count; bin_count > 0;
	     bin_count--) {
		(void)memset(&req, 0, sizeof(req));
		req.count = bin_count;
		req.size = dma_bs->secondary_bin_size;

		err = drm_addbufs_pci(dev, &req);
		if (!err)
			break;
	}

	if (bin_count == 0) {
		DRM_ERROR("Unable to add secondary DMA buffers: %d\n", err);
		return err;
	}

	if (bin_count != dma_bs->secondary_bin_count) {
		DRM_INFO("Secondary PCI DMA buffer bin count reduced from %u "
			 "to %u.\n", dma_bs->secondary_bin_count, bin_count);

		dma_bs->secondary_bin_count = bin_count;
	}

	dev_priv->dma_access = 0;
	dev_priv->wagp_enable = 0;

	dma_bs->agp_mode = 0;

	DRM_INFO("Initialized card for PCI DMA.\n");
	return 0;
}

static int mga_do_dma_bootstrap(struct drm_device *dev,
				drm_mga_dma_bootstrap_t *dma_bs)
{
	const int is_agp = (dma_bs->agp_mode != 0) && drm_device_is_agp(dev);
	int err;
	drm_mga_private_t *const dev_priv =
	    (drm_mga_private_t *) dev->dev_private;

	dev_priv->used_new_dma_init = 1;

	/* The first steps are the same for both PCI and AGP based DMA.  Map
	 * the cards MMIO registers and map a status page.
	 */
	err = drm_addmap(dev, dev_priv->mmio_base, dev_priv->mmio_size,
			 _DRM_REGISTERS, _DRM_READ_ONLY, &dev_priv->mmio);
	if (err) {
		DRM_ERROR("Unable to map MMIO region: %d\n", err);
		return err;
	}

	err = drm_addmap(dev, 0, SAREA_MAX, _DRM_SHM,
			 _DRM_READ_ONLY | _DRM_LOCKED | _DRM_KERNEL,
			 &dev_priv->status);
	if (err) {
		DRM_ERROR("Unable to map status region: %d\n", err);
		return err;
	}

	/* The DMA initialization procedure is slightly different for PCI and
	 * AGP cards.  AGP cards just allocate a large block of AGP memory and
	 * carve off portions of it for internal uses.  The remaining memory
	 * is returned to user-mode to be used for AGP textures.
	 */
	if (is_agp)
		err = mga_do_agp_dma_bootstrap(dev, dma_bs);

	/* If we attempted to initialize the card for AGP DMA but failed,
	 * clean-up any mess that may have been created.
	 */

	if (err)
		mga_do_cleanup_dma(dev, MINIMAL_CLEANUP);

	/* Not only do we want to try and initialized PCI cards for PCI DMA,
	 * but we also try to initialized AGP cards that could not be
	 * initialized for AGP DMA.  This covers the case where we have an AGP
	 * card in a system with an unsupported AGP chipset.  In that case the
	 * card will be detected as AGP, but we won't be able to allocate any
	 * AGP memory, etc.
	 */

	if (!is_agp || err)
		err = mga_do_pci_dma_bootstrap(dev, dma_bs);

	return err;
}

int mga_dma_bootstrap(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	drm_mga_dma_bootstrap_t *bootstrap = data;
	int err;
	static const int modes[] = { 0, 1, 2, 2, 4, 4, 4, 4 };
	const drm_mga_private_t *const dev_priv =
		(drm_mga_private_t *) dev->dev_private;

	err = mga_do_dma_bootstrap(dev, bootstrap);
	if (err) {
		mga_do_cleanup_dma(dev, FULL_CLEANUP);
		return err;
	}

	if (dev_priv->agp_textures != NULL) {
		bootstrap->texture_handle = dev_priv->agp_textures->offset;
		bootstrap->texture_size = dev_priv->agp_textures->size;
	} else {
		bootstrap->texture_handle = 0;
		bootstrap->texture_size = 0;
	}

	bootstrap->agp_mode = modes[bootstrap->agp_mode & 0x07];

	return err;
}

static int mga_do_init_dma(struct drm_device *dev, drm_mga_init_t *init)
{
	drm_mga_private_t *dev_priv;
	int ret;
	DRM_DEBUG("\n");

	dev_priv = dev->dev_private;

	if (init->sgram)
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_BLK;
	else
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_RSTR;
	dev_priv->maccess = init->maccess;

	dev_priv->fb_cpp = init->fb_cpp;
	dev_priv->front_offset = init->front_offset;
	dev_priv->front_pitch = init->front_pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->back_pitch = init->back_pitch;

	dev_priv->depth_cpp = init->depth_cpp;
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->depth_pitch = init->depth_pitch;

	/* FIXME: Need to support AGP textures...
	 */
	dev_priv->texture_offset = init->texture_offset[0];
	dev_priv->texture_size = init->texture_size[0];

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("failed to find sarea!\n");
		return -EINVAL;
	}

	if (!dev_priv->used_new_dma_init) {

		dev_priv->dma_access = MGA_PAGPXFER;
		dev_priv->wagp_enable = MGA_WAGP_ENABLE;

		dev_priv->status = drm_core_findmap(dev, init->status_offset);
		if (!dev_priv->status) {
			DRM_ERROR("failed to find status page!\n");
			return -EINVAL;
		}
		dev_priv->mmio = drm_core_findmap(dev, init->mmio_offset);
		if (!dev_priv->mmio) {
			DRM_ERROR("failed to find mmio region!\n");
			return -EINVAL;
		}
		dev_priv->warp = drm_core_findmap(dev, init->warp_offset);
		if (!dev_priv->warp) {
			DRM_ERROR("failed to find warp microcode region!\n");
			return -EINVAL;
		}
		dev_priv->primary = drm_core_findmap(dev, init->primary_offset);
		if (!dev_priv->primary) {
			DRM_ERROR("failed to find primary dma region!\n");
			return -EINVAL;
		}
		dev->agp_buffer_token = init->buffers_offset;
		dev->agp_buffer_map =
		    drm_core_findmap(dev, init->buffers_offset);
		if (!dev->agp_buffer_map) {
			DRM_ERROR("failed to find dma buffer region!\n");
			return -EINVAL;
		}

		drm_core_ioremap(dev_priv->warp, dev);
		drm_core_ioremap(dev_priv->primary, dev);
		drm_core_ioremap(dev->agp_buffer_map, dev);
	}

	dev_priv->sarea_priv =
	    (drm_mga_sarea_t *) ((u8 *) dev_priv->sarea->handle +
				 init->sarea_priv_offset);

	if (!dev_priv->warp->handle ||
	    !dev_priv->primary->handle ||
	    ((dev_priv->dma_access != 0) &&
	     ((dev->agp_buffer_map == NULL) ||
	      (dev->agp_buffer_map->handle == NULL)))) {
		DRM_ERROR("failed to ioremap agp regions!\n");
		return -ENOMEM;
	}

	ret = mga_warp_install_microcode(dev_priv);
	if (ret < 0) {
		DRM_ERROR("failed to install WARP ucode!: %d\n", ret);
		return ret;
	}

	ret = mga_warp_init(dev_priv);
	if (ret < 0) {
		DRM_ERROR("failed to init WARP engine!: %d\n", ret);
		return ret;
	}

	dev_priv->prim.status = (u32 *) dev_priv->status->handle;

	mga_do_wait_for_idle(dev_priv);

	/* Init the primary DMA registers.
	 */
	MGA_WRITE(MGA_PRIMADDRESS, dev_priv->primary->offset | MGA_DMA_GENERAL);
#if 0
	MGA_WRITE(MGA_PRIMPTR, virt_to_bus((void *)dev_priv->prim.status) | MGA_PRIMPTREN0 |	/* Soft trap, SECEND, SETUPEND */
		  MGA_PRIMPTREN1);	/* DWGSYNC */
#endif

	dev_priv->prim.start = (u8 *) dev_priv->primary->handle;
	dev_priv->prim.end = ((u8 *) dev_priv->primary->handle
			      + dev_priv->primary->size);
	dev_priv->prim.size = dev_priv->primary->size;

	dev_priv->prim.tail = 0;
	dev_priv->prim.space = dev_priv->prim.size;
	dev_priv->prim.wrapped = 0;

	dev_priv->prim.last_flush = 0;
	dev_priv->prim.last_wrap = 0;

	dev_priv->prim.high_mark = 256 * DMA_BLOCK_SIZE;

	dev_priv->prim.status[0] = dev_priv->primary->offset;
	dev_priv->prim.status[1] = 0;

	dev_priv->sarea_priv->last_wrap = 0;
	dev_priv->sarea_priv->last_frame.head = 0;
	dev_priv->sarea_priv->last_frame.wrap = 0;

	if (mga_freelist_init(dev, dev_priv) < 0) {
		DRM_ERROR("could not initialize freelist\n");
		return -ENOMEM;
	}

	return 0;
}

static int mga_do_cleanup_dma(struct drm_device *dev, int full_cleanup)
{
	int err = 0;
	DRM_DEBUG("\n");

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	if (dev->dev_private) {
		drm_mga_private_t *dev_priv = dev->dev_private;

		if ((dev_priv->warp != NULL)
		    && (dev_priv->warp->type != _DRM_CONSISTENT))
			drm_core_ioremapfree(dev_priv->warp, dev);

		if ((dev_priv->primary != NULL)
		    && (dev_priv->primary->type != _DRM_CONSISTENT))
			drm_core_ioremapfree(dev_priv->primary, dev);

		if (dev->agp_buffer_map != NULL)
			drm_core_ioremapfree(dev->agp_buffer_map, dev);

		if (dev_priv->used_new_dma_init) {
#if __OS_HAS_AGP
			if (dev_priv->agp_handle != 0) {
				struct drm_agp_binding unbind_req;
				struct drm_agp_buffer free_req;

				unbind_req.handle = dev_priv->agp_handle;
				drm_agp_unbind(dev, &unbind_req);

				free_req.handle = dev_priv->agp_handle;
				drm_agp_free(dev, &free_req);

				dev_priv->agp_textures = NULL;
				dev_priv->agp_size = 0;
				dev_priv->agp_handle = 0;
			}

			if ((dev->agp != NULL) && dev->agp->acquired)
				err = drm_agp_release(dev);
#endif
		}

		dev_priv->warp = NULL;
		dev_priv->primary = NULL;
		dev_priv->sarea = NULL;
		dev_priv->sarea_priv = NULL;
		dev->agp_buffer_map = NULL;

		if (full_cleanup) {
			dev_priv->mmio = NULL;
			dev_priv->status = NULL;
			dev_priv->used_new_dma_init = 0;
		}

		memset(&dev_priv->prim, 0, sizeof(dev_priv->prim));
		dev_priv->warp_pipe = 0;
		memset(dev_priv->warp_pipe_phys, 0,
		       sizeof(dev_priv->warp_pipe_phys));

		if (dev_priv->head != NULL)
			mga_freelist_cleanup(dev);
	}

	return err;
}

int mga_dma_init(struct drm_device *dev, void *data,
		 struct drm_file *file_priv)
{
	drm_mga_init_t *init = data;
	int err;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	switch (init->func) {
	case MGA_INIT_DMA:
		err = mga_do_init_dma(dev, init);
		if (err)
			(void)mga_do_cleanup_dma(dev, FULL_CLEANUP);
		return err;
	case MGA_CLEANUP_DMA:
		return mga_do_cleanup_dma(dev, FULL_CLEANUP);
	}

	return -EINVAL;
}

/* ================================================================
 * Primary DMA stream management
 */

int mga_dma_flush(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *) dev->dev_private;
	struct drm_lock *lock = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("%s%s%s\n",
		  (lock->flags & _DRM_LOCK_FLUSH) ? "flush, " : "",
		  (lock->flags & _DRM_LOCK_FLUSH_ALL) ? "flush all, " : "",
		  (lock->flags & _DRM_LOCK_QUIESCENT) ? "idle, " : "");

	WRAP_WAIT_WITH_RETURN(dev_priv);

	if (lock->flags & (_DRM_LOCK_FLUSH | _DRM_LOCK_FLUSH_ALL))
		mga_do_dma_flush(dev_priv);

	if (lock->flags & _DRM_LOCK_QUIESCENT) {
#if MGA_DMA_DEBUG
		int ret = mga_do_wait_for_idle(dev_priv);
		if (ret < 0)
			DRM_INFO("-EBUSY\n");
		return ret;
#else
		return mga_do_wait_for_idle(dev_priv);
#endif
	} else {
		return 0;
	}
}

int mga_dma_reset(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *) dev->dev_private;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return mga_do_dma_reset(dev_priv);
}

/* ================================================================
 * DMA buffer management
 */

static int mga_dma_get_buffers(struct drm_device *dev,
			       struct drm_file *file_priv, struct drm_dma *d)
{
	struct drm_buf *buf;
	int i;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = mga_freelist_get(dev);
		if (!buf)
			return -EAGAIN;

		buf->file_priv = file_priv;

		if (DRM_COPY_TO_USER(&d->request_indices[i],
				     &buf->idx, sizeof(buf->idx)))
			return -EFAULT;
		if (DRM_COPY_TO_USER(&d->request_sizes[i],
				     &buf->total, sizeof(buf->total)))
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int mga_dma_buffers(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *) dev->dev_private;
	struct drm_dma *d = data;
	int ret = 0;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* Please don't send us buffers.
	 */
	if (d->send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  DRM_CURRENTPID, d->send_count);
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if (d->request_count < 0 || d->request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  DRM_CURRENTPID, d->request_count, dma->buf_count);
		return -EINVAL;
	}

	WRAP_TEST_WITH_RETURN(dev_priv);

	d->granted_count = 0;

	if (d->request_count)
		ret = mga_dma_get_buffers(dev, file_priv, d);

	return ret;
}

/**
 * Called just before the module is unloaded.
 */
int mga_driver_unload(struct drm_device *dev)
{
	kfree(dev->dev_private);
	dev->dev_private = NULL;

	return 0;
}

/**
 * Called when the last opener of the device is closed.
 */
void mga_driver_lastclose(struct drm_device *dev)
{
	mga_do_cleanup_dma(dev, FULL_CLEANUP);
}

int mga_driver_dma_quiescent(struct drm_device *dev)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	return mga_do_wait_for_idle(dev_priv);
}
