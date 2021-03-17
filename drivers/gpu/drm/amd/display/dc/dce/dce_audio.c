/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/slab.h>

#include "reg_helper.h"
#include "dce_audio.h"
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#define DCE_AUD(audio)\
	container_of(audio, struct dce_audio, base)

#define CTX \
	aud->base.ctx

#define DC_LOGGER_INIT()

#define REG(reg)\
	(aud->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	aud->shifts->field_name, aud->masks->field_name

#define IX_REG(reg)\
	ix ## reg

#define AZ_REG_READ(reg_name) \
		read_indirect_azalia_reg(audio, IX_REG(reg_name))

#define AZ_REG_WRITE(reg_name, value) \
		write_indirect_azalia_reg(audio, IX_REG(reg_name), value)

static void write_indirect_azalia_reg(struct audio *audio,
	uint32_t reg_index,
	uint32_t reg_data)
{
	struct dce_audio *aud = DCE_AUD(audio);

	/* AZALIA_F0_CODEC_ENDPOINT_INDEX  endpoint index  */
	REG_SET(AZALIA_F0_CODEC_ENDPOINT_INDEX, 0,
			AZALIA_ENDPOINT_REG_INDEX, reg_index);

	/* AZALIA_F0_CODEC_ENDPOINT_DATA  endpoint data  */
	REG_SET(AZALIA_F0_CODEC_ENDPOINT_DATA, 0,
			AZALIA_ENDPOINT_REG_DATA, reg_data);

	DC_LOG_HW_AUDIO("AUDIO:write_indirect_azalia_reg: index: %u  data: %u\n",
		reg_index, reg_data);
}

static uint32_t read_indirect_azalia_reg(struct audio *audio, uint32_t reg_index)
{
	struct dce_audio *aud = DCE_AUD(audio);

	uint32_t value = 0;

	/* AZALIA_F0_CODEC_ENDPOINT_INDEX  endpoint index  */
	REG_SET(AZALIA_F0_CODEC_ENDPOINT_INDEX, 0,
			AZALIA_ENDPOINT_REG_INDEX, reg_index);

	/* AZALIA_F0_CODEC_ENDPOINT_DATA  endpoint data  */
	value = REG_READ(AZALIA_F0_CODEC_ENDPOINT_DATA);

	DC_LOG_HW_AUDIO("AUDIO:read_indirect_azalia_reg: index: %u  data: %u\n",
		reg_index, value);

	return value;
}

static bool is_audio_format_supported(
	const struct audio_info *audio_info,
	enum audio_format_code audio_format_code,
	uint32_t *format_index)
{
	uint32_t index;
	uint32_t max_channe_index = 0;
	bool found = false;

	if (audio_info == NULL)
		return found;

	/* pass through whole array */
	for (index = 0; index < audio_info->mode_count; index++) {
		if (audio_info->modes[index].format_code == audio_format_code) {
			if (found) {
				/* format has multiply entries, choose one with
				 *  highst number of channels */
				if (audio_info->modes[index].channel_count >
		audio_info->modes[max_channe_index].channel_count) {
					max_channe_index = index;
				}
			} else {
				/* format found, save it's index */
				found = true;
				max_channe_index = index;
			}
		}
	}

	/* return index */
	if (found && format_index != NULL)
		*format_index = max_channe_index;

	return found;
}

/*For HDMI, calculate if specified sample rates can fit into a given timing */
static void check_audio_bandwidth_hdmi(
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates)
{
	uint32_t samples;
	uint32_t  h_blank;
	bool limit_freq_to_48_khz = false;
	bool limit_freq_to_88_2_khz = false;
	bool limit_freq_to_96_khz = false;
	bool limit_freq_to_174_4_khz = false;
	if (!crtc_info)
		return;

	/* For two channels supported return whatever sink support,unmodified*/
	if (channel_count > 2) {

		/* Based on HDMI spec 1.3 Table 7.5 */
		if ((crtc_info->requested_pixel_clock_100Hz <= 270000) &&
		(crtc_info->v_active <= 576) &&
		!(crtc_info->interlaced) &&
		!(crtc_info->pixel_repetition == 2 ||
		crtc_info->pixel_repetition == 4)) {
			limit_freq_to_48_khz = true;

		} else if ((crtc_info->requested_pixel_clock_100Hz <= 270000) &&
				(crtc_info->v_active <= 576) &&
				(crtc_info->interlaced) &&
				(crtc_info->pixel_repetition == 2)) {
			limit_freq_to_88_2_khz = true;

		} else if ((crtc_info->requested_pixel_clock_100Hz <= 540000) &&
				(crtc_info->v_active <= 576) &&
				!(crtc_info->interlaced)) {
			limit_freq_to_174_4_khz = true;
		}
	}

	/* Also do some calculation for the available Audio Bandwidth for the
	 * 8 ch (i.e. for the Layout 1 => ch > 2)
	 */
	h_blank = crtc_info->h_total - crtc_info->h_active;

	if (crtc_info->pixel_repetition)
		h_blank *= crtc_info->pixel_repetition;

	/*based on HDMI spec 1.3 Table 7.5 */
	h_blank -= 58;
	/*for Control Period */
	h_blank -= 16;

	samples = h_blank * 10;
	/* Number of Audio Packets (multiplied by 10) per Line (for 8 ch number
	 * of Audio samples per line multiplied by 10 - Layout 1)
	 */
	samples /= 32;
	samples *= crtc_info->v_active;
	/*Number of samples multiplied by 10, per second */
	samples *= crtc_info->refresh_rate;
	/*Number of Audio samples per second */
	samples /= 10;

	/* @todo do it after deep color is implemented
	 * 8xx - deep color bandwidth scaling
	 * Extra bandwidth is avaliable in deep color b/c link runs faster than
	 * pixel rate. This has the effect of allowing more tmds characters to
	 * be transmitted during blank
	 */

	switch (crtc_info->color_depth) {
	case COLOR_DEPTH_888:
		samples *= 4;
		break;
	case COLOR_DEPTH_101010:
		samples *= 5;
		break;
	case COLOR_DEPTH_121212:
		samples *= 6;
		break;
	default:
		samples *= 4;
		break;
	}

	samples /= 4;

	/*check limitation*/
	if (samples < 88200)
		limit_freq_to_48_khz = true;
	else if (samples < 96000)
		limit_freq_to_88_2_khz = true;
	else if (samples < 176400)
		limit_freq_to_96_khz = true;
	else if (samples < 192000)
		limit_freq_to_174_4_khz = true;

	if (sample_rates != NULL) {
		/* limit frequencies */
		if (limit_freq_to_174_4_khz)
			sample_rates->rate.RATE_192 = 0;

		if (limit_freq_to_96_khz) {
			sample_rates->rate.RATE_192 = 0;
			sample_rates->rate.RATE_176_4 = 0;
		}
		if (limit_freq_to_88_2_khz) {
			sample_rates->rate.RATE_192 = 0;
			sample_rates->rate.RATE_176_4 = 0;
			sample_rates->rate.RATE_96 = 0;
		}
		if (limit_freq_to_48_khz) {
			sample_rates->rate.RATE_192 = 0;
			sample_rates->rate.RATE_176_4 = 0;
			sample_rates->rate.RATE_96 = 0;
			sample_rates->rate.RATE_88_2 = 0;
		}
	}
}

/*For DP SST, calculate if specified sample rates can fit into a given timing */
static void check_audio_bandwidth_dpsst(
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates)
{
	/* do nothing */
}

/*For DP MST, calculate if specified sample rates can fit into a given timing */
static void check_audio_bandwidth_dpmst(
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	union audio_sample_rates *sample_rates)
{
	/* do nothing  */
}

static void check_audio_bandwidth(
	const struct audio_crtc_info *crtc_info,
	uint32_t channel_count,
	enum signal_type signal,
	union audio_sample_rates *sample_rates)
{
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		check_audio_bandwidth_hdmi(
			crtc_info, channel_count, sample_rates);
		break;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		check_audio_bandwidth_dpsst(
			crtc_info, channel_count, sample_rates);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		check_audio_bandwidth_dpmst(
			crtc_info, channel_count, sample_rates);
		break;
	default:
		break;
	}
}

