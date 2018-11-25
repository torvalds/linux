/*
    V4L2 controls framework implementation.

    Copyright (C) 2010  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dev.h>

#define has_op(master, op) \
	(master->ops && master->ops->op)
#define call_op(master, op) \
	(has_op(master, op) ? master->ops->op(master) : 0)

/* Internal temporary helper struct, one for each v4l2_ext_control */
struct v4l2_ctrl_helper {
	/* Pointer to the control reference of the master control */
	struct v4l2_ctrl_ref *mref;
	/* The control ref corresponding to the v4l2_ext_control ID field. */
	struct v4l2_ctrl_ref *ref;
	/* v4l2_ext_control index of the next control belonging to the
	   same cluster, or 0 if there isn't any. */
	u32 next;
};

/* Small helper function to determine if the autocluster is set to manual
   mode. */
static bool is_cur_manual(const struct v4l2_ctrl *master)
{
	return master->is_auto && master->cur.val == master->manual_mode_value;
}

/* Same as above, but this checks the against the new value instead of the
   current value. */
static bool is_new_manual(const struct v4l2_ctrl *master)
{
	return master->is_auto && master->val == master->manual_mode_value;
}

/* Returns NULL or a character pointer array containing the menu for
   the given control ID. The pointer array ends with a NULL pointer.
   An empty string signifies a menu entry that is invalid. This allows
   drivers to disable certain options if it is not supported. */
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

	default:
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_menu);

#define __v4l2_qmenu_int_len(arr, len) ({ *(len) = ARRAY_SIZE(arr); arr; })
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

	/* Codec controls */
	/* The MPEG controls are applicable to all codec controls
	 * and the 'MPEG' part of the define is historical */
	/* Keep the order of the 'case's the same as in videodev2.h! */
	case V4L2_CID_MPEG_CLASS:		return "Codec Controls";
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
	case V4L2_CID_MPEG_VIDEO_VBV_DELAY:			return "Initial Delay for VBV Control";
	case V4L2_CID_MPEG_VIDEO_MV_H_SEARCH_RANGE:		return "Horizontal MV Search Range";
	case V4L2_CID_MPEG_VIDEO_MV_V_SEARCH_RANGE:		return "Vertical MV Search Range";
	case V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER:		return "Repeat Sequence Header";
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:		return "Force Key Frame";
	case V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS:		return "MPEG-2 Slice Parameters";
	case V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION:		return "MPEG-2 Quantization Matrices";

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

	/* HEVC controls */
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP:		return "HEVC I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP:		return "HEVC P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP:		return "HEVC B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP:			return "HEVC Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP:			return "HEVC Maximum QP Value";
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
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:
	case V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER:
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
		*type = V4L2_CTRL_TYPE_INTEGER;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
	case V4L2_CID_PAN_RESET:
	case V4L2_CID_TILT_RESET:
	case V4L2_CID_FLASH_STROBE:
	case V4L2_CID_FLASH_STROBE_STOP:
	case V4L2_CID_AUTO_FOCUS_START:
	case V4L2_CID_AUTO_FOCUS_STOP:
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
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:
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
	case V4L2_CID_DETECT_MD_MODE:
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE:
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE:
	case V4L2_CID_MPEG_VIDEO_HEVC_SIZE_OF_LENGTH_FIELD:
	case V4L2_CID_MPEG_VIDEO_HEVC_TIER:
	case V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE:
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
	case V4L2_CID_MPEG_CLASS:
	case V4L2_CID_FM_TX_CLASS:
	case V4L2_CID_FLASH_CLASS:
	case V4L2_CID_JPEG_CLASS:
	case V4L2_CID_IMAGE_SOURCE_CLASS:
	case V4L2_CID_IMAGE_PROC_CLASS:
	case V4L2_CID_DV_CLASS:
	case V4L2_CID_FM_RX_CLASS:
	case V4L2_CID_RF_TUNER_CLASS:
	case V4L2_CID_DETECT_CLASS:
		*type = V4L2_CTRL_TYPE_CTRL_CLASS;
		/* You can neither read not write these */
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
	case V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS:
		*type = V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION:
		*type = V4L2_CTRL_TYPE_MPEG2_QUANTIZATION;
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
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	case V4L2_CID_RF_TUNER_PLL_LOCK:
		*flags |= V4L2_CTRL_FLAG_VOLATILE;
		break;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_fill);

static u32 user_flags(const struct v4l2_ctrl *ctrl)
{
	u32 flags = ctrl->flags;

	if (ctrl->is_ptr)
		flags |= V4L2_CTRL_FLAG_HAS_PAYLOAD;

	return flags;
}

static void fill_event(struct v4l2_event *ev, struct v4l2_ctrl *ctrl, u32 changes)
{
	memset(ev->reserved, 0, sizeof(ev->reserved));
	ev->type = V4L2_EVENT_CTRL;
	ev->id = ctrl->id;
	ev->u.ctrl.changes = changes;
	ev->u.ctrl.type = ctrl->type;
	ev->u.ctrl.flags = user_flags(ctrl);
	if (ctrl->is_ptr)
		ev->u.ctrl.value64 = 0;
	else
		ev->u.ctrl.value64 = *ctrl->p_cur.p_s64;
	ev->u.ctrl.minimum = ctrl->minimum;
	ev->u.ctrl.maximum = ctrl->maximum;
	if (ctrl->type == V4L2_CTRL_TYPE_MENU
	    || ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU)
		ev->u.ctrl.step = 1;
	else
		ev->u.ctrl.step = ctrl->step;
	ev->u.ctrl.default_value = ctrl->default_value;
}

static void send_event(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 changes)
{
	struct v4l2_event ev;
	struct v4l2_subscribed_event *sev;

	if (list_empty(&ctrl->ev_subs))
		return;
	fill_event(&ev, ctrl, changes);

	list_for_each_entry(sev, &ctrl->ev_subs, node)
		if (sev->fh != fh ||
		    (sev->flags & V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK))
			v4l2_event_queue_fh(sev->fh, &ev);
}

static bool std_equal(const struct v4l2_ctrl *ctrl, u32 idx,
		      union v4l2_ctrl_ptr ptr1,
		      union v4l2_ctrl_ptr ptr2)
{
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BUTTON:
		return false;
	case V4L2_CTRL_TYPE_STRING:
		idx *= ctrl->elem_size;
		/* strings are always 0-terminated */
		return !strcmp(ptr1.p_char + idx, ptr2.p_char + idx);
	case V4L2_CTRL_TYPE_INTEGER64:
		return ptr1.p_s64[idx] == ptr2.p_s64[idx];
	case V4L2_CTRL_TYPE_U8:
		return ptr1.p_u8[idx] == ptr2.p_u8[idx];
	case V4L2_CTRL_TYPE_U16:
		return ptr1.p_u16[idx] == ptr2.p_u16[idx];
	case V4L2_CTRL_TYPE_U32:
		return ptr1.p_u32[idx] == ptr2.p_u32[idx];
	default:
		if (ctrl->is_int)
			return ptr1.p_s32[idx] == ptr2.p_s32[idx];
		idx *= ctrl->elem_size;
		return !memcmp(ptr1.p + idx, ptr2.p + idx, ctrl->elem_size);
	}
}

static void std_init(const struct v4l2_ctrl *ctrl, u32 idx,
		     union v4l2_ctrl_ptr ptr)
{
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		idx *= ctrl->elem_size;
		memset(ptr.p_char + idx, ' ', ctrl->minimum);
		ptr.p_char[idx + ctrl->minimum] = '\0';
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		ptr.p_s64[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_BITMASK:
	case V4L2_CTRL_TYPE_BOOLEAN:
		ptr.p_s32[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_U8:
		ptr.p_u8[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_U16:
		ptr.p_u16[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_U32:
		ptr.p_u32[idx] = ctrl->default_value;
		break;
	default:
		idx *= ctrl->elem_size;
		memset(ptr.p + idx, 0, ctrl->elem_size);
		break;
	}
}

static void std_log(const struct v4l2_ctrl *ctrl)
{
	union v4l2_ctrl_ptr ptr = ctrl->p_cur;

	if (ctrl->is_array) {
		unsigned i;

		for (i = 0; i < ctrl->nr_of_dims; i++)
			pr_cont("[%u]", ctrl->dims[i]);
		pr_cont(" ");
	}

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
		pr_cont("%d", *ptr.p_s32);
		break;
	case V4L2_CTRL_TYPE_BOOLEAN:
		pr_cont("%s", *ptr.p_s32 ? "true" : "false");
		break;
	case V4L2_CTRL_TYPE_MENU:
		pr_cont("%s", ctrl->qmenu[*ptr.p_s32]);
		break;
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		pr_cont("%lld", ctrl->qmenu_int[*ptr.p_s32]);
		break;
	case V4L2_CTRL_TYPE_BITMASK:
		pr_cont("0x%08x", *ptr.p_s32);
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		pr_cont("%lld", *ptr.p_s64);
		break;
	case V4L2_CTRL_TYPE_STRING:
		pr_cont("%s", ptr.p_char);
		break;
	case V4L2_CTRL_TYPE_U8:
		pr_cont("%u", (unsigned)*ptr.p_u8);
		break;
	case V4L2_CTRL_TYPE_U16:
		pr_cont("%u", (unsigned)*ptr.p_u16);
		break;
	case V4L2_CTRL_TYPE_U32:
		pr_cont("%u", (unsigned)*ptr.p_u32);
		break;
	default:
		pr_cont("unknown type %d", ctrl->type);
		break;
	}
}

/*
 * Round towards the closest legal value. Be careful when we are
 * close to the maximum range of the control type to prevent
 * wrap-arounds.
 */
#define ROUND_TO_RANGE(val, offset_type, ctrl)			\
({								\
	offset_type offset;					\
	if ((ctrl)->maximum >= 0 &&				\
	    val >= (ctrl)->maximum - (s32)((ctrl)->step / 2))	\
		val = (ctrl)->maximum;				\
	else							\
		val += (s32)((ctrl)->step / 2);			\
	val = clamp_t(typeof(val), val,				\
		      (ctrl)->minimum, (ctrl)->maximum);	\
	offset = (val) - (ctrl)->minimum;			\
	offset = (ctrl)->step * (offset / (u32)(ctrl)->step);	\
	val = (ctrl)->minimum + offset;				\
	0;							\
})

/* Validate a new control */
static int std_validate(const struct v4l2_ctrl *ctrl, u32 idx,
			union v4l2_ctrl_ptr ptr)
{
	struct v4l2_ctrl_mpeg2_slice_params *p_mpeg2_slice_params;
	size_t len;
	u64 offset;
	s64 val;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
		return ROUND_TO_RANGE(ptr.p_s32[idx], u32, ctrl);
	case V4L2_CTRL_TYPE_INTEGER64:
		/*
		 * We can't use the ROUND_TO_RANGE define here due to
		 * the u64 divide that needs special care.
		 */
		val = ptr.p_s64[idx];
		if (ctrl->maximum >= 0 && val >= ctrl->maximum - (s64)(ctrl->step / 2))
			val = ctrl->maximum;
		else
			val += (s64)(ctrl->step / 2);
		val = clamp_t(s64, val, ctrl->minimum, ctrl->maximum);
		offset = val - ctrl->minimum;
		do_div(offset, ctrl->step);
		ptr.p_s64[idx] = ctrl->minimum + offset * ctrl->step;
		return 0;
	case V4L2_CTRL_TYPE_U8:
		return ROUND_TO_RANGE(ptr.p_u8[idx], u8, ctrl);
	case V4L2_CTRL_TYPE_U16:
		return ROUND_TO_RANGE(ptr.p_u16[idx], u16, ctrl);
	case V4L2_CTRL_TYPE_U32:
		return ROUND_TO_RANGE(ptr.p_u32[idx], u32, ctrl);

	case V4L2_CTRL_TYPE_BOOLEAN:
		ptr.p_s32[idx] = !!ptr.p_s32[idx];
		return 0;

	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (ptr.p_s32[idx] < ctrl->minimum || ptr.p_s32[idx] > ctrl->maximum)
			return -ERANGE;
		if (ctrl->menu_skip_mask & (1 << ptr.p_s32[idx]))
			return -EINVAL;
		if (ctrl->type == V4L2_CTRL_TYPE_MENU &&
		    ctrl->qmenu[ptr.p_s32[idx]][0] == '\0')
			return -EINVAL;
		return 0;

	case V4L2_CTRL_TYPE_BITMASK:
		ptr.p_s32[idx] &= ctrl->maximum;
		return 0;

	case V4L2_CTRL_TYPE_BUTTON:
	case V4L2_CTRL_TYPE_CTRL_CLASS:
		ptr.p_s32[idx] = 0;
		return 0;

	case V4L2_CTRL_TYPE_STRING:
		idx *= ctrl->elem_size;
		len = strlen(ptr.p_char + idx);
		if (len < ctrl->minimum)
			return -ERANGE;
		if ((len - (u32)ctrl->minimum) % (u32)ctrl->step)
			return -ERANGE;
		return 0;

	case V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS:
		p_mpeg2_slice_params = ptr.p;

		switch (p_mpeg2_slice_params->sequence.chroma_format) {
		case 1: /* 4:2:0 */
		case 2: /* 4:2:2 */
		case 3: /* 4:4:4 */
			break;
		default:
			return -EINVAL;
		}

		switch (p_mpeg2_slice_params->picture.intra_dc_precision) {
		case 0: /* 8 bits */
		case 1: /* 9 bits */
		case 2: /* 10 bits */
		case 3: /* 11 bits */
			break;
		default:
			return -EINVAL;
		}

		switch (p_mpeg2_slice_params->picture.picture_structure) {
		case 1: /* interlaced top field */
		case 2: /* interlaced bottom field */
		case 3: /* progressive */
			break;
		default:
			return -EINVAL;
		}

		switch (p_mpeg2_slice_params->picture.picture_coding_type) {
		case V4L2_MPEG2_PICTURE_CODING_TYPE_I:
		case V4L2_MPEG2_PICTURE_CODING_TYPE_P:
		case V4L2_MPEG2_PICTURE_CODING_TYPE_B:
			break;
		default:
			return -EINVAL;
		}

		if (p_mpeg2_slice_params->backward_ref_index >= VIDEO_MAX_FRAME ||
		    p_mpeg2_slice_params->forward_ref_index >= VIDEO_MAX_FRAME)
			return -EINVAL;

		if (p_mpeg2_slice_params->pad ||
		    p_mpeg2_slice_params->picture.pad ||
		    p_mpeg2_slice_params->sequence.pad)
			return -EINVAL;

		return 0;

	case V4L2_CTRL_TYPE_MPEG2_QUANTIZATION:
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_type_ops std_type_ops = {
	.equal = std_equal,
	.init = std_init,
	.log = std_log,
	.validate = std_validate,
};

/* Helper function: copy the given control value back to the caller */
static int ptr_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl,
		       union v4l2_ctrl_ptr ptr)
{
	u32 len;

	if (ctrl->is_ptr && !ctrl->is_string)
		return copy_to_user(c->ptr, ptr.p, c->size) ?
		       -EFAULT : 0;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		len = strlen(ptr.p_char);
		if (c->size < len + 1) {
			c->size = ctrl->elem_size;
			return -ENOSPC;
		}
		return copy_to_user(c->string, ptr.p_char, len + 1) ?
		       -EFAULT : 0;
	case V4L2_CTRL_TYPE_INTEGER64:
		c->value64 = *ptr.p_s64;
		break;
	default:
		c->value = *ptr.p_s32;
		break;
	}
	return 0;
}

/* Helper function: copy the current control value back to the caller */
static int cur_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	return ptr_to_user(c, ctrl, ctrl->p_cur);
}

