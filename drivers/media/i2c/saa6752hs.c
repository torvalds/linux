// SPDX-License-Identifier: GPL-2.0-or-later
 /*
    saa6752hs - i2c-driver for the saa6752hs by Philips

    Copyright (C) 2004 Andrew de Quincey

    AC-3 support:

    Copyright (C) 2008 Hans Verkuil <hverkuil@xs4all.nl>

  */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-common.h>

#define MPEG_VIDEO_TARGET_BITRATE_MAX  27000
#define MPEG_VIDEO_MAX_BITRATE_MAX     27000
#define MPEG_TOTAL_TARGET_BITRATE_MAX  27000
#define MPEG_PID_MAX ((1 << 14) - 1)


MODULE_DESCRIPTION("device driver for saa6752hs MPEG2 encoder");
MODULE_AUTHOR("Andrew de Quincey");
MODULE_LICENSE("GPL");

enum saa6752hs_videoformat {
	SAA6752HS_VF_D1 = 0,    /* standard D1 video format: 720x576 */
	SAA6752HS_VF_2_3_D1 = 1,/* 2/3D1 video format: 480x576 */
	SAA6752HS_VF_1_2_D1 = 2,/* 1/2D1 video format: 352x576 */
	SAA6752HS_VF_SIF = 3,   /* SIF video format: 352x288 */
	SAA6752HS_VF_UNKNOWN,
};

struct saa6752hs_mpeg_params {
	/* transport streams */
	__u16				ts_pid_pmt;
	__u16				ts_pid_audio;
	__u16				ts_pid_video;
	__u16				ts_pid_pcr;

	/* audio */
	enum v4l2_mpeg_audio_encoding    au_encoding;
	enum v4l2_mpeg_audio_l2_bitrate  au_l2_bitrate;
	enum v4l2_mpeg_audio_ac3_bitrate au_ac3_bitrate;

	/* video */
	enum v4l2_mpeg_video_aspect	vi_aspect;
	enum v4l2_mpeg_video_bitrate_mode vi_bitrate_mode;
	__u32				vi_bitrate;
	__u32				vi_bitrate_peak;
};

static const struct v4l2_format v4l2_format_table[] =
{
	[SAA6752HS_VF_D1] =
		{ .fmt = { .pix = { .width = 720, .height = 576 }}},
	[SAA6752HS_VF_2_3_D1] =
		{ .fmt = { .pix = { .width = 480, .height = 576 }}},
	[SAA6752HS_VF_1_2_D1] =
		{ .fmt = { .pix = { .width = 352, .height = 576 }}},
	[SAA6752HS_VF_SIF] =
		{ .fmt = { .pix = { .width = 352, .height = 288 }}},
	[SAA6752HS_VF_UNKNOWN] =
		{ .fmt = { .pix = { .width = 0, .height = 0}}},
};

struct saa6752hs_state {
	struct v4l2_subdev            sd;
	struct v4l2_ctrl_handler      hdl;
	struct { /* video bitrate mode control cluster */
		struct v4l2_ctrl *video_bitrate_mode;
		struct v4l2_ctrl *video_bitrate;
		struct v4l2_ctrl *video_bitrate_peak;
	};
	u32			      revision;
	int			      has_ac3;
	struct saa6752hs_mpeg_params  params;
	enum saa6752hs_videoformat    video_format;
	v4l2_std_id                   standard;
};

enum saa6752hs_command {
	SAA6752HS_COMMAND_RESET = 0,
	SAA6752HS_COMMAND_STOP = 1,
	SAA6752HS_COMMAND_START = 2,
	SAA6752HS_COMMAND_PAUSE = 3,
	SAA6752HS_COMMAND_RECONFIGURE = 4,
	SAA6752HS_COMMAND_SLEEP = 5,
	SAA6752HS_COMMAND_RECONFIGURE_FORCE = 6,

	SAA6752HS_COMMAND_MAX
};

static inline struct saa6752hs_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa6752hs_state, sd);
}

/* ---------------------------------------------------------------------- */

