/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/crc16.h>
#include <linux/extcon.h>
#include <linux/firmware.h>
#include <linux/hdmi-notifier.h>
#include <linux/iopoll.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <linux/nvmem-consumer.h>
#include <linux/phy/phy.h>
#include <uapi/linux/videodev2.h>

#include <sound/hdmi-codec.h>

#include "cdn-dp-core.h"
#include "cdn-dp-reg.h"
#include "rockchip_drm_vop.h"

#define HDCP_TOGGLE_INTERVAL_US		10000
#define HDCP_RETRY_INTERVAL_US		20000
#define HDCP_EVENT_TIMEOUT_US		3000000
#define HDCP_AUTHENTICATE_DELAY_MS	400

#define HDCP_KEY_DATA_START_TRANSFER	0
#define HDCP_KEY_DATA_START_DECRYPT	1

#define RK_SIP_HDCP_CONTROL		0x82000009
#define RK_SIP_HDCP_KEY_DATA64		0xC200000A

#define connector_to_dp(c) \
		container_of(c, struct cdn_dp_device, connector)

#define encoder_to_dp(c) \
		container_of(c, struct cdn_dp_device, encoder)

#define GRF_SOC_CON9		0x6224
#define DP_SEL_VOP_LIT		BIT(12)
#define GRF_SOC_CON26		0x6268
#define DPTX_HPD_SEL		(3 << 12)
#define DPTX_HPD_DEL		(2 << 12)
#define DPTX_HPD_SEL_MASK	(3 << 28)

#define CDN_FW_TIMEOUT_MS	(64 * 1000)
#define CDN_DPCD_TIMEOUT_MS	5000
#define CDN_DP_FIRMWARE		"rockchip/dptx.bin"

struct cdn_dp_data {
	u8 max_phy;
};

struct cdn_dp_data rk3399_cdn_dp = {
	.max_phy = 2,
};

static const struct of_device_id cdn_dp_dt_ids[] = {
	{ .compatible = "rockchip,rk3399-cdn-dp",
		.data = (void *)&rk3399_cdn_dp },
	{}
};

MODULE_DEVICE_TABLE(of, cdn_dp_dt_ids);

static int cdn_dp_grf_write(struct cdn_dp_device *dp,
			    unsigned int reg, unsigned int val)
{
	int ret;

	ret = clk_prepare_enable(dp->grf_clk);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "Failed to prepare_enable grf clock\n");
		return ret;
	}

	ret = regmap_write(dp->grf, reg, val);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	clk_disable_unprepare(dp->grf_clk);

	return 0;
}

static int cdn_dp_clk_enable(struct cdn_dp_device *dp)
{
	int ret;
	unsigned long rate;

	ret = clk_prepare_enable(dp->pclk);
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "cannot enable dp pclk %d\n", ret);
		goto err_pclk;
	}

	ret = clk_prepare_enable(dp->core_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "cannot enable core_clk %d\n", ret);
		goto err_core_clk;
	}

	ret = pm_runtime_get_sync(dp->dev);
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "cannot get pm runtime %d\n", ret);
		goto err_pm_runtime_get;
	}

	reset_control_assert(dp->core_rst);
	reset_control_assert(dp->dptx_rst);
	reset_control_assert(dp->apb_rst);
	reset_control_deassert(dp->core_rst);
	reset_control_deassert(dp->dptx_rst);
	reset_control_deassert(dp->apb_rst);

	rate = clk_get_rate(dp->core_clk);
	if (!rate) {
		DRM_DEV_ERROR(dp->dev, "get clk rate failed\n");
		ret = -EINVAL;
		goto err_set_rate;
	}

	cdn_dp_set_fw_clk(dp, rate);
	cdn_dp_clock_reset(dp);

	return 0;

err_set_rate:
	pm_runtime_put(dp->dev);
err_pm_runtime_get:
	clk_disable_unprepare(dp->core_clk);
err_core_clk:
	clk_disable_unprepare(dp->pclk);
err_pclk:
	return ret;
}

static void cdn_dp_clk_disable(struct cdn_dp_device *dp)
{
	pm_runtime_put_sync(dp->dev);
	clk_disable_unprepare(dp->pclk);
	clk_disable_unprepare(dp->core_clk);
}

static int cdn_dp_get_port_lanes(struct cdn_dp_port *port)
{
	struct extcon_dev *edev = port->extcon;
	union extcon_property_value property;
	int dptx;
	u8 lanes;

	dptx = extcon_get_state(edev, EXTCON_DISP_DP);
	if (dptx > 0) {
		extcon_get_property(edev, EXTCON_DISP_DP,
				    EXTCON_PROP_USB_SS, &property);
		if (property.intval)
			lanes = 2;
		else
			lanes = 4;
	} else {
		lanes = 0;
	}

	return lanes;
}

static int cdn_dp_get_sink_count(struct cdn_dp_device *dp, u8 *sink_count)
{
	int ret;
	u8 value;

	*sink_count = 0;
	ret = drm_dp_dpcd_read(&dp->aux, DP_SINK_COUNT, &value, 1);
	if (ret < 0)
		return ret;

	*sink_count = DP_GET_SINK_COUNT(value);
	return 0;
}

