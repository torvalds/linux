// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/component.h>
#include <linux/ioport.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <uapi/drm/drm_mode.h>

#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sun4i_lvds.h"
#include "sun4i_rgb.h"
#include "sun4i_tcon.h"
#include "sun6i_mipi_dsi.h"
#include "sun4i_tcon_dclk.h"
#include "sun8i_tcon_top.h"
#include "sunxi_engine.h"

static struct drm_connector *sun4i_tcon_get_connector(const struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;

	drm_connector_list_iter_begin(encoder->dev, &iter);
	drm_for_each_connector_iter(connector, &iter)
		if (connector->encoder == encoder) {
			drm_connector_list_iter_end(&iter);
			return connector;
		}
	drm_connector_list_iter_end(&iter);

	return NULL;
}

static int sun4i_tcon_get_pixel_depth(const struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct drm_display_info *info;

	connector = sun4i_tcon_get_connector(encoder);
	if (!connector)
		return -EINVAL;

	info = &connector->display_info;
	if (info->num_bus_formats != 1)
		return -EINVAL;

	switch (info->bus_formats[0]) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		return 18;

	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		return 24;
	}

	return -EINVAL;
}

static void sun4i_tcon_channel_set_status(struct sun4i_tcon *tcon, int channel,
					  bool enabled)
{
	struct clk *clk;

	switch (channel) {
	case 0:
		WARN_ON(!tcon->quirks->has_channel_0);
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_TCON_ENABLE,
				   enabled ? SUN4I_TCON0_CTL_TCON_ENABLE : 0);
		clk = tcon->dclk;
		break;
	case 1:
		WARN_ON(!tcon->quirks->has_channel_1);
		regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
				   SUN4I_TCON1_CTL_TCON_ENABLE,
				   enabled ? SUN4I_TCON1_CTL_TCON_ENABLE : 0);
		clk = tcon->sclk1;
		break;
	default:
		DRM_WARN("Unknown channel... doing nothing\n");
		return;
	}

	if (enabled) {
		clk_prepare_enable(clk);
		clk_rate_exclusive_get(clk);
	} else {
		clk_rate_exclusive_put(clk);
		clk_disable_unprepare(clk);
	}
}

static void sun4i_tcon_setup_lvds_phy(struct sun4i_tcon *tcon,
				      const struct drm_encoder *encoder)
{
	regmap_write(tcon->regs, SUN4I_TCON0_LVDS_ANA0_REG,
		     SUN4I_TCON0_LVDS_ANA0_CK_EN |
		     SUN4I_TCON0_LVDS_ANA0_REG_V |
		     SUN4I_TCON0_LVDS_ANA0_REG_C |
		     SUN4I_TCON0_LVDS_ANA0_EN_MB |
		     SUN4I_TCON0_LVDS_ANA0_PD |
		     SUN4I_TCON0_LVDS_ANA0_DCHS);

	udelay(2); /* delay at least 1200 ns */
	regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_ANA1_REG,
			   SUN4I_TCON0_LVDS_ANA1_INIT,
			   SUN4I_TCON0_LVDS_ANA1_INIT);
	udelay(1); /* delay at least 120 ns */
	regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_ANA1_REG,
			   SUN4I_TCON0_LVDS_ANA1_UPDATE,
			   SUN4I_TCON0_LVDS_ANA1_UPDATE);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_ANA0_REG,
			   SUN4I_TCON0_LVDS_ANA0_EN_MB,
			   SUN4I_TCON0_LVDS_ANA0_EN_MB);
}

static void sun6i_tcon_setup_lvds_phy(struct sun4i_tcon *tcon,
				      const struct drm_encoder *encoder)
{
	u8 val;

	regmap_write(tcon->regs, SUN4I_TCON0_LVDS_ANA0_REG,
		     SUN6I_TCON0_LVDS_ANA0_C(2) |
		     SUN6I_TCON0_LVDS_ANA0_V(3) |
		     SUN6I_TCON0_LVDS_ANA0_PD(2) |
		     SUN6I_TCON0_LVDS_ANA0_EN_LDO);
	udelay(2);

	regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_ANA0_REG,
			   SUN6I_TCON0_LVDS_ANA0_EN_MB,
			   SUN6I_TCON0_LVDS_ANA0_EN_MB);
	udelay(2);

	regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_ANA0_REG,
			   SUN6I_TCON0_LVDS_ANA0_EN_DRVC,
			   SUN6I_TCON0_LVDS_ANA0_EN_DRVC);

	if (sun4i_tcon_get_pixel_depth(encoder) == 18)
		val = 7;
	else
		val = 0xf;

	regmap_write_bits(tcon->regs, SUN4I_TCON0_LVDS_ANA0_REG,
			  SUN6I_TCON0_LVDS_ANA0_EN_DRVD(0xf),
			  SUN6I_TCON0_LVDS_ANA0_EN_DRVD(val));
}

static void sun4i_tcon_lvds_set_status(struct sun4i_tcon *tcon,
				       const struct drm_encoder *encoder,
				       bool enabled)
{
	if (enabled) {
		regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_IF_REG,
				   SUN4I_TCON0_LVDS_IF_EN,
				   SUN4I_TCON0_LVDS_IF_EN);
		if (tcon->quirks->setup_lvds_phy)
			tcon->quirks->setup_lvds_phy(tcon, encoder);
	} else {
		regmap_update_bits(tcon->regs, SUN4I_TCON0_LVDS_IF_REG,
				   SUN4I_TCON0_LVDS_IF_EN, 0);
	}
}

void sun4i_tcon_set_status(struct sun4i_tcon *tcon,
			   const struct drm_encoder *encoder,
			   bool enabled)
{
	bool is_lvds = false;
	int channel;

	switch (encoder->encoder_type) {
	case DRM_MODE_ENCODER_LVDS:
		is_lvds = true;
		fallthrough;
	case DRM_MODE_ENCODER_DSI:
	case DRM_MODE_ENCODER_NONE:
		channel = 0;
		break;
	case DRM_MODE_ENCODER_TMDS:
	case DRM_MODE_ENCODER_TVDAC:
		channel = 1;
		break;
	default:
		DRM_DEBUG_DRIVER("Unknown encoder type, doing nothing...\n");
		return;
	}

	if (is_lvds && !enabled)
		sun4i_tcon_lvds_set_status(tcon, encoder, false);

	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_TCON_ENABLE,
			   enabled ? SUN4I_TCON_GCTL_TCON_ENABLE : 0);

	if (is_lvds && enabled)
		sun4i_tcon_lvds_set_status(tcon, encoder, true);

	sun4i_tcon_channel_set_status(tcon, channel, enabled);
}