/* Helper function: copy the new control value back to the caller */
static int new_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	return ptr_to_user(c, ctrl, ctrl->p_new);
}

/* Helper function: copy the request value back to the caller */
static int req_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl_ref *ref)
{
	return ptr_to_user(c, ref->ctrl, ref->p_req);
}

/* Helper function: copy the initial control value back to the caller */
static int def_to_user(struct v4l2_ext_control *c, struct v4l2_ctrl *ctrl)
{
	int idx;

	for (idx = 0; idx < ctrl->elems; idx++)
		ctrl->type_ops->init(ctrl, idx, ctrl->p_new);

	return ptr_to_user(c, ctrl, ctrl->p_new);
}

/* Helper function: copy the caller-provider value to the given control value */
static int user_to_ptr(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl,
		       union v4l2_ctrl_ptr ptr)
{
	int ret;
	u32 size;

	ctrl->is_new = 1;
	if (ctrl->is_ptr && !ctrl->is_string) {
		unsigned idx;

		ret = copy_from_user(ptr.p, c->ptr, c->size) ? -EFAULT : 0;
		if (ret || !ctrl->is_array)
			return ret;
		for (idx = c->size / ctrl->elem_size; idx < ctrl->elems; idx++)
			ctrl->type_ops->init(ctrl, idx, ptr);
		return 0;
	}

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER64:
		*ptr.p_s64 = c->value64;
		break;
	case V4L2_CTRL_TYPE_STRING:
		size = c->size;
		if (size == 0)
			return -ERANGE;
		if (size > ctrl->maximum + 1)
			size = ctrl->maximum + 1;
		ret = copy_from_user(ptr.p_char, c->string, size) ? -EFAULT : 0;
		if (!ret) {
			char last = ptr.p_char[size - 1];

			ptr.p_char[size - 1] = 0;
			/* If the string was longer than ctrl->maximum,
			   then return an error. */
			if (strlen(ptr.p_char) == ctrl->maximum && last)
				return -ERANGE;
		}
		return ret;
	default:
		*ptr.p_s32 = c->value;
		break;
	}
	return 0;
}

/* Helper function: copy the caller-provider value as the new control value */
static int user_to_new(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	return user_to_ptr(c, ctrl, ctrl->p_new);
}

/* Copy the one value to another. */
static void ptr_to_ptr(struct v4l2_ctrl *ctrl,
		       union v4l2_ctrl_ptr from, union v4l2_ctrl_ptr to)
{
	if (ctrl == NULL)
		return;
	memcpy(to.p, from.p, ctrl->elems * ctrl->elem_size);
}

/* Copy the new value to the current value. */
static void new_to_cur(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 ch_flags)
{
	bool changed;

	if (ctrl == NULL)
		return;

	/* has_changed is set by cluster_changed */
	changed = ctrl->has_changed;
	if (changed)
		ptr_to_ptr(ctrl, ctrl->p_new, ctrl->p_cur);

	if (ch_flags & V4L2_EVENT_CTRL_CH_FLAGS) {
		/* Note: CH_FLAGS is only set for auto clusters. */
		ctrl->flags &=
			~(V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_VOLATILE);
		if (!is_cur_manual(ctrl->cluster[0])) {
			ctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
			if (ctrl->cluster[0]->has_volatiles)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
		}
		fh = NULL;
	}
	if (changed || ch_flags) {
		/* If a control was changed that was not one of the controls
		   modified by the application, then send the event to all. */
		if (!ctrl->is_new)
			fh = NULL;
		send_event(fh, ctrl,
			(changed ? V4L2_EVENT_CTRL_CH_VALUE : 0) | ch_flags);
		if (ctrl->call_notify && changed && ctrl->handler->notify)
			ctrl->handler->notify(ctrl, ctrl->handler->notify_priv);
	}
}

/* Copy the current value to the new value */
static void cur_to_new(struct v4l2_ctrl *ctrl)
{
	if (ctrl == NULL)
		return;
	ptr_to_ptr(ctrl, ctrl->p_cur, ctrl->p_new);
}

/* Copy the new value to the request value */
static void new_to_req(struct v4l2_ctrl_ref *ref)
{
	if (!ref)
		return;
	ptr_to_ptr(ref->ctrl, ref->ctrl->p_new, ref->p_req);
	ref->req = ref;
}

/* Copy the request value to the new value */
static void req_to_new(struct v4l2_ctrl_ref *ref)
{
	if (!ref)
		return;
	if (ref->req)
		ptr_to_ptr(ref->ctrl, ref->req->p_req, ref->ctrl->p_new);
	else
		ptr_to_ptr(ref->ctrl, ref->ctrl->p_cur, ref->ctrl->p_new);
}

/* Return non-zero if one or more of the controls in the cluster has a new
   value that differs from the current value. */
static int cluster_changed(struct v4l2_ctrl *master)
{
	bool changed = false;
	unsigned idx;
	int i;

	for (i = 0; i < master->ncontrols; i++) {
		struct v4l2_ctrl *ctrl = master->cluster[i];
		bool ctrl_changed = false;

		if (ctrl == NULL)
			continue;

		if (ctrl->flags & V4L2_CTRL_FLAG_EXECUTE_ON_WRITE)
			changed = ctrl_changed = true;

		/*
		 * Set has_changed to false to avoid generating
		 * the event V4L2_EVENT_CTRL_CH_VALUE
		 */
		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
			ctrl->has_changed = false;
			continue;
		}

		for (idx = 0; !ctrl_changed && idx < ctrl->elems; idx++)
			ctrl_changed = !ctrl->type_ops->equal(ctrl, idx,
				ctrl->p_cur, ctrl->p_new);
		ctrl->has_changed = ctrl_changed;
		changed |= ctrl->has_changed;
	}
	return changed;
}

/* Control range checking */
static int check_range(enum v4l2_ctrl_type type,
		s64 min, s64 max, u64 step, s64 def)
{
	switch (type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		if (step != 1 || max > 1 || min < 0)
			return -ERANGE;
		/* fall through */
	case V4L2_CTRL_TYPE_U8:
	case V4L2_CTRL_TYPE_U16:
	case V4L2_CTRL_TYPE_U32:
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_INTEGER64:
		if (step == 0 || min > max || def < min || def > max)
			return -ERANGE;
		return 0;
	case V4L2_CTRL_TYPE_BITMASK:
		if (step || min || !max || (def & ~max))
			return -ERANGE;
		return 0;
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (min > max || def < min || def > max)
			return -ERANGE;
		/* Note: step == menu_skip_mask for menu controls.
		   So here we check if the default value is masked out. */
		if (step && ((1 << def) & step))
			return -EINVAL;
		return 0;
	case V4L2_CTRL_TYPE_STRING:
		if (min > max || min < 0 || step < 1 || def)
			return -ERANGE;
		return 0;
	default:
		return 0;
	}
}

/* Validate a new control */
static int validate_new(const struct v4l2_ctrl *ctrl, union v4l2_ctrl_ptr p_new)
{
	unsigned idx;
	int err = 0;

	for (idx = 0; !err && idx < ctrl->elems; idx++)
		err = ctrl->type_ops->validate(ctrl, idx, p_new);
	return err;
}

static inline u32 node2id(struct list_head *node)
{
	return list_entry(node, struct v4l2_ctrl_ref, node)->ctrl->id;
}

/* Set the handler's error code if it wasn't set earlier already */
static inline int handler_set_err(struct v4l2_ctrl_handler *hdl, int err)
{
	if (hdl->error == 0)
		hdl->error = err;
	return err;
}

