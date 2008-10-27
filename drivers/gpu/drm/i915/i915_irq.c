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

#define MAX_NOPID ((u32)~0)

/** These are the interrupts used by the driver */
#define I915_INTERRUPT_ENABLE_MASK (I915_USER_INTERRUPT |		\
				    I915_ASLE_INTERRUPT |		\
				    I915_DISPLAY_PIPE_A_EVENT_INTERRUPT | \
				    I915_DISPLAY_PIPE_B_EVENT_INTERRUPT)

void
i915_enable_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != 0) {
		dev_priv->irq_mask_reg &= ~mask;
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
		(void) I915_READ(IMR);
	}
}

static inline void
i915_disable_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != mask) {
		dev_priv->irq_mask_reg |= mask;
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
		(void) I915_READ(IMR);
	}
}

/**
 * i915_pipe_enabled - check if a pipe is enabled
 * @dev: DRM device
 * @pipe: pipe to check
 *
 * Reading certain registers when the pipe is disabled can hang the chip.
 * Use this routine to make sure the PLL is running and the pipe is active
 * before reading such registers if unsure.
 */
static int
i915_pipe_enabled(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long pipeconf = pipe ? PIPEBCONF : PIPEACONF;

	if (I915_READ(pipeconf) & PIPEACONF_ENABLE)
		return 1;

	return 0;
}

/**
 * Emit blits for scheduled buffer swaps.
 *
 * This function will be called with the HW lock held.
 * Because this function must grab the ring mutex (dev->struct_mutex),
 * it can no longer run at soft irq time. We'll fix this when we do
 * the DRI2 swap buffer work.
 */
