/*
 * camera image capture (abstract) bus driver
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This driver provides an interface between platform-specific camera
 * busses and camera devices. It should be used if the camera is
 * connected not over a "proper" bus like PCI or USB, but over a
 * special bus, like, for example, the Quick Capture interface on PXA270
 * SoCs. Later it should also be used for i.MX31 SoCs from Freescale.
 * It can handle multiple cameras and / or multiple busses, which can
 * be used, e.g., in stereo-vision applications.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/v4l2-async.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-of.h>
#include <media/videobuf-core.h>
#include <media/videobuf2-core.h>

/* Default to VGA resolution */
#define DEFAULT_WIDTH	640
#define DEFAULT_HEIGHT	480

#define is_streaming(ici, icd)				\
	(((ici)->ops->init_videobuf) ?			\
	 (icd)->vb_vidq.streaming :			\
	 vb2_is_streaming(&(icd)->vb2_vidq))

#define MAP_MAX_NUM 32
static DECLARE_BITMAP(device_map, MAP_MAX_NUM);
static LIST_HEAD(hosts);
static LIST_HEAD(devices);
/*
 * Protects lists and bitmaps of hosts and devices.
 * Lock nesting: Ok to take ->host_lock under list_lock.
 */
static DEFINE_MUTEX(list_lock);

struct soc_camera_async_client {
	struct v4l2_async_subdev *sensor;
	struct v4l2_async_notifier notifier;
	struct platform_device *pdev;
	struct list_head list;		/* needed for clean up */
};

static int soc_camera_video_start(struct soc_camera_device *icd);
static int video_dev_create(struct soc_camera_device *icd);

int soc_camera_power_on(struct device *dev, struct soc_camera_subdev_desc *ssdd,
			struct v4l2_clk *clk)
{
	int ret;
	bool clock_toggle;

	if (clk && (!ssdd->unbalanced_power ||
		    !test_and_set_bit(0, &ssdd->clock_state))) {
		ret = v4l2_clk_enable(clk);
		if (ret < 0) {
			dev_err(dev, "Cannot enable clock: %d\n", ret);
			return ret;
		}
		clock_toggle = true;
	} else {
		clock_toggle = false;
	}

	ret = regulator_bulk_enable(ssdd->sd_pdata.num_regulators,
				    ssdd->sd_pdata.regulators);
	if (ret < 0) {
		dev_err(dev, "Cannot enable regulators\n");
		goto eregenable;
	}

	if (ssdd->power) {
		ret = ssdd->power(dev, 1);
		if (ret < 0) {
			dev_err(dev,
				"Platform failed to power-on the camera.\n");
			goto epwron;
		}
	}

	return 0;

epwron:
	regulator_bulk_disable(ssdd->sd_pdata.num_regulators,
			       ssdd->sd_pdata.regulators);
eregenable:
	if (clock_toggle)
		v4l2_clk_disable(clk);

	return ret;
}
EXPORT_SYMBOL(soc_camera_power_on);

int soc_camera_power_off(struct device *dev, struct soc_camera_subdev_desc *ssdd,
			 struct v4l2_clk *clk)
{
	int ret = 0;
	int err;

	if (ssdd->power) {
		err = ssdd->power(dev, 0);
		if (err < 0) {
			dev_err(dev,
				"Platform failed to power-off the camera.\n");
			ret = err;
		}
	}

	err = regulator_bulk_disable(ssdd->sd_pdata.num_regulators,
				     ssdd->sd_pdata.regulators);
	if (err < 0) {
		dev_err(dev, "Cannot disable regulators\n");
		ret = ret ? : err;
	}

	if (clk && (!ssdd->unbalanced_power || test_and_clear_bit(0, &ssdd->clock_state)))
		v4l2_clk_disable(clk);

	return ret;
}
EXPORT_SYMBOL(soc_camera_power_off);

int soc_camera_power_init(struct device *dev, struct soc_camera_subdev_desc *ssdd)
{
	/* Should not have any effect in synchronous case */
	return devm_regulator_bulk_get(dev, ssdd->sd_pdata.num_regulators,
				       ssdd->sd_pdata.regulators);
}
EXPORT_SYMBOL(soc_camera_power_init);

static int __soc_camera_power_on(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	int ret;

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	return 0;
}

static int __soc_camera_power_off(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	int ret;

	ret = v4l2_subdev_call(sd, core, s_power, 0);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	return 0;
}

const struct soc_camera_format_xlate *soc_camera_xlate_by_fourcc(
	struct soc_camera_device *icd, unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < icd->num_user_formats; i++)
		if (icd->user_formats[i].host_fmt->fourcc == fourcc)
			return icd->user_formats + i;
	return NULL;
}
EXPORT_SYMBOL(soc_camera_xlate_by_fourcc);

/**
 * soc_camera_apply_board_flags() - apply platform SOCAM_SENSOR_INVERT_* flags
 * @ssdd:	camera platform parameters
 * @cfg:	media bus configuration
 * @return:	resulting flags
 */
unsigned long soc_camera_apply_board_flags(struct soc_camera_subdev_desc *ssdd,
					   const struct v4l2_mbus_config *cfg)
{
	unsigned long f, flags = cfg->flags;

	/* If only one of the two polarities is supported, switch to the opposite */
	if (ssdd->flags & SOCAM_SENSOR_INVERT_HSYNC) {
		f = flags & (V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_LOW);
		if (f == V4L2_MBUS_HSYNC_ACTIVE_HIGH || f == V4L2_MBUS_HSYNC_ACTIVE_LOW)
			flags ^= V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_LOW;
	}

	if (ssdd->flags & SOCAM_SENSOR_INVERT_VSYNC) {
		f = flags & (V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_LOW);
		if (f == V4L2_MBUS_VSYNC_ACTIVE_HIGH || f == V4L2_MBUS_VSYNC_ACTIVE_LOW)
			flags ^= V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_LOW;
	}

	if (ssdd->flags & SOCAM_SENSOR_INVERT_PCLK) {
		f = flags & (V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_PCLK_SAMPLE_FALLING);
		if (f == V4L2_MBUS_PCLK_SAMPLE_RISING || f == V4L2_MBUS_PCLK_SAMPLE_FALLING)
			flags ^= V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_PCLK_SAMPLE_FALLING;
	}

	return flags;
}
EXPORT_SYMBOL(soc_camera_apply_board_flags);

#define pixfmtstr(x) (x) & 0xff, ((x) >> 8) & 0xff, ((x) >> 16) & 0xff, \
	((x) >> 24) & 0xff

static int soc_camera_try_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	dev_dbg(icd->pdev, "TRY_FMT(%c%c%c%c, %ux%u)\n",
		pixfmtstr(pix->pixelformat), pix->width, pix->height);

	if (pix->pixelformat != V4L2_PIX_FMT_JPEG &&
	    !(ici->capabilities & SOCAM_HOST_CAP_STRIDE)) {
		pix->bytesperline = 0;
		pix->sizeimage = 0;
	}

	ret = ici->ops->try_fmt(icd, f);
	if (ret < 0)
		return ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate)
		return -EINVAL;

	ret = soc_mbus_bytes_per_line(pix->width, xlate->host_fmt);
	if (ret < 0)
		return ret;

	pix->bytesperline = max_t(u32, pix->bytesperline, ret);

	ret = soc_mbus_image_size(xlate->host_fmt, pix->bytesperline,
				  pix->height);
	if (ret < 0)
		return ret;

	pix->sizeimage = max_t(u32, pix->sizeimage, ret);

	return 0;
}

static int soc_camera_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct soc_camera_device *icd = file->private_data;

	WARN_ON(priv != file->private_data);

	/* Only single-plane capture is supported so far */
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* limit format to hardware capabilities */
	return soc_camera_try_fmt(icd, f);
}

