/*
 *   tm6000-video.c - driver for TM5600/TM6000/TM6010 USB video capture devices
 *
 *  Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 *  Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>
 *	- Fixed module load/unload
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/tuner.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>

#include "tm6000-regs.h"
#include "tm6000.h"

#define BUFFER_TIMEOUT     msecs_to_jiffies(2000)  /* 2 seconds */

/* Limits minimum and default number of buffers */
#define TM6000_MIN_BUF 4
#define TM6000_DEF_BUF 8

#define TM6000_NUM_URB_BUF 8

#define TM6000_MAX_ISO_PACKETS	46	/* Max number of ISO packets */

/* Declare static vars that will be used as parameters */
static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */
static int video_nr = -1;		/* /dev/videoN, -1 for autodetect */
static int radio_nr = -1;		/* /dev/radioN, -1 for autodetect */
static bool keep_urb;			/* keep urb buffers allocated */

/* Debug level */
int tm6000_debug;
EXPORT_SYMBOL_GPL(tm6000_debug);

static struct tm6000_fmt format[] = {
	{
		.name     = "4:2:2, packed, YVY2",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.depth    = 16,
	}, {
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.depth    = 16,
	}, {
		.name     = "A/V + VBI mux packet",
		.fourcc   = V4L2_PIX_FMT_TM6000,
		.depth    = 16,
	}
};

/* ------------------------------------------------------------------
 *	DMA and thread functions
 * ------------------------------------------------------------------
 */

#define norm_maxw(a) 720
#define norm_maxh(a) 576

#define norm_minw(a) norm_maxw(a)
#define norm_minh(a) norm_maxh(a)

/*
 * video-buf generic routine to get the next available buffer
 */
static inline void get_next_buf(struct tm6000_dmaqueue *dma_q,
			       struct tm6000_buffer   **buf)
{
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);

	if (list_empty(&dma_q->active)) {
		dprintk(dev, V4L2_DEBUG_QUEUE, "No active queue to serve\n");
		*buf = NULL;
		return;
	}

	*buf = list_entry(dma_q->active.next,
			struct tm6000_buffer, vb.queue);
}

/*
 * Announces that a buffer were filled and request the next
 */
static inline void buffer_filled(struct tm6000_core *dev,
				 struct tm6000_dmaqueue *dma_q,
				 struct tm6000_buffer *buf)
{
	/* Advice that buffer was filled */
	dprintk(dev, V4L2_DEBUG_ISOC, "[%p/%d] wakeup\n", buf, buf->vb.i);
	buf->vb.state = VIDEOBUF_DONE;
	buf->vb.field_count++;
	v4l2_get_timestamp(&buf->vb.ts);

	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
}

/*
 * Identify the tm5600/6000 buffer header type and properly handles
 */
static int copy_streams(u8 *data, unsigned long len,
			struct urb *urb)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);
	u8 *ptr = data, *endp = data+len;
	unsigned long header = 0;
	int rc = 0;
	unsigned int cmd, cpysize, pktsize, size, field, block, line, pos = 0;
	struct tm6000_buffer *vbuf = NULL;
	char *voutp = NULL;
	unsigned int linewidth;

	if (!dev->radio) {
		/* get video buffer */
		get_next_buf(dma_q, &vbuf);

		if (!vbuf)
			return rc;
		voutp = videobuf_to_vmalloc(&vbuf->vb);

		if (!voutp)
			return 0;
	}

	for (ptr = data; ptr < endp;) {
		if (!dev->isoc_ctl.cmd) {
			/* Header */
			if (dev->isoc_ctl.tmp_buf_len > 0) {
				/* from last urb or packet */
				header = dev->isoc_ctl.tmp_buf;
				if (4 - dev->isoc_ctl.tmp_buf_len > 0) {
					memcpy((u8 *)&header +
						dev->isoc_ctl.tmp_buf_len,
						ptr,
						4 - dev->isoc_ctl.tmp_buf_len);
					ptr += 4 - dev->isoc_ctl.tmp_buf_len;
				}
				dev->isoc_ctl.tmp_buf_len = 0;
			} else {
				if (ptr + 3 >= endp) {
					/* have incomplete header */
					dev->isoc_ctl.tmp_buf_len = endp - ptr;
					memcpy(&dev->isoc_ctl.tmp_buf, ptr,
						dev->isoc_ctl.tmp_buf_len);
					return rc;
				}
				/* Seek for sync */
				for (; ptr < endp - 3; ptr++) {
					if (*(ptr + 3) == 0x47)
						break;
				}
				/* Get message header */
				header = *(unsigned long *)ptr;
				ptr += 4;
			}

			/* split the header fields */
			size = ((header & 0x7e) << 1);
			if (size > 0)
				size -= 4;
			block = (header >> 7) & 0xf;
			field = (header >> 11) & 0x1;
			line  = (header >> 12) & 0x1ff;
			cmd   = (header >> 21) & 0x7;
			/* Validates haeder fields */
			if (size > TM6000_URB_MSG_LEN)
				size = TM6000_URB_MSG_LEN;
			pktsize = TM6000_URB_MSG_LEN;
			/*
			 * calculate position in buffer and change the buffer
			 */
			switch (cmd) {
			case TM6000_URB_MSG_VIDEO:
				if (!dev->radio) {
					if ((dev->isoc_ctl.vfield != field) &&
						(field == 1)) {
						/*
						 * Announces that a new buffer
						 * were filled
						 */
						buffer_filled(dev, dma_q, vbuf);
						dprintk(dev, V4L2_DEBUG_ISOC,
							"new buffer filled\n");
						get_next_buf(dma_q, &vbuf);
						if (!vbuf)
							return rc;
						voutp = videobuf_to_vmalloc(&vbuf->vb);
						if (!voutp)
							return rc;
						memset(voutp, 0, vbuf->vb.size);
					}
					linewidth = vbuf->vb.width << 1;
					pos = ((line << 1) - field - 1) *
					linewidth + block * TM6000_URB_MSG_LEN;
					/* Don't allow to write out of the buffer */
					if (pos + size > vbuf->vb.size)
						cmd = TM6000_URB_MSG_ERR;
					dev->isoc_ctl.vfield = field;
				}
				break;
			case TM6000_URB_MSG_VBI:
				break;
			case TM6000_URB_MSG_AUDIO:
			case TM6000_URB_MSG_PTS:
				size = pktsize; /* Size is always 180 bytes */
				break;
			}
		} else {
			/* Continue the last copy */
			cmd = dev->isoc_ctl.cmd;
			size = dev->isoc_ctl.size;
			pos = dev->isoc_ctl.pos;
			pktsize = dev->isoc_ctl.pktsize;
			field = dev->isoc_ctl.field;
		}
		cpysize = (endp - ptr > size) ? size : endp - ptr;
		if (cpysize) {
			/* copy data in different buffers */
			switch (cmd) {
			case TM6000_URB_MSG_VIDEO:
				/* Fills video buffer */
				if (vbuf)
					memcpy(&voutp[pos], ptr, cpysize);
				break;
			case TM6000_URB_MSG_AUDIO: {
				int i;
				for (i = 0; i < cpysize; i += 2)
					swab16s((u16 *)(ptr + i));

				tm6000_call_fillbuf(dev, TM6000_AUDIO, ptr, cpysize);
				break;
			}
			case TM6000_URB_MSG_VBI:
				/* Need some code to copy vbi buffer */
				break;
			case TM6000_URB_MSG_PTS: {
				/* Need some code to copy pts */
				u32 pts;
				pts = *(u32 *)ptr;
				dprintk(dev, V4L2_DEBUG_ISOC, "field %d, PTS %x",
					field, pts);
				break;
			}
			}
		}
		if (ptr + pktsize > endp) {
			/*
			 * End of URB packet, but cmd processing is not
			 * complete. Preserve the state for a next packet
			 */
			dev->isoc_ctl.pos = pos + cpysize;
			dev->isoc_ctl.size = size - cpysize;
			dev->isoc_ctl.cmd = cmd;
			dev->isoc_ctl.field = field;
			dev->isoc_ctl.pktsize = pktsize - (endp - ptr);
			ptr += endp - ptr;
		} else {
			dev->isoc_ctl.cmd = 0;
			ptr += pktsize;
		}
	}
	return 0;
}

