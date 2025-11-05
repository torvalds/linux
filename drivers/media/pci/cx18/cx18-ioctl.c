// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 ioctl system call
 *
 *  Derived from ivtv-ioctl.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#include "cx18-driver.h"
#include "cx18-io.h"
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
#include <media/v4l2-event.h>

static const struct v4l2_fmtdesc cx18_formats_yuv[] = {
	{
		.index = 0,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.pixelformat = V4L2_PIX_FMT_NV12_16L16,
	},
	{
		.index = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.pixelformat = V4L2_PIX_FMT_UYVY,
	},
};

static const struct v4l2_fmtdesc cx18_formats_mpeg[] = {
	{
		.index = 0,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags = V4L2_FMT_FLAG_COMPRESSED,
		.pixelformat = V4L2_PIX_FMT_MPEG,
	},
};

static int cx18_g_fmt_vid_cap(struct file *file, void *fh,
			      struct v4l2_format *fmt)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[id->type];
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;

	pixfmt->width = cx->cxhdl.width;
	pixfmt->height = cx->cxhdl.height;
	pixfmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pixfmt->field = V4L2_FIELD_INTERLACED;
	if (id->type == CX18_ENC_STREAM_TYPE_YUV) {
		pixfmt->pixelformat = s->pixelformat;
		pixfmt->sizeimage = s->vb_bytes_per_frame;
		pixfmt->bytesperline = s->vb_bytes_per_line;
	} else {
		pixfmt->pixelformat = V4L2_PIX_FMT_MPEG;
		pixfmt->sizeimage = 128 * 1024;
		pixfmt->bytesperline = 0;
	}
	return 0;
}

static int cx18_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *fmt)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
	int w = pixfmt->width;
	int h = pixfmt->height;

	w = min(w, 720);
	w = max(w, 720 / 16);

	h = min(h, cx->is_50hz ? 576 : 480);
	h = max(h, (cx->is_50hz ? 576 : 480) / 8);

	if (id->type == CX18_ENC_STREAM_TYPE_YUV) {
		if (pixfmt->pixelformat != V4L2_PIX_FMT_NV12_16L16 &&
		    pixfmt->pixelformat != V4L2_PIX_FMT_UYVY)
			pixfmt->pixelformat = V4L2_PIX_FMT_UYVY;
		/* YUV height must be a multiple of 32 */
		h = round_up(h, 32);
		/*
		 * HM12 YUV size is (Y=(h*720) + UV=(h*(720/2)))
		 * UYUV YUV size is (Y=(h*720) + UV=(h*(720)))
		 */
		if (pixfmt->pixelformat == V4L2_PIX_FMT_NV12_16L16) {
			pixfmt->sizeimage = h * 720 * 3 / 2;
			pixfmt->bytesperline = 720; /* First plane */
		} else {
			pixfmt->sizeimage = h * 720 * 2;
			pixfmt->bytesperline = 1440; /* Packed */
		}
	} else {
		pixfmt->pixelformat = V4L2_PIX_FMT_MPEG;
		pixfmt->sizeimage = 128 * 1024;
		pixfmt->bytesperline = 0;
	}

	pixfmt->width = w;
	pixfmt->height = h;
	pixfmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pixfmt->field = V4L2_FIELD_INTERLACED;
	return 0;
}

static int cx18_s_fmt_vid_cap(struct file *file, void *fh,
			      struct v4l2_format *fmt)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct cx18_stream *s = &cx->streams[id->type];
	int ret;
	int w, h;

	ret = cx18_try_fmt_vid_cap(file, fh, fmt);
	if (ret)
		return ret;
	w = fmt->fmt.pix.width;
	h = fmt->fmt.pix.height;

	if (cx->cxhdl.width == w && cx->cxhdl.height == h &&
	    s->pixelformat == fmt->fmt.pix.pixelformat)
		return 0;

	if (atomic_read(&cx->ana_capturing) > 0)
		return -EBUSY;

	s->pixelformat = fmt->fmt.pix.pixelformat;
	s->vb_bytes_per_frame = fmt->fmt.pix.sizeimage;
	s->vb_bytes_per_line = fmt->fmt.pix.bytesperline;

	format.format.width = cx->cxhdl.width = w;
	format.format.height = cx->cxhdl.height = h;
	format.format.code = MEDIA_BUS_FMT_FIXED;
	v4l2_subdev_call(cx->sd_av, pad, set_fmt, NULL, &format);
	return cx18_g_fmt_vid_cap(file, fh, fmt);
}

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

