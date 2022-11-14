// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Broadcom
 */

/**
 * DOC: VC4 CRTC module
 *
 * In VC4, the Pixel Valve is what most closely corresponds to the
 * DRM's concept of a CRTC.  The PV generates video timings from the
 * encoder's clock plus its configuration.  It pulls scaled pixels from
 * the HVS at that timing, and feeds it to the encoder.
 *
 * However, the DRM CRTC also collects the configuration of all the
 * DRM planes attached to it.  As a result, the CRTC is also
 * responsible for writing the display list for the HVS channel that
 * the CRTC will use.
 *
 * The 2835 has 3 different pixel valves.  pv0 in the audio power
 * domain feeds DSI0 or DPI, while pv1 feeds DS1 or SMI.  pv2 in the
 * image domain can feed either HDMI or the SDTV controller.  The
 * pixel valve chooses from the CPRMAN clocks (HSM for HDMI, VEC for
 * SDTV, etc.) according to which output type is chosen in the mux.
 *
 * For power management, the pixel valve's registers are all clocked
 * by the AXI clock, while the timings and FIFOs make use of the
 * output-specific clock.  Since the encoders also directly consume
 * the CPRMAN clocks, and know what timings they need, they are the
 * ones that set the clock.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "vc4_drv.h"
#include "vc4_hdmi.h"
#include "vc4_regs.h"

#define HVS_FIFO_LATENCY_PIX	6

#define CRTC_WRITE(offset, val) writel(val, vc4_crtc->regs + (offset))
#define CRTC_READ(offset) readl(vc4_crtc->regs + (offset))

static const struct debugfs_reg32 crtc_regs[] = {
	VC4_REG32(PV_CONTROL),
	VC4_REG32(PV_V_CONTROL),
	VC4_REG32(PV_VSYNCD_EVEN),
	VC4_REG32(PV_HORZA),
	VC4_REG32(PV_HORZB),
	VC4_REG32(PV_VERTA),
	VC4_REG32(PV_VERTB),
	VC4_REG32(PV_VERTA_EVEN),
	VC4_REG32(PV_VERTB_EVEN),
	VC4_REG32(PV_INTEN),
	VC4_REG32(PV_INTSTAT),
	VC4_REG32(PV_STAT),
	VC4_REG32(PV_HACT_ACT),
};

static unsigned int
vc4_crtc_get_cob_allocation(struct vc4_dev *vc4, unsigned int channel)
{
	struct vc4_hvs *hvs = vc4->hvs;
	u32 dispbase = HVS_READ(SCALER_DISPBASEX(channel));
	/* Top/base are supposed to be 4-pixel aligned, but the
	 * Raspberry Pi firmware fills the low bits (which are
	 * presumably ignored).
	 */
	u32 top = VC4_GET_FIELD(dispbase, SCALER_DISPBASEX_TOP) & ~3;
	u32 base = VC4_GET_FIELD(dispbase, SCALER_DISPBASEX_BASE) & ~3;

	return top - base + 4;
}

static bool vc4_crtc_get_scanout_position(struct drm_crtc *crtc,
					  bool in_vblank_irq,
					  int *vpos, int *hpos,
					  ktime_t *stime, ktime_t *etime,
					  const struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_hvs *hvs = vc4->hvs;
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct vc4_crtc_state *vc4_crtc_state = to_vc4_crtc_state(crtc->state);
	unsigned int cob_size;
	u32 val;
	int fifo_lines;
	int vblank_lines;
	bool ret = false;

	/* preempt_disable_rt() should go right here in PREEMPT_RT patchset. */

	/* Get optional system timestamp before query. */
	if (stime)
		*stime = ktime_get();

	/*
	 * Read vertical scanline which is currently composed for our
	 * pixelvalve by the HVS, and also the scaler status.
	 */
	val = HVS_READ(SCALER_DISPSTATX(vc4_crtc_state->assigned_channel));

	/* Get optional system timestamp after query. */
	if (etime)
		*etime = ktime_get();

	/* preempt_enable_rt() should go right here in PREEMPT_RT patchset. */

	/* Vertical position of hvs composed scanline. */
	*vpos = VC4_GET_FIELD(val, SCALER_DISPSTATX_LINE);
	*hpos = 0;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		*vpos /= 2;

		/* Use hpos to correct for field offset in interlaced mode. */
		if (vc4_hvs_get_fifo_frame_count(hvs, vc4_crtc_state->assigned_channel) % 2)
			*hpos += mode->crtc_htotal / 2;
	}

	cob_size = vc4_crtc_get_cob_allocation(vc4, vc4_crtc_state->assigned_channel);
	/* This is the offset we need for translating hvs -> pv scanout pos. */
	fifo_lines = cob_size / mode->crtc_hdisplay;

	if (fifo_lines > 0)
		ret = true;

	/* HVS more than fifo_lines into frame for compositing? */
	if (*vpos > fifo_lines) {
		/*
		 * We are in active scanout and can get some meaningful results
		 * from HVS. The actual PV scanout can not trail behind more
		 * than fifo_lines as that is the fifo's capacity. Assume that
		 * in active scanout the HVS and PV work in lockstep wrt. HVS
		 * refilling the fifo and PV consuming from the fifo, ie.
		 * whenever the PV consumes and frees up a scanline in the
		 * fifo, the HVS will immediately refill it, therefore
		 * incrementing vpos. Therefore we choose HVS read position -
		 * fifo size in scanlines as a estimate of the real scanout
		 * position of the PV.
		 */
		*vpos -= fifo_lines + 1;

		return ret;
	}

	/*
	 * Less: This happens when we are in vblank and the HVS, after getting
	 * the VSTART restart signal from the PV, just started refilling its
	 * fifo with new lines from the top-most lines of the new framebuffers.
	 * The PV does not scan out in vblank, so does not remove lines from
	 * the fifo, so the fifo will be full quickly and the HVS has to pause.
	 * We can't get meaningful readings wrt. scanline position of the PV
	 * and need to make things up in a approximative but consistent way.
	 */
	vblank_lines = mode->vtotal - mode->vdisplay;

	if (in_vblank_irq) {
		/*
		 * Assume the irq handler got called close to first
		 * line of vblank, so PV has about a full vblank
		 * scanlines to go, and as a base timestamp use the
		 * one taken at entry into vblank irq handler, so it
		 * is not affected by random delays due to lock
		 * contention on event_lock or vblank_time lock in
		 * the core.
		 */
		*vpos = -vblank_lines;

		if (stime)
			*stime = vc4_crtc->t_vblank;
		if (etime)
			*etime = vc4_crtc->t_vblank;

		/*
		 * If the HVS fifo is not yet full then we know for certain
		 * we are at the very beginning of vblank, as the hvs just
		 * started refilling, and the stime and etime timestamps
		 * truly correspond to start of vblank.
		 *
		 * Unfortunately there's no way to report this to upper levels
		 * and make it more useful.
		 */
	} else {
		/*
		 * No clue where we are inside vblank. Return a vpos of zero,
		 * which will cause calling code to just return the etime
		 * timestamp uncorrected. At least this is no worse than the
		 * standard fallback.
		 */
		*vpos = 0;
	}

	return ret;
}

