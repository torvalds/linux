// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include <soc/tegra/pmc.h>

#include "vi.h"
#include "video.h"

#define SURFACE_ALIGN_BYTES		64
#define MAX_CID_CONTROLS		1

static const struct tegra_video_format tegra_default_format = {
	.img_dt = TEGRA_IMAGE_DT_RAW10,
	.bit_width = 10,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.bpp = 2,
	.img_fmt = TEGRA_IMAGE_FORMAT_DEF,
	.fourcc = V4L2_PIX_FMT_SRGGB10,
};

static inline struct tegra_vi *
host1x_client_to_vi(struct host1x_client *client)
{
	return container_of(client, struct tegra_vi, client);
}

static inline struct tegra_channel_buffer *
to_tegra_channel_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct tegra_channel_buffer, buf);
}

static int tegra_get_format_idx_by_code(struct tegra_vi *vi,
					unsigned int code)
{
	unsigned int i;

	for (i = 0; i < vi->soc->nformats; ++i) {
		if (vi->soc->video_formats[i].code == code)
			return i;
	}

	return -1;
}

static u32 tegra_get_format_fourcc_by_idx(struct tegra_vi *vi,
					  unsigned int index)
{
	if (index >= vi->soc->nformats)
		return -EINVAL;

	return vi->soc->video_formats[index].fourcc;
}

static const struct tegra_video_format *
tegra_get_format_by_fourcc(struct tegra_vi *vi, u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < vi->soc->nformats; ++i) {
		if (vi->soc->video_formats[i].fourcc == fourcc)
			return &vi->soc->video_formats[i];
	}

	return NULL;
}

/*
 * videobuf2 queue operations
 */
static int tegra_channel_queue_setup(struct vb2_queue *vq,
				     unsigned int *nbuffers,
				     unsigned int *nplanes,
				     unsigned int sizes[],
				     struct device *alloc_devs[])
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);

	if (*nplanes)
		return sizes[0] < chan->format.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = chan->format.sizeimage;
	alloc_devs[0] = chan->vi->dev;

	return 0;
}

static int tegra_channel_buffer_prepare(struct vb2_buffer *vb)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct tegra_channel_buffer *buf = to_tegra_channel_buffer(vbuf);
	unsigned long size = chan->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		v4l2_err(chan->video.v4l2_dev,
			 "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	buf->chan = chan;
	buf->addr = vb2_dma_contig_plane_dma_addr(vb, 0);

	return 0;
}

static void tegra_channel_buffer_queue(struct vb2_buffer *vb)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct tegra_channel_buffer *buf = to_tegra_channel_buffer(vbuf);

	/* put buffer into the capture queue */
	spin_lock(&chan->start_lock);
	list_add_tail(&buf->queue, &chan->capture);
	spin_unlock(&chan->start_lock);

	/* wait up kthread for capture */
	wake_up_interruptible(&chan->start_wait);
}

struct v4l2_subdev *
tegra_channel_get_remote_subdev(struct tegra_vi_channel *chan)
{
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct media_entity *entity;

	pad = media_entity_remote_pad(&chan->pad);
	entity = pad->entity;
	subdev = media_entity_to_v4l2_subdev(entity);

	return subdev;
}

int tegra_channel_set_stream(struct tegra_vi_channel *chan, bool on)
{
	struct v4l2_subdev *subdev;
	int ret;

	/* stream CSI */
	subdev = tegra_channel_get_remote_subdev(chan);
	ret = v4l2_subdev_call(subdev, video, s_stream, on);
	if (on && ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	return 0;
}

void tegra_channel_release_buffers(struct tegra_vi_channel *chan,
				   enum vb2_buffer_state state)
{
	struct tegra_channel_buffer *buf, *nbuf;

	spin_lock(&chan->start_lock);
	list_for_each_entry_safe(buf, nbuf, &chan->capture, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, state);
		list_del(&buf->queue);
	}
	spin_unlock(&chan->start_lock);

	spin_lock(&chan->done_lock);
	list_for_each_entry_safe(buf, nbuf, &chan->done, queue) {
		vb2_buffer_done(&buf->buf.vb2_buf, state);
		list_del(&buf->queue);
	}
	spin_unlock(&chan->done_lock);
}

