/*
 *  Driver for Microtune MT2060 "Single chip dual conversion broadband tuner"
 *
 *  Copyright (c) 2006 Olivier DANET <odanet@caramail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#ifndef MT2060_H
#define MT2060_H

struct dvb_frontend;
struct i2c_adapter;

/*
 * I2C address
 * 0x60, ...
 */

/**
 * struct mt2060_platform_data - Platform data for the mt2060 driver
 * @clock_out: Clock output setting. 0 = off, 1 = CLK/4, 2 = CLK/2, 3 = CLK/1.
 * @if1: First IF used [MHz]. 0 defaults to 1220.
 * @i2c_write_max: Maximum number of bytes I2C adapter can write at once.
 *  0 defaults to maximum.
 * @dvb_frontend: DVB frontend.
 */

struct mt2060_platform_data {
	u8 clock_out;
	u16 if1;
	unsigned int i2c_write_max:5;
	struct dvb_frontend *dvb_frontend;
};


/* configuration struct for mt2060_attach() */
struct mt2060_config {
	u8 i2c_address;
	u8 clock_out; /* 0 = off, 1 = CLK/4, 2 = CLK/2, 3 = CLK/1 */
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_MT2060)
extern struct dvb_frontend * mt2060_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct mt2060_config *cfg, u16 if1);
#else
static inline struct dvb_frontend * mt2060_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct mt2060_config *cfg, u16 if1)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_MEDIA_TUNER_MT2060

#endif
