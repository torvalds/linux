// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung EXYNOS FIMC-LITE (camera host interface) driver
*
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 */
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/drv-intf/exynos-fimc.h>

#include "common.h"
#include "fimc-core.h"
#include "fimc-lite.h"
#include "fimc-lite-reg.h"

static int debug;
module_param(debug, int, 0644);

static const struct fimc_fmt fimc_lite_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.depth		= { 16 },
		.color		= FIMC_FMT_YCBYCR422,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.flags		= FMT_FLAGS_YUV,
	}, {
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.depth		= { 16 },
		.color		= FIMC_FMT_CBYCRY422,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.flags		= FMT_FLAGS_YUV,
	}, {
		.fourcc		= V4L2_PIX_FMT_VYUY,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.depth		= { 16 },
		.color		= FIMC_FMT_CRYCBY422,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.flags		= FMT_FLAGS_YUV,
	}, {
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.depth		= { 16 },
		.color		= FIMC_FMT_YCRYCB422,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.flags		= FMT_FLAGS_YUV,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.depth		= { 8 },
		.color		= FIMC_FMT_RAW8,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.flags		= FMT_FLAGS_RAW_BAYER,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.depth		= { 16 },
		.color		= FIMC_FMT_RAW10,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.flags		= FMT_FLAGS_RAW_BAYER,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.depth		= { 16 },
		.color		= FIMC_FMT_RAW12,
		.memplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.flags		= FMT_FLAGS_RAW_BAYER,
	},
};

/**
 * fimc_lite_find_format - lookup fimc color format by fourcc or media bus code
 * @pixelformat: fourcc to match, ignored if null
 * @mbus_code: media bus code to match, ignored if null
 * @mask: the color format flags to match
 * @index: index to the fimc_lite_formats array, ignored if negative
 */