static int tegra_channel_start_streaming(struct vb2_queue *vq, u32 count)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);
	int ret;

	ret = pm_runtime_get_sync(chan->vi->dev);
	if (ret < 0) {
		dev_err(chan->vi->dev, "failed to get runtime PM: %d\n", ret);
		pm_runtime_put_noidle(chan->vi->dev);
		return ret;
	}

	ret = chan->vi->ops->vi_start_streaming(vq, count);
	if (ret < 0)
		pm_runtime_put(chan->vi->dev);

	return ret;
}

static void tegra_channel_stop_streaming(struct vb2_queue *vq)
{
	struct tegra_vi_channel *chan = vb2_get_drv_priv(vq);

	chan->vi->ops->vi_stop_streaming(vq);
	pm_runtime_put(chan->vi->dev);
}

static const struct vb2_ops tegra_channel_queue_qops = {
	.queue_setup = tegra_channel_queue_setup,
	.buf_prepare = tegra_channel_buffer_prepare,
	.buf_queue = tegra_channel_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = tegra_channel_start_streaming,
	.stop_streaming = tegra_channel_stop_streaming,
};

/*
 * V4L2 ioctl operations
 */
static int tegra_channel_querycap(struct file *file, void *fh,
				  struct v4l2_capability *cap)
{
	struct tegra_vi_channel *chan = video_drvdata(file);

	strscpy(cap->driver, "tegra-video", sizeof(cap->driver));
	strscpy(cap->card, chan->video.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(chan->vi->dev));

	return 0;
}

static int tegra_channel_g_parm(struct file *file, void *fh,
				struct v4l2_streamparm *a)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_subdev(chan);
	return v4l2_g_parm_cap(&chan->video, subdev, a);
}

static int tegra_channel_s_parm(struct file *file, void *fh,
				struct v4l2_streamparm *a)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_subdev(chan);
	return v4l2_s_parm_cap(&chan->video, subdev, a);
}

static int tegra_channel_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *sizes)
{
	int ret;
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;
	const struct tegra_video_format *fmtinfo;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = sizes->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	fmtinfo = tegra_get_format_by_fourcc(chan->vi, sizes->pixel_format);
	if (!fmtinfo)
		return -EINVAL;

	fse.code = fmtinfo->code;

	subdev = tegra_channel_get_remote_subdev(chan);
	ret = v4l2_subdev_call(subdev, pad, enum_frame_size, NULL, &fse);
	if (ret)
		return ret;

	sizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	sizes->discrete.width = fse.max_width;
	sizes->discrete.height = fse.max_height;

	return 0;
}

static int tegra_channel_enum_frameintervals(struct file *file, void *fh,
					     struct v4l2_frmivalenum *ivals)
{
	int ret;
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;
	const struct tegra_video_format *fmtinfo;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = ivals->index,
		.width = ivals->width,
		.height = ivals->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	fmtinfo = tegra_get_format_by_fourcc(chan->vi, ivals->pixel_format);
	if (!fmtinfo)
		return -EINVAL;

	fie.code = fmtinfo->code;

	subdev = tegra_channel_get_remote_subdev(chan);
	ret = v4l2_subdev_call(subdev, pad, enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;

	ivals->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	ivals->discrete.numerator = fie.interval.numerator;
	ivals->discrete.denominator = fie.interval.denominator;

	return 0;
}

static int tegra_channel_enum_format(struct file *file, void *fh,
				     struct v4l2_fmtdesc *f)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	unsigned int index = 0, i;
	unsigned long *fmts_bitmap = chan->tpg_fmts_bitmap;

	if (f->index >= bitmap_weight(fmts_bitmap, MAX_FORMAT_NUM))
		return -EINVAL;

	for (i = 0; i < f->index + 1; i++, index++)
		index = find_next_bit(fmts_bitmap, MAX_FORMAT_NUM, index);

	f->pixelformat = tegra_get_format_fourcc_by_idx(chan->vi, index - 1);

	return 0;
}

static int tegra_channel_get_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct tegra_vi_channel *chan = video_drvdata(file);

	format->fmt.pix = chan->format;

	return 0;
}