/*
 * Identify the tm5600/6000 buffer header type and properly handles
 */
static int copy_multiplexed(u8 *ptr, unsigned long len,
			struct urb *urb)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);
	unsigned int pos = dev->isoc_ctl.pos, cpysize;
	int rc = 1;
	struct tm6000_buffer *buf;
	char *outp = NULL;

	get_next_buf(dma_q, &buf);
	if (buf)
		outp = videobuf_to_vmalloc(&buf->vb);

	if (!outp)
		return 0;

	while (len > 0) {
		cpysize = min(len, buf->vb.size-pos);
		memcpy(&outp[pos], ptr, cpysize);
		pos += cpysize;
		ptr += cpysize;
		len -= cpysize;
		if (pos >= buf->vb.size) {
			pos = 0;
			/* Announces that a new buffer were filled */
			buffer_filled(dev, dma_q, buf);
			dprintk(dev, V4L2_DEBUG_ISOC, "new buffer filled\n");
			get_next_buf(dma_q, &buf);
			if (!buf)
				break;
			outp = videobuf_to_vmalloc(&(buf->vb));
			if (!outp)
				return rc;
			pos = 0;
		}
	}

	dev->isoc_ctl.pos = pos;
	return rc;
}

static inline void print_err_status(struct tm6000_core *dev,
				     int packet, int status)
{
	char *errmsg = "Unknown";

	switch (status) {
	case -ENOENT:
		errmsg = "unlinked synchronuously";
		break;
	case -ECONNRESET:
		errmsg = "unlinked asynchronuously";
		break;
	case -ENOSR:
		errmsg = "Buffer error (overrun)";
		break;
	case -EPIPE:
		errmsg = "Stalled (device not responding)";
		break;
	case -EOVERFLOW:
		errmsg = "Babble (bad cable?)";
		break;
	case -EPROTO:
		errmsg = "Bit-stuff error (bad cable?)";
		break;
	case -EILSEQ:
		errmsg = "CRC/Timeout (could be anything)";
		break;
	case -ETIME:
		errmsg = "Device does not respond";
		break;
	}
	if (packet < 0) {
		dprintk(dev, V4L2_DEBUG_QUEUE, "URB status %d [%s].\n",
			status, errmsg);
	} else {
		dprintk(dev, V4L2_DEBUG_QUEUE, "URB packet %d, status %d [%s].\n",
			packet, status, errmsg);
	}
}


/*
 * Controls the isoc copy of each urb packet
 */
