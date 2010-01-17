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
#include "dvb-usb-common.h"

/* URB stuff for streaming */
static void usb_urb_complete(struct urb *urb)
{
	struct usb_data_stream *stream = urb->context;
	int ptype = usb_pipetype(urb->pipe);
	int i;
	u8 *b;

	deb_uxfer("'%s' urb completed. status: %d, length: %d/%d, pack_num: %d, errors: %d\n",
		ptype == PIPE_ISOCHRONOUS ? "isoc" : "bulk",
		urb->status,urb->actual_length,urb->transfer_buffer_length,
		urb->number_of_packets,urb->error_count);

	switch (urb->status) {
		case 0:         /* success */
		case -ETIMEDOUT:    /* NAK */
			break;
		case -ECONNRESET:   /* kill */
		case -ENOENT:
		case -ESHUTDOWN:
			return;
		default:        /* error */
			deb_ts("urb completition error %d.\n", urb->status);
			break;
	}

	b = (u8 *) urb->transfer_buffer;
	switch (ptype) {
		case PIPE_ISOCHRONOUS:
			for (i = 0; i < urb->number_of_packets; i++) {

				if (urb->iso_frame_desc[i].status != 0)
					deb_ts("iso frame descriptor has an error: %d\n",urb->iso_frame_desc[i].status);
				else if (urb->iso_frame_desc[i].actual_length > 0)
					stream->complete(stream, b + urb->iso_frame_desc[i].offset, urb->iso_frame_desc[i].actual_length);

				urb->iso_frame_desc[i].status = 0;
				urb->iso_frame_desc[i].actual_length = 0;
			}
			debug_dump(b,20,deb_uxfer);
			break;
		case PIPE_BULK:
			if (urb->actual_length > 0)
				stream->complete(stream, b, urb->actual_length);
			break;
		default:
			err("unknown endpoint type in completition handler.");
			return;
	}
	usb_submit_urb(urb,GFP_ATOMIC);
}

int usb_urb_kill(struct usb_data_stream *stream)
{
	int i;
	for (i = 0; i < stream->urbs_submitted; i++) {
		deb_ts("killing URB no. %d.\n",i);

		/* stop the URB */
		usb_kill_urb(stream->urb_list[i]);
	}
	stream->urbs_submitted = 0;
	return 0;
}

int usb_urb_submit(struct usb_data_stream *stream)
{
	int i,ret;
	for (i = 0; i < stream->urbs_initialized; i++) {
		deb_ts("submitting URB no. %d\n",i);
		if ((ret = usb_submit_urb(stream->urb_list[i],GFP_ATOMIC))) {
			err("could not submit URB no. %d - get them all back",i);
			usb_urb_kill(stream);
			return ret;
		}
		stream->urbs_submitted++;
	}
	return 0;
}

static int usb_free_stream_buffers(struct usb_data_stream *stream)
{
	if (stream->state & USB_STATE_URB_BUF) {
		while (stream->buf_num) {
			stream->buf_num--;
			deb_mem("freeing buffer %d\n",stream->buf_num);
			usb_buffer_free(stream->udev, stream->buf_size,
					stream->buf_list[stream->buf_num], stream->dma_addr[stream->buf_num]);
		}
	}

	stream->state &= ~USB_STATE_URB_BUF;

	return 0;
}

static int usb_allocate_stream_buffers(struct usb_data_stream *stream, int num, unsigned long size)
{
	stream->buf_num = 0;
	stream->buf_size = size;

	deb_mem("all in all I will use %lu bytes for streaming\n",num*size);

	for (stream->buf_num = 0; stream->buf_num < num; stream->buf_num++) {
		deb_mem("allocating buffer %d\n",stream->buf_num);
		if (( stream->buf_list[stream->buf_num] =
					usb_buffer_alloc(stream->udev, size, GFP_ATOMIC,
					&stream->dma_addr[stream->buf_num]) ) == NULL) {
			deb_mem("not enough memory for urb-buffer allocation.\n");
			usb_free_stream_buffers(stream);
			return -ENOMEM;
		}
		deb_mem("buffer %d: %p (dma: %Lu)\n",
			stream->buf_num,
stream->buf_list[stream->buf_num], (long long)stream->dma_addr[stream->buf_num]);
		memset(stream->buf_list[stream->buf_num],0,size);
		stream->state |= USB_STATE_URB_BUF;
	}
	deb_mem("allocation successful\n");

	return 0;
}

