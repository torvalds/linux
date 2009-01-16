/*
 * Main USB camera driver
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
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

#define MODULE_NAME "gspca"

#include <linux/init.h>
#include <linux/version.h>
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
#include <linux/jiffies.h>
#include <media/v4l2-ioctl.h>

#include "gspca.h"

/* global values */
#define DEF_NURBS 2		/* default number of URBs */

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("GSPCA USB Camera Driver");
MODULE_LICENSE("GPL");

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(2, 4, 0)

static int video_nr = -1;

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

/* specific memory types - !! should different from V4L2_MEMORY_xxx */
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

static struct vm_operations_struct gspca_vm_ops = {
	.open		= gspca_vm_open,
	.close		= gspca_vm_close,
};

/* get the current input frame buffer */
struct gspca_frame *gspca_get_i_frame(struct gspca_dev *gspca_dev)
{
	struct gspca_frame *frame;
	int i;

	i = gspca_dev->fr_i;
	i = gspca_dev->fr_queue[i];
	frame = &gspca_dev->frame[i];
	if ((frame->v4l2_buf.flags & BUF_ALL_FLAGS)
				!= V4L2_BUF_FLAG_QUEUED)
		return NULL;
	return frame;
}
EXPORT_SYMBOL(gspca_get_i_frame);

/*
 * fill a video frame from an URB and resubmit
 */
static void fill_frame(struct gspca_dev *gspca_dev,
			struct urb *urb)
{
	struct gspca_frame *frame;
	__u8 *data;		/* address of data in the iso message */
	int i, len, st;
	cam_pkt_op pkt_scan;

	if (urb->status != 0) {
#ifdef CONFIG_PM
		if (!gspca_dev->frozen)
#endif
			PDEBUG(D_ERR|D_PACK, "urb status: %d", urb->status);
		return;		/* disconnection ? */
	}
	pkt_scan = gspca_dev->sd_desc->pkt_scan;
	for (i = 0; i < urb->number_of_packets; i++) {

		/* check the availability of the frame buffer */
		frame = gspca_get_i_frame(gspca_dev);
		if (!frame) {
			gspca_dev->last_packet_type = DISCARD_PACKET;
			break;
		}

		/* check the packet status and length */
		len = urb->iso_frame_desc[i].actual_length;
		if (len == 0) {
			if (gspca_dev->empty_packet == 0)
				gspca_dev->empty_packet = 1;
			continue;
		}
		st = urb->iso_frame_desc[i].status;
		if (st) {
			PDEBUG(D_ERR,
				"ISOC data error: [%d] len=%d, status=%d",
				i, len, st);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}

		/* let the packet be analyzed by the subdriver */
		PDEBUG(D_PACK, "packet [%d] o:%d l:%d",
			i, urb->iso_frame_desc[i].offset, len);
		data = (__u8 *) urb->transfer_buffer
					+ urb->iso_frame_desc[i].offset;
		pkt_scan(gspca_dev, frame, data, len);
	}

	/* resubmit the URB */
	st = usb_submit_urb(urb, GFP_ATOMIC);
	if (st < 0)
		PDEBUG(D_ERR|D_PACK, "usb_submit_urb() ret %d", st);
}

/*
 * ISOC message interrupt from the USB device
 *
 * Analyse each packet and call the subdriver for copy to the frame buffer.
 */
static void isoc_irq(struct urb *urb
)
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
static void bulk_irq(struct urb *urb
)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	struct gspca_frame *frame;
	int st;

	PDEBUG(D_PACK, "bulk irq");
	if (!gspca_dev->streaming)
		return;
	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
		urb->status = 0;
		break;
	default:
#ifdef CONFIG_PM
		if (!gspca_dev->frozen)
