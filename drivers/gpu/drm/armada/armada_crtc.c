/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>
#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include "armada_trace.h"

enum csc_mode {
	CSC_AUTO = 0,
	CSC_YUV_CCIR601 = 1,
	CSC_YUV_CCIR709 = 2,
	CSC_RGB_COMPUTER = 1,
	CSC_RGB_STUDIO = 2,
};

static const uint32_t armada_primary_formats[] = {
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

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

#define dpms_blanked(dpms)	((dpms) != DRM_MODE_DPMS_ON)

static void armada_drm_crtc_update(struct armada_crtc *dcrtc)
{
	uint32_t dumb_ctrl;

	dumb_ctrl = dcrtc->cfg_dumb_ctrl;

	if (!dpms_blanked(dcrtc->dpms))
		dumb_ctrl |= CFG_DUMB_ENA;

	/*
	 * When the dumb interface isn't in DUMB24_RGB888_0 mode, it might
	 * be using SPI or GPIO.  If we set this to DUMB_BLANK, we will
	 * force LCD_D[23:0] to output blank color, overriding the GPIO or
	 * SPI usage.  So leave it as-is unless in DUMB24_RGB888_0 mode.
	 */
	if (dpms_blanked(dcrtc->dpms) &&
	    (dumb_ctrl & DUMB_MASK) == DUMB24_RGB888_0) {
		dumb_ctrl &= ~DUMB_MASK;
		dumb_ctrl |= DUMB_BLANK;
	}

	/*
	 * The documentation doesn't indicate what the normal state of
	 * the sync signals are.  Sebastian Hesselbart kindly probed
	 * these signals on his board to determine their state.
	 *
	 * The non-inverted state of the sync signals is active high.
	 * Setting these bits makes the appropriate signal active low.
	 */
	if (dcrtc->crtc.mode.flags & DRM_MODE_FLAG_NCSYNC)
		dumb_ctrl |= CFG_INV_CSYNC;
	if (dcrtc->crtc.mode.flags & DRM_MODE_FLAG_NHSYNC)
		dumb_ctrl |= CFG_INV_HSYNC;
	if (dcrtc->crtc.mode.flags & DRM_MODE_FLAG_NVSYNC)
		dumb_ctrl |= CFG_INV_VSYNC;

	if (dcrtc->dumb_ctrl != dumb_ctrl) {
		dcrtc->dumb_ctrl = dumb_ctrl;
		writel_relaxed(dumb_ctrl, dcrtc->base + LCD_SPU_DUMB_CTRL);
	}
}

void armada_drm_plane_calc_addrs(u32 *addrs, struct drm_framebuffer *fb,
	int x, int y)
{
	const struct drm_format_info *format = fb->format;
	unsigned int num_planes = format->num_planes;
	u32 addr = drm_fb_obj(fb)->dev_addr;
	int i;

	if (num_planes > 3)
		num_planes = 3;

	addrs[0] = addr + fb->offsets[0] + y * fb->pitches[0] +
		   x * format->cpp[0];

	y /= format->vsub;
	x /= format->hsub;

	for (i = 1; i < num_planes; i++)
		addrs[i] = addr + fb->offsets[i] + y * fb->pitches[i] +
			     x * format->cpp[i];
	for (; i < 3; i++)
		addrs[i] = 0;
}

static unsigned armada_drm_crtc_calc_fb(struct drm_framebuffer *fb,
	int x, int y, struct armada_regs *regs, bool interlaced)
{
	unsigned pitch = fb->pitches[0];
	u32 addrs[3], addr_odd, addr_even;
	unsigned i = 0;

	DRM_DEBUG_DRIVER("pitch %u x %d y %d bpp %d\n",
		pitch, x, y, fb->format->cpp[0] * 8);

	armada_drm_plane_calc_addrs(addrs, fb, x, y);

	addr_odd = addr_even = addrs[0];

	if (interlaced) {
		addr_even += pitch;
		pitch *= 2;
	}

	/* write offset, base, and pitch */
	armada_reg_queue_set(regs, i, addr_odd, LCD_CFG_GRA_START_ADDR0);
	armada_reg_queue_set(regs, i, addr_even, LCD_CFG_GRA_START_ADDR1);
	armada_reg_queue_mod(regs, i, pitch, 0xffff, LCD_CFG_GRA_PITCH);

	return i;
}

static void armada_drm_plane_work_call(struct armada_crtc *dcrtc,
	struct armada_plane_work *work,
	void (*fn)(struct armada_crtc *, struct armada_plane_work *))
{
	struct armada_plane *dplane = drm_to_armada_plane(work->plane);
	struct drm_pending_vblank_event *event;
	struct drm_framebuffer *fb;

	if (fn)
		fn(dcrtc, work);
	drm_crtc_vblank_put(&dcrtc->crtc);

	event = work->event;
	fb = work->old_fb;
	if (event || fb) {
		struct drm_device *dev = dcrtc->crtc.dev;
		unsigned long flags;

		spin_lock_irqsave(&dev->event_lock, flags);
		if (event)
			drm_crtc_send_vblank_event(&dcrtc->crtc, event);
		if (fb)
			__armada_drm_queue_unref_work(dev, fb);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	if (work->need_kfree)
		kfree(work);

	wake_up(&dplane->frame_wait);
}

static void armada_drm_plane_work_run(struct armada_crtc *dcrtc,
	struct drm_plane *plane)
{
	struct armada_plane *dplane = drm_to_armada_plane(plane);
	struct armada_plane_work *work = xchg(&dplane->work, NULL);

	/* Handle any pending frame work. */
	if (work)
		armada_drm_plane_work_call(dcrtc, work, work->fn);
}

int armada_drm_plane_work_queue(struct armada_crtc *dcrtc,
	struct armada_plane_work *work)
{
	struct armada_plane *plane = drm_to_armada_plane(work->plane);
	int ret;

	ret = drm_crtc_vblank_get(&dcrtc->crtc);
	if (ret)
		return ret;

	ret = cmpxchg(&plane->work, NULL, work) ? -EBUSY : 0;
	if (ret)
		drm_crtc_vblank_put(&dcrtc->crtc);

	return ret;
}

int armada_drm_plane_work_wait(struct armada_plane *plane, long timeout)
{
	return wait_event_timeout(plane->frame_wait, !plane->work, timeout);
}

void armada_drm_plane_work_cancel(struct armada_crtc *dcrtc,
	struct armada_plane *dplane)
{
	struct armada_plane_work *work = xchg(&dplane->work, NULL);

	if (work)
		armada_drm_plane_work_call(dcrtc, work, work->cancel);
}

static void armada_drm_crtc_complete_frame_work(struct armada_crtc *dcrtc,
	struct armada_plane_work *work)
{
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_crtc_update_regs(dcrtc, work->regs);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

static void armada_drm_crtc_complete_disable_work(struct armada_crtc *dcrtc,
	struct armada_plane_work *work)
{
	unsigned long flags;

	if (dcrtc->plane == work->plane)
		dcrtc->plane = NULL;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_crtc_update_regs(dcrtc, work->regs);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

static struct armada_plane_work *
armada_drm_crtc_alloc_plane_work(struct drm_plane *plane)
{
	struct armada_plane_work *work;
	int i = 0;

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return NULL;

	work->plane = plane;
	work->fn = armada_drm_crtc_complete_frame_work;
	work->need_kfree = true;
	armada_reg_queue_end(work->regs, i);

	return work;
}

static void armada_drm_vblank_off(struct armada_crtc *dcrtc)
{
	/*
	 * Tell the DRM core that vblank IRQs aren't going to happen for
	 * a while.  This cleans up any pending vblank events for us.
	 */
	drm_crtc_vblank_off(&dcrtc->crtc);
	armada_drm_plane_work_run(dcrtc, dcrtc->crtc.primary);
}

/* The mode_config.mutex will be held for this call */
static void armada_drm_crtc_dpms(struct drm_crtc *crtc, int dpms)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);

	if (dpms_blanked(dcrtc->dpms) != dpms_blanked(dpms)) {
		if (dpms_blanked(dpms))
			armada_drm_vblank_off(dcrtc);
		else if (!IS_ERR(dcrtc->clk))
			WARN_ON(clk_prepare_enable(dcrtc->clk));
		dcrtc->dpms = dpms;
		armada_drm_crtc_update(dcrtc);
		if (!dpms_blanked(dpms))
			drm_crtc_vblank_on(&dcrtc->crtc);
		else if (!IS_ERR(dcrtc->clk))
			clk_disable_unprepare(dcrtc->clk);
	} else if (dcrtc->dpms != dpms) {
		dcrtc->dpms = dpms;
	}
}

/*
 * Prepare for a mode set.  Turn off overlay to ensure that we don't end
 * up with the overlay size being bigger than the active screen size.
 * We rely upon X refreshing this state after the mode set has completed.
 *
 * The mode_config.mutex will be held for this call
 */
static void armada_drm_crtc_prepare(struct drm_crtc *crtc)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct drm_plane *plane;
	u32 val;

	/*
	 * If we have an overlay plane associated with this CRTC, disable
	 * it before the modeset to avoid its coordinates being outside
	 * the new mode parameters.
	 */
	plane = dcrtc->plane;
	if (plane) {
		drm_plane_force_disable(plane);
		WARN_ON(!armada_drm_plane_work_wait(drm_to_armada_plane(plane),
						    HZ));
	}

	/* Wait for pending flips to complete */
	armada_drm_plane_work_wait(drm_to_armada_plane(dcrtc->crtc.primary),
				   MAX_SCHEDULE_TIMEOUT);

	drm_crtc_vblank_off(crtc);

	val = dcrtc->dumb_ctrl & ~CFG_DUMB_ENA;
	if (val != dcrtc->dumb_ctrl) {
		dcrtc->dumb_ctrl = val;
		writel_relaxed(val, dcrtc->base + LCD_SPU_DUMB_CTRL);
	}
}

/* The mode_config.mutex will be held for this call */
static void armada_drm_crtc_commit(struct drm_crtc *crtc)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);

	dcrtc->dpms = DRM_MODE_DPMS_ON;
	armada_drm_crtc_update(dcrtc);
	drm_crtc_vblank_on(crtc);
}

