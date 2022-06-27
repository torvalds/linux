// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */


#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/of_platform.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_edid.h>

#include "dp_catalog.h"
#include "dp_audio.h"
#include "dp_panel.h"
#include "dp_display.h"

#define HEADER_BYTE_2_BIT	 0
#define PARITY_BYTE_2_BIT	 8
#define HEADER_BYTE_1_BIT	16
#define PARITY_BYTE_1_BIT	24
#define HEADER_BYTE_3_BIT	16
#define PARITY_BYTE_3_BIT	24

struct dp_audio_private {
	struct platform_device *audio_pdev;
	struct platform_device *pdev;
	struct drm_device *drm_dev;
	struct dp_catalog *catalog;
	struct dp_panel *panel;

	bool engine_on;
	u32 channels;

	struct dp_audio dp_audio;
};

static u8 dp_audio_get_g0_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[3];
	g[1] = c[0] ^ c[3];
	g[2] = c[1];
	g[3] = c[2];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

static u8 dp_audio_get_g1_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[0] ^ c[3];
	g[1] = c[0] ^ c[1] ^ c[3];
	g[2] = c[1] ^ c[2];
	g[3] = c[2] ^ c[3];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

static u8 dp_audio_calculate_parity(u32 data)
{
	u8 x0 = 0;
	u8 x1 = 0;
	u8 ci = 0;
	u8 iData = 0;
	u8 i = 0;
	u8 parity_byte;
	u8 num_byte = (data & 0xFF00) > 0 ? 8 : 2;

	for (i = 0; i < num_byte; i++) {
		iData = (data >> i*4) & 0xF;

		ci = iData ^ x1;
		x1 = x0 ^ dp_audio_get_g1_value(ci);
		x0 = dp_audio_get_g0_value(ci);
	}

	parity_byte = x1 | (x0 << 4);

	return parity_byte;
}

static u32 dp_audio_get_header(struct dp_catalog *catalog,
		enum dp_catalog_audio_sdp_type sdp,
		enum dp_catalog_audio_header_type header)
{
	catalog->sdp_type = sdp;
	catalog->sdp_header = header;
	dp_catalog_audio_get_header(catalog);

	return catalog->audio_data;
}

static void dp_audio_set_header(struct dp_catalog *catalog,
		u32 data,
		enum dp_catalog_audio_sdp_type sdp,
		enum dp_catalog_audio_header_type header)
{
	catalog->sdp_type = sdp;
	catalog->sdp_header = header;
	catalog->audio_data = data;
	dp_catalog_audio_set_header(catalog);
}

static void dp_audio_stream_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_1);

	new_value = 0x02;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_2);
	new_value = value;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);

	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_3);

	new_value = audio->channels - 1;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
		value, parity_byte);

	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_timestamp_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_1);

	new_value = 0x1;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_2);

	new_value = 0x17;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_3);

	new_value = (0x0 | (0x11 << 2));
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_infoframe_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_1);

	new_value = 0x84;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_2);

	new_value = 0x1b;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_3);

	new_value = (0x0 | (0x11 << 2));
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			new_value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_copy_management_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_1);

	new_value = 0x05;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_2);

	new_value = 0x0F;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_3);

	new_value = 0x0;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_isrc_sdp(struct dp_audio_private *audio)
{
	struct dp_catalog *catalog = audio->catalog;
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_1);

	new_value = 0x06;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(catalog,
			DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_2);

	new_value = 0x0F;
	parity_byte = dp_audio_calculate_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	drm_dbg_dp(audio->drm_dev,
			"Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(catalog, value,
		DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_2);
}

static void dp_audio_setup_sdp(struct dp_audio_private *audio)
{
	dp_catalog_audio_config_sdp(audio->catalog);

	dp_audio_stream_sdp(audio);
	dp_audio_timestamp_sdp(audio);
	dp_audio_infoframe_sdp(audio);
	dp_audio_copy_management_sdp(audio);
	dp_audio_isrc_sdp(audio);
}

static void dp_audio_setup_acr(struct dp_audio_private *audio)
{
	u32 select = 0;
	struct dp_catalog *catalog = audio->catalog;

	switch (audio->dp_audio.bw_code) {
	case DP_LINK_BW_1_62:
		select = 0;
		break;
	case DP_LINK_BW_2_7:
		select = 1;
		break;
	case DP_LINK_BW_5_4:
		select = 2;
		break;
	case DP_LINK_BW_8_1:
		select = 3;
		break;
	default:
		drm_dbg_dp(audio->drm_dev, "Unknown link rate\n");
		select = 0;
		break;
	}

	catalog->audio_data = select;
	dp_catalog_audio_config_acr(catalog);
}

static void dp_audio_safe_to_exit_level(struct dp_audio_private *audio)
{
	struct dp_catalog *catalog = audio->catalog;
	u32 safe_to_exit_level = 0;

	switch (audio->dp_audio.lane_count) {
	case 1:
		safe_to_exit_level = 14;
		break;
	case 2:
		safe_to_exit_level = 8;
		break;
	case 4:
		safe_to_exit_level = 5;
		break;
	default:
		drm_dbg_dp(audio->drm_dev,
				"setting the default safe_to_exit_level = %u\n",
				safe_to_exit_level);
		safe_to_exit_level = 14;
		break;
	}

	catalog->audio_data = safe_to_exit_level;
	dp_catalog_audio_sfe_level(catalog);
}

static void dp_audio_enable(struct dp_audio_private *audio, bool enable)
{
	struct dp_catalog *catalog = audio->catalog;

	catalog->audio_data = enable;
	dp_catalog_audio_enable(catalog);

	audio->engine_on = enable;
}