static void tegra_channel_fmt_align(struct tegra_vi_channel *chan,
				    struct v4l2_pix_format *pix,
				    unsigned int bpp)
{
	unsigned int align;
	unsigned int min_width;
	unsigned int max_width;
	unsigned int width;
	unsigned int min_bpl;
	unsigned int max_bpl;
	unsigned int bpl;

	/*
	 * The transfer alignment requirements are expressed in bytes. Compute
	 * minimum and maximum values, clamp the requested width and convert
	 * it back to pixels. Use bytesperline to adjust the width.
	 */
	align = lcm(SURFACE_ALIGN_BYTES, bpp);
	min_width = roundup(TEGRA_MIN_WIDTH, align);
	max_width = rounddown(TEGRA_MAX_WIDTH, align);
	width = roundup(pix->width * bpp, align);

	pix->width = clamp(width, min_width, max_width) / bpp;
	pix->height = clamp(pix->height, TEGRA_MIN_HEIGHT, TEGRA_MAX_HEIGHT);

	/* Clamp the requested bytes per line value. If the maximum bytes per
	 * line value is zero, the module doesn't support user configurable
	 * line sizes. Override the requested value with the minimum in that
	 * case.
	 */
	min_bpl = pix->width * bpp;
	max_bpl = rounddown(TEGRA_MAX_WIDTH, SURFACE_ALIGN_BYTES);
	bpl = roundup(pix->bytesperline, SURFACE_ALIGN_BYTES);

	pix->bytesperline = clamp(bpl, min_bpl, max_bpl);
	pix->sizeimage = pix->bytesperline * pix->height;
}

static int __tegra_channel_try_format(struct tegra_vi_channel *chan,
				      struct v4l2_pix_format *pix)
{
	const struct tegra_video_format *fmtinfo;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev_pad_config *pad_cfg;

	subdev = tegra_channel_get_remote_subdev(chan);
	pad_cfg = v4l2_subdev_alloc_pad_config(subdev);
	if (!pad_cfg)
		return -ENOMEM;
	/*
	 * Retrieve the format information and if requested format isn't
	 * supported, keep the current format.
	 */
	fmtinfo = tegra_get_format_by_fourcc(chan->vi, pix->pixelformat);
	if (!fmtinfo) {
		pix->pixelformat = chan->format.pixelformat;
		pix->colorspace = chan->format.colorspace;
		fmtinfo = tegra_get_format_by_fourcc(chan->vi,
						     pix->pixelformat);
	}

	pix->field = V4L2_FIELD_NONE;
	fmt.which = V4L2_SUBDEV_FORMAT_TRY;
	fmt.pad = 0;
	v4l2_fill_mbus_format(&fmt.format, pix, fmtinfo->code);
	v4l2_subdev_call(subdev, pad, set_fmt, pad_cfg, &fmt);
	v4l2_fill_pix_format(pix, &fmt.format);
	tegra_channel_fmt_align(chan, pix, fmtinfo->bpp);

	v4l2_subdev_free_pad_config(pad_cfg);

	return 0;
}

static int tegra_channel_try_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct tegra_vi_channel *chan = video_drvdata(file);

	return __tegra_channel_try_format(chan, &format->fmt.pix);
}

static int tegra_channel_set_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	const struct tegra_video_format *fmtinfo;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	struct v4l2_pix_format *pix = &format->fmt.pix;
	int ret;

	if (vb2_is_busy(&chan->queue))
		return -EBUSY;

	/* get supported format by try_fmt */
	ret = __tegra_channel_try_format(chan, pix);
	if (ret)
		return ret;

	fmtinfo = tegra_get_format_by_fourcc(chan->vi, pix->pixelformat);

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.pad = 0;
	v4l2_fill_mbus_format(&fmt.format, pix, fmtinfo->code);
	subdev = tegra_channel_get_remote_subdev(chan);
	v4l2_subdev_call(subdev, pad, set_fmt, NULL, &fmt);
	v4l2_fill_pix_format(pix, &fmt.format);
	tegra_channel_fmt_align(chan, pix, fmtinfo->bpp);

	chan->format = *pix;
	chan->fmtinfo = fmtinfo;

	return 0;
}

static int tegra_channel_enum_input(struct file *file, void *fh,
				    struct v4l2_input *inp)
{
	/* currently driver supports internal TPG only */
	if (inp->index)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, "Tegra TPG", sizeof(inp->name));

	return 0;
}

static int tegra_channel_g_input(struct file *file, void *priv,
				 unsigned int *i)
{
	*i = 0;

	return 0;
}

