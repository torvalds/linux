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

#define pr_fmt(fmt) "s5p-tv (mixer): " fmt

#include "mixer.h"

#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <media/videobuf2-dma-contig.h>

static int find_reg_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd = p;

	*sd = dev_get_drvdata(dev);
	/* non-zero value stops iteration */
	return 1;
}

static struct v4l2_subdev *find_and_register_subdev(
	struct mxr_device *mdev, char *module_name)
{
	struct device_driver *drv;
	struct v4l2_subdev *sd = NULL;
	int ret;

	/* TODO: add waiting until probe is finished */
	drv = driver_find(module_name, &platform_bus_type);
	if (!drv) {
		mxr_warn(mdev, "module %s is missing\n", module_name);
		return NULL;
	}
	/* driver refcnt is increased, it is safe to iterate over devices */
	ret = driver_for_each_device(drv, NULL, &sd, find_reg_callback);
	/* ret == 0 means that find_reg_callback was never executed */
	if (sd == NULL) {
		mxr_warn(mdev, "module %s provides no subdev!\n", module_name);
		goto done;
	}
	/* v4l2_device_register_subdev detects if sd is NULL */
	ret = v4l2_device_register_subdev(&mdev->v4l2_dev, sd);
	if (ret) {
		mxr_warn(mdev, "failed to register subdev %s\n", sd->name);
		sd = NULL;
	}

done:
	return sd;
}

int __devinit mxr_acquire_video(struct mxr_device *mdev,
	struct mxr_output_conf *output_conf, int output_count)
{
	struct device *dev = mdev->dev;
	struct v4l2_device *v4l2_dev = &mdev->v4l2_dev;
	int i;
	int ret = 0;
	struct v4l2_subdev *sd;

	strlcpy(v4l2_dev->name, dev_name(mdev->dev), sizeof(v4l2_dev->name));
	/* prepare context for V4L2 device */
	ret = v4l2_device_register(dev, v4l2_dev);
	if (ret) {
		mxr_err(mdev, "could not register v4l2 device.\n");
		goto fail;
	}

	mdev->alloc_ctx = vb2_dma_contig_init_ctx(mdev->dev);
	if (IS_ERR_OR_NULL(mdev->alloc_ctx)) {
		mxr_err(mdev, "could not acquire vb2 allocator\n");
		goto fail_v4l2_dev;
	}

	/* registering outputs */
	mdev->output_cnt = 0;
	for (i = 0; i < output_count; ++i) {
		struct mxr_output_conf *conf = &output_conf[i];
		struct mxr_output *out;

		sd = find_and_register_subdev(mdev, conf->module_name);
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
	vb2_dma_contig_cleanup_ctx(mdev->alloc_ctx);

fail_v4l2_dev:
	/* NOTE: automatically unregister all subdevs */
	v4l2_device_unregister(v4l2_dev);

fail:
	return ret;
}

void mxr_release_video(struct mxr_device *mdev)
{
	int i;

	/* kfree is NULL-safe */
	for (i = 0; i < mdev->output_cnt; ++i)
		kfree(mdev->output[i]);

	vb2_dma_contig_cleanup_ctx(mdev->alloc_ctx);
	v4l2_device_unregister(&mdev->v4l2_dev);
}

static int mxr_querycap(struct file *file, void *priv,
	struct v4l2_capability *cap)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	strlcpy(cap->driver, MXR_DRIVER_NAME, sizeof cap->driver);
	strlcpy(cap->card, layer->vfd.name, sizeof cap->card);
	sprintf(cap->bus_info, "%d", layer->idx);
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_OUTPUT_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
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

static void mxr_layer_default_geo(struct mxr_layer *layer)
{
	struct mxr_device *mdev = layer->mdev;
	struct v4l2_mbus_framefmt mbus_fmt;

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

	mxr_geometry_dump(mdev, &layer->geo);
	layer->ops.fix_geometry(layer, MXR_GEOMETRY_SINK, 0);
	mxr_geometry_dump(mdev, &layer->geo);
}

static void mxr_layer_update_output(struct mxr_layer *layer)
{
	struct mxr_device *mdev = layer->mdev;
	struct v4l2_mbus_framefmt mbus_fmt;

	mxr_get_mbus_fmt(mdev, &mbus_fmt);
	/* checking if update is needed */
	if (layer->geo.dst.full_width == mbus_fmt.width &&
		layer->geo.dst.full_height == mbus_fmt.width)
		return;

	layer->geo.dst.full_width = mbus_fmt.width;
	layer->geo.dst.full_height = mbus_fmt.height;
	layer->geo.dst.field = mbus_fmt.field;
	layer->ops.fix_geometry(layer, MXR_GEOMETRY_SINK, 0);

	mxr_geometry_dump(mdev, &layer->geo);
}

static const struct mxr_format *find_format_by_fourcc(
	struct mxr_layer *layer, unsigned long fourcc);
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

