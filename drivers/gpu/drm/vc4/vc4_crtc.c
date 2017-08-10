/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <linux/clk.h>
#include <drm/drm_fb_cma_helper.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include "vc4_drv.h"
#include "vc4_regs.h"

struct vc4_crtc {
	struct drm_crtc base;
	const struct vc4_crtc_data *data;
	void __iomem *regs;

	/* Timestamp at start of vblank irq - unaffected by lock delays. */
	ktime_t t_vblank;

	/* Which HVS channel we're using for our CRTC. */
	int channel;

	u8 lut_r[256];
	u8 lut_g[256];
	u8 lut_b[256];
	/* Size in pixels of the COB memory allocated to this CRTC. */
	u32 cob_size;

	struct drm_pending_vblank_event *event;
};

struct vc4_crtc_state {
	struct drm_crtc_state base;
	/* Dlist area for this CRTC configuration. */
	struct drm_mm_node mm;
};

static inline struct vc4_crtc *
to_vc4_crtc(struct drm_crtc *crtc)
{
	return (struct vc4_crtc *)crtc;
}

static inline struct vc4_crtc_state *
to_vc4_crtc_state(struct drm_crtc_state *crtc_state)
{
	return (struct vc4_crtc_state *)crtc_state;
}

struct vc4_crtc_data {
	/* Which channel of the HVS this pixelvalve sources from. */
	int hvs_channel;

	enum vc4_encoder_type encoder_types[4];
};

#define CRTC_WRITE(offset, val) writel(val, vc4_crtc->regs + (offset))
#define CRTC_READ(offset) readl(vc4_crtc->regs + (offset))

#define CRTC_REG(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} crtc_regs[] = {
	CRTC_REG(PV_CONTROL),
	CRTC_REG(PV_V_CONTROL),
	CRTC_REG(PV_VSYNCD_EVEN),
	CRTC_REG(PV_HORZA),
	CRTC_REG(PV_HORZB),
	CRTC_REG(PV_VERTA),
	CRTC_REG(PV_VERTB),
	CRTC_REG(PV_VERTA_EVEN),
	CRTC_REG(PV_VERTB_EVEN),
	CRTC_REG(PV_INTEN),
	CRTC_REG(PV_INTSTAT),
	CRTC_REG(PV_STAT),
	CRTC_REG(PV_HACT_ACT),
};

static void vc4_crtc_dump_regs(struct vc4_crtc *vc4_crtc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(crtc_regs); i++) {
		DRM_INFO("0x%04x (%s): 0x%08x\n",
			 crtc_regs[i].reg, crtc_regs[i].name,
			 CRTC_READ(crtc_regs[i].reg));
	}
}

#ifdef CONFIG_DEBUG_FS
int vc4_crtc_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	int crtc_index = (uintptr_t)node->info_ent->data;
	struct drm_crtc *crtc;
	struct vc4_crtc *vc4_crtc;
	int i;

	i = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (i == crtc_index)
			break;
		i++;
	}
	if (!crtc)
		return 0;
	vc4_crtc = to_vc4_crtc(crtc);

	for (i = 0; i < ARRAY_SIZE(crtc_regs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   crtc_regs[i].name, crtc_regs[i].reg,
			   CRTC_READ(crtc_regs[i].reg));
	}

	return 0;
}
#endif

bool vc4_crtc_get_scanoutpos(struct drm_device *dev, unsigned int crtc_id,
			     bool in_vblank_irq, int *vpos, int *hpos,
			     ktime_t *stime, ktime_t *etime,
			     const struct drm_display_mode *mode)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_crtc *crtc = drm_crtc_from_index(dev, crtc_id);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
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
	val = HVS_READ(SCALER_DISPSTATX(vc4_crtc->channel));

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
		if (VC4_GET_FIELD(val, SCALER_DISPSTATX_FRAME_COUNT) % 2)
			*hpos += mode->crtc_htotal / 2;
	}

	/* This is the offset we need for translating hvs -> pv scanout pos. */
	fifo_lines = vc4_crtc->cob_size / mode->crtc_hdisplay;

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

