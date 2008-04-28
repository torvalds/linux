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
 * i915_get_pipe - return the the pipe associated with a given plane
 * @dev: DRM device
 * @plane: plane to look for
 *
 * The Intel Mesa & 2D drivers call the vblank routines with a plane number
 * rather than a pipe number, since they may not always be equal.  This routine
 * maps the given @plane back to a pipe number.
 */
static int
i915_get_pipe(struct drm_device *dev, int plane)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 dspcntr;

	dspcntr = plane ? I915_READ(DSPBCNTR) : I915_READ(DSPACNTR);

	return dspcntr & DISPPLANE_SEL_PIPE_MASK ? 1 : 0;
}

/**
 * i915_get_plane - return the the plane associated with a given pipe
 * @dev: DRM device
 * @pipe: pipe to look for
 *
 * The Intel Mesa & 2D drivers call the vblank routines with a plane number
 * rather than a plane number, since they may not always be equal.  This routine
 * maps the given @pipe back to a plane number.
 */
static int
i915_get_plane(struct drm_device *dev, int pipe)
{
	if (i915_get_pipe(dev, 0) == pipe)
		return 0;
	return 1;
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
 * Emit a synchronous flip.
 *
 * This function must be called with the drawable spinlock held.
 */
static void
i915_dispatch_vsync_flip(struct drm_device *dev, struct drm_drawable_info *drw,
			 int plane)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u16 x1, y1, x2, y2;
	int pf_planes = 1 << plane;

	/* If the window is visible on the other plane, we have to flip on that
	 * plane as well.
	 */
	if (plane == 1) {
		x1 = sarea_priv->planeA_x;
		y1 = sarea_priv->planeA_y;
		x2 = x1 + sarea_priv->planeA_w;
		y2 = y1 + sarea_priv->planeA_h;
	} else {
		x1 = sarea_priv->planeB_x;
		y1 = sarea_priv->planeB_y;
		x2 = x1 + sarea_priv->planeB_w;
		y2 = y1 + sarea_priv->planeB_h;
	}

	if (x2 > 0 && y2 > 0) {
		int i, num_rects = drw->num_rects;
		struct drm_clip_rect *rect = drw->rects;

		for (i = 0; i < num_rects; i++)
			if (!(rect[i].x1 >= x2 || rect[i].y1 >= y2 ||
			      rect[i].x2 <= x1 || rect[i].y2 <= y1)) {
				pf_planes = 0x3;

				break;
			}
	}

	i915_dispatch_flip(dev, pf_planes, 1);
}

/**
 * Emit blits for scheduled buffer swaps.
 *
 * This function will be called with the HW lock held.
 */
