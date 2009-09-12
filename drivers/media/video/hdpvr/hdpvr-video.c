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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include "hdpvr.h"

#define BULK_URB_TIMEOUT 1250 /* 1.25 seconds */

#define print_buffer_status() { \
		v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,	\
			 "%s:%d buffer stat: %d free, %d proc\n",	\
			 __func__, __LINE__,				\
			 list_size(&dev->free_buff_list),		\
			 list_size(&dev->rec_buff_list)); }

struct hdpvr_fh {
	struct hdpvr_device	*dev;
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
/* bufffer bits */

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
		usb_buffer_free(urb->dev, urb->transfer_buffer_length,
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
			goto exit;
		}
		buf->urb = urb;

		mem = usb_buffer_alloc(dev->udev, dev->bulk_in_size, GFP_KERNEL,
				       &urb->transfer_dma);
		if (!mem) {
			v4l2_err(&dev->v4l2_dev,
				 "cannot allocate usb transfer buffer\n");
			goto exit;
		}

		usb_fill_bulk_urb(buf->urb, dev->udev,
				  usb_rcvbulkpipe(dev->udev,
						  dev->bulk_in_endpointAddr),
				  mem, dev->bulk_in_size,
				  hdpvr_read_bulk_callback, buf);

		buf->status = BUFSTAT_AVAILABLE;
		list_add_tail(&buf->buff_list, &dev->free_buff_list);
	}
	return 0;
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
	struct hdpvr_video_info *vidinf;

	if (dev->status == STATUS_STREAMING)
		return 0;
	else if (dev->status != STATUS_IDLE)
		return -EAGAIN;

	vidinf = get_video_info(dev);

	if (vidinf) {
		v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			 "video signal: %dx%d@%dhz\n", vidinf->width,
			 vidinf->height, vidinf->fps);
		kfree(vidinf);

		/* start streaming 2 request */
		ret = usb_control_msg(dev->udev,
				      usb_sndctrlpipe(dev->udev, 0),
				      0xb8, 0x38, 0x1, 0, NULL, 0, 8000);
		v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			 "encoder start control request returned %d\n", ret);

		hdpvr_config_call(dev, CTRL_START_STREAMING_VALUE, 0x00);

		INIT_WORK(&dev->worker, hdpvr_transmit_buffers);
		queue_work(dev->workqueue, &dev->worker);

		v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
			 "streaming started\n");
		dev->status = STATUS_STREAMING;

		return 0;
	}
	msleep(250);
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "no video signal at input %d\n", dev->options.video_input);
	return -EAGAIN;
}


/* function expects dev->io_mutex to be hold by caller */
static int hdpvr_stop_streaming(struct hdpvr_device *dev)
{
	uint actual_length, c = 0;
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

	flush_workqueue(dev->workqueue);

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
		/* wait */
		msleep(5);
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
	struct hdpvr_device *dev;
	struct hdpvr_fh *fh;
	int retval = -ENOMEM;

	dev = (struct hdpvr_device *)video_get_drvdata(video_devdata(file));
	if (!dev) {
		v4l2_err(&dev->v4l2_dev, "open failing with with ENODEV\n");
		retval = -ENODEV;
		goto err;
	}

	fh = kzalloc(sizeof(struct hdpvr_fh), GFP_KERNEL);
	if (!fh) {
		v4l2_err(&dev->v4l2_dev, "Out of memory\n");
		goto err;
	}
	/* lock the device to allow correctly handling errors
	 * in resumption */
	mutex_lock(&dev->io_mutex);
	dev->open_count++;

	fh->dev = dev;

	/* save our object in the file's private structure */
	file->private_data = fh;

	retval = 0;
err:
	mutex_unlock(&dev->io_mutex);
	return retval;
}

static int hdpvr_release(struct file *file)
{
	struct hdpvr_fh		*fh  = (struct hdpvr_fh *)file->private_data;
	struct hdpvr_device	*dev = fh->dev;

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->io_mutex);
	if (!(--dev->open_count) && dev->status == STATUS_STREAMING)
		hdpvr_stop_streaming(dev);

	mutex_unlock(&dev->io_mutex);

	return 0;
}

/*
 * hdpvr_v4l2_read()
 * will allocate buffers when called for the first time
 */