	/* checking if nothing to fill */
	if (!planes)
		return;

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
	/* set source size to highest accepted value */
	geo->src.full_width = max(geo->dst.full_width, pix->width);
	geo->src.full_height = max(geo->dst.full_height, pix->height);
	layer->ops.fix_geometry(layer, MXR_GEOMETRY_SOURCE, 0);
	mxr_geometry_dump(mdev, &layer->geo);
	/* set cropping to total visible screen */
	geo->src.width = pix->width;
	geo->src.height = pix->height;
	geo->src.x_offset = 0;
	geo->src.y_offset = 0;
	/* assure consistency of geometry */
	layer->ops.fix_geometry(layer, MXR_GEOMETRY_CROP, MXR_NO_OFFSET);
	mxr_geometry_dump(mdev, &layer->geo);
	/* set full size to lowest possible value */
	geo->src.full_width = 0;
	geo->src.full_height = 0;
	layer->ops.fix_geometry(layer, MXR_GEOMETRY_SOURCE, 0);
	mxr_geometry_dump(mdev, &layer->geo);

	/* returning results */
	mxr_g_fmt(file, priv, f);

	return 0;
}

static int mxr_g_selection(struct file *file, void *fh,
	struct v4l2_selection *s)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_geometry *geo = &layer->geo;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		s->r.left = geo->src.x_offset;
		s->r.top = geo->src.y_offset;
		s->r.width = geo->src.width;
		s->r.height = geo->src.height;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = geo->src.full_width;
		s->r.height = geo->src.full_height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r.left = geo->dst.x_offset;
		s->r.top = geo->dst.y_offset;
		s->r.width = geo->dst.width;
		s->r.height = geo->dst.height;
		break;
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = geo->dst.full_width;
		s->r.height = geo->dst.full_height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* returns 1 if rectangle 'a' is inside 'b' */
static int mxr_is_rect_inside(struct v4l2_rect *a, struct v4l2_rect *b)
{
	if (a->left < b->left)
		return 0;
	if (a->top < b->top)
		return 0;
	if (a->left + a->width > b->left + b->width)
		return 0;
	if (a->top + a->height > b->top + b->height)
		return 0;
	return 1;
}

static int mxr_s_selection(struct file *file, void *fh,
	struct v4l2_selection *s)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_geometry *geo = &layer->geo;
	struct mxr_crop *target = NULL;
	enum mxr_geometry_stage stage;
	struct mxr_geometry tmp;
	struct v4l2_rect res;

	memset(&res, 0, sizeof res);

	mxr_dbg(layer->mdev, "%s: rect: %dx%d@%d,%d\n", __func__,
		s->r.width, s->r.height, s->r.left, s->r.top);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	switch (s->target) {
	/* ignore read-only targets */
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		res.width = geo->src.full_width;
		res.height = geo->src.full_height;
		break;

	/* ignore read-only targets */
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		res.width = geo->dst.full_width;
		res.height = geo->dst.full_height;
		break;

	case V4L2_SEL_TGT_CROP:
		target = &geo->src;
		stage = MXR_GEOMETRY_CROP;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		target = &geo->dst;
		stage = MXR_GEOMETRY_COMPOSE;
		break;
	default:
		return -EINVAL;
	}
	/* apply change and update geometry if needed */
	if (target) {
		/* backup current geometry if setup fails */
		memcpy(&tmp, geo, sizeof tmp);

		/* apply requested selection */
		target->x_offset = s->r.left;
		target->y_offset = s->r.top;
		target->width = s->r.width;
		target->height = s->r.height;

		layer->ops.fix_geometry(layer, stage, s->flags);

		/* retrieve update selection rectangle */
		res.left = target->x_offset;
		res.top = target->y_offset;
		res.width = target->width;
		res.height = target->height;

		mxr_geometry_dump(layer->mdev, &layer->geo);
	}

	/* checking if the rectangle satisfies constraints */
	if ((s->flags & V4L2_SEL_FLAG_LE) && !mxr_is_rect_inside(&res, &s->r))
		goto fail;
	if ((s->flags & V4L2_SEL_FLAG_GE) && !mxr_is_rect_inside(&s->r, &res))
		goto fail;

	/* return result rectangle */
	s->r = res;

	return 0;
