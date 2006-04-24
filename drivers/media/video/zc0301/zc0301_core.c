/***************************************************************************
 * Video4Linux2 driver for ZC0301 Image Processor and Control Chip         *
 *                                                                         *
 * Copyright (C) 2006 by Luca Risolia <luca.risolia@studio.unibo.it>       *
 *                                                                         *
 * Informations about the chip internals needed to enable the I2C protocol *
 * have been taken from the documentation of the ZC030x Video4Linux1       *
 * driver written by Andrew Birkett <andy@nobugs.org>                      *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/compiler.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/page-flags.h>
#include <linux/byteorder/generic.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include "zc0301.h"

/*****************************************************************************/

#define ZC0301_MODULE_NAME    "V4L2 driver for ZC0301 "                       \
			      "Image Processor and Control Chip"
#define ZC0301_MODULE_AUTHOR  "(C) 2006 Luca Risolia"
#define ZC0301_AUTHOR_EMAIL   "<luca.risolia@studio.unibo.it>"
#define ZC0301_MODULE_LICENSE "GPL"
#define ZC0301_MODULE_VERSION "1:1.04"
#define ZC0301_MODULE_VERSION_CODE  KERNEL_VERSION(1, 0, 4)

/*****************************************************************************/

MODULE_DEVICE_TABLE(usb, zc0301_id_table);

MODULE_AUTHOR(ZC0301_MODULE_AUTHOR " " ZC0301_AUTHOR_EMAIL);
MODULE_DESCRIPTION(ZC0301_MODULE_NAME);
MODULE_VERSION(ZC0301_MODULE_VERSION);
MODULE_LICENSE(ZC0301_MODULE_LICENSE);

static short video_nr[] = {[0 ... ZC0301_MAX_DEVICES-1] = -1};
module_param_array(video_nr, short, NULL, 0444);
MODULE_PARM_DESC(video_nr,
		 "\n<-1|n[,...]> Specify V4L2 minor mode number."
		 "\n -1 = use next available (default)"
		 "\n  n = use minor number n (integer >= 0)"
		 "\nYou can specify up to "
		 __MODULE_STRING(ZC0301_MAX_DEVICES) " cameras this way."
		 "\nFor example:"
		 "\nvideo_nr=-1,2,-1 would assign minor number 2 to"
		 "\nthe second registered camera and use auto for the first"
		 "\none and for every other camera."
		 "\n");

static short force_munmap[] = {[0 ... ZC0301_MAX_DEVICES-1] =
			       ZC0301_FORCE_MUNMAP};
module_param_array(force_munmap, bool, NULL, 0444);
MODULE_PARM_DESC(force_munmap,
		 "\n<0|1[,...]> Force the application to unmap previously"
		 "\nmapped buffer memory before calling any VIDIOC_S_CROP or"
		 "\nVIDIOC_S_FMT ioctl's. Not all the applications support"
		 "\nthis feature. This parameter is specific for each"
		 "\ndetected camera."
		 "\n 0 = do not force memory unmapping"
		 "\n 1 = force memory unmapping (save memory)"
		 "\nDefault value is "__MODULE_STRING(SN9C102_FORCE_MUNMAP)"."
		 "\n");

static unsigned int frame_timeout[] = {[0 ... ZC0301_MAX_DEVICES-1] =
				       ZC0301_FRAME_TIMEOUT};
module_param_array(frame_timeout, uint, NULL, 0644);
MODULE_PARM_DESC(frame_timeout,
		 "\n<n[,...]> Timeout for a video frame in seconds."
		 "\nThis parameter is specific for each detected camera."
		 "\nDefault value is "__MODULE_STRING(ZC0301_FRAME_TIMEOUT)"."
		 "\n");

#ifdef ZC0301_DEBUG
static unsigned short debug = ZC0301_DEBUG_LEVEL;
module_param(debug, ushort, 0644);
MODULE_PARM_DESC(debug,
		 "\n<n> Debugging information level, from 0 to 3:"
		 "\n0 = none (use carefully)"
		 "\n1 = critical errors"
		 "\n2 = significant informations"
		 "\n3 = more verbose messages"
		 "\nLevel 3 is useful for testing only, when only "
		 "one device is used."
		 "\nDefault value is "__MODULE_STRING(ZC0301_DEBUG_LEVEL)"."
		 "\n");
#endif

/*****************************************************************************/

static u32
zc0301_request_buffers(struct zc0301_device* cam, u32 count,
		       enum zc0301_io_method io)
{
	struct v4l2_pix_format* p = &(cam->sensor.pix_format);
	struct v4l2_rect* r = &(cam->sensor.cropcap.bounds);
	const size_t imagesize = cam->module_param.force_munmap ||
				 io == IO_READ ?
				 (p->width * p->height * p->priv) / 8 :
				 (r->width * r->height * p->priv) / 8;
	void* buff = NULL;
	u32 i;

	if (count > ZC0301_MAX_FRAMES)
		count = ZC0301_MAX_FRAMES;

	cam->nbuffers = count;
	while (cam->nbuffers > 0) {
		if ((buff = vmalloc_32(cam->nbuffers * PAGE_ALIGN(imagesize))))
			break;
		cam->nbuffers--;
	}

	for (i = 0; i < cam->nbuffers; i++) {
		cam->frame[i].bufmem = buff + i*PAGE_ALIGN(imagesize);
		cam->frame[i].buf.index = i;
		cam->frame[i].buf.m.offset = i*PAGE_ALIGN(imagesize);
		cam->frame[i].buf.length = imagesize;
		cam->frame[i].buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam->frame[i].buf.sequence = 0;
		cam->frame[i].buf.field = V4L2_FIELD_NONE;
		cam->frame[i].buf.memory = V4L2_MEMORY_MMAP;
		cam->frame[i].buf.flags = 0;
	}

	return cam->nbuffers;
}


static void zc0301_release_buffers(struct zc0301_device* cam)
{
	if (cam->nbuffers) {
		vfree(cam->frame[0].bufmem);
		cam->nbuffers = 0;
	}
	cam->frame_current = NULL;
}


static void zc0301_empty_framequeues(struct zc0301_device* cam)
{
	u32 i;

	INIT_LIST_HEAD(&cam->inqueue);
	INIT_LIST_HEAD(&cam->outqueue);

	for (i = 0; i < ZC0301_MAX_FRAMES; i++) {
		cam->frame[i].state = F_UNUSED;
		cam->frame[i].buf.bytesused = 0;
	}
}


static void zc0301_requeue_outqueue(struct zc0301_device* cam)
{
	struct zc0301_frame_t *i;

	list_for_each_entry(i, &cam->outqueue, frame) {
		i->state = F_QUEUED;
		list_add(&i->frame, &cam->inqueue);
	}

	INIT_LIST_HEAD(&cam->outqueue);
}


static void zc0301_queue_unusedframes(struct zc0301_device* cam)
{
	unsigned long lock_flags;
	u32 i;

	for (i = 0; i < cam->nbuffers; i++)
		if (cam->frame[i].state == F_UNUSED) {
			cam->frame[i].state = F_QUEUED;
			spin_lock_irqsave(&cam->queue_lock, lock_flags);
			list_add_tail(&cam->frame[i].frame, &cam->inqueue);
			spin_unlock_irqrestore(&cam->queue_lock, lock_flags);
		}
}