/* The mode_config.mutex will be held for this call */
static bool armada_drm_crtc_mode_fixup(struct drm_crtc *crtc,
	const struct drm_display_mode *mode, struct drm_display_mode *adj)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	int ret;

	/* We can't do interlaced modes if we don't have the SPU_ADV_REG */
	if (!dcrtc->variant->has_spu_adv_reg &&
	    adj->flags & DRM_MODE_FLAG_INTERLACE)
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
	void __iomem *base = dcrtc->base;
	struct drm_plane *ovl_plane;

	if (stat & DMA_FF_UNDERFLOW)
		DRM_ERROR("video underflow on crtc %u\n", dcrtc->num);
	if (stat & GRA_FF_UNDERFLOW)
		DRM_ERROR("graphics underflow on crtc %u\n", dcrtc->num);

	if (stat & VSYNC_IRQ)
		drm_crtc_handle_vblank(&dcrtc->crtc);

	ovl_plane = dcrtc->plane;
	if (ovl_plane)
		armada_drm_plane_work_run(dcrtc, ovl_plane);

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

	if (stat & DUMB_FRAMEDONE && dcrtc->cursor_update) {
		writel_relaxed(dcrtc->cursor_hw_pos,
			       base + LCD_SPU_HWC_OVSA_HPXL_VLN);
		writel_relaxed(dcrtc->cursor_hw_sz,
			       base + LCD_SPU_HWC_HPXL_VLN);
		armada_updatel(CFG_HWC_ENA,
			       CFG_HWC_ENA | CFG_HWC_1BITMOD | CFG_HWC_1BITENA,
			       base + LCD_SPU_DMA_CTRL0);
		dcrtc->cursor_update = false;
		armada_drm_crtc_disable_irq(dcrtc, DUMB_FRAMEDONE_ENA);
	}

	spin_unlock(&dcrtc->irq_lock);

	if (stat & GRA_FRAME_IRQ)
		armada_drm_plane_work_run(dcrtc, dcrtc->crtc.primary);
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

