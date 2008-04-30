/*
 *  cx18 ioctl system call
 *
 *  Derived from ivtv-ioctl.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-version.h"
#include "cx18-mailbox.h"
#include "cx18-i2c.h"
#include "cx18-queue.h"
#include "cx18-fileops.h"
#include "cx18-vbi.h"
#include "cx18-audio.h"
#include "cx18-video.h"
#include "cx18-streams.h"
#include "cx18-ioctl.h"
#include "cx18-gpio.h"
#include "cx18-controls.h"
#include "cx18-cards.h"
#include "cx18-av-core.h"
#include <media/tveeprom.h>
#include <media/v4l2-chip-ident.h>
#include <linux/i2c-id.h>

u16 cx18_service2vbi(int type)
{
	switch (type) {
	case V4L2_SLICED_TELETEXT_B:
		return CX18_SLICED_TYPE_TELETEXT_B;
	case V4L2_SLICED_CAPTION_525:
		return CX18_SLICED_TYPE_CAPTION_525;
	case V4L2_SLICED_WSS_625:
		return CX18_SLICED_TYPE_WSS_625;
	case V4L2_SLICED_VPS:
		return CX18_SLICED_TYPE_VPS;
	default:
		return 0;
	}
}

static int valid_service_line(int field, int line, int is_pal)
{
	return (is_pal && line >= 6 && (line != 23 || field == 0)) ||
	       (!is_pal && line >= 10 && line < 22);
}

static u16 select_service_from_set(int field, int line, u16 set, int is_pal)
{
	u16 valid_set = (is_pal ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525);
	int i;

	set = set & valid_set;
	if (set == 0 || !valid_service_line(field, line, is_pal))
		return 0;
	if (!is_pal) {
		if (line == 21 && (set & V4L2_SLICED_CAPTION_525))
			return V4L2_SLICED_CAPTION_525;
	} else {
		if (line == 16 && field == 0 && (set & V4L2_SLICED_VPS))
			return V4L2_SLICED_VPS;
		if (line == 23 && field == 0 && (set & V4L2_SLICED_WSS_625))
			return V4L2_SLICED_WSS_625;
		if (line == 23)
			return 0;
	}
	for (i = 0; i < 32; i++) {
		if ((1 << i) & set)
			return 1 << i;
	}
	return 0;
}

void cx18_expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
{
	u16 set = fmt->service_set;
	int f, l;

	fmt->service_set = 0;
	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++)
			fmt->service_lines[f][l] = select_service_from_set(f, l, set, is_pal);
	}
}

static int check_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
{
	int f, l;
	u16 set = 0;

	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++) {
			fmt->service_lines[f][l] = select_service_from_set(f, l, fmt->service_lines[f][l], is_pal);
			set |= fmt->service_lines[f][l];
		}
	}
	return set != 0;
}

u16 cx18_get_service_set(struct v4l2_sliced_vbi_format *fmt)
{
	int f, l;
	u16 set = 0;

	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++)
			set |= fmt->service_lines[f][l];
	}
	return set;
}

static const struct {
	v4l2_std_id  std;
	char        *name;
} enum_stds[] = {
	{ V4L2_STD_PAL_BG | V4L2_STD_PAL_H, "PAL-BGH" },
	{ V4L2_STD_PAL_DK,    "PAL-DK"    },
	{ V4L2_STD_PAL_I,     "PAL-I"     },
	{ V4L2_STD_PAL_M,     "PAL-M"     },
	{ V4L2_STD_PAL_N,     "PAL-N"     },
	{ V4L2_STD_PAL_Nc,    "PAL-Nc"    },
	{ V4L2_STD_SECAM_B | V4L2_STD_SECAM_G | V4L2_STD_SECAM_H, "SECAM-BGH" },
	{ V4L2_STD_SECAM_DK,  "SECAM-DK"  },
	{ V4L2_STD_SECAM_L,   "SECAM-L"   },
	{ V4L2_STD_SECAM_LC,  "SECAM-L'"  },
	{ V4L2_STD_NTSC_M,    "NTSC-M"    },
	{ V4L2_STD_NTSC_M_JP, "NTSC-J"    },
	{ V4L2_STD_NTSC_M_KR, "NTSC-K"    },
};

static const struct v4l2_standard cx18_std_60hz = {
	.frameperiod = {.numerator = 1001, .denominator = 30000},
	.framelines = 525,
};

static const struct v4l2_standard cx18_std_50hz = {
	.frameperiod = { .numerator = 1, .denominator = 25 },
	.framelines = 625,
};

static int cx18_cxc(struct cx18 *cx, unsigned int cmd, void *arg)
{
	struct v4l2_register *regs = arg;
	unsigned long flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (regs->reg >= CX18_MEM_OFFSET + CX18_MEM_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&cx18_cards_lock, flags);
	if (cmd == VIDIOC_DBG_G_REGISTER)
		regs->val = read_enc(regs->reg);
	else
		write_enc(regs->val, regs->reg);
	spin_unlock_irqrestore(&cx18_cards_lock, flags);
	return 0;
}

static int cx18_get_fmt(struct cx18 *cx, int streamtype, struct v4l2_format *fmt)
{
	switch (fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		fmt->fmt.pix.width = cx->params.width;
		fmt->fmt.pix.height = cx->params.height;
		fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
		if (streamtype == CX18_ENC_STREAM_TYPE_YUV) {
			fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_HM12;
			/* YUV size is (Y=(h*w) + UV=(h*(w/2))) */
			fmt->fmt.pix.sizeimage =
				fmt->fmt.pix.height * fmt->fmt.pix.width +
				fmt->fmt.pix.height * (fmt->fmt.pix.width / 2);
		} else {
			fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG;
			fmt->fmt.pix.sizeimage = 128 * 1024;
		}
		break;

	case V4L2_BUF_TYPE_VBI_CAPTURE:
		fmt->fmt.vbi.sampling_rate = 27000000;
		fmt->fmt.vbi.offset = 248;
		fmt->fmt.vbi.samples_per_line = cx->vbi.raw_decoder_line_size - 4;
		fmt->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
		fmt->fmt.vbi.start[0] = cx->vbi.start[0];
		fmt->fmt.vbi.start[1] = cx->vbi.start[1];
		fmt->fmt.vbi.count[0] = fmt->fmt.vbi.count[1] = cx->vbi.count;
		break;

	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	{
		struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

		vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
		memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));
		memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));

		cx18_av_cmd(cx, VIDIOC_G_FMT, fmt);
		vbifmt->service_set = cx18_get_service_set(vbifmt);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx18_try_or_set_fmt(struct cx18 *cx, int streamtype,
		struct v4l2_format *fmt, int set_fmt)
{
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;
	u16 set;

