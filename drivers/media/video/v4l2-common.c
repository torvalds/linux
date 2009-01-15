/*
 *	Video for Linux Two
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *	This file replaces the videodev.c file that comes with the
 *	regular kernel distribution.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Author:	Bill Dirks <bill@thedirks.org>
 *		based on code by Alan Cox, <alan@cymru.net>
 *
 */

/*
 * Video capture interface for Linux
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *
 * Fixes:
 */

/*
 * Video4linux 1/2 integration by Justin Schoeman
 * <justin@suntiger.ee.up.ac.za>
 * 2.4 PROCFS support ported from 2.4 kernels by
 *  Iñaki García Etxebarria <garetxe@euskalnet.net>
 * Makefile fix by "W. Michael Petullo" <mike@flyn.org>
 * 2.4 devfs support ported from 2.4 kernels by
 *  Dan Merillat <dan@merillat.org>
 * Added Gerd Knorrs v4l1 enhancements (Justin Schoeman)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/div64.h>
#define __OLD_VIDIOC_ /* To allow fixing old calls*/
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

#include <linux/videodev2.h>

MODULE_AUTHOR("Bill Dirks, Justin Schoeman, Gerd Knorr");
MODULE_DESCRIPTION("misc helper functions for v4l2 device drivers");
MODULE_LICENSE("GPL");

/*
 *
 *	V 4 L 2   D R I V E R   H E L P E R   A P I
 *
 */

/*
 *  Video Standard Operations (contributed by Michael Schimek)
 */


/* ----------------------------------------------------------------- */
/* priority handling                                                 */

#define V4L2_PRIO_VALID(val) (val == V4L2_PRIORITY_BACKGROUND   || \
			      val == V4L2_PRIORITY_INTERACTIVE  || \
			      val == V4L2_PRIORITY_RECORD)

int v4l2_prio_init(struct v4l2_prio_state *global)
{
	memset(global,0,sizeof(*global));
	return 0;
}
EXPORT_SYMBOL(v4l2_prio_init);

int v4l2_prio_change(struct v4l2_prio_state *global, enum v4l2_priority *local,
		     enum v4l2_priority new)
{
	if (!V4L2_PRIO_VALID(new))
		return -EINVAL;
	if (*local == new)
		return 0;

	atomic_inc(&global->prios[new]);
	if (V4L2_PRIO_VALID(*local))
		atomic_dec(&global->prios[*local]);
	*local = new;
	return 0;
}
EXPORT_SYMBOL(v4l2_prio_change);

int v4l2_prio_open(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	return v4l2_prio_change(global,local,V4L2_PRIORITY_DEFAULT);
}
EXPORT_SYMBOL(v4l2_prio_open);

int v4l2_prio_close(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	if (V4L2_PRIO_VALID(*local))
		atomic_dec(&global->prios[*local]);
	return 0;
}
EXPORT_SYMBOL(v4l2_prio_close);

enum v4l2_priority v4l2_prio_max(struct v4l2_prio_state *global)
{
	if (atomic_read(&global->prios[V4L2_PRIORITY_RECORD]) > 0)
		return V4L2_PRIORITY_RECORD;
	if (atomic_read(&global->prios[V4L2_PRIORITY_INTERACTIVE]) > 0)
		return V4L2_PRIORITY_INTERACTIVE;
	if (atomic_read(&global->prios[V4L2_PRIORITY_BACKGROUND]) > 0)
		return V4L2_PRIORITY_BACKGROUND;
	return V4L2_PRIORITY_UNSET;
}
EXPORT_SYMBOL(v4l2_prio_max);

int v4l2_prio_check(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	if (*local < v4l2_prio_max(global))
		return -EBUSY;
	return 0;
}
EXPORT_SYMBOL(v4l2_prio_check);

/* ----------------------------------------------------------------- */

/* Helper functions for control handling			     */

/* Check for correctness of the ctrl's value based on the data from
   struct v4l2_queryctrl and the available menu items. Note that
   menu_items may be NULL, in that case it is ignored. */
