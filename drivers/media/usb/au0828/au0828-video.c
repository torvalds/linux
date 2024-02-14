// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Auvitek AU0828 USB Bridge (Analog video support)
 *
 * Copyright (C) 2009 Devin Heitmueller <dheitmueller@linuxtv.org>
 * Copyright (C) 2005-2008 Auvitek International, Ltd.
 */

/* Developer Notes:
 *
 * The hardware scaler supported is unimplemented
 * AC97 audio support is unimplemented (only i2s audio mode)
 *
 */

#include "au0828.h"
#include "au8522.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/tuner.h>
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
		pr_info("au0828 %s :"fmt, \
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
		au0828_isocdbg("URB status %d [%s].\n",	status, errmsg);
	} else {
		au0828_isocdbg("URB packet %d, status %d [%s].\n",
			       packet, status, errmsg);
	}
}

static int check_dev(struct au0828_dev *dev)
{
	if (test_bit(DEV_DISCONNECTED, &dev->dev_state)) {
		pr_info("v4l2 ioctl: device not present\n");
		return -ENODEV;
	}

	if (test_bit(DEV_MISCONFIGURED, &dev->dev_state)) {
		pr_info("v4l2 ioctl: device is misconfigured; close and open it again\n");
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
		au0828_isocdbg("urb completion error %d.\n", urb->status);
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
	dev->stream_state = STREAM_ON;
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

	dev->stream_state = STREAM_OFF;
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

	dev->isoc_ctl.isoc_copy = isoc_copy;
	dev->isoc_ctl.num_bufs = num_bufs;

	dev->isoc_ctl.urb = kcalloc(num_bufs, sizeof(void *),  GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		au0828_isocdbg("cannot alloc memory for usb buffers\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kcalloc(num_bufs, sizeof(void *),
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
			au0828_isocdbg("cannot allocate URB\n");
			au0828_uninit_isoc(dev);
			return -ENOMEM;
		}
		dev->isoc_ctl.urb[i] = urb;

		dev->isoc_ctl.transfer_buffer[i] = usb_alloc_coherent(dev->usbdev,
			sb_size, GFP_KERNEL, &urb->transfer_dma);
		if (!dev->isoc_ctl.transfer_buffer[i]) {
			au0828_isocdbg("cannot allocate transfer buffer\n");
			au0828_uninit_isoc(dev);
			return -ENOMEM;
		}
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

		pipe = usb_rcvisocpipe(dev->usbdev,
				       dev->isoc_in_endpointaddr);

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
	struct vb2_v4l2_buffer *vb = &buf->vb;
	struct vb2_queue *q = vb->vb2_buf.vb2_queue;

	/* Advice that buffer was filled */
	au0828_isocdbg("[%p/%d] wakeup\n", buf, buf->top_field);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		vb->sequence = dev->frame_count++;
	else
		vb->sequence = dev->vbi_frame_count++;

	vb->field = V4L2_FIELD_INTERLACED;
	vb->vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&vb->vb2_buf, VB2_BUF_STATE_DONE);
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

	if (dma_q->pos + len > buf->length)
		len = buf->length - dma_q->pos;

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

	if ((char *)startwrite + lencopy > (char *)outp + buf->length) {
		au0828_isocdbg("Overflow of %zi bytes past buffer end (1)\n",
			       ((char *)startwrite + lencopy) -
			       ((char *)outp + buf->length));
		remain = (char *)outp + buf->length - (char *)startwrite;
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
		    buf->length) {
			au0828_isocdbg("Overflow %zi bytes past buf end (2)\n",
				       ((char *)startwrite + lencopy) -
				       ((char *)outp + buf->length));
			lencopy = remain = (char *)outp + buf->length -
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
 * generic routine to get the next available buffer
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
	*buf = list_entry(dma_q->active.next, struct au0828_buffer, list);
	/* Cleans up buffer - Useful for testing for frame/URB loss */
	list_del(&(*buf)->list);
	dma_q->pos = 0;
	(*buf)->vb_buf = (*buf)->mem;
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

	if (dma_q->pos + len > buf->length)
		len = buf->length - dma_q->pos;

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
 * generic routine to get the next available VBI buffer
 */
static inline void vbi_get_next_buf(struct au0828_dmaqueue *dma_q,
				    struct au0828_buffer **buf)
{
	struct au0828_dev *dev = container_of(dma_q, struct au0828_dev, vbiq);

	if (list_empty(&dma_q->active)) {
		au0828_isocdbg("No active queue to serve\n");
		dev->isoc_ctl.vbi_buf = NULL;
		*buf = NULL;
		return;
	}

	/* Get the next buffer */
	*buf = list_entry(dma_q->active.next, struct au0828_buffer, list);
	/* Cleans up buffer - Useful for testing for frame/URB loss */
	list_del(&(*buf)->list);
	dma_q->pos = 0;
	(*buf)->vb_buf = (*buf)->mem;
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

	if (test_bit(DEV_DISCONNECTED, &dev->dev_state) ||
	    test_bit(DEV_MISCONFIGURED, &dev->dev_state))
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	buf = dev->isoc_ctl.buf;
	if (buf != NULL)
		outp = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	vbi_buf = dev->isoc_ctl.vbi_buf;
	if (vbi_buf != NULL)
		vbioutp = vb2_plane_vaddr(&vbi_buf->vb.vb2_buf, 0);

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
					buffer_filled(dev, vbi_dma_q, vbi_buf);
				vbi_get_next_buf(vbi_dma_q, &vbi_buf);
				if (vbi_buf == NULL)
					vbioutp = NULL;
				else
					vbioutp = vb2_plane_vaddr(
						&vbi_buf->vb.vb2_buf, 0);

				/* Video */
				if (buf != NULL)
					buffer_filled(dev, dma_q, buf);
				get_next_buf(dma_q, &buf);
				if (buf == NULL)
					outp = NULL;
				else
					outp = vb2_plane_vaddr(
						&buf->vb.vb2_buf, 0);

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

void au0828_usb_v4l2_media_release(struct au0828_dev *dev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	int i;

	for (i = 0; i < AU0828_MAX_INPUT; i++) {
		if (AUVI_INPUT(i).type == AU0828_VMUX_UNDEFINED)
			return;
		media_device_unregister_entity(&dev->input_ent[i]);
	}
#endif
}

static void au0828_usb_v4l2_release(struct v4l2_device *v4l2_dev)
{
	struct au0828_dev *dev =
		container_of(v4l2_dev, struct au0828_dev, v4l2_dev);

	v4l2_ctrl_handler_free(&dev->v4l2_ctrl_hdl);
	v4l2_device_unregister(&dev->v4l2_dev);
	au0828_usb_v4l2_media_release(dev);
	au0828_usb_release(dev);
}

int au0828_v4l2_device_register(struct usb_interface *interface,
				struct au0828_dev *dev)
{
	int retval;

	if (AUVI_INPUT(0).type == AU0828_VMUX_UNDEFINED)
		return 0;

	/* Create the v4l2_device */
#ifdef CONFIG_MEDIA_CONTROLLER
	dev->v4l2_dev.mdev = dev->media_dev;
#endif
	retval = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
	if (retval) {
		pr_err("%s() v4l2_device_register failed\n",
		       __func__);
		return retval;
	}

	dev->v4l2_dev.release = au0828_usb_v4l2_release;

	/* This control handler will inherit the controls from au8522 */
	retval = v4l2_ctrl_handler_init(&dev->v4l2_ctrl_hdl, 4);
	if (retval) {
		pr_err("%s() v4l2_ctrl_handler_init failed\n",
		       __func__);
		return retval;
	}
	dev->v4l2_dev.ctrl_handler = &dev->v4l2_ctrl_hdl;

	return 0;
}

static int queue_setup(struct vb2_queue *vq,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct au0828_dev *dev = vb2_get_drv_priv(vq);
	unsigned long size = dev->height * dev->bytesperline;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = size;
	return 0;
}

static int
buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct au0828_buffer *buf = container_of(vbuf,
				struct au0828_buffer, vb);
	struct au0828_dev    *dev = vb2_get_drv_priv(vb->vb2_queue);

	buf->length = dev->height * dev->bytesperline;

	if (vb2_plane_size(vb, 0) < buf->length) {
		pr_err("%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), buf->length);
		return -EINVAL;
	}
	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->length);
	return 0;
}

static void
buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct au0828_buffer    *buf     = container_of(vbuf,
							struct au0828_buffer,
							vb);
	struct au0828_dev       *dev     = vb2_get_drv_priv(vb->vb2_queue);
	struct au0828_dmaqueue  *vidq    = &dev->vidq;
	unsigned long flags = 0;

	buf->mem = vb2_plane_vaddr(vb, 0);
	buf->length = vb2_plane_size(vb, 0);

	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int au0828_i2s_init(struct au0828_dev *dev)
{
	/* Enable i2s mode */
	au0828_writereg(dev, AU0828_AUDIOCTRL_50C, 0x01);
	return 0;
}

/*
 * Auvitek au0828 analog stream enable
 */
static int au0828_analog_stream_enable(struct au0828_dev *d)
{
	struct usb_interface *iface;
	int ret, h, w;

	dprintk(1, "au0828_analog_stream_enable called\n");

	if (test_bit(DEV_DISCONNECTED, &d->dev_state))
		return -ENODEV;

	iface = usb_ifnum_to_if(d->usbdev, 0);
	if (iface && iface->cur_altsetting->desc.bAlternateSetting != 5) {
		dprintk(1, "Changing intf#0 to alt 5\n");
		/* set au0828 interface0 to AS5 here again */
		ret = usb_set_interface(d->usbdev, 0, 5);
		if (ret < 0) {
			pr_info("Au0828 can't set alt setting to 5!\n");
			return -EBUSY;
		}
	}

	h = d->height / 2 + 2;
	w = d->width * 2;

	au0828_writereg(d, AU0828_SENSORCTRL_VBI_103, 0x00);
	au0828_writereg(d, 0x106, 0x00);
	/* set x position */
	au0828_writereg(d, 0x110, 0x00);
	au0828_writereg(d, 0x111, 0x00);
	au0828_writereg(d, 0x114, w & 0xff);
	au0828_writereg(d, 0x115, w >> 8);
	/* set y position */
	au0828_writereg(d, 0x112, 0x00);
	au0828_writereg(d, 0x113, 0x00);
	au0828_writereg(d, 0x116, h & 0xff);
	au0828_writereg(d, 0x117, h >> 8);
	au0828_writereg(d, AU0828_SENSORCTRL_100, 0xb3);

	return 0;
}

static int au0828_analog_stream_disable(struct au0828_dev *d)
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
	dev->stream_state = STREAM_INTERRUPT;
	if (test_bit(DEV_DISCONNECTED, &dev->dev_state))
		return -ENODEV;
	return 0;
}

int au0828_start_analog_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct au0828_dev *dev = vb2_get_drv_priv(vq);
	int rc = 0;

	dprintk(1, "au0828_start_analog_streaming called %d\n",
		dev->streaming_users);

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		dev->frame_count = 0;
	else
		dev->vbi_frame_count = 0;

	if (dev->streaming_users == 0) {
		/* If we were doing ac97 instead of i2s, it would go here...*/
		au0828_i2s_init(dev);
		rc = au0828_init_isoc(dev, AU0828_ISO_PACKETS_PER_URB,
				   AU0828_MAX_ISO_BUFS, dev->max_pkt_size,
				   au0828_isoc_copy);
		if (rc < 0) {
			pr_info("au0828_init_isoc failed\n");
			return rc;
		}

		v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 1);

		if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			dev->vid_timeout_running = 1;
			mod_timer(&dev->vid_timeout, jiffies + (HZ / 10));
		} else if (vq->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
			dev->vbi_timeout_running = 1;
			mod_timer(&dev->vbi_timeout, jiffies + (HZ / 10));
		}
	}
	dev->streaming_users++;
	return rc;
}

