// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 NextThing Co
 * Copyright (C) 2016-2019 Bootlin
 *
 * Author: Maxime Ripard <maxime.ripard@bootlin.com>
 */

#include <linux/device.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-v4l2.h>

#include "sun4i_csi.h"

#define CSI_DEFAULT_WIDTH	640
#define CSI_DEFAULT_HEIGHT	480

static const struct sun4i_csi_format sun4i_csi_formats[] = {
	/* YUV422 inputs */
	{
		.mbus		= MEDIA_BUS_FMT_YUYV8_2X8,
		.fourcc		= V4L2_PIX_FMT_YUV420M,
		.input		= CSI_INPUT_YUV,
		.output		= CSI_OUTPUT_YUV_420_PLANAR,
		.num_planes	= 3,
		.bpp		= { 8, 8, 8 },
		.hsub		= 2,
		.vsub		= 2,
	},
};

const struct sun4i_csi_format *sun4i_csi_find_format(const u32 *fourcc,
						     const u32 *mbus)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun4i_csi_formats); i++) {
		if (fourcc && *fourcc != sun4i_csi_formats[i].fourcc)
			continue;

		if (mbus && *mbus != sun4i_csi_formats[i].mbus)
			continue;

		return &sun4i_csi_formats[i];
	}

	return NULL;
}

static int sun4i_csi_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	struct sun4i_csi *csi = video_drvdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, "sun4i-csi", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(csi->dev));

	return 0;
}

static int sun4i_csi_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, "Camera", sizeof(inp->name));

	return 0;
}

static int sun4i_csi_g_input(struct file *file, void *fh,
			     unsigned int *i)
{
	*i = 0;

	return 0;
}

static int sun4i_csi_s_input(struct file *file, void *fh,
			     unsigned int i)
{
	if (i != 0)
		return -EINVAL;

	return 0;
}

static void _sun4i_csi_try_fmt(struct sun4i_csi *csi,
			       struct v4l2_pix_format_mplane *pix)
{
	const struct sun4i_csi_format *_fmt;
	unsigned int height, width;
	unsigned int i;

	_fmt = sun4i_csi_find_format(&pix->pixelformat, NULL);
	if (!_fmt)
		_fmt = &sun4i_csi_formats[0];

	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);
	pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	pix->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true, pix->colorspace,
							  pix->ycbcr_enc);

	pix->num_planes = _fmt->num_planes;
	pix->pixelformat = _fmt->fourcc;

	memset(pix->reserved, 0, sizeof(pix->reserved));

	/* Align the width and height on the subsampling */
	width = ALIGN(pix->width, _fmt->hsub);
	height = ALIGN(pix->height, _fmt->vsub);

	/* Clamp the width and height to our capabilities */
	pix->width = clamp(width, _fmt->hsub, CSI_MAX_WIDTH);
	pix->height = clamp(height, _fmt->vsub, CSI_MAX_HEIGHT);

	for (i = 0; i < _fmt->num_planes; i++) {
		unsigned int hsub = i > 0 ? _fmt->hsub : 1;
		unsigned int vsub = i > 0 ? _fmt->vsub : 1;
		unsigned int bpl;

		bpl = pix->width / hsub * _fmt->bpp[i] / 8;
		pix->plane_fmt[i].bytesperline = bpl;
		pix->plane_fmt[i].sizeimage = bpl * pix->height / vsub;
		memset(pix->plane_fmt[i].reserved, 0,
		       sizeof(pix->plane_fmt[i].reserved));
	}
}

static int sun4i_csi_try_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct sun4i_csi *csi = video_drvdata(file);

	_sun4i_csi_try_fmt(csi, &f->fmt.pix_mp);

	return 0;
}

static int sun4i_csi_s_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct sun4i_csi *csi = video_drvdata(file);

	_sun4i_csi_try_fmt(csi, &f->fmt.pix_mp);
	csi->fmt = f->fmt.pix_mp;

	return 0;
}

static int sun4i_csi_g_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct sun4i_csi *csi = video_drvdata(file);

	f->fmt.pix_mp = csi->fmt;

	return 0;
}

static int sun4i_csi_enum_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(sun4i_csi_formats))
		return -EINVAL;

	f->pixelformat = sun4i_csi_formats[f->index].fourcc;

	return 0;
}