void sun4i_tcon_enable_vblank(struct sun4i_tcon *tcon, bool enable)
{
	u32 mask, val = 0;

	DRM_DEBUG_DRIVER("%sabling VBLANK interrupt\n", enable ? "En" : "Dis");

	mask = SUN4I_TCON_GINT0_VBLANK_ENABLE(0) |
		SUN4I_TCON_GINT0_VBLANK_ENABLE(1) |
		SUN4I_TCON_GINT0_TCON0_TRI_FINISH_ENABLE;

	if (enable)
		val = mask;

	regmap_update_bits(tcon->regs, SUN4I_TCON_GINT0_REG, mask, val);
}
EXPORT_SYMBOL(sun4i_tcon_enable_vblank);

/*
 * This function is a helper for TCON output muxing. The TCON output
 * muxing control register in earlier SoCs (without the TCON TOP block)
 * are located in TCON0. This helper returns a pointer to TCON0's
 * sun4i_tcon structure, or NULL if not found.
 */
static struct sun4i_tcon *sun4i_get_tcon0(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon;

	list_for_each_entry(tcon, &drv->tcon_list, list)
		if (tcon->id == 0)
			return tcon;

	dev_warn(drm->dev,
		 "TCON0 not found, display output muxing may not work\n");

	return NULL;
}

static void sun4i_tcon_set_mux(struct sun4i_tcon *tcon, int channel,
			       const struct drm_encoder *encoder)
{
	int ret = -ENOTSUPP;

	if (tcon->quirks->set_mux)
		ret = tcon->quirks->set_mux(tcon, encoder);

	DRM_DEBUG_DRIVER("Muxing encoder %s to CRTC %s: %d\n",
			 encoder->name, encoder->crtc->name, ret);
}

static int sun4i_tcon_get_clk_delay(const struct drm_display_mode *mode,
				    int channel)
{
	int delay = mode->vtotal - mode->vdisplay;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		delay /= 2;

	if (channel == 1)
		delay -= 2;

	delay = min(delay, 30);

	DRM_DEBUG_DRIVER("TCON %d clock delay %u\n", channel, delay);

	return delay;
}

static void sun4i_tcon0_mode_set_dithering(struct sun4i_tcon *tcon,
					   const struct drm_connector *connector)
{
	u32 bus_format = 0;
	u32 val = 0;

	/* XXX Would this ever happen? */
	if (!connector)
		return;

	/*
	 * FIXME: Undocumented bits
	 *
	 * The whole dithering process and these parameters are not
	 * explained in the vendor documents or BSP kernel code.
	 */
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_SEED_PR_REG, 0x11111111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_SEED_PG_REG, 0x11111111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_SEED_PB_REG, 0x11111111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_SEED_LR_REG, 0x11111111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_SEED_LG_REG, 0x11111111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_SEED_LB_REG, 0x11111111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_TBL0_REG, 0x01010000);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_TBL1_REG, 0x15151111);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_TBL2_REG, 0x57575555);
	regmap_write(tcon->regs, SUN4I_TCON0_FRM_TBL3_REG, 0x7f7f7777);

	/* Do dithering if panel only supports 6 bits per color */
	if (connector->display_info.bpc == 6)
		val |= SUN4I_TCON0_FRM_CTL_EN;

	if (connector->display_info.num_bus_formats == 1)
		bus_format = connector->display_info.bus_formats[0];

	/* Check the connection format */
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		/* R and B components are only 5 bits deep */
		val |= SUN4I_TCON0_FRM_CTL_MODE_R;
		val |= SUN4I_TCON0_FRM_CTL_MODE_B;
		fallthrough;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		/* Fall through: enable dithering */
		val |= SUN4I_TCON0_FRM_CTL_EN;
		break;
	}

	/* Write dithering settings */
	regmap_write(tcon->regs, SUN4I_TCON_FRM_CTL_REG, val);
}

static void sun4i_tcon0_mode_set_cpu(struct sun4i_tcon *tcon,
				     const struct drm_encoder *encoder,
				     const struct drm_display_mode *mode)
{
	/* TODO support normal CPU interface modes */
	struct sun6i_dsi *dsi = encoder_to_sun6i_dsi(encoder);
	struct mipi_dsi_device *device = dsi->device;
	u8 bpp = mipi_dsi_pixel_format_to_bpp(device->format);
	u8 lanes = device->lanes;
	u32 block_space, start_delay;
	u32 tcon_div;

	/*
	 * dclk is required to run at 1/4 the DSI per-lane bit rate.
	 */
	tcon->dclk_min_div = SUN6I_DSI_TCON_DIV;
	tcon->dclk_max_div = SUN6I_DSI_TCON_DIV;
	clk_set_rate(tcon->dclk, mode->crtc_clock * 1000 * (bpp / lanes)
						  / SUN6I_DSI_TCON_DIV);

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON0_BASIC0_Y(mode->crtc_vdisplay));

	/* Set dithering if needed */
	sun4i_tcon0_mode_set_dithering(tcon, sun4i_tcon_get_connector(encoder));

	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_IF_MASK,
			   SUN4I_TCON0_CTL_IF_8080);

	regmap_write(tcon->regs, SUN4I_TCON_ECC_FIFO_REG,
		     SUN4I_TCON_ECC_FIFO_EN);

	regmap_write(tcon->regs, SUN4I_TCON0_CPU_IF_REG,
		     SUN4I_TCON0_CPU_IF_MODE_DSI |
		     SUN4I_TCON0_CPU_IF_TRI_FIFO_FLUSH |
		     SUN4I_TCON0_CPU_IF_TRI_FIFO_EN |
		     SUN4I_TCON0_CPU_IF_TRI_EN);

	/*
	 * This looks suspicious, but it works...
	 *
	 * The datasheet says that this should be set higher than 20 *
	 * pixel cycle, but it's not clear what a pixel cycle is.
	 */
	regmap_read(tcon->regs, SUN4I_TCON0_DCLK_REG, &tcon_div);
	tcon_div &= GENMASK(6, 0);
	block_space = mode->htotal * bpp / (tcon_div * lanes);
	block_space -= mode->hdisplay + 40;

	regmap_write(tcon->regs, SUN4I_TCON0_CPU_TRI0_REG,
		     SUN4I_TCON0_CPU_TRI0_BLOCK_SPACE(block_space) |
		     SUN4I_TCON0_CPU_TRI0_BLOCK_SIZE(mode->hdisplay));

	regmap_write(tcon->regs, SUN4I_TCON0_CPU_TRI1_REG,
		     SUN4I_TCON0_CPU_TRI1_BLOCK_NUM(mode->vdisplay));

	start_delay = (mode->crtc_vtotal - mode->crtc_vdisplay - 10 - 1);
	start_delay = start_delay * mode->crtc_htotal * 149;
	start_delay = start_delay / (mode->crtc_clock / 1000) / 8;
	regmap_write(tcon->regs, SUN4I_TCON0_CPU_TRI2_REG,
		     SUN4I_TCON0_CPU_TRI2_TRANS_START_SET(10) |
		     SUN4I_TCON0_CPU_TRI2_START_DELAY(start_delay));

	/*
	 * The Allwinner BSP has a comment that the period should be
	 * the display clock * 15, but uses an hardcoded 3000...
	 */
	regmap_write(tcon->regs, SUN4I_TCON_SAFE_PERIOD_REG,
		     SUN4I_TCON_SAFE_PERIOD_NUM(3000) |
		     SUN4I_TCON_SAFE_PERIOD_MODE(3));

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG,
		     0xe0000000);
}

