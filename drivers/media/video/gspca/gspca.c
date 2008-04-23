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
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <linux/jiffies.h>

#include "gspca.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("GSPCA USB Camera Driver");
MODULE_LICENSE("GPL");

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(0, 0, 30)
static const char version[] = "0.0.30";

static int video_nr = -1;

static int comp_fac = 30;	/* Buffer size ratio when compressed in % */

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

/*
 * ISOC message interrupt from the USB device
 *
 * Analyse each packet and call the subdriver for doing the copy
 * to the frame buffer.
 */
static void isoc_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	struct gspca_frame *frame;
	unsigned char *data;	/* address of data in the iso message */
	int i, j, len, st;
	cam_pkt_op pkt_scan;

	PDEBUG(D_PACK, "isoc irq");
	if (!gspca_dev->streaming)
		return;
	pkt_scan = gspca_dev->sd_desc->pkt_scan;
	for (i = 0; i < urb->number_of_packets; i++) {

		/* check the availability of the frame buffer */
		j = gspca_dev->fr_i;
		j = gspca_dev->fr_queue[j];
		frame = &gspca_dev->frame[j];
		if ((frame->v4l2_buf.flags
				& (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))
					!= V4L2_BUF_FLAG_QUEUED) {
			gspca_dev->last_packet_type = DISCARD_PACKET;
			break;
		}

		/* check the packet status and length */
		len = urb->iso_frame_desc[i].actual_length;
		st = urb->iso_frame_desc[i].status;
		if (st) {
			PDEBUG(D_ERR, "ISOC data error: [%d] len=%d, status=%d",
				i, len, st);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}
		if (len == 0)
			continue;

		/* let the packet be analyzed by the subdriver */
		PDEBUG(D_PACK, "packet [%d] o:%d l:%d",
			i, urb->iso_frame_desc[i].offset, len);
		data = (unsigned char *) urb->transfer_buffer
					+ urb->iso_frame_desc[i].offset;
		pkt_scan(gspca_dev, frame, data, len);
	}

	/* resubmit the URB */
	urb->status = 0;
	st = usb_submit_urb(urb, GFP_ATOMIC);
	if (st < 0)
		PDEBUG(D_ERR|D_PACK, "usb_submit_urb() ret %d", st);
}

/*
 * add data to the current frame
 *
 * This function is called by the subdrivers at interrupt level.
 * To build a frame, these ones must add
 *	- one FIRST_PACKET
 *	- 0 or many INTER_PACKETs
 *	- one LAST_PACKET
 * DISCARD_PACKET invalidates the whole frame.
 * On LAST_PACKET, a new frame is returned.
 */
struct gspca_frame *gspca_frame_add(struct gspca_dev *gspca_dev,
				    int packet_type,
				    struct gspca_frame *frame,
				    unsigned char *data,
				    int len)
{
	int i, j;

	PDEBUG(D_PACK, "add t:%d l:%d",	packet_type, len);

	/* when start of a new frame, if the current frame buffer
	 * is not queued, discard the whole frame */
	if (packet_type == FIRST_PACKET) {
		if ((frame->v4l2_buf.flags
				& (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))
					!= V4L2_BUF_FLAG_QUEUED) {
			gspca_dev->last_packet_type = DISCARD_PACKET;
			return frame;
		}
		frame->data_end = frame->data;
		jiffies_to_timeval(get_jiffies_64(),
				   &frame->v4l2_buf.timestamp);
		frame->v4l2_buf.sequence = ++gspca_dev->sequence;
	} else if (gspca_dev->last_packet_type == DISCARD_PACKET)
		return frame;

	/* append the packet in the frame buffer */
	if (len > 0) {
		if (frame->data_end - frame->data + len
						 > frame->v4l2_buf.length) {
			PDEBUG(D_ERR|D_PACK, "frame overflow %d > %d",
				frame->data_end - frame->data + len,
				frame->v4l2_buf.length);
			packet_type = DISCARD_PACKET;
		} else {
			if (frame->v4l2_buf.memory != V4L2_MEMORY_USERPTR)
				memcpy(frame->data_end, data, len);
			else
				copy_to_user(frame->data_end, data, len);
			frame->data_end += len;
		}
	}
	gspca_dev->last_packet_type = packet_type;

	/* if last packet, wake the application and advance in the queue */
	if (packet_type == LAST_PACKET) {
		frame->v4l2_buf.bytesused = frame->data_end - frame->data;
		frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_QUEUED;
		frame->v4l2_buf.flags |= V4L2_BUF_FLAG_DONE;
		atomic_inc(&gspca_dev->nevent);
		wake_up_interruptible(&gspca_dev->wq);	/* event = new frame */
		i = gspca_dev->fr_i;
		i = (i + 1) % gspca_dev->nframes;
		PDEBUG(D_FRAM, "frame complete len:%d q:%d i:%d o:%d",
			frame->v4l2_buf.bytesused,
			gspca_dev->fr_q,
			i,
			gspca_dev->fr_o);
		j = gspca_dev->fr_queue[i];
		frame = &gspca_dev->frame[j];
		gspca_dev->fr_i = i;
	}
	return frame;
}
EXPORT_SYMBOL(gspca_frame_add);

