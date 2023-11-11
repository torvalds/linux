/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Support for the sensor part which is integrated (I think) into the
 * st6422 stv06xx alike bridge, as its integrated there are no i2c writes
 * but instead direct bridge writes.
 *
 * Copyright (c) 2009 Hans de Goede <hdegoede@redhat.com>
 *
 * Strongly based on qc-usb-messenger, which is:
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 */

#ifndef STV06XX_ST6422_H_
#define STV06XX_ST6422_H_

#include "stv06xx_sensor.h"

static int st6422_probe(struct sd *sd);
static int st6422_start(struct sd *sd);
static int st6422_init(struct sd *sd);
static int st6422_init_controls(struct sd *sd);
static int st6422_stop(struct sd *sd);

const struct stv06xx_sensor stv06xx_sensor_st6422 = {
	.name = "ST6422",
	/* No known way to lower framerate in case of less bandwidth */
	.min_packet_size = { 300, 847 },
	.max_packet_size = { 300, 847 },
	.init = st6422_init,
	.init_controls = st6422_init_controls,
	.probe = st6422_probe,
	.start = st6422_start,
	.stop = st6422_stop,
};

#endif
