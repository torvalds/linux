/*
 * Main USB camera driver
 *
 * Copyright (C) 2008-2011 Jean-François Moine <http://moinejf.free.fr>
 *
 * Camera button input handling by Márton Németh
 * Copyright (C) 2009-2010 Márton Németh <nm127@freemail.hu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define GSPCA_VERSION	"2.14.0"

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/io.h>
#include <asm/page.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

#include "gspca.h"

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
#include <linux/input.h>
#include <linux/usb/input.h>
#endif

/* global values */
#define DEF_NURBS 3		/* default number of URBs */
#if DEF_NURBS > MAX_NURBS
#error "DEF_NURBS too big"
#endif

MODULE_AUTHOR("Jean-François Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("GSPCA USB Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(GSPCA_VERSION);

#ifdef GSPCA_DEBUG
int gspca_debug = D_ERR | D_PROBE;
EXPORT_SYMBOL(gspca_debug);

static void PDEBUG_MODE(char *txt, __u32 pixfmt, int w, int h)
{
	if ((pixfmt >> 24) >= '0' && (pixfmt >> 24) <= 'z') {
		PDEBUG(D_CONF|D_STREAM, "%s %c%c%c%c %dx%d",
			txt,
			pixfmt & 0xff,
			(pixfmt >> 8) & 0xff,
			(pixfmt >> 16) & 0xff,
			pixfmt >> 24,
			w, h);
	} else {
		PDEBUG(D_CONF|D_STREAM, "%s 0x%08x %dx%d",
			txt,
			pixfmt,
			w, h);
	}
}
#else
#define PDEBUG_MODE(txt, pixfmt, w, h)
#endif

/* specific memory types - !! should be different from V4L2_MEMORY_xxx */
#define GSPCA_MEMORY_NO 0	/* V4L2_MEMORY_xxx starts from 1 */
#define GSPCA_MEMORY_READ 7

#define BUF_ALL_FLAGS (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE)

/*
 * VMA operations.
 */
static void gspca_vm_open(struct vm_area_struct *vma)
{
	struct gspca_frame *frame = vma->vm_private_data;

	frame->vma_use_count++;
	frame->v4l2_buf.flags |= V4L2_BUF_FLAG_MAPPED;
}

static void gspca_vm_close(struct vm_area_struct *vma)
{
	struct gspca_frame *frame = vma->vm_private_data;

	if (--frame->vma_use_count <= 0)
		frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_MAPPED;
}

static const struct vm_operations_struct gspca_vm_ops = {
	.open		= gspca_vm_open,
	.close		= gspca_vm_close,
};

/*
 * Input and interrupt endpoint handling functions
 */
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
static void int_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	int ret;

	ret = urb->status;
	switch (ret) {
	case 0:
		if (gspca_dev->sd_desc->int_pkt_scan(gspca_dev,
		    urb->transfer_buffer, urb->actual_length) < 0) {
			PDEBUG(D_ERR, "Unknown packet received");
		}
		break;

	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		/* Stop is requested either by software or hardware is gone,
		 * keep the ret value non-zero and don't resubmit later.
		 */
		break;

	default:
		PDEBUG(D_ERR, "URB error %i, resubmitting", urb->status);
		urb->status = 0;
		ret = 0;
	}

	if (ret == 0) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0)
			pr_err("Resubmit URB failed with error %i\n", ret);
	}
}

static int gspca_input_connect(struct gspca_dev *dev)
{
	struct input_dev *input_dev;
	int err = 0;

	dev->input_dev = NULL;
	if (dev->sd_desc->int_pkt_scan || dev->sd_desc->other_input)  {
		input_dev = input_allocate_device();
		if (!input_dev)
			return -ENOMEM;

		usb_make_path(dev->dev, dev->phys, sizeof(dev->phys));
		strlcat(dev->phys, "/input0", sizeof(dev->phys));

		input_dev->name = dev->sd_desc->name;
		input_dev->phys = dev->phys;

		usb_to_input_id(dev->dev, &input_dev->id);

		input_dev->evbit[0] = BIT_MASK(EV_KEY);
		input_dev->keybit[BIT_WORD(KEY_CAMERA)] = BIT_MASK(KEY_CAMERA);
		input_dev->dev.parent = &dev->dev->dev;

		err = input_register_device(input_dev);
		if (err) {
			pr_err("Input device registration failed with error %i\n",
			       err);
			input_dev->dev.parent = NULL;
			input_free_device(input_dev);
		} else {
			dev->input_dev = input_dev;
		}
	}

	return err;
}

static int alloc_and_submit_int_urb(struct gspca_dev *gspca_dev,
			  struct usb_endpoint_descriptor *ep)
{
	unsigned int buffer_len;
	int interval;
	struct urb *urb;
	struct usb_device *dev;
	void *buffer = NULL;
	int ret = -EINVAL;

	buffer_len = le16_to_cpu(ep->wMaxPacketSize);
	interval = ep->bInterval;
	PDEBUG(D_CONF, "found int in endpoint: 0x%x, "
		"buffer_len=%u, interval=%u",
		ep->bEndpointAddress, buffer_len, interval);

	dev = gspca_dev->dev;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto error;
	}

	buffer = usb_alloc_coherent(dev, buffer_len,
				GFP_KERNEL, &urb->transfer_dma);
	if (!buffer) {
		ret = -ENOMEM;
		goto error_buffer;
	}
	usb_fill_int_urb(urb, dev,
		usb_rcvintpipe(dev, ep->bEndpointAddress),
		buffer, buffer_len,
		int_irq, (void *)gspca_dev, interval);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret < 0) {
		PDEBUG(D_ERR, "submit int URB failed with error %i", ret);
		goto error_submit;
	}
	gspca_dev->int_urb = urb;
	return ret;

error_submit:
	usb_free_coherent(dev,
			  urb->transfer_buffer_length,
			  urb->transfer_buffer,
			  urb->transfer_dma);
error_buffer:
	usb_free_urb(urb);
error:
	return ret;
}

static void gspca_input_create_urb(struct gspca_dev *gspca_dev)
{
	struct usb_interface *intf;
	struct usb_host_interface *intf_desc;
	struct usb_endpoint_descriptor *ep;
	int i;

	if (gspca_dev->sd_desc->int_pkt_scan)  {
		intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
		intf_desc = intf->cur_altsetting;
		for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
			ep = &intf_desc->endpoint[i].desc;
			if (usb_endpoint_dir_in(ep) &&
			    usb_endpoint_xfer_int(ep)) {

				alloc_and_submit_int_urb(gspca_dev, ep);
				break;
			}
		}
	}
}

static void gspca_input_destroy_urb(struct gspca_dev *gspca_dev)
{
	struct urb *urb;

	urb = gspca_dev->int_urb;
	if (urb) {
		gspca_dev->int_urb = NULL;
		usb_kill_urb(urb);
		usb_free_coherent(gspca_dev->dev,
				  urb->transfer_buffer_length,
				  urb->transfer_buffer,
				  urb->transfer_dma);
		usb_free_urb(urb);
	}
}
#else
static inline void gspca_input_destroy_urb(struct gspca_dev *gspca_dev)
{
}

static inline void gspca_input_create_urb(struct gspca_dev *gspca_dev)
{
}

static inline int gspca_input_connect(struct gspca_dev *dev)
{
	return 0;
}
#endif

/*
 * fill a video frame from an URB and resubmit
 */
static void fill_frame(struct gspca_dev *gspca_dev,
			struct urb *urb)
{
	u8 *data;		/* address of data in the iso message */
	int i, len, st;
	cam_pkt_op pkt_scan;

	if (urb->status != 0) {
		if (urb->status == -ESHUTDOWN)
			return;		/* disconnection */
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			return;
#endif
		PDEBUG(D_ERR|D_PACK, "urb status: %d", urb->status);
		urb->status = 0;
		goto resubmit;
	}
	pkt_scan = gspca_dev->sd_desc->pkt_scan;
	for (i = 0; i < urb->number_of_packets; i++) {
		len = urb->iso_frame_desc[i].actual_length;

		/* check the packet status and length */
		st = urb->iso_frame_desc[i].status;
		if (st) {
			pr_err("ISOC data error: [%d] len=%d, status=%d\n",
			       i, len, st);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}
		if (len == 0) {
			if (gspca_dev->empty_packet == 0)
				gspca_dev->empty_packet = 1;
			continue;
		}

		/* let the packet be analyzed by the subdriver */
		PDEBUG(D_PACK, "packet [%d] o:%d l:%d",
			i, urb->iso_frame_desc[i].offset, len);
		data = (u8 *) urb->transfer_buffer
					+ urb->iso_frame_desc[i].offset;
		pkt_scan(gspca_dev, data, len);
	}

resubmit:
	/* resubmit the URB */
	st = usb_submit_urb(urb, GFP_ATOMIC);
	if (st < 0)
		pr_err("usb_submit_urb() ret %d\n", st);
}

/*
 * ISOC message interrupt from the USB device
 *
 * Analyse each packet and call the subdriver for copy to the frame buffer.
 */