static u32 vc4_get_fifo_full_level(struct vc4_crtc *vc4_crtc, u32 format)
{
	const struct vc4_crtc_data *crtc_data = vc4_crtc_to_vc4_crtc_data(vc4_crtc);
	const struct vc4_pv_data *pv_data = vc4_crtc_to_vc4_pv_data(vc4_crtc);
	struct vc4_dev *vc4 = to_vc4_dev(vc4_crtc->base.dev);
	u32 fifo_len_bytes = pv_data->fifo_depth;

	/*
	 * Pixels are pulled from the HVS if the number of bytes is
	 * lower than the FIFO full level.
	 *
	 * The latency of the pixel fetch mechanism is 6 pixels, so we
	 * need to convert those 6 pixels in bytes, depending on the
	 * format, and then subtract that from the length of the FIFO
	 * to make sure we never end up in a situation where the FIFO
	 * is full.
	 */
	switch (format) {
	case PV_CONTROL_FORMAT_DSIV_16:
	case PV_CONTROL_FORMAT_DSIC_16:
		return fifo_len_bytes - 2 * HVS_FIFO_LATENCY_PIX;
	case PV_CONTROL_FORMAT_DSIV_18:
		return fifo_len_bytes - 14;
	case PV_CONTROL_FORMAT_24:
	case PV_CONTROL_FORMAT_DSIV_24:
	default:
		/*
		 * For some reason, the pixelvalve4 doesn't work with
		 * the usual formula and will only work with 32.
		 */
		if (crtc_data->hvs_output == 5)
			return 32;

		/*
		 * It looks like in some situations, we will overflow
		 * the PixelValve FIFO (with the bit 10 of PV stat being
		 * set) and stall the HVS / PV, eventually resulting in
		 * a page flip timeout.
		 *
		 * Displaying the video overlay during a playback with
		 * Kodi on an RPi3 seems to be a great solution with a
		 * failure rate around 50%.
		 *
		 * Removing 1 from the FIFO full level however
		 * seems to completely remove that issue.
		 */
		if (!vc4->is_vc5)
			return fifo_len_bytes - 3 * HVS_FIFO_LATENCY_PIX - 1;

		return fifo_len_bytes - 3 * HVS_FIFO_LATENCY_PIX;
	}
}

static u32 vc4_crtc_get_fifo_full_level_bits(struct vc4_crtc *vc4_crtc,
					     u32 format)
{
	u32 level = vc4_get_fifo_full_level(vc4_crtc, format);
	u32 ret = 0;

	ret |= VC4_SET_FIELD((level >> 6),
			     PV5_CONTROL_FIFO_LEVEL_HIGH);

	return ret | VC4_SET_FIELD(level & 0x3f,
				   PV_CONTROL_FIFO_LEVEL);
}

/*
 * Returns the encoder attached to the CRTC.
 *
 * VC4 can only scan out to one encoder at a time, while the DRM core
 * allows drivers to push pixels to more than one encoder from the
 * same CRTC.
 */
struct drm_encoder *vc4_get_crtc_encoder(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	struct drm_encoder *encoder;

	WARN_ON(hweight32(state->encoder_mask) > 1);

	drm_for_each_encoder_mask(encoder, crtc->dev, state->encoder_mask)
		return encoder;

	return NULL;
}

static void vc4_crtc_pixelvalve_reset(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	/* The PV needs to be disabled before it can be flushed */
	CRTC_WRITE(PV_CONTROL, CRTC_READ(PV_CONTROL) & ~PV_CONTROL_EN);
	CRTC_WRITE(PV_CONTROL, CRTC_READ(PV_CONTROL) | PV_CONTROL_FIFO_CLR);

	drm_dev_exit(idx);
}

