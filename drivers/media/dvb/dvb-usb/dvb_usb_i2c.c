/* dvb-usb-i2c.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for (de-)initializing an I2C adapter.
 */
#include "dvb_usb_common.h"

int dvb_usb_i2c_init(struct dvb_usb_device *d)
{
	int ret = 0;

	if (!d->props.i2c_algo)
		return 0;

	strlcpy(d->i2c_adap.name, d->name, sizeof(d->i2c_adap.name));
	d->i2c_adap.algo      = d->props.i2c_algo;
	d->i2c_adap.algo_data = NULL;
	d->i2c_adap.dev.parent = &d->udev->dev;

	i2c_set_adapdata(&d->i2c_adap, d);

	ret = i2c_add_adapter(&d->i2c_adap);
	if (ret < 0)
		pr_err("%s: could not add i2c adapter", KBUILD_MODNAME);

	d->state |= DVB_USB_STATE_I2C;

	return ret;
}

int dvb_usb_i2c_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_I2C)
		i2c_del_adapter(&d->i2c_adap);
	d->state &= ~DVB_USB_STATE_I2C;
	return 0;
}
