// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * dw-hdmi-gp-audio.c
 *
 * Copyright 2020-2022 NXP
 */
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_connector.h>

#include <sound/hdmi-codec.h>
#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_drm_eld.h>
#include <sound/pcm_iec958.h>
#include <sound/dmaengine_pcm.h>

#include "dw-hdmi-audio.h"

#define DRIVER_NAME "dw-hdmi-gp-audio"
#define DRV_NAME    "hdmi-gp-audio"

struct snd_dw_hdmi {
	struct dw_hdmi_audio_data data;
	struct platform_device  *audio_pdev;
	unsigned int pos;
};

struct dw_hdmi_channel_conf {
	u8 conf1;
	u8 ca;
};

/*
 * The default mapping of ALSA channels to HDMI channels and speaker
 * allocation bits.  Note that we can't do channel remapping here -
 * channels must be in the same order.
 *
 * Mappings for alsa-lib pcm/surround*.conf files:
 *
 *		Front	Sur4.0	Sur4.1	Sur5.0	Sur5.1	Sur7.1
 * Channels	2	4	6	6	6	8
 *
 * Our mapping from ALSA channel to CEA686D speaker name and HDMI channel:
 *
 *				Number of ALSA channels
 * ALSA Channel	2	3	4	5	6	7	8
 * 0		FL:0	=	=	=	=	=	=
 * 1		FR:1	=	=	=	=	=	=
 * 2			FC:3	RL:4	LFE:2	=	=	=
 * 3				RR:5	RL:4	FC:3	=	=
 * 4					RR:5	RL:4	=	=
 * 5						RR:5	=	=
 * 6							RC:6	=
 * 7							RLC/FRC	RLC/FRC
 */
static struct dw_hdmi_channel_conf default_hdmi_channel_config[7] = {
	{ 0x03, 0x00 },	/* FL,FR */
	{ 0x0b, 0x02 },	/* FL,FR,FC */
	{ 0x33, 0x08 },	/* FL,FR,RL,RR */
	{ 0x37, 0x09 },	/* FL,FR,LFE,RL,RR */
	{ 0x3f, 0x0b },	/* FL,FR,LFE,FC,RL,RR */
	{ 0x7f, 0x0f },	/* FL,FR,LFE,FC,RL,RR,RC */
	{ 0xff, 0x13 },	/* FL,FR,LFE,FC,RL,RR,[FR]RC,[FR]LC */
};

static int audio_hw_params(struct device *dev,  void *data,
			   struct hdmi_codec_daifmt *daifmt,
			   struct hdmi_codec_params *params)
{
	struct snd_dw_hdmi *dw = dev_get_drvdata(dev);
	u8 ca;

	dw_hdmi_set_sample_rate(dw->data.hdmi, params->sample_rate);

	ca = default_hdmi_channel_config[params->channels - 2].ca;

	dw_hdmi_set_channel_count(dw->data.hdmi, params->channels);
	dw_hdmi_set_channel_allocation(dw->data.hdmi, ca);

	dw_hdmi_set_sample_non_pcm(dw->data.hdmi,
				   params->iec.status[0] & IEC958_AES0_NONAUDIO);
	dw_hdmi_set_sample_width(dw->data.hdmi, params->sample_width);

	return 0;
}

static void audio_shutdown(struct device *dev, void *data)
{
}

static int audio_mute_stream(struct device *dev, void *data,
			     bool enable, int direction)
{
	struct snd_dw_hdmi *dw = dev_get_drvdata(dev);

	if (!enable)
		dw_hdmi_audio_enable(dw->data.hdmi);
	else
		dw_hdmi_audio_disable(dw->data.hdmi);

	return 0;
}

static int audio_get_eld(struct device *dev, void *data,
			 u8 *buf, size_t len)
{
	struct dw_hdmi_audio_data *audio = data;
	u8 *eld;

	eld = audio->get_eld(audio->hdmi);
	if (eld)
		memcpy(buf, eld, min_t(size_t, MAX_ELD_BYTES, len));
	else
		/* Pass en empty ELD if connector not available */
		memset(buf, 0, len);

	return 0;
}

static int audio_hook_plugged_cb(struct device *dev, void *data,
				 hdmi_codec_plugged_cb fn,
				 struct device *codec_dev)
{
	struct snd_dw_hdmi *dw = dev_get_drvdata(dev);

	return dw_hdmi_set_plugged_cb(dw->data.hdmi, fn, codec_dev);
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = audio_hw_params,
	.audio_shutdown = audio_shutdown,
	.mute_stream = audio_mute_stream,
	.get_eld = audio_get_eld,
	.hook_plugged_cb = audio_hook_plugged_cb,
};

static int snd_dw_hdmi_probe(struct platform_device *pdev)
{
	struct dw_hdmi_audio_data *data = pdev->dev.platform_data;
	struct snd_dw_hdmi *dw;

	const struct hdmi_codec_pdata codec_data = {
		.i2s = 1,
		.spdif = 0,
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
		.data = data,
	};

	dw = devm_kzalloc(&pdev->dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw->data = *data;

	platform_set_drvdata(pdev, dw);

	dw->audio_pdev = platform_device_register_data(&pdev->dev,
						       HDMI_CODEC_DRV_NAME, 1,
						       &codec_data,
						       sizeof(codec_data));

	return PTR_ERR_OR_ZERO(dw->audio_pdev);
}

static void snd_dw_hdmi_remove(struct platform_device *pdev)
{
	struct snd_dw_hdmi *dw = platform_get_drvdata(pdev);

	platform_device_unregister(dw->audio_pdev);
}

static struct platform_driver snd_dw_hdmi_driver = {
	.probe	= snd_dw_hdmi_probe,
	.remove_new = snd_dw_hdmi_remove,
	.driver	= {
		.name = DRIVER_NAME,
	},
};

module_platform_driver(snd_dw_hdmi_driver);

MODULE_AUTHOR("Shengjiu Wang <shengjiu.wang@nxp.com>");
MODULE_DESCRIPTION("Synopsys Designware HDMI GPA ALSA interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
