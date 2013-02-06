/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief Large physical address extension definitions.
 *
 * See ARM PRD03-GENC-008469 11.0.
 */
#ifndef _LPAE_DEFS_H_
#define _LPAE_DEFS_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define ARM_LPAE_PT_ORDER    12

#define ARM_LPAE_PT_SIZE     (1 << ARM_LPAE_PT_ORDER)
#define ARM_LPAE_ENTRY_ORDER 3
#define ARM_LPAE_PT_ENTRIES_ORDER (ARM_LPAE_PT_ORDER - ARM_LPAE_ENTRY_ORDER)
#define ARM_LPAE_PT_ENTRIES  (1 << ARM_LPAE_PT_ENTRIES_ORDER)

#define ARM_LPAE_L1D_BLOCK_ORDER 30
#define ARM_LPAE_L2D_BLOCK_ORDER 21
#define ARM_LPAE_L3D_BLOCK_ORDER 12

#define ARM_LPAE_L1D_BLOCK_BITS (40 - ARM_LPAE_L1D_BLOCK_ORDER)
#define ARM_LPAE_L2D_BLOCK_BITS (40 - ARM_LPAE_L2D_BLOCK_ORDER)
#define ARM_LPAE_L3D_BLOCK_BITS (40 - ARM_LPAE_L3D_BLOCK_ORDER)

/*
 * Currently supporting up to 16GB PA spaces.
 */
#define ARM_LPAE_L1PT_INDX(addr) \
   MVP_EXTRACT_FIELD64(addr, ARM_LPAE_L1D_BLOCK_ORDER, 4)
#define ARM_LPAE_L2PT_INDX(addr) \
   MVP_EXTRACT_FIELD64(addr, ARM_LPAE_L2D_BLOCK_ORDER, ARM_LPAE_PT_ENTRIES_ORDER)
#define ARM_LPAE_L3PT_INDX(addr) \
   MVP_EXTRACT_FIELD64(addr, ARM_LPAE_L3D_BLOCK_ORDER, ARM_LPAE_PT_ENTRIES_ORDER)

#define ARM_LPAE_L1D_BLOCK_BASE_ADDR(base) ((base) << ARM_LPAE_L1D_BLOCK_ORDER)
#define ARM_LPAE_L1D_BLOCK_ADDR_BASE(addr) ((addr) >> ARM_LPAE_L1D_BLOCK_ORDER)
#define ARM_LPAE_L2D_BLOCK_BASE_ADDR(base) ((base) << ARM_LPAE_L2D_BLOCK_ORDER)
#define ARM_LPAE_L2D_BLOCK_ADDR_BASE(addr) ((addr) >> ARM_LPAE_L2D_BLOCK_ORDER)
#define ARM_LPAE_L3D_BLOCK_BASE_ADDR(base) ((base) << ARM_LPAE_L3D_BLOCK_ORDER)
#define ARM_LPAE_L3D_BLOCK_ADDR_BASE(addr) ((addr) >> ARM_LPAE_L3D_BLOCK_ORDER)

#define ARM_LPAE_TABLE_BASE_ADDR(base) ((base) << ARM_LPAE_PT_ORDER)
#define ARM_LPAE_TABLE_ADDR_BASE(addr) ((addr) >> ARM_LPAE_PT_ORDER)

#define ARM_LPAE_TYPE_INVALID   0
#define ARM_LPAE_TYPE_TABLE     3
#define ARM_LPAE_L1D_TYPE_BLOCK 1
#define ARM_LPAE_L2D_TYPE_BLOCK 1
#define ARM_LPAE_L3D_TYPE_BLOCK 3

/**
 * @name Second stage permission model.
 *
 * @{
 */
#define ARM_LPAE_S2_PERM_NONE   0
#define ARM_LPAE_S2_PERM_RO     1
#define ARM_LPAE_S2_PERM_WO     2
#define ARM_LPAE_S2_PERM_RW     3
/*@}*/


#endif /// ifndef _LPAE_DEFS_H_