	/* set window size */
	if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		int w = fmt->fmt.pix.width;
		int h = fmt->fmt.pix.height;

		if (w > 720)
			w = 720;
		else if (w < 1)
			w = 1;
		if (h > (cx->is_50hz ? 576 : 480))
			h = (cx->is_50hz ? 576 : 480);
		else if (h < 2)
			h = 2;
		cx18_get_fmt(cx, streamtype, fmt);
		fmt->fmt.pix.width = w;
		fmt->fmt.pix.height = h;

		if (!set_fmt || (cx->params.width == w && cx->params.height == h))
			return 0;
		if (atomic_read(&cx->capturing) > 0)
			return -EBUSY;

		cx->params.width = w;
		cx->params.height = h;
		if (w != 720 || h != (cx->is_50hz ? 576 : 480))
			cx->params.video_temporal_filter = 0;
		else
			cx->params.video_temporal_filter = 8;
		cx18_av_cmd(cx, VIDIOC_S_FMT, fmt);
		return cx18_get_fmt(cx, streamtype, fmt);
	}

	/* set raw VBI format */
	if (fmt->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		if (set_fmt && streamtype == CX18_ENC_STREAM_TYPE_VBI &&
		    cx->vbi.sliced_in->service_set &&
		    atomic_read(&cx->capturing) > 0)
			return -EBUSY;
		if (set_fmt) {
			cx->vbi.sliced_in->service_set = 0;
			cx18_av_cmd(cx, VIDIOC_S_FMT, &cx->vbi.in);
		}
		return cx18_get_fmt(cx, streamtype, fmt);
	}

	/* any else but sliced VBI capture is an error */
	if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
		return -EINVAL;

	/* TODO: implement sliced VBI, for now silently return 0 */
	return 0;

	/* set sliced VBI capture format */
	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));

	if (vbifmt->service_set)
		cx18_expand_service_set(vbifmt, cx->is_50hz);
	set = check_service_set(vbifmt, cx->is_50hz);
	vbifmt->service_set = cx18_get_service_set(vbifmt);

	if (!set_fmt)
		return 0;
	if (set == 0)
		return -EINVAL;
	if (atomic_read(&cx->capturing) > 0 && cx->vbi.sliced_in->service_set == 0)
		return -EBUSY;
	cx18_av_cmd(cx, VIDIOC_S_FMT, fmt);
	memcpy(cx->vbi.sliced_in, vbifmt, sizeof(*cx->vbi.sliced_in));
	return 0;
}

