/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

/* Really want an OS-independent resettable timer.  Would like to have
 * this loop run for (eg) 3 sec, but have the timer reset every time
 * the head pointer changes, so that EBUSY only happens if the ring
 * actually stalls for (eg) 3 seconds.
 */
int i915_wait_ring(struct drm_device * dev, int n, const char *caller)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);
	u32 acthd_reg = IS_I965G(dev) ? ACTHD_I965 : ACTHD;
	u32 last_acthd = I915_READ(acthd_reg);
	u32 acthd;
	u32 last_head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
	int i;

	for (i = 0; i < 100000; i++) {
		ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
		acthd = I915_READ(acthd_reg);
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
		if (ring->space >= n)
			return 0;

		if (dev->primary->master) {
			struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
			if (master_priv->sarea_priv)
				master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;
		}


		if (ring->head != last_head)
			i = 0;
		if (acthd != last_acthd)
			i = 0;

		last_head = ring->head;
		last_acthd = acthd;
		msleep_interruptible(10);

	}

	return -EBUSY;
}

/**
 * Sets up the hardware status page for devices that need a physical address
 * in the register.
 */
static int i915_init_phys_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	/* Program Hardware Status Page */
	dev_priv->status_page_dmah =
		drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE, 0xffffffff);

	if (!dev_priv->status_page_dmah) {
		DRM_ERROR("Can not allocate hardware status page\n");
		return -ENOMEM;
	}
	dev_priv->hw_status_page = dev_priv->status_page_dmah->vaddr;
	dev_priv->dma_status_page = dev_priv->status_page_dmah->busaddr;

	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);

	I915_WRITE(HWS_PGA, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");
	return 0;
}

/**
 * Frees the hardware status page, whether it's a physical address or a virtual
 * address set up by the X Server.
 */
static void i915_free_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	if (dev_priv->status_page_dmah) {
		drm_pci_free(dev, dev_priv->status_page_dmah);
		dev_priv->status_page_dmah = NULL;
	}

	if (dev_priv->status_gfx_addr) {
		dev_priv->status_gfx_addr = 0;
		drm_core_ioremapfree(&dev_priv->hws_map, dev);
	}

	/* Need to rewrite hardware status page */
	I915_WRITE(HWS_PGA, 0x1ffff000);
}

void i915_kernel_lost_context(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);

	/*
	 * We should never lose context on the ring with modesetting
	 * as we don't expose it to userspace
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
	ring->tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;

	if (!dev->primary->master)
		return;

	master_priv = dev->primary->master->driver_priv;
	if (ring->head == ring->tail && master_priv->sarea_priv)
		master_priv->sarea_priv->perf_boxes |= I915_BOX_RING_EMPTY;
}

static int i915_dma_cleanup(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	if (dev_priv->ring.virtual_start) {
		drm_core_ioremapfree(&dev_priv->ring.map, dev);
		dev_priv->ring.virtual_start = NULL;
		dev_priv->ring.map.handle = NULL;
		dev_priv->ring.map.size = 0;
	}

	/* Clear the HWS virtual address at teardown */
	if (I915_NEED_GFX_HWS(dev))
		i915_free_hws(dev);

	return 0;
}

static int i915_initialize(struct drm_device * dev, drm_i915_init_t * init)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;

	master_priv->sarea = drm_getsarea(dev);
	if (master_priv->sarea) {
		master_priv->sarea_priv = (drm_i915_sarea_t *)
			((u8 *)master_priv->sarea->handle + init->sarea_priv_offset);
	} else {
		DRM_DEBUG("sarea not found assuming DRI2 userspace\n");
	}

	if (init->ring_size != 0) {
		if (dev_priv->ring.ring_obj != NULL) {
			i915_dma_cleanup(dev);
			DRM_ERROR("Client tried to initialize ringbuffer in "
				  "GEM mode\n");
			return -EINVAL;
		}

		dev_priv->ring.Size = init->ring_size;
		dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;

		dev_priv->ring.map.offset = init->ring_start;
		dev_priv->ring.map.size = init->ring_size;
		dev_priv->ring.map.type = 0;
		dev_priv->ring.map.flags = 0;
		dev_priv->ring.map.mtrr = 0;

		drm_core_ioremap_wc(&dev_priv->ring.map, dev);

		if (dev_priv->ring.map.handle == NULL) {
			i915_dma_cleanup(dev);
			DRM_ERROR("can not ioremap virtual address for"
				  " ring buffer\n");
			return -ENOMEM;
		}
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->cpp = init->cpp;
	dev_priv->back_offset = init->back_offset;
	dev_priv->front_offset = init->front_offset;
	dev_priv->current_page = 0;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->pf_current_page = 0;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->allow_batchbuffer = 1;

	return 0;
}