static void sun4i_tcon0_mode_set_lvds(struct sun4i_tcon *tcon,
				      const struct drm_encoder *encoder,
				      const struct drm_display_mode *mode)
{
	unsigned int bp;
	u8 clk_delay;
	u32 reg, val = 0;

	WARN_ON(!tcon->quirks->has_channel_0);

	tcon->dclk_min_div = 7;
	tcon->dclk_max_div = 7;
	clk_set_rate(tcon->dclk, mode->crtc_clock * 1000);

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON0_BASIC0_Y(mode->crtc_vdisplay));

	/* Set dithering if needed */
	sun4i_tcon0_mode_set_dithering(tcon, sun4i_tcon_get_connector(encoder));

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 0);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON0_CTL_CLK_DELAY(clk_delay));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the back porch + hsync
	 */
	bp = mode->crtc_htotal - mode->crtc_hsync_start;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->crtc_htotal, bp);

	/* Set horizontal display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC1_REG,
		     SUN4I_TCON0_BASIC1_H_TOTAL(mode->htotal) |
		     SUN4I_TCON0_BASIC1_H_BACKPORCH(bp));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the back porch + hsync
	 */
	bp = mode->crtc_vtotal - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->crtc_vtotal, bp);

	/* Set vertical display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC2_REG,
		     SUN4I_TCON0_BASIC2_V_TOTAL(mode->crtc_vtotal * 2) |
		     SUN4I_TCON0_BASIC2_V_BACKPORCH(bp));

	reg = SUN4I_TCON0_LVDS_IF_CLK_SEL_TCON0;
	if (sun4i_tcon_get_pixel_depth(encoder) == 24)
		reg |= SUN4I_TCON0_LVDS_IF_BITWIDTH_24BITS;
	else
		reg |= SUN4I_TCON0_LVDS_IF_BITWIDTH_18BITS;

	regmap_write(tcon->regs, SUN4I_TCON0_LVDS_IF_REG, reg);

	/* Setup the polarity of the various signals */
	if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
		val |= SUN4I_TCON0_IO_POL_HSYNC_POSITIVE;

	if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
		val |= SUN4I_TCON0_IO_POL_VSYNC_POSITIVE;

	regmap_write(tcon->regs, SUN4I_TCON0_IO_POL_REG, val);

	/* Map output pins to channel 0 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON0);

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, 0xe0000000);
}

static void sun4i_tcon0_mode_set_rgb(struct sun4i_tcon *tcon,
				     const struct drm_encoder *encoder,
				     const struct drm_display_mode *mode)
{
	struct drm_connector *connector = sun4i_tcon_get_connector(encoder);
	const struct drm_display_info *info = &connector->display_info;
	unsigned int bp, hsync, vsync;
	u8 clk_delay;
	u32 val = 0;

	WARN_ON(!tcon->quirks->has_channel_0);

	tcon->dclk_min_div = tcon->quirks->dclk_min_div;
	tcon->dclk_max_div = 127;
	clk_set_rate(tcon->dclk, mode->crtc_clock * 1000);

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON0_BASIC0_Y(mode->crtc_vdisplay));

	/* Set dithering if needed */
	sun4i_tcon0_mode_set_dithering(tcon, connector);

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 0);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON0_CTL_CLK_DELAY(clk_delay));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the back porch + hsync
	 */
	bp = mode->crtc_htotal - mode->crtc_hsync_start;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->crtc_htotal, bp);

	/* Set horizontal display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC1_REG,
		     SUN4I_TCON0_BASIC1_H_TOTAL(mode->crtc_htotal) |
		     SUN4I_TCON0_BASIC1_H_BACKPORCH(bp));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the back porch + hsync
	 */
	bp = mode->crtc_vtotal - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->crtc_vtotal, bp);

	/* Set vertical display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC2_REG,
		     SUN4I_TCON0_BASIC2_V_TOTAL(mode->crtc_vtotal * 2) |
		     SUN4I_TCON0_BASIC2_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting HSYNC %d, VSYNC %d\n", hsync, vsync);
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC3_REG,
		     SUN4I_TCON0_BASIC3_V_SYNC(vsync) |
		     SUN4I_TCON0_BASIC3_H_SYNC(hsync));

	/* Setup the polarity of the various signals */
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= SUN4I_TCON0_IO_POL_HSYNC_POSITIVE;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= SUN4I_TCON0_IO_POL_VSYNC_POSITIVE;

	if (info->bus_flags & DRM_BUS_FLAG_DE_LOW)
		val |= SUN4I_TCON0_IO_POL_DE_NEGATIVE;

	if (info->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		val |= SUN4I_TCON0_IO_POL_DCLK_DRIVE_NEGEDGE;

	regmap_update_bits(tcon->regs, SUN4I_TCON0_IO_POL_REG,
			   SUN4I_TCON0_IO_POL_HSYNC_POSITIVE |
			   SUN4I_TCON0_IO_POL_VSYNC_POSITIVE |
			   SUN4I_TCON0_IO_POL_DCLK_DRIVE_NEGEDGE |
			   SUN4I_TCON0_IO_POL_DE_NEGATIVE,
			   val);

	/* Map output pins to channel 0 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON0);

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, 0);
}

static void sun4i_tcon1_mode_set(struct sun4i_tcon *tcon,
				 const struct drm_display_mode *mode)
{
	unsigned int bp, hsync, vsync, vtotal;
	u8 clk_delay;
	u32 val;

	WARN_ON(!tcon->quirks->has_channel_1);

	/* Configure the dot clock */
	clk_set_rate(tcon->sclk1, mode->crtc_clock * 1000);

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 1);
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON1_CTL_CLK_DELAY(clk_delay));

	/* Set interlaced mode */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		val = SUN4I_TCON1_CTL_INTERLACE_ENABLE;
	else
		val = 0;
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_INTERLACE_ENABLE,
			   val);

	/* Set the input resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC0_REG,
		     SUN4I_TCON1_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC0_Y(mode->crtc_vdisplay));

	/* Set the upscaling resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC1_REG,
		     SUN4I_TCON1_BASIC1_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC1_Y(mode->crtc_vdisplay));

	/* Set the output resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC2_REG,
		     SUN4I_TCON1_BASIC2_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC2_Y(mode->crtc_vdisplay));

	/* Set horizontal display timings */
	bp = mode->crtc_htotal - mode->crtc_hsync_start;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->htotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC3_REG,
		     SUN4I_TCON1_BASIC3_H_TOTAL(mode->crtc_htotal) |
		     SUN4I_TCON1_BASIC3_H_BACKPORCH(bp));

	bp = mode->crtc_vtotal - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->crtc_vtotal, bp);

	/*
	 * The vertical resolution needs to be doubled in all
	 * cases. We could use crtc_vtotal and always multiply by two,
	 * but that leads to a rounding error in interlace when vtotal
	 * is odd.
	 *
	 * This happens with TV's PAL for example, where vtotal will
	 * be 625, crtc_vtotal 312, and thus crtc_vtotal * 2 will be
	 * 624, which apparently confuses the hardware.
	 *
	 * To work around this, we will always use vtotal, and
	 * multiply by two only if we're not in interlace.
	 */
	vtotal = mode->vtotal;
	if (!(mode->flags & DRM_MODE_FLAG_INTERLACE))
		vtotal = vtotal * 2;

	/* Set vertical display timings */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC4_REG,
		     SUN4I_TCON1_BASIC4_V_TOTAL(vtotal) |
		     SUN4I_TCON1_BASIC4_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting HSYNC %d, VSYNC %d\n", hsync, vsync);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC5_REG,
		     SUN4I_TCON1_BASIC5_V_SYNC(vsync) |
		     SUN4I_TCON1_BASIC5_H_SYNC(hsync));

	/* Setup the polarity of multiple signals */
	if (tcon->quirks->polarity_in_ch0) {
		val = 0;

		if (mode->flags & DRM_MODE_FLAG_PHSYNC)
			val |= SUN4I_TCON0_IO_POL_HSYNC_POSITIVE;

		if (mode->flags & DRM_MODE_FLAG_PVSYNC)
			val |= SUN4I_TCON0_IO_POL_VSYNC_POSITIVE;

		regmap_write(tcon->regs, SUN4I_TCON0_IO_POL_REG, val);
	} else {
		/* according to vendor driver, this bit must be always set */
		val = SUN4I_TCON1_IO_POL_UNKNOWN;

		if (mode->flags & DRM_MODE_FLAG_PHSYNC)
			val |= SUN4I_TCON1_IO_POL_HSYNC_POSITIVE;

		if (mode->flags & DRM_MODE_FLAG_PVSYNC)
			val |= SUN4I_TCON1_IO_POL_VSYNC_POSITIVE;

		regmap_write(tcon->regs, SUN4I_TCON1_IO_POL_REG, val);
	}

	/* Map output pins to channel 1 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON1);
}

void sun4i_tcon_mode_set(struct sun4i_tcon *tcon,
			 const struct drm_encoder *encoder,
			 const struct drm_display_mode *mode)
{
	switch (encoder->encoder_type) {
	case DRM_MODE_ENCODER_DSI:
		/* DSI is tied to special case of CPU interface */
		sun4i_tcon0_mode_set_cpu(tcon, encoder, mode);
		break;
	case DRM_MODE_ENCODER_LVDS:
		sun4i_tcon0_mode_set_lvds(tcon, encoder, mode);
		break;
	case DRM_MODE_ENCODER_NONE:
		sun4i_tcon0_mode_set_rgb(tcon, encoder, mode);
		sun4i_tcon_set_mux(tcon, 0, encoder);
		break;
	case DRM_MODE_ENCODER_TVDAC:
	case DRM_MODE_ENCODER_TMDS:
		sun4i_tcon1_mode_set(tcon, mode);
		sun4i_tcon_set_mux(tcon, 1, encoder);
		break;
	default:
		DRM_DEBUG_DRIVER("Unknown encoder type, doing nothing...\n");
	}
}
EXPORT_SYMBOL(sun4i_tcon_mode_set);

