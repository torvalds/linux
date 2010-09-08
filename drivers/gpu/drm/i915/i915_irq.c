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

#include <linux/sysrq.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

#define MAX_NOPID ((u32)~0)

/**
 * Interrupts that are always left unmasked.
 *
 * Since pipe events are edge-triggered from the PIPESTAT register to IIR,
 * we leave them always unmasked in IMR and then control enabling them through
 * PIPESTAT alone.
 */
#define I915_INTERRUPT_ENABLE_FIX			\
	(I915_ASLE_INTERRUPT |				\
	 I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |		\
	 I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |		\
	 I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |	\
	 I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT |	\
	 I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)

/** Interrupts that we mask and unmask at runtime. */
#define I915_INTERRUPT_ENABLE_VAR (I915_USER_INTERRUPT | I915_BSD_USER_INTERRUPT)

#define I915_PIPE_VBLANK_STATUS	(PIPE_START_VBLANK_INTERRUPT_STATUS |\
				 PIPE_VBLANK_INTERRUPT_STATUS)

#define I915_PIPE_VBLANK_ENABLE	(PIPE_START_VBLANK_INTERRUPT_ENABLE |\
				 PIPE_VBLANK_INTERRUPT_ENABLE)

#define DRM_I915_VBLANK_PIPE_ALL	(DRM_I915_VBLANK_PIPE_A | \
					 DRM_I915_VBLANK_PIPE_B)

void
ironlake_enable_graphics_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->gt_irq_mask_reg & mask) != 0) {
		dev_priv->gt_irq_mask_reg &= ~mask;
		I915_WRITE(GTIMR, dev_priv->gt_irq_mask_reg);
		(void) I915_READ(GTIMR);
	}
}

void
ironlake_disable_graphics_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->gt_irq_mask_reg & mask) != mask) {
		dev_priv->gt_irq_mask_reg |= mask;
		I915_WRITE(GTIMR, dev_priv->gt_irq_mask_reg);
		(void) I915_READ(GTIMR);
	}
}

/* For display hotplug interrupt */
void
ironlake_enable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != 0) {
		dev_priv->irq_mask_reg &= ~mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask_reg);
		(void) I915_READ(DEIMR);
	}
}

static inline void
ironlake_disable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != mask) {
		dev_priv->irq_mask_reg |= mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask_reg);
		(void) I915_READ(DEIMR);
	}
}

void
i915_enable_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != 0) {
		dev_priv->irq_mask_reg &= ~mask;
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
		(void) I915_READ(IMR);
	}
}

void
i915_disable_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != mask) {
		dev_priv->irq_mask_reg |= mask;
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
		(void) I915_READ(IMR);
	}
}

static inline u32
i915_pipestat(int pipe)
{
	if (pipe == 0)
		return PIPEASTAT;
	if (pipe == 1)
		return PIPEBSTAT;
	BUG();
}

void
i915_enable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != mask) {
		u32 reg = i915_pipestat(pipe);

		dev_priv->pipestat[pipe] |= mask;
		/* Enable the interrupt, clear any pending status */
		I915_WRITE(reg, dev_priv->pipestat[pipe] | (mask >> 16));
		(void) I915_READ(reg);
	}
}

void
i915_disable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != 0) {
		u32 reg = i915_pipestat(pipe);

		dev_priv->pipestat[pipe] &= ~mask;
		I915_WRITE(reg, dev_priv->pipestat[pipe]);
		(void) I915_READ(reg);
	}
}

/**
 * intel_enable_asle - enable ASLE interrupt for OpRegion
 */
void intel_enable_asle (struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (HAS_PCH_SPLIT(dev))
		ironlake_enable_display_irq(dev_priv, DE_GSE);
	else {
		i915_enable_pipestat(dev_priv, 1,
				     PIPE_LEGACY_BLC_EVENT_ENABLE);
		if (IS_I965G(dev))
			i915_enable_pipestat(dev_priv, 0,
					     PIPE_LEGACY_BLC_EVENT_ENABLE);
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
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
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

	return count;
}

u32 gm45_get_vblank_counter(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int reg = pipe ? PIPEB_FRMCOUNT_GM45 : PIPEA_FRMCOUNT_GM45;

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
					"pipe %d\n", pipe);
		return 0;
	}

	return I915_READ(reg);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
static void i915_hotplug_work_func(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    hotplug_work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_encoder *encoder;

	if (mode_config->num_encoder) {
		list_for_each_entry(encoder, &mode_config->encoder_list, head) {
			struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	
			if (intel_encoder->hot_plug)
				(*intel_encoder->hot_plug) (intel_encoder);
		}
	}
	/* Just fire off a uevent and let userspace tell us what to do */
	drm_helper_hpd_irq_event(dev);
}

static void i915_handle_rps_change(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 busy_up, busy_down, max_avg, min_avg;
	u8 new_delay = dev_priv->cur_delay;

	I915_WRITE16(MEMINTRSTS, MEMINT_EVAL_CHG);
	busy_up = I915_READ(RCPREVBSYTUPAVG);
	busy_down = I915_READ(RCPREVBSYTDNAVG);
	max_avg = I915_READ(RCBMAXAVG);
	min_avg = I915_READ(RCBMINAVG);

	/* Handle RCS change request from hw */
	if (busy_up > max_avg) {
		if (dev_priv->cur_delay != dev_priv->max_delay)
			new_delay = dev_priv->cur_delay - 1;
		if (new_delay < dev_priv->max_delay)
			new_delay = dev_priv->max_delay;
	} else if (busy_down < min_avg) {
		if (dev_priv->cur_delay != dev_priv->min_delay)
			new_delay = dev_priv->cur_delay + 1;
		if (new_delay > dev_priv->min_delay)
			new_delay = dev_priv->min_delay;
	}

	if (ironlake_set_drps(dev, new_delay))
		dev_priv->cur_delay = new_delay;

	return;
}