static int soc_camera_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	/* default is camera */
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strcpy(inp->name, "Camera");

	return 0;
}

static int soc_camera_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int soc_camera_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

static int soc_camera_s_std(struct file *file, void *priv, v4l2_std_id a)
{
	struct soc_camera_device *icd = file->private_data;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, s_std, a);
}

static int soc_camera_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct soc_camera_device *icd = file->private_data;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, g_std, a);
}

static int soc_camera_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *fsize)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	return ici->ops->enum_framesizes(icd, fsize);
}

static int soc_camera_reqbufs(struct file *file, void *priv,
			      struct v4l2_requestbuffers *p)
{
	int ret;
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	WARN_ON(priv != file->private_data);

	if (icd->streamer && icd->streamer != file)
		return -EBUSY;

	if (ici->ops->init_videobuf) {
		ret = videobuf_reqbufs(&icd->vb_vidq, p);
		if (ret < 0)
			return ret;

		ret = ici->ops->reqbufs(icd, p);
	} else {
		ret = vb2_reqbufs(&icd->vb2_vidq, p);
	}

	if (!ret && !icd->streamer)
		icd->streamer = file;

	return ret;
}

static int soc_camera_querybuf(struct file *file, void *priv,
			       struct v4l2_buffer *p)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	WARN_ON(priv != file->private_data);

	if (ici->ops->init_videobuf)
		return videobuf_querybuf(&icd->vb_vidq, p);
	else
		return vb2_querybuf(&icd->vb2_vidq, p);
}

static int soc_camera_qbuf(struct file *file, void *priv,
			   struct v4l2_buffer *p)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	WARN_ON(priv != file->private_data);

	if (icd->streamer != file)
		return -EBUSY;

	if (ici->ops->init_videobuf)
		return videobuf_qbuf(&icd->vb_vidq, p);
	else
		return vb2_qbuf(&icd->vb2_vidq, p);
}

static int soc_camera_dqbuf(struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	WARN_ON(priv != file->private_data);

	if (icd->streamer != file)
		return -EBUSY;

	if (ici->ops->init_videobuf)
		return videobuf_dqbuf(&icd->vb_vidq, p, file->f_flags & O_NONBLOCK);
	else
		return vb2_dqbuf(&icd->vb2_vidq, p, file->f_flags & O_NONBLOCK);
}

static int soc_camera_create_bufs(struct file *file, void *priv,
			    struct v4l2_create_buffers *create)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	/* videobuf2 only */
	if (ici->ops->init_videobuf)
		return -EINVAL;
	else
		return vb2_create_bufs(&icd->vb2_vidq, create);
}

static int soc_camera_prepare_buf(struct file *file, void *priv,
				  struct v4l2_buffer *b)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	/* videobuf2 only */
	if (ici->ops->init_videobuf)
		return -EINVAL;
	else
		return vb2_prepare_buf(&icd->vb2_vidq, b);
}

static int soc_camera_expbuf(struct file *file, void *priv,
			     struct v4l2_exportbuffer *p)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	if (icd->streamer != file)
		return -EBUSY;

	/* videobuf2 only */
	if (ici->ops->init_videobuf)
		return -EINVAL;
	else
		return vb2_expbuf(&icd->vb2_vidq, p);
}

/* Always entered with .host_lock held */
static int soc_camera_init_user_formats(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	unsigned int i, fmts = 0, raw_fmts = 0;
	int ret;
	u32 code;

	while (!v4l2_subdev_call(sd, video, enum_mbus_fmt, raw_fmts, &code))
		raw_fmts++;

	if (!ici->ops->get_formats)
		/*
		 * Fallback mode - the host will have to serve all
		 * sensor-provided formats one-to-one to the user
		 */
		fmts = raw_fmts;
	else
		/*
		 * First pass - only count formats this host-sensor
		 * configuration can provide
		 */
		for (i = 0; i < raw_fmts; i++) {
			ret = ici->ops->get_formats(icd, i, NULL);
			if (ret < 0)
				return ret;
			fmts += ret;
		}

	if (!fmts)
		return -ENXIO;

	icd->user_formats =
		vmalloc(fmts * sizeof(struct soc_camera_format_xlate));
	if (!icd->user_formats)
		return -ENOMEM;

	dev_dbg(icd->pdev, "Found %d supported formats.\n", fmts);

	/* Second pass - actually fill data formats */
	fmts = 0;
	for (i = 0; i < raw_fmts; i++)
		if (!ici->ops->get_formats) {
			v4l2_subdev_call(sd, video, enum_mbus_fmt, i, &code);
			icd->user_formats[fmts].host_fmt =
				soc_mbus_get_fmtdesc(code);
			if (icd->user_formats[fmts].host_fmt)
				icd->user_formats[fmts++].code = code;
		} else {
			ret = ici->ops->get_formats(icd, i,
						    &icd->user_formats[fmts]);
			if (ret < 0)
				goto egfmt;
			fmts += ret;
		}

	icd->num_user_formats = fmts;
	icd->current_fmt = &icd->user_formats[0];

	return 0;

egfmt:
	vfree(icd->user_formats);
	return ret;
}

/* Always entered with .host_lock held */
static void soc_camera_free_user_formats(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	if (ici->ops->put_formats)
		ici->ops->put_formats(icd);
	icd->current_fmt = NULL;
	icd->num_user_formats = 0;
	vfree(icd->user_formats);
	icd->user_formats = NULL;
}

/* Called with .vb_lock held, or from the first open(2), see comment there */
static int soc_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	dev_dbg(icd->pdev, "S_FMT(%c%c%c%c, %ux%u)\n",
		pixfmtstr(pix->pixelformat), pix->width, pix->height);

	/* We always call try_fmt() before set_fmt() or set_crop() */
	ret = soc_camera_try_fmt(icd, f);
	if (ret < 0)
		return ret;

	ret = ici->ops->set_fmt(icd, f);
	if (ret < 0) {
		return ret;
	} else if (!icd->current_fmt ||
		   icd->current_fmt->host_fmt->fourcc != pix->pixelformat) {
		dev_err(icd->pdev,
			"Host driver hasn't set up current format correctly!\n");
		return -EINVAL;
	}

	icd->user_width		= pix->width;
	icd->user_height	= pix->height;
	icd->bytesperline	= pix->bytesperline;
	icd->sizeimage		= pix->sizeimage;
	icd->colorspace		= pix->colorspace;
	icd->field		= pix->field;
	if (ici->ops->init_videobuf)
		icd->vb_vidq.field = pix->field;

	dev_dbg(icd->pdev, "set width: %d height: %d\n",
		icd->user_width, icd->user_height);

	/* set physical bus parameters */
	return ici->ops->set_bus_param(icd);
}

static int soc_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	int ret;

	if (ici->icd)
		return -EBUSY;

	if (!icd->clk) {
		mutex_lock(&ici->clk_lock);
		ret = ici->ops->clock_start(ici);
		mutex_unlock(&ici->clk_lock);
		if (ret < 0)
			return ret;
	}

	if (ici->ops->add) {
		ret = ici->ops->add(icd);
		if (ret < 0)
			goto eadd;
	}

	ici->icd = icd;

	return 0;

eadd:
	if (!icd->clk) {
		mutex_lock(&ici->clk_lock);
		ici->ops->clock_stop(ici);
		mutex_unlock(&ici->clk_lock);
	}
	return ret;
}

static void soc_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	if (WARN_ON(icd != ici->icd))
		return;

	if (ici->ops->remove)
		ici->ops->remove(icd);
	if (!icd->clk) {
		mutex_lock(&ici->clk_lock);
		ici->ops->clock_stop(ici);
		mutex_unlock(&ici->clk_lock);
	}
	ici->icd = NULL;
}

