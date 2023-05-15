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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/sysrq.h>

#include <drm/drm_drv.h>

#include "display/icl_dsi_regs.h"
#include "display/intel_de.h"
#include "display/intel_display_trace.h"
#include "display/intel_display_types.h"
#include "display/intel_dp_aux.h"
#include "display/intel_fdi_regs.h"
#include "display/intel_fifo_underrun.h"
#include "display/intel_gmbus.h"
#include "display/intel_hotplug.h"
#include "display/intel_hotplug_irq.h"
#include "display/intel_lpe_audio.h"
#include "display/intel_psr.h"
#include "display/intel_psr_regs.h"

#include "gt/intel_breadcrumbs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm_irq.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_rps.h"

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"

/**
 * DOC: interrupt handling
 *
 * These functions provide the basic support for enabling and disabling the
 * interrupt handling support. There's a lot more functionality in i915_irq.c
 * and related files, but that will be described in separate chapters.
 */

/*
 * Interrupt statistic for PMU. Increments the counter only if the
 * interrupt originated from the GPU so interrupts from a device which
 * shares the interrupt line are not accounted.
 */
static inline void pmu_irq_stats(struct drm_i915_private *i915,
				 irqreturn_t res)
{
	if (unlikely(res != IRQ_HANDLED))
		return;

	/*
	 * A clever compiler translates that into INC. A not so clever one
	 * should at least prevent store tearing.
	 */
	WRITE_ONCE(i915->pmu.irq_count, i915->pmu.irq_count + 1);
}

static void
intel_handle_vblank(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);

	drm_crtc_handle_vblank(&crtc->base);
}

void gen3_irq_reset(struct intel_uncore *uncore, i915_reg_t imr,
		    i915_reg_t iir, i915_reg_t ier)
{
	intel_uncore_write(uncore, imr, 0xffffffff);
	intel_uncore_posting_read(uncore, imr);

	intel_uncore_write(uncore, ier, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write(uncore, iir, 0xffffffff);
	intel_uncore_posting_read(uncore, iir);
	intel_uncore_write(uncore, iir, 0xffffffff);
	intel_uncore_posting_read(uncore, iir);
}

static void gen2_irq_reset(struct intel_uncore *uncore)
{
	intel_uncore_write16(uncore, GEN2_IMR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IMR);

	intel_uncore_write16(uncore, GEN2_IER, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
}

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
static void gen3_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg)
{
	u32 val = intel_uncore_read(uncore, reg);

	if (val == 0)
		return;

	drm_WARN(&uncore->i915->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 i915_mmio_reg_offset(reg), val);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
}

static void gen2_assert_iir_is_zero(struct intel_uncore *uncore)
{
	u16 val = intel_uncore_read16(uncore, GEN2_IIR);

	if (val == 0)
		return;

	drm_WARN(&uncore->i915->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 i915_mmio_reg_offset(GEN2_IIR), val);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
}

void gen3_irq_init(struct intel_uncore *uncore,
		   i915_reg_t imr, u32 imr_val,
		   i915_reg_t ier, u32 ier_val,
		   i915_reg_t iir)
{
	gen3_assert_iir_is_zero(uncore, iir);

	intel_uncore_write(uncore, ier, ier_val);
	intel_uncore_write(uncore, imr, imr_val);
	intel_uncore_posting_read(uncore, imr);
}

static void gen2_irq_init(struct intel_uncore *uncore,
			  u32 imr_val, u32 ier_val)
{
	gen2_assert_iir_is_zero(uncore);

	intel_uncore_write16(uncore, GEN2_IER, ier_val);
	intel_uncore_write16(uncore, GEN2_IMR, imr_val);
	intel_uncore_posting_read16(uncore, GEN2_IMR);
}

/**
 * ilk_update_display_irq - update DEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void ilk_update_display_irq(struct drm_i915_private *dev_priv,
			    u32 interrupt_mask, u32 enabled_irq_mask)
{
	u32 new_val;

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	new_val = dev_priv->irq_mask;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->irq_mask &&
	    !drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv))) {
		dev_priv->irq_mask = new_val;
		intel_uncore_write(&dev_priv->uncore, DEIMR, dev_priv->irq_mask);
		intel_uncore_posting_read(&dev_priv->uncore, DEIMR);
	}
}

void ilk_enable_display_irq(struct drm_i915_private *i915, u32 bits)
{
	ilk_update_display_irq(i915, bits, bits);
}

void ilk_disable_display_irq(struct drm_i915_private *i915, u32 bits)
{
	ilk_update_display_irq(i915, bits, 0);
}

/**
 * bdw_update_port_irq - update DE port interrupt
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void bdw_update_port_irq(struct drm_i915_private *dev_priv,
			 u32 interrupt_mask, u32 enabled_irq_mask)
{
	u32 new_val;
	u32 old_val;

	lockdep_assert_held(&dev_priv->irq_lock);

	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	if (drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv)))
		return;

	old_val = intel_uncore_read(&dev_priv->uncore, GEN8_DE_PORT_IMR);

	new_val = old_val;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != old_val) {
		intel_uncore_write(&dev_priv->uncore, GEN8_DE_PORT_IMR, new_val);
		intel_uncore_posting_read(&dev_priv->uncore, GEN8_DE_PORT_IMR);
	}
}

/**
 * bdw_update_pipe_irq - update DE pipe interrupt
 * @dev_priv: driver private
 * @pipe: pipe whose interrupt to update
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void bdw_update_pipe_irq(struct drm_i915_private *dev_priv,
				enum pipe pipe, u32 interrupt_mask,
				u32 enabled_irq_mask)
{
	u32 new_val;

	lockdep_assert_held(&dev_priv->irq_lock);

	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	if (drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv)))
		return;

	new_val = dev_priv->de_irq_mask[pipe];
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->de_irq_mask[pipe]) {
		dev_priv->de_irq_mask[pipe] = new_val;
		intel_uncore_write(&dev_priv->uncore, GEN8_DE_PIPE_IMR(pipe), dev_priv->de_irq_mask[pipe]);
		intel_uncore_posting_read(&dev_priv->uncore, GEN8_DE_PIPE_IMR(pipe));
	}
}

void bdw_enable_pipe_irq(struct drm_i915_private *i915,
			 enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(i915, pipe, bits, bits);
}

void bdw_disable_pipe_irq(struct drm_i915_private *i915,
			  enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(i915, pipe, bits, 0);
}

/**
 * ibx_display_interrupt_update - update SDEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void ibx_display_interrupt_update(struct drm_i915_private *dev_priv,
				  u32 interrupt_mask,
				  u32 enabled_irq_mask)
{
	u32 sdeimr = intel_uncore_read(&dev_priv->uncore, SDEIMR);
	sdeimr &= ~interrupt_mask;
	sdeimr |= (~enabled_irq_mask & interrupt_mask);

	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	lockdep_assert_held(&dev_priv->irq_lock);

	if (drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv)))
		return;

	intel_uncore_write(&dev_priv->uncore, SDEIMR, sdeimr);
	intel_uncore_posting_read(&dev_priv->uncore, SDEIMR);
}

void ibx_enable_display_interrupt(struct drm_i915_private *i915, u32 bits)
{
	ibx_display_interrupt_update(i915, bits, bits);
}

void ibx_disable_display_interrupt(struct drm_i915_private *i915, u32 bits)
{
	ibx_display_interrupt_update(i915, bits, 0);
}

u32 i915_pipestat_enable_mask(struct drm_i915_private *dev_priv,
			      enum pipe pipe)
{
	u32 status_mask = dev_priv->pipestat_irq_mask[pipe];
	u32 enable_mask = status_mask << 16;

	lockdep_assert_held(&dev_priv->irq_lock);

	if (DISPLAY_VER(dev_priv) < 5)
		goto out;

	/*
	 * On pipe A we don't support the PSR interrupt yet,
	 * on pipe B and C the same bit MBZ.
	 */
	if (drm_WARN_ON_ONCE(&dev_priv->drm,
			     status_mask & PIPE_A_PSR_STATUS_VLV))
		return 0;
	/*
	 * On pipe B and C we don't support the PSR interrupt yet, on pipe
	 * A the same bit is for perf counters which we don't use either.
	 */
	if (drm_WARN_ON_ONCE(&dev_priv->drm,
			     status_mask & PIPE_B_PSR_STATUS_VLV))
		return 0;

	enable_mask &= ~(PIPE_FIFO_UNDERRUN_STATUS |
			 SPRITE0_FLIP_DONE_INT_EN_VLV |
			 SPRITE1_FLIP_DONE_INT_EN_VLV);
	if (status_mask & SPRITE0_FLIP_DONE_INT_STATUS_VLV)
		enable_mask |= SPRITE0_FLIP_DONE_INT_EN_VLV;
	if (status_mask & SPRITE1_FLIP_DONE_INT_STATUS_VLV)
		enable_mask |= SPRITE1_FLIP_DONE_INT_EN_VLV;

out:
	drm_WARN_ONCE(&dev_priv->drm,
		      enable_mask & ~PIPESTAT_INT_ENABLE_MASK ||
		      status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: enable_mask=0x%x, status_mask=0x%x\n",
		      pipe_name(pipe), enable_mask, status_mask);

	return enable_mask;
}

void i915_enable_pipestat(struct drm_i915_private *dev_priv,
			  enum pipe pipe, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 enable_mask;

	drm_WARN_ONCE(&dev_priv->drm, status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: status_mask=0x%x\n",
		      pipe_name(pipe), status_mask);

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv));

	if ((dev_priv->pipestat_irq_mask[pipe] & status_mask) == status_mask)
		return;

	dev_priv->pipestat_irq_mask[pipe] |= status_mask;
	enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

	intel_uncore_write(&dev_priv->uncore, reg, enable_mask | status_mask);
	intel_uncore_posting_read(&dev_priv->uncore, reg);
}

void i915_disable_pipestat(struct drm_i915_private *dev_priv,
			   enum pipe pipe, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 enable_mask;

	drm_WARN_ONCE(&dev_priv->drm, status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: status_mask=0x%x\n",
		      pipe_name(pipe), status_mask);

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv));

	if ((dev_priv->pipestat_irq_mask[pipe] & status_mask) == 0)
		return;

	dev_priv->pipestat_irq_mask[pipe] &= ~status_mask;
	enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

	intel_uncore_write(&dev_priv->uncore, reg, enable_mask | status_mask);
	intel_uncore_posting_read(&dev_priv->uncore, reg);
}

