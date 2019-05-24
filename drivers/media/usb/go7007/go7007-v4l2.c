/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>
#include <media/i2c/saa7115.h>

#include "go7007-priv.h"

#define call_all(dev, o, f, args...) \
	v4l2_device_call_until_err(dev, 0, o, f, ##args)

static bool valid_pixelformat(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
	case V4L2_PIX_FMT_MPEG4:
		return true;
	default:
		return false;
	}
}

static u32 get_frame_type_flag(struct go7007_buffer *vb, int format)
{
	u8 *ptr = vb2_plane_vaddr(&vb->vb.vb2_buf, 0);

	switch (format) {
	case V4L2_PIX_FMT_MJPEG:
		return V4L2_BUF_FLAG_KEYFRAME;
	case V4L2_PIX_FMT_MPEG4:
		switch ((ptr[vb->frame_offset + 4] >> 6) & 0x3) {
		case 0:
			return V4L2_BUF_FLAG_KEYFRAME;
		case 1:
			return V4L2_BUF_FLAG_PFRAME;
		case 2:
			return V4L2_BUF_FLAG_BFRAME;
		default:
			return 0;
		}
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
		switch ((ptr[vb->frame_offset + 5] >> 3) & 0x7) {
		case 1:
			return V4L2_BUF_FLAG_KEYFRAME;
		case 2:
			return V4L2_BUF_FLAG_PFRAME;
		case 3:
			return V4L2_BUF_FLAG_BFRAME;
		default:
			return 0;
		}
	}

	return 0;
}

static void get_resolution(struct go7007 *go, int *width, int *height)
{
	switch (go->standard) {
	case GO7007_STD_NTSC:
		*width = 720;
		*height = 480;
		break;
	case GO7007_STD_PAL:
		*width = 720;
		*height = 576;
		break;
	case GO7007_STD_OTHER:
	default:
		*width = go->board_info->sensor_width;
		*height = go->board_info->sensor_height;
		break;
	}
}

static void set_formatting(struct go7007 *go)
{
	if (go->format == V4L2_PIX_FMT_MJPEG) {
		go->pali = 0;
		go->aspect_ratio = GO7007_RATIO_1_1;
		go->gop_size = 0;
		go->ipb = 0;
		go->closed_gop = 0;
		go->repeat_seqhead = 0;
		go->seq_header_enable = 0;
		go->gop_header_enable = 0;
		go->dvd_mode = 0;
		return;
	}

	switch (go->format) {
	case V4L2_PIX_FMT_MPEG1:
		go->pali = 0;
		break;
	default:
	case V4L2_PIX_FMT_MPEG2:
		go->pali = 0x48;
		break;
	case V4L2_PIX_FMT_MPEG4:
		/* For future reference: this is the list of MPEG4
		 * profiles that are available, although they are
		 * untested:
		 *
		 * Profile		pali
		 * --------------	----
		 * PROFILE_S_L0		0x08
		 * PROFILE_S_L1		0x01
		 * PROFILE_S_L2		0x02
		 * PROFILE_S_L3		0x03
		 * PROFILE_ARTS_L1	0x91
		 * PROFILE_ARTS_L2	0x92
		 * PROFILE_ARTS_L3	0x93
		 * PROFILE_ARTS_L4	0x94
		 * PROFILE_AS_L0	0xf0
		 * PROFILE_AS_L1	0xf1
		 * PROFILE_AS_L2	0xf2
		 * PROFILE_AS_L3	0xf3
		 * PROFILE_AS_L4	0xf4
		 * PROFILE_AS_L5	0xf5
		 */
		go->pali = 0xf5;
		break;
	}
	go->gop_size = v4l2_ctrl_g_ctrl(go->mpeg_video_gop_size);
	go->closed_gop = v4l2_ctrl_g_ctrl(go->mpeg_video_gop_closure);
	go->ipb = v4l2_ctrl_g_ctrl(go->mpeg_video_b_frames) != 0;
	go->bitrate = v4l2_ctrl_g_ctrl(go->mpeg_video_bitrate);
	go->repeat_seqhead = v4l2_ctrl_g_ctrl(go->mpeg_video_rep_seqheader);
	go->gop_header_enable = 1;
	go->dvd_mode = 0;
	if (go->format == V4L2_PIX_FMT_MPEG2)
		go->dvd_mode =
			go->bitrate == 9800000 &&
			go->gop_size == 15 &&
			go->ipb == 0 &&
			go->repeat_seqhead == 1 &&
			go->closed_gop;

	switch (v4l2_ctrl_g_ctrl(go->mpeg_video_aspect_ratio)) {
	default:
	case V4L2_MPEG_VIDEO_ASPECT_1x1:
		go->aspect_ratio = GO7007_RATIO_1_1;
		break;
	case V4L2_MPEG_VIDEO_ASPECT_4x3:
		go->aspect_ratio = GO7007_RATIO_4_3;
		break;
	case V4L2_MPEG_VIDEO_ASPECT_16x9:
		go->aspect_ratio = GO7007_RATIO_16_9;
		break;
	}
}

