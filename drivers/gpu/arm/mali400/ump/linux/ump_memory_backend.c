/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2010, 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include <linux/module.h>            /* kernel module definitions */
#include <linux/ioport.h>            /* request_mem_region */

#include "arch/config.h"             /* Configuration for current platform. The symlink for arch is set by Makefile */

#include "ump_osk.h"
#include "ump_kernel_common.h"
#include "ump_kernel_memory_backend_os.h"
#include "ump_kernel_memory_backend_dedicated.h"
extern unsigned long totalram_pages;

/* Configure which dynamic memory allocator to use */
int ump_backend = ARCH_UMP_BACKEND_DEFAULT;
module_param(ump_backend, int, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(ump_backend, "0 = dedicated memory backend (default), 1 = OS memory backend");

/* The base address of the memory block for the dedicated memory backend */
unsigned int ump_memory_address = ARCH_UMP_MEMORY_ADDRESS_DEFAULT;
module_param(ump_memory_address, uint, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(ump_memory_address, "The physical address to map for the dedicated memory backend");

/* The size of the memory block for the dedicated memory backend */
unsigned int ump_memory_size = ARCH_UMP_MEMORY_SIZE_DEFAULT;
module_param(ump_memory_size, uint, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(ump_memory_size, "The size of fixed memory to map in the dedicated memory backend");

ump_memory_backend* ump_memory_backend_create ( void )
{
	ump_memory_backend * backend = NULL;

	/* Create the dynamic memory allocator backend */
	if (0 == ump_backend) {
		DBG_MSG(2, ("Using dedicated memory backend\n"));

		DBG_MSG(2, ("Requesting dedicated memory: 0x%08x, size: %u\n", ump_memory_address, ump_memory_size));
		/* Ask the OS if we can use the specified physical memory */
		if (NULL == request_mem_region(ump_memory_address, ump_memory_size, "UMP Memory")) {
			MSG_ERR(("Failed to request memory region (0x%08X - 0x%08X). Is Mali DD already loaded?\n", ump_memory_address, ump_memory_address + ump_memory_size - 1));
			return NULL;
		}
		backend = ump_block_allocator_create(ump_memory_address, ump_memory_size);
	} else if (1 == ump_backend) {
		ump_memory_size = totalram_pages * 4 * 1024 / 2;
		DBG_MSG(2, ("Using OS memory backend, allocation limit: %d\n", ump_memory_size));
		backend = ump_os_memory_backend_create(ump_memory_size);
	}

	return backend;
}

void ump_memory_backend_destroy( void )
{
	if (0 == ump_backend) {
		DBG_MSG(2, ("Releasing dedicated memory: 0x%08x\n", ump_memory_address));
		release_mem_region(ump_memory_address, ump_memory_size);
	}
}
