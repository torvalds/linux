/* savage_bci.c -- BCI support for Savage
 *
 * Copyright 2004  Felix Kuehling
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL FELIX KUEHLING BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>
#include <drm/savage_drm.h>

#include "savage_drv.h"

/* Need a long timeout for shadow status updates can take a while
 * and so can waiting for events when the queue is full. */
#define SAVAGE_DEFAULT_USEC_TIMEOUT	1000000	/* 1s */
#define SAVAGE_EVENT_USEC_TIMEOUT	5000000	/* 5s */
#define SAVAGE_FREELIST_DEBUG		0

static int savage_do_cleanup_bci(struct drm_device *dev);

static int
savage_bci_wait_fifo_shadow(drm_savage_private_t * dev_priv, unsigned int n)
{
	uint32_t mask = dev_priv->status_used_mask;
	uint32_t threshold = dev_priv->bci_threshold_hi;
	uint32_t status;
	int i;

#if SAVAGE_BCI_DEBUG
	if (n > dev_priv->cob_size + SAVAGE_BCI_FIFO_SIZE - threshold)
		DRM_ERROR("Trying to emit %d words "
			  "(more than guaranteed space in COB)\n", n);
#endif

	for (i = 0; i < SAVAGE_DEFAULT_USEC_TIMEOUT; i++) {
		mb();
		status = dev_priv->status_ptr[0];
		if ((status & mask) < threshold)
			return 0;
		udelay(1);
	}

#if SAVAGE_BCI_DEBUG
	DRM_ERROR("failed!\n");
	DRM_INFO("   status=0x%08x, threshold=0x%08x\n", status, threshold);
#endif
	return -EBUSY;
}

static int
savage_bci_wait_fifo_s3d(drm_savage_private_t * dev_priv, unsigned int n)
{
	uint32_t maxUsed = dev_priv->cob_size + SAVAGE_BCI_FIFO_SIZE - n;
	uint32_t status;
	int i;

	for (i = 0; i < SAVAGE_DEFAULT_USEC_TIMEOUT; i++) {
		status = SAVAGE_READ(SAVAGE_STATUS_WORD0);
		if ((status & SAVAGE_FIFO_USED_MASK_S3D) <= maxUsed)
			return 0;
		udelay(1);
	}

#if SAVAGE_BCI_DEBUG
	DRM_ERROR("failed!\n");
	DRM_INFO("   status=0x%08x\n", status);
#endif
	return -EBUSY;
}

static int
savage_bci_wait_fifo_s4(drm_savage_private_t * dev_priv, unsigned int n)
{
	uint32_t maxUsed = dev_priv->cob_size + SAVAGE_BCI_FIFO_SIZE - n;
	uint32_t status;
	int i;

	for (i = 0; i < SAVAGE_DEFAULT_USEC_TIMEOUT; i++) {
		status = SAVAGE_READ(SAVAGE_ALT_STATUS_WORD0);
		if ((status & SAVAGE_FIFO_USED_MASK_S4) <= maxUsed)
			return 0;
		udelay(1);
	}

#if SAVAGE_BCI_DEBUG
	DRM_ERROR("failed!\n");
	DRM_INFO("   status=0x%08x\n", status);
#endif
	return -EBUSY;
}

/*
 * Waiting for events.
 *
 * The BIOSresets the event tag to 0 on mode changes. Therefore we
 * never emit 0 to the event tag. If we find a 0 event tag we know the
 * BIOS stomped on it and return success assuming that the BIOS waited
 * for engine idle.
 *
 * Note: if the Xserver uses the event tag it has to follow the same
 * rule. Otherwise there may be glitches every 2^16 events.
 */
static int
savage_bci_wait_event_shadow(drm_savage_private_t * dev_priv, uint16_t e)
{
	uint32_t status;
	int i;

	for (i = 0; i < SAVAGE_EVENT_USEC_TIMEOUT; i++) {
		mb();
		status = dev_priv->status_ptr[1];
		if ((((status & 0xffff) - e) & 0xffff) <= 0x7fff ||
		    (status & 0xffff) == 0)
			return 0;
		udelay(1);
	}

#if SAVAGE_BCI_DEBUG
	DRM_ERROR("failed!\n");
	DRM_INFO("   status=0x%08x, e=0x%04x\n", status, e);
#endif

	return -EBUSY;
}

static int
savage_bci_wait_event_reg(drm_savage_private_t * dev_priv, uint16_t e)
{
	uint32_t status;
	int i;

	for (i = 0; i < SAVAGE_EVENT_USEC_TIMEOUT; i++) {
		status = SAVAGE_READ(SAVAGE_STATUS_WORD1);
		if ((((status & 0xffff) - e) & 0xffff) <= 0x7fff ||
		    (status & 0xffff) == 0)
			return 0;
		udelay(1);
	}

#if SAVAGE_BCI_DEBUG
	DRM_ERROR("failed!\n");
	DRM_INFO("   status=0x%08x, e=0x%04x\n", status, e);
#endif

	return -EBUSY;
}

