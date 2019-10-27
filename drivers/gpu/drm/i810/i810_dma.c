/* i810_dma.c -- DMA support for the i810 -*- linux-c -*-
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
 *
 * Authors: Rickard E. (Rik) Faith <faith@valinux.com>
 *	    Jeff Hartmann <jhartmann@valinux.com>
 *          Keith Whitwell <keith@tungstengraphics.com>
 *
 */

#include <linux/delay.h>
#include <linux/mman.h>

#include <drm/drm_agpsupport.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_irq.h>
#include <drm/drm_pci.h>
#include <drm/drm_print.h>
#include <drm/i810_drm.h>

#include "i810_drv.h"

#define I810_BUF_FREE		2
#define I810_BUF_CLIENT		1
#define I810_BUF_HARDWARE	0

#define I810_BUF_UNMAPPED 0
#define I810_BUF_MAPPED   1

static struct drm_buf *i810_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	int i;
	int used;

	/* Linear search might not be the best solution */

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i810_buf_priv_t *buf_priv = buf->dev_private;
		/* In use is already a pointer */
		used = cmpxchg(buf_priv->in_use, I810_BUF_FREE,
			       I810_BUF_CLIENT);
		if (used == I810_BUF_FREE)
			return buf;
	}
	return NULL;
}

/* This should only be called if the buffer is not sent to the hardware
 * yet, the hardware updates in use for us once its on the ring buffer.
 */

static int i810_freelist_put(struct drm_device *dev, struct drm_buf *buf)
{
	drm_i810_buf_priv_t *buf_priv = buf->dev_private;
	int used;

	/* In use is already a pointer */
	used = cmpxchg(buf_priv->in_use, I810_BUF_CLIENT, I810_BUF_FREE);
	if (used != I810_BUF_CLIENT) {
		DRM_ERROR("Freeing buffer thats not in use : %d\n", buf->idx);
		return -EINVAL;
	}

	return 0;
}

