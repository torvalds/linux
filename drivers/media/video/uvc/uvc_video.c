/*
 *      uvc_video.c  --  USB Video Class driver - Video handling
 *
 *      Copyright (C) 2005-2008
 *          Laurent Pinchart (laurent.pinchart@skynet.be)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>

#include <media/v4l2-common.h>

#include "uvcvideo.h"

/* ------------------------------------------------------------------------
 * UVC Controls
 */

static int __uvc_query_ctrl(struct uvc_device *dev, __u8 query, __u8 unit,
			__u8 intfnum, __u8 cs, void *data, __u16 size,
			int timeout)
{
	__u8 type = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	unsigned int pipe;
	int ret;

	pipe = (query & 0x80) ? usb_rcvctrlpipe(dev->udev, 0)
			      : usb_sndctrlpipe(dev->udev, 0);
	type |= (query & 0x80) ? USB_DIR_IN : USB_DIR_OUT;

	ret = usb_control_msg(dev->udev, pipe, query, type, cs << 8,
			unit << 8 | intfnum, data, size, timeout);

	if (ret != size) {
		uvc_printk(KERN_ERR, "Failed to query (%u) UVC control %u "
			"(unit %u) : %d (exp. %u).\n", query, cs, unit, ret,
			size);
		return -EIO;
	}

	return 0;
}

int uvc_query_ctrl(struct uvc_device *dev, __u8 query, __u8 unit,
			__u8 intfnum, __u8 cs, void *data, __u16 size)
{
	return __uvc_query_ctrl(dev, query, unit, intfnum, cs, data, size,
				UVC_CTRL_CONTROL_TIMEOUT);
}

static void uvc_fixup_buffer_size(struct uvc_video_device *video,
	struct uvc_streaming_control *ctrl)
{
	struct uvc_format *format;
	struct uvc_frame *frame;

	if (ctrl->bFormatIndex <= 0 ||
	    ctrl->bFormatIndex > video->streaming->nformats)
		return;

	format = &video->streaming->format[ctrl->bFormatIndex - 1];

	if (ctrl->bFrameIndex <= 0 ||
	    ctrl->bFrameIndex > format->nframes)
		return;

	frame = &format->frame[ctrl->bFrameIndex - 1];

	if (!(format->flags & UVC_FMT_FLAG_COMPRESSED) ||
	     (ctrl->dwMaxVideoFrameSize == 0 &&
	      video->dev->uvc_version < 0x0110))
		ctrl->dwMaxVideoFrameSize =
			frame->dwMaxVideoFrameBufferSize;
}

static int uvc_get_video_ctrl(struct uvc_video_device *video,
	struct uvc_streaming_control *ctrl, int probe, __u8 query)
{
	__u8 *data;
	__u16 size;
	int ret;

	size = video->dev->uvc_version >= 0x0110 ? 34 : 26;
	data = kmalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	ret = __uvc_query_ctrl(video->dev, query, 0, video->streaming->intfnum,
		probe ? VS_PROBE_CONTROL : VS_COMMIT_CONTROL, data, size,
		UVC_CTRL_STREAMING_TIMEOUT);
	if (ret < 0)
		goto out;

	ctrl->bmHint = le16_to_cpup((__le16 *)&data[0]);
	ctrl->bFormatIndex = data[2];
	ctrl->bFrameIndex = data[3];
	ctrl->dwFrameInterval = le32_to_cpup((__le32 *)&data[4]);
	ctrl->wKeyFrameRate = le16_to_cpup((__le16 *)&data[8]);
	ctrl->wPFrameRate = le16_to_cpup((__le16 *)&data[10]);
	ctrl->wCompQuality = le16_to_cpup((__le16 *)&data[12]);
	ctrl->wCompWindowSize = le16_to_cpup((__le16 *)&data[14]);
	ctrl->wDelay = le16_to_cpup((__le16 *)&data[16]);
	ctrl->dwMaxVideoFrameSize =
		le32_to_cpu(get_unaligned((__le32 *)&data[18]));
	ctrl->dwMaxPayloadTransferSize =
		le32_to_cpu(get_unaligned((__le32 *)&data[22]));