#endif
			PDEBUG(D_ERR|D_PACK, "urb status: %d", urb->status);
		return;		/* disconnection ? */
	}

	/* check the availability of the frame buffer */
	frame = gspca_get_i_frame(gspca_dev);
	if (!frame) {
		gspca_dev->last_packet_type = DISCARD_PACKET;
	} else {
		PDEBUG(D_PACK, "packet l:%d", urb->actual_length);
		gspca_dev->sd_desc->pkt_scan(gspca_dev,
					frame,
					urb->transfer_buffer,
					urb->actual_length);
	}

	/* resubmit the URB */
	if (gspca_dev->cam.bulk_nurbs != 0) {
		st = usb_submit_urb(urb, GFP_ATOMIC);
		if (st < 0)
			PDEBUG(D_ERR|D_PACK, "usb_submit_urb() ret %d", st);
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
 * On LAST_PACKET, a new frame is returned.
 */
struct gspca_frame *gspca_frame_add(struct gspca_dev *gspca_dev,
				    enum gspca_packet_type packet_type,
				    struct gspca_frame *frame,
				    const __u8 *data,
				    int len)
{
	int i, j;

	PDEBUG(D_PACK, "add t:%d l:%d",	packet_type, len);

	/* when start of a new frame, if the current frame buffer
	 * is not queued, discard the whole frame */
	if (packet_type == FIRST_PACKET) {
		if ((frame->v4l2_buf.flags & BUF_ALL_FLAGS)
						!= V4L2_BUF_FLAG_QUEUED) {
			gspca_dev->last_packet_type = DISCARD_PACKET;
			return frame;
		}
		frame->data_end = frame->data;
		jiffies_to_timeval(get_jiffies_64(),
				   &frame->v4l2_buf.timestamp);
		frame->v4l2_buf.sequence = ++gspca_dev->sequence;
	} else if (gspca_dev->last_packet_type == DISCARD_PACKET) {
		if (packet_type == LAST_PACKET)
			gspca_dev->last_packet_type = packet_type;
		return frame;
	}

	/* append the packet to the frame buffer */
	if (len > 0) {
		if (frame->data_end - frame->data + len
						 > frame->v4l2_buf.length) {
			PDEBUG(D_ERR|D_PACK, "frame overflow %zd > %d",
				frame->data_end - frame->data + len,
				frame->v4l2_buf.length);
			packet_type = DISCARD_PACKET;
		} else {
			memcpy(frame->data_end, data, len);
			frame->data_end += len;
		}
	}
	gspca_dev->last_packet_type = packet_type;

	/* if last packet, wake up the application and advance in the queue */
	if (packet_type == LAST_PACKET) {
		frame->v4l2_buf.bytesused = frame->data_end - frame->data;
		frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_QUEUED;
		frame->v4l2_buf.flags |= V4L2_BUF_FLAG_DONE;
		wake_up_interruptible(&gspca_dev->wq);	/* event = new frame */
		i = (gspca_dev->fr_i + 1) % gspca_dev->nframes;
		gspca_dev->fr_i = i;
		PDEBUG(D_FRAM, "frame complete len:%d q:%d i:%d o:%d",
			frame->v4l2_buf.bytesused,
			gspca_dev->fr_q,
			i,
			gspca_dev->fr_o);
		j = gspca_dev->fr_queue[i];
		frame = &gspca_dev->frame[j];
	}
	return frame;
}
EXPORT_SYMBOL(gspca_frame_add);

static int gspca_is_compressed(__u32 format)
{
	switch (format) {
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_JPEG:
	case V4L2_PIX_FMT_SPCA561:
	case V4L2_PIX_FMT_PAC207:
		return 1;
	}
	return 0;
}

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	mem = vmalloc_32(size);
	if (mem != NULL) {
		adr = (unsigned long) mem;
		while ((long) size > 0) {
			SetPageReserved(vmalloc_to_page((void *) adr));
			adr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void *mem, long size)
{
	unsigned long adr;

	adr = (unsigned long) mem;
	while (size > 0) {
		ClearPageReserved(vmalloc_to_page((void *) adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static int frame_alloc(struct gspca_dev *gspca_dev,
			unsigned int count)
{
	struct gspca_frame *frame;
	unsigned int frsz;
	int i;

	i = gspca_dev->curr_mode;
	frsz = gspca_dev->cam.cam_mode[i].sizeimage;
	PDEBUG(D_STREAM, "frame alloc frsz: %d", frsz);
	frsz = PAGE_ALIGN(frsz);
	gspca_dev->frsz = frsz;
	if (count > GSPCA_MAX_FRAMES)
		count = GSPCA_MAX_FRAMES;
	gspca_dev->frbuf = rvmalloc(frsz * count);
	if (!gspca_dev->frbuf) {
		err("frame alloc failed");
		return -ENOMEM;
	}
	gspca_dev->nframes = count;
	for (i = 0; i < count; i++) {
		frame = &gspca_dev->frame[i];
		frame->v4l2_buf.index = i;
		frame->v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		frame->v4l2_buf.flags = 0;
		frame->v4l2_buf.field = V4L2_FIELD_NONE;
		frame->v4l2_buf.length = frsz;
		frame->v4l2_buf.memory = gspca_dev->memory;
		frame->v4l2_buf.sequence = 0;
		frame->data = frame->data_end =
					gspca_dev->frbuf + i * frsz;
		frame->v4l2_buf.m.offset = i * frsz;
	}
	gspca_dev->fr_i = gspca_dev->fr_o = gspca_dev->fr_q = 0;
	gspca_dev->last_packet_type = DISCARD_PACKET;
	gspca_dev->sequence = 0;
	return 0;
}

static void frame_free(struct gspca_dev *gspca_dev)
{
	int i;

	PDEBUG(D_STREAM, "frame free");
	if (gspca_dev->frbuf != NULL) {
		rvfree(gspca_dev->frbuf,
			gspca_dev->nframes * gspca_dev->frsz);
		gspca_dev->frbuf = NULL;
		for (i = 0; i < gspca_dev->nframes; i++)
			gspca_dev->frame[i].data = NULL;
	}
	gspca_dev->nframes = 0;
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
			usb_buffer_free(gspca_dev->dev,
					urb->transfer_buffer_length,
					urb->transfer_buffer,
					urb->transfer_dma);
		usb_free_urb(urb);
	}
}

/*
 * look for an input transfer endpoint in an alternate setting
 */
static struct usb_host_endpoint *alt_xfer(struct usb_host_interface *alt,
					  __u8 epaddr,
					  __u8 xfer)
{
	struct usb_host_endpoint *ep;
	int i, attr;

	epaddr |= USB_DIR_IN;
	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		ep = &alt->endpoint[i];
		if (ep->desc.bEndpointAddress == epaddr) {
			attr = ep->desc.bmAttributes
						& USB_ENDPOINT_XFERTYPE_MASK;
			if (attr == xfer)
				return ep;
			break;
		}
	}
	return NULL;
}

/*
 * look for an input (isoc or bulk) endpoint
 *
 * The endpoint is defined by the subdriver.
 * Use only the first isoc (some Zoran - 0x0572:0x0001 - have two such ep).
 * This routine may be called many times when the bandwidth is too small
 * (the bandwidth is checked on urb submit).
 */
static struct usb_host_endpoint *get_ep(struct gspca_dev *gspca_dev)
{
	struct usb_interface *intf;
	struct usb_host_endpoint *ep;
	int i, ret;

	intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
	ep = NULL;
	i = gspca_dev->alt;			/* previous alt setting */

	/* try isoc */
	while (--i > 0) {			/* alt 0 is unusable */
		ep = alt_xfer(&intf->altsetting[i],
				gspca_dev->cam.epaddr,
				USB_ENDPOINT_XFER_ISOC);
		if (ep)
			break;
	}

	/* if no isoc, try bulk */
	if (ep == NULL) {
		ep = alt_xfer(&intf->altsetting[0],
				gspca_dev->cam.epaddr,
				USB_ENDPOINT_XFER_BULK);
		if (ep == NULL) {
			err("no transfer endpoint found");
			return NULL;
		}
	}
	PDEBUG(D_STREAM, "use alt %d ep 0x%02x",
			i, ep->desc.bEndpointAddress);
	if (i > 0) {
		ret = usb_set_interface(gspca_dev->dev, gspca_dev->iface, i);
		if (ret < 0) {
			err("set interface err %d", ret);
			return NULL;
		}
	}
	gspca_dev->alt = i;		/* memorize the current alt setting */
	return ep;
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

	if (gspca_dev->alt != 0) {		/* isoc */

		/* See paragraph 5.9 / table 5-11 of the usb 2.0 spec. */
		psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
		npkt = ISO_MAX_SIZE / psize;
		if (npkt > ISO_MAX_PKT)
			npkt = ISO_MAX_PKT;
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

	gspca_dev->nurbs = nurbs;
	for (n = 0; n < nurbs; n++) {
		urb = usb_alloc_urb(npkt, GFP_KERNEL);
		if (!urb) {
			err("usb_alloc_urb failed");
			destroy_urbs(gspca_dev);
			return -ENOMEM;
		}
		urb->transfer_buffer = usb_buffer_alloc(gspca_dev->dev,
						bsize,
						GFP_KERNEL,
						&urb->transfer_dma);

		if (urb->transfer_buffer == NULL) {
			usb_free_urb(urb);
			err("usb_buffer_urb failed");
			destroy_urbs(gspca_dev);
			return -ENOMEM;
		}
		gspca_dev->urb[n] = urb;
		urb->dev = gspca_dev->dev;
		urb->context = gspca_dev;
		urb->transfer_buffer_length = bsize;
		if (npkt != 0) {		/* ISOC */
			urb->pipe = usb_rcvisocpipe(gspca_dev->dev,
						    ep->desc.bEndpointAddress);
			urb->transfer_flags = URB_ISO_ASAP
					| URB_NO_TRANSFER_DMA_MAP;
			urb->interval = ep->desc.bInterval;
			urb->complete = isoc_irq;
			urb->number_of_packets = npkt;
			for (i = 0; i < npkt; i++) {
				urb->iso_frame_desc[i].length = psize;
				urb->iso_frame_desc[i].offset = psize * i;
			}
		} else {		/* bulk */
			urb->pipe = usb_rcvbulkpipe(gspca_dev->dev,
						ep->desc.bEndpointAddress),
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
	struct usb_host_endpoint *ep;
	int n, ret;

	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;

	/* set the higher alternate setting and
	 * loop until urb submit succeeds */
	gspca_dev->alt = gspca_dev->nbalt;
	for (;;) {
		PDEBUG(D_STREAM, "init transfer alt %d", gspca_dev->alt);
		ep = get_ep(gspca_dev);
		if (ep == NULL) {
			ret = -EIO;
			goto out;
		}
		ret = create_urbs(gspca_dev, ep);
		if (ret < 0)
			goto out;

		/* clear the bulk endpoint */
		if (gspca_dev->alt == 0)	/* if bulk transfer */
			usb_clear_halt(gspca_dev->dev,
					usb_rcvintpipe(gspca_dev->dev,
						 gspca_dev->cam.epaddr));

		/* start the cam */
		ret = gspca_dev->sd_desc->start(gspca_dev);
		if (ret < 0) {
			destroy_urbs(gspca_dev);
			goto out;
		}
		gspca_dev->streaming = 1;

		/* some bulk transfers are started by the subdriver */
		if (gspca_dev->alt == 0 && gspca_dev->cam.bulk_nurbs == 0)
			break;

		/* submit the URBs */
		for (n = 0; n < gspca_dev->nurbs; n++) {
			ret = usb_submit_urb(gspca_dev->urb[n], GFP_KERNEL);
			if (ret < 0) {
				PDEBUG(D_ERR|D_STREAM,
					"usb_submit_urb [%d] err %d", n, ret);
				gspca_dev->streaming = 0;
				destroy_urbs(gspca_dev);
				if (ret == -ENOSPC) {
					msleep(20);	/* wait for kill
							 * complete */
					break;	/* try the previous alt */
				}
				goto out;
			}
		}
		if (ret >= 0)
			break;
	}
out:
	mutex_unlock(&gspca_dev->usb_lock);
	return ret;
}

static int gspca_set_alt0(struct gspca_dev *gspca_dev)
{
	int ret;

	ret = usb_set_interface(gspca_dev->dev, gspca_dev->iface, 0);
	if (ret < 0)
		PDEBUG(D_ERR|D_STREAM, "set alt 0 err %d", ret);
	return ret;
}

/* Note: both the queue and the usb locks should be held when calling this */
static void gspca_stream_off(struct gspca_dev *gspca_dev)
{
	gspca_dev->streaming = 0;
	if (gspca_dev->present
	    && gspca_dev->sd_desc->stopN)
		gspca_dev->sd_desc->stopN(gspca_dev);
	destroy_urbs(gspca_dev);
	gspca_set_alt0(gspca_dev);
	if (gspca_dev->sd_desc->stop0)
		gspca_dev->sd_desc->stop0(gspca_dev);
	PDEBUG(D_STREAM, "stream off OK");
}

static void gspca_set_default_mode(struct gspca_dev *gspca_dev)
{
	int i;

	i = gspca_dev->cam.nmodes - 1;	/* take the highest mode */
	gspca_dev->curr_mode = i;
	gspca_dev->width = gspca_dev->cam.cam_mode[i].width;
	gspca_dev->height = gspca_dev->cam.cam_mode[i].height;
	gspca_dev->pixfmt = gspca_dev->cam.cam_mode[i].pixelformat;
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

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
				struct v4l2_fmtdesc *fmtdesc)
{
	struct gspca_dev *gspca_dev = priv;
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
	if (gspca_is_compressed(fmt_tb[index]))
		fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
	fmtdesc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
	struct gspca_dev *gspca_dev = priv;
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
	struct gspca_dev *gspca_dev = priv;
	int ret;

	ret = try_fmt_vid_cap(gspca_dev, fmt);
	if (ret < 0)
		return ret;
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = priv;
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

static void gspca_release(struct video_device *vfd)
{
	struct gspca_dev *gspca_dev = container_of(vfd, struct gspca_dev, vdev);

	PDEBUG(D_STREAM, "device released");

	kfree(gspca_dev->usb_buf);
	kfree(gspca_dev);
}

static int dev_open(struct file *file)
{
	struct gspca_dev *gspca_dev;
	int ret;

	PDEBUG(D_STREAM, "%s open", current->comm);
	gspca_dev = (struct gspca_dev *) video_devdata(file);
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->present) {
		ret = -ENODEV;
		goto out;
	}

	if (gspca_dev->users > 4) {	/* (arbitrary value) */
		ret = -EBUSY;
		goto out;
	}

	/* protect the subdriver against rmmod */
	if (!try_module_get(gspca_dev->module)) {
		ret = -ENODEV;
		goto out;
	}

	gspca_dev->users++;

	file->private_data = gspca_dev;
#ifdef GSPCA_DEBUG
	/* activate the v4l2 debug */
	if (gspca_debug & D_V4L2)
		gspca_dev->vdev.debug |= V4L2_DEBUG_IOCTL
					| V4L2_DEBUG_IOCTL_ARG;
	else
		gspca_dev->vdev.debug &= ~(V4L2_DEBUG_IOCTL
					| V4L2_DEBUG_IOCTL_ARG);
#endif
	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	if (ret != 0)
		PDEBUG(D_ERR|D_STREAM, "open failed err %d", ret);
	else
		PDEBUG(D_STREAM, "open done");
	return ret;
}

static int dev_close(struct file *file)
{
	struct gspca_dev *gspca_dev = file->private_data;

	PDEBUG(D_STREAM, "%s close", current->comm);
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	gspca_dev->users--;

	/* if the file did the capture, free the streaming resources */
	if (gspca_dev->capt_file == file) {
		if (gspca_dev->streaming) {
			mutex_lock(&gspca_dev->usb_lock);
			gspca_stream_off(gspca_dev);
			mutex_unlock(&gspca_dev->usb_lock);
		}
		frame_free(gspca_dev);
		gspca_dev->capt_file = NULL;
		gspca_dev->memory = GSPCA_MEMORY_NO;
	}
	file->private_data = NULL;
	module_put(gspca_dev->module);
	mutex_unlock(&gspca_dev->queue_lock);

	PDEBUG(D_STREAM, "close done");

	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct gspca_dev *gspca_dev = priv;

	memset(cap, 0, sizeof *cap);
	strncpy(cap->driver, gspca_dev->sd_desc->name, sizeof cap->driver);
	if (gspca_dev->dev->product != NULL) {
		strncpy(cap->card, gspca_dev->dev->product,
			sizeof cap->card);
	} else {
		snprintf(cap->card, sizeof cap->card,
			"USB Camera (%04x:%04x)",
			le16_to_cpu(gspca_dev->dev->descriptor.idVendor),
			le16_to_cpu(gspca_dev->dev->descriptor.idProduct));
	}
	strncpy(cap->bus_info, gspca_dev->dev->bus->bus_name,
		sizeof cap->bus_info);
	cap->version = DRIVER_VERSION_NUMBER;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE
			  | V4L2_CAP_STREAMING
			  | V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
			   struct v4l2_queryctrl *q_ctrl)
{
	struct gspca_dev *gspca_dev = priv;
	int i, ix;
	u32 id;

	ix = -1;
	id = q_ctrl->id;
	if (id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		id &= V4L2_CTRL_ID_MASK;
		id++;
		for (i = 0; i < gspca_dev->sd_desc->nctrls; i++) {
			if (gspca_dev->sd_desc->ctrls[i].qctrl.id < id)
				continue;
			if (ix < 0) {
				ix = i;
				continue;
			}
			if (gspca_dev->sd_desc->ctrls[i].qctrl.id
				    > gspca_dev->sd_desc->ctrls[ix].qctrl.id)
				continue;
			ix = i;
		}
	}
	for (i = 0; i < gspca_dev->sd_desc->nctrls; i++) {
		if (id == gspca_dev->sd_desc->ctrls[i].qctrl.id) {
			ix = i;
			break;
		}
	}
	if (ix < 0)
		return -EINVAL;
	memcpy(q_ctrl, &gspca_dev->sd_desc->ctrls[ix].qctrl,
		sizeof *q_ctrl);
	if (gspca_dev->ctrl_dis & (1 << ix))
		q_ctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gspca_dev *gspca_dev = priv;
	const struct ctrl *ctrls;
	int i, ret;

	for (i = 0, ctrls = gspca_dev->sd_desc->ctrls;
	     i < gspca_dev->sd_desc->nctrls;
	     i++, ctrls++) {
		if (ctrl->id != ctrls->qctrl.id)
			continue;
		if (gspca_dev->ctrl_dis & (1 << i))
			return -EINVAL;
		if (ctrl->value < ctrls->qctrl.minimum
		    || ctrl->value > ctrls->qctrl.maximum)
			return -ERANGE;
		PDEBUG(D_CONF, "set ctrl [%08x] = %d", ctrl->id, ctrl->value);
		if (mutex_lock_interruptible(&gspca_dev->usb_lock))
			return -ERESTARTSYS;
		ret = ctrls->set(gspca_dev, ctrl->value);
		mutex_unlock(&gspca_dev->usb_lock);
		return ret;
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gspca_dev *gspca_dev = priv;

	const struct ctrl *ctrls;
	int i, ret;

	for (i = 0, ctrls = gspca_dev->sd_desc->ctrls;
	     i < gspca_dev->sd_desc->nctrls;
	     i++, ctrls++) {
		if (ctrl->id != ctrls->qctrl.id)
			continue;
		if (gspca_dev->ctrl_dis & (1 << i))
			return -EINVAL;
		if (mutex_lock_interruptible(&gspca_dev->usb_lock))
			return -ERESTARTSYS;
		ret = ctrls->get(gspca_dev, &ctrl->value);
		mutex_unlock(&gspca_dev->usb_lock);
		return ret;
	}
	return -EINVAL;
}

/*fixme: have an audio flag in gspca_dev?*/
static int vidioc_s_audio(struct file *file, void *priv,
			 struct v4l2_audio *audio)
{
	if (audio->index != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv,
			 struct v4l2_audio *audio)
{
	memset(audio, 0, sizeof *audio);
	strcpy(audio->name, "Microphone");
	return 0;
}

static int vidioc_enumaudio(struct file *file, void *priv,
			 struct v4l2_audio *audio)
{
	if (audio->index != 0)
		return -EINVAL;

	strcpy(audio->name, "Microphone");
	audio->capability = 0;
	audio->mode = 0;
	return 0;
}

static int vidioc_querymenu(struct file *file, void *priv,
			    struct v4l2_querymenu *qmenu)
{
	struct gspca_dev *gspca_dev = priv;

	if (!gspca_dev->sd_desc->querymenu)
		return -EINVAL;
	return gspca_dev->sd_desc->querymenu(gspca_dev, qmenu);
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *input)
{
	struct gspca_dev *gspca_dev = priv;

	if (input->index != 0)
		return -EINVAL;
	memset(input, 0, sizeof *input);
	input->type = V4L2_INPUT_TYPE_CAMERA;
	strncpy(input->name, gspca_dev->sd_desc->name,
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
	struct gspca_dev *gspca_dev = priv;
	int i, ret = 0;

	switch (rb->memory) {
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
	if (gspca_dev->streaming) {
		mutex_lock(&gspca_dev->usb_lock);
		gspca_stream_off(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
	}

	/* free the previous allocated buffers, if any */
	if (gspca_dev->nframes != 0) {
		frame_free(gspca_dev);
		gspca_dev->capt_file = NULL;
	}
	if (rb->count == 0)			/* unrequest */
		goto out;
	gspca_dev->memory = rb->memory;
	ret = frame_alloc(gspca_dev, rb->count);
	if (ret == 0) {
		rb->count = gspca_dev->nframes;
		gspca_dev->capt_file = file;
	}
out:
	mutex_unlock(&gspca_dev->queue_lock);
	PDEBUG(D_STREAM, "reqbufs st:%d c:%d", ret, rb->count);
	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *v4l2_buf)
{
	struct gspca_dev *gspca_dev = priv;
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
	struct gspca_dev *gspca_dev = priv;
	int ret;

	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->present) {
		ret = -ENODEV;
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
	struct gspca_dev *gspca_dev = priv;
	int i, ret;

	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (!gspca_dev->streaming)
		return 0;
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	/* stop streaming */
	if (mutex_lock_interruptible(&gspca_dev->usb_lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}
	gspca_stream_off(gspca_dev);
	mutex_unlock(&gspca_dev->usb_lock);

	/* empty the application queues */
	for (i = 0; i < gspca_dev->nframes; i++)
		gspca_dev->frame[i].v4l2_buf.flags &= ~BUF_ALL_FLAGS;
	gspca_dev->fr_i = gspca_dev->fr_o = gspca_dev->fr_q = 0;
	gspca_dev->last_packet_type = DISCARD_PACKET;
	gspca_dev->sequence = 0;
	ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static int vidioc_g_jpegcomp(struct file *file, void *priv,
			struct v4l2_jpegcompression *jpegcomp)
{
	struct gspca_dev *gspca_dev = priv;
	int ret;

	if (!gspca_dev->sd_desc->get_jcomp)
		return -EINVAL;
	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;
	ret = gspca_dev->sd_desc->get_jcomp(gspca_dev, jpegcomp);
	mutex_unlock(&gspca_dev->usb_lock);
	return ret;
}

static int vidioc_s_jpegcomp(struct file *file, void *priv,
			struct v4l2_jpegcompression *jpegcomp)
{
	struct gspca_dev *gspca_dev = priv;
	int ret;

	if (!gspca_dev->sd_desc->set_jcomp)
		return -EINVAL;
	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;
	ret = gspca_dev->sd_desc->set_jcomp(gspca_dev, jpegcomp);
	mutex_unlock(&gspca_dev->usb_lock);
	return ret;
}

static int vidioc_g_parm(struct file *filp, void *priv,
			struct v4l2_streamparm *parm)
{
	struct gspca_dev *gspca_dev = priv;

	memset(parm, 0, sizeof *parm);
	parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm->parm.capture.readbuffers = gspca_dev->nbufread;

	if (gspca_dev->sd_desc->get_streamparm) {
		int ret;

		if (mutex_lock_interruptible(&gspca_dev->usb_lock))
			return -ERESTARTSYS;
		ret = gspca_dev->sd_desc->get_streamparm(gspca_dev, parm);
		mutex_unlock(&gspca_dev->usb_lock);
		return ret;
	}

	return 0;
}

static int vidioc_s_parm(struct file *filp, void *priv,
			struct v4l2_streamparm *parm)
{
	struct gspca_dev *gspca_dev = priv;
	int n;

	n = parm->parm.capture.readbuffers;
	if (n == 0 || n > GSPCA_MAX_FRAMES)
		parm->parm.capture.readbuffers = gspca_dev->nbufread;
	else
		gspca_dev->nbufread = n;

	if (gspca_dev->sd_desc->set_streamparm) {
		int ret;

		if (mutex_lock_interruptible(&gspca_dev->usb_lock))
			return -ERESTARTSYS;
		ret = gspca_dev->sd_desc->set_streamparm(gspca_dev, parm);
		mutex_unlock(&gspca_dev->usb_lock);
		return ret;
	}

	return 0;
}

static int vidioc_s_std(struct file *filp, void *priv,
			v4l2_std_id *parm)
{
	return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv,
			struct video_mbuf *mbuf)
{
	struct gspca_dev *gspca_dev = file->private_data;
	int i;

	PDEBUG(D_STREAM, "cgmbuf");
	if (gspca_dev->nframes == 0) {
		int ret;

		{
			struct v4l2_format fmt;

			memset(&fmt, 0, sizeof fmt);
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			i = gspca_dev->cam.nmodes - 1;	/* highest mode */
			fmt.fmt.pix.width = gspca_dev->cam.cam_mode[i].width;
			fmt.fmt.pix.height = gspca_dev->cam.cam_mode[i].height;
			fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
			ret = vidioc_s_fmt_vid_cap(file, priv, &fmt);
			if (ret != 0)
				return ret;
		}
		{
			struct v4l2_requestbuffers rb;

			memset(&rb, 0, sizeof rb);
			rb.count = 4;
			rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			rb.memory = V4L2_MEMORY_MMAP;
			ret = vidioc_reqbufs(file, priv, &rb);
			if (ret != 0)
				return ret;
		}
	}
	mbuf->frames = gspca_dev->nframes;
	mbuf->size = gspca_dev->frsz * gspca_dev->nframes;
	for (i = 0; i < mbuf->frames; i++)
		mbuf->offsets[i] = gspca_dev->frame[i].v4l2_buf.m.offset;
	return 0;
}
#endif

static int dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gspca_dev *gspca_dev = file->private_data;
	struct gspca_frame *frame;
	struct page *page;
	unsigned long addr, start, size;
	int i, ret;

	start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;
	PDEBUG(D_STREAM, "mmap start:%08x size:%d", (int) start, (int) size);

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->present) {
		ret = -ENODEV;
		goto out;
	}
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
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	/* v4l1 maps all the buffers */
	if (i != 0
	    || size != frame->v4l2_buf.length * gspca_dev->nframes)
#endif
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

/*
 * wait for a video frame
 *
 * If a frame is ready, its index is returned.
 */
static int frame_wait(struct gspca_dev *gspca_dev,
			int nonblock_ing)
{
	struct gspca_frame *frame;
	int i, j, ret;

	/* check if a frame is ready */
	i = gspca_dev->fr_o;
	j = gspca_dev->fr_queue[i];
	frame = &gspca_dev->frame[j];

	if (!(frame->v4l2_buf.flags & V4L2_BUF_FLAG_DONE)) {
		if (nonblock_ing)
			return -EAGAIN;

		/* wait till a frame is ready */
		ret = wait_event_interruptible_timeout(gspca_dev->wq,
			(frame->v4l2_buf.flags & V4L2_BUF_FLAG_DONE) ||
			!gspca_dev->streaming || !gspca_dev->present,
			msecs_to_jiffies(3000));
		if (ret < 0)
			return ret;
		if (ret == 0 || !gspca_dev->streaming || !gspca_dev->present)
			return -EIO;
	}

	gspca_dev->fr_o = (i + 1) % gspca_dev->nframes;
	PDEBUG(D_FRAM, "frame wait q:%d i:%d o:%d",
		gspca_dev->fr_q,
		gspca_dev->fr_i,
		gspca_dev->fr_o);

	if (gspca_dev->sd_desc->dq_callback) {
		mutex_lock(&gspca_dev->usb_lock);
		gspca_dev->sd_desc->dq_callback(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
	}
	return j;
}

/*
 * dequeue a video buffer
 *
 * If nonblock_ing is false, block until a buffer is available.
 */
static int vidioc_dqbuf(struct file *file, void *priv,
			struct v4l2_buffer *v4l2_buf)
{
	struct gspca_dev *gspca_dev = priv;
	struct gspca_frame *frame;
	int i, ret;

	PDEBUG(D_FRAM, "dqbuf");
	if (v4l2_buf->memory != gspca_dev->memory)
		return -EINVAL;

	/* if not streaming, be sure the application will not loop forever */
	if (!(file->f_flags & O_NONBLOCK)
	    && !gspca_dev->streaming && gspca_dev->users == 1)
		return -EINVAL;

	/* only the capturing file may dequeue */
	if (gspca_dev->capt_file != file)
		return -EINVAL;

	/* only one dequeue / read at a time */
	if (mutex_lock_interruptible(&gspca_dev->read_lock))
		return -ERESTARTSYS;

	ret = frame_wait(gspca_dev, file->f_flags & O_NONBLOCK);
	if (ret < 0)
		goto out;
	i = ret;				/* frame index */
	frame = &gspca_dev->frame[i];
	if (gspca_dev->memory == V4L2_MEMORY_USERPTR) {
		if (copy_to_user((__u8 __user *) frame->v4l2_buf.m.userptr,
				 frame->data,
				 frame->v4l2_buf.bytesused)) {
			PDEBUG(D_ERR|D_STREAM,
				"dqbuf cp to user failed");
			ret = -EFAULT;
			goto out;
		}
	}
	frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	memcpy(v4l2_buf, &frame->v4l2_buf, sizeof *v4l2_buf);
	PDEBUG(D_FRAM, "dqbuf %d", i);
	ret = 0;
out:
	mutex_unlock(&gspca_dev->read_lock);
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
	struct gspca_dev *gspca_dev = priv;
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
	i = gspca_dev->fr_q;
	gspca_dev->fr_queue[i] = index;
	gspca_dev->fr_q = (i + 1) % gspca_dev->nframes;
	PDEBUG(D_FRAM, "qbuf q:%d i:%d o:%d",
		gspca_dev->fr_q,
		gspca_dev->fr_i,
		gspca_dev->fr_o);

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
	if (gspca_dev->nframes == 0) {
		struct v4l2_requestbuffers rb;

		memset(&rb, 0, sizeof rb);
		rb.count = gspca_dev->nbufread;
		rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rb.memory = GSPCA_MEMORY_READ;
		ret = vidioc_reqbufs(file, gspca_dev, &rb);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read reqbuf err %d", ret);
			return ret;
		}
		memset(&v4l2_buf, 0, sizeof v4l2_buf);
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = GSPCA_MEMORY_READ;
		for (i = 0; i < gspca_dev->nbufread; i++) {
			v4l2_buf.index = i;
			ret = vidioc_qbuf(file, gspca_dev, &v4l2_buf);
			if (ret != 0) {
				PDEBUG(D_STREAM, "read qbuf err: %d", ret);
				return ret;
			}
		}
		gspca_dev->memory = GSPCA_MEMORY_READ;
	}

	/* start streaming */
	ret = vidioc_streamon(file, gspca_dev, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret != 0)
		PDEBUG(D_STREAM, "read streamon err %d", ret);
	return ret;
}

static unsigned int dev_poll(struct file *file, poll_table *wait)
{
	struct gspca_dev *gspca_dev = file->private_data;
	int i, ret;

	PDEBUG(D_FRAM, "poll");

	poll_wait(file, &gspca_dev->wq, wait);
	if (!gspca_dev->present)
		return POLLERR;

	/* if reqbufs is not done, the user would use read() */
	if (gspca_dev->nframes == 0) {
		if (gspca_dev->memory != GSPCA_MEMORY_NO)
			return POLLERR;		/* not the 1st time */
		ret = read_alloc(gspca_dev, file);
		if (ret != 0)
			return POLLERR;
	}

	if (mutex_lock_interruptible(&gspca_dev->queue_lock) != 0)
		return POLLERR;
	if (!gspca_dev->present) {
		ret = POLLERR;
		goto out;
	}

	/* check the next incoming buffer */
	i = gspca_dev->fr_o;
	i = gspca_dev->fr_queue[i];
	if (gspca_dev->frame[i].v4l2_buf.flags & V4L2_BUF_FLAG_DONE)
		ret = POLLIN | POLLRDNORM;	/* something to read */
	else
		ret = 0;
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static ssize_t dev_read(struct file *file, char __user *data,
		    size_t count, loff_t *ppos)
{
	struct gspca_dev *gspca_dev = file->private_data;
	struct gspca_frame *frame;
	struct v4l2_buffer v4l2_buf;
	struct timeval timestamp;
	int n, ret, ret2;

	PDEBUG(D_FRAM, "read (%zd)", count);
	if (!gspca_dev->present)
		return -ENODEV;
	switch (gspca_dev->memory) {
	case GSPCA_MEMORY_NO:			/* first time */
		ret = read_alloc(gspca_dev, file);
		if (ret != 0)
			return ret;
		break;
	case GSPCA_MEMORY_READ:
		if (gspca_dev->capt_file == file)
			break;
		/* fall thru */
	default:
		return -EINVAL;
	}

	/* get a frame */
	jiffies_to_timeval(get_jiffies_64(), &timestamp);
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
	.vidioc_g_audio		= vidioc_g_audio,
	.vidioc_s_audio		= vidioc_s_audio,
	.vidioc_enumaudio	= vidioc_enumaudio,
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
	.vidioc_s_std		= vidioc_s_std,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device gspca_template = {
	.name = "gspca main driver",
	.fops = &dev_fops,
	.ioctl_ops = &dev_ioctl_ops,
	.release = gspca_release,
	.minor = -1,
};

/*
 * probe and create a new gspca device
 *
 * This function must be called by the sub-driver when it is
 * called for probing a new device.
 */
int gspca_dev_probe(struct usb_interface *intf,
		const struct usb_device_id *id,
		const struct sd_desc *sd_desc,
		int dev_size,
		struct module *module)
{
	struct usb_interface_descriptor *interface;
	struct gspca_dev *gspca_dev;
	struct usb_device *dev = interface_to_usbdev(intf);
	int ret;

	PDEBUG(D_PROBE, "probing %04x:%04x", id->idVendor, id->idProduct);

	/* we don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return -ENODEV;
	interface = &intf->cur_altsetting->desc;
	if (interface->bInterfaceNumber > 0)
		return -ENODEV;

	/* create the device */
	if (dev_size < sizeof *gspca_dev)
		dev_size = sizeof *gspca_dev;
	gspca_dev = kzalloc(dev_size, GFP_KERNEL);
	if (!gspca_dev) {
		err("couldn't kzalloc gspca struct");
		return -ENOMEM;
	}
	gspca_dev->usb_buf = kmalloc(USB_BUF_SZ, GFP_KERNEL);
	if (!gspca_dev->usb_buf) {
		err("out of memory");
		ret = -ENOMEM;
		goto out;
	}
	gspca_dev->dev = dev;
	gspca_dev->iface = interface->bInterfaceNumber;
	gspca_dev->nbalt = intf->num_altsetting;
	gspca_dev->sd_desc = sd_desc;
	gspca_dev->nbufread = 2;
	gspca_dev->empty_packet = -1;	/* don't check the empty packets */

	/* configure the subdriver and initialize the USB device */
	ret = sd_desc->config(gspca_dev, id);
	if (ret < 0)
		goto out;
	ret = sd_desc->init(gspca_dev);
	if (ret < 0)
		goto out;
	ret = gspca_set_alt0(gspca_dev);
	if (ret < 0)
		goto out;
	gspca_set_default_mode(gspca_dev);

	mutex_init(&gspca_dev->usb_lock);
	mutex_init(&gspca_dev->read_lock);
	mutex_init(&gspca_dev->queue_lock);
	init_waitqueue_head(&gspca_dev->wq);

	/* init video stuff */
	memcpy(&gspca_dev->vdev, &gspca_template, sizeof gspca_template);
	gspca_dev->vdev.parent = &dev->dev;
	gspca_dev->module = module;
	gspca_dev->present = 1;
	ret = video_register_device(&gspca_dev->vdev,
				  VFL_TYPE_GRABBER,
				  video_nr);
	if (ret < 0) {
		err("video_register_device err %d", ret);
		goto out;
	}

	usb_set_intfdata(intf, gspca_dev);
	PDEBUG(D_PROBE, "probe ok");
	return 0;
out:
	kfree(gspca_dev->usb_buf);
	kfree(gspca_dev);
	return ret;
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

	gspca_dev->present = 0;
	gspca_dev->streaming = 0;

	usb_set_intfdata(intf, NULL);

	/* release the device */
	/* (this will call gspca_release() immediatly or on last close) */
	video_unregister_device(&gspca_dev->vdev);

	PDEBUG(D_PROBE, "disconnect complete");
}
EXPORT_SYMBOL(gspca_disconnect);

#ifdef CONFIG_PM
int gspca_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);

	if (!gspca_dev->streaming)
		return 0;
	gspca_dev->frozen = 1;		/* avoid urb error messages */
	if (gspca_dev->sd_desc->stopN)
		gspca_dev->sd_desc->stopN(gspca_dev);
	destroy_urbs(gspca_dev);
	gspca_set_alt0(gspca_dev);
	if (gspca_dev->sd_desc->stop0)
		gspca_dev->sd_desc->stop0(gspca_dev);
	return 0;
}
EXPORT_SYMBOL(gspca_suspend);

int gspca_resume(struct usb_interface *intf)
{
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);

	gspca_dev->frozen = 0;
	gspca_dev->sd_desc->init(gspca_dev);
	if (gspca_dev->streaming)
		return gspca_init_transfer(gspca_dev);
	return 0;
}
EXPORT_SYMBOL(gspca_resume);
#endif
/* -- cam driver utility functions -- */

/* auto gain and exposure algorithm based on the knee algorithm described here:
   http://ytse.tricolour.net/docs/LowLightOptimization.html

   Returns 0 if no changes were made, 1 if the gain and or exposure settings
   where changed. */
int gspca_auto_gain_n_exposure(struct gspca_dev *gspca_dev, int avg_lum,
	int desired_avg_lum, int deadzone, int gain_knee, int exposure_knee)
{
	int i, steps, gain, orig_gain, exposure, orig_exposure, autogain;
	const struct ctrl *gain_ctrl = NULL;
	const struct ctrl *exposure_ctrl = NULL;
	const struct ctrl *autogain_ctrl = NULL;
	int retval = 0;

	for (i = 0; i < gspca_dev->sd_desc->nctrls; i++) {
		if (gspca_dev->sd_desc->ctrls[i].qctrl.id == V4L2_CID_GAIN)
			gain_ctrl = &gspca_dev->sd_desc->ctrls[i];
		if (gspca_dev->sd_desc->ctrls[i].qctrl.id == V4L2_CID_EXPOSURE)
			exposure_ctrl = &gspca_dev->sd_desc->ctrls[i];
		if (gspca_dev->sd_desc->ctrls[i].qctrl.id == V4L2_CID_AUTOGAIN)
			autogain_ctrl = &gspca_dev->sd_desc->ctrls[i];
	}
	if (!gain_ctrl || !exposure_ctrl || !autogain_ctrl) {
		PDEBUG(D_ERR, "Error: gspca_auto_gain_n_exposure called "
			"on cam without (auto)gain/exposure");
		return 0;
	}

	if (gain_ctrl->get(gspca_dev, &gain) ||
			exposure_ctrl->get(gspca_dev, &exposure) ||
			autogain_ctrl->get(gspca_dev, &autogain) || !autogain)
		return 0;

	orig_gain = gain;
	orig_exposure = exposure;

	/* If we are of a multiple of deadzone, do multiple steps to reach the
	   desired lumination fast (with the risc of a slight overshoot) */
	steps = abs(desired_avg_lum - avg_lum) / deadzone;

	PDEBUG(D_FRAM, "autogain: lum: %d, desired: %d, steps: %d",
		avg_lum, desired_avg_lum, steps);

	for (i = 0; i < steps; i++) {
		if (avg_lum > desired_avg_lum) {
			if (gain > gain_knee)
				gain--;
			else if (exposure > exposure_knee)
				exposure--;
			else if (gain > gain_ctrl->qctrl.default_value)
				gain--;
			else if (exposure > exposure_ctrl->qctrl.minimum)
				exposure--;
			else if (gain > gain_ctrl->qctrl.minimum)
				gain--;
			else
				break;
		} else {
			if (gain < gain_ctrl->qctrl.default_value)
				gain++;
			else if (exposure < exposure_knee)
				exposure++;
			else if (gain < gain_knee)
				gain++;
			else if (exposure < exposure_ctrl->qctrl.maximum)
				exposure++;
			else if (gain < gain_ctrl->qctrl.maximum)
				gain++;
			else
				break;
		}
	}

	if (gain != orig_gain) {
		gain_ctrl->set(gspca_dev, gain);
		retval = 1;
	}
	if (exposure != orig_exposure) {
		exposure_ctrl->set(gspca_dev, exposure);
		retval = 1;
	}

	return retval;
}
EXPORT_SYMBOL(gspca_auto_gain_n_exposure);

/* -- module insert / remove -- */
static int __init gspca_init(void)
{
	info("main v%d.%d.%d registered",
		(DRIVER_VERSION_NUMBER >> 16) & 0xff,
		(DRIVER_VERSION_NUMBER >> 8) & 0xff,
		DRIVER_VERSION_NUMBER & 0xff);
	return 0;
}
static void __exit gspca_exit(void)
{
	info("main deregistered");
}

module_init(gspca_init);
module_exit(gspca_exit);

#ifdef GSPCA_DEBUG
module_param_named(debug, gspca_debug, int, 0644);
MODULE_PARM_DESC(debug,
		"Debug (bit) 0x01:error 0x02:probe 0x04:config"
		" 0x08:stream 0x10:frame 0x20:packet 0x40:USBin 0x80:USBout"
		" 0x0100: v4l2");
#endif
