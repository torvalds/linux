/*
 * stk-webcam.c : Driver for Syntek 1125 USB webcam controller
 *
 * Copyright (C) 2006 Nicolas VIVIEN
 * Copyright 2007-2008 Jaime Velasco Juan <jsagarribay@gmail.com>
 *
 * Some parts are inspired from cafe_ccic.c
 * Copyright 2006-2007 Jonathan Corbet
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/kref.h>

#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

#include "stk-webcam.h"


static int hflip = 1;
module_param(hflip, bool, 0444);
MODULE_PARM_DESC(hflip, "Horizontal image flip (mirror). Defaults to 1");

static int vflip = 1;
module_param(vflip, bool, 0444);
MODULE_PARM_DESC(vflip, "Vertical image flip. Defaults to 1");

static int debug;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Debug v4l ioctls. Defaults to 0");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaime Velasco Juan <jsagarribay@gmail.com> and Nicolas VIVIEN");
MODULE_DESCRIPTION("Syntek DC1125 webcam driver");



/* Some cameras have audio interfaces, we aren't interested in those */
static struct usb_device_id stkwebcam_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x174f, 0xa311, 0xff, 0xff, 0xff) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x05e1, 0x0501, 0xff, 0xff, 0xff) },
	{ }
};
MODULE_DEVICE_TABLE(usb, stkwebcam_table);

void stk_camera_cleanup(struct kref *kref)
{
	struct stk_camera *dev = to_stk_camera(kref);

	STK_INFO("Syntek USB2.0 Camera release resources"
		" video device /dev/video%d\n", dev->vdev.minor);
	video_unregister_device(&dev->vdev);
	dev->vdev.priv = NULL;

	if (dev->sio_bufs != NULL || dev->isobufs != NULL)
		STK_ERROR("We are leaking memory\n");
	usb_put_intf(dev->interface);
	kfree(dev);
}


/*
 * Basic stuff
 */
int stk_camera_write_reg(struct stk_camera *dev, u16 index, u8 value)
{
	struct usb_device *udev = dev->udev;
	int ret;

	ret =  usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			NULL,
			0,
			500);
	if (ret < 0)
		return ret;
	else
		return 0;
}

int stk_camera_read_reg(struct stk_camera *dev, u16 index, int *value)
{
	struct usb_device *udev = dev->udev;
	int ret;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x00,
			index,
			(u8 *) value,
			sizeof(u8),
			500);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static int stk_start_stream(struct stk_camera *dev)
{
	int value;
	int i, ret;
	int value_116, value_117;

	if (!is_present(dev))
		return -ENODEV;
	if (!is_memallocd(dev) || !is_initialised(dev)) {
		STK_ERROR("FIXME: Buffers are not allocated\n");
		return -EFAULT;
	}
	ret = usb_set_interface(dev->udev, 0, 5);

	if (ret < 0)
		STK_ERROR("usb_set_interface failed !\n");
	if (stk_sensor_wakeup(dev))
		STK_ERROR("error awaking the sensor\n");

	stk_camera_read_reg(dev, 0x0116, &value_116);
	stk_camera_read_reg(dev, 0x0117, &value_117);

	stk_camera_write_reg(dev, 0x0116, 0x0000);
	stk_camera_write_reg(dev, 0x0117, 0x0000);

	stk_camera_read_reg(dev, 0x0100, &value);
	stk_camera_write_reg(dev, 0x0100, value | 0x80);

	stk_camera_write_reg(dev, 0x0116, value_116);
	stk_camera_write_reg(dev, 0x0117, value_117);
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (dev->isobufs[i].urb) {
			ret = usb_submit_urb(dev->isobufs[i].urb, GFP_KERNEL);
			atomic_inc(&dev->urbs_used);
			if (ret)
				return ret;
		}
	}
	set_streaming(dev);
	return 0;
}

static int stk_stop_stream(struct stk_camera *dev)
{
	int value;
	int i;
	if (is_present(dev)) {
		stk_camera_read_reg(dev, 0x0100, &value);
		stk_camera_write_reg(dev, 0x0100, value & ~0x80);
		if (dev->isobufs != NULL) {
			for (i = 0; i < MAX_ISO_BUFS; i++) {
				if (dev->isobufs[i].urb)
					usb_kill_urb(dev->isobufs[i].urb);
			}
		}
		unset_streaming(dev);

		if (usb_set_interface(dev->udev, 0, 0))
			STK_ERROR("usb_set_interface failed !\n");
		if (stk_sensor_sleep(dev))
			STK_ERROR("error suspending the sensor\n");
	}
	return 0;
}

/*
 * This seems to be the shortest init sequence we
 * must do in order to find the sensor
 * Bit 5 of reg. 0x0000 here is important, when reset to 0 the sensor
 * is also reset. Maybe powers down it?
 * Rest of values don't make a difference
 */

static struct regval stk1125_initvals[] = {
	/*TODO: What means this sequence? */
	{0x0000, 0x24},
	{0x0100, 0x21},
	{0x0002, 0x68},
	{0x0003, 0x80},
	{0x0005, 0x00},
	{0x0007, 0x03},
	{0x000d, 0x00},
	{0x000f, 0x02},
	{0x0300, 0x12},
	{0x0350, 0x41},
	{0x0351, 0x00},
	{0x0352, 0x00},
	{0x0353, 0x00},
	{0x0018, 0x10},
	{0x0019, 0x00},
	{0x001b, 0x0e},
	{0x001c, 0x46},
	{0x0300, 0x80},
	{0x001a, 0x04},
	{0x0110, 0x00},
	{0x0111, 0x00},
	{0x0112, 0x00},
	{0x0113, 0x00},

