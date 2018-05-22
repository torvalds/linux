/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * P/N 861037:      Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 - QuickCam Express
 * P/N 861055:      Sensor ST VV6410       ASIC STV0610   - LEGO cam
 * P/N 861075-0040: Sensor HDCS1000        ASIC
 * P/N 961179-0700: Sensor ST VV6410       ASIC STV0602   - Dexxa WebCam USB
 * P/N 861040-0000: Sensor ST VV6410       ASIC STV0610   - QuickCam Web
 */

#ifndef STV06XX_H_
#define STV06XX_H_

#include <linux/slab.h>
#include "gspca.h"

#define MODULE_NAME "STV06xx"

#define STV_ISOC_ENDPOINT_ADDR		0x81

#define STV_R                           0x0509

#define STV_REG23			0x0423

/* Control registers of the STV0600 ASIC */
#define STV_I2C_PARTNER			0x1420
#define STV_I2C_VAL_REG_VAL_PAIRS_MIN1	0x1421
#define STV_I2C_READ_WRITE_TOGGLE	0x1422
#define STV_I2C_FLUSH			0x1423
#define STV_I2C_SUCC_READ_REG_VALS	0x1424

#define STV_ISO_ENABLE			0x1440
#define STV_SCAN_RATE			0x1443
#define STV_LED_CTRL			0x1445
#define STV_STV0600_EMULATION		0x1446
#define STV_REG00			0x1500
#define STV_REG01			0x1501
#define STV_REG02			0x1502
#define STV_REG03			0x1503
#define STV_REG04			0x1504

#define STV_ISO_SIZE_L			0x15c1
#define STV_ISO_SIZE_H			0x15c2

/* Refers to the CIF 352x288 and QCIF 176x144 */
/* 1: 288 lines, 2: 144 lines */
#define STV_Y_CTRL			0x15c3

#define STV_RESET                       0x1620

/* 0xa: 352 columns, 0x6: 176 columns */
#define STV_X_CTRL			0x1680

#define STV06XX_URB_MSG_TIMEOUT		5000

#define I2C_MAX_BYTES			16
#define I2C_MAX_WORDS			8

#define I2C_BUFFER_LENGTH		0x23
#define I2C_READ_CMD			3
#define I2C_WRITE_CMD			1

#define LED_ON				1
#define LED_OFF				0

/* STV06xx device descriptor */
struct sd {
	struct gspca_dev gspca_dev;

	/* A pointer to the currently connected sensor */
	const struct stv06xx_sensor *sensor;

	/* Sensor private data */
	void *sensor_priv;

	/* The first 4 lines produced by the stv6422 are no good, this keeps
	   track of how many bytes we still need to skip during a frame */
	int to_skip;

	/* Bridge / Camera type */
	u8 bridge;
	#define BRIDGE_STV600 0
	#define BRIDGE_STV602 1
	#define BRIDGE_STV610 2
	#define BRIDGE_ST6422 3 /* With integrated sensor */
};

int stv06xx_write_bridge(struct sd *sd, u16 address, u16 i2c_data);
int stv06xx_read_bridge(struct sd *sd, u16 address, u8 *i2c_data);

int stv06xx_write_sensor_bytes(struct sd *sd, const u8 *data, u8 len);
int stv06xx_write_sensor_words(struct sd *sd, const u16 *data, u8 len);

int stv06xx_read_sensor(struct sd *sd, const u8 address, u16 *value);
int stv06xx_write_sensor(struct sd *sd, u8 address, u16 value);

#endif
