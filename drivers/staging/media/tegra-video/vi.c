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
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include <soc/tegra/pmc.h>

#include "vi.h"
#include "video.h"

#define MAX_CID_CONTROLS		3

/**
 * struct tegra_vi_graph_entity - Entity in the video graph
 *
 * @asd: subdev asynchronous registration information
 * @entity: media entity from the corresponding V4L2 subdev
 * @subdev: V4L2 subdev
 */
struct tegra_vi_graph_entity {
	struct v4l2_async_connection asd;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
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

static inline struct tegra_vi_graph_entity *
to_tegra_vi_graph_entity(struct v4l2_async_connection *asd)
{
	return container_of(asd, struct tegra_vi_graph_entity, asd);
}

static int tegra_get_format_idx_by_code(struct tegra_vi *vi,
					unsigned int code,
					unsigned int offset)
{
	unsigned int i;

	for (i = offset; i < vi->soc->nformats; ++i) {
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

	if (chan->vi->ops->channel_queue_setup)
		chan->vi->ops->channel_queue_setup(chan);

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
tegra_channel_get_remote_csi_subdev(struct tegra_vi_channel *chan)
{
	struct media_pad *pad;

	pad = media_pad_remote_pad_first(&chan->pad);
	if (!pad)
		return NULL;

	return media_entity_to_v4l2_subdev(pad->entity);
}

/*
 * Walk up the chain until the initial source (e.g. image sensor)
 */
struct v4l2_subdev *
tegra_channel_get_remote_source_subdev(struct tegra_vi_channel *chan)
{
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct media_entity *entity;

	subdev = tegra_channel_get_remote_csi_subdev(chan);
	if (!subdev)
		return NULL;

	pad = &subdev->entity.pads[0];
	while (!(pad->flags & MEDIA_PAD_FL_SOURCE)) {
		pad = media_pad_remote_pad_first(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;
		entity = pad->entity;
		pad = &entity->pads[0];
		subdev = media_entity_to_v4l2_subdev(entity);
	}

	return subdev;
}

static int tegra_channel_enable_stream(struct tegra_vi_channel *chan)
{
	struct v4l2_subdev *subdev;
	int ret;

	subdev = tegra_channel_get_remote_csi_subdev(chan);
	ret = v4l2_subdev_call(subdev, video, s_stream, true);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	return 0;
}

static int tegra_channel_disable_stream(struct tegra_vi_channel *chan)
{
	struct v4l2_subdev *subdev;
	int ret;

	subdev = tegra_channel_get_remote_csi_subdev(chan);
	ret = v4l2_subdev_call(subdev, video, s_stream, false);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	return 0;
}

int tegra_channel_set_stream(struct tegra_vi_channel *chan, bool on)
{
	int ret;

	if (on)
		ret = tegra_channel_enable_stream(chan);
	else
		ret = tegra_channel_disable_stream(chan);

	return ret;
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

	ret = pm_runtime_resume_and_get(chan->vi->dev);
	if (ret < 0) {
		dev_err(chan->vi->dev, "failed to get runtime PM: %d\n", ret);
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

	subdev = tegra_channel_get_remote_source_subdev(chan);
	return v4l2_g_parm_cap(&chan->video, subdev, a);
}

static int tegra_channel_s_parm(struct file *file, void *fh,
				struct v4l2_streamparm *a)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
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

	subdev = tegra_channel_get_remote_source_subdev(chan);
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

	subdev = tegra_channel_get_remote_source_subdev(chan);
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

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		fmts_bitmap = chan->fmts_bitmap;

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

static int __tegra_channel_try_format(struct tegra_vi_channel *chan,
				      struct v4l2_pix_format *pix)
{
	const struct tegra_video_format *fmtinfo;
	static struct lock_class_key key;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct v4l2_subdev_state *sd_state;
	struct v4l2_subdev_frame_size_enum fse = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct v4l2_subdev_selection sdsel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = V4L2_SEL_TGT_CROP_BOUNDS,
	};
	int ret;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!subdev)
		return -ENODEV;

	/*
	 * FIXME: Drop this call, drivers are not supposed to use
	 * __v4l2_subdev_state_alloc().
	 */
	sd_state = __v4l2_subdev_state_alloc(subdev, "tegra:state->lock",
					     &key);
	if (IS_ERR(sd_state))
		return PTR_ERR(sd_state);
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
	fmt.pad = 0;
	v4l2_fill_mbus_format(&fmt.format, pix, fmtinfo->code);

	/*
	 * Attempt to obtain the format size from subdev.
	 * If not available, try to get crop boundary from subdev.
	 */
	fse.code = fmtinfo->code;
	ret = v4l2_subdev_call(subdev, pad, enum_frame_size, sd_state, &fse);
	if (ret) {
		if (!v4l2_subdev_has_op(subdev, pad, get_selection)) {
			sd_state->pads->try_crop.width = 0;
			sd_state->pads->try_crop.height = 0;
		} else {
			ret = v4l2_subdev_call(subdev, pad, get_selection,
					       NULL, &sdsel);
			if (ret)
				return -EINVAL;

			sd_state->pads->try_crop.width = sdsel.r.width;
			sd_state->pads->try_crop.height = sdsel.r.height;
		}
	} else {
		sd_state->pads->try_crop.width = fse.max_width;
		sd_state->pads->try_crop.height = fse.max_height;
	}

	ret = v4l2_subdev_call(subdev, pad, set_fmt, sd_state, &fmt);
	if (ret < 0)
		return ret;

	v4l2_fill_pix_format(pix, &fmt.format);
	chan->vi->ops->vi_fmt_align(pix, fmtinfo->bpp);

	__v4l2_subdev_state_free(sd_state);

	return 0;
}

static int tegra_channel_try_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct tegra_vi_channel *chan = video_drvdata(file);

	return __tegra_channel_try_format(chan, &format->fmt.pix);
}

static void tegra_channel_update_gangports(struct tegra_vi_channel *chan)
{
	if (chan->format.width <= 1920)
		chan->numgangports = 1;
	else
		chan->numgangports = chan->totalports;
}

static int tegra_channel_set_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	const struct tegra_video_format *fmtinfo;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
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

	fmt.pad = 0;
	v4l2_fill_mbus_format(&fmt.format, pix, fmtinfo->code);
	subdev = tegra_channel_get_remote_source_subdev(chan);
	ret = v4l2_subdev_call(subdev, pad, set_fmt, NULL, &fmt);
	if (ret < 0)
		return ret;

	v4l2_fill_pix_format(pix, &fmt.format);
	chan->vi->ops->vi_fmt_align(pix, fmtinfo->bpp);

	chan->format = *pix;
	chan->fmtinfo = fmtinfo;
	tegra_channel_update_gangports(chan);

	return 0;
}

static int tegra_channel_set_subdev_active_fmt(struct tegra_vi_channel *chan)
{
	int ret, index;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	/*
	 * Initialize channel format to the sub-device active format if there
	 * is corresponding match in the Tegra supported video formats.
	 */
	subdev = tegra_channel_get_remote_source_subdev(chan);
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	index = tegra_get_format_idx_by_code(chan->vi, fmt.format.code, 0);
	if (index < 0)
		return -EINVAL;

	chan->fmtinfo = &chan->vi->soc->video_formats[index];
	v4l2_fill_pix_format(&chan->format, &fmt.format);
	chan->format.pixelformat = chan->fmtinfo->fourcc;
	chan->format.bytesperline = chan->format.width * chan->fmtinfo->bpp;
	chan->format.sizeimage = chan->format.bytesperline *
				 chan->format.height;
	chan->vi->ops->vi_fmt_align(&chan->format, chan->fmtinfo->bpp);
	tegra_channel_update_gangports(chan);

	return 0;
}

static int
tegra_channel_subscribe_event(struct v4l2_fh *fh,
			      const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_event_subscribe(fh, sub, 4, NULL);
	}

