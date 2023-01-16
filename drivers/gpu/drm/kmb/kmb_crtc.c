// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2018-2020 Intel Corporation
 */

#include <linux/clk.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_modeset_helper_vtables.h>

#include "kmb_drv.h"
#include "kmb_dsi.h"
#include "kmb_plane.h"
#include "kmb_regs.h"

struct kmb_crtc_timing {
	u32 vfront_porch;
	u32 vback_porch;
	u32 vsync_len;
	u32 hfront_porch;
	u32 hback_porch;
	u32 hsync_len;
};

static int kmb_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct kmb_drm_private *kmb = to_kmb(dev);

	/* Clear interrupt */
	kmb_write_lcd(kmb, LCD_INT_CLEAR, LCD_INT_VERT_COMP);
	/* Set which interval to generate vertical interrupt */
	kmb_write_lcd(kmb, LCD_VSTATUS_COMPARE,
		      LCD_VSTATUS_COMPARE_VSYNC);
	/* Enable vertical interrupt */
	kmb_set_bitmask_lcd(kmb, LCD_INT_ENABLE,
			    LCD_INT_VERT_COMP);
	return 0;
}

static void kmb_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct kmb_drm_private *kmb = to_kmb(dev);

	/* Clear interrupt */
	kmb_write_lcd(kmb, LCD_INT_CLEAR, LCD_INT_VERT_COMP);
	/* Disable vertical interrupt */
	kmb_clr_bitmask_lcd(kmb, LCD_INT_ENABLE,
			    LCD_INT_VERT_COMP);
}

static const struct drm_crtc_funcs kmb_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = kmb_crtc_enable_vblank,
	.disable_vblank = kmb_crtc_disable_vblank,
};

static void kmb_crtc_set_mode(struct drm_crtc *crtc,
			      struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_display_mode *m = &crtc->state->adjusted_mode;
	struct kmb_crtc_timing vm;
	struct kmb_drm_private *kmb = to_kmb(dev);
	unsigned int val = 0;

	/* Initialize mipi */
	kmb_dsi_mode_set(kmb->kmb_dsi, m, kmb->sys_clk_mhz, old_state);
	drm_info(dev,
		 "vfp= %d vbp= %d vsync_len=%d hfp=%d hbp=%d hsync_len=%d\n",
		 m->crtc_vsync_start - m->crtc_vdisplay,
		 m->crtc_vtotal - m->crtc_vsync_end,
		 m->crtc_vsync_end - m->crtc_vsync_start,
		 m->crtc_hsync_start - m->crtc_hdisplay,
		 m->crtc_htotal - m->crtc_hsync_end,
		 m->crtc_hsync_end - m->crtc_hsync_start);
	val = kmb_read_lcd(kmb, LCD_INT_ENABLE);
	kmb_clr_bitmask_lcd(kmb, LCD_INT_ENABLE, val);
	kmb_set_bitmask_lcd(kmb, LCD_INT_CLEAR, ~0x0);
	vm.vfront_porch = 2;
	vm.vback_porch = 2;
	vm.vsync_len = 8;
	vm.hfront_porch = 0;
	vm.hback_porch = 0;
	vm.hsync_len = 28;

	drm_dbg(dev, "%s : %dactive height= %d vbp=%d vfp=%d vsync-w=%d h-active=%d h-bp=%d h-fp=%d hsync-l=%d",
		__func__, __LINE__,
			m->crtc_vdisplay, vm.vback_porch, vm.vfront_porch,
			vm.vsync_len, m->crtc_hdisplay, vm.hback_porch,
			vm.hfront_porch, vm.hsync_len);
	kmb_write_lcd(kmb, LCD_V_ACTIVEHEIGHT,
		      m->crtc_vdisplay - 1);
	kmb_write_lcd(kmb, LCD_V_BACKPORCH, vm.vback_porch);
	kmb_write_lcd(kmb, LCD_V_FRONTPORCH, vm.vfront_porch);
	kmb_write_lcd(kmb, LCD_VSYNC_WIDTH, vm.vsync_len - 1);
	kmb_write_lcd(kmb, LCD_H_ACTIVEWIDTH,
		      m->crtc_hdisplay - 1);
	kmb_write_lcd(kmb, LCD_H_BACKPORCH, vm.hback_porch);
	kmb_write_lcd(kmb, LCD_H_FRONTPORCH, vm.hfront_porch);
	kmb_write_lcd(kmb, LCD_HSYNC_WIDTH, vm.hsync_len - 1);
	/* This is hardcoded as 0 in the Myriadx code */
	kmb_write_lcd(kmb, LCD_VSYNC_START, 0);
	kmb_write_lcd(kmb, LCD_VSYNC_END, 0);
	/* Back ground color */
	kmb_write_lcd(kmb, LCD_BG_COLOUR_LS, 0x4);
	if (m->flags == DRM_MODE_FLAG_INTERLACE) {
		kmb_write_lcd(kmb,
			      LCD_VSYNC_WIDTH_EVEN, vm.vsync_len - 1);
		kmb_write_lcd(kmb,
			      LCD_V_BACKPORCH_EVEN, vm.vback_porch);
		kmb_write_lcd(kmb,
			      LCD_V_FRONTPORCH_EVEN, vm.vfront_porch);
		kmb_write_lcd(kmb, LCD_V_ACTIVEHEIGHT_EVEN,
			      m->crtc_vdisplay - 1);
		/* This is hardcoded as 10 in the Myriadx code */
		kmb_write_lcd(kmb, LCD_VSYNC_START_EVEN, 10);
		kmb_write_lcd(kmb, LCD_VSYNC_END_EVEN, 10);
	}
	kmb_write_lcd(kmb, LCD_TIMING_GEN_TRIG, 1);
	kmb_set_bitmask_lcd(kmb, LCD_CONTROL, LCD_CTRL_ENABLE);
	kmb_set_bitmask_lcd(kmb, LCD_INT_ENABLE, val);
}

