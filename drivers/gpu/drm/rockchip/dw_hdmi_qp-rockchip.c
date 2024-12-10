// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2022 Rockchip Electronics Co., Ltd.
 * Copyright (c) 2024 Collabora Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 * Author: Cristian Ciocaltea <cristian.ciocaltea@collabora.com>
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#include <drm/bridge/dw_hdmi_qp.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "rockchip_drm_drv.h"

#define RK3588_GRF_SOC_CON2		0x0308
#define RK3588_HDMI0_HPD_INT_MSK	BIT(13)
#define RK3588_HDMI0_HPD_INT_CLR	BIT(12)
#define RK3588_HDMI1_HPD_INT_MSK	BIT(15)
#define RK3588_HDMI1_HPD_INT_CLR	BIT(14)
#define RK3588_GRF_SOC_CON7		0x031c
#define RK3588_SET_HPD_PATH_MASK	GENMASK(13, 12)
#define RK3588_GRF_SOC_STATUS1		0x0384
#define RK3588_HDMI0_LEVEL_INT		BIT(16)
#define RK3588_HDMI1_LEVEL_INT		BIT(24)
#define RK3588_GRF_VO1_CON3		0x000c
#define RK3588_GRF_VO1_CON6		0x0018
#define RK3588_SCLIN_MASK		BIT(9)
#define RK3588_SDAIN_MASK		BIT(10)
#define RK3588_MODE_MASK		BIT(11)
#define RK3588_I2S_SEL_MASK		BIT(13)
#define RK3588_GRF_VO1_CON9		0x0024
#define RK3588_HDMI0_GRANT_SEL		BIT(10)
#define RK3588_HDMI1_GRANT_SEL		BIT(12)

#define HIWORD_UPDATE(val, mask)	((val) | (mask) << 16)
#define HOTPLUG_DEBOUNCE_MS		150
#define MAX_HDMI_PORT_NUM		2

struct rockchip_hdmi_qp {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *vo_regmap;
	struct rockchip_encoder encoder;
	struct clk *ref_clk;
	struct dw_hdmi_qp *hdmi;
	struct phy *phy;
	struct gpio_desc *enable_gpio;
	struct delayed_work hpd_work;
	int port_id;
};

static struct rockchip_hdmi_qp *to_rockchip_hdmi_qp(struct drm_encoder *encoder)
{
	struct rockchip_encoder *rkencoder = to_rockchip_encoder(encoder);

	return container_of(rkencoder, struct rockchip_hdmi_qp, encoder);
}

static void dw_hdmi_qp_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi_qp *hdmi = to_rockchip_hdmi_qp(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	unsigned long long rate;

	/* Unconditionally switch to TMDS as FRL is not yet supported */
	gpiod_set_value(hdmi->enable_gpio, 1);

	if (crtc && crtc->state) {
		rate = drm_hdmi_compute_mode_clock(&crtc->state->adjusted_mode,
						   8, HDMI_COLORSPACE_RGB);
		clk_set_rate(hdmi->ref_clk, rate);
		/*
		 * FIXME: Temporary workaround to pass pixel clock rate
		 * to the PHY driver until phy_configure_opts_hdmi
		 * becomes available in the PHY API. See also the related
		 * comment in rk_hdptx_phy_power_on() from
		 * drivers/phy/rockchip/phy-rockchip-samsung-hdptx.c
		 */
		phy_set_bus_width(hdmi->phy, div_u64(rate, 100));
	}
}

static int
dw_hdmi_qp_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const struct
drm_encoder_helper_funcs dw_hdmi_qp_rockchip_encoder_helper_funcs = {
	.enable		= dw_hdmi_qp_rockchip_encoder_enable,
	.atomic_check	= dw_hdmi_qp_rockchip_encoder_atomic_check,
};

static int dw_hdmi_qp_rk3588_phy_init(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	struct rockchip_hdmi_qp *hdmi = (struct rockchip_hdmi_qp *)data;

	return phy_power_on(hdmi->phy);
}

