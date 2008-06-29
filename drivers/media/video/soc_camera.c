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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/soc_camera.h>

static LIST_HEAD(hosts);
static LIST_HEAD(devices);
static DEFINE_MUTEX(list_lock);
static DEFINE_MUTEX(video_lock);

const static struct soc_camera_data_format*
format_by_fourcc(struct soc_camera_device *icd, unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < icd->num_formats; i++)
		if (icd->formats[i].fourcc == fourcc)
			return icd->formats + i;
	return NULL;
}

static int soc_camera_try_fmt_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	enum v4l2_field field;
	const struct soc_camera_data_format *fmt;
	int ret;

	WARN_ON(priv != file->private_data);

	fmt = format_by_fourcc(icd, f->fmt.pix.pixelformat);
	if (!fmt) {
		dev_dbg(&icd->dev, "invalid format 0x%08x\n",
			f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	dev_dbg(&icd->dev, "fmt: 0x%08x\n", fmt->fourcc);

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_NONE;
	} else if (V4L2_FIELD_NONE != field) {
		dev_err(&icd->dev, "Field type invalid.\n");
		return -EINVAL;
	}

	/* test physical bus parameters */
	ret = ici->ops->try_bus_param(icd, f->fmt.pix.pixelformat);
	if (ret)
		return ret;

	/* limit format to hardware capabilities */
	ret = ici->ops->try_fmt_cap(icd, f);

	/* calculate missing fields */
	f->fmt.pix.field = field;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return ret;
}

static int soc_camera_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_UNKNOWN;
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

static int soc_camera_s_std(struct file *file, void *priv, v4l2_std_id *a)
{
	return 0;
}

static int soc_camera_reqbufs(struct file *file, void *priv,
			      struct v4l2_requestbuffers *p)
{
	int ret;
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);

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

static int soc_camera_open(struct inode *inode, struct file *file)
{
	struct video_device *vdev;
	struct soc_camera_device *icd;
	struct soc_camera_host *ici;
	struct soc_camera_file *icf;
	spinlock_t *lock;
	int ret;

	icf = vmalloc(sizeof(*icf));
	if (!icf)
		return -ENOMEM;

	/* Protect against icd->remove() until we module_get() both drivers. */
	mutex_lock(&video_lock);

	vdev = video_devdata(file);
	icd = container_of(vdev->dev, struct soc_camera_device, dev);
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

	icf->icd = icd;

	icf->lock = ici->ops->spinlock_alloc(icf);
	if (!icf->lock) {
		ret = -ENOMEM;
		goto esla;
	}

	icd->use_count++;

	/* Now we really have to activate the camera */
	if (icd->use_count == 1) {
		ret = ici->ops->add(icd);
		if (ret < 0) {
			dev_err(&icd->dev, "Couldn't activate the camera: %d\n", ret);
			icd->use_count--;
			goto eiciadd;
		}
	}

	mutex_unlock(&video_lock);

	file->private_data = icf;
	dev_dbg(&icd->dev, "camera device open\n");

	/* We must pass NULL as dev pointer, then all pci_* dma operations
	 * transform to normal dma_* ones. */
	videobuf_queue_sg_init(&icf->vb_vidq, ici->vbq_ops, NULL, icf->lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				ici->msize, icd);

	return 0;

	/* All errors are entered with the video_lock held */
eiciadd:
	lock = icf->lock;
	icf->lock = NULL;
	if (ici->ops->spinlock_free)
		ici->ops->spinlock_free(lock);
esla:
	module_put(ici->ops->owner);
emgi:
	module_put(icd->ops->owner);
emgd:
	mutex_unlock(&video_lock);
	vfree(icf);
	return ret;
}

static int soc_camera_close(struct inode *inode, struct file *file)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct video_device *vdev = icd->vdev;
	spinlock_t *lock = icf->lock;

	mutex_lock(&video_lock);
	icd->use_count--;
	if (!icd->use_count)
		ici->ops->remove(icd);
	icf->lock = NULL;
	if (ici->ops->spinlock_free)
		ici->ops->spinlock_free(lock);
	module_put(icd->ops->owner);
	module_put(ici->ops->owner);
	mutex_unlock(&video_lock);

	vfree(icf);

	dev_dbg(vdev->dev, "camera device close\n");

	return 0;
}

