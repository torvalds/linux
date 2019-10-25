// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include "armada_plane.h"
#include "armada_trace.h"

/*
 * A note about interlacing.  Let's consider HDMI 1920x1080i.
 * The timing parameters we have from X are:
 *  Hact HsyA HsyI Htot  Vact VsyA VsyI Vtot
 *  1920 2448 2492 2640  1080 1084 1094 1125
 * Which get translated to:
 *  Hact HsyA HsyI Htot  Vact VsyA VsyI Vtot
 *  1920 2448 2492 2640   540  542  547  562
 *
 * This is how it is defined by CEA-861-D - line and pixel numbers are
 * referenced to the rising edge of VSYNC and HSYNC.  Total clocks per
 * line: 2640.  The odd frame, the first active line is at line 21, and
 * the even frame, the first active line is 584.
 *
 * LN:    560     561     562     563             567     568    569
 * DE:    ~~~|____________________________//__________________________
 * HSYNC: ____|~|_____|~|_____|~|_____|~|_//__|~|_____|~|_____|~|_____
 * VSYNC: _________________________|~~~~~~//~~~~~~~~~~~~~~~|__________
 *  22 blanking lines.  VSYNC at 1320 (referenced to the HSYNC rising edge).
 *
 * LN:    1123   1124    1125      1               5       6      7
 * DE:    ~~~|____________________________//__________________________
 * HSYNC: ____|~|_____|~|_____|~|_____|~|_//__|~|_____|~|_____|~|_____
 * VSYNC: ____________________|~~~~~~~~~~~//~~~~~~~~~~|_______________
 *  23 blanking lines
 *
 * The Armada LCD Controller line and pixel numbers are, like X timings,
 * referenced to the top left of the active frame.
 *
 * So, translating these to our LCD controller:
 *  Odd frame, 563 total lines, VSYNC at line 543-548, pixel 1128.
 *  Even frame, 562 total lines, VSYNC at line 542-547, pixel 2448.
 * Note: Vsync front porch remains constant!
 *
 * if (odd_frame) {
 *   vtotal = mode->crtc_vtotal + 1;
 *   vbackporch = mode->crtc_vsync_start - mode->crtc_vdisplay + 1;
 *   vhorizpos = mode->crtc_hsync_start - mode->crtc_htotal / 2
 * } else {
 *   vtotal = mode->crtc_vtotal;
 *   vbackporch = mode->crtc_vsync_start - mode->crtc_vdisplay;
 *   vhorizpos = mode->crtc_hsync_start;
 * }
 * vfrontporch = mode->crtc_vtotal - mode->crtc_vsync_end;
 *
 * So, we need to reprogram these registers on each vsync event:
 *  LCD_SPU_V_PORCH, LCD_SPU_ADV_REG, LCD_SPUT_V_H_TOTAL
 *
 * Note: we do not use the frame done interrupts because these appear
 * to happen too early, and lead to jitter on the display (presumably
 * they occur at the end of the last active line, before the vsync back
 * porch, which we're reprogramming.)
 */

void
armada_drm_crtc_update_regs(struct armada_crtc *dcrtc, struct armada_regs *regs)
{
	while (regs->offset != ~0) {
		void __iomem *reg = dcrtc->base + regs->offset;
		uint32_t val;

		val = regs->mask;
		if (val != 0)
			val &= readl_relaxed(reg);
		writel_relaxed(val | regs->val, reg);
		++regs;
	}
}

static void armada_drm_crtc_update(struct armada_crtc *dcrtc, bool enable)
{
	uint32_t dumb_ctrl;

	dumb_ctrl = dcrtc->cfg_dumb_ctrl;

	if (enable)
		dumb_ctrl |= CFG_DUMB_ENA;

	/*
	 * When the dumb interface isn't in DUMB24_RGB888_0 mode, it might
	 * be using SPI or GPIO.  If we set this to DUMB_BLANK, we will
	 * force LCD_D[23:0] to output blank color, overriding the GPIO or
	 * SPI usage.  So leave it as-is unless in DUMB24_RGB888_0 mode.
	 */
	if (!enable && (dumb_ctrl & DUMB_MASK) == DUMB24_RGB888_0) {
		dumb_ctrl &= ~DUMB_MASK;
		dumb_ctrl |= DUMB_BLANK;
	}

	armada_updatel(dumb_ctrl,
		       ~(CFG_INV_CSYNC | CFG_INV_HSYNC | CFG_INV_VSYNC),
		       dcrtc->base + LCD_SPU_DUMB_CTRL);
}

static void armada_drm_crtc_queue_state_event(struct drm_crtc *crtc)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct drm_pending_vblank_event *event;

	/* If we have an event, we need vblank events enabled */
	event = xchg(&crtc->state->event, NULL);
	if (event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		dcrtc->event = event;
	}
}

