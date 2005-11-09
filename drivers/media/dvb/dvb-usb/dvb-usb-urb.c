/* dvb-usb-urb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for initializing and handling the
 * USB and URB stuff.
 */
#include "dvb-usb-common.h"

int dvb_usb_generic_rw(struct dvb_usb_device *d, u8 *wbuf, u16 wlen, u8 *rbuf,
	u16 rlen, int delay_ms)
{
	int actlen,ret = -ENOMEM;

	if (d->props.generic_bulk_ctrl_endpoint == 0) {
		err("endpoint for generic control not specified.");
		return -EINVAL;
	}

	if (wbuf == NULL || wlen == 0)
		return -EINVAL;

	if ((ret = down_interruptible(&d->usb_sem)))
		return ret;

	deb_xfer(">>> ");
	debug_dump(wbuf,wlen,deb_xfer);

	ret = usb_bulk_msg(d->udev,usb_sndbulkpipe(d->udev,
			d->props.generic_bulk_ctrl_endpoint), wbuf,wlen,&actlen,
			2000);

	if (ret)
		err("bulk message failed: %d (%d/%d)",ret,wlen,actlen);
	else
		ret = actlen != wlen ? -1 : 0;

	/* an answer is expected, and no error before */
	if (!ret && rbuf && rlen) {
		if (delay_ms)
			msleep(delay_ms);

		ret = usb_bulk_msg(d->udev,usb_rcvbulkpipe(d->udev,
				d->props.generic_bulk_ctrl_endpoint),rbuf,rlen,&actlen,
				2000);

		if (ret)
			err("recv bulk message failed: %d",ret);
		else {
			deb_xfer("<<< ");
			debug_dump(rbuf,actlen,deb_xfer);
		}
	}

	up(&d->usb_sem);
	return ret;
}
EXPORT_SYMBOL(dvb_usb_generic_rw);

int dvb_usb_generic_write(struct dvb_usb_device *d, u8 *buf, u16 len)
{
	return dvb_usb_generic_rw(d,buf,len,NULL,0,0);
}
EXPORT_SYMBOL(dvb_usb_generic_write);


/* URB stuff for streaming */
static void dvb_usb_urb_complete(struct urb *urb, struct pt_regs *ptregs)
{
	struct dvb_usb_device *d = urb->context;
	int ptype = usb_pipetype(urb->pipe);
	int i;
	u8 *b;

	deb_ts("'%s' urb completed. feedcount: %d, status: %d, length: %d/%d, pack_num: %d, errors: %d\n",
			ptype == PIPE_ISOCHRONOUS ? "isoc" : "bulk", d->feedcount,
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
			deb_ts("urb completition error %d.", urb->status);
			break;
	}

	if (d->feedcount > 0) {
		if (d->state & DVB_USB_STATE_DVB) {
			switch (ptype) {
				case PIPE_ISOCHRONOUS:
					b = (u8 *) urb->transfer_buffer;
					for (i = 0; i < urb->number_of_packets; i++) {
						if (urb->iso_frame_desc[i].status != 0)
							deb_ts("iso frame descriptor has an error: %d\n",urb->iso_frame_desc[i].status);
						else if (urb->iso_frame_desc[i].actual_length > 0) {
								dvb_dmx_swfilter(&d->demux,b + urb->iso_frame_desc[i].offset,
										urb->iso_frame_desc[i].actual_length);
							}
						urb->iso_frame_desc[i].status = 0;
						urb->iso_frame_desc[i].actual_length = 0;
					}
					debug_dump(b,20,deb_ts);
					break;
				case PIPE_BULK:
					if (urb->actual_length > 0)
						dvb_dmx_swfilter(&d->demux, (u8 *) urb->transfer_buffer,urb->actual_length);
					break;
				default:
					err("unkown endpoint type in completition handler.");
					return;
			}
		}
	}

	usb_submit_urb(urb,GFP_ATOMIC);
}

int dvb_usb_urb_kill(struct dvb_usb_device *d)
{
	int i;
	for (i = 0; i < d->urbs_submitted; i++) {
		deb_ts("killing URB no. %d.\n",i);

		/* stop the URB */
		usb_kill_urb(d->urb_list[i]);
	}
	d->urbs_submitted = 0;
	return 0;
}

int dvb_usb_urb_submit(struct dvb_usb_device *d)
{
	int i,ret;
	for (i = 0; i < d->urbs_initialized; i++) {
		deb_ts("submitting URB no. %d\n",i);
		if ((ret = usb_submit_urb(d->urb_list[i],GFP_ATOMIC))) {
			err("could not submit URB no. %d - get them all back",i);
			dvb_usb_urb_kill(d);
			return ret;
		}
		d->urbs_submitted++;
	}
	return 0;
}

static int dvb_usb_free_stream_buffers(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_URB_BUF) {
		while (d->buf_num) {
			d->buf_num--;
			deb_mem("freeing buffer %d\n",d->buf_num);
			usb_buffer_free(d->udev, d->buf_size,
					d->buf_list[d->buf_num], d->dma_addr[d->buf_num]);
		}
		kfree(d->buf_list);
		kfree(d->dma_addr);
	}

	d->state &= ~DVB_USB_STATE_URB_BUF;

	return 0;
}

