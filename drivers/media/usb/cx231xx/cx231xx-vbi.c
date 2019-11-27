// SPDX-License-Identifier: GPL-2.0-or-later
/*
   cx231xx_vbi.c - driver for Conexant Cx23100/101/102 USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
	Based on cx88 driver

 */

#include "cx231xx.h"
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/i2c.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/drv-intf/msp3400.h>
#include <media/tuner.h>

#include "cx231xx-vbi.h"

static inline void print_err_status(struct cx231xx *dev, int packet, int status)
{
	char *errmsg = "Unknown";

	switch (status) {
	case -ENOENT:
		errmsg = "unlinked synchronously";
		break;
	case -ECONNRESET:
		errmsg = "unlinked asynchronously";
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
		dev_err(dev->dev,
			"URB status %d [%s].\n", status, errmsg);
	} else {
		dev_err(dev->dev,
			"URB packet %d, status %d [%s].\n",
			packet, status, errmsg);
	}
}

/*
 * Controls the isoc copy of each urb packet
 */
static inline int cx231xx_isoc_vbi_copy(struct cx231xx *dev, struct urb *urb)
{
	struct cx231xx_dmaqueue *dma_q = urb->context;
	int rc = 1;
	unsigned char *p_buffer;
	u32 bytes_parsed = 0, buffer_size = 0;
	u8 sav_eav = 0;

	if (!dev)
		return 0;

	if (dev->state & DEV_DISCONNECTED)
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	/* get buffer pointer and length */
	p_buffer = urb->transfer_buffer;
	buffer_size = urb->actual_length;

	if (buffer_size > 0) {
		bytes_parsed = 0;

		if (dma_q->is_partial_line) {
			/* Handle the case where we were working on a partial
			   line */
			sav_eav = dma_q->last_sav;
		} else {
			/* Check for a SAV/EAV overlapping the
			   buffer boundary */

			sav_eav = cx231xx_find_boundary_SAV_EAV(p_buffer,
							  dma_q->partial_buf,
							  &bytes_parsed);
		}

		sav_eav &= 0xF0;
		/* Get the first line if we have some portion of an SAV/EAV from
		   the last buffer or a partial line */
		if (sav_eav) {
			bytes_parsed += cx231xx_get_vbi_line(dev, dma_q,
				sav_eav,		       /* SAV/EAV */
				p_buffer + bytes_parsed,       /* p_buffer */
				buffer_size - bytes_parsed);   /* buffer size */
		}

		/* Now parse data that is completely in this buffer */
		dma_q->is_partial_line = 0;

		while (bytes_parsed < buffer_size) {
			u32 bytes_used = 0;

			sav_eav = cx231xx_find_next_SAV_EAV(
				p_buffer + bytes_parsed,	/* p_buffer */
				buffer_size - bytes_parsed, /* buffer size */
				&bytes_used);	/* bytes used to get SAV/EAV */

			bytes_parsed += bytes_used;

			sav_eav &= 0xF0;
			if (sav_eav && (bytes_parsed < buffer_size)) {
				bytes_parsed += cx231xx_get_vbi_line(dev,
					dma_q, sav_eav,	/* SAV/EAV */
					p_buffer+bytes_parsed, /* p_buffer */
					buffer_size-bytes_parsed);/*buf size*/
			}
		}

		/* Save the last four bytes of the buffer so we can
		check the buffer boundary condition next time */
		memcpy(dma_q->partial_buf, p_buffer + buffer_size - 4, 4);
		bytes_parsed = 0;
	}

	return rc;
}

/* ------------------------------------------------------------------
	Vbi buf operations
   ------------------------------------------------------------------*/

static int vbi_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);
	u32 height = 0;

	height = ((dev->norm & V4L2_STD_625_50) ?
		  PAL_VBI_LINES : NTSC_VBI_LINES);

	*nplanes = 1;
	sizes[0] = (dev->width * height * 2 * 2);
	return 0;
}