static const u8 PAT[] = {
	0xc2, /* i2c register */
	0x00, /* table number for encoder */

	0x47, /* sync */
	0x40, 0x00, /* transport_error_indicator(0), payload_unit_start(1), transport_priority(0), pid(0) */
	0x10, /* transport_scrambling_control(00), adaptation_field_control(01), continuity_counter(0) */

	0x00, /* PSI pointer to start of table */

	0x00, /* tid(0) */
	0xb0, 0x0d, /* section_syntax_indicator(1), section_length(13) */

	0x00, 0x01, /* transport_stream_id(1) */

	0xc1, /* version_number(0), current_next_indicator(1) */

	0x00, 0x00, /* section_number(0), last_section_number(0) */

	0x00, 0x01, /* program_number(1) */

	0xe0, 0x00, /* PMT PID */

	0x00, 0x00, 0x00, 0x00 /* CRC32 */
};

static const u8 PMT[] = {
	0xc2, /* i2c register */
	0x01, /* table number for encoder */

	0x47, /* sync */
	0x40, 0x00, /* transport_error_indicator(0), payload_unit_start(1), transport_priority(0), pid */
	0x10, /* transport_scrambling_control(00), adaptation_field_control(01), continuity_counter(0) */

	0x00, /* PSI pointer to start of table */

	0x02, /* tid(2) */
	0xb0, 0x17, /* section_syntax_indicator(1), section_length(23) */

	0x00, 0x01, /* program_number(1) */

	0xc1, /* version_number(0), current_next_indicator(1) */

	0x00, 0x00, /* section_number(0), last_section_number(0) */

	0xe0, 0x00, /* PCR_PID */

	0xf0, 0x00, /* program_info_length(0) */

	0x02, 0xe0, 0x00, 0xf0, 0x00, /* video stream type(2), pid */
	0x04, 0xe0, 0x00, 0xf0, 0x00, /* audio stream type(4), pid */

	0x00, 0x00, 0x00, 0x00 /* CRC32 */
};

static const u8 PMT_AC3[] = {
	0xc2, /* i2c register */
	0x01, /* table number for encoder(1) */
	0x47, /* sync */

	0x40, /* transport_error_indicator(0), payload_unit_start(1), transport_priority(0) */
	0x10, /* PMT PID (0x0010) */
	0x10, /* transport_scrambling_control(00), adaptation_field_control(01), continuity_counter(0) */

	0x00, /* PSI pointer to start of table */

	0x02, /* TID (2) */
	0xb0, 0x1a, /* section_syntax_indicator(1), section_length(26) */

	0x00, 0x01, /* program_number(1) */

	0xc1, /* version_number(0), current_next_indicator(1) */

	0x00, 0x00, /* section_number(0), last_section_number(0) */

	0xe1, 0x04, /* PCR_PID (0x0104) */

	0xf0, 0x00, /* program_info_length(0) */

	0x02, 0xe1, 0x00, 0xf0, 0x00, /* video stream type(2), pid */
	0x06, 0xe1, 0x03, 0xf0, 0x03, /* audio stream type(6), pid */
	0x6a, /* AC3 */
	0x01, /* Descriptor_length(1) */
	0x00, /* component_type_flag(0), bsid_flag(0), mainid_flag(0), asvc_flag(0), reserved flags(0) */

	0xED, 0xDE, 0x2D, 0xF3 /* CRC32 BE */
};

static const struct saa6752hs_mpeg_params param_defaults =
{
	.ts_pid_pmt      = 16,
	.ts_pid_video    = 260,
	.ts_pid_audio    = 256,
	.ts_pid_pcr      = 259,

	.vi_aspect       = V4L2_MPEG_VIDEO_ASPECT_4x3,
	.vi_bitrate      = 4000,
	.vi_bitrate_peak = 6000,
	.vi_bitrate_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,

	.au_encoding     = V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
	.au_l2_bitrate   = V4L2_MPEG_AUDIO_L2_BITRATE_256K,
	.au_ac3_bitrate  = V4L2_MPEG_AUDIO_AC3_BITRATE_256K,
};

/* ---------------------------------------------------------------------- */

static int saa6752hs_chip_command(struct i2c_client *client,
				  enum saa6752hs_command command)
{
	unsigned char buf[3];
	unsigned long timeout;
	int status = 0;

	/* execute the command */
	switch(command) {
	case SAA6752HS_COMMAND_RESET:
		buf[0] = 0x00;
		break;

	case SAA6752HS_COMMAND_STOP:
		buf[0] = 0x03;
		break;

	case SAA6752HS_COMMAND_START:
		buf[0] = 0x02;
		break;

	case SAA6752HS_COMMAND_PAUSE:
		buf[0] = 0x04;
		break;

	case SAA6752HS_COMMAND_RECONFIGURE:
		buf[0] = 0x05;
		break;

	case SAA6752HS_COMMAND_SLEEP:
		buf[0] = 0x06;
		break;

	case SAA6752HS_COMMAND_RECONFIGURE_FORCE:
		buf[0] = 0x07;
		break;

	default:
		return -EINVAL;
	}

