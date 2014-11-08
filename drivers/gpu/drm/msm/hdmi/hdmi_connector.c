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

#include <linux/gpio.h>

#include "msm_kms.h"
#include "hdmi.h"

struct hdmi_connector {
	struct drm_connector base;
	struct hdmi *hdmi;
	struct work_struct hpd_work;
};
#define to_hdmi_connector(x) container_of(x, struct hdmi_connector, base)

static int gpio_config(struct hdmi *hdmi, bool on)
{
	struct drm_device *dev = hdmi->dev;
	const struct hdmi_platform_config *config = hdmi->config;
	int ret;

	if (on) {
		ret = gpio_request(config->ddc_clk_gpio, "HDMI_DDC_CLK");
		if (ret) {
			dev_err(dev->dev, "'%s'(%d) gpio_request failed: %d\n",
				"HDMI_DDC_CLK", config->ddc_clk_gpio, ret);
			goto error1;
		}
		gpio_set_value_cansleep(config->ddc_clk_gpio, 1);

		ret = gpio_request(config->ddc_data_gpio, "HDMI_DDC_DATA");
		if (ret) {
			dev_err(dev->dev, "'%s'(%d) gpio_request failed: %d\n",
				"HDMI_DDC_DATA", config->ddc_data_gpio, ret);
			goto error2;
		}
		gpio_set_value_cansleep(config->ddc_data_gpio, 1);

		ret = gpio_request(config->hpd_gpio, "HDMI_HPD");
		if (ret) {
			dev_err(dev->dev, "'%s'(%d) gpio_request failed: %d\n",
				"HDMI_HPD", config->hpd_gpio, ret);
			goto error3;
		}
		gpio_direction_input(config->hpd_gpio);
		gpio_set_value_cansleep(config->hpd_gpio, 1);

		if (config->mux_en_gpio != -1) {
			ret = gpio_request(config->mux_en_gpio, "HDMI_MUX_EN");
			if (ret) {
				dev_err(dev->dev, "'%s'(%d) gpio_request failed: %d\n",
					"HDMI_MUX_EN", config->mux_en_gpio, ret);
				goto error4;
			}
			gpio_set_value_cansleep(config->mux_en_gpio, 1);
		}

		if (config->mux_sel_gpio != -1) {
			ret = gpio_request(config->mux_sel_gpio, "HDMI_MUX_SEL");
			if (ret) {
				dev_err(dev->dev, "'%s'(%d) gpio_request failed: %d\n",
					"HDMI_MUX_SEL", config->mux_sel_gpio, ret);
				goto error5;
			}
			gpio_set_value_cansleep(config->mux_sel_gpio, 0);
		}

		if (config->mux_lpm_gpio != -1) {
			ret = gpio_request(config->mux_lpm_gpio,
					"HDMI_MUX_LPM");
			if (ret) {
				dev_err(dev->dev,
					"'%s'(%d) gpio_request failed: %d\n",
					"HDMI_MUX_LPM",
					config->mux_lpm_gpio, ret);
				goto error6;
			}
			gpio_set_value_cansleep(config->mux_lpm_gpio, 1);
		}
		DBG("gpio on");
	} else {
		gpio_free(config->ddc_clk_gpio);
		gpio_free(config->ddc_data_gpio);
		gpio_free(config->hpd_gpio);

		if (config->mux_en_gpio != -1) {
			gpio_set_value_cansleep(config->mux_en_gpio, 0);
			gpio_free(config->mux_en_gpio);
		}

		if (config->mux_sel_gpio != -1) {
			gpio_set_value_cansleep(config->mux_sel_gpio, 1);
			gpio_free(config->mux_sel_gpio);
		}

		if (config->mux_lpm_gpio != -1) {
			gpio_set_value_cansleep(config->mux_lpm_gpio, 0);
			gpio_free(config->mux_lpm_gpio);
		}
		DBG("gpio off");
	}

	return 0;

error6:
	if (config->mux_sel_gpio != -1)
		gpio_free(config->mux_sel_gpio);
error5:
	if (config->mux_en_gpio != -1)
		gpio_free(config->mux_en_gpio);
error4:
	gpio_free(config->hpd_gpio);
error3:
	gpio_free(config->ddc_data_gpio);
error2:
	gpio_free(config->ddc_clk_gpio);
error1:
	return ret;
}