static void armada_drm_update_gamma(struct drm_crtc *crtc)
{
	struct drm_property_blob *blob = crtc->state->gamma_lut;
	void __iomem *base = drm_to_armada_crtc(crtc)->base;
	int i;

	if (blob) {
		struct drm_color_lut *lut = blob->data;

		armada_updatel(CFG_CSB_256x8, CFG_CSB_256x8 | CFG_PDWN256x8,
			       base + LCD_SPU_SRAM_PARA1);

		for (i = 0; i < 256; i++) {
			writel_relaxed(drm_color_lut_extract(lut[i].red, 8),
				       base + LCD_SPU_SRAM_WRDAT);
			writel_relaxed(i | SRAM_WRITE | SRAM_GAMMA_YR,
				       base + LCD_SPU_SRAM_CTRL);
			readl_relaxed(base + LCD_SPU_HWC_OVSA_HPXL_VLN);
			writel_relaxed(drm_color_lut_extract(lut[i].green, 8),
				       base + LCD_SPU_SRAM_WRDAT);
			writel_relaxed(i | SRAM_WRITE | SRAM_GAMMA_UG,
				       base + LCD_SPU_SRAM_CTRL);
			readl_relaxed(base + LCD_SPU_HWC_OVSA_HPXL_VLN);
			writel_relaxed(drm_color_lut_extract(lut[i].blue, 8),
				       base + LCD_SPU_SRAM_WRDAT);
			writel_relaxed(i | SRAM_WRITE | SRAM_GAMMA_VB,
				       base + LCD_SPU_SRAM_CTRL);
			readl_relaxed(base + LCD_SPU_HWC_OVSA_HPXL_VLN);
		}
		armada_updatel(CFG_GAMMA_ENA, CFG_GAMMA_ENA,
			       base + LCD_SPU_DMA_CTRL0);
	} else {
		armada_updatel(0, CFG_GAMMA_ENA, base + LCD_SPU_DMA_CTRL0);
		armada_updatel(CFG_PDWN256x8, CFG_CSB_256x8 | CFG_PDWN256x8,
			       base + LCD_SPU_SRAM_PARA1);
	}
}

static enum drm_mode_status armada_drm_crtc_mode_valid(struct drm_crtc *crtc,
	const struct drm_display_mode *mode)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);

	if (mode->vscan > 1)
		return MODE_NO_VSCAN;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (mode->flags & DRM_MODE_FLAG_HSKEW)
		return MODE_H_ILLEGAL;

	/* We can't do interlaced modes if we don't have the SPU_ADV_REG */
	if (!dcrtc->variant->has_spu_adv_reg &&
	    mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	if (mode->flags & (DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_PIXMUX |
			   DRM_MODE_FLAG_CLKDIV2))
		return MODE_BAD;

	return MODE_OK;
}

/* The mode_config.mutex will be held for this call */
static bool armada_drm_crtc_mode_fixup(struct drm_crtc *crtc,
	const struct drm_display_mode *mode, struct drm_display_mode *adj)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	int ret;

	/*
	 * Set CRTC modesetting parameters for the adjusted mode.  This is
	 * applied after the connectors, bridges, and encoders have fixed up
	 * this mode, as described above drm_atomic_helper_check_modeset().
	 */
	drm_mode_set_crtcinfo(adj, CRTC_INTERLACE_HALVE_V);

	/*
	 * Validate the adjusted mode in case an encoder/bridge has set
	 * something we don't support.
	 */
	if (armada_drm_crtc_mode_valid(crtc, adj) != MODE_OK)
		return false;

	/* Check whether the display mode is possible */
	ret = dcrtc->variant->compute_clock(dcrtc, adj, NULL);
	if (ret)
		return false;

	return true;
}

/* These are locked by dev->vbl_lock */
static void armada_drm_crtc_disable_irq(struct armada_crtc *dcrtc, u32 mask)
{
	if (dcrtc->irq_ena & mask) {
		dcrtc->irq_ena &= ~mask;
		writel(dcrtc->irq_ena, dcrtc->base + LCD_SPU_IRQ_ENA);
	}
}

static void armada_drm_crtc_enable_irq(struct armada_crtc *dcrtc, u32 mask)
{
	if ((dcrtc->irq_ena & mask) != mask) {
		dcrtc->irq_ena |= mask;
		writel(dcrtc->irq_ena, dcrtc->base + LCD_SPU_IRQ_ENA);
		if (readl_relaxed(dcrtc->base + LCD_SPU_IRQ_ISR) & mask)
			writel(0, dcrtc->base + LCD_SPU_IRQ_ISR);
	}
}