int v4l2_ctrl_check(struct v4l2_ext_control *ctrl, struct v4l2_queryctrl *qctrl,
		const char **menu_items)
{
	if (qctrl->flags & V4L2_CTRL_FLAG_DISABLED)
		return -EINVAL;
	if (qctrl->flags & V4L2_CTRL_FLAG_GRABBED)
		return -EBUSY;
	if (qctrl->type == V4L2_CTRL_TYPE_BUTTON ||
	    qctrl->type == V4L2_CTRL_TYPE_INTEGER64 ||
	    qctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
		return 0;
	if (ctrl->value < qctrl->minimum || ctrl->value > qctrl->maximum)
		return -ERANGE;
	if (qctrl->type == V4L2_CTRL_TYPE_MENU && menu_items != NULL) {
		if (menu_items[ctrl->value] == NULL ||
		    menu_items[ctrl->value][0] == '\0')
			return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_check);

/* Returns NULL or a character pointer array containing the menu for
   the given control ID. The pointer array ends with a NULL pointer.
   An empty string signifies a menu entry that is invalid. This allows
   drivers to disable certain options if it is not supported. */
const char **v4l2_ctrl_get_menu(u32 id)
{
	static const char *mpeg_audio_sampling_freq[] = {
		"44.1 kHz",
		"48 kHz",
		"32 kHz",
		NULL
	};
	static const char *mpeg_audio_encoding[] = {
		"MPEG-1/2 Layer I",
		"MPEG-1/2 Layer II",
		"MPEG-1/2 Layer III",
		"MPEG-2/4 AAC",
		"AC-3",
		NULL
	};
	static const char *mpeg_audio_l1_bitrate[] = {
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
	static const char *mpeg_audio_l2_bitrate[] = {
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
	static const char *mpeg_audio_l3_bitrate[] = {
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
	static const char *mpeg_audio_ac3_bitrate[] = {
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
	static const char *mpeg_audio_mode[] = {
		"Stereo",
		"Joint Stereo",
		"Dual",
		"Mono",
		NULL
	};
	static const char *mpeg_audio_mode_extension[] = {
		"Bound 4",
		"Bound 8",
		"Bound 12",
		"Bound 16",
		NULL
	};
	static const char *mpeg_audio_emphasis[] = {
		"No Emphasis",
		"50/15 us",
		"CCITT J17",
		NULL
	};
	static const char *mpeg_audio_crc[] = {
		"No CRC",
		"16-bit CRC",
		NULL
	};
	static const char *mpeg_video_encoding[] = {
		"MPEG-1",
		"MPEG-2",
		"MPEG-4 AVC",
		NULL
	};
	static const char *mpeg_video_aspect[] = {
		"1x1",
		"4x3",
		"16x9",
		"2.21x1",
		NULL
	};
	static const char *mpeg_video_bitrate_mode[] = {
		"Variable Bitrate",
		"Constant Bitrate",
		NULL
	};
	static const char *mpeg_stream_type[] = {
		"MPEG-2 Program Stream",
		"MPEG-2 Transport Stream",
		"MPEG-1 System Stream",
		"MPEG-2 DVD-compatible Stream",
		"MPEG-1 VCD-compatible Stream",
		"MPEG-2 SVCD-compatible Stream",
		NULL
	};
	static const char *mpeg_stream_vbi_fmt[] = {
		"No VBI",
		"Private packet, IVTV format",
		NULL
	};
	static const char *camera_power_line_frequency[] = {
		"Disabled",
		"50 Hz",
		"60 Hz",
		NULL
	};
	static const char *camera_exposure_auto[] = {
		"Auto Mode",
		"Manual Mode",
		"Shutter Priority Mode",
		"Aperture Priority Mode",
		NULL
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
		default:
			return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_menu);

/* Return the control name. */
const char *v4l2_ctrl_get_name(u32 id)
{
	switch (id) {
	/* USER controls */
	case V4L2_CID_USER_CLASS: 		return "User Controls";
	case V4L2_CID_AUDIO_VOLUME: 		return "Volume";
	case V4L2_CID_AUDIO_MUTE: 		return "Mute";
	case V4L2_CID_AUDIO_BALANCE: 		return "Balance";
	case V4L2_CID_AUDIO_BASS: 		return "Bass";
	case V4L2_CID_AUDIO_TREBLE: 		return "Treble";
	case V4L2_CID_AUDIO_LOUDNESS: 		return "Loudness";
	case V4L2_CID_BRIGHTNESS: 		return "Brightness";
	case V4L2_CID_CONTRAST: 		return "Contrast";
	case V4L2_CID_SATURATION: 		return "Saturation";
	case V4L2_CID_HUE: 			return "Hue";
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
	case V4L2_CID_HCENTER:			return "Horizontal Center";
	case V4L2_CID_VCENTER:			return "Vertical Center";
	case V4L2_CID_POWER_LINE_FREQUENCY:	return "Power Line Frequency";
	case V4L2_CID_HUE_AUTO:			return "Hue, Automatic";
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE: return "White Balance Temperature";
	case V4L2_CID_SHARPNESS:		return "Sharpness";
	case V4L2_CID_BACKLIGHT_COMPENSATION:	return "Backlight Compensation";
	case V4L2_CID_CHROMA_AGC:		return "Chroma AGC";
	case V4L2_CID_COLOR_KILLER:		return "Color Killer";

	/* MPEG controls */
	case V4L2_CID_MPEG_CLASS: 		return "MPEG Encoder Controls";
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ: return "Audio Sampling Frequency";
	case V4L2_CID_MPEG_AUDIO_ENCODING: 	return "Audio Encoding";
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE: 	return "Audio Layer I Bitrate";
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE: 	return "Audio Layer II Bitrate";
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE: 	return "Audio Layer III Bitrate";
	case V4L2_CID_MPEG_AUDIO_AAC_BITRATE: 	return "Audio AAC Bitrate";
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE: 	return "Audio AC-3 Bitrate";
	case V4L2_CID_MPEG_AUDIO_MODE: 		return "Audio Stereo Mode";
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION: return "Audio Stereo Mode Extension";
	case V4L2_CID_MPEG_AUDIO_EMPHASIS: 	return "Audio Emphasis";
	case V4L2_CID_MPEG_AUDIO_CRC: 		return "Audio CRC";
	case V4L2_CID_MPEG_AUDIO_MUTE: 		return "Audio Mute";
	case V4L2_CID_MPEG_VIDEO_ENCODING: 	return "Video Encoding";
	case V4L2_CID_MPEG_VIDEO_ASPECT: 	return "Video Aspect";
	case V4L2_CID_MPEG_VIDEO_B_FRAMES: 	return "Video B Frames";
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE: 	return "Video GOP Size";
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE: 	return "Video GOP Closure";
	case V4L2_CID_MPEG_VIDEO_PULLDOWN: 	return "Video Pulldown";
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE: 	return "Video Bitrate Mode";
	case V4L2_CID_MPEG_VIDEO_BITRATE: 	return "Video Bitrate";
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK: 	return "Video Peak Bitrate";
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION: return "Video Temporal Decimation";
	case V4L2_CID_MPEG_VIDEO_MUTE: 		return "Video Mute";
	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:	return "Video Mute YUV";
	case V4L2_CID_MPEG_STREAM_TYPE: 	return "Stream Type";
	case V4L2_CID_MPEG_STREAM_PID_PMT: 	return "Stream PMT Program ID";
	case V4L2_CID_MPEG_STREAM_PID_AUDIO: 	return "Stream Audio Program ID";
	case V4L2_CID_MPEG_STREAM_PID_VIDEO: 	return "Stream Video Program ID";
	case V4L2_CID_MPEG_STREAM_PID_PCR: 	return "Stream PCR Program ID";
	case V4L2_CID_MPEG_STREAM_PES_ID_AUDIO: return "Stream PES Audio ID";
	case V4L2_CID_MPEG_STREAM_PES_ID_VIDEO: return "Stream PES Video ID";
	case V4L2_CID_MPEG_STREAM_VBI_FMT:	return "Stream VBI Format";

	/* CAMERA controls */
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
	case V4L2_CID_FOCUS_AUTO:		return "Focus, Automatic";
	case V4L2_CID_ZOOM_ABSOLUTE:		return "Zoom, Absolute";
	case V4L2_CID_ZOOM_RELATIVE:		return "Zoom, Relative";
	case V4L2_CID_ZOOM_CONTINUOUS:		return "Zoom, Continuous";
	case V4L2_CID_PRIVACY:			return "Privacy";

	default:
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_name);

/* Fill in a struct v4l2_queryctrl */
int v4l2_ctrl_query_fill(struct v4l2_queryctrl *qctrl, s32 min, s32 max, s32 step, s32 def)
{
	const char *name = v4l2_ctrl_get_name(qctrl->id);

	qctrl->flags = 0;
	if (name == NULL)
		return -EINVAL;

	switch (qctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_LOUDNESS:
	case V4L2_CID_AUTO_WHITE_BALANCE:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HUE_AUTO:
	case V4L2_CID_MPEG_AUDIO_MUTE:
	case V4L2_CID_MPEG_VIDEO_MUTE:
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
	case V4L2_CID_MPEG_VIDEO_PULLDOWN:
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
	case V4L2_CID_PRIVACY:
		qctrl->type = V4L2_CTRL_TYPE_BOOLEAN;
		min = 0;
		max = step = 1;
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
	case V4L2_CID_MPEG_VIDEO_ENCODING:
	case V4L2_CID_MPEG_VIDEO_ASPECT:
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
	case V4L2_CID_MPEG_STREAM_TYPE:
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
	case V4L2_CID_EXPOSURE_AUTO:
		qctrl->type = V4L2_CTRL_TYPE_MENU;
		step = 1;
		break;
	case V4L2_CID_USER_CLASS:
	case V4L2_CID_CAMERA_CLASS:
	case V4L2_CID_MPEG_CLASS:
		qctrl->type = V4L2_CTRL_TYPE_CTRL_CLASS;
		qctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
		min = max = step = def = 0;
		break;
	default:
		qctrl->type = V4L2_CTRL_TYPE_INTEGER;
		break;
	}
	switch (qctrl->id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
	case V4L2_CID_MPEG_AUDIO_MODE:
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
	case V4L2_CID_MPEG_STREAM_TYPE:
		qctrl->flags |= V4L2_CTRL_FLAG_UPDATE;
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		break;
	}
	qctrl->minimum = min;
	qctrl->maximum = max;
	qctrl->step = step;
	qctrl->default_value = def;
	qctrl->reserved[0] = qctrl->reserved[1] = 0;
	snprintf(qctrl->name, sizeof(qctrl->name), name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_query_fill);

/* Fill in a struct v4l2_queryctrl with standard values based on
   the control ID. */
int v4l2_ctrl_query_fill_std(struct v4l2_queryctrl *qctrl)
{
	switch (qctrl->id) {
	/* USER controls */
	case V4L2_CID_USER_CLASS:
	case V4L2_CID_MPEG_CLASS:
		return v4l2_ctrl_query_fill(qctrl, 0, 0, 0, 0);
	case V4L2_CID_AUDIO_VOLUME:
		return v4l2_ctrl_query_fill(qctrl, 0, 65535, 65535 / 100, 58880);
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 0);
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
		return v4l2_ctrl_query_fill(qctrl, 0, 65535, 65535 / 100, 32768);
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 128);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(qctrl, 0, 127, 1, 64);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(qctrl, -128, 127, 1, 0);

	/* MPEG controls */
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100,
				V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000, 1,
				V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000);
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_1,
				V4L2_MPEG_AUDIO_ENCODING_AC3, 1,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_2);
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_L1_BITRATE_32K,
				V4L2_MPEG_AUDIO_L1_BITRATE_448K, 1,
				V4L2_MPEG_AUDIO_L1_BITRATE_256K);
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_L2_BITRATE_32K,
				V4L2_MPEG_AUDIO_L2_BITRATE_384K, 1,
				V4L2_MPEG_AUDIO_L2_BITRATE_224K);
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_L3_BITRATE_32K,
				V4L2_MPEG_AUDIO_L3_BITRATE_320K, 1,
				V4L2_MPEG_AUDIO_L3_BITRATE_192K);
	case V4L2_CID_MPEG_AUDIO_AAC_BITRATE:
		return v4l2_ctrl_query_fill(qctrl, 0, 6400, 1, 3200000);
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_AC3_BITRATE_32K,
				V4L2_MPEG_AUDIO_AC3_BITRATE_640K, 1,
				V4L2_MPEG_AUDIO_AC3_BITRATE_384K);
	case V4L2_CID_MPEG_AUDIO_MODE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_MODE_STEREO,
				V4L2_MPEG_AUDIO_MODE_MONO, 1,
				V4L2_MPEG_AUDIO_MODE_STEREO);
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4,
				V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_16, 1,
				V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4);
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_EMPHASIS_NONE,
				V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17, 1,
				V4L2_MPEG_AUDIO_EMPHASIS_NONE);
	case V4L2_CID_MPEG_AUDIO_CRC:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_CRC_NONE,
				V4L2_MPEG_AUDIO_CRC_CRC16, 1,
				V4L2_MPEG_AUDIO_CRC_NONE);
	case V4L2_CID_MPEG_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 0);
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_VIDEO_ENCODING_MPEG_1,
				V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC, 1,
				V4L2_MPEG_VIDEO_ENCODING_MPEG_2);
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_VIDEO_ASPECT_1x1,
				V4L2_MPEG_VIDEO_ASPECT_221x100, 1,
				V4L2_MPEG_VIDEO_ASPECT_4x3);
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		return v4l2_ctrl_query_fill(qctrl, 0, 33, 1, 2);
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		return v4l2_ctrl_query_fill(qctrl, 1, 34, 1, 12);
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 1);
	case V4L2_CID_MPEG_VIDEO_PULLDOWN:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 0);
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
				V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 1,
				V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		return v4l2_ctrl_query_fill(qctrl, 0, 27000000, 1, 6000000);
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		return v4l2_ctrl_query_fill(qctrl, 0, 27000000, 1, 8000000);
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION:
		return v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 0);
	case V4L2_CID_MPEG_VIDEO_MUTE:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 0);
	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:  /* Init YUV (really YCbCr) to black */
		return v4l2_ctrl_query_fill(qctrl, 0, 0xffffff, 1, 0x008080);
	case V4L2_CID_MPEG_STREAM_TYPE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_STREAM_TYPE_MPEG2_PS,
				V4L2_MPEG_STREAM_TYPE_MPEG2_SVCD, 1,
				V4L2_MPEG_STREAM_TYPE_MPEG2_PS);
	case V4L2_CID_MPEG_STREAM_PID_PMT:
		return v4l2_ctrl_query_fill(qctrl, 0, (1 << 14) - 1, 1, 16);
	case V4L2_CID_MPEG_STREAM_PID_AUDIO:
		return v4l2_ctrl_query_fill(qctrl, 0, (1 << 14) - 1, 1, 260);
	case V4L2_CID_MPEG_STREAM_PID_VIDEO:
		return v4l2_ctrl_query_fill(qctrl, 0, (1 << 14) - 1, 1, 256);
	case V4L2_CID_MPEG_STREAM_PID_PCR:
		return v4l2_ctrl_query_fill(qctrl, 0, (1 << 14) - 1, 1, 259);
	case V4L2_CID_MPEG_STREAM_PES_ID_AUDIO:
		return v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 0);
	case V4L2_CID_MPEG_STREAM_PES_ID_VIDEO:
		return v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 0);
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_STREAM_VBI_FMT_NONE,
				V4L2_MPEG_STREAM_VBI_FMT_IVTV, 1,
				V4L2_MPEG_STREAM_VBI_FMT_NONE);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_query_fill_std);