static int i810_mmap_buffers(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev;
	drm_i810_private_t *dev_priv;
	struct drm_buf *buf;
	drm_i810_buf_priv_t *buf_priv;

	dev = priv->minor->dev;
	dev_priv = dev->dev_private;
	buf = dev_priv->mmap_buffer;
	buf_priv = buf->dev_private;

	vma->vm_flags |= VM_DONTCOPY;

	buf_priv->currently_mapped = I810_BUF_MAPPED;

	if (io_remap_pfn_range(vma, vma->vm_start,
			       vma->vm_pgoff,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static const struct file_operations i810_buffer_fops = {
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = i810_mmap_buffers,
	.compat_ioctl = drm_compat_ioctl,
	.llseek = noop_llseek,
};

static int i810_map_buffer(struct drm_buf *buf, struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	drm_i810_buf_priv_t *buf_priv = buf->dev_private;
	drm_i810_private_t *dev_priv = dev->dev_private;
	const struct file_operations *old_fops;
	int retcode = 0;

	if (buf_priv->currently_mapped == I810_BUF_MAPPED)
		return -EINVAL;

	/* This is all entirely broken */
	old_fops = file_priv->filp->f_op;
	file_priv->filp->f_op = &i810_buffer_fops;
	dev_priv->mmap_buffer = buf;
	buf_priv->virtual = (void *)vm_mmap(file_priv->filp, 0, buf->total,
					    PROT_READ | PROT_WRITE,
					    MAP_SHARED, buf->bus_address);
	dev_priv->mmap_buffer = NULL;
	file_priv->filp->f_op = old_fops;
	if (IS_ERR(buf_priv->virtual)) {
		/* Real error */
		DRM_ERROR("mmap error\n");
		retcode = PTR_ERR(buf_priv->virtual);
		buf_priv->virtual = NULL;
	}

	return retcode;
}

static int i810_unmap_buffer(struct drm_buf *buf)
{
	drm_i810_buf_priv_t *buf_priv = buf->dev_private;
	int retcode = 0;

	if (buf_priv->currently_mapped != I810_BUF_MAPPED)
		return -EINVAL;

	retcode = vm_munmap((unsigned long)buf_priv->virtual,
			    (size_t) buf->total);

	buf_priv->currently_mapped = I810_BUF_UNMAPPED;
	buf_priv->virtual = NULL;

	return retcode;
}

static int i810_dma_get_buffer(struct drm_device *dev, drm_i810_dma_t *d,
			       struct drm_file *file_priv)
{
	struct drm_buf *buf;
	drm_i810_buf_priv_t *buf_priv;
	int retcode = 0;

	buf = i810_freelist_get(dev);
	if (!buf) {
		retcode = -ENOMEM;
		DRM_DEBUG("retcode=%d\n", retcode);
		return retcode;
	}

	retcode = i810_map_buffer(buf, file_priv);
	if (retcode) {
		i810_freelist_put(dev, buf);
		DRM_ERROR("mapbuf failed, retcode %d\n", retcode);
		return retcode;
	}
	buf->file_priv = file_priv;
	buf_priv = buf->dev_private;
	d->granted = 1;
	d->request_idx = buf->idx;
	d->request_size = buf->total;
	d->virtual = buf_priv->virtual;

	return retcode;
}

static int i810_dma_cleanup(struct drm_device *dev)
{
	struct drm_device_dma *dma = dev->dma;

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (drm_core_check_feature(dev, DRIVER_HAVE_IRQ) && dev->irq_enabled)
		drm_irq_uninstall(dev);

	if (dev->dev_private) {
		int i;
		drm_i810_private_t *dev_priv =
		    (drm_i810_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start)
			drm_legacy_ioremapfree(&dev_priv->ring.map, dev);
		if (dev_priv->hw_status_page) {
			pci_free_consistent(dev->pdev, PAGE_SIZE,
					    dev_priv->hw_status_page,
					    dev_priv->dma_status_page);
		}
		kfree(dev->dev_private);
		dev->dev_private = NULL;

		for (i = 0; i < dma->buf_count; i++) {
			struct drm_buf *buf = dma->buflist[i];
			drm_i810_buf_priv_t *buf_priv = buf->dev_private;

			if (buf_priv->kernel_virtual && buf->total)
				drm_legacy_ioremapfree(&buf_priv->map, dev);
		}
	}
	return 0;
}

static int i810_wait_ring(struct drm_device *dev, int n)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_ring_buffer_t *ring = &(dev_priv->ring);
	int iters = 0;
	unsigned long end;
	unsigned int last_head = I810_READ(LP_RING + RING_HEAD) & HEAD_ADDR;

	end = jiffies + (HZ * 3);
	while (ring->space < n) {
		ring->head = I810_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;

		if (ring->head != last_head) {
			end = jiffies + (HZ * 3);
			last_head = ring->head;
		}

		iters++;
		if (time_before(end, jiffies)) {
			DRM_ERROR("space: %d wanted %d\n", ring->space, n);
			DRM_ERROR("lockup\n");
			goto out_wait_ring;
		}
		udelay(1);
	}

out_wait_ring:
	return iters;
}

static void i810_kernel_lost_context(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_ring_buffer_t *ring = &(dev_priv->ring);

	ring->head = I810_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	ring->tail = I810_READ(LP_RING + RING_TAIL);
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;
}

static int i810_freelist_init(struct drm_device *dev, drm_i810_private_t *dev_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int my_idx = 24;
	u32 *hw_status = (u32 *) (dev_priv->hw_status_page + my_idx);
	int i;

	if (dma->buf_count > 1019) {
		/* Not enough space in the status page for the freelist */
		return -EINVAL;
	}

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i810_buf_priv_t *buf_priv = buf->dev_private;

		buf_priv->in_use = hw_status++;
		buf_priv->my_use_idx = my_idx;
		my_idx += 4;

		*buf_priv->in_use = I810_BUF_FREE;

		buf_priv->map.offset = buf->bus_address;
		buf_priv->map.size = buf->total;
		buf_priv->map.type = _DRM_AGP;
		buf_priv->map.flags = 0;
		buf_priv->map.mtrr = 0;

		drm_legacy_ioremap(&buf_priv->map, dev);
		buf_priv->kernel_virtual = buf_priv->map.handle;

	}
	return 0;
}

static int i810_dma_initialize(struct drm_device *dev,
			       drm_i810_private_t *dev_priv,
			       drm_i810_init_t *init)
{
	struct drm_map_list *r_list;
	memset(dev_priv, 0, sizeof(drm_i810_private_t));

	list_for_each_entry(r_list, &dev->maplist, head) {
		if (r_list->map &&
		    r_list->map->type == _DRM_SHM &&
		    r_list->map->flags & _DRM_CONTAINS_LOCK) {
			dev_priv->sarea_map = r_list->map;
			break;
		}
	}
	if (!dev_priv->sarea_map) {
		dev->dev_private = (void *)dev_priv;
		i810_dma_cleanup(dev);
		DRM_ERROR("can not find sarea!\n");
		return -EINVAL;
	}
	dev_priv->mmio_map = drm_legacy_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio_map) {
		dev->dev_private = (void *)dev_priv;
		i810_dma_cleanup(dev);
		DRM_ERROR("can not find mmio map!\n");
		return -EINVAL;
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_legacy_findmap(dev, init->buffers_offset);
	if (!dev->agp_buffer_map) {
		dev->dev_private = (void *)dev_priv;
		i810_dma_cleanup(dev);
		DRM_ERROR("can not find dma buffer map!\n");
		return -EINVAL;
	}

	dev_priv->sarea_priv = (drm_i810_sarea_t *)
	    ((u8 *) dev_priv->sarea_map->handle + init->sarea_priv_offset);

	dev_priv->ring.Start = init->ring_start;
	dev_priv->ring.End = init->ring_end;
	dev_priv->ring.Size = init->ring_size;

	dev_priv->ring.map.offset = dev->agp->base + init->ring_start;
	dev_priv->ring.map.size = init->ring_size;
	dev_priv->ring.map.type = _DRM_AGP;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_legacy_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		dev->dev_private = (void *)dev_priv;
		i810_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;

	dev_priv->w = init->w;
	dev_priv->h = init->h;
	dev_priv->pitch = init->pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->front_offset = init->front_offset;

	dev_priv->overlay_offset = init->overlay_offset;
	dev_priv->overlay_physical = init->overlay_physical;

	dev_priv->front_di1 = init->front_offset | init->pitch_bits;
	dev_priv->back_di1 = init->back_offset | init->pitch_bits;
	dev_priv->zi1 = init->depth_offset | init->pitch_bits;

	/* Program Hardware Status Page */
	dev_priv->hw_status_page =
		pci_zalloc_consistent(dev->pdev, PAGE_SIZE,
				      &dev_priv->dma_status_page);
	if (!dev_priv->hw_status_page) {
		dev->dev_private = (void *)dev_priv;
		i810_dma_cleanup(dev);
		DRM_ERROR("Can not allocate hardware status page\n");
		return -ENOMEM;
	}
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	I810_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	/* Now we need to init our freelist */
	if (i810_freelist_init(dev, dev_priv) != 0) {
		dev->dev_private = (void *)dev_priv;
		i810_dma_cleanup(dev);
		DRM_ERROR("Not enough space in the status page for"
			  " the freelist\n");
		return -ENOMEM;
	}
	dev->dev_private = (void *)dev_priv;

	return 0;
}

static int i810_dma_init(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv;
	drm_i810_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case I810_INIT_DMA_1_4:
		DRM_INFO("Using v1.4 init.\n");
		dev_priv = kmalloc(sizeof(drm_i810_private_t), GFP_KERNEL);
		if (dev_priv == NULL)
			return -ENOMEM;
		retcode = i810_dma_initialize(dev, dev_priv, init);
		break;

	case I810_CLEANUP_DMA:
		DRM_INFO("DMA Cleanup\n");
		retcode = i810_dma_cleanup(dev);
		break;
	default:
		return -EINVAL;
	}

	return retcode;
}

