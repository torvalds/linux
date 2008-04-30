/* i830_dma.c -- DMA support for the I830 -*- linux-c -*-
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
 *	    Keith Whitwell <keith@tungstengraphics.com>
 *	    Abraham vd Merwe <abraham@2d3d.co.za>
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i830_drm.h"
#include "i830_drv.h"
#include <linux/interrupt.h>	/* For task queue support */
#include <linux/pagemap.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#define I830_BUF_FREE		2
#define I830_BUF_CLIENT		1
#define I830_BUF_HARDWARE	0

#define I830_BUF_UNMAPPED 0
#define I830_BUF_MAPPED   1

static struct drm_buf *i830_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	int i;
	int used;

	/* Linear search might not be the best solution */

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i830_buf_priv_t *buf_priv = buf->dev_private;
		/* In use is already a pointer */
		used = cmpxchg(buf_priv->in_use, I830_BUF_FREE,
			       I830_BUF_CLIENT);
		if (used == I830_BUF_FREE) {
			return buf;
		}
	}
	return NULL;
}

/* This should only be called if the buffer is not sent to the hardware
 * yet, the hardware updates in use for us once its on the ring buffer.
 */

static int i830_freelist_put(struct drm_device * dev, struct drm_buf * buf)
{
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	int used;

	/* In use is already a pointer */
	used = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT, I830_BUF_FREE);
	if (used != I830_BUF_CLIENT) {
		DRM_ERROR("Freeing buffer thats not in use : %d\n", buf->idx);
		return -EINVAL;
	}

	return 0;
}