static void au0828_stop_streaming(struct vb2_queue *vq)
{
	struct au0828_dev *dev = vb2_get_drv_priv(vq);
	struct au0828_dmaqueue *vidq = &dev->vidq;
	unsigned long flags = 0;

	dprintk(1, "au0828_stop_streaming called %d\n", dev->streaming_users);

	if (dev->streaming_users-- == 1) {
		au0828_uninit_isoc(dev);
		v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 0);
	}

	dev->vid_timeout_running = 0;
	del_timer_sync(&dev->vid_timeout);

	spin_lock_irqsave(&dev->slock, flags);
	if (dev->isoc_ctl.buf != NULL) {
		vb2_buffer_done(&dev->isoc_ctl.buf->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		dev->isoc_ctl.buf = NULL;
	}
	while (!list_empty(&vidq->active)) {
		struct au0828_buffer *buf;

		buf = list_entry(vidq->active.next, struct au0828_buffer, list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}

void au0828_stop_vbi_streaming(struct vb2_queue *vq)
{
	struct au0828_dev *dev = vb2_get_drv_priv(vq);
	struct au0828_dmaqueue *vbiq = &dev->vbiq;
	unsigned long flags = 0;

	dprintk(1, "au0828_stop_vbi_streaming called %d\n",
		dev->streaming_users);

	if (dev->streaming_users-- == 1) {
		au0828_uninit_isoc(dev);
		v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 0);
	}

	spin_lock_irqsave(&dev->slock, flags);
	if (dev->isoc_ctl.vbi_buf != NULL) {
		vb2_buffer_done(&dev->isoc_ctl.vbi_buf->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		dev->isoc_ctl.vbi_buf = NULL;
	}
	while (!list_empty(&vbiq->active)) {
		struct au0828_buffer *buf;

		buf = list_entry(vbiq->active.next, struct au0828_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	dev->vbi_timeout_running = 0;
	del_timer_sync(&dev->vbi_timeout);
}

static const struct vb2_ops au0828_video_qops = {
	.queue_setup     = queue_setup,
	.buf_prepare     = buffer_prepare,
	.buf_queue       = buffer_queue,
	.start_streaming = au0828_start_analog_streaming,
	.stop_streaming  = au0828_stop_streaming,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
};

/* ------------------------------------------------------------------
   V4L2 interface
   ------------------------------------------------------------------*/
/*
 * au0828_analog_unregister
 * unregister v4l2 devices
 */
int au0828_analog_unregister(struct au0828_dev *dev)
{
	dprintk(1, "au0828_analog_unregister called\n");

	/* No analog TV */
	if (AUVI_INPUT(0).type == AU0828_VMUX_UNDEFINED)
		return 0;

	mutex_lock(&au0828_sysfs_lock);
	vb2_video_unregister_device(&dev->vdev);
	vb2_video_unregister_device(&dev->vbi_dev);
	mutex_unlock(&au0828_sysfs_lock);

	v4l2_device_disconnect(&dev->v4l2_dev);
	v4l2_device_put(&dev->v4l2_dev);

	return 1;
}

/* This function ensures that video frames continue to be delivered even if
   the ITU-656 input isn't receiving any data (thereby preventing applications
   such as tvtime from hanging) */
static void au0828_vid_buffer_timeout(struct timer_list *t)
{
	struct au0828_dev *dev = from_timer(dev, t, vid_timeout);
	struct au0828_dmaqueue *dma_q = &dev->vidq;
	struct au0828_buffer *buf;
	unsigned char *vid_data;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);

	buf = dev->isoc_ctl.buf;
	if (buf != NULL) {
		vid_data = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
		memset(vid_data, 0x00, buf->length); /* Blank green frame */
		buffer_filled(dev, dma_q, buf);
	}
	get_next_buf(dma_q, &buf);

	if (dev->vid_timeout_running == 1)
		mod_timer(&dev->vid_timeout, jiffies + (HZ / 10));

	spin_unlock_irqrestore(&dev->slock, flags);
}

static void au0828_vbi_buffer_timeout(struct timer_list *t)
{
	struct au0828_dev *dev = from_timer(dev, t, vbi_timeout);
	struct au0828_dmaqueue *dma_q = &dev->vbiq;
	struct au0828_buffer *buf;
	unsigned char *vbi_data;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);

	buf = dev->isoc_ctl.vbi_buf;
	if (buf != NULL) {
		vbi_data = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
		memset(vbi_data, 0x00, buf->length);
		buffer_filled(dev, dma_q, buf);
	}
	vbi_get_next_buf(dma_q, &buf);

	if (dev->vbi_timeout_running == 1)
		mod_timer(&dev->vbi_timeout, jiffies + (HZ / 10));
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int au0828_v4l2_open(struct file *filp)
{
	struct au0828_dev *dev = video_drvdata(filp);
	int ret;

	dprintk(1,
		"%s called std_set %d dev_state %ld stream users %d users %d\n",
		__func__, dev->std_set_in_tuner_core, dev->dev_state,
		dev->streaming_users, dev->users);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(filp);
	if (ret) {
		au0828_isocdbg("%s: v4l2_fh_open() returned error %d\n",
				__func__, ret);
		mutex_unlock(&dev->lock);
		return ret;
	}

	if (dev->users == 0) {
		au0828_analog_stream_enable(dev);
		au0828_analog_stream_reset(dev);
		dev->stream_state = STREAM_OFF;
		set_bit(DEV_INITIALIZED, &dev->dev_state);
	}
	dev->users++;
	mutex_unlock(&dev->lock);
	return ret;
}

static int au0828_v4l2_close(struct file *filp)
{
	int ret;
	struct au0828_dev *dev = video_drvdata(filp);
	struct video_device *vdev = video_devdata(filp);

	dprintk(1,
		"%s called std_set %d dev_state %ld stream users %d users %d\n",
		__func__, dev->std_set_in_tuner_core, dev->dev_state,
		dev->streaming_users, dev->users);

	mutex_lock(&dev->lock);
	if (vdev->vfl_type == VFL_TYPE_VIDEO && dev->vid_timeout_running) {
		/* Cancel timeout thread in case they didn't call streamoff */
		dev->vid_timeout_running = 0;
		del_timer_sync(&dev->vid_timeout);
	} else if (vdev->vfl_type == VFL_TYPE_VBI &&
			dev->vbi_timeout_running) {
		/* Cancel timeout thread in case they didn't call streamoff */
		dev->vbi_timeout_running = 0;
		del_timer_sync(&dev->vbi_timeout);
	}

	if (test_bit(DEV_DISCONNECTED, &dev->dev_state))
		goto end;

	if (dev->users == 1) {
		/*
		 * Avoid putting tuner in sleep if DVB or ALSA are
		 * streaming.
		 *
		 * On most USB devices  like au0828 the tuner can
		 * be safely put in sleep state here if ALSA isn't
		 * streaming. Exceptions are some very old USB tuner
		 * models such as em28xx-based WinTV USB2 which have
		 * a separate audio output jack. The devices that have
		 * a separate audio output jack have analog tuners,
		 * like Philips FM1236. Those devices are always on,
		 * so the s_power callback are silently ignored.
		 * So, the current logic here does the following:
		 * Disable (put tuner to sleep) when
		 * - ALSA and DVB aren't streaming.
		 * - the last V4L2 file handler is closed.
		 *
		 * FIXME:
		 *
		 * Additionally, this logic could be improved to
		 * disable the media source if the above conditions
		 * are met and if the device:
		 * - doesn't have a separate audio out plug (or
		 * - doesn't use a silicon tuner like xc2028/3028/4000/5000).
		 *
		 * Once this additional logic is in place, a callback
		 * is needed to enable the media source and power on
		 * the tuner, for radio to work.
		*/
		ret = v4l_enable_media_source(vdev);
		if (ret == 0)
			v4l2_device_call_all(&dev->v4l2_dev, 0, tuner,
					     standby);
		dev->std_set_in_tuner_core = 0;

		/* When close the device, set the usb intf0 into alt0 to free
		   USB bandwidth */
		ret = usb_set_interface(dev->usbdev, 0, 0);
		if (ret < 0)
			pr_info("Au0828 can't set alternate to 0!\n");
	}
end:
	_vb2_fop_release(filp, NULL);
	dev->users--;
	mutex_unlock(&dev->lock);
	return 0;
}

/* Must be called with dev->lock held */
static void au0828_init_tuner(struct au0828_dev *dev)
{
	struct v4l2_frequency f = {
		.frequency = dev->ctrl_freq,
		.type = V4L2_TUNER_ANALOG_TV,
	};

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	if (dev->std_set_in_tuner_core)
		return;
	dev->std_set_in_tuner_core = 1;
	i2c_gate_ctrl(dev, 1);
	/* If we've never sent the standard in tuner core, do so now.
	   We don't do this at device probe because we don't want to
	   incur the cost of a firmware load */
	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_std, dev->std);
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);
	i2c_gate_ctrl(dev, 0);
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

	au0828_analog_stream_enable(dev);

	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	strscpy(cap->driver, "au0828", sizeof(cap->driver));
	strscpy(cap->card, dev->board.name, sizeof(cap->card));
	usb_make_path(dev->usbdev, cap->bus_info, sizeof(cap->bus_info));

	/* set the device capabilities */
	cap->capabilities =
		V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
		V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE | V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	dprintk(1, "%s called\n", __func__);

	f->pixelformat = V4L2_PIX_FMT_UYVY;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	f->fmt.pix.bytesperline = dev->bytesperline;
	f->fmt.pix.sizeimage = dev->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M; /* NTSC/PAL */
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	return au0828_set_format(dev, VIDIOC_TRY_FMT, f);
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct au0828_dev *dev = video_drvdata(file);
	int rc;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	rc = check_dev(dev);
	if (rc < 0)
		return rc;

	if (vb2_is_busy(&dev->vb_vidq)) {
		pr_info("%s queue busy\n", __func__);
		rc = -EBUSY;
		goto out;
	}

	rc = au0828_set_format(dev, VIDIOC_S_FMT, f);
out:
	return rc;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id norm)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	if (norm == dev->std)
		return 0;

	if (dev->streaming_users > 0)
		return -EBUSY;

	dev->std = norm;

	au0828_init_tuner(dev);

	i2c_gate_ctrl(dev, 1);

	/*
	 * FIXME: when we support something other than 60Hz standards,
	 * we are going to have to make the au0828 bridge adjust the size
	 * of its capture buffer, which is currently hardcoded at 720x480
	 */

	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_std, norm);

	i2c_gate_ctrl(dev, 0);

	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	*norm = dev->std;
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *input)
{
	struct au0828_dev *dev = video_drvdata(file);
	unsigned int tmp;

	static const char *inames[] = {
		[AU0828_VMUX_UNDEFINED] = "Undefined",
		[AU0828_VMUX_COMPOSITE] = "Composite",
		[AU0828_VMUX_SVIDEO] = "S-Video",
		[AU0828_VMUX_CABLE] = "Cable TV",
		[AU0828_VMUX_TELEVISION] = "Television",
		[AU0828_VMUX_DVB] = "DVB",
	};

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	tmp = input->index;

	if (tmp >= AU0828_MAX_INPUT)
		return -EINVAL;
	if (AUVI_INPUT(tmp).type == 0)
		return -EINVAL;

	input->index = tmp;
	strscpy(input->name, inames[AUVI_INPUT(tmp).type], sizeof(input->name));
	if ((AUVI_INPUT(tmp).type == AU0828_VMUX_TELEVISION) ||
	    (AUVI_INPUT(tmp).type == AU0828_VMUX_CABLE)) {
		input->type |= V4L2_INPUT_TYPE_TUNER;
		input->audioset = 1;
	} else {
		input->type |= V4L2_INPUT_TYPE_CAMERA;
		input->audioset = 2;
	}

	input->std = dev->vdev.tvnorms;

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	*i = dev->ctrl_input;
	return 0;
}