static const struct v4l2_ioctl_ops sun4i_csi_ioctl_ops = {
	.vidioc_querycap		= sun4i_csi_querycap,

	.vidioc_enum_fmt_vid_cap	= sun4i_csi_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane	= sun4i_csi_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane	= sun4i_csi_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap_mplane	= sun4i_csi_try_fmt_vid_cap,

	.vidioc_enum_input		= sun4i_csi_enum_input,
	.vidioc_g_input			= sun4i_csi_g_input,
	.vidioc_s_input			= sun4i_csi_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

static int sun4i_csi_open(struct file *file)
{
	struct sun4i_csi *csi = video_drvdata(file);
	int ret;

	ret = mutex_lock_interruptible(&csi->lock);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(csi->dev);
	if (ret < 0)
		goto err_pm_put;

	ret = v4l2_pipeline_pm_get(&csi->vdev.entity);
	if (ret)
		goto err_pm_put;

	ret = v4l2_fh_open(file);
	if (ret)
		goto err_pipeline_pm_put;

	mutex_unlock(&csi->lock);

	return 0;

err_pipeline_pm_put:
	v4l2_pipeline_pm_put(&csi->vdev.entity);

err_pm_put:
	pm_runtime_put(csi->dev);
	mutex_unlock(&csi->lock);

	return ret;
}

static int sun4i_csi_release(struct file *file)
{
	struct sun4i_csi *csi = video_drvdata(file);

	mutex_lock(&csi->lock);

	v4l2_fh_release(file);
	v4l2_pipeline_pm_put(&csi->vdev.entity);
	pm_runtime_put(csi->dev);

	mutex_unlock(&csi->lock);

	return 0;
}

static const struct v4l2_file_operations sun4i_csi_fops = {
	.owner		= THIS_MODULE,
	.open		= sun4i_csi_open,
	.release	= sun4i_csi_release,
	.unlocked_ioctl	= video_ioctl2,
	.read		= vb2_fop_read,
	.write		= vb2_fop_write,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
};

static const struct v4l2_mbus_framefmt sun4i_csi_pad_fmt_default = {
	.width = CSI_DEFAULT_WIDTH,
	.height = CSI_DEFAULT_HEIGHT,
	.code = MEDIA_BUS_FMT_YUYV8_2X8,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
	.quantization = V4L2_QUANTIZATION_DEFAULT,
	.xfer_func = V4L2_XFER_FUNC_DEFAULT,
};

static int sun4i_csi_subdev_init_cfg(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_get_try_format(subdev, cfg, CSI_SUBDEV_SINK);
	*fmt = sun4i_csi_pad_fmt_default;

	return 0;
}

static int sun4i_csi_subdev_get_fmt(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *fmt)
{
	struct sun4i_csi *csi = container_of(subdev, struct sun4i_csi, subdev);
	struct v4l2_mbus_framefmt *subdev_fmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		subdev_fmt = v4l2_subdev_get_try_format(subdev, cfg, fmt->pad);
	else
		subdev_fmt = &csi->subdev_fmt;

	fmt->format = *subdev_fmt;

	return 0;
}

static int sun4i_csi_subdev_set_fmt(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *fmt)
{
	struct sun4i_csi *csi = container_of(subdev, struct sun4i_csi, subdev);
	struct v4l2_mbus_framefmt *subdev_fmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		subdev_fmt = v4l2_subdev_get_try_format(subdev, cfg, fmt->pad);
	else
		subdev_fmt = &csi->subdev_fmt;

	/* We can only set the format on the sink pad */
	if (fmt->pad == CSI_SUBDEV_SINK) {
		/* It's the sink, only allow changing the frame size */
		subdev_fmt->width = fmt->format.width;
		subdev_fmt->height = fmt->format.height;
		subdev_fmt->code = fmt->format.code;
	}

	fmt->format = *subdev_fmt;

	return 0;
}

static int
sun4i_csi_subdev_enum_mbus_code(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *mbus)
{
	if (mbus->index >= ARRAY_SIZE(sun4i_csi_formats))
		return -EINVAL;

	mbus->code = sun4i_csi_formats[mbus->index].mbus;

	return 0;
}

static const struct v4l2_subdev_pad_ops sun4i_csi_subdev_pad_ops = {
	.link_validate	= v4l2_subdev_link_validate_default,
	.init_cfg	= sun4i_csi_subdev_init_cfg,
	.get_fmt	= sun4i_csi_subdev_get_fmt,
	.set_fmt	= sun4i_csi_subdev_set_fmt,
	.enum_mbus_code	= sun4i_csi_subdev_enum_mbus_code,
};

const struct v4l2_subdev_ops sun4i_csi_subdev_ops = {
	.pad = &sun4i_csi_subdev_pad_ops,
};

int sun4i_csi_v4l2_register(struct sun4i_csi *csi)
{
	struct video_device *vdev = &csi->vdev;
	int ret;

	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING;
	vdev->v4l2_dev = &csi->v4l;
	vdev->queue = &csi->queue;
	strscpy(vdev->name, KBUILD_MODNAME, sizeof(vdev->name));
	vdev->release = video_device_release_empty;
	vdev->lock = &csi->lock;

	/* Set a default format */
	csi->fmt.pixelformat = sun4i_csi_formats[0].fourcc,
	csi->fmt.width = CSI_DEFAULT_WIDTH;
	csi->fmt.height = CSI_DEFAULT_HEIGHT;
	_sun4i_csi_try_fmt(csi, &csi->fmt);
	csi->subdev_fmt = sun4i_csi_pad_fmt_default;

	vdev->fops = &sun4i_csi_fops;
	vdev->ioctl_ops = &sun4i_csi_ioctl_ops;
	video_set_drvdata(vdev, csi);

	ret = video_register_device(&csi->vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		return ret;

	dev_info(csi->dev, "Device registered as %s\n",
		 video_device_node_name(vdev));

	return 0;
}
