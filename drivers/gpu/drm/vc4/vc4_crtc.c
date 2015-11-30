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
 * output's clock plus its configuration.  It pulls scaled pixels from
 * the HVS at that timing, and feeds it to the encoder.
 *
 * However, the DRM CRTC also collects the configuration of all the
 * DRM planes attached to it.  As a result, this file also manages
 * setup of the VC4 HVS's display elements on the CRTC.
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

#include "drm_atomic.h"
#include "drm_atomic_helper.h"
#include "drm_crtc_helper.h"
#include "linux/clk.h"
#include "drm_fb_cma_helper.h"
#include "linux/component.h"
#include "linux/of_device.h"
#include "vc4_drv.h"
#include "vc4_regs.h"

struct vc4_crtc {
	struct drm_crtc base;
	const struct vc4_crtc_data *data;
	void __iomem *regs;

	/* Which HVS channel we're using for our CRTC. */
	int channel;

	/* Pointer to the actual hardware display list memory for the
	 * crtc.
	 */
	u32 __iomem *dlist;

	u32 dlist_size; /* in dwords */

	struct drm_pending_vblank_event *event;
};

static inline struct vc4_crtc *
to_vc4_crtc(struct drm_crtc *crtc)
{
	return (struct vc4_crtc *)crtc;
}

struct vc4_crtc_data {
	/* Which channel of the HVS this pixelvalve sources from. */
	int hvs_channel;

	enum vc4_encoder_type encoder0_type;
	enum vc4_encoder_type encoder1_type;
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
	CRTC_REG(PV_VSYNCD),
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

static void vc4_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
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
 * Returns the clock select bit for the connector attached to the
 * CRTC.
 */
static int vc4_get_clock_select(struct drm_crtc *crtc)
{
	struct drm_connector *connector;

	drm_for_each_connector(connector, crtc->dev) {
		if (connector && connector->state->crtc == crtc) {
			struct drm_encoder *encoder = connector->encoder;
			struct vc4_encoder *vc4_encoder =
				to_vc4_encoder(encoder);

			return vc4_encoder->clock_select;
		}
	}

	return -1;
}

static void vc4_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_crtc_state *state = crtc->state;
	struct drm_display_mode *mode = &state->adjusted_mode;
	bool interlace = mode->flags & DRM_MODE_FLAG_INTERLACE;
	u32 vactive = (mode->vdisplay >> (interlace ? 1 : 0));
	u32 format = PV_CONTROL_FORMAT_24;
	bool debug_dump_regs = false;
	int clock_select = vc4_get_clock_select(crtc);

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d regs before:\n", drm_crtc_index(crtc));
		vc4_crtc_dump_regs(vc4_crtc);
	}

	/* Reset the PV fifo. */
	CRTC_WRITE(PV_CONTROL, 0);
	CRTC_WRITE(PV_CONTROL, PV_CONTROL_FIFO_CLR | PV_CONTROL_EN);
	CRTC_WRITE(PV_CONTROL, 0);

	CRTC_WRITE(PV_HORZA,
		   VC4_SET_FIELD(mode->htotal - mode->hsync_end,
				 PV_HORZA_HBP) |
		   VC4_SET_FIELD(mode->hsync_end - mode->hsync_start,
				 PV_HORZA_HSYNC));
	CRTC_WRITE(PV_HORZB,
		   VC4_SET_FIELD(mode->hsync_start - mode->hdisplay,
				 PV_HORZB_HFP) |
		   VC4_SET_FIELD(mode->hdisplay, PV_HORZB_HACTIVE));

	if (interlace) {
		CRTC_WRITE(PV_VERTA_EVEN,
			   VC4_SET_FIELD(mode->vtotal - mode->vsync_end - 1,
					 PV_VERTA_VBP) |
			   VC4_SET_FIELD(mode->vsync_end - mode->vsync_start,
					 PV_VERTA_VSYNC));
		CRTC_WRITE(PV_VERTB_EVEN,
			   VC4_SET_FIELD(mode->vsync_start - mode->vdisplay,
					 PV_VERTB_VFP) |
			   VC4_SET_FIELD(vactive, PV_VERTB_VACTIVE));
	}

	CRTC_WRITE(PV_HACT_ACT, mode->hdisplay);

	CRTC_WRITE(PV_V_CONTROL,
		   PV_VCONTROL_CONTINUOUS |
		   (interlace ? PV_VCONTROL_INTERLACE : 0));

	CRTC_WRITE(PV_CONTROL,
		   VC4_SET_FIELD(format, PV_CONTROL_FORMAT) |
		   VC4_SET_FIELD(vc4_get_fifo_full_level(format),
				 PV_CONTROL_FIFO_LEVEL) |
		   PV_CONTROL_CLR_AT_START |
		   PV_CONTROL_TRIGGER_UNDERFLOW |
		   PV_CONTROL_WAIT_HSTART |
		   VC4_SET_FIELD(clock_select, PV_CONTROL_CLK_SELECT) |
		   PV_CONTROL_FIFO_CLR |
		   PV_CONTROL_EN);

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

