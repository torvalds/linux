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
	u16 rlen, int delay_ms)
{
	int actlen, ret = -ENOMEM;

	if (!d || wbuf == NULL || wlen == 0)
		return -EINVAL;

	if (d->props->generic_bulk_ctrl_endpoint == 0) {
		pr_err("%s: endpoint for generic control not specified\n",
				KBUILD_MODNAME);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&d->usb_mutex);
	if (ret)
		return ret;

#ifdef DVB_USB_XFER_DEBUG
	print_hex_dump(KERN_DEBUG, KBUILD_MODNAME ": >>> ", DUMP_PREFIX_NONE,
			32, 1, wbuf, wlen, 0);
#endif

	ret = usb_bulk_msg(d->udev, usb_sndbulkpipe(d->udev,
			d->props->generic_bulk_ctrl_endpoint), wbuf, wlen,
			&actlen, 2000);

	if (ret)
		pr_err("%s: bulk message failed: %d (%d/%d)\n", KBUILD_MODNAME,
				ret, wlen, actlen);
	else
		ret = actlen != wlen ? -1 : 0;

	/* an answer is expected, and no error before */
	if (!ret && rbuf && rlen) {
		if (delay_ms)
			msleep(delay_ms);

		ret = usb_bulk_msg(d->udev, usb_rcvbulkpipe(d->udev,
				d->props->generic_bulk_ctrl_endpoint_response ?
				d->props->generic_bulk_ctrl_endpoint_response :
				d->props->generic_bulk_ctrl_endpoint),
				rbuf, rlen, &actlen, 2000);

		if (ret)
			pr_err("%s: recv bulk message failed: %d\n",
					KBUILD_MODNAME, ret);
#ifdef DVB_USB_XFER_DEBUG
		print_hex_dump(KERN_DEBUG, KBUILD_MODNAME ": <<< ",
				DUMP_PREFIX_NONE, 32, 1, rbuf, actlen, 0);
#endif
	}

	mutex_unlock(&d->usb_mutex);
	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_generic_rw);

int dvb_usbv2_generic_write(struct dvb_usb_device *d, u8 *buf, u16 len)
{
	return dvb_usbv2_generic_rw(d, buf, len, NULL, 0, 0);
}
EXPORT_SYMBOL(dvb_usbv2_generic_write);