static void i915_vblank_tasklet(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct list_head *list, *tmp, hits, *hit;
	int nhits, nrects, slice[2], upper[2], lower[2], i, num_pages;
	unsigned counter[2];
	struct drm_drawable_info *drw;
	drm_i915_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 cpp = dev_priv->cpp,  offsets[3];
	u32 cmd = (cpp == 4) ? (XY_SRC_COPY_BLT_CMD |
				XY_SRC_COPY_BLT_WRITE_ALPHA |
				XY_SRC_COPY_BLT_WRITE_RGB)
			     : XY_SRC_COPY_BLT_CMD;
	u32 src_pitch = sarea_priv->pitch * cpp;
	u32 dst_pitch = sarea_priv->pitch * cpp;
	/* COPY rop (0xcc), map cpp to magic color depth constants */
	u32 ropcpp = (0xcc << 16) | ((cpp - 1) << 24);
	RING_LOCALS;

	if (sarea_priv->front_tiled) {
		cmd |= XY_SRC_COPY_BLT_DST_TILED;
		dst_pitch >>= 2;
	}
	if (sarea_priv->back_tiled) {
		cmd |= XY_SRC_COPY_BLT_SRC_TILED;
		src_pitch >>= 2;
	}

	counter[0] = drm_vblank_count(dev, 0);
	counter[1] = drm_vblank_count(dev, 1);

	DRM_DEBUG("\n");

	INIT_LIST_HEAD(&hits);

	nhits = nrects = 0;

	/* No irqsave/restore necessary.  This tasklet may be run in an
	 * interrupt context or normal context, but we don't have to worry
	 * about getting interrupted by something acquiring the lock, because
	 * we are the interrupt context thing that acquires the lock.
	 */
	spin_lock(&dev_priv->swaps_lock);

	/* Find buffer swaps scheduled for this vertical blank */
	list_for_each_safe(list, tmp, &dev_priv->vbl_swaps.head) {
		drm_i915_vbl_swap_t *vbl_swap =
			list_entry(list, drm_i915_vbl_swap_t, head);
		int pipe = i915_get_pipe(dev, vbl_swap->plane);

		if ((counter[pipe] - vbl_swap->sequence) > (1<<23))
			continue;

		list_del(list);
		dev_priv->swaps_pending--;
		drm_vblank_put(dev, pipe);

		spin_unlock(&dev_priv->swaps_lock);
		spin_lock(&dev->drw_lock);

		drw = drm_get_drawable_info(dev, vbl_swap->drw_id);

		if (!drw) {
			spin_unlock(&dev->drw_lock);
			drm_free(vbl_swap, sizeof(*vbl_swap), DRM_MEM_DRIVER);
			spin_lock(&dev_priv->swaps_lock);
			continue;
		}

		list_for_each(hit, &hits) {
			drm_i915_vbl_swap_t *swap_cmp =
				list_entry(hit, drm_i915_vbl_swap_t, head);
			struct drm_drawable_info *drw_cmp =
				drm_get_drawable_info(dev, swap_cmp->drw_id);

			if (drw_cmp &&
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

	spin_unlock(&dev_priv->swaps_lock);

	if (nhits == 0)
		return;

	i915_kernel_lost_context(dev);

	upper[0] = upper[1] = 0;
	slice[0] = max(sarea_priv->planeA_h / nhits, 1);
	slice[1] = max(sarea_priv->planeB_h / nhits, 1);
	lower[0] = sarea_priv->planeA_y + slice[0];
	lower[1] = sarea_priv->planeB_y + slice[0];

	offsets[0] = sarea_priv->front_offset;
	offsets[1] = sarea_priv->back_offset;
	offsets[2] = sarea_priv->third_offset;
	num_pages = sarea_priv->third_handle ? 3 : 2;

	spin_lock(&dev->drw_lock);

	/* Emit blits for buffer swaps, partitioning both outputs into as many
	 * slices as there are buffer swaps scheduled in order to avoid tearing
	 * (based on the assumption that a single buffer swap would always
	 * complete before scanout starts).
	 */
	for (i = 0; i++ < nhits;
	     upper[0] = lower[0], lower[0] += slice[0],
	     upper[1] = lower[1], lower[1] += slice[1]) {
		int init_drawrect = 1;

		if (i == nhits)
			lower[0] = lower[1] = sarea_priv->height;

		list_for_each(hit, &hits) {
			drm_i915_vbl_swap_t *swap_hit =
				list_entry(hit, drm_i915_vbl_swap_t, head);
			struct drm_clip_rect *rect;
			int num_rects, plane, front, back;
			unsigned short top, bottom;

			drw = drm_get_drawable_info(dev, swap_hit->drw_id);

			if (!drw)
				continue;

			plane = swap_hit->plane;

			if (swap_hit->flip) {
				i915_dispatch_vsync_flip(dev, drw, plane);
				continue;
			}

			if (init_drawrect) {
				int width  = sarea_priv->width;
				int height = sarea_priv->height;
				if (IS_I965G(dev)) {
					BEGIN_LP_RING(4);

					OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
					OUT_RING(0);
					OUT_RING(((width - 1) & 0xffff) | ((height - 1) << 16));
					OUT_RING(0);

					ADVANCE_LP_RING();
				} else {
					BEGIN_LP_RING(6);

					OUT_RING(GFX_OP_DRAWRECT_INFO);
					OUT_RING(0);
					OUT_RING(0);
					OUT_RING(((width - 1) & 0xffff) | ((height - 1) << 16));
					OUT_RING(0);
					OUT_RING(0);

					ADVANCE_LP_RING();
				}

				sarea_priv->ctxOwner = DRM_KERNEL_CONTEXT;

				init_drawrect = 0;
			}

			rect = drw->rects;
			top = upper[plane];
			bottom = lower[plane];

			front = (dev_priv->sarea_priv->pf_current_page >>
				 (2 * plane)) & 0x3;
			back = (front + 1) % num_pages;

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
				OUT_RING(offsets[front]);
				OUT_RING((y1 << 16) | rect->x1);
				OUT_RING(src_pitch);
				OUT_RING(offsets[back]);

				ADVANCE_LP_RING();
			}
		}
	}

	spin_unlock(&dev->drw_lock);

	list_for_each_safe(hit, tmp, &hits) {
		drm_i915_vbl_swap_t *swap_hit =
			list_entry(hit, drm_i915_vbl_swap_t, head);

		list_del(hit);

		drm_free(swap_hit, sizeof(*swap_hit), DRM_MEM_DRIVER);
	}
}

u32 i915_get_vblank_counter(struct drm_device *dev, int plane)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long high_frame;
	unsigned long low_frame;
	u32 high1, high2, low, count;
	int pipe;

	pipe = i915_get_pipe(dev, plane);
	high_frame = pipe ? PIPEBFRAMEHIGH : PIPEAFRAMEHIGH;
	low_frame = pipe ? PIPEBFRAMEPIXEL : PIPEAFRAMEPIXEL;

	if (!i915_pipe_enabled(dev, pipe)) {
	    printk(KERN_ERR "trying to get vblank count for disabled "
		   "pipe %d\n", pipe);
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

	/* count may be reset by other driver(e.g. 2D driver),
	   we have no way to know if it is wrapped or resetted
	   when count is zero. do a rough guess.
	*/
	if (count == 0 && dev->last_vblank[pipe] < dev->max_vblank_count/2)
		dev->last_vblank[pipe] = 0;

	return count;
}

irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 iir;
	u32 pipea_stats, pipeb_stats;
	int vblank = 0;

	iir = I915_READ(I915REG_INT_IDENTITY_R);
	if (iir == 0) {
		DRM_DEBUG ("iir 0x%08x im 0x%08x ie 0x%08x pipea 0x%08x pipeb 0x%08x\n",
			   iir,
			   I915_READ(I915REG_INT_MASK_R),
			   I915_READ(I915REG_INT_ENABLE_R),
			   I915_READ(I915REG_PIPEASTAT),
			   I915_READ(I915REG_PIPEBSTAT));
		return IRQ_NONE;
	}

	/*
	 * Clear the PIPE(A|B)STAT regs before the IIR otherwise
	 * we may get extra interrupts.
	 */
	if (iir & I915_DISPLAY_PIPE_A_EVENT_INTERRUPT) {
		pipea_stats = I915_READ(I915REG_PIPEASTAT);
		if (pipea_stats & (I915_START_VBLANK_INTERRUPT_STATUS|
				   I915_VBLANK_INTERRUPT_STATUS))
		{
			vblank++;
			drm_handle_vblank(dev, i915_get_plane(dev, 0));
		}
		I915_WRITE(I915REG_PIPEASTAT, pipea_stats);
	}
	if (iir & I915_DISPLAY_PIPE_B_EVENT_INTERRUPT) {
		pipeb_stats = I915_READ(I915REG_PIPEBSTAT);
		if (pipeb_stats & (I915_START_VBLANK_INTERRUPT_STATUS|
				   I915_VBLANK_INTERRUPT_STATUS))
		{
			vblank++;
			drm_handle_vblank(dev, i915_get_plane(dev, 1));
		}
		I915_WRITE(I915REG_PIPEBSTAT, pipeb_stats);
	}

	if (dev_priv->sarea_priv)
	    dev_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

	I915_WRITE(I915REG_INT_IDENTITY_R, iir);
	(void) I915_READ(I915REG_INT_IDENTITY_R); /* Flush posted write */

	if (iir & I915_USER_INTERRUPT) {
		DRM_WAKEUP(&dev_priv->irq_queue);
	}
	if (vblank) {
		if (dev_priv->swaps_pending > 0)
			drm_locked_tasklet(dev, i915_vblank_tasklet);
	}

	return IRQ_HANDLED;
}

static int i915_emit_irq(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	i915_kernel_lost_context(dev);

	DRM_DEBUG("\n");

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

static int i915_wait_irq(struct drm_device * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = 0;

	DRM_DEBUG("irq_nr=%d breadcrumb=%d\n", irq_nr,
		  READ_BREADCRUMB(dev_priv));

	if (READ_BREADCRUMB(dev_priv) >= irq_nr)
		return 0;

	dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	DRM_WAIT_ON(ret, dev_priv->irq_queue, 3 * DRM_HZ,
		    READ_BREADCRUMB(dev_priv) >= irq_nr);

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

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	result = i915_emit_irq(dev);

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

int i915_enable_vblank(struct drm_device *dev, int plane)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe = i915_get_pipe(dev, plane);
	u32	pipestat_reg = 0;
	u32	pipestat;

	switch (pipe) {
	case 0:
		pipestat_reg = I915REG_PIPEASTAT;
		dev_priv->irq_enable_reg |= I915_DISPLAY_PIPE_A_EVENT_INTERRUPT;
		break;
	case 1:
		pipestat_reg = I915REG_PIPEBSTAT;
		dev_priv->irq_enable_reg |= I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
		break;
	default:
		DRM_ERROR("tried to enable vblank on non-existent pipe %d\n",
			  pipe);
		break;
	}

	if (pipestat_reg)
	{
		pipestat = I915_READ (pipestat_reg);
		/*
		 * Older chips didn't have the start vblank interrupt,
		 * but
		 */
		if (IS_I965G (dev))
			pipestat |= I915_START_VBLANK_INTERRUPT_ENABLE;
		else
			pipestat |= I915_VBLANK_INTERRUPT_ENABLE;
		/*
		 * Clear any pending status
		 */
		pipestat |= (I915_START_VBLANK_INTERRUPT_STATUS |
			     I915_VBLANK_INTERRUPT_STATUS);
		I915_WRITE(pipestat_reg, pipestat);
	}
	I915_WRITE(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);

	return 0;
}

void i915_disable_vblank(struct drm_device *dev, int plane)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe = i915_get_pipe(dev, plane);
	u32	pipestat_reg = 0;
	u32	pipestat;

	switch (pipe) {
	case 0:
		pipestat_reg = I915REG_PIPEASTAT;
		dev_priv->irq_enable_reg &= ~I915_DISPLAY_PIPE_A_EVENT_INTERRUPT;
		break;
	case 1:
		pipestat_reg = I915REG_PIPEBSTAT;
		dev_priv->irq_enable_reg &= ~I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
		break;
	default:
		DRM_ERROR("tried to disable vblank on non-existent pipe %d\n",
			  pipe);
		break;
	}

	I915_WRITE(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);
	if (pipestat_reg)
	{
		pipestat = I915_READ (pipestat_reg);
		pipestat &= ~(I915_START_VBLANK_INTERRUPT_ENABLE |
			      I915_VBLANK_INTERRUPT_ENABLE);
		/*
		 * Clear any pending status
		 */
		pipestat |= (I915_START_VBLANK_INTERRUPT_STATUS |
			     I915_VBLANK_INTERRUPT_STATUS);
		I915_WRITE(pipestat_reg, pipestat);
	}
}

static void i915_enable_interrupt (struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	dev_priv->irq_enable_reg |= I915_USER_INTERRUPT;

	I915_WRITE(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);
	dev_priv->irq_enabled = 1;
}

/* Set the vblank monitor pipe
 */
int i915_vblank_pipe_set(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t *pipe = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (pipe->pipe & ~(DRM_I915_VBLANK_PIPE_A|DRM_I915_VBLANK_PIPE_B)) {
		DRM_ERROR("called with invalid pipe 0x%x\n", pipe->pipe);
		return -EINVAL;
	}

	dev_priv->vblank_pipe = pipe->pipe;

	return 0;
}

int i915_vblank_pipe_get(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t *pipe = data;
	u16 flag;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	flag = I915_READ(I915REG_INT_ENABLE_R);
	pipe->pipe = 0;
	if (flag & I915_DISPLAY_PIPE_A_EVENT_INTERRUPT)
		pipe->pipe |= DRM_I915_VBLANK_PIPE_A;
	if (flag & I915_DISPLAY_PIPE_B_EVENT_INTERRUPT)
		pipe->pipe |= DRM_I915_VBLANK_PIPE_B;

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
	drm_i915_vbl_swap_t *vbl_swap;
	unsigned int pipe, seqtype, curseq, plane;
	unsigned long irqflags;
	struct list_head *list;
	int ret;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __func__);
		return -EINVAL;
	}

	if (!dev_priv->sarea_priv || dev_priv->sarea_priv->rotation) {
		DRM_DEBUG("Rotation not supported\n");
		return -EINVAL;
	}

	if (swap->seqtype & ~(_DRM_VBLANK_RELATIVE | _DRM_VBLANK_ABSOLUTE |
			     _DRM_VBLANK_SECONDARY | _DRM_VBLANK_NEXTONMISS |
			     _DRM_VBLANK_FLIP)) {
		DRM_ERROR("Invalid sequence type 0x%x\n", swap->seqtype);
		return -EINVAL;
	}

	plane = (swap->seqtype & _DRM_VBLANK_SECONDARY) ? 1 : 0;
	pipe = i915_get_pipe(dev, plane);

	seqtype = swap->seqtype & (_DRM_VBLANK_RELATIVE | _DRM_VBLANK_ABSOLUTE);

	if (!(dev_priv->vblank_pipe & (1 << pipe))) {
		DRM_ERROR("Invalid pipe %d\n", pipe);
		return -EINVAL;
	}

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	/* It makes no sense to schedule a swap for a drawable that doesn't have
	 * valid information at this point. E.g. this could mean that the X
	 * server is too old to push drawable information to the DRM, in which
	 * case all such swaps would become ineffective.
	 */
	if (!drm_get_drawable_info(dev, swap->drawable)) {
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		DRM_DEBUG("Invalid drawable ID %d\n", swap->drawable);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	drm_update_vblank_count(dev, pipe);
	curseq = drm_vblank_count(dev, pipe);

	if (seqtype == _DRM_VBLANK_RELATIVE)
		swap->sequence += curseq;

	if ((curseq - swap->sequence) <= (1<<23)) {
		if (swap->seqtype & _DRM_VBLANK_NEXTONMISS) {
			swap->sequence = curseq + 1;
		} else {
			DRM_DEBUG("Missed target sequence\n");
			return -EINVAL;
		}
	}

	if (swap->seqtype & _DRM_VBLANK_FLIP) {
		swap->sequence--;

		if ((curseq - swap->sequence) <= (1<<23)) {
			struct drm_drawable_info *drw;

			LOCK_TEST_WITH_RETURN(dev, file_priv);

			spin_lock_irqsave(&dev->drw_lock, irqflags);

			drw = drm_get_drawable_info(dev, swap->drawable);

			if (!drw) {
				spin_unlock_irqrestore(&dev->drw_lock,
				    irqflags);
				DRM_DEBUG("Invalid drawable ID %d\n",
					  swap->drawable);
				return -EINVAL;
			}

			i915_dispatch_vsync_flip(dev, drw, plane);

			spin_unlock_irqrestore(&dev->drw_lock, irqflags);

			return 0;
		}
	}

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

	list_for_each(list, &dev_priv->vbl_swaps.head) {
		vbl_swap = list_entry(list, drm_i915_vbl_swap_t, head);

		if (vbl_swap->drw_id == swap->drawable &&
		    vbl_swap->plane == plane &&
		    vbl_swap->sequence == swap->sequence) {
			vbl_swap->flip = (swap->seqtype & _DRM_VBLANK_FLIP);
			spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);
			DRM_DEBUG("Already scheduled\n");
			return 0;
		}
	}

	spin_unlock_irqrestore(&dev_priv->swaps_lock, irqflags);

	if (dev_priv->swaps_pending >= 100) {
		DRM_DEBUG("Too many swaps queued\n");
		return -EBUSY;
	}

	vbl_swap = drm_calloc(1, sizeof(*vbl_swap), DRM_MEM_DRIVER);

	if (!vbl_swap) {
		DRM_ERROR("Failed to allocate memory to queue swap\n");
		return -ENOMEM;
	}

	DRM_DEBUG("\n");

	ret = drm_vblank_get(dev, pipe);
	if (ret) {
		drm_free(vbl_swap, sizeof(*vbl_swap), DRM_MEM_DRIVER);
		return ret;
	}

	vbl_swap->drw_id = swap->drawable;
	vbl_swap->plane = plane;
	vbl_swap->sequence = swap->sequence;
	vbl_swap->flip = (swap->seqtype & _DRM_VBLANK_FLIP);

	if (vbl_swap->flip)
		swap->sequence++;

	spin_lock_irqsave(&dev_priv->swaps_lock, irqflags);

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

	I915_WRITE16(I915REG_HWSTAM, 0xeffe);
	I915_WRITE16(I915REG_INT_MASK_R, 0x0);
	I915_WRITE16(I915REG_INT_ENABLE_R, 0x0);
}

