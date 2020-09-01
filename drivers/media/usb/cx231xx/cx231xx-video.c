// SPDX-License-Identifier: GPL-2.0-or-later
/*
   cx231xx-video.c - driver for Conexant Cx23100/101/102
		     USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
	Based on em28xx driver
	Based on cx23885 driver
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
#include <media/v4l2-event.h>
#include <media/drv-intf/msp3400.h>
#include <media/tuner.h>

#include <media/dvb_frontend.h>

#include "cx231xx-vbi.h"

#define CX231XX_VERSION "0.0.3"

#define DRIVER_AUTHOR   "Srinivasa Deevi <srinivasa.deevi@conexant.com>"
#define DRIVER_DESC     "Conexant cx231xx based USB video device driver"

#define cx231xx_videodbg(fmt, arg...) do {\
	if (video_debug) \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); } while (0)

static unsigned int isoc_debug;
module_param(isoc_debug, int, 0644);
MODULE_PARM_DESC(isoc_debug, "enable debug messages [isoc transfers]");

#define cx231xx_isocdbg(fmt, arg...) \
do {\
	if (isoc_debug) { \
		printk(KERN_INFO "%s %s :"fmt, \
			 dev->name, __func__ , ##arg); \
	} \
  } while (0)

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(CX231XX_VERSION);

static unsigned int card[]     = {[0 ... (CX231XX_MAXBOARDS - 1)] = -1U };
static unsigned int video_nr[] = {[0 ... (CX231XX_MAXBOARDS - 1)] = -1U };
static unsigned int vbi_nr[]   = {[0 ... (CX231XX_MAXBOARDS - 1)] = -1U };
static unsigned int radio_nr[] = {[0 ... (CX231XX_MAXBOARDS - 1)] = -1U };

module_param_array(card, int, NULL, 0444);
module_param_array(video_nr, int, NULL, 0444);
module_param_array(vbi_nr, int, NULL, 0444);
module_param_array(radio_nr, int, NULL, 0444);

MODULE_PARM_DESC(card, "card type");
MODULE_PARM_DESC(video_nr, "video device numbers");
MODULE_PARM_DESC(vbi_nr, "vbi device numbers");
MODULE_PARM_DESC(radio_nr, "radio device numbers");

static unsigned int video_debug;
module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

/* supported video standards */
static struct cx231xx_fmt format[] = {
	{
	 .fourcc = V4L2_PIX_FMT_YUYV,
	 .depth = 16,
	 .reg = 0,
	 },
};


static int cx231xx_enable_analog_tuner(struct cx231xx *dev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device *mdev = dev->media_dev;
	struct media_entity  *entity, *decoder = NULL, *source;
	struct media_link *link, *found_link = NULL;
	int ret, active_links = 0;

	if (!mdev)
		return 0;

	/*
	 * This will find the tuner that is connected into the decoder.
	 * Technically, this is not 100% correct, as the device may be
	 * using an analog input instead of the tuner. However, as we can't
	 * do DVB streaming while the DMA engine is being used for V4L2,
	 * this should be enough for the actual needs.
	 */
	media_device_for_each_entity(entity, mdev) {
		if (entity->function == MEDIA_ENT_F_ATV_DECODER) {
			decoder = entity;
			break;
		}
	}
	if (!decoder)
		return 0;

	list_for_each_entry(link, &decoder->links, list) {
		if (link->sink->entity == decoder) {
			found_link = link;
			if (link->flags & MEDIA_LNK_FL_ENABLED)
				active_links++;
			break;
		}
	}

	if (active_links == 1 || !found_link)
		return 0;

	source = found_link->source->entity;
	list_for_each_entry(link, &source->links, list) {
		struct media_entity *sink;
		int flags = 0;

		sink = link->sink->entity;

		if (sink == entity)
			flags = MEDIA_LNK_FL_ENABLED;

		ret = media_entity_setup_link(link, flags);
		if (ret) {
			dev_err(dev->dev,
				"Couldn't change link %s->%s to %s. Error %d\n",
				source->name, sink->name,
				flags ? "enabled" : "disabled",
				ret);
			return ret;
		} else
			dev_dbg(dev->dev,
				"link %s->%s was %s\n",
				source->name, sink->name,
				flags ? "ENABLED" : "disabled");
	}
#endif
	return 0;
}

/* ------------------------------------------------------------------
	Video buffer and parser functions
   ------------------------------------------------------------------*/

/*
 * Announces that a buffer were filled and request the next
 */
static inline void buffer_filled(struct cx231xx *dev,
				 struct cx231xx_dmaqueue *dma_q,
				 struct cx231xx_buffer *buf)
{
	/* Advice that buffer was filled */
	cx231xx_isocdbg("[%p/%d] wakeup\n", buf, buf->vb.vb2_buf.index);
	buf->vb.sequence = dma_q->sequence++;
	buf->vb.field = V4L2_FIELD_INTERLACED;
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, dev->size);

	if (dev->USE_ISO)
		dev->video_mode.isoc_ctl.buf = NULL;
	else
		dev->video_mode.bulk_ctl.buf = NULL;

	list_del(&buf->list);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

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
		cx231xx_isocdbg("URB status %d [%s].\n", status, errmsg);
	} else {
		cx231xx_isocdbg("URB packet %d, status %d [%s].\n",
				packet, status, errmsg);
	}
}

/*
 * video-buf generic routine to get the next available buffer
 */
static inline void get_next_buf(struct cx231xx_dmaqueue *dma_q,
				struct cx231xx_buffer **buf)
{
	struct cx231xx_video_mode *vmode =
	    container_of(dma_q, struct cx231xx_video_mode, vidq);
	struct cx231xx *dev = container_of(vmode, struct cx231xx, video_mode);

	char *outp;

	if (list_empty(&dma_q->active)) {
		cx231xx_isocdbg("No active queue to serve\n");
		if (dev->USE_ISO)
			dev->video_mode.isoc_ctl.buf = NULL;
		else
			dev->video_mode.bulk_ctl.buf = NULL;
		*buf = NULL;
		return;
	}

	/* Get the next buffer */
	*buf = list_entry(dma_q->active.next, struct cx231xx_buffer, list);

	/* Cleans up buffer - Useful for testing for frame/URB loss */
	outp = vb2_plane_vaddr(&(*buf)->vb.vb2_buf, 0);
	memset(outp, 0, dev->size);

	if (dev->USE_ISO)
		dev->video_mode.isoc_ctl.buf = *buf;
	else
		dev->video_mode.bulk_ctl.buf = *buf;

	return;
}

/*
 * Controls the isoc copy of each urb packet
 */