static const struct fimc_fmt *fimc_lite_find_format(const u32 *pixelformat,
			const u32 *mbus_code, unsigned int mask, int index)
{
	const struct fimc_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= (int)ARRAY_SIZE(fimc_lite_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(fimc_lite_formats); ++i) {
		fmt = &fimc_lite_formats[i];
		if (mask && !(fmt->flags & mask))
			continue;
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

static int fimc_lite_hw_init(struct fimc_lite *fimc, bool isp_output)
{
	struct fimc_source_info *si;
	unsigned long flags;

	if (fimc->sensor == NULL)
		return -ENXIO;

	if (fimc->inp_frame.fmt == NULL || fimc->out_frame.fmt == NULL)
		return -EINVAL;

	/* Get sensor configuration data from the sensor subdev */
	si = v4l2_get_subdev_hostdata(fimc->sensor);
	if (!si)
		return -EINVAL;

	spin_lock_irqsave(&fimc->slock, flags);

	flite_hw_set_camera_bus(fimc, si);
	flite_hw_set_source_format(fimc, &fimc->inp_frame);
	flite_hw_set_window_offset(fimc, &fimc->inp_frame);
	flite_hw_set_dma_buf_mask(fimc, 0);
	flite_hw_set_output_dma(fimc, &fimc->out_frame, !isp_output);
	flite_hw_set_interrupt_mask(fimc);
	flite_hw_set_test_pattern(fimc, fimc->test_pattern->val);

	if (debug > 0)
		flite_hw_dump_regs(fimc, __func__);

	spin_unlock_irqrestore(&fimc->slock, flags);
	return 0;
}

/*
 * Reinitialize the driver so it is ready to start the streaming again.
 * Set fimc->state to indicate stream off and the hardware shut down state.
 * If not suspending (@suspend is false), return any buffers to videobuf2.
 * Otherwise put any owned buffers onto the pending buffers queue, so they
 * can be re-spun when the device is being resumed. Also perform FIMC
 * software reset and disable streaming on the whole pipeline if required.
 */
static int fimc_lite_reinit(struct fimc_lite *fimc, bool suspend)
{
	struct flite_buffer *buf;
	unsigned long flags;
	bool streaming;

	spin_lock_irqsave(&fimc->slock, flags);
	streaming = fimc->state & (1 << ST_SENSOR_STREAM);

	fimc->state &= ~(1 << ST_FLITE_RUN | 1 << ST_FLITE_OFF |
			 1 << ST_FLITE_STREAM | 1 << ST_SENSOR_STREAM);
	if (suspend)
		fimc->state |= (1 << ST_FLITE_SUSPENDED);
	else
		fimc->state &= ~(1 << ST_FLITE_PENDING |
				 1 << ST_FLITE_SUSPENDED);

	/* Release unused buffers */
	while (!suspend && !list_empty(&fimc->pending_buf_q)) {
		buf = fimc_lite_pending_queue_pop(fimc);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	/* If suspending put unused buffers onto pending queue */
	while (!list_empty(&fimc->active_buf_q)) {
		buf = fimc_lite_active_queue_pop(fimc);
		if (suspend)
			fimc_lite_pending_queue_add(fimc, buf);
		else
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&fimc->slock, flags);

	flite_hw_reset(fimc);

	if (!streaming)
		return 0;

	return fimc_pipeline_call(&fimc->ve, set_stream, 0);
}

static int fimc_lite_stop_capture(struct fimc_lite *fimc, bool suspend)
{
	unsigned long flags;

	if (!fimc_lite_active(fimc))
		return 0;

	spin_lock_irqsave(&fimc->slock, flags);
	set_bit(ST_FLITE_OFF, &fimc->state);
	flite_hw_capture_stop(fimc);
	spin_unlock_irqrestore(&fimc->slock, flags);

	wait_event_timeout(fimc->irq_queue,
			   !test_bit(ST_FLITE_OFF, &fimc->state),
			   (2*HZ/10)); /* 200 ms */

	return fimc_lite_reinit(fimc, suspend);
}

/* Must be called  with fimc.slock spinlock held. */
static void fimc_lite_config_update(struct fimc_lite *fimc)
{
	flite_hw_set_window_offset(fimc, &fimc->inp_frame);
	flite_hw_set_dma_window(fimc, &fimc->out_frame);
	flite_hw_set_test_pattern(fimc, fimc->test_pattern->val);
	clear_bit(ST_FLITE_CONFIG, &fimc->state);
}

static irqreturn_t flite_irq_handler(int irq, void *priv)
{
	struct fimc_lite *fimc = priv;
	struct flite_buffer *vbuf;
	unsigned long flags;
	u32 intsrc;

	spin_lock_irqsave(&fimc->slock, flags);

	intsrc = flite_hw_get_interrupt_source(fimc);
	flite_hw_clear_pending_irq(fimc);

	if (test_and_clear_bit(ST_FLITE_OFF, &fimc->state)) {
		wake_up(&fimc->irq_queue);
		goto done;
	}

	if (intsrc & FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW) {
		clear_bit(ST_FLITE_RUN, &fimc->state);
		fimc->events.data_overflow++;
	}

	if (intsrc & FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND) {
		flite_hw_clear_last_capture_end(fimc);
		clear_bit(ST_FLITE_STREAM, &fimc->state);
		wake_up(&fimc->irq_queue);
	}

	if (atomic_read(&fimc->out_path) != FIMC_IO_DMA)
		goto done;

	if ((intsrc & FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART) &&
	    test_bit(ST_FLITE_RUN, &fimc->state) &&
	    !list_empty(&fimc->pending_buf_q)) {
		vbuf = fimc_lite_pending_queue_pop(fimc);
		flite_hw_set_dma_buffer(fimc, vbuf);
		fimc_lite_active_queue_add(fimc, vbuf);
	}

	if ((intsrc & FLITE_REG_CISTATUS_IRQ_SRC_FRMEND) &&
	    test_bit(ST_FLITE_RUN, &fimc->state) &&
	    !list_empty(&fimc->active_buf_q)) {
		vbuf = fimc_lite_active_queue_pop(fimc);
		vbuf->vb.vb2_buf.timestamp = ktime_get_ns();
		vbuf->vb.sequence = fimc->frame_count++;
		flite_hw_mask_dma_buffer(fimc, vbuf->index);
		vb2_buffer_done(&vbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	if (test_bit(ST_FLITE_CONFIG, &fimc->state))
		fimc_lite_config_update(fimc);

	if (list_empty(&fimc->pending_buf_q)) {
		flite_hw_capture_stop(fimc);
		clear_bit(ST_FLITE_STREAM, &fimc->state);
	}
done:
	set_bit(ST_FLITE_RUN, &fimc->state);
	spin_unlock_irqrestore(&fimc->slock, flags);
	return IRQ_HANDLED;
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct fimc_lite *fimc = q->drv_priv;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&fimc->slock, flags);

	fimc->buf_index = 0;
	fimc->frame_count = 0;

	spin_unlock_irqrestore(&fimc->slock, flags);

	ret = fimc_lite_hw_init(fimc, false);
	if (ret) {
		fimc_lite_reinit(fimc, false);
		return ret;
	}

	set_bit(ST_FLITE_PENDING, &fimc->state);

	if (!list_empty(&fimc->active_buf_q) &&
	    !test_and_set_bit(ST_FLITE_STREAM, &fimc->state)) {
		flite_hw_capture_start(fimc);

		if (!test_and_set_bit(ST_SENSOR_STREAM, &fimc->state))
			fimc_pipeline_call(&fimc->ve, set_stream, 1);
	}
	if (debug > 0)
		flite_hw_dump_regs(fimc, __func__);

	return 0;
}

static void stop_streaming(struct vb2_queue *q)
{
	struct fimc_lite *fimc = q->drv_priv;

	if (!fimc_lite_active(fimc))
		return;

	fimc_lite_stop_capture(fimc, false);
}

static int queue_setup(struct vb2_queue *vq,
		       unsigned int *num_buffers, unsigned int *num_planes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct fimc_lite *fimc = vq->drv_priv;
	struct flite_frame *frame = &fimc->out_frame;
	const struct fimc_fmt *fmt = frame->fmt;
	unsigned long wh = frame->f_width * frame->f_height;
	int i;

	if (fmt == NULL)
		return -EINVAL;

	if (*num_planes) {
		if (*num_planes != fmt->memplanes)
			return -EINVAL;
		for (i = 0; i < *num_planes; i++)
			if (sizes[i] < (wh * fmt->depth[i]) / 8)
				return -EINVAL;
		return 0;
	}

	*num_planes = fmt->memplanes;

	for (i = 0; i < fmt->memplanes; i++)
		sizes[i] = (wh * fmt->depth[i]) / 8;

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct fimc_lite *fimc = vq->drv_priv;
	int i;

	if (fimc->out_frame.fmt == NULL)
		return -EINVAL;

	for (i = 0; i < fimc->out_frame.fmt->memplanes; i++) {
		unsigned long size = fimc->payload[i];

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(&fimc->ve.vdev,
				 "User buffer too small (%ld < %ld)\n",
				 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct flite_buffer *buf
		= container_of(vbuf, struct flite_buffer, vb);
	struct fimc_lite *fimc = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long flags;

	spin_lock_irqsave(&fimc->slock, flags);
	buf->paddr = vb2_dma_contig_plane_dma_addr(vb, 0);

	buf->index = fimc->buf_index++;
	if (fimc->buf_index >= fimc->reqbufs_count)
		fimc->buf_index = 0;

	if (!test_bit(ST_FLITE_SUSPENDED, &fimc->state) &&
	    !test_bit(ST_FLITE_STREAM, &fimc->state) &&
	    list_empty(&fimc->active_buf_q)) {
		flite_hw_set_dma_buffer(fimc, buf);
		fimc_lite_active_queue_add(fimc, buf);
	} else {
		fimc_lite_pending_queue_add(fimc, buf);
	}

	if (vb2_is_streaming(&fimc->vb_queue) &&
	    !list_empty(&fimc->pending_buf_q) &&
	    !test_and_set_bit(ST_FLITE_STREAM, &fimc->state)) {
		flite_hw_capture_start(fimc);
		spin_unlock_irqrestore(&fimc->slock, flags);

		if (!test_and_set_bit(ST_SENSOR_STREAM, &fimc->state))
			fimc_pipeline_call(&fimc->ve, set_stream, 1);
		return;
	}
	spin_unlock_irqrestore(&fimc->slock, flags);
}

static const struct vb2_ops fimc_lite_qops = {
	.queue_setup	 = queue_setup,
	.buf_prepare	 = buffer_prepare,
	.buf_queue	 = buffer_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.start_streaming = start_streaming,
	.stop_streaming	 = stop_streaming,
};

static void fimc_lite_clear_event_counters(struct fimc_lite *fimc)
{
	unsigned long flags;

	spin_lock_irqsave(&fimc->slock, flags);
	memset(&fimc->events, 0, sizeof(fimc->events));
	spin_unlock_irqrestore(&fimc->slock, flags);
}

static int fimc_lite_open(struct file *file)
{
	struct fimc_lite *fimc = video_drvdata(file);
	struct media_entity *me = &fimc->ve.vdev.entity;
	int ret;

	mutex_lock(&fimc->lock);
	if (atomic_read(&fimc->out_path) != FIMC_IO_DMA) {
		ret = -EBUSY;
		goto unlock;
	}

	set_bit(ST_FLITE_IN_USE, &fimc->state);
	ret = pm_runtime_get_sync(&fimc->pdev->dev);
	if (ret < 0)
		goto unlock;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto err_pm;

	if (!v4l2_fh_is_singular_file(file) ||
	    atomic_read(&fimc->out_path) != FIMC_IO_DMA)
		goto unlock;

	mutex_lock(&me->graph_obj.mdev->graph_mutex);

	ret = fimc_pipeline_call(&fimc->ve, open, me, true);

	/* Mark video pipeline ending at this video node as in use. */
	if (ret == 0)
		me->use_count++;

	mutex_unlock(&me->graph_obj.mdev->graph_mutex);

	if (!ret) {
		fimc_lite_clear_event_counters(fimc);
		goto unlock;
	}

	v4l2_fh_release(file);
err_pm:
	pm_runtime_put_sync(&fimc->pdev->dev);
	clear_bit(ST_FLITE_IN_USE, &fimc->state);
unlock:
	mutex_unlock(&fimc->lock);
	return ret;
}

static int fimc_lite_release(struct file *file)
{
	struct fimc_lite *fimc = video_drvdata(file);
	struct media_entity *entity = &fimc->ve.vdev.entity;

	mutex_lock(&fimc->lock);

	if (v4l2_fh_is_singular_file(file) &&
	    atomic_read(&fimc->out_path) == FIMC_IO_DMA) {
		if (fimc->streaming) {
			media_pipeline_stop(entity);
			fimc->streaming = false;
		}
		fimc_lite_stop_capture(fimc, false);
		fimc_pipeline_call(&fimc->ve, close);
		clear_bit(ST_FLITE_IN_USE, &fimc->state);

		mutex_lock(&entity->graph_obj.mdev->graph_mutex);
		entity->use_count--;
		mutex_unlock(&entity->graph_obj.mdev->graph_mutex);
	}

	_vb2_fop_release(file, NULL);
	pm_runtime_put(&fimc->pdev->dev);
	clear_bit(ST_FLITE_SUSPENDED, &fimc->state);

	mutex_unlock(&fimc->lock);
	return 0;
}

static const struct v4l2_file_operations fimc_lite_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_lite_open,
	.release	= fimc_lite_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/*
 * Format and crop negotiation helpers
 */

static const struct fimc_fmt *fimc_lite_subdev_try_fmt(struct fimc_lite *fimc,
					struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_format *format)
{
	struct flite_drvdata *dd = fimc->dd;
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct fimc_fmt *fmt = NULL;

	if (format->pad == FLITE_SD_PAD_SINK) {
		v4l_bound_align_image(&mf->width, 8, dd->max_width,
				ffs(dd->out_width_align) - 1,
				&mf->height, 0, dd->max_height, 0, 0);

		fmt = fimc_lite_find_format(NULL, &mf->code, 0, 0);
		if (WARN_ON(!fmt))
			return NULL;

		mf->colorspace = fmt->colorspace;
		mf->code = fmt->mbus_code;
	} else {
		struct flite_frame *sink = &fimc->inp_frame;
		struct v4l2_mbus_framefmt *sink_fmt;
		struct v4l2_rect *rect;

		if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
			sink_fmt = v4l2_subdev_get_try_format(&fimc->subdev, cfg,
						FLITE_SD_PAD_SINK);

			mf->code = sink_fmt->code;
			mf->colorspace = sink_fmt->colorspace;

			rect = v4l2_subdev_get_try_crop(&fimc->subdev, cfg,
						FLITE_SD_PAD_SINK);
		} else {
			mf->code = sink->fmt->mbus_code;
			mf->colorspace = sink->fmt->colorspace;
			rect = &sink->rect;
		}

		/* Allow changing format only on sink pad */
		mf->width = rect->width;
		mf->height = rect->height;
	}

	mf->field = V4L2_FIELD_NONE;

	v4l2_dbg(1, debug, &fimc->subdev, "code: %#x (%d), %dx%d\n",
		 mf->code, mf->colorspace, mf->width, mf->height);

	return fmt;
}

static void fimc_lite_try_crop(struct fimc_lite *fimc, struct v4l2_rect *r)
{
	struct flite_frame *frame = &fimc->inp_frame;

	v4l_bound_align_image(&r->width, 0, frame->f_width, 0,
			      &r->height, 0, frame->f_height, 0, 0);

	/* Adjust left/top if cropping rectangle got out of bounds */
	r->left = clamp_t(u32, r->left, 0, frame->f_width - r->width);
	r->left = round_down(r->left, fimc->dd->win_hor_offs_align);
	r->top  = clamp_t(u32, r->top, 0, frame->f_height - r->height);

	v4l2_dbg(1, debug, &fimc->subdev, "(%d,%d)/%dx%d, sink fmt: %dx%d\n",
		 r->left, r->top, r->width, r->height,
		 frame->f_width, frame->f_height);
}

static void fimc_lite_try_compose(struct fimc_lite *fimc, struct v4l2_rect *r)
{
	struct flite_frame *frame = &fimc->out_frame;
	struct v4l2_rect *crop_rect = &fimc->inp_frame.rect;

	/* Scaling is not supported so we enforce compose rectangle size
	   same as size of the sink crop rectangle. */
	r->width = crop_rect->width;
	r->height = crop_rect->height;

	/* Adjust left/top if the composing rectangle got out of bounds */
	r->left = clamp_t(u32, r->left, 0, frame->f_width - r->width);
	r->left = round_down(r->left, fimc->dd->out_hor_offs_align);
	r->top  = clamp_t(u32, r->top, 0, fimc->out_frame.f_height - r->height);

	v4l2_dbg(1, debug, &fimc->subdev, "(%d,%d)/%dx%d, source fmt: %dx%d\n",
		 r->left, r->top, r->width, r->height,
		 frame->f_width, frame->f_height);
}

/*
 * Video node ioctl operations
 */
static int fimc_lite_querycap(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct fimc_lite *fimc = video_drvdata(file);

	strscpy(cap->driver, FIMC_LITE_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, FIMC_LITE_DRV_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
					dev_name(&fimc->pdev->dev));
	return 0;
}

static int fimc_lite_enum_fmt(struct file *file, void *priv,
			      struct v4l2_fmtdesc *f)
{
	const struct fimc_fmt *fmt;

	if (f->index >= ARRAY_SIZE(fimc_lite_formats))
		return -EINVAL;

	fmt = &fimc_lite_formats[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int fimc_lite_g_fmt_mplane(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct fimc_lite *fimc = video_drvdata(file);
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt = &pixm->plane_fmt[0];
	struct flite_frame *frame = &fimc->out_frame;
	const struct fimc_fmt *fmt = frame->fmt;

	plane_fmt->bytesperline = (frame->f_width * fmt->depth[0]) / 8;
	plane_fmt->sizeimage = plane_fmt->bytesperline * frame->f_height;

	pixm->num_planes = fmt->memplanes;
	pixm->pixelformat = fmt->fourcc;
	pixm->width = frame->f_width;
	pixm->height = frame->f_height;
	pixm->field = V4L2_FIELD_NONE;
	pixm->colorspace = fmt->colorspace;
	return 0;
}

static int fimc_lite_try_fmt(struct fimc_lite *fimc,
			     struct v4l2_pix_format_mplane *pixm,
			     const struct fimc_fmt **ffmt)
{
	u32 bpl = pixm->plane_fmt[0].bytesperline;
	struct flite_drvdata *dd = fimc->dd;
	const struct fimc_fmt *inp_fmt = fimc->inp_frame.fmt;
	const struct fimc_fmt *fmt;

	if (WARN_ON(inp_fmt == NULL))
		return -EINVAL;
	/*
	 * We allow some flexibility only for YUV formats. In case of raw
	 * raw Bayer the FIMC-LITE's output format must match its camera
	 * interface input format.
	 */
	if (inp_fmt->flags & FMT_FLAGS_YUV)
		fmt = fimc_lite_find_format(&pixm->pixelformat, NULL,
						inp_fmt->flags, 0);
	else
		fmt = inp_fmt;

	if (WARN_ON(fmt == NULL))
		return -EINVAL;
	if (ffmt)
		*ffmt = fmt;
	v4l_bound_align_image(&pixm->width, 8, dd->max_width,
			      ffs(dd->out_width_align) - 1,
			      &pixm->height, 0, dd->max_height, 0, 0);

	if ((bpl == 0 || ((bpl * 8) / fmt->depth[0]) < pixm->width))
		pixm->plane_fmt[0].bytesperline = (pixm->width *
						   fmt->depth[0]) / 8;

	if (pixm->plane_fmt[0].sizeimage == 0)
		pixm->plane_fmt[0].sizeimage = (pixm->width * pixm->height *
						fmt->depth[0]) / 8;
	pixm->num_planes = fmt->memplanes;
	pixm->pixelformat = fmt->fourcc;
	pixm->colorspace = fmt->colorspace;
	pixm->field = V4L2_FIELD_NONE;
	return 0;
}

static int fimc_lite_try_fmt_mplane(struct file *file, void *fh,
				    struct v4l2_format *f)
{
	struct fimc_lite *fimc = video_drvdata(file);
	return fimc_lite_try_fmt(fimc, &f->fmt.pix_mp, NULL);
}

static int fimc_lite_s_fmt_mplane(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	struct fimc_lite *fimc = video_drvdata(file);
	struct flite_frame *frame = &fimc->out_frame;
	const struct fimc_fmt *fmt = NULL;
	int ret;

	if (vb2_is_busy(&fimc->vb_queue))
		return -EBUSY;

	ret = fimc_lite_try_fmt(fimc, &f->fmt.pix_mp, &fmt);
	if (ret < 0)
		return ret;

	frame->fmt = fmt;
	fimc->payload[0] = max((pixm->width * pixm->height * fmt->depth[0]) / 8,
			       pixm->plane_fmt[0].sizeimage);
	frame->f_width = pixm->width;
	frame->f_height = pixm->height;

	return 0;
}

static int fimc_pipeline_validate(struct fimc_lite *fimc)
{
	struct v4l2_subdev *sd = &fimc->subdev;
	struct v4l2_subdev_format sink_fmt, src_fmt;
	struct media_pad *pad;
	int ret;

	while (1) {
		/* Retrieve format at the sink pad */
		pad = &sd->entity.pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;
		/* Don't call FIMC subdev operation to avoid nested locking */
		if (sd == &fimc->subdev) {
			struct flite_frame *ff = &fimc->out_frame;
			sink_fmt.format.width = ff->f_width;
			sink_fmt.format.height = ff->f_height;
			sink_fmt.format.code = fimc->inp_frame.fmt->mbus_code;
		} else {
			sink_fmt.pad = pad->index;
			sink_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sd, pad, get_fmt, NULL,
					       &sink_fmt);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return -EPIPE;
		}
		/* Retrieve format at the source pad */
		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		src_fmt.pad = pad->index;
		src_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &src_fmt);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return -EPIPE;

		if (src_fmt.format.width != sink_fmt.format.width ||
		    src_fmt.format.height != sink_fmt.format.height ||
		    src_fmt.format.code != sink_fmt.format.code)
			return -EPIPE;
	}
	return 0;
}

static int fimc_lite_streamon(struct file *file, void *priv,
			      enum v4l2_buf_type type)
{
	struct fimc_lite *fimc = video_drvdata(file);
	struct media_entity *entity = &fimc->ve.vdev.entity;
	int ret;

	if (fimc_lite_active(fimc))
		return -EBUSY;

	ret = media_pipeline_start(entity, &fimc->ve.pipe->mp);
	if (ret < 0)
		return ret;

	ret = fimc_pipeline_validate(fimc);
	if (ret < 0)
		goto err_p_stop;

	fimc->sensor = fimc_find_remote_sensor(&fimc->subdev.entity);

	ret = vb2_ioctl_streamon(file, priv, type);
	if (!ret) {
		fimc->streaming = true;
		return ret;
	}

err_p_stop:
	media_pipeline_stop(entity);
	return 0;
}

static int fimc_lite_streamoff(struct file *file, void *priv,
			       enum v4l2_buf_type type)
{
	struct fimc_lite *fimc = video_drvdata(file);
	int ret;

	ret = vb2_ioctl_streamoff(file, priv, type);
	if (ret < 0)
		return ret;

	media_pipeline_stop(&fimc->ve.vdev.entity);
	fimc->streaming = false;
	return 0;
}

static int fimc_lite_reqbufs(struct file *file, void *priv,
			     struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_lite *fimc = video_drvdata(file);
	int ret;

	reqbufs->count = max_t(u32, FLITE_REQ_BUFS_MIN, reqbufs->count);
	ret = vb2_ioctl_reqbufs(file, priv, reqbufs);
	if (!ret)
		fimc->reqbufs_count = reqbufs->count;

	return ret;
}

/* Return 1 if rectangle a is enclosed in rectangle b, or 0 otherwise. */
static int enclosed_rectangle(struct v4l2_rect *a, struct v4l2_rect *b)
{
	if (a->left < b->left || a->top < b->top)
		return 0;
	if (a->left + a->width > b->left + b->width)
		return 0;
	if (a->top + a->height > b->top + b->height)
		return 0;

	return 1;
}

static int fimc_lite_g_selection(struct file *file, void *fh,
				 struct v4l2_selection *sel)
{
	struct fimc_lite *fimc = video_drvdata(file);
	struct flite_frame *f = &fimc->out_frame;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = f->f_width;
		sel->r.height = f->f_height;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		sel->r = f->rect;
		return 0;
	}

	return -EINVAL;
}

static int fimc_lite_s_selection(struct file *file, void *fh,
				 struct v4l2_selection *sel)
{
	struct fimc_lite *fimc = video_drvdata(file);
	struct flite_frame *f = &fimc->out_frame;
	struct v4l2_rect rect = sel->r;
	unsigned long flags;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	fimc_lite_try_compose(fimc, &rect);

	if ((sel->flags & V4L2_SEL_FLAG_LE) &&
	    !enclosed_rectangle(&rect, &sel->r))
		return -ERANGE;

	if ((sel->flags & V4L2_SEL_FLAG_GE) &&
	    !enclosed_rectangle(&sel->r, &rect))
		return -ERANGE;

	sel->r = rect;
	spin_lock_irqsave(&fimc->slock, flags);
	f->rect = rect;
	set_bit(ST_FLITE_CONFIG, &fimc->state);
	spin_unlock_irqrestore(&fimc->slock, flags);

	return 0;
}

static const struct v4l2_ioctl_ops fimc_lite_ioctl_ops = {
	.vidioc_querycap		= fimc_lite_querycap,
	.vidioc_enum_fmt_vid_cap	= fimc_lite_enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_lite_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_lite_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_lite_g_fmt_mplane,
	.vidioc_g_selection		= fimc_lite_g_selection,
	.vidioc_s_selection		= fimc_lite_s_selection,
	.vidioc_reqbufs			= fimc_lite_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= fimc_lite_streamon,
	.vidioc_streamoff		= fimc_lite_streamoff,
};

/* Capture subdev media entity operations */
static int fimc_lite_link_setup(struct media_entity *entity,
				const struct media_pad *local,
				const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (WARN_ON(fimc == NULL))
		return 0;

	v4l2_dbg(1, debug, sd, "%s: %s --> %s, flags: 0x%x. source_id: 0x%x\n",
		 __func__, remote->entity->name, local->entity->name,
		 flags, fimc->source_subdev_grp_id);

	switch (local->index) {
	case FLITE_SD_PAD_SINK:
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (fimc->source_subdev_grp_id == 0)
				fimc->source_subdev_grp_id = sd->grp_id;
			else
				ret = -EBUSY;
		} else {
			fimc->source_subdev_grp_id = 0;
			fimc->sensor = NULL;
		}
		break;

	case FLITE_SD_PAD_SOURCE_DMA:
		if (!(flags & MEDIA_LNK_FL_ENABLED))
			atomic_set(&fimc->out_path, FIMC_IO_NONE);
		else
			atomic_set(&fimc->out_path, FIMC_IO_DMA);
		break;

	case FLITE_SD_PAD_SOURCE_ISP:
		if (!(flags & MEDIA_LNK_FL_ENABLED))
			atomic_set(&fimc->out_path, FIMC_IO_NONE);
		else
			atomic_set(&fimc->out_path, FIMC_IO_ISP);
		break;

	default:
		v4l2_err(sd, "Invalid pad index\n");
		ret = -EINVAL;
	}
	mb();

	return ret;
}

static const struct media_entity_operations fimc_lite_subdev_media_ops = {
	.link_setup = fimc_lite_link_setup,
};

static int fimc_lite_subdev_enum_mbus_code(struct v4l2_subdev *sd,
					   struct v4l2_subdev_pad_config *cfg,
					   struct v4l2_subdev_mbus_code_enum *code)
{
	const struct fimc_fmt *fmt;

	fmt = fimc_lite_find_format(NULL, NULL, 0, code->index);
	if (!fmt)
		return -EINVAL;
	code->code = fmt->mbus_code;
	return 0;
}

static struct v4l2_mbus_framefmt *__fimc_lite_subdev_get_try_fmt(
		struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg, unsigned int pad)
{
	if (pad != FLITE_SD_PAD_SINK)
		pad = FLITE_SD_PAD_SOURCE_DMA;

	return v4l2_subdev_get_try_format(sd, cfg, pad);
}

static int fimc_lite_subdev_get_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *fmt)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct flite_frame *f = &fimc->inp_frame;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = __fimc_lite_subdev_get_try_fmt(sd, cfg, fmt->pad);
		fmt->format = *mf;
		return 0;
	}

	mutex_lock(&fimc->lock);
	mf->colorspace = f->fmt->colorspace;
	mf->code = f->fmt->mbus_code;

	if (fmt->pad == FLITE_SD_PAD_SINK) {
		/* full camera input frame size */
		mf->width = f->f_width;
		mf->height = f->f_height;
	} else {
		/* crop size */
		mf->width = f->rect.width;
		mf->height = f->rect.height;
	}
	mutex_unlock(&fimc->lock);
	return 0;
}