static ssize_t soc_camera_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct video_device *vdev = icd->vdev;
	int err = -EINVAL;

	dev_err(vdev->dev, "camera device read not implemented\n");

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
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);

	if (list_empty(&icf->vb_vidq.stream)) {
		dev_err(&icd->dev, "Trying to poll with no queued buffers!\n");
		return POLLERR;
	}

	return ici->ops->poll(file, pt);
}


static struct file_operations soc_camera_fops = {
	.owner		= THIS_MODULE,
	.open		= soc_camera_open,
	.release	= soc_camera_close,
	.ioctl		= video_ioctl2,
	.read		= soc_camera_read,
	.mmap		= soc_camera_mmap,
	.poll		= soc_camera_poll,
	.llseek		= no_llseek,
};


static int soc_camera_s_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	int ret;
	struct v4l2_rect rect;
	const static struct soc_camera_data_format *data_fmt;

	WARN_ON(priv != file->private_data);

	data_fmt = format_by_fourcc(icd, f->fmt.pix.pixelformat);
	if (!data_fmt)
		return -EINVAL;

	/* buswidth may be further adjusted by the ici */
	icd->buswidth = data_fmt->depth;

	ret = soc_camera_try_fmt_cap(file, icf, f);
	if (ret < 0)
		return ret;

	rect.left	= icd->x_current;
	rect.top	= icd->y_current;
	rect.width	= f->fmt.pix.width;
	rect.height	= f->fmt.pix.height;
	ret = ici->ops->set_fmt_cap(icd, f->fmt.pix.pixelformat, &rect);
	if (ret < 0)
		return ret;

	icd->current_fmt	= data_fmt;
	icd->width		= rect.width;
	icd->height		= rect.height;
	icf->vb_vidq.field	= f->fmt.pix.field;
	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != f->type)
		dev_warn(&icd->dev, "Attention! Wrong buf-type %d\n",
			 f->type);

	dev_dbg(&icd->dev, "set width: %d height: %d\n",
		icd->width, icd->height);

	/* set physical bus parameters */
	return ici->ops->set_bus_param(icd, f->fmt.pix.pixelformat);
}

static int soc_camera_enum_fmt_cap(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	const struct soc_camera_data_format *format;

	WARN_ON(priv != file->private_data);

	if (f->index >= icd->num_formats)
		return -EINVAL;

	format = &icd->formats[f->index];

	strlcpy(f->description, format->name, sizeof(f->description));
	f->pixelformat = format->fourcc;
	return 0;
}

static int soc_camera_g_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	WARN_ON(priv != file->private_data);

	f->fmt.pix.width	= icd->width;
	f->fmt.pix.height	= icd->height;
	f->fmt.pix.field	= icf->vb_vidq.field;
	f->fmt.pix.pixelformat	= icd->current_fmt->fourcc;
	f->fmt.pix.bytesperline	=
		(f->fmt.pix.width * icd->current_fmt->depth) >> 3;
	f->fmt.pix.sizeimage	=
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	dev_dbg(&icd->dev, "current_fmt->fourcc: 0x%08x\n",
		icd->current_fmt->fourcc);
	return 0;
}

static int soc_camera_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);

	WARN_ON(priv != file->private_data);

	strlcpy(cap->driver, ici->drv_name, sizeof(cap->driver));
	return ici->ops->querycap(ici, cap);
}

static int soc_camera_streamon(struct file *file, void *priv,
			       enum v4l2_buf_type i)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	WARN_ON(priv != file->private_data);

	dev_dbg(&icd->dev, "%s\n", __func__);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	icd->ops->start_capture(icd);

	/* This calls buf_queue from host driver's videobuf_queue_ops */
	return videobuf_streamon(&icf->vb_vidq);
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

	/* This calls buf_release from host driver's videobuf_queue_ops for all
	 * remaining buffers. When the last buffer is freed, stop capture */
	videobuf_streamoff(&icf->vb_vidq);

	icd->ops->stop_capture(icd);

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
	a->defrect.width		= 640;
	a->defrect.height		= 480;
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
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	int ret;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = ici->ops->set_fmt_cap(icd, 0, &a->c);
	if (!ret) {
		icd->width	= a->c.width;
		icd->height	= a->c.height;
		icd->x_current	= a->c.left;
		icd->y_current	= a->c.top;
	}

	return ret;
}