static uint32_t armada_drm_crtc_calculate_csc(struct armada_crtc *dcrtc)
{
	struct drm_display_mode *adj = &dcrtc->crtc.mode;
	uint32_t val = 0;

	if (dcrtc->csc_yuv_mode == CSC_YUV_CCIR709)
		val |= CFG_CSC_YUV_CCIR709;
	if (dcrtc->csc_rgb_mode == CSC_RGB_STUDIO)
		val |= CFG_CSC_RGB_STUDIO;

	/*
	 * In auto mode, set the colorimetry, based upon the HDMI spec.
	 * 1280x720p, 1920x1080p and 1920x1080i use ITU709, others use
	 * ITU601.  It may be more appropriate to set this depending on
	 * the source - but what if the graphic frame is YUV and the
	 * video frame is RGB?
	 */
	if ((adj->hdisplay == 1280 && adj->vdisplay == 720 &&
	     !(adj->flags & DRM_MODE_FLAG_INTERLACE)) ||
	    (adj->hdisplay == 1920 && adj->vdisplay == 1080)) {
		if (dcrtc->csc_yuv_mode == CSC_AUTO)
			val |= CFG_CSC_YUV_CCIR709;
	}

	/*
	 * We assume we're connected to a TV-like device, so the YUV->RGB
	 * conversion should produce a limited range.  We should set this
	 * depending on the connectors attached to this CRTC, and what
	 * kind of device they report being connected.
	 */
	if (dcrtc->csc_rgb_mode == CSC_AUTO)
		val |= CFG_CSC_RGB_STUDIO;

	return val;
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

	DRM_DEBUG_DRIVER("H: %d %d %d %d lm %d rm %d\n",
		adj->crtc_hdisplay,
		adj->crtc_hsync_start,
		adj->crtc_hsync_end,
		adj->crtc_htotal, lm, rm);
	DRM_DEBUG_DRIVER("V: %d %d %d %d tm %d bm %d\n",
		adj->crtc_vdisplay,
		adj->crtc_vsync_start,
		adj->crtc_vsync_end,
		adj->crtc_vtotal, tm, bm);

	/*
	 * If we are blanked, we would have disabled the clock.  Re-enable
	 * it so that compute_clock() does the right thing.
	 */
	if (!IS_ERR(dcrtc->clk) && dpms_blanked(dcrtc->dpms))
		WARN_ON(clk_prepare_enable(dcrtc->clk));

	/* Now compute the divider for real */
	dcrtc->variant->compute_clock(dcrtc, adj, &sclk);

	armada_reg_queue_set(regs, i, sclk, LCD_CFG_SCLK_DIV);

	if (interlaced ^ dcrtc->interlaced) {
		if (adj->flags & DRM_MODE_FLAG_INTERLACE)
			drm_crtc_vblank_get(&dcrtc->crtc);
		else
			drm_crtc_vblank_put(&dcrtc->crtc);
		dcrtc->interlaced = interlaced;
	}

	spin_lock_irqsave(&dcrtc->irq_lock, flags);

	/* Even interlaced/progressive frame */
	dcrtc->v[1].spu_v_h_total = adj->crtc_vtotal << 16 |
				    adj->crtc_htotal;
	dcrtc->v[1].spu_v_porch = tm << 16 | bm;
	val = adj->crtc_hsync_start;
	dcrtc->v[1].spu_adv_reg = val << 20 | val | ADV_VSYNCOFFEN |
		dcrtc->variant->spu_adv_reg;

	if (interlaced) {
		/* Odd interlaced frame */
		dcrtc->v[0].spu_v_h_total = dcrtc->v[1].spu_v_h_total +
						(1 << 16);
		dcrtc->v[0].spu_v_porch = dcrtc->v[1].spu_v_porch + 1;
		val = adj->crtc_hsync_start - adj->crtc_htotal / 2;
		dcrtc->v[0].spu_adv_reg = val << 20 | val | ADV_VSYNCOFFEN |
			dcrtc->variant->spu_adv_reg;
	} else {
		dcrtc->v[0] = dcrtc->v[1];
	}

	val = adj->crtc_vdisplay << 16 | adj->crtc_hdisplay;

	armada_reg_queue_set(regs, i, val, LCD_SPU_V_H_ACTIVE);
	armada_reg_queue_set(regs, i, (lm << 16) | rm, LCD_SPU_H_PORCH);
	armada_reg_queue_set(regs, i, dcrtc->v[0].spu_v_porch, LCD_SPU_V_PORCH);
	armada_reg_queue_set(regs, i, dcrtc->v[0].spu_v_h_total,
			   LCD_SPUT_V_H_TOTAL);

	if (dcrtc->variant->has_spu_adv_reg) {
		armada_reg_queue_mod(regs, i, dcrtc->v[0].spu_adv_reg,
				     ADV_VSYNC_L_OFF | ADV_VSYNC_H_OFF |
				     ADV_VSYNCOFFEN, LCD_SPU_ADV_REG);
	}

	val = adj->flags & DRM_MODE_FLAG_NVSYNC ? CFG_VSYNC_INV : 0;
	armada_reg_queue_mod(regs, i, val, CFG_VSYNC_INV, LCD_SPU_DMA_CTRL1);

	val = dcrtc->spu_iopad_ctrl | armada_drm_crtc_calculate_csc(dcrtc);
	armada_reg_queue_set(regs, i, val, LCD_SPU_IOPAD_CONTROL);
	armada_reg_queue_end(regs, i);

	armada_drm_crtc_update_regs(dcrtc, regs);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

/* The mode_config.mutex will be held for this call */
static void armada_drm_crtc_disable(struct drm_crtc *crtc)
{
	armada_drm_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	/* Disable our primary plane when we disable the CRTC. */
	crtc->primary->funcs->disable_plane(crtc->primary, NULL);
}

static void armada_drm_crtc_atomic_begin(struct drm_crtc *crtc,
					 struct drm_crtc_state *old_crtc_state)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct armada_plane *dplane;

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	/* Wait 100ms for any plane works to complete */
	dplane = drm_to_armada_plane(crtc->primary);
	if (WARN_ON(armada_drm_plane_work_wait(dplane, HZ / 10) == 0))
		armada_drm_plane_work_cancel(dcrtc, dplane);

	dcrtc->regs_idx = 0;
	dcrtc->regs = dcrtc->atomic_regs;
}

