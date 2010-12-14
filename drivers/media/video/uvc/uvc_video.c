/*
 *      uvc_video.c  --  USB Video Class driver - Video handling
 *
 *      Copyright (C) 2005-2010
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
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

	pipe = (query & 0x80) ? usb_rcvctrlpipe(dev->udev, 0)
			      : usb_sndctrlpipe(dev->udev, 0);
	type |= (query & 0x80) ? USB_DIR_IN : USB_DIR_OUT;

	return usb_control_msg(dev->udev, pipe, query, type, cs << 8,
			unit << 8 | intfnum, data, size, timeout);
}

static const char *uvc_query_name(__u8 query)
{
	switch (query) {
	case UVC_SET_CUR:
		return "SET_CUR";
	case UVC_GET_CUR:
		return "GET_CUR";
	case UVC_GET_MIN:
		return "GET_MIN";
	case UVC_GET_MAX:
		return "GET_MAX";
	case UVC_GET_RES:
		return "GET_RES";
	case UVC_GET_LEN:
		return "GET_LEN";
	case UVC_GET_INFO:
		return "GET_INFO";
	case UVC_GET_DEF:
		return "GET_DEF";
	default:
		return "<invalid>";
	}
}

int uvc_query_ctrl(struct uvc_device *dev, __u8 query, __u8 unit,
			__u8 intfnum, __u8 cs, void *data, __u16 size)
{
	int ret;

	ret = __uvc_query_ctrl(dev, query, unit, intfnum, cs, data, size,
				UVC_CTRL_CONTROL_TIMEOUT);
	if (ret != size) {
		uvc_printk(KERN_ERR, "Failed to query (%s) UVC control %u on "
			"unit %u: %d (exp. %u).\n", uvc_query_name(query), cs,
			unit, ret, size);
		return -EIO;
	}

	return 0;
}

static void uvc_fixup_video_ctrl(struct uvc_streaming *stream,
	struct uvc_streaming_control *ctrl)
{
	struct uvc_format *format;
	struct uvc_frame *frame = NULL;
	unsigned int i;

	if (ctrl->bFormatIndex <= 0 ||
	    ctrl->bFormatIndex > stream->nformats)
		return;

	format = &stream->format[ctrl->bFormatIndex - 1];

	for (i = 0; i < format->nframes; ++i) {
		if (format->frame[i].bFrameIndex == ctrl->bFrameIndex) {
			frame = &format->frame[i];
			break;
		}
	}

	if (frame == NULL)
		return;

	if (!(format->flags & UVC_FMT_FLAG_COMPRESSED) ||
	     (ctrl->dwMaxVideoFrameSize == 0 &&
	      stream->dev->uvc_version < 0x0110))
		ctrl->dwMaxVideoFrameSize =
			frame->dwMaxVideoFrameBufferSize;

	if (!(format->flags & UVC_FMT_FLAG_COMPRESSED) &&
	    stream->dev->quirks & UVC_QUIRK_FIX_BANDWIDTH &&
	    stream->intf->num_altsetting > 1) {
		u32 interval;
		u32 bandwidth;

		interval = (ctrl->dwFrameInterval > 100000)
			 ? ctrl->dwFrameInterval
			 : frame->dwFrameInterval[0];

		/* Compute a bandwidth estimation by multiplying the frame
		 * size by the number of video frames per second, divide the
		 * result by the number of USB frames (or micro-frames for
		 * high-speed devices) per second and add the UVC header size
		 * (assumed to be 12 bytes long).
		 */
		bandwidth = frame->wWidth * frame->wHeight / 8 * format->bpp;
		bandwidth *= 10000000 / interval + 1;
		bandwidth /= 1000;
		if (stream->dev->udev->speed == USB_SPEED_HIGH)
			bandwidth /= 8;
		bandwidth += 12;

		/* The bandwidth estimate is too low for many cameras. Don't use
		 * maximum packet sizes lower than 1024 bytes to try and work
		 * around the problem. According to measurements done on two
		 * different camera models, the value is high enough to get most
		 * resolutions working while not preventing two simultaneous
		 * VGA streams at 15 fps.
		 */
		bandwidth = max_t(u32, bandwidth, 1024);

		ctrl->dwMaxPayloadTransferSize = bandwidth;
	}
}

static int uvc_get_video_ctrl(struct uvc_streaming *stream,
	struct uvc_streaming_control *ctrl, int probe, __u8 query)
{
	__u8 *data;
	__u16 size;
	int ret;