static int set_capture_size(struct go7007 *go, struct v4l2_format *fmt, int try)
{
	int sensor_height = 0, sensor_width = 0;
	int width, height;

	if (fmt != NULL && !valid_pixelformat(fmt->fmt.pix.pixelformat))
		return -EINVAL;

	get_resolution(go, &sensor_width, &sensor_height);

	if (fmt == NULL) {
		width = sensor_width;
		height = sensor_height;
	} else if (go->board_info->sensor_flags & GO7007_SENSOR_SCALING) {
		if (fmt->fmt.pix.width > sensor_width)
			width = sensor_width;
		else if (fmt->fmt.pix.width < 144)
			width = 144;
		else
			width = fmt->fmt.pix.width & ~0x0f;

		if (fmt->fmt.pix.height > sensor_height)
			height = sensor_height;
		else if (fmt->fmt.pix.height < 96)
			height = 96;
		else
			height = fmt->fmt.pix.height & ~0x0f;
	} else {
		width = fmt->fmt.pix.width;

		if (width <= sensor_width / 4) {
			width = sensor_width / 4;
			height = sensor_height / 4;
		} else if (width <= sensor_width / 2) {
			width = sensor_width / 2;
			height = sensor_height / 2;
		} else {
			width = sensor_width;
			height = sensor_height;
		}
		width &= ~0xf;
		height &= ~0xf;
	}

	if (fmt != NULL) {
		u32 pixelformat = fmt->fmt.pix.pixelformat;

		memset(fmt, 0, sizeof(*fmt));
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt->fmt.pix.width = width;
		fmt->fmt.pix.height = height;
		fmt->fmt.pix.pixelformat = pixelformat;
		fmt->fmt.pix.field = V4L2_FIELD_NONE;
		fmt->fmt.pix.bytesperline = 0;
		fmt->fmt.pix.sizeimage = GO7007_BUF_SIZE;
		fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	}

	if (try)
		return 0;

	if (fmt)
		go->format = fmt->fmt.pix.pixelformat;
	go->width = width;
	go->height = height;
	go->encoder_h_offset = go->board_info->sensor_h_offset;
	go->encoder_v_offset = go->board_info->sensor_v_offset;

	if (go->board_info->sensor_flags & GO7007_SENSOR_SCALING) {
		struct v4l2_subdev_format format = {
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		};

		format.format.code = MEDIA_BUS_FMT_FIXED;
		format.format.width = fmt ? fmt->fmt.pix.width : width;
		format.format.height = height;
		go->encoder_h_halve = 0;
		go->encoder_v_halve = 0;
		go->encoder_subsample = 0;
		call_all(&go->v4l2_dev, pad, set_fmt, NULL, &format);
	} else {
		if (width <= sensor_width / 4) {
			go->encoder_h_halve = 1;
			go->encoder_v_halve = 1;
			go->encoder_subsample = 1;
		} else if (width <= sensor_width / 2) {
			go->encoder_h_halve = 1;
			go->encoder_v_halve = 1;
			go->encoder_subsample = 0;
		} else {
			go->encoder_h_halve = 0;
			go->encoder_v_halve = 0;
			go->encoder_subsample = 0;
		}
	}
	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct go7007 *go = video_drvdata(file);

	strscpy(cap->driver, "go7007", sizeof(cap->driver));
	strscpy(cap->card, go->name, sizeof(cap->card));
	strscpy(cap->bus_info, go->bus_info, sizeof(cap->bus_info));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING;

	if (go->board_info->num_aud_inputs)
		cap->device_caps |= V4L2_CAP_AUDIO;
	if (go->board_info->flags & GO7007_BOARD_HAS_TUNER)
		cap->device_caps |= V4L2_CAP_TUNER;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *fmt)
{
	char *desc = NULL;

	switch (fmt->index) {
	case 0:
		fmt->pixelformat = V4L2_PIX_FMT_MJPEG;
		desc = "Motion JPEG";
		break;
	case 1:
		fmt->pixelformat = V4L2_PIX_FMT_MPEG1;
		desc = "MPEG-1 ES";
		break;
	case 2:
		fmt->pixelformat = V4L2_PIX_FMT_MPEG2;
		desc = "MPEG-2 ES";
		break;
	case 3:
		fmt->pixelformat = V4L2_PIX_FMT_MPEG4;
		desc = "MPEG-4 ES";
		break;
	default:
		return -EINVAL;
	}
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt->flags = V4L2_FMT_FLAG_COMPRESSED;

	strscpy(fmt->description, desc, sizeof(fmt->description));

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *fmt)
{
	struct go7007 *go = video_drvdata(file);

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt->fmt.pix.width = go->width;
	fmt->fmt.pix.height = go->height;
	fmt->fmt.pix.pixelformat = go->format;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.sizeimage = GO7007_BUF_SIZE;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *fmt)
{
	struct go7007 *go = video_drvdata(file);

	return set_capture_size(go, fmt, 1);
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *fmt)
{
	struct go7007 *go = video_drvdata(file);

	if (vb2_is_busy(&go->vidq))
		return -EBUSY;

	return set_capture_size(go, fmt, 0);
}