static struct dp_audio_private *dp_audio_get_data(struct platform_device *pdev)
{
	struct dp_audio *dp_audio;
	struct msm_dp *dp_display;

	if (!pdev) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-ENODEV);
	}

	dp_display = platform_get_drvdata(pdev);
	if (!dp_display) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-ENODEV);
	}

	dp_audio = dp_display->dp_audio;

	if (!dp_audio) {
		DRM_ERROR("invalid dp_audio data\n");
		return ERR_PTR(-EINVAL);
	}

	return container_of(dp_audio, struct dp_audio_private, dp_audio);
}

static int dp_audio_hook_plugged_cb(struct device *dev, void *data,
		hdmi_codec_plugged_cb fn,
		struct device *codec_dev)
{

	struct platform_device *pdev;
	struct msm_dp *dp_display;

	pdev = to_platform_device(dev);
	if (!pdev) {
		pr_err("invalid input\n");
		return -ENODEV;
	}

	dp_display = platform_get_drvdata(pdev);
	if (!dp_display) {
		pr_err("invalid input\n");
		return -ENODEV;
	}

	return dp_display_set_plugged_cb(dp_display, fn, codec_dev);
}

static int dp_audio_get_eld(struct device *dev,
	void *data, uint8_t *buf, size_t len)
{
	struct platform_device *pdev;
	struct msm_dp *dp_display;

	pdev = to_platform_device(dev);

	if (!pdev) {
		DRM_ERROR("invalid input\n");
		return -ENODEV;
	}

	dp_display = platform_get_drvdata(pdev);
	if (!dp_display) {
		DRM_ERROR("invalid input\n");
		return -ENODEV;
	}

	memcpy(buf, dp_display->connector->eld,
		min(sizeof(dp_display->connector->eld), len));

	return 0;
}

int dp_audio_hw_params(struct device *dev,
	void *data,
	struct hdmi_codec_daifmt *daifmt,
	struct hdmi_codec_params *params)
{
	int rc = 0;
	struct dp_audio_private *audio;
	struct platform_device *pdev;
	struct msm_dp *dp_display;

	pdev = to_platform_device(dev);
	dp_display = platform_get_drvdata(pdev);

	/*
	 * there could be cases where sound card can be opened even
	 * before OR even when DP is not connected . This can cause
	 * unclocked access as the audio subsystem relies on the DP
	 * driver to maintain the correct state of clocks. To protect
	 * such cases check for connection status and bail out if not
	 * connected.
	 */
	if (!dp_display->power_on) {
		rc = -EINVAL;
		goto end;
	}

	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	audio->channels = params->channels;

	dp_audio_setup_sdp(audio);
	dp_audio_setup_acr(audio);
	dp_audio_safe_to_exit_level(audio);
	dp_audio_enable(audio, true);
	dp_display_signal_audio_start(dp_display);
	dp_display->audio_enabled = true;

end:
	return rc;
}

static void dp_audio_shutdown(struct device *dev, void *data)
{
	struct dp_audio_private *audio;
	struct platform_device *pdev;
	struct msm_dp *dp_display;

	pdev = to_platform_device(dev);
	dp_display = platform_get_drvdata(pdev);
	audio = dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		DRM_ERROR("failed to get audio data\n");
		return;
	}

	/*
	 * if audio was not enabled there is no need
	 * to execute the shutdown and we can bail out early.
	 * This also makes sure that we dont cause an unclocked
	 * access when audio subsystem calls this without DP being
	 * connected. is_connected cannot be used here as its set
	 * to false earlier than this call
	 */
	if (!dp_display->audio_enabled)
		return;

	dp_audio_enable(audio, false);
	/* signal the dp display to safely shutdown clocks */
	dp_display_signal_audio_complete(dp_display);
}

static const struct hdmi_codec_ops dp_audio_codec_ops = {
	.hw_params = dp_audio_hw_params,
	.audio_shutdown = dp_audio_shutdown,
	.get_eld = dp_audio_get_eld,
	.hook_plugged_cb = dp_audio_hook_plugged_cb,
};

static struct hdmi_codec_pdata codec_data = {
	.ops = &dp_audio_codec_ops,
	.max_i2s_channels = 8,
	.i2s = 1,
};

int dp_register_audio_driver(struct device *dev,
		struct dp_audio *dp_audio)
{
	struct dp_audio_private *audio_priv;

	audio_priv = container_of(dp_audio,
			struct dp_audio_private, dp_audio);

	audio_priv->audio_pdev = platform_device_register_data(dev,
						HDMI_CODEC_DRV_NAME,
						PLATFORM_DEVID_AUTO,
						&codec_data,
						sizeof(codec_data));
	return PTR_ERR_OR_ZERO(audio_priv->audio_pdev);
}

struct dp_audio *dp_audio_get(struct platform_device *pdev,
			struct dp_panel *panel,
			struct dp_catalog *catalog)
{
	int rc = 0;
	struct dp_audio_private *audio;
	struct dp_audio *dp_audio;

	if (!pdev || !panel || !catalog) {
		DRM_ERROR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	audio = devm_kzalloc(&pdev->dev, sizeof(*audio), GFP_KERNEL);
	if (!audio) {
		rc = -ENOMEM;
		goto error;
	}

	audio->pdev = pdev;
	audio->panel = panel;
	audio->catalog = catalog;

	dp_audio = &audio->dp_audio;

	dp_catalog_audio_init(catalog);

	return dp_audio;
error:
	return ERR_PTR(rc);
}

void dp_audio_put(struct dp_audio *dp_audio)
{
	struct dp_audio_private *audio;

	if (!dp_audio)
		return;

	audio = container_of(dp_audio, struct dp_audio_private, dp_audio);

	devm_kfree(&audio->pdev->dev, audio);
}
