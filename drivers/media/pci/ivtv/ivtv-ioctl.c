// SPDX-License-Identifier: GPL-2.0-or-later
/*
    ioctl system call
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#include "ivtv-driver.h"
#include "ivtv-version.h"
#include "ivtv-mailbox.h"
#include "ivtv-i2c.h"
#include "ivtv-queue.h"
#include "ivtv-fileops.h"
#include "ivtv-vbi.h"
#include "ivtv-routing.h"
#include "ivtv-streams.h"
#include "ivtv-yuv.h"
#include "ivtv-ioctl.h"
#include "ivtv-gpio.h"
#include "ivtv-controls.h"
#include "ivtv-cards.h"
#include <media/i2c/saa7127.h>
#include <media/tveeprom.h>
#include <media/v4l2-event.h>

u16 ivtv_service2vbi(int type)
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
		if (BIT(i) & set)
			return BIT(i);
	}
	return 0;
}

void ivtv_expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
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

static void check_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal)
{
	int f, l;

	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++) {
			fmt->service_lines[f][l] = select_service_from_set(f, l, fmt->service_lines[f][l], is_pal);
		}
	}
}

u16 ivtv_get_service_set(struct v4l2_sliced_vbi_format *fmt)
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

void ivtv_set_osd_alpha(struct ivtv *itv)
{
	ivtv_vapi(itv, CX2341X_OSD_SET_GLOBAL_ALPHA, 3,
		itv->osd_global_alpha_state, itv->osd_global_alpha, !itv->osd_local_alpha_state);
	ivtv_vapi(itv, CX2341X_OSD_SET_CHROMA_KEY, 2, itv->osd_chroma_key_state, itv->osd_chroma_key);
}

int ivtv_set_speed(struct ivtv *itv, int speed)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	int single_step = (speed == 1 || speed == -1);
	DEFINE_WAIT(wait);

	if (speed == 0) speed = 1000;

	/* No change? */
	if (speed == itv->speed && !single_step)
		return 0;

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
	data[3] = v4l2_ctrl_g_ctrl(itv->cxhdl.video_b_frames);
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
		mutex_unlock(&itv->serialize_lock);
		prepare_to_wait(&itv->dma_waitq, &wait, TASK_INTERRUPTIBLE);
		while (test_bit(IVTV_F_I_DMA, &itv->i_flags)) {
			got_sig = signal_pending(current);
			if (got_sig)
				break;
			got_sig = 0;
			schedule();
		}
		finish_wait(&itv->dma_waitq, &wait);
		mutex_lock(&itv->serialize_lock);
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

	if (cur_speed == 0)
		cur_speed = 1000;
	if (new_speed < 0)
		new_speed = -new_speed;
	if (cur_speed < 0)
		cur_speed = -cur_speed;

	if (cur_speed <= new_speed) {
		if (new_speed > 1500)
			return fact * 2000;
		if (new_speed > 1000)
			return fact * 1500;
	}
	else {
		if (new_speed >= 2000)
			return fact * 2000;
		if (new_speed >= 1500)
			return fact * 1500;
		if (new_speed >= 1000)
			return fact * 1000;
	}
	if (new_speed == 0)
		return 1000;
	if (new_speed == 1 || new_speed == 1000)
		return fact * new_speed;

	s = new_speed;
	new_speed = 1000 / new_speed;
	if (1000 / cur_speed == new_speed)
		new_speed += (cur_speed < s) ? -1 : 1;
	if (new_speed > 60) return 1000 / (fact * 60);
	return 1000 / (fact * new_speed);
}

static int ivtv_video_command(struct ivtv *itv, struct ivtv_open_id *id,
		struct v4l2_decoder_cmd *dc, int try)
{
	struct ivtv_stream *s = &itv->streams[IVTV_DEC_STREAM_TYPE_MPG];

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;

	switch (dc->cmd) {
	case V4L2_DEC_CMD_START: {
		dc->flags &= V4L2_DEC_CMD_START_MUTE_AUDIO;
		dc->start.speed = ivtv_validate_speed(itv->speed, dc->start.speed);
		if (dc->start.speed < 0)
			dc->start.format = V4L2_DEC_START_FMT_GOP;
		else
			dc->start.format = V4L2_DEC_START_FMT_NONE;
		if (dc->start.speed != 500 && dc->start.speed != 1500)
			dc->flags = dc->start.speed == 1000 ? 0 :
					V4L2_DEC_CMD_START_MUTE_AUDIO;
		if (try) break;

		itv->speed_mute_audio = dc->flags & V4L2_DEC_CMD_START_MUTE_AUDIO;
		if (ivtv_set_output_mode(itv, OUT_MPG) != OUT_MPG)
			return -EBUSY;
		if (test_and_clear_bit(IVTV_F_I_DEC_PAUSED, &itv->i_flags)) {
			/* forces ivtv_set_speed to be called */
			itv->speed = 0;
		}
		return ivtv_start_decoding(id, dc->start.speed);
	}

	case V4L2_DEC_CMD_STOP:
		dc->flags &= V4L2_DEC_CMD_STOP_IMMEDIATELY | V4L2_DEC_CMD_STOP_TO_BLACK;
		if (dc->flags & V4L2_DEC_CMD_STOP_IMMEDIATELY)
			dc->stop.pts = 0;
		if (try) break;
		if (atomic_read(&itv->decoding) == 0)
			return 0;
		if (itv->output_mode != OUT_MPG)
			return -EBUSY;

		itv->output_mode = OUT_NONE;
		return ivtv_stop_v4l2_decode_stream(s, dc->flags, dc->stop.pts);

	case V4L2_DEC_CMD_PAUSE:
		dc->flags &= V4L2_DEC_CMD_PAUSE_TO_BLACK;
		if (try) break;
		if (!atomic_read(&itv->decoding))
			return -EPERM;
		if (itv->output_mode != OUT_MPG)
			return -EBUSY;
		if (atomic_read(&itv->decoding) > 0) {
			ivtv_vapi(itv, CX2341X_DEC_PAUSE_PLAYBACK, 1,
				(dc->flags & V4L2_DEC_CMD_PAUSE_TO_BLACK) ? 1 : 0);
			set_bit(IVTV_F_I_DEC_PAUSED, &itv->i_flags);
		}
		break;

	case V4L2_DEC_CMD_RESUME:
		dc->flags = 0;
		if (try) break;
		if (!atomic_read(&itv->decoding))
			return -EPERM;
		if (itv->output_mode != OUT_MPG)
			return -EBUSY;
		if (test_and_clear_bit(IVTV_F_I_DEC_PAUSED, &itv->i_flags)) {
			int speed = itv->speed;
			itv->speed = 0;
			return ivtv_start_decoding(id, speed);
		}
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int ivtv_g_fmt_sliced_vbi_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;
	if (!(itv->v4l2_cap & V4L2_CAP_SLICED_VBI_OUTPUT))
		return -EINVAL;
	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));
	if (itv->is_60hz) {
		vbifmt->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
		vbifmt->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
	} else {
		vbifmt->service_lines[0][23] = V4L2_SLICED_WSS_625;
		vbifmt->service_lines[0][16] = V4L2_SLICED_VPS;
	}
	vbifmt->service_set = ivtv_get_service_set(vbifmt);
	return 0;
}