irqreturn_t ironlake_irq_handler(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = IRQ_NONE;
	u32 de_iir, gt_iir, de_ier, pch_iir;
	struct drm_i915_master_private *master_priv;
	struct intel_ring_buffer *render_ring = &dev_priv->render_ring;

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);
	(void)I915_READ(DEIER);

	de_iir = I915_READ(DEIIR);
	gt_iir = I915_READ(GTIIR);
	pch_iir = I915_READ(SDEIIR);

	if (de_iir == 0 && gt_iir == 0 && pch_iir == 0)
		goto done;

	ret = IRQ_HANDLED;

	if (dev->primary->master) {
		master_priv = dev->primary->master->driver_priv;
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch =
				READ_BREADCRUMB(dev_priv);
	}

	if (gt_iir & GT_PIPE_NOTIFY) {
		u32 seqno = render_ring->get_gem_seqno(dev, render_ring);
		render_ring->irq_gem_seqno = seqno;
		trace_i915_gem_request_complete(dev, seqno);
		DRM_WAKEUP(&dev_priv->render_ring.irq_queue);
		dev_priv->hangcheck_count = 0;
		mod_timer(&dev_priv->hangcheck_timer, jiffies + DRM_I915_HANGCHECK_PERIOD);
	}
	if (gt_iir & GT_BSD_USER_INTERRUPT)
		DRM_WAKEUP(&dev_priv->bsd_ring.irq_queue);


	if (de_iir & DE_GSE)
		ironlake_opregion_gse_intr(dev);

	if (de_iir & DE_PLANEA_FLIP_DONE) {
		intel_prepare_page_flip(dev, 0);
		intel_finish_page_flip(dev, 0);
	}

	if (de_iir & DE_PLANEB_FLIP_DONE) {
		intel_prepare_page_flip(dev, 1);
		intel_finish_page_flip(dev, 1);
	}

	if (de_iir & DE_PIPEA_VBLANK)
		drm_handle_vblank(dev, 0);

	if (de_iir & DE_PIPEB_VBLANK)
		drm_handle_vblank(dev, 1);

	/* check event from PCH */
	if ((de_iir & DE_PCH_EVENT) &&
	    (pch_iir & SDE_HOTPLUG_MASK)) {
		queue_work(dev_priv->wq, &dev_priv->hotplug_work);
	}

	if (de_iir & DE_PCU_EVENT) {
		I915_WRITE16(MEMINTRSTS, I915_READ(MEMINTRSTS));
		i915_handle_rps_change(dev);
	}

	/* should clear PCH hotplug event before clear CPU irq */
	I915_WRITE(SDEIIR, pch_iir);
	I915_WRITE(GTIIR, gt_iir);
	I915_WRITE(DEIIR, de_iir);

done:
	I915_WRITE(DEIER, de_ier);
	(void)I915_READ(DEIER);

	return ret;
}

/**
 * i915_error_work_func - do process context error handling work
 * @work: work struct
 *
 * Fire an error uevent so userspace can see that a hang or error
 * was detected.
 */
static void i915_error_work_func(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    error_work);
	struct drm_device *dev = dev_priv->dev;
	char *error_event[] = { "ERROR=1", NULL };
	char *reset_event[] = { "RESET=1", NULL };
	char *reset_done_event[] = { "ERROR=0", NULL };

	DRM_DEBUG_DRIVER("generating error event\n");
	kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, error_event);

	if (atomic_read(&dev_priv->mm.wedged)) {
		if (IS_I965G(dev)) {
			DRM_DEBUG_DRIVER("resetting chip\n");
			kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, reset_event);
			if (!i965_reset(dev, GDRST_RENDER)) {
				atomic_set(&dev_priv->mm.wedged, 0);
				kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, reset_done_event);
			}
		} else {
			DRM_DEBUG_DRIVER("reboot required\n");
		}
	}
}

static struct drm_i915_error_object *
i915_error_object_create(struct drm_device *dev,
			 struct drm_gem_object *src)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_error_object *dst;
	struct drm_i915_gem_object *src_priv;
	int page, page_count;
	u32 reloc_offset;

	if (src == NULL)
		return NULL;

	src_priv = to_intel_bo(src);
	if (src_priv->pages == NULL)
		return NULL;

	page_count = src->size / PAGE_SIZE;

	dst = kmalloc(sizeof(*dst) + page_count * sizeof (u32 *), GFP_ATOMIC);
	if (dst == NULL)
		return NULL;

	reloc_offset = src_priv->gtt_offset;
	for (page = 0; page < page_count; page++) {
		unsigned long flags;
		void __iomem *s;
		void *d;

		d = kmalloc(PAGE_SIZE, GFP_ATOMIC);
		if (d == NULL)
			goto unwind;

		local_irq_save(flags);
		s = io_mapping_map_atomic_wc(dev_priv->mm.gtt_mapping,
					     reloc_offset,
					     KM_IRQ0);
		memcpy_fromio(d, s, PAGE_SIZE);
		io_mapping_unmap_atomic(s, KM_IRQ0);
		local_irq_restore(flags);

		dst->pages[page] = d;

		reloc_offset += PAGE_SIZE;
	}
	dst->page_count = page_count;
	dst->gtt_offset = src_priv->gtt_offset;

	return dst;

unwind:
	while (page--)
		kfree(dst->pages[page]);
	kfree(dst);
	return NULL;
}

static void
i915_error_object_free(struct drm_i915_error_object *obj)
{
	int page;

	if (obj == NULL)
		return;

	for (page = 0; page < obj->page_count; page++)
		kfree(obj->pages[page]);

	kfree(obj);
}

