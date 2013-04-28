/*
 * Samsung TV Mixer driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Tomasz Stanislawski, <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation. either version 2 of the License,
 * or (at your option) any later version
 */
#include "mixer.h"

#include <linux/videodev2.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/timer.h>

#include <media/exynos_mc.h>
#include <media/v4l2-ioctl.h>
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif

#include <media/videobuf2-fb.h>

int __devinit mxr_acquire_video(struct mxr_device *mdev,
	struct mxr_output_conf *output_conf, int output_count)
{
	int i;
	int ret = 0;
	struct v4l2_subdev *sd;

	mdev->alloc_ctx = mdev->vb2->init(mdev);
	if (IS_ERR_OR_NULL(mdev->alloc_ctx)) {
		mxr_err(mdev, "could not acquire vb2 allocator\n");
		ret = PTR_ERR(mdev->alloc_ctx);
		goto fail;
	}

	/* registering outputs */
	mdev->output_cnt = 0;
	for (i = 0; i < output_count; ++i) {
		struct mxr_output_conf *conf = &output_conf[i];
		struct mxr_output *out;

		/* find subdev of output devices */
		sd = (struct v4l2_subdev *)
			module_name_to_driver_data(conf->module_name);
		/* trying to register next output */
		if (sd == NULL)
			continue;
		out = kzalloc(sizeof *out, GFP_KERNEL);
		if (out == NULL) {
			mxr_err(mdev, "no memory for '%s'\n",
				conf->output_name);
			ret = -ENOMEM;
			/* registered subdevs are removed in fail_v4l2_dev */
			goto fail_output;
		}
		strlcpy(out->name, conf->output_name, sizeof(out->name));
		out->sd = sd;
		out->cookie = conf->cookie;
		mdev->output[mdev->output_cnt++] = out;
		mxr_info(mdev, "added output '%s' from module '%s'\n",
			conf->output_name, conf->module_name);
		/* checking if maximal number of outputs is reached */
		if (mdev->output_cnt >= MXR_MAX_OUTPUTS)
			break;
	}

	if (mdev->output_cnt == 0) {
		mxr_err(mdev, "failed to register any output\n");
		ret = -ENODEV;
		/* skipping fail_output because there is nothing to free */
		goto fail_vb2_allocator;
	}

	return 0;

fail_output:
	/* kfree is NULL-safe */
	for (i = 0; i < mdev->output_cnt; ++i)
		kfree(mdev->output[i]);
	memset(mdev->output, 0, sizeof mdev->output);

fail_vb2_allocator:
	/* freeing allocator context */
	mdev->vb2->cleanup(mdev->alloc_ctx);

fail:
	return ret;
}

void __devexit mxr_release_video(struct mxr_device *mdev)
{
	int i;

	/* kfree is NULL-safe */
	for (i = 0; i < mdev->output_cnt; ++i)
		kfree(mdev->output[i]);

	mdev->vb2->cleanup(mdev->alloc_ctx);
}

static void tv_graph_pipeline_stream(struct mxr_pipeline *pipe, int on)
{
	struct mxr_device *mdev = pipe->layer->mdev;
	struct media_entity *me = &pipe->layer->vfd.entity;
	/* source pad of graphic layer entity */
	struct media_pad *pad = &me->pads[0];
	struct v4l2_subdev *sd;
	struct exynos_entity_data md_data;

	mxr_dbg(mdev, "%s TV graphic layer pipeline\n", on ? "start" : "stop");

	/* find remote pad through enabled link */
	pad = media_entity_remote_source(pad);
	if (media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV
			|| pad == NULL)
		mxr_warn(mdev, "cannot find remote pad\n");

	sd = media_entity_to_v4l2_subdev(pad->entity);
	mxr_dbg(mdev, "s_stream of %s sub-device is called\n", sd->name);

	md_data.mxr_data_from = FROM_MXR_VD;
	v4l2_set_subdevdata(sd, &md_data);
	v4l2_subdev_call(sd, video, s_stream, on);
}

static int mxr_querycap(struct file *file, void *priv,
	struct v4l2_capability *cap)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	strlcpy(cap->driver, MXR_DRIVER_NAME, sizeof cap->driver);
	strlcpy(cap->card, layer->vfd.name, sizeof cap->card);
	sprintf(cap->bus_info, "%d", layer->idx);
	cap->version = KERNEL_VERSION(0, 1, 0);
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE;

	return 0;
}