/* This is called *without* dev->slock held; please keep it that way */
static int vbi_buf_prepare(struct vb2_buffer *vb)
{
	struct cx231xx *dev = vb2_get_drv_priv(vb->vb2_queue);
	u32 height = 0;
	u32 size;

	height = ((dev->norm & V4L2_STD_625_50) ?
		  PAL_VBI_LINES : NTSC_VBI_LINES);
	size = ((dev->width << 1) * height * 2);

	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void vbi_buf_queue(struct vb2_buffer *vb)
{
	struct cx231xx *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct cx231xx_buffer *buf =
	    container_of(vb, struct cx231xx_buffer, vb.vb2_buf);
	struct cx231xx_dmaqueue *vidq = &dev->vbi_mode.vidq;
	unsigned long flags;

	spin_lock_irqsave(&dev->vbi_mode.slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&dev->vbi_mode.slock, flags);
}

static void return_all_buffers(struct cx231xx *dev,
			       enum vb2_buffer_state state)
{
	struct cx231xx_dmaqueue *vidq = &dev->vbi_mode.vidq;
	struct cx231xx_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&dev->vbi_mode.slock, flags);
	dev->vbi_mode.bulk_ctl.buf = NULL;
	list_for_each_entry_safe(buf, node, &vidq->active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&dev->vbi_mode.slock, flags);
}

static int vbi_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);
	struct cx231xx_dmaqueue *vidq = &dev->vbi_mode.vidq;
	int ret;

	vidq->sequence = 0;
	ret = cx231xx_init_vbi_isoc(dev, CX231XX_NUM_VBI_PACKETS,
				    CX231XX_NUM_VBI_BUFS,
				    dev->vbi_mode.alt_max_pkt_size[0],
				    cx231xx_isoc_vbi_copy);
	if (ret)
		return_all_buffers(dev, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void vbi_stop_streaming(struct vb2_queue *vq)
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);

	return_all_buffers(dev, VB2_BUF_STATE_ERROR);
}

struct vb2_ops cx231xx_vbi_qops = {
	.queue_setup = vbi_queue_setup,
	.buf_prepare = vbi_buf_prepare,
	.buf_queue = vbi_buf_queue,
	.start_streaming = vbi_start_streaming,
	.stop_streaming = vbi_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

/* ------------------------------------------------------------------
	URB control
   ------------------------------------------------------------------*/

/*
 * IRQ callback, called by URB callback
 */
static void cx231xx_irq_vbi_callback(struct urb *urb)
{
	struct cx231xx_dmaqueue *dma_q = urb->context;
	struct cx231xx_video_mode *vmode =
	    container_of(dma_q, struct cx231xx_video_mode, vidq);
	struct cx231xx *dev = container_of(vmode, struct cx231xx, vbi_mode);
	unsigned long flags;

	switch (urb->status) {
	case 0:		/* success */
	case -ETIMEDOUT:	/* NAK */
		break;
	case -ECONNRESET:	/* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:		/* error */
		dev_err(dev->dev,
			"urb completion error %d.\n", urb->status);
		break;
	}

	/* Copy data from URB */
	spin_lock_irqsave(&dev->vbi_mode.slock, flags);
	dev->vbi_mode.bulk_ctl.bulk_copy(dev, urb);
	spin_unlock_irqrestore(&dev->vbi_mode.slock, flags);

	/* Reset status */
	urb->status = 0;

	urb->status = usb_submit_urb(urb, GFP_ATOMIC);
	if (urb->status) {
		dev_err(dev->dev, "urb resubmit failed (error=%i)\n",
			urb->status);
	}
}

/*
 * Stop and Deallocate URBs
 */
void cx231xx_uninit_vbi_isoc(struct cx231xx *dev)
{
	struct urb *urb;
	int i;

	dev_dbg(dev->dev, "called cx231xx_uninit_vbi_isoc\n");

	dev->vbi_mode.bulk_ctl.nfields = -1;
	for (i = 0; i < dev->vbi_mode.bulk_ctl.num_bufs; i++) {
		urb = dev->vbi_mode.bulk_ctl.urb[i];
		if (urb) {
			if (!irqs_disabled())
				usb_kill_urb(urb);
			else
				usb_unlink_urb(urb);

			if (dev->vbi_mode.bulk_ctl.transfer_buffer[i]) {

				kfree(dev->vbi_mode.bulk_ctl.
				      transfer_buffer[i]);
				dev->vbi_mode.bulk_ctl.transfer_buffer[i] =
				    NULL;
			}
			usb_free_urb(urb);
			dev->vbi_mode.bulk_ctl.urb[i] = NULL;
		}
		dev->vbi_mode.bulk_ctl.transfer_buffer[i] = NULL;
	}

	kfree(dev->vbi_mode.bulk_ctl.urb);
	kfree(dev->vbi_mode.bulk_ctl.transfer_buffer);

	dev->vbi_mode.bulk_ctl.urb = NULL;
	dev->vbi_mode.bulk_ctl.transfer_buffer = NULL;
	dev->vbi_mode.bulk_ctl.num_bufs = 0;

	cx231xx_capture_start(dev, 0, Vbi);
}
EXPORT_SYMBOL_GPL(cx231xx_uninit_vbi_isoc);

/*
 * Allocate URBs and start IRQ
 */
int cx231xx_init_vbi_isoc(struct cx231xx *dev, int max_packets,
			  int num_bufs, int max_pkt_size,
			  int (*bulk_copy) (struct cx231xx *dev,
					    struct urb *urb))
{
	struct cx231xx_dmaqueue *dma_q = &dev->vbi_mode.vidq;
	int i;
	int sb_size, pipe;
	struct urb *urb;
	int rc;