static void armada_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					 struct drm_crtc_state *old_crtc_state)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	unsigned long flags;

	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	armada_reg_queue_end(dcrtc->regs, dcrtc->regs_idx);

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_crtc_update_regs(dcrtc, dcrtc->regs);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

static const struct drm_crtc_helper_funcs armada_crtc_helper_funcs = {
	.dpms		= armada_drm_crtc_dpms,
	.prepare	= armada_drm_crtc_prepare,
	.commit		= armada_drm_crtc_commit,
	.mode_fixup	= armada_drm_crtc_mode_fixup,
	.mode_set	= drm_helper_crtc_mode_set,
	.mode_set_nofb	= armada_drm_crtc_mode_set_nofb,
	.mode_set_base	= drm_helper_crtc_mode_set_base,
	.disable	= armada_drm_crtc_disable,
	.atomic_begin	= armada_drm_crtc_atomic_begin,
	.atomic_flush	= armada_drm_crtc_atomic_flush,
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
		armada_drm_crtc_disable_irq(dcrtc, DUMB_FRAMEDONE_ENA);
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
		armada_drm_crtc_disable_irq(dcrtc, DUMB_FRAMEDONE_ENA);
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

	if (!IS_ERR(dcrtc->clk))
		clk_disable_unprepare(dcrtc->clk);

	writel_relaxed(0, dcrtc->base + LCD_SPU_IRQ_ENA);

	of_node_put(dcrtc->crtc.port);

	kfree(dcrtc);
}