/* Initialize the handler */
int v4l2_ctrl_handler_init_class(struct v4l2_ctrl_handler *hdl,
				 unsigned nr_of_controls_hint,
				 struct lock_class_key *key, const char *name)
{
	mutex_init(&hdl->_lock);
	hdl->lock = &hdl->_lock;
	lockdep_set_class_and_name(hdl->lock, key, name);
	INIT_LIST_HEAD(&hdl->ctrls);
	INIT_LIST_HEAD(&hdl->ctrl_refs);
	INIT_LIST_HEAD(&hdl->requests);
	INIT_LIST_HEAD(&hdl->requests_queued);
	hdl->request_is_queued = false;
	hdl->nr_of_buckets = 1 + nr_of_controls_hint / 8;
	hdl->buckets = kvmalloc_array(hdl->nr_of_buckets,
				      sizeof(hdl->buckets[0]),
				      GFP_KERNEL | __GFP_ZERO);
	hdl->error = hdl->buckets ? 0 : -ENOMEM;
	media_request_object_init(&hdl->req_obj);
	return hdl->error;
}
EXPORT_SYMBOL(v4l2_ctrl_handler_init_class);

/* Free all controls and control refs */
void v4l2_ctrl_handler_free(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl_ref *ref, *next_ref;
	struct v4l2_ctrl *ctrl, *next_ctrl;
	struct v4l2_subscribed_event *sev, *next_sev;

	if (hdl == NULL || hdl->buckets == NULL)
		return;

	if (!hdl->req_obj.req && !list_empty(&hdl->requests)) {
		struct v4l2_ctrl_handler *req, *next_req;

		list_for_each_entry_safe(req, next_req, &hdl->requests, requests) {
			media_request_object_unbind(&req->req_obj);
			media_request_object_put(&req->req_obj);
		}
	}
	mutex_lock(hdl->lock);
	/* Free all nodes */
	list_for_each_entry_safe(ref, next_ref, &hdl->ctrl_refs, node) {
		list_del(&ref->node);
		kfree(ref);
	}
	/* Free all controls owned by the handler */
	list_for_each_entry_safe(ctrl, next_ctrl, &hdl->ctrls, node) {
		list_del(&ctrl->node);
		list_for_each_entry_safe(sev, next_sev, &ctrl->ev_subs, node)
			list_del(&sev->node);
		kvfree(ctrl);
	}
	kvfree(hdl->buckets);
	hdl->buckets = NULL;
	hdl->cached = NULL;
	hdl->error = 0;
	mutex_unlock(hdl->lock);
	mutex_destroy(&hdl->_lock);
}
EXPORT_SYMBOL(v4l2_ctrl_handler_free);

/* For backwards compatibility: V4L2_CID_PRIVATE_BASE should no longer
   be used except in G_CTRL, S_CTRL, QUERYCTRL and QUERYMENU when dealing
   with applications that do not use the NEXT_CTRL flag.

   We just find the n-th private user control. It's O(N), but that should not
   be an issue in this particular case. */
static struct v4l2_ctrl_ref *find_private_ref(
		struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref;

	id -= V4L2_CID_PRIVATE_BASE;
	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		/* Search for private user controls that are compatible with
		   VIDIOC_G/S_CTRL. */
		if (V4L2_CTRL_ID2WHICH(ref->ctrl->id) == V4L2_CTRL_CLASS_USER &&
		    V4L2_CTRL_DRIVER_PRIV(ref->ctrl->id)) {
			if (!ref->ctrl->is_int)
				continue;
			if (id == 0)
				return ref;
			id--;
		}
	}
	return NULL;
}

/* Find a control with the given ID. */
static struct v4l2_ctrl_ref *find_ref(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref;
	int bucket;

	id &= V4L2_CTRL_ID_MASK;

	/* Old-style private controls need special handling */
	if (id >= V4L2_CID_PRIVATE_BASE)
		return find_private_ref(hdl, id);
	bucket = id % hdl->nr_of_buckets;

	/* Simple optimization: cache the last control found */
	if (hdl->cached && hdl->cached->ctrl->id == id)
		return hdl->cached;

	/* Not in cache, search the hash */
	ref = hdl->buckets ? hdl->buckets[bucket] : NULL;
	while (ref && ref->ctrl->id != id)
		ref = ref->next;

	if (ref)
		hdl->cached = ref; /* cache it! */
	return ref;
}

/* Find a control with the given ID. Take the handler's lock first. */
static struct v4l2_ctrl_ref *find_ref_lock(
		struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = NULL;

	if (hdl) {
		mutex_lock(hdl->lock);
		ref = find_ref(hdl, id);
		mutex_unlock(hdl->lock);
	}
	return ref;
}

/* Find a control with the given ID. */
struct v4l2_ctrl *v4l2_ctrl_find(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = find_ref_lock(hdl, id);

	return ref ? ref->ctrl : NULL;
}
EXPORT_SYMBOL(v4l2_ctrl_find);

/* Allocate a new v4l2_ctrl_ref and hook it into the handler. */
static int handler_new_ref(struct v4l2_ctrl_handler *hdl,
			   struct v4l2_ctrl *ctrl,
			   struct v4l2_ctrl_ref **ctrl_ref,
			   bool from_other_dev, bool allocate_req)
{
	struct v4l2_ctrl_ref *ref;
	struct v4l2_ctrl_ref *new_ref;
	u32 id = ctrl->id;
	u32 class_ctrl = V4L2_CTRL_ID2WHICH(id) | 1;
	int bucket = id % hdl->nr_of_buckets;	/* which bucket to use */
	unsigned int size_extra_req = 0;

	if (ctrl_ref)
		*ctrl_ref = NULL;

	/*
	 * Automatically add the control class if it is not yet present and
	 * the new control is not a compound control.
	 */
	if (ctrl->type < V4L2_CTRL_COMPOUND_TYPES &&
	    id != class_ctrl && find_ref_lock(hdl, class_ctrl) == NULL)
		if (!v4l2_ctrl_new_std(hdl, NULL, class_ctrl, 0, 0, 0, 0))
			return hdl->error;

	if (hdl->error)
		return hdl->error;

	if (allocate_req)
		size_extra_req = ctrl->elems * ctrl->elem_size;
	new_ref = kzalloc(sizeof(*new_ref) + size_extra_req, GFP_KERNEL);
	if (!new_ref)
		return handler_set_err(hdl, -ENOMEM);
	new_ref->ctrl = ctrl;
	new_ref->from_other_dev = from_other_dev;
	if (size_extra_req)
		new_ref->p_req.p = &new_ref[1];

	if (ctrl->handler == hdl) {
		/* By default each control starts in a cluster of its own.
		   new_ref->ctrl is basically a cluster array with one
		   element, so that's perfect to use as the cluster pointer.
		   But only do this for the handler that owns the control. */
		ctrl->cluster = &new_ref->ctrl;
		ctrl->ncontrols = 1;
	}

	INIT_LIST_HEAD(&new_ref->node);

	mutex_lock(hdl->lock);

	/* Add immediately at the end of the list if the list is empty, or if
	   the last element in the list has a lower ID.
	   This ensures that when elements are added in ascending order the
	   insertion is an O(1) operation. */
	if (list_empty(&hdl->ctrl_refs) || id > node2id(hdl->ctrl_refs.prev)) {
		list_add_tail(&new_ref->node, &hdl->ctrl_refs);
		goto insert_in_hash;
	}

	/* Find insert position in sorted list */
	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		if (ref->ctrl->id < id)
			continue;
		/* Don't add duplicates */
		if (ref->ctrl->id == id) {
			kfree(new_ref);
			goto unlock;
		}
		list_add(&new_ref->node, ref->node.prev);
		break;
	}

insert_in_hash:
	/* Insert the control node in the hash */
	new_ref->next = hdl->buckets[bucket];
	hdl->buckets[bucket] = new_ref;
	if (ctrl_ref)
		*ctrl_ref = new_ref;

unlock:
	mutex_unlock(hdl->lock);
	return 0;
}