static int go7007_queue_setup(struct vb2_queue *q,
		unsigned int *num_buffers, unsigned int *num_planes,
		unsigned int sizes[], struct device *alloc_devs[])
{
	sizes[0] = GO7007_BUF_SIZE;
	*num_planes = 1;

	if (*num_buffers < 2)
		*num_buffers = 2;

	return 0;
}

static void go7007_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct go7007 *go = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct go7007_buffer *go7007_vb =
		container_of(vbuf, struct go7007_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&go->spinlock, flags);
	list_add_tail(&go7007_vb->list, &go->vidq_active);
	spin_unlock_irqrestore(&go->spinlock, flags);
}

static int go7007_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct go7007_buffer *go7007_vb =
		container_of(vbuf, struct go7007_buffer, vb);

	go7007_vb->modet_active = 0;
	go7007_vb->frame_offset = 0;
	vb->planes[0].bytesused = 0;
	return 0;
}

static void go7007_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct go7007 *go = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct go7007_buffer *go7007_vb =
		container_of(vbuf, struct go7007_buffer, vb);
	u32 frame_type_flag = get_frame_type_flag(go7007_vb, go->format);

	vbuf->flags &= ~(V4L2_BUF_FLAG_KEYFRAME | V4L2_BUF_FLAG_BFRAME |
			V4L2_BUF_FLAG_PFRAME);
	vbuf->flags |= frame_type_flag;
	vbuf->field = V4L2_FIELD_NONE;
}

static int go7007_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct go7007 *go = vb2_get_drv_priv(q);
	int ret;

	set_formatting(go);
	mutex_lock(&go->hw_lock);
	go->next_seq = 0;
	go->active_buf = NULL;
	go->modet_event_status = 0;
	q->streaming = 1;
	if (go7007_start_encoder(go) < 0)
		ret = -EIO;
	else
		ret = 0;
	mutex_unlock(&go->hw_lock);
	if (ret) {
		q->streaming = 0;
		return ret;
	}
	call_all(&go->v4l2_dev, video, s_stream, 1);
	v4l2_ctrl_grab(go->mpeg_video_gop_size, true);
	v4l2_ctrl_grab(go->mpeg_video_gop_closure, true);
	v4l2_ctrl_grab(go->mpeg_video_bitrate, true);
	v4l2_ctrl_grab(go->mpeg_video_aspect_ratio, true);
	/* Turn on Capture LED */
	if (go->board_id == GO7007_BOARDID_ADS_USBAV_709)
		go7007_write_addr(go, 0x3c82, 0x0005);
	return ret;
}