uint16_t savage_bci_emit_event(drm_savage_private_t * dev_priv,
			       unsigned int flags)
{
	uint16_t count;
	BCI_LOCALS;

	if (dev_priv->status_ptr) {
		/* coordinate with Xserver */
		count = dev_priv->status_ptr[1023];
		if (count < dev_priv->event_counter)
			dev_priv->event_wrap++;
	} else {
		count = dev_priv->event_counter;
	}
	count = (count + 1) & 0xffff;
	if (count == 0) {
		count++;	/* See the comment above savage_wait_event_*. */
		dev_priv->event_wrap++;
	}
	dev_priv->event_counter = count;
	if (dev_priv->status_ptr)
		dev_priv->status_ptr[1023] = (uint32_t) count;

	if ((flags & (SAVAGE_WAIT_2D | SAVAGE_WAIT_3D))) {
		unsigned int wait_cmd = BCI_CMD_WAIT;
		if ((flags & SAVAGE_WAIT_2D))
			wait_cmd |= BCI_CMD_WAIT_2D;
		if ((flags & SAVAGE_WAIT_3D))
			wait_cmd |= BCI_CMD_WAIT_3D;
		BEGIN_BCI(2);
		BCI_WRITE(wait_cmd);
	} else {
		BEGIN_BCI(1);
	}
	BCI_WRITE(BCI_CMD_UPDATE_EVENT_TAG | (uint32_t) count);

	return count;
}

/*
 * Freelist management
 */
static int savage_freelist_init(struct drm_device * dev)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	struct drm_device_dma *dma = dev->dma;
	struct drm_buf *buf;
	drm_savage_buf_priv_t *entry;
	int i;
	DRM_DEBUG("count=%d\n", dma->buf_count);

	dev_priv->head.next = &dev_priv->tail;
	dev_priv->head.prev = NULL;
	dev_priv->head.buf = NULL;

	dev_priv->tail.next = NULL;
	dev_priv->tail.prev = &dev_priv->head;
	dev_priv->tail.buf = NULL;

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[i];
		entry = buf->dev_private;

		SET_AGE(&entry->age, 0, 0);
		entry->buf = buf;

		entry->next = dev_priv->head.next;
		entry->prev = &dev_priv->head;
		dev_priv->head.next->prev = entry;
		dev_priv->head.next = entry;
	}

	return 0;
}

static struct drm_buf *savage_freelist_get(struct drm_device * dev)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	drm_savage_buf_priv_t *tail = dev_priv->tail.prev;
	uint16_t event;
	unsigned int wrap;
	DRM_DEBUG("\n");

	UPDATE_EVENT_COUNTER();
	if (dev_priv->status_ptr)
		event = dev_priv->status_ptr[1] & 0xffff;
	else
		event = SAVAGE_READ(SAVAGE_STATUS_WORD1) & 0xffff;
	wrap = dev_priv->event_wrap;
	if (event > dev_priv->event_counter)
		wrap--;		/* hardware hasn't passed the last wrap yet */

	DRM_DEBUG("   tail=0x%04x %d\n", tail->age.event, tail->age.wrap);
	DRM_DEBUG("   head=0x%04x %d\n", event, wrap);

	if (tail->buf && (TEST_AGE(&tail->age, event, wrap) || event == 0)) {
		drm_savage_buf_priv_t *next = tail->next;
		drm_savage_buf_priv_t *prev = tail->prev;
		prev->next = next;
		next->prev = prev;
		tail->next = tail->prev = NULL;
		return tail->buf;
	}

	DRM_DEBUG("returning NULL, tail->buf=%p!\n", tail->buf);
	return NULL;
}

void savage_freelist_put(struct drm_device * dev, struct drm_buf * buf)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	drm_savage_buf_priv_t *entry = buf->dev_private, *prev, *next;

	DRM_DEBUG("age=0x%04x wrap=%d\n", entry->age.event, entry->age.wrap);

	if (entry->next != NULL || entry->prev != NULL) {
		DRM_ERROR("entry already on freelist.\n");
		return;
	}

	prev = &dev_priv->head;
	next = prev->next;
	prev->next = entry;
	next->prev = entry;
	entry->prev = prev;
	entry->next = next;
}

/*
 * Command DMA
 */
