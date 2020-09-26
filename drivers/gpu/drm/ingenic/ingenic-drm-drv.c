// SPDX-License-Identifier: GPL-2.0
//
// Ingenic JZ47xx KMS driver
//
// Copyright (C) 2019, Paul Cercueil <paul@crapouillou.net>

#include "ingenic-drm.h"

#include <linux/component.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dma-noncoherent.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_irq.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

struct ingenic_dma_hwdesc {
	u32 next;
	u32 addr;
	u32 id;
	u32 cmd;
} __aligned(16);

struct ingenic_dma_hwdescs {
	struct ingenic_dma_hwdesc hwdesc_f0;
	struct ingenic_dma_hwdesc hwdesc_f1;
};

struct jz_soc_info {
	bool needs_dev_clk;
	bool has_osd;
	unsigned int max_width, max_height;
};

struct ingenic_drm {
	struct drm_device drm;
	/*
	 * f1 (aka. foreground1) is our primary plane, on top of which
	 * f0 (aka. foreground0) can be overlayed. Z-order is fixed in
	 * hardware and cannot be changed.
	 */
	struct drm_plane f0, f1, *ipu_plane;
	struct drm_crtc crtc;

	struct device *dev;
	struct regmap *map;
	struct clk *lcd_clk, *pix_clk;
	const struct jz_soc_info *soc_info;

	struct ingenic_dma_hwdescs *dma_hwdescs;
	dma_addr_t dma_hwdescs_phys;

	bool panel_is_sharp;
	bool no_vblank;

	/*
	 * clk_mutex is used to synchronize the pixel clock rate update with
	 * the VBLANK. When the pixel clock's parent clock needs to be updated,
	 * clock_nb's notifier function will lock the mutex, then wait until the
	 * next VBLANK. At that point, the parent clock's rate can be updated,
	 * and the mutex is then unlocked. If an atomic commit happens in the
	 * meantime, it will lock on the mutex, effectively waiting until the
	 * clock update process finishes. Finally, the pixel clock's rate will
	 * be recomputed when the mutex has been released, in the pending atomic
	 * commit, or a future one.
	 */
	struct mutex clk_mutex;
	bool update_clk_rate;
	struct notifier_block clock_nb;
};

static const u32 ingenic_drm_primary_formats[] = {
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
};

static bool ingenic_drm_cached_gem_buf;
module_param_named(cached_gem_buffers, ingenic_drm_cached_gem_buf, bool, 0400);
MODULE_PARM_DESC(cached_gem_buffers,
		 "Enable fully cached GEM buffers [default=false]");

static bool ingenic_drm_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case JZ_REG_LCD_IID:
	case JZ_REG_LCD_SA0:
	case JZ_REG_LCD_FID0:
	case JZ_REG_LCD_CMD0:
	case JZ_REG_LCD_SA1:
	case JZ_REG_LCD_FID1:
	case JZ_REG_LCD_CMD1:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config ingenic_drm_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,

	.max_register = JZ_REG_LCD_SIZE1,
	.writeable_reg = ingenic_drm_writeable_reg,
};

static inline struct ingenic_drm *drm_device_get_priv(struct drm_device *drm)
{
	return container_of(drm, struct ingenic_drm, drm);
}

static inline struct ingenic_drm *drm_crtc_get_priv(struct drm_crtc *crtc)
{
	return container_of(crtc, struct ingenic_drm, crtc);
}

static inline struct ingenic_drm *drm_nb_get_priv(struct notifier_block *nb)
{
	return container_of(nb, struct ingenic_drm, clock_nb);
}

static int ingenic_drm_update_pixclk(struct notifier_block *nb,
				     unsigned long action,
				     void *data)
{
	struct ingenic_drm *priv = drm_nb_get_priv(nb);

	switch (action) {
	case PRE_RATE_CHANGE:
		mutex_lock(&priv->clk_mutex);
		priv->update_clk_rate = true;
		drm_crtc_wait_one_vblank(&priv->crtc);
		return NOTIFY_OK;
	default:
		mutex_unlock(&priv->clk_mutex);
		return NOTIFY_OK;
	}
}

static void ingenic_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					   struct drm_crtc_state *state)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);

	regmap_write(priv->map, JZ_REG_LCD_STATE, 0);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_ENABLE | JZ_LCD_CTRL_DISABLE,
			   JZ_LCD_CTRL_ENABLE);

	drm_crtc_vblank_on(crtc);
}

static void ingenic_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					    struct drm_crtc_state *state)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	unsigned int var;

	drm_crtc_vblank_off(crtc);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_DISABLE, JZ_LCD_CTRL_DISABLE);

	regmap_read_poll_timeout(priv->map, JZ_REG_LCD_STATE, var,
				 var & JZ_LCD_STATE_DISABLED,
				 1000, 0);
}

static void ingenic_drm_crtc_update_timings(struct ingenic_drm *priv,
					    struct drm_display_mode *mode)
{
	unsigned int vpe, vds, vde, vt, hpe, hds, hde, ht;

