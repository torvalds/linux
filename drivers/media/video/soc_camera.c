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
#include <linux/vmalloc.h>

#include <media/soc_camera.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-core.h>

/* Default to VGA resolution */
#define DEFAULT_WIDTH	640
#define DEFAULT_HEIGHT	480

static LIST_HEAD(hosts);
static LIST_HEAD(devices);
static DEFINE_MUTEX(list_lock);

const struct soc_camera_data_format *soc_camera_format_by_fourcc(
	struct soc_camera_device *icd, unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < icd->num_formats; i++)
		if (icd->formats[i].fourcc == fourcc)
			return icd->formats + i;
	return NULL;
}
EXPORT_SYMBOL(soc_camera_format_by_fourcc);

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
 * soc_camera_apply_sensor_flags() - apply platform SOCAM_SENSOR_INVERT_* flags
 * @icl:	camera platform parameters
 * @flags:	flags to be inverted according to platform configuration
 * @return:	resulting flags
 */
unsigned long soc_camera_apply_sensor_flags(struct soc_camera_link *icl,
					    unsigned long flags)
{
	unsigned long f;

	/* If only one of the two polarities is supported, switch to the opposite */
	if (icl->flags & SOCAM_SENSOR_INVERT_HSYNC) {
		f = flags & (SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_LOW);
		if (f == SOCAM_HSYNC_ACTIVE_HIGH || f == SOCAM_HSYNC_ACTIVE_LOW)
			flags ^= SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_LOW;
	}

	if (icl->flags & SOCAM_SENSOR_INVERT_VSYNC) {
		f = flags & (SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW);
		if (f == SOCAM_VSYNC_ACTIVE_HIGH || f == SOCAM_VSYNC_ACTIVE_LOW)
			flags ^= SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW;
	}

	if (icl->flags & SOCAM_SENSOR_INVERT_PCLK) {
		f = flags & (SOCAM_PCLK_SAMPLE_RISING | SOCAM_PCLK_SAMPLE_FALLING);
		if (f == SOCAM_PCLK_SAMPLE_RISING || f == SOCAM_PCLK_SAMPLE_FALLING)
			flags ^= SOCAM_PCLK_SAMPLE_RISING | SOCAM_PCLK_SAMPLE_FALLING;
	}

	return flags;
}
EXPORT_SYMBOL(soc_camera_apply_sensor_flags);

static int soc_camera_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);

	WARN_ON(priv != file->private_data);

	/* limit format to hardware capabilities */
	return ici->ops->try_fmt(icd, f);
}

static int soc_camera_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	int ret = 0;

	if (inp->index != 0)
		return -EINVAL;

	if (icd->ops->enum_input)
		ret = icd->ops->enum_input(icd, inp);
	else {
		/* default is camera */
		inp->type = V4L2_INPUT_TYPE_CAMERA;
		inp->std  = V4L2_STD_UNKNOWN;
		strcpy(inp->name, "Camera");
	}

	return ret;
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

static int soc_camera_s_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	int ret = 0;

	if (icd->ops->set_std)
		ret = icd->ops->set_std(icd, a);

	return ret;
}

static int soc_camera_reqbufs(struct file *file, void *priv,
			      struct v4l2_requestbuffers *p)
{
	int ret;
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);

	WARN_ON(priv != file->private_data);

	dev_dbg(&icd->dev, "%s: %d\n", __func__, p->memory);

	ret = videobuf_reqbufs(&icf->vb_vidq, p);
	if (ret < 0)
		return ret;

	return ici->ops->reqbufs(icf, p);
}

static int soc_camera_querybuf(struct file *file, void *priv,
			       struct v4l2_buffer *p)
{
	struct soc_camera_file *icf = file->private_data;

	WARN_ON(priv != file->private_data);

	return videobuf_querybuf(&icf->vb_vidq, p);
}

static int soc_camera_qbuf(struct file *file, void *priv,
			   struct v4l2_buffer *p)
{
	struct soc_camera_file *icf = file->private_data;