static void vc4_crtc_config_pv(struct drm_crtc *crtc, struct drm_encoder *encoder,
			       struct drm_atomic_state *state)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	const struct vc4_pv_data *pv_data = vc4_crtc_to_vc4_pv_data(vc4_crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	bool interlace = mode->flags & DRM_MODE_FLAG_INTERLACE;
	bool is_hdmi = vc4_encoder->type == VC4_ENCODER_TYPE_HDMI0 ||
		       vc4_encoder->type == VC4_ENCODER_TYPE_HDMI1;
	u32 pixel_rep = ((mode->flags & DRM_MODE_FLAG_DBLCLK) && !is_hdmi) ? 2 : 1;
	bool is_dsi = (vc4_encoder->type == VC4_ENCODER_TYPE_DSI0 ||
		       vc4_encoder->type == VC4_ENCODER_TYPE_DSI1);
	bool is_dsi1 = vc4_encoder->type == VC4_ENCODER_TYPE_DSI1;
	u32 format = is_dsi1 ? PV_CONTROL_FORMAT_DSIV_24 : PV_CONTROL_FORMAT_24;
	u8 ppc = pv_data->pixels_per_clock;
	bool debug_dump_regs = false;
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	if (debug_dump_regs) {
		struct drm_printer p = drm_info_printer(&vc4_crtc->pdev->dev);
		dev_info(&vc4_crtc->pdev->dev, "CRTC %d regs before:\n",
			 drm_crtc_index(crtc));
		drm_print_regset32(&p, &vc4_crtc->regset);
	}

	vc4_crtc_pixelvalve_reset(crtc);

	CRTC_WRITE(PV_HORZA,
		   VC4_SET_FIELD((mode->htotal - mode->hsync_end) * pixel_rep / ppc,
				 PV_HORZA_HBP) |
		   VC4_SET_FIELD((mode->hsync_end - mode->hsync_start) * pixel_rep / ppc,
				 PV_HORZA_HSYNC));

	CRTC_WRITE(PV_HORZB,
		   VC4_SET_FIELD((mode->hsync_start - mode->hdisplay) * pixel_rep / ppc,
				 PV_HORZB_HFP) |
		   VC4_SET_FIELD(mode->hdisplay * pixel_rep / ppc,
				 PV_HORZB_HACTIVE));

	CRTC_WRITE(PV_VERTA,
		   VC4_SET_FIELD(mode->crtc_vtotal - mode->crtc_vsync_end +
				 interlace,
				 PV_VERTA_VBP) |
		   VC4_SET_FIELD(mode->crtc_vsync_end - mode->crtc_vsync_start,
				 PV_VERTA_VSYNC));
	CRTC_WRITE(PV_VERTB,
		   VC4_SET_FIELD(mode->crtc_vsync_start - mode->crtc_vdisplay,
				 PV_VERTB_VFP) |
		   VC4_SET_FIELD(mode->crtc_vdisplay, PV_VERTB_VACTIVE));

	if (interlace) {
		CRTC_WRITE(PV_VERTA_EVEN,
			   VC4_SET_FIELD(mode->crtc_vtotal -
					 mode->crtc_vsync_end,
					 PV_VERTA_VBP) |
			   VC4_SET_FIELD(mode->crtc_vsync_end -
					 mode->crtc_vsync_start,
					 PV_VERTA_VSYNC));
		CRTC_WRITE(PV_VERTB_EVEN,
			   VC4_SET_FIELD(mode->crtc_vsync_start -
					 mode->crtc_vdisplay,
					 PV_VERTB_VFP) |
			   VC4_SET_FIELD(mode->crtc_vdisplay, PV_VERTB_VACTIVE));

		/* We set up first field even mode for HDMI.  VEC's
		 * NTSC mode would want first field odd instead, once
		 * we support it (to do so, set ODD_FIRST and put the
		 * delay in VSYNCD_EVEN instead).
		 */
		CRTC_WRITE(PV_V_CONTROL,
			   PV_VCONTROL_CONTINUOUS |
			   (is_dsi ? PV_VCONTROL_DSI : 0) |
			   PV_VCONTROL_INTERLACE |
			   VC4_SET_FIELD(mode->htotal * pixel_rep / (2 * ppc),
					 PV_VCONTROL_ODD_DELAY));
		CRTC_WRITE(PV_VSYNCD_EVEN, 0);
	} else {
		CRTC_WRITE(PV_V_CONTROL,
			   PV_VCONTROL_CONTINUOUS |
			   (is_dsi ? PV_VCONTROL_DSI : 0));
	}

	if (is_dsi)
		CRTC_WRITE(PV_HACT_ACT, mode->hdisplay * pixel_rep);

	if (vc4->is_vc5)
		CRTC_WRITE(PV_MUX_CFG,
			   VC4_SET_FIELD(PV_MUX_CFG_RGB_PIXEL_MUX_MODE_NO_SWAP,
					 PV_MUX_CFG_RGB_PIXEL_MUX_MODE));

	CRTC_WRITE(PV_CONTROL, PV_CONTROL_FIFO_CLR |
		   vc4_crtc_get_fifo_full_level_bits(vc4_crtc, format) |
		   VC4_SET_FIELD(format, PV_CONTROL_FORMAT) |
		   VC4_SET_FIELD(pixel_rep - 1, PV_CONTROL_PIXEL_REP) |
		   PV_CONTROL_CLR_AT_START |
		   PV_CONTROL_TRIGGER_UNDERFLOW |
		   PV_CONTROL_WAIT_HSTART |
		   VC4_SET_FIELD(vc4_encoder->clock_select,
				 PV_CONTROL_CLK_SELECT));

	if (debug_dump_regs) {
		struct drm_printer p = drm_info_printer(&vc4_crtc->pdev->dev);
		dev_info(&vc4_crtc->pdev->dev, "CRTC %d regs after:\n",
			 drm_crtc_index(crtc));
		drm_print_regset32(&p, &vc4_crtc->regset);
	}

	drm_dev_exit(idx);
}

static void require_hvs_enabled(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_hvs *hvs = vc4->hvs;

	WARN_ON_ONCE((HVS_READ(SCALER_DISPCTRL) & SCALER_DISPCTRL_ENABLE) !=
		     SCALER_DISPCTRL_ENABLE);
}

