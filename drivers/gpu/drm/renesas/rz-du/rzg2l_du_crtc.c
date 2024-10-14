// SPDX-License-Identifier: GPL-2.0+
/*
 * RZ/G2L Display Unit CRTCs
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_crtc.c
 */

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_vblank.h>

#include "rzg2l_du_crtc.h"
#include "rzg2l_du_drv.h"
#include "rzg2l_du_encoder.h"
#include "rzg2l_du_kms.h"
#include "rzg2l_du_vsp.h"

#define DU_MCR0			0x00
#define DU_MCR0_DPI_OE		BIT(0)
#define DU_MCR0_DI_EN		BIT(8)

#define DU_DITR0		0x10
#define DU_DITR0_DEMD_HIGH	(BIT(8) | BIT(9))
#define DU_DITR0_VSPOL		BIT(16)
#define DU_DITR0_HSPOL		BIT(17)

#define DU_DITR1		0x14
#define DU_DITR1_VSA(x)		((x) << 0)
#define DU_DITR1_VACTIVE(x)	((x) << 16)

#define DU_DITR2		0x18
#define DU_DITR2_VBP(x)		((x) << 0)
#define DU_DITR2_VFP(x)		((x) << 16)

#define DU_DITR3		0x1c
#define DU_DITR3_HSA(x)		((x) << 0)
#define DU_DITR3_HACTIVE(x)	((x) << 16)

#define DU_DITR4		0x20
#define DU_DITR4_HBP(x)		((x) << 0)
#define DU_DITR4_HFP(x)		((x) << 16)

#define DU_MCR1			0x40
#define DU_MCR1_PB_AUTOCLR	BIT(16)

#define DU_PBCR0		0x4c
#define DU_PBCR0_PB_DEP(x)	((x) << 0)

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

static void rzg2l_du_crtc_set_display_timing(struct rzg2l_du_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.state->adjusted_mode;
	unsigned long mode_clock = mode->clock * 1000;
	u32 ditr0, ditr1, ditr2, ditr3, ditr4, pbcr0;
	struct rzg2l_du_device *rcdu = rcrtc->dev;

	clk_prepare_enable(rcrtc->rzg2l_clocks.dclk);
	clk_set_rate(rcrtc->rzg2l_clocks.dclk, mode_clock);

	ditr0 = (DU_DITR0_DEMD_HIGH
	      | ((mode->flags & DRM_MODE_FLAG_PVSYNC) ? DU_DITR0_VSPOL : 0)
	      | ((mode->flags & DRM_MODE_FLAG_PHSYNC) ? DU_DITR0_HSPOL : 0));

	ditr1 = DU_DITR1_VSA(mode->vsync_end - mode->vsync_start)
	      | DU_DITR1_VACTIVE(mode->vdisplay);

	ditr2 = DU_DITR2_VBP(mode->vtotal - mode->vsync_end)
	      | DU_DITR2_VFP(mode->vsync_start - mode->vdisplay);

	ditr3 = DU_DITR3_HSA(mode->hsync_end - mode->hsync_start)
	      | DU_DITR3_HACTIVE(mode->hdisplay);

	ditr4 = DU_DITR4_HBP(mode->htotal - mode->hsync_end)
	      | DU_DITR4_HFP(mode->hsync_start - mode->hdisplay);

	pbcr0 = DU_PBCR0_PB_DEP(0x1f);

	writel(ditr0, rcdu->mmio + DU_DITR0);
	writel(ditr1, rcdu->mmio + DU_DITR1);
	writel(ditr2, rcdu->mmio + DU_DITR2);
	writel(ditr3, rcdu->mmio + DU_DITR3);
	writel(ditr4, rcdu->mmio + DU_DITR4);
	writel(pbcr0, rcdu->mmio + DU_PBCR0);

	/* Enable auto clear */
	writel(DU_MCR1_PB_AUTOCLR, rcdu->mmio + DU_MCR1);
}

/* -----------------------------------------------------------------------------
 * Page Flip
 */

void rzg2l_du_crtc_finish_page_flip(struct rzg2l_du_crtc *rcrtc)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = rcrtc->event;
	rcrtc->event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (!event)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_crtc_send_vblank_event(&rcrtc->crtc, event);
	wake_up(&rcrtc->flip_wait);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_crtc_vblank_put(&rcrtc->crtc);
}