static int i830_mmap_buffers(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev;
	drm_i830_private_t *dev_priv;
	struct drm_buf *buf;
	drm_i830_buf_priv_t *buf_priv;

	lock_kernel();
	dev = priv->minor->dev;
	dev_priv = dev->dev_private;
	buf = dev_priv->mmap_buffer;
	buf_priv = buf->dev_private;

	vma->vm_flags |= (VM_IO | VM_DONTCOPY);
	vma->vm_file = filp;

	buf_priv->currently_mapped = I830_BUF_MAPPED;
	unlock_kernel();

	if (io_remap_pfn_range(vma, vma->vm_start,
			       vma->vm_pgoff,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static const struct file_operations i830_buffer_fops = {
	.open = drm_open,
	.release = drm_release,
	.ioctl = drm_ioctl,
	.mmap = i830_mmap_buffers,
	.fasync = drm_fasync,
};

static int i830_map_buffer(struct drm_buf * buf, struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	drm_i830_private_t *dev_priv = dev->dev_private;
	const struct file_operations *old_fops;
	unsigned long virtual;
	int retcode = 0;

	if (buf_priv->currently_mapped == I830_BUF_MAPPED)
		return -EINVAL;

	down_write(&current->mm->mmap_sem);
	old_fops = file_priv->filp->f_op;
	file_priv->filp->f_op = &i830_buffer_fops;
	dev_priv->mmap_buffer = buf;
	virtual = do_mmap(file_priv->filp, 0, buf->total, PROT_READ | PROT_WRITE,
			  MAP_SHARED, buf->bus_address);
	dev_priv->mmap_buffer = NULL;
	file_priv->filp->f_op = old_fops;
	if (IS_ERR((void *)virtual)) {	/* ugh */
		/* Real error */
		DRM_ERROR("mmap error\n");
		retcode = PTR_ERR((void *)virtual);
		buf_priv->virtual = NULL;
	} else {
		buf_priv->virtual = (void __user *)virtual;
	}
	up_write(&current->mm->mmap_sem);

	return retcode;
}

static int i830_unmap_buffer(struct drm_buf * buf)
{
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	int retcode = 0;

	if (buf_priv->currently_mapped != I830_BUF_MAPPED)
		return -EINVAL;

	down_write(&current->mm->mmap_sem);
	retcode = do_munmap(current->mm,
			    (unsigned long)buf_priv->virtual,
			    (size_t) buf->total);
	up_write(&current->mm->mmap_sem);

	buf_priv->currently_mapped = I830_BUF_UNMAPPED;
	buf_priv->virtual = NULL;

	return retcode;
}

static int i830_dma_get_buffer(struct drm_device * dev, drm_i830_dma_t * d,
			       struct drm_file *file_priv)
{
	struct drm_buf *buf;
	drm_i830_buf_priv_t *buf_priv;
	int retcode = 0;

	buf = i830_freelist_get(dev);
	if (!buf) {
		retcode = -ENOMEM;
		DRM_DEBUG("retcode=%d\n", retcode);
		return retcode;
	}

	retcode = i830_map_buffer(buf, file_priv);
	if (retcode) {
		i830_freelist_put(dev, buf);
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

static int i830_dma_cleanup(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	if (dev->dev_private) {
		int i;
		drm_i830_private_t *dev_priv =
		    (drm_i830_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start) {
			drm_core_ioremapfree(&dev_priv->ring.map, dev);
		}
		if (dev_priv->hw_status_page) {
			pci_free_consistent(dev->pdev, PAGE_SIZE,
					    dev_priv->hw_status_page,
					    dev_priv->dma_status_page);
			/* Need to rewrite hardware status page */
			I830_WRITE(0x02080, 0x1ffff000);
		}

		drm_free(dev->dev_private, sizeof(drm_i830_private_t),
			 DRM_MEM_DRIVER);
		dev->dev_private = NULL;

		for (i = 0; i < dma->buf_count; i++) {
			struct drm_buf *buf = dma->buflist[i];
			drm_i830_buf_priv_t *buf_priv = buf->dev_private;
			if (buf_priv->kernel_virtual && buf->total)
				drm_core_ioremapfree(&buf_priv->map, dev);
		}
	}
	return 0;
}

int i830_wait_ring(struct drm_device * dev, int n, const char *caller)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_ring_buffer_t *ring = &(dev_priv->ring);
	int iters = 0;
	unsigned long end;
	unsigned int last_head = I830_READ(LP_RING + RING_HEAD) & HEAD_ADDR;

	end = jiffies + (HZ * 3);
	while (ring->space < n) {
		ring->head = I830_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
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
		dev_priv->sarea_priv->perf_boxes |= I830_BOX_WAIT;
	}

      out_wait_ring:
	return iters;
}

static void i830_kernel_lost_context(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_ring_buffer_t *ring = &(dev_priv->ring);

	ring->head = I830_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	ring->tail = I830_READ(LP_RING + RING_TAIL) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;

	if (ring->head == ring->tail)
		dev_priv->sarea_priv->perf_boxes |= I830_BOX_RING_EMPTY;
}

static int i830_freelist_init(struct drm_device * dev, drm_i830_private_t * dev_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int my_idx = 36;
	u32 *hw_status = (u32 *) (dev_priv->hw_status_page + my_idx);
	int i;

	if (dma->buf_count > 1019) {
		/* Not enough space in the status page for the freelist */
		return -EINVAL;
	}

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i830_buf_priv_t *buf_priv = buf->dev_private;

		buf_priv->in_use = hw_status++;
		buf_priv->my_use_idx = my_idx;
		my_idx += 4;

		*buf_priv->in_use = I830_BUF_FREE;

		buf_priv->map.offset = buf->bus_address;
		buf_priv->map.size = buf->total;
		buf_priv->map.type = _DRM_AGP;
		buf_priv->map.flags = 0;
		buf_priv->map.mtrr = 0;

		drm_core_ioremap(&buf_priv->map, dev);
		buf_priv->kernel_virtual = buf_priv->map.handle;
	}
	return 0;
}

static int i830_dma_initialize(struct drm_device * dev,
			       drm_i830_private_t * dev_priv,
			       drm_i830_init_t * init)
{
	struct drm_map_list *r_list;

	memset(dev_priv, 0, sizeof(drm_i830_private_t));

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
		i830_dma_cleanup(dev);
		DRM_ERROR("can not find sarea!\n");
		return -EINVAL;
	}
	dev_priv->mmio_map = drm_core_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio_map) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("can not find mmio map!\n");
		return -EINVAL;
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
	if (!dev->agp_buffer_map) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("can not find dma buffer map!\n");
		return -EINVAL;
	}

	dev_priv->sarea_priv = (drm_i830_sarea_t *)
	    ((u8 *) dev_priv->sarea_map->handle + init->sarea_priv_offset);

	dev_priv->ring.Start = init->ring_start;
	dev_priv->ring.End = init->ring_end;
	dev_priv->ring.Size = init->ring_size;

	dev_priv->ring.map.offset = dev->agp->base + init->ring_start;
	dev_priv->ring.map.size = init->ring_size;
	dev_priv->ring.map.type = _DRM_AGP;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_core_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
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

	dev_priv->front_di1 = init->front_offset | init->pitch_bits;
	dev_priv->back_di1 = init->back_offset | init->pitch_bits;
	dev_priv->zi1 = init->depth_offset | init->pitch_bits;

	DRM_DEBUG("front_di1 %x\n", dev_priv->front_di1);
	DRM_DEBUG("back_offset %x\n", dev_priv->back_offset);
	DRM_DEBUG("back_di1 %x\n", dev_priv->back_di1);
	DRM_DEBUG("pitch_bits %x\n", init->pitch_bits);

	dev_priv->cpp = init->cpp;
	/* We are using separate values as placeholders for mechanisms for
	 * private backbuffer/depthbuffer usage.
	 */

	dev_priv->back_pitch = init->back_pitch;
	dev_priv->depth_pitch = init->depth_pitch;
	dev_priv->do_boxes = 0;
	dev_priv->use_mi_batchbuffer_start = 0;

	/* Program Hardware Status Page */
	dev_priv->hw_status_page =
	    pci_alloc_consistent(dev->pdev, PAGE_SIZE,
				 &dev_priv->dma_status_page);
	if (!dev_priv->hw_status_page) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("Can not allocate hardware status page\n");
		return -ENOMEM;
	}
	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	I830_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	/* Now we need to init our freelist */
	if (i830_freelist_init(dev, dev_priv) != 0) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("Not enough space in the status page for"
			  " the freelist\n");
		return -ENOMEM;
	}
	dev->dev_private = (void *)dev_priv;

	return 0;
}