static int soc_camera_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct soc_camera_device *icd;
	struct soc_camera_host *ici;
	int ret;

	/*
	 * Don't mess with the host during probe: wait until the loop in
	 * scan_add_host() completes. Also protect against a race with
	 * soc_camera_host_unregister().
	 */
	if (mutex_lock_interruptible(&list_lock))
		return -ERESTARTSYS;

	if (!vdev || !video_is_registered(vdev)) {
		mutex_unlock(&list_lock);
		return -ENODEV;
	}

	icd = video_get_drvdata(vdev);
	ici = to_soc_camera_host(icd->parent);

	ret = try_module_get(ici->ops->owner) ? 0 : -ENODEV;
	mutex_unlock(&list_lock);

	if (ret < 0) {
		dev_err(icd->pdev, "Couldn't lock capture bus driver.\n");
		return ret;
	}

	if (!to_soc_camera_control(icd)) {
		/* No device driver attached */
		ret = -ENODEV;
		goto econtrol;
	}

	if (mutex_lock_interruptible(&ici->host_lock)) {
		ret = -ERESTARTSYS;
		goto elockhost;
	}
	icd->use_count++;

	/* Now we really have to activate the camera */
	if (icd->use_count == 1) {
		struct soc_camera_desc *sdesc = to_soc_camera_desc(icd);
		/* Restore parameters before the last close() per V4L2 API */
		struct v4l2_format f = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.fmt.pix = {
				.width		= icd->user_width,
				.height		= icd->user_height,
				.field		= icd->field,
				.colorspace	= icd->colorspace,
				.pixelformat	=
					icd->current_fmt->host_fmt->fourcc,
			},
		};

		/* The camera could have been already on, try to reset */
		if (sdesc->subdev_desc.reset)
			sdesc->subdev_desc.reset(icd->pdev);

		ret = soc_camera_add_device(icd);
		if (ret < 0) {
			dev_err(icd->pdev, "Couldn't activate the camera: %d\n", ret);
			goto eiciadd;
		}

		ret = __soc_camera_power_on(icd);
		if (ret < 0)
			goto epower;

		pm_runtime_enable(&icd->vdev->dev);
		ret = pm_runtime_resume(&icd->vdev->dev);
		if (ret < 0 && ret != -ENOSYS)
			goto eresume;

		/*
		 * Try to configure with default parameters. Notice: this is the
		 * very first open, so, we cannot race against other calls,
		 * apart from someone else calling open() simultaneously, but
		 * .host_lock is protecting us against it.
		 */
		ret = soc_camera_set_fmt(icd, &f);
		if (ret < 0)
			goto esfmt;

		if (ici->ops->init_videobuf) {
			ici->ops->init_videobuf(&icd->vb_vidq, icd);
		} else {
			ret = ici->ops->init_videobuf2(&icd->vb2_vidq, icd);
			if (ret < 0)
				goto einitvb;
		}
		v4l2_ctrl_handler_setup(&icd->ctrl_handler);
	}
	mutex_unlock(&ici->host_lock);

	file->private_data = icd;
	dev_dbg(icd->pdev, "camera device open\n");

	return 0;

	/*
	 * All errors are entered with the .host_lock held, first four also
	 * with use_count == 1
	 */
einitvb:
esfmt:
	pm_runtime_disable(&icd->vdev->dev);
eresume:
	__soc_camera_power_off(icd);
epower:
	soc_camera_remove_device(icd);
eiciadd:
	icd->use_count--;
	mutex_unlock(&ici->host_lock);
elockhost:
econtrol:
	module_put(ici->ops->owner);

	return ret;
}

static int soc_camera_close(struct file *file)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	mutex_lock(&ici->host_lock);
	icd->use_count--;
	if (!icd->use_count) {
		pm_runtime_suspend(&icd->vdev->dev);
		pm_runtime_disable(&icd->vdev->dev);

		if (ici->ops->init_videobuf2)
			vb2_queue_release(&icd->vb2_vidq);
		__soc_camera_power_off(icd);

		soc_camera_remove_device(icd);
	}

	if (icd->streamer == file)
		icd->streamer = NULL;
	mutex_unlock(&ici->host_lock);

	module_put(ici->ops->owner);

	dev_dbg(icd->pdev, "camera device close\n");

	return 0;
}

static ssize_t soc_camera_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	dev_dbg(icd->pdev, "read called, buf %p\n", buf);

	if (ici->ops->init_videobuf2 && icd->vb2_vidq.io_modes & VB2_READ)
		return vb2_read(&icd->vb2_vidq, buf, count, ppos,
				file->f_flags & O_NONBLOCK);

	dev_err(icd->pdev, "camera device read not implemented\n");

	return -EINVAL;
}

static int soc_camera_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	int err;

	dev_dbg(icd->pdev, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	if (icd->streamer != file)
		return -EBUSY;

	if (mutex_lock_interruptible(&ici->host_lock))
		return -ERESTARTSYS;
	if (ici->ops->init_videobuf)
		err = videobuf_mmap_mapper(&icd->vb_vidq, vma);
	else
		err = vb2_mmap(&icd->vb2_vidq, vma);
	mutex_unlock(&ici->host_lock);

	dev_dbg(icd->pdev, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start,
		err);

	return err;
}

static unsigned int soc_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	unsigned res = POLLERR;

	if (icd->streamer != file)
		return POLLERR;

	mutex_lock(&ici->host_lock);
	if (ici->ops->init_videobuf && list_empty(&icd->vb_vidq.stream))
		dev_err(icd->pdev, "Trying to poll with no queued buffers!\n");
	else
		res = ici->ops->poll(file, pt);
	mutex_unlock(&ici->host_lock);
	return res;
}

static struct v4l2_file_operations soc_camera_fops = {
	.owner		= THIS_MODULE,
	.open		= soc_camera_open,
	.release	= soc_camera_close,
	.unlocked_ioctl	= video_ioctl2,
	.read		= soc_camera_read,
	.mmap		= soc_camera_mmap,
	.poll		= soc_camera_poll,
};

static int soc_camera_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct soc_camera_device *icd = file->private_data;
	int ret;

	WARN_ON(priv != file->private_data);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		dev_warn(icd->pdev, "Wrong buf-type %d\n", f->type);
		return -EINVAL;
	}

	if (icd->streamer && icd->streamer != file)
		return -EBUSY;

	if (is_streaming(to_soc_camera_host(icd->parent), icd)) {
		dev_err(icd->pdev, "S_FMT denied: queue initialised\n");
		return -EBUSY;
	}

	ret = soc_camera_set_fmt(icd, f);

	if (!ret && !icd->streamer)
		icd->streamer = file;

	return ret;
}

static int soc_camera_enum_fmt_vid_cap(struct file *file, void  *priv,
				       struct v4l2_fmtdesc *f)
{
	struct soc_camera_device *icd = file->private_data;
	const struct soc_mbus_pixelfmt *format;

	WARN_ON(priv != file->private_data);

	if (f->index >= icd->num_user_formats)
		return -EINVAL;

	format = icd->user_formats[f->index].host_fmt;

	if (format->name)
		strlcpy(f->description, format->name, sizeof(f->description));
	f->pixelformat = format->fourcc;
	return 0;
}

static int soc_camera_g_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct soc_camera_device *icd = file->private_data;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	WARN_ON(priv != file->private_data);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	pix->width		= icd->user_width;
	pix->height		= icd->user_height;
	pix->bytesperline	= icd->bytesperline;
	pix->sizeimage		= icd->sizeimage;
	pix->field		= icd->field;
	pix->pixelformat	= icd->current_fmt->host_fmt->fourcc;
	pix->colorspace		= icd->colorspace;
	dev_dbg(icd->pdev, "current_fmt->fourcc: 0x%08x\n",
		icd->current_fmt->host_fmt->fourcc);
	return 0;
}