/* Geometry handling */
void mxr_layer_geo_fix(struct mxr_layer *layer)
{
	struct mxr_device *mdev = layer->mdev;
	struct v4l2_mbus_framefmt mbus_fmt;

	/* TODO: add some dirty flag to avoid unnecessary adjustments */
	mxr_get_mbus_fmt(mdev, &mbus_fmt);
	layer->geo.dst.full_width = mbus_fmt.width;
	layer->geo.dst.full_height = mbus_fmt.height;
	layer->geo.dst.field = mbus_fmt.field;
	layer->ops.fix_geometry(layer);
}

void mxr_layer_default_geo(struct mxr_layer *layer)
{
	struct mxr_device *mdev = layer->mdev;
	struct v4l2_mbus_framefmt mbus_fmt;

	mxr_dbg(layer->mdev, "%s start\n", __func__);
	memset(&layer->geo, 0, sizeof layer->geo);

	mxr_get_mbus_fmt(mdev, &mbus_fmt);

	layer->geo.dst.full_width = mbus_fmt.width;
	layer->geo.dst.full_height = mbus_fmt.height;
	layer->geo.dst.width = layer->geo.dst.full_width;
	layer->geo.dst.height = layer->geo.dst.full_height;
	layer->geo.dst.field = mbus_fmt.field;

	layer->geo.src.full_width = mbus_fmt.width;
	layer->geo.src.full_height = mbus_fmt.height;
	layer->geo.src.width = layer->geo.src.full_width;
	layer->geo.src.height = layer->geo.src.full_height;

	layer->ops.fix_geometry(layer);
}

static void mxr_geometry_dump(struct mxr_device *mdev, struct mxr_geometry *geo)
{
	mxr_dbg(mdev, "src.full_size = (%u, %u)\n",
		geo->src.full_width, geo->src.full_height);
	mxr_dbg(mdev, "src.size = (%u, %u)\n",
		geo->src.width, geo->src.height);
	mxr_dbg(mdev, "src.offset = (%u, %u)\n",
		geo->src.x_offset, geo->src.y_offset);
	mxr_dbg(mdev, "dst.full_size = (%u, %u)\n",
		geo->dst.full_width, geo->dst.full_height);
	mxr_dbg(mdev, "dst.size = (%u, %u)\n",
		geo->dst.width, geo->dst.height);
	mxr_dbg(mdev, "dst.offset = (%u, %u)\n",
		geo->dst.x_offset, geo->dst.y_offset);
	mxr_dbg(mdev, "ratio = (%u, %u)\n",
		geo->x_ratio, geo->y_ratio);
}

static const struct mxr_format *find_format_by_index(
	struct mxr_layer *layer, unsigned long index);

static int mxr_enum_fmt(struct file *file, void  *priv,
	struct v4l2_fmtdesc *f)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	const struct mxr_format *fmt;

	mxr_dbg(mdev, "%s\n", __func__);
	fmt = find_format_by_index(layer, f->index);
	if (fmt == NULL)
		return -EINVAL;

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int mxr_s_fmt(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct mxr_layer *layer = video_drvdata(file);
	const struct mxr_format *fmt;
	struct v4l2_pix_format_mplane *pix;
	struct mxr_device *mdev = layer->mdev;
	struct mxr_geometry *geo = &layer->geo;

	mxr_dbg(mdev, "%s:%d\n", __func__, __LINE__);

	pix = &f->fmt.pix_mp;
	fmt = find_format_by_fourcc(layer, pix->pixelformat);
	if (fmt == NULL) {
		mxr_warn(mdev, "not recognized fourcc: %08x\n",
			pix->pixelformat);
		return -EINVAL;
	}
	layer->fmt = fmt;
	geo->src.full_width = pix->width;
	geo->src.width = pix->width;
	geo->src.full_height = pix->height;
	geo->src.height = pix->height;
	/* assure consistency of geometry */
	mxr_layer_geo_fix(layer);
	mxr_dbg(mdev, "width=%u height=%u span=%u\n",
		geo->src.width, geo->src.height, geo->src.full_width);

	return 0;
}

static unsigned int divup(unsigned int divident, unsigned int divisor)
{
	return (divident + divisor - 1) / divisor;
}

unsigned long mxr_get_plane_size(const struct mxr_block *blk,
	unsigned int width, unsigned int height)
{
	unsigned int bl_width = divup(width, blk->width);
	unsigned int bl_height = divup(height, blk->height);

	return bl_width * bl_height * blk->size;
}