static void au0828_s_input(struct au0828_dev *dev, int index)
{
	int i;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

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
		return;
	}

	dev->ctrl_input = index;

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
	struct au0828_dev *dev = video_drvdata(file);
	struct video_device *vfd = video_devdata(file);

	dprintk(1, "VIDIOC_S_INPUT in function %s, input=%d\n", __func__,
		index);
	if (index >= AU0828_MAX_INPUT)
		return -EINVAL;
	if (AUVI_INPUT(index).type == 0)
		return -EINVAL;

	if (dev->ctrl_input == index)
		return 0;

	au0828_s_input(dev, index);

	/*
	 * Input has been changed. Disable the media source
	 * associated with the old input and enable source
	 * for the newly set input
	 */
	v4l_disable_media_source(vfd);
	return v4l_enable_media_source(vfd);
}

static int vidioc_enumaudio(struct file *file, void *priv, struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	dprintk(1, "%s called\n", __func__);

	if (a->index == 0)
		strscpy(a->name, "Television", sizeof(a->name));
	else
		strscpy(a->name, "Line in", sizeof(a->name));

	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	a->index = dev->ctrl_ainput;
	if (a->index == 0)
		strscpy(a->name, "Television", sizeof(a->name));
	else
		strscpy(a->name, "Line in", sizeof(a->name));

	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv, const struct v4l2_audio *a)
{
	struct au0828_dev *dev = video_drvdata(file);

	if (a->index != dev->ctrl_ainput)
		return -EINVAL;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct au0828_dev *dev = video_drvdata(file);
	struct video_device *vfd = video_devdata(file);
	int ret;

	if (t->index != 0)
		return -EINVAL;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	strscpy(t->name, "Auvitek tuner", sizeof(t->name));

	au0828_init_tuner(dev);
	i2c_gate_ctrl(dev, 1);
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, g_tuner, t);
	i2c_gate_ctrl(dev, 0);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *t)
{
	struct au0828_dev *dev = video_drvdata(file);

	if (t->index != 0)
		return -EINVAL;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

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
	struct au0828_dev *dev = video_drvdata(file);

	if (freq->tuner != 0)
		return -EINVAL;
	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);
	freq->frequency = dev->ctrl_freq;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *freq)
{
	struct au0828_dev *dev = video_drvdata(file);
	struct v4l2_frequency new_freq = *freq;

	if (freq->tuner != 0)
		return -EINVAL;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

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
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

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

static int vidioc_g_pixelaspect(struct file *file, void *priv,
				int type, struct v4l2_fract *f)
{
	struct au0828_dev *dev = video_drvdata(file);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	f->numerator = 54;
	f->denominator = 59;

	return 0;
}

static int vidioc_g_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct au0828_dev *dev = video_drvdata(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = dev->width;
		s->r.height = dev->height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *priv,
			     struct v4l2_dbg_register *reg)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	reg->val = au0828_read(dev, reg->reg);
	reg->size = 1;
	return 0;
}

