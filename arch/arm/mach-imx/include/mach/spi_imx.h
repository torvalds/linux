/*
 * arch/arm/mach-imx/include/mach/spi_imx.h
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 *
 * Initial version inspired by:
 *	linux-2.6.17-rc3-mm1/arch/arm/mach-pxa/include/mach/pxa2xx_spi.h
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

#ifndef SPI_IMX_H_
#define SPI_IMX_H_


/*-------------------------------------------------------------------------*/
/**
 * struct spi_imx_master - device.platform_data for SPI controller devices.
 * @num_chipselect: chipselects are used to distinguish individual
 *	SPI slaves, and are numbered from zero to num_chipselects - 1.
 *	each slave has a chipselect signal, but it's common that not
 *	every chipselect is connected to a slave.
 * @enable_dma: if true enables DMA driven transfers.
*/
struct spi_imx_master {
	u8 num_chipselect;
	u8 enable_dma:1;
};
/*-------------------------------------------------------------------------*/


/*-------------------------------------------------------------------------*/
/**
 * struct spi_imx_chip - spi_board_info.controller_data for SPI
 * slave devices, copied to spi_device.controller_data.
 * @enable_loopback : used for test purpouse to internally connect RX and TX
 *	sections.
 * @enable_dma : enables dma transfer (provided that controller driver has
 *	dma enabled too).
 * @ins_ss_pulse : enable /SS pulse insertion between SPI burst.
 * @bclk_wait : number of bclk waits between each bits_per_word SPI burst.
 * @cs_control : function pointer to board-specific function to assert/deassert
 *	I/O port to control HW generation of devices chip-select.
*/
struct spi_imx_chip {
	u8	enable_loopback:1;
	u8	enable_dma:1;
	u8	ins_ss_pulse:1;
	u16	bclk_wait:15;
	void (*cs_control)(u32 control);
};

/* Chip-select state */
#define SPI_CS_ASSERT			(1 << 0)
#define SPI_CS_DEASSERT			(1 << 1)
/*-------------------------------------------------------------------------*/


#endif /* SPI_IMX_H_*/
