/*
 * Auvitek AU0828 USB Bridge (Analog video support)
 *
 * Copyright (C) 2009 Devin Heitmueller <dheitmueller@linuxtv.org>
 * Copyright (C) 2005-2008 Auvitek International, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * As published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* Developer Notes:
 *
 * VBI support is not yet working
 * The hardware scaler supported is unimplemented
 * AC97 audio support is unimplemented (only i2s audio mode)
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/tuner.h>
#include "au0828.h"
#include "au0828-reg.h"

static DEFINE_MUTEX(au0828_sysfs_lock);

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/

static unsigned int isoc_debug;
module_param(isoc_debug, int, 0644);
MODULE_PARM_DESC(isoc_debug, "enable debug messages [isoc transfers]");

#define au0828_isocdbg(fmt, arg...) \
do {\
	if (isoc_debug) { \
		printk(KERN_INFO "au0828 %s :"fmt, \
		       __func__ , ##arg);	   \
	} \
  } while (0)

static inline void i2c_gate_ctrl(struct au0828_dev *dev, int val)
{
	if (dev->dvb.frontend && dev->dvb.frontend->ops.analog_ops.i2c_gate_ctrl)
		dev->dvb.frontend->ops.analog_ops.i2c_gate_ctrl(dev->dvb.frontend, val);
}

static inline void print_err_status(struct au0828_dev *dev,
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
		au0828_isocdbg("URB status %d [%s].\n",	status, errmsg);
	} else {
		au0828_isocdbg("URB packet %d, status %d [%s].\n",
			       packet, status, errmsg);
	}
}

static int check_dev(struct au0828_dev *dev)
{
	if (dev->dev_state & DEV_DISCONNECTED) {
		printk(KERN_INFO "v4l2 ioctl: device not present\n");
		return -ENODEV;
	}

	if (dev->dev_state & DEV_MISCONFIGURED) {
		printk(KERN_INFO "v4l2 ioctl: device is misconfigured; "
		       "close and open it again\n");
		return -EIO;
	}
	return 0;
}

/*
 * IRQ callback, called by URB callback
 */
static void au0828_irq_callback(struct urb *urb)
{
	struct au0828_dmaqueue  *dma_q = urb->context;
	struct au0828_dev *dev = container_of(dma_q, struct au0828_dev, vidq);
	unsigned long flags = 0;
	int i;

	switch (urb->status) {
	case 0:             /* success */
	case -ETIMEDOUT:    /* NAK */
		break;
	case -ECONNRESET:   /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		au0828_isocdbg("au0828_irq_callback called: status kill\n");
		return;
	default:            /* unknown error */
		au0828_isocdbg("urb completition error %d.\n", urb->status);
		break;
	}

	/* Copy data from URB */
	spin_lock_irqsave(&dev->slock, flags);
	dev->isoc_ctl.isoc_copy(dev, urb);
	spin_unlock_irqrestore(&dev->slock, flags);

	/* Reset urb buffers */
	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}
	urb->status = 0;

	urb->status = usb_submit_urb(urb, GFP_ATOMIC);
	if (urb->status) {
		au0828_isocdbg("urb resubmit failed (error=%i)\n",
			       urb->status);
	}
}

/*
 * Stop and Deallocate URBs
 */
static void au0828_uninit_isoc(struct au0828_dev *dev)
{
	struct urb *urb;
	int i;

	au0828_isocdbg("au0828: called au0828_uninit_isoc\n");

	dev->isoc_ctl.nfields = -1;
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb = dev->isoc_ctl.urb[i];
		if (urb) {
			if (!irqs_disabled())
				usb_kill_urb(urb);
			else
				usb_unlink_urb(urb);

			if (dev->isoc_ctl.transfer_buffer[i]) {
				usb_free_coherent(dev->usbdev,
					urb->transfer_buffer_length,
					dev->isoc_ctl.transfer_buffer[i],
					urb->transfer_dma);
			}
			usb_free_urb(urb);
			dev->isoc_ctl.urb[i] = NULL;
		}
		dev->isoc_ctl.transfer_buffer[i] = NULL;
	}

	kfree(dev->isoc_ctl.urb);
	kfree(dev->isoc_ctl.transfer_buffer);

	dev->isoc_ctl.urb = NULL;
	dev->isoc_ctl.transfer_buffer = NULL;
	dev->isoc_ctl.num_bufs = 0;
}

/*
 * Allocate URBs and start IRQ
 */
static int au0828_init_isoc(struct au0828_dev *dev, int max_packets,
			    int num_bufs, int max_pkt_size,
			    int (*isoc_copy) (struct au0828_dev *dev, struct urb *urb))
{
	struct au0828_dmaqueue *dma_q = &dev->vidq;
	int i;
	int sb_size, pipe;
	struct urb *urb;
	int j, k;
	int rc;

	au0828_isocdbg("au0828: called au0828_prepare_isoc\n");

	/* De-allocates all pending stuff */
	au0828_uninit_isoc(dev);

	dev->isoc_ctl.isoc_copy = isoc_copy;
	dev->isoc_ctl.num_bufs = num_bufs;

	dev->isoc_ctl.urb = kzalloc(sizeof(void *)*num_bufs,  GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		au0828_isocdbg("cannot alloc memory for usb buffers\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kzalloc(sizeof(void *)*num_bufs,
					      GFP_KERNEL);
	if (!dev->isoc_ctl.transfer_buffer) {
		au0828_isocdbg("cannot allocate memory for usb transfer\n");
		kfree(dev->isoc_ctl.urb);
		return -ENOMEM;
	}

	dev->isoc_ctl.max_pkt_size = max_pkt_size;
	dev->isoc_ctl.buf = NULL;

	sb_size = max_packets * dev->isoc_ctl.max_pkt_size;

	/* allocate urbs and transfer buffers */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		urb = usb_alloc_urb(max_packets, GFP_KERNEL);
		if (!urb) {
			au0828_isocdbg("cannot alloc isoc_ctl.urb %i\n", i);
			au0828_uninit_isoc(dev);
			return -ENOMEM;
		}
		dev->isoc_ctl.urb[i] = urb;

		dev->isoc_ctl.transfer_buffer[i] = usb_alloc_coherent(dev->usbdev,
			sb_size, GFP_KERNEL, &urb->transfer_dma);
		if (!dev->isoc_ctl.transfer_buffer[i]) {
			printk("unable to allocate %i bytes for transfer"
					" buffer %i%s\n",
					sb_size, i,
					in_interrupt() ? " while in int" : "");
			au0828_uninit_isoc(dev);
			return -ENOMEM;
		}
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

		pipe = usb_rcvisocpipe(dev->usbdev,
				       dev->isoc_in_endpointaddr),

		usb_fill_int_urb(urb, dev->usbdev, pipe,
				 dev->isoc_ctl.transfer_buffer[i], sb_size,
				 au0828_irq_callback, dma_q, 1);

		urb->number_of_packets = max_packets;
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;

		k = 0;
		for (j = 0; j < max_packets; j++) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length =
						dev->isoc_ctl.max_pkt_size;
			k += dev->isoc_ctl.max_pkt_size;
		}
	}

	init_waitqueue_head(&dma_q->wq);

	/* submit urbs and enables IRQ */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		rc = usb_submit_urb(dev->isoc_ctl.urb[i], GFP_ATOMIC);
		if (rc) {
			au0828_isocdbg("submit of urb %i failed (error=%i)\n",
				       i, rc);
			au0828_uninit_isoc(dev);
			return rc;
		}
	}

	return 0;
}

