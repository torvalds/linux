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


#ifndef __ARCH_TRIO_SHM_H__
#define __ARCH_TRIO_SHM_H__

#include <arch/abi.h>
#include <arch/trio_shm_def.h>

#ifndef __ASSEMBLER__
/**
 * TRIO DMA Descriptor.
 * The TRIO DMA descriptor is written by software and consumed by hardware.
 * It is used to specify the location of transaction data in the IO and Tile
 * domains.
 */

__extension__
typedef union
{
  struct
  {
    /* Word 0 */

#ifndef __BIG_ENDIAN__
    /** Tile side virtual address. */
    int_reg_t va           : 42;
    /**
     * Encoded size of buffer used on push DMA when C=1:
     * 0 = 128 bytes
     * 1 = 256 bytes
     * 2 = 512 bytes
     * 3 = 1024 bytes
     * 4 = 1664 bytes
     * 5 = 4096 bytes
     * 6 = 10368 bytes
     * 7 = 16384 bytes
     */
    uint_reg_t bsz          : 3;
    /**
     * Chaining designation.  Always zero for pull DMA
     * 0 : Unchained buffer pointer
     * 1 : Chained buffer pointer.  Next buffer descriptor (e.g. VA) stored
     * in 1st 8-bytes in buffer.  For chained buffers, first 8-bytes of each
     * buffer contain the next buffer descriptor formatted exactly like a PDE
     * buffer descriptor.  This allows a chained PDE buffer to be sent using
     * push DMA.
     */
    uint_reg_t c            : 1;
    /**
     * Notification interrupt will be delivered when the transaction has
     * completed (all data has been read from or written to the Tile-side
     * buffer).
     */
    uint_reg_t notif        : 1;
    /**
     * When 0, the XSIZE field specifies the total byte count for the
     * transaction.  When 1, the XSIZE field is encoded as 2^(N+14) for N in
     * {0..6}:
     * 0 = 16KB
     * 1 = 32KB
     * 2 = 64KB
     * 3 = 128KB
     * 4 = 256KB
     * 5 = 512KB
     * 6 = 1MB
     * All other encodings of the XSIZE field are reserved when SMOD=1
     */
    uint_reg_t smod         : 1;
    /**
     * Total number of bytes to move for this transaction.   When SMOD=1,
     * this field is encoded - see SMOD description.
     */
    uint_reg_t xsize        : 14;
    /** Reserved. */
    uint_reg_t __reserved_0 : 1;
    /**
     * Generation number.  Used to indicate a valid descriptor in ring.  When
     * a new descriptor is written into the ring, software must toggle this
     * bit.  The net effect is that the GEN bit being written into new
     * descriptors toggles each time the ring tail pointer wraps.
     */
    uint_reg_t gen          : 1;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t gen          : 1;
    uint_reg_t __reserved_0 : 1;
    uint_reg_t xsize        : 14;
    uint_reg_t smod         : 1;
    uint_reg_t notif        : 1;
    uint_reg_t c            : 1;
    uint_reg_t bsz          : 3;
    int_reg_t va           : 42;
#endif

    /* Word 1 */

#ifndef __BIG_ENDIAN__
    /** IO-side address */
    uint_reg_t io_address : 64;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t io_address : 64;
#endif

  };

  /** Word access */
  uint_reg_t words[2];
} TRIO_DMA_DESC_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_TRIO_SHM_H__) */