/*
 * The mode_config lock is held here, to prevent races between this
 * and a mode_set.
 */
static int armada_drm_crtc_page_flip(struct drm_crtc *crtc,
	struct drm_framebuffer *fb, struct drm_pending_vblank_event *event,
	uint32_t page_flip_flags, struct drm_modeset_acquire_ctx *ctx)
{
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct drm_plane *plane = crtc->primary;
	const struct drm_plane_helper_funcs *plane_funcs;
	struct drm_plane_state *state;
	struct armada_plane_work *work;
	int ret;

	/* Construct new state for the primary plane */
	state = drm_atomic_helper_plane_duplicate_state(plane);
	if (!state)
		return -ENOMEM;

	drm_atomic_set_fb_for_plane(state, fb);

	work = armada_drm_crtc_alloc_plane_work(plane);
	if (!work) {
		ret = -ENOMEM;
		goto put_state;
	}

	/* Make sure we can get vblank interrupts */
	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		goto put_work;

	/*
	 * If we have another work pending, we can't process this flip.
	 * The modeset locks protect us from another user queuing a work
	 * while we're setting up.
	 */
	if (drm_to_armada_plane(plane)->work) {
		ret = -EBUSY;
		goto put_vblank;
	}

	work->event = event;
	work->old_fb = plane->state->fb;

	/*
	 * Hold a ref on the new fb while it's being displayed by the
	 * hardware. The old fb refcount will be released in the worker.
	 */
	drm_framebuffer_get(state->fb);

	/* Point of no return */
	swap(plane->state, state);

	dcrtc->regs_idx = 0;
	dcrtc->regs = work->regs;

	plane_funcs = plane->helper_private;
	plane_funcs->atomic_update(plane, state);
	armada_reg_queue_end(dcrtc->regs, dcrtc->regs_idx);

	/* Queue the work - this should never fail */
	WARN_ON(armada_drm_plane_work_queue(dcrtc, work));
	work = NULL;

	/*
	 * Finally, if the display is blanked, we won't receive an
	 * interrupt, so complete it now.
	 */
	if (dpms_blanked(dcrtc->dpms))
		armada_drm_plane_work_run(dcrtc, plane);

put_vblank:
	drm_crtc_vblank_put(crtc);
put_work:
	kfree(work);
put_state:
	drm_atomic_helper_plane_destroy_state(plane, state);
	return ret;
}

static int
armada_drm_crtc_set_property(struct drm_crtc *crtc,
	struct drm_property *property, uint64_t val)
{
	struct armada_private *priv = crtc->dev->dev_private;
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	bool update_csc = false;