static int vc4_crtc_disable(struct drm_crtc *crtc,
			    struct drm_encoder *encoder,
			    struct drm_atomic_state *state,
			    unsigned int channel)
{
	struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int idx, ret;

	if (!drm_dev_enter(dev, &idx))
		return -ENODEV;

	CRTC_WRITE(PV_V_CONTROL,
		   CRTC_READ(PV_V_CONTROL) & ~PV_VCONTROL_VIDEN);
	ret = wait_for(!(CRTC_READ(PV_V_CONTROL) & PV_VCONTROL_VIDEN), 1);
	WARN_ONCE(ret, "Timeout waiting for !PV_VCONTROL_VIDEN\n");

	/*
	 * This delay is needed to avoid to get a pixel stuck in an
	 * unflushable FIFO between the pixelvalve and the HDMI
	 * controllers on the BCM2711.
	 *
	 * Timing is fairly sensitive here, so mdelay is the safest
	 * approach.
	 *
	 * If it was to be reworked, the stuck pixel happens on a
	 * BCM2711 when changing mode with a good probability, so a
	 * script that changes mode on a regular basis should trigger
	 * the bug after less than 10 attempts. It manifests itself with
	 * every pixels being shifted by one to the right, and thus the
	 * last pixel of a line actually being displayed as the first
	 * pixel on the next line.
	 */
	mdelay(20);

	if (vc4_encoder && vc4_encoder->post_crtc_disable)
		vc4_encoder->post_crtc_disable(encoder, state);

	vc4_crtc_pixelvalve_reset(crtc);
	vc4_hvs_stop_channel(vc4->hvs, channel);

	if (vc4_encoder && vc4_encoder->post_crtc_powerdown)
		vc4_encoder->post_crtc_powerdown(encoder, state);

	drm_dev_exit(idx);

	return 0;
}

static struct drm_encoder *vc4_crtc_get_encoder_by_type(struct drm_crtc *crtc,
							enum vc4_encoder_type type)
{
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, crtc->dev) {
		struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);

		if (vc4_encoder->type == type)
			return encoder;
	}

	return NULL;
}

int vc4_crtc_disable_at_boot(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	enum vc4_encoder_type encoder_type;
	const struct vc4_pv_data *pv_data;
	struct drm_encoder *encoder;
	struct vc4_hdmi *vc4_hdmi;
	unsigned encoder_sel;
	int channel;
	int ret;

	if (!(of_device_is_compatible(vc4_crtc->pdev->dev.of_node,
				      "brcm,bcm2711-pixelvalve2") ||
	      of_device_is_compatible(vc4_crtc->pdev->dev.of_node,
				      "brcm,bcm2711-pixelvalve4")))
		return 0;

	if (!(CRTC_READ(PV_CONTROL) & PV_CONTROL_EN))
		return 0;

	if (!(CRTC_READ(PV_V_CONTROL) & PV_VCONTROL_VIDEN))
		return 0;

	channel = vc4_hvs_get_fifo_from_output(vc4->hvs, vc4_crtc->data->hvs_output);
	if (channel < 0)
		return 0;

	encoder_sel = VC4_GET_FIELD(CRTC_READ(PV_CONTROL), PV_CONTROL_CLK_SELECT);
	if (WARN_ON(encoder_sel != 0))
		return 0;

	pv_data = vc4_crtc_to_vc4_pv_data(vc4_crtc);
	encoder_type = pv_data->encoder_types[encoder_sel];
	encoder = vc4_crtc_get_encoder_by_type(crtc, encoder_type);
	if (WARN_ON(!encoder))
		return 0;

	vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	ret = pm_runtime_resume_and_get(&vc4_hdmi->pdev->dev);
	if (ret)
		return ret;

	ret = vc4_crtc_disable(crtc, encoder, NULL, channel);
	if (ret)
		return ret;

	/*
	 * post_crtc_powerdown will have called pm_runtime_put, so we
	 * don't need it here otherwise we'll get the reference counting
	 * wrong.
	 */

	return 0;
}

void vc4_crtc_send_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	if (!crtc->state || !crtc->state->event)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_crtc_send_vblank_event(crtc, crtc->state->event);
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void vc4_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_state = drm_atomic_get_old_crtc_state(state,
									 crtc);
	struct vc4_crtc_state *old_vc4_state = to_vc4_crtc_state(old_state);
	struct drm_encoder *encoder = vc4_get_crtc_encoder(crtc, old_state);
	struct drm_device *dev = crtc->dev;

	drm_dbg(dev, "Disabling CRTC %s (%u) connected to Encoder %s (%u)",
		crtc->name, crtc->base.id, encoder->name, encoder->base.id);

	require_hvs_enabled(dev);

	/* Disable vblank irq handling before crtc is disabled. */
	drm_crtc_vblank_off(crtc);

	vc4_crtc_disable(crtc, encoder, state, old_vc4_state->assigned_channel);

	/*
	 * Make sure we issue a vblank event after disabling the CRTC if
	 * someone was waiting it.
	 */
	vc4_crtc_send_vblank(crtc);
}

static void vc4_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct drm_crtc_state *new_state = drm_atomic_get_new_crtc_state(state,
									 crtc);
	struct drm_device *dev = crtc->dev;
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_encoder *encoder = vc4_get_crtc_encoder(crtc, new_state);
	struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);
	int idx;

	drm_dbg(dev, "Enabling CRTC %s (%u) connected to Encoder %s (%u)",
		crtc->name, crtc->base.id, encoder->name, encoder->base.id);

	if (!drm_dev_enter(dev, &idx))
		return;

	require_hvs_enabled(dev);

	/* Enable vblank irq handling before crtc is started otherwise
	 * drm_crtc_get_vblank() fails in vc4_crtc_update_dlist().
	 */
	drm_crtc_vblank_on(crtc);

	vc4_hvs_atomic_enable(crtc, state);

	if (vc4_encoder->pre_crtc_configure)
		vc4_encoder->pre_crtc_configure(encoder, state);

	vc4_crtc_config_pv(crtc, encoder, state);

	CRTC_WRITE(PV_CONTROL, CRTC_READ(PV_CONTROL) | PV_CONTROL_EN);

	if (vc4_encoder->pre_crtc_enable)
		vc4_encoder->pre_crtc_enable(encoder, state);

	/* When feeding the transposer block the pixelvalve is unneeded and
	 * should not be enabled.
	 */
	CRTC_WRITE(PV_V_CONTROL,
		   CRTC_READ(PV_V_CONTROL) | PV_VCONTROL_VIDEN);

	if (vc4_encoder->post_crtc_enable)
		vc4_encoder->post_crtc_enable(encoder, state);

	drm_dev_exit(idx);
}