static bool i915_has_asle(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->display.opregion.asle)
		return false;

	return IS_PINEVIEW(dev_priv) || IS_MOBILE(dev_priv);
}

/**
 * i915_enable_asle_pipestat - enable ASLE pipestat for OpRegion
 * @dev_priv: i915 device private
 */
static void i915_enable_asle_pipestat(struct drm_i915_private *dev_priv)
{
	if (!i915_has_asle(dev_priv))
		return;

	spin_lock_irq(&dev_priv->irq_lock);

	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_LEGACY_BLC_EVENT_STATUS);
	if (DISPLAY_VER(dev_priv) >= 4)
		i915_enable_pipestat(dev_priv, PIPE_A,
				     PIPE_LEGACY_BLC_EVENT_STATUS);

	spin_unlock_irq(&dev_priv->irq_lock);
}

/**
 * ivb_parity_work - Workqueue called when a parity error interrupt
 * occurred.
 * @work: workqueue struct
 *
 * Doesn't actually do anything except notify userspace. As a consequence of
 * this event, userspace should try to remap the bad rows since statistically
 * it is likely the same row is more likely to go bad again.
 */
static void ivb_parity_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), l3_parity.error_work);
	struct intel_gt *gt = to_gt(dev_priv);
	u32 error_status, row, bank, subbank;
	char *parity_event[6];
	u32 misccpctl;
	u8 slice = 0;

	/* We must turn off DOP level clock gating to access the L3 registers.
	 * In order to prevent a get/put style interface, acquire struct mutex
	 * any time we access those registers.
	 */
	mutex_lock(&dev_priv->drm.struct_mutex);

	/* If we've screwed up tracking, just let the interrupt fire again */
	if (drm_WARN_ON(&dev_priv->drm, !dev_priv->l3_parity.which_slice))
		goto out;

	misccpctl = intel_uncore_rmw(&dev_priv->uncore, GEN7_MISCCPCTL,
				     GEN7_DOP_CLOCK_GATE_ENABLE, 0);
	intel_uncore_posting_read(&dev_priv->uncore, GEN7_MISCCPCTL);

	while ((slice = ffs(dev_priv->l3_parity.which_slice)) != 0) {
		i915_reg_t reg;

		slice--;
		if (drm_WARN_ON_ONCE(&dev_priv->drm,
				     slice >= NUM_L3_SLICES(dev_priv)))
			break;

		dev_priv->l3_parity.which_slice &= ~(1<<slice);

		reg = GEN7_L3CDERRST1(slice);

		error_status = intel_uncore_read(&dev_priv->uncore, reg);
		row = GEN7_PARITY_ERROR_ROW(error_status);
		bank = GEN7_PARITY_ERROR_BANK(error_status);
		subbank = GEN7_PARITY_ERROR_SUBBANK(error_status);

		intel_uncore_write(&dev_priv->uncore, reg, GEN7_PARITY_ERROR_VALID | GEN7_L3CDERRST1_ENABLE);
		intel_uncore_posting_read(&dev_priv->uncore, reg);

		parity_event[0] = I915_L3_PARITY_UEVENT "=1";
		parity_event[1] = kasprintf(GFP_KERNEL, "ROW=%d", row);
		parity_event[2] = kasprintf(GFP_KERNEL, "BANK=%d", bank);
		parity_event[3] = kasprintf(GFP_KERNEL, "SUBBANK=%d", subbank);
		parity_event[4] = kasprintf(GFP_KERNEL, "SLICE=%d", slice);
		parity_event[5] = NULL;

		kobject_uevent_env(&dev_priv->drm.primary->kdev->kobj,
				   KOBJ_CHANGE, parity_event);

		drm_dbg(&dev_priv->drm,
			"Parity error: Slice = %d, Row = %d, Bank = %d, Sub bank = %d.\n",
			slice, row, bank, subbank);

		kfree(parity_event[4]);
		kfree(parity_event[3]);
		kfree(parity_event[2]);
		kfree(parity_event[1]);
	}

	intel_uncore_write(&dev_priv->uncore, GEN7_MISCCPCTL, misccpctl);

out:
	drm_WARN_ON(&dev_priv->drm, dev_priv->l3_parity.which_slice);
	spin_lock_irq(gt->irq_lock);
	gen5_gt_enable_irq(gt, GT_PARITY_ERROR(dev_priv));
	spin_unlock_irq(gt->irq_lock);

	mutex_unlock(&dev_priv->drm.struct_mutex);
}

#if defined(CONFIG_DEBUG_FS)
static void display_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe,
					 u32 crc0, u32 crc1,
					 u32 crc2, u32 crc3,
					 u32 crc4)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);
	struct intel_pipe_crc *pipe_crc = &crtc->pipe_crc;
	u32 crcs[5] = { crc0, crc1, crc2, crc3, crc4 };

	trace_intel_pipe_crc(crtc, crcs);

	spin_lock(&pipe_crc->lock);
	/*
	 * For some not yet identified reason, the first CRC is
	 * bonkers. So let's just wait for the next vblank and read
	 * out the buggy result.
	 *
	 * On GEN8+ sometimes the second CRC is bonkers as well, so
	 * don't trust that one either.
	 */
	if (pipe_crc->skipped <= 0 ||
	    (DISPLAY_VER(dev_priv) >= 8 && pipe_crc->skipped == 1)) {
		pipe_crc->skipped++;
		spin_unlock(&pipe_crc->lock);
		return;
	}
	spin_unlock(&pipe_crc->lock);

	drm_crtc_add_crc_entry(&crtc->base, true,
				drm_crtc_accurate_vblank_count(&crtc->base),
				crcs);
}
#else
static inline void
display_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
			     enum pipe pipe,
			     u32 crc0, u32 crc1,
			     u32 crc2, u32 crc3,
			     u32 crc4) {}
#endif

static void flip_done_handler(struct drm_i915_private *i915,
			      enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(i915, pipe);
	struct drm_crtc_state *crtc_state = crtc->base.state;
	struct drm_pending_vblank_event *e = crtc_state->event;
	struct drm_device *dev = &i915->drm;
	unsigned long irqflags;

	spin_lock_irqsave(&dev->event_lock, irqflags);

	crtc_state->event = NULL;

	drm_crtc_send_vblank_event(&crtc->base, e);

	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}

static void hsw_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(dev_priv, pipe,
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_1_IVB(pipe)),
				     0, 0, 0, 0);
}

static void ivb_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(dev_priv, pipe,
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_1_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_2_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_3_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_4_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_5_IVB(pipe)));
}

static void i9xx_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	u32 res1, res2;

	if (DISPLAY_VER(dev_priv) >= 3)
		res1 = intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_RES1_I915(pipe));
	else
		res1 = 0;

	if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv))
		res2 = intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_RES2_G4X(pipe));
	else
		res2 = 0;

	display_pipe_crc_irq_handler(dev_priv, pipe,
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_RED(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_GREEN(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_BLUE(pipe)),
				     res1, res2);
}

static void i9xx_pipestat_irq_reset(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		intel_uncore_write(&dev_priv->uncore, PIPESTAT(pipe),
			   PIPESTAT_INT_STATUS_MASK |
			   PIPE_FIFO_UNDERRUN_STATUS);

		dev_priv->pipestat_irq_mask[pipe] = 0;
	}
}

static void i9xx_pipestat_irq_ack(struct drm_i915_private *dev_priv,
				  u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	spin_lock(&dev_priv->irq_lock);

	if (!dev_priv->display_irqs_enabled) {
		spin_unlock(&dev_priv->irq_lock);
		return;
	}

	for_each_pipe(dev_priv, pipe) {
		i915_reg_t reg;
		u32 status_mask, enable_mask, iir_bit = 0;

		/*
		 * PIPESTAT bits get signalled even when the interrupt is
		 * disabled with the mask bits, and some of the status bits do
		 * not generate interrupts at all (like the underrun bit). Hence
		 * we need to be careful that we only handle what we want to
		 * handle.
		 */

		/* fifo underruns are filterered in the underrun handler. */
		status_mask = PIPE_FIFO_UNDERRUN_STATUS;

		switch (pipe) {
		default:
		case PIPE_A:
			iir_bit = I915_DISPLAY_PIPE_A_EVENT_INTERRUPT;
			break;
		case PIPE_B:
			iir_bit = I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
			break;
		case PIPE_C:
			iir_bit = I915_DISPLAY_PIPE_C_EVENT_INTERRUPT;
			break;
		}
		if (iir & iir_bit)
			status_mask |= dev_priv->pipestat_irq_mask[pipe];

		if (!status_mask)
			continue;

		reg = PIPESTAT(pipe);
		pipe_stats[pipe] = intel_uncore_read(&dev_priv->uncore, reg) & status_mask;
		enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

		/*
		 * Clear the PIPE*STAT regs before the IIR
		 *
		 * Toggle the enable bits to make sure we get an
		 * edge in the ISR pipe event bit if we don't clear
		 * all the enabled status bits. Otherwise the edge
		 * triggered IIR on i965/g4x wouldn't notice that
		 * an interrupt is still pending.
		 */
		if (pipe_stats[pipe]) {
			intel_uncore_write(&dev_priv->uncore, reg, pipe_stats[pipe]);
			intel_uncore_write(&dev_priv->uncore, reg, enable_mask);
		}
	}
	spin_unlock(&dev_priv->irq_lock);
}

static void i8xx_pipestat_irq_handler(struct drm_i915_private *dev_priv,
				      u16 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}
}

static void i915_pipestat_irq_handler(struct drm_i915_private *dev_priv,
				      u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	bool blc_event = false;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
			blc_event = true;

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (blc_event || (iir & I915_ASLE_INTERRUPT))
		intel_opregion_asle_intr(dev_priv);
}

static void i965_pipestat_irq_handler(struct drm_i915_private *dev_priv,
				      u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	bool blc_event = false;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
			blc_event = true;

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (blc_event || (iir & I915_ASLE_INTERRUPT))
		intel_opregion_asle_intr(dev_priv);

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		intel_gmbus_irq_handler(dev_priv);
}