	{0xffff, 0xff},
};


static int stk_initialise(struct stk_camera *dev)
{
	struct regval *rv;
	int ret;
	if (!is_present(dev))
		return -ENODEV;
	if (is_initialised(dev))
		return 0;
	rv = stk1125_initvals;
	while (rv->reg != 0xffff) {
		ret = stk_camera_write_reg(dev, rv->reg, rv->val);
		if (ret)
			return ret;
		rv++;
	}
	if (stk_sensor_init(dev) == 0) {
		set_initialised(dev);
		return 0;
	} else
		return -1;
}

/* sysfs functions */
/*FIXME cleanup this */

static ssize_t show_brightness(struct device *class,
			struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(class);
	struct stk_camera *dev = vdev_to_camera(vdev);

	return sprintf(buf, "%X\n", dev->vsettings.brightness);
}

static ssize_t store_brightness(struct device *class,
		struct device_attribute *attr, const char *buf, size_t count)
{
	char *endp;
	unsigned long value;
	int ret;

	struct video_device *vdev = to_video_device(class);
	struct stk_camera *dev = vdev_to_camera(vdev);

	value = simple_strtoul(buf, &endp, 16);

	dev->vsettings.brightness = (int) value;

	ret = stk_sensor_set_brightness(dev, value >> 8);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t show_hflip(struct device *class,
		struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(class);
	struct stk_camera *dev = vdev_to_camera(vdev);

	return sprintf(buf, "%d\n", dev->vsettings.hflip);
}

static ssize_t store_hflip(struct device *class,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(class);
	struct stk_camera *dev = vdev_to_camera(vdev);

	if (strncmp(buf, "1", 1) == 0)
		dev->vsettings.hflip = 1;
	else if (strncmp(buf, "0", 1) == 0)
		dev->vsettings.hflip = 0;
	else
		return -EINVAL;

	return strlen(buf);
}

static ssize_t show_vflip(struct device *class,
		struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(class);
	struct stk_camera *dev = vdev_to_camera(vdev);

	return sprintf(buf, "%d\n", dev->vsettings.vflip);
}

static ssize_t store_vflip(struct device *class,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(class);
	struct stk_camera *dev = vdev_to_camera(vdev);

	if (strncmp(buf, "1", 1) == 0)
		dev->vsettings.vflip = 1;
	else if (strncmp(buf, "0", 1) == 0)
		dev->vsettings.vflip = 0;
	else
		return -EINVAL;

	return strlen(buf);
}

static DEVICE_ATTR(brightness, S_IRUGO | S_IWUGO,
			show_brightness, store_brightness);
static DEVICE_ATTR(hflip, S_IRUGO | S_IWUGO, show_hflip, store_hflip);
static DEVICE_ATTR(vflip, S_IRUGO | S_IWUGO, show_vflip, store_vflip);

static int stk_create_sysfs_files(struct video_device *vdev)
{
	int ret;

	ret = video_device_create_file(vdev, &dev_attr_brightness);
	ret += video_device_create_file(vdev, &dev_attr_hflip);
	ret += video_device_create_file(vdev, &dev_attr_vflip);
	return ret;
}

static void stk_remove_sysfs_files(struct video_device *vdev)
{
	video_device_remove_file(vdev, &dev_attr_brightness);
	video_device_remove_file(vdev, &dev_attr_hflip);
	video_device_remove_file(vdev, &dev_attr_vflip);
}


/* *********************************************** */
/*
 * This function is called as an URB transfert is complete (Isochronous pipe).
 * So, the traitement is done in interrupt time, so it has be fast, not crash,
 * and not stall. Neat.
 */
static void stk_isoc_handler(struct urb *urb)
{
	int i;
	int ret;
	int framelen;
	unsigned long flags;

	unsigned char *fill = NULL;
	unsigned char *iso_buf = NULL;

	struct stk_camera *dev;
	struct stk_sio_buffer *fb;

	dev = (struct stk_camera *) urb->context;

	if (dev == NULL) {
		STK_ERROR("isoc_handler called with NULL device !\n");
		return;
	}

	if (urb->status == -ENOENT || urb->status == -ECONNRESET
		|| urb->status == -ESHUTDOWN) {
		atomic_dec(&dev->urbs_used);
		return;
	}

	spin_lock_irqsave(&dev->spinlock, flags);

	if (urb->status != -EINPROGRESS && urb->status != 0) {
		STK_ERROR("isoc_handler: urb->status == %d\n", urb->status);
		goto resubmit;
	}

	if (list_empty(&dev->sio_avail)) {
		/*FIXME Stop streaming after a while */
		(void) (printk_ratelimit() &&
		STK_ERROR("isoc_handler without available buffer!\n"));
		goto resubmit;
	}
	fb = list_first_entry(&dev->sio_avail,
			struct stk_sio_buffer, list);
	fill = fb->buffer + fb->v4lbuf.bytesused;

	for (i = 0; i < urb->number_of_packets; i++) {
		if (urb->iso_frame_desc[i].status != 0) {
			if (urb->iso_frame_desc[i].status != -EXDEV)
				STK_ERROR("Frame %d has error %d\n", i,
					urb->iso_frame_desc[i].status);
			continue;
		}
		framelen = urb->iso_frame_desc[i].actual_length;
		iso_buf = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		if (framelen <= 4)
			continue; /* no data */

		/*
		 * we found something informational from there
		 * the isoc frames have to type of headers
		 * type1: 00 xx 00 00 or 20 xx 00 00
		 * type2: 80 xx 00 00 00 00 00 00 or a0 xx 00 00 00 00 00 00
		 * xx is a sequencer which has never been seen over 0x3f
		 * imho data written down looks like bayer, i see similarities
		 * after every 640 bytes
		 */
		if (*iso_buf & 0x80) {
			framelen -= 8;
			iso_buf += 8;
			/* This marks a new frame */
			if (fb->v4lbuf.bytesused != 0
				&& fb->v4lbuf.bytesused != dev->frame_size) {
				(void) (printk_ratelimit() &&
				STK_ERROR("frame %d, "
					"bytesused=%d, skipping\n",
					i, fb->v4lbuf.bytesused));
				fb->v4lbuf.bytesused = 0;
				fill = fb->buffer;
			} else if (fb->v4lbuf.bytesused == dev->frame_size) {
				list_move_tail(dev->sio_avail.next,
					&dev->sio_full);
				wake_up(&dev->wait_frame);
				if (list_empty(&dev->sio_avail)) {
					(void) (printk_ratelimit() &&
					STK_ERROR("No buffer available\n"));
					goto resubmit;
				}
				fb = list_first_entry(&dev->sio_avail,
					struct stk_sio_buffer, list);
				fb->v4lbuf.bytesused = 0;
				fill = fb->buffer;
			}
		} else {
			framelen -= 4;
			iso_buf += 4;
		}

		/* Our buffer is full !!! */
		if (framelen + fb->v4lbuf.bytesused > dev->frame_size) {
			(void) (printk_ratelimit() &&
			STK_ERROR("Frame buffer overflow, lost sync\n"));
			/*FIXME Do something here? */
			continue;
		}
		spin_unlock_irqrestore(&dev->spinlock, flags);
		memcpy(fill, iso_buf, framelen);
		spin_lock_irqsave(&dev->spinlock, flags);
		fill += framelen;

		/* New size of our buffer */
		fb->v4lbuf.bytesused += framelen;
	}

resubmit:
	spin_unlock_irqrestore(&dev->spinlock, flags);
	urb->dev = dev->udev;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret != 0) {
		STK_ERROR("Error (%d) re-submitting urb in stk_isoc_handler.\n",
			ret);
	}
}