/*
 * Announces that a buffer were filled and request the next
 */
static inline void buffer_filled(struct au0828_dev *dev,
				  struct au0828_dmaqueue *dma_q,
				  struct au0828_buffer *buf)
{
	/* Advice that buffer was filled */
	au0828_isocdbg("[%p/%d] wakeup\n", buf, buf->vb.i);

	buf->vb.state = VIDEOBUF_DONE;
	buf->vb.field_count++;
	v4l2_get_timestamp(&buf->vb.ts);

	dev->isoc_ctl.buf = NULL;

	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
}

static inline void vbi_buffer_filled(struct au0828_dev *dev,
				     struct au0828_dmaqueue *dma_q,
				     struct au0828_buffer *buf)
{
	/* Advice that buffer was filled */
	au0828_isocdbg("[%p/%d] wakeup\n", buf, buf->vb.i);

	buf->vb.state = VIDEOBUF_DONE;
	buf->vb.field_count++;
	v4l2_get_timestamp(&buf->vb.ts);

	dev->isoc_ctl.vbi_buf = NULL;

	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
}

/*
 * Identify the buffer header type and properly handles
 */
static void au0828_copy_video(struct au0828_dev *dev,
			      struct au0828_dmaqueue  *dma_q,
			      struct au0828_buffer *buf,
			      unsigned char *p,
			      unsigned char *outp, unsigned long len)
{
	void *fieldstart, *startwrite, *startread;
	int  linesdone, currlinedone, offset, lencopy, remain;
	int bytesperline = dev->width << 1; /* Assumes 16-bit depth @@@@ */

	if (len == 0)
		return;

	if (dma_q->pos + len > buf->vb.size)
		len = buf->vb.size - dma_q->pos;

	startread = p;
	remain = len;

	/* Interlaces frame */
	if (buf->top_field)
		fieldstart = outp;
	else
		fieldstart = outp + bytesperline;

	linesdone = dma_q->pos / bytesperline;
	currlinedone = dma_q->pos % bytesperline;
	offset = linesdone * bytesperline * 2 + currlinedone;
	startwrite = fieldstart + offset;
	lencopy = bytesperline - currlinedone;
	lencopy = lencopy > remain ? remain : lencopy;

	if ((char *)startwrite + lencopy > (char *)outp + buf->vb.size) {
		au0828_isocdbg("Overflow of %zi bytes past buffer end (1)\n",
			       ((char *)startwrite + lencopy) -
			       ((char *)outp + buf->vb.size));
		remain = (char *)outp + buf->vb.size - (char *)startwrite;
		lencopy = remain;
	}
	if (lencopy <= 0)
		return;
	memcpy(startwrite, startread, lencopy);

	remain -= lencopy;

	while (remain > 0) {
		startwrite += lencopy + bytesperline;
		startread += lencopy;
		if (bytesperline > remain)
			lencopy = remain;
		else
			lencopy = bytesperline;

		if ((char *)startwrite + lencopy > (char *)outp +
		    buf->vb.size) {
			au0828_isocdbg("Overflow %zi bytes past buf end (2)\n",
				       ((char *)startwrite + lencopy) -
				       ((char *)outp + buf->vb.size));
			lencopy = remain = (char *)outp + buf->vb.size -
					   (char *)startwrite;
		}
		if (lencopy <= 0)
			break;

		memcpy(startwrite, startread, lencopy);

		remain -= lencopy;
	}

	if (offset > 1440) {
		/* We have enough data to check for greenscreen */
		if (outp[0] < 0x60 && outp[1440] < 0x60)
			dev->greenscreen_detected = 1;
	}

	dma_q->pos += len;
}

/*
 * video-buf generic routine to get the next available buffer
 */
static inline void get_next_buf(struct au0828_dmaqueue *dma_q,
				struct au0828_buffer **buf)
{
	struct au0828_dev *dev = container_of(dma_q, struct au0828_dev, vidq);

	if (list_empty(&dma_q->active)) {
		au0828_isocdbg("No active queue to serve\n");
		dev->isoc_ctl.buf = NULL;
		*buf = NULL;
		return;
	}

	/* Get the next buffer */
	*buf = list_entry(dma_q->active.next, struct au0828_buffer, vb.queue);
	dev->isoc_ctl.buf = *buf;

	return;
}

static void au0828_copy_vbi(struct au0828_dev *dev,
			      struct au0828_dmaqueue  *dma_q,
			      struct au0828_buffer *buf,
			      unsigned char *p,
			      unsigned char *outp, unsigned long len)
{
	unsigned char *startwrite, *startread;
	int bytesperline;
	int i, j = 0;

	if (dev == NULL) {
		au0828_isocdbg("dev is null\n");
		return;
	}

	if (dma_q == NULL) {
		au0828_isocdbg("dma_q is null\n");
		return;
	}
	if (buf == NULL)
		return;
	if (p == NULL) {
		au0828_isocdbg("p is null\n");
		return;
	}
	if (outp == NULL) {
		au0828_isocdbg("outp is null\n");
		return;
	}

	bytesperline = dev->vbi_width;

	if (dma_q->pos + len > buf->vb.size)
		len = buf->vb.size - dma_q->pos;

	startread = p;
	startwrite = outp + (dma_q->pos / 2);

	/* Make sure the bottom field populates the second half of the frame */
	if (buf->top_field == 0)
		startwrite += bytesperline * dev->vbi_height;

	for (i = 0; i < len; i += 2)
		startwrite[j++] = startread[i+1];

	dma_q->pos += len;
}


/*
 * video-buf generic routine to get the next available VBI buffer
 */
static inline void vbi_get_next_buf(struct au0828_dmaqueue *dma_q,
				    struct au0828_buffer **buf)
{
	struct au0828_dev *dev = container_of(dma_q, struct au0828_dev, vbiq);
	char *outp;

