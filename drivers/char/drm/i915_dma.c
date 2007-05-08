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
#include "i915_drm.h"
#include "i915_drv.h"

#define IS_I965G(dev) (dev->pci_device == 0x2972 || \
		       dev->pci_device == 0x2982 || \
		       dev->pci_device == 0x2992 || \
		       dev->pci_device == 0x29A2 || \
		       dev->pci_device == 0x2A02)

/* Really want an OS-independent resettable timer.  Would like to have
 * this loop run for (eg) 3 sec, but have the timer reset every time
 * the head pointer changes, so that EBUSY only happens if the ring
 * actually stalls for (eg) 3 seconds.
 */
int i915_wait_ring(drm_device_t * dev, int n, const char *caller)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);
	u32 last_head = I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	int i;

	for (i = 0; i < 10000; i++) {
		ring->head = I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
		if (ring->space >= n)
			return 0;

		dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

		if (ring->head != last_head)
			i = 0;

		last_head = ring->head;
	}

	return DRM_ERR(EBUSY);
}

void i915_kernel_lost_context(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);

	ring->head = I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	ring->tail = I915_READ(LP_RING + RING_TAIL) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;

	if (ring->head == ring->tail)
		dev_priv->sarea_priv->perf_boxes |= I915_BOX_RING_EMPTY;
}

static int i915_dma_cleanup(drm_device_t * dev)
{
	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq)
		drm_irq_uninstall(dev);

	if (dev->dev_private) {
		drm_i915_private_t *dev_priv =
		    (drm_i915_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start) {
			drm_core_ioremapfree(&dev_priv->ring.map, dev);
		}

		if (dev_priv->status_page_dmah) {
			drm_pci_free(dev, dev_priv->status_page_dmah);
			/* Need to rewrite hardware status page */
			I915_WRITE(0x02080, 0x1ffff000);
		}

		drm_free(dev->dev_private, sizeof(drm_i915_private_t),
			 DRM_MEM_DRIVER);

		dev->dev_private = NULL;
	}

	return 0;
}

static int i915_initialize(drm_device_t * dev,
			   drm_i915_private_t * dev_priv,
			   drm_i915_init_t * init)
{
	memset(dev_priv, 0, sizeof(drm_i915_private_t));

	DRM_GETSAREA();
	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		return DRM_ERR(EINVAL);
	}

	dev_priv->mmio_map = drm_core_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio_map) {
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		DRM_ERROR("can not find mmio map!\n");
		return DRM_ERR(EINVAL);
	}

	dev_priv->sarea_priv = (drm_i915_sarea_t *)
	    ((u8 *) dev_priv->sarea->handle + init->sarea_priv_offset);

	dev_priv->ring.Start = init->ring_start;
	dev_priv->ring.End = init->ring_end;
	dev_priv->ring.Size = init->ring_size;
	dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;

	dev_priv->ring.map.offset = init->ring_start;
	dev_priv->ring.map.size = init->ring_size;
	dev_priv->ring.map.type = 0;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_core_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->cpp = init->cpp;
	dev_priv->back_offset = init->back_offset;
	dev_priv->front_offset = init->front_offset;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;

	/* We are using separate values as placeholders for mechanisms for
	 * private backbuffer/depthbuffer usage.
	 */
	dev_priv->use_mi_batchbuffer_start = 0;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->allow_batchbuffer = 1;

	/* Program Hardware Status Page */
	dev_priv->status_page_dmah = drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE,
						   0xffffffff);

	if (!dev_priv->status_page_dmah) {
		dev->dev_private = (void *)dev_priv;
		i915_dma_cleanup(dev);
		DRM_ERROR("Can not allocate hardware status page\n");
		return DRM_ERR(ENOMEM);
	}
	dev_priv->hw_status_page = dev_priv->status_page_dmah->vaddr;
	dev_priv->dma_status_page = dev_priv->status_page_dmah->busaddr;

	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	I915_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	dev->dev_private = (void *)dev_priv;

	return 0;
}

static int i915_dma_resume(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	DRM_DEBUG("%s\n", __FUNCTION__);

	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		return DRM_ERR(EINVAL);
	}

	if (!dev_priv->mmio_map) {
		DRM_ERROR("can not find mmio map!\n");
		return DRM_ERR(EINVAL);
	}

	if (dev_priv->ring.map.handle == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	/* Program Hardware Status Page */
	if (!dev_priv->hw_status_page) {
		DRM_ERROR("Can not find hardware status page\n");
		return DRM_ERR(EINVAL);
	}
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	I915_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	return 0;
}

