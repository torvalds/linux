// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/of_irq.h>
#include <linux/of_gpio.h>

#include <sound/hdmi-codec.h>
#include "hdmi.h"

void msm_hdmi_set_mode(struct hdmi *hdmi, bool power_on)
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

static irqreturn_t msm_hdmi_irq(int irq, void *dev_id)
{
	struct hdmi *hdmi = dev_id;

	/* Process HPD: */
	msm_hdmi_connector_irq(hdmi->connector);

	/* Process DDC: */
	msm_hdmi_i2c_irq(hdmi->i2c);

	/* Process HDCP: */
	if (hdmi->hdcp_ctrl)
		msm_hdmi_hdcp_irq(hdmi->hdcp_ctrl);

	/* TODO audio.. */

	return IRQ_HANDLED;
}

static void msm_hdmi_destroy(struct hdmi *hdmi)
{
	/*
	 * at this point, hpd has been disabled,
	 * after flush workq, it's safe to deinit hdcp
	 */
	if (hdmi->workq) {
		flush_workqueue(hdmi->workq);
		destroy_workqueue(hdmi->workq);
	}
	msm_hdmi_hdcp_destroy(hdmi);

	if (hdmi->phy_dev) {
		put_device(hdmi->phy_dev);
		hdmi->phy = NULL;
		hdmi->phy_dev = NULL;
	}

	if (hdmi->i2c)
		msm_hdmi_i2c_destroy(hdmi->i2c);

	platform_set_drvdata(hdmi->pdev, NULL);
}

static int msm_hdmi_get_phy(struct hdmi *hdmi)
{
	struct platform_device *pdev = hdmi->pdev;
	struct platform_device *phy_pdev;
	struct device_node *phy_node;

	phy_node = of_parse_phandle(pdev->dev.of_node, "phys", 0);
	if (!phy_node) {
		DRM_DEV_ERROR(&pdev->dev, "cannot find phy device\n");
		return -ENXIO;
	}

	phy_pdev = of_find_device_by_node(phy_node);
	if (phy_pdev)
		hdmi->phy = platform_get_drvdata(phy_pdev);

	of_node_put(phy_node);

	if (!phy_pdev) {
		DRM_DEV_ERROR(&pdev->dev, "phy driver is not ready\n");
		return -EPROBE_DEFER;
	}
	if (!hdmi->phy) {
		DRM_DEV_ERROR(&pdev->dev, "phy driver is not ready\n");
		put_device(&phy_pdev->dev);
		return -EPROBE_DEFER;
	}

	hdmi->phy_dev = get_device(&phy_pdev->dev);

	return 0;
}

/* construct hdmi at bind/probe time, grab all the resources.  If
 * we are to EPROBE_DEFER we want to do it here, rather than later
 * at modeset_init() time
 */