	dev_dbg(dev->dev, "called cx231xx_vbi_isoc\n");

	/* De-allocates all pending stuff */
	cx231xx_uninit_vbi_isoc(dev);

	/* clear if any halt */
	usb_clear_halt(dev->udev,
		       usb_rcvbulkpipe(dev->udev,
				       dev->vbi_mode.end_point_addr));

	dev->vbi_mode.bulk_ctl.bulk_copy = bulk_copy;
	dev->vbi_mode.bulk_ctl.num_bufs = num_bufs;
	dma_q->pos = 0;
	dma_q->is_partial_line = 0;
	dma_q->last_sav = 0;
	dma_q->current_field = -1;
	dma_q->bytes_left_in_line = dev->width << 1;
	dma_q->lines_per_field = ((dev->norm & V4L2_STD_625_50) ?
				  PAL_VBI_LINES : NTSC_VBI_LINES);
	dma_q->lines_completed = 0;
	for (i = 0; i < 8; i++)
		dma_q->partial_buf[i] = 0;

	dev->vbi_mode.bulk_ctl.urb = kcalloc(num_bufs, sizeof(void *),
					     GFP_KERNEL);
	if (!dev->vbi_mode.bulk_ctl.urb) {
		dev_err(dev->dev,
			"cannot alloc memory for usb buffers\n");
		return -ENOMEM;
	}

	dev->vbi_mode.bulk_ctl.transfer_buffer =
	    kcalloc(num_bufs, sizeof(void *), GFP_KERNEL);
	if (!dev->vbi_mode.bulk_ctl.transfer_buffer) {
		dev_err(dev->dev,
			"cannot allocate memory for usbtransfer\n");
		kfree(dev->vbi_mode.bulk_ctl.urb);
		return -ENOMEM;
	}

	dev->vbi_mode.bulk_ctl.max_pkt_size = max_pkt_size;
	dev->vbi_mode.bulk_ctl.buf = NULL;

	sb_size = max_packets * dev->vbi_mode.bulk_ctl.max_pkt_size;

	/* allocate urbs and transfer buffers */
	for (i = 0; i < dev->vbi_mode.bulk_ctl.num_bufs; i++) {

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			cx231xx_uninit_vbi_isoc(dev);
			return -ENOMEM;
		}
		dev->vbi_mode.bulk_ctl.urb[i] = urb;
		urb->transfer_flags = 0;