static int fimc_lite_subdev_set_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *fmt)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct flite_frame *sink = &fimc->inp_frame;
	struct flite_frame *source = &fimc->out_frame;
	const struct fimc_fmt *ffmt;

	v4l2_dbg(1, debug, sd, "pad%d: code: 0x%x, %dx%d\n",
		 fmt->pad, mf->code, mf->width, mf->height);

	mutex_lock(&fimc->lock);

	if ((atomic_read(&fimc->out_path) == FIMC_IO_ISP &&
	    sd->entity.stream_count > 0) ||
	    (atomic_read(&fimc->out_path) == FIMC_IO_DMA &&
	    vb2_is_busy(&fimc->vb_queue))) {
		mutex_unlock(&fimc->lock);
		return -EBUSY;
	}

	ffmt = fimc_lite_subdev_try_fmt(fimc, cfg, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *src_fmt;

		mf = __fimc_lite_subdev_get_try_fmt(sd, cfg, fmt->pad);
		*mf = fmt->format;

		if (fmt->pad == FLITE_SD_PAD_SINK) {
			unsigned int pad = FLITE_SD_PAD_SOURCE_DMA;
			src_fmt = __fimc_lite_subdev_get_try_fmt(sd, cfg, pad);
			*src_fmt = *mf;
		}

		mutex_unlock(&fimc->lock);
		return 0;
	}

	if (fmt->pad == FLITE_SD_PAD_SINK) {
		sink->f_width = mf->width;
		sink->f_height = mf->height;
		sink->fmt = ffmt;
		/* Set sink crop rectangle */
		sink->rect.width = mf->width;
		sink->rect.height = mf->height;
		sink->rect.left = 0;
		sink->rect.top = 0;
		/* Reset source format and crop rectangle */
		source->rect = sink->rect;
		source->f_width = mf->width;
		source->f_height = mf->height;
	}

	mutex_unlock(&fimc->lock);
	return 0;
}