/* expose/not expose HBR capability to Audio driver */
static void set_high_bit_rate_capable(
	struct audio *audio,
	bool capable)
{
	uint32_t value = 0;

	/* set high bit rate audio capable*/
	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR);

	set_reg_field_value(value, capable,
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR,
		HBR_CAPABLE);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR, value);
}

/* set video latency in in ms/2+1 */
static void set_video_latency(
	struct audio *audio,
	int latency_in_ms)
{
	uint32_t value = 0;

	if ((latency_in_ms < 0) || (latency_in_ms > 255))
		return;

	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC);

	set_reg_field_value(value, latency_in_ms,
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		VIDEO_LIPSYNC);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		value);
}

/* set audio latency in in ms/2+1 */
static void set_audio_latency(
	struct audio *audio,
	int latency_in_ms)
{
	uint32_t value = 0;

	if (latency_in_ms < 0)
		latency_in_ms = 0;

	if (latency_in_ms > 255)
		latency_in_ms = 255;

	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC);

	set_reg_field_value(value, latency_in_ms,
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		AUDIO_LIPSYNC);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		value);
}

void dce_aud_az_enable(struct audio *audio)
{
	uint32_t value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);
	DC_LOGGER_INIT();

	set_reg_field_value(value, 1,
			    AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			    CLOCK_GATING_DISABLE);
	set_reg_field_value(value, 1,
			    AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			    AUDIO_ENABLED);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);
	set_reg_field_value(value, 0,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);

	DC_LOG_HW_AUDIO("\n\t========= AUDIO:dce_aud_az_enable: index: %u  data: 0x%x\n",
			audio->inst, value);
}

