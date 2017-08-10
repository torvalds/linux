/*
 * Rockchip SoC DP (Display Port) interface driver.
 *
 * Copyright (C) Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Andy Yan <andy.yan@rock-chips.com>
 *         Yakir Yang <ykk@rock-chips.com>
 *         Jeff Chen <jeff.chen@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/clk.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/bridge/analogix_dp.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_psr.h"
#include "rockchip_drm_vop.h"

#define RK3288_GRF_SOC_CON6		0x25c
#define RK3288_EDP_LCDC_SEL		BIT(5)
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_EDP_LCDC_SEL		BIT(5)

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

#define PSR_WAIT_LINE_FLAG_TIMEOUT_MS	100

#define to_dp(nm)	container_of(nm, struct rockchip_dp_device, nm)

/**
 * struct rockchip_dp_chip_data - splite the grf setting of kind of chips
 * @lcdsel_grf_reg: grf register offset of lcdc select
 * @lcdsel_big: reg value of selecting vop big for eDP
 * @lcdsel_lit: reg value of selecting vop little for eDP
 * @chip_type: specific chip type
 */
struct rockchip_dp_chip_data {
	u32	lcdsel_grf_reg;
	u32	lcdsel_big;
	u32	lcdsel_lit;
	u32	chip_type;
};

struct rockchip_dp_device {
	struct drm_device        *drm_dev;
	struct device            *dev;
	struct drm_encoder       encoder;
	struct drm_display_mode  mode;

	struct clk               *pclk;
	struct clk               *grfclk;
	struct regmap            *grf;
	struct reset_control     *rst;

	struct work_struct	 psr_work;
	spinlock_t		 psr_lock;
	unsigned int             psr_state;

	const struct rockchip_dp_chip_data *data;

	struct analogix_dp_plat_data plat_data;
};

static void analogix_dp_psr_set(struct drm_encoder *encoder, bool enabled)
{
	struct rockchip_dp_device *dp = to_dp(encoder);
	unsigned long flags;

	if (!analogix_dp_psr_supported(dp->dev))
		return;

	dev_dbg(dp->dev, "%s PSR...\n", enabled ? "Entry" : "Exit");

	spin_lock_irqsave(&dp->psr_lock, flags);
	if (enabled)
		dp->psr_state = EDP_VSC_PSR_STATE_ACTIVE;
	else
		dp->psr_state = ~EDP_VSC_PSR_STATE_ACTIVE;

	schedule_work(&dp->psr_work);
	spin_unlock_irqrestore(&dp->psr_lock, flags);
}

static void analogix_dp_psr_work(struct work_struct *work)
{
	struct rockchip_dp_device *dp =
				container_of(work, typeof(*dp), psr_work);
	int ret;
	unsigned long flags;

	ret = rockchip_drm_wait_vact_end(dp->encoder.crtc,
					 PSR_WAIT_LINE_FLAG_TIMEOUT_MS);
	if (ret) {
		dev_err(dp->dev, "line flag interrupt did not arrive\n");
		return;
	}

	spin_lock_irqsave(&dp->psr_lock, flags);
	if (dp->psr_state == EDP_VSC_PSR_STATE_ACTIVE)
		analogix_dp_enable_psr(dp->dev);
	else
		analogix_dp_disable_psr(dp->dev);
	spin_unlock_irqrestore(&dp->psr_lock, flags);
}

static int rockchip_dp_pre_init(struct rockchip_dp_device *dp)
{
	reset_control_assert(dp->rst);
	usleep_range(10, 20);
	reset_control_deassert(dp->rst);

	return 0;
}

static int rockchip_dp_poweron(struct analogix_dp_plat_data *plat_data)
{
	struct rockchip_dp_device *dp = to_dp(plat_data);
	int ret;

	cancel_work_sync(&dp->psr_work);

	ret = clk_prepare_enable(dp->pclk);
	if (ret < 0) {
		dev_err(dp->dev, "failed to enable pclk %d\n", ret);
		return ret;
	}

	ret = rockchip_dp_pre_init(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to dp pre init %d\n", ret);
		clk_disable_unprepare(dp->pclk);
		return ret;
	}

	return 0;
}

