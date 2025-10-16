// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_vblank.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "icl_dsi_regs.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_regs.h"
#include "intel_display_rpm.h"
#include "intel_display_rps.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dmc_wl.h"
#include "intel_dp_aux.h"
#include "intel_dsb.h"
#include "intel_fdi_regs.h"
#include "intel_fifo_underrun.h"
#include "intel_gmbus.h"
#include "intel_hotplug_irq.h"
#include "intel_pipe_crc_regs.h"
#include "intel_plane.h"
#include "intel_pmdemand.h"
#include "intel_psr.h"
#include "intel_psr_regs.h"
#include "intel_uncore.h"

static void
intel_display_irq_regs_init(struct intel_display *display, struct i915_irq_regs regs,
			    u32 imr_val, u32 ier_val)
{
	intel_dmc_wl_get(display, regs.imr);
	intel_dmc_wl_get(display, regs.ier);
	intel_dmc_wl_get(display, regs.iir);

	gen2_irq_init(to_intel_uncore(display->drm), regs, imr_val, ier_val);

	intel_dmc_wl_put(display, regs.iir);
	intel_dmc_wl_put(display, regs.ier);
	intel_dmc_wl_put(display, regs.imr);
}

static void
intel_display_irq_regs_reset(struct intel_display *display, struct i915_irq_regs regs)
{
	intel_dmc_wl_get(display, regs.imr);
	intel_dmc_wl_get(display, regs.ier);
	intel_dmc_wl_get(display, regs.iir);

	gen2_irq_reset(to_intel_uncore(display->drm), regs);

	intel_dmc_wl_put(display, regs.iir);
	intel_dmc_wl_put(display, regs.ier);
	intel_dmc_wl_put(display, regs.imr);
}

static void
intel_display_irq_regs_assert_irr_is_zero(struct intel_display *display, i915_reg_t reg)
{
	intel_dmc_wl_get(display, reg);

	gen2_assert_iir_is_zero(to_intel_uncore(display->drm), reg);

	intel_dmc_wl_put(display, reg);
}

struct pipe_fault_handler {
	bool (*handle)(struct intel_crtc *crtc, enum plane_id plane_id);
	u32 fault;
	enum plane_id plane_id;
};

static bool handle_plane_fault(struct intel_crtc *crtc, enum plane_id plane_id)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_plane_error error = {};
	struct intel_plane *plane;

	plane = intel_crtc_get_plane(crtc, plane_id);
	if (!plane || !plane->capture_error)
		return false;

	plane->capture_error(crtc, plane, &error);

	drm_err_ratelimited(display->drm,
			    "[CRTC:%d:%s][PLANE:%d:%s] fault (CTL=0x%x, SURF=0x%x, SURFLIVE=0x%x)\n",
			    crtc->base.base.id, crtc->base.name,
			    plane->base.base.id, plane->base.name,
			    error.ctl, error.surf, error.surflive);

	return true;
}

static void intel_pipe_fault_irq_handler(struct intel_display *display,
					 const struct pipe_fault_handler *handlers,
					 enum pipe pipe, u32 fault_errors)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
	const struct pipe_fault_handler *handler;

	for (handler = handlers; handler && handler->fault; handler++) {
		if ((fault_errors & handler->fault) == 0)
			continue;

		if (handler->handle(crtc, handler->plane_id))
			fault_errors &= ~handler->fault;
	}

	WARN_ONCE(fault_errors, "[CRTC:%d:%s] unreported faults 0x%x\n",
		  crtc->base.base.id, crtc->base.name, fault_errors);
}

static void
intel_handle_vblank(struct intel_display *display, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);

	drm_crtc_handle_vblank(&crtc->base);
}

/**
 * ilk_update_display_irq - update DEIMR
 * @display: display device
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void ilk_update_display_irq(struct intel_display *display,
			    u32 interrupt_mask, u32 enabled_irq_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 new_val;

	lockdep_assert_held(&display->irq.lock);
	drm_WARN_ON(display->drm, enabled_irq_mask & ~interrupt_mask);

	new_val = dev_priv->irq_mask;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->irq_mask &&
	    !drm_WARN_ON(display->drm, !intel_irqs_enabled(dev_priv))) {
		dev_priv->irq_mask = new_val;
		intel_de_write(display, DEIMR, dev_priv->irq_mask);
		intel_de_posting_read(display, DEIMR);
	}
}

void ilk_enable_display_irq(struct intel_display *display, u32 bits)
{
	ilk_update_display_irq(display, bits, bits);
}

void ilk_disable_display_irq(struct intel_display *display, u32 bits)
{
	ilk_update_display_irq(display, bits, 0);
}

/**
 * bdw_update_port_irq - update DE port interrupt
 * @display: display device
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void bdw_update_port_irq(struct intel_display *display,
			 u32 interrupt_mask, u32 enabled_irq_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 new_val;
	u32 old_val;

	lockdep_assert_held(&display->irq.lock);

	drm_WARN_ON(display->drm, enabled_irq_mask & ~interrupt_mask);

	if (drm_WARN_ON(display->drm, !intel_irqs_enabled(dev_priv)))
		return;

	old_val = intel_de_read(display, GEN8_DE_PORT_IMR);

	new_val = old_val;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != old_val) {
		intel_de_write(display, GEN8_DE_PORT_IMR, new_val);
		intel_de_posting_read(display, GEN8_DE_PORT_IMR);
	}
}

/**
 * bdw_update_pipe_irq - update DE pipe interrupt
 * @display: display device
 * @pipe: pipe whose interrupt to update
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void bdw_update_pipe_irq(struct intel_display *display,
				enum pipe pipe, u32 interrupt_mask,
				u32 enabled_irq_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 new_val;

	lockdep_assert_held(&display->irq.lock);

	drm_WARN_ON(display->drm, enabled_irq_mask & ~interrupt_mask);

	if (drm_WARN_ON(display->drm, !intel_irqs_enabled(dev_priv)))
		return;

	new_val = display->irq.de_irq_mask[pipe];
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != display->irq.de_irq_mask[pipe]) {
		display->irq.de_irq_mask[pipe] = new_val;
		intel_de_write(display, GEN8_DE_PIPE_IMR(pipe), display->irq.de_irq_mask[pipe]);
		intel_de_posting_read(display, GEN8_DE_PIPE_IMR(pipe));
	}
}

void bdw_enable_pipe_irq(struct intel_display *display,
			 enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(display, pipe, bits, bits);
}

void bdw_disable_pipe_irq(struct intel_display *display,
			  enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(display, pipe, bits, 0);
}

/**
 * ibx_display_interrupt_update - update SDEIMR
 * @display: display device
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void ibx_display_interrupt_update(struct intel_display *display,
				  u32 interrupt_mask,
				  u32 enabled_irq_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 sdeimr = intel_de_read(display, SDEIMR);

	sdeimr &= ~interrupt_mask;
	sdeimr |= (~enabled_irq_mask & interrupt_mask);

	drm_WARN_ON(display->drm, enabled_irq_mask & ~interrupt_mask);

	lockdep_assert_held(&display->irq.lock);

	if (drm_WARN_ON(display->drm, !intel_irqs_enabled(dev_priv)))
		return;

	intel_de_write(display, SDEIMR, sdeimr);
	intel_de_posting_read(display, SDEIMR);
}

void ibx_enable_display_interrupt(struct intel_display *display, u32 bits)
{
	ibx_display_interrupt_update(display, bits, bits);
}

void ibx_disable_display_interrupt(struct intel_display *display, u32 bits)
{
	ibx_display_interrupt_update(display, bits, 0);
}

u32 i915_pipestat_enable_mask(struct intel_display *display,
			      enum pipe pipe)
{
	u32 status_mask = display->irq.pipestat_irq_mask[pipe];
	u32 enable_mask = status_mask << 16;

	lockdep_assert_held(&display->irq.lock);

	if (DISPLAY_VER(display) < 5)
		goto out;

	/*
	 * On pipe A we don't support the PSR interrupt yet,
	 * on pipe B and C the same bit MBZ.
	 */
	if (drm_WARN_ON_ONCE(display->drm,
			     status_mask & PIPE_A_PSR_STATUS_VLV))
		return 0;
	/*
	 * On pipe B and C we don't support the PSR interrupt yet, on pipe
	 * A the same bit is for perf counters which we don't use either.
	 */
	if (drm_WARN_ON_ONCE(display->drm,
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
	drm_WARN_ONCE(display->drm,
		      enable_mask & ~PIPESTAT_INT_ENABLE_MASK ||
		      status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: enable_mask=0x%x, status_mask=0x%x\n",
		      pipe_name(pipe), enable_mask, status_mask);

	return enable_mask;
}

void i915_enable_pipestat(struct intel_display *display,
			  enum pipe pipe, u32 status_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	i915_reg_t reg = PIPESTAT(display, pipe);
	u32 enable_mask;

	drm_WARN_ONCE(display->drm, status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: status_mask=0x%x\n",
		      pipe_name(pipe), status_mask);

	lockdep_assert_held(&display->irq.lock);
	drm_WARN_ON(display->drm, !intel_irqs_enabled(dev_priv));

	if ((display->irq.pipestat_irq_mask[pipe] & status_mask) == status_mask)
		return;

	display->irq.pipestat_irq_mask[pipe] |= status_mask;
	enable_mask = i915_pipestat_enable_mask(display, pipe);

	intel_de_write(display, reg, enable_mask | status_mask);
	intel_de_posting_read(display, reg);
}

void i915_disable_pipestat(struct intel_display *display,
			   enum pipe pipe, u32 status_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	i915_reg_t reg = PIPESTAT(display, pipe);
	u32 enable_mask;

	drm_WARN_ONCE(display->drm, status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: status_mask=0x%x\n",
		      pipe_name(pipe), status_mask);

	lockdep_assert_held(&display->irq.lock);
	drm_WARN_ON(display->drm, !intel_irqs_enabled(dev_priv));

	if ((display->irq.pipestat_irq_mask[pipe] & status_mask) == 0)
		return;

	display->irq.pipestat_irq_mask[pipe] &= ~status_mask;
	enable_mask = i915_pipestat_enable_mask(display, pipe);

	intel_de_write(display, reg, enable_mask | status_mask);
	intel_de_posting_read(display, reg);
}

static bool i915_has_legacy_blc_interrupt(struct intel_display *display)
{
	if (display->platform.i85x)
		return true;

	if (display->platform.pineview)
		return true;

	return IS_DISPLAY_VER(display, 3, 4) && display->platform.mobile;
}

/* enable ASLE pipestat for OpRegion */
static void i915_enable_asle_pipestat(struct intel_display *display)
{
	if (!intel_opregion_asle_present(display))
		return;

	if (!i915_has_legacy_blc_interrupt(display))
		return;

	spin_lock_irq(&display->irq.lock);

	i915_enable_pipestat(display, PIPE_B, PIPE_LEGACY_BLC_EVENT_STATUS);
	if (DISPLAY_VER(display) >= 4)
		i915_enable_pipestat(display, PIPE_A,
				     PIPE_LEGACY_BLC_EVENT_STATUS);

	spin_unlock_irq(&display->irq.lock);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static void display_pipe_crc_irq_handler(struct intel_display *display,
					 enum pipe pipe,
					 u32 crc0, u32 crc1,
					 u32 crc2, u32 crc3,
					 u32 crc4)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
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
	    (DISPLAY_VER(display) >= 8 && pipe_crc->skipped == 1)) {
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
display_pipe_crc_irq_handler(struct intel_display *display,
			     enum pipe pipe,
			     u32 crc0, u32 crc1,
			     u32 crc2, u32 crc3,
			     u32 crc4) {}
#endif

static void flip_done_handler(struct intel_display *display,
			      enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);

	spin_lock(&display->drm->event_lock);

	if (crtc->flip_done_event) {
		trace_intel_crtc_flip_done(crtc);
		drm_crtc_send_vblank_event(&crtc->base, crtc->flip_done_event);
		crtc->flip_done_event = NULL;
	}

	spin_unlock(&display->drm->event_lock);
}

static void hsw_pipe_crc_irq_handler(struct intel_display *display,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(display, pipe,
				     intel_de_read(display, PIPE_CRC_RES_HSW(pipe)),
				     0, 0, 0, 0);
}

static void ivb_pipe_crc_irq_handler(struct intel_display *display,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(display, pipe,
				     intel_de_read(display, PIPE_CRC_RES_1_IVB(pipe)),
				     intel_de_read(display, PIPE_CRC_RES_2_IVB(pipe)),
				     intel_de_read(display, PIPE_CRC_RES_3_IVB(pipe)),
				     intel_de_read(display, PIPE_CRC_RES_4_IVB(pipe)),
				     intel_de_read(display, PIPE_CRC_RES_5_IVB(pipe)));
}

static void i9xx_pipe_crc_irq_handler(struct intel_display *display,
				      enum pipe pipe)
{
	u32 res1, res2;

	if (DISPLAY_VER(display) >= 3)
		res1 = intel_de_read(display, PIPE_CRC_RES_RES1_I915(display, pipe));
	else
		res1 = 0;

	if (DISPLAY_VER(display) >= 5 || display->platform.g4x)
		res2 = intel_de_read(display, PIPE_CRC_RES_RES2_G4X(display, pipe));
	else
		res2 = 0;

	display_pipe_crc_irq_handler(display, pipe,
				     intel_de_read(display, PIPE_CRC_RES_RED(display, pipe)),
				     intel_de_read(display, PIPE_CRC_RES_GREEN(display, pipe)),
				     intel_de_read(display, PIPE_CRC_RES_BLUE(display, pipe)),
				     res1, res2);
}

static void i9xx_pipestat_irq_reset(struct intel_display *display)
{
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		intel_de_write(display,
			       PIPESTAT(display, pipe),
			       PIPESTAT_INT_STATUS_MASK | PIPE_FIFO_UNDERRUN_STATUS);

		display->irq.pipestat_irq_mask[pipe] = 0;
	}
}