static struct hdmi *msm_hdmi_init(struct platform_device *pdev)
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

	hdmi->mmio = msm_ioremap(pdev, config->mmio_name, "HDMI");
	if (IS_ERR(hdmi->mmio)) {
		ret = PTR_ERR(hdmi->mmio);
		goto fail;
	}

	/* HDCP needs physical address of hdmi register */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
		config->mmio_name);
	if (!res) {
		ret = -EINVAL;
		goto fail;
	}
	hdmi->mmio_phy_addr = res->start;

	hdmi->qfprom_mmio = msm_ioremap(pdev,
		config->qfprom_mmio_name, "HDMI_QFPROM");
	if (IS_ERR(hdmi->qfprom_mmio)) {
		DRM_DEV_INFO(&pdev->dev, "can't find qfprom resource\n");
		hdmi->qfprom_mmio = NULL;
	}

	hdmi->hpd_regs = devm_kcalloc(&pdev->dev,
				      config->hpd_reg_cnt,
				      sizeof(hdmi->hpd_regs[0]),
				      GFP_KERNEL);
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
			DRM_DEV_ERROR(&pdev->dev, "failed to get hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
			goto fail;
		}

		hdmi->hpd_regs[i] = reg;
	}

	hdmi->pwr_regs = devm_kcalloc(&pdev->dev,
				      config->pwr_reg_cnt,
				      sizeof(hdmi->pwr_regs[0]),
				      GFP_KERNEL);
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
			DRM_DEV_ERROR(&pdev->dev, "failed to get pwr regulator: %s (%d)\n",
					config->pwr_reg_names[i], ret);
			goto fail;
		}

		hdmi->pwr_regs[i] = reg;
	}

	hdmi->hpd_clks = devm_kcalloc(&pdev->dev,
				      config->hpd_clk_cnt,
				      sizeof(hdmi->hpd_clks[0]),
				      GFP_KERNEL);
	if (!hdmi->hpd_clks) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < config->hpd_clk_cnt; i++) {
		struct clk *clk;

		clk = msm_clk_get(pdev, config->hpd_clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			DRM_DEV_ERROR(&pdev->dev, "failed to get hpd clk: %s (%d)\n",
					config->hpd_clk_names[i], ret);
			goto fail;
		}

		hdmi->hpd_clks[i] = clk;
	}

	hdmi->pwr_clks = devm_kcalloc(&pdev->dev,
				      config->pwr_clk_cnt,
				      sizeof(hdmi->pwr_clks[0]),
				      GFP_KERNEL);
	if (!hdmi->pwr_clks) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < config->pwr_clk_cnt; i++) {
		struct clk *clk;

		clk = msm_clk_get(pdev, config->pwr_clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			DRM_DEV_ERROR(&pdev->dev, "failed to get pwr clk: %s (%d)\n",
					config->pwr_clk_names[i], ret);
			goto fail;
		}

		hdmi->pwr_clks[i] = clk;
	}

	pm_runtime_enable(&pdev->dev);

	hdmi->workq = alloc_ordered_workqueue("msm_hdmi", 0);

	hdmi->i2c = msm_hdmi_i2c_init(hdmi);
	if (IS_ERR(hdmi->i2c)) {
		ret = PTR_ERR(hdmi->i2c);
		DRM_DEV_ERROR(&pdev->dev, "failed to get i2c: %d\n", ret);
		hdmi->i2c = NULL;
		goto fail;
	}

	ret = msm_hdmi_get_phy(hdmi);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to get phy\n");
		goto fail;
	}

	hdmi->hdcp_ctrl = msm_hdmi_hdcp_init(hdmi);
	if (IS_ERR(hdmi->hdcp_ctrl)) {
		dev_warn(&pdev->dev, "failed to init hdcp: disabled\n");
		hdmi->hdcp_ctrl = NULL;
	}

	return hdmi;

fail:
	if (hdmi)
		msm_hdmi_destroy(hdmi);

	return ERR_PTR(ret);
}

/* Second part of initialization, the drm/kms level modeset_init,
 * constructs/initializes mode objects, etc, is called from master
 * driver (not hdmi sub-device's probe/bind!)
 *
 * Any resource (regulator/clk/etc) which could be missing at boot
 * should be handled in msm_hdmi_init() so that failure happens from
 * hdmi sub-device's probe.
 */