static void vc4_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static void
vc4_crtc_lut_load(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	u32 i;

	/* The LUT memory is laid out with each HVS channel in order,
	 * each of which takes 256 writes for R, 256 for G, then 256
	 * for B.
	 */
	HVS_WRITE(SCALER_GAMADDR,
		  SCALER_GAMADDR_AUTOINC |
		  (vc4_crtc->channel * 3 * crtc->gamma_size));

	for (i = 0; i < crtc->gamma_size; i++)
		HVS_WRITE(SCALER_GAMDATA, vc4_crtc->lut_r[i]);
	for (i = 0; i < crtc->gamma_size; i++)
		HVS_WRITE(SCALER_GAMDATA, vc4_crtc->lut_g[i]);
	for (i = 0; i < crtc->gamma_size; i++)
		HVS_WRITE(SCALER_GAMDATA, vc4_crtc->lut_b[i]);
}

static int
vc4_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		   uint32_t size,
		   struct drm_modeset_acquire_ctx *ctx)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	u32 i;

	for (i = 0; i < size; i++) {
		vc4_crtc->lut_r[i] = r[i] >> 8;
		vc4_crtc->lut_g[i] = g[i] >> 8;
		vc4_crtc->lut_b[i] = b[i] >> 8;
	}

	vc4_crtc_lut_load(crtc);

	return 0;
}

static u32 vc4_get_fifo_full_level(u32 format)
{
	static const u32 fifo_len_bytes = 64;
	static const u32 hvs_latency_pix = 6;

	switch (format) {
	case PV_CONTROL_FORMAT_DSIV_16:
	case PV_CONTROL_FORMAT_DSIC_16:
		return fifo_len_bytes - 2 * hvs_latency_pix;
	case PV_CONTROL_FORMAT_DSIV_18:
		return fifo_len_bytes - 14;
	case PV_CONTROL_FORMAT_24:
	case PV_CONTROL_FORMAT_DSIV_24:
	default:
		return fifo_len_bytes - 3 * hvs_latency_pix;
	}
}

/*
 * Returns the encoder attached to the CRTC.
 *
 * VC4 can only scan out to one encoder at a time, while the DRM core
 * allows drivers to push pixels to more than one encoder from the
 * same CRTC.
 */
static struct drm_encoder *vc4_get_crtc_encoder(struct drm_crtc *crtc)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(crtc->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->state->crtc == crtc) {
			drm_connector_list_iter_end(&conn_iter);
			return connector->encoder;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return NULL;
}