static void mxr_mplane_fill(struct v4l2_plane_pix_format *planes,
	const struct mxr_format *fmt, u32 width, u32 height)
{
	int i;

	memset(planes, 0, sizeof(*planes) * fmt->num_subframes);
	for (i = 0; i < fmt->num_planes; ++i) {
		struct v4l2_plane_pix_format *plane = planes
			+ fmt->plane2subframe[i];
		const struct mxr_block *blk = &fmt->plane[i];
		u32 bl_width = divup(width, blk->width);
		u32 bl_height = divup(height, blk->height);
		u32 sizeimage = bl_width * bl_height * blk->size;
		u16 bytesperline = bl_width * blk->size / blk->height;

		plane->sizeimage += sizeimage;
		plane->bytesperline = max(plane->bytesperline, bytesperline);
	}
}

static int mxr_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	pix->width = layer->geo.src.full_width;
	pix->height = layer->geo.src.full_height;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = layer->fmt->fourcc;
	pix->colorspace = layer->fmt->colorspace;
	mxr_mplane_fill(pix->plane_fmt, layer->fmt, pix->width, pix->height);

	return 0;
}

static inline struct mxr_crop *choose_crop_by_type(struct mxr_geometry *geo,
	enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return &geo->dst;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return &geo->src;
	default:
		return NULL;
	}
}

static int mxr_g_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_crop *crop;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	crop = choose_crop_by_type(&layer->geo, a->type);
	if (crop == NULL)
		return -EINVAL;
	mxr_layer_geo_fix(layer);
	a->c.left = crop->x_offset;
	a->c.top = crop->y_offset;
	a->c.width = crop->width;
	a->c.height = crop->height;
	return 0;
}

static int mxr_s_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_crop *crop;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	crop = choose_crop_by_type(&layer->geo, a->type);
	if (crop == NULL)
		return -EINVAL;
	crop->x_offset = a->c.left;
	crop->y_offset = a->c.top;
	crop->width = a->c.width;
	crop->height = a->c.height;
	mxr_layer_geo_fix(layer);
	return 0;
}

static int mxr_cropcap(struct file *file, void *fh, struct v4l2_cropcap *a)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_crop *crop;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	crop = choose_crop_by_type(&layer->geo, a->type);
	if (crop == NULL)
		return -EINVAL;
	mxr_layer_geo_fix(layer);
	a->bounds.left = 0;
	a->bounds.top = 0;
	a->bounds.width = crop->full_width;
	a->bounds.top = crop->full_height;
	a->defrect = a->bounds;
	/* setting pixel aspect to 1/1 */
	a->pixelaspect.numerator = 1;
	a->pixelaspect.denominator = 1;
	return 0;
}

static int mxr_check_ctrl_val(struct v4l2_control *ctrl)
{
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_TV_LAYER_BLEND_ALPHA:
	case V4L2_CID_TV_CHROMA_VALUE:
		if (ctrl->value < 0 || ctrl->value > 256)
			ret = -ERANGE;
		break;
	}

	return ret;
}