static int cx18_debug_ioctls(struct file *filp, unsigned int cmd, void *arg)
{
	struct cx18_open_id *id = (struct cx18_open_id *)filp->private_data;
	struct cx18 *cx = id->cx;
	struct v4l2_register *reg = arg;

	switch (cmd) {
	/* ioctls to allow direct access to the encoder registers for testing */
	case VIDIOC_DBG_G_REGISTER:
		if (v4l2_chip_match_host(reg->match_type, reg->match_chip))
			return cx18_cxc(cx, cmd, arg);
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_DRIVER)
			return cx18_i2c_id(cx, reg->match_chip, cmd, arg);
		return cx18_call_i2c_client(cx, reg->match_chip, cmd, arg);

	case VIDIOC_DBG_S_REGISTER:
		if (v4l2_chip_match_host(reg->match_type, reg->match_chip))
			return cx18_cxc(cx, cmd, arg);
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_DRIVER)
			return cx18_i2c_id(cx, reg->match_chip, cmd, arg);
		return cx18_call_i2c_client(cx, reg->match_chip, cmd, arg);

	case VIDIOC_G_CHIP_IDENT: {
		struct v4l2_chip_ident *chip = arg;

		chip->ident = V4L2_IDENT_NONE;
		chip->revision = 0;
		if (reg->match_type == V4L2_CHIP_MATCH_HOST) {
			if (v4l2_chip_match_host(reg->match_type, reg->match_chip)) {
				struct v4l2_chip_ident *chip = arg;

				chip->ident = V4L2_IDENT_CX23418;
			}
			return 0;
		}
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_DRIVER)
			return cx18_i2c_id(cx, reg->match_chip, cmd, arg);
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_ADDR)
			return cx18_call_i2c_client(cx, reg->match_chip, cmd, arg);
		return -EINVAL;
	}

	case VIDIOC_INT_S_AUDIO_ROUTING: {
		struct v4l2_routing *route = arg;

		cx18_audio_set_route(cx, route);
		break;
	}

	default:
		return -EINVAL;
	}
	return 0;
}

