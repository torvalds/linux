/* dvb-usb-i2c.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for (de-)initializing an I2C adapter.
 */
#include "dvb-usb-common.h"

int dvb_usb_i2c_init(struct dvb_usb_device *d)
{
	int ret = 0;

	if (!(d->props.caps & DVB_USB_IS_AN_I2C_ADAPTER))
		return 0;

	if (d->props.i2c_algo == NULL) {
		err("no i2c algorithm specified");
		return -EINVAL;
	}

	strncpy(d->i2c_adap.name, d->desc->name, sizeof(d->i2c_adap.name));
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	d->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL,
#else
	d->i2c_adap.class = I2C_CLASS_TV_DIGITAL,
#endif
	d->i2c_adap.algo      = d->props.i2c_algo;
	d->i2c_adap.algo_data = NULL;
	d->i2c_adap.dev.parent = &d->udev->dev;

	i2c_set_adapdata(&d->i2c_adap, d);

	if ((ret = i2c_add_adapter(&d->i2c_adap)) < 0)
		err("could not add i2c adapter");

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

int dvb_usb_tuner_init_i2c(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct i2c_msg msg = { .addr = adap->pll_addr, .flags = 0, .buf = adap->pll_init, .len = 4 };
	int ret = 0;

	/* if pll_desc is not used */
	if (adap->pll_desc == NULL)
		return 0;

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe, 1, adap->pll_addr);

	deb_pll("pll init: %x\n",adap->pll_addr);
	deb_pll("pll-buf: %x %x %x %x\n",adap->pll_init[0], adap->pll_init[1],
			adap->pll_init[2], adap->pll_init[3]);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&adap->dev->i2c_adap, &msg, 1) != 1) {
		err("tuner i2c write failed for pll_init.");
		ret = -EREMOTEIO;
	}
	msleep(1);

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe,0,adap->pll_addr);
	return ret;
}
EXPORT_SYMBOL(dvb_usb_tuner_init_i2c);

int dvb_usb_tuner_set_params_i2c(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	int ret = 0;
	u8 b[5];
	struct i2c_msg msg = { .addr = adap->pll_addr, .flags = 0, .buf = &b[1], .len = 4 };

	fe->ops.tuner_ops.calc_regs(fe, fep, b, sizeof(b));

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe, 1, adap->pll_addr);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (i2c_transfer(&adap->dev->i2c_adap, &msg, 1) != 1) {
		err("tuner i2c write failed for pll_set.");
		ret = -EREMOTEIO;
	}
	msleep(1);

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe, 0, adap->pll_addr);

	return ret;
}
EXPORT_SYMBOL(dvb_usb_tuner_set_params_i2c);
