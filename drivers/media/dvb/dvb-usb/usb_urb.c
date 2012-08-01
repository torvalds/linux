/* usb-urb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file keeps functions for initializing and handling the
 * BULK and ISOC USB data transfers in a generic way.
 * Can be used for DVB-only and also, that's the plan, for
 * Hybrid USB devices (analog and DVB).
 */
#include "dvb_usb_common.h"

/* URB stuff for streaming */

int usb_urb_reconfig(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props);

static void usb_urb_complete(struct urb *urb)
{
	struct usb_data_stream *stream = urb->context;
	int ptype = usb_pipetype(urb->pipe);
	int i;
	u8 *b;

	pr_debug("%s: %s urb completed status=%d length=%d/%d" \
			" pack_num=%d errors=%d\n", __func__,
			ptype == PIPE_ISOCHRONOUS ? "isoc" : "bulk",
			urb->status, urb->actual_length,
			urb->transfer_buffer_length,
			urb->number_of_packets, urb->error_count);

	switch (urb->status) {
	case 0:         /* success */
	case -ETIMEDOUT:    /* NAK */
		break;
	case -ECONNRESET:   /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:        /* error */
		pr_debug("%s: URB completition failed=%d\n", __func__,
				urb->status);
		break;
	}

	b = (u8 *) urb->transfer_buffer;
	switch (ptype) {
	case PIPE_ISOCHRONOUS:
		for (i = 0; i < urb->number_of_packets; i++) {
			if (urb->iso_frame_desc[i].status != 0)
				pr_debug("%s: iso frame descriptor has an " \
						"error=%d\n", __func__,
						urb->iso_frame_desc[i].status);
			else if (urb->iso_frame_desc[i].actual_length > 0)
				stream->complete(stream,
					b + urb->iso_frame_desc[i].offset,
					urb->iso_frame_desc[i].actual_length);

			urb->iso_frame_desc[i].status = 0;
			urb->iso_frame_desc[i].actual_length = 0;
		}
		break;
	case PIPE_BULK:
		if (urb->actual_length > 0)
			stream->complete(stream, b, urb->actual_length);
		break;
	default:
		pr_err("%s: unknown endpoint type in completition handler\n",
				KBUILD_MODNAME);
		return;
	}
	usb_submit_urb(urb, GFP_ATOMIC);
}

int usb_urb_killv2(struct usb_data_stream *stream)
{
	int i;
	for (i = 0; i < stream->urbs_submitted; i++) {
		pr_debug("%s: kill URB=%d\n", __func__, i);
		/* stop the URB */
		usb_kill_urb(stream->urb_list[i]);
	}
	stream->urbs_submitted = 0;
	return 0;
}

int usb_urb_submitv2(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props)
{
	int i, ret;

	if (props) {
		ret = usb_urb_reconfig(stream, props);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < stream->urbs_initialized; i++) {
		pr_debug("%s: submit URB=%d\n", __func__, i);
		ret = usb_submit_urb(stream->urb_list[i], GFP_ATOMIC);
		if (ret) {
			pr_err("%s: could not submit URB no. %d - get them " \
					"all back\n", KBUILD_MODNAME, i);
			usb_urb_killv2(stream);
			return ret;
		}
		stream->urbs_submitted++;
	}
	return 0;
}

int usb_urb_free_urbs(struct usb_data_stream *stream)
{
	int i;

	usb_urb_killv2(stream);

	for (i = 0; i < stream->urbs_initialized; i++) {
		if (stream->urb_list[i] != NULL) {
			pr_debug("%s: free URB=%d\n", __func__, i);
			/* free the URBs */
			usb_free_urb(stream->urb_list[i]);
		}
	}
	stream->urbs_initialized = 0;

	return 0;
}

static int usb_urb_alloc_bulk_urbs(struct usb_data_stream *stream)
{
	int i, j;

	/* allocate the URBs */
	for (i = 0; i < stream->props.count; i++) {
		pr_debug("%s: alloc URB=%d\n", __func__, i);
		stream->urb_list[i] = usb_alloc_urb(0, GFP_ATOMIC);
		if (!stream->urb_list[i]) {
			pr_debug("%s: failed\n", __func__);
			for (j = 0; j < i; j++)
				usb_free_urb(stream->urb_list[j]);
			return -ENOMEM;
		}
		usb_fill_bulk_urb(stream->urb_list[i],
				stream->udev,
				usb_rcvbulkpipe(stream->udev,
						stream->props.endpoint),
				stream->buf_list[i],
				stream->props.u.bulk.buffersize,
				usb_urb_complete, stream);

		stream->urb_list[i]->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		stream->urb_list[i]->transfer_dma = stream->dma_addr[i];
		stream->urbs_initialized++;
	}
	return 0;
}

static int usb_urb_alloc_isoc_urbs(struct usb_data_stream *stream)
{
	int i, j;

	/* allocate the URBs */
	for (i = 0; i < stream->props.count; i++) {
		struct urb *urb;
		int frame_offset = 0;
		pr_debug("%s: alloc URB=%d\n", __func__, i);
		stream->urb_list[i] = usb_alloc_urb(
				stream->props.u.isoc.framesperurb, GFP_ATOMIC);
		if (!stream->urb_list[i]) {
			pr_debug("%s: failed\n", __func__);
			for (j = 0; j < i; j++)
				usb_free_urb(stream->urb_list[j]);
			return -ENOMEM;
		}

		urb = stream->urb_list[i];

		urb->dev = stream->udev;
		urb->context = stream;
		urb->complete = usb_urb_complete;
		urb->pipe = usb_rcvisocpipe(stream->udev,
				stream->props.endpoint);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = stream->props.u.isoc.interval;
		urb->number_of_packets = stream->props.u.isoc.framesperurb;
		urb->transfer_buffer_length = stream->props.u.isoc.framesize *
				stream->props.u.isoc.framesperurb;
		urb->transfer_buffer = stream->buf_list[i];
		urb->transfer_dma = stream->dma_addr[i];

		for (j = 0; j < stream->props.u.isoc.framesperurb; j++) {
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length =
					stream->props.u.isoc.framesize;
			frame_offset += stream->props.u.isoc.framesize;
		}

		stream->urbs_initialized++;
	}
	return 0;
}