static int cdn_dp_start_hdcp1x_auth(struct cdn_dp_device *dp)
{
	int ret;
	struct arm_smccc_res res;
	uint64_t *buf;

	if (!dp->active) {
		DRM_DEV_ERROR(dp->dev, "firmware is not active\n");
		return 0;
	}

	arm_smccc_smc(RK_SIP_HDCP_CONTROL, HDCP_KEY_DATA_START_TRANSFER,
		      0, 0, 0, 0, 0, 0, &res);
	if (res.a0) {
		DRM_DEV_ERROR(dp->dev, "start hdcp transfer failed: %#lx\n",
			      res.a0);
		return -EIO;
	}

	BUILD_BUG_ON(sizeof(dp->key) % 6);
	for (buf = (uint64_t *)&dp->key;
	     !res.a0 && (u8 *)buf - (u8 *)&dp->key < sizeof(dp->key);
	     buf += 6)
		arm_smccc_smc(RK_SIP_HDCP_KEY_DATA64, buf[0], buf[1],
			      buf[2], buf[3], buf[4], buf[5], 0, &res);

	if (res.a0) {
		DRM_DEV_ERROR(dp->dev, "send hdcp keys failed: %#lx\n", res.a0);
		return -EIO;
	}
	arm_smccc_smc(RK_SIP_HDCP_CONTROL, HDCP_KEY_DATA_START_DECRYPT,
		      0, 0, 0, 0, 0, 0, &res);

	/*
	 * First thing's first, disable HDCP. The hardware likes to start with
	 * a fresh slate, so this ensures that re-enabling on unplug/replug
	 * works.
	 */
	ret = cdn_dp_hdcp_tx_configuration(dp, HDCP_TX_CONFIGURATION_HDCP_V1,
					   false);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "HDCP pre-disable failed %d\n", ret);
		return ret;
	}
	usleep_range(HDCP_TOGGLE_INTERVAL_US, HDCP_TOGGLE_INTERVAL_US * 2);

	ret = cdn_dp_hdcp_tx_configuration(dp, HDCP_TX_CONFIGURATION_HDCP_V1,
					   true);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "start hdcp authentication failed: %d\n",
			      ret);
		return ret;
	}

	schedule_delayed_work(&dp->hdcp_event_work,
			      msecs_to_jiffies(HDCP_AUTHENTICATE_DELAY_MS));

	return ret;
}

static int cdn_dp_stop_hdcp1x_auth(struct cdn_dp_device *dp)
{
	int ret;

	if (!dp->active)
		return 0;

	ret = cdn_dp_hdcp_tx_configuration(dp, HDCP_TX_CONFIGURATION_HDCP_V1,
					   false);
	if (!ret)
		DRM_DEV_INFO(dp->dev, "HDCP has been disabled\n");
	else
		DRM_DEV_ERROR(dp->dev, "Disable HDCP failed %d\n", ret);

	dp->hdcp_enabled = false;

	/* If hdcp is still desired, we're disabling because of error or
	 * hotplug. In both of these cases, we'll want to change the content
	 * protection property from ENABLED to DESIRED. Schedule the property
	 * worker for this.
	 */
	if (dp->hdcp_desired)
		schedule_work(&dp->hdcp_prop_work);

	return ret;
}

static struct cdn_dp_port *cdn_dp_connected_port(struct cdn_dp_device *dp)
{
	struct cdn_dp_port *port;
	int i, lanes;

	for (i = 0; i < dp->ports; i++) {
		port = dp->port[i];
		lanes = cdn_dp_get_port_lanes(port);
		if (lanes)
			return port;
	}
	return NULL;
}

static bool cdn_dp_check_sink_connection(struct cdn_dp_device *dp)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(CDN_DPCD_TIMEOUT_MS);
	struct cdn_dp_port *port;
	u8 sink_count = 0;

	if (dp->active_port < 0 || dp->active_port >= dp->ports) {
		DRM_DEV_ERROR(dp->dev, "active_port is wrong!\n");
		return false;
	}

	port = dp->port[dp->active_port];

	/*
	 * Attempt to read sink count, retry in case the sink may not be ready.
	 *
	 * Sinks are *supposed* to come up within 1ms from an off state, but
	 * some docks need more time to power up.
	 */
	while (time_before(jiffies, timeout)) {
		if (!extcon_get_state(port->extcon, EXTCON_DISP_DP))
			return false;

		if (!cdn_dp_get_sink_count(dp, &sink_count))
			return sink_count ? true : false;

		usleep_range(5000, 10000);
	}

	DRM_DEV_ERROR(dp->dev, "Get sink capability timed out\n");
	return false;
}

static enum drm_connector_status
cdn_dp_connector_detect(struct drm_connector *connector, bool force)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	enum drm_connector_status status = connector_status_disconnected;

	mutex_lock(&dp->lock);
	if (dp->connected)
		status = connector_status_connected;
	mutex_unlock(&dp->lock);

	return status;
}

static void cdn_dp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int cdn_dp_connector_atomic_get_property(
					struct drm_connector *connector,
					const struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	int i;

	if (property == connector->content_protection_property) {
		for (i = 0; i < connector->properties.count; i++) {
			if (connector->properties.properties[i] == property) {
				*val = connector->properties.values[i];
				return 0;
			}
		}
	}

	return -EINVAL;
}

static const struct drm_connector_funcs cdn_dp_atomic_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = cdn_dp_connector_detect,
	.destroy = cdn_dp_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.reset = drm_atomic_helper_connector_reset,
	.set_property = drm_atomic_helper_connector_set_property,
	.atomic_get_property = cdn_dp_connector_atomic_get_property,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

bool is_connector_cdn_dp(struct drm_connector *connector) {
	return connector->funcs == &cdn_dp_atomic_connector_funcs;
}

static int cdn_dp_connector_get_modes(struct drm_connector *connector)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	struct edid *edid;
	int ret = 0;

	mutex_lock(&dp->lock);
	edid = dp->edid;
	if (edid) {
		DRM_DEV_DEBUG_KMS(dp->dev, "got edid: width[%d] x height[%d]\n",
				  edid->width_cm, edid->height_cm);

		dp->sink_has_audio = drm_detect_monitor_audio(edid);
		ret = drm_add_edid_modes(connector, edid);
		if (ret) {
			drm_mode_connector_update_edid_property(connector,
								edid);
			drm_edid_to_eld(connector, edid);
		}
	}
	mutex_unlock(&dp->lock);

	return ret;
}

static struct drm_encoder *
cdn_dp_connector_best_encoder(struct drm_connector *connector)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);

	return &dp->encoder;
}