void dce_aud_az_disable(struct audio *audio)
{
	uint32_t value;
	DC_LOGGER_INIT();

	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);
	set_reg_field_value(value, 1,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);

	set_reg_field_value(value, 0,
		AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
		AUDIO_ENABLED);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);

	set_reg_field_value(value, 0,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);
	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);
	DC_LOG_HW_AUDIO("\n\t========= AUDIO:dce_aud_az_disable: index: %u  data: 0x%x\n",
			audio->inst, value);
}

void dce_aud_az_configure(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_info *audio_info)
{
	struct dce_audio *aud = DCE_AUD(audio);

	uint32_t speakers = audio_info->flags.info.ALLSPEAKERS;
	uint32_t value;
	uint32_t field = 0;
	enum audio_format_code audio_format_code;
	uint32_t format_index;
	uint32_t index;
	bool is_ac3_supported = false;
	union audio_sample_rates sample_rate;
	uint32_t strlen = 0;
	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);
	set_reg_field_value(value, 1,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);

	/* Speaker Allocation */
	/*
	uint32_t value;
	uint32_t field = 0;*/
	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER);

	set_reg_field_value(value,
		speakers,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		SPEAKER_ALLOCATION);

	/* LFE_PLAYBACK_LEVEL = LFEPBL
	 * LFEPBL = 0 : Unknown or refer to other information
	 * LFEPBL = 1 : 0dB playback
	 * LFEPBL = 2 : +10dB playback
	 * LFE_BL = 3 : Reserved
	 */
	set_reg_field_value(value,
		0,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		LFE_PLAYBACK_LEVEL);
	/* todo: according to reg spec LFE_PLAYBACK_LEVEL is read only.
	 *  why are we writing to it?  DCE8 does not write this */


	set_reg_field_value(value,
		0,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		HDMI_CONNECTION);

	set_reg_field_value(value,
		0,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		DP_CONNECTION);

	field = get_reg_field_value(value,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			EXTRA_CONNECTION_INFO);

	field &= ~0x1;

	set_reg_field_value(value,
		field,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		EXTRA_CONNECTION_INFO);

	/* set audio for output signal */
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		set_reg_field_value(value,
			1,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			HDMI_CONNECTION);

		break;

	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		set_reg_field_value(value,
			1,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			DP_CONNECTION);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER, value);

	/*  Audio Descriptors   */
	/* pass through all formats */
	for (format_index = 0; format_index < AUDIO_FORMAT_CODE_COUNT;
			format_index++) {
		audio_format_code =
			(AUDIO_FORMAT_CODE_FIRST + format_index);

		/* those are unsupported, skip programming */
		if (audio_format_code == AUDIO_FORMAT_CODE_1BITAUDIO ||
			audio_format_code == AUDIO_FORMAT_CODE_DST)
			continue;

		value = 0;

		/* check if supported */
		if (is_audio_format_supported(
				audio_info, audio_format_code, &index)) {
			const struct audio_mode *audio_mode =
					&audio_info->modes[index];
			union audio_sample_rates sample_rates =
					audio_mode->sample_rates;
			uint8_t byte2 = audio_mode->max_bit_rate;

			/* adjust specific properties */
			switch (audio_format_code) {
			case AUDIO_FORMAT_CODE_LINEARPCM: {
				check_audio_bandwidth(
					crtc_info,
					audio_mode->channel_count,
					signal,
					&sample_rates);

				byte2 = audio_mode->sample_size;

				set_reg_field_value(value,
						sample_rates.all,
						AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
						SUPPORTED_FREQUENCIES_STEREO);
				}
				break;
			case AUDIO_FORMAT_CODE_AC3:
				is_ac3_supported = true;
				break;
			case AUDIO_FORMAT_CODE_DOLBYDIGITALPLUS:
			case AUDIO_FORMAT_CODE_DTS_HD:
			case AUDIO_FORMAT_CODE_MAT_MLP:
			case AUDIO_FORMAT_CODE_DST:
			case AUDIO_FORMAT_CODE_WMAPRO:
				byte2 = audio_mode->vendor_specific;
				break;
			default:
				break;
			}

			/* fill audio format data */
			set_reg_field_value(value,
					audio_mode->channel_count - 1,
					AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
					MAX_CHANNELS);

			set_reg_field_value(value,
					sample_rates.all,
					AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
					SUPPORTED_FREQUENCIES);

			set_reg_field_value(value,
					byte2,
					AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
					DESCRIPTOR_BYTE_2);
		} /* if */

		AZ_REG_WRITE(
				AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0 + format_index,
				value);
	} /* for */

	if (is_ac3_supported)
		/* todo: this reg global.  why program global register? */
		REG_WRITE(AZALIA_F0_CODEC_FUNCTION_PARAMETER_STREAM_FORMATS,
				0x05);

	/* check for 192khz/8-Ch support for HBR requirements */
	sample_rate.all = 0;
	sample_rate.rate.RATE_192 = 1;

	check_audio_bandwidth(
		crtc_info,
		8,
		signal,
		&sample_rate);

	set_high_bit_rate_capable(audio, sample_rate.rate.RATE_192);

	/* Audio and Video Lipsync */
	set_video_latency(audio, audio_info->video_latency);
	set_audio_latency(audio, audio_info->audio_latency);

	value = 0;
	set_reg_field_value(value, audio_info->manufacture_id,
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0,
		MANUFACTURER_ID);

	set_reg_field_value(value, audio_info->product_id,
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0,
		PRODUCT_ID);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0,
		value);

	value = 0;

	/*get display name string length */
	while (audio_info->display_name[strlen++] != '\0') {
		if (strlen >=
		MAX_HW_AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS)
			break;
		}
	set_reg_field_value(value, strlen,
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO1,
		SINK_DESCRIPTION_LEN);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO1,
		value);
	DC_LOG_HW_AUDIO("\n\tAUDIO:az_configure: index: %u data, 0x%x, displayName %s: \n",
		audio->inst, value, audio_info->display_name);

	/*
	*write the port ID:
	*PORT_ID0 = display index
	*PORT_ID1 = 16bit BDF
	*(format MSB->LSB: 8bit Bus, 5bit Device, 3bit Function)
	*/

	value = 0;

	set_reg_field_value(value, audio_info->port_id[0],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO2,
		PORT_ID0);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO2, value);

	value = 0;
	set_reg_field_value(value, audio_info->port_id[1],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO3,
		PORT_ID1);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO3, value);

	/*write the 18 char monitor string */

	value = 0;
	set_reg_field_value(value, audio_info->display_name[0],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION0);

	set_reg_field_value(value, audio_info->display_name[1],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION1);

	set_reg_field_value(value, audio_info->display_name[2],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION2);

	set_reg_field_value(value, audio_info->display_name[3],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION3);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4, value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[4],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION4);

	set_reg_field_value(value, audio_info->display_name[5],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION5);

	set_reg_field_value(value, audio_info->display_name[6],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION6);

	set_reg_field_value(value, audio_info->display_name[7],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION7);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5, value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[8],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION8);

	set_reg_field_value(value, audio_info->display_name[9],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION9);

	set_reg_field_value(value, audio_info->display_name[10],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION10);

	set_reg_field_value(value, audio_info->display_name[11],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION11);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6, value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[12],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION12);

	set_reg_field_value(value, audio_info->display_name[13],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION13);

	set_reg_field_value(value, audio_info->display_name[14],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION14);

	set_reg_field_value(value, audio_info->display_name[15],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION15);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7, value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[16],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO8,
		DESCRIPTION16);

	set_reg_field_value(value, audio_info->display_name[17],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO8,
		DESCRIPTION17);

	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO8, value);
	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);
	set_reg_field_value(value, 0,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);
}