static int mxr_s_ctrl(struct file *file, void *fh, struct v4l2_control *ctrl)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int v = ctrl->value;
	int ret;

	mxr_dbg(mdev, "%s start\n", __func__);

	ret = mxr_check_ctrl_val(ctrl);
	if (ret) {
		mxr_err(mdev, "alpha value is out of range\n");
		return ret;
	}

	switch (ctrl->id) {
	case V4L2_CID_TV_LAYER_BLEND_ENABLE:
		layer->layer_blend_en = v;
		break;
	case V4L2_CID_TV_LAYER_BLEND_ALPHA:
		layer->layer_alpha = (u32)v;
		break;
	case V4L2_CID_TV_PIXEL_BLEND_ENABLE:
		layer->pixel_blend_en = v;
		break;
	case V4L2_CID_TV_CHROMA_ENABLE:
		layer->chroma_en = v;
		break;
	case V4L2_CID_TV_CHROMA_VALUE:
		layer->chroma_val = (u32)v;
		break;
	case V4L2_CID_TV_HPD_STATUS:
		v4l2_subdev_call(to_outsd(mdev), core, s_ctrl, ctrl);
		break;
	case V4L2_CID_TV_LAYER_PRIO:
		layer->prio = (u8)v;
		/* This can be turned on/off each layer while streaming */
		if (layer->pipe.state == MXR_PIPELINE_STREAMING)
			mxr_reg_set_layer_prio(mdev);
		break;
	case V4L2_CID_TV_SET_DVI_MODE:
		v4l2_subdev_call(to_outsd(mdev), core, s_ctrl, ctrl);
		break;
	default:
		mxr_err(mdev, "invalid control id\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mxr_g_ctrl(struct file *file, void *fh, struct v4l2_control *ctrl)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int num = 0;
	int ret = 0;

	mxr_dbg(mdev, "%s start\n", __func__);

	if (layer->type == MXR_LAYER_TYPE_VIDEO)
		num = 0;
	else if (layer->type == MXR_LAYER_TYPE_GRP && layer->idx == 0)
		num = 1;
	else if (layer->type == MXR_LAYER_TYPE_GRP && layer->idx == 1)
		num = 2;

	ret = mxr_check_ctrl_val(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_TV_HPD_STATUS:
		v4l2_subdev_call(to_outsd(mdev), core, g_ctrl, ctrl);
		break;
	default:
		mxr_err(mdev, "invalid control id\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int mxr_enum_dv_presets(struct file *file, void *fh,
	struct v4l2_dv_enum_preset *preset)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret;

	/* lock protects from changing sd_out */
	mutex_lock(&mdev->mutex);
	ret = v4l2_subdev_call(to_outsd(mdev), video, enum_dv_presets, preset);
	mutex_unlock(&mdev->mutex);

	return ret ? -EINVAL : 0;
}

static int mxr_s_dv_preset(struct file *file, void *fh,
	struct v4l2_dv_preset *preset)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret;

	/* lock protects from changing sd_out */
	mutex_lock(&mdev->mutex);

	/* preset change cannot be done while there is an entity
	 * dependant on output configuration
	 */
	if (mdev->n_output > 0) {
		mutex_unlock(&mdev->mutex);
		return -EBUSY;
	}

	ret = v4l2_subdev_call(to_outsd(mdev), video, s_dv_preset, preset);

	mutex_unlock(&mdev->mutex);

	/* any failure should return EINVAL according to V4L2 doc */
	return ret ? -EINVAL : 0;
}

static int mxr_g_dv_preset(struct file *file, void *fh,
	struct v4l2_dv_preset *preset)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret;

	/* lock protects from changing sd_out */
	mutex_lock(&mdev->mutex);
	ret = v4l2_subdev_call(to_outsd(mdev), video, g_dv_preset, preset);
	mutex_unlock(&mdev->mutex);

	return ret ? -EINVAL : 0;
}

static int mxr_s_std(struct file *file, void *fh, v4l2_std_id *norm)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret;

	/* lock protects from changing sd_out */
	mutex_lock(&mdev->mutex);

	/* standard change cannot be done while there is an entity
	 * dependant on output configuration
	 */
	if (mdev->n_output > 0) {
		mutex_unlock(&mdev->mutex);
		return -EBUSY;
	}

	ret = v4l2_subdev_call(to_outsd(mdev), video, s_std_output, *norm);

	mutex_unlock(&mdev->mutex);

	return ret ? -EINVAL : 0;
}

static int mxr_g_std(struct file *file, void *fh, v4l2_std_id *norm)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret;

	/* lock protects from changing sd_out */
	mutex_lock(&mdev->mutex);
	ret = v4l2_subdev_call(to_outsd(mdev), video, g_std_output, norm);
	mutex_unlock(&mdev->mutex);

	return ret ? -EINVAL : 0;
}

static int mxr_enum_output(struct file *file, void *fh, struct v4l2_output *a)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	struct mxr_output *out;
	struct v4l2_subdev *sd;

	if (a->index >= mdev->output_cnt)
		return -EINVAL;
	out = mdev->output[a->index];
	BUG_ON(out == NULL);
	sd = out->sd;
	strlcpy(a->name, out->name, sizeof(a->name));

	/* try to obtain supported tv norms */
	v4l2_subdev_call(sd, video, g_tvnorms_output, &a->std);
	a->capabilities = 0;
	if (sd->ops->video && sd->ops->video->s_dv_preset)
		a->capabilities |= V4L2_OUT_CAP_PRESETS;
	if (sd->ops->video && sd->ops->video->s_std_output)
		a->capabilities |= V4L2_OUT_CAP_STD;
	a->type = V4L2_OUTPUT_TYPE_ANALOG;

	return 0;
}

