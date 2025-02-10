// SPDX-License-Identifier: GPL-2.0
/*
 * video-i2c.c - Support for I2C transport video devices
 *
 * Copyright (C) 2018 Matt Ranostay <matt.ranostay@konsulko.com>
 *
 * Supported:
 * - Panasonic AMG88xx Grid-Eye Sensors
 * - Melexis MLX90640 Thermal Cameras
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/hwmon.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>
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

/* Power control register */
#define AMG88XX_REG_PCTL	0x00
#define AMG88XX_PCTL_NORMAL		0x00
#define AMG88XX_PCTL_SLEEP		0x10

/* Reset register */
#define AMG88XX_REG_RST		0x01
#define AMG88XX_RST_FLAG		0x30
#define AMG88XX_RST_INIT		0x3f

/* Frame rate register */
#define AMG88XX_REG_FPSC	0x02
#define AMG88XX_FPSC_1FPS		BIT(0)

/* Thermistor register */
#define AMG88XX_REG_TTHL	0x0e

/* Temperature register */
#define AMG88XX_REG_T01L	0x80

/* RAM */
#define MLX90640_RAM_START_ADDR		0x0400

/* EEPROM */
#define MLX90640_EEPROM_START_ADDR	0x2400

/* Control register */
#define MLX90640_REG_CTL1		0x800d
#define MLX90640_REG_CTL1_MASK		GENMASK(9, 7)
#define MLX90640_REG_CTL1_MASK_SHIFT	7

struct video_i2c_chip;

struct video_i2c_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct video_i2c_data {
	struct regmap *regmap;
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

	struct v4l2_fract frame_interval;
};

static const struct v4l2_fmtdesc amg88xx_format = {
	.pixelformat = V4L2_PIX_FMT_Y12,
};

static const struct v4l2_frmsize_discrete amg88xx_size = {
	.width = 8,
	.height = 8,
};

static const struct v4l2_fmtdesc mlx90640_format = {
	.pixelformat = V4L2_PIX_FMT_Y16_BE,
};

static const struct v4l2_frmsize_discrete mlx90640_size = {
	.width = 32,
	.height = 26, /* 24 lines of pixel data + 2 lines of processing data */
};

static const struct regmap_config amg88xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff
};

static const struct regmap_config mlx90640_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
};

struct video_i2c_chip {
	/* video dimensions */
	const struct v4l2_fmtdesc *format;
	const struct v4l2_frmsize_discrete *size;

	/* available frame intervals */
	const struct v4l2_fract *frame_intervals;
	unsigned int num_frame_intervals;

	/* pixel buffer size */
	unsigned int buffer_size;

	/* pixel size in bits */
	unsigned int bpp;

	const struct regmap_config *regmap_config;
	struct nvmem_config *nvmem_config;

	/* setup function */
	int (*setup)(struct video_i2c_data *data);

	/* xfer function */
	int (*xfer)(struct video_i2c_data *data, char *buf);

	/* power control function */
	int (*set_power)(struct video_i2c_data *data, bool on);

	/* hwmon init function */
	int (*hwmon_init)(struct video_i2c_data *data);
};

static int mlx90640_nvram_read(void *priv, unsigned int offset, void *val,
			     size_t bytes)
{
	struct video_i2c_data *data = priv;

	return regmap_bulk_read(data->regmap, MLX90640_EEPROM_START_ADDR + offset, val, bytes);
}

static struct nvmem_config mlx90640_nvram_config = {
	.name = "mlx90640_nvram",
	.word_size = 2,
	.stride = 1,
	.size = 1664,
	.reg_read = mlx90640_nvram_read,
};

static int amg88xx_xfer(struct video_i2c_data *data, char *buf)
{
	return regmap_bulk_read(data->regmap, AMG88XX_REG_T01L, buf,
				data->chip->buffer_size);
}

static int mlx90640_xfer(struct video_i2c_data *data, char *buf)
{
	return regmap_bulk_read(data->regmap, MLX90640_RAM_START_ADDR, buf,
				data->chip->buffer_size);
}

static int amg88xx_setup(struct video_i2c_data *data)
{
	unsigned int mask = AMG88XX_FPSC_1FPS;
	unsigned int val;

	if (data->frame_interval.numerator == data->frame_interval.denominator)
		val = mask;
	else
		val = 0;

	return regmap_update_bits(data->regmap, AMG88XX_REG_FPSC, mask, val);
}