/*
* todo: wall clk related functionality probably belong to clock_src.
*/

/* search pixel clock value for Azalia HDMI Audio */
static void get_azalia_clock_info_hdmi(
	uint32_t crtc_pixel_clock_100hz,
	uint32_t actual_pixel_clock_100Hz,
	struct azalia_clock_info *azalia_clock_info)
{
	/* audio_dto_phase= 24 * 10,000;
	 *   24MHz in [100Hz] units */
	azalia_clock_info->audio_dto_phase =
			24 * 10000;

	/* audio_dto_module = PCLKFrequency * 10,000;
	 *  [khz] -> [100Hz] */
	azalia_clock_info->audio_dto_module =
			actual_pixel_clock_100Hz;
}

static void get_azalia_clock_info_dp(
	uint32_t requested_pixel_clock_100Hz,
	const struct audio_pll_info *pll_info,
	struct azalia_clock_info *azalia_clock_info)
{
	/* Reported dpDtoSourceClockInkhz value for
	 * DCE8 already adjusted for SS, do not need any
	 * adjustment here anymore
	 */

	/*audio_dto_phase = 24 * 10,000;
	 * 24MHz in [100Hz] units */
	azalia_clock_info->audio_dto_phase = 24 * 10000;

	/*audio_dto_module = dpDtoSourceClockInkhz * 10,000;
	 *  [khz] ->[100Hz] */
	azalia_clock_info->audio_dto_module =
		pll_info->dp_dto_source_clock_in_khz * 10;
}

