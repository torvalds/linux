/*
 * Copyright (C) 2010, 2012, 2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

/* Use OS memory. */
#define ARCH_UMP_BACKEND_DEFAULT          1

/* OS memory won't need a base address. */
#define ARCH_UMP_MEMORY_ADDRESS_DEFAULT   0x00000000

/* 512 MB maximum limit for UMP allocations. */
#define ARCH_UMP_MEMORY_SIZE_DEFAULT 512UL * 1024UL * 1024UL


#endif /* __ARCH_CONFIG_H__ */