static inline int tm6000_isoc_copy(struct urb *urb)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);
	int i, len = 0, rc = 1, status;
	char *p;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		return 0;
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		status = urb->iso_frame_desc[i].status;

		if (status < 0) {
			print_err_status(dev, i, status);
			continue;
		}

		len = urb->iso_frame_desc[i].actual_length;

		if (len > 0) {
			p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
			if (!urb->iso_frame_desc[i].status) {
				if ((dev->fourcc) == V4L2_PIX_FMT_TM6000) {
					rc = copy_multiplexed(p, len, urb);
					if (rc <= 0)
						return rc;
				} else {
					copy_streams(p, len, urb);
				}
			}
		}
	}
	return rc;
}

/* ------------------------------------------------------------------
 *	URB control
 * ------------------------------------------------------------------
 */

/*
 * IRQ callback, called by URB callback
 */
static void tm6000_irq_callback(struct urb *urb)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);
	int i;

	switch (urb->status) {
	case 0:
	case -ETIMEDOUT:
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;

	default:
		tm6000_err("urb completion error %d.\n", urb->status);
		break;
	}

	spin_lock(&dev->slock);
	tm6000_isoc_copy(urb);
	spin_unlock(&dev->slock);

	/* Reset urb buffers */
	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	urb->status = usb_submit_urb(urb, GFP_ATOMIC);
	if (urb->status)
		tm6000_err("urb resubmit failed (error=%i)\n",
			urb->status);
}

/*
 * Allocate URB buffers
 */
static int tm6000_alloc_urb_buffers(struct tm6000_core *dev)
{
	int num_bufs = TM6000_NUM_URB_BUF;
	int i;

	if (dev->urb_buffer != NULL)
		return 0;

	dev->urb_buffer = kmalloc(sizeof(void *)*num_bufs, GFP_KERNEL);
	if (!dev->urb_buffer) {
		tm6000_err("cannot allocate memory for urb buffers\n");
		return -ENOMEM;
	}

	dev->urb_dma = kmalloc(sizeof(dma_addr_t *)*num_bufs, GFP_KERNEL);
	if (!dev->urb_dma) {
		tm6000_err("cannot allocate memory for urb dma pointers\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_bufs; i++) {
		dev->urb_buffer[i] = usb_alloc_coherent(
					dev->udev, dev->urb_size,
					GFP_KERNEL, &dev->urb_dma[i]);
		if (!dev->urb_buffer[i]) {
			tm6000_err("unable to allocate %i bytes for transfer buffer %i\n",
				    dev->urb_size, i);
			return -ENOMEM;
		}
		memset(dev->urb_buffer[i], 0, dev->urb_size);
	}

	return 0;
}

/*
 * Free URB buffers
 */
static int tm6000_free_urb_buffers(struct tm6000_core *dev)
{
	int i;

	if (dev->urb_buffer == NULL)
		return 0;

	for (i = 0; i < TM6000_NUM_URB_BUF; i++) {
		if (dev->urb_buffer[i]) {
			usb_free_coherent(dev->udev,
					dev->urb_size,
					dev->urb_buffer[i],
					dev->urb_dma[i]);
			dev->urb_buffer[i] = NULL;
		}
	}
	kfree(dev->urb_buffer);
	kfree(dev->urb_dma);
	dev->urb_buffer = NULL;
	dev->urb_dma = NULL;

	return 0;
}

/*
 * Stop and Deallocate URBs
 */
static void tm6000_uninit_isoc(struct tm6000_core *dev)
{
	struct urb *urb;
	int i;

	dev->isoc_ctl.buf = NULL;
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb = dev->isoc_ctl.urb[i];
		if (urb) {
			usb_kill_urb(urb);
			usb_unlink_urb(urb);
			usb_free_urb(urb);
			dev->isoc_ctl.urb[i] = NULL;
		}
		dev->isoc_ctl.transfer_buffer[i] = NULL;
	}

	if (!keep_urb)
		tm6000_free_urb_buffers(dev);

	kfree(dev->isoc_ctl.urb);
	kfree(dev->isoc_ctl.transfer_buffer);

	dev->isoc_ctl.urb = NULL;
	dev->isoc_ctl.transfer_buffer = NULL;
	dev->isoc_ctl.num_bufs = 0;
}

/*
 * Assign URBs and start IRQ
 */