	if (size == 34) {
		ctrl->dwClockFrequency =
			le32_to_cpu(get_unaligned((__le32 *)&data[26]));
		ctrl->bmFramingInfo = data[30];
		ctrl->bPreferedVersion = data[31];
		ctrl->bMinVersion = data[32];
		ctrl->bMaxVersion = data[33];
	} else {
		ctrl->dwClockFrequency = video->dev->clock_frequency;
		ctrl->bmFramingInfo = 0;
		ctrl->bPreferedVersion = 0;
		ctrl->bMinVersion = 0;
		ctrl->bMaxVersion = 0;
	}

	/* Some broken devices return a null or wrong dwMaxVideoFrameSize.
	 * Try to get the value from the format and frame descriptor.
	 */
	uvc_fixup_buffer_size(video, ctrl);

out:
	kfree(data);
	return ret;
}

int uvc_set_video_ctrl(struct uvc_video_device *video,
	struct uvc_streaming_control *ctrl, int probe)
{
	__u8 *data;
	__u16 size;
	int ret;

	size = video->dev->uvc_version >= 0x0110 ? 34 : 26;
	data = kzalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	*(__le16 *)&data[0] = cpu_to_le16(ctrl->bmHint);
	data[2] = ctrl->bFormatIndex;
	data[3] = ctrl->bFrameIndex;
	*(__le32 *)&data[4] = cpu_to_le32(ctrl->dwFrameInterval);
	*(__le16 *)&data[8] = cpu_to_le16(ctrl->wKeyFrameRate);
	*(__le16 *)&data[10] = cpu_to_le16(ctrl->wPFrameRate);
	*(__le16 *)&data[12] = cpu_to_le16(ctrl->wCompQuality);
	*(__le16 *)&data[14] = cpu_to_le16(ctrl->wCompWindowSize);
	*(__le16 *)&data[16] = cpu_to_le16(ctrl->wDelay);
	/* Note: Some of the fields below are not required for IN devices (see
	 * UVC spec, 4.3.1.1), but we still copy them in case support for OUT
	 * devices is added in the future. */
	put_unaligned(cpu_to_le32(ctrl->dwMaxVideoFrameSize),
		(__le32 *)&data[18]);
	put_unaligned(cpu_to_le32(ctrl->dwMaxPayloadTransferSize),
		(__le32 *)&data[22]);

	if (size == 34) {
		put_unaligned(cpu_to_le32(ctrl->dwClockFrequency),
			(__le32 *)&data[26]);
		data[30] = ctrl->bmFramingInfo;
		data[31] = ctrl->bPreferedVersion;
		data[32] = ctrl->bMinVersion;
		data[33] = ctrl->bMaxVersion;
	}

	ret = __uvc_query_ctrl(video->dev, SET_CUR, 0,
		video->streaming->intfnum,
		probe ? VS_PROBE_CONTROL : VS_COMMIT_CONTROL, data, size,
		UVC_CTRL_STREAMING_TIMEOUT);

	kfree(data);
	return ret;
}

int uvc_probe_video(struct uvc_video_device *video,
	struct uvc_streaming_control *probe)
{
	struct uvc_streaming_control probe_min, probe_max;
	__u16 bandwidth;
	unsigned int i;
	int ret;

	mutex_lock(&video->streaming->mutex);

	/* Perform probing. The device should adjust the requested values
	 * according to its capabilities. However, some devices, namely the
	 * first generation UVC Logitech webcams, don't implement the Video
	 * Probe control properly, and just return the needed bandwidth. For
	 * that reason, if the needed bandwidth exceeds the maximum available
	 * bandwidth, try to lower the quality.
	 */
	if ((ret = uvc_set_video_ctrl(video, probe, 1)) < 0)
		goto done;

	/* Get the minimum and maximum values for compression settings. */
	if (!(video->dev->quirks & UVC_QUIRK_PROBE_MINMAX)) {
		ret = uvc_get_video_ctrl(video, &probe_min, 1, GET_MIN);
		if (ret < 0)
			goto done;
		ret = uvc_get_video_ctrl(video, &probe_max, 1, GET_MAX);
		if (ret < 0)
			goto done;

		probe->wCompQuality = probe_max.wCompQuality;
	}