static int hpd_enable(struct hdmi_connector *hdmi_connector)
{
	struct hdmi *hdmi = hdmi_connector->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct drm_device *dev = hdmi_connector->base.dev;
	struct hdmi_phy *phy = hdmi->phy;
	uint32_t hpd_ctrl;
	int i, ret;

	ret = gpio_config(hdmi, true);
	if (ret) {
		dev_err(dev->dev, "failed to configure GPIOs: %d\n", ret);
		goto fail;
	}

	for (i = 0; i < config->hpd_clk_cnt; i++) {
		if (config->hpd_freq && config->hpd_freq[i]) {
			ret = clk_set_rate(hdmi->hpd_clks[i],
					config->hpd_freq[i]);
			if (ret)
				dev_warn(dev->dev, "failed to set clk %s (%d)\n",
						config->hpd_clk_names[i], ret);
		}

		ret = clk_prepare_enable(hdmi->hpd_clks[i]);
		if (ret) {
			dev_err(dev->dev, "failed to enable hpd clk: %s (%d)\n",
					config->hpd_clk_names[i], ret);
			goto fail;
		}
	}

	for (i = 0; i < config->hpd_reg_cnt; i++) {
		ret = regulator_enable(hdmi->hpd_regs[i]);
		if (ret) {
			dev_err(dev->dev, "failed to enable hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
			goto fail;
		}
	}

	hdmi_set_mode(hdmi, false);
	phy->funcs->reset(phy);
	hdmi_set_mode(hdmi, true);

	hdmi_write(hdmi, REG_HDMI_USEC_REFTIMER, 0x0001001b);

	/* enable HPD events: */
	hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL,
			HDMI_HPD_INT_CTRL_INT_CONNECT |
			HDMI_HPD_INT_CTRL_INT_EN);

	/* set timeout to 4.1ms (max) for hardware debounce */
	hpd_ctrl = hdmi_read(hdmi, REG_HDMI_HPD_CTRL);
	hpd_ctrl |= HDMI_HPD_CTRL_TIMEOUT(0x1fff);

	/* Toggle HPD circuit to trigger HPD sense */
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL,
			~HDMI_HPD_CTRL_ENABLE & hpd_ctrl);
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL,
			HDMI_HPD_CTRL_ENABLE | hpd_ctrl);

	return 0;

fail:
	return ret;
}

static int hdp_disable(struct hdmi_connector *hdmi_connector)
{
	struct hdmi *hdmi = hdmi_connector->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct drm_device *dev = hdmi_connector->base.dev;
	int i, ret = 0;

	/* Disable HPD interrupt */
	hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL, 0);

	hdmi_set_mode(hdmi, false);

	for (i = 0; i < config->hpd_reg_cnt; i++) {
		ret = regulator_disable(hdmi->hpd_regs[i]);
		if (ret) {
			dev_err(dev->dev, "failed to disable hpd regulator: %s (%d)\n",
					config->hpd_reg_names[i], ret);
			goto fail;
		}
	}

	for (i = 0; i < config->hpd_clk_cnt; i++)
		clk_disable_unprepare(hdmi->hpd_clks[i]);

	ret = gpio_config(hdmi, false);
	if (ret) {
		dev_err(dev->dev, "failed to unconfigure GPIOs: %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	return ret;
}

static void
hotplug_work(struct work_struct *work)
{
	struct hdmi_connector *hdmi_connector =
		container_of(work, struct hdmi_connector, hpd_work);
	struct drm_connector *connector = &hdmi_connector->base;
	drm_helper_hpd_irq_event(connector->dev);
}

void hdmi_connector_irq(struct drm_connector *connector)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct hdmi *hdmi = hdmi_connector->hdmi;
	uint32_t hpd_int_status, hpd_int_ctrl;

	/* Process HPD: */
	hpd_int_status = hdmi_read(hdmi, REG_HDMI_HPD_INT_STATUS);
	hpd_int_ctrl   = hdmi_read(hdmi, REG_HDMI_HPD_INT_CTRL);

	if ((hpd_int_ctrl & HDMI_HPD_INT_CTRL_INT_EN) &&
			(hpd_int_status & HDMI_HPD_INT_STATUS_INT)) {
		bool detected = !!(hpd_int_status & HDMI_HPD_INT_STATUS_CABLE_DETECTED);

		DBG("status=%04x, ctrl=%04x", hpd_int_status, hpd_int_ctrl);

		/* ack the irq: */
		hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL,
				hpd_int_ctrl | HDMI_HPD_INT_CTRL_INT_ACK);

		/* detect disconnect if we are connected or visa versa: */
		hpd_int_ctrl = HDMI_HPD_INT_CTRL_INT_EN;
		if (!detected)
			hpd_int_ctrl |= HDMI_HPD_INT_CTRL_INT_CONNECT;
		hdmi_write(hdmi, REG_HDMI_HPD_INT_CTRL, hpd_int_ctrl);

		queue_work(priv->wq, &hdmi_connector->hpd_work);
	}
}

