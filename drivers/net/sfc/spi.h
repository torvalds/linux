/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005 Fen Systems Ltd.
 * Copyright 2006 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_SPI_H
#define EFX_SPI_H

#include "net_driver.h"

/**************************************************************************
 *
 * Basic SPI command set and bit definitions
 *
 *************************************************************************/

#define SPI_WRSR 0x01		/* Write status register */
#define SPI_WRITE 0x02		/* Write data to memory array */
#define SPI_READ 0x03		/* Read data from memory array */
#define SPI_WRDI 0x04		/* Reset write enable latch */
#define SPI_RDSR 0x05		/* Read status register */
#define SPI_WREN 0x06		/* Set write enable latch */

#define SPI_STATUS_WPEN 0x80	/* Write-protect pin enabled */
#define SPI_STATUS_BP2 0x10	/* Block protection bit 2 */
#define SPI_STATUS_BP1 0x08	/* Block protection bit 1 */
#define SPI_STATUS_BP0 0x04	/* Block protection bit 0 */
#define SPI_STATUS_WEN 0x02	/* State of the write enable latch */
#define SPI_STATUS_NRDY 0x01	/* Device busy flag */

/**
 * struct efx_spi_device - an Efx SPI (Serial Peripheral Interface) device
 * @efx:		The Efx controller that owns this device
 * @device_id:		Controller's id for the device
 * @size:		Size (in bytes)
 * @addr_len:		Number of address bytes in read/write commands
 * @munge_address:	Flag whether addresses should be munged.
 *	Some devices with 9-bit addresses (e.g. AT25040A EEPROM)
 *	use bit 3 of the command byte as address bit A8, rather
 *	than having a two-byte address.  If this flag is set, then
 *	commands should be munged in this way.
 * @block_size:		Write block size (in bytes).
 *	Write commands are limited to blocks with this size and alignment.
 * @read:		Read function for the device
 * @write:		Write function for the device
 */
struct efx_spi_device {
	struct efx_nic *efx;
	int device_id;
	unsigned int size;
	unsigned int addr_len;
	unsigned int munge_address:1;
	unsigned int block_size;
};

int falcon_spi_read(const struct efx_spi_device *spi, loff_t start,
		    size_t len, size_t *retlen, u8 *buffer);
int falcon_spi_write(const struct efx_spi_device *spi, loff_t start,
		     size_t len, size_t *retlen, const u8 *buffer);

#endif /* EFX_SPI_H */