/*****************************************************************************/

int zc0301_write_reg(struct zc0301_device* cam, u16 index, u16 value)
{
	struct usb_device* udev = cam->usbdev;
	int res;

	res = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0xa0, 0x40,
			      value, index, NULL, 0, ZC0301_CTRL_TIMEOUT);
	if (res < 0) {
		DBG(3, "Failed to write a register (index 0x%04X, "
		       "value 0x%02X, error %d)",index, value, res);
		return -1;
	}

	return 0;
}


int zc0301_read_reg(struct zc0301_device* cam, u16 index)
{
	struct usb_device* udev = cam->usbdev;
	u8* buff = cam->control_buffer;
	int res;

	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0xa1, 0xc0,
			      0x0001, index, buff, 1, ZC0301_CTRL_TIMEOUT);
	if (res < 0)
		DBG(3, "Failed to read a register (index 0x%04X, error %d)",
		    index, res);

	PDBGG("Read: index 0x%04X, value: 0x%04X", index, (int)(*buff));

	return (res >= 0) ? (int)(*buff) : -1;
}


int zc0301_i2c_read(struct zc0301_device* cam, u16 address, u8 length)
{
	int err = 0, res, r0, r1;

	err += zc0301_write_reg(cam, 0x0092, address);
	err += zc0301_write_reg(cam, 0x0090, 0x02);

	msleep(1);

	res = zc0301_read_reg(cam, 0x0091);
	if (res < 0)
		err += res;
	r0 = zc0301_read_reg(cam, 0x0095);
	if (r0 < 0)
		err += r0;
	r1 = zc0301_read_reg(cam, 0x0096);
	if (r1 < 0)
		err += r1;

	res = (length <= 1) ? r0 : r0 | (r1 << 8);

	if (err)
		DBG(3, "I2C read failed at address 0x%04X, value: 0x%04X",
		    address, res);


	PDBGG("I2C read: address 0x%04X, value: 0x%04X", address, res);

	return err ? -1 : res;
}


int zc0301_i2c_write(struct zc0301_device* cam, u16 address, u16 value)
{
	int err = 0, res;

	err += zc0301_write_reg(cam, 0x0092, address);
	err += zc0301_write_reg(cam, 0x0093, value & 0xff);
	err += zc0301_write_reg(cam, 0x0094, value >> 8);
	err += zc0301_write_reg(cam, 0x0090, 0x01);

	msleep(1);

	res = zc0301_read_reg(cam, 0x0091);
	if (res < 0)
		err += res;

	if (err)
		DBG(3, "I2C write failed at address 0x%04X, value: 0x%04X",
		    address, value);

	PDBGG("I2C write: address 0x%04X, value: 0x%04X", address, value);

	return err ? -1 : 0;
}

/*****************************************************************************/

static void zc0301_urb_complete(struct urb *urb, struct pt_regs* regs)
{
	struct zc0301_device* cam = urb->context;
	struct zc0301_frame_t** f;
	size_t imagesize;
	u8 i;
	int err = 0;

	if (urb->status == -ENOENT)
		return;

	f = &cam->frame_current;

	if (cam->stream == STREAM_INTERRUPT) {
		cam->stream = STREAM_OFF;
		if ((*f))
			(*f)->state = F_QUEUED;
		DBG(3, "Stream interrupted");
		wake_up(&cam->wait_stream);
	}

	if (cam->state & DEV_DISCONNECTED)
		return;

	if (cam->state & DEV_MISCONFIGURED) {
		wake_up_interruptible(&cam->wait_frame);
		return;
	}

	if (cam->stream == STREAM_OFF || list_empty(&cam->inqueue))
		goto resubmit_urb;

	if (!(*f))
		(*f) = list_entry(cam->inqueue.next, struct zc0301_frame_t,
				  frame);

	imagesize = (cam->sensor.pix_format.width *
		     cam->sensor.pix_format.height *
		     cam->sensor.pix_format.priv) / 8;

	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned int len, status;
		void *pos;
		u16* soi;
		u8 sof;

		len = urb->iso_frame_desc[i].actual_length;
		status = urb->iso_frame_desc[i].status;
		pos = urb->iso_frame_desc[i].offset + urb->transfer_buffer;

		if (status) {
			DBG(3, "Error in isochronous frame");
			(*f)->state = F_ERROR;
			continue;
		}

		sof = (*(soi = pos) == 0xd8ff);

		PDBGG("Isochrnous frame: length %u, #%u i,", len, i);

		if ((*f)->state == F_QUEUED || (*f)->state == F_ERROR)
start_of_frame:
			if (sof) {
				(*f)->state = F_GRABBING;
				(*f)->buf.bytesused = 0;
				do_gettimeofday(&(*f)->buf.timestamp);
				DBG(3, "SOF detected: new video frame");
			}

		if ((*f)->state == F_GRABBING) {
			if (sof && (*f)->buf.bytesused)
					goto end_of_frame;

			if ((*f)->buf.bytesused + len > imagesize) {
				DBG(3, "Video frame size exceeded");
				(*f)->state = F_ERROR;
				continue;
			}

			memcpy((*f)->bufmem+(*f)->buf.bytesused, pos, len);
			(*f)->buf.bytesused += len;

			if ((*f)->buf.bytesused == imagesize) {
				u32 b;
end_of_frame:
				b = (*f)->buf.bytesused;
				(*f)->state = F_DONE;
				(*f)->buf.sequence= ++cam->frame_count;
				spin_lock(&cam->queue_lock);
				list_move_tail(&(*f)->frame, &cam->outqueue);
				if (!list_empty(&cam->inqueue))
					(*f) = list_entry(cam->inqueue.next,
						       struct zc0301_frame_t,
							  frame);
				else
					(*f) = NULL;
				spin_unlock(&cam->queue_lock);
				DBG(3, "Video frame captured: : %lu bytes",
				       (unsigned long)(b));

				if (!(*f))
					goto resubmit_urb;

				if (sof)
					goto start_of_frame;
			}
		}
	}

resubmit_urb:
	urb->dev = cam->usbdev;
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0 && err != -EPERM) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "usb_submit_urb() failed");
	}

	wake_up_interruptible(&cam->wait_frame);
}