static int i830_dma_init(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i830_private_t *dev_priv;
	drm_i830_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case I830_INIT_DMA:
		dev_priv = drm_alloc(sizeof(drm_i830_private_t),
				     DRM_MEM_DRIVER);
		if (dev_priv == NULL)
			return -ENOMEM;
		retcode = i830_dma_initialize(dev, dev_priv, init);
		break;
	case I830_CLEANUP_DMA:
		retcode = i830_dma_cleanup(dev);
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

#define GFX_OP_STIPPLE           ((0x3<<29)|(0x1d<<24)|(0x83<<16))
#define ST1_ENABLE               (1<<16)
#define ST1_MASK                 (0xffff)

/* Most efficient way to verify state for the i830 is as it is
 * emitted.  Non-conformant state is silently dropped.
 */
static void i830EmitContextVerified(struct drm_device * dev, unsigned int *code)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING(I830_CTX_SETUP_SIZE + 4);

	for (i = 0; i < I830_CTXREG_BLENDCOLR0; i++) {
		tmp = code[i];
		if ((tmp & (7 << 29)) == CMD_3D &&
		    (tmp & (0x1f << 24)) < (0x1d << 24)) {
			OUT_RING(tmp);
			j++;
		} else {
			DRM_ERROR("Skipping %d\n", i);
		}
	}

	OUT_RING(STATE3D_CONST_BLEND_COLOR_CMD);
	OUT_RING(code[I830_CTXREG_BLENDCOLR]);
	j += 2;

	for (i = I830_CTXREG_VF; i < I830_CTXREG_MCSB0; i++) {
		tmp = code[i];
		if ((tmp & (7 << 29)) == CMD_3D &&
		    (tmp & (0x1f << 24)) < (0x1d << 24)) {
			OUT_RING(tmp);
			j++;
		} else {
			DRM_ERROR("Skipping %d\n", i);
		}
	}

	OUT_RING(STATE3D_MAP_COORD_SETBIND_CMD);
	OUT_RING(code[I830_CTXREG_MCSB1]);
	j += 2;

	if (j & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();
}

static void i830EmitTexVerified(struct drm_device * dev, unsigned int *code)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	if (code[I830_TEXREG_MI0] == GFX_OP_MAP_INFO ||
	    (code[I830_TEXREG_MI0] & ~(0xf * LOAD_TEXTURE_MAP0)) ==
	    (STATE3D_LOAD_STATE_IMMEDIATE_2 | 4)) {

		BEGIN_LP_RING(I830_TEX_SETUP_SIZE);

		OUT_RING(code[I830_TEXREG_MI0]);	/* TM0LI */
		OUT_RING(code[I830_TEXREG_MI1]);	/* TM0S0 */
		OUT_RING(code[I830_TEXREG_MI2]);	/* TM0S1 */
		OUT_RING(code[I830_TEXREG_MI3]);	/* TM0S2 */
		OUT_RING(code[I830_TEXREG_MI4]);	/* TM0S3 */
		OUT_RING(code[I830_TEXREG_MI5]);	/* TM0S4 */

		for (i = 6; i < I830_TEX_SETUP_SIZE; i++) {
			tmp = code[i];
			OUT_RING(tmp);
			j++;
		}

		if (j & 1)
			OUT_RING(0);

		ADVANCE_LP_RING();
	} else
		printk("rejected packet %x\n", code[0]);
}

static void i830EmitTexBlendVerified(struct drm_device * dev,
				     unsigned int *code, unsigned int num)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	if (!num)
		return;

	BEGIN_LP_RING(num + 1);

	for (i = 0; i < num; i++) {
		tmp = code[i];
		OUT_RING(tmp);
		j++;
	}

	if (j & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();
}

static void i830EmitTexPalette(struct drm_device * dev,
			       unsigned int *palette, int number, int is_shared)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	return;

	BEGIN_LP_RING(258);

	if (is_shared == 1) {
		OUT_RING(CMD_OP_MAP_PALETTE_LOAD |
			 MAP_PALETTE_NUM(0) | MAP_PALETTE_BOTH);
	} else {
		OUT_RING(CMD_OP_MAP_PALETTE_LOAD | MAP_PALETTE_NUM(number));
	}
	for (i = 0; i < 256; i++) {
		OUT_RING(palette[i]);
	}
	OUT_RING(0);
	/* KW:  WHERE IS THE ADVANCE_LP_RING?  This is effectively a noop!
	 */
}

/* Need to do some additional checking when setting the dest buffer.
 */