static void armada_drm_crtc_irq(struct armada_crtc *dcrtc, u32 stat)
{
	struct drm_pending_vblank_event *event;
	void __iomem *base = dcrtc->base;

	if (stat & DMA_FF_UNDERFLOW)
		DRM_ERROR("video underflow on crtc %u\n", dcrtc->num);
	if (stat & GRA_FF_UNDERFLOW)
		DRM_ERROR("graphics underflow on crtc %u\n", dcrtc->num);

	if (stat & VSYNC_IRQ)
		drm_crtc_handle_vblank(&dcrtc->crtc);

	spin_lock(&dcrtc->irq_lock);
	if (stat & GRA_FRAME_IRQ && dcrtc->interlaced) {
		int i = stat & GRA_FRAME_IRQ0 ? 0 : 1;
		uint32_t val;

		writel_relaxed(dcrtc->v[i].spu_v_porch, base + LCD_SPU_V_PORCH);
		writel_relaxed(dcrtc->v[i].spu_v_h_total,
			       base + LCD_SPUT_V_H_TOTAL);

		val = readl_relaxed(base + LCD_SPU_ADV_REG);
		val &= ~(ADV_VSYNC_L_OFF | ADV_VSYNC_H_OFF | ADV_VSYNCOFFEN);
		val |= dcrtc->v[i].spu_adv_reg;
		writel_relaxed(val, base + LCD_SPU_ADV_REG);
	}

	if (stat & dcrtc->irq_ena & DUMB_FRAMEDONE) {
		if (dcrtc->update_pending) {
			armada_drm_crtc_update_regs(dcrtc, dcrtc->regs);
			dcrtc->update_pending = false;
		}
		if (dcrtc->cursor_update) {
			writel_relaxed(dcrtc->cursor_hw_pos,
				       base + LCD_SPU_HWC_OVSA_HPXL_VLN);
			writel_relaxed(dcrtc->cursor_hw_sz,
				       base + LCD_SPU_HWC_HPXL_VLN);
			armada_updatel(CFG_HWC_ENA,
				       CFG_HWC_ENA | CFG_HWC_1BITMOD |
				       CFG_HWC_1BITENA,
				       base + LCD_SPU_DMA_CTRL0);
			dcrtc->cursor_update = false;
		}
		armada_drm_crtc_disable_irq(dcrtc, DUMB_FRAMEDONE_ENA);
	}
	spin_unlock(&dcrtc->irq_lock);

	if (stat & VSYNC_IRQ && !dcrtc->update_pending) {
		event = xchg(&dcrtc->event, NULL);
		if (event) {
			spin_lock(&dcrtc->crtc.dev->event_lock);
			drm_crtc_send_vblank_event(&dcrtc->crtc, event);
			spin_unlock(&dcrtc->crtc.dev->event_lock);
			drm_crtc_vblank_put(&dcrtc->crtc);
		}
	}
}

