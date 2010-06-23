/*
   tm6000-video.c - driver for TM5600/TM6000/TM6010 USB video capture devices

   Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

   Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>
	- Fixed module load/unload

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
#include <linux/version.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <media/v4l2-ioctl.h>
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

#define TM6000_MAX_ISO_PACKETS	46	/* Max number of ISO packets */

/* Declare static vars that will be used as parameters */
static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */
static int video_nr = -1;		/* /dev/videoN, -1 for autodetect */

/* Debug level */
int tm6000_debug;

/* supported controls */
static struct v4l2_queryctrl tm6000_qctrl[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 54,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 119,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 112,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 0x1,
		.default_value = 0,
		.flags         = 0,
	}
};

static int qctl_regs[ARRAY_SIZE(tm6000_qctrl)];

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
	DMA and thread functions
   ------------------------------------------------------------------*/

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
	char *outp;

	if (list_empty(&dma_q->active)) {
		dprintk(dev, V4L2_DEBUG_QUEUE, "No active queue to serve\n");
		*buf = NULL;
		return;
	}

	*buf = list_entry(dma_q->active.next,
			struct tm6000_buffer, vb.queue);

	if (!buf)
		return;

	/* Cleans up buffer - Usefull for testing for frame/URB loss */
	outp = videobuf_to_vmalloc(&(*buf)->vb);
//	if (outp)
//		memset(outp, 0, (*buf)->vb.size);

	return;
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
	do_gettimeofday(&buf->vb.ts);

	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
}

const char *tm6000_msg_type[] = {
	"unknown(0)",   /* 0 */
	"video",        /* 1 */
	"audio",        /* 2 */
	"vbi",          /* 3 */
	"pts",          /* 4 */
	"err",          /* 5 */
	"unknown(6)",   /* 6 */
	"unknown(7)",   /* 7 */
};

/*
 * Identify the tm5600/6000 buffer header type and properly handles
 */