	vpe = mode->vsync_end - mode->vsync_start;
	vds = mode->vtotal - mode->vsync_start;
	vde = vds + mode->vdisplay;
	vt = vde + mode->vsync_start - mode->vdisplay;

	hpe = mode->hsync_end - mode->hsync_start;
	hds = mode->htotal - mode->hsync_start;
	hde = hds + mode->hdisplay;
	ht = hde + mode->hsync_start - mode->hdisplay;

	regmap_write(priv->map, JZ_REG_LCD_VSYNC,
		     0 << JZ_LCD_VSYNC_VPS_OFFSET |
		     vpe << JZ_LCD_VSYNC_VPE_OFFSET);

	regmap_write(priv->map, JZ_REG_LCD_HSYNC,
		     0 << JZ_LCD_HSYNC_HPS_OFFSET |
		     hpe << JZ_LCD_HSYNC_HPE_OFFSET);

	regmap_write(priv->map, JZ_REG_LCD_VAT,
		     ht << JZ_LCD_VAT_HT_OFFSET |
		     vt << JZ_LCD_VAT_VT_OFFSET);

	regmap_write(priv->map, JZ_REG_LCD_DAH,
		     hds << JZ_LCD_DAH_HDS_OFFSET |
		     hde << JZ_LCD_DAH_HDE_OFFSET);
	regmap_write(priv->map, JZ_REG_LCD_DAV,
		     vds << JZ_LCD_DAV_VDS_OFFSET |
		     vde << JZ_LCD_DAV_VDE_OFFSET);

	if (priv->panel_is_sharp) {
		regmap_write(priv->map, JZ_REG_LCD_PS, hde << 16 | (hde + 1));
		regmap_write(priv->map, JZ_REG_LCD_CLS, hde << 16 | (hde + 1));
		regmap_write(priv->map, JZ_REG_LCD_SPL, hpe << 16 | (hpe + 1));
		regmap_write(priv->map, JZ_REG_LCD_REV, mode->htotal << 16);
	}

	regmap_set_bits(priv->map, JZ_REG_LCD_CTRL,
			JZ_LCD_CTRL_OFUP | JZ_LCD_CTRL_BURST_16);

	/*
	 * IPU restart - specify how much time the LCDC will wait before
	 * transferring a new frame from the IPU. The value is the one
	 * suggested in the programming manual.
	 */
	regmap_write(priv->map, JZ_REG_LCD_IPUR, JZ_LCD_IPUR_IPUREN |
		     (ht * vpe / 3) << JZ_LCD_IPUR_IPUR_LSB);
}

static int ingenic_drm_crtc_atomic_check(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	struct drm_plane_state *f1_state, *f0_state, *ipu_state = NULL;

	if (drm_atomic_crtc_needs_modeset(state) && priv->soc_info->has_osd) {
		f1_state = drm_atomic_get_plane_state(state->state, &priv->f1);
		if (IS_ERR(f1_state))
			return PTR_ERR(f1_state);

		f0_state = drm_atomic_get_plane_state(state->state, &priv->f0);
		if (IS_ERR(f0_state))
			return PTR_ERR(f0_state);

		if (IS_ENABLED(CONFIG_DRM_INGENIC_IPU) && priv->ipu_plane) {
			ipu_state = drm_atomic_get_plane_state(state->state, priv->ipu_plane);
			if (IS_ERR(ipu_state))
				return PTR_ERR(ipu_state);

			/* IPU and F1 planes cannot be enabled at the same time. */
			if (f1_state->fb && ipu_state->fb) {
				dev_dbg(priv->dev, "Cannot enable both F1 and IPU\n");
				return -EINVAL;
			}
		}

		/* If all the planes are disabled, we won't get a VBLANK IRQ */
		priv->no_vblank = !f1_state->fb && !f0_state->fb &&
				  !(ipu_state && ipu_state->fb);
	}

	return 0;
}

static enum drm_mode_status
ingenic_drm_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	long rate;

	if (mode->hdisplay > priv->soc_info->max_width)
		return MODE_BAD_HVALUE;
	if (mode->vdisplay > priv->soc_info->max_height)
		return MODE_BAD_VVALUE;

	rate = clk_round_rate(priv->pix_clk, mode->clock * 1000);
	if (rate < 0)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static void ingenic_drm_crtc_atomic_begin(struct drm_crtc *crtc,
					  struct drm_crtc_state *oldstate)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	u32 ctrl = 0;

	if (priv->soc_info->has_osd &&
	    drm_atomic_crtc_needs_modeset(crtc->state)) {
		/*
		 * If IPU plane is enabled, enable IPU as source for the F1
		 * plane; otherwise use regular DMA.
		 */
		if (priv->ipu_plane && priv->ipu_plane->state->fb)
			ctrl |= JZ_LCD_OSDCTRL_IPU;

		regmap_update_bits(priv->map, JZ_REG_LCD_OSDCTRL,
				   JZ_LCD_OSDCTRL_IPU, ctrl);
	}
}