	WARN_ON(priv != file->private_data);

	return videobuf_qbuf(&icf->vb_vidq, p);
}

static int soc_camera_dqbuf(struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct soc_camera_file *icf = file->private_data;

	WARN_ON(priv != file->private_data);

	return videobuf_dqbuf(&icf->vb_vidq, p, file->f_flags & O_NONBLOCK);
}

static int soc_camera_init_user_formats(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int i, fmts = 0;

	if (!ici->ops->get_formats)
		/*
		 * Fallback mode - the host will have to serve all
		 * sensor-provided formats one-to-one to the user
		 */
		fmts = icd->num_formats;
	else
		/*
		 * First pass - only count formats this host-sensor
		 * configuration can provide
		 */
		for (i = 0; i < icd->num_formats; i++)
			fmts += ici->ops->get_formats(icd, i, NULL);

	if (!fmts)
		return -ENXIO;

	icd->user_formats =
		vmalloc(fmts * sizeof(struct soc_camera_format_xlate));
	if (!icd->user_formats)
		return -ENOMEM;

	icd->num_user_formats = fmts;

	dev_dbg(&icd->dev, "Found %d supported formats.\n", fmts);

	/* Second pass - actually fill data formats */
	fmts = 0;
	for (i = 0; i < icd->num_formats; i++)
		if (!ici->ops->get_formats) {
			icd->user_formats[i].host_fmt = icd->formats + i;
			icd->user_formats[i].cam_fmt = icd->formats + i;
			icd->user_formats[i].buswidth = icd->formats[i].depth;
		} else {
			fmts += ici->ops->get_formats(icd, i,
						      &icd->user_formats[fmts]);
		}

	icd->current_fmt = icd->user_formats[0].host_fmt;

	return 0;
}

static void soc_camera_free_user_formats(struct soc_camera_device *icd)
{
	vfree(icd->user_formats);
}

/* Called with .vb_lock held */
static int soc_camera_set_fmt(struct soc_camera_file *icf,
			      struct v4l2_format *f)
{
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	/* We always call try_fmt() before set_fmt() or set_crop() */
	ret = ici->ops->try_fmt(icd, f);
	if (ret < 0)
		return ret;

	ret = ici->ops->set_fmt(icd, f);
	if (ret < 0) {
		return ret;
	} else if (!icd->current_fmt ||
		   icd->current_fmt->fourcc != pix->pixelformat) {
		dev_err(ici->dev,
			"Host driver hasn't set up current format correctly!\n");
		return -EINVAL;
	}

	icd->width		= pix->width;
	icd->height		= pix->height;
	icf->vb_vidq.field	=
		icd->field	= pix->field;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		dev_warn(&icd->dev, "Attention! Wrong buf-type %d\n",
			 f->type);

	dev_dbg(&icd->dev, "set width: %d height: %d\n",
		icd->width, icd->height);

	/* set physical bus parameters */
	return ici->ops->set_bus_param(icd, pix->pixelformat);
}