static irqreturn_t armada_drm_irq(int irq, void *arg)
{
	struct armada_crtc *dcrtc = arg;
	u32 v, stat = readl_relaxed(dcrtc->base + LCD_SPU_IRQ_ISR);

	/*
	 * Reading the ISR appears to clear bits provided CLEAN_SPU_IRQ_ISR
	 * is set.  Writing has some other effect to acknowledge the IRQ -
	 * without this, we only get a single IRQ.
	 */
	writel_relaxed(0, dcrtc->base + LCD_SPU_IRQ_ISR);

	trace_armada_drm_irq(&dcrtc->crtc, stat);

	/* Mask out those interrupts we haven't enabled */
	v = stat & dcrtc->irq_ena;

	if (v & (VSYNC_IRQ|GRA_FRAME_IRQ|DUMB_FRAMEDONE)) {
		armada_drm_crtc_irq(dcrtc, stat);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/* The mode_config.mutex will be held for this call */
static void armada_drm_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_display_mode *adj = &crtc->state->adjusted_mode;
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct armada_regs regs[17];
	uint32_t lm, rm, tm, bm, val, sclk;
	unsigned long flags;
	unsigned i;
	bool interlaced = !!(adj->flags & DRM_MODE_FLAG_INTERLACE);

	i = 0;
	rm = adj->crtc_hsync_start - adj->crtc_hdisplay;
	lm = adj->crtc_htotal - adj->crtc_hsync_end;
	bm = adj->crtc_vsync_start - adj->crtc_vdisplay;
	tm = adj->crtc_vtotal - adj->crtc_vsync_end;

	DRM_DEBUG_KMS("[CRTC:%d:%s] mode " DRM_MODE_FMT "\n",
		      crtc->base.id, crtc->name, DRM_MODE_ARG(adj));
	DRM_DEBUG_KMS("lm %d rm %d tm %d bm %d\n", lm, rm, tm, bm);

	/* Now compute the divider for real */
	dcrtc->variant->compute_clock(dcrtc, adj, &sclk);

	armada_reg_queue_set(regs, i, sclk, LCD_CFG_SCLK_DIV);

	spin_lock_irqsave(&dcrtc->irq_lock, flags);

	dcrtc->interlaced = interlaced;
	/* Even interlaced/progressive frame */
	dcrtc->v[1].spu_v_h_total = adj->crtc_vtotal << 16 |
				    adj->crtc_htotal;
	dcrtc->v[1].spu_v_porch = tm << 16 | bm;
	val = adj->crtc_hsync_start;
	dcrtc->v[1].spu_adv_reg = val << 20 | val | ADV_VSYNCOFFEN;

	if (interlaced) {
		/* Odd interlaced frame */
		val -= adj->crtc_htotal / 2;
		dcrtc->v[0].spu_adv_reg = val << 20 | val | ADV_VSYNCOFFEN;
		dcrtc->v[0].spu_v_h_total = dcrtc->v[1].spu_v_h_total +
						(1 << 16);
		dcrtc->v[0].spu_v_porch = dcrtc->v[1].spu_v_porch + 1;
	} else {
		dcrtc->v[0] = dcrtc->v[1];
	}

	val = adj->crtc_vdisplay << 16 | adj->crtc_hdisplay;

	armada_reg_queue_set(regs, i, val, LCD_SPU_V_H_ACTIVE);
	armada_reg_queue_set(regs, i, (lm << 16) | rm, LCD_SPU_H_PORCH);
	armada_reg_queue_set(regs, i, dcrtc->v[0].spu_v_porch, LCD_SPU_V_PORCH);
	armada_reg_queue_set(regs, i, dcrtc->v[0].spu_v_h_total,
			   LCD_SPUT_V_H_TOTAL);

	if (dcrtc->variant->has_spu_adv_reg)
		armada_reg_queue_mod(regs, i, dcrtc->v[0].spu_adv_reg,
				     ADV_VSYNC_L_OFF | ADV_VSYNC_H_OFF |
				     ADV_VSYNCOFFEN, LCD_SPU_ADV_REG);

	val = adj->flags & DRM_MODE_FLAG_NVSYNC ? CFG_VSYNC_INV : 0;
	armada_reg_queue_mod(regs, i, val, CFG_VSYNC_INV, LCD_SPU_DMA_CTRL1);

	/*
	 * The documentation doesn't indicate what the normal state of
	 * the sync signals are.  Sebastian Hesselbart kindly probed
	 * these signals on his board to determine their state.
	 *
	 * The non-inverted state of the sync signals is active high.
	 * Setting these bits makes the appropriate signal active low.
	 */
	val = 0;
	if (adj->flags & DRM_MODE_FLAG_NCSYNC)
		val |= CFG_INV_CSYNC;
	if (adj->flags & DRM_MODE_FLAG_NHSYNC)
		val |= CFG_INV_HSYNC;
	if (adj->flags & DRM_MODE_FLAG_NVSYNC)
		val |= CFG_INV_VSYNC;
	armada_reg_queue_mod(regs, i, val, CFG_INV_CSYNC | CFG_INV_HSYNC |
			     CFG_INV_VSYNC, LCD_SPU_DUMB_CTRL);
	armada_reg_queue_end(regs, i);

	armada_drm_crtc_update_regs(dcrtc, regs);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

static int armada_drm_crtc_atomic_check(struct drm_crtc *crtc,
					struct drm_crtc_state *state)
{
	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	if (state->gamma_lut && drm_color_lut_size(state->gamma_lut) != 256)
		return -EINVAL;

	if (state->color_mgmt_changed)
		state->planes_changed = true;

	return 0;
}

static void armada_drm_crtc_atomic_begin(struct drm_crtc *crtc,
					 struct drm_crtc_state *old_crtc_state)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	if (crtc->state->color_mgmt_changed)
		armada_drm_update_gamma(crtc);

	dcrtc->regs_idx = 0;
	dcrtc->regs = dcrtc->atomic_regs;
}

static void armada_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					 struct drm_crtc_state *old_crtc_state)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	armada_reg_queue_end(dcrtc->regs, dcrtc->regs_idx);

	/*
	 * If we aren't doing a full modeset, then we need to queue
	 * the event here.
	 */
	if (!drm_atomic_crtc_needs_modeset(crtc->state)) {
		dcrtc->update_pending = true;
		armada_drm_crtc_queue_state_event(crtc);
		spin_lock_irq(&dcrtc->irq_lock);
		armada_drm_crtc_enable_irq(dcrtc, DUMB_FRAMEDONE_ENA);
		spin_unlock_irq(&dcrtc->irq_lock);
	} else {
		spin_lock_irq(&dcrtc->irq_lock);
		armada_drm_crtc_update_regs(dcrtc, dcrtc->regs);
		spin_unlock_irq(&dcrtc->irq_lock);
	}
}

static void armada_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					   struct drm_crtc_state *old_state)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct drm_pending_vblank_event *event;

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	if (old_state->adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		drm_crtc_vblank_put(crtc);

	drm_crtc_vblank_off(crtc);
	armada_drm_crtc_update(dcrtc, false);

	if (!crtc->state->active) {
		/*
		 * This modeset will be leaving the CRTC disabled, so
		 * call the backend to disable upstream clocks etc.
		 */
		if (dcrtc->variant->disable)
			dcrtc->variant->disable(dcrtc);

		/*
		 * We will not receive any further vblank events.
		 * Send the flip_done event manually.
		 */
		event = crtc->state->event;
		crtc->state->event = NULL;
		if (event) {
			spin_lock_irq(&crtc->dev->event_lock);
			drm_crtc_send_vblank_event(crtc, event);
			spin_unlock_irq(&crtc->dev->event_lock);
		}
	}
}