static void valleyview_pipestat_irq_handler(struct drm_i915_private *dev_priv,
					    u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(dev_priv, pipe);

		if (pipe_stats[pipe] & PLANE_FLIP_DONE_INT_STATUS_VLV)
			flip_done_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		intel_gmbus_irq_handler(dev_priv);
}

static irqreturn_t valleyview_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 iir, gt_iir, pm_iir;
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
		u32 ier = 0;

		gt_iir = intel_uncore_read(&dev_priv->uncore, GTIIR);
		pm_iir = intel_uncore_read(&dev_priv->uncore, GEN6_PMIIR);
		iir = intel_uncore_read(&dev_priv->uncore, VLV_IIR);

		if (gt_iir == 0 && pm_iir == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		/*
		 * Theory on interrupt generation, based on empirical evidence:
		 *
		 * x = ((VLV_IIR & VLV_IER) ||
		 *      (((GT_IIR & GT_IER) || (GEN6_PMIIR & GEN6_PMIER)) &&
		 *       (VLV_MASTER_IER & MASTER_INTERRUPT_ENABLE)));
		 *
		 * A CPU interrupt will only be raised when 'x' has a 0->1 edge.
		 * Hence we clear MASTER_INTERRUPT_ENABLE and VLV_IER to
		 * guarantee the CPU interrupt will be raised again even if we
		 * don't end up clearing all the VLV_IIR, GT_IIR, GEN6_PMIIR
		 * bits this time around.
		 */
		intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, 0);
		ier = intel_uncore_rmw(&dev_priv->uncore, VLV_IER, ~0, 0);

		if (gt_iir)
			intel_uncore_write(&dev_priv->uncore, GTIIR, gt_iir);
		if (pm_iir)
			intel_uncore_write(&dev_priv->uncore, GEN6_PMIIR, pm_iir);

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & (I915_LPE_PIPE_A_INTERRUPT |
			   I915_LPE_PIPE_B_INTERRUPT))
			intel_lpe_audio_irq_handler(dev_priv);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			intel_uncore_write(&dev_priv->uncore, VLV_IIR, iir);

		intel_uncore_write(&dev_priv->uncore, VLV_IER, ier);
		intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);

		if (gt_iir)
			gen6_gt_irq_handler(to_gt(dev_priv), gt_iir);
		if (pm_iir)
			gen6_rps_irq_handler(&to_gt(dev_priv)->rps, pm_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static irqreturn_t cherryview_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 master_ctl, iir;
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
		u32 ier = 0;

		master_ctl = intel_uncore_read(&dev_priv->uncore, GEN8_MASTER_IRQ) & ~GEN8_MASTER_IRQ_CONTROL;
		iir = intel_uncore_read(&dev_priv->uncore, VLV_IIR);

		if (master_ctl == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		/*
		 * Theory on interrupt generation, based on empirical evidence:
		 *
		 * x = ((VLV_IIR & VLV_IER) ||
		 *      ((GEN8_MASTER_IRQ & ~GEN8_MASTER_IRQ_CONTROL) &&
		 *       (GEN8_MASTER_IRQ & GEN8_MASTER_IRQ_CONTROL)));
		 *
		 * A CPU interrupt will only be raised when 'x' has a 0->1 edge.
		 * Hence we clear GEN8_MASTER_IRQ_CONTROL and VLV_IER to
		 * guarantee the CPU interrupt will be raised again even if we
		 * don't end up clearing all the VLV_IIR and GEN8_MASTER_IRQ_CONTROL
		 * bits this time around.
		 */
		intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, 0);
		ier = intel_uncore_rmw(&dev_priv->uncore, VLV_IER, ~0, 0);

		gen8_gt_irq_handler(to_gt(dev_priv), master_ctl);

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & (I915_LPE_PIPE_A_INTERRUPT |
			   I915_LPE_PIPE_B_INTERRUPT |
			   I915_LPE_PIPE_C_INTERRUPT))
			intel_lpe_audio_irq_handler(dev_priv);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			intel_uncore_write(&dev_priv->uncore, VLV_IIR, iir);

		intel_uncore_write(&dev_priv->uncore, VLV_IER, ier);
		intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void ibx_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK;

	ibx_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (pch_iir & SDE_AUDIO_POWER_MASK) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK) >>
			       SDE_AUDIO_POWER_SHIFT);
		drm_dbg(&dev_priv->drm, "PCH audio power change on port %d\n",
			port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK)
		intel_dp_aux_irq_handler(dev_priv);

	if (pch_iir & SDE_GMBUS)
		intel_gmbus_irq_handler(dev_priv);

	if (pch_iir & SDE_AUDIO_HDCP_MASK)
		drm_dbg(&dev_priv->drm, "PCH HDCP audio interrupt\n");

	if (pch_iir & SDE_AUDIO_TRANS_MASK)
		drm_dbg(&dev_priv->drm, "PCH transcoder audio interrupt\n");

	if (pch_iir & SDE_POISON)
		drm_err(&dev_priv->drm, "PCH poison interrupt\n");

	if (pch_iir & SDE_FDI_MASK) {
		for_each_pipe(dev_priv, pipe)
			drm_dbg(&dev_priv->drm, "  pipe %c FDI IIR: 0x%08x\n",
				pipe_name(pipe),
				intel_uncore_read(&dev_priv->uncore, FDI_RX_IIR(pipe)));
	}

	if (pch_iir & (SDE_TRANSB_CRC_DONE | SDE_TRANSA_CRC_DONE))
		drm_dbg(&dev_priv->drm, "PCH transcoder CRC done interrupt\n");

	if (pch_iir & (SDE_TRANSB_CRC_ERR | SDE_TRANSA_CRC_ERR))
		drm_dbg(&dev_priv->drm,
			"PCH transcoder CRC error interrupt\n");

	if (pch_iir & SDE_TRANSA_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, PIPE_A);

	if (pch_iir & SDE_TRANSB_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, PIPE_B);
}

static void ivb_err_int_handler(struct drm_i915_private *dev_priv)
{
	u32 err_int = intel_uncore_read(&dev_priv->uncore, GEN7_ERR_INT);
	enum pipe pipe;

	if (err_int & ERR_INT_POISON)
		drm_err(&dev_priv->drm, "Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (err_int & ERR_INT_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (err_int & ERR_INT_PIPE_CRC_DONE(pipe)) {
			if (IS_IVYBRIDGE(dev_priv))
				ivb_pipe_crc_irq_handler(dev_priv, pipe);
			else
				hsw_pipe_crc_irq_handler(dev_priv, pipe);
		}
	}

	intel_uncore_write(&dev_priv->uncore, GEN7_ERR_INT, err_int);
}

static void cpt_serr_int_handler(struct drm_i915_private *dev_priv)
{
	u32 serr_int = intel_uncore_read(&dev_priv->uncore, SERR_INT);
	enum pipe pipe;

	if (serr_int & SERR_INT_POISON)
		drm_err(&dev_priv->drm, "PCH poison interrupt\n");

	for_each_pipe(dev_priv, pipe)
		if (serr_int & SERR_INT_TRANS_FIFO_UNDERRUN(pipe))
			intel_pch_fifo_underrun_irq_handler(dev_priv, pipe);

	intel_uncore_write(&dev_priv->uncore, SERR_INT, serr_int);
}

static void cpt_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_CPT;

	ibx_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (pch_iir & SDE_AUDIO_POWER_MASK_CPT) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK_CPT) >>
			       SDE_AUDIO_POWER_SHIFT_CPT);
		drm_dbg(&dev_priv->drm, "PCH audio power change on port %c\n",
			port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK_CPT)
		intel_dp_aux_irq_handler(dev_priv);

	if (pch_iir & SDE_GMBUS_CPT)
		intel_gmbus_irq_handler(dev_priv);

	if (pch_iir & SDE_AUDIO_CP_REQ_CPT)
		drm_dbg(&dev_priv->drm, "Audio CP request interrupt\n");

	if (pch_iir & SDE_AUDIO_CP_CHG_CPT)
		drm_dbg(&dev_priv->drm, "Audio CP change interrupt\n");

	if (pch_iir & SDE_FDI_MASK_CPT) {
		for_each_pipe(dev_priv, pipe)
			drm_dbg(&dev_priv->drm, "  pipe %c FDI IIR: 0x%08x\n",
				pipe_name(pipe),
				intel_uncore_read(&dev_priv->uncore, FDI_RX_IIR(pipe)));
	}

	if (pch_iir & SDE_ERROR_CPT)
		cpt_serr_int_handler(dev_priv);
}

static void ilk_display_irq_handler(struct drm_i915_private *dev_priv,
				    u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (de_iir & DE_AUX_CHANNEL_A)
		intel_dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE)
		intel_opregion_asle_intr(dev_priv);

	if (de_iir & DE_POISON)
		drm_err(&dev_priv->drm, "Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & DE_PIPE_VBLANK(pipe))
			intel_handle_vblank(dev_priv, pipe);

		if (de_iir & DE_PLANE_FLIP_DONE(pipe))
			flip_done_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_CRC_DONE(pipe))
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);
	}

	/* check event from PCH */
	if (de_iir & DE_PCH_EVENT) {
		u32 pch_iir = intel_uncore_read(&dev_priv->uncore, SDEIIR);

		if (HAS_PCH_CPT(dev_priv))
			cpt_irq_handler(dev_priv, pch_iir);
		else
			ibx_irq_handler(dev_priv, pch_iir);

		/* should clear PCH hotplug event before clear CPU irq */
		intel_uncore_write(&dev_priv->uncore, SDEIIR, pch_iir);
	}

	if (DISPLAY_VER(dev_priv) == 5 && de_iir & DE_PCU_EVENT)
		gen5_rps_irq_handler(&to_gt(dev_priv)->rps);
}

static void ivb_display_irq_handler(struct drm_i915_private *dev_priv,
				    u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG_IVB;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (de_iir & DE_ERR_INT_IVB)
		ivb_err_int_handler(dev_priv);

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		intel_dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(dev_priv);

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & DE_PIPE_VBLANK_IVB(pipe))
			intel_handle_vblank(dev_priv, pipe);

		if (de_iir & DE_PLANE_FLIP_DONE_IVB(pipe))
			flip_done_handler(dev_priv, pipe);
	}

	/* check event from PCH */
	if (!HAS_PCH_NOP(dev_priv) && (de_iir & DE_PCH_EVENT_IVB)) {
		u32 pch_iir = intel_uncore_read(&dev_priv->uncore, SDEIIR);

		cpt_irq_handler(dev_priv, pch_iir);

		/* clear PCH hotplug event before clear CPU irq */
		intel_uncore_write(&dev_priv->uncore, SDEIIR, pch_iir);
	}
}

