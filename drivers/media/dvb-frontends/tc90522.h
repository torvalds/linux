/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Toshiba TC90522 Demodulator
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 */

/*
 * The demod has 4 input (2xISDB-T and 2xISDB-S),
 * and provides independent sub modules for each input.
 * As the sub modules work in parallel and have the separate i2c addr's,
 * this driver treats each sub module as one demod device.
 */

#ifndef TC90522_H
#define TC90522_H

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

/* I2C device types */
#define TC90522_I2C_DEV_SAT "tc90522sat"
#define TC90522_I2C_DEV_TER "tc90522ter"

struct tc90522_config {
	/* [OUT] frontend returned by driver */
	struct dvb_frontend *fe;

	/* [OUT] tuner I2C adapter returned by driver */
	struct i2c_adapter *tuner_i2c;

	/* [IN] use two separate I2C transactions for one tuner read */
	bool split_tuner_read_i2c;
};

#endif /* TC90522_H */