/* Fill in a struct v4l2_querymenu based on the struct v4l2_queryctrl and
   the menu. The qctrl pointer may be NULL, in which case it is ignored.
   If menu_items is NULL, then the menu items are retrieved using
   v4l2_ctrl_get_menu. */
int v4l2_ctrl_query_menu(struct v4l2_querymenu *qmenu, struct v4l2_queryctrl *qctrl,
	       const char **menu_items)
{
	int i;

	qmenu->reserved = 0;
	if (menu_items == NULL)
		menu_items = v4l2_ctrl_get_menu(qmenu->id);
	if (menu_items == NULL ||
	    (qctrl && (qmenu->index < qctrl->minimum || qmenu->index > qctrl->maximum)))
		return -EINVAL;
	for (i = 0; i < qmenu->index && menu_items[i]; i++) ;
	if (menu_items[i] == NULL || menu_items[i][0] == '\0')
		return -EINVAL;
	snprintf(qmenu->name, sizeof(qmenu->name), menu_items[qmenu->index]);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_query_menu);

/* Fill in a struct v4l2_querymenu based on the specified array of valid
   menu items (terminated by V4L2_CTRL_MENU_IDS_END).
   Use this if there are 'holes' in the list of valid menu items. */
int v4l2_ctrl_query_menu_valid_items(struct v4l2_querymenu *qmenu, const u32 *ids)
{
	const char **menu_items = v4l2_ctrl_get_menu(qmenu->id);

	qmenu->reserved = 0;
	if (menu_items == NULL || ids == NULL)
		return -EINVAL;
	while (*ids != V4L2_CTRL_MENU_IDS_END) {
		if (*ids++ == qmenu->index) {
			snprintf(qmenu->name, sizeof(qmenu->name),
				       menu_items[qmenu->index]);
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL(v4l2_ctrl_query_menu_valid_items);

/* ctrl_classes points to an array of u32 pointers, the last element is
   a NULL pointer. Each u32 array is a 0-terminated array of control IDs.
   Each array must be sorted low to high and belong to the same control
   class. The array of u32 pointer must also be sorted, from low class IDs
   to high class IDs.

   This function returns the first ID that follows after the given ID.
   When no more controls are available 0 is returned. */
u32 v4l2_ctrl_next(const u32 * const * ctrl_classes, u32 id)
{
	u32 ctrl_class = V4L2_CTRL_ID2CLASS(id);
	const u32 *pctrl;

	if (ctrl_classes == NULL)
		return 0;

	/* if no query is desired, then check if the ID is part of ctrl_classes */
	if ((id & V4L2_CTRL_FLAG_NEXT_CTRL) == 0) {
		/* find class */
		while (*ctrl_classes && V4L2_CTRL_ID2CLASS(**ctrl_classes) != ctrl_class)
			ctrl_classes++;
		if (*ctrl_classes == NULL)
			return 0;
		pctrl = *ctrl_classes;
		/* find control ID */
		while (*pctrl && *pctrl != id) pctrl++;
		return *pctrl ? id : 0;
	}
	id &= V4L2_CTRL_ID_MASK;
	id++;	/* select next control */
	/* find first class that matches (or is greater than) the class of
	   the ID */
	while (*ctrl_classes && V4L2_CTRL_ID2CLASS(**ctrl_classes) < ctrl_class)
		ctrl_classes++;
	/* no more classes */
	if (*ctrl_classes == NULL)
		return 0;
	pctrl = *ctrl_classes;
	/* find first ctrl within the class that is >= ID */
	while (*pctrl && *pctrl < id) pctrl++;
	if (*pctrl)
		return *pctrl;
	/* we are at the end of the controls of the current class. */
	/* continue with next class if available */
	ctrl_classes++;
	if (*ctrl_classes == NULL)
		return 0;
	return **ctrl_classes;
}
EXPORT_SYMBOL(v4l2_ctrl_next);

int v4l2_chip_match_host(const struct v4l2_dbg_match *match)
{
	switch (match->type) {
	case V4L2_CHIP_MATCH_HOST:
		return match->addr == 0;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(v4l2_chip_match_host);

#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
int v4l2_chip_match_i2c_client(struct i2c_client *c, const struct v4l2_dbg_match *match)
{
	int len;

	if (c == NULL || match == NULL)
		return 0;

	switch (match->type) {
	case V4L2_CHIP_MATCH_I2C_DRIVER:
		if (c->driver == NULL || c->driver->driver.name == NULL)
			return 0;
		len = strlen(c->driver->driver.name);
		/* legacy drivers have a ' suffix, don't try to match that */
		if (len && c->driver->driver.name[len - 1] == '\'')
			len--;
		return len && !strncmp(c->driver->driver.name, match->name, len);
	case V4L2_CHIP_MATCH_I2C_ADDR:
		return c->addr == match->addr;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(v4l2_chip_match_i2c_client);

int v4l2_chip_ident_i2c_client(struct i2c_client *c, struct v4l2_dbg_chip_ident *chip,
		u32 ident, u32 revision)
{
	if (!v4l2_chip_match_i2c_client(c, &chip->match))
		return 0;
	if (chip->ident == V4L2_IDENT_NONE) {
		chip->ident = ident;
		chip->revision = revision;
	}
	else {
		chip->ident = V4L2_IDENT_AMBIGUOUS;
		chip->revision = 0;
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_chip_ident_i2c_client);

/* ----------------------------------------------------------------- */

/* Helper function for I2C legacy drivers */

int v4l2_i2c_attach(struct i2c_adapter *adapter, int address, struct i2c_driver *driver,
		const char *name,
		int (*probe)(struct i2c_client *, const struct i2c_device_id *))
{
	struct i2c_client *client;
	int err;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->addr = address;
	client->adapter = adapter;
	client->driver = driver;
	strlcpy(client->name, name, sizeof(client->name));

	err = probe(client, NULL);
	if (err == 0) {
		i2c_attach_client(client);
	} else {
		kfree(client);
	}
	return err != -ENOMEM ? 0 : err;
}
EXPORT_SYMBOL(v4l2_i2c_attach);

void v4l2_i2c_subdev_init(struct v4l2_subdev *sd, struct i2c_client *client,
		const struct v4l2_subdev_ops *ops)
{
	v4l2_subdev_init(sd, ops);
	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = client->driver->driver.owner;
	/* i2c_client and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, client);
	i2c_set_clientdata(client, sd);
	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
		client->driver->driver.name, i2c_adapter_id(client->adapter),
		client->addr);
}
EXPORT_SYMBOL_GPL(v4l2_i2c_subdev_init);



/* Load an i2c sub-device. It assumes that i2c_get_adapdata(adapter)
   returns the v4l2_device and that i2c_get_clientdata(client)
   returns the v4l2_subdev. */
struct v4l2_subdev *v4l2_i2c_new_subdev(struct i2c_adapter *adapter,
		const char *module_name, const char *client_type, u8 addr)
{
	struct v4l2_device *dev = i2c_get_adapdata(adapter);
	struct v4l2_subdev *sd = NULL;
	struct i2c_client *client;
	struct i2c_board_info info;

	BUG_ON(!dev);
#ifdef MODULE
	if (module_name)
		request_module(module_name);
#endif
	/* Setup the i2c board info with the device type and
	   the device address. */
	memset(&info, 0, sizeof(info));
	strlcpy(info.type, client_type, sizeof(info.type));
	info.addr = addr;

	/* Create the i2c client */
	client = i2c_new_device(adapter, &info);
	/* Note: it is possible in the future that
	   c->driver is NULL if the driver is still being loaded.
	   We need better support from the kernel so that we
	   can easily wait for the load to finish. */
	if (client == NULL || client->driver == NULL)
		return NULL;

	/* Lock the module so we can safely get the v4l2_subdev pointer */
	if (!try_module_get(client->driver->driver.owner))
		return NULL;
	sd = i2c_get_clientdata(client);

	/* Register with the v4l2_device which increases the module's
	   use count as well. */
	if (v4l2_device_register_subdev(dev, sd))
		sd = NULL;
	/* Decrease the module use count to match the first try_module_get. */
	module_put(client->driver->driver.owner);
	return sd;

}
EXPORT_SYMBOL_GPL(v4l2_i2c_new_subdev);

/* Probe and load an i2c sub-device. It assumes that i2c_get_adapdata(adapter)
   returns the v4l2_device and that i2c_get_clientdata(client)
   returns the v4l2_subdev. */
struct v4l2_subdev *v4l2_i2c_new_probed_subdev(struct i2c_adapter *adapter,
	const char *module_name, const char *client_type,
	const unsigned short *addrs)
{
	struct v4l2_device *dev = i2c_get_adapdata(adapter);
	struct v4l2_subdev *sd = NULL;
	struct i2c_client *client = NULL;
	struct i2c_board_info info;

	BUG_ON(!dev);
#ifdef MODULE
	if (module_name)
		request_module(module_name);
#endif
	/* Setup the i2c board info with the device type and
	   the device address. */
	memset(&info, 0, sizeof(info));
	strlcpy(info.type, client_type, sizeof(info.type));

	/* Probe and create the i2c client */
	client = i2c_new_probed_device(adapter, &info, addrs);
	/* Note: it is possible in the future that
	   c->driver is NULL if the driver is still being loaded.
	   We need better support from the kernel so that we
	   can easily wait for the load to finish. */
	if (client == NULL || client->driver == NULL)
		return NULL;

	/* Lock the module so we can safely get the v4l2_subdev pointer */
	if (!try_module_get(client->driver->driver.owner))
		return NULL;
	sd = i2c_get_clientdata(client);

	/* Register with the v4l2_device which increases the module's
	   use count as well. */
	if (v4l2_device_register_subdev(dev, sd))
		sd = NULL;
	/* Decrease the module use count to match the first try_module_get. */
	module_put(client->driver->driver.owner);
	return sd;
}
EXPORT_SYMBOL_GPL(v4l2_i2c_new_probed_subdev);

#endif
