/*
 * Copyright (C) 2010, 2013, 2015-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_SECURE_H__
#define __MALI_MEMORY_SECURE_H__

#include "mali_session.h"
#include "mali_memory.h"
#include <linux/spinlock.h>

#include "mali_memory_types.h"

_mali_osk_errcode_t mali_mem_secure_attach_dma_buf(mali_mem_secure *secure_mem, u32 size, int mem_fd);

_mali_osk_errcode_t mali_mem_secure_mali_map(mali_mem_secure *secure_mem, struct mali_session_data *session, u32 vaddr, u32 props);

void mali_mem_secure_mali_unmap(mali_mem_allocation *alloc);

int mali_mem_secure_cpu_map(mali_mem_backend *mem_bkend, struct vm_area_struct *vma);

u32 mali_mem_secure_release(mali_mem_backend *mem_bkend);

#endif /* __MALI_MEMORY_SECURE_H__ */