static void i830EmitDestVerified(struct drm_device * dev, unsigned int *code)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING(I830_DEST_SETUP_SIZE + 10);

	tmp = code[I830_DESTREG_CBUFADDR];
	if (tmp == dev_priv->front_di1 || tmp == dev_priv->back_di1) {
		if (((int)outring) & 8) {
			OUT_RING(0);
			OUT_RING(0);
		}

		OUT_RING(CMD_OP_DESTBUFFER_INFO);
		OUT_RING(BUF_3D_ID_COLOR_BACK |
			 BUF_3D_PITCH(dev_priv->back_pitch * dev_priv->cpp) |
			 BUF_3D_USE_FENCE);
		OUT_RING(tmp);
		OUT_RING(0);

		OUT_RING(CMD_OP_DESTBUFFER_INFO);
		OUT_RING(BUF_3D_ID_DEPTH | BUF_3D_USE_FENCE |
			 BUF_3D_PITCH(dev_priv->depth_pitch * dev_priv->cpp));
		OUT_RING(dev_priv->zi1);
		OUT_RING(0);
	} else {
		DRM_ERROR("bad di1 %x (allow %x or %x)\n",
			  tmp, dev_priv->front_di1, dev_priv->back_di1);
	}

	/* invarient:
	 */

	OUT_RING(GFX_OP_DESTBUFFER_VARS);
	OUT_RING(code[I830_DESTREG_DV1]);

	OUT_RING(GFX_OP_DRAWRECT_INFO);
	OUT_RING(code[I830_DESTREG_DR1]);
	OUT_RING(code[I830_DESTREG_DR2]);
	OUT_RING(code[I830_DESTREG_DR3]);
	OUT_RING(code[I830_DESTREG_DR4]);

	/* Need to verify this */
	tmp = code[I830_DESTREG_SENABLE];
	if ((tmp & ~0x3) == GFX_OP_SCISSOR_ENABLE) {
		OUT_RING(tmp);
	} else {
		DRM_ERROR("bad scissor enable\n");
		OUT_RING(0);
	}

	OUT_RING(GFX_OP_SCISSOR_RECT);
	OUT_RING(code[I830_DESTREG_SR1]);
	OUT_RING(code[I830_DESTREG_SR2]);
	OUT_RING(0);

	ADVANCE_LP_RING();
}

static void i830EmitStippleVerified(struct drm_device * dev, unsigned int *code)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	BEGIN_LP_RING(2);
	OUT_RING(GFX_OP_STIPPLE);
	OUT_RING(code[1]);
	ADVANCE_LP_RING();
}

static void i830EmitState(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	DRM_DEBUG("%s %x\n", __FUNCTION__, dirty);

	if (dirty & I830_UPLOAD_BUFFERS) {
		i830EmitDestVerified(dev, sarea_priv->BufferState);
		sarea_priv->dirty &= ~I830_UPLOAD_BUFFERS;
	}

	if (dirty & I830_UPLOAD_CTX) {
		i830EmitContextVerified(dev, sarea_priv->ContextState);
		sarea_priv->dirty &= ~I830_UPLOAD_CTX;
	}

	if (dirty & I830_UPLOAD_TEX0) {
		i830EmitTexVerified(dev, sarea_priv->TexState[0]);
		sarea_priv->dirty &= ~I830_UPLOAD_TEX0;
	}

	if (dirty & I830_UPLOAD_TEX1) {
		i830EmitTexVerified(dev, sarea_priv->TexState[1]);
		sarea_priv->dirty &= ~I830_UPLOAD_TEX1;
	}

	if (dirty & I830_UPLOAD_TEXBLEND0) {
		i830EmitTexBlendVerified(dev, sarea_priv->TexBlendState[0],
					 sarea_priv->TexBlendStateWordsUsed[0]);
		sarea_priv->dirty &= ~I830_UPLOAD_TEXBLEND0;
	}

	if (dirty & I830_UPLOAD_TEXBLEND1) {
		i830EmitTexBlendVerified(dev, sarea_priv->TexBlendState[1],
					 sarea_priv->TexBlendStateWordsUsed[1]);
		sarea_priv->dirty &= ~I830_UPLOAD_TEXBLEND1;
	}

	if (dirty & I830_UPLOAD_TEX_PALETTE_SHARED) {
		i830EmitTexPalette(dev, sarea_priv->Palette[0], 0, 1);
	} else {
		if (dirty & I830_UPLOAD_TEX_PALETTE_N(0)) {
			i830EmitTexPalette(dev, sarea_priv->Palette[0], 0, 0);
			sarea_priv->dirty &= ~I830_UPLOAD_TEX_PALETTE_N(0);
		}
		if (dirty & I830_UPLOAD_TEX_PALETTE_N(1)) {
			i830EmitTexPalette(dev, sarea_priv->Palette[1], 1, 0);
			sarea_priv->dirty &= ~I830_UPLOAD_TEX_PALETTE_N(1);
		}

		/* 1.3:
		 */
#if 0
		if (dirty & I830_UPLOAD_TEX_PALETTE_N(2)) {
			i830EmitTexPalette(dev, sarea_priv->Palette2[0], 0, 0);
			sarea_priv->dirty &= ~I830_UPLOAD_TEX_PALETTE_N(2);
		}
		if (dirty & I830_UPLOAD_TEX_PALETTE_N(3)) {
			i830EmitTexPalette(dev, sarea_priv->Palette2[1], 1, 0);
			sarea_priv->dirty &= ~I830_UPLOAD_TEX_PALETTE_N(2);
		}
#endif
	}

	/* 1.3:
	 */
	if (dirty & I830_UPLOAD_STIPPLE) {
		i830EmitStippleVerified(dev, sarea_priv->StippleState);
		sarea_priv->dirty &= ~I830_UPLOAD_STIPPLE;
	}

	if (dirty & I830_UPLOAD_TEX2) {
		i830EmitTexVerified(dev, sarea_priv->TexState2);
		sarea_priv->dirty &= ~I830_UPLOAD_TEX2;
	}

	if (dirty & I830_UPLOAD_TEX3) {
		i830EmitTexVerified(dev, sarea_priv->TexState3);
		sarea_priv->dirty &= ~I830_UPLOAD_TEX3;
	}

	if (dirty & I830_UPLOAD_TEXBLEND2) {
		i830EmitTexBlendVerified(dev,
					 sarea_priv->TexBlendState2,
					 sarea_priv->TexBlendStateWordsUsed2);

		sarea_priv->dirty &= ~I830_UPLOAD_TEXBLEND2;
	}

	if (dirty & I830_UPLOAD_TEXBLEND3) {
		i830EmitTexBlendVerified(dev,
					 sarea_priv->TexBlendState3,
					 sarea_priv->TexBlendStateWordsUsed3);
		sarea_priv->dirty &= ~I830_UPLOAD_TEXBLEND3;
	}
}

