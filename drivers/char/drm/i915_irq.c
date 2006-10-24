/* i915_irq.c -- IRQ support for the I915 -*- linux-c -*-
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

#define USER_INT_FLAG (1<<1)
#define VSYNC_PIPEB_FLAG (1<<5)
#define VSYNC_PIPEA_FLAG (1<<7)

#define MAX_NOPID ((u32)~0)

/**
 * Emit blits for scheduled buffer swaps.
 *
 * This function will be called with the HW lock held.
 */
static void i915_vblank_tasklet(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;
	struct list_head *list, *tmp;

	DRM_DEBUG("\n");

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

	list_for_each_safe(list, tmp, &dev_priv->vbl_swaps.head) {
		drm_i915_vbl_swap_t *vbl_swap =
			list_entry(list, drm_i915_vbl_swap_t, head);
		atomic_t *counter = vbl_swap->pipe ? &dev->vbl_received2 :
			&dev->vbl_received;

		if ((atomic_read(counter) - vbl_swap->sequence) <= (1<<23)) {
			drm_drawable_info_t *drw;

			spin_unlock(&dev_priv->swaps_lock);

			spin_lock(&dev->drw_lock);

			drw = drm_get_drawable_info(dev, vbl_swap->drw_id);
				
			if (drw) {
				int i, num_rects = drw->num_rects;
				drm_clip_rect_t *rect = drw->rects;
				drm_i915_sarea_t *sarea_priv =
				    dev_priv->sarea_priv;
				u32 cpp = dev_priv->cpp;
				u32 cmd = (cpp == 4) ? (XY_SRC_COPY_BLT_CMD |
							XY_SRC_COPY_BLT_WRITE_ALPHA |
							XY_SRC_COPY_BLT_WRITE_RGB)
						     : XY_SRC_COPY_BLT_CMD;
				u32 pitchropcpp = (sarea_priv->pitch * cpp) |
						  (0xcc << 16) | (cpp << 23) |
						  (1 << 24);
				RING_LOCALS;

				i915_kernel_lost_context(dev);

				BEGIN_LP_RING(6);

				OUT_RING(GFX_OP_DRAWRECT_INFO);
				OUT_RING(0);
				OUT_RING(0);
				OUT_RING(sarea_priv->width |
					 sarea_priv->height << 16);
				OUT_RING(sarea_priv->width |
					 sarea_priv->height << 16);
				OUT_RING(0);

				ADVANCE_LP_RING();

				sarea_priv->ctxOwner = DRM_KERNEL_CONTEXT;

				for (i = 0; i < num_rects; i++, rect++) {
					BEGIN_LP_RING(8);

					OUT_RING(cmd);
					OUT_RING(pitchropcpp);
					OUT_RING((rect->y1 << 16) | rect->x1);
					OUT_RING((rect->y2 << 16) | rect->x2);
					OUT_RING(sarea_priv->front_offset);
					OUT_RING((rect->y1 << 16) | rect->x1);
					OUT_RING(pitchropcpp & 0xffff);
					OUT_RING(sarea_priv->back_offset);

					ADVANCE_LP_RING();
				}
			}

			spin_unlock(&dev->drw_lock);

			spin_lock(&dev_priv->swaps_lock);

			list_del(list);

			drm_free(vbl_swap, sizeof(*vbl_swap), DRM_MEM_DRIVER);

			dev_priv->swaps_pending--;
		}
	}

	spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);
}

irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS)
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u16 temp;

	temp = I915_READ16(I915REG_INT_IDENTITY_R);

	temp &= (USER_INT_FLAG | VSYNC_PIPEA_FLAG | VSYNC_PIPEB_FLAG);

	DRM_DEBUG("%s flag=%08x\n", __FUNCTION__, temp);

	if (temp == 0)
		return IRQ_NONE;

	I915_WRITE16(I915REG_INT_IDENTITY_R, temp);

	dev_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

	if (temp & USER_INT_FLAG)
		DRM_WAKEUP(&dev_priv->irq_queue);

	if (temp & (VSYNC_PIPEA_FLAG | VSYNC_PIPEB_FLAG)) {
		int vblank_pipe = dev_priv->vblank_pipe;

		if ((vblank_pipe &
		     (DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B))
		    == (DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B)) {
			if (temp & VSYNC_PIPEA_FLAG)
				atomic_inc(&dev->vbl_received);
			if (temp & VSYNC_PIPEB_FLAG)
				atomic_inc(&dev->vbl_received2);
		} else if (((temp & VSYNC_PIPEA_FLAG) &&
			    (vblank_pipe & DRM_I915_VBLANK_PIPE_A)) ||
			   ((temp & VSYNC_PIPEB_FLAG) &&
			    (vblank_pipe & DRM_I915_VBLANK_PIPE_B)))
			atomic_inc(&dev->vbl_received);

		DRM_WAKEUP(&dev->vbl_queue);
		drm_vbl_send_signals(dev);

		if (dev_priv->swaps_pending > 0)
			drm_locked_tasklet(dev, i915_vblank_tasklet);
	}

	return IRQ_HANDLED;
}