static void ingenic_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					  struct drm_crtc_state *oldstate)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	struct drm_crtc_state *state = crtc->state;
	struct drm_pending_vblank_event *event = state->event;

	if (drm_atomic_crtc_needs_modeset(state)) {
		ingenic_drm_crtc_update_timings(priv, &state->mode);
		priv->update_clk_rate = true;
	}

	if (priv->update_clk_rate) {
		mutex_lock(&priv->clk_mutex);
		clk_set_rate(priv->pix_clk, state->adjusted_mode.clock * 1000);
		priv->update_clk_rate = false;
		mutex_unlock(&priv->clk_mutex);
	}

	if (event) {
		state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static int ingenic_drm_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct ingenic_drm *priv = drm_device_get_priv(plane->dev);
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc = state->crtc ?: plane->state->crtc;
	int ret;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  priv->soc_info->has_osd,
						  true);
	if (ret)
		return ret;

	/*
	 * If OSD is not available, check that the width/height match.
	 * Note that state->src_* are in 16.16 fixed-point format.
	 */
	if (!priv->soc_info->has_osd &&
	    (state->src_x != 0 ||
	     (state->src_w >> 16) != state->crtc_w ||
	     (state->src_h >> 16) != state->crtc_h))
		return -EINVAL;

	/*
	 * Require full modeset if enabling or disabling a plane, or changing
	 * its position, size or depth.
	 */
	if (priv->soc_info->has_osd &&
	    (!plane->state->fb || !state->fb ||
	     plane->state->crtc_x != state->crtc_x ||
	     plane->state->crtc_y != state->crtc_y ||
	     plane->state->crtc_w != state->crtc_w ||
	     plane->state->crtc_h != state->crtc_h ||
	     plane->state->fb->format->format != state->fb->format->format))
		crtc_state->mode_changed = true;

	drm_atomic_helper_check_plane_damage(state->state, state);

	return 0;
}

static void ingenic_drm_plane_enable(struct ingenic_drm *priv,
				     struct drm_plane *plane)
{
	unsigned int en_bit;

	if (priv->soc_info->has_osd) {
		if (plane->type == DRM_PLANE_TYPE_PRIMARY)
			en_bit = JZ_LCD_OSDC_F1EN;
		else
			en_bit = JZ_LCD_OSDC_F0EN;

		regmap_set_bits(priv->map, JZ_REG_LCD_OSDC, en_bit);
	}
}

void ingenic_drm_plane_disable(struct device *dev, struct drm_plane *plane)
{
	struct ingenic_drm *priv = dev_get_drvdata(dev);
	unsigned int en_bit;

	if (priv->soc_info->has_osd) {
		if (plane->type == DRM_PLANE_TYPE_PRIMARY)
			en_bit = JZ_LCD_OSDC_F1EN;
		else
			en_bit = JZ_LCD_OSDC_F0EN;

		regmap_clear_bits(priv->map, JZ_REG_LCD_OSDC, en_bit);
	}
}

static void ingenic_drm_plane_atomic_disable(struct drm_plane *plane,
					     struct drm_plane_state *old_state)
{
	struct ingenic_drm *priv = drm_device_get_priv(plane->dev);

	ingenic_drm_plane_disable(priv->dev, plane);
}

void ingenic_drm_plane_config(struct device *dev,
			      struct drm_plane *plane, u32 fourcc)
{
	struct ingenic_drm *priv = dev_get_drvdata(dev);
	struct drm_plane_state *state = plane->state;
	unsigned int xy_reg, size_reg;
	unsigned int ctrl = 0;

	ingenic_drm_plane_enable(priv, plane);

	if (priv->soc_info->has_osd &&
	    plane->type == DRM_PLANE_TYPE_PRIMARY) {
		switch (fourcc) {
		case DRM_FORMAT_XRGB1555:
			ctrl |= JZ_LCD_OSDCTRL_RGB555;
			fallthrough;
		case DRM_FORMAT_RGB565:
			ctrl |= JZ_LCD_OSDCTRL_BPP_15_16;
			break;
		case DRM_FORMAT_XRGB8888:
			ctrl |= JZ_LCD_OSDCTRL_BPP_18_24;
			break;
		}

		regmap_update_bits(priv->map, JZ_REG_LCD_OSDCTRL,
				   JZ_LCD_OSDCTRL_BPP_MASK, ctrl);
	} else {
		switch (fourcc) {
		case DRM_FORMAT_XRGB1555:
			ctrl |= JZ_LCD_CTRL_RGB555;
			fallthrough;
		case DRM_FORMAT_RGB565:
			ctrl |= JZ_LCD_CTRL_BPP_15_16;
			break;
		case DRM_FORMAT_XRGB8888:
			ctrl |= JZ_LCD_CTRL_BPP_18_24;
			break;
		}

		regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
				   JZ_LCD_CTRL_BPP_MASK, ctrl);
	}

	if (priv->soc_info->has_osd) {
		if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
			xy_reg = JZ_REG_LCD_XYP1;
			size_reg = JZ_REG_LCD_SIZE1;
		} else {
			xy_reg = JZ_REG_LCD_XYP0;
			size_reg = JZ_REG_LCD_SIZE0;
		}

		regmap_write(priv->map, xy_reg,
			     state->crtc_x << JZ_LCD_XYP01_XPOS_LSB |
			     state->crtc_y << JZ_LCD_XYP01_YPOS_LSB);
		regmap_write(priv->map, size_reg,
			     state->crtc_w << JZ_LCD_SIZE01_WIDTH_LSB |
			     state->crtc_h << JZ_LCD_SIZE01_HEIGHT_LSB);
	}
}

