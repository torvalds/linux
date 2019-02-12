// SPDX-License-Identifier: GPL-2.0
/*
 * video-i2c.c - Support for I2C transport video devices
 *
 * Copyright (C) 2018 Matt Ranostay <matt.ranostay@konsulko.com>
 *
 * Supported:
 * - Panasonic AMG88xx Grid-Eye Sensors
 */

#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/hwmon.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#define VIDEO_I2C_DRIVER	"video-i2c"

struct video_i2c_chip;

struct video_i2c_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct video_i2c_data {
	struct i2c_client *client;
	const struct video_i2c_chip *chip;
	struct mutex lock;
	spinlock_t slock;
	unsigned int sequence;
	struct mutex queue_lock;

	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct vb2_queue vb_vidq;

	struct task_struct *kthread_vid_cap;
	struct list_head vid_cap_active;
};

static const struct v4l2_fmtdesc amg88xx_format = {
	.pixelformat = V4L2_PIX_FMT_Y12,
};

static const struct v4l2_frmsize_discrete amg88xx_size = {
	.width = 8,
	.height = 8,
};

struct video_i2c_chip {
	/* video dimensions */
	const struct v4l2_fmtdesc *format;
	const struct v4l2_frmsize_discrete *size;

	/* max frames per second */
	unsigned int max_fps;

	/* pixel buffer size */
	unsigned int buffer_size;

	/* pixel size in bits */
	unsigned int bpp;

	/* xfer function */
	int (*xfer)(struct video_i2c_data *data, char *buf);

	/* hwmon init function */
	int (*hwmon_init)(struct video_i2c_data *data);
};

static int amg88xx_xfer(struct video_i2c_data *data, char *buf)
{
	struct i2c_client *client = data->client;
	struct i2c_msg msg[2];
	u8 reg = 0x80;
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf  = (char *)&reg;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = data->chip->buffer_size;
	msg[1].buf = (char *)buf;

	ret = i2c_transfer(client->adapter, msg, 2);

	return (ret == 2) ? 0 : -EIO;
}

#if IS_ENABLED(CONFIG_HWMON)

static const u32 amg88xx_temp_config[] = {
	HWMON_T_INPUT,
	0
};

static const struct hwmon_channel_info amg88xx_temp = {
	.type = hwmon_temp,
	.config = amg88xx_temp_config,
};

static const struct hwmon_channel_info *amg88xx_info[] = {
	&amg88xx_temp,
	NULL
};

static umode_t amg88xx_is_visible(const void *drvdata,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	return 0444;
}

static int amg88xx_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct video_i2c_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int tmp = i2c_smbus_read_word_data(client, 0x0e);

	if (tmp < 0)
		return tmp;

	/*
	 * Check for sign bit, this isn't a two's complement value but an
	 * absolute temperature that needs to be inverted in the case of being
	 * negative.
	 */
	if (tmp & BIT(11))
		tmp = -(tmp & 0x7ff);

	*val = (tmp * 625) / 10;

	return 0;
}

static const struct hwmon_ops amg88xx_hwmon_ops = {
	.is_visible = amg88xx_is_visible,
	.read = amg88xx_read,
};

static const struct hwmon_chip_info amg88xx_chip_info = {
	.ops = &amg88xx_hwmon_ops,
	.info = amg88xx_info,
};

static int amg88xx_hwmon_init(struct video_i2c_data *data)
{
	void *hwmon = devm_hwmon_device_register_with_info(&data->client->dev,
				"amg88xx", data, &amg88xx_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon);
}
#else
#define	amg88xx_hwmon_init	NULL
#endif

#define AMG88XX		0

static const struct video_i2c_chip video_i2c_chip[] = {
	[AMG88XX] = {
		.size		= &amg88xx_size,
		.format		= &amg88xx_format,
		.max_fps	= 10,
		.buffer_size	= 128,
		.bpp		= 16,
		.xfer		= &amg88xx_xfer,
		.hwmon_init	= amg88xx_hwmon_init,
	},
};