static int ivtv_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;

	pixfmt->width = itv->cxhdl.width;
	pixfmt->height = itv->cxhdl.height;
	pixfmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pixfmt->field = V4L2_FIELD_INTERLACED;
	if (id->type == IVTV_ENC_STREAM_TYPE_YUV) {
		pixfmt->pixelformat = V4L2_PIX_FMT_NV12_16L16;
		/* YUV size is (Y=(h*720) + UV=(h*(720/2))) */
		pixfmt->sizeimage = pixfmt->height * 720 * 3 / 2;
		pixfmt->bytesperline = 720;
	} else {
		pixfmt->pixelformat = V4L2_PIX_FMT_MPEG;
		pixfmt->sizeimage = 128 * 1024;
		pixfmt->bytesperline = 0;
	}
	return 0;
}

static int ivtv_g_fmt_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct v4l2_vbi_format *vbifmt = &fmt->fmt.vbi;

	vbifmt->sampling_rate = 27000000;
	vbifmt->offset = 248;
	vbifmt->samples_per_line = itv->vbi.raw_decoder_line_size - 4;
	vbifmt->sample_format = V4L2_PIX_FMT_GREY;
	vbifmt->start[0] = itv->vbi.start[0];
	vbifmt->start[1] = itv->vbi.start[1];
	vbifmt->count[0] = vbifmt->count[1] = itv->vbi.count;
	vbifmt->flags = 0;
	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;
	return 0;
}

static int ivtv_g_fmt_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;

	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;
	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;

	if (id->type == IVTV_DEC_STREAM_TYPE_VBI) {
		vbifmt->service_set = itv->is_50hz ? V4L2_SLICED_VBI_625 :
			V4L2_SLICED_VBI_525;
		ivtv_expand_service_set(vbifmt, itv->is_50hz);
		vbifmt->service_set = ivtv_get_service_set(vbifmt);
		return 0;
	}

	v4l2_subdev_call(itv->sd_video, vbi, g_sliced_fmt, vbifmt);
	vbifmt->service_set = ivtv_get_service_set(vbifmt);
	return 0;
}

static int ivtv_g_fmt_vid_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;
	pixfmt->width = itv->main_rect.width;
	pixfmt->height = itv->main_rect.height;
	pixfmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pixfmt->field = V4L2_FIELD_INTERLACED;
	if (id->type == IVTV_DEC_STREAM_TYPE_YUV) {
		switch (itv->yuv_info.lace_mode & IVTV_YUV_MODE_MASK) {
		case IVTV_YUV_MODE_INTERLACED:
			pixfmt->field = (itv->yuv_info.lace_mode & IVTV_YUV_SYNC_MASK) ?
				V4L2_FIELD_INTERLACED_BT : V4L2_FIELD_INTERLACED_TB;
			break;
		case IVTV_YUV_MODE_PROGRESSIVE:
			pixfmt->field = V4L2_FIELD_NONE;
			break;
		default:
			pixfmt->field = V4L2_FIELD_ANY;
			break;
		}
		pixfmt->pixelformat = V4L2_PIX_FMT_NV12_16L16;
		pixfmt->bytesperline = 720;
		pixfmt->width = itv->yuv_info.v4l2_src_w;
		pixfmt->height = itv->yuv_info.v4l2_src_h;
		/* YUV size is (Y=(h*w) + UV=(h*(w/2))) */
		pixfmt->sizeimage =
			1080 * ((pixfmt->height + 31) & ~31);
	} else {
		pixfmt->pixelformat = V4L2_PIX_FMT_MPEG;
		pixfmt->sizeimage = 128 * 1024;
		pixfmt->bytesperline = 0;
	}
	return 0;
}

static int ivtv_g_fmt_vid_out_overlay(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];
	struct v4l2_window *winfmt = &fmt->fmt.win;

	if (!(s->vdev.device_caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
		return -EINVAL;
	if (!itv->osd_video_pbase)
		return -EINVAL;
	winfmt->chromakey = itv->osd_chroma_key;
	winfmt->global_alpha = itv->osd_global_alpha;
	winfmt->field = V4L2_FIELD_INTERLACED;
	winfmt->clips = NULL;
	winfmt->clipcount = 0;
	winfmt->bitmap = NULL;
	winfmt->w.top = winfmt->w.left = 0;
	winfmt->w.width = itv->osd_rect.width;
	winfmt->w.height = itv->osd_rect.height;
	return 0;
}

static int ivtv_try_fmt_sliced_vbi_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return ivtv_g_fmt_sliced_vbi_out(file, fh, fmt);
}

static int ivtv_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	int w = fmt->fmt.pix.width;
	int h = fmt->fmt.pix.height;
	int min_h = 2;

	w = min(w, 720);
	w = max(w, 2);
	if (id->type == IVTV_ENC_STREAM_TYPE_YUV) {
		/* YUV height must be a multiple of 32 */
		h &= ~0x1f;
		min_h = 32;
	}
	h = min(h, itv->is_50hz ? 576 : 480);
	h = max(h, min_h);
	ivtv_g_fmt_vid_cap(file, fh, fmt);
	fmt->fmt.pix.width = w;
	fmt->fmt.pix.height = h;
	return 0;
}

static int ivtv_try_fmt_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return ivtv_g_fmt_vbi_cap(file, fh, fmt);
}

static int ivtv_try_fmt_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;

	if (id->type == IVTV_DEC_STREAM_TYPE_VBI)
		return ivtv_g_fmt_sliced_vbi_cap(file, fh, fmt);

	/* set sliced VBI capture format */
	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;

	if (vbifmt->service_set)
		ivtv_expand_service_set(vbifmt, itv->is_50hz);
	check_service_set(vbifmt, itv->is_50hz);
	vbifmt->service_set = ivtv_get_service_set(vbifmt);
	return 0;
}

static int ivtv_try_fmt_vid_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv_open_id *id = fh2id(fh);
	s32 w = fmt->fmt.pix.width;
	s32 h = fmt->fmt.pix.height;
	int field = fmt->fmt.pix.field;
	int ret = ivtv_g_fmt_vid_out(file, fh, fmt);

	w = min(w, 720);
	w = max(w, 2);
	/* Why can the height be 576 even when the output is NTSC?

	   Internally the buffers of the PVR350 are always set to 720x576. The
	   decoded video frame will always be placed in the top left corner of
	   this buffer. For any video which is not 720x576, the buffer will
	   then be cropped to remove the unused right and lower areas, with
	   the remaining image being scaled by the hardware to fit the display
	   area. The video can be scaled both up and down, so a 720x480 video
	   can be displayed full-screen on PAL and a 720x576 video can be
	   displayed without cropping on NTSC.

	   Note that the scaling only occurs on the video stream, the osd
	   resolution is locked to the broadcast standard and not scaled.

	   Thanks to Ian Armstrong for this explanation. */
	h = min(h, 576);
	h = max(h, 2);
	if (id->type == IVTV_DEC_STREAM_TYPE_YUV)
		fmt->fmt.pix.field = field;
	fmt->fmt.pix.width = w;
	fmt->fmt.pix.height = h;
	return ret;
}

