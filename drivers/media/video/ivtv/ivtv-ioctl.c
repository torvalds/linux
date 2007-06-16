/*
    ioctl system call
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

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

#include "ivtv-driver.h"
#include "ivtv-version.h"
#include "ivtv-mailbox.h"
#include "ivtv-i2c.h"
#include "ivtv-queue.h"
#include "ivtv-fileops.h"
#include "ivtv-vbi.h"
#include "ivtv-audio.h"
#include "ivtv-video.h"
#include "ivtv-streams.h"
#include "ivtv-yuv.h"
#include "ivtv-ioctl.h"
#include "ivtv-gpio.h"
#include "ivtv-controls.h"
#include "ivtv-cards.h"
#include <media/saa7127.h>
#include <media/tveeprom.h>
#include <media/v4l2-chip-ident.h>
#include <linux/dvb/audio.h>
#include <linux/i2c-id.h>

u16 service2vbi(int type)
{
	switch (type) {
		case V4L2_SLICED_TELETEXT_B:
			return IVTV_SLICED_TYPE_TELETEXT_B;
		case V4L2_SLICED_CAPTION_525:
			return IVTV_SLICED_TYPE_CAPTION_525;
		case V4L2_SLICED_WSS_625:
			return IVTV_SLICED_TYPE_WSS_625;
		case V4L2_SLICED_VPS:
			return IVTV_SLICED_TYPE_VPS;
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
	if (set == 0 || !valid_service_line(field, line, is_pal)) {
		return 0;
	}
	if (!is_pal) {
		if (line == 21 && (set & V4L2_SLICED_CAPTION_525))
			return V4L2_SLICED_CAPTION_525;
	}
	else {
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

void expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
{
	u16 set = fmt->service_set;
	int f, l;

	fmt->service_set = 0;
	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++) {
			fmt->service_lines[f][l] = select_service_from_set(f, l, set, is_pal);
		}
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

u16 get_service_set(struct v4l2_sliced_vbi_format *fmt)
{
	int f, l;
	u16 set = 0;

	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++) {
			set |= fmt->service_lines[f][l];
		}
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

static const struct v4l2_standard ivtv_std_60hz =
{
	.frameperiod = {.numerator = 1001, .denominator = 30000},
	.framelines = 525,
};

static const struct v4l2_standard ivtv_std_50hz =
{
	.frameperiod = {.numerator = 1, .denominator = 25},
	.framelines = 625,
};

void ivtv_set_osd_alpha(struct ivtv *itv)
{
	ivtv_vapi(itv, CX2341X_OSD_SET_GLOBAL_ALPHA, 3,
		itv->osd_global_alpha_state, itv->osd_global_alpha, !itv->osd_local_alpha_state);
	ivtv_vapi(itv, CX2341X_OSD_SET_CHROMA_KEY, 2, itv->osd_color_key_state, itv->osd_color_key);
}

int ivtv_set_speed(struct ivtv *itv, int speed)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv_stream *s;
	int single_step = (speed == 1 || speed == -1);
	DEFINE_WAIT(wait);

	if (speed == 0) speed = 1000;

	/* No change? */
	if (speed == itv->speed && !single_step)
		return 0;

	s = &itv->streams[IVTV_DEC_STREAM_TYPE_MPG];

	if (single_step && (speed < 0) == (itv->speed < 0)) {
		/* Single step video and no need to change direction */
		ivtv_vapi(itv, CX2341X_DEC_STEP_VIDEO, 1, 0);
		itv->speed = speed;
		return 0;
	}
	if (single_step)
		/* Need to change direction */
		speed = speed < 0 ? -1000 : 1000;

	data[0] = (speed > 1000 || speed < -1000) ? 0x80000000 : 0;
	data[0] |= (speed > 1000 || speed < -1500) ? 0x40000000 : 0;
	data[1] = (speed < 0);
	data[2] = speed < 0 ? 3 : 7;
	data[3] = itv->params.video_b_frames;
	data[4] = (speed == 1500 || speed == 500) ? itv->speed_mute_audio : 0;
	data[5] = 0;
	data[6] = 0;

	if (speed == 1500 || speed == -1500) data[0] |= 1;
	else if (speed == 2000 || speed == -2000) data[0] |= 2;
	else if (speed > -1000 && speed < 0) data[0] |= (-1000 / speed);
	else if (speed < 1000 && speed > 0) data[0] |= (1000 / speed);

	/* If not decoding, just change speed setting */
	if (atomic_read(&itv->decoding) > 0) {
		int got_sig = 0;

		/* Stop all DMA and decoding activity */
		ivtv_vapi(itv, CX2341X_DEC_PAUSE_PLAYBACK, 1, 0);

		/* Wait for any DMA to finish */
		prepare_to_wait(&itv->dma_waitq, &wait, TASK_INTERRUPTIBLE);
		while (itv->i_flags & IVTV_F_I_DMA) {
			got_sig = signal_pending(current);
			if (got_sig)
				break;
			got_sig = 0;
			schedule();
		}
		finish_wait(&itv->dma_waitq, &wait);
		if (got_sig)
			return -EINTR;

		/* Change Speed safely */
		ivtv_api(itv, CX2341X_DEC_SET_PLAYBACK_SPEED, 7, data);
		IVTV_DEBUG_INFO("Setting Speed to 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
				data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
	}
	if (single_step) {
		speed = (speed < 0) ? -1 : 1;
		ivtv_vapi(itv, CX2341X_DEC_STEP_VIDEO, 1, 0);
	}
	itv->speed = speed;
	return 0;
}

static int ivtv_validate_speed(int cur_speed, int new_speed)
{
	int fact = new_speed < 0 ? -1 : 1;
	int s;

	if (new_speed < 0) new_speed = -new_speed;
	if (cur_speed < 0) cur_speed = -cur_speed;

	if (cur_speed <= new_speed) {
		if (new_speed > 1500) return fact * 2000;
		if (new_speed > 1000) return fact * 1500;
	}
	else {
		if (new_speed >= 2000) return fact * 2000;
		if (new_speed >= 1500) return fact * 1500;
		if (new_speed >= 1000) return fact * 1000;
	}
	if (new_speed == 0) return 1000;
	if (new_speed == 1 || new_speed == 1000) return fact * new_speed;

	s = new_speed;
	new_speed = 1000 / new_speed;
	if (1000 / cur_speed == new_speed)
		new_speed += (cur_speed < s) ? -1 : 1;
	if (new_speed > 60) return 1000 / (fact * 60);
	return 1000 / (fact * new_speed);
}