	if (list_empty(&dma_q->active)) {
		au0828_isocdbg("No active queue to serve\n");
		dev->isoc_ctl.vbi_buf = NULL;
		*buf = NULL;
		return;
	}

	/* Get the next buffer */
	*buf = list_entry(dma_q->active.next, struct au0828_buffer, vb.queue);
	/* Cleans up buffer - Useful for testing for frame/URB loss */
	outp = videobuf_to_vmalloc(&(*buf)->vb);
	memset(outp, 0x00, (*buf)->vb.size);

	dev->isoc_ctl.vbi_buf = *buf;

	return;
}

/*
 * Controls the isoc copy of each urb packet
 */
static inline int au0828_isoc_copy(struct au0828_dev *dev, struct urb *urb)
{
	struct au0828_buffer    *buf;
	struct au0828_buffer    *vbi_buf;
	struct au0828_dmaqueue  *dma_q = urb->context;
	struct au0828_dmaqueue  *vbi_dma_q = &dev->vbiq;
	unsigned char *outp = NULL;
	unsigned char *vbioutp = NULL;
	int i, len = 0, rc = 1;
	unsigned char *p;
	unsigned char fbyte;
	unsigned int vbi_field_size;
	unsigned int remain, lencopy;

	if (!dev)
		return 0;

	if ((dev->dev_state & DEV_DISCONNECTED) ||
	    (dev->dev_state & DEV_MISCONFIGURED))
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	buf = dev->isoc_ctl.buf;
	if (buf != NULL)
		outp = videobuf_to_vmalloc(&buf->vb);

	vbi_buf = dev->isoc_ctl.vbi_buf;
	if (vbi_buf != NULL)
		vbioutp = videobuf_to_vmalloc(&vbi_buf->vb);

	for (i = 0; i < urb->number_of_packets; i++) {
		int status = urb->iso_frame_desc[i].status;

		if (status < 0) {
			print_err_status(dev, i, status);
			if (urb->iso_frame_desc[i].status != -EPROTO)
				continue;
		}

		if (urb->iso_frame_desc[i].actual_length <= 0)
			continue;

		if (urb->iso_frame_desc[i].actual_length >
						dev->max_pkt_size) {
			au0828_isocdbg("packet bigger than packet size");
			continue;
		}

		p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		fbyte = p[0];
		len = urb->iso_frame_desc[i].actual_length - 4;
		p += 4;

		if (fbyte & 0x80) {
			len -= 4;
			p += 4;
			au0828_isocdbg("Video frame %s\n",
				       (fbyte & 0x40) ? "odd" : "even");
			if (fbyte & 0x40) {
				/* VBI */
				if (vbi_buf != NULL)
					vbi_buffer_filled(dev,
							  vbi_dma_q,
							  vbi_buf);
				vbi_get_next_buf(vbi_dma_q, &vbi_buf);
				if (vbi_buf == NULL)
					vbioutp = NULL;
				else
					vbioutp = videobuf_to_vmalloc(
						&vbi_buf->vb);

				/* Video */
				if (buf != NULL)
					buffer_filled(dev, dma_q, buf);
				get_next_buf(dma_q, &buf);
				if (buf == NULL)
					outp = NULL;
				else
					outp = videobuf_to_vmalloc(&buf->vb);

				/* As long as isoc traffic is arriving, keep
				   resetting the timer */
				if (dev->vid_timeout_running)
					mod_timer(&dev->vid_timeout,
						  jiffies + (HZ / 10));
				if (dev->vbi_timeout_running)
					mod_timer(&dev->vbi_timeout,
						  jiffies + (HZ / 10));
			}

			if (buf != NULL) {
				if (fbyte & 0x40)
					buf->top_field = 1;
				else
					buf->top_field = 0;
			}

			if (vbi_buf != NULL) {
				if (fbyte & 0x40)
					vbi_buf->top_field = 1;
				else
					vbi_buf->top_field = 0;
			}

			dev->vbi_read = 0;
			vbi_dma_q->pos = 0;
			dma_q->pos = 0;
		}

		vbi_field_size = dev->vbi_width * dev->vbi_height * 2;
		if (dev->vbi_read < vbi_field_size) {
			remain  = vbi_field_size - dev->vbi_read;
			if (len < remain)
				lencopy = len;
			else
				lencopy = remain;

			if (vbi_buf != NULL)
				au0828_copy_vbi(dev, vbi_dma_q, vbi_buf, p,
						vbioutp, len);

			len -= lencopy;
			p += lencopy;
			dev->vbi_read += lencopy;
		}

		if (dev->vbi_read >= vbi_field_size && buf != NULL)
			au0828_copy_video(dev, dma_q, buf, p, outp, len);
	}
	return rc;
}

static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count,
	     unsigned int *size)
{
	struct au0828_fh *fh = vq->priv_data;
	*size = (fh->dev->width * fh->dev->height * 16 + 7) >> 3;

	if (0 == *count)
		*count = AU0828_DEF_BUF;

	if (*count < AU0828_MIN_BUF)
		*count = AU0828_MIN_BUF;
	return 0;
}

