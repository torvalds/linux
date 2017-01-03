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

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/extcon.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>
#include <sound/hdmi-codec.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/fb.h>
#include <linux/platform_device.h>
#include "rockchip_dp_core.h"
#include "cdn-dp-fb-reg.h"

static struct cdn_dp_data rk3399_cdn_dp = {
	.max_phy = 2,
};

static const struct of_device_id cdn_dp_dt_ids[] = {
	{ .compatible = "rockchip,rk3399-cdn-dp-fb",
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
		dev_err(dp->dev, "Failed to prepare_enable grf clock\n");
		return ret;
	}

	ret = regmap_write(dp->grf, reg, val);
	if (ret) {
		dev_err(dp->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	clk_disable_unprepare(dp->grf_clk);

	return 0;
}

static int cdn_dp_set_fw_rate(struct cdn_dp_device *dp)
{
	u32 rate;

	if (!dp->fw_clk_enabled) {
		rate = clk_get_rate(dp->core_clk);
		if (rate == 0) {
			dev_err(dp->dev, "get clk rate failed: %d\n", rate);
			return rate;
		}
		cdn_dp_fb_set_fw_clk(dp, rate);
		cdn_dp_fb_clock_reset(dp);
		dp->fw_clk_enabled = true;
	}

	return 0;
}

static int cdn_dp_clk_enable(struct cdn_dp_device *dp)
{
	int ret;

	ret = clk_prepare_enable(dp->pclk);
	if (ret < 0) {
		dev_err(dp->dev, "cannot enable dp pclk %d\n", ret);
		goto runtime_get_pm;
	}

	ret = clk_prepare_enable(dp->core_clk);
	if (ret < 0) {
		dev_err(dp->dev, "cannot enable core_clk %d\n", ret);
		goto err_core_clk;
	}

	ret = pm_runtime_get_sync(dp->dev);
	if (ret < 0) {
		dev_err(dp->dev, "cannot get pm runtime %d\n", ret);
		return ret;
	}

	reset_control_assert(dp->apb_rst);
	reset_control_assert(dp->core_rst);
	reset_control_assert(dp->dptx_rst);
	udelay(1);
	reset_control_deassert(dp->dptx_rst);
	reset_control_deassert(dp->core_rst);
	reset_control_deassert(dp->apb_rst);

	ret = cdn_dp_set_fw_rate(dp);
	if (ret < 0) {
		dev_err(dp->dev, "cannot get set fw rate %d\n", ret);
		goto err_set_rate;
	}

	return 0;

err_set_rate:
	clk_disable_unprepare(dp->core_clk);
err_core_clk:
	clk_disable_unprepare(dp->pclk);
runtime_get_pm:
	pm_runtime_put_sync(dp->dev);
	return ret;
}

static void cdn_dp_clk_disable(struct cdn_dp_device *dp)
{
	pm_runtime_put_sync(dp->dev);
	clk_disable_unprepare(dp->pclk);
	clk_disable_unprepare(dp->core_clk);
}

int cdn_dp_get_edid(void *dp, u8 *buf, int block)
{
	int ret;
	struct cdn_dp_device *dp_dev = dp;

	mutex_lock(&dp_dev->lock);
	ret = cdn_dp_fb_get_edid_block(dp_dev, buf, block, EDID_BLOCK_SIZE);
	mutex_unlock(&dp_dev->lock);

	return ret;
}

int cdn_dp_connector_detect(void *dp)
{
	struct cdn_dp_device *dp_dev = dp;
	bool ret = false;

	mutex_lock(&dp_dev->lock);
	if (dp_dev->hpd_status == connector_status_connected)
		ret = true;
	mutex_unlock(&dp_dev->lock);

	return ret;
}

int cdn_dp_encoder_disable(void *dp)
{
	struct cdn_dp_device *dp_dev = dp;
	int ret = 0;

	mutex_lock(&dp_dev->lock);
	memset(&dp_dev->mode, 0, sizeof(dp_dev->mode));
	if (dp_dev->hpd_status == connector_status_disconnected) {
		dp_dev->dpms_mode = DRM_MODE_DPMS_OFF;
		mutex_unlock(&dp_dev->lock);
		return ret;
	}

	if (dp_dev->dpms_mode == DRM_MODE_DPMS_ON) {
		dp_dev->dpms_mode = DRM_MODE_DPMS_OFF;
	} else{
		dev_warn(dp_dev->dev, "wrong dpms status,dp encoder has already been disabled\n");
		ret = -1;
	}
	mutex_unlock(&dp_dev->lock);

	return ret;
}

static void cdn_dp_commit(struct cdn_dp_device *dp)
{
	char guid[16];
	int ret = cdn_dp_fb_training_start(dp);

	if (ret) {
		dev_err(dp->dev, "link training failed: %d\n", ret);
		return;
	}

	ret = cdn_dp_fb_get_training_status(dp);
	if (ret) {
		dev_err(dp->dev, "get link training status failed: %d\n", ret);
		return;
	}

	dev_info(dp->dev, "rate:%d, lanes:%d\n",
			dp->link.rate, dp->link.num_lanes);

	/**
	* Use dpcd@0x0030~0x003f(which is GUID registers) to sync with NanoC
	* to make sure training is ok. Nanoc will write "nanoc" in GUID registers
	* when booting, and then we will use these registers to decide whether
	* need to sync with device which plugged in.
	* The sync register is 0x0035, firstly we write 0xaa to sync register,
	* nanoc will read this register and then start the part2 code of DP.
	*/
	ret = cdn_dp_fb_dpcd_read(dp, 0x0030, guid, 8);
	if (ret == 0 && guid[0] == 'n' && guid[1] == 'a' && guid[2] == 'n' &&
			guid[3] == 'o' && guid[4] == 'c') {
		u8 sync_number = 0xaa;

		cdn_dp_fb_dpcd_write(dp, 0x0035, sync_number);
	}

	if (cdn_dp_fb_set_video_status(dp, CONTROL_VIDEO_IDLE))
		return;

	if (cdn_dp_fb_config_video(dp)) {
		dev_err(dp->dev, "unable to config video\n");
		return;
	}

	if (cdn_dp_fb_set_video_status(dp, CONTROL_VIDEO_VALID))
		return;

	dp->dpms_mode = DRM_MODE_DPMS_ON;
}

int cdn_dp_encoder_mode_set(void *dp, struct dp_disp_info *disp_info)
{
	int ret, val;
	struct cdn_dp_device *dp_dev = dp;
	struct video_info *video = &dp_dev->video_info;
	struct drm_display_mode disp_mode;
	struct fb_videomode *mode = disp_info->mode;

	mutex_lock(&dp_dev->lock);
	disp_mode.clock = mode->pixclock / 1000;
	disp_mode.hdisplay = mode->xres;
	disp_mode.hsync_start = disp_mode.hdisplay + mode->right_margin;
	disp_mode.hsync_end = disp_mode.hsync_start + mode->hsync_len;
	disp_mode.htotal = disp_mode.hsync_end + mode->left_margin;
	disp_mode.vdisplay = mode->yres;
	disp_mode.vsync_start = disp_mode.vdisplay + mode->lower_margin;
	disp_mode.vsync_end = disp_mode.vsync_start + mode->vsync_len;
	disp_mode.vtotal = disp_mode.vsync_end + mode->upper_margin;

	switch (disp_info->color_depth) {
	case 16:
	case 12:
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

	video->v_sync_polarity = disp_info->vsync_polarity;
	video->h_sync_polarity = disp_info->hsync_polarity;

	if (disp_info->vop_sel)
		val = DP_SEL_VOP_LIT | (DP_SEL_VOP_LIT << 16);
	else
		val = DP_SEL_VOP_LIT << 16;

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON9, val);
	if (ret != 0) {
		dev_err(dp_dev->dev, "Could not write to GRF: %d\n", ret);
		mutex_unlock(&dp_dev->lock);
		return ret;
	}
	memcpy(&dp_dev->mode, &disp_mode, sizeof(disp_mode));

	mutex_unlock(&dp_dev->lock);

	return 0;
}

int cdn_dp_encoder_enable(void *dp)
{
	struct cdn_dp_device *dp_dev = dp;
	int ret = 0;

	mutex_lock(&dp_dev->lock);

	if (dp_dev->dpms_mode == DRM_MODE_DPMS_OFF) {
		/**
		* the mode info of dp device will be cleared when dp encoder is disabled
		* so if clock value of mode is 0, means rockchip_dp_config_video is not
		* return success, so we don't do cdn_dp_commit.
		*/
		if (dp_dev->mode.clock == 0) {
			dev_err(dp_dev->dev, "Error !Please make sure function cdn_dp_encoder_mode_set return success!\n");
			mutex_unlock(&dp_dev->lock);
			return -1;
		}
		cdn_dp_commit(dp_dev);
	} else {
		dev_warn(dp_dev->dev, "wrong dpms status,dp encoder has already been enabled\n");
		ret = -1;
	}
	mutex_unlock(&dp_dev->lock);

	return ret;
}

static int cdn_dp_firmware_init(struct cdn_dp_device *dp)
{
	int ret;
	const u32 *iram_data, *dram_data;
	const struct firmware *fw = dp->fw;
	const struct cdn_firmware_header *hdr;

	hdr = (struct cdn_firmware_header *)fw->data;
	if (fw->size != le32_to_cpu(hdr->size_bytes)) {
		dev_err(dp->dev, "firmware is invalid\n");
		return -EINVAL;
	}

	iram_data = (const u32 *)(fw->data + hdr->header_size);
	dram_data = (const u32 *)(fw->data + hdr->header_size + hdr->iram_size);

	ret = cdn_dp_fb_load_firmware(dp, iram_data, hdr->iram_size,
				   dram_data, hdr->dram_size);
	if (ret)
		return ret;

	ret = cdn_dp_fb_set_firmware_active(dp, true);
	if (ret) {
		dev_err(dp->dev, "active ucpu failed: %d\n", ret);
		return ret;
	}

	dp->fw_loaded = 1;
	return cdn_dp_fb_event_config(dp);
}

static int cdn_dp_init(struct cdn_dp_device *dp)
{
	struct device *dev = dp->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	dp->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dp->grf)) {
		dev_err(dev, "cdn-dp needs rockchip,grf property\n");
		return PTR_ERR(dp->grf);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dp->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(dp->regs)) {
		dev_err(dev, "ioremap reg failed\n");
		return PTR_ERR(dp->regs);
	}

	dp->core_clk = devm_clk_get(dev, "core-clk");
	if (IS_ERR(dp->core_clk)) {
		dev_err(dev, "cannot get core_clk_dp\n");
		return PTR_ERR(dp->core_clk);
	}

	dp->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dp->pclk)) {
		dev_err(dev, "cannot get pclk\n");
		return PTR_ERR(dp->pclk);
	}

	dp->spdif_clk = devm_clk_get(dev, "spdif");
	if (IS_ERR(dp->spdif_clk)) {
		dev_err(dev, "cannot get spdif_clk\n");
		return PTR_ERR(dp->spdif_clk);
	}

	dp->grf_clk = devm_clk_get(dev, "grf");
	if (IS_ERR(dp->grf_clk)) {
		dev_err(dev, "cannot get grf clk\n");
		return PTR_ERR(dp->grf_clk);
	}

	dp->spdif_rst = devm_reset_control_get(dev, "spdif");
	if (IS_ERR(dp->spdif_rst)) {
		dev_err(dev, "no spdif reset control found\n");
		return PTR_ERR(dp->spdif_rst);
	}

	dp->dptx_rst = devm_reset_control_get(dev, "dptx");
	if (IS_ERR(dp->dptx_rst)) {
		dev_err(dev, "no uphy reset control found\n");
		return PTR_ERR(dp->dptx_rst);
	}

	dp->apb_rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(dp->apb_rst)) {
		dev_err(dev, "no apb reset control found\n");
		return PTR_ERR(dp->apb_rst);
	}

	dp->core_rst = devm_reset_control_get(dev, "core");
	if (IS_ERR(dp->core_rst)) {
		DRM_DEV_ERROR(dev, "no core reset control found\n");
		return PTR_ERR(dp->core_rst);
	}

	dp->dpms_mode = DRM_MODE_DPMS_OFF;
	dp->fw_clk_enabled = false;

	pm_runtime_enable(dev);

	mutex_init(&dp->lock);
	wake_lock_init(&dp->wake_lock, WAKE_LOCK_SUSPEND, "cdn_dp_fb");
	return 0;
}

