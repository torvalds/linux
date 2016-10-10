/*
 * Hauppauge HD PVR USB driver - video 4 linux 2 interface
 *
 * Copyright (C) 2008      Janne Grunau (j@jannau.net)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include "hdpvr.h"

#define BULK_URB_TIMEOUT   90 /* 0.09 seconds */

#define print_buffer_status() { \
		v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,	\
			 "%s:%d buffer stat: %d free, %d proc\n",	\
			 __func__, __LINE__,				\
			 list_size(&dev->free_buff_list),		\
			 list_size(&dev->rec_buff_list)); }

static const struct v4l2_dv_timings hdpvr_dv_timings[] = {
	V4L2_DV_BT_CEA_720X480I59_94,
	V4L2_DV_BT_CEA_720X576I50,
	V4L2_DV_BT_CEA_720X480P59_94,
	V4L2_DV_BT_CEA_720X576P50,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080I50,
	V4L2_DV_BT_CEA_1920X1080I60,
};

/* Use 480i59 as the default timings */
#define HDPVR_DEF_DV_TIMINGS_IDX (0)

struct hdpvr_fh {
	struct v4l2_fh fh;
	bool legacy_mode;
};

static uint list_size(struct list_head *list)
{
	struct list_head *tmp;
	uint count = 0;

	list_for_each(tmp, list) {
		count++;
	}

	return count;
}

/*=========================================================================*/
/* urb callback */
static void hdpvr_read_bulk_callback(struct urb *urb)
{
	struct hdpvr_buffer *buf = (struct hdpvr_buffer *)urb->context;
	struct hdpvr_device *dev = buf->dev;

	/* marking buffer as received and wake waiting */
	buf->status = BUFSTAT_READY;
	wake_up_interruptible(&dev->wait_data);
}

/*=========================================================================*/
/* buffer bits */

/* function expects dev->io_mutex to be hold by caller */
int hdpvr_cancel_queue(struct hdpvr_device *dev)
{
	struct hdpvr_buffer *buf;

	list_for_each_entry(buf, &dev->rec_buff_list, buff_list) {
		usb_kill_urb(buf->urb);
		buf->status = BUFSTAT_AVAILABLE;
	}

	list_splice_init(&dev->rec_buff_list, dev->free_buff_list.prev);

	return 0;
}

static int hdpvr_free_queue(struct list_head *q)
{
	struct list_head *tmp;
	struct list_head *p;
	struct hdpvr_buffer *buf;
	struct urb *urb;

	for (p = q->next; p != q;) {
		buf = list_entry(p, struct hdpvr_buffer, buff_list);

		urb = buf->urb;
		usb_free_coherent(urb->dev, urb->transfer_buffer_length,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		tmp = p->next;
		list_del(p);
		kfree(buf);
		p = tmp;
	}

	return 0;
}

/* function expects dev->io_mutex to be hold by caller */
int hdpvr_free_buffers(struct hdpvr_device *dev)
{
	hdpvr_cancel_queue(dev);

	hdpvr_free_queue(&dev->free_buff_list);
	hdpvr_free_queue(&dev->rec_buff_list);

	return 0;
}

/* function expects dev->io_mutex to be hold by caller */
int hdpvr_alloc_buffers(struct hdpvr_device *dev, uint count)
{
	uint i;
	int retval = -ENOMEM;
	u8 *mem;
	struct hdpvr_buffer *buf;
	struct urb *urb;

	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "allocating %u buffers\n", count);

	for (i = 0; i < count; i++) {

		buf = kzalloc(sizeof(struct hdpvr_buffer), GFP_KERNEL);
		if (!buf) {
			v4l2_err(&dev->v4l2_dev, "cannot allocate buffer\n");
			goto exit;
		}
		buf->dev = dev;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			v4l2_err(&dev->v4l2_dev, "cannot allocate urb\n");
			goto exit_urb;
		}
		buf->urb = urb;

		mem = usb_alloc_coherent(dev->udev, dev->bulk_in_size, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!mem) {
			v4l2_err(&dev->v4l2_dev,
				 "cannot allocate usb transfer buffer\n");
			goto exit_urb_buffer;
		}

		usb_fill_bulk_urb(buf->urb, dev->udev,
				  usb_rcvbulkpipe(dev->udev,
						  dev->bulk_in_endpointAddr),
				  mem, dev->bulk_in_size,
				  hdpvr_read_bulk_callback, buf);

		buf->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		buf->status = BUFSTAT_AVAILABLE;
		list_add_tail(&buf->buff_list, &dev->free_buff_list);
	}
	return 0;
exit_urb_buffer:
	usb_free_urb(urb);