static int i915_dma_resume(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	DRM_DEBUG("%s\n", __func__);

	if (dev_priv->ring.map.handle == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	/* Program Hardware Status Page */
	if (!dev_priv->hw_status_page) {
		DRM_ERROR("Can not find hardware status page\n");
		return -EINVAL;
	}
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	if (dev_priv->status_gfx_addr != 0)
		I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
	else
		I915_WRITE(HWS_PGA, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	return 0;
}

static int i915_dma_init(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case I915_INIT_DMA:
		retcode = i915_initialize(dev, init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

/* Implement basically the same security restrictions as hardware does
 * for MI_BATCH_NON_SECURE.  These can be made stricter at any time.
 *
 * Most of the calculations below involve calculating the size of a
 * particular instruction.  It's important to get the size right as
 * that tells us where the next instruction to check is.  Any illegal
 * instruction detected will be given a size of zero, which is a
 * signal to abort the rest of the buffer.
 */
static int do_validate_cmd(int cmd)
{
	switch (((cmd >> 29) & 0x7)) {
	case 0x0:
		switch ((cmd >> 23) & 0x3f) {
		case 0x0:
			return 1;	/* MI_NOOP */
		case 0x4:
			return 1;	/* MI_FLUSH */
		default:
			return 0;	/* disallow everything else */
		}
		break;
	case 0x1:
		return 0;	/* reserved */
	case 0x2:
		return (cmd & 0xff) + 2;	/* 2d commands */
	case 0x3:
		if (((cmd >> 24) & 0x1f) <= 0x18)
			return 1;

		switch ((cmd >> 24) & 0x1f) {
		case 0x1c:
			return 1;
		case 0x1d:
			switch ((cmd >> 16) & 0xff) {
			case 0x3:
				return (cmd & 0x1f) + 2;
			case 0x4:
				return (cmd & 0xf) + 2;
			default:
				return (cmd & 0xffff) + 2;
			}
		case 0x1e:
			if (cmd & (1 << 23))
				return (cmd & 0xffff) + 1;
			else
				return 1;
		case 0x1f:
			if ((cmd & (1 << 23)) == 0)	/* inline vertices */
				return (cmd & 0x1ffff) + 2;
			else if (cmd & (1 << 17))	/* indirect random */
				if ((cmd & 0xffff) == 0)
					return 0;	/* unknown length, too hard */
				else
					return (((cmd & 0xffff) + 1) / 2) + 1;
			else
				return 2;	/* indirect sequential */
		default:
			return 0;
		}
	default:
		return 0;
	}

	return 0;
}

static int validate_cmd(int cmd)
{
	int ret = do_validate_cmd(cmd);

/*	printk("validate_cmd( %x ): %d\n", cmd, ret); */

	return ret;
}

static int i915_emit_cmds(struct drm_device * dev, int *buffer, int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	if ((dwords+1) * sizeof(int) >= dev_priv->ring.Size - 8)
		return -EINVAL;

	BEGIN_LP_RING((dwords+1)&~1);

	for (i = 0; i < dwords;) {
		int cmd, sz;

		cmd = buffer[i];

		if ((sz = validate_cmd(cmd)) == 0 || i + sz > dwords)
			return -EINVAL;

		OUT_RING(cmd);

		while (++i, --sz) {
			OUT_RING(buffer[i]);
		}
	}

	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();

	return 0;
}

int
i915_emit_box(struct drm_device *dev,
	      struct drm_clip_rect *boxes,
	      int i, int DR1, int DR4)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_clip_rect box = boxes[i];
	RING_LOCALS;

	if (box.y2 <= box.y1 || box.x2 <= box.x1 || box.y2 <= 0 || box.x2 <= 0) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box.x1, box.y1, box.x2, box.y2);
		return -EINVAL;
	}

	if (IS_I965G(dev)) {
		BEGIN_LP_RING(4);
		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		ADVANCE_LP_RING();
	} else {
		BEGIN_LP_RING(6);
		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(DR1);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}

	return 0;
}

/* XXX: Emitting the counter should really be moved to part of the IRQ
 * emit. For now, do it in both places:
 */

static void i915_emit_breadcrumb(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	RING_LOCALS;

	dev_priv->counter++;
	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->counter = 0;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->last_enqueue = dev_priv->counter;

	BEGIN_LP_RING(4);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();
}

static int i915_dispatch_cmdbuffer(struct drm_device * dev,
				   drm_i915_cmdbuffer_t *cmd,
				   struct drm_clip_rect *cliprects,
				   void *cmdbuf)
{
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, cliprects, i,
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, cmdbuf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_batchbuffer(struct drm_device * dev,
				     drm_i915_batchbuffer_t * batch,
				     struct drm_clip_rect *cliprects)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int nbox = batch->num_cliprects;
	int i = 0, count;
	RING_LOCALS;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, cliprects, i,
						batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (!IS_I830(dev) && !IS_845G(dev)) {
			BEGIN_LP_RING(2);
			if (IS_I965G(dev)) {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6) | MI_BATCH_NON_SECURE_I965);
				OUT_RING(batch->start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
				OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			}
			ADVANCE_LP_RING();
		} else {
			BEGIN_LP_RING(4);
			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			OUT_RING(batch->start + batch->used - 4);
			OUT_RING(0);
			ADVANCE_LP_RING();
		}
	}

	i915_emit_breadcrumb(dev);

	return 0;
}