static int mxr_s_output(struct file *file, void *fh, unsigned int i)
{
	struct video_device *vfd = video_devdata(file);
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret = 0;

	if (i >= mdev->output_cnt || mdev->output[i] == NULL)
		return -EINVAL;

	mutex_lock(&mdev->mutex);
	if (mdev->n_output > 0) {
		ret = -EBUSY;
		goto done;
	}
	mdev->current_output = i;
	vfd->tvnorms = 0;
	v4l2_subdev_call(to_outsd(mdev), video, g_tvnorms_output,
		&vfd->tvnorms);
	mxr_dbg(mdev, "tvnorms = %08llx\n", vfd->tvnorms);

done:
	mutex_unlock(&mdev->mutex);
	return ret;
}

static int mxr_g_output(struct file *file, void *fh, unsigned int *p)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;

	mutex_lock(&mdev->mutex);
	*p = mdev->current_output;
	mutex_unlock(&mdev->mutex);

	return 0;
}

static int mxr_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_reqbufs(&layer->vb_queue, p);
}

static int mxr_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_querybuf(&layer->vb_queue, p);
}

static int mxr_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct mxr_layer *layer = video_drvdata(file);
	return vb2_qbuf(&layer->vb_queue, p);
}

static int mxr_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct mxr_layer *layer = video_drvdata(file);
	return vb2_dqbuf(&layer->vb_queue, p, file->f_flags & O_NONBLOCK);
}

static int mxr_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;

	switch (layer->idx) {
	case 0:
		mdev->layer_en.graph0 = 1;
		break;
	case 1:
		mdev->layer_en.graph1 = 1;
		break;
	case 2:
		mdev->layer_en.graph2 = 1;
		break;
	case 3:
		mdev->layer_en.graph3 = 1;
		break;
	default:
		mxr_err(mdev, "invalid layer number\n");
		return -EINVAL;
	}

	if ((mdev->layer_en.graph0 && mdev->layer_en.graph2) ||
	    (mdev->layer_en.graph1 && mdev->layer_en.graph3)) {
		mdev->frame_packing = 1;
		mxr_dbg(mdev, "frame packing mode\n");
	}

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_streamon(&layer->vb_queue, i);
}

static int mxr_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;

	switch (layer->idx) {
	case 0:
		mdev->layer_en.graph0 = 0;
		break;
	case 1:
		mdev->layer_en.graph1 = 0;
		break;
	case 2:
		mdev->layer_en.graph2 = 0;
		break;
	case 3:
		mdev->layer_en.graph3 = 0;
		break;
	default:
		mxr_err(mdev, "invalid layer number\n");
		return -EINVAL;
	}

	mdev->frame_packing = 0;
	if ((mdev->layer_en.graph0 && mdev->layer_en.graph2) ||
	    (mdev->layer_en.graph1 && mdev->layer_en.graph3)) {
		mdev->frame_packing = 1;
		mxr_dbg(mdev, "frame packing mode\n");
	}

	mxr_dbg(mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_streamoff(&layer->vb_queue, i);
}

static const struct v4l2_ioctl_ops mxr_ioctl_ops = {
	.vidioc_querycap = mxr_querycap,
	/* format handling */
	.vidioc_enum_fmt_vid_out = mxr_enum_fmt,
	.vidioc_s_fmt_vid_out_mplane = mxr_s_fmt,
	.vidioc_g_fmt_vid_out_mplane = mxr_g_fmt,
	/* buffer control */
	.vidioc_reqbufs = mxr_reqbufs,
	.vidioc_querybuf = mxr_querybuf,
	.vidioc_qbuf = mxr_qbuf,
	.vidioc_dqbuf = mxr_dqbuf,
	/* Streaming control */
	.vidioc_streamon = mxr_streamon,
	.vidioc_streamoff = mxr_streamoff,
	/* Preset functions */
	.vidioc_enum_dv_presets = mxr_enum_dv_presets,
	.vidioc_s_dv_preset = mxr_s_dv_preset,
	.vidioc_g_dv_preset = mxr_g_dv_preset,
	/* analog TV standard functions */
	.vidioc_s_std = mxr_s_std,
	.vidioc_g_std = mxr_g_std,
	/* Output handling */
	.vidioc_enum_output = mxr_enum_output,
	.vidioc_s_output = mxr_s_output,
	.vidioc_g_output = mxr_g_output,
	/* Crop ioctls */
	.vidioc_g_crop = mxr_g_crop,
	.vidioc_s_crop = mxr_s_crop,
	.vidioc_cropcap = mxr_cropcap,
	/* Alpha blending functions */
	.vidioc_s_ctrl = mxr_s_ctrl,
	.vidioc_g_ctrl = mxr_g_ctrl,
};