exit_urb:
	kfree(buf);
exit:
	hdpvr_free_buffers(dev);
	return retval;
}

static int hdpvr_submit_buffers(struct hdpvr_device *dev)
{
	struct hdpvr_buffer *buf;
	struct urb *urb;
	int ret = 0, err_count = 0;

	mutex_lock(&dev->io_mutex);

	while (dev->status == STATUS_STREAMING &&
	       !list_empty(&dev->free_buff_list)) {

		buf = list_entry(dev->free_buff_list.next, struct hdpvr_buffer,
				 buff_list);
		if (buf->status != BUFSTAT_AVAILABLE) {
			v4l2_err(&dev->v4l2_dev,
				 "buffer not marked as available\n");
			ret = -EFAULT;
			goto err;
		}

		urb = buf->urb;
		urb->status = 0;
		urb->actual_length = 0;
		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret) {
			v4l2_err(&dev->v4l2_dev,
				 "usb_submit_urb in %s returned %d\n",
				 __func__, ret);
			if (++err_count > 2)
				break;
			continue;
		}
		buf->status = BUFSTAT_INPROGRESS;
		list_move_tail(&buf->buff_list, &dev->rec_buff_list);
	}
err:
	print_buffer_status();
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static struct hdpvr_buffer *hdpvr_get_next_buffer(struct hdpvr_device *dev)
{
	struct hdpvr_buffer *buf;

	mutex_lock(&dev->io_mutex);

	if (list_empty(&dev->rec_buff_list)) {
		mutex_unlock(&dev->io_mutex);
		return NULL;
	}

	buf = list_entry(dev->rec_buff_list.next, struct hdpvr_buffer,
			 buff_list);
	mutex_unlock(&dev->io_mutex);

	return buf;
}

static void hdpvr_transmit_buffers(struct work_struct *work)
{
	struct hdpvr_device *dev = container_of(work, struct hdpvr_device,
						worker);

	while (dev->status == STATUS_STREAMING) {

		if (hdpvr_submit_buffers(dev)) {
			v4l2_err(&dev->v4l2_dev, "couldn't submit buffers\n");
			goto error;
		}
		if (wait_event_interruptible(dev->wait_buffer,
				!list_empty(&dev->free_buff_list) ||
					     dev->status != STATUS_STREAMING))
			goto error;
	}

	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "transmit worker exited\n");
	return;
error:
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "transmit buffers errored\n");
	dev->status = STATUS_ERROR;
}

/* function expects dev->io_mutex to be hold by caller */
static int hdpvr_start_streaming(struct hdpvr_device *dev)
{
	int ret;
	struct hdpvr_video_info vidinf;

	if (dev->status == STATUS_STREAMING)
		return 0;
	if (dev->status != STATUS_IDLE)
		return -EAGAIN;

	ret = get_video_info(dev, &vidinf);
	if (ret < 0)
		return ret;

	if (!vidinf.valid) {
		msleep(250);
		v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
				"no video signal at input %d\n", dev->options.video_input);
		return -EAGAIN;
	}

	v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			"video signal: %dx%d@%dhz\n", vidinf.width,
			vidinf.height, vidinf.fps);

	/* start streaming 2 request */
	ret = usb_control_msg(dev->udev,
			usb_sndctrlpipe(dev->udev, 0),
			0xb8, 0x38, 0x1, 0, NULL, 0, 8000);
	v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			"encoder start control request returned %d\n", ret);
	if (ret < 0)
		return ret;

	ret = hdpvr_config_call(dev, CTRL_START_STREAMING_VALUE, 0x00);
	if (ret)
		return ret;

	dev->status = STATUS_STREAMING;

	INIT_WORK(&dev->worker, hdpvr_transmit_buffers);
	schedule_work(&dev->worker);

	v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			"streaming started\n");

	return 0;
}


