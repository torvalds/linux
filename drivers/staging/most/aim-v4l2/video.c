/*
 * V4L2 AIM - V4L2 Application Interface Module for MostCore
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>

#include "mostcore.h"

#define V4L2_AIM_MAX_INPUT  1

static struct most_aim aim_info;

struct most_video_dev {
	struct most_interface *iface;
	int ch_idx;
	struct list_head list;
	bool mute;

	struct list_head pending_mbos;
	spinlock_t list_lock;

	struct v4l2_device v4l2_dev;
	atomic_t access_ref;
	struct video_device *vdev;
	unsigned int ctrl_input;

	struct mutex lock;

	wait_queue_head_t wait_data;
};

struct aim_fh {
	/* must be the first field of this struct! */
	struct v4l2_fh fh;
	struct most_video_dev *mdev;
	u32 offs;
};

static struct list_head video_devices = LIST_HEAD_INIT(video_devices);
static struct spinlock list_lock;

static inline bool data_ready(struct most_video_dev *mdev)
{
	return !list_empty(&mdev->pending_mbos);
}

static inline struct mbo *get_top_mbo(struct most_video_dev *mdev)
{
	return list_first_entry(&mdev->pending_mbos, struct mbo, list);
}

static int aim_vdev_open(struct file *filp)
{
	int ret;
	struct video_device *vdev = video_devdata(filp);
	struct most_video_dev *mdev = video_drvdata(filp);
	struct aim_fh *fh;

	v4l2_info(&mdev->v4l2_dev, "aim_vdev_open()\n");

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		break;
	default:
		return -EINVAL;
	}

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return -ENOMEM;

	if (!atomic_inc_and_test(&mdev->access_ref)) {
		v4l2_err(&mdev->v4l2_dev, "too many clients\n");
		ret = -EBUSY;
		goto err_dec;
	}

	fh->mdev = mdev;
	v4l2_fh_init(&fh->fh, vdev);
	filp->private_data = fh;

	v4l2_fh_add(&fh->fh);

	ret = most_start_channel(mdev->iface, mdev->ch_idx, &aim_info);
	if (ret) {
		v4l2_err(&mdev->v4l2_dev, "most_start_channel() failed\n");
		goto err_rm;
	}

	return 0;

err_rm:
	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);

err_dec:
	atomic_dec(&mdev->access_ref);
	kfree(fh);
	return ret;
}

static int aim_vdev_close(struct file *filp)
{
	struct aim_fh *fh = filp->private_data;
	struct most_video_dev *mdev = fh->mdev;
	struct mbo *mbo, *tmp;

	v4l2_info(&mdev->v4l2_dev, "aim_vdev_close()\n");

	/*
	 * We need to put MBOs back before we call most_stop_channel()
	 * to deallocate MBOs.
	 * From the other hand mostcore still calling rx_completion()
	 * to deliver MBOs until most_stop_channel() is called.
	 * Use mute to work around this issue.
	 * This must be implemented in core.
	 */

	spin_lock_irq(&mdev->list_lock);
	mdev->mute = true;
	list_for_each_entry_safe(mbo, tmp, &mdev->pending_mbos, list) {
		list_del(&mbo->list);
		spin_unlock_irq(&mdev->list_lock);
		most_put_mbo(mbo);
		spin_lock_irq(&mdev->list_lock);
	}
	spin_unlock_irq(&mdev->list_lock);
	most_stop_channel(mdev->iface, mdev->ch_idx, &aim_info);
	mdev->mute = false;

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);

	atomic_dec(&mdev->access_ref);
	kfree(fh);
	return 0;
}

static ssize_t aim_vdev_read(struct file *filp, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct aim_fh *fh = filp->private_data;
	struct most_video_dev *mdev = fh->mdev;
	int ret = 0;

	if (*pos)
		return -ESPIPE;

	if (!mdev)
		return -ENODEV;

	/* wait for the first buffer */
	if (!(filp->f_flags & O_NONBLOCK)) {
		if (wait_event_interruptible(mdev->wait_data, data_ready(mdev)))
			return -ERESTARTSYS;
	}

	if (!data_ready(mdev))
		return -EAGAIN;

	while (count > 0 && data_ready(mdev)) {
		struct mbo *const mbo = get_top_mbo(mdev);
		int const rem = mbo->processed_length - fh->offs;
		int const cnt = rem < count ? rem : count;

		if (copy_to_user(buf, mbo->virt_address + fh->offs, cnt)) {
			v4l2_err(&mdev->v4l2_dev, "read: copy_to_user failed\n");
			if (!ret)
				ret = -EFAULT;
			return ret;
		}

		fh->offs += cnt;
		count -= cnt;
		buf += cnt;
		ret += cnt;

		if (cnt >= rem) {
			fh->offs = 0;
			spin_lock_irq(&mdev->list_lock);
			list_del(&mbo->list);
			spin_unlock_irq(&mdev->list_lock);
			most_put_mbo(mbo);
		}
	}
	return ret;
}