struct cdn_dp_device *g_dp;
static int cdn_dp_audio_hw_params(struct device *dev,  void *data,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct dp_dev *dp_dev = dev_get_drvdata(dev);
	struct cdn_dp_device *dp = dp_dev->dp;
	int ret;
	struct audio_info audio = {
		.sample_width = 16,
		.sample_rate = 44100,
		.channels = 8,
	};

	if (!cdn_dp_connector_detect(dp))
		return 0;

	switch (HDMI_I2S) {
	case HDMI_I2S:
		audio.format = AFMT_I2S;
		break;
	case HDMI_SPDIF:
		audio.format = AFMT_SPDIF;
		break;
	default:
		dev_err(dev, "%s: Invalid format %d\n", __func__, daifmt->fmt);
		return -EINVAL;
	}

	ret = cdn_dp_fb_audio_config(dp, &audio);
	if (!ret)
		dp->audio_info = audio;

	return ret;
}

static void cdn_dp_audio_shutdown(struct device *dev, void *data)
{
	struct dp_dev *dp_dev = dev_get_drvdata(dev);
	struct cdn_dp_device *dp = dp_dev->dp;
	int ret;

	if (cdn_dp_connector_detect(dp)) {
		ret = cdn_dp_fb_audio_stop(dp, &dp->audio_info);
		if (!ret)
			dp->audio_info.format = AFMT_UNUSED;
	}
}

