/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 **********************************************************************/

/*!  \file octeon_mem_ops.h
 *   \brief Host Driver: Routines used to read/write Octeon memory.
 */

#ifndef __OCTEON_MEM_OPS_H__
#define __OCTEON_MEM_OPS_H__

/**  Read a 64-bit value from a BAR1 mapped core memory address.
 *   @param  oct        -  pointer to the octeon device.
 *   @param  core_addr  -  the address to read from.
 *
 *   The range_idx gives the BAR1 index register for the range of address
 *   in which core_addr is mapped.
 *
 *   @return  64-bit value read from Core memory
 */
u64 octeon_read_device_mem64(struct octeon_device *oct, u64 core_addr);

/**  Read a 32-bit value from a BAR1 mapped core memory address.
 *   @param  oct        -  pointer to the octeon device.
 *   @param  core_addr  -  the address to read from.
 *
 *   @return  32-bit value read from Core memory
 */
u32 octeon_read_device_mem32(struct octeon_device *oct, u64 core_addr);

/**  Write a 32-bit value to a BAR1 mapped core memory address.
 *   @param  oct        -  pointer to the octeon device.
 *   @param  core_addr  -  the address to write to.
 *   @param  val        -  32-bit value to write.
 */
void
octeon_write_device_mem32(struct octeon_device *oct,
			  u64 core_addr,
			  u32 val);

/** Read multiple bytes from Octeon memory.
 */
void
octeon_pci_read_core_mem(struct octeon_device *oct,
			 u64 coreaddr,
			 u8 *buf,
			 u32 len);

/** Write multiple bytes into Octeon memory.
 */
void
octeon_pci_write_core_mem(struct octeon_device *oct,
			  u64 coreaddr,
			  u8 *buf,
			  u32 len);

#endif