/* Most efficient way to verify state for the i810 is as it is
 * emitted.  Non-conformant state is silently dropped.
 *
 * Use 'volatile' & local var tmp to force the emitted values to be
 * identical to the verified ones.
 */
static void i810EmitContextVerified(struct drm_device *dev,
				    volatile unsigned int *code)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING(I810_CTX_SETUP_SIZE);

	OUT_RING(GFX_OP_COLOR_FACTOR);
	OUT_RING(code[I810_CTXREG_CF1]);

	OUT_RING(GFX_OP_STIPPLE);
	OUT_RING(code[I810_CTXREG_ST1]);

	for (i = 4; i < I810_CTX_SETUP_SIZE; i++) {
		tmp = code[i];

		if ((tmp & (7 << 29)) == (3 << 29) &&
		    (tmp & (0x1f << 24)) < (0x1d << 24)) {
			OUT_RING(tmp);
			j++;
		} else
			printk("constext state dropped!!!\n");
	}

	if (j & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();
}

static void i810EmitTexVerified(struct drm_device *dev, volatile unsigned int *code)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING(I810_TEX_SETUP_SIZE);

	OUT_RING(GFX_OP_MAP_INFO);
	OUT_RING(code[I810_TEXREG_MI1]);
	OUT_RING(code[I810_TEXREG_MI2]);
	OUT_RING(code[I810_TEXREG_MI3]);

	for (i = 4; i < I810_TEX_SETUP_SIZE; i++) {
		tmp = code[i];

		if ((tmp & (7 << 29)) == (3 << 29) &&
		    (tmp & (0x1f << 24)) < (0x1d << 24)) {
			OUT_RING(tmp);
			j++;
		} else
			printk("texture state dropped!!!\n");
	}

	if (j & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();
}