static int tm6000_prepare_isoc(struct tm6000_core *dev)
{
	struct tm6000_dmaqueue *dma_q = &dev->vidq;
	int i, j, sb_size, pipe, size, max_packets;
	int num_bufs = TM6000_NUM_URB_BUF;
	struct urb *urb;

	/* De-allocates all pending stuff */
	tm6000_uninit_isoc(dev);
	/* Stop interrupt USB pipe */
	tm6000_ir_int_stop(dev);

	usb_set_interface(dev->udev,
			  dev->isoc_in.bInterfaceNumber,
			  dev->isoc_in.bAlternateSetting);

	/* Start interrupt USB pipe */
	tm6000_ir_int_start(dev);

	pipe = usb_rcvisocpipe(dev->udev,
			       dev->isoc_in.endp->desc.bEndpointAddress &
			       USB_ENDPOINT_NUMBER_MASK);

	size = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));

	if (size > dev->isoc_in.maxsize)
		size = dev->isoc_in.maxsize;

	dev->isoc_ctl.max_pkt_size = size;

	max_packets = TM6000_MAX_ISO_PACKETS;
	sb_size = max_packets * size;
	dev->urb_size = sb_size;

	dev->isoc_ctl.num_bufs = num_bufs;

	dev->isoc_ctl.urb = kmalloc(sizeof(void *)*num_bufs, GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		tm6000_err("cannot alloc memory for usb buffers\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kmalloc(sizeof(void *)*num_bufs,
				   GFP_KERNEL);
	if (!dev->isoc_ctl.transfer_buffer) {
		tm6000_err("cannot allocate memory for usbtransfer\n");
		kfree(dev->isoc_ctl.urb);
		return -ENOMEM;
	}

	dprintk(dev, V4L2_DEBUG_QUEUE, "Allocating %d x %d packets"
		    " (%d bytes) of %d bytes each to handle %u size\n",
		    max_packets, num_bufs, sb_size,
		    dev->isoc_in.maxsize, size);


	if (!dev->urb_buffer && tm6000_alloc_urb_buffers(dev) < 0) {
		tm6000_err("cannot allocate memory for urb buffers\n");

		/* call free, as some buffers might have been allocated */
		tm6000_free_urb_buffers(dev);
		kfree(dev->isoc_ctl.urb);
		kfree(dev->isoc_ctl.transfer_buffer);
		return -ENOMEM;
	}

	/* allocate urbs and transfer buffers */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb = usb_alloc_urb(max_packets, GFP_KERNEL);
		if (!urb) {
			tm6000_err("cannot alloc isoc_ctl.urb %i\n", i);
			tm6000_uninit_isoc(dev);
			usb_free_urb(urb);
			return -ENOMEM;
		}
		dev->isoc_ctl.urb[i] = urb;

		urb->transfer_dma = dev->urb_dma[i];
		dev->isoc_ctl.transfer_buffer[i] = dev->urb_buffer[i];

		usb_fill_bulk_urb(urb, dev->udev, pipe,
				  dev->isoc_ctl.transfer_buffer[i], sb_size,
				  tm6000_irq_callback, dma_q);
		urb->interval = dev->isoc_in.endp->desc.bInterval;
		urb->number_of_packets = max_packets;
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;

		for (j = 0; j < max_packets; j++) {
			urb->iso_frame_desc[j].offset = size * j;
			urb->iso_frame_desc[j].length = size;
		}
	}

	return 0;
}

static int tm6000_start_thread(struct tm6000_core *dev)
{
	struct tm6000_dmaqueue *dma_q = &dev->vidq;
	int i;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	init_waitqueue_head(&dma_q->wq);

	/* submit urbs and enables IRQ */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		int rc = usb_submit_urb(dev->isoc_ctl.urb[i], GFP_ATOMIC);
		if (rc) {
			tm6000_err("submit of urb %i failed (error=%i)\n", i,
				   rc);
			tm6000_uninit_isoc(dev);
			return rc;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------
 *	Videobuf operations
 * ------------------------------------------------------------------
 */

static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct tm6000_fh *fh = vq->priv_data;

	*size = fh->fmt->depth * fh->width * fh->height >> 3;
	if (0 == *count)
		*count = TM6000_DEF_BUF;

	if (*count < TM6000_MIN_BUF)
		*count = TM6000_MIN_BUF;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct tm6000_buffer *buf)
{
	struct tm6000_fh *fh = vq->priv_data;
	struct tm6000_core   *dev = fh->dev;
	unsigned long flags;

	if (in_interrupt())
		BUG();

	/* We used to wait for the buffer to finish here, but this didn't work
	   because, as we were keeping the state as VIDEOBUF_QUEUED,
	   videobuf_queue_cancel marked it as finished for us.
	   (Also, it could wedge forever if the hardware was misconfigured.)

	   This should be safe; by the time we get here, the buffer isn't
	   queued anymore. If we ever start marking the buffers as
	   VIDEOBUF_ACTIVE, it won't be, though.
	*/
	spin_lock_irqsave(&dev->slock, flags);
	if (dev->isoc_ctl.buf == buf)
		dev->isoc_ctl.buf = NULL;
	spin_unlock_irqrestore(&dev->slock, flags);

	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct tm6000_fh     *fh  = vq->priv_data;
	struct tm6000_buffer *buf = container_of(vb, struct tm6000_buffer, vb);
	struct tm6000_core   *dev = fh->dev;
	int rc = 0;

	BUG_ON(NULL == fh->fmt);


	/* FIXME: It assumes depth=2 */
	/* The only currently supported format is 16 bits/pixel */
	buf->vb.size = fh->fmt->depth*fh->width*fh->height >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != fh->fmt    ||
	    buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	    buf->vb.field  != field) {
		buf->fmt       = fh->fmt;
		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field  = field;
		buf->vb.state = VIDEOBUF_NEEDS_INIT;
	}

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc != 0)
			goto fail;
	}

	if (!dev->isoc_ctl.num_bufs) {
		rc = tm6000_prepare_isoc(dev);
		if (rc < 0)
			goto fail;

		rc = tm6000_start_thread(dev);
		if (rc < 0)
			goto fail;

	}

	buf->vb.state = VIDEOBUF_PREPARED;
	return 0;