static void vc4_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_crtc_state *state = crtc->state;
	struct drm_display_mode *mode = &state->adjusted_mode;

	require_hvs_enabled(dev);

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

static int vc4_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_crtc_state *state)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_plane *plane;
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	u32 dlist_count = 0;

	/* The pixelvalve can only feed one encoder (and encoders are
	 * 1:1 with connectors.)
	 */
	if (drm_atomic_connectors_for_crtc(state->state, crtc) > 1)
		return -EINVAL;

	drm_atomic_crtc_state_for_each_plane(plane, state) {
		struct drm_plane_state *plane_state =
			state->state->plane_states[drm_plane_index(plane)];

		/* plane might not have changed, in which case take
		 * current state:
		 */
		if (!plane_state)
			plane_state = plane->state;

		dlist_count += vc4_plane_dlist_size(plane_state);
	}

	dlist_count++; /* Account for SCALER_CTL0_END. */

	if (!vc4_crtc->dlist || dlist_count > vc4_crtc->dlist_size) {
		vc4_crtc->dlist = ((u32 __iomem *)vc4->hvs->dlist +
				   HVS_BOOTLOADER_DLIST_END);
		vc4_crtc->dlist_size = ((SCALER_DLIST_SIZE >> 2) -
					HVS_BOOTLOADER_DLIST_END);

		if (dlist_count > vc4_crtc->dlist_size) {
			DRM_DEBUG_KMS("dlist too large for CRTC (%d > %d).\n",
				      dlist_count, vc4_crtc->dlist_size);
			return -EINVAL;
		}
	}

	return 0;
}

static void vc4_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_plane *plane;
	bool debug_dump_regs = false;
	u32 __iomem *dlist_next = vc4_crtc->dlist;

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d HVS before:\n", drm_crtc_index(crtc));
		vc4_hvs_dump_state(dev);
	}

	/* Copy all the active planes' dlist contents to the hardware dlist.
	 *
	 * XXX: If the new display list was large enough that it
	 * overlapped a currently-read display list, we need to do
	 * something like disable scanout before putting in the new
	 * list.  For now, we're safe because we only have the two
	 * planes.
	 */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		dlist_next += vc4_plane_write_dlist(plane, dlist_next);
	}

	if (dlist_next == vc4_crtc->dlist) {
		/* If no planes were enabled, use the SCALER_CTL0_END
		 * at the start of the display list memory (in the
		 * bootloader section).  We'll rewrite that
		 * SCALER_CTL0_END, just in case, though.
		 */
		writel(SCALER_CTL0_END, vc4->hvs->dlist);
		HVS_WRITE(SCALER_DISPLISTX(vc4_crtc->channel), 0);
	} else {
		writel(SCALER_CTL0_END, dlist_next);
		dlist_next++;

		HVS_WRITE(SCALER_DISPLISTX(vc4_crtc->channel),
			  (u32 *)vc4_crtc->dlist - (u32 *)vc4->hvs->dlist);

		/* Make the next display list start after ours. */
		vc4_crtc->dlist_size -= (dlist_next - vc4_crtc->dlist);
		vc4_crtc->dlist = dlist_next;
	}

	if (debug_dump_regs) {
		DRM_INFO("CRTC %d HVS after:\n", drm_crtc_index(crtc));
		vc4_hvs_dump_state(dev);
	}

	if (crtc->state->event) {
		unsigned long flags;

		crtc->state->event->pipe = drm_crtc_index(crtc);

		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		vc4_crtc->event = crtc->state->event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
		crtc->state->event = NULL;
	}
}

int vc4_enable_vblank(struct drm_device *dev, unsigned int crtc_id)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = vc4->crtc[crtc_id];

	CRTC_WRITE(PV_INTEN, PV_INT_VFP_START);

	return 0;
}

void vc4_disable_vblank(struct drm_device *dev, unsigned int crtc_id)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_crtc *vc4_crtc = vc4->crtc[crtc_id];

	CRTC_WRITE(PV_INTEN, 0);
}