static void vc4_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_encoder *encoder = vc4_get_crtc_encoder(crtc);
	struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_crtc_state *state = crtc->state;
	struct drm_display_mode *mode = &state->adjusted_mode;
	bool interlace = mode->flags & DRM_MODE_FLAG_INTERLACE;
	u32 pixel_rep = (mode->flags & DRM_MODE_FLAG_DBLCLK) ? 2 : 1;
	bool is_dsi = (vc4_encoder->type == VC4_ENCODER_TYPE_DSI0 ||
		       vc4_encoder->type == VC4_ENCODER_TYPE_DSI1);
	u32 format = is_dsi ? PV_CONTROL_FORMAT_DSIV_24 : PV_CONTROL_FORMAT_24;
	bool debug_dump_regs = false;

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d regs before:\n", drm_crtc_index(crtc));
		vc4_crtc_dump_regs(vc4_crtc);
	}

	/* Reset the PV fifo. */
	CRTC_WRITE(PV_CONTROL, 0);
	CRTC_WRITE(PV_CONTROL, PV_CONTROL_FIFO_CLR | PV_CONTROL_EN);
	CRTC_WRITE(PV_CONTROL, 0);

	CRTC_WRITE(PV_HORZA,
		   VC4_SET_FIELD((mode->htotal -
				  mode->hsync_end) * pixel_rep,
				 PV_HORZA_HBP) |
		   VC4_SET_FIELD((mode->hsync_end -
				  mode->hsync_start) * pixel_rep,
				 PV_HORZA_HSYNC));
	CRTC_WRITE(PV_HORZB,
		   VC4_SET_FIELD((mode->hsync_start -
				  mode->hdisplay) * pixel_rep,
				 PV_HORZB_HFP) |
		   VC4_SET_FIELD(mode->hdisplay * pixel_rep, PV_HORZB_HACTIVE));

	CRTC_WRITE(PV_VERTA,
		   VC4_SET_FIELD(mode->crtc_vtotal - mode->crtc_vsync_end,
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
					 mode->crtc_vsync_end - 1,
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
			   VC4_SET_FIELD(mode->htotal * pixel_rep / 2,
					 PV_VCONTROL_ODD_DELAY));
		CRTC_WRITE(PV_VSYNCD_EVEN, 0);
	} else {
		CRTC_WRITE(PV_V_CONTROL,
			   PV_VCONTROL_CONTINUOUS |
			   (is_dsi ? PV_VCONTROL_DSI : 0));
	}

	CRTC_WRITE(PV_HACT_ACT, mode->hdisplay * pixel_rep);

	CRTC_WRITE(PV_CONTROL,
		   VC4_SET_FIELD(format, PV_CONTROL_FORMAT) |
		   VC4_SET_FIELD(vc4_get_fifo_full_level(format),
				 PV_CONTROL_FIFO_LEVEL) |
		   VC4_SET_FIELD(pixel_rep - 1, PV_CONTROL_PIXEL_REP) |
		   PV_CONTROL_CLR_AT_START |
		   PV_CONTROL_TRIGGER_UNDERFLOW |
		   PV_CONTROL_WAIT_HSTART |
		   VC4_SET_FIELD(vc4_encoder->clock_select,
				 PV_CONTROL_CLK_SELECT) |
		   PV_CONTROL_FIFO_CLR |
		   PV_CONTROL_EN);

	HVS_WRITE(SCALER_DISPBKGNDX(vc4_crtc->channel),
		  SCALER_DISPBKGND_AUTOHS |
		  SCALER_DISPBKGND_GAMMA |
		  (interlace ? SCALER_DISPBKGND_INTERLACE : 0));

	/* Reload the LUT, since the SRAMs would have been disabled if
	 * all CRTCs had SCALER_DISPBKGND_GAMMA unset at once.
	 */
	vc4_crtc_lut_load(crtc);

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d regs after:\n", drm_crtc_index(crtc));
		vc4_crtc_dump_regs(vc4_crtc);
	}
}

static void require_hvs_enabled(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	WARN_ON_ONCE((HVS_READ(SCALER_DISPCTRL) & SCALER_DISPCTRL_ENABLE) !=
		     SCALER_DISPCTRL_ENABLE);
}

static void vc4_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	u32 chan = vc4_crtc->channel;
	int ret;
	require_hvs_enabled(dev);

	/* Disable vblank irq handling before crtc is disabled. */
	drm_crtc_vblank_off(crtc);

	CRTC_WRITE(PV_V_CONTROL,
		   CRTC_READ(PV_V_CONTROL) & ~PV_VCONTROL_VIDEN);
	ret = wait_for(!(CRTC_READ(PV_V_CONTROL) & PV_VCONTROL_VIDEN), 1);
	WARN_ONCE(ret, "Timeout waiting for !PV_VCONTROL_VIDEN\n");

	if (HVS_READ(SCALER_DISPCTRLX(chan)) &
	    SCALER_DISPCTRLX_ENABLE) {
		HVS_WRITE(SCALER_DISPCTRLX(chan),
			  SCALER_DISPCTRLX_RESET);

		/* While the docs say that reset is self-clearing, it
		 * seems it doesn't actually.
		 */
		HVS_WRITE(SCALER_DISPCTRLX(chan), 0);
	}

	/* Once we leave, the scaler should be disabled and its fifo empty. */

	WARN_ON_ONCE(HVS_READ(SCALER_DISPCTRLX(chan)) & SCALER_DISPCTRLX_RESET);

	WARN_ON_ONCE(VC4_GET_FIELD(HVS_READ(SCALER_DISPSTATX(chan)),
				   SCALER_DISPSTATX_MODE) !=
		     SCALER_DISPSTATX_MODE_DISABLED);

	WARN_ON_ONCE((HVS_READ(SCALER_DISPSTATX(chan)) &
		      (SCALER_DISPSTATX_FULL | SCALER_DISPSTATX_EMPTY)) !=
		     SCALER_DISPSTATX_EMPTY);
}