static int ivtv_try_fmt_vid_out_overlay(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];
	u32 chromakey = fmt->fmt.win.chromakey;
	u8 global_alpha = fmt->fmt.win.global_alpha;

	if (!(s->vdev.device_caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
		return -EINVAL;
	if (!itv->osd_video_pbase)
		return -EINVAL;
	ivtv_g_fmt_vid_out_overlay(file, fh, fmt);
	fmt->fmt.win.chromakey = chromakey;
	fmt->fmt.win.global_alpha = global_alpha;
	return 0;
}

static int ivtv_s_fmt_sliced_vbi_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return ivtv_g_fmt_sliced_vbi_out(file, fh, fmt);
}

static int ivtv_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret = ivtv_try_fmt_vid_cap(file, fh, fmt);
	int w = fmt->fmt.pix.width;
	int h = fmt->fmt.pix.height;

	if (ret)
		return ret;

	if (itv->cxhdl.width == w && itv->cxhdl.height == h)
		return 0;

	if (atomic_read(&itv->capturing) > 0)
		return -EBUSY;

	itv->cxhdl.width = w;
	itv->cxhdl.height = h;
	if (v4l2_ctrl_g_ctrl(itv->cxhdl.video_encoding) == V4L2_MPEG_VIDEO_ENCODING_MPEG_1)
		fmt->fmt.pix.width /= 2;
	format.format.width = fmt->fmt.pix.width;
	format.format.height = h;
	format.format.code = MEDIA_BUS_FMT_FIXED;
	v4l2_subdev_call(itv->sd_video, pad, set_fmt, NULL, &format);
	return ivtv_g_fmt_vid_cap(file, fh, fmt);
}

static int ivtv_s_fmt_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (!ivtv_raw_vbi(itv) && atomic_read(&itv->capturing) > 0)
		return -EBUSY;
	itv->vbi.sliced_in->service_set = 0;
	itv->vbi.in.type = V4L2_BUF_TYPE_VBI_CAPTURE;
	v4l2_subdev_call(itv->sd_video, vbi, s_raw_fmt, &fmt->fmt.vbi);
	return ivtv_g_fmt_vbi_cap(file, fh, fmt);
}

static int ivtv_s_fmt_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	int ret = ivtv_try_fmt_sliced_vbi_cap(file, fh, fmt);

	if (ret || id->type == IVTV_DEC_STREAM_TYPE_VBI)
		return ret;

	check_service_set(vbifmt, itv->is_50hz);
	if (ivtv_raw_vbi(itv) && atomic_read(&itv->capturing) > 0)
		return -EBUSY;
	itv->vbi.in.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	v4l2_subdev_call(itv->sd_video, vbi, s_sliced_fmt, vbifmt);
	memcpy(itv->vbi.sliced_in, vbifmt, sizeof(*itv->vbi.sliced_in));
	return 0;
}

static int ivtv_s_fmt_vid_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct yuv_playback_info *yi = &itv->yuv_info;
	int ret = ivtv_try_fmt_vid_out(file, fh, fmt);

	if (ret)
		return ret;

	if (id->type != IVTV_DEC_STREAM_TYPE_YUV)
		return 0;

	/* Return now if we already have some frame data */
	if (yi->stream_size)
		return -EBUSY;

	yi->v4l2_src_w = fmt->fmt.pix.width;
	yi->v4l2_src_h = fmt->fmt.pix.height;

	switch (fmt->fmt.pix.field) {
	case V4L2_FIELD_NONE:
		yi->lace_mode = IVTV_YUV_MODE_PROGRESSIVE;
		break;
	case V4L2_FIELD_ANY:
		yi->lace_mode = IVTV_YUV_MODE_AUTO;
		break;
	case V4L2_FIELD_INTERLACED_BT:
		yi->lace_mode =
			IVTV_YUV_MODE_INTERLACED|IVTV_YUV_SYNC_ODD;
		break;
	case V4L2_FIELD_INTERLACED_TB:
	default:
		yi->lace_mode = IVTV_YUV_MODE_INTERLACED;
		break;
	}
	yi->lace_sync_field = (yi->lace_mode & IVTV_YUV_SYNC_MASK) == IVTV_YUV_SYNC_EVEN ? 0 : 1;

	if (test_bit(IVTV_F_I_DEC_YUV, &itv->i_flags))
		itv->dma_data_req_size =
			1080 * ((yi->v4l2_src_h + 31) & ~31);

	return 0;
}

static int ivtv_s_fmt_vid_out_overlay(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct ivtv *itv = fh2id(fh)->itv;
	int ret = ivtv_try_fmt_vid_out_overlay(file, fh, fmt);

	if (ret == 0) {
		itv->osd_chroma_key = fmt->fmt.win.chromakey;
		itv->osd_global_alpha = fmt->fmt.win.global_alpha;
		ivtv_set_osd_alpha(itv);
	}
	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ivtv_itvc(struct ivtv *itv, bool get, u64 reg, u64 *val)
{
	volatile u8 __iomem *reg_start;

	if (reg & 0x3)
		return -EINVAL;
	if (reg >= IVTV_REG_OFFSET && reg < IVTV_REG_OFFSET + IVTV_REG_SIZE)
		reg_start = itv->reg_mem - IVTV_REG_OFFSET;
	else if (itv->has_cx23415 && reg >= IVTV_DECODER_OFFSET &&
			reg < IVTV_DECODER_OFFSET + IVTV_DECODER_SIZE)
		reg_start = itv->dec_mem - IVTV_DECODER_OFFSET;
	else if (reg < IVTV_ENCODER_SIZE)
		reg_start = itv->enc_mem;
	else
		return -EINVAL;

	if (get)
		*val = readl(reg + reg_start);
	else
		writel(*val, reg + reg_start);
	return 0;
}

static int ivtv_g_register(struct file *file, void *fh, struct v4l2_dbg_register *reg)
{
	struct ivtv *itv = fh2id(fh)->itv;

	reg->size = 4;
	return ivtv_itvc(itv, true, reg->reg, &reg->val);
}

static int ivtv_s_register(struct file *file, void *fh, const struct v4l2_dbg_register *reg)
{
	struct ivtv *itv = fh2id(fh)->itv;
	u64 val = reg->val;

	return ivtv_itvc(itv, false, reg->reg, &val);
}
#endif

static int ivtv_querycap(struct file *file, void *fh, struct v4l2_capability *vcap)
{
	struct ivtv_open_id *id = fh2id(file->private_data);
	struct ivtv *itv = id->itv;

	strscpy(vcap->driver, IVTV_DRIVER_NAME, sizeof(vcap->driver));
	strscpy(vcap->card, itv->card_name, sizeof(vcap->card));
	snprintf(vcap->bus_info, sizeof(vcap->bus_info), "PCI:%s", pci_name(itv->pdev));
	vcap->capabilities = itv->v4l2_cap | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int ivtv_enumaudio(struct file *file, void *fh, struct v4l2_audio *vin)
{
	struct ivtv *itv = fh2id(fh)->itv;

	return ivtv_get_audio_input(itv, vin->index, vin);
}

static int ivtv_g_audio(struct file *file, void *fh, struct v4l2_audio *vin)
{
	struct ivtv *itv = fh2id(fh)->itv;

	vin->index = itv->audio_input;
	return ivtv_get_audio_input(itv, vin->index, vin);
}

static int ivtv_s_audio(struct file *file, void *fh, const struct v4l2_audio *vout)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (vout->index >= itv->nof_audio_inputs)
		return -EINVAL;

	itv->audio_input = vout->index;
	ivtv_audio_set_io(itv);

	return 0;
}

static int ivtv_enumaudout(struct file *file, void *fh, struct v4l2_audioout *vin)
{
	struct ivtv *itv = fh2id(fh)->itv;

	/* set it to defaults from our table */
	return ivtv_get_audio_output(itv, vin->index, vin);
}

static int ivtv_g_audout(struct file *file, void *fh, struct v4l2_audioout *vin)
{
	struct ivtv *itv = fh2id(fh)->itv;

	vin->index = 0;
	return ivtv_get_audio_output(itv, vin->index, vin);
}

static int ivtv_s_audout(struct file *file, void *fh, const struct v4l2_audioout *vout)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (itv->card->video_outputs == NULL || vout->index != 0)
		return -EINVAL;
	return 0;
}

