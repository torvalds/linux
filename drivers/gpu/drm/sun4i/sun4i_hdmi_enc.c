/*
 * Copyright (C) 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "sun4i_backend.h"
#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sun4i_hdmi.h"
#include "sun4i_tcon.h"

#define DDC_SEGMENT_ADDR	0x30

static inline struct sun4i_hdmi *
drm_encoder_to_sun4i_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun4i_hdmi,
			    encoder);
}

static inline struct sun4i_hdmi *
drm_connector_to_sun4i_hdmi(struct drm_connector *connector)
{
	return container_of(connector, struct sun4i_hdmi,
			    connector);
}

static int sun4i_hdmi_setup_avi_infoframes(struct sun4i_hdmi *hdmi,
					   struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 buffer[17];
	int i, ret;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode);
	if (ret < 0) {
		DRM_ERROR("Failed to get infoframes from mode\n");
		return ret;
	}

	ret = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		DRM_ERROR("Failed to pack infoframes\n");
		return ret;
	}

	for (i = 0; i < sizeof(buffer); i++)
		writeb(buffer[i], hdmi->base + SUN4I_HDMI_AVI_INFOFRAME_REG(i));

	return 0;
}

static int sun4i_hdmi_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct drm_display_mode *mode = &crtc_state->mode;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return -EINVAL;

	return 0;
}

static void sun4i_hdmi_disable(struct drm_encoder *encoder)
{
	struct sun4i_hdmi *hdmi = drm_encoder_to_sun4i_hdmi(encoder);
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);
	struct sun4i_tcon *tcon = crtc->tcon;
	u32 val;

	DRM_DEBUG_DRIVER("Disabling the HDMI Output\n");

	val = readl(hdmi->base + SUN4I_HDMI_VID_CTRL_REG);
	val &= ~SUN4I_HDMI_VID_CTRL_ENABLE;
	writel(val, hdmi->base + SUN4I_HDMI_VID_CTRL_REG);

	sun4i_tcon_channel_disable(tcon, 1);
}

static void sun4i_hdmi_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct sun4i_hdmi *hdmi = drm_encoder_to_sun4i_hdmi(encoder);
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);
	struct sun4i_tcon *tcon = crtc->tcon;
	u32 val = 0;

	DRM_DEBUG_DRIVER("Enabling the HDMI Output\n");

	sun4i_tcon_channel_enable(tcon, 1);

	sun4i_hdmi_setup_avi_infoframes(hdmi, mode);
	val |= SUN4I_HDMI_PKT_CTRL_TYPE(0, SUN4I_HDMI_PKT_AVI);
	val |= SUN4I_HDMI_PKT_CTRL_TYPE(1, SUN4I_HDMI_PKT_END);
	writel(val, hdmi->base + SUN4I_HDMI_PKT_CTRL_REG(0));

	val = SUN4I_HDMI_VID_CTRL_ENABLE;
	if (hdmi->hdmi_monitor)
		val |= SUN4I_HDMI_VID_CTRL_HDMI_MODE;

	writel(val, hdmi->base + SUN4I_HDMI_VID_CTRL_REG);
}

static void sun4i_hdmi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct sun4i_hdmi *hdmi = drm_encoder_to_sun4i_hdmi(encoder);
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);
	struct sun4i_tcon *tcon = crtc->tcon;
	unsigned int x, y;
	u32 val;

	sun4i_tcon1_mode_set(tcon, mode);
	sun4i_tcon_set_mux(tcon, 1, encoder);

	clk_set_rate(tcon->sclk1, mode->crtc_clock * 1000);
	clk_set_rate(hdmi->mod_clk, mode->crtc_clock * 1000);
	clk_set_rate(hdmi->tmds_clk, mode->crtc_clock * 1000);

	/* Set input sync enable */
	writel(SUN4I_HDMI_UNKNOWN_INPUT_SYNC,
	       hdmi->base + SUN4I_HDMI_UNKNOWN_REG);

	/* Setup timing registers */
	writel(SUN4I_HDMI_VID_TIMING_X(mode->hdisplay) |
	       SUN4I_HDMI_VID_TIMING_Y(mode->vdisplay),
	       hdmi->base + SUN4I_HDMI_VID_TIMING_ACT_REG);

	x = mode->htotal - mode->hsync_start;
	y = mode->vtotal - mode->vsync_start;
	writel(SUN4I_HDMI_VID_TIMING_X(x) | SUN4I_HDMI_VID_TIMING_Y(y),
	       hdmi->base + SUN4I_HDMI_VID_TIMING_BP_REG);

	x = mode->hsync_start - mode->hdisplay;
	y = mode->vsync_start - mode->vdisplay;
	writel(SUN4I_HDMI_VID_TIMING_X(x) | SUN4I_HDMI_VID_TIMING_Y(y),
	       hdmi->base + SUN4I_HDMI_VID_TIMING_FP_REG);

	x = mode->hsync_end - mode->hsync_start;
	y = mode->vsync_end - mode->vsync_start;
	writel(SUN4I_HDMI_VID_TIMING_X(x) | SUN4I_HDMI_VID_TIMING_Y(y),
	       hdmi->base + SUN4I_HDMI_VID_TIMING_SPW_REG);

	val = SUN4I_HDMI_VID_TIMING_POL_TX_CLK;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= SUN4I_HDMI_VID_TIMING_POL_HSYNC;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= SUN4I_HDMI_VID_TIMING_POL_VSYNC;

	writel(val, hdmi->base + SUN4I_HDMI_VID_TIMING_POL_REG);
}

