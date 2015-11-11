/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/of_irq.h>
#include "hdmi.h"

void hdmi_set_mode(struct hdmi *hdmi, bool power_on)
{
	uint32_t ctrl = 0;
	unsigned long flags;

	spin_lock_irqsave(&hdmi->reg_lock, flags);
	if (power_on) {
		ctrl |= HDMI_CTRL_ENABLE;
		if (!hdmi->hdmi_mode) {
			ctrl |= HDMI_CTRL_HDMI;
			hdmi_write(hdmi, REG_HDMI_CTRL, ctrl);
			ctrl &= ~HDMI_CTRL_HDMI;
		} else {
			ctrl |= HDMI_CTRL_HDMI;
		}
	} else {
		ctrl = HDMI_CTRL_HDMI;
	}

	hdmi_write(hdmi, REG_HDMI_CTRL, ctrl);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);
	DBG("HDMI Core: %s, HDMI_CTRL=0x%08x",
			power_on ? "Enable" : "Disable", ctrl);
}

static irqreturn_t hdmi_irq(int irq, void *dev_id)
{
	struct hdmi *hdmi = dev_id;

	/* Process HPD: */
	hdmi_connector_irq(hdmi->connector);

	/* Process DDC: */
	hdmi_i2c_irq(hdmi->i2c);

	/* Process HDCP: */
	if (hdmi->hdcp_ctrl)
		hdmi_hdcp_irq(hdmi->hdcp_ctrl);

	/* TODO audio.. */

	return IRQ_HANDLED;
}

static void hdmi_destroy(struct hdmi *hdmi)
{
	struct hdmi_phy *phy = hdmi->phy;

	/*
	 * at this point, hpd has been disabled,
	 * after flush workq, it's safe to deinit hdcp
	 */
	if (hdmi->workq) {
		flush_workqueue(hdmi->workq);
		destroy_workqueue(hdmi->workq);
	}
	hdmi_hdcp_destroy(hdmi);
	if (phy)
		phy->funcs->destroy(phy);

	if (hdmi->i2c)
		hdmi_i2c_destroy(hdmi->i2c);

	platform_set_drvdata(hdmi->pdev, NULL);
}

/* construct hdmi at bind/probe time, grab all the resources.  If
 * we are to EPROBE_DEFER we want to do it here, rather than later
 * at modeset_init() time
 */