	if (property == priv->csc_yuv_prop) {
		dcrtc->csc_yuv_mode = val;
		update_csc = true;
	} else if (property == priv->csc_rgb_prop) {
		dcrtc->csc_rgb_mode = val;
		update_csc = true;
	}

	if (update_csc) {
		uint32_t val;

		val = dcrtc->spu_iopad_ctrl |
		      armada_drm_crtc_calculate_csc(dcrtc);
		writel_relaxed(val, dcrtc->base + LCD_SPU_IOPAD_CONTROL);
	}

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
	.set_config	= drm_crtc_helper_set_config,
	.page_flip	= armada_drm_crtc_page_flip,
	.set_property	= armada_drm_crtc_set_property,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank	= armada_drm_crtc_enable_vblank,
	.disable_vblank	= armada_drm_crtc_disable_vblank,
};

static int armada_drm_plane_prepare_fb(struct drm_plane *plane,
	struct drm_plane_state *state)
{
	DRM_DEBUG_KMS("[PLANE:%d:%s] [FB:%d]\n",
		plane->base.id, plane->name,
		state->fb ? state->fb->base.id : 0);

	/*
	 * Take a reference on the new framebuffer - we want to
	 * hold on to it while the hardware is displaying it.
	 */
	if (state->fb)
		drm_framebuffer_get(state->fb);
	return 0;
}

static void armada_drm_plane_cleanup_fb(struct drm_plane *plane,
	struct drm_plane_state *old_state)
{
	DRM_DEBUG_KMS("[PLANE:%d:%s] [FB:%d]\n",
		plane->base.id, plane->name,
		old_state->fb ? old_state->fb->base.id : 0);

	if (old_state->fb)
		drm_framebuffer_put(old_state->fb);
}

static int armada_drm_plane_atomic_check(struct drm_plane *plane,
	struct drm_plane_state *state)
{
	if (state->fb && !WARN_ON(!state->crtc)) {
		struct drm_crtc *crtc = state->crtc;
		struct drm_crtc_state crtc_state = {
			.crtc = crtc,
			.enable = crtc->enabled,
			.mode = crtc->mode,
		};

		return drm_atomic_helper_check_plane_state(state, &crtc_state,
							   0, INT_MAX,
							   true, false);
	} else {
		state->visible = false;
	}
	return 0;
}

static unsigned int armada_drm_primary_update_state(
	struct drm_plane_state *state, struct armada_regs *regs)
{
	struct armada_plane *dplane = drm_to_armada_plane(state->plane);
	struct armada_crtc *dcrtc = drm_to_armada_crtc(state->crtc);
	struct armada_framebuffer *dfb = drm_fb_to_armada_fb(state->fb);
	bool was_disabled;
	unsigned int idx = 0;
	u32 val;

	val = CFG_GRA_FMT(dfb->fmt) | CFG_GRA_MOD(dfb->mod);
	if (dfb->fmt > CFG_420)
		val |= CFG_PALETTE_ENA;
	if (state->visible)
		val |= CFG_GRA_ENA;
	if (drm_rect_width(&state->src) >> 16 != drm_rect_width(&state->dst))
		val |= CFG_GRA_HSMOOTH;
	if (dcrtc->interlaced)
		val |= CFG_GRA_FTOGGLE;

	was_disabled = !(dplane->state.ctrl0 & CFG_GRA_ENA);
	if (was_disabled)
		armada_reg_queue_mod(regs, idx,
				     0, CFG_PDWN64x66, LCD_SPU_SRAM_PARA1);

	dplane->state.ctrl0 = val;
	dplane->state.src_hw = armada_rect_hw_fp(&state->src);
	dplane->state.dst_hw = armada_rect_hw(&state->dst);
	dplane->state.dst_yx = armada_rect_yx(&state->dst);

	idx += armada_drm_crtc_calc_fb(&dfb->fb, state->src.x1 >> 16,
				       state->src.y1 >> 16, regs + idx,
				       dcrtc->interlaced);
	armada_reg_queue_set(regs, idx, dplane->state.dst_yx,
			     LCD_SPU_GRA_OVSA_HPXL_VLN);
	armada_reg_queue_set(regs, idx, dplane->state.src_hw,
			     LCD_SPU_GRA_HPXL_VLN);
	armada_reg_queue_set(regs, idx, dplane->state.dst_hw,
			     LCD_SPU_GZM_HPXL_VLN);
	armada_reg_queue_mod(regs, idx, dplane->state.ctrl0, CFG_GRAFORMAT |
			     CFG_GRA_MOD(CFG_SWAPRB | CFG_SWAPUV |
					 CFG_SWAPYU | CFG_YUV2RGB) |
			     CFG_PALETTE_ENA | CFG_GRA_FTOGGLE |
			     CFG_GRA_HSMOOTH | CFG_GRA_ENA,
			     LCD_SPU_DMA_CTRL0);