static int soc_camera_open(struct file *file)
{
	struct video_device *vdev;
	struct soc_camera_device *icd;
	struct soc_camera_host *ici;
	struct soc_camera_file *icf;
	int ret;

	icf = vmalloc(sizeof(*icf));
	if (!icf)
		return -ENOMEM;

	/*
	 * It is safe to dereference these pointers now as long as a user has
	 * the video device open - we are protected by the held cdev reference.
	 */

	vdev = video_devdata(file);
	icd = container_of(vdev->parent, struct soc_camera_device, dev);
	ici = to_soc_camera_host(icd->dev.parent);

	if (!try_module_get(icd->ops->owner)) {
		dev_err(&icd->dev, "Couldn't lock sensor driver.\n");
		ret = -EINVAL;
		goto emgd;
	}

	if (!try_module_get(ici->ops->owner)) {
		dev_err(&icd->dev, "Couldn't lock capture bus driver.\n");
		ret = -EINVAL;
		goto emgi;
	}

	/* Protect against icd->remove() until we module_get() both drivers. */
	mutex_lock(&icd->video_lock);

	icf->icd = icd;
	icd->use_count++;

	/* Now we really have to activate the camera */
	if (icd->use_count == 1) {
		/* Restore parameters before the last close() per V4L2 API */
		struct v4l2_format f = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.fmt.pix = {
				.width		= icd->width,
				.height		= icd->height,
				.field		= icd->field,
				.pixelformat	= icd->current_fmt->fourcc,
				.colorspace	= icd->current_fmt->colorspace,
			},
		};

		ret = ici->ops->add(icd);
		if (ret < 0) {
			dev_err(&icd->dev, "Couldn't activate the camera: %d\n", ret);
			goto eiciadd;
		}

		/* Try to configure with default parameters */
		ret = soc_camera_set_fmt(icf, &f);
		if (ret < 0)
			goto esfmt;
	}

	mutex_unlock(&icd->video_lock);

	file->private_data = icf;
	dev_dbg(&icd->dev, "camera device open\n");

	ici->ops->init_videobuf(&icf->vb_vidq, icd);

	return 0;

	/*
	 * First three errors are entered with the .video_lock held
	 * and use_count == 1
	 */
esfmt:
	ici->ops->remove(icd);
eiciadd:
	icd->use_count--;
	mutex_unlock(&icd->video_lock);
	module_put(ici->ops->owner);
emgi:
	module_put(icd->ops->owner);
emgd:
	vfree(icf);
	return ret;
}

static int soc_camera_close(struct file *file)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct video_device *vdev = icd->vdev;

	mutex_lock(&icd->video_lock);
	icd->use_count--;
	if (!icd->use_count)
		ici->ops->remove(icd);

	mutex_unlock(&icd->video_lock);

	module_put(icd->ops->owner);
	module_put(ici->ops->owner);

	vfree(icf);

	dev_dbg(vdev->parent, "camera device close\n");

	return 0;
}

static ssize_t soc_camera_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct video_device *vdev = icd->vdev;
	int err = -EINVAL;

	dev_err(vdev->parent, "camera device read not implemented\n");

	return err;
}

static int soc_camera_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	int err;

	dev_dbg(&icd->dev, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	err = videobuf_mmap_mapper(&icf->vb_vidq, vma);

	dev_dbg(&icd->dev, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start,
		err);

	return err;
}

static unsigned int soc_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);

	if (list_empty(&icf->vb_vidq.stream)) {
		dev_err(&icd->dev, "Trying to poll with no queued buffers!\n");
		return POLLERR;
	}

	return ici->ops->poll(file, pt);
}

static struct v4l2_file_operations soc_camera_fops = {
	.owner		= THIS_MODULE,
	.open		= soc_camera_open,
	.release	= soc_camera_close,
	.ioctl		= video_ioctl2,
	.read		= soc_camera_read,
	.mmap		= soc_camera_mmap,
	.poll		= soc_camera_poll,
};

static int soc_camera_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	int ret;

	WARN_ON(priv != file->private_data);

	mutex_lock(&icf->vb_vidq.vb_lock);

	if (videobuf_queue_is_busy(&icf->vb_vidq)) {
		dev_err(&icd->dev, "S_FMT denied: queue busy\n");
		ret = -EBUSY;
		goto unlock;
	}

	ret = soc_camera_set_fmt(icf, f);

unlock:
	mutex_unlock(&icf->vb_vidq.vb_lock);

	return ret;
}

static int soc_camera_enum_fmt_vid_cap(struct file *file, void  *priv,
				       struct v4l2_fmtdesc *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	const struct soc_camera_data_format *format;

	WARN_ON(priv != file->private_data);

	if (f->index >= icd->num_user_formats)
		return -EINVAL;

	format = icd->user_formats[f->index].host_fmt;

	strlcpy(f->description, format->name, sizeof(f->description));
	f->pixelformat = format->fourcc;
	return 0;
}