/* Check if VBI services are allowed on the (field, line) for the video std */
static int valid_service_line(int field, int line, int is_pal)
{
	return (is_pal && line >= 6 &&
		((field == 0 && line <= 23) || (field == 1 && line <= 22))) ||
	       (!is_pal && line >= 10 && line < 22);
}

/*
 * For a (field, line, std) and inbound potential set of services for that line,
 * return the first valid service of those passed in the incoming set for that
 * line in priority order:
 * CC, VPS, or WSS over TELETEXT for well known lines
 * TELETEXT, before VPS, before CC, before WSS, for other lines
 */
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
		if (BIT(i) & set)
			return 1 << i;
	}
	return 0;
}

/*
 * Expand the service_set of *fmt into valid service_lines for the std,
 * and clear the passed in fmt->service_set
 */
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

/*
 * Sanitize the service_lines in *fmt per the video std, and return 1
 * if any service_line is left as valid after santization
 */
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

/* Compute the service_set from the assumed valid service_lines of *fmt */
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

static int cx18_g_fmt_vbi_cap(struct file *file, void *fh,
				struct v4l2_format *fmt)
{
	struct cx18 *cx = file2id(file)->cx;
	struct v4l2_vbi_format *vbifmt = &fmt->fmt.vbi;

	vbifmt->sampling_rate = 27000000;
	vbifmt->offset = 248; /* FIXME - slightly wrong for both 50 & 60 Hz */
	vbifmt->samples_per_line = VBI_ACTIVE_SAMPLES - 4;
	vbifmt->sample_format = V4L2_PIX_FMT_GREY;
	vbifmt->start[0] = cx->vbi.start[0];
	vbifmt->start[1] = cx->vbi.start[1];
	vbifmt->count[0] = vbifmt->count[1] = cx->vbi.count;
	vbifmt->flags = 0;
	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;
	return 0;
}

static int cx18_g_fmt_sliced_vbi_cap(struct file *file, void *fh,
					struct v4l2_format *fmt)
{
	struct cx18 *cx = file2id(file)->cx;
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

	/* sane, V4L2 spec compliant, defaults */
	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;
	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	memset(vbifmt->service_lines, 0, sizeof(vbifmt->service_lines));
	vbifmt->service_set = 0;

	/*
	 * Fetch the configured service_lines and total service_set from the
	 * digitizer/slicer.  Note, cx18_av_vbi() wipes the passed in
	 * fmt->fmt.sliced under valid calling conditions
	 */
	if (v4l2_subdev_call(cx->sd_av, vbi, g_sliced_fmt, &fmt->fmt.sliced))
		return -EINVAL;

	vbifmt->service_set = cx18_get_service_set(vbifmt);
	return 0;
}

static int cx18_try_fmt_vbi_cap(struct file *file, void *fh,
				struct v4l2_format *fmt)
{
	return cx18_g_fmt_vbi_cap(file, fh, fmt);
}

static int cx18_try_fmt_sliced_vbi_cap(struct file *file, void *fh,
					struct v4l2_format *fmt)
{
	struct cx18 *cx = file2id(file)->cx;
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

	vbifmt->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	vbifmt->reserved[0] = 0;
	vbifmt->reserved[1] = 0;

	/* If given a service set, expand it validly & clear passed in set */
	if (vbifmt->service_set)
		cx18_expand_service_set(vbifmt, cx->is_50hz);
	/* Sanitize the service_lines, and compute the new set if any valid */
	if (check_service_set(vbifmt, cx->is_50hz))
		vbifmt->service_set = cx18_get_service_set(vbifmt);
	return 0;
}