static int ivtv_video_command(struct ivtv *itv, struct ivtv_open_id *id,
		struct video_command *vc, int try)
{
	struct ivtv_stream *s = &itv->streams[IVTV_DEC_STREAM_TYPE_MPG];

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;

	switch (vc->cmd) {
	case VIDEO_CMD_PLAY: {
		vc->flags = 0;
		vc->play.speed = ivtv_validate_speed(itv->speed, vc->play.speed);
		if (vc->play.speed < 0)
			vc->play.format = VIDEO_PLAY_FMT_GOP;
		if (try) break;

		if (ivtv_set_output_mode(itv, OUT_MPG) != OUT_MPG)
			return -EBUSY;
		return ivtv_start_decoding(id, vc->play.speed);
	}

	case VIDEO_CMD_STOP:
		vc->flags &= VIDEO_CMD_STOP_IMMEDIATELY|VIDEO_CMD_STOP_TO_BLACK;
		if (vc->flags & VIDEO_CMD_STOP_IMMEDIATELY)
			vc->stop.pts = 0;
		if (try) break;
		if (atomic_read(&itv->decoding) == 0)
			return 0;
		if (itv->output_mode != OUT_MPG)
			return -EBUSY;

		itv->output_mode = OUT_NONE;
		return ivtv_stop_v4l2_decode_stream(s, vc->flags, vc->stop.pts);

	case VIDEO_CMD_FREEZE:
		vc->flags &= VIDEO_CMD_FREEZE_TO_BLACK;
		if (try) break;
		if (itv->output_mode != OUT_MPG)
			return -EBUSY;
		if (atomic_read(&itv->decoding) > 0) {
			ivtv_vapi(itv, CX2341X_DEC_PAUSE_PLAYBACK, 1,
				(vc->flags & VIDEO_CMD_FREEZE_TO_BLACK) ? 1 : 0);
		}
		break;

	case VIDEO_CMD_CONTINUE:
		vc->flags = 0;
		if (try) break;
		if (itv->output_mode != OUT_MPG)
			return -EBUSY;
		if (atomic_read(&itv->decoding) > 0) {
			ivtv_vapi(itv, CX2341X_DEC_START_PLAYBACK, 2, 0, 0);
		}
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int ivtv_itvc(struct ivtv *itv, unsigned int cmd, void *arg)
{
	struct v4l2_register *regs = arg;
	unsigned long flags;
	volatile u8 __iomem *reg_start;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (regs->reg >= IVTV_REG_OFFSET && regs->reg < IVTV_REG_OFFSET + IVTV_REG_SIZE)
		reg_start = itv->reg_mem - IVTV_REG_OFFSET;
	else if (itv->has_cx23415 && regs->reg >= IVTV_DECODER_OFFSET &&
			regs->reg < IVTV_DECODER_OFFSET + IVTV_DECODER_SIZE)
		reg_start = itv->dec_mem - IVTV_DECODER_OFFSET;
	else if (regs->reg >= 0 && regs->reg < IVTV_ENCODER_SIZE)
		reg_start = itv->enc_mem;
	else
		return -EINVAL;

	spin_lock_irqsave(&ivtv_cards_lock, flags);
	if (cmd == VIDIOC_DBG_G_REGISTER) {
		regs->val = readl(regs->reg + reg_start);
	} else {
		writel(regs->val, regs->reg + reg_start);
	}
	spin_unlock_irqrestore(&ivtv_cards_lock, flags);
	return 0;
}

static int ivtv_get_fmt(struct ivtv *itv, int streamtype, struct v4l2_format *fmt)
{
	switch (fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		fmt->fmt.pix.width = itv->main_rect.width;
		fmt->fmt.pix.height = itv->main_rect.height;
		fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
		if (itv->output_mode == OUT_UDMA_YUV) {
			switch (itv->yuv_info.lace_mode & IVTV_YUV_MODE_MASK) {
			case IVTV_YUV_MODE_INTERLACED:
				fmt->fmt.pix.field = (itv->yuv_info.lace_mode & IVTV_YUV_SYNC_MASK) ?
					V4L2_FIELD_INTERLACED_BT : V4L2_FIELD_INTERLACED_TB;
				break;
			case IVTV_YUV_MODE_PROGRESSIVE:
				fmt->fmt.pix.field = V4L2_FIELD_NONE;
				break;
			default:
				fmt->fmt.pix.field = V4L2_FIELD_ANY;
				break;
			}
			fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_HM12;
			/* YUV size is (Y=(h*w) + UV=(h*(w/2))) */
			fmt->fmt.pix.sizeimage =
				fmt->fmt.pix.height * fmt->fmt.pix.width +
				fmt->fmt.pix.height * (fmt->fmt.pix.width / 2);
		}
		else if (itv->output_mode == OUT_YUV ||
				streamtype == IVTV_ENC_STREAM_TYPE_YUV ||
				streamtype == IVTV_DEC_STREAM_TYPE_YUV) {
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

	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		fmt->fmt.pix.width = itv->params.width;
		fmt->fmt.pix.height = itv->params.height;
		fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
		if (streamtype == IVTV_ENC_STREAM_TYPE_YUV ||
				streamtype == IVTV_DEC_STREAM_TYPE_YUV) {
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

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		fmt->fmt.win.chromakey = itv->osd_color_key;
		fmt->fmt.win.global_alpha = itv->osd_global_alpha;
		break;

	case V4L2_BUF_TYPE_VBI_CAPTURE:
		fmt->fmt.vbi.sampling_rate = 27000000;
		fmt->fmt.vbi.offset = 248;
		fmt->fmt.vbi.samples_per_line = itv->vbi.raw_decoder_line_size - 4;
		fmt->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
		fmt->fmt.vbi.start[0] = itv->vbi.start[0];
		fmt->fmt.vbi.start[1] = itv->vbi.start[1];
		fmt->fmt.vbi.count[0] = fmt->fmt.vbi.count[1] = itv->vbi.count;
		break;

	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
	{
		struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

		if (!(itv->v4l2_cap & V4L2_CAP_SLICED_VBI_OUTPUT))
			return -EINVAL;
		vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
		memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));
		memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));
		if (itv->is_60hz) {
			vbifmt->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
			vbifmt->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
		} else {
			vbifmt->service_lines[0][23] = V4L2_SLICED_WSS_625;
			vbifmt->service_lines[0][16] = V4L2_SLICED_VPS;
		}
		vbifmt->service_set = get_service_set(vbifmt);
		break;
	}

	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	{
		struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

		vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
		memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));
		memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));

		if (streamtype == IVTV_DEC_STREAM_TYPE_VBI) {
			vbifmt->service_set = itv->is_50hz ? V4L2_SLICED_VBI_625 :
						 V4L2_SLICED_VBI_525;
			expand_service_set(vbifmt, itv->is_50hz);
			break;
		}

		itv->video_dec_func(itv, VIDIOC_G_FMT, fmt);
		vbifmt->service_set = get_service_set(vbifmt);
		break;
	}
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	default:
		return -EINVAL;
	}
	return 0;
}