	dplane->state.vsync_update = !was_disabled;
	dplane->state.changed = true;

	return idx;
}

static void armada_drm_primary_plane_atomic_update(struct drm_plane *plane,
	struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct armada_crtc *dcrtc;
	struct armada_regs *regs;

	DRM_DEBUG_KMS("[PLANE:%d:%s]\n", plane->base.id, plane->name);

	if (!state->fb || WARN_ON(!state->crtc))
		return;

	DRM_DEBUG_KMS("[PLANE:%d:%s] is on [CRTC:%d:%s] with [FB:%d] visible %u->%u\n",
		plane->base.id, plane->name,
		state->crtc->base.id, state->crtc->name,
		state->fb->base.id,
		old_state->visible, state->visible);

	dcrtc = drm_to_armada_crtc(state->crtc);
	regs = dcrtc->regs + dcrtc->regs_idx;

	dcrtc->regs_idx += armada_drm_primary_update_state(state, regs);
}

static void armada_drm_primary_plane_atomic_disable(struct drm_plane *plane,
	struct drm_plane_state *old_state)
{
	struct armada_plane *dplane = drm_to_armada_plane(plane);
	struct armada_crtc *dcrtc;
	struct armada_regs *regs;
	unsigned int idx = 0;

	DRM_DEBUG_KMS("[PLANE:%d:%s]\n", plane->base.id, plane->name);

	if (!old_state->crtc)
		return;

	DRM_DEBUG_KMS("[PLANE:%d:%s] was on [CRTC:%d:%s] with [FB:%d]\n",
		plane->base.id, plane->name,
		old_state->crtc->base.id, old_state->crtc->name,
		old_state->fb->base.id);

	dplane->state.ctrl0 &= ~CFG_GRA_ENA;

	dcrtc = drm_to_armada_crtc(old_state->crtc);
	regs = dcrtc->regs + dcrtc->regs_idx;

	/* Disable plane and power down most RAMs and FIFOs */
	armada_reg_queue_mod(regs, idx, 0, CFG_GRA_ENA, LCD_SPU_DMA_CTRL0);
	armada_reg_queue_mod(regs, idx, CFG_PDWN256x32 | CFG_PDWN256x24 |
			     CFG_PDWN256x8 | CFG_PDWN32x32 | CFG_PDWN64x66,
			     0, LCD_SPU_SRAM_PARA1);

	dcrtc->regs_idx += idx;
}

static const struct drm_plane_helper_funcs armada_primary_plane_helper_funcs = {
	.prepare_fb	= armada_drm_plane_prepare_fb,
	.cleanup_fb	= armada_drm_plane_cleanup_fb,
	.atomic_check	= armada_drm_plane_atomic_check,
	.atomic_update	= armada_drm_primary_plane_atomic_update,
	.atomic_disable	= armada_drm_primary_plane_atomic_disable,
};

int armada_drm_plane_disable(struct drm_plane *plane,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct armada_plane *dplane = drm_to_armada_plane(plane);
	struct armada_crtc *dcrtc;
	struct armada_plane_work *work;
	unsigned int idx = 0;
	u32 sram_para1, enable_mask;

	if (!plane->crtc)
		return 0;

	/*
	 * Arrange to power down most RAMs and FIFOs if this is the primary
	 * plane, otherwise just the YUV FIFOs for the overlay plane.
	 */
	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		sram_para1 = CFG_PDWN256x32 | CFG_PDWN256x24 | CFG_PDWN256x8 |
			     CFG_PDWN32x32 | CFG_PDWN64x66;
		enable_mask = CFG_GRA_ENA;
	} else {
		sram_para1 = CFG_PDWN16x66 | CFG_PDWN32x66;
		enable_mask = CFG_DMA_ENA;
	}

	dplane->state.ctrl0 &= ~enable_mask;

	dcrtc = drm_to_armada_crtc(plane->crtc);

	/*
	 * Try to disable the plane and drop our ref on the framebuffer
	 * at the next frame update. If we fail for any reason, disable
	 * the plane immediately.
	 */
	work = &dplane->works[dplane->next_work];
	work->fn = armada_drm_crtc_complete_disable_work;
	work->cancel = armada_drm_crtc_complete_disable_work;
	work->old_fb = plane->fb;

	armada_reg_queue_mod(work->regs, idx,
			     0, enable_mask, LCD_SPU_DMA_CTRL0);
	armada_reg_queue_mod(work->regs, idx,
			     sram_para1, 0, LCD_SPU_SRAM_PARA1);
	armada_reg_queue_end(work->regs, idx);

	/* Wait for any preceding work to complete, but don't wedge */
	if (WARN_ON(!armada_drm_plane_work_wait(dplane, HZ)))
		armada_drm_plane_work_cancel(dcrtc, dplane);

	if (armada_drm_plane_work_queue(dcrtc, work)) {
		work->fn(dcrtc, work);
		if (work->old_fb)
			drm_framebuffer_unreference(work->old_fb);
	}

	dplane->next_work = !dplane->next_work;

	return 0;
}