static int soc_camera_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	WARN_ON(priv != file->private_data);

	strlcpy(cap->driver, ici->drv_name, sizeof(cap->driver));
	return ici->ops->querycap(ici, cap);
}

static int soc_camera_streamon(struct file *file, void *priv,
			       enum v4l2_buf_type i)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	int ret;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (icd->streamer != file)
		return -EBUSY;

	/* This calls buf_queue from host driver's videobuf_queue_ops */
	if (ici->ops->init_videobuf)
		ret = videobuf_streamon(&icd->vb_vidq);
	else
		ret = vb2_streamon(&icd->vb2_vidq, i);

	if (!ret)
		v4l2_subdev_call(sd, video, s_stream, 1);

	return ret;
}

static int soc_camera_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type i)
{
	struct soc_camera_device *icd = file->private_data;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (icd->streamer != file)
		return -EBUSY;

	/*
	 * This calls buf_release from host driver's videobuf_queue_ops for all
	 * remaining buffers. When the last buffer is freed, stop capture
	 */
	if (ici->ops->init_videobuf)
		videobuf_streamoff(&icd->vb_vidq);
	else
		vb2_streamoff(&icd->vb2_vidq, i);

	v4l2_subdev_call(sd, video, s_stream, 0);

	return 0;
}

static int soc_camera_cropcap(struct file *file, void *fh,
			      struct v4l2_cropcap *a)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	return ici->ops->cropcap(icd, a);
}

static int soc_camera_g_crop(struct file *file, void *fh,
			     struct v4l2_crop *a)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	int ret;

	ret = ici->ops->get_crop(icd, a);

	return ret;
}

/*
 * According to the V4L2 API, drivers shall not update the struct v4l2_crop
 * argument with the actual geometry, instead, the user shall use G_CROP to
 * retrieve it.
 */
static int soc_camera_s_crop(struct file *file, void *fh,
			     const struct v4l2_crop *a)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	const struct v4l2_rect *rect = &a->c;
	struct v4l2_crop current_crop;
	int ret;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	dev_dbg(icd->pdev, "S_CROP(%ux%u@%u:%u)\n",
		rect->width, rect->height, rect->left, rect->top);

	current_crop.type = a->type;

	/* If get_crop fails, we'll let host and / or client drivers decide */
	ret = ici->ops->get_crop(icd, &current_crop);

	/* Prohibit window size change with initialised buffers */
	if (ret < 0) {
		dev_err(icd->pdev,
			"S_CROP denied: getting current crop failed\n");
	} else if ((a->c.width == current_crop.c.width &&
		    a->c.height == current_crop.c.height) ||
		   !is_streaming(ici, icd)) {
		/* same size or not streaming - use .set_crop() */
		ret = ici->ops->set_crop(icd, a);
	} else if (ici->ops->set_livecrop) {
		ret = ici->ops->set_livecrop(icd, a);
	} else {
		dev_err(icd->pdev,
			"S_CROP denied: queue initialised and sizes differ\n");
		ret = -EBUSY;
	}

	return ret;
}

static int soc_camera_g_selection(struct file *file, void *fh,
				  struct v4l2_selection *s)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	/* With a wrong type no need to try to fall back to cropping */
	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!ici->ops->get_selection)
		return -ENOTTY;

	return ici->ops->get_selection(icd, s);
}

static int soc_camera_s_selection(struct file *file, void *fh,
				  struct v4l2_selection *s)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	int ret;

	/* In all these cases cropping emulation will not help */
	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    (s->target != V4L2_SEL_TGT_COMPOSE &&
	     s->target != V4L2_SEL_TGT_CROP))
		return -EINVAL;

	if (s->target == V4L2_SEL_TGT_COMPOSE) {
		/* No output size change during a running capture! */
		if (is_streaming(ici, icd) &&
		    (icd->user_width != s->r.width ||
		     icd->user_height != s->r.height))
			return -EBUSY;

		/*
		 * Only one user is allowed to change the output format, touch
		 * buffers, start / stop streaming, poll for data
		 */
		if (icd->streamer && icd->streamer != file)
			return -EBUSY;
	}

	if (!ici->ops->set_selection)
		return -ENOTTY;

	ret = ici->ops->set_selection(icd, s);
	if (!ret &&
	    s->target == V4L2_SEL_TGT_COMPOSE) {
		icd->user_width = s->r.width;
		icd->user_height = s->r.height;
		if (!icd->streamer)
			icd->streamer = file;
	}

	return ret;
}

static int soc_camera_g_parm(struct file *file, void *fh,
			     struct v4l2_streamparm *a)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	if (ici->ops->get_parm)
		return ici->ops->get_parm(icd, a);

	return -ENOIOCTLCMD;
}

static int soc_camera_s_parm(struct file *file, void *fh,
			     struct v4l2_streamparm *a)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	if (ici->ops->set_parm)
		return ici->ops->set_parm(icd, a);

	return -ENOIOCTLCMD;
}

static int soc_camera_probe(struct soc_camera_host *ici,
			    struct soc_camera_device *icd);

/* So far this function cannot fail */
static void scan_add_host(struct soc_camera_host *ici)
{
	struct soc_camera_device *icd;

	mutex_lock(&list_lock);

	list_for_each_entry(icd, &devices, list)
		if (icd->iface == ici->nr) {
			struct soc_camera_desc *sdesc = to_soc_camera_desc(icd);
			struct soc_camera_subdev_desc *ssdd = &sdesc->subdev_desc;

			/* The camera could have been already on, try to reset */
			if (ssdd->reset)
				ssdd->reset(icd->pdev);

			icd->parent = ici->v4l2_dev.dev;

			/* Ignore errors */
			soc_camera_probe(ici, icd);
		}

	mutex_unlock(&list_lock);
}

/*
 * It is invalid to call v4l2_clk_enable() after a successful probing
 * asynchronously outside of V4L2 operations, i.e. with .host_lock not held.
 */
static int soc_camera_clk_enable(struct v4l2_clk *clk)
{
	struct soc_camera_device *icd = clk->priv;
	struct soc_camera_host *ici;
	int ret;

	if (!icd || !icd->parent)
		return -ENODEV;

	ici = to_soc_camera_host(icd->parent);

	if (!try_module_get(ici->ops->owner))
		return -ENODEV;

	/*
	 * If a different client is currently being probed, the host will tell
	 * you to go
	 */
	mutex_lock(&ici->clk_lock);
	ret = ici->ops->clock_start(ici);
	mutex_unlock(&ici->clk_lock);
	return ret;
}

static void soc_camera_clk_disable(struct v4l2_clk *clk)
{
	struct soc_camera_device *icd = clk->priv;
	struct soc_camera_host *ici;

	if (!icd || !icd->parent)
		return;

	ici = to_soc_camera_host(icd->parent);

	mutex_lock(&ici->clk_lock);
	ici->ops->clock_stop(ici);
	mutex_unlock(&ici->clk_lock);

	module_put(ici->ops->owner);
}

/*
 * Eventually, it would be more logical to make the respective host the clock
 * owner, but then we would have to copy this struct for each ici. Besides, it
 * would introduce the circular dependency problem, unless we port all client
 * drivers to release the clock, when not in use.
 */
static const struct v4l2_clk_ops soc_camera_clk_ops = {
	.owner = THIS_MODULE,
	.enable = soc_camera_clk_enable,
	.disable = soc_camera_clk_disable,
};

static int soc_camera_dyn_pdev(struct soc_camera_desc *sdesc,
			       struct soc_camera_async_client *sasc)
{
	struct platform_device *pdev;
	int ret, i;

	mutex_lock(&list_lock);
	i = find_first_zero_bit(device_map, MAP_MAX_NUM);
	if (i < MAP_MAX_NUM)
		set_bit(i, device_map);
	mutex_unlock(&list_lock);
	if (i >= MAP_MAX_NUM)
		return -ENOMEM;

