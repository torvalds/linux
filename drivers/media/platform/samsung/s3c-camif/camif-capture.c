// SPDX-License-Identifier: GPL-2.0-only
/*
 * s3c24xx/s3c64xx SoC series Camera Interface (CAMIF) driver
 *
 * Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
 * Copyright (C) 2012 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Based on drivers/media/platform/s5p-fimc,
 * Copyright (C) 2010 - 2012 Samsung Electronics Co., Ltd.
*/
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "camif-core.h"
#include "camif-regs.h"

static int debug;
module_param(debug, int, 0644);

/* Locking: called with vp->camif->slock spinlock held */
static void camif_cfg_video_path(struct camif_vp *vp)
{
	WARN_ON(s3c_camif_get_scaler_config(vp, &vp->scaler));
	camif_hw_set_scaler(vp);
	camif_hw_set_flip(vp);
	camif_hw_set_target_format(vp);
	camif_hw_set_output_dma(vp);
}

static void camif_prepare_dma_offset(struct camif_vp *vp)
{
	struct camif_frame *f = &vp->out_frame;

	f->dma_offset.initial = f->rect.top * f->f_width + f->rect.left;
	f->dma_offset.line = f->f_width - (f->rect.left + f->rect.width);

	pr_debug("dma_offset: initial: %d, line: %d\n",
		 f->dma_offset.initial, f->dma_offset.line);
}

/* Locking: called with camif->slock spinlock held */
static int s3c_camif_hw_init(struct camif_dev *camif, struct camif_vp *vp)
{
	const struct s3c_camif_variant *variant = camif->variant;

	if (camif->sensor.sd == NULL || vp->out_fmt == NULL)
		return -EINVAL;

	if (variant->ip_revision == S3C244X_CAMIF_IP_REV)
		camif_hw_clear_fifo_overflow(vp);
	camif_hw_set_camera_bus(camif);
	camif_hw_set_source_format(camif);
	camif_hw_set_camera_crop(camif);
	camif_hw_set_test_pattern(camif, camif->test_pattern);
	if (variant->has_img_effect)
		camif_hw_set_effect(camif, camif->colorfx,
				camif->colorfx_cr, camif->colorfx_cb);
	if (variant->ip_revision == S3C6410_CAMIF_IP_REV)
		camif_hw_set_input_path(vp);
	camif_cfg_video_path(vp);
	vp->state &= ~ST_VP_CONFIG;

	return 0;
}

/*
 * Initialize the video path, only up from the scaler stage. The camera
 * input interface set up is skipped. This is useful to enable one of the
 * video paths when the other is already running.
 * Locking: called with camif->slock spinlock held.
 */
static int s3c_camif_hw_vp_init(struct camif_dev *camif, struct camif_vp *vp)
{
	unsigned int ip_rev = camif->variant->ip_revision;

	if (vp->out_fmt == NULL)
		return -EINVAL;

	camif_prepare_dma_offset(vp);
	if (ip_rev == S3C244X_CAMIF_IP_REV)
		camif_hw_clear_fifo_overflow(vp);
	camif_cfg_video_path(vp);
	vp->state &= ~ST_VP_CONFIG;
	return 0;
}

static int sensor_set_power(struct camif_dev *camif, int on)
{
	struct cam_sensor *sensor = &camif->sensor;
	int err = 0;

	if (camif->sensor.power_count == !on)
		err = v4l2_subdev_call(sensor->sd, core, s_power, on);
	if (err == -ENOIOCTLCMD)
		err = 0;
	if (!err)
		sensor->power_count += on ? 1 : -1;

	pr_debug("on: %d, power_count: %d, err: %d\n",
		 on, sensor->power_count, err);

	return err;
}

static int sensor_set_streaming(struct camif_dev *camif, int on)
{
	struct cam_sensor *sensor = &camif->sensor;
	int err = 0;

	if (camif->sensor.stream_count == !on)
		err = v4l2_subdev_call(sensor->sd, video, s_stream, on);
	if (!err)
		sensor->stream_count += on ? 1 : -1;

	pr_debug("on: %d, stream_count: %d, err: %d\n",
		 on, sensor->stream_count, err);

	return err;
}

/*
 * Reinitialize the driver so it is ready to start streaming again.
 * Return any buffers to vb2, perform CAMIF software reset and
 * turn off streaming at the data pipeline (sensor) if required.
 */
static int camif_reinitialize(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	struct camif_buffer *buf;
	unsigned long flags;
	bool streaming;

	spin_lock_irqsave(&camif->slock, flags);
	streaming = vp->state & ST_VP_SENSOR_STREAMING;

	vp->state &= ~(ST_VP_PENDING | ST_VP_RUNNING | ST_VP_OFF |
		       ST_VP_ABORTING | ST_VP_STREAMING |
		       ST_VP_SENSOR_STREAMING | ST_VP_LASTIRQ);

	/* Release unused buffers */
	while (!list_empty(&vp->pending_buf_q)) {
		buf = camif_pending_queue_pop(vp);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	while (!list_empty(&vp->active_buf_q)) {
		buf = camif_active_queue_pop(vp);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&camif->slock, flags);

	if (!streaming)
		return 0;

	return sensor_set_streaming(camif, 0);
}

static bool s3c_vp_active(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&camif->slock, flags);
	ret = (vp->state & ST_VP_RUNNING) || (vp->state & ST_VP_PENDING);
	spin_unlock_irqrestore(&camif->slock, flags);

	return ret;
}

static bool camif_is_streaming(struct camif_dev *camif)
{
	unsigned long flags;
	bool status;

	spin_lock_irqsave(&camif->slock, flags);
	status = camif->stream_count > 0;
	spin_unlock_irqrestore(&camif->slock, flags);

	return status;
}

static int camif_stop_capture(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	unsigned long flags;
	int ret;

	if (!s3c_vp_active(vp))
		return 0;

	spin_lock_irqsave(&camif->slock, flags);
	vp->state &= ~(ST_VP_OFF | ST_VP_LASTIRQ);
	vp->state |= ST_VP_ABORTING;
	spin_unlock_irqrestore(&camif->slock, flags);

	ret = wait_event_timeout(vp->irq_queue,
			   !(vp->state & ST_VP_ABORTING),
			   msecs_to_jiffies(CAMIF_STOP_TIMEOUT));

	spin_lock_irqsave(&camif->slock, flags);

	if (ret == 0 && !(vp->state & ST_VP_OFF)) {
		/* Timed out, forcibly stop capture */
		vp->state &= ~(ST_VP_OFF | ST_VP_ABORTING |
			       ST_VP_LASTIRQ);

		camif_hw_disable_capture(vp);
		camif_hw_enable_scaler(vp, false);
	}

	spin_unlock_irqrestore(&camif->slock, flags);

	return camif_reinitialize(vp);
}