static void
i915_error_state_free(struct drm_device *dev,
		      struct drm_i915_error_state *error)
{
	i915_error_object_free(error->batchbuffer[0]);
	i915_error_object_free(error->batchbuffer[1]);
	i915_error_object_free(error->ringbuffer);
	kfree(error->active_bo);
	kfree(error->overlay);
	kfree(error);
}

static u32
i915_get_bbaddr(struct drm_device *dev, u32 *ring)
{
	u32 cmd;

	if (IS_I830(dev) || IS_845G(dev))
		cmd = MI_BATCH_BUFFER;
	else if (IS_I965G(dev))
		cmd = (MI_BATCH_BUFFER_START | (2 << 6) |
		       MI_BATCH_NON_SECURE_I965);
	else
		cmd = (MI_BATCH_BUFFER_START | (2 << 6));

	return ring[0] == cmd ? ring[1] : 0;
}

static u32
i915_ringbuffer_last_batch(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 head, bbaddr;
	u32 *ring;

	/* Locate the current position in the ringbuffer and walk back
	 * to find the most recently dispatched batch buffer.
	 */
	bbaddr = 0;
	head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
	ring = (u32 *)(dev_priv->render_ring.virtual_start + head);

	while (--ring >= (u32 *)dev_priv->render_ring.virtual_start) {
		bbaddr = i915_get_bbaddr(dev, ring);
		if (bbaddr)
			break;
	}

	if (bbaddr == 0) {
		ring = (u32 *)(dev_priv->render_ring.virtual_start
				+ dev_priv->render_ring.size);
		while (--ring >= (u32 *)dev_priv->render_ring.virtual_start) {
			bbaddr = i915_get_bbaddr(dev, ring);
			if (bbaddr)
				break;
		}
	}

	return bbaddr;
}

/**
 * i915_capture_error_state - capture an error record for later analysis
 * @dev: drm device
 *
 * Should be called when an error is detected (either a hang or an error
 * interrupt) to capture error state from the time of the error.  Fills
 * out a structure which becomes available in debugfs for user level tools
 * to pick up.
 */
static void i915_capture_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	struct drm_i915_error_state *error;
	struct drm_gem_object *batchbuffer[2];
	unsigned long flags;
	u32 bbaddr;
	int count;

	spin_lock_irqsave(&dev_priv->error_lock, flags);
	error = dev_priv->first_error;
	spin_unlock_irqrestore(&dev_priv->error_lock, flags);
	if (error)
		return;

	error = kmalloc(sizeof(*error), GFP_ATOMIC);
	if (!error) {
		DRM_DEBUG_DRIVER("out of memory, not capturing error state\n");
		return;
	}

	error->seqno = i915_get_gem_seqno(dev, &dev_priv->render_ring);
	error->eir = I915_READ(EIR);
	error->pgtbl_er = I915_READ(PGTBL_ER);
	error->pipeastat = I915_READ(PIPEASTAT);
	error->pipebstat = I915_READ(PIPEBSTAT);
	error->instpm = I915_READ(INSTPM);
	if (!IS_I965G(dev)) {
		error->ipeir = I915_READ(IPEIR);
		error->ipehr = I915_READ(IPEHR);
		error->instdone = I915_READ(INSTDONE);
		error->acthd = I915_READ(ACTHD);
		error->bbaddr = 0;
	} else {
		error->ipeir = I915_READ(IPEIR_I965);
		error->ipehr = I915_READ(IPEHR_I965);
		error->instdone = I915_READ(INSTDONE_I965);
		error->instps = I915_READ(INSTPS);
		error->instdone1 = I915_READ(INSTDONE1);
		error->acthd = I915_READ(ACTHD_I965);
		error->bbaddr = I915_READ64(BB_ADDR);
	}

	bbaddr = i915_ringbuffer_last_batch(dev);

	/* Grab the current batchbuffer, most likely to have crashed. */
	batchbuffer[0] = NULL;
	batchbuffer[1] = NULL;
	count = 0;
	list_for_each_entry(obj_priv,
			&dev_priv->render_ring.active_list, list) {

		struct drm_gem_object *obj = &obj_priv->base;

		if (batchbuffer[0] == NULL &&
		    bbaddr >= obj_priv->gtt_offset &&
		    bbaddr < obj_priv->gtt_offset + obj->size)
			batchbuffer[0] = obj;

		if (batchbuffer[1] == NULL &&
		    error->acthd >= obj_priv->gtt_offset &&
		    error->acthd < obj_priv->gtt_offset + obj->size)
			batchbuffer[1] = obj;

		count++;
	}
	/* Scan the other lists for completeness for those bizarre errors. */
	if (batchbuffer[0] == NULL || batchbuffer[1] == NULL) {
		list_for_each_entry(obj_priv, &dev_priv->mm.flushing_list, list) {
			struct drm_gem_object *obj = &obj_priv->base;

			if (batchbuffer[0] == NULL &&
			    bbaddr >= obj_priv->gtt_offset &&
			    bbaddr < obj_priv->gtt_offset + obj->size)
				batchbuffer[0] = obj;

			if (batchbuffer[1] == NULL &&
			    error->acthd >= obj_priv->gtt_offset &&
			    error->acthd < obj_priv->gtt_offset + obj->size)
				batchbuffer[1] = obj;

			if (batchbuffer[0] && batchbuffer[1])
				break;
		}
	}
	if (batchbuffer[0] == NULL || batchbuffer[1] == NULL) {
		list_for_each_entry(obj_priv, &dev_priv->mm.inactive_list, list) {
			struct drm_gem_object *obj = &obj_priv->base;

			if (batchbuffer[0] == NULL &&
			    bbaddr >= obj_priv->gtt_offset &&
			    bbaddr < obj_priv->gtt_offset + obj->size)
				batchbuffer[0] = obj;

			if (batchbuffer[1] == NULL &&
			    error->acthd >= obj_priv->gtt_offset &&
			    error->acthd < obj_priv->gtt_offset + obj->size)
				batchbuffer[1] = obj;

			if (batchbuffer[0] && batchbuffer[1])
				break;
		}
	}

	/* We need to copy these to an anonymous buffer as the simplest
	 * method to avoid being overwritten by userpace.
	 */
	error->batchbuffer[0] = i915_error_object_create(dev, batchbuffer[0]);
	if (batchbuffer[1] != batchbuffer[0])
		error->batchbuffer[1] = i915_error_object_create(dev, batchbuffer[1]);
	else
		error->batchbuffer[1] = NULL;

	/* Record the ringbuffer */
	error->ringbuffer = i915_error_object_create(dev,
			dev_priv->render_ring.gem_object);

	/* Record buffers on the active list. */
	error->active_bo = NULL;
	error->active_bo_count = 0;

	if (count)
		error->active_bo = kmalloc(sizeof(*error->active_bo)*count,
					   GFP_ATOMIC);

	if (error->active_bo) {
		int i = 0;
		list_for_each_entry(obj_priv,
				&dev_priv->render_ring.active_list, list) {
			struct drm_gem_object *obj = &obj_priv->base;

			error->active_bo[i].size = obj->size;
			error->active_bo[i].name = obj->name;
			error->active_bo[i].seqno = obj_priv->last_rendering_seqno;
			error->active_bo[i].gtt_offset = obj_priv->gtt_offset;
			error->active_bo[i].read_domains = obj->read_domains;
			error->active_bo[i].write_domain = obj->write_domain;
			error->active_bo[i].fence_reg = obj_priv->fence_reg;
			error->active_bo[i].pinned = 0;
			if (obj_priv->pin_count > 0)
				error->active_bo[i].pinned = 1;
			if (obj_priv->user_pin_count > 0)
				error->active_bo[i].pinned = -1;
			error->active_bo[i].tiling = obj_priv->tiling_mode;
			error->active_bo[i].dirty = obj_priv->dirty;
			error->active_bo[i].purgeable = obj_priv->madv != I915_MADV_WILLNEED;

			if (++i == count)
				break;
		}
		error->active_bo_count = i;
	}

	do_gettimeofday(&error->time);

	error->overlay = intel_overlay_capture_error_state(dev);

	spin_lock_irqsave(&dev_priv->error_lock, flags);
	if (dev_priv->first_error == NULL) {
		dev_priv->first_error = error;
		error = NULL;
	}
	spin_unlock_irqrestore(&dev_priv->error_lock, flags);

	if (error)
		i915_error_state_free(dev, error);
}