/* function expects dev->io_mutex to be hold by caller */
static int hdpvr_stop_streaming(struct hdpvr_device *dev)
{
	int actual_length;
	uint c = 0;
	u8 *buf;

	if (dev->status == STATUS_IDLE)
		return 0;
	else if (dev->status != STATUS_STREAMING)
		return -EAGAIN;

	buf = kmalloc(dev->bulk_in_size, GFP_KERNEL);
	if (!buf)
		v4l2_err(&dev->v4l2_dev, "failed to allocate temporary buffer "
			 "for emptying the internal device buffer. "
			 "Next capture start will be slow\n");

	dev->status = STATUS_SHUTTING_DOWN;
	hdpvr_config_call(dev, CTRL_STOP_STREAMING_VALUE, 0x00);
	mutex_unlock(&dev->io_mutex);

	wake_up_interruptible(&dev->wait_buffer);
	msleep(50);

	flush_work(&dev->worker);

	mutex_lock(&dev->io_mutex);
	/* kill the still outstanding urbs */
	hdpvr_cancel_queue(dev);

	/* emptying the device buffer beforeshutting it down */
	while (buf && ++c < 500 &&
	       !usb_bulk_msg(dev->udev,
			     usb_rcvbulkpipe(dev->udev,
					     dev->bulk_in_endpointAddr),
			     buf, dev->bulk_in_size, &actual_length,
			     BULK_URB_TIMEOUT)) {
		v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			 "%2d: got %d bytes\n", c, actual_length);
	}
	kfree(buf);
	v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
		 "used %d urbs to empty device buffers\n", c-1);
	msleep(10);

	dev->status = STATUS_IDLE;

	return 0;
}


/*=======================================================================*/
/*
 * video 4 linux 2 file operations
 */

static int hdpvr_open(struct file *file)
{
	struct hdpvr_fh *fh = kzalloc(sizeof(*fh), GFP_KERNEL);

	if (fh == NULL)
		return -ENOMEM;
	fh->legacy_mode = true;
	v4l2_fh_init(&fh->fh, video_devdata(file));
	v4l2_fh_add(&fh->fh);
	file->private_data = fh;
	return 0;
}

static int hdpvr_release(struct file *file)
{
	struct hdpvr_device *dev = video_drvdata(file);

	mutex_lock(&dev->io_mutex);
	if (file->private_data == dev->owner) {
		hdpvr_stop_streaming(dev);
		dev->owner = NULL;
	}
	mutex_unlock(&dev->io_mutex);

	return v4l2_fh_release(file);
}

/*
 * hdpvr_v4l2_read()
 * will allocate buffers when called for the first time
 */
static ssize_t hdpvr_read(struct file *file, char __user *buffer, size_t count,
			  loff_t *pos)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_buffer *buf = NULL;
	struct urb *urb;
	unsigned int ret = 0;
	int rem, cnt;

	if (*pos)
		return -ESPIPE;

	mutex_lock(&dev->io_mutex);
	if (dev->status == STATUS_IDLE) {
		if (hdpvr_start_streaming(dev)) {
			v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
				 "start_streaming failed\n");
			ret = -EIO;
			msleep(200);
			dev->status = STATUS_IDLE;
			mutex_unlock(&dev->io_mutex);
			goto err;
		}
		dev->owner = file->private_data;
		print_buffer_status();
	}
	mutex_unlock(&dev->io_mutex);

	/* wait for the first buffer */
	if (!(file->f_flags & O_NONBLOCK)) {
		if (wait_event_interruptible(dev->wait_data,
					     hdpvr_get_next_buffer(dev)))
			return -ERESTARTSYS;
	}

	buf = hdpvr_get_next_buffer(dev);

	while (count > 0 && buf) {

		if (buf->status != BUFSTAT_READY &&
		    dev->status != STATUS_DISCONNECTED) {
			/* return nonblocking */
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				goto err;
			}

			if (wait_event_interruptible(dev->wait_data,
					      buf->status == BUFSTAT_READY))
				return -ERESTARTSYS;
		}

		if (buf->status != BUFSTAT_READY)
			break;

		/* set remaining bytes to copy */
		urb = buf->urb;
		rem = urb->actual_length - buf->pos;
		cnt = rem > count ? count : rem;

		if (copy_to_user(buffer, urb->transfer_buffer + buf->pos,
				 cnt)) {
			v4l2_err(&dev->v4l2_dev, "read: copy_to_user failed\n");
			if (!ret)
				ret = -EFAULT;
			goto err;
		}

		buf->pos += cnt;
		count -= cnt;
		buffer += cnt;
		ret += cnt;

		/* finished, take next buffer */
		if (buf->pos == urb->actual_length) {
			mutex_lock(&dev->io_mutex);
			buf->pos = 0;
			buf->status = BUFSTAT_AVAILABLE;

			list_move_tail(&buf->buff_list, &dev->free_buff_list);

			print_buffer_status();

			mutex_unlock(&dev->io_mutex);

			wake_up_interruptible(&dev->wait_buffer);

			buf = hdpvr_get_next_buffer(dev);
		}
	}
err:
	if (!ret && !buf)
		ret = -EAGAIN;
	return ret;
}

