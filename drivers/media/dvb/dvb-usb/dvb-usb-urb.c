/* dvb-usb-urb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file keeps functions for initializing and handling the
 * USB and URB stuff.
 */
#include "dvb-usb-common.h"

int dvb_usb_generic_rw(struct dvb_usb_device *d, u8 *wbuf, u16 wlen, u8 *rbuf,
	u16 rlen, int delay_ms)
{
	int actlen,ret = -ENOMEM;

	if (!d || wbuf == NULL || wlen == 0)
		return -EINVAL;

	if (d->props.generic_bulk_ctrl_endpoint == 0) {
		err("endpoint for generic control not specified.");
		return -EINVAL;
	}

	if ((ret = mutex_lock_interruptible(&d->usb_mutex)))
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

	mutex_unlock(&d->usb_mutex);
	return ret;
}
EXPORT_SYMBOL(dvb_usb_generic_rw);

int dvb_usb_generic_write(struct dvb_usb_device *d, u8 *buf, u16 len)
{
	return dvb_usb_generic_rw(d,buf,len,NULL,0,0);
}
EXPORT_SYMBOL(dvb_usb_generic_write);

static void dvb_usb_data_complete(struct usb_data_stream *stream, u8 *buffer, size_t length)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	if (adap->feedcount > 0 && adap->state & DVB_USB_ADAP_STATE_DVB)
		dvb_dmx_swfilter(&adap->demux, buffer, length);
}

static void dvb_usb_data_complete_204(struct usb_data_stream *stream, u8 *buffer, size_t length)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	if (adap->feedcount > 0 && adap->state & DVB_USB_ADAP_STATE_DVB)
		dvb_dmx_swfilter_204(&adap->demux, buffer, length);
}

int dvb_usb_adapter_stream_init(struct dvb_usb_adapter *adap)
{
	adap->stream.udev      = adap->dev->udev;
	if (adap->props.caps & DVB_USB_ADAP_RECEIVES_204_BYTE_TS)
		adap->stream.complete = dvb_usb_data_complete_204;
	else
	adap->stream.complete  = dvb_usb_data_complete;
	adap->stream.user_priv = adap;
	return usb_urb_init(&adap->stream, &adap->props.stream);
}

int dvb_usb_adapter_stream_exit(struct dvb_usb_adapter *adap)
{
	return usb_urb_exit(&adap->stream);
}
