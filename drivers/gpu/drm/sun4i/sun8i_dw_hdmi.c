// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "sun8i_dw_hdmi.h"
#include "sun8i_tcon_top.h"

static void sun8i_dw_hdmi_encoder_mode_set(struct drm_encoder *encoder,
					   struct drm_display_mode *mode,
					   struct drm_display_mode *adj_mode)
{
	struct sun8i_dw_hdmi *hdmi = encoder_to_sun8i_dw_hdmi(encoder);

	clk_set_rate(hdmi->clk_tmds, mode->crtc_clock * 1000);
}

static const struct drm_encoder_helper_funcs
sun8i_dw_hdmi_encoder_helper_funcs = {
	.mode_set = sun8i_dw_hdmi_encoder_mode_set,
};

static const struct drm_encoder_funcs sun8i_dw_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static enum drm_mode_status
sun8i_dw_hdmi_mode_valid(struct drm_connector *connector,
			 const struct drm_display_mode *mode)
{
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool sun8i_dw_hdmi_node_is_tcon_top(struct device_node *node)
{
	return IS_ENABLED(CONFIG_DRM_SUN8I_TCON_TOP) &&
		!!of_match_node(sun8i_tcon_top_of_table, node);
}

static u32 sun8i_dw_hdmi_find_possible_crtcs(struct drm_device *drm,
					     struct device_node *node)
{
	struct device_node *port, *ep, *remote, *remote_port;
	u32 crtcs = 0;

	remote = of_graph_get_remote_node(node, 0, -1);
	if (!remote)
		return 0;

	if (sun8i_dw_hdmi_node_is_tcon_top(remote)) {
		port = of_graph_get_port_by_id(remote, 4);
		if (!port)
			goto crtcs_exit;

		for_each_child_of_node(port, ep) {
			remote_port = of_graph_get_remote_port(ep);
			if (remote_port) {
				crtcs |= drm_of_crtc_port_mask(drm, remote_port);
				of_node_put(remote_port);
			}
		}
	} else {
		crtcs = drm_of_find_possible_crtcs(drm, node);
	}

crtcs_exit:
	of_node_put(remote);

	return crtcs;
}

static int sun8i_dw_hdmi_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	struct drm_device *drm = data;
	struct device_node *phy_node;
	struct drm_encoder *encoder;
	struct sun8i_dw_hdmi *hdmi;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	plat_data = &hdmi->plat_data;
	hdmi->dev = &pdev->dev;
	encoder = &hdmi->encoder;

	encoder->possible_crtcs =
		sun8i_dw_hdmi_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	hdmi->rst_ctrl = devm_reset_control_get(dev, "ctrl");
	if (IS_ERR(hdmi->rst_ctrl)) {
		dev_err(dev, "Could not get ctrl reset control\n");
		return PTR_ERR(hdmi->rst_ctrl);
	}

	hdmi->clk_tmds = devm_clk_get(dev, "tmds");
	if (IS_ERR(hdmi->clk_tmds)) {
		dev_err(dev, "Couldn't get the tmds clock\n");
		return PTR_ERR(hdmi->clk_tmds);
	}

	ret = reset_control_deassert(hdmi->rst_ctrl);
	if (ret) {
		dev_err(dev, "Could not deassert ctrl reset control\n");
		return ret;
	}

	ret = clk_prepare_enable(hdmi->clk_tmds);
	if (ret) {
		dev_err(dev, "Could not enable tmds clock\n");
		goto err_assert_ctrl_reset;
	}

	phy_node = of_parse_phandle(dev->of_node, "phys", 0);
	if (!phy_node) {
		dev_err(dev, "Can't found PHY phandle\n");
		ret = -EINVAL;
		goto err_disable_clk_tmds;
	}

	ret = sun8i_hdmi_phy_probe(hdmi, phy_node);
	of_node_put(phy_node);
	if (ret) {
		dev_err(dev, "Couldn't get the HDMI PHY\n");
		goto err_disable_clk_tmds;
	}

	drm_encoder_helper_add(encoder, &sun8i_dw_hdmi_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &sun8i_dw_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	sun8i_hdmi_phy_init(hdmi->phy);

	plat_data->mode_valid = &sun8i_dw_hdmi_mode_valid;
	plat_data->phy_ops = sun8i_hdmi_phy_get_ops();
	plat_data->phy_name = "sun8i_dw_hdmi_phy";
	plat_data->phy_data = hdmi->phy;

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_bind(pdev, encoder, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		goto cleanup_encoder;
	}

	return 0;

cleanup_encoder:
	drm_encoder_cleanup(encoder);
	sun8i_hdmi_phy_remove(hdmi);
err_disable_clk_tmds:
	clk_disable_unprepare(hdmi->clk_tmds);
err_assert_ctrl_reset:
	reset_control_assert(hdmi->rst_ctrl);

	return ret;
}

static void sun8i_dw_hdmi_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct sun8i_dw_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_unbind(hdmi->hdmi);
	sun8i_hdmi_phy_remove(hdmi);
	clk_disable_unprepare(hdmi->clk_tmds);
	reset_control_assert(hdmi->rst_ctrl);
}

static const struct component_ops sun8i_dw_hdmi_ops = {
	.bind	= sun8i_dw_hdmi_bind,
	.unbind	= sun8i_dw_hdmi_unbind,
};

static int sun8i_dw_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_dw_hdmi_ops);
}

static int sun8i_dw_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_dw_hdmi_ops);

	return 0;
}

static const struct of_device_id sun8i_dw_hdmi_dt_ids[] = {
	{ .compatible = "allwinner,sun8i-a83t-dw-hdmi" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sun8i_dw_hdmi_dt_ids);

static struct platform_driver sun8i_dw_hdmi_pltfm_driver = {
	.probe  = sun8i_dw_hdmi_probe,
	.remove = sun8i_dw_hdmi_remove,
	.driver = {
		.name = "sun8i-dw-hdmi",
		.of_match_table = sun8i_dw_hdmi_dt_ids,
	},
};
module_platform_driver(sun8i_dw_hdmi_pltfm_driver);

MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@siol.net>");
MODULE_DESCRIPTION("Allwinner DW HDMI bridge");
MODULE_LICENSE("GPL");