static int cdn_dp_connector_mode_valid(struct drm_connector *connector,
				       struct drm_display_mode *mode)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	struct drm_display_info *display_info = &dp->connector.display_info;
	u32 requested, actual, rate, sink_max, source_max = 0;
	struct drm_encoder *encoder = connector->encoder;
	enum drm_mode_status status = MODE_OK;
	struct drm_device *dev = connector->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	u8 lanes, bpc;

	/* If DP is disconnected, every mode is invalid */
	if (!dp->connected)
		return MODE_BAD;

	switch (display_info->bpc) {
	case 10:
		bpc = 10;
		break;
	case 6:
		bpc = 6;
		break;
	default:
		bpc = 8;
		break;
	}

	requested = mode->clock * bpc * 3 / 1000;

	source_max = dp->lanes;
	sink_max = drm_dp_max_lane_count(dp->dpcd);
	lanes = min(source_max, sink_max);

	source_max = drm_dp_bw_code_to_link_rate(CDN_DP_MAX_LINK_RATE);
	sink_max = drm_dp_max_link_rate(dp->dpcd);
	rate = min(source_max, sink_max);

	actual = rate * lanes / 100;

	/* efficiency is about 0.8 */
	actual = actual * 8 / 10;

	if (requested > actual) {
		DRM_DEV_DEBUG_KMS(dp->dev,
				  "requested=%d, actual=%d, clock=%d\n",
				  requested, actual, mode->clock);
		return MODE_CLOCK_HIGH;
	}

	if (!encoder) {
		const struct drm_connector_helper_funcs *funcs;

		funcs = connector->helper_private;
		if (funcs->atomic_best_encoder)
			encoder = funcs->atomic_best_encoder(connector,
							     connector->state);
		else
			encoder = funcs->best_encoder(connector);
	}

	if (!encoder || !encoder->possible_crtcs)
		return MODE_BAD;
	/*
	 * ensure all drm display mode can work, if someone want support more
	 * resolutions, please limit the possible_crtc, only connect to
	 * needed crtc.
	 */
	drm_for_each_crtc(crtc, connector->dev) {
		int pipe = drm_crtc_index(crtc);
		const struct rockchip_crtc_funcs *funcs =
						priv->crtc_funcs[pipe];

		if (!(encoder->possible_crtcs & drm_crtc_mask(crtc)))
			continue;
		if (!funcs || !funcs->mode_valid)
			continue;

		status = funcs->mode_valid(crtc, mode,
					   DRM_MODE_CONNECTOR_HDMIA);
		if (status != MODE_OK)
			return status;
	}

	return MODE_OK;
}

static struct drm_connector_helper_funcs cdn_dp_connector_helper_funcs = {
	.get_modes = cdn_dp_connector_get_modes,
	.best_encoder = cdn_dp_connector_best_encoder,
	.mode_valid = cdn_dp_connector_mode_valid,
};

static int cdn_dp_firmware_init(struct cdn_dp_device *dp)
{
	int ret;
	const u32 *iram_data, *dram_data;
	const struct firmware *fw = dp->fw;
	const struct cdn_firmware_header *hdr;

	hdr = (struct cdn_firmware_header *)fw->data;
	if (fw->size != le32_to_cpu(hdr->size_bytes)) {
		DRM_DEV_ERROR(dp->dev, "firmware is invalid\n");
		return -EINVAL;
	}

	iram_data = (const u32 *)(fw->data + hdr->header_size);
	dram_data = (const u32 *)(fw->data + hdr->header_size + hdr->iram_size);

	ret = cdn_dp_load_firmware(dp, iram_data, hdr->iram_size,
				   dram_data, hdr->dram_size);
	if (ret)
		return ret;

	ret = cdn_dp_set_firmware_active(dp, true);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "active ucpu failed: %d\n", ret);
		return ret;
	}

	return cdn_dp_event_config(dp);
}

static int cdn_dp_get_sink_capability(struct cdn_dp_device *dp)
{
	int ret;

	if (!cdn_dp_check_sink_connection(dp))
		return -ENODEV;

	ret = drm_dp_dpcd_read(&dp->aux, DP_DPCD_REV, dp->dpcd,
			       sizeof(dp->dpcd));
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "Failed to get caps %d\n", ret);
		return ret;
	}

	kfree(dp->edid);
	dp->edid = drm_do_get_edid(&dp->connector,
				   cdn_dp_get_edid_block, dp);
	return 0;
}

static int cdn_dp_enable_phy(struct cdn_dp_device *dp, struct cdn_dp_port *port)
{
	union extcon_property_value property;
	int ret;

	if (!port->phy_enabled) {
		ret = phy_power_on(port->phy);
		if (ret) {
			DRM_DEV_ERROR(dp->dev, "phy power on failed: %d\n",
				      ret);
			goto err_phy;
		}
		port->phy_enabled = true;
	}

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON26,
			       DPTX_HPD_SEL_MASK | DPTX_HPD_SEL);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "Failed to write HPD_SEL %d\n", ret);
		goto err_power_on;
	}

	ret = cdn_dp_get_hpd_status(dp);
	if (ret <= 0) {
		if (!ret)
			DRM_DEV_ERROR(dp->dev, "hpd does not exist\n");
		goto err_power_on;
	}

	ret = extcon_get_property(port->extcon, EXTCON_DISP_DP,
				  EXTCON_PROP_USB_TYPEC_POLARITY, &property);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "get property failed\n");
		goto err_power_on;
	}

	port->lanes = cdn_dp_get_port_lanes(port);
	ret = cdn_dp_set_host_cap(dp, port->lanes, property.intval);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "set host capabilities failed: %d\n",
			      ret);
		goto err_power_on;
	}

	dp->active_port = port->id;
	return 0;

err_power_on:
	if (phy_power_off(port->phy))
		DRM_DEV_ERROR(dp->dev, "phy power off failed: %d", ret);
	else
		port->phy_enabled = false;

err_phy:
	cdn_dp_grf_write(dp, GRF_SOC_CON26,
			 DPTX_HPD_SEL_MASK | DPTX_HPD_DEL);
	return ret;
}

static int cdn_dp_disable_phy(struct cdn_dp_device *dp,
			      struct cdn_dp_port *port)
{
	int ret;

	if (port->phy_enabled) {
		ret = phy_power_off(port->phy);
		if (ret) {
			DRM_DEV_ERROR(dp->dev, "phy power off failed: %d", ret);
			return ret;
		}
	}

	port->phy_enabled = false;
	port->lanes = 0;
	dp->active_port = -1;
	return 0;
}

static int cdn_dp_disable(struct cdn_dp_device *dp)
{
	int ret, i;

	WARN_ON(!mutex_is_locked(&dp->lock));

	if (!dp->active)
		return 0;

	for (i = 0; i < dp->ports; i++)
		cdn_dp_disable_phy(dp, dp->port[i]);

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON26,
			       DPTX_HPD_SEL_MASK | DPTX_HPD_DEL);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "Failed to clear hpd sel %d\n",
			      ret);
		return ret;
	}

	cdn_dp_set_firmware_active(dp, false);
	cdn_dp_clk_disable(dp);
	dp->active = false;
	dp->link.rate = 0;
	dp->link.num_lanes = 0;
	if (!dp->connected) {
		kfree(dp->edid);
		dp->edid = NULL;
	}

	return 0;
}