/* -------------------------------------------- */

static int stk_prepare_iso(struct stk_camera *dev)
{
	void *kbuf;
	int i, j;
	struct urb *urb;
	struct usb_device *udev;

	if (dev == NULL)
		return -ENXIO;
	udev = dev->udev;

	if (dev->isobufs)
		STK_ERROR("isobufs already allocated. Bad\n");
	else
		dev->isobufs = kzalloc(MAX_ISO_BUFS * sizeof(*dev->isobufs),
					GFP_KERNEL);
	if (dev->isobufs == NULL) {
		STK_ERROR("Unable to allocate iso buffers\n");
		return -ENOMEM;
	}
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (dev->isobufs[i].data == NULL) {
			kbuf = kzalloc(ISO_BUFFER_SIZE, GFP_KERNEL);
			if (kbuf == NULL) {
				STK_ERROR("Failed to allocate iso buffer %d\n",
					i);
				goto isobufs_out;
			}
			dev->isobufs[i].data = kbuf;
		} else
			STK_ERROR("isobuf data already allocated\n");
		if (dev->isobufs[i].urb == NULL) {
			urb = usb_alloc_urb(ISO_FRAMES_PER_DESC, GFP_KERNEL);
			if (urb == NULL) {
				STK_ERROR("Failed to allocate URB %d\n", i);
				goto isobufs_out;
			}
			dev->isobufs[i].urb = urb;
		} else {
			STK_ERROR("Killing URB\n");
			usb_kill_urb(dev->isobufs[i].urb);
			urb = dev->isobufs[i].urb;
		}
		urb->interval = 1;
		urb->dev = udev;
		urb->pipe = usb_rcvisocpipe(udev, dev->isoc_ep);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = dev->isobufs[i].data;
		urb->transfer_buffer_length = ISO_BUFFER_SIZE;
		urb->complete = stk_isoc_handler;
		urb->context = dev;
		urb->start_frame = 0;
		urb->number_of_packets = ISO_FRAMES_PER_DESC;

		for (j = 0; j < ISO_FRAMES_PER_DESC; j++) {
			urb->iso_frame_desc[j].offset = j * ISO_MAX_FRAME_SIZE;
			urb->iso_frame_desc[j].length = ISO_MAX_FRAME_SIZE;
		}
	}
	set_memallocd(dev);
	return 0;

isobufs_out:
	for (i = 0; i < MAX_ISO_BUFS && dev->isobufs[i].data; i++)
		kfree(dev->isobufs[i].data);
	for (i = 0; i < MAX_ISO_BUFS && dev->isobufs[i].urb; i++)
		usb_free_urb(dev->isobufs[i].urb);
	kfree(dev->isobufs);
	dev->isobufs = NULL;
	return -ENOMEM;
}

static void stk_clean_iso(struct stk_camera *dev)
{
	int i;

	if (dev == NULL || dev->isobufs == NULL)
		return;

	for (i = 0; i < MAX_ISO_BUFS; i++) {
		struct urb *urb;

		urb = dev->isobufs[i].urb;
		if (urb) {
			if (atomic_read(&dev->urbs_used))
				usb_kill_urb(urb);
			usb_free_urb(urb);
		}
		kfree(dev->isobufs[i].data);
	}
	kfree(dev->isobufs);
	dev->isobufs = NULL;
	unset_memallocd(dev);
}