	pdev = platform_device_alloc("soc-camera-pdrv", i);
	if (!pdev)
		return -ENOMEM;

	ret = platform_device_add_data(pdev, sdesc, sizeof(*sdesc));
	if (ret < 0) {
		platform_device_put(pdev);
		return ret;
	}

	sasc->pdev = pdev;

	return 0;
}

static struct soc_camera_device *soc_camera_add_pdev(struct soc_camera_async_client *sasc)
{
	struct platform_device *pdev = sasc->pdev;
	int ret;

	ret = platform_device_add(pdev);
	if (ret < 0 || !pdev->dev.driver)
		return NULL;

	return platform_get_drvdata(pdev);
}

/* Locking: called with .host_lock held */
static int soc_camera_probe_finish(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	int ret;

	sd->grp_id = soc_camera_grp_id(icd);
	v4l2_set_subdev_hostdata(sd, icd);

	v4l2_subdev_call(sd, video, g_tvnorms, &icd->vdev->tvnorms);

	ret = v4l2_ctrl_add_handler(&icd->ctrl_handler, sd->ctrl_handler, NULL);
	if (ret < 0)
		return ret;

	ret = soc_camera_add_device(icd);
	if (ret < 0) {
		dev_err(icd->pdev, "Couldn't activate the camera: %d\n", ret);
		return ret;
	}

	/* At this point client .probe() should have run already */
	ret = soc_camera_init_user_formats(icd);
	if (ret < 0)
		goto eusrfmt;

	icd->field = V4L2_FIELD_ANY;

	ret = soc_camera_video_start(icd);
	if (ret < 0)
		goto evidstart;

	/* Try to improve our guess of a reasonable window format */
	if (!v4l2_subdev_call(sd, video, g_mbus_fmt, &mf)) {
		icd->user_width		= mf.width;
		icd->user_height	= mf.height;
		icd->colorspace		= mf.colorspace;
		icd->field		= mf.field;
	}
	soc_camera_remove_device(icd);

	return 0;

evidstart:
	soc_camera_free_user_formats(icd);
eusrfmt:
	soc_camera_remove_device(icd);

	return ret;
}

#ifdef CONFIG_I2C_BOARDINFO
static int soc_camera_i2c_init(struct soc_camera_device *icd,
			       struct soc_camera_desc *sdesc)
{
	struct soc_camera_subdev_desc *ssdd;
	struct i2c_client *client;
	struct soc_camera_host *ici;
	struct soc_camera_host_desc *shd = &sdesc->host_desc;
	struct i2c_adapter *adap;
	struct v4l2_subdev *subdev;
	char clk_name[V4L2_SUBDEV_NAME_SIZE];
	int ret;

	/* First find out how we link the main client */
	if (icd->sasc) {
		/* Async non-OF probing handled by the subdevice list */
		return -EPROBE_DEFER;
	}

	ici = to_soc_camera_host(icd->parent);
	adap = i2c_get_adapter(shd->i2c_adapter_id);
	if (!adap) {
		dev_err(icd->pdev, "Cannot get I2C adapter #%d. No driver?\n",
			shd->i2c_adapter_id);
		return -ENODEV;
	}

	ssdd = kmemdup(&sdesc->subdev_desc, sizeof(*ssdd), GFP_KERNEL);
	if (!ssdd) {
		ret = -ENOMEM;
		goto ealloc;
	}
	/*
	 * In synchronous case we request regulators ourselves in
	 * soc_camera_pdrv_probe(), make sure the subdevice driver doesn't try
	 * to allocate them again.
	 */
	ssdd->sd_pdata.num_regulators = 0;
	ssdd->sd_pdata.regulators = NULL;
	shd->board_info->platform_data = ssdd;

	snprintf(clk_name, sizeof(clk_name), "%d-%04x",
		 shd->i2c_adapter_id, shd->board_info->addr);

	icd->clk = v4l2_clk_register(&soc_camera_clk_ops, clk_name, "mclk", icd);
	if (IS_ERR(icd->clk)) {
		ret = PTR_ERR(icd->clk);
		goto eclkreg;
	}

	subdev = v4l2_i2c_new_subdev_board(&ici->v4l2_dev, adap,
				shd->board_info, NULL);
	if (!subdev) {
		ret = -ENODEV;
		goto ei2cnd;
	}

	client = v4l2_get_subdevdata(subdev);

	/* Use to_i2c_client(dev) to recover the i2c client */
	icd->control = &client->dev;

	return 0;
ei2cnd:
	v4l2_clk_unregister(icd->clk);
	icd->clk = NULL;
eclkreg:
	kfree(ssdd);
ealloc:
	i2c_put_adapter(adap);
	return ret;
}

static void soc_camera_i2c_free(struct soc_camera_device *icd)
{
	struct i2c_client *client =
		to_i2c_client(to_soc_camera_control(icd));
	struct i2c_adapter *adap;
	struct soc_camera_subdev_desc *ssdd;

	icd->control = NULL;
	if (icd->sasc)
		return;

	adap = client->adapter;
	ssdd = client->dev.platform_data;
	v4l2_device_unregister_subdev(i2c_get_clientdata(client));
	i2c_unregister_device(client);
	i2c_put_adapter(adap);
	kfree(ssdd);
	v4l2_clk_unregister(icd->clk);
	icd->clk = NULL;
}

/*
 * V4L2 asynchronous notifier callbacks. They are all called under a v4l2-async
 * internal global mutex, therefore cannot race against other asynchronous
 * events. Until notifier->complete() (soc_camera_async_complete()) is called,
 * the video device node is not registered and no V4L fops can occur. Unloading
 * of the host driver also calls a v4l2-async function, so also there we're
 * protected.
 */
static int soc_camera_async_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct soc_camera_async_client *sasc = container_of(notifier,
					struct soc_camera_async_client, notifier);
	struct soc_camera_device *icd = platform_get_drvdata(sasc->pdev);

	if (asd == sasc->sensor && !WARN_ON(icd->control)) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);

		/*
		 * Only now we get subdevice-specific information like
		 * regulators, flags, callbacks, etc.
		 */
		if (client) {
			struct soc_camera_desc *sdesc = to_soc_camera_desc(icd);
			struct soc_camera_subdev_desc *ssdd =
				soc_camera_i2c_to_desc(client);
			if (ssdd) {
				memcpy(&sdesc->subdev_desc, ssdd,
				       sizeof(sdesc->subdev_desc));
				if (ssdd->reset)
					ssdd->reset(icd->pdev);
			}

			icd->control = &client->dev;
		}
	}

	return 0;
}

static void soc_camera_async_unbind(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *sd,
				    struct v4l2_async_subdev *asd)
{
	struct soc_camera_async_client *sasc = container_of(notifier,
					struct soc_camera_async_client, notifier);
	struct soc_camera_device *icd = platform_get_drvdata(sasc->pdev);

	if (icd->clk) {
		v4l2_clk_unregister(icd->clk);
		icd->clk = NULL;
	}
}