static int usb_bulk_urb_init(struct usb_data_stream *stream)
{
	int i, j;

	if ((i = usb_allocate_stream_buffers(stream,stream->props.count,
					stream->props.u.bulk.buffersize)) < 0)
		return i;

	/* allocate the URBs */
	for (i = 0; i < stream->props.count; i++) {
		stream->urb_list[i] = usb_alloc_urb(0, GFP_ATOMIC);
		if (!stream->urb_list[i]) {
			deb_mem("not enough memory for urb_alloc_urb!.\n");
			for (j = 0; j < i; j++)
				usb_free_urb(stream->urb_list[i]);
			return -ENOMEM;
		}
		usb_fill_bulk_urb( stream->urb_list[i], stream->udev,
				usb_rcvbulkpipe(stream->udev,stream->props.endpoint),
				stream->buf_list[i],
				stream->props.u.bulk.buffersize,
				usb_urb_complete, stream);

		stream->urb_list[i]->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		stream->urb_list[i]->transfer_dma = stream->dma_addr[i];
		stream->urbs_initialized++;
	}
	return 0;
}

static int usb_isoc_urb_init(struct usb_data_stream *stream)
{
	int i,j;

	if ((i = usb_allocate_stream_buffers(stream,stream->props.count,
					stream->props.u.isoc.framesize*stream->props.u.isoc.framesperurb)) < 0)
		return i;

	/* allocate the URBs */
	for (i = 0; i < stream->props.count; i++) {
		struct urb *urb;
		int frame_offset = 0;

		stream->urb_list[i] = usb_alloc_urb(stream->props.u.isoc.framesperurb, GFP_ATOMIC);
		if (!stream->urb_list[i]) {
			deb_mem("not enough memory for urb_alloc_urb!\n");
			for (j = 0; j < i; j++)
				usb_free_urb(stream->urb_list[i]);
			return -ENOMEM;
		}

		urb = stream->urb_list[i];

		urb->dev = stream->udev;
		urb->context = stream;
		urb->complete = usb_urb_complete;
		urb->pipe = usb_rcvisocpipe(stream->udev,stream->props.endpoint);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = stream->props.u.isoc.interval;
		urb->number_of_packets = stream->props.u.isoc.framesperurb;
		urb->transfer_buffer_length = stream->buf_size;
		urb->transfer_buffer = stream->buf_list[i];
		urb->transfer_dma = stream->dma_addr[i];

		for (j = 0; j < stream->props.u.isoc.framesperurb; j++) {
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = stream->props.u.isoc.framesize;
			frame_offset += stream->props.u.isoc.framesize;
		}

		stream->urbs_initialized++;
	}
	return 0;
}

int usb_urb_init(struct usb_data_stream *stream, struct usb_data_stream_properties *props)
{
	if (stream == NULL || props == NULL)
		return -EINVAL;

	memcpy(&stream->props, props, sizeof(*props));

	usb_clear_halt(stream->udev,usb_rcvbulkpipe(stream->udev,stream->props.endpoint));

	if (stream->complete == NULL) {
		err("there is no data callback - this doesn't make sense.");
		return -EINVAL;
	}

	switch (stream->props.type) {
		case USB_BULK:
			return usb_bulk_urb_init(stream);
		case USB_ISOC:
			return usb_isoc_urb_init(stream);
		default:
			err("unknown URB-type for data transfer.");
			return -EINVAL;
	}
}

int usb_urb_exit(struct usb_data_stream *stream)
{
	int i;

	usb_urb_kill(stream);

	for (i = 0; i < stream->urbs_initialized; i++) {
		if (stream->urb_list[i] != NULL) {
			deb_mem("freeing URB no. %d.\n",i);
			/* free the URBs */
			usb_free_urb(stream->urb_list[i]);
		}
	}
	stream->urbs_initialized = 0;

	usb_free_stream_buffers(stream);
	return 0;
}