static int gspca_is_compressed(__u32 format)
{
	switch (format) {
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_JPEG:
		return 1;
	}
	return 0;
}

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (mem != 0) {
		memset(mem, 0, size);
		adr = (unsigned long) mem;
		while ((long) size > 0) {
			SetPageReserved(vmalloc_to_page((void *) adr));
			adr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;
	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *) adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static int frame_alloc(struct gspca_dev *gspca_dev,
			unsigned int count,
			unsigned int frsz,
			enum v4l2_memory memory)
{
	int i, ret = 0;

	PDEBUG(D_STREAM, "frame alloc frsz: %d", frsz);
	if (gspca_dev->nframes != 0) {
		PDEBUG(D_ERR|D_STREAM, "alloc frame already done");
		return -EBUSY;
	}
	if (count > GSPCA_MAX_FRAMES)
		count = GSPCA_MAX_FRAMES;
	/* if compressed, reduce the buffer size */
	if (gspca_is_compressed(gspca_dev->pixfmt))
		frsz = (frsz * comp_fac) / 100;
	frsz = PAGE_ALIGN(frsz);
	PDEBUG(D_STREAM, "new fr_sz: %d", frsz);
	gspca_dev->frsz = frsz;
	if (memory == V4L2_MEMORY_MMAP) {
		gspca_dev->frbuf = rvmalloc(frsz * count);
		if (!gspca_dev->frbuf) {
			err("frame alloc failed");
			return -ENOMEM;
		}
	}
	gspca_dev->nframes = count;
	for (i = 0; i < count; i++) {
		gspca_dev->frame[i].v4l2_buf.index = i;
		gspca_dev->frame[i].v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		gspca_dev->frame[i].v4l2_buf.flags = 0;
		gspca_dev->frame[i].v4l2_buf.field = V4L2_FIELD_NONE;
		gspca_dev->frame[i].v4l2_buf.length = frsz;
		gspca_dev->frame[i].v4l2_buf.memory = memory;
		if (memory == V4L2_MEMORY_MMAP) {
			gspca_dev->frame[i].data
				= gspca_dev->frame[i].data_end
				= gspca_dev->frbuf + i * frsz;
			gspca_dev->frame[i].v4l2_buf.m.offset = i * frsz;
		}
		gspca_dev->frame[i].v4l2_buf.flags = 0;	/* buf in app space */
	}
	gspca_dev->fr_i = gspca_dev->fr_o = gspca_dev->fr_q = 0;
	gspca_dev->last_packet_type = DISCARD_PACKET;
	gspca_dev->sequence = 0;
	atomic_set(&gspca_dev->nevent, 0);
	return ret;
}

static void frame_free(struct gspca_dev *gspca_dev)
{
	int i;

	PDEBUG(D_STREAM, "frame free");
	if (gspca_dev->frbuf != 0) {
		rvfree(gspca_dev->frbuf,
			gspca_dev->nframes * gspca_dev->frsz);
		gspca_dev->frbuf = NULL;
		for (i = 0; i < gspca_dev->nframes; i++)
			gspca_dev->frame[i].data = NULL;
	}
	gspca_dev->nframes = 0;
}

static int gspca_kill_transfer(struct gspca_dev *gspca_dev)
{
	struct urb *urb;
	unsigned int i;

	PDEBUG(D_STREAM, "kill transfer");
	for (i = 0; i < NURBS; ++i) {
		urb = gspca_dev->pktbuf[i].urb;
		if (urb == NULL)
			continue;

		gspca_dev->pktbuf[i].urb = NULL;
		usb_kill_urb(urb);

		/* urb->transfer_buffer_length is not touched by USB core,
		 * so we can use it here as the buffer length */
		if (gspca_dev->pktbuf[i].data) {
			usb_buffer_free(gspca_dev->dev,
					urb->transfer_buffer_length,
					gspca_dev->pktbuf[i].data,
					urb->transfer_dma);
			gspca_dev->pktbuf[i].data = NULL;
		}
		usb_free_urb(urb);
	}
	return 0;
}

/*
 * search an input isochronous endpoint in an alternate setting
 */
static struct usb_host_endpoint *alt_isoc(struct usb_host_interface *alt,
					  __u8 epaddr)
{
	struct usb_host_endpoint *ep;
	int i, attr;

	epaddr |= USB_DIR_IN;
	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		ep = &alt->endpoint[i];
		if (ep->desc.bEndpointAddress == epaddr) {
			attr = ep->desc.bmAttributes
						& USB_ENDPOINT_XFERTYPE_MASK;
			if (attr == USB_ENDPOINT_XFER_ISOC)
				return ep;
			break;
		}
	}
	return NULL;
}

/*
 * search an input isochronous endpoint
 *
 * The endpoint is defined by the subdriver.
 * Use only the first isoc (some Zoran - 0x0572:0x0001 - have two such ep).
 * This routine may be called many times when the bandwidth is too small
 * (the bandwidth is checked on urb submit).
 */
struct usb_host_endpoint *get_isoc_ep(struct gspca_dev *gspca_dev)
{
	struct usb_interface *intf;
	struct usb_host_endpoint *ep;
	int i, ret;

	intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
	i = gspca_dev->alt;			/* previous alt setting */
	while (--i > 0) {			/* alt 0 is unusable */
		ep = alt_isoc(&intf->altsetting[i], gspca_dev->cam.epaddr);
		if (ep)
			break;
	}
	if (i <= 0) {
		err("no ISOC endpoint found");
		return NULL;
	}
	PDEBUG(D_STREAM, "use ISOC alt %d ep 0x%02x",
			i, ep->desc.bEndpointAddress);
	ret = usb_set_interface(gspca_dev->dev, gspca_dev->iface, i);
	if (ret < 0) {
		err("set interface err %d", ret);
		return NULL;
	}
	gspca_dev->alt = i;
	return ep;
}

