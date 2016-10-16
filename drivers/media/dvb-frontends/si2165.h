/*
    Driver for Silicon Labs SI2165 DVB-C/-T Demodulator

    Copyright (C) 2013-2014 Matthias Schwarzott <zzam@gentoo.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    References:
    http://www.silabs.com/Support%20Documents/TechnicalDocs/Si2165-short.pdf
*/

#ifndef _DVB_SI2165_H
#define _DVB_SI2165_H

#include <linux/dvb/frontend.h>

enum {
	SI2165_MODE_OFF = 0x00,
	SI2165_MODE_PLL_EXT = 0x20,
	SI2165_MODE_PLL_XTAL = 0x21
};

/* I2C addresses
 * possible values: 0x64,0x65,0x66,0x67
 */
struct si2165_platform_data {
	/*
	 * frontend
	 * returned by driver
	 */
	struct dvb_frontend **fe;

	/* external clock or XTAL */
	u8 chip_mode;

	/* frequency of external clock or xtal in Hz
	 * possible values: 4000000, 16000000, 20000000, 240000000, 27000000
	 */
	u32 ref_freq_Hz;

	/* invert the spectrum */
	bool inversion;
};

#endif /* _DVB_SI2165_H */