static int stk_setup_siobuf(struct stk_camera *dev, int index)
{
	struct stk_sio_buffer *buf = dev->sio_bufs + index;
	INIT_LIST_HEAD(&buf->list);
	buf->v4lbuf.length = PAGE_ALIGN(dev->frame_size);
	buf->buffer = vmalloc_user(buf->v4lbuf.length);
	if (buf->buffer == NULL)
		return -ENOMEM;
	buf->mapcount = 0;
	buf->dev = dev;
	buf->v4lbuf.index = index;
	buf->v4lbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->v4lbuf.field = V4L2_FIELD_NONE;
	buf->v4lbuf.memory = V4L2_MEMORY_MMAP;
	buf->v4lbuf.m.offset = 2*index*buf->v4lbuf.length;
	return 0;
}

static int stk_free_sio_buffers(struct stk_camera *dev)
{
	int i;
	int nbufs;
	unsigned long flags;
	if (dev->n_sbufs == 0 || dev->sio_bufs == NULL)
		return 0;
	/*
	* If any buffers are mapped, we cannot free them at all.
	*/
	for (i = 0; i < dev->n_sbufs; i++) {
		if (dev->sio_bufs[i].mapcount > 0)
			return -EBUSY;
	}
	/*
	* OK, let's do it.
	*/
	spin_lock_irqsave(&dev->spinlock, flags);
	INIT_LIST_HEAD(&dev->sio_avail);
	INIT_LIST_HEAD(&dev->sio_full);
	nbufs = dev->n_sbufs;
	dev->n_sbufs = 0;
	spin_unlock_irqrestore(&dev->spinlock, flags);
	for (i = 0; i < nbufs; i++) {
		if (dev->sio_bufs[i].buffer != NULL)
			vfree(dev->sio_bufs[i].buffer);
	}
	kfree(dev->sio_bufs);
	dev->sio_bufs = NULL;
	return 0;
}

static int stk_prepare_sio_buffers(struct stk_camera *dev, unsigned n_sbufs)
{
	int i;
	if (dev->sio_bufs != NULL)
		STK_ERROR("sio_bufs already allocated\n");
	else {
		dev->sio_bufs = kzalloc(n_sbufs * sizeof(struct stk_sio_buffer),
				GFP_KERNEL);
		if (dev->sio_bufs == NULL)
			return -ENOMEM;
		for (i = 0; i < n_sbufs; i++) {
			if (stk_setup_siobuf(dev, i))
				return (dev->n_sbufs > 1)? 0 : -ENOMEM;
			dev->n_sbufs = i+1;
		}
	}
	return 0;
}

static int stk_allocate_buffers(struct stk_camera *dev, unsigned n_sbufs)
{
	int err;
	err = stk_prepare_iso(dev);
	if (err) {
		stk_clean_iso(dev);
		return err;
	}
	err = stk_prepare_sio_buffers(dev, n_sbufs);
	if (err) {
		stk_free_sio_buffers(dev);
		return err;
	}
	return 0;
}

static void stk_free_buffers(struct stk_camera *dev)
{
	stk_clean_iso(dev);
	stk_free_sio_buffers(dev);
}
/* -------------------------------------------- */

/* v4l file operations */

static int v4l_stk_open(struct inode *inode, struct file *fp)
{
	struct stk_camera *dev;
	struct video_device *vdev;

	vdev = video_devdata(fp);
	dev = vdev_to_camera(vdev);

	if (dev == NULL || !is_present(dev))
		return -ENXIO;
	fp->private_data = vdev;
	kref_get(&dev->kref);

	return 0;
}

static int v4l_stk_release(struct inode *inode, struct file *fp)
{
	struct stk_camera *dev;
	struct video_device *vdev;

	vdev = video_devdata(fp);
	if (vdev == NULL) {
		STK_ERROR("v4l_release called w/o video devdata\n");
		return -EFAULT;
	}
	dev = vdev_to_camera(vdev);
	if (dev == NULL) {
		STK_ERROR("v4l_release called on removed device\n");
		return -ENODEV;
	}

	if (dev->owner != fp) {
		kref_put(&dev->kref, stk_camera_cleanup);
		return 0;
	}

	stk_stop_stream(dev);

	stk_free_buffers(dev);

	dev->owner = NULL;

	kref_put(&dev->kref, stk_camera_cleanup);

	return 0;
}

