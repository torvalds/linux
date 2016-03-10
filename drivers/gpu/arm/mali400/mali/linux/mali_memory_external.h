
/*
 * Copyright (C) 2011-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_EXTERNAL_H__
#define __MALI_MEMORY_EXTERNAL_H__

#ifdef __cplusplus
extern "C" {
#endif

_mali_osk_errcode_t mali_mem_bind_ext_buf(mali_mem_allocation *alloc,
		mali_mem_backend *mem_backend,
		u32 phys_addr,
		u32 flag);
void mali_mem_unbind_ext_buf(mali_mem_backend *mem_backend);

#ifdef __cplusplus
}
#endif

#endif