static void i915_vblank_tasklet(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;
	struct list_head *list, *tmp, hits, *hit;
	int nhits, nrects, slice[2], upper[2], lower[2], i;
	unsigned counter[2];
	struct drm_drawable_info *drw;
	drm_i915_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 cpp = dev_priv->cpp;
	u32 cmd = (cpp == 4) ? (XY_SRC_COPY_BLT_CMD |
				XY_SRC_COPY_BLT_WRITE_ALPHA |
				XY_SRC_COPY_BLT_WRITE_RGB)
			     : XY_SRC_COPY_BLT_CMD;
	u32 src_pitch = sarea_priv->pitch * cpp;
	u32 dst_pitch = sarea_priv->pitch * cpp;
	u32 ropcpp = (0xcc << 16) | ((cpp - 1) << 24);
	RING_LOCALS;

	mutex_lock(&dev->struct_mutex);

	if (IS_I965G(dev) && sarea_priv->front_tiled) {
		cmd |= XY_SRC_COPY_BLT_DST_TILED;
		dst_pitch >>= 2;
	}
	if (IS_I965G(dev) && sarea_priv->back_tiled) {
		cmd |= XY_SRC_COPY_BLT_SRC_TILED;
		src_pitch >>= 2;
	}

	counter[0] = drm_vblank_count(dev, 0);
	counter[1] = drm_vblank_count(dev, 1);

	DRM_DEBUG("\n");

	INIT_LIST_HEAD(&hits);

	nhits = nrects = 0;

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

	/* Find buffer swaps scheduled for this vertical blank */
	list_for_each_safe(list, tmp, &dev_priv->vbl_swaps.head) {
		drm_i915_vbl_swap_t *vbl_swap =
			list_entry(list, drm_i915_vbl_swap_t, head);
		int pipe = vbl_swap->pipe;

		if ((counter[pipe] - vbl_swap->sequence) > (1<<23))
			continue;

		list_del(list);
		dev_priv->swaps_pending--;
		drm_vblank_put(dev, pipe);

		spin_unlock(&dev_priv->swaps_lock);
		spin_lock(&dev->drw_lock);

		drw = drm_get_drawable_info(dev, vbl_swap->drw_id);

		list_for_each(hit, &hits) {
			drm_i915_vbl_swap_t *swap_cmp =
				list_entry(hit, drm_i915_vbl_swap_t, head);
			struct drm_drawable_info *drw_cmp =
				drm_get_drawable_info(dev, swap_cmp->drw_id);

			/* Make sure both drawables are still
			 * around and have some rectangles before
			 * we look inside to order them for the
			 * blts below.
			 */
			if (drw_cmp && drw_cmp->num_rects > 0 &&
			    drw && drw->num_rects > 0 &&
			    drw_cmp->rects[0].y1 > drw->rects[0].y1) {
				list_add_tail(list, hit);
				break;
			}
		}

		spin_unlock(&dev->drw_lock);

		/* List of hits was empty, or we reached the end of it */
		if (hit == &hits)
			list_add_tail(list, hits.prev);

		nhits++;

		spin_lock(&dev_priv->swaps_lock);
	}

	if (nhits == 0) {
		spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);
		mutex_unlock(&dev->struct_mutex);
		return;
	}

	spin_unlock(&dev_priv->swaps_lock);

	i915_kernel_lost_context(dev);

	if (IS_I965G(dev)) {
		BEGIN_LP_RING(4);

		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING(0);
		OUT_RING(((sarea_priv->width - 1) & 0xffff) | ((sarea_priv->height - 1) << 16));
		OUT_RING(0);
		ADVANCE_LP_RING();
	} else {
		BEGIN_LP_RING(6);

		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(0);
		OUT_RING(0);
		OUT_RING(sarea_priv->width | sarea_priv->height << 16);
		OUT_RING(sarea_priv->width | sarea_priv->height << 16);
		OUT_RING(0);

		ADVANCE_LP_RING();
	}

	sarea_priv->ctxOwner = DRM_KERNEL_CONTEXT;

	upper[0] = upper[1] = 0;
	slice[0] = max(sarea_priv->pipeA_h / nhits, 1);
	slice[1] = max(sarea_priv->pipeB_h / nhits, 1);
	lower[0] = sarea_priv->pipeA_y + slice[0];
	lower[1] = sarea_priv->pipeB_y + slice[0];

	spin_lock(&dev->drw_lock);

	/* Emit blits for buffer swaps, partitioning both outputs into as many
	 * slices as there are buffer swaps scheduled in order to avoid tearing
	 * (based on the assumption that a single buffer swap would always
	 * complete before scanout starts).
	 */
	for (i = 0; i++ < nhits;
	     upper[0] = lower[0], lower[0] += slice[0],
	     upper[1] = lower[1], lower[1] += slice[1]) {
		if (i == nhits)
			lower[0] = lower[1] = sarea_priv->height;

		list_for_each(hit, &hits) {
			drm_i915_vbl_swap_t *swap_hit =
				list_entry(hit, drm_i915_vbl_swap_t, head);
			struct drm_clip_rect *rect;
			int num_rects, pipe;
			unsigned short top, bottom;

			drw = drm_get_drawable_info(dev, swap_hit->drw_id);

			/* The drawable may have been destroyed since
			 * the vblank swap was queued
			 */
			if (!drw)
				continue;

			rect = drw->rects;
			pipe = swap_hit->pipe;
			top = upper[pipe];
			bottom = lower[pipe];

			for (num_rects = drw->num_rects; num_rects--; rect++) {
				int y1 = max(rect->y1, top);
				int y2 = min(rect->y2, bottom);

				if (y1 >= y2)
					continue;

				BEGIN_LP_RING(8);

				OUT_RING(cmd);
				OUT_RING(ropcpp | dst_pitch);
				OUT_RING((y1 << 16) | rect->x1);
				OUT_RING((y2 << 16) | rect->x2);
				OUT_RING(sarea_priv->front_offset);
				OUT_RING((y1 << 16) | rect->x1);
				OUT_RING(src_pitch);
				OUT_RING(sarea_priv->back_offset);

				ADVANCE_LP_RING();
			}
		}
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);
	mutex_unlock(&dev->struct_mutex);

	list_for_each_safe(hit, tmp, &hits) {
		drm_i915_vbl_swap_t *swap_hit =
			list_entry(hit, drm_i915_vbl_swap_t, head);

		list_del(hit);

		drm_free(swap_hit, sizeof(*swap_hit), DRM_MEM_DRIVER);
	}
}

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
u32 i915_get_vblank_counter(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long high_frame;
	unsigned long low_frame;
	u32 high1, high2, low, count;

	high_frame = pipe ? PIPEBFRAMEHIGH : PIPEAFRAMEHIGH;
	low_frame = pipe ? PIPEBFRAMEPIXEL : PIPEAFRAMEPIXEL;

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_ERROR("trying to get vblank count for disabled pipe %d\n", pipe);
		return 0;
	}

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	do {
		high1 = ((I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK) >>
			 PIPE_FRAME_HIGH_SHIFT);
		low =  ((I915_READ(low_frame) & PIPE_FRAME_LOW_MASK) >>
			PIPE_FRAME_LOW_SHIFT);
		high2 = ((I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK) >>
			 PIPE_FRAME_HIGH_SHIFT);
	} while (high1 != high2);

	count = (high1 << 8) | low;

	return count;
}