/* This is called *without* dev->slock held; please keep it that way */
static void free_buffer(struct videobuf_queue *vq, struct au0828_buffer *buf)
{
	struct au0828_fh     *fh  = vq->priv_data;
	struct au0828_dev    *dev = fh->dev;
	unsigned long flags = 0;
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
	struct au0828_fh     *fh  = vq->priv_data;
	struct au0828_buffer *buf = container_of(vb, struct au0828_buffer, vb);
	struct au0828_dev    *dev = fh->dev;
	int                  rc = 0, urb_init = 0;

	buf->vb.size = (fh->dev->width * fh->dev->height * 16 + 7) >> 3;

	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	buf->vb.width  = dev->width;
	buf->vb.height = dev->height;
	buf->vb.field  = field;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0) {
			printk(KERN_INFO "videobuf_iolock failed\n");
			goto fail;
		}
	}

	if (!dev->isoc_ctl.num_bufs)
		urb_init = 1;

	if (urb_init) {
		rc = au0828_init_isoc(dev, AU0828_ISO_PACKETS_PER_URB,
				      AU0828_MAX_ISO_BUFS, dev->max_pkt_size,
				      au0828_isoc_copy);
		if (rc < 0) {
			printk(KERN_INFO "au0828_init_isoc failed\n");
			goto fail;
		}
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
	struct au0828_buffer    *buf     = container_of(vb,
							struct au0828_buffer,
							vb);
	struct au0828_fh        *fh      = vq->priv_data;
	struct au0828_dev       *dev     = fh->dev;
	struct au0828_dmaqueue  *vidq    = &dev->vidq;

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
				struct videobuf_buffer *vb)
{
	struct au0828_buffer   *buf  = container_of(vb,
						    struct au0828_buffer,
						    vb);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops au0828_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
   V4L2 interface
   ------------------------------------------------------------------*/

static int au0828_i2s_init(struct au0828_dev *dev)
{
	/* Enable i2s mode */
	au0828_writereg(dev, AU0828_AUDIOCTRL_50C, 0x01);
	return 0;
}

/*
 * Auvitek au0828 analog stream enable
 * Please set interface0 to AS5 before enable the stream
 */
static int au0828_analog_stream_enable(struct au0828_dev *d)
{
	dprintk(1, "au0828_analog_stream_enable called\n");
	au0828_writereg(d, AU0828_SENSORCTRL_VBI_103, 0x00);
	au0828_writereg(d, 0x106, 0x00);
	/* set x position */
	au0828_writereg(d, 0x110, 0x00);
	au0828_writereg(d, 0x111, 0x00);
	au0828_writereg(d, 0x114, 0xa0);
	au0828_writereg(d, 0x115, 0x05);
	/* set y position */
	au0828_writereg(d, 0x112, 0x00);
	au0828_writereg(d, 0x113, 0x00);
	au0828_writereg(d, 0x116, 0xf2);
	au0828_writereg(d, 0x117, 0x00);
	au0828_writereg(d, AU0828_SENSORCTRL_100, 0xb3);

	return 0;
}

int au0828_analog_stream_disable(struct au0828_dev *d)
{
	dprintk(1, "au0828_analog_stream_disable called\n");
	au0828_writereg(d, AU0828_SENSORCTRL_100, 0x0);
	return 0;
}

static void au0828_analog_stream_reset(struct au0828_dev *dev)
{
	dprintk(1, "au0828_analog_stream_reset called\n");
	au0828_writereg(dev, AU0828_SENSORCTRL_100, 0x0);
	mdelay(30);
	au0828_writereg(dev, AU0828_SENSORCTRL_100, 0xb3);
}

/*
 * Some operations needs to stop current streaming
 */
static int au0828_stream_interrupt(struct au0828_dev *dev)
{
	int ret = 0;

	dev->stream_state = STREAM_INTERRUPT;
	if (dev->dev_state == DEV_DISCONNECTED)
		return -ENODEV;
	else if (ret) {
		dev->dev_state = DEV_MISCONFIGURED;
		dprintk(1, "%s device is misconfigured!\n", __func__);
		return ret;
	}
	return 0;
}

/*
 * au0828_release_resources
 * unregister v4l2 devices
 */
void au0828_analog_unregister(struct au0828_dev *dev)
{
	dprintk(1, "au0828_release_resources called\n");
	mutex_lock(&au0828_sysfs_lock);

	if (dev->vdev)
		video_unregister_device(dev->vdev);
	if (dev->vbi_dev)
		video_unregister_device(dev->vbi_dev);

	mutex_unlock(&au0828_sysfs_lock);
}


/* Usage lock check functions */
static int res_get(struct au0828_fh *fh, unsigned int bit)
{
	struct au0828_dev    *dev = fh->dev;

	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	if (dev->resources & bit) {
		/* no, someone else uses it */
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	dev->resources |= bit;
	dprintk(1, "res: get %d\n", bit);

	return 1;
}

static int res_check(struct au0828_fh *fh, unsigned int bit)
{
	return fh->resources & bit;
}

static int res_locked(struct au0828_dev *dev, unsigned int bit)
{
	return dev->resources & bit;
}

static void res_free(struct au0828_fh *fh, unsigned int bits)
{
	struct au0828_dev    *dev = fh->dev;

	BUG_ON((fh->resources & bits) != bits);

	fh->resources  &= ~bits;
	dev->resources &= ~bits;
	dprintk(1, "res: put %d\n", bits);
}

static int get_ressource(struct au0828_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return AU0828_RESOURCE_VIDEO;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return AU0828_RESOURCE_VBI;
	default:
		BUG();
		return 0;
	}
}

/* This function ensures that video frames continue to be delivered even if
   the ITU-656 input isn't receiving any data (thereby preventing applications
   such as tvtime from hanging) */
static void au0828_vid_buffer_timeout(unsigned long data)
{
	struct au0828_dev *dev = (struct au0828_dev *) data;
	struct au0828_dmaqueue *dma_q = &dev->vidq;
	struct au0828_buffer *buf;
	unsigned char *vid_data;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);

	buf = dev->isoc_ctl.buf;
	if (buf != NULL) {
		vid_data = videobuf_to_vmalloc(&buf->vb);
		memset(vid_data, 0x00, buf->vb.size); /* Blank green frame */
		buffer_filled(dev, dma_q, buf);
	}
	get_next_buf(dma_q, &buf);

	if (dev->vid_timeout_running == 1)
		mod_timer(&dev->vid_timeout, jiffies + (HZ / 10));

	spin_unlock_irqrestore(&dev->slock, flags);
}

static void au0828_vbi_buffer_timeout(unsigned long data)
{
	struct au0828_dev *dev = (struct au0828_dev *) data;
	struct au0828_dmaqueue *dma_q = &dev->vbiq;
	struct au0828_buffer *buf;
	unsigned char *vbi_data;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);

	buf = dev->isoc_ctl.vbi_buf;
	if (buf != NULL) {
		vbi_data = videobuf_to_vmalloc(&buf->vb);
		memset(vbi_data, 0x00, buf->vb.size);
		vbi_buffer_filled(dev, dma_q, buf);
	}
	vbi_get_next_buf(dma_q, &buf);

	if (dev->vbi_timeout_running == 1)
		mod_timer(&dev->vbi_timeout, jiffies + (HZ / 10));
	spin_unlock_irqrestore(&dev->slock, flags);
}