static int soc_camera_g_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	WARN_ON(priv != file->private_data);

	pix->width		= icd->width;
	pix->height		= icd->height;
	pix->field		= icf->vb_vidq.field;
	pix->pixelformat	= icd->current_fmt->fourcc;
	pix->bytesperline	= pix->width *
		DIV_ROUND_UP(icd->current_fmt->depth, 8);
	pix->sizeimage		= pix->height * pix->bytesperline;
	dev_dbg(&icd->dev, "current_fmt->fourcc: 0x%08x\n",
		icd->current_fmt->fourcc);
	return 0;
}

static int soc_camera_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);

	WARN_ON(priv != file->private_data);

	strlcpy(cap->driver, ici->drv_name, sizeof(cap->driver));
	return ici->ops->querycap(ici, cap);
}

static int soc_camera_streamon(struct file *file, void *priv,
			       enum v4l2_buf_type i)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	int ret;

	WARN_ON(priv != file->private_data);

	dev_dbg(&icd->dev, "%s\n", __func__);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	mutex_lock(&icd->video_lock);

	icd->ops->start_capture(icd);

	/* This calls buf_queue from host driver's videobuf_queue_ops */
	ret = videobuf_streamon(&icf->vb_vidq);

	mutex_unlock(&icd->video_lock);

	return ret;
}

static int soc_camera_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type i)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	WARN_ON(priv != file->private_data);

	dev_dbg(&icd->dev, "%s\n", __func__);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	mutex_lock(&icd->video_lock);

	/* This calls buf_release from host driver's videobuf_queue_ops for all
	 * remaining buffers. When the last buffer is freed, stop capture */
	videobuf_streamoff(&icf->vb_vidq);

	icd->ops->stop_capture(icd);

	mutex_unlock(&icd->video_lock);

	return 0;
}

static int soc_camera_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	int i;

	WARN_ON(priv != file->private_data);

	if (!qc->id)
		return -EINVAL;

	for (i = 0; i < icd->ops->num_controls; i++)
		if (qc->id == icd->ops->controls[i].id) {
			memcpy(qc, &(icd->ops->controls[i]),
				sizeof(*qc));
			return 0;
		}

	return -EINVAL;
}

static int soc_camera_g_ctrl(struct file *file, void *priv,
			     struct v4l2_control *ctrl)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	WARN_ON(priv != file->private_data);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		if (icd->gain == (unsigned short)~0)
			return -EINVAL;
		ctrl->value = icd->gain;
		return 0;
	case V4L2_CID_EXPOSURE:
		if (icd->exposure == (unsigned short)~0)
			return -EINVAL;
		ctrl->value = icd->exposure;
		return 0;
	}

	if (icd->ops->get_control)
		return icd->ops->get_control(icd, ctrl);
	return -EINVAL;
}

static int soc_camera_s_ctrl(struct file *file, void *priv,
			     struct v4l2_control *ctrl)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	WARN_ON(priv != file->private_data);

	if (icd->ops->set_control)
		return icd->ops->set_control(icd, ctrl);
	return -EINVAL;
}

static int soc_camera_cropcap(struct file *file, void *fh,
			      struct v4l2_cropcap *a)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->bounds.left			= icd->x_min;
	a->bounds.top			= icd->y_min;
	a->bounds.width			= icd->width_max;
	a->bounds.height		= icd->height_max;
	a->defrect.left			= icd->x_min;
	a->defrect.top			= icd->y_min;
	a->defrect.width		= DEFAULT_WIDTH;
	a->defrect.height		= DEFAULT_HEIGHT;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int soc_camera_g_crop(struct file *file, void *fh,
			     struct v4l2_crop *a)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->c.left	= icd->x_current;
	a->c.top	= icd->y_current;
	a->c.width	= icd->width;
	a->c.height	= icd->height;

	return 0;
}