fail:
	/* restore old geometry, which is not touched if target is NULL */
	if (target)
		memcpy(geo, &tmp, sizeof tmp);
	return -ERANGE;
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

	mxr_layer_update_output(layer);

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

	mxr_layer_update_output(layer);

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

	if (i >= mdev->output_cnt || mdev->output[i] == NULL)
		return -EINVAL;

	mutex_lock(&mdev->mutex);
	if (mdev->n_output > 0) {
		mutex_unlock(&mdev->mutex);
		return -EBUSY;
	}
	mdev->current_output = i;
	vfd->tvnorms = 0;
	v4l2_subdev_call(to_outsd(mdev), video, g_tvnorms_output,
		&vfd->tvnorms);
	mutex_unlock(&mdev->mutex);

	/* update layers geometry */
	mxr_layer_update_output(layer);

	mxr_dbg(mdev, "tvnorms = %08llx\n", vfd->tvnorms);

	return 0;
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

	mxr_dbg(layer->mdev, "%s:%d(%d)\n", __func__, __LINE__, p->index);
	return vb2_qbuf(&layer->vb_queue, p);
}

static int mxr_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_dqbuf(&layer->vb_queue, p, file->f_flags & O_NONBLOCK);
}

static int mxr_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_streamon(&layer->vb_queue, i);
}

static int mxr_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	return vb2_streamoff(&layer->vb_queue, i);
}

static const struct v4l2_ioctl_ops mxr_ioctl_ops = {
	.vidioc_querycap = mxr_querycap,
	/* format handling */
	.vidioc_enum_fmt_vid_out_mplane = mxr_enum_fmt,
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
	/* selection ioctls */
	.vidioc_g_selection = mxr_g_selection,
	.vidioc_s_selection = mxr_s_selection,
};

static int mxr_video_open(struct file *file)
{
	struct mxr_layer *layer = video_drvdata(file);
	struct mxr_device *mdev = layer->mdev;
	int ret = 0;

	mxr_dbg(mdev, "%s:%d\n", __func__, __LINE__);
	if (mutex_lock_interruptible(&layer->mutex))
		return -ERESTARTSYS;
	/* assure device probe is finished */
	wait_for_device_probe();
	/* creating context for file descriptor */
	ret = v4l2_fh_open(file);
	if (ret) {
		mxr_err(mdev, "v4l2_fh_open failed\n");
		goto unlock;
	}

	/* leaving if layer is already initialized */
	if (!v4l2_fh_is_singular_file(file))
		goto unlock;

	/* FIXME: should power be enabled on open? */
	ret = mxr_power_get(mdev);
	if (ret) {
		mxr_err(mdev, "power on failed\n");
		goto fail_fh_open;
	}

	ret = vb2_queue_init(&layer->vb_queue);
	if (ret != 0) {
		mxr_err(mdev, "failed to initialize vb2 queue\n");
		goto fail_power;
	}
	/* set default format, first on the list */
	layer->fmt = layer->fmt_array[0];
	/* setup default geometry */
	mxr_layer_default_geo(layer);
	mutex_unlock(&layer->mutex);

	return 0;

fail_power:
	mxr_power_put(mdev);

fail_fh_open:
	v4l2_fh_release(file);

unlock:
	mutex_unlock(&layer->mutex);

	return ret;
}

static unsigned int
mxr_video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct mxr_layer *layer = video_drvdata(file);
	unsigned int res;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	mutex_lock(&layer->mutex);
	res = vb2_poll(&layer->vb_queue, file, wait);
	mutex_unlock(&layer->mutex);
	return res;
}

static int mxr_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mxr_layer *layer = video_drvdata(file);
	int ret;

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);

	if (mutex_lock_interruptible(&layer->mutex))
		return -ERESTARTSYS;
	ret = vb2_mmap(&layer->vb_queue, vma);
	mutex_unlock(&layer->mutex);
	return ret;
}

static int mxr_video_release(struct file *file)
{
	struct mxr_layer *layer = video_drvdata(file);

	mxr_dbg(layer->mdev, "%s:%d\n", __func__, __LINE__);
	mutex_lock(&layer->mutex);
	if (v4l2_fh_is_singular_file(file)) {
		vb2_queue_release(&layer->vb_queue);
		mxr_power_put(layer->mdev);
	}
	v4l2_fh_release(file);
	mutex_unlock(&layer->mutex);
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

static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *pfmt,
	unsigned int *nbuffers, unsigned int *nplanes, unsigned int sizes[],
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
		sizes[i] = planes[i].sizeimage;
		mxr_dbg(mdev, "size[%d] = %08x\n", i, sizes[i]);
	}

	if (*nbuffers == 0)
		*nbuffers = 1;

	return 0;
}