static int camif_prepare_addr(struct camif_vp *vp, struct vb2_buffer *vb,
			      struct camif_addr *paddr)
{
	struct camif_frame *frame = &vp->out_frame;
	u32 pix_size;

	if (vb == NULL || frame == NULL)
		return -EINVAL;

	pix_size = frame->rect.width * frame->rect.height;

	pr_debug("colplanes: %d, pix_size: %u\n",
		 vp->out_fmt->colplanes, pix_size);

	paddr->y = vb2_dma_contig_plane_dma_addr(vb, 0);

	switch (vp->out_fmt->colplanes) {
	case 1:
		paddr->cb = 0;
		paddr->cr = 0;
		break;
	case 2:
		/* decompose Y into Y/Cb */
		paddr->cb = (u32)(paddr->y + pix_size);
		paddr->cr = 0;
		break;
	case 3:
		paddr->cb = (u32)(paddr->y + pix_size);
		/* decompose Y into Y/Cb/Cr */
		if (vp->out_fmt->color == IMG_FMT_YCBCR422P)
			paddr->cr = (u32)(paddr->cb + (pix_size >> 1));
		else /* 420 */
			paddr->cr = (u32)(paddr->cb + (pix_size >> 2));

		if (vp->out_fmt->color == IMG_FMT_YCRCB420)
			swap(paddr->cb, paddr->cr);
		break;
	default:
		return -EINVAL;
	}

	pr_debug("DMA address: y: %pad  cb: %pad cr: %pad\n",
		 &paddr->y, &paddr->cb, &paddr->cr);

	return 0;
}

irqreturn_t s3c_camif_irq_handler(int irq, void *priv)
{
	struct camif_vp *vp = priv;
	struct camif_dev *camif = vp->camif;
	unsigned int ip_rev = camif->variant->ip_revision;
	unsigned int status;

	spin_lock(&camif->slock);

	if (ip_rev == S3C6410_CAMIF_IP_REV)
		camif_hw_clear_pending_irq(vp);

	status = camif_hw_get_status(vp);

	if (ip_rev == S3C244X_CAMIF_IP_REV && (status & CISTATUS_OVF_MASK)) {
		camif_hw_clear_fifo_overflow(vp);
		goto unlock;
	}

	if (vp->state & ST_VP_ABORTING) {
		if (vp->state & ST_VP_OFF) {
			/* Last IRQ */
			vp->state &= ~(ST_VP_OFF | ST_VP_ABORTING |
				       ST_VP_LASTIRQ);
			wake_up(&vp->irq_queue);
			goto unlock;
		} else if (vp->state & ST_VP_LASTIRQ) {
			camif_hw_disable_capture(vp);
			camif_hw_enable_scaler(vp, false);
			camif_hw_set_lastirq(vp, false);
			vp->state |= ST_VP_OFF;
		} else {
			/* Disable capture, enable last IRQ */
			camif_hw_set_lastirq(vp, true);
			vp->state |= ST_VP_LASTIRQ;
		}
	}

	if (!list_empty(&vp->pending_buf_q) && (vp->state & ST_VP_RUNNING) &&
	    !list_empty(&vp->active_buf_q)) {
		unsigned int index;
		struct camif_buffer *vbuf;
		/*
		 * Get previous DMA write buffer index:
		 * 0 => DMA buffer 0, 2;
		 * 1 => DMA buffer 1, 3.
		 */
		index = (CISTATUS_FRAMECNT(status) + 2) & 1;
		vbuf = camif_active_queue_peek(vp, index);

		if (!WARN_ON(vbuf == NULL)) {
			/* Dequeue a filled buffer */
			vbuf->vb.vb2_buf.timestamp = ktime_get_ns();
			vbuf->vb.sequence = vp->frame_sequence++;
			vb2_buffer_done(&vbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);

			/* Set up an empty buffer at the DMA engine */
			vbuf = camif_pending_queue_pop(vp);
			vbuf->index = index;
			camif_hw_set_output_addr(vp, &vbuf->paddr, index);
			camif_hw_set_output_addr(vp, &vbuf->paddr, index + 2);

			/* Scheduled in H/W, add to the queue */
			camif_active_queue_add(vp, vbuf);
		}
	} else if (!(vp->state & ST_VP_ABORTING) &&
		   (vp->state & ST_VP_PENDING))  {
		vp->state |= ST_VP_RUNNING;
	}

	if (vp->state & ST_VP_CONFIG) {
		camif_prepare_dma_offset(vp);
		camif_hw_set_camera_crop(camif);
		camif_hw_set_scaler(vp);
		camif_hw_set_flip(vp);
		camif_hw_set_test_pattern(camif, camif->test_pattern);
		if (camif->variant->has_img_effect)
			camif_hw_set_effect(camif, camif->colorfx,
				    camif->colorfx_cr, camif->colorfx_cb);
		vp->state &= ~ST_VP_CONFIG;
	}
unlock:
	spin_unlock(&camif->slock);
	return IRQ_HANDLED;
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct camif_vp *vp = vb2_get_drv_priv(vq);
	struct camif_dev *camif = vp->camif;
	unsigned long flags;
	int ret;

	/*
	 * We assume the codec capture path is always activated
	 * first, before the preview path starts streaming.
	 * This is required to avoid internal FIFO overflow and
	 * a need for CAMIF software reset.
	 */
	spin_lock_irqsave(&camif->slock, flags);

	if (camif->stream_count == 0) {
		camif_hw_reset(camif);
		ret = s3c_camif_hw_init(camif, vp);
	} else {
		ret = s3c_camif_hw_vp_init(camif, vp);
	}
	spin_unlock_irqrestore(&camif->slock, flags);

	if (ret < 0) {
		camif_reinitialize(vp);
		return ret;
	}

	spin_lock_irqsave(&camif->slock, flags);
	vp->frame_sequence = 0;
	vp->state |= ST_VP_PENDING;

	if (!list_empty(&vp->pending_buf_q) &&
	    (!(vp->state & ST_VP_STREAMING) ||
	     !(vp->state & ST_VP_SENSOR_STREAMING))) {

		camif_hw_enable_scaler(vp, vp->scaler.enable);
		camif_hw_enable_capture(vp);
		vp->state |= ST_VP_STREAMING;

		if (!(vp->state & ST_VP_SENSOR_STREAMING)) {
			vp->state |= ST_VP_SENSOR_STREAMING;
			spin_unlock_irqrestore(&camif->slock, flags);
			ret = sensor_set_streaming(camif, 1);
			if (ret)
				v4l2_err(&vp->vdev, "Sensor s_stream failed\n");
			if (debug)
				camif_hw_dump_regs(camif, __func__);

			return ret;
		}
	}

	spin_unlock_irqrestore(&camif->slock, flags);
	return 0;
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct camif_vp *vp = vb2_get_drv_priv(vq);
	camif_stop_capture(vp);
}