static int mlx90640_setup(struct video_i2c_data *data)
{
	unsigned int n, idx;

	for (n = 0; n < data->chip->num_frame_intervals - 1; n++) {
		if (V4L2_FRACT_COMPARE(data->frame_interval, ==,
				       data->chip->frame_intervals[n]))
			break;
	}

	idx = data->chip->num_frame_intervals - n - 1;

	return regmap_update_bits(data->regmap, MLX90640_REG_CTL1,
				  MLX90640_REG_CTL1_MASK,
				  idx << MLX90640_REG_CTL1_MASK_SHIFT);
}

static int amg88xx_set_power_on(struct video_i2c_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, AMG88XX_REG_PCTL, AMG88XX_PCTL_NORMAL);
	if (ret)
		return ret;

	msleep(50);

	ret = regmap_write(data->regmap, AMG88XX_REG_RST, AMG88XX_RST_INIT);
	if (ret)
		return ret;

	usleep_range(2000, 3000);

	ret = regmap_write(data->regmap, AMG88XX_REG_RST, AMG88XX_RST_FLAG);
	if (ret)
		return ret;

	/*
	 * Wait two frames before reading thermistor and temperature registers
	 */
	msleep(200);

	return 0;
}

static int amg88xx_set_power_off(struct video_i2c_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, AMG88XX_REG_PCTL, AMG88XX_PCTL_SLEEP);
	if (ret)
		return ret;
	/*
	 * Wait for a while to avoid resuming normal mode immediately after
	 * entering sleep mode, otherwise the device occasionally goes wrong
	 * (thermistor and temperature registers are not updated at all)
	 */
	msleep(100);

	return 0;
}

static int amg88xx_set_power(struct video_i2c_data *data, bool on)
{
	if (on)
		return amg88xx_set_power_on(data);

	return amg88xx_set_power_off(data);
}

#if IS_REACHABLE(CONFIG_HWMON)

static const struct hwmon_channel_info * const amg88xx_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
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
	__le16 buf;
	int tmp;

	tmp = pm_runtime_resume_and_get(regmap_get_device(data->regmap));
	if (tmp < 0)
		return tmp;

	tmp = regmap_bulk_read(data->regmap, AMG88XX_REG_TTHL, &buf, 2);
	pm_runtime_mark_last_busy(regmap_get_device(data->regmap));
	pm_runtime_put_autosuspend(regmap_get_device(data->regmap));
	if (tmp)
		return tmp;

	tmp = le16_to_cpu(buf);

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
	struct device *dev = regmap_get_device(data->regmap);
	void *hwmon = devm_hwmon_device_register_with_info(dev, "amg88xx", data,
						&amg88xx_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon);
}
#else
#define	amg88xx_hwmon_init	NULL
#endif

enum {
	AMG88XX,
	MLX90640,
};

static const struct v4l2_fract amg88xx_frame_intervals[] = {
	{ 1, 10 },
	{ 1, 1 },
};

static const struct v4l2_fract mlx90640_frame_intervals[] = {
	{ 1, 64 },
	{ 1, 32 },
	{ 1, 16 },
	{ 1, 8 },
	{ 1, 4 },
	{ 1, 2 },
	{ 1, 1 },
	{ 2, 1 },
};