void ingenic_drm_sync_data(struct device *dev,
			   struct drm_plane_state *old_state,
			   struct drm_plane_state *state)
{
	const struct drm_format_info *finfo = state->fb->format;
	struct ingenic_drm *priv = dev_get_drvdata(dev);
	struct drm_atomic_helper_damage_iter iter;
	unsigned int offset, i;
	struct drm_rect clip;
	dma_addr_t paddr;
	void *addr;

	if (!ingenic_drm_cached_gem_buf)
		return;

	drm_atomic_helper_damage_iter_init(&iter, old_state, state);

	drm_atomic_for_each_plane_damage(&iter, &clip) {
		for (i = 0; i < finfo->num_planes; i++) {
			paddr = drm_fb_cma_get_gem_addr(state->fb, state, i);
			addr = phys_to_virt(paddr);

			/* Ignore x1/x2 values, invalidate complete lines */
			offset = clip.y1 * state->fb->pitches[i];

			dma_cache_sync(priv->dev, addr + offset,
				       (clip.y2 - clip.y1) * state->fb->pitches[i],
				       DMA_TO_DEVICE);
		}
	}
}

static void ingenic_drm_plane_atomic_update(struct drm_plane *plane,
					    struct drm_plane_state *oldstate)
{
	struct ingenic_drm *priv = drm_device_get_priv(plane->dev);
	struct drm_plane_state *state = plane->state;
	struct ingenic_dma_hwdesc *hwdesc;
	unsigned int width, height, cpp;
	dma_addr_t addr;

	if (state && state->fb) {
		ingenic_drm_sync_data(priv->dev, oldstate, state);

		addr = drm_fb_cma_get_gem_addr(state->fb, state, 0);
		width = state->src_w >> 16;
		height = state->src_h >> 16;
		cpp = state->fb->format->cpp[0];

		if (priv->soc_info->has_osd && plane->type == DRM_PLANE_TYPE_OVERLAY)
			hwdesc = &priv->dma_hwdescs->hwdesc_f0;
		else
			hwdesc = &priv->dma_hwdescs->hwdesc_f1;

		hwdesc->addr = addr;
		hwdesc->cmd = JZ_LCD_CMD_EOF_IRQ | (width * height * cpp / 4);

		if (drm_atomic_crtc_needs_modeset(state->crtc->state))
			ingenic_drm_plane_config(priv->dev, plane,
						 state->fb->format->format);
	}
}

static void ingenic_drm_encoder_atomic_mode_set(struct drm_encoder *encoder,
						struct drm_crtc_state *crtc_state,
						struct drm_connector_state *conn_state)
{
	struct ingenic_drm *priv = drm_device_get_priv(encoder->dev);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct drm_connector *conn = conn_state->connector;
	struct drm_display_info *info = &conn->display_info;
	unsigned int cfg;

	priv->panel_is_sharp = info->bus_flags & DRM_BUS_FLAG_SHARP_SIGNALS;