	for (i = 0; i < 2; ++i) {
		if ((ret = uvc_set_video_ctrl(video, probe, 1)) < 0 ||
		    (ret = uvc_get_video_ctrl(video, probe, 1, GET_CUR)) < 0)
			goto done;

		if (video->streaming->intf->num_altsetting == 1)
			break;

		bandwidth = probe->dwMaxPayloadTransferSize;
		if (bandwidth <= video->streaming->maxpsize)
			break;

		if (video->dev->quirks & UVC_QUIRK_PROBE_MINMAX) {
			ret = -ENOSPC;
			goto done;
		}

		/* TODO: negotiate compression parameters */
		probe->wKeyFrameRate = probe_min.wKeyFrameRate;
		probe->wPFrameRate = probe_min.wPFrameRate;
		probe->wCompQuality = probe_max.wCompQuality;
		probe->wCompWindowSize = probe_min.wCompWindowSize;
	}

done:
	mutex_unlock(&video->streaming->mutex);
	return ret;
}

/* ------------------------------------------------------------------------
 * Video codecs
 */

/* Values for bmHeaderInfo (Video and Still Image Payload Headers, 2.4.3.3) */
#define UVC_STREAM_EOH	(1 << 7)
#define UVC_STREAM_ERR	(1 << 6)
#define UVC_STREAM_STI	(1 << 5)
#define UVC_STREAM_RES	(1 << 4)
#define UVC_STREAM_SCR	(1 << 3)
#define UVC_STREAM_PTS	(1 << 2)
#define UVC_STREAM_EOF	(1 << 1)
#define UVC_STREAM_FID	(1 << 0)

/* Video payload decoding is handled by uvc_video_decode_start(),
 * uvc_video_decode_data() and uvc_video_decode_end().
 *
 * uvc_video_decode_start is called with URB data at the start of a bulk or
 * isochronous payload. It processes header data and returns the header size
 * in bytes if successful. If an error occurs, it returns a negative error
 * code. The following error codes have special meanings.
 *
 * - EAGAIN informs the caller that the current video buffer should be marked
 *   as done, and that the function should be called again with the same data
 *   and a new video buffer. This is used when end of frame conditions can be
 *   reliably detected at the beginning of the next frame only.
 *
 * If an error other than -EAGAIN is returned, the caller will drop the current
 * payload. No call to uvc_video_decode_data and uvc_video_decode_end will be
 * made until the next payload. -ENODATA can be used to drop the current
 * payload if no other error code is appropriate.
 *
 * uvc_video_decode_data is called for every URB with URB data. It copies the
 * data to the video buffer.
 *
 * uvc_video_decode_end is called with header data at the end of a bulk or
 * isochronous payload. It performs any additional header data processing and
 * returns 0 or a negative error code if an error occured. As header data have
 * already been processed by uvc_video_decode_start, this functions isn't
 * required to perform sanity checks a second time.
 *
 * For isochronous transfers where a payload is always transfered in a single
 * URB, the three functions will be called in a row.
 *
 * To let the decoder process header data and update its internal state even
 * when no video buffer is available, uvc_video_decode_start must be prepared
 * to be called with a NULL buf parameter. uvc_video_decode_data and
 * uvc_video_decode_end will never be called with a NULL buffer.
 */
static int uvc_video_decode_start(struct uvc_video_device *video,
		struct uvc_buffer *buf, const __u8 *data, int len)
{
	__u8 fid;

	/* Sanity checks:
	 * - packet must be at least 2 bytes long
	 * - bHeaderLength value must be at least 2 bytes (see above)
	 * - bHeaderLength value can't be larger than the packet size.
	 */
	if (len < 2 || data[0] < 2 || data[0] > len)
		return -EINVAL;

	/* Skip payloads marked with the error bit ("error frames"). */
	if (data[1] & UVC_STREAM_ERR) {
		uvc_trace(UVC_TRACE_FRAME, "Dropping payload (error bit "
			  "set).\n");
		return -ENODATA;
	}

	fid = data[1] & UVC_STREAM_FID;

	/* Store the payload FID bit and return immediately when the buffer is
	 * NULL.
	 */
	if (buf == NULL) {
		video->last_fid = fid;
		return -ENODATA;
	}