static struct hdmi *hdmi_init(struct platform_device *pdev)
{
	struct hdmi_platform_config *config = pdev->dev.platform_data;
	struct hdmi *hdmi = NULL;
	struct resource *res;
	int i, ret;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi) {
		ret = -ENOMEM;
		goto fail;
	}

	hdmi->pdev = pdev;
	hdmi->config = config;
	spin_lock_init(&hdmi->reg_lock);

	/* not sure about which phy maps to which msm.. probably I miss some */
	if (config->phy_init) {
		hdmi->phy = config->phy_init(hdmi);

		if (IS_ERR(hdmi->phy)) {
			ret = PTR_ERR(hdmi->phy);
			dev_err(&pdev->dev, "failed to load phy: %d\n", ret);
			hdmi->phy = NULL;
			goto fail;
		}
	}

	hdmi->mmio = msm_ioremap(pdev, config->mmio_name, "HDMI");
	if (IS_ERR(hdmi->mmio)) {
		ret = PTR_ERR(hdmi->mmio);
		goto fail;
	}

	/* HDCP needs physical address of hdmi register */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
		config->mmio_name);
	hdmi->mmio_phy_addr = res->start;

	hdmi->qfprom_mmio = msm_ioremap(pdev,
		config->qfprom_mmio_name, "HDMI_QFPROM");
	if (IS_ERR(hdmi->qfprom_mmio)) {
		dev_info(&pdev->dev, "can't find qfprom resource\n");
		hdmi->qfprom_mmio = NULL;
	}

	hdmi->hpd_regs = devm_kzalloc(&pdev->dev, sizeof(hdmi->hpd_regs[0]) *
			config->hpd_reg_cnt, GFP_KERNEL);
	if (!hdmi->hpd_regs) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < config->hpd_reg_cnt; i++) {
		struct regulator *reg;

		reg = devm_regulator_get(&pdev->dev,
				config->hpd_reg_names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			dev_err(&pdev->dev, "failed to get hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
			goto fail;
		}

		hdmi->hpd_regs[i] = reg;
	}

	hdmi->pwr_regs = devm_kzalloc(&pdev->dev, sizeof(hdmi->pwr_regs[0]) *
			config->pwr_reg_cnt, GFP_KERNEL);
	if (!hdmi->pwr_regs) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < config->pwr_reg_cnt; i++) {
		struct regulator *reg;

		reg = devm_regulator_get(&pdev->dev,
				config->pwr_reg_names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			dev_err(&pdev->dev, "failed to get pwr regulator: %s (%d)\n",
					config->pwr_reg_names[i], ret);
			goto fail;
		}

		hdmi->pwr_regs[i] = reg;
	}

	hdmi->hpd_clks = devm_kzalloc(&pdev->dev, sizeof(hdmi->hpd_clks[0]) *
			config->hpd_clk_cnt, GFP_KERNEL);
	if (!hdmi->hpd_clks) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < config->hpd_clk_cnt; i++) {
		struct clk *clk;

		clk = devm_clk_get(&pdev->dev, config->hpd_clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(&pdev->dev, "failed to get hpd clk: %s (%d)\n",
					config->hpd_clk_names[i], ret);
			goto fail;
		}

		hdmi->hpd_clks[i] = clk;
	}

	hdmi->pwr_clks = devm_kzalloc(&pdev->dev, sizeof(hdmi->pwr_clks[0]) *
			config->pwr_clk_cnt, GFP_KERNEL);
	if (!hdmi->pwr_clks) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < config->pwr_clk_cnt; i++) {
		struct clk *clk;

		clk = devm_clk_get(&pdev->dev, config->pwr_clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(&pdev->dev, "failed to get pwr clk: %s (%d)\n",
					config->pwr_clk_names[i], ret);
			goto fail;
		}

		hdmi->pwr_clks[i] = clk;
	}

	hdmi->workq = alloc_ordered_workqueue("msm_hdmi", 0);

	hdmi->i2c = hdmi_i2c_init(hdmi);
	if (IS_ERR(hdmi->i2c)) {
		ret = PTR_ERR(hdmi->i2c);
		dev_err(&pdev->dev, "failed to get i2c: %d\n", ret);
		hdmi->i2c = NULL;
		goto fail;
	}

	hdmi->hdcp_ctrl = hdmi_hdcp_init(hdmi);
	if (IS_ERR(hdmi->hdcp_ctrl)) {
		dev_warn(&pdev->dev, "failed to init hdcp: disabled\n");
		hdmi->hdcp_ctrl = NULL;
	}

	return hdmi;

fail:
	if (hdmi)
		hdmi_destroy(hdmi);

	return ERR_PTR(ret);
}

/* Second part of initialization, the drm/kms level modeset_init,
 * constructs/initializes mode objects, etc, is called from master
 * driver (not hdmi sub-device's probe/bind!)
 *
 * Any resource (regulator/clk/etc) which could be missing at boot
 * should be handled in hdmi_init() so that failure happens from
 * hdmi sub-device's probe.
 */