static void buf_queue(struct vb2_buffer *vb)
{
	struct mxr_buffer *buffer = container_of(vb, struct mxr_buffer, vb);
	struct mxr_layer *layer = vb2_get_drv_priv(vb->vb2_queue);
	struct mxr_device *mdev = layer->mdev;
	unsigned long flags;

	spin_lock_irqsave(&layer->enq_slock, flags);
	list_add_tail(&buffer->list, &layer->enq_list);
	spin_unlock_irqrestore(&layer->enq_slock, flags);

	mxr_dbg(mdev, "queuing buffer\n");
}

static void wait_lock(struct vb2_queue *vq)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);

	mxr_dbg(layer->mdev, "%s\n", __func__);
	mutex_lock(&layer->mutex);
}

static void wait_unlock(struct vb2_queue *vq)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);

	mxr_dbg(layer->mdev, "%s\n", __func__);
	mutex_unlock(&layer->mutex);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct mxr_layer *layer = vb2_get_drv_priv(vq);
	struct mxr_device *mdev = layer->mdev;
	unsigned long flags;

	mxr_dbg(mdev, "%s\n", __func__);

	if (count == 0) {
		mxr_dbg(mdev, "no output buffers queued\n");
		return -EINVAL;
	}

	/* block any changes in output configuration */
	mxr_output_get(mdev);

	mxr_layer_update_output(layer);
	layer->ops.format_set(layer);
	/* enabling layer in hardware */
	spin_lock_irqsave(&layer->enq_slock, flags);
	layer->state = MXR_LAYER_STREAMING;
	spin_unlock_irqrestore(&layer->enq_slock, flags);

	layer->ops.stream_set(layer, MXR_ENABLE);
	mxr_streamer_get(mdev);

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

	mxr_dbg(mdev, "%s\n", __func__);

	spin_lock_irqsave(&layer->enq_slock, flags);

	/* reset list */
	layer->state = MXR_LAYER_STREAMING_FINISH;

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
	layer->state = MXR_LAYER_IDLE;
	spin_unlock_irqrestore(&layer->enq_slock, flags);

	/* disabling layer in hardware */
	layer->ops.stream_set(layer, MXR_DISABLE);
	/* remove one streamer */
	mxr_streamer_put(mdev);
	/* allow changes in output configuration */
	mxr_output_put(mdev);
	return 0;
}

static struct vb2_ops mxr_video_qops = {
	.queue_setup = queue_setup,
	.buf_queue = buf_queue,
	.wait_prepare = wait_unlock,
	.wait_finish = wait_lock,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};

/* FIXME: try to put this functions to mxr_base_layer_create */
int mxr_base_layer_register(struct mxr_layer *layer)
{
	struct mxr_device *mdev = layer->mdev;
	int ret;

	ret = video_register_device(&layer->vfd, VFL_TYPE_GRABBER, -1);
	if (ret)
		mxr_err(mdev, "failed to register video device\n");
	else
		mxr_info(mdev, "registered layer %s as /dev/video%d\n",
			layer->vfd.name, layer->vfd.num);
	return ret;
}

void mxr_base_layer_unregister(struct mxr_layer *layer)
{
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
	pr_info("video device release\n");
}

struct mxr_layer *mxr_base_layer_create(struct mxr_device *mdev,
	int idx, char *name, struct mxr_layer_ops *ops)
{
	struct mxr_layer *layer;

	layer = kzalloc(sizeof *layer, GFP_KERNEL);
	if (layer == NULL) {
		mxr_err(mdev, "not enough memory for layer.\n");
		goto fail;
	}

	layer->mdev = mdev;
	layer->idx = idx;
	layer->ops = *ops;

	spin_lock_init(&layer->enq_slock);
	INIT_LIST_HEAD(&layer->enq_list);
	mutex_init(&layer->mutex);

	layer->vfd = (struct video_device) {
		.minor = -1,
		.release = mxr_vfd_release,
		.fops = &mxr_fops,
		.vfl_dir = VFL_DIR_TX,
		.ioctl_ops = &mxr_ioctl_ops,
	};
	strlcpy(layer->vfd.name, name, sizeof(layer->vfd.name));
	/* let framework control PRIORITY */
	set_bit(V4L2_FL_USE_FH_PRIO, &layer->vfd.flags);

	video_set_drvdata(&layer->vfd, layer);
	layer->vfd.lock = &layer->mutex;
	layer->vfd.v4l2_dev = &mdev->v4l2_dev;

	layer->vb_queue = (struct vb2_queue) {
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF,
		.drv_priv = layer,
		.buf_struct_size = sizeof(struct mxr_buffer),
		.ops = &mxr_video_qops,
		.mem_ops = &vb2_dma_contig_memops,
	};

	return layer;

fail:
	return NULL;
}

static const struct mxr_format *find_format_by_fourcc(
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