void
i915_vblank_work_handler(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    vblank_work);
	struct drm_device *dev = dev_priv->dev;
	unsigned long irqflags;

	if (dev->lock.hw_lock == NULL) {
		i915_vblank_tasklet(dev);
		return;
	}

	spin_lock_irqsave(&dev->tasklet_lock, irqflags);
	dev->locked_tasklet_func = i915_vblank_tasklet;
	spin_unlock_irqrestore(&dev->tasklet_lock, irqflags);

	/* Try to get the lock now, if this fails, the lock
	 * holder will execute the tasklet during unlock
	 */
	if (!drm_lock_take(&dev->lock, DRM_KERNEL_CONTEXT))
		return;

	dev->lock.lock_time = jiffies;
	atomic_inc(&dev->counts[_DRM_STAT_LOCKS]);

	spin_lock_irqsave(&dev->tasklet_lock, irqflags);
	dev->locked_tasklet_func = NULL;
	spin_unlock_irqrestore(&dev->tasklet_lock, irqflags);

	i915_vblank_tasklet(dev);
	drm_lock_free(&dev->lock, DRM_KERNEL_CONTEXT);
}

irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 iir;
	u32 pipea_stats, pipeb_stats;
	int vblank = 0;

	atomic_inc(&dev_priv->irq_received);

	if (dev->pdev->msi_enabled)
		I915_WRITE(IMR, ~0);
	iir = I915_READ(IIR);

	if (iir == 0) {
		if (dev->pdev->msi_enabled) {
			I915_WRITE(IMR, dev_priv->irq_mask_reg);
			(void) I915_READ(IMR);
		}
		return IRQ_NONE;
	}

	/*
	 * Clear the PIPE(A|B)STAT regs before the IIR otherwise
	 * we may get extra interrupts.
	 */
	if (iir & I915_DISPLAY_PIPE_A_EVENT_INTERRUPT) {
		pipea_stats = I915_READ(PIPEASTAT);
		if (!(dev_priv->vblank_pipe & DRM_I915_VBLANK_PIPE_A))
			pipea_stats &= ~(PIPE_START_VBLANK_INTERRUPT_ENABLE |
					 PIPE_VBLANK_INTERRUPT_ENABLE);
		else if (pipea_stats & (PIPE_START_VBLANK_INTERRUPT_STATUS|
					PIPE_VBLANK_INTERRUPT_STATUS)) {
			vblank++;
			drm_handle_vblank(dev, 0);
		}

		I915_WRITE(PIPEASTAT, pipea_stats);
	}
	if (iir & I915_DISPLAY_PIPE_B_EVENT_INTERRUPT) {
		pipeb_stats = I915_READ(PIPEBSTAT);
		/* Ack the event */
		I915_WRITE(PIPEBSTAT, pipeb_stats);

		/* The vblank interrupt gets enabled even if we didn't ask for
		   it, so make sure it's shut down again */
		if (!(dev_priv->vblank_pipe & DRM_I915_VBLANK_PIPE_B))
			pipeb_stats &= ~(PIPE_START_VBLANK_INTERRUPT_ENABLE |
					 PIPE_VBLANK_INTERRUPT_ENABLE);
		else if (pipeb_stats & (PIPE_START_VBLANK_INTERRUPT_STATUS|
					PIPE_VBLANK_INTERRUPT_STATUS)) {
			vblank++;
			drm_handle_vblank(dev, 1);
		}

		if (pipeb_stats & I915_LEGACY_BLC_EVENT_STATUS)
			opregion_asle_intr(dev);
		I915_WRITE(PIPEBSTAT, pipeb_stats);
	}

	I915_WRITE(IIR, iir);
	if (dev->pdev->msi_enabled)
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
	(void) I915_READ(IIR); /* Flush posted writes */

	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->last_dispatch =
			READ_BREADCRUMB(dev_priv);

	if (iir & I915_USER_INTERRUPT) {
		dev_priv->mm.irq_gem_seqno = i915_get_gem_seqno(dev);
		DRM_WAKEUP(&dev_priv->irq_queue);
	}

	if (iir & I915_ASLE_INTERRUPT)
		opregion_asle_intr(dev);

	if (vblank && dev_priv->swaps_pending > 0)
		schedule_work(&dev_priv->vblank_work);

	return IRQ_HANDLED;
}