/* Add a new control */
static struct v4l2_ctrl *v4l2_ctrl_new(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			const struct v4l2_ctrl_type_ops *type_ops,
			u32 id, const char *name, enum v4l2_ctrl_type type,
			s64 min, s64 max, u64 step, s64 def,
			const u32 dims[V4L2_CTRL_MAX_DIMS], u32 elem_size,
			u32 flags, const char * const *qmenu,
			const s64 *qmenu_int, void *priv)
{
	struct v4l2_ctrl *ctrl;
	unsigned sz_extra;
	unsigned nr_of_dims = 0;
	unsigned elems = 1;
	bool is_array;
	unsigned tot_ctrl_size;
	unsigned idx;
	void *data;
	int err;

	if (hdl->error)
		return NULL;

	while (dims && dims[nr_of_dims]) {
		elems *= dims[nr_of_dims];
		nr_of_dims++;
		if (nr_of_dims == V4L2_CTRL_MAX_DIMS)
			break;
	}
	is_array = nr_of_dims > 0;

	/* Prefill elem_size for all types handled by std_type_ops */
	switch (type) {
	case V4L2_CTRL_TYPE_INTEGER64:
		elem_size = sizeof(s64);
		break;
	case V4L2_CTRL_TYPE_STRING:
		elem_size = max + 1;
		break;
	case V4L2_CTRL_TYPE_U8:
		elem_size = sizeof(u8);
		break;
	case V4L2_CTRL_TYPE_U16:
		elem_size = sizeof(u16);
		break;
	case V4L2_CTRL_TYPE_U32:
		elem_size = sizeof(u32);
		break;
	case V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS:
		elem_size = sizeof(struct v4l2_ctrl_mpeg2_slice_params);
		break;
	case V4L2_CTRL_TYPE_MPEG2_QUANTIZATION:
		elem_size = sizeof(struct v4l2_ctrl_mpeg2_quantization);
		break;
	default:
		if (type < V4L2_CTRL_COMPOUND_TYPES)
			elem_size = sizeof(s32);
		break;
	}
	tot_ctrl_size = elem_size * elems;

	/* Sanity checks */
	if (id == 0 || name == NULL || !elem_size ||
	    id >= V4L2_CID_PRIVATE_BASE ||
	    (type == V4L2_CTRL_TYPE_MENU && qmenu == NULL) ||
	    (type == V4L2_CTRL_TYPE_INTEGER_MENU && qmenu_int == NULL)) {
		handler_set_err(hdl, -ERANGE);
		return NULL;
	}
	err = check_range(type, min, max, step, def);
	if (err) {
		handler_set_err(hdl, err);
		return NULL;
	}
	if (is_array &&
	    (type == V4L2_CTRL_TYPE_BUTTON ||
	     type == V4L2_CTRL_TYPE_CTRL_CLASS)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	sz_extra = 0;
	if (type == V4L2_CTRL_TYPE_BUTTON)
		flags |= V4L2_CTRL_FLAG_WRITE_ONLY |
			V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	else if (type == V4L2_CTRL_TYPE_CTRL_CLASS)
		flags |= V4L2_CTRL_FLAG_READ_ONLY;
	else if (type == V4L2_CTRL_TYPE_INTEGER64 ||
		 type == V4L2_CTRL_TYPE_STRING ||
		 type >= V4L2_CTRL_COMPOUND_TYPES ||
		 is_array)
		sz_extra += 2 * tot_ctrl_size;

	ctrl = kvzalloc(sizeof(*ctrl) + sz_extra, GFP_KERNEL);
	if (ctrl == NULL) {
		handler_set_err(hdl, -ENOMEM);
		return NULL;
	}

	INIT_LIST_HEAD(&ctrl->node);
	INIT_LIST_HEAD(&ctrl->ev_subs);
	ctrl->handler = hdl;
	ctrl->ops = ops;
	ctrl->type_ops = type_ops ? type_ops : &std_type_ops;
	ctrl->id = id;
	ctrl->name = name;
	ctrl->type = type;
	ctrl->flags = flags;
	ctrl->minimum = min;
	ctrl->maximum = max;
	ctrl->step = step;
	ctrl->default_value = def;
	ctrl->is_string = !is_array && type == V4L2_CTRL_TYPE_STRING;
	ctrl->is_ptr = is_array || type >= V4L2_CTRL_COMPOUND_TYPES || ctrl->is_string;
	ctrl->is_int = !ctrl->is_ptr && type != V4L2_CTRL_TYPE_INTEGER64;
	ctrl->is_array = is_array;
	ctrl->elems = elems;
	ctrl->nr_of_dims = nr_of_dims;
	if (nr_of_dims)
		memcpy(ctrl->dims, dims, nr_of_dims * sizeof(dims[0]));
	ctrl->elem_size = elem_size;
	if (type == V4L2_CTRL_TYPE_MENU)
		ctrl->qmenu = qmenu;
	else if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		ctrl->qmenu_int = qmenu_int;
	ctrl->priv = priv;
	ctrl->cur.val = ctrl->val = def;
	data = &ctrl[1];

	if (!ctrl->is_int) {
		ctrl->p_new.p = data;
		ctrl->p_cur.p = data + tot_ctrl_size;
	} else {
		ctrl->p_new.p = &ctrl->val;
		ctrl->p_cur.p = &ctrl->cur.val;
	}
	for (idx = 0; idx < elems; idx++) {
		ctrl->type_ops->init(ctrl, idx, ctrl->p_cur);
		ctrl->type_ops->init(ctrl, idx, ctrl->p_new);
	}

	if (handler_new_ref(hdl, ctrl, NULL, false, false)) {
		kvfree(ctrl);
		return NULL;
	}
	mutex_lock(hdl->lock);
	list_add_tail(&ctrl->node, &hdl->ctrls);
	mutex_unlock(hdl->lock);
	return ctrl;
}

struct v4l2_ctrl *v4l2_ctrl_new_custom(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_config *cfg, void *priv)
{
	bool is_menu;
	struct v4l2_ctrl *ctrl;
	const char *name = cfg->name;
	const char * const *qmenu = cfg->qmenu;
	const s64 *qmenu_int = cfg->qmenu_int;
	enum v4l2_ctrl_type type = cfg->type;
	u32 flags = cfg->flags;
	s64 min = cfg->min;
	s64 max = cfg->max;
	u64 step = cfg->step;
	s64 def = cfg->def;

	if (name == NULL)
		v4l2_ctrl_fill(cfg->id, &name, &type, &min, &max, &step,
								&def, &flags);

	is_menu = (cfg->type == V4L2_CTRL_TYPE_MENU ||
		   cfg->type == V4L2_CTRL_TYPE_INTEGER_MENU);
	if (is_menu)
		WARN_ON(step);
	else
		WARN_ON(cfg->menu_skip_mask);
	if (cfg->type == V4L2_CTRL_TYPE_MENU && qmenu == NULL)
		qmenu = v4l2_ctrl_get_menu(cfg->id);
	else if (cfg->type == V4L2_CTRL_TYPE_INTEGER_MENU &&
		 qmenu_int == NULL) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	ctrl = v4l2_ctrl_new(hdl, cfg->ops, cfg->type_ops, cfg->id, name,
			type, min, max,
			is_menu ? cfg->menu_skip_mask : step, def,
			cfg->dims, cfg->elem_size,
			flags, qmenu, qmenu_int, priv);
	if (ctrl)
		ctrl->is_private = cfg->is_private;
	return ctrl;
}
EXPORT_SYMBOL(v4l2_ctrl_new_custom);

/* Helper function for standard non-menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s64 min, s64 max, u64 step, s64 def)
{
	const char *name;
	enum v4l2_ctrl_type type;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type == V4L2_CTRL_TYPE_MENU ||
	    type == V4L2_CTRL_TYPE_INTEGER_MENU ||
	    type >= V4L2_CTRL_COMPOUND_TYPES) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     min, max, step, def, NULL, 0,
			     flags, NULL, NULL, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std);

/* Helper function for standard menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, u8 _max, u64 mask, u8 _def)
{
	const char * const *qmenu = NULL;
	const s64 *qmenu_int = NULL;
	unsigned int qmenu_int_len = 0;
	const char *name;
	enum v4l2_ctrl_type type;
	s64 min;
	s64 max = _max;
	s64 def = _def;
	u64 step;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);

	if (type == V4L2_CTRL_TYPE_MENU)
		qmenu = v4l2_ctrl_get_menu(id);
	else if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		qmenu_int = v4l2_ctrl_get_int_menu(id, &qmenu_int_len);

	if ((!qmenu && !qmenu_int) || (qmenu_int && max > qmenu_int_len)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     0, max, mask, def, NULL, 0,
			     flags, qmenu, qmenu_int, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std_menu);

/* Helper function for standard menu controls with driver defined menu */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu_items(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops, u32 id, u8 _max,
			u64 mask, u8 _def, const char * const *qmenu)
{
	enum v4l2_ctrl_type type;
	const char *name;
	u32 flags;
	u64 step;
	s64 min;
	s64 max = _max;
	s64 def = _def;

	/* v4l2_ctrl_new_std_menu_items() should only be called for
	 * standard controls without a standard menu.
	 */
	if (v4l2_ctrl_get_menu(id)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type != V4L2_CTRL_TYPE_MENU || qmenu == NULL) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     0, max, mask, def, NULL, 0,
			     flags, qmenu, NULL, NULL);

}
EXPORT_SYMBOL(v4l2_ctrl_new_std_menu_items);

/* Helper function for standard integer menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_int_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, u8 _max, u8 _def, const s64 *qmenu_int)
{
	const char *name;
	enum v4l2_ctrl_type type;
	s64 min;
	u64 step;
	s64 max = _max;
	s64 def = _def;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type != V4L2_CTRL_TYPE_INTEGER_MENU) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     0, max, 0, def, NULL, 0,
			     flags, NULL, qmenu_int, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_int_menu);

/* Add the controls from another handler to our own. */
int v4l2_ctrl_add_handler(struct v4l2_ctrl_handler *hdl,
			  struct v4l2_ctrl_handler *add,
			  bool (*filter)(const struct v4l2_ctrl *ctrl),
			  bool from_other_dev)
{
	struct v4l2_ctrl_ref *ref;
	int ret = 0;

	/* Do nothing if either handler is NULL or if they are the same */
	if (!hdl || !add || hdl == add)
		return 0;
	if (hdl->error)
		return hdl->error;
	mutex_lock(add->lock);
	list_for_each_entry(ref, &add->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;

		/* Skip handler-private controls. */
		if (ctrl->is_private)
			continue;
		/* And control classes */
		if (ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
			continue;
		/* Filter any unwanted controls */
		if (filter && !filter(ctrl))
			continue;
		ret = handler_new_ref(hdl, ctrl, NULL, from_other_dev, false);
		if (ret)
			break;
	}
	mutex_unlock(add->lock);
	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_add_handler);

bool v4l2_ctrl_radio_filter(const struct v4l2_ctrl *ctrl)
{
	if (V4L2_CTRL_ID2WHICH(ctrl->id) == V4L2_CTRL_CLASS_FM_TX)
		return true;
	if (V4L2_CTRL_ID2WHICH(ctrl->id) == V4L2_CTRL_CLASS_FM_RX)
		return true;
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return true;
	default:
		break;
	}
	return false;
}
EXPORT_SYMBOL(v4l2_ctrl_radio_filter);

/* Cluster controls */
void v4l2_ctrl_cluster(unsigned ncontrols, struct v4l2_ctrl **controls)
{
	bool has_volatiles = false;
	int i;

	/* The first control is the master control and it must not be NULL */
	if (WARN_ON(ncontrols == 0 || controls[0] == NULL))
		return;

	for (i = 0; i < ncontrols; i++) {
		if (controls[i]) {
			controls[i]->cluster = controls;
			controls[i]->ncontrols = ncontrols;
			if (controls[i]->flags & V4L2_CTRL_FLAG_VOLATILE)
				has_volatiles = true;
		}
	}
	controls[0]->has_volatiles = has_volatiles;
}
EXPORT_SYMBOL(v4l2_ctrl_cluster);

void v4l2_ctrl_auto_cluster(unsigned ncontrols, struct v4l2_ctrl **controls,
			    u8 manual_val, bool set_volatile)
{
	struct v4l2_ctrl *master = controls[0];
	u32 flag = 0;
	int i;

	v4l2_ctrl_cluster(ncontrols, controls);
	WARN_ON(ncontrols <= 1);
	WARN_ON(manual_val < master->minimum || manual_val > master->maximum);
	WARN_ON(set_volatile && !has_op(master, g_volatile_ctrl));
	master->is_auto = true;
	master->has_volatiles = set_volatile;
	master->manual_mode_value = manual_val;
	master->flags |= V4L2_CTRL_FLAG_UPDATE;

	if (!is_cur_manual(master))
		flag = V4L2_CTRL_FLAG_INACTIVE |
			(set_volatile ? V4L2_CTRL_FLAG_VOLATILE : 0);

	for (i = 1; i < ncontrols; i++)
		if (controls[i])
			controls[i]->flags |= flag;
}
EXPORT_SYMBOL(v4l2_ctrl_auto_cluster);

/* Activate/deactivate a control. */
void v4l2_ctrl_activate(struct v4l2_ctrl *ctrl, bool active)
{
	/* invert since the actual flag is called 'inactive' */
	bool inactive = !active;
	bool old;

	if (ctrl == NULL)
		return;

	if (inactive)
		/* set V4L2_CTRL_FLAG_INACTIVE */
		old = test_and_set_bit(4, &ctrl->flags);
	else
		/* clear V4L2_CTRL_FLAG_INACTIVE */
		old = test_and_clear_bit(4, &ctrl->flags);
	if (old != inactive)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_FLAGS);
}
EXPORT_SYMBOL(v4l2_ctrl_activate);

void __v4l2_ctrl_grab(struct v4l2_ctrl *ctrl, bool grabbed)
{
	bool old;

	if (ctrl == NULL)
		return;

	lockdep_assert_held(ctrl->handler->lock);

	if (grabbed)
		/* set V4L2_CTRL_FLAG_GRABBED */
		old = test_and_set_bit(1, &ctrl->flags);
	else
		/* clear V4L2_CTRL_FLAG_GRABBED */
		old = test_and_clear_bit(1, &ctrl->flags);
	if (old != grabbed)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_FLAGS);
}
EXPORT_SYMBOL(__v4l2_ctrl_grab);

/* Log the control name and value */
static void log_ctrl(const struct v4l2_ctrl *ctrl,
		     const char *prefix, const char *colon)
{
	if (ctrl->flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_WRITE_ONLY))
		return;
	if (ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
		return;

	pr_info("%s%s%s: ", prefix, colon, ctrl->name);

	ctrl->type_ops->log(ctrl);

	if (ctrl->flags & (V4L2_CTRL_FLAG_INACTIVE |
			   V4L2_CTRL_FLAG_GRABBED |
			   V4L2_CTRL_FLAG_VOLATILE)) {
		if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
			pr_cont(" inactive");
		if (ctrl->flags & V4L2_CTRL_FLAG_GRABBED)
			pr_cont(" grabbed");
		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE)
			pr_cont(" volatile");
	}
	pr_cont("\n");
}

