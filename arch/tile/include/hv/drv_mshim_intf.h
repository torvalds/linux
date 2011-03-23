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
 * @file drv_mshim_intf.h
 * Interface definitions for the Linux EDAC memory controller driver.
 */

#ifndef _SYS_HV_INCLUDE_DRV_MSHIM_INTF_H
#define _SYS_HV_INCLUDE_DRV_MSHIM_INTF_H

/** Number of memory controllers in the public API. */
#define TILE_MAX_MSHIMS 4

/** Memory info under each memory controller. */
struct mshim_mem_info
{
  uint64_t mem_size;     /**< Total memory size in bytes. */
  uint8_t mem_type;      /**< Memory type, DDR2 or DDR3. */
  uint8_t mem_ecc;       /**< Memory supports ECC. */
};

/**
 * DIMM error structure.
 * For now, only correctable errors are counted and the mshim doesn't record
 * the error PA. HV takes panic upon uncorrectable errors.
 */
struct mshim_mem_error
{
  uint32_t sbe_count;     /**< Number of single-bit errors. */
};

/** Read this offset to get the memory info per mshim. */
#define MSHIM_MEM_INFO_OFF 0x100

/** Read this offset to check DIMM error. */
#define MSHIM_MEM_ERROR_OFF 0x200

#endif /* _SYS_HV_INCLUDE_DRV_MSHIM_INTF_H */