static int queue_setup(struct vb2_queue *vq,
		       unsigned int *num_buffers, unsigned int *num_planes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct camif_vp *vp = vb2_get_drv_priv(vq);
	struct camif_frame *frame = &vp->out_frame;
	const struct camif_fmt *fmt = vp->out_fmt;
	unsigned int size;

	if (fmt == NULL)
		return -EINVAL;

	size = (frame->f_width * frame->f_height * fmt->depth) / 8;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;

	*num_planes = 1;
	sizes[0] = size;

	pr_debug("size: %u\n", sizes[0]);
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct camif_vp *vp = vb2_get_drv_priv(vb->vb2_queue);

	if (vp->out_fmt == NULL)
		return -EINVAL;

	if (vb2_plane_size(vb, 0) < vp->payload) {
		v4l2_err(&vp->vdev, "buffer too small: %lu, required: %u\n",
			 vb2_plane_size(vb, 0), vp->payload);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, vp->payload);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct camif_buffer *buf = container_of(vbuf, struct camif_buffer, vb);
	struct camif_vp *vp = vb2_get_drv_priv(vb->vb2_queue);
	struct camif_dev *camif = vp->camif;
	unsigned long flags;

	spin_lock_irqsave(&camif->slock, flags);
	WARN_ON(camif_prepare_addr(vp, &buf->vb.vb2_buf, &buf->paddr));

	if (!(vp->state & ST_VP_STREAMING) && vp->active_buffers < 2) {
		/* Schedule an empty buffer in H/W */
		buf->index = vp->buf_index;

		camif_hw_set_output_addr(vp, &buf->paddr, buf->index);
		camif_hw_set_output_addr(vp, &buf->paddr, buf->index + 2);

		camif_active_queue_add(vp, buf);
		vp->buf_index = !vp->buf_index;
	} else {
		camif_pending_queue_add(vp, buf);
	}

	if (vb2_is_streaming(&vp->vb_queue) && !list_empty(&vp->pending_buf_q)
		&& !(vp->state & ST_VP_STREAMING)) {

		vp->state |= ST_VP_STREAMING;
		camif_hw_enable_scaler(vp, vp->scaler.enable);
		camif_hw_enable_capture(vp);
		spin_unlock_irqrestore(&camif->slock, flags);

		if (!(vp->state & ST_VP_SENSOR_STREAMING)) {
			if (sensor_set_streaming(camif, 1) == 0)
				vp->state |= ST_VP_SENSOR_STREAMING;
			else
				v4l2_err(&vp->vdev, "Sensor s_stream failed\n");

			if (debug)
				camif_hw_dump_regs(camif, __func__);
		}
		return;
	}
	spin_unlock_irqrestore(&camif->slock, flags);
}

static const struct vb2_ops s3c_camif_qops = {
	.queue_setup	 = queue_setup,
	.buf_prepare	 = buffer_prepare,
	.buf_queue	 = buffer_queue,
	.start_streaming = start_streaming,
	.stop_streaming	 = stop_streaming,
};

static int s3c_camif_open(struct file *file)
{
	struct camif_vp *vp = video_drvdata(file);
	struct camif_dev *camif = vp->camif;
	int ret;

	pr_debug("[vp%d] state: %#x,  owner: %p, pid: %d\n", vp->id,
		 vp->state, vp->owner, task_pid_nr(current));

	if (mutex_lock_interruptible(&camif->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	ret = pm_runtime_resume_and_get(camif->dev);
	if (ret < 0)
		goto err_pm;

	ret = sensor_set_power(camif, 1);
	if (!ret)
		goto unlock;

	pm_runtime_put(camif->dev);
err_pm:
	v4l2_fh_release(file);
unlock:
	mutex_unlock(&camif->lock);
	return ret;
}

static int s3c_camif_close(struct file *file)
{
	struct camif_vp *vp = video_drvdata(file);
	struct camif_dev *camif = vp->camif;
	int ret;

	pr_debug("[vp%d] state: %#x, owner: %p, pid: %d\n", vp->id,
		 vp->state, vp->owner, task_pid_nr(current));

	mutex_lock(&camif->lock);

	if (vp->owner == file_to_v4l2_fh(file)) {
		camif_stop_capture(vp);
		vb2_queue_release(&vp->vb_queue);
		vp->owner = NULL;
	}

	sensor_set_power(camif, 0);

	pm_runtime_put(camif->dev);
	ret = v4l2_fh_release(file);

	mutex_unlock(&camif->lock);
	return ret;
}

static __poll_t s3c_camif_poll(struct file *file,
				   struct poll_table_struct *wait)
{
	struct camif_vp *vp = video_drvdata(file);
	struct camif_dev *camif = vp->camif;
	__poll_t ret;

	mutex_lock(&camif->lock);
	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		ret = EPOLLERR;
	else
		ret = vb2_poll(&vp->vb_queue, file, wait);

	mutex_unlock(&camif->lock);
	return ret;
}

static int s3c_camif_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct camif_vp *vp = video_drvdata(file);
	int ret;

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		ret = -EBUSY;
	else
		ret = vb2_mmap(&vp->vb_queue, vma);

	return ret;
}

static const struct v4l2_file_operations s3c_camif_fops = {
	.owner		= THIS_MODULE,
	.open		= s3c_camif_open,
	.release	= s3c_camif_close,
	.poll		= s3c_camif_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= s3c_camif_mmap,
};

/*
 * Video node IOCTLs
 */

static int s3c_camif_vidioc_querycap(struct file *file, void *priv,
				     struct v4l2_capability *cap)
{
	struct camif_vp *vp = video_drvdata(file);

	strscpy(cap->driver, S3C_CAMIF_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, S3C_CAMIF_DRIVER_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s.%d",
		 dev_name(vp->camif->dev), vp->id);
	return 0;
}

static int s3c_camif_vidioc_enum_input(struct file *file, void *priv,
				       struct v4l2_input *input)
{
	struct camif_vp *vp = video_drvdata(file);
	struct v4l2_subdev *sensor = vp->camif->sensor.sd;

	if (input->index || sensor == NULL)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, sensor->name, sizeof(input->name));
	return 0;
}