static void sun4i_tcon_finish_page_flip(struct drm_device *dev,
					struct sun4i_crtc *scrtc)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (scrtc->event) {
		drm_crtc_send_vblank_event(&scrtc->crtc, scrtc->event);
		drm_crtc_vblank_put(&scrtc->crtc);
		scrtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static irqreturn_t sun4i_tcon_handler(int irq, void *private)
{
	struct sun4i_tcon *tcon = private;
	struct drm_device *drm = tcon->drm;
	struct sun4i_crtc *scrtc = tcon->crtc;
	struct sunxi_engine *engine = scrtc->engine;
	unsigned int status;

	regmap_read(tcon->regs, SUN4I_TCON_GINT0_REG, &status);

	if (!(status & (SUN4I_TCON_GINT0_VBLANK_INT(0) |
			SUN4I_TCON_GINT0_VBLANK_INT(1) |
			SUN4I_TCON_GINT0_TCON0_TRI_FINISH_INT)))
		return IRQ_NONE;

	drm_crtc_handle_vblank(&scrtc->crtc);
	sun4i_tcon_finish_page_flip(drm, scrtc);

	/* Acknowledge the interrupt */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GINT0_REG,
			   SUN4I_TCON_GINT0_VBLANK_INT(0) |
			   SUN4I_TCON_GINT0_VBLANK_INT(1) |
			   SUN4I_TCON_GINT0_TCON0_TRI_FINISH_INT,
			   0);

	if (engine->ops->vblank_quirk)
		engine->ops->vblank_quirk(engine);

	return IRQ_HANDLED;
}