static inline int cx231xx_isoc_copy(struct cx231xx *dev, struct urb *urb)
{
	struct cx231xx_dmaqueue *dma_q = urb->context;
	int i;
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

	for (i = 0; i < urb->number_of_packets; i++) {
		int status = urb->iso_frame_desc[i].status;

		if (status < 0) {
			print_err_status(dev, i, status);
			if (urb->iso_frame_desc[i].status != -EPROTO)
				continue;
		}

		if (urb->iso_frame_desc[i].actual_length <= 0) {
			/* cx231xx_isocdbg("packet %d is empty",i); - spammy */
			continue;
		}
		if (urb->iso_frame_desc[i].actual_length >
		    dev->video_mode.max_pkt_size) {
			cx231xx_isocdbg("packet bigger than packet size");
			continue;
		}

		/*  get buffer pointer and length */
		p_buffer = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		buffer_size = urb->iso_frame_desc[i].actual_length;
		bytes_parsed = 0;

		if (dma_q->is_partial_line) {
			/* Handle the case of a partial line */
			sav_eav = dma_q->last_sav;
		} else {
			/* Check for a SAV/EAV overlapping
				the buffer boundary */
			sav_eav =
			    cx231xx_find_boundary_SAV_EAV(p_buffer,
							  dma_q->partial_buf,
							  &bytes_parsed);
		}

		sav_eav &= 0xF0;
		/* Get the first line if we have some portion of an SAV/EAV from
		   the last buffer or a partial line  */
		if (sav_eav) {
			bytes_parsed += cx231xx_get_video_line(dev, dma_q,
				sav_eav,	/* SAV/EAV */
				p_buffer + bytes_parsed,	/* p_buffer */
				buffer_size - bytes_parsed);/* buf size */
		}

		/* Now parse data that is completely in this buffer */
		/* dma_q->is_partial_line = 0;  */

		while (bytes_parsed < buffer_size) {
			u32 bytes_used = 0;

			sav_eav = cx231xx_find_next_SAV_EAV(
				p_buffer + bytes_parsed,	/* p_buffer */
				buffer_size - bytes_parsed,	/* buf size */
				&bytes_used);/* bytes used to get SAV/EAV */

			bytes_parsed += bytes_used;

			sav_eav &= 0xF0;
			if (sav_eav && (bytes_parsed < buffer_size)) {
				bytes_parsed += cx231xx_get_video_line(dev,
					dma_q, sav_eav,	/* SAV/EAV */
					p_buffer + bytes_parsed,/* p_buffer */
					buffer_size - bytes_parsed);/*buf size*/
			}
		}

		/* Save the last four bytes of the buffer so we can check the
		   buffer boundary condition next time */
		memcpy(dma_q->partial_buf, p_buffer + buffer_size - 4, 4);
		bytes_parsed = 0;

	}
	return 1;
}

static inline int cx231xx_bulk_copy(struct cx231xx *dev, struct urb *urb)
{
	struct cx231xx_dmaqueue *dma_q = urb->context;
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

	if (1) {

		/*  get buffer pointer and length */
		p_buffer = urb->transfer_buffer;
		buffer_size = urb->actual_length;
		bytes_parsed = 0;

		if (dma_q->is_partial_line) {
			/* Handle the case of a partial line */
			sav_eav = dma_q->last_sav;
		} else {
			/* Check for a SAV/EAV overlapping
				the buffer boundary */
			sav_eav =
			    cx231xx_find_boundary_SAV_EAV(p_buffer,
							  dma_q->partial_buf,
							  &bytes_parsed);
		}

		sav_eav &= 0xF0;
		/* Get the first line if we have some portion of an SAV/EAV from
		   the last buffer or a partial line  */
		if (sav_eav) {
			bytes_parsed += cx231xx_get_video_line(dev, dma_q,
				sav_eav,	/* SAV/EAV */
				p_buffer + bytes_parsed,	/* p_buffer */
				buffer_size - bytes_parsed);/* buf size */
		}

		/* Now parse data that is completely in this buffer */
		/* dma_q->is_partial_line = 0;  */

		while (bytes_parsed < buffer_size) {
			u32 bytes_used = 0;

			sav_eav = cx231xx_find_next_SAV_EAV(
				p_buffer + bytes_parsed,	/* p_buffer */
				buffer_size - bytes_parsed,	/* buf size */
				&bytes_used);/* bytes used to get SAV/EAV */

			bytes_parsed += bytes_used;

			sav_eav &= 0xF0;
			if (sav_eav && (bytes_parsed < buffer_size)) {
				bytes_parsed += cx231xx_get_video_line(dev,
					dma_q, sav_eav,	/* SAV/EAV */
					p_buffer + bytes_parsed,/* p_buffer */
					buffer_size - bytes_parsed);/*buf size*/
			}
		}

		/* Save the last four bytes of the buffer so we can check the
		   buffer boundary condition next time */
		memcpy(dma_q->partial_buf, p_buffer + buffer_size - 4, 4);
		bytes_parsed = 0;

	}
	return 1;
}


u8 cx231xx_find_boundary_SAV_EAV(u8 *p_buffer, u8 *partial_buf,
				 u32 *p_bytes_used)
{
	u32 bytes_used;
	u8 boundary_bytes[8];
	u8 sav_eav = 0;

	*p_bytes_used = 0;

	/* Create an array of the last 4 bytes of the last buffer and the first
	   4 bytes of the current buffer. */

	memcpy(boundary_bytes, partial_buf, 4);
	memcpy(boundary_bytes + 4, p_buffer, 4);

	/* Check for the SAV/EAV in the boundary buffer */
	sav_eav = cx231xx_find_next_SAV_EAV((u8 *)&boundary_bytes, 8,
					    &bytes_used);

	if (sav_eav) {
		/* found a boundary SAV/EAV.  Updates the bytes used to reflect
		   only those used in the new buffer */
		*p_bytes_used = bytes_used - 4;
	}

	return sav_eav;
}

u8 cx231xx_find_next_SAV_EAV(u8 *p_buffer, u32 buffer_size, u32 *p_bytes_used)
{
	u32 i;
	u8 sav_eav = 0;

	/*
	 * Don't search if the buffer size is less than 4.  It causes a page
	 * fault since buffer_size - 4 evaluates to a large number in that
	 * case.
	 */
	if (buffer_size < 4) {
		*p_bytes_used = buffer_size;
		return 0;
	}

	for (i = 0; i < (buffer_size - 3); i++) {

		if ((p_buffer[i] == 0xFF) &&
		    (p_buffer[i + 1] == 0x00) && (p_buffer[i + 2] == 0x00)) {

			*p_bytes_used = i + 4;
			sav_eav = p_buffer[i + 3];
			return sav_eav;
		}
	}

	*p_bytes_used = buffer_size;
	return 0;
}

u32 cx231xx_get_video_line(struct cx231xx *dev,
			   struct cx231xx_dmaqueue *dma_q, u8 sav_eav,
			   u8 *p_buffer, u32 buffer_size)
{
	u32 bytes_copied = 0;
	int current_field = -1;

	switch (sav_eav) {
	case SAV_ACTIVE_VIDEO_FIELD1:
		/* looking for skipped line which occurred in PAL 720x480 mode.
		   In this case, there will be no active data contained
		   between the SAV and EAV */
		if ((buffer_size > 3) && (p_buffer[0] == 0xFF) &&
		    (p_buffer[1] == 0x00) && (p_buffer[2] == 0x00) &&
		    ((p_buffer[3] == EAV_ACTIVE_VIDEO_FIELD1) ||
		     (p_buffer[3] == EAV_ACTIVE_VIDEO_FIELD2) ||
		     (p_buffer[3] == EAV_VBLANK_FIELD1) ||
		     (p_buffer[3] == EAV_VBLANK_FIELD2)))
			return bytes_copied;
		current_field = 1;
		break;

	case SAV_ACTIVE_VIDEO_FIELD2:
		/* looking for skipped line which occurred in PAL 720x480 mode.
		   In this case, there will be no active data contained between
		   the SAV and EAV */
		if ((buffer_size > 3) && (p_buffer[0] == 0xFF) &&
		    (p_buffer[1] == 0x00) && (p_buffer[2] == 0x00) &&
		    ((p_buffer[3] == EAV_ACTIVE_VIDEO_FIELD1) ||
		     (p_buffer[3] == EAV_ACTIVE_VIDEO_FIELD2) ||
		     (p_buffer[3] == EAV_VBLANK_FIELD1)       ||
		     (p_buffer[3] == EAV_VBLANK_FIELD2)))
			return bytes_copied;
		current_field = 2;
		break;
	}

	dma_q->last_sav = sav_eav;

	bytes_copied = cx231xx_copy_video_line(dev, dma_q, p_buffer,
					       buffer_size, current_field);

	return bytes_copied;
}

