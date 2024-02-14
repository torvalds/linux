/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * public header file of the frontend drivers for mobile DVB-T demodulators
 * DiBcom 3000M-B and DiBcom 3000P/M-C (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * based on GPL code from DibCom, which has
 *
 * Copyright (C) 2004 Amaury Demol for DiBcom
 *
 * Acknowledgements
 *
 *  Amaury Demol from DiBcom for providing specs and driver
 *  sources, on which this driver (and the dvb-dibusb) are based.
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
 */

#ifndef DIB3000_H
#define DIB3000_H

#include <linux/dvb/frontend.h>

struct dib3000_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

struct dib_fe_xfer_ops
{
	/* pid and transfer handling is done in the demodulator */
	int (*pid_parse)(struct dvb_frontend *fe, int onoff);
	int (*fifo_ctrl)(struct dvb_frontend *fe, int onoff);
	int (*pid_ctrl)(struct dvb_frontend *fe, int index, int pid, int onoff);
	int (*tuner_pass_ctrl)(struct dvb_frontend *fe, int onoff, u8 pll_ctrl);
};

#if IS_REACHABLE(CONFIG_DVB_DIB3000MB)
extern struct dvb_frontend* dib3000mb_attach(const struct dib3000_config* config,
					     struct i2c_adapter* i2c, struct dib_fe_xfer_ops *xfer_ops);
#else
static inline struct dvb_frontend* dib3000mb_attach(const struct dib3000_config* config,
					     struct i2c_adapter* i2c, struct dib_fe_xfer_ops *xfer_ops)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_DIB3000MB

#endif // DIB3000_H