static ssize_t hdpvr_read(struct file *file, char __user *buffer, size_t count,
			  loff_t *pos)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	struct hdpvr_buffer *buf = NULL;
	struct urb *urb;
	unsigned int ret = 0;
	int rem, cnt;

	if (*pos)
		return -ESPIPE;

	if (!dev)
		return -ENODEV;

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
					      buf->status == BUFSTAT_READY)) {
				ret = -ERESTARTSYS;
				goto err;
			}
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
	struct hdpvr_buffer *buf = NULL;
	struct hdpvr_fh *fh = (struct hdpvr_fh *)filp->private_data;
	struct hdpvr_device *dev = fh->dev;
	unsigned int mask = 0;

	mutex_lock(&dev->io_mutex);

	if (video_is_unregistered(dev->video_dev))
		return -EIO;

	if (dev->status == STATUS_IDLE) {
		if (hdpvr_start_streaming(dev)) {
			v4l2_dbg(MSG_BUFFER, hdpvr_debug, &dev->v4l2_dev,
				 "start_streaming failed\n");
			dev->status = STATUS_IDLE;
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
	strcpy(cap->card, "Haupauge HD PVR");
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->version = HDPVR_VERSION;
	cap->capabilities =     V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_AUDIO         |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_s_std(struct file *file, void *private_data,
			v4l2_std_id *std)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	u8 std_type = 1;

	if (*std & (V4L2_STD_NTSC | V4L2_STD_PAL_60))
		std_type = 0;

	return hdpvr_config_call(dev, CTRL_VIDEO_STD_TYPE, std_type);
}

static const char *iname[] = {
	[HDPVR_COMPONENT] = "Component",
	[HDPVR_SVIDEO]    = "S-Video",
	[HDPVR_COMPOSITE] = "Composite",
};

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	unsigned int n;

	n = i->index;
	if (n >= HDPVR_VIDEO_INPUTS)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(i->name, iname[n], sizeof(i->name) - 1);
	i->name[sizeof(i->name) - 1] = '\0';

	i->audioset = 1<<HDPVR_RCA_FRONT | 1<<HDPVR_RCA_BACK | 1<<HDPVR_SPDIF;

	i->std = dev->video_dev->tvnorms;

	return 0;
}

static int vidioc_s_input(struct file *file, void *private_data,
			  unsigned int index)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int retval;

	if (index >= HDPVR_VIDEO_INPUTS)
		return -EINVAL;

	if (dev->status != STATUS_IDLE)
		return -EAGAIN;

	retval = hdpvr_config_call(dev, CTRL_VIDEO_INPUT_VALUE, index+1);
	if (!retval)
		dev->options.video_input = index;

	return retval;
}

static int vidioc_g_input(struct file *file, void *private_data,
			  unsigned int *index)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;

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
			  struct v4l2_audio *audio)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int retval;

	if (audio->index >= HDPVR_AUDIO_INPUTS)
		return -EINVAL;

	if (dev->status != STATUS_IDLE)
		return -EAGAIN;

	retval = hdpvr_set_audio(dev, audio->index+1, dev->options.audio_codec);
	if (!retval)
		dev->options.audio_input = audio->index;

	return retval;
}

static int vidioc_g_audio(struct file *file, void *private_data,
			  struct v4l2_audio *audio)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;

	audio->index = dev->options.audio_input;
	audio->capability = V4L2_AUDCAP_STEREO;
	strncpy(audio->name, audio_iname[audio->index], sizeof(audio->name));
	audio->name[sizeof(audio->name) - 1] = '\0';
	return 0;
}

static const s32 supported_v4l2_ctrls[] = {
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_CONTRAST,
	V4L2_CID_SATURATION,
	V4L2_CID_HUE,
	V4L2_CID_SHARPNESS,
	V4L2_CID_MPEG_AUDIO_ENCODING,
	V4L2_CID_MPEG_VIDEO_ENCODING,
	V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
	V4L2_CID_MPEG_VIDEO_BITRATE,
	V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
};

static int fill_queryctrl(struct hdpvr_options *opt, struct v4l2_queryctrl *qc,
			  int ac3)
{
	int err;

	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0x0, 0xff, 1, 0x86);
	case V4L2_CID_CONTRAST:
		return v4l2_ctrl_query_fill(qc, 0x0, 0xff, 1, 0x80);
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(qc, 0x0, 0xff, 1, 0x80);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(qc, 0x0, 0xff, 1, 0x80);
	case V4L2_CID_SHARPNESS:
		return v4l2_ctrl_query_fill(qc, 0x0, 0xff, 1, 0x80);
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		return v4l2_ctrl_query_fill(
			qc, V4L2_MPEG_AUDIO_ENCODING_AAC,
			ac3 ? V4L2_MPEG_AUDIO_ENCODING_AC3
			: V4L2_MPEG_AUDIO_ENCODING_AAC,
			1, V4L2_MPEG_AUDIO_ENCODING_AAC);
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		return v4l2_ctrl_query_fill(
			qc, V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC,
			V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC, 1,
			V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC);