static int i915_emit_irq(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	i915_kernel_lost_context(dev);

	DRM_DEBUG("\n");

	dev_priv->counter++;
	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->counter = 1;
	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->last_enqueue = dev_priv->counter;

	BEGIN_LP_RING(6);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(5 << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	OUT_RING(0);
	OUT_RING(MI_USER_INTERRUPT);
	ADVANCE_LP_RING();

	return dev_priv->counter;
}

void i915_user_irq_get(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (dev->irq_enabled && (++dev_priv->user_irq_refcount == 1))
		i915_enable_irq(dev_priv, I915_USER_INTERRUPT);
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

void i915_user_irq_put(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	BUG_ON(dev->irq_enabled && dev_priv->user_irq_refcount <= 0);
	if (dev->irq_enabled && (--dev_priv->user_irq_refcount == 0))
		i915_disable_irq(dev_priv, I915_USER_INTERRUPT);
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

static int i915_wait_irq(struct drm_device * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = 0;

	DRM_DEBUG("irq_nr=%d breadcrumb=%d\n", irq_nr,
		  READ_BREADCRUMB(dev_priv));

	if (READ_BREADCRUMB(dev_priv) >= irq_nr) {
		if (dev_priv->sarea_priv) {
			dev_priv->sarea_priv->last_dispatch =
				READ_BREADCRUMB(dev_priv);
		}
		return 0;
	}

	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	i915_user_irq_get(dev);
	DRM_WAIT_ON(ret, dev_priv->irq_queue, 3 * DRM_HZ,
		    READ_BREADCRUMB(dev_priv) >= irq_nr);
	i915_user_irq_put(dev);

	if (ret == -EBUSY) {
		DRM_ERROR("EBUSY -- rec: %d emitted: %d\n",
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->counter);
	}

	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->last_dispatch =
			READ_BREADCRUMB(dev_priv);

	return ret;
}

/* Needs the lock as it touches the ring.
 */
int i915_irq_emit(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t *emit = data;
	int result;

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}
	mutex_lock(&dev->struct_mutex);
	result = i915_emit_irq(dev);
	mutex_unlock(&dev->struct_mutex);

	if (DRM_COPY_TO_USER(emit->irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
int i915_irq_wait(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t *irqwait = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return i915_wait_irq(dev, irqwait->irq_seq);
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
int i915_enable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32	pipestat_reg = 0;
	u32	pipestat;
	u32	interrupt = 0;
	unsigned long irqflags;

	switch (pipe) {
	case 0:
		pipestat_reg = PIPEASTAT;
		interrupt = I915_DISPLAY_PIPE_A_EVENT_INTERRUPT;
		break;
	case 1:
		pipestat_reg = PIPEBSTAT;
		interrupt = I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
		break;
	default:
		DRM_ERROR("tried to enable vblank on non-existent pipe %d\n",
			  pipe);
		return 0;
	}

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	/* Enabling vblank events in IMR comes before PIPESTAT write, or
	 * there's a race where the PIPESTAT vblank bit gets set to 1, so
	 * the OR of enabled PIPESTAT bits goes to 1, so the PIPExEVENT in
	 * ISR flashes to 1, but the IIR bit doesn't get set to 1 because
	 * IMR masks it.  It doesn't ever get set after we clear the masking
	 * in IMR because the ISR bit is edge, not level-triggered, on the
	 * OR of PIPESTAT bits.
	 */
	i915_enable_irq(dev_priv, interrupt);
	pipestat = I915_READ(pipestat_reg);
	if (IS_I965G(dev))
		pipestat |= PIPE_START_VBLANK_INTERRUPT_ENABLE;
	else
		pipestat |= PIPE_VBLANK_INTERRUPT_ENABLE;
	/* Clear any stale interrupt status */
	pipestat |= (PIPE_START_VBLANK_INTERRUPT_STATUS |
		     PIPE_VBLANK_INTERRUPT_STATUS);
	I915_WRITE(pipestat_reg, pipestat);
	(void) I915_READ(pipestat_reg);	/* Posting read */
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);

	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
void i915_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32	pipestat_reg = 0;
	u32	pipestat;
	u32	interrupt = 0;
	unsigned long irqflags;

	switch (pipe) {
	case 0:
		pipestat_reg = PIPEASTAT;
		interrupt = I915_DISPLAY_PIPE_A_EVENT_INTERRUPT;
		break;
	case 1:
		pipestat_reg = PIPEBSTAT;
		interrupt = I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
		break;
	default:
		DRM_ERROR("tried to disable vblank on non-existent pipe %d\n",
			  pipe);
		return;
		break;
	}

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	i915_disable_irq(dev_priv, interrupt);
	pipestat = I915_READ(pipestat_reg);
	pipestat &= ~(PIPE_START_VBLANK_INTERRUPT_ENABLE |
		      PIPE_VBLANK_INTERRUPT_ENABLE);
	/* Clear any stale interrupt status */
	pipestat |= (PIPE_START_VBLANK_INTERRUPT_STATUS |
		     PIPE_VBLANK_INTERRUPT_STATUS);
	I915_WRITE(pipestat_reg, pipestat);
	(void) I915_READ(pipestat_reg);	/* Posting read */
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

/* Set the vblank monitor pipe
 */
int i915_vblank_pipe_set(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return 0;
}

int i915_vblank_pipe_get(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t *pipe = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	pipe->pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;

	return 0;
}

/**
 * Schedule buffer swap at given vertical blank.
 */
int i915_vblank_swap(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_swap_t *swap = data;
	drm_i915_vbl_swap_t *vbl_swap, *vbl_old;
	unsigned int pipe, seqtype, curseq;
	unsigned long irqflags;
	struct list_head *list;
	int ret;

	if (!dev_priv || !dev_priv->sarea_priv) {
		DRM_ERROR("%s called with no initialization\n", __func__);
		return -EINVAL;
	}

	if (dev_priv->sarea_priv->rotation) {
		DRM_DEBUG("Rotation not supported\n");
		return -EINVAL;
	}

	if (swap->seqtype & ~(_DRM_VBLANK_RELATIVE | _DRM_VBLANK_ABSOLUTE |
			     _DRM_VBLANK_SECONDARY | _DRM_VBLANK_NEXTONMISS)) {
		DRM_ERROR("Invalid sequence type 0x%x\n", swap->seqtype);
		return -EINVAL;
	}

	pipe = (swap->seqtype & _DRM_VBLANK_SECONDARY) ? 1 : 0;

	seqtype = swap->seqtype & (_DRM_VBLANK_RELATIVE | _DRM_VBLANK_ABSOLUTE);

	if (!(dev_priv->vblank_pipe & (1 << pipe))) {
		DRM_ERROR("Invalid pipe %d\n", pipe);
		return -EINVAL;
	}

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	if (!drm_get_drawable_info(dev, swap->drawable)) {
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		DRM_DEBUG("Invalid drawable ID %d\n", swap->drawable);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	/*
	 * We take the ref here and put it when the swap actually completes
	 * in the tasklet.
	 */
	ret = drm_vblank_get(dev, pipe);
	if (ret)
		return ret;
	curseq = drm_vblank_count(dev, pipe);

	if (seqtype == _DRM_VBLANK_RELATIVE)
		swap->sequence += curseq;

	if ((curseq - swap->sequence) <= (1<<23)) {
		if (swap->seqtype & _DRM_VBLANK_NEXTONMISS) {
			swap->sequence = curseq + 1;
		} else {
			DRM_DEBUG("Missed target sequence\n");
			drm_vblank_put(dev, pipe);
			return -EINVAL;
		}
	}

	vbl_swap = drm_calloc(1, sizeof(*vbl_swap), DRM_MEM_DRIVER);

	if (!vbl_swap) {
		DRM_ERROR("Failed to allocate memory to queue swap\n");
		drm_vblank_put(dev, pipe);
		return -ENOMEM;
	}

	vbl_swap->drw_id = swap->drawable;
	vbl_swap->pipe = pipe;
	vbl_swap->sequence = swap->sequence;

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

	list_for_each(list, &dev_priv->vbl_swaps.head) {
		vbl_old = list_entry(list, drm_i915_vbl_swap_t, head);

		if (vbl_old->drw_id == swap->drawable &&
		    vbl_old->pipe == pipe &&
		    vbl_old->sequence == swap->sequence) {
			spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);
			drm_vblank_put(dev, pipe);
			drm_free(vbl_swap, sizeof(*vbl_swap), DRM_MEM_DRIVER);
			DRM_DEBUG("Already scheduled\n");
			return 0;
		}
	}

	if (dev_priv->swaps_pending >= 10) {
		DRM_DEBUG("Too many swaps queued\n");
		DRM_DEBUG(" pipe 0: %d pipe 1: %d\n",
			  drm_vblank_count(dev, 0),
			  drm_vblank_count(dev, 1));

		list_for_each(list, &dev_priv->vbl_swaps.head) {
			vbl_old = list_entry(list, drm_i915_vbl_swap_t, head);
			DRM_DEBUG("\tdrw %x pipe %d seq %x\n",
				  vbl_old->drw_id, vbl_old->pipe,
				  vbl_old->sequence);
		}
		spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);
		drm_vblank_put(dev, pipe);
		drm_free(vbl_swap, sizeof(*vbl_swap), DRM_MEM_DRIVER);
		return -EBUSY;
	}

	list_add_tail(&vbl_swap->head, &dev_priv->vbl_swaps.head);
	dev_priv->swaps_pending++;

	spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);

	return 0;
}

/* drm_dma.h hooks
*/
void i915_driver_irq_preinstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	I915_WRITE(HWSTAM, 0xeffe);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
}

int i915_driver_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret, num_pipes = 2;

	spin_lock_init(&dev_priv->swaps_lock);
	INIT_LIST_HEAD(&dev_priv->vbl_swaps.head);
	INIT_WORK(&dev_priv->vblank_work, i915_vblank_work_handler);
	dev_priv->swaps_pending = 0;

	/* Set initial unmasked IRQs to just the selected vblank pipes. */
	dev_priv->irq_mask_reg = ~0;

	ret = drm_vblank_init(dev, num_pipes);
	if (ret)
		return ret;

	dev_priv->vblank_pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;
	dev_priv->irq_mask_reg &= ~I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT;
	dev_priv->irq_mask_reg &= ~I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT;

	dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */

	dev_priv->irq_mask_reg &= I915_INTERRUPT_ENABLE_MASK;

	I915_WRITE(IMR, dev_priv->irq_mask_reg);
	I915_WRITE(IER, I915_INTERRUPT_ENABLE_MASK);
	(void) I915_READ(IER);

	opregion_enable_asle(dev);
	DRM_INIT_WAITQUEUE(&dev_priv->irq_queue);

	return 0;
}

void i915_driver_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 temp;

	if (!dev_priv)
		return;

	dev_priv->vblank_pipe = 0;

	I915_WRITE(HWSTAM, 0xffffffff);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);

	temp = I915_READ(PIPEASTAT);
	I915_WRITE(PIPEASTAT, temp);
	temp = I915_READ(PIPEBSTAT);
	I915_WRITE(PIPEBSTAT, temp);
	temp = I915_READ(IIR);
	I915_WRITE(IIR, temp);
}