static int i915_dispatch_flip(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv =
		dev->primary->master->driver_priv;
	RING_LOCALS;

	if (!master_priv->sarea_priv)
		return -EINVAL;

	DRM_DEBUG("%s: page=%d pfCurrentPage=%d\n",
		  __func__,
		  dev_priv->current_page,
		  master_priv->sarea_priv->pf_current_page);

	i915_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(MI_FLUSH | MI_READ_FLUSH);
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(6);
	OUT_RING(CMD_OP_DISPLAYBUFFER_INFO | ASYNC_FLIP);
	OUT_RING(0);
	if (dev_priv->current_page == 0) {
		OUT_RING(dev_priv->back_offset);
		dev_priv->current_page = 1;
	} else {
		OUT_RING(dev_priv->front_offset);
		dev_priv->current_page = 0;
	}
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(2);
	OUT_RING(MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_A_FLIP);
	OUT_RING(0);
	ADVANCE_LP_RING();

	master_priv->sarea_priv->last_enqueue = dev_priv->counter++;

	BEGIN_LP_RING(4);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();

	master_priv->sarea_priv->pf_current_page = dev_priv->current_page;
	return 0;
}

static int i915_quiescent(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	i915_kernel_lost_context(dev);
	return i915_wait_ring(dev, dev_priv->ring.Size - 8, __func__);
}

static int i915_flush_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	int ret;

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	mutex_lock(&dev->struct_mutex);
	ret = i915_quiescent(dev);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