/*
 * To handle irqs with the minimum potential races with fresh interrupts, we:
 * 1 - Disable Master Interrupt Control.
 * 2 - Find the source(s) of the interrupt.
 * 3 - Clear the Interrupt Identity bits (IIR).
 * 4 - Process the interrupt(s) that had bits set in the IIRs.
 * 5 - Re-enable Master Interrupt Control.
 */
static irqreturn_t ilk_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *i915 = arg;
	void __iomem * const regs = i915->uncore.regs;
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	if (unlikely(!intel_irqs_enabled(i915)))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	/* disable master interrupt before clearing iir  */
	de_ier = raw_reg_read(regs, DEIER);
	raw_reg_write(regs, DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	if (!HAS_PCH_NOP(i915)) {
		sde_ier = raw_reg_read(regs, SDEIER);
		raw_reg_write(regs, SDEIER, 0);
	}

	/* Find, clear, then process each source of interrupt */

	gt_iir = raw_reg_read(regs, GTIIR);
	if (gt_iir) {
		raw_reg_write(regs, GTIIR, gt_iir);
		if (GRAPHICS_VER(i915) >= 6)
			gen6_gt_irq_handler(to_gt(i915), gt_iir);
		else
			gen5_gt_irq_handler(to_gt(i915), gt_iir);
		ret = IRQ_HANDLED;
	}

	de_iir = raw_reg_read(regs, DEIIR);
	if (de_iir) {
		raw_reg_write(regs, DEIIR, de_iir);
		if (DISPLAY_VER(i915) >= 7)
			ivb_display_irq_handler(i915, de_iir);
		else
			ilk_display_irq_handler(i915, de_iir);
		ret = IRQ_HANDLED;
	}

	if (GRAPHICS_VER(i915) >= 6) {
		u32 pm_iir = raw_reg_read(regs, GEN6_PMIIR);
		if (pm_iir) {
			raw_reg_write(regs, GEN6_PMIIR, pm_iir);
			gen6_rps_irq_handler(&to_gt(i915)->rps, pm_iir);
			ret = IRQ_HANDLED;
		}
	}

	raw_reg_write(regs, DEIER, de_ier);
	if (sde_ier)
		raw_reg_write(regs, SDEIER, sde_ier);

	pmu_irq_stats(i915, ret);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	return ret;
}

static u32 gen8_de_port_aux_mask(struct drm_i915_private *dev_priv)
{
	u32 mask;

	if (DISPLAY_VER(dev_priv) >= 14)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB;
	else if (DISPLAY_VER(dev_priv) >= 13)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC |
			XELPD_DE_PORT_AUX_DDID |
			XELPD_DE_PORT_AUX_DDIE |
			TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 |
			TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4;
	else if (DISPLAY_VER(dev_priv) >= 12)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC |
			TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 |
			TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4 |
			TGL_DE_PORT_AUX_USBC5 |
			TGL_DE_PORT_AUX_USBC6;


	mask = GEN8_AUX_CHANNEL_A;
	if (DISPLAY_VER(dev_priv) >= 9)
		mask |= GEN9_AUX_CHANNEL_B |
			GEN9_AUX_CHANNEL_C |
			GEN9_AUX_CHANNEL_D;

	if (DISPLAY_VER(dev_priv) == 11) {
		mask |= ICL_AUX_CHANNEL_F;
		mask |= ICL_AUX_CHANNEL_E;
	}

	return mask;
}

static u32 gen8_de_pipe_fault_mask(struct drm_i915_private *dev_priv)
{
	if (DISPLAY_VER(dev_priv) >= 13 || HAS_D12_PLANE_MINIMIZATION(dev_priv))
		return RKL_DE_PIPE_IRQ_FAULT_ERRORS;
	else if (DISPLAY_VER(dev_priv) >= 11)
		return GEN11_DE_PIPE_IRQ_FAULT_ERRORS;
	else if (DISPLAY_VER(dev_priv) >= 9)
		return GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
	else
		return GEN8_DE_PIPE_IRQ_FAULT_ERRORS;
}

static void
gen8_de_misc_irq_handler(struct drm_i915_private *dev_priv, u32 iir)
{
	bool found = false;

	if (iir & GEN8_DE_MISC_GSE) {
		intel_opregion_asle_intr(dev_priv);
		found = true;
	}

	if (iir & GEN8_DE_EDP_PSR) {
		struct intel_encoder *encoder;
		u32 psr_iir;
		i915_reg_t iir_reg;

		for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
			struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

			if (DISPLAY_VER(dev_priv) >= 12)
				iir_reg = TRANS_PSR_IIR(intel_dp->psr.transcoder);
			else
				iir_reg = EDP_PSR_IIR;

			psr_iir = intel_uncore_rmw(&dev_priv->uncore, iir_reg, 0, 0);

			if (psr_iir)
				found = true;

			intel_psr_irq_handler(intel_dp, psr_iir);

			/* prior GEN12 only have one EDP PSR */
			if (DISPLAY_VER(dev_priv) < 12)
				break;
		}
	}

	if (!found)
		drm_err(&dev_priv->drm, "Unexpected DE Misc interrupt\n");
}

static void gen11_dsi_te_interrupt_handler(struct drm_i915_private *dev_priv,
					   u32 te_trigger)
{
	enum pipe pipe = INVALID_PIPE;
	enum transcoder dsi_trans;
	enum port port;
	u32 val, tmp;

	/*
	 * Incase of dual link, TE comes from DSI_1
	 * this is to check if dual link is enabled
	 */
	val = intel_uncore_read(&dev_priv->uncore, TRANS_DDI_FUNC_CTL2(TRANSCODER_DSI_0));
	val &= PORT_SYNC_MODE_ENABLE;

	/*
	 * if dual link is enabled, then read DSI_0
	 * transcoder registers
	 */
	port = ((te_trigger & DSI1_TE && val) || (te_trigger & DSI0_TE)) ?
						  PORT_A : PORT_B;
	dsi_trans = (port == PORT_A) ? TRANSCODER_DSI_0 : TRANSCODER_DSI_1;

	/* Check if DSI configured in command mode */
	val = intel_uncore_read(&dev_priv->uncore, DSI_TRANS_FUNC_CONF(dsi_trans));
	val = val & OP_MODE_MASK;

	if (val != CMD_MODE_NO_GATE && val != CMD_MODE_TE_GATE) {
		drm_err(&dev_priv->drm, "DSI trancoder not configured in command mode\n");
		return;
	}

	/* Get PIPE for handling VBLANK event */
	val = intel_uncore_read(&dev_priv->uncore, TRANS_DDI_FUNC_CTL(dsi_trans));
	switch (val & TRANS_DDI_EDP_INPUT_MASK) {
	case TRANS_DDI_EDP_INPUT_A_ON:
		pipe = PIPE_A;
		break;
	case TRANS_DDI_EDP_INPUT_B_ONOFF:
		pipe = PIPE_B;
		break;
	case TRANS_DDI_EDP_INPUT_C_ONOFF:
		pipe = PIPE_C;
		break;
	default:
		drm_err(&dev_priv->drm, "Invalid PIPE\n");
		return;
	}

	intel_handle_vblank(dev_priv, pipe);

	/* clear TE in dsi IIR */
	port = (te_trigger & DSI1_TE) ? PORT_B : PORT_A;
	tmp = intel_uncore_rmw(&dev_priv->uncore, DSI_INTR_IDENT_REG(port), 0, 0);
}

static u32 gen8_de_pipe_flip_done_mask(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 9)
		return GEN9_PIPE_PLANE1_FLIP_DONE;
	else
		return GEN8_PIPE_PRIMARY_FLIP_DONE;
}

u32 gen8_de_pipe_underrun_mask(struct drm_i915_private *dev_priv)
{
	u32 mask = GEN8_PIPE_FIFO_UNDERRUN;

	if (DISPLAY_VER(dev_priv) >= 13)
		mask |= XELPD_PIPE_SOFT_UNDERRUN |
			XELPD_PIPE_HARD_UNDERRUN;

	return mask;
}

static void gen8_read_and_ack_pch_irqs(struct drm_i915_private *i915, u32 *pch_iir, u32 *pica_iir)
{
	u32 pica_ier = 0;

	*pica_iir = 0;
	*pch_iir = intel_de_read(i915, SDEIIR);
	if (!*pch_iir)
		return;

	/**
	 * PICA IER must be disabled/re-enabled around clearing PICA IIR and
	 * SDEIIR, to avoid losing PICA IRQs and to ensure that such IRQs set
	 * their flags both in the PICA and SDE IIR.
	 */
	if (*pch_iir & SDE_PICAINTERRUPT) {
		drm_WARN_ON(&i915->drm, INTEL_PCH_TYPE(i915) < PCH_MTP);

		pica_ier = intel_de_rmw(i915, PICAINTERRUPT_IER, ~0, 0);
		*pica_iir = intel_de_read(i915, PICAINTERRUPT_IIR);
		intel_de_write(i915, PICAINTERRUPT_IIR, *pica_iir);
	}

	intel_de_write(i915, SDEIIR, *pch_iir);

	if (pica_ier)
		intel_de_write(i915, PICAINTERRUPT_IER, pica_ier);
}