/* Need to do some additional checking when setting the dest buffer.
 */
static void i810EmitDestVerified(struct drm_device *dev,
				 volatile unsigned int *code)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING(I810_DEST_SETUP_SIZE + 2);

	tmp = code[I810_DESTREG_DI1];
	if (tmp == dev_priv->front_di1 || tmp == dev_priv->back_di1) {
		OUT_RING(CMD_OP_DESTBUFFER_INFO);
		OUT_RING(tmp);
	} else
		DRM_DEBUG("bad di1 %x (allow %x or %x)\n",
			  tmp, dev_priv->front_di1, dev_priv->back_di1);

	/* invarient:
	 */
	OUT_RING(CMD_OP_Z_BUFFER_INFO);
	OUT_RING(dev_priv->zi1);

	OUT_RING(GFX_OP_DESTBUFFER_VARS);
	OUT_RING(code[I810_DESTREG_DV1]);

	OUT_RING(GFX_OP_DRAWRECT_INFO);
	OUT_RING(code[I810_DESTREG_DR1]);
	OUT_RING(code[I810_DESTREG_DR2]);
	OUT_RING(code[I810_DESTREG_DR3]);
	OUT_RING(code[I810_DESTREG_DR4]);
	OUT_RING(0);

	ADVANCE_LP_RING();
}

static void i810EmitState(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	DRM_DEBUG("%x\n", dirty);

	if (dirty & I810_UPLOAD_BUFFERS) {
		i810EmitDestVerified(dev, sarea_priv->BufferState);
		sarea_priv->dirty &= ~I810_UPLOAD_BUFFERS;
	}

	if (dirty & I810_UPLOAD_CTX) {
		i810EmitContextVerified(dev, sarea_priv->ContextState);
		sarea_priv->dirty &= ~I810_UPLOAD_CTX;
	}

	if (dirty & I810_UPLOAD_TEX0) {
		i810EmitTexVerified(dev, sarea_priv->TexState[0]);
		sarea_priv->dirty &= ~I810_UPLOAD_TEX0;
	}

	if (dirty & I810_UPLOAD_TEX1) {
		i810EmitTexVerified(dev, sarea_priv->TexState[1]);
		sarea_priv->dirty &= ~I810_UPLOAD_TEX1;
	}
}

/* need to verify
 */