	/* set it and wait for it to be so */
	i2c_master_send(client, buf, 1);
	timeout = jiffies + HZ * 3;
	for (;;) {
		/* get the current status */
		buf[0] = 0x10;
		i2c_master_send(client, buf, 1);
		i2c_master_recv(client, buf, 1);

		if (!(buf[0] & 0x20))
			break;
		if (time_after(jiffies,timeout)) {
			status = -ETIMEDOUT;
			break;
		}

		msleep(10);
	}

	/* delay a bit to let encoder settle */
	msleep(50);

	return status;
}


static inline void set_reg8(struct i2c_client *client, uint8_t reg, uint8_t val)
{
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;
	i2c_master_send(client, buf, 2);
}

static inline void set_reg16(struct i2c_client *client, uint8_t reg, uint16_t val)
{
	u8 buf[3];

	buf[0] = reg;
	buf[1] = val >> 8;
	buf[2] = val & 0xff;
	i2c_master_send(client, buf, 3);
}

static int saa6752hs_set_bitrate(struct i2c_client *client,
				 struct saa6752hs_state *h)
{
	struct saa6752hs_mpeg_params *params = &h->params;
	int tot_bitrate;
	int is_384k;

	/* set the bitrate mode */
	set_reg8(client, 0x71,
		params->vi_bitrate_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);

	/* set the video bitrate */
	if (params->vi_bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) {
		/* set the target bitrate */
		set_reg16(client, 0x80, params->vi_bitrate);

		/* set the max bitrate */
		set_reg16(client, 0x81, params->vi_bitrate_peak);
		tot_bitrate = params->vi_bitrate_peak;
	} else {
		/* set the target bitrate (no max bitrate for CBR) */
		set_reg16(client, 0x81, params->vi_bitrate);
		tot_bitrate = params->vi_bitrate;
	}

	/* set the audio encoding */
	set_reg8(client, 0x93,
			params->au_encoding == V4L2_MPEG_AUDIO_ENCODING_AC3);

	/* set the audio bitrate */
	if (params->au_encoding == V4L2_MPEG_AUDIO_ENCODING_AC3)
		is_384k = V4L2_MPEG_AUDIO_AC3_BITRATE_384K == params->au_ac3_bitrate;
	else
		is_384k = V4L2_MPEG_AUDIO_L2_BITRATE_384K == params->au_l2_bitrate;
	set_reg8(client, 0x94, is_384k);
	tot_bitrate += is_384k ? 384 : 256;

	/* Note: the total max bitrate is determined by adding the video and audio
	   bitrates together and also adding an extra 768kbit/s to stay on the
	   safe side. If more control should be required, then an extra MPEG control
	   should be added. */
	tot_bitrate += 768;
	if (tot_bitrate > MPEG_TOTAL_TARGET_BITRATE_MAX)
		tot_bitrate = MPEG_TOTAL_TARGET_BITRATE_MAX;

	/* set the total bitrate */
	set_reg16(client, 0xb1, tot_bitrate);
	return 0;
}

static int saa6752hs_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct saa6752hs_state *h =
		container_of(ctrl->handler, struct saa6752hs_state, hdl);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		/* peak bitrate shall be >= normal bitrate */
		if (ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
		    h->video_bitrate_peak->val < h->video_bitrate->val)
			h->video_bitrate_peak->val = h->video_bitrate->val;
		break;
	}
	return 0;
}