static int vidioc_s_register(struct file *file, void *priv,
			     const struct v4l2_dbg_register *reg)
{
	struct au0828_dev *dev = video_drvdata(file);

	dprintk(1, "%s called std_set %d dev_state %ld\n", __func__,
		dev->std_set_in_tuner_core, dev->dev_state);

	return au0828_writereg(dev, reg->reg, reg->val);
}
#endif

static int vidioc_log_status(struct file *file, void *fh)
{
	struct video_device *vdev = video_devdata(file);

	dprintk(1, "%s called\n", __func__);

	v4l2_ctrl_log_status(file, fh);
	v4l2_device_call_all(vdev->v4l2_dev, 0, core, log_status);
	return 0;
}

void au0828_v4l2_suspend(struct au0828_dev *dev)
{
	struct urb *urb;
	int i;

	pr_info("stopping V4L2\n");

	if (dev->stream_state == STREAM_ON) {
		pr_info("stopping V4L2 active URBs\n");
		au0828_analog_stream_disable(dev);
		/* stop urbs */
		for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
			urb = dev->isoc_ctl.urb[i];
			if (urb) {
				if (!irqs_disabled())
					usb_kill_urb(urb);
				else
					usb_unlink_urb(urb);
			}
		}
	}

	if (dev->vid_timeout_running)
		del_timer_sync(&dev->vid_timeout);
	if (dev->vbi_timeout_running)
		del_timer_sync(&dev->vbi_timeout);
}