static ssize_t v4l_stk_read(struct file *fp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	int i;
	int ret;
	unsigned long flags;
	struct stk_camera *dev;
	struct video_device *vdev;
	struct stk_sio_buffer *sbuf;

	vdev = video_devdata(fp);
	if (vdev == NULL)
		return -EFAULT;
	dev = vdev_to_camera(vdev);

	if (dev == NULL)
		return -EIO;

	if (!is_present(dev))
		return -EIO;
	if (dev->owner && dev->owner != fp)
		return -EBUSY;
	dev->owner = fp;
	if (!is_streaming(dev)) {
		if (stk_initialise(dev)
			|| stk_allocate_buffers(dev, 3)
			|| stk_start_stream(dev))
			return -ENOMEM;
		spin_lock_irqsave(&dev->spinlock, flags);
		for (i = 0; i < dev->n_sbufs; i++) {
			list_add_tail(&dev->sio_bufs[i].list, &dev->sio_avail);
			dev->sio_bufs[i].v4lbuf.flags = V4L2_BUF_FLAG_QUEUED;
		}
		spin_unlock_irqrestore(&dev->spinlock, flags);
	}
	if (*f_pos == 0) {
		if (fp->f_flags & O_NONBLOCK && list_empty(&dev->sio_full))
			return -EWOULDBLOCK;
		ret = wait_event_interruptible(dev->wait_frame,
			!list_empty(&dev->sio_full) || !is_present(dev));
		if (ret)
			return ret;
		if (!is_present(dev))
			return -EIO;
	}
	if (count + *f_pos > dev->frame_size)
		count = dev->frame_size - *f_pos;
	spin_lock_irqsave(&dev->spinlock, flags);
	if (list_empty(&dev->sio_full)) {
		spin_unlock_irqrestore(&dev->spinlock, flags);
		STK_ERROR("BUG: No siobufs ready\n");
		return 0;
	}
	sbuf = list_first_entry(&dev->sio_full, struct stk_sio_buffer, list);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	if (copy_to_user(buf, sbuf->buffer + *f_pos, count))
		return -EFAULT;

	*f_pos += count;

	if (*f_pos >= dev->frame_size) {
		*f_pos = 0;
		spin_lock_irqsave(&dev->spinlock, flags);
		list_move_tail(&sbuf->list, &dev->sio_avail);
		spin_unlock_irqrestore(&dev->spinlock, flags);
	}
	return count;
}

static unsigned int v4l_stk_poll(struct file *fp, poll_table *wait)
{
	struct stk_camera *dev;
	struct video_device *vdev;

	vdev = video_devdata(fp);

	if (vdev == NULL)
		return -EFAULT;

	dev = vdev_to_camera(vdev);
	if (dev == NULL)
		return -ENODEV;

	poll_wait(fp, &dev->wait_frame, wait);

	if (!is_present(dev))
		return POLLERR;

	if (!list_empty(&dev->sio_full))
		return (POLLIN | POLLRDNORM);

	return 0;
}


static void stk_v4l_vm_open(struct vm_area_struct *vma)
{
	struct stk_sio_buffer *sbuf = vma->vm_private_data;
	sbuf->mapcount++;
}
static void stk_v4l_vm_close(struct vm_area_struct *vma)
{
	struct stk_sio_buffer *sbuf = vma->vm_private_data;
	sbuf->mapcount--;
	if (sbuf->mapcount == 0)
		sbuf->v4lbuf.flags &= ~V4L2_BUF_FLAG_MAPPED;
}
static struct vm_operations_struct stk_v4l_vm_ops = {
	.open = stk_v4l_vm_open,
	.close = stk_v4l_vm_close
};