static void isoc_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;

	PDEBUG(D_PACK, "isoc irq");
	if (!gspca_dev->streaming)
		return;
	fill_frame(gspca_dev, urb);
}

/*
 * bulk message interrupt from the USB device
 */
static void bulk_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	int st;

	PDEBUG(D_PACK, "bulk irq");
	if (!gspca_dev->streaming)
		return;
	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:
		return;		/* disconnection */
	default:
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			return;
#endif
		PDEBUG(D_ERR|D_PACK, "urb status: %d", urb->status);
		urb->status = 0;
		goto resubmit;
	}

	PDEBUG(D_PACK, "packet l:%d", urb->actual_length);
	gspca_dev->sd_desc->pkt_scan(gspca_dev,
				urb->transfer_buffer,
				urb->actual_length);

resubmit:
	/* resubmit the URB */
	if (gspca_dev->cam.bulk_nurbs != 0) {
		st = usb_submit_urb(urb, GFP_ATOMIC);
		if (st < 0)
			pr_err("usb_submit_urb() ret %d\n", st);
	}
}

/*
 * add data to the current frame
 *
 * This function is called by the subdrivers at interrupt level.
 *
 * To build a frame, these ones must add
 *	- one FIRST_PACKET
 *	- 0 or many INTER_PACKETs
 *	- one LAST_PACKET
 * DISCARD_PACKET invalidates the whole frame.
 */
void gspca_frame_add(struct gspca_dev *gspca_dev,
			enum gspca_packet_type packet_type,
			const u8 *data,
			int len)
{
	struct gspca_frame *frame;
	int i, j;

	PDEBUG(D_PACK, "add t:%d l:%d",	packet_type, len);

	if (packet_type == FIRST_PACKET) {
		i = atomic_read(&gspca_dev->fr_i);

		/* if there are no queued buffer, discard the whole frame */
		if (i == atomic_read(&gspca_dev->fr_q)) {
			gspca_dev->last_packet_type = DISCARD_PACKET;
			gspca_dev->sequence++;
			return;
		}
		j = gspca_dev->fr_queue[i];
		frame = &gspca_dev->frame[j];
		frame->v4l2_buf.timestamp = ktime_to_timeval(ktime_get());
		frame->v4l2_buf.sequence = gspca_dev->sequence++;
		gspca_dev->image = frame->data;
		gspca_dev->image_len = 0;
	} else {
		switch (gspca_dev->last_packet_type) {
		case DISCARD_PACKET:
			if (packet_type == LAST_PACKET) {
				gspca_dev->last_packet_type = packet_type;
				gspca_dev->image = NULL;
				gspca_dev->image_len = 0;
			}
			return;
		case LAST_PACKET:
			return;
		}
	}

	/* append the packet to the frame buffer */
	if (len > 0) {
		if (gspca_dev->image_len + len > gspca_dev->frsz) {
			PDEBUG(D_ERR|D_PACK, "frame overflow %d > %d",
				gspca_dev->image_len + len,
				gspca_dev->frsz);
			packet_type = DISCARD_PACKET;
		} else {
/* !! image is NULL only when last pkt is LAST or DISCARD
			if (gspca_dev->image == NULL) {
				pr_err("gspca_frame_add() image == NULL\n");
				return;
			}
 */
			memcpy(gspca_dev->image + gspca_dev->image_len,
				data, len);
			gspca_dev->image_len += len;
		}
	}
	gspca_dev->last_packet_type = packet_type;

	/* if last packet, invalidate packet concatenation until
	 * next first packet, wake up the application and advance
	 * in the queue */
	if (packet_type == LAST_PACKET) {
		i = atomic_read(&gspca_dev->fr_i);
		j = gspca_dev->fr_queue[i];
		frame = &gspca_dev->frame[j];
		frame->v4l2_buf.bytesused = gspca_dev->image_len;
		frame->v4l2_buf.flags = (frame->v4l2_buf.flags
					 | V4L2_BUF_FLAG_DONE)
					& ~V4L2_BUF_FLAG_QUEUED;
		i = (i + 1) % GSPCA_MAX_FRAMES;
		atomic_set(&gspca_dev->fr_i, i);
		wake_up_interruptible(&gspca_dev->wq);	/* event = new frame */
		PDEBUG(D_FRAM, "frame complete len:%d",
			frame->v4l2_buf.bytesused);
		gspca_dev->image = NULL;
		gspca_dev->image_len = 0;
	}
}
EXPORT_SYMBOL(gspca_frame_add);

static int frame_alloc(struct gspca_dev *gspca_dev, struct file *file,
			enum v4l2_memory memory, unsigned int count)
{
	struct gspca_frame *frame;
	unsigned int frsz;
	int i;

	i = gspca_dev->curr_mode;
	frsz = gspca_dev->cam.cam_mode[i].sizeimage;
	PDEBUG(D_STREAM, "frame alloc frsz: %d", frsz);
	frsz = PAGE_ALIGN(frsz);
	if (count >= GSPCA_MAX_FRAMES)
		count = GSPCA_MAX_FRAMES - 1;
	gspca_dev->frbuf = vmalloc_32(frsz * count);
	if (!gspca_dev->frbuf) {
		pr_err("frame alloc failed\n");
		return -ENOMEM;
	}
	gspca_dev->capt_file = file;
	gspca_dev->memory = memory;
	gspca_dev->frsz = frsz;
	gspca_dev->nframes = count;
	for (i = 0; i < count; i++) {
		frame = &gspca_dev->frame[i];
		frame->v4l2_buf.index = i;
		frame->v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		frame->v4l2_buf.flags = 0;
		frame->v4l2_buf.field = V4L2_FIELD_NONE;
		frame->v4l2_buf.length = frsz;
		frame->v4l2_buf.memory = memory;
		frame->v4l2_buf.sequence = 0;
		frame->data = gspca_dev->frbuf + i * frsz;
		frame->v4l2_buf.m.offset = i * frsz;
	}
	atomic_set(&gspca_dev->fr_q, 0);
	atomic_set(&gspca_dev->fr_i, 0);
	gspca_dev->fr_o = 0;
	return 0;
}

static void frame_free(struct gspca_dev *gspca_dev)
{
	int i;

	PDEBUG(D_STREAM, "frame free");
	if (gspca_dev->frbuf != NULL) {
		vfree(gspca_dev->frbuf);
		gspca_dev->frbuf = NULL;
		for (i = 0; i < gspca_dev->nframes; i++)
			gspca_dev->frame[i].data = NULL;
	}
	gspca_dev->nframes = 0;
	gspca_dev->frsz = 0;
	gspca_dev->capt_file = NULL;
	gspca_dev->memory = GSPCA_MEMORY_NO;
}

static void destroy_urbs(struct gspca_dev *gspca_dev)
{
	struct urb *urb;
	unsigned int i;

	PDEBUG(D_STREAM, "kill transfer");
	for (i = 0; i < MAX_NURBS; i++) {
		urb = gspca_dev->urb[i];
		if (urb == NULL)
			break;

		gspca_dev->urb[i] = NULL;
		usb_kill_urb(urb);
		if (urb->transfer_buffer != NULL)
			usb_free_coherent(gspca_dev->dev,
					  urb->transfer_buffer_length,
					  urb->transfer_buffer,
					  urb->transfer_dma);
		usb_free_urb(urb);
	}
}

static int gspca_set_alt0(struct gspca_dev *gspca_dev)
{
	int ret;

	if (gspca_dev->alt == 0)
		return 0;
	ret = usb_set_interface(gspca_dev->dev, gspca_dev->iface, 0);
	if (ret < 0)
		pr_err("set alt 0 err %d\n", ret);
	return ret;
}

/* Note: both the queue and the usb locks should be held when calling this */
static void gspca_stream_off(struct gspca_dev *gspca_dev)
{
	gspca_dev->streaming = 0;
	gspca_dev->usb_err = 0;
	if (gspca_dev->sd_desc->stopN)
		gspca_dev->sd_desc->stopN(gspca_dev);
	destroy_urbs(gspca_dev);
	gspca_input_destroy_urb(gspca_dev);
	gspca_set_alt0(gspca_dev);
	gspca_input_create_urb(gspca_dev);
	if (gspca_dev->sd_desc->stop0)
		gspca_dev->sd_desc->stop0(gspca_dev);
	PDEBUG(D_STREAM, "stream off OK");
}

/*
 * look for an input transfer endpoint in an alternate setting
 */
static struct usb_host_endpoint *alt_xfer(struct usb_host_interface *alt,
					  int xfer)
{
	struct usb_host_endpoint *ep;
	int i, attr;

	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		ep = &alt->endpoint[i];
		attr = ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
		if (attr == xfer
		    && ep->desc.wMaxPacketSize != 0
		    && usb_endpoint_dir_in(&ep->desc))
			return ep;
	}
	return NULL;
}