	return v4l2_ctrl_subscribe_event(fh, sub);
}

static int tegra_channel_g_selection(struct file *file, void *priv,
				     struct v4l2_selection *sel)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_subdev_selection sdsel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = sel->target,
	};
	int ret;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, pad, get_selection))
		return -ENOTTY;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	/*
	 * Try the get selection operation and fallback to get format if not
	 * implemented.
	 */
	ret = v4l2_subdev_call(subdev, pad, get_selection, NULL, &sdsel);
	if (!ret)
		sel->r = sdsel.r;
	if (ret != -ENOIOCTLCMD)
		return ret;

	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret;

	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = fmt.format.width;
	sel->r.height = fmt.format.height;

	return 0;
}

static int tegra_channel_s_selection(struct file *file, void *fh,
				     struct v4l2_selection *sel)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;
	int ret;
	struct v4l2_subdev_selection sdsel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = sel->target,
		.flags = sel->flags,
		.r = sel->r,
	};

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, pad, set_selection))
		return -ENOTTY;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (vb2_is_busy(&chan->queue))
		return -EBUSY;

	ret = v4l2_subdev_call(subdev, pad, set_selection, NULL, &sdsel);
	if (!ret) {
		sel->r = sdsel.r;
		/*
		 * Subdev active format resolution may have changed during
		 * set selection operation. So, update channel format to
		 * the sub-device active format.
		 */
		return tegra_channel_set_subdev_active_fmt(chan);
	}

	return ret;
}