/* 	case V4L2_CID_MPEG_VIDEO_? maybe keyframe interval: */
/* 		return v4l2_ctrl_query_fill(qc, 0, 128, 128, 0); */
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		return v4l2_ctrl_query_fill(
			qc, V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
			V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 1,
			V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);

	case V4L2_CID_MPEG_VIDEO_BITRATE:
		return v4l2_ctrl_query_fill(qc, 1000000, 13500000, 100000,
					    6500000);
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		err = v4l2_ctrl_query_fill(qc, 1100000, 20200000, 100000,
					   9000000);
		if (!err && opt->bitrate_mode == HDPVR_CONSTANT)
			qc->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;
	default:
		return -EINVAL;
	}
}

static int vidioc_queryctrl(struct file *file, void *private_data,
			    struct v4l2_queryctrl *qc)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int i, next;
	u32 id = qc->id;

	memset(qc, 0, sizeof(*qc));

	next = !!(id &  V4L2_CTRL_FLAG_NEXT_CTRL);
	qc->id = id & ~V4L2_CTRL_FLAG_NEXT_CTRL;

	for (i = 0; i < ARRAY_SIZE(supported_v4l2_ctrls); i++) {
		if (next) {
			if (qc->id < supported_v4l2_ctrls[i])
				qc->id = supported_v4l2_ctrls[i];
			else
				continue;
		}

		if (qc->id == supported_v4l2_ctrls[i])
			return fill_queryctrl(&dev->options, qc,
					      dev->flags & HDPVR_FLAG_AC3_CAP);

		if (qc->id < supported_v4l2_ctrls[i])
			break;
	}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *private_data,
			 struct v4l2_control *ctrl)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = dev->options.brightness;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = dev->options.contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = dev->options.saturation;
		break;
	case V4L2_CID_HUE:
		ctrl->value = dev->options.hue;
		break;
	case V4L2_CID_SHARPNESS:
		ctrl->value = dev->options.sharpness;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *private_data,
			 struct v4l2_control *ctrl)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int retval;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		retval = hdpvr_config_call(dev, CTRL_BRIGHTNESS, ctrl->value);
		if (!retval)
			dev->options.brightness = ctrl->value;
		break;
	case V4L2_CID_CONTRAST:
		retval = hdpvr_config_call(dev, CTRL_CONTRAST, ctrl->value);
		if (!retval)
			dev->options.contrast = ctrl->value;
		break;
	case V4L2_CID_SATURATION:
		retval = hdpvr_config_call(dev, CTRL_SATURATION, ctrl->value);
		if (!retval)
			dev->options.saturation = ctrl->value;
		break;
	case V4L2_CID_HUE:
		retval = hdpvr_config_call(dev, CTRL_HUE, ctrl->value);
		if (!retval)
			dev->options.hue = ctrl->value;
		break;
	case V4L2_CID_SHARPNESS:
		retval = hdpvr_config_call(dev, CTRL_SHARPNESS, ctrl->value);
		if (!retval)
			dev->options.sharpness = ctrl->value;
		break;
	default:
		return -EINVAL;
	}

	return retval;
}


static int hdpvr_get_ctrl(struct hdpvr_options *opt,
			  struct v4l2_ext_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		ctrl->value = opt->audio_codec;
		break;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		ctrl->value = V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC;
		break;
/* 	case V4L2_CID_MPEG_VIDEO_B_FRAMES: */
/* 		ctrl->value = (opt->gop_mode & 0x2) ? 0 : 128; */
/* 		break; */
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		ctrl->value = opt->bitrate_mode == HDPVR_CONSTANT
			? V4L2_MPEG_VIDEO_BITRATE_MODE_CBR
			: V4L2_MPEG_VIDEO_BITRATE_MODE_VBR;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctrl->value = opt->bitrate * 100000;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		ctrl->value = opt->peak_bitrate * 100000;
		break;
	case V4L2_CID_MPEG_STREAM_TYPE:
		ctrl->value = V4L2_MPEG_STREAM_TYPE_MPEG2_TS;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_ext_ctrls(struct file *file, void *priv,
			      struct v4l2_ext_controls *ctrls)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int i, err = 0;

	if (ctrls->ctrl_class == V4L2_CTRL_CLASS_MPEG) {
		for (i = 0; i < ctrls->count; i++) {
			struct v4l2_ext_control *ctrl = ctrls->controls + i;

			err = hdpvr_get_ctrl(&dev->options, ctrl);
			if (err) {
				ctrls->error_idx = i;
				break;
			}
		}
		return err;

	}

	return -EINVAL;
}