	size = stream->dev->uvc_version >= 0x0110 ? 34 : 26;
	if ((stream->dev->quirks & UVC_QUIRK_PROBE_DEF) &&
			query == UVC_GET_DEF)
		return -EIO;

	data = kmalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	ret = __uvc_query_ctrl(stream->dev, query, 0, stream->intfnum,
		probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL, data,
		size, uvc_timeout_param);

	if ((query == UVC_GET_MIN || query == UVC_GET_MAX) && ret == 2) {
		/* Some cameras, mostly based on Bison Electronics chipsets,
		 * answer a GET_MIN or GET_MAX request with the wCompQuality
		 * field only.
		 */
		uvc_warn_once(stream->dev, UVC_WARN_MINMAX, "UVC non "
			"compliance - GET_MIN/MAX(PROBE) incorrectly "
			"supported. Enabling workaround.\n");
		memset(ctrl, 0, sizeof *ctrl);
		ctrl->wCompQuality = le16_to_cpup((__le16 *)data);
		ret = 0;
		goto out;
	} else if (query == UVC_GET_DEF && probe == 1 && ret != size) {
		/* Many cameras don't support the GET_DEF request on their
		 * video probe control. Warn once and return, the caller will
		 * fall back to GET_CUR.
		 */
		uvc_warn_once(stream->dev, UVC_WARN_PROBE_DEF, "UVC non "
			"compliance - GET_DEF(PROBE) not supported. "
			"Enabling workaround.\n");
		ret = -EIO;
		goto out;
	} else if (ret != size) {
		uvc_printk(KERN_ERR, "Failed to query (%u) UVC %s control : "
			"%d (exp. %u).\n", query, probe ? "probe" : "commit",
			ret, size);
		ret = -EIO;
		goto out;
	}

	ctrl->bmHint = le16_to_cpup((__le16 *)&data[0]);
	ctrl->bFormatIndex = data[2];
	ctrl->bFrameIndex = data[3];
	ctrl->dwFrameInterval = le32_to_cpup((__le32 *)&data[4]);
	ctrl->wKeyFrameRate = le16_to_cpup((__le16 *)&data[8]);
	ctrl->wPFrameRate = le16_to_cpup((__le16 *)&data[10]);
	ctrl->wCompQuality = le16_to_cpup((__le16 *)&data[12]);
	ctrl->wCompWindowSize = le16_to_cpup((__le16 *)&data[14]);
	ctrl->wDelay = le16_to_cpup((__le16 *)&data[16]);
	ctrl->dwMaxVideoFrameSize = get_unaligned_le32(&data[18]);
	ctrl->dwMaxPayloadTransferSize = get_unaligned_le32(&data[22]);

	if (size == 34) {
		ctrl->dwClockFrequency = get_unaligned_le32(&data[26]);
		ctrl->bmFramingInfo = data[30];
		ctrl->bPreferedVersion = data[31];
		ctrl->bMinVersion = data[32];
		ctrl->bMaxVersion = data[33];
	} else {
		ctrl->dwClockFrequency = stream->dev->clock_frequency;
		ctrl->bmFramingInfo = 0;
		ctrl->bPreferedVersion = 0;
		ctrl->bMinVersion = 0;
		ctrl->bMaxVersion = 0;
	}

	/* Some broken devices return null or wrong dwMaxVideoFrameSize and
	 * dwMaxPayloadTransferSize fields. Try to get the value from the
	 * format and frame descriptors.
	 */
	uvc_fixup_video_ctrl(stream, ctrl);
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int uvc_set_video_ctrl(struct uvc_streaming *stream,
	struct uvc_streaming_control *ctrl, int probe)
{
	__u8 *data;
	__u16 size;
	int ret;

	size = stream->dev->uvc_version >= 0x0110 ? 34 : 26;
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
	put_unaligned_le32(ctrl->dwMaxVideoFrameSize, &data[18]);
	put_unaligned_le32(ctrl->dwMaxPayloadTransferSize, &data[22]);

	if (size == 34) {
		put_unaligned_le32(ctrl->dwClockFrequency, &data[26]);
		data[30] = ctrl->bmFramingInfo;
		data[31] = ctrl->bPreferedVersion;
		data[32] = ctrl->bMinVersion;
		data[33] = ctrl->bMaxVersion;
	}

	ret = __uvc_query_ctrl(stream->dev, UVC_SET_CUR, 0, stream->intfnum,
		probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL, data,
		size, uvc_timeout_param);
	if (ret != size) {
		uvc_printk(KERN_ERR, "Failed to set UVC %s control : "
			"%d (exp. %u).\n", probe ? "probe" : "commit",
			ret, size);
		ret = -EIO;
	}