static int s3c_camif_vidioc_s_input(struct file *file, void *priv,
				    unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int s3c_camif_vidioc_g_input(struct file *file, void *priv,
				    unsigned int *i)
{
	*i = 0;
	return 0;
}

static int s3c_camif_vidioc_enum_fmt(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	struct camif_vp *vp = video_drvdata(file);
	const struct camif_fmt *fmt;

	fmt = s3c_camif_find_format(vp, NULL, f->index);
	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	return 0;
}

static int s3c_camif_vidioc_g_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct camif_vp *vp = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct camif_frame *frame = &vp->out_frame;
	const struct camif_fmt *fmt = vp->out_fmt;

	pix->bytesperline = frame->f_width * fmt->ybpp;
	pix->sizeimage = vp->payload;

	pix->pixelformat = fmt->fourcc;
	pix->width = frame->f_width;
	pix->height = frame->f_height;
	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_JPEG;

	return 0;
}

static int __camif_video_try_format(struct camif_vp *vp,
				    struct v4l2_pix_format *pix,
				    const struct camif_fmt **ffmt)
{
	struct camif_dev *camif = vp->camif;
	struct v4l2_rect *crop = &camif->camif_crop;
	unsigned int wmin, hmin, sc_hrmax, sc_vrmax;
	const struct vp_pix_limits *pix_lim;
	const struct camif_fmt *fmt;

	fmt = s3c_camif_find_format(vp, &pix->pixelformat, 0);

	if (WARN_ON(fmt == NULL))
		return -EINVAL;

	if (ffmt)
		*ffmt = fmt;

	pix_lim = &camif->variant->vp_pix_limits[vp->id];

	pr_debug("fmt: %ux%u, crop: %ux%u, bytesperline: %u\n",
		 pix->width, pix->height, crop->width, crop->height,
		 pix->bytesperline);
	/*
	 * Calculate minimum width and height according to the configured
	 * camera input interface crop rectangle and the resizer's capabilities.
	 */
	sc_hrmax = min(SCALER_MAX_RATIO, 1 << (ffs(crop->width) - 3));
	sc_vrmax = min(SCALER_MAX_RATIO, 1 << (ffs(crop->height) - 1));

	wmin = max_t(u32, pix_lim->min_out_width, crop->width / sc_hrmax);
	wmin = round_up(wmin, pix_lim->out_width_align);
	hmin = max_t(u32, 8, crop->height / sc_vrmax);
	hmin = round_up(hmin, 8);

	v4l_bound_align_image(&pix->width, wmin, pix_lim->max_sc_out_width,
			      ffs(pix_lim->out_width_align) - 1,
			      &pix->height, hmin, pix_lim->max_height, 0, 0);

	pix->bytesperline = pix->width * fmt->ybpp;
	pix->sizeimage = (pix->width * pix->height * fmt->depth) / 8;
	pix->pixelformat = fmt->fourcc;
	pix->colorspace = V4L2_COLORSPACE_JPEG;
	pix->field = V4L2_FIELD_NONE;

	pr_debug("%ux%u, wmin: %d, hmin: %d, sc_hrmax: %d, sc_vrmax: %d\n",
		 pix->width, pix->height, wmin, hmin, sc_hrmax, sc_vrmax);

	return 0;
}

static int s3c_camif_vidioc_try_fmt(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct camif_vp *vp = video_drvdata(file);
	return __camif_video_try_format(vp, &f->fmt.pix, NULL);
}

static int s3c_camif_vidioc_s_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct camif_vp *vp = video_drvdata(file);
	struct camif_frame *out_frame = &vp->out_frame;
	const struct camif_fmt *fmt = NULL;
	int ret;

	pr_debug("[vp%d]\n", vp->id);

	if (vb2_is_busy(&vp->vb_queue))
		return -EBUSY;

	ret = __camif_video_try_format(vp, &f->fmt.pix, &fmt);
	if (ret < 0)
		return ret;

	vp->out_fmt = fmt;
	vp->payload = pix->sizeimage;
	out_frame->f_width = pix->width;
	out_frame->f_height = pix->height;

	/* Reset composition rectangle */
	out_frame->rect.width = pix->width;
	out_frame->rect.height = pix->height;
	out_frame->rect.left = 0;
	out_frame->rect.top = 0;

	if (vp->owner == NULL)
		vp->owner = file_to_v4l2_fh(file);

	pr_debug("%ux%u. payload: %u. fmt: 0x%08x. %d %d. sizeimage: %d. bpl: %d\n",
		 out_frame->f_width, out_frame->f_height, vp->payload,
		 fmt->fourcc, pix->width * pix->height * fmt->depth,
		 fmt->depth, pix->sizeimage, pix->bytesperline);

	return 0;
}

/* Only check pixel formats at the sensor and the camif subdev pads */
static int camif_pipeline_validate(struct camif_dev *camif)
{
	struct v4l2_subdev_format src_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct media_pad *pad;
	int ret;

	/* Retrieve format at the sensor subdev source pad */
	pad = media_pad_remote_pad_first(&camif->pads[0]);
	if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
		return -EPIPE;

	src_fmt.pad = pad->index;
	ret = v4l2_subdev_call(camif->sensor.sd, pad, get_fmt, NULL, &src_fmt);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return -EPIPE;

	if (src_fmt.format.width != camif->mbus_fmt.width ||
	    src_fmt.format.height != camif->mbus_fmt.height ||
	    src_fmt.format.code != camif->mbus_fmt.code)
		return -EPIPE;

	return 0;
}