static int cx18_s_fmt_vbi_cap(struct file *file, void *fh,
				struct v4l2_format *fmt)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;
	int ret;

	/*
	 * Changing the Encoder's Raw VBI parameters won't have any effect
	 * if any analog capture is ongoing
	 */
	if (!cx18_raw_vbi(cx) && atomic_read(&cx->ana_capturing) > 0)
		return -EBUSY;

	/*
	 * Set the digitizer registers for raw active VBI.
	 * Note cx18_av_vbi_wipes out a lot of the passed in fmt under valid
	 * calling conditions
	 */
	ret = v4l2_subdev_call(cx->sd_av, vbi, s_raw_fmt, &fmt->fmt.vbi);
	if (ret)
		return ret;

	/* Store our new v4l2 (non-)sliced VBI state */
	cx->vbi.sliced_in->service_set = 0;
	cx->vbi.in.type = V4L2_BUF_TYPE_VBI_CAPTURE;

	return cx18_g_fmt_vbi_cap(file, fh, fmt);
}

static int cx18_s_fmt_sliced_vbi_cap(struct file *file, void *fh,
					struct v4l2_format *fmt)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;
	int ret;
	struct v4l2_sliced_vbi_format *vbifmt = &fmt->fmt.sliced;

	cx18_try_fmt_sliced_vbi_cap(file, fh, fmt);

	/*
	 * Changing the Encoder's Raw VBI parameters won't have any effect
	 * if any analog capture is ongoing
	 */
	if (cx18_raw_vbi(cx) && atomic_read(&cx->ana_capturing) > 0)
		return -EBUSY;

	/*
	 * Set the service_lines requested in the digitizer/slicer registers.
	 * Note, cx18_av_vbi() wipes some "impossible" service lines in the
	 * passed in fmt->fmt.sliced under valid calling conditions
	 */
	ret = v4l2_subdev_call(cx->sd_av, vbi, s_sliced_fmt, &fmt->fmt.sliced);
	if (ret)
		return ret;
	/* Store our current v4l2 sliced VBI settings */
	cx->vbi.in.type =  V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	memcpy(cx->vbi.sliced_in, vbifmt, sizeof(*cx->vbi.sliced_in));
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int cx18_g_register(struct file *file, void *fh,
				struct v4l2_dbg_register *reg)
{
	struct cx18 *cx = file2id(file)->cx;

	if (reg->reg & 0x3)
		return -EINVAL;
	if (reg->reg >= CX18_MEM_OFFSET + CX18_MEM_SIZE)
		return -EINVAL;
	reg->size = 4;
	reg->val = cx18_read_enc(cx, reg->reg);
	return 0;
}

static int cx18_s_register(struct file *file, void *fh,
				const struct v4l2_dbg_register *reg)
{
	struct cx18 *cx = file2id(file)->cx;

	if (reg->reg & 0x3)
		return -EINVAL;
	if (reg->reg >= CX18_MEM_OFFSET + CX18_MEM_SIZE)
		return -EINVAL;
	cx18_write_enc(cx, reg->val, reg->reg);
	return 0;
}
#endif

static int cx18_querycap(struct file *file, void *fh,
				struct v4l2_capability *vcap)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;

	strscpy(vcap->driver, CX18_DRIVER_NAME, sizeof(vcap->driver));
	strscpy(vcap->card, cx->card_name, sizeof(vcap->card));
	vcap->capabilities = cx->v4l2_cap | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int cx18_enumaudio(struct file *file, void *fh, struct v4l2_audio *vin)
{
	struct cx18 *cx = file2id(file)->cx;

	return cx18_get_audio_input(cx, vin->index, vin);
}

static int cx18_g_audio(struct file *file, void *fh, struct v4l2_audio *vin)
{
	struct cx18 *cx = file2id(file)->cx;

	vin->index = cx->audio_input;
	return cx18_get_audio_input(cx, vin->index, vin);
}

static int cx18_s_audio(struct file *file, void *fh, const struct v4l2_audio *vout)
{
	struct cx18 *cx = file2id(file)->cx;

	if (vout->index >= cx->nof_audio_inputs)
		return -EINVAL;
	cx->audio_input = vout->index;
	cx18_audio_set_io(cx);
	return 0;
}

static int cx18_enum_input(struct file *file, void *fh, struct v4l2_input *vin)
{
	struct cx18 *cx = file2id(file)->cx;

	/* set it to defaults from our table */
	return cx18_get_input(cx, vin->index, vin);
}

static int cx18_g_pixelaspect(struct file *file, void *fh,
			      int type, struct v4l2_fract *f)
{
	struct cx18 *cx = file2id(file)->cx;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f->numerator = cx->is_50hz ? 54 : 11;
	f->denominator = cx->is_50hz ? 59 : 10;
	return 0;
}