static int rockchip_dp_powerdown(struct analogix_dp_plat_data *plat_data)
{
	struct rockchip_dp_device *dp = to_dp(plat_data);

	clk_disable_unprepare(dp->pclk);

	return 0;
}

static int rockchip_dp_get_modes(struct analogix_dp_plat_data *plat_data,
				 struct drm_connector *connector)
{
	struct drm_display_info *di = &connector->display_info;
	/* VOP couldn't output YUV video format for eDP rightly */
	u32 mask = DRM_COLOR_FORMAT_YCRCB444 | DRM_COLOR_FORMAT_YCRCB422;

	if ((di->color_formats & mask)) {
		DRM_DEBUG_KMS("Swapping display color format from YUV to RGB\n");
		di->color_formats &= ~mask;
		di->color_formats |= DRM_COLOR_FORMAT_RGB444;
		di->bpc = 8;
	}

	return 0;
}

static bool
rockchip_dp_drm_encoder_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	/* do nothing */
	return true;
}

static void rockchip_dp_drm_encoder_mode_set(struct drm_encoder *encoder,
					     struct drm_display_mode *mode,
					     struct drm_display_mode *adjusted)
{
	/* do nothing */
}

static void rockchip_dp_drm_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_dp_device *dp = to_dp(encoder);
	int ret;
	u32 val;

	ret = drm_of_encoder_active_endpoint_id(dp->dev->of_node, encoder);
	if (ret < 0)
		return;

	if (ret)
		val = dp->data->lcdsel_lit;
	else
		val = dp->data->lcdsel_big;

	dev_dbg(dp->dev, "vop %s output to dp\n", (ret) ? "LIT" : "BIG");

	ret = clk_prepare_enable(dp->grfclk);
	if (ret < 0) {
		dev_err(dp->dev, "failed to enable grfclk %d\n", ret);
		return;
	}

	ret = regmap_write(dp->grf, dp->data->lcdsel_grf_reg, val);
	if (ret != 0)
		dev_err(dp->dev, "Could not write to GRF: %d\n", ret);

	clk_disable_unprepare(dp->grfclk);
}

static void rockchip_dp_drm_encoder_nop(struct drm_encoder *encoder)
{
	/* do nothing */
}

static int
rockchip_dp_drm_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	/*
	 * The hardware IC designed that VOP must output the RGB10 video
	 * format to eDP controller, and if eDP panel only support RGB8,
	 * then eDP controller should cut down the video data, not via VOP
	 * controller, that's why we need to hardcode the VOP output mode
	 * to RGA10 here.
	 */

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_eDP;

	return 0;
}

static struct drm_encoder_helper_funcs rockchip_dp_encoder_helper_funcs = {
	.mode_fixup = rockchip_dp_drm_encoder_mode_fixup,
	.mode_set = rockchip_dp_drm_encoder_mode_set,
	.enable = rockchip_dp_drm_encoder_enable,
	.disable = rockchip_dp_drm_encoder_nop,
	.atomic_check = rockchip_dp_drm_encoder_atomic_check,
};

static void rockchip_dp_drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs rockchip_dp_encoder_funcs = {
	.destroy = rockchip_dp_drm_encoder_destroy,
};

static int rockchip_dp_init(struct rockchip_dp_device *dp)
{
	struct device *dev = dp->dev;
	struct device_node *np = dev->of_node;
	int ret;

	dp->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dp->grf)) {
		dev_err(dev, "failed to get rockchip,grf property\n");
		return PTR_ERR(dp->grf);
	}

	dp->grfclk = devm_clk_get(dev, "grf");
	if (PTR_ERR(dp->grfclk) == -ENOENT) {
		dp->grfclk = NULL;
	} else if (PTR_ERR(dp->grfclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(dp->grfclk)) {
		dev_err(dev, "failed to get grf clock\n");
		return PTR_ERR(dp->grfclk);
	}

	dp->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dp->pclk)) {
		dev_err(dev, "failed to get pclk property\n");
		return PTR_ERR(dp->pclk);
	}

	dp->rst = devm_reset_control_get(dev, "dp");
	if (IS_ERR(dp->rst)) {
		dev_err(dev, "failed to get dp reset control\n");
		return PTR_ERR(dp->rst);
	}

	ret = clk_prepare_enable(dp->pclk);
	if (ret < 0) {
		dev_err(dp->dev, "failed to enable pclk %d\n", ret);
		return ret;
	}

	ret = rockchip_dp_pre_init(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to pre init %d\n", ret);
		clk_disable_unprepare(dp->pclk);
		return ret;
	}

	return 0;
}