static int cdn_dp_audio_digital_mute(struct device *dev, void *data,
				     bool enable)
{
	struct dp_dev *dp_dev = dev_get_drvdata(dev);
	struct cdn_dp_device *dp = dp_dev->dp;

	if (!cdn_dp_connector_detect(dp))
		return 0;
	return cdn_dp_fb_audio_mute(dp, enable);
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = cdn_dp_audio_hw_params,
	.audio_shutdown = cdn_dp_audio_shutdown,
	.digital_mute = cdn_dp_audio_digital_mute,
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

static int cdn_dp_get_cap_lanes(struct cdn_dp_device *dp,
				struct extcon_dev *edev)
{
	union extcon_property_value property;
	u8 lanes = 0;
	int dptx;

	if (dp->suspend)
		return 0;

	dptx = extcon_get_state(edev, EXTCON_DISP_DP);
	if (dptx > 0) {
		extcon_get_property(edev, EXTCON_DISP_DP,
				    EXTCON_PROP_USB_SS, &property);
		if (property.intval)
			lanes = 2;
		else
			lanes = 4;
	}

	return lanes;
}

static int cdn_dp_get_dpcd(struct cdn_dp_device *dp, struct cdn_dp_port *port)
{
	u8 sink_count;
	int i, ret;
	int retry = 60;

	/*
	 * Native read with retry for link status and receiver capability reads
	 * for cases where the sink may still not be ready.
	 *
	 * Sinks are *supposed* to come up within 1ms from an off state, but
	 * some DOCKs need about 5 seconds to power up, so read the dpcd every
	 * 100ms, if can not get a good dpcd in 10 seconds, give up.
	 */
	for (i = 0; i < 100; i++) {
		ret = cdn_dp_fb_dpcd_read(dp, DP_SINK_COUNT,
				       &sink_count, 1);
		if (!ret) {
			dev_dbg(dp->dev, "get dpcd success!\n");

			sink_count = DP_GET_SINK_COUNT(sink_count);
			if (!sink_count) {
				if (retry-- <= 0) {
					dev_err(dp->dev, "sink cout is 0, no sink device!\n");
					return -ENODEV;
				}
				msleep(50);
				continue;
			}

			ret = cdn_dp_fb_dpcd_read(dp, 0x000, dp->dpcd,
					       DP_RECEIVER_CAP_SIZE);
			if (ret)
				continue;

			return ret;
		} else if (!extcon_get_state(port->extcon, EXTCON_DISP_DP)) {
			break;
		}

		msleep(100);
	}

	dev_err(dp->dev, "get dpcd failed!\n");

	return -ETIMEDOUT;
}

static void cdn_dp_enter_standy(struct cdn_dp_device *dp,
				struct cdn_dp_port *port)
{
	int i, ret;

	if (port->phy_status) {
		ret = phy_power_off(port->phy);
		if (ret) {
			dev_err(dp->dev, "phy power off failed: %d", ret);
			return;
		}
	}

	port->phy_status = false;
	port->cap_lanes = 0;
	for (i = 0; i < dp->ports; i++)
		if (dp->port[i]->phy_status)
			return;

	memset(dp->dpcd, 0, DP_RECEIVER_CAP_SIZE);
	if (dp->fw_actived)
		cdn_dp_fb_set_firmware_active(dp, false);
	if (dp->fw_clk_enabled) {
		cdn_dp_clk_disable(dp);
		dp->fw_clk_enabled = false;
	}
	dp->hpd_status = connector_status_disconnected;

	hpd_change(dp->dev, 0);
}

static int cdn_dp_start_work(struct cdn_dp_device *dp,
			     struct cdn_dp_port *port,
			     u8 cap_lanes)
{
	union extcon_property_value property;
	int ret;

	if (!dp->fw_loaded) {
		ret = request_firmware(&dp->fw, CDN_DP_FIRMWARE, dp->dev);
		if (ret) {
			if (ret == -ENOENT && dp->fw_wait <= MAX_FW_WAIT_SECS) {
				unsigned long time = msecs_to_jiffies(dp->fw_wait * HZ);

				/*
				 * Keep trying to load the firmware for up to 1 minute,
				 * if can not find the file.
				 */
				schedule_delayed_work(&port->event_wq, time);
				dp->fw_wait *= 2;
			} else {
				dev_err(dp->dev, "failed to request firmware: %d\n",
					ret);
			}

			return ret;
		} else
			dp->fw_loaded = true;
	}

	ret = cdn_dp_clk_enable(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to enable clock for dp: %d\n", ret);
		return ret;
	}

	ret = phy_power_on(port->phy);
	if (ret) {
		dev_err(dp->dev, "phy power on failed: %d\n", ret);
		goto err_phy;
	}

	port->phy_status = true;

	ret = cdn_dp_firmware_init(dp);
	if (ret) {
		dev_err(dp->dev, "firmware init failed: %d", ret);
		goto err_firmware;
	}

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON26,
			       DPTX_HPD_SEL_MASK | DPTX_HPD_SEL);
	if (ret)
		goto err_grf;

	ret = cdn_dp_fb_get_hpd_status(dp);
	if (ret <= 0) {
		if (!ret)
			dev_err(dp->dev, "hpd does not exist\n");
		goto err_hpd;
	}

	ret = extcon_get_property(port->extcon, EXTCON_DISP_DP,
				  EXTCON_PROP_USB_TYPEC_POLARITY, &property);
	if (ret) {
		dev_err(dp->dev, "get property failed\n");
		goto err_hpd;
	}

	ret = cdn_dp_fb_set_host_cap(dp, cap_lanes, property.intval);
	if (ret) {
		dev_err(dp->dev, "set host capabilities failed: %d\n", ret);
		goto err_hpd;
	}

	ret = cdn_dp_get_dpcd(dp, port);
	if (ret)
		goto err_hpd;

	return 0;

err_hpd:
	cdn_dp_grf_write(dp, GRF_SOC_CON26,
			 DPTX_HPD_SEL_MASK | DPTX_HPD_DEL);

err_grf:
	if (dp->fw_actived)
		cdn_dp_fb_set_firmware_active(dp, false);

err_firmware:
	if (phy_power_off(port->phy))
		dev_err(dp->dev, "phy power off failed: %d", ret);
	else
		port->phy_status = false;

err_phy:
	cdn_dp_clk_disable(dp);
	dp->fw_clk_enabled = false;
	return ret;
}