static int i915_emit_irq(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	i915_kernel_lost_context(dev);

	DRM_DEBUG("%s\n", __FUNCTION__);

	dev_priv->sarea_priv->last_enqueue = ++dev_priv->counter;

	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->sarea_priv->last_enqueue = dev_priv->counter = 1;

	BEGIN_LP_RING(6);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(20);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(GFX_OP_USER_INTERRUPT);
	ADVANCE_LP_RING();
	
	return dev_priv->counter;
}

static int i915_wait_irq(drm_device_t * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = 0;

	DRM_DEBUG("%s irq_nr=%d breadcrumb=%d\n", __FUNCTION__, irq_nr,
		  READ_BREADCRUMB(dev_priv));

	if (READ_BREADCRUMB(dev_priv) >= irq_nr)
		return 0;

	dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	DRM_WAIT_ON(ret, dev_priv->irq_queue, 3 * DRM_HZ,
		    READ_BREADCRUMB(dev_priv) >= irq_nr);

	if (ret == DRM_ERR(EBUSY)) {
		DRM_ERROR("%s: EBUSY -- rec: %d emitted: %d\n",
			  __FUNCTION__,
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->counter);
	}

	dev_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
	return ret;
}

static int i915_driver_vblank_do_wait(drm_device_t *dev, unsigned int *sequence,
				      atomic_t *counter)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned int cur_vblank;
	int ret = 0;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(counter))
			- *sequence) <= (1<<23)));
	
	*sequence = cur_vblank;

	return ret;
}


int i915_driver_vblank_wait(drm_device_t *dev, unsigned int *sequence)
{
	return i915_driver_vblank_do_wait(dev, sequence, &dev->vbl_received);
}

int i915_driver_vblank_wait2(drm_device_t *dev, unsigned int *sequence)
{
	return i915_driver_vblank_do_wait(dev, sequence, &dev->vbl_received2);
}

/* Needs the lock as it touches the ring.
 */