static void gen8_de_irq_handler(struct drm_i915_private *dev_priv, u32 master_ctl)
{
	u32 iir;
	enum pipe pipe;

	drm_WARN_ON_ONCE(&dev_priv->drm, !HAS_DISPLAY(dev_priv));

	if (master_ctl & GEN8_DE_MISC_IRQ) {
		iir = intel_uncore_read(&dev_priv->uncore, GEN8_DE_MISC_IIR);
		if (iir) {
			intel_uncore_write(&dev_priv->uncore, GEN8_DE_MISC_IIR, iir);
			gen8_de_misc_irq_handler(dev_priv, iir);
		} else {
			drm_err_ratelimited(&dev_priv->drm,
					    "The master control interrupt lied (DE MISC)!\n");
		}
	}

	if (DISPLAY_VER(dev_priv) >= 11 && (master_ctl & GEN11_DE_HPD_IRQ)) {
		iir = intel_uncore_read(&dev_priv->uncore, GEN11_DE_HPD_IIR);
		if (iir) {
			intel_uncore_write(&dev_priv->uncore, GEN11_DE_HPD_IIR, iir);
			gen11_hpd_irq_handler(dev_priv, iir);
		} else {
			drm_err_ratelimited(&dev_priv->drm,
					    "The master control interrupt lied, (DE HPD)!\n");
		}
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		iir = intel_uncore_read(&dev_priv->uncore, GEN8_DE_PORT_IIR);
		if (iir) {
			bool found = false;

			intel_uncore_write(&dev_priv->uncore, GEN8_DE_PORT_IIR, iir);

			if (iir & gen8_de_port_aux_mask(dev_priv)) {
				intel_dp_aux_irq_handler(dev_priv);
				found = true;
			}

			if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
				u32 hotplug_trigger = iir & BXT_DE_PORT_HOTPLUG_MASK;

				if (hotplug_trigger) {
					bxt_hpd_irq_handler(dev_priv, hotplug_trigger);
					found = true;
				}
			} else if (IS_BROADWELL(dev_priv)) {
				u32 hotplug_trigger = iir & BDW_DE_PORT_HOTPLUG_MASK;

				if (hotplug_trigger) {
					ilk_hpd_irq_handler(dev_priv, hotplug_trigger);
					found = true;
				}
			}

			if ((IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) &&
			    (iir & BXT_DE_PORT_GMBUS)) {
				intel_gmbus_irq_handler(dev_priv);
				found = true;
			}

			if (DISPLAY_VER(dev_priv) >= 11) {
				u32 te_trigger = iir & (DSI0_TE | DSI1_TE);

				if (te_trigger) {
					gen11_dsi_te_interrupt_handler(dev_priv, te_trigger);
					found = true;
				}
			}

			if (!found)
				drm_err_ratelimited(&dev_priv->drm,
						    "Unexpected DE Port interrupt\n");
		}
		else
			drm_err_ratelimited(&dev_priv->drm,
					    "The master control interrupt lied (DE PORT)!\n");
	}

	for_each_pipe(dev_priv, pipe) {
		u32 fault_errors;

		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		iir = intel_uncore_read(&dev_priv->uncore, GEN8_DE_PIPE_IIR(pipe));
		if (!iir) {
			drm_err_ratelimited(&dev_priv->drm,
					    "The master control interrupt lied (DE PIPE)!\n");
			continue;
		}

		intel_uncore_write(&dev_priv->uncore, GEN8_DE_PIPE_IIR(pipe), iir);

		if (iir & GEN8_PIPE_VBLANK)
			intel_handle_vblank(dev_priv, pipe);

		if (iir & gen8_de_pipe_flip_done_mask(dev_priv))
			flip_done_handler(dev_priv, pipe);

		if (iir & GEN8_PIPE_CDCLK_CRC_DONE)
			hsw_pipe_crc_irq_handler(dev_priv, pipe);

		if (iir & gen8_de_pipe_underrun_mask(dev_priv))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		fault_errors = iir & gen8_de_pipe_fault_mask(dev_priv);
		if (fault_errors)
			drm_err_ratelimited(&dev_priv->drm,
					    "Fault errors on pipe %c: 0x%08x\n",
					    pipe_name(pipe),
					    fault_errors);
	}

	if (HAS_PCH_SPLIT(dev_priv) && !HAS_PCH_NOP(dev_priv) &&
	    master_ctl & GEN8_DE_PCH_IRQ) {
		u32 pica_iir;

		/*
		 * FIXME(BDW): Assume for now that the new interrupt handling
		 * scheme also closed the SDE interrupt handling race we've seen
		 * on older pch-split platforms. But this needs testing.
		 */
		gen8_read_and_ack_pch_irqs(dev_priv, &iir, &pica_iir);
		if (iir) {
			if (pica_iir)
				xelpdp_pica_irq_handler(dev_priv, pica_iir);

			if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
				icp_irq_handler(dev_priv, iir);
			else if (INTEL_PCH_TYPE(dev_priv) >= PCH_SPT)
				spt_irq_handler(dev_priv, iir);
			else
				cpt_irq_handler(dev_priv, iir);
		} else {
			/*
			 * Like on previous PCH there seems to be something
			 * fishy going on with forwarding PCH interrupts.
			 */
			drm_dbg(&dev_priv->drm,
				"The master control interrupt lied (SDE)!\n");
		}
	}
}

static inline u32 gen8_master_intr_disable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN8_MASTER_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return raw_reg_read(regs, GEN8_MASTER_IRQ);
}

static inline void gen8_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
}

static irqreturn_t gen8_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	void __iomem * const regs = dev_priv->uncore.regs;
	u32 master_ctl;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	master_ctl = gen8_master_intr_disable(regs);
	if (!master_ctl) {
		gen8_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, queue (onto bottom-halves), then clear each source */
	gen8_gt_irq_handler(to_gt(dev_priv), master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & ~GEN8_GT_IRQS) {
		disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
		gen8_de_irq_handler(dev_priv, master_ctl);
		enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
	}

	gen8_master_intr_enable(regs);

	pmu_irq_stats(dev_priv, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static u32
gen11_gu_misc_irq_ack(struct drm_i915_private *i915, const u32 master_ctl)
{
	void __iomem * const regs = i915->uncore.regs;
	u32 iir;

	if (!(master_ctl & GEN11_GU_MISC_IRQ))
		return 0;

	iir = raw_reg_read(regs, GEN11_GU_MISC_IIR);
	if (likely(iir))
		raw_reg_write(regs, GEN11_GU_MISC_IIR, iir);

	return iir;
}

static void
gen11_gu_misc_irq_handler(struct drm_i915_private *i915, const u32 iir)
{
	if (iir & GEN11_GU_MISC_GSE)
		intel_opregion_asle_intr(i915);
}

static inline u32 gen11_master_intr_disable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return raw_reg_read(regs, GEN11_GFX_MSTR_IRQ);
}

static inline void gen11_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);
}

static void
gen11_display_irq_handler(struct drm_i915_private *i915)
{
	void __iomem * const regs = i915->uncore.regs;
	const u32 disp_ctl = raw_reg_read(regs, GEN11_DISPLAY_INT_CTL);

	disable_rpm_wakeref_asserts(&i915->runtime_pm);
	/*
	 * GEN11_DISPLAY_INT_CTL has same format as GEN8_MASTER_IRQ
	 * for the display related bits.
	 */
	raw_reg_write(regs, GEN11_DISPLAY_INT_CTL, 0x0);
	gen8_de_irq_handler(i915, disp_ctl);
	raw_reg_write(regs, GEN11_DISPLAY_INT_CTL,
		      GEN11_DISPLAY_IRQ_ENABLE);

	enable_rpm_wakeref_asserts(&i915->runtime_pm);
}

static irqreturn_t gen11_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *i915 = arg;
	void __iomem * const regs = i915->uncore.regs;
	struct intel_gt *gt = to_gt(i915);
	u32 master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_ctl = gen11_master_intr_disable(regs);
	if (!master_ctl) {
		gen11_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, queue (onto bottom-halves), then clear each source */
	gen11_gt_irq_handler(gt, master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & GEN11_DISPLAY_IRQ)
		gen11_display_irq_handler(i915);

	gu_misc_iir = gen11_gu_misc_irq_ack(i915, master_ctl);

	gen11_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(i915, gu_misc_iir);

	pmu_irq_stats(i915, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static inline u32 dg1_master_intr_disable(void __iomem * const regs)
{
	u32 val;

	/* First disable interrupts */
	raw_reg_write(regs, DG1_MSTR_TILE_INTR, 0);

	/* Get the indication levels and ack the master unit */
	val = raw_reg_read(regs, DG1_MSTR_TILE_INTR);
	if (unlikely(!val))
		return 0;

	raw_reg_write(regs, DG1_MSTR_TILE_INTR, val);

	return val;
}

static inline void dg1_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, DG1_MSTR_TILE_INTR, DG1_MSTR_IRQ);
}

static irqreturn_t dg1_irq_handler(int irq, void *arg)
{
	struct drm_i915_private * const i915 = arg;
	struct intel_gt *gt = to_gt(i915);
	void __iomem * const regs = gt->uncore->regs;
	u32 master_tile_ctl, master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_tile_ctl = dg1_master_intr_disable(regs);
	if (!master_tile_ctl) {
		dg1_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* FIXME: we only support tile 0 for now. */
	if (master_tile_ctl & DG1_MSTR_TILE(0)) {
		master_ctl = raw_reg_read(regs, GEN11_GFX_MSTR_IRQ);
		raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, master_ctl);
	} else {
		drm_err(&i915->drm, "Tile not supported: 0x%08x\n",
			master_tile_ctl);
		dg1_master_intr_enable(regs);
		return IRQ_NONE;
	}

	gen11_gt_irq_handler(gt, master_ctl);

	if (master_ctl & GEN11_DISPLAY_IRQ)
		gen11_display_irq_handler(i915);

	gu_misc_iir = gen11_gu_misc_irq_ack(i915, master_ctl);

	dg1_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(i915, gu_misc_iir);

	pmu_irq_stats(i915, IRQ_HANDLED);

	return IRQ_HANDLED;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
int i8xx_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

int i915gm_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	/*
	 * Vblank interrupts fail to wake the device up from C2+.
	 * Disabling render clock gating during C-states avoids
	 * the problem. There is a small power cost so we do this
	 * only when vblank interrupts are actually enabled.
	 */
	if (dev_priv->vblank_enabled++ == 0)
		intel_uncore_write(&dev_priv->uncore, SCPD0, _MASKED_BIT_ENABLE(CSTATE_RENDER_CLOCK_GATE_DISABLE));

	return i8xx_enable_vblank(crtc);
}

int i965_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, pipe,
			     PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

int ilk_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;
	u32 bit = DISPLAY_VER(dev_priv) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_enable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	/* Even though there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated.
	 */
	if (HAS_PSR(dev_priv))
		drm_crtc_vblank_restore(crtc);

	return 0;
}