static int ivtv_enum_input(struct file *file, void *fh, struct v4l2_input *vin)
{
	struct ivtv *itv = fh2id(fh)->itv;

	/* set it to defaults from our table */
	return ivtv_get_input(itv, vin->index, vin);
}

static int ivtv_enum_output(struct file *file, void *fh, struct v4l2_output *vout)
{
	struct ivtv *itv = fh2id(fh)->itv;

	return ivtv_get_output(itv, vout->index, vout);
}

static int ivtv_g_pixelaspect(struct file *file, void *fh,
			      int type, struct v4l2_fract *f)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		f->numerator = itv->is_50hz ? 54 : 11;
		f->denominator = itv->is_50hz ? 59 : 10;
	} else if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		f->numerator = itv->is_out_50hz ? 54 : 11;
		f->denominator = itv->is_out_50hz ? 59 : 10;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int ivtv_s_selection(struct file *file, void *fh,
			    struct v4l2_selection *sel)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct yuv_playback_info *yi = &itv->yuv_info;
	struct v4l2_rect r = { 0, 0, 720, 0 };
	int streamtype = id->type;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    !(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;


	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    !(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;

	r.height = itv->is_out_50hz ? 576 : 480;
	if (streamtype == IVTV_DEC_STREAM_TYPE_YUV && yi->track_osd) {
		r.width = yi->osd_full_w;
		r.height = yi->osd_full_h;
	}
	sel->r.width = clamp(sel->r.width, 16U, r.width);
	sel->r.height = clamp(sel->r.height, 16U, r.height);
	sel->r.left = clamp_t(unsigned, sel->r.left, 0, r.width - sel->r.width);
	sel->r.top = clamp_t(unsigned, sel->r.top, 0, r.height - sel->r.height);

	if (streamtype == IVTV_DEC_STREAM_TYPE_YUV) {
		yi->main_rect = sel->r;
		return 0;
	}
	if (!ivtv_vapi(itv, CX2341X_OSD_SET_FRAMEBUFFER_WINDOW, 4,
			sel->r.width, sel->r.height, sel->r.left, sel->r.top)) {
		itv->main_rect = sel->r;
		return 0;
	}
	return -EINVAL;
}

static int ivtv_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *sel)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct yuv_playback_info *yi = &itv->yuv_info;
	struct v4l2_rect r = { 0, 0, 720, 0 };
	int streamtype = id->type;

	if (sel->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		switch (sel->target) {
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP_BOUNDS:
			sel->r.top = sel->r.left = 0;
			sel->r.width = 720;
			sel->r.height = itv->is_50hz ? 576 : 480;
			return 0;
		default:
			return -EINVAL;
		}
	}

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    !(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE:
		if (streamtype == IVTV_DEC_STREAM_TYPE_YUV)
			sel->r = yi->main_rect;
		else
			sel->r = itv->main_rect;
		return 0;
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		r.height = itv->is_out_50hz ? 576 : 480;
		if (streamtype == IVTV_DEC_STREAM_TYPE_YUV && yi->track_osd) {
			r.width = yi->osd_full_w;
			r.height = yi->osd_full_h;
		}
		sel->r = r;
		return 0;
	}
	return -EINVAL;
}

static int ivtv_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *fmt)
{
	static const struct v4l2_fmtdesc hm12 = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.description = "HM12 (YUV 4:2:0)",
		.pixelformat = V4L2_PIX_FMT_NV12_16L16,
	};
	static const struct v4l2_fmtdesc mpeg = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags = V4L2_FMT_FLAG_COMPRESSED,
		.description = "MPEG",
		.pixelformat = V4L2_PIX_FMT_MPEG,
	};
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];

	if (fmt->index)
		return -EINVAL;
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG)
		*fmt = mpeg;
	else if (s->type == IVTV_ENC_STREAM_TYPE_YUV)
		*fmt = hm12;
	else
		return -EINVAL;
	return 0;
}

static int ivtv_enum_fmt_vid_out(struct file *file, void *fh, struct v4l2_fmtdesc *fmt)
{
	static const struct v4l2_fmtdesc hm12 = {
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
		.description = "HM12 (YUV 4:2:0)",
		.pixelformat = V4L2_PIX_FMT_NV12_16L16,
	};
	static const struct v4l2_fmtdesc mpeg = {
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
		.flags = V4L2_FMT_FLAG_COMPRESSED,
		.description = "MPEG",
		.pixelformat = V4L2_PIX_FMT_MPEG,
	};
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];

	if (fmt->index)
		return -EINVAL;
	if (s->type == IVTV_DEC_STREAM_TYPE_MPG)
		*fmt = mpeg;
	else if (s->type == IVTV_DEC_STREAM_TYPE_YUV)
		*fmt = hm12;
	else
		return -EINVAL;
	return 0;
}

static int ivtv_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct ivtv *itv = fh2id(fh)->itv;

	*i = itv->active_input;

	return 0;
}