static int fimc_lite_subdev_get_selection(struct v4l2_subdev *sd,
					  struct v4l2_subdev_pad_config *cfg,
					  struct v4l2_subdev_selection *sel)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	struct flite_frame *f = &fimc->inp_frame;

	if ((sel->target != V4L2_SEL_TGT_CROP &&
	     sel->target != V4L2_SEL_TGT_CROP_BOUNDS) ||
	     sel->pad != FLITE_SD_PAD_SINK)
		return -EINVAL;

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		sel->r = *v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
		return 0;
	}

	mutex_lock(&fimc->lock);
	if (sel->target == V4L2_SEL_TGT_CROP) {
		sel->r = f->rect;
	} else {
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = f->f_width;
		sel->r.height = f->f_height;
	}
	mutex_unlock(&fimc->lock);

	v4l2_dbg(1, debug, sd, "%s: (%d,%d) %dx%d, f_w: %d, f_h: %d\n",
		 __func__, f->rect.left, f->rect.top, f->rect.width,
		 f->rect.height, f->f_width, f->f_height);

	return 0;
}

static int fimc_lite_subdev_set_selection(struct v4l2_subdev *sd,
					  struct v4l2_subdev_pad_config *cfg,
					  struct v4l2_subdev_selection *sel)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	struct flite_frame *f = &fimc->inp_frame;
	int ret = 0;

	if (sel->target != V4L2_SEL_TGT_CROP || sel->pad != FLITE_SD_PAD_SINK)
		return -EINVAL;

	mutex_lock(&fimc->lock);
	fimc_lite_try_crop(fimc, &sel->r);

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_crop(sd, cfg, sel->pad) = sel->r;
	} else {
		unsigned long flags;
		spin_lock_irqsave(&fimc->slock, flags);
		f->rect = sel->r;
		/* Same crop rectangle on the source pad */
		fimc->out_frame.rect = sel->r;
		set_bit(ST_FLITE_CONFIG, &fimc->state);
		spin_unlock_irqrestore(&fimc->slock, flags);
	}
	mutex_unlock(&fimc->lock);

	v4l2_dbg(1, debug, sd, "%s: (%d,%d) %dx%d, f_w: %d, f_h: %d\n",
		 __func__, f->rect.left, f->rect.top, f->rect.width,
		 f->rect.height, f->f_width, f->f_height);

	return ret;
}