static bool gen11_dsi_configure_te(struct intel_crtc *intel_crtc,
				   bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	enum port port;

	if (!(intel_crtc->mode_flags &
	    (I915_MODE_FLAG_DSI_USE_TE1 | I915_MODE_FLAG_DSI_USE_TE0)))
		return false;

	/* for dual link cases we consider TE from slave */
	if (intel_crtc->mode_flags & I915_MODE_FLAG_DSI_USE_TE1)
		port = PORT_B;
	else
		port = PORT_A;

	intel_uncore_rmw(&dev_priv->uncore, DSI_INTR_MASK_REG(port), DSI_TE_EVENT,
			 enable ? 0 : DSI_TE_EVENT);

	intel_uncore_rmw(&dev_priv->uncore, DSI_INTR_IDENT_REG(port), 0, 0);

	return true;
}

int bdw_enable_vblank(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	unsigned long irqflags;

	if (gen11_dsi_configure_te(crtc, true))
		return 0;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_enable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	/* Even if there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated, so check only for PSR.
	 */
	if (HAS_PSR(dev_priv))
		drm_crtc_vblank_restore(&crtc->base);

	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
void i8xx_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void i915gm_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	i8xx_disable_vblank(crtc);

	if (--dev_priv->vblank_enabled == 0)
		intel_uncore_write(&dev_priv->uncore, SCPD0, _MASKED_BIT_DISABLE(CSTATE_RENDER_CLOCK_GATE_DISABLE));
}

void i965_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void ilk_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;
	u32 bit = DISPLAY_VER(dev_priv) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_disable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void bdw_disable_vblank(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	unsigned long irqflags;

	if (gen11_dsi_configure_te(crtc, false))
		return;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_disable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void ibx_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (HAS_PCH_NOP(dev_priv))
		return;

	GEN3_IRQ_RESET(uncore, SDE);

	if (HAS_PCH_CPT(dev_priv) || HAS_PCH_LPT(dev_priv))
		intel_uncore_write(&dev_priv->uncore, SERR_INT, 0xffffffff);
}

static void vlv_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (IS_CHERRYVIEW(dev_priv))
		intel_uncore_write(uncore, DPINVGTT, DPINVGTT_STATUS_MASK_CHV);
	else
		intel_uncore_write(uncore, DPINVGTT, DPINVGTT_STATUS_MASK_VLV);

	i915_hotplug_interrupt_update_locked(dev_priv, 0xffffffff, 0);
	intel_uncore_rmw(uncore, PORT_HOTPLUG_STAT, 0, 0);

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, VLV_);
	dev_priv->irq_mask = ~0u;
}

static void vlv_display_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	u32 pipestat_mask;
	u32 enable_mask;
	enum pipe pipe;

	pipestat_mask = PIPE_CRC_DONE_INTERRUPT_STATUS;

	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	for_each_pipe(dev_priv, pipe)
		i915_enable_pipestat(dev_priv, pipe, pipestat_mask);

	enable_mask = I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_LPE_PIPE_A_INTERRUPT |
		I915_LPE_PIPE_B_INTERRUPT;

	if (IS_CHERRYVIEW(dev_priv))
		enable_mask |= I915_DISPLAY_PIPE_C_EVENT_INTERRUPT |
			I915_LPE_PIPE_C_INTERRUPT;

	drm_WARN_ON(&dev_priv->drm, dev_priv->irq_mask != ~0u);

	dev_priv->irq_mask = ~enable_mask;

	GEN3_IRQ_INIT(uncore, VLV_, dev_priv->irq_mask, enable_mask);
}

/* drm_dma.h hooks
*/
static void ilk_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	GEN3_IRQ_RESET(uncore, DE);
	dev_priv->irq_mask = ~0u;

	if (GRAPHICS_VER(dev_priv) == 7)
		intel_uncore_write(uncore, GEN7_ERR_INT, 0xffffffff);

	if (IS_HASWELL(dev_priv)) {
		intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
		intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);
	}

	gen5_gt_irq_reset(to_gt(dev_priv));

	ibx_irq_reset(dev_priv);
}

static void valleyview_irq_reset(struct drm_i915_private *dev_priv)
{
	intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, 0);
	intel_uncore_posting_read(&dev_priv->uncore, VLV_MASTER_IER);

	gen5_gt_irq_reset(to_gt(dev_priv));

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void gen8_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	enum pipe pipe;

	if (!HAS_DISPLAY(dev_priv))
		return;

	intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
	intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	GEN3_IRQ_RESET(uncore, GEN8_DE_PORT_);
	GEN3_IRQ_RESET(uncore, GEN8_DE_MISC_);
}

static void gen8_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	gen8_master_intr_disable(uncore->regs);

	gen8_gt_irq_reset(to_gt(dev_priv));
	gen8_display_irq_reset(dev_priv);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_reset(dev_priv);

}

static void gen11_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	enum pipe pipe;
	u32 trans_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);

	if (!HAS_DISPLAY(dev_priv))
		return;

	intel_uncore_write(uncore, GEN11_DISPLAY_INT_CTL, 0);

	if (DISPLAY_VER(dev_priv) >= 12) {
		enum transcoder trans;

		for_each_cpu_transcoder_masked(dev_priv, trans, trans_mask) {
			enum intel_display_power_domain domain;

			domain = POWER_DOMAIN_TRANSCODER(trans);
			if (!intel_display_power_is_enabled(dev_priv, domain))
				continue;

			intel_uncore_write(uncore, TRANS_PSR_IMR(trans), 0xffffffff);
			intel_uncore_write(uncore, TRANS_PSR_IIR(trans), 0xffffffff);
		}
	} else {
		intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
		intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);
	}

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	GEN3_IRQ_RESET(uncore, GEN8_DE_PORT_);
	GEN3_IRQ_RESET(uncore, GEN8_DE_MISC_);

	if (DISPLAY_VER(dev_priv) >= 14)
		GEN3_IRQ_RESET(uncore, PICAINTERRUPT_);
	else
		GEN3_IRQ_RESET(uncore, GEN11_DE_HPD_);

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		GEN3_IRQ_RESET(uncore, SDE);
}

static void gen11_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;

	gen11_master_intr_disable(dev_priv->uncore.regs);

	gen11_gt_irq_reset(gt);
	gen11_display_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN11_GU_MISC_);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);
}

static void dg1_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;

	dg1_master_intr_disable(dev_priv->uncore.regs);

	gen11_gt_irq_reset(gt);
	gen11_display_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN11_GU_MISC_);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);
}

void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 extra_ier = GEN8_PIPE_VBLANK |
		gen8_de_pipe_underrun_mask(dev_priv) |
		gen8_de_pipe_flip_done_mask(dev_priv);
	enum pipe pipe;

	spin_lock_irq(&dev_priv->irq_lock);

	if (!intel_irqs_enabled(dev_priv)) {
		spin_unlock_irq(&dev_priv->irq_lock);
		return;
	}

	for_each_pipe_masked(dev_priv, pipe, pipe_mask)
		GEN8_IRQ_INIT_NDX(uncore, DE_PIPE, pipe,
				  dev_priv->de_irq_mask[pipe],
				  ~dev_priv->de_irq_mask[pipe] | extra_ier);

	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen8_irq_power_well_pre_disable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	enum pipe pipe;

	spin_lock_irq(&dev_priv->irq_lock);

	if (!intel_irqs_enabled(dev_priv)) {
		spin_unlock_irq(&dev_priv->irq_lock);
		return;
	}

	for_each_pipe_masked(dev_priv, pipe, pipe_mask)
		GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	spin_unlock_irq(&dev_priv->irq_lock);

	/* make sure we're done processing display irqs */
	intel_synchronize_irq(dev_priv);
}

static void cherryview_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	intel_uncore_write(uncore, GEN8_MASTER_IRQ, 0);
	intel_uncore_posting_read(&dev_priv->uncore, GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(to_gt(dev_priv));

	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

/*
 * SDEIER is also touched by the interrupt handler to work around missed PCH
 * interrupts. Hence we can't update it after the interrupt handler is enabled -
 * instead we unconditionally enable all PCH interrupt sources here, but then
 * only unmask them as needed with SDEIMR.
 *
 * Note that we currently do this after installing the interrupt handler,
 * but before we enable the master interrupt. That should be sufficient
 * to avoid races with the irq handler, assuming we have MSI. Shared legacy
 * interrupts could still race.
 */
static void ibx_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 mask;

	if (HAS_PCH_NOP(dev_priv))
		return;

	if (HAS_PCH_IBX(dev_priv))
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_POISON;
	else if (HAS_PCH_CPT(dev_priv) || HAS_PCH_LPT(dev_priv))
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT;
	else
		mask = SDE_GMBUS_CPT;

	GEN3_IRQ_INIT(uncore, SDE, ~mask, 0xffffffff);
}

static void ilk_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 display_mask, extra_mask;

	if (GRAPHICS_VER(dev_priv) >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_AUX_CHANNEL_A_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB | DE_ERR_INT_IVB |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_C) |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_B) |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_A) |
			      DE_DP_A_HOTPLUG_IVB);
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
				DE_AUX_CHANNEL_A | DE_PIPEB_CRC_DONE |
				DE_PIPEA_CRC_DONE | DE_POISON);
		extra_mask = (DE_PIPEA_VBLANK | DE_PIPEB_VBLANK |
			      DE_PIPEB_FIFO_UNDERRUN | DE_PIPEA_FIFO_UNDERRUN |
			      DE_PLANE_FLIP_DONE(PLANE_A) |
			      DE_PLANE_FLIP_DONE(PLANE_B) |
			      DE_DP_A_HOTPLUG);
	}

	if (IS_HASWELL(dev_priv)) {
		gen3_assert_iir_is_zero(uncore, EDP_PSR_IIR);
		display_mask |= DE_EDP_PSR_INT_HSW;
	}

	if (IS_IRONLAKE_M(dev_priv))
		extra_mask |= DE_PCU_EVENT;

	dev_priv->irq_mask = ~display_mask;

	ibx_irq_postinstall(dev_priv);

	gen5_gt_irq_postinstall(to_gt(dev_priv));

	GEN3_IRQ_INIT(uncore, DE, dev_priv->irq_mask,
		      display_mask | extra_mask);
}