static int i915_batchbuffer(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    master_priv->sarea_priv;
	drm_i915_batchbuffer_t *batch = data;
	int ret;
	struct drm_clip_rect *cliprects = NULL;

	if (!dev_priv->allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return -EINVAL;
	}

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d\n",
		  batch->start, batch->used, batch->num_cliprects);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (batch->num_cliprects < 0)
		return -EINVAL;

	if (batch->num_cliprects) {
		cliprects = drm_calloc(batch->num_cliprects,
				       sizeof(struct drm_clip_rect),
				       DRM_MEM_DRIVER);
		if (cliprects == NULL)
			return -ENOMEM;

		ret = copy_from_user(cliprects, batch->cliprects,
				     batch->num_cliprects *
				     sizeof(struct drm_clip_rect));
		if (ret != 0)
			goto fail_free;
	}

	mutex_lock(&dev->struct_mutex);
	ret = i915_dispatch_batchbuffer(dev, batch, cliprects);
	mutex_unlock(&dev->struct_mutex);

	if (sarea_priv)
		sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

fail_free:
	drm_free(cliprects,
		 batch->num_cliprects * sizeof(struct drm_clip_rect),
		 DRM_MEM_DRIVER);

	return ret;
}

static int i915_cmdbuffer(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    master_priv->sarea_priv;
	drm_i915_cmdbuffer_t *cmdbuf = data;
	struct drm_clip_rect *cliprects = NULL;
	void *batch_data;
	int ret;

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf->buf, cmdbuf->sz, cmdbuf->num_cliprects);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (cmdbuf->num_cliprects < 0)
		return -EINVAL;

	batch_data = drm_alloc(cmdbuf->sz, DRM_MEM_DRIVER);
	if (batch_data == NULL)
		return -ENOMEM;

	ret = copy_from_user(batch_data, cmdbuf->buf, cmdbuf->sz);
	if (ret != 0)
		goto fail_batch_free;

	if (cmdbuf->num_cliprects) {
		cliprects = drm_calloc(cmdbuf->num_cliprects,
				       sizeof(struct drm_clip_rect),
				       DRM_MEM_DRIVER);
		if (cliprects == NULL)
			goto fail_batch_free;

		ret = copy_from_user(cliprects, cmdbuf->cliprects,
				     cmdbuf->num_cliprects *
				     sizeof(struct drm_clip_rect));
		if (ret != 0)
			goto fail_clip_free;
	}

	mutex_lock(&dev->struct_mutex);
	ret = i915_dispatch_cmdbuffer(dev, cmdbuf, cliprects, batch_data);
	mutex_unlock(&dev->struct_mutex);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		goto fail_clip_free;
	}

	if (sarea_priv)
		sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

fail_clip_free:
	drm_free(cliprects,
		 cmdbuf->num_cliprects * sizeof(struct drm_clip_rect),
		 DRM_MEM_DRIVER);
fail_batch_free:
	drm_free(batch_data, cmdbuf->sz, DRM_MEM_DRIVER);

	return ret;
}

static int i915_flip_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	int ret;

	DRM_DEBUG("%s\n", __func__);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	mutex_lock(&dev->struct_mutex);
	ret = i915_dispatch_flip(dev);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

static int i915_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t *param = data;
	int value;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->pdev->irq ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	case I915_PARAM_CHIPSET_ID:
		value = dev->pci_device;
		break;
	case I915_PARAM_HAS_GEM:
		value = dev_priv->has_gem;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs - dev_priv->fence_reg_start;
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	if (DRM_COPY_TO_USER(param->value, &value, sizeof(int))) {
		DRM_ERROR("DRM_COPY_TO_USER failed\n");
		return -EFAULT;
	}

	return 0;
}

static int i915_setparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t *param = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		dev_priv->tex_lru_log_granularity = param->value;
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->allow_batchbuffer = param->value;
		break;
	case I915_SETPARAM_NUM_USED_FENCES:
		if (param->value > dev_priv->num_fence_regs ||
		    param->value < 0)
			return -EINVAL;
		/* Userspace can use first N regs */
		dev_priv->fence_reg_start = param->value;
		break;
	default:
		DRM_DEBUG("unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	return 0;
}

