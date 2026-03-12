// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <drm/drm_modes.h>
#include <linux/device.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/math.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <sound/hdmi-codec.h>

#include "mtk_hdmi_common.h"

struct hdmi_acr_n {
	unsigned int clock;
	unsigned int n[3];
};

/* Recommended N values from HDMI specification, tables 7-1 to 7-3 */
static const struct hdmi_acr_n hdmi_rec_n_table[] = {
	/* Clock, N: 32kHz 44.1kHz 48kHz */
	{  25175, {  4576,  7007,  6864 } },
	{  74176, { 11648, 17836, 11648 } },
	{ 148352, { 11648,  8918,  5824 } },
	{ 296703, {  5824,  4459,  5824 } },
	{ 297000, {  3072,  4704,  5120 } },
	{      0, {  4096,  6272,  6144 } }, /* all other TMDS clocks */
};

/**
 * hdmi_recommended_n() - Return N value recommended by HDMI specification
 * @freq: audio sample rate in Hz
 * @clock: rounded TMDS clock in kHz
 */
static unsigned int hdmi_recommended_n(unsigned int freq, unsigned int clock)
{
	const struct hdmi_acr_n *recommended;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_rec_n_table) - 1; i++) {
		if (clock == hdmi_rec_n_table[i].clock)
			break;
	}
	recommended = hdmi_rec_n_table + i;

	switch (freq) {
	case 32000:
		return recommended->n[0];
	case 44100:
		return recommended->n[1];
	case 48000:
		return recommended->n[2];
	case 88200:
		return recommended->n[1] * 2;
	case 96000:
		return recommended->n[2] * 2;
	case 176400:
		return recommended->n[1] * 4;
	case 192000:
		return recommended->n[2] * 4;
	default:
		return (128 * freq) / 1000;
	}
}

static unsigned int hdmi_mode_clock_to_hz(unsigned int clock)
{
	switch (clock) {
	case 25175:
		return 25174825;	/* 25.2/1.001 MHz */
	case 74176:
		return 74175824;	/* 74.25/1.001 MHz */
	case 148352:
		return 148351648;	/* 148.5/1.001 MHz */
	case 296703:
		return 296703297;	/* 297/1.001 MHz */
	default:
		return clock * 1000;
	}
}

static unsigned int hdmi_expected_cts(unsigned int audio_sample_rate,
				      unsigned int tmds_clock, unsigned int n)
{
	return DIV_ROUND_CLOSEST_ULL((u64)hdmi_mode_clock_to_hz(tmds_clock) * n,
				     128 * audio_sample_rate);
}

void mtk_hdmi_get_ncts(unsigned int sample_rate, unsigned int clock,
		       unsigned int *n, unsigned int *cts)
{
	*n = hdmi_recommended_n(sample_rate, clock);
	*cts = hdmi_expected_cts(sample_rate, clock, *n);
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_get_ncts, "DRM_MTK_HDMI");

int mtk_hdmi_audio_params(struct mtk_hdmi *hdmi,
			  struct hdmi_codec_daifmt *daifmt,
			  struct hdmi_codec_params *params)
{
	struct hdmi_audio_param aud_params = { 0 };
	unsigned int chan = params->cea.channels;

	dev_dbg(hdmi->dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
		params->sample_rate, params->sample_width, chan);

	if (!hdmi->bridge.encoder)
		return -ENODEV;

	switch (chan) {
	case 2:
		aud_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_2_0;
		break;
	case 4:
		aud_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_4_0;
		break;
	case 6:
		aud_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_5_1;
		break;
	case 8:
		aud_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_7_1;
		break;
	default:
		dev_err(hdmi->dev, "channel[%d] not supported!\n", chan);
		return -EINVAL;
	}

	switch (params->sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		break;
	default:
		dev_err(hdmi->dev, "rate[%d] not supported!\n",
			params->sample_rate);
		return -EINVAL;
	}