	kfree(data);
	return ret;
}

int uvc_probe_video(struct uvc_streaming *stream,
	struct uvc_streaming_control *probe)
{
	struct uvc_streaming_control probe_min, probe_max;
	__u16 bandwidth;
	unsigned int i;
	int ret;

	mutex_lock(&stream->mutex);

	/* Perform probing. The device should adjust the requested values
	 * according to its capabilities. However, some devices, namely the
	 * first generation UVC Logitech webcams, don't implement the Video
	 * Probe control properly, and just return the needed bandwidth. For
	 * that reason, if the needed bandwidth exceeds the maximum available
	 * bandwidth, try to lower the quality.
	 */
	ret = uvc_set_video_ctrl(stream, probe, 1);
	if (ret < 0)
		goto done;

	/* Get the minimum and maximum values for compression settings. */
	if (!(stream->dev->quirks & UVC_QUIRK_PROBE_MINMAX)) {
		ret = uvc_get_video_ctrl(stream, &probe_min, 1, UVC_GET_MIN);
		if (ret < 0)
			goto done;
		ret = uvc_get_video_ctrl(stream, &probe_max, 1, UVC_GET_MAX);
		if (ret < 0)
			goto done;

		probe->wCompQuality = probe_max.wCompQuality;
	}

	for (i = 0; i < 2; ++i) {
		ret = uvc_set_video_ctrl(stream, probe, 1);
		if (ret < 0)
			goto done;
		ret = uvc_get_video_ctrl(stream, probe, 1, UVC_GET_CUR);
		if (ret < 0)
			goto done;

		if (stream->intf->num_altsetting == 1)
			break;

		bandwidth = probe->dwMaxPayloadTransferSize;
		if (bandwidth <= stream->maxpsize)
			break;

		if (stream->dev->quirks & UVC_QUIRK_PROBE_MINMAX) {
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
	mutex_unlock(&stream->mutex);
	return ret;
}

int uvc_commit_video(struct uvc_streaming *stream,
	struct uvc_streaming_control *probe)
{
	return uvc_set_video_ctrl(stream, probe, 0);
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
static int uvc_video_decode_start(struct uvc_streaming *stream,
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

	/* Increase the sequence number regardless of any buffer states, so
	 * that discontinuous sequence numbers always indicate lost frames.
	 */
	if (stream->last_fid != fid)
		stream->sequence++;

	/* Store the payload FID bit and return immediately when the buffer is
	 * NULL.
	 */
	if (buf == NULL) {
		stream->last_fid = fid;
		return -ENODATA;
	}

	/* Synchronize to the input stream by waiting for the FID bit to be
	 * toggled when the the buffer state is not UVC_BUF_STATE_ACTIVE.
	 * stream->last_fid is initialized to -1, so the first isochronous
	 * frame will always be in sync.
	 *
	 * If the device doesn't toggle the FID bit, invert stream->last_fid
	 * when the EOF bit is set to force synchronisation on the next packet.
	 */
	if (buf->state != UVC_BUF_STATE_ACTIVE) {
		struct timespec ts;

		if (fid == stream->last_fid) {
			uvc_trace(UVC_TRACE_FRAME, "Dropping payload (out of "
				"sync).\n");
			if ((stream->dev->quirks & UVC_QUIRK_STREAM_NO_FID) &&
			    (data[1] & UVC_STREAM_EOF))
				stream->last_fid ^= UVC_STREAM_FID;
			return -ENODATA;
		}

		if (uvc_clock_param == CLOCK_MONOTONIC)
			ktime_get_ts(&ts);
		else
			ktime_get_real_ts(&ts);

		buf->buf.sequence = stream->sequence;
		buf->buf.timestamp.tv_sec = ts.tv_sec;
		buf->buf.timestamp.tv_usec = ts.tv_nsec / NSEC_PER_USEC;

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
	 * stream->last_fid is initialized to -1, so the first isochronous
	 * frame will never trigger an end of frame detection.
	 *
	 * Empty buffers (bytesused == 0) don't trigger end of frame detection
	 * as it doesn't make sense to return an empty buffer. This also
	 * avoids detecting end of frame conditions at FID toggling if the
	 * previous payload had the EOF bit set.
	 */
	if (fid != stream->last_fid && buf->buf.bytesused != 0) {
		uvc_trace(UVC_TRACE_FRAME, "Frame complete (FID bit "
				"toggled).\n");
		buf->state = UVC_BUF_STATE_READY;
		return -EAGAIN;
	}

	stream->last_fid = fid;

	return data[0];
}

static void uvc_video_decode_data(struct uvc_streaming *stream,
		struct uvc_buffer *buf, const __u8 *data, int len)
{
	struct uvc_video_queue *queue = &stream->queue;
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
		buf->state = UVC_BUF_STATE_READY;
	}
}

static void uvc_video_decode_end(struct uvc_streaming *stream,
		struct uvc_buffer *buf, const __u8 *data, int len)
{
	/* Mark the buffer as done if the EOF marker is set. */
	if (data[1] & UVC_STREAM_EOF && buf->buf.bytesused != 0) {
		uvc_trace(UVC_TRACE_FRAME, "Frame complete (EOF found).\n");
		if (data[0] == len)
			uvc_trace(UVC_TRACE_FRAME, "EOF in empty payload.\n");
		buf->state = UVC_BUF_STATE_READY;
		if (stream->dev->quirks & UVC_QUIRK_STREAM_NO_FID)
			stream->last_fid ^= UVC_STREAM_FID;
	}
}

/* Video payload encoding is handled by uvc_video_encode_header() and
 * uvc_video_encode_data(). Only bulk transfers are currently supported.
 *
 * uvc_video_encode_header is called at the start of a payload. It adds header
 * data to the transfer buffer and returns the header size. As the only known
 * UVC output device transfers a whole frame in a single payload, the EOF bit
 * is always set in the header.
 *
 * uvc_video_encode_data is called for every URB and copies the data from the
 * video buffer to the transfer buffer.
 */
static int uvc_video_encode_header(struct uvc_streaming *stream,
		struct uvc_buffer *buf, __u8 *data, int len)
{
	data[0] = 2;	/* Header length */
	data[1] = UVC_STREAM_EOH | UVC_STREAM_EOF
		| (stream->last_fid & UVC_STREAM_FID);
	return 2;
}

static int uvc_video_encode_data(struct uvc_streaming *stream,
		struct uvc_buffer *buf, __u8 *data, int len)
{
	struct uvc_video_queue *queue = &stream->queue;
	unsigned int nbytes;
	void *mem;

	/* Copy video data to the URB buffer. */
	mem = queue->mem + buf->buf.m.offset + queue->buf_used;
	nbytes = min((unsigned int)len, buf->buf.bytesused - queue->buf_used);
	nbytes = min(stream->bulk.max_payload_size - stream->bulk.payload_size,
			nbytes);
	memcpy(data, mem, nbytes);

	queue->buf_used += nbytes;

	return nbytes;
}

/* ------------------------------------------------------------------------
 * URB handling
 */

/*
 * Completion handler for video URBs.
 */
static void uvc_video_decode_isoc(struct urb *urb, struct uvc_streaming *stream,
	struct uvc_buffer *buf)
{
	u8 *mem;
	int ret, i;

	for (i = 0; i < urb->number_of_packets; ++i) {
		if (urb->iso_frame_desc[i].status < 0) {
			uvc_trace(UVC_TRACE_FRAME, "USB isochronous frame "
				"lost (%d).\n", urb->iso_frame_desc[i].status);
			/* Mark the buffer as faulty. */
			if (buf != NULL)
				buf->error = 1;
			continue;
		}

		/* Decode the payload header. */
		mem = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		do {
			ret = uvc_video_decode_start(stream, buf, mem,
				urb->iso_frame_desc[i].actual_length);
			if (ret == -EAGAIN)
				buf = uvc_queue_next_buffer(&stream->queue,
							    buf);
		} while (ret == -EAGAIN);

		if (ret < 0)
			continue;

		/* Decode the payload data. */
		uvc_video_decode_data(stream, buf, mem + ret,
			urb->iso_frame_desc[i].actual_length - ret);

		/* Process the header again. */
		uvc_video_decode_end(stream, buf, mem,
			urb->iso_frame_desc[i].actual_length);

		if (buf->state == UVC_BUF_STATE_READY) {
			if (buf->buf.length != buf->buf.bytesused &&
			    !(stream->cur_format->flags &
			      UVC_FMT_FLAG_COMPRESSED))
				buf->error = 1;

			buf = uvc_queue_next_buffer(&stream->queue, buf);
		}
	}
}

static void uvc_video_decode_bulk(struct urb *urb, struct uvc_streaming *stream,
	struct uvc_buffer *buf)
{
	u8 *mem;
	int len, ret;

	if (urb->actual_length == 0)
		return;

	mem = urb->transfer_buffer;
	len = urb->actual_length;
	stream->bulk.payload_size += len;

	/* If the URB is the first of its payload, decode and save the
	 * header.
	 */
	if (stream->bulk.header_size == 0 && !stream->bulk.skip_payload) {
		do {
			ret = uvc_video_decode_start(stream, buf, mem, len);
			if (ret == -EAGAIN)
				buf = uvc_queue_next_buffer(&stream->queue,
							    buf);
		} while (ret == -EAGAIN);

		/* If an error occured skip the rest of the payload. */
		if (ret < 0 || buf == NULL) {
			stream->bulk.skip_payload = 1;
		} else {
			memcpy(stream->bulk.header, mem, ret);
			stream->bulk.header_size = ret;

			mem += ret;
			len -= ret;
		}
	}

	/* The buffer queue might have been cancelled while a bulk transfer
	 * was in progress, so we can reach here with buf equal to NULL. Make
	 * sure buf is never dereferenced if NULL.
	 */

	/* Process video data. */
	if (!stream->bulk.skip_payload && buf != NULL)
		uvc_video_decode_data(stream, buf, mem, len);

	/* Detect the payload end by a URB smaller than the maximum size (or
	 * a payload size equal to the maximum) and process the header again.
	 */
	if (urb->actual_length < urb->transfer_buffer_length ||
	    stream->bulk.payload_size >= stream->bulk.max_payload_size) {
		if (!stream->bulk.skip_payload && buf != NULL) {
			uvc_video_decode_end(stream, buf, stream->bulk.header,
				stream->bulk.payload_size);
			if (buf->state == UVC_BUF_STATE_READY)
				buf = uvc_queue_next_buffer(&stream->queue,
							    buf);
		}

		stream->bulk.header_size = 0;
		stream->bulk.skip_payload = 0;
		stream->bulk.payload_size = 0;
	}
}

static void uvc_video_encode_bulk(struct urb *urb, struct uvc_streaming *stream,
	struct uvc_buffer *buf)
{
	u8 *mem = urb->transfer_buffer;
	int len = stream->urb_size, ret;

	if (buf == NULL) {
		urb->transfer_buffer_length = 0;
		return;
	}

	/* If the URB is the first of its payload, add the header. */
	if (stream->bulk.header_size == 0) {
		ret = uvc_video_encode_header(stream, buf, mem, len);
		stream->bulk.header_size = ret;
		stream->bulk.payload_size += ret;
		mem += ret;
		len -= ret;
	}

	/* Process video data. */
	ret = uvc_video_encode_data(stream, buf, mem, len);

	stream->bulk.payload_size += ret;
	len -= ret;

	if (buf->buf.bytesused == stream->queue.buf_used ||
	    stream->bulk.payload_size == stream->bulk.max_payload_size) {
		if (buf->buf.bytesused == stream->queue.buf_used) {
			stream->queue.buf_used = 0;
			buf->state = UVC_BUF_STATE_READY;
			buf->buf.sequence = ++stream->sequence;
			uvc_queue_next_buffer(&stream->queue, buf);
			stream->last_fid ^= UVC_STREAM_FID;
		}

		stream->bulk.header_size = 0;
		stream->bulk.payload_size = 0;
	}

	urb->transfer_buffer_length = stream->urb_size - len;
}

static void uvc_video_complete(struct urb *urb)
{
	struct uvc_streaming *stream = urb->context;
	struct uvc_video_queue *queue = &stream->queue;
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
		if (stream->frozen)
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

	stream->decode(urb, stream, buf);

	if ((ret = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
		uvc_printk(KERN_ERR, "Failed to resubmit video URB (%d).\n",
			ret);
	}
}

/*
 * Free transfer buffers.
 */
static void uvc_free_urb_buffers(struct uvc_streaming *stream)
{
	unsigned int i;

	for (i = 0; i < UVC_URBS; ++i) {
		if (stream->urb_buffer[i]) {
			usb_free_coherent(stream->dev->udev, stream->urb_size,
				stream->urb_buffer[i], stream->urb_dma[i]);
			stream->urb_buffer[i] = NULL;
		}
	}

	stream->urb_size = 0;
}

/*
 * Allocate transfer buffers. This function can be called with buffers
 * already allocated when resuming from suspend, in which case it will
 * return without touching the buffers.
 *
 * Limit the buffer size to UVC_MAX_PACKETS bulk/isochronous packets. If the
 * system is too low on memory try successively smaller numbers of packets
 * until allocation succeeds.
 *
 * Return the number of allocated packets on success or 0 when out of memory.
 */
static int uvc_alloc_urb_buffers(struct uvc_streaming *stream,
	unsigned int size, unsigned int psize, gfp_t gfp_flags)
{
	unsigned int npackets;
	unsigned int i;

	/* Buffers are already allocated, bail out. */
	if (stream->urb_size)
		return stream->urb_size / psize;

	/* Compute the number of packets. Bulk endpoints might transfer UVC
	 * payloads accross multiple URBs.
	 */
	npackets = DIV_ROUND_UP(size, psize);
	if (npackets > UVC_MAX_PACKETS)
		npackets = UVC_MAX_PACKETS;

	/* Retry allocations until one succeed. */
	for (; npackets > 1; npackets /= 2) {
		for (i = 0; i < UVC_URBS; ++i) {
			stream->urb_size = psize * npackets;
			stream->urb_buffer[i] = usb_alloc_coherent(
				stream->dev->udev, stream->urb_size,
				gfp_flags | __GFP_NOWARN, &stream->urb_dma[i]);
			if (!stream->urb_buffer[i]) {
				uvc_free_urb_buffers(stream);
				break;
			}
		}

		if (i == UVC_URBS) {
			uvc_trace(UVC_TRACE_VIDEO, "Allocated %u URB buffers "
				"of %ux%u bytes each.\n", UVC_URBS, npackets,
				psize);
			return npackets;
		}
	}

	uvc_trace(UVC_TRACE_VIDEO, "Failed to allocate URB buffers (%u bytes "
		"per packet).\n", psize);
	return 0;
}

/*
 * Uninitialize isochronous/bulk URBs and free transfer buffers.
 */
static void uvc_uninit_video(struct uvc_streaming *stream, int free_buffers)
{
	struct urb *urb;
	unsigned int i;

	for (i = 0; i < UVC_URBS; ++i) {
		urb = stream->urb[i];
		if (urb == NULL)
			continue;

		usb_kill_urb(urb);
		usb_free_urb(urb);
		stream->urb[i] = NULL;
	}

	if (free_buffers)
		uvc_free_urb_buffers(stream);
}

/*
 * Initialize isochronous URBs and allocate transfer buffers. The packet size
 * is given by the endpoint.
 */
static int uvc_init_video_isoc(struct uvc_streaming *stream,
	struct usb_host_endpoint *ep, gfp_t gfp_flags)
{
	struct urb *urb;
	unsigned int npackets, i, j;
	u16 psize;
	u32 size;

	psize = le16_to_cpu(ep->desc.wMaxPacketSize);
	psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
	size = stream->ctrl.dwMaxVideoFrameSize;

	npackets = uvc_alloc_urb_buffers(stream, size, psize, gfp_flags);
	if (npackets == 0)
		return -ENOMEM;

	size = npackets * psize;

	for (i = 0; i < UVC_URBS; ++i) {
		urb = usb_alloc_urb(npackets, gfp_flags);
		if (urb == NULL) {
			uvc_uninit_video(stream, 1);
			return -ENOMEM;
		}

		urb->dev = stream->dev->udev;
		urb->context = stream;
		urb->pipe = usb_rcvisocpipe(stream->dev->udev,
				ep->desc.bEndpointAddress);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = ep->desc.bInterval;
		urb->transfer_buffer = stream->urb_buffer[i];
		urb->transfer_dma = stream->urb_dma[i];
		urb->complete = uvc_video_complete;
		urb->number_of_packets = npackets;
		urb->transfer_buffer_length = size;

		for (j = 0; j < npackets; ++j) {
			urb->iso_frame_desc[j].offset = j * psize;
			urb->iso_frame_desc[j].length = psize;
		}

		stream->urb[i] = urb;
	}

	return 0;
}

/*
 * Initialize bulk URBs and allocate transfer buffers. The packet size is
 * given by the endpoint.
 */
static int uvc_init_video_bulk(struct uvc_streaming *stream,
	struct usb_host_endpoint *ep, gfp_t gfp_flags)
{
	struct urb *urb;
	unsigned int npackets, pipe, i;
	u16 psize;
	u32 size;

	psize = le16_to_cpu(ep->desc.wMaxPacketSize) & 0x07ff;
	size = stream->ctrl.dwMaxPayloadTransferSize;
	stream->bulk.max_payload_size = size;

	npackets = uvc_alloc_urb_buffers(stream, size, psize, gfp_flags);
	if (npackets == 0)
		return -ENOMEM;

	size = npackets * psize;

	if (usb_endpoint_dir_in(&ep->desc))
		pipe = usb_rcvbulkpipe(stream->dev->udev,
				       ep->desc.bEndpointAddress);
	else
		pipe = usb_sndbulkpipe(stream->dev->udev,
				       ep->desc.bEndpointAddress);

	if (stream->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		size = 0;

	for (i = 0; i < UVC_URBS; ++i) {
		urb = usb_alloc_urb(0, gfp_flags);
		if (urb == NULL) {
			uvc_uninit_video(stream, 1);
			return -ENOMEM;
		}

		usb_fill_bulk_urb(urb, stream->dev->udev, pipe,
			stream->urb_buffer[i], size, uvc_video_complete,
			stream);
		urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = stream->urb_dma[i];

		stream->urb[i] = urb;
	}

	return 0;
}

/*
 * Initialize isochronous/bulk URBs and allocate transfer buffers.
 */
static int uvc_init_video(struct uvc_streaming *stream, gfp_t gfp_flags)
{
	struct usb_interface *intf = stream->intf;
	struct usb_host_endpoint *ep;
	unsigned int i;
	int ret;

	stream->sequence = -1;
	stream->last_fid = -1;
	stream->bulk.header_size = 0;
	stream->bulk.skip_payload = 0;
	stream->bulk.payload_size = 0;

	if (intf->num_altsetting > 1) {
		struct usb_host_endpoint *best_ep = NULL;
		unsigned int best_psize = 3 * 1024;
		unsigned int bandwidth;
		unsigned int uninitialized_var(altsetting);
		int intfnum = stream->intfnum;

		/* Isochronous endpoint, select the alternate setting. */
		bandwidth = stream->ctrl.dwMaxPayloadTransferSize;

		if (bandwidth == 0) {
			uvc_trace(UVC_TRACE_VIDEO, "Device requested null "
				"bandwidth, defaulting to lowest.\n");
			bandwidth = 1;
		} else {
			uvc_trace(UVC_TRACE_VIDEO, "Device requested %u "
				"B/frame bandwidth.\n", bandwidth);
		}

		for (i = 0; i < intf->num_altsetting; ++i) {
			struct usb_host_interface *alts;
			unsigned int psize;

			alts = &intf->altsetting[i];
			ep = uvc_find_endpoint(alts,
				stream->header.bEndpointAddress);
			if (ep == NULL)
				continue;

			/* Check if the bandwidth is high enough. */
			psize = le16_to_cpu(ep->desc.wMaxPacketSize);
			psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
			if (psize >= bandwidth && psize <= best_psize) {
				altsetting = i;
				best_psize = psize;
				best_ep = ep;
			}
		}

		if (best_ep == NULL) {
			uvc_trace(UVC_TRACE_VIDEO, "No fast enough alt setting "
				"for requested bandwidth.\n");
			return -EIO;
		}

		uvc_trace(UVC_TRACE_VIDEO, "Selecting alternate setting %u "
			"(%u B/frame bandwidth).\n", altsetting, best_psize);

		ret = usb_set_interface(stream->dev->udev, intfnum, altsetting);
		if (ret < 0)
			return ret;

		ret = uvc_init_video_isoc(stream, best_ep, gfp_flags);
	} else {
		/* Bulk endpoint, proceed to URB initialization. */
		ep = uvc_find_endpoint(&intf->altsetting[0],
				stream->header.bEndpointAddress);
		if (ep == NULL)
			return -EIO;

		ret = uvc_init_video_bulk(stream, ep, gfp_flags);
	}

	if (ret < 0)
		return ret;

	/* Submit the URBs. */
	for (i = 0; i < UVC_URBS; ++i) {
		ret = usb_submit_urb(stream->urb[i], gfp_flags);
		if (ret < 0) {
			uvc_printk(KERN_ERR, "Failed to submit URB %u "
					"(%d).\n", i, ret);
			uvc_uninit_video(stream, 1);
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
int uvc_video_suspend(struct uvc_streaming *stream)
{
	if (!uvc_queue_streaming(&stream->queue))
		return 0;

	stream->frozen = 1;
	uvc_uninit_video(stream, 0);
	usb_set_interface(stream->dev->udev, stream->intfnum, 0);
	return 0;
}

/*
 * Reconfigure the video interface and restart streaming if it was enabled
 * before suspend.
 *
 * If an error occurs, disable the video queue. This will wake all pending
 * buffers, making sure userspace applications are notified of the problem
 * instead of waiting forever.
 */
int uvc_video_resume(struct uvc_streaming *stream)
{
	int ret;

	stream->frozen = 0;

	ret = uvc_commit_video(stream, &stream->ctrl);
	if (ret < 0) {
		uvc_queue_enable(&stream->queue, 0);
		return ret;
	}

	if (!uvc_queue_streaming(&stream->queue))
		return 0;

	ret = uvc_init_video(stream, GFP_NOIO);
	if (ret < 0)
		uvc_queue_enable(&stream->queue, 0);

	return ret;
}

/* ------------------------------------------------------------------------
 * Video device
 */

/*
 * Initialize the UVC video device by switching to alternate setting 0 and
 * retrieve the default format.
 *
 * Some cameras (namely the Fuji Finepix) set the format and frame
 * indexes to zero. The UVC standard doesn't clearly make this a spec
 * violation, so try to silently fix the values if possible.
 *
 * This function is called before registering the device with V4L.
 */
int uvc_video_init(struct uvc_streaming *stream)
{
	struct uvc_streaming_control *probe = &stream->ctrl;
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	unsigned int i;
	int ret;

	if (stream->nformats == 0) {
		uvc_printk(KERN_INFO, "No supported video formats found.\n");
		return -EINVAL;
	}

	atomic_set(&stream->active, 0);

	/* Initialize the video buffers queue. */
	uvc_queue_init(&stream->queue, stream->type, !uvc_no_drop_param);

	/* Alternate setting 0 should be the default, yet the XBox Live Vision
	 * Cam (and possibly other devices) crash or otherwise misbehave if
	 * they don't receive a SET_INTERFACE request before any other video
	 * control request.
	 */
	usb_set_interface(stream->dev->udev, stream->intfnum, 0);

	/* Set the streaming probe control with default streaming parameters
	 * retrieved from the device. Webcams that don't suport GET_DEF
	 * requests on the probe control will just keep their current streaming
	 * parameters.
	 */
	if (uvc_get_video_ctrl(stream, probe, 1, UVC_GET_DEF) == 0)
		uvc_set_video_ctrl(stream, probe, 1);

	/* Initialize the streaming parameters with the probe control current
	 * value. This makes sure SET_CUR requests on the streaming commit
	 * control will always use values retrieved from a successful GET_CUR
	 * request on the probe control, as required by the UVC specification.
	 */
	ret = uvc_get_video_ctrl(stream, probe, 1, UVC_GET_CUR);
	if (ret < 0)
		return ret;

	/* Check if the default format descriptor exists. Use the first
	 * available format otherwise.
	 */
	for (i = stream->nformats; i > 0; --i) {
		format = &stream->format[i-1];
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
	 * descriptor is not found, use the first available frame.
	 */
	for (i = format->nframes; i > 0; --i) {
		frame = &format->frame[i-1];
		if (frame->bFrameIndex == probe->bFrameIndex)
			break;
	}

	probe->bFormatIndex = format->index;
	probe->bFrameIndex = frame->bFrameIndex;

	stream->cur_format = format;
	stream->cur_frame = frame;

	/* Select the video decoding function */
	if (stream->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (stream->dev->quirks & UVC_QUIRK_BUILTIN_ISIGHT)
			stream->decode = uvc_video_decode_isight;
		else if (stream->intf->num_altsetting > 1)
			stream->decode = uvc_video_decode_isoc;
		else
			stream->decode = uvc_video_decode_bulk;
	} else {
		if (stream->intf->num_altsetting == 1)
			stream->decode = uvc_video_encode_bulk;
		else {
			uvc_printk(KERN_INFO, "Isochronous endpoints are not "
				"supported for video output devices.\n");
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Enable or disable the video stream.
 */
int uvc_video_enable(struct uvc_streaming *stream, int enable)
{
	int ret;

	if (!enable) {
		uvc_uninit_video(stream, 1);
		usb_set_interface(stream->dev->udev, stream->intfnum, 0);
		uvc_queue_enable(&stream->queue, 0);
		return 0;
	}

	ret = uvc_queue_enable(&stream->queue, 1);
	if (ret < 0)
		return ret;

	/* Commit the streaming parameters. */
	ret = uvc_commit_video(stream, &stream->ctrl);
	if (ret < 0)
		return ret;

	return uvc_init_video(stream, GFP_KERNEL);
}