static int ivtv_try_or_set_fmt(struct ivtv *itv, int streamtype,
		struct v4l2_format *fmt, int set_fmt)
{
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;
	u16 set;

	if (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		struct v4l2_rect r;
		int field;

		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		field = fmt->fmt.pix.field;
		r.top = 0;
		r.left = 0;
		r.width = fmt->fmt.pix.width;
		r.height = fmt->fmt.pix.height;
		ivtv_get_fmt(itv, streamtype, fmt);
		if (itv->output_mode != OUT_UDMA_YUV) {
			/* TODO: would setting the rect also be valid for this mode? */
			fmt->fmt.pix.width = r.width;
			fmt->fmt.pix.height = r.height;
		}
		if (itv->output_mode == OUT_UDMA_YUV) {
			/* TODO: add checks for validity */
			fmt->fmt.pix.field = field;
		}
		if (set_fmt) {
			if (itv->output_mode == OUT_UDMA_YUV) {
				switch (field) {
				case V4L2_FIELD_NONE:
					itv->yuv_info.lace_mode = IVTV_YUV_MODE_PROGRESSIVE;
					break;
				case V4L2_FIELD_ANY:
					itv->yuv_info.lace_mode = IVTV_YUV_MODE_AUTO;
					break;
				case V4L2_FIELD_INTERLACED_BT:
					itv->yuv_info.lace_mode =
						IVTV_YUV_MODE_INTERLACED|IVTV_YUV_SYNC_ODD;
					break;
				case V4L2_FIELD_INTERLACED_TB:
				default:
					itv->yuv_info.lace_mode = IVTV_YUV_MODE_INTERLACED;
					break;
				}
				itv->yuv_info.lace_sync_field = (itv->yuv_info.lace_mode & IVTV_YUV_SYNC_MASK) == IVTV_YUV_SYNC_EVEN ? 0 : 1;

				/* Force update of yuv registers */
				itv->yuv_info.yuv_forced_update = 1;
				return 0;
			}
		}
		return 0;
	}

	if (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY) {
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		if (set_fmt) {
			itv->osd_color_key = fmt->fmt.win.chromakey;
			itv->osd_global_alpha = fmt->fmt.win.global_alpha;
			ivtv_set_osd_alpha(itv);
		}
		return 0;
	}

	/* set window size */
	if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		int w = fmt->fmt.pix.width;
		int h = fmt->fmt.pix.height;

		if (w > 720) w = 720;
		else if (w < 1) w = 1;
		if (h > (itv->is_50hz ? 576 : 480)) h = (itv->is_50hz ? 576 : 480);
		else if (h < 2) h = 2;
		ivtv_get_fmt(itv, streamtype, fmt);
		fmt->fmt.pix.width = w;
		fmt->fmt.pix.height = h;

		if (!set_fmt || (itv->params.width == w && itv->params.height == h))
			return 0;
		if (atomic_read(&itv->capturing) > 0)
			return -EBUSY;

		itv->params.width = w;
		itv->params.height = h;
		if (w != 720 || h != (itv->is_50hz ? 576 : 480))
			itv->params.video_temporal_filter = 0;
		else
			itv->params.video_temporal_filter = 8;
		itv->video_dec_func(itv, VIDIOC_S_FMT, fmt);
		return ivtv_get_fmt(itv, streamtype, fmt);
	}

	/* set raw VBI format */
	if (fmt->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		if (set_fmt && streamtype == IVTV_ENC_STREAM_TYPE_VBI &&
		    itv->vbi.sliced_in->service_set &&
		    atomic_read(&itv->capturing) > 0) {
			return -EBUSY;
		}
		if (set_fmt) {
			itv->vbi.sliced_in->service_set = 0;
			itv->video_dec_func(itv, VIDIOC_S_FMT, &itv->vbi.in);
		}
		return ivtv_get_fmt(itv, streamtype, fmt);
	}

	/* set sliced VBI output
	   In principle the user could request that only certain
	   VBI types are output and that the others are ignored.
	   I.e., suppress CC in the even fields or only output
	   WSS and no VPS. Currently though there is no choice. */
	if (fmt->type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT)
		return ivtv_get_fmt(itv, streamtype, fmt);

	/* any else but sliced VBI capture is an error */
	if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
		return -EINVAL;

	if (streamtype == IVTV_DEC_STREAM_TYPE_VBI)
		return ivtv_get_fmt(itv, streamtype, fmt);

	/* set sliced VBI capture format */
	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	memset(vbifmt->reserved, 0, sizeof(vbifmt->reserved));

	if (vbifmt->service_set)
		expand_service_set(vbifmt, itv->is_50hz);
	set = check_service_set(vbifmt, itv->is_50hz);
	vbifmt->service_set = get_service_set(vbifmt);

	if (!set_fmt)
		return 0;
	if (set == 0)
		return -EINVAL;
	if (atomic_read(&itv->capturing) > 0 && itv->vbi.sliced_in->service_set == 0) {
		return -EBUSY;
	}
	itv->video_dec_func(itv, VIDIOC_S_FMT, fmt);
	memcpy(itv->vbi.sliced_in, vbifmt, sizeof(*itv->vbi.sliced_in));
	return 0;
}