	switch (daifmt->fmt) {
	case HDMI_I2S:
		aud_params.aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
		aud_params.aud_sample_size = HDMI_AUDIO_SAMPLE_SIZE_16;
		aud_params.aud_input_type = HDMI_AUD_INPUT_I2S;
		aud_params.aud_i2s_fmt = HDMI_I2S_MODE_I2S_24BIT;
		aud_params.aud_mclk = HDMI_AUD_MCLK_128FS;
		break;
	case HDMI_SPDIF:
		aud_params.aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
		aud_params.aud_sample_size = HDMI_AUDIO_SAMPLE_SIZE_16;
		aud_params.aud_input_type = HDMI_AUD_INPUT_SPDIF;
		break;
	default:
		dev_err(hdmi->dev, "%s: Invalid DAI format %d\n", __func__,
			daifmt->fmt);
		return -EINVAL;
	}
	memcpy(&aud_params.codec_params, params, sizeof(aud_params.codec_params));
	memcpy(&hdmi->aud_param, &aud_params, sizeof(aud_params));

	dev_dbg(hdmi->dev, "codec:%d, input:%d, channel:%d, fs:%d\n",
		aud_params.aud_codec, aud_params.aud_input_type,
		aud_params.aud_input_chan_type, aud_params.codec_params.sample_rate);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_audio_params, "DRM_MTK_HDMI");

int mtk_hdmi_audio_get_eld(struct device *dev, void *data, uint8_t *buf, size_t len)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi->enabled)
		memcpy(buf, hdmi->curr_conn->eld, min(sizeof(hdmi->curr_conn->eld), len));
	else
		memset(buf, 0, len);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_audio_get_eld, "DRM_MTK_HDMI");

void mtk_hdmi_audio_set_plugged_cb(struct mtk_hdmi *hdmi, hdmi_codec_plugged_cb fn,
				   struct device *codec_dev)
{
	mutex_lock(&hdmi->update_plugged_status_lock);
	hdmi->plugged_cb = fn;
	hdmi->codec_dev = codec_dev;
	mutex_unlock(&hdmi->update_plugged_status_lock);
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_audio_set_plugged_cb, "DRM_MTK_HDMI");

static int mtk_hdmi_get_all_clk(struct mtk_hdmi *hdmi, struct device_node *np,
				const char * const *clock_names, size_t num_clocks)
{
	int i;

	for (i = 0; i < num_clocks; i++) {
		hdmi->clk[i] = of_clk_get_by_name(np, clock_names[i]);

		if (IS_ERR(hdmi->clk[i]))
			return PTR_ERR(hdmi->clk[i]);
	}

	return 0;
}

bool mtk_hdmi_bridge_mode_fixup(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_bridge_mode_fixup, "DRM_MTK_HDMI");

void mtk_hdmi_bridge_mode_set(struct drm_bridge *bridge,
			      const struct drm_display_mode *mode,
			      const struct drm_display_mode *adjusted_mode)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	dev_dbg(hdmi->dev, "cur info: name:%s, hdisplay:%d\n",
		adjusted_mode->name, adjusted_mode->hdisplay);
	dev_dbg(hdmi->dev, "hsync_start:%d,hsync_end:%d, htotal:%d",
		adjusted_mode->hsync_start, adjusted_mode->hsync_end,
		adjusted_mode->htotal);
	dev_dbg(hdmi->dev, "hskew:%d, vdisplay:%d\n",
		adjusted_mode->hskew, adjusted_mode->vdisplay);
	dev_dbg(hdmi->dev, "vsync_start:%d, vsync_end:%d, vtotal:%d",
		adjusted_mode->vsync_start, adjusted_mode->vsync_end,
		adjusted_mode->vtotal);
	dev_dbg(hdmi->dev, "vscan:%d, flag:%d\n",
		adjusted_mode->vscan, adjusted_mode->flags);

	drm_mode_copy(&hdmi->mode, adjusted_mode);
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_bridge_mode_set, "DRM_MTK_HDMI");