static int soc_camera_g_chip_ident(struct file *file, void *fh,
				   struct v4l2_chip_ident *id)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	if (!icd->ops->get_chip_id)
		return -EINVAL;

	return icd->ops->get_chip_id(icd, id);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int soc_camera_g_register(struct file *file, void *fh,
				 struct v4l2_register *reg)
{
	struct soc_camera_file *icf = file->private_data;
	struct soc_camera_device *icd = icf->icd;

	if (!icd->ops->get_register)
		return -EINVAL;

	return icd->ops->get_register(icd, reg);
}

static int soc_camera_s_register(struct file *file, void *fh,
				 struct v4l2_register *reg)
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
	int ret = device_register(&icd->dev);

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
			icd->dev.parent = &ici->dev;
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
			icd->dev.parent = &ici->dev;
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
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	int ret;

	if (!icd->ops->probe)
		return -ENODEV;

	/* We only call ->add() here to activate and probe the camera.
	 * We shall ->remove() and deactivate it immediately afterwards. */
	ret = ici->ops->add(icd);
	if (ret < 0)
		return ret;

	ret = icd->ops->probe(icd);
	if (ret >= 0) {
		const struct v4l2_queryctrl *qctrl;

		qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_GAIN);
		icd->gain = qctrl ? qctrl->default_value : (unsigned short)~0;
		qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_EXPOSURE);
		icd->exposure = qctrl ? qctrl->default_value :
			(unsigned short)~0;
	}
	ici->ops->remove(icd);

	return ret;
}

/* This is called on device_unregister, which only means we have to disconnect
 * from the host, but not remove ourselves from the device list */
static int soc_camera_remove(struct device *dev)
{
	struct soc_camera_device *icd = to_soc_camera_dev(dev);

	if (icd->ops->remove)
		icd->ops->remove(icd);

	return 0;
}

static struct bus_type soc_camera_bus_type = {
	.name		= "soc-camera",
	.probe		= soc_camera_probe,
	.remove		= soc_camera_remove,
};

static struct device_driver ic_drv = {
	.name	= "camera",
	.bus	= &soc_camera_bus_type,
	.owner	= THIS_MODULE,
};

static void dummy_release(struct device *dev)
{
}

static spinlock_t *spinlock_alloc(struct soc_camera_file *icf)
{
	spinlock_t *lock = kmalloc(sizeof(spinlock_t), GFP_KERNEL);

	if (lock)
		spin_lock_init(lock);

	return lock;
}

static void spinlock_free(spinlock_t *lock)
{
	kfree(lock);
}