static void kmb_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct kmb_drm_private *kmb = crtc_to_kmb_priv(crtc);

	clk_prepare_enable(kmb->kmb_clk.clk_lcd);
	kmb_crtc_set_mode(crtc, state);
	drm_crtc_vblank_on(crtc);
}

static void kmb_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct kmb_drm_private *kmb = crtc_to_kmb_priv(crtc);
	struct drm_crtc_state *old_state = drm_atomic_get_old_crtc_state(state, crtc);

	/* due to hw limitations, planes need to be off when crtc is off */
	drm_atomic_helper_disable_planes_on_crtc(old_state, false);

	drm_crtc_vblank_off(crtc);
	clk_disable_unprepare(kmb->kmb_clk.clk_lcd);
}

static void kmb_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct drm_device *dev = crtc->dev;
	struct kmb_drm_private *kmb = to_kmb(dev);

	kmb_clr_bitmask_lcd(kmb, LCD_INT_ENABLE,
			    LCD_INT_VERT_COMP);
}

static void kmb_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct drm_device *dev = crtc->dev;
	struct kmb_drm_private *kmb = to_kmb(dev);

	kmb_set_bitmask_lcd(kmb, LCD_INT_ENABLE,
			    LCD_INT_VERT_COMP);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
	}
	crtc->state->event = NULL;
	spin_unlock_irq(&crtc->dev->event_lock);
}

static enum drm_mode_status
		kmb_crtc_mode_valid(struct drm_crtc *crtc,
				    const struct drm_display_mode *mode)
{
	int refresh;
	struct drm_device *dev = crtc->dev;
	int vfp = mode->vsync_start - mode->vdisplay;

	if (mode->vdisplay < KMB_CRTC_MAX_HEIGHT) {
		drm_dbg(dev, "height = %d less than %d",
			mode->vdisplay, KMB_CRTC_MAX_HEIGHT);
		return MODE_BAD_VVALUE;
	}
	if (mode->hdisplay < KMB_CRTC_MAX_WIDTH) {
		drm_dbg(dev, "width = %d less than %d",
			mode->hdisplay, KMB_CRTC_MAX_WIDTH);
		return MODE_BAD_HVALUE;
	}
	refresh = drm_mode_vrefresh(mode);
	if (refresh < KMB_MIN_VREFRESH || refresh > KMB_MAX_VREFRESH) {
		drm_dbg(dev, "refresh = %d less than %d or greater than %d",
			refresh, KMB_MIN_VREFRESH, KMB_MAX_VREFRESH);
		return MODE_BAD;
	}

	if (vfp < KMB_CRTC_MIN_VFP) {
		drm_dbg(dev, "vfp = %d less than %d", vfp, KMB_CRTC_MIN_VFP);
		return MODE_BAD;
	}

	return MODE_OK;
}

static const struct drm_crtc_helper_funcs kmb_crtc_helper_funcs = {
	.atomic_begin = kmb_crtc_atomic_begin,
	.atomic_enable = kmb_crtc_atomic_enable,
	.atomic_disable = kmb_crtc_atomic_disable,
	.atomic_flush = kmb_crtc_atomic_flush,
	.mode_valid = kmb_crtc_mode_valid,
};

int kmb_setup_crtc(struct drm_device *drm)
{
	struct kmb_drm_private *kmb = to_kmb(drm);
	struct kmb_plane *primary;
	int ret;

	primary = kmb_plane_init(drm);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	ret = drm_crtc_init_with_planes(drm, &kmb->crtc, &primary->base_plane,
					NULL, &kmb_crtc_funcs, NULL);
	if (ret) {
		kmb_plane_destroy(&primary->base_plane);
		return ret;
	}

	drm_crtc_helper_add(&kmb->crtc, &kmb_crtc_helper_funcs);
	return 0;
}