/* ================================================================
 * Performance monitoring functions
 */

static void i830_fill_box(struct drm_device * dev,
			  int x, int y, int w, int h, int r, int g, int b)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	u32 color;
	unsigned int BR13, CMD;
	RING_LOCALS;

	BR13 = (0xF0 << 16) | (dev_priv->pitch * dev_priv->cpp) | (1 << 24);
	CMD = XY_COLOR_BLT_CMD;
	x += dev_priv->sarea_priv->boxes[0].x1;
	y += dev_priv->sarea_priv->boxes[0].y1;

	if (dev_priv->cpp == 4) {
		BR13 |= (1 << 25);
		CMD |= (XY_COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB);
		color = (((0xff) << 24) | (r << 16) | (g << 8) | b);
	} else {
		color = (((r & 0xf8) << 8) |
			 ((g & 0xfc) << 3) | ((b & 0xf8) >> 3));
	}

	BEGIN_LP_RING(6);
	OUT_RING(CMD);
	OUT_RING(BR13);
	OUT_RING((y << 16) | x);
	OUT_RING(((y + h) << 16) | (x + w));

	if (dev_priv->current_page == 1) {
		OUT_RING(dev_priv->front_offset);
	} else {
		OUT_RING(dev_priv->back_offset);
	}

	OUT_RING(color);
	ADVANCE_LP_RING();
}

static void i830_cp_performance_boxes(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;

	/* Purple box for page flipping
	 */
	if (dev_priv->sarea_priv->perf_boxes & I830_BOX_FLIP)
		i830_fill_box(dev, 4, 4, 8, 8, 255, 0, 255);

	/* Red box if we have to wait for idle at any point
	 */
	if (dev_priv->sarea_priv->perf_boxes & I830_BOX_WAIT)
		i830_fill_box(dev, 16, 4, 8, 8, 255, 0, 0);

	/* Blue box: lost context?
	 */
	if (dev_priv->sarea_priv->perf_boxes & I830_BOX_LOST_CONTEXT)
		i830_fill_box(dev, 28, 4, 8, 8, 0, 0, 255);

	/* Yellow box for texture swaps
	 */
	if (dev_priv->sarea_priv->perf_boxes & I830_BOX_TEXTURE_LOAD)
		i830_fill_box(dev, 40, 4, 8, 8, 255, 255, 0);

	/* Green box if hardware never idles (as far as we can tell)
	 */
	if (!(dev_priv->sarea_priv->perf_boxes & I830_BOX_RING_EMPTY))
		i830_fill_box(dev, 64, 4, 8, 8, 0, 255, 0);

	/* Draw bars indicating number of buffers allocated
	 * (not a great measure, easily confused)
	 */
	if (dev_priv->dma_used) {
		int bar = dev_priv->dma_used / 10240;
		if (bar > 100)
			bar = 100;
		if (bar < 1)
			bar = 1;
		i830_fill_box(dev, 4, 16, bar, 4, 196, 128, 128);
		dev_priv->dma_used = 0;
	}

	dev_priv->sarea_priv->perf_boxes = 0;
}