static int cdn_dp_enable(struct cdn_dp_device *dp)
{
	int ret, i, lanes;
	struct cdn_dp_port *port;

	port = cdn_dp_connected_port(dp);
	if (!port) {
		DRM_DEV_ERROR(dp->dev,
			      "Can't enable without connection\n");
		return -ENODEV;
	}

	if (dp->active)
		return 0;

	ret = cdn_dp_clk_enable(dp);
	if (ret)
		return ret;

	ret = cdn_dp_firmware_init(dp);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "firmware init failed: %d", ret);
		goto err_clk_disable;
	}

	/* only enable the port that connected with downstream device */
	for (i = port->id; i < dp->ports; i++) {
		port = dp->port[i];
		lanes = cdn_dp_get_port_lanes(port);
		if (lanes) {
			ret = cdn_dp_enable_phy(dp, port);
			if (ret)
				continue;

			ret = cdn_dp_get_sink_capability(dp);
			if (ret) {
				cdn_dp_disable_phy(dp, port);
			} else {
				dp->active = true;
				dp->lanes = port->lanes;
				return 0;
			}
		}
	}

	/* Enable hdcp if it's desired */
	if (dp->connector.state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED)
		ret = cdn_dp_start_hdcp1x_auth(dp);

	return ret;

err_clk_disable:
	cdn_dp_clk_disable(dp);
	return ret;
}

static void cdn_dp_encoder_mode_set(struct drm_encoder *encoder,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	struct drm_display_info *display_info = &dp->connector.display_info;
	struct video_info *video = &dp->video_info;

	switch (display_info->bpc) {
	case 10:
		video->color_depth = 10;
		break;
	case 6:
		video->color_depth = 6;
		break;
	default:
		video->color_depth = 8;
		break;
	}

	video->color_fmt = PXL_RGB;
	video->v_sync_polarity = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);
	video->h_sync_polarity = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);

	memcpy(&dp->mode, adjusted, sizeof(*mode));
}

static bool cdn_dp_check_link_status(struct cdn_dp_device *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	struct cdn_dp_port *port = cdn_dp_connected_port(dp);
	u8 sink_lanes = drm_dp_max_lane_count(dp->dpcd);

	if (!port || !dp->link.rate || !dp->link.num_lanes)
		return false;

	if (drm_dp_dpcd_read_link_status(&dp->aux, link_status) !=
	    DP_LINK_STATUS_SIZE) {
		DRM_ERROR("Failed to get link status\n");
		return false;
	}

	/* if link training is requested we should perform it always */
	return drm_dp_channel_eq_ok(link_status, min(port->lanes, sink_lanes));
}

static void cdn_dp_encoder_enable(struct drm_encoder *encoder)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	int ret, val;

	ret = drm_of_encoder_active_endpoint_id(dp->dev->of_node, encoder);
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "Could not get vop id, %d", ret);
		return;
	}

	DRM_DEV_DEBUG_KMS(dp->dev, "vop %s output to cdn-dp\n",
			  (ret) ? "LIT" : "BIG");
	if (ret)
		val = DP_SEL_VOP_LIT | (DP_SEL_VOP_LIT << 16);
	else
		val = DP_SEL_VOP_LIT << 16;

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON9, val);
	if (ret)
		return;

	mutex_lock(&dp->lock);

	ret = cdn_dp_enable(dp);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "Failed to enable encoder %d\n",
			      ret);
		goto out;
	}
	if (!cdn_dp_check_link_status(dp)) {
		ret = cdn_dp_train_link(dp);
		if (ret) {
			DRM_DEV_ERROR(dp->dev, "Failed link train %d\n", ret);
			goto out;
		}
	}
	if (dp->use_fw_training) {
		ret = cdn_dp_set_video_status(dp, CONTROL_VIDEO_IDLE);
		if (ret) {
			DRM_DEV_ERROR(dp->dev,
				      "Failed to idle video %d\n", ret);
			goto out;
		}
	}

	ret = cdn_dp_config_video(dp);
	if (ret) {
		DRM_DEV_ERROR(dp->dev, "Failed to config video %d\n", ret);
		goto out;
	}

	if (dp->use_fw_training) {
		ret = cdn_dp_set_video_status(dp, CONTROL_VIDEO_VALID);
		if (ret) {
			DRM_DEV_ERROR(dp->dev,
				"Failed to valid video %d\n", ret);
			goto out;
		}
	}

out:
	mutex_unlock(&dp->lock);
	if (!ret) {
#ifdef CONFIG_SWITCH
		switch_set_state(&dp->switchdev, 1);
#else
		hdmi_event_connect(dp->dev);
#endif
	}
}

static void cdn_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	int ret;

	mutex_lock(&dp->lock);

	if (dp->active) {
		ret = cdn_dp_disable(dp);
		if (ret) {
			DRM_DEV_ERROR(dp->dev, "Failed to disable encoder %d\n",
				      ret);
		}
	}
	mutex_unlock(&dp->lock);
#ifdef CONFIG_SWITCH
	switch_set_state(&dp->switchdev, 0);
#else
	hdmi_event_disconnect(dp->dev);
#endif

	/*
	 * In the following 2 cases, we need to run the event_work to re-enable
	 * the DP:
	 * 1. If there is not just one port device is connected, and remove one
	 *    device from a port, the DP will be disabled here, at this case,
	 *    run the event_work to re-open DP for the other port.
	 * 2. If re-training or re-config failed, the DP will be disabled here.
	 *    run the event_work to re-connect it.
	 */
	if (!dp->connected && cdn_dp_connected_port(dp))
		schedule_work(&dp->event_work);
}

void cdn_dp_hdcp_atomic_enable(struct drm_connector *connector)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	int ret;

	cancel_delayed_work_sync(&dp->hdcp_event_work);

	mutex_lock(&dp->lock);

	dp->hdcp_desired = true;

	ret = cdn_dp_start_hdcp1x_auth(dp);
	if (ret)
		DRM_DEV_ERROR(dp->dev, "hdcp start failed %d\n", ret);

	mutex_unlock(&dp->lock);
}