int cx18_v4l2_ioctls(struct cx18 *cx, struct file *filp, unsigned cmd, void *arg)
{
	struct cx18_open_id *id = NULL;

	if (filp)
		id = (struct cx18_open_id *)filp->private_data;

	switch (cmd) {
	case VIDIOC_G_PRIORITY:
	{
		enum v4l2_priority *p = arg;

		*p = v4l2_prio_max(&cx->prio);
		break;
	}

	case VIDIOC_S_PRIORITY:
	{
		enum v4l2_priority *prio = arg;

		return v4l2_prio_change(&cx->prio, &id->prio, *prio);
	}

	case VIDIOC_QUERYCAP:{
		struct v4l2_capability *vcap = arg;

		memset(vcap, 0, sizeof(*vcap));
		strlcpy(vcap->driver, CX18_DRIVER_NAME, sizeof(vcap->driver));
		strlcpy(vcap->card, cx->card_name, sizeof(vcap->card));
		strlcpy(vcap->bus_info, pci_name(cx->dev), sizeof(vcap->bus_info));
		vcap->version = CX18_DRIVER_VERSION; 	    /* version */
		vcap->capabilities = cx->v4l2_cap; 	    /* capabilities */

		/* reserved.. must set to 0! */
		vcap->reserved[0] = vcap->reserved[1] =
			vcap->reserved[2] = vcap->reserved[3] = 0;
		break;
	}

	case VIDIOC_ENUMAUDIO:{
		struct v4l2_audio *vin = arg;

		return cx18_get_audio_input(cx, vin->index, vin);
	}

	case VIDIOC_G_AUDIO:{
		struct v4l2_audio *vin = arg;

		vin->index = cx->audio_input;
		return cx18_get_audio_input(cx, vin->index, vin);
	}

	case VIDIOC_S_AUDIO:{
		struct v4l2_audio *vout = arg;

		if (vout->index >= cx->nof_audio_inputs)
			return -EINVAL;
		cx->audio_input = vout->index;
		cx18_audio_set_io(cx);
		break;
	}

	case VIDIOC_ENUMINPUT:{
		struct v4l2_input *vin = arg;

		/* set it to defaults from our table */
		return cx18_get_input(cx, vin->index, vin);
	}

	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT: {
		struct v4l2_format *fmt = arg;

		return cx18_try_or_set_fmt(cx, id->type, fmt, cmd == VIDIOC_S_FMT);
	}

	case VIDIOC_G_FMT: {
		struct v4l2_format *fmt = arg;
		int type = fmt->type;

		memset(fmt, 0, sizeof(*fmt));
		fmt->type = type;
		return cx18_get_fmt(cx, id->type, fmt);
	}

	case VIDIOC_CROPCAP: {
		struct v4l2_cropcap *cropcap = arg;

		if (cropcap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		cropcap->bounds.top = cropcap->bounds.left = 0;
		cropcap->bounds.width = 720;
		cropcap->bounds.height = cx->is_50hz ? 576 : 480;
		cropcap->pixelaspect.numerator = cx->is_50hz ? 59 : 10;
		cropcap->pixelaspect.denominator = cx->is_50hz ? 54 : 11;
		cropcap->defrect = cropcap->bounds;
		return 0;
	}

	case VIDIOC_S_CROP: {
		struct v4l2_crop *crop = arg;

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		return cx18_av_cmd(cx, VIDIOC_S_CROP, arg);
	}

	case VIDIOC_G_CROP: {
		struct v4l2_crop *crop = arg;

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		return cx18_av_cmd(cx, VIDIOC_G_CROP, arg);
	}

	case VIDIOC_ENUM_FMT: {
		static struct v4l2_fmtdesc formats[] = {
			{ 0, 0, 0,
			  "HM12 (YUV 4:1:1)", V4L2_PIX_FMT_HM12,
			  { 0, 0, 0, 0 }
			},
			{ 1, 0, V4L2_FMT_FLAG_COMPRESSED,
			  "MPEG", V4L2_PIX_FMT_MPEG,
			  { 0, 0, 0, 0 }
			}
		};
		struct v4l2_fmtdesc *fmt = arg;
		enum v4l2_buf_type type = fmt->type;

		switch (type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			break;
		default:
			return -EINVAL;
		}
		if (fmt->index > 1)
			return -EINVAL;
		*fmt = formats[fmt->index];
		fmt->type = type;
		return 0;
	}

	case VIDIOC_G_INPUT:{
		*(int *)arg = cx->active_input;
		break;
	}

	case VIDIOC_S_INPUT:{
		int inp = *(int *)arg;

		if (inp < 0 || inp >= cx->nof_inputs)
			return -EINVAL;

		if (inp == cx->active_input) {
			CX18_DEBUG_INFO("Input unchanged\n");
			break;
		}
		CX18_DEBUG_INFO("Changing input from %d to %d\n",
				cx->active_input, inp);

		cx->active_input = inp;
		/* Set the audio input to whatever is appropriate for the
		   input type. */
		cx->audio_input = cx->card->video_inputs[inp].audio_index;

		/* prevent others from messing with the streams until
		   we're finished changing inputs. */
		cx18_mute(cx);
		cx18_video_set_io(cx);
		cx18_audio_set_io(cx);
		cx18_unmute(cx);
		break;
	}

	case VIDIOC_G_FREQUENCY:{
		struct v4l2_frequency *vf = arg;

		if (vf->tuner != 0)
			return -EINVAL;
		cx18_call_i2c_clients(cx, cmd, arg);
		break;
	}

	case VIDIOC_S_FREQUENCY:{
		struct v4l2_frequency vf = *(struct v4l2_frequency *)arg;

		if (vf.tuner != 0)
			return -EINVAL;

		cx18_mute(cx);
		CX18_DEBUG_INFO("v4l2 ioctl: set frequency %d\n", vf.frequency);
		cx18_call_i2c_clients(cx, cmd, &vf);
		cx18_unmute(cx);
		break;
	}

	case VIDIOC_ENUMSTD:{
		struct v4l2_standard *vs = arg;
		int idx = vs->index;

		if (idx < 0 || idx >= ARRAY_SIZE(enum_stds))
			return -EINVAL;

		*vs = (enum_stds[idx].std & V4L2_STD_525_60) ?
				cx18_std_60hz : cx18_std_50hz;
		vs->index = idx;
		vs->id = enum_stds[idx].std;
		strlcpy(vs->name, enum_stds[idx].name, sizeof(vs->name));
		break;
	}

	case VIDIOC_G_STD:{
		*(v4l2_std_id *) arg = cx->std;
		break;
	}

	case VIDIOC_S_STD: {
		v4l2_std_id std = *(v4l2_std_id *) arg;

		if ((std & V4L2_STD_ALL) == 0)
			return -EINVAL;

		if (std == cx->std)
			break;

		if (test_bit(CX18_F_I_RADIO_USER, &cx->i_flags) ||
		    atomic_read(&cx->capturing) > 0) {
			/* Switching standard would turn off the radio or mess
			   with already running streams, prevent that by
			   returning EBUSY. */
			return -EBUSY;
		}

		cx->std = std;
		cx->is_60hz = (std & V4L2_STD_525_60) ? 1 : 0;
		cx->params.is_50hz = cx->is_50hz = !cx->is_60hz;
		cx->params.width = 720;
		cx->params.height = cx->is_50hz ? 576 : 480;
		cx->vbi.count = cx->is_50hz ? 18 : 12;
		cx->vbi.start[0] = cx->is_50hz ? 6 : 10;
		cx->vbi.start[1] = cx->is_50hz ? 318 : 273;
		cx->vbi.sliced_decoder_line_size = cx->is_60hz ? 272 : 284;
		CX18_DEBUG_INFO("Switching standard to %llx.\n", (unsigned long long)cx->std);

		/* Tuner */
		cx18_call_i2c_clients(cx, VIDIOC_S_STD, &cx->std);
		break;
	}

	case VIDIOC_S_TUNER: {	/* Setting tuner can only set audio mode */
		struct v4l2_tuner *vt = arg;

		if (vt->index != 0)
			return -EINVAL;

		cx18_call_i2c_clients(cx, VIDIOC_S_TUNER, vt);
		break;
	}

	case VIDIOC_G_TUNER: {
		struct v4l2_tuner *vt = arg;

		if (vt->index != 0)
			return -EINVAL;

		memset(vt, 0, sizeof(*vt));
		cx18_call_i2c_clients(cx, VIDIOC_G_TUNER, vt);

		if (test_bit(CX18_F_I_RADIO_USER, &cx->i_flags)) {
			strlcpy(vt->name, "cx18 Radio Tuner", sizeof(vt->name));
			vt->type = V4L2_TUNER_RADIO;
		} else {
			strlcpy(vt->name, "cx18 TV Tuner", sizeof(vt->name));
			vt->type = V4L2_TUNER_ANALOG_TV;
		}
		break;
	}

	case VIDIOC_G_SLICED_VBI_CAP: {
		struct v4l2_sliced_vbi_cap *cap = arg;
		int set = cx->is_50hz ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525;
		int f, l;
		enum v4l2_buf_type type = cap->type;

		memset(cap, 0, sizeof(*cap));
		cap->type = type;
		if (type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) {
			for (f = 0; f < 2; f++) {
				for (l = 0; l < 24; l++) {
					if (valid_service_line(f, l, cx->is_50hz))
						cap->service_lines[f][l] = set;
				}
			}
			return 0;
		}
		return -EINVAL;
	}

	case VIDIOC_ENCODER_CMD:
	case VIDIOC_TRY_ENCODER_CMD: {
		struct v4l2_encoder_cmd *enc = arg;
		int try = cmd == VIDIOC_TRY_ENCODER_CMD;

		memset(&enc->raw, 0, sizeof(enc->raw));
		switch (enc->cmd) {
		case V4L2_ENC_CMD_START:
			enc->flags = 0;
			if (try)
				return 0;
			return cx18_start_capture(id);

		case V4L2_ENC_CMD_STOP:
			enc->flags &= V4L2_ENC_CMD_STOP_AT_GOP_END;
			if (try)
				return 0;
			cx18_stop_capture(id, enc->flags & V4L2_ENC_CMD_STOP_AT_GOP_END);
			return 0;

		case V4L2_ENC_CMD_PAUSE:
			enc->flags = 0;
			if (try)
				return 0;
			if (!atomic_read(&cx->capturing))
				return -EPERM;
			if (test_and_set_bit(CX18_F_I_ENC_PAUSED, &cx->i_flags))
				return 0;
			cx18_mute(cx);
			cx18_vapi(cx, CX18_CPU_CAPTURE_PAUSE, 1, cx18_find_handle(cx));
			break;

		case V4L2_ENC_CMD_RESUME:
			enc->flags = 0;
			if (try)
				return 0;
			if (!atomic_read(&cx->capturing))
				return -EPERM;
			if (!test_and_clear_bit(CX18_F_I_ENC_PAUSED, &cx->i_flags))
				return 0;
			cx18_vapi(cx, CX18_CPU_CAPTURE_RESUME, 1, cx18_find_handle(cx));
			cx18_unmute(cx);
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	case VIDIOC_LOG_STATUS:
	{
		struct v4l2_input vidin;
		struct v4l2_audio audin;
		int i;

		CX18_INFO("=================  START STATUS CARD #%d  =================\n", cx->num);
		if (cx->hw_flags & CX18_HW_TVEEPROM) {
			struct tveeprom tv;

			cx18_read_eeprom(cx, &tv);
		}
		cx18_call_i2c_clients(cx, VIDIOC_LOG_STATUS, NULL);
		cx18_get_input(cx, cx->active_input, &vidin);
		cx18_get_audio_input(cx, cx->audio_input, &audin);
		CX18_INFO("Video Input: %s\n", vidin.name);
		CX18_INFO("Audio Input: %s\n", audin.name);
		CX18_INFO("Tuner: %s\n",
			test_bit(CX18_F_I_RADIO_USER, &cx->i_flags) ?
			"Radio" : "TV");
		cx2341x_log_status(&cx->params, cx->name);
		CX18_INFO("Status flags: 0x%08lx\n", cx->i_flags);
		for (i = 0; i < CX18_MAX_STREAMS; i++) {
			struct cx18_stream *s = &cx->streams[i];

			if (s->v4l2dev == NULL || s->buffers == 0)
				continue;
			CX18_INFO("Stream %s: status 0x%04lx, %d%% of %d KiB (%d buffers) in use\n",
				s->name, s->s_flags,
				(s->buffers - s->q_free.buffers) * 100 / s->buffers,
				(s->buffers * s->buf_size) / 1024, s->buffers);
		}
		CX18_INFO("Read MPEG/VBI: %lld/%lld bytes\n",
				(long long)cx->mpg_data_received,
				(long long)cx->vbi_data_inserted);
		CX18_INFO("==================  END STATUS CARD #%d  ==================\n", cx->num);
		break;
	}

	default:
		return -EINVAL;
	}
	return 0;
}

static int cx18_v4l2_do_ioctl(struct inode *inode, struct file *filp,
			      unsigned int cmd, void *arg)
{
	struct cx18_open_id *id = (struct cx18_open_id *)filp->private_data;
	struct cx18 *cx = id->cx;
	int ret;

	/* check priority */
	switch (cmd) {
	case VIDIOC_S_CTRL:
	case VIDIOC_S_STD:
	case VIDIOC_S_INPUT:
	case VIDIOC_S_TUNER:
	case VIDIOC_S_FREQUENCY:
	case VIDIOC_S_FMT:
	case VIDIOC_S_CROP:
	case VIDIOC_S_EXT_CTRLS:
		ret = v4l2_prio_check(&cx->prio, &id->prio);
		if (ret)
			return ret;
	}

	switch (cmd) {
	case VIDIOC_DBG_G_REGISTER:
	case VIDIOC_DBG_S_REGISTER:
	case VIDIOC_G_CHIP_IDENT:
	case VIDIOC_INT_S_AUDIO_ROUTING:
	case VIDIOC_INT_RESET:
		if (cx18_debug & CX18_DBGFLG_IOCTL) {
			printk(KERN_INFO "cx18%d ioctl: ", cx->num);
			v4l_printk_ioctl(cmd);
		}
		return cx18_debug_ioctls(filp, cmd, arg);

	case VIDIOC_G_PRIORITY:
	case VIDIOC_S_PRIORITY:
	case VIDIOC_QUERYCAP:
	case VIDIOC_ENUMINPUT:
	case VIDIOC_G_INPUT:
	case VIDIOC_S_INPUT:
	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
	case VIDIOC_ENUM_FMT:
	case VIDIOC_CROPCAP:
	case VIDIOC_G_CROP:
	case VIDIOC_S_CROP:
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
	case VIDIOC_ENUMSTD:
	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_S_TUNER:
	case VIDIOC_G_TUNER:
	case VIDIOC_ENUMAUDIO:
	case VIDIOC_S_AUDIO:
	case VIDIOC_G_AUDIO:
	case VIDIOC_G_SLICED_VBI_CAP:
	case VIDIOC_LOG_STATUS:
	case VIDIOC_G_ENC_INDEX:
	case VIDIOC_ENCODER_CMD:
	case VIDIOC_TRY_ENCODER_CMD:
		if (cx18_debug & CX18_DBGFLG_IOCTL) {
			printk(KERN_INFO "cx18%d ioctl: ", cx->num);
			v4l_printk_ioctl(cmd);
		}
		return cx18_v4l2_ioctls(cx, filp, cmd, arg);

	case VIDIOC_QUERYMENU:
	case VIDIOC_QUERYCTRL:
	case VIDIOC_S_CTRL:
	case VIDIOC_G_CTRL:
	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_G_EXT_CTRLS:
	case VIDIOC_TRY_EXT_CTRLS:
		if (cx18_debug & CX18_DBGFLG_IOCTL) {
			printk(KERN_INFO "cx18%d ioctl: ", cx->num);
			v4l_printk_ioctl(cmd);
		}
		return cx18_control_ioctls(cx, cmd, arg);

	case 0x00005401:	/* Handle isatty() calls */
		return -EINVAL;
	default:
		return v4l_compat_translate_ioctl(inode, filp, cmd, arg,
						   cx18_v4l2_do_ioctl);
	}
	return 0;
}

int cx18_v4l2_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		    unsigned long arg)
{
	struct cx18_open_id *id = (struct cx18_open_id *)filp->private_data;
	struct cx18 *cx = id->cx;
	int res;

	mutex_lock(&cx->serialize_lock);
	res = video_usercopy(inode, filp, cmd, arg, cx18_v4l2_do_ioctl);
	mutex_unlock(&cx->serialize_lock);
	return res;
}