static int tegra_channel_g_edid(struct file *file, void *fh,
				struct v4l2_edid *edid)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, pad, get_edid))
		return -ENOTTY;

	return v4l2_subdev_call(subdev, pad, get_edid, edid);
}

static int tegra_channel_s_edid(struct file *file, void *fh,
				struct v4l2_edid *edid)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, pad, set_edid))
		return -ENOTTY;

	return v4l2_subdev_call(subdev, pad, set_edid, edid);
}

static int tegra_channel_g_dv_timings(struct file *file, void *fh,
				      struct v4l2_dv_timings *timings)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, video, g_dv_timings))
		return -ENOTTY;

	return v4l2_device_call_until_err(chan->video.v4l2_dev, 0,
					  video, g_dv_timings, timings);
}

static int tegra_channel_s_dv_timings(struct file *file, void *fh,
				      struct v4l2_dv_timings *timings)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;
	struct v4l2_bt_timings *bt = &timings->bt;
	struct v4l2_dv_timings curr_timings;
	int ret;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, video, s_dv_timings))
		return -ENOTTY;

	ret = tegra_channel_g_dv_timings(file, fh, &curr_timings);
	if (ret)
		return ret;

	if (v4l2_match_dv_timings(timings, &curr_timings, 0, false))
		return 0;

	if (vb2_is_busy(&chan->queue))
		return -EBUSY;

	ret = v4l2_device_call_until_err(chan->video.v4l2_dev, 0,
					 video, s_dv_timings, timings);
	if (ret)
		return ret;

	chan->format.width = bt->width;
	chan->format.height = bt->height;
	chan->format.bytesperline = bt->width * chan->fmtinfo->bpp;
	chan->format.sizeimage = chan->format.bytesperline * bt->height;
	chan->vi->ops->vi_fmt_align(&chan->format, chan->fmtinfo->bpp);
	tegra_channel_update_gangports(chan);

	return 0;
}

static int tegra_channel_query_dv_timings(struct file *file, void *fh,
					  struct v4l2_dv_timings *timings)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, video, query_dv_timings))
		return -ENOTTY;

	return v4l2_device_call_until_err(chan->video.v4l2_dev, 0,
					  video, query_dv_timings, timings);
}

static int tegra_channel_enum_dv_timings(struct file *file, void *fh,
					 struct v4l2_enum_dv_timings *timings)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, pad, enum_dv_timings))
		return -ENOTTY;

	return v4l2_subdev_call(subdev, pad, enum_dv_timings, timings);
}

static int tegra_channel_dv_timings_cap(struct file *file, void *fh,
					struct v4l2_dv_timings_cap *cap)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!v4l2_subdev_has_op(subdev, pad, dv_timings_cap))
		return -ENOTTY;

	return v4l2_subdev_call(subdev, pad, dv_timings_cap, cap);
}

static int tegra_channel_log_status(struct file *file, void *fh)
{
	struct tegra_vi_channel *chan = video_drvdata(file);

	v4l2_device_call_all(chan->video.v4l2_dev, 0, core, log_status);

	return 0;
}

static int tegra_channel_enum_input(struct file *file, void *fh,
				    struct v4l2_input *inp)
{
	struct tegra_vi_channel *chan = video_drvdata(file);
	struct v4l2_subdev *subdev;