static int savage_dma_init(drm_savage_private_t * dev_priv)
{
	unsigned int i;

	dev_priv->nr_dma_pages = dev_priv->cmd_dma->size /
	    (SAVAGE_DMA_PAGE_SIZE * 4);
	dev_priv->dma_pages = kmalloc_array(dev_priv->nr_dma_pages,
					    sizeof(drm_savage_dma_page_t),
					    GFP_KERNEL);
	if (dev_priv->dma_pages == NULL)
		return -ENOMEM;

	for (i = 0; i < dev_priv->nr_dma_pages; ++i) {
		SET_AGE(&dev_priv->dma_pages[i].age, 0, 0);
		dev_priv->dma_pages[i].used = 0;
		dev_priv->dma_pages[i].flushed = 0;
	}
	SET_AGE(&dev_priv->last_dma_age, 0, 0);

	dev_priv->first_dma_page = 0;
	dev_priv->current_dma_page = 0;

	return 0;
}

void savage_dma_reset(drm_savage_private_t * dev_priv)
{
	uint16_t event;
	unsigned int wrap, i;
	event = savage_bci_emit_event(dev_priv, 0);
	wrap = dev_priv->event_wrap;
	for (i = 0; i < dev_priv->nr_dma_pages; ++i) {
		SET_AGE(&dev_priv->dma_pages[i].age, event, wrap);
		dev_priv->dma_pages[i].used = 0;
		dev_priv->dma_pages[i].flushed = 0;
	}
	SET_AGE(&dev_priv->last_dma_age, event, wrap);
	dev_priv->first_dma_page = dev_priv->current_dma_page = 0;
}

void savage_dma_wait(drm_savage_private_t * dev_priv, unsigned int page)
{
	uint16_t event;
	unsigned int wrap;

	/* Faked DMA buffer pages don't age. */
	if (dev_priv->cmd_dma == &dev_priv->fake_dma)
		return;

	UPDATE_EVENT_COUNTER();
	if (dev_priv->status_ptr)
		event = dev_priv->status_ptr[1] & 0xffff;
	else
		event = SAVAGE_READ(SAVAGE_STATUS_WORD1) & 0xffff;
	wrap = dev_priv->event_wrap;
	if (event > dev_priv->event_counter)
		wrap--;		/* hardware hasn't passed the last wrap yet */

	if (dev_priv->dma_pages[page].age.wrap > wrap ||
	    (dev_priv->dma_pages[page].age.wrap == wrap &&
	     dev_priv->dma_pages[page].age.event > event)) {
		if (dev_priv->wait_evnt(dev_priv,
					dev_priv->dma_pages[page].age.event)
		    < 0)
			DRM_ERROR("wait_evnt failed!\n");
	}
}

uint32_t *savage_dma_alloc(drm_savage_private_t * dev_priv, unsigned int n)
{
	unsigned int cur = dev_priv->current_dma_page;
	unsigned int rest = SAVAGE_DMA_PAGE_SIZE -
	    dev_priv->dma_pages[cur].used;
	unsigned int nr_pages = (n - rest + SAVAGE_DMA_PAGE_SIZE - 1) /
	    SAVAGE_DMA_PAGE_SIZE;
	uint32_t *dma_ptr;
	unsigned int i;

	DRM_DEBUG("cur=%u, cur->used=%u, n=%u, rest=%u, nr_pages=%u\n",
		  cur, dev_priv->dma_pages[cur].used, n, rest, nr_pages);

	if (cur + nr_pages < dev_priv->nr_dma_pages) {
		dma_ptr = (uint32_t *) dev_priv->cmd_dma->handle +
		    cur * SAVAGE_DMA_PAGE_SIZE + dev_priv->dma_pages[cur].used;
		if (n < rest)
			rest = n;
		dev_priv->dma_pages[cur].used += rest;
		n -= rest;
		cur++;
	} else {
		dev_priv->dma_flush(dev_priv);
		nr_pages =
		    (n + SAVAGE_DMA_PAGE_SIZE - 1) / SAVAGE_DMA_PAGE_SIZE;
		for (i = cur; i < dev_priv->nr_dma_pages; ++i) {
			dev_priv->dma_pages[i].age = dev_priv->last_dma_age;
			dev_priv->dma_pages[i].used = 0;
			dev_priv->dma_pages[i].flushed = 0;
		}
		dma_ptr = (uint32_t *) dev_priv->cmd_dma->handle;
		dev_priv->first_dma_page = cur = 0;
	}
	for (i = cur; nr_pages > 0; ++i, --nr_pages) {
#if SAVAGE_DMA_DEBUG
		if (dev_priv->dma_pages[i].used) {
			DRM_ERROR("unflushed page %u: used=%u\n",
				  i, dev_priv->dma_pages[i].used);
		}
#endif
		if (n > SAVAGE_DMA_PAGE_SIZE)
			dev_priv->dma_pages[i].used = SAVAGE_DMA_PAGE_SIZE;
		else
			dev_priv->dma_pages[i].used = n;
		n -= SAVAGE_DMA_PAGE_SIZE;
	}
	dev_priv->current_dma_page = --i;

	DRM_DEBUG("cur=%u, cur->used=%u, n=%u\n",
		  i, dev_priv->dma_pages[i].used, n);

	savage_dma_wait(dev_priv, dev_priv->current_dma_page);

	return dma_ptr;
}

