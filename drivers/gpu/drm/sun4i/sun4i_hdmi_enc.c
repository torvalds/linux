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

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode, false);
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

static int sun4i_hdmi_get_modes(struct drm_connector *connector)
{
	struct sun4i_hdmi *hdmi = drm_connector_to_sun4i_hdmi(connector);
	struct edid *edid;
	int ret;

	edid = drm_get_edid(connector, hdmi->i2c);
	if (!edid)
		return 0;

	hdmi->hdmi_monitor = drm_detect_hdmi_monitor(edid);
	DRM_DEBUG_DRIVER("Monitor is %s monitor\n",
			 hdmi->hdmi_monitor ? "an HDMI" : "a DVI");

	drm_mode_connector_update_edid_property(connector, edid);
	cec_s_phys_addr_from_edid(hdmi->cec_adap, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

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
			       0, 500000)) {
		cec_phys_addr_invalidate(hdmi->cec_adap);
		return connector_status_disconnected;
	}

	return connector_status_connected;
}

static const struct drm_connector_funcs sun4i_hdmi_connector_funcs = {
	.detect			= sun4i_hdmi_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

#ifdef CONFIG_DRM_SUN4I_HDMI_CEC
static bool sun4i_hdmi_cec_pin_read(struct cec_adapter *adap)
{
	struct sun4i_hdmi *hdmi = cec_get_drvdata(adap);

	return readl(hdmi->base + SUN4I_HDMI_CEC) & SUN4I_HDMI_CEC_RX;
}

static void sun4i_hdmi_cec_pin_low(struct cec_adapter *adap)
{
	struct sun4i_hdmi *hdmi = cec_get_drvdata(adap);

	/* Start driving the CEC pin low */
	writel(SUN4I_HDMI_CEC_ENABLE, hdmi->base + SUN4I_HDMI_CEC);
}

static void sun4i_hdmi_cec_pin_high(struct cec_adapter *adap)
{
	struct sun4i_hdmi *hdmi = cec_get_drvdata(adap);

	/*
	 * Stop driving the CEC pin, the pull up will take over
	 * unless another CEC device is driving the pin low.
	 */
	writel(0, hdmi->base + SUN4I_HDMI_CEC);
}

static const struct cec_pin_ops sun4i_hdmi_cec_pin_ops = {
	.read = sun4i_hdmi_cec_pin_read,
	.low = sun4i_hdmi_cec_pin_low,
	.high = sun4i_hdmi_cec_pin_high,
};
#endif

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

	ret = sun4i_hdmi_i2c_create(dev, hdmi);
	if (ret) {
		dev_err(dev, "Couldn't create the HDMI I2C adapter\n");
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
		goto err_del_i2c_adapter;
	}

	hdmi->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm,
								  dev->of_node);
	if (!hdmi->encoder.possible_crtcs) {
		ret = -EPROBE_DEFER;
		goto err_del_i2c_adapter;
	}

#ifdef CONFIG_DRM_SUN4I_HDMI_CEC
	hdmi->cec_adap = cec_pin_allocate_adapter(&sun4i_hdmi_cec_pin_ops,
		hdmi, "sun4i", CEC_CAP_TRANSMIT | CEC_CAP_LOG_ADDRS |
		CEC_CAP_PASSTHROUGH | CEC_CAP_RC);
	ret = PTR_ERR_OR_ZERO(hdmi->cec_adap);
	if (ret < 0)
		goto err_cleanup_connector;
	writel(readl(hdmi->base + SUN4I_HDMI_CEC) & ~SUN4I_HDMI_CEC_TX,
	       hdmi->base + SUN4I_HDMI_CEC);
#endif

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

	ret = cec_register_adapter(hdmi->cec_adap, dev);
	if (ret < 0)
		goto err_cleanup_connector;
	drm_mode_connector_attach_encoder(&hdmi->connector, &hdmi->encoder);

	return 0;

err_cleanup_connector:
	cec_delete_adapter(hdmi->cec_adap);
	drm_encoder_cleanup(&hdmi->encoder);
err_del_i2c_adapter:
	i2c_del_adapter(hdmi->i2c);
	return ret;
}

static void sun4i_hdmi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct sun4i_hdmi *hdmi = dev_get_drvdata(dev);

	cec_unregister_adapter(hdmi->cec_adap);
	drm_connector_cleanup(&hdmi->connector);
	drm_encoder_cleanup(&hdmi->encoder);
	i2c_del_adapter(hdmi->i2c);
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