static const struct drm_encoder_helper_funcs sun4i_hdmi_helper_funcs = {
	.atomic_check	= sun4i_hdmi_atomic_check,
	.disable	= sun4i_hdmi_disable,
	.enable		= sun4i_hdmi_enable,
	.mode_set	= sun4i_hdmi_mode_set,
};

static const struct drm_encoder_funcs sun4i_hdmi_funcs = {
	.destroy	= drm_encoder_cleanup,
};

static int sun4i_hdmi_read_sub_block(struct sun4i_hdmi *hdmi,
				     unsigned int blk, unsigned int offset,
				     u8 *buf, unsigned int count)
{
	unsigned long reg;
	int i;

	reg = readl(hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);
	reg &= ~SUN4I_HDMI_DDC_CTRL_FIFO_DIR_MASK;
	writel(reg | SUN4I_HDMI_DDC_CTRL_FIFO_DIR_READ,
	       hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);

	writel(SUN4I_HDMI_DDC_ADDR_SEGMENT(offset >> 8) |
	       SUN4I_HDMI_DDC_ADDR_EDDC(DDC_SEGMENT_ADDR << 1) |
	       SUN4I_HDMI_DDC_ADDR_OFFSET(offset) |
	       SUN4I_HDMI_DDC_ADDR_SLAVE(DDC_ADDR),
	       hdmi->base + SUN4I_HDMI_DDC_ADDR_REG);

	reg = readl(hdmi->base + SUN4I_HDMI_DDC_FIFO_CTRL_REG);
	writel(reg | SUN4I_HDMI_DDC_FIFO_CTRL_CLEAR,
	       hdmi->base + SUN4I_HDMI_DDC_FIFO_CTRL_REG);

	writel(count, hdmi->base + SUN4I_HDMI_DDC_BYTE_COUNT_REG);
	writel(SUN4I_HDMI_DDC_CMD_EXPLICIT_EDDC_READ,
	       hdmi->base + SUN4I_HDMI_DDC_CMD_REG);

	reg = readl(hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);
	writel(reg | SUN4I_HDMI_DDC_CTRL_START_CMD,
	       hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);

	if (readl_poll_timeout(hdmi->base + SUN4I_HDMI_DDC_CTRL_REG, reg,
			       !(reg & SUN4I_HDMI_DDC_CTRL_START_CMD),
			       100, 100000))
		return -EIO;

	for (i = 0; i < count; i++)
		buf[i] = readb(hdmi->base + SUN4I_HDMI_DDC_FIFO_DATA_REG);

	return 0;
}

static int sun4i_hdmi_read_edid_block(void *data, u8 *buf, unsigned int blk,
				      size_t length)
{
	struct sun4i_hdmi *hdmi = data;
	int retry = 2, i;

	do {
		for (i = 0; i < length; i += SUN4I_HDMI_DDC_FIFO_SIZE) {
			unsigned char offset = blk * EDID_LENGTH + i;
			unsigned int count = min((unsigned int)SUN4I_HDMI_DDC_FIFO_SIZE,
						 length - i);
			int ret;

			ret = sun4i_hdmi_read_sub_block(hdmi, blk, offset,
							buf + i, count);
			if (ret)
				return ret;
		}
	} while (!drm_edid_block_valid(buf, blk, true, NULL) && (retry--));

	return 0;
}