/* Log all controls owned by the handler */
void v4l2_ctrl_handler_log_status(struct v4l2_ctrl_handler *hdl,
				  const char *prefix)
{
	struct v4l2_ctrl *ctrl;
	const char *colon = "";
	int len;

	if (hdl == NULL)
		return;
	if (prefix == NULL)
		prefix = "";
	len = strlen(prefix);
	if (len && prefix[len - 1] != ' ')
		colon = ": ";
	mutex_lock(hdl->lock);
	list_for_each_entry(ctrl, &hdl->ctrls, node)
		if (!(ctrl->flags & V4L2_CTRL_FLAG_DISABLED))
			log_ctrl(ctrl, prefix, colon);
	mutex_unlock(hdl->lock);
}
EXPORT_SYMBOL(v4l2_ctrl_handler_log_status);

int v4l2_ctrl_subdev_log_status(struct v4l2_subdev *sd)
{
	v4l2_ctrl_handler_log_status(sd->ctrl_handler, sd->name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_subdev_log_status);

/* Call s_ctrl for all controls owned by the handler */
int __v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	if (hdl == NULL)
		return 0;

	lockdep_assert_held(hdl->lock);

	list_for_each_entry(ctrl, &hdl->ctrls, node)
		ctrl->done = false;

	list_for_each_entry(ctrl, &hdl->ctrls, node) {
		struct v4l2_ctrl *master = ctrl->cluster[0];
		int i;

		/* Skip if this control was already handled by a cluster. */
		/* Skip button controls and read-only controls. */
		if (ctrl->done || ctrl->type == V4L2_CTRL_TYPE_BUTTON ||
		    (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY))
			continue;

		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				cur_to_new(master->cluster[i]);
				master->cluster[i]->is_new = 1;
				master->cluster[i]->done = true;
			}
		}
		ret = call_op(master, s_ctrl);
		if (ret)
			break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(__v4l2_ctrl_handler_setup);

int v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl)
{
	int ret;

	if (hdl == NULL)
		return 0;

	mutex_lock(hdl->lock);
	ret = __v4l2_ctrl_handler_setup(hdl);
	mutex_unlock(hdl->lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_handler_setup);

/* Implement VIDIOC_QUERY_EXT_CTRL */
int v4l2_query_ext_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_query_ext_ctrl *qc)
{
	const unsigned next_flags = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
	u32 id = qc->id & V4L2_CTRL_ID_MASK;
	struct v4l2_ctrl_ref *ref;
	struct v4l2_ctrl *ctrl;

	if (hdl == NULL)
		return -EINVAL;

	mutex_lock(hdl->lock);

	/* Try to find it */
	ref = find_ref(hdl, id);

	if ((qc->id & next_flags) && !list_empty(&hdl->ctrl_refs)) {
		bool is_compound;
		/* Match any control that is not hidden */
		unsigned mask = 1;
		bool match = false;

		if ((qc->id & next_flags) == V4L2_CTRL_FLAG_NEXT_COMPOUND) {
			/* Match any hidden control */
			match = true;
		} else if ((qc->id & next_flags) == next_flags) {
			/* Match any control, compound or not */
			mask = 0;
		}

		/* Find the next control with ID > qc->id */

		/* Did we reach the end of the control list? */
		if (id >= node2id(hdl->ctrl_refs.prev)) {
			ref = NULL; /* Yes, so there is no next control */
		} else if (ref) {
			/* We found a control with the given ID, so just get
			   the next valid one in the list. */
			list_for_each_entry_continue(ref, &hdl->ctrl_refs, node) {
				is_compound = ref->ctrl->is_array ||
					ref->ctrl->type >= V4L2_CTRL_COMPOUND_TYPES;
				if (id < ref->ctrl->id &&
				    (is_compound & mask) == match)
					break;
			}
			if (&ref->node == &hdl->ctrl_refs)
				ref = NULL;
		} else {
			/* No control with the given ID exists, so start
			   searching for the next largest ID. We know there
			   is one, otherwise the first 'if' above would have
			   been true. */
			list_for_each_entry(ref, &hdl->ctrl_refs, node) {
				is_compound = ref->ctrl->is_array ||
					ref->ctrl->type >= V4L2_CTRL_COMPOUND_TYPES;
				if (id < ref->ctrl->id &&
				    (is_compound & mask) == match)
					break;
			}
			if (&ref->node == &hdl->ctrl_refs)
				ref = NULL;
		}
	}
	mutex_unlock(hdl->lock);

	if (!ref)
		return -EINVAL;

	ctrl = ref->ctrl;
	memset(qc, 0, sizeof(*qc));
	if (id >= V4L2_CID_PRIVATE_BASE)
		qc->id = id;
	else
		qc->id = ctrl->id;
	strscpy(qc->name, ctrl->name, sizeof(qc->name));
	qc->flags = user_flags(ctrl);
	qc->type = ctrl->type;
	qc->elem_size = ctrl->elem_size;
	qc->elems = ctrl->elems;
	qc->nr_of_dims = ctrl->nr_of_dims;
	memcpy(qc->dims, ctrl->dims, qc->nr_of_dims * sizeof(qc->dims[0]));
	qc->minimum = ctrl->minimum;
	qc->maximum = ctrl->maximum;
	qc->default_value = ctrl->default_value;
	if (ctrl->type == V4L2_CTRL_TYPE_MENU
	    || ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU)
		qc->step = 1;
	else
		qc->step = ctrl->step;
	return 0;
}
EXPORT_SYMBOL(v4l2_query_ext_ctrl);