static int soc_camera_s_crop(struct file *file, void *fh,
			     struct v4l2_crop *a)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int ret;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* Cropping is allowed during a running capture, guard consistency */
	mutex_lock(&icf->vb_vidq.vb_lock);

	ret = ici->ops->set_crop(icd, &a->c);
	if (!ret) {
		icd->width	= a->c.width;
		icd->height	= a->c.height;
		icd->x_current	= a->c.left;
		icd->y_current	= a->c.top;
	}

	mutex_unlock(&icf->vb_vidq.vb_lock);

	return ret;
}

static int soc_camera_g_chip_ident(struct file *file, void *fh,
				   struct v4l2_dbg_chip_ident *id)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	if (!icd->ops->get_chip_id)
		return -EINVAL;

	return icd->ops->get_chip_id(icd, id);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int soc_camera_g_register(struct file *file, void *fh,
				 struct v4l2_dbg_register *reg)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	if (!icd->ops->get_register)
		return -EINVAL;

	return icd->ops->get_register(icd, reg);
}

static int soc_camera_s_register(struct file *file, void *fh,
				 struct v4l2_dbg_register *reg)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	if (!icd->ops->set_register)
		return -EINVAL;

	return icd->ops->set_register(icd, reg);
}
#endif

static int device_register_link(struct soc_camera_device *icd)
{
	int ret = dev_set_name(&icd->dev, "%u-%u", icd->iface, icd->devnum);

	if (!ret)
		ret = device_register(&icd->dev);

	if (ret < 0) {
		/* Prevent calling device_unregister() */
		icd->dev.parent = NULL;
		dev_err(&icd->dev, "Cannot register device: %d\n", ret);
	/* Even if probe() was unsuccessful for all registered drivers,
	 * device_register() returns 0, and we add the link, just to
	 * document this camera's control device */
	} else if (icd->control)
		/* Have to sysfs_remove_link() before device_unregister()? */
		if (sysfs_create_link(&icd->dev.kobj, &icd->control->kobj,
				      "control"))
			dev_warn(&icd->dev,
				 "Failed creating the control symlink\n");
	return ret;
}

/* So far this function cannot fail */
static void scan_add_host(struct soc_camera_host *ici)
{
	struct soc_camera_device *icd;

	mutex_lock(&list_lock);

	list_for_each_entry(icd, &devices, list) {
		if (icd->iface == ici->nr) {
			icd->dev.parent = ici->dev;
			device_register_link(icd);
		}
	}

	mutex_unlock(&list_lock);
}

/* return: 0 if no match found or a match found and
 * device_register() successful, error code otherwise */
static int scan_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici;
	int ret = 0;

	mutex_lock(&list_lock);

	list_add_tail(&icd->list, &devices);

	/* Watch out for class_for_each_device / class_find_device API by
	 * Dave Young <hidave.darkstar@gmail.com> */
	list_for_each_entry(ici, &hosts, list) {
		if (icd->iface == ici->nr) {
			ret = 1;
			icd->dev.parent = ici->dev;
			break;
		}
	}

	mutex_unlock(&list_lock);

	if (ret)
		ret = device_register_link(icd);

	return ret;
}