static void i810_dma_dispatch_clear(struct drm_device *dev, int flags,
				    unsigned int clear_color,
				    unsigned int clear_zval)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int pitch = dev_priv->pitch;
	int cpp = 2;
	int i;
	RING_LOCALS;

	if (dev_priv->current_page == 1) {
		unsigned int tmp = flags;

		flags &= ~(I810_FRONT | I810_BACK);
		if (tmp & I810_FRONT)
			flags |= I810_BACK;
		if (tmp & I810_BACK)
			flags |= I810_FRONT;
	}

	i810_kernel_lost_context(dev);

	if (nbox > I810_NR_SAREA_CLIPRECTS)
		nbox = I810_NR_SAREA_CLIPRECTS;

	for (i = 0; i < nbox; i++, pbox++) {
		unsigned int x = pbox->x1;
		unsigned int y = pbox->y1;
		unsigned int width = (pbox->x2 - x) * cpp;
		unsigned int height = pbox->y2 - y;
		unsigned int start = y * pitch + x * cpp;

		if (pbox->x1 > pbox->x2 ||
		    pbox->y1 > pbox->y2 ||
		    pbox->x2 > dev_priv->w || pbox->y2 > dev_priv->h)
			continue;

		if (flags & I810_FRONT) {
			BEGIN_LP_RING(6);
			OUT_RING(BR00_BITBLT_CLIENT | BR00_OP_COLOR_BLT | 0x3);
			OUT_RING(BR13_SOLID_PATTERN | (0xF0 << 16) | pitch);
			OUT_RING((height << 16) | width);
			OUT_RING(start);
			OUT_RING(clear_color);
			OUT_RING(0);
			ADVANCE_LP_RING();
		}

		if (flags & I810_BACK) {
			BEGIN_LP_RING(6);
			OUT_RING(BR00_BITBLT_CLIENT | BR00_OP_COLOR_BLT | 0x3);
			OUT_RING(BR13_SOLID_PATTERN | (0xF0 << 16) | pitch);
			OUT_RING((height << 16) | width);
			OUT_RING(dev_priv->back_offset + start);
			OUT_RING(clear_color);
			OUT_RING(0);
			ADVANCE_LP_RING();
		}

		if (flags & I810_DEPTH) {
			BEGIN_LP_RING(6);
			OUT_RING(BR00_BITBLT_CLIENT | BR00_OP_COLOR_BLT | 0x3);
			OUT_RING(BR13_SOLID_PATTERN | (0xF0 << 16) | pitch);
			OUT_RING((height << 16) | width);
			OUT_RING(dev_priv->depth_offset + start);
			OUT_RING(clear_zval);
			OUT_RING(0);
			ADVANCE_LP_RING();
		}
	}
}

static void i810_dma_dispatch_swap(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int pitch = dev_priv->pitch;
	int cpp = 2;
	int i;
	RING_LOCALS;

	DRM_DEBUG("swapbuffers\n");

	i810_kernel_lost_context(dev);

	if (nbox > I810_NR_SAREA_CLIPRECTS)
		nbox = I810_NR_SAREA_CLIPRECTS;

	for (i = 0; i < nbox; i++, pbox++) {
		unsigned int w = pbox->x2 - pbox->x1;
		unsigned int h = pbox->y2 - pbox->y1;
		unsigned int dst = pbox->x1 * cpp + pbox->y1 * pitch;
		unsigned int start = dst;

		if (pbox->x1 > pbox->x2 ||
		    pbox->y1 > pbox->y2 ||
		    pbox->x2 > dev_priv->w || pbox->y2 > dev_priv->h)
			continue;

		BEGIN_LP_RING(6);
		OUT_RING(BR00_BITBLT_CLIENT | BR00_OP_SRC_COPY_BLT | 0x4);
		OUT_RING(pitch | (0xCC << 16));
		OUT_RING((h << 16) | (w * cpp));
		if (dev_priv->current_page == 0)
			OUT_RING(dev_priv->front_offset + start);
		else
			OUT_RING(dev_priv->back_offset + start);
		OUT_RING(pitch);
		if (dev_priv->current_page == 0)
			OUT_RING(dev_priv->back_offset + start);
		else
			OUT_RING(dev_priv->front_offset + start);
		ADVANCE_LP_RING();
	}
}