/* compute the minimum bandwidth for the current transfer */
static u32 which_bandwidth(struct gspca_dev *gspca_dev)
{
	u32 bandwidth;
	int i;

	/* get the (max) image size */
	i = gspca_dev->curr_mode;
	bandwidth = gspca_dev->cam.cam_mode[i].sizeimage;

	/* if the image is compressed, estimate its mean size */
	if (!gspca_dev->cam.needs_full_bandwidth &&
	    bandwidth < gspca_dev->cam.cam_mode[i].width *
				gspca_dev->cam.cam_mode[i].height)
		bandwidth = bandwidth * 3 / 8;	/* 0.375 */

	/* estimate the frame rate */
	if (gspca_dev->sd_desc->get_streamparm) {
		struct v4l2_streamparm parm;

		gspca_dev->sd_desc->get_streamparm(gspca_dev, &parm);
		bandwidth *= parm.parm.capture.timeperframe.denominator;
		bandwidth /= parm.parm.capture.timeperframe.numerator;
	} else {

		/* don't hope more than 15 fps with USB 1.1 and
		 * image resolution >= 640x480 */
		if (gspca_dev->width >= 640
		 && gspca_dev->dev->speed == USB_SPEED_FULL)
			bandwidth *= 15;		/* 15 fps */
		else
			bandwidth *= 30;		/* 30 fps */
	}

	PDEBUG(D_STREAM, "min bandwidth: %d", bandwidth);
	return bandwidth;
}

/* endpoint table */
#define MAX_ALT 16
struct ep_tb_s {
	u32 alt;
	u32 bandwidth;
};

/*
 * build the table of the endpoints
 * and compute the minimum bandwidth for the image transfer
 */
static int build_isoc_ep_tb(struct gspca_dev *gspca_dev,
			struct usb_interface *intf,
			struct ep_tb_s *ep_tb)
{
	struct usb_host_endpoint *ep;
	int i, j, nbalt, psize, found;
	u32 bandwidth, last_bw;

	nbalt = intf->num_altsetting;
	if (nbalt > MAX_ALT)
		nbalt = MAX_ALT;	/* fixme: should warn */

	/* build the endpoint table */
	i = 0;
	last_bw = 0;
	for (;;) {
		ep_tb->bandwidth = 2000 * 2000 * 120;
		found = 0;
		for (j = 0; j < nbalt; j++) {
			ep = alt_xfer(&intf->altsetting[j],
				      USB_ENDPOINT_XFER_ISOC);
			if (ep == NULL)
				continue;
			if (ep->desc.bInterval == 0) {
				pr_err("alt %d iso endp with 0 interval\n", j);
				continue;
			}
			psize = le16_to_cpu(ep->desc.wMaxPacketSize);
			psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
			bandwidth = psize * 1000;
			if (gspca_dev->dev->speed == USB_SPEED_HIGH
			 || gspca_dev->dev->speed == USB_SPEED_SUPER)
				bandwidth *= 8;
			bandwidth /= 1 << (ep->desc.bInterval - 1);
			if (bandwidth <= last_bw)
				continue;
			if (bandwidth < ep_tb->bandwidth) {
				ep_tb->bandwidth = bandwidth;
				ep_tb->alt = j;
				found = 1;
			}
		}
		if (!found)
			break;
		PDEBUG(D_STREAM, "alt %d bandwidth %d",
				ep_tb->alt, ep_tb->bandwidth);
		last_bw = ep_tb->bandwidth;
		i++;
		ep_tb++;
	}

	/*
	 * If the camera:
	 * has a usb audio class interface (a built in usb mic); and
	 * is a usb 1 full speed device; and
	 * uses the max full speed iso bandwidth; and
	 * and has more than 1 alt setting
	 * then skip the highest alt setting to spare bandwidth for the mic
	 */
	if (gspca_dev->audio &&
			gspca_dev->dev->speed == USB_SPEED_FULL &&
			last_bw >= 1000000 &&
			i > 1) {
		PDEBUG(D_STREAM, "dev has usb audio, skipping highest alt");
		i--;
		ep_tb--;
	}

	/* get the requested bandwidth and start at the highest atlsetting */
	bandwidth = which_bandwidth(gspca_dev);
	ep_tb--;
	while (i > 1) {
		ep_tb--;
		if (ep_tb->bandwidth < bandwidth)
			break;
		i--;
	}
	return i;
}

/*
 * create the URBs for image transfer
 */
static int create_urbs(struct gspca_dev *gspca_dev,
			struct usb_host_endpoint *ep)
{
	struct urb *urb;
	int n, nurbs, i, psize, npkt, bsize;

	/* calculate the packet size and the number of packets */
	psize = le16_to_cpu(ep->desc.wMaxPacketSize);

	if (!gspca_dev->cam.bulk) {		/* isoc */

		/* See paragraph 5.9 / table 5-11 of the usb 2.0 spec. */
		if (gspca_dev->pkt_size == 0)
			psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
		else
			psize = gspca_dev->pkt_size;
		npkt = gspca_dev->cam.npkt;
		if (npkt == 0)
			npkt = 32;		/* default value */
		bsize = psize * npkt;
		PDEBUG(D_STREAM,
			"isoc %d pkts size %d = bsize:%d",
			npkt, psize, bsize);
		nurbs = DEF_NURBS;
	} else {				/* bulk */
		npkt = 0;
		bsize = gspca_dev->cam.bulk_size;
		if (bsize == 0)
			bsize = psize;
		PDEBUG(D_STREAM, "bulk bsize:%d", bsize);
		if (gspca_dev->cam.bulk_nurbs != 0)
			nurbs = gspca_dev->cam.bulk_nurbs;
		else
			nurbs = 1;
	}

	for (n = 0; n < nurbs; n++) {
		urb = usb_alloc_urb(npkt, GFP_KERNEL);
		if (!urb) {
			pr_err("usb_alloc_urb failed\n");
			return -ENOMEM;
		}
		gspca_dev->urb[n] = urb;
		urb->transfer_buffer = usb_alloc_coherent(gspca_dev->dev,
						bsize,
						GFP_KERNEL,
						&urb->transfer_dma);

		if (urb->transfer_buffer == NULL) {
			pr_err("usb_alloc_coherent failed\n");
			return -ENOMEM;
		}
		urb->dev = gspca_dev->dev;
		urb->context = gspca_dev;
		urb->transfer_buffer_length = bsize;
		if (npkt != 0) {		/* ISOC */
			urb->pipe = usb_rcvisocpipe(gspca_dev->dev,
						    ep->desc.bEndpointAddress);
			urb->transfer_flags = URB_ISO_ASAP
					| URB_NO_TRANSFER_DMA_MAP;
			urb->interval = 1 << (ep->desc.bInterval - 1);
			urb->complete = isoc_irq;
			urb->number_of_packets = npkt;
			for (i = 0; i < npkt; i++) {
				urb->iso_frame_desc[i].length = psize;
				urb->iso_frame_desc[i].offset = psize * i;
			}
		} else {		/* bulk */
			urb->pipe = usb_rcvbulkpipe(gspca_dev->dev,
						ep->desc.bEndpointAddress);
			urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
			urb->complete = bulk_irq;
		}
	}
	return 0;
}

/*
 * start the USB transfer
 */