static void dw_hdmi_qp_rk3588_phy_disable(struct dw_hdmi_qp *dw_hdmi,
					  void *data)
{
	struct rockchip_hdmi_qp *hdmi = (struct rockchip_hdmi_qp *)data;

	phy_power_off(hdmi->phy);
}

static enum drm_connector_status
dw_hdmi_qp_rk3588_read_hpd(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	struct rockchip_hdmi_qp *hdmi = (struct rockchip_hdmi_qp *)data;
	u32 val;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &val);
	val &= hdmi->port_id ? RK3588_HDMI1_LEVEL_INT : RK3588_HDMI0_LEVEL_INT;

	return val ? connector_status_connected : connector_status_disconnected;
}

static void dw_hdmi_qp_rk3588_setup_hpd(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	struct rockchip_hdmi_qp *hdmi = (struct rockchip_hdmi_qp *)data;
	u32 val;

	if (hdmi->port_id)
		val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_CLR,
				    RK3588_HDMI1_HPD_INT_CLR | RK3588_HDMI1_HPD_INT_MSK);
	else
		val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_CLR,
				    RK3588_HDMI0_HPD_INT_CLR | RK3588_HDMI0_HPD_INT_MSK);

	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);
}

static const struct dw_hdmi_qp_phy_ops rk3588_hdmi_phy_ops = {
	.init		= dw_hdmi_qp_rk3588_phy_init,
	.disable	= dw_hdmi_qp_rk3588_phy_disable,
	.read_hpd	= dw_hdmi_qp_rk3588_read_hpd,
	.setup_hpd	= dw_hdmi_qp_rk3588_setup_hpd,
};

static void dw_hdmi_qp_rk3588_hpd_work(struct work_struct *work)
{
	struct rockchip_hdmi_qp *hdmi = container_of(work,
						     struct rockchip_hdmi_qp,
						     hpd_work.work);
	struct drm_device *drm = hdmi->encoder.encoder.dev;
	bool changed;

	if (drm) {
		changed = drm_helper_hpd_irq_event(drm);
		if (changed)
			drm_dbg(hdmi, "connector status changed\n");
	}
}

static irqreturn_t dw_hdmi_qp_rk3588_hardirq(int irq, void *dev_id)
{
	struct rockchip_hdmi_qp *hdmi = dev_id;
	u32 intr_stat, val;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &intr_stat);

	if (intr_stat) {
		if (hdmi->port_id)
			val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_MSK,
					    RK3588_HDMI1_HPD_INT_MSK);
		else
			val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_MSK,
					    RK3588_HDMI0_HPD_INT_MSK);
		regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t dw_hdmi_qp_rk3588_irq(int irq, void *dev_id)
{
	struct rockchip_hdmi_qp *hdmi = dev_id;
	u32 intr_stat, val;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &intr_stat);
	if (!intr_stat)
		return IRQ_NONE;

	if (hdmi->port_id)
		val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_CLR,
				    RK3588_HDMI1_HPD_INT_CLR);
	else
		val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_CLR,
				    RK3588_HDMI0_HPD_INT_CLR);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	mod_delayed_work(system_wq, &hdmi->hpd_work,
			 msecs_to_jiffies(HOTPLUG_DEBOUNCE_MS));

	if (hdmi->port_id)
		val |= HIWORD_UPDATE(0, RK3588_HDMI1_HPD_INT_MSK);
	else
		val |= HIWORD_UPDATE(0, RK3588_HDMI0_HPD_INT_MSK);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	return IRQ_HANDLED;
}

struct rockchip_hdmi_qp_cfg {
	unsigned int num_ports;
	unsigned int port_ids[MAX_HDMI_PORT_NUM];
	const struct dw_hdmi_qp_phy_ops *phy_ops;
};