static enum drm_mode_status vc4_crtc_mode_valid(struct drm_crtc *crtc,
						const struct drm_display_mode *mode)
{
	/* Do not allow doublescan modes from user space */
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		DRM_DEBUG_KMS("[CRTC:%d] Doublescan mode rejected.\n",
			      crtc->base.id);
		return MODE_NO_DBLESCAN;
	}

	return MODE_OK;
}

void vc4_crtc_get_margins(struct drm_crtc_state *state,
			  unsigned int *left, unsigned int *right,
			  unsigned int *top, unsigned int *bottom)
{
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(state);
	struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	*left = vc4_state->margins.left;
	*right = vc4_state->margins.right;
	*top = vc4_state->margins.top;
	*bottom = vc4_state->margins.bottom;

	/* We have to interate over all new connector states because
	 * vc4_crtc_get_margins() might be called before
	 * vc4_crtc_atomic_check() which means margins info in vc4_crtc_state
	 * might be outdated.
	 */
	for_each_new_connector_in_state(state->state, conn, conn_state, i) {
		if (conn_state->crtc != state->crtc)
			continue;

		*left = conn_state->tv.margins.left;
		*right = conn_state->tv.margins.right;
		*top = conn_state->tv.margins.top;
		*bottom = conn_state->tv.margins.bottom;
		break;
	}
}

static int vc4_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(crtc_state);
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	struct drm_encoder *encoder;
	int ret, i;

	ret = vc4_hvs_atomic_check(crtc, state);
	if (ret)
		return ret;

	encoder = vc4_get_crtc_encoder(crtc, crtc_state);
	if (encoder) {
		const struct drm_display_mode *mode = &crtc_state->adjusted_mode;
		struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);

		if (vc4_encoder->type == VC4_ENCODER_TYPE_HDMI0) {
			vc4_state->hvs_load = max(mode->clock * mode->hdisplay / mode->htotal + 1000,
						  mode->clock * 9 / 10) * 1000;
		} else {
			vc4_state->hvs_load = mode->clock * 1000;
		}
	}

	for_each_new_connector_in_state(state, conn, conn_state,
					i) {
		if (conn_state->crtc != crtc)
			continue;

		vc4_state->margins.left = conn_state->tv.margins.left;
		vc4_state->margins.right = conn_state->tv.margins.right;
		vc4_state->margins.top = conn_state->tv.margins.top;
		vc4_state->margins.bottom = conn_state->tv.margins.bottom;
		break;
	}

	return 0;
}

static int vc4_enable_vblank(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return -ENODEV;

	CRTC_WRITE(PV_INTEN, PV_INT_VFP_START);

	drm_dev_exit(idx);

	return 0;
}

static void vc4_disable_vblank(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	CRTC_WRITE(PV_INTEN, 0);

	drm_dev_exit(idx);
}