static int dvb_usb_allocate_stream_buffers(struct dvb_usb_device *d, int num, unsigned long size)
{
	d->buf_num = 0;
	d->buf_size = size;

	deb_mem("all in all I will use %lu bytes for streaming\n",num*size);

	if ((d->buf_list = kmalloc(num*sizeof(u8 *), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((d->dma_addr = kmalloc(num*sizeof(dma_addr_t), GFP_ATOMIC)) == NULL) {
		kfree(d->buf_list);
		return -ENOMEM;
	}
	memset(d->buf_list,0,num*sizeof(u8 *));
	memset(d->dma_addr,0,num*sizeof(dma_addr_t));

	d->state |= DVB_USB_STATE_URB_BUF;

	for (d->buf_num = 0; d->buf_num < num; d->buf_num++) {
		deb_mem("allocating buffer %d\n",d->buf_num);
		if (( d->buf_list[d->buf_num] =
					usb_buffer_alloc(d->udev, size, SLAB_ATOMIC,
					&d->dma_addr[d->buf_num]) ) == NULL) {
			deb_mem("not enough memory for urb-buffer allocation.\n");
			dvb_usb_free_stream_buffers(d);
			return -ENOMEM;
		}
		deb_mem("buffer %d: %p (dma: %llu)\n",
			d->buf_num, d->buf_list[d->buf_num],
			(unsigned long long)d->dma_addr[d->buf_num]);
		memset(d->buf_list[d->buf_num],0,size);
	}
	deb_mem("allocation successful\n");

	return 0;
}

static int dvb_usb_bulk_urb_init(struct dvb_usb_device *d)
{
	int i;

	if ((i = dvb_usb_allocate_stream_buffers(d,d->props.urb.count,
					d->props.urb.u.bulk.buffersize)) < 0)
		return i;

	/* allocate the URBs */
	for (i = 0; i < d->props.urb.count; i++) {
		if ((d->urb_list[i] = usb_alloc_urb(0,GFP_ATOMIC)) == NULL)
			return -ENOMEM;

		usb_fill_bulk_urb( d->urb_list[i], d->udev,
				usb_rcvbulkpipe(d->udev,d->props.urb.endpoint),
				d->buf_list[i],
				d->props.urb.u.bulk.buffersize,
				dvb_usb_urb_complete, d);

		d->urb_list[i]->transfer_flags = 0;
		d->urbs_initialized++;
	}
	return 0;
}

static int dvb_usb_isoc_urb_init(struct dvb_usb_device *d)
{
	int i,j;

	if ((i = dvb_usb_allocate_stream_buffers(d,d->props.urb.count,
					d->props.urb.u.isoc.framesize*d->props.urb.u.isoc.framesperurb)) < 0)
		return i;

	/* allocate the URBs */
	for (i = 0; i < d->props.urb.count; i++) {
		struct urb *urb;
		int frame_offset = 0;
		if ((d->urb_list[i] =
					usb_alloc_urb(d->props.urb.u.isoc.framesperurb,GFP_ATOMIC)) == NULL)
			return -ENOMEM;

		urb = d->urb_list[i];

		urb->dev = d->udev;
		urb->context = d;
		urb->complete = dvb_usb_urb_complete;
		urb->pipe = usb_rcvisocpipe(d->udev,d->props.urb.endpoint);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = d->props.urb.u.isoc.interval;
		urb->number_of_packets = d->props.urb.u.isoc.framesperurb;
		urb->transfer_buffer_length = d->buf_size;
		urb->transfer_buffer = d->buf_list[i];
		urb->transfer_dma = d->dma_addr[i];

		for (j = 0; j < d->props.urb.u.isoc.framesperurb; j++) {
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = d->props.urb.u.isoc.framesize;
			frame_offset += d->props.urb.u.isoc.framesize;
		}

		d->urbs_initialized++;
	}
	return 0;

}

int dvb_usb_urb_init(struct dvb_usb_device *d)
{
	/*
	 * when reloading the driver w/o replugging the device
	 * sometimes a timeout occures, this helps
	 */
	if (d->props.generic_bulk_ctrl_endpoint != 0) {
		usb_clear_halt(d->udev,usb_sndbulkpipe(d->udev,d->props.generic_bulk_ctrl_endpoint));
		usb_clear_halt(d->udev,usb_rcvbulkpipe(d->udev,d->props.generic_bulk_ctrl_endpoint));
	}
	usb_clear_halt(d->udev,usb_rcvbulkpipe(d->udev,d->props.urb.endpoint));

	/* allocate the array for the data transfer URBs */
	d->urb_list = kmalloc(d->props.urb.count * sizeof(struct urb *),GFP_KERNEL);
	if (d->urb_list == NULL)
		return -ENOMEM;
	memset(d->urb_list,0,d->props.urb.count * sizeof(struct urb *));
	d->state |= DVB_USB_STATE_URB_LIST;

	switch (d->props.urb.type) {
		case DVB_USB_BULK:
			return dvb_usb_bulk_urb_init(d);
		case DVB_USB_ISOC:
			return dvb_usb_isoc_urb_init(d);
		default:
			err("unkown URB-type for data transfer.");
			return -EINVAL;
	}
}

int dvb_usb_urb_exit(struct dvb_usb_device *d)
{
	int i;

	dvb_usb_urb_kill(d);

	if (d->state & DVB_USB_STATE_URB_LIST) {
		for (i = 0; i < d->urbs_initialized; i++) {
			if (d->urb_list[i] != NULL) {
				deb_mem("freeing URB no. %d.\n",i);
				/* free the URBs */
				usb_free_urb(d->urb_list[i]);
			}
		}
		d->urbs_initialized = 0;
		/* free the urb array */
		kfree(d->urb_list);
		d->state &= ~DVB_USB_STATE_URB_LIST;
	}

	dvb_usb_free_stream_buffers(d);
	return 0;
}