static const struct rockchip_hdmi_qp_cfg rk3588_hdmi_cfg = {
	.num_ports = 2,
	.port_ids = {
		0xfde80000,
		0xfdea0000,
	},
	.phy_ops = &rk3588_hdmi_phy_ops,
};

static const struct of_device_id dw_hdmi_qp_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3588-dw-hdmi-qp",
	  .data = &rk3588_hdmi_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_qp_rockchip_dt_ids);

static int dw_hdmi_qp_rockchip_bind(struct device *dev, struct device *master,
				    void *data)
{
	static const char * const clk_names[] = {
		"pclk", "earc", "aud", "hdp", "hclk_vo1",
		"ref" /* keep "ref" last */
	};
	struct platform_device *pdev = to_platform_device(dev);
	const struct rockchip_hdmi_qp_cfg *cfg;
	struct dw_hdmi_qp_plat_data plat_data;
	struct drm_device *drm = data;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct rockchip_hdmi_qp *hdmi;
	struct resource *res;
	struct clk *clk;
	int ret, irq, i;
	u32 val;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	cfg = of_device_get_match_data(dev);
	if (!cfg)
		return -ENODEV;

	hdmi->dev = &pdev->dev;
	hdmi->port_id = -ENODEV;

	/* Identify port ID by matching base IO address */
	for (i = 0; i < cfg->num_ports; i++) {
		if (res->start == cfg->port_ids[i]) {
			hdmi->port_id = i;
			break;
		}
	}
	if (hdmi->port_id < 0) {
		drm_err(hdmi, "Failed to match HDMI port ID\n");
		return hdmi->port_id;
	}

	plat_data.phy_ops = cfg->phy_ops;
	plat_data.phy_data = hdmi;

	encoder = &hdmi->encoder.encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	rockchip_drm_encoder_set_crtc_endpoint_id(&hdmi->encoder,
						  dev->of_node, 0, 0);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
						       "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		drm_err(hdmi, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->vo_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
							  "rockchip,vo-grf");
	if (IS_ERR(hdmi->vo_regmap)) {
		drm_err(hdmi, "Unable to get rockchip,vo-grf\n");
		return PTR_ERR(hdmi->vo_regmap);
	}

	for (i = 0; i < ARRAY_SIZE(clk_names); i++) {
		clk = devm_clk_get_enabled(hdmi->dev, clk_names[i]);

		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			if (ret != -EPROBE_DEFER)
				drm_err(hdmi, "Failed to get %s clock: %d\n",
					clk_names[i], ret);
			return ret;
		}
	}
	hdmi->ref_clk = clk;

	hdmi->enable_gpio = devm_gpiod_get_optional(hdmi->dev, "enable",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(hdmi->enable_gpio)) {
		ret = PTR_ERR(hdmi->enable_gpio);
		drm_err(hdmi, "Failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	hdmi->phy = devm_of_phy_get_by_index(dev, dev->of_node, 0);
	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		if (ret != -EPROBE_DEFER)
			drm_err(hdmi, "failed to get phy: %d\n", ret);
		return ret;
	}

	val = HIWORD_UPDATE(RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
	      HIWORD_UPDATE(RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
	      HIWORD_UPDATE(RK3588_MODE_MASK, RK3588_MODE_MASK) |
	      HIWORD_UPDATE(RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
	regmap_write(hdmi->vo_regmap,
		     hdmi->port_id ? RK3588_GRF_VO1_CON6 : RK3588_GRF_VO1_CON3,
		     val);

	val = HIWORD_UPDATE(RK3588_SET_HPD_PATH_MASK,
			    RK3588_SET_HPD_PATH_MASK);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

	if (hdmi->port_id)
		val = HIWORD_UPDATE(RK3588_HDMI1_GRANT_SEL,
				    RK3588_HDMI1_GRANT_SEL);
	else
		val = HIWORD_UPDATE(RK3588_HDMI0_GRANT_SEL,
				    RK3588_HDMI0_GRANT_SEL);
	regmap_write(hdmi->vo_regmap, RK3588_GRF_VO1_CON9, val);

	if (hdmi->port_id)
		val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_MSK, RK3588_HDMI1_HPD_INT_MSK);
	else
		val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_MSK, RK3588_HDMI0_HPD_INT_MSK);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	INIT_DELAYED_WORK(&hdmi->hpd_work, dw_hdmi_qp_rk3588_hpd_work);

	plat_data.main_irq = platform_get_irq_byname(pdev, "main");
	if (plat_data.main_irq < 0)
		return plat_data.main_irq;

	irq = platform_get_irq_byname(pdev, "hpd");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(hdmi->dev, irq,
					dw_hdmi_qp_rk3588_hardirq,
					dw_hdmi_qp_rk3588_irq,
					IRQF_SHARED, "dw-hdmi-qp-hpd",
					hdmi);
	if (ret)
		return ret;

	drm_encoder_helper_add(encoder, &dw_hdmi_qp_rockchip_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_qp_bind(pdev, encoder, &plat_data);
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		drm_encoder_cleanup(encoder);
		return ret;
	}

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		drm_err(hdmi, "failed to init bridge connector: %d\n", ret);
		return ret;
	}

	return drm_connector_attach_encoder(connector, encoder);
}