void valleyview_enable_display_irqs(struct drm_i915_private *dev_priv)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	if (dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = true;

	if (intel_irqs_enabled(dev_priv)) {
		vlv_display_irq_reset(dev_priv);
		vlv_display_irq_postinstall(dev_priv);
	}
}

void valleyview_disable_display_irqs(struct drm_i915_private *dev_priv)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	if (!dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = false;

	if (intel_irqs_enabled(dev_priv))
		vlv_display_irq_reset(dev_priv);
}


static void valleyview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	gen5_gt_irq_postinstall(to_gt(dev_priv));

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);
	intel_uncore_posting_read(&dev_priv->uncore, VLV_MASTER_IER);
}

static void gen8_de_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	u32 de_pipe_masked = gen8_de_pipe_fault_mask(dev_priv) |
		GEN8_PIPE_CDCLK_CRC_DONE;
	u32 de_pipe_enables;
	u32 de_port_masked = gen8_de_port_aux_mask(dev_priv);
	u32 de_port_enables;
	u32 de_misc_masked = GEN8_DE_EDP_PSR;
	u32 trans_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);
	enum pipe pipe;

	if (!HAS_DISPLAY(dev_priv))
		return;

	if (DISPLAY_VER(dev_priv) <= 10)
		de_misc_masked |= GEN8_DE_MISC_GSE;

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		de_port_masked |= BXT_DE_PORT_GMBUS;

	if (DISPLAY_VER(dev_priv) >= 11) {
		enum port port;

		if (intel_bios_is_dsi_present(dev_priv, &port))
			de_port_masked |= DSI0_TE | DSI1_TE;
	}

	de_pipe_enables = de_pipe_masked |
		GEN8_PIPE_VBLANK |
		gen8_de_pipe_underrun_mask(dev_priv) |
		gen8_de_pipe_flip_done_mask(dev_priv);

	de_port_enables = de_port_masked;
	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		de_port_enables |= BXT_DE_PORT_HOTPLUG_MASK;
	else if (IS_BROADWELL(dev_priv))
		de_port_enables |= BDW_DE_PORT_HOTPLUG_MASK;

	if (DISPLAY_VER(dev_priv) >= 12) {
		enum transcoder trans;

		for_each_cpu_transcoder_masked(dev_priv, trans, trans_mask) {
			enum intel_display_power_domain domain;

			domain = POWER_DOMAIN_TRANSCODER(trans);
			if (!intel_display_power_is_enabled(dev_priv, domain))
				continue;

			gen3_assert_iir_is_zero(uncore, TRANS_PSR_IIR(trans));
		}
	} else {
		gen3_assert_iir_is_zero(uncore, EDP_PSR_IIR);
	}

	for_each_pipe(dev_priv, pipe) {
		dev_priv->de_irq_mask[pipe] = ~de_pipe_masked;

		if (intel_display_power_is_enabled(dev_priv,
				POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_INIT_NDX(uncore, DE_PIPE, pipe,
					  dev_priv->de_irq_mask[pipe],
					  de_pipe_enables);
	}

	GEN3_IRQ_INIT(uncore, GEN8_DE_PORT_, ~de_port_masked, de_port_enables);
	GEN3_IRQ_INIT(uncore, GEN8_DE_MISC_, ~de_misc_masked, de_misc_masked);

	if (IS_DISPLAY_VER(dev_priv, 11, 13)) {
		u32 de_hpd_masked = 0;
		u32 de_hpd_enables = GEN11_DE_TC_HOTPLUG_MASK |
				     GEN11_DE_TBT_HOTPLUG_MASK;

		GEN3_IRQ_INIT(uncore, GEN11_DE_HPD_, ~de_hpd_masked,
			      de_hpd_enables);
	}
}

static void mtp_irq_postinstall(struct drm_i915_private *i915)
{
	struct intel_uncore *uncore = &i915->uncore;
	u32 sde_mask = SDE_GMBUS_ICP | SDE_PICAINTERRUPT;
	u32 de_hpd_mask = XELPDP_AUX_TC_MASK;
	u32 de_hpd_enables = de_hpd_mask | XELPDP_DP_ALT_HOTPLUG_MASK |
			     XELPDP_TBT_HOTPLUG_MASK;

	GEN3_IRQ_INIT(uncore, PICAINTERRUPT_, ~de_hpd_mask,
		      de_hpd_enables);

	GEN3_IRQ_INIT(uncore, SDE, ~sde_mask, 0xffffffff);
}

static void icp_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 mask = SDE_GMBUS_ICP;

	GEN3_IRQ_INIT(uncore, SDE, ~mask, 0xffffffff);
}

static void gen8_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_irq_postinstall(dev_priv);
	else if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_postinstall(dev_priv);

	gen8_gt_irq_postinstall(to_gt(dev_priv));
	gen8_de_irq_postinstall(dev_priv);

	gen8_master_intr_enable(dev_priv->uncore.regs);
}

static void gen11_de_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (!HAS_DISPLAY(dev_priv))
		return;

	gen8_de_irq_postinstall(dev_priv);

	intel_uncore_write(&dev_priv->uncore, GEN11_DISPLAY_INT_CTL,
			   GEN11_DISPLAY_IRQ_ENABLE);
}

static void gen11_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_irq_postinstall(dev_priv);

	gen11_gt_irq_postinstall(gt);
	gen11_de_irq_postinstall(dev_priv);

	GEN3_IRQ_INIT(uncore, GEN11_GU_MISC_, ~gu_misc_masked, gu_misc_masked);

	gen11_master_intr_enable(uncore->regs);
	intel_uncore_posting_read(&dev_priv->uncore, GEN11_GFX_MSTR_IRQ);
}

static void dg1_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;

	gen11_gt_irq_postinstall(gt);

	GEN3_IRQ_INIT(uncore, GEN11_GU_MISC_, ~gu_misc_masked, gu_misc_masked);

	if (HAS_DISPLAY(dev_priv)) {
		if (DISPLAY_VER(dev_priv) >= 14)
			mtp_irq_postinstall(dev_priv);
		else
			icp_irq_postinstall(dev_priv);

		gen8_de_irq_postinstall(dev_priv);
		intel_uncore_write(&dev_priv->uncore, GEN11_DISPLAY_INT_CTL,
				   GEN11_DISPLAY_IRQ_ENABLE);
	}

	dg1_master_intr_enable(uncore->regs);
	intel_uncore_posting_read(uncore, DG1_MSTR_TILE_INTR);
}

static void cherryview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	gen8_gt_irq_postinstall(to_gt(dev_priv));

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	intel_uncore_posting_read(&dev_priv->uncore, GEN8_MASTER_IRQ);
}

static void i8xx_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	i9xx_pipestat_irq_reset(dev_priv);

	gen2_irq_reset(uncore);
	dev_priv->irq_mask = ~0u;
}

static u32 i9xx_error_mask(struct drm_i915_private *i915)
{
	/*
	 * On gen2/3 FBC generates (seemingly spurious)
	 * display INVALID_GTT/INVALID_GTT_PTE table errors.
	 *
	 * Also gen3 bspec has this to say:
	 * "DISPA_INVALID_GTT_PTE
	 "  [DevNapa] : Reserved. This bit does not reflect the page
	 "              table error for the display plane A."
	 *
	 * Unfortunately we can't mask off individual PGTBL_ER bits,
	 * so we just have to mask off all page table errors via EMR.
	 */
	if (HAS_FBC(i915))
		return ~I915_ERROR_MEMORY_REFRESH;
	else
		return ~(I915_ERROR_PAGE_TABLE |
			 I915_ERROR_MEMORY_REFRESH);
}

static void i8xx_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u16 enable_mask;

	intel_uncore_write16(uncore, EMR, i9xx_error_mask(dev_priv));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	gen2_irq_init(uncore, dev_priv->irq_mask, enable_mask);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void i8xx_error_irq_ack(struct drm_i915_private *i915,
			       u16 *eir, u16 *eir_stuck)
{
	struct intel_uncore *uncore = &i915->uncore;
	u16 emr;

	*eir = intel_uncore_read16(uncore, EIR);
	intel_uncore_write16(uncore, EIR, *eir);

	*eir_stuck = intel_uncore_read16(uncore, EIR);
	if (*eir_stuck == 0)
		return;

	/*
	 * Toggle all EMR bits to make sure we get an edge
	 * in the ISR master error bit if we don't clear
	 * all the EIR bits. Otherwise the edge triggered
	 * IIR on i965/g4x wouldn't notice that an interrupt
	 * is still pending. Also some EIR bits can't be
	 * cleared except by handling the underlying error
	 * (or by a GPU reset) so we mask any bit that
	 * remains set.
	 */
	emr = intel_uncore_read16(uncore, EMR);
	intel_uncore_write16(uncore, EMR, 0xffff);
	intel_uncore_write16(uncore, EMR, emr | *eir_stuck);
}

static void i8xx_error_irq_handler(struct drm_i915_private *dev_priv,
				   u16 eir, u16 eir_stuck)
{
	drm_dbg(&dev_priv->drm, "Master Error: EIR 0x%04x\n", eir);

	if (eir_stuck)
		drm_dbg(&dev_priv->drm, "EIR stuck: 0x%04x, masked\n",
			eir_stuck);

	drm_dbg(&dev_priv->drm, "PGTBL_ER: 0x%08x\n",
		intel_uncore_read(&dev_priv->uncore, PGTBL_ER));
}

static void i9xx_error_irq_ack(struct drm_i915_private *dev_priv,
			       u32 *eir, u32 *eir_stuck)
{
	u32 emr;

	*eir = intel_uncore_read(&dev_priv->uncore, EIR);
	intel_uncore_write(&dev_priv->uncore, EIR, *eir);

	*eir_stuck = intel_uncore_read(&dev_priv->uncore, EIR);
	if (*eir_stuck == 0)
		return;

	/*
	 * Toggle all EMR bits to make sure we get an edge
	 * in the ISR master error bit if we don't clear
	 * all the EIR bits. Otherwise the edge triggered
	 * IIR on i965/g4x wouldn't notice that an interrupt
	 * is still pending. Also some EIR bits can't be
	 * cleared except by handling the underlying error
	 * (or by a GPU reset) so we mask any bit that
	 * remains set.
	 */
	emr = intel_uncore_read(&dev_priv->uncore, EMR);
	intel_uncore_write(&dev_priv->uncore, EMR, 0xffffffff);
	intel_uncore_write(&dev_priv->uncore, EMR, emr | *eir_stuck);
}