static void vc4_crtc_update_dlist(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(crtc->state);

	if (crtc->state->event) {
		unsigned long flags;

		crtc->state->event->pipe = drm_crtc_index(crtc);

		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		vc4_crtc->event = crtc->state->event;
		crtc->state->event = NULL;

		HVS_WRITE(SCALER_DISPLISTX(vc4_crtc->channel),
			  vc4_state->mm.start);

		spin_unlock_irqrestore(&dev->event_lock, flags);
	} else {
		HVS_WRITE(SCALER_DISPLISTX(vc4_crtc->channel),
			  vc4_state->mm.start);
	}
}

static void vc4_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_crtc_state *state = crtc->state;
	struct drm_display_mode *mode = &state->adjusted_mode;

	require_hvs_enabled(dev);

	/* Enable vblank irq handling before crtc is started otherwise
	 * drm_crtc_get_vblank() fails in vc4_crtc_update_dlist().
	 */
	drm_crtc_vblank_on(crtc);
	vc4_crtc_update_dlist(crtc);

	/* Turn on the scaler, which will wait for vstart to start
	 * compositing.
	 */
	HVS_WRITE(SCALER_DISPCTRLX(vc4_crtc->channel),
		  VC4_SET_FIELD(mode->hdisplay, SCALER_DISPCTRLX_WIDTH) |
		  VC4_SET_FIELD(mode->vdisplay, SCALER_DISPCTRLX_HEIGHT) |
		  SCALER_DISPCTRLX_ENABLE);

	/* Turn on the pixel valve, which will emit the vstart signal. */
	CRTC_WRITE(PV_V_CONTROL,
		   CRTC_READ(PV_V_CONTROL) | PV_VCONTROL_VIDEN);
}

static bool vc4_crtc_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	/* Do not allow doublescan modes from user space */
	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		DRM_DEBUG_KMS("[CRTC:%d] Doublescan mode rejected.\n",
			      crtc->base.id);
		return false;
	}

	return true;
}

static int vc4_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_crtc_state *state)
{
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(state);
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_plane *plane;
	unsigned long flags;
	const struct drm_plane_state *plane_state;
	u32 dlist_count = 0;
	int ret;

	/* The pixelvalve can only feed one encoder (and encoders are
	 * 1:1 with connectors.)
	 */
	if (hweight32(state->connector_mask) > 1)
		return -EINVAL;

	drm_atomic_crtc_state_for_each_plane_state(plane, plane_state, state)
		dlist_count += vc4_plane_dlist_size(plane_state);

	dlist_count++; /* Account for SCALER_CTL0_END. */

	spin_lock_irqsave(&vc4->hvs->mm_lock, flags);
	ret = drm_mm_insert_node(&vc4->hvs->dlist_mm, &vc4_state->mm,
				 dlist_count);
	spin_unlock_irqrestore(&vc4->hvs->mm_lock, flags);
	if (ret)
		return ret;

	return 0;
}

static void vc4_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(crtc->state);
	struct drm_plane *plane;
	bool debug_dump_regs = false;
	u32 __iomem *dlist_start = vc4->hvs->dlist + vc4_state->mm.start;
	u32 __iomem *dlist_next = dlist_start;

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d HVS before:\n", drm_crtc_index(crtc));
		vc4_hvs_dump_state(dev);
	}

	/* Copy all the active planes' dlist contents to the hardware dlist. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		dlist_next += vc4_plane_write_dlist(plane, dlist_next);
	}

	writel(SCALER_CTL0_END, dlist_next);
	dlist_next++;

	WARN_ON_ONCE(dlist_next - dlist_start != vc4_state->mm.size);

	/* Only update DISPLIST if the CRTC was already running and is not
	 * being disabled.
	 * vc4_crtc_enable() takes care of updating the dlist just after
	 * re-enabling VBLANK interrupts and before enabling the engine.
	 * If the CRTC is being disabled, there's no point in updating this
	 * information.
	 */
	if (crtc->state->active && old_state->active)
		vc4_crtc_update_dlist(crtc);

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d HVS after:\n", drm_crtc_index(crtc));
		vc4_hvs_dump_state(dev);
	}
}

static int vc4_enable_vblank(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);

	CRTC_WRITE(PV_INTEN, PV_INT_VFP_START);

	return 0;
}

static void vc4_disable_vblank(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);

	CRTC_WRITE(PV_INTEN, 0);
}