static int mxr_video_open(struct file *file)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret = 0;

	mxr_dbg(mdev, "%s:%d\n", __func__, __LINE__);
	/* assure device probe is finished */
	wait_for_device_probe();
	/* creating context for file descriptor */
	ret = v4l2_fh_open(file);
	if (ret) {
		mxr_err(mdev, "v4l2_fh_open failed\n");
		return ret;
	}

	/* leaving if layer is already initialized */
	if (!v4l2_fh_is_singular_file(file))
		return 0;

	ret = vb2_queue_init(&layer->vb_queue);
	if (ret != 0) {
		mxr_err(mdev, "failed to initialize vb2 queue\n");
		goto fail_fh_open;
	}
	/* set default format, first on the list */
	layer->fmt = layer->fmt_array[0];
	/* setup default geometry */
	mxr_layer_default_geo(layer);

	return 0;

fail_fh_open:
	v4l2_fh_release(file);

	return ret;
}

static unsigned int
mxr_video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	return vb2_poll(&layer->vb_queue, file, wait);
}

static int mxr_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	return vb2_mmap(&layer->vb_queue, vma);
}

static int mxr_video_release(struct file *file)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	/* initialize alpha blending variables */
	layer->layer_blend_en = 0;
	layer->layer_alpha = 0;
	layer->pixel_blend_en = 0;
	layer->chroma_en = 0;
	layer->chroma_val = 0;

	if (v4l2_fh_is_singular_file(file))
		vb2_queue_release(&layer->vb_queue);

	v4l2_fh_release(file);
	return 0;
}

static const struct v4l2_file_operations mxr_fops = {
	.owner = THIS_MODULE,
	.open = mxr_video_open,
	.poll = mxr_video_poll,
	.mmap = mxr_video_mmap,
	.release = mxr_video_release,
	.unlocked_ioctl = video_ioctl2,
};

static int queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
	unsigned int *nplanes, unsigned long sizes[],
	void *alloc_ctxs[])
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);
	const struct mxr_format *fmt = layer->fmt;
	int i;
	struct mxr_device *mdev = layer->mdev;
	struct v4l2_plane_pix_format planes[3];

	mxr_dbg(mdev, "%s\n", __func__);
	/* checking if format was configured */
	if (fmt == NULL)
		return -EINVAL;
	mxr_dbg(mdev, "fmt = %s\n", fmt->name);
	mxr_mplane_fill(planes, fmt, layer->geo.src.full_width,
		layer->geo.src.full_height);

	*nplanes = fmt->num_subframes;
	for (i = 0; i < fmt->num_subframes; ++i) {
		alloc_ctxs[i] = layer->mdev->alloc_ctx;
		sizes[i] = PAGE_ALIGN(planes[i].sizeimage);
		mxr_dbg(mdev, "size[%d] = %08lx\n", i, sizes[i]);
	}

	if (*nbuffers == 0)
		*nbuffers = 1;

	vb2_queue_init(vq);

	return 0;
}

static void buf_queue(struct vb2_buffer *vb)
{
	struct mxr_buffer *buffer = container_of(vb, struct mxr_buffer, vb);
	struct mxr_layer *layer = vb2_get_drv_priv(vb->vb2_queue);
	struct mxr_pipeline *pipe = &layer->pipe;
	unsigned long flags;
	int must_start = 0;

	spin_lock_irqsave(&layer->enq_slock, flags);
	if (pipe->state == MXR_PIPELINE_STREAMING_START) {
		pipe->state = MXR_PIPELINE_STREAMING;
		must_start = 1;
	}
	list_add_tail(&buffer->list, &layer->enq_list);
	spin_unlock_irqrestore(&layer->enq_slock, flags);
	if (must_start) {
		layer->ops.stream_set(layer, MXR_ENABLE);
		/* store starting entity ptr on the tv graphic pipeline */
		pipe->layer = layer;
		/* start streaming all entities on the tv graphic pipeline */
		tv_graph_pipeline_stream(pipe, 1);
	}
}

static void wait_lock(struct vb2_queue *vq)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);

	mutex_lock(&layer->mutex);
}

static void wait_unlock(struct vb2_queue *vq)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);

	mutex_unlock(&layer->mutex);
}

