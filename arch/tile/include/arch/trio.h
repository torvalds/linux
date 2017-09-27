/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
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

/* Machine-generated file; do not edit. */

#ifndef __ARCH_TRIO_H__
#define __ARCH_TRIO_H__

#include <arch/abi.h>
#include <arch/trio_def.h>

#ifndef __ASSEMBLER__

/*
 * Map SQ Doorbell Format.
 * This describes the format of the write-only doorbell register that exists
 * in the last 8-bytes of the MAP_SQ_BASE/LIM range.  This register is only
 * writable from PCIe space.  Writes to this register will not be written to
 * Tile memory space and thus no IO VA translation is required if the last
 * page of the BASE/LIM range is not otherwise written.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * When written with a 1, the associated MAP_SQ region's doorbell
     * interrupt will be triggered once all previous writes are visible to
     * Tile software.
     */
    uint_reg_t doorbell   : 1;
    /*
     * When written with a 1, the descriptor at the head of the associated
     * MAP_SQ's FIFO will be dequeued.
     */
    uint_reg_t pop        : 1;
    /* Reserved. */
    uint_reg_t __reserved : 62;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved : 62;
    uint_reg_t pop        : 1;
    uint_reg_t doorbell   : 1;
#endif
  };

  uint_reg_t word;
} TRIO_MAP_SQ_DOORBELL_FMT_t;


/*
 * Tile PIO Region Configuration - CFG Address Format.
 * This register describes the address format for PIO accesses when the
 * associated region is setup with TYPE=CFG.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Register Address (full byte address). */
    uint_reg_t reg_addr     : 12;
    /* Function Number */
    uint_reg_t fn           : 3;
    /* Device Number */
    uint_reg_t dev          : 5;
    /* BUS Number */
    uint_reg_t bus          : 8;
    /* Config Type: 0 for access to directly-attached device.  1 otherwise. */
    uint_reg_t type         : 1;
    /* Reserved. */
    uint_reg_t __reserved_0 : 1;
    /*
     * MAC select.  This must match the configuration in
     * TILE_PIO_REGION_SETUP.MAC.
     */
    uint_reg_t mac          : 2;
    /* Reserved. */
    uint_reg_t __reserved_1 : 32;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_1 : 32;
    uint_reg_t mac          : 2;
    uint_reg_t __reserved_0 : 1;
    uint_reg_t type         : 1;
    uint_reg_t bus          : 8;
    uint_reg_t dev          : 5;
    uint_reg_t fn           : 3;
    uint_reg_t reg_addr     : 12;
#endif
  };

  uint_reg_t word;
} TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_TRIO_H__) */