static int gspca_init_transfer(struct gspca_dev *gspca_dev)
{
	struct usb_interface *intf;
	struct usb_host_endpoint *ep;
	struct urb *urb;
	struct ep_tb_s ep_tb[MAX_ALT];
	int n, ret, xfer, alt, alt_idx;

	/* reset the streaming variables */
	gspca_dev->image = NULL;
	gspca_dev->image_len = 0;
	gspca_dev->last_packet_type = DISCARD_PACKET;
	gspca_dev->sequence = 0;

	gspca_dev->usb_err = 0;

	/* do the specific subdriver stuff before endpoint selection */
	intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
	gspca_dev->alt = gspca_dev->cam.bulk ? intf->num_altsetting : 0;
	if (gspca_dev->sd_desc->isoc_init) {
		ret = gspca_dev->sd_desc->isoc_init(gspca_dev);
		if (ret < 0)
			return ret;
	}
	xfer = gspca_dev->cam.bulk ? USB_ENDPOINT_XFER_BULK
				   : USB_ENDPOINT_XFER_ISOC;

	/* if bulk or the subdriver forced an altsetting, get the endpoint */
	if (gspca_dev->alt != 0) {
		gspca_dev->alt--;	/* (previous version compatibility) */
		ep = alt_xfer(&intf->altsetting[gspca_dev->alt], xfer);
		if (ep == NULL) {
			pr_err("bad altsetting %d\n", gspca_dev->alt);
			return -EIO;
		}
		ep_tb[0].alt = gspca_dev->alt;
		alt_idx = 1;
	} else {

	/* else, compute the minimum bandwidth
	 * and build the endpoint table */
		alt_idx = build_isoc_ep_tb(gspca_dev, intf, ep_tb);
		if (alt_idx <= 0) {
			pr_err("no transfer endpoint found\n");
			return -EIO;
		}
	}

	/* set the highest alternate setting and
	 * loop until urb submit succeeds */
	gspca_input_destroy_urb(gspca_dev);

	gspca_dev->alt = ep_tb[--alt_idx].alt;
	alt = -1;
	for (;;) {
		if (alt != gspca_dev->alt) {
			alt = gspca_dev->alt;
			if (intf->num_altsetting > 1) {
				ret = usb_set_interface(gspca_dev->dev,
							gspca_dev->iface,
							alt);
				if (ret < 0) {
					if (ret == -ENOSPC)
						goto retry; /*fixme: ugly*/
					pr_err("set alt %d err %d\n", alt, ret);
					goto out;
				}
			}
		}
		if (!gspca_dev->cam.no_urb_create) {
			PDEBUG(D_STREAM, "init transfer alt %d", alt);
			ret = create_urbs(gspca_dev,
				alt_xfer(&intf->altsetting[alt], xfer));
			if (ret < 0) {
				destroy_urbs(gspca_dev);
				goto out;
			}
		}

		/* clear the bulk endpoint */
		if (gspca_dev->cam.bulk)
			usb_clear_halt(gspca_dev->dev,
					gspca_dev->urb[0]->pipe);

		/* start the cam */
		ret = gspca_dev->sd_desc->start(gspca_dev);
		if (ret < 0) {
			destroy_urbs(gspca_dev);
			goto out;
		}
		gspca_dev->streaming = 1;

		/* some bulk transfers are started by the subdriver */
		if (gspca_dev->cam.bulk && gspca_dev->cam.bulk_nurbs == 0)
			break;

		/* submit the URBs */
		for (n = 0; n < MAX_NURBS; n++) {
			urb = gspca_dev->urb[n];
			if (urb == NULL)
				break;
			ret = usb_submit_urb(urb, GFP_KERNEL);
			if (ret < 0)
				break;
		}
		if (ret >= 0)
			break;			/* transfer is started */

		/* something when wrong
		 * stop the webcam and free the transfer resources */
		gspca_stream_off(gspca_dev);
		if (ret != -ENOSPC) {
			pr_err("usb_submit_urb alt %d err %d\n",
			       gspca_dev->alt, ret);
			goto out;
		}

		/* the bandwidth is not wide enough
		 * negotiate or try a lower alternate setting */
retry:
		PDEBUG(D_ERR|D_STREAM,
			"alt %d - bandwidth not wide enough - trying again",
			alt);
		msleep(20);	/* wait for kill complete */
		if (gspca_dev->sd_desc->isoc_nego) {
			ret = gspca_dev->sd_desc->isoc_nego(gspca_dev);
			if (ret < 0)
				goto out;
		} else {
			if (alt_idx <= 0) {
				pr_err("no transfer endpoint found\n");
				ret = -EIO;
				goto out;
			}
			gspca_dev->alt = ep_tb[--alt_idx].alt;
		}
	}
out:
	gspca_input_create_urb(gspca_dev);
	return ret;
}

static void gspca_set_default_mode(struct gspca_dev *gspca_dev)
{
	struct gspca_ctrl *ctrl;
	int i;

	i = gspca_dev->cam.nmodes - 1;	/* take the highest mode */
	gspca_dev->curr_mode = i;
	gspca_dev->width = gspca_dev->cam.cam_mode[i].width;
	gspca_dev->height = gspca_dev->cam.cam_mode[i].height;
	gspca_dev->pixfmt = gspca_dev->cam.cam_mode[i].pixelformat;

	/* set the current control values to their default values
	 * which may have changed in sd_init() */
	/* does nothing if ctrl_handler == NULL */
	v4l2_ctrl_handler_setup(gspca_dev->vdev.ctrl_handler);
	ctrl = gspca_dev->cam.ctrls;
	if (ctrl != NULL) {
		for (i = 0;
		     i < gspca_dev->sd_desc->nctrls;
		     i++, ctrl++)
			ctrl->val = ctrl->def;
	}
}

static int wxh_to_mode(struct gspca_dev *gspca_dev,
			int width, int height)
{
	int i;

	for (i = gspca_dev->cam.nmodes; --i > 0; ) {
		if (width >= gspca_dev->cam.cam_mode[i].width
		    && height >= gspca_dev->cam.cam_mode[i].height)
			break;
	}
	return i;
}

/*
 * search a mode with the right pixel format
 */
static int gspca_get_mode(struct gspca_dev *gspca_dev,
			int mode,
			int pixfmt)
{
	int modeU, modeD;

	modeU = modeD = mode;
	while ((modeU < gspca_dev->cam.nmodes) || modeD >= 0) {
		if (--modeD >= 0) {
			if (gspca_dev->cam.cam_mode[modeD].pixelformat
								== pixfmt)
				return modeD;
		}
		if (++modeU < gspca_dev->cam.nmodes) {
			if (gspca_dev->cam.cam_mode[modeU].pixelformat
								== pixfmt)
				return modeU;
		}
	}
	return -EINVAL;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *priv,
			struct v4l2_dbg_register *reg)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (!gspca_dev->sd_desc->get_chip_ident)
		return -ENOTTY;

	if (!gspca_dev->sd_desc->get_register)
		return -ENOTTY;

	gspca_dev->usb_err = 0;
	return gspca_dev->sd_desc->get_register(gspca_dev, reg);
}

static int vidioc_s_register(struct file *file, void *priv,
			struct v4l2_dbg_register *reg)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (!gspca_dev->sd_desc->get_chip_ident)
		return -ENOTTY;

	if (!gspca_dev->sd_desc->set_register)
		return -ENOTTY;

	gspca_dev->usb_err = 0;
	return gspca_dev->sd_desc->set_register(gspca_dev, reg);
}
#endif

static int vidioc_g_chip_ident(struct file *file, void *priv,
			struct v4l2_dbg_chip_ident *chip)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (!gspca_dev->sd_desc->get_chip_ident)
		return -ENOTTY;

	gspca_dev->usb_err = 0;
	return gspca_dev->sd_desc->get_chip_ident(gspca_dev, chip);
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
				struct v4l2_fmtdesc *fmtdesc)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int i, j, index;
	__u32 fmt_tb[8];

	/* give an index to each format */
	index = 0;
	j = 0;
	for (i = gspca_dev->cam.nmodes; --i >= 0; ) {
		fmt_tb[index] = gspca_dev->cam.cam_mode[i].pixelformat;
		j = 0;
		for (;;) {
			if (fmt_tb[j] == fmt_tb[index])
				break;
			j++;
		}
		if (j == index) {
			if (fmtdesc->index == index)
				break;		/* new format */
			index++;
			if (index >= ARRAY_SIZE(fmt_tb))
				return -EINVAL;
		}
	}
	if (i < 0)
		return -EINVAL;		/* no more format */

	fmtdesc->pixelformat = fmt_tb[index];
	if (gspca_dev->cam.cam_mode[i].sizeimage <
			gspca_dev->cam.cam_mode[i].width *
				gspca_dev->cam.cam_mode[i].height)
		fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
	fmtdesc->description[0] = fmtdesc->pixelformat & 0xff;
	fmtdesc->description[1] = (fmtdesc->pixelformat >> 8) & 0xff;
	fmtdesc->description[2] = (fmtdesc->pixelformat >> 16) & 0xff;
	fmtdesc->description[3] = fmtdesc->pixelformat >> 24;
	fmtdesc->description[4] = '\0';
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int mode;

	mode = gspca_dev->curr_mode;
	memcpy(&fmt->fmt.pix, &gspca_dev->cam.cam_mode[mode],
		sizeof fmt->fmt.pix);
	return 0;
}

static int try_fmt_vid_cap(struct gspca_dev *gspca_dev,
			struct v4l2_format *fmt)
{
	int w, h, mode, mode2;

	w = fmt->fmt.pix.width;
	h = fmt->fmt.pix.height;

#ifdef GSPCA_DEBUG
	if (gspca_debug & D_CONF)
		PDEBUG_MODE("try fmt cap", fmt->fmt.pix.pixelformat, w, h);
#endif
	/* search the closest mode for width and height */
	mode = wxh_to_mode(gspca_dev, w, h);

	/* OK if right palette */
	if (gspca_dev->cam.cam_mode[mode].pixelformat
						!= fmt->fmt.pix.pixelformat) {

		/* else, search the closest mode with the same pixel format */
		mode2 = gspca_get_mode(gspca_dev, mode,
					fmt->fmt.pix.pixelformat);
		if (mode2 >= 0)
			mode = mode2;
/*		else
			;		 * no chance, return this mode */
	}
	memcpy(&fmt->fmt.pix, &gspca_dev->cam.cam_mode[mode],
		sizeof fmt->fmt.pix);
	return mode;			/* used when s_fmt */
}

