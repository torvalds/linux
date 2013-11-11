/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * @file drv_srom_intf.h
 * Interface definitions for the SPI Flash ROM driver.
 */

#ifndef _SYS_HV_INCLUDE_DRV_SROM_INTF_H
#define _SYS_HV_INCLUDE_DRV_SROM_INTF_H

/** Read this offset to get the total device size. */
#define SROM_TOTAL_SIZE_OFF   0xF0000000

/** Read this offset to get the device sector size. */
#define SROM_SECTOR_SIZE_OFF  0xF0000004

/** Read this offset to get the device page size. */
#define SROM_PAGE_SIZE_OFF    0xF0000008

/** Write this offset to flush any pending writes. */
#define SROM_FLUSH_OFF        0xF1000000

/** Write this offset, plus the byte offset of the start of a sector, to
 *  erase a sector.  Any write data is ignored, but there must be at least
 *  one byte of write data.  Only applies when the driver is in MTD mode.
 */
#define SROM_ERASE_OFF        0xF2000000

#endif /* _SYS_HV_INCLUDE_DRV_SROM_INTF_H */