static unsigned int hdpvr_poll(struct file *filp, poll_table *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct hdpvr_buffer *buf = NULL;
	struct hdpvr_device *dev = video_drvdata(filp);
	unsigned int mask = v4l2_ctrl_poll(filp, wait);

	if (!(req_events & (POLLIN | POLLRDNORM)))
		return mask;

	mutex_lock(&dev->io_mutex);

	if (dev->status == STATUS_IDLE) {
		if (hdpvr_start_streaming(dev)) {
			v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
				 "start_streaming failed\n");
			dev->status = STATUS_IDLE;
		} else {
			dev->owner = filp->private_data;
		}

		print_buffer_status();
	}
	mutex_unlock(&dev->io_mutex);

	buf = hdpvr_get_next_buffer(dev);
	/* only wait if no data is available */
	if (!buf || buf->status != BUFSTAT_READY) {
		poll_wait(filp, &dev->wait_data, wait);
		buf = hdpvr_get_next_buffer(dev);
	}
	if (buf && buf->status == BUFSTAT_READY)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}


static const struct v4l2_file_operations hdpvr_fops = {
	.owner		= THIS_MODULE,
	.open		= hdpvr_open,
	.release	= hdpvr_release,
	.read		= hdpvr_read,
	.poll		= hdpvr_poll,
	.unlocked_ioctl	= video_ioctl2,
};

/*=======================================================================*/
/*
 * V4L2 ioctl handling
 */

static int vidioc_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct hdpvr_device *dev = video_drvdata(file);

	strcpy(cap->driver, "hdpvr");
	strcpy(cap->card, "Hauppauge HD PVR");
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_AUDIO |
			    V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_s_std(struct file *file, void *_fh,
			v4l2_std_id std)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;
	u8 std_type = 1;

	if (!fh->legacy_mode && dev->options.video_input == HDPVR_COMPONENT)
		return -ENODATA;
	if (dev->status != STATUS_IDLE)
		return -EBUSY;
	if (std & V4L2_STD_525_60)
		std_type = 0;
	dev->cur_std = std;
	dev->width = 720;
	dev->height = std_type ? 576 : 480;

	return hdpvr_config_call(dev, CTRL_VIDEO_STD_TYPE, std_type);
}

static int vidioc_g_std(struct file *file, void *_fh,
			v4l2_std_id *std)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;

	if (!fh->legacy_mode && dev->options.video_input == HDPVR_COMPONENT)
		return -ENODATA;
	*std = dev->cur_std;
	return 0;
}

static int vidioc_querystd(struct file *file, void *_fh, v4l2_std_id *a)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_video_info vid_info;
	struct hdpvr_fh *fh = _fh;
	int ret;

	*a = V4L2_STD_UNKNOWN;
	if (dev->options.video_input == HDPVR_COMPONENT)
		return fh->legacy_mode ? 0 : -ENODATA;
	ret = get_video_info(dev, &vid_info);
	if (vid_info.valid && vid_info.width == 720 &&
	    (vid_info.height == 480 || vid_info.height == 576)) {
		*a = (vid_info.height == 480) ?
			V4L2_STD_525_60 : V4L2_STD_625_50;
	}
	return ret;
}

static int vidioc_s_dv_timings(struct file *file, void *_fh,
				    struct v4l2_dv_timings *timings)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;
	int i;

	fh->legacy_mode = false;
	if (dev->options.video_input)
		return -ENODATA;
	if (dev->status != STATUS_IDLE)
		return -EBUSY;
	for (i = 0; i < ARRAY_SIZE(hdpvr_dv_timings); i++)
		if (v4l2_match_dv_timings(timings, hdpvr_dv_timings + i, 0, false))
			break;
	if (i == ARRAY_SIZE(hdpvr_dv_timings))
		return -EINVAL;
	dev->cur_dv_timings = hdpvr_dv_timings[i];
	dev->width = hdpvr_dv_timings[i].bt.width;
	dev->height = hdpvr_dv_timings[i].bt.height;
	return 0;
}

static int vidioc_g_dv_timings(struct file *file, void *_fh,
				    struct v4l2_dv_timings *timings)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;

	fh->legacy_mode = false;
	if (dev->options.video_input)
		return -ENODATA;
	*timings = dev->cur_dv_timings;
	return 0;
}