static int copy_packet(struct urb *urb, u32 header, u8 **ptr, u8 *endp,
			u8 *out_p, struct tm6000_buffer **buf)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);
	u8 c;
	unsigned int cmd, cpysize, pktsize, size, field, block, line, pos = 0;
	int rc = 0;
	/* FIXME: move to tm6000-isoc */
	static int last_line = -2, start_line = -2, last_field = -2;

	/* FIXME: this is the hardcoded window size
	 */
	unsigned int linewidth = (*buf)->vb.width << 1;

	if (!dev->isoc_ctl.cmd) {
		c = (header >> 24) & 0xff;

		/* split the header fields */
		size  = ((header & 0x7e) << 1);

		if (size > 0)
			size -= 4;

		block = (header >> 7) & 0xf;
		field = (header >> 11) & 0x1;
		line  = (header >> 12) & 0x1ff;
		cmd   = (header >> 21) & 0x7;

		/* Validates header fields */
		if(size > TM6000_URB_MSG_LEN)
			size = TM6000_URB_MSG_LEN;

		if (cmd == TM6000_URB_MSG_VIDEO) {
			if ((block+1)*TM6000_URB_MSG_LEN>linewidth)
				cmd = TM6000_URB_MSG_ERR;

			/* FIXME: Mounts the image as field0+field1
			 * It should, instead, check if the user selected
			 * entrelaced or non-entrelaced mode
			 */
			pos = ((line << 1) - field - 1) * linewidth +
				block * TM6000_URB_MSG_LEN;

			/* Don't allow to write out of the buffer */
			if (pos+TM6000_URB_MSG_LEN > (*buf)->vb.size) {
				dprintk(dev, V4L2_DEBUG_ISOC,
					"ERR: size=%d, num=%d, line=%d, "
					"field=%d\n",
					size, block, line, field);

				cmd = TM6000_URB_MSG_ERR;
			}
		} else {
			pos=0;
		}

		/* Prints debug info */
		dprintk(dev, V4L2_DEBUG_ISOC, "size=%d, num=%d, "
				" line=%d, field=%d\n",
				size, block, line, field);

		if ((last_line!=line)&&(last_line+1!=line) &&
		    (cmd != TM6000_URB_MSG_ERR) )  {
			if (cmd != TM6000_URB_MSG_VIDEO)  {
				dprintk(dev, V4L2_DEBUG_ISOC,  "cmd=%d, "
					"size=%d, num=%d, line=%d, field=%d\n",
					cmd, size, block, line, field);
			}
			if (start_line<0)
				start_line=last_line;
			/* Prints debug info */
			dprintk(dev, V4L2_DEBUG_ISOC, "lines= %d-%d, "
					"field=%d\n",
					start_line, last_line, field);

			if ((start_line<6 && last_line>200) &&
				(last_field != field) ) {

				dev->isoc_ctl.nfields++;
				if (dev->isoc_ctl.nfields>=2) {
					dev->isoc_ctl.nfields=0;

					/* Announces that a new buffer were filled */
					buffer_filled (dev, dma_q, *buf);
					dprintk(dev, V4L2_DEBUG_ISOC,
							"new buffer filled\n");
					get_next_buf (dma_q, buf);
					if (!*buf)
						return rc;
					out_p = videobuf_to_vmalloc(&((*buf)->vb));
					if (!out_p)
						return rc;

					pos = dev->isoc_ctl.pos = 0;
				}
			}

			start_line=line;
			last_field=field;
		}
		if (cmd == TM6000_URB_MSG_VIDEO)
			last_line = line;

		pktsize = TM6000_URB_MSG_LEN;
	} else {
		/* Continue the last copy */
		cmd = dev->isoc_ctl.cmd;
		size= dev->isoc_ctl.size;
		pos = dev->isoc_ctl.pos;
		pktsize = dev->isoc_ctl.pktsize;
	}

	cpysize = (endp-(*ptr) > size) ? size : endp - *ptr;

	if (cpysize) {
		/* handles each different URB message */
		switch(cmd) {
		case TM6000_URB_MSG_VIDEO:
			/* Fills video buffer */
			memcpy(&out_p[pos], *ptr, cpysize);
			break;
		case TM6000_URB_MSG_PTS:
			break;
		case TM6000_URB_MSG_AUDIO:
			/* Need some code to process audio */
			printk ("%ld: cmd=%s, size=%d\n", jiffies,
				tm6000_msg_type[cmd],size);
			break;
		case TM6000_URB_MSG_VBI:
			break;
		default:
			dprintk (dev, V4L2_DEBUG_ISOC, "cmd=%s, size=%d\n",
						tm6000_msg_type[cmd],size);
		}
	}
	if (cpysize<size) {
		/* End of URB packet, but cmd processing is not
		 * complete. Preserve the state for a next packet
		 */
		dev->isoc_ctl.pos = pos+cpysize;
		dev->isoc_ctl.size= size-cpysize;
		dev->isoc_ctl.cmd = cmd;
		dev->isoc_ctl.pktsize = pktsize-cpysize;
		(*ptr)+=cpysize;
	} else {
		dev->isoc_ctl.cmd = 0;
		(*ptr)+=pktsize;
	}

	return rc;
}