int ivtv_s_input(struct file *file, void *fh, unsigned int inp)
{
	struct ivtv *itv = fh2id(fh)->itv;
	v4l2_std_id std;
	int i;

	if (inp >= itv->nof_inputs)
		return -EINVAL;

	if (inp == itv->active_input) {
		IVTV_DEBUG_INFO("Input unchanged\n");
		return 0;
	}

	if (atomic_read(&itv->capturing) > 0) {
		return -EBUSY;
	}

	IVTV_DEBUG_INFO("Changing input from %d to %d\n",
			itv->active_input, inp);

	itv->active_input = inp;
	/* Set the audio input to whatever is appropriate for the
	   input type. */
	itv->audio_input = itv->card->video_inputs[inp].audio_index;

	if (itv->card->video_inputs[inp].video_type == IVTV_CARD_INPUT_VID_TUNER)
		std = itv->tuner_std;
	else
		std = V4L2_STD_ALL;
	for (i = 0; i <= IVTV_ENC_STREAM_TYPE_VBI; i++)
		itv->streams[i].vdev.tvnorms = std;

	/* prevent others from messing with the streams until
	   we're finished changing inputs. */
	ivtv_mute(itv);
	ivtv_video_set_io(itv);
	ivtv_audio_set_io(itv);
	ivtv_unmute(itv);

	return 0;
}

static int ivtv_g_output(struct file *file, void *fh, unsigned int *i)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return -EINVAL;

	*i = itv->active_output;

	return 0;
}

static int ivtv_s_output(struct file *file, void *fh, unsigned int outp)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (outp >= itv->card->nof_outputs)
		return -EINVAL;

	if (outp == itv->active_output) {
		IVTV_DEBUG_INFO("Output unchanged\n");
		return 0;
	}
	IVTV_DEBUG_INFO("Changing output from %d to %d\n",
		   itv->active_output, outp);

	itv->active_output = outp;
	ivtv_call_hw(itv, IVTV_HW_SAA7127, video, s_routing,
			SAA7127_INPUT_TYPE_NORMAL,
			itv->card->video_outputs[outp].video_output, 0);

	return 0;
}

static int ivtv_g_frequency(struct file *file, void *fh, struct v4l2_frequency *vf)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];

	if (s->vdev.vfl_dir)
		return -ENOTTY;
	if (vf->tuner != 0)
		return -EINVAL;

	ivtv_call_all(itv, tuner, g_frequency, vf);
	return 0;
}

int ivtv_s_frequency(struct file *file, void *fh, const struct v4l2_frequency *vf)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];

	if (s->vdev.vfl_dir)
		return -ENOTTY;
	if (vf->tuner != 0)
		return -EINVAL;

	ivtv_mute(itv);
	IVTV_DEBUG_INFO("v4l2 ioctl: set frequency %d\n", vf->frequency);
	ivtv_call_all(itv, tuner, s_frequency, vf);
	ivtv_unmute(itv);
	return 0;
}

static int ivtv_g_std(struct file *file, void *fh, v4l2_std_id *std)
{
	struct ivtv *itv = fh2id(fh)->itv;

	*std = itv->std;
	return 0;
}

void ivtv_s_std_enc(struct ivtv *itv, v4l2_std_id std)
{
	itv->std = std;
	itv->is_60hz = (std & V4L2_STD_525_60) ? 1 : 0;
	itv->is_50hz = !itv->is_60hz;
	cx2341x_handler_set_50hz(&itv->cxhdl, itv->is_50hz);
	itv->cxhdl.width = 720;
	itv->cxhdl.height = itv->is_50hz ? 576 : 480;
	itv->vbi.count = itv->is_50hz ? 18 : 12;
	itv->vbi.start[0] = itv->is_50hz ? 6 : 10;
	itv->vbi.start[1] = itv->is_50hz ? 318 : 273;

	if (itv->hw_flags & IVTV_HW_CX25840)
		itv->vbi.sliced_decoder_line_size = itv->is_60hz ? 272 : 284;

	/* Tuner */
	ivtv_call_all(itv, video, s_std, itv->std);
}

void ivtv_s_std_dec(struct ivtv *itv, v4l2_std_id std)
{
	struct yuv_playback_info *yi = &itv->yuv_info;
	DEFINE_WAIT(wait);
	int f;

	/* set display standard */
	itv->std_out = std;
	itv->is_out_60hz = (std & V4L2_STD_525_60) ? 1 : 0;
	itv->is_out_50hz = !itv->is_out_60hz;
	ivtv_call_all(itv, video, s_std_output, itv->std_out);

	/*
	 * The next firmware call is time sensitive. Time it to
	 * avoid risk of a hard lock, by trying to ensure the call
	 * happens within the first 100 lines of the top field.
	 * Make 4 attempts to sync to the decoder before giving up.
	 */
	mutex_unlock(&itv->serialize_lock);
	for (f = 0; f < 4; f++) {
		prepare_to_wait(&itv->vsync_waitq, &wait,
				TASK_UNINTERRUPTIBLE);
		if ((read_reg(IVTV_REG_DEC_LINE_FIELD) >> 16) < 100)
			break;
		schedule_timeout(msecs_to_jiffies(25));
	}
	finish_wait(&itv->vsync_waitq, &wait);
	mutex_lock(&itv->serialize_lock);

	if (f == 4)
		IVTV_WARN("Mode change failed to sync to decoder\n");

	ivtv_vapi(itv, CX2341X_DEC_SET_STANDARD, 1, itv->is_out_50hz);
	itv->main_rect.left = 0;
	itv->main_rect.top = 0;
	itv->main_rect.width = 720;
	itv->main_rect.height = itv->is_out_50hz ? 576 : 480;
	ivtv_vapi(itv, CX2341X_OSD_SET_FRAMEBUFFER_WINDOW, 4,
		720, itv->main_rect.height, 0, 0);
	yi->main_rect = itv->main_rect;
	if (!itv->osd_info) {
		yi->osd_full_w = 720;
		yi->osd_full_h = itv->is_out_50hz ? 576 : 480;
	}
}

static int ivtv_s_std(struct file *file, void *fh, v4l2_std_id std)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if ((std & V4L2_STD_ALL) == 0)
		return -EINVAL;

	if (std == itv->std)
		return 0;

	if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags) ||
	    atomic_read(&itv->capturing) > 0 ||
	    atomic_read(&itv->decoding) > 0) {
		/* Switching standard would mess with already running
		   streams, prevent that by returning EBUSY. */
		return -EBUSY;
	}

	IVTV_DEBUG_INFO("Switching standard to %llx.\n",
		(unsigned long long)itv->std);

	ivtv_s_std_enc(itv, std);
	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT)
		ivtv_s_std_dec(itv, std);

	return 0;
}

static int ivtv_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *vt)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;

	if (vt->index != 0)
		return -EINVAL;

	ivtv_call_all(itv, tuner, s_tuner, vt);

	return 0;
}

static int ivtv_g_tuner(struct file *file, void *fh, struct v4l2_tuner *vt)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (vt->index != 0)
		return -EINVAL;

	ivtv_call_all(itv, tuner, g_tuner, vt);

	if (vt->type == V4L2_TUNER_RADIO)
		strscpy(vt->name, "ivtv Radio Tuner", sizeof(vt->name));
	else
		strscpy(vt->name, "ivtv TV Tuner", sizeof(vt->name));
	return 0;
}