static int vidioc_query_dv_timings(struct file *file, void *_fh,
				    struct v4l2_dv_timings *timings)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;
	struct hdpvr_video_info vid_info;
	bool interlaced;
	int ret = 0;
	int i;

	fh->legacy_mode = false;
	if (dev->options.video_input)
		return -ENODATA;
	ret = get_video_info(dev, &vid_info);
	if (ret)
		return ret;
	if (!vid_info.valid)
		return -ENOLCK;
	interlaced = vid_info.fps <= 30;
	for (i = 0; i < ARRAY_SIZE(hdpvr_dv_timings); i++) {
		const struct v4l2_bt_timings *bt = &hdpvr_dv_timings[i].bt;
		unsigned hsize;
		unsigned vsize;
		unsigned fps;

		hsize = V4L2_DV_BT_FRAME_WIDTH(bt);
		vsize = V4L2_DV_BT_FRAME_HEIGHT(bt);
		fps = (unsigned)bt->pixelclock / (hsize * vsize);
		if (bt->width != vid_info.width ||
		    bt->height != vid_info.height ||
		    bt->interlaced != interlaced ||
		    (fps != vid_info.fps && fps + 1 != vid_info.fps))
			continue;
		*timings = hdpvr_dv_timings[i];
		break;
	}
	if (i == ARRAY_SIZE(hdpvr_dv_timings))
		ret = -ERANGE;

	return ret;
}

static int vidioc_enum_dv_timings(struct file *file, void *_fh,
				    struct v4l2_enum_dv_timings *timings)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;

	fh->legacy_mode = false;
	memset(timings->reserved, 0, sizeof(timings->reserved));
	if (dev->options.video_input)
		return -ENODATA;
	if (timings->index >= ARRAY_SIZE(hdpvr_dv_timings))
		return -EINVAL;
	timings->timings = hdpvr_dv_timings[timings->index];
	return 0;
}

static int vidioc_dv_timings_cap(struct file *file, void *_fh,
				    struct v4l2_dv_timings_cap *cap)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;

	fh->legacy_mode = false;
	if (dev->options.video_input)
		return -ENODATA;
	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.min_width = 720;
	cap->bt.max_width = 1920;
	cap->bt.min_height = 480;
	cap->bt.max_height = 1080;
	cap->bt.min_pixelclock = 27000000;
	cap->bt.max_pixelclock = 74250000;
	cap->bt.standards = V4L2_DV_BT_STD_CEA861;
	cap->bt.capabilities = V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE;
	return 0;
}

static const char *iname[] = {
	[HDPVR_COMPONENT] = "Component",
	[HDPVR_SVIDEO]    = "S-Video",
	[HDPVR_COMPOSITE] = "Composite",
};

static int vidioc_enum_input(struct file *file, void *_fh, struct v4l2_input *i)
{
	unsigned int n;

	n = i->index;
	if (n >= HDPVR_VIDEO_INPUTS)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(i->name, iname[n], sizeof(i->name) - 1);
	i->name[sizeof(i->name) - 1] = '\0';

	i->audioset = 1<<HDPVR_RCA_FRONT | 1<<HDPVR_RCA_BACK | 1<<HDPVR_SPDIF;

	i->capabilities = n ? V4L2_IN_CAP_STD : V4L2_IN_CAP_DV_TIMINGS;
	i->std = n ? V4L2_STD_ALL : 0;

	return 0;
}

static int vidioc_s_input(struct file *file, void *_fh,
			  unsigned int index)
{
	struct hdpvr_device *dev = video_drvdata(file);
	int retval;

	if (index >= HDPVR_VIDEO_INPUTS)
		return -EINVAL;

	if (dev->status != STATUS_IDLE)
		return -EBUSY;

	retval = hdpvr_config_call(dev, CTRL_VIDEO_INPUT_VALUE, index+1);
	if (!retval) {
		dev->options.video_input = index;
		/*
		 * Unfortunately gstreamer calls ENUMSTD and bails out if it
		 * won't find any formats, even though component input is
		 * selected. This means that we have to leave tvnorms at
		 * V4L2_STD_ALL. We cannot use the 'legacy' trick since
		 * tvnorms is set at the device node level and not at the
		 * filehandle level.
		 *
		 * Comment this out for now, but if the legacy mode can be
		 * removed in the future, then this code should be enabled
		 * again.
		dev->video_dev.tvnorms =
			(index != HDPVR_COMPONENT) ? V4L2_STD_ALL : 0;
		 */
	}

	return retval;
}

static int vidioc_g_input(struct file *file, void *private_data,
			  unsigned int *index)
{
	struct hdpvr_device *dev = video_drvdata(file);

	*index = dev->options.video_input;
	return 0;
}


static const char *audio_iname[] = {
	[HDPVR_RCA_FRONT] = "RCA front",
	[HDPVR_RCA_BACK]  = "RCA back",
	[HDPVR_SPDIF]     = "SPDIF",
};

static int vidioc_enumaudio(struct file *file, void *priv,
				struct v4l2_audio *audio)
{
	unsigned int n;

	n = audio->index;
	if (n >= HDPVR_AUDIO_INPUTS)
		return -EINVAL;