	if (inp->index)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	subdev = tegra_channel_get_remote_source_subdev(chan);
	strscpy(inp->name, subdev->name, sizeof(inp->name));
	if (v4l2_subdev_has_op(subdev, pad, dv_timings_cap))
		inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;

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
	.vidioc_subscribe_event		= tegra_channel_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
	.vidioc_g_selection		= tegra_channel_g_selection,
	.vidioc_s_selection		= tegra_channel_s_selection,
	.vidioc_g_edid			= tegra_channel_g_edid,
	.vidioc_s_edid			= tegra_channel_s_edid,
	.vidioc_g_dv_timings		= tegra_channel_g_dv_timings,
	.vidioc_s_dv_timings		= tegra_channel_s_dv_timings,
	.vidioc_query_dv_timings	= tegra_channel_query_dv_timings,
	.vidioc_enum_dv_timings		= tegra_channel_enum_dv_timings,
	.vidioc_dv_timings_cap		= tegra_channel_dv_timings_cap,
	.vidioc_log_status		= tegra_channel_log_status,
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
	case V4L2_CID_TEGRA_SYNCPT_TIMEOUT_RETRY:
		chan->syncpt_timeout_retry = ctrl->val;
		break;
	case V4L2_CID_HFLIP:
		chan->hflip = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		chan->vflip = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops vi_ctrl_ops = {
	.s_ctrl	= vi_s_ctrl,
};

#if IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG)
static const char *const vi_pattern_strings[] = {
	"Black/White Direct Mode",
	"Color Patch Mode",
};
#else
static const struct v4l2_ctrl_config syncpt_timeout_ctrl = {
	.ops = &vi_ctrl_ops,
	.id = V4L2_CID_TEGRA_SYNCPT_TIMEOUT_RETRY,
	.name = "Syncpt timeout retry",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 10000,
	.step = 1,
	.def = 5,
};
#endif

static int tegra_channel_setup_ctrl_handler(struct tegra_vi_channel *chan)
{
	int ret;

#if IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG)
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
#else
	struct v4l2_subdev *subdev;

	/* custom control */
	v4l2_ctrl_new_custom(&chan->ctrl_handler, &syncpt_timeout_ctrl, NULL);
	if (chan->ctrl_handler.error) {
		dev_err(chan->vi->dev, "failed to add %s ctrl handler: %d\n",
			syncpt_timeout_ctrl.name,
			chan->ctrl_handler.error);
		v4l2_ctrl_handler_free(&chan->ctrl_handler);
		return chan->ctrl_handler.error;
	}

	subdev = tegra_channel_get_remote_source_subdev(chan);
	if (!subdev)
		return -ENODEV;

	ret = v4l2_ctrl_add_handler(&chan->ctrl_handler, subdev->ctrl_handler,
				    NULL, true);
	if (ret < 0) {
		dev_err(chan->vi->dev,
			"failed to add subdev %s ctrl handler: %d\n",
			subdev->name, ret);
		v4l2_ctrl_handler_free(&chan->ctrl_handler);
		return ret;
	}

	if (chan->vi->soc->has_h_v_flip) {
		v4l2_ctrl_new_std(&chan->ctrl_handler, &vi_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
		v4l2_ctrl_new_std(&chan->ctrl_handler, &vi_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	}

#endif

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
					     MEDIA_BUS_FMT_SRGGB10_1X10, 0);
	bitmap_set(chan->tpg_fmts_bitmap, index, 1);

	index = tegra_get_format_idx_by_code(chan->vi,
					     MEDIA_BUS_FMT_RGB888_1X32_PADHI,
					     0);
	bitmap_set(chan->tpg_fmts_bitmap, index, 1);
}

static int vi_fmts_bitmap_init(struct tegra_vi_channel *chan)
{
	int index, ret, match_code = 0;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	bitmap_zero(chan->fmts_bitmap, MAX_FORMAT_NUM);

	/*
	 * Set the bitmap bits based on all the matched formats between the
	 * available media bus formats of sub-device and the pre-defined Tegra
	 * supported video formats.
	 */
	subdev = tegra_channel_get_remote_source_subdev(chan);
	while (1) {
		ret = v4l2_subdev_call(subdev, pad, enum_mbus_code,
				       NULL, &code);
		if (ret < 0)
			break;

		index = tegra_get_format_idx_by_code(chan->vi, code.code, 0);
		while (index >= 0) {
			bitmap_set(chan->fmts_bitmap, index, 1);
			if (!match_code)
				match_code = code.code;
			/* look for other formats with same mbus code */
			index = tegra_get_format_idx_by_code(chan->vi,
							     code.code,
							     index + 1);
		}

		code.index++;
	}

	/*
	 * Set the bitmap bit corresponding to default tegra video format if
	 * there are no matched formats.
	 */
	if (!match_code) {
		match_code = chan->vi->soc->default_video_format->code;
		index = tegra_get_format_idx_by_code(chan->vi, match_code, 0);
		if (WARN_ON(index < 0))
			return -EINVAL;

		bitmap_set(chan->fmts_bitmap, index, 1);
	}

	/* initialize channel format to the sub-device active format */
	tegra_channel_set_subdev_active_fmt(chan);

	return 0;
}

static void tegra_channel_cleanup(struct tegra_vi_channel *chan)
{
	v4l2_ctrl_handler_free(&chan->ctrl_handler);
	media_entity_cleanup(&chan->video.entity);
	chan->vi->ops->channel_host1x_syncpt_free(chan);
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
	int ret;

	mutex_init(&chan->video_lock);
	INIT_LIST_HEAD(&chan->capture);
	INIT_LIST_HEAD(&chan->done);
	spin_lock_init(&chan->start_lock);
	spin_lock_init(&chan->done_lock);
	init_waitqueue_head(&chan->start_wait);
	init_waitqueue_head(&chan->done_wait);

	/* initialize the video format */
	chan->fmtinfo = chan->vi->soc->default_video_format;
	chan->format.pixelformat = chan->fmtinfo->fourcc;
	chan->format.colorspace = V4L2_COLORSPACE_SRGB;
	chan->format.field = V4L2_FIELD_NONE;
	chan->format.width = TEGRA_DEF_WIDTH;
	chan->format.height = TEGRA_DEF_HEIGHT;
	chan->format.bytesperline = TEGRA_DEF_WIDTH * chan->fmtinfo->bpp;
	chan->format.sizeimage = chan->format.bytesperline * TEGRA_DEF_HEIGHT;
	vi->ops->vi_fmt_align(&chan->format, chan->fmtinfo->bpp);

	ret = vi->ops->channel_host1x_syncpt_init(chan);
	if (ret)
		return ret;

	/* initialize the media entity */
	chan->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&chan->video.entity, 1, &chan->pad);
	if (ret < 0) {
		dev_err(vi->dev,
			"failed to initialize media entity: %d\n", ret);
		goto free_syncpts;
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
		 dev_name(vi->dev), "output", chan->portnos[0]);
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

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		v4l2_async_nf_init(&chan->notifier, &vid->v4l2_dev);

	return 0;

free_v4l2_ctrl_hdl:
	v4l2_ctrl_handler_free(&chan->ctrl_handler);
cleanup_media:
	media_entity_cleanup(&chan->video.entity);
free_syncpts:
	vi->ops->channel_host1x_syncpt_free(chan);
	return ret;
}

static int tegra_vi_channel_alloc(struct tegra_vi *vi, unsigned int port_num,
				  struct device_node *node, unsigned int lanes)
{
	struct tegra_vi_channel *chan;
	unsigned int i;

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
	chan->portnos[0] = port_num;
	/*
	 * For data lanes more than maximum csi lanes per brick, multiple of
	 * x4 ports are used simultaneously for capture.
	 */
	if (lanes <= CSI_LANES_PER_BRICK)
		chan->totalports = 1;
	else
		chan->totalports = lanes / CSI_LANES_PER_BRICK;
	chan->numgangports = chan->totalports;