void cdn_dp_hdcp_atomic_disable(struct drm_connector *connector)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	uint64_t val;
	int ret;

	cancel_delayed_work_sync(&dp->hdcp_event_work);

	mutex_lock(&dp->lock);

	val = dp->connector.state->content_protection;
	dp->hdcp_desired = val == DRM_MODE_CONTENT_PROTECTION_DESIRED;

	ret = cdn_dp_stop_hdcp1x_auth(dp);
	if (ret)
		DRM_DEV_ERROR(dp->dev, "Failed to stop hdcp%d\n", ret);

	mutex_unlock(&dp->lock);
}

static int cdn_dp_hdcp_atomic_check(struct cdn_dp_device *dp,
				    uint64_t old_val,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	mutex_lock(&dp->lock);

	if (!conn_state->crtc) {
		/*
		 * If the connector is being disabled with CP enabled, mark it
		 * desired so it's re-enabled when the connector is brought back
		 */
		if (old_val == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			conn_state->content_protection =
				DRM_MODE_CONTENT_PROTECTION_DESIRED;
	}

	mutex_unlock(&dp->lock);
	return 0;
}

static int cdn_dp_encoder_atomic_check(struct drm_encoder *encoder,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	uint64_t old_cp = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_DisplayPort;
	s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	s->tv_state = &conn_state->tv;
	s->eotf = TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	if (dp->connector.state)
		old_cp = dp->connector.state->content_protection;

	return cdn_dp_hdcp_atomic_check(dp, old_cp, crtc_state, conn_state);
}

static const struct drm_encoder_helper_funcs cdn_dp_encoder_helper_funcs = {
	.mode_set = cdn_dp_encoder_mode_set,
	.enable = cdn_dp_encoder_enable,
	.disable = cdn_dp_encoder_disable,
	.atomic_check = cdn_dp_encoder_atomic_check,
};

static const struct drm_encoder_funcs cdn_dp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int cdn_dp_parse_dt(struct cdn_dp_device *dp)
{
	struct device *dev = dp->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	dp->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dp->grf)) {
		DRM_DEV_ERROR(dev, "cdn-dp needs rockchip,grf property\n");
		return PTR_ERR(dp->grf);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dp->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(dp->regs)) {
		DRM_DEV_ERROR(dev, "ioremap reg failed\n");
		return PTR_ERR(dp->regs);
	}

	dp->core_clk = devm_clk_get(dev, "core-clk");
	if (IS_ERR(dp->core_clk)) {
		DRM_DEV_ERROR(dev, "cannot get core_clk_dp\n");
		return PTR_ERR(dp->core_clk);
	}

	dp->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dp->pclk)) {
		DRM_DEV_ERROR(dev, "cannot get pclk\n");
		return PTR_ERR(dp->pclk);
	}

	dp->spdif_clk = devm_clk_get(dev, "spdif");
	if (IS_ERR(dp->spdif_clk)) {
		DRM_DEV_ERROR(dev, "cannot get spdif_clk\n");
		return PTR_ERR(dp->spdif_clk);
	}

	dp->grf_clk = devm_clk_get(dev, "grf");
	if (IS_ERR(dp->grf_clk)) {
		DRM_DEV_ERROR(dev, "cannot get grf clk\n");
		return PTR_ERR(dp->grf_clk);
	}

	dp->spdif_rst = devm_reset_control_get(dev, "spdif");
	if (IS_ERR(dp->spdif_rst)) {
		DRM_DEV_ERROR(dev, "no spdif reset control found\n");
		return PTR_ERR(dp->spdif_rst);
	}

	dp->dptx_rst = devm_reset_control_get(dev, "dptx");
	if (IS_ERR(dp->dptx_rst)) {
		DRM_DEV_ERROR(dev, "no uphy reset control found\n");
		return PTR_ERR(dp->dptx_rst);
	}

	dp->core_rst = devm_reset_control_get(dev, "core");
	if (IS_ERR(dp->core_rst)) {
		DRM_DEV_ERROR(dev, "no core reset control found\n");
		return PTR_ERR(dp->core_rst);
	}

	dp->apb_rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(dp->apb_rst)) {
		DRM_DEV_ERROR(dev, "no apb reset control found\n");
		return PTR_ERR(dp->apb_rst);
	}

	return 0;
}

struct dp_sdp {
	struct edp_sdp_header sdp_header;
	u8 db[28];
} __packed;

static int cdn_dp_setup_audio_infoframe(struct cdn_dp_device *dp)
{
	struct dp_sdp infoframe_sdp;
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;

	/* Prepare VSC packet as per EDP 1.4 spec, Table 6.9 */
	memset(&infoframe_sdp, 0, sizeof(infoframe_sdp));
	infoframe_sdp.sdp_header.HB0 = 0;
	infoframe_sdp.sdp_header.HB1 = HDMI_INFOFRAME_TYPE_AUDIO;
	infoframe_sdp.sdp_header.HB2 = 0x1b;
	infoframe_sdp.sdp_header.HB3 = 0x48;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		DRM_DEV_ERROR(dp->dev, "Failed to setup audio infoframe: %zd\n",
			      err);
		return err;
	}

	frame.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	frame.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	frame.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	frame.channels = 0;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_DEV_ERROR(dp->dev, "Failed to pack audio infoframe: %zd\n",
			      err);
		return err;
	}

	memcpy(&infoframe_sdp.db[0], &buffer[HDMI_INFOFRAME_HEADER_SIZE],
	       sizeof(buffer) - HDMI_INFOFRAME_HEADER_SIZE);

	cdn_dp_infoframe_set(dp, 0, (u8 *)&infoframe_sdp,
			     sizeof(infoframe_sdp), 0x84);

	return 0;
}

static int cdn_dp_audio_hw_params(struct device *dev,  void *data,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	struct audio_info audio = {
		.sample_width = params->sample_width,
		.sample_rate = params->sample_rate,
		.channels = params->channels,
	};
	int ret;

	mutex_lock(&dp->lock);
	if (!dp->active) {
		ret = 0;
		goto out;
	}

	switch (daifmt->fmt) {
	case HDMI_I2S:
		audio.format = AFMT_I2S;
		break;
	case HDMI_SPDIF:
		audio.format = AFMT_SPDIF;
		break;
	default:
		DRM_DEV_ERROR(dev, "Invalid format %d\n", daifmt->fmt);
		ret = -EINVAL;
		goto out;
	}

	ret = cdn_dp_setup_audio_infoframe(dp);
	if (ret)
		goto out;

	ret = cdn_dp_audio_config(dp, &audio);
	if (!ret)
		dp->audio_info = audio;

out:
	mutex_unlock(&dp->lock);
	return ret;
}