static int vidioc_try_fmt_vid_cap(struct file *file,
			      void *priv,
			      struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int ret;

	ret = try_fmt_vid_cap(gspca_dev, fmt);
	if (ret < 0)
		return ret;
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	ret = try_fmt_vid_cap(gspca_dev, fmt);
	if (ret < 0)
		goto out;

	if (gspca_dev->nframes != 0
	    && fmt->fmt.pix.sizeimage > gspca_dev->frsz) {
		ret = -EINVAL;
		goto out;
	}

	if (ret == gspca_dev->curr_mode) {
		ret = 0;
		goto out;			/* same mode */
	}

	if (gspca_dev->streaming) {
		ret = -EBUSY;
		goto out;
	}
	gspca_dev->width = fmt->fmt.pix.width;
	gspca_dev->height = fmt->fmt.pix.height;
	gspca_dev->pixfmt = fmt->fmt.pix.pixelformat;
	gspca_dev->curr_mode = ret;

	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int i;
	__u32 index = 0;

	for (i = 0; i < gspca_dev->cam.nmodes; i++) {
		if (fsize->pixel_format !=
				gspca_dev->cam.cam_mode[i].pixelformat)
			continue;

		if (fsize->index == index) {
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fsize->discrete.width =
				gspca_dev->cam.cam_mode[i].width;
			fsize->discrete.height =
				gspca_dev->cam.cam_mode[i].height;
			return 0;
		}
		index++;
	}

	return -EINVAL;
}

static int vidioc_enum_frameintervals(struct file *filp, void *priv,
				      struct v4l2_frmivalenum *fival)
{
	struct gspca_dev *gspca_dev = video_drvdata(filp);
	int mode = wxh_to_mode(gspca_dev, fival->width, fival->height);
	__u32 i;

	if (gspca_dev->cam.mode_framerates == NULL ||
			gspca_dev->cam.mode_framerates[mode].nrates == 0)
		return -EINVAL;

	if (fival->pixel_format !=
			gspca_dev->cam.cam_mode[mode].pixelformat)
		return -EINVAL;

	for (i = 0; i < gspca_dev->cam.mode_framerates[mode].nrates; i++) {
		if (fival->index == i) {
			fival->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fival->discrete.numerator = 1;
			fival->discrete.denominator =
				gspca_dev->cam.mode_framerates[mode].rates[i];
			return 0;
		}
	}

	return -EINVAL;
}

static void gspca_release(struct v4l2_device *v4l2_device)
{
	struct gspca_dev *gspca_dev =
		container_of(v4l2_device, struct gspca_dev, v4l2_dev);

	PDEBUG(D_PROBE, "%s released",
		video_device_node_name(&gspca_dev->vdev));

	v4l2_ctrl_handler_free(gspca_dev->vdev.ctrl_handler);
	v4l2_device_unregister(&gspca_dev->v4l2_dev);
	kfree(gspca_dev->usb_buf);
	kfree(gspca_dev);
}

static int dev_open(struct file *file)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	PDEBUG(D_STREAM, "[%s] open", current->comm);

	/* protect the subdriver against rmmod */
	if (!try_module_get(gspca_dev->module))
		return -ENODEV;

#ifdef GSPCA_DEBUG
	/* activate the v4l2 debug */
	if (gspca_debug & D_V4L2)
		gspca_dev->vdev.debug |= V4L2_DEBUG_IOCTL
					| V4L2_DEBUG_IOCTL_ARG;
	else
		gspca_dev->vdev.debug &= ~(V4L2_DEBUG_IOCTL
					| V4L2_DEBUG_IOCTL_ARG);
#endif
	return v4l2_fh_open(file);
}

static int dev_close(struct file *file)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	PDEBUG(D_STREAM, "[%s] close", current->comm);

	/* Needed for gspca_stream_off, always lock before queue_lock! */
	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;

	if (mutex_lock_interruptible(&gspca_dev->queue_lock)) {
		mutex_unlock(&gspca_dev->usb_lock);
		return -ERESTARTSYS;
	}

	/* if the file did the capture, free the streaming resources */
	if (gspca_dev->capt_file == file) {
		if (gspca_dev->streaming)
			gspca_stream_off(gspca_dev);
		frame_free(gspca_dev);
	}
	module_put(gspca_dev->module);
	mutex_unlock(&gspca_dev->queue_lock);
	mutex_unlock(&gspca_dev->usb_lock);

	PDEBUG(D_STREAM, "close done");

	return v4l2_fh_release(file);
}

static int vidioc_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	strlcpy((char *) cap->driver, gspca_dev->sd_desc->name,
			sizeof cap->driver);
	if (gspca_dev->dev->product != NULL) {
		strlcpy((char *) cap->card, gspca_dev->dev->product,
			sizeof cap->card);
	} else {
		snprintf((char *) cap->card, sizeof cap->card,
			"USB Camera (%04x:%04x)",
			le16_to_cpu(gspca_dev->dev->descriptor.idVendor),
			le16_to_cpu(gspca_dev->dev->descriptor.idProduct));
	}
	usb_make_path(gspca_dev->dev, (char *) cap->bus_info,
			sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE
			  | V4L2_CAP_STREAMING
			  | V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int get_ctrl(struct gspca_dev *gspca_dev,
				   int id)
{
	const struct ctrl *ctrls;
	int i;

	for (i = 0, ctrls = gspca_dev->sd_desc->ctrls;
	     i < gspca_dev->sd_desc->nctrls;
	     i++, ctrls++) {
		if (gspca_dev->ctrl_dis & (1 << i))
			continue;
		if (id == ctrls->qctrl.id)
			return i;
	}
	return -1;
}

static int vidioc_queryctrl(struct file *file, void *priv,
			   struct v4l2_queryctrl *q_ctrl)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	const struct ctrl *ctrls;
	struct gspca_ctrl *gspca_ctrl;
	int i, idx;
	u32 id;

	id = q_ctrl->id;
	if (id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		id &= V4L2_CTRL_ID_MASK;
		id++;
		idx = -1;
		for (i = 0; i < gspca_dev->sd_desc->nctrls; i++) {
			if (gspca_dev->ctrl_dis & (1 << i))
				continue;
			if (gspca_dev->sd_desc->ctrls[i].qctrl.id < id)
				continue;
			if (idx >= 0
			 && gspca_dev->sd_desc->ctrls[i].qctrl.id
				    > gspca_dev->sd_desc->ctrls[idx].qctrl.id)
				continue;
			idx = i;
		}
	} else {
		idx = get_ctrl(gspca_dev, id);
	}
	if (idx < 0)
		return -EINVAL;
	ctrls = &gspca_dev->sd_desc->ctrls[idx];
	memcpy(q_ctrl, &ctrls->qctrl, sizeof *q_ctrl);
	if (gspca_dev->cam.ctrls != NULL) {
		gspca_ctrl = &gspca_dev->cam.ctrls[idx];
		q_ctrl->default_value = gspca_ctrl->def;
		q_ctrl->minimum = gspca_ctrl->min;
		q_ctrl->maximum = gspca_ctrl->max;
	}
	if (gspca_dev->ctrl_inac & (1 << idx))
		q_ctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	const struct ctrl *ctrls;
	struct gspca_ctrl *gspca_ctrl;
	int idx;

	idx = get_ctrl(gspca_dev, ctrl->id);
	if (idx < 0)
		return -EINVAL;
	if (gspca_dev->ctrl_inac & (1 << idx))
		return -EINVAL;
	ctrls = &gspca_dev->sd_desc->ctrls[idx];
	if (gspca_dev->cam.ctrls != NULL) {
		gspca_ctrl = &gspca_dev->cam.ctrls[idx];
		if (ctrl->value < gspca_ctrl->min
		    || ctrl->value > gspca_ctrl->max)
			return -ERANGE;
	} else {
		gspca_ctrl = NULL;
		if (ctrl->value < ctrls->qctrl.minimum
		    || ctrl->value > ctrls->qctrl.maximum)
			return -ERANGE;
	}
	PDEBUG(D_CONF, "set ctrl [%08x] = %d", ctrl->id, ctrl->value);
	gspca_dev->usb_err = 0;
	if (ctrls->set != NULL)
		return ctrls->set(gspca_dev, ctrl->value);
	if (gspca_ctrl != NULL) {
		gspca_ctrl->val = ctrl->value;
		if (ctrls->set_control != NULL
		 && gspca_dev->streaming)
			ctrls->set_control(gspca_dev);
	}
	return gspca_dev->usb_err;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	const struct ctrl *ctrls;
	int idx;

	idx = get_ctrl(gspca_dev, ctrl->id);
	if (idx < 0)
		return -EINVAL;
	ctrls = &gspca_dev->sd_desc->ctrls[idx];

	gspca_dev->usb_err = 0;
	if (ctrls->get != NULL)
		return ctrls->get(gspca_dev, &ctrl->value);
	if (gspca_dev->cam.ctrls != NULL)
		ctrl->value = gspca_dev->cam.ctrls[idx].val;
	return 0;
}

