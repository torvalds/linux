/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Allegro A8293 SEC driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 */

#ifndef A8293_H
#define A8293_H

#include <media/dvb_frontend.h>

/*
 * I2C address
 * 0x08, 0x09, 0x0a, 0x0b
 */

/**
 * struct a8293_platform_data - Platform data for the a8293 driver
 * @dvb_frontend: DVB frontend.
 * @volt_slew_nanos_per_mv: Slew rate when increasing LNB voltage,
 *	 in nanoseconds per millivolt.
 */
struct a8293_platform_data {
	struct dvb_frontend *dvb_frontend;
	int volt_slew_nanos_per_mv;
};

#endif /* A8293_H */