static void armada_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_state)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	if (!old_state->active) {
		/*
		 * This modeset is enabling the CRTC after it having
		 * been disabled.  Reverse the call to ->disable in
		 * the atomic_disable().
		 */
		if (dcrtc->variant->enable)
			dcrtc->variant->enable(dcrtc, &crtc->state->adjusted_mode);
	}
	armada_drm_crtc_update(dcrtc, true);
	drm_crtc_vblank_on(crtc);

	if (crtc->state->adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		WARN_ON(drm_crtc_vblank_get(crtc));

	armada_drm_crtc_queue_state_event(crtc);
}

static const struct drm_crtc_helper_funcs armada_crtc_helper_funcs = {
	.mode_valid	= armada_drm_crtc_mode_valid,
	.mode_fixup	= armada_drm_crtc_mode_fixup,
	.mode_set_nofb	= armada_drm_crtc_mode_set_nofb,
	.atomic_check	= armada_drm_crtc_atomic_check,
	.atomic_begin	= armada_drm_crtc_atomic_begin,
	.atomic_flush	= armada_drm_crtc_atomic_flush,
	.atomic_disable	= armada_drm_crtc_atomic_disable,
	.atomic_enable	= armada_drm_crtc_atomic_enable,
};

static void armada_load_cursor_argb(void __iomem *base, uint32_t *pix,
	unsigned stride, unsigned width, unsigned height)
{
	uint32_t addr;
	unsigned y;

	addr = SRAM_HWC32_RAM1;
	for (y = 0; y < height; y++) {
		uint32_t *p = &pix[y * stride];
		unsigned x;

		for (x = 0; x < width; x++, p++) {
			uint32_t val = *p;

			/*
			 * In "ARGB888" (HWC32) mode, writing to the SRAM
			 * requires these bits to contain:
			 * 31:24 = alpha 23:16 = blue 15:8 = green 7:0 = red
			 * So, it's actually ABGR8888.  This is independent
			 * of the SWAPRB bits in DMA control register 0.
			 */
			val = (val & 0xff00ff00) |
			      (val & 0x000000ff) << 16 |
			      (val & 0x00ff0000) >> 16;

			writel_relaxed(val,
				       base + LCD_SPU_SRAM_WRDAT);
			writel_relaxed(addr | SRAM_WRITE,
				       base + LCD_SPU_SRAM_CTRL);
			readl_relaxed(base + LCD_SPU_HWC_OVSA_HPXL_VLN);
			addr += 1;
			if ((addr & 0x00ff) == 0)
				addr += 0xf00;
			if ((addr & 0x30ff) == 0)
				addr = SRAM_HWC32_RAM2;
		}
	}
}

static void armada_drm_crtc_cursor_tran(void __iomem *base)
{
	unsigned addr;

	for (addr = 0; addr < 256; addr++) {
		/* write the default value */
		writel_relaxed(0x55555555, base + LCD_SPU_SRAM_WRDAT);
		writel_relaxed(addr | SRAM_WRITE | SRAM_HWC32_TRAN,
			       base + LCD_SPU_SRAM_CTRL);
	}
}