static const struct drm_plane_funcs armada_primary_plane_funcs = {
	.update_plane	= drm_plane_helper_update,
	.disable_plane	= drm_plane_helper_disable,
	.destroy	= drm_primary_helper_destroy,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

int armada_drm_plane_init(struct armada_plane *plane)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(plane->works); i++)
		plane->works[i].plane = &plane->base;

	init_waitqueue_head(&plane->frame_wait);

	return 0;
}

static const struct drm_prop_enum_list armada_drm_csc_yuv_enum_list[] = {
	{ CSC_AUTO,        "Auto" },
	{ CSC_YUV_CCIR601, "CCIR601" },
	{ CSC_YUV_CCIR709, "CCIR709" },
};

static const struct drm_prop_enum_list armada_drm_csc_rgb_enum_list[] = {
	{ CSC_AUTO,         "Auto" },
	{ CSC_RGB_COMPUTER, "Computer system" },
	{ CSC_RGB_STUDIO,   "Studio" },
};

static int armada_drm_crtc_create_properties(struct drm_device *dev)
{
	struct armada_private *priv = dev->dev_private;

	if (priv->csc_yuv_prop)
		return 0;

	priv->csc_yuv_prop = drm_property_create_enum(dev, 0,
				"CSC_YUV", armada_drm_csc_yuv_enum_list,
				ARRAY_SIZE(armada_drm_csc_yuv_enum_list));
	priv->csc_rgb_prop = drm_property_create_enum(dev, 0,
				"CSC_RGB", armada_drm_csc_rgb_enum_list,
				ARRAY_SIZE(armada_drm_csc_rgb_enum_list));

	if (!priv->csc_yuv_prop || !priv->csc_rgb_prop)
		return -ENOMEM;

	return 0;
}

static int armada_drm_crtc_create(struct drm_device *drm, struct device *dev,
	struct resource *res, int irq, const struct armada_variant *variant,
	struct device_node *port)
{
	struct armada_private *priv = drm->dev_private;
	struct armada_crtc *dcrtc;
	struct armada_plane *primary;
	void __iomem *base;
	int ret;

	ret = armada_drm_crtc_create_properties(drm);
	if (ret)
		return ret;

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
	dcrtc->clk = ERR_PTR(-EINVAL);
	dcrtc->csc_yuv_mode = CSC_AUTO;
	dcrtc->csc_rgb_mode = CSC_AUTO;
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

	ret = armada_drm_plane_init(primary);
	if (ret) {
		kfree(primary);
		goto err_crtc;
	}

	drm_plane_helper_add(&primary->base,
			     &armada_primary_plane_helper_funcs);

	ret = drm_universal_plane_init(drm, &primary->base, 0,
				       &armada_primary_plane_funcs,
				       armada_primary_formats,
				       ARRAY_SIZE(armada_primary_formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		kfree(primary);
		goto err_crtc;
	}

	ret = drm_crtc_init_with_planes(drm, &dcrtc->crtc, &primary->base, NULL,
					&armada_crtc_funcs, NULL);
	if (ret)
		goto err_crtc_init;

	drm_crtc_helper_add(&dcrtc->crtc, &armada_crtc_helper_funcs);

	drm_object_attach_property(&dcrtc->crtc.base, priv->csc_yuv_prop,
				   dcrtc->csc_yuv_mode);
	drm_object_attach_property(&dcrtc->crtc.base, priv->csc_rgb_prop,
				   dcrtc->csc_rgb_mode);

	return armada_overlay_plane_create(drm, 1 << dcrtc->num);

err_crtc_init:
	primary->base.funcs->destroy(&primary->base);
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