	for (i = 1; i < chan->totalports; i++)
		chan->portnos[i] = chan->portnos[0] + i * CSI_PORTS_PER_BRICK;

	chan->of_node = node;
	list_add_tail(&chan->list, &vi->vi_chans);

	return 0;
}

static int tegra_vi_tpg_channels_alloc(struct tegra_vi *vi)
{
	unsigned int port_num;
	unsigned int nchannels = vi->soc->vi_max_channels;
	int ret;

	for (port_num = 0; port_num < nchannels; port_num++) {
		ret = tegra_vi_channel_alloc(vi, port_num,
					     vi->dev->of_node, 2);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tegra_vi_channels_alloc(struct tegra_vi *vi)
{
	struct device_node *node = vi->dev->of_node;
	struct device_node *ep = NULL;
	struct device_node *ports;
	struct device_node *port = NULL;
	unsigned int port_num;
	struct device_node *parent;
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	unsigned int lanes;
	int ret = 0;

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		return dev_err_probe(vi->dev, -ENODEV, "%pOF: missing 'ports' node\n", node);

	for_each_child_of_node(ports, port) {
		if (!of_node_name_eq(port, "port"))
			continue;

		ret = of_property_read_u32(port, "reg", &port_num);
		if (ret < 0)
			continue;

		if (port_num > vi->soc->vi_max_channels) {
			dev_err(vi->dev, "invalid port num %d for %pOF\n",
				port_num, port);
			ret = -EINVAL;
			goto cleanup;
		}

		ep = of_get_child_by_name(port, "endpoint");
		if (!ep)
			continue;

		parent = of_graph_get_remote_port_parent(ep);
		of_node_put(ep);
		if (!parent)
			continue;

		ep = of_graph_get_endpoint_by_regs(parent, 0, 0);
		of_node_put(parent);
		ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep),
						 &v4l2_ep);
		of_node_put(ep);
		if (ret)
			continue;

		lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;
		ret = tegra_vi_channel_alloc(vi, port_num, port, lanes);
		if (ret < 0)
			goto cleanup;
	}

cleanup:
	of_node_put(port);
	of_node_put(ports);
	return ret;
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
				chan->portnos[0], ret);
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