/* Must be called with the event lock held */
bool vc4_event_pending(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);

	return !!vc4_crtc->event;
}

static void vc4_crtc_handle_page_flip(struct vc4_crtc *vc4_crtc)
{
	struct drm_crtc *crtc = &vc4_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(crtc->state);
	u32 chan = vc4_crtc->channel;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (vc4_crtc->event &&
	    (vc4_state->mm.start == HVS_READ(SCALER_DISPLACTX(chan)))) {
		drm_crtc_send_vblank_event(crtc, vc4_crtc->event);
		vc4_crtc->event = NULL;
		drm_crtc_vblank_put(crtc);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static irqreturn_t vc4_crtc_irq_handler(int irq, void *data)
{
	struct vc4_crtc *vc4_crtc = data;
	u32 stat = CRTC_READ(PV_INTSTAT);
	irqreturn_t ret = IRQ_NONE;

	if (stat & PV_INT_VFP_START) {
		vc4_crtc->t_vblank = ktime_get();
		CRTC_WRITE(PV_INTSTAT, PV_INT_VFP_START);
		drm_crtc_handle_vblank(&vc4_crtc->base);
		vc4_crtc_handle_page_flip(vc4_crtc);
		ret = IRQ_HANDLED;
	}

	return ret;
}

struct vc4_async_flip_state {
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;
	struct drm_pending_vblank_event *event;

	struct vc4_seqno_cb cb;
};

/* Called when the V3D execution for the BO being flipped to is done, so that
 * we can actually update the plane's address to point to it.
 */
static void
vc4_async_page_flip_complete(struct vc4_seqno_cb *cb)
{
	struct vc4_async_flip_state *flip_state =
		container_of(cb, struct vc4_async_flip_state, cb);
	struct drm_crtc *crtc = flip_state->crtc;
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_plane *plane = crtc->primary;

	vc4_plane_async_set_fb(plane, flip_state->fb);
	if (flip_state->event) {
		unsigned long flags;

		spin_lock_irqsave(&dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, flip_state->event);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	drm_crtc_vblank_put(crtc);
	drm_framebuffer_unreference(flip_state->fb);
	kfree(flip_state);

	up(&vc4->async_modeset);
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
	struct drm_plane *plane = crtc->primary;
	int ret = 0;
	struct vc4_async_flip_state *flip_state;
	struct drm_gem_cma_object *cma_bo = drm_fb_cma_get_gem_obj(fb, 0);
	struct vc4_bo *bo = to_vc4_bo(&cma_bo->base);

	flip_state = kzalloc(sizeof(*flip_state), GFP_KERNEL);
	if (!flip_state)
		return -ENOMEM;

	drm_framebuffer_reference(fb);
	flip_state->fb = fb;
	flip_state->crtc = crtc;
	flip_state->event = event;

	/* Make sure all other async modesetes have landed. */
	ret = down_interruptible(&vc4->async_modeset);
	if (ret) {
		drm_framebuffer_unreference(fb);
		kfree(flip_state);
		return ret;
	}

	WARN_ON(drm_crtc_vblank_get(crtc) != 0);

	/* Immediately update the plane's legacy fb pointer, so that later
	 * modeset prep sees the state that will be present when the semaphore
	 * is released.
	 */
	drm_atomic_set_fb_for_plane(plane->state, fb);
	plane->fb = fb;

	vc4_queue_seqno_cb(dev, &flip_state->cb, bo->seqno,
			   vc4_async_page_flip_complete);

	/* Driver takes ownership of state on successful async commit. */
	return 0;
}

static int vc4_page_flip(struct drm_crtc *crtc,
			 struct drm_framebuffer *fb,
			 struct drm_pending_vblank_event *event,
			 uint32_t flags,
			 struct drm_modeset_acquire_ctx *ctx)
{
	if (flags & DRM_MODE_PAGE_FLIP_ASYNC)
		return vc4_async_page_flip(crtc, fb, event, flags);
	else
		return drm_atomic_helper_page_flip(crtc, fb, event, flags, ctx);
}

static struct drm_crtc_state *vc4_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct vc4_crtc_state *vc4_state;

	vc4_state = kzalloc(sizeof(*vc4_state), GFP_KERNEL);
	if (!vc4_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &vc4_state->base);
	return &vc4_state->base;
}

static void vc4_crtc_destroy_state(struct drm_crtc *crtc,
				   struct drm_crtc_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(crtc->dev);
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(state);

	if (vc4_state->mm.allocated) {
		unsigned long flags;

		spin_lock_irqsave(&vc4->hvs->mm_lock, flags);
		drm_mm_remove_node(&vc4_state->mm);
		spin_unlock_irqrestore(&vc4->hvs->mm_lock, flags);

	}

	drm_atomic_helper_crtc_destroy_state(crtc, state);
}

static void
vc4_crtc_reset(struct drm_crtc *crtc)
{
	if (crtc->state)
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

	crtc->state = kzalloc(sizeof(struct vc4_crtc_state), GFP_KERNEL);
	if (crtc->state)
		crtc->state->crtc = crtc;
}

static const struct drm_crtc_funcs vc4_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = vc4_crtc_destroy,
	.page_flip = vc4_page_flip,
	.set_property = NULL,
	.cursor_set = NULL, /* handled by drm_mode_cursor_universal */
	.cursor_move = NULL, /* handled by drm_mode_cursor_universal */
	.reset = vc4_crtc_reset,
	.atomic_duplicate_state = vc4_crtc_duplicate_state,
	.atomic_destroy_state = vc4_crtc_destroy_state,
	.gamma_set = vc4_crtc_gamma_set,
	.enable_vblank = vc4_enable_vblank,
	.disable_vblank = vc4_disable_vblank,
};

static const struct drm_crtc_helper_funcs vc4_crtc_helper_funcs = {
	.mode_set_nofb = vc4_crtc_mode_set_nofb,
	.disable = vc4_crtc_disable,
	.enable = vc4_crtc_enable,
	.mode_fixup = vc4_crtc_mode_fixup,
	.atomic_check = vc4_crtc_atomic_check,
	.atomic_flush = vc4_crtc_atomic_flush,
};

static const struct vc4_crtc_data pv0_data = {
	.hvs_channel = 0,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_DSI] = VC4_ENCODER_TYPE_DSI0,
		[PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI] = VC4_ENCODER_TYPE_DPI,
	},
};