static void i830_dma_dispatch_clear(struct drm_device * dev, int flags,
				    unsigned int clear_color,
				    unsigned int clear_zval,
				    unsigned int clear_depthmask)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int pitch = dev_priv->pitch;
	int cpp = dev_priv->cpp;
	int i;
	unsigned int BR13, CMD, D_CMD;
	RING_LOCALS;

	if (dev_priv->current_page == 1) {
		unsigned int tmp = flags;

		flags &= ~(I830_FRONT | I830_BACK);
		if (tmp & I830_FRONT)
			flags |= I830_BACK;
		if (tmp & I830_BACK)
			flags |= I830_FRONT;
	}

	i830_kernel_lost_context(dev);

	switch (cpp) {
	case 2:
		BR13 = (0xF0 << 16) | (pitch * cpp) | (1 << 24);
		D_CMD = CMD = XY_COLOR_BLT_CMD;
		break;
	case 4:
		BR13 = (0xF0 << 16) | (pitch * cpp) | (1 << 24) | (1 << 25);
		CMD = (XY_COLOR_BLT_CMD | XY_COLOR_BLT_WRITE_ALPHA |
		       XY_COLOR_BLT_WRITE_RGB);
		D_CMD = XY_COLOR_BLT_CMD;
		if (clear_depthmask & 0x00ffffff)
			D_CMD |= XY_COLOR_BLT_WRITE_RGB;
		if (clear_depthmask & 0xff000000)
			D_CMD |= XY_COLOR_BLT_WRITE_ALPHA;
		break;
	default:
		BR13 = (0xF0 << 16) | (pitch * cpp) | (1 << 24);
		D_CMD = CMD = XY_COLOR_BLT_CMD;
		break;
	}

	if (nbox > I830_NR_SAREA_CLIPRECTS)
		nbox = I830_NR_SAREA_CLIPRECTS;

	for (i = 0; i < nbox; i++, pbox++) {
		if (pbox->x1 > pbox->x2 ||
		    pbox->y1 > pbox->y2 ||
		    pbox->x2 > dev_priv->w || pbox->y2 > dev_priv->h)
			continue;

		if (flags & I830_FRONT) {
			DRM_DEBUG("clear front\n");
			BEGIN_LP_RING(6);
			OUT_RING(CMD);
			OUT_RING(BR13);
			OUT_RING((pbox->y1 << 16) | pbox->x1);
			OUT_RING((pbox->y2 << 16) | pbox->x2);
			OUT_RING(dev_priv->front_offset);
			OUT_RING(clear_color);
			ADVANCE_LP_RING();
		}

		if (flags & I830_BACK) {
			DRM_DEBUG("clear back\n");
			BEGIN_LP_RING(6);
			OUT_RING(CMD);
			OUT_RING(BR13);
			OUT_RING((pbox->y1 << 16) | pbox->x1);
			OUT_RING((pbox->y2 << 16) | pbox->x2);
			OUT_RING(dev_priv->back_offset);
			OUT_RING(clear_color);
			ADVANCE_LP_RING();
		}

		if (flags & I830_DEPTH) {
			DRM_DEBUG("clear depth\n");
			BEGIN_LP_RING(6);
			OUT_RING(D_CMD);
			OUT_RING(BR13);
			OUT_RING((pbox->y1 << 16) | pbox->x1);
			OUT_RING((pbox->y2 << 16) | pbox->x2);
			OUT_RING(dev_priv->depth_offset);
			OUT_RING(clear_zval);
			ADVANCE_LP_RING();
		}
	}
}

static void i830_dma_dispatch_swap(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int pitch = dev_priv->pitch;
	int cpp = dev_priv->cpp;
	int i;
	unsigned int CMD, BR13;
	RING_LOCALS;

	DRM_DEBUG("swapbuffers\n");

	i830_kernel_lost_context(dev);

	if (dev_priv->do_boxes)
		i830_cp_performance_boxes(dev);

	switch (cpp) {
	case 2:
		BR13 = (pitch * cpp) | (0xCC << 16) | (1 << 24);
		CMD = XY_SRC_COPY_BLT_CMD;
		break;
	case 4:
		BR13 = (pitch * cpp) | (0xCC << 16) | (1 << 24) | (1 << 25);
		CMD = (XY_SRC_COPY_BLT_CMD | XY_SRC_COPY_BLT_WRITE_ALPHA |
		       XY_SRC_COPY_BLT_WRITE_RGB);
		break;
	default:
		BR13 = (pitch * cpp) | (0xCC << 16) | (1 << 24);
		CMD = XY_SRC_COPY_BLT_CMD;
		break;
	}

	if (nbox > I830_NR_SAREA_CLIPRECTS)
		nbox = I830_NR_SAREA_CLIPRECTS;

	for (i = 0; i < nbox; i++, pbox++) {
		if (pbox->x1 > pbox->x2 ||
		    pbox->y1 > pbox->y2 ||
		    pbox->x2 > dev_priv->w || pbox->y2 > dev_priv->h)
			continue;

		DRM_DEBUG("dispatch swap %d,%d-%d,%d!\n",
			  pbox->x1, pbox->y1, pbox->x2, pbox->y2);

		BEGIN_LP_RING(8);
		OUT_RING(CMD);
		OUT_RING(BR13);
		OUT_RING((pbox->y1 << 16) | pbox->x1);
		OUT_RING((pbox->y2 << 16) | pbox->x2);

		if (dev_priv->current_page == 0)
			OUT_RING(dev_priv->front_offset);
		else
			OUT_RING(dev_priv->back_offset);

		OUT_RING((pbox->y1 << 16) | pbox->x1);
		OUT_RING(BR13 & 0xffff);

		if (dev_priv->current_page == 0)
			OUT_RING(dev_priv->back_offset);
		else
			OUT_RING(dev_priv->front_offset);

		ADVANCE_LP_RING();
	}
}

static void i830_dma_dispatch_flip(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	DRM_DEBUG("%s: page=%d pfCurrentPage=%d\n",
		  __FUNCTION__,
		  dev_priv->current_page,
		  dev_priv->sarea_priv->pf_current_page);

	i830_kernel_lost_context(dev);

	if (dev_priv->do_boxes) {
		dev_priv->sarea_priv->perf_boxes |= I830_BOX_FLIP;
		i830_cp_performance_boxes(dev);
	}

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

	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;
}