static int s3c_camif_streamon(struct file *file, void *priv,
			      enum v4l2_buf_type type)
{
	struct camif_vp *vp = video_drvdata(file);
	struct camif_dev *camif = vp->camif;
	struct media_entity *sensor = &camif->sensor.sd->entity;
	int ret;

	pr_debug("[vp%d]\n", vp->id);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		return -EBUSY;

	if (s3c_vp_active(vp))
		return 0;

	ret = media_pipeline_start(sensor->pads, camif->m_pipeline);
	if (ret < 0)
		return ret;

	ret = camif_pipeline_validate(camif);
	if (ret < 0) {
		media_pipeline_stop(sensor->pads);
		return ret;
	}

	return vb2_streamon(&vp->vb_queue, type);
}

static int s3c_camif_streamoff(struct file *file, void *priv,
			       enum v4l2_buf_type type)
{
	struct camif_vp *vp = video_drvdata(file);
	struct camif_dev *camif = vp->camif;
	int ret;

	pr_debug("[vp%d]\n", vp->id);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		return -EBUSY;

	ret = vb2_streamoff(&vp->vb_queue, type);
	if (ret == 0)
		media_pipeline_stop(camif->sensor.sd->entity.pads);
	return ret;
}

static int s3c_camif_reqbufs(struct file *file, void *priv,
			     struct v4l2_requestbuffers *rb)
{
	struct camif_vp *vp = video_drvdata(file);
	int ret;

	pr_debug("[vp%d] rb count: %d, owner: %p, priv: %p\n",
		 vp->id, rb->count, vp->owner, file_to_v4l2_fh(file));

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		return -EBUSY;

	if (rb->count)
		rb->count = max_t(u32, CAMIF_REQ_BUFS_MIN, rb->count);
	else
		vp->owner = NULL;

	ret = vb2_reqbufs(&vp->vb_queue, rb);
	if (ret < 0)
		return ret;

	if (rb->count && rb->count < CAMIF_REQ_BUFS_MIN) {
		rb->count = 0;
		vb2_reqbufs(&vp->vb_queue, rb);
		ret = -ENOMEM;
	}

	vp->reqbufs_count = rb->count;
	if (vp->owner == NULL && rb->count > 0)
		vp->owner = file_to_v4l2_fh(file);

	return ret;
}

static int s3c_camif_querybuf(struct file *file, void *priv,
			      struct v4l2_buffer *buf)
{
	struct camif_vp *vp = video_drvdata(file);
	return vb2_querybuf(&vp->vb_queue, buf);
}

static int s3c_camif_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct camif_vp *vp = video_drvdata(file);

	pr_debug("[vp%d]\n", vp->id);

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		return -EBUSY;

	return vb2_qbuf(&vp->vb_queue, vp->vdev.v4l2_dev->mdev, buf);
}

static int s3c_camif_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct camif_vp *vp = video_drvdata(file);

	pr_debug("[vp%d] sequence: %d\n", vp->id, vp->frame_sequence);

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		return -EBUSY;

	return vb2_dqbuf(&vp->vb_queue, buf, file->f_flags & O_NONBLOCK);
}

static int s3c_camif_create_bufs(struct file *file, void *priv,
				 struct v4l2_create_buffers *create)
{
	struct camif_vp *vp = video_drvdata(file);
	int ret;

	if (vp->owner && vp->owner != file_to_v4l2_fh(file))
		return -EBUSY;

	create->count = max_t(u32, 1, create->count);
	ret = vb2_create_bufs(&vp->vb_queue, create);

	if (!ret && vp->owner == NULL)
		vp->owner = file_to_v4l2_fh(file);

	return ret;
}

static int s3c_camif_prepare_buf(struct file *file, void *priv,
				 struct v4l2_buffer *b)
{
	struct camif_vp *vp = video_drvdata(file);
	return vb2_prepare_buf(&vp->vb_queue, vp->vdev.v4l2_dev->mdev, b);
}

static int s3c_camif_g_selection(struct file *file, void *priv,
				 struct v4l2_selection *sel)
{
	struct camif_vp *vp = video_drvdata(file);

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = vp->out_frame.f_width;
		sel->r.height = vp->out_frame.f_height;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		sel->r = vp->out_frame.rect;
		return 0;
	}

	return -EINVAL;
}

static void __camif_try_compose(struct camif_dev *camif, struct camif_vp *vp,
				struct v4l2_rect *r)
{
	/* s3c244x doesn't support composition */
	if (camif->variant->ip_revision == S3C244X_CAMIF_IP_REV) {
		*r = vp->out_frame.rect;
		return;
	}

	/* TODO: s3c64xx */
}

static int s3c_camif_s_selection(struct file *file, void *priv,
				 struct v4l2_selection *sel)
{
	struct camif_vp *vp = video_drvdata(file);
	struct camif_dev *camif = vp->camif;
	struct v4l2_rect rect = sel->r;
	unsigned long flags;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	__camif_try_compose(camif, vp, &rect);

	sel->r = rect;
	spin_lock_irqsave(&camif->slock, flags);
	vp->out_frame.rect = rect;
	vp->state |= ST_VP_CONFIG;
	spin_unlock_irqrestore(&camif->slock, flags);

	pr_debug("type: %#x, target: %#x, flags: %#x, (%d,%d)/%ux%u\n",
		 sel->type, sel->target, sel->flags,
		 sel->r.left, sel->r.top, sel->r.width, sel->r.height);

	return 0;
}

static const struct v4l2_ioctl_ops s3c_camif_ioctl_ops = {
	.vidioc_querycap	  = s3c_camif_vidioc_querycap,
	.vidioc_enum_input	  = s3c_camif_vidioc_enum_input,
	.vidioc_g_input		  = s3c_camif_vidioc_g_input,
	.vidioc_s_input		  = s3c_camif_vidioc_s_input,
	.vidioc_enum_fmt_vid_cap  = s3c_camif_vidioc_enum_fmt,
	.vidioc_try_fmt_vid_cap	  = s3c_camif_vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap	  = s3c_camif_vidioc_s_fmt,
	.vidioc_g_fmt_vid_cap	  = s3c_camif_vidioc_g_fmt,
	.vidioc_g_selection	  = s3c_camif_g_selection,
	.vidioc_s_selection	  = s3c_camif_s_selection,
	.vidioc_reqbufs		  = s3c_camif_reqbufs,
	.vidioc_querybuf	  = s3c_camif_querybuf,
	.vidioc_prepare_buf	  = s3c_camif_prepare_buf,
	.vidioc_create_bufs	  = s3c_camif_create_bufs,
	.vidioc_qbuf		  = s3c_camif_qbuf,
	.vidioc_dqbuf		  = s3c_camif_dqbuf,
	.vidioc_streamon	  = s3c_camif_streamon,
	.vidioc_streamoff	  = s3c_camif_streamoff,
	.vidioc_subscribe_event	  = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_log_status	  = v4l2_ctrl_log_status,
};