static int i915_set_status_page(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_hws_addr_t *hws = data;

	if (!I915_NEED_GFX_HWS(dev))
		return -EINVAL;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		WARN(1, "tried to set status page when mode setting active\n");
		return 0;
	}

	printk(KERN_DEBUG "set status page addr 0x%08x\n", (u32)hws->addr);

	dev_priv->status_gfx_addr = hws->addr & (0x1ffff<<12);

	dev_priv->hws_map.offset = dev->agp->base + hws->addr;
	dev_priv->hws_map.size = 4*1024;
	dev_priv->hws_map.type = 0;
	dev_priv->hws_map.flags = 0;
	dev_priv->hws_map.mtrr = 0;

	drm_core_ioremap_wc(&dev_priv->hws_map, dev);
	if (dev_priv->hws_map.handle == NULL) {
		i915_dma_cleanup(dev);
		dev_priv->status_gfx_addr = 0;
		DRM_ERROR("can not ioremap virtual address for"
				" G33 hw status page\n");
		return -ENOMEM;
	}
	dev_priv->hw_status_page = dev_priv->hws_map.handle;

	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws HWS_PGA with gfx mem 0x%x\n",
			dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws at %p\n", dev_priv->hw_status_page);
	return 0;
}

/**
 * i915_probe_agp - get AGP bootup configuration
 * @pdev: PCI device
 * @aperture_size: returns AGP aperture configured size
 * @preallocated_size: returns size of BIOS preallocated AGP space
 *
 * Since Intel integrated graphics are UMA, the BIOS has to set aside
 * some RAM for the framebuffer at early boot.  This code figures out
 * how much was set aside so we can use it for our own purposes.
 */