static int au0828_v4l2_open(struct file *filp)
{
	int ret = 0;
	struct video_device *vdev = video_devdata(filp);
	struct au0828_dev *dev = video_drvdata(filp);
	struct au0828_fh *fh;
	int type;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		break;
	case VFL_TYPE_VBI:
		type = V4L2_BUF_TYPE_VBI_CAPTURE;
		break;
	default:
		return -EINVAL;
	}

	fh = kzalloc(sizeof(struct au0828_fh), GFP_KERNEL);
	if (NULL == fh) {
		dprintk(1, "Failed allocate au0828_fh struct!\n");
		return -ENOMEM;
	}

	fh->type = type;
	fh->dev = dev;
	v4l2_fh_init(&fh->fh, vdev);
	filp->private_data = fh;

	if (mutex_lock_interruptible(&dev->lock)) {
		kfree(fh);
		return -ERESTARTSYS;
	}
	if (dev->users == 0) {
		/* set au0828 interface0 to AS5 here again */
		ret = usb_set_interface(dev->usbdev, 0, 5);
		if (ret < 0) {
			mutex_unlock(&dev->lock);
			printk(KERN_INFO "Au0828 can't set alternate to 5!\n");
			kfree(fh);
			return -EBUSY;
		}

		au0828_analog_stream_enable(dev);
		au0828_analog_stream_reset(dev);

		/* If we were doing ac97 instead of i2s, it would go here...*/
		au0828_i2s_init(dev);

		dev->stream_state = STREAM_OFF;
		dev->dev_state |= DEV_INITIALIZED;
	}

	dev->users++;
	mutex_unlock(&dev->lock);

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &au0828_video_qops,
				    NULL, &dev->slock,
				    V4L2_BUF_TYPE_VIDEO_CAPTURE,
				    V4L2_FIELD_INTERLACED,
				    sizeof(struct au0828_buffer), fh,
				    &dev->lock);

	/* VBI Setup */
	videobuf_queue_vmalloc_init(&fh->vb_vbiq, &au0828_vbi_qops,
				    NULL, &dev->slock,
				    V4L2_BUF_TYPE_VBI_CAPTURE,
				    V4L2_FIELD_SEQ_TB,
				    sizeof(struct au0828_buffer), fh,
				    &dev->lock);
	v4l2_fh_add(&fh->fh);
	return ret;
}

static int au0828_v4l2_close(struct file *filp)
{
	int ret;
	struct au0828_fh *fh = filp->private_data;
	struct au0828_dev *dev = fh->dev;

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	mutex_lock(&dev->lock);
	if (res_check(fh, AU0828_RESOURCE_VIDEO)) {
		/* Cancel timeout thread in case they didn't call streamoff */
		dev->vid_timeout_running = 0;
		del_timer_sync(&dev->vid_timeout);

		videobuf_stop(&fh->vb_vidq);
		res_free(fh, AU0828_RESOURCE_VIDEO);
	}

	if (res_check(fh, AU0828_RESOURCE_VBI)) {
		/* Cancel timeout thread in case they didn't call streamoff */
		dev->vbi_timeout_running = 0;
		del_timer_sync(&dev->vbi_timeout);

		videobuf_stop(&fh->vb_vbiq);
		res_free(fh, AU0828_RESOURCE_VBI);
	}

	if (dev->users == 1 && video_is_registered(video_devdata(filp))) {
		au0828_analog_stream_disable(dev);

		au0828_uninit_isoc(dev);

		/* Save some power by putting tuner to sleep */
		v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_power, 0);
		dev->std_set_in_tuner_core = 0;

		/* When close the device, set the usb intf0 into alt0 to free
		   USB bandwidth */
		ret = usb_set_interface(dev->usbdev, 0, 0);
		if (ret < 0)
			printk(KERN_INFO "Au0828 can't set alternate to 0!\n");
	}
	mutex_unlock(&dev->lock);

	videobuf_mmap_free(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vbiq);
	kfree(fh);
	dev->users--;
	wake_up_interruptible_nr(&dev->open, 1);
	return 0;
}

/* Must be called with dev->lock held */
static void au0828_init_tuner(struct au0828_dev *dev)
{
	struct v4l2_frequency f = {
		.frequency = dev->ctrl_freq,
		.type = V4L2_TUNER_ANALOG_TV,
	};

	if (dev->std_set_in_tuner_core)
		return;
	dev->std_set_in_tuner_core = 1;
	i2c_gate_ctrl(dev, 1);
	/* If we've never sent the standard in tuner core, do so now.
	   We don't do this at device probe because we don't want to
	   incur the cost of a firmware load */
	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_std, dev->std);
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);
	i2c_gate_ctrl(dev, 0);
}

static ssize_t au0828_v4l2_read(struct file *filp, char __user *buf,
				size_t count, loff_t *pos)
{
	struct au0828_fh *fh = filp->private_data;
	struct au0828_dev *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	au0828_init_tuner(dev);
	mutex_unlock(&dev->lock);

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (res_locked(dev, AU0828_RESOURCE_VIDEO))
			return -EBUSY;

		return videobuf_read_stream(&fh->vb_vidq, buf, count, pos, 0,
					filp->f_flags & O_NONBLOCK);
	}

	if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		if (!res_get(fh, AU0828_RESOURCE_VBI))
			return -EBUSY;

		if (dev->vbi_timeout_running == 0) {
			/* Handle case where caller tries to read without
			   calling streamon first */
			dev->vbi_timeout_running = 1;
			mod_timer(&dev->vbi_timeout, jiffies + (HZ / 10));
		}

		return videobuf_read_stream(&fh->vb_vbiq, buf, count, pos, 0,
					    filp->f_flags & O_NONBLOCK);
	}

	return 0;
}

static unsigned int au0828_v4l2_poll(struct file *filp, poll_table *wait)
{
	struct au0828_fh *fh = filp->private_data;
	struct au0828_dev *dev = fh->dev;
	unsigned long req_events = poll_requested_events(wait);
	unsigned int res;

	if (check_dev(dev) < 0)
		return POLLERR;

	res = v4l2_ctrl_poll(filp, wait);
	if (!(req_events & (POLLIN | POLLRDNORM)))
		return res;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	au0828_init_tuner(dev);
	mutex_unlock(&dev->lock);

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (!res_get(fh, AU0828_RESOURCE_VIDEO))
			return POLLERR;
		return res | videobuf_poll_stream(filp, &fh->vb_vidq, wait);
	}
	if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		if (!res_get(fh, AU0828_RESOURCE_VBI))
			return POLLERR;
		return res | videobuf_poll_stream(filp, &fh->vb_vbiq, wait);
	}
	return POLLERR;
}

static int au0828_v4l2_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct au0828_fh *fh    = filp->private_data;
	struct au0828_dev *dev   = fh->dev;
	int		 rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		rc = videobuf_mmap_mapper(&fh->vb_vidq, vma);
	else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)
		rc = videobuf_mmap_mapper(&fh->vb_vbiq, vma);

	return rc;
}

static int au0828_set_format(struct au0828_dev *dev, unsigned int cmd,
			     struct v4l2_format *format)
{
	int ret;
	int width = format->fmt.pix.width;
	int height = format->fmt.pix.height;

	/* If they are demanding a format other than the one we support,
	   bail out (tvtime asks for UYVY and then retries with YUYV) */
	if (format->fmt.pix.pixelformat != V4L2_PIX_FMT_UYVY)
		return -EINVAL;

	/* format->fmt.pix.width only support 720 and height 480 */
	if (width != 720)
		width = 720;
	if (height != 480)
		height = 480;

	format->fmt.pix.width = width;
	format->fmt.pix.height = height;
	format->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	format->fmt.pix.bytesperline = width * 2;
	format->fmt.pix.sizeimage = width * height * 2;
	format->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	format->fmt.pix.field = V4L2_FIELD_INTERLACED;
	format->fmt.pix.priv = 0;

