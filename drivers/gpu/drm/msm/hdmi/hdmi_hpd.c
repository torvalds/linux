// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>

#include "msm_kms.h"
#include "hdmi.h"

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

int msm_hdmi_hpd_enable(struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
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

	if (hdmi->hpd_gpiod)
		gpiod_set_value_cansleep(hdmi->hpd_gpiod, 1);

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

void msm_hdmi_hpd_disable(struct hdmi_bridge *hdmi_bridge)
{
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct device *dev = &hdmi->pdev->dev;
	int i, ret = 0;

	/* Disable HPD interrupt */
	hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL, 0);

	msm_hdmi_set_mode(hdmi, false);

	enable_hpd_clocks(hdmi, false);
	pm_runtime_put_autosuspend(dev);

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

void msm_hdmi_hpd_irq(struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
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

		queue_work(hdmi->workq, &hdmi_bridge->hpd_work);
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
	return gpiod_get_value(hdmi->hpd_gpiod) ?
			connector_status_connected :
			connector_status_disconnected;
}

enum drm_connector_status msm_hdmi_bridge_detect(
		struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	enum drm_connector_status stat_gpio, stat_reg;
	int retry = 20;

	/*
	 * some platforms may not have hpd gpio. Rely only on the status
	 * provided by REG_HDMI_HPD_INT_STATUS in this case.
	 */
	if (!hdmi->hpd_gpiod)
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