static int armada_drm_crtc_cursor_update(struct armada_crtc *dcrtc, bool reload)
{
	uint32_t xoff, xscr, w = dcrtc->cursor_w, s;
	uint32_t yoff, yscr, h = dcrtc->cursor_h;
	uint32_t para1;

	/*
	 * Calculate the visible width and height of the cursor,
	 * screen position, and the position in the cursor bitmap.
	 */
	if (dcrtc->cursor_x < 0) {
		xoff = -dcrtc->cursor_x;
		xscr = 0;
		w -= min(xoff, w);
	} else if (dcrtc->cursor_x + w > dcrtc->crtc.mode.hdisplay) {
		xoff = 0;
		xscr = dcrtc->cursor_x;
		w = max_t(int, dcrtc->crtc.mode.hdisplay - dcrtc->cursor_x, 0);
	} else {
		xoff = 0;
		xscr = dcrtc->cursor_x;
	}

	if (dcrtc->cursor_y < 0) {
		yoff = -dcrtc->cursor_y;
		yscr = 0;
		h -= min(yoff, h);
	} else if (dcrtc->cursor_y + h > dcrtc->crtc.mode.vdisplay) {
		yoff = 0;
		yscr = dcrtc->cursor_y;
		h = max_t(int, dcrtc->crtc.mode.vdisplay - dcrtc->cursor_y, 0);
	} else {
		yoff = 0;
		yscr = dcrtc->cursor_y;
	}

	/* On interlaced modes, the vertical cursor size must be halved */
	s = dcrtc->cursor_w;
	if (dcrtc->interlaced) {
		s *= 2;
		yscr /= 2;
		h /= 2;
	}

	if (!dcrtc->cursor_obj || !h || !w) {
		spin_lock_irq(&dcrtc->irq_lock);
		dcrtc->cursor_update = false;
		armada_updatel(0, CFG_HWC_ENA, dcrtc->base + LCD_SPU_DMA_CTRL0);
		spin_unlock_irq(&dcrtc->irq_lock);
		return 0;
	}

	spin_lock_irq(&dcrtc->irq_lock);
	para1 = readl_relaxed(dcrtc->base + LCD_SPU_SRAM_PARA1);
	armada_updatel(CFG_CSB_256x32, CFG_CSB_256x32 | CFG_PDWN256x32,
		       dcrtc->base + LCD_SPU_SRAM_PARA1);
	spin_unlock_irq(&dcrtc->irq_lock);

	/*
	 * Initialize the transparency if the SRAM was powered down.
	 * We must also reload the cursor data as well.
	 */
	if (!(para1 & CFG_CSB_256x32)) {
		armada_drm_crtc_cursor_tran(dcrtc->base);
		reload = true;
	}

	if (dcrtc->cursor_hw_sz != (h << 16 | w)) {
		spin_lock_irq(&dcrtc->irq_lock);
		dcrtc->cursor_update = false;
		armada_updatel(0, CFG_HWC_ENA, dcrtc->base + LCD_SPU_DMA_CTRL0);
		spin_unlock_irq(&dcrtc->irq_lock);
		reload = true;
	}
	if (reload) {
		struct armada_gem_object *obj = dcrtc->cursor_obj;
		uint32_t *pix;
		/* Set the top-left corner of the cursor image */
		pix = obj->addr;
		pix += yoff * s + xoff;
		armada_load_cursor_argb(dcrtc->base, pix, s, w, h);
	}

	/* Reload the cursor position, size and enable in the IRQ handler */
	spin_lock_irq(&dcrtc->irq_lock);
	dcrtc->cursor_hw_pos = yscr << 16 | xscr;
	dcrtc->cursor_hw_sz = h << 16 | w;
	dcrtc->cursor_update = true;
	armada_drm_crtc_enable_irq(dcrtc, DUMB_FRAMEDONE_ENA);
	spin_unlock_irq(&dcrtc->irq_lock);

	return 0;
}

static void cursor_update(void *data)
{
	armada_drm_crtc_cursor_update(data, true);
}

static int armada_drm_crtc_cursor_set(struct drm_crtc *crtc,
	struct drm_file *file, uint32_t handle, uint32_t w, uint32_t h)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct armada_gem_object *obj = NULL;
	int ret;

	/* If no cursor support, replicate drm's return value */
	if (!dcrtc->variant->has_spu_adv_reg)
		return -ENXIO;

	if (handle && w > 0 && h > 0) {
		/* maximum size is 64x32 or 32x64 */
		if (w > 64 || h > 64 || (w > 32 && h > 32))
			return -ENOMEM;

		obj = armada_gem_object_lookup(file, handle);
		if (!obj)
			return -ENOENT;

		/* Must be a kernel-mapped object */
		if (!obj->addr) {
			drm_gem_object_put_unlocked(&obj->obj);
			return -EINVAL;
		}

		if (obj->obj.size < w * h * 4) {
			DRM_ERROR("buffer is too small\n");
			drm_gem_object_put_unlocked(&obj->obj);
			return -ENOMEM;
		}
	}

	if (dcrtc->cursor_obj) {
		dcrtc->cursor_obj->update = NULL;
		dcrtc->cursor_obj->update_data = NULL;
		drm_gem_object_put_unlocked(&dcrtc->cursor_obj->obj);
	}
	dcrtc->cursor_obj = obj;
	dcrtc->cursor_w = w;
	dcrtc->cursor_h = h;
	ret = armada_drm_crtc_cursor_update(dcrtc, true);
	if (obj) {
		obj->update_data = dcrtc;
		obj->update = cursor_update;
	}

	return ret;
}

static int armada_drm_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	int ret;

	/* If no cursor support, replicate drm's return value */
	if (!dcrtc->variant->has_spu_adv_reg)
		return -EFAULT;

	dcrtc->cursor_x = x;
	dcrtc->cursor_y = y;
	ret = armada_drm_crtc_cursor_update(dcrtc, false);

	return ret;
}

static void armada_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct armada_private *priv = crtc->dev->dev_private;

	if (dcrtc->cursor_obj)
		drm_gem_object_put_unlocked(&dcrtc->cursor_obj->obj);

	priv->dcrtc[dcrtc->num] = NULL;
	drm_crtc_cleanup(&dcrtc->crtc);

	if (dcrtc->variant->disable)
		dcrtc->variant->disable(dcrtc);

	writel_relaxed(0, dcrtc->base + LCD_SPU_IRQ_ENA);

	of_node_put(dcrtc->crtc.port);

	kfree(dcrtc);
}

static int armada_drm_crtc_late_register(struct drm_crtc *crtc)
{
	if (IS_ENABLED(CONFIG_DEBUG_FS))
		armada_drm_crtc_debugfs_init(drm_to_armada_crtc(crtc));

	return 0;
}