	list_for_each_entry(chan, &vi->vi_chans, list)
		vb2_video_unregister_device(&chan->video);

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

/*
 * Graph Management
 */
static struct tegra_vi_graph_entity *
tegra_vi_graph_find_entity(struct tegra_vi_channel *chan,
			   const struct fwnode_handle *fwnode)
{
	struct tegra_vi_graph_entity *entity;
	struct v4l2_async_connection *asd;

	list_for_each_entry(asd, &chan->notifier.done_list, asc_entry) {
		entity = to_tegra_vi_graph_entity(asd);
		if (entity->asd.match.fwnode == fwnode)
			return entity;
	}

	return NULL;
}

static int tegra_vi_graph_build(struct tegra_vi_channel *chan,
				struct tegra_vi_graph_entity *entity)
{
	struct tegra_vi *vi = chan->vi;
	struct tegra_vi_graph_entity *ent;
	struct fwnode_handle *ep = NULL;
	struct v4l2_fwnode_link link;
	struct media_entity *local = entity->entity;
	struct media_entity *remote;
	struct media_pad *local_pad;
	struct media_pad *remote_pad;
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	int ret = 0;

	dev_dbg(vi->dev, "creating links for entity %s\n", local->name);

	while (1) {
		ep = fwnode_graph_get_next_endpoint(entity->asd.match.fwnode,
						    ep);
		if (!ep)
			break;

		ret = v4l2_fwnode_parse_link(ep, &link);
		if (ret < 0) {
			dev_err(vi->dev, "failed to parse link for %pOF: %d\n",
				to_of_node(ep), ret);
			continue;
		}

		if (link.local_port >= local->num_pads) {
			dev_err(vi->dev, "invalid port number %u on %pOF\n",
				link.local_port, to_of_node(link.local_node));
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		local_pad = &local->pads[link.local_port];
		/* Remote node is vi node. So use channel video entity and pad
		 * as remote/sink.
		 */
		if (link.remote_node == of_fwnode_handle(vi->dev->of_node)) {
			remote = &chan->video.entity;
			remote_pad = &chan->pad;
			goto create_link;
		}

		/*
		 * Skip sink ports, they will be processed from the other end
		 * of the link.
		 */
		if (local_pad->flags & MEDIA_PAD_FL_SINK) {
			dev_dbg(vi->dev, "skipping sink port %pOF:%u\n",
				to_of_node(link.local_node), link.local_port);
			v4l2_fwnode_put_link(&link);
			continue;
		}

		/* find the remote entity from notifier list */
		ent = tegra_vi_graph_find_entity(chan, link.remote_node);
		if (!ent) {
			dev_err(vi->dev, "no entity found for %pOF\n",
				to_of_node(link.remote_node));
			v4l2_fwnode_put_link(&link);
			ret = -ENODEV;
			break;
		}

		remote = ent->entity;
		if (link.remote_port >= remote->num_pads) {
			dev_err(vi->dev, "invalid port number %u on %pOF\n",
				link.remote_port,
				to_of_node(link.remote_node));
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		remote_pad = &remote->pads[link.remote_port];

create_link:
		dev_dbg(vi->dev, "creating %s:%u -> %s:%u link\n",
			local->name, local_pad->index,
			remote->name, remote_pad->index);

		ret = media_create_pad_link(local, local_pad->index,
					    remote, remote_pad->index,
					    link_flags);
		v4l2_fwnode_put_link(&link);
		if (ret < 0) {
			dev_err(vi->dev,
				"failed to create %s:%u -> %s:%u link: %d\n",
				local->name, local_pad->index,
				remote->name, remote_pad->index, ret);
			break;
		}
	}

	fwnode_handle_put(ep);
	return ret;
}

static int tegra_vi_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct tegra_vi_graph_entity *entity;
	struct v4l2_async_connection *asd;
	struct v4l2_subdev *subdev;
	struct tegra_vi_channel *chan;
	struct tegra_vi *vi;
	int ret;

	chan = container_of(notifier, struct tegra_vi_channel, notifier);
	vi = chan->vi;

	dev_dbg(vi->dev, "notify complete, all subdevs registered\n");

	/*
	 * Video device node should be created at the end of all the device
	 * related initialization/setup.
	 * Current video_register_device() does both initialize and register
	 * video device in same API.
	 *
	 * TODO: Update v4l2-dev driver to split initialize and register into
	 * separate APIs and then update Tegra video driver to do video device
	 * initialize followed by all video device related setup and then
	 * register the video device.
	 */
	ret = video_register_device(&chan->video, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(vi->dev,
			"failed to register video device: %d\n", ret);
		goto unregister_video;
	}

	/* create links between the entities */
	list_for_each_entry(asd, &chan->notifier.done_list, asc_entry) {
		entity = to_tegra_vi_graph_entity(asd);
		ret = tegra_vi_graph_build(chan, entity);
		if (ret < 0)
			goto unregister_video;
	}

	ret = tegra_channel_setup_ctrl_handler(chan);
	if (ret < 0) {
		dev_err(vi->dev,
			"failed to setup channel controls: %d\n", ret);
		goto unregister_video;
	}

	ret = vi_fmts_bitmap_init(chan);
	if (ret < 0) {
		dev_err(vi->dev,
			"failed to initialize formats bitmap: %d\n", ret);
		goto unregister_video;
	}

	subdev = tegra_channel_get_remote_csi_subdev(chan);
	if (!subdev) {
		ret = -ENODEV;
		dev_err(vi->dev,
			"failed to get remote csi subdev: %d\n", ret);
		goto unregister_video;
	}

	v4l2_set_subdev_hostdata(subdev, chan);

	subdev = tegra_channel_get_remote_source_subdev(chan);
	v4l2_set_subdev_hostdata(subdev, chan);

	return 0;

unregister_video:
	vb2_video_unregister_device(&chan->video);
	return ret;
}

static int tegra_vi_graph_notify_bound(struct v4l2_async_notifier *notifier,
				       struct v4l2_subdev *subdev,
				       struct v4l2_async_connection *asd)
{
	struct tegra_vi_graph_entity *entity;
	struct tegra_vi *vi;
	struct tegra_vi_channel *chan;