static int ivtv_g_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_sliced_vbi_cap *cap)
{
	struct ivtv *itv = fh2id(fh)->itv;
	int set = itv->is_50hz ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525;
	int f, l;

	if (cap->type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) {
		for (f = 0; f < 2; f++) {
			for (l = 0; l < 24; l++) {
				if (valid_service_line(f, l, itv->is_50hz))
					cap->service_lines[f][l] = set;
			}
		}
	} else if (cap->type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT) {
		if (!(itv->v4l2_cap & V4L2_CAP_SLICED_VBI_OUTPUT))
			return -EINVAL;
		if (itv->is_60hz) {
			cap->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
			cap->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
		} else {
			cap->service_lines[0][23] = V4L2_SLICED_WSS_625;
			cap->service_lines[0][16] = V4L2_SLICED_VPS;
		}
	} else {
		return -EINVAL;
	}

	set = 0;
	for (f = 0; f < 2; f++)
		for (l = 0; l < 24; l++)
			set |= cap->service_lines[f][l];
	cap->service_set = set;
	return 0;
}

static int ivtv_g_enc_index(struct file *file, void *fh, struct v4l2_enc_idx *idx)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct v4l2_enc_idx_entry *e = idx->entry;
	int entries;
	int i;

	entries = (itv->pgm_info_write_idx + IVTV_MAX_PGM_INDEX - itv->pgm_info_read_idx) %
				IVTV_MAX_PGM_INDEX;
	if (entries > V4L2_ENC_IDX_ENTRIES)
		entries = V4L2_ENC_IDX_ENTRIES;
	idx->entries = 0;
	idx->entries_cap = IVTV_MAX_PGM_INDEX;
	if (!atomic_read(&itv->capturing))
		return 0;
	for (i = 0; i < entries; i++) {
		*e = itv->pgm_info[(itv->pgm_info_read_idx + i) % IVTV_MAX_PGM_INDEX];
		if ((e->flags & V4L2_ENC_IDX_FRAME_MASK) <= V4L2_ENC_IDX_FRAME_B) {
			idx->entries++;
			e++;
		}
	}
	itv->pgm_info_read_idx = (itv->pgm_info_read_idx + idx->entries) % IVTV_MAX_PGM_INDEX;
	return 0;
}

static int ivtv_encoder_cmd(struct file *file, void *fh, struct v4l2_encoder_cmd *enc)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;


	switch (enc->cmd) {
	case V4L2_ENC_CMD_START:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_START\n");
		enc->flags = 0;
		return ivtv_start_capture(id);

	case V4L2_ENC_CMD_STOP:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_STOP\n");
		enc->flags &= V4L2_ENC_CMD_STOP_AT_GOP_END;
		ivtv_stop_capture(id, enc->flags & V4L2_ENC_CMD_STOP_AT_GOP_END);
		return 0;

	case V4L2_ENC_CMD_PAUSE:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_PAUSE\n");
		enc->flags = 0;

		if (!atomic_read(&itv->capturing))
			return -EPERM;
		if (test_and_set_bit(IVTV_F_I_ENC_PAUSED, &itv->i_flags))
			return 0;

		ivtv_mute(itv);
		ivtv_vapi(itv, CX2341X_ENC_PAUSE_ENCODER, 1, 0);
		break;

	case V4L2_ENC_CMD_RESUME:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_RESUME\n");
		enc->flags = 0;

		if (!atomic_read(&itv->capturing))
			return -EPERM;

		if (!test_and_clear_bit(IVTV_F_I_ENC_PAUSED, &itv->i_flags))
			return 0;

		ivtv_vapi(itv, CX2341X_ENC_PAUSE_ENCODER, 1, 1);
		ivtv_unmute(itv);
		break;
	default:
		IVTV_DEBUG_IOCTL("Unknown cmd %d\n", enc->cmd);
		return -EINVAL;
	}

	return 0;
}

static int ivtv_try_encoder_cmd(struct file *file, void *fh, struct v4l2_encoder_cmd *enc)
{
	struct ivtv *itv = fh2id(fh)->itv;

	switch (enc->cmd) {
	case V4L2_ENC_CMD_START:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_START\n");
		enc->flags = 0;
		return 0;

	case V4L2_ENC_CMD_STOP:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_STOP\n");
		enc->flags &= V4L2_ENC_CMD_STOP_AT_GOP_END;
		return 0;

	case V4L2_ENC_CMD_PAUSE:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_PAUSE\n");
		enc->flags = 0;
		return 0;

	case V4L2_ENC_CMD_RESUME:
		IVTV_DEBUG_IOCTL("V4L2_ENC_CMD_RESUME\n");
		enc->flags = 0;
		return 0;
	default:
		IVTV_DEBUG_IOCTL("Unknown cmd %d\n", enc->cmd);
		return -EINVAL;
	}
}

