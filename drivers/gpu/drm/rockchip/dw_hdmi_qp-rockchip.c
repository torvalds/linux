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
#include <linux/hw_bitfield.h>
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

#define RK3576_IOC_MISC_CON0		0xa400
#define RK3576_HDMI_HPD_INT_MSK		BIT(2)
#define RK3576_HDMI_HPD_INT_CLR		BIT(1)

#define RK3576_IOC_HDMI_HPD_STATUS	0xa440
#define RK3576_HDMI_LEVEL_INT		BIT(3)

#define RK3576_VO0_GRF_SOC_CON1		0x0004
#define RK3576_HDMI_FRL_MOD		BIT(0)
#define RK3576_HDMI_HDCP14_MEM_EN	BIT(15)

#define RK3576_VO0_GRF_SOC_CON8		0x0020
#define RK3576_COLOR_FORMAT_MASK	(0xf << 4)
#define RK3576_COLOR_DEPTH_MASK		(0xf << 8)
#define RK3576_RGB			(0 << 4)
#define RK3576_YUV422			(0x1 << 4)
#define RK3576_YUV444			(0x2 << 4)
#define RK3576_YUV420			(0x3 << 4)
#define RK3576_8BPC			(0x0 << 8)
#define RK3576_10BPC			(0x6 << 8)
#define RK3576_CECIN_MASK		BIT(3)

#define RK3576_VO0_GRF_SOC_CON12	0x0030
#define RK3576_GRF_OSDA_DLYN		(0xf << 12)
#define RK3576_GRF_OSDA_DIV		(0x7f << 1)
#define RK3576_GRF_OSDA_DLY_EN		BIT(0)

#define RK3576_VO0_GRF_SOC_CON14	0x0038
#define RK3576_I2S_SEL_MASK		BIT(0)
#define RK3576_SPDIF_SEL_MASK		BIT(1)
#define HDCP0_P1_GPIO_IN		BIT(2)
#define RK3576_SCLIN_MASK		BIT(4)
#define RK3576_SDAIN_MASK		BIT(5)
#define RK3576_HDMI_GRANT_SEL		BIT(6)

#define RK3588_GRF_SOC_CON2		0x0308
#define RK3588_HDMI0_HPD_INT_MSK	BIT(13)
#define RK3588_HDMI0_HPD_INT_CLR	BIT(12)
#define RK3588_HDMI1_HPD_INT_MSK	BIT(15)
#define RK3588_HDMI1_HPD_INT_CLR	BIT(14)
#define RK3588_GRF_SOC_CON7		0x031c
#define RK3588_HPD_HDMI0_IO_EN_MASK	BIT(12)
#define RK3588_HPD_HDMI1_IO_EN_MASK	BIT(13)
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

#define HOTPLUG_DEBOUNCE_MS		150
#define MAX_HDMI_PORT_NUM		2

struct rockchip_hdmi_qp {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *vo_regmap;
	struct rockchip_encoder encoder;
	struct dw_hdmi_qp *hdmi;
	struct phy *phy;
	struct gpio_desc *enable_gpio;
	struct delayed_work hpd_work;
	int port_id;
	const struct rockchip_hdmi_qp_ctrl_ops *ctrl_ops;
};

struct rockchip_hdmi_qp_ctrl_ops {
	void (*io_init)(struct rockchip_hdmi_qp *hdmi);
	irqreturn_t (*irq_callback)(int irq, void *dev_id);
	irqreturn_t (*hardirq_callback)(int irq, void *dev_id);
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
		val = (FIELD_PREP_WM16(RK3588_HDMI1_HPD_INT_CLR, 1) |
		       FIELD_PREP_WM16(RK3588_HDMI1_HPD_INT_MSK, 0));
	else
		val = (FIELD_PREP_WM16(RK3588_HDMI0_HPD_INT_CLR, 1) |
		       FIELD_PREP_WM16(RK3588_HDMI0_HPD_INT_MSK, 0));

	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);
}

static const struct dw_hdmi_qp_phy_ops rk3588_hdmi_phy_ops = {
	.init		= dw_hdmi_qp_rk3588_phy_init,
	.disable	= dw_hdmi_qp_rk3588_phy_disable,
	.read_hpd	= dw_hdmi_qp_rk3588_read_hpd,
	.setup_hpd	= dw_hdmi_qp_rk3588_setup_hpd,
};

static enum drm_connector_status
dw_hdmi_qp_rk3576_read_hpd(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	struct rockchip_hdmi_qp *hdmi = (struct rockchip_hdmi_qp *)data;
	u32 val;

	regmap_read(hdmi->regmap, RK3576_IOC_HDMI_HPD_STATUS, &val);

	return val & RK3576_HDMI_LEVEL_INT ?
		connector_status_connected : connector_status_disconnected;
}

static void dw_hdmi_qp_rk3576_setup_hpd(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	struct rockchip_hdmi_qp *hdmi = (struct rockchip_hdmi_qp *)data;
	u32 val;

	val = (FIELD_PREP_WM16(RK3576_HDMI_HPD_INT_CLR, 1) |
	       FIELD_PREP_WM16(RK3576_HDMI_HPD_INT_MSK, 0));

	regmap_write(hdmi->regmap, RK3576_IOC_MISC_CON0, val);
	regmap_write(hdmi->regmap, 0xa404, 0xffff0102);
}