fail:
	free_buffer(vq, buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct tm6000_buffer    *buf     = container_of(vb, struct tm6000_buffer, vb);
	struct tm6000_fh        *fh      = vq->priv_data;
	struct tm6000_core      *dev     = fh->dev;
	struct tm6000_dmaqueue  *vidq    = &dev->vidq;

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct tm6000_buffer   *buf  = container_of(vb, struct tm6000_buffer, vb);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops tm6000_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
 *	IOCTL handling
 * ------------------------------------------------------------------
 */

static bool is_res_read(struct tm6000_core *dev, struct tm6000_fh *fh)
{
	/* Is the current fh handling it? if so, that's OK */
	if (dev->resources == fh && dev->is_res_read)
		return true;

	return false;
}

static bool is_res_streaming(struct tm6000_core *dev, struct tm6000_fh *fh)
{
	/* Is the current fh handling it? if so, that's OK */
	if (dev->resources == fh)
		return true;

	return false;
}

static bool res_get(struct tm6000_core *dev, struct tm6000_fh *fh,
		   bool is_res_read)
{
	/* Is the current fh handling it? if so, that's OK */
	if (dev->resources == fh && dev->is_res_read == is_res_read)
		return true;

	/* is it free? */
	if (dev->resources)
		return false;

	/* grab it */
	dev->resources = fh;
	dev->is_res_read = is_res_read;
	dprintk(dev, V4L2_DEBUG_RES_LOCK, "res: get\n");
	return true;
}

static void res_free(struct tm6000_core *dev, struct tm6000_fh *fh)
{
	/* Is the current fh handling it? if so, that's OK */
	if (dev->resources != fh)
		return;

	dev->resources = NULL;
	dprintk(dev, V4L2_DEBUG_RES_LOCK, "res: put\n");
}

/* ------------------------------------------------------------------
 *	IOCTL vidioc handling
 * ------------------------------------------------------------------
 */
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct tm6000_core *dev = ((struct tm6000_fh *)priv)->dev;
	struct video_device *vdev = video_devdata(file);

	strlcpy(cap->driver, "tm6000", sizeof(cap->driver));
	strlcpy(cap->card, "Trident TVMaster TM5600/6000/6010", sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	if (dev->tuner_type != TUNER_ABSENT)
		cap->device_caps |= V4L2_CAP_TUNER;
	if (vdev->vfl_type == VFL_TYPE_GRABBER)
		cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING |
				V4L2_CAP_READWRITE;
	else
		cap->device_caps |= V4L2_CAP_RADIO;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS |
		V4L2_CAP_RADIO | V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(format))
		return -EINVAL;

	strlcpy(f->description, format[f->index].name, sizeof(f->description));
	f->pixelformat = format[f->index].fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct tm6000_fh  *fh = priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.priv = 0;

	return 0;
}

static struct tm6000_fmt *format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format); i++)
		if (format[i].fourcc == fourcc)
			return format+i;
	return NULL;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct tm6000_core *dev = ((struct tm6000_fh *)priv)->dev;
	struct tm6000_fmt *fmt;
	enum v4l2_field field;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt) {
		dprintk(dev, V4L2_DEBUG_IOCTL_ARG, "Fourcc format (0x%08x)"
				" invalid.\n", f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	field = f->fmt.pix.field;

	field = V4L2_FIELD_INTERLACED;

	tm6000_get_std_res(dev);

	f->fmt.pix.width  = dev->width;
	f->fmt.pix.height = dev->height;

	f->fmt.pix.width &= ~0x01;

	f->fmt.pix.field = field;
	f->fmt.pix.priv = 0;

	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct tm6000_fh  *fh = priv;
	struct tm6000_core *dev = fh->dev;
	int ret = vidioc_try_fmt_vid_cap(file, fh, f);
	if (ret < 0)
		return ret;

	fh->fmt           = format_by_fourcc(f->fmt.pix.pixelformat);
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	dev->fourcc       = f->fmt.pix.pixelformat;

	tm6000_set_fourcc_format(dev);

	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			   struct v4l2_requestbuffers *p)
{
	struct tm6000_fh  *fh = priv;

	return videobuf_reqbufs(&fh->vb_vidq, p);
}

static int vidioc_querybuf(struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct tm6000_fh  *fh = priv;

	return videobuf_querybuf(&fh->vb_vidq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct tm6000_fh  *fh = priv;

	return videobuf_qbuf(&fh->vb_vidq, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct tm6000_fh  *fh = priv;

	return videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct tm6000_fh *fh = priv;
	struct tm6000_core *dev = fh->dev;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	if (!res_get(dev, fh, false))
		return -EBUSY;
	return videobuf_streamon(&fh->vb_vidq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct tm6000_fh *fh = priv;
	struct tm6000_core *dev = fh->dev;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (i != fh->type)
		return -EINVAL;

	videobuf_streamoff(&fh->vb_vidq);
	res_free(dev, fh);

	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id norm)
{
	int rc = 0;
	struct tm6000_fh *fh = priv;
	struct tm6000_core *dev = fh->dev;

	dev->norm = norm;
	rc = tm6000_init_analog_mode(dev);

	fh->width  = dev->width;
	fh->height = dev->height;

	if (rc < 0)
		return rc;

	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_std, dev->norm);

	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct tm6000_fh *fh = priv;
	struct tm6000_core *dev = fh->dev;

	*norm = dev->norm;
	return 0;
}

static const char *iname[] = {
	[TM6000_INPUT_TV] = "Television",
	[TM6000_INPUT_COMPOSITE1] = "Composite 1",
	[TM6000_INPUT_COMPOSITE2] = "Composite 2",
	[TM6000_INPUT_SVIDEO] = "S-Video",
};

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct tm6000_fh   *fh = priv;
	struct tm6000_core *dev = fh->dev;
	unsigned int n;

	n = i->index;
	if (n >= 3)
		return -EINVAL;

	if (!dev->vinput[n].type)
		return -EINVAL;

	i->index = n;

	if (dev->vinput[n].type == TM6000_INPUT_TV)
		i->type = V4L2_INPUT_TYPE_TUNER;
	else
		i->type = V4L2_INPUT_TYPE_CAMERA;

	strcpy(i->name, iname[dev->vinput[n].type]);

	i->std = TM6000_STD;

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct tm6000_fh   *fh = priv;
	struct tm6000_core *dev = fh->dev;

	*i = dev->input;

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct tm6000_fh   *fh = priv;
	struct tm6000_core *dev = fh->dev;
	int rc = 0;

	if (i >= 3)
		return -EINVAL;
	if (!dev->vinput[i].type)
		return -EINVAL;

	dev->input = i;

	rc = vidioc_s_std(file, priv, dev->norm);

	return rc;
}

/* --- controls ---------------------------------------------- */

static int tm6000_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tm6000_core *dev = container_of(ctrl->handler, struct tm6000_core, ctrl_handler);
	u8  val = ctrl->val;

	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		tm6000_set_reg(dev, TM6010_REQ07_R08_LUMA_CONTRAST_ADJ, val);
		return 0;
	case V4L2_CID_BRIGHTNESS:
		tm6000_set_reg(dev, TM6010_REQ07_R09_LUMA_BRIGHTNESS_ADJ, val);
		return 0;
	case V4L2_CID_SATURATION:
		tm6000_set_reg(dev, TM6010_REQ07_R0A_CHROMA_SATURATION_ADJ, val);
		return 0;
	case V4L2_CID_HUE:
		tm6000_set_reg(dev, TM6010_REQ07_R0B_CHROMA_HUE_PHASE_ADJ, val);
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops tm6000_ctrl_ops = {
	.s_ctrl = tm6000_s_ctrl,
};

static int tm6000_radio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tm6000_core *dev = container_of(ctrl->handler,
			struct tm6000_core, radio_ctrl_handler);
	u8  val = ctrl->val;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		dev->ctl_mute = val;
		tm6000_tvaudio_set_mute(dev, val);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		dev->ctl_volume = val;
		tm6000_set_volume(dev, val);
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops tm6000_radio_ctrl_ops = {
	.s_ctrl = tm6000_radio_s_ctrl,
};

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct tm6000_fh   *fh  = priv;
	struct tm6000_core *dev = fh->dev;

	if (UNSET == dev->tuner_type)
		return -ENOTTY;
	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Television");
	t->type       = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO;
	t->rangehigh  = 0xffffffffUL;
	t->rxsubchans = V4L2_TUNER_SUB_STEREO;

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_tuner, t);

	t->audmode = dev->amode;

	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct tm6000_fh   *fh  = priv;
	struct tm6000_core *dev = fh->dev;

	if (UNSET == dev->tuner_type)
		return -ENOTTY;
	if (0 != t->index)
		return -EINVAL;

	if (t->audmode > V4L2_TUNER_MODE_STEREO)
		dev->amode = V4L2_TUNER_MODE_STEREO;
	else
		dev->amode = t->audmode;
	dprintk(dev, 3, "audio mode: %x\n", t->audmode);

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_tuner, t);

	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct tm6000_fh   *fh  = priv;
	struct tm6000_core *dev = fh->dev;

	if (UNSET == dev->tuner_type)
		return -ENOTTY;
	if (f->tuner)
		return -EINVAL;

	f->frequency = dev->freq;

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_frequency, f);

	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct tm6000_fh   *fh  = priv;
	struct tm6000_core *dev = fh->dev;

	if (UNSET == dev->tuner_type)
		return -ENOTTY;
	if (f->tuner != 0)
		return -EINVAL;

	dev->freq = f->frequency;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, f);

	return 0;
}