static int ivtv_debug_ioctls(struct file *filp, unsigned int cmd, void *arg)
{
	struct ivtv_open_id *id = (struct ivtv_open_id *)filp->private_data;
	struct ivtv *itv = id->itv;
	struct v4l2_register *reg = arg;

	switch (cmd) {
	/* ioctls to allow direct access to the encoder registers for testing */
	case VIDIOC_DBG_G_REGISTER:
		if (v4l2_chip_match_host(reg->match_type, reg->match_chip))
			return ivtv_itvc(itv, cmd, arg);
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_DRIVER)
			return ivtv_i2c_id(itv, reg->match_chip, cmd, arg);
		return ivtv_call_i2c_client(itv, reg->match_chip, cmd, arg);

	case VIDIOC_DBG_S_REGISTER:
		if (v4l2_chip_match_host(reg->match_type, reg->match_chip))
			return ivtv_itvc(itv, cmd, arg);
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_DRIVER)
			return ivtv_i2c_id(itv, reg->match_chip, cmd, arg);
		return ivtv_call_i2c_client(itv, reg->match_chip, cmd, arg);

	case VIDIOC_G_CHIP_IDENT: {
		struct v4l2_chip_ident *chip = arg;

		chip->ident = V4L2_IDENT_NONE;
		chip->revision = 0;
		if (reg->match_type == V4L2_CHIP_MATCH_HOST) {
			if (v4l2_chip_match_host(reg->match_type, reg->match_chip)) {
				struct v4l2_chip_ident *chip = arg;

				chip->ident = itv->has_cx23415 ? V4L2_IDENT_CX23415 : V4L2_IDENT_CX23416;
			}
			return 0;
		}
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_DRIVER)
			return ivtv_i2c_id(itv, reg->match_chip, cmd, arg);
		if (reg->match_type == V4L2_CHIP_MATCH_I2C_ADDR)
			return ivtv_call_i2c_client(itv, reg->match_chip, cmd, arg);
		return -EINVAL;
	}

	case VIDIOC_INT_S_AUDIO_ROUTING: {
		struct v4l2_routing *route = arg;

		ivtv_audio_set_route(itv, route);
		break;
	}

	case VIDIOC_INT_RESET:
		ivtv_reset_ir_gpio(itv);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