static enum drm_connector_status detect_reg(struct hdmi *hdmi)
{
	uint32_t hpd_int_status = hdmi_read(hdmi, REG_HDMI_HPD_INT_STATUS);
	return (hpd_int_status & HDMI_HPD_INT_STATUS_CABLE_DETECTED) ?
			connector_status_connected : connector_status_disconnected;
}

static enum drm_connector_status detect_gpio(struct hdmi *hdmi)
{
	const struct hdmi_platform_config *config = hdmi->config;
	return gpio_get_value(config->hpd_gpio) ?
			connector_status_connected :
			connector_status_disconnected;
}

static enum drm_connector_status hdmi_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	struct hdmi *hdmi = hdmi_connector->hdmi;
	enum drm_connector_status stat_gpio, stat_reg;
	int retry = 20;

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

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	hdmi_unreference(hdmi_connector->hdmi);

	kfree(hdmi_connector);
}

static int hdmi_connector_get_modes(struct drm_connector *connector)
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

	drm_mode_connector_update_edid_property(connector, edid);

	if (edid) {
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static int hdmi_connector_mode_valid(struct drm_connector *connector,
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

static struct drm_encoder *
hdmi_connector_best_encoder(struct drm_connector *connector)
{
	struct hdmi_connector *hdmi_connector = to_hdmi_connector(connector);
	return hdmi_connector->hdmi->encoder;
}

static const struct drm_connector_funcs hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs hdmi_connector_helper_funcs = {
	.get_modes = hdmi_connector_get_modes,
	.mode_valid = hdmi_connector_mode_valid,
	.best_encoder = hdmi_connector_best_encoder,
};

/* initialize connector */
struct drm_connector *hdmi_connector_init(struct hdmi *hdmi)
{
	struct drm_connector *connector = NULL;
	struct hdmi_connector *hdmi_connector;
	int ret;

	hdmi_connector = kzalloc(sizeof(*hdmi_connector), GFP_KERNEL);
	if (!hdmi_connector) {
		ret = -ENOMEM;
		goto fail;
	}

	hdmi_connector->hdmi = hdmi_reference(hdmi);
	INIT_WORK(&hdmi_connector->hpd_work, hotplug_work);

	connector = &hdmi_connector->base;

	drm_connector_init(hdmi->dev, connector, &hdmi_connector_funcs,
			DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &hdmi_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	drm_connector_register(connector);

	ret = hpd_enable(hdmi_connector);
	if (ret) {
		dev_err(hdmi->dev->dev, "failed to enable HPD: %d\n", ret);
		goto fail;
	}

	drm_mode_connector_attach_encoder(connector, hdmi->encoder);

	return connector;

fail:
	if (connector)
		hdmi_connector_destroy(connector);

	return ERR_PTR(ret);
}