static int buf_prepare(struct vb2_buffer *vb)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vb->vb2_queue);
	struct mxr_device *mdev = layer->mdev;
	struct v4l2_subdev *sd;
	struct media_pad *pad;
	int i, j;
	int enable = 0;

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
		sd = &mdev->sub_mxr[i].sd;

		for (j = MXR_PAD_SOURCE_GSCALER; j < MXR_PADS_NUM; ++j) {
			pad = &sd->entity.pads[j];

			/* find sink pad of hdmi or sdo through enabled link*/
			pad = media_entity_remote_source(pad);
			if (media_entity_type(pad->entity)
					== MEDIA_ENT_T_V4L2_SUBDEV) {
				enable = 1;
				break;
			}
		}
		if (enable)
			break;
	}
	if (!enable)
		return -ENODEV;

	sd = media_entity_to_v4l2_subdev(pad->entity);

	/* current output device must be matched terminal entity
	 * which represents HDMI or SDO sub-device
	 */
	if (strcmp(sd->name, to_output(mdev)->sd->name)) {
		mxr_err(mdev, "subdev name : %s, output device name : %s\n",
				sd->name, to_output(mdev)->sd->name);
		mxr_err(mdev, "output device is not mached\n");
		return -ERANGE;
	}

	return 0;
}

static int start_streaming(struct vb2_queue *vq)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);
	struct mxr_device *mdev = layer->mdev;
	struct mxr_pipeline *pipe = &layer->pipe;
	unsigned long flags;
	int ret;

	mxr_dbg(mdev, "%s\n", __func__);

	/* enable mixer clock */
	ret = mxr_power_get(mdev);
	if (ret) {
		mxr_err(mdev, "power on failed\n");
		return -ENODEV;
	}

	/* block any changes in output configuration */
	mxr_output_get(mdev);

	/* update layers geometry */
	mxr_layer_geo_fix(layer);
	mxr_geometry_dump(mdev, &layer->geo);

	layer->ops.format_set(layer);
	/* enabling layer in hardware */
	spin_lock_irqsave(&layer->enq_slock, flags);
	pipe->state = MXR_PIPELINE_STREAMING_START;
	spin_unlock_irqrestore(&layer->enq_slock, flags);

	return 0;
}

static void mxr_watchdog(unsigned long arg)
{
	struct mxr_layer *layer = (struct mxr_layer *) arg;
	struct mxr_device *mdev = layer->mdev;
	unsigned long flags;

	mxr_err(mdev, "watchdog fired for layer %s\n", layer->vfd.name);

	spin_lock_irqsave(&layer->enq_slock, flags);

	if (layer->update_buf == layer->shadow_buf)
		layer->update_buf = NULL;
	if (layer->update_buf) {
		vb2_buffer_done(&layer->update_buf->vb, VB2_BUF_STATE_ERROR);
		layer->update_buf = NULL;
	}
	if (layer->shadow_buf) {
		vb2_buffer_done(&layer->shadow_buf->vb, VB2_BUF_STATE_ERROR);
		layer->shadow_buf = NULL;
	}
	spin_unlock_irqrestore(&layer->enq_slock, flags);
}

static int stop_streaming(struct vb2_queue *vq)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);
	struct mxr_device *mdev = layer->mdev;
	unsigned long flags;
	struct timer_list watchdog;
	struct mxr_buffer *buf, *buf_tmp;
	struct mxr_pipeline *pipe = &layer->pipe;

	mxr_dbg(mdev, "%s\n", __func__);

	spin_lock_irqsave(&layer->enq_slock, flags);

	/* reset list */
	pipe->state = MXR_PIPELINE_STREAMING_FINISH;

	/* set all buffer to be done */
	list_for_each_entry_safe(buf, buf_tmp, &layer->enq_list, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&layer->enq_slock, flags);

	/* give 1 seconds to complete to complete last buffers */
	setup_timer_on_stack(&watchdog, mxr_watchdog,
		(unsigned long)layer);
	mod_timer(&watchdog, jiffies + msecs_to_jiffies(1000));

	/* wait until all buffers are goes to done state */
	vb2_wait_for_all_buffers(vq);

	/* stop timer if all synchronization is done */
	del_timer_sync(&watchdog);
	destroy_timer_on_stack(&watchdog);

	/* stopping hardware */
	spin_lock_irqsave(&layer->enq_slock, flags);

	pipe->state = MXR_PIPELINE_IDLE;
	spin_unlock_irqrestore(&layer->enq_slock, flags);

	/* disabling layer in hardware */
	layer->ops.stream_set(layer, MXR_DISABLE);

	/* starting entity on the pipeline */
	pipe->layer = layer;
	/* stop streaming all entities on the pipeline */
	tv_graph_pipeline_stream(pipe, 0);

	/* allow changes in output configuration */
	mxr_output_put(mdev);

	/* disable mixer clock */
	mxr_power_put(mdev);

	return 0;
}

