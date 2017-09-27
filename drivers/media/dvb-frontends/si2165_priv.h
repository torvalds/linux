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

*/

#ifndef _DVB_SI2165_PRIV
#define _DVB_SI2165_PRIV

#define SI2165_FIRMWARE_REV_D "dvb-demod-si2165.fw"

struct si2165_config {
	/* i2c addr
	 * possible values: 0x64,0x65,0x66,0x67 */
	u8 i2c_addr;

	/* external clock or XTAL */
	u8 chip_mode;

	/* frequency of external clock or xtal in Hz
	 * possible values: 4000000, 16000000, 20000000, 240000000, 27000000
	 */
	u32 ref_freq_Hz;

	/* invert the spectrum */
	bool inversion;
};

#endif /* _DVB_SI2165_PRIV */