static void mtk_hdmi_put_device(void *_dev)
{
	struct device *dev = _dev;

	put_device(dev);
}

static int mtk_hdmi_get_cec_dev(struct mtk_hdmi *hdmi, struct device *dev, struct device_node *np)
{
	struct platform_device *cec_pdev;
	struct device_node *cec_np;
	int ret;

	/* The CEC module handles HDMI hotplug detection */
	cec_np = of_get_compatible_child(np->parent, "mediatek,mt8173-cec");
	if (!cec_np)
		return dev_err_probe(dev, -EOPNOTSUPP, "Failed to find CEC node\n");

	cec_pdev = of_find_device_by_node(cec_np);
	if (!cec_pdev) {
		dev_err(hdmi->dev, "Waiting for CEC device %pOF\n", cec_np);
		of_node_put(cec_np);
		return -EPROBE_DEFER;
	}
	of_node_put(cec_np);

	ret = devm_add_action_or_reset(dev, mtk_hdmi_put_device, &cec_pdev->dev);
	if (ret)
		return ret;

	/*
	 * The mediatek,syscon-hdmi property contains a phandle link to the
	 * MMSYS_CONFIG device and the register offset of the HDMI_SYS_CFG
	 * registers it contains.
	 */
	hdmi->sys_regmap = syscon_regmap_lookup_by_phandle_args(np, "mediatek,syscon-hdmi",
								1, &hdmi->sys_offset);
	if (IS_ERR(hdmi->sys_regmap))
		return dev_err_probe(dev, PTR_ERR(hdmi->sys_regmap),
				     "Failed to get system configuration registers\n");

	hdmi->cec_dev = &cec_pdev->dev;
	return 0;
}

static int mtk_hdmi_dt_parse_pdata(struct mtk_hdmi *hdmi, struct platform_device *pdev,
				   const char * const *clk_names, size_t num_clocks)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *remote, *i2c_np;
	int ret;

	ret = mtk_hdmi_get_all_clk(hdmi, np, clk_names, num_clocks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	hdmi->irq = platform_get_irq(pdev, 0);
	if (hdmi->irq < 0)
		return hdmi->irq;

	hdmi->regs = device_node_to_regmap(dev->of_node);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	remote = of_graph_get_remote_node(np, 1, 0);
	if (!remote)
		return -EINVAL;

	if (!of_device_is_compatible(remote, "hdmi-connector")) {
		hdmi->bridge.next_bridge = of_drm_find_and_get_bridge(remote);
		if (!hdmi->bridge.next_bridge) {
			dev_err(dev, "Waiting for external bridge\n");
			of_node_put(remote);
			return -EPROBE_DEFER;
		}
	}

	i2c_np = of_parse_phandle(remote, "ddc-i2c-bus", 0);
	of_node_put(remote);
	if (!i2c_np)
		return dev_err_probe(dev, -EINVAL, "No ddc-i2c-bus in connector\n");

	hdmi->ddc_adpt = of_find_i2c_adapter_by_node(i2c_np);
	of_node_put(i2c_np);
	if (!hdmi->ddc_adpt)
		return dev_err_probe(dev, -EPROBE_DEFER, "Failed to get ddc i2c adapter by node\n");

	ret = devm_add_action_or_reset(dev, mtk_hdmi_put_device, &hdmi->ddc_adpt->dev);
	if (ret)
		return ret;

	ret = mtk_hdmi_get_cec_dev(hdmi, dev, np);
	if (ret == -EOPNOTSUPP)
		dev_info(dev, "CEC support unavailable: node not found\n");
	else if (ret)
		return ret;

	return 0;
}

static void mtk_hdmi_unregister_audio_driver(void *data)
{
	platform_device_unregister(data);
}