static int soc_camera_async_complete(struct v4l2_async_notifier *notifier)
{
	struct soc_camera_async_client *sasc = container_of(notifier,
					struct soc_camera_async_client, notifier);
	struct soc_camera_device *icd = platform_get_drvdata(sasc->pdev);

	if (to_soc_camera_control(icd)) {
		struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
		int ret;

		mutex_lock(&list_lock);
		ret = soc_camera_probe(ici, icd);
		mutex_unlock(&list_lock);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int scan_async_group(struct soc_camera_host *ici,
			    struct v4l2_async_subdev **asd, unsigned int size)
{
	struct soc_camera_async_subdev *sasd;
	struct soc_camera_async_client *sasc;
	struct soc_camera_device *icd;
	struct soc_camera_desc sdesc = {.host_desc.bus_id = ici->nr,};
	char clk_name[V4L2_SUBDEV_NAME_SIZE];
	unsigned int i;
	int ret;

	/* First look for a sensor */
	for (i = 0; i < size; i++) {
		sasd = container_of(asd[i], struct soc_camera_async_subdev, asd);
		if (sasd->role == SOCAM_SUBDEV_DATA_SOURCE)
			break;
	}

	if (i >= size || asd[i]->match_type != V4L2_ASYNC_MATCH_I2C) {
		/* All useless */
		dev_err(ici->v4l2_dev.dev, "No I2C data source found!\n");
		return -ENODEV;
	}

	/* Or shall this be managed by the soc-camera device? */
	sasc = devm_kzalloc(ici->v4l2_dev.dev, sizeof(*sasc), GFP_KERNEL);
	if (!sasc)
		return -ENOMEM;

	/* HACK: just need a != NULL */
	sdesc.host_desc.board_info = ERR_PTR(-ENODATA);

	ret = soc_camera_dyn_pdev(&sdesc, sasc);
	if (ret < 0)
		goto eallocpdev;

	sasc->sensor = &sasd->asd;

	icd = soc_camera_add_pdev(sasc);
	if (!icd) {
		ret = -ENOMEM;
		goto eaddpdev;
	}

	sasc->notifier.subdevs = asd;
	sasc->notifier.num_subdevs = size;
	sasc->notifier.bound = soc_camera_async_bound;
	sasc->notifier.unbind = soc_camera_async_unbind;
	sasc->notifier.complete = soc_camera_async_complete;

	icd->sasc = sasc;
	icd->parent = ici->v4l2_dev.dev;

	snprintf(clk_name, sizeof(clk_name), "%d-%04x",
		 sasd->asd.match.i2c.adapter_id, sasd->asd.match.i2c.address);

	icd->clk = v4l2_clk_register(&soc_camera_clk_ops, clk_name, "mclk", icd);
	if (IS_ERR(icd->clk)) {
		ret = PTR_ERR(icd->clk);
		goto eclkreg;
	}

	ret = v4l2_async_notifier_register(&ici->v4l2_dev, &sasc->notifier);
	if (!ret)
		return 0;

	v4l2_clk_unregister(icd->clk);
eclkreg:
	icd->clk = NULL;
	platform_device_del(sasc->pdev);
eaddpdev:
	platform_device_put(sasc->pdev);
eallocpdev:
	devm_kfree(ici->v4l2_dev.dev, sasc);
	dev_err(ici->v4l2_dev.dev, "group probe failed: %d\n", ret);

	return ret;
}

static void scan_async_host(struct soc_camera_host *ici)
{
	struct v4l2_async_subdev **asd;
	int j;

	for (j = 0, asd = ici->asd; ici->asd_sizes[j]; j++) {
		scan_async_group(ici, asd, ici->asd_sizes[j]);
		asd += ici->asd_sizes[j];
	}
}
#else
#define soc_camera_i2c_init(icd, sdesc)	(-ENODEV)
#define soc_camera_i2c_free(icd)	do {} while (0)
#define scan_async_host(ici)		do {} while (0)
#endif

#ifdef CONFIG_OF

struct soc_of_info {
	struct soc_camera_async_subdev	sasd;
	struct soc_camera_async_client	sasc;
	struct v4l2_async_subdev	*subdev;
};

static int soc_of_bind(struct soc_camera_host *ici,
		       struct device_node *ep,
		       struct device_node *remote)
{
	struct soc_camera_device *icd;
	struct soc_camera_desc sdesc = {.host_desc.bus_id = ici->nr,};
	struct soc_camera_async_client *sasc;
	struct soc_of_info *info;
	struct i2c_client *client;
	char clk_name[V4L2_SUBDEV_NAME_SIZE];
	int ret;

	/* allocate a new subdev and add match info to it */
	info = devm_kzalloc(ici->v4l2_dev.dev, sizeof(struct soc_of_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->sasd.asd.match.of.node = remote;
	info->sasd.asd.match_type = V4L2_ASYNC_MATCH_OF;
	info->subdev = &info->sasd.asd;

	/* Or shall this be managed by the soc-camera device? */
	sasc = &info->sasc;

	/* HACK: just need a != NULL */
	sdesc.host_desc.board_info = ERR_PTR(-ENODATA);

	ret = soc_camera_dyn_pdev(&sdesc, sasc);
	if (ret < 0)
		goto eallocpdev;

	sasc->sensor = &info->sasd.asd;

	icd = soc_camera_add_pdev(sasc);
	if (!icd) {
		ret = -ENOMEM;
		goto eaddpdev;
	}

	sasc->notifier.subdevs = &info->subdev;
	sasc->notifier.num_subdevs = 1;
	sasc->notifier.bound = soc_camera_async_bound;
	sasc->notifier.unbind = soc_camera_async_unbind;
	sasc->notifier.complete = soc_camera_async_complete;

	icd->sasc = sasc;
	icd->parent = ici->v4l2_dev.dev;

	client = of_find_i2c_device_by_node(remote);

	if (client)
		snprintf(clk_name, sizeof(clk_name), "%d-%04x",
			 client->adapter->nr, client->addr);
	else
		snprintf(clk_name, sizeof(clk_name), "of-%s",
			 of_node_full_name(remote));

	icd->clk = v4l2_clk_register(&soc_camera_clk_ops, clk_name, "mclk", icd);
	if (IS_ERR(icd->clk)) {
		ret = PTR_ERR(icd->clk);
		goto eclkreg;
	}

	ret = v4l2_async_notifier_register(&ici->v4l2_dev, &sasc->notifier);
	if (!ret)
		return 0;
eclkreg:
	icd->clk = NULL;
	platform_device_del(sasc->pdev);
eaddpdev:
	platform_device_put(sasc->pdev);
eallocpdev:
	devm_kfree(ici->v4l2_dev.dev, sasc);
	dev_err(ici->v4l2_dev.dev, "group probe failed: %d\n", ret);

	return ret;
}

static void scan_of_host(struct soc_camera_host *ici)
{
	struct device *dev = ici->v4l2_dev.dev;
	struct device_node *np = dev->of_node;
	struct device_node *epn = NULL, *ren;
	unsigned int i;

	for (i = 0; ; i++) {
		epn = of_graph_get_next_endpoint(np, epn);
		if (!epn)
			break;

		ren = of_graph_get_remote_port(epn);
		if (!ren) {
			dev_notice(dev, "no remote for %s\n",
				   of_node_full_name(epn));
			continue;
		}

		/* so we now have a remote node to connect */
		if (!i)
			soc_of_bind(ici, epn, ren->parent);

		of_node_put(epn);
		of_node_put(ren);

		if (i) {
			dev_err(dev, "multiple subdevices aren't supported yet!\n");
			break;
		}
	}
}

#else
static inline void scan_of_host(struct soc_camera_host *ici) { }
#endif

/* Called during host-driver probe */
static int soc_camera_probe(struct soc_camera_host *ici,
			    struct soc_camera_device *icd)
{
	struct soc_camera_desc *sdesc = to_soc_camera_desc(icd);
	struct soc_camera_host_desc *shd = &sdesc->host_desc;
	struct device *control = NULL;
	int ret;

	dev_info(icd->pdev, "Probing %s\n", dev_name(icd->pdev));

	/*
	 * Currently the subdev with the largest number of controls (13) is
	 * ov6550. So let's pick 16 as a hint for the control handler. Note
	 * that this is a hint only: too large and you waste some memory, too
	 * small and there is a (very) small performance hit when looking up
	 * controls in the internal hash.
	 */
	ret = v4l2_ctrl_handler_init(&icd->ctrl_handler, 16);
	if (ret < 0)
		return ret;

	/* Must have icd->vdev before registering the device */
	ret = video_dev_create(icd);
	if (ret < 0)
		goto evdc;

	/*
	 * ..._video_start() will create a device node, video_register_device()
	 * itself is protected against concurrent open() calls, but we also have
	 * to protect our data also during client probing.
	 */

	/* Non-i2c cameras, e.g., soc_camera_platform, have no board_info */
	if (shd->board_info) {
		ret = soc_camera_i2c_init(icd, sdesc);
		if (ret < 0 && ret != -EPROBE_DEFER)
			goto eadd;
	} else if (!shd->add_device || !shd->del_device) {
		ret = -EINVAL;
		goto eadd;
	} else {
		mutex_lock(&ici->clk_lock);
		ret = ici->ops->clock_start(ici);
		mutex_unlock(&ici->clk_lock);
		if (ret < 0)
			goto eadd;

		if (shd->module_name)
			ret = request_module(shd->module_name);

		ret = shd->add_device(icd);
		if (ret < 0)
			goto eadddev;

		/*
		 * FIXME: this is racy, have to use driver-binding notification,
		 * when it is available
		 */
		control = to_soc_camera_control(icd);
		if (!control || !control->driver || !dev_get_drvdata(control) ||
		    !try_module_get(control->driver->owner)) {
			shd->del_device(icd);
			ret = -ENODEV;
			goto enodrv;
		}
	}

	mutex_lock(&ici->host_lock);
	ret = soc_camera_probe_finish(icd);
	mutex_unlock(&ici->host_lock);
	if (ret < 0)
		goto efinish;

	return 0;

efinish:
	if (shd->board_info) {
		soc_camera_i2c_free(icd);
	} else {
		shd->del_device(icd);
		module_put(control->driver->owner);
enodrv:
eadddev:
		mutex_lock(&ici->clk_lock);
		ici->ops->clock_stop(ici);
		mutex_unlock(&ici->clk_lock);
	}
eadd:
	if (icd->vdev) {
		video_device_release(icd->vdev);
		icd->vdev = NULL;
	}
evdc:
	v4l2_ctrl_handler_free(&icd->ctrl_handler);
	return ret;
}

/*
 * This is called on device_unregister, which only means we have to disconnect
 * from the host, but not remove ourselves from the device list. With
 * asynchronous client probing this can also be called without
 * soc_camera_probe_finish() having run. Careful with clean up.
 */
static int soc_camera_remove(struct soc_camera_device *icd)
{
	struct soc_camera_desc *sdesc = to_soc_camera_desc(icd);
	struct video_device *vdev = icd->vdev;

	v4l2_ctrl_handler_free(&icd->ctrl_handler);
	if (vdev) {
		video_unregister_device(vdev);
		icd->vdev = NULL;
	}

	if (sdesc->host_desc.board_info) {
		soc_camera_i2c_free(icd);
	} else {
		struct device *dev = to_soc_camera_control(icd);
		struct device_driver *drv = dev ? dev->driver : NULL;
		if (drv) {
			sdesc->host_desc.del_device(icd);
			module_put(drv->owner);
		}
	}

	if (icd->num_user_formats)
		soc_camera_free_user_formats(icd);

	if (icd->clk) {
		/* For the synchronous case */
		v4l2_clk_unregister(icd->clk);
		icd->clk = NULL;
	}

	if (icd->sasc)
		platform_device_unregister(icd->sasc->pdev);

	return 0;
}

static int default_cropcap(struct soc_camera_device *icd,
			   struct v4l2_cropcap *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	return v4l2_subdev_call(sd, video, cropcap, a);
}

static int default_g_crop(struct soc_camera_device *icd, struct v4l2_crop *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	return v4l2_subdev_call(sd, video, g_crop, a);
}

static int default_s_crop(struct soc_camera_device *icd, const struct v4l2_crop *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	return v4l2_subdev_call(sd, video, s_crop, a);
}

static int default_g_parm(struct soc_camera_device *icd,
			  struct v4l2_streamparm *parm)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	return v4l2_subdev_call(sd, video, g_parm, parm);
}

static int default_s_parm(struct soc_camera_device *icd,
			  struct v4l2_streamparm *parm)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	return v4l2_subdev_call(sd, video, s_parm, parm);
}

static int default_enum_framesizes(struct soc_camera_device *icd,
				   struct v4l2_frmsizeenum *fsize)
{
	int ret;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	xlate = soc_camera_xlate_by_fourcc(icd, fsize->pixel_format);
	if (!xlate)
		return -EINVAL;
	fse.code = xlate->code;

	ret = v4l2_subdev_call(sd, pad, enum_frame_size, NULL, &fse);
	if (ret < 0)
		return ret;

	if (fse.min_width == fse.max_width &&
	    fse.min_height == fse.max_height) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = fse.min_width;
		fsize->discrete.height = fse.min_height;
		return 0;
	}
	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = fse.min_width;
	fsize->stepwise.max_width = fse.max_width;
	fsize->stepwise.min_height = fse.min_height;
	fsize->stepwise.max_height = fse.max_height;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;
	return 0;
}

int soc_camera_host_register(struct soc_camera_host *ici)
{
	struct soc_camera_host *ix;
	int ret;

	if (!ici || !ici->ops ||
	    !ici->ops->try_fmt ||
	    !ici->ops->set_fmt ||
	    !ici->ops->set_bus_param ||
	    !ici->ops->querycap ||
	    ((!ici->ops->init_videobuf ||
	      !ici->ops->reqbufs) &&
	     !ici->ops->init_videobuf2) ||
	    !ici->ops->clock_start ||
	    !ici->ops->clock_stop ||
	    !ici->ops->poll ||
	    !ici->v4l2_dev.dev)
		return -EINVAL;

	if (!ici->ops->set_crop)
		ici->ops->set_crop = default_s_crop;
	if (!ici->ops->get_crop)
		ici->ops->get_crop = default_g_crop;
	if (!ici->ops->cropcap)
		ici->ops->cropcap = default_cropcap;
	if (!ici->ops->set_parm)
		ici->ops->set_parm = default_s_parm;
	if (!ici->ops->get_parm)
		ici->ops->get_parm = default_g_parm;
	if (!ici->ops->enum_framesizes)
		ici->ops->enum_framesizes = default_enum_framesizes;

	mutex_lock(&list_lock);
	list_for_each_entry(ix, &hosts, list) {
		if (ix->nr == ici->nr) {
			ret = -EBUSY;
			goto edevreg;
		}
	}

	ret = v4l2_device_register(ici->v4l2_dev.dev, &ici->v4l2_dev);
	if (ret < 0)
		goto edevreg;

	list_add_tail(&ici->list, &hosts);
	mutex_unlock(&list_lock);

	mutex_init(&ici->host_lock);
	mutex_init(&ici->clk_lock);

	if (ici->v4l2_dev.dev->of_node)
		scan_of_host(ici);
	else if (ici->asd_sizes)
		/*
		 * No OF, host with a list of subdevices. Don't try to mix
		 * modes by initialising some groups statically and some
		 * dynamically!
		 */
		scan_async_host(ici);
	else
		/* Legacy: static platform devices from board data */
		scan_add_host(ici);

	return 0;

edevreg:
	mutex_unlock(&list_lock);
	return ret;
}
EXPORT_SYMBOL(soc_camera_host_register);

/* Unregister all clients! */
void soc_camera_host_unregister(struct soc_camera_host *ici)
{
	struct soc_camera_device *icd, *tmp;
	struct soc_camera_async_client *sasc;
	LIST_HEAD(notifiers);

	mutex_lock(&list_lock);
	list_del(&ici->list);
	list_for_each_entry(icd, &devices, list)
		if (icd->iface == ici->nr && icd->sasc) {
			/* as long as we hold the device, sasc won't be freed */
			get_device(icd->pdev);
			list_add(&icd->sasc->list, &notifiers);
		}
	mutex_unlock(&list_lock);

	list_for_each_entry(sasc, &notifiers, list) {
		/* Must call unlocked to avoid AB-BA dead-lock */
		v4l2_async_notifier_unregister(&sasc->notifier);
		put_device(&sasc->pdev->dev);
	}

	mutex_lock(&list_lock);

	list_for_each_entry_safe(icd, tmp, &devices, list)
		if (icd->iface == ici->nr)
			soc_camera_remove(icd);

	mutex_unlock(&list_lock);

	v4l2_device_unregister(&ici->v4l2_dev);
}
EXPORT_SYMBOL(soc_camera_host_unregister);

/* Image capture device */
static int soc_camera_device_register(struct soc_camera_device *icd)
{
	struct soc_camera_device *ix;
	int num = -1, i;

	mutex_lock(&list_lock);
	for (i = 0; i < 256 && num < 0; i++) {
		num = i;
		/* Check if this index is available on this interface */
		list_for_each_entry(ix, &devices, list) {
			if (ix->iface == icd->iface && ix->devnum == i) {
				num = -1;
				break;
			}
		}
	}

	if (num < 0) {
		/*
		 * ok, we have 256 cameras on this host...
		 * man, stay reasonable...
		 */
		mutex_unlock(&list_lock);
		return -ENOMEM;
	}

	icd->devnum		= num;
	icd->use_count		= 0;
	icd->host_priv		= NULL;

	/*
	 * Dynamically allocated devices set the bit earlier, but it doesn't hurt setting
	 * it again
	 */
	i = to_platform_device(icd->pdev)->id;
	if (i < 0)
		/* One static (legacy) soc-camera platform device */
		i = 0;
	if (i >= MAP_MAX_NUM) {
		mutex_unlock(&list_lock);
		return -EBUSY;
	}
	set_bit(i, device_map);
	list_add_tail(&icd->list, &devices);
	mutex_unlock(&list_lock);

	return 0;
}

static const struct v4l2_ioctl_ops soc_camera_ioctl_ops = {
	.vidioc_querycap	 = soc_camera_querycap,
	.vidioc_try_fmt_vid_cap  = soc_camera_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap    = soc_camera_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap    = soc_camera_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = soc_camera_enum_fmt_vid_cap,
	.vidioc_enum_input	 = soc_camera_enum_input,
	.vidioc_g_input		 = soc_camera_g_input,
	.vidioc_s_input		 = soc_camera_s_input,
	.vidioc_s_std		 = soc_camera_s_std,
	.vidioc_g_std		 = soc_camera_g_std,
	.vidioc_enum_framesizes  = soc_camera_enum_framesizes,
	.vidioc_reqbufs		 = soc_camera_reqbufs,
	.vidioc_querybuf	 = soc_camera_querybuf,
	.vidioc_qbuf		 = soc_camera_qbuf,
	.vidioc_dqbuf		 = soc_camera_dqbuf,
	.vidioc_create_bufs	 = soc_camera_create_bufs,
	.vidioc_prepare_buf	 = soc_camera_prepare_buf,
	.vidioc_expbuf		 = soc_camera_expbuf,
	.vidioc_streamon	 = soc_camera_streamon,
	.vidioc_streamoff	 = soc_camera_streamoff,
	.vidioc_cropcap		 = soc_camera_cropcap,
	.vidioc_g_crop		 = soc_camera_g_crop,
	.vidioc_s_crop		 = soc_camera_s_crop,
	.vidioc_g_selection	 = soc_camera_g_selection,
	.vidioc_s_selection	 = soc_camera_s_selection,
	.vidioc_g_parm		 = soc_camera_g_parm,
	.vidioc_s_parm		 = soc_camera_s_parm,
};

static int video_dev_create(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct video_device *vdev = video_device_alloc();

	if (!vdev)
		return -ENOMEM;

	strlcpy(vdev->name, ici->drv_name, sizeof(vdev->name));

	vdev->v4l2_dev		= &ici->v4l2_dev;
	vdev->fops		= &soc_camera_fops;
	vdev->ioctl_ops		= &soc_camera_ioctl_ops;
	vdev->release		= video_device_release;
	vdev->ctrl_handler	= &icd->ctrl_handler;
	vdev->lock		= &ici->host_lock;

	icd->vdev = vdev;

	return 0;
}

/*
 * Called from soc_camera_probe() above with .host_lock held
 */
static int soc_camera_video_start(struct soc_camera_device *icd)
{
	const struct device_type *type = icd->vdev->dev.type;
	int ret;

	if (!icd->parent)
		return -ENODEV;

	video_set_drvdata(icd->vdev, icd);
	if (icd->vdev->tvnorms == 0) {
		/* disable the STD API if there are no tvnorms defined */
		v4l2_disable_ioctl(icd->vdev, VIDIOC_G_STD);
		v4l2_disable_ioctl(icd->vdev, VIDIOC_S_STD);
		v4l2_disable_ioctl(icd->vdev, VIDIOC_ENUMSTD);
	}
	ret = video_register_device(icd->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(icd->pdev, "video_register_device failed: %d\n", ret);
		return ret;
	}

	/* Restore device type, possibly set by the subdevice driver */
	icd->vdev->dev.type = type;

	return 0;
}

static int soc_camera_pdrv_probe(struct platform_device *pdev)
{
	struct soc_camera_desc *sdesc = pdev->dev.platform_data;
	struct soc_camera_subdev_desc *ssdd = &sdesc->subdev_desc;
	struct soc_camera_device *icd;
	int ret;

	if (!sdesc)
		return -EINVAL;

	icd = devm_kzalloc(&pdev->dev, sizeof(*icd), GFP_KERNEL);
	if (!icd)
		return -ENOMEM;

	/*
	 * In the asynchronous case ssdd->num_regulators == 0 yet, so, the below
	 * regulator allocation is a dummy. They are actually requested by the
	 * subdevice driver, using soc_camera_power_init(). Also note, that in
	 * that case regulators are attached to the I2C device and not to the
	 * camera platform device.
	 */
	ret = devm_regulator_bulk_get(&pdev->dev, ssdd->sd_pdata.num_regulators,
				      ssdd->sd_pdata.regulators);
	if (ret < 0)
		return ret;

	icd->iface = sdesc->host_desc.bus_id;
	icd->sdesc = sdesc;
	icd->pdev = &pdev->dev;
	platform_set_drvdata(pdev, icd);

	icd->user_width		= DEFAULT_WIDTH;
	icd->user_height	= DEFAULT_HEIGHT;

	return soc_camera_device_register(icd);
}

/*
 * Only called on rmmod for each platform device, since they are not
 * hot-pluggable. Now we know, that all our users - hosts and devices have
 * been unloaded already
 */
static int soc_camera_pdrv_remove(struct platform_device *pdev)
{
	struct soc_camera_device *icd = platform_get_drvdata(pdev);
	int i;

	if (!icd)
		return -EINVAL;

	i = pdev->id;
	if (i < 0)
		i = 0;

	/*
	 * In synchronous mode with static platform devices this is called in a
	 * loop from drivers/base/dd.c::driver_detach(), no parallel execution,
	 * no need to lock. In asynchronous case the caller -
	 * soc_camera_host_unregister() - already holds the lock
	 */
	if (test_bit(i, device_map)) {
		clear_bit(i, device_map);
		list_del(&icd->list);
	}

	return 0;
}

static struct platform_driver __refdata soc_camera_pdrv = {
	.probe = soc_camera_pdrv_probe,
	.remove  = soc_camera_pdrv_remove,
	.driver  = {
		.name	= "soc-camera-pdrv",
	},
};

module_platform_driver(soc_camera_pdrv);

MODULE_DESCRIPTION("Image capture bus driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-camera-pdrv");