static int fimc_lite_subdev_s_stream(struct v4l2_subdev *sd, int on)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	unsigned long flags;
	int ret;

	/*
	 * Find sensor subdev linked to FIMC-LITE directly or through
	 * MIPI-CSIS. This is required for configuration where FIMC-LITE
	 * is used as a subdev only and feeds data internally to FIMC-IS.
	 * The pipeline links are protected through entity.stream_count
	 * so there is no need to take the media graph mutex here.
	 */
	fimc->sensor = fimc_find_remote_sensor(&sd->entity);

	if (atomic_read(&fimc->out_path) != FIMC_IO_ISP)
		return -ENOIOCTLCMD;

	mutex_lock(&fimc->lock);
	if (on) {
		flite_hw_reset(fimc);
		ret = fimc_lite_hw_init(fimc, true);
		if (!ret) {
			spin_lock_irqsave(&fimc->slock, flags);
			flite_hw_capture_start(fimc);
			spin_unlock_irqrestore(&fimc->slock, flags);
		}
	} else {
		set_bit(ST_FLITE_OFF, &fimc->state);

		spin_lock_irqsave(&fimc->slock, flags);
		flite_hw_capture_stop(fimc);
		spin_unlock_irqrestore(&fimc->slock, flags);

		ret = wait_event_timeout(fimc->irq_queue,
				!test_bit(ST_FLITE_OFF, &fimc->state),
				msecs_to_jiffies(200));
		if (ret == 0)
			v4l2_err(sd, "s_stream(0) timeout\n");
		clear_bit(ST_FLITE_RUN, &fimc->state);
	}

	mutex_unlock(&fimc->lock);
	return ret;
}