void dce_aud_wall_dto_setup(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info)
{
	struct dce_audio *aud = DCE_AUD(audio);

	struct azalia_clock_info clock_info = { 0 };

	if (dc_is_hdmi_tmds_signal(signal)) {
		uint32_t src_sel;

		/*DTO0 Programming goal:
		-generate 24MHz, 128*Fs from 24MHz
		-use DTO0 when an active HDMI port is connected
		(optionally a DP is connected) */

		/* calculate DTO settings */
		get_azalia_clock_info_hdmi(
			crtc_info->requested_pixel_clock_100Hz,
			crtc_info->calculated_pixel_clock_100Hz,
			&clock_info);

		DC_LOG_HW_AUDIO("\n%s:Input::requested_pixel_clock_100Hz = %d"\
				"calculated_pixel_clock_100Hz =%d\n"\
				"audio_dto_module = %d audio_dto_phase =%d \n\n", __func__,\
				crtc_info->requested_pixel_clock_100Hz,\
				crtc_info->calculated_pixel_clock_100Hz,\
				clock_info.audio_dto_module,\
				clock_info.audio_dto_phase);

		/* On TN/SI, Program DTO source select and DTO select before
		programming DTO modulo and DTO phase. These bits must be
		programmed first, otherwise there will be no HDMI audio at boot
		up. This is a HW sequence change (different from old ASICs).
		Caution when changing this programming sequence.

		HDMI enabled, using DTO0
		program master CRTC for DTO0 */
		src_sel = pll_info->dto_source - DTO_SOURCE_ID0;
		REG_UPDATE_2(DCCG_AUDIO_DTO_SOURCE,
			DCCG_AUDIO_DTO0_SOURCE_SEL, src_sel,
			DCCG_AUDIO_DTO_SEL, 0);

		/* module */
		REG_UPDATE(DCCG_AUDIO_DTO0_MODULE,
			DCCG_AUDIO_DTO0_MODULE, clock_info.audio_dto_module);

		/* phase */
		REG_UPDATE(DCCG_AUDIO_DTO0_PHASE,
			DCCG_AUDIO_DTO0_PHASE, clock_info.audio_dto_phase);
	} else {
		/*DTO1 Programming goal:
		-generate 24MHz, 512*Fs, 128*Fs from 24MHz
		-default is to used DTO1, and switch to DTO0 when an audio
		master HDMI port is connected
		-use as default for DP

		calculate DTO settings */
		get_azalia_clock_info_dp(
			crtc_info->requested_pixel_clock_100Hz,
			pll_info,
			&clock_info);

		/* Program DTO select before programming DTO modulo and DTO
		phase. default to use DTO1 */

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL, 1);

			/* DCCG_AUDIO_DTO2_USE_512FBR_DTO, 1)
			 * Select 512fs for DP TODO: web register definition
			 * does not match register header file
			 * DCE11 version it's commented out while DCE8 it's set to 1
			*/

		/* module */
		REG_UPDATE(DCCG_AUDIO_DTO1_MODULE,
				DCCG_AUDIO_DTO1_MODULE, clock_info.audio_dto_module);

		/* phase */
		REG_UPDATE(DCCG_AUDIO_DTO1_PHASE,
				DCCG_AUDIO_DTO1_PHASE, clock_info.audio_dto_phase);

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO2_USE_512FBR_DTO, 1);

	}
}