static int sun4i_hdmi_get_modes(struct drm_connector *connector)
{
	struct sun4i_hdmi *hdmi = drm_connector_to_sun4i_hdmi(connector);
	unsigned long reg;
	struct edid *edid;
	int ret;

	/* Reset i2c controller */
	writel(SUN4I_HDMI_DDC_CTRL_ENABLE | SUN4I_HDMI_DDC_CTRL_RESET,
	       hdmi->base + SUN4I_HDMI_DDC_CTRL_REG);
	if (readl_poll_timeout(hdmi->base + SUN4I_HDMI_DDC_CTRL_REG, reg,
			       !(reg & SUN4I_HDMI_DDC_CTRL_RESET),
			       100, 2000))
		return -EIO;

	writel(SUN4I_HDMI_DDC_LINE_CTRL_SDA_ENABLE |
	       SUN4I_HDMI_DDC_LINE_CTRL_SCL_ENABLE,
	       hdmi->base + SUN4I_HDMI_DDC_LINE_CTRL_REG);

	clk_prepare_enable(hdmi->ddc_clk);
	clk_set_rate(hdmi->ddc_clk, 100000);

	edid = drm_do_get_edid(connector, sun4i_hdmi_read_edid_block, hdmi);
	if (!edid)
		return 0;

	hdmi->hdmi_monitor = drm_detect_hdmi_monitor(edid);
	DRM_DEBUG_DRIVER("Monitor is %s monitor\n",
			 hdmi->hdmi_monitor ? "an HDMI" : "a DVI");

	drm_mode_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	clk_disable_unprepare(hdmi->ddc_clk);

	return ret;
}

static const struct drm_connector_helper_funcs sun4i_hdmi_connector_helper_funcs = {
	.get_modes	= sun4i_hdmi_get_modes,
};

