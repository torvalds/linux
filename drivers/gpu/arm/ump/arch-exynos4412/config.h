/*
 * Copyright (C) 2010-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

/*
ARCH_UMP_BACKEND_DEFAULT
    0 specifies the dedicated memory allocator.
    1 specifies the OS memory allocator.
ARCH_UMP_MEMORY_ADDRESS_DEFAULT
    This is only required for the dedicated memory allocator, and specifies
    the physical start address of the memory block reserved for UMP.
ARCH_UMP_MEMORY_SIZE_DEFAULT
    This specified the size of the memory block reserved for UMP, or the
    maximum limit for allocations from the OS.
*/

#define ARCH_UMP_BACKEND_DEFAULT          1
#define ARCH_UMP_MEMORY_ADDRESS_DEFAULT   0xE1000000
#define ARCH_UMP_MEMORY_SIZE_DEFAULT 16UL * 1024UL * 1024UL

#endif /* __ARCH_CONFIG_H__ */
