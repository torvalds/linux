/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_kernel_interface.h
 */

#ifndef __UMP_KERNEL_INTERFACE_REF_DRV_H__
#define __UMP_KERNEL_INTERFACE_REF_DRV_H__

#include "ump_kernel_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Turn specified physical memory into UMP memory. */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_phys_blocks(ump_dd_physical_block * blocks, unsigned long num_blocks);
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_get(ump_secure_id secure_id);
UMP_KERNEL_API_EXPORT ump_dd_status_code ump_dd_meminfo_set(ump_dd_handle memh, void* args);
UMP_KERNEL_API_EXPORT void *ump_dd_meminfo_get(ump_secure_id secure_id, void* args);
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_get_from_vaddr(unsigned long vaddr);

#ifdef __cplusplus
}
#endif

#endif  /* __UMP_KERNEL_INTERFACE_REF_DRV_H__ */