static bool rzg2l_du_crtc_page_flip_pending(struct rzg2l_du_crtc *rcrtc)
{
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&dev->event_lock, flags);
	pending = rcrtc->event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return pending;
}

static void rzg2l_du_crtc_wait_page_flip(struct rzg2l_du_crtc *rcrtc)
{
	struct rzg2l_du_device *rcdu = rcrtc->dev;

	if (wait_event_timeout(rcrtc->flip_wait,
			       !rzg2l_du_crtc_page_flip_pending(rcrtc),
			       msecs_to_jiffies(50)))
		return;

	dev_warn(rcdu->dev, "page flip timeout\n");

	rzg2l_du_crtc_finish_page_flip(rcrtc);
}

/* -----------------------------------------------------------------------------
 * Start/Stop and Suspend/Resume
 */

static void rzg2l_du_crtc_setup(struct rzg2l_du_crtc *rcrtc)
{
	/* Configure display timings and output routing */
	rzg2l_du_crtc_set_display_timing(rcrtc);

	/* Enable the VSP compositor. */
	rzg2l_du_vsp_enable(rcrtc);

	/* Turn vertical blanking interrupt reporting on. */
	drm_crtc_vblank_on(&rcrtc->crtc);
}

static int rzg2l_du_crtc_get(struct rzg2l_du_crtc *rcrtc)
{
	int ret;

	/*
	 * Guard against double-get, as the function is called from both the
	 * .atomic_enable() and .atomic_flush() handlers.
	 */
	if (rcrtc->initialized)
		return 0;

	ret = clk_prepare_enable(rcrtc->rzg2l_clocks.aclk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(rcrtc->rzg2l_clocks.pclk);
	if (ret < 0)
		goto error_bus_clock;

	ret = reset_control_deassert(rcrtc->rstc);
	if (ret < 0)
		goto error_peri_clock;

	rzg2l_du_crtc_setup(rcrtc);
	rcrtc->initialized = true;

	return 0;

error_peri_clock:
	clk_disable_unprepare(rcrtc->rzg2l_clocks.pclk);
error_bus_clock:
	clk_disable_unprepare(rcrtc->rzg2l_clocks.aclk);
	return ret;
}

static void rzg2l_du_crtc_put(struct rzg2l_du_crtc *rcrtc)
{
	clk_disable_unprepare(rcrtc->rzg2l_clocks.dclk);
	reset_control_assert(rcrtc->rstc);
	clk_disable_unprepare(rcrtc->rzg2l_clocks.pclk);
	clk_disable_unprepare(rcrtc->rzg2l_clocks.aclk);

	rcrtc->initialized = false;
}

static void rzg2l_du_start_stop(struct rzg2l_du_crtc *rcrtc, bool start)
{
	struct rzg2l_du_crtc_state *rstate = to_rzg2l_crtc_state(rcrtc->crtc.state);
	struct rzg2l_du_device *rcdu = rcrtc->dev;
	u32 val = DU_MCR0_DI_EN;

	if (rstate->outputs & BIT(RZG2L_DU_OUTPUT_DPAD0))
		val |= DU_MCR0_DPI_OE;

	writel(start ? val : 0, rcdu->mmio + DU_MCR0);
}

static void rzg2l_du_crtc_start(struct rzg2l_du_crtc *rcrtc)
{
	rzg2l_du_start_stop(rcrtc, true);
}

static void rzg2l_du_crtc_stop(struct rzg2l_du_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;

	/*
	 * Disable vertical blanking interrupt reporting. We first need to wait
	 * for page flip completion before stopping the CRTC as userspace
	 * expects page flips to eventually complete.
	 */
	rzg2l_du_crtc_wait_page_flip(rcrtc);
	drm_crtc_vblank_off(crtc);

	/* Disable the VSP compositor. */
	rzg2l_du_vsp_disable(rcrtc);

	rzg2l_du_start_stop(rcrtc, false);
}

/* -----------------------------------------------------------------------------
 * CRTC Functions
 */

static void rzg2l_du_crtc_atomic_enable(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct rzg2l_du_crtc *rcrtc = to_rzg2l_crtc(crtc);

	rzg2l_du_crtc_get(rcrtc);

	rzg2l_du_crtc_start(rcrtc);
}

static void rzg2l_du_crtc_atomic_disable(struct drm_crtc *crtc,
					 struct drm_atomic_state *state)
{
	struct rzg2l_du_crtc *rcrtc = to_rzg2l_crtc(crtc);

	rzg2l_du_crtc_stop(rcrtc);
	rzg2l_du_crtc_put(rcrtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void rzg2l_du_crtc_atomic_flush(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	struct rzg2l_du_crtc *rcrtc = to_rzg2l_crtc(crtc);
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	WARN_ON(!crtc->state->enable);

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		rcrtc->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	rzg2l_du_vsp_atomic_flush(rcrtc);
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.atomic_flush = rzg2l_du_crtc_atomic_flush,
	.atomic_enable = rzg2l_du_crtc_atomic_enable,
	.atomic_disable = rzg2l_du_crtc_atomic_disable,
};

static struct drm_crtc_state *
rzg2l_du_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct rzg2l_du_crtc_state *state;
	struct rzg2l_du_crtc_state *copy;

	if (WARN_ON(!crtc->state))
		return NULL;

	state = to_rzg2l_crtc_state(crtc->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &copy->state);

	return &copy->state;
}

static void rzg2l_du_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					       struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_rzg2l_crtc_state(state));
}