static int i915_probe_agp(struct drm_device *dev, unsigned long *aperture_size,
			  unsigned long *preallocated_size)
{
	struct pci_dev *bridge_dev;
	u16 tmp = 0;
	unsigned long overhead;
	unsigned long stolen;

	bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0,0));
	if (!bridge_dev) {
		DRM_ERROR("bridge device not found\n");
		return -1;
	}

	/* Get the fb aperture size and "stolen" memory amount. */
	pci_read_config_word(bridge_dev, INTEL_GMCH_CTRL, &tmp);
	pci_dev_put(bridge_dev);

	*aperture_size = 1024 * 1024;
	*preallocated_size = 1024 * 1024;

	switch (dev->pdev->device) {
	case PCI_DEVICE_ID_INTEL_82830_CGC:
	case PCI_DEVICE_ID_INTEL_82845G_IG:
	case PCI_DEVICE_ID_INTEL_82855GM_IG:
	case PCI_DEVICE_ID_INTEL_82865_IG:
		if ((tmp & INTEL_GMCH_MEM_MASK) == INTEL_GMCH_MEM_64M)
			*aperture_size *= 64;
		else
			*aperture_size *= 128;
		break;
	default:
		/* 9xx supports large sizes, just look at the length */
		*aperture_size = pci_resource_len(dev->pdev, 2);
		break;
	}

	/*
	 * Some of the preallocated space is taken by the GTT
	 * and popup.  GTT is 1K per MB of aperture size, and popup is 4K.
	 */
	if (IS_G4X(dev) || IS_IGD(dev))
		overhead = 4096;
	else
		overhead = (*aperture_size / 1024) + 4096;

	switch (tmp & INTEL_GMCH_GMS_MASK) {
	case INTEL_855_GMCH_GMS_DISABLED:
		DRM_ERROR("video memory is disabled\n");
		return -1;
	case INTEL_855_GMCH_GMS_STOLEN_1M:
		stolen = 1 * 1024 * 1024;
		break;
	case INTEL_855_GMCH_GMS_STOLEN_4M:
		stolen = 4 * 1024 * 1024;
		break;
	case INTEL_855_GMCH_GMS_STOLEN_8M:
		stolen = 8 * 1024 * 1024;
		break;
	case INTEL_855_GMCH_GMS_STOLEN_16M:
		stolen = 16 * 1024 * 1024;
		break;
	case INTEL_855_GMCH_GMS_STOLEN_32M:
		stolen = 32 * 1024 * 1024;
		break;
	case INTEL_915G_GMCH_GMS_STOLEN_48M:
		stolen = 48 * 1024 * 1024;
		break;
	case INTEL_915G_GMCH_GMS_STOLEN_64M:
		stolen = 64 * 1024 * 1024;
		break;
	case INTEL_GMCH_GMS_STOLEN_128M:
		stolen = 128 * 1024 * 1024;
		break;
	case INTEL_GMCH_GMS_STOLEN_256M:
		stolen = 256 * 1024 * 1024;
		break;
	case INTEL_GMCH_GMS_STOLEN_96M:
		stolen = 96 * 1024 * 1024;
		break;
	case INTEL_GMCH_GMS_STOLEN_160M:
		stolen = 160 * 1024 * 1024;
		break;
	case INTEL_GMCH_GMS_STOLEN_224M:
		stolen = 224 * 1024 * 1024;
		break;
	case INTEL_GMCH_GMS_STOLEN_352M:
		stolen = 352 * 1024 * 1024;
		break;
	default:
		DRM_ERROR("unexpected GMCH_GMS value: 0x%02x\n",
			tmp & INTEL_GMCH_GMS_MASK);
		return -1;
	}
	*preallocated_size = stolen - overhead;

	return 0;
}

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long agp_size, prealloc_size;
	int fb_bar = IS_I9XX(dev) ? 2 : 0;
	int ret = 0;

	dev->mode_config.fb_base = drm_get_resource_start(dev, fb_bar) &
		0xff000000;

	if (IS_MOBILE(dev) || IS_I9XX(dev))
		dev_priv->cursor_needs_physical = true;
	else
		dev_priv->cursor_needs_physical = false;

	if (IS_I965G(dev) || IS_G33(dev))
		dev_priv->cursor_needs_physical = false;

	ret = i915_probe_agp(dev, &agp_size, &prealloc_size);
	if (ret)
		goto out;

	/* Basic memrange allocator for stolen space (aka vram) */
	drm_mm_init(&dev_priv->vram, 0, prealloc_size);

	/* Let GEM Manage from end of prealloc space to end of aperture.
	 *
	 * However, leave one page at the end still bound to the scratch page.
	 * There are a number of places where the hardware apparently
	 * prefetches past the end of the object, and we've seen multiple
	 * hangs with the GPU head pointer stuck in a batchbuffer bound
	 * at the last page of the aperture.  One page should be enough to
	 * keep any prefetching inside of the aperture.
	 */
	i915_gem_do_init(dev, prealloc_size, agp_size - 4096);

	ret = i915_gem_init_ringbuffer(dev);
	if (ret)
		goto out;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->allow_batchbuffer = 1;

	ret = intel_init_bios(dev);
	if (ret)
		DRM_INFO("failed to find VBIOS tables\n");

	ret = drm_irq_install(dev);
	if (ret)
		goto destroy_ringbuffer;

	/* Always safe in the mode setting case. */
	/* FIXME: do pre/post-mode set stuff in core KMS code */
	dev->vblank_disable_allowed = 1;

	/*
	 * Initialize the hardware status page IRQ location.
	 */

	I915_WRITE(INSTPM, (1 << 5) | (1 << 21));

	intel_modeset_init(dev);

	drm_helper_initial_config(dev);

	return 0;

destroy_ringbuffer:
	i915_gem_cleanup_ringbuffer(dev);
out:
	return ret;
}

int i915_master_create(struct drm_device *dev, struct drm_master *master)
{
	struct drm_i915_master_private *master_priv;

	master_priv = drm_calloc(1, sizeof(*master_priv), DRM_MEM_DRIVER);
	if (!master_priv)
		return -ENOMEM;

	master->driver_priv = master_priv;
	return 0;
}

void i915_master_destroy(struct drm_device *dev, struct drm_master *master)
{
	struct drm_i915_master_private *master_priv = master->driver_priv;

	if (!master_priv)
		return;

	drm_free(master_priv, sizeof(*master_priv), DRM_MEM_DRIVER);

	master->driver_priv = NULL;
}