u32 cx231xx_copy_video_line(struct cx231xx *dev,
			    struct cx231xx_dmaqueue *dma_q, u8 *p_line,
			    u32 length, int field_number)
{
	u32 bytes_to_copy;
	struct cx231xx_buffer *buf;
	u32 _line_size = dev->width * 2;

	if (dma_q->current_field != field_number)
		cx231xx_reset_video_buffer(dev, dma_q);

	/* get the buffer pointer */
	if (dev->USE_ISO)
		buf = dev->video_mode.isoc_ctl.buf;
	else
		buf = dev->video_mode.bulk_ctl.buf;

	/* Remember the field number for next time */
	dma_q->current_field = field_number;

	bytes_to_copy = dma_q->bytes_left_in_line;
	if (bytes_to_copy > length)
		bytes_to_copy = length;

	if (dma_q->lines_completed >= dma_q->lines_per_field) {
		dma_q->bytes_left_in_line -= bytes_to_copy;
		dma_q->is_partial_line = (dma_q->bytes_left_in_line == 0) ?
					  0 : 1;
		return 0;
	}

	dma_q->is_partial_line = 1;

	/* If we don't have a buffer, just return the number of bytes we would
	   have copied if we had a buffer. */
	if (!buf) {
		dma_q->bytes_left_in_line -= bytes_to_copy;
		dma_q->is_partial_line = (dma_q->bytes_left_in_line == 0)
					 ? 0 : 1;
		return bytes_to_copy;
	}

	/* copy the data to video buffer */
	cx231xx_do_copy(dev, dma_q, p_line, bytes_to_copy);

	dma_q->pos += bytes_to_copy;
	dma_q->bytes_left_in_line -= bytes_to_copy;

	if (dma_q->bytes_left_in_line == 0) {
		dma_q->bytes_left_in_line = _line_size;
		dma_q->lines_completed++;
		dma_q->is_partial_line = 0;

		if (cx231xx_is_buffer_done(dev, dma_q) && buf) {
			buffer_filled(dev, dma_q, buf);

			dma_q->pos = 0;
			buf = NULL;
			dma_q->lines_completed = 0;
		}
	}

	return bytes_to_copy;
}

void cx231xx_reset_video_buffer(struct cx231xx *dev,
				struct cx231xx_dmaqueue *dma_q)
{
	struct cx231xx_buffer *buf;

	/* handle the switch from field 1 to field 2 */
	if (dma_q->current_field == 1) {
		if (dma_q->lines_completed >= dma_q->lines_per_field)
			dma_q->field1_done = 1;
		else
			dma_q->field1_done = 0;
	}

	if (dev->USE_ISO)
		buf = dev->video_mode.isoc_ctl.buf;
	else
		buf = dev->video_mode.bulk_ctl.buf;

	if (buf == NULL) {
		/* first try to get the buffer */
		get_next_buf(dma_q, &buf);

		dma_q->pos = 0;
		dma_q->field1_done = 0;
		dma_q->current_field = -1;
	}

	/* reset the counters */
	dma_q->bytes_left_in_line = dev->width << 1;
	dma_q->lines_completed = 0;
}

int cx231xx_do_copy(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
		    u8 *p_buffer, u32 bytes_to_copy)
{
	u8 *p_out_buffer = NULL;
	u32 current_line_bytes_copied = 0;
	struct cx231xx_buffer *buf;
	u32 _line_size = dev->width << 1;
	void *startwrite;
	int offset, lencopy;

	if (dev->USE_ISO)
		buf = dev->video_mode.isoc_ctl.buf;
	else
		buf = dev->video_mode.bulk_ctl.buf;

	if (buf == NULL)
		return -1;

	p_out_buffer = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	current_line_bytes_copied = _line_size - dma_q->bytes_left_in_line;

	/* Offset field 2 one line from the top of the buffer */
	offset = (dma_q->current_field == 1) ? 0 : _line_size;

	/* Offset for field 2 */
	startwrite = p_out_buffer + offset;

	/* lines already completed in the current field */
	startwrite += (dma_q->lines_completed * _line_size * 2);

	/* bytes already completed in the current line */
	startwrite += current_line_bytes_copied;

	lencopy = dma_q->bytes_left_in_line > bytes_to_copy ?
		  bytes_to_copy : dma_q->bytes_left_in_line;

	if ((u8 *)(startwrite + lencopy) > (u8 *)(p_out_buffer + dev->size))
		return 0;

	/* The below copies the UYVY data straight into video buffer */
	cx231xx_swab((u16 *) p_buffer, (u16 *) startwrite, (u16) lencopy);

	return 0;
}

void cx231xx_swab(u16 *from, u16 *to, u16 len)
{
	u16 i;

	if (len <= 0)
		return;

	for (i = 0; i < len / 2; i++)
		to[i] = (from[i] << 8) | (from[i] >> 8);
}

u8 cx231xx_is_buffer_done(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q)
{
	u8 buffer_complete = 0;

	/* Dual field stream */
	buffer_complete = ((dma_q->current_field == 2) &&
			   (dma_q->lines_completed >= dma_q->lines_per_field) &&
			    dma_q->field1_done);

	return buffer_complete;
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/

static int queue_setup(struct vb2_queue *vq,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);

	dev->size = (dev->width * dev->height * dev->format->depth + 7) >> 3;

	if (vq->num_buffers + *nbuffers < CX231XX_MIN_BUF)
		*nbuffers = CX231XX_MIN_BUF - vq->num_buffers;

	if (*nplanes)
		return sizes[0] < dev->size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = dev->size;

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct cx231xx_buffer *buf =
	    container_of(vb, struct cx231xx_buffer, vb.vb2_buf);
	struct cx231xx *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct cx231xx_dmaqueue *vidq = &dev->video_mode.vidq;
	unsigned long flags;

	spin_lock_irqsave(&dev->video_mode.slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&dev->video_mode.slock, flags);
}