static int soc_camera_probe(struct device *dev)
{
	struct soc_camera_device *icd = to_soc_camera_dev(dev);
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int ret;

	/*
	 * Possible race scenario:
	 * modprobe <camera-host-driver> triggers __func__
	 * at this moment respective <camera-sensor-driver> gets rmmod'ed
	 * to protect take module references.
	 */

	if (!try_module_get(icd->ops->owner)) {
		dev_err(&icd->dev, "Couldn't lock sensor driver.\n");
		ret = -EINVAL;
		goto emgd;
	}

	if (!try_module_get(ici->ops->owner)) {
		dev_err(&icd->dev, "Couldn't lock capture bus driver.\n");
		ret = -EINVAL;
		goto emgi;
	}

	mutex_lock(&icd->video_lock);

	/* We only call ->add() here to activate and probe the camera.
	 * We shall ->remove() and deactivate it immediately afterwards. */
	ret = ici->ops->add(icd);
	if (ret < 0)
		goto eiadd;

	ret = icd->ops->probe(icd);
	if (ret >= 0) {
		const struct v4l2_queryctrl *qctrl;

		qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_GAIN);
		icd->gain = qctrl ? qctrl->default_value : (unsigned short)~0;
		qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_EXPOSURE);
		icd->exposure = qctrl ? qctrl->default_value :
			(unsigned short)~0;

		ret = soc_camera_init_user_formats(icd);
		if (ret < 0) {
			if (icd->ops->remove)
				icd->ops->remove(icd);
			goto eiufmt;
		}

		icd->height	= DEFAULT_HEIGHT;
		icd->width	= DEFAULT_WIDTH;
		icd->field	= V4L2_FIELD_ANY;
	}

eiufmt:
	ici->ops->remove(icd);
eiadd:
	mutex_unlock(&icd->video_lock);
	module_put(ici->ops->owner);
emgi:
	module_put(icd->ops->owner);
emgd:
	return ret;
}

/* This is called on device_unregister, which only means we have to disconnect
 * from the host, but not remove ourselves from the device list */
static int soc_camera_remove(struct device *dev)
{
	struct soc_camera_device *icd = to_soc_camera_dev(dev);

	mutex_lock(&icd->video_lock);
	if (icd->ops->remove)
		icd->ops->remove(icd);
	mutex_unlock(&icd->video_lock);

	soc_camera_free_user_formats(icd);

	return 0;
}

static int soc_camera_suspend(struct device *dev, pm_message_t state)
{
	struct soc_camera_device *icd = to_soc_camera_dev(dev);
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int ret = 0;

	if (ici->ops->suspend)
		ret = ici->ops->suspend(icd, state);

	return ret;
}

static int soc_camera_resume(struct device *dev)
{
	struct soc_camera_device *icd = to_soc_camera_dev(dev);
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int ret = 0;

	if (ici->ops->resume)
		ret = ici->ops->resume(icd);

	return ret;
}

static struct bus_type soc_camera_bus_type = {
	.name		= "soc-camera",
	.probe		= soc_camera_probe,
	.remove		= soc_camera_remove,
	.suspend	= soc_camera_suspend,
	.resume		= soc_camera_resume,
};

static struct device_driver ic_drv = {
	.name	= "camera",
	.bus	= &soc_camera_bus_type,
	.owner	= THIS_MODULE,
};

static void dummy_release(struct device *dev)
{
}

int soc_camera_host_register(struct soc_camera_host *ici)
{
	struct soc_camera_host *ix;

	if (!ici || !ici->ops ||
	    !ici->ops->try_fmt ||
	    !ici->ops->set_fmt ||
	    !ici->ops->set_crop ||
	    !ici->ops->set_bus_param ||
	    !ici->ops->querycap ||
	    !ici->ops->init_videobuf ||
	    !ici->ops->reqbufs ||
	    !ici->ops->add ||
	    !ici->ops->remove ||
	    !ici->ops->poll ||
	    !ici->dev)
		return -EINVAL;

	mutex_lock(&list_lock);
	list_for_each_entry(ix, &hosts, list) {
		if (ix->nr == ici->nr) {
			mutex_unlock(&list_lock);
			return -EBUSY;
		}
	}

	dev_set_drvdata(ici->dev, ici);

	list_add_tail(&ici->list, &hosts);
	mutex_unlock(&list_lock);

	scan_add_host(ici);

	return 0;
}
EXPORT_SYMBOL(soc_camera_host_register);

