// SPDX-License-Identifier: GPL-2.0
/* dvb-usb-urb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@posteo.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file keeps functions for initializing and handling the
 * USB and URB stuff.
 */
#include "dvb-usb-common.h"

int dvb_usb_generic_rw(struct dvb_usb_device *d, u8 *wbuf, u16 wlen, u8 *rbuf,
	u16 rlen, int delay_ms)
{
	int actlen = 0, ret = -ENOMEM;

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
				d->props.generic_bulk_ctrl_endpoint_response ?
				d->props.generic_bulk_ctrl_endpoint_response :
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

static void dvb_usb_data_complete_raw(struct usb_data_stream *stream,
				      u8 *buffer, size_t length)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	if (adap->feedcount > 0 && adap->state & DVB_USB_ADAP_STATE_DVB)
		dvb_dmx_swfilter_raw(&adap->demux, buffer, length);
}

int dvb_usb_adapter_stream_init(struct dvb_usb_adapter *adap)
{
	int i, ret = 0;
	for (i = 0; i < adap->props.num_frontends; i++) {

		adap->fe_adap[i].stream.udev      = adap->dev->udev;
		if (adap->props.fe[i].caps & DVB_USB_ADAP_RECEIVES_204_BYTE_TS)
			adap->fe_adap[i].stream.complete =
				dvb_usb_data_complete_204;
		else
		if (adap->props.fe[i].caps & DVB_USB_ADAP_RECEIVES_RAW_PAYLOAD)
			adap->fe_adap[i].stream.complete =
				dvb_usb_data_complete_raw;
		else
		adap->fe_adap[i].stream.complete  = dvb_usb_data_complete;
		adap->fe_adap[i].stream.user_priv = adap;
		ret = usb_urb_init(&adap->fe_adap[i].stream,
				   &adap->props.fe[i].stream);
		if (ret < 0)
			break;
	}
	return ret;
}

int dvb_usb_adapter_stream_exit(struct dvb_usb_adapter *adap)
{
	int i;
	for (i = 0; i < adap->props.num_frontends; i++)
		usb_urb_exit(&adap->fe_adap[i].stream);
	return 0;
}