static int fimc_lite_log_status(struct v4l2_subdev *sd)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);

	flite_hw_dump_regs(fimc, __func__);
	return 0;
}

static int fimc_lite_subdev_registered(struct v4l2_subdev *sd)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);
	struct vb2_queue *q = &fimc->vb_queue;
	struct video_device *vfd = &fimc->ve.vdev;
	int ret;

	memset(vfd, 0, sizeof(*vfd));
	atomic_set(&fimc->out_path, FIMC_IO_DMA);

	snprintf(vfd->name, sizeof(vfd->name), "fimc-lite.%d.capture",
		 fimc->index);

	vfd->fops = &fimc_lite_fops;
	vfd->ioctl_ops = &fimc_lite_ioctl_ops;
	vfd->v4l2_dev = sd->v4l2_dev;
	vfd->minor = -1;
	vfd->release = video_device_release_empty;
	vfd->queue = q;
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING;
	fimc->reqbufs_count = 0;

	INIT_LIST_HEAD(&fimc->pending_buf_q);
	INIT_LIST_HEAD(&fimc->active_buf_q);

	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->ops = &fimc_lite_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct flite_buffer);
	q->drv_priv = fimc;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &fimc->lock;
	q->dev = &fimc->pdev->dev;

	ret = vb2_queue_init(q);
	if (ret < 0)
		return ret;

	fimc->vd_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vfd->entity, 1, &fimc->vd_pad);
	if (ret < 0)
		return ret;

	video_set_drvdata(vfd, fimc);
	fimc->ve.pipe = v4l2_get_subdev_hostdata(sd);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		media_entity_cleanup(&vfd->entity);
		fimc->ve.pipe = NULL;
		return ret;
	}

	v4l2_info(sd->v4l2_dev, "Registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));
	return 0;
}