static void return_all_buffers(struct cx231xx *dev,
			       enum vb2_buffer_state state)
{
	struct cx231xx_dmaqueue *vidq = &dev->video_mode.vidq;
	struct cx231xx_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&dev->video_mode.slock, flags);
	if (dev->USE_ISO)
		dev->video_mode.isoc_ctl.buf = NULL;
	else
		dev->video_mode.bulk_ctl.buf = NULL;
	list_for_each_entry_safe(buf, node, &vidq->active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&dev->video_mode.slock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);
	struct cx231xx_dmaqueue *vidq = &dev->video_mode.vidq;
	int ret = 0;

	vidq->sequence = 0;
	dev->mode_tv = 0;

	cx231xx_enable_analog_tuner(dev);
	if (dev->USE_ISO)
		ret = cx231xx_init_isoc(dev, CX231XX_NUM_PACKETS,
					CX231XX_NUM_BUFS,
					dev->video_mode.max_pkt_size,
					cx231xx_isoc_copy);
	else
		ret = cx231xx_init_bulk(dev, CX231XX_NUM_PACKETS,
					CX231XX_NUM_BUFS,
					dev->video_mode.max_pkt_size,
					cx231xx_bulk_copy);
	if (ret)
		return_all_buffers(dev, VB2_BUF_STATE_QUEUED);
	call_all(dev, video, s_stream, 1);
	return ret;
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);

	call_all(dev, video, s_stream, 0);
	return_all_buffers(dev, VB2_BUF_STATE_ERROR);
}

static struct vb2_ops cx231xx_video_qops = {
	.queue_setup		= queue_setup,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/*********************  v4l2 interface  **************************************/

void video_mux(struct cx231xx *dev, int index)
{
	dev->video_input = index;
	dev->ctl_ainput = INPUT(index)->amux;

	cx231xx_set_video_input_mux(dev, index);

	cx25840_call(dev, video, s_routing, INPUT(index)->vmux, 0, 0);

	cx231xx_set_audio_input(dev, dev->ctl_ainput);

	dev_dbg(dev->dev, "video_mux : %d\n", index);

	/* do mode control overrides if required */
	cx231xx_do_mode_ctrl_overrides(dev);
}

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);

	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.pixelformat = dev->format->fourcc;
	f->fmt.pix.bytesperline = (dev->width * dev->format->depth + 7) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * dev->height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	return 0;
}