static int ivtv_g_fbuf(struct file *file, void *fh, struct v4l2_framebuffer *fb)
{
	struct ivtv *itv = fh2id(fh)->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct yuv_playback_info *yi = &itv->yuv_info;

	int pixfmt;
	static u32 pixel_format[16] = {
		V4L2_PIX_FMT_PAL8, /* Uses a 256-entry RGB colormap */
		V4L2_PIX_FMT_RGB565,
		V4L2_PIX_FMT_RGB555,
		V4L2_PIX_FMT_RGB444,
		V4L2_PIX_FMT_RGB32,
		0,
		0,
		0,
		V4L2_PIX_FMT_PAL8, /* Uses a 256-entry YUV colormap */
		V4L2_PIX_FMT_YUV565,
		V4L2_PIX_FMT_YUV555,
		V4L2_PIX_FMT_YUV444,
		V4L2_PIX_FMT_YUV32,
		0,
		0,
		0,
	};

	if (!(s->vdev.device_caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
		return -ENOTTY;
	if (!itv->osd_video_pbase)
		return -ENOTTY;

	fb->capability = V4L2_FBUF_CAP_EXTERNOVERLAY | V4L2_FBUF_CAP_CHROMAKEY |
		V4L2_FBUF_CAP_GLOBAL_ALPHA;

	ivtv_vapi_result(itv, data, CX2341X_OSD_GET_STATE, 0);
	data[0] |= (read_reg(0x2a00) >> 7) & 0x40;
	pixfmt = (data[0] >> 3) & 0xf;

	fb->fmt.pixelformat = pixel_format[pixfmt];
	fb->fmt.width = itv->osd_rect.width;
	fb->fmt.height = itv->osd_rect.height;
	fb->fmt.field = V4L2_FIELD_INTERLACED;
	fb->fmt.bytesperline = fb->fmt.width;
	fb->fmt.colorspace = V4L2_COLORSPACE_SMPTE170M;
	fb->fmt.field = V4L2_FIELD_INTERLACED;
	if (fb->fmt.pixelformat != V4L2_PIX_FMT_PAL8)
		fb->fmt.bytesperline *= 2;
	if (fb->fmt.pixelformat == V4L2_PIX_FMT_RGB32 ||
	    fb->fmt.pixelformat == V4L2_PIX_FMT_YUV32)
		fb->fmt.bytesperline *= 2;
	fb->fmt.sizeimage = fb->fmt.bytesperline * fb->fmt.height;
	fb->base = (void *)itv->osd_video_pbase;
	fb->flags = 0;

	if (itv->osd_chroma_key_state)
		fb->flags |= V4L2_FBUF_FLAG_CHROMAKEY;

	if (itv->osd_global_alpha_state)
		fb->flags |= V4L2_FBUF_FLAG_GLOBAL_ALPHA;

	if (yi->track_osd)
		fb->flags |= V4L2_FBUF_FLAG_OVERLAY;

	pixfmt &= 7;

	/* no local alpha for RGB565 or unknown formats */
	if (pixfmt == 1 || pixfmt > 4)
		return 0;

	/* 16-bit formats have inverted local alpha */
	if (pixfmt == 2 || pixfmt == 3)
		fb->capability |= V4L2_FBUF_CAP_LOCAL_INV_ALPHA;
	else
		fb->capability |= V4L2_FBUF_CAP_LOCAL_ALPHA;

	if (itv->osd_local_alpha_state) {
		/* 16-bit formats have inverted local alpha */
		if (pixfmt == 2 || pixfmt == 3)
			fb->flags |= V4L2_FBUF_FLAG_LOCAL_INV_ALPHA;
		else
			fb->flags |= V4L2_FBUF_FLAG_LOCAL_ALPHA;
	}

	return 0;
}

static int ivtv_s_fbuf(struct file *file, void *fh, const struct v4l2_framebuffer *fb)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];
	struct yuv_playback_info *yi = &itv->yuv_info;

	if (!(s->vdev.device_caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
		return -ENOTTY;
	if (!itv->osd_video_pbase)
		return -ENOTTY;

	itv->osd_global_alpha_state = (fb->flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) != 0;
	itv->osd_local_alpha_state =
		(fb->flags & (V4L2_FBUF_FLAG_LOCAL_ALPHA|V4L2_FBUF_FLAG_LOCAL_INV_ALPHA)) != 0;
	itv->osd_chroma_key_state = (fb->flags & V4L2_FBUF_FLAG_CHROMAKEY) != 0;
	ivtv_set_osd_alpha(itv);
	yi->track_osd = (fb->flags & V4L2_FBUF_FLAG_OVERLAY) != 0;
	return 0;
}

static int ivtv_overlay(struct file *file, void *fh, unsigned int on)
{
	struct ivtv_open_id *id = fh2id(fh);
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[fh2id(fh)->type];

	if (!(s->vdev.device_caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY))
		return -ENOTTY;
	if (!itv->osd_video_pbase)
		return -ENOTTY;

	ivtv_vapi(itv, CX2341X_OSD_SET_STATE, 1, on != 0);

	return 0;
}

static int ivtv_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_VSYNC:
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static int ivtv_log_status(struct file *file, void *fh)
{
	struct ivtv *itv = fh2id(fh)->itv;
	u32 data[CX2341X_MBOX_MAX_DATA];

	int has_output = itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT;
	struct v4l2_input vidin;
	struct v4l2_audio audin;
	int i;

	IVTV_INFO("Version: %s Card: %s\n", IVTV_VERSION, itv->card_name);
	if (itv->hw_flags & IVTV_HW_TVEEPROM) {
		struct tveeprom tv;

		ivtv_read_eeprom(itv, &tv);
	}
	ivtv_call_all(itv, core, log_status);
	ivtv_get_input(itv, itv->active_input, &vidin);
	ivtv_get_audio_input(itv, itv->audio_input, &audin);
	IVTV_INFO("Video Input:  %s\n", vidin.name);
	IVTV_INFO("Audio Input:  %s%s\n", audin.name,
		itv->dualwatch_stereo_mode == V4L2_MPEG_AUDIO_MODE_DUAL ?
			" (Bilingual)" : "");
	if (has_output) {
		struct v4l2_output vidout;
		struct v4l2_audioout audout;
		int mode = itv->output_mode;
		static const char * const output_modes[5] = {
			"None",
			"MPEG Streaming",
			"YUV Streaming",
			"YUV Frames",
			"Passthrough",
		};
		static const char * const alpha_mode[4] = {
			"None",
			"Global",
			"Local",
			"Global and Local"
		};
		static const char * const pixel_format[16] = {
			"ARGB Indexed",
			"RGB 5:6:5",
			"ARGB 1:5:5:5",
			"ARGB 1:4:4:4",
			"ARGB 8:8:8:8",
			"5",
			"6",
			"7",
			"AYUV Indexed",
			"YUV 5:6:5",
			"AYUV 1:5:5:5",
			"AYUV 1:4:4:4",
			"AYUV 8:8:8:8",
			"13",
			"14",
			"15",
		};

		ivtv_get_output(itv, itv->active_output, &vidout);
		ivtv_get_audio_output(itv, 0, &audout);
		IVTV_INFO("Video Output: %s\n", vidout.name);
		if (mode < 0 || mode > OUT_PASSTHROUGH)
			mode = OUT_NONE;
		IVTV_INFO("Output Mode:  %s\n", output_modes[mode]);
		ivtv_vapi_result(itv, data, CX2341X_OSD_GET_STATE, 0);
		data[0] |= (read_reg(0x2a00) >> 7) & 0x40;
		IVTV_INFO("Overlay:      %s, Alpha: %s, Pixel Format: %s\n",
			data[0] & 1 ? "On" : "Off",
			alpha_mode[(data[0] >> 1) & 0x3],
			pixel_format[(data[0] >> 3) & 0xf]);
	}
	IVTV_INFO("Tuner:  %s\n",
		test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags) ? "Radio" : "TV");
	v4l2_ctrl_handler_log_status(&itv->cxhdl.hdl, itv->v4l2_dev.name);
	IVTV_INFO("Status flags:    0x%08lx\n", itv->i_flags);
	for (i = 0; i < IVTV_MAX_STREAMS; i++) {
		struct ivtv_stream *s = &itv->streams[i];

		if (s->vdev.v4l2_dev == NULL || s->buffers == 0)
			continue;
		IVTV_INFO("Stream %s: status 0x%04lx, %d%% of %d KiB (%d buffers) in use\n", s->name, s->s_flags,
				(s->buffers - s->q_free.buffers) * 100 / s->buffers,
				(s->buffers * s->buf_size) / 1024, s->buffers);
	}

	IVTV_INFO("Read MPG/VBI: %lld/%lld bytes\n",
			(long long)itv->mpg_data_received,
			(long long)itv->vbi_data_inserted);
	return 0;
}

static int ivtv_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *dec)
{
	struct ivtv_open_id *id = fh2id(file->private_data);
	struct ivtv *itv = id->itv;

	IVTV_DEBUG_IOCTL("VIDIOC_DECODER_CMD %d\n", dec->cmd);
	return ivtv_video_command(itv, id, dec, false);
}