/* Unregister all clients! */
void soc_camera_host_unregister(struct soc_camera_host *ici)
{
	struct soc_camera_device *icd;

	mutex_lock(&list_lock);

	list_del(&ici->list);

	list_for_each_entry(icd, &devices, list) {
		if (icd->dev.parent == ici->dev) {
			device_unregister(&icd->dev);
			/* Not before device_unregister(), .remove
			 * needs parent to call ici->ops->remove() */
			icd->dev.parent = NULL;
			memset(&icd->dev.kobj, 0, sizeof(icd->dev.kobj));
		}
	}

	mutex_unlock(&list_lock);

	dev_set_drvdata(ici->dev, NULL);
}
EXPORT_SYMBOL(soc_camera_host_unregister);

/* Image capture device */
int soc_camera_device_register(struct soc_camera_device *icd)
{
	struct soc_camera_device *ix;
	int num = -1, i;

	if (!icd || !icd->ops ||
	    !icd->ops->probe ||
	    !icd->ops->init ||
	    !icd->ops->release ||
	    !icd->ops->start_capture ||
	    !icd->ops->stop_capture ||
	    !icd->ops->set_crop ||
	    !icd->ops->set_fmt ||
	    !icd->ops->try_fmt ||
	    !icd->ops->query_bus_param ||
	    !icd->ops->set_bus_param)
		return -EINVAL;

	for (i = 0; i < 256 && num < 0; i++) {
		num = i;
		list_for_each_entry(ix, &devices, list) {
			if (ix->iface == icd->iface && ix->devnum == i) {
				num = -1;
				break;
			}
		}
	}

	if (num < 0)
		/* ok, we have 256 cameras on this host...
		 * man, stay reasonable... */
		return -ENOMEM;

	icd->devnum = num;
	icd->dev.bus = &soc_camera_bus_type;

	icd->dev.release	= dummy_release;
	icd->use_count		= 0;
	icd->host_priv		= NULL;
	mutex_init(&icd->video_lock);

	return scan_add_device(icd);
}
EXPORT_SYMBOL(soc_camera_device_register);