static int hdpvr_try_ctrl(struct v4l2_ext_control *ctrl, int ac3)
{
	int ret = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		if (ctrl->value == V4L2_MPEG_AUDIO_ENCODING_AAC ||
		    (ac3 && ctrl->value == V4L2_MPEG_AUDIO_ENCODING_AC3))
			ret = 0;
		break;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		if (ctrl->value == V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC)
			ret = 0;
		break;
/* 	case V4L2_CID_MPEG_VIDEO_B_FRAMES: */
/* 		if (ctrl->value == 0 || ctrl->value == 128) */
/* 			ret = 0; */
/* 		break; */
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		if (ctrl->value == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR ||
		    ctrl->value == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)
			ret = 0;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
	{
		uint bitrate = ctrl->value / 100000;
		if (bitrate >= 10 && bitrate <= 135)
			ret = 0;
		break;
	}
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
	{
		uint peak_bitrate = ctrl->value / 100000;
		if (peak_bitrate >= 10 && peak_bitrate <= 202)
			ret = 0;
		break;
	}
	case V4L2_CID_MPEG_STREAM_TYPE:
		if (ctrl->value == V4L2_MPEG_STREAM_TYPE_MPEG2_TS)
			ret = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_try_ext_ctrls(struct file *file, void *priv,
				struct v4l2_ext_controls *ctrls)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int i, err = 0;

	if (ctrls->ctrl_class == V4L2_CTRL_CLASS_MPEG) {
		for (i = 0; i < ctrls->count; i++) {
			struct v4l2_ext_control *ctrl = ctrls->controls + i;

			err = hdpvr_try_ctrl(ctrl,
					     dev->flags & HDPVR_FLAG_AC3_CAP);
			if (err) {
				ctrls->error_idx = i;
				break;
			}
		}
		return err;
	}

	return -EINVAL;
}


static int hdpvr_set_ctrl(struct hdpvr_device *dev,
			  struct v4l2_ext_control *ctrl)
{
	struct hdpvr_options *opt = &dev->options;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		if (dev->flags & HDPVR_FLAG_AC3_CAP) {
			opt->audio_codec = ctrl->value;
			ret = hdpvr_set_audio(dev, opt->audio_input,
					      opt->audio_codec);
		}
		break;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		break;
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
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		if (ctrl->value == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR &&
		    opt->bitrate_mode != HDPVR_CONSTANT) {
			opt->bitrate_mode = HDPVR_CONSTANT;
			hdpvr_config_call(dev, CTRL_BITRATE_MODE_VALUE,
					  opt->bitrate_mode);
		}
		if (ctrl->value == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
		    opt->bitrate_mode == HDPVR_CONSTANT) {
			opt->bitrate_mode = HDPVR_VARIABLE_AVERAGE;
			hdpvr_config_call(dev, CTRL_BITRATE_MODE_VALUE,
					  opt->bitrate_mode);
		}
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE: {
		uint bitrate = ctrl->value / 100000;

		opt->bitrate = bitrate;
		if (bitrate >= opt->peak_bitrate)
			opt->peak_bitrate = bitrate+1;

		hdpvr_set_bitrate(dev);
		break;
	}
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK: {
		uint peak_bitrate = ctrl->value / 100000;

		if (opt->bitrate_mode == HDPVR_CONSTANT)
			break;

		if (opt->bitrate < peak_bitrate) {
			opt->peak_bitrate = peak_bitrate;
			hdpvr_set_bitrate(dev);
		} else
			ret = -EINVAL;
		break;
	}
	case V4L2_CID_MPEG_STREAM_TYPE:
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int vidioc_s_ext_ctrls(struct file *file, void *priv,
			      struct v4l2_ext_controls *ctrls)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	int i, err = 0;

	if (ctrls->ctrl_class == V4L2_CTRL_CLASS_MPEG) {
		for (i = 0; i < ctrls->count; i++) {
			struct v4l2_ext_control *ctrl = ctrls->controls + i;

			err = hdpvr_try_ctrl(ctrl,
					     dev->flags & HDPVR_FLAG_AC3_CAP);
			if (err) {
				ctrls->error_idx = i;
				break;
			}
			err = hdpvr_set_ctrl(dev, ctrl);
			if (err) {
				ctrls->error_idx = i;
				break;
			}
		}
		return err;

	}

	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *private_data,
				    struct v4l2_fmtdesc *f)
{