static unsigned int aim_vdev_poll(struct file *filp, poll_table *wait)
{
	struct aim_fh *fh = filp->private_data;
	struct most_video_dev *mdev = fh->mdev;
	unsigned int mask = 0;

	/* only wait if no data is available */
	if (!data_ready(mdev))
		poll_wait(filp, &mdev->wait_data, wait);
	if (data_ready(mdev))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static void aim_set_format_struct(struct v4l2_format *f)
{
	f->fmt.pix.width = 8;
	f->fmt.pix.height = 8;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = 188 * 2;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.priv = 0;
}

static int aim_set_format(struct most_video_dev *mdev, unsigned int cmd,
			  struct v4l2_format *format)
{
	if (format->fmt.pix.pixelformat != V4L2_PIX_FMT_MPEG)
		return -EINVAL;

	if (cmd == VIDIOC_TRY_FMT)
		return 0;

	aim_set_format_struct(format);

	return 0;
}

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	v4l2_info(&mdev->v4l2_dev, "vidioc_querycap()\n");

	strlcpy(cap->driver, "v4l2_most_aim", sizeof(cap->driver));
	strlcpy(cap->card, "MOST", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "%s", mdev->iface->description);

	cap->capabilities =
		V4L2_CAP_READWRITE |
		V4L2_CAP_TUNER |
		V4L2_CAP_VIDEO_CAPTURE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	v4l2_info(&mdev->v4l2_dev, "vidioc_enum_fmt_vid_cap() %d\n", f->index);

	if (f->index)
		return -EINVAL;

	strcpy(f->description, "MPEG");
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	f->pixelformat = V4L2_PIX_FMT_MPEG;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	v4l2_info(&mdev->v4l2_dev, "vidioc_g_fmt_vid_cap()\n");

	aim_set_format_struct(f);
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	return aim_set_format(mdev, VIDIOC_TRY_FMT, f);
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	return aim_set_format(mdev, VIDIOC_S_FMT, f);
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	v4l2_info(&mdev->v4l2_dev, "vidioc_g_std()\n");

	*norm = V4L2_STD_UNKNOWN;
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
			     struct v4l2_input *input)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	if (input->index >= V4L2_AIM_MAX_INPUT)
		return -EINVAL;

	strcpy(input->name, "MOST Video");
	input->type |= V4L2_INPUT_TYPE_CAMERA;
	input->audioset = 0;

	input->std = mdev->vdev->tvnorms;

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;
	*i = mdev->ctrl_input;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int index)
{
	struct aim_fh *fh = priv;
	struct most_video_dev *mdev = fh->mdev;

	v4l2_info(&mdev->v4l2_dev, "vidioc_s_input(%d)\n", index);

	if (index >= V4L2_AIM_MAX_INPUT)
		return -EINVAL;
	mdev->ctrl_input = index;
	return 0;
}

static const struct v4l2_file_operations aim_fops = {
	.owner      = THIS_MODULE,
	.open       = aim_vdev_open,
	.release    = aim_vdev_close,
	.read       = aim_vdev_read,
	.poll       = aim_vdev_poll,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap            = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap    = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap       = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap     = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap       = vidioc_s_fmt_vid_cap,
	.vidioc_g_std               = vidioc_g_std,
	.vidioc_enum_input          = vidioc_enum_input,
	.vidioc_g_input             = vidioc_g_input,
	.vidioc_s_input             = vidioc_s_input,
};

static const struct video_device aim_videodev_template = {
	.fops = &aim_fops,
	.release = video_device_release,
	.ioctl_ops = &video_ioctl_ops,
	.tvnorms = V4L2_STD_UNKNOWN,
};

/**************************************************************************/

static struct most_video_dev *get_aim_dev(
	struct most_interface *iface, int channel_idx)
{
	struct most_video_dev *mdev;
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	list_for_each_entry(mdev, &video_devices, list) {
		if (mdev->iface == iface && mdev->ch_idx == channel_idx) {
			spin_unlock_irqrestore(&list_lock, flags);
			return mdev;
		}
	}
	spin_unlock_irqrestore(&list_lock, flags);
	return NULL;
}

static int aim_rx_data(struct mbo *mbo)
{
	unsigned long flags;
	struct most_video_dev *mdev =
		get_aim_dev(mbo->ifp, mbo->hdm_channel_id);

	if (!mdev)
		return -EIO;

	spin_lock_irqsave(&mdev->list_lock, flags);
	if (unlikely(mdev->mute)) {
		spin_unlock_irqrestore(&mdev->list_lock, flags);
		return -EIO;
	}

	list_add_tail(&mbo->list, &mdev->pending_mbos);
	spin_unlock_irqrestore(&mdev->list_lock, flags);
	wake_up_interruptible(&mdev->wait_data);
	return 0;
}