void i915_destroy_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;

	spin_lock(&dev_priv->error_lock);
	error = dev_priv->first_error;
	dev_priv->first_error = NULL;
	spin_unlock(&dev_priv->error_lock);

	if (error)
		i915_error_state_free(dev, error);
}

static void i915_report_and_clear_eir(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 eir = I915_READ(EIR);

	if (!eir)
		return;

	printk(KERN_ERR "render error detected, EIR: 0x%08x\n",
	       eir);

	if (IS_G4X(dev)) {
		if (eir & (GM45_ERROR_MEM_PRIV | GM45_ERROR_CP_PRIV)) {
			u32 ipeir = I915_READ(IPEIR_I965);

			printk(KERN_ERR "  IPEIR: 0x%08x\n",
			       I915_READ(IPEIR_I965));
			printk(KERN_ERR "  IPEHR: 0x%08x\n",
			       I915_READ(IPEHR_I965));
			printk(KERN_ERR "  INSTDONE: 0x%08x\n",
			       I915_READ(INSTDONE_I965));
			printk(KERN_ERR "  INSTPS: 0x%08x\n",
			       I915_READ(INSTPS));
			printk(KERN_ERR "  INSTDONE1: 0x%08x\n",
			       I915_READ(INSTDONE1));
			printk(KERN_ERR "  ACTHD: 0x%08x\n",
			       I915_READ(ACTHD_I965));
			I915_WRITE(IPEIR_I965, ipeir);
			(void)I915_READ(IPEIR_I965);
		}
		if (eir & GM45_ERROR_PAGE_TABLE) {
			u32 pgtbl_err = I915_READ(PGTBL_ER);
			printk(KERN_ERR "page table error\n");
			printk(KERN_ERR "  PGTBL_ER: 0x%08x\n",
			       pgtbl_err);
			I915_WRITE(PGTBL_ER, pgtbl_err);
			(void)I915_READ(PGTBL_ER);
		}
	}

	if (IS_I9XX(dev)) {
		if (eir & I915_ERROR_PAGE_TABLE) {
			u32 pgtbl_err = I915_READ(PGTBL_ER);
			printk(KERN_ERR "page table error\n");
			printk(KERN_ERR "  PGTBL_ER: 0x%08x\n",
			       pgtbl_err);
			I915_WRITE(PGTBL_ER, pgtbl_err);
			(void)I915_READ(PGTBL_ER);
		}
	}

	if (eir & I915_ERROR_MEMORY_REFRESH) {
		u32 pipea_stats = I915_READ(PIPEASTAT);
		u32 pipeb_stats = I915_READ(PIPEBSTAT);

		printk(KERN_ERR "memory refresh error\n");
		printk(KERN_ERR "PIPEASTAT: 0x%08x\n",
		       pipea_stats);
		printk(KERN_ERR "PIPEBSTAT: 0x%08x\n",
		       pipeb_stats);
		/* pipestat has already been acked */
	}
	if (eir & I915_ERROR_INSTRUCTION) {
		printk(KERN_ERR "instruction error\n");
		printk(KERN_ERR "  INSTPM: 0x%08x\n",
		       I915_READ(INSTPM));
		if (!IS_I965G(dev)) {
			u32 ipeir = I915_READ(IPEIR);

			printk(KERN_ERR "  IPEIR: 0x%08x\n",
			       I915_READ(IPEIR));
			printk(KERN_ERR "  IPEHR: 0x%08x\n",
			       I915_READ(IPEHR));
			printk(KERN_ERR "  INSTDONE: 0x%08x\n",
			       I915_READ(INSTDONE));
			printk(KERN_ERR "  ACTHD: 0x%08x\n",
			       I915_READ(ACTHD));
			I915_WRITE(IPEIR, ipeir);
			(void)I915_READ(IPEIR);
		} else {
			u32 ipeir = I915_READ(IPEIR_I965);

			printk(KERN_ERR "  IPEIR: 0x%08x\n",
			       I915_READ(IPEIR_I965));
			printk(KERN_ERR "  IPEHR: 0x%08x\n",
			       I915_READ(IPEHR_I965));
			printk(KERN_ERR "  INSTDONE: 0x%08x\n",
			       I915_READ(INSTDONE_I965));
			printk(KERN_ERR "  INSTPS: 0x%08x\n",
			       I915_READ(INSTPS));
			printk(KERN_ERR "  INSTDONE1: 0x%08x\n",
			       I915_READ(INSTDONE1));
			printk(KERN_ERR "  ACTHD: 0x%08x\n",
			       I915_READ(ACTHD_I965));
			I915_WRITE(IPEIR_I965, ipeir);
			(void)I915_READ(IPEIR_I965);
		}
	}

	I915_WRITE(EIR, eir);
	(void)I915_READ(EIR);
	eir = I915_READ(EIR);
	if (eir) {
		/*
		 * some errors might have become stuck,
		 * mask them.
		 */
		DRM_ERROR("EIR stuck: 0x%08x, masking\n", eir);
		I915_WRITE(EMR, I915_READ(EMR) | eir);
		I915_WRITE(IIR, I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT);
	}
}