void au0828_v4l2_resume(struct au0828_dev *dev)
{
	int i, rc;

	pr_info("restarting V4L2\n");

	if (dev->stream_state == STREAM_ON) {
		au0828_stream_interrupt(dev);
		au0828_init_tuner(dev);
	}

	if (dev->vid_timeout_running)
		mod_timer(&dev->vid_timeout, jiffies + (HZ / 10));
	if (dev->vbi_timeout_running)
		mod_timer(&dev->vbi_timeout, jiffies + (HZ / 10));

	/* If we were doing ac97 instead of i2s, it would go here...*/
	au0828_i2s_init(dev);

	au0828_analog_stream_enable(dev);

	if (!(dev->stream_state == STREAM_ON)) {
		au0828_analog_stream_reset(dev);
		/* submit urbs */
		for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
			rc = usb_submit_urb(dev->isoc_ctl.urb[i], GFP_ATOMIC);
			if (rc) {
				au0828_isocdbg("submit of urb %i failed (error=%i)\n",
					       i, rc);
				au0828_uninit_isoc(dev);
			}
		}
	}
}

static const struct v4l2_file_operations au0828_v4l_fops = {
	.owner      = THIS_MODULE,
	.open       = au0828_v4l2_open,
	.release    = au0828_v4l2_close,
	.read       = vb2_fop_read,
	.poll       = vb2_fop_poll,
	.mmap       = vb2_fop_mmap,
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
	.vidioc_g_pixelaspect       = vidioc_g_pixelaspect,
	.vidioc_g_selection         = vidioc_g_selection,

	.vidioc_reqbufs             = vb2_ioctl_reqbufs,
	.vidioc_create_bufs         = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf         = vb2_ioctl_prepare_buf,
	.vidioc_querybuf            = vb2_ioctl_querybuf,
	.vidioc_qbuf                = vb2_ioctl_qbuf,
	.vidioc_dqbuf               = vb2_ioctl_dqbuf,
	.vidioc_expbuf               = vb2_ioctl_expbuf,

	.vidioc_s_std               = vidioc_s_std,
	.vidioc_g_std               = vidioc_g_std,
	.vidioc_enum_input          = vidioc_enum_input,
	.vidioc_g_input             = vidioc_g_input,
	.vidioc_s_input             = vidioc_s_input,

	.vidioc_streamon            = vb2_ioctl_streamon,
	.vidioc_streamoff           = vb2_ioctl_streamoff,

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
	.release                    = video_device_release_empty,
	.ioctl_ops		    = &video_ioctl_ops,
	.tvnorms                    = V4L2_STD_NTSC_M | V4L2_STD_PAL_M,
};