static void dw_hdmi_qp_rockchip_unbind(struct device *dev,
				       struct device *master,
				       void *data)
{
	struct rockchip_hdmi_qp *hdmi = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&hdmi->hpd_work);

	drm_encoder_cleanup(&hdmi->encoder.encoder);
}

static const struct component_ops dw_hdmi_qp_rockchip_ops = {
	.bind	= dw_hdmi_qp_rockchip_bind,
	.unbind	= dw_hdmi_qp_rockchip_unbind,
};

static int dw_hdmi_qp_rockchip_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dw_hdmi_qp_rockchip_ops);
}

static void dw_hdmi_qp_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_qp_rockchip_ops);
}

static int __maybe_unused dw_hdmi_qp_rockchip_resume(struct device *dev)
{
	struct rockchip_hdmi_qp *hdmi = dev_get_drvdata(dev);
	u32 val;

	val = HIWORD_UPDATE(RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
	      HIWORD_UPDATE(RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
	      HIWORD_UPDATE(RK3588_MODE_MASK, RK3588_MODE_MASK) |
	      HIWORD_UPDATE(RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
	regmap_write(hdmi->vo_regmap,
		     hdmi->port_id ? RK3588_GRF_VO1_CON6 : RK3588_GRF_VO1_CON3,
		     val);

	val = HIWORD_UPDATE(RK3588_SET_HPD_PATH_MASK,
			    RK3588_SET_HPD_PATH_MASK);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

	if (hdmi->port_id)
		val = HIWORD_UPDATE(RK3588_HDMI1_GRANT_SEL,
				    RK3588_HDMI1_GRANT_SEL);
	else
		val = HIWORD_UPDATE(RK3588_HDMI0_GRANT_SEL,
				    RK3588_HDMI0_GRANT_SEL);
	regmap_write(hdmi->vo_regmap, RK3588_GRF_VO1_CON9, val);

	dw_hdmi_qp_resume(dev, hdmi->hdmi);

	if (hdmi->encoder.encoder.dev)
		drm_helper_hpd_irq_event(hdmi->encoder.encoder.dev);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_qp_rockchip_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, dw_hdmi_qp_rockchip_resume)
};

struct platform_driver dw_hdmi_qp_rockchip_pltfm_driver = {
	.probe = dw_hdmi_qp_rockchip_probe,
	.remove = dw_hdmi_qp_rockchip_remove,
	.driver = {
		.name = "dwhdmiqp-rockchip",
		.pm = &dw_hdmi_qp_rockchip_pm,
		.of_match_table = dw_hdmi_qp_rockchip_dt_ids,
	},
};