static void i810_dma_dispatch_vertex(struct drm_device *dev,
				     struct drm_buf *buf, int discard, int used)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_buf_priv_t *buf_priv = buf->dev_private;
	drm_i810_sarea_t *sarea_priv = dev_priv->sarea_priv;
	struct drm_clip_rect *box = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	unsigned long address = (unsigned long)buf->bus_address;
	unsigned long start = address - dev->agp->base;
	int i = 0;
	RING_LOCALS;

	i810_kernel_lost_context(dev);

	if (nbox > I810_NR_SAREA_CLIPRECTS)
		nbox = I810_NR_SAREA_CLIPRECTS;

	if (used > 4 * 1024)
		used = 0;

	if (sarea_priv->dirty)
		i810EmitState(dev);

	if (buf_priv->currently_mapped == I810_BUF_MAPPED) {
		unsigned int prim = (sarea_priv->vertex_prim & PR_MASK);

		*(u32 *) buf_priv->kernel_virtual =
		    ((GFX_OP_PRIMITIVE | prim | ((used / 4) - 2)));

		if (used & 4) {
			*(u32 *) ((char *) buf_priv->kernel_virtual + used) = 0;
			used += 4;
		}

		i810_unmap_buffer(buf);
	}

	if (used) {
		do {
			if (i < nbox) {
				BEGIN_LP_RING(4);
				OUT_RING(GFX_OP_SCISSOR | SC_UPDATE_SCISSOR |
					 SC_ENABLE);
				OUT_RING(GFX_OP_SCISSOR_INFO);
				OUT_RING(box[i].x1 | (box[i].y1 << 16));
				OUT_RING((box[i].x2 -
					  1) | ((box[i].y2 - 1) << 16));
				ADVANCE_LP_RING();
			}

			BEGIN_LP_RING(4);
			OUT_RING(CMD_OP_BATCH_BUFFER);
			OUT_RING(start | BB1_PROTECTED);
			OUT_RING(start + used - 4);
			OUT_RING(0);
			ADVANCE_LP_RING();

		} while (++i < nbox);
	}

	if (discard) {
		dev_priv->counter++;

		(void)cmpxchg(buf_priv->in_use, I810_BUF_CLIENT,
			      I810_BUF_HARDWARE);

		BEGIN_LP_RING(8);
		OUT_RING(CMD_STORE_DWORD_IDX);
		OUT_RING(20);
		OUT_RING(dev_priv->counter);
		OUT_RING(CMD_STORE_DWORD_IDX);
		OUT_RING(buf_priv->my_use_idx);
		OUT_RING(I810_BUF_FREE);
		OUT_RING(CMD_REPORT_HEAD);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}
}

static void i810_dma_dispatch_flip(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	int pitch = dev_priv->pitch;
	RING_LOCALS;

	DRM_DEBUG("page=%d pfCurrentPage=%d\n",
		  dev_priv->current_page,
		  dev_priv->sarea_priv->pf_current_page);

	i810_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(INST_PARSER_CLIENT | INST_OP_FLUSH | INST_FLUSH_MAP_CACHE);
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(I810_DEST_SETUP_SIZE + 2);
	/* On i815 at least ASYNC is buggy */
	/* pitch<<5 is from 11.2.8 p158,
	   its the pitch / 8 then left shifted 8,
	   so (pitch >> 3) << 8 */
	OUT_RING(CMD_OP_FRONTBUFFER_INFO | (pitch << 5) /*| ASYNC_FLIP */ );
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
	OUT_RING(CMD_OP_WAIT_FOR_EVENT | WAIT_FOR_PLANE_A_FLIP);
	OUT_RING(0);
	ADVANCE_LP_RING();

	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;

}

static void i810_dma_quiescent(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	i810_kernel_lost_context(dev);

	BEGIN_LP_RING(4);
	OUT_RING(INST_PARSER_CLIENT | INST_OP_FLUSH | INST_FLUSH_MAP_CACHE);
	OUT_RING(CMD_REPORT_HEAD);
	OUT_RING(0);
	OUT_RING(0);
	ADVANCE_LP_RING();

	i810_wait_ring(dev, dev_priv->ring.Size - 8);
}

static int i810_flush_queue(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	struct drm_device_dma *dma = dev->dma;
	int i, ret = 0;
	RING_LOCALS;

	i810_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(CMD_REPORT_HEAD);
	OUT_RING(0);
	ADVANCE_LP_RING();

	i810_wait_ring(dev, dev_priv->ring.Size - 8);

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i810_buf_priv_t *buf_priv = buf->dev_private;

		int used = cmpxchg(buf_priv->in_use, I810_BUF_HARDWARE,
				   I810_BUF_FREE);

		if (used == I810_BUF_HARDWARE)
			DRM_DEBUG("reclaimed from HARDWARE\n");
		if (used == I810_BUF_CLIENT)
			DRM_DEBUG("still on client\n");
	}

	return ret;
}

