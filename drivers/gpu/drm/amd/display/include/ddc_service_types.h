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

#define DP_BRANCH_DEVICE_ID_1 0x0010FA
#define DP_BRANCH_DEVICE_ID_2 0x0022B9
#define DP_SINK_DEVICE_ID_1 0x4CE000
#define DP_BRANCH_DEVICE_ID_3 0x00001A
#define DP_BRANCH_DEVICE_ID_4 0x0080e1
#define DP_BRANCH_DEVICE_ID_5 0x006037
#define DP_SINK_DEVICE_ID_2 0x001CF8


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
	DDC_RESULT_FAILED_BUFFER_OVERFLOW
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

/** EDID retrieval related constants, also used by MstMgr **/

#define DDC_EDID_SEGMENT_SIZE 256
#define DDC_EDID_BLOCK_SIZE 128
#define DDC_EDID_BLOCKS_PER_SEGMENT \
	(DDC_EDID_SEGMENT_SIZE / DDC_EDID_BLOCK_SIZE)

#define DDC_EDID_EXT_COUNT_OFFSET 0x7E

#define DDC_EDID_ADDRESS_START 0x50
#define DDC_EDID_ADDRESS_END 0x52
#define DDC_EDID_SEGMENT_ADDRESS 0x30

/* signatures for Edid 1x */
#define DDC_EDID1X_VENDORID_SIGNATURE_OFFSET 8
#define DDC_EDID1X_VENDORID_SIGNATURE_LEN 4
#define DDC_EDID1X_EXT_CNT_AND_CHECKSUM_OFFSET 126
#define DDC_EDID1X_EXT_CNT_AND_CHECKSUM_LEN 2
#define DDC_EDID1X_CHECKSUM_OFFSET 127
/* signatures for Edid 20*/
#define DDC_EDID_20_SIGNATURE_OFFSET 0
#define DDC_EDID_20_SIGNATURE 0x20

#define DDC_EDID20_VENDORID_SIGNATURE_OFFSET 1
#define DDC_EDID20_VENDORID_SIGNATURE_LEN 4
#define DDC_EDID20_CHECKSUM_OFFSET 255
#define DDC_EDID20_CHECKSUM_LEN 1

/*DP to VGA converter*/
static const uint8_t DP_VGA_CONVERTER_ID_1[] = "mVGAa";
/*DP to Dual link DVI converter*/
static const uint8_t DP_DVI_CONVERTER_ID_1[] = "m2DVIa";
/*Travis*/
static const uint8_t DP_VGA_LVDS_CONVERTER_ID_2[] = "sivarT";
/*Nutmeg*/
static const uint8_t DP_VGA_LVDS_CONVERTER_ID_3[] = "dnomlA";
/*DP to VGA converter*/
static const uint8_t DP_VGA_CONVERTER_ID_4[] = "DpVga";
/*DP to Dual link DVI converter*/
static const uint8_t DP_DVI_CONVERTER_ID_4[] = "m2DVIa";
/*DP to Dual link DVI converter 2*/
static const uint8_t DP_DVI_CONVERTER_ID_42[] = "v2DVIa";

static const uint8_t DP_SINK_DEV_STRING_ID2_REV0[] = "\0\0\0\0\0\0";

/* Identifies second generation PSR TCON from Parade: Device ID string:
 * yy-xx-**-**-**-**
 */
/* xx - Hw ID high byte */
static const uint32_t DP_SINK_DEV_STRING_ID2_REV1_HW_ID_HIGH_BYTE =
	0x06;

/* yy - HW ID low byte, the same silicon has several package/feature flavors */
static const uint32_t DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE1 =
	0x61;
static const uint32_t DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE2 =
	0x62;
static const uint32_t DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE3 =
	0x63;
static const uint32_t DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE4 =
	0x72;
static const uint32_t DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE5 =
	0x73;

#endif /* __DAL_DDC_SERVICE_TYPES_H__ */