int usb_free_stream_buffers(struct usb_data_stream *stream)
{
	if (stream->state & USB_STATE_URB_BUF) {
		while (stream->buf_num) {
			stream->buf_num--;
			pr_debug("%s: free buf=%d\n", __func__,
				stream->buf_num);
			usb_free_coherent(stream->udev, stream->buf_size,
					  stream->buf_list[stream->buf_num],
					  stream->dma_addr[stream->buf_num]);
		}
	}

	stream->state &= ~USB_STATE_URB_BUF;

	return 0;
}

int usb_alloc_stream_buffers(struct usb_data_stream *stream, int num,
		unsigned long size)
{
	stream->buf_num = 0;
	stream->buf_size = size;

	pr_debug("%s: all in all I will use %lu bytes for streaming\n",
			__func__,  num * size);

	for (stream->buf_num = 0; stream->buf_num < num; stream->buf_num++) {
		pr_debug("%s: alloc buf=%d\n", __func__, stream->buf_num);
		stream->buf_list[stream->buf_num] = usb_alloc_coherent(
				stream->udev, size, GFP_ATOMIC,
				&stream->dma_addr[stream->buf_num]);
		if (stream->buf_list[stream->buf_num] == NULL) {
			pr_debug("%s: failed\n", __func__);
			usb_free_stream_buffers(stream);
			return -ENOMEM;
		}

		pr_debug("%s: buf %d: %p (dma %llu)\n", __func__,
				stream->buf_num,
				stream->buf_list[stream->buf_num],
				(long long)stream->dma_addr[stream->buf_num]);
		memset(stream->buf_list[stream->buf_num], 0, size);
		stream->state |= USB_STATE_URB_BUF;
	}

	return 0;
}

int usb_urb_reconfig(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props)
{
	int buf_size;

	if (props == NULL)
		return 0;

	/* check allocated buffers are large enough for the request */
	if (props->type == USB_BULK) {
		buf_size = stream->props.u.bulk.buffersize;
	} else if (props->type == USB_ISOC) {
		buf_size = props->u.isoc.framesize * props->u.isoc.framesperurb;
	} else {
		pr_err("%s: invalid endpoint type=%d\n", KBUILD_MODNAME,
				props->type);
		return -EINVAL;
	}

	if (stream->buf_num < props->count || stream->buf_size < buf_size) {
		pr_err("%s: cannot reconfigure as allocated buffers are too " \
				"small\n", KBUILD_MODNAME);
		return -EINVAL;
	}

	/* check if all fields are same */
	if (stream->props.type == props->type &&
			stream->props.count == props->count &&
			stream->props.endpoint == props->endpoint) {
		if (props->type == USB_BULK &&
				props->u.bulk.buffersize ==
				stream->props.u.bulk.buffersize)
			return 0;
		else if (props->type == USB_ISOC &&
				props->u.isoc.framesperurb ==
				stream->props.u.isoc.framesperurb &&
				props->u.isoc.framesize ==
				stream->props.u.isoc.framesize &&
				props->u.isoc.interval ==
				stream->props.u.isoc.interval)
			return 0;
	}

	pr_debug("%s: re-alloc URBs\n", __func__);

	usb_urb_free_urbs(stream);
	memcpy(&stream->props, props, sizeof(*props));
	if (props->type == USB_BULK)
		return usb_urb_alloc_bulk_urbs(stream);
	else if (props->type == USB_ISOC)
		return usb_urb_alloc_isoc_urbs(stream);

	return 0;
}

int usb_urb_initv2(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props)
{
	int ret;

	if (stream == NULL || props == NULL)
		return -EINVAL;

	memcpy(&stream->props, props, sizeof(*props));

	usb_clear_halt(stream->udev, usb_rcvbulkpipe(stream->udev,
			stream->props.endpoint));

	if (stream->complete == NULL) {
		pr_err("%s: there is no data callback - this doesn't make " \
				"sense\n", KBUILD_MODNAME);
		return -EINVAL;
	}

	switch (stream->props.type) {
	case USB_BULK:
		ret = usb_alloc_stream_buffers(stream, stream->props.count,
				stream->props.u.bulk.buffersize);
		if (ret < 0)
			return ret;

		return usb_urb_alloc_bulk_urbs(stream);
	case USB_ISOC:
		ret = usb_alloc_stream_buffers(stream, stream->props.count,
				stream->props.u.isoc.framesize *
				stream->props.u.isoc.framesperurb);
		if (ret < 0)
			return ret;

		return usb_urb_alloc_isoc_urbs(stream);
	default:
		pr_err("%s: unknown URB-type for data transfer\n",
				KBUILD_MODNAME);
		return -EINVAL;
	}
}

int usb_urb_exitv2(struct usb_data_stream *stream)
{
	usb_urb_free_urbs(stream);
	usb_free_stream_buffers(stream);

	return 0;
}