/* Must be called with the lock held */
void i810_driver_reclaim_buffers(struct drm_device *dev,
				 struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int i;

	if (!dma)
		return;
	if (!dev->dev_private)
		return;
	if (!dma->buflist)
		return;

	i810_flush_queue(dev);

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i810_buf_priv_t *buf_priv = buf->dev_private;

		if (buf->file_priv == file_priv && buf_priv) {
			int used = cmpxchg(buf_priv->in_use, I810_BUF_CLIENT,
					   I810_BUF_FREE);

			if (used == I810_BUF_CLIENT)
				DRM_DEBUG("reclaimed from client\n");
			if (buf_priv->currently_mapped == I810_BUF_MAPPED)
				buf_priv->currently_mapped = I810_BUF_UNMAPPED;
		}
	}
}

static int i810_flush_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	LOCK_TEST_WITH_RETURN(dev, file_priv);

	i810_flush_queue(dev);
	return 0;
}

static int i810_dma_vertex(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i810_sarea_t *sarea_priv = (drm_i810_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i810_vertex_t *vertex = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("idx %d used %d discard %d\n",
		  vertex->idx, vertex->used, vertex->discard);

	if (vertex->idx < 0 || vertex->idx >= dma->buf_count)
		return -EINVAL;

	i810_dma_dispatch_vertex(dev,
				 dma->buflist[vertex->idx],
				 vertex->discard, vertex->used);

	sarea_priv->last_enqueue = dev_priv->counter - 1;
	sarea_priv->last_dispatch = (int)hw_status[5];

	return 0;
}

static int i810_clear_bufs(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	drm_i810_clear_t *clear = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* GH: Someone's doing nasty things... */
	if (!dev->dev_private)
		return -EINVAL;

	i810_dma_dispatch_clear(dev, clear->flags,
				clear->clear_color, clear->clear_depth);
	return 0;
}

static int i810_swap_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	i810_dma_dispatch_swap(dev);
	return 0;
}

static int i810_getage(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i810_sarea_t *sarea_priv = (drm_i810_sarea_t *)
	    dev_priv->sarea_priv;

	sarea_priv->last_dispatch = (int)hw_status[5];
	return 0;
}

static int i810_getbuf(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	int retcode = 0;
	drm_i810_dma_t *d = data;
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i810_sarea_t *sarea_priv = (drm_i810_sarea_t *)
	    dev_priv->sarea_priv;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	d->granted = 0;

	retcode = i810_dma_get_buffer(dev, d, file_priv);

	DRM_DEBUG("i810_dma: %d returning %d, granted = %d\n",
		  task_pid_nr(current), retcode, d->granted);

	sarea_priv->last_dispatch = (int)hw_status[5];

	return retcode;
}

static int i810_copybuf(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	/* Never copy - 2.4.x doesn't need it */
	return 0;
}

static int i810_docopy(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	/* Never copy - 2.4.x doesn't need it */
	return 0;
}

static void i810_dma_dispatch_mc(struct drm_device *dev, struct drm_buf *buf, int used,
				 unsigned int last_render)
{
	drm_i810_private_t *dev_priv = dev->dev_private;
	drm_i810_buf_priv_t *buf_priv = buf->dev_private;
	drm_i810_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned long address = (unsigned long)buf->bus_address;
	unsigned long start = address - dev->agp->base;
	int u;
	RING_LOCALS;

	i810_kernel_lost_context(dev);

	u = cmpxchg(buf_priv->in_use, I810_BUF_CLIENT, I810_BUF_HARDWARE);
	if (u != I810_BUF_CLIENT)
		DRM_DEBUG("MC found buffer that isn't mine!\n");

	if (used > 4 * 1024)
		used = 0;

	sarea_priv->dirty = 0x7f;

	DRM_DEBUG("addr 0x%lx, used 0x%x\n", address, used);

	dev_priv->counter++;
	DRM_DEBUG("dispatch counter : %ld\n", dev_priv->counter);
	DRM_DEBUG("start : %lx\n", start);
	DRM_DEBUG("used : %d\n", used);
	DRM_DEBUG("start + used - 4 : %ld\n", start + used - 4);

	if (buf_priv->currently_mapped == I810_BUF_MAPPED) {
		if (used & 4) {
			*(u32 *) ((char *) buf_priv->virtual + used) = 0;
			used += 4;
		}

		i810_unmap_buffer(buf);
	}
	BEGIN_LP_RING(4);
	OUT_RING(CMD_OP_BATCH_BUFFER);
	OUT_RING(start | BB1_PROTECTED);
	OUT_RING(start + used - 4);
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(8);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(buf_priv->my_use_idx);
	OUT_RING(I810_BUF_FREE);
	OUT_RING(0);

	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(16);
	OUT_RING(last_render);
	OUT_RING(0);
	ADVANCE_LP_RING();
}