static int radio_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct tm6000_fh *fh = file->private_data;
	struct tm6000_core *dev = fh->dev;

	if (0 != t->index)
		return -EINVAL;

	memset(t, 0, sizeof(*t));
	strcpy(t->name, "Radio");
	t->type = V4L2_TUNER_RADIO;
	t->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
	t->rxsubchans = V4L2_TUNER_SUB_STEREO;
	t->audmode = V4L2_TUNER_MODE_STEREO;

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_tuner, t);

	return 0;
}

static int radio_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t)
{
	struct tm6000_fh *fh = file->private_data;
	struct tm6000_core *dev = fh->dev;

	if (0 != t->index)
		return -EINVAL;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_tuner, t);
	return 0;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int __tm6000_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct tm6000_core *dev = video_drvdata(file);
	struct tm6000_fh *fh;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int rc;
	int radio = 0;

	dprintk(dev, V4L2_DEBUG_OPEN, "tm6000: open called (dev=%s)\n",
		video_device_node_name(vdev));

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		break;
	case VFL_TYPE_VBI:
		type = V4L2_BUF_TYPE_VBI_CAPTURE;
		break;
	case VFL_TYPE_RADIO:
		radio = 1;
		break;
	}

	/* If more than one user, mutex should be added */
	dev->users++;

	dprintk(dev, V4L2_DEBUG_OPEN, "open dev=%s type=%s users=%d\n",
		video_device_node_name(vdev), v4l2_type_names[type],
		dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		return -ENOMEM;
	}

	v4l2_fh_init(&fh->fh, vdev);
	file->private_data = fh;
	fh->dev      = dev;
	fh->radio    = radio;
	dev->radio   = radio;
	fh->type     = type;
	dev->fourcc  = format[0].fourcc;

	fh->fmt      = format_by_fourcc(dev->fourcc);

	tm6000_get_std_res(dev);

	fh->width = dev->width;
	fh->height = dev->height;

	dprintk(dev, V4L2_DEBUG_OPEN, "Open: fh=0x%08lx, dev=0x%08lx, "
						"dev->vidq=0x%08lx\n",
			(unsigned long)fh, (unsigned long)dev,
			(unsigned long)&dev->vidq);
	dprintk(dev, V4L2_DEBUG_OPEN, "Open: list_empty "
				"queued=%d\n", list_empty(&dev->vidq.queued));
	dprintk(dev, V4L2_DEBUG_OPEN, "Open: list_empty "
				"active=%d\n", list_empty(&dev->vidq.active));

	/* initialize hardware on analog mode */
	rc = tm6000_init_analog_mode(dev);
	if (rc < 0)
		return rc;

	dev->mode = TM6000_MODE_ANALOG;

	if (!fh->radio) {
		videobuf_queue_vmalloc_init(&fh->vb_vidq, &tm6000_video_qops,
				NULL, &dev->slock,
				fh->type,
				V4L2_FIELD_INTERLACED,
				sizeof(struct tm6000_buffer), fh, &dev->lock);
	} else {
		dprintk(dev, V4L2_DEBUG_OPEN, "video_open: setting radio device\n");
		tm6000_set_audio_rinput(dev);
		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_radio);
		tm6000_prepare_isoc(dev);
		tm6000_start_thread(dev);
	}
	v4l2_fh_add(&fh->fh);

	return 0;
}

