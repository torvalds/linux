/*
 * Xilinx SPI device driver API and platform data header file
 *
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#ifndef _XILINX_SPI_H_
#define _XILINX_SPI_H_

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#define XILINX_SPI_NAME "xilinx_spi"

struct spi_master *xilinx_spi_init(struct device *dev, struct resource *mem,
	u32 irq, s16 bus_num);

void xilinx_spi_deinit(struct spi_master *master);
#endif
