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

	debug_dump(wbuf,wlen,deb_xfer);

	ret = usb_bulk_msg(d->udev,usb_sndbulkpipe(d->udev,
			d->props.generic_bulk_ctrl_endpoint), wbuf,wlen,&actlen,
			2*HZ);

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
				2*HZ);

		if (ret)
			err("recv bulk message failed: %d",ret);
		else
			debug_dump(rbuf,actlen,deb_xfer);
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

static void dvb_usb_bulk_urb_complete(struct urb *urb, struct pt_regs *ptregs)
{
	struct dvb_usb_device *d = urb->context;

	deb_ts("bulk urb completed. feedcount: %d, status: %d, length: %d\n",d->feedcount,urb->status,
			urb->actual_length);

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

	if (d->feedcount > 0 && urb->actual_length > 0) {
		if (d->state & DVB_USB_STATE_DVB)
			dvb_dmx_swfilter(&d->demux, (u8*) urb->transfer_buffer,urb->actual_length);
	} else
		deb_ts("URB dropped because of feedcount.\n");

	usb_submit_urb(urb,GFP_ATOMIC);
}

int dvb_usb_urb_kill(struct dvb_usb_device *d)
{
	int i;
	for (i = 0; i < d->urbs_submitted; i++) {
		deb_info("killing URB no. %d.\n",i);

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
		deb_info("submitting URB no. %d\n",i);
		if ((ret = usb_submit_urb(d->urb_list[i],GFP_ATOMIC))) {
			err("could not submit URB no. %d - get them all back\n",i);
			dvb_usb_urb_kill(d);
			return ret;
		}
		d->urbs_submitted++;
	}
	return 0;
}

static int dvb_usb_bulk_urb_init(struct dvb_usb_device *d)
{
	int i,bufsize = d->props.urb.count * d->props.urb.u.bulk.buffersize;

	deb_info("allocate %d bytes as buffersize for all URBs\n",bufsize);
	/* allocate the actual buffer for the URBs */
	if ((d->buffer =  usb_buffer_alloc(d->udev, bufsize, SLAB_ATOMIC, &d->dma_handle)) == NULL) {
		deb_info("not enough memory for urb-buffer allocation.\n");
		return -ENOMEM;
	}
	deb_info("allocation successful\n");
	memset(d->buffer,0,bufsize);

	d->state |= DVB_USB_STATE_URB_BUF;

	/* allocate the URBs */
	for (i = 0; i < d->props.urb.count; i++) {
		if (!(d->urb_list[i] = usb_alloc_urb(0,GFP_ATOMIC))) {
			return -ENOMEM;
		}

		usb_fill_bulk_urb( d->urb_list[i], d->udev,
				usb_rcvbulkpipe(d->udev,d->props.urb.endpoint),
				&d->buffer[i*d->props.urb.u.bulk.buffersize],
				d->props.urb.u.bulk.buffersize,
				dvb_usb_bulk_urb_complete, d);

		d->urb_list[i]->transfer_flags = 0;
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
			err("isochronous transfer not yet implemented in dvb-usb.");
			return -EINVAL;
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
				deb_info("freeing URB no. %d.\n",i);
				/* free the URBs */
				usb_free_urb(d->urb_list[i]);
			}
		}
		d->urbs_initialized = 0;
		/* free the urb array */
		kfree(d->urb_list);
		d->state &= ~DVB_USB_STATE_URB_LIST;
	}

	if (d->state & DVB_USB_STATE_URB_BUF)
		usb_buffer_free(d->udev, d->props.urb.u.bulk.buffersize * d->props.urb.count,
				d->buffer, d->dma_handle);

	d->state &= ~DVB_USB_STATE_URB_BUF;
	return 0;
}