static int mtk_hdmi_register_audio_driver(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	struct hdmi_audio_param *aud_param = &hdmi->aud_param;
	struct hdmi_codec_pdata codec_data = {
		.ops = hdmi->conf->ver_conf->codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
		.data = hdmi,
		.no_capture_mute = 1,
	};
	int ret;

	aud_param->aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
	aud_param->aud_sample_size = HDMI_AUDIO_SAMPLE_SIZE_16;
	aud_param->aud_input_type = HDMI_AUD_INPUT_I2S;
	aud_param->aud_i2s_fmt = HDMI_I2S_MODE_I2S_24BIT;
	aud_param->aud_mclk = HDMI_AUD_MCLK_128FS;
	aud_param->aud_input_chan_type = HDMI_AUD_CHAN_TYPE_2_0;

	hdmi->audio_pdev = platform_device_register_data(dev,
							 HDMI_CODEC_DRV_NAME,
							 PLATFORM_DEVID_AUTO,
							 &codec_data,
							 sizeof(codec_data));
	if (IS_ERR(hdmi->audio_pdev))
		return PTR_ERR(hdmi->audio_pdev);

	ret = devm_add_action_or_reset(dev, mtk_hdmi_unregister_audio_driver,
				       hdmi->audio_pdev);
	if (ret)
		return ret;

	return 0;
}

struct mtk_hdmi *mtk_hdmi_common_probe(struct platform_device *pdev)
{
	const struct mtk_hdmi_ver_conf *ver_conf;
	const struct mtk_hdmi_conf *hdmi_conf;
	struct device *dev = &pdev->dev;
	struct mtk_hdmi *hdmi;
	int ret;

	hdmi_conf = of_device_get_match_data(dev);
	if (!hdmi_conf)
		return ERR_PTR(-ENODEV);

	ver_conf = hdmi_conf->ver_conf;

	hdmi = devm_drm_bridge_alloc(dev, struct mtk_hdmi, bridge,
					ver_conf->bridge_funcs);
	if (IS_ERR(hdmi))
		return hdmi;

	hdmi->dev = dev;
	hdmi->conf = hdmi_conf;

	hdmi->clk = devm_kcalloc(dev, ver_conf->num_clocks, sizeof(*hdmi->clk), GFP_KERNEL);
	if (!hdmi->clk)
		return ERR_PTR(-ENOMEM);

	ret = mtk_hdmi_dt_parse_pdata(hdmi, pdev, ver_conf->mtk_hdmi_clock_names,
				      ver_conf->num_clocks);
	if (ret)
		return ERR_PTR(ret);

	hdmi->phy = devm_phy_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy))
		return dev_err_cast_probe(dev, hdmi->phy, "Failed to get HDMI PHY\n");

	mutex_init(&hdmi->update_plugged_status_lock);
	platform_set_drvdata(pdev, hdmi);

	ret = mtk_hdmi_register_audio_driver(dev);
	if (ret)
		return dev_err_ptr_probe(dev, ret, "Cannot register HDMI Audio driver\n");

	hdmi->bridge.of_node = pdev->dev.of_node;
	hdmi->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID
			 | DRM_BRIDGE_OP_HPD;

	/* Only v2 support OP_HDMI now and it we know that it also support SPD */
	if (ver_conf->bridge_funcs->hdmi_write_avi_infoframe &&
	    ver_conf->bridge_funcs->hdmi_clear_avi_infoframe)
		hdmi->bridge.ops |= DRM_BRIDGE_OP_HDMI |
				    DRM_BRIDGE_OP_HDMI_SPD_INFOFRAME;

	hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->bridge.ddc = hdmi->ddc_adpt;
	hdmi->bridge.vendor = "MediaTek";
	hdmi->bridge.product = "On-Chip HDMI";
	hdmi->bridge.interlace_allowed = ver_conf->interlace_allowed;

	ret = devm_drm_bridge_add(dev, &hdmi->bridge);
	if (ret)
		return dev_err_ptr_probe(dev, ret, "Failed to add bridge\n");

	return hdmi;
}
EXPORT_SYMBOL_NS_GPL(mtk_hdmi_common_probe, "DRM_MTK_HDMI");

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek HDMI Common Library");
MODULE_LICENSE("GPL");