static int tm6000_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	int res;

	mutex_lock(vdev->lock);
	res = __tm6000_open(file);
	mutex_unlock(vdev->lock);
	return res;
}

static ssize_t
tm6000_read(struct file *file, char __user *data, size_t count, loff_t *pos)
{
	struct tm6000_fh *fh = file->private_data;
	struct tm6000_core *dev = fh->dev;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		int res;

		if (!res_get(fh->dev, fh, true))
			return -EBUSY;

		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
		res = videobuf_read_stream(&fh->vb_vidq, data, count, pos, 0,
					file->f_flags & O_NONBLOCK);
		mutex_unlock(&dev->lock);
		return res;
	}
	return 0;
}

static unsigned int
__tm6000_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct tm6000_fh        *fh = file->private_data;
	struct tm6000_buffer    *buf;
	int res = 0;

	if (v4l2_event_pending(&fh->fh))
		res = POLLPRI;
	else if (req_events & POLLPRI)
		poll_wait(file, &fh->fh.wait, wait);
	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return res | POLLERR;

	if (!!is_res_streaming(fh->dev, fh))
		return res | POLLERR;

	if (!is_res_read(fh->dev, fh)) {
		/* streaming capture */
		if (list_empty(&fh->vb_vidq.stream))
			return res | POLLERR;
		buf = list_entry(fh->vb_vidq.stream.next, struct tm6000_buffer, vb.stream);
		poll_wait(file, &buf->vb.done, wait);
		if (buf->vb.state == VIDEOBUF_DONE ||
		    buf->vb.state == VIDEOBUF_ERROR)
			return res | POLLIN | POLLRDNORM;
	} else if (req_events & (POLLIN | POLLRDNORM)) {
		/* read() capture */
		return res | videobuf_poll_stream(file, &fh->vb_vidq, wait);
	}
	return res;
}

static unsigned int tm6000_poll(struct file *file, struct poll_table_struct *wait)
{
	struct tm6000_fh *fh = file->private_data;
	struct tm6000_core *dev = fh->dev;
	unsigned int res;

	mutex_lock(&dev->lock);
	res = __tm6000_poll(file, wait);
	mutex_unlock(&dev->lock);
	return res;
}

static int tm6000_release(struct file *file)
{
	struct tm6000_fh         *fh = file->private_data;
	struct tm6000_core      *dev = fh->dev;
	struct video_device    *vdev = video_devdata(file);

	dprintk(dev, V4L2_DEBUG_OPEN, "tm6000: close called (dev=%s, users=%d)\n",
		video_device_node_name(vdev), dev->users);

	mutex_lock(&dev->lock);
	dev->users--;

	res_free(dev, fh);

	if (!dev->users) {
		tm6000_uninit_isoc(dev);

		/* Stop interrupt USB pipe */
		tm6000_ir_int_stop(dev);

		usb_reset_configuration(dev->udev);

		if (dev->int_in.endp)
			usb_set_interface(dev->udev,
					dev->isoc_in.bInterfaceNumber, 2);
		else
			usb_set_interface(dev->udev,
					dev->isoc_in.bInterfaceNumber, 0);

		/* Start interrupt USB pipe */
		tm6000_ir_int_start(dev);

		if (!fh->radio)
			videobuf_mmap_free(&fh->vb_vidq);
	}
	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	kfree(fh);
	mutex_unlock(&dev->lock);

	return 0;
}

static int tm6000_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct tm6000_fh *fh = file->private_data;
	struct tm6000_core *dev = fh->dev;
	int res;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	res = videobuf_mmap_mapper(&fh->vb_vidq, vma);
	mutex_unlock(&dev->lock);
	return res;
}

