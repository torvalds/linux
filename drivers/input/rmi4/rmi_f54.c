/*
 * Copyright (c) 2012-2015 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include "rmi_driver.h"

#define F54_NAME		"rmi4_f54"

/* F54 data offsets */
#define F54_REPORT_DATA_OFFSET  3
#define F54_FIFO_OFFSET         1
#define F54_NUM_TX_OFFSET       1
#define F54_NUM_RX_OFFSET       0

/* F54 commands */
#define F54_GET_REPORT          1
#define F54_FORCE_CAL           2

/* Fixed sizes of reports */
#define F54_QUERY_LEN			27

/* F54 capabilities */
#define F54_CAP_BASELINE	(1 << 2)
#define F54_CAP_IMAGE8		(1 << 3)
#define F54_CAP_IMAGE16		(1 << 6)

/**
 * enum rmi_f54_report_type - RMI4 F54 report types
 *
 * @F54_8BIT_IMAGE:	Normalized 8-Bit Image Report. The capacitance variance
 *			from baseline for each pixel.
 *
 * @F54_16BIT_IMAGE:	Normalized 16-Bit Image Report. The capacitance variance
 *			from baseline for each pixel.
 *
 * @F54_RAW_16BIT_IMAGE:
 *			Raw 16-Bit Image Report. The raw capacitance for each
 *			pixel.
 *
 * @F54_TRUE_BASELINE:	True Baseline Report. The baseline capacitance for each
 *			pixel.
 *
 * @F54_FULL_RAW_CAP:   Full Raw Capacitance Report. The raw capacitance with
 *			low reference set to its minimum value and high
 *			reference set to its maximum value.
 *
 * @F54_FULL_RAW_CAP_RX_OFFSET_REMOVED:
 *			Full Raw Capacitance with Receiver Offset Removed
 *			Report. Set Low reference to its minimum value and high
 *			references to its maximum value, then report the raw
 *			capacitance for each pixel.
 */
enum rmi_f54_report_type {
	F54_REPORT_NONE = 0,
	F54_8BIT_IMAGE = 1,
	F54_16BIT_IMAGE = 2,
	F54_RAW_16BIT_IMAGE = 3,
	F54_TRUE_BASELINE = 9,
	F54_FULL_RAW_CAP = 19,
	F54_FULL_RAW_CAP_RX_OFFSET_REMOVED = 20,
	F54_MAX_REPORT_TYPE,
};

const char *rmi_f54_report_type_names[] = {
	[F54_REPORT_NONE]		= "Unknown",
	[F54_8BIT_IMAGE]		= "Normalized 8-Bit Image",
	[F54_16BIT_IMAGE]		= "Normalized 16-Bit Image",
	[F54_RAW_16BIT_IMAGE]		= "Raw 16-Bit Image",
	[F54_TRUE_BASELINE]		= "True Baseline",
	[F54_FULL_RAW_CAP]		= "Full Raw Capacitance",
	[F54_FULL_RAW_CAP_RX_OFFSET_REMOVED]
					= "Full Raw Capacitance RX Offset Removed",
};

struct rmi_f54_reports {
	int start;
	int size;
};

struct f54_data {
	struct rmi_function *fn;

	u8 qry[F54_QUERY_LEN];
	u8 num_rx_electrodes;
	u8 num_tx_electrodes;
	u8 capabilities;
	u16 clock_rate;
	u8 family;

	enum rmi_f54_report_type report_type;
	u8 *report_data;
	int report_size;
	struct rmi_f54_reports standard_report[2];

	bool is_busy;
	struct mutex status_mutex;
	struct mutex data_mutex;

	struct workqueue_struct *workqueue;
	struct delayed_work work;
	unsigned long timeout;

	struct completion cmd_done;

	/* V4L2 support */
	struct v4l2_device v4l2;
	struct v4l2_pix_format format;
	struct video_device vdev;
	struct vb2_queue queue;
	struct mutex lock;
	int input;
	enum rmi_f54_report_type inputs[F54_MAX_REPORT_TYPE];
};

/*
 * Basic checks on report_type to ensure we write a valid type
 * to the sensor.
 */
static bool is_f54_report_type_valid(struct f54_data *f54,
				     enum rmi_f54_report_type reptype)
{
	switch (reptype) {
	case F54_8BIT_IMAGE:
		return f54->capabilities & F54_CAP_IMAGE8;
	case F54_16BIT_IMAGE:
	case F54_RAW_16BIT_IMAGE:
		return f54->capabilities & F54_CAP_IMAGE16;
	case F54_TRUE_BASELINE:
		return f54->capabilities & F54_CAP_IMAGE16;
	case F54_FULL_RAW_CAP:
	case F54_FULL_RAW_CAP_RX_OFFSET_REMOVED:
		return true;
	default:
		return false;
	}
}