	if (cmd == VIDIOC_TRY_FMT)
		return 0;

	/* maybe set new image format, driver current only support 720*480 */
	dev->width = width;
	dev->height = height;
	dev->frame_size = width * height * 2;
	dev->field_size = width * height;
	dev->bytesperline = width * 2;

	if (dev->stream_state == STREAM_ON) {
		dprintk(1, "VIDIOC_SET_FMT: interrupting stream!\n");
		ret = au0828_stream_interrupt(dev);
		if (ret != 0) {
			dprintk(1, "error interrupting video stream!\n");
			return ret;
		}
	}

	/* set au0828 interface0 to AS5 here again */
	ret = usb_set_interface(dev->usbdev, 0, 5);
	if (ret < 0) {
		printk(KERN_INFO "Au0828 can't set alt setting to 5!\n");
		return -EBUSY;
	}

	au0828_analog_stream_enable(dev);

	return 0;
}


static int vidioc_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	strlcpy(cap->driver, "au0828", sizeof(cap->driver));
	strlcpy(cap->card, dev->board.name, sizeof(cap->card));
	usb_make_path(dev->usbdev, cap->bus_info, sizeof(cap->bus_info));

	/* set the device capabilities */
	cap->device_caps = V4L2_CAP_AUDIO |
		V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING |
		V4L2_CAP_TUNER;
	if (vdev->vfl_type == VFL_TYPE_GRABBER)
		cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE;
	else
		cap->device_caps |= V4L2_CAP_VBI_CAPTURE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS |
		V4L2_CAP_VBI_CAPTURE | V4L2_CAP_VIDEO_CAPTURE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	strcpy(f->description, "Packed YUV2");

	f->flags = 0;
	f->pixelformat = V4L2_PIX_FMT_UYVY;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct au0828_fh *fh  = priv;
	struct au0828_dev *dev = fh->dev;

	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	f->fmt.pix.bytesperline = dev->bytesperline;
	f->fmt.pix.sizeimage = dev->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M; /* NTSC/PAL */
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.priv = 0;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct au0828_fh *fh  = priv;
	struct au0828_dev *dev = fh->dev;

	return au0828_set_format(dev, VIDIOC_TRY_FMT, f);
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct au0828_fh *fh  = priv;
	struct au0828_dev *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		printk(KERN_INFO "%s queue busy\n", __func__);
		rc = -EBUSY;
		goto out;
	}

	rc = au0828_set_format(dev, VIDIOC_S_FMT, f);
out:
	return rc;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id norm)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	dev->std = norm;

	au0828_init_tuner(dev);

	i2c_gate_ctrl(dev, 1);

	/* FIXME: when we support something other than NTSC, we are going to
	   have to make the au0828 bridge adjust the size of its capture
	   buffer, which is currently hardcoded at 720x480 */

	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_std, norm);

	i2c_gate_ctrl(dev, 0);

	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	*norm = dev->std;
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *input)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	unsigned int tmp;

	static const char *inames[] = {
		[AU0828_VMUX_UNDEFINED] = "Undefined",
		[AU0828_VMUX_COMPOSITE] = "Composite",
		[AU0828_VMUX_SVIDEO] = "S-Video",
		[AU0828_VMUX_CABLE] = "Cable TV",
		[AU0828_VMUX_TELEVISION] = "Television",
		[AU0828_VMUX_DVB] = "DVB",
		[AU0828_VMUX_DEBUG] = "tv debug"
	};

	tmp = input->index;

	if (tmp >= AU0828_MAX_INPUT)
		return -EINVAL;
	if (AUVI_INPUT(tmp).type == 0)
		return -EINVAL;

	input->index = tmp;
	strcpy(input->name, inames[AUVI_INPUT(tmp).type]);
	if ((AUVI_INPUT(tmp).type == AU0828_VMUX_TELEVISION) ||
	    (AUVI_INPUT(tmp).type == AU0828_VMUX_CABLE)) {
		input->type |= V4L2_INPUT_TYPE_TUNER;
		input->audioset = 1;
	} else {
		input->type |= V4L2_INPUT_TYPE_CAMERA;
		input->audioset = 2;
	}

	input->std = dev->vdev->tvnorms;

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	*i = dev->ctrl_input;
	return 0;
}

static void au0828_s_input(struct au0828_dev *dev, int index)
{
	int i;

	switch (AUVI_INPUT(index).type) {
	case AU0828_VMUX_SVIDEO:
		dev->input_type = AU0828_VMUX_SVIDEO;
		dev->ctrl_ainput = 1;
		break;
	case AU0828_VMUX_COMPOSITE:
		dev->input_type = AU0828_VMUX_COMPOSITE;
		dev->ctrl_ainput = 1;
		break;
	case AU0828_VMUX_TELEVISION:
		dev->input_type = AU0828_VMUX_TELEVISION;
		dev->ctrl_ainput = 0;
		break;
	default:
		dprintk(1, "unknown input type set [%d]\n",
			AUVI_INPUT(index).type);
		break;
	}

	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_routing,
			AUVI_INPUT(index).vmux, 0, 0);

	for (i = 0; i < AU0828_MAX_INPUT; i++) {
		int enable = 0;
		if (AUVI_INPUT(i).audio_setup == NULL)
			continue;

		if (i == index)
			enable = 1;
		else
			enable = 0;
		if (enable) {
			(AUVI_INPUT(i).audio_setup)(dev, enable);
		} else {
			/* Make sure we leave it turned on if some
			   other input is routed to this callback */
			if ((AUVI_INPUT(i).audio_setup) !=
			    ((AUVI_INPUT(index).audio_setup))) {
				(AUVI_INPUT(i).audio_setup)(dev, enable);
			}
		}
	}

	v4l2_device_call_all(&dev->v4l2_dev, 0, audio, s_routing,
			AUVI_INPUT(index).amux, 0, 0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int index)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	dprintk(1, "VIDIOC_S_INPUT in function %s, input=%d\n", __func__,
		index);
	if (index >= AU0828_MAX_INPUT)
		return -EINVAL;
	if (AUVI_INPUT(index).type == 0)
		return -EINVAL;
	dev->ctrl_input = index;
	au0828_s_input(dev, index);
	return 0;
}