	chan = container_of(notifier, struct tegra_vi_channel, notifier);
	vi = chan->vi;

	/*
	 * Locate the entity corresponding to the bound subdev and store the
	 * subdev pointer.
	 */
	entity = tegra_vi_graph_find_entity(chan, subdev->fwnode);
	if (!entity) {
		dev_err(vi->dev, "no entity for subdev %s\n", subdev->name);
		return -EINVAL;
	}

	if (entity->subdev) {
		dev_err(vi->dev, "duplicate subdev for node %pOF\n",
			to_of_node(entity->asd.match.fwnode));
		return -EINVAL;
	}

	dev_dbg(vi->dev, "subdev %s bound\n", subdev->name);
	entity->entity = &subdev->entity;
	entity->subdev = subdev;

	return 0;
}

static const struct v4l2_async_notifier_operations tegra_vi_async_ops = {
	.bound = tegra_vi_graph_notify_bound,
	.complete = tegra_vi_graph_notify_complete,
};

static int tegra_vi_graph_parse_one(struct tegra_vi_channel *chan,
				    struct fwnode_handle *fwnode)
{
	struct tegra_vi *vi = chan->vi;
	struct fwnode_handle *ep = NULL;
	struct fwnode_handle *remote = NULL;
	struct tegra_vi_graph_entity *tvge;
	struct device_node *node = NULL;
	int ret;

	dev_dbg(vi->dev, "parsing node %pOF\n", to_of_node(fwnode));

	/* parse all the remote entities and put them into the list */
	for_each_endpoint_of_node(to_of_node(fwnode), node) {
		ep = of_fwnode_handle(node);
		remote = fwnode_graph_get_remote_port_parent(ep);
		if (!remote) {
			dev_err(vi->dev,
				"remote device at %pOF not found\n", node);
			ret = -EINVAL;
			goto cleanup;
		}

		/* skip entities that are already processed */
		if (device_match_fwnode(vi->dev, remote) ||
		    tegra_vi_graph_find_entity(chan, remote)) {
			fwnode_handle_put(remote);
			continue;
		}

		tvge = v4l2_async_nf_add_fwnode(&chan->notifier, remote,
						struct tegra_vi_graph_entity);
		if (IS_ERR(tvge)) {
			ret = PTR_ERR(tvge);
			dev_err(vi->dev,
				"failed to add subdev to notifier: %d\n", ret);
			fwnode_handle_put(remote);
			goto cleanup;
		}

		ret = tegra_vi_graph_parse_one(chan, remote);
		if (ret < 0) {
			fwnode_handle_put(remote);
			goto cleanup;
		}

		fwnode_handle_put(remote);
	}