static void cdn_dp_audio_shutdown(struct device *dev, void *data)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&dp->lock);
	if (!dp->active)
		goto out;

	ret = cdn_dp_audio_stop(dp, &dp->audio_info);
	if (!ret)
		dp->audio_info.format = AFMT_UNUSED;
out:
	mutex_unlock(&dp->lock);
}

static int cdn_dp_audio_digital_mute(struct device *dev, void *data,
				     bool enable)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&dp->lock);
	if (!dp->active) {
		ret = 0;
		goto out;
	}

	ret = cdn_dp_audio_mute(dp, enable);

out:
	mutex_unlock(&dp->lock);
	return ret;
}

static int cdn_dp_audio_get_eld(struct device *dev, void *data,
				u8 *buf, size_t len)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);

	memcpy(buf, dp->connector.eld, min(sizeof(dp->connector.eld), len));

	return 0;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = cdn_dp_audio_hw_params,
	.audio_shutdown = cdn_dp_audio_shutdown,
	.digital_mute = cdn_dp_audio_digital_mute,
	.get_eld = cdn_dp_audio_get_eld,
};

static int cdn_dp_audio_codec_init(struct cdn_dp_device *dp,
				   struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.i2s = 1,
		.spdif = 1,
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
	};

	dp->audio_pdev = platform_device_register_data(
			 dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
			 &codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(dp->audio_pdev);
}

static int cdn_dp_request_firmware(struct cdn_dp_device *dp)
{
	int ret;
	unsigned long timeout = jiffies + msecs_to_jiffies(CDN_FW_TIMEOUT_MS);
	unsigned long sleep = 1000;

	WARN_ON(!mutex_is_locked(&dp->lock));

	if (dp->fw_loaded)
		return 0;

	/* Drop the lock before getting the firmware to avoid blocking boot */
	mutex_unlock(&dp->lock);

	while (time_before(jiffies, timeout)) {
		ret = request_firmware(&dp->fw, CDN_DP_FIRMWARE, dp->dev);
		if (ret == -ENOENT) {
			msleep(sleep);
			sleep *= 2;
			continue;
		} else if (ret) {
			DRM_DEV_ERROR(dp->dev,
				      "failed to request firmware: %d\n", ret);
			goto out;
		}

		dp->fw_loaded = true;
		ret = 0;
		goto out;
	}

	DRM_DEV_ERROR(dp->dev, "Timed out trying to load firmware\n");
	ret = -ETIMEDOUT;
out:
	mutex_lock(&dp->lock);
	return ret;
}

static void cdn_dp_pd_event_work(struct work_struct *work)
{
	struct cdn_dp_device *dp = container_of(work, struct cdn_dp_device,
						event_work);
	struct drm_connector *connector = &dp->connector;
	enum drm_connector_status old_status;

	int ret;

	mutex_lock(&dp->lock);

	if (dp->suspended)
		goto out;

	ret = cdn_dp_request_firmware(dp);
	if (ret)
		goto out;

	dp->connected = true;

	/* Not connected, notify userspace to disable the block */
	if (!cdn_dp_connected_port(dp)) {
		DRM_DEV_INFO(dp->dev, "Not connected. Disabling cdn\n");
		dp->connected = false;
		if (dp->hdcp_desired)
			cdn_dp_stop_hdcp1x_auth(dp);

	/* Connected but not enabled, enable the block */
	} else if (!dp->active) {
		DRM_DEV_INFO(dp->dev, "Connected, not enabled. Enabling cdn\n");
		ret = cdn_dp_enable(dp);
		if (ret) {
			DRM_DEV_ERROR(dp->dev, "Enable dp failed %d\n", ret);
			dp->connected = false;
			if (dp->hdcp_desired)
				cdn_dp_stop_hdcp1x_auth(dp);
		}

	/* Enabled and connected to a dongle without a sink, notify userspace */
	} else if (!cdn_dp_check_sink_connection(dp)) {
		DRM_DEV_INFO(dp->dev, "Connected without sink. Assert hpd\n");
		dp->connected = false;
		if (dp->hdcp_desired)
			cdn_dp_stop_hdcp1x_auth(dp);

	/* Enabled and connected with a sink, re-train if requested */
	} else if (!cdn_dp_check_link_status(dp)) {
		unsigned int rate = dp->link.rate;
		unsigned int lanes = dp->link.num_lanes;
		struct drm_display_mode *mode = &dp->mode;

		DRM_DEV_INFO(dp->dev, "Connected with sink. Re-train link\n");
		ret = cdn_dp_train_link(dp);
		if (ret) {
			dp->connected = false;
			DRM_DEV_ERROR(dp->dev, "Train link failed %d\n", ret);
			goto out;
		}

		/* If training result is changed, update the video config */
		if (mode->clock &&
		    (rate != dp->link.rate || lanes != dp->link.num_lanes)) {
			ret = cdn_dp_config_video(dp);
			if (ret) {
				dp->connected = false;
				DRM_DEV_ERROR(dp->dev,
					      "Failed to config video %d\n",
					      ret);
			}
		}

		if (dp->hdcp_desired) {
			ret = cdn_dp_start_hdcp1x_auth(dp);
			if (ret)
				DRM_DEV_ERROR(dp->dev,
					      "Failed to re-enable hdcp %d\n",
					      ret);
		}
	}

out:
	mutex_unlock(&dp->lock);

	old_status = connector->status;
	connector->status = connector->funcs->detect(connector, false);
	if (old_status != connector->status)
		drm_kms_helper_hotplug_event(dp->drm_dev);
}

static int cdn_dp_pd_event(struct notifier_block *nb,
			   unsigned long event, void *priv)
{
	struct cdn_dp_port *port = container_of(nb, struct cdn_dp_port,
						event_nb);
	struct cdn_dp_device *dp = port->dp;

	/*
	 * It would be nice to be able to just do the work inline right here.
	 * However, we need to make a bunch of calls that might sleep in order
	 * to turn on the block/phy, so use a worker instead.
	 */
	schedule_work(&dp->event_work);

	return NOTIFY_DONE;
}