static struct cx231xx_fmt *format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format); i++)
		if (format[i].fourcc == fourcc)
			return &format[i];

	return NULL;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);
	unsigned int width = f->fmt.pix.width;
	unsigned int height = f->fmt.pix.height;
	unsigned int maxw = norm_maxw(dev);
	unsigned int maxh = norm_maxh(dev);
	struct cx231xx_fmt *fmt;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (!fmt) {
		cx231xx_videodbg("Fourcc format (%08x) invalid.\n",
				 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	/* width must even because of the YUYV format
	   height must be even because of interlacing */
	v4l_bound_align_image(&width, 48, maxw, 1, &height, 32, maxh, 1, 0);

	f->fmt.pix.width = width;
	f->fmt.pix.height = height;
	f->fmt.pix.pixelformat = fmt->fourcc;
	f->fmt.pix.bytesperline = (width * fmt->depth + 7) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int rc;

	rc = vidioc_try_fmt_vid_cap(file, priv, f);
	if (rc)
		return rc;

	if (vb2_is_busy(&dev->vidq)) {
		dev_err(dev->dev, "%s: queue busy\n", __func__);
		return -EBUSY;
	}

	/* set new image size */
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	dev->format = format_by_fourcc(f->fmt.pix.pixelformat);

	v4l2_fill_mbus_format(&format.format, &f->fmt.pix, MEDIA_BUS_FMT_FIXED);
	call_all(dev, pad, set_fmt, NULL, &format);
	v4l2_fill_pix_format(&f->fmt.pix, &format.format);

	return rc;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct cx231xx *dev = video_drvdata(file);

	*id = dev->norm;
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id norm)
{
	struct cx231xx *dev = video_drvdata(file);
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	if (dev->norm == norm)
		return 0;

	if (vb2_is_busy(&dev->vidq))
		return -EBUSY;

	dev->norm = norm;

	/* Adjusts width/height, if needed */
	dev->width = 720;
	dev->height = (dev->norm & V4L2_STD_625_50) ? 576 : 480;

	call_all(dev, video, s_std, dev->norm);

	/* We need to reset basic properties in the decoder related to
	   resolution (since a standard change effects things like the number
	   of lines in VACT, etc) */
	format.format.code = MEDIA_BUS_FMT_FIXED;
	format.format.width = dev->width;
	format.format.height = dev->height;
	call_all(dev, pad, set_fmt, NULL, &format);

	/* do mode control overrides */
	cx231xx_do_mode_ctrl_overrides(dev);

	return 0;
}

static const char *iname[] = {
	[CX231XX_VMUX_COMPOSITE1] = "Composite1",
	[CX231XX_VMUX_SVIDEO]     = "S-Video",
	[CX231XX_VMUX_TELEVISION] = "Television",
	[CX231XX_VMUX_CABLE]      = "Cable TV",
	[CX231XX_VMUX_DVB]        = "DVB",
};

void cx231xx_v4l2_create_entities(struct cx231xx *dev)
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	int ret, i;

	/* Create entities for each input connector */
	for (i = 0; i < MAX_CX231XX_INPUT; i++) {
		struct media_entity *ent = &dev->input_ent[i];

		if (!INPUT(i)->type)
			break;

		ent->name = iname[INPUT(i)->type];
		ent->flags = MEDIA_ENT_FL_CONNECTOR;
		dev->input_pad[i].flags = MEDIA_PAD_FL_SOURCE;

		switch (INPUT(i)->type) {
		case CX231XX_VMUX_COMPOSITE1:
			ent->function = MEDIA_ENT_F_CONN_COMPOSITE;
			break;
		case CX231XX_VMUX_SVIDEO:
			ent->function = MEDIA_ENT_F_CONN_SVIDEO;
			break;
		case CX231XX_VMUX_TELEVISION:
		case CX231XX_VMUX_CABLE:
		case CX231XX_VMUX_DVB:
			/* The DVB core will handle it */
			if (dev->tuner_type == TUNER_ABSENT)
				continue;
			fallthrough;
		default: /* just to shut up a gcc warning */
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

int cx231xx_enum_input(struct file *file, void *priv,
			     struct v4l2_input *i)
{
	struct cx231xx *dev = video_drvdata(file);
	u32 gen_stat;
	unsigned int n;
	int ret;

	n = i->index;
	if (n >= MAX_CX231XX_INPUT)
		return -EINVAL;
	if (0 == INPUT(n)->type)
		return -EINVAL;

	i->index = n;
	i->type = V4L2_INPUT_TYPE_CAMERA;

	strscpy(i->name, iname[INPUT(n)->type], sizeof(i->name));

	if ((CX231XX_VMUX_TELEVISION == INPUT(n)->type) ||
	    (CX231XX_VMUX_CABLE == INPUT(n)->type))
		i->type = V4L2_INPUT_TYPE_TUNER;

	i->std = dev->vdev.tvnorms;

	/* If they are asking about the active input, read signal status */
	if (n == dev->video_input) {
		ret = cx231xx_read_i2c_data(dev, VID_BLK_I2C_ADDRESS,
					    GEN_STAT, 2, &gen_stat, 4);
		if (ret > 0) {
			if ((gen_stat & FLD_VPRES) == 0x00)
				i->status |= V4L2_IN_ST_NO_SIGNAL;
			if ((gen_stat & FLD_HLOCK) == 0x00)
				i->status |= V4L2_IN_ST_NO_H_LOCK;
		}
	}

	return 0;
}

int cx231xx_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct cx231xx *dev = video_drvdata(file);

	*i = dev->video_input;

	return 0;
}

int cx231xx_s_input(struct file *file, void *priv, unsigned int i)
{
	struct cx231xx *dev = video_drvdata(file);

	dev->mode_tv = 0;

	if (i >= MAX_CX231XX_INPUT)
		return -EINVAL;
	if (0 == INPUT(i)->type)
		return -EINVAL;

	video_mux(dev, i);

	if (INPUT(i)->type == CX231XX_VMUX_TELEVISION ||
	    INPUT(i)->type == CX231XX_VMUX_CABLE) {
		/* There's a tuner, so reset the standard and put it on the
		   last known frequency (since it was probably powered down
		   until now */
		call_all(dev, video, s_std, dev->norm);
	}

	return 0;
}

int cx231xx_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct cx231xx *dev = video_drvdata(file);

	if (0 != t->index)
		return -EINVAL;

	strscpy(t->name, "Tuner", sizeof(t->name));

	t->type = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM;
	t->rangehigh = 0xffffffffUL;
	t->signal = 0xffff;	/* LOCKED */
	call_all(dev, tuner, g_tuner, t);

	return 0;
}

int cx231xx_s_tuner(struct file *file, void *priv, const struct v4l2_tuner *t)
{
	if (0 != t->index)
		return -EINVAL;
	return 0;
}

int cx231xx_g_frequency(struct file *file, void *priv,
			      struct v4l2_frequency *f)
{
	struct cx231xx *dev = video_drvdata(file);

	if (f->tuner)
		return -EINVAL;

	f->frequency = dev->ctl_freq;

	return 0;
}

int cx231xx_s_frequency(struct file *file, void *priv,
			      const struct v4l2_frequency *f)
{
	struct cx231xx *dev = video_drvdata(file);
	struct v4l2_frequency new_freq = *f;
	int rc, need_if_freq = 0;
	u32 if_frequency = 5400000;

	dev_dbg(dev->dev,
		"Enter vidioc_s_frequency()f->frequency=%d;f->type=%d\n",
		f->frequency, f->type);

	if (0 != f->tuner)
		return -EINVAL;

	/* set pre channel change settings in DIF first */
	rc = cx231xx_tuner_pre_channel_change(dev);

	switch (dev->model) { /* i2c device tuners */
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
	case CX231XX_BOARD_HAUPPAUGE_935C:
	case CX231XX_BOARD_HAUPPAUGE_955Q:
	case CX231XX_BOARD_HAUPPAUGE_975:
	case CX231XX_BOARD_EVROMEDIA_FULL_HYBRID_FULLHD:
		if (dev->cx231xx_set_analog_freq)
			dev->cx231xx_set_analog_freq(dev, f->frequency);
		dev->ctl_freq = f->frequency;
		need_if_freq = 1;
		break;
	default:
		call_all(dev, tuner, s_frequency, f);
		call_all(dev, tuner, g_frequency, &new_freq);
		dev->ctl_freq = new_freq.frequency;
		break;
	}

	pr_debug("%s() %u  :  %u\n", __func__, f->frequency, dev->ctl_freq);

	/* set post channel change settings in DIF first */
	rc = cx231xx_tuner_post_channel_change(dev);

	if (need_if_freq || dev->tuner_type == TUNER_NXP_TDA18271) {
		if (dev->norm & (V4L2_STD_MN | V4L2_STD_NTSC_443))
			if_frequency = 5400000;  /*5.4MHz	*/
		else if (dev->norm & V4L2_STD_B)
			if_frequency = 6000000;  /*6.0MHz	*/
		else if (dev->norm & (V4L2_STD_PAL_DK | V4L2_STD_SECAM_DK))
			if_frequency = 6900000;  /*6.9MHz	*/
		else if (dev->norm & V4L2_STD_GH)
			if_frequency = 7100000;  /*7.1MHz	*/
		else if (dev->norm & V4L2_STD_PAL_I)
			if_frequency = 7250000;  /*7.25MHz	*/
		else if (dev->norm & V4L2_STD_SECAM_L)
			if_frequency = 6900000;  /*6.9MHz	*/
		else if (dev->norm & V4L2_STD_SECAM_LC)
			if_frequency = 1250000;  /*1.25MHz	*/

		dev_dbg(dev->dev,
			"if_frequency is set to %d\n", if_frequency);
		cx231xx_set_Colibri_For_LowIF(dev, if_frequency, 1, 1);

		update_HH_register_after_set_DIF(dev);
	}

	dev_dbg(dev->dev, "Set New FREQUENCY to %d\n", f->frequency);

	return rc;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG

int cx231xx_g_chip_info(struct file *file, void *fh,
			struct v4l2_dbg_chip_info *chip)
{
	switch (chip->match.addr) {
	case 0:	/* Cx231xx - internal registers */
		return 0;
	case 1:	/* AFE - read byte */
		strscpy(chip->name, "AFE (byte)", sizeof(chip->name));
		return 0;
	case 2:	/* Video Block - read byte */
		strscpy(chip->name, "Video (byte)", sizeof(chip->name));
		return 0;
	case 3:	/* I2S block - read byte */
		strscpy(chip->name, "I2S (byte)", sizeof(chip->name));
		return 0;
	case 4: /* AFE - read dword */
		strscpy(chip->name, "AFE (dword)", sizeof(chip->name));
		return 0;
	case 5: /* Video Block - read dword */
		strscpy(chip->name, "Video (dword)", sizeof(chip->name));
		return 0;
	case 6: /* I2S Block - read dword */
		strscpy(chip->name, "I2S (dword)", sizeof(chip->name));
		return 0;
	}
	return -EINVAL;
}

int cx231xx_g_register(struct file *file, void *priv,
			     struct v4l2_dbg_register *reg)
{
	struct cx231xx *dev = video_drvdata(file);
	int ret;
	u8 value[4] = { 0, 0, 0, 0 };
	u32 data = 0;

	switch (reg->match.addr) {
	case 0:	/* Cx231xx - internal registers */
		ret = cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER,
				(u16)reg->reg, value, 4);
		reg->val = value[0] | value[1] << 8 |
			value[2] << 16 | (u32)value[3] << 24;
		reg->size = 4;
		break;
	case 1:	/* AFE - read byte */
		ret = cx231xx_read_i2c_data(dev, AFE_DEVICE_ADDRESS,
				(u16)reg->reg, 2, &data, 1);
		reg->val = data;
		reg->size = 1;
		break;
	case 2:	/* Video Block - read byte */
		ret = cx231xx_read_i2c_data(dev, VID_BLK_I2C_ADDRESS,
				(u16)reg->reg, 2, &data, 1);
		reg->val = data;
		reg->size = 1;
		break;
	case 3:	/* I2S block - read byte */
		ret = cx231xx_read_i2c_data(dev, I2S_BLK_DEVICE_ADDRESS,
				(u16)reg->reg, 1, &data, 1);
		reg->val = data;
		reg->size = 1;
		break;
	case 4: /* AFE - read dword */
		ret = cx231xx_read_i2c_data(dev, AFE_DEVICE_ADDRESS,
				(u16)reg->reg, 2, &data, 4);
		reg->val = data;
		reg->size = 4;
		break;
	case 5: /* Video Block - read dword */
		ret = cx231xx_read_i2c_data(dev, VID_BLK_I2C_ADDRESS,
				(u16)reg->reg, 2, &data, 4);
		reg->val = data;
		reg->size = 4;
		break;
	case 6: /* I2S Block - read dword */
		ret = cx231xx_read_i2c_data(dev, I2S_BLK_DEVICE_ADDRESS,
				(u16)reg->reg, 1, &data, 4);
		reg->val = data;
		reg->size = 4;
		break;
	default:
		return -EINVAL;
	}
	return ret < 0 ? ret : 0;
}

int cx231xx_s_register(struct file *file, void *priv,
			     const struct v4l2_dbg_register *reg)
{
	struct cx231xx *dev = video_drvdata(file);
	int ret;
	u8 data[4] = { 0, 0, 0, 0 };

	switch (reg->match.addr) {
	case 0:	/* cx231xx internal registers */
		data[0] = (u8) reg->val;
		data[1] = (u8) (reg->val >> 8);
		data[2] = (u8) (reg->val >> 16);
		data[3] = (u8) (reg->val >> 24);
		ret = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
				(u16)reg->reg, data, 4);
		break;
	case 1:	/* AFE - write byte */
		ret = cx231xx_write_i2c_data(dev, AFE_DEVICE_ADDRESS,
				(u16)reg->reg, 2, reg->val, 1);
		break;
	case 2:	/* Video Block - write byte */
		ret = cx231xx_write_i2c_data(dev, VID_BLK_I2C_ADDRESS,
				(u16)reg->reg, 2, reg->val, 1);
		break;
	case 3:	/* I2S block - write byte */
		ret = cx231xx_write_i2c_data(dev, I2S_BLK_DEVICE_ADDRESS,
				(u16)reg->reg, 1, reg->val, 1);
		break;
	case 4: /* AFE - write dword */
		ret = cx231xx_write_i2c_data(dev, AFE_DEVICE_ADDRESS,
				(u16)reg->reg, 2, reg->val, 4);
		break;
	case 5: /* Video Block - write dword */
		ret = cx231xx_write_i2c_data(dev, VID_BLK_I2C_ADDRESS,
				(u16)reg->reg, 2, reg->val, 4);
		break;
	case 6: /* I2S block - write dword */
		ret = cx231xx_write_i2c_data(dev, I2S_BLK_DEVICE_ADDRESS,
				(u16)reg->reg, 1, reg->val, 4);
		break;
	default:
		return -EINVAL;
	}
	return ret < 0 ? ret : 0;
}
#endif