static void rzg2l_du_crtc_reset(struct drm_crtc *crtc)
{
	struct rzg2l_du_crtc_state *state;

	if (crtc->state) {
		rzg2l_du_crtc_atomic_destroy_state(crtc, crtc->state);
		crtc->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return;

	__drm_atomic_helper_crtc_reset(crtc, &state->state);
}

static int rzg2l_du_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct rzg2l_du_crtc *rcrtc = to_rzg2l_crtc(crtc);

	rcrtc->vblank_enable = true;

	return 0;
}

static void rzg2l_du_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct rzg2l_du_crtc *rcrtc = to_rzg2l_crtc(crtc);

	rcrtc->vblank_enable = false;
}

static const struct drm_crtc_funcs crtc_funcs_rz = {
	.reset = rzg2l_du_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = rzg2l_du_crtc_atomic_duplicate_state,
	.atomic_destroy_state = rzg2l_du_crtc_atomic_destroy_state,
	.enable_vblank = rzg2l_du_crtc_enable_vblank,
	.disable_vblank = rzg2l_du_crtc_disable_vblank,
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

int rzg2l_du_crtc_create(struct rzg2l_du_device *rcdu)
{
	struct rzg2l_du_crtc *rcrtc = &rcdu->crtcs[0];
	struct drm_crtc *crtc = &rcrtc->crtc;
	struct drm_plane *primary;
	int ret;

	rcrtc->rstc = devm_reset_control_get_shared(rcdu->dev, NULL);
	if (IS_ERR(rcrtc->rstc)) {
		dev_err(rcdu->dev, "can't get cpg reset\n");
		return PTR_ERR(rcrtc->rstc);
	}

	rcrtc->rzg2l_clocks.aclk = devm_clk_get(rcdu->dev, "aclk");
	if (IS_ERR(rcrtc->rzg2l_clocks.aclk)) {
		dev_err(rcdu->dev, "no axi clock for DU\n");
		return PTR_ERR(rcrtc->rzg2l_clocks.aclk);
	}

	rcrtc->rzg2l_clocks.pclk = devm_clk_get(rcdu->dev, "pclk");
	if (IS_ERR(rcrtc->rzg2l_clocks.pclk)) {
		dev_err(rcdu->dev, "no peripheral clock for DU\n");
		return PTR_ERR(rcrtc->rzg2l_clocks.pclk);
	}

	rcrtc->rzg2l_clocks.dclk = devm_clk_get(rcdu->dev, "vclk");
	if (IS_ERR(rcrtc->rzg2l_clocks.dclk)) {
		dev_err(rcdu->dev, "no video clock for DU\n");
		return PTR_ERR(rcrtc->rzg2l_clocks.dclk);
	}

	init_waitqueue_head(&rcrtc->flip_wait);
	rcrtc->dev = rcdu;

	primary = rzg2l_du_vsp_get_drm_plane(rcrtc, rcrtc->vsp_pipe);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	ret = drmm_crtc_init_with_planes(&rcdu->ddev, crtc, primary, NULL,
					 &crtc_funcs_rz, NULL);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	return 0;
}