static int cx18_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *sel)
{
	struct cx18 *cx = file2id(file)->cx;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = sel->r.left = 0;
		sel->r.width = 720;
		sel->r.height = cx->is_50hz ? 576 : 480;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx18_enum_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_fmtdesc *fmt)
{
	struct cx18_open_id *id = file2id(file);

	if (id->type == CX18_ENC_STREAM_TYPE_YUV) {
		if (fmt->index >= ARRAY_SIZE(cx18_formats_yuv))
			return -EINVAL;
		*fmt = cx18_formats_yuv[fmt->index];
		return 0;
	}
	if (fmt->index)
		return -EINVAL;
	*fmt = cx18_formats_mpeg[0];
	return 0;
}

static int cx18_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct cx18 *cx = file2id(file)->cx;

	*i = cx->active_input;
	return 0;
}

int cx18_do_s_input(struct cx18 *cx, unsigned int inp)
{
	v4l2_std_id std = V4L2_STD_ALL;
	const struct cx18_card_video_input *card_input =
				cx->card->video_inputs + inp;

	if (inp >= cx->nof_inputs)
		return -EINVAL;

	if (inp == cx->active_input) {
		CX18_DEBUG_INFO("Input unchanged\n");
		return 0;
	}

	CX18_DEBUG_INFO("Changing input from %d to %d\n",
			cx->active_input, inp);

	cx->active_input = inp;
	/* Set the audio input to whatever is appropriate for the input type. */
	cx->audio_input = cx->card->video_inputs[inp].audio_index;
	if (card_input->video_type == V4L2_INPUT_TYPE_TUNER)
		std = cx->tuner_std;
	cx->streams[CX18_ENC_STREAM_TYPE_MPG].video_dev.tvnorms = std;
	cx->streams[CX18_ENC_STREAM_TYPE_YUV].video_dev.tvnorms = std;
	cx->streams[CX18_ENC_STREAM_TYPE_VBI].video_dev.tvnorms = std;

	/* prevent others from messing with the streams until
	   we're finished changing inputs. */
	cx18_mute(cx);
	cx18_video_set_io(cx);
	cx18_audio_set_io(cx);
	cx18_unmute(cx);
	return 0;
}

static int cx18_s_input(struct file *file, void *fh, unsigned int inp)
{
	return cx18_do_s_input(file2id(file)->cx, inp);
}

static int cx18_g_frequency(struct file *file, void *fh,
				struct v4l2_frequency *vf)
{
	struct cx18 *cx = file2id(file)->cx;

	if (vf->tuner != 0)
		return -EINVAL;

	cx18_call_all(cx, tuner, g_frequency, vf);
	return 0;
}

int cx18_do_s_frequency(struct cx18 *cx, const struct v4l2_frequency *vf)
{
	if (vf->tuner != 0)
		return -EINVAL;

	cx18_mute(cx);
	CX18_DEBUG_INFO("v4l2 ioctl: set frequency %d\n", vf->frequency);
	cx18_call_all(cx, tuner, s_frequency, vf);
	cx18_unmute(cx);
	return 0;
}

static int cx18_s_frequency(struct file *file, void *fh,
			    const struct v4l2_frequency *vf)
{
	return cx18_do_s_frequency(file2id(file)->cx, vf);
}

static int cx18_g_std(struct file *file, void *fh, v4l2_std_id *std)
{
	struct cx18 *cx = file2id(file)->cx;

	*std = cx->std;
	return 0;
}