int ivtv_v4l2_ioctls(struct ivtv *itv, struct file *filp, unsigned int cmd, void *arg)
{
	struct ivtv_open_id *id = NULL;

	if (filp) id = (struct ivtv_open_id *)filp->private_data;

	switch (cmd) {
	case VIDIOC_G_PRIORITY:
	{
		enum v4l2_priority *p = arg;

		*p = v4l2_prio_max(&itv->prio);
		break;
	}

	case VIDIOC_S_PRIORITY:
	{
		enum v4l2_priority *prio = arg;

		return v4l2_prio_change(&itv->prio, &id->prio, *prio);
	}

	case VIDIOC_QUERYCAP:{
		struct v4l2_capability *vcap = arg;

		memset(vcap, 0, sizeof(*vcap));
		strcpy(vcap->driver, IVTV_DRIVER_NAME);     /* driver name */
		strcpy(vcap->card, itv->card_name); 	    /* card type */
		strcpy(vcap->bus_info, pci_name(itv->dev)); /* bus info... */
		vcap->version = IVTV_DRIVER_VERSION; 	    /* version */
		vcap->capabilities = itv->v4l2_cap; 	    /* capabilities */

		/* reserved.. must set to 0! */
		vcap->reserved[0] = vcap->reserved[1] =
			vcap->reserved[2] = vcap->reserved[3] = 0;
		break;
	}

	case VIDIOC_ENUMAUDIO:{
		struct v4l2_audio *vin = arg;

		return ivtv_get_audio_input(itv, vin->index, vin);
	}

	case VIDIOC_G_AUDIO:{
		struct v4l2_audio *vin = arg;

		vin->index = itv->audio_input;
		return ivtv_get_audio_input(itv, vin->index, vin);
	}

	case VIDIOC_S_AUDIO:{
		struct v4l2_audio *vout = arg;

		if (vout->index >= itv->nof_audio_inputs)
			return -EINVAL;
		itv->audio_input = vout->index;
		ivtv_audio_set_io(itv);
		break;
	}

	case VIDIOC_ENUMAUDOUT:{
		struct v4l2_audioout *vin = arg;

		/* set it to defaults from our table */
		return ivtv_get_audio_output(itv, vin->index, vin);
	}

	case VIDIOC_G_AUDOUT:{
		struct v4l2_audioout *vin = arg;

		vin->index = 0;
		return ivtv_get_audio_output(itv, vin->index, vin);
	}

	case VIDIOC_S_AUDOUT:{
		struct v4l2_audioout *vout = arg;

		return ivtv_get_audio_output(itv, vout->index, vout);
	}

	case VIDIOC_ENUMINPUT:{
		struct v4l2_input *vin = arg;

		/* set it to defaults from our table */
		return ivtv_get_input(itv, vin->index, vin);
	}

	case VIDIOC_ENUMOUTPUT:{
		struct v4l2_output *vout = arg;

		return ivtv_get_output(itv, vout->index, vout);
	}

	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT: {
		struct v4l2_format *fmt = arg;

		return ivtv_try_or_set_fmt(itv, id->type, fmt, cmd == VIDIOC_S_FMT);
	}

	case VIDIOC_G_FMT: {
		struct v4l2_format *fmt = arg;
		int type = fmt->type;

		memset(fmt, 0, sizeof(*fmt));
		fmt->type = type;
		return ivtv_get_fmt(itv, id->type, fmt);
	}

	case VIDIOC_CROPCAP: {
		struct v4l2_cropcap *cropcap = arg;

		if (cropcap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    cropcap->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		cropcap->bounds.top = cropcap->bounds.left = 0;
		cropcap->bounds.width = 720;
		if (cropcap->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			cropcap->bounds.height = itv->is_50hz ? 576 : 480;
			cropcap->pixelaspect.numerator = itv->is_50hz ? 59 : 10;
			cropcap->pixelaspect.denominator = itv->is_50hz ? 54 : 11;
		} else {
			cropcap->bounds.height = itv->is_out_50hz ? 576 : 480;
			cropcap->pixelaspect.numerator = itv->is_out_50hz ? 59 : 10;
			cropcap->pixelaspect.denominator = itv->is_out_50hz ? 54 : 11;
		}
		cropcap->defrect = cropcap->bounds;
		return 0;
	}

	case VIDIOC_S_CROP: {
		struct v4l2_crop *crop = arg;

		if (crop->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		    (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT)) {
			if (!ivtv_vapi(itv, CX2341X_OSD_SET_FRAMEBUFFER_WINDOW, 4,
				 crop->c.width, crop->c.height, crop->c.left, crop->c.top)) {
				itv->main_rect = crop->c;
				return 0;
			}
			return -EINVAL;
		}
		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		return itv->video_dec_func(itv, VIDIOC_S_CROP, arg);
	}

	case VIDIOC_G_CROP: {
		struct v4l2_crop *crop = arg;

		if (crop->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		    (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT)) {
			crop->c = itv->main_rect;
			return 0;
		}
		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		return itv->video_dec_func(itv, VIDIOC_G_CROP, arg);
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
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
				return -EINVAL;
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
		*(int *)arg = itv->active_input;
		break;
	}

	case VIDIOC_S_INPUT:{
		int inp = *(int *)arg;

		if (inp < 0 || inp >= itv->nof_inputs)
			return -EINVAL;

		if (inp == itv->active_input) {
			IVTV_DEBUG_INFO("Input unchanged\n");
			break;
		}
		IVTV_DEBUG_INFO("Changing input from %d to %d\n",
				itv->active_input, inp);

		itv->active_input = inp;
		/* Set the audio input to whatever is appropriate for the
		   input type. */
		itv->audio_input = itv->card->video_inputs[inp].audio_index;

		/* prevent others from messing with the streams until
		   we're finished changing inputs. */
		ivtv_mute(itv);
		ivtv_video_set_io(itv);
		ivtv_audio_set_io(itv);
		ivtv_unmute(itv);
		break;
	}

	case VIDIOC_G_OUTPUT:{
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		*(int *)arg = itv->active_output;
		break;
	}

	case VIDIOC_S_OUTPUT:{
		int outp = *(int *)arg;
		struct v4l2_routing route;

		if (outp >= itv->card->nof_outputs)
			return -EINVAL;

		if (outp == itv->active_output) {
			IVTV_DEBUG_INFO("Output unchanged\n");
			break;
		}
		IVTV_DEBUG_INFO("Changing output from %d to %d\n",
			   itv->active_output, outp);

		itv->active_output = outp;
		route.input = SAA7127_INPUT_TYPE_NORMAL;
		route.output = itv->card->video_outputs[outp].video_output;
		ivtv_saa7127(itv, VIDIOC_INT_S_VIDEO_ROUTING, &route);
		break;
	}

	case VIDIOC_G_FREQUENCY:{
		struct v4l2_frequency *vf = arg;

		if (vf->tuner != 0)
			return -EINVAL;
		ivtv_call_i2c_clients(itv, cmd, arg);
		break;
	}

	case VIDIOC_S_FREQUENCY:{
		struct v4l2_frequency vf = *(struct v4l2_frequency *)arg;

		if (vf.tuner != 0)
			return -EINVAL;

		ivtv_mute(itv);
		IVTV_DEBUG_INFO("v4l2 ioctl: set frequency %d\n", vf.frequency);
		ivtv_call_i2c_clients(itv, cmd, &vf);
		ivtv_unmute(itv);
		break;
	}

	case VIDIOC_ENUMSTD:{
		struct v4l2_standard *vs = arg;
		int idx = vs->index;

		if (idx < 0 || idx >= ARRAY_SIZE(enum_stds))
			return -EINVAL;

		*vs = (enum_stds[idx].std & V4L2_STD_525_60) ?
				ivtv_std_60hz : ivtv_std_50hz;
		vs->index = idx;
		vs->id = enum_stds[idx].std;
		strcpy(vs->name, enum_stds[idx].name);
		break;
	}

	case VIDIOC_G_STD:{
		*(v4l2_std_id *) arg = itv->std;
		break;
	}

	case VIDIOC_S_STD: {
		v4l2_std_id std = *(v4l2_std_id *) arg;

		if ((std & V4L2_STD_ALL) == 0)
			return -EINVAL;

		if (std == itv->std)
			break;

		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags) ||
		    atomic_read(&itv->capturing) > 0 ||
		    atomic_read(&itv->decoding) > 0) {
			/* Switching standard would turn off the radio or mess
			   with already running streams, prevent that by
			   returning EBUSY. */
			return -EBUSY;
		}

		itv->std = std;
		itv->is_60hz = (std & V4L2_STD_525_60) ? 1 : 0;
		itv->params.is_50hz = itv->is_50hz = !itv->is_60hz;
		itv->params.width = 720;
		itv->params.height = itv->is_50hz ? 576 : 480;
		itv->vbi.count = itv->is_50hz ? 18 : 12;
		itv->vbi.start[0] = itv->is_50hz ? 6 : 10;
		itv->vbi.start[1] = itv->is_50hz ? 318 : 273;
		if (itv->hw_flags & IVTV_HW_CX25840) {
			itv->vbi.sliced_decoder_line_size = itv->is_60hz ? 272 : 284;
		}
		IVTV_DEBUG_INFO("Switching standard to %llx.\n", (unsigned long long)itv->std);

		/* Tuner */
		ivtv_call_i2c_clients(itv, VIDIOC_S_STD, &itv->std);

		if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT) {
			/* set display standard */
			itv->std_out = std;
			itv->is_out_60hz = itv->is_60hz;
			itv->is_out_50hz = itv->is_50hz;
			ivtv_call_i2c_clients(itv, VIDIOC_INT_S_STD_OUTPUT, &itv->std_out);
			ivtv_vapi(itv, CX2341X_DEC_SET_STANDARD, 1, itv->is_out_50hz);
			itv->main_rect.left = itv->main_rect.top = 0;
			itv->main_rect.width = 720;
			itv->main_rect.height = itv->params.height;
			ivtv_vapi(itv, CX2341X_OSD_SET_FRAMEBUFFER_WINDOW, 4,
				720, itv->main_rect.height, 0, 0);
		}
		break;
	}

	case VIDIOC_S_TUNER: {	/* Setting tuner can only set audio mode */
		struct v4l2_tuner *vt = arg;

		if (vt->index != 0)
			return -EINVAL;

		ivtv_call_i2c_clients(itv, VIDIOC_S_TUNER, vt);
		break;
	}

	case VIDIOC_G_TUNER: {
		struct v4l2_tuner *vt = arg;

		if (vt->index != 0)
			return -EINVAL;

		memset(vt, 0, sizeof(*vt));
		ivtv_call_i2c_clients(itv, VIDIOC_G_TUNER, vt);

		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			strcpy(vt->name, "ivtv Radio Tuner");
			vt->type = V4L2_TUNER_RADIO;
		} else {
			strcpy(vt->name, "ivtv TV Tuner");
			vt->type = V4L2_TUNER_ANALOG_TV;
		}
		break;
	}

	case VIDIOC_G_SLICED_VBI_CAP: {
		struct v4l2_sliced_vbi_cap *cap = arg;
		int set = itv->is_50hz ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525;
		int f, l;
		enum v4l2_buf_type type = cap->type;

		memset(cap, 0, sizeof(*cap));
		cap->type = type;
		if (type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) {
			for (f = 0; f < 2; f++) {
				for (l = 0; l < 24; l++) {
					if (valid_service_line(f, l, itv->is_50hz)) {
						cap->service_lines[f][l] = set;
					}
				}
			}
			return 0;
		}
		if (type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT) {
			if (!(itv->v4l2_cap & V4L2_CAP_SLICED_VBI_OUTPUT))
				return -EINVAL;
			if (itv->is_60hz) {
				cap->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
				cap->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
			} else {
				cap->service_lines[0][23] = V4L2_SLICED_WSS_625;
				cap->service_lines[0][16] = V4L2_SLICED_VPS;
			}
			return 0;
		}
		return -EINVAL;
	}

	case VIDIOC_G_ENC_INDEX: {
		struct v4l2_enc_idx *idx = arg;
		int i;

		idx->entries = (itv->pgm_info_write_idx + IVTV_MAX_PGM_INDEX - itv->pgm_info_read_idx) %
					IVTV_MAX_PGM_INDEX;
		if (idx->entries > V4L2_ENC_IDX_ENTRIES)
			idx->entries = V4L2_ENC_IDX_ENTRIES;
		for (i = 0; i < idx->entries; i++) {
			idx->entry[i] = itv->pgm_info[(itv->pgm_info_read_idx + i) % IVTV_MAX_PGM_INDEX];
		}
		itv->pgm_info_read_idx = (itv->pgm_info_read_idx + idx->entries) % IVTV_MAX_PGM_INDEX;
		break;
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
			return ivtv_start_capture(id);

		case V4L2_ENC_CMD_STOP:
			enc->flags &= V4L2_ENC_CMD_STOP_AT_GOP_END;
			if (try)
				return 0;
			ivtv_stop_capture(id, enc->flags & V4L2_ENC_CMD_STOP_AT_GOP_END);
			return 0;

		case V4L2_ENC_CMD_PAUSE:
			enc->flags = 0;
			if (try)
				return 0;
			if (!atomic_read(&itv->capturing))
				return -EPERM;
			if (test_and_set_bit(IVTV_F_I_ENC_PAUSED, &itv->i_flags))
				return 0;
			ivtv_mute(itv);
			ivtv_vapi(itv, CX2341X_ENC_PAUSE_ENCODER, 1, 0);
			break;

		case V4L2_ENC_CMD_RESUME:
			enc->flags = 0;
			if (try)
				return 0;
			if (!atomic_read(&itv->capturing))
				return -EPERM;
			if (!test_and_clear_bit(IVTV_F_I_ENC_PAUSED, &itv->i_flags))
				return 0;
			ivtv_vapi(itv, CX2341X_ENC_PAUSE_ENCODER, 1, 1);
			ivtv_unmute(itv);
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	case VIDIOC_G_FBUF: {
		struct v4l2_framebuffer *fb = arg;

		memset(fb, 0, sizeof(*fb));
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
			return -EINVAL;
		fb->capability = V4L2_FBUF_CAP_EXTERNOVERLAY | V4L2_FBUF_CAP_CHROMAKEY |
			V4L2_FBUF_CAP_LOCAL_ALPHA | V4L2_FBUF_CAP_GLOBAL_ALPHA;
		fb->fmt.pixelformat = itv->osd_pixelformat;
		fb->fmt.width = itv->osd_rect.width;
		fb->fmt.height = itv->osd_rect.height;
		fb->base = (void *)itv->osd_video_pbase;
		if (itv->osd_global_alpha_state)
			fb->flags |= V4L2_FBUF_FLAG_GLOBAL_ALPHA;
		if (itv->osd_local_alpha_state)
			fb->flags |= V4L2_FBUF_FLAG_LOCAL_ALPHA;
		if (itv->osd_color_key_state)
			fb->flags |= V4L2_FBUF_FLAG_CHROMAKEY;
		break;
	}

	case VIDIOC_S_FBUF: {
		struct v4l2_framebuffer *fb = arg;

		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
			return -EINVAL;
		itv->osd_global_alpha_state = (fb->flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) != 0;
		itv->osd_local_alpha_state = (fb->flags & V4L2_FBUF_FLAG_LOCAL_ALPHA) != 0;
		itv->osd_color_key_state = (fb->flags & V4L2_FBUF_FLAG_CHROMAKEY) != 0;
		break;
	}

	case VIDIOC_LOG_STATUS:
	{
		int has_output = itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT;
		struct v4l2_input vidin;
		struct v4l2_audio audin;
		int i;

		IVTV_INFO("=================  START STATUS CARD #%d  =================\n", itv->num);
		if (itv->hw_flags & IVTV_HW_TVEEPROM) {
			struct tveeprom tv;

			ivtv_read_eeprom(itv, &tv);
		}
		ivtv_call_i2c_clients(itv, VIDIOC_LOG_STATUS, NULL);
		ivtv_get_input(itv, itv->active_input, &vidin);
		ivtv_get_audio_input(itv, itv->audio_input, &audin);
		IVTV_INFO("Video Input: %s\n", vidin.name);
		IVTV_INFO("Audio Input: %s\n", audin.name);
		if (has_output) {
			struct v4l2_output vidout;
			struct v4l2_audioout audout;
			int mode = itv->output_mode;
			static const char * const output_modes[] = {
				"None",
				"MPEG Streaming",
				"YUV Streaming",
				"YUV Frames",
				"Passthrough",
			};

			ivtv_get_output(itv, itv->active_output, &vidout);
			ivtv_get_audio_output(itv, 0, &audout);
			IVTV_INFO("Video Output: %s\n", vidout.name);
			IVTV_INFO("Audio Output: %s\n", audout.name);
			if (mode < 0 || mode > OUT_PASSTHROUGH)
				mode = OUT_NONE;
			IVTV_INFO("Output Mode: %s\n", output_modes[mode]);
		}
		IVTV_INFO("Tuner: %s\n",
			test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags) ? "Radio" : "TV");
		cx2341x_log_status(&itv->params, itv->name);
		IVTV_INFO("Status flags: 0x%08lx\n", itv->i_flags);
		for (i = 0; i < IVTV_MAX_STREAMS; i++) {
			struct ivtv_stream *s = &itv->streams[i];

			if (s->v4l2dev == NULL || s->buffers == 0)
				continue;
			IVTV_INFO("Stream %s: status 0x%04lx, %d%% of %d KiB (%d buffers) in use\n", s->name, s->s_flags,
					(s->buffers - s->q_free.buffers) * 100 / s->buffers,
					(s->buffers * s->buf_size) / 1024, s->buffers);
		}
		IVTV_INFO("Read MPEG/VBI: %lld/%lld bytes\n", (long long)itv->mpg_data_received, (long long)itv->vbi_data_inserted);
		IVTV_INFO("==================  END STATUS CARD #%d  ==================\n", itv->num);
		break;
	}

	default:
		return -EINVAL;
	}
	return 0;
}