static int vidioc_g_pixelaspect(struct file *file, void *priv,
				int type, struct v4l2_fract *f)
{
	struct cx231xx *dev = video_drvdata(file);
	bool is_50hz = dev->norm & V4L2_STD_625_50;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f->numerator = is_50hz ? 54 : 11;
	f->denominator = is_50hz ? 59 : 10;

	return 0;
}

static int vidioc_g_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct cx231xx *dev = video_drvdata(file);

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

int cx231xx_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct cx231xx *dev = video_drvdata(file);

	strscpy(cap->driver, "cx231xx", sizeof(cap->driver));
	strscpy(cap->card, cx231xx_boards[dev->model].name, sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_READWRITE |
		V4L2_CAP_VBI_CAPTURE | V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	if (video_is_registered(&dev->radio_dev))
		cap->capabilities |= V4L2_CAP_RADIO;

	switch (dev->model) {
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
	case CX231XX_BOARD_HAUPPAUGE_935C:
	case CX231XX_BOARD_HAUPPAUGE_955Q:
	case CX231XX_BOARD_HAUPPAUGE_975:
	case CX231XX_BOARD_EVROMEDIA_FULL_HYBRID_FULLHD:
		cap->capabilities |= V4L2_CAP_TUNER;
		break;
	default:
		if (dev->tuner_type != TUNER_ABSENT)
			cap->capabilities |= V4L2_CAP_TUNER;
		break;
	}
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(format)))
		return -EINVAL;

	f->pixelformat = format[f->index].fourcc;

	return 0;
}

/* RAW VBI ioctls */

static int vidioc_g_fmt_vbi_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);

	f->fmt.vbi.sampling_rate = 6750000 * 4;
	f->fmt.vbi.samples_per_line = VBI_LINE_LENGTH;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 0;
	f->fmt.vbi.start[0] = (dev->norm & V4L2_STD_625_50) ?
	    PAL_VBI_START_LINE : NTSC_VBI_START_LINE;
	f->fmt.vbi.count[0] = (dev->norm & V4L2_STD_625_50) ?
	    PAL_VBI_LINES : NTSC_VBI_LINES;
	f->fmt.vbi.start[1] = (dev->norm & V4L2_STD_625_50) ?
	    PAL_VBI_START_LINE + 312 : NTSC_VBI_START_LINE + 263;
	f->fmt.vbi.count[1] = f->fmt.vbi.count[0];
	memset(f->fmt.vbi.reserved, 0, sizeof(f->fmt.vbi.reserved));

	return 0;

}

static int vidioc_try_fmt_vbi_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);

	f->fmt.vbi.sampling_rate = 6750000 * 4;
	f->fmt.vbi.samples_per_line = VBI_LINE_LENGTH;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 0;
	f->fmt.vbi.flags = 0;
	f->fmt.vbi.start[0] = (dev->norm & V4L2_STD_625_50) ?
	    PAL_VBI_START_LINE : NTSC_VBI_START_LINE;
	f->fmt.vbi.count[0] = (dev->norm & V4L2_STD_625_50) ?
	    PAL_VBI_LINES : NTSC_VBI_LINES;
	f->fmt.vbi.start[1] = (dev->norm & V4L2_STD_625_50) ?
	    PAL_VBI_START_LINE + 312 : NTSC_VBI_START_LINE + 263;
	f->fmt.vbi.count[1] = f->fmt.vbi.count[0];
	memset(f->fmt.vbi.reserved, 0, sizeof(f->fmt.vbi.reserved));

	return 0;

}

static int vidioc_s_fmt_vbi_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	return vidioc_try_fmt_vbi_cap(file, priv, f);
}

/* ----------------------------------------------------------- */
/* RADIO ESPECIFIC IOCTLS                                      */
/* ----------------------------------------------------------- */

static int radio_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct cx231xx *dev = video_drvdata(file);

	if (t->index)
		return -EINVAL;

	strscpy(t->name, "Radio", sizeof(t->name));

	call_all(dev, tuner, g_tuner, t);

	return 0;
}
static int radio_s_tuner(struct file *file, void *priv, const struct v4l2_tuner *t)
{
	struct cx231xx *dev = video_drvdata(file);

	if (t->index)
		return -EINVAL;

	call_all(dev, tuner, s_tuner, t);

	return 0;
}

/*
 * cx231xx_v4l2_open()
 * inits the device and starts isoc transfer
 */