static void i830_dma_dispatch_vertex(struct drm_device * dev,
				     struct drm_buf * buf, int discard, int used)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	struct drm_clip_rect *box = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	unsigned long address = (unsigned long)buf->bus_address;
	unsigned long start = address - dev->agp->base;
	int i = 0, u;
	RING_LOCALS;

	i830_kernel_lost_context(dev);

	if (nbox > I830_NR_SAREA_CLIPRECTS)
		nbox = I830_NR_SAREA_CLIPRECTS;

	if (discard) {
		u = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT,
			    I830_BUF_HARDWARE);
		if (u != I830_BUF_CLIENT) {
			DRM_DEBUG("xxxx 2\n");
		}
	}

	if (used > 4 * 1023)
		used = 0;

	if (sarea_priv->dirty)
		i830EmitState(dev);

	DRM_DEBUG("dispatch vertex addr 0x%lx, used 0x%x nbox %d\n",
		  address, used, nbox);

	dev_priv->counter++;
	DRM_DEBUG("dispatch counter : %ld\n", dev_priv->counter);
	DRM_DEBUG("i830_dma_dispatch\n");
	DRM_DEBUG("start : %lx\n", start);
	DRM_DEBUG("used : %d\n", used);
	DRM_DEBUG("start + used - 4 : %ld\n", start + used - 4);

	if (buf_priv->currently_mapped == I830_BUF_MAPPED) {
		u32 *vp = buf_priv->kernel_virtual;

		vp[0] = (GFX_OP_PRIMITIVE |
			 sarea_priv->vertex_prim | ((used / 4) - 2));

		if (dev_priv->use_mi_batchbuffer_start) {
			vp[used / 4] = MI_BATCH_BUFFER_END;
			used += 4;
		}

		if (used & 4) {
			vp[used / 4] = 0;
			used += 4;
		}

		i830_unmap_buffer(buf);
	}

	if (used) {
		do {
			if (i < nbox) {
				BEGIN_LP_RING(6);
				OUT_RING(GFX_OP_DRAWRECT_INFO);
				OUT_RING(sarea_priv->
					 BufferState[I830_DESTREG_DR1]);
				OUT_RING(box[i].x1 | (box[i].y1 << 16));
				OUT_RING(box[i].x2 | (box[i].y2 << 16));
				OUT_RING(sarea_priv->
					 BufferState[I830_DESTREG_DR4]);
				OUT_RING(0);
				ADVANCE_LP_RING();
			}

			if (dev_priv->use_mi_batchbuffer_start) {
				BEGIN_LP_RING(2);
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
				OUT_RING(start | MI_BATCH_NON_SECURE);
				ADVANCE_LP_RING();
			} else {
				BEGIN_LP_RING(4);
				OUT_RING(MI_BATCH_BUFFER);
				OUT_RING(start | MI_BATCH_NON_SECURE);
				OUT_RING(start + used - 4);
				OUT_RING(0);
				ADVANCE_LP_RING();
			}

		} while (++i < nbox);
	}

	if (discard) {
		dev_priv->counter++;

		(void)cmpxchg(buf_priv->in_use, I830_BUF_CLIENT,
			      I830_BUF_HARDWARE);

		BEGIN_LP_RING(8);
		OUT_RING(CMD_STORE_DWORD_IDX);
		OUT_RING(20);
		OUT_RING(dev_priv->counter);
		OUT_RING(CMD_STORE_DWORD_IDX);
		OUT_RING(buf_priv->my_use_idx);
		OUT_RING(I830_BUF_FREE);
		OUT_RING(CMD_REPORT_HEAD);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}
}

static void i830_dma_quiescent(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	i830_kernel_lost_context(dev);

	BEGIN_LP_RING(4);
	OUT_RING(INST_PARSER_CLIENT | INST_OP_FLUSH | INST_FLUSH_MAP_CACHE);
	OUT_RING(CMD_REPORT_HEAD);
	OUT_RING(0);
	OUT_RING(0);
	ADVANCE_LP_RING();

	i830_wait_ring(dev, dev_priv->ring.Size - 8, __FUNCTION__);
}

static int i830_flush_queue(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	struct drm_device_dma *dma = dev->dma;
	int i, ret = 0;
	RING_LOCALS;

	i830_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(CMD_REPORT_HEAD);
	OUT_RING(0);
	ADVANCE_LP_RING();

	i830_wait_ring(dev, dev_priv->ring.Size - 8, __FUNCTION__);

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i830_buf_priv_t *buf_priv = buf->dev_private;

		int used = cmpxchg(buf_priv->in_use, I830_BUF_HARDWARE,
				   I830_BUF_FREE);

		if (used == I830_BUF_HARDWARE)
			DRM_DEBUG("reclaimed from HARDWARE\n");
		if (used == I830_BUF_CLIENT)
			DRM_DEBUG("still on client\n");
	}

	return ret;
}

/* Must be called with the lock held */
static void i830_reclaim_buffers(struct drm_device * dev, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int i;

	if (!dma)
		return;
	if (!dev->dev_private)
		return;
	if (!dma->buflist)
		return;

	i830_flush_queue(dev);

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_i830_buf_priv_t *buf_priv = buf->dev_private;

		if (buf->file_priv == file_priv && buf_priv) {
			int used = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT,
					   I830_BUF_FREE);

			if (used == I830_BUF_CLIENT)
				DRM_DEBUG("reclaimed from client\n");
			if (buf_priv->currently_mapped == I830_BUF_MAPPED)
				buf_priv->currently_mapped = I830_BUF_UNMAPPED;
		}
	}
}

