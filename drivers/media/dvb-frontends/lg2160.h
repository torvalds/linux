/*
 *    Support for LG2160 - ATSC/MH
 *
 *    Copyright (C) 2010 Michael Krufky <mkrufky@linuxtv.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 */

#ifndef _LG2160_H_
#define _LG2160_H_

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

enum lg_chip_type {
	LG2160 = 0,
	LG2161 = 1,
};

#define LG2161_1019 LG2161
#define LG2161_1040 LG2161

enum lg2160_spi_clock {
	LG2160_SPI_3_125_MHZ = 0,
	LG2160_SPI_6_25_MHZ = 1,
	LG2160_SPI_12_5_MHZ = 2,
};

#if 0
enum lg2161_oif {
	LG2161_OIF_EBI2_SLA  = 1,
	LG2161_OIF_SDIO_SLA  = 2,
	LG2161_OIF_SPI_SLA   = 3,
	LG2161_OIF_SPI_MAS   = 4,
	LG2161_OIF_SERIAL_TS = 7,
};
#endif

struct lg2160_config {
	u8 i2c_addr;

	/* user defined IF frequency in KHz */
	u16 if_khz;

	/* disable i2c repeater - 0:repeater enabled 1:repeater disabled */
	unsigned int deny_i2c_rptr:1;

	/* spectral inversion - 0:disabled 1:enabled */
	unsigned int spectral_inversion:1;

	unsigned int output_if;
	enum lg2160_spi_clock spi_clock;
	enum lg_chip_type lg_chip;
};

#if IS_REACHABLE(CONFIG_DVB_LG2160)
extern
struct dvb_frontend *lg2160_attach(const struct lg2160_config *config,
				     struct i2c_adapter *i2c_adap);
#else
static inline
struct dvb_frontend *lg2160_attach(const struct lg2160_config *config,
				     struct i2c_adapter *i2c_adap)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_LG2160 */

#endif /* _LG2160_H_ */