void i9xx_pipestat_irq_ack(struct intel_display *display,
			   u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	spin_lock(&display->irq.lock);

	if ((display->platform.valleyview || display->platform.cherryview) &&
	    !display->irq.vlv_display_irqs_enabled) {
		spin_unlock(&display->irq.lock);
		return;
	}

	for_each_pipe(display, pipe) {
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
			status_mask |= display->irq.pipestat_irq_mask[pipe];

		if (!status_mask)
			continue;

		reg = PIPESTAT(display, pipe);
		pipe_stats[pipe] = intel_de_read(display, reg) & status_mask;
		enable_mask = i915_pipestat_enable_mask(display, pipe);

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
			intel_de_write(display, reg, pipe_stats[pipe]);
			intel_de_write(display, reg, enable_mask);
		}
	}
	spin_unlock(&display->irq.lock);
}

void i915_pipestat_irq_handler(struct intel_display *display,
			       u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	bool blc_event = false;
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(display, pipe);

		if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
			blc_event = true;

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(display, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(display, pipe);
	}

	if (blc_event || (iir & I915_ASLE_INTERRUPT))
		intel_opregion_asle_intr(display);
}

void i965_pipestat_irq_handler(struct intel_display *display,
			       u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	bool blc_event = false;
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(display, pipe);

		if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
			blc_event = true;

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(display, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(display, pipe);
	}

	if (blc_event || (iir & I915_ASLE_INTERRUPT))
		intel_opregion_asle_intr(display);

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		intel_gmbus_irq_handler(display);
}

void valleyview_pipestat_irq_handler(struct intel_display *display,
				     u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS)
			intel_handle_vblank(display, pipe);

		if (pipe_stats[pipe] & PLANE_FLIP_DONE_INT_STATUS_VLV)
			flip_done_handler(display, pipe);

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(display, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(display, pipe);
	}

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		intel_gmbus_irq_handler(display);
}