static int zc0301_start_transfer(struct zc0301_device* cam)
{
	struct usb_device *udev = cam->usbdev;
	struct urb* urb;
	const unsigned int wMaxPacketSize[] = {0, 128, 192, 256, 384,
					       512, 768, 1023};
	const unsigned int psz = wMaxPacketSize[ZC0301_ALTERNATE_SETTING];
	s8 i, j;
	int err = 0;

	for (i = 0; i < ZC0301_URBS; i++) {
		cam->transfer_buffer[i] = kzalloc(ZC0301_ISO_PACKETS * psz,
						  GFP_KERNEL);
		if (!cam->transfer_buffer[i]) {
			err = -ENOMEM;
			DBG(1, "Not enough memory");
			goto free_buffers;
		}
	}

	for (i = 0; i < ZC0301_URBS; i++) {
		urb = usb_alloc_urb(ZC0301_ISO_PACKETS, GFP_KERNEL);
		cam->urb[i] = urb;
		if (!urb) {
			err = -ENOMEM;
			DBG(1, "usb_alloc_urb() failed");
			goto free_urbs;
		}
		urb->dev = udev;
		urb->context = cam;
		urb->pipe = usb_rcvisocpipe(udev, 1);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->number_of_packets = ZC0301_ISO_PACKETS;
		urb->complete = zc0301_urb_complete;
		urb->transfer_buffer = cam->transfer_buffer[i];
		urb->transfer_buffer_length = psz * ZC0301_ISO_PACKETS;
		urb->interval = 1;
		for (j = 0; j < ZC0301_ISO_PACKETS; j++) {
			urb->iso_frame_desc[j].offset = psz * j;
			urb->iso_frame_desc[j].length = psz;
		}
	}

	err = usb_set_interface(udev, 0, ZC0301_ALTERNATE_SETTING);
	if (err) {
		DBG(1, "usb_set_interface() failed");
		goto free_urbs;
	}

	cam->frame_current = NULL;

	for (i = 0; i < ZC0301_URBS; i++) {
		err = usb_submit_urb(cam->urb[i], GFP_KERNEL);
		if (err) {
			for (j = i-1; j >= 0; j--)
				usb_kill_urb(cam->urb[j]);
			DBG(1, "usb_submit_urb() failed, error %d", err);
			goto free_urbs;
		}
	}

	return 0;

free_urbs:
	for (i = 0; (i < ZC0301_URBS) &&  cam->urb[i]; i++)
		usb_free_urb(cam->urb[i]);

free_buffers:
	for (i = 0; (i < ZC0301_URBS) && cam->transfer_buffer[i]; i++)
		kfree(cam->transfer_buffer[i]);

	return err;
}


static int zc0301_stop_transfer(struct zc0301_device* cam)
{
	struct usb_device *udev = cam->usbdev;
	s8 i;
	int err = 0;

	if (cam->state & DEV_DISCONNECTED)
		return 0;

	for (i = ZC0301_URBS-1; i >= 0; i--) {
		usb_kill_urb(cam->urb[i]);
		usb_free_urb(cam->urb[i]);
		kfree(cam->transfer_buffer[i]);
	}

	err = usb_set_interface(udev, 0, 0); /* 0 Mb/s */
	if (err)
		DBG(3, "usb_set_interface() failed");

	return err;
}


static int zc0301_stream_interrupt(struct zc0301_device* cam)
{
	long timeout;

	cam->stream = STREAM_INTERRUPT;
	timeout = wait_event_timeout(cam->wait_stream,
				     (cam->stream == STREAM_OFF) ||
				     (cam->state & DEV_DISCONNECTED),
				     ZC0301_URB_TIMEOUT);
	if (cam->state & DEV_DISCONNECTED)
		return -ENODEV;
	else if (cam->stream != STREAM_OFF) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "URB timeout reached. The camera is misconfigured. To "
		       "use it, close and open /dev/video%d again.",
		    cam->v4ldev->minor);
		return -EIO;
	}

	return 0;
}

/*****************************************************************************/

static int
zc0301_set_compression(struct zc0301_device* cam,
		       struct v4l2_jpegcompression* compression)
{
	int r, err = 0;

	if ((r = zc0301_read_reg(cam, 0x0008)) < 0)
		err += r;
	err += zc0301_write_reg(cam, 0x0008, r | 0x11 | compression->quality);

	return err ? -EIO : 0;
}


static int zc0301_init(struct zc0301_device* cam)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_control ctrl;
	struct v4l2_queryctrl *qctrl;
	struct v4l2_rect* rect;
	u8 i = 0;
	int err = 0;

	if (!(cam->state & DEV_INITIALIZED)) {
		init_waitqueue_head(&cam->open);
		qctrl = s->qctrl;
		rect = &(s->cropcap.defrect);
		cam->compression.quality = ZC0301_COMPRESSION_QUALITY;
	} else { /* use current values */
		qctrl = s->_qctrl;
		rect = &(s->_rect);
	}

	if (s->init) {
		err = s->init(cam);
		if (err) {
			DBG(3, "Sensor initialization failed");
			return err;
		}
	}

	if ((err = zc0301_set_compression(cam, &cam->compression))) {
		DBG(3, "set_compression() failed");
		return err;
	}

	if (s->set_crop)
		if ((err = s->set_crop(cam, rect))) {
			DBG(3, "set_crop() failed");
			return err;
		}

	if (s->set_ctrl) {
		for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
			if (s->qctrl[i].id != 0 &&
			    !(s->qctrl[i].flags & V4L2_CTRL_FLAG_DISABLED)) {
				ctrl.id = s->qctrl[i].id;
				ctrl.value = qctrl[i].default_value;
				err = s->set_ctrl(cam, &ctrl);
				if (err) {
					DBG(3, "Set %s control failed",
					    s->qctrl[i].name);
					return err;
				}
				DBG(3, "Image sensor supports '%s' control",
				    s->qctrl[i].name);
			}
	}

	if (!(cam->state & DEV_INITIALIZED)) {
		mutex_init(&cam->fileop_mutex);
		spin_lock_init(&cam->queue_lock);
		init_waitqueue_head(&cam->wait_frame);
		init_waitqueue_head(&cam->wait_stream);
		cam->nreadbuffers = 2;
		memcpy(s->_qctrl, s->qctrl, sizeof(s->qctrl));
		memcpy(&(s->_rect), &(s->cropcap.defrect),
		       sizeof(struct v4l2_rect));
		cam->state |= DEV_INITIALIZED;
	}

	DBG(2, "Initialization succeeded");
	return 0;
}


static void zc0301_release_resources(struct zc0301_device* cam)
{
	DBG(2, "V4L2 device /dev/video%d deregistered", cam->v4ldev->minor);
	video_set_drvdata(cam->v4ldev, NULL);
	video_unregister_device(cam->v4ldev);
	kfree(cam->control_buffer);
}

/*****************************************************************************/

static int zc0301_open(struct inode* inode, struct file* filp)
{
	struct zc0301_device* cam;
	int err = 0;

	/*
	   This is the only safe way to prevent race conditions with
	   disconnect
	*/
	if (!down_read_trylock(&zc0301_disconnect))
		return -ERESTARTSYS;

	cam = video_get_drvdata(video_devdata(filp));

	if (mutex_lock_interruptible(&cam->dev_mutex)) {
		up_read(&zc0301_disconnect);
		return -ERESTARTSYS;
	}

	if (cam->users) {
		DBG(2, "Device /dev/video%d is busy...", cam->v4ldev->minor);
		if ((filp->f_flags & O_NONBLOCK) ||
		    (filp->f_flags & O_NDELAY)) {
			err = -EWOULDBLOCK;
			goto out;
		}
		mutex_unlock(&cam->dev_mutex);
		err = wait_event_interruptible_exclusive(cam->open,
						  cam->state & DEV_DISCONNECTED
							 || !cam->users);
		if (err) {
			up_read(&zc0301_disconnect);
			return err;
		}
		if (cam->state & DEV_DISCONNECTED) {
			up_read(&zc0301_disconnect);
			return -ENODEV;
		}
		mutex_lock(&cam->dev_mutex);
	}


	if (cam->state & DEV_MISCONFIGURED) {
		err = zc0301_init(cam);
		if (err) {
			DBG(1, "Initialization failed again. "
			       "I will retry on next open().");
			goto out;
		}
		cam->state &= ~DEV_MISCONFIGURED;
	}

	if ((err = zc0301_start_transfer(cam)))
		goto out;

	filp->private_data = cam;
	cam->users++;
	cam->io = IO_NONE;
	cam->stream = STREAM_OFF;
	cam->nbuffers = 0;
	cam->frame_count = 0;
	zc0301_empty_framequeues(cam);

	DBG(3, "Video device /dev/video%d is open", cam->v4ldev->minor);

out:
	mutex_unlock(&cam->dev_mutex);
	up_read(&zc0301_disconnect);
	return err;
}