/* These are called under the vbl_lock. */
static int armada_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_crtc_enable_irq(dcrtc, VSYNC_IRQ_ENA);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
	return 0;
}

static void armada_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_crtc_disable_irq(dcrtc, VSYNC_IRQ_ENA);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

static const struct drm_crtc_funcs armada_crtc_funcs = {
	.reset		= drm_atomic_helper_crtc_reset,
	.cursor_set	= armada_drm_crtc_cursor_set,
	.cursor_move	= armada_drm_crtc_cursor_move,
	.destroy	= armada_drm_crtc_destroy,
	.gamma_set	= drm_atomic_helper_legacy_gamma_set,
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.late_register	= armada_drm_crtc_late_register,
	.enable_vblank	= armada_drm_crtc_enable_vblank,
	.disable_vblank	= armada_drm_crtc_disable_vblank,
};

int armada_crtc_select_clock(struct armada_crtc *dcrtc,
			     struct armada_clk_result *res,
			     const struct armada_clocking_params *params,
			     struct clk *clks[], size_t num_clks,
			     unsigned long desired_khz)
{
	unsigned long desired_hz = desired_khz * 1000;
	unsigned long desired_clk_hz;	// requested clk input
	unsigned long real_clk_hz;	// actual clk input
	unsigned long real_hz;		// actual pixel clk
	unsigned long permillage;
	struct clk *clk;
	u32 div;
	int i;

	DRM_DEBUG_KMS("[CRTC:%u:%s] desired clock=%luHz\n",
		      dcrtc->crtc.base.id, dcrtc->crtc.name, desired_hz);

	for (i = 0; i < num_clks; i++) {
		clk = clks[i];
		if (!clk)
			continue;

		if (params->settable & BIT(i)) {
			real_clk_hz = clk_round_rate(clk, desired_hz);
			desired_clk_hz = desired_hz;
		} else {
			real_clk_hz = clk_get_rate(clk);
			desired_clk_hz = real_clk_hz;
		}

		/* If the clock can do exactly the desired rate, we're done */
		if (real_clk_hz == desired_hz) {
			real_hz = real_clk_hz;
			div = 1;
			goto found;
		}

		/* Calculate the divider - if invalid, we can't do this rate */
		div = DIV_ROUND_CLOSEST(real_clk_hz, desired_hz);
		if (div == 0 || div > params->div_max)
			continue;

		/* Calculate the actual rate - HDMI requires -0.6%..+0.5% */
		real_hz = DIV_ROUND_CLOSEST(real_clk_hz, div);

		DRM_DEBUG_KMS("[CRTC:%u:%s] clk=%u %luHz div=%u real=%luHz\n",
			dcrtc->crtc.base.id, dcrtc->crtc.name,
			i, real_clk_hz, div, real_hz);

		/* Avoid repeated division */
		if (real_hz < desired_hz) {
			permillage = real_hz / desired_khz;
			if (permillage < params->permillage_min)
				continue;
		} else {
			permillage = DIV_ROUND_UP(real_hz, desired_khz);
			if (permillage > params->permillage_max)
				continue;
		}
		goto found;
	}

	return -ERANGE;

found:
	DRM_DEBUG_KMS("[CRTC:%u:%s] selected clk=%u %luHz div=%u real=%luHz\n",
		dcrtc->crtc.base.id, dcrtc->crtc.name,
		i, real_clk_hz, div, real_hz);

	res->desired_clk_hz = desired_clk_hz;
	res->clk = clk;
	res->div = div;

	return i;
}

static int armada_drm_crtc_create(struct drm_device *drm, struct device *dev,
	struct resource *res, int irq, const struct armada_variant *variant,
	struct device_node *port)
{
	struct armada_private *priv = drm->dev_private;
	struct armada_crtc *dcrtc;
	struct drm_plane *primary;
	void __iomem *base;
	int ret;

	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	dcrtc = kzalloc(sizeof(*dcrtc), GFP_KERNEL);
	if (!dcrtc) {
		DRM_ERROR("failed to allocate Armada crtc\n");
		return -ENOMEM;
	}

	if (dev != drm->dev)
		dev_set_drvdata(dev, dcrtc);

	dcrtc->variant = variant;
	dcrtc->base = base;
	dcrtc->num = drm->mode_config.num_crtc;
	dcrtc->cfg_dumb_ctrl = DUMB24_RGB888_0;
	dcrtc->spu_iopad_ctrl = CFG_VSCALE_LN_EN | CFG_IOPAD_DUMB24;
	spin_lock_init(&dcrtc->irq_lock);
	dcrtc->irq_ena = CLEAN_SPU_IRQ_ISR;