/* Implement VIDIOC_QUERYCTRL */
int v4l2_queryctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_queryctrl *qc)
{
	struct v4l2_query_ext_ctrl qec = { qc->id };
	int rc;

	rc = v4l2_query_ext_ctrl(hdl, &qec);
	if (rc)
		return rc;

	qc->id = qec.id;
	qc->type = qec.type;
	qc->flags = qec.flags;
	strscpy(qc->name, qec.name, sizeof(qc->name));
	switch (qc->type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_STRING:
	case V4L2_CTRL_TYPE_BITMASK:
		qc->minimum = qec.minimum;
		qc->maximum = qec.maximum;
		qc->step = qec.step;
		qc->default_value = qec.default_value;
		break;
	default:
		qc->minimum = 0;
		qc->maximum = 0;
		qc->step = 0;
		qc->default_value = 0;
		break;
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_queryctrl);

/* Implement VIDIOC_QUERYMENU */
int v4l2_querymenu(struct v4l2_ctrl_handler *hdl, struct v4l2_querymenu *qm)
{
	struct v4l2_ctrl *ctrl;
	u32 i = qm->index;

	ctrl = v4l2_ctrl_find(hdl, qm->id);
	if (!ctrl)
		return -EINVAL;

	qm->reserved = 0;
	/* Sanity checks */
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_MENU:
		if (ctrl->qmenu == NULL)
			return -EINVAL;
		break;
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (ctrl->qmenu_int == NULL)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (i < ctrl->minimum || i > ctrl->maximum)
		return -EINVAL;

	/* Use mask to see if this menu item should be skipped */
	if (ctrl->menu_skip_mask & (1 << i))
		return -EINVAL;
	/* Empty menu items should also be skipped */
	if (ctrl->type == V4L2_CTRL_TYPE_MENU) {
		if (ctrl->qmenu[i] == NULL || ctrl->qmenu[i][0] == '\0')
			return -EINVAL;
		strscpy(qm->name, ctrl->qmenu[i], sizeof(qm->name));
	} else {
		qm->value = ctrl->qmenu_int[i];
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_querymenu);

static int v4l2_ctrl_request_clone(struct v4l2_ctrl_handler *hdl,
				   const struct v4l2_ctrl_handler *from)
{
	struct v4l2_ctrl_ref *ref;
	int err = 0;

	if (WARN_ON(!hdl || hdl == from))
		return -EINVAL;

	if (hdl->error)
		return hdl->error;

	WARN_ON(hdl->lock != &hdl->_lock);

	mutex_lock(from->lock);
	list_for_each_entry(ref, &from->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;
		struct v4l2_ctrl_ref *new_ref;

		/* Skip refs inherited from other devices */
		if (ref->from_other_dev)
			continue;
		/* And buttons */
		if (ctrl->type == V4L2_CTRL_TYPE_BUTTON)
			continue;
		err = handler_new_ref(hdl, ctrl, &new_ref, false, true);
		if (err)
			break;
	}
	mutex_unlock(from->lock);
	return err;
}

static void v4l2_ctrl_request_queue(struct media_request_object *obj)
{
	struct v4l2_ctrl_handler *hdl =
		container_of(obj, struct v4l2_ctrl_handler, req_obj);
	struct v4l2_ctrl_handler *main_hdl = obj->priv;
	struct v4l2_ctrl_handler *prev_hdl = NULL;
	struct v4l2_ctrl_ref *ref_ctrl, *ref_ctrl_prev = NULL;

	if (list_empty(&main_hdl->requests_queued))
		goto queue;

	prev_hdl = list_last_entry(&main_hdl->requests_queued,
				   struct v4l2_ctrl_handler, requests_queued);
	/*
	 * Note: prev_hdl and hdl must contain the same list of control
	 * references, so if any differences are detected then that is a
	 * driver bug and the WARN_ON is triggered.
	 */
	mutex_lock(prev_hdl->lock);
	ref_ctrl_prev = list_first_entry(&prev_hdl->ctrl_refs,
					 struct v4l2_ctrl_ref, node);
	list_for_each_entry(ref_ctrl, &hdl->ctrl_refs, node) {
		if (ref_ctrl->req)
			continue;
		while (ref_ctrl_prev->ctrl->id < ref_ctrl->ctrl->id) {
			/* Should never happen, but just in case... */
			if (list_is_last(&ref_ctrl_prev->node,
					 &prev_hdl->ctrl_refs))
				break;
			ref_ctrl_prev = list_next_entry(ref_ctrl_prev, node);
		}
		if (WARN_ON(ref_ctrl_prev->ctrl->id != ref_ctrl->ctrl->id))
			break;
		ref_ctrl->req = ref_ctrl_prev->req;
	}
	mutex_unlock(prev_hdl->lock);
queue:
	list_add_tail(&hdl->requests_queued, &main_hdl->requests_queued);
	hdl->request_is_queued = true;
}

static void v4l2_ctrl_request_unbind(struct media_request_object *obj)
{
	struct v4l2_ctrl_handler *hdl =
		container_of(obj, struct v4l2_ctrl_handler, req_obj);

	list_del_init(&hdl->requests);
	if (hdl->request_is_queued) {
		list_del_init(&hdl->requests_queued);
		hdl->request_is_queued = false;
	}
}

static void v4l2_ctrl_request_release(struct media_request_object *obj)
{
	struct v4l2_ctrl_handler *hdl =
		container_of(obj, struct v4l2_ctrl_handler, req_obj);

	v4l2_ctrl_handler_free(hdl);
	kfree(hdl);
}

static const struct media_request_object_ops req_ops = {
	.queue = v4l2_ctrl_request_queue,
	.unbind = v4l2_ctrl_request_unbind,
	.release = v4l2_ctrl_request_release,
};

struct v4l2_ctrl_handler *v4l2_ctrl_request_hdl_find(struct media_request *req,
					struct v4l2_ctrl_handler *parent)
{
	struct media_request_object *obj;

	if (WARN_ON(req->state != MEDIA_REQUEST_STATE_VALIDATING &&
		    req->state != MEDIA_REQUEST_STATE_QUEUED))
		return NULL;

	obj = media_request_object_find(req, &req_ops, parent);
	if (obj)
		return container_of(obj, struct v4l2_ctrl_handler, req_obj);
	return NULL;
}
EXPORT_SYMBOL_GPL(v4l2_ctrl_request_hdl_find);

struct v4l2_ctrl *
v4l2_ctrl_request_hdl_ctrl_find(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = find_ref_lock(hdl, id);

	return (ref && ref->req == ref) ? ref->ctrl : NULL;
}
EXPORT_SYMBOL_GPL(v4l2_ctrl_request_hdl_ctrl_find);

static int v4l2_ctrl_request_bind(struct media_request *req,
			   struct v4l2_ctrl_handler *hdl,
			   struct v4l2_ctrl_handler *from)
{
	int ret;

	ret = v4l2_ctrl_request_clone(hdl, from);

	if (!ret) {
		ret = media_request_object_bind(req, &req_ops,
						from, false, &hdl->req_obj);
		if (!ret)
			list_add_tail(&hdl->requests, &from->requests);
	}
	return ret;
}

/* Some general notes on the atomic requirements of VIDIOC_G/TRY/S_EXT_CTRLS:

   It is not a fully atomic operation, just best-effort only. After all, if
   multiple controls have to be set through multiple i2c writes (for example)
   then some initial writes may succeed while others fail. Thus leaving the
   system in an inconsistent state. The question is how much effort you are
   willing to spend on trying to make something atomic that really isn't.

   From the point of view of an application the main requirement is that
   when you call VIDIOC_S_EXT_CTRLS and some values are invalid then an
   error should be returned without actually affecting any controls.

   If all the values are correct, then it is acceptable to just give up
   in case of low-level errors.

   It is important though that the application can tell when only a partial
   configuration was done. The way we do that is through the error_idx field
   of struct v4l2_ext_controls: if that is equal to the count field then no
   controls were affected. Otherwise all controls before that index were
   successful in performing their 'get' or 'set' operation, the control at
   the given index failed, and you don't know what happened with the controls
   after the failed one. Since if they were part of a control cluster they
   could have been successfully processed (if a cluster member was encountered
   at index < error_idx), they could have failed (if a cluster member was at
   error_idx), or they may not have been processed yet (if the first cluster
   member appeared after error_idx).

   It is all fairly theoretical, though. In practice all you can do is to
   bail out. If error_idx == count, then it is an application bug. If
   error_idx < count then it is only an application bug if the error code was
   EBUSY. That usually means that something started streaming just when you
   tried to set the controls. In all other cases it is a driver/hardware
   problem and all you can do is to retry or bail out.

   Note that these rules do not apply to VIDIOC_TRY_EXT_CTRLS: since that
   never modifies controls the error_idx is just set to whatever control
   has an invalid value.
 */

/* Prepare for the extended g/s/try functions.
   Find the controls in the control array and do some basic checks. */
static int prepare_ext_ctrls(struct v4l2_ctrl_handler *hdl,
			     struct v4l2_ext_controls *cs,
			     struct v4l2_ctrl_helper *helpers,
			     bool get)
{
	struct v4l2_ctrl_helper *h;
	bool have_clusters = false;
	u32 i;

	for (i = 0, h = helpers; i < cs->count; i++, h++) {
		struct v4l2_ext_control *c = &cs->controls[i];
		struct v4l2_ctrl_ref *ref;
		struct v4l2_ctrl *ctrl;
		u32 id = c->id & V4L2_CTRL_ID_MASK;

		cs->error_idx = i;

		if (cs->which &&
		    cs->which != V4L2_CTRL_WHICH_DEF_VAL &&
		    cs->which != V4L2_CTRL_WHICH_REQUEST_VAL &&
		    V4L2_CTRL_ID2WHICH(id) != cs->which)
			return -EINVAL;

		/* Old-style private controls are not allowed for
		   extended controls */
		if (id >= V4L2_CID_PRIVATE_BASE)
			return -EINVAL;
		ref = find_ref_lock(hdl, id);
		if (ref == NULL)
			return -EINVAL;
		h->ref = ref;
		ctrl = ref->ctrl;
		if (ctrl->flags & V4L2_CTRL_FLAG_DISABLED)
			return -EINVAL;

		if (ctrl->cluster[0]->ncontrols > 1)
			have_clusters = true;
		if (ctrl->cluster[0] != ctrl)
			ref = find_ref_lock(hdl, ctrl->cluster[0]->id);
		if (ctrl->is_ptr && !ctrl->is_string) {
			unsigned tot_size = ctrl->elems * ctrl->elem_size;

			if (c->size < tot_size) {
				if (get) {
					c->size = tot_size;
					return -ENOSPC;
				}
				return -EFAULT;
			}
			c->size = tot_size;
		}
		/* Store the ref to the master control of the cluster */
		h->mref = ref;
		/* Initially set next to 0, meaning that there is no other
		   control in this helper array belonging to the same
		   cluster */
		h->next = 0;
	}

	/* We are done if there were no controls that belong to a multi-
	   control cluster. */
	if (!have_clusters)
		return 0;

	/* The code below figures out in O(n) time which controls in the list
	   belong to the same cluster. */

	/* This has to be done with the handler lock taken. */
	mutex_lock(hdl->lock);

	/* First zero the helper field in the master control references */
	for (i = 0; i < cs->count; i++)
		helpers[i].mref->helper = NULL;
	for (i = 0, h = helpers; i < cs->count; i++, h++) {
		struct v4l2_ctrl_ref *mref = h->mref;

		/* If the mref->helper is set, then it points to an earlier
		   helper that belongs to the same cluster. */
		if (mref->helper) {
			/* Set the next field of mref->helper to the current
			   index: this means that that earlier helper now
			   points to the next helper in the same cluster. */
			mref->helper->next = i;
			/* mref should be set only for the first helper in the
			   cluster, clear the others. */
			h->mref = NULL;
		}
		/* Point the mref helper to the current helper struct. */
		mref->helper = h;
	}
	mutex_unlock(hdl->lock);
	return 0;
}

/* Handles the corner case where cs->count == 0. It checks whether the
   specified control class exists. If that class ID is 0, then it checks
   whether there are any controls at all. */
static int class_check(struct v4l2_ctrl_handler *hdl, u32 which)
{
	if (which == 0 || which == V4L2_CTRL_WHICH_DEF_VAL ||
	    which == V4L2_CTRL_WHICH_REQUEST_VAL)
		return 0;
	return find_ref_lock(hdl, which | 1) ? 0 : -EINVAL;
}

/* Get extended controls. Allocates the helpers array if needed. */
static int v4l2_g_ext_ctrls_common(struct v4l2_ctrl_handler *hdl,
				   struct v4l2_ext_controls *cs)
{
	struct v4l2_ctrl_helper helper[4];
	struct v4l2_ctrl_helper *helpers = helper;
	int ret;
	int i, j;
	bool def_value;

	def_value = (cs->which == V4L2_CTRL_WHICH_DEF_VAL);

	cs->error_idx = cs->count;
	cs->which = V4L2_CTRL_ID2WHICH(cs->which);

	if (hdl == NULL)
		return -EINVAL;

	if (cs->count == 0)
		return class_check(hdl, cs->which);

	if (cs->count > ARRAY_SIZE(helper)) {
		helpers = kvmalloc_array(cs->count, sizeof(helper[0]),
					 GFP_KERNEL);
		if (helpers == NULL)
			return -ENOMEM;
	}

	ret = prepare_ext_ctrls(hdl, cs, helpers, true);
	cs->error_idx = cs->count;

	for (i = 0; !ret && i < cs->count; i++)
		if (helpers[i].ref->ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY)
			ret = -EACCES;

	for (i = 0; !ret && i < cs->count; i++) {
		int (*ctrl_to_user)(struct v4l2_ext_control *c,
				    struct v4l2_ctrl *ctrl);
		struct v4l2_ctrl *master;

		ctrl_to_user = def_value ? def_to_user : cur_to_user;

		if (helpers[i].mref == NULL)
			continue;

		master = helpers[i].mref->ctrl;
		cs->error_idx = i;

		v4l2_ctrl_lock(master);

		/* g_volatile_ctrl will update the new control values */
		if (!def_value &&
		    ((master->flags & V4L2_CTRL_FLAG_VOLATILE) ||
		    (master->has_volatiles && !is_cur_manual(master)))) {
			for (j = 0; j < master->ncontrols; j++)
				cur_to_new(master->cluster[j]);
			ret = call_op(master, g_volatile_ctrl);
			ctrl_to_user = new_to_user;
		}
		/* If OK, then copy the current (for non-volatile controls)
		   or the new (for volatile controls) control values to the
		   caller */
		if (!ret) {
			u32 idx = i;

			do {
				if (helpers[idx].ref->req)
					ret = req_to_user(cs->controls + idx,
						helpers[idx].ref->req);
				else
					ret = ctrl_to_user(cs->controls + idx,
						helpers[idx].ref->ctrl);
				idx = helpers[idx].next;
			} while (!ret && idx);
		}
		v4l2_ctrl_unlock(master);
	}

	if (cs->count > ARRAY_SIZE(helper))
		kvfree(helpers);
	return ret;
}

static struct media_request_object *
v4l2_ctrls_find_req_obj(struct v4l2_ctrl_handler *hdl,
			struct media_request *req, bool set)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *new_hdl;
	int ret;

	if (IS_ERR(req))
		return ERR_CAST(req);

	if (set && WARN_ON(req->state != MEDIA_REQUEST_STATE_UPDATING))
		return ERR_PTR(-EBUSY);

	obj = media_request_object_find(req, &req_ops, hdl);
	if (obj)
		return obj;
	if (!set)
		return ERR_PTR(-ENOENT);

	new_hdl = kzalloc(sizeof(*new_hdl), GFP_KERNEL);
	if (!new_hdl)
		return ERR_PTR(-ENOMEM);

	obj = &new_hdl->req_obj;
	ret = v4l2_ctrl_handler_init(new_hdl, (hdl->nr_of_buckets - 1) * 8);
	if (!ret)
		ret = v4l2_ctrl_request_bind(req, new_hdl, hdl);
	if (ret) {
		kfree(new_hdl);

		return ERR_PTR(ret);
	}

	media_request_object_get(obj);
	return obj;
}

int v4l2_g_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct media_device *mdev,
		     struct v4l2_ext_controls *cs)
{
	struct media_request_object *obj = NULL;
	struct media_request *req = NULL;
	int ret;

	if (cs->which == V4L2_CTRL_WHICH_REQUEST_VAL) {
		if (!mdev || cs->request_fd < 0)
			return -EINVAL;

		req = media_request_get_by_fd(mdev, cs->request_fd);
		if (IS_ERR(req))
			return PTR_ERR(req);

		if (req->state != MEDIA_REQUEST_STATE_COMPLETE) {
			media_request_put(req);
			return -EACCES;
		}

		ret = media_request_lock_for_access(req);
		if (ret) {
			media_request_put(req);
			return ret;
		}

		obj = v4l2_ctrls_find_req_obj(hdl, req, false);
		if (IS_ERR(obj)) {
			media_request_unlock_for_access(req);
			media_request_put(req);
			return PTR_ERR(obj);
		}

		hdl = container_of(obj, struct v4l2_ctrl_handler,
				   req_obj);
	}

	ret = v4l2_g_ext_ctrls_common(hdl, cs);

	if (obj) {
		media_request_unlock_for_access(req);
		media_request_object_put(obj);
		media_request_put(req);
	}
	return ret;
}
EXPORT_SYMBOL(v4l2_g_ext_ctrls);

