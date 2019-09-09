// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>

#include "msm_kms.h"
#include "hdmi.h"

struct hdmi_connector {
	struct drm_connector base;
	struct hdmi *hdmi;
	struct work_struct hpd_work;
};
#define to_hdmi_connector(x) container_of(x, struct hdmi_connector, base)

static void msm_hdmi_phy_reset(struct hdmi *hdmi)
{
	unsigned int val;

	val = hdmi_read(hdmi, REG_HDMI_PHY_CTRL);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	}

	msleep(100);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	}
}

static int gpio_config(struct hdmi *hdmi, bool on)
{
	struct device *dev = &hdmi->pdev->dev;
	const struct hdmi_platform_config *config = hdmi->config;
	int ret, i;

	if (on) {
		for (i = 0; i < HDMI_MAX_NUM_GPIO; i++) {
			struct hdmi_gpio_data gpio = config->gpios[i];

			if (gpio.num != -1) {
				ret = gpio_request(gpio.num, gpio.label);
				if (ret) {
					DRM_DEV_ERROR(dev,
						"'%s'(%d) gpio_request failed: %d\n",
						gpio.label, gpio.num, ret);
					goto err;
				}

				if (gpio.output) {
					gpio_direction_output(gpio.num,
							      gpio.value);
				} else {
					gpio_direction_input(gpio.num);
					gpio_set_value_cansleep(gpio.num,
								gpio.value);
				}
			}
		}

		DBG("gpio on");
	} else {
		for (i = 0; i < HDMI_MAX_NUM_GPIO; i++) {
			struct hdmi_gpio_data gpio = config->gpios[i];

			if (gpio.num == -1)
				continue;

			if (gpio.output) {
				int value = gpio.value ? 0 : 1;

				gpio_set_value_cansleep(gpio.num, value);
			}

			gpio_free(gpio.num);
		};

		DBG("gpio off");
	}

	return 0;
err:
	while (i--) {
		if (config->gpios[i].num != -1)
			gpio_free(config->gpios[i].num);
	}

	return ret;
}

static void enable_hpd_clocks(struct hdmi *hdmi, bool enable)
{
	const struct hdmi_platform_config *config = hdmi->config;
	struct device *dev = &hdmi->pdev->dev;
	int i, ret;

	if (enable) {
		for (i = 0; i < config->hpd_clk_cnt; i++) {
			if (config->hpd_freq && config->hpd_freq[i]) {
				ret = clk_set_rate(hdmi->hpd_clks[i],
						   config->hpd_freq[i]);
				if (ret)
					dev_warn(dev,
						 "failed to set clk %s (%d)\n",
						 config->hpd_clk_names[i], ret);
			}

			ret = clk_prepare_enable(hdmi->hpd_clks[i]);
			if (ret) {
				DRM_DEV_ERROR(dev,
					"failed to enable hpd clk: %s (%d)\n",
					config->hpd_clk_names[i], ret);
			}
		}
	} else {
		for (i = config->hpd_clk_cnt - 1; i >= 0; i--)
			clk_disable_unprepare(hdmi->hpd_clks[i]);
	}
}

int msm_hdmi_hpd_enable(struct drm_connector *connector)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct hdmi *hdmi = hdmi_connector->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct device *dev = &hdmi->pdev->dev;
	uint32_t hpd_ctrl;
	int i, ret;
	unsigned long flags;

	for (i = 0; i < config->hpd_reg_cnt; i++) {
		ret = regulator_enable(hdmi->hpd_regs[i]);
		if (ret) {
			DRM_DEV_ERROR(dev, "failed to enable hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
			goto fail;
		}
	}

	ret = pinctrl_pm_select_default_state(dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "pinctrl state chg failed: %d\n", ret);
		goto fail;
	}

	ret = gpio_config(hdmi, true);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to configure GPIOs: %d\n", ret);
		goto fail;
	}

	pm_runtime_get_sync(dev);
	enable_hpd_clocks(hdmi, true);

	msm_hdmi_set_mode(hdmi, false);
	msm_hdmi_phy_reset(hdmi);
	msm_hdmi_set_mode(hdmi, true);

	hdmi_write(hdmi, REG_HDMI_USEC_REFTIMER, 0x0001001b);

	/* enable HPD events: */
	hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL,
			HDMI_HPD_INT_CTRL_INT_CONNECT |
			HDMI_HPD_INT_CTRL_INT_EN);

	/* set timeout to 4.1ms (max) for hardware debounce */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	hpd_ctrl = hdmi_read(hdmi, REG_HDMI_HPD_CTRL);
	hpd_ctrl |= HDMI_HPD_CTRL_TIMEOUT(0x1fff);

	/* Toggle HPD circuit to trigger HPD sense */
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL,
			~HDMI_HPD_CTRL_ENABLE & hpd_ctrl);
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL,
			HDMI_HPD_CTRL_ENABLE | hpd_ctrl);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	return 0;