static enum rmi_f54_report_type rmi_f54_get_reptype(struct f54_data *f54,
						unsigned int i)
{
	if (i >= F54_MAX_REPORT_TYPE)
		return F54_REPORT_NONE;

	return f54->inputs[i];
}

static void rmi_f54_create_input_map(struct f54_data *f54)
{
	int i = 0;
	enum rmi_f54_report_type reptype;

	for (reptype = 1; reptype < F54_MAX_REPORT_TYPE; reptype++) {
		if (!is_f54_report_type_valid(f54, reptype))
			continue;

		f54->inputs[i++] = reptype;
	}

	/* Remaining values are zero via kzalloc */
}

static int rmi_f54_request_report(struct rmi_function *fn, u8 report_type)
{
	struct f54_data *f54 = dev_get_drvdata(&fn->dev);
	struct rmi_device *rmi_dev = fn->rmi_dev;
	int error;

	/* Write Report Type into F54_AD_Data0 */
	if (f54->report_type != report_type) {
		error = rmi_write(rmi_dev, f54->fn->fd.data_base_addr,
				  report_type);
		if (error)
			return error;
		f54->report_type = report_type;
	}

	/*
	 * Small delay after disabling interrupts to avoid race condition
	 * in firmare. This value is a bit higher than absolutely necessary.
	 * Should be removed once issue is resolved in firmware.
	 */
	usleep_range(2000, 3000);

	mutex_lock(&f54->data_mutex);

	error = rmi_write(rmi_dev, fn->fd.command_base_addr, F54_GET_REPORT);
	if (error < 0)
		return error;

	init_completion(&f54->cmd_done);

	f54->is_busy = 1;
	f54->timeout = jiffies + msecs_to_jiffies(100);

	queue_delayed_work(f54->workqueue, &f54->work, 0);

	mutex_unlock(&f54->data_mutex);

	return 0;
}

static size_t rmi_f54_get_report_size(struct f54_data *f54)
{
	u8 rx = f54->num_rx_electrodes ? : f54->num_rx_electrodes;
	u8 tx = f54->num_tx_electrodes ? : f54->num_tx_electrodes;
	size_t size;

	switch (rmi_f54_get_reptype(f54, f54->input)) {
	case F54_8BIT_IMAGE:
		size = rx * tx;
		break;
	case F54_16BIT_IMAGE:
	case F54_RAW_16BIT_IMAGE:
	case F54_TRUE_BASELINE:
	case F54_FULL_RAW_CAP:
	case F54_FULL_RAW_CAP_RX_OFFSET_REMOVED:
		size = sizeof(u16) * rx * tx;
		break;
	default:
		size = 0;
	}

	return size;
}