static void i9xx_error_irq_handler(struct drm_i915_private *dev_priv,
				   u32 eir, u32 eir_stuck)
{
	drm_dbg(&dev_priv->drm, "Master Error, EIR 0x%08x\n", eir);

	if (eir_stuck)
		drm_dbg(&dev_priv->drm, "EIR stuck: 0x%08x, masked\n",
			eir_stuck);

	drm_dbg(&dev_priv->drm, "PGTBL_ER: 0x%08x\n",
		intel_uncore_read(&dev_priv->uncore, PGTBL_ER));
}

static irqreturn_t i8xx_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u16 eir = 0, eir_stuck = 0;
		u16 iir;

		iir = intel_uncore_read16(&dev_priv->uncore, GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i8xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		intel_uncore_write16(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0], iir);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i8xx_error_irq_handler(dev_priv, eir, eir_stuck);

		i8xx_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i915_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (I915_HAS_HOTPLUG(dev_priv)) {
		i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
		intel_uncore_rmw(&dev_priv->uncore, PORT_HOTPLUG_STAT, 0, 0);
	}

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN2_);
	dev_priv->irq_mask = ~0u;
}

static void i915_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;

	intel_uncore_write(uncore, EMR, i9xx_error_mask(dev_priv));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (I915_HAS_HOTPLUG(dev_priv)) {
		/* Enable in IER... */
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
		/* and unmask in IMR */
		dev_priv->irq_mask &= ~I915_DISPLAY_PORT_INTERRUPT;
	}

	GEN3_IRQ_INIT(uncore, GEN2_, dev_priv->irq_mask, enable_mask);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

	i915_enable_asle_pipestat(dev_priv);
}

static irqreturn_t i915_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 eir = 0, eir_stuck = 0;
		u32 hotplug_status = 0;
		u32 iir;

		iir = intel_uncore_read(&dev_priv->uncore, GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		if (I915_HAS_HOTPLUG(dev_priv) &&
		    iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		intel_uncore_write(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0], iir);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		i915_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i965_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
	intel_uncore_rmw(uncore, PORT_HOTPLUG_STAT, 0, 0);

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN2_);
	dev_priv->irq_mask = ~0u;
}

static u32 i965_error_mask(struct drm_i915_private *i915)
{
	/*
	 * Enable some error detection, note the instruction error mask
	 * bit is reserved, so we leave it masked.
	 *
	 * i965 FBC no longer generates spurious GTT errors,
	 * so we can always enable the page table errors.
	 */
	if (IS_G4X(i915))
		return ~(GM45_ERROR_PAGE_TABLE |
			 GM45_ERROR_MEM_PRIV |
			 GM45_ERROR_CP_PRIV |
			 I915_ERROR_MEMORY_REFRESH);
	else
		return ~(I915_ERROR_PAGE_TABLE |
			 I915_ERROR_MEMORY_REFRESH);
}

static void i965_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;

	intel_uncore_write(uncore, EMR, i965_error_mask(dev_priv));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PORT_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (IS_G4X(dev_priv))
		enable_mask |= I915_BSD_USER_INTERRUPT;

	GEN3_IRQ_INIT(uncore, GEN2_, dev_priv->irq_mask, enable_mask);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

	i915_enable_asle_pipestat(dev_priv);
}

static irqreturn_t i965_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 eir = 0, eir_stuck = 0;
		u32 hotplug_status = 0;
		u32 iir;

		iir = intel_uncore_read(&dev_priv->uncore, GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		intel_uncore_write(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0],
					    iir);

		if (iir & I915_BSD_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[VCS0],
					    iir >> 25);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		i965_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, IRQ_HANDLED);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

/**
 * intel_irq_init - initializes irq support
 * @dev_priv: i915 device instance
 *
 * This function initializes all the irq support including work items, timers
 * and all the vtables. It does not setup the interrupt itself though.
 */
void intel_irq_init(struct drm_i915_private *dev_priv)
{
	int i;

	INIT_WORK(&dev_priv->l3_parity.error_work, ivb_parity_work);
	for (i = 0; i < MAX_L3_SLICES; ++i)
		dev_priv->l3_parity.remap_info[i] = NULL;

	/* pre-gen11 the guc irqs bits are in the upper 16 bits of the pm reg */
	if (HAS_GT_UC(dev_priv) && GRAPHICS_VER(dev_priv) < 11)
		to_gt(dev_priv)->pm_guc_events = GUC_INTR_GUC2HOST << 16;

	if (!HAS_DISPLAY(dev_priv))
		return;

	dev_priv->drm.vblank_disable_immediate = true;

	/* Most platforms treat the display irq block as an always-on
	 * power domain. vlv/chv can disable it at runtime and need
	 * special care to avoid writing any of the display block registers
	 * outside of the power domain. We defer setting up the display irqs
	 * in this case to the runtime pm.
	 */
	dev_priv->display_irqs_enabled = true;
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		dev_priv->display_irqs_enabled = false;

	intel_hotplug_irq_init(dev_priv);
}

/**
 * intel_irq_fini - deinitializes IRQ support
 * @i915: i915 device instance
 *
 * This function deinitializes all the IRQ support.
 */
void intel_irq_fini(struct drm_i915_private *i915)
{
	int i;

	for (i = 0; i < MAX_L3_SLICES; ++i)
		kfree(i915->l3_parity.remap_info[i]);
}

static irq_handler_t intel_irq_handler(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			return cherryview_irq_handler;
		else if (IS_VALLEYVIEW(dev_priv))
			return valleyview_irq_handler;
		else if (GRAPHICS_VER(dev_priv) == 4)
			return i965_irq_handler;
		else if (GRAPHICS_VER(dev_priv) == 3)
			return i915_irq_handler;
		else
			return i8xx_irq_handler;
	} else {
		if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			return dg1_irq_handler;
		else if (GRAPHICS_VER(dev_priv) >= 11)
			return gen11_irq_handler;
		else if (GRAPHICS_VER(dev_priv) >= 8)
			return gen8_irq_handler;
		else
			return ilk_irq_handler;
	}
}

static void intel_irq_reset(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_reset(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 4)
			i965_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 3)
			i915_irq_reset(dev_priv);
		else
			i8xx_irq_reset(dev_priv);
	} else {
		if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			dg1_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 11)
			gen11_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 8)
			gen8_irq_reset(dev_priv);
		else
			ilk_irq_reset(dev_priv);
	}
}

static void intel_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_postinstall(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 4)
			i965_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 3)
			i915_irq_postinstall(dev_priv);
		else
			i8xx_irq_postinstall(dev_priv);
	} else {
		if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			dg1_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 11)
			gen11_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 8)
			gen8_irq_postinstall(dev_priv);
		else
			ilk_irq_postinstall(dev_priv);
	}
}

/**
 * intel_irq_install - enables the hardware interrupt
 * @dev_priv: i915 device instance
 *
 * This function enables the hardware interrupt handling, but leaves the hotplug
 * handling still disabled. It is called after intel_irq_init().
 *
 * In the driver load and resume code we need working interrupts in a few places
 * but don't want to deal with the hassle of concurrent probe and hotplug
 * workers. Hence the split into this two-stage approach.
 */
int intel_irq_install(struct drm_i915_private *dev_priv)
{
	int irq = to_pci_dev(dev_priv->drm.dev)->irq;
	int ret;

	/*
	 * We enable some interrupt sources in our postinstall hooks, so mark
	 * interrupts as enabled _before_ actually enabling them to avoid
	 * special cases in our ordering checks.
	 */
	dev_priv->runtime_pm.irqs_enabled = true;

	dev_priv->irq_enabled = true;

	intel_irq_reset(dev_priv);

	ret = request_irq(irq, intel_irq_handler(dev_priv),
			  IRQF_SHARED, DRIVER_NAME, dev_priv);
	if (ret < 0) {
		dev_priv->irq_enabled = false;
		return ret;
	}

	intel_irq_postinstall(dev_priv);

	return ret;
}

/**
 * intel_irq_uninstall - finilizes all irq handling
 * @dev_priv: i915 device instance
 *
 * This stops interrupt and hotplug handling and unregisters and frees all
 * resources acquired in the init functions.
 */
void intel_irq_uninstall(struct drm_i915_private *dev_priv)
{
	int irq = to_pci_dev(dev_priv->drm.dev)->irq;

	/*
	 * FIXME we can get called twice during driver probe
	 * error handling as well as during driver remove due to
	 * intel_display_driver_remove() calling us out of sequence.
	 * Would be nice if it didn't do that...
	 */
	if (!dev_priv->irq_enabled)
		return;

	dev_priv->irq_enabled = false;

	intel_irq_reset(dev_priv);

	free_irq(irq, dev_priv);

	intel_hpd_cancel_work(dev_priv);
	dev_priv->runtime_pm.irqs_enabled = false;
}

/**
 * intel_runtime_pm_disable_interrupts - runtime interrupt disabling
 * @dev_priv: i915 device instance
 *
 * This function is used to disable interrupts at runtime, both in the runtime
 * pm and the system suspend/resume code.
 */
void intel_runtime_pm_disable_interrupts(struct drm_i915_private *dev_priv)
{
	intel_irq_reset(dev_priv);
	dev_priv->runtime_pm.irqs_enabled = false;
	intel_synchronize_irq(dev_priv);
}

/**
 * intel_runtime_pm_enable_interrupts - runtime interrupt enabling
 * @dev_priv: i915 device instance
 *
 * This function is used to enable interrupts at runtime, both in the runtime
 * pm and the system suspend/resume code.
 */
void intel_runtime_pm_enable_interrupts(struct drm_i915_private *dev_priv)
{
	dev_priv->runtime_pm.irqs_enabled = true;
	intel_irq_reset(dev_priv);
	intel_irq_postinstall(dev_priv);
}

bool intel_irqs_enabled(struct drm_i915_private *dev_priv)
{
	return dev_priv->runtime_pm.irqs_enabled;
}

void intel_synchronize_irq(struct drm_i915_private *i915)
{
	synchronize_irq(to_pci_dev(i915->drm.dev)->irq);
}

void intel_synchronize_hardirq(struct drm_i915_private *i915)
{
	synchronize_hardirq(to_pci_dev(i915->drm.dev)->irq);
}