static int sun4i_tcon_init_clocks(struct device *dev,
				  struct sun4i_tcon *tcon)
{
	tcon->clk = devm_clk_get_enabled(dev, "ahb");
	if (IS_ERR(tcon->clk)) {
		dev_err(dev, "Couldn't get the TCON bus clock\n");
		return PTR_ERR(tcon->clk);
	}

	if (tcon->quirks->has_channel_0) {
		tcon->sclk0 = devm_clk_get_enabled(dev, "tcon-ch0");
		if (IS_ERR(tcon->sclk0)) {
			dev_err(dev, "Couldn't get the TCON channel 0 clock\n");
			return PTR_ERR(tcon->sclk0);
		}
	}

	if (tcon->quirks->has_channel_1) {
		tcon->sclk1 = devm_clk_get(dev, "tcon-ch1");
		if (IS_ERR(tcon->sclk1)) {
			dev_err(dev, "Couldn't get the TCON channel 1 clock\n");
			return PTR_ERR(tcon->sclk1);
		}
	}

	return 0;
}

static int sun4i_tcon_init_irq(struct device *dev,
			       struct sun4i_tcon *tcon)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, sun4i_tcon_handler, 0,
			       dev_name(dev), tcon);
	if (ret) {
		dev_err(dev, "Couldn't request the IRQ\n");
		return ret;
	}

	return 0;
}

static const struct regmap_config sun4i_tcon_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x800,
};

static int sun4i_tcon_init_regmap(struct device *dev,
				  struct sun4i_tcon *tcon)
{
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *regs;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	tcon->regs = devm_regmap_init_mmio(dev, regs,
					   &sun4i_tcon_regmap_config);
	if (IS_ERR(tcon->regs)) {
		dev_err(dev, "Couldn't create the TCON regmap\n");
		return PTR_ERR(tcon->regs);
	}

	/* Make sure the TCON is disabled and all IRQs are off */
	regmap_write(tcon->regs, SUN4I_TCON_GCTL_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT0_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT1_REG, 0);

	/* Disable IO lines and set them to tristate */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, ~0);
	regmap_write(tcon->regs, SUN4I_TCON1_IO_TRI_REG, ~0);

	return 0;
}

/*
 * On SoCs with the old display pipeline design (Display Engine 1.0),
 * the TCON is always tied to just one backend. Hence we can traverse
 * the of_graph upwards to find the backend our tcon is connected to,
 * and take its ID as our own.
 *
 * We can either identify backends from their compatible strings, which
 * means maintaining a large list of them. Or, since the backend is
 * registered and binded before the TCON, we can just go through the
 * list of registered backends and compare the device node.
 *
 * As the structures now store engines instead of backends, here this
 * function in fact searches the corresponding engine, and the ID is
 * requested via the get_id function of the engine.
 */
static struct sunxi_engine *
sun4i_tcon_find_engine_traverse(struct sun4i_drv *drv,
				struct device_node *node,
				u32 port_id)
{
	struct device_node *port, *ep, *remote;
	struct sunxi_engine *engine = ERR_PTR(-EINVAL);
	u32 reg = 0;

	port = of_graph_get_port_by_id(node, port_id);
	if (!port)
		return ERR_PTR(-EINVAL);

	/*
	 * This only works if there is only one path from the TCON
	 * to any display engine. Otherwise the probe order of the
	 * TCONs and display engines is not guaranteed. They may
	 * either bind to the wrong one, or worse, bind to the same
	 * one if additional checks are not done.
	 *
	 * Bail out if there are multiple input connections.
	 */
	if (of_get_available_child_count(port) != 1)
		goto out_put_port;

	/* Get the first connection without specifying an ID */
	ep = of_get_next_available_child(port, NULL);
	if (!ep)
		goto out_put_port;

	remote = of_graph_get_remote_port_parent(ep);
	if (!remote)
		goto out_put_ep;

	/* does this node match any registered engines? */
	list_for_each_entry(engine, &drv->engine_list, list)
		if (remote == engine->node)
			goto out_put_remote;

	/*
	 * According to device tree binding input ports have even id
	 * number and output ports have odd id. Since component with
	 * more than one input and one output (TCON TOP) exits, correct
	 * remote input id has to be calculated by subtracting 1 from
	 * remote output id. If this for some reason can't be done, 0
	 * is used as input port id.
	 */
	of_node_put(port);
	port = of_graph_get_remote_port(ep);
	if (!of_property_read_u32(port, "reg", &reg) && reg > 0)
		reg -= 1;

	/* keep looking through upstream ports */
	engine = sun4i_tcon_find_engine_traverse(drv, remote, reg);

out_put_remote:
	of_node_put(remote);
out_put_ep:
	of_node_put(ep);
out_put_port:
	of_node_put(port);

	return engine;
}

/*
 * The device tree binding says that the remote endpoint ID of any
 * connection between components, up to and including the TCON, of
 * the display pipeline should be equal to the actual ID of the local
 * component. Thus we can look at any one of the input connections of
 * the TCONs, and use that connection's remote endpoint ID as our own.
 *
 * Since the user of this function already finds the input port,
 * the port is passed in directly without further checks.
 */
static int sun4i_tcon_of_get_id_from_port(struct device_node *port)
{
	struct device_node *ep;
	int ret = -EINVAL;

	/* try finding an upstream endpoint */
	for_each_available_child_of_node(port, ep) {
		struct device_node *remote;
		u32 reg;

		remote = of_graph_get_remote_endpoint(ep);
		if (!remote)
			continue;

		ret = of_property_read_u32(remote, "reg", &reg);
		if (ret)
			continue;

		ret = reg;
	}

	return ret;
}

/*
 * Once we know the TCON's id, we can look through the list of
 * engines to find a matching one. We assume all engines have
 * been probed and added to the list.
 */
static struct sunxi_engine *sun4i_tcon_get_engine_by_id(struct sun4i_drv *drv,
							int id)
{
	struct sunxi_engine *engine;

	list_for_each_entry(engine, &drv->engine_list, list)
		if (engine->id == id)
			return engine;

	return ERR_PTR(-EINVAL);
}