	if (f->index != 0 || f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	strncpy(f->description, "MPEG2-TS with AVC/AAC streams", 32);
	f->pixelformat = V4L2_PIX_FMT_MPEG;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *private_data,
				struct v4l2_format *f)
{
	struct hdpvr_fh *fh = file->private_data;
	struct hdpvr_device *dev = fh->dev;
	struct hdpvr_video_info *vid_info;

	if (!dev)
		return -ENODEV;

	vid_info = get_video_info(dev);
	if (!vid_info)
		return -EFAULT;

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->fmt.pix.pixelformat	= V4L2_PIX_FMT_MPEG;
	f->fmt.pix.width	= vid_info->width;
	f->fmt.pix.height	= vid_info->height;
	f->fmt.pix.sizeimage	= dev->bulk_in_size;
	f->fmt.pix.colorspace	= 0;
	f->fmt.pix.bytesperline	= 0;
	f->fmt.pix.field	= V4L2_FIELD_ANY;

	kfree(vid_info);
	return 0;
}

static int vidioc_encoder_cmd(struct file *filp, void *priv,
			       struct v4l2_encoder_cmd *a)
{
	struct hdpvr_fh *fh = filp->private_data;
	struct hdpvr_device *dev = fh->dev;
	int res;

	mutex_lock(&dev->io_mutex);

	memset(&a->raw, 0, sizeof(a->raw));
	switch (a->cmd) {
	case V4L2_ENC_CMD_START:
		a->flags = 0;
		res = hdpvr_start_streaming(dev);
		break;
	case V4L2_ENC_CMD_STOP:
		res = hdpvr_stop_streaming(dev);
		break;
	default:
		v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
			 "Unsupported encoder cmd %d\n", a->cmd);
		res = -EINVAL;
	}
	mutex_unlock(&dev->io_mutex);
	return res;
}

static int vidioc_try_encoder_cmd(struct file *filp, void *priv,
					struct v4l2_encoder_cmd *a)
{
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
	.vidioc_enum_input	= vidioc_enum_input,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_enumaudio	= vidioc_enumaudio,
	.vidioc_g_audio		= vidioc_g_audio,
	.vidioc_s_audio		= vidioc_s_audio,
	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl,
	.vidioc_g_ext_ctrls	= vidioc_g_ext_ctrls,
	.vidioc_s_ext_ctrls	= vidioc_s_ext_ctrls,
	.vidioc_try_ext_ctrls	= vidioc_try_ext_ctrls,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt_vid_cap,
	.vidioc_encoder_cmd	= vidioc_encoder_cmd,
	.vidioc_try_encoder_cmd	= vidioc_try_encoder_cmd,
};

static void hdpvr_device_release(struct video_device *vdev)
{
	struct hdpvr_device *dev = video_get_drvdata(vdev);

	hdpvr_delete(dev);
}

static const struct video_device hdpvr_video_template = {
/* 	.type			= VFL_TYPE_GRABBER, */
/* 	.type2			= VID_TYPE_CAPTURE | VID_TYPE_MPEG_ENCODER, */
	.fops			= &hdpvr_fops,
	.release		= hdpvr_device_release,
	.ioctl_ops 		= &hdpvr_ioctl_ops,
	.tvnorms 		=
		V4L2_STD_NTSC  | V4L2_STD_SECAM | V4L2_STD_PAL_B |
		V4L2_STD_PAL_G | V4L2_STD_PAL_H | V4L2_STD_PAL_I |
		V4L2_STD_PAL_D | V4L2_STD_PAL_M | V4L2_STD_PAL_N |
		V4L2_STD_PAL_60,
	.current_norm 		= V4L2_STD_NTSC | V4L2_STD_PAL_M |
		V4L2_STD_PAL_60,
};

int hdpvr_register_videodev(struct hdpvr_device *dev, struct device *parent,
			    int devnum)
{
	/* setup and register video device */
	dev->video_dev = video_device_alloc();
	if (!dev->video_dev) {
		v4l2_err(&dev->v4l2_dev, "video_device_alloc() failed\n");
		goto error;
	}

	*(dev->video_dev) = hdpvr_video_template;
	strcpy(dev->video_dev->name, "Hauppauge HD PVR");
	dev->video_dev->parent = parent;
	video_set_drvdata(dev->video_dev, dev);

	if (video_register_device(dev->video_dev, VFL_TYPE_GRABBER, devnum)) {
		v4l2_err(&dev->v4l2_dev, "video_device registration failed\n");
		goto error;
	}

	return 0;
error:
	return -ENOMEM;
}