int msm_hdmi_modeset_init(struct hdmi *hdmi,
		struct drm_device *dev, struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = hdmi->pdev;
	int ret;

	if (priv->num_bridges == ARRAY_SIZE(priv->bridges)) {
		DRM_DEV_ERROR(dev->dev, "too many bridges\n");
		return -ENOSPC;
	}

	hdmi->dev = dev;
	hdmi->encoder = encoder;

	hdmi_audio_infoframe_init(&hdmi->audio.infoframe);

	hdmi->bridge = msm_hdmi_bridge_init(hdmi);
	if (IS_ERR(hdmi->bridge)) {
		ret = PTR_ERR(hdmi->bridge);
		DRM_DEV_ERROR(dev->dev, "failed to create HDMI bridge: %d\n", ret);
		hdmi->bridge = NULL;
		goto fail;
	}

	hdmi->connector = msm_hdmi_connector_init(hdmi);
	if (IS_ERR(hdmi->connector)) {
		ret = PTR_ERR(hdmi->connector);
		DRM_DEV_ERROR(dev->dev, "failed to create HDMI connector: %d\n", ret);
		hdmi->connector = NULL;
		goto fail;
	}

	hdmi->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!hdmi->irq) {
		ret = -EINVAL;
		DRM_DEV_ERROR(dev->dev, "failed to get irq\n");
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, hdmi->irq,
			msm_hdmi_irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"hdmi_isr", hdmi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "failed to request IRQ%u: %d\n",
				hdmi->irq, ret);
		goto fail;
	}

	ret = msm_hdmi_hpd_enable(hdmi->connector);
	if (ret < 0) {
		DRM_DEV_ERROR(&hdmi->pdev->dev, "failed to enable HPD: %d\n", ret);
		goto fail;
	}

	priv->bridges[priv->num_bridges++]       = hdmi->bridge;
	priv->connectors[priv->num_connectors++] = hdmi->connector;

	platform_set_drvdata(pdev, hdmi);

	return 0;