	if (priv->panel_is_sharp) {
		cfg = JZ_LCD_CFG_MODE_SPECIAL_TFT_1 | JZ_LCD_CFG_REV_POLARITY;
	} else {
		cfg = JZ_LCD_CFG_PS_DISABLE | JZ_LCD_CFG_CLS_DISABLE
		    | JZ_LCD_CFG_SPL_DISABLE | JZ_LCD_CFG_REV_DISABLE;
	}

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		cfg |= JZ_LCD_CFG_HSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		cfg |= JZ_LCD_CFG_VSYNC_ACTIVE_LOW;
	if (info->bus_flags & DRM_BUS_FLAG_DE_LOW)
		cfg |= JZ_LCD_CFG_DE_ACTIVE_LOW;
	if (info->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		cfg |= JZ_LCD_CFG_PCLK_FALLING_EDGE;

	if (!priv->panel_is_sharp) {
		if (conn->connector_type == DRM_MODE_CONNECTOR_TV) {
			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				cfg |= JZ_LCD_CFG_MODE_TV_OUT_I;
			else
				cfg |= JZ_LCD_CFG_MODE_TV_OUT_P;
		} else {
			switch (*info->bus_formats) {
			case MEDIA_BUS_FMT_RGB565_1X16:
				cfg |= JZ_LCD_CFG_MODE_GENERIC_16BIT;
				break;
			case MEDIA_BUS_FMT_RGB666_1X18:
				cfg |= JZ_LCD_CFG_MODE_GENERIC_18BIT;
				break;
			case MEDIA_BUS_FMT_RGB888_1X24:
				cfg |= JZ_LCD_CFG_MODE_GENERIC_24BIT;
				break;
			case MEDIA_BUS_FMT_RGB888_3X8:
				cfg |= JZ_LCD_CFG_MODE_8BIT_SERIAL;
				break;
			default:
				break;
			}
		}
	}

	regmap_write(priv->map, JZ_REG_LCD_CFG, cfg);
}

static int ingenic_drm_encoder_atomic_check(struct drm_encoder *encoder,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state)
{
	struct drm_display_info *info = &conn_state->connector->display_info;

	if (info->num_bus_formats != 1)
		return -EINVAL;

	if (conn_state->connector->connector_type == DRM_MODE_CONNECTOR_TV)
		return 0;

	switch (*info->bus_formats) {
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_3X8:
		return 0;
	default:
		return -EINVAL;
	}
}

static void ingenic_drm_atomic_helper_commit_tail(struct drm_atomic_state *old_state)
{
	/*
	 * Just your regular drm_atomic_helper_commit_tail(), but only calls
	 * drm_atomic_helper_wait_for_vblanks() if priv->no_vblank.
	 */
	struct drm_device *dev = old_state->dev;
	struct ingenic_drm *priv = drm_device_get_priv(dev);

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	if (!priv->no_vblank)
		drm_atomic_helper_wait_for_vblanks(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static irqreturn_t ingenic_drm_irq_handler(int irq, void *arg)
{
	struct ingenic_drm *priv = drm_device_get_priv(arg);
	unsigned int state;

	regmap_read(priv->map, JZ_REG_LCD_STATE, &state);

	regmap_update_bits(priv->map, JZ_REG_LCD_STATE,
			   JZ_LCD_STATE_EOF_IRQ, 0);

	if (state & JZ_LCD_STATE_EOF_IRQ)
		drm_crtc_handle_vblank(&priv->crtc);

	return IRQ_HANDLED;
}

static int ingenic_drm_enable_vblank(struct drm_crtc *crtc)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_EOF_IRQ, JZ_LCD_CTRL_EOF_IRQ);

	return 0;
}

static void ingenic_drm_disable_vblank(struct drm_crtc *crtc)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL, JZ_LCD_CTRL_EOF_IRQ, 0);
}

static struct drm_framebuffer *
ingenic_drm_gem_fb_create(struct drm_device *dev, struct drm_file *file,
			  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	if (ingenic_drm_cached_gem_buf)
		return drm_gem_fb_create_with_dirty(dev, file, mode_cmd);

	return drm_gem_fb_create(dev, file, mode_cmd);
}