#if defined(CONFIG_DRM_AMD_DC_SI)
void dce60_aud_wall_dto_setup(
	struct audio *audio,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info)
{
	struct dce_audio *aud = DCE_AUD(audio);

	struct azalia_clock_info clock_info = { 0 };

	if (dc_is_hdmi_signal(signal)) {
		uint32_t src_sel;

		/*DTO0 Programming goal:
		-generate 24MHz, 128*Fs from 24MHz
		-use DTO0 when an active HDMI port is connected
		(optionally a DP is connected) */

		/* calculate DTO settings */
		get_azalia_clock_info_hdmi(
			crtc_info->requested_pixel_clock_100Hz,
			crtc_info->calculated_pixel_clock_100Hz,
			&clock_info);

		DC_LOG_HW_AUDIO("\n%s:Input::requested_pixel_clock_100Hz = %d"\
				"calculated_pixel_clock_100Hz =%d\n"\
				"audio_dto_module = %d audio_dto_phase =%d \n\n", __func__,\
				crtc_info->requested_pixel_clock_100Hz,\
				crtc_info->calculated_pixel_clock_100Hz,\
				clock_info.audio_dto_module,\
				clock_info.audio_dto_phase);

		/* On TN/SI, Program DTO source select and DTO select before
		programming DTO modulo and DTO phase. These bits must be
		programmed first, otherwise there will be no HDMI audio at boot
		up. This is a HW sequence change (different from old ASICs).
		Caution when changing this programming sequence.

		HDMI enabled, using DTO0
		program master CRTC for DTO0 */
		src_sel = pll_info->dto_source - DTO_SOURCE_ID0;
		REG_UPDATE_2(DCCG_AUDIO_DTO_SOURCE,
			DCCG_AUDIO_DTO0_SOURCE_SEL, src_sel,
			DCCG_AUDIO_DTO_SEL, 0);

		/* module */
		REG_UPDATE(DCCG_AUDIO_DTO0_MODULE,
			DCCG_AUDIO_DTO0_MODULE, clock_info.audio_dto_module);

		/* phase */
		REG_UPDATE(DCCG_AUDIO_DTO0_PHASE,
			DCCG_AUDIO_DTO0_PHASE, clock_info.audio_dto_phase);
	} else {
		/*DTO1 Programming goal:
		-generate 24MHz, 128*Fs from 24MHz (DCE6 does not support 512*Fs)
		-default is to used DTO1, and switch to DTO0 when an audio
		master HDMI port is connected
		-use as default for DP

		calculate DTO settings */
		get_azalia_clock_info_dp(
			crtc_info->requested_pixel_clock_100Hz,
			pll_info,
			&clock_info);

		/* Program DTO select before programming DTO modulo and DTO
		phase. default to use DTO1 */

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL, 1);

			/* DCCG_AUDIO_DTO2_USE_512FBR_DTO, 1)
			 * Cannot select 512fs for DP
			 *
			 * DCE6 has no DCCG_AUDIO_DTO2_USE_512FBR_DTO mask
			*/