int cx18_do_s_std(struct cx18 *cx, v4l2_std_id std)
{
	if ((std & V4L2_STD_ALL) == 0)
		return -EINVAL;

	if (std == cx->std)
		return 0;

	if (test_bit(CX18_F_I_RADIO_USER, &cx->i_flags) ||
	    atomic_read(&cx->ana_capturing) > 0) {
		/* Switching standard would turn off the radio or mess
		   with already running streams, prevent that by
		   returning EBUSY. */
		return -EBUSY;
	}

	cx->std = std;
	cx->is_60hz = (std & V4L2_STD_525_60) ? 1 : 0;
	cx->is_50hz = !cx->is_60hz;
	cx2341x_handler_set_50hz(&cx->cxhdl, cx->is_50hz);
	cx->cxhdl.width = 720;
	cx->cxhdl.height = cx->is_50hz ? 576 : 480;
	/*
	 * HM12 YUV size is (Y=(h*720) + UV=(h*(720/2)))
	 * UYUV YUV size is (Y=(h*720) + UV=(h*(720)))
	 */
	if (cx->streams[CX18_ENC_STREAM_TYPE_YUV].pixelformat == V4L2_PIX_FMT_NV12_16L16) {
		cx->streams[CX18_ENC_STREAM_TYPE_YUV].vb_bytes_per_frame =
			cx->cxhdl.height * 720 * 3 / 2;
		cx->streams[CX18_ENC_STREAM_TYPE_YUV].vb_bytes_per_line = 720;
	} else {
		cx->streams[CX18_ENC_STREAM_TYPE_YUV].vb_bytes_per_frame =
			cx->cxhdl.height * 720 * 2;
		cx->streams[CX18_ENC_STREAM_TYPE_YUV].vb_bytes_per_line = 1440;
	}
	cx->vbi.count = cx->is_50hz ? 18 : 12;
	cx->vbi.start[0] = cx->is_50hz ? 6 : 10;
	cx->vbi.start[1] = cx->is_50hz ? 318 : 273;
	CX18_DEBUG_INFO("Switching standard to %llx.\n",
			(unsigned long long) cx->std);

	/* Tuner */
	cx18_call_all(cx, video, s_std, cx->std);
	return 0;
}

static int cx18_s_std(struct file *file, void *fh, v4l2_std_id std)
{
	return cx18_do_s_std(file2id(file)->cx, std);
}

static int cx18_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *vt)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;

	if (vt->index != 0)
		return -EINVAL;

	cx18_call_all(cx, tuner, s_tuner, vt);
	return 0;
}

static int cx18_g_tuner(struct file *file, void *fh, struct v4l2_tuner *vt)
{
	struct cx18 *cx = file2id(file)->cx;

	if (vt->index != 0)
		return -EINVAL;

	cx18_call_all(cx, tuner, g_tuner, vt);

	if (vt->type == V4L2_TUNER_RADIO)
		strscpy(vt->name, "cx18 Radio Tuner", sizeof(vt->name));
	else
		strscpy(vt->name, "cx18 TV Tuner", sizeof(vt->name));
	return 0;
}

static int cx18_g_sliced_vbi_cap(struct file *file, void *fh,
					struct v4l2_sliced_vbi_cap *cap)
{
	struct cx18 *cx = file2id(file)->cx;
	int set = cx->is_50hz ? V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525;
	int f, l;

	if (cap->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
		return -EINVAL;

	cap->service_set = 0;
	for (f = 0; f < 2; f++) {
		for (l = 0; l < 24; l++) {
			if (valid_service_line(f, l, cx->is_50hz)) {
				/*
				 * We can find all v4l2 supported vbi services
				 * for the standard, on a valid line for the std
				 */
				cap->service_lines[f][l] = set;
				cap->service_set |= set;
			} else
				cap->service_lines[f][l] = 0;
		}
	}
	for (f = 0; f < 3; f++)
		cap->reserved[f] = 0;
	return 0;
}

static int _cx18_process_idx_data(struct cx18_buffer *buf,
				  struct v4l2_enc_idx *idx)
{
	int consumed, remaining;
	struct v4l2_enc_idx_entry *e_idx;
	struct cx18_enc_idx_entry *e_buf;

	/* Frame type lookup: 1=I, 2=P, 4=B */
	static const int mapping[8] = {
		-1, V4L2_ENC_IDX_FRAME_I, V4L2_ENC_IDX_FRAME_P,
		-1, V4L2_ENC_IDX_FRAME_B, -1, -1, -1
	};

	/*
	 * Assumption here is that a buf holds an integral number of
	 * struct cx18_enc_idx_entry objects and is properly aligned.
	 * This is enforced by the module options on IDX buffer sizes.
	 */
	remaining = buf->bytesused - buf->readpos;
	consumed = 0;
	e_idx = &idx->entry[idx->entries];
	e_buf = (struct cx18_enc_idx_entry *) &buf->buf[buf->readpos];