	/* Initialize some registers which we don't otherwise set */
	writel_relaxed(0x00000001, dcrtc->base + LCD_CFG_SCLK_DIV);
	writel_relaxed(0x00000000, dcrtc->base + LCD_SPU_BLANKCOLOR);
	writel_relaxed(dcrtc->spu_iopad_ctrl,
		       dcrtc->base + LCD_SPU_IOPAD_CONTROL);
	writel_relaxed(0x00000000, dcrtc->base + LCD_SPU_SRAM_PARA0);
	writel_relaxed(CFG_PDWN256x32 | CFG_PDWN256x24 | CFG_PDWN256x8 |
		       CFG_PDWN32x32 | CFG_PDWN16x66 | CFG_PDWN32x66 |
		       CFG_PDWN64x66, dcrtc->base + LCD_SPU_SRAM_PARA1);
	writel_relaxed(0x2032ff81, dcrtc->base + LCD_SPU_DMA_CTRL1);
	writel_relaxed(dcrtc->irq_ena, dcrtc->base + LCD_SPU_IRQ_ENA);
	readl_relaxed(dcrtc->base + LCD_SPU_IRQ_ISR);
	writel_relaxed(0, dcrtc->base + LCD_SPU_IRQ_ISR);

	ret = devm_request_irq(dev, irq, armada_drm_irq, 0, "armada_drm_crtc",
			       dcrtc);
	if (ret < 0)
		goto err_crtc;

	if (dcrtc->variant->init) {
		ret = dcrtc->variant->init(dcrtc, dev);
		if (ret)
			goto err_crtc;
	}

	/* Ensure AXI pipeline is enabled */
	armada_updatel(CFG_ARBFAST_ENA, 0, dcrtc->base + LCD_SPU_DMA_CTRL0);

	priv->dcrtc[dcrtc->num] = dcrtc;

	dcrtc->crtc.port = port;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (!primary) {
		ret = -ENOMEM;
		goto err_crtc;
	}

	ret = armada_drm_primary_plane_init(drm, primary);
	if (ret) {
		kfree(primary);
		goto err_crtc;
	}

	ret = drm_crtc_init_with_planes(drm, &dcrtc->crtc, primary, NULL,
					&armada_crtc_funcs, NULL);
	if (ret)
		goto err_crtc_init;

	drm_crtc_helper_add(&dcrtc->crtc, &armada_crtc_helper_funcs);

	ret = drm_mode_crtc_set_gamma_size(&dcrtc->crtc, 256);
	if (ret)
		return ret;

	drm_crtc_enable_color_mgmt(&dcrtc->crtc, 0, false, 256);

	return armada_overlay_plane_create(drm, 1 << dcrtc->num);

err_crtc_init:
	primary->funcs->destroy(primary);
err_crtc:
	kfree(dcrtc);

	return ret;
}

static int
armada_lcd_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int irq = platform_get_irq(pdev, 0);
	const struct armada_variant *variant;
	struct device_node *port = NULL;

	if (irq < 0)
		return irq;

	if (!dev->of_node) {
		const struct platform_device_id *id;

		id = platform_get_device_id(pdev);
		if (!id)
			return -ENXIO;

		variant = (const struct armada_variant *)id->driver_data;
	} else {
		const struct of_device_id *match;
		struct device_node *np, *parent = dev->of_node;

		match = of_match_device(dev->driver->of_match_table, dev);
		if (!match)
			return -ENXIO;

		np = of_get_child_by_name(parent, "ports");
		if (np)
			parent = np;
		port = of_get_child_by_name(parent, "port");
		of_node_put(np);
		if (!port) {
			dev_err(dev, "no port node found in %pOF\n", parent);
			return -ENXIO;
		}

		variant = match->data;
	}

	return armada_drm_crtc_create(drm, dev, res, irq, variant, port);
}

static void
armada_lcd_unbind(struct device *dev, struct device *master, void *data)
{
	struct armada_crtc *dcrtc = dev_get_drvdata(dev);

	armada_drm_crtc_destroy(&dcrtc->crtc);
}

static const struct component_ops armada_lcd_ops = {
	.bind = armada_lcd_bind,
	.unbind = armada_lcd_unbind,
};

static int armada_lcd_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &armada_lcd_ops);
}

static int armada_lcd_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &armada_lcd_ops);
	return 0;
}

static const struct of_device_id armada_lcd_of_match[] = {
	{
		.compatible	= "marvell,dove-lcd",
		.data		= &armada510_ops,
	},
	{}
};
MODULE_DEVICE_TABLE(of, armada_lcd_of_match);

static const struct platform_device_id armada_lcd_platform_ids[] = {
	{
		.name		= "armada-lcd",
		.driver_data	= (unsigned long)&armada510_ops,
	}, {
		.name		= "armada-510-lcd",
		.driver_data	= (unsigned long)&armada510_ops,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, armada_lcd_platform_ids);

struct platform_driver armada_lcd_platform_driver = {
	.probe	= armada_lcd_probe,
	.remove	= armada_lcd_remove,
	.driver = {
		.name	= "armada-lcd",
		.owner	=  THIS_MODULE,
		.of_match_table = armada_lcd_of_match,
	},
	.id_table = armada_lcd_platform_ids,
};