static void go7007_stop_streaming(struct vb2_queue *q)
{
	struct go7007 *go = vb2_get_drv_priv(q);
	unsigned long flags;

	q->streaming = 0;
	go7007_stream_stop(go);
	mutex_lock(&go->hw_lock);
	go7007_reset_encoder(go);
	mutex_unlock(&go->hw_lock);
	call_all(&go->v4l2_dev, video, s_stream, 0);

	spin_lock_irqsave(&go->spinlock, flags);
	INIT_LIST_HEAD(&go->vidq_active);
	spin_unlock_irqrestore(&go->spinlock, flags);
	v4l2_ctrl_grab(go->mpeg_video_gop_size, false);
	v4l2_ctrl_grab(go->mpeg_video_gop_closure, false);
	v4l2_ctrl_grab(go->mpeg_video_bitrate, false);
	v4l2_ctrl_grab(go->mpeg_video_aspect_ratio, false);
	/* Turn on Capture LED */
	if (go->board_id == GO7007_BOARDID_ADS_USBAV_709)
		go7007_write_addr(go, 0x3c82, 0x000d);
}

static const struct vb2_ops go7007_video_qops = {
	.queue_setup    = go7007_queue_setup,
	.buf_queue      = go7007_buf_queue,
	.buf_prepare    = go7007_buf_prepare,
	.buf_finish     = go7007_buf_finish,
	.start_streaming = go7007_start_streaming,
	.stop_streaming = go7007_stop_streaming,
	.wait_prepare   = vb2_ops_wait_prepare,
	.wait_finish    = vb2_ops_wait_finish,
};

static int vidioc_g_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	struct go7007 *go = video_drvdata(filp);
	struct v4l2_fract timeperframe = {
		.numerator = 1001 *  go->fps_scale,
		.denominator = go->sensor_framerate,
	};

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	parm->parm.capture.readbuffers = 2;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = timeperframe;

	return 0;
}

static int vidioc_s_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	struct go7007 *go = video_drvdata(filp);
	unsigned int n, d;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	n = go->sensor_framerate *
		parm->parm.capture.timeperframe.numerator;
	d = 1001 * parm->parm.capture.timeperframe.denominator;
	if (n != 0 && d != 0 && n > d)
		go->fps_scale = (n + d/2) / d;
	else
		go->fps_scale = 1;

	return vidioc_g_parm(filp, priv, parm);
}

/* VIDIOC_ENUMSTD on go7007 were used for enumerating the supported fps and
   its resolution, when the device is not connected to TV.
   This is were an API abuse, probably used by the lack of specific IOCTL's to
   enumerate it, by the time the driver was written.

   However, since kernel 2.6.19, two new ioctls (VIDIOC_ENUM_FRAMEINTERVALS
   and VIDIOC_ENUM_FRAMESIZES) were added for this purpose.

   The two functions below implement the newer ioctls
*/
static int vidioc_enum_framesizes(struct file *filp, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	struct go7007 *go = video_drvdata(filp);
	int width, height;

	if (fsize->index > 2)
		return -EINVAL;

	if (!valid_pixelformat(fsize->pixel_format))
		return -EINVAL;

	get_resolution(go, &width, &height);
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = (width >> fsize->index) & ~0xf;
	fsize->discrete.height = (height >> fsize->index) & ~0xf;
	return 0;
}

static int vidioc_enum_frameintervals(struct file *filp, void *priv,
				      struct v4l2_frmivalenum *fival)
{
	struct go7007 *go = video_drvdata(filp);
	int width, height;
	int i;

	if (fival->index > 4)
		return -EINVAL;

	if (!valid_pixelformat(fival->pixel_format))
		return -EINVAL;