static void fimc_lite_subdev_unregistered(struct v4l2_subdev *sd)
{
	struct fimc_lite *fimc = v4l2_get_subdevdata(sd);

	if (fimc == NULL)
		return;

	mutex_lock(&fimc->lock);

	if (video_is_registered(&fimc->ve.vdev)) {
		video_unregister_device(&fimc->ve.vdev);
		media_entity_cleanup(&fimc->ve.vdev.entity);
		fimc->ve.pipe = NULL;
	}

	mutex_unlock(&fimc->lock);
}

static const struct v4l2_subdev_internal_ops fimc_lite_subdev_internal_ops = {
	.registered = fimc_lite_subdev_registered,
	.unregistered = fimc_lite_subdev_unregistered,
};

static const struct v4l2_subdev_pad_ops fimc_lite_subdev_pad_ops = {
	.enum_mbus_code = fimc_lite_subdev_enum_mbus_code,
	.get_selection = fimc_lite_subdev_get_selection,
	.set_selection = fimc_lite_subdev_set_selection,
	.get_fmt = fimc_lite_subdev_get_fmt,
	.set_fmt = fimc_lite_subdev_set_fmt,
};

static const struct v4l2_subdev_video_ops fimc_lite_subdev_video_ops = {
	.s_stream = fimc_lite_subdev_s_stream,
};

static const struct v4l2_subdev_core_ops fimc_lite_core_ops = {
	.log_status = fimc_lite_log_status,
};

static const struct v4l2_subdev_ops fimc_lite_subdev_ops = {
	.core = &fimc_lite_core_ops,
	.video = &fimc_lite_subdev_video_ops,
	.pad = &fimc_lite_subdev_pad_ops,
};

static int fimc_lite_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fimc_lite *fimc = container_of(ctrl->handler, struct fimc_lite,
					      ctrl_handler);
	set_bit(ST_FLITE_CONFIG, &fimc->state);
	return 0;
}

static const struct v4l2_ctrl_ops fimc_lite_ctrl_ops = {
	.s_ctrl	= fimc_lite_s_ctrl,
};

static const struct v4l2_ctrl_config fimc_lite_ctrl = {
	.ops	= &fimc_lite_ctrl_ops,
	.id	= V4L2_CTRL_CLASS_USER | 0x1001,
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.name	= "Test Pattern 640x480",
	.step	= 1,
};

static void fimc_lite_set_default_config(struct fimc_lite *fimc)
{
	struct flite_frame *sink = &fimc->inp_frame;
	struct flite_frame *source = &fimc->out_frame;

	sink->fmt = &fimc_lite_formats[0];
	sink->f_width = FLITE_DEFAULT_WIDTH;
	sink->f_height = FLITE_DEFAULT_HEIGHT;

	sink->rect.width = FLITE_DEFAULT_WIDTH;
	sink->rect.height = FLITE_DEFAULT_HEIGHT;
	sink->rect.left = 0;
	sink->rect.top = 0;

	*source = *sink;
}

static int fimc_lite_create_capture_subdev(struct fimc_lite *fimc)
{
	struct v4l2_ctrl_handler *handler = &fimc->ctrl_handler;
	struct v4l2_subdev *sd = &fimc->subdev;
	int ret;

	v4l2_subdev_init(sd, &fimc_lite_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "FIMC-LITE.%d", fimc->index);

	fimc->subdev_pads[FLITE_SD_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	fimc->subdev_pads[FLITE_SD_PAD_SOURCE_DMA].flags = MEDIA_PAD_FL_SOURCE;
	fimc->subdev_pads[FLITE_SD_PAD_SOURCE_ISP].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, FLITE_SD_PADS_NUM,
				fimc->subdev_pads);
	if (ret)
		return ret;

	v4l2_ctrl_handler_init(handler, 1);
	fimc->test_pattern = v4l2_ctrl_new_custom(handler, &fimc_lite_ctrl,
						  NULL);
	if (handler->error) {
		media_entity_cleanup(&sd->entity);
		return handler->error;
	}

	sd->ctrl_handler = handler;
	sd->internal_ops = &fimc_lite_subdev_internal_ops;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	sd->entity.ops = &fimc_lite_subdev_media_ops;
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, fimc);

	return 0;
}

static void fimc_lite_unregister_capture_subdev(struct fimc_lite *fimc)
{
	struct v4l2_subdev *sd = &fimc->subdev;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&fimc->ctrl_handler);
	v4l2_set_subdevdata(sd, NULL);
}

static void fimc_lite_clk_put(struct fimc_lite *fimc)
{
	if (IS_ERR(fimc->clock))
		return;

	clk_put(fimc->clock);
	fimc->clock = ERR_PTR(-EINVAL);
}

static int fimc_lite_clk_get(struct fimc_lite *fimc)
{
	fimc->clock = clk_get(&fimc->pdev->dev, FLITE_CLK_NAME);
	return PTR_ERR_OR_ZERO(fimc->clock);
}

static const struct of_device_id flite_of_match[];