static int i915_dma_init(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv;
	drm_i915_init_t init;
	int retcode = 0;

	DRM_COPY_FROM_USER_IOCTL(init, (drm_i915_init_t __user *) data,
				 sizeof(init));

	switch (init.func) {
	case I915_INIT_DMA:
		dev_priv = drm_alloc(sizeof(drm_i915_private_t),
				     DRM_MEM_DRIVER);
		if (dev_priv == NULL)
			return DRM_ERR(ENOMEM);
		retcode = i915_initialize(dev, dev_priv, &init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = DRM_ERR(EINVAL);
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

/* 	printk("validate_cmd( %x ): %d\n", cmd, ret); */

	return ret;
}

static int i915_emit_cmds(drm_device_t * dev, int __user * buffer, int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	if ((dwords+1) * sizeof(int) >= dev_priv->ring.Size - 8)
		return DRM_ERR(EINVAL);

	BEGIN_LP_RING((dwords+1)&~1);

	for (i = 0; i < dwords;) {
		int cmd, sz;

		if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i], sizeof(cmd)))
			return DRM_ERR(EINVAL);

		if ((sz = validate_cmd(cmd)) == 0 || i + sz > dwords)
			return DRM_ERR(EINVAL);

		OUT_RING(cmd);

		while (++i, --sz) {
			if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i],
							 sizeof(cmd))) {
				return DRM_ERR(EINVAL);
			}
			OUT_RING(cmd);
		}
	}

	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();

	return 0;
}

static int i915_emit_box(drm_device_t * dev,
			 drm_clip_rect_t __user * boxes,
			 int i, int DR1, int DR4)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t box;
	RING_LOCALS;

	if (DRM_COPY_FROM_USER_UNCHECKED(&box, &boxes[i], sizeof(box))) {
		return DRM_ERR(EFAULT);
	}

	if (box.y2 <= box.y1 || box.x2 <= box.x1 || box.y2 <= 0 || box.x2 <= 0) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box.x1, box.y1, box.x2, box.y2);
		return DRM_ERR(EINVAL);
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

static void i915_emit_breadcrumb(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	dev_priv->sarea_priv->last_enqueue = ++dev_priv->counter;

	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->sarea_priv->last_enqueue = dev_priv->counter = 1;

	BEGIN_LP_RING(4);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(20);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();
}

static int i915_dispatch_cmdbuffer(drm_device_t * dev,
				   drm_i915_cmdbuffer_t * cmd)
{
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return DRM_ERR(EINVAL);
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, cmd->cliprects, i,
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, (int __user *)cmd->buf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_batchbuffer(drm_device_t * dev,
				     drm_i915_batchbuffer_t * batch)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t __user *boxes = batch->cliprects;
	int nbox = batch->num_cliprects;
	int i = 0, count;
	RING_LOCALS;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return DRM_ERR(EINVAL);
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, boxes, i,
						batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (dev_priv->use_mi_batchbuffer_start) {
			BEGIN_LP_RING(2);
			OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
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

static int i915_dispatch_flip(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	DRM_DEBUG("%s: page=%d pfCurrentPage=%d\n",
		  __FUNCTION__,
		  dev_priv->current_page,
		  dev_priv->sarea_priv->pf_current_page);

	i915_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(INST_PARSER_CLIENT | INST_OP_FLUSH | INST_FLUSH_MAP_CACHE);
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

	dev_priv->sarea_priv->last_enqueue = dev_priv->counter++;

	BEGIN_LP_RING(4);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(20);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();

	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;
	return 0;
}

static int i915_quiescent(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	i915_kernel_lost_context(dev);
	return i915_wait_ring(dev, dev_priv->ring.Size - 8, __FUNCTION__);
}

static int i915_flush_ioctl(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	LOCK_TEST_WITH_RETURN(dev, filp);

	return i915_quiescent(dev);
}

static int i915_batchbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_batchbuffer_t batch;
	int ret;

	if (!dev_priv->allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(batch, (drm_i915_batchbuffer_t __user *) data,
				 sizeof(batch));

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d\n",
		  batch.start, batch.used, batch.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (batch.num_cliprects && DRM_VERIFYAREA_READ(batch.cliprects,
						       batch.num_cliprects *
						       sizeof(drm_clip_rect_t)))
		return DRM_ERR(EFAULT);

	ret = i915_dispatch_batchbuffer(dev, &batch);

	sarea_priv->last_dispatch = (int)hw_status[5];
	return ret;
}

static int i915_cmdbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_cmdbuffer_t cmdbuf;
	int ret;

	DRM_COPY_FROM_USER_IOCTL(cmdbuf, (drm_i915_cmdbuffer_t __user *) data,
				 sizeof(cmdbuf));

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf.buf, cmdbuf.sz, cmdbuf.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (cmdbuf.num_cliprects &&
	    DRM_VERIFYAREA_READ(cmdbuf.cliprects,
				cmdbuf.num_cliprects *
				sizeof(drm_clip_rect_t))) {
		DRM_ERROR("Fault accessing cliprects\n");
		return DRM_ERR(EFAULT);
	}

	ret = i915_dispatch_cmdbuffer(dev, &cmdbuf);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		return ret;
	}

	sarea_priv->last_dispatch = (int)hw_status[5];
	return 0;
}

