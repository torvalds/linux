/*
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

#include "hdmi.h"

void hdmi_set_mode(struct hdmi *hdmi, bool power_on)
{
	uint32_t ctrl = 0;

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
	DBG("HDMI Core: %s, HDMI_CTRL=0x%08x",
			power_on ? "Enable" : "Disable", ctrl);
}

irqreturn_t hdmi_irq(int irq, void *dev_id)
{
	struct hdmi *hdmi = dev_id;

	/* Process HPD: */
	hdmi_connector_irq(hdmi->connector);

	/* Process DDC: */
	hdmi_i2c_irq(hdmi->i2c);

	/* TODO audio.. */

	return IRQ_HANDLED;
}

void hdmi_destroy(struct kref *kref)
{
	struct hdmi *hdmi = container_of(kref, struct hdmi, refcount);
	struct hdmi_phy *phy = hdmi->phy;

	if (phy)
		phy->funcs->destroy(phy);

	if (hdmi->i2c)
		hdmi_i2c_destroy(hdmi->i2c);

	platform_set_drvdata(hdmi->pdev, NULL);
}

/* initialize connector */
struct hdmi *hdmi_init(struct drm_device *dev, struct drm_encoder *encoder)
{
	struct hdmi *hdmi = NULL;
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->hdmi_pdev;
	struct hdmi_platform_config *config;
	int i, ret;

	if (!pdev) {
		dev_err(dev->dev, "no hdmi device\n");
		ret = -ENXIO;
		goto fail;
	}

	config = pdev->dev.platform_data;

	hdmi = kzalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi) {
		ret = -ENOMEM;
		goto fail;
	}

	kref_init(&hdmi->refcount);

	hdmi->dev = dev;
	hdmi->pdev = pdev;
	hdmi->config = config;
	hdmi->encoder = encoder;

	hdmi_audio_infoframe_init(&hdmi->audio.infoframe);

	/* not sure about which phy maps to which msm.. probably I miss some */
	if (config->phy_init)
		hdmi->phy = config->phy_init(hdmi);
	else
		hdmi->phy = ERR_PTR(-ENXIO);

	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		dev_err(dev->dev, "failed to load phy: %d\n", ret);
		hdmi->phy = NULL;
		goto fail;
	}

	hdmi->mmio = msm_ioremap(pdev, config->mmio_name, "HDMI");
	if (IS_ERR(hdmi->mmio)) {
		ret = PTR_ERR(hdmi->mmio);
		goto fail;
	}

	BUG_ON(config->hpd_reg_cnt > ARRAY_SIZE(hdmi->hpd_regs));
	for (i = 0; i < config->hpd_reg_cnt; i++) {
		struct regulator *reg;

		reg = devm_regulator_get_exclusive(&pdev->dev,
				config->hpd_reg_names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			dev_err(dev->dev, "failed to get hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
			goto fail;
		}

		hdmi->hpd_regs[i] = reg;
	}

	BUG_ON(config->pwr_reg_cnt > ARRAY_SIZE(hdmi->pwr_regs));
	for (i = 0; i < config->pwr_reg_cnt; i++) {
		struct regulator *reg;

		reg = devm_regulator_get_exclusive(&pdev->dev,
				config->pwr_reg_names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			dev_err(dev->dev, "failed to get pwr regulator: %s (%d)\n",
					config->pwr_reg_names[i], ret);
			goto fail;
		}

		hdmi->pwr_regs[i] = reg;
	}

	BUG_ON(config->hpd_clk_cnt > ARRAY_SIZE(hdmi->hpd_clks));
	for (i = 0; i < config->hpd_clk_cnt; i++) {
		struct clk *clk;

		clk = devm_clk_get(&pdev->dev, config->hpd_clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(dev->dev, "failed to get hpd clk: %s (%d)\n",
					config->hpd_clk_names[i], ret);
			goto fail;
		}

		hdmi->hpd_clks[i] = clk;
	}

	BUG_ON(config->pwr_clk_cnt > ARRAY_SIZE(hdmi->pwr_clks));
	for (i = 0; i < config->pwr_clk_cnt; i++) {
		struct clk *clk;

		clk = devm_clk_get(&pdev->dev, config->pwr_clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(dev->dev, "failed to get pwr clk: %s (%d)\n",
					config->pwr_clk_names[i], ret);
			goto fail;
		}

		hdmi->pwr_clks[i] = clk;
	}

	hdmi->i2c = hdmi_i2c_init(hdmi);
	if (IS_ERR(hdmi->i2c)) {
		ret = PTR_ERR(hdmi->i2c);
		dev_err(dev->dev, "failed to get i2c: %d\n", ret);
		hdmi->i2c = NULL;
		goto fail;
	}

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

	if (!config->shared_irq) {
		hdmi->irq = platform_get_irq(pdev, 0);
		if (hdmi->irq < 0) {
			ret = hdmi->irq;
			dev_err(dev->dev, "failed to get irq: %d\n", ret);
			goto fail;
		}

		ret = devm_request_threaded_irq(&pdev->dev, hdmi->irq,
				NULL, hdmi_irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				"hdmi_isr", hdmi);
		if (ret < 0) {
			dev_err(dev->dev, "failed to request IRQ%u: %d\n",
					hdmi->irq, ret);
			goto fail;
		}
	}

	encoder->bridge = hdmi->bridge;

	priv->bridges[priv->num_bridges++]       = hdmi->bridge;
	priv->connectors[priv->num_connectors++] = hdmi->connector;

	platform_set_drvdata(pdev, hdmi);

	return hdmi;

fail:
	if (hdmi) {
		/* bridge/connector are normally destroyed by drm: */
		if (hdmi->bridge)
			hdmi->bridge->funcs->destroy(hdmi->bridge);
		if (hdmi->connector)
			hdmi->connector->funcs->destroy(hdmi->connector);
		hdmi_destroy(&hdmi->refcount);
	}

	return ERR_PTR(ret);
}

