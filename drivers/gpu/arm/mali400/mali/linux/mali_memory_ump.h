/*
 * Copyright (C) 2011-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_UMP_BUF_H__
#define __MALI_MEMORY_UMP_BUF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "mali_uk_types.h"
#include "mali_osk.h"
#include "mali_memory.h"

int mali_mem_bind_ump_buf(mali_mem_allocation *alloc, mali_mem_backend *mem_backend, u32  secure_id, u32 flags);
void mali_mem_unbind_ump_buf(mali_mem_backend *mem_backend);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_MEMORY_DMA_BUF_H__ */