/*
 * Video node controls
 */
static int s3c_camif_video_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camif_vp *vp = ctrl->priv;
	struct camif_dev *camif = vp->camif;
	unsigned long flags;

	pr_debug("[vp%d] ctrl: %s, value: %d\n", vp->id,
		 ctrl->name, ctrl->val);

	spin_lock_irqsave(&camif->slock, flags);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		vp->hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		vp->vflip = ctrl->val;
		break;
	}

	vp->state |= ST_VP_CONFIG;
	spin_unlock_irqrestore(&camif->slock, flags);
	return 0;
}

/* Codec and preview video node control ops */
static const struct v4l2_ctrl_ops s3c_camif_video_ctrl_ops = {
	.s_ctrl = s3c_camif_video_s_ctrl,
};

int s3c_camif_register_video_node(struct camif_dev *camif, int idx)
{
	struct camif_vp *vp = &camif->vp[idx];
	struct vb2_queue *q = &vp->vb_queue;
	struct video_device *vfd = &vp->vdev;
	struct v4l2_ctrl *ctrl;
	int ret;

	memset(vfd, 0, sizeof(*vfd));
	snprintf(vfd->name, sizeof(vfd->name), "camif-%s",
		 vp->id == 0 ? "codec" : "preview");

	vfd->fops = &s3c_camif_fops;
	vfd->ioctl_ops = &s3c_camif_ioctl_ops;
	vfd->v4l2_dev = &camif->v4l2_dev;
	vfd->minor = -1;
	vfd->release = video_device_release_empty;
	vfd->lock = &camif->lock;
	vp->reqbufs_count = 0;

	INIT_LIST_HEAD(&vp->pending_buf_q);
	INIT_LIST_HEAD(&vp->active_buf_q);

	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->ops = &s3c_camif_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct camif_buffer);
	q->drv_priv = vp;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &vp->camif->lock;
	q->dev = camif->v4l2_dev.dev;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	vp->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vfd->entity, 1, &vp->pad);
	if (ret)
		return ret;

	video_set_drvdata(vfd, vp);

	v4l2_ctrl_handler_init(&vp->ctrl_handler, 1);
	ctrl = v4l2_ctrl_new_std(&vp->ctrl_handler, &s3c_camif_video_ctrl_ops,
				 V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (ctrl)
		ctrl->priv = vp;
	ctrl = v4l2_ctrl_new_std(&vp->ctrl_handler, &s3c_camif_video_ctrl_ops,
				 V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (ctrl)
		ctrl->priv = vp;

	ret = vp->ctrl_handler.error;
	if (ret < 0)
		goto err_me_cleanup;

	vfd->ctrl_handler = &vp->ctrl_handler;
	vfd->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_ctrlh_free;

	v4l2_info(&camif->v4l2_dev, "registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));
	return 0;

err_ctrlh_free:
	v4l2_ctrl_handler_free(&vp->ctrl_handler);
err_me_cleanup:
	media_entity_cleanup(&vfd->entity);
	return ret;
}

void s3c_camif_unregister_video_node(struct camif_dev *camif, int idx)
{
	struct video_device *vfd = &camif->vp[idx].vdev;

	if (video_is_registered(vfd)) {
		video_unregister_device(vfd);
		media_entity_cleanup(&vfd->entity);
		v4l2_ctrl_handler_free(vfd->ctrl_handler);
	}
}

/* Media bus pixel formats supported at the camif input */
static const u32 camif_mbus_formats[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_YVYU8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_VYUY8_2X8,
};

/*
 *  Camera input interface subdev operations
 */

static int s3c_camif_subdev_enum_mbus_code(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *sd_state,
					struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(camif_mbus_formats))
		return -EINVAL;

	code->code = camif_mbus_formats[code->index];
	return 0;
}

static int s3c_camif_subdev_get_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_format *fmt)
{
	struct camif_dev *camif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *mf;
		return 0;
	}

	mutex_lock(&camif->lock);

	switch (fmt->pad) {
	case CAMIF_SD_PAD_SINK:
		/* full camera input pixel size */
		*mf = camif->mbus_fmt;
		break;

	case CAMIF_SD_PAD_SOURCE_C...CAMIF_SD_PAD_SOURCE_P:
		/* crop rectangle at camera interface input */
		mf->width = camif->camif_crop.width;
		mf->height = camif->camif_crop.height;
		mf->code = camif->mbus_fmt.code;
		break;
	}

	mutex_unlock(&camif->lock);
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	return 0;
}

static void __camif_subdev_try_format(struct camif_dev *camif,
				struct v4l2_mbus_framefmt *mf, int pad)
{
	const struct s3c_camif_variant *variant = camif->variant;
	const struct vp_pix_limits *pix_lim;
	unsigned int i;

	/* FIXME: constraints against codec or preview path ? */
	pix_lim = &variant->vp_pix_limits[VP_CODEC];

	for (i = 0; i < ARRAY_SIZE(camif_mbus_formats); i++)
		if (camif_mbus_formats[i] == mf->code)
			break;

	if (i == ARRAY_SIZE(camif_mbus_formats))
		mf->code = camif_mbus_formats[0];

	if (pad == CAMIF_SD_PAD_SINK) {
		v4l_bound_align_image(&mf->width, 8, CAMIF_MAX_PIX_WIDTH,
				      ffs(pix_lim->out_width_align) - 1,
				      &mf->height, 8, CAMIF_MAX_PIX_HEIGHT, 0,
				      0);
	} else {
		struct v4l2_rect *crop = &camif->camif_crop;
		v4l_bound_align_image(&mf->width, 8, crop->width,
				      ffs(pix_lim->out_width_align) - 1,
				      &mf->height, 8, crop->height,
				      0, 0);
	}

	v4l2_dbg(1, debug, &camif->subdev, "%ux%u\n", mf->width, mf->height);
}