	return 0;

cleanup:
	dev_err(vi->dev, "failed parsing the graph: %d\n", ret);
	v4l2_async_nf_cleanup(&chan->notifier);
	of_node_put(node);
	return ret;
}

static int tegra_vi_graph_init(struct tegra_vi *vi)
{
	struct tegra_vi_channel *chan;
	struct fwnode_handle *fwnode = dev_fwnode(vi->dev);
	int ret;

	/*
	 * Walk the links to parse the full graph. Each channel will have
	 * one endpoint of the composite node. Start by parsing the
	 * composite node and parse the remote entities in turn.
	 * Each channel will register a v4l2 async notifier to make the graph
	 * independent between the channels so we can skip the current channel
	 * in case of something wrong during graph parsing and continue with
	 * the next channels.
	 */
	list_for_each_entry(chan, &vi->vi_chans, list) {
		struct fwnode_handle *ep, *remote;

		ep = fwnode_graph_get_endpoint_by_id(fwnode,
						     chan->portnos[0], 0, 0);
		if (!ep)
			continue;

		remote = fwnode_graph_get_remote_port_parent(ep);
		fwnode_handle_put(ep);

		ret = tegra_vi_graph_parse_one(chan, remote);
		fwnode_handle_put(remote);
		if (ret < 0 || list_empty(&chan->notifier.waiting_list))
			continue;

		chan->notifier.ops = &tegra_vi_async_ops;
		ret = v4l2_async_nf_register(&chan->notifier);
		if (ret < 0) {
			dev_err(vi->dev,
				"failed to register channel %d notifier: %d\n",
				chan->portnos[0], ret);
			v4l2_async_nf_cleanup(&chan->notifier);
		}
	}

	return 0;
}

static void tegra_vi_graph_cleanup(struct tegra_vi *vi)
{
	struct tegra_vi_channel *chan;

	list_for_each_entry(chan, &vi->vi_chans, list) {
		vb2_video_unregister_device(&chan->video);
		v4l2_async_nf_unregister(&chan->notifier);
		v4l2_async_nf_cleanup(&chan->notifier);
	}
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

	if (IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		ret = tegra_vi_tpg_channels_alloc(vi);
	else
		ret = tegra_vi_channels_alloc(vi);
	if (ret < 0)
		goto free_chans;

	ret = tegra_vi_channels_init(vi);
	if (ret < 0)
		goto free_chans;

	vid->vi = vi;

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG)) {
		ret = tegra_vi_graph_init(vi);
		if (ret < 0)
			goto cleanup_chans;
	}

	return 0;

cleanup_chans:
	list_for_each_entry(chan, &vi->vi_chans, list)
		tegra_channel_cleanup(chan);
free_chans:
	list_for_each_entry_safe(chan, tmp, &vi->vi_chans, list) {
		list_del(&chan->list);
		kfree(chan);
	}

	return ret;
}

static int tegra_vi_exit(struct host1x_client *client)
{
	struct tegra_vi *vi = host1x_client_to_vi(client);

	/*
	 * Do not cleanup the channels here as application might still be
	 * holding video device nodes. Channels cleanup will happen during
	 * v4l2_device release callback which gets called after all video
	 * device nodes are released.
	 */

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		tegra_vi_graph_cleanup(vi);

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

	if (vi->ops->vi_enable)
		vi->ops->vi_enable(vi, true);

	ret = host1x_client_register(&vi->client);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to register host1x client: %d\n", ret);
		goto rpm_disable;
	}

	return 0;

rpm_disable:
	if (vi->ops->vi_enable)
		vi->ops->vi_enable(vi, false);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int tegra_vi_remove(struct platform_device *pdev)
{
	struct tegra_vi *vi = platform_get_drvdata(pdev);

	host1x_client_unregister(&vi->client);

	if (vi->ops->vi_enable)
		vi->ops->vi_enable(vi, false);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id tegra_vi_of_id_table[] = {
#if defined(CONFIG_ARCH_TEGRA_2x_SOC)
	{ .compatible = "nvidia,tegra20-vi",  .data = &tegra20_vi_soc },
#endif
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