static int aim_register_videodev(struct most_video_dev *mdev)
{
	int ret;

	v4l2_info(&mdev->v4l2_dev, "aim_register_videodev()\n");

	init_waitqueue_head(&mdev->wait_data);

	/* allocate and fill v4l2 video struct */
	mdev->vdev = video_device_alloc();
	if (!mdev->vdev)
		return -ENOMEM;

	/* Fill the video capture device struct */
	*mdev->vdev = aim_videodev_template;
	mdev->vdev->v4l2_dev = &mdev->v4l2_dev;
	mdev->vdev->lock = &mdev->lock;
	snprintf(mdev->vdev->name, sizeof(mdev->vdev->name), "MOST: %s",
		 mdev->v4l2_dev.name);

	/* Register the v4l2 device */
	video_set_drvdata(mdev->vdev, mdev);
	ret = video_register_device(mdev->vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		v4l2_err(&mdev->v4l2_dev, "video_register_device failed (%d)\n",
			 ret);
		video_device_release(mdev->vdev);
	}

	return ret;
}

static void aim_unregister_videodev(struct most_video_dev *mdev)
{
	v4l2_info(&mdev->v4l2_dev, "aim_unregister_videodev()\n");

	video_unregister_device(mdev->vdev);
}

static void aim_v4l2_dev_release(struct v4l2_device *v4l2_dev)
{
	struct most_video_dev *mdev =
		container_of(v4l2_dev, struct most_video_dev, v4l2_dev);

	v4l2_device_unregister(v4l2_dev);
	kfree(mdev);
}

static int aim_probe_channel(struct most_interface *iface, int channel_idx,
			     struct most_channel_config *ccfg,
			     struct kobject *parent, char *name)
{
	int ret;
	struct most_video_dev *mdev = get_aim_dev(iface, channel_idx);

	pr_info("aim_probe_channel(%s)\n", name);

	if (mdev) {
		pr_err("channel already linked\n");
		return -EEXIST;
	}

	if (ccfg->direction != MOST_CH_RX) {
		pr_err("wrong direction, expect rx\n");
		return -EINVAL;
	}

	if (ccfg->data_type != MOST_CH_SYNC &&
	    ccfg->data_type != MOST_CH_ISOC) {
		pr_err("wrong channel type, expect sync or isoc\n");
		return -EINVAL;
	}

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mutex_init(&mdev->lock);
	atomic_set(&mdev->access_ref, -1);
	spin_lock_init(&mdev->list_lock);
	INIT_LIST_HEAD(&mdev->pending_mbos);
	mdev->iface = iface;
	mdev->ch_idx = channel_idx;
	mdev->v4l2_dev.release = aim_v4l2_dev_release;

	/* Create the v4l2_device */
	strlcpy(mdev->v4l2_dev.name, name, sizeof(mdev->v4l2_dev.name));
	ret = v4l2_device_register(NULL, &mdev->v4l2_dev);
	if (ret) {
		pr_err("v4l2_device_register() failed\n");
		kfree(mdev);
		return ret;
	}

	ret = aim_register_videodev(mdev);
	if (ret)
		goto err_unreg;

	spin_lock_irq(&list_lock);
	list_add(&mdev->list, &video_devices);
	spin_unlock_irq(&list_lock);
	v4l2_info(&mdev->v4l2_dev, "aim_probe_channel() done\n");
	return 0;

err_unreg:
	v4l2_device_disconnect(&mdev->v4l2_dev);
	v4l2_device_put(&mdev->v4l2_dev);
	return ret;
}

static int aim_disconnect_channel(struct most_interface *iface,
				  int channel_idx)
{
	struct most_video_dev *mdev = get_aim_dev(iface, channel_idx);

	if (!mdev) {
		pr_err("no such channel is linked\n");
		return -ENOENT;
	}

	v4l2_info(&mdev->v4l2_dev, "aim_disconnect_channel()\n");

	spin_lock_irq(&list_lock);
	list_del(&mdev->list);
	spin_unlock_irq(&list_lock);

	aim_unregister_videodev(mdev);
	v4l2_device_disconnect(&mdev->v4l2_dev);
	v4l2_device_put(&mdev->v4l2_dev);
	return 0;
}

static struct most_aim aim_info = {
	.name = "v4l",
	.probe_channel = aim_probe_channel,
	.disconnect_channel = aim_disconnect_channel,
	.rx_completion = aim_rx_data,
};

static int __init aim_init(void)
{
	spin_lock_init(&list_lock);
	return most_register_aim(&aim_info);
}

static void __exit aim_exit(void)
{
	struct most_video_dev *mdev, *tmp;

	/*
	 * As the mostcore currently doesn't call disconnect_channel()
	 * for linked channels while we call most_deregister_aim()
	 * we simulate this call here.
	 * This must be fixed in core.
	 */
	spin_lock_irq(&list_lock);
	list_for_each_entry_safe(mdev, tmp, &video_devices, list) {
		list_del(&mdev->list);
		spin_unlock_irq(&list_lock);

		aim_unregister_videodev(mdev);
		v4l2_device_disconnect(&mdev->v4l2_dev);
		v4l2_device_put(&mdev->v4l2_dev);
		spin_lock_irq(&list_lock);
	}
	spin_unlock_irq(&list_lock);

	most_deregister_aim(&aim_info);
	BUG_ON(!list_empty(&video_devices));
}

module_init(aim_init);
module_exit(aim_exit);

MODULE_DESCRIPTION("V4L2 Application Interface Module for MostCore");
MODULE_AUTHOR("Andrey Shvetsov <andrey.shvetsov@k2l.de>");
MODULE_LICENSE("GPL");