static int au0828_vb2_setup(struct au0828_dev *dev)
{
	int rc;
	struct vb2_queue *q;

	/* Setup Videobuf2 for Video capture */
	q = &dev->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_READ | VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct au0828_buffer);
	q->ops = &au0828_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;

	rc = vb2_queue_init(q);
	if (rc < 0)
		return rc;

	/* Setup Videobuf2 for VBI capture */
	q = &dev->vb_vbiq;
	q->type = V4L2_BUF_TYPE_VBI_CAPTURE;
	q->io_modes = VB2_READ | VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct au0828_buffer);
	q->ops = &au0828_vbi_qops;
	q->mem_ops = &vb2_vmalloc_memops;

	rc = vb2_queue_init(q);
	if (rc < 0)
		return rc;

	return 0;
}

static void au0828_analog_create_entities(struct au0828_dev *dev)
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	static const char * const inames[] = {
		[AU0828_VMUX_COMPOSITE] = "Composite",
		[AU0828_VMUX_SVIDEO] = "S-Video",
		[AU0828_VMUX_CABLE] = "Cable TV",
		[AU0828_VMUX_TELEVISION] = "Television",
		[AU0828_VMUX_DVB] = "DVB",
	};
	int ret, i;

	/* Initialize Video and VBI pads */
	dev->video_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&dev->vdev.entity, 1, &dev->video_pad);
	if (ret < 0)
		pr_err("failed to initialize video media entity!\n");

	dev->vbi_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&dev->vbi_dev.entity, 1, &dev->vbi_pad);
	if (ret < 0)
		pr_err("failed to initialize vbi media entity!\n");

	/* Create entities for each input connector */
	for (i = 0; i < AU0828_MAX_INPUT; i++) {
		struct media_entity *ent = &dev->input_ent[i];

		if (AUVI_INPUT(i).type == AU0828_VMUX_UNDEFINED)
			break;

		ent->name = inames[AUVI_INPUT(i).type];
		ent->flags = MEDIA_ENT_FL_CONNECTOR;
		dev->input_pad[i].flags = MEDIA_PAD_FL_SOURCE;

		switch (AUVI_INPUT(i).type) {
		case AU0828_VMUX_COMPOSITE:
			ent->function = MEDIA_ENT_F_CONN_COMPOSITE;
			break;
		case AU0828_VMUX_SVIDEO:
			ent->function = MEDIA_ENT_F_CONN_SVIDEO;
			break;
		case AU0828_VMUX_CABLE:
		case AU0828_VMUX_TELEVISION:
		case AU0828_VMUX_DVB:
		default: /* Just to shut up a warning */
			ent->function = MEDIA_ENT_F_CONN_RF;
			break;
		}

		ret = media_entity_pads_init(ent, 1, &dev->input_pad[i]);
		if (ret < 0)
			pr_err("failed to initialize input pad[%d]!\n", i);

		ret = media_device_register_entity(dev->media_dev, ent);
		if (ret < 0)
			pr_err("failed to register input entity %d!\n", i);
	}
