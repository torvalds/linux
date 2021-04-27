// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * V4L2 controls framework control definitions.
 *
 * Copyright (C) 2010-2021  Hans Verkuil <hverkuil-cisco@xs4all.nl>
 */

#include <linux/export.h>
#include <media/v4l2-ctrls.h>

/*
 * Returns NULL or a character pointer array containing the menu for
 * the given control ID. The pointer array ends with a NULL pointer.
 * An empty string signifies a menu entry that is invalid. This allows
 * drivers to disable certain options if it is not supported.
 */
const char * const *v4l2_ctrl_get_menu(u32 id)
{
	static const char * const mpeg_audio_sampling_freq[] = {
		"44.1 kHz",
		"48 kHz",
		"32 kHz",
		NULL
	};
	static const char * const mpeg_audio_encoding[] = {
		"MPEG-1/2 Layer I",
		"MPEG-1/2 Layer II",
		"MPEG-1/2 Layer III",
		"MPEG-2/4 AAC",
		"AC-3",
		NULL
	};
	static const char * const mpeg_audio_l1_bitrate[] = {
		"32 kbps",
		"64 kbps",
		"96 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"288 kbps",
		"320 kbps",
		"352 kbps",
		"384 kbps",
		"416 kbps",
		"448 kbps",
		NULL
	};
	static const char * const mpeg_audio_l2_bitrate[] = {
		"32 kbps",
		"48 kbps",
		"56 kbps",
		"64 kbps",
		"80 kbps",
		"96 kbps",
		"112 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"320 kbps",
		"384 kbps",
		NULL
	};
	static const char * const mpeg_audio_l3_bitrate[] = {
		"32 kbps",
		"40 kbps",
		"48 kbps",
		"56 kbps",
		"64 kbps",
		"80 kbps",
		"96 kbps",
		"112 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"320 kbps",
		NULL
	};
	static const char * const mpeg_audio_ac3_bitrate[] = {
		"32 kbps",
		"40 kbps",
		"48 kbps",
		"56 kbps",
		"64 kbps",
		"80 kbps",
		"96 kbps",
		"112 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"320 kbps",
		"384 kbps",
		"448 kbps",
		"512 kbps",
		"576 kbps",
		"640 kbps",
		NULL
	};
	static const char * const mpeg_audio_mode[] = {
		"Stereo",
		"Joint Stereo",
		"Dual",
		"Mono",
		NULL
	};
	static const char * const mpeg_audio_mode_extension[] = {
		"Bound 4",
		"Bound 8",
		"Bound 12",
		"Bound 16",
		NULL
	};
	static const char * const mpeg_audio_emphasis[] = {
		"No Emphasis",
		"50/15 us",
		"CCITT J17",
		NULL
	};
	static const char * const mpeg_audio_crc[] = {
		"No CRC",
		"16-bit CRC",
		NULL
	};
	static const char * const mpeg_audio_dec_playback[] = {
		"Auto",
		"Stereo",
		"Left",
		"Right",
		"Mono",
		"Swapped Stereo",
		NULL
	};
	static const char * const mpeg_video_encoding[] = {
		"MPEG-1",
		"MPEG-2",
		"MPEG-4 AVC",
		NULL
	};
	static const char * const mpeg_video_aspect[] = {
		"1x1",
		"4x3",
		"16x9",
		"2.21x1",
		NULL
	};
	static const char * const mpeg_video_bitrate_mode[] = {
		"Variable Bitrate",
		"Constant Bitrate",
		"Constant Quality",
		NULL
	};
	static const char * const mpeg_stream_type[] = {
		"MPEG-2 Program Stream",
		"MPEG-2 Transport Stream",
		"MPEG-1 System Stream",
		"MPEG-2 DVD-compatible Stream",
		"MPEG-1 VCD-compatible Stream",
		"MPEG-2 SVCD-compatible Stream",
		NULL
	};
	static const char * const mpeg_stream_vbi_fmt[] = {
		"No VBI",
		"Private Packet, IVTV Format",
		NULL
	};
	static const char * const camera_power_line_frequency[] = {
		"Disabled",
		"50 Hz",
		"60 Hz",
		"Auto",
		NULL
	};
	static const char * const camera_exposure_auto[] = {
		"Auto Mode",
		"Manual Mode",
		"Shutter Priority Mode",
		"Aperture Priority Mode",
		NULL
	};
	static const char * const camera_exposure_metering[] = {
		"Average",
		"Center Weighted",
		"Spot",
		"Matrix",
		NULL
	};
	static const char * const camera_auto_focus_range[] = {
		"Auto",
		"Normal",
		"Macro",
		"Infinity",
		NULL
	};
	static const char * const colorfx[] = {
		"None",
		"Black & White",
		"Sepia",
		"Negative",
		"Emboss",
		"Sketch",
		"Sky Blue",
		"Grass Green",
		"Skin Whiten",
		"Vivid",
		"Aqua",
		"Art Freeze",
		"Silhouette",
		"Solarization",
		"Antique",
		"Set Cb/Cr",
		NULL
	};
	static const char * const auto_n_preset_white_balance[] = {
		"Manual",
		"Auto",
		"Incandescent",
		"Fluorescent",
		"Fluorescent H",
		"Horizon",
		"Daylight",
		"Flash",
		"Cloudy",
		"Shade",
		NULL,
	};
	static const char * const camera_iso_sensitivity_auto[] = {
		"Manual",
		"Auto",
		NULL
	};
	static const char * const scene_mode[] = {
		"None",
		"Backlight",
		"Beach/Snow",
		"Candle Light",
		"Dusk/Dawn",
		"Fall Colors",
		"Fireworks",
		"Landscape",
		"Night",
		"Party/Indoor",
		"Portrait",
		"Sports",
		"Sunset",
		"Text",
		NULL
	};
	static const char * const tune_emphasis[] = {
		"None",
		"50 Microseconds",
		"75 Microseconds",
		NULL,
	};
	static const char * const header_mode[] = {
		"Separate Buffer",
		"Joined With 1st Frame",
		NULL,
	};
	static const char * const multi_slice[] = {
		"Single",
		"Max Macroblocks",
		"Max Bytes",
		NULL,
	};
	static const char * const entropy_mode[] = {
		"CAVLC",
		"CABAC",
		NULL,
	};
	static const char * const mpeg_h264_level[] = {
		"1",
		"1b",
		"1.1",
		"1.2",
		"1.3",
		"2",
		"2.1",
		"2.2",
		"3",
		"3.1",
		"3.2",
		"4",
		"4.1",
		"4.2",
		"5",
		"5.1",
		"5.2",
		"6.0",
		"6.1",
		"6.2",
		NULL,
	};
	static const char * const h264_loop_filter[] = {
		"Enabled",
		"Disabled",
		"Disabled at Slice Boundary",
		NULL,
	};
	static const char * const h264_profile[] = {
		"Baseline",
		"Constrained Baseline",
		"Main",
		"Extended",
		"High",
		"High 10",
		"High 422",
		"High 444 Predictive",
		"High 10 Intra",
		"High 422 Intra",
		"High 444 Intra",
		"CAVLC 444 Intra",
		"Scalable Baseline",
		"Scalable High",
		"Scalable High Intra",
		"Stereo High",
		"Multiview High",
		"Constrained High",
		NULL,
	};
	static const char * const vui_sar_idc[] = {
		"Unspecified",
		"1:1",
		"12:11",
		"10:11",
		"16:11",
		"40:33",
		"24:11",
		"20:11",
		"32:11",
		"80:33",
		"18:11",
		"15:11",
		"64:33",
		"160:99",
		"4:3",
		"3:2",
		"2:1",
		"Extended SAR",
		NULL,
	};
	static const char * const h264_fp_arrangement_type[] = {
		"Checkerboard",
		"Column",
		"Row",
		"Side by Side",
		"Top Bottom",
		"Temporal",
		NULL,
	};
	static const char * const h264_fmo_map_type[] = {
		"Interleaved Slices",
		"Scattered Slices",
		"Foreground with Leftover",
		"Box Out",
		"Raster Scan",
		"Wipe Scan",
		"Explicit",
		NULL,
	};
	static const char * const h264_decode_mode[] = {
		"Slice-Based",
		"Frame-Based",
		NULL,
	};
	static const char * const h264_start_code[] = {
		"No Start Code",
		"Annex B Start Code",
		NULL,
	};
	static const char * const h264_hierarchical_coding_type[] = {
		"Hier Coding B",
		"Hier Coding P",
		NULL,
	};
	static const char * const mpeg_mpeg2_level[] = {
		"Low",
		"Main",
		"High 1440",
		"High",
		NULL,
	};
	static const char * const mpeg2_profile[] = {
		"Simple",
		"Main",
		"SNR Scalable",
		"Spatially Scalable",
		"High",
		NULL,
	};
	static const char * const mpeg_mpeg4_level[] = {
		"0",
		"0b",
		"1",
		"2",
		"3",
		"3b",
		"4",
		"5",
		NULL,
	};
	static const char * const mpeg4_profile[] = {
		"Simple",
		"Advanced Simple",
		"Core",
		"Simple Scalable",
		"Advanced Coding Efficiency",
		NULL,
	};

	static const char * const vpx_golden_frame_sel[] = {
		"Use Previous Frame",
		"Use Previous Specific Frame",
		NULL,
	};
	static const char * const vp8_profile[] = {
		"0",
		"1",
		"2",
		"3",
		NULL,
	};
	static const char * const vp9_profile[] = {
		"0",
		"1",
		"2",
		"3",
		NULL,
	};
	static const char * const vp9_level[] = {
		"1",
		"1.1",
		"2",
		"2.1",
		"3",
		"3.1",
		"4",
		"4.1",
		"5",
		"5.1",
		"5.2",
		"6",
		"6.1",
		"6.2",
		NULL,
	};

	static const char * const flash_led_mode[] = {
		"Off",
		"Flash",
		"Torch",
		NULL,
	};
	static const char * const flash_strobe_source[] = {
		"Software",
		"External",
		NULL,
	};

	static const char * const jpeg_chroma_subsampling[] = {
		"4:4:4",
		"4:2:2",
		"4:2:0",
		"4:1:1",
		"4:1:0",
		"Gray",
		NULL,
	};
	static const char * const dv_tx_mode[] = {
		"DVI-D",
		"HDMI",
		NULL,
	};
	static const char * const dv_rgb_range[] = {
		"Automatic",
		"RGB Limited Range (16-235)",
		"RGB Full Range (0-255)",
		NULL,
	};
	static const char * const dv_it_content_type[] = {
		"Graphics",
		"Photo",
		"Cinema",
		"Game",
		"No IT Content",
		NULL,
	};
	static const char * const detect_md_mode[] = {
		"Disabled",
		"Global",
		"Threshold Grid",
		"Region Grid",
		NULL,
	};

	static const char * const hevc_profile[] = {
		"Main",
		"Main Still Picture",
		"Main 10",
		NULL,
	};
	static const char * const hevc_level[] = {
		"1",
		"2",
		"2.1",
		"3",
		"3.1",
		"4",
		"4.1",
		"5",
		"5.1",
		"5.2",
		"6",
		"6.1",
		"6.2",
		NULL,
	};
	static const char * const hevc_hierarchial_coding_type[] = {
		"B",
		"P",
		NULL,
	};
	static const char * const hevc_refresh_type[] = {
		"None",
		"CRA",
		"IDR",
		NULL,
	};
	static const char * const hevc_size_of_length_field[] = {
		"0",
		"1",
		"2",
		"4",
		NULL,
	};
	static const char * const hevc_tier[] = {
		"Main",
		"High",
		NULL,
	};
	static const char * const hevc_loop_filter_mode[] = {
		"Disabled",
		"Enabled",
		"Disabled at slice boundary",
		"NULL",
	};
	static const char * const hevc_decode_mode[] = {
		"Slice-Based",
		"Frame-Based",
		NULL,
	};
	static const char * const hevc_start_code[] = {
		"No Start Code",
		"Annex B Start Code",
		NULL,
	};
	static const char * const camera_orientation[] = {
		"Front",
		"Back",
		"External",
		NULL,
	};
	static const char * const mpeg_video_frame_skip[] = {
		"Disabled",
		"Level Limit",
		"VBV/CPB Limit",
		NULL,
	};

	switch (id) {
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		return mpeg_audio_sampling_freq;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		return mpeg_audio_encoding;
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
		return mpeg_audio_l1_bitrate;
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		return mpeg_audio_l2_bitrate;
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
		return mpeg_audio_l3_bitrate;
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		return mpeg_audio_ac3_bitrate;
	case V4L2_CID_MPEG_AUDIO_MODE:
		return mpeg_audio_mode;
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		return mpeg_audio_mode_extension;
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
		return mpeg_audio_emphasis;
	case V4L2_CID_MPEG_AUDIO_CRC:
		return mpeg_audio_crc;
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:
	case V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK:
		return mpeg_audio_dec_playback;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		return mpeg_video_encoding;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		return mpeg_video_aspect;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		return mpeg_video_bitrate_mode;
	case V4L2_CID_MPEG_STREAM_TYPE:
		return mpeg_stream_type;
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		return mpeg_stream_vbi_fmt;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		return camera_power_line_frequency;
	case V4L2_CID_EXPOSURE_AUTO:
		return camera_exposure_auto;
	case V4L2_CID_EXPOSURE_METERING:
		return camera_exposure_metering;
	case V4L2_CID_AUTO_FOCUS_RANGE:
		return camera_auto_focus_range;
	case V4L2_CID_COLORFX:
		return colorfx;
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		return auto_n_preset_white_balance;
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
		return camera_iso_sensitivity_auto;
	case V4L2_CID_SCENE_MODE:
		return scene_mode;
	case V4L2_CID_TUNE_PREEMPHASIS:
		return tune_emphasis;
	case V4L2_CID_TUNE_DEEMPHASIS:
		return tune_emphasis;
	case V4L2_CID_FLASH_LED_MODE:
		return flash_led_mode;
	case V4L2_CID_FLASH_STROBE_SOURCE:
		return flash_strobe_source;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		return header_mode;
	case V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE:
		return mpeg_video_frame_skip;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		return multi_slice;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		return entropy_mode;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		return mpeg_h264_level;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		return h264_loop_filter;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		return h264_profile;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
		return vui_sar_idc;
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
		return h264_fp_arrangement_type;
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:
		return h264_fmo_map_type;
	case V4L2_CID_STATELESS_H264_DECODE_MODE:
		return h264_decode_mode;
	case V4L2_CID_STATELESS_H264_START_CODE:
		return h264_start_code;
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE:
		return h264_hierarchical_coding_type;
	case V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL:
		return mpeg_mpeg2_level;
	case V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE:
		return mpeg2_profile;
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
		return mpeg_mpeg4_level;
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
		return mpeg4_profile;
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL:
		return vpx_golden_frame_sel;
	case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:
		return vp8_profile;
	case V4L2_CID_MPEG_VIDEO_VP9_PROFILE:
		return vp9_profile;
	case V4L2_CID_MPEG_VIDEO_VP9_LEVEL:
		return vp9_level;
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		return jpeg_chroma_subsampling;
	case V4L2_CID_DV_TX_MODE:
		return dv_tx_mode;
	case V4L2_CID_DV_TX_RGB_RANGE:
	case V4L2_CID_DV_RX_RGB_RANGE:
		return dv_rgb_range;
	case V4L2_CID_DV_TX_IT_CONTENT_TYPE:
	case V4L2_CID_DV_RX_IT_CONTENT_TYPE:
		return dv_it_content_type;
	case V4L2_CID_DETECT_MD_MODE:
		return detect_md_mode;
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
		return hevc_profile;
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
		return hevc_level;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE:
		return hevc_hierarchial_coding_type;
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE:
		return hevc_refresh_type;
	case V4L2_CID_MPEG_VIDEO_HEVC_SIZE_OF_LENGTH_FIELD:
		return hevc_size_of_length_field;
	case V4L2_CID_MPEG_VIDEO_HEVC_TIER:
		return hevc_tier;
	case V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE:
		return hevc_loop_filter_mode;
	case V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE:
		return hevc_decode_mode;
	case V4L2_CID_MPEG_VIDEO_HEVC_START_CODE:
		return hevc_start_code;
	case V4L2_CID_CAMERA_ORIENTATION:
		return camera_orientation;
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_menu);

#define __v4l2_qmenu_int_len(arr, len) ({ *(len) = ARRAY_SIZE(arr); (arr); })
/*
 * Returns NULL or an s64 type array containing the menu for given
 * control ID. The total number of the menu items is returned in @len.
 */
const s64 *v4l2_ctrl_get_int_menu(u32 id, u32 *len)
{
	static const s64 qmenu_int_vpx_num_partitions[] = {
		1, 2, 4, 8,
	};

	static const s64 qmenu_int_vpx_num_ref_frames[] = {
		1, 2, 3,
	};

	switch (id) {
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS:
		return __v4l2_qmenu_int_len(qmenu_int_vpx_num_partitions, len);
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES:
		return __v4l2_qmenu_int_len(qmenu_int_vpx_num_ref_frames, len);
	default:
		*len = 0;
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_int_menu);

/* Return the control name. */
const char *v4l2_ctrl_get_name(u32 id)
{
	switch (id) {
	/* USER controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_USER_CLASS:		return "User Controls";
	case V4L2_CID_BRIGHTNESS:		return "Brightness";
	case V4L2_CID_CONTRAST:			return "Contrast";
	case V4L2_CID_SATURATION:		return "Saturation";
	case V4L2_CID_HUE:			return "Hue";
	case V4L2_CID_AUDIO_VOLUME:		return "Volume";
	case V4L2_CID_AUDIO_BALANCE:		return "Balance";
	case V4L2_CID_AUDIO_BASS:		return "Bass";
	case V4L2_CID_AUDIO_TREBLE:		return "Treble";
	case V4L2_CID_AUDIO_MUTE:		return "Mute";
	case V4L2_CID_AUDIO_LOUDNESS:		return "Loudness";
	case V4L2_CID_BLACK_LEVEL:		return "Black Level";
	case V4L2_CID_AUTO_WHITE_BALANCE:	return "White Balance, Automatic";
	case V4L2_CID_DO_WHITE_BALANCE:		return "Do White Balance";
	case V4L2_CID_RED_BALANCE:		return "Red Balance";
	case V4L2_CID_BLUE_BALANCE:		return "Blue Balance";
	case V4L2_CID_GAMMA:			return "Gamma";
	case V4L2_CID_EXPOSURE:			return "Exposure";
	case V4L2_CID_AUTOGAIN:			return "Gain, Automatic";
	case V4L2_CID_GAIN:			return "Gain";
	case V4L2_CID_HFLIP:			return "Horizontal Flip";
	case V4L2_CID_VFLIP:			return "Vertical Flip";
	case V4L2_CID_POWER_LINE_FREQUENCY:	return "Power Line Frequency";
	case V4L2_CID_HUE_AUTO:			return "Hue, Automatic";
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE: return "White Balance Temperature";
	case V4L2_CID_SHARPNESS:		return "Sharpness";
	case V4L2_CID_BACKLIGHT_COMPENSATION:	return "Backlight Compensation";
	case V4L2_CID_CHROMA_AGC:		return "Chroma AGC";
	case V4L2_CID_COLOR_KILLER:		return "Color Killer";
	case V4L2_CID_COLORFX:			return "Color Effects";
	case V4L2_CID_AUTOBRIGHTNESS:		return "Brightness, Automatic";
	case V4L2_CID_BAND_STOP_FILTER:		return "Band-Stop Filter";
	case V4L2_CID_ROTATE:			return "Rotate";
	case V4L2_CID_BG_COLOR:			return "Background Color";
	case V4L2_CID_CHROMA_GAIN:		return "Chroma Gain";
	case V4L2_CID_ILLUMINATORS_1:		return "Illuminator 1";
	case V4L2_CID_ILLUMINATORS_2:		return "Illuminator 2";
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:	return "Min Number of Capture Buffers";
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:	return "Min Number of Output Buffers";
	case V4L2_CID_ALPHA_COMPONENT:		return "Alpha Component";
	case V4L2_CID_COLORFX_CBCR:		return "Color Effects, CbCr";

	/*
	 * Codec controls
	 *
	 * The MPEG controls are applicable to all codec controls
	 * and the 'MPEG' part of the define is historical.
	 *
	 * Keep the order of the 'case's the same as in videodev2.h!
	 */
	case V4L2_CID_CODEC_CLASS:		return "Codec Controls";
	case V4L2_CID_MPEG_STREAM_TYPE:		return "Stream Type";
	case V4L2_CID_MPEG_STREAM_PID_PMT:	return "Stream PMT Program ID";
	case V4L2_CID_MPEG_STREAM_PID_AUDIO:	return "Stream Audio Program ID";
	case V4L2_CID_MPEG_STREAM_PID_VIDEO:	return "Stream Video Program ID";
	case V4L2_CID_MPEG_STREAM_PID_PCR:	return "Stream PCR Program ID";
	case V4L2_CID_MPEG_STREAM_PES_ID_AUDIO: return "Stream PES Audio ID";
	case V4L2_CID_MPEG_STREAM_PES_ID_VIDEO: return "Stream PES Video ID";
	case V4L2_CID_MPEG_STREAM_VBI_FMT:	return "Stream VBI Format";
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ: return "Audio Sampling Frequency";
	case V4L2_CID_MPEG_AUDIO_ENCODING:	return "Audio Encoding";
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:	return "Audio Layer I Bitrate";
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:	return "Audio Layer II Bitrate";
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:	return "Audio Layer III Bitrate";
	case V4L2_CID_MPEG_AUDIO_MODE:		return "Audio Stereo Mode";
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION: return "Audio Stereo Mode Extension";
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:	return "Audio Emphasis";
	case V4L2_CID_MPEG_AUDIO_CRC:		return "Audio CRC";
	case V4L2_CID_MPEG_AUDIO_MUTE:		return "Audio Mute";
	case V4L2_CID_MPEG_AUDIO_AAC_BITRATE:	return "Audio AAC Bitrate";
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:	return "Audio AC-3 Bitrate";
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:	return "Audio Playback";
	case V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK: return "Audio Multilingual Playback";
	case V4L2_CID_MPEG_VIDEO_ENCODING:	return "Video Encoding";
	case V4L2_CID_MPEG_VIDEO_ASPECT:	return "Video Aspect";
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:	return "Video B Frames";
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:	return "Video GOP Size";
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:	return "Video GOP Closure";
	case V4L2_CID_MPEG_VIDEO_PULLDOWN:	return "Video Pulldown";
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:	return "Video Bitrate Mode";
	case V4L2_CID_MPEG_VIDEO_CONSTANT_QUALITY:	return "Constant Quality";
	case V4L2_CID_MPEG_VIDEO_BITRATE:	return "Video Bitrate";
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:	return "Video Peak Bitrate";
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION: return "Video Temporal Decimation";
	case V4L2_CID_MPEG_VIDEO_MUTE:		return "Video Mute";
	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:	return "Video Mute YUV";
	case V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE:	return "Decoder Slice Interface";
	case V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER:	return "MPEG4 Loop Filter Enable";
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:	return "Number of Intra Refresh MBs";
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:		return "Frame Level Rate Control Enable";
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:			return "H264 MB Level Rate Control";
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:			return "Sequence Header Mode";
	case V4L2_CID_MPEG_VIDEO_MAX_REF_PIC:			return "Max Number of Reference Pics";
	case V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE:		return "Frame Skip Mode";
	case V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY:		return "Display Delay";
	case V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE:	return "Display Delay Enable";
	case V4L2_CID_MPEG_VIDEO_AU_DELIMITER:			return "Generate Access Unit Delimiters";
	case V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP:		return "H263 I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP:		return "H263 P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP:		return "H263 B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_MIN_QP:			return "H263 Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_MAX_QP:			return "H263 Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:		return "H264 I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:		return "H264 P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:		return "H264 B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:			return "H264 Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:			return "H264 Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:		return "H264 8x8 Transform Enable";
	case V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE:			return "H264 CPB Buffer Size";
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:		return "H264 Entropy Mode";
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:			return "H264 I-Frame Period";
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:			return "H264 Level";
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:	return "H264 Loop Filter Alpha Offset";
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:		return "H264 Loop Filter Beta Offset";
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:		return "H264 Loop Filter Mode";
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:			return "H264 Profile";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT:	return "Vertical Size of SAR";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH:	return "Horizontal Size of SAR";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:		return "Aspect Ratio VUI Enable";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:		return "VUI Aspect Ratio IDC";
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING:	return "H264 Enable Frame Packing SEI";
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0:	return "H264 Set Curr. Frame as Frame0";
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:	return "H264 FP Arrangement Type";
	case V4L2_CID_MPEG_VIDEO_H264_FMO:			return "H264 Flexible MB Ordering";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:		return "H264 Map Type for FMO";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP:		return "H264 FMO Number of Slice Groups";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION:	return "H264 FMO Direction of Change";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_RATE:		return "H264 FMO Size of 1st Slice Grp";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_RUN_LENGTH:		return "H264 FMO No. of Consecutive MBs";
	case V4L2_CID_MPEG_VIDEO_H264_ASO:			return "H264 Arbitrary Slice Ordering";
	case V4L2_CID_MPEG_VIDEO_H264_ASO_SLICE_ORDER:		return "H264 ASO Slice Order";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING:	return "Enable H264 Hierarchical Coding";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE:	return "H264 Hierarchical Coding Type";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER:return "H264 Number of HC Layers";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP:
								return "H264 Set QP Value for HC Layers";
	case V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION:
								return "H264 Constrained Intra Pred";
	case V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET:	return "H264 Chroma QP Index Offset";
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MIN_QP:		return "H264 I-Frame Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MAX_QP:		return "H264 I-Frame Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MIN_QP:		return "H264 P-Frame Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MAX_QP:		return "H264 P-Frame Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MIN_QP:		return "H264 B-Frame Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MAX_QP:		return "H264 B-Frame Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L0_BR:	return "H264 Hierarchical Lay 0 Bitrate";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L1_BR:	return "H264 Hierarchical Lay 1 Bitrate";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L2_BR:	return "H264 Hierarchical Lay 2 Bitrate";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L3_BR:	return "H264 Hierarchical Lay 3 Bitrate";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L4_BR:	return "H264 Hierarchical Lay 4 Bitrate";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L5_BR:	return "H264 Hierarchical Lay 5 Bitrate";
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L6_BR:	return "H264 Hierarchical Lay 6 Bitrate";
	case V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL:			return "MPEG2 Level";
	case V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE:			return "MPEG2 Profile";
	case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:		return "MPEG4 I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:		return "MPEG4 P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP:		return "MPEG4 B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP:			return "MPEG4 Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP:			return "MPEG4 Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:			return "MPEG4 Level";
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:			return "MPEG4 Profile";
	case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:			return "Quarter Pixel Search Enable";
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:		return "Maximum Bytes in a Slice";
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:		return "Number of MBs in a Slice";
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:		return "Slice Partitioning Method";
	case V4L2_CID_MPEG_VIDEO_VBV_SIZE:			return "VBV Buffer Size";
	case V4L2_CID_MPEG_VIDEO_DEC_PTS:			return "Video Decoder PTS";
	case V4L2_CID_MPEG_VIDEO_DEC_FRAME:			return "Video Decoder Frame Count";
	case V4L2_CID_MPEG_VIDEO_DEC_CONCEAL_COLOR:		return "Video Decoder Conceal Color";
	case V4L2_CID_MPEG_VIDEO_VBV_DELAY:			return "Initial Delay for VBV Control";
	case V4L2_CID_MPEG_VIDEO_MV_H_SEARCH_RANGE:		return "Horizontal MV Search Range";
	case V4L2_CID_MPEG_VIDEO_MV_V_SEARCH_RANGE:		return "Vertical MV Search Range";
	case V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER:		return "Repeat Sequence Header";
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:		return "Force Key Frame";
	case V4L2_CID_MPEG_VIDEO_BASELAYER_PRIORITY_ID:		return "Base Layer Priority ID";
	case V4L2_CID_MPEG_VIDEO_LTR_COUNT:			return "LTR Count";
	case V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX:		return "Frame LTR Index";
	case V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES:		return "Use LTR Frames";
	case V4L2_CID_FWHT_I_FRAME_QP:				return "FWHT I-Frame QP Value";
	case V4L2_CID_FWHT_P_FRAME_QP:				return "FWHT P-Frame QP Value";

	/* VPX controls */
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS:		return "VPX Number of Partitions";
	case V4L2_CID_MPEG_VIDEO_VPX_IMD_DISABLE_4X4:		return "VPX Intra Mode Decision Disable";
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES:		return "VPX No. of Refs for P Frame";
	case V4L2_CID_MPEG_VIDEO_VPX_FILTER_LEVEL:		return "VPX Loop Filter Level Range";
	case V4L2_CID_MPEG_VIDEO_VPX_FILTER_SHARPNESS:		return "VPX Deblocking Effect Control";
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD:	return "VPX Golden Frame Refresh Period";
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL:		return "VPX Golden Frame Indicator";
	case V4L2_CID_MPEG_VIDEO_VPX_MIN_QP:			return "VPX Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_VPX_MAX_QP:			return "VPX Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_VPX_I_FRAME_QP:		return "VPX I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_VPX_P_FRAME_QP:		return "VPX P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:			return "VP8 Profile";
	case V4L2_CID_MPEG_VIDEO_VP9_PROFILE:			return "VP9 Profile";
	case V4L2_CID_MPEG_VIDEO_VP9_LEVEL:			return "VP9 Level";

	/* HEVC controls */
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP:		return "HEVC I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP:		return "HEVC P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP:		return "HEVC B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP:			return "HEVC Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP:			return "HEVC Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MIN_QP:		return "HEVC I-Frame Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MAX_QP:		return "HEVC I-Frame Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MIN_QP:		return "HEVC P-Frame Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MAX_QP:		return "HEVC P-Frame Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MIN_QP:		return "HEVC B-Frame Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MAX_QP:		return "HEVC B-Frame Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:			return "HEVC Profile";
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:			return "HEVC Level";
	case V4L2_CID_MPEG_VIDEO_HEVC_TIER:			return "HEVC Tier";
	case V4L2_CID_MPEG_VIDEO_HEVC_FRAME_RATE_RESOLUTION:	return "HEVC Frame Rate Resolution";
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_PARTITION_DEPTH:	return "HEVC Maximum Coding Unit Depth";
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE:		return "HEVC Refresh Type";
	case V4L2_CID_MPEG_VIDEO_HEVC_CONST_INTRA_PRED:		return "HEVC Constant Intra Prediction";
	case V4L2_CID_MPEG_VIDEO_HEVC_LOSSLESS_CU:		return "HEVC Lossless Encoding";
	case V4L2_CID_MPEG_VIDEO_HEVC_WAVEFRONT:		return "HEVC Wavefront";
	case V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE:		return "HEVC Loop Filter";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_QP:			return "HEVC QP Values";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE:		return "HEVC Hierarchical Coding Type";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_LAYER:	return "HEVC Hierarchical Coding Layer";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L0_QP:	return "HEVC Hierarchical Layer 0 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L1_QP:	return "HEVC Hierarchical Layer 1 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L2_QP:	return "HEVC Hierarchical Layer 2 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L3_QP:	return "HEVC Hierarchical Layer 3 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L4_QP:	return "HEVC Hierarchical Layer 4 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L5_QP:	return "HEVC Hierarchical Layer 5 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L6_QP:	return "HEVC Hierarchical Layer 6 QP";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L0_BR:	return "HEVC Hierarchical Lay 0 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L1_BR:	return "HEVC Hierarchical Lay 1 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L2_BR:	return "HEVC Hierarchical Lay 2 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L3_BR:	return "HEVC Hierarchical Lay 3 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L4_BR:	return "HEVC Hierarchical Lay 4 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L5_BR:	return "HEVC Hierarchical Lay 5 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L6_BR:	return "HEVC Hierarchical Lay 6 BitRate";
	case V4L2_CID_MPEG_VIDEO_HEVC_GENERAL_PB:		return "HEVC General PB";
	case V4L2_CID_MPEG_VIDEO_HEVC_TEMPORAL_ID:		return "HEVC Temporal ID";
	case V4L2_CID_MPEG_VIDEO_HEVC_STRONG_SMOOTHING:		return "HEVC Strong Intra Smoothing";
	case V4L2_CID_MPEG_VIDEO_HEVC_INTRA_PU_SPLIT:		return "HEVC Intra PU Split";
	case V4L2_CID_MPEG_VIDEO_HEVC_TMV_PREDICTION:		return "HEVC TMV Prediction";
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_NUM_MERGE_MV_MINUS1:	return "HEVC Max Num of Candidate MVs";
	case V4L2_CID_MPEG_VIDEO_HEVC_WITHOUT_STARTCODE:	return "HEVC ENC Without Startcode";
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_PERIOD:		return "HEVC Num of I-Frame b/w 2 IDR";
	case V4L2_CID_MPEG_VIDEO_HEVC_LF_BETA_OFFSET_DIV2:	return "HEVC Loop Filter Beta Offset";
	case V4L2_CID_MPEG_VIDEO_HEVC_LF_TC_OFFSET_DIV2:	return "HEVC Loop Filter TC Offset";
	case V4L2_CID_MPEG_VIDEO_HEVC_SIZE_OF_LENGTH_FIELD:	return "HEVC Size of Length Field";
	case V4L2_CID_MPEG_VIDEO_REF_NUMBER_FOR_PFRAMES:	return "Reference Frames for a P-Frame";
	case V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR:		return "Prepend SPS and PPS to IDR";
	case V4L2_CID_MPEG_VIDEO_HEVC_SPS:			return "HEVC Sequence Parameter Set";
	case V4L2_CID_MPEG_VIDEO_HEVC_PPS:			return "HEVC Picture Parameter Set";
	case V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS:		return "HEVC Slice Parameters";
	case V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE:		return "HEVC Decode Mode";
	case V4L2_CID_MPEG_VIDEO_HEVC_START_CODE:		return "HEVC Start Code";

	/* CAMERA controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_CAMERA_CLASS:		return "Camera Controls";
	case V4L2_CID_EXPOSURE_AUTO:		return "Auto Exposure";
	case V4L2_CID_EXPOSURE_ABSOLUTE:	return "Exposure Time, Absolute";
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:	return "Exposure, Dynamic Framerate";
	case V4L2_CID_PAN_RELATIVE:		return "Pan, Relative";
	case V4L2_CID_TILT_RELATIVE:		return "Tilt, Relative";
	case V4L2_CID_PAN_RESET:		return "Pan, Reset";
	case V4L2_CID_TILT_RESET:		return "Tilt, Reset";
	case V4L2_CID_PAN_ABSOLUTE:		return "Pan, Absolute";
	case V4L2_CID_TILT_ABSOLUTE:		return "Tilt, Absolute";
	case V4L2_CID_FOCUS_ABSOLUTE:		return "Focus, Absolute";
	case V4L2_CID_FOCUS_RELATIVE:		return "Focus, Relative";
	case V4L2_CID_FOCUS_AUTO:		return "Focus, Automatic Continuous";
	case V4L2_CID_ZOOM_ABSOLUTE:		return "Zoom, Absolute";
	case V4L2_CID_ZOOM_RELATIVE:		return "Zoom, Relative";
	case V4L2_CID_ZOOM_CONTINUOUS:		return "Zoom, Continuous";
	case V4L2_CID_PRIVACY:			return "Privacy";
	case V4L2_CID_IRIS_ABSOLUTE:		return "Iris, Absolute";
	case V4L2_CID_IRIS_RELATIVE:		return "Iris, Relative";
	case V4L2_CID_AUTO_EXPOSURE_BIAS:	return "Auto Exposure, Bias";
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE: return "White Balance, Auto & Preset";
	case V4L2_CID_WIDE_DYNAMIC_RANGE:	return "Wide Dynamic Range";
	case V4L2_CID_IMAGE_STABILIZATION:	return "Image Stabilization";
	case V4L2_CID_ISO_SENSITIVITY:		return "ISO Sensitivity";
	case V4L2_CID_ISO_SENSITIVITY_AUTO:	return "ISO Sensitivity, Auto";
	case V4L2_CID_EXPOSURE_METERING:	return "Exposure, Metering Mode";
	case V4L2_CID_SCENE_MODE:		return "Scene Mode";
	case V4L2_CID_3A_LOCK:			return "3A Lock";
	case V4L2_CID_AUTO_FOCUS_START:		return "Auto Focus, Start";
	case V4L2_CID_AUTO_FOCUS_STOP:		return "Auto Focus, Stop";
	case V4L2_CID_AUTO_FOCUS_STATUS:	return "Auto Focus, Status";
	case V4L2_CID_AUTO_FOCUS_RANGE:		return "Auto Focus, Range";
	case V4L2_CID_PAN_SPEED:		return "Pan, Speed";
	case V4L2_CID_TILT_SPEED:		return "Tilt, Speed";
	case V4L2_CID_UNIT_CELL_SIZE:		return "Unit Cell Size";
	case V4L2_CID_CAMERA_ORIENTATION:	return "Camera Orientation";
	case V4L2_CID_CAMERA_SENSOR_ROTATION:	return "Camera Sensor Rotation";

	/* FM Radio Modulator controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_FM_TX_CLASS:		return "FM Radio Modulator Controls";
	case V4L2_CID_RDS_TX_DEVIATION:		return "RDS Signal Deviation";
	case V4L2_CID_RDS_TX_PI:		return "RDS Program ID";
	case V4L2_CID_RDS_TX_PTY:		return "RDS Program Type";
	case V4L2_CID_RDS_TX_PS_NAME:		return "RDS PS Name";
	case V4L2_CID_RDS_TX_RADIO_TEXT:	return "RDS Radio Text";
	case V4L2_CID_RDS_TX_MONO_STEREO:	return "RDS Stereo";
	case V4L2_CID_RDS_TX_ARTIFICIAL_HEAD:	return "RDS Artificial Head";
	case V4L2_CID_RDS_TX_COMPRESSED:	return "RDS Compressed";
	case V4L2_CID_RDS_TX_DYNAMIC_PTY:	return "RDS Dynamic PTY";
	case V4L2_CID_RDS_TX_TRAFFIC_ANNOUNCEMENT: return "RDS Traffic Announcement";
	case V4L2_CID_RDS_TX_TRAFFIC_PROGRAM:	return "RDS Traffic Program";
	case V4L2_CID_RDS_TX_MUSIC_SPEECH:	return "RDS Music";
	case V4L2_CID_RDS_TX_ALT_FREQS_ENABLE:	return "RDS Enable Alt Frequencies";
	case V4L2_CID_RDS_TX_ALT_FREQS:		return "RDS Alternate Frequencies";
	case V4L2_CID_AUDIO_LIMITER_ENABLED:	return "Audio Limiter Feature Enabled";
	case V4L2_CID_AUDIO_LIMITER_RELEASE_TIME: return "Audio Limiter Release Time";
	case V4L2_CID_AUDIO_LIMITER_DEVIATION:	return "Audio Limiter Deviation";
	case V4L2_CID_AUDIO_COMPRESSION_ENABLED: return "Audio Compression Enabled";
	case V4L2_CID_AUDIO_COMPRESSION_GAIN:	return "Audio Compression Gain";
	case V4L2_CID_AUDIO_COMPRESSION_THRESHOLD: return "Audio Compression Threshold";
	case V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME: return "Audio Compression Attack Time";
	case V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME: return "Audio Compression Release Time";
	case V4L2_CID_PILOT_TONE_ENABLED:	return "Pilot Tone Feature Enabled";
	case V4L2_CID_PILOT_TONE_DEVIATION:	return "Pilot Tone Deviation";
	case V4L2_CID_PILOT_TONE_FREQUENCY:	return "Pilot Tone Frequency";
	case V4L2_CID_TUNE_PREEMPHASIS:		return "Pre-Emphasis";
	case V4L2_CID_TUNE_POWER_LEVEL:		return "Tune Power Level";
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:	return "Tune Antenna Capacitor";

	/* Flash controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_FLASH_CLASS:		return "Flash Controls";
	case V4L2_CID_FLASH_LED_MODE:		return "LED Mode";
	case V4L2_CID_FLASH_STROBE_SOURCE:	return "Strobe Source";
	case V4L2_CID_FLASH_STROBE:		return "Strobe";
	case V4L2_CID_FLASH_STROBE_STOP:	return "Stop Strobe";
	case V4L2_CID_FLASH_STROBE_STATUS:	return "Strobe Status";
	case V4L2_CID_FLASH_TIMEOUT:		return "Strobe Timeout";
	case V4L2_CID_FLASH_INTENSITY:		return "Intensity, Flash Mode";
	case V4L2_CID_FLASH_TORCH_INTENSITY:	return "Intensity, Torch Mode";
	case V4L2_CID_FLASH_INDICATOR_INTENSITY: return "Intensity, Indicator";
	case V4L2_CID_FLASH_FAULT:		return "Faults";
	case V4L2_CID_FLASH_CHARGE:		return "Charge";
	case V4L2_CID_FLASH_READY:		return "Ready to Strobe";

	/* JPEG encoder controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_JPEG_CLASS:		return "JPEG Compression Controls";
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:	return "Chroma Subsampling";
	case V4L2_CID_JPEG_RESTART_INTERVAL:	return "Restart Interval";
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:	return "Compression Quality";
	case V4L2_CID_JPEG_ACTIVE_MARKER:	return "Active Markers";

	/* Image source controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_IMAGE_SOURCE_CLASS:	return "Image Source Controls";
	case V4L2_CID_VBLANK:			return "Vertical Blanking";
	case V4L2_CID_HBLANK:			return "Horizontal Blanking";
	case V4L2_CID_ANALOGUE_GAIN:		return "Analogue Gain";
	case V4L2_CID_TEST_PATTERN_RED:		return "Red Pixel Value";
	case V4L2_CID_TEST_PATTERN_GREENR:	return "Green (Red) Pixel Value";
	case V4L2_CID_TEST_PATTERN_BLUE:	return "Blue Pixel Value";
	case V4L2_CID_TEST_PATTERN_GREENB:	return "Green (Blue) Pixel Value";

	/* Image processing controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_IMAGE_PROC_CLASS:		return "Image Processing Controls";
	case V4L2_CID_LINK_FREQ:		return "Link Frequency";
	case V4L2_CID_PIXEL_RATE:		return "Pixel Rate";
	case V4L2_CID_TEST_PATTERN:		return "Test Pattern";
	case V4L2_CID_DEINTERLACING_MODE:	return "Deinterlacing Mode";
	case V4L2_CID_DIGITAL_GAIN:		return "Digital Gain";

	/* DV controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_DV_CLASS:			return "Digital Video Controls";
	case V4L2_CID_DV_TX_HOTPLUG:		return "Hotplug Present";
	case V4L2_CID_DV_TX_RXSENSE:		return "RxSense Present";
	case V4L2_CID_DV_TX_EDID_PRESENT:	return "EDID Present";
	case V4L2_CID_DV_TX_MODE:		return "Transmit Mode";
	case V4L2_CID_DV_TX_RGB_RANGE:		return "Tx RGB Quantization Range";
	case V4L2_CID_DV_TX_IT_CONTENT_TYPE:	return "Tx IT Content Type";
	case V4L2_CID_DV_RX_POWER_PRESENT:	return "Power Present";
	case V4L2_CID_DV_RX_RGB_RANGE:		return "Rx RGB Quantization Range";
	case V4L2_CID_DV_RX_IT_CONTENT_TYPE:	return "Rx IT Content Type";

	case V4L2_CID_FM_RX_CLASS:		return "FM Radio Receiver Controls";
	case V4L2_CID_TUNE_DEEMPHASIS:		return "De-Emphasis";
	case V4L2_CID_RDS_RECEPTION:		return "RDS Reception";
	case V4L2_CID_RF_TUNER_CLASS:		return "RF Tuner Controls";
	case V4L2_CID_RF_TUNER_RF_GAIN:		return "RF Gain";
	case V4L2_CID_RF_TUNER_LNA_GAIN_AUTO:	return "LNA Gain, Auto";
	case V4L2_CID_RF_TUNER_LNA_GAIN:	return "LNA Gain";
	case V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO:	return "Mixer Gain, Auto";
	case V4L2_CID_RF_TUNER_MIXER_GAIN:	return "Mixer Gain";
	case V4L2_CID_RF_TUNER_IF_GAIN_AUTO:	return "IF Gain, Auto";
	case V4L2_CID_RF_TUNER_IF_GAIN:		return "IF Gain";
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:	return "Bandwidth, Auto";
	case V4L2_CID_RF_TUNER_BANDWIDTH:	return "Bandwidth";
	case V4L2_CID_RF_TUNER_PLL_LOCK:	return "PLL Lock";
	case V4L2_CID_RDS_RX_PTY:		return "RDS Program Type";
	case V4L2_CID_RDS_RX_PS_NAME:		return "RDS PS Name";
	case V4L2_CID_RDS_RX_RADIO_TEXT:	return "RDS Radio Text";
	case V4L2_CID_RDS_RX_TRAFFIC_ANNOUNCEMENT: return "RDS Traffic Announcement";
	case V4L2_CID_RDS_RX_TRAFFIC_PROGRAM:	return "RDS Traffic Program";
	case V4L2_CID_RDS_RX_MUSIC_SPEECH:	return "RDS Music";

	/* Detection controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_DETECT_CLASS:		return "Detection Controls";
	case V4L2_CID_DETECT_MD_MODE:		return "Motion Detection Mode";
	case V4L2_CID_DETECT_MD_GLOBAL_THRESHOLD: return "MD Global Threshold";
	case V4L2_CID_DETECT_MD_THRESHOLD_GRID:	return "MD Threshold Grid";
	case V4L2_CID_DETECT_MD_REGION_GRID:	return "MD Region Grid";

	/* Stateless Codec controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_CODEC_STATELESS_CLASS:	return "Stateless Codec Controls";
	case V4L2_CID_STATELESS_H264_DECODE_MODE:		return "H264 Decode Mode";
	case V4L2_CID_STATELESS_H264_START_CODE:		return "H264 Start Code";
	case V4L2_CID_STATELESS_H264_SPS:			return "H264 Sequence Parameter Set";
	case V4L2_CID_STATELESS_H264_PPS:			return "H264 Picture Parameter Set";
	case V4L2_CID_STATELESS_H264_SCALING_MATRIX:		return "H264 Scaling Matrix";
	case V4L2_CID_STATELESS_H264_PRED_WEIGHTS:		return "H264 Prediction Weight Table";
	case V4L2_CID_STATELESS_H264_SLICE_PARAMS:		return "H264 Slice Parameters";
	case V4L2_CID_STATELESS_H264_DECODE_PARAMS:		return "H264 Decode Parameters";
	case V4L2_CID_STATELESS_FWHT_PARAMS:			return "FWHT Stateless Parameters";
	case V4L2_CID_STATELESS_VP8_FRAME:			return "VP8 Frame Parameters";
	case V4L2_CID_STATELESS_MPEG2_SEQUENCE:			return "MPEG-2 Sequence Header";
	case V4L2_CID_STATELESS_MPEG2_PICTURE:			return "MPEG-2 Picture Header";
	case V4L2_CID_STATELESS_MPEG2_QUANTISATION:		return "MPEG-2 Quantisation Matrices";

	/* Colorimetry controls */
	/* Keep the order of the 'case's the same as in v4l2-controls.h! */
	case V4L2_CID_COLORIMETRY_CLASS:	return "Colorimetry Controls";
	case V4L2_CID_COLORIMETRY_HDR10_CLL_INFO:		return "HDR10 Content Light Info";
	case V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY:	return "HDR10 Mastering Display";
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_name);

void v4l2_ctrl_fill(u32 id, const char **name, enum v4l2_ctrl_type *type,
		    s64 *min, s64 *max, u64 *step, s64 *def, u32 *flags)
{
	*name = v4l2_ctrl_get_name(id);
	*flags = 0;

	switch (id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_LOUDNESS:
	case V4L2_CID_AUTO_WHITE_BALANCE:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HUE_AUTO:
	case V4L2_CID_CHROMA_AGC:
	case V4L2_CID_COLOR_KILLER:
	case V4L2_CID_AUTOBRIGHTNESS:
	case V4L2_CID_MPEG_AUDIO_MUTE:
	case V4L2_CID_MPEG_VIDEO_MUTE:
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
	case V4L2_CID_MPEG_VIDEO_PULLDOWN:
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
	case V4L2_CID_FOCUS_AUTO:
	case V4L2_CID_PRIVACY:
	case V4L2_CID_AUDIO_LIMITER_ENABLED:
	case V4L2_CID_AUDIO_COMPRESSION_ENABLED:
	case V4L2_CID_PILOT_TONE_ENABLED:
	case V4L2_CID_ILLUMINATORS_1:
	case V4L2_CID_ILLUMINATORS_2:
	case V4L2_CID_FLASH_STROBE_STATUS:
	case V4L2_CID_FLASH_CHARGE:
	case V4L2_CID_FLASH_READY:
	case V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER:
	case V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE:
	case V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE:
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:
	case V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER:
	case V4L2_CID_MPEG_VIDEO_AU_DELIMITER:
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
	case V4L2_CID_IMAGE_STABILIZATION:
	case V4L2_CID_RDS_RECEPTION:
	case V4L2_CID_RF_TUNER_LNA_GAIN_AUTO:
	case V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO:
	case V4L2_CID_RF_TUNER_IF_GAIN_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_PLL_LOCK:
	case V4L2_CID_RDS_TX_MONO_STEREO:
	case V4L2_CID_RDS_TX_ARTIFICIAL_HEAD:
	case V4L2_CID_RDS_TX_COMPRESSED:
	case V4L2_CID_RDS_TX_DYNAMIC_PTY:
	case V4L2_CID_RDS_TX_TRAFFIC_ANNOUNCEMENT:
	case V4L2_CID_RDS_TX_TRAFFIC_PROGRAM:
	case V4L2_CID_RDS_TX_MUSIC_SPEECH:
	case V4L2_CID_RDS_TX_ALT_FREQS_ENABLE:
	case V4L2_CID_RDS_RX_TRAFFIC_ANNOUNCEMENT:
	case V4L2_CID_RDS_RX_TRAFFIC_PROGRAM:
	case V4L2_CID_RDS_RX_MUSIC_SPEECH:
		*type = V4L2_CTRL_TYPE_BOOLEAN;
		*min = 0;
		*max = *step = 1;
		break;
	case V4L2_CID_ROTATE:
		*type = V4L2_CTRL_TYPE_INTEGER;
		*flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
		break;
	case V4L2_CID_MPEG_VIDEO_MV_H_SEARCH_RANGE:
	case V4L2_CID_MPEG_VIDEO_MV_V_SEARCH_RANGE:
	case V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY:
		*type = V4L2_CTRL_TYPE_INTEGER;
		break;
	case V4L2_CID_MPEG_VIDEO_LTR_COUNT:
		*type = V4L2_CTRL_TYPE_INTEGER;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX:
		*type = V4L2_CTRL_TYPE_INTEGER;
		*flags |= V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		break;
	case V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES:
		*type = V4L2_CTRL_TYPE_BITMASK;
		*flags |= V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
	case V4L2_CID_PAN_RESET:
	case V4L2_CID_TILT_RESET:
	case V4L2_CID_FLASH_STROBE:
	case V4L2_CID_FLASH_STROBE_STOP:
	case V4L2_CID_AUTO_FOCUS_START:
	case V4L2_CID_AUTO_FOCUS_STOP:
	case V4L2_CID_DO_WHITE_BALANCE:
		*type = V4L2_CTRL_TYPE_BUTTON;
		*flags |= V4L2_CTRL_FLAG_WRITE_ONLY |
			  V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		*min = *max = *step = *def = 0;
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
	case V4L2_CID_MPEG_AUDIO_ENCODING:
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
	case V4L2_CID_MPEG_AUDIO_MODE:
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
	case V4L2_CID_MPEG_AUDIO_CRC:
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:
	case V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK:
	case V4L2_CID_MPEG_VIDEO_ENCODING:
	case V4L2_CID_MPEG_VIDEO_ASPECT:
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
	case V4L2_CID_MPEG_STREAM_TYPE:
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
	case V4L2_CID_EXPOSURE_AUTO:
	case V4L2_CID_AUTO_FOCUS_RANGE:
	case V4L2_CID_COLORFX:
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
	case V4L2_CID_TUNE_PREEMPHASIS:
	case V4L2_CID_FLASH_LED_MODE:
	case V4L2_CID_FLASH_STROBE_SOURCE:
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
	case V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE:
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE:
	case V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL:
	case V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE:
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
	case V4L2_CID_EXPOSURE_METERING:
	case V4L2_CID_SCENE_MODE:
	case V4L2_CID_DV_TX_MODE:
	case V4L2_CID_DV_TX_RGB_RANGE:
	case V4L2_CID_DV_TX_IT_CONTENT_TYPE:
	case V4L2_CID_DV_RX_RGB_RANGE:
	case V4L2_CID_DV_RX_IT_CONTENT_TYPE:
	case V4L2_CID_TEST_PATTERN:
	case V4L2_CID_DEINTERLACING_MODE:
	case V4L2_CID_TUNE_DEEMPHASIS:
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL:
	case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:
	case V4L2_CID_MPEG_VIDEO_VP9_PROFILE:
	case V4L2_CID_MPEG_VIDEO_VP9_LEVEL:
	case V4L2_CID_DETECT_MD_MODE:
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE:
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE:
	case V4L2_CID_MPEG_VIDEO_HEVC_SIZE_OF_LENGTH_FIELD:
	case V4L2_CID_MPEG_VIDEO_HEVC_TIER:
	case V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE:
	case V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE:
	case V4L2_CID_MPEG_VIDEO_HEVC_START_CODE:
	case V4L2_CID_STATELESS_H264_DECODE_MODE:
	case V4L2_CID_STATELESS_H264_START_CODE:
	case V4L2_CID_CAMERA_ORIENTATION:
		*type = V4L2_CTRL_TYPE_MENU;
		break;
	case V4L2_CID_LINK_FREQ:
		*type = V4L2_CTRL_TYPE_INTEGER_MENU;
		break;
	case V4L2_CID_RDS_TX_PS_NAME:
	case V4L2_CID_RDS_TX_RADIO_TEXT:
	case V4L2_CID_RDS_RX_PS_NAME:
	case V4L2_CID_RDS_RX_RADIO_TEXT:
		*type = V4L2_CTRL_TYPE_STRING;
		break;
	case V4L2_CID_ISO_SENSITIVITY:
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS:
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES:
		*type = V4L2_CTRL_TYPE_INTEGER_MENU;
		break;
	case V4L2_CID_USER_CLASS:
	case V4L2_CID_CAMERA_CLASS:
	case V4L2_CID_CODEC_CLASS:
	case V4L2_CID_FM_TX_CLASS:
	case V4L2_CID_FLASH_CLASS:
	case V4L2_CID_JPEG_CLASS:
	case V4L2_CID_IMAGE_SOURCE_CLASS:
	case V4L2_CID_IMAGE_PROC_CLASS:
	case V4L2_CID_DV_CLASS:
	case V4L2_CID_FM_RX_CLASS:
	case V4L2_CID_RF_TUNER_CLASS:
	case V4L2_CID_DETECT_CLASS:
	case V4L2_CID_CODEC_STATELESS_CLASS:
	case V4L2_CID_COLORIMETRY_CLASS:
		*type = V4L2_CTRL_TYPE_CTRL_CLASS;
		/* You can neither read nor write these */
		*flags |= V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY;
		*min = *max = *step = *def = 0;
		break;
	case V4L2_CID_BG_COLOR:
		*type = V4L2_CTRL_TYPE_INTEGER;
		*step = 1;
		*min = 0;
		/* Max is calculated as RGB888 that is 2^24 */
		*max = 0xFFFFFF;
		break;
	case V4L2_CID_FLASH_FAULT:
	case V4L2_CID_JPEG_ACTIVE_MARKER:
	case V4L2_CID_3A_LOCK:
	case V4L2_CID_AUTO_FOCUS_STATUS:
	case V4L2_CID_DV_TX_HOTPLUG:
	case V4L2_CID_DV_TX_RXSENSE:
	case V4L2_CID_DV_TX_EDID_PRESENT:
	case V4L2_CID_DV_RX_POWER_PRESENT:
		*type = V4L2_CTRL_TYPE_BITMASK;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		*type = V4L2_CTRL_TYPE_INTEGER;
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	case V4L2_CID_MPEG_VIDEO_DEC_PTS:
		*type = V4L2_CTRL_TYPE_INTEGER64;
		*flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;
		*min = *def = 0;
		*max = 0x1ffffffffLL;
		*step = 1;
		break;
	case V4L2_CID_MPEG_VIDEO_DEC_FRAME:
		*type = V4L2_CTRL_TYPE_INTEGER64;
		*flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;
		*min = *def = 0;
		*max = 0x7fffffffffffffffLL;
		*step = 1;
		break;
	case V4L2_CID_MPEG_VIDEO_DEC_CONCEAL_COLOR:
		*type = V4L2_CTRL_TYPE_INTEGER64;
		*min = 0;
		/* default for 8 bit black, luma is 16, chroma is 128 */
		*def = 0x8000800010LL;
		*max = 0xffffffffffffLL;
		*step = 1;
		break;
	case V4L2_CID_PIXEL_RATE:
		*type = V4L2_CTRL_TYPE_INTEGER64;
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	case V4L2_CID_DETECT_MD_REGION_GRID:
		*type = V4L2_CTRL_TYPE_U8;
		break;
	case V4L2_CID_DETECT_MD_THRESHOLD_GRID:
		*type = V4L2_CTRL_TYPE_U16;
		break;
	case V4L2_CID_RDS_TX_ALT_FREQS:
		*type = V4L2_CTRL_TYPE_U32;
		break;
	case V4L2_CID_STATELESS_MPEG2_SEQUENCE:
		*type = V4L2_CTRL_TYPE_MPEG2_SEQUENCE;
		break;
	case V4L2_CID_STATELESS_MPEG2_PICTURE:
		*type = V4L2_CTRL_TYPE_MPEG2_PICTURE;
		break;
	case V4L2_CID_STATELESS_MPEG2_QUANTISATION:
		*type = V4L2_CTRL_TYPE_MPEG2_QUANTISATION;
		break;
	case V4L2_CID_STATELESS_FWHT_PARAMS:
		*type = V4L2_CTRL_TYPE_FWHT_PARAMS;
		break;
	case V4L2_CID_STATELESS_H264_SPS:
		*type = V4L2_CTRL_TYPE_H264_SPS;
		break;
	case V4L2_CID_STATELESS_H264_PPS:
		*type = V4L2_CTRL_TYPE_H264_PPS;
		break;
	case V4L2_CID_STATELESS_H264_SCALING_MATRIX:
		*type = V4L2_CTRL_TYPE_H264_SCALING_MATRIX;
		break;
	case V4L2_CID_STATELESS_H264_SLICE_PARAMS:
		*type = V4L2_CTRL_TYPE_H264_SLICE_PARAMS;
		break;
	case V4L2_CID_STATELESS_H264_DECODE_PARAMS:
		*type = V4L2_CTRL_TYPE_H264_DECODE_PARAMS;
		break;
	case V4L2_CID_STATELESS_H264_PRED_WEIGHTS:
		*type = V4L2_CTRL_TYPE_H264_PRED_WEIGHTS;
		break;
	case V4L2_CID_STATELESS_VP8_FRAME:
		*type = V4L2_CTRL_TYPE_VP8_FRAME;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_SPS:
		*type = V4L2_CTRL_TYPE_HEVC_SPS;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_PPS:
		*type = V4L2_CTRL_TYPE_HEVC_PPS;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS:
		*type = V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS;
		break;
	case V4L2_CID_UNIT_CELL_SIZE:
		*type = V4L2_CTRL_TYPE_AREA;
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	case V4L2_CID_COLORIMETRY_HDR10_CLL_INFO:
		*type = V4L2_CTRL_TYPE_HDR10_CLL_INFO;
		break;
	case V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY:
		*type = V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY;
		break;
	default:
		*type = V4L2_CTRL_TYPE_INTEGER;
		break;
	}
	switch (id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
	case V4L2_CID_MPEG_AUDIO_MODE:
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
	case V4L2_CID_MPEG_STREAM_TYPE:
		*flags |= V4L2_CTRL_FLAG_UPDATE;
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
	case V4L2_CID_GAMMA:
	case V4L2_CID_SHARPNESS:
	case V4L2_CID_CHROMA_GAIN:
	case V4L2_CID_RDS_TX_DEVIATION:
	case V4L2_CID_AUDIO_LIMITER_RELEASE_TIME:
	case V4L2_CID_AUDIO_LIMITER_DEVIATION:
	case V4L2_CID_AUDIO_COMPRESSION_GAIN:
	case V4L2_CID_AUDIO_COMPRESSION_THRESHOLD:
	case V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME:
	case V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME:
	case V4L2_CID_PILOT_TONE_DEVIATION:
	case V4L2_CID_PILOT_TONE_FREQUENCY:
	case V4L2_CID_TUNE_POWER_LEVEL:
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
	case V4L2_CID_RF_TUNER_RF_GAIN:
	case V4L2_CID_RF_TUNER_LNA_GAIN:
	case V4L2_CID_RF_TUNER_MIXER_GAIN:
	case V4L2_CID_RF_TUNER_IF_GAIN:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
	case V4L2_CID_DETECT_MD_GLOBAL_THRESHOLD:
		*flags |= V4L2_CTRL_FLAG_SLIDER;
		break;
	case V4L2_CID_PAN_RELATIVE:
	case V4L2_CID_TILT_RELATIVE:
	case V4L2_CID_FOCUS_RELATIVE:
	case V4L2_CID_IRIS_RELATIVE:
	case V4L2_CID_ZOOM_RELATIVE:
		*flags |= V4L2_CTRL_FLAG_WRITE_ONLY |
			  V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		break;
	case V4L2_CID_FLASH_STROBE_STATUS:
	case V4L2_CID_AUTO_FOCUS_STATUS:
	case V4L2_CID_FLASH_READY:
	case V4L2_CID_DV_TX_HOTPLUG:
	case V4L2_CID_DV_TX_RXSENSE:
	case V4L2_CID_DV_TX_EDID_PRESENT:
	case V4L2_CID_DV_RX_POWER_PRESENT:
	case V4L2_CID_DV_RX_IT_CONTENT_TYPE:
	case V4L2_CID_RDS_RX_PTY:
	case V4L2_CID_RDS_RX_PS_NAME:
	case V4L2_CID_RDS_RX_RADIO_TEXT:
	case V4L2_CID_RDS_RX_TRAFFIC_ANNOUNCEMENT:
	case V4L2_CID_RDS_RX_TRAFFIC_PROGRAM:
	case V4L2_CID_RDS_RX_MUSIC_SPEECH:
	case V4L2_CID_CAMERA_ORIENTATION:
	case V4L2_CID_CAMERA_SENSOR_ROTATION:
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	case V4L2_CID_RF_TUNER_PLL_LOCK:
		*flags |= V4L2_CTRL_FLAG_VOLATILE;
		break;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_fill);