/**
 * i915_driver_load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	resource_size_t base, size;
	int ret = 0, mmio_bar = IS_I9XX(dev) ? 0 : 1;

	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	dev_priv = drm_alloc(sizeof(drm_i915_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return -ENOMEM;

	memset(dev_priv, 0, sizeof(drm_i915_private_t));

	dev->dev_private = (void *)dev_priv;
	dev_priv->dev = dev;

	/* Add register map (needed for suspend/resume) */
	base = drm_get_resource_start(dev, mmio_bar);
	size = drm_get_resource_len(dev, mmio_bar);

	dev_priv->regs = ioremap(base, size);
	if (!dev_priv->regs) {
		DRM_ERROR("failed to map registers\n");
		ret = -EIO;
		goto free_priv;
	}

        dev_priv->mm.gtt_mapping =
		io_mapping_create_wc(dev->agp->base,
				     dev->agp->agp_info.aper_size * 1024*1024);
	if (dev_priv->mm.gtt_mapping == NULL) {
		ret = -EIO;
		goto out_rmmap;
	}

	/* Set up a WC MTRR for non-PAT systems.  This is more common than
	 * one would think, because the kernel disables PAT on first
	 * generation Core chips because WC PAT gets overridden by a UC
	 * MTRR if present.  Even if a UC MTRR isn't present.
	 */
	dev_priv->mm.gtt_mtrr = mtrr_add(dev->agp->base,
					 dev->agp->agp_info.aper_size *
					 1024 * 1024,
					 MTRR_TYPE_WRCOMB, 1);
	if (dev_priv->mm.gtt_mtrr < 0) {
		DRM_INFO("MTRR allocation failed.  Graphics "
			 "performance may suffer.\n");
	}

#ifdef CONFIG_HIGHMEM64G
	/* don't enable GEM on PAE - needs agp + set_memory_* interface fixes */
	dev_priv->has_gem = 0;
#else
	/* enable GEM by default */
	dev_priv->has_gem = 1;
#endif

	dev->driver->get_vblank_counter = i915_get_vblank_counter;
	if (IS_GM45(dev))
		dev->driver->get_vblank_counter = gm45_get_vblank_counter;

	i915_gem_load(dev);

	/* Init HWS */
	if (!I915_NEED_GFX_HWS(dev)) {
		ret = i915_init_phys_hws(dev);
		if (ret != 0)
			goto out_iomapfree;
	}

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, but we use them anyways to avoid
	 * stuck interrupts on some machines.
	 */
	if (!IS_I945G(dev) && !IS_I945GM(dev))
		pci_enable_msi(dev->pdev);

	spin_lock_init(&dev_priv->user_irq_lock);
	dev_priv->user_irq_refcount = 0;

	ret = drm_vblank_init(dev, I915_NUM_PIPE);

	if (ret) {
		(void) i915_driver_unload(dev);
		return ret;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = i915_load_modeset_init(dev);
		if (ret < 0) {
			DRM_ERROR("failed to init modeset\n");
			goto out_rmmap;
		}
	}

	/* Must be done after probing outputs */
	intel_opregion_init(dev, 0);

	return 0;

out_iomapfree:
	io_mapping_free(dev_priv->mm.gtt_mapping);
out_rmmap:
	iounmap(dev_priv->regs);
free_priv:
	drm_free(dev_priv, sizeof(struct drm_i915_private), DRM_MEM_DRIVER);
	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	io_mapping_free(dev_priv->mm.gtt_mapping);
	if (dev_priv->mm.gtt_mtrr >= 0) {
		mtrr_del(dev_priv->mm.gtt_mtrr, dev->agp->base,
			 dev->agp->agp_info.aper_size * 1024 * 1024);
		dev_priv->mm.gtt_mtrr = -1;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		drm_irq_uninstall(dev);
	}

	if (dev->pdev->msi_enabled)
		pci_disable_msi(dev->pdev);

	if (dev_priv->regs != NULL)
		iounmap(dev_priv->regs);

	intel_opregion_free(dev, 0);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		intel_modeset_cleanup(dev);

		i915_gem_free_all_phys_object(dev);

		mutex_lock(&dev->struct_mutex);
		i915_gem_cleanup_ringbuffer(dev);
		mutex_unlock(&dev->struct_mutex);
		drm_mm_takedown(&dev_priv->vram);
		i915_gem_lastclose(dev);
	}

	drm_free(dev->dev_private, sizeof(drm_i915_private_t),
		 DRM_MEM_DRIVER);

	return 0;
}

