// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/display/drm_hdmi_state_helper.h>

#include <linux/hdmi.h>

#include <sound/hdmi-codec.h>

#include "hdmi.h"

/* Supported HDMI Audio sample rates */
#define MSM_HDMI_SAMPLE_RATE_32KHZ		0
#define MSM_HDMI_SAMPLE_RATE_44_1KHZ		1
#define MSM_HDMI_SAMPLE_RATE_48KHZ		2
#define MSM_HDMI_SAMPLE_RATE_88_2KHZ		3
#define MSM_HDMI_SAMPLE_RATE_96KHZ		4
#define MSM_HDMI_SAMPLE_RATE_176_4KHZ		5
#define MSM_HDMI_SAMPLE_RATE_192KHZ		6
#define MSM_HDMI_SAMPLE_RATE_MAX		7


struct hdmi_msm_audio_acr {
	uint32_t n;	/* N parameter for clock regeneration */
	uint32_t cts;	/* CTS parameter for clock regeneration */
};

struct hdmi_msm_audio_arcs {
	unsigned long int pixclock;
	struct hdmi_msm_audio_acr lut[MSM_HDMI_SAMPLE_RATE_MAX];
};

#define HDMI_MSM_AUDIO_ARCS(pclk, ...) { (1000 * (pclk)), __VA_ARGS__ }

/* Audio constants lookup table for hdmi_msm_audio_acr_setup */
/* Valid Pixel-Clock rates: 25.2MHz, 27MHz, 27.03MHz, 74.25MHz, 148.5MHz */
static const struct hdmi_msm_audio_arcs acr_lut[] = {
	/*  25.200MHz  */
	HDMI_MSM_AUDIO_ARCS(25200, {
		{4096, 25200}, {6272, 28000}, {6144, 25200}, {12544, 28000},
		{12288, 25200}, {25088, 28000}, {24576, 25200} }),
	/*  27.000MHz  */
	HDMI_MSM_AUDIO_ARCS(27000, {
		{4096, 27000}, {6272, 30000}, {6144, 27000}, {12544, 30000},
		{12288, 27000}, {25088, 30000}, {24576, 27000} }),
	/*  27.027MHz */
	HDMI_MSM_AUDIO_ARCS(27030, {
		{4096, 27027}, {6272, 30030}, {6144, 27027}, {12544, 30030},
		{12288, 27027}, {25088, 30030}, {24576, 27027} }),
	/*  74.250MHz */
	HDMI_MSM_AUDIO_ARCS(74250, {
		{4096, 74250}, {6272, 82500}, {6144, 74250}, {12544, 82500},
		{12288, 74250}, {25088, 82500}, {24576, 74250} }),
	/* 148.500MHz */
	HDMI_MSM_AUDIO_ARCS(148500, {
		{4096, 148500}, {6272, 165000}, {6144, 148500}, {12544, 165000},
		{12288, 148500}, {25088, 165000}, {24576, 148500} }),
};

static const struct hdmi_msm_audio_arcs *get_arcs(unsigned long int pixclock)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(acr_lut); i++) {
		const struct hdmi_msm_audio_arcs *arcs = &acr_lut[i];
		if (arcs->pixclock == pixclock)
			return arcs;
	}

	return NULL;
}

int msm_hdmi_audio_update(struct hdmi *hdmi)
{
	struct hdmi_audio *audio = &hdmi->audio;
	const struct hdmi_msm_audio_arcs *arcs = NULL;
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

	if (enabled) {
		arcs = get_arcs(hdmi->pixclock);
		if (!arcs) {
			DBG("disabling audio: unsupported pixclock: %lu",
					hdmi->pixclock);
			enabled = false;
		}
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

		n   = arcs->lut[audio->rate].n;
		cts = arcs->lut[audio->rate].cts;

		if ((MSM_HDMI_SAMPLE_RATE_192KHZ == audio->rate) ||
				(MSM_HDMI_SAMPLE_RATE_176_4KHZ == audio->rate)) {
			multiplier = 4;
			n >>= 2; /* divide N by 4 and use multiplier */
		} else if ((MSM_HDMI_SAMPLE_RATE_96KHZ == audio->rate) ||
				(MSM_HDMI_SAMPLE_RATE_88_2KHZ == audio->rate)) {
			multiplier = 2;
			n >>= 1; /* divide N by 2 and use multiplier */
		} else {
			multiplier = 1;
		}

		DBG("n=%u, cts=%u, multiplier=%u", n, cts, multiplier);

		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_SOURCE;
		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_AUDIO_PRIORITY;
		acr_pkt_ctrl |= HDMI_ACR_PKT_CTRL_N_MULTIPLIER(multiplier);

		if ((MSM_HDMI_SAMPLE_RATE_48KHZ == audio->rate) ||
				(MSM_HDMI_SAMPLE_RATE_96KHZ == audio->rate) ||
				(MSM_HDMI_SAMPLE_RATE_192KHZ == audio->rate))
			select = ACR_48;
		else if ((MSM_HDMI_SAMPLE_RATE_44_1KHZ == audio->rate) ||
				(MSM_HDMI_SAMPLE_RATE_88_2KHZ == audio->rate) ||
				(MSM_HDMI_SAMPLE_RATE_176_4KHZ == audio->rate))
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

int msm_hdmi_bridge_audio_prepare(struct drm_connector *connector,
				  struct drm_bridge *bridge,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	unsigned int rate;
	int ret;

	drm_dbg_driver(bridge->dev, "%u Hz, %d bit, %d channels\n",
		       params->sample_rate,
		       params->sample_width,
		       params->cea.channels);

	switch (params->sample_rate) {
	case 32000:
		rate = MSM_HDMI_SAMPLE_RATE_32KHZ;
		break;
	case 44100:
		rate = MSM_HDMI_SAMPLE_RATE_44_1KHZ;
		break;
	case 48000:
		rate = MSM_HDMI_SAMPLE_RATE_48KHZ;
		break;
	case 88200:
		rate = MSM_HDMI_SAMPLE_RATE_88_2KHZ;
		break;
	case 96000:
		rate = MSM_HDMI_SAMPLE_RATE_96KHZ;
		break;
	case 176400:
		rate = MSM_HDMI_SAMPLE_RATE_176_4KHZ;
		break;
	case 192000:
		rate = MSM_HDMI_SAMPLE_RATE_192KHZ;
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

	hdmi->audio.rate = rate;
	hdmi->audio.channels = params->cea.channels;
	hdmi->audio.enabled = true;

	return msm_hdmi_audio_update(hdmi);
}

void msm_hdmi_bridge_audio_shutdown(struct drm_connector *connector,
				    struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;

	drm_atomic_helper_connector_hdmi_clear_audio_infoframe(connector);

	hdmi->audio.rate = 0;
	hdmi->audio.channels = 2;
	hdmi->audio.enabled = false;

	msm_hdmi_audio_update(hdmi);
}