static int vidioc_enumaudio(struct file *file, void *priv, struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	if (a->index == 0)
		strcpy(a->name, "Television");
	else
		strcpy(a->name, "Line in");

	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	a->index = dev->ctrl_ainput;
	if (a->index == 0)
		strcpy(a->name, "Television");
	else
		strcpy(a->name, "Line in");

	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv, const struct v4l2_audio *a)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	if (a->index != dev->ctrl_ainput)
		return -EINVAL;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	if (t->index != 0)
		return -EINVAL;

	strcpy(t->name, "Auvitek tuner");

	au0828_init_tuner(dev);
	i2c_gate_ctrl(dev, 1);
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_tuner, t);
	i2c_gate_ctrl(dev, 0);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	if (t->index != 0)
		return -EINVAL;

	au0828_init_tuner(dev);
	i2c_gate_ctrl(dev, 1);
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_tuner, t);
	i2c_gate_ctrl(dev, 0);

	dprintk(1, "VIDIOC_S_TUNER: signal = %x, afc = %x\n", t->signal,
		t->afc);

	return 0;

}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *freq)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	if (freq->tuner != 0)
		return -EINVAL;
	freq->frequency = dev->ctrl_freq;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *freq)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	struct v4l2_frequency new_freq = *freq;

	if (freq->tuner != 0)
		return -EINVAL;

	au0828_init_tuner(dev);
	i2c_gate_ctrl(dev, 1);

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, freq);
	/* Get the actual set (and possibly clamped) frequency */
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_frequency, &new_freq);
	dev->ctrl_freq = new_freq.frequency;

	i2c_gate_ctrl(dev, 0);

	au0828_analog_stream_reset(dev);

	return 0;
}


/* RAW VBI ioctls */

static int vidioc_g_fmt_vbi_cap(struct file *file, void *priv,
				struct v4l2_format *format)
{
	struct au0828_fh      *fh  = priv;
	struct au0828_dev     *dev = fh->dev;

	format->fmt.vbi.samples_per_line = dev->vbi_width;
	format->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	format->fmt.vbi.offset = 0;
	format->fmt.vbi.flags = 0;
	format->fmt.vbi.sampling_rate = 6750000 * 4 / 2;

	format->fmt.vbi.count[0] = dev->vbi_height;
	format->fmt.vbi.count[1] = dev->vbi_height;
	format->fmt.vbi.start[0] = 21;
	format->fmt.vbi.start[1] = 284;
	memset(format->fmt.vbi.reserved, 0, sizeof(format->fmt.vbi.reserved));

	return 0;
}

static int vidioc_cropcap(struct file *file, void *priv,
			  struct v4l2_cropcap *cc)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	if (cc->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cc->bounds.left = 0;
	cc->bounds.top = 0;
	cc->bounds.width = dev->width;
	cc->bounds.height = dev->height;

	cc->defrect = cc->bounds;

	cc->pixelaspect.numerator = 54;
	cc->pixelaspect.denominator = 59;

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct au0828_fh      *fh  = priv;
	struct au0828_dev     *dev = fh->dev;
	int                   rc = -EINVAL;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (unlikely(type != fh->type))
		return -EINVAL;

	dprintk(1, "vidioc_streamon fh=%p t=%d fh->res=%d dev->res=%d\n",
		fh, type, fh->resources, dev->resources);

	if (unlikely(!res_get(fh, get_ressource(fh))))
		return -EBUSY;

	au0828_init_tuner(dev);
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		au0828_analog_stream_enable(dev);
		v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 1);
	}

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		rc = videobuf_streamon(&fh->vb_vidq);
		dev->vid_timeout_running = 1;
		mod_timer(&dev->vid_timeout, jiffies + (HZ / 10));
	} else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		rc = videobuf_streamon(&fh->vb_vbiq);
		dev->vbi_timeout_running = 1;
		mod_timer(&dev->vbi_timeout, jiffies + (HZ / 10));
	}

	return rc;
}

static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct au0828_fh      *fh  = priv;
	struct au0828_dev     *dev = fh->dev;
	int                   rc;
	int                   i;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    fh->type != V4L2_BUF_TYPE_VBI_CAPTURE)
		return -EINVAL;
	if (type != fh->type)
		return -EINVAL;

	dprintk(1, "vidioc_streamoff fh=%p t=%d fh->res=%d dev->res=%d\n",
		fh, type, fh->resources, dev->resources);

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		dev->vid_timeout_running = 0;
		del_timer_sync(&dev->vid_timeout);

		v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 0);
		rc = au0828_stream_interrupt(dev);
		if (rc != 0)
			return rc;

		for (i = 0; i < AU0828_MAX_INPUT; i++) {
			if (AUVI_INPUT(i).audio_setup == NULL)
				continue;
			(AUVI_INPUT(i).audio_setup)(dev, 0);
		}

		if (res_check(fh, AU0828_RESOURCE_VIDEO)) {
			videobuf_streamoff(&fh->vb_vidq);
			res_free(fh, AU0828_RESOURCE_VIDEO);
		}
	} else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
		dev->vbi_timeout_running = 0;
		del_timer_sync(&dev->vbi_timeout);

		if (res_check(fh, AU0828_RESOURCE_VBI)) {
			videobuf_streamoff(&fh->vb_vbiq);
			res_free(fh, AU0828_RESOURCE_VBI);
		}
	}

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *priv,
			     struct v4l2_dbg_register *reg)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	reg->val = au0828_read(dev, reg->reg);
	reg->size = 1;
	return 0;
}

static int vidioc_s_register(struct file *file, void *priv,
			     const struct v4l2_dbg_register *reg)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;

	return au0828_writereg(dev, reg->reg, reg->val);
}
#endif

static int vidioc_log_status(struct file *file, void *fh)
{
	struct video_device *vdev = video_devdata(file);

	v4l2_ctrl_log_status(file, fh);
	v4l2_device_call_all(vdev->v4l2_dev, 0, core, log_status);
	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *rb)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		rc = videobuf_reqbufs(&fh->vb_vidq, rb);
	else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)
		rc = videobuf_reqbufs(&fh->vb_vbiq, rb);

	return rc;
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *b)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		rc = videobuf_querybuf(&fh->vb_vidq, b);
	else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)
		rc = videobuf_querybuf(&fh->vb_vbiq, b);

	return rc;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		rc = videobuf_qbuf(&fh->vb_vidq, b);
	else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)
		rc = videobuf_qbuf(&fh->vb_vbiq, b);

	return rc;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct au0828_fh *fh = priv;
	struct au0828_dev *dev = fh->dev;
	int rc;

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	/* Workaround for a bug in the au0828 hardware design that sometimes
	   results in the colorspace being inverted */
	if (dev->greenscreen_detected == 1) {
		dprintk(1, "Detected green frame.  Resetting stream...\n");
		au0828_analog_stream_reset(dev);
		dev->greenscreen_detected = 0;
	}

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		rc = videobuf_dqbuf(&fh->vb_vidq, b, file->f_flags & O_NONBLOCK);
	else if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)
		rc = videobuf_dqbuf(&fh->vb_vbiq, b, file->f_flags & O_NONBLOCK);

	return rc;
}