static void ibx_irq_handler(struct intel_display *display, u32 pch_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK;

	ibx_hpd_irq_handler(display, hotplug_trigger);

	if (pch_iir & SDE_AUDIO_POWER_MASK) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK) >>
			       SDE_AUDIO_POWER_SHIFT);
		drm_dbg(display->drm, "PCH audio power change on port %d\n",
			port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK)
		intel_dp_aux_irq_handler(display);

	if (pch_iir & SDE_GMBUS)
		intel_gmbus_irq_handler(display);

	if (pch_iir & SDE_AUDIO_HDCP_MASK)
		drm_dbg(display->drm, "PCH HDCP audio interrupt\n");

	if (pch_iir & SDE_AUDIO_TRANS_MASK)
		drm_dbg(display->drm, "PCH transcoder audio interrupt\n");

	if (pch_iir & SDE_POISON)
		drm_err(display->drm, "PCH poison interrupt\n");

	if (pch_iir & SDE_FDI_MASK) {
		for_each_pipe(display, pipe)
			drm_dbg(display->drm, "  pipe %c FDI IIR: 0x%08x\n",
				pipe_name(pipe),
				intel_de_read(display, FDI_RX_IIR(pipe)));
	}

	if (pch_iir & (SDE_TRANSB_CRC_DONE | SDE_TRANSA_CRC_DONE))
		drm_dbg(display->drm, "PCH transcoder CRC done interrupt\n");

	if (pch_iir & (SDE_TRANSB_CRC_ERR | SDE_TRANSA_CRC_ERR))
		drm_dbg(display->drm,
			"PCH transcoder CRC error interrupt\n");

	if (pch_iir & SDE_TRANSA_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(display, PIPE_A);

	if (pch_iir & SDE_TRANSB_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(display, PIPE_B);
}

static u32 ivb_err_int_pipe_fault_mask(enum pipe pipe)
{
	switch (pipe) {
	case PIPE_A:
		return ERR_INT_SPRITE_A_FAULT |
			ERR_INT_PRIMARY_A_FAULT |
			ERR_INT_CURSOR_A_FAULT;
	case PIPE_B:
		return ERR_INT_SPRITE_B_FAULT |
			ERR_INT_PRIMARY_B_FAULT |
			ERR_INT_CURSOR_B_FAULT;
	case PIPE_C:
		return ERR_INT_SPRITE_C_FAULT |
			ERR_INT_PRIMARY_C_FAULT |
			ERR_INT_CURSOR_C_FAULT;
	default:
		return 0;
	}
}

static const struct pipe_fault_handler ivb_pipe_fault_handlers[] = {
	{ .fault = ERR_INT_SPRITE_A_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = ERR_INT_PRIMARY_A_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = ERR_INT_CURSOR_A_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{ .fault = ERR_INT_SPRITE_B_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = ERR_INT_PRIMARY_B_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = ERR_INT_CURSOR_B_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{ .fault = ERR_INT_SPRITE_C_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = ERR_INT_PRIMARY_C_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = ERR_INT_CURSOR_C_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static void ivb_err_int_handler(struct intel_display *display)
{
	u32 err_int = intel_de_read(display, GEN7_ERR_INT);
	enum pipe pipe;

	if (err_int & ERR_INT_POISON)
		drm_err(display->drm, "Poison interrupt\n");

	if (err_int & ERR_INT_INVALID_GTT_PTE)
		drm_err_ratelimited(display->drm, "Invalid GTT PTE\n");

	if (err_int & ERR_INT_INVALID_PTE_DATA)
		drm_err_ratelimited(display->drm, "Invalid PTE data\n");

	for_each_pipe(display, pipe) {
		u32 fault_errors;

		if (err_int & ERR_INT_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(display, pipe);

		if (err_int & ERR_INT_PIPE_CRC_DONE(pipe)) {
			if (display->platform.ivybridge)
				ivb_pipe_crc_irq_handler(display, pipe);
			else
				hsw_pipe_crc_irq_handler(display, pipe);
		}

		fault_errors = err_int & ivb_err_int_pipe_fault_mask(pipe);
		if (fault_errors)
			intel_pipe_fault_irq_handler(display, ivb_pipe_fault_handlers,
						     pipe, fault_errors);
	}

	intel_de_write(display, GEN7_ERR_INT, err_int);
}

static void cpt_serr_int_handler(struct intel_display *display)
{
	u32 serr_int = intel_de_read(display, SERR_INT);
	enum pipe pipe;

	if (serr_int & SERR_INT_POISON)
		drm_err(display->drm, "PCH poison interrupt\n");

	for_each_pipe(display, pipe)
		if (serr_int & SERR_INT_TRANS_FIFO_UNDERRUN(pipe))
			intel_pch_fifo_underrun_irq_handler(display, pipe);

	intel_de_write(display, SERR_INT, serr_int);
}

static void cpt_irq_handler(struct intel_display *display, u32 pch_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_CPT;

	ibx_hpd_irq_handler(display, hotplug_trigger);

	if (pch_iir & SDE_AUDIO_POWER_MASK_CPT) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK_CPT) >>
			       SDE_AUDIO_POWER_SHIFT_CPT);
		drm_dbg(display->drm, "PCH audio power change on port %c\n",
			port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK_CPT)
		intel_dp_aux_irq_handler(display);

	if (pch_iir & SDE_GMBUS_CPT)
		intel_gmbus_irq_handler(display);

	if (pch_iir & SDE_AUDIO_CP_REQ_CPT)
		drm_dbg(display->drm, "Audio CP request interrupt\n");

	if (pch_iir & SDE_AUDIO_CP_CHG_CPT)
		drm_dbg(display->drm, "Audio CP change interrupt\n");

	if (pch_iir & SDE_FDI_MASK_CPT) {
		for_each_pipe(display, pipe)
			drm_dbg(display->drm, "  pipe %c FDI IIR: 0x%08x\n",
				pipe_name(pipe),
				intel_de_read(display, FDI_RX_IIR(pipe)));
	}

	if (pch_iir & SDE_ERROR_CPT)
		cpt_serr_int_handler(display);
}

static u32 ilk_gtt_fault_pipe_fault_mask(enum pipe pipe)
{
	switch (pipe) {
	case PIPE_A:
		return GTT_FAULT_SPRITE_A_FAULT |
			GTT_FAULT_PRIMARY_A_FAULT |
			GTT_FAULT_CURSOR_A_FAULT;
	case PIPE_B:
		return GTT_FAULT_SPRITE_B_FAULT |
			GTT_FAULT_PRIMARY_B_FAULT |
			GTT_FAULT_CURSOR_B_FAULT;
	default:
		return 0;
	}
}

static const struct pipe_fault_handler ilk_pipe_fault_handlers[] = {
	{ .fault = GTT_FAULT_SPRITE_A_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = GTT_FAULT_SPRITE_B_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = GTT_FAULT_PRIMARY_A_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = GTT_FAULT_PRIMARY_B_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = GTT_FAULT_CURSOR_A_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{ .fault = GTT_FAULT_CURSOR_B_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static void ilk_gtt_fault_irq_handler(struct intel_display *display)
{
	enum pipe pipe;
	u32 gtt_fault;

	gtt_fault = intel_de_read(display, ILK_GTT_FAULT);
	intel_de_write(display, ILK_GTT_FAULT, gtt_fault);

	if (gtt_fault & GTT_FAULT_INVALID_GTT_PTE)
		drm_err_ratelimited(display->drm, "Invalid GTT PTE\n");

	if (gtt_fault & GTT_FAULT_INVALID_PTE_DATA)
		drm_err_ratelimited(display->drm, "Invalid PTE data\n");

	for_each_pipe(display, pipe) {
		u32 fault_errors;

		fault_errors = gtt_fault & ilk_gtt_fault_pipe_fault_mask(pipe);
		if (fault_errors)
			intel_pipe_fault_irq_handler(display, ilk_pipe_fault_handlers,
						     pipe, fault_errors);
	}
}

void ilk_display_irq_handler(struct intel_display *display, u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(display, hotplug_trigger);

	if (de_iir & DE_AUX_CHANNEL_A)
		intel_dp_aux_irq_handler(display);

	if (de_iir & DE_GSE)
		intel_opregion_asle_intr(display);

	if (de_iir & DE_POISON)
		drm_err(display->drm, "Poison interrupt\n");

	if (de_iir & DE_GTT_FAULT)
		ilk_gtt_fault_irq_handler(display);

	for_each_pipe(display, pipe) {
		if (de_iir & DE_PIPE_VBLANK(pipe))
			intel_handle_vblank(display, pipe);

		if (de_iir & DE_PLANE_FLIP_DONE(pipe))
			flip_done_handler(display, pipe);

		if (de_iir & DE_PIPE_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(display, pipe);

		if (de_iir & DE_PIPE_CRC_DONE(pipe))
			i9xx_pipe_crc_irq_handler(display, pipe);
	}

	/* check event from PCH */
	if (de_iir & DE_PCH_EVENT) {
		u32 pch_iir = intel_de_read(display, SDEIIR);

		if (HAS_PCH_CPT(display))
			cpt_irq_handler(display, pch_iir);
		else
			ibx_irq_handler(display, pch_iir);

		/* should clear PCH hotplug event before clear CPU irq */
		intel_de_write(display, SDEIIR, pch_iir);
	}

	if (DISPLAY_VER(display) == 5 && de_iir & DE_PCU_EVENT)
		ilk_display_rps_irq_handler(display);
}

void ivb_display_irq_handler(struct intel_display *display, u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG_IVB;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(display, hotplug_trigger);

	if (de_iir & DE_ERR_INT_IVB)
		ivb_err_int_handler(display);

	if (de_iir & DE_EDP_PSR_INT_HSW) {
		struct intel_encoder *encoder;

		for_each_intel_encoder_with_psr(display->drm, encoder) {
			struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
			u32 psr_iir;

			psr_iir = intel_de_rmw(display, EDP_PSR_IIR, 0, 0);
			intel_psr_irq_handler(intel_dp, psr_iir);
			break;
		}
	}

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		intel_dp_aux_irq_handler(display);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(display);

	for_each_pipe(display, pipe) {
		if (de_iir & DE_PIPE_VBLANK_IVB(pipe))
			intel_handle_vblank(display, pipe);

		if (de_iir & DE_PLANE_FLIP_DONE_IVB(pipe))
			flip_done_handler(display, pipe);
	}

	/* check event from PCH */
	if (!HAS_PCH_NOP(display) && (de_iir & DE_PCH_EVENT_IVB)) {
		u32 pch_iir = intel_de_read(display, SDEIIR);

		cpt_irq_handler(display, pch_iir);

		/* clear PCH hotplug event before clear CPU irq */
		intel_de_write(display, SDEIIR, pch_iir);
	}
}

static u32 gen8_de_port_aux_mask(struct intel_display *display)
{
	u32 mask;

	if (DISPLAY_VER(display) >= 20)
		return 0;
	else if (DISPLAY_VER(display) >= 14)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB;
	else if (DISPLAY_VER(display) >= 13)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC |
			XELPD_DE_PORT_AUX_DDID |
			XELPD_DE_PORT_AUX_DDIE |
			TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 |
			TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4;
	else if (DISPLAY_VER(display) >= 12)
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
	if (DISPLAY_VER(display) >= 9)
		mask |= GEN9_AUX_CHANNEL_B |
			GEN9_AUX_CHANNEL_C |
			GEN9_AUX_CHANNEL_D;

	if (DISPLAY_VER(display) == 11) {
		mask |= ICL_AUX_CHANNEL_F;
		mask |= ICL_AUX_CHANNEL_E;
	}

	return mask;
}

static u32 gen8_de_pipe_fault_mask(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 20)
		return MTL_PLANE_ATS_FAULT |
			GEN9_PIPE_CURSOR_FAULT |
			GEN11_PIPE_PLANE5_FAULT |
			GEN9_PIPE_PLANE4_FAULT |
			GEN9_PIPE_PLANE3_FAULT |
			GEN9_PIPE_PLANE2_FAULT |
			GEN9_PIPE_PLANE1_FAULT;
	else if (DISPLAY_VER(display) >= 14)
		return MTL_PIPEDMC_ATS_FAULT |
			MTL_PLANE_ATS_FAULT |
			GEN12_PIPEDMC_FAULT |
			GEN9_PIPE_CURSOR_FAULT |
			GEN11_PIPE_PLANE5_FAULT |
			GEN9_PIPE_PLANE4_FAULT |
			GEN9_PIPE_PLANE3_FAULT |
			GEN9_PIPE_PLANE2_FAULT |
			GEN9_PIPE_PLANE1_FAULT;
	else if (DISPLAY_VER(display) >= 13 || HAS_D12_PLANE_MINIMIZATION(display))
		return GEN12_PIPEDMC_FAULT |
			GEN9_PIPE_CURSOR_FAULT |
			GEN11_PIPE_PLANE5_FAULT |
			GEN9_PIPE_PLANE4_FAULT |
			GEN9_PIPE_PLANE3_FAULT |
			GEN9_PIPE_PLANE2_FAULT |
			GEN9_PIPE_PLANE1_FAULT;
	else if (DISPLAY_VER(display) == 12)
		return GEN12_PIPEDMC_FAULT |
			GEN9_PIPE_CURSOR_FAULT |
			GEN11_PIPE_PLANE7_FAULT |
			GEN11_PIPE_PLANE6_FAULT |
			GEN11_PIPE_PLANE5_FAULT |
			GEN9_PIPE_PLANE4_FAULT |
			GEN9_PIPE_PLANE3_FAULT |
			GEN9_PIPE_PLANE2_FAULT |
			GEN9_PIPE_PLANE1_FAULT;
	else if (DISPLAY_VER(display) == 11)
		return GEN9_PIPE_CURSOR_FAULT |
			GEN11_PIPE_PLANE7_FAULT |
			GEN11_PIPE_PLANE6_FAULT |
			GEN11_PIPE_PLANE5_FAULT |
			GEN9_PIPE_PLANE4_FAULT |
			GEN9_PIPE_PLANE3_FAULT |
			GEN9_PIPE_PLANE2_FAULT |
			GEN9_PIPE_PLANE1_FAULT;
	else if (DISPLAY_VER(display) >= 9)
		return GEN9_PIPE_CURSOR_FAULT |
			GEN9_PIPE_PLANE4_FAULT |
			GEN9_PIPE_PLANE3_FAULT |
			GEN9_PIPE_PLANE2_FAULT |
			GEN9_PIPE_PLANE1_FAULT;
	else
		return GEN8_PIPE_CURSOR_FAULT |
			GEN8_PIPE_SPRITE_FAULT |
			GEN8_PIPE_PRIMARY_FAULT;
}

static bool handle_plane_ats_fault(struct intel_crtc *crtc, enum plane_id plane_id)
{
	struct intel_display *display = to_intel_display(crtc);

	drm_err_ratelimited(display->drm,
			    "[CRTC:%d:%s] PLANE ATS fault\n",
			    crtc->base.base.id, crtc->base.name);

	return true;
}

static bool handle_pipedmc_ats_fault(struct intel_crtc *crtc, enum plane_id plane_id)
{
	struct intel_display *display = to_intel_display(crtc);

	drm_err_ratelimited(display->drm,
			    "[CRTC:%d:%s] PIPEDMC ATS fault\n",
			    crtc->base.base.id, crtc->base.name);

	return true;
}

static bool handle_pipedmc_fault(struct intel_crtc *crtc, enum plane_id plane_id)
{
	struct intel_display *display = to_intel_display(crtc);

	drm_err_ratelimited(display->drm,
			    "[CRTC:%d:%s] PIPEDMC fault\n",
			    crtc->base.base.id, crtc->base.name);

	return true;
}

static const struct pipe_fault_handler mtl_pipe_fault_handlers[] = {
	{ .fault = MTL_PLANE_ATS_FAULT,     .handle = handle_plane_ats_fault, },
	{ .fault = MTL_PIPEDMC_ATS_FAULT,   .handle = handle_pipedmc_ats_fault, },
	{ .fault = GEN12_PIPEDMC_FAULT,     .handle = handle_pipedmc_fault, },
	{ .fault = GEN11_PIPE_PLANE5_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_5, },
	{ .fault = GEN9_PIPE_PLANE4_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_4, },
	{ .fault = GEN9_PIPE_PLANE3_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_3, },
	{ .fault = GEN9_PIPE_PLANE2_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_2, },
	{ .fault = GEN9_PIPE_PLANE1_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_1, },
	{ .fault = GEN9_PIPE_CURSOR_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static const struct pipe_fault_handler tgl_pipe_fault_handlers[] = {
	{ .fault = GEN12_PIPEDMC_FAULT,     .handle = handle_pipedmc_fault, },
	{ .fault = GEN11_PIPE_PLANE7_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_7, },
	{ .fault = GEN11_PIPE_PLANE6_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_6, },
	{ .fault = GEN11_PIPE_PLANE5_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_5, },
	{ .fault = GEN9_PIPE_PLANE4_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_4, },
	{ .fault = GEN9_PIPE_PLANE3_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_3, },
	{ .fault = GEN9_PIPE_PLANE2_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_2, },
	{ .fault = GEN9_PIPE_PLANE1_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_1, },
	{ .fault = GEN9_PIPE_CURSOR_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static const struct pipe_fault_handler icl_pipe_fault_handlers[] = {
	{ .fault = GEN11_PIPE_PLANE7_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_7, },
	{ .fault = GEN11_PIPE_PLANE6_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_6, },
	{ .fault = GEN11_PIPE_PLANE5_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_5, },
	{ .fault = GEN9_PIPE_PLANE4_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_4, },
	{ .fault = GEN9_PIPE_PLANE3_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_3, },
	{ .fault = GEN9_PIPE_PLANE2_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_2, },
	{ .fault = GEN9_PIPE_PLANE1_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_1, },
	{ .fault = GEN9_PIPE_CURSOR_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static const struct pipe_fault_handler skl_pipe_fault_handlers[] = {
	{ .fault = GEN9_PIPE_PLANE4_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_4, },
	{ .fault = GEN9_PIPE_PLANE3_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_3, },
	{ .fault = GEN9_PIPE_PLANE2_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_2, },
	{ .fault = GEN9_PIPE_PLANE1_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_1, },
	{ .fault = GEN9_PIPE_CURSOR_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static const struct pipe_fault_handler bdw_pipe_fault_handlers[] = {
	{ .fault = GEN8_PIPE_SPRITE_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = GEN8_PIPE_PRIMARY_FAULT, .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = GEN8_PIPE_CURSOR_FAULT,  .handle = handle_plane_fault, .plane_id = PLANE_CURSOR, },
	{}
};

static const struct pipe_fault_handler *
gen8_pipe_fault_handlers(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 14)
		return mtl_pipe_fault_handlers;
	else if (DISPLAY_VER(display) >= 12)
		return tgl_pipe_fault_handlers;
	else if (DISPLAY_VER(display) >= 11)
		return icl_pipe_fault_handlers;
	else if (DISPLAY_VER(display) >= 9)
		return skl_pipe_fault_handlers;
	else
		return bdw_pipe_fault_handlers;
}

static void intel_pmdemand_irq_handler(struct intel_display *display)
{
	wake_up_all(&display->pmdemand.waitqueue);
}

static void
gen8_de_misc_irq_handler(struct intel_display *display, u32 iir)
{
	bool found = false;

	if (HAS_DBUF_OVERLAP_DETECTION(display)) {
		if (iir & XE2LPD_DBUF_OVERLAP_DETECTED) {
			drm_warn(display->drm,  "DBuf overlap detected\n");
			found = true;
		}
	}

	if (DISPLAY_VER(display) >= 14) {
		if (iir & (XELPDP_PMDEMAND_RSP |
			   XELPDP_PMDEMAND_RSPTOUT_ERR)) {
			if (iir & XELPDP_PMDEMAND_RSPTOUT_ERR)
				drm_dbg(display->drm,
					"Error waiting for Punit PM Demand Response\n");

			intel_pmdemand_irq_handler(display);
			found = true;
		}

		if (iir & XELPDP_RM_TIMEOUT) {
			u32 val = intel_de_read(display, RM_TIMEOUT_REG_CAPTURE);
			drm_warn(display->drm, "Register Access Timeout = 0x%x\n", val);
			found = true;
		}
	} else if (iir & GEN8_DE_MISC_GSE) {
		intel_opregion_asle_intr(display);
		found = true;
	}

	if (iir & GEN8_DE_EDP_PSR) {
		struct intel_encoder *encoder;
		u32 psr_iir;
		i915_reg_t iir_reg;

		for_each_intel_encoder_with_psr(display->drm, encoder) {
			struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

			if (DISPLAY_VER(display) >= 12)
				iir_reg = TRANS_PSR_IIR(display,
							intel_dp->psr.transcoder);
			else
				iir_reg = EDP_PSR_IIR;

			psr_iir = intel_de_rmw(display, iir_reg, 0, 0);

			if (psr_iir)
				found = true;

			intel_psr_irq_handler(intel_dp, psr_iir);

			/* prior GEN12 only have one EDP PSR */
			if (DISPLAY_VER(display) < 12)
				break;
		}
	}

	if (!found)
		drm_err(display->drm, "Unexpected DE Misc interrupt: 0x%08x\n", iir);
}

static void gen11_dsi_te_interrupt_handler(struct intel_display *display,
					   u32 te_trigger)
{
	enum pipe pipe = INVALID_PIPE;
	enum transcoder dsi_trans;
	enum port port;
	u32 val;

	/*
	 * Incase of dual link, TE comes from DSI_1
	 * this is to check if dual link is enabled
	 */
	val = intel_de_read(display, TRANS_DDI_FUNC_CTL2(display, TRANSCODER_DSI_0));
	val &= PORT_SYNC_MODE_ENABLE;

	/*
	 * if dual link is enabled, then read DSI_0
	 * transcoder registers
	 */
	port = ((te_trigger & DSI1_TE && val) || (te_trigger & DSI0_TE)) ?
						  PORT_A : PORT_B;
	dsi_trans = (port == PORT_A) ? TRANSCODER_DSI_0 : TRANSCODER_DSI_1;

	/* Check if DSI configured in command mode */
	val = intel_de_read(display, DSI_TRANS_FUNC_CONF(dsi_trans));
	val = val & OP_MODE_MASK;

	if (val != CMD_MODE_NO_GATE && val != CMD_MODE_TE_GATE) {
		drm_err(display->drm, "DSI trancoder not configured in command mode\n");
		return;
	}

	/* Get PIPE for handling VBLANK event */
	val = intel_de_read(display, TRANS_DDI_FUNC_CTL(display, dsi_trans));
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
		drm_err(display->drm, "Invalid PIPE\n");
		return;
	}

	intel_handle_vblank(display, pipe);

	/* clear TE in dsi IIR */
	port = (te_trigger & DSI1_TE) ? PORT_B : PORT_A;
	intel_de_rmw(display, DSI_INTR_IDENT_REG(port), 0, 0);
}

static u32 gen8_de_pipe_flip_done_mask(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 9)
		return GEN9_PIPE_PLANE1_FLIP_DONE;
	else
		return GEN8_PIPE_PRIMARY_FLIP_DONE;
}

static void gen8_read_and_ack_pch_irqs(struct intel_display *display, u32 *pch_iir, u32 *pica_iir)
{
	u32 pica_ier = 0;

	*pica_iir = 0;
	*pch_iir = intel_de_read(display, SDEIIR);
	if (!*pch_iir)
		return;

	/**
	 * PICA IER must be disabled/re-enabled around clearing PICA IIR and
	 * SDEIIR, to avoid losing PICA IRQs and to ensure that such IRQs set
	 * their flags both in the PICA and SDE IIR.
	 */
	if (*pch_iir & SDE_PICAINTERRUPT) {
		drm_WARN_ON(display->drm, INTEL_PCH_TYPE(display) < PCH_MTL);

		pica_ier = intel_de_rmw(display, PICAINTERRUPT_IER, ~0, 0);
		*pica_iir = intel_de_read(display, PICAINTERRUPT_IIR);
		intel_de_write(display, PICAINTERRUPT_IIR, *pica_iir);
	}

	intel_de_write(display, SDEIIR, *pch_iir);

	if (pica_ier)
		intel_de_write(display, PICAINTERRUPT_IER, pica_ier);
}

void gen8_de_irq_handler(struct intel_display *display, u32 master_ctl)
{
	u32 iir;
	enum pipe pipe;

	drm_WARN_ON_ONCE(display->drm, !HAS_DISPLAY(display));

	if (master_ctl & GEN8_DE_MISC_IRQ) {
		iir = intel_de_read(display, GEN8_DE_MISC_IIR);
		if (iir) {
			intel_de_write(display, GEN8_DE_MISC_IIR, iir);
			gen8_de_misc_irq_handler(display, iir);
		} else {
			drm_err_ratelimited(display->drm,
					    "The master control interrupt lied (DE MISC)!\n");
		}
	}

	if (DISPLAY_VER(display) >= 11 && (master_ctl & GEN11_DE_HPD_IRQ)) {
		iir = intel_de_read(display, GEN11_DE_HPD_IIR);
		if (iir) {
			intel_de_write(display, GEN11_DE_HPD_IIR, iir);
			gen11_hpd_irq_handler(display, iir);
		} else {
			drm_err_ratelimited(display->drm,
					    "The master control interrupt lied, (DE HPD)!\n");
		}
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		iir = intel_de_read(display, GEN8_DE_PORT_IIR);
		if (iir) {
			bool found = false;

			intel_de_write(display, GEN8_DE_PORT_IIR, iir);

			if (iir & gen8_de_port_aux_mask(display)) {
				intel_dp_aux_irq_handler(display);
				found = true;
			}

			if (display->platform.geminilake || display->platform.broxton) {
				u32 hotplug_trigger = iir & BXT_DE_PORT_HOTPLUG_MASK;

				if (hotplug_trigger) {
					bxt_hpd_irq_handler(display, hotplug_trigger);
					found = true;
				}
			} else if (display->platform.broadwell) {
				u32 hotplug_trigger = iir & BDW_DE_PORT_HOTPLUG_MASK;

				if (hotplug_trigger) {
					ilk_hpd_irq_handler(display, hotplug_trigger);
					found = true;
				}
			}

			if ((display->platform.geminilake || display->platform.broxton) &&
			    (iir & BXT_DE_PORT_GMBUS)) {
				intel_gmbus_irq_handler(display);
				found = true;
			}

			if (DISPLAY_VER(display) >= 11) {
				u32 te_trigger = iir & (DSI0_TE | DSI1_TE);

				if (te_trigger) {
					gen11_dsi_te_interrupt_handler(display, te_trigger);
					found = true;
				}
			}

			if (!found)
				drm_err_ratelimited(display->drm,
						    "Unexpected DE Port interrupt\n");
		} else {
			drm_err_ratelimited(display->drm,
					    "The master control interrupt lied (DE PORT)!\n");
		}
	}

	for_each_pipe(display, pipe) {
		u32 fault_errors;

		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		iir = intel_de_read(display, GEN8_DE_PIPE_IIR(pipe));
		if (!iir) {
			drm_err_ratelimited(display->drm,
					    "The master control interrupt lied (DE PIPE %c)!\n",
					    pipe_name(pipe));
			continue;
		}

		intel_de_write(display, GEN8_DE_PIPE_IIR(pipe), iir);

		if (iir & GEN8_PIPE_VBLANK)
			intel_handle_vblank(display, pipe);

		if (iir & gen8_de_pipe_flip_done_mask(display))
			flip_done_handler(display, pipe);

		if (HAS_DSB(display)) {
			if (iir & GEN12_DSB_INT(INTEL_DSB_0))
				intel_dsb_irq_handler(display, pipe, INTEL_DSB_0);

			if (iir & GEN12_DSB_INT(INTEL_DSB_1))
				intel_dsb_irq_handler(display, pipe, INTEL_DSB_1);

			if (iir & GEN12_DSB_INT(INTEL_DSB_2))
				intel_dsb_irq_handler(display, pipe, INTEL_DSB_2);
		}

		if (HAS_PIPEDMC(display) && iir & GEN12_PIPEDMC_INTERRUPT)
			intel_pipedmc_irq_handler(display, pipe);

		if (iir & GEN8_PIPE_CDCLK_CRC_DONE)
			hsw_pipe_crc_irq_handler(display, pipe);

		if (iir & GEN8_PIPE_FIFO_UNDERRUN)
			intel_cpu_fifo_underrun_irq_handler(display, pipe);

		fault_errors = iir & gen8_de_pipe_fault_mask(display);
		if (fault_errors)
			intel_pipe_fault_irq_handler(display,
						     gen8_pipe_fault_handlers(display),
						     pipe, fault_errors);
	}

	if (HAS_PCH_SPLIT(display) && !HAS_PCH_NOP(display) &&
	    master_ctl & GEN8_DE_PCH_IRQ) {
		u32 pica_iir;

		/*
		 * FIXME(BDW): Assume for now that the new interrupt handling
		 * scheme also closed the SDE interrupt handling race we've seen
		 * on older pch-split platforms. But this needs testing.
		 */
		gen8_read_and_ack_pch_irqs(display, &iir, &pica_iir);
		if (iir) {
			if (pica_iir)
				xelpdp_pica_irq_handler(display, pica_iir);

			if (INTEL_PCH_TYPE(display) >= PCH_ICP)
				icp_irq_handler(display, iir);
			else if (INTEL_PCH_TYPE(display) >= PCH_SPT)
				spt_irq_handler(display, iir);
			else
				cpt_irq_handler(display, iir);
		} else {
			/*
			 * Like on previous PCH there seems to be something
			 * fishy going on with forwarding PCH interrupts.
			 */
			drm_dbg(display->drm,
				"The master control interrupt lied (SDE)!\n");
		}
	}
}

u32 gen11_gu_misc_irq_ack(struct intel_display *display, const u32 master_ctl)
{
	u32 iir;

	if (!(master_ctl & GEN11_GU_MISC_IRQ))
		return 0;

	intel_display_rpm_assert_block(display);

	iir = intel_de_read(display, GEN11_GU_MISC_IIR);
	if (likely(iir))
		intel_de_write(display, GEN11_GU_MISC_IIR, iir);

	intel_display_rpm_assert_unblock(display);

	return iir;
}

void gen11_gu_misc_irq_handler(struct intel_display *display, const u32 iir)
{
	if (iir & GEN11_GU_MISC_GSE)
		intel_opregion_asle_intr(display);
}

void gen11_display_irq_handler(struct intel_display *display)
{
	u32 disp_ctl;

	intel_display_rpm_assert_block(display);
	/*
	 * GEN11_DISPLAY_INT_CTL has same format as GEN8_MASTER_IRQ
	 * for the display related bits.
	 */
	disp_ctl = intel_de_read(display, GEN11_DISPLAY_INT_CTL);

	intel_de_write(display, GEN11_DISPLAY_INT_CTL, 0);
	gen8_de_irq_handler(display, disp_ctl);
	intel_de_write(display, GEN11_DISPLAY_INT_CTL, GEN11_DISPLAY_IRQ_ENABLE);

	intel_display_rpm_assert_unblock(display);
}

static void i915gm_irq_cstate_wa_enable(struct intel_display *display)
{
	lockdep_assert_held(&display->drm->vblank_time_lock);

	/*
	 * Vblank/CRC interrupts fail to wake the device up from C2+.
	 * Disabling render clock gating during C-states avoids
	 * the problem. There is a small power cost so we do this
	 * only when vblank/CRC interrupts are actually enabled.
	 */
	if (display->irq.vblank_enabled++ == 0)
		intel_de_write(display, SCPD0,
			       _MASKED_BIT_ENABLE(CSTATE_RENDER_CLOCK_GATE_DISABLE));
}

static void i915gm_irq_cstate_wa_disable(struct intel_display *display)
{
	lockdep_assert_held(&display->drm->vblank_time_lock);

	if (--display->irq.vblank_enabled == 0)
		intel_de_write(display, SCPD0,
			       _MASKED_BIT_DISABLE(CSTATE_RENDER_CLOCK_GATE_DISABLE));
}

void i915gm_irq_cstate_wa(struct intel_display *display, bool enable)
{
	spin_lock_irq(&display->drm->vblank_time_lock);

	if (enable)
		i915gm_irq_cstate_wa_enable(display);
	else
		i915gm_irq_cstate_wa_disable(display);

	spin_unlock_irq(&display->drm->vblank_time_lock);
}

int i8xx_enable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&display->irq.lock, irqflags);
	i915_enable_pipestat(display, pipe, PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);

	return 0;
}

void i8xx_disable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&display->irq.lock, irqflags);
	i915_disable_pipestat(display, pipe, PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);
}

int i915gm_enable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);

	i915gm_irq_cstate_wa_enable(display);

	return i8xx_enable_vblank(crtc);
}

void i915gm_disable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);

	i8xx_disable_vblank(crtc);

	i915gm_irq_cstate_wa_disable(display);
}

int i965_enable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&display->irq.lock, irqflags);
	i915_enable_pipestat(display, pipe,
			     PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);

	return 0;
}

void i965_disable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&display->irq.lock, irqflags);
	i915_disable_pipestat(display, pipe,
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);
}