static int cdn_dp_pd_event(struct notifier_block *nb,
			   unsigned long event, void *priv)
{
	struct cdn_dp_port *port;

	port = container_of(nb, struct cdn_dp_port, event_nb);
	schedule_delayed_work(&port->event_wq, 0);
	return 0;
}

static void cdn_dp_pd_event_wq(struct work_struct *work)
{
	struct cdn_dp_port *port = container_of(work, struct cdn_dp_port,
						event_wq.work);
	struct cdn_dp_device *dp = port->dp;
	u8 new_cap_lanes, sink_count, i;
	int ret;

	mutex_lock(&dp->lock);
	wake_lock_timeout(&dp->wake_lock, msecs_to_jiffies(1000));

	new_cap_lanes = cdn_dp_get_cap_lanes(dp, port->extcon);

	if (new_cap_lanes == port->cap_lanes) {
		if (!new_cap_lanes) {
			dev_err(dp->dev, "dp lanes is 0, and same with last time\n");
			goto out;
		}

		/*
		 * If HPD interrupt is triggered, and cable states is still
		 * attached, that means something on the Type-C Dock/Dongle
		 * changed, check the sink count by DPCD. If sink count became
		 * 0, this port phy can be powered off; if the sink count does
		 * not change and dp is connected, don't do anything, because
		 * dp video output maybe ongoing. if dp is not connected, that
		 * means something is wrong, we don't do anything here, just
		 * output error log.
		 */
		cdn_dp_fb_dpcd_read(dp, DP_SINK_COUNT, &sink_count, 1);
		if (sink_count) {
			if (dp->hpd_status == connector_status_connected)
				dev_info(dp->dev,
					 "hpd interrupt is triggered when dp has been already connected\n");
			else
				dev_err(dp->dev,
					"something is wrong, hpd is triggered before dp is connected\n");

			goto out;
		} else {
			new_cap_lanes = 0;
		}
	}

	if (dp->hpd_status == connector_status_connected && new_cap_lanes) {
		dev_err(dp->dev, "error, dp connector has already been connected\n");
		goto out;
	}

	if (!new_cap_lanes) {
		dev_info(dp->dev, "dp lanes is 0, enter standby\n");
		cdn_dp_enter_standy(dp, port);
		goto out;
	}

	/* if other phy is running, do not do anything, just return */
	for (i = 0; i < dp->ports; i++) {
		if (dp->port[i]->phy_status) {
			dev_warn(dp->dev, "busy, phy[%d] is running",
				 dp->port[i]->id);
			goto out;
		}
	}

	ret = cdn_dp_start_work(dp, port, new_cap_lanes);
	if (ret) {
		dev_err(dp->dev, "dp failed to connect ,error = %d\n", ret);
		goto out;
	}
	port->cap_lanes = new_cap_lanes;
	dp->hpd_status = connector_status_connected;
	wake_unlock(&dp->wake_lock);
	mutex_unlock(&dp->lock);
	hpd_change(dp->dev, new_cap_lanes);

	return;
out:
	wake_unlock(&dp->wake_lock);
	mutex_unlock(&dp->lock);
}