static int copy_streams(u8 *data, unsigned long len,
			struct urb *urb)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev= container_of(dma_q,struct tm6000_core,vidq);
	u8 *ptr=data, *endp=data+len;
	unsigned long header=0;
	int rc=0;
	struct tm6000_buffer *buf;
	char *outp = NULL;

	get_next_buf(dma_q, &buf);
	if (buf)
		outp = videobuf_to_vmalloc(&buf->vb);

	if (!outp)
		return 0;

	for (ptr=data; ptr<endp;) {
		if (!dev->isoc_ctl.cmd) {
			u8 *p=(u8 *)&dev->isoc_ctl.tmp_buf;
			/* FIXME: This seems very complex
			 * It just recovers up to 3 bytes of the header that
			 * might be at the previous packet
			 */
			if (dev->isoc_ctl.tmp_buf_len) {
				while (dev->isoc_ctl.tmp_buf_len) {
					if ( *(ptr+3-dev->isoc_ctl.tmp_buf_len) == 0x47) {
						break;
					}
					p++;
					dev->isoc_ctl.tmp_buf_len--;
				}
				if (dev->isoc_ctl.tmp_buf_len) {
					memcpy(&header, p,
						dev->isoc_ctl.tmp_buf_len);
					memcpy((u8 *)&header +
						dev->isoc_ctl.tmp_buf_len,
						ptr,
						4 - dev->isoc_ctl.tmp_buf_len);
					ptr += 4 - dev->isoc_ctl.tmp_buf_len;
					goto HEADER;
				}
			}
			/* Seek for sync */
			for (;ptr<endp-3;ptr++) {
				if (*(ptr+3)==0x47)
					break;
			}

			if (ptr+3>=endp) {
				dev->isoc_ctl.tmp_buf_len=endp-ptr;
				memcpy (&dev->isoc_ctl.tmp_buf,ptr,
					dev->isoc_ctl.tmp_buf_len);
				dev->isoc_ctl.cmd=0;
				return rc;
			}

			/* Get message header */
			header=*(unsigned long *)ptr;
			ptr+=4;
		}
HEADER:
		/* Copy or continue last copy */
		rc=copy_packet(urb,header,&ptr,endp,outp,&buf);
		if (rc<0) {
			buf=NULL;
			printk(KERN_ERR "tm6000: buffer underrun at %ld\n",
					jiffies);
			return rc;
		}
		if (!buf)
			return 0;
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
	struct tm6000_core *dev= container_of(dma_q,struct tm6000_core,vidq);
	unsigned int pos=dev->isoc_ctl.pos,cpysize;
	int rc=1;
	struct tm6000_buffer *buf;
	char *outp = NULL;

	get_next_buf(dma_q, &buf);
	if (buf)
		outp = videobuf_to_vmalloc(&buf->vb);

	if (!outp)
		return 0;

	while (len>0) {
		cpysize=min(len,buf->vb.size-pos);
		//printk("Copying %d bytes (max=%lu) from %p to %p[%u]\n",cpysize,(*buf)->vb.size,ptr,out_p,pos);
		memcpy(&outp[pos], ptr, cpysize);
		pos+=cpysize;
		ptr+=cpysize;
		len-=cpysize;
		if (pos >= buf->vb.size) {
			pos=0;
			/* Announces that a new buffer were filled */
			buffer_filled (dev, dma_q, buf);
			dprintk(dev, V4L2_DEBUG_ISOC, "new buffer filled\n");
			get_next_buf (dma_q, &buf);
			if (!buf)
				break;
			outp = videobuf_to_vmalloc(&(buf->vb));
			if (!outp)
				return rc;
			pos = 0;
		}
	}

	dev->isoc_ctl.pos=pos;
	return rc;
}