static void savage_dma_flush(drm_savage_private_t * dev_priv)
{
	unsigned int first = dev_priv->first_dma_page;
	unsigned int cur = dev_priv->current_dma_page;
	uint16_t event;
	unsigned int wrap, pad, align, len, i;
	unsigned long phys_addr;
	BCI_LOCALS;

	if (first == cur &&
	    dev_priv->dma_pages[cur].used == dev_priv->dma_pages[cur].flushed)
		return;

	/* pad length to multiples of 2 entries
	 * align start of next DMA block to multiles of 8 entries */
	pad = -dev_priv->dma_pages[cur].used & 1;
	align = -(dev_priv->dma_pages[cur].used + pad) & 7;

	DRM_DEBUG("first=%u, cur=%u, first->flushed=%u, cur->used=%u, "
		  "pad=%u, align=%u\n",
		  first, cur, dev_priv->dma_pages[first].flushed,
		  dev_priv->dma_pages[cur].used, pad, align);

	/* pad with noops */
	if (pad) {
		uint32_t *dma_ptr = (uint32_t *) dev_priv->cmd_dma->handle +
		    cur * SAVAGE_DMA_PAGE_SIZE + dev_priv->dma_pages[cur].used;
		dev_priv->dma_pages[cur].used += pad;
		while (pad != 0) {
			*dma_ptr++ = BCI_CMD_WAIT;
			pad--;
		}
	}

	mb();

	/* do flush ... */
	phys_addr = dev_priv->cmd_dma->offset +
	    (first * SAVAGE_DMA_PAGE_SIZE +
	     dev_priv->dma_pages[first].flushed) * 4;
	len = (cur - first) * SAVAGE_DMA_PAGE_SIZE +
	    dev_priv->dma_pages[cur].used - dev_priv->dma_pages[first].flushed;

	DRM_DEBUG("phys_addr=%lx, len=%u\n",
		  phys_addr | dev_priv->dma_type, len);

	BEGIN_BCI(3);
	BCI_SET_REGISTERS(SAVAGE_DMABUFADDR, 1);
	BCI_WRITE(phys_addr | dev_priv->dma_type);
	BCI_DMA(len);

	/* fix alignment of the start of the next block */
	dev_priv->dma_pages[cur].used += align;

	/* age DMA pages */
	event = savage_bci_emit_event(dev_priv, 0);
	wrap = dev_priv->event_wrap;
	for (i = first; i < cur; ++i) {
		SET_AGE(&dev_priv->dma_pages[i].age, event, wrap);
		dev_priv->dma_pages[i].used = 0;
		dev_priv->dma_pages[i].flushed = 0;
	}
	/* age the current page only when it's full */
	if (dev_priv->dma_pages[cur].used == SAVAGE_DMA_PAGE_SIZE) {
		SET_AGE(&dev_priv->dma_pages[cur].age, event, wrap);
		dev_priv->dma_pages[cur].used = 0;
		dev_priv->dma_pages[cur].flushed = 0;
		/* advance to next page */
		cur++;
		if (cur == dev_priv->nr_dma_pages)
			cur = 0;
		dev_priv->first_dma_page = dev_priv->current_dma_page = cur;
	} else {
		dev_priv->first_dma_page = cur;
		dev_priv->dma_pages[cur].flushed = dev_priv->dma_pages[i].used;
	}
	SET_AGE(&dev_priv->last_dma_age, event, wrap);

	DRM_DEBUG("first=cur=%u, cur->used=%u, cur->flushed=%u\n", cur,
		  dev_priv->dma_pages[cur].used,
		  dev_priv->dma_pages[cur].flushed);
}

static void savage_fake_dma_flush(drm_savage_private_t * dev_priv)
{
	unsigned int i, j;
	BCI_LOCALS;

	if (dev_priv->first_dma_page == dev_priv->current_dma_page &&
	    dev_priv->dma_pages[dev_priv->current_dma_page].used == 0)
		return;

	DRM_DEBUG("first=%u, cur=%u, cur->used=%u\n",
		  dev_priv->first_dma_page, dev_priv->current_dma_page,
		  dev_priv->dma_pages[dev_priv->current_dma_page].used);

	for (i = dev_priv->first_dma_page;
	     i <= dev_priv->current_dma_page && dev_priv->dma_pages[i].used;
	     ++i) {
		uint32_t *dma_ptr = (uint32_t *) dev_priv->cmd_dma->handle +
		    i * SAVAGE_DMA_PAGE_SIZE;
#if SAVAGE_DMA_DEBUG
		/* Sanity check: all pages except the last one must be full. */
		if (i < dev_priv->current_dma_page &&
		    dev_priv->dma_pages[i].used != SAVAGE_DMA_PAGE_SIZE) {
			DRM_ERROR("partial DMA page %u: used=%u",
				  i, dev_priv->dma_pages[i].used);
		}
#endif
		BEGIN_BCI(dev_priv->dma_pages[i].used);
		for (j = 0; j < dev_priv->dma_pages[i].used; ++j) {
			BCI_WRITE(dma_ptr[j]);
		}
		dev_priv->dma_pages[i].used = 0;
	}

	/* reset to first page */
	dev_priv->first_dma_page = dev_priv->current_dma_page = 0;
}

