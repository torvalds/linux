/*
 * DVB USB framework
 *
 * Copyright (C) 2004-6 Patrick Boettcher <patrick.boettcher@desy.de>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "dvb_usb_common.h"

static int dvb_usb_v2_generic_io(struct dvb_usb_device *d,
		u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	int ret, actual_length;

	if (!wbuf || !wlen || !d->props->generic_bulk_ctrl_endpoint ||
			!d->props->generic_bulk_ctrl_endpoint_response) {
		dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, -EINVAL);
		return -EINVAL;
	}

	dev_dbg(&d->udev->dev, "%s: >>> %*ph\n", __func__, wlen, wbuf);

	ret = usb_bulk_msg(d->udev, usb_sndbulkpipe(d->udev,
			d->props->generic_bulk_ctrl_endpoint), wbuf, wlen,
			&actual_length, 2000);
	if (ret < 0)
		dev_err(&d->udev->dev, "%s: usb_bulk_msg() failed=%d\n",
				KBUILD_MODNAME, ret);
	else
		ret = actual_length != wlen ? -EIO : 0;

	/* an answer is expected, and no error before */
	if (!ret && rbuf && rlen) {
		if (d->props->generic_bulk_ctrl_delay)
			usleep_range(d->props->generic_bulk_ctrl_delay,
					d->props->generic_bulk_ctrl_delay
					+ 20000);

		ret = usb_bulk_msg(d->udev, usb_rcvbulkpipe(d->udev,
				d->props->generic_bulk_ctrl_endpoint_response),
				rbuf, rlen, &actual_length, 2000);
		if (ret)
			dev_err(&d->udev->dev,
					"%s: 2nd usb_bulk_msg() failed=%d\n",
					KBUILD_MODNAME, ret);

		dev_dbg(&d->udev->dev, "%s: <<< %*ph\n", __func__,
				actual_length, rbuf);
	}

	return ret;
}

int dvb_usbv2_generic_rw(struct dvb_usb_device *d,
		u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	int ret;

	mutex_lock(&d->usb_mutex);
	ret = dvb_usb_v2_generic_io(d, wbuf, wlen, rbuf, rlen);
	mutex_unlock(&d->usb_mutex);

	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_generic_rw);

int dvb_usbv2_generic_write(struct dvb_usb_device *d, u8 *buf, u16 len)
{
	int ret;

	mutex_lock(&d->usb_mutex);
	ret = dvb_usb_v2_generic_io(d, buf, len, NULL, 0);
	mutex_unlock(&d->usb_mutex);

	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_generic_write);

int dvb_usbv2_generic_rw_locked(struct dvb_usb_device *d,
		u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	return dvb_usb_v2_generic_io(d, wbuf, wlen, rbuf, rlen);
}
EXPORT_SYMBOL(dvb_usbv2_generic_rw_locked);

int dvb_usbv2_generic_write_locked(struct dvb_usb_device *d, u8 *buf, u16 len)
{
	return dvb_usb_v2_generic_io(d, buf, len, NULL, 0);
}
EXPORT_SYMBOL(dvb_usbv2_generic_write_locked);
