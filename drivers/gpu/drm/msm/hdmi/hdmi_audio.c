// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

#include <linux/hdmi.h>

#include <sound/hdmi-codec.h>

#include "hdmi.h"

int msm_hdmi_audio_update(struct hdmi *hdmi)
{
	struct hdmi_audio *audio = &hdmi->audio;
	bool enabled = audio->enabled;
	uint32_t acr_pkt_ctrl, vbi_pkt_ctrl, aud_pkt_ctrl;
	uint32_t audio_config;

	if (!hdmi->connector->display_info.is_hdmi)
		return -EINVAL;

	DBG("audio: enabled=%d, channels=%d, rate=%d",
	    audio->enabled, audio->channels, audio->rate);

	DBG("video: power_on=%d, pixclock=%lu", hdmi->power_on, hdmi->pixclock);

	if (enabled && !(hdmi->power_on && hdmi->pixclock)) {
		DBG("disabling audio: no video");
		enabled = false;
	}

	/* Read first before writing */
	acr_pkt_ctrl = hdmi_read(hdmi, REG_HDMI_ACR_PKT_CTRL);
	vbi_pkt_ctrl = hdmi_read(hdmi, REG_HDMI_VBI_PKT_CTRL);
	aud_pkt_ctrl = hdmi_read(hdmi, REG_HDMI_AUDIO_PKT_CTRL1);
	audio_config = hdmi_read(hdmi, REG_HDMI_AUDIO_CFG);

	/* Clear N/CTS selection bits */
	acr_pkt_ctrl &= ~HDMI_ACR_PKT_CTRL_SELECT__MASK;

	if (enabled) {
		uint32_t n, cts, multiplier;
		enum hdmi_acr_cts select;

		drm_hdmi_acr_get_n_cts(hdmi->pixclock, audio->rate, &n, &cts);

		if (audio->rate == 192000 || audio->rate == 176400) {
			multiplier = 4;
			n >>= 2; /* divide N by 4 and use multiplier */
		} else if (audio->rate == 96000 || audio->rate == 88200) {
			multiplier = 2;
			n >>= 1; /* divide N by 2 and use multiplier */
		} else {
			multiplier = 1;
		}

		DBG("n=%u, cts=%u, multiplier=%u", n, cts, multiplier);

		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_SOURCE;
		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_AUDIO_PRIORITY;
		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_N_MULTIPLIER(multiplier);

		if (audio->rate == 48000 || audio->rate == 96000 ||
		    audio->rate == 192000)
			select = ACR_48;
		else if (audio->rate == 44100 || audio->rate == 88200 ||
			 audio->rate == 176400)
			select = ACR_44;
		else /* default to 32k */
			select = ACR_32;

		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_SELECT(select);

		hdmi_write(hdmi, REG_HDMI_ACR_0(select - 1),
				HDMI_ACR_0_CTS(cts));
		hdmi_write(hdmi, REG_HDMI_ACR_1(select - 1),
				HDMI_ACR_1_N(n));

		hdmi_write(hdmi, REG_HDMI_AUDIO_PKT_CTRL2,
				COND(audio->channels != 2, HDMI_AUDIO_PKT_CTRL2_LAYOUT) |
				HDMI_AUDIO_PKT_CTRL2_OVERRIDE);

		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_CONT;
		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_SEND;

		hdmi_write(hdmi, REG_HDMI_GC, 0);

		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_GC_ENABLE;
		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_GC_EVERY_FRAME;

		aud_pkt_ctrl |= HDMI_AUDIO_PKT_CTRL1_AUDIO_SAMPLE_SEND;

		audio_config &= ~HDMI_AUDIO_CFG_FIFO_WATERMARK__MASK;
		audio_config |= HDMI_AUDIO_CFG_FIFO_WATERMARK(4);
		audio_config |= HDMI_AUDIO_CFG_ENGINE_ENABLE;
	} else {
		acr_pkt_ctrl &= ~HDMI_ACR_PKT_CTRL_CONT;
		acr_pkt_ctrl &= ~HDMI_ACR_PKT_CTRL_SEND;
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_GC_ENABLE;
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_GC_EVERY_FRAME;
		aud_pkt_ctrl &= ~HDMI_AUDIO_PKT_CTRL1_AUDIO_SAMPLE_SEND;
		audio_config &= ~HDMI_AUDIO_CFG_ENGINE_ENABLE;
	}

	hdmi_write(hdmi, REG_HDMI_ACR_PKT_CTRL, acr_pkt_ctrl);
	hdmi_write(hdmi, REG_HDMI_VBI_PKT_CTRL, vbi_pkt_ctrl);
	hdmi_write(hdmi, REG_HDMI_AUDIO_PKT_CTRL1, aud_pkt_ctrl);

	hdmi_write(hdmi, REG_HDMI_AUD_INT,
			COND(enabled, HDMI_AUD_INT_AUD_FIFO_URUN_INT) |
			COND(enabled, HDMI_AUD_INT_AUD_SAM_DROP_INT));

	hdmi_write(hdmi, REG_HDMI_AUDIO_CFG, audio_config);


	DBG("audio %sabled", enabled ? "en" : "dis");

	return 0;
}

int msm_hdmi_bridge_audio_prepare(struct drm_bridge *bridge,
				  struct drm_connector *connector,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	int ret;

	drm_dbg_driver(bridge->dev, "%u Hz, %d bit, %d channels\n",
		       params->sample_rate,
		       params->sample_width,
		       params->cea.channels);

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
		drm_err(bridge->dev, "rate[%d] not supported!\n",
			params->sample_rate);
		return -EINVAL;
	}

	ret = drm_atomic_helper_connector_hdmi_update_audio_infoframe(connector,
								      &params->cea);
	if (ret)
		return ret;

	hdmi->audio.rate = params->sample_rate;
	hdmi->audio.channels = params->cea.channels;
	hdmi->audio.enabled = true;

	return msm_hdmi_audio_update(hdmi);
}

void msm_hdmi_bridge_audio_shutdown(struct drm_bridge *bridge,
				    struct drm_connector *connector)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;

	drm_atomic_helper_connector_hdmi_clear_audio_infoframe(connector);

	hdmi->audio.rate = 0;
	hdmi->audio.channels = 2;
	hdmi->audio.enabled = false;

	msm_hdmi_audio_update(hdmi);
}