static int cx231xx_v4l2_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct cx231xx *dev = video_drvdata(filp);
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(filp);
	if (ret) {
		mutex_unlock(&dev->lock);
		return ret;
	}

	if (dev->users++ == 0) {
		/* Power up in Analog TV mode */
		if (dev->board.external_av)
			cx231xx_set_power_mode(dev,
				 POLARIS_AVMODE_ENXTERNAL_AV);
		else
			cx231xx_set_power_mode(dev, POLARIS_AVMODE_ANALOGT_TV);

		/* set video alternate setting */
		cx231xx_set_video_alternate(dev);

		/* Needed, since GPIO might have disabled power of
		   some i2c device */
		cx231xx_config_i2c(dev);

		/* device needs to be initialized before isoc transfer */
		dev->video_input = dev->video_input > 2 ? 2 : dev->video_input;
	}

	if (vdev->vfl_type == VFL_TYPE_RADIO) {
		cx231xx_videodbg("video_open: setting radio device\n");

		/* cx231xx_start_radio(dev); */

		call_all(dev, tuner, s_radio);
	}
	if (vdev->vfl_type == VFL_TYPE_VBI) {
		/* Set the required alternate setting  VBI interface works in
		   Bulk mode only */
		cx231xx_set_alt_setting(dev, INDEX_VANC, 0);
	}
	mutex_unlock(&dev->lock);
	return 0;
}

/*
 * cx231xx_realease_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconnected or at module unload
*/
void cx231xx_release_analog_resources(struct cx231xx *dev)
{

	/*FIXME: I2C IR should be disconnected */

	if (video_is_registered(&dev->radio_dev))
		video_unregister_device(&dev->radio_dev);
	if (video_is_registered(&dev->vbi_dev)) {
		dev_info(dev->dev, "V4L2 device %s deregistered\n",
			video_device_node_name(&dev->vbi_dev));
		video_unregister_device(&dev->vbi_dev);
	}
	if (video_is_registered(&dev->vdev)) {
		dev_info(dev->dev, "V4L2 device %s deregistered\n",
			video_device_node_name(&dev->vdev));

		if (dev->board.has_417)
			cx231xx_417_unregister(dev);

		video_unregister_device(&dev->vdev);
	}
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	v4l2_ctrl_handler_free(&dev->radio_ctrl_handler);
}

/*
 * cx231xx_close()
 * stops streaming and deallocates all resources allocated by the v4l2
 * calls and ioctls
 */
static int cx231xx_close(struct file *filp)
{
	struct cx231xx *dev = video_drvdata(filp);
	struct video_device *vdev = video_devdata(filp);

	_vb2_fop_release(filp, NULL);

	if (--dev->users == 0) {
		/* Save some power by putting tuner to sleep */
		call_all(dev, tuner, standby);

		/* do this before setting alternate! */
		if (dev->USE_ISO)
			cx231xx_uninit_isoc(dev);
		else
			cx231xx_uninit_bulk(dev);
		cx231xx_set_mode(dev, CX231XX_SUSPEND);
	}

	/*
	 * To workaround error number=-71 on EP0 for VideoGrabber,
	 *	 need exclude following.
	 * FIXME: It is probably safe to remove most of these, as we're
	 * now avoiding the alternate setting for INDEX_VANC
	 */
	if (!dev->board.no_alt_vanc && vdev->vfl_type == VFL_TYPE_VBI) {
		/* do this before setting alternate! */
		cx231xx_uninit_vbi_isoc(dev);

		/* set alternate 0 */
		if (!dev->vbi_or_sliced_cc_mode)
			cx231xx_set_alt_setting(dev, INDEX_VANC, 0);
		else
			cx231xx_set_alt_setting(dev, INDEX_HANC, 0);

		wake_up_interruptible_nr(&dev->open, 1);
		return 0;
	}

	if (dev->users == 0) {
		/* set alternate 0 */
		cx231xx_set_alt_setting(dev, INDEX_VIDEO, 0);
	}

	wake_up_interruptible(&dev->open);
	return 0;
}

static int cx231xx_v4l2_close(struct file *filp)
{
	struct cx231xx *dev = video_drvdata(filp);
	int rc;

	mutex_lock(&dev->lock);
	rc = cx231xx_close(filp);
	mutex_unlock(&dev->lock);
	return rc;
}