	while (remaining >= sizeof(struct cx18_enc_idx_entry) &&
	       idx->entries < V4L2_ENC_IDX_ENTRIES) {

		e_idx->offset = (((u64) le32_to_cpu(e_buf->offset_high)) << 32)
				| le32_to_cpu(e_buf->offset_low);

		e_idx->pts = (((u64) (le32_to_cpu(e_buf->pts_high) & 1)) << 32)
			     | le32_to_cpu(e_buf->pts_low);

		e_idx->length = le32_to_cpu(e_buf->length);

		e_idx->flags = mapping[le32_to_cpu(e_buf->flags) & 0x7];

		e_idx->reserved[0] = 0;
		e_idx->reserved[1] = 0;

		idx->entries++;
		e_idx = &idx->entry[idx->entries];
		e_buf++;

		remaining -= sizeof(struct cx18_enc_idx_entry);
		consumed += sizeof(struct cx18_enc_idx_entry);
	}

	/* Swallow any partial entries at the end, if there are any */
	if (remaining > 0 && remaining < sizeof(struct cx18_enc_idx_entry))
		consumed += remaining;

	buf->readpos += consumed;
	return consumed;
}

static int cx18_process_idx_data(struct cx18_stream *s, struct cx18_mdl *mdl,
				 struct v4l2_enc_idx *idx)
{
	if (s->type != CX18_ENC_STREAM_TYPE_IDX)
		return -EINVAL;

	if (mdl->curr_buf == NULL)
		mdl->curr_buf = list_first_entry(&mdl->buf_list,
						 struct cx18_buffer, list);

	if (list_entry_is_head(mdl->curr_buf, &mdl->buf_list, list)) {
		/*
		 * For some reason we've exhausted the buffers, but the MDL
		 * object still said some data was unread.
		 * Fix that and bail out.
		 */
		mdl->readpos = mdl->bytesused;
		return 0;
	}

	list_for_each_entry_from(mdl->curr_buf, &mdl->buf_list, list) {

		/* Skip any empty buffers in the MDL */
		if (mdl->curr_buf->readpos >= mdl->curr_buf->bytesused)
			continue;

		mdl->readpos += _cx18_process_idx_data(mdl->curr_buf, idx);

		/* exit when MDL drained or request satisfied */
		if (idx->entries >= V4L2_ENC_IDX_ENTRIES ||
		    mdl->curr_buf->readpos < mdl->curr_buf->bytesused ||
		    mdl->readpos >= mdl->bytesused)
			break;
	}
	return 0;
}

static int cx18_g_enc_index(struct file *file, void *fh,
				struct v4l2_enc_idx *idx)
{
	struct cx18 *cx = file2id(file)->cx;
	struct cx18_stream *s = &cx->streams[CX18_ENC_STREAM_TYPE_IDX];
	s32 tmp;
	struct cx18_mdl *mdl;

	if (!cx18_stream_enabled(s)) /* Module options inhibited IDX stream */
		return -EINVAL;

	/* Compute the best case number of entries we can buffer */
	tmp = s->buffers -
			  s->bufs_per_mdl * CX18_ENC_STREAM_TYPE_IDX_FW_MDL_MIN;
	if (tmp <= 0)
		tmp = 1;
	tmp = tmp * s->buf_size / sizeof(struct cx18_enc_idx_entry);

	/* Fill out the header of the return structure */
	idx->entries = 0;
	idx->entries_cap = tmp;
	memset(idx->reserved, 0, sizeof(idx->reserved));

	/* Pull IDX MDLs and buffers from q_full and populate the entries */
	do {
		mdl = cx18_dequeue(s, &s->q_full);
		if (mdl == NULL) /* No more IDX data right now */
			break;

		/* Extract the Index entry data from the MDL and buffers */
		cx18_process_idx_data(s, mdl, idx);
		if (mdl->readpos < mdl->bytesused) {
			/* We finished with data remaining, push the MDL back */
			cx18_push(s, mdl, &s->q_full);
			break;
		}

		/* We drained this MDL, schedule it to go to the firmware */
		cx18_enqueue(s, mdl, &s->q_free);

	} while (idx->entries < V4L2_ENC_IDX_ENTRIES);