static struct v4l2_file_operations au0828_v4l_fops = {
	.owner      = THIS_MODULE,
	.open       = au0828_v4l2_open,
	.release    = au0828_v4l2_close,
	.read       = au0828_v4l2_read,
	.poll       = au0828_v4l2_poll,
	.mmap       = au0828_v4l2_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap            = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap    = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap       = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap     = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap       = vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap       = vidioc_g_fmt_vbi_cap,
	.vidioc_try_fmt_vbi_cap     = vidioc_g_fmt_vbi_cap,
	.vidioc_s_fmt_vbi_cap       = vidioc_g_fmt_vbi_cap,
	.vidioc_enumaudio           = vidioc_enumaudio,
	.vidioc_g_audio             = vidioc_g_audio,
	.vidioc_s_audio             = vidioc_s_audio,
	.vidioc_cropcap             = vidioc_cropcap,
	.vidioc_reqbufs             = vidioc_reqbufs,
	.vidioc_querybuf            = vidioc_querybuf,
	.vidioc_qbuf                = vidioc_qbuf,
	.vidioc_dqbuf               = vidioc_dqbuf,
	.vidioc_s_std               = vidioc_s_std,
	.vidioc_g_std               = vidioc_g_std,
	.vidioc_enum_input          = vidioc_enum_input,
	.vidioc_g_input             = vidioc_g_input,
	.vidioc_s_input             = vidioc_s_input,
	.vidioc_streamon            = vidioc_streamon,
	.vidioc_streamoff           = vidioc_streamoff,
	.vidioc_g_tuner             = vidioc_g_tuner,
	.vidioc_s_tuner             = vidioc_s_tuner,
	.vidioc_g_frequency         = vidioc_g_frequency,
	.vidioc_s_frequency         = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register          = vidioc_g_register,
	.vidioc_s_register          = vidioc_s_register,
#endif
	.vidioc_log_status	    = vidioc_log_status,
	.vidioc_subscribe_event     = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event   = v4l2_event_unsubscribe,
};

static const struct video_device au0828_video_template = {
	.fops                       = &au0828_v4l_fops,
	.release                    = video_device_release,
	.ioctl_ops 		    = &video_ioctl_ops,
	.tvnorms                    = V4L2_STD_NTSC_M,
};

/**************************************************************************/

int au0828_analog_register(struct au0828_dev *dev,
			   struct usb_interface *interface)
{
	int retval = -ENOMEM;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, ret;

	dprintk(1, "au0828_analog_register called!\n");

	/* set au0828 usb interface0 to as5 */
	retval = usb_set_interface(dev->usbdev,
			interface->cur_altsetting->desc.bInterfaceNumber, 5);
	if (retval != 0) {
		printk(KERN_INFO "Failure setting usb interface0 to as5\n");
		return retval;
	}

	/* Figure out which endpoint has the isoc interface */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		     == USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		     == USB_ENDPOINT_XFER_ISOC)) {

			/* we find our isoc in endpoint */
			u16 tmp = le16_to_cpu(endpoint->wMaxPacketSize);
			dev->max_pkt_size = (tmp & 0x07ff) *
				(((tmp & 0x1800) >> 11) + 1);
			dev->isoc_in_endpointaddr = endpoint->bEndpointAddress;
		}
	}
	if (!(dev->isoc_in_endpointaddr)) {
		printk(KERN_INFO "Could not locate isoc endpoint\n");
		kfree(dev);
		return -ENODEV;
	}

	init_waitqueue_head(&dev->open);
	spin_lock_init(&dev->slock);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);
	INIT_LIST_HEAD(&dev->vbiq.active);
	INIT_LIST_HEAD(&dev->vbiq.queued);

	dev->vid_timeout.function = au0828_vid_buffer_timeout;
	dev->vid_timeout.data = (unsigned long) dev;
	init_timer(&dev->vid_timeout);

	dev->vbi_timeout.function = au0828_vbi_buffer_timeout;
	dev->vbi_timeout.data = (unsigned long) dev;
	init_timer(&dev->vbi_timeout);

	dev->width = NTSC_STD_W;
	dev->height = NTSC_STD_H;
	dev->field_size = dev->width * dev->height;
	dev->frame_size = dev->field_size << 1;
	dev->bytesperline = dev->width << 1;
	dev->vbi_width = 720;
	dev->vbi_height = 1;
	dev->ctrl_ainput = 0;
	dev->ctrl_freq = 960;
	dev->std = V4L2_STD_NTSC_M;
	au0828_s_input(dev, 0);

	/* allocate and fill v4l2 video struct */
	dev->vdev = video_device_alloc();
	if (NULL == dev->vdev) {
		dprintk(1, "Can't allocate video_device.\n");
		return -ENOMEM;
	}

	/* allocate the VBI struct */
	dev->vbi_dev = video_device_alloc();
	if (NULL == dev->vbi_dev) {
		dprintk(1, "Can't allocate vbi_device.\n");
		ret = -ENOMEM;
		goto err_vdev;
	}

	/* Fill the video capture device struct */
	*dev->vdev = au0828_video_template;
	dev->vdev->v4l2_dev = &dev->v4l2_dev;
	dev->vdev->lock = &dev->lock;
	set_bit(V4L2_FL_USE_FH_PRIO, &dev->vdev->flags);
	strcpy(dev->vdev->name, "au0828a video");

	/* Setup the VBI device */
	*dev->vbi_dev = au0828_video_template;
	dev->vbi_dev->v4l2_dev = &dev->v4l2_dev;
	dev->vbi_dev->lock = &dev->lock;
	set_bit(V4L2_FL_USE_FH_PRIO, &dev->vbi_dev->flags);
	strcpy(dev->vbi_dev->name, "au0828a vbi");

	/* Register the v4l2 device */
	video_set_drvdata(dev->vdev, dev);
	retval = video_register_device(dev->vdev, VFL_TYPE_GRABBER, -1);
	if (retval != 0) {
		dprintk(1, "unable to register video device (error = %d).\n",
			retval);
		ret = -ENODEV;
		goto err_vbi_dev;
	}

	/* Register the vbi device */
	video_set_drvdata(dev->vbi_dev, dev);
	retval = video_register_device(dev->vbi_dev, VFL_TYPE_VBI, -1);
	if (retval != 0) {
		dprintk(1, "unable to register vbi device (error = %d).\n",
			retval);
		ret = -ENODEV;
		goto err_vbi_dev;
	}

	dprintk(1, "%s completed!\n", __func__);

	return 0;

err_vbi_dev:
	video_device_release(dev->vbi_dev);
err_vdev:
	video_device_release(dev->vdev);
	return ret;
}