		/* module */
		REG_UPDATE(DCCG_AUDIO_DTO1_MODULE,
				DCCG_AUDIO_DTO1_MODULE, clock_info.audio_dto_module);

		/* phase */
		REG_UPDATE(DCCG_AUDIO_DTO1_PHASE,
				DCCG_AUDIO_DTO1_PHASE, clock_info.audio_dto_phase);

		/* DCE6 has no DCCG_AUDIO_DTO2_USE_512FBR_DTO mask in DCCG_AUDIO_DTO_SOURCE reg */

	}
}
#endif

static bool dce_aud_endpoint_valid(struct audio *audio)
{
	uint32_t value;
	uint32_t port_connectivity;

	value = AZ_REG_READ(
			AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT);

	port_connectivity = get_reg_field_value(value,
			AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT,
			PORT_CONNECTIVITY);

	return !(port_connectivity == 1);
}

/* initialize HW state */
void dce_aud_hw_init(
		struct audio *audio)
{
	uint32_t value;
	struct dce_audio *aud = DCE_AUD(audio);

	/* we only need to program the following registers once, so we only do
	it for the inst 0*/
	if (audio->inst != 0)
		return;

	/* Suport R5 - 32khz
	 * Suport R6 - 44.1khz
	 * Suport R7 - 48khz
	 */
	/*disable clock gating before write to endpoint register*/
	value = AZ_REG_READ(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);
	set_reg_field_value(value, 1,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);
	REG_UPDATE(AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES,
			AUDIO_RATE_CAPABILITIES, 0x70);

	/*Keep alive bit to verify HW block in BU. */
	REG_UPDATE_2(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES,
			CLKSTOP, 1,
			EPSS, 1);
	set_reg_field_value(value, 0,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			CLOCK_GATING_DISABLE);
	AZ_REG_WRITE(AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL, value);
}

static const struct audio_funcs funcs = {
	.endpoint_valid = dce_aud_endpoint_valid,
	.hw_init = dce_aud_hw_init,
	.wall_dto_setup = dce_aud_wall_dto_setup,
	.az_enable = dce_aud_az_enable,
	.az_disable = dce_aud_az_disable,
	.az_configure = dce_aud_az_configure,
	.destroy = dce_aud_destroy,
};

#if defined(CONFIG_DRM_AMD_DC_SI)
static const struct audio_funcs dce60_funcs = {
	.endpoint_valid = dce_aud_endpoint_valid,
	.hw_init = dce_aud_hw_init,
	.wall_dto_setup = dce60_aud_wall_dto_setup,
	.az_enable = dce_aud_az_enable,
	.az_disable = dce_aud_az_disable,
	.az_configure = dce_aud_az_configure,
	.destroy = dce_aud_destroy,
};
#endif

void dce_aud_destroy(struct audio **audio)
{
	struct dce_audio *aud = DCE_AUD(*audio);

	kfree(aud);
	*audio = NULL;
}

struct audio *dce_audio_create(
		struct dc_context *ctx,
		unsigned int inst,
		const struct dce_audio_registers *reg,
		const struct dce_audio_shift *shifts,
		const struct dce_audio_mask *masks
		)
{
	struct dce_audio *audio = kzalloc(sizeof(*audio), GFP_KERNEL);

	if (audio == NULL) {
		ASSERT_CRITICAL(audio);
		return NULL;
	}

	audio->base.ctx = ctx;
	audio->base.inst = inst;
	audio->base.funcs = &funcs;

	audio->regs = reg;
	audio->shifts = shifts;
	audio->masks = masks;
	return &audio->base;
}

#if defined(CONFIG_DRM_AMD_DC_SI)
struct audio *dce60_audio_create(
		struct dc_context *ctx,
		unsigned int inst,
		const struct dce_audio_registers *reg,
		const struct dce_audio_shift *shifts,
		const struct dce_audio_mask *masks
		)
{
	struct dce_audio *audio = kzalloc(sizeof(*audio), GFP_KERNEL);

	if (audio == NULL) {
		ASSERT_CRITICAL(audio);
		return NULL;
	}

	audio->base.ctx = ctx;
	audio->base.inst = inst;
	audio->base.funcs = &dce60_funcs;

	audio->regs = reg;
	audio->shifts = shifts;
	audio->masks = masks;
	return &audio->base;
}
#endif