/**
 * i915_handle_error - handle an error interrupt
 * @dev: drm device
 *
 * Do some basic checking of regsiter state at error interrupt time and
 * dump it to the syslog.  Also call i915_capture_error_state() to make
 * sure we get a record and make it available in debugfs.  Fire a uevent
 * so userspace knows something bad happened (should trigger collection
 * of a ring dump etc.).
 */
static void i915_handle_error(struct drm_device *dev, bool wedged)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	i915_capture_error_state(dev);
	i915_report_and_clear_eir(dev);

	if (wedged) {
		atomic_set(&dev_priv->mm.wedged, 1);

		/*
		 * Wakeup waiting processes so they don't hang
		 */
		DRM_WAKEUP(&dev_priv->render_ring.irq_queue);
	}

	queue_work(dev_priv->wq, &dev_priv->error_work);
}

static void i915_pageflip_stall_check(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_gem_object *obj_priv;
	struct intel_unpin_work *work;
	unsigned long flags;
	bool stall_detected;

	/* Ignore early vblank irqs */
	if (intel_crtc == NULL)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	work = intel_crtc->unpin_work;

	if (work == NULL || work->pending || !work->enable_stall_check) {
		/* Either the pending flip IRQ arrived, or we're too early. Don't check */
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return;
	}

	/* Potential stall - if we see that the flip has happened, assume a missed interrupt */
	obj_priv = to_intel_bo(work->pending_flip_obj);
	if(IS_I965G(dev)) {
		int dspsurf = intel_crtc->plane == 0 ? DSPASURF : DSPBSURF;
		stall_detected = I915_READ(dspsurf) == obj_priv->gtt_offset;
	} else {
		int dspaddr = intel_crtc->plane == 0 ? DSPAADDR : DSPBADDR;
		stall_detected = I915_READ(dspaddr) == (obj_priv->gtt_offset +
							crtc->y * crtc->fb->pitch +
							crtc->x * crtc->fb->bits_per_pixel/8);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (stall_detected) {
		DRM_DEBUG_DRIVER("Pageflip stall detected\n");
		intel_prepare_page_flip(dev, intel_crtc->plane);
	}
}

irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv;
	u32 iir, new_iir;
	u32 pipea_stats, pipeb_stats;
	u32 vblank_status;
	int vblank = 0;
	unsigned long irqflags;
	int irq_received;
	int ret = IRQ_NONE;
	struct intel_ring_buffer *render_ring = &dev_priv->render_ring;

	atomic_inc(&dev_priv->irq_received);

	if (HAS_PCH_SPLIT(dev))
		return ironlake_irq_handler(dev);

	iir = I915_READ(IIR);

	if (IS_I965G(dev))
		vblank_status = PIPE_START_VBLANK_INTERRUPT_STATUS;
	else
		vblank_status = PIPE_VBLANK_INTERRUPT_STATUS;

	for (;;) {
		irq_received = iir != 0;

		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
		pipea_stats = I915_READ(PIPEASTAT);
		pipeb_stats = I915_READ(PIPEBSTAT);

		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			i915_handle_error(dev, false);

		/*
		 * Clear the PIPE(A|B)STAT regs before the IIR
		 */
		if (pipea_stats & 0x8000ffff) {
			if (pipea_stats &  PIPE_FIFO_UNDERRUN_STATUS)
				DRM_DEBUG_DRIVER("pipe a underrun\n");
			I915_WRITE(PIPEASTAT, pipea_stats);
			irq_received = 1;
		}

		if (pipeb_stats & 0x8000ffff) {
			if (pipeb_stats &  PIPE_FIFO_UNDERRUN_STATUS)
				DRM_DEBUG_DRIVER("pipe b underrun\n");
			I915_WRITE(PIPEBSTAT, pipeb_stats);
			irq_received = 1;
		}
		spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);

		if (!irq_received)
			break;

		ret = IRQ_HANDLED;

		/* Consume port.  Then clear IIR or we'll miss events */
		if ((I915_HAS_HOTPLUG(dev)) &&
		    (iir & I915_DISPLAY_PORT_INTERRUPT)) {
			u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);

			DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x\n",
				  hotplug_status);
			if (hotplug_status & dev_priv->hotplug_supported_mask)
				queue_work(dev_priv->wq,
					   &dev_priv->hotplug_work);

			I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
			I915_READ(PORT_HOTPLUG_STAT);
		}

		I915_WRITE(IIR, iir);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (dev->primary->master) {
			master_priv = dev->primary->master->driver_priv;
			if (master_priv->sarea_priv)
				master_priv->sarea_priv->last_dispatch =
					READ_BREADCRUMB(dev_priv);
		}

		if (iir & I915_USER_INTERRUPT) {
			u32 seqno =
				render_ring->get_gem_seqno(dev, render_ring);
			render_ring->irq_gem_seqno = seqno;
			trace_i915_gem_request_complete(dev, seqno);
			DRM_WAKEUP(&dev_priv->render_ring.irq_queue);
			dev_priv->hangcheck_count = 0;
			mod_timer(&dev_priv->hangcheck_timer, jiffies + DRM_I915_HANGCHECK_PERIOD);
		}

		if (HAS_BSD(dev) && (iir & I915_BSD_USER_INTERRUPT))
			DRM_WAKEUP(&dev_priv->bsd_ring.irq_queue);

		if (iir & I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT) {
			intel_prepare_page_flip(dev, 0);
			if (dev_priv->flip_pending_is_done)
				intel_finish_page_flip_plane(dev, 0);
		}

		if (iir & I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT) {
			intel_prepare_page_flip(dev, 1);
			if (dev_priv->flip_pending_is_done)
				intel_finish_page_flip_plane(dev, 1);
		}

		if (pipea_stats & vblank_status) {
			vblank++;
			drm_handle_vblank(dev, 0);
			if (!dev_priv->flip_pending_is_done) {
				i915_pageflip_stall_check(dev, 0);
				intel_finish_page_flip(dev, 0);
			}
		}

		if (pipeb_stats & vblank_status) {
			vblank++;
			drm_handle_vblank(dev, 1);
			if (!dev_priv->flip_pending_is_done) {
				i915_pageflip_stall_check(dev, 1);
				intel_finish_page_flip(dev, 1);
			}
		}

		if ((pipea_stats & PIPE_LEGACY_BLC_EVENT_STATUS) ||
		    (pipeb_stats & PIPE_LEGACY_BLC_EVENT_STATUS) ||
		    (iir & I915_ASLE_INTERRUPT))
			opregion_asle_intr(dev);

		/* With MSI, interrupts are only generated when iir
		 * transitions from zero to nonzero.  If another bit got
		 * set while we were handling the existing iir bits, then
		 * we would never get another interrupt.
		 *
		 * This is fine on non-MSI as well, as if we hit this path
		 * we avoid exiting the interrupt handler only to generate
		 * another one.
		 *
		 * Note that for MSI this could cause a stray interrupt report
		 * if an interrupt landed in the time between writing IIR and
		 * the posting read.  This should be rare enough to never
		 * trigger the 99% of 100,000 interrupts test for disabling
		 * stray interrupts.
		 */
		iir = new_iir;
	}

	return ret;
}