static int zc0301_release(struct inode* inode, struct file* filp)
{
	struct zc0301_device* cam = video_get_drvdata(video_devdata(filp));

	mutex_lock(&cam->dev_mutex); /* prevent disconnect() to be called */

	zc0301_stop_transfer(cam);

	zc0301_release_buffers(cam);

	if (cam->state & DEV_DISCONNECTED) {
		zc0301_release_resources(cam);
		usb_put_dev(cam->usbdev);
		mutex_unlock(&cam->dev_mutex);
		kfree(cam);
		return 0;
	}

	cam->users--;
	wake_up_interruptible_nr(&cam->open, 1);

	DBG(3, "Video device /dev/video%d closed", cam->v4ldev->minor);

	mutex_unlock(&cam->dev_mutex);

	return 0;
}


static ssize_t
zc0301_read(struct file* filp, char __user * buf, size_t count, loff_t* f_pos)
{
	struct zc0301_device* cam = video_get_drvdata(video_devdata(filp));
	struct zc0301_frame_t* f, * i;
	unsigned long lock_flags;
	long timeout;
	int err = 0;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return -ERESTARTSYS;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		mutex_unlock(&cam->fileop_mutex);
		return -ENODEV;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		mutex_unlock(&cam->fileop_mutex);
		return -EIO;
	}

	if (cam->io == IO_MMAP) {
		DBG(3, "Close and open the device again to choose the read "
		       "method");
		mutex_unlock(&cam->fileop_mutex);
		return -EINVAL;
	}

	if (cam->io == IO_NONE) {
		if (!zc0301_request_buffers(cam, cam->nreadbuffers, IO_READ)) {
			DBG(1, "read() failed, not enough memory");
			mutex_unlock(&cam->fileop_mutex);
			return -ENOMEM;
		}
		cam->io = IO_READ;
		cam->stream = STREAM_ON;
	}

	if (list_empty(&cam->inqueue)) {
		if (!list_empty(&cam->outqueue))
			zc0301_empty_framequeues(cam);
		zc0301_queue_unusedframes(cam);
	}

	if (!count) {
		mutex_unlock(&cam->fileop_mutex);
		return 0;
	}

	if (list_empty(&cam->outqueue)) {
		if (filp->f_flags & O_NONBLOCK) {
			mutex_unlock(&cam->fileop_mutex);
			return -EAGAIN;
		}
		timeout = wait_event_interruptible_timeout
			  ( cam->wait_frame,
			    (!list_empty(&cam->outqueue)) ||
			    (cam->state & DEV_DISCONNECTED) ||
			    (cam->state & DEV_MISCONFIGURED),
			    cam->module_param.frame_timeout *
			    1000 * msecs_to_jiffies(1) );
		if (timeout < 0) {
			mutex_unlock(&cam->fileop_mutex);
			return timeout;
		}
		if (cam->state & DEV_DISCONNECTED) {
			mutex_unlock(&cam->fileop_mutex);
			return -ENODEV;
		}
		if (!timeout || (cam->state & DEV_MISCONFIGURED)) {
			mutex_unlock(&cam->fileop_mutex);
			return -EIO;
		}
	}

	f = list_entry(cam->outqueue.prev, struct zc0301_frame_t, frame);

	if (count > f->buf.bytesused)
		count = f->buf.bytesused;

	if (copy_to_user(buf, f->bufmem, count)) {
		err = -EFAULT;
		goto exit;
	}
	*f_pos += count;

exit:
	spin_lock_irqsave(&cam->queue_lock, lock_flags);
	list_for_each_entry(i, &cam->outqueue, frame)
		i->state = F_UNUSED;
	INIT_LIST_HEAD(&cam->outqueue);
	spin_unlock_irqrestore(&cam->queue_lock, lock_flags);

	zc0301_queue_unusedframes(cam);

	PDBGG("Frame #%lu, bytes read: %zu",
	      (unsigned long)f->buf.index, count);

	mutex_unlock(&cam->fileop_mutex);

	return err ? err : count;
}


static unsigned int zc0301_poll(struct file *filp, poll_table *wait)
{
	struct zc0301_device* cam = video_get_drvdata(video_devdata(filp));
	struct zc0301_frame_t* f;
	unsigned long lock_flags;
	unsigned int mask = 0;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return POLLERR;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		goto error;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		goto error;
	}

	if (cam->io == IO_NONE) {
		if (!zc0301_request_buffers(cam, cam->nreadbuffers, IO_READ)) {
			DBG(1, "poll() failed, not enough memory");
			goto error;
		}
		cam->io = IO_READ;
		cam->stream = STREAM_ON;
	}

	if (cam->io == IO_READ) {
		spin_lock_irqsave(&cam->queue_lock, lock_flags);
		list_for_each_entry(f, &cam->outqueue, frame)
			f->state = F_UNUSED;
		INIT_LIST_HEAD(&cam->outqueue);
		spin_unlock_irqrestore(&cam->queue_lock, lock_flags);
		zc0301_queue_unusedframes(cam);
	}

	poll_wait(filp, &cam->wait_frame, wait);

	if (!list_empty(&cam->outqueue))
		mask |= POLLIN | POLLRDNORM;

	mutex_unlock(&cam->fileop_mutex);

	return mask;

error:
	mutex_unlock(&cam->fileop_mutex);
	return POLLERR;
}


static void zc0301_vm_open(struct vm_area_struct* vma)
{
	struct zc0301_frame_t* f = vma->vm_private_data;
	f->vma_use_count++;
}


static void zc0301_vm_close(struct vm_area_struct* vma)
{
	/* NOTE: buffers are not freed here */
	struct zc0301_frame_t* f = vma->vm_private_data;
	f->vma_use_count--;
}


static struct vm_operations_struct zc0301_vm_ops = {
	.open = zc0301_vm_open,
	.close = zc0301_vm_close,
};