static void vc4_crtc_handle_page_flip(struct vc4_crtc *vc4_crtc)
{
	struct drm_crtc *crtc = &vc4_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_hvs *hvs = vc4->hvs;
	u32 chan = vc4_crtc->current_hvs_channel;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	spin_lock(&vc4_crtc->irq_lock);
	if (vc4_crtc->event &&
	    (vc4_crtc->current_dlist == HVS_READ(SCALER_DISPLACTX(chan)) ||
	     vc4_crtc->feeds_txp)) {
		drm_crtc_send_vblank_event(crtc, vc4_crtc->event);
		vc4_crtc->event = NULL;
		drm_crtc_vblank_put(crtc);

		/* Wait for the page flip to unmask the underrun to ensure that
		 * the display list was updated by the hardware. Before that
		 * happens, the HVS will be using the previous display list with
		 * the CRTC and encoder already reconfigured, leading to
		 * underruns. This can be seen when reconfiguring the CRTC.
		 */
		vc4_hvs_unmask_underrun(hvs, chan);
	}
	spin_unlock(&vc4_crtc->irq_lock);
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

void vc4_crtc_handle_vblank(struct vc4_crtc *crtc)
{
	crtc->t_vblank = ktime_get();
	drm_crtc_handle_vblank(&crtc->base);
	vc4_crtc_handle_page_flip(crtc);
}

static irqreturn_t vc4_crtc_irq_handler(int irq, void *data)
{
	struct vc4_crtc *vc4_crtc = data;
	u32 stat = CRTC_READ(PV_INTSTAT);
	irqreturn_t ret = IRQ_NONE;

	if (stat & PV_INT_VFP_START) {
		CRTC_WRITE(PV_INTSTAT, PV_INT_VFP_START);
		vc4_crtc_handle_vblank(vc4_crtc);
		ret = IRQ_HANDLED;
	}

	return ret;
}

struct vc4_async_flip_state {
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;
	struct drm_framebuffer *old_fb;
	struct drm_pending_vblank_event *event;

	union {
		struct dma_fence_cb fence;
		struct vc4_seqno_cb seqno;
	} cb;
};

/* Called when the V3D execution for the BO being flipped to is done, so that
 * we can actually update the plane's address to point to it.
 */
static void
vc4_async_page_flip_complete(struct vc4_async_flip_state *flip_state)
{
	struct drm_crtc *crtc = flip_state->crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_plane *plane = crtc->primary;

	vc4_plane_async_set_fb(plane, flip_state->fb);
	if (flip_state->event) {
		unsigned long flags;

		spin_lock_irqsave(&dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, flip_state->event);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	drm_crtc_vblank_put(crtc);
	drm_framebuffer_put(flip_state->fb);

	if (flip_state->old_fb)
		drm_framebuffer_put(flip_state->old_fb);

	kfree(flip_state);
}

static void vc4_async_page_flip_seqno_complete(struct vc4_seqno_cb *cb)
{
	struct vc4_async_flip_state *flip_state =
		container_of(cb, struct vc4_async_flip_state, cb.seqno);
	struct vc4_bo *bo = NULL;

	if (flip_state->old_fb) {
		struct drm_gem_dma_object *dma_bo =
			drm_fb_dma_get_gem_obj(flip_state->old_fb, 0);
		bo = to_vc4_bo(&dma_bo->base);
	}

	vc4_async_page_flip_complete(flip_state);

	/*
	 * Decrement the BO usecnt in order to keep the inc/dec
	 * calls balanced when the planes are updated through
	 * the async update path.
	 *
	 * FIXME: we should move to generic async-page-flip when
	 * it's available, so that we can get rid of this
	 * hand-made cleanup_fb() logic.
	 */
	if (bo)
		vc4_bo_dec_usecnt(bo);
}

static void vc4_async_page_flip_fence_complete(struct dma_fence *fence,
					       struct dma_fence_cb *cb)
{
	struct vc4_async_flip_state *flip_state =
		container_of(cb, struct vc4_async_flip_state, cb.fence);

	vc4_async_page_flip_complete(flip_state);
	dma_fence_put(fence);
}

static int vc4_async_set_fence_cb(struct drm_device *dev,
				  struct vc4_async_flip_state *flip_state)
{
	struct drm_framebuffer *fb = flip_state->fb;
	struct drm_gem_dma_object *dma_bo = drm_fb_dma_get_gem_obj(fb, 0);
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct dma_fence *fence;
	int ret;

	if (!vc4->is_vc5) {
		struct vc4_bo *bo = to_vc4_bo(&dma_bo->base);

		return vc4_queue_seqno_cb(dev, &flip_state->cb.seqno, bo->seqno,
					  vc4_async_page_flip_seqno_complete);
	}

	ret = dma_resv_get_singleton(dma_bo->base.resv, DMA_RESV_USAGE_READ, &fence);
	if (ret)
		return ret;

	/* If there's no fence, complete the page flip immediately */
	if (!fence) {
		vc4_async_page_flip_fence_complete(fence, &flip_state->cb.fence);
		return 0;
	}

	/* If the fence has already been completed, complete the page flip */
	if (dma_fence_add_callback(fence, &flip_state->cb.fence,
				   vc4_async_page_flip_fence_complete))
		vc4_async_page_flip_fence_complete(fence, &flip_state->cb.fence);

	return 0;
}

static int
vc4_async_page_flip_common(struct drm_crtc *crtc,
			   struct drm_framebuffer *fb,
			   struct drm_pending_vblank_event *event,
			   uint32_t flags)
{
	struct drm_device *dev = crtc->dev;
	struct drm_plane *plane = crtc->primary;
	struct vc4_async_flip_state *flip_state;

	flip_state = kzalloc(sizeof(*flip_state), GFP_KERNEL);
	if (!flip_state)
		return -ENOMEM;

	drm_framebuffer_get(fb);
	flip_state->fb = fb;
	flip_state->crtc = crtc;
	flip_state->event = event;

	/* Save the current FB before it's replaced by the new one in
	 * drm_atomic_set_fb_for_plane(). We'll need the old FB in
	 * vc4_async_page_flip_complete() to decrement the BO usecnt and keep
	 * it consistent.
	 * FIXME: we should move to generic async-page-flip when it's
	 * available, so that we can get rid of this hand-made cleanup_fb()
	 * logic.
	 */
	flip_state->old_fb = plane->state->fb;
	if (flip_state->old_fb)
		drm_framebuffer_get(flip_state->old_fb);

	WARN_ON(drm_crtc_vblank_get(crtc) != 0);

	/* Immediately update the plane's legacy fb pointer, so that later
	 * modeset prep sees the state that will be present when the semaphore
	 * is released.
	 */
	drm_atomic_set_fb_for_plane(plane->state, fb);

	vc4_async_set_fence_cb(dev, flip_state);

	/* Driver takes ownership of state on successful async commit. */
	return 0;
}

/* Implements async (non-vblank-synced) page flips.
 *
 * The page flip ioctl needs to return immediately, so we grab the
 * modeset semaphore on the pipe, and queue the address update for
 * when V3D is done with the BO being flipped to.
 */
static int vc4_async_page_flip(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb,
			       struct drm_pending_vblank_event *event,
			       uint32_t flags)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_gem_dma_object *dma_bo = drm_fb_dma_get_gem_obj(fb, 0);
	struct vc4_bo *bo = to_vc4_bo(&dma_bo->base);
	int ret;

	if (WARN_ON_ONCE(vc4->is_vc5))
		return -ENODEV;

	/*
	 * Increment the BO usecnt here, so that we never end up with an
	 * unbalanced number of vc4_bo_{dec,inc}_usecnt() calls when the
	 * plane is later updated through the non-async path.
	 *
	 * FIXME: we should move to generic async-page-flip when
	 * it's available, so that we can get rid of this
	 * hand-made prepare_fb() logic.
	 */
	ret = vc4_bo_inc_usecnt(bo);
	if (ret)
		return ret;

	ret = vc4_async_page_flip_common(crtc, fb, event, flags);
	if (ret) {
		vc4_bo_dec_usecnt(bo);
		return ret;
	}

	return 0;
}

static int vc5_async_page_flip(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb,
			       struct drm_pending_vblank_event *event,
			       uint32_t flags)
{
	return vc4_async_page_flip_common(crtc, fb, event, flags);
}

int vc4_page_flip(struct drm_crtc *crtc,
		  struct drm_framebuffer *fb,
		  struct drm_pending_vblank_event *event,
		  uint32_t flags,
		  struct drm_modeset_acquire_ctx *ctx)
{
	if (flags & DRM_MODE_PAGE_FLIP_ASYNC) {
		struct drm_device *dev = crtc->dev;
		struct vc4_dev *vc4 = to_vc4_dev(dev);

		if (vc4->is_vc5)
			return vc5_async_page_flip(crtc, fb, event, flags);
		else
			return vc4_async_page_flip(crtc, fb, event, flags);
	} else {
		return drm_atomic_helper_page_flip(crtc, fb, event, flags, ctx);
	}
}

struct drm_crtc_state *vc4_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct vc4_crtc_state *vc4_state, *old_vc4_state;

	vc4_state = kzalloc(sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return NULL;

	old_vc4_state = to_vc4_crtc_state(crtc->state);
	vc4_state->margins = old_vc4_state->margins;
	vc4_state->assigned_channel = old_vc4_state->assigned_channel;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &vc4_state->base);
	return &vc4_state->base;
}