int ilk_enable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;
	u32 bit = DISPLAY_VER(display) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&display->irq.lock, irqflags);
	ilk_enable_display_irq(display, bit);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);

	/* Even though there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated.
	 */
	if (HAS_PSR(display))
		drm_crtc_vblank_restore(crtc);

	return 0;
}

void ilk_disable_vblank(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;
	u32 bit = DISPLAY_VER(display) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&display->irq.lock, irqflags);
	ilk_disable_display_irq(display, bit);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);
}

static bool gen11_dsi_configure_te(struct intel_crtc *intel_crtc,
				   bool enable)
{
	struct intel_display *display = to_intel_display(intel_crtc);
	enum port port;

	if (!(intel_crtc->mode_flags &
	    (I915_MODE_FLAG_DSI_USE_TE1 | I915_MODE_FLAG_DSI_USE_TE0)))
		return false;

	/* for dual link cases we consider TE from slave */
	if (intel_crtc->mode_flags & I915_MODE_FLAG_DSI_USE_TE1)
		port = PORT_B;
	else
		port = PORT_A;

	intel_de_rmw(display, DSI_INTR_MASK_REG(port), DSI_TE_EVENT, enable ? 0 : DSI_TE_EVENT);

	intel_de_rmw(display, DSI_INTR_IDENT_REG(port), 0, 0);

	return true;
}