int hdmi_modeset_init(struct hdmi *hdmi,
		struct drm_device *dev, struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = hdmi->pdev;
	int ret;

	hdmi->dev = dev;
	hdmi->encoder = encoder;

	hdmi_audio_infoframe_init(&hdmi->audio.infoframe);

	hdmi->bridge = hdmi_bridge_init(hdmi);
	if (IS_ERR(hdmi->bridge)) {
		ret = PTR_ERR(hdmi->bridge);
		dev_err(dev->dev, "failed to create HDMI bridge: %d\n", ret);
		hdmi->bridge = NULL;
		goto fail;
	}

	hdmi->connector = hdmi_connector_init(hdmi);
	if (IS_ERR(hdmi->connector)) {
		ret = PTR_ERR(hdmi->connector);
		dev_err(dev->dev, "failed to create HDMI connector: %d\n", ret);
		hdmi->connector = NULL;
		goto fail;
	}

	hdmi->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (hdmi->irq < 0) {
		ret = hdmi->irq;
		dev_err(dev->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, hdmi->irq,
			hdmi_irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"hdmi_isr", hdmi);
	if (ret < 0) {
		dev_err(dev->dev, "failed to request IRQ%u: %d\n",
				hdmi->irq, ret);
		goto fail;
	}

	encoder->bridge = hdmi->bridge;

	priv->bridges[priv->num_bridges++]       = hdmi->bridge;
	priv->connectors[priv->num_connectors++] = hdmi->connector;

	platform_set_drvdata(pdev, hdmi);

	return 0;

fail:
	/* bridge is normally destroyed by drm: */
	if (hdmi->bridge) {
		hdmi_bridge_destroy(hdmi->bridge);
		hdmi->bridge = NULL;
	}
	if (hdmi->connector) {
		hdmi->connector->funcs->destroy(hdmi->connector);
		hdmi->connector = NULL;
	}

	return ret;
}

/*
 * The hdmi device:
 */

#include <linux/of_gpio.h>

#define HDMI_CFG(item, entry) \
	.item ## _names = item ##_names_ ## entry, \
	.item ## _cnt   = ARRAY_SIZE(item ## _names_ ## entry)

static const char *pwr_reg_names_none[] = {};
static const char *hpd_reg_names_none[] = {};

static struct hdmi_platform_config hdmi_tx_8660_config = {
		.phy_init = hdmi_phy_8x60_init,
};

static const char *hpd_reg_names_8960[] = {"core-vdda", "hdmi-mux"};
static const char *hpd_clk_names_8960[] = {"core_clk", "master_iface_clk", "slave_iface_clk"};

static struct hdmi_platform_config hdmi_tx_8960_config = {
		.phy_init = hdmi_phy_8960_init,
		HDMI_CFG(hpd_reg, 8960),
		HDMI_CFG(hpd_clk, 8960),
};

static const char *pwr_reg_names_8x74[] = {"core-vdda", "core-vcc"};
static const char *hpd_reg_names_8x74[] = {"hpd-gdsc", "hpd-5v"};
static const char *pwr_clk_names_8x74[] = {"extp_clk", "alt_iface_clk"};
static const char *hpd_clk_names_8x74[] = {"iface_clk", "core_clk", "mdp_core_clk"};
static unsigned long hpd_clk_freq_8x74[] = {0, 19200000, 0};

static struct hdmi_platform_config hdmi_tx_8974_config = {
		.phy_init = hdmi_phy_8x74_init,
		HDMI_CFG(pwr_reg, 8x74),
		HDMI_CFG(hpd_reg, 8x74),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static const char *hpd_reg_names_8084[] = {"hpd-gdsc", "hpd-5v", "hpd-5v-en"};

static struct hdmi_platform_config hdmi_tx_8084_config = {
		.phy_init = hdmi_phy_8x74_init,
		HDMI_CFG(pwr_reg, 8x74),
		HDMI_CFG(hpd_reg, 8084),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static struct hdmi_platform_config hdmi_tx_8994_config = {
		.phy_init = NULL, /* nothing to do for this HDMI PHY 20nm */
		HDMI_CFG(pwr_reg, 8x74),
		HDMI_CFG(hpd_reg, none),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static struct hdmi_platform_config hdmi_tx_8996_config = {
		.phy_init = NULL,
		HDMI_CFG(pwr_reg, none),
		HDMI_CFG(hpd_reg, none),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,hdmi-tx-8996", .data = &hdmi_tx_8996_config },
	{ .compatible = "qcom,hdmi-tx-8994", .data = &hdmi_tx_8994_config },
	{ .compatible = "qcom,hdmi-tx-8084", .data = &hdmi_tx_8084_config },
	{ .compatible = "qcom,hdmi-tx-8974", .data = &hdmi_tx_8974_config },
	{ .compatible = "qcom,hdmi-tx-8960", .data = &hdmi_tx_8960_config },
	{ .compatible = "qcom,hdmi-tx-8660", .data = &hdmi_tx_8660_config },
	{}
};

#ifdef CONFIG_OF
static int get_gpio(struct device *dev, struct device_node *of_node, const char *name)
{
	int gpio = of_get_named_gpio(of_node, name, 0);
	if (gpio < 0) {
		char name2[32];
		snprintf(name2, sizeof(name2), "%s-gpio", name);
		gpio = of_get_named_gpio(of_node, name2, 0);
		if (gpio < 0) {
			DBG("failed to get gpio: %s (%d)", name, gpio);
			gpio = -1;
		}
	}
	return gpio;
}
#endif

static int hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	static struct hdmi_platform_config *hdmi_cfg;
	struct hdmi *hdmi;
#ifdef CONFIG_OF
	struct device_node *of_node = dev->of_node;
	const struct of_device_id *match;

	match = of_match_node(dt_match, of_node);
	if (match && match->data) {
		hdmi_cfg = (struct hdmi_platform_config *)match->data;
		DBG("hdmi phy: %s", match->compatible);
	} else {
		dev_err(dev, "unknown phy: %s\n", of_node->name);
		return -ENXIO;
	}

	hdmi_cfg->mmio_name     = "core_physical";
	hdmi_cfg->qfprom_mmio_name = "qfprom_physical";
	hdmi_cfg->ddc_clk_gpio  = get_gpio(dev, of_node, "qcom,hdmi-tx-ddc-clk");
	hdmi_cfg->ddc_data_gpio = get_gpio(dev, of_node, "qcom,hdmi-tx-ddc-data");
	hdmi_cfg->hpd_gpio      = get_gpio(dev, of_node, "qcom,hdmi-tx-hpd");
	hdmi_cfg->mux_en_gpio   = get_gpio(dev, of_node, "qcom,hdmi-tx-mux-en");
	hdmi_cfg->mux_sel_gpio  = get_gpio(dev, of_node, "qcom,hdmi-tx-mux-sel");
	hdmi_cfg->mux_lpm_gpio  = get_gpio(dev, of_node, "qcom,hdmi-tx-mux-lpm");

#else
	static struct hdmi_platform_config config = {};
	static const char *hpd_clk_names[] = {
			"core_clk", "master_iface_clk", "slave_iface_clk",
	};
	if (cpu_is_apq8064()) {
		static const char *hpd_reg_names[] = {"8921_hdmi_mvs"};
		config.phy_init      = hdmi_phy_8960_init;
		config.hpd_reg_names = hpd_reg_names;
		config.hpd_reg_cnt   = ARRAY_SIZE(hpd_reg_names);
		config.hpd_clk_names = hpd_clk_names;
		config.hpd_clk_cnt   = ARRAY_SIZE(hpd_clk_names);
		config.ddc_clk_gpio  = 70;
		config.ddc_data_gpio = 71;
		config.hpd_gpio      = 72;
		config.mux_en_gpio   = -1;
		config.mux_sel_gpio  = -1;
	} else if (cpu_is_msm8960() || cpu_is_msm8960ab()) {
		static const char *hpd_reg_names[] = {"8921_hdmi_mvs"};
		config.phy_init      = hdmi_phy_8960_init;
		config.hpd_reg_names = hpd_reg_names;
		config.hpd_reg_cnt   = ARRAY_SIZE(hpd_reg_names);
		config.hpd_clk_names = hpd_clk_names;
		config.hpd_clk_cnt   = ARRAY_SIZE(hpd_clk_names);
		config.ddc_clk_gpio  = 100;
		config.ddc_data_gpio = 101;
		config.hpd_gpio      = 102;
		config.mux_en_gpio   = -1;
		config.mux_sel_gpio  = -1;
	} else if (cpu_is_msm8x60()) {
		static const char *hpd_reg_names[] = {
				"8901_hdmi_mvs", "8901_mpp0"
		};
		config.phy_init      = hdmi_phy_8x60_init;
		config.hpd_reg_names = hpd_reg_names;
		config.hpd_reg_cnt   = ARRAY_SIZE(hpd_reg_names);
		config.hpd_clk_names = hpd_clk_names;
		config.hpd_clk_cnt   = ARRAY_SIZE(hpd_clk_names);
		config.ddc_clk_gpio  = 170;
		config.ddc_data_gpio = 171;
		config.hpd_gpio      = 172;
		config.mux_en_gpio   = -1;
		config.mux_sel_gpio  = -1;
	}
	config.mmio_name     = "hdmi_msm_hdmi_addr";
	config.qfprom_mmio_name = "hdmi_msm_qfprom_addr";

	hdmi_cfg = &config;
#endif
	dev->platform_data = hdmi_cfg;

	hdmi = hdmi_init(to_platform_device(dev));
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);
	priv->hdmi = hdmi;

	return 0;
}

static void hdmi_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	if (priv->hdmi) {
		hdmi_destroy(priv->hdmi);
		priv->hdmi = NULL;
	}
}

static const struct component_ops hdmi_ops = {
		.bind   = hdmi_bind,
		.unbind = hdmi_unbind,
};

static int hdmi_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &hdmi_ops);
}

static int hdmi_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &hdmi_ops);
	return 0;
}

static struct platform_driver hdmi_driver = {
	.probe = hdmi_dev_probe,
	.remove = hdmi_dev_remove,
	.driver = {
		.name = "hdmi_msm",
		.of_match_table = dt_match,
	},
};

void __init hdmi_register(void)
{
	platform_driver_register(&hdmi_driver);
}

void __exit hdmi_unregister(void)
{
	platform_driver_unregister(&hdmi_driver);
}