static bool sun4i_tcon_connected_to_tcon_top(struct device_node *node)
{
	struct device_node *remote;
	bool ret = false;

	remote = of_graph_get_remote_node(node, 0, -1);
	if (remote) {
		ret = !!(IS_ENABLED(CONFIG_DRM_SUN8I_TCON_TOP) &&
			 of_match_node(sun8i_tcon_top_of_table, remote));
		of_node_put(remote);
	}

	return ret;
}

static int sun4i_tcon_get_index(struct sun4i_drv *drv)
{
	struct list_head *pos;
	int size = 0;

	/*
	 * Because TCON is added to the list at the end of the probe
	 * (after this function is called), index of the current TCON
	 * will be same as current TCON list size.
	 */
	list_for_each(pos, &drv->tcon_list)
		++size;

	return size;
}

/*
 * On SoCs with the old display pipeline design (Display Engine 1.0),
 * we assumed the TCON was always tied to just one backend. However
 * this proved not to be the case. On the A31, the TCON can select
 * either backend as its source. On the A20 (and likely on the A10),
 * the backend can choose which TCON to output to.
 *
 * The device tree binding says that the remote endpoint ID of any
 * connection between components, up to and including the TCON, of
 * the display pipeline should be equal to the actual ID of the local
 * component. Thus we should be able to look at any one of the input
 * connections of the TCONs, and use that connection's remote endpoint
 * ID as our own.
 *
 * However  the connections between the backend and TCON were assumed
 * to be always singular, and their endpoit IDs were all incorrectly
 * set to 0. This means for these old device trees, we cannot just look
 * up the remote endpoint ID of a TCON input endpoint. TCON1 would be
 * incorrectly identified as TCON0.
 *
 * This function first checks if the TCON node has 2 input endpoints.
 * If so, then the device tree is a corrected version, and it will use
 * sun4i_tcon_of_get_id() and sun4i_tcon_get_engine_by_id() from above
 * to fetch the ID and engine directly. If not, then it is likely an
 * old device trees, where the endpoint IDs were incorrect, but did not
 * have endpoint connections between the backend and TCON across
 * different display pipelines. It will fall back to the old method of
 * traversing the  of_graph to try and find a matching engine by device
 * node.
 *
 * In the case of single display pipeline device trees, either method
 * works.
 */
static struct sunxi_engine *sun4i_tcon_find_engine(struct sun4i_drv *drv,
						   struct device_node *node)
{
	struct device_node *port;
	struct sunxi_engine *engine;

	port = of_graph_get_port_by_id(node, 0);
	if (!port)
		return ERR_PTR(-EINVAL);

	/*
	 * Is this a corrected device tree with cross pipeline
	 * connections between the backend and TCON?
	 */
	if (of_get_child_count(port) > 1) {
		int id;

		/*
		 * When pipeline has the same number of TCONs and engines which
		 * are represented by frontends/backends (DE1) or mixers (DE2),
		 * we match them by their respective IDs. However, if pipeline
		 * contains TCON TOP, chances are that there are either more
		 * TCONs than engines (R40) or TCONs with non-consecutive ids.
		 * (H6). In that case it's easier just use TCON index in list
		 * as an id. That means that on R40, any 2 TCONs can be enabled
		 * in DT out of 4 (there are 2 mixers). Due to the design of
		 * TCON TOP, remaining 2 TCONs can't be connected to anything
		 * anyway.
		 */
		if (sun4i_tcon_connected_to_tcon_top(node))
			id = sun4i_tcon_get_index(drv);
		else
			id = sun4i_tcon_of_get_id_from_port(port);

		/* Get our engine by matching our ID */
		engine = sun4i_tcon_get_engine_by_id(drv, id);

		of_node_put(port);
		return engine;
	}

	/* Fallback to old method by traversing input endpoints */
	of_node_put(port);
	return sun4i_tcon_find_engine_traverse(drv, node, 0);
}