static void intel_display_vblank_notify_work(struct work_struct *work)
{
	struct intel_display *display =
		container_of(work, typeof(*display), irq.vblank_notify_work);
	int vblank_enable_count = READ_ONCE(display->irq.vblank_enable_count);

	intel_psr_notify_vblank_enable_disable(display, vblank_enable_count);
}

int bdw_enable_vblank(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;
	unsigned long irqflags;

	if (gen11_dsi_configure_te(crtc, true))
		return 0;

	if (crtc->vblank_psr_notify && display->irq.vblank_enable_count++ == 0)
		schedule_work(&display->irq.vblank_notify_work);

	spin_lock_irqsave(&display->irq.lock, irqflags);
	bdw_enable_pipe_irq(display, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);

	/* Even if there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated, so check only for PSR.
	 */
	if (HAS_PSR(display))
		drm_crtc_vblank_restore(&crtc->base);

	return 0;
}

void bdw_disable_vblank(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;
	unsigned long irqflags;

	if (gen11_dsi_configure_te(crtc, false))
		return;

	spin_lock_irqsave(&display->irq.lock, irqflags);
	bdw_disable_pipe_irq(display, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&display->irq.lock, irqflags);

	if (crtc->vblank_psr_notify && --display->irq.vblank_enable_count == 0)
		schedule_work(&display->irq.vblank_notify_work);
}