static int i915_emit_irq(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;

	i915_kernel_lost_context(dev);

	DRM_DEBUG_DRIVER("\n");

	dev_priv->counter++;
	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->counter = 1;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->last_enqueue = dev_priv->counter;

	BEGIN_LP_RING(4);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(MI_USER_INTERRUPT);
	ADVANCE_LP_RING();

	return dev_priv->counter;
}

void i915_trace_irq_get(struct drm_device *dev, u32 seqno)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct intel_ring_buffer *render_ring = &dev_priv->render_ring;

	if (dev_priv->trace_irq_seqno == 0)
		render_ring->user_irq_get(dev, render_ring);

	dev_priv->trace_irq_seqno = seqno;
}

static int i915_wait_irq(struct drm_device * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	int ret = 0;
	struct intel_ring_buffer *render_ring = &dev_priv->render_ring;

	DRM_DEBUG_DRIVER("irq_nr=%d breadcrumb=%d\n", irq_nr,
		  READ_BREADCRUMB(dev_priv));

	if (READ_BREADCRUMB(dev_priv) >= irq_nr) {
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
		return 0;
	}

	if (master_priv->sarea_priv)
		master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	render_ring->user_irq_get(dev, render_ring);
	DRM_WAIT_ON(ret, dev_priv->render_ring.irq_queue, 3 * DRM_HZ,
		    READ_BREADCRUMB(dev_priv) >= irq_nr);
	render_ring->user_irq_put(dev, render_ring);

	if (ret == -EBUSY) {
		DRM_ERROR("EBUSY -- rec: %d emitted: %d\n",
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->counter);
	}

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

	if (!dev_priv || !dev_priv->render_ring.virtual_start) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

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
	unsigned long irqflags;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	u32 pipeconf;

	pipeconf = I915_READ(pipeconf_reg);
	if (!(pipeconf & PIPEACONF_ENABLE))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (HAS_PCH_SPLIT(dev))
		ironlake_enable_display_irq(dev_priv, (pipe == 0) ? 
					    DE_PIPEA_VBLANK: DE_PIPEB_VBLANK);
	else if (IS_I965G(dev))
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_START_VBLANK_INTERRUPT_ENABLE);
	else
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_VBLANK_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
void i915_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (HAS_PCH_SPLIT(dev))
		ironlake_disable_display_irq(dev_priv, (pipe == 0) ? 
					     DE_PIPEA_VBLANK: DE_PIPEB_VBLANK);
	else
		i915_disable_pipestat(dev_priv, pipe,
				      PIPE_VBLANK_INTERRUPT_ENABLE |
				      PIPE_START_VBLANK_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

void i915_enable_interrupt (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!HAS_PCH_SPLIT(dev))
		opregion_enable_asle(dev);
	dev_priv->irq_enabled = 1;
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
	/* The delayed swap mechanism was fundamentally racy, and has been
	 * removed.  The model was that the client requested a delayed flip/swap
	 * from the kernel, then waited for vblank before continuing to perform
	 * rendering.  The problem was that the kernel might wake the client
	 * up before it dispatched the vblank swap (since the lock has to be
	 * held while touching the ringbuffer), in which case the client would
	 * clear and start the next frame before the swap occurred, and
	 * flicker would occur in addition to likely missing the vblank.
	 *
	 * In the absence of this ioctl, userland falls back to a correct path
	 * of waiting for a vblank, then dispatching the swap on its own.
	 * Context switching to userland and back is plenty fast enough for
	 * meeting the requirements of vblank swapping.
	 */
	return -EINVAL;
}