fail:
	/* bridge is normally destroyed by drm: */
	if (hdmi->bridge) {
		msm_hdmi_bridge_destroy(hdmi->bridge);
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

#define HDMI_CFG(item, entry) \
	.item ## _names = item ##_names_ ## entry, \
	.item ## _cnt   = ARRAY_SIZE(item ## _names_ ## entry)

static const char *pwr_reg_names_none[] = {};
static const char *hpd_reg_names_none[] = {};

static struct hdmi_platform_config hdmi_tx_8660_config;

static const char *hpd_reg_names_8960[] = {"core-vdda", "hdmi-mux"};
static const char *hpd_clk_names_8960[] = {"core", "master_iface", "slave_iface"};

static struct hdmi_platform_config hdmi_tx_8960_config = {
		HDMI_CFG(hpd_reg, 8960),
		HDMI_CFG(hpd_clk, 8960),
};

static const char *pwr_reg_names_8x74[] = {"core-vdda", "core-vcc"};
static const char *hpd_reg_names_8x74[] = {"hpd-gdsc", "hpd-5v"};
static const char *pwr_clk_names_8x74[] = {"extp", "alt_iface"};
static const char *hpd_clk_names_8x74[] = {"iface", "core", "mdp_core"};
static unsigned long hpd_clk_freq_8x74[] = {0, 19200000, 0};

static struct hdmi_platform_config hdmi_tx_8974_config = {
		HDMI_CFG(pwr_reg, 8x74),
		HDMI_CFG(hpd_reg, 8x74),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static const char *hpd_reg_names_8084[] = {"hpd-gdsc", "hpd-5v", "hpd-5v-en"};

static struct hdmi_platform_config hdmi_tx_8084_config = {
		HDMI_CFG(pwr_reg, 8x74),
		HDMI_CFG(hpd_reg, 8084),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static struct hdmi_platform_config hdmi_tx_8994_config = {
		HDMI_CFG(pwr_reg, 8x74),
		HDMI_CFG(hpd_reg, none),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static struct hdmi_platform_config hdmi_tx_8996_config = {
		HDMI_CFG(pwr_reg, none),
		HDMI_CFG(hpd_reg, none),
		HDMI_CFG(pwr_clk, 8x74),
		HDMI_CFG(hpd_clk, 8x74),
		.hpd_freq      = hpd_clk_freq_8x74,
};

static const struct {
	const char *name;
	const bool output;
	const int value;
	const char *label;
} msm_hdmi_gpio_pdata[] = {
	{ "qcom,hdmi-tx-ddc-clk", true, 1, "HDMI_DDC_CLK" },
	{ "qcom,hdmi-tx-ddc-data", true, 1, "HDMI_DDC_DATA" },
	{ "qcom,hdmi-tx-hpd", false, 1, "HDMI_HPD" },
	{ "qcom,hdmi-tx-mux-en", true, 1, "HDMI_MUX_EN" },
	{ "qcom,hdmi-tx-mux-sel", true, 0, "HDMI_MUX_SEL" },
	{ "qcom,hdmi-tx-mux-lpm", true, 1, "HDMI_MUX_LPM" },
};

/*
 * HDMI audio codec callbacks
 */
static int msm_hdmi_audio_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *daifmt,
				    struct hdmi_codec_params *params)
{
	struct hdmi *hdmi = dev_get_drvdata(dev);
	unsigned int chan;
	unsigned int channel_allocation = 0;
	unsigned int rate;
	unsigned int level_shift  = 0; /* 0dB */
	bool down_mix = false;

	DRM_DEV_DEBUG(dev, "%u Hz, %d bit, %d channels\n", params->sample_rate,
		 params->sample_width, params->cea.channels);

	switch (params->cea.channels) {
	case 2:
		/* FR and FL speakers */
		channel_allocation  = 0;
		chan = MSM_HDMI_AUDIO_CHANNEL_2;
		break;
	case 4:
		/* FC, LFE, FR and FL speakers */
		channel_allocation  = 0x3;
		chan = MSM_HDMI_AUDIO_CHANNEL_4;
		break;
	case 6:
		/* RR, RL, FC, LFE, FR and FL speakers */
		channel_allocation  = 0x0B;
		chan = MSM_HDMI_AUDIO_CHANNEL_6;
		break;
	case 8:
		/* FRC, FLC, RR, RL, FC, LFE, FR and FL speakers */
		channel_allocation  = 0x1F;
		chan = MSM_HDMI_AUDIO_CHANNEL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (params->sample_rate) {
	case 32000:
		rate = HDMI_SAMPLE_RATE_32KHZ;
		break;
	case 44100:
		rate = HDMI_SAMPLE_RATE_44_1KHZ;
		break;
	case 48000:
		rate = HDMI_SAMPLE_RATE_48KHZ;
		break;
	case 88200:
		rate = HDMI_SAMPLE_RATE_88_2KHZ;
		break;
	case 96000:
		rate = HDMI_SAMPLE_RATE_96KHZ;
		break;
	case 176400:
		rate = HDMI_SAMPLE_RATE_176_4KHZ;
		break;
	case 192000:
		rate = HDMI_SAMPLE_RATE_192KHZ;
		break;
	default:
		DRM_DEV_ERROR(dev, "rate[%d] not supported!\n",
			params->sample_rate);
		return -EINVAL;
	}

	msm_hdmi_audio_set_sample_rate(hdmi, rate);
	msm_hdmi_audio_info_setup(hdmi, 1, chan, channel_allocation,
			      level_shift, down_mix);

	return 0;
}

static void msm_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct hdmi *hdmi = dev_get_drvdata(dev);

	msm_hdmi_audio_info_setup(hdmi, 0, 0, 0, 0, 0);
}

static const struct hdmi_codec_ops msm_hdmi_audio_codec_ops = {
	.hw_params = msm_hdmi_audio_hw_params,
	.audio_shutdown = msm_hdmi_audio_shutdown,
};

static struct hdmi_codec_pdata codec_data = {
	.ops = &msm_hdmi_audio_codec_ops,
	.max_i2s_channels = 8,
	.i2s = 1,
};

static int msm_hdmi_register_audio_driver(struct hdmi *hdmi, struct device *dev)
{
	hdmi->audio_pdev = platform_device_register_data(dev,
							 HDMI_CODEC_DRV_NAME,
							 PLATFORM_DEVID_AUTO,
							 &codec_data,
							 sizeof(codec_data));
	return PTR_ERR_OR_ZERO(hdmi->audio_pdev);
}

static int msm_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct hdmi_platform_config *hdmi_cfg;
	struct hdmi *hdmi;
	struct device_node *of_node = dev->of_node;
	int i, err;

	hdmi_cfg = (struct hdmi_platform_config *)
			of_device_get_match_data(dev);
	if (!hdmi_cfg) {
		DRM_DEV_ERROR(dev, "unknown hdmi_cfg: %pOFn\n", of_node);
		return -ENXIO;
	}

	hdmi_cfg->mmio_name     = "core_physical";
	hdmi_cfg->qfprom_mmio_name = "qfprom_physical";

	for (i = 0; i < HDMI_MAX_NUM_GPIO; i++) {
		const char *name = msm_hdmi_gpio_pdata[i].name;
		struct gpio_desc *gpiod;

		/*
		 * We are fetching the GPIO lines "as is" since the connector
		 * code is enabling and disabling the lines. Until that point
		 * the power-on default value will be kept.
		 */
		gpiod = devm_gpiod_get_optional(dev, name, GPIOD_ASIS);
		/* This will catch e.g. -PROBE_DEFER */
		if (IS_ERR(gpiod))
			return PTR_ERR(gpiod);
		if (!gpiod) {
			/* Try a second time, stripping down the name */
			char name3[32];

			/*
			 * Try again after stripping out the "qcom,hdmi-tx"
			 * prefix. This is mainly to match "hpd-gpios" used
			 * in the upstream bindings.
			 */
			if (sscanf(name, "qcom,hdmi-tx-%s", name3))
				gpiod = devm_gpiod_get_optional(dev, name3, GPIOD_ASIS);
			if (IS_ERR(gpiod))
				return PTR_ERR(gpiod);
			if (!gpiod)
				DBG("failed to get gpio: %s", name);
		}
		hdmi_cfg->gpios[i].gpiod = gpiod;
		if (gpiod)
			gpiod_set_consumer_name(gpiod, msm_hdmi_gpio_pdata[i].label);
		hdmi_cfg->gpios[i].output = msm_hdmi_gpio_pdata[i].output;
		hdmi_cfg->gpios[i].value = msm_hdmi_gpio_pdata[i].value;
	}

	dev->platform_data = hdmi_cfg;

	hdmi = msm_hdmi_init(to_platform_device(dev));
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);
	priv->hdmi = hdmi;

	err = msm_hdmi_register_audio_driver(hdmi, dev);
	if (err) {
		DRM_ERROR("Failed to attach an audio codec %d\n", err);
		hdmi->audio_pdev = NULL;
	}

	return 0;
}

static void msm_hdmi_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	if (priv->hdmi) {
		if (priv->hdmi->audio_pdev)
			platform_device_unregister(priv->hdmi->audio_pdev);

		msm_hdmi_destroy(priv->hdmi);
		priv->hdmi = NULL;
	}
}