static int sun4i_tcon_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sunxi_engine *engine;
	struct device_node *remote;
	struct sun4i_tcon *tcon;
	struct reset_control *edp_rstc;
	bool has_lvds_rst, has_lvds_alt, can_lvds;
	int ret;

	engine = sun4i_tcon_find_engine(drv, dev->of_node);
	if (IS_ERR(engine)) {
		dev_err(dev, "Couldn't find matching engine\n");
		return -EPROBE_DEFER;
	}

	tcon = devm_kzalloc(dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		return -ENOMEM;
	dev_set_drvdata(dev, tcon);
	tcon->drm = drm;
	tcon->dev = dev;
	tcon->id = engine->id;
	tcon->quirks = of_device_get_match_data(dev);

	tcon->lcd_rst = devm_reset_control_get(dev, "lcd");
	if (IS_ERR(tcon->lcd_rst)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(tcon->lcd_rst);
	}

	if (tcon->quirks->needs_edp_reset) {
		edp_rstc = devm_reset_control_get_shared(dev, "edp");
		if (IS_ERR(edp_rstc)) {
			dev_err(dev, "Couldn't get edp reset line\n");
			return PTR_ERR(edp_rstc);
		}

		ret = reset_control_deassert(edp_rstc);
		if (ret) {
			dev_err(dev, "Couldn't deassert edp reset line\n");
			return ret;
		}
	}

	/* Make sure our TCON is reset */
	ret = reset_control_reset(tcon->lcd_rst);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	if (tcon->quirks->supports_lvds) {
		/*
		 * This can only be made optional since we've had DT
		 * nodes without the LVDS reset properties.
		 *
		 * If the property is missing, just disable LVDS, and
		 * print a warning.
		 */
		tcon->lvds_rst = devm_reset_control_get_optional(dev, "lvds");
		if (IS_ERR(tcon->lvds_rst)) {
			dev_err(dev, "Couldn't get our reset line\n");
			return PTR_ERR(tcon->lvds_rst);
		} else if (tcon->lvds_rst) {
			has_lvds_rst = true;
			reset_control_reset(tcon->lvds_rst);
		} else {
			has_lvds_rst = false;
		}

		/*
		 * This can only be made optional since we've had DT
		 * nodes without the LVDS reset properties.
		 *
		 * If the property is missing, just disable LVDS, and
		 * print a warning.
		 */
		if (tcon->quirks->has_lvds_alt) {
			tcon->lvds_pll = devm_clk_get(dev, "lvds-alt");
			if (IS_ERR(tcon->lvds_pll)) {
				if (PTR_ERR(tcon->lvds_pll) == -ENOENT) {
					has_lvds_alt = false;
				} else {
					dev_err(dev, "Couldn't get the LVDS PLL\n");
					return PTR_ERR(tcon->lvds_pll);
				}
			} else {
				has_lvds_alt = true;
			}
		}

		if (!has_lvds_rst ||
		    (tcon->quirks->has_lvds_alt && !has_lvds_alt)) {
			dev_warn(dev, "Missing LVDS properties, Please upgrade your DT\n");
			dev_warn(dev, "LVDS output disabled\n");
			can_lvds = false;
		} else {
			can_lvds = true;
		}
	} else {
		can_lvds = false;
	}

	ret = sun4i_tcon_init_clocks(dev, tcon);
	if (ret) {
		dev_err(dev, "Couldn't init our TCON clocks\n");
		goto err_assert_reset;
	}

	ret = sun4i_tcon_init_regmap(dev, tcon);
	if (ret) {
		dev_err(dev, "Couldn't init our TCON regmap\n");
		goto err_assert_reset;
	}

	if (tcon->quirks->has_channel_0) {
		ret = sun4i_dclk_create(dev, tcon);
		if (ret) {
			dev_err(dev, "Couldn't create our TCON dot clock\n");
			goto err_assert_reset;
		}
	}

	ret = sun4i_tcon_init_irq(dev, tcon);
	if (ret) {
		dev_err(dev, "Couldn't init our TCON interrupts\n");
		goto err_free_dclk;
	}

	tcon->crtc = sun4i_crtc_init(drm, engine, tcon);
	if (IS_ERR(tcon->crtc)) {
		dev_err(dev, "Couldn't create our CRTC\n");
		ret = PTR_ERR(tcon->crtc);
		goto err_free_dclk;
	}

	if (tcon->quirks->has_channel_0) {
		/*
		 * If we have an LVDS panel connected to the TCON, we should
		 * just probe the LVDS connector. Otherwise, just probe RGB as
		 * we used to.
		 */
		remote = of_graph_get_remote_node(dev->of_node, 1, 0);
		if (of_device_is_compatible(remote, "panel-lvds"))
			if (can_lvds)
				ret = sun4i_lvds_init(drm, tcon);
			else
				ret = -EINVAL;
		else
			ret = sun4i_rgb_init(drm, tcon);
		of_node_put(remote);

		if (ret < 0)
			goto err_free_dclk;
	}

	if (tcon->quirks->needs_de_be_mux) {
		/*
		 * We assume there is no dynamic muxing of backends
		 * and TCONs, so we select the backend with same ID.
		 *
		 * While dynamic selection might be interesting, since
		 * the CRTC is tied to the TCON, while the layers are
		 * tied to the backends, this means, we will need to
		 * switch between groups of layers. There might not be
		 * a way to represent this constraint in DRM.
		 */
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_SRC_SEL_MASK,
				   tcon->id);
		regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
				   SUN4I_TCON1_CTL_SRC_SEL_MASK,
				   tcon->id);
	}

	list_add_tail(&tcon->list, &drv->tcon_list);

	return 0;

err_free_dclk:
	if (tcon->quirks->has_channel_0)
		sun4i_dclk_free(tcon);
err_assert_reset:
	reset_control_assert(tcon->lcd_rst);
	return ret;
}

static void sun4i_tcon_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct sun4i_tcon *tcon = dev_get_drvdata(dev);

	list_del(&tcon->list);
	if (tcon->quirks->has_channel_0)
		sun4i_dclk_free(tcon);
}

static const struct component_ops sun4i_tcon_ops = {
	.bind	= sun4i_tcon_bind,
	.unbind	= sun4i_tcon_unbind,
};

static int sun4i_tcon_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct sun4i_tcon_quirks *quirks;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	quirks = of_device_get_match_data(&pdev->dev);

	/* panels and bridges are present only on TCONs with channel 0 */
	if (quirks->has_channel_0) {
		ret = drm_of_find_panel_or_bridge(node, 1, 0, &panel, &bridge);
		if (ret == -EPROBE_DEFER)
			return ret;
	}

	return component_add(&pdev->dev, &sun4i_tcon_ops);
}

static void sun4i_tcon_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_tcon_ops);
}

/* platform specific TCON muxing callbacks */
static int sun4i_a10_tcon_set_mux(struct sun4i_tcon *tcon,
				  const struct drm_encoder *encoder)
{
	struct sun4i_tcon *tcon0 = sun4i_get_tcon0(encoder->dev);
	u32 shift;

	if (!tcon0)
		return -EINVAL;

	switch (encoder->encoder_type) {
	case DRM_MODE_ENCODER_TMDS:
		/* HDMI */
		shift = 8;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(tcon0->regs, SUN4I_TCON_MUX_CTRL_REG,
			   0x3 << shift, tcon->id << shift);

	return 0;
}

static int sun5i_a13_tcon_set_mux(struct sun4i_tcon *tcon,
				  const struct drm_encoder *encoder)
{
	u32 val;

	if (encoder->encoder_type == DRM_MODE_ENCODER_TVDAC)
		val = 1;
	else
		val = 0;

	/*
	 * FIXME: Undocumented bits
	 */
	return regmap_write(tcon->regs, SUN4I_TCON_MUX_CTRL_REG, val);
}

static int sun6i_tcon_set_mux(struct sun4i_tcon *tcon,
			      const struct drm_encoder *encoder)
{
	struct sun4i_tcon *tcon0 = sun4i_get_tcon0(encoder->dev);
	u32 shift;

	if (!tcon0)
		return -EINVAL;

	switch (encoder->encoder_type) {
	case DRM_MODE_ENCODER_TMDS:
		/* HDMI */
		shift = 8;
		break;
	default:
		/* TODO A31 has MIPI DSI but A31s does not */
		return -EINVAL;
	}

	regmap_update_bits(tcon0->regs, SUN4I_TCON_MUX_CTRL_REG,
			   0x3 << shift, tcon->id << shift);

	return 0;
}

static int sun8i_r40_tcon_tv_set_mux(struct sun4i_tcon *tcon,
				     const struct drm_encoder *encoder)
{
	struct device_node *port, *remote;
	struct platform_device *pdev;
	int id, ret;

	/* find TCON TOP platform device and TCON id */

	port = of_graph_get_port_by_id(tcon->dev->of_node, 0);
	if (!port)
		return -EINVAL;

	id = sun4i_tcon_of_get_id_from_port(port);
	of_node_put(port);

	remote = of_graph_get_remote_node(tcon->dev->of_node, 0, -1);
	if (!remote)
		return -EINVAL;

	pdev = of_find_device_by_node(remote);
	of_node_put(remote);
	if (!pdev)
		return -EINVAL;

	if (IS_ENABLED(CONFIG_DRM_SUN8I_TCON_TOP) &&
	    encoder->encoder_type == DRM_MODE_ENCODER_TMDS) {
		ret = sun8i_tcon_top_set_hdmi_src(&pdev->dev, id);
		if (ret) {
			put_device(&pdev->dev);
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_DRM_SUN8I_TCON_TOP)) {
		ret = sun8i_tcon_top_de_config(&pdev->dev, tcon->id, id);
		if (ret) {
			put_device(&pdev->dev);
			return ret;
		}
	}

	return 0;
}

static const struct sun4i_tcon_quirks sun4i_a10_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	.set_mux		= sun4i_a10_tcon_set_mux,
};