struct drm_i915_gem_request *
i915_get_tail_request(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return list_entry(dev_priv->render_ring.request_list.prev,
			struct drm_i915_gem_request, list);
}

/**
 * This is called when the chip hasn't reported back with completed
 * batchbuffers in a long time. The first time this is called we simply record
 * ACTHD. If ACTHD hasn't changed by the time the hangcheck timer elapses
 * again, we assume the chip is wedged and try to fix it.
 */
void i915_hangcheck_elapsed(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t acthd, instdone, instdone1;

	/* No reset support on this chip yet. */
	if (IS_GEN6(dev))
		return;

	if (!IS_I965G(dev)) {
		acthd = I915_READ(ACTHD);
		instdone = I915_READ(INSTDONE);
		instdone1 = 0;
	} else {
		acthd = I915_READ(ACTHD_I965);
		instdone = I915_READ(INSTDONE_I965);
		instdone1 = I915_READ(INSTDONE1);
	}

	/* If all work is done then ACTHD clearly hasn't advanced. */
	if (list_empty(&dev_priv->render_ring.request_list) ||
		i915_seqno_passed(i915_get_gem_seqno(dev,
				&dev_priv->render_ring),
			i915_get_tail_request(dev)->seqno)) {
		bool missed_wakeup = false;

		dev_priv->hangcheck_count = 0;

		/* Issue a wake-up to catch stuck h/w. */
		if (dev_priv->render_ring.waiting_gem_seqno &&
		    waitqueue_active(&dev_priv->render_ring.irq_queue)) {
			DRM_WAKEUP(&dev_priv->render_ring.irq_queue);
			missed_wakeup = true;
		}

		if (dev_priv->bsd_ring.waiting_gem_seqno &&
		    waitqueue_active(&dev_priv->bsd_ring.irq_queue)) {
			DRM_WAKEUP(&dev_priv->bsd_ring.irq_queue);
			missed_wakeup = true;
		}

		if (missed_wakeup)
			DRM_ERROR("Hangcheck timer elapsed... GPU idle, missed IRQ.\n");
		return;
	}

	if (dev_priv->last_acthd == acthd &&
	    dev_priv->last_instdone == instdone &&
	    dev_priv->last_instdone1 == instdone1) {
		if (dev_priv->hangcheck_count++ > 1) {
			DRM_ERROR("Hangcheck timer elapsed... GPU hung\n");
			i915_handle_error(dev, true);
			return;
		}
	} else {
		dev_priv->hangcheck_count = 0;

		dev_priv->last_acthd = acthd;
		dev_priv->last_instdone = instdone;
		dev_priv->last_instdone1 = instdone1;
	}

	/* Reset timer case chip hangs without another request being added */
	mod_timer(&dev_priv->hangcheck_timer, jiffies + DRM_I915_HANGCHECK_PERIOD);
}

/* drm_dma.h hooks
*/
static void ironlake_irq_preinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	I915_WRITE(HWSTAM, 0xeffe);

	/* XXX hotplug from PCH */

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	(void) I915_READ(DEIER);

	/* and GT */
	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	(void) I915_READ(GTIER);

	/* south display irq */
	I915_WRITE(SDEIMR, 0xffffffff);
	I915_WRITE(SDEIER, 0x0);
	(void) I915_READ(SDEIER);
}

static int ironlake_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* enable kind of interrupts always enabled */
	u32 display_mask = DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
			   DE_PLANEA_FLIP_DONE | DE_PLANEB_FLIP_DONE;
	u32 render_mask = GT_PIPE_NOTIFY | GT_BSD_USER_INTERRUPT;
	u32 hotplug_mask = SDE_CRT_HOTPLUG | SDE_PORTB_HOTPLUG |
			   SDE_PORTC_HOTPLUG | SDE_PORTD_HOTPLUG;

	dev_priv->irq_mask_reg = ~display_mask;
	dev_priv->de_irq_enable_reg = display_mask | DE_PIPEA_VBLANK | DE_PIPEB_VBLANK;

	/* should always can generate irq */
	I915_WRITE(DEIIR, I915_READ(DEIIR));
	I915_WRITE(DEIMR, dev_priv->irq_mask_reg);
	I915_WRITE(DEIER, dev_priv->de_irq_enable_reg);
	(void) I915_READ(DEIER);

	/* Gen6 only needs render pipe_control now */
	if (IS_GEN6(dev))
		render_mask = GT_PIPE_NOTIFY;

	dev_priv->gt_irq_mask_reg = ~render_mask;
	dev_priv->gt_irq_enable_reg = render_mask;

	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask_reg);
	if (IS_GEN6(dev))
		I915_WRITE(GEN6_RENDER_IMR, ~GEN6_RENDER_PIPE_CONTROL_NOTIFY_INTERRUPT);
	I915_WRITE(GTIER, dev_priv->gt_irq_enable_reg);
	(void) I915_READ(GTIER);

	dev_priv->pch_irq_mask_reg = ~hotplug_mask;
	dev_priv->pch_irq_enable_reg = hotplug_mask;

	I915_WRITE(SDEIIR, I915_READ(SDEIIR));
	I915_WRITE(SDEIMR, dev_priv->pch_irq_mask_reg);
	I915_WRITE(SDEIER, dev_priv->pch_irq_enable_reg);
	(void) I915_READ(SDEIER);

	if (IS_IRONLAKE_M(dev)) {
		/* Clear & enable PCU event interrupts */
		I915_WRITE(DEIIR, DE_PCU_EVENT);
		I915_WRITE(DEIER, I915_READ(DEIER) | DE_PCU_EVENT);
		ironlake_enable_display_irq(dev_priv, DE_PCU_EVENT);
	}

	return 0;
}