static void vc4_crtc_handle_page_flip(struct vc4_crtc *vc4_crtc)
{
	struct drm_crtc *crtc = &vc4_crtc->base;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (vc4_crtc->event) {
		drm_crtc_send_vblank_event(crtc, vc4_crtc->event);
		vc4_crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static irqreturn_t vc4_crtc_irq_handler(int irq, void *data)
{
	struct vc4_crtc *vc4_crtc = data;
	u32 stat = CRTC_READ(PV_INTSTAT);
	irqreturn_t ret = IRQ_NONE;

	if (stat & PV_INT_VFP_START) {
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
		kfree(flip_state);
		return ret;
	}

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
			 uint32_t flags)
{
	if (flags & DRM_MODE_PAGE_FLIP_ASYNC)
		return vc4_async_page_flip(crtc, fb, event, flags);
	else
		return drm_atomic_helper_page_flip(crtc, fb, event, flags);
}

static const struct drm_crtc_funcs vc4_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = vc4_crtc_destroy,
	.page_flip = vc4_page_flip,
	.set_property = NULL,
	.cursor_set = NULL, /* handled by drm_mode_cursor_universal */
	.cursor_move = NULL, /* handled by drm_mode_cursor_universal */
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs vc4_crtc_helper_funcs = {
	.mode_set_nofb = vc4_crtc_mode_set_nofb,
	.disable = vc4_crtc_disable,
	.enable = vc4_crtc_enable,
	.atomic_check = vc4_crtc_atomic_check,
	.atomic_flush = vc4_crtc_atomic_flush,
};

/* Frees the page flip event when the DRM device is closed with the
 * event still outstanding.
 */
void vc4_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (vc4_crtc->event && vc4_crtc->event->base.file_priv == file) {
		vc4_crtc->event->base.destroy(&vc4_crtc->event->base);
		drm_crtc_vblank_put(crtc);
		vc4_crtc->event = NULL;
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static const struct vc4_crtc_data pv0_data = {
	.hvs_channel = 0,
	.encoder0_type = VC4_ENCODER_TYPE_DSI0,
	.encoder1_type = VC4_ENCODER_TYPE_DPI,
};

static const struct vc4_crtc_data pv1_data = {
	.hvs_channel = 2,
	.encoder0_type = VC4_ENCODER_TYPE_DSI1,
	.encoder1_type = VC4_ENCODER_TYPE_SMI,
};

static const struct vc4_crtc_data pv2_data = {
	.hvs_channel = 1,
	.encoder0_type = VC4_ENCODER_TYPE_VEC,
	.encoder1_type = VC4_ENCODER_TYPE_HDMI,
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
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, drm) {
		struct vc4_encoder *vc4_encoder = to_vc4_encoder(encoder);

		if (vc4_encoder->type == vc4_crtc->data->encoder0_type) {
			vc4_encoder->clock_select = 0;
			encoder->possible_crtcs |= drm_crtc_mask(crtc);
		} else if (vc4_encoder->type == vc4_crtc->data->encoder1_type) {
			vc4_encoder->clock_select = 1;
			encoder->possible_crtcs |= drm_crtc_mask(crtc);
		}
	}
}

static int vc4_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_crtc *vc4_crtc;
	struct drm_crtc *crtc;
	struct drm_plane *primary_plane, *cursor_plane;
	const struct of_device_id *match;
	int ret;

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
	if (!primary_plane) {
		dev_err(dev, "failed to construct primary plane\n");
		ret = PTR_ERR(primary_plane);
		goto err;
	}

	cursor_plane = vc4_plane_init(drm, DRM_PLANE_TYPE_CURSOR);
	if (!cursor_plane) {
		dev_err(dev, "failed to construct cursor plane\n");
		ret = PTR_ERR(cursor_plane);
		goto err_primary;
	}

	drm_crtc_init_with_planes(drm, crtc, primary_plane, cursor_plane,
				  &vc4_crtc_funcs);
	drm_crtc_helper_add(crtc, &vc4_crtc_helper_funcs);
	primary_plane->crtc = crtc;
	cursor_plane->crtc = crtc;
	vc4->crtc[drm_crtc_index(crtc)] = vc4_crtc;
	vc4_crtc->channel = vc4_crtc->data->hvs_channel;

	CRTC_WRITE(PV_INTEN, 0);
	CRTC_WRITE(PV_INTSTAT, PV_INT_VFP_START);
	ret = devm_request_irq(dev, platform_get_irq(pdev, 0),
			       vc4_crtc_irq_handler, 0, "vc4 crtc", vc4_crtc);
	if (ret)
		goto err_cursor;

	vc4_set_crtc_possible_masks(drm, crtc);

	platform_set_drvdata(pdev, vc4_crtc);

	return 0;

err_cursor:
	cursor_plane->funcs->destroy(cursor_plane);
err_primary:
	primary_plane->funcs->destroy(primary_plane);
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