static u32 vlv_dpinvgtt_pipe_fault_mask(enum pipe pipe)
{
	switch (pipe) {
	case PIPE_A:
		return SPRITEB_INVALID_GTT_STATUS |
			SPRITEA_INVALID_GTT_STATUS |
			PLANEA_INVALID_GTT_STATUS |
			CURSORA_INVALID_GTT_STATUS;
	case PIPE_B:
		return SPRITED_INVALID_GTT_STATUS |
			SPRITEC_INVALID_GTT_STATUS |
			PLANEB_INVALID_GTT_STATUS |
			CURSORB_INVALID_GTT_STATUS;
	case PIPE_C:
		return SPRITEF_INVALID_GTT_STATUS |
			SPRITEE_INVALID_GTT_STATUS |
			PLANEC_INVALID_GTT_STATUS |
			CURSORC_INVALID_GTT_STATUS;
	default:
		return 0;
	}
}

static const struct pipe_fault_handler vlv_pipe_fault_handlers[] = {
	{ .fault = SPRITEB_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE1, },
	{ .fault = SPRITEA_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = PLANEA_INVALID_GTT_STATUS,  .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = CURSORA_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR,  },
	{ .fault = SPRITED_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE1, },
	{ .fault = SPRITEC_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = PLANEB_INVALID_GTT_STATUS,  .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = CURSORB_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR,  },
	{ .fault = SPRITEF_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE1, },
	{ .fault = SPRITEE_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_SPRITE0, },
	{ .fault = PLANEC_INVALID_GTT_STATUS,  .handle = handle_plane_fault, .plane_id = PLANE_PRIMARY, },
	{ .fault = CURSORC_INVALID_GTT_STATUS, .handle = handle_plane_fault, .plane_id = PLANE_CURSOR,  },
	{}
};

static void vlv_page_table_error_irq_ack(struct intel_display *display, u32 *dpinvgtt)
{
	u32 status, enable, tmp;

	tmp = intel_de_read(display, DPINVGTT);

	enable = tmp >> 16;
	status = tmp & 0xffff;

	/*
	 * Despite what the docs claim, the status bits seem to get
	 * stuck permanently (similar the old PGTBL_ER register), so
	 * we have to disable and ignore them once set. They do get
	 * reset if the display power well goes down, so no need to
	 * track the enable mask explicitly.
	 */
	*dpinvgtt = status & enable;
	enable &= ~status;

	/* customary ack+disable then re-enable to guarantee an edge */
	intel_de_write(display, DPINVGTT, status);
	intel_de_write(display, DPINVGTT, enable << 16);
}

static void vlv_page_table_error_irq_handler(struct intel_display *display, u32 dpinvgtt)
{
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		u32 fault_errors;

		fault_errors = dpinvgtt & vlv_dpinvgtt_pipe_fault_mask(pipe);
		if (fault_errors)
			intel_pipe_fault_irq_handler(display, vlv_pipe_fault_handlers,
						     pipe, fault_errors);
	}
}