static int i810_dma_mc(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i810_sarea_t *sarea_priv = (drm_i810_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i810_mc_t *mc = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (mc->idx >= dma->buf_count || mc->idx < 0)
		return -EINVAL;

	i810_dma_dispatch_mc(dev, dma->buflist[mc->idx], mc->used,
			     mc->last_render);

	sarea_priv->last_enqueue = dev_priv->counter - 1;
	sarea_priv->last_dispatch = (int)hw_status[5];

	return 0;
}

static int i810_rstatus(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;

	return (int)(((u32 *) (dev_priv->hw_status_page))[4]);
}

static int i810_ov0_info(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;
	drm_i810_overlay_t *ov = data;

	ov->offset = dev_priv->overlay_offset;
	ov->physical = dev_priv->overlay_physical;

	return 0;
}

static int i810_fstatus(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;

	LOCK_TEST_WITH_RETURN(dev, file_priv);
	return I810_READ(0x30008);
}

static int i810_ov0_flip(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* Tell the overlay to update */
	I810_WRITE(0x30000, dev_priv->overlay_physical | 0x80000000);

	return 0;
}

/* Not sure why this isn't set all the time:
 */
static void i810_do_init_pageflip(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");
	dev_priv->page_flipping = 1;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;
}

static int i810_do_cleanup_pageflip(struct drm_device *dev)
{
	drm_i810_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");
	if (dev_priv->current_page != 0)
		i810_dma_dispatch_flip(dev);

	dev_priv->page_flipping = 0;
	return 0;
}

static int i810_flip_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	drm_i810_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv->page_flipping)
		i810_do_init_pageflip(dev);

	i810_dma_dispatch_flip(dev);
	return 0;
}

int i810_driver_load(struct drm_device *dev, unsigned long flags)
{
	dev->agp = drm_agp_init(dev);
	if (dev->agp) {
		dev->agp->agp_mtrr = arch_phys_wc_add(
			dev->agp->agp_info.aper_base,
			dev->agp->agp_info.aper_size *
			1024 * 1024);
	}

	/* Our userspace depends upon the agp mapping support. */
	if (!dev->agp)
		return -EINVAL;

	pci_set_master(dev->pdev);

	return 0;
}

void i810_driver_lastclose(struct drm_device *dev)
{
	i810_dma_cleanup(dev);
}

void i810_driver_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
	if (dev->dev_private) {
		drm_i810_private_t *dev_priv = dev->dev_private;
		if (dev_priv->page_flipping)
			i810_do_cleanup_pageflip(dev);
	}

	if (file_priv->master && file_priv->master->lock.hw_lock) {
		drm_legacy_idlelock_take(&file_priv->master->lock);
		i810_driver_reclaim_buffers(dev, file_priv);
		drm_legacy_idlelock_release(&file_priv->master->lock);
	} else {
		/* master disappeared, clean up stuff anyway and hope nothing
		 * goes wrong */
		i810_driver_reclaim_buffers(dev, file_priv);
	}

}

int i810_driver_dma_quiescent(struct drm_device *dev)
{
	i810_dma_quiescent(dev);
	return 0;
}

const struct drm_ioctl_desc i810_ioctls[] = {
	DRM_IOCTL_DEF_DRV(I810_INIT, i810_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_VERTEX, i810_dma_vertex, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_CLEAR, i810_clear_bufs, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_FLUSH, i810_flush_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_GETAGE, i810_getage, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_GETBUF, i810_getbuf, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_SWAP, i810_swap_bufs, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_COPY, i810_copybuf, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_DOCOPY, i810_docopy, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_OV0INFO, i810_ov0_info, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_FSTATUS, i810_fstatus, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_OV0FLIP, i810_ov0_flip, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_MC, i810_dma_mc, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_RSTATUS, i810_rstatus, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I810_FLIP, i810_flip_bufs, DRM_AUTH|DRM_UNLOCKED),
};

int i810_max_ioctl = ARRAY_SIZE(i810_ioctls);
