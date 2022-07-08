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
#ifndef __DAL_DDC_SERVICE_TYPES_H__
#define __DAL_DDC_SERVICE_TYPES_H__

/* 0010FA dongles (ST Micro) external converter chip id */
#define DP_BRANCH_DEVICE_ID_0010FA 0x0010FA
/* 0022B9 external converter chip id */
#define DP_BRANCH_DEVICE_ID_0022B9 0x0022B9
#define DP_BRANCH_DEVICE_ID_00001A 0x00001A
#define DP_BRANCH_DEVICE_ID_0080E1 0x0080e1
#define DP_BRANCH_DEVICE_ID_90CC24 0x90CC24
#define DP_BRANCH_DEVICE_ID_00E04C 0x00E04C
#define DP_BRANCH_DEVICE_ID_006037 0x006037
#define DP_BRANCH_DEVICE_ID_001CF8 0x001CF8
#define DP_BRANCH_HW_REV_10 0x10
#define DP_BRANCH_HW_REV_20 0x20

#define DP_DEVICE_ID_38EC11 0x38EC11
#define DP_FORCE_PSRSU_CAPABILITY 0x40F

enum ddc_result {
	DDC_RESULT_UNKNOWN = 0,
	DDC_RESULT_SUCESSFULL,
	DDC_RESULT_FAILED_CHANNEL_BUSY,
	DDC_RESULT_FAILED_TIMEOUT,
	DDC_RESULT_FAILED_PROTOCOL_ERROR,
	DDC_RESULT_FAILED_NACK,
	DDC_RESULT_FAILED_INCOMPLETE,
	DDC_RESULT_FAILED_OPERATION,
	DDC_RESULT_FAILED_INVALID_OPERATION,
	DDC_RESULT_FAILED_BUFFER_OVERFLOW,
	DDC_RESULT_FAILED_HPD_DISCON
};

enum ddc_service_type {
	DDC_SERVICE_TYPE_CONNECTOR,
	DDC_SERVICE_TYPE_DISPLAY_PORT_MST,
};

/**
 * display sink capability
 */
struct display_sink_capability {
	/* dongle type (DP converter, CV smart dongle) */
	enum display_dongle_type dongle_type;
	bool is_dongle_type_one;

	/**********************************************************
	 capabilities going INTO SINK DEVICE (stream capabilities)
	 **********************************************************/
	/* Dongle's downstream count. */
	uint32_t downstrm_sink_count;
	/* Is dongle's downstream count info field (downstrm_sink_count)
	 * valid. */
	bool downstrm_sink_count_valid;

	/* Maximum additional audio delay in microsecond (us) */
	uint32_t additional_audio_delay;
	/* Audio latency value in microsecond (us) */
	uint32_t audio_latency;
	/* Interlace video latency value in microsecond (us) */
	uint32_t video_latency_interlace;
	/* Progressive video latency value in microsecond (us) */
	uint32_t video_latency_progressive;
	/* Dongle caps: Maximum pixel clock supported over dongle for HDMI */
	uint32_t max_hdmi_pixel_clock;
	/* Dongle caps: Maximum deep color supported over dongle for HDMI */
	enum dc_color_depth max_hdmi_deep_color;

	/************************************************************
	 capabilities going OUT OF SOURCE DEVICE (link capabilities)
	 ************************************************************/
	/* support for Spread Spectrum(SS) */
	bool ss_supported;
	/* DP link settings (laneCount, linkRate, Spread) */
	uint32_t dp_link_lane_count;
	uint32_t dp_link_rate;
	uint32_t dp_link_spead;

	/* If dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER,
	indicates 'Frame Sequential-to-lllFrame Pack' conversion capability.*/
	bool is_dp_hdmi_s3d_converter;
	/* to check if we have queried the display capability
	 * for eDP panel already. */
	bool is_edp_sink_cap_valid;

	enum ddc_transaction_type transaction_type;
	enum signal_type signal;
};

struct av_sync_data {
	uint8_t av_granularity;/* DPCD 00023h */
	uint8_t aud_dec_lat1;/* DPCD 00024h */
	uint8_t aud_dec_lat2;/* DPCD 00025h */
	uint8_t aud_pp_lat1;/* DPCD 00026h */
	uint8_t aud_pp_lat2;/* DPCD 00027h */
	uint8_t vid_inter_lat;/* DPCD 00028h */
	uint8_t vid_prog_lat;/* DPCD 00029h */
	uint8_t aud_del_ins1;/* DPCD 0002Bh */
	uint8_t aud_del_ins2;/* DPCD 0002Ch */
	uint8_t aud_del_ins3;/* DPCD 0002Dh */
};

static const uint8_t DP_SINK_DEVICE_STR_ID_1[] = {7, 1, 8, 7, 3, 0};
static const uint8_t DP_SINK_DEVICE_STR_ID_2[] = {7, 1, 8, 7, 5, 0};

static const u8 DP_SINK_BRANCH_DEV_NAME_7580[] = "7580\x80u";

/*MST Dock*/
static const uint8_t SYNAPTICS_DEVICE_ID[] = "SYNA";

#endif /* __DAL_DDC_SERVICE_TYPES_H__ */