/*
 * create the isochronous URBs
 */
static int create_urbs(struct gspca_dev *gspca_dev,
			struct usb_host_endpoint *ep)
{
	struct urb *urb;
	int n, i, psize, npkt, bsize;

	/* calculate the packet size and the number of packets */
	/* the URB buffer size must be a power of 2 */
	psize = le16_to_cpu(ep->desc.wMaxPacketSize);
	/* See paragraph 5.9 / table 5-11 of the usb 2.0 spec. */
	psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
	npkt = ISO_MAX_SIZE / psize;
	if (npkt > ISO_MAX_PKT)
		npkt = ISO_MAX_PKT;
	bsize = psize * npkt;
	for (n = ISO_MAX_SIZE; n > 0; n >>= 1) {
		if (n & bsize)	/* !! assume ISO_MAX_SIZE is a power of 2 */
			break;
	}
	if (n != 0) {
		npkt = n / psize;
		bsize = psize * npkt;
	}
	PDEBUG(D_STREAM,
		"isoc %d pkts size %d (bsize:%d)", npkt, psize, bsize);
	for (n = 0; n < NURBS; n++) {
		urb = usb_alloc_urb(npkt, GFP_KERNEL);
		if (!urb) {
			err("usb_alloc_urb failed");
			return -ENOMEM;
		}
		gspca_dev->pktbuf[n].data = usb_buffer_alloc(gspca_dev->dev,
						bsize,
						GFP_KERNEL,
						&urb->transfer_dma);

		if (gspca_dev->pktbuf[n].data == NULL) {
			usb_free_urb(urb);
			gspca_kill_transfer(gspca_dev);
			err("usb_buffer_urb failed");
			return -ENOMEM;
		}
		gspca_dev->pktbuf[n].urb = urb;
		urb->dev = gspca_dev->dev;
		urb->context = gspca_dev;
		urb->pipe = usb_rcvisocpipe(gspca_dev->dev,
					    ep->desc.bEndpointAddress);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = ep->desc.bInterval;
		urb->transfer_buffer = gspca_dev->pktbuf[n].data;
		urb->complete = isoc_irq;
		urb->number_of_packets = npkt;
		urb->transfer_buffer_length = bsize;
		for (i = 0; i < npkt; i++) {
			urb->iso_frame_desc[i].length = psize;
			urb->iso_frame_desc[i].offset = psize * i;
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
	int n, ret;

	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;

	/* set the max alternate setting and loop until urb submit succeeds */
	intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
	gspca_dev->alt = intf->num_altsetting;
	for (;;) {
		PDEBUG(D_STREAM, "init transfer nbalt %d", gspca_dev->alt);
		ep = get_isoc_ep(gspca_dev);
		if (ep == NULL) {
			ret = -EIO;
			goto out;
		}
		ret = create_urbs(gspca_dev, ep);
		if (ret < 0)
			goto out;

		/* start the cam */
		gspca_dev->sd_desc->start(gspca_dev);
		gspca_dev->streaming = 1;
		atomic_set(&gspca_dev->nevent, 0);

		/* submit the URBs */
		for (n = 0; n < NURBS; n++) {
			ret = usb_submit_urb(gspca_dev->pktbuf[n].urb,
						 GFP_KERNEL);
			if (ret < 0) {
				PDEBUG(D_ERR|D_STREAM,
					"usb_submit_urb [%d] err %d", n, ret);
				gspca_dev->streaming = 0;
				gspca_kill_transfer(gspca_dev);
				if (ret == -ENOSPC)
					break;	/* try the previous alt */
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
		PDEBUG(D_ERR|D_STREAM, "set interface 0 err %d", ret);
	return ret;
}

/* Note both the queue and the usb lock should be hold when calling this */
static void gspca_stream_off(struct gspca_dev *gspca_dev)
{
	gspca_dev->streaming = 0;
	if (gspca_dev->present) {
		gspca_dev->sd_desc->stopN(gspca_dev);
		gspca_kill_transfer(gspca_dev);
		gspca_set_alt0(gspca_dev);
		gspca_dev->sd_desc->stop0(gspca_dev);
		PDEBUG(D_STREAM, "stream off OK");
	} else {
		gspca_kill_transfer(gspca_dev);
		atomic_inc(&gspca_dev->nevent);
		wake_up_interruptible(&gspca_dev->wq);
		PDEBUG(D_ERR|D_STREAM, "stream off no device ??");
	}
}

static int gspca_set_default_mode(struct gspca_dev *gspca_dev)
{
	int i;

	i = gspca_dev->cam.nmodes - 1;	/* take the highest mode */
	gspca_dev->curr_mode = i;
	gspca_dev->width = gspca_dev->cam.cam_mode[i].width;
	gspca_dev->height = gspca_dev->cam.cam_mode[i].height;
	gspca_dev->pixfmt = gspca_dev->cam.cam_mode[i].pixfmt;
	return 0;
}

static int wxh_to_mode(struct gspca_dev *gspca_dev,
			int width, int height)
{
	int i;

	for (i = gspca_dev->cam.nmodes - 1; --i >= 0; ) {
		if (width > gspca_dev->cam.cam_mode[i].width)
			break;
	}
	i++;
	while (i < gspca_dev->cam.nmodes - 1
	       && width == gspca_dev->cam.cam_mode[i + 1].width
	       && height < gspca_dev->cam.cam_mode[i + 1].height)
		i++;
	return i;
}

static __u32 get_v4l2_depth(__u32 pixfmt)
{
	switch (pixfmt) {
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_RGB32:
		return 32;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		return 24;
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:		/* packed 4.2.2 */
	case V4L2_PIX_FMT_YYUV:
		return 16;
	case V4L2_PIX_FMT_YUV420:	/* planar 4.2.0 */
		return 12;
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_JPEG:
	case V4L2_PIX_FMT_SBGGR8:	/* Bayer */
		return 8;
	}
	PDEBUG(D_ERR|D_CONF, "Unknown pixel format %c%c%c%c",
		pixfmt & 0xff,
		(pixfmt >> 8) & 0xff,
		(pixfmt >> 16) & 0xff,
		pixfmt >> 24);
	return -EINVAL;
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
			if (gspca_dev->cam.cam_mode[modeD].pixfmt == pixfmt)
				return modeD;
		}
		if (++modeU < gspca_dev->cam.nmodes) {
			if (gspca_dev->cam.cam_mode[modeU].pixfmt == pixfmt)
				return modeU;
		}
	}
	return -EINVAL;
}

static int vidioc_enum_fmt_cap(struct file *file, void  *priv,
				struct v4l2_fmtdesc *fmtdesc)
{
	struct gspca_dev *gspca_dev = priv;
	int i, j, index;
	__u32 fmt_tb[8];

	PDEBUG(D_CONF, "enum fmt cap");

	/* give an index to each format */
	index = 0;
	j = 0;
	for (i = gspca_dev->cam.nmodes; --i >= 0; ) {
		fmt_tb[index] = gspca_dev->cam.cam_mode[i].pixfmt;
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
			if (index >= sizeof fmt_tb / sizeof fmt_tb[0])
				return -EINVAL;
		}
	}
	if (i < 0)
		return -EINVAL;		/* no more format */

	fmtdesc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmtdesc->pixelformat = fmt_tb[index];
	if (gspca_is_compressed(fmt_tb[index]))
		fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
	fmtdesc->description[0] = fmtdesc->pixelformat & 0xff;
	fmtdesc->description[1] = (fmtdesc->pixelformat >> 8) & 0xff;
	fmtdesc->description[2] = (fmtdesc->pixelformat >> 16) & 0xff;
	fmtdesc->description[3] = fmtdesc->pixelformat >> 24;
	fmtdesc->description[4] = '\0';
	return 0;
}

static int gspca_get_buff_size(struct gspca_dev *gspca_dev)
{
	unsigned int size;

	size = gspca_dev->width * gspca_dev->height
				* get_v4l2_depth(gspca_dev->pixfmt) / 8;
	if (!size)
		return -ENOMEM;
	return size;
}

static int vidioc_g_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = priv;

	fmt->fmt.pix.width = gspca_dev->width;
	fmt->fmt.pix.height = gspca_dev->height;
	fmt->fmt.pix.pixelformat = gspca_dev->pixfmt;
#ifdef GSPCA_DEBUG
	if (gspca_debug & D_CONF) {
		PDEBUG_MODE("get fmt cap",
			fmt->fmt.pix.pixelformat,
			fmt->fmt.pix.width,
			fmt->fmt.pix.height);
	}
#endif
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = get_v4l2_depth(fmt->fmt.pix.pixelformat)
					* fmt->fmt.pix.width / 8;
	fmt->fmt.pix.sizeimage = fmt->fmt.pix.bytesperline
					* fmt->fmt.pix.height;
/* (should be in the subdriver) */
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;
	return 0;
}

static int try_fmt_cap(struct file *file,
			void *priv,
			struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = priv;
	int w, h, mode, mode2, frsz;

	w = (int) fmt->fmt.pix.width;
	h = (int) fmt->fmt.pix.height;
#ifdef GSPCA_DEBUG
	if (gspca_debug & D_CONF)
		PDEBUG_MODE("try fmt cap", fmt->fmt.pix.pixelformat, w, h);
#endif
	/* search the closest mode for width and height */
	mode = wxh_to_mode(gspca_dev, w, h);

	/* OK if right palette */
	if (gspca_dev->cam.cam_mode[mode].pixfmt != fmt->fmt.pix.pixelformat) {

		/* else, search the closest mode with the same pixel format */
		mode2 = gspca_get_mode(gspca_dev, mode,
					fmt->fmt.pix.pixelformat);
		if (mode2 >= 0)
			mode = mode2;
		else {

			/* no chance, return this mode */
			fmt->fmt.pix.pixelformat
				= gspca_dev->cam.cam_mode[mode].pixfmt;
#ifdef GSPCA_DEBUG
			if (gspca_debug & D_CONF) {
				PDEBUG_MODE("new format",
					fmt->fmt.pix.pixelformat,
					gspca_dev->cam.cam_mode[mode].width,
					gspca_dev->cam.cam_mode[mode].height);
			}
#endif
		}
	}
	fmt->fmt.pix.width = gspca_dev->cam.cam_mode[mode].width;
	fmt->fmt.pix.height = gspca_dev->cam.cam_mode[mode].height;
	fmt->fmt.pix.bytesperline = get_v4l2_depth(fmt->fmt.pix.pixelformat)
					* fmt->fmt.pix.width / 8;
	frsz = fmt->fmt.pix.bytesperline * fmt->fmt.pix.height;
	if (gspca_is_compressed(fmt->fmt.pix.pixelformat))
		frsz = (frsz * comp_fac) / 100;
	fmt->fmt.pix.sizeimage = frsz;
	return mode;			/* used when s_fmt */
}

static int vidioc_try_fmt_cap(struct file *file,
			      void *priv,
			      struct v4l2_format *fmt)
{
	int ret;