static int s3c_camif_subdev_set_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_format *fmt)
{
	struct camif_dev *camif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct v4l2_rect *crop = &camif->camif_crop;
	int i;

	v4l2_dbg(1, debug, sd, "pad%d: code: 0x%x, %ux%u\n",
		 fmt->pad, mf->code, mf->width, mf->height);

	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mutex_lock(&camif->lock);

	/*
	 * No pixel format change at the camera input is allowed
	 * while streaming.
	 */
	if (vb2_is_busy(&camif->vp[VP_CODEC].vb_queue) ||
	    vb2_is_busy(&camif->vp[VP_PREVIEW].vb_queue)) {
		mutex_unlock(&camif->lock);
		return -EBUSY;
	}

	__camif_subdev_try_format(camif, mf, fmt->pad);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*mf = fmt->format;
		mutex_unlock(&camif->lock);
		return 0;
	}

	switch (fmt->pad) {
	case CAMIF_SD_PAD_SINK:
		camif->mbus_fmt = *mf;
		/* Reset sink crop rectangle. */
		crop->width = mf->width;
		crop->height = mf->height;
		crop->left = 0;
		crop->top = 0;
		/*
		 * Reset source format (the camif's crop rectangle)
		 * and the video output resolution.
		 */
		for (i = 0; i < CAMIF_VP_NUM; i++) {
			struct camif_frame *frame = &camif->vp[i].out_frame;
			frame->rect = *crop;
			frame->f_width = mf->width;
			frame->f_height = mf->height;
		}
		break;

	case CAMIF_SD_PAD_SOURCE_C...CAMIF_SD_PAD_SOURCE_P:
		/* Pixel format can be only changed on the sink pad. */
		mf->code = camif->mbus_fmt.code;
		mf->width = crop->width;
		mf->height = crop->height;
		break;
	}

	mutex_unlock(&camif->lock);
	return 0;
}

static int s3c_camif_subdev_get_selection(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *sd_state,
					  struct v4l2_subdev_selection *sel)
{
	struct camif_dev *camif = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop = &camif->camif_crop;
	struct v4l2_mbus_framefmt *mf = &camif->mbus_fmt;

	if ((sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS) ||
	    sel->pad != CAMIF_SD_PAD_SINK)
		return -EINVAL;

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);
		return 0;
	}

	mutex_lock(&camif->lock);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		sel->r = *crop;
	} else { /* crop bounds */
		sel->r.width = mf->width;
		sel->r.height = mf->height;
		sel->r.left = 0;
		sel->r.top = 0;
	}

	mutex_unlock(&camif->lock);

	v4l2_dbg(1, debug, sd, "%s: crop: (%d,%d)/%ux%u, size: %ux%u\n",
		 __func__, crop->left, crop->top, crop->width,
		 crop->height, mf->width, mf->height);

	return 0;
}

static void __camif_try_crop(struct camif_dev *camif, struct v4l2_rect *r)
{
	struct v4l2_mbus_framefmt *mf = &camif->mbus_fmt;
	const struct camif_pix_limits *pix_lim = &camif->variant->pix_limits;
	unsigned int left = 2 * r->left;
	unsigned int top = 2 * r->top;

	/*
	 * Following constraints must be met:
	 *  - r->width + 2 * r->left = mf->width;
	 *  - r->height + 2 * r->top = mf->height;
	 *  - crop rectangle size and position must be aligned
	 *    to 8 or 2 pixels, depending on SoC version.
	 */
	v4l_bound_align_image(&r->width, 0, mf->width,
			      ffs(pix_lim->win_hor_offset_align) - 1,
			      &r->height, 0, mf->height, 1, 0);

	v4l_bound_align_image(&left, 0, mf->width - r->width,
			      ffs(pix_lim->win_hor_offset_align),
			      &top, 0, mf->height - r->height, 2, 0);

	r->left = left / 2;
	r->top = top / 2;
	r->width = mf->width - left;
	r->height = mf->height - top;
	/*
	 * Make sure we either downscale or upscale both the pixel
	 * width and height. Just return current crop rectangle if
	 * this scaler constraint is not met.
	 */
	if (camif->variant->ip_revision == S3C244X_CAMIF_IP_REV &&
	    camif_is_streaming(camif)) {
		unsigned int i;

		for (i = 0; i < CAMIF_VP_NUM; i++) {
			struct v4l2_rect *or = &camif->vp[i].out_frame.rect;
			if ((or->width > r->width) == (or->height > r->height))
				continue;
			*r = camif->camif_crop;
			pr_debug("Width/height scaling direction limitation\n");
			break;
		}
	}

	v4l2_dbg(1, debug, &camif->v4l2_dev, "crop: (%d,%d)/%ux%u, fmt: %ux%u\n",
		 r->left, r->top, r->width, r->height, mf->width, mf->height);
}

static int s3c_camif_subdev_set_selection(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *sd_state,
					  struct v4l2_subdev_selection *sel)
{
	struct camif_dev *camif = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop = &camif->camif_crop;
	struct camif_scaler scaler;

	if (sel->target != V4L2_SEL_TGT_CROP || sel->pad != CAMIF_SD_PAD_SINK)
		return -EINVAL;

	mutex_lock(&camif->lock);
	__camif_try_crop(camif, &sel->r);

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_crop(sd_state, sel->pad) = sel->r;
	} else {
		unsigned long flags;
		unsigned int i;

		spin_lock_irqsave(&camif->slock, flags);
		*crop = sel->r;

		for (i = 0; i < CAMIF_VP_NUM; i++) {
			struct camif_vp *vp = &camif->vp[i];
			scaler = vp->scaler;
			if (s3c_camif_get_scaler_config(vp, &scaler))
				continue;
			vp->scaler = scaler;
			vp->state |= ST_VP_CONFIG;
		}

		spin_unlock_irqrestore(&camif->slock, flags);
	}
	mutex_unlock(&camif->lock);

	v4l2_dbg(1, debug, sd, "%s: (%d,%d)/%ux%u, f_w: %u, f_h: %u\n",
		 __func__, crop->left, crop->top, crop->width, crop->height,
		 camif->mbus_fmt.width, camif->mbus_fmt.height);

	return 0;
}

static const struct v4l2_subdev_pad_ops s3c_camif_subdev_pad_ops = {
	.enum_mbus_code = s3c_camif_subdev_enum_mbus_code,
	.get_selection = s3c_camif_subdev_get_selection,
	.set_selection = s3c_camif_subdev_set_selection,
	.get_fmt = s3c_camif_subdev_get_fmt,
	.set_fmt = s3c_camif_subdev_set_fmt,
};