static const struct dw_hdmi_qp_phy_ops rk3576_hdmi_phy_ops = {
	.init		= dw_hdmi_qp_rk3588_phy_init,
	.disable	= dw_hdmi_qp_rk3588_phy_disable,
	.read_hpd	= dw_hdmi_qp_rk3576_read_hpd,
	.setup_hpd	= dw_hdmi_qp_rk3576_setup_hpd,
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
			dev_dbg(hdmi->dev, "connector status changed\n");
	}
}

static irqreturn_t dw_hdmi_qp_rk3576_hardirq(int irq, void *dev_id)
{
	struct rockchip_hdmi_qp *hdmi = dev_id;
	u32 intr_stat, val;

	regmap_read(hdmi->regmap, RK3576_IOC_HDMI_HPD_STATUS, &intr_stat);
	if (intr_stat) {
		val = FIELD_PREP_WM16(RK3576_HDMI_HPD_INT_MSK, 1);

		regmap_write(hdmi->regmap, RK3576_IOC_MISC_CON0, val);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t dw_hdmi_qp_rk3576_irq(int irq, void *dev_id)
{
	struct rockchip_hdmi_qp *hdmi = dev_id;
	u32 intr_stat, val;

	regmap_read(hdmi->regmap, RK3576_IOC_HDMI_HPD_STATUS, &intr_stat);

	if (!intr_stat)
		return IRQ_NONE;

	val = FIELD_PREP_WM16(RK3576_HDMI_HPD_INT_CLR, 1);
	regmap_write(hdmi->regmap, RK3576_IOC_MISC_CON0, val);
	mod_delayed_work(system_wq, &hdmi->hpd_work,
			 msecs_to_jiffies(HOTPLUG_DEBOUNCE_MS));

	val = FIELD_PREP_WM16(RK3576_HDMI_HPD_INT_MSK, 0);
	regmap_write(hdmi->regmap, RK3576_IOC_MISC_CON0, val);

	return IRQ_HANDLED;
}

static irqreturn_t dw_hdmi_qp_rk3588_hardirq(int irq, void *dev_id)
{
	struct rockchip_hdmi_qp *hdmi = dev_id;
	u32 intr_stat, val;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &intr_stat);

	if (intr_stat) {
		if (hdmi->port_id)
			val = FIELD_PREP_WM16(RK3588_HDMI1_HPD_INT_MSK, 1);
		else
			val = FIELD_PREP_WM16(RK3588_HDMI0_HPD_INT_MSK, 1);
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
		val = FIELD_PREP_WM16(RK3588_HDMI1_HPD_INT_CLR, 1);
	else
		val = FIELD_PREP_WM16(RK3588_HDMI0_HPD_INT_CLR, 1);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	mod_delayed_work(system_wq, &hdmi->hpd_work,
			 msecs_to_jiffies(HOTPLUG_DEBOUNCE_MS));

	if (hdmi->port_id)
		val |= FIELD_PREP_WM16(RK3588_HDMI1_HPD_INT_MSK, 0);
	else
		val |= FIELD_PREP_WM16(RK3588_HDMI0_HPD_INT_MSK, 0);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	return IRQ_HANDLED;
}

static void dw_hdmi_qp_rk3576_io_init(struct rockchip_hdmi_qp *hdmi)
{
	u32 val;

	val = FIELD_PREP_WM16(RK3576_SCLIN_MASK, 1) |
	      FIELD_PREP_WM16(RK3576_SDAIN_MASK, 1) |
	      FIELD_PREP_WM16(RK3576_HDMI_GRANT_SEL, 1) |
	      FIELD_PREP_WM16(RK3576_I2S_SEL_MASK, 1);

	regmap_write(hdmi->vo_regmap, RK3576_VO0_GRF_SOC_CON14, val);

	val = FIELD_PREP_WM16(RK3576_HDMI_HPD_INT_MSK, 0);
	regmap_write(hdmi->regmap, RK3576_IOC_MISC_CON0, val);
}

static void dw_hdmi_qp_rk3588_io_init(struct rockchip_hdmi_qp *hdmi)
{
	u32 val;

	val = FIELD_PREP_WM16(RK3588_SCLIN_MASK, 1) |
	      FIELD_PREP_WM16(RK3588_SDAIN_MASK, 1) |
	      FIELD_PREP_WM16(RK3588_MODE_MASK, 1) |
	      FIELD_PREP_WM16(RK3588_I2S_SEL_MASK, 1);
	regmap_write(hdmi->vo_regmap,
		     hdmi->port_id ? RK3588_GRF_VO1_CON6 : RK3588_GRF_VO1_CON3,
		     val);

	val = FIELD_PREP_WM16(RK3588_HPD_HDMI0_IO_EN_MASK, 1) |
	      FIELD_PREP_WM16(RK3588_HPD_HDMI1_IO_EN_MASK, 1);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

	if (hdmi->port_id)
		val = FIELD_PREP_WM16(RK3588_HDMI1_GRANT_SEL, 1);
	else
		val = FIELD_PREP_WM16(RK3588_HDMI0_GRANT_SEL, 1);
	regmap_write(hdmi->vo_regmap, RK3588_GRF_VO1_CON9, val);

	if (hdmi->port_id)
		val = FIELD_PREP_WM16(RK3588_HDMI1_HPD_INT_MSK, 1);
	else
		val = FIELD_PREP_WM16(RK3588_HDMI0_HPD_INT_MSK, 1);
	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);
}

static const struct rockchip_hdmi_qp_ctrl_ops rk3576_hdmi_ctrl_ops = {
	.io_init		= dw_hdmi_qp_rk3576_io_init,
	.irq_callback	        = dw_hdmi_qp_rk3576_irq,
	.hardirq_callback	= dw_hdmi_qp_rk3576_hardirq,
};

static const struct rockchip_hdmi_qp_ctrl_ops rk3588_hdmi_ctrl_ops = {
	.io_init		= dw_hdmi_qp_rk3588_io_init,
	.irq_callback	        = dw_hdmi_qp_rk3588_irq,
	.hardirq_callback	= dw_hdmi_qp_rk3588_hardirq,
};

struct rockchip_hdmi_qp_cfg {
	unsigned int num_ports;
	unsigned int port_ids[MAX_HDMI_PORT_NUM];
	const struct rockchip_hdmi_qp_ctrl_ops *ctrl_ops;
	const struct dw_hdmi_qp_phy_ops *phy_ops;
};

static const struct rockchip_hdmi_qp_cfg rk3576_hdmi_cfg = {
	.num_ports = 1,
	.port_ids = {
		0x27da0000,
	},
	.ctrl_ops = &rk3576_hdmi_ctrl_ops,
	.phy_ops = &rk3576_hdmi_phy_ops,
};

static const struct rockchip_hdmi_qp_cfg rk3588_hdmi_cfg = {
	.num_ports = 2,
	.port_ids = {
		0xfde80000,
		0xfdea0000,
	},
	.ctrl_ops = &rk3588_hdmi_ctrl_ops,
	.phy_ops = &rk3588_hdmi_phy_ops,
};

static const struct of_device_id dw_hdmi_qp_rockchip_dt_ids[] = {
	{
		.compatible = "rockchip,rk3576-dw-hdmi-qp",
		.data = &rk3576_hdmi_cfg
	}, {
		.compatible = "rockchip,rk3588-dw-hdmi-qp",
		.data = &rk3588_hdmi_cfg
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_qp_rockchip_dt_ids);

static int dw_hdmi_qp_rockchip_bind(struct device *dev, struct device *master,
				    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct rockchip_hdmi_qp_cfg *cfg;
	struct dw_hdmi_qp_plat_data plat_data;
	struct drm_device *drm = data;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct rockchip_hdmi_qp *hdmi;
	struct resource *res;
	struct clk_bulk_data *clks;
	int ret, irq, i;

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

	if (!cfg->ctrl_ops || !cfg->ctrl_ops->io_init ||
	    !cfg->ctrl_ops->irq_callback || !cfg->ctrl_ops->hardirq_callback) {
		dev_err(dev, "Missing platform ctrl ops\n");
		return -ENODEV;
	}

	hdmi->ctrl_ops = cfg->ctrl_ops;
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
		dev_err(hdmi->dev, "Failed to match HDMI port ID\n");
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
		dev_err(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->vo_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
							  "rockchip,vo-grf");
	if (IS_ERR(hdmi->vo_regmap)) {
		dev_err(hdmi->dev, "Unable to get rockchip,vo-grf\n");
		return PTR_ERR(hdmi->vo_regmap);
	}

	ret = devm_clk_bulk_get_all_enabled(hdmi->dev, &clks);
	if (ret < 0) {
		dev_err(hdmi->dev, "Failed to get clocks: %d\n", ret);
		return ret;
	}

	hdmi->enable_gpio = devm_gpiod_get_optional(hdmi->dev, "enable",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(hdmi->enable_gpio)) {
		ret = PTR_ERR(hdmi->enable_gpio);
		dev_err(hdmi->dev, "Failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	hdmi->phy = devm_of_phy_get_by_index(dev, dev->of_node, 0);
	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(hdmi->dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	cfg->ctrl_ops->io_init(hdmi);

	INIT_DELAYED_WORK(&hdmi->hpd_work, dw_hdmi_qp_rk3588_hpd_work);

	plat_data.main_irq = platform_get_irq_byname(pdev, "main");
	if (plat_data.main_irq < 0)
		return plat_data.main_irq;

	irq = platform_get_irq_byname(pdev, "hpd");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(hdmi->dev, irq,
					cfg->ctrl_ops->hardirq_callback,
					cfg->ctrl_ops->irq_callback,
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
		dev_err(hdmi->dev, "failed to init bridge connector: %d\n", ret);
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

	hdmi->ctrl_ops->io_init(hdmi);

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
