/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include "stk1160.h"

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

static inline void print_err_status(struct stk1160 *dev,
				     int packet, int status)
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

	if (packet < 0)
		printk_ratelimited(KERN_WARNING "URB status %d [%s].\n",
				status, errmsg);
	else
		printk_ratelimited(KERN_INFO "URB packet %d, status %d [%s].\n",
			       packet, status, errmsg);
}

static inline
struct stk1160_buffer *stk1160_next_buffer(struct stk1160 *dev)
{
	struct stk1160_buffer *buf = NULL;
	unsigned long flags = 0;

	/* Current buffer must be NULL when this functions gets called */
	WARN_ON(dev->isoc_ctl.buf);

	spin_lock_irqsave(&dev->buf_lock, flags);
	if (!list_empty(&dev->avail_bufs)) {
		buf = list_first_entry(&dev->avail_bufs,
				struct stk1160_buffer, list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&dev->buf_lock, flags);

	return buf;
}

static inline
void stk1160_buffer_done(struct stk1160 *dev)
{
	struct stk1160_buffer *buf = dev->isoc_ctl.buf;

	buf->vb.sequence = dev->sequence++;
	buf->vb.field = V4L2_FIELD_INTERLACED;
	buf->vb.vb2_buf.timestamp = ktime_get_ns();

	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->bytesused);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

	dev->isoc_ctl.buf = NULL;
}

static inline
void stk1160_copy_video(struct stk1160 *dev, u8 *src, int len)
{
	int linesdone, lineoff, lencopy;
	int bytesperline = dev->width * 2;
	struct stk1160_buffer *buf = dev->isoc_ctl.buf;
	u8 *dst = buf->mem;
	int remain;

	/*
	 * TODO: These stk1160_dbg are very spammy!
	 * We should 1) check why we are getting them
	 * and 2) add ratelimit.
	 *
	 * UPDATE: One of the reasons (the only one?) for getting these
	 * is incorrect standard (mismatch between expected and configured).
	 * So perhaps, we could add a counter for errors. When the counter
	 * reaches some value, we simply stop streaming.
	 */

	len -= 4;
	src += 4;

	remain = len;

	linesdone = buf->pos / bytesperline;
	lineoff = buf->pos % bytesperline; /* offset in current line */

	if (!buf->odd)
		dst += bytesperline;

	/* Multiply linesdone by two, to take account of the other field */
	dst += linesdone * bytesperline * 2 + lineoff;

	/* Copy the remaining of current line */
	if (remain < (bytesperline - lineoff))
		lencopy = remain;
	else
		lencopy = bytesperline - lineoff;

	/*
	 * Check if we have enough space left in the buffer.
	 * In that case, we force loop exit after copy.
	 */
	if (lencopy > buf->bytesused - buf->length) {
		lencopy = buf->bytesused - buf->length;
		remain = lencopy;
	}

	/* Check if the copy is done */
	if (lencopy == 0 || remain == 0)
		return;

	/* Let the bug hunt begin! sanity checks! */
	if (lencopy < 0) {
		stk1160_dbg("copy skipped: negative lencopy\n");
		return;
	}

	if ((unsigned long)dst + lencopy >
		(unsigned long)buf->mem + buf->length) {
		printk_ratelimited(KERN_WARNING "stk1160: buffer overflow detected\n");
		return;
	}

	memcpy(dst, src, lencopy);

	buf->bytesused += lencopy;
	buf->pos += lencopy;
	remain -= lencopy;

	/* Copy current field line by line, interlacing with the other field */
	while (remain > 0) {

		dst += lencopy + bytesperline;
		src += lencopy;

		/* Copy one line at a time */
		if (remain < bytesperline)
			lencopy = remain;
		else
			lencopy = bytesperline;

		/*
		 * Check if we have enough space left in the buffer.
		 * In that case, we force loop exit after copy.
		 */
		if (lencopy > buf->bytesused - buf->length) {
			lencopy = buf->bytesused - buf->length;
			remain = lencopy;
		}

		/* Check if the copy is done */
		if (lencopy == 0 || remain == 0)
			return;

		if (lencopy < 0) {
			printk_ratelimited(KERN_WARNING "stk1160: negative lencopy detected\n");
			return;
		}

		if ((unsigned long)dst + lencopy >
			(unsigned long)buf->mem + buf->length) {
			printk_ratelimited(KERN_WARNING "stk1160: buffer overflow detected\n");
			return;
		}

		memcpy(dst, src, lencopy);
		remain -= lencopy;

		buf->bytesused += lencopy;
		buf->pos += lencopy;
	}
}