	audio->capability = V4L2_AUDCAP_STEREO;

	strncpy(audio->name, audio_iname[n], sizeof(audio->name) - 1);
	audio->name[sizeof(audio->name) - 1] = '\0';

	return 0;
}

static int vidioc_s_audio(struct file *file, void *private_data,
			  const struct v4l2_audio *audio)
{
	struct hdpvr_device *dev = video_drvdata(file);
	int retval;

	if (audio->index >= HDPVR_AUDIO_INPUTS)
		return -EINVAL;

	if (dev->status != STATUS_IDLE)
		return -EBUSY;

	retval = hdpvr_set_audio(dev, audio->index+1, dev->options.audio_codec);
	if (!retval)
		dev->options.audio_input = audio->index;

	return retval;
}

static int vidioc_g_audio(struct file *file, void *private_data,
			  struct v4l2_audio *audio)
{
	struct hdpvr_device *dev = video_drvdata(file);

	audio->index = dev->options.audio_input;
	audio->capability = V4L2_AUDCAP_STEREO;
	strncpy(audio->name, audio_iname[audio->index], sizeof(audio->name));
	audio->name[sizeof(audio->name) - 1] = '\0';
	return 0;
}

static int hdpvr_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hdpvr_device *dev =
		container_of(ctrl->handler, struct hdpvr_device, hdl);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		if (ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
		    dev->video_bitrate->val >= dev->video_bitrate_peak->val)
			dev->video_bitrate_peak->val =
					dev->video_bitrate->val + 100000;
		break;
	}
	return 0;
}

static int hdpvr_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hdpvr_device *dev =
		container_of(ctrl->handler, struct hdpvr_device, hdl);
	struct hdpvr_options *opt = &dev->options;
	int ret = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = hdpvr_config_call(dev, CTRL_BRIGHTNESS, ctrl->val);
		if (ret)
			break;
		dev->options.brightness = ctrl->val;
		return 0;
	case V4L2_CID_CONTRAST:
		ret = hdpvr_config_call(dev, CTRL_CONTRAST, ctrl->val);
		if (ret)
			break;
		dev->options.contrast = ctrl->val;
		return 0;
	case V4L2_CID_SATURATION:
		ret = hdpvr_config_call(dev, CTRL_SATURATION, ctrl->val);
		if (ret)
			break;
		dev->options.saturation = ctrl->val;
		return 0;
	case V4L2_CID_HUE:
		ret = hdpvr_config_call(dev, CTRL_HUE, ctrl->val);
		if (ret)
			break;
		dev->options.hue = ctrl->val;
		return 0;
	case V4L2_CID_SHARPNESS:
		ret = hdpvr_config_call(dev, CTRL_SHARPNESS, ctrl->val);
		if (ret)
			break;
		dev->options.sharpness = ctrl->val;
		return 0;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		if (dev->flags & HDPVR_FLAG_AC3_CAP) {
			opt->audio_codec = ctrl->val;
			return hdpvr_set_audio(dev, opt->audio_input + 1,
					      opt->audio_codec);
		}
		return 0;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		return 0;
/* 	case V4L2_CID_MPEG_VIDEO_B_FRAMES: */
/* 		if (ctrl->value == 0 && !(opt->gop_mode & 0x2)) { */
/* 			opt->gop_mode |= 0x2; */
/* 			hdpvr_config_call(dev, CTRL_GOP_MODE_VALUE, */
/* 					  opt->gop_mode); */
/* 		} */
/* 		if (ctrl->value == 128 && opt->gop_mode & 0x2) { */
/* 			opt->gop_mode &= ~0x2; */
/* 			hdpvr_config_call(dev, CTRL_GOP_MODE_VALUE, */
/* 					  opt->gop_mode); */
/* 		} */
/* 		break; */
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE: {
		uint peak_bitrate = dev->video_bitrate_peak->val / 100000;
		uint bitrate = dev->video_bitrate->val / 100000;

		if (ctrl->is_new) {
			if (ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
				opt->bitrate_mode = HDPVR_CONSTANT;
			else
				opt->bitrate_mode = HDPVR_VARIABLE_AVERAGE;
			hdpvr_config_call(dev, CTRL_BITRATE_MODE_VALUE,
					  opt->bitrate_mode);
			v4l2_ctrl_activate(dev->video_bitrate_peak,
				ctrl->val != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
		}

		if (dev->video_bitrate_peak->is_new ||
		    dev->video_bitrate->is_new) {
			opt->bitrate = bitrate;
			opt->peak_bitrate = peak_bitrate;
			hdpvr_set_bitrate(dev);
		}
		return 0;
	}
	case V4L2_CID_MPEG_STREAM_TYPE:
		return 0;
	default:
		break;
	}
	return ret;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *private_data,
				    struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	strncpy(f->description, "MPEG2-TS with AVC/AAC streams", 32);
	f->pixelformat = V4L2_PIX_FMT_MPEG;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *_fh,
				struct v4l2_format *f)
{
	struct hdpvr_device *dev = video_drvdata(file);
	struct hdpvr_fh *fh = _fh;
	int ret;