	/* Synchronize to the input stream by waiting for the FID bit to be
	 * toggled when the the buffer state is not UVC_BUF_STATE_ACTIVE.
	 * queue->last_fid is initialized to -1, so the first isochronous
	 * frame will always be in sync.
	 *
	 * If the device doesn't toggle the FID bit, invert video->last_fid
	 * when the EOF bit is set to force synchronisation on the next packet.
	 */
	if (buf->state != UVC_BUF_STATE_ACTIVE) {
		if (fid == video->last_fid) {
			uvc_trace(UVC_TRACE_FRAME, "Dropping payload (out of "
				"sync).\n");
			if ((video->dev->quirks & UVC_QUIRK_STREAM_NO_FID) &&
			    (data[1] & UVC_STREAM_EOF))
				video->last_fid ^= UVC_STREAM_FID;
			return -ENODATA;
		}

		/* TODO: Handle PTS and SCR. */
		buf->state = UVC_BUF_STATE_ACTIVE;
	}

	/* Mark the buffer as done if we're at the beginning of a new frame.
	 * End of frame detection is better implemented by checking the EOF
	 * bit (FID bit toggling is delayed by one frame compared to the EOF
	 * bit), but some devices don't set the bit at end of frame (and the
	 * last payload can be lost anyway). We thus must check if the FID has
	 * been toggled.
	 *
	 * queue->last_fid is initialized to -1, so the first isochronous
	 * frame will never trigger an end of frame detection.
	 *
	 * Empty buffers (bytesused == 0) don't trigger end of frame detection
	 * as it doesn't make sense to return an empty buffer. This also
	 * avoids detecting and of frame conditions at FID toggling if the
	 * previous payload had the EOF bit set.
	 */
	if (fid != video->last_fid && buf->buf.bytesused != 0) {
		uvc_trace(UVC_TRACE_FRAME, "Frame complete (FID bit "
				"toggled).\n");
		buf->state = UVC_BUF_STATE_DONE;
		return -EAGAIN;
	}

	video->last_fid = fid;

	return data[0];
}

static void uvc_video_decode_data(struct uvc_video_device *video,
		struct uvc_buffer *buf, const __u8 *data, int len)
{
	struct uvc_video_queue *queue = &video->queue;
	unsigned int maxlen, nbytes;
	void *mem;

	if (len <= 0)
		return;

	/* Copy the video data to the buffer. */
	maxlen = buf->buf.length - buf->buf.bytesused;
	mem = queue->mem + buf->buf.m.offset + buf->buf.bytesused;
	nbytes = min((unsigned int)len, maxlen);
	memcpy(mem, data, nbytes);
	buf->buf.bytesused += nbytes;

	/* Complete the current frame if the buffer size was exceeded. */
	if (len > maxlen) {
		uvc_trace(UVC_TRACE_FRAME, "Frame complete (overflow).\n");
		buf->state = UVC_BUF_STATE_DONE;
	}
}

static void uvc_video_decode_end(struct uvc_video_device *video,
		struct uvc_buffer *buf, const __u8 *data, int len)
{
	/* Mark the buffer as done if the EOF marker is set. */
	if (data[1] & UVC_STREAM_EOF && buf->buf.bytesused != 0) {
		uvc_trace(UVC_TRACE_FRAME, "Frame complete (EOF found).\n");
		if (data[0] == len)
			uvc_trace(UVC_TRACE_FRAME, "EOF in empty payload.\n");
		buf->state = UVC_BUF_STATE_DONE;
		if (video->dev->quirks & UVC_QUIRK_STREAM_NO_FID)
			video->last_fid ^= UVC_STREAM_FID;
	}
}

/* ------------------------------------------------------------------------
 * URB handling
 */

/*
 * Completion handler for video URBs.
 */
static void uvc_video_decode_isoc(struct urb *urb,
	struct uvc_video_device *video, struct uvc_buffer *buf)
{
	u8 *mem;
	int ret, i;