static const struct sun4i_tcon_quirks sun5i_a13_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	.set_mux		= sun5i_a13_tcon_set_mux,
};

static const struct sun4i_tcon_quirks sun6i_a31_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.has_lvds_alt		= true,
	.needs_de_be_mux	= true,
	.dclk_min_div		= 1,
	.set_mux		= sun6i_tcon_set_mux,
};

static const struct sun4i_tcon_quirks sun6i_a31s_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.needs_de_be_mux	= true,
	.dclk_min_div		= 1,
};

static const struct sun4i_tcon_quirks sun7i_a20_tcon0_quirks = {
	.supports_lvds		= true,
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	/* Same display pipeline structure as A10 */
	.set_mux		= sun4i_a10_tcon_set_mux,
	.setup_lvds_phy		= sun4i_tcon_setup_lvds_phy,
};

static const struct sun4i_tcon_quirks sun7i_a20_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	/* Same display pipeline structure as A10 */
	.set_mux		= sun4i_a10_tcon_set_mux,
};

static const struct sun4i_tcon_quirks sun8i_a33_quirks = {
	.has_channel_0		= true,
	.has_lvds_alt		= true,
	.dclk_min_div		= 1,
	.setup_lvds_phy		= sun6i_tcon_setup_lvds_phy,
	.supports_lvds		= true,
};

static const struct sun4i_tcon_quirks sun8i_a83t_lcd_quirks = {
	.supports_lvds		= true,
	.has_channel_0		= true,
	.dclk_min_div		= 1,
	.setup_lvds_phy		= sun6i_tcon_setup_lvds_phy,
};

static const struct sun4i_tcon_quirks sun8i_a83t_tv_quirks = {
	.has_channel_1		= true,
};

static const struct sun4i_tcon_quirks sun8i_r40_tv_quirks = {
	.has_channel_1		= true,
	.polarity_in_ch0	= true,
	.set_mux		= sun8i_r40_tcon_tv_set_mux,
};

static const struct sun4i_tcon_quirks sun8i_v3s_quirks = {
	.has_channel_0		= true,
	.dclk_min_div		= 1,
};

static const struct sun4i_tcon_quirks sun9i_a80_tcon_lcd_quirks = {
	.has_channel_0		= true,
	.needs_edp_reset	= true,
	.dclk_min_div		= 1,
};

static const struct sun4i_tcon_quirks sun9i_a80_tcon_tv_quirks = {
	.has_channel_1	= true,
	.needs_edp_reset = true,
};

static const struct sun4i_tcon_quirks sun20i_d1_lcd_quirks = {
	.has_channel_0		= true,
	.dclk_min_div		= 1,
	.set_mux		= sun8i_r40_tcon_tv_set_mux,
};

/* sun4i_drv uses this list to check if a device node is a TCON */
const struct of_device_id sun4i_tcon_of_table[] = {
	{ .compatible = "allwinner,sun4i-a10-tcon", .data = &sun4i_a10_quirks },
	{ .compatible = "allwinner,sun5i-a13-tcon", .data = &sun5i_a13_quirks },
	{ .compatible = "allwinner,sun6i-a31-tcon", .data = &sun6i_a31_quirks },
	{ .compatible = "allwinner,sun6i-a31s-tcon", .data = &sun6i_a31s_quirks },
	{ .compatible = "allwinner,sun7i-a20-tcon", .data = &sun7i_a20_quirks },
	{ .compatible = "allwinner,sun7i-a20-tcon0", .data = &sun7i_a20_tcon0_quirks },
	{ .compatible = "allwinner,sun7i-a20-tcon1", .data = &sun7i_a20_quirks },
	{ .compatible = "allwinner,sun8i-a23-tcon", .data = &sun8i_a33_quirks },
	{ .compatible = "allwinner,sun8i-a33-tcon", .data = &sun8i_a33_quirks },
	{ .compatible = "allwinner,sun8i-a83t-tcon-lcd", .data = &sun8i_a83t_lcd_quirks },
	{ .compatible = "allwinner,sun8i-a83t-tcon-tv", .data = &sun8i_a83t_tv_quirks },
	{ .compatible = "allwinner,sun8i-r40-tcon-tv", .data = &sun8i_r40_tv_quirks },
	{ .compatible = "allwinner,sun8i-v3s-tcon", .data = &sun8i_v3s_quirks },
	{ .compatible = "allwinner,sun9i-a80-tcon-lcd", .data = &sun9i_a80_tcon_lcd_quirks },
	{ .compatible = "allwinner,sun9i-a80-tcon-tv", .data = &sun9i_a80_tcon_tv_quirks },
	{ .compatible = "allwinner,sun20i-d1-tcon-lcd", .data = &sun20i_d1_lcd_quirks },
	{ .compatible = "allwinner,sun20i-d1-tcon-tv", .data = &sun8i_r40_tv_quirks },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_tcon_of_table);
EXPORT_SYMBOL(sun4i_tcon_of_table);

static struct platform_driver sun4i_tcon_platform_driver = {
	.probe		= sun4i_tcon_probe,
	.remove_new	= sun4i_tcon_remove,
	.driver		= {
		.name		= "sun4i-tcon",
		.of_match_table	= sun4i_tcon_of_table,
	},
};
module_platform_driver(sun4i_tcon_platform_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Timing Controller Driver");
MODULE_LICENSE("GPL");