int i915_irq_emit(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t emit;
	int result;

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(emit, (drm_i915_irq_emit_t __user *) data,
				 sizeof(emit));

	result = i915_emit_irq(dev);

	if (DRM_COPY_TO_USER(emit.irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return DRM_ERR(EFAULT);
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
int i915_irq_wait(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t irqwait;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(irqwait, (drm_i915_irq_wait_t __user *) data,
				 sizeof(irqwait));

	return i915_wait_irq(dev, irqwait.irq_seq);
}

static void i915_enable_interrupt (drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u16 flag;

	flag = 0;
	if (dev_priv->vblank_pipe & DRM_I915_VBLANK_PIPE_A)
		flag |= VSYNC_PIPEA_FLAG;
	if (dev_priv->vblank_pipe & DRM_I915_VBLANK_PIPE_B)
		flag |= VSYNC_PIPEB_FLAG;

	I915_WRITE16(I915REG_INT_ENABLE_R, USER_INT_FLAG | flag);
}

/* Set the vblank monitor pipe
 */
int i915_vblank_pipe_set(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t pipe;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(pipe, (drm_i915_vblank_pipe_t __user *) data,
				 sizeof(pipe));

	if (pipe.pipe & ~(DRM_I915_VBLANK_PIPE_A|DRM_I915_VBLANK_PIPE_B)) {
		DRM_ERROR("%s called with invalid pipe 0x%x\n", 
			  __FUNCTION__, pipe.pipe);
		return DRM_ERR(EINVAL);
	}

	dev_priv->vblank_pipe = pipe.pipe;

	i915_enable_interrupt (dev);

	return 0;
}

int i915_vblank_pipe_get(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t pipe;
	u16 flag;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	flag = I915_READ(I915REG_INT_ENABLE_R);
	pipe.pipe = 0;
	if (flag & VSYNC_PIPEA_FLAG)
		pipe.pipe |= DRM_I915_VBLANK_PIPE_A;
	if (flag & VSYNC_PIPEB_FLAG)
		pipe.pipe |= DRM_I915_VBLANK_PIPE_B;
	DRM_COPY_TO_USER_IOCTL((drm_i915_vblank_pipe_t __user *) data, pipe,
				 sizeof(pipe));
	return 0;
}

/**
 * Schedule buffer swap at given vertical blank.
 */
int i915_vblank_swap(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_swap_t swap;
	drm_i915_vbl_swap_t *vbl_swap;
	unsigned int pipe, seqtype, curseq;
	unsigned long irqflags;
	struct list_head *list;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __func__);
		return DRM_ERR(EINVAL);
	}

	if (dev_priv->sarea_priv->rotation) {
		DRM_DEBUG("Rotation not supported\n");
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(swap, (drm_i915_vblank_swap_t __user *) data,
				 sizeof(swap));

	if (swap.seqtype & ~(_DRM_VBLANK_RELATIVE | _DRM_VBLANK_ABSOLUTE |
			     _DRM_VBLANK_SECONDARY | _DRM_VBLANK_NEXTONMISS)) {
		DRM_ERROR("Invalid sequence type 0x%x\n", swap.seqtype);
		return DRM_ERR(EINVAL);
	}

	pipe = (swap.seqtype & _DRM_VBLANK_SECONDARY) ? 1 : 0;

	seqtype = swap.seqtype & (_DRM_VBLANK_RELATIVE | _DRM_VBLANK_ABSOLUTE);

	if (!(dev_priv->vblank_pipe & (1 << pipe))) {
		DRM_ERROR("Invalid pipe %d\n", pipe);
		return DRM_ERR(EINVAL);
	}

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	if (!drm_get_drawable_info(dev, swap.drawable)) {
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		DRM_ERROR("Invalid drawable ID %d\n", swap.drawable);
		return DRM_ERR(EINVAL);
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	curseq = atomic_read(pipe ? &dev->vbl_received2 : &dev->vbl_received);

	if (seqtype == _DRM_VBLANK_RELATIVE)
		swap.sequence += curseq;

	if ((curseq - swap.sequence) <= (1<<23)) {
		if (swap.seqtype & _DRM_VBLANK_NEXTONMISS) {
			swap.sequence = curseq + 1;
		} else {
			DRM_DEBUG("Missed target sequence\n");
			return DRM_ERR(EINVAL);
		}
	}

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

	list_for_each(list, &dev_priv->vbl_swaps.head) {
		vbl_swap = list_entry(list, drm_i915_vbl_swap_t, head);

		if (vbl_swap->drw_id == swap.drawable &&
		    vbl_swap->pipe == pipe &&
		    vbl_swap->sequence == swap.sequence) {
			spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);
			DRM_DEBUG("Already scheduled\n");
			return 0;
		}
	}

	spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);

	if (dev_priv->swaps_pending >= 100) {
		DRM_DEBUG("Too many swaps queued\n");
		return DRM_ERR(EBUSY);
	}

	vbl_swap = drm_calloc(1, sizeof(vbl_swap), DRM_MEM_DRIVER);

	if (!vbl_swap) {
		DRM_ERROR("Failed to allocate memory to queue swap\n");
		return DRM_ERR(ENOMEM);
	}

	DRM_DEBUG("\n");

	vbl_swap->drw_id = swap.drawable;
	vbl_swap->pipe = pipe;
	vbl_swap->sequence = swap.sequence;

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

	list_add_tail((struct list_head *)vbl_swap, &dev_priv->vbl_swaps.head);
	dev_priv->swaps_pending++;

	spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);

	DRM_COPY_TO_USER_IOCTL((drm_i915_vblank_swap_t __user *) data, swap,
			       sizeof(swap));

	return 0;
}

/* drm_dma.h hooks
*/
void i915_driver_irq_preinstall(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	I915_WRITE16(I915REG_HWSTAM, 0xfffe);
	I915_WRITE16(I915REG_INT_MASK_R, 0x0);
	I915_WRITE16(I915REG_INT_ENABLE_R, 0x0);
}

void i915_driver_irq_postinstall(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	dev_priv->swaps_lock = SPIN_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&dev_priv->vbl_swaps.head);
	dev_priv->swaps_pending = 0;

	if (!dev_priv->vblank_pipe)
		dev_priv->vblank_pipe = DRM_I915_VBLANK_PIPE_A;
	i915_enable_interrupt(dev);
	DRM_INIT_WAITQUEUE(&dev_priv->irq_queue);
}

void i915_driver_irq_uninstall(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u16 temp;

	if (!dev_priv)
		return;

	I915_WRITE16(I915REG_HWSTAM, 0xffff);
	I915_WRITE16(I915REG_INT_MASK_R, 0xffff);
	I915_WRITE16(I915REG_INT_ENABLE_R, 0x0);

	temp = I915_READ16(I915REG_INT_IDENTITY_R);
	I915_WRITE16(I915REG_INT_IDENTITY_R, temp);
}