static struct v4l2_file_operations tm6000_fops = {
	.owner = THIS_MODULE,
	.open = tm6000_open,
	.release = tm6000_release,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.read = tm6000_read,
	.poll = tm6000_poll,
	.mmap = tm6000_mmap,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap          = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_s_std             = vidioc_s_std,
	.vidioc_g_std             = vidioc_g_std,
	.vidioc_enum_input        = vidioc_enum_input,
	.vidioc_g_input           = vidioc_g_input,
	.vidioc_s_input           = vidioc_s_input,
	.vidioc_g_tuner           = vidioc_g_tuner,
	.vidioc_s_tuner           = vidioc_s_tuner,
	.vidioc_g_frequency       = vidioc_g_frequency,
	.vidioc_s_frequency       = vidioc_s_frequency,
	.vidioc_streamon          = vidioc_streamon,
	.vidioc_streamoff         = vidioc_streamoff,
	.vidioc_reqbufs           = vidioc_reqbufs,
	.vidioc_querybuf          = vidioc_querybuf,
	.vidioc_qbuf              = vidioc_qbuf,
	.vidioc_dqbuf             = vidioc_dqbuf,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device tm6000_template = {
	.name		= "tm6000",
	.fops           = &tm6000_fops,
	.ioctl_ops      = &video_ioctl_ops,
	.release	= video_device_release,
	.tvnorms        = TM6000_STD,
};

static const struct v4l2_file_operations radio_fops = {
	.owner		= THIS_MODULE,
	.open		= tm6000_open,
	.poll		= v4l2_ctrl_poll,
	.release	= tm6000_release,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops radio_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_g_tuner		= radio_g_tuner,
	.vidioc_s_tuner		= radio_s_tuner,
	.vidioc_g_frequency	= vidioc_g_frequency,
	.vidioc_s_frequency	= vidioc_s_frequency,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device tm6000_radio_template = {
	.name			= "tm6000",
	.fops			= &radio_fops,
	.ioctl_ops		= &radio_ioctl_ops,
};

/* -----------------------------------------------------------------
 *	Initialization and module stuff
 * ------------------------------------------------------------------
 */

static struct video_device *vdev_init(struct tm6000_core *dev,
		const struct video_device
		*template, const char *type_name)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;

	*vfd = *template;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->release = video_device_release;
	vfd->debug = tm6000_debug;
	vfd->lock = &dev->lock;

	snprintf(vfd->name, sizeof(vfd->name), "%s %s", dev->name, type_name);

	video_set_drvdata(vfd, dev);
	return vfd;
}

int tm6000_v4l2_register(struct tm6000_core *dev)
{
	int ret = 0;

	v4l2_ctrl_handler_init(&dev->ctrl_handler, 6);
	v4l2_ctrl_handler_init(&dev->radio_ctrl_handler, 2);
	v4l2_ctrl_new_std(&dev->radio_ctrl_handler, &tm6000_radio_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&dev->radio_ctrl_handler, &tm6000_radio_ctrl_ops,
			V4L2_CID_AUDIO_VOLUME, -15, 15, 1, 0);
	v4l2_ctrl_new_std(&dev->ctrl_handler, &tm6000_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 54);
	v4l2_ctrl_new_std(&dev->ctrl_handler, &tm6000_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 119);
	v4l2_ctrl_new_std(&dev->ctrl_handler, &tm6000_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 112);
	v4l2_ctrl_new_std(&dev->ctrl_handler, &tm6000_ctrl_ops,
			V4L2_CID_HUE, -128, 127, 1, 0);
	v4l2_ctrl_add_handler(&dev->ctrl_handler,
			&dev->radio_ctrl_handler, NULL);

	if (dev->radio_ctrl_handler.error)
		ret = dev->radio_ctrl_handler.error;
	if (!ret && dev->ctrl_handler.error)
		ret = dev->ctrl_handler.error;
	if (ret)
		goto free_ctrl;

	dev->vfd = vdev_init(dev, &tm6000_template, "video");

	if (!dev->vfd) {
		printk(KERN_INFO "%s: can't register video device\n",
		       dev->name);
		ret = -ENOMEM;
		goto free_ctrl;
	}
	dev->vfd->ctrl_handler = &dev->ctrl_handler;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);

	ret = video_register_device(dev->vfd, VFL_TYPE_GRABBER, video_nr);

	if (ret < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       dev->name);
		video_device_release(dev->vfd);
		dev->vfd = NULL;
		goto free_ctrl;
	}

	printk(KERN_INFO "%s: registered device %s\n",
	       dev->name, video_device_node_name(dev->vfd));

	if (dev->caps.has_radio) {
		dev->radio_dev = vdev_init(dev, &tm6000_radio_template,
							   "radio");
		if (!dev->radio_dev) {
			printk(KERN_INFO "%s: can't register radio device\n",
			       dev->name);
			ret = -ENXIO;
			goto unreg_video;
		}

		dev->radio_dev->ctrl_handler = &dev->radio_ctrl_handler;
		ret = video_register_device(dev->radio_dev, VFL_TYPE_RADIO,
					    radio_nr);
		if (ret < 0) {
			printk(KERN_INFO "%s: can't register radio device\n",
			       dev->name);
			video_device_release(dev->radio_dev);
			goto unreg_video;
		}

		printk(KERN_INFO "%s: registered device %s\n",
		       dev->name, video_device_node_name(dev->radio_dev));
	}

	printk(KERN_INFO "Trident TVMaster TM5600/TM6000/TM6010 USB2 board (Load status: %d)\n", ret);
	return ret;

unreg_video:
	video_unregister_device(dev->vfd);
free_ctrl:
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	v4l2_ctrl_handler_free(&dev->radio_ctrl_handler);
	return ret;
}

int tm6000_v4l2_unregister(struct tm6000_core *dev)
{
	video_unregister_device(dev->vfd);

	/* if URB buffers are still allocated free them now */
	tm6000_free_urb_buffers(dev);

	if (dev->radio_dev) {
		if (video_is_registered(dev->radio_dev))
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}

	return 0;
}

int tm6000_v4l2_exit(void)
{
	return 0;
}

module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr, "Allow changing video device number");

module_param_named(debug, tm6000_debug, int, 0444);
MODULE_PARM_DESC(debug, "activates debug info");

module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

module_param(keep_urb, bool, 0);
MODULE_PARM_DESC(keep_urb, "Keep urb buffers allocated even when the device is closed by the user");