static int rmi_f54_get_pixel_fmt(enum rmi_f54_report_type reptype, u32 *pixfmt)
{
	int ret = 0;

	switch (reptype) {
	case F54_8BIT_IMAGE:
		*pixfmt = V4L2_TCH_FMT_DELTA_TD08;
		break;

	case F54_16BIT_IMAGE:
		*pixfmt = V4L2_TCH_FMT_DELTA_TD16;
		break;

	case F54_RAW_16BIT_IMAGE:
	case F54_TRUE_BASELINE:
	case F54_FULL_RAW_CAP:
	case F54_FULL_RAW_CAP_RX_OFFSET_REMOVED:
		*pixfmt = V4L2_TCH_FMT_TU16;
		break;

	case F54_REPORT_NONE:
	case F54_MAX_REPORT_TYPE:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_file_operations rmi_f54_video_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

static int rmi_f54_queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
			       unsigned int *nplanes, unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct f54_data *f54 = q->drv_priv;

	if (*nplanes)
		return sizes[0] < rmi_f54_get_report_size(f54) ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = rmi_f54_get_report_size(f54);

	return 0;
}

static void rmi_f54_buffer_queue(struct vb2_buffer *vb)
{
	struct f54_data *f54 = vb2_get_drv_priv(vb->vb2_queue);
	u16 *ptr;
	enum vb2_buffer_state state;
	enum rmi_f54_report_type reptype;
	int ret;

	mutex_lock(&f54->status_mutex);

	reptype = rmi_f54_get_reptype(f54, f54->input);
	if (reptype == F54_REPORT_NONE) {
		state = VB2_BUF_STATE_ERROR;
		goto done;
	}

	if (f54->is_busy) {
		state = VB2_BUF_STATE_ERROR;
		goto done;
	}

	ret = rmi_f54_request_report(f54->fn, reptype);
	if (ret) {
		dev_err(&f54->fn->dev, "Error requesting F54 report\n");
		state = VB2_BUF_STATE_ERROR;
		goto done;
	}

	/* get frame data */
	mutex_lock(&f54->data_mutex);

	while (f54->is_busy) {
		mutex_unlock(&f54->data_mutex);
		if (!wait_for_completion_timeout(&f54->cmd_done,
						 msecs_to_jiffies(1000))) {
			dev_err(&f54->fn->dev, "Timed out\n");
			state = VB2_BUF_STATE_ERROR;
			goto done;
		}
		mutex_lock(&f54->data_mutex);
	}

	ptr = vb2_plane_vaddr(vb, 0);
	if (!ptr) {
		dev_err(&f54->fn->dev, "Error acquiring frame ptr\n");
		state = VB2_BUF_STATE_ERROR;
		goto data_done;
	}

	memcpy(ptr, f54->report_data, f54->report_size);
	vb2_set_plane_payload(vb, 0, rmi_f54_get_report_size(f54));
	state = VB2_BUF_STATE_DONE;

data_done:
	mutex_unlock(&f54->data_mutex);
done:
	vb2_buffer_done(vb, state);
	mutex_unlock(&f54->status_mutex);
}

/* V4L2 structures */
static const struct vb2_ops rmi_f54_queue_ops = {
	.queue_setup            = rmi_f54_queue_setup,
	.buf_queue              = rmi_f54_buffer_queue,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static const struct vb2_queue rmi_f54_queue = {
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ,
	.buf_struct_size = sizeof(struct vb2_buffer),
	.ops = &rmi_f54_queue_ops,
	.mem_ops = &vb2_vmalloc_memops,
	.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,
	.min_buffers_needed = 1,
};

static int rmi_f54_vidioc_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	struct f54_data *f54 = video_drvdata(file);

	strlcpy(cap->driver, F54_NAME, sizeof(cap->driver));
	strlcpy(cap->card, SYNAPTICS_INPUT_DEVICE_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		"rmi4:%s", dev_name(&f54->fn->dev));

	return 0;
}

static int rmi_f54_vidioc_enum_input(struct file *file, void *priv,
				     struct v4l2_input *i)
{
	struct f54_data *f54 = video_drvdata(file);
	enum rmi_f54_report_type reptype;

	reptype = rmi_f54_get_reptype(f54, i->index);
	if (reptype == F54_REPORT_NONE)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_TOUCH;

	strlcpy(i->name, rmi_f54_report_type_names[reptype], sizeof(i->name));
	return 0;
}

static int rmi_f54_set_input(struct f54_data *f54, unsigned int i)
{
	struct v4l2_pix_format *f = &f54->format;
	enum rmi_f54_report_type reptype;
	int ret;

	reptype = rmi_f54_get_reptype(f54, i);
	if (reptype == F54_REPORT_NONE)
		return -EINVAL;

	ret = rmi_f54_get_pixel_fmt(reptype, &f->pixelformat);
	if (ret)
		return ret;

	f54->input = i;

	f->width = f54->num_rx_electrodes;
	f->height = f54->num_tx_electrodes;
	f->field = V4L2_FIELD_NONE;
	f->colorspace = V4L2_COLORSPACE_RAW;
	f->bytesperline = f->width * sizeof(u16);
	f->sizeimage = f->width * f->height * sizeof(u16);

	return 0;
}

static int rmi_f54_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return rmi_f54_set_input(video_drvdata(file), i);
}

static int rmi_f54_vidioc_g_input(struct file *file, void *priv,
				  unsigned int *i)
{
	struct f54_data *f54 = video_drvdata(file);

	*i = f54->input;

	return 0;
}

static int rmi_f54_vidioc_fmt(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct f54_data *f54 = video_drvdata(file);

	f->fmt.pix = f54->format;

	return 0;
}