/*
 * The hdmi device:
 */

#include <linux/of_gpio.h>

static void set_hdmi_pdev(struct drm_device *dev,
		struct platform_device *pdev)
{
	struct msm_drm_private *priv = dev->dev_private;
	priv->hdmi_pdev = pdev;
}

static int hdmi_bind(struct device *dev, struct device *master, void *data)
{
	static struct hdmi_platform_config config = {};
#ifdef CONFIG_OF
	struct device_node *of_node = dev->of_node;

	int get_gpio(const char *name)
	{
		int gpio = of_get_named_gpio(of_node, name, 0);
		if (gpio < 0) {
			char name2[32];
			snprintf(name2, sizeof(name2), "%s-gpio", name);
			gpio = of_get_named_gpio(of_node, name2, 0);
			if (gpio < 0) {
				dev_err(dev, "failed to get gpio: %s (%d)\n",
						name, gpio);
				gpio = -1;
			}
		}
		return gpio;
	}

	if (of_device_is_compatible(of_node, "qcom,hdmi-tx-8074")) {
		static const char *hpd_reg_names[] = {"hpd-gdsc", "hpd-5v"};
		static const char *pwr_reg_names[] = {"core-vdda", "core-vcc"};
		static const char *hpd_clk_names[] = {"iface_clk", "core_clk", "mdp_core_clk"};
		static unsigned long hpd_clk_freq[] = {0, 19200000, 0};
		static const char *pwr_clk_names[] = {"extp_clk", "alt_iface_clk"};
		config.phy_init      = hdmi_phy_8x74_init;
		config.hpd_reg_names = hpd_reg_names;
		config.hpd_reg_cnt   = ARRAY_SIZE(hpd_reg_names);
		config.pwr_reg_names = pwr_reg_names;
		config.pwr_reg_cnt   = ARRAY_SIZE(pwr_reg_names);
		config.hpd_clk_names = hpd_clk_names;
		config.hpd_freq      = hpd_clk_freq;
		config.hpd_clk_cnt   = ARRAY_SIZE(hpd_clk_names);
		config.pwr_clk_names = pwr_clk_names;
		config.pwr_clk_cnt   = ARRAY_SIZE(pwr_clk_names);
		config.shared_irq    = true;
	} else if (of_device_is_compatible(of_node, "qcom,hdmi-tx-8960")) {
		static const char *hpd_clk_names[] = {"core_clk", "master_iface_clk", "slave_iface_clk"};
		static const char *hpd_reg_names[] = {"core-vdda", "hdmi-mux"};
		config.phy_init      = hdmi_phy_8960_init;
		config.hpd_reg_names = hpd_reg_names;
		config.hpd_reg_cnt   = ARRAY_SIZE(hpd_reg_names);
		config.hpd_clk_names = hpd_clk_names;
		config.hpd_clk_cnt   = ARRAY_SIZE(hpd_clk_names);
	} else if (of_device_is_compatible(of_node, "qcom,hdmi-tx-8660")) {
		config.phy_init      = hdmi_phy_8x60_init;
	} else {
		dev_err(dev, "unknown phy: %s\n", of_node->name);
	}

	config.mmio_name     = "core_physical";
	config.ddc_clk_gpio  = get_gpio("qcom,hdmi-tx-ddc-clk");
	config.ddc_data_gpio = get_gpio("qcom,hdmi-tx-ddc-data");
	config.hpd_gpio      = get_gpio("qcom,hdmi-tx-hpd");
	config.mux_en_gpio   = get_gpio("qcom,hdmi-tx-mux-en");
	config.mux_sel_gpio  = get_gpio("qcom,hdmi-tx-mux-sel");

#else
	static const char *hpd_clk_names[] = {
			"core_clk", "master_iface_clk", "slave_iface_clk",
	};
	if (cpu_is_apq8064()) {
		static const char *hpd_reg_names[] = {"8921_hdmi_mvs"};
		config.phy_init      = hdmi_phy_8960_init;
		config.mmio_name     = "hdmi_msm_hdmi_addr";
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
		config.mmio_name     = "hdmi_msm_hdmi_addr";
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
		config.mmio_name     = "hdmi_msm_hdmi_addr";
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
#endif
	dev->platform_data = &config;
	set_hdmi_pdev(dev_get_drvdata(master), to_platform_device(dev));
	return 0;
}

static void hdmi_unbind(struct device *dev, struct device *master,
		void *data)
{
	set_hdmi_pdev(dev_get_drvdata(master), NULL);
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

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,hdmi-tx-8074" },
	{ .compatible = "qcom,hdmi-tx-8960" },
	{ .compatible = "qcom,hdmi-tx-8660" },
	{}
};

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