static int vidioc_querymenu(struct file *file, void *priv,
			    struct v4l2_querymenu *qmenu)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (!gspca_dev->sd_desc->querymenu)
		return -ENOTTY;
	return gspca_dev->sd_desc->querymenu(gspca_dev, qmenu);
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *input)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (input->index != 0)
		return -EINVAL;
	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->status = gspca_dev->cam.input_flags;
	strlcpy(input->name, gspca_dev->sd_desc->name,
		sizeof input->name);
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return (0);
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *rb)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int i, ret = 0, streaming;

	i = rb->memory;			/* (avoid compilation warning) */
	switch (i) {
	case GSPCA_MEMORY_READ:			/* (internal call) */
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_USERPTR:
		break;
	default:
		return -EINVAL;
	}
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	if (gspca_dev->memory != GSPCA_MEMORY_NO
	    && gspca_dev->memory != GSPCA_MEMORY_READ
	    && gspca_dev->memory != rb->memory) {
		ret = -EBUSY;
		goto out;
	}

	/* only one file may do the capture */
	if (gspca_dev->capt_file != NULL
	    && gspca_dev->capt_file != file) {
		ret = -EBUSY;
		goto out;
	}

	/* if allocated, the buffers must not be mapped */
	for (i = 0; i < gspca_dev->nframes; i++) {
		if (gspca_dev->frame[i].vma_use_count) {
			ret = -EBUSY;
			goto out;
		}
	}

	/* stop streaming */
	streaming = gspca_dev->streaming;
	if (streaming) {
		gspca_stream_off(gspca_dev);

		/* Don't restart the stream when switching from read
		 * to mmap mode */
		if (gspca_dev->memory == GSPCA_MEMORY_READ)
			streaming = 0;
	}

	/* free the previous allocated buffers, if any */
	if (gspca_dev->nframes != 0)
		frame_free(gspca_dev);
	if (rb->count == 0)			/* unrequest */
		goto out;
	ret = frame_alloc(gspca_dev, file, rb->memory, rb->count);
	if (ret == 0) {
		rb->count = gspca_dev->nframes;
		if (streaming)
			ret = gspca_init_transfer(gspca_dev);
	}
out:
	mutex_unlock(&gspca_dev->queue_lock);
	PDEBUG(D_STREAM, "reqbufs st:%d c:%d", ret, rb->count);
	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *v4l2_buf)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	struct gspca_frame *frame;

	if (v4l2_buf->index < 0
	    || v4l2_buf->index >= gspca_dev->nframes)
		return -EINVAL;

	frame = &gspca_dev->frame[v4l2_buf->index];
	memcpy(v4l2_buf, &frame->v4l2_buf, sizeof *v4l2_buf);
	return 0;
}

static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type buf_type)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int ret;

	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	/* check the capture file */
	if (gspca_dev->capt_file != file) {
		ret = -EBUSY;
		goto out;
	}

	if (gspca_dev->nframes == 0
	    || !(gspca_dev->frame[0].v4l2_buf.flags & V4L2_BUF_FLAG_QUEUED)) {
		ret = -EINVAL;
		goto out;
	}
	if (!gspca_dev->streaming) {
		ret = gspca_init_transfer(gspca_dev);
		if (ret < 0)
			goto out;
	}
#ifdef GSPCA_DEBUG
	if (gspca_debug & D_STREAM) {
		PDEBUG_MODE("stream on OK",
			gspca_dev->pixfmt,
			gspca_dev->width,
			gspca_dev->height);
	}
#endif
	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type buf_type)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	int ret;

	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	if (!gspca_dev->streaming) {
		ret = 0;
		goto out;
	}

	/* check the capture file */
	if (gspca_dev->capt_file != file) {
		ret = -EBUSY;
		goto out;
	}

	/* stop streaming */
	gspca_stream_off(gspca_dev);
	/* In case another thread is waiting in dqbuf */
	wake_up_interruptible(&gspca_dev->wq);

	/* empty the transfer queues */
	atomic_set(&gspca_dev->fr_q, 0);
	atomic_set(&gspca_dev->fr_i, 0);
	gspca_dev->fr_o = 0;
	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static int vidioc_g_jpegcomp(struct file *file, void *priv,
			struct v4l2_jpegcompression *jpegcomp)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (!gspca_dev->sd_desc->get_jcomp)
		return -ENOTTY;
	gspca_dev->usb_err = 0;
	return gspca_dev->sd_desc->get_jcomp(gspca_dev, jpegcomp);
}

static int vidioc_s_jpegcomp(struct file *file, void *priv,
			struct v4l2_jpegcompression *jpegcomp)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);

	if (!gspca_dev->sd_desc->set_jcomp)
		return -ENOTTY;
	gspca_dev->usb_err = 0;
	return gspca_dev->sd_desc->set_jcomp(gspca_dev, jpegcomp);
}

static int vidioc_g_parm(struct file *filp, void *priv,
			struct v4l2_streamparm *parm)
{
	struct gspca_dev *gspca_dev = video_drvdata(filp);

	parm->parm.capture.readbuffers = gspca_dev->nbufread;

	if (gspca_dev->sd_desc->get_streamparm) {
		gspca_dev->usb_err = 0;
		gspca_dev->sd_desc->get_streamparm(gspca_dev, parm);
		return gspca_dev->usb_err;
	}
	return 0;
}

static int vidioc_s_parm(struct file *filp, void *priv,
			struct v4l2_streamparm *parm)
{
	struct gspca_dev *gspca_dev = video_drvdata(filp);
	int n;

	n = parm->parm.capture.readbuffers;
	if (n == 0 || n >= GSPCA_MAX_FRAMES)
		parm->parm.capture.readbuffers = gspca_dev->nbufread;
	else
		gspca_dev->nbufread = n;

	if (gspca_dev->sd_desc->set_streamparm) {
		gspca_dev->usb_err = 0;
		gspca_dev->sd_desc->set_streamparm(gspca_dev, parm);
		return gspca_dev->usb_err;
	}

	return 0;
}