static int saa6752hs_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct saa6752hs_state *h =
		container_of(ctrl->handler, struct saa6752hs_state, hdl);
	struct saa6752hs_mpeg_params *params = &h->params;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_STREAM_TYPE:
		break;
	case V4L2_CID_MPEG_STREAM_PID_PMT:
		params->ts_pid_pmt = ctrl->val;
		break;
	case V4L2_CID_MPEG_STREAM_PID_AUDIO:
		params->ts_pid_audio = ctrl->val;
		break;
	case V4L2_CID_MPEG_STREAM_PID_VIDEO:
		params->ts_pid_video = ctrl->val;
		break;
	case V4L2_CID_MPEG_STREAM_PID_PCR:
		params->ts_pid_pcr = ctrl->val;
		break;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		params->au_encoding = ctrl->val;
		break;
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		params->au_l2_bitrate = ctrl->val;
		break;
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		params->au_ac3_bitrate = ctrl->val;
		break;
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		break;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		break;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		params->vi_aspect = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		params->vi_bitrate_mode = ctrl->val;
		params->vi_bitrate = h->video_bitrate->val / 1000;
		params->vi_bitrate_peak = h->video_bitrate_peak->val / 1000;
		v4l2_ctrl_activate(h->video_bitrate_peak,
				ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int saa6752hs_init(struct v4l2_subdev *sd, u32 leading_null_bytes)
{
	unsigned char buf[9], buf2[4];
	struct saa6752hs_state *h = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned size;
	u32 crc;
	unsigned char localPAT[256];
	unsigned char localPMT[256];

	/* Set video format - must be done first as it resets other settings */
	set_reg8(client, 0x41, h->video_format);

	/* Set number of lines in input signal */
	set_reg8(client, 0x40, (h->standard & V4L2_STD_525_60) ? 1 : 0);

	/* set bitrate */
	saa6752hs_set_bitrate(client, h);

	/* Set GOP structure {3, 13} */
	set_reg16(client, 0x72, 0x030d);

	/* Set minimum Q-scale {4} */
	set_reg8(client, 0x82, 0x04);

	/* Set maximum Q-scale {12} */
	set_reg8(client, 0x83, 0x0c);

	/* Set Output Protocol */
	set_reg8(client, 0xd0, 0x81);

	/* Set video output stream format {TS} */
	set_reg8(client, 0xb0, 0x05);

	/* Set leading null byte for TS */
	set_reg16(client, 0xf6, leading_null_bytes);

	/* compute PAT */
	memcpy(localPAT, PAT, sizeof(PAT));
	localPAT[17] = 0xe0 | ((h->params.ts_pid_pmt >> 8) & 0x0f);
	localPAT[18] = h->params.ts_pid_pmt & 0xff;
	crc = crc32_be(~0, &localPAT[7], sizeof(PAT) - 7 - 4);
	localPAT[sizeof(PAT) - 4] = (crc >> 24) & 0xFF;
	localPAT[sizeof(PAT) - 3] = (crc >> 16) & 0xFF;
	localPAT[sizeof(PAT) - 2] = (crc >> 8) & 0xFF;
	localPAT[sizeof(PAT) - 1] = crc & 0xFF;

	/* compute PMT */
	if (h->params.au_encoding == V4L2_MPEG_AUDIO_ENCODING_AC3) {
		size = sizeof(PMT_AC3);
		memcpy(localPMT, PMT_AC3, size);
	} else {
		size = sizeof(PMT);
		memcpy(localPMT, PMT, size);
	}
	localPMT[3] = 0x40 | ((h->params.ts_pid_pmt >> 8) & 0x0f);
	localPMT[4] = h->params.ts_pid_pmt & 0xff;
	localPMT[15] = 0xE0 | ((h->params.ts_pid_pcr >> 8) & 0x0F);
	localPMT[16] = h->params.ts_pid_pcr & 0xFF;
	localPMT[20] = 0xE0 | ((h->params.ts_pid_video >> 8) & 0x0F);
	localPMT[21] = h->params.ts_pid_video & 0xFF;
	localPMT[25] = 0xE0 | ((h->params.ts_pid_audio >> 8) & 0x0F);
	localPMT[26] = h->params.ts_pid_audio & 0xFF;
	crc = crc32_be(~0, &localPMT[7], size - 7 - 4);
	localPMT[size - 4] = (crc >> 24) & 0xFF;
	localPMT[size - 3] = (crc >> 16) & 0xFF;
	localPMT[size - 2] = (crc >> 8) & 0xFF;
	localPMT[size - 1] = crc & 0xFF;

	/* Set Audio PID */
	set_reg16(client, 0xc1, h->params.ts_pid_audio);

	/* Set Video PID */
	set_reg16(client, 0xc0, h->params.ts_pid_video);

	/* Set PCR PID */
	set_reg16(client, 0xc4, h->params.ts_pid_pcr);

	/* Send SI tables */
	i2c_master_send(client, localPAT, sizeof(PAT));
	i2c_master_send(client, localPMT, size);

	/* mute then unmute audio. This removes buzzing artefacts */
	set_reg8(client, 0xa4, 1);
	set_reg8(client, 0xa4, 0);

	/* start it going */
	saa6752hs_chip_command(client, SAA6752HS_COMMAND_START);

	/* readout current state */
	buf[0] = 0xE1;
	buf[1] = 0xA7;
	buf[2] = 0xFE;
	buf[3] = 0x82;
	buf[4] = 0xB0;
	i2c_master_send(client, buf, 5);
	i2c_master_recv(client, buf2, 4);

	/* change aspect ratio */
	buf[0] = 0xE0;
	buf[1] = 0xA7;
	buf[2] = 0xFE;
	buf[3] = 0x82;
	buf[4] = 0xB0;
	buf[5] = buf2[0];
	switch (h->params.vi_aspect) {
	case V4L2_MPEG_VIDEO_ASPECT_16x9:
		buf[6] = buf2[1] | 0x40;
		break;
	case V4L2_MPEG_VIDEO_ASPECT_4x3:
	default:
		buf[6] = buf2[1] & 0xBF;
		break;
	}
	buf[7] = buf2[2];
	buf[8] = buf2[3];
	i2c_master_send(client, buf, 9);

	return 0;
}

static int saa6752hs_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *f = &format->format;
	struct saa6752hs_state *h = to_state(sd);

	if (format->pad)
		return -EINVAL;

	if (h->video_format == SAA6752HS_VF_UNKNOWN)
		h->video_format = SAA6752HS_VF_D1;
	f->width = v4l2_format_table[h->video_format].fmt.pix.width;
	f->height = v4l2_format_table[h->video_format].fmt.pix.height;
	f->code = MEDIA_BUS_FMT_FIXED;
	f->field = V4L2_FIELD_INTERLACED;
	f->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int saa6752hs_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *f = &format->format;
	struct saa6752hs_state *h = to_state(sd);
	int dist_352, dist_480, dist_720;

	if (format->pad)
		return -EINVAL;

	f->code = MEDIA_BUS_FMT_FIXED;

	dist_352 = abs(f->width - 352);
	dist_480 = abs(f->width - 480);
	dist_720 = abs(f->width - 720);
	if (dist_720 < dist_480) {
		f->width = 720;
		f->height = 576;
	} else if (dist_480 < dist_352) {
		f->width = 480;
		f->height = 576;
	} else {
		f->width = 352;
		if (abs(f->height - 576) < abs(f->height - 288))
			f->height = 576;
		else
			f->height = 288;
	}
	f->field = V4L2_FIELD_INTERLACED;
	f->colorspace = V4L2_COLORSPACE_SMPTE170M;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		sd_state->pads->try_fmt = *f;
		return 0;
	}

	/*
	  FIXME: translate and round width/height into EMPRESS
	  subsample type:

	  type   |   PAL   |  NTSC
	  ---------------------------
	  SIF    | 352x288 | 352x240
	  1/2 D1 | 352x576 | 352x480
	  2/3 D1 | 480x576 | 480x480
	  D1     | 720x576 | 720x480
	*/

	if (f->code != MEDIA_BUS_FMT_FIXED)
		return -EINVAL;

	if (f->width == 720)
		h->video_format = SAA6752HS_VF_D1;
	else if (f->width == 480)
		h->video_format = SAA6752HS_VF_2_3_D1;
	else if (f->height == 576)
		h->video_format = SAA6752HS_VF_1_2_D1;
	else
		h->video_format = SAA6752HS_VF_SIF;
	return 0;
}