static int ingenic_drm_gem_mmap(struct drm_gem_object *obj,
				struct vm_area_struct *vma)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);
	struct device *dev = cma_obj->base.dev->dev;
	unsigned long attrs;
	int ret;

	if (ingenic_drm_cached_gem_buf)
		attrs = DMA_ATTR_NON_CONSISTENT;
	else
		attrs = DMA_ATTR_WRITE_COMBINE;

	/*
	 * Clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	ret = dma_mmap_attrs(dev, vma, cma_obj->vaddr, cma_obj->paddr,
			     vma->vm_end - vma->vm_start, attrs);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

static int ingenic_drm_gem_cma_mmap(struct file *filp,
				    struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	return ingenic_drm_gem_mmap(vma->vm_private_data, vma);
}

static const struct file_operations ingenic_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= noop_llseek,
	.mmap		= ingenic_drm_gem_cma_mmap,
};

static struct drm_driver ingenic_drm_driver_data = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.name			= "ingenic-drm",
	.desc			= "DRM module for Ingenic SoCs",
	.date			= "20200716",
	.major			= 1,
	.minor			= 1,
	.patchlevel		= 0,

	.fops			= &ingenic_drm_fops,
	DRM_GEM_CMA_DRIVER_OPS,

	.irq_handler		= ingenic_drm_irq_handler,
};

static const struct drm_plane_funcs ingenic_drm_primary_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.destroy		= drm_plane_cleanup,

	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const struct drm_crtc_funcs ingenic_drm_crtc_funcs = {
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,

	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,

	.enable_vblank		= ingenic_drm_enable_vblank,
	.disable_vblank		= ingenic_drm_disable_vblank,

	.gamma_set		= drm_atomic_helper_legacy_gamma_set,
};

static const struct drm_plane_helper_funcs ingenic_drm_plane_helper_funcs = {
	.atomic_update		= ingenic_drm_plane_atomic_update,
	.atomic_check		= ingenic_drm_plane_atomic_check,
	.atomic_disable		= ingenic_drm_plane_atomic_disable,
	.prepare_fb		= drm_gem_fb_prepare_fb,
};

static const struct drm_crtc_helper_funcs ingenic_drm_crtc_helper_funcs = {
	.atomic_enable		= ingenic_drm_crtc_atomic_enable,
	.atomic_disable		= ingenic_drm_crtc_atomic_disable,
	.atomic_begin		= ingenic_drm_crtc_atomic_begin,
	.atomic_flush		= ingenic_drm_crtc_atomic_flush,
	.atomic_check		= ingenic_drm_crtc_atomic_check,
	.mode_valid		= ingenic_drm_crtc_mode_valid,
};

static const struct drm_encoder_helper_funcs ingenic_drm_encoder_helper_funcs = {
	.atomic_mode_set	= ingenic_drm_encoder_atomic_mode_set,
	.atomic_check		= ingenic_drm_encoder_atomic_check,
};

static const struct drm_mode_config_funcs ingenic_drm_mode_config_funcs = {
	.fb_create		= ingenic_drm_gem_fb_create,
	.output_poll_changed	= drm_fb_helper_output_poll_changed,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs ingenic_drm_mode_config_helpers = {
	.atomic_commit_tail = ingenic_drm_atomic_helper_commit_tail,
};

static void ingenic_drm_unbind_all(void *d)
{
	struct ingenic_drm *priv = d;

	component_unbind_all(priv->dev, &priv->drm);
}

static void __maybe_unused ingenic_drm_release_rmem(void *d)
{
	of_reserved_mem_device_release(d);
}

static int ingenic_drm_bind(struct device *dev, bool has_components)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct jz_soc_info *soc_info;
	struct ingenic_drm *priv;
	struct clk *parent_clk;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct drm_encoder *encoder;
	struct drm_device *drm;
	void __iomem *base;
	long parent_rate;
	unsigned int i, clone_mask = 0;
	dma_addr_t dma_hwdesc_phys_f0, dma_hwdesc_phys_f1;
	int ret, irq;

	soc_info = of_device_get_match_data(dev);
	if (!soc_info) {
		dev_err(dev, "Missing platform data\n");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_OF_RESERVED_MEM)) {
		ret = of_reserved_mem_device_init(dev);

		if (ret && ret != -ENODEV)
			dev_warn(dev, "Failed to get reserved memory: %d\n", ret);

		if (!ret) {
			ret = devm_add_action_or_reset(dev, ingenic_drm_release_rmem, dev);
			if (ret)
				return ret;
		}
	}

	priv = devm_drm_dev_alloc(dev, &ingenic_drm_driver_data,
				  struct ingenic_drm, drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	priv->soc_info = soc_info;
	priv->dev = dev;
	drm = &priv->drm;

	platform_set_drvdata(pdev, priv);

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = soc_info->max_width;
	drm->mode_config.max_height = 4095;
	drm->mode_config.funcs = &ingenic_drm_mode_config_funcs;
	drm->mode_config.helper_private = &ingenic_drm_mode_config_helpers;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "Failed to get memory resource\n");
		return PTR_ERR(base);
	}

	priv->map = devm_regmap_init_mmio(dev, base,
					  &ingenic_drm_regmap_config);
	if (IS_ERR(priv->map)) {
		dev_err(dev, "Failed to create regmap\n");
		return PTR_ERR(priv->map);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (soc_info->needs_dev_clk) {
		priv->lcd_clk = devm_clk_get(dev, "lcd");
		if (IS_ERR(priv->lcd_clk)) {
			dev_err(dev, "Failed to get lcd clock\n");
			return PTR_ERR(priv->lcd_clk);
		}
	}

	priv->pix_clk = devm_clk_get(dev, "lcd_pclk");
	if (IS_ERR(priv->pix_clk)) {
		dev_err(dev, "Failed to get pixel clock\n");
		return PTR_ERR(priv->pix_clk);
	}

	priv->dma_hwdescs = dmam_alloc_coherent(dev,
						sizeof(*priv->dma_hwdescs),
						&priv->dma_hwdescs_phys,
						GFP_KERNEL);
	if (!priv->dma_hwdescs)
		return -ENOMEM;


	/* Configure DMA hwdesc for foreground0 plane */
	dma_hwdesc_phys_f0 = priv->dma_hwdescs_phys
		+ offsetof(struct ingenic_dma_hwdescs, hwdesc_f0);
	priv->dma_hwdescs->hwdesc_f0.next = dma_hwdesc_phys_f0;
	priv->dma_hwdescs->hwdesc_f0.id = 0xf0;

	/* Configure DMA hwdesc for foreground1 plane */
	dma_hwdesc_phys_f1 = priv->dma_hwdescs_phys
		+ offsetof(struct ingenic_dma_hwdescs, hwdesc_f1);
	priv->dma_hwdescs->hwdesc_f1.next = dma_hwdesc_phys_f1;
	priv->dma_hwdescs->hwdesc_f1.id = 0xf1;

	if (soc_info->has_osd)
		priv->ipu_plane = drm_plane_from_index(drm, 0);

	drm_plane_helper_add(&priv->f1, &ingenic_drm_plane_helper_funcs);

	ret = drm_universal_plane_init(drm, &priv->f1, 1,
				       &ingenic_drm_primary_plane_funcs,
				       ingenic_drm_primary_formats,
				       ARRAY_SIZE(ingenic_drm_primary_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		dev_err(dev, "Failed to register plane: %i\n", ret);
		return ret;
	}

	drm_plane_enable_fb_damage_clips(&priv->f1);

	drm_crtc_helper_add(&priv->crtc, &ingenic_drm_crtc_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, &priv->crtc, &priv->f1,
					NULL, &ingenic_drm_crtc_funcs, NULL);
	if (ret) {
		dev_err(dev, "Failed to init CRTC: %i\n", ret);
		return ret;
	}

	if (soc_info->has_osd) {
		drm_plane_helper_add(&priv->f0,
				     &ingenic_drm_plane_helper_funcs);

		ret = drm_universal_plane_init(drm, &priv->f0, 1,
					       &ingenic_drm_primary_plane_funcs,
					       ingenic_drm_primary_formats,
					       ARRAY_SIZE(ingenic_drm_primary_formats),
					       NULL, DRM_PLANE_TYPE_OVERLAY,
					       NULL);
		if (ret) {
			dev_err(dev, "Failed to register overlay plane: %i\n",
				ret);
			return ret;
		}

		drm_plane_enable_fb_damage_clips(&priv->f0);

		if (IS_ENABLED(CONFIG_DRM_INGENIC_IPU) && has_components) {
			ret = component_bind_all(dev, drm);
			if (ret) {
				if (ret != -EPROBE_DEFER)
					dev_err(dev, "Failed to bind components: %i\n", ret);
				return ret;
			}

			ret = devm_add_action_or_reset(dev, ingenic_drm_unbind_all, priv);
			if (ret)
				return ret;

			priv->ipu_plane = drm_plane_from_index(drm, 2);
			if (!priv->ipu_plane) {
				dev_err(dev, "Failed to retrieve IPU plane\n");
				return -EINVAL;
			}
		}
	}

	for (i = 0; ; i++) {
		ret = drm_of_find_panel_or_bridge(dev->of_node, 0, i, &panel, &bridge);
		if (ret) {
			if (ret == -ENODEV)
				break; /* we're done */
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to get bridge handle\n");
			return ret;
		}

		if (panel)
			bridge = devm_drm_panel_bridge_add_typed(dev, panel,
								 DRM_MODE_CONNECTOR_DPI);

		encoder = devm_kzalloc(dev, sizeof(*encoder), GFP_KERNEL);
		if (!encoder)
			return -ENOMEM;

		encoder->possible_crtcs = 1;

		drm_encoder_helper_add(encoder, &ingenic_drm_encoder_helper_funcs);

		ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_DPI);
		if (ret) {
			dev_err(dev, "Failed to init encoder: %d\n", ret);
			return ret;
		}

		ret = drm_bridge_attach(encoder, bridge, NULL, 0);
		if (ret) {
			dev_err(dev, "Unable to attach bridge\n");
			return ret;
		}
	}

	drm_for_each_encoder(encoder, drm) {
		clone_mask |= BIT(drm_encoder_index(encoder));
	}

	drm_for_each_encoder(encoder, drm) {
		encoder->possible_clones = clone_mask;
	}

	ret = drm_irq_install(drm, irq);
	if (ret) {
		dev_err(dev, "Unable to install IRQ handler\n");
		return ret;
	}

	ret = drm_vblank_init(drm, 1);
	if (ret) {
		dev_err(dev, "Failed calling drm_vblank_init()\n");
		return ret;
	}

	drm_mode_config_reset(drm);

	ret = clk_prepare_enable(priv->pix_clk);
	if (ret) {
		dev_err(dev, "Unable to start pixel clock\n");
		return ret;
	}

	if (priv->lcd_clk) {
		parent_clk = clk_get_parent(priv->lcd_clk);
		parent_rate = clk_get_rate(parent_clk);

		/* LCD Device clock must be 3x the pixel clock for STN panels,
		 * or 1.5x the pixel clock for TFT panels. To avoid having to
		 * check for the LCD device clock everytime we do a mode change,
		 * we set the LCD device clock to the highest rate possible.
		 */
		ret = clk_set_rate(priv->lcd_clk, parent_rate);
		if (ret) {
			dev_err(dev, "Unable to set LCD clock rate\n");
			goto err_pixclk_disable;
		}

		ret = clk_prepare_enable(priv->lcd_clk);
		if (ret) {
			dev_err(dev, "Unable to start lcd clock\n");
			goto err_pixclk_disable;
		}
	}

	/* Set address of our DMA descriptor chain */
	regmap_write(priv->map, JZ_REG_LCD_DA0, dma_hwdesc_phys_f0);
	regmap_write(priv->map, JZ_REG_LCD_DA1, dma_hwdesc_phys_f1);

	/* Enable OSD if available */
	if (soc_info->has_osd)
		regmap_write(priv->map, JZ_REG_LCD_OSDC, JZ_LCD_OSDC_OSDEN);

	mutex_init(&priv->clk_mutex);
	priv->clock_nb.notifier_call = ingenic_drm_update_pixclk;

	parent_clk = clk_get_parent(priv->pix_clk);
	ret = clk_notifier_register(parent_clk, &priv->clock_nb);
	if (ret) {
		dev_err(dev, "Unable to register clock notifier\n");
		goto err_devclk_disable;
	}

	ret = drm_dev_register(drm, 0);
	if (ret) {
		dev_err(dev, "Failed to register DRM driver\n");
		goto err_clk_notifier_unregister;
	}

	drm_fbdev_generic_setup(drm, 32);

	return 0;