static int ivtv_try_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *dec)
{
	struct ivtv_open_id *id = fh2id(file->private_data);
	struct ivtv *itv = id->itv;

	IVTV_DEBUG_IOCTL("VIDIOC_TRY_DECODER_CMD %d\n", dec->cmd);
	return ivtv_video_command(itv, id, dec, true);
}

static int ivtv_decoder_ioctls(struct file *filp, unsigned int cmd, void *arg)
{
	struct ivtv_open_id *id = fh2id(filp->private_data);
	struct ivtv *itv = id->itv;
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
		if (ivtv_start_decoding(id, id->type)) {
			return -EBUSY;
		}
		if (ivtv_set_output_mode(itv, OUT_UDMA_YUV) != OUT_UDMA_YUV) {
			ivtv_release_stream(s);
			return -EBUSY;
		}
		/* Mark that this file handle started the UDMA_YUV mode */
		id->yuv_frames = 1;
		if (args->y_source == NULL)
			return 0;
		return ivtv_yuv_prep_frame(itv, args);
	}

	case IVTV_IOC_PASSTHROUGH_MODE:
		IVTV_DEBUG_IOCTL("IVTV_IOC_PASSTHROUGH_MODE\n");
		if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
			return -EINVAL;
		return ivtv_passthrough_mode(itv, *(int *)arg != 0);
	default:
		return -EINVAL;
	}
	return 0;
}

static long ivtv_default(struct file *file, void *fh, bool valid_prio,
			 unsigned int cmd, void *arg)
{
	struct ivtv *itv = fh2id(fh)->itv;

	if (!valid_prio) {
		switch (cmd) {
		case IVTV_IOC_PASSTHROUGH_MODE:
			return -EBUSY;
		}
	}

	switch (cmd) {
	case VIDIOC_INT_RESET: {
		u32 val = *(u32 *)arg;

		if ((val == 0 && itv->options.newi2c) || (val & 0x01))
			ivtv_reset_ir_gpio(itv);
		if (val & 0x02)
			v4l2_subdev_call(itv->sd_video, core, reset, 0);
		break;
	}

	case IVTV_IOC_DMA_FRAME:
	case IVTV_IOC_PASSTHROUGH_MODE:
		return ivtv_decoder_ioctls(file, cmd, (void *)arg);

	default:
		return -ENOTTY;
	}
	return 0;
}

static const struct v4l2_ioctl_ops ivtv_ioctl_ops = {
	.vidioc_querycap		    = ivtv_querycap,
	.vidioc_s_audio			    = ivtv_s_audio,
	.vidioc_g_audio			    = ivtv_g_audio,
	.vidioc_enumaudio		    = ivtv_enumaudio,
	.vidioc_s_audout		    = ivtv_s_audout,
	.vidioc_g_audout		    = ivtv_g_audout,
	.vidioc_enum_input		    = ivtv_enum_input,
	.vidioc_enum_output		    = ivtv_enum_output,
	.vidioc_enumaudout		    = ivtv_enumaudout,
	.vidioc_g_pixelaspect		    = ivtv_g_pixelaspect,
	.vidioc_s_selection		    = ivtv_s_selection,
	.vidioc_g_selection		    = ivtv_g_selection,
	.vidioc_g_input			    = ivtv_g_input,
	.vidioc_s_input			    = ivtv_s_input,
	.vidioc_g_output		    = ivtv_g_output,
	.vidioc_s_output		    = ivtv_s_output,
	.vidioc_g_frequency		    = ivtv_g_frequency,
	.vidioc_s_frequency		    = ivtv_s_frequency,
	.vidioc_s_tuner			    = ivtv_s_tuner,
	.vidioc_g_tuner			    = ivtv_g_tuner,
	.vidioc_g_enc_index		    = ivtv_g_enc_index,
	.vidioc_g_fbuf			    = ivtv_g_fbuf,
	.vidioc_s_fbuf			    = ivtv_s_fbuf,
	.vidioc_g_std			    = ivtv_g_std,
	.vidioc_s_std			    = ivtv_s_std,
	.vidioc_overlay			    = ivtv_overlay,
	.vidioc_log_status		    = ivtv_log_status,
	.vidioc_enum_fmt_vid_cap	    = ivtv_enum_fmt_vid_cap,
	.vidioc_encoder_cmd		    = ivtv_encoder_cmd,
	.vidioc_try_encoder_cmd		    = ivtv_try_encoder_cmd,
	.vidioc_decoder_cmd		    = ivtv_decoder_cmd,
	.vidioc_try_decoder_cmd		    = ivtv_try_decoder_cmd,
	.vidioc_enum_fmt_vid_out	    = ivtv_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_cap		    = ivtv_g_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap		    = ivtv_g_fmt_vbi_cap,
	.vidioc_g_fmt_sliced_vbi_cap        = ivtv_g_fmt_sliced_vbi_cap,
	.vidioc_g_fmt_vid_out               = ivtv_g_fmt_vid_out,
	.vidioc_g_fmt_vid_out_overlay       = ivtv_g_fmt_vid_out_overlay,
	.vidioc_g_fmt_sliced_vbi_out        = ivtv_g_fmt_sliced_vbi_out,
	.vidioc_s_fmt_vid_cap		    = ivtv_s_fmt_vid_cap,
	.vidioc_s_fmt_vbi_cap		    = ivtv_s_fmt_vbi_cap,
	.vidioc_s_fmt_sliced_vbi_cap        = ivtv_s_fmt_sliced_vbi_cap,
	.vidioc_s_fmt_vid_out               = ivtv_s_fmt_vid_out,
	.vidioc_s_fmt_vid_out_overlay       = ivtv_s_fmt_vid_out_overlay,
	.vidioc_s_fmt_sliced_vbi_out        = ivtv_s_fmt_sliced_vbi_out,
	.vidioc_try_fmt_vid_cap		    = ivtv_try_fmt_vid_cap,
	.vidioc_try_fmt_vbi_cap		    = ivtv_try_fmt_vbi_cap,
	.vidioc_try_fmt_sliced_vbi_cap      = ivtv_try_fmt_sliced_vbi_cap,
	.vidioc_try_fmt_vid_out		    = ivtv_try_fmt_vid_out,
	.vidioc_try_fmt_vid_out_overlay     = ivtv_try_fmt_vid_out_overlay,
	.vidioc_try_fmt_sliced_vbi_out	    = ivtv_try_fmt_sliced_vbi_out,
	.vidioc_g_sliced_vbi_cap	    = ivtv_g_sliced_vbi_cap,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register		    = ivtv_g_register,
	.vidioc_s_register		    = ivtv_s_register,
#endif
	.vidioc_default			    = ivtv_default,
	.vidioc_subscribe_event		    = ivtv_subscribe_event,
	.vidioc_unsubscribe_event	    = v4l2_event_unsubscribe,
};

void ivtv_set_funcs(struct video_device *vdev)
{
	vdev->ioctl_ops = &ivtv_ioctl_ops;
}