	if (!(go->board_info->sensor_flags & GO7007_SENSOR_SCALING)) {
		get_resolution(go, &width, &height);
		for (i = 0; i <= 2; i++)
			if (fival->width == ((width >> i) & ~0xf) &&
			    fival->height == ((height >> i) & ~0xf))
				break;
		if (i > 2)
			return -EINVAL;
	}
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1001 * (fival->index + 1);
	fival->discrete.denominator = go->sensor_framerate;
	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct go7007 *go = video_drvdata(file);

	*std = go->std;
	return 0;
}

static int go7007_s_std(struct go7007 *go)
{
	if (go->std & V4L2_STD_625_50) {
		go->standard = GO7007_STD_PAL;
		go->sensor_framerate = 25025;
	} else {
		go->standard = GO7007_STD_NTSC;
		go->sensor_framerate = 30000;
	}

	call_all(&go->v4l2_dev, video, s_std, go->std);
	set_capture_size(go, NULL, 0);
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct go7007 *go = video_drvdata(file);

	if (vb2_is_busy(&go->vidq))
		return -EBUSY;

	go->std = std;

	return go7007_s_std(go);
}

static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct go7007 *go = video_drvdata(file);

	return call_all(&go->v4l2_dev, video, querystd, std);
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	struct go7007 *go = video_drvdata(file);

	if (inp->index >= go->board_info->num_inputs)
		return -EINVAL;

	strscpy(inp->name, go->board_info->inputs[inp->index].name,
		sizeof(inp->name));

	/* If this board has a tuner, it will be the first input */
	if ((go->board_info->flags & GO7007_BOARD_HAS_TUNER) &&
			inp->index == 0)
		inp->type = V4L2_INPUT_TYPE_TUNER;
	else
		inp->type = V4L2_INPUT_TYPE_CAMERA;

	if (go->board_info->num_aud_inputs)
		inp->audioset = (1 << go->board_info->num_aud_inputs) - 1;
	else
		inp->audioset = 0;
	inp->tuner = 0;
	if (go->board_info->sensor_flags & GO7007_SENSOR_TV)
		inp->std = video_devdata(file)->tvnorms;
	else
		inp->std = 0;

	return 0;
}


static int vidioc_g_input(struct file *file, void *priv, unsigned int *input)
{
	struct go7007 *go = video_drvdata(file);

	*input = go->input;

	return 0;
}

static int vidioc_enumaudio(struct file *file, void *fh, struct v4l2_audio *a)
{
	struct go7007 *go = video_drvdata(file);

	if (a->index >= go->board_info->num_aud_inputs)
		return -EINVAL;
	strscpy(a->name, go->board_info->aud_inputs[a->index].name,
		sizeof(a->name));
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *fh, struct v4l2_audio *a)
{
	struct go7007 *go = video_drvdata(file);

	a->index = go->aud_input;
	strscpy(a->name, go->board_info->aud_inputs[go->aud_input].name,
		sizeof(a->name));
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *fh,
	const struct v4l2_audio *a)
{
	struct go7007 *go = video_drvdata(file);

	if (a->index >= go->board_info->num_aud_inputs)
		return -EINVAL;
	go->aud_input = a->index;
	v4l2_subdev_call(go->sd_audio, audio, s_routing,
		go->board_info->aud_inputs[go->aud_input].audio_input, 0, 0);
	return 0;
}

static void go7007_s_input(struct go7007 *go)
{
	unsigned int input = go->input;

	v4l2_subdev_call(go->sd_video, video, s_routing,
			go->board_info->inputs[input].video_input, 0,
			go->board_info->video_config);
	if (go->board_info->num_aud_inputs) {
		int aud_input = go->board_info->inputs[input].audio_index;

		v4l2_subdev_call(go->sd_audio, audio, s_routing,
			go->board_info->aud_inputs[aud_input].audio_input, 0, 0);
		go->aud_input = aud_input;
	}
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int input)
{
	struct go7007 *go = video_drvdata(file);

	if (input >= go->board_info->num_inputs)
		return -EINVAL;
	if (vb2_is_busy(&go->vidq))
		return -EBUSY;

	go->input = input;
	go7007_s_input(go);

	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct go7007 *go = video_drvdata(file);

	if (t->index != 0)
		return -EINVAL;

	strscpy(t->name, "Tuner", sizeof(t->name));
	return call_all(&go->v4l2_dev, tuner, g_tuner, t);
}