int savage_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	drm_savage_private_t *dev_priv;

	dev_priv = kzalloc(sizeof(drm_savage_private_t), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = (void *)dev_priv;

	dev_priv->chipset = (enum savage_family)chipset;

	pci_set_master(pdev);

	return 0;
}


/*
 * Initialize mappings. On Savage4 and SavageIX the alignment
 * and size of the aperture is not suitable for automatic MTRR setup
 * in drm_legacy_addmap. Therefore we add them manually before the maps are
 * initialized, and tear them down on last close.
 */
int savage_driver_firstopen(struct drm_device *dev)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned long mmio_base, fb_base, fb_size, aperture_base;
	int ret = 0;

	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		fb_base = pci_resource_start(pdev, 0);
		fb_size = SAVAGE_FB_SIZE_S3;
		mmio_base = fb_base + SAVAGE_FB_SIZE_S3;
		aperture_base = fb_base + SAVAGE_APERTURE_OFFSET;
		/* this should always be true */
		if (pci_resource_len(pdev, 0) == 0x08000000) {
			/* Don't make MMIO write-cobining! We need 3
			 * MTRRs. */
			dev_priv->mtrr_handles[0] =
				arch_phys_wc_add(fb_base, 0x01000000);
			dev_priv->mtrr_handles[1] =
				arch_phys_wc_add(fb_base + 0x02000000,
						 0x02000000);
			dev_priv->mtrr_handles[2] =
				arch_phys_wc_add(fb_base + 0x04000000,
						0x04000000);
		} else {
			DRM_ERROR("strange pci_resource_len %08llx\n",
				  (unsigned long long)
				  pci_resource_len(pdev, 0));
		}
	} else if (dev_priv->chipset != S3_SUPERSAVAGE &&
		   dev_priv->chipset != S3_SAVAGE2000) {
		mmio_base = pci_resource_start(pdev, 0);
		fb_base = pci_resource_start(pdev, 1);
		fb_size = SAVAGE_FB_SIZE_S4;
		aperture_base = fb_base + SAVAGE_APERTURE_OFFSET;
		/* this should always be true */
		if (pci_resource_len(pdev, 1) == 0x08000000) {
			/* Can use one MTRR to cover both fb and
			 * aperture. */
			dev_priv->mtrr_handles[0] =
				arch_phys_wc_add(fb_base,
						 0x08000000);
		} else {
			DRM_ERROR("strange pci_resource_len %08llx\n",
				  (unsigned long long)
				  pci_resource_len(pdev, 1));
		}
	} else {
		mmio_base = pci_resource_start(pdev, 0);
		fb_base = pci_resource_start(pdev, 1);
		fb_size = pci_resource_len(pdev, 1);
		aperture_base = pci_resource_start(pdev, 2);
		/* Automatic MTRR setup will do the right thing. */
	}

	ret = drm_legacy_addmap(dev, mmio_base, SAVAGE_MMIO_SIZE,
				_DRM_REGISTERS, _DRM_READ_ONLY,
				&dev_priv->mmio);
	if (ret)
		return ret;

	ret = drm_legacy_addmap(dev, fb_base, fb_size, _DRM_FRAME_BUFFER,
				_DRM_WRITE_COMBINING, &dev_priv->fb);
	if (ret)
		return ret;

	ret = drm_legacy_addmap(dev, aperture_base, SAVAGE_APERTURE_SIZE,
				_DRM_FRAME_BUFFER, _DRM_WRITE_COMBINING,
				&dev_priv->aperture);
	return ret;
}

/*
 * Delete MTRRs and free device-private data.
 */
void savage_driver_lastclose(struct drm_device *dev)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	int i;

	for (i = 0; i < 3; ++i) {
		arch_phys_wc_del(dev_priv->mtrr_handles[i]);
		dev_priv->mtrr_handles[i] = 0;
	}
}

void savage_driver_unload(struct drm_device *dev)
{
	drm_savage_private_t *dev_priv = dev->dev_private;

	kfree(dev_priv);
}

