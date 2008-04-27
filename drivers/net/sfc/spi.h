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

/*
 * Commands common to all known devices.
 *
 */

/* Write status register */
#define SPI_WRSR 0x01

/* Write data to memory array */
#define SPI_WRITE 0x02

/* Read data from memory array */
#define SPI_READ 0x03

/* Reset write enable latch */
#define SPI_WRDI 0x04

/* Read status register */
#define SPI_RDSR 0x05

/* Set write enable latch */
#define SPI_WREN 0x06

/* SST: Enable write to status register */
#define SPI_SST_EWSR 0x50

/*
 * Status register bits.  Not all bits are supported on all devices.
 *
 */

/* Write-protect pin enabled */
#define SPI_STATUS_WPEN 0x80

/* Block protection bit 2 */
#define SPI_STATUS_BP2 0x10

/* Block protection bit 1 */
#define SPI_STATUS_BP1 0x08

/* Block protection bit 0 */
#define SPI_STATUS_BP0 0x04

/* State of the write enable latch */
#define SPI_STATUS_WEN 0x02

/* Device busy flag */
#define SPI_STATUS_NRDY 0x01

#endif /* EFX_SPI_H */