static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct go7007 *go = video_drvdata(file);

	if (t->index != 0)
		return -EINVAL;

	return call_all(&go->v4l2_dev, tuner, s_tuner, t);
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct go7007 *go = video_drvdata(file);

	if (f->tuner)
		return -EINVAL;

	return call_all(&go->v4l2_dev, tuner, g_frequency, f);
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct go7007 *go = video_drvdata(file);

	if (f->tuner)
		return -EINVAL;

	return call_all(&go->v4l2_dev, tuner, s_frequency, f);
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct go7007 *go = video_drvdata(file);

	v4l2_ctrl_log_status(file, priv);
	return call_all(&go->v4l2_dev, core, log_status);
}

static int vidioc_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{

	switch (sub->type) {
	case V4L2_EVENT_MOTION_DET:
		/* Allow for up to 30 events (1 second for NTSC) to be
		 * stored. */
		return v4l2_event_subscribe(fh, sub, 30, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}


static int go7007_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct go7007 *go =
		container_of(ctrl->handler, struct go7007, hdl);
	unsigned y;
	u8 *mt;

	switch (ctrl->id) {
	case V4L2_CID_PIXEL_THRESHOLD0:
		go->modet[0].pixel_threshold = ctrl->val;
		break;
	case V4L2_CID_MOTION_THRESHOLD0:
		go->modet[0].motion_threshold = ctrl->val;
		break;
	case V4L2_CID_MB_THRESHOLD0:
		go->modet[0].mb_threshold = ctrl->val;
		break;
	case V4L2_CID_PIXEL_THRESHOLD1:
		go->modet[1].pixel_threshold = ctrl->val;
		break;
	case V4L2_CID_MOTION_THRESHOLD1:
		go->modet[1].motion_threshold = ctrl->val;
		break;
	case V4L2_CID_MB_THRESHOLD1:
		go->modet[1].mb_threshold = ctrl->val;
		break;
	case V4L2_CID_PIXEL_THRESHOLD2:
		go->modet[2].pixel_threshold = ctrl->val;
		break;
	case V4L2_CID_MOTION_THRESHOLD2:
		go->modet[2].motion_threshold = ctrl->val;
		break;
	case V4L2_CID_MB_THRESHOLD2:
		go->modet[2].mb_threshold = ctrl->val;
		break;
	case V4L2_CID_PIXEL_THRESHOLD3:
		go->modet[3].pixel_threshold = ctrl->val;
		break;
	case V4L2_CID_MOTION_THRESHOLD3:
		go->modet[3].motion_threshold = ctrl->val;
		break;
	case V4L2_CID_MB_THRESHOLD3:
		go->modet[3].mb_threshold = ctrl->val;
		break;
	case V4L2_CID_DETECT_MD_REGION_GRID:
		mt = go->modet_map;
		for (y = 0; y < go->height / 16; y++, mt += go->width / 16)
			memcpy(mt, ctrl->p_new.p_u8 + y * (720 / 16), go->width / 16);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_file_operations go7007_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.unlocked_ioctl	= video_ioctl2,
	.read		= vb2_fop_read,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap          = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,
	.vidioc_g_std             = vidioc_g_std,
	.vidioc_s_std             = vidioc_s_std,
	.vidioc_querystd          = vidioc_querystd,
	.vidioc_enum_input        = vidioc_enum_input,
	.vidioc_g_input           = vidioc_g_input,
	.vidioc_s_input           = vidioc_s_input,
	.vidioc_enumaudio         = vidioc_enumaudio,
	.vidioc_g_audio           = vidioc_g_audio,
	.vidioc_s_audio           = vidioc_s_audio,
	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,
	.vidioc_g_tuner           = vidioc_g_tuner,
	.vidioc_s_tuner           = vidioc_s_tuner,
	.vidioc_g_frequency       = vidioc_g_frequency,
	.vidioc_s_frequency       = vidioc_s_frequency,
	.vidioc_g_parm            = vidioc_g_parm,
	.vidioc_s_parm            = vidioc_s_parm,
	.vidioc_enum_framesizes   = vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
	.vidioc_log_status        = vidioc_log_status,
	.vidioc_subscribe_event   = vidioc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device go7007_template = {
	.name		= "go7007",
	.fops		= &go7007_fops,
	.release	= video_device_release_empty,
	.ioctl_ops	= &video_ioctl_ops,
	.tvnorms	= V4L2_STD_ALL,
};

static const struct v4l2_ctrl_ops go7007_ctrl_ops = {
	.s_ctrl = go7007_s_ctrl,
};

static const struct v4l2_ctrl_config go7007_pixel_threshold0_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_PIXEL_THRESHOLD0,
	.name = "Pixel Threshold Region 0",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 20,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_motion_threshold0_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MOTION_THRESHOLD0,
	.name = "Motion Threshold Region 0",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 80,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_mb_threshold0_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MB_THRESHOLD0,
	.name = "MB Threshold Region 0",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 200,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_pixel_threshold1_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_PIXEL_THRESHOLD1,
	.name = "Pixel Threshold Region 1",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 20,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_motion_threshold1_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MOTION_THRESHOLD1,
	.name = "Motion Threshold Region 1",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 80,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_mb_threshold1_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MB_THRESHOLD1,
	.name = "MB Threshold Region 1",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 200,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_pixel_threshold2_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_PIXEL_THRESHOLD2,
	.name = "Pixel Threshold Region 2",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 20,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_motion_threshold2_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MOTION_THRESHOLD2,
	.name = "Motion Threshold Region 2",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 80,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_mb_threshold2_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MB_THRESHOLD2,
	.name = "MB Threshold Region 2",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 200,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_pixel_threshold3_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_PIXEL_THRESHOLD3,
	.name = "Pixel Threshold Region 3",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 20,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_motion_threshold3_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MOTION_THRESHOLD3,
	.name = "Motion Threshold Region 3",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 80,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_mb_threshold3_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_MB_THRESHOLD3,
	.name = "MB Threshold Region 3",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 200,
	.max = 32767,
	.step = 1,
};

static const struct v4l2_ctrl_config go7007_mb_regions_ctrl = {
	.ops = &go7007_ctrl_ops,
	.id = V4L2_CID_DETECT_MD_REGION_GRID,
	.dims = { 576 / 16, 720 / 16 },
	.max = 3,
	.step = 1,
};

int go7007_v4l2_ctrl_init(struct go7007 *go)
{
	struct v4l2_ctrl_handler *hdl = &go->hdl;
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(hdl, 22);
	go->mpeg_video_gop_size = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0, 34, 1, 15);
	go->mpeg_video_gop_closure = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_MPEG_VIDEO_GOP_CLOSURE, 0, 1, 1, 1);
	go->mpeg_video_bitrate = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_MPEG_VIDEO_BITRATE,
			64000, 10000000, 1, 9800000);
	go->mpeg_video_b_frames = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_MPEG_VIDEO_B_FRAMES, 0, 2, 2, 0);
	go->mpeg_video_rep_seqheader = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, 0, 1, 1, 1);

	go->mpeg_video_aspect_ratio = v4l2_ctrl_new_std_menu(hdl, NULL,
			V4L2_CID_MPEG_VIDEO_ASPECT,
			V4L2_MPEG_VIDEO_ASPECT_16x9, 0,
			V4L2_MPEG_VIDEO_ASPECT_1x1);
	ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_JPEG_ACTIVE_MARKER, 0,
			V4L2_JPEG_ACTIVE_MARKER_DQT |
			V4L2_JPEG_ACTIVE_MARKER_DHT, 0,
			V4L2_JPEG_ACTIVE_MARKER_DQT |
			V4L2_JPEG_ACTIVE_MARKER_DHT);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	v4l2_ctrl_new_custom(hdl, &go7007_pixel_threshold0_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_motion_threshold0_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_mb_threshold0_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_pixel_threshold1_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_motion_threshold1_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_mb_threshold1_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_pixel_threshold2_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_motion_threshold2_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_mb_threshold2_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_pixel_threshold3_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_motion_threshold3_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_mb_threshold3_ctrl, NULL);
	v4l2_ctrl_new_custom(hdl, &go7007_mb_regions_ctrl, NULL);
	go->modet_mode = v4l2_ctrl_new_std_menu(hdl, NULL,
			V4L2_CID_DETECT_MD_MODE,
			V4L2_DETECT_MD_MODE_REGION_GRID,
			1 << V4L2_DETECT_MD_MODE_THRESHOLD_GRID,
			V4L2_DETECT_MD_MODE_DISABLED);
	if (hdl->error) {
		int rv = hdl->error;

		v4l2_err(&go->v4l2_dev, "Could not register controls\n");
		return rv;
	}
	go->v4l2_dev.ctrl_handler = hdl;
	return 0;
}