	for (i = 0; i < urb->number_of_packets; ++i) {
		if (urb->iso_frame_desc[i].status < 0) {
			uvc_trace(UVC_TRACE_FRAME, "USB isochronous frame "
				"lost (%d).\n", urb->iso_frame_desc[i].status);
			continue;
		}

		/* Decode the payload header. */
		mem = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		do {
			ret = uvc_video_decode_start(video, buf, mem,
				urb->iso_frame_desc[i].actual_length);
			if (ret == -EAGAIN)
				buf = uvc_queue_next_buffer(&video->queue, buf);
		} while (ret == -EAGAIN);

		if (ret < 0)
			continue;

		/* Decode the payload data. */
		uvc_video_decode_data(video, buf, mem + ret,
			urb->iso_frame_desc[i].actual_length - ret);

		/* Process the header again. */
		uvc_video_decode_end(video, buf, mem, ret);

		if (buf->state == UVC_BUF_STATE_DONE ||
		    buf->state == UVC_BUF_STATE_ERROR)
			buf = uvc_queue_next_buffer(&video->queue, buf);
	}
}

static void uvc_video_decode_bulk(struct urb *urb,
	struct uvc_video_device *video, struct uvc_buffer *buf)
{
	u8 *mem;
	int len, ret;

	mem = urb->transfer_buffer;
	len = urb->actual_length;
	video->bulk.payload_size += len;

	/* If the URB is the first of its payload, decode and save the
	 * header.
	 */
	if (video->bulk.header_size == 0) {
		do {
			ret = uvc_video_decode_start(video, buf, mem, len);
			if (ret == -EAGAIN)
				buf = uvc_queue_next_buffer(&video->queue, buf);
		} while (ret == -EAGAIN);

		/* If an error occured skip the rest of the payload. */
		if (ret < 0 || buf == NULL) {
			video->bulk.skip_payload = 1;
			return;
		}

		video->bulk.header_size = ret;
		memcpy(video->bulk.header, mem, video->bulk.header_size);

		mem += ret;
		len -= ret;
	}

	/* The buffer queue might have been cancelled while a bulk transfer
	 * was in progress, so we can reach here with buf equal to NULL. Make
	 * sure buf is never dereferenced if NULL.
	 */

	/* Process video data. */
	if (!video->bulk.skip_payload && buf != NULL)
		uvc_video_decode_data(video, buf, mem, len);

	/* Detect the payload end by a URB smaller than the maximum size (or
	 * a payload size equal to the maximum) and process the header again.
	 */
	if (urb->actual_length < urb->transfer_buffer_length ||
	    video->bulk.payload_size >= video->bulk.max_payload_size) {
		if (!video->bulk.skip_payload && buf != NULL) {
			uvc_video_decode_end(video, buf, video->bulk.header,
				video->bulk.header_size);
			if (buf->state == UVC_BUF_STATE_DONE ||
			    buf->state == UVC_BUF_STATE_ERROR)
				buf = uvc_queue_next_buffer(&video->queue, buf);
		}

		video->bulk.header_size = 0;
		video->bulk.skip_payload = 0;
		video->bulk.payload_size = 0;
	}
}