static int ivtv_decoder_ioctls(struct file *filp, unsigned int cmd, void *arg)
{
	struct ivtv_open_id *id = (struct ivtv_open_id *)filp->private_data;
	struct ivtv *itv = id->itv;
	int nonblocking = filp->f_flags & O_NONBLOCK;
	struct ivtv_stream *s = &itv->streams[id->type];

	switch (cmd) {
	case IVTV_IOC_DMA_FRAME: {
		struct ivtv_dma_frame *args = arg;

		IVTV_DEBUG_IOCTL("IVTV_IOC_DMA_FRAME\n");
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		if (args->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		if (itv->output_mode == OUT_UDMA_YUV && args->y_source == NULL)
			return 0;
		if (ivtv_claim_stream(id, id->type)) {
			return -EBUSY;
		}
		if (ivtv_set_output_mode(itv, OUT_UDMA_YUV) != OUT_UDMA_YUV) {
			ivtv_release_stream(s);
			return -EBUSY;
		}
		if (args->y_source == NULL)
			return 0;
		return ivtv_yuv_prep_frame(itv, args);
	}

	case VIDEO_GET_PTS: {
		u32 data[CX2341X_MBOX_MAX_DATA];
		u64 *pts = arg;

		IVTV_DEBUG_IOCTL("VIDEO_GET_PTS\n");
		if (s->type < IVTV_DEC_STREAM_TYPE_MPG) {
			*pts = s->dma_pts;
			break;
		}
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;

		if (test_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags)) {
			*pts = (u64) ((u64)itv->last_dec_timing[2] << 32) |
					(u64)itv->last_dec_timing[1];
			break;
		}
		*pts = 0;
		if (atomic_read(&itv->decoding)) {
			if (ivtv_api(itv, CX2341X_DEC_GET_TIMING_INFO, 5, data)) {
				IVTV_DEBUG_WARN("GET_TIMING: couldn't read clock\n");
				return -EIO;
			}
			memcpy(itv->last_dec_timing, data, sizeof(itv->last_dec_timing));
			set_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags);
			*pts = (u64) ((u64) data[2] << 32) | (u64) data[1];
			/*timing->scr = (u64) (((u64) data[4] << 32) | (u64) (data[3]));*/
		}
		break;
	}

	case VIDEO_GET_FRAME_COUNT: {
		u32 data[CX2341X_MBOX_MAX_DATA];
		u64 *frame = arg;

		IVTV_DEBUG_IOCTL("VIDEO_GET_FRAME_COUNT\n");
		if (s->type < IVTV_DEC_STREAM_TYPE_MPG) {
			*frame = 0;
			break;
		}
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;

		if (test_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags)) {
			*frame = itv->last_dec_timing[0];
			break;
		}
		*frame = 0;
		if (atomic_read(&itv->decoding)) {
			if (ivtv_api(itv, CX2341X_DEC_GET_TIMING_INFO, 5, data)) {
				IVTV_DEBUG_WARN("GET_TIMING: couldn't read clock\n");
				return -EIO;
			}
			memcpy(itv->last_dec_timing, data, sizeof(itv->last_dec_timing));
			set_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags);
			*frame = data[0];
		}
		break;
	}

	case VIDEO_PLAY: {
		struct video_command vc;

		IVTV_DEBUG_IOCTL("VIDEO_PLAY\n");
		memset(&vc, 0, sizeof(vc));
		vc.cmd = VIDEO_CMD_PLAY;
		return ivtv_video_command(itv, id, &vc, 0);
	}

	case VIDEO_STOP: {
		struct video_command vc;

		IVTV_DEBUG_IOCTL("VIDEO_STOP\n");
		memset(&vc, 0, sizeof(vc));
		vc.cmd = VIDEO_CMD_STOP;
		vc.flags = VIDEO_CMD_STOP_TO_BLACK | VIDEO_CMD_STOP_IMMEDIATELY;
		return ivtv_video_command(itv, id, &vc, 0);
	}

	case VIDEO_FREEZE: {
		struct video_command vc;

		IVTV_DEBUG_IOCTL("VIDEO_FREEZE\n");
		memset(&vc, 0, sizeof(vc));
		vc.cmd = VIDEO_CMD_FREEZE;
		return ivtv_video_command(itv, id, &vc, 0);
	}

	case VIDEO_CONTINUE: {
		struct video_command vc;

		IVTV_DEBUG_IOCTL("VIDEO_CONTINUE\n");
		memset(&vc, 0, sizeof(vc));
		vc.cmd = VIDEO_CMD_CONTINUE;
		return ivtv_video_command(itv, id, &vc, 0);
	}

	case VIDEO_COMMAND:
	case VIDEO_TRY_COMMAND: {
		struct video_command *vc = arg;
		int try = (cmd == VIDEO_TRY_COMMAND);

		if (try)
			IVTV_DEBUG_IOCTL("VIDEO_TRY_COMMAND\n");
		else
			IVTV_DEBUG_IOCTL("VIDEO_COMMAND\n");
		return ivtv_video_command(itv, id, vc, try);
	}

	case VIDEO_GET_EVENT: {
		struct video_event *ev = arg;
		DEFINE_WAIT(wait);

		IVTV_DEBUG_IOCTL("VIDEO_GET_EVENT\n");
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		memset(ev, 0, sizeof(*ev));
		set_bit(IVTV_F_I_EV_VSYNC_ENABLED, &itv->i_flags);

		while (1) {
			if (test_and_clear_bit(IVTV_F_I_EV_DEC_STOPPED, &itv->i_flags))
				ev->type = VIDEO_EVENT_DECODER_STOPPED;
			else if (test_and_clear_bit(IVTV_F_I_EV_VSYNC, &itv->i_flags)) {
				ev->type = VIDEO_EVENT_VSYNC;
				ev->u.vsync_field = test_bit(IVTV_F_I_EV_VSYNC_FIELD, &itv->i_flags) ?
					VIDEO_VSYNC_FIELD_ODD : VIDEO_VSYNC_FIELD_EVEN;
				if (itv->output_mode == OUT_UDMA_YUV &&
					(itv->yuv_info.lace_mode & IVTV_YUV_MODE_MASK) ==
								IVTV_YUV_MODE_PROGRESSIVE) {
					ev->u.vsync_field = VIDEO_VSYNC_FIELD_PROGRESSIVE;
				}
			}
			if (ev->type)
				return 0;
			if (nonblocking)
				return -EAGAIN;
			/* wait for event */
			prepare_to_wait(&itv->event_waitq, &wait, TASK_INTERRUPTIBLE);
			if ((itv->i_flags & (IVTV_F_I_EV_DEC_STOPPED|IVTV_F_I_EV_VSYNC)) == 0)
				schedule();
			finish_wait(&itv->event_waitq, &wait);
			if (signal_pending(current)) {
				/* return if a signal was received */
				IVTV_DEBUG_INFO("User stopped wait for event\n");
				return -EINTR;
			}
		}
		break;
	}

	default:
		return -EINVAL;
	}
	return 0;
}