static bool cdn_dp_hdcp_authorize(struct cdn_dp_device *dp)
{
	bool auth_done = false;
	u16 tx_status;
	u32 sw_event;
	int ret = 0;

	mutex_lock(&dp->lock);

	/*
	 * HDCP authentication might cause the disconnect hpd event, just
	 * stop the hdcp event SM.
	 */
	if (!dp->connected) {
		auth_done = true;
		goto out;
	}

	sw_event = cdn_dp_get_event(dp);

	if (sw_event & HDCP_TX_STATUS_EVENT) {
		ret = cdn_dp_hdcp_tx_status_req(dp, &tx_status);
		if (ret)
			goto out;
		if (HDCP_TX_STATUS_ERROR(tx_status)) {
			dp->hdcp_enabled = false;
			DRM_DEV_ERROR(dp->dev, "hdcp status error: %#x\n",
				HDCP_TX_STATUS_ERROR(tx_status));
			goto out;
		} else if (tx_status & HDCP_TX_STATUS_AUTHENTICATED) {
			if (!ret) {
				dp->hdcp_enabled = true;
				auth_done = true;
				DRM_DEV_INFO(dp->dev, "HDCP is enabled\n");
			}
			goto out;
		}
	}

	if (sw_event & HDCP_TX_IS_RECEIVER_ID_VALID_EVENT) {
		ret = cdn_dp_hdcp_tx_is_receiver_id_valid_req(dp);
		if (ret) {
			dp->hdcp_enabled = false;
			auth_done = true;
			goto out;
		}
		ret = cdn_dp_hdcp_tx_respond_id_valid(dp, true);
		if (ret) {
			dp->hdcp_enabled = false;
			auth_done = true;
		}
	}

out:
	mutex_unlock(&dp->lock);

	return auth_done;
}

static void cdn_dp_hdcp_event_work(struct work_struct *work)
{
	struct cdn_dp_device *dp = container_of(work, struct cdn_dp_device,
						hdcp_event_work.work);
	bool auth_done;
	int ret;

	ret = readx_poll_timeout(cdn_dp_hdcp_authorize, dp, auth_done,
				 auth_done, HDCP_RETRY_INTERVAL_US,
				 HDCP_EVENT_TIMEOUT_US);
	if (ret)
		DRM_DEV_ERROR(dp->dev, "Failed to authorize hdcp\n");

	/*
	 * Ensure the content_protection property value is consistent with the
	 * current authentication state of the hardware. This might mean a
	 * transition from DESIRED->ENABLED, or in the case of error,
	 * ENABLED->DESIRED. We never transition to ->UNDESIRED, that's up to
	 * atomic_check().
	 */
	schedule_work(&dp->hdcp_prop_work);
}

static void cdn_dp_hdcp_prop_work(struct work_struct *work)
{
	struct cdn_dp_device *dp = container_of(work, struct cdn_dp_device,
						hdcp_prop_work);
	struct drm_device *dev = dp->drm_dev;
	struct drm_connector *connector = &dp->connector;
	struct drm_connector_state *state;
	uint64_t val;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	mutex_lock(&dp->lock);

	/*
	 * This worker is only used to flip between ENABLED/DESIRED. Either of
	 * those to UNDESIRED is handled by core. If !hdcp_desired, we're
	 * running just after hdcp has been disabled, so just exit
	 */
	if (dp->hdcp_desired) {
		if (dp->hdcp_enabled)
			val = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		else
			val = DRM_MODE_CONTENT_PROTECTION_DESIRED;

		state = connector->state;
		state->content_protection = val;
	}

	mutex_unlock(&dp->lock);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);

}

static ssize_t cdn_dp_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	struct cdn_dp_device *dp = container_of(aux, struct cdn_dp_device, aux);
	int ret;
	u8 status;

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		ret = cdn_dp_dpcd_write(dp, msg->address, msg->buffer,
					msg->size);
		break;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		ret = cdn_dp_dpcd_read(dp, msg->address, msg->buffer,
				       msg->size);
		break;
	default:
		return -EINVAL;
	}

	status = cdn_dp_get_aux_status(dp);
	if (status == AUX_STATUS_ACK)
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
	else if (status == AUX_STATUS_NACK)
		msg->reply = DP_AUX_NATIVE_REPLY_NACK;
	else if (status == AUX_STATUS_DEFER)
		msg->reply = DP_AUX_NATIVE_REPLY_DEFER;

	return ret;
}

static int cdn_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct cdn_dp_port *port;
	struct drm_device *drm_dev = data;
	int ret, i;

	ret = cdn_dp_parse_dt(dp);
	if (ret < 0)
		return ret;

	dp->drm_dev = drm_dev;
	dp->connected = false;
	dp->active = false;
	dp->active_port = -1;
	dp->fw_loaded = false;
	dp->aux.name = "DP-AUX";
	dp->aux.transfer = cdn_dp_aux_transfer;
	dp->aux.dev = dev;

	ret = drm_dp_aux_register(&dp->aux);
	if (ret)
		return ret;

	INIT_WORK(&dp->event_work, cdn_dp_pd_event_work);
	INIT_DELAYED_WORK(&dp->hdcp_event_work, cdn_dp_hdcp_event_work);
	INIT_WORK(&dp->hdcp_prop_work, cdn_dp_hdcp_prop_work);

	encoder = &dp->encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);
	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	ret = drm_encoder_init(drm_dev, encoder, &cdn_dp_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &cdn_dp_encoder_helper_funcs);

	connector = &dp->connector;
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->dpms = DRM_MODE_DPMS_OFF;

	ret = drm_connector_init(drm_dev, connector,
				 &cdn_dp_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		DRM_ERROR("failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector, &cdn_dp_connector_helper_funcs);

#ifdef CONFIG_SWITCH
	dp->switchdev.name = "cdn-dp";
	switch_dev_register(&dp->switchdev);
#endif

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector;
	}

	cdn_dp_audio_codec_init(dp, dev);

	for (i = 0; i < dp->ports; i++) {
		port = dp->port[i];

		port->event_nb.notifier_call = cdn_dp_pd_event;
		ret = devm_extcon_register_notifier(dp->dev, port->extcon,
						    EXTCON_DISP_DP,
						    &port->event_nb);
		if (ret) {
			DRM_DEV_ERROR(dev,
				      "register EXTCON_DISP_DP notifier err\n");
			goto err_free_connector;
		}
	}

	pm_runtime_enable(dev);

	schedule_work(&dp->event_work);

	ret = drm_connector_attach_content_protection_property(connector);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach protection (%d)\n", ret);
		goto err_free_connector;
	}

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void cdn_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_connector *connector = &dp->connector;