void vc4_crtc_destroy_state(struct drm_crtc *crtc,
			    struct drm_crtc_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(crtc->dev);
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(state);

	if (drm_mm_node_allocated(&vc4_state->mm)) {
		unsigned long flags;

		spin_lock_irqsave(&vc4->hvs->mm_lock, flags);
		drm_mm_remove_node(&vc4_state->mm);
		spin_unlock_irqrestore(&vc4->hvs->mm_lock, flags);

	}

	drm_atomic_helper_crtc_destroy_state(crtc, state);
}

void vc4_crtc_reset(struct drm_crtc *crtc)
{
	struct vc4_crtc_state *vc4_crtc_state;

	if (crtc->state)
		vc4_crtc_destroy_state(crtc, crtc->state);

	vc4_crtc_state = kzalloc(sizeof(*vc4_crtc_state), GFP_KERNEL);
	if (!vc4_crtc_state) {
		crtc->state = NULL;
		return;
	}

	vc4_crtc_state->assigned_channel = VC4_HVS_CHANNEL_DISABLED;
	__drm_atomic_helper_crtc_reset(crtc, &vc4_crtc_state->base);
}

int vc4_crtc_late_register(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	const struct vc4_crtc_data *crtc_data = vc4_crtc_to_vc4_crtc_data(vc4_crtc);
	int ret;

	ret = vc4_debugfs_add_regset32(drm->primary, crtc_data->debugfs_name,
				       &vc4_crtc->regset);
	if (ret)
		return ret;

	return 0;
}

static const struct drm_crtc_funcs vc4_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = vc4_page_flip,
	.set_property = NULL,
	.cursor_set = NULL, /* handled by drm_mode_cursor_universal */
	.cursor_move = NULL, /* handled by drm_mode_cursor_universal */
	.reset = vc4_crtc_reset,
	.atomic_duplicate_state = vc4_crtc_duplicate_state,
	.atomic_destroy_state = vc4_crtc_destroy_state,
	.enable_vblank = vc4_enable_vblank,
	.disable_vblank = vc4_disable_vblank,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
	.late_register = vc4_crtc_late_register,
};

static const struct drm_crtc_helper_funcs vc4_crtc_helper_funcs = {
	.mode_valid = vc4_crtc_mode_valid,
	.atomic_check = vc4_crtc_atomic_check,
	.atomic_begin = vc4_hvs_atomic_begin,
	.atomic_flush = vc4_hvs_atomic_flush,
	.atomic_enable = vc4_crtc_atomic_enable,
	.atomic_disable = vc4_crtc_atomic_disable,
	.get_scanout_position = vc4_crtc_get_scanout_position,
};

static const struct vc4_pv_data bcm2835_pv0_data = {
	.base = {
		.debugfs_name = "crtc0_regs",
		.hvs_available_channels = BIT(0),
		.hvs_output = 0,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 1,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_DSI] = VC4_ENCODER_TYPE_DSI0,
		[PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI] = VC4_ENCODER_TYPE_DPI,
	},
};

static const struct vc4_pv_data bcm2835_pv1_data = {
	.base = {
		.debugfs_name = "crtc1_regs",
		.hvs_available_channels = BIT(2),
		.hvs_output = 2,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 1,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_DSI] = VC4_ENCODER_TYPE_DSI1,
		[PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI] = VC4_ENCODER_TYPE_SMI,
	},
};

static const struct vc4_pv_data bcm2835_pv2_data = {
	.base = {
		.debugfs_name = "crtc2_regs",
		.hvs_available_channels = BIT(1),
		.hvs_output = 1,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 1,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI] = VC4_ENCODER_TYPE_HDMI0,
		[PV_CONTROL_CLK_SELECT_VEC] = VC4_ENCODER_TYPE_VEC,
	},
};

static const struct vc4_pv_data bcm2711_pv0_data = {
	.base = {
		.debugfs_name = "crtc0_regs",
		.hvs_available_channels = BIT(0),
		.hvs_output = 0,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 1,
	.encoder_types = {
		[0] = VC4_ENCODER_TYPE_DSI0,
		[1] = VC4_ENCODER_TYPE_DPI,
	},
};

static const struct vc4_pv_data bcm2711_pv1_data = {
	.base = {
		.debugfs_name = "crtc1_regs",
		.hvs_available_channels = BIT(0) | BIT(1) | BIT(2),
		.hvs_output = 3,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 1,
	.encoder_types = {
		[0] = VC4_ENCODER_TYPE_DSI1,
		[1] = VC4_ENCODER_TYPE_SMI,
	},
};

static const struct vc4_pv_data bcm2711_pv2_data = {
	.base = {
		.debugfs_name = "crtc2_regs",
		.hvs_available_channels = BIT(0) | BIT(1) | BIT(2),
		.hvs_output = 4,
	},
	.fifo_depth = 256,
	.pixels_per_clock = 2,
	.encoder_types = {
		[0] = VC4_ENCODER_TYPE_HDMI0,
	},
};

static const struct vc4_pv_data bcm2711_pv3_data = {
	.base = {
		.debugfs_name = "crtc3_regs",
		.hvs_available_channels = BIT(1),
		.hvs_output = 1,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 1,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_VEC] = VC4_ENCODER_TYPE_VEC,
	},
};

static const struct vc4_pv_data bcm2711_pv4_data = {
	.base = {
		.debugfs_name = "crtc4_regs",
		.hvs_available_channels = BIT(0) | BIT(1) | BIT(2),
		.hvs_output = 5,
	},
	.fifo_depth = 64,
	.pixels_per_clock = 2,
	.encoder_types = {
		[0] = VC4_ENCODER_TYPE_HDMI1,
	},
};

static const struct of_device_id vc4_crtc_dt_match[] = {
	{ .compatible = "brcm,bcm2835-pixelvalve0", .data = &bcm2835_pv0_data },
	{ .compatible = "brcm,bcm2835-pixelvalve1", .data = &bcm2835_pv1_data },
	{ .compatible = "brcm,bcm2835-pixelvalve2", .data = &bcm2835_pv2_data },
	{ .compatible = "brcm,bcm2711-pixelvalve0", .data = &bcm2711_pv0_data },
	{ .compatible = "brcm,bcm2711-pixelvalve1", .data = &bcm2711_pv1_data },
	{ .compatible = "brcm,bcm2711-pixelvalve2", .data = &bcm2711_pv2_data },
	{ .compatible = "brcm,bcm2711-pixelvalve3", .data = &bcm2711_pv3_data },
	{ .compatible = "brcm,bcm2711-pixelvalve4", .data = &bcm2711_pv4_data },
	{}
};

static void vc4_set_crtc_possible_masks(struct drm_device *drm,
					struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	const struct vc4_pv_data *pv_data = vc4_crtc_to_vc4_pv_data(vc4_crtc);
	const enum vc4_encoder_type *encoder_types = pv_data->encoder_types;
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, drm) {
		struct vc4_encoder *vc4_encoder;
		int i;

		if (encoder->encoder_type == DRM_MODE_ENCODER_VIRTUAL)
			continue;

		vc4_encoder = to_vc4_encoder(encoder);
		for (i = 0; i < ARRAY_SIZE(pv_data->encoder_types); i++) {
			if (vc4_encoder->type == encoder_types[i]) {
				vc4_encoder->clock_select = i;
				encoder->possible_crtcs |= drm_crtc_mask(crtc);
				break;
			}
		}
	}
}