#endif
}

/**************************************************************************/

int au0828_analog_register(struct au0828_dev *dev,
			   struct usb_interface *interface)
{
	int retval = -ENOMEM;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, ret;

	dprintk(1, "au0828_analog_register called for intf#%d!\n",
		interface->cur_altsetting->desc.bInterfaceNumber);

	/* No analog TV */
	if (AUVI_INPUT(0).type == AU0828_VMUX_UNDEFINED)
		return 0;

	/* set au0828 usb interface0 to as5 */
	retval = usb_set_interface(dev->usbdev,
			interface->cur_altsetting->desc.bInterfaceNumber, 5);
	if (retval != 0) {
		pr_info("Failure setting usb interface0 to as5\n");
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
			dprintk(1,
				"Found isoc endpoint 0x%02x, max size = %d\n",
				dev->isoc_in_endpointaddr, dev->max_pkt_size);
		}
	}
	if (!(dev->isoc_in_endpointaddr)) {
		pr_info("Could not locate isoc endpoint\n");
		return -ENODEV;
	}

	init_waitqueue_head(&dev->open);
	spin_lock_init(&dev->slock);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vbiq.active);

	timer_setup(&dev->vid_timeout, au0828_vid_buffer_timeout, 0);
	timer_setup(&dev->vbi_timeout, au0828_vbi_buffer_timeout, 0);

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
	/* Default input is TV Tuner */
	au0828_s_input(dev, 0);

	mutex_init(&dev->vb_queue_lock);
	mutex_init(&dev->vb_vbi_queue_lock);

	/* Fill the video capture device struct */
	dev->vdev = au0828_video_template;
	dev->vdev.v4l2_dev = &dev->v4l2_dev;
	dev->vdev.lock = &dev->lock;
	dev->vdev.queue = &dev->vb_vidq;
	dev->vdev.queue->lock = &dev->vb_queue_lock;
	dev->vdev.device_caps =
		V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
		V4L2_CAP_TUNER | V4L2_CAP_VIDEO_CAPTURE;
	strscpy(dev->vdev.name, "au0828a video", sizeof(dev->vdev.name));

	/* Setup the VBI device */
	dev->vbi_dev = au0828_video_template;
	dev->vbi_dev.v4l2_dev = &dev->v4l2_dev;
	dev->vbi_dev.lock = &dev->lock;
	dev->vbi_dev.queue = &dev->vb_vbiq;
	dev->vbi_dev.queue->lock = &dev->vb_vbi_queue_lock;
	dev->vbi_dev.device_caps =
		V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
		V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE;
	strscpy(dev->vbi_dev.name, "au0828a vbi", sizeof(dev->vbi_dev.name));

	/* Init entities at the Media Controller */
	au0828_analog_create_entities(dev);

	/* initialize videobuf2 stuff */
	retval = au0828_vb2_setup(dev);
	if (retval != 0) {
		dprintk(1, "unable to setup videobuf2 queues (error = %d).\n",
			retval);
		return -ENODEV;
	}

	/* Register the v4l2 device */
	video_set_drvdata(&dev->vdev, dev);
	retval = video_register_device(&dev->vdev, VFL_TYPE_VIDEO, -1);
	if (retval != 0) {
		dprintk(1, "unable to register video device (error = %d).\n",
			retval);
		return -ENODEV;
	}

	/* Register the vbi device */
	video_set_drvdata(&dev->vbi_dev, dev);
	retval = video_register_device(&dev->vbi_dev, VFL_TYPE_VBI, -1);
	if (retval != 0) {
		dprintk(1, "unable to register vbi device (error = %d).\n",
			retval);
		ret = -ENODEV;
		goto err_reg_vbi_dev;
	}

#ifdef CONFIG_MEDIA_CONTROLLER
	retval = v4l2_mc_create_media_graph(dev->media_dev);
	if (retval) {
		pr_err("%s() au0282_dev_register failed to create graph\n",
			__func__);
		ret = -ENODEV;
		goto err_reg_vbi_dev;
	}
#endif

	dprintk(1, "%s completed!\n", __func__);

	return 0;

err_reg_vbi_dev:
	vb2_video_unregister_device(&dev->vdev);
	return ret;
}