static const struct v4l2_file_operations cx231xx_v4l_fops = {
	.owner   = THIS_MODULE,
	.open    = cx231xx_v4l2_open,
	.release = cx231xx_v4l2_close,
	.read    = vb2_fop_read,
	.poll    = vb2_fop_poll,
	.mmap    = vb2_fop_mmap,
	.unlocked_ioctl   = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap               = cx231xx_querycap,
	.vidioc_enum_fmt_vid_cap       = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap          = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap        = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap          = vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap          = vidioc_g_fmt_vbi_cap,
	.vidioc_try_fmt_vbi_cap        = vidioc_try_fmt_vbi_cap,
	.vidioc_s_fmt_vbi_cap          = vidioc_s_fmt_vbi_cap,
	.vidioc_g_pixelaspect          = vidioc_g_pixelaspect,
	.vidioc_g_selection            = vidioc_g_selection,
	.vidioc_reqbufs                = vb2_ioctl_reqbufs,
	.vidioc_querybuf               = vb2_ioctl_querybuf,
	.vidioc_qbuf                   = vb2_ioctl_qbuf,
	.vidioc_dqbuf                  = vb2_ioctl_dqbuf,
	.vidioc_s_std                  = vidioc_s_std,
	.vidioc_g_std                  = vidioc_g_std,
	.vidioc_enum_input             = cx231xx_enum_input,
	.vidioc_g_input                = cx231xx_g_input,
	.vidioc_s_input                = cx231xx_s_input,
	.vidioc_streamon               = vb2_ioctl_streamon,
	.vidioc_streamoff              = vb2_ioctl_streamoff,
	.vidioc_g_tuner                = cx231xx_g_tuner,
	.vidioc_s_tuner                = cx231xx_s_tuner,
	.vidioc_g_frequency            = cx231xx_g_frequency,
	.vidioc_s_frequency            = cx231xx_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_chip_info            = cx231xx_g_chip_info,
	.vidioc_g_register             = cx231xx_g_register,
	.vidioc_s_register             = cx231xx_s_register,
#endif
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device cx231xx_vbi_template;

static const struct video_device cx231xx_video_template = {
	.fops         = &cx231xx_v4l_fops,
	.release      = video_device_release_empty,
	.ioctl_ops    = &video_ioctl_ops,
	.tvnorms      = V4L2_STD_ALL,
};

static const struct v4l2_file_operations radio_fops = {
	.owner   = THIS_MODULE,
	.open   = cx231xx_v4l2_open,
	.release = cx231xx_v4l2_close,
	.poll = v4l2_ctrl_poll,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops radio_ioctl_ops = {
	.vidioc_querycap    = cx231xx_querycap,
	.vidioc_g_tuner     = radio_g_tuner,
	.vidioc_s_tuner     = radio_s_tuner,
	.vidioc_g_frequency = cx231xx_g_frequency,
	.vidioc_s_frequency = cx231xx_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_chip_info = cx231xx_g_chip_info,
	.vidioc_g_register  = cx231xx_g_register,
	.vidioc_s_register  = cx231xx_s_register,
#endif
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device cx231xx_radio_template = {
	.name      = "cx231xx-radio",
	.fops      = &radio_fops,
	.ioctl_ops = &radio_ioctl_ops,
};

/******************************** usb interface ******************************/

static void cx231xx_vdev_init(struct cx231xx *dev,
		struct video_device *vfd,
		const struct video_device *template,
		const char *type_name)
{
	*vfd = *template;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->release = video_device_release_empty;
	vfd->lock = &dev->lock;

	snprintf(vfd->name, sizeof(vfd->name), "%s %s", dev->name, type_name);

	video_set_drvdata(vfd, dev);
	if (dev->tuner_type == TUNER_ABSENT) {
		switch (dev->model) {
		case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
		case CX231XX_BOARD_HAUPPAUGE_935C:
		case CX231XX_BOARD_HAUPPAUGE_955Q:
		case CX231XX_BOARD_HAUPPAUGE_975:
		case CX231XX_BOARD_EVROMEDIA_FULL_HYBRID_FULLHD:
			break;
		default:
			v4l2_disable_ioctl(vfd, VIDIOC_G_FREQUENCY);
			v4l2_disable_ioctl(vfd, VIDIOC_S_FREQUENCY);
			v4l2_disable_ioctl(vfd, VIDIOC_G_TUNER);
			v4l2_disable_ioctl(vfd, VIDIOC_S_TUNER);
			break;
		}
	}
}

int cx231xx_register_analog_devices(struct cx231xx *dev)
{
	struct vb2_queue *q;
	int ret;

	dev_info(dev->dev, "v4l2 driver version %s\n", CX231XX_VERSION);

	/* set default norm */
	dev->norm = V4L2_STD_PAL;
	dev->width = norm_maxw(dev);
	dev->height = norm_maxh(dev);
	dev->interlaced = 0;

	/* Analog specific initialization */
	dev->format = &format[0];

	/* Set the initial input */
	video_mux(dev, dev->video_input);

	call_all(dev, video, s_std, dev->norm);

	v4l2_ctrl_handler_init(&dev->ctrl_handler, 10);
	v4l2_ctrl_handler_init(&dev->radio_ctrl_handler, 5);

	if (dev->sd_cx25840) {
		v4l2_ctrl_add_handler(&dev->ctrl_handler,
				dev->sd_cx25840->ctrl_handler, NULL, true);
		v4l2_ctrl_add_handler(&dev->radio_ctrl_handler,
				dev->sd_cx25840->ctrl_handler,
				v4l2_ctrl_radio_filter, true);
	}

	if (dev->ctrl_handler.error)
		return dev->ctrl_handler.error;
	if (dev->radio_ctrl_handler.error)
		return dev->radio_ctrl_handler.error;

	/* enable vbi capturing */
	/* write code here...  */

	/* allocate and fill video video_device struct */
	cx231xx_vdev_init(dev, &dev->vdev, &cx231xx_video_template, "video");
#if defined(CONFIG_MEDIA_CONTROLLER)
	dev->video_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&dev->vdev.entity, 1, &dev->video_pad);
	if (ret < 0)
		dev_err(dev->dev, "failed to initialize video media entity!\n");
#endif
	dev->vdev.ctrl_handler = &dev->ctrl_handler;

	q = &dev->vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_USERPTR | VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct cx231xx_buffer);
	q->ops = &cx231xx_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 1;
	q->lock = &dev->lock;
	ret = vb2_queue_init(q);
	if (ret)
		return ret;
	dev->vdev.queue = q;
	dev->vdev.device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
				V4L2_CAP_VIDEO_CAPTURE;

	switch (dev->model) { /* i2c device tuners */
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
	case CX231XX_BOARD_HAUPPAUGE_935C:
	case CX231XX_BOARD_HAUPPAUGE_955Q:
	case CX231XX_BOARD_HAUPPAUGE_975:
	case CX231XX_BOARD_EVROMEDIA_FULL_HYBRID_FULLHD:
		dev->vdev.device_caps |= V4L2_CAP_TUNER;
		break;
	default:
		if (dev->tuner_type != TUNER_ABSENT)
			dev->vdev.device_caps |= V4L2_CAP_TUNER;
		break;
	}

	/* register v4l2 video video_device */
	ret = video_register_device(&dev->vdev, VFL_TYPE_VIDEO,
				    video_nr[dev->devno]);
	if (ret) {
		dev_err(dev->dev,
			"unable to register video device (error=%i).\n",
			ret);
		return ret;
	}

	dev_info(dev->dev, "Registered video device %s [v4l2]\n",
		video_device_node_name(&dev->vdev));

	/* Initialize VBI template */
	cx231xx_vbi_template = cx231xx_video_template;
	strscpy(cx231xx_vbi_template.name, "cx231xx-vbi",
		sizeof(cx231xx_vbi_template.name));

	/* Allocate and fill vbi video_device struct */
	cx231xx_vdev_init(dev, &dev->vbi_dev, &cx231xx_vbi_template, "vbi");

#if defined(CONFIG_MEDIA_CONTROLLER)
	dev->vbi_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&dev->vbi_dev.entity, 1, &dev->vbi_pad);
	if (ret < 0)
		dev_err(dev->dev, "failed to initialize vbi media entity!\n");
#endif
	dev->vbi_dev.ctrl_handler = &dev->ctrl_handler;

	q = &dev->vbiq;
	q->type = V4L2_BUF_TYPE_VBI_CAPTURE;
	q->io_modes = VB2_USERPTR | VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct cx231xx_buffer);
	q->ops = &cx231xx_vbi_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 1;
	q->lock = &dev->lock;
	ret = vb2_queue_init(q);
	if (ret)
		return ret;
	dev->vbi_dev.queue = q;
	dev->vbi_dev.device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
				   V4L2_CAP_VBI_CAPTURE;
	switch (dev->model) { /* i2c device tuners */
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
	case CX231XX_BOARD_HAUPPAUGE_935C:
	case CX231XX_BOARD_HAUPPAUGE_955Q:
	case CX231XX_BOARD_HAUPPAUGE_975:
	case CX231XX_BOARD_EVROMEDIA_FULL_HYBRID_FULLHD:
		dev->vbi_dev.device_caps |= V4L2_CAP_TUNER;
		break;
	default:
		if (dev->tuner_type != TUNER_ABSENT)
			dev->vbi_dev.device_caps |= V4L2_CAP_TUNER;
	}

	/* register v4l2 vbi video_device */
	ret = video_register_device(&dev->vbi_dev, VFL_TYPE_VBI,
				    vbi_nr[dev->devno]);
	if (ret < 0) {
		dev_err(dev->dev, "unable to register vbi device\n");
		return ret;
	}

	dev_info(dev->dev, "Registered VBI device %s\n",
		video_device_node_name(&dev->vbi_dev));

	if (cx231xx_boards[dev->model].radio.type == CX231XX_RADIO) {
		cx231xx_vdev_init(dev, &dev->radio_dev,
				&cx231xx_radio_template, "radio");
		dev->radio_dev.ctrl_handler = &dev->radio_ctrl_handler;
		dev->radio_dev.device_caps = V4L2_CAP_RADIO | V4L2_CAP_TUNER;
		ret = video_register_device(&dev->radio_dev, VFL_TYPE_RADIO,
					    radio_nr[dev->devno]);
		if (ret < 0) {
			dev_err(dev->dev,
				"can't register radio device\n");
			return ret;
		}
		dev_info(dev->dev, "Registered radio device as %s\n",
			video_device_node_name(&dev->radio_dev));
	}

	return 0;
}