int vc4_crtc_init(struct drm_device *drm, struct vc4_crtc *vc4_crtc,
		  const struct drm_crtc_funcs *crtc_funcs,
		  const struct drm_crtc_helper_funcs *crtc_helper_funcs)
{
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct drm_crtc *crtc = &vc4_crtc->base;
	struct drm_plane *primary_plane;
	unsigned int i;
	int ret;

	/* For now, we create just the primary and the legacy cursor
	 * planes.  We should be able to stack more planes on easily,
	 * but to do that we would need to compute the bandwidth
	 * requirement of the plane configuration, and reject ones
	 * that will take too much.
	 */
	primary_plane = vc4_plane_init(drm, DRM_PLANE_TYPE_PRIMARY, 0);
	if (IS_ERR(primary_plane)) {
		dev_err(drm->dev, "failed to construct primary plane\n");
		return PTR_ERR(primary_plane);
	}

	spin_lock_init(&vc4_crtc->irq_lock);
	ret = drmm_crtc_init_with_planes(drm, crtc, primary_plane, NULL,
					 crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(crtc, crtc_helper_funcs);

	if (!vc4->is_vc5) {
		drm_mode_crtc_set_gamma_size(crtc, ARRAY_SIZE(vc4_crtc->lut_r));

		drm_crtc_enable_color_mgmt(crtc, 0, false, crtc->gamma_size);

		/* We support CTM, but only for one CRTC at a time. It's therefore
		 * implemented as private driver state in vc4_kms, not here.
		 */
		drm_crtc_enable_color_mgmt(crtc, 0, true, crtc->gamma_size);
	}

	for (i = 0; i < crtc->gamma_size; i++) {
		vc4_crtc->lut_r[i] = i;
		vc4_crtc->lut_g[i] = i;
		vc4_crtc->lut_b[i] = i;
	}

	return 0;
}

static int vc4_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	const struct vc4_pv_data *pv_data;
	struct vc4_crtc *vc4_crtc;
	struct drm_crtc *crtc;
	int ret;

	vc4_crtc = drmm_kzalloc(drm, sizeof(*vc4_crtc), GFP_KERNEL);
	if (!vc4_crtc)
		return -ENOMEM;
	crtc = &vc4_crtc->base;

	pv_data = of_device_get_match_data(dev);
	if (!pv_data)
		return -ENODEV;
	vc4_crtc->data = &pv_data->base;
	vc4_crtc->pdev = pdev;

	vc4_crtc->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(vc4_crtc->regs))
		return PTR_ERR(vc4_crtc->regs);

	vc4_crtc->regset.base = vc4_crtc->regs;
	vc4_crtc->regset.regs = crtc_regs;
	vc4_crtc->regset.nregs = ARRAY_SIZE(crtc_regs);

	ret = vc4_crtc_init(drm, vc4_crtc,
			    &vc4_crtc_funcs, &vc4_crtc_helper_funcs);
	if (ret)
		return ret;
	vc4_set_crtc_possible_masks(drm, crtc);

	CRTC_WRITE(PV_INTEN, 0);
	CRTC_WRITE(PV_INTSTAT, PV_INT_VFP_START);
	ret = devm_request_irq(dev, platform_get_irq(pdev, 0),
			       vc4_crtc_irq_handler,
			       IRQF_SHARED,
			       "vc4 crtc", vc4_crtc);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, vc4_crtc);

	return 0;
}

static void vc4_crtc_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vc4_crtc *vc4_crtc = dev_get_drvdata(dev);

	CRTC_WRITE(PV_INTEN, 0);

	platform_set_drvdata(pdev, NULL);
}

static const struct component_ops vc4_crtc_ops = {
	.bind   = vc4_crtc_bind,
	.unbind = vc4_crtc_unbind,
};

static int vc4_crtc_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_crtc_ops);
}

static int vc4_crtc_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_crtc_ops);
	return 0;
}

struct platform_driver vc4_crtc_driver = {
	.probe = vc4_crtc_dev_probe,
	.remove = vc4_crtc_dev_remove,
	.driver = {
		.name = "vc4_crtc",
		.of_match_table = vc4_crtc_dt_match,
	},
};