static const struct component_ops msm_hdmi_ops = {
		.bind   = msm_hdmi_bind,
		.unbind = msm_hdmi_unbind,
};

static int msm_hdmi_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &msm_hdmi_ops);
}

static int msm_hdmi_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &msm_hdmi_ops);
	return 0;
}

static const struct of_device_id msm_hdmi_dt_match[] = {
	{ .compatible = "qcom,hdmi-tx-8996", .data = &hdmi_tx_8996_config },
	{ .compatible = "qcom,hdmi-tx-8994", .data = &hdmi_tx_8994_config },
	{ .compatible = "qcom,hdmi-tx-8084", .data = &hdmi_tx_8084_config },
	{ .compatible = "qcom,hdmi-tx-8974", .data = &hdmi_tx_8974_config },
	{ .compatible = "qcom,hdmi-tx-8960", .data = &hdmi_tx_8960_config },
	{ .compatible = "qcom,hdmi-tx-8660", .data = &hdmi_tx_8660_config },
	{}
};

static struct platform_driver msm_hdmi_driver = {
	.probe = msm_hdmi_dev_probe,
	.remove = msm_hdmi_dev_remove,
	.driver = {
		.name = "hdmi_msm",
		.of_match_table = msm_hdmi_dt_match,
	},
};

void __init msm_hdmi_register(void)
{
	msm_hdmi_phy_driver_register();
	platform_driver_register(&msm_hdmi_driver);
}

void __exit msm_hdmi_unregister(void)
{
	platform_driver_unregister(&msm_hdmi_driver);
	msm_hdmi_phy_driver_unregister();
}