static const struct v4l2_subdev_ops s3c_camif_subdev_ops = {
	.pad = &s3c_camif_subdev_pad_ops,
};

static int s3c_camif_subdev_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camif_dev *camif = container_of(ctrl->handler, struct camif_dev,
					       ctrl_handler);
	unsigned long flags;

	spin_lock_irqsave(&camif->slock, flags);

	switch (ctrl->id) {
	case V4L2_CID_COLORFX:
		camif->colorfx = camif->ctrl_colorfx->val;
		/* Set Cb, Cr */
		switch (ctrl->val) {
		case V4L2_COLORFX_SEPIA:
			camif->colorfx_cb = 115;
			camif->colorfx_cr = 145;
			break;
		case V4L2_COLORFX_SET_CBCR:
			camif->colorfx_cb = camif->ctrl_colorfx_cbcr->val >> 8;
			camif->colorfx_cr = camif->ctrl_colorfx_cbcr->val & 0xff;
			break;
		default:
			/* for V4L2_COLORFX_BW and others */
			camif->colorfx_cb = 128;
			camif->colorfx_cr = 128;
		}
		break;
	case V4L2_CID_TEST_PATTERN:
		camif->test_pattern = camif->ctrl_test_pattern->val;
		break;
	default:
		WARN_ON(1);
	}

	camif->vp[VP_CODEC].state |= ST_VP_CONFIG;
	camif->vp[VP_PREVIEW].state |= ST_VP_CONFIG;
	spin_unlock_irqrestore(&camif->slock, flags);

	return 0;
}

static const struct v4l2_ctrl_ops s3c_camif_subdev_ctrl_ops = {
	.s_ctrl	= s3c_camif_subdev_s_ctrl,
};

static const char * const s3c_camif_test_pattern_menu[] = {
	"Disabled",
	"Color bars",
	"Horizontal increment",
	"Vertical increment",
};

int s3c_camif_create_subdev(struct camif_dev *camif)
{
	struct v4l2_ctrl_handler *handler = &camif->ctrl_handler;
	struct v4l2_subdev *sd = &camif->subdev;
	int ret;

	v4l2_subdev_init(sd, &s3c_camif_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(sd->name, "S3C-CAMIF", sizeof(sd->name));

	camif->pads[CAMIF_SD_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	camif->pads[CAMIF_SD_PAD_SOURCE_C].flags = MEDIA_PAD_FL_SOURCE;
	camif->pads[CAMIF_SD_PAD_SOURCE_P].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, CAMIF_SD_PADS_NUM,
				camif->pads);
	if (ret)
		return ret;

	v4l2_ctrl_handler_init(handler, 3);
	camif->ctrl_test_pattern = v4l2_ctrl_new_std_menu_items(handler,
			&s3c_camif_subdev_ctrl_ops, V4L2_CID_TEST_PATTERN,
			ARRAY_SIZE(s3c_camif_test_pattern_menu) - 1, 0, 0,
			s3c_camif_test_pattern_menu);

	if (camif->variant->has_img_effect) {
		camif->ctrl_colorfx = v4l2_ctrl_new_std_menu(handler,
				&s3c_camif_subdev_ctrl_ops,
				V4L2_CID_COLORFX, V4L2_COLORFX_SET_CBCR,
				~0x981f, V4L2_COLORFX_NONE);

		camif->ctrl_colorfx_cbcr = v4l2_ctrl_new_std(handler,
				&s3c_camif_subdev_ctrl_ops,
				V4L2_CID_COLORFX_CBCR, 0, 0xffff, 1, 0);
	}

	if (handler->error) {
		v4l2_ctrl_handler_free(handler);
		media_entity_cleanup(&sd->entity);
		return handler->error;
	}

	if (camif->variant->has_img_effect)
		v4l2_ctrl_auto_cluster(2, &camif->ctrl_colorfx,
			       V4L2_COLORFX_SET_CBCR, false);

	sd->ctrl_handler = handler;
	v4l2_set_subdevdata(sd, camif);

	return 0;
}

void s3c_camif_unregister_subdev(struct camif_dev *camif)
{
	struct v4l2_subdev *sd = &camif->subdev;

	/* Return if not registered */
	if (v4l2_get_subdevdata(sd) == NULL)
		return;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&camif->ctrl_handler);
	v4l2_set_subdevdata(sd, NULL);
}

int s3c_camif_set_defaults(struct camif_dev *camif)
{
	unsigned int ip_rev = camif->variant->ip_revision;
	int i;

	for (i = 0; i < CAMIF_VP_NUM; i++) {
		struct camif_vp *vp = &camif->vp[i];
		struct camif_frame *f = &vp->out_frame;

		vp->camif = camif;
		vp->id = i;
		vp->offset = camif->variant->vp_offset;

		if (ip_rev == S3C244X_CAMIF_IP_REV)
			vp->fmt_flags = i ? FMT_FL_S3C24XX_PREVIEW :
					FMT_FL_S3C24XX_CODEC;
		else
			vp->fmt_flags = FMT_FL_S3C64XX;

		vp->out_fmt = s3c_camif_find_format(vp, NULL, 0);
		BUG_ON(vp->out_fmt == NULL);

		memset(f, 0, sizeof(*f));
		f->f_width = CAMIF_DEF_WIDTH;
		f->f_height = CAMIF_DEF_HEIGHT;
		f->rect.width = CAMIF_DEF_WIDTH;
		f->rect.height = CAMIF_DEF_HEIGHT;

		/* Scaler is always enabled */
		vp->scaler.enable = 1;

		vp->payload = (f->f_width * f->f_height *
			       vp->out_fmt->depth) / 8;
	}

	memset(&camif->mbus_fmt, 0, sizeof(camif->mbus_fmt));
	camif->mbus_fmt.width = CAMIF_DEF_WIDTH;
	camif->mbus_fmt.height = CAMIF_DEF_HEIGHT;
	camif->mbus_fmt.code  = camif_mbus_formats[0];

	memset(&camif->camif_crop, 0, sizeof(camif->camif_crop));
	camif->camif_crop.width = CAMIF_DEF_WIDTH;
	camif->camif_crop.height = CAMIF_DEF_HEIGHT;

	return 0;
}