fail:
	return ret;
}

static void hdp_disable(struct hdmi_connector *hdmi_connector)
{
	struct hdmi *hdmi = hdmi_connector->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct device *dev = &hdmi->pdev->dev;
	int i, ret = 0;

	/* Disable HPD interrupt */
	hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL, 0);

	msm_hdmi_set_mode(hdmi, false);

	enable_hpd_clocks(hdmi, false);
	pm_runtime_put_autosuspend(dev);

	ret = gpio_config(hdmi, false);
	if (ret)
		dev_warn(dev, "failed to unconfigure GPIOs: %d\n", ret);

	ret = pinctrl_pm_select_sleep_state(dev);
	if (ret)
		dev_warn(dev, "pinctrl state chg failed: %d\n", ret);

	for (i = 0; i < config->hpd_reg_cnt; i++) {
		ret = regulator_disable(hdmi->hpd_regs[i]);
		if (ret)
			dev_warn(dev, "failed to disable hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
	}
}

static void
msm_hdmi_hotplug_work(struct work_struct *work)
{
	struct hdmi_connector *hdmi_connector =
		container_of(work, struct hdmi_connector, hpd_work);
	struct drm_connector *connector = &hdmi_connector->base;
	drm_helper_hpd_irq_event(connector->dev);
}

void msm_hdmi_connector_irq(struct drm_connector *connector)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct hdmi *hdmi = hdmi_connector->hdmi;
	uint32_t hpd_int_status, hpd_int_ctrl;

	/* Process HPD: */
	hpd_int_status = hdmi_read(hdmi, REG_HDMI_HPD_INT_STATUS);
	hpd_int_ctrl   = hdmi_read(hdmi, REG_HDMI_HPD_INT_CTRL);

	if ((hpd_int_ctrl & HDMI_HPD_INT_CTRL_INT_EN) &&
			(hpd_int_status & HDMI_HPD_INT_STATUS_INT)) {
		bool detected = !!(hpd_int_status & HDMI_HPD_INT_STATUS_CABLE_DETECTED);

		/* ack & disable (temporarily) HPD events: */
		hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL,
			HDMI_HPD_INT_CTRL_INT_ACK);

		DBG("status=%04x, ctrl=%04x", hpd_int_status, hpd_int_ctrl);

		/* detect disconnect if we are connected or visa versa: */
		hpd_int_ctrl = HDMI_HPD_INT_CTRL_INT_EN;
		if (!detected)
			hpd_int_ctrl |= HDMI_HPD_INT_CTRL_INT_CONNECT;
		hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL, hpd_int_ctrl);

		queue_work(hdmi->workq, &hdmi_connector->hpd_work);
	}
}

static enum drm_connector_status detect_reg(struct hdmi *hdmi)
{
	uint32_t hpd_int_status;

	pm_runtime_get_sync(&hdmi->pdev->dev);
	enable_hpd_clocks(hdmi, true);

	hpd_int_status = hdmi_read(hdmi, REG_HDMI_HPD_INT_STATUS);

	enable_hpd_clocks(hdmi, false);
	pm_runtime_put_autosuspend(&hdmi->pdev->dev);

	return (hpd_int_status & HDMI_HPD_INT_STATUS_CABLE_DETECTED) ?
			connector_status_connected : connector_status_disconnected;
}