static enum drm_connector_status
sun4i_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct sun4i_hdmi *hdmi = drm_connector_to_sun4i_hdmi(connector);
	unsigned long reg;

	if (readl_poll_timeout(hdmi->base + SUN4I_HDMI_HPD_REG, reg,
			       reg & SUN4I_HDMI_HPD_HIGH,
			       0, 500000))
		return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_connector_funcs sun4i_hdmi_connector_funcs = {
	.dpms			= drm_atomic_helper_connector_dpms,
	.detect			= sun4i_hdmi_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int sun4i_hdmi_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_hdmi *hdmi;
	struct resource *res;
	u32 reg;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;
	dev_set_drvdata(dev, hdmi);
	hdmi->dev = dev;
	hdmi->drv = drv;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdmi->base)) {
		dev_err(dev, "Couldn't map the HDMI encoder registers\n");
		return PTR_ERR(hdmi->base);
	}

	hdmi->bus_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(hdmi->bus_clk)) {
		dev_err(dev, "Couldn't get the HDMI bus clock\n");
		return PTR_ERR(hdmi->bus_clk);
	}
	clk_prepare_enable(hdmi->bus_clk);

	hdmi->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(hdmi->mod_clk)) {
		dev_err(dev, "Couldn't get the HDMI mod clock\n");
		return PTR_ERR(hdmi->mod_clk);
	}
	clk_prepare_enable(hdmi->mod_clk);

	hdmi->pll0_clk = devm_clk_get(dev, "pll-0");
	if (IS_ERR(hdmi->pll0_clk)) {
		dev_err(dev, "Couldn't get the HDMI PLL 0 clock\n");
		return PTR_ERR(hdmi->pll0_clk);
	}

	hdmi->pll1_clk = devm_clk_get(dev, "pll-1");
	if (IS_ERR(hdmi->pll1_clk)) {
		dev_err(dev, "Couldn't get the HDMI PLL 1 clock\n");
		return PTR_ERR(hdmi->pll1_clk);
	}

	ret = sun4i_tmds_create(hdmi);
	if (ret) {
		dev_err(dev, "Couldn't create the TMDS clock\n");
		return ret;
	}

	writel(SUN4I_HDMI_CTRL_ENABLE, hdmi->base + SUN4I_HDMI_CTRL_REG);

	writel(SUN4I_HDMI_PAD_CTRL0_TXEN | SUN4I_HDMI_PAD_CTRL0_CKEN |
	       SUN4I_HDMI_PAD_CTRL0_PWENG | SUN4I_HDMI_PAD_CTRL0_PWEND |
	       SUN4I_HDMI_PAD_CTRL0_PWENC | SUN4I_HDMI_PAD_CTRL0_LDODEN |
	       SUN4I_HDMI_PAD_CTRL0_LDOCEN | SUN4I_HDMI_PAD_CTRL0_BIASEN,
	       hdmi->base + SUN4I_HDMI_PAD_CTRL0_REG);

	/*
	 * We can't just initialize the register there, we need to
	 * protect the clock bits that have already been read out and
	 * cached by the clock framework.
	 */
	reg = readl(hdmi->base + SUN4I_HDMI_PAD_CTRL1_REG);
	reg &= SUN4I_HDMI_PAD_CTRL1_HALVE_CLK;
	reg |= SUN4I_HDMI_PAD_CTRL1_REG_AMP(6) |
		SUN4I_HDMI_PAD_CTRL1_REG_EMP(2) |
		SUN4I_HDMI_PAD_CTRL1_REG_DENCK |
		SUN4I_HDMI_PAD_CTRL1_REG_DEN |
		SUN4I_HDMI_PAD_CTRL1_EMPCK_OPT |
		SUN4I_HDMI_PAD_CTRL1_EMP_OPT |
		SUN4I_HDMI_PAD_CTRL1_AMPCK_OPT |
		SUN4I_HDMI_PAD_CTRL1_AMP_OPT;
	writel(reg, hdmi->base + SUN4I_HDMI_PAD_CTRL1_REG);

	reg = readl(hdmi->base + SUN4I_HDMI_PLL_CTRL_REG);
	reg &= SUN4I_HDMI_PLL_CTRL_DIV_MASK;
	reg |= SUN4I_HDMI_PLL_CTRL_VCO_S(8) | SUN4I_HDMI_PLL_CTRL_CS(7) |
		SUN4I_HDMI_PLL_CTRL_CP_S(15) | SUN4I_HDMI_PLL_CTRL_S(7) |
		SUN4I_HDMI_PLL_CTRL_VCO_GAIN(4) | SUN4I_HDMI_PLL_CTRL_SDIV2 |
		SUN4I_HDMI_PLL_CTRL_LDO2_EN | SUN4I_HDMI_PLL_CTRL_LDO1_EN |
		SUN4I_HDMI_PLL_CTRL_HV_IS_33 | SUN4I_HDMI_PLL_CTRL_BWS |
		SUN4I_HDMI_PLL_CTRL_PLL_EN;
	writel(reg, hdmi->base + SUN4I_HDMI_PLL_CTRL_REG);

	ret = sun4i_ddc_create(hdmi, hdmi->tmds_clk);
	if (ret) {
		dev_err(dev, "Couldn't create the DDC clock\n");
		return ret;
	}

	drm_encoder_helper_add(&hdmi->encoder,
			       &sun4i_hdmi_helper_funcs);
	ret = drm_encoder_init(drm,
			       &hdmi->encoder,
			       &sun4i_hdmi_funcs,
			       DRM_MODE_ENCODER_TMDS,
			       NULL);
	if (ret) {
		dev_err(dev, "Couldn't initialise the HDMI encoder\n");
		return ret;
	}

	hdmi->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm,
								  dev->of_node);
	if (!hdmi->encoder.possible_crtcs)
		return -EPROBE_DEFER;

	drm_connector_helper_add(&hdmi->connector,
				 &sun4i_hdmi_connector_helper_funcs);
	ret = drm_connector_init(drm, &hdmi->connector,
				 &sun4i_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(dev,
			"Couldn't initialise the HDMI connector\n");
		goto err_cleanup_connector;
	}

	/* There is no HPD interrupt, so we need to poll the controller */
	hdmi->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
		DRM_CONNECTOR_POLL_DISCONNECT;

	drm_mode_connector_attach_encoder(&hdmi->connector, &hdmi->encoder);

	return 0;

err_cleanup_connector:
	drm_encoder_cleanup(&hdmi->encoder);
	return ret;
}

static void sun4i_hdmi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct sun4i_hdmi *hdmi = dev_get_drvdata(dev);

	drm_connector_cleanup(&hdmi->connector);
	drm_encoder_cleanup(&hdmi->encoder);
}

static const struct component_ops sun4i_hdmi_ops = {
	.bind	= sun4i_hdmi_bind,
	.unbind	= sun4i_hdmi_unbind,
};

static int sun4i_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun4i_hdmi_ops);
}

static int sun4i_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_hdmi_ops);

	return 0;
}

static const struct of_device_id sun4i_hdmi_of_table[] = {
	{ .compatible = "allwinner,sun5i-a10s-hdmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_hdmi_of_table);

static struct platform_driver sun4i_hdmi_driver = {
	.probe		= sun4i_hdmi_probe,
	.remove		= sun4i_hdmi_remove,
	.driver		= {
		.name		= "sun4i-hdmi",
		.of_match_table	= sun4i_hdmi_of_table,
	},
};
module_platform_driver(sun4i_hdmi_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 HDMI Driver");
MODULE_LICENSE("GPL");