void vlv_display_error_irq_ack(struct intel_display *display,
			       u32 *eir, u32 *dpinvgtt)
{
	u32 emr;

	*eir = intel_de_read(display, VLV_EIR);

	if (*eir & VLV_ERROR_PAGE_TABLE)
		vlv_page_table_error_irq_ack(display, dpinvgtt);

	intel_de_write(display, VLV_EIR, *eir);

	/*
	 * Toggle all EMR bits to make sure we get an edge
	 * in the ISR master error bit if we don't clear
	 * all the EIR bits.
	 */
	emr = intel_de_read(display, VLV_EMR);
	intel_de_write(display, VLV_EMR, 0xffffffff);
	intel_de_write(display, VLV_EMR, emr);
}

void vlv_display_error_irq_handler(struct intel_display *display,
				   u32 eir, u32 dpinvgtt)
{
	drm_dbg(display->drm, "Master Error, EIR 0x%08x\n", eir);

	if (eir & VLV_ERROR_PAGE_TABLE)
		vlv_page_table_error_irq_handler(display, dpinvgtt);
}

static void _vlv_display_irq_reset(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	if (display->platform.cherryview)
		intel_de_write(display, DPINVGTT, DPINVGTT_STATUS_MASK_CHV);
	else
		intel_de_write(display, DPINVGTT, DPINVGTT_STATUS_MASK_VLV);

	gen2_error_reset(to_intel_uncore(display->drm),
			 VLV_ERROR_REGS);

	i915_hotplug_interrupt_update_locked(display, 0xffffffff, 0);
	intel_de_rmw(display, PORT_HOTPLUG_STAT(display), 0, 0);

	i9xx_pipestat_irq_reset(display);

	intel_display_irq_regs_reset(display, VLV_IRQ_REGS);
	dev_priv->irq_mask = ~0u;
}

void vlv_display_irq_reset(struct intel_display *display)
{
	spin_lock_irq(&display->irq.lock);
	if (display->irq.vlv_display_irqs_enabled)
		_vlv_display_irq_reset(display);
	spin_unlock_irq(&display->irq.lock);
}

void i9xx_display_irq_reset(struct intel_display *display)
{
	if (HAS_HOTPLUG(display)) {
		i915_hotplug_interrupt_update(display, 0xffffffff, 0);
		intel_de_rmw(display, PORT_HOTPLUG_STAT(display), 0, 0);
	}

	i9xx_pipestat_irq_reset(display);
}

void i915_display_irq_postinstall(struct intel_display *display)
{
	/*
	 * Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy.
	 */
	spin_lock_irq(&display->irq.lock);
	i915_enable_pipestat(display, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(display, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&display->irq.lock);

	i915_enable_asle_pipestat(display);
}

void i965_display_irq_postinstall(struct intel_display *display)
{
	/*
	 * Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy.
	 */
	spin_lock_irq(&display->irq.lock);
	i915_enable_pipestat(display, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	i915_enable_pipestat(display, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(display, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&display->irq.lock);

	i915_enable_asle_pipestat(display);
}

static u32 vlv_error_mask(void)
{
	/* TODO enable other errors too? */
	return VLV_ERROR_PAGE_TABLE;
}

static void _vlv_display_irq_postinstall(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 pipestat_mask;
	u32 enable_mask;
	enum pipe pipe;

	if (display->platform.cherryview)
		intel_de_write(display, DPINVGTT,
			       DPINVGTT_STATUS_MASK_CHV |
			       DPINVGTT_EN_MASK_CHV);
	else
		intel_de_write(display, DPINVGTT,
			       DPINVGTT_STATUS_MASK_VLV |
			       DPINVGTT_EN_MASK_VLV);

	gen2_error_init(to_intel_uncore(display->drm),
			VLV_ERROR_REGS, ~vlv_error_mask());

	pipestat_mask = PIPE_CRC_DONE_INTERRUPT_STATUS;

	i915_enable_pipestat(display, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	for_each_pipe(display, pipe)
		i915_enable_pipestat(display, pipe, pipestat_mask);

	enable_mask = I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_LPE_PIPE_A_INTERRUPT |
		I915_LPE_PIPE_B_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT;

	if (display->platform.cherryview)
		enable_mask |= I915_DISPLAY_PIPE_C_EVENT_INTERRUPT |
			I915_LPE_PIPE_C_INTERRUPT;

	drm_WARN_ON(display->drm, dev_priv->irq_mask != ~0u);

	dev_priv->irq_mask = ~enable_mask;

	intel_display_irq_regs_init(display, VLV_IRQ_REGS, dev_priv->irq_mask, enable_mask);
}

void vlv_display_irq_postinstall(struct intel_display *display)
{
	spin_lock_irq(&display->irq.lock);
	if (display->irq.vlv_display_irqs_enabled)
		_vlv_display_irq_postinstall(display);
	spin_unlock_irq(&display->irq.lock);
}

void ibx_display_irq_reset(struct intel_display *display)
{
	if (HAS_PCH_NOP(display))
		return;

	gen2_irq_reset(to_intel_uncore(display->drm), SDE_IRQ_REGS);

	if (HAS_PCH_CPT(display) || HAS_PCH_LPT(display))
		intel_de_write(display, SERR_INT, 0xffffffff);
}

void gen8_display_irq_reset(struct intel_display *display)
{
	enum pipe pipe;

	if (!HAS_DISPLAY(display))
		return;

	intel_de_write(display, EDP_PSR_IMR, 0xffffffff);
	intel_de_write(display, EDP_PSR_IIR, 0xffffffff);

	for_each_pipe(display, pipe)
		if (intel_display_power_is_enabled(display,
						   POWER_DOMAIN_PIPE(pipe)))
			intel_display_irq_regs_reset(display, GEN8_DE_PIPE_IRQ_REGS(pipe));

	intel_display_irq_regs_reset(display, GEN8_DE_PORT_IRQ_REGS);
	intel_display_irq_regs_reset(display, GEN8_DE_MISC_IRQ_REGS);

	if (HAS_PCH_SPLIT(display))
		ibx_display_irq_reset(display);
}

void gen11_display_irq_reset(struct intel_display *display)
{
	enum pipe pipe;
	u32 trans_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);

	if (!HAS_DISPLAY(display))
		return;

	intel_de_write(display, GEN11_DISPLAY_INT_CTL, 0);

	if (DISPLAY_VER(display) >= 12) {
		enum transcoder trans;

		for_each_cpu_transcoder_masked(display, trans, trans_mask) {
			enum intel_display_power_domain domain;

			domain = POWER_DOMAIN_TRANSCODER(trans);
			if (!intel_display_power_is_enabled(display, domain))
				continue;

			intel_de_write(display,
				       TRANS_PSR_IMR(display, trans),
				       0xffffffff);
			intel_de_write(display,
				       TRANS_PSR_IIR(display, trans),
				       0xffffffff);
		}
	} else {
		intel_de_write(display, EDP_PSR_IMR, 0xffffffff);
		intel_de_write(display, EDP_PSR_IIR, 0xffffffff);
	}

	for_each_pipe(display, pipe)
		if (intel_display_power_is_enabled(display,
						   POWER_DOMAIN_PIPE(pipe)))
			intel_display_irq_regs_reset(display, GEN8_DE_PIPE_IRQ_REGS(pipe));

	intel_display_irq_regs_reset(display, GEN8_DE_PORT_IRQ_REGS);
	intel_display_irq_regs_reset(display, GEN8_DE_MISC_IRQ_REGS);

	if (DISPLAY_VER(display) >= 14)
		intel_display_irq_regs_reset(display, PICAINTERRUPT_IRQ_REGS);
	else
		intel_display_irq_regs_reset(display, GEN11_DE_HPD_IRQ_REGS);

	if (INTEL_PCH_TYPE(display) >= PCH_ICP)
		intel_display_irq_regs_reset(display, SDE_IRQ_REGS);
}

void gen8_irq_power_well_post_enable(struct intel_display *display,
				     u8 pipe_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 extra_ier = GEN8_PIPE_VBLANK | GEN8_PIPE_FIFO_UNDERRUN |
		gen8_de_pipe_flip_done_mask(display);
	enum pipe pipe;

	spin_lock_irq(&display->irq.lock);

	if (!intel_irqs_enabled(dev_priv)) {
		spin_unlock_irq(&display->irq.lock);
		return;
	}

	for_each_pipe_masked(display, pipe, pipe_mask)
		intel_display_irq_regs_init(display, GEN8_DE_PIPE_IRQ_REGS(pipe),
					    display->irq.de_irq_mask[pipe],
					    ~display->irq.de_irq_mask[pipe] | extra_ier);

	spin_unlock_irq(&display->irq.lock);
}

void gen8_irq_power_well_pre_disable(struct intel_display *display,
				     u8 pipe_mask)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	enum pipe pipe;

	spin_lock_irq(&display->irq.lock);

	if (!intel_irqs_enabled(dev_priv)) {
		spin_unlock_irq(&display->irq.lock);
		return;
	}

	for_each_pipe_masked(display, pipe, pipe_mask)
		intel_display_irq_regs_reset(display, GEN8_DE_PIPE_IRQ_REGS(pipe));

	spin_unlock_irq(&display->irq.lock);

	/* make sure we're done processing display irqs */
	intel_synchronize_irq(dev_priv);
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
static void ibx_irq_postinstall(struct intel_display *display)
{
	u32 mask;

	if (HAS_PCH_NOP(display))
		return;

	if (HAS_PCH_IBX(display))
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_POISON;
	else if (HAS_PCH_CPT(display) || HAS_PCH_LPT(display))
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT;
	else
		mask = SDE_GMBUS_CPT;

	intel_display_irq_regs_init(display, SDE_IRQ_REGS, ~mask, 0xffffffff);
}

void valleyview_enable_display_irqs(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	spin_lock_irq(&display->irq.lock);

	if (display->irq.vlv_display_irqs_enabled)
		goto out;

	display->irq.vlv_display_irqs_enabled = true;

	if (intel_irqs_enabled(dev_priv)) {
		_vlv_display_irq_reset(display);
		_vlv_display_irq_postinstall(display);
	}

out:
	spin_unlock_irq(&display->irq.lock);
}

void valleyview_disable_display_irqs(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	spin_lock_irq(&display->irq.lock);

	if (!display->irq.vlv_display_irqs_enabled)
		goto out;

	display->irq.vlv_display_irqs_enabled = false;

	if (intel_irqs_enabled(dev_priv))
		_vlv_display_irq_reset(display);
out:
	spin_unlock_irq(&display->irq.lock);
}

void ilk_de_irq_postinstall(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	u32 display_mask, extra_mask;

	if (DISPLAY_VER(display) >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_AUX_CHANNEL_A_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB | DE_ERR_INT_IVB |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_C) |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_B) |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_A) |
			      DE_DP_A_HOTPLUG_IVB);
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE |
				DE_PCH_EVENT | DE_GTT_FAULT |
				DE_AUX_CHANNEL_A | DE_PIPEB_CRC_DONE |
				DE_PIPEA_CRC_DONE | DE_POISON);
		extra_mask = (DE_PIPEA_VBLANK | DE_PIPEB_VBLANK |
			      DE_PIPEB_FIFO_UNDERRUN | DE_PIPEA_FIFO_UNDERRUN |
			      DE_PLANE_FLIP_DONE(PLANE_A) |
			      DE_PLANE_FLIP_DONE(PLANE_B) |
			      DE_DP_A_HOTPLUG);
	}

	if (display->platform.haswell) {
		intel_display_irq_regs_assert_irr_is_zero(display, EDP_PSR_IIR);
		display_mask |= DE_EDP_PSR_INT_HSW;
	}

	if (display->platform.ironlake && display->platform.mobile)
		extra_mask |= DE_PCU_EVENT;

	i915->irq_mask = ~display_mask;

	ibx_irq_postinstall(display);

	intel_display_irq_regs_init(display, DE_IRQ_REGS, i915->irq_mask,
				    display_mask | extra_mask);
}