static const struct vc4_crtc_data pv1_data = {
	.hvs_channel = 2,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_DSI] = VC4_ENCODER_TYPE_DSI1,
		[PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI] = VC4_ENCODER_TYPE_SMI,
	},
};

static const struct vc4_crtc_data pv2_data = {
	.hvs_channel = 1,
	.encoder_types = {
		[PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI] = VC4_ENCODER_TYPE_HDMI,
		[PV_CONTROL_CLK_SELECT_VEC] = VC4_ENCODER_TYPE_VEC,
	},
};

static const struct of_device_id vc4_crtc_dt_match[] = {
	{ .compatible = "brcm,bcm2835-pixelvalve0", .data = &pv0_data },
	{ .compatible = "brcm,bcm2835-pixelvalve1", .data = &pv1_data },
	{ .compatible = "brcm,bcm2835-pixelvalve2", .data = &pv2_data },
	{}
};

static void vc4_set_crtc_possible_masks(struct drm_device *drm,
					struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	const struct vc4_crtc_data *crtc_data = vc4_crtc->data;
	const enum vc4_encoder_type *encoder_types = crtc_data->encoder_types;
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, drm) {
		struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);
		int i;

		for (i = 0; i < ARRAY_SIZE(crtc_data->encoder_types); i++) {
			if (vc4_encoder->type == encoder_types[i]) {
				vc4_encoder->clock_select = i;
				encoder->possible_crtcs |= drm_crtc_mask(crtc);
				break;
			}
		}
	}
}

static void
vc4_crtc_get_cob_allocation(struct vc4_crtc *vc4_crtc)
{
	struct drm_device *drm = vc4_crtc->base.dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	u32 dispbase = HVS_READ(SCALER_DISPBASEX(vc4_crtc->channel));
	/* Top/base are supposed to be 4-pixel aligned, but the
	 * Raspberry Pi firmware fills the low bits (which are
	 * presumably ignored).
	 */
	u32 top = VC4_GET_FIELD(dispbase, SCALER_DISPBASEX_TOP) & ~3;
	u32 base = VC4_GET_FIELD(dispbase, SCALER_DISPBASEX_BASE) & ~3;

	vc4_crtc->cob_size = top - base + 4;
}