err_clk_notifier_unregister:
	clk_notifier_unregister(parent_clk, &priv->clock_nb);
err_devclk_disable:
	if (priv->lcd_clk)
		clk_disable_unprepare(priv->lcd_clk);
err_pixclk_disable:
	clk_disable_unprepare(priv->pix_clk);
	return ret;
}

static int ingenic_drm_bind_with_components(struct device *dev)
{
	return ingenic_drm_bind(dev, true);
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static void ingenic_drm_unbind(struct device *dev)
{
	struct ingenic_drm *priv = dev_get_drvdata(dev);
	struct clk *parent_clk = clk_get_parent(priv->pix_clk);

	clk_notifier_unregister(parent_clk, &priv->clock_nb);
	if (priv->lcd_clk)
		clk_disable_unprepare(priv->lcd_clk);
	clk_disable_unprepare(priv->pix_clk);

	drm_dev_unregister(&priv->drm);
	drm_atomic_helper_shutdown(&priv->drm);
}

static const struct component_master_ops ingenic_master_ops = {
	.bind = ingenic_drm_bind_with_components,
	.unbind = ingenic_drm_unbind,
};

static int ingenic_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	struct device_node *np;

	if (!IS_ENABLED(CONFIG_DRM_INGENIC_IPU))
		return ingenic_drm_bind(dev, false);

	/* IPU is at port address 8 */
	np = of_graph_get_remote_node(dev->of_node, 8, 0);
	if (!np)
		return ingenic_drm_bind(dev, false);

	drm_of_component_match_add(dev, &match, compare_of, np);
	of_node_put(np);

	return component_master_add_with_match(dev, &ingenic_master_ops, match);
}