int i915_driver_irq_postinstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret, num_pipes = 2;

	spin_lock_init(&dev_priv->swaps_lock);
	INIT_LIST_HEAD(&dev_priv->vbl_swaps.head);
	dev_priv->swaps_pending = 0;

	dev_priv->user_irq_refcount = 0;
	dev_priv->irq_enable_reg = 0;

	ret = drm_vblank_init(dev, num_pipes);
	if (ret)
		return ret;

	dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */

	i915_enable_interrupt(dev);
	DRM_INIT_WAITQUEUE(&dev_priv->irq_queue);

	/*
	 * Initialize the hardware status page IRQ location.
	 */

	I915_WRITE(I915REG_INSTPM, (1 << 5) | (1 << 21));
	return 0;
}

void i915_driver_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 temp;

	if (!dev_priv)
		return;

	dev_priv->irq_enabled = 0;
	I915_WRITE(I915REG_HWSTAM, 0xffffffff);
	I915_WRITE(I915REG_INT_MASK_R, 0xffffffff);
	I915_WRITE(I915REG_INT_ENABLE_R, 0x0);

	temp = I915_READ(I915REG_PIPEASTAT);
	I915_WRITE(I915REG_PIPEASTAT, temp);
	temp = I915_READ(I915REG_PIPEBSTAT);
	I915_WRITE(I915REG_PIPEBSTAT, temp);
	temp = I915_READ(I915REG_INT_IDENTITY_R);
	I915_WRITE(I915REG_INT_IDENTITY_R, temp);
}