static int saa6752hs_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa6752hs_state *h = to_state(sd);

	h->standard = std;
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops saa6752hs_ctrl_ops = {
	.try_ctrl = saa6752hs_try_ctrl,
	.s_ctrl = saa6752hs_s_ctrl,
};

static const struct v4l2_subdev_core_ops saa6752hs_core_ops = {
	.init = saa6752hs_init,
};

static const struct v4l2_subdev_video_ops saa6752hs_video_ops = {
	.s_std = saa6752hs_s_std,
};

static const struct v4l2_subdev_pad_ops saa6752hs_pad_ops = {
	.get_fmt = saa6752hs_get_fmt,
	.set_fmt = saa6752hs_set_fmt,
};

static const struct v4l2_subdev_ops saa6752hs_ops = {
	.core = &saa6752hs_core_ops,
	.video = &saa6752hs_video_ops,
	.pad = &saa6752hs_pad_ops,
};

static int saa6752hs_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct saa6752hs_state *h;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	u8 addr = 0x13;
	u8 data[12];

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	h = devm_kzalloc(&client->dev, sizeof(*h), GFP_KERNEL);
	if (h == NULL)
		return -ENOMEM;
	sd = &h->sd;
	v4l2_i2c_subdev_init(sd, client, &saa6752hs_ops);

	i2c_master_send(client, &addr, 1);
	i2c_master_recv(client, data, sizeof(data));
	h->revision = (data[8] << 8) | data[9];
	h->has_ac3 = 0;
	if (h->revision == 0x0206) {
		h->has_ac3 = 1;
		v4l_info(client, "supports AC-3\n");
	}
	h->params = param_defaults;

	hdl = &h->hdl;
	v4l2_ctrl_handler_init(hdl, 14);
	v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_AUDIO_ENCODING,
		h->has_ac3 ? V4L2_MPEG_AUDIO_ENCODING_AC3 :
			V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
		0x0d, V4L2_MPEG_AUDIO_ENCODING_LAYER_2);

	v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_AUDIO_L2_BITRATE,
		V4L2_MPEG_AUDIO_L2_BITRATE_384K,
		~((1 << V4L2_MPEG_AUDIO_L2_BITRATE_256K) |
		  (1 << V4L2_MPEG_AUDIO_L2_BITRATE_384K)),
		V4L2_MPEG_AUDIO_L2_BITRATE_256K);

	if (h->has_ac3)
		v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
			V4L2_CID_MPEG_AUDIO_AC3_BITRATE,
			V4L2_MPEG_AUDIO_AC3_BITRATE_384K,
			~((1 << V4L2_MPEG_AUDIO_AC3_BITRATE_256K) |
			  (1 << V4L2_MPEG_AUDIO_AC3_BITRATE_384K)),
			V4L2_MPEG_AUDIO_AC3_BITRATE_256K);

	v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ,
		V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000,
		~(1 << V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000),
		V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000);

	v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_ENCODING,
		V4L2_MPEG_VIDEO_ENCODING_MPEG_2,
		~(1 << V4L2_MPEG_VIDEO_ENCODING_MPEG_2),
		V4L2_MPEG_VIDEO_ENCODING_MPEG_2);

	v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_ASPECT,
		V4L2_MPEG_VIDEO_ASPECT_16x9, 0x01,
		V4L2_MPEG_VIDEO_ASPECT_4x3);

	h->video_bitrate_peak = v4l2_ctrl_new_std(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
		1000000, 27000000, 1000, 8000000);

	v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_STREAM_TYPE,
		V4L2_MPEG_STREAM_TYPE_MPEG2_TS,
		~(1 << V4L2_MPEG_STREAM_TYPE_MPEG2_TS),
		V4L2_MPEG_STREAM_TYPE_MPEG2_TS);

	h->video_bitrate_mode = v4l2_ctrl_new_std_menu(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
		V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
	h->video_bitrate = v4l2_ctrl_new_std(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE, 1000000, 27000000, 1000, 6000000);
	v4l2_ctrl_new_std(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_STREAM_PID_PMT, 0, (1 << 14) - 1, 1, 16);
	v4l2_ctrl_new_std(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_STREAM_PID_AUDIO, 0, (1 << 14) - 1, 1, 260);
	v4l2_ctrl_new_std(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_STREAM_PID_VIDEO, 0, (1 << 14) - 1, 1, 256);
	v4l2_ctrl_new_std(hdl, &saa6752hs_ctrl_ops,
		V4L2_CID_MPEG_STREAM_PID_PCR, 0, (1 << 14) - 1, 1, 259);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}
	v4l2_ctrl_cluster(3, &h->video_bitrate_mode);
	v4l2_ctrl_handler_setup(hdl);
	h->standard = 0; /* Assume 625 input lines */
	return 0;
}

static int saa6752hs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&to_state(sd)->hdl);
	return 0;
}

static const struct i2c_device_id saa6752hs_id[] = {
	{ "saa6752hs", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa6752hs_id);

static struct i2c_driver saa6752hs_driver = {
	.driver = {
		.name	= "saa6752hs",
	},
	.probe		= saa6752hs_probe,
	.remove		= saa6752hs_remove,
	.id_table	= saa6752hs_id,
};

module_i2c_driver(saa6752hs_driver);