static int i915_flip_bufs(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	DRM_DEBUG("%s\n", __FUNCTION__);

	LOCK_TEST_WITH_RETURN(dev, filp);

	return i915_dispatch_flip(dev);
}

static int i915_getparam(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t param;
	int value;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(param, (drm_i915_getparam_t __user *) data,
				 sizeof(param));

	switch (param.param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->irq ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	default:
		DRM_ERROR("Unknown parameter %d\n", param.param);
		return DRM_ERR(EINVAL);
	}

	if (DRM_COPY_TO_USER(param.value, &value, sizeof(int))) {
		DRM_ERROR("DRM_COPY_TO_USER failed\n");
		return DRM_ERR(EFAULT);
	}

	return 0;
}

static int i915_setparam(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t param;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(param, (drm_i915_setparam_t __user *) data,
				 sizeof(param));

	switch (param.param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		dev_priv->use_mi_batchbuffer_start = param.value;
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		dev_priv->tex_lru_log_granularity = param.value;
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->allow_batchbuffer = param.value;
		break;
	default:
		DRM_ERROR("unknown parameter %d\n", param.param);
		return DRM_ERR(EINVAL);
	}

	return 0;
}

int i915_driver_load(drm_device_t *dev, unsigned long flags)
{
	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	return 0;
}

void i915_driver_lastclose(drm_device_t * dev)
{
	if (dev->dev_private) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		i915_mem_takedown(&(dev_priv->agp_heap));
	}
	i915_dma_cleanup(dev);
}

void i915_driver_preclose(drm_device_t * dev, DRMFILE filp)
{
	if (dev->dev_private) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		i915_mem_release(dev, filp, dev_priv->agp_heap);
	}
}

drm_ioctl_desc_t i915_ioctls[] = {
	[DRM_IOCTL_NR(DRM_I915_INIT)] = {i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_FLUSH)] = {i915_flush_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FLIP)] = {i915_flip_bufs, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_BATCHBUFFER)] = {i915_batchbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_EMIT)] = {i915_irq_emit, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_WAIT)] = {i915_irq_wait, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_GETPARAM)] = {i915_getparam, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_SETPARAM)] = {i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_ALLOC)] = {i915_mem_alloc, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FREE)] = {i915_mem_free, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_INIT_HEAP)] = {i915_mem_init_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_CMDBUFFER)] = {i915_cmdbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_DESTROY_HEAP)] = { i915_mem_destroy_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY },
	[DRM_IOCTL_NR(DRM_I915_SET_VBLANK_PIPE)] = { i915_vblank_pipe_set, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY },
	[DRM_IOCTL_NR(DRM_I915_GET_VBLANK_PIPE)] = { i915_vblank_pipe_get, DRM_AUTH },
	[DRM_IOCTL_NR(DRM_I915_VBLANK_SWAP)] = {i915_vblank_swap, DRM_AUTH},
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
int i915_driver_device_is_agp(drm_device_t * dev)
{
	return 1;
}