static void uvc_video_complete(struct urb *urb)
{
	struct uvc_video_device *video = urb->context;
	struct uvc_video_queue *queue = &video->queue;
	struct uvc_buffer *buf = NULL;
	unsigned long flags;
	int ret;

	switch (urb->status) {
	case 0:
		break;

	default:
		uvc_printk(KERN_WARNING, "Non-zero status (%d) in video "
			"completion handler.\n", urb->status);

	case -ENOENT:		/* usb_kill_urb() called. */
		if (video->frozen)
			return;

	case -ECONNRESET:	/* usb_unlink_urb() called. */
	case -ESHUTDOWN:	/* The endpoint is being disabled. */
		uvc_queue_cancel(queue, urb->status == -ESHUTDOWN);
		return;
	}

	spin_lock_irqsave(&queue->irqlock, flags);
	if (!list_empty(&queue->irqqueue))
		buf = list_first_entry(&queue->irqqueue, struct uvc_buffer,
				       queue);
	spin_unlock_irqrestore(&queue->irqlock, flags);

	video->decode(urb, video, buf);

	if ((ret = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
		uvc_printk(KERN_ERR, "Failed to resubmit video URB (%d).\n",
			ret);
	}
}

/*
 * Free transfer buffers.
 */
static void uvc_free_urb_buffers(struct uvc_video_device *video)
{
	unsigned int i;

	for (i = 0; i < UVC_URBS; ++i) {
		if (video->urb_buffer[i]) {
			usb_buffer_free(video->dev->udev, video->urb_size,
				video->urb_buffer[i], video->urb_dma[i]);
			video->urb_buffer[i] = NULL;
		}
	}

	video->urb_size = 0;
}

/*
 * Allocate transfer buffers. This function can be called with buffers
 * already allocated when resuming from suspend, in which case it will
 * return without touching the buffers.
 *
 * Return 0 on success or -ENOMEM when out of memory.
 */
static int uvc_alloc_urb_buffers(struct uvc_video_device *video,
	unsigned int size)
{
	unsigned int i;

	/* Buffers are already allocated, bail out. */
	if (video->urb_size)
		return 0;

	for (i = 0; i < UVC_URBS; ++i) {
		video->urb_buffer[i] = usb_buffer_alloc(video->dev->udev,
			size, GFP_KERNEL, &video->urb_dma[i]);
		if (video->urb_buffer[i] == NULL) {
			uvc_free_urb_buffers(video);
			return -ENOMEM;
		}
	}

	video->urb_size = size;
	return 0;
}

/*
 * Uninitialize isochronous/bulk URBs and free transfer buffers.
 */
static void uvc_uninit_video(struct uvc_video_device *video, int free_buffers)
{
	struct urb *urb;
	unsigned int i;

	for (i = 0; i < UVC_URBS; ++i) {
		if ((urb = video->urb[i]) == NULL)
			continue;

		usb_kill_urb(urb);
		usb_free_urb(urb);
		video->urb[i] = NULL;
	}

	if (free_buffers)
		uvc_free_urb_buffers(video);
}

/*
 * Initialize isochronous URBs and allocate transfer buffers. The packet size
 * is given by the endpoint.
 */
static int uvc_init_video_isoc(struct uvc_video_device *video,
	struct usb_host_endpoint *ep, gfp_t gfp_flags)
{
	struct urb *urb;
	unsigned int npackets, i, j;
	__u16 psize;
	__u32 size;

	/* Compute the number of isochronous packets to allocate by dividing
	 * the maximum video frame size by the packet size. Limit the result
	 * to UVC_MAX_ISO_PACKETS.
	 */
	psize = le16_to_cpu(ep->desc.wMaxPacketSize);
	psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));

	size = video->streaming->ctrl.dwMaxVideoFrameSize;
	if (size > UVC_MAX_FRAME_SIZE)
		return -EINVAL;

	npackets = (size + psize - 1) / psize;
	if (npackets > UVC_MAX_ISO_PACKETS)
		npackets = UVC_MAX_ISO_PACKETS;

	size = npackets * psize;

	if (uvc_alloc_urb_buffers(video, size) < 0)
		return -ENOMEM;

	for (i = 0; i < UVC_URBS; ++i) {
		urb = usb_alloc_urb(npackets, gfp_flags);
		if (urb == NULL) {
			uvc_uninit_video(video, 1);
			return -ENOMEM;
		}

		urb->dev = video->dev->udev;
		urb->context = video;
		urb->pipe = usb_rcvisocpipe(video->dev->udev,
				ep->desc.bEndpointAddress);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = ep->desc.bInterval;
		urb->transfer_buffer = video->urb_buffer[i];
		urb->transfer_dma = video->urb_dma[i];
		urb->complete = uvc_video_complete;
		urb->number_of_packets = npackets;
		urb->transfer_buffer_length = size;

		for (j = 0; j < npackets; ++j) {
			urb->iso_frame_desc[j].offset = j * psize;
			urb->iso_frame_desc[j].length = psize;
		}

		video->urb[i] = urb;
	}

	return 0;
}

/*
 * Initialize bulk URBs and allocate transfer buffers. The packet size is
 * given by the endpoint.
 */
static int uvc_init_video_bulk(struct uvc_video_device *video,
	struct usb_host_endpoint *ep, gfp_t gfp_flags)
{
	struct urb *urb;
	unsigned int pipe, i;
	__u16 psize;
	__u32 size;