static int cdn_dp_bind(struct cdn_dp_device *dp)
{
	struct cdn_dp_port *port;
	int ret, i;

	ret = cdn_dp_init(dp);
	if (ret < 0)
		return ret;

	dp->hpd_status = connector_status_disconnected;
	dp->fw_wait = 1;
	cdn_dp_audio_codec_init(dp, dp->dev);

	for (i = 0; i < dp->ports; i++) {
		port = dp->port[i];

		port->event_nb.notifier_call = cdn_dp_pd_event;
		INIT_DELAYED_WORK(&port->event_wq, cdn_dp_pd_event_wq);
		ret = extcon_register_notifier(port->extcon, EXTCON_DISP_DP,
					       &port->event_nb);
		if (ret) {
			dev_err(dp->dev, "regitster EXTCON_DISP_DP notifier err\n");
			return ret;
		}

		if (extcon_get_state(port->extcon, EXTCON_DISP_DP))
			schedule_delayed_work(&port->event_wq,
							msecs_to_jiffies(2000));
	}

	return 0;
}

int cdn_dp_fb_suspend(void *dp_dev)
{
	struct cdn_dp_device *dp = dp_dev;
	struct cdn_dp_port *port;
	int i;

	for (i = 0; i < dp->ports; i++) {
		port = dp->port[i];
		if (port->phy_status) {
			cdn_dp_fb_dpcd_write(dp, DP_SET_POWER, DP_SET_POWER_D3);
			cdn_dp_enter_standy(dp, port);
		}
	}

	/*
	 * if dp has been suspended, need to download firmware
	 * and set fw clk again.
	 */
	dp->fw_clk_enabled = false;
	dp->fw_loaded = false;
	dp->suspend = true;
	return 0;
}