static int i830_flush_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	LOCK_TEST_WITH_RETURN(dev, file_priv);

	i830_flush_queue(dev);
	return 0;
}

static int i830_dma_vertex(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	drm_i830_private_t *dev_priv = (drm_i830_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i830_vertex_t *vertex = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("i830 dma vertex, idx %d used %d discard %d\n",
		  vertex->idx, vertex->used, vertex->discard);

	if (vertex->idx < 0 || vertex->idx > dma->buf_count)
		return -EINVAL;

	i830_dma_dispatch_vertex(dev,
				 dma->buflist[vertex->idx],
				 vertex->discard, vertex->used);

	sarea_priv->last_enqueue = dev_priv->counter - 1;
	sarea_priv->last_dispatch = (int)hw_status[5];

	return 0;
}

static int i830_clear_bufs(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	drm_i830_clear_t *clear = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* GH: Someone's doing nasty things... */
	if (!dev->dev_private) {
		return -EINVAL;
	}

	i830_dma_dispatch_clear(dev, clear->flags,
				clear->clear_color,
				clear->clear_depth, clear->clear_depthmask);
	return 0;
}

static int i830_swap_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	DRM_DEBUG("i830_swap_bufs\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	i830_dma_dispatch_swap(dev);
	return 0;
}

/* Not sure why this isn't set all the time:
 */
static void i830_do_init_pageflip(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("%s\n", __FUNCTION__);
	dev_priv->page_flipping = 1;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;
}

static int i830_do_cleanup_pageflip(struct drm_device * dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("%s\n", __FUNCTION__);
	if (dev_priv->current_page != 0)
		i830_dma_dispatch_flip(dev);

	dev_priv->page_flipping = 0;
	return 0;
}

static int i830_flip_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	drm_i830_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("%s\n", __FUNCTION__);

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv->page_flipping)
		i830_do_init_pageflip(dev);

	i830_dma_dispatch_flip(dev);
	return 0;
}

static int i830_getage(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	drm_i830_private_t *dev_priv = (drm_i830_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *)
	    dev_priv->sarea_priv;

	sarea_priv->last_dispatch = (int)hw_status[5];
	return 0;
}

static int i830_getbuf(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	int retcode = 0;
	drm_i830_dma_t *d = data;
	drm_i830_private_t *dev_priv = (drm_i830_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *)
	    dev_priv->sarea_priv;

	DRM_DEBUG("getbuf\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	d->granted = 0;

	retcode = i830_dma_get_buffer(dev, d, file_priv);

	DRM_DEBUG("i830_dma: %d returning %d, granted = %d\n",
		  task_pid_nr(current), retcode, d->granted);

	sarea_priv->last_dispatch = (int)hw_status[5];

	return retcode;
}

static int i830_copybuf(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	/* Never copy - 2.4.x doesn't need it */
	return 0;
}

static int i830_docopy(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	return 0;
}

static int i830_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_getparam_t *param = data;
	int value;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	switch (param->param) {
	case I830_PARAM_IRQ_ACTIVE:
		value = dev->irq_enabled;
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user(param->value, &value, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

static int i830_setparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_setparam_t *param = data;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	switch (param->param) {
	case I830_SETPARAM_USE_MI_BATCHBUFFER_START:
		dev_priv->use_mi_batchbuffer_start = param->value;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int i830_driver_load(struct drm_device *dev, unsigned long flags)
{
	/* i830 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	return 0;
}

void i830_driver_lastclose(struct drm_device * dev)
{
	i830_dma_cleanup(dev);
}

void i830_driver_preclose(struct drm_device * dev, struct drm_file *file_priv)
{
	if (dev->dev_private) {
		drm_i830_private_t *dev_priv = dev->dev_private;
		if (dev_priv->page_flipping) {
			i830_do_cleanup_pageflip(dev);
		}
	}
}

void i830_driver_reclaim_buffers_locked(struct drm_device * dev, struct drm_file *file_priv)
{
	i830_reclaim_buffers(dev, file_priv);
}

int i830_driver_dma_quiescent(struct drm_device * dev)
{
	i830_dma_quiescent(dev);
	return 0;
}

struct drm_ioctl_desc i830_ioctls[] = {
	DRM_IOCTL_DEF(DRM_I830_INIT, i830_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I830_VERTEX, i830_dma_vertex, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_CLEAR, i830_clear_bufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_FLUSH, i830_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_GETAGE, i830_getage, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_GETBUF, i830_getbuf, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_SWAP, i830_swap_bufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_COPY, i830_copybuf, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_DOCOPY, i830_docopy, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_FLIP, i830_flip_bufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_IRQ_EMIT, i830_irq_emit, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_IRQ_WAIT, i830_irq_wait, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_GETPARAM, i830_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I830_SETPARAM, i830_setparam, DRM_AUTH)
};

int i830_max_ioctl = DRM_ARRAY_SIZE(i830_ioctls);

/**
 * Determine if the device really is AGP or not.
 *
 * All Intel graphics chipsets are treated as AGP, even if they are really
 * PCI-e.
 *
 * \param dev   The device to be tested.
 *
 * \returns
 * A value of 1 is always retured to indictate every i8xx is AGP.
 */
int i830_driver_device_is_agp(struct drm_device * dev)
{
	return 1;
}