	/* Compute the bulk URB size. Some devices set the maximum payload
	 * size to a value too high for memory-constrained devices. We must
	 * then transfer the payload accross multiple URBs. To be consistant
	 * with isochronous mode, allocate maximum UVC_MAX_ISO_PACKETS per bulk
	 * URB.
	 */
	psize = le16_to_cpu(ep->desc.wMaxPacketSize) & 0x07ff;
	size = video->streaming->ctrl.dwMaxPayloadTransferSize;
	video->bulk.max_payload_size = size;
	if (size > psize * UVC_MAX_ISO_PACKETS)
		size = psize * UVC_MAX_ISO_PACKETS;

	if (uvc_alloc_urb_buffers(video, size) < 0)
		return -ENOMEM;

	pipe = usb_rcvbulkpipe(video->dev->udev, ep->desc.bEndpointAddress);

	for (i = 0; i < UVC_URBS; ++i) {
		urb = usb_alloc_urb(0, gfp_flags);
		if (urb == NULL) {
			uvc_uninit_video(video, 1);
			return -ENOMEM;
		}

		usb_fill_bulk_urb(urb, video->dev->udev, pipe,
			video->urb_buffer[i], size, uvc_video_complete,
			video);
		urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = video->urb_dma[i];

		video->urb[i] = urb;
	}

	return 0;
}

/*
 * Initialize isochronous/bulk URBs and allocate transfer buffers.
 */