static int rockchip_dp_drm_create_encoder(struct rockchip_dp_device *dp)
{
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_device *drm_dev = dp->drm_dev;
	struct device *dev = dp->dev;
	int ret;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);
	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_dp_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_dp_encoder_helper_funcs);

	return 0;
}

static int rockchip_dp_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);
	const struct rockchip_dp_chip_data *dp_data;
	struct drm_device *drm_dev = data;
	int ret;

	/*
	 * Just like the probe function said, we don't need the
	 * device drvrate anymore, we should leave the charge to
	 * analogix dp driver, set the device drvdata to NULL.
	 */
	dev_set_drvdata(dev, NULL);

	dp_data = of_device_get_match_data(dev);
	if (!dp_data)
		return -ENODEV;

	ret = rockchip_dp_init(dp);
	if (ret < 0)
		return ret;

	dp->data = dp_data;
	dp->drm_dev = drm_dev;

	ret = rockchip_dp_drm_create_encoder(dp);
	if (ret) {
		DRM_ERROR("failed to create drm encoder\n");
		return ret;
	}

	dp->plat_data.encoder = &dp->encoder;

	dp->plat_data.dev_type = dp->data->chip_type;
	dp->plat_data.power_on = rockchip_dp_poweron;
	dp->plat_data.power_off = rockchip_dp_powerdown;
	dp->plat_data.get_modes = rockchip_dp_get_modes;

	spin_lock_init(&dp->psr_lock);
	dp->psr_state = ~EDP_VSC_PSR_STATE_ACTIVE;
	INIT_WORK(&dp->psr_work, analogix_dp_psr_work);

	rockchip_drm_psr_register(&dp->encoder, analogix_dp_psr_set);

	return analogix_dp_bind(dev, dp->drm_dev, &dp->plat_data);
}

static void rockchip_dp_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	rockchip_drm_psr_unregister(&dp->encoder);

	analogix_dp_unbind(dev, master, data);
	clk_disable_unprepare(dp->pclk);
}

static const struct component_ops rockchip_dp_component_ops = {
	.bind = rockchip_dp_bind,
	.unbind = rockchip_dp_unbind,
};

static int rockchip_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_panel *panel = NULL;
	struct rockchip_dp_device *dp;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, NULL);
	if (ret)
		return ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->dev = dev;

	dp->plat_data.panel = panel;

	/*
	 * We just use the drvdata until driver run into component
	 * add function, and then we would set drvdata to null, so
	 * that analogix dp driver could take charge of the drvdata.
	 */
	platform_set_drvdata(pdev, dp);

	return component_add(dev, &rockchip_dp_component_ops);
}

static int rockchip_dp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_dp_component_ops);

	return 0;
}

static const struct dev_pm_ops rockchip_dp_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = analogix_dp_suspend,
	.resume_early = analogix_dp_resume,
#endif
};

static const struct rockchip_dp_chip_data rk3399_edp = {
	.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
	.lcdsel_big = HIWORD_UPDATE(0, RK3399_EDP_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3399_EDP_LCDC_SEL, RK3399_EDP_LCDC_SEL),
	.chip_type = RK3399_EDP,
};

static const struct rockchip_dp_chip_data rk3288_dp = {
	.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
	.lcdsel_big = HIWORD_UPDATE(0, RK3288_EDP_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3288_EDP_LCDC_SEL, RK3288_EDP_LCDC_SEL),
	.chip_type = RK3288_DP,
};

static const struct of_device_id rockchip_dp_dt_ids[] = {
	{.compatible = "rockchip,rk3288-dp", .data = &rk3288_dp },
	{.compatible = "rockchip,rk3399-edp", .data = &rk3399_edp },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_dp_dt_ids);

struct platform_driver rockchip_dp_driver = {
	.probe = rockchip_dp_probe,
	.remove = rockchip_dp_remove,
	.driver = {
		   .name = "rockchip-dp",
		   .pm = &rockchip_dp_pm_ops,
		   .of_match_table = of_match_ptr(rockchip_dp_dt_ids),
	},
};