	/*
	 * The original driver would always returns the current detected
	 * resolution as the format (and EFAULT if it couldn't be detected).
	 * With the introduction of VIDIOC_QUERY_DV_TIMINGS there is now a
	 * better way of doing this, but to stay compatible with existing
	 * applications we assume legacy mode every time an application opens
	 * the device. Only if one of the new DV_TIMINGS ioctls is called
	 * will the filehandle go into 'normal' mode where g_fmt returns the
	 * last set format.
	 */
	if (fh->legacy_mode) {
		struct hdpvr_video_info vid_info;

		ret = get_video_info(dev, &vid_info);
		if (ret < 0)
			return ret;
		if (!vid_info.valid)
			return -EFAULT;
		f->fmt.pix.width = vid_info.width;
		f->fmt.pix.height = vid_info.height;
	} else {
		f->fmt.pix.width = dev->width;
		f->fmt.pix.height = dev->height;
	}
	f->fmt.pix.pixelformat	= V4L2_PIX_FMT_MPEG;
	f->fmt.pix.sizeimage	= dev->bulk_in_size;
	f->fmt.pix.bytesperline	= 0;
	if (f->fmt.pix.width == 720) {
		/* SDTV formats */
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
		f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	} else {
		/* HDTV formats */
		f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
		f->fmt.pix.field = V4L2_FIELD_NONE;
	}
	return 0;
}

static int vidioc_encoder_cmd(struct file *filp, void *priv,
			       struct v4l2_encoder_cmd *a)
{
	struct hdpvr_device *dev = video_drvdata(filp);
	int res = 0;

	mutex_lock(&dev->io_mutex);
	a->flags = 0;

	switch (a->cmd) {
	case V4L2_ENC_CMD_START:
		if (dev->owner && filp->private_data != dev->owner) {
			res = -EBUSY;
			break;
		}
		if (dev->status == STATUS_STREAMING)
			break;
		res = hdpvr_start_streaming(dev);
		if (!res)
			dev->owner = filp->private_data;
		else
			dev->status = STATUS_IDLE;
		break;
	case V4L2_ENC_CMD_STOP:
		if (dev->owner && filp->private_data != dev->owner) {
			res = -EBUSY;
			break;
		}
		if (dev->status == STATUS_IDLE)
			break;
		res = hdpvr_stop_streaming(dev);
		if (!res)
			dev->owner = NULL;
		break;
	default:
		v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
			 "Unsupported encoder cmd %d\n", a->cmd);
		res = -EINVAL;
		break;
	}

	mutex_unlock(&dev->io_mutex);
	return res;
}