void soc_camera_device_unregister(struct soc_camera_device *icd)
{
	mutex_lock(&list_lock);
	list_del(&icd->list);

	/* The bus->remove will be eventually called */
	if (icd->dev.parent)
		device_unregister(&icd->dev);
	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(soc_camera_device_unregister);

static const struct v4l2_ioctl_ops soc_camera_ioctl_ops = {
	.vidioc_querycap	 = soc_camera_querycap,
	.vidioc_g_fmt_vid_cap    = soc_camera_g_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = soc_camera_enum_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap    = soc_camera_s_fmt_vid_cap,
	.vidioc_enum_input	 = soc_camera_enum_input,
	.vidioc_g_input		 = soc_camera_g_input,
	.vidioc_s_input		 = soc_camera_s_input,
	.vidioc_s_std		 = soc_camera_s_std,
	.vidioc_reqbufs		 = soc_camera_reqbufs,
	.vidioc_try_fmt_vid_cap  = soc_camera_try_fmt_vid_cap,
	.vidioc_querybuf	 = soc_camera_querybuf,
	.vidioc_qbuf		 = soc_camera_qbuf,
	.vidioc_dqbuf		 = soc_camera_dqbuf,
	.vidioc_streamon	 = soc_camera_streamon,
	.vidioc_streamoff	 = soc_camera_streamoff,
	.vidioc_queryctrl	 = soc_camera_queryctrl,
	.vidioc_g_ctrl		 = soc_camera_g_ctrl,
	.vidioc_s_ctrl		 = soc_camera_s_ctrl,
	.vidioc_cropcap		 = soc_camera_cropcap,
	.vidioc_g_crop		 = soc_camera_g_crop,
	.vidioc_s_crop		 = soc_camera_s_crop,
	.vidioc_g_chip_ident     = soc_camera_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register	 = soc_camera_g_register,
	.vidioc_s_register	 = soc_camera_s_register,
#endif
};

/*
 * Usually called from the struct soc_camera_ops .probe() method, i.e., from
 * soc_camera_probe() above with .video_lock held
 */
int soc_camera_video_start(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int err = -ENOMEM;
	struct video_device *vdev;

	if (!icd->dev.parent)
		return -ENODEV;

	vdev = video_device_alloc();
	if (!vdev)
		goto evidallocd;
	dev_dbg(ici->dev, "Allocated video_device %p\n", vdev);

	strlcpy(vdev->name, ici->drv_name, sizeof(vdev->name));

	vdev->parent		= &icd->dev;
	vdev->current_norm	= V4L2_STD_UNKNOWN;
	vdev->fops		= &soc_camera_fops;
	vdev->ioctl_ops		= &soc_camera_ioctl_ops;
	vdev->release		= video_device_release;
	vdev->minor		= -1;
	vdev->tvnorms		= V4L2_STD_UNKNOWN,

	err = video_register_device(vdev, VFL_TYPE_GRABBER, vdev->minor);
	if (err < 0) {
		dev_err(vdev->parent, "video_register_device failed\n");
		goto evidregd;
	}
	icd->vdev = vdev;

	return 0;

evidregd:
	video_device_release(vdev);
evidallocd:
	return err;
}
EXPORT_SYMBOL(soc_camera_video_start);

/* Called from client .remove() methods with .video_lock held */
void soc_camera_video_stop(struct soc_camera_device *icd)
{
	struct video_device *vdev = icd->vdev;

	dev_dbg(&icd->dev, "%s\n", __func__);

	if (!icd->dev.parent || !vdev)
		return;

	video_unregister_device(vdev);
	icd->vdev = NULL;
}
EXPORT_SYMBOL(soc_camera_video_stop);

static int __devinit soc_camera_pdrv_probe(struct platform_device *pdev)
{
	struct soc_camera_link *icl = pdev->dev.platform_data;
	struct i2c_adapter *adap;
	struct i2c_client *client;

	if (!icl)
		return -EINVAL;

	adap = i2c_get_adapter(icl->i2c_adapter_id);
	if (!adap) {
		dev_warn(&pdev->dev, "Cannot get adapter #%d. No driver?\n",
			 icl->i2c_adapter_id);
		/* -ENODEV and -ENXIO do not produce an error on probe()... */
		return -ENOENT;
	}

	icl->board_info->platform_data = icl;
	client = i2c_new_device(adap, icl->board_info);
	if (!client) {
		i2c_put_adapter(adap);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, client);

	return 0;
}

static int __devexit soc_camera_pdrv_remove(struct platform_device *pdev)
{
	struct i2c_client *client = platform_get_drvdata(pdev);

	if (!client)
		return -ENODEV;

	i2c_unregister_device(client);
	i2c_put_adapter(client->adapter);

	return 0;
}

static struct platform_driver __refdata soc_camera_pdrv = {
	.probe	= soc_camera_pdrv_probe,
	.remove	= __devexit_p(soc_camera_pdrv_remove),
	.driver	= {
		.name = "soc-camera-pdrv",
		.owner = THIS_MODULE,
	},
};

static int __init soc_camera_init(void)
{
	int ret = bus_register(&soc_camera_bus_type);
	if (ret)
		return ret;
	ret = driver_register(&ic_drv);
	if (ret)
		goto edrvr;

	ret = platform_driver_register(&soc_camera_pdrv);
	if (ret)
		goto epdr;

	return 0;

epdr:
	driver_unregister(&ic_drv);
edrvr:
	bus_unregister(&soc_camera_bus_type);
	return ret;
}

static void __exit soc_camera_exit(void)
{
	platform_driver_unregister(&soc_camera_pdrv);
	driver_unregister(&ic_drv);
	bus_unregister(&soc_camera_bus_type);
}

module_init(soc_camera_init);
module_exit(soc_camera_exit);

MODULE_DESCRIPTION("Image capture bus driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-camera-pdrv");