static struct vb2_ops mxr_video_qops = {
	.queue_setup = queue_setup,
	.buf_queue = buf_queue,
	.wait_prepare = wait_unlock,
	.wait_finish = wait_lock,
	.buf_prepare = buf_prepare,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};

/* FIXME: try to put this functions to mxr_base_layer_create */
int mxr_base_layer_register(struct mxr_layer *layer)
{
	struct mxr_device *mdev = layer->mdev;
	struct exynos_md *md;
	int ret;

	md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);
	if (!md) {
		mxr_err(mdev, "failed to get output media device\n");
		return -ENODEV;
	}

	layer->vfd.v4l2_dev = &md->v4l2_dev;
	ret = video_register_device(&layer->vfd, VFL_TYPE_GRABBER, layer->minor);
	if (ret)
		mxr_err(mdev, "failed to register video device\n");
	else
		mxr_info(mdev, "registered layer %s as /dev/video%d\n",
			layer->vfd.name, layer->vfd.num);
			
	layer->fb = vb2_fb_register(&layer->vb_queue, &layer->vfd);
	if (PTR_ERR(layer->fb))
	    layer->fb = NULL;
	    
	return ret;
}

void mxr_base_layer_unregister(struct mxr_layer *layer)
{
    if (layer->fb)
        vb2_fb_unregister(layer->fb);
	video_unregister_device(&layer->vfd);
}

void mxr_layer_release(struct mxr_layer *layer)
{
	if (layer->ops.release)
		layer->ops.release(layer);
}

void mxr_base_layer_release(struct mxr_layer *layer)
{
	kfree(layer);
}

static void mxr_vfd_release(struct video_device *vdev)
{
	printk(KERN_INFO "video device release\n");
}

struct mxr_layer *mxr_base_layer_create(struct mxr_device *mdev,
	int idx, char *name, struct mxr_layer_ops *ops)
{
	struct mxr_layer *layer;
	int ret;

	layer = kzalloc(sizeof *layer, GFP_KERNEL);
	if (layer == NULL) {
		mxr_err(mdev, "not enough memory for layer.\n");
		goto fail;
	}

	layer->mdev = mdev;
	layer->idx = idx;
	layer->ops = *ops;
	layer->prio = idx + 2;

	spin_lock_init(&layer->enq_slock);
	INIT_LIST_HEAD(&layer->enq_list);
	mutex_init(&layer->mutex);

	layer->vfd = (struct video_device) {
		.minor = -1,
		.release = mxr_vfd_release,
		.fops = &mxr_fops,
		.ioctl_ops = &mxr_ioctl_ops,
	};

	/* media_entity_init must be called after initializing layer->vfd
	 * for preventing to overwrite
	 */
	ret = media_entity_init(&layer->vfd.entity, 1, &layer->pad, 0);
	if (ret) {
		mxr_err(mdev, "media entity init failed\n");
		goto fail_alloc;
	}

	strlcpy(layer->vfd.name, name, sizeof(layer->vfd.name));
	layer->vfd.entity.name = layer->vfd.name;
	/* let framework control PRIORITY */
	set_bit(V4L2_FL_USE_FH_PRIO, &layer->vfd.flags);

	video_set_drvdata(&layer->vfd, layer);
	layer->vfd.lock = &layer->mutex;

	layer->vb_queue = (struct vb2_queue) {
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.io_modes = VB2_MMAP | VB2_USERPTR,
		.drv_priv = layer,
		.buf_struct_size = sizeof(struct mxr_buffer),
		.ops = &mxr_video_qops,
		.mem_ops = mdev->vb2->ops,
	};

	return layer;

fail_alloc:
	kfree(layer);

fail:
	return NULL;
}

const struct mxr_format *find_format_by_fourcc(
	struct mxr_layer *layer, unsigned long fourcc)
{
	int i;

	for (i = 0; i < layer->fmt_array_size; ++i)
		if (layer->fmt_array[i]->fourcc == fourcc)
			return layer->fmt_array[i];
	return NULL;
}

static const struct mxr_format *find_format_by_index(
	struct mxr_layer *layer, unsigned long index)
{
	if (index >= layer->fmt_array_size)
		return NULL;
	return layer->fmt_array[index];
}

