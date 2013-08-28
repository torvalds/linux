/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005 Fen Systems Ltd.
 * Copyright 2006-2010 Solarflare Communications Inc.
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
#define SPI_SST_EWSR 0x50	/* SST: Enable write to status register */

#define SPI_STATUS_WPEN 0x80	/* Write-protect pin enabled */
#define SPI_STATUS_BP2 0x10	/* Block protection bit 2 */
#define SPI_STATUS_BP1 0x08	/* Block protection bit 1 */
#define SPI_STATUS_BP0 0x04	/* Block protection bit 0 */
#define SPI_STATUS_WEN 0x02	/* State of the write enable latch */
#define SPI_STATUS_NRDY 0x01	/* Device busy flag */

/**
 * struct falcon_spi_device - a Falcon SPI (Serial Peripheral Interface) device
 * @device_id:		Controller's id for the device
 * @size:		Size (in bytes)
 * @addr_len:		Number of address bytes in read/write commands
 * @munge_address:	Flag whether addresses should be munged.
 *	Some devices with 9-bit addresses (e.g. AT25040A EEPROM)
 *	use bit 3 of the command byte as address bit A8, rather
 *	than having a two-byte address.  If this flag is set, then
 *	commands should be munged in this way.
 * @erase_command:	Erase command (or 0 if sector erase not needed).
 * @erase_size:		Erase sector size (in bytes)
 *	Erase commands affect sectors with this size and alignment.
 *	This must be a power of two.
 * @block_size:		Write block size (in bytes).
 *	Write commands are limited to blocks with this size and alignment.
 */
struct falcon_spi_device {
	int device_id;
	unsigned int size;
	unsigned int addr_len;
	unsigned int munge_address:1;
	u8 erase_command;
	unsigned int erase_size;
	unsigned int block_size;
};

static inline bool falcon_spi_present(const struct falcon_spi_device *spi)
{
	return spi->size != 0;
}

int falcon_spi_cmd(struct efx_nic *efx,
		   const struct falcon_spi_device *spi, unsigned int command,
		   int address, const void *in, void *out, size_t len);
int falcon_spi_wait_write(struct efx_nic *efx,
			  const struct falcon_spi_device *spi);
int falcon_spi_read(struct efx_nic *efx,
		    const struct falcon_spi_device *spi, loff_t start,
		    size_t len, size_t *retlen, u8 *buffer);
int falcon_spi_write(struct efx_nic *efx,
		     const struct falcon_spi_device *spi, loff_t start,
		     size_t len, size_t *retlen, const u8 *buffer);

/*
 * SFC4000 flash is partitioned into:
 *     0-0x400       chip and board config (see falcon_hwdefs.h)
 *     0x400-0x8000  unused (or may contain VPD if EEPROM not present)
 *     0x8000-end    boot code (mapped to PCI expansion ROM)
 * SFC4000 small EEPROM (size < 0x400) is used for VPD only.
 * SFC4000 large EEPROM (size >= 0x400) is partitioned into:
 *     0-0x400       chip and board config
 *     configurable  VPD
 *     0x800-0x1800  boot config
 * Aside from the chip and board config, all of these are optional and may
 * be absent or truncated depending on the devices used.
 */
#define FALCON_NVCONFIG_END 0x400U
#define FALCON_FLASH_BOOTCODE_START 0x8000U
#define FALCON_EEPROM_BOOTCONFIG_START 0x800U
#define FALCON_EEPROM_BOOTCONFIG_END 0x1800U

#endif /* EFX_SPI_H */