void i915_driver_irq_preinstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	atomic_set(&dev_priv->irq_received, 0);

	INIT_WORK(&dev_priv->hotplug_work, i915_hotplug_work_func);
	INIT_WORK(&dev_priv->error_work, i915_error_work_func);

	if (HAS_PCH_SPLIT(dev)) {
		ironlake_irq_preinstall(dev);
		return;
	}

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE(HWSTAM, 0xeffe);
	I915_WRITE(PIPEASTAT, 0);
	I915_WRITE(PIPEBSTAT, 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
	(void) I915_READ(IER);
}

/*
 * Must be called after intel_modeset_init or hotplug interrupts won't be
 * enabled correctly.
 */
int i915_driver_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 enable_mask = I915_INTERRUPT_ENABLE_FIX | I915_INTERRUPT_ENABLE_VAR;
	u32 error_mask;

	DRM_INIT_WAITQUEUE(&dev_priv->render_ring.irq_queue);

	if (HAS_BSD(dev))
		DRM_INIT_WAITQUEUE(&dev_priv->bsd_ring.irq_queue);

	dev_priv->vblank_pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;

	if (HAS_PCH_SPLIT(dev))
		return ironlake_irq_postinstall(dev);

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask_reg = ~I915_INTERRUPT_ENABLE_FIX;

	dev_priv->pipestat[0] = 0;
	dev_priv->pipestat[1] = 0;

	if (I915_HAS_HOTPLUG(dev)) {
		/* Enable in IER... */
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
		/* and unmask in IMR */
		dev_priv->irq_mask_reg &= ~I915_DISPLAY_PORT_INTERRUPT;
	}

	/*
	 * Enable some error detection, note the instruction error mask
	 * bit is reserved, so we leave it masked.
	 */
	if (IS_G4X(dev)) {
		error_mask = ~(GM45_ERROR_PAGE_TABLE |
			       GM45_ERROR_MEM_PRIV |
			       GM45_ERROR_CP_PRIV |
			       I915_ERROR_MEMORY_REFRESH);
	} else {
		error_mask = ~(I915_ERROR_PAGE_TABLE |
			       I915_ERROR_MEMORY_REFRESH);
	}
	I915_WRITE(EMR, error_mask);

	I915_WRITE(IMR, dev_priv->irq_mask_reg);
	I915_WRITE(IER, enable_mask);
	(void) I915_READ(IER);

	if (I915_HAS_HOTPLUG(dev)) {
		u32 hotplug_en = I915_READ(PORT_HOTPLUG_EN);

		/* Note HDMI and DP share bits */
		if (dev_priv->hotplug_supported_mask & HDMIB_HOTPLUG_INT_STATUS)
			hotplug_en |= HDMIB_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & HDMIC_HOTPLUG_INT_STATUS)
			hotplug_en |= HDMIC_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & HDMID_HOTPLUG_INT_STATUS)
			hotplug_en |= HDMID_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & SDVOC_HOTPLUG_INT_STATUS)
			hotplug_en |= SDVOC_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & SDVOB_HOTPLUG_INT_STATUS)
			hotplug_en |= SDVOB_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & CRT_HOTPLUG_INT_STATUS) {
			hotplug_en |= CRT_HOTPLUG_INT_EN;

			/* Programming the CRT detection parameters tends
			   to generate a spurious hotplug event about three
			   seconds later.  So just do it once.
			*/
			if (IS_G4X(dev))
				hotplug_en |= CRT_HOTPLUG_ACTIVATION_PERIOD_64;
			hotplug_en |= CRT_HOTPLUG_VOLTAGE_COMPARE_50;
		}

		/* Ignore TV since it's buggy */

		I915_WRITE(PORT_HOTPLUG_EN, hotplug_en);
	}

	opregion_enable_asle(dev);

	return 0;
}

static void ironlake_irq_uninstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	I915_WRITE(HWSTAM, 0xffffffff);

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	I915_WRITE(DEIIR, I915_READ(DEIIR));

	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	I915_WRITE(GTIIR, I915_READ(GTIIR));
}

void i915_driver_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (!dev_priv)
		return;

	dev_priv->vblank_pipe = 0;

	if (HAS_PCH_SPLIT(dev)) {
		ironlake_irq_uninstall(dev);
		return;
	}

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE(HWSTAM, 0xffffffff);
	I915_WRITE(PIPEASTAT, 0);
	I915_WRITE(PIPEBSTAT, 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);

	I915_WRITE(PIPEASTAT, I915_READ(PIPEASTAT) & 0x8000ffff);
	I915_WRITE(PIPEBSTAT, I915_READ(PIPEBSTAT) & 0x8000ffff);
	I915_WRITE(IIR, I915_READ(IIR));
}