static int tegra_channel_s_input(struct file *file, void *priv,
				 unsigned int input)
{
	if (input > 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops tegra_channel_ioctl_ops = {
	.vidioc_querycap		= tegra_channel_querycap,
	.vidioc_g_parm			= tegra_channel_g_parm,
	.vidioc_s_parm			= tegra_channel_s_parm,
	.vidioc_enum_framesizes		= tegra_channel_enum_framesizes,
	.vidioc_enum_frameintervals	= tegra_channel_enum_frameintervals,
	.vidioc_enum_fmt_vid_cap	= tegra_channel_enum_format,
	.vidioc_g_fmt_vid_cap		= tegra_channel_get_format,
	.vidioc_s_fmt_vid_cap		= tegra_channel_set_format,
	.vidioc_try_fmt_vid_cap		= tegra_channel_try_format,
	.vidioc_enum_input		= tegra_channel_enum_input,
	.vidioc_g_input			= tegra_channel_g_input,
	.vidioc_s_input			= tegra_channel_s_input,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*
 * V4L2 file operations
 */
static const struct v4l2_file_operations tegra_channel_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read		= vb2_fop_read,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
};

/*
 * V4L2 control operations
 */
static int vi_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tegra_vi_channel *chan = container_of(ctrl->handler,
						     struct tegra_vi_channel,
						     ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		/* pattern change takes effect on next stream */
		chan->pg_mode = ctrl->val + 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops vi_ctrl_ops = {
	.s_ctrl	= vi_s_ctrl,
};

static const char *const vi_pattern_strings[] = {
	"Black/White Direct Mode",
	"Color Patch Mode",
};

static int tegra_channel_setup_ctrl_handler(struct tegra_vi_channel *chan)
{
	int ret;

	/* add test pattern control handler to v4l2 device */
	v4l2_ctrl_new_std_menu_items(&chan->ctrl_handler, &vi_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(vi_pattern_strings) - 1,
				     0, 0, vi_pattern_strings);
	if (chan->ctrl_handler.error) {
		dev_err(chan->vi->dev, "failed to add TPG ctrl handler: %d\n",
			chan->ctrl_handler.error);
		v4l2_ctrl_handler_free(&chan->ctrl_handler);
		return chan->ctrl_handler.error;
	}

	/* setup the controls */
	ret = v4l2_ctrl_handler_setup(&chan->ctrl_handler);
	if (ret < 0) {
		dev_err(chan->vi->dev,
			"failed to setup v4l2 ctrl handler: %d\n", ret);
		return ret;
	}

	return 0;
}

/* VI only support 2 formats in TPG mode */
static void vi_tpg_fmts_bitmap_init(struct tegra_vi_channel *chan)
{
	int index;

	bitmap_zero(chan->tpg_fmts_bitmap, MAX_FORMAT_NUM);

	index = tegra_get_format_idx_by_code(chan->vi,
					     MEDIA_BUS_FMT_SRGGB10_1X10);
	bitmap_set(chan->tpg_fmts_bitmap, index, 1);

	index = tegra_get_format_idx_by_code(chan->vi,
					     MEDIA_BUS_FMT_RGB888_1X32_PADHI);
	bitmap_set(chan->tpg_fmts_bitmap, index, 1);
}

static void tegra_channel_cleanup(struct tegra_vi_channel *chan)
{
	v4l2_ctrl_handler_free(&chan->ctrl_handler);
	media_entity_cleanup(&chan->video.entity);
	host1x_syncpt_free(chan->mw_ack_sp);
	host1x_syncpt_free(chan->frame_start_sp);
	mutex_destroy(&chan->video_lock);
}

void tegra_channels_cleanup(struct tegra_vi *vi)
{
	struct tegra_vi_channel *chan, *tmp;

	if (!vi)
		return;

	list_for_each_entry_safe(chan, tmp, &vi->vi_chans, list) {
		tegra_channel_cleanup(chan);
		list_del(&chan->list);
		kfree(chan);
	}
}

static int tegra_channel_init(struct tegra_vi_channel *chan)
{
	struct tegra_vi *vi = chan->vi;
	struct tegra_video_device *vid = dev_get_drvdata(vi->client.host);
	unsigned long flags = HOST1X_SYNCPT_CLIENT_MANAGED;
	int ret;

	mutex_init(&chan->video_lock);
	INIT_LIST_HEAD(&chan->capture);
	INIT_LIST_HEAD(&chan->done);
	spin_lock_init(&chan->start_lock);
	spin_lock_init(&chan->done_lock);
	spin_lock_init(&chan->sp_incr_lock);
	init_waitqueue_head(&chan->start_wait);
	init_waitqueue_head(&chan->done_wait);

	/* initialize the video format */
	chan->fmtinfo = &tegra_default_format;
	chan->format.pixelformat = chan->fmtinfo->fourcc;
	chan->format.colorspace = V4L2_COLORSPACE_SRGB;
	chan->format.field = V4L2_FIELD_NONE;
	chan->format.width = TEGRA_DEF_WIDTH;
	chan->format.height = TEGRA_DEF_HEIGHT;
	chan->format.bytesperline = TEGRA_DEF_WIDTH * chan->fmtinfo->bpp;
	chan->format.sizeimage = chan->format.bytesperline * TEGRA_DEF_HEIGHT;
	tegra_channel_fmt_align(chan, &chan->format, chan->fmtinfo->bpp);

	chan->frame_start_sp = host1x_syncpt_request(&vi->client, flags);
	if (!chan->frame_start_sp) {
		dev_err(vi->dev, "failed to request frame start syncpoint\n");
		return -ENOMEM;
	}

	chan->mw_ack_sp = host1x_syncpt_request(&vi->client, flags);
	if (!chan->mw_ack_sp) {
		dev_err(vi->dev, "failed to request memory ack syncpoint\n");
		ret = -ENOMEM;
		goto free_fs_syncpt;
	}

	/* initialize the media entity */
	chan->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&chan->video.entity, 1, &chan->pad);
	if (ret < 0) {
		dev_err(vi->dev,
			"failed to initialize media entity: %d\n", ret);
		goto free_mw_ack_syncpt;
	}

	ret = v4l2_ctrl_handler_init(&chan->ctrl_handler, MAX_CID_CONTROLS);
	if (chan->ctrl_handler.error) {
		dev_err(vi->dev,
			"failed to initialize v4l2 ctrl handler: %d\n", ret);
		goto cleanup_media;
	}

	/* initialize the video_device */
	chan->video.fops = &tegra_channel_fops;
	chan->video.v4l2_dev = &vid->v4l2_dev;
	chan->video.release = video_device_release_empty;
	chan->video.queue = &chan->queue;
	snprintf(chan->video.name, sizeof(chan->video.name), "%s-%s-%u",
		 dev_name(vi->dev), "output", chan->portno);
	chan->video.vfl_type = VFL_TYPE_VIDEO;
	chan->video.vfl_dir = VFL_DIR_RX;
	chan->video.ioctl_ops = &tegra_channel_ioctl_ops;
	chan->video.ctrl_handler = &chan->ctrl_handler;
	chan->video.lock = &chan->video_lock;
	chan->video.device_caps = V4L2_CAP_VIDEO_CAPTURE |
				  V4L2_CAP_STREAMING |
				  V4L2_CAP_READWRITE;
	video_set_drvdata(&chan->video, chan);

	chan->queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	chan->queue.io_modes = VB2_MMAP | VB2_DMABUF | VB2_READ;
	chan->queue.lock = &chan->video_lock;
	chan->queue.drv_priv = chan;
	chan->queue.buf_struct_size = sizeof(struct tegra_channel_buffer);
	chan->queue.ops = &tegra_channel_queue_qops;
	chan->queue.mem_ops = &vb2_dma_contig_memops;
	chan->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	chan->queue.min_buffers_needed = 2;
	chan->queue.dev = vi->dev;
	ret = vb2_queue_init(&chan->queue);
	if (ret < 0) {
		dev_err(vi->dev, "failed to initialize vb2 queue: %d\n", ret);
		goto free_v4l2_ctrl_hdl;
	}

	return 0;

free_v4l2_ctrl_hdl:
	v4l2_ctrl_handler_free(&chan->ctrl_handler);
cleanup_media:
	media_entity_cleanup(&chan->video.entity);
free_mw_ack_syncpt:
	host1x_syncpt_free(chan->mw_ack_sp);
free_fs_syncpt:
	host1x_syncpt_free(chan->frame_start_sp);
	return ret;
}

static int tegra_vi_tpg_channels_alloc(struct tegra_vi *vi)
{
	struct tegra_vi_channel *chan;
	unsigned int port_num;
	unsigned int nchannels = vi->soc->vi_max_channels;

	for (port_num = 0; port_num < nchannels; port_num++) {
		/*
		 * Do not use devm_kzalloc as memory is freed immediately
		 * when device instance is unbound but application might still
		 * be holding the device node open. Channel memory allocated
		 * with kzalloc is freed during video device release callback.
		 */
		chan = kzalloc(sizeof(*chan), GFP_KERNEL);
		if (!chan)
			return -ENOMEM;

		chan->vi = vi;
		chan->portno = port_num;
		list_add_tail(&chan->list, &vi->vi_chans);
	}

	return 0;
}

static int tegra_vi_channels_init(struct tegra_vi *vi)
{
	struct tegra_vi_channel *chan;
	int ret;

	list_for_each_entry(chan, &vi->vi_chans, list) {
		ret = tegra_channel_init(chan);
		if (ret < 0) {
			dev_err(vi->dev,
				"failed to initialize channel-%d: %d\n",
				chan->portno, ret);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	list_for_each_entry_continue_reverse(chan, &vi->vi_chans, list)
		tegra_channel_cleanup(chan);

	return ret;
}

void tegra_v4l2_nodes_cleanup_tpg(struct tegra_video_device *vid)
{
	struct tegra_vi *vi = vid->vi;
	struct tegra_csi *csi = vid->csi;
	struct tegra_csi_channel *csi_chan;
	struct tegra_vi_channel *chan;

	list_for_each_entry(chan, &vi->vi_chans, list) {
		video_unregister_device(&chan->video);
		mutex_lock(&chan->video_lock);
		vb2_queue_release(&chan->queue);
		mutex_unlock(&chan->video_lock);
	}

	list_for_each_entry(csi_chan, &csi->csi_chans, list)
		v4l2_device_unregister_subdev(&csi_chan->subdev);
}

int tegra_v4l2_nodes_setup_tpg(struct tegra_video_device *vid)
{
	struct tegra_vi *vi = vid->vi;
	struct tegra_csi *csi = vid->csi;
	struct tegra_vi_channel *vi_chan;
	struct tegra_csi_channel *csi_chan;
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	int ret;

	if (!vi || !csi)
		return -ENODEV;

	csi_chan = list_first_entry(&csi->csi_chans,
				    struct tegra_csi_channel, list);

	list_for_each_entry(vi_chan, &vi->vi_chans, list) {
		struct media_entity *source = &csi_chan->subdev.entity;
		struct media_entity *sink = &vi_chan->video.entity;
		struct media_pad *source_pad = csi_chan->pads;
		struct media_pad *sink_pad = &vi_chan->pad;

		ret = v4l2_device_register_subdev(&vid->v4l2_dev,
						  &csi_chan->subdev);
		if (ret) {
			dev_err(vi->dev,
				"failed to register subdev: %d\n", ret);
			goto cleanup;
		}

		ret = video_register_device(&vi_chan->video,
					    VFL_TYPE_VIDEO, -1);
		if (ret < 0) {
			dev_err(vi->dev,
				"failed to register video device: %d\n", ret);
			goto cleanup;
		}

		dev_dbg(vi->dev, "creating %s:%u -> %s:%u link\n",
			source->name, source_pad->index,
			sink->name, sink_pad->index);

		ret = media_create_pad_link(source, source_pad->index,
					    sink, sink_pad->index,
					    link_flags);
		if (ret < 0) {
			dev_err(vi->dev,
				"failed to create %s:%u -> %s:%u link: %d\n",
				source->name, source_pad->index,
				sink->name, sink_pad->index, ret);
			goto cleanup;
		}

		ret = tegra_channel_setup_ctrl_handler(vi_chan);
		if (ret < 0)
			goto cleanup;

		v4l2_set_subdev_hostdata(&csi_chan->subdev, vi_chan);
		vi_tpg_fmts_bitmap_init(vi_chan);
		csi_chan = list_next_entry(csi_chan, list);
	}

	return 0;

cleanup:
	tegra_v4l2_nodes_cleanup_tpg(vid);
	return ret;
}

static int __maybe_unused vi_runtime_resume(struct device *dev)
{
	struct tegra_vi *vi = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(vi->vdd);
	if (ret) {
		dev_err(dev, "failed to enable VDD supply: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(vi->clk, vi->soc->vi_max_clk_hz);
	if (ret) {
		dev_err(dev, "failed to set vi clock rate: %d\n", ret);
		goto disable_vdd;
	}

	ret = clk_prepare_enable(vi->clk);
	if (ret) {
		dev_err(dev, "failed to enable vi clock: %d\n", ret);
		goto disable_vdd;
	}

	return 0;

disable_vdd:
	regulator_disable(vi->vdd);
	return ret;
}

static int __maybe_unused vi_runtime_suspend(struct device *dev)
{
	struct tegra_vi *vi = dev_get_drvdata(dev);

	clk_disable_unprepare(vi->clk);

	regulator_disable(vi->vdd);

	return 0;
}

static int tegra_vi_init(struct host1x_client *client)
{
	struct tegra_video_device *vid = dev_get_drvdata(client->host);
	struct tegra_vi *vi = host1x_client_to_vi(client);
	struct tegra_vi_channel *chan, *tmp;
	int ret;

	vid->media_dev.hw_revision = vi->soc->hw_revision;
	snprintf(vid->media_dev.bus_info, sizeof(vid->media_dev.bus_info),
		 "platform:%s", dev_name(vi->dev));

	INIT_LIST_HEAD(&vi->vi_chans);

	ret = tegra_vi_tpg_channels_alloc(vi);
	if (ret < 0) {
		dev_err(vi->dev, "failed to allocate tpg channels: %d\n", ret);
		goto free_chans;
	}

	ret = tegra_vi_channels_init(vi);
	if (ret < 0)
		goto free_chans;

	vid->vi = vi;

	return 0;

free_chans:
	list_for_each_entry_safe(chan, tmp, &vi->vi_chans, list) {
		list_del(&chan->list);
		kfree(chan);
	}

	return ret;
}

static int tegra_vi_exit(struct host1x_client *client)
{
	/*
	 * Do not cleanup the channels here as application might still be
	 * holding video device nodes. Channels cleanup will happen during
	 * v4l2_device release callback which gets called after all video
	 * device nodes are released.
	 */

	return 0;
}

static const struct host1x_client_ops vi_client_ops = {
	.init = tegra_vi_init,
	.exit = tegra_vi_exit,
};

static int tegra_vi_probe(struct platform_device *pdev)
{
	struct tegra_vi *vi;
	int ret;

	vi = devm_kzalloc(&pdev->dev, sizeof(*vi), GFP_KERNEL);
	if (!vi)
		return -ENOMEM;

	vi->iomem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vi->iomem))
		return PTR_ERR(vi->iomem);

	vi->soc = of_device_get_match_data(&pdev->dev);

	vi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(vi->clk)) {
		ret = PTR_ERR(vi->clk);
		dev_err(&pdev->dev, "failed to get vi clock: %d\n", ret);
		return ret;
	}

	vi->vdd = devm_regulator_get(&pdev->dev, "avdd-dsi-csi");
	if (IS_ERR(vi->vdd)) {
		ret = PTR_ERR(vi->vdd);
		dev_err(&pdev->dev, "failed to get VDD supply: %d\n", ret);
		return ret;
	}

	if (!pdev->dev.pm_domain) {
		ret = -ENOENT;
		dev_warn(&pdev->dev, "PM domain is not attached: %d\n", ret);
		return ret;
	}

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to populate vi child device: %d\n", ret);
		return ret;
	}

	vi->dev = &pdev->dev;
	vi->ops = vi->soc->ops;
	platform_set_drvdata(pdev, vi);
	pm_runtime_enable(&pdev->dev);

	/* initialize host1x interface */
	INIT_LIST_HEAD(&vi->client.list);
	vi->client.ops = &vi_client_ops;
	vi->client.dev = &pdev->dev;

	ret = host1x_client_register(&vi->client);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to register host1x client: %d\n", ret);
		goto rpm_disable;
	}

	return 0;

rpm_disable:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int tegra_vi_remove(struct platform_device *pdev)
{
	struct tegra_vi *vi = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&vi->client);
	if (err < 0) {
		dev_err(&pdev->dev,
			"failed to unregister host1x client: %d\n", err);
		return err;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id tegra_vi_of_id_table[] = {
#if defined(CONFIG_ARCH_TEGRA_210_SOC)
	{ .compatible = "nvidia,tegra210-vi", .data = &tegra210_vi_soc },
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_vi_of_id_table);

static const struct dev_pm_ops tegra_vi_pm_ops = {
	SET_RUNTIME_PM_OPS(vi_runtime_suspend, vi_runtime_resume, NULL)
};

struct platform_driver tegra_vi_driver = {
	.driver = {
		.name = "tegra-vi",
		.of_match_table = tegra_vi_of_id_table,
		.pm = &tegra_vi_pm_ops,
	},
	.probe = tegra_vi_probe,
	.remove = tegra_vi_remove,
};