	/* Tell the work handler to send free IDX MDLs to the firmware */
	cx18_stream_load_fw_queue(s);
	return 0;
}

static int cx18_encoder_cmd(struct file *file, void *fh,
				struct v4l2_encoder_cmd *enc)
{
	struct cx18_open_id *id = file2id(file);
	struct cx18 *cx = id->cx;
	u32 h;

	switch (enc->cmd) {
	case V4L2_ENC_CMD_START:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_START\n");
		enc->flags = 0;
		return cx18_start_capture(id);

	case V4L2_ENC_CMD_STOP:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_STOP\n");
		enc->flags &= V4L2_ENC_CMD_STOP_AT_GOP_END;
		cx18_stop_capture(&cx->streams[id->type],
				  enc->flags & V4L2_ENC_CMD_STOP_AT_GOP_END);
		break;

	case V4L2_ENC_CMD_PAUSE:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_PAUSE\n");
		enc->flags = 0;
		if (!atomic_read(&cx->ana_capturing))
			return -EPERM;
		if (test_and_set_bit(CX18_F_I_ENC_PAUSED, &cx->i_flags))
			return 0;
		h = cx18_find_handle(cx);
		if (h == CX18_INVALID_TASK_HANDLE) {
			CX18_ERR("Can't find valid task handle for V4L2_ENC_CMD_PAUSE\n");
			return -EBADFD;
		}
		cx18_mute(cx);
		cx18_vapi(cx, CX18_CPU_CAPTURE_PAUSE, 1, h);
		break;

	case V4L2_ENC_CMD_RESUME:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_RESUME\n");
		enc->flags = 0;
		if (!atomic_read(&cx->ana_capturing))
			return -EPERM;
		if (!test_and_clear_bit(CX18_F_I_ENC_PAUSED, &cx->i_flags))
			return 0;
		h = cx18_find_handle(cx);
		if (h == CX18_INVALID_TASK_HANDLE) {
			CX18_ERR("Can't find valid task handle for V4L2_ENC_CMD_RESUME\n");
			return -EBADFD;
		}
		cx18_vapi(cx, CX18_CPU_CAPTURE_RESUME, 1, h);
		cx18_unmute(cx);
		break;

	default:
		CX18_DEBUG_IOCTL("Unknown cmd %d\n", enc->cmd);
		return -EINVAL;
	}
	return 0;
}

static int cx18_try_encoder_cmd(struct file *file, void *fh,
				struct v4l2_encoder_cmd *enc)
{
	struct cx18 *cx = file2id(file)->cx;