static int dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	struct gspca_frame *frame;
	struct page *page;
	unsigned long addr, start, size;
	int i, ret;

	start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;
	PDEBUG(D_STREAM, "mmap start:%08x size:%d", (int) start, (int) size);

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (gspca_dev->capt_file != file) {
		ret = -EINVAL;
		goto out;
	}

	frame = NULL;
	for (i = 0; i < gspca_dev->nframes; ++i) {
		if (gspca_dev->frame[i].v4l2_buf.memory != V4L2_MEMORY_MMAP) {
			PDEBUG(D_STREAM, "mmap bad memory type");
			break;
		}
		if ((gspca_dev->frame[i].v4l2_buf.m.offset >> PAGE_SHIFT)
						== vma->vm_pgoff) {
			frame = &gspca_dev->frame[i];
			break;
		}
	}
	if (frame == NULL) {
		PDEBUG(D_STREAM, "mmap no frame buffer found");
		ret = -EINVAL;
		goto out;
	}
	if (size != frame->v4l2_buf.length) {
		PDEBUG(D_STREAM, "mmap bad size");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * - VM_IO marks the area as being a mmaped region for I/O to a
	 *   device. It also prevents the region from being core dumped.
	 */
	vma->vm_flags |= VM_IO;

	addr = (unsigned long) frame->data;
	while (size > 0) {
		page = vmalloc_to_page((void *) addr);
		ret = vm_insert_page(vma, start, page);
		if (ret < 0)
			goto out;
		start += PAGE_SIZE;
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &gspca_vm_ops;
	vma->vm_private_data = frame;
	gspca_vm_open(vma);
	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static int frame_ready_nolock(struct gspca_dev *gspca_dev, struct file *file,
				enum v4l2_memory memory)
{
	if (!gspca_dev->present)
		return -ENODEV;
	if (gspca_dev->capt_file != file || gspca_dev->memory != memory ||
			!gspca_dev->streaming)
		return -EINVAL;

	/* check if a frame is ready */
	return gspca_dev->fr_o != atomic_read(&gspca_dev->fr_i);
}

static int frame_ready(struct gspca_dev *gspca_dev, struct file *file,
			enum v4l2_memory memory)
{
	int ret;

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	ret = frame_ready_nolock(gspca_dev, file, memory);
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

/*
 * dequeue a video buffer
 *
 * If nonblock_ing is false, block until a buffer is available.
 */
static int vidioc_dqbuf(struct file *file, void *priv,
			struct v4l2_buffer *v4l2_buf)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	struct gspca_frame *frame;
	int i, j, ret;

	PDEBUG(D_FRAM, "dqbuf");

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	for (;;) {
		ret = frame_ready_nolock(gspca_dev, file, v4l2_buf->memory);
		if (ret < 0)
			goto out;
		if (ret > 0)
			break;

		mutex_unlock(&gspca_dev->queue_lock);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* wait till a frame is ready */
		ret = wait_event_interruptible_timeout(gspca_dev->wq,
			frame_ready(gspca_dev, file, v4l2_buf->memory),
			msecs_to_jiffies(3000));
		if (ret < 0)
			return ret;
		if (ret == 0)
			return -EIO;

		if (mutex_lock_interruptible(&gspca_dev->queue_lock))
			return -ERESTARTSYS;
	}

	i = gspca_dev->fr_o;
	j = gspca_dev->fr_queue[i];
	frame = &gspca_dev->frame[j];

	gspca_dev->fr_o = (i + 1) % GSPCA_MAX_FRAMES;

	frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	memcpy(v4l2_buf, &frame->v4l2_buf, sizeof *v4l2_buf);
	PDEBUG(D_FRAM, "dqbuf %d", j);
	ret = 0;

	if (gspca_dev->memory == V4L2_MEMORY_USERPTR) {
		if (copy_to_user((__u8 __user *) frame->v4l2_buf.m.userptr,
				 frame->data,
				 frame->v4l2_buf.bytesused)) {
			PDEBUG(D_ERR|D_STREAM,
				"dqbuf cp to user failed");
			ret = -EFAULT;
		}
	}
out:
	mutex_unlock(&gspca_dev->queue_lock);

	if (ret == 0 && gspca_dev->sd_desc->dq_callback) {
		mutex_lock(&gspca_dev->usb_lock);
		gspca_dev->usb_err = 0;
		if (gspca_dev->present)
			gspca_dev->sd_desc->dq_callback(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
	}

	return ret;
}

/*
 * queue a video buffer
 *
 * Attempting to queue a buffer that has already been
 * queued will return -EINVAL.
 */
static int vidioc_qbuf(struct file *file, void *priv,
			struct v4l2_buffer *v4l2_buf)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	struct gspca_frame *frame;
	int i, index, ret;

	PDEBUG(D_FRAM, "qbuf %d", v4l2_buf->index);

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	index = v4l2_buf->index;
	if ((unsigned) index >= gspca_dev->nframes) {
		PDEBUG(D_FRAM,
			"qbuf idx %d >= %d", index, gspca_dev->nframes);
		ret = -EINVAL;
		goto out;
	}
	if (v4l2_buf->memory != gspca_dev->memory) {
		PDEBUG(D_FRAM, "qbuf bad memory type");
		ret = -EINVAL;
		goto out;
	}

	frame = &gspca_dev->frame[index];
	if (frame->v4l2_buf.flags & BUF_ALL_FLAGS) {
		PDEBUG(D_FRAM, "qbuf bad state");
		ret = -EINVAL;
		goto out;
	}

	frame->v4l2_buf.flags |= V4L2_BUF_FLAG_QUEUED;

	if (frame->v4l2_buf.memory == V4L2_MEMORY_USERPTR) {
		frame->v4l2_buf.m.userptr = v4l2_buf->m.userptr;
		frame->v4l2_buf.length = v4l2_buf->length;
	}

	/* put the buffer in the 'queued' queue */
	i = atomic_read(&gspca_dev->fr_q);
	gspca_dev->fr_queue[i] = index;
	atomic_set(&gspca_dev->fr_q, (i + 1) % GSPCA_MAX_FRAMES);

	v4l2_buf->flags |= V4L2_BUF_FLAG_QUEUED;
	v4l2_buf->flags &= ~V4L2_BUF_FLAG_DONE;
	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

/*
 * allocate the resources for read()
 */
static int read_alloc(struct gspca_dev *gspca_dev,
			struct file *file)
{
	struct v4l2_buffer v4l2_buf;
	int i, ret;

	PDEBUG(D_STREAM, "read alloc");

	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;

	if (gspca_dev->nframes == 0) {
		struct v4l2_requestbuffers rb;

		memset(&rb, 0, sizeof rb);
		rb.count = gspca_dev->nbufread;
		rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rb.memory = GSPCA_MEMORY_READ;
		ret = vidioc_reqbufs(file, gspca_dev, &rb);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read reqbuf err %d", ret);
			goto out;
		}
		memset(&v4l2_buf, 0, sizeof v4l2_buf);
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = GSPCA_MEMORY_READ;
		for (i = 0; i < gspca_dev->nbufread; i++) {
			v4l2_buf.index = i;
			ret = vidioc_qbuf(file, gspca_dev, &v4l2_buf);
			if (ret != 0) {
				PDEBUG(D_STREAM, "read qbuf err: %d", ret);
				goto out;
			}
		}
	}

	/* start streaming */
	ret = vidioc_streamon(file, gspca_dev, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret != 0)
		PDEBUG(D_STREAM, "read streamon err %d", ret);
out:
	mutex_unlock(&gspca_dev->usb_lock);
	return ret;
}

static unsigned int dev_poll(struct file *file, poll_table *wait)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	unsigned long req_events = poll_requested_events(wait);
	int ret = 0;

	PDEBUG(D_FRAM, "poll");

	if (req_events & POLLPRI)
		ret |= v4l2_ctrl_poll(file, wait);

	if (req_events & (POLLIN | POLLRDNORM)) {
		/* if reqbufs is not done, the user would use read() */
		if (gspca_dev->memory == GSPCA_MEMORY_NO) {
			if (read_alloc(gspca_dev, file) != 0) {
				ret |= POLLERR;
				goto out;
			}
		}

		poll_wait(file, &gspca_dev->wq, wait);

		/* check if an image has been received */
		if (mutex_lock_interruptible(&gspca_dev->queue_lock) != 0) {
			ret |= POLLERR;
			goto out;
		}
		if (gspca_dev->fr_o != atomic_read(&gspca_dev->fr_i))
			ret |= POLLIN | POLLRDNORM;
		mutex_unlock(&gspca_dev->queue_lock);
	}

out:
	if (!gspca_dev->present)
		ret |= POLLHUP;

	return ret;
}

static ssize_t dev_read(struct file *file, char __user *data,
		    size_t count, loff_t *ppos)
{
	struct gspca_dev *gspca_dev = video_drvdata(file);
	struct gspca_frame *frame;
	struct v4l2_buffer v4l2_buf;
	struct timeval timestamp;
	int n, ret, ret2;

	PDEBUG(D_FRAM, "read (%zd)", count);
	if (gspca_dev->memory == GSPCA_MEMORY_NO) { /* first time ? */
		ret = read_alloc(gspca_dev, file);
		if (ret != 0)
			return ret;
	}

	/* get a frame */
	timestamp = ktime_to_timeval(ktime_get());
	timestamp.tv_sec--;
	n = 2;
	for (;;) {
		memset(&v4l2_buf, 0, sizeof v4l2_buf);
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = GSPCA_MEMORY_READ;
		ret = vidioc_dqbuf(file, gspca_dev, &v4l2_buf);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read dqbuf err %d", ret);
			return ret;
		}

		/* if the process slept for more than 1 second,
		 * get a newer frame */
		frame = &gspca_dev->frame[v4l2_buf.index];
		if (--n < 0)
			break;			/* avoid infinite loop */
		if (frame->v4l2_buf.timestamp.tv_sec >= timestamp.tv_sec)
			break;
		ret = vidioc_qbuf(file, gspca_dev, &v4l2_buf);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read qbuf err %d", ret);
			return ret;
		}
	}

	/* copy the frame */
	if (count > frame->v4l2_buf.bytesused)
		count = frame->v4l2_buf.bytesused;
	ret = copy_to_user(data, frame->data, count);
	if (ret != 0) {
		PDEBUG(D_ERR|D_STREAM,
			"read cp to user lack %d / %zd", ret, count);
		ret = -EFAULT;
		goto out;
	}
	ret = count;
out:
	/* in each case, requeue the buffer */
	ret2 = vidioc_qbuf(file, gspca_dev, &v4l2_buf);
	if (ret2 != 0)
		return ret2;
	return ret;
}

static struct v4l2_file_operations dev_fops = {
	.owner = THIS_MODULE,
	.open = dev_open,
	.release = dev_close,
	.read = dev_read,
	.mmap = dev_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll	= dev_poll,
};

static const struct v4l2_ioctl_ops dev_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_dqbuf		= vidioc_dqbuf,
	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,
	.vidioc_streamon	= vidioc_streamon,
	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl,
	.vidioc_querymenu	= vidioc_querymenu,
	.vidioc_enum_input	= vidioc_enum_input,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,
	.vidioc_streamoff	= vidioc_streamoff,
	.vidioc_g_jpegcomp	= vidioc_g_jpegcomp,
	.vidioc_s_jpegcomp	= vidioc_s_jpegcomp,
	.vidioc_g_parm		= vidioc_g_parm,
	.vidioc_s_parm		= vidioc_s_parm,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = vidioc_enum_frameintervals,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register	= vidioc_g_register,
	.vidioc_s_register	= vidioc_s_register,
#endif
	.vidioc_g_chip_ident	= vidioc_g_chip_ident,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device gspca_template = {
	.name = "gspca main driver",
	.fops = &dev_fops,
	.ioctl_ops = &dev_ioctl_ops,
	.release = video_device_release_empty, /* We use v4l2_dev.release */
};

/* initialize the controls */
static void ctrls_init(struct gspca_dev *gspca_dev)
{
	struct gspca_ctrl *ctrl;
	int i;

	for (i = 0, ctrl = gspca_dev->cam.ctrls;
	     i < gspca_dev->sd_desc->nctrls;
	     i++, ctrl++) {
		ctrl->def = gspca_dev->sd_desc->ctrls[i].qctrl.default_value;
		ctrl->val = ctrl->def;
		ctrl->min = gspca_dev->sd_desc->ctrls[i].qctrl.minimum;
		ctrl->max = gspca_dev->sd_desc->ctrls[i].qctrl.maximum;
	}
}

/*
 * probe and create a new gspca device
 *
 * This function must be called by the sub-driver when it is
 * called for probing a new device.
 */
