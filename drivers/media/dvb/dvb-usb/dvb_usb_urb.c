/* dvb-usb-urb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file keeps functions for initializing and handling the
 * USB and URB stuff.
 */
#include "dvb_usb_common.h"

#undef DVB_USB_XFER_DEBUG
int dvb_usbv2_generic_rw(struct dvb_usb_device *d, u8 *wbuf, u16 wlen, u8 *rbuf,
		u16 rlen)
{
	int ret, actual_length;

	if (!d || !wbuf || !wlen || !d->props->generic_bulk_ctrl_endpoint ||
			!d->props->generic_bulk_ctrl_endpoint_response) {
		pr_debug("%s: failed=%d\n", __func__, -EINVAL);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&d->usb_mutex);
	if (ret < 0)
		return ret;

#ifdef DVB_USB_XFER_DEBUG
	print_hex_dump(KERN_DEBUG, KBUILD_MODNAME ": >>> ", DUMP_PREFIX_NONE,
			32, 1, wbuf, wlen, 0);
#endif
	ret = usb_bulk_msg(d->udev, usb_sndbulkpipe(d->udev,
			d->props->generic_bulk_ctrl_endpoint), wbuf, wlen,
			&actual_length, 2000);
	if (ret < 0)
		pr_err("%s: usb_bulk_msg() failed=%d\n", KBUILD_MODNAME, ret);
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
			pr_err("%s: 2nd usb_bulk_msg() failed=%d\n",
					KBUILD_MODNAME, ret);

#ifdef DVB_USB_XFER_DEBUG
		print_hex_dump(KERN_DEBUG, KBUILD_MODNAME ": <<< ",
				DUMP_PREFIX_NONE, 32, 1, rbuf, actual_length,
				0);
#endif
	}

	mutex_unlock(&d->usb_mutex);
	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_generic_rw);

int dvb_usbv2_generic_write(struct dvb_usb_device *d, u8 *buf, u16 len)
{
	return dvb_usbv2_generic_rw(d, buf, len, NULL, 0);
}
EXPORT_SYMBOL(dvb_usbv2_generic_write);