	ret = try_fmt_cap(file, priv, fmt);
	if (ret < 0)
		return ret;
	return 0;
}

static int vidioc_s_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct gspca_dev *gspca_dev = priv;
	int ret, was_streaming;

#ifdef GSPCA_DEBUG
	if (gspca_debug & D_CONF) {
		PDEBUG_MODE("set fmt cap",
			fmt->fmt.pix.pixelformat,
			fmt->fmt.pix.width, fmt->fmt.pix.height);
	}
#endif
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	ret = try_fmt_cap(file, priv, fmt);
	if (ret < 0)
		goto out;

	if (ret == gspca_dev->curr_mode)
		goto out;			/* same mode */
	was_streaming = gspca_dev->streaming;
	if (was_streaming != 0) {
		if (mutex_lock_interruptible(&gspca_dev->usb_lock)) {
			ret = -ERESTARTSYS;
			goto out;
		}
		gspca_stream_off(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
	}
	gspca_dev->width = (int) fmt->fmt.pix.width;
	gspca_dev->height = (int) fmt->fmt.pix.height;
	gspca_dev->pixfmt = fmt->fmt.pix.pixelformat;
	gspca_dev->curr_mode = ret;
	if (was_streaming)
		ret = gspca_init_transfer(gspca_dev);
out:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static int dev_open(struct inode *inode, struct file *file)
{
	struct gspca_dev *gspca_dev;
	int ret;

	PDEBUG(D_STREAM, "opening");
	gspca_dev = (struct gspca_dev *) video_devdata(file);
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->present) {
		ret = -ENODEV;
		goto out;
	}

	/* if not done yet, initialize the sensor */
	if (gspca_dev->users == 0) {
		if (mutex_lock_interruptible(&gspca_dev->usb_lock)) {
			ret = -ERESTARTSYS;
			goto out;
		}
		ret = gspca_dev->sd_desc->open(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
		if (ret != 0) {
			PDEBUG(D_ERR|D_CONF, "init device failed %d", ret);
			goto out;
		}
	} else if (gspca_dev->users > 4) {	/* (arbitrary value) */
		ret = -EBUSY;
		goto out;
	}
	gspca_dev->users++;
	file->private_data = gspca_dev;
#ifdef GSPCA_DEBUG
	/* activate the v4l2 debug */
	if (gspca_debug & D_CONF)
		gspca_dev->vdev.debug |= 3;
	else
		gspca_dev->vdev.debug &= ~3;
#endif
out:
	mutex_unlock(&gspca_dev->queue_lock);
	if (ret != 0)
		PDEBUG(D_ERR|D_STREAM, "open failed err %d", ret);
	else
		PDEBUG(D_STREAM, "open OK");
	return ret;
}

static int dev_close(struct inode *inode, struct file *file)
{
	struct gspca_dev *gspca_dev = file->private_data;

	PDEBUG(D_STREAM, "closing");
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	gspca_dev->users--;
	if (gspca_dev->users > 0) {
		mutex_unlock(&gspca_dev->queue_lock);
		return 0;
	}

	if (gspca_dev->streaming)
		gspca_stream_off(gspca_dev);
	gspca_dev->sd_desc->close(gspca_dev);

	frame_free(gspca_dev);
	file->private_data = NULL;
	mutex_unlock(&gspca_dev->queue_lock);
	PDEBUG(D_STREAM, "closed");
	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct gspca_dev *gspca_dev = priv;

	PDEBUG(D_CONF, "querycap");
	memset(cap, 0, sizeof *cap);
	strncpy(cap->driver, gspca_dev->sd_desc->name, sizeof cap->driver);
	strncpy(cap->card, gspca_dev->cam.dev_name, sizeof cap->card);
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
	int i;

	PDEBUG(D_CONF, "queryctrl");
	for (i = 0; i < gspca_dev->sd_desc->nctrls; i++) {
		if (q_ctrl->id == gspca_dev->sd_desc->ctrls[i].qctrl.id) {
			memcpy(q_ctrl,
				&gspca_dev->sd_desc->ctrls[i].qctrl,
				sizeof *q_ctrl);
			return 0;
		}
	}
	if (q_ctrl->id >= V4L2_CID_BASE
	    && q_ctrl->id <= V4L2_CID_LASTP1) {
		q_ctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct gspca_dev *gspca_dev = priv;
	struct ctrl *ctrls;
	int i, ret;

	PDEBUG(D_CONF, "set ctrl");
	for (i = 0, ctrls = gspca_dev->sd_desc->ctrls;
	     i < gspca_dev->sd_desc->nctrls;
	     i++, ctrls++) {
		if (ctrl->id != ctrls->qctrl.id)
			continue;
		if (ctrl->value < ctrls->qctrl.minimum
		    && ctrl->value > ctrls->qctrl.maximum)
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

	struct ctrl *ctrls;
	int i, ret;

	for (i = 0, ctrls = gspca_dev->sd_desc->ctrls;
	     i < gspca_dev->sd_desc->nctrls;
	     i++, ctrls++) {
		if (ctrl->id != ctrls->qctrl.id)
			continue;
		if (mutex_lock_interruptible(&gspca_dev->usb_lock))
			return -ERESTARTSYS;
		ret = ctrls->get(gspca_dev, &ctrl->value);
		mutex_unlock(&gspca_dev->usb_lock);
		return ret;
	}
	return -EINVAL;
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
	int frsz, ret;

	PDEBUG(D_STREAM, "reqbufs %d", rb->count);
	if (rb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (rb->memory != V4L2_MEMORY_MMAP
	    && rb->memory != V4L2_MEMORY_USERPTR)
		return -EINVAL;
	if (rb->count == 0)
		return -EINVAL;
	frsz = gspca_get_buff_size(gspca_dev);
	if (frsz < 0)
		return frsz;
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	ret = frame_alloc(gspca_dev,
				rb->count,
				(unsigned int) frsz,
				rb->memory);
	if (ret == 0)
		rb->count = gspca_dev->nframes;
	mutex_unlock(&gspca_dev->queue_lock);
	PDEBUG(D_STREAM, "reqbufs st:%d c:%d", ret, rb->count);
	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *v4l2_buf)
{
	struct gspca_dev *gspca_dev = priv;
	struct gspca_frame *frame;

	PDEBUG(D_STREAM, "querybuf");
	if (v4l2_buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE
	    || v4l2_buf->index < 0
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

	PDEBUG(D_STREAM, "stream on");
	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->present) {
		ret = -ENODEV;
		goto out;
	}
	if (gspca_dev->nframes == 0) {
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

	PDEBUG(D_STREAM, "stream off");
	if (buf_type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (gspca_dev->streaming) {
		if (mutex_lock_interruptible(&gspca_dev->queue_lock))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&gspca_dev->usb_lock)) {
			mutex_unlock(&gspca_dev->queue_lock);
			return -ERESTARTSYS;
		}
		gspca_stream_off(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
		mutex_unlock(&gspca_dev->queue_lock);
	}
	return 0;
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

	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->sd_desc->set_jcomp)
		return -EINVAL;
	ret = gspca_dev->sd_desc->set_jcomp(gspca_dev, jpegcomp);
	mutex_unlock(&gspca_dev->usb_lock);
	return ret;
}

static int vidioc_g_parm(struct file *filp, void *priv,
			struct v4l2_streamparm *parm)
{
	struct gspca_dev *gspca_dev = priv;

	memset(parm, 0, sizeof parm);
	parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm->parm.capture.readbuffers = gspca_dev->nbufread;
	return 0;
}

static int vidioc_s_parm(struct file *filp, void *priv,
			struct v4l2_streamparm *parm)
{
	struct gspca_dev *gspca_dev = priv;
	int n;

	if (mutex_lock_interruptible(&gspca_dev->usb_lock))
		return -ERESTARTSYS;
	n = parm->parm.capture.readbuffers;
	if (n == 0 || n > GSPCA_MAX_FRAMES)
		parm->parm.capture.readbuffers = gspca_dev->nbufread;
	else
		gspca_dev->nbufread = n;
	mutex_unlock(&gspca_dev->usb_lock);
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
		struct v4l2_requestbuffers rb;
		int ret;

		memset(&rb, 0, sizeof rb);
		rb.count = 4;
		rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rb.memory = V4L2_MEMORY_MMAP;
		ret = vidioc_reqbufs(file, priv, &rb);
		if (ret != 0)
			return ret;
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
	struct gspca_frame *frame = 0;
	struct page *page;
	unsigned long addr, start, size;
	int i, ret;
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	int compat = 0;
#endif

	start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;
	PDEBUG(D_STREAM, "mmap start:%08x size:%d", (int) start, (int) size);

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;
	if (!gspca_dev->present) {
		ret = -ENODEV;
		goto done;
	}

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
	if (frame == 0) {
		PDEBUG(D_STREAM, "mmap no frame buffer found");
		ret = -EINVAL;
		goto done;
	}
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	if (i == 0 && size == frame->v4l2_buf.length * gspca_dev->nframes)
		compat = 1;
	else
#endif
	if (size != frame->v4l2_buf.length) {
		PDEBUG(D_STREAM, "mmap bad size");
		ret = -EINVAL;
		goto done;
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
			goto done;
		start += PAGE_SIZE;
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &gspca_vm_ops;
	vma->vm_private_data = frame;
	gspca_vm_open(vma);
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	if (compat) {
/*fixme: ugly*/
		for (i = 1; i < gspca_dev->nframes; ++i)
			gspca_dev->frame[i].v4l2_buf.flags |=
						V4L2_BUF_FLAG_MAPPED;
	}
#endif
	ret = 0;
done:
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

static unsigned int dev_poll(struct file *file, poll_table * wait)
{
	struct gspca_dev *gspca_dev = file->private_data;
	int i, ret;

	PDEBUG(D_FRAM, "poll");

	poll_wait(file, &gspca_dev->wq, wait);

	if (mutex_lock_interruptible(&gspca_dev->queue_lock) != 0)
		return POLLERR;
	if (gspca_dev->dev == 0
	    || !gspca_dev->streaming)			/* if not streaming */
		ret = POLLERR;
	else {
		i = gspca_dev->fr_o;
		i = gspca_dev->fr_queue[i];
		if (gspca_dev->frame[i].v4l2_buf.flags & V4L2_BUF_FLAG_DONE)
			ret = POLLIN | POLLRDNORM;	/* something to read */
		else
			ret = 0;
	}
	mutex_unlock(&gspca_dev->queue_lock);
	return ret;
}

/*
 * wait for a video frame
 *
 * If a frame is ready, its index is returned.
 */
static int gspca_frame_wait(struct gspca_dev *gspca_dev,
			int nonblock_ing)
{
	struct gspca_frame *frame;
	int i, j, ret;

	i = gspca_dev->fr_o;
	j = gspca_dev->fr_queue[i];
	frame = &gspca_dev->frame[j];
	if (frame->v4l2_buf.flags & V4L2_BUF_FLAG_DONE)
		goto ok;
	if (nonblock_ing)			/* no frame yet */
		return -EAGAIN;

	/* wait till a frame is ready */
	for (;;) {
		ret = wait_event_interruptible(gspca_dev->wq,
					atomic_read(&gspca_dev->nevent) > 0);
		if (ret != 0)
			return ret;
		if (!gspca_dev->streaming || !gspca_dev->present)
			return -EIO;
		i = gspca_dev->fr_o;
		j = gspca_dev->fr_queue[i];
		frame = &gspca_dev->frame[j];
		if (frame->v4l2_buf.flags & V4L2_BUF_FLAG_DONE)
			break;
	}

ok:
	atomic_dec(&gspca_dev->nevent);
	gspca_dev->fr_o = (i + 1) % gspca_dev->nframes;
	PDEBUG(D_FRAM, "frame wait q:%d i:%d o:%d",
		gspca_dev->fr_q,
		gspca_dev->fr_i,
		gspca_dev->fr_o);

	if (gspca_dev->sd_desc->dq_callback)
		gspca_dev->sd_desc->dq_callback(gspca_dev);

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
	if (v4l2_buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE
	    || (v4l2_buf->memory != V4L2_MEMORY_MMAP
		&& v4l2_buf->memory != V4L2_MEMORY_USERPTR))
		return -EINVAL;
	if (!gspca_dev->streaming)
		return -EINVAL;

	/* only one read */
	if (mutex_lock_interruptible(&gspca_dev->read_lock))
		return -ERESTARTSYS;

	ret = gspca_frame_wait(gspca_dev, file->f_flags & O_NONBLOCK);
	if (ret < 0)
		goto done;
	i = ret;				/* frame index */
	frame = &gspca_dev->frame[i];
	frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	memcpy(v4l2_buf, &frame->v4l2_buf, sizeof *v4l2_buf);
	PDEBUG(D_FRAM, "dqbuf %d", i);
	ret = 0;
done:
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
	if (v4l2_buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	index = v4l2_buf->index;
	if ((unsigned) index >= gspca_dev->nframes) {
		PDEBUG(D_STREAM,
			"qbuf idx %d >= %d", index, gspca_dev->nframes);
		return -EINVAL;
	}
	frame = &gspca_dev->frame[index];

	if (v4l2_buf->memory != frame->v4l2_buf.memory) {
		PDEBUG(D_STREAM, "qbuf bad memory type");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&gspca_dev->queue_lock))
		return -ERESTARTSYS;

	if (frame->v4l2_buf.flags
			& (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE)) {
		PDEBUG(D_STREAM, "qbuf bad state");
		ret = -EINVAL;
		goto out;
	}

	frame->v4l2_buf.flags |= V4L2_BUF_FLAG_QUEUED;
	frame->v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;

	if (v4l2_buf->memory == V4L2_MEMORY_USERPTR) {
		frame->data = frame->data_end =
				(unsigned char *) v4l2_buf->m.userptr;
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

static ssize_t dev_read(struct file *file, char __user *data,
		    size_t count, loff_t *ppos)
{
	struct gspca_dev *gspca_dev = file->private_data;
	struct gspca_frame *frame;
	struct v4l2_buffer v4l2_buf;
	struct timeval timestamp;
	int i, ret, ret2;

	PDEBUG(D_FRAM, "read (%p, %d)", data, count);
	if (gspca_dev->nframes == 0) {
		struct v4l2_requestbuffers rb;

		memset(&rb, 0, sizeof rb);
		rb.count = gspca_dev->nbufread;
		rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rb.memory = V4L2_MEMORY_MMAP;
		ret = vidioc_reqbufs(file, gspca_dev, &rb);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read reqbuf err: %d", ret);
			return ret;
		}
		memset(&v4l2_buf, 0, sizeof v4l2_buf);
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = V4L2_MEMORY_MMAP;
		for (i = 0; i < gspca_dev->nbufread; i++) {
			v4l2_buf.index = i;
/*fixme: ugly!*/
			gspca_dev->frame[i].v4l2_buf.flags |=
							V4L2_BUF_FLAG_MAPPED;
			ret = vidioc_qbuf(file, gspca_dev, &v4l2_buf);
			if (ret != 0) {
				PDEBUG(D_STREAM, "read qbuf err: %d", ret);
				return ret;
			}
		}
	}
	if (!gspca_dev->streaming) {
		ret = vidioc_streamon(file, gspca_dev,
					V4L2_BUF_TYPE_VIDEO_CAPTURE);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read streamon err %d", ret);
			return ret;
		}
	}

	/* get a frame */
	jiffies_to_timeval(get_jiffies_64(), &timestamp);
	timestamp.tv_sec--;
	for (i = 0; i < 2; i++) {
		memset(&v4l2_buf, 0, sizeof v4l2_buf);
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = V4L2_MEMORY_MMAP;
		ret = vidioc_dqbuf(file, gspca_dev, &v4l2_buf);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read dqbuf err %d", ret);
			return ret;
		}

		/* if the process slept for more than 1 second,
		 * get a brand new frame */
		frame = &gspca_dev->frame[v4l2_buf.index];
		if (frame->v4l2_buf.timestamp.tv_sec >= timestamp.tv_sec)
			break;
		ret = vidioc_qbuf(file, gspca_dev, &v4l2_buf);
		if (ret != 0) {
			PDEBUG(D_STREAM, "read qbuf err %d", ret);
			return ret;
		}
	}

	/* copy the frame */
	if (count < frame->v4l2_buf.bytesused) {
		PDEBUG(D_STREAM, "read bad count: %d < %d",
			count, frame->v4l2_buf.bytesused);
/*fixme: special errno?*/
		ret = -EINVAL;
		goto out;
	}
	count = frame->v4l2_buf.bytesused;
	ret = copy_to_user(data, frame->data, count);
	if (ret != 0) {
		PDEBUG(D_ERR|D_STREAM,
			"read cp to user lack %d / %d", ret, count);
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

static void gspca_dev_release(struct video_device *vfd)
{
	/* nothing */
}

static struct file_operations dev_fops = {
	.owner = THIS_MODULE,
	.open = dev_open,
	.release = dev_close,
	.read = dev_read,
	.mmap = dev_mmap,
	.ioctl = video_ioctl2,
	.llseek = no_llseek,
	.poll	= dev_poll,
};

static struct video_device gspca_template = {
	.name = "gspca main driver",
	.type = VID_TYPE_CAPTURE,
	.fops = &dev_fops,
	.release = gspca_dev_release,	/* mandatory */
	.minor = -1,
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_dqbuf		= vidioc_dqbuf,
	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_enum_fmt_cap	= vidioc_enum_fmt_cap,
	.vidioc_try_fmt_cap	= vidioc_try_fmt_cap,
	.vidioc_g_fmt_cap	= vidioc_g_fmt_cap,
	.vidioc_s_fmt_cap	= vidioc_s_fmt_cap,
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
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
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
		int dev_size)
{
	struct usb_interface_descriptor *interface;
	struct gspca_dev *gspca_dev;
	struct usb_device *dev = interface_to_usbdev(intf);
	int ret;
	__u16 vendor;
	__u16 product;

	vendor = le16_to_cpu(dev->descriptor.idVendor);
	product = le16_to_cpu(dev->descriptor.idProduct);
	PDEBUG(D_PROBE, "probing %04x:%04x", vendor, product);

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
	if (gspca_dev == NULL) {
		err("couldn't kzalloc gspca struct");
		return -EIO;
	}
	gspca_dev->dev = dev;
	gspca_dev->iface = interface->bInterfaceNumber;
	gspca_dev->sd_desc = sd_desc;
/*	gspca_dev->users = 0;			(done by kzalloc) */
	gspca_dev->nbufread = 2;

	/* configure the subdriver */
	ret = gspca_dev->sd_desc->config(gspca_dev, id);
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
	gspca_dev->vdev.dev = &dev->dev;
	ret = video_register_device(&gspca_dev->vdev,
				  VFL_TYPE_GRABBER,
				  video_nr);
	if (ret < 0) {
		err("video_register_device err %d", ret);
		goto out;
	}

	gspca_dev->present = 1;
	usb_set_intfdata(intf, gspca_dev);
	PDEBUG(D_PROBE, "probe ok");
	return 0;
out:
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
	struct gspca_dev *gspca_dev  = usb_get_intfdata(intf);

	if (!gspca_dev)
		return;
	gspca_dev->present = 0;
	mutex_lock(&gspca_dev->queue_lock);
	mutex_lock(&gspca_dev->usb_lock);
	gspca_dev->streaming = 0;
	gspca_kill_transfer(gspca_dev);
	mutex_unlock(&gspca_dev->usb_lock);
	mutex_unlock(&gspca_dev->queue_lock);
	while (gspca_dev->users != 0) {		/* wait until fully closed */
		atomic_inc(&gspca_dev->nevent);
		wake_up_interruptible(&gspca_dev->wq);	/* wake processes */
		schedule();
	}
/* We don't want people trying to open up the device */
	video_unregister_device(&gspca_dev->vdev);
/* Free the memory */
	kfree(gspca_dev);
	PDEBUG(D_PROBE, "disconnect complete");
}
EXPORT_SYMBOL(gspca_disconnect);

/* -- module insert / remove -- */
static int __init gspca_init(void)
{
	info("main v%s registered", version);
	return 0;
}
static void __exit gspca_exit(void)
{
	info("main deregistered");
}

module_init(gspca_init);
module_exit(gspca_exit);

module_param_named(debug, gspca_debug, int, 0644);
MODULE_PARM_DESC(debug,
		"Debug (bit) 0x01:error 0x02:probe 0x04:config"
		" 0x08:stream 0x10:frame 0x20:packet 0x40:USBin 0x80:USBout");

module_param(comp_fac, int, 0644);
MODULE_PARM_DESC(comp_fac,
		"Buffer size ratio when compressed in percent");