/* Helper function to get a single control */
static int get_ctrl(struct v4l2_ctrl *ctrl, struct v4l2_ext_control *c)
{
	struct v4l2_ctrl *master = ctrl->cluster[0];
	int ret = 0;
	int i;

	/* Compound controls are not supported. The new_to_user() and
	 * cur_to_user() calls below would need to be modified not to access
	 * userspace memory when called from get_ctrl().
	 */
	if (!ctrl->is_int && ctrl->type != V4L2_CTRL_TYPE_INTEGER64)
		return -EINVAL;

	if (ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY)
		return -EACCES;

	v4l2_ctrl_lock(master);
	/* g_volatile_ctrl will update the current control values */
	if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
		for (i = 0; i < master->ncontrols; i++)
			cur_to_new(master->cluster[i]);
		ret = call_op(master, g_volatile_ctrl);
		new_to_user(c, ctrl);
	} else {
		cur_to_user(c, ctrl);
	}
	v4l2_ctrl_unlock(master);
	return ret;
}

int v4l2_g_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_control *control)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, control->id);
	struct v4l2_ext_control c;
	int ret;

	if (ctrl == NULL || !ctrl->is_int)
		return -EINVAL;
	ret = get_ctrl(ctrl, &c);
	control->value = c.value;
	return ret;
}
EXPORT_SYMBOL(v4l2_g_ctrl);

s32 v4l2_ctrl_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	WARN_ON(!ctrl->is_int);
	c.value = 0;
	get_ctrl(ctrl, &c);
	return c.value;
}
EXPORT_SYMBOL(v4l2_ctrl_g_ctrl);

s64 v4l2_ctrl_g_ctrl_int64(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	WARN_ON(ctrl->is_ptr || ctrl->type != V4L2_CTRL_TYPE_INTEGER64);
	c.value64 = 0;
	get_ctrl(ctrl, &c);
	return c.value64;
}
EXPORT_SYMBOL(v4l2_ctrl_g_ctrl_int64);


/* Core function that calls try/s_ctrl and ensures that the new value is
   copied to the current value on a set.
   Must be called with ctrl->handler->lock held. */
static int try_or_set_cluster(struct v4l2_fh *fh, struct v4l2_ctrl *master,
			      bool set, u32 ch_flags)
{
	bool update_flag;
	int ret;
	int i;

	/* Go through the cluster and either validate the new value or
	   (if no new value was set), copy the current value to the new
	   value, ensuring a consistent view for the control ops when
	   called. */
	for (i = 0; i < master->ncontrols; i++) {
		struct v4l2_ctrl *ctrl = master->cluster[i];

		if (ctrl == NULL)
			continue;

		if (!ctrl->is_new) {
			cur_to_new(ctrl);
			continue;
		}
		/* Check again: it may have changed since the
		   previous check in try_or_set_ext_ctrls(). */
		if (set && (ctrl->flags & V4L2_CTRL_FLAG_GRABBED))
			return -EBUSY;
	}

	ret = call_op(master, try_ctrl);

	/* Don't set if there is no change */
	if (ret || !set || !cluster_changed(master))
		return ret;
	ret = call_op(master, s_ctrl);
	if (ret)
		return ret;

	/* If OK, then make the new values permanent. */
	update_flag = is_cur_manual(master) != is_new_manual(master);

	for (i = 0; i < master->ncontrols; i++) {
		/*
		 * If we switch from auto to manual mode, and this cluster
		 * contains volatile controls, then all non-master controls
		 * have to be marked as changed. The 'new' value contains
		 * the volatile value (obtained by update_from_auto_cluster),
		 * which now has to become the current value.
		 */
		if (i && update_flag && is_new_manual(master) &&
		    master->has_volatiles && master->cluster[i])
			master->cluster[i]->has_changed = true;

		new_to_cur(fh, master->cluster[i], ch_flags |
			((update_flag && i > 0) ? V4L2_EVENT_CTRL_CH_FLAGS : 0));
	}
	return 0;
}

/* Validate controls. */
static int validate_ctrls(struct v4l2_ext_controls *cs,
			  struct v4l2_ctrl_helper *helpers, bool set)
{
	unsigned i;
	int ret = 0;

	cs->error_idx = cs->count;
	for (i = 0; i < cs->count; i++) {
		struct v4l2_ctrl *ctrl = helpers[i].ref->ctrl;
		union v4l2_ctrl_ptr p_new;

		cs->error_idx = i;

		if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
			return -EACCES;
		/* This test is also done in try_set_control_cluster() which
		   is called in atomic context, so that has the final say,
		   but it makes sense to do an up-front check as well. Once
		   an error occurs in try_set_control_cluster() some other
		   controls may have been set already and we want to do a
		   best-effort to avoid that. */
		if (set && (ctrl->flags & V4L2_CTRL_FLAG_GRABBED))
			return -EBUSY;
		/*
		 * Skip validation for now if the payload needs to be copied
		 * from userspace into kernelspace. We'll validate those later.
		 */
		if (ctrl->is_ptr)
			continue;
		if (ctrl->type == V4L2_CTRL_TYPE_INTEGER64)
			p_new.p_s64 = &cs->controls[i].value64;
		else
			p_new.p_s32 = &cs->controls[i].value;
		ret = validate_new(ctrl, p_new);
		if (ret)
			return ret;
	}
	return 0;
}

/* Obtain the current volatile values of an autocluster and mark them
   as new. */
static void update_from_auto_cluster(struct v4l2_ctrl *master)
{
	int i;

	for (i = 1; i < master->ncontrols; i++)
		cur_to_new(master->cluster[i]);
	if (!call_op(master, g_volatile_ctrl))
		for (i = 1; i < master->ncontrols; i++)
			if (master->cluster[i])
				master->cluster[i]->is_new = 1;
}

/* Try or try-and-set controls */
static int try_set_ext_ctrls_common(struct v4l2_fh *fh,
				    struct v4l2_ctrl_handler *hdl,
				    struct v4l2_ext_controls *cs, bool set)
{
	struct v4l2_ctrl_helper helper[4];
	struct v4l2_ctrl_helper *helpers = helper;
	unsigned i, j;
	int ret;

	cs->error_idx = cs->count;

	/* Default value cannot be changed */
	if (cs->which == V4L2_CTRL_WHICH_DEF_VAL)
		return -EINVAL;

	cs->which = V4L2_CTRL_ID2WHICH(cs->which);

	if (hdl == NULL)
		return -EINVAL;

	if (cs->count == 0)
		return class_check(hdl, cs->which);

	if (cs->count > ARRAY_SIZE(helper)) {
		helpers = kvmalloc_array(cs->count, sizeof(helper[0]),
					 GFP_KERNEL);
		if (!helpers)
			return -ENOMEM;
	}
	ret = prepare_ext_ctrls(hdl, cs, helpers, false);
	if (!ret)
		ret = validate_ctrls(cs, helpers, set);
	if (ret && set)
		cs->error_idx = cs->count;
	for (i = 0; !ret && i < cs->count; i++) {
		struct v4l2_ctrl *master;
		u32 idx = i;

		if (helpers[i].mref == NULL)
			continue;

		cs->error_idx = i;
		master = helpers[i].mref->ctrl;
		v4l2_ctrl_lock(master);

		/* Reset the 'is_new' flags of the cluster */
		for (j = 0; j < master->ncontrols; j++)
			if (master->cluster[j])
				master->cluster[j]->is_new = 0;

		/* For volatile autoclusters that are currently in auto mode
		   we need to discover if it will be set to manual mode.
		   If so, then we have to copy the current volatile values
		   first since those will become the new manual values (which
		   may be overwritten by explicit new values from this set
		   of controls). */
		if (master->is_auto && master->has_volatiles &&
						!is_cur_manual(master)) {
			/* Pick an initial non-manual value */
			s32 new_auto_val = master->manual_mode_value + 1;
			u32 tmp_idx = idx;

			do {
				/* Check if the auto control is part of the
				   list, and remember the new value. */
				if (helpers[tmp_idx].ref->ctrl == master)
					new_auto_val = cs->controls[tmp_idx].value;
				tmp_idx = helpers[tmp_idx].next;
			} while (tmp_idx);
			/* If the new value == the manual value, then copy
			   the current volatile values. */
			if (new_auto_val == master->manual_mode_value)
				update_from_auto_cluster(master);
		}

		/* Copy the new caller-supplied control values.
		   user_to_new() sets 'is_new' to 1. */
		do {
			struct v4l2_ctrl *ctrl = helpers[idx].ref->ctrl;

			ret = user_to_new(cs->controls + idx, ctrl);
			if (!ret && ctrl->is_ptr)
				ret = validate_new(ctrl, ctrl->p_new);
			idx = helpers[idx].next;
		} while (!ret && idx);

		if (!ret)
			ret = try_or_set_cluster(fh, master,
						 !hdl->req_obj.req && set, 0);
		if (!ret && hdl->req_obj.req && set) {
			for (j = 0; j < master->ncontrols; j++) {
				struct v4l2_ctrl_ref *ref =
					find_ref(hdl, master->cluster[j]->id);

				new_to_req(ref);
			}
		}

		/* Copy the new values back to userspace. */
		if (!ret) {
			idx = i;
			do {
				ret = new_to_user(cs->controls + idx,
						helpers[idx].ref->ctrl);
				idx = helpers[idx].next;
			} while (!ret && idx);
		}
		v4l2_ctrl_unlock(master);
	}

	if (cs->count > ARRAY_SIZE(helper))
		kvfree(helpers);
	return ret;
}

static int try_set_ext_ctrls(struct v4l2_fh *fh,
			     struct v4l2_ctrl_handler *hdl, struct media_device *mdev,
			     struct v4l2_ext_controls *cs, bool set)
{
	struct media_request_object *obj = NULL;
	struct media_request *req = NULL;
	int ret;

	if (cs->which == V4L2_CTRL_WHICH_REQUEST_VAL) {
		if (!mdev || cs->request_fd < 0)
			return -EINVAL;

		req = media_request_get_by_fd(mdev, cs->request_fd);
		if (IS_ERR(req))
			return PTR_ERR(req);

		ret = media_request_lock_for_update(req);
		if (ret) {
			media_request_put(req);
			return ret;
		}

		obj = v4l2_ctrls_find_req_obj(hdl, req, set);
		if (IS_ERR(obj)) {
			media_request_unlock_for_update(req);
			media_request_put(req);
			return PTR_ERR(obj);
		}
		hdl = container_of(obj, struct v4l2_ctrl_handler,
				   req_obj);
	}

	ret = try_set_ext_ctrls_common(fh, hdl, cs, set);

	if (obj) {
		media_request_unlock_for_update(req);
		media_request_object_put(obj);
		media_request_put(req);
	}

	return ret;
}

int v4l2_try_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct media_device *mdev,
		       struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(NULL, hdl, mdev, cs, false);
}
EXPORT_SYMBOL(v4l2_try_ext_ctrls);

int v4l2_s_ext_ctrls(struct v4l2_fh *fh, struct v4l2_ctrl_handler *hdl,
		     struct media_device *mdev, struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(fh, hdl, mdev, cs, true);
}
EXPORT_SYMBOL(v4l2_s_ext_ctrls);

/* Helper function for VIDIOC_S_CTRL compatibility */
static int set_ctrl(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 ch_flags)
{
	struct v4l2_ctrl *master = ctrl->cluster[0];
	int ret;
	int i;

	/* Reset the 'is_new' flags of the cluster */
	for (i = 0; i < master->ncontrols; i++)
		if (master->cluster[i])
			master->cluster[i]->is_new = 0;

	ret = validate_new(ctrl, ctrl->p_new);
	if (ret)
		return ret;

	/* For autoclusters with volatiles that are switched from auto to
	   manual mode we have to update the current volatile values since
	   those will become the initial manual values after such a switch. */
	if (master->is_auto && master->has_volatiles && ctrl == master &&
	    !is_cur_manual(master) && ctrl->val == master->manual_mode_value)
		update_from_auto_cluster(master);

	ctrl->is_new = 1;
	return try_or_set_cluster(fh, master, true, ch_flags);
}

