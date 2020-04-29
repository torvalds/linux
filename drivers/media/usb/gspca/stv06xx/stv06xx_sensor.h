/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * P/N 861037:      Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 - QuickCam Express
 * P/N 861055:      Sensor ST VV6410       ASIC STV0610   - LEGO cam
 * P/N 861075-0040: Sensor HDCS1000        ASIC
 * P/N 961179-0700: Sensor ST VV6410       ASIC STV0602   - Dexxa WebCam USB
 * P/N 861040-0000: Sensor ST VV6410       ASIC STV0610   - QuickCam Web
 */

#ifndef STV06XX_SENSOR_H_
#define STV06XX_SENSOR_H_

#include "stv06xx.h"

#define IS_1020(sd)	((sd)->sensor == &stv06xx_sensor_hdcs1020)

extern const struct stv06xx_sensor stv06xx_sensor_vv6410;
extern const struct stv06xx_sensor stv06xx_sensor_hdcs1x00;
extern const struct stv06xx_sensor stv06xx_sensor_hdcs1020;
extern const struct stv06xx_sensor stv06xx_sensor_pb0100;
extern const struct stv06xx_sensor stv06xx_sensor_st6422;

struct stv06xx_sensor {
	/* Defines the name of a sensor */
	char name[32];

	/* Sensor i2c address */
	u8 i2c_addr;

	/* Flush value*/
	u8 i2c_flush;

	/* length of an i2c word */
	u8 i2c_len;

	/* Isoc packet size (per mode) */
	int min_packet_size[4];
	int max_packet_size[4];

	/* Probes if the sensor is connected */
	int (*probe)(struct sd *sd);

	/* Performs a initialization sequence */
	int (*init)(struct sd *sd);

	/* Initializes the controls */
	int (*init_controls)(struct sd *sd);

	/* Reads a sensor register */
	int (*read_sensor)(struct sd *sd, const u8 address,
	      u8 *i2c_data, const u8 len);

	/* Writes to a sensor register */
	int (*write_sensor)(struct sd *sd, const u8 address,
	      u8 *i2c_data, const u8 len);

	/* Instructs the sensor to start streaming */
	int (*start)(struct sd *sd);

	/* Instructs the sensor to stop streaming */
	int (*stop)(struct sd *sd);

	/* Instructs the sensor to dump all its contents */
	int (*dump)(struct sd *sd);
};

#endif