static int rmi_f54_vidioc_enum_fmt(struct file *file, void *priv,
				   struct v4l2_fmtdesc *fmt)
{
	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (fmt->index) {
	case 0:
		fmt->pixelformat = V4L2_TCH_FMT_DELTA_TD16;
		break;

	case 1:
		fmt->pixelformat = V4L2_TCH_FMT_DELTA_TD08;
		break;

	case 2:
		fmt->pixelformat = V4L2_TCH_FMT_TU16;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int rmi_f54_vidioc_g_parm(struct file *file, void *fh,
				 struct v4l2_streamparm *a)
{
	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	a->parm.capture.readbuffers = 1;
	a->parm.capture.timeperframe.numerator = 1;
	a->parm.capture.timeperframe.denominator = 10;
	return 0;
}

static const struct v4l2_ioctl_ops rmi_f54_video_ioctl_ops = {
	.vidioc_querycap	= rmi_f54_vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = rmi_f54_vidioc_enum_fmt,
	.vidioc_s_fmt_vid_cap	= rmi_f54_vidioc_fmt,
	.vidioc_g_fmt_vid_cap	= rmi_f54_vidioc_fmt,
	.vidioc_try_fmt_vid_cap	= rmi_f54_vidioc_fmt,
	.vidioc_g_parm		= rmi_f54_vidioc_g_parm,

	.vidioc_enum_input	= rmi_f54_vidioc_enum_input,
	.vidioc_g_input		= rmi_f54_vidioc_g_input,
	.vidioc_s_input		= rmi_f54_vidioc_s_input,

	.vidioc_reqbufs		= vb2_ioctl_reqbufs,
	.vidioc_create_bufs	= vb2_ioctl_create_bufs,
	.vidioc_querybuf	= vb2_ioctl_querybuf,
	.vidioc_qbuf		= vb2_ioctl_qbuf,
	.vidioc_dqbuf		= vb2_ioctl_dqbuf,
	.vidioc_expbuf		= vb2_ioctl_expbuf,

	.vidioc_streamon	= vb2_ioctl_streamon,
	.vidioc_streamoff	= vb2_ioctl_streamoff,
};

static const struct video_device rmi_f54_video_device = {
	.name = "Synaptics RMI4",
	.fops = &rmi_f54_video_fops,
	.ioctl_ops = &rmi_f54_video_ioctl_ops,
	.release = video_device_release_empty,
	.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TOUCH |
		       V4L2_CAP_READWRITE | V4L2_CAP_STREAMING,
};

static void rmi_f54_work(struct work_struct *work)
{
	struct f54_data *f54 = container_of(work, struct f54_data, work.work);
	struct rmi_function *fn = f54->fn;
	u8 fifo[2];
	struct rmi_f54_reports *report;
	int report_size;
	u8 command;
	u8 *data;
	int error;

	data = f54->report_data;
	report_size = rmi_f54_get_report_size(f54);
	if (report_size == 0) {
		dev_err(&fn->dev, "Bad report size, report type=%d\n",
				f54->report_type);
		error = -EINVAL;
		goto error;     /* retry won't help */
	}
	f54->standard_report[0].size = report_size;
	report = f54->standard_report;

	mutex_lock(&f54->data_mutex);

	/*
	 * Need to check if command has completed.
	 * If not try again later.
	 */
	error = rmi_read(fn->rmi_dev, f54->fn->fd.command_base_addr,
			 &command);
	if (error) {
		dev_err(&fn->dev, "Failed to read back command\n");
		goto error;
	}
	if (command & F54_GET_REPORT) {
		if (time_after(jiffies, f54->timeout)) {
			dev_err(&fn->dev, "Get report command timed out\n");
			error = -ETIMEDOUT;
		}
		report_size = 0;
		goto error;
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "Get report command completed, reading data\n");

	report_size = 0;
	for (; report->size; report++) {
		fifo[0] = report->start & 0xff;
		fifo[1] = (report->start >> 8) & 0xff;
		error = rmi_write_block(fn->rmi_dev,
					fn->fd.data_base_addr + F54_FIFO_OFFSET,
					fifo, sizeof(fifo));
		if (error) {
			dev_err(&fn->dev, "Failed to set fifo start offset\n");
			goto abort;
		}

		error = rmi_read_block(fn->rmi_dev, fn->fd.data_base_addr +
				       F54_REPORT_DATA_OFFSET, data,
				       report->size);
		if (error) {
			dev_err(&fn->dev, "%s: read [%d bytes] returned %d\n",
				__func__, report->size, error);
			goto abort;
		}
		data += report->size;
		report_size += report->size;
	}

abort:
	f54->report_size = error ? 0 : report_size;
error:
	if (error)
		report_size = 0;

	if (report_size == 0 && !error) {
		queue_delayed_work(f54->workqueue, &f54->work,
				   msecs_to_jiffies(1));
	} else {
		f54->is_busy = false;
		complete(&f54->cmd_done);
	}

	mutex_unlock(&f54->data_mutex);
}

static int rmi_f54_attention(struct rmi_function *fn, unsigned long *irqbits)
{
	return 0;
}

static int rmi_f54_config(struct rmi_function *fn)
{
	struct rmi_driver *drv = fn->rmi_dev->driver;

	drv->set_irq_bits(fn->rmi_dev, fn->irq_mask);

	return 0;
}

static int rmi_f54_detect(struct rmi_function *fn)
{
	int error;
	struct f54_data *f54;

	f54 = dev_get_drvdata(&fn->dev);

	error = rmi_read_block(fn->rmi_dev, fn->fd.query_base_addr,
			       &f54->qry, sizeof(f54->qry));
	if (error) {
		dev_err(&fn->dev, "%s: Failed to query F54 properties\n",
			__func__);
		return error;
	}

	f54->num_rx_electrodes = f54->qry[0];
	f54->num_tx_electrodes = f54->qry[1];
	f54->capabilities = f54->qry[2];
	f54->clock_rate = f54->qry[3] | (f54->qry[4] << 8);
	f54->family = f54->qry[5];

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F54 num_rx_electrodes: %d\n",
		f54->num_rx_electrodes);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F54 num_tx_electrodes: %d\n",
		f54->num_tx_electrodes);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F54 capabilities: 0x%x\n",
		f54->capabilities);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F54 clock rate: 0x%x\n",
		f54->clock_rate);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F54 family: 0x%x\n",
		f54->family);

	f54->is_busy = false;

	return 0;
}