#ifdef CONFIG_SWITCH
	switch_dev_unregister(&dp->switchdev);
#endif

	cancel_work_sync(&dp->event_work);
	platform_device_unregister(dp->audio_pdev);
	cdn_dp_encoder_disable(encoder);
	encoder->funcs->destroy(encoder);
	connector->funcs->destroy(connector);

	pm_runtime_disable(dev);
	if (dp->fw_loaded)
		release_firmware(dp->fw);
	kfree(dp->edid);
	dp->edid = NULL;
}

static const struct component_ops cdn_dp_component_ops = {
	.bind = cdn_dp_bind,
	.unbind = cdn_dp_unbind,
};

static ssize_t hdcp_key_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct nvmem_cell *cell;
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	u8 *cpu_id;
	int ret;
	size_t len;

	/*
	 * The HDCP Key format should look like this: "12345678...",
	 * every two characters stand for a byte, so the total key size
	 * would be (308 * 2) byte.
	 */
	if (count != (CDN_DP_HDCP_KEY_LEN * 2)) {
		DRM_DEV_ERROR(dev, "mis-match hdcp cipher length\n");
		return -EINVAL;
	}

	cell = nvmem_cell_get(dev, "cpu-id");
	if (IS_ERR(cell)) {
		DRM_DEV_ERROR(dev, "missing cpu-id nvmen cell property\n");
		return -ENODEV;
	}

	cpu_id = (u8 *)nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(cpu_id))
		return PTR_ERR(cpu_id);

	if (len != CDN_DP_HDCP_UID_LEN) {
		DRM_DEV_ERROR(dev, "mismatch cpu-id size\n");
		ret = -EINVAL;
		goto err;
	}

	/*
	 * The format of input HDCP Key should be "12345678...".
	 * there is no standard format for HDCP keys, so it is
	 * just made up for this driver.
	 *
	 * The "ksv & device_key & sha" should parsed from input data
	 * buffer, and the "seed" would take the crc16 of cpu uid.
	 */
	ret = hex2bin((u8 *)&dp->key, buf, CDN_DP_HDCP_KEY_LEN);
	if (ret) {
		DRM_DEV_ERROR(dev, "decode input HDCP key format failed %d\n",
			      ret);
		goto err;
	}

	memcpy(dp->key.uid, cpu_id, CDN_DP_HDCP_UID_LEN);
	dp->key.seed = crc16(0xffff, cpu_id, 16);
	ret = count;
err:
	kfree(cpu_id);
	return ret;
}
static DEVICE_ATTR(hdcp_key, S_IWUSR, NULL, hdcp_key_store);

static int cdn_dp_suspend(struct device *dev)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&dp->lock);
	if (dp->active)
		ret = cdn_dp_disable(dp);
	dp->suspended = true;
	mutex_unlock(&dp->lock);

	return ret;
}

static int cdn_dp_resume(struct device *dev)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);

	mutex_lock(&dp->lock);
	dp->suspended = false;
	if (dp->fw_loaded)
		schedule_work(&dp->event_work);
	mutex_unlock(&dp->lock);

	return 0;
}

static int cdn_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct cdn_dp_data *dp_data;
	struct cdn_dp_port *port;
	struct cdn_dp_device *dp;
	struct extcon_dev *extcon;
	struct phy *phy;
	int ret, i;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;
	dp->dev = dev;

	match = of_match_node(cdn_dp_dt_ids, pdev->dev.of_node);
	dp_data = (struct cdn_dp_data *)match->data;

	for (i = 0; i < dp_data->max_phy; i++) {
		extcon = extcon_get_edev_by_phandle(dev, i);
		phy = devm_of_phy_get_by_index(dev, dev->of_node, i);

		if (PTR_ERR(extcon) == -EPROBE_DEFER ||
		    PTR_ERR(phy) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		if (IS_ERR(extcon) || IS_ERR(phy))
			continue;

		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		port->extcon = extcon;
		port->phy = phy;
		port->dp = dp;
		port->id = i;
		dp->port[dp->ports++] = port;
	}

	if (!dp->ports) {
		DRM_DEV_ERROR(dev, "missing extcon or phy\n");
		return -EINVAL;
	}

	mutex_init(&dp->lock);
	dev_set_drvdata(dev, dp);

	ret = device_create_file(dev, &dev_attr_hdcp_key);
	if (ret)
		return ret;

	ret = component_add(dev, &cdn_dp_component_ops);
	if (ret)
		goto err;

	return 0;

err:
	device_remove_file(dev, &dev_attr_hdcp_key);
	return ret;
}

static int cdn_dp_remove(struct platform_device *pdev)
{
	struct cdn_dp_device *dp = platform_get_drvdata(pdev);

	cdn_dp_suspend(dp->dev);
	component_del(&pdev->dev, &cdn_dp_component_ops);
	device_remove_file(&pdev->dev, &dev_attr_hdcp_key);

	return 0;
}

static void cdn_dp_shutdown(struct platform_device *pdev)
{
	struct cdn_dp_device *dp = platform_get_drvdata(pdev);

	cdn_dp_suspend(dp->dev);
}

static const struct dev_pm_ops cdn_dp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdn_dp_suspend,
				cdn_dp_resume)
};

static struct platform_driver cdn_dp_driver = {
	.probe = cdn_dp_probe,
	.remove = cdn_dp_remove,
	.shutdown = cdn_dp_shutdown,
	.driver = {
		   .name = "cdn-dp",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(cdn_dp_dt_ids),
		   .pm = &cdn_dp_pm_ops,
	},
};

module_platform_driver(cdn_dp_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_DESCRIPTION("cdn DP Driver");
MODULE_LICENSE("GPL v2");