		dev->vbi_mode.bulk_ctl.transfer_buffer[i] =
		    kzalloc(sb_size, GFP_KERNEL);
		if (!dev->vbi_mode.bulk_ctl.transfer_buffer[i]) {
			dev_err(dev->dev,
				"unable to allocate %i bytes for transfer buffer %i%s\n",
				sb_size, i,
				in_interrupt() ? " while in int" : "");
			cx231xx_uninit_vbi_isoc(dev);
			return -ENOMEM;
		}

		pipe = usb_rcvbulkpipe(dev->udev, dev->vbi_mode.end_point_addr);
		usb_fill_bulk_urb(urb, dev->udev, pipe,
				  dev->vbi_mode.bulk_ctl.transfer_buffer[i],
				  sb_size, cx231xx_irq_vbi_callback, dma_q);
	}

	init_waitqueue_head(&dma_q->wq);

	/* submit urbs and enables IRQ */
	for (i = 0; i < dev->vbi_mode.bulk_ctl.num_bufs; i++) {
		rc = usb_submit_urb(dev->vbi_mode.bulk_ctl.urb[i], GFP_ATOMIC);
		if (rc) {
			dev_err(dev->dev,
				"submit of urb %i failed (error=%i)\n", i, rc);
			cx231xx_uninit_vbi_isoc(dev);
			return rc;
		}
	}

	cx231xx_capture_start(dev, 1, Vbi);

	return 0;
}
EXPORT_SYMBOL_GPL(cx231xx_init_vbi_isoc);

u32 cx231xx_get_vbi_line(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			 u8 sav_eav, u8 *p_buffer, u32 buffer_size)
{
	u32 bytes_copied = 0;
	int current_field = -1;

	switch (sav_eav) {

	case SAV_VBI_FIELD1:
		current_field = 1;
		break;

	case SAV_VBI_FIELD2:
		current_field = 2;
		break;
	default:
		break;
	}

	if (current_field < 0)
		return bytes_copied;

	dma_q->last_sav = sav_eav;

	bytes_copied =
	    cx231xx_copy_vbi_line(dev, dma_q, p_buffer, buffer_size,
				  current_field);

	return bytes_copied;
}

/*
 * Announces that a buffer were filled and request the next
 */
static inline void vbi_buffer_filled(struct cx231xx *dev,
				     struct cx231xx_dmaqueue *dma_q,
				     struct cx231xx_buffer *buf)
{
	/* Advice that buffer was filled */
	/* dev_dbg(dev->dev, "[%p/%d] wakeup\n", buf, buf->vb.index); */

	buf->vb.sequence = dma_q->sequence++;
	buf->vb.vb2_buf.timestamp = ktime_get_ns();

	dev->vbi_mode.bulk_ctl.buf = NULL;

	list_del(&buf->list);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

u32 cx231xx_copy_vbi_line(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			  u8 *p_line, u32 length, int field_number)
{
	u32 bytes_to_copy;
	struct cx231xx_buffer *buf;
	u32 _line_size = dev->width * 2;

	if (dma_q->current_field == -1) {
		/* Just starting up */
		cx231xx_reset_vbi_buffer(dev, dma_q);
	}

	if (dma_q->current_field != field_number)
		dma_q->lines_completed = 0;

	/* get the buffer pointer */
	buf = dev->vbi_mode.bulk_ctl.buf;

	/* Remember the field number for next time */
	dma_q->current_field = field_number;

	bytes_to_copy = dma_q->bytes_left_in_line;
	if (bytes_to_copy > length)
		bytes_to_copy = length;

	if (dma_q->lines_completed >= dma_q->lines_per_field) {
		dma_q->bytes_left_in_line -= bytes_to_copy;
		dma_q->is_partial_line =
		    (dma_q->bytes_left_in_line == 0) ? 0 : 1;
		return 0;
	}

	dma_q->is_partial_line = 1;

	/* If we don't have a buffer, just return the number of bytes we would
	   have copied if we had a buffer. */
	if (!buf) {
		dma_q->bytes_left_in_line -= bytes_to_copy;
		dma_q->is_partial_line =
		    (dma_q->bytes_left_in_line == 0) ? 0 : 1;
		return bytes_to_copy;
	}

	/* copy the data to video buffer */
	cx231xx_do_vbi_copy(dev, dma_q, p_line, bytes_to_copy);

	dma_q->pos += bytes_to_copy;
	dma_q->bytes_left_in_line -= bytes_to_copy;

	if (dma_q->bytes_left_in_line == 0) {

		dma_q->bytes_left_in_line = _line_size;
		dma_q->lines_completed++;
		dma_q->is_partial_line = 0;

		if (cx231xx_is_vbi_buffer_done(dev, dma_q) && buf) {

			vbi_buffer_filled(dev, dma_q, buf);

			dma_q->pos = 0;
			dma_q->lines_completed = 0;
			cx231xx_reset_vbi_buffer(dev, dma_q);
		}
	}

	return bytes_to_copy;
}

/*
 * video-buf generic routine to get the next available buffer
 */
static inline void get_next_vbi_buf(struct cx231xx_dmaqueue *dma_q,
				    struct cx231xx_buffer **buf)
{
	struct cx231xx_video_mode *vmode =
	    container_of(dma_q, struct cx231xx_video_mode, vidq);
	struct cx231xx *dev = container_of(vmode, struct cx231xx, vbi_mode);
	char *outp;