static int ivtv_v4l2_do_ioctl(struct inode *inode, struct file *filp,
			      unsigned int cmd, void *arg)
{
	struct ivtv_open_id *id = (struct ivtv_open_id *)filp->private_data;
	struct ivtv *itv = id->itv;
	int ret;

	/* check priority */
	switch (cmd) {
	case VIDIOC_S_CTRL:
	case VIDIOC_S_STD:
	case VIDIOC_S_INPUT:
	case VIDIOC_S_OUTPUT:
	case VIDIOC_S_TUNER:
	case VIDIOC_S_FREQUENCY:
	case VIDIOC_S_FMT:
	case VIDIOC_S_CROP:
	case VIDIOC_S_AUDIO:
	case VIDIOC_S_AUDOUT:
	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_S_FBUF:
		ret = v4l2_prio_check(&itv->prio, &id->prio);
		if (ret)
			return ret;
	}

	switch (cmd) {
	case VIDIOC_DBG_G_REGISTER:
	case VIDIOC_DBG_S_REGISTER:
	case VIDIOC_G_CHIP_IDENT:
	case VIDIOC_INT_S_AUDIO_ROUTING:
	case VIDIOC_INT_RESET:
		if (ivtv_debug & IVTV_DBGFLG_IOCTL) {
			printk(KERN_INFO "ivtv%d ioctl: ", itv->num);
			v4l_printk_ioctl(cmd);
		}
		return ivtv_debug_ioctls(filp, cmd, arg);

	case VIDIOC_G_PRIORITY:
	case VIDIOC_S_PRIORITY:
	case VIDIOC_QUERYCAP:
	case VIDIOC_ENUMINPUT:
	case VIDIOC_G_INPUT:
	case VIDIOC_S_INPUT:
	case VIDIOC_ENUMOUTPUT:
	case VIDIOC_G_OUTPUT:
	case VIDIOC_S_OUTPUT:
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
	case VIDIOC_ENUMAUDOUT:
	case VIDIOC_S_AUDOUT:
	case VIDIOC_G_AUDOUT:
	case VIDIOC_G_SLICED_VBI_CAP:
	case VIDIOC_LOG_STATUS:
	case VIDIOC_G_ENC_INDEX:
	case VIDIOC_ENCODER_CMD:
	case VIDIOC_TRY_ENCODER_CMD:
	case VIDIOC_G_FBUF:
	case VIDIOC_S_FBUF:
		if (ivtv_debug & IVTV_DBGFLG_IOCTL) {
			printk(KERN_INFO "ivtv%d ioctl: ", itv->num);
			v4l_printk_ioctl(cmd);
		}
		return ivtv_v4l2_ioctls(itv, filp, cmd, arg);

	case VIDIOC_QUERYMENU:
	case VIDIOC_QUERYCTRL:
	case VIDIOC_S_CTRL:
	case VIDIOC_G_CTRL:
	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_G_EXT_CTRLS:
	case VIDIOC_TRY_EXT_CTRLS:
		if (ivtv_debug & IVTV_DBGFLG_IOCTL) {
			printk(KERN_INFO "ivtv%d ioctl: ", itv->num);
			v4l_printk_ioctl(cmd);
		}
		return ivtv_control_ioctls(itv, cmd, arg);

	case IVTV_IOC_DMA_FRAME:
	case VIDEO_GET_PTS:
	case VIDEO_GET_FRAME_COUNT:
	case VIDEO_GET_EVENT:
	case VIDEO_PLAY:
	case VIDEO_STOP:
	case VIDEO_FREEZE:
	case VIDEO_CONTINUE:
	case VIDEO_COMMAND:
	case VIDEO_TRY_COMMAND:
		return ivtv_decoder_ioctls(filp, cmd, arg);

	case 0x00005401:	/* Handle isatty() calls */
		return -EINVAL;
	default:
		return v4l_compat_translate_ioctl(inode, filp, cmd, arg,
						   ivtv_v4l2_do_ioctl);
	}
	return 0;
}

