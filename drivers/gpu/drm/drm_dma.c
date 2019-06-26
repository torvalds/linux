/**
 * \file drm_dma.c
 * DMA IOCTL and function support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Mar 19 14:30:16 1999 by faith@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>

#include <drm/drm_drv.h>
#include <drm/drm_pci.h>
#include <drm/drm_print.h>

#include "drm_legacy.h"

/**
 * Initialize the DMA data.
 *
 * \param dev DRM device.
 * \return zero on success or a negative value on failure.
 *
 * Allocate and initialize a drm_device_dma structure.
 */
int drm_legacy_dma_setup(struct drm_device *dev)
{
	int i;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_DMA) ||
	    !drm_core_check_feature(dev, DRIVER_LEGACY))
		return 0;

	dev->buf_use = 0;
	atomic_set(&dev->buf_alloc, 0);

	dev->dma = kzalloc(sizeof(*dev->dma), GFP_KERNEL);
	if (!dev->dma)
		return -ENOMEM;

	for (i = 0; i <= DRM_MAX_ORDER; i++)
		memset(&dev->dma->bufs[i], 0, sizeof(dev->dma->bufs[0]));

	return 0;
}

/**
 * Cleanup the DMA resources.
 *
 * \param dev DRM device.
 *
 * Free all pages associated with DMA buffers, the buffers and pages lists, and
 * finally the drm_device::dma structure itself.
 */
void drm_legacy_dma_takedown(struct drm_device *dev)
{
	struct drm_device_dma *dma = dev->dma;
	int i, j;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_DMA) ||
	    !drm_core_check_feature(dev, DRIVER_LEGACY))
		return;

	if (!dma)
		return;

	/* Clear dma buffers */
	for (i = 0; i <= DRM_MAX_ORDER; i++) {
		if (dma->bufs[i].seg_count) {
			DRM_DEBUG("order %d: buf_count = %d,"
				  " seg_count = %d\n",
				  i,
				  dma->bufs[i].buf_count,
				  dma->bufs[i].seg_count);
			for (j = 0; j < dma->bufs[i].seg_count; j++) {
				if (dma->bufs[i].seglist[j]) {
					drm_pci_free(dev, dma->bufs[i].seglist[j]);
				}
			}
			kfree(dma->bufs[i].seglist);
		}
		if (dma->bufs[i].buf_count) {
			for (j = 0; j < dma->bufs[i].buf_count; j++) {
				kfree(dma->bufs[i].buflist[j].dev_private);
			}
			kfree(dma->bufs[i].buflist);
		}
	}

	kfree(dma->buflist);
	kfree(dma->pagelist);
	kfree(dev->dma);
	dev->dma = NULL;
}

/**
 * Free a buffer.
 *
 * \param dev DRM device.
 * \param buf buffer to free.
 *
 * Resets the fields of \p buf.
 */
void drm_legacy_free_buffer(struct drm_device *dev, struct drm_buf * buf)
{
	if (!buf)
		return;

	buf->waiting = 0;
	buf->pending = 0;
	buf->file_priv = NULL;
	buf->used = 0;
}

/**
 * Reclaim the buffers.
 *
 * \param file_priv DRM file private.
 *
 * Frees each buffer associated with \p file_priv not already on the hardware.
 */
void drm_legacy_reclaim_buffers(struct drm_device *dev,
				struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int i;

	if (!dma)
		return;
	for (i = 0; i < dma->buf_count; i++) {
		if (dma->buflist[i]->file_priv == file_priv) {
			switch (dma->buflist[i]->list) {
			case DRM_LIST_NONE:
				drm_legacy_free_buffer(dev, dma->buflist[i]);
				break;
			case DRM_LIST_WAIT:
				dma->buflist[i]->list = DRM_LIST_RECLAIM;
				break;
			default:
				/* Buffer already on hardware. */
				break;
			}
		}
	}
}