	if (list_empty(&dma_q->active)) {
		dev_err(dev->dev, "No active queue to serve\n");
		dev->vbi_mode.bulk_ctl.buf = NULL;
		*buf = NULL;
		return;
	}

	/* Get the next buffer */
	*buf = list_entry(dma_q->active.next, struct cx231xx_buffer, list);

	/* Cleans up buffer - Useful for testing for frame/URB loss */
	outp = vb2_plane_vaddr(&(*buf)->vb.vb2_buf, 0);
	memset(outp, 0, vb2_plane_size(&(*buf)->vb.vb2_buf, 0));

	dev->vbi_mode.bulk_ctl.buf = *buf;

	return;
}

void cx231xx_reset_vbi_buffer(struct cx231xx *dev,
			      struct cx231xx_dmaqueue *dma_q)
{
	struct cx231xx_buffer *buf;

	buf = dev->vbi_mode.bulk_ctl.buf;

	if (buf == NULL) {
		/* first try to get the buffer */
		get_next_vbi_buf(dma_q, &buf);

		dma_q->pos = 0;
		dma_q->current_field = -1;
	}

	dma_q->bytes_left_in_line = dev->width << 1;
	dma_q->lines_completed = 0;
}

int cx231xx_do_vbi_copy(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			u8 *p_buffer, u32 bytes_to_copy)
{
	u8 *p_out_buffer = NULL;
	u32 current_line_bytes_copied = 0;
	struct cx231xx_buffer *buf;
	u32 _line_size = dev->width << 1;
	void *startwrite;
	int offset, lencopy;

	buf = dev->vbi_mode.bulk_ctl.buf;

	if (buf == NULL)
		return -EINVAL;

	p_out_buffer = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	if (dma_q->bytes_left_in_line != _line_size) {
		current_line_bytes_copied =
		    _line_size - dma_q->bytes_left_in_line;
	}

	offset = (dma_q->lines_completed * _line_size) +
		 current_line_bytes_copied;

	if (dma_q->current_field == 2) {
		/* Populate the second half of the frame */
		offset += (dev->width * 2 * dma_q->lines_per_field);
	}

	/* prepare destination address */
	startwrite = p_out_buffer + offset;

	lencopy = dma_q->bytes_left_in_line > bytes_to_copy ?
		  bytes_to_copy : dma_q->bytes_left_in_line;

	memcpy(startwrite, p_buffer, lencopy);

	return 0;
}

u8 cx231xx_is_vbi_buffer_done(struct cx231xx *dev,
			      struct cx231xx_dmaqueue *dma_q)
{
	u32 height = 0;

	height = ((dev->norm & V4L2_STD_625_50) ?
		  PAL_VBI_LINES : NTSC_VBI_LINES);
	if (dma_q->lines_completed == height && dma_q->current_field == 2)
		return 1;
	else
		return 0;
}