static int vc4_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_crtc *vc4_crtc;
	struct drm_crtc *crtc;
	struct drm_plane *primary_plane, *cursor_plane, *destroy_plane, *temp;
	const struct of_device_id *match;
	int ret, i;

	vc4_crtc = devm_kzalloc(dev, sizeof(*vc4_crtc), GFP_KERNEL);
	if (!vc4_crtc)
		return -ENOMEM;
	crtc = &vc4_crtc->base;

	match = of_match_device(vc4_crtc_dt_match, dev);
	if (!match)
		return -ENODEV;
	vc4_crtc->data = match->data;

	vc4_crtc->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(vc4_crtc->regs))
		return PTR_ERR(vc4_crtc->regs);

	/* For now, we create just the primary and the legacy cursor
	 * planes.  We should be able to stack more planes on easily,
	 * but to do that we would need to compute the bandwidth
	 * requirement of the plane configuration, and reject ones
	 * that will take too much.
	 */
	primary_plane = vc4_plane_init(drm, DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(primary_plane)) {
		dev_err(dev, "failed to construct primary plane\n");
		ret = PTR_ERR(primary_plane);
		goto err;
	}

	drm_crtc_init_with_planes(drm, crtc, primary_plane, NULL,
				  &vc4_crtc_funcs, NULL);
	drm_crtc_helper_add(crtc, &vc4_crtc_helper_funcs);
	primary_plane->crtc = crtc;
	vc4_crtc->channel = vc4_crtc->data->hvs_channel;
	drm_mode_crtc_set_gamma_size(crtc, ARRAY_SIZE(vc4_crtc->lut_r));

	/* Set up some arbitrary number of planes.  We're not limited
	 * by a set number of physical registers, just the space in
	 * the HVS (16k) and how small an plane can be (28 bytes).
	 * However, each plane we set up takes up some memory, and
	 * increases the cost of looping over planes, which atomic
	 * modesetting does quite a bit.  As a result, we pick a
	 * modest number of planes to expose, that should hopefully
	 * still cover any sane usecase.
	 */
	for (i = 0; i < 8; i++) {
		struct drm_plane *plane =
			vc4_plane_init(drm, DRM_PLANE_TYPE_OVERLAY);

		if (IS_ERR(plane))
			continue;

		plane->possible_crtcs = 1 << drm_crtc_index(crtc);
	}

	/* Set up the legacy cursor after overlay initialization,
	 * since we overlay planes on the CRTC in the order they were
	 * initialized.
	 */
	cursor_plane = vc4_plane_init(drm, DRM_PLANE_TYPE_CURSOR);
	if (!IS_ERR(cursor_plane)) {
		cursor_plane->possible_crtcs = 1 << drm_crtc_index(crtc);
		cursor_plane->crtc = crtc;
		crtc->cursor = cursor_plane;
	}

	vc4_crtc_get_cob_allocation(vc4_crtc);

	CRTC_WRITE(PV_INTEN, 0);
	CRTC_WRITE(PV_INTSTAT, PV_INT_VFP_START);
	ret = devm_request_irq(dev, platform_get_irq(pdev, 0),
			       vc4_crtc_irq_handler, 0, "vc4 crtc", vc4_crtc);
	if (ret)
		goto err_destroy_planes;

	vc4_set_crtc_possible_masks(drm, crtc);

	for (i = 0; i < crtc->gamma_size; i++) {
		vc4_crtc->lut_r[i] = i;
		vc4_crtc->lut_g[i] = i;
		vc4_crtc->lut_b[i] = i;
	}

	platform_set_drvdata(pdev, vc4_crtc);

	return 0;

err_destroy_planes:
	list_for_each_entry_safe(destroy_plane, temp,
				 &drm->mode_config.plane_list, head) {
		if (destroy_plane->possible_crtcs == 1 << drm_crtc_index(crtc))
		    destroy_plane->funcs->destroy(destroy_plane);
	}
err:
	return ret;
}

static void vc4_crtc_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vc4_crtc *vc4_crtc = dev_get_drvdata(dev);

	vc4_crtc_destroy(&vc4_crtc->base);

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