static void inline print_err_status (struct tm6000_core *dev,
				     int packet, int status)
{
	char *errmsg = "Unknown";

	switch(status) {
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
	if (packet<0) {
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
	struct tm6000_core *dev= container_of(dma_q,struct tm6000_core,vidq);
	struct tm6000_buffer *buf;
	int i, len=0, rc=1, status;
	char *p;

	if (urb->status < 0) {
		print_err_status (dev, -1, urb->status);
		return 0;
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		status = urb->iso_frame_desc[i].status;

		if (status<0) {
			print_err_status (dev,i,status);
			continue;
		}

		len = urb->iso_frame_desc[i].actual_length;

		if (len > 0) {
			p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
			if (!urb->iso_frame_desc[i].status) {
				if ((dev->fourcc)==V4L2_PIX_FMT_TM6000) {
					rc=copy_multiplexed(p, len, urb);
					if (rc<=0)
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
	URB control
   ------------------------------------------------------------------*/

/*
 * IRQ callback, called by URB callback
 */
static void tm6000_irq_callback(struct urb *urb)
{
	struct tm6000_dmaqueue  *dma_q = urb->context;
	struct tm6000_core *dev = container_of(dma_q, struct tm6000_core, vidq);
	int i;

	if (!dev)
		return;

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
 * Stop and Deallocate URBs
 */
static void tm6000_uninit_isoc(struct tm6000_core *dev)
{
	struct urb *urb;
	int i;

	dev->isoc_ctl.nfields = -1;
	dev->isoc_ctl.buf = NULL;
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb=dev->isoc_ctl.urb[i];
		if (urb) {
			usb_kill_urb(urb);
			usb_unlink_urb(urb);
			if (dev->isoc_ctl.transfer_buffer[i]) {
				usb_free_coherent(dev->udev,
						urb->transfer_buffer_length,
						dev->isoc_ctl.transfer_buffer[i],
						urb->transfer_dma);
			}
			usb_free_urb(urb);
			dev->isoc_ctl.urb[i] = NULL;
		}
		dev->isoc_ctl.transfer_buffer[i] = NULL;
	}

	kfree (dev->isoc_ctl.urb);
	kfree (dev->isoc_ctl.transfer_buffer);

	dev->isoc_ctl.urb=NULL;
	dev->isoc_ctl.transfer_buffer=NULL;
	dev->isoc_ctl.num_bufs = 0;

	dev->isoc_ctl.num_bufs=0;
}

/*
 * Allocate URBs and start IRQ
 */
static int tm6000_prepare_isoc(struct tm6000_core *dev, unsigned int framesize)
{
	struct tm6000_dmaqueue *dma_q = &dev->vidq;
	int i, j, sb_size, pipe, size, max_packets, num_bufs = 8;
	struct urb *urb;

	/* De-allocates all pending stuff */
	tm6000_uninit_isoc(dev);

	usb_set_interface(dev->udev,
			  dev->isoc_in.bInterfaceNumber,
			  dev->isoc_in.bAlternateSetting);

	pipe = usb_rcvisocpipe(dev->udev,
			       dev->isoc_in.endp->desc.bEndpointAddress &
			       USB_ENDPOINT_NUMBER_MASK);

	size = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));

	if (size > dev->isoc_in.maxsize)
		size = dev->isoc_in.maxsize;

	dev->isoc_ctl.max_pkt_size = size;

	max_packets = ( framesize + size - 1) / size;

	if (max_packets > TM6000_MAX_ISO_PACKETS)
		max_packets = TM6000_MAX_ISO_PACKETS;

	sb_size = max_packets * size;

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

		dev->isoc_ctl.transfer_buffer[i] = usb_alloc_coherent(dev->udev,
			sb_size, GFP_KERNEL, &urb->transfer_dma);
		if (!dev->isoc_ctl.transfer_buffer[i]) {
			tm6000_err ("unable to allocate %i bytes for transfer"
					" buffer %i%s\n",
					sb_size, i,
					in_interrupt()?" while in int":"");
			tm6000_uninit_isoc(dev);
			return -ENOMEM;
		}
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

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

static int tm6000_start_thread( struct tm6000_core *dev)
{
	struct tm6000_dmaqueue *dma_q = &dev->vidq;
	int i;

	dma_q->frame=0;
	dma_q->ini_jiffies=jiffies;

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
	Videobuf operations
   ------------------------------------------------------------------*/

static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct tm6000_fh *fh = vq->priv_data;

	*size = fh->fmt->depth * fh->width * fh->height >> 3;
	if (0 == *count)
		*count = TM6000_DEF_BUF;

	if (*count < TM6000_MIN_BUF) {
		*count=TM6000_MIN_BUF;
	}

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
	struct tm6000_buffer *buf = container_of(vb,struct tm6000_buffer,vb);
	struct tm6000_core   *dev = fh->dev;
	int rc = 0, urb_init = 0;

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
		if (0 != (rc = videobuf_iolock(vq, &buf->vb, NULL)))
			goto fail;
		urb_init = 1;
	}

	if (!dev->isoc_ctl.num_bufs)
		urb_init = 1;

	if (urb_init) {
		rc = tm6000_prepare_isoc(dev, buf->vb.size);
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
	struct tm6000_buffer    *buf     = container_of(vb,struct tm6000_buffer,vb);
	struct tm6000_fh        *fh      = vq->priv_data;
	struct tm6000_core      *dev     = fh->dev;
	struct tm6000_dmaqueue  *vidq    = &dev->vidq;

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct tm6000_buffer   *buf  = container_of(vb,struct tm6000_buffer,vb);

	free_buffer(vq,buf);
}

static struct videobuf_queue_ops tm6000_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL handling
   ------------------------------------------------------------------*/

static int res_get(struct tm6000_core *dev, struct tm6000_fh *fh)
{
	/* is it free? */
	mutex_lock(&dev->lock);
	if (dev->resources) {
		/* no, someone else uses it */
		mutex_unlock(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	dev->resources =1;
	dprintk(dev, V4L2_DEBUG_RES_LOCK, "res: get\n");
	mutex_unlock(&dev->lock);
	return 1;
}

static int res_locked(struct tm6000_core *dev)
{
	return (dev->resources);
}

static void res_free(struct tm6000_core *dev, struct tm6000_fh *fh)
{
	mutex_lock(&dev->lock);
	dev->resources = 0;
	dprintk(dev, V4L2_DEBUG_RES_LOCK, "res: put\n");
	mutex_unlock(&dev->lock);
}

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap (struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	//	struct tm6000_core *dev = ((struct tm6000_fh *)priv)->dev;

	strlcpy(cap->driver, "tm6000", sizeof(cap->driver));
	strlcpy(cap->card,"Trident TVMaster TM5600/6000/6010", sizeof(cap->card));
	//	strlcpy(cap->bus_info, dev->udev->dev.bus_id, sizeof(cap->bus_info));
	cap->version = TM6000_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_TUNER	       |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap (struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(format)))
		return -EINVAL;

	strlcpy(f->description,format[f->index].name,sizeof(f->description));
	f->pixelformat = format[f->index].fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct tm6000_fh  *fh=priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return (0);
}

static struct tm6000_fmt* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format); i++)
		if (format[i].fourcc == fourcc)
			return format+i;
	return NULL;
}

static int vidioc_try_fmt_vid_cap (struct file *file, void *priv,
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

	if (field == V4L2_FIELD_ANY) {
//		field=V4L2_FIELD_INTERLACED;
		field=V4L2_FIELD_SEQ_TB;
	} else if (V4L2_FIELD_INTERLACED != field) {
		dprintk(dev, V4L2_DEBUG_IOCTL_ARG, "Field type invalid.\n");
		return -EINVAL;
	}

	tm6000_get_std_res (dev);

	f->fmt.pix.width  = dev->width;
	f->fmt.pix.height = dev->height;

	f->fmt.pix.width &= ~0x01;

	f->fmt.pix.field = field;

	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct tm6000_fh  *fh=priv;
	struct tm6000_core *dev = fh->dev;
	int ret = vidioc_try_fmt_vid_cap(file,fh,f);
	if (ret < 0)
		return (ret);

	fh->fmt           = format_by_fourcc(f->fmt.pix.pixelformat);
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	dev->fourcc       = f->fmt.pix.pixelformat;

	tm6000_set_fourcc_format(dev);

	return (0);
}

static int vidioc_reqbufs (struct file *file, void *priv,
			   struct v4l2_requestbuffers *p)
{
	struct tm6000_fh  *fh=priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf (struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct tm6000_fh  *fh=priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct tm6000_fh  *fh=priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct tm6000_fh  *fh=priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf (struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct tm6000_fh  *fh=priv;

	return videobuf_cgmbuf (&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct tm6000_fh  *fh=priv;
	struct tm6000_core *dev    = fh->dev;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	if (!res_get(dev,fh))
		return -EBUSY;
	return (videobuf_streamon(&fh->vb_vidq));
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct tm6000_fh  *fh=priv;
	struct tm6000_core *dev    = fh->dev;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	videobuf_streamoff(&fh->vb_vidq);
	res_free(dev,fh);

	return (0);
}

static int vidioc_s_std (struct file *file, void *priv, v4l2_std_id *norm)
{
	int rc=0;
	struct tm6000_fh   *fh=priv;
	struct tm6000_core *dev = fh->dev;

	rc=tm6000_set_standard (dev, norm);

	fh->width  = dev->width;
	fh->height = dev->height;

	if (rc<0)
		return rc;

	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_std, dev->norm);

	return 0;
}

static int vidioc_enum_input (struct file *file, void *priv,
				struct v4l2_input *inp)
{
	switch (inp->index) {
	case TM6000_INPUT_TV:
		inp->type = V4L2_INPUT_TYPE_TUNER;
		strcpy(inp->name,"Television");
		break;
	case TM6000_INPUT_COMPOSITE:
		inp->type = V4L2_INPUT_TYPE_CAMERA;
		strcpy(inp->name,"Composite");
		break;
	case TM6000_INPUT_SVIDEO:
		inp->type = V4L2_INPUT_TYPE_CAMERA;
		strcpy(inp->name,"S-Video");
		break;
	default:
		return -EINVAL;
	}
	inp->std = TM6000_STD;

	return 0;
}

static int vidioc_g_input (struct file *file, void *priv, unsigned int *i)
{
	struct tm6000_fh   *fh=priv;
	struct tm6000_core *dev = fh->dev;

	*i=dev->input;

	return 0;
}
static int vidioc_s_input (struct file *file, void *priv, unsigned int i)
{
	struct tm6000_fh   *fh=priv;
	struct tm6000_core *dev = fh->dev;
	int rc=0;
	char buf[1];

	switch (i) {
	case TM6000_INPUT_TV:
		dev->input=i;
		*buf=0;
		break;
	case TM6000_INPUT_COMPOSITE:
	case TM6000_INPUT_SVIDEO:
		dev->input=i;
		*buf=1;
		break;
	default:
		return -EINVAL;
	}
	rc=tm6000_read_write_usb (dev, USB_DIR_OUT | USB_TYPE_VENDOR,
			       REQ_03_SET_GET_MCU_PIN, 0x03, 1, buf, 1);

	if (!rc) {
		dev->input=i;
		rc=vidioc_s_std (file, priv, &dev->vfd->current_norm);
	}

	return (rc);
}

	/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl (struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tm6000_qctrl); i++)
		if (qc->id && qc->id == tm6000_qctrl[i].id) {
			memcpy(qc, &(tm6000_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct tm6000_fh  *fh=priv;
	struct tm6000_core *dev    = fh->dev;
	int  val;

	/* FIXME: Probably, those won't work! Maybe we need shadow regs */
	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		val = tm6000_get_reg(dev, TM6010_REQ07_R08_LUMA_CONTRAST_ADJ, 0);
		break;
	case V4L2_CID_BRIGHTNESS:
		val = tm6000_get_reg(dev, TM6010_REQ07_R09_LUMA_BRIGHTNESS_ADJ, 0);
		return 0;
	case V4L2_CID_SATURATION:
		val = tm6000_get_reg(dev, TM6010_REQ07_R0A_CHROMA_SATURATION_ADJ, 0);
		return 0;
	case V4L2_CID_HUE:
		val = tm6000_get_reg(dev, TM6010_REQ07_R0B_CHROMA_HUE_PHASE_ADJ, 0);
		return 0;
	default:
		return -EINVAL;
	}

	if (val<0)
		return val;

	ctrl->value=val;

	return 0;
}
static int vidioc_s_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct tm6000_fh   *fh  =priv;
	struct tm6000_core *dev = fh->dev;
	u8  val=ctrl->value;

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

static int vidioc_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct tm6000_fh   *fh  =priv;
	struct tm6000_core *dev = fh->dev;

	if (unlikely(UNSET == dev->tuner_type))
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Television");
	t->type       = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM;
	t->rangehigh  = 0xffffffffUL;
	t->rxsubchans = V4L2_TUNER_SUB_MONO;

	return 0;
}

static int vidioc_s_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct tm6000_fh   *fh  =priv;
	struct tm6000_core *dev = fh->dev;

	if (UNSET == dev->tuner_type)
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	return 0;
}

static int vidioc_g_frequency (struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct tm6000_fh   *fh  =priv;
	struct tm6000_core *dev = fh->dev;

	if (unlikely(UNSET == dev->tuner_type))
		return -EINVAL;

	f->type = V4L2_TUNER_ANALOG_TV;
	f->frequency = dev->freq;

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_frequency, f);

	return 0;
}

static int vidioc_s_frequency (struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct tm6000_fh   *fh  =priv;
	struct tm6000_core *dev = fh->dev;

	if (unlikely(f->type != V4L2_TUNER_ANALOG_TV))
		return -EINVAL;

	if (unlikely(UNSET == dev->tuner_type))
		return -EINVAL;
	if (unlikely(f->tuner != 0))
		return -EINVAL;

//	mutex_lock(&dev->lock);
	dev->freq = f->frequency;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, f);
//	mutex_unlock(&dev->lock);

	return 0;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int tm6000_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct tm6000_core *dev = video_drvdata(file);
	struct tm6000_fh *fh;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int i,rc;

	printk(KERN_INFO "tm6000: open called (dev=%s)\n",
		video_device_node_name(vdev));

	dprintk(dev, V4L2_DEBUG_OPEN, "tm6000: open called (dev=%s)\n",
		video_device_node_name(vdev));


	/* If more than one user, mutex should be added */
	dev->users++;

	dprintk(dev, V4L2_DEBUG_OPEN, "open dev=%s type=%s users=%d\n",
		video_device_node_name(vdev), v4l2_type_names[type],
		dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		return -ENOMEM;
	}

	file->private_data = fh;
	fh->dev      = dev;

	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dev->fourcc  = format[0].fourcc;

	fh->fmt      = format_by_fourcc(dev->fourcc);

	tm6000_get_std_res (dev);

	fh->width    = dev->width;
	fh->height   = dev->height;

	dprintk(dev, V4L2_DEBUG_OPEN, "Open: fh=0x%08lx, dev=0x%08lx, "
						"dev->vidq=0x%08lx\n",
		(unsigned long)fh,(unsigned long)dev,(unsigned long)&dev->vidq);
	dprintk(dev, V4L2_DEBUG_OPEN, "Open: list_empty "
				"queued=%d\n",list_empty(&dev->vidq.queued));
	dprintk(dev, V4L2_DEBUG_OPEN, "Open: list_empty "
				"active=%d\n",list_empty(&dev->vidq.active));

	/* initialize hardware on analog mode */
	if (dev->mode!=TM6000_MODE_ANALOG) {
		rc=tm6000_init_analog_mode (dev);
		if (rc<0)
			return rc;

		/* Put all controls at a sane state */
		for (i = 0; i < ARRAY_SIZE(tm6000_qctrl); i++)
			qctl_regs[i] =tm6000_qctrl[i].default_value;

		dev->mode=TM6000_MODE_ANALOG;
	}

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &tm6000_video_qops,
			NULL, &dev->slock,
			fh->type,
			V4L2_FIELD_INTERLACED,
			sizeof(struct tm6000_buffer),fh);

	return 0;
}

static ssize_t
tm6000_read(struct file *file, char __user *data, size_t count, loff_t *pos)
{
	struct tm6000_fh        *fh = file->private_data;

	if (fh->type==V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (res_locked(fh->dev))
			return -EBUSY;

		return videobuf_read_stream(&fh->vb_vidq, data, count, pos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
tm6000_poll(struct file *file, struct poll_table_struct *wait)
{
	struct tm6000_fh        *fh = file->private_data;
	struct tm6000_buffer    *buf;

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	if (res_get(fh->dev,fh)) {
		/* streaming capture */
		if (list_empty(&fh->vb_vidq.stream))
			return POLLERR;
		buf = list_entry(fh->vb_vidq.stream.next,struct tm6000_buffer,vb.stream);
	} else {
		/* read() capture */
		return videobuf_poll_stream(file, &fh->vb_vidq,
					    wait);
	}
	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;
	return 0;
}

static int tm6000_release(struct file *file)
{
	struct tm6000_fh         *fh = file->private_data;
	struct tm6000_core      *dev = fh->dev;
	struct video_device    *vdev = video_devdata(file);

	dprintk(dev, V4L2_DEBUG_OPEN, "tm6000: close called (dev=%s, users=%d)\n",
		video_device_node_name(vdev), dev->users);

	dev->users--;

	if (!dev->users) {
		tm6000_uninit_isoc(dev);
		videobuf_mmap_free(&fh->vb_vidq);
	}

	kfree (fh);

	return 0;
}

static int tm6000_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct tm6000_fh        *fh = file->private_data;
	int ret;

	ret=videobuf_mmap_mapper(&fh->vb_vidq, vma);

	return ret;
}

static struct v4l2_file_operations tm6000_fops = {
	.owner		= THIS_MODULE,
	.open           = tm6000_open,
	.release        = tm6000_release,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.read           = tm6000_read,
	.poll		= tm6000_poll,
	.mmap		= tm6000_mmap,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap          = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_s_std             = vidioc_s_std,
	.vidioc_enum_input        = vidioc_enum_input,
	.vidioc_g_input           = vidioc_g_input,
	.vidioc_s_input           = vidioc_s_input,
	.vidioc_queryctrl         = vidioc_queryctrl,
	.vidioc_g_ctrl            = vidioc_g_ctrl,
	.vidioc_s_ctrl            = vidioc_s_ctrl,
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
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf              = vidiocgmbuf,
#endif
};

static struct video_device tm6000_template = {
	.name		= "tm6000",
	.fops           = &tm6000_fops,
	.ioctl_ops      = &video_ioctl_ops,
	.release	= video_device_release,
	.tvnorms        = TM6000_STD,
	.current_norm   = V4L2_STD_NTSC_M,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

int tm6000_v4l2_register(struct tm6000_core *dev)
{
	int ret = -1;
	struct video_device *vfd;

	vfd = video_device_alloc();
	if(!vfd) {
		return -ENOMEM;
	}
	dev->vfd = vfd;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);

	memcpy (dev->vfd, &tm6000_template, sizeof(*(dev->vfd)));
	dev->vfd->debug=tm6000_debug;
	vfd->v4l2_dev = &dev->v4l2_dev;
	video_set_drvdata(vfd, dev);

	ret = video_register_device(dev->vfd, VFL_TYPE_GRABBER, video_nr);
	printk(KERN_INFO "Trident TVMaster TM5600/TM6000/TM6010 USB2 board (Load status: %d)\n", ret);
	return ret;
}

int tm6000_v4l2_unregister(struct tm6000_core *dev)
{
	video_unregister_device(dev->vfd);

	return 0;
}

int tm6000_v4l2_exit(void)
{
	return 0;
}

module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr,"Allow changing video device number");

module_param_named (debug, tm6000_debug, int, 0444);
MODULE_PARM_DESC(debug,"activates debug info");

module_param(vid_limit,int,0644);
MODULE_PARM_DESC(vid_limit,"capture memory limit in megabytes");

