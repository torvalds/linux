/*
 * Copyright 2009 Texas Instruments.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ARCH_ARM_DAVINCI_SPI_H
#define __ARCH_ARM_DAVINCI_SPI_H

enum {
	SPI_VERSION_1, /* For DM355/DM365/DM6467 */
	SPI_VERSION_2, /* For DA8xx */
};

struct davinci_spi_platform_data {
	u8	version;
	u8	num_chipselect;
	u8	wdelay;
	u8	odd_parity;
	u8	parity_enable;
	u8	wait_enable;
	u8	timer_disable;
	u8	clk_internal;
	u8	cs_hold;
	u8	intr_level;
	u8	poll_mode;
	u8	use_dma;
	u8	c2tdelay;
	u8	t2cdelay;
};

#endif	/* __ARCH_ARM_DAVINCI_SPI_H */