static int fimc_lite_probe(struct platform_device *pdev)
{
	struct flite_drvdata *drv_data = NULL;
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	struct fimc_lite *fimc;
	struct resource *res;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	fimc = devm_kzalloc(dev, sizeof(*fimc), GFP_KERNEL);
	if (!fimc)
		return -ENOMEM;

	of_id = of_match_node(flite_of_match, dev->of_node);
	if (of_id)
		drv_data = (struct flite_drvdata *)of_id->data;
	fimc->index = of_alias_get_id(dev->of_node, "fimc-lite");

	if (!drv_data || fimc->index >= drv_data->num_instances ||
						fimc->index < 0) {
		dev_err(dev, "Wrong %pOF node alias\n", dev->of_node);
		return -EINVAL;
	}

	fimc->dd = drv_data;
	fimc->pdev = pdev;

	init_waitqueue_head(&fimc->irq_queue);
	spin_lock_init(&fimc->slock);
	mutex_init(&fimc->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fimc->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(fimc->regs))
		return PTR_ERR(fimc->regs);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(dev, "Failed to get IRQ resource\n");
		return -ENXIO;
	}

	ret = fimc_lite_clk_get(fimc);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, res->start, flite_irq_handler,
			       0, dev_name(dev), fimc);
	if (ret) {
		dev_err(dev, "Failed to install irq (%d)\n", ret);
		goto err_clk_put;
	}

	/* The video node will be created within the subdev's registered() op */
	ret = fimc_lite_create_capture_subdev(fimc);
	if (ret)
		goto err_clk_put;

	platform_set_drvdata(pdev, fimc);
	pm_runtime_enable(dev);

	if (!pm_runtime_enabled(dev)) {
		ret = clk_prepare_enable(fimc->clock);
		if (ret < 0)
			goto err_sd;
	}

	vb2_dma_contig_set_max_seg_size(dev, DMA_BIT_MASK(32));

	fimc_lite_set_default_config(fimc);

	dev_dbg(dev, "FIMC-LITE.%d registered successfully\n",
		fimc->index);
	return 0;

err_sd:
	fimc_lite_unregister_capture_subdev(fimc);
err_clk_put:
	fimc_lite_clk_put(fimc);
	return ret;
}

#ifdef CONFIG_PM
static int fimc_lite_runtime_resume(struct device *dev)
{
	struct fimc_lite *fimc = dev_get_drvdata(dev);

	clk_prepare_enable(fimc->clock);
	return 0;
}

static int fimc_lite_runtime_suspend(struct device *dev)
{
	struct fimc_lite *fimc = dev_get_drvdata(dev);

	clk_disable_unprepare(fimc->clock);
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int fimc_lite_resume(struct device *dev)
{
	struct fimc_lite *fimc = dev_get_drvdata(dev);
	struct flite_buffer *buf;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&fimc->slock, flags);
	if (!test_and_clear_bit(ST_LPM, &fimc->state) ||
	    !test_bit(ST_FLITE_IN_USE, &fimc->state)) {
		spin_unlock_irqrestore(&fimc->slock, flags);
		return 0;
	}
	flite_hw_reset(fimc);
	spin_unlock_irqrestore(&fimc->slock, flags);

	if (!test_and_clear_bit(ST_FLITE_SUSPENDED, &fimc->state))
		return 0;

	INIT_LIST_HEAD(&fimc->active_buf_q);
	fimc_pipeline_call(&fimc->ve, open,
			   &fimc->ve.vdev.entity, false);
	fimc_lite_hw_init(fimc, atomic_read(&fimc->out_path) == FIMC_IO_ISP);
	clear_bit(ST_FLITE_SUSPENDED, &fimc->state);

	for (i = 0; i < fimc->reqbufs_count; i++) {
		if (list_empty(&fimc->pending_buf_q))
			break;
		buf = fimc_lite_pending_queue_pop(fimc);
		buffer_queue(&buf->vb.vb2_buf);
	}
	return 0;
}

static int fimc_lite_suspend(struct device *dev)
{
	struct fimc_lite *fimc = dev_get_drvdata(dev);
	bool suspend = test_bit(ST_FLITE_IN_USE, &fimc->state);
	int ret;

	if (test_and_set_bit(ST_LPM, &fimc->state))
		return 0;

	ret = fimc_lite_stop_capture(fimc, suspend);
	if (ret < 0 || !fimc_lite_active(fimc))
		return ret;

	return fimc_pipeline_call(&fimc->ve, close);
}
#endif /* CONFIG_PM_SLEEP */

static int fimc_lite_remove(struct platform_device *pdev)
{
	struct fimc_lite *fimc = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	fimc_lite_unregister_capture_subdev(fimc);
	vb2_dma_contig_clear_max_seg_size(dev);
	fimc_lite_clk_put(fimc);

	dev_info(dev, "Driver unloaded\n");
	return 0;
}

static const struct dev_pm_ops fimc_lite_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimc_lite_suspend, fimc_lite_resume)
	SET_RUNTIME_PM_OPS(fimc_lite_runtime_suspend, fimc_lite_runtime_resume,
			   NULL)
};

/* EXYNOS4412 */
static struct flite_drvdata fimc_lite_drvdata_exynos4 = {
	.max_width		= 8192,
	.max_height		= 8192,
	.out_width_align	= 8,
	.win_hor_offs_align	= 2,
	.out_hor_offs_align	= 8,
	.max_dma_bufs		= 1,
	.num_instances		= 2,
};

/* EXYNOS5250 */
static struct flite_drvdata fimc_lite_drvdata_exynos5 = {
	.max_width		= 8192,
	.max_height		= 8192,
	.out_width_align	= 8,
	.win_hor_offs_align	= 2,
	.out_hor_offs_align	= 8,
	.max_dma_bufs		= 32,
	.num_instances		= 3,
};

static const struct of_device_id flite_of_match[] = {
	{
		.compatible = "samsung,exynos4212-fimc-lite",
		.data = &fimc_lite_drvdata_exynos4,
	},
	{
		.compatible = "samsung,exynos5250-fimc-lite",
		.data = &fimc_lite_drvdata_exynos5,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, flite_of_match);

static struct platform_driver fimc_lite_driver = {
	.probe		= fimc_lite_probe,
	.remove		= fimc_lite_remove,
	.driver = {
		.of_match_table = flite_of_match,
		.name		= FIMC_LITE_DRV_NAME,
		.pm		= &fimc_lite_pm_ops,
	}
};
module_platform_driver(fimc_lite_driver);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" FIMC_LITE_DRV_NAME);