static int savage_do_init_bci(struct drm_device * dev, drm_savage_init_t * init)
{
	drm_savage_private_t *dev_priv = dev->dev_private;

	if (init->fb_bpp != 16 && init->fb_bpp != 32) {
		DRM_ERROR("invalid frame buffer bpp %d!\n", init->fb_bpp);
		return -EINVAL;
	}
	if (init->depth_bpp != 16 && init->depth_bpp != 32) {
		DRM_ERROR("invalid depth buffer bpp %d!\n", init->fb_bpp);
		return -EINVAL;
	}
	if (init->dma_type != SAVAGE_DMA_AGP &&
	    init->dma_type != SAVAGE_DMA_PCI) {
		DRM_ERROR("invalid dma memory type %d!\n", init->dma_type);
		return -EINVAL;
	}

	dev_priv->cob_size = init->cob_size;
	dev_priv->bci_threshold_lo = init->bci_threshold_lo;
	dev_priv->bci_threshold_hi = init->bci_threshold_hi;
	dev_priv->dma_type = init->dma_type;

	dev_priv->fb_bpp = init->fb_bpp;
	dev_priv->front_offset = init->front_offset;
	dev_priv->front_pitch = init->front_pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->back_pitch = init->back_pitch;
	dev_priv->depth_bpp = init->depth_bpp;
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->depth_pitch = init->depth_pitch;

	dev_priv->texture_offset = init->texture_offset;
	dev_priv->texture_size = init->texture_size;

	dev_priv->sarea = drm_legacy_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		savage_do_cleanup_bci(dev);
		return -EINVAL;
	}
	if (init->status_offset != 0) {
		dev_priv->status = drm_legacy_findmap(dev, init->status_offset);
		if (!dev_priv->status) {
			DRM_ERROR("could not find shadow status region!\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
	} else {
		dev_priv->status = NULL;
	}
	if (dev_priv->dma_type == SAVAGE_DMA_AGP && init->buffers_offset) {
		dev->agp_buffer_token = init->buffers_offset;
		dev->agp_buffer_map = drm_legacy_findmap(dev,
						       init->buffers_offset);
		if (!dev->agp_buffer_map) {
			DRM_ERROR("could not find DMA buffer region!\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
		drm_legacy_ioremap(dev->agp_buffer_map, dev);
		if (!dev->agp_buffer_map->handle) {
			DRM_ERROR("failed to ioremap DMA buffer region!\n");
			savage_do_cleanup_bci(dev);
			return -ENOMEM;
		}
	}
	if (init->agp_textures_offset) {
		dev_priv->agp_textures =
		    drm_legacy_findmap(dev, init->agp_textures_offset);
		if (!dev_priv->agp_textures) {
			DRM_ERROR("could not find agp texture region!\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
	} else {
		dev_priv->agp_textures = NULL;
	}

	if (init->cmd_dma_offset) {
		if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
			DRM_ERROR("command DMA not supported on "
				  "Savage3D/MX/IX.\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
		if (dev->dma && dev->dma->buflist) {
			DRM_ERROR("command and vertex DMA not supported "
				  "at the same time.\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
		dev_priv->cmd_dma = drm_legacy_findmap(dev, init->cmd_dma_offset);
		if (!dev_priv->cmd_dma) {
			DRM_ERROR("could not find command DMA region!\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
		if (dev_priv->dma_type == SAVAGE_DMA_AGP) {
			if (dev_priv->cmd_dma->type != _DRM_AGP) {
				DRM_ERROR("AGP command DMA region is not a "
					  "_DRM_AGP map!\n");
				savage_do_cleanup_bci(dev);
				return -EINVAL;
			}
			drm_legacy_ioremap(dev_priv->cmd_dma, dev);
			if (!dev_priv->cmd_dma->handle) {
				DRM_ERROR("failed to ioremap command "
					  "DMA region!\n");
				savage_do_cleanup_bci(dev);
				return -ENOMEM;
			}
		} else if (dev_priv->cmd_dma->type != _DRM_CONSISTENT) {
			DRM_ERROR("PCI command DMA region is not a "
				  "_DRM_CONSISTENT map!\n");
			savage_do_cleanup_bci(dev);
			return -EINVAL;
		}
	} else {
		dev_priv->cmd_dma = NULL;
	}

	dev_priv->dma_flush = savage_dma_flush;
	if (!dev_priv->cmd_dma) {
		DRM_DEBUG("falling back to faked command DMA.\n");
		dev_priv->fake_dma.offset = 0;
		dev_priv->fake_dma.size = SAVAGE_FAKE_DMA_SIZE;
		dev_priv->fake_dma.type = _DRM_SHM;
		dev_priv->fake_dma.handle = kmalloc(SAVAGE_FAKE_DMA_SIZE,
						    GFP_KERNEL);
		if (!dev_priv->fake_dma.handle) {
			DRM_ERROR("could not allocate faked DMA buffer!\n");
			savage_do_cleanup_bci(dev);
			return -ENOMEM;
		}
		dev_priv->cmd_dma = &dev_priv->fake_dma;
		dev_priv->dma_flush = savage_fake_dma_flush;
	}

	dev_priv->sarea_priv =
	    (drm_savage_sarea_t *) ((uint8_t *) dev_priv->sarea->handle +
				    init->sarea_priv_offset);

	/* setup bitmap descriptors */
	{
		unsigned int color_tile_format;
		unsigned int depth_tile_format;
		unsigned int front_stride, back_stride, depth_stride;
		if (dev_priv->chipset <= S3_SAVAGE4) {
			color_tile_format = dev_priv->fb_bpp == 16 ?
			    SAVAGE_BD_TILE_16BPP : SAVAGE_BD_TILE_32BPP;
			depth_tile_format = dev_priv->depth_bpp == 16 ?
			    SAVAGE_BD_TILE_16BPP : SAVAGE_BD_TILE_32BPP;
		} else {
			color_tile_format = SAVAGE_BD_TILE_DEST;
			depth_tile_format = SAVAGE_BD_TILE_DEST;
		}
		front_stride = dev_priv->front_pitch / (dev_priv->fb_bpp / 8);
		back_stride = dev_priv->back_pitch / (dev_priv->fb_bpp / 8);
		depth_stride =
		    dev_priv->depth_pitch / (dev_priv->depth_bpp / 8);

		dev_priv->front_bd = front_stride | SAVAGE_BD_BW_DISABLE |
		    (dev_priv->fb_bpp << SAVAGE_BD_BPP_SHIFT) |
		    (color_tile_format << SAVAGE_BD_TILE_SHIFT);

		dev_priv->back_bd = back_stride | SAVAGE_BD_BW_DISABLE |
		    (dev_priv->fb_bpp << SAVAGE_BD_BPP_SHIFT) |
		    (color_tile_format << SAVAGE_BD_TILE_SHIFT);

		dev_priv->depth_bd = depth_stride | SAVAGE_BD_BW_DISABLE |
		    (dev_priv->depth_bpp << SAVAGE_BD_BPP_SHIFT) |
		    (depth_tile_format << SAVAGE_BD_TILE_SHIFT);
	}

	/* setup status and bci ptr */
	dev_priv->event_counter = 0;
	dev_priv->event_wrap = 0;
	dev_priv->bci_ptr = (volatile uint32_t *)
	    ((uint8_t *) dev_priv->mmio->handle + SAVAGE_BCI_OFFSET);
	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		dev_priv->status_used_mask = SAVAGE_FIFO_USED_MASK_S3D;
	} else {
		dev_priv->status_used_mask = SAVAGE_FIFO_USED_MASK_S4;
	}
	if (dev_priv->status != NULL) {
		dev_priv->status_ptr =
		    (volatile uint32_t *)dev_priv->status->handle;
		dev_priv->wait_fifo = savage_bci_wait_fifo_shadow;
		dev_priv->wait_evnt = savage_bci_wait_event_shadow;
		dev_priv->status_ptr[1023] = dev_priv->event_counter;
	} else {
		dev_priv->status_ptr = NULL;
		if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
			dev_priv->wait_fifo = savage_bci_wait_fifo_s3d;
		} else {
			dev_priv->wait_fifo = savage_bci_wait_fifo_s4;
		}
		dev_priv->wait_evnt = savage_bci_wait_event_reg;
	}

	/* cliprect functions */
	if (S3_SAVAGE3D_SERIES(dev_priv->chipset))
		dev_priv->emit_clip_rect = savage_emit_clip_rect_s3d;
	else
		dev_priv->emit_clip_rect = savage_emit_clip_rect_s4;

	if (savage_freelist_init(dev) < 0) {
		DRM_ERROR("could not initialize freelist\n");
		savage_do_cleanup_bci(dev);
		return -ENOMEM;
	}

	if (savage_dma_init(dev_priv) < 0) {
		DRM_ERROR("could not initialize command DMA\n");
		savage_do_cleanup_bci(dev);
		return -ENOMEM;
	}

	return 0;
}

static int savage_do_cleanup_bci(struct drm_device * dev)
{
	drm_savage_private_t *dev_priv = dev->dev_private;

	if (dev_priv->cmd_dma == &dev_priv->fake_dma) {
		kfree(dev_priv->fake_dma.handle);
	} else if (dev_priv->cmd_dma && dev_priv->cmd_dma->handle &&
		   dev_priv->cmd_dma->type == _DRM_AGP &&
		   dev_priv->dma_type == SAVAGE_DMA_AGP)
		drm_legacy_ioremapfree(dev_priv->cmd_dma, dev);

	if (dev_priv->dma_type == SAVAGE_DMA_AGP &&
	    dev->agp_buffer_map && dev->agp_buffer_map->handle) {
		drm_legacy_ioremapfree(dev->agp_buffer_map, dev);
		/* make sure the next instance (which may be running
		 * in PCI mode) doesn't try to use an old
		 * agp_buffer_map. */
		dev->agp_buffer_map = NULL;
	}

	kfree(dev_priv->dma_pages);

	return 0;
}

static int savage_bci_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_savage_init_t *init = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	switch (init->func) {
	case SAVAGE_INIT_BCI:
		return savage_do_init_bci(dev, init);
	case SAVAGE_CLEANUP_BCI:
		return savage_do_cleanup_bci(dev);
	}

	return -EINVAL;
}

static int savage_bci_event_emit(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	drm_savage_event_emit_t *event = data;

	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	event->count = savage_bci_emit_event(dev_priv, event->flags);
	event->count |= dev_priv->event_wrap << 16;

	return 0;
}

static int savage_bci_event_wait(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_savage_private_t *dev_priv = dev->dev_private;
	drm_savage_event_wait_t *event = data;
	unsigned int event_e, hw_e;
	unsigned int event_w, hw_w;

	DRM_DEBUG("\n");

	UPDATE_EVENT_COUNTER();
	if (dev_priv->status_ptr)
		hw_e = dev_priv->status_ptr[1] & 0xffff;
	else
		hw_e = SAVAGE_READ(SAVAGE_STATUS_WORD1) & 0xffff;
	hw_w = dev_priv->event_wrap;
	if (hw_e > dev_priv->event_counter)
		hw_w--;		/* hardware hasn't passed the last wrap yet */

	event_e = event->count & 0xffff;
	event_w = event->count >> 16;

	/* Don't need to wait if
	 * - event counter wrapped since the event was emitted or
	 * - the hardware has advanced up to or over the event to wait for.
	 */
	if (event_w < hw_w || (event_w == hw_w && event_e <= hw_e))
		return 0;
	else
		return dev_priv->wait_evnt(dev_priv, event_e);
}

/*
 * DMA buffer management
 */

static int savage_bci_get_buffers(struct drm_device *dev,
				  struct drm_file *file_priv,
				  struct drm_dma *d)
{
	struct drm_buf *buf;
	int i;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = savage_freelist_get(dev);
		if (!buf)
			return -EAGAIN;

		buf->file_priv = file_priv;

		if (copy_to_user(&d->request_indices[i],
				     &buf->idx, sizeof(buf->idx)))
			return -EFAULT;
		if (copy_to_user(&d->request_sizes[i],
				     &buf->total, sizeof(buf->total)))
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int savage_bci_buffers(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	struct drm_dma *d = data;
	int ret = 0;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* Please don't send us buffers.
	 */
	if (d->send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  task_pid_nr(current), d->send_count);
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if (d->request_count < 0 || d->request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  task_pid_nr(current), d->request_count, dma->buf_count);
		return -EINVAL;
	}

	d->granted_count = 0;

	if (d->request_count) {
		ret = savage_bci_get_buffers(dev, file_priv, d);
	}

	return ret;
}

void savage_reclaim_buffers(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	drm_savage_private_t *dev_priv = dev->dev_private;
	int release_idlelock = 0;
	int i;

	if (!dma)
		return;
	if (!dev_priv)
		return;
	if (!dma->buflist)
		return;

	if (file_priv->master && file_priv->master->lock.hw_lock) {
		drm_legacy_idlelock_take(&file_priv->master->lock);
		release_idlelock = 1;
	}

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_savage_buf_priv_t *buf_priv = buf->dev_private;

		if (buf->file_priv == file_priv && buf_priv &&
		    buf_priv->next == NULL && buf_priv->prev == NULL) {
			uint16_t event;
			DRM_DEBUG("reclaimed from client\n");
			event = savage_bci_emit_event(dev_priv, SAVAGE_WAIT_3D);
			SET_AGE(&buf_priv->age, event, dev_priv->event_wrap);
			savage_freelist_put(dev, buf);
		}
	}

	if (release_idlelock)
		drm_legacy_idlelock_release(&file_priv->master->lock);
}

const struct drm_ioctl_desc savage_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SAVAGE_BCI_INIT, savage_bci_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(SAVAGE_BCI_CMDBUF, savage_bci_cmdbuf, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(SAVAGE_BCI_EVENT_EMIT, savage_bci_event_emit, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(SAVAGE_BCI_EVENT_WAIT, savage_bci_event_wait, DRM_AUTH),
};

int savage_max_ioctl = ARRAY_SIZE(savage_ioctls);