int i915_driver_open(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_i915_file_private *i915_file_priv;

	DRM_DEBUG("\n");
	i915_file_priv = (struct drm_i915_file_private *)
	    drm_alloc(sizeof(*i915_file_priv), DRM_MEM_FILES);

	if (!i915_file_priv)
		return -ENOMEM;

	file_priv->driver_priv = i915_file_priv;

	i915_file_priv->mm.last_gem_seqno = 0;
	i915_file_priv->mm.last_gem_throttle_seqno = 0;

	return 0;
}

/**
 * i915_driver_lastclose - clean up after all DRM clients have exited
 * @dev: DRM device
 *
 * Take care of cleaning up after all DRM clients have exited.  In the
 * mode setting case, we want to restore the kernel's initial mode (just
 * in case the last client left us in a bad state).
 *
 * Additionally, in the non-mode setting case, we'll tear down the AGP
 * and DMA structures, since the kernel won't be using them, and clea
 * up any GEM state.
 */
void i915_driver_lastclose(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!dev_priv || drm_core_check_feature(dev, DRIVER_MODESET)) {
		intelfb_restore();
		return;
	}

	i915_gem_lastclose(dev);

	if (dev_priv->agp_heap)
		i915_mem_takedown(&(dev_priv->agp_heap));

	i915_dma_cleanup(dev);
}

void i915_driver_preclose(struct drm_device * dev, struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_mem_release(dev, file_priv, dev_priv->agp_heap);
}

void i915_driver_postclose(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_i915_file_private *i915_file_priv = file_priv->driver_priv;

	drm_free(i915_file_priv, sizeof(*i915_file_priv), DRM_MEM_FILES);
}

struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF(DRM_I915_INIT, i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_FLUSH, i915_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_FLIP, i915_flip_bufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_BATCHBUFFER, i915_batchbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_IRQ_EMIT, i915_irq_emit, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_IRQ_WAIT, i915_irq_wait, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GETPARAM, i915_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_SETPARAM, i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_ALLOC, i915_mem_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_FREE, i915_mem_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_INIT_HEAP, i915_mem_init_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_CMDBUFFER, i915_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_DESTROY_HEAP,  i915_mem_destroy_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY ),
	DRM_IOCTL_DEF(DRM_I915_SET_VBLANK_PIPE,  i915_vblank_pipe_set, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY ),
	DRM_IOCTL_DEF(DRM_I915_GET_VBLANK_PIPE,  i915_vblank_pipe_get, DRM_AUTH ),
	DRM_IOCTL_DEF(DRM_I915_VBLANK_SWAP, i915_vblank_swap, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_HWS_ADDR, i915_set_status_page, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_INIT, i915_gem_init_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_EXECBUFFER, i915_gem_execbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GEM_PIN, i915_gem_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_UNPIN, i915_gem_unpin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GEM_ENTERVT, i915_gem_entervt_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_LEAVEVT, i915_gem_leavevt_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_CREATE, i915_gem_create_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_PREAD, i915_gem_pread_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_PWRITE, i915_gem_pwrite_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_MMAP, i915_gem_mmap_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_MMAP_GTT, i915_gem_mmap_gtt_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_SET_TILING, i915_gem_set_tiling, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_GET_TILING, i915_gem_get_tiling, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id, 0),
};

int i915_max_ioctl = DRM_ARRAY_SIZE(i915_ioctls);

/**
 * Determine if the device really is AGP or not.
 *
 * All Intel graphics chipsets are treated as AGP, even if they are really
 * PCI-e.
 *
 * \param dev   The device to be tested.
 *
 * \returns
 * A value of 1 is always retured to indictate every i9x5 is AGP.
 */
int i915_driver_device_is_agp(struct drm_device * dev)
{
	return 1;
}