int go7007_v4l2_init(struct go7007 *go)
{
	struct video_device *vdev = &go->vdev;
	int rv;

	mutex_init(&go->serialize_lock);
	mutex_init(&go->queue_lock);

	INIT_LIST_HEAD(&go->vidq_active);
	go->vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	go->vidq.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	go->vidq.ops = &go7007_video_qops;
	go->vidq.mem_ops = &vb2_vmalloc_memops;
	go->vidq.drv_priv = go;
	go->vidq.buf_struct_size = sizeof(struct go7007_buffer);
	go->vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	go->vidq.lock = &go->queue_lock;
	rv = vb2_queue_init(&go->vidq);
	if (rv)
		return rv;
	*vdev = go7007_template;
	vdev->lock = &go->serialize_lock;
	vdev->queue = &go->vidq;
	video_set_drvdata(vdev, go);
	vdev->v4l2_dev = &go->v4l2_dev;
	if (!v4l2_device_has_op(&go->v4l2_dev, 0, video, querystd))
		v4l2_disable_ioctl(vdev, VIDIOC_QUERYSTD);
	if (!(go->board_info->flags & GO7007_BOARD_HAS_TUNER)) {
		v4l2_disable_ioctl(vdev, VIDIOC_S_FREQUENCY);
		v4l2_disable_ioctl(vdev, VIDIOC_G_FREQUENCY);
		v4l2_disable_ioctl(vdev, VIDIOC_S_TUNER);
		v4l2_disable_ioctl(vdev, VIDIOC_G_TUNER);
	} else {
		struct v4l2_frequency f = {
			.type = V4L2_TUNER_ANALOG_TV,
			.frequency = 980,
		};

		call_all(&go->v4l2_dev, tuner, s_frequency, &f);
	}
	if (!(go->board_info->sensor_flags & GO7007_SENSOR_TV)) {
		v4l2_disable_ioctl(vdev, VIDIOC_G_STD);
		v4l2_disable_ioctl(vdev, VIDIOC_S_STD);
		vdev->tvnorms = 0;
	}
	if (go->board_info->sensor_flags & GO7007_SENSOR_SCALING)
		v4l2_disable_ioctl(vdev, VIDIOC_ENUM_FRAMESIZES);
	if (go->board_info->num_aud_inputs == 0) {
		v4l2_disable_ioctl(vdev, VIDIOC_G_AUDIO);
		v4l2_disable_ioctl(vdev, VIDIOC_S_AUDIO);
		v4l2_disable_ioctl(vdev, VIDIOC_ENUMAUDIO);
	}
	/* Setup correct crystal frequency on this board */
	if (go->board_info->sensor_flags & GO7007_SENSOR_SAA7115)
		v4l2_subdev_call(go->sd_video, video, s_crystal_freq,
				SAA7115_FREQ_24_576_MHZ,
				SAA7115_FREQ_FL_APLL | SAA7115_FREQ_FL_UCGC |
				SAA7115_FREQ_FL_DOUBLE_ASCLK);
	go7007_s_input(go);
	if (go->board_info->sensor_flags & GO7007_SENSOR_TV)
		go7007_s_std(go);
	rv = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (rv < 0)
		return rv;
	dev_info(go->dev, "registered device %s [v4l2]\n",
		 video_device_node_name(vdev));

	return 0;
}

void go7007_v4l2_remove(struct go7007 *go)
{
	v4l2_ctrl_handler_free(&go->hdl);
}