static int uvc_init_video(struct uvc_video_device *video, gfp_t gfp_flags)
{
	struct usb_interface *intf = video->streaming->intf;
	struct usb_host_interface *alts;
	struct usb_host_endpoint *ep = NULL;
	int intfnum = video->streaming->intfnum;
	unsigned int bandwidth, psize, i;
	int ret;

	video->last_fid = -1;
	video->bulk.header_size = 0;
	video->bulk.skip_payload = 0;
	video->bulk.payload_size = 0;

	if (intf->num_altsetting > 1) {
		/* Isochronous endpoint, select the alternate setting. */
		bandwidth = video->streaming->ctrl.dwMaxPayloadTransferSize;

		if (bandwidth == 0) {
			uvc_printk(KERN_WARNING, "device %s requested null "
				"bandwidth, defaulting to lowest.\n",
				video->vdev->name);
			bandwidth = 1;
		}

		for (i = 0; i < intf->num_altsetting; ++i) {
			alts = &intf->altsetting[i];
			ep = uvc_find_endpoint(alts,
				video->streaming->header.bEndpointAddress);
			if (ep == NULL)
				continue;

			/* Check if the bandwidth is high enough. */
			psize = le16_to_cpu(ep->desc.wMaxPacketSize);
			psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
			if (psize >= bandwidth)
				break;
		}

		if (i >= intf->num_altsetting)
			return -EIO;

		if ((ret = usb_set_interface(video->dev->udev, intfnum, i)) < 0)
			return ret;

		ret = uvc_init_video_isoc(video, ep, gfp_flags);
	} else {
		/* Bulk endpoint, proceed to URB initialization. */
		ep = uvc_find_endpoint(&intf->altsetting[0],
				video->streaming->header.bEndpointAddress);
		if (ep == NULL)
			return -EIO;

		ret = uvc_init_video_bulk(video, ep, gfp_flags);
	}

	if (ret < 0)
		return ret;

	/* Submit the URBs. */
	for (i = 0; i < UVC_URBS; ++i) {
		if ((ret = usb_submit_urb(video->urb[i], gfp_flags)) < 0) {
			uvc_printk(KERN_ERR, "Failed to submit URB %u "
					"(%d).\n", i, ret);
			uvc_uninit_video(video, 1);
			return ret;
		}
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * Suspend/resume
 */

/*
 * Stop streaming without disabling the video queue.
 *
 * To let userspace applications resume without trouble, we must not touch the
 * video buffers in any way. We mark the device as frozen to make sure the URB
 * completion handler won't try to cancel the queue when we kill the URBs.
 */
int uvc_video_suspend(struct uvc_video_device *video)
{
	if (!uvc_queue_streaming(&video->queue))
		return 0;

	video->frozen = 1;
	uvc_uninit_video(video, 0);
	usb_set_interface(video->dev->udev, video->streaming->intfnum, 0);
	return 0;
}

/*
 * Reconfigure the video interface and restart streaming if it was enable
 * before suspend.
 *
 * If an error occurs, disable the video queue. This will wake all pending
 * buffers, making sure userspace applications are notified of the problem
 * instead of waiting forever.
 */
int uvc_video_resume(struct uvc_video_device *video)
{
	int ret;

	video->frozen = 0;

	if ((ret = uvc_set_video_ctrl(video, &video->streaming->ctrl, 0)) < 0) {
		uvc_queue_enable(&video->queue, 0);
		return ret;
	}

	if (!uvc_queue_streaming(&video->queue))
		return 0;

	if ((ret = uvc_init_video(video, GFP_NOIO)) < 0)
		uvc_queue_enable(&video->queue, 0);

	return ret;
}

/* ------------------------------------------------------------------------
 * Video device
 */

/*
 * Initialize the UVC video device by retrieving the default format and
 * committing it.
 *
 * Some cameras (namely the Fuji Finepix) set the format and frame
 * indexes to zero. The UVC standard doesn't clearly make this a spec
 * violation, so try to silently fix the values if possible.
 *
 * This function is called before registering the device with V4L.
 */
int uvc_video_init(struct uvc_video_device *video)
{
	struct uvc_streaming_control *probe = &video->streaming->ctrl;
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	unsigned int i;
	int ret;

	if (video->streaming->nformats == 0) {
		uvc_printk(KERN_INFO, "No supported video formats found.\n");
		return -EINVAL;
	}

	/* Alternate setting 0 should be the default, yet the XBox Live Vision
	 * Cam (and possibly other devices) crash or otherwise misbehave if
	 * they don't receive a SET_INTERFACE request before any other video
	 * control request.
	 */
	usb_set_interface(video->dev->udev, video->streaming->intfnum, 0);

	/* Some webcams don't suport GET_DEF request on the probe control. We
	 * fall back to GET_CUR if GET_DEF fails.
	 */
	if ((ret = uvc_get_video_ctrl(video, probe, 1, GET_DEF)) < 0 &&
	    (ret = uvc_get_video_ctrl(video, probe, 1, GET_CUR)) < 0)
		return ret;

	/* Check if the default format descriptor exists. Use the first
	 * available format otherwise.
	 */
	for (i = video->streaming->nformats; i > 0; --i) {
		format = &video->streaming->format[i-1];
		if (format->index == probe->bFormatIndex)
			break;
	}

	if (format->nframes == 0) {
		uvc_printk(KERN_INFO, "No frame descriptor found for the "
			"default format.\n");
		return -EINVAL;
	}

	/* Zero bFrameIndex might be correct. Stream-based formats (including
	 * MPEG-2 TS and DV) do not support frames but have a dummy frame
	 * descriptor with bFrameIndex set to zero. If the default frame
	 * descriptor is not found, use the first avalable frame.
	 */
	for (i = format->nframes; i > 0; --i) {
		frame = &format->frame[i-1];
		if (frame->bFrameIndex == probe->bFrameIndex)
			break;
	}

	/* Commit the default settings. */
	probe->bFormatIndex = format->index;
	probe->bFrameIndex = frame->bFrameIndex;
	if ((ret = uvc_set_video_ctrl(video, probe, 0)) < 0)
		return ret;

	video->streaming->cur_format = format;
	video->streaming->cur_frame = frame;
	atomic_set(&video->active, 0);

	/* Select the video decoding function */
	if (video->dev->quirks & UVC_QUIRK_BUILTIN_ISIGHT)
		video->decode = uvc_video_decode_isight;
	else if (video->streaming->intf->num_altsetting > 1)
		video->decode = uvc_video_decode_isoc;
	else
		video->decode = uvc_video_decode_bulk;

	return 0;
}

/*
 * Enable or disable the video stream.
 */
int uvc_video_enable(struct uvc_video_device *video, int enable)
{
	int ret;

	if (!enable) {
		uvc_uninit_video(video, 1);
		usb_set_interface(video->dev->udev,
			video->streaming->intfnum, 0);
		uvc_queue_enable(&video->queue, 0);
		return 0;
	}

	if ((ret = uvc_queue_enable(&video->queue, 1)) < 0)
		return ret;

	return uvc_init_video(video, GFP_KERNEL);
}