	switch (enc->cmd) {
	case V4L2_ENC_CMD_START:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_START\n");
		enc->flags = 0;
		break;

	case V4L2_ENC_CMD_STOP:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_STOP\n");
		enc->flags &= V4L2_ENC_CMD_STOP_AT_GOP_END;
		break;

	case V4L2_ENC_CMD_PAUSE:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_PAUSE\n");
		enc->flags = 0;
		break;

	case V4L2_ENC_CMD_RESUME:
		CX18_DEBUG_IOCTL("V4L2_ENC_CMD_RESUME\n");
		enc->flags = 0;
		break;

	default:
		CX18_DEBUG_IOCTL("Unknown cmd %d\n", enc->cmd);
		return -EINVAL;
	}
	return 0;
}

static int cx18_log_status(struct file *file, void *fh)
{
	struct cx18 *cx = file2id(file)->cx;
	struct v4l2_input vidin;
	struct v4l2_audio audin;
	int i;

	CX18_INFO("Version: %s  Card: %s\n", CX18_VERSION, cx->card_name);
	if (cx->hw_flags & CX18_HW_TVEEPROM) {
		struct tveeprom tv;

		cx18_read_eeprom(cx, &tv);
	}
	cx18_call_all(cx, core, log_status);
	cx18_get_input(cx, cx->active_input, &vidin);
	cx18_get_audio_input(cx, cx->audio_input, &audin);
	CX18_INFO("Video Input: %s\n", vidin.name);
	CX18_INFO("Audio Input: %s\n", audin.name);
	mutex_lock(&cx->gpio_lock);
	CX18_INFO("GPIO:  direction 0x%08x, value 0x%08x\n",
		cx->gpio_dir, cx->gpio_val);
	mutex_unlock(&cx->gpio_lock);
	CX18_INFO("Tuner: %s\n",
		test_bit(CX18_F_I_RADIO_USER, &cx->i_flags) ?  "Radio" : "TV");
	v4l2_ctrl_handler_log_status(&cx->cxhdl.hdl, cx->v4l2_dev.name);
	CX18_INFO("Status flags: 0x%08lx\n", cx->i_flags);
	for (i = 0; i < CX18_MAX_STREAMS; i++) {
		struct cx18_stream *s = &cx->streams[i];

		if (s->video_dev.v4l2_dev == NULL || s->buffers == 0)
			continue;
		CX18_INFO("Stream %s: status 0x%04lx, %d%% of %d KiB (%d buffers) in use\n",
			  s->name, s->s_flags,
			  atomic_read(&s->q_full.depth) * s->bufs_per_mdl * 100
			   / s->buffers,
			  (s->buffers * s->buf_size) / 1024, s->buffers);
	}
	CX18_INFO("Read MPEG/VBI: %lld/%lld bytes\n",
			(long long)cx->mpg_data_received,
			(long long)cx->vbi_data_inserted);
	return 0;
}

static long cx18_default(struct file *file, void *fh, bool valid_prio,
			 unsigned int cmd, void *arg)
{
	struct cx18 *cx = file2id(file)->cx;

	switch (cmd) {
	case VIDIOC_INT_RESET: {
		u32 val = *(u32 *)arg;

		if ((val == 0) || (val & 0x01))
			cx18_call_hw(cx, CX18_HW_GPIO_RESET_CTRL, core, reset,
				     (u32) CX18_GPIO_RESET_Z8F0811);
		break;
	}

	default:
		return -ENOTTY;
	}
	return 0;
}

static const struct v4l2_ioctl_ops cx18_ioctl_ops = {
	.vidioc_querycap                = cx18_querycap,
	.vidioc_s_audio                 = cx18_s_audio,
	.vidioc_g_audio                 = cx18_g_audio,
	.vidioc_enumaudio               = cx18_enumaudio,
	.vidioc_enum_input              = cx18_enum_input,
	.vidioc_g_pixelaspect           = cx18_g_pixelaspect,
	.vidioc_g_selection             = cx18_g_selection,
	.vidioc_g_input                 = cx18_g_input,
	.vidioc_s_input                 = cx18_s_input,
	.vidioc_g_frequency             = cx18_g_frequency,
	.vidioc_s_frequency             = cx18_s_frequency,
	.vidioc_s_tuner                 = cx18_s_tuner,
	.vidioc_g_tuner                 = cx18_g_tuner,
	.vidioc_g_enc_index             = cx18_g_enc_index,
	.vidioc_g_std                   = cx18_g_std,
	.vidioc_s_std                   = cx18_s_std,
	.vidioc_log_status              = cx18_log_status,
	.vidioc_enum_fmt_vid_cap        = cx18_enum_fmt_vid_cap,
	.vidioc_encoder_cmd             = cx18_encoder_cmd,
	.vidioc_try_encoder_cmd         = cx18_try_encoder_cmd,
	.vidioc_g_fmt_vid_cap           = cx18_g_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap           = cx18_g_fmt_vbi_cap,
	.vidioc_g_fmt_sliced_vbi_cap    = cx18_g_fmt_sliced_vbi_cap,
	.vidioc_s_fmt_vid_cap           = cx18_s_fmt_vid_cap,
	.vidioc_s_fmt_vbi_cap           = cx18_s_fmt_vbi_cap,
	.vidioc_s_fmt_sliced_vbi_cap    = cx18_s_fmt_sliced_vbi_cap,
	.vidioc_try_fmt_vid_cap         = cx18_try_fmt_vid_cap,
	.vidioc_try_fmt_vbi_cap         = cx18_try_fmt_vbi_cap,
	.vidioc_try_fmt_sliced_vbi_cap  = cx18_try_fmt_sliced_vbi_cap,
	.vidioc_g_sliced_vbi_cap        = cx18_g_sliced_vbi_cap,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register              = cx18_g_register,
	.vidioc_s_register              = cx18_s_register,
#endif
	.vidioc_default                 = cx18_default,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

void cx18_set_funcs(struct video_device *vdev)
{
	vdev->ioctl_ops = &cx18_ioctl_ops;
}