int cdn_dp_fb_resume(void *dp_dev)
{
	struct cdn_dp_device *dp = dp_dev;
	struct cdn_dp_port *port;
	int i;
	if (dp->suspend) {
		dp->suspend = false;
		for (i = 0; i < dp->ports; i++) {
			port = dp->port[i];
			schedule_delayed_work(&port->event_wq, 0);
			flush_delayed_work(&port->event_wq);
		}
	}

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
	int i, ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;
	dp->dev = dev;
	g_dp = dp;

	match = of_match_node(cdn_dp_dt_ids, pdev->dev.of_node);
	dp_data = (struct cdn_dp_data *)match->data;

	for (i = 0; i < dp_data->max_phy; i++) {
		extcon = extcon_get_edev_by_phandle(dev, i);
		phy = devm_of_phy_get_by_index(dev, dev->of_node, i);

		if (PTR_ERR(extcon) == -EPROBE_DEFER ||
		    PTR_ERR(phy) == -EPROBE_DEFER){
			/* don't exit if there already has one port */
			if(dp->ports)
				continue;
			return -EPROBE_DEFER;

		}

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
		dev_err(dev, "missing extcon or phy\n");
		return -EINVAL;
	}

	cdn_dp_bind(dp);
	ret = cdn_dp_fb_register(pdev, dp);

	return ret;
}

static struct platform_driver cdn_dp_driver = {
	.probe = cdn_dp_probe,
	.driver = {
		   .name = "cdn-dp-fb",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(cdn_dp_dt_ids),
	},
};

module_platform_driver(cdn_dp_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_DESCRIPTION("cdn DP Driver");
MODULE_LICENSE("GPL v2");