static const struct video_i2c_chip video_i2c_chip[] = {
	[AMG88XX] = {
		.size		= &amg88xx_size,
		.format		= &amg88xx_format,
		.frame_intervals	= amg88xx_frame_intervals,
		.num_frame_intervals	= ARRAY_SIZE(amg88xx_frame_intervals),
		.buffer_size	= 128,
		.bpp		= 16,
		.regmap_config	= &amg88xx_regmap_config,
		.setup		= &amg88xx_setup,
		.xfer		= &amg88xx_xfer,
		.set_power	= amg88xx_set_power,
		.hwmon_init	= amg88xx_hwmon_init,
	},
	[MLX90640] = {
		.size		= &mlx90640_size,
		.format		= &mlx90640_format,
		.frame_intervals	= mlx90640_frame_intervals,
		.num_frame_intervals	= ARRAY_SIZE(mlx90640_frame_intervals),
		.buffer_size	= 1664,
		.bpp		= 16,
		.regmap_config	= &mlx90640_regmap_config,
		.nvmem_config	= &mlx90640_nvram_config,
		.setup		= mlx90640_setup,
		.xfer		= mlx90640_xfer,
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
	unsigned int q_num_bufs = vb2_get_num_buffers(vq);

	if (q_num_bufs + *nbuffers < 2)
		*nbuffers = 2 - q_num_bufs;

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
	u32 delay = mult_frac(1000000UL, data->frame_interval.numerator,
			       data->frame_interval.denominator);
	s64 end_us = ktime_to_us(ktime_get());

	set_freezable();

	do {
		struct video_i2c_buffer *vid_cap_buf = NULL;
		s64 current_us;
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

		end_us += delay;
		current_us = ktime_to_us(ktime_get());
		if (current_us < end_us) {
			schedule_delay = end_us - current_us;
			usleep_range(schedule_delay * 3 / 4, schedule_delay);
		} else {
			end_us = current_us;
		}
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
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	if (data->kthread_vid_cap)
		return 0;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto error_del_list;

	ret = data->chip->setup(data);
	if (ret)
		goto error_rpm_put;

	data->sequence = 0;
	data->kthread_vid_cap = kthread_run(video_i2c_thread_vid_cap, data,
					    "%s-vid-cap", data->v4l2_dev.name);
	ret = PTR_ERR_OR_ZERO(data->kthread_vid_cap);
	if (!ret)
		return 0;

error_rpm_put:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
error_del_list:
	video_i2c_del_list(vq, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct video_i2c_data *data = vb2_get_drv_priv(vq);

	if (data->kthread_vid_cap == NULL)
		return;

	kthread_stop(data->kthread_vid_cap);
	data->kthread_vid_cap = NULL;
	pm_runtime_mark_last_busy(regmap_get_device(data->regmap));
	pm_runtime_put_autosuspend(regmap_get_device(data->regmap));

	video_i2c_del_list(vq, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops video_i2c_video_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
};

static int video_i2c_querycap(struct file *file, void  *priv,
				struct v4l2_capability *vcap)
{
	struct video_i2c_data *data = video_drvdata(file);
	struct device *dev = regmap_get_device(data->regmap);
	struct i2c_client *client = to_i2c_client(dev);

	strscpy(vcap->driver, data->v4l2_dev.name, sizeof(vcap->driver));
	strscpy(vcap->card, data->vdev.name, sizeof(vcap->card));

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

	strscpy(vin->name, "Camera", sizeof(vin->name));

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

	if (fe->index >= data->chip->num_frame_intervals)
		return -EINVAL;

	if (fe->width != size->width || fe->height != size->height)
		return -EINVAL;

	fe->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fe->discrete = data->chip->frame_intervals[fe->index];

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
	parm->parm.capture.timeperframe = data->frame_interval;

	return 0;
}

static int video_i2c_s_parm(struct file *filp, void *priv,
			      struct v4l2_streamparm *parm)
{
	struct video_i2c_data *data = video_drvdata(filp);
	int i;

	for (i = 0; i < data->chip->num_frame_intervals - 1; i++) {
		if (V4L2_FRACT_COMPARE(parm->parm.capture.timeperframe, <=,
				       data->chip->frame_intervals[i]))
			break;
	}
	data->frame_interval = data->chip->frame_intervals[i];

	return video_i2c_g_parm(filp, priv, parm);
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
	.vidioc_s_parm			= video_i2c_s_parm,
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
	regmap_exit(data->regmap);
	kfree(data);
}

static int video_i2c_probe(struct i2c_client *client)
{
	struct video_i2c_data *data;
	struct v4l2_device *v4l2_dev;
	struct vb2_queue *queue;
	int ret = -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip = i2c_get_match_data(client);
	if (!data->chip)
		goto error_free_device;

	data->regmap = regmap_init_i2c(client, data->chip->regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		goto error_free_device;
	}

	v4l2_dev = &data->v4l2_dev;
	strscpy(v4l2_dev->name, VIDEO_I2C_DRIVER, sizeof(v4l2_dev->name));

	ret = v4l2_device_register(&client->dev, v4l2_dev);
	if (ret < 0)
		goto error_regmap_exit;

	mutex_init(&data->lock);
	mutex_init(&data->queue_lock);

	queue = &data->vb_vidq;
	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_DMABUF | VB2_MMAP | VB2_USERPTR | VB2_READ;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->drv_priv = data;
	queue->buf_struct_size = sizeof(struct video_i2c_buffer);
	queue->min_queued_buffers = 1;
	queue->ops = &video_i2c_video_qops;
	queue->mem_ops = &vb2_vmalloc_memops;
	queue->lock = &data->queue_lock;

	ret = vb2_queue_init(queue);
	if (ret < 0)
		goto error_unregister_device;

	data->vdev.queue = queue;

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

	data->frame_interval = data->chip->frame_intervals[0];

	video_set_drvdata(&data->vdev, data);
	i2c_set_clientdata(client, data);

	if (data->chip->set_power) {
		ret = data->chip->set_power(data, true);
		if (ret)
			goto error_unregister_device;
	}

	pm_runtime_get_noresume(&client->dev);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, 2000);
	pm_runtime_use_autosuspend(&client->dev);

	if (data->chip->hwmon_init) {
		ret = data->chip->hwmon_init(data);
		if (ret < 0) {
			dev_warn(&client->dev,
				 "failed to register hwmon device\n");
		}
	}

	if (data->chip->nvmem_config) {
		struct nvmem_config *config = data->chip->nvmem_config;
		struct nvmem_device *device;

		config->priv = data;
		config->dev = &client->dev;

		device = devm_nvmem_register(&client->dev, config);

		if (IS_ERR(device)) {
			dev_warn(&client->dev,
				 "failed to register nvmem device\n");
		}
	}

	ret = video_register_device(&data->vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0)
		goto error_pm_disable;

	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return 0;

error_pm_disable:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	if (data->chip->set_power)
		data->chip->set_power(data, false);

error_unregister_device:
	v4l2_device_unregister(v4l2_dev);
	mutex_destroy(&data->lock);
	mutex_destroy(&data->queue_lock);

error_regmap_exit:
	regmap_exit(data->regmap);

error_free_device:
	kfree(data);

	return ret;
}

static void video_i2c_remove(struct i2c_client *client)
{
	struct video_i2c_data *data = i2c_get_clientdata(client);

	pm_runtime_get_sync(&client->dev);
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	if (data->chip->set_power)
		data->chip->set_power(data, false);

	video_unregister_device(&data->vdev);
}

#ifdef CONFIG_PM

static int video_i2c_pm_runtime_suspend(struct device *dev)
{
	struct video_i2c_data *data = i2c_get_clientdata(to_i2c_client(dev));

	if (!data->chip->set_power)
		return 0;

	return data->chip->set_power(data, false);
}

static int video_i2c_pm_runtime_resume(struct device *dev)
{
	struct video_i2c_data *data = i2c_get_clientdata(to_i2c_client(dev));

	if (!data->chip->set_power)
		return 0;

	return data->chip->set_power(data, true);
}

#endif

static const struct dev_pm_ops video_i2c_pm_ops = {
	SET_RUNTIME_PM_OPS(video_i2c_pm_runtime_suspend,
			   video_i2c_pm_runtime_resume, NULL)
};

static const struct i2c_device_id video_i2c_id_table[] = {
	{ "amg88xx", (kernel_ulong_t)&video_i2c_chip[AMG88XX] },
	{ "mlx90640", (kernel_ulong_t)&video_i2c_chip[MLX90640] },
	{}
};
MODULE_DEVICE_TABLE(i2c, video_i2c_id_table);

static const struct of_device_id video_i2c_of_match[] = {
	{ .compatible = "panasonic,amg88xx", .data = &video_i2c_chip[AMG88XX] },
	{ .compatible = "melexis,mlx90640", .data = &video_i2c_chip[MLX90640] },
	{}
};
MODULE_DEVICE_TABLE(of, video_i2c_of_match);

static struct i2c_driver video_i2c_driver = {
	.driver = {
		.name	= VIDEO_I2C_DRIVER,
		.of_match_table = video_i2c_of_match,
		.pm	= &video_i2c_pm_ops,
	},
	.probe		= video_i2c_probe,
	.remove		= video_i2c_remove,
	.id_table	= video_i2c_id_table,
};

module_i2c_driver(video_i2c_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("I2C transport video support");
MODULE_LICENSE("GPL v2");