/*
 * Controls the isoc copy of each urb packet
 */
static void stk1160_process_isoc(struct stk1160 *dev, struct urb *urb)
{
	int i, len, status;
	u8 *p;

	if (!dev) {
		stk1160_warn("%s called with null device\n", __func__);
		return;
	}

	if (urb->status < 0) {
		/* Print status and drop current packet (or field?) */
		print_err_status(dev, -1, urb->status);
		return;
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		status = urb->iso_frame_desc[i].status;
		if (status < 0) {
			print_err_status(dev, i, status);
			continue;
		}

		/* Get packet actual length and pointer to data */
		p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		len = urb->iso_frame_desc[i].actual_length;

		/* Empty packet */
		if (len <= 4)
			continue;

		/*
		 * An 8-byte packet sequence means end of field.
		 * So if we don't have any packet, we start receiving one now
		 * and if we do have a packet, then we are done with it.
		 *
		 * These end of field packets are always 0xc0 or 0x80,
		 * but not always 8-byte long so we don't check packet length.
		 */
		if (p[0] == 0xc0) {

			/*
			 * If first byte is 0xc0 then we received
			 * second field, and frame has ended.
			 */
			if (dev->isoc_ctl.buf != NULL)
				stk1160_buffer_done(dev);

			dev->isoc_ctl.buf = stk1160_next_buffer(dev);
			if (dev->isoc_ctl.buf == NULL)
				return;
		}

		/*
		 * If we don't have a buffer here, then it means we
		 * haven't found the start mark sequence.
		 */
		if (dev->isoc_ctl.buf == NULL)
			continue;

		if (p[0] == 0xc0 || p[0] == 0x80) {

			/* We set next packet parity and
			 * continue to get next one
			 */
			dev->isoc_ctl.buf->odd = *p & 0x40;
			dev->isoc_ctl.buf->pos = 0;
			continue;
		}

		stk1160_copy_video(dev, p, len);
	}
}


/*
 * IRQ callback, called by URB callback
 */
static void stk1160_isoc_irq(struct urb *urb)
{
	int i, rc;
	struct stk1160 *dev = urb->context;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:   /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		/* TODO: check uvc driver: he frees the queue here */
		return;
	default:
		stk1160_err("urb error! status %d\n", urb->status);
		return;
	}

	stk1160_process_isoc(dev, urb);

	/* Reset urb buffers */
	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc)
		stk1160_err("urb re-submit failed (%d)\n", rc);
}

/*
 * Cancel urbs
 * This function can't be called in atomic context
 */
void stk1160_cancel_isoc(struct stk1160 *dev)
{
	int i, num_bufs = dev->isoc_ctl.num_bufs;

	/*
	 * This check is not necessary, but we add it
	 * to avoid a spurious debug message
	 */
	if (!num_bufs)
		return;

	stk1160_dbg("killing %d urbs...\n", num_bufs);

	for (i = 0; i < num_bufs; i++) {

		/*
		 * To kill urbs we can't be in atomic context.
		 * We don't care for NULL pointer since
		 * usb_kill_urb allows it.
		 */
		usb_kill_urb(dev->isoc_ctl.urb[i]);
	}

	stk1160_dbg("all urbs killed\n");
}

/*
 * Releases urb and transfer buffers
 * Obviusly, associated urb must be killed before releasing it.
 */