static int vidioc_try_encoder_cmd(struct file *filp, void *priv,
					struct v4l2_encoder_cmd *a)
{
	a->flags = 0;
	switch (a->cmd) {
	case V4L2_ENC_CMD_START:
	case V4L2_ENC_CMD_STOP:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops hdpvr_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_s_std		= vidioc_s_std,
	.vidioc_g_std		= vidioc_g_std,
	.vidioc_querystd	= vidioc_querystd,
	.vidioc_s_dv_timings	= vidioc_s_dv_timings,
	.vidioc_g_dv_timings	= vidioc_g_dv_timings,
	.vidioc_query_dv_timings= vidioc_query_dv_timings,
	.vidioc_enum_dv_timings	= vidioc_enum_dv_timings,
	.vidioc_dv_timings_cap	= vidioc_dv_timings_cap,
	.vidioc_enum_input	= vidioc_enum_input,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_enumaudio	= vidioc_enumaudio,
	.vidioc_g_audio		= vidioc_g_audio,
	.vidioc_s_audio		= vidioc_s_audio,
	.vidioc_enum_fmt_vid_cap= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_encoder_cmd	= vidioc_encoder_cmd,
	.vidioc_try_encoder_cmd	= vidioc_try_encoder_cmd,
	.vidioc_log_status	= v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static void hdpvr_device_release(struct video_device *vdev)
{
	struct hdpvr_device *dev = video_get_drvdata(vdev);

	hdpvr_delete(dev);
	mutex_lock(&dev->io_mutex);
	flush_work(&dev->worker);
	mutex_unlock(&dev->io_mutex);

	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_ctrl_handler_free(&dev->hdl);

	/* deregister I2C adapter */
#if IS_ENABLED(CONFIG_I2C)
	mutex_lock(&dev->i2c_mutex);
	i2c_del_adapter(&dev->i2c_adapter);
	mutex_unlock(&dev->i2c_mutex);
#endif /* CONFIG_I2C */

	kfree(dev->usbc_buf);
	kfree(dev);
}

static const struct video_device hdpvr_video_template = {
	.fops			= &hdpvr_fops,
	.release		= hdpvr_device_release,
	.ioctl_ops 		= &hdpvr_ioctl_ops,
	.tvnorms		= V4L2_STD_ALL,
};

static const struct v4l2_ctrl_ops hdpvr_ctrl_ops = {
	.try_ctrl = hdpvr_try_ctrl,
	.s_ctrl = hdpvr_s_ctrl,
};

int hdpvr_register_videodev(struct hdpvr_device *dev, struct device *parent,
			    int devnum)
{
	struct v4l2_ctrl_handler *hdl = &dev->hdl;
	bool ac3 = dev->flags & HDPVR_FLAG_AC3_CAP;
	int res;

	dev->cur_std = V4L2_STD_525_60;
	dev->width = 720;
	dev->height = 480;
	dev->cur_dv_timings = hdpvr_dv_timings[HDPVR_DEF_DV_TIMINGS_IDX];
	v4l2_ctrl_handler_init(hdl, 11);
	if (dev->fw_ver > 0x15) {
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0x0, 0xff, 1, 0x80);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_CONTRAST, 0x0, 0xff, 1, 0x40);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_SATURATION, 0x0, 0xff, 1, 0x40);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_HUE, 0x0, 0x1e, 1, 0xf);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_SHARPNESS, 0x0, 0xff, 1, 0x80);
	} else {
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0x0, 0xff, 1, 0x86);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_CONTRAST, 0x0, 0xff, 1, 0x80);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_SATURATION, 0x0, 0xff, 1, 0x80);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_HUE, 0x0, 0xff, 1, 0x80);
		v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
			V4L2_CID_SHARPNESS, 0x0, 0xff, 1, 0x80);
	}

	v4l2_ctrl_new_std_menu(hdl, &hdpvr_ctrl_ops,
		V4L2_CID_MPEG_STREAM_TYPE,
		V4L2_MPEG_STREAM_TYPE_MPEG2_TS,
		0x1, V4L2_MPEG_STREAM_TYPE_MPEG2_TS);
	v4l2_ctrl_new_std_menu(hdl, &hdpvr_ctrl_ops,
		V4L2_CID_MPEG_AUDIO_ENCODING,
		ac3 ? V4L2_MPEG_AUDIO_ENCODING_AC3 : V4L2_MPEG_AUDIO_ENCODING_AAC,
		0x7, ac3 ? dev->options.audio_codec : V4L2_MPEG_AUDIO_ENCODING_AAC);
	v4l2_ctrl_new_std_menu(hdl, &hdpvr_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_ENCODING,
		V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC, 0x3,
		V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC);

	dev->video_mode = v4l2_ctrl_new_std_menu(hdl, &hdpvr_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);

	dev->video_bitrate = v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE,
		1000000, 13500000, 100000, 6500000);
	dev->video_bitrate_peak = v4l2_ctrl_new_std(hdl, &hdpvr_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
		1100000, 20200000, 100000, 9000000);
	dev->v4l2_dev.ctrl_handler = hdl;
	if (hdl->error) {
		res = hdl->error;
		v4l2_err(&dev->v4l2_dev, "Could not register controls\n");
		goto error;
	}
	v4l2_ctrl_cluster(3, &dev->video_mode);
	res = v4l2_ctrl_handler_setup(hdl);
	if (res < 0) {
		v4l2_err(&dev->v4l2_dev, "Could not setup controls\n");
		goto error;
	}

	/* setup and register video device */
	dev->video_dev = hdpvr_video_template;
	strcpy(dev->video_dev.name, "Hauppauge HD PVR");
	dev->video_dev.v4l2_dev = &dev->v4l2_dev;
	video_set_drvdata(&dev->video_dev, dev);

	res = video_register_device(&dev->video_dev, VFL_TYPE_GRABBER, devnum);
	if (res < 0) {
		v4l2_err(&dev->v4l2_dev, "video_device registration failed\n");
		goto error;
	}

	return 0;
error:
	v4l2_ctrl_handler_free(hdl);
	return res;
}