static const struct v4l2_file_operations video_i2c_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.poll		= vb2_fop_poll,
	.read		= vb2_fop_read,
	.mmap		= vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static int queue_setup(struct vb2_queue *vq,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct video_i2c_data *data = vb2_get_drv_priv(vq);
	unsigned int size = data->chip->buffer_size;

	if (vq->num_buffers + *nbuffers < 2)
		*nbuffers = 2;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct video_i2c_data *data = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int size = data->chip->buffer_size;

	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;

	vbuf->field = V4L2_FIELD_NONE;
	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct video_i2c_data *data = vb2_get_drv_priv(vb->vb2_queue);
	struct video_i2c_buffer *buf =
			container_of(vbuf, struct video_i2c_buffer, vb);

	spin_lock(&data->slock);
	list_add_tail(&buf->list, &data->vid_cap_active);
	spin_unlock(&data->slock);
}

static int video_i2c_thread_vid_cap(void *priv)
{
	struct video_i2c_data *data = priv;
	unsigned int delay = msecs_to_jiffies(1000 / data->chip->max_fps);

	set_freezable();

	do {
		unsigned long start_jiffies = jiffies;
		struct video_i2c_buffer *vid_cap_buf = NULL;
		int schedule_delay;

		try_to_freeze();

		spin_lock(&data->slock);

		if (!list_empty(&data->vid_cap_active)) {
			vid_cap_buf = list_last_entry(&data->vid_cap_active,
						 struct video_i2c_buffer, list);
			list_del(&vid_cap_buf->list);
		}

		spin_unlock(&data->slock);

		if (vid_cap_buf) {
			struct vb2_buffer *vb2_buf = &vid_cap_buf->vb.vb2_buf;
			void *vbuf = vb2_plane_vaddr(vb2_buf, 0);
			int ret;

			ret = data->chip->xfer(data, vbuf);
			vb2_buf->timestamp = ktime_get_ns();
			vid_cap_buf->vb.sequence = data->sequence++;
			vb2_buffer_done(vb2_buf, ret ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		}

		schedule_delay = delay - (jiffies - start_jiffies);

		if (time_after(jiffies, start_jiffies + delay))
			schedule_delay = delay;

		schedule_timeout_interruptible(schedule_delay);
	} while (!kthread_should_stop());

	return 0;
}

static void video_i2c_del_list(struct vb2_queue *vq, enum vb2_buffer_state state)
{
	struct video_i2c_data *data = vb2_get_drv_priv(vq);
	struct video_i2c_buffer *buf, *tmp;

	spin_lock(&data->slock);

	list_for_each_entry_safe(buf, tmp, &data->vid_cap_active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}

	spin_unlock(&data->slock);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct video_i2c_data *data = vb2_get_drv_priv(vq);

	if (data->kthread_vid_cap)
		return 0;

	data->sequence = 0;
	data->kthread_vid_cap = kthread_run(video_i2c_thread_vid_cap, data,
					    "%s-vid-cap", data->v4l2_dev.name);
	if (!IS_ERR(data->kthread_vid_cap))
		return 0;

	video_i2c_del_list(vq, VB2_BUF_STATE_QUEUED);

	return PTR_ERR(data->kthread_vid_cap);
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct video_i2c_data *data = vb2_get_drv_priv(vq);

	if (data->kthread_vid_cap == NULL)
		return;

	kthread_stop(data->kthread_vid_cap);
	data->kthread_vid_cap = NULL;

	video_i2c_del_list(vq, VB2_BUF_STATE_ERROR);
}

static struct vb2_ops video_i2c_video_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int video_i2c_querycap(struct file *file, void  *priv,
				struct v4l2_capability *vcap)
{
	struct video_i2c_data *data = video_drvdata(file);
	struct i2c_client *client = data->client;

	strlcpy(vcap->driver, data->v4l2_dev.name, sizeof(vcap->driver));
	strlcpy(vcap->card, data->vdev.name, sizeof(vcap->card));

	sprintf(vcap->bus_info, "I2C:%d-%d", client->adapter->nr, client->addr);

	return 0;
}

static int video_i2c_g_input(struct file *file, void *fh, unsigned int *inp)
{
	*inp = 0;

	return 0;
}

static int video_i2c_s_input(struct file *file, void *fh, unsigned int inp)
{
	return (inp > 0) ? -EINVAL : 0;
}

static int video_i2c_enum_input(struct file *file, void *fh,
				  struct v4l2_input *vin)
{
	if (vin->index > 0)
		return -EINVAL;

	strlcpy(vin->name, "Camera", sizeof(vin->name));

	vin->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int video_i2c_enum_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_fmtdesc *fmt)
{
	struct video_i2c_data *data = video_drvdata(file);
	enum v4l2_buf_type type = fmt->type;

	if (fmt->index > 0)
		return -EINVAL;

	*fmt = *data->chip->format;
	fmt->type = type;

	return 0;
}

static int video_i2c_enum_framesizes(struct file *file, void *fh,
				       struct v4l2_frmsizeenum *fsize)
{
	const struct video_i2c_data *data = video_drvdata(file);
	const struct v4l2_frmsize_discrete *size = data->chip->size;

	/* currently only one frame size is allowed */
	if (fsize->index > 0)
		return -EINVAL;

	if (fsize->pixel_format != data->chip->format->pixelformat)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = size->width;
	fsize->discrete.height = size->height;

	return 0;
}

static int video_i2c_enum_frameintervals(struct file *file, void *priv,
					   struct v4l2_frmivalenum *fe)
{
	const struct video_i2c_data *data = video_drvdata(file);
	const struct v4l2_frmsize_discrete *size = data->chip->size;

	if (fe->index > 0)
		return -EINVAL;

	if (fe->width != size->width || fe->height != size->height)
		return -EINVAL;

	fe->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fe->discrete.numerator = 1;
	fe->discrete.denominator = data->chip->max_fps;

	return 0;
}

static int video_i2c_try_fmt_vid_cap(struct file *file, void *fh,
				       struct v4l2_format *fmt)
{
	const struct video_i2c_data *data = video_drvdata(file);
	const struct v4l2_frmsize_discrete *size = data->chip->size;
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	unsigned int bpp = data->chip->bpp / 8;

	pix->width = size->width;
	pix->height = size->height;
	pix->pixelformat = data->chip->format->pixelformat;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * bpp;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = V4L2_COLORSPACE_RAW;

	return 0;
}

static int video_i2c_s_fmt_vid_cap(struct file *file, void *fh,
				     struct v4l2_format *fmt)
{
	struct video_i2c_data *data = video_drvdata(file);

	if (vb2_is_busy(&data->vb_vidq))
		return -EBUSY;

	return video_i2c_try_fmt_vid_cap(file, fh, fmt);
}

static int video_i2c_g_parm(struct file *filp, void *priv,
			      struct v4l2_streamparm *parm)
{
	struct video_i2c_data *data = video_drvdata(filp);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	parm->parm.capture.readbuffers = 1;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe.numerator = 1;
	parm->parm.capture.timeperframe.denominator = data->chip->max_fps;

	return 0;
}

static const struct v4l2_ioctl_ops video_i2c_ioctl_ops = {
	.vidioc_querycap		= video_i2c_querycap,
	.vidioc_g_input			= video_i2c_g_input,
	.vidioc_s_input			= video_i2c_s_input,
	.vidioc_enum_input		= video_i2c_enum_input,
	.vidioc_enum_fmt_vid_cap	= video_i2c_enum_fmt_vid_cap,
	.vidioc_enum_framesizes		= video_i2c_enum_framesizes,
	.vidioc_enum_frameintervals	= video_i2c_enum_frameintervals,
	.vidioc_g_fmt_vid_cap		= video_i2c_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= video_i2c_s_fmt_vid_cap,
	.vidioc_g_parm			= video_i2c_g_parm,
	.vidioc_s_parm			= video_i2c_g_parm,
	.vidioc_try_fmt_vid_cap		= video_i2c_try_fmt_vid_cap,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

static void video_i2c_release(struct video_device *vdev)
{
	struct video_i2c_data *data = video_get_drvdata(vdev);

	v4l2_device_unregister(&data->v4l2_dev);
	mutex_destroy(&data->lock);
	mutex_destroy(&data->queue_lock);
	kfree(data);
}

static int video_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct video_i2c_data *data;
	struct v4l2_device *v4l2_dev;
	struct vb2_queue *queue;
	int ret = -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (dev_fwnode(&client->dev))
		data->chip = device_get_match_data(&client->dev);
	else if (id)
		data->chip = &video_i2c_chip[id->driver_data];
	else
		goto error_free_device;

	data->client = client;
	v4l2_dev = &data->v4l2_dev;
	strlcpy(v4l2_dev->name, VIDEO_I2C_DRIVER, sizeof(v4l2_dev->name));

	ret = v4l2_device_register(&client->dev, v4l2_dev);
	if (ret < 0)
		goto error_free_device;

	mutex_init(&data->lock);
	mutex_init(&data->queue_lock);

	queue = &data->vb_vidq;
	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_DMABUF | VB2_MMAP | VB2_USERPTR | VB2_READ;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->drv_priv = data;
	queue->buf_struct_size = sizeof(struct video_i2c_buffer);
	queue->min_buffers_needed = 1;
	queue->ops = &video_i2c_video_qops;
	queue->mem_ops = &vb2_vmalloc_memops;

	ret = vb2_queue_init(queue);
	if (ret < 0)
		goto error_unregister_device;

	data->vdev.queue = queue;
	data->vdev.queue->lock = &data->queue_lock;

	snprintf(data->vdev.name, sizeof(data->vdev.name),
				 "I2C %d-%d Transport Video",
				 client->adapter->nr, client->addr);

	data->vdev.v4l2_dev = v4l2_dev;
	data->vdev.fops = &video_i2c_fops;
	data->vdev.lock = &data->lock;
	data->vdev.ioctl_ops = &video_i2c_ioctl_ops;
	data->vdev.release = video_i2c_release;
	data->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE |
				 V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;

	spin_lock_init(&data->slock);
	INIT_LIST_HEAD(&data->vid_cap_active);

	video_set_drvdata(&data->vdev, data);
	i2c_set_clientdata(client, data);

	if (data->chip->hwmon_init) {
		ret = data->chip->hwmon_init(data);
		if (ret < 0) {
			dev_warn(&client->dev,
				 "failed to register hwmon device\n");
		}
	}

	ret = video_register_device(&data->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		goto error_unregister_device;

	return 0;

error_unregister_device:
	v4l2_device_unregister(v4l2_dev);
	mutex_destroy(&data->lock);
	mutex_destroy(&data->queue_lock);

error_free_device:
	kfree(data);

	return ret;
}

static int video_i2c_remove(struct i2c_client *client)
{
	struct video_i2c_data *data = i2c_get_clientdata(client);

	video_unregister_device(&data->vdev);

	return 0;
}

static const struct i2c_device_id video_i2c_id_table[] = {
	{ "amg88xx", AMG88XX },
	{}
};
MODULE_DEVICE_TABLE(i2c, video_i2c_id_table);

static const struct of_device_id video_i2c_of_match[] = {
	{ .compatible = "panasonic,amg88xx", .data = &video_i2c_chip[AMG88XX] },
	{}
};
MODULE_DEVICE_TABLE(of, video_i2c_of_match);

static struct i2c_driver video_i2c_driver = {
	.driver = {
		.name	= VIDEO_I2C_DRIVER,
		.of_match_table = video_i2c_of_match,
	},
	.probe		= video_i2c_probe,
	.remove		= video_i2c_remove,
	.id_table	= video_i2c_id_table,
};

module_i2c_driver(video_i2c_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("I2C transport video support");
MODULE_LICENSE("GPL v2");