int ivtv_v4l2_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		    unsigned long arg)
{
	struct ivtv_open_id *id = (struct ivtv_open_id *)filp->private_data;
	struct ivtv *itv = id->itv;

	/* Filter dvb ioctls that cannot be handled by video_usercopy */
	switch (cmd) {
	case VIDEO_SELECT_SOURCE:
		IVTV_DEBUG_IOCTL("VIDEO_SELECT_SOURCE\n");
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		return ivtv_passthrough_mode(itv, arg == VIDEO_SOURCE_DEMUX);

	case AUDIO_SET_MUTE:
		IVTV_DEBUG_IOCTL("AUDIO_SET_MUTE\n");
		itv->speed_mute_audio = arg;
		return 0;

	case AUDIO_CHANNEL_SELECT:
		IVTV_DEBUG_IOCTL("AUDIO_CHANNEL_SELECT\n");
		if (arg > AUDIO_STEREO_SWAPPED)
			return -EINVAL;
		itv->audio_stereo_mode = arg;
		ivtv_vapi(itv, CX2341X_DEC_SET_AUDIO_MODE, 2, itv->audio_bilingual_mode, itv->audio_stereo_mode);
		return 0;

	case AUDIO_BILINGUAL_CHANNEL_SELECT:
		IVTV_DEBUG_IOCTL("AUDIO_BILINGUAL_CHANNEL_SELECT\n");
		if (arg > AUDIO_STEREO_SWAPPED)
			return -EINVAL;
		itv->audio_bilingual_mode = arg;
		ivtv_vapi(itv, CX2341X_DEC_SET_AUDIO_MODE, 2, itv->audio_bilingual_mode, itv->audio_stereo_mode);
		return 0;

	default:
		break;
	}
	return video_usercopy(inode, filp, cmd, arg, ivtv_v4l2_do_ioctl);
}