static int ingenic_drm_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!IS_ENABLED(CONFIG_DRM_INGENIC_IPU))
		ingenic_drm_unbind(dev);
	else
		component_master_del(dev, &ingenic_master_ops);

	return 0;
}

static const struct jz_soc_info jz4740_soc_info = {
	.needs_dev_clk = true,
	.has_osd = false,
	.max_width = 800,
	.max_height = 600,
};

static const struct jz_soc_info jz4725b_soc_info = {
	.needs_dev_clk = false,
	.has_osd = true,
	.max_width = 800,
	.max_height = 600,
};

static const struct jz_soc_info jz4770_soc_info = {
	.needs_dev_clk = false,
	.has_osd = true,
	.max_width = 1280,
	.max_height = 720,
};

static const struct of_device_id ingenic_drm_of_match[] = {
	{ .compatible = "ingenic,jz4740-lcd", .data = &jz4740_soc_info },
	{ .compatible = "ingenic,jz4725b-lcd", .data = &jz4725b_soc_info },
	{ .compatible = "ingenic,jz4770-lcd", .data = &jz4770_soc_info },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ingenic_drm_of_match);

static struct platform_driver ingenic_drm_driver = {
	.driver = {
		.name = "ingenic-drm",
		.of_match_table = of_match_ptr(ingenic_drm_of_match),
	},
	.probe = ingenic_drm_probe,
	.remove = ingenic_drm_remove,
};

static int ingenic_drm_init(void)
{
	int err;

	if (IS_ENABLED(CONFIG_DRM_INGENIC_IPU)) {
		err = platform_driver_register(ingenic_ipu_driver_ptr);
		if (err)
			return err;
	}

	return platform_driver_register(&ingenic_drm_driver);
}
module_init(ingenic_drm_init);

static void ingenic_drm_exit(void)
{
	platform_driver_unregister(&ingenic_drm_driver);

	if (IS_ENABLED(CONFIG_DRM_INGENIC_IPU))
		platform_driver_unregister(ingenic_ipu_driver_ptr);
}
module_exit(ingenic_drm_exit);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("DRM driver for the Ingenic SoCs\n");
MODULE_LICENSE("GPL v2");