static int v4l_stk_mmap(struct file *fp, struct vm_area_struct *vma)
{
	unsigned int i;
	int ret;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct stk_camera *dev;
	struct video_device *vdev;
	struct stk_sio_buffer *sbuf = NULL;

	if (!(vma->vm_flags & VM_WRITE) || !(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	vdev = video_devdata(fp);
	dev = vdev_to_camera(vdev);

	for (i = 0; i < dev->n_sbufs; i++) {
		if (dev->sio_bufs[i].v4lbuf.m.offset == offset) {
			sbuf = dev->sio_bufs + i;
			break;
		}
	}
	if (sbuf == NULL)
		return -EINVAL;
	ret = remap_vmalloc_range(vma, sbuf->buffer, 0);
	if (ret)
		return ret;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_private_data = sbuf;
	vma->vm_ops = &stk_v4l_vm_ops;
	sbuf->v4lbuf.flags |= V4L2_BUF_FLAG_MAPPED;
	stk_v4l_vm_open(vma);
	return 0;
}

/* v4l ioctl handlers */

static int stk_vidioc_querycap(struct file *filp,
		void *priv, struct v4l2_capability *cap)
{
	strcpy(cap->driver, "stk");
	strcpy(cap->card, "stk");
	cap->version = DRIVER_VERSION_NUM;

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE
		| V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	return 0;
}

static int stk_vidioc_enum_input(struct file *filp,
		void *priv, struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	strcpy(input->name, "Syntek USB Camera");
	input->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}


static int stk_vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int stk_vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	else
		return 0;
}

/* from vivi.c */
static int stk_vidioc_s_std(struct file *filp, void *priv, v4l2_std_id *a)
{
	return 0;
}

/* List of all V4Lv2 controls supported by the driver */
static struct v4l2_queryctrl stk_controls[] = {
	{
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 0xffff,
		.step    = 0x0100,
		.default_value = 0x6000,
	},
	/*TODO: get more controls to work */
};

static int stk_vidioc_queryctrl(struct file *filp,
		void *priv, struct v4l2_queryctrl *c)
{
	int i;
	int nbr;
	nbr = ARRAY_SIZE(stk_controls);

	for (i = 0; i < nbr; i++) {
		if (stk_controls[i].id == c->id) {
			memcpy(c, &stk_controls[i],
				sizeof(struct v4l2_queryctrl));
			return 0;
		}
	}
	return -EINVAL;
}

static int stk_vidioc_g_ctrl(struct file *filp,
		void *priv, struct v4l2_control *c)
{
	struct stk_camera *dev = priv;
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->value = dev->vsettings.brightness;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int stk_vidioc_s_ctrl(struct file *filp,
		void *priv, struct v4l2_control *c)
{
	struct stk_camera *dev = priv;
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		dev->vsettings.brightness = c->value;
		return stk_sensor_set_brightness(dev, c->value >> 8);
	default:
		return -EINVAL;
	}
	return 0;
}


static int stk_vidioc_enum_fmt_cap(struct file *filp,
		void *priv, struct v4l2_fmtdesc *fmtd)
{
	fmtd->flags = 0;

	switch (fmtd->index) {
	case 0:
		fmtd->pixelformat = V4L2_PIX_FMT_RGB565;
		strcpy(fmtd->description, "r5g6b5");
		break;
	case 1:
		fmtd->pixelformat = V4L2_PIX_FMT_RGB565X;
		strcpy(fmtd->description, "r5g6b5BE");
		break;
	case 2:
		fmtd->pixelformat = V4L2_PIX_FMT_UYVY;
		strcpy(fmtd->description, "yuv4:2:2");
		break;
	case 3:
		fmtd->pixelformat = V4L2_PIX_FMT_SBGGR8;
		strcpy(fmtd->description, "Raw bayer");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct stk_size {
	unsigned w;
	unsigned h;
	enum stk_mode m;
} stk_sizes[] = {
	{ .w = 1280, .h = 1024, .m = MODE_SXGA, },
	{ .w = 640,  .h = 480,  .m = MODE_VGA,  },
	{ .w = 352,  .h = 288,  .m = MODE_CIF,  },
	{ .w = 320,  .h = 240,  .m = MODE_QVGA, },
	{ .w = 176,  .h = 144,  .m = MODE_QCIF, },
};

static int stk_vidioc_g_fmt_cap(struct file *filp,
		void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix_format = &f->fmt.pix;
	struct stk_camera *dev = priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(stk_sizes)
			&& stk_sizes[i].m != dev->vsettings.mode;
		i++);
	if (i == ARRAY_SIZE(stk_sizes)) {
		STK_ERROR("ERROR: mode invalid\n");
		return -EINVAL;
	}
	pix_format->width = stk_sizes[i].w;
	pix_format->height = stk_sizes[i].h;
	pix_format->field = V4L2_FIELD_NONE;
	pix_format->colorspace = V4L2_COLORSPACE_SRGB;
	pix_format->priv = 0;
	pix_format->pixelformat = dev->vsettings.palette;
	if (dev->vsettings.palette == V4L2_PIX_FMT_SBGGR8)
		pix_format->bytesperline = pix_format->width;
	else
		pix_format->bytesperline = 2 * pix_format->width;
	pix_format->sizeimage = pix_format->bytesperline
				* pix_format->height;
	return 0;
}

static int stk_vidioc_try_fmt_cap(struct file *filp,
		void *priv, struct v4l2_format *fmtd)
{
	int i;
	switch (fmtd->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_SBGGR8:
		break;
	default:
		return -EINVAL;
	}
	for (i = 1; i < ARRAY_SIZE(stk_sizes); i++) {
		if (fmtd->fmt.pix.width > stk_sizes[i].w)
			break;
	}
	if (i == ARRAY_SIZE(stk_sizes)
		|| (abs(fmtd->fmt.pix.width - stk_sizes[i-1].w)
			< abs(fmtd->fmt.pix.width - stk_sizes[i].w))) {
		fmtd->fmt.pix.height = stk_sizes[i-1].h;
		fmtd->fmt.pix.width = stk_sizes[i-1].w;
		fmtd->fmt.pix.priv = i - 1;
	} else {
		fmtd->fmt.pix.height = stk_sizes[i].h;
		fmtd->fmt.pix.width = stk_sizes[i].w;
		fmtd->fmt.pix.priv = i;
	}

	fmtd->fmt.pix.field = V4L2_FIELD_NONE;
	fmtd->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	if (fmtd->fmt.pix.pixelformat == V4L2_PIX_FMT_SBGGR8)
		fmtd->fmt.pix.bytesperline = fmtd->fmt.pix.width;
	else
		fmtd->fmt.pix.bytesperline = 2 * fmtd->fmt.pix.width;
	fmtd->fmt.pix.sizeimage = fmtd->fmt.pix.bytesperline
		* fmtd->fmt.pix.height;
	return 0;
}

static int stk_vidioc_s_fmt_cap(struct file *filp,
		void *priv, struct v4l2_format *fmtd)
{
	int ret;
	struct stk_camera *dev = priv;

	if (dev == NULL)
		return -ENODEV;
	if (!is_present(dev))
		return -ENODEV;
	if (is_streaming(dev))
		return -EBUSY;
	if (dev->owner && dev->owner != filp)
		return -EBUSY;
	dev->owner = filp;
	ret = stk_vidioc_try_fmt_cap(filp, priv, fmtd);
	if (ret)
		return ret;

	dev->vsettings.palette = fmtd->fmt.pix.pixelformat;
	stk_free_buffers(dev);
	dev->frame_size = fmtd->fmt.pix.sizeimage;
	dev->vsettings.mode = stk_sizes[fmtd->fmt.pix.priv].m;

	stk_initialise(dev);
	/* This registers controls some timings, not sure of what. */
	stk_camera_write_reg(dev, 0x001b, 0x0e);
	if (dev->vsettings.mode == MODE_SXGA)
		stk_camera_write_reg(dev, 0x001c, 0x0e);
	else
		stk_camera_write_reg(dev, 0x001c, 0x46);
	/*
	 * Registers 0x0115 0x0114 are the size of each line (bytes),
	 * regs 0x0117 0x0116 are the heigth of the image.
	 */
	stk_camera_write_reg(dev, 0x0115,
		(fmtd->fmt.pix.bytesperline >> 8) & 0xff);
	stk_camera_write_reg(dev, 0x0114,
		fmtd->fmt.pix.bytesperline & 0xff);
	stk_camera_write_reg(dev, 0x0117,
		(fmtd->fmt.pix.height >> 8) & 0xff);
	stk_camera_write_reg(dev, 0x0116,
		fmtd->fmt.pix.height & 0xff);
	return stk_sensor_configure(dev);
}

static int stk_vidioc_reqbufs(struct file *filp,
		void *priv, struct v4l2_requestbuffers *rb)
{
	struct stk_camera *dev = priv;

	if (dev == NULL)
		return -ENODEV;
	if (rb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (rb->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;
	if (is_streaming(dev)
		|| (dev->owner && dev->owner != filp))
		return -EBUSY;
	dev->owner = filp;

	/*FIXME If they ask for zero, we must stop streaming and free */
	if (rb->count < 3)
		rb->count = 3;
	/* Arbitrary limit */
	else if (rb->count > 5)
		rb->count = 5;

	stk_allocate_buffers(dev, rb->count);
	rb->count = dev->n_sbufs;
	return 0;
}

static int stk_vidioc_querybuf(struct file *filp,
		void *priv, struct v4l2_buffer *buf)
{
	int index;
	struct stk_camera *dev = priv;
	struct stk_sio_buffer *sbuf;

	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	index = buf->index;

	if (index < 0 || index >= dev->n_sbufs)
		return -EINVAL;
	sbuf = dev->sio_bufs + buf->index;
	*buf = sbuf->v4lbuf;
	return 0;
}

static int stk_vidioc_qbuf(struct file *filp,
		void *priv, struct v4l2_buffer *buf)
{
	struct stk_camera *dev = priv;
	struct stk_sio_buffer *sbuf;
	unsigned long flags;
	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (buf->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	if (buf->index < 0 || buf->index >= dev->n_sbufs)
		return -EINVAL;
	sbuf = dev->sio_bufs + buf->index;
	if (sbuf->v4lbuf.flags & V4L2_BUF_FLAG_QUEUED)
		return 0;
	sbuf->v4lbuf.flags |= V4L2_BUF_FLAG_QUEUED;
	sbuf->v4lbuf.flags &= ~V4L2_BUF_FLAG_DONE;
	spin_lock_irqsave(&dev->spinlock, flags);
	list_add_tail(&sbuf->list, &dev->sio_avail);
	*buf = sbuf->v4lbuf;
	spin_unlock_irqrestore(&dev->spinlock, flags);
	return 0;
}

static int stk_vidioc_dqbuf(struct file *filp,
		void *priv, struct v4l2_buffer *buf)
{
	struct stk_camera *dev = priv;
	struct stk_sio_buffer *sbuf;
	unsigned long flags;
	int ret;

	if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE
		|| !is_streaming(dev))
		return -EINVAL;

	if (filp->f_flags & O_NONBLOCK && list_empty(&dev->sio_full))
		return -EWOULDBLOCK;
	ret = wait_event_interruptible(dev->wait_frame,
		!list_empty(&dev->sio_full) || !is_present(dev));
	if (ret)
		return ret;
	if (!is_present(dev))
		return -EIO;

	spin_lock_irqsave(&dev->spinlock, flags);
	sbuf = list_first_entry(&dev->sio_full, struct stk_sio_buffer, list);
	list_del_init(&sbuf->list);
	spin_unlock_irqrestore(&dev->spinlock, flags);
	sbuf->v4lbuf.flags &= ~V4L2_BUF_FLAG_QUEUED;
	sbuf->v4lbuf.flags |= V4L2_BUF_FLAG_DONE;
	sbuf->v4lbuf.sequence = ++dev->sequence;
	do_gettimeofday(&sbuf->v4lbuf.timestamp);

	*buf = sbuf->v4lbuf;
	return 0;
}

static int stk_vidioc_streamon(struct file *filp,
		void *priv, enum v4l2_buf_type type)
{
	struct stk_camera *dev = priv;
	if (is_streaming(dev))
		return 0;
	if (dev->sio_bufs == NULL)
		return -EINVAL;
	dev->sequence = 0;
	return stk_start_stream(dev);
}

static int stk_vidioc_streamoff(struct file *filp,
		void *priv, enum v4l2_buf_type type)
{
	struct stk_camera *dev = priv;
	unsigned long flags;
	int i;
	stk_stop_stream(dev);
	spin_lock_irqsave(&dev->spinlock, flags);
	INIT_LIST_HEAD(&dev->sio_avail);
	INIT_LIST_HEAD(&dev->sio_full);
	for (i = 0; i < dev->n_sbufs; i++) {
		INIT_LIST_HEAD(&dev->sio_bufs[i].list);
		dev->sio_bufs[i].v4lbuf.flags = 0;
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);
	return 0;
}


static int stk_vidioc_g_parm(struct file *filp,
		void *priv, struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp->parm.capture.capability = 0;
	sp->parm.capture.capturemode = 0;
	/*FIXME This is not correct */
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;
	sp->parm.capture.readbuffers = 2;
	sp->parm.capture.extendedmode = 0;
	return 0;
}

static struct file_operations v4l_stk_fops = {
	.owner = THIS_MODULE,
	.open = v4l_stk_open,
	.release = v4l_stk_release,
	.read = v4l_stk_read,
	.poll = v4l_stk_poll,
	.mmap = v4l_stk_mmap,
	.ioctl = video_ioctl2,
	.llseek = no_llseek
};

static void stk_v4l_dev_release(struct video_device *vd)
{
}

static struct video_device stk_v4l_data = {
	.name = "stkwebcam",
	.type = VFL_TYPE_GRABBER,
	.type2 = VID_TYPE_CAPTURE,
	.minor = -1,
	.tvnorms = V4L2_STD_UNKNOWN,
	.current_norm = V4L2_STD_UNKNOWN,
	.fops = &v4l_stk_fops,
	.release = stk_v4l_dev_release,

	.vidioc_querycap = stk_vidioc_querycap,
	.vidioc_enum_fmt_cap = stk_vidioc_enum_fmt_cap,
	.vidioc_try_fmt_cap = stk_vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap = stk_vidioc_s_fmt_cap,
	.vidioc_g_fmt_cap = stk_vidioc_g_fmt_cap,
	.vidioc_enum_input = stk_vidioc_enum_input,
	.vidioc_s_input = stk_vidioc_s_input,
	.vidioc_g_input = stk_vidioc_g_input,
	.vidioc_s_std = stk_vidioc_s_std,
	.vidioc_reqbufs = stk_vidioc_reqbufs,
	.vidioc_querybuf = stk_vidioc_querybuf,
	.vidioc_qbuf = stk_vidioc_qbuf,
	.vidioc_dqbuf = stk_vidioc_dqbuf,
	.vidioc_streamon = stk_vidioc_streamon,
	.vidioc_streamoff = stk_vidioc_streamoff,
	.vidioc_queryctrl = stk_vidioc_queryctrl,
	.vidioc_g_ctrl = stk_vidioc_g_ctrl,
	.vidioc_s_ctrl = stk_vidioc_s_ctrl,
	.vidioc_g_parm = stk_vidioc_g_parm,
};


static int stk_register_video_device(struct stk_camera *dev)
{
	int err;

	dev->vdev = stk_v4l_data;
	dev->vdev.debug = debug;
	dev->vdev.dev = &dev->interface->dev;
	dev->vdev.priv = dev;
	err = video_register_device(&dev->vdev, VFL_TYPE_GRABBER, -1);
	if (err)
		STK_ERROR("v4l registration failed\n");
	else
		STK_INFO("Syntek USB2.0 Camera is now controlling video device"
			" /dev/video%d\n", dev->vdev.minor);
	return err;
}


/* USB Stuff */

static int stk_camera_probe(struct usb_interface *interface,
		const struct usb_device_id *id)
{
	int i;
	int err;

	struct stk_camera *dev = NULL;
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;

	dev = kzalloc(sizeof(struct stk_camera), GFP_KERNEL);
	if (dev == NULL) {
		STK_ERROR("Out of memory !\n");
		return -ENOMEM;
	}

	kref_init(&dev->kref);
	spin_lock_init(&dev->spinlock);
	init_waitqueue_head(&dev->wait_frame);

	dev->udev = udev;
	dev->interface = interface;
	usb_get_intf(interface);

	dev->vsettings.vflip = vflip;
	dev->vsettings.hflip = hflip;
	dev->n_sbufs = 0;
	set_present(dev);

	/* Set up the endpoint information
	 * use only the first isoc-in endpoint
	 * for the current alternate setting */
	iface_desc = interface->cur_altsetting;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->isoc_ep
			&& ((endpoint->bEndpointAddress
				& USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
			&& ((endpoint->bmAttributes
				& USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC)) {
			/* we found an isoc in endpoint */
			dev->isoc_ep = (endpoint->bEndpointAddress & 0xF);
			break;
		}
	}
	if (!dev->isoc_ep) {
		STK_ERROR("Could not find isoc-in endpoint");
		kref_put(&dev->kref, stk_camera_cleanup);
		return -ENODEV;
	}
	dev->vsettings.brightness = 0x7fff;
	dev->vsettings.palette = V4L2_PIX_FMT_RGB565;
	dev->vsettings.mode = MODE_VGA;
	dev->frame_size = 640*480*2;

	INIT_LIST_HEAD(&dev->sio_avail);
	INIT_LIST_HEAD(&dev->sio_full);

	usb_set_intfdata(interface, dev);

	err = stk_register_video_device(dev);
	if (err) {
		kref_put(&dev->kref, stk_camera_cleanup);
		return err;
	}

	stk_create_sysfs_files(&dev->vdev);

	return 0;
}

static void stk_camera_disconnect(struct usb_interface *interface)
{
	struct stk_camera *dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);
	unset_present(dev);

	wake_up_interruptible(&dev->wait_frame);
	stk_remove_sysfs_files(&dev->vdev);

	kref_put(&dev->kref, stk_camera_cleanup);
}

static struct usb_driver stk_camera_driver = {
	.name = "stkwebcam",
	.probe = stk_camera_probe,
	.disconnect = stk_camera_disconnect,
	.id_table = stkwebcam_table,
};


static int __init stk_camera_init(void)
{
	int result;

	result = usb_register(&stk_camera_driver);
	if (result)
		STK_ERROR("usb_register failed ! Error number %d\n", result);


	return result;
}

static void __exit stk_camera_exit(void)
{
	usb_deregister(&stk_camera_driver);
}

module_init(stk_camera_init);
module_exit(stk_camera_exit);