static int rmi_f54_probe(struct rmi_function *fn)
{
	struct f54_data *f54;
	int ret;
	u8 rx, tx;

	f54 = devm_kzalloc(&fn->dev, sizeof(struct f54_data), GFP_KERNEL);
	if (!f54)
		return -ENOMEM;

	f54->fn = fn;
	dev_set_drvdata(&fn->dev, f54);

	ret = rmi_f54_detect(fn);
	if (ret)
		return ret;

	mutex_init(&f54->data_mutex);
	mutex_init(&f54->status_mutex);

	rx = f54->num_rx_electrodes;
	tx = f54->num_tx_electrodes;
	f54->report_data = devm_kzalloc(&fn->dev,
					sizeof(u16) * tx * rx,
					GFP_KERNEL);
	if (f54->report_data == NULL)
		return -ENOMEM;

	INIT_DELAYED_WORK(&f54->work, rmi_f54_work);

	f54->workqueue = create_singlethread_workqueue("rmi4-poller");
	if (!f54->workqueue)
		return -ENOMEM;

	rmi_f54_create_input_map(f54);

	/* register video device */
	strlcpy(f54->v4l2.name, F54_NAME, sizeof(f54->v4l2.name));
	ret = v4l2_device_register(&fn->dev, &f54->v4l2);
	if (ret) {
		dev_err(&fn->dev, "Unable to register video dev.\n");
		goto remove_wq;
	}

	/* initialize the queue */
	mutex_init(&f54->lock);
	f54->queue = rmi_f54_queue;
	f54->queue.drv_priv = f54;
	f54->queue.lock = &f54->lock;
	f54->queue.dev = &fn->dev;

	ret = vb2_queue_init(&f54->queue);
	if (ret)
		goto remove_v4l2;

	f54->vdev = rmi_f54_video_device;
	f54->vdev.v4l2_dev = &f54->v4l2;
	f54->vdev.lock = &f54->lock;
	f54->vdev.vfl_dir = VFL_DIR_RX;
	f54->vdev.queue = &f54->queue;
	video_set_drvdata(&f54->vdev, f54);

	ret = video_register_device(&f54->vdev, VFL_TYPE_TOUCH, -1);
	if (ret) {
		dev_err(&fn->dev, "Unable to register video subdevice.");
		goto remove_v4l2;
	}

	return 0;

remove_v4l2:
	v4l2_device_unregister(&f54->v4l2);
remove_wq:
	cancel_delayed_work_sync(&f54->work);
	flush_workqueue(f54->workqueue);
	destroy_workqueue(f54->workqueue);
	return ret;
}

static void rmi_f54_remove(struct rmi_function *fn)
{
	struct f54_data *f54 = dev_get_drvdata(&fn->dev);

	video_unregister_device(&f54->vdev);
	v4l2_device_unregister(&f54->v4l2);
}

struct rmi_function_handler rmi_f54_handler = {
	.driver = {
		.name = F54_NAME,
	},
	.func = 0x54,
	.probe = rmi_f54_probe,
	.config = rmi_f54_config,
	.attention = rmi_f54_attention,
	.remove = rmi_f54_remove,
};