void stk1160_free_isoc(struct stk1160 *dev)
{
	struct urb *urb;
	int i, num_bufs = dev->isoc_ctl.num_bufs;

	stk1160_dbg("freeing %d urb buffers...\n", num_bufs);

	for (i = 0; i < num_bufs; i++) {

		urb = dev->isoc_ctl.urb[i];
		if (urb) {

			if (dev->isoc_ctl.transfer_buffer[i]) {
#ifndef CONFIG_DMA_NONCOHERENT
				usb_free_coherent(dev->udev,
					urb->transfer_buffer_length,
					dev->isoc_ctl.transfer_buffer[i],
					urb->transfer_dma);
#else
				kfree(dev->isoc_ctl.transfer_buffer[i]);
#endif
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

	stk1160_dbg("all urb buffers freed\n");
}

/*
 * Helper for cancelling and freeing urbs
 * This function can't be called in atomic context
 */
void stk1160_uninit_isoc(struct stk1160 *dev)
{
	stk1160_cancel_isoc(dev);
	stk1160_free_isoc(dev);
}

/*
 * Allocate URBs
 */
int stk1160_alloc_isoc(struct stk1160 *dev)
{
	struct urb *urb;
	int i, j, k, sb_size, max_packets, num_bufs;

	/*
	 * It may be necessary to release isoc here,
	 * since isoc are only released on disconnection.
	 * (see new_pkt_size flag)
	 */
	if (dev->isoc_ctl.num_bufs)
		stk1160_uninit_isoc(dev);

	stk1160_dbg("allocating urbs...\n");

	num_bufs = STK1160_NUM_BUFS;
	max_packets = STK1160_NUM_PACKETS;
	sb_size = max_packets * dev->max_pkt_size;

	dev->isoc_ctl.buf = NULL;
	dev->isoc_ctl.max_pkt_size = dev->max_pkt_size;
	dev->isoc_ctl.urb = kcalloc(num_bufs, sizeof(void *), GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		stk1160_err("out of memory for urb array\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kcalloc(num_bufs, sizeof(void *),
						GFP_KERNEL);
	if (!dev->isoc_ctl.transfer_buffer) {
		stk1160_err("out of memory for usb transfers\n");
		kfree(dev->isoc_ctl.urb);
		return -ENOMEM;
	}

	/* allocate urbs and transfer buffers */
	for (i = 0; i < num_bufs; i++) {

		urb = usb_alloc_urb(max_packets, GFP_KERNEL);
		if (!urb)
			goto free_i_bufs;
		dev->isoc_ctl.urb[i] = urb;

#ifndef CONFIG_DMA_NONCOHERENT
		dev->isoc_ctl.transfer_buffer[i] = usb_alloc_coherent(dev->udev,
			sb_size, GFP_KERNEL, &urb->transfer_dma);
#else
		dev->isoc_ctl.transfer_buffer[i] = kmalloc(sb_size, GFP_KERNEL);
#endif
		if (!dev->isoc_ctl.transfer_buffer[i]) {
			stk1160_err("cannot alloc %d bytes for tx[%d] buffer\n",
				sb_size, i);

			/* Not enough transfer buffers, so just give up */
			if (i < STK1160_MIN_BUFS)
				goto free_i_bufs;
			goto nomore_tx_bufs;
		}
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

		/*
		 * FIXME: Where can I get the endpoint?
		 */
		urb->dev = dev->udev;
		urb->pipe = usb_rcvisocpipe(dev->udev, STK1160_EP_VIDEO);
		urb->transfer_buffer = dev->isoc_ctl.transfer_buffer[i];
		urb->transfer_buffer_length = sb_size;
		urb->complete = stk1160_isoc_irq;
		urb->context = dev;
		urb->interval = 1;
		urb->start_frame = 0;
		urb->number_of_packets = max_packets;
#ifndef CONFIG_DMA_NONCOHERENT
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
#else
		urb->transfer_flags = URB_ISO_ASAP;
#endif

		k = 0;
		for (j = 0; j < max_packets; j++) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length =
					dev->isoc_ctl.max_pkt_size;
			k += dev->isoc_ctl.max_pkt_size;
		}
	}

	stk1160_dbg("%d urbs allocated\n", num_bufs);

	/* At last we can say we have some buffers */
	dev->isoc_ctl.num_bufs = num_bufs;

	return 0;

nomore_tx_bufs:
	/*
	 * Failed to allocate desired buffer count. However, we may have
	 * enough to work fine, so we just free the extra urb,
	 * store the allocated count and keep going, fingers crossed!
	 */
	usb_free_urb(dev->isoc_ctl.urb[i]);
	dev->isoc_ctl.urb[i] = NULL;

	stk1160_warn("%d urbs allocated. Trying to continue...\n", i - 1);

	dev->isoc_ctl.num_bufs = i - 1;

	return 0;

free_i_bufs:
	/* Save the allocated buffers so far, so we can properly free them */
	dev->isoc_ctl.num_bufs = i+1;
	stk1160_free_isoc(dev);
	return -ENOMEM;
}