int gspca_dev_probe2(struct usb_interface *intf,
		const struct usb_device_id *id,
		const struct sd_desc *sd_desc,
		int dev_size,
		struct module *module)
{
	struct gspca_dev *gspca_dev;
	struct usb_device *dev = interface_to_usbdev(intf);
	int ret;

	pr_info("%s-" GSPCA_VERSION " probing %04x:%04x\n",
		sd_desc->name, id->idVendor, id->idProduct);

	/* create the device */
	if (dev_size < sizeof *gspca_dev)
		dev_size = sizeof *gspca_dev;
	gspca_dev = kzalloc(dev_size, GFP_KERNEL);
	if (!gspca_dev) {
		pr_err("couldn't kzalloc gspca struct\n");
		return -ENOMEM;
	}
	gspca_dev->usb_buf = kmalloc(USB_BUF_SZ, GFP_KERNEL);
	if (!gspca_dev->usb_buf) {
		pr_err("out of memory\n");
		ret = -ENOMEM;
		goto out;
	}
	gspca_dev->dev = dev;
	gspca_dev->iface = intf->cur_altsetting->desc.bInterfaceNumber;

	/* check if any audio device */
	if (dev->actconfig->desc.bNumInterfaces != 1) {
		int i;
		struct usb_interface *intf2;

		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			intf2 = dev->actconfig->interface[i];
			if (intf2 != NULL
			 && intf2->altsetting != NULL
			 && intf2->altsetting->desc.bInterfaceClass ==
					 USB_CLASS_AUDIO) {
				gspca_dev->audio = 1;
				break;
			}
		}
	}

	gspca_dev->v4l2_dev.release = gspca_release;
	ret = v4l2_device_register(&intf->dev, &gspca_dev->v4l2_dev);
	if (ret)
		goto out;
	gspca_dev->sd_desc = sd_desc;
	gspca_dev->nbufread = 2;
	gspca_dev->empty_packet = -1;	/* don't check the empty packets */
	gspca_dev->vdev = gspca_template;
	gspca_dev->vdev.v4l2_dev = &gspca_dev->v4l2_dev;
	video_set_drvdata(&gspca_dev->vdev, gspca_dev);
	set_bit(V4L2_FL_USE_FH_PRIO, &gspca_dev->vdev.flags);
	gspca_dev->module = module;
	gspca_dev->present = 1;

	mutex_init(&gspca_dev->usb_lock);
	gspca_dev->vdev.lock = &gspca_dev->usb_lock;
	mutex_init(&gspca_dev->queue_lock);
	init_waitqueue_head(&gspca_dev->wq);

	/* configure the subdriver and initialize the USB device */
	ret = sd_desc->config(gspca_dev, id);
	if (ret < 0)
		goto out;
	if (gspca_dev->cam.ctrls != NULL)
		ctrls_init(gspca_dev);
	ret = sd_desc->init(gspca_dev);
	if (ret < 0)
		goto out;
	if (sd_desc->init_controls)
		ret = sd_desc->init_controls(gspca_dev);
	if (ret < 0)
		goto out;
	gspca_set_default_mode(gspca_dev);

	ret = gspca_input_connect(gspca_dev);
	if (ret)
		goto out;

	/*
	 * Don't take usb_lock for these ioctls. This improves latency if
	 * usb_lock is taken for a long time, e.g. when changing a control
	 * value, and a new frame is ready to be dequeued.
	 */
	v4l2_disable_ioctl_locking(&gspca_dev->vdev, VIDIOC_DQBUF);
	v4l2_disable_ioctl_locking(&gspca_dev->vdev, VIDIOC_QBUF);
	v4l2_disable_ioctl_locking(&gspca_dev->vdev, VIDIOC_QUERYBUF);

	/* init video stuff */
	ret = video_register_device(&gspca_dev->vdev,
				  VFL_TYPE_GRABBER,
				  -1);
	if (ret < 0) {
		pr_err("video_register_device err %d\n", ret);
		goto out;
	}

	usb_set_intfdata(intf, gspca_dev);
	PDEBUG(D_PROBE, "%s created", video_device_node_name(&gspca_dev->vdev));

	gspca_input_create_urb(gspca_dev);

	return 0;
out:
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	if (gspca_dev->input_dev)
		input_unregister_device(gspca_dev->input_dev);
#endif
	v4l2_ctrl_handler_free(gspca_dev->vdev.ctrl_handler);
	kfree(gspca_dev->usb_buf);
	kfree(gspca_dev);
	return ret;
}
EXPORT_SYMBOL(gspca_dev_probe2);

/* same function as the previous one, but check the interface */
int gspca_dev_probe(struct usb_interface *intf,
		const struct usb_device_id *id,
		const struct sd_desc *sd_desc,
		int dev_size,
		struct module *module)
{
	struct usb_device *dev = interface_to_usbdev(intf);

	/* we don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1) {
		pr_err("%04x:%04x too many config\n",
		       id->idVendor, id->idProduct);
		return -ENODEV;
	}

	/* the USB video interface must be the first one */
	if (dev->actconfig->desc.bNumInterfaces != 1
	 && intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	return gspca_dev_probe2(intf, id, sd_desc, dev_size, module);
}
EXPORT_SYMBOL(gspca_dev_probe);

/*
 * USB disconnection
 *
 * This function must be called by the sub-driver
 * when the device disconnects, after the specific resources are freed.
 */
void gspca_disconnect(struct usb_interface *intf)
{
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	struct input_dev *input_dev;
#endif

	PDEBUG(D_PROBE, "%s disconnect",
		video_device_node_name(&gspca_dev->vdev));

	mutex_lock(&gspca_dev->usb_lock);

	usb_set_intfdata(intf, NULL);
	gspca_dev->dev = NULL;
	gspca_dev->present = 0;
	destroy_urbs(gspca_dev);

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	gspca_input_destroy_urb(gspca_dev);
	input_dev = gspca_dev->input_dev;
	if (input_dev) {
		gspca_dev->input_dev = NULL;
		input_unregister_device(input_dev);
	}
#endif
	/* Free subdriver's streaming resources / stop sd workqueue(s) */
	if (gspca_dev->sd_desc->stop0 && gspca_dev->streaming)
		gspca_dev->sd_desc->stop0(gspca_dev);
	gspca_dev->streaming = 0;
	wake_up_interruptible(&gspca_dev->wq);

	v4l2_device_disconnect(&gspca_dev->v4l2_dev);
	video_unregister_device(&gspca_dev->vdev);

	mutex_unlock(&gspca_dev->usb_lock);

	/* (this will call gspca_release() immediately or on last close) */
	v4l2_device_put(&gspca_dev->v4l2_dev);
}
EXPORT_SYMBOL(gspca_disconnect);

#ifdef CONFIG_PM
int gspca_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);

	if (!gspca_dev->streaming)
		return 0;
	mutex_lock(&gspca_dev->usb_lock);
	gspca_dev->frozen = 1;		/* avoid urb error messages */
	gspca_dev->usb_err = 0;
	if (gspca_dev->sd_desc->stopN)
		gspca_dev->sd_desc->stopN(gspca_dev);
	destroy_urbs(gspca_dev);
	gspca_input_destroy_urb(gspca_dev);
	gspca_set_alt0(gspca_dev);
	if (gspca_dev->sd_desc->stop0)
		gspca_dev->sd_desc->stop0(gspca_dev);
	mutex_unlock(&gspca_dev->usb_lock);
	return 0;
}
EXPORT_SYMBOL(gspca_suspend);

int gspca_resume(struct usb_interface *intf)
{
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);
	int streaming, ret = 0;

	mutex_lock(&gspca_dev->usb_lock);
	gspca_dev->frozen = 0;
	gspca_dev->usb_err = 0;
	gspca_dev->sd_desc->init(gspca_dev);
	gspca_input_create_urb(gspca_dev);
	/*
	 * Most subdrivers send all ctrl values on sd_start and thus
	 * only write to the device registers on s_ctrl when streaming ->
	 * Clear streaming to avoid setting all ctrls twice.
	 */
	streaming = gspca_dev->streaming;
	gspca_dev->streaming = 0;
	v4l2_ctrl_handler_setup(gspca_dev->vdev.ctrl_handler);
	if (streaming)
		ret = gspca_init_transfer(gspca_dev);
	mutex_unlock(&gspca_dev->usb_lock);
	return ret;
}
EXPORT_SYMBOL(gspca_resume);
#endif

/* -- module insert / remove -- */
static int __init gspca_init(void)
{
	pr_info("v" GSPCA_VERSION " registered\n");
	return 0;
}
static void __exit gspca_exit(void)
{
}

module_init(gspca_init);
module_exit(gspca_exit);

#ifdef GSPCA_DEBUG
module_param_named(debug, gspca_debug, int, 0644);
MODULE_PARM_DESC(debug,
		"Debug (bit) 0x01:error 0x02:probe 0x04:config"
		" 0x08:stream 0x10:frame 0x20:packet"
		" 0x0100: v4l2");
#endif