static void mtp_irq_postinstall(struct intel_display *display);
static void icp_irq_postinstall(struct intel_display *display);

void gen8_de_irq_postinstall(struct intel_display *display)
{
	u32 de_pipe_masked = gen8_de_pipe_fault_mask(display) |
		GEN8_PIPE_CDCLK_CRC_DONE;
	u32 de_pipe_enables;
	u32 de_port_masked = gen8_de_port_aux_mask(display);
	u32 de_port_enables;
	u32 de_misc_masked = GEN8_DE_EDP_PSR;
	u32 trans_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);
	enum pipe pipe;

	if (!HAS_DISPLAY(display))
		return;

	if (DISPLAY_VER(display) >= 14)
		mtp_irq_postinstall(display);
	else if (INTEL_PCH_TYPE(display) >= PCH_ICP)
		icp_irq_postinstall(display);
	else if (HAS_PCH_SPLIT(display))
		ibx_irq_postinstall(display);

	if (DISPLAY_VER(display) < 11)
		de_misc_masked |= GEN8_DE_MISC_GSE;

	if (display->platform.geminilake || display->platform.broxton)
		de_port_masked |= BXT_DE_PORT_GMBUS;

	if (DISPLAY_VER(display) >= 14) {
		de_misc_masked |= XELPDP_PMDEMAND_RSPTOUT_ERR |
				  XELPDP_PMDEMAND_RSP | XELPDP_RM_TIMEOUT;
	} else if (DISPLAY_VER(display) >= 11) {
		enum port port;

		if (intel_bios_is_dsi_present(display, &port))
			de_port_masked |= DSI0_TE | DSI1_TE;
	}

	if (HAS_DBUF_OVERLAP_DETECTION(display))
		de_misc_masked |= XE2LPD_DBUF_OVERLAP_DETECTED;

	if (HAS_DSB(display))
		de_pipe_masked |= GEN12_DSB_INT(INTEL_DSB_0) |
			GEN12_DSB_INT(INTEL_DSB_1) |
			GEN12_DSB_INT(INTEL_DSB_2);

	/* TODO figure PIPEDMC interrupts for pre-LNL */
	if (DISPLAY_VER(display) >= 20)
		de_pipe_masked |= GEN12_PIPEDMC_INTERRUPT;

	de_pipe_enables = de_pipe_masked |
		GEN8_PIPE_VBLANK | GEN8_PIPE_FIFO_UNDERRUN |
		gen8_de_pipe_flip_done_mask(display);

	de_port_enables = de_port_masked;
	if (display->platform.geminilake || display->platform.broxton)
		de_port_enables |= BXT_DE_PORT_HOTPLUG_MASK;
	else if (display->platform.broadwell)
		de_port_enables |= BDW_DE_PORT_HOTPLUG_MASK;

	if (DISPLAY_VER(display) >= 12) {
		enum transcoder trans;

		for_each_cpu_transcoder_masked(display, trans, trans_mask) {
			enum intel_display_power_domain domain;

			domain = POWER_DOMAIN_TRANSCODER(trans);
			if (!intel_display_power_is_enabled(display, domain))
				continue;

			intel_display_irq_regs_assert_irr_is_zero(display,
								  TRANS_PSR_IIR(display, trans));
		}
	} else {
		intel_display_irq_regs_assert_irr_is_zero(display, EDP_PSR_IIR);
	}

	for_each_pipe(display, pipe) {
		display->irq.de_irq_mask[pipe] = ~de_pipe_masked;

		if (intel_display_power_is_enabled(display,
						   POWER_DOMAIN_PIPE(pipe)))
			intel_display_irq_regs_init(display, GEN8_DE_PIPE_IRQ_REGS(pipe),
						    display->irq.de_irq_mask[pipe],
						    de_pipe_enables);
	}

	intel_display_irq_regs_init(display, GEN8_DE_PORT_IRQ_REGS, ~de_port_masked,
				    de_port_enables);
	intel_display_irq_regs_init(display, GEN8_DE_MISC_IRQ_REGS, ~de_misc_masked,
				    de_misc_masked);

	if (IS_DISPLAY_VER(display, 11, 13)) {
		u32 de_hpd_masked = 0;
		u32 de_hpd_enables = GEN11_DE_TC_HOTPLUG_MASK |
				     GEN11_DE_TBT_HOTPLUG_MASK;

		intel_display_irq_regs_init(display, GEN11_DE_HPD_IRQ_REGS, ~de_hpd_masked,
					    de_hpd_enables);
	}
}

static void mtp_irq_postinstall(struct intel_display *display)
{
	u32 sde_mask = SDE_GMBUS_ICP | SDE_PICAINTERRUPT;
	u32 de_hpd_mask = XELPDP_AUX_TC_MASK;
	u32 de_hpd_enables = de_hpd_mask | XELPDP_DP_ALT_HOTPLUG_MASK |
			     XELPDP_TBT_HOTPLUG_MASK;

	intel_display_irq_regs_init(display, PICAINTERRUPT_IRQ_REGS, ~de_hpd_mask,
				    de_hpd_enables);

	intel_display_irq_regs_init(display, SDE_IRQ_REGS, ~sde_mask, 0xffffffff);
}

static void icp_irq_postinstall(struct intel_display *display)
{
	u32 mask = SDE_GMBUS_ICP;

	intel_display_irq_regs_init(display, SDE_IRQ_REGS, ~mask, 0xffffffff);
}

void gen11_de_irq_postinstall(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	gen8_de_irq_postinstall(display);

	intel_de_write(display, GEN11_DISPLAY_INT_CTL, GEN11_DISPLAY_IRQ_ENABLE);
}

void dg1_de_irq_postinstall(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	gen8_de_irq_postinstall(display);
	intel_de_write(display, GEN11_DISPLAY_INT_CTL, GEN11_DISPLAY_IRQ_ENABLE);
}

void intel_display_irq_init(struct intel_display *display)
{
	spin_lock_init(&display->irq.lock);

	display->drm->vblank_disable_immediate = true;

	intel_hotplug_irq_init(display);

	INIT_WORK(&display->irq.vblank_notify_work,
		  intel_display_vblank_notify_work);
}

struct intel_display_irq_snapshot {
	u32 derrmr;
};

struct intel_display_irq_snapshot *
intel_display_irq_snapshot_capture(struct intel_display *display)
{
	struct intel_display_irq_snapshot *snapshot;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);
	if (!snapshot)
		return NULL;

	if (DISPLAY_VER(display) >= 6 && DISPLAY_VER(display) < 20 && !HAS_GMCH(display))
		snapshot->derrmr = intel_de_read(display, DERRMR);

	return snapshot;
}

void intel_display_irq_snapshot_print(const struct intel_display_irq_snapshot *snapshot,
				      struct drm_printer *p)
{
	if (!snapshot)
		return;

	drm_printf(p, "DERRMR: 0x%08x\n", snapshot->derrmr);
}