static int zc0301_mmap(struct file* filp, struct vm_area_struct *vma)
{
	struct zc0301_device* cam = video_get_drvdata(video_devdata(filp));
	unsigned long size = vma->vm_end - vma->vm_start,
		      start = vma->vm_start;
	void *pos;
	u32 i;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return -ERESTARTSYS;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		mutex_unlock(&cam->fileop_mutex);
		return -ENODEV;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		mutex_unlock(&cam->fileop_mutex);
		return -EIO;
	}

	if (cam->io != IO_MMAP || !(vma->vm_flags & VM_WRITE) ||
	    size != PAGE_ALIGN(cam->frame[0].buf.length)) {
		mutex_unlock(&cam->fileop_mutex);
		return -EINVAL;
	}

	for (i = 0; i < cam->nbuffers; i++) {
		if ((cam->frame[i].buf.m.offset>>PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}
	if (i == cam->nbuffers) {
		mutex_unlock(&cam->fileop_mutex);
		return -EINVAL;
	}

	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;

	pos = cam->frame[i].bufmem;
	while (size > 0) { /* size is page-aligned */
		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			mutex_unlock(&cam->fileop_mutex);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &zc0301_vm_ops;
	vma->vm_private_data = &cam->frame[i];

	zc0301_vm_open(vma);

	mutex_unlock(&cam->fileop_mutex);

	return 0;
}

/*****************************************************************************/

static int
zc0301_vidioc_querycap(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_capability cap = {
		.driver = "zc0301",
		.version = ZC0301_MODULE_VERSION_CODE,
		.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING,
	};

	strlcpy(cap.card, cam->v4ldev->name, sizeof(cap.card));
	if (usb_make_path(cam->usbdev, cap.bus_info, sizeof(cap.bus_info)) < 0)
		strlcpy(cap.bus_info, cam->usbdev->dev.bus_id,
			sizeof(cap.bus_info));

	if (copy_to_user(arg, &cap, sizeof(cap)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_enuminput(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_input i;

	if (copy_from_user(&i, arg, sizeof(i)))
		return -EFAULT;

	if (i.index)
		return -EINVAL;

	memset(&i, 0, sizeof(i));
	strcpy(i.name, "Camera");
	i.type = V4L2_INPUT_TYPE_CAMERA;

	if (copy_to_user(arg, &i, sizeof(i)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_g_input(struct zc0301_device* cam, void __user * arg)
{
	int index = 0;

	if (copy_to_user(arg, &index, sizeof(index)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_s_input(struct zc0301_device* cam, void __user * arg)
{
	int index;

	if (copy_from_user(&index, arg, sizeof(index)))
		return -EFAULT;

	if (index != 0)
		return -EINVAL;

	return 0;
}


static int
zc0301_vidioc_query_ctrl(struct zc0301_device* cam, void __user * arg)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_queryctrl qc;
	u8 i;

	if (copy_from_user(&qc, arg, sizeof(qc)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
		if (qc.id && qc.id == s->qctrl[i].id) {
			memcpy(&qc, &(s->qctrl[i]), sizeof(qc));
			if (copy_to_user(arg, &qc, sizeof(qc)))
				return -EFAULT;
			return 0;
		}

	return -EINVAL;
}


static int
zc0301_vidioc_g_ctrl(struct zc0301_device* cam, void __user * arg)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_control ctrl;
	int err = 0;
	u8 i;

	if (!s->get_ctrl && !s->set_ctrl)
		return -EINVAL;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;

	if (!s->get_ctrl) {
		for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
			if (ctrl.id == s->qctrl[i].id) {
				ctrl.value = s->_qctrl[i].default_value;
				goto exit;
			}
		return -EINVAL;
	} else
		err = s->get_ctrl(cam, &ctrl);

exit:
	if (copy_to_user(arg, &ctrl, sizeof(ctrl)))
		return -EFAULT;

	return err;
}


static int
zc0301_vidioc_s_ctrl(struct zc0301_device* cam, void __user * arg)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_control ctrl;
	u8 i;
	int err = 0;

	if (!s->set_ctrl)
		return -EINVAL;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
		if (ctrl.id == s->qctrl[i].id) {
			if (s->qctrl[i].flags & V4L2_CTRL_FLAG_DISABLED)
				return -EINVAL;
			if (ctrl.value < s->qctrl[i].minimum ||
			    ctrl.value > s->qctrl[i].maximum)
				return -ERANGE;
			ctrl.value -= ctrl.value % s->qctrl[i].step;
			break;
		}

	if ((err = s->set_ctrl(cam, &ctrl)))
		return err;

	s->_qctrl[i].default_value = ctrl.value;

	return 0;
}


static int
zc0301_vidioc_cropcap(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_cropcap* cc = &(cam->sensor.cropcap);

	cc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cc->pixelaspect.numerator = 1;
	cc->pixelaspect.denominator = 1;

	if (copy_to_user(arg, cc, sizeof(*cc)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_g_crop(struct zc0301_device* cam, void __user * arg)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_crop crop = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};

	memcpy(&(crop.c), &(s->_rect), sizeof(struct v4l2_rect));

	if (copy_to_user(arg, &crop, sizeof(crop)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_s_crop(struct zc0301_device* cam, void __user * arg)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_crop crop;
	struct v4l2_rect* rect;
	struct v4l2_rect* bounds = &(s->cropcap.bounds);
	const enum zc0301_stream_state stream = cam->stream;
	const u32 nbuffers = cam->nbuffers;
	u32 i;
	int err = 0;

	if (copy_from_user(&crop, arg, sizeof(crop)))
		return -EFAULT;

	rect = &(crop.c);

	if (crop.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (cam->module_param.force_munmap)
		for (i = 0; i < cam->nbuffers; i++)
			if (cam->frame[i].vma_use_count) {
				DBG(3, "VIDIOC_S_CROP failed. "
				       "Unmap the buffers first.");
				return -EINVAL;
			}

	if (!s->set_crop) {
		memcpy(rect, &(s->_rect), sizeof(*rect));
		if (copy_to_user(arg, &crop, sizeof(crop)))
			return -EFAULT;
		return 0;
	}

	rect->left &= ~7L;
	rect->top &= ~7L;
	if (rect->width < 8)
		rect->width = 8;
	if (rect->height < 8)
		rect->height = 8;
	if (rect->width > bounds->width)
		rect->width = bounds->width;
	if (rect->height > bounds->height)
		rect->height = bounds->height;
	if (rect->left < bounds->left)
		rect->left = bounds->left;
	if (rect->top < bounds->top)
		rect->top = bounds->top;
	if (rect->left + rect->width > bounds->left + bounds->width)
		rect->left = bounds->left+bounds->width - rect->width;
	if (rect->top + rect->height > bounds->top + bounds->height)
		rect->top = bounds->top+bounds->height - rect->height;
	rect->width &= ~7L;
	rect->height &= ~7L;

	if (cam->stream == STREAM_ON)
		if ((err = zc0301_stream_interrupt(cam)))
			return err;

	if (copy_to_user(arg, &crop, sizeof(crop))) {
		cam->stream = stream;
		return -EFAULT;
	}

	if (cam->module_param.force_munmap || cam->io == IO_READ)
		zc0301_release_buffers(cam);

	if (s->set_crop)
		err += s->set_crop(cam, rect);

	if (err) { /* atomic, no rollback in ioctl() */
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_CROP failed because of hardware problems. To "
		       "use the camera, close and open /dev/video%d again.",
		    cam->v4ldev->minor);
		return -EIO;
	}

	s->pix_format.width = rect->width;
	s->pix_format.height = rect->height;
	memcpy(&(s->_rect), rect, sizeof(*rect));

	if ((cam->module_param.force_munmap  || cam->io == IO_READ) &&
	    nbuffers != zc0301_request_buffers(cam, nbuffers, cam->io)) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_CROP failed because of not enough memory. To "
		       "use the camera, close and open /dev/video%d again.",
		    cam->v4ldev->minor);
		return -ENOMEM;
	}

	if (cam->io == IO_READ)
		zc0301_empty_framequeues(cam);
	else if (cam->module_param.force_munmap)
		zc0301_requeue_outqueue(cam);

	cam->stream = stream;

	return 0;
}


static int
zc0301_vidioc_enum_fmt(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_fmtdesc fmtd;

	if (copy_from_user(&fmtd, arg, sizeof(fmtd)))
		return -EFAULT;

	if (fmtd.index == 0) {
		strcpy(fmtd.description, "JPEG");
		fmtd.pixelformat = V4L2_PIX_FMT_JPEG;
		fmtd.flags = V4L2_FMT_FLAG_COMPRESSED;
	} else
		return -EINVAL;

	fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	memset(&fmtd.reserved, 0, sizeof(fmtd.reserved));

	if (copy_to_user(arg, &fmtd, sizeof(fmtd)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_g_fmt(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_format format;
	struct v4l2_pix_format* pfmt = &(cam->sensor.pix_format);

	if (copy_from_user(&format, arg, sizeof(format)))
		return -EFAULT;

	if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	pfmt->bytesperline = 0;
	pfmt->sizeimage = pfmt->height * ((pfmt->width*pfmt->priv)/8);
	pfmt->field = V4L2_FIELD_NONE;
	memcpy(&(format.fmt.pix), pfmt, sizeof(*pfmt));

	if (copy_to_user(arg, &format, sizeof(format)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_try_s_fmt(struct zc0301_device* cam, unsigned int cmd,
			void __user * arg)
{
	struct zc0301_sensor* s = &cam->sensor;
	struct v4l2_format format;
	struct v4l2_pix_format* pix;
	struct v4l2_pix_format* pfmt = &(s->pix_format);
	struct v4l2_rect* bounds = &(s->cropcap.bounds);
	struct v4l2_rect rect;
	const enum zc0301_stream_state stream = cam->stream;
	const u32 nbuffers = cam->nbuffers;
	u32 i;
	int err = 0;

	if (copy_from_user(&format, arg, sizeof(format)))
		return -EFAULT;

	pix = &(format.fmt.pix);

	if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memcpy(&rect, &(s->_rect), sizeof(rect));

	if (!s->set_crop) {
		pix->width = rect.width;
		pix->height = rect.height;
	} else {
		rect.width = pix->width;
		rect.height = pix->height;
	}

	if (rect.width < 8)
		rect.width = 8;
	if (rect.height < 8)
		rect.height = 8;
	if (rect.width > bounds->left + bounds->width - rect.left)
		rect.width = bounds->left + bounds->width - rect.left;
	if (rect.height > bounds->top + bounds->height - rect.top)
		rect.height = bounds->top + bounds->height - rect.top;
	rect.width &= ~7L;
	rect.height &= ~7L;

	pix->width = rect.width;
	pix->height = rect.height;
	pix->pixelformat = pfmt->pixelformat;
	pix->priv = pfmt->priv;
	pix->colorspace = pfmt->colorspace;
	pix->bytesperline = 0;
	pix->sizeimage = pix->height * ((pix->width * pix->priv) / 8);
	pix->field = V4L2_FIELD_NONE;

	if (cmd == VIDIOC_TRY_FMT) {
		if (copy_to_user(arg, &format, sizeof(format)))
			return -EFAULT;
		return 0;
	}

	if (cam->module_param.force_munmap)
		for (i = 0; i < cam->nbuffers; i++)
			if (cam->frame[i].vma_use_count) {
				DBG(3, "VIDIOC_S_FMT failed. "
				       "Unmap the buffers first.");
				return -EINVAL;
			}

	if (cam->stream == STREAM_ON)
		if ((err = zc0301_stream_interrupt(cam)))
			return err;

	if (copy_to_user(arg, &format, sizeof(format))) {
		cam->stream = stream;
		return -EFAULT;
	}

	if (cam->module_param.force_munmap || cam->io == IO_READ)
		zc0301_release_buffers(cam);

	if (s->set_crop)
		err += s->set_crop(cam, &rect);

	if (err) { /* atomic, no rollback in ioctl() */
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_FMT failed because of hardware problems. To "
		       "use the camera, close and open /dev/video%d again.",
		    cam->v4ldev->minor);
		return -EIO;
	}

	memcpy(pfmt, pix, sizeof(*pix));
	memcpy(&(s->_rect), &rect, sizeof(rect));

	if ((cam->module_param.force_munmap  || cam->io == IO_READ) &&
	    nbuffers != zc0301_request_buffers(cam, nbuffers, cam->io)) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_FMT failed because of not enough memory. To "
		       "use the camera, close and open /dev/video%d again.",
		    cam->v4ldev->minor);
		return -ENOMEM;
	}

	if (cam->io == IO_READ)
		zc0301_empty_framequeues(cam);
	else if (cam->module_param.force_munmap)
		zc0301_requeue_outqueue(cam);

	cam->stream = stream;

	return 0;
}


static int
zc0301_vidioc_g_jpegcomp(struct zc0301_device* cam, void __user * arg)
{
	if (copy_to_user(arg, &cam->compression, sizeof(cam->compression)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_s_jpegcomp(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_jpegcompression jc;
	const enum zc0301_stream_state stream = cam->stream;
	int err = 0;

	if (copy_from_user(&jc, arg, sizeof(jc)))
		return -EFAULT;

	if (jc.quality != 0)
		return -EINVAL;

	if (cam->stream == STREAM_ON)
		if ((err = zc0301_stream_interrupt(cam)))
			return err;

	err += zc0301_set_compression(cam, &jc);
	if (err) { /* atomic, no rollback in ioctl() */
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_JPEGCOMP failed because of hardware "
		       "problems. To use the camera, close and open "
		       "/dev/video%d again.", cam->v4ldev->minor);
		return -EIO;
	}

	cam->compression.quality = jc.quality;

	cam->stream = stream;

	return 0;
}


static int
zc0301_vidioc_reqbufs(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_requestbuffers rb;
	u32 i;
	int err;

	if (copy_from_user(&rb, arg, sizeof(rb)))
		return -EFAULT;

	if (rb.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    rb.memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	if (cam->io == IO_READ) {
		DBG(3, "Close and open the device again to choose the mmap "
		       "I/O method");
		return -EINVAL;
	}

	for (i = 0; i < cam->nbuffers; i++)
		if (cam->frame[i].vma_use_count) {
			DBG(3, "VIDIOC_REQBUFS failed. "
			       "Previous buffers are still mapped.");
			return -EINVAL;
		}

	if (cam->stream == STREAM_ON)
		if ((err = zc0301_stream_interrupt(cam)))
			return err;

	zc0301_empty_framequeues(cam);

	zc0301_release_buffers(cam);
	if (rb.count)
		rb.count = zc0301_request_buffers(cam, rb.count, IO_MMAP);

	if (copy_to_user(arg, &rb, sizeof(rb))) {
		zc0301_release_buffers(cam);
		cam->io = IO_NONE;
		return -EFAULT;
	}

	cam->io = rb.count ? IO_MMAP : IO_NONE;

	return 0;
}


static int
zc0301_vidioc_querybuf(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_buffer b;

	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;

	if (b.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    b.index >= cam->nbuffers || cam->io != IO_MMAP)
		return -EINVAL;

	memcpy(&b, &cam->frame[b.index].buf, sizeof(b));

	if (cam->frame[b.index].vma_use_count)
		b.flags |= V4L2_BUF_FLAG_MAPPED;

	if (cam->frame[b.index].state == F_DONE)
		b.flags |= V4L2_BUF_FLAG_DONE;
	else if (cam->frame[b.index].state != F_UNUSED)
		b.flags |= V4L2_BUF_FLAG_QUEUED;

	if (copy_to_user(arg, &b, sizeof(b)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_qbuf(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_buffer b;
	unsigned long lock_flags;

	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;

	if (b.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    b.index >= cam->nbuffers || cam->io != IO_MMAP)
		return -EINVAL;

	if (cam->frame[b.index].state != F_UNUSED)
		return -EINVAL;

	cam->frame[b.index].state = F_QUEUED;

	spin_lock_irqsave(&cam->queue_lock, lock_flags);
	list_add_tail(&cam->frame[b.index].frame, &cam->inqueue);
	spin_unlock_irqrestore(&cam->queue_lock, lock_flags);

	PDBGG("Frame #%lu queued", (unsigned long)b.index);

	return 0;
}


static int
zc0301_vidioc_dqbuf(struct zc0301_device* cam, struct file* filp,
		    void __user * arg)
{
	struct v4l2_buffer b;
	struct zc0301_frame_t *f;
	unsigned long lock_flags;
	long timeout;

	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;

	if (b.type != V4L2_BUF_TYPE_VIDEO_CAPTURE || cam->io!= IO_MMAP)
		return -EINVAL;

	if (list_empty(&cam->outqueue)) {
		if (cam->stream == STREAM_OFF)
			return -EINVAL;
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		timeout = wait_event_interruptible_timeout
			  ( cam->wait_frame,
			    (!list_empty(&cam->outqueue)) ||
			    (cam->state & DEV_DISCONNECTED) ||
			    (cam->state & DEV_MISCONFIGURED),
			    cam->module_param.frame_timeout *
			    1000 * msecs_to_jiffies(1) );
		if (timeout < 0)
			return timeout;
		if (cam->state & DEV_DISCONNECTED)
			return -ENODEV;
		if (!timeout || (cam->state & DEV_MISCONFIGURED))
			return -EIO;
	}

	spin_lock_irqsave(&cam->queue_lock, lock_flags);
	f = list_entry(cam->outqueue.next, struct zc0301_frame_t, frame);
	list_del(cam->outqueue.next);
	spin_unlock_irqrestore(&cam->queue_lock, lock_flags);

	f->state = F_UNUSED;

	memcpy(&b, &f->buf, sizeof(b));
	if (f->vma_use_count)
		b.flags |= V4L2_BUF_FLAG_MAPPED;

	if (copy_to_user(arg, &b, sizeof(b)))
		return -EFAULT;

	PDBGG("Frame #%lu dequeued", (unsigned long)f->buf.index);

	return 0;
}


static int
zc0301_vidioc_streamon(struct zc0301_device* cam, void __user * arg)
{
	int type;

	if (copy_from_user(&type, arg, sizeof(type)))
		return -EFAULT;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE || cam->io != IO_MMAP)
		return -EINVAL;

	if (list_empty(&cam->inqueue))
		return -EINVAL;

	cam->stream = STREAM_ON;

	DBG(3, "Stream on");

	return 0;
}


static int
zc0301_vidioc_streamoff(struct zc0301_device* cam, void __user * arg)
{
	int type, err;

	if (copy_from_user(&type, arg, sizeof(type)))
		return -EFAULT;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE || cam->io != IO_MMAP)
		return -EINVAL;

	if (cam->stream == STREAM_ON)
		if ((err = zc0301_stream_interrupt(cam)))
			return err;

	zc0301_empty_framequeues(cam);

	DBG(3, "Stream off");

	return 0;
}


static int
zc0301_vidioc_g_parm(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_streamparm sp;

	if (copy_from_user(&sp, arg, sizeof(sp)))
		return -EFAULT;

	if (sp.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp.parm.capture.extendedmode = 0;
	sp.parm.capture.readbuffers = cam->nreadbuffers;

	if (copy_to_user(arg, &sp, sizeof(sp)))
		return -EFAULT;

	return 0;
}


static int
zc0301_vidioc_s_parm(struct zc0301_device* cam, void __user * arg)
{
	struct v4l2_streamparm sp;

	if (copy_from_user(&sp, arg, sizeof(sp)))
		return -EFAULT;

	if (sp.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp.parm.capture.extendedmode = 0;

	if (sp.parm.capture.readbuffers == 0)
		sp.parm.capture.readbuffers = cam->nreadbuffers;

	if (sp.parm.capture.readbuffers > ZC0301_MAX_FRAMES)
		sp.parm.capture.readbuffers = ZC0301_MAX_FRAMES;

	if (copy_to_user(arg, &sp, sizeof(sp)))
		return -EFAULT;

	cam->nreadbuffers = sp.parm.capture.readbuffers;

	return 0;
}


static int zc0301_ioctl_v4l2(struct inode* inode, struct file* filp,
			     unsigned int cmd, void __user * arg)
{
	struct zc0301_device* cam = video_get_drvdata(video_devdata(filp));

	switch (cmd) {

	case VIDIOC_QUERYCAP:
		return zc0301_vidioc_querycap(cam, arg);

	case VIDIOC_ENUMINPUT:
		return zc0301_vidioc_enuminput(cam, arg);

	case VIDIOC_G_INPUT:
		return zc0301_vidioc_g_input(cam, arg);

	case VIDIOC_S_INPUT:
		return zc0301_vidioc_s_input(cam, arg);

	case VIDIOC_QUERYCTRL:
		return zc0301_vidioc_query_ctrl(cam, arg);

	case VIDIOC_G_CTRL:
		return zc0301_vidioc_g_ctrl(cam, arg);

	case VIDIOC_S_CTRL_OLD:
	case VIDIOC_S_CTRL:
		return zc0301_vidioc_s_ctrl(cam, arg);

	case VIDIOC_CROPCAP_OLD:
	case VIDIOC_CROPCAP:
		return zc0301_vidioc_cropcap(cam, arg);

	case VIDIOC_G_CROP:
		return zc0301_vidioc_g_crop(cam, arg);

	case VIDIOC_S_CROP:
		return zc0301_vidioc_s_crop(cam, arg);

	case VIDIOC_ENUM_FMT:
		return zc0301_vidioc_enum_fmt(cam, arg);

	case VIDIOC_G_FMT:
		return zc0301_vidioc_g_fmt(cam, arg);

	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT:
		return zc0301_vidioc_try_s_fmt(cam, cmd, arg);

	case VIDIOC_G_JPEGCOMP:
		return zc0301_vidioc_g_jpegcomp(cam, arg);

	case VIDIOC_S_JPEGCOMP:
		return zc0301_vidioc_s_jpegcomp(cam, arg);

	case VIDIOC_REQBUFS:
		return zc0301_vidioc_reqbufs(cam, arg);

	case VIDIOC_QUERYBUF:
		return zc0301_vidioc_querybuf(cam, arg);

	case VIDIOC_QBUF:
		return zc0301_vidioc_qbuf(cam, arg);

	case VIDIOC_DQBUF:
		return zc0301_vidioc_dqbuf(cam, filp, arg);

	case VIDIOC_STREAMON:
		return zc0301_vidioc_streamon(cam, arg);

	case VIDIOC_STREAMOFF:
		return zc0301_vidioc_streamoff(cam, arg);

	case VIDIOC_G_PARM:
		return zc0301_vidioc_g_parm(cam, arg);

	case VIDIOC_S_PARM_OLD:
	case VIDIOC_S_PARM:
		return zc0301_vidioc_s_parm(cam, arg);

	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_QUERYSTD:
	case VIDIOC_ENUMSTD:
	case VIDIOC_QUERYMENU:
		return -EINVAL;

	default:
		return -EINVAL;

	}
}


static int zc0301_ioctl(struct inode* inode, struct file* filp,
			unsigned int cmd, unsigned long arg)
{
	struct zc0301_device* cam = video_get_drvdata(video_devdata(filp));
	int err = 0;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return -ERESTARTSYS;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		mutex_unlock(&cam->fileop_mutex);
		return -ENODEV;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		mutex_unlock(&cam->fileop_mutex);
		return -EIO;
	}

	V4LDBG(3, "zc0301", cmd);

	err = zc0301_ioctl_v4l2(inode, filp, cmd, (void __user *)arg);

	mutex_unlock(&cam->fileop_mutex);

	return err;
}


static struct file_operations zc0301_fops = {
	.owner =   THIS_MODULE,
	.open =    zc0301_open,
	.release = zc0301_release,
	.ioctl =   zc0301_ioctl,
	.read =    zc0301_read,
	.poll =    zc0301_poll,
	.mmap =    zc0301_mmap,
	.llseek =  no_llseek,
};

/*****************************************************************************/

static int
zc0301_usb_probe(struct usb_interface* intf, const struct usb_device_id* id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct zc0301_device* cam;
	static unsigned int dev_nr = 0;
	unsigned int i;
	int err = 0;

	if (!(cam = kzalloc(sizeof(struct zc0301_device), GFP_KERNEL)))
		return -ENOMEM;

	cam->usbdev = udev;

	if (!(cam->control_buffer = kzalloc(4, GFP_KERNEL))) {
		DBG(1, "kmalloc() failed");
		err = -ENOMEM;
		goto fail;
	}

	if (!(cam->v4ldev = video_device_alloc())) {
		DBG(1, "video_device_alloc() failed");
		err = -ENOMEM;
		goto fail;
	}

	mutex_init(&cam->dev_mutex);

	DBG(2, "ZC0301 Image Processor and Control Chip detected "
	       "(vid/pid 0x%04X/0x%04X)",id->idVendor, id->idProduct);

	for  (i = 0; zc0301_sensor_table[i]; i++) {
		err = zc0301_sensor_table[i](cam);
		if (!err)
			break;
	}

	if (!err)
		DBG(2, "%s image sensor detected", cam->sensor.name);
	else {
		DBG(1, "No supported image sensor detected");
		err = -ENODEV;
		goto fail;
	}

	if (zc0301_init(cam)) {
		DBG(1, "Initialization failed. I will retry on open().");
		cam->state |= DEV_MISCONFIGURED;
	}

	strcpy(cam->v4ldev->name, "ZC0301 PC Camera");
	cam->v4ldev->owner = THIS_MODULE;
	cam->v4ldev->type = VID_TYPE_CAPTURE | VID_TYPE_SCALES;
	cam->v4ldev->hardware = 0;
	cam->v4ldev->fops = &zc0301_fops;
	cam->v4ldev->minor = video_nr[dev_nr];
	cam->v4ldev->release = video_device_release;
	video_set_drvdata(cam->v4ldev, cam);

	mutex_lock(&cam->dev_mutex);

	err = video_register_device(cam->v4ldev, VFL_TYPE_GRABBER,
				    video_nr[dev_nr]);
	if (err) {
		DBG(1, "V4L2 device registration failed");
		if (err == -ENFILE && video_nr[dev_nr] == -1)
			DBG(1, "Free /dev/videoX node not found");
		video_nr[dev_nr] = -1;
		dev_nr = (dev_nr < ZC0301_MAX_DEVICES-1) ? dev_nr+1 : 0;
		mutex_unlock(&cam->dev_mutex);
		goto fail;
	}

	DBG(2, "V4L2 device registered as /dev/video%d", cam->v4ldev->minor);

	cam->module_param.force_munmap = force_munmap[dev_nr];
	cam->module_param.frame_timeout = frame_timeout[dev_nr];

	dev_nr = (dev_nr < ZC0301_MAX_DEVICES-1) ? dev_nr+1 : 0;

	usb_set_intfdata(intf, cam);

	mutex_unlock(&cam->dev_mutex);

	return 0;

fail:
	if (cam) {
		kfree(cam->control_buffer);
		if (cam->v4ldev)
			video_device_release(cam->v4ldev);
		kfree(cam);
	}
	return err;
}


static void zc0301_usb_disconnect(struct usb_interface* intf)
{
	struct zc0301_device* cam = usb_get_intfdata(intf);

	if (!cam)
		return;

	down_write(&zc0301_disconnect);

	mutex_lock(&cam->dev_mutex);

	DBG(2, "Disconnecting %s...", cam->v4ldev->name);

	wake_up_interruptible_all(&cam->open);

	if (cam->users) {
		DBG(2, "Device /dev/video%d is open! Deregistration and "
		       "memory deallocation are deferred on close.",
		    cam->v4ldev->minor);
		cam->state |= DEV_MISCONFIGURED;
		zc0301_stop_transfer(cam);
		cam->state |= DEV_DISCONNECTED;
		wake_up_interruptible(&cam->wait_frame);
		wake_up(&cam->wait_stream);
		usb_get_dev(cam->usbdev);
	} else {
		cam->state |= DEV_DISCONNECTED;
		zc0301_release_resources(cam);
	}

	mutex_unlock(&cam->dev_mutex);

	if (!cam->users)
		kfree(cam);

	up_write(&zc0301_disconnect);
}


static struct usb_driver zc0301_usb_driver = {
	.name =       "zc0301",
	.id_table =   zc0301_id_table,
	.probe =      zc0301_usb_probe,
	.disconnect = zc0301_usb_disconnect,
};

/*****************************************************************************/

static int __init zc0301_module_init(void)
{
	int err = 0;

	KDBG(2, ZC0301_MODULE_NAME " v" ZC0301_MODULE_VERSION);
	KDBG(3, ZC0301_MODULE_AUTHOR);

	if ((err = usb_register(&zc0301_usb_driver)))
		KDBG(1, "usb_register() failed");

	return err;
}


static void __exit zc0301_module_exit(void)
{
	usb_deregister(&zc0301_usb_driver);
}


module_init(zc0301_module_init);
module_exit(zc0301_module_exit);