#define HPD_GPIO_INDEX	2
static enum drm_connector_status detect_gpio(struct hdmi *hdmi)
{
	const struct hdmi_platform_config *config = hdmi->config;
	struct hdmi_gpio_data hpd_gpio = config->gpios[HPD_GPIO_INDEX];

	return gpio_get_value(hpd_gpio.num) ?
			connector_status_connected :
			connector_status_disconnected;
}

static enum drm_connector_status hdmi_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct hdmi *hdmi = hdmi_connector->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct hdmi_gpio_data hpd_gpio = config->gpios[HPD_GPIO_INDEX];
	enum drm_connector_status stat_gpio, stat_reg;
	int retry = 20;

	/*
	 * some platforms may not have hpd gpio. Rely only on the status
	 * provided by REG_HDMI_HPD_INT_STATUS in this case.
	 */
	if (hpd_gpio.num == -1)
		return detect_reg(hdmi);

	do {
		stat_gpio = detect_gpio(hdmi);
		stat_reg  = detect_reg(hdmi);

		if (stat_gpio == stat_reg)
			break;

		mdelay(10);
	} while (--retry);

	/* the status we get from reading gpio seems to be more reliable,
	 * so trust that one the most if we didn't manage to get hdmi and
	 * gpio status to agree:
	 */
	if (stat_gpio != stat_reg) {
		DBG("HDMI_HPD_INT_STATUS tells us: %d", stat_reg);
		DBG("hpd gpio tells us: %d", stat_gpio);
	}

	return stat_gpio;
}

static void hdmi_connector_destroy(struct drm_connector *connector)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);

	hdp_disable(hdmi_connector);

	drm_connector_cleanup(connector);

	kfree(hdmi_connector);
}

static int msm_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct hdmi *hdmi = hdmi_connector->hdmi;
	struct edid *edid;
	uint32_t hdmi_ctrl;
	int ret = 0;

	hdmi_ctrl = hdmi_read(hdmi, REG_HDMI_CTRL);
	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl | HDMI_CTRL_ENABLE);

	edid = drm_get_edid(connector, hdmi->i2c);

	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl);

	hdmi->hdmi_mode = drm_detect_hdmi_monitor(edid);
	drm_connector_update_edid_property(connector, edid);

	if (edid) {
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static int msm_hdmi_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct hdmi *hdmi = hdmi_connector->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual, requested;

	requested = 1000 * mode->clock;
	actual = kms->funcs->round_pixclk(kms,
			requested, hdmi_connector->hdmi->encoder);

	/* for mdp5/apq8074, we manage our own pixel clk (as opposed to
	 * mdp4/dtv stuff where pixel clk is assigned to mdp/encoder
	 * instead):
	 */
	if (config->pwr_clk_cnt > 0)
		actual = clk_round_rate(hdmi->pwr_clks[0], actual);

	DBG("requested=%ld, actual=%ld", requested, actual);

	if (actual != requested)
		return MODE_CLOCK_RANGE;

	return 0;
}

static const struct drm_connector_funcs hdmi_connector_funcs = {
	.detect = hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs msm_hdmi_connector_helper_funcs = {
	.get_modes = msm_hdmi_connector_get_modes,
	.mode_valid = msm_hdmi_connector_mode_valid,
};

/* initialize connector */
struct drm_connector *msm_hdmi_connector_init(struct hdmi *hdmi)
{
	struct drm_connector *connector = NULL;
	struct hdmi_connector *hdmi_connector;

	hdmi_connector = kzalloc(sizeof(*hdmi_connector), GFP_KERNEL);
	if (!hdmi_connector)
		return ERR_PTR(-ENOMEM);

	hdmi_connector->hdmi = hdmi;
	INIT_WORK(&hdmi_connector->hpd_work, msm_hdmi_hotplug_work);

	connector = &hdmi_connector->base;

	drm_connector_init(hdmi->dev, connector, &hdmi_connector_funcs,
			DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &msm_hdmi_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_attach_encoder(connector, hdmi->encoder);

	return connector;
}