int soc_camera_host_register(struct soc_camera_host *ici)
{
	int ret;
	struct soc_camera_host *ix;

	if (!ici->vbq_ops || !ici->ops->add || !ici->ops->remove)
		return -EINVAL;

	/* Number might be equal to the platform device ID */
	sprintf(ici->dev.bus_id, "camera_host%d", ici->nr);

	mutex_lock(&list_lock);
	list_for_each_entry(ix, &hosts, list) {
		if (ix->nr == ici->nr) {
			mutex_unlock(&list_lock);
			return -EBUSY;
		}
	}

	list_add_tail(&ici->list, &hosts);
	mutex_unlock(&list_lock);

	ici->dev.release = dummy_release;

	ret = device_register(&ici->dev);

	if (ret)
		goto edevr;

	if (!ici->ops->spinlock_alloc) {
		ici->ops->spinlock_alloc = spinlock_alloc;
		ici->ops->spinlock_free = spinlock_free;
	}

	scan_add_host(ici);

	return 0;

edevr:
	mutex_lock(&list_lock);
	list_del(&ici->list);
	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(soc_camera_host_register);

/* Unregister all clients! */
void soc_camera_host_unregister(struct soc_camera_host *ici)
{
	struct soc_camera_device *icd;

	mutex_lock(&list_lock);

	list_del(&ici->list);

	list_for_each_entry(icd, &devices, list) {
		if (icd->dev.parent == &ici->dev) {
			device_unregister(&icd->dev);
			/* Not before device_unregister(), .remove
			 * needs parent to call ici->ops->remove() */
			icd->dev.parent = NULL;
			memset(&icd->dev.kobj, 0, sizeof(icd->dev.kobj));
		}
	}

	mutex_unlock(&list_lock);

	device_unregister(&ici->dev);
}
EXPORT_SYMBOL(soc_camera_host_unregister);

/* Image capture device */
int soc_camera_device_register(struct soc_camera_device *icd)
{
	struct soc_camera_device *ix;
	int num = -1, i;

	if (!icd)
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
	snprintf(icd->dev.bus_id, sizeof(icd->dev.bus_id),
		 "%u-%u", icd->iface, icd->devnum);

	icd->dev.release = dummy_release;

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
	dev_dbg(&ici->dev, "Allocated video_device %p\n", vdev);

	strlcpy(vdev->name, ici->drv_name, sizeof(vdev->name));
	/* Maybe better &ici->dev */
	vdev->dev		= &icd->dev;
	vdev->type		= VID_TYPE_CAPTURE;
	vdev->current_norm	= V4L2_STD_UNKNOWN;
	vdev->fops		= &soc_camera_fops;
	vdev->release		= video_device_release;
	vdev->minor		= -1;
	vdev->tvnorms		= V4L2_STD_UNKNOWN,
	vdev->vidioc_querycap	= soc_camera_querycap;
	vdev->vidioc_g_fmt_cap	= soc_camera_g_fmt_cap;
	vdev->vidioc_enum_fmt_cap = soc_camera_enum_fmt_cap;
	vdev->vidioc_s_fmt_cap	= soc_camera_s_fmt_cap;
	vdev->vidioc_enum_input	= soc_camera_enum_input;
	vdev->vidioc_g_input	= soc_camera_g_input;
	vdev->vidioc_s_input	= soc_camera_s_input;
	vdev->vidioc_s_std	= soc_camera_s_std;
	vdev->vidioc_reqbufs	= soc_camera_reqbufs;
	vdev->vidioc_try_fmt_cap = soc_camera_try_fmt_cap;
	vdev->vidioc_querybuf	= soc_camera_querybuf;
	vdev->vidioc_qbuf	= soc_camera_qbuf;
	vdev->vidioc_dqbuf	= soc_camera_dqbuf;
	vdev->vidioc_streamon	= soc_camera_streamon;
	vdev->vidioc_streamoff	= soc_camera_streamoff;
	vdev->vidioc_queryctrl	= soc_camera_queryctrl;
	vdev->vidioc_g_ctrl	= soc_camera_g_ctrl;
	vdev->vidioc_s_ctrl	= soc_camera_s_ctrl;
	vdev->vidioc_cropcap	= soc_camera_cropcap;
	vdev->vidioc_g_crop	= soc_camera_g_crop;
	vdev->vidioc_s_crop	= soc_camera_s_crop;
	vdev->vidioc_g_chip_ident = soc_camera_g_chip_ident;
#ifdef CONFIG_VIDEO_ADV_DEBUG
	vdev->vidioc_g_register	= soc_camera_g_register;
	vdev->vidioc_s_register	= soc_camera_s_register;
#endif

	icd->current_fmt = &icd->formats[0];

	err = video_register_device(vdev, VFL_TYPE_GRABBER, vdev->minor);
	if (err < 0) {
		dev_err(vdev->dev, "video_register_device failed\n");
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

void soc_camera_video_stop(struct soc_camera_device *icd)
{
	struct video_device *vdev = icd->vdev;

	dev_dbg(&icd->dev, "%s\n", __func__);

	if (!icd->dev.parent || !vdev)
		return;

	mutex_lock(&video_lock);
	video_unregister_device(vdev);
	icd->vdev = NULL;
	mutex_unlock(&video_lock);
}
EXPORT_SYMBOL(soc_camera_video_stop);

static int __init soc_camera_init(void)
{
	int ret = bus_register(&soc_camera_bus_type);
	if (ret)
		return ret;
	ret = driver_register(&ic_drv);
	if (ret)
		goto edrvr;

	return 0;

edrvr:
	bus_unregister(&soc_camera_bus_type);
	return ret;
}

static void __exit soc_camera_exit(void)
{
	driver_unregister(&ic_drv);
	bus_unregister(&soc_camera_bus_type);
}

module_init(soc_camera_init);
module_exit(soc_camera_exit);

MODULE_DESCRIPTION("Image capture bus driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