/* Helper function for VIDIOC_S_CTRL compatibility */
static int set_ctrl_lock(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl,
			 struct v4l2_ext_control *c)
{
	int ret;

	v4l2_ctrl_lock(ctrl);
	user_to_new(c, ctrl);
	ret = set_ctrl(fh, ctrl, 0);
	if (!ret)
		cur_to_user(c, ctrl);
	v4l2_ctrl_unlock(ctrl);
	return ret;
}

int v4l2_s_ctrl(struct v4l2_fh *fh, struct v4l2_ctrl_handler *hdl,
					struct v4l2_control *control)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, control->id);
	struct v4l2_ext_control c = { control->id };
	int ret;

	if (ctrl == NULL || !ctrl->is_int)
		return -EINVAL;

	if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
		return -EACCES;

	c.value = control->value;
	ret = set_ctrl_lock(fh, ctrl, &c);
	control->value = c.value;
	return ret;
}
EXPORT_SYMBOL(v4l2_s_ctrl);

int __v4l2_ctrl_s_ctrl(struct v4l2_ctrl *ctrl, s32 val)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	WARN_ON(!ctrl->is_int);
	ctrl->val = val;
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl);

int __v4l2_ctrl_s_ctrl_int64(struct v4l2_ctrl *ctrl, s64 val)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	WARN_ON(ctrl->is_ptr || ctrl->type != V4L2_CTRL_TYPE_INTEGER64);
	*ctrl->p_new.p_s64 = val;
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl_int64);

int __v4l2_ctrl_s_ctrl_string(struct v4l2_ctrl *ctrl, const char *s)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	WARN_ON(ctrl->type != V4L2_CTRL_TYPE_STRING);
	strscpy(ctrl->p_new.p_char, s, ctrl->maximum + 1);
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl_string);

void v4l2_ctrl_request_complete(struct media_request *req,
				struct v4l2_ctrl_handler *main_hdl)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl_ref *ref;

	if (!req || !main_hdl)
		return;

	/*
	 * Note that it is valid if nothing was found. It means
	 * that this request doesn't have any controls and so just
	 * wants to leave the controls unchanged.
	 */
	obj = media_request_object_find(req, &req_ops, main_hdl);
	if (!obj)
		return;
	hdl = container_of(obj, struct v4l2_ctrl_handler, req_obj);

	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;
		struct v4l2_ctrl *master = ctrl->cluster[0];
		unsigned int i;

		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
			ref->req = ref;

			v4l2_ctrl_lock(master);
			/* g_volatile_ctrl will update the current control values */
			for (i = 0; i < master->ncontrols; i++)
				cur_to_new(master->cluster[i]);
			call_op(master, g_volatile_ctrl);
			new_to_req(ref);
			v4l2_ctrl_unlock(master);
			continue;
		}
		if (ref->req == ref)
			continue;

		v4l2_ctrl_lock(ctrl);
		if (ref->req)
			ptr_to_ptr(ctrl, ref->req->p_req, ref->p_req);
		else
			ptr_to_ptr(ctrl, ctrl->p_cur, ref->p_req);
		v4l2_ctrl_unlock(ctrl);
	}

	WARN_ON(!hdl->request_is_queued);
	list_del_init(&hdl->requests_queued);
	hdl->request_is_queued = false;
	media_request_object_complete(obj);
	media_request_object_put(obj);
}
EXPORT_SYMBOL(v4l2_ctrl_request_complete);

void v4l2_ctrl_request_setup(struct media_request *req,
			     struct v4l2_ctrl_handler *main_hdl)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl_ref *ref;

	if (!req || !main_hdl)
		return;

	if (WARN_ON(req->state != MEDIA_REQUEST_STATE_QUEUED))
		return;

	/*
	 * Note that it is valid if nothing was found. It means
	 * that this request doesn't have any controls and so just
	 * wants to leave the controls unchanged.
	 */
	obj = media_request_object_find(req, &req_ops, main_hdl);
	if (!obj)
		return;
	if (obj->completed) {
		media_request_object_put(obj);
		return;
	}
	hdl = container_of(obj, struct v4l2_ctrl_handler, req_obj);

	list_for_each_entry(ref, &hdl->ctrl_refs, node)
		ref->req_done = false;

	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;
		struct v4l2_ctrl *master = ctrl->cluster[0];
		bool have_new_data = false;
		int i;

		/*
		 * Skip if this control was already handled by a cluster.
		 * Skip button controls and read-only controls.
		 */
		if (ref->req_done || ctrl->type == V4L2_CTRL_TYPE_BUTTON ||
		    (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY))
			continue;

		v4l2_ctrl_lock(master);
		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				struct v4l2_ctrl_ref *r =
					find_ref(hdl, master->cluster[i]->id);

				if (r->req && r == r->req) {
					have_new_data = true;
					break;
				}
			}
		}
		if (!have_new_data) {
			v4l2_ctrl_unlock(master);
			continue;
		}

		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				struct v4l2_ctrl_ref *r =
					find_ref(hdl, master->cluster[i]->id);

				req_to_new(r);
				master->cluster[i]->is_new = 1;
				r->req_done = true;
			}
		}
		/*
		 * For volatile autoclusters that are currently in auto mode
		 * we need to discover if it will be set to manual mode.
		 * If so, then we have to copy the current volatile values
		 * first since those will become the new manual values (which
		 * may be overwritten by explicit new values from this set
		 * of controls).
		 */
		if (master->is_auto && master->has_volatiles &&
		    !is_cur_manual(master)) {
			s32 new_auto_val = *master->p_new.p_s32;

			/*
			 * If the new value == the manual value, then copy
			 * the current volatile values.
			 */
			if (new_auto_val == master->manual_mode_value)
				update_from_auto_cluster(master);
		}

		try_or_set_cluster(NULL, master, true, 0);

		v4l2_ctrl_unlock(master);
	}

	media_request_object_put(obj);
}
EXPORT_SYMBOL(v4l2_ctrl_request_setup);

void v4l2_ctrl_notify(struct v4l2_ctrl *ctrl, v4l2_ctrl_notify_fnc notify, void *priv)
{
	if (ctrl == NULL)
		return;
	if (notify == NULL) {
		ctrl->call_notify = 0;
		return;
	}
	if (WARN_ON(ctrl->handler->notify && ctrl->handler->notify != notify))
		return;
	ctrl->handler->notify = notify;
	ctrl->handler->notify_priv = priv;
	ctrl->call_notify = 1;
}
EXPORT_SYMBOL(v4l2_ctrl_notify);

int __v4l2_ctrl_modify_range(struct v4l2_ctrl *ctrl,
			s64 min, s64 max, u64 step, s64 def)
{
	bool value_changed;
	bool range_changed = false;
	int ret;

	lockdep_assert_held(ctrl->handler->lock);

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_INTEGER64:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_BITMASK:
	case V4L2_CTRL_TYPE_U8:
	case V4L2_CTRL_TYPE_U16:
	case V4L2_CTRL_TYPE_U32:
		if (ctrl->is_array)
			return -EINVAL;
		ret = check_range(ctrl->type, min, max, step, def);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}
	if ((ctrl->minimum != min) || (ctrl->maximum != max) ||
		(ctrl->step != step) || ctrl->default_value != def) {
		range_changed = true;
		ctrl->minimum = min;
		ctrl->maximum = max;
		ctrl->step = step;
		ctrl->default_value = def;
	}
	cur_to_new(ctrl);
	if (validate_new(ctrl, ctrl->p_new)) {
		if (ctrl->type == V4L2_CTRL_TYPE_INTEGER64)
			*ctrl->p_new.p_s64 = def;
		else
			*ctrl->p_new.p_s32 = def;
	}

	if (ctrl->type == V4L2_CTRL_TYPE_INTEGER64)
		value_changed = *ctrl->p_new.p_s64 != *ctrl->p_cur.p_s64;
	else
		value_changed = *ctrl->p_new.p_s32 != *ctrl->p_cur.p_s32;
	if (value_changed)
		ret = set_ctrl(NULL, ctrl, V4L2_EVENT_CTRL_CH_RANGE);
	else if (range_changed)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_RANGE);
	return ret;
}
EXPORT_SYMBOL(__v4l2_ctrl_modify_range);

static int v4l2_ctrl_add_event(struct v4l2_subscribed_event *sev, unsigned elems)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(sev->fh->ctrl_handler, sev->id);

	if (ctrl == NULL)
		return -EINVAL;

	v4l2_ctrl_lock(ctrl);
	list_add_tail(&sev->node, &ctrl->ev_subs);
	if (ctrl->type != V4L2_CTRL_TYPE_CTRL_CLASS &&
	    (sev->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL)) {
		struct v4l2_event ev;
		u32 changes = V4L2_EVENT_CTRL_CH_FLAGS;

		if (!(ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY))
			changes |= V4L2_EVENT_CTRL_CH_VALUE;
		fill_event(&ev, ctrl, changes);
		/* Mark the queue as active, allowing this initial
		   event to be accepted. */
		sev->elems = elems;
		v4l2_event_queue_fh(sev->fh, &ev);
	}
	v4l2_ctrl_unlock(ctrl);
	return 0;
}

static void v4l2_ctrl_del_event(struct v4l2_subscribed_event *sev)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(sev->fh->ctrl_handler, sev->id);

	if (ctrl == NULL)
		return;

	v4l2_ctrl_lock(ctrl);
	list_del(&sev->node);
	v4l2_ctrl_unlock(ctrl);
}

void v4l2_ctrl_replace(struct v4l2_event *old, const struct v4l2_event *new)
{
	u32 old_changes = old->u.ctrl.changes;

	old->u.ctrl = new->u.ctrl;
	old->u.ctrl.changes |= old_changes;
}
EXPORT_SYMBOL(v4l2_ctrl_replace);

void v4l2_ctrl_merge(const struct v4l2_event *old, struct v4l2_event *new)
{
	new->u.ctrl.changes |= old->u.ctrl.changes;
}
EXPORT_SYMBOL(v4l2_ctrl_merge);

const struct v4l2_subscribed_event_ops v4l2_ctrl_sub_ev_ops = {
	.add = v4l2_ctrl_add_event,
	.del = v4l2_ctrl_del_event,
	.replace = v4l2_ctrl_replace,
	.merge = v4l2_ctrl_merge,
};
EXPORT_SYMBOL(v4l2_ctrl_sub_ev_ops);

int v4l2_ctrl_log_status(struct file *file, void *fh)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_fh *vfh = file->private_data;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) && vfd->v4l2_dev)
		v4l2_ctrl_handler_log_status(vfh->ctrl_handler,
			vfd->v4l2_dev->name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_log_status);

int v4l2_ctrl_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_CTRL)
		return v4l2_event_subscribe(fh, sub, 0, &v4l2_ctrl_sub_ev_ops);
	return -EINVAL;
}
EXPORT_SYMBOL(v4l2_ctrl_subscribe_event);

int v4l2_ctrl_subdev_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				     struct v4l2_event_subscription *sub)
{
	if (!sd->ctrl_handler)
		return -EINVAL;
	return v4l2_ctrl_subscribe_event(fh, sub);
}
EXPORT_SYMBOL(v4l2_ctrl_subdev_subscribe_event);

__poll_t v4l2_ctrl_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2_fh *fh = file->private_data;

	if (v4l2_event_pending(fh))
		return EPOLLPRI;
	poll_wait(file, &fh->wait, wait);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_poll);
