/*
 *      uvc_isight.c  --  USB Video Class driver - iSight support
 *
 *	Copyright (C) 2006-2007
 *		Ivan N. Zlatev <contact@i-nz.net>
 *	Copyright (C) 2008-2009
 *		Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include "uvcvideo.h"

/* Built-in iSight webcams implements most of UVC 1.0 except a
 * different packet format. Instead of sending a header at the
 * beginning of each isochronous transfer payload, the webcam sends a
 * single header per image (on its own in a packet), followed by
 * packets containing data only.
 *
 * Offset   Size (bytes)	Description
 * ------------------------------------------------------------------
 * 0x00	1	Header length
 * 0x01	1	Flags (UVC-compliant)
 * 0x02	4	Always equal to '11223344'
 * 0x06	8	Always equal to 'deadbeefdeadface'
 * 0x0e	16	Unknown
 *
 * The header can be prefixed by an optional, unknown-purpose byte.
 */

static int isight_decode(struct uvc_video_queue *queue, struct uvc_buffer *buf,
		const __u8 *data, unsigned int len)
{
	static const __u8 hdr[] = {
		0x11, 0x22, 0x33, 0x44,
		0xde, 0xad, 0xbe, 0xef,
		0xde, 0xad, 0xfa, 0xce
	};

	unsigned int maxlen, nbytes;
	__u8 *mem;
	int is_header = 0;

	if (buf == NULL)
		return 0;

	if ((len >= 14 && memcmp(&data[2], hdr, 12) == 0) ||
	    (len >= 15 && memcmp(&data[3], hdr, 12) == 0)) {
		uvc_trace(UVC_TRACE_FRAME, "iSight header found\n");
		is_header = 1;
	}

	/* Synchronize to the input stream by waiting for a header packet. */
	if (buf->state != UVC_BUF_STATE_ACTIVE) {
		if (!is_header) {
			uvc_trace(UVC_TRACE_FRAME, "Dropping packet (out of "
				  "sync).\n");
			return 0;
		}

		buf->state = UVC_BUF_STATE_ACTIVE;
	}

	/* Mark the buffer as done if we're at the beginning of a new frame.
	 *
	 * Empty buffers (bytesused == 0) don't trigger end of frame detection
	 * as it doesn't make sense to return an empty buffer.
	 */
	if (is_header && buf->bytesused != 0) {
		buf->state = UVC_BUF_STATE_DONE;
		return -EAGAIN;
	}

	/* Copy the video data to the buffer. Skip header packets, as they
	 * contain no data.
	 */
	if (!is_header) {
		maxlen = buf->length - buf->bytesused;
		mem = buf->mem + buf->bytesused;
		nbytes = min(len, maxlen);
		memcpy(mem, data, nbytes);
		buf->bytesused += nbytes;

		if (len > maxlen || buf->bytesused == buf->length) {
			uvc_trace(UVC_TRACE_FRAME, "Frame complete "
				  "(overflow).\n");
			buf->state = UVC_BUF_STATE_DONE;
		}
	}

	return 0;
}

void uvc_video_decode_isight(struct urb *urb, struct uvc_streaming *stream,
			struct uvc_buffer *buf, struct uvc_buffer *meta_buf)
{
	int ret, i;

	for (i = 0; i < urb->number_of_packets; ++i) {
		if (urb->iso_frame_desc[i].status < 0) {
			uvc_trace(UVC_TRACE_FRAME, "USB isochronous frame "
				  "lost (%d).\n",
				  urb->iso_frame_desc[i].status);
		}

		/* Decode the payload packet.
		 * uvc_video_decode is entered twice when a frame transition
		 * has been detected because the end of frame can only be
		 * reliably detected when the first packet of the new frame
		 * is processed. The first pass detects the transition and
		 * closes the previous frame's buffer, the second pass
		 * processes the data of the first payload of the new frame.
		 */
		do {
			ret = isight_decode(&stream->queue, buf,
					urb->transfer_buffer +
					urb->iso_frame_desc[i].offset,
					urb->iso_frame_desc[i].actual_length);

			if (buf == NULL)
				break;

			if (buf->state == UVC_BUF_STATE_DONE ||
			    buf->state == UVC_BUF_STATE_ERROR)
				buf = uvc_queue_next_buffer(&stream->queue,
							buf);
		} while (ret == -EAGAIN);
	}
}
