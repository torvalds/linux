/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2011, 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_kernel_memory_mapping.h
 */

#ifndef __UMP_KERNEL_MEMORY_BACKEND_H__
#define __UMP_KERNEL_MEMORY_BACKEND_H__

#include "ump_kernel_interface.h"
#include "ump_kernel_types.h"


typedef struct ump_memory_allocation {
	void                    * phys_addr;
	void                    * mapping;
	unsigned long             size;
	ump_dd_handle             handle;
	void                    * process_mapping_info;
	u32                       cookie;               /**< necessary on some U/K interface implementations */
	struct ump_session_data * ump_session;          /**< Session that this allocation belongs to */
	_mali_osk_list_t          list;                 /**< List for linking together memory allocations into the session's memory head */
	u32 is_cached;
} ump_memory_allocation;

typedef struct ump_memory_backend {
	int  (*allocate)(void* ctx, ump_dd_mem * descriptor);
	void (*release)(void* ctx, ump_dd_mem * descriptor);
	void (*shutdown)(struct ump_memory_backend * backend);
	u32  (*stat)(struct ump_memory_backend *backend);
	int  (*pre_allocate_physical_check)(void *ctx, u32 size);
	u32  (*adjust_to_mali_phys)(void *ctx, u32 cpu_phys);
	void * ctx;
} ump_memory_backend;

ump_memory_backend * ump_memory_backend_create ( void );
void ump_memory_backend_destroy( void );

#endif /*__UMP_KERNEL_MEMORY_BACKEND_H__ */

