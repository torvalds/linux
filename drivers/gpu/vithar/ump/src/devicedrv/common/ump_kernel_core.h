/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _UMP_KERNEL_CORE_H_
#define _UMP_KERNEL_CORE_H_


#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/cred.h>
#include <asm/mmu_context.h>



#include <ump/ump_common.h>
#include <ump/src/devicedrv/common/ump_kernel_descriptor_mapping.h>

/* forward decl */
struct umpp_session;

/**
 * UMP handle metadata.
 * Tracks various data about a handle not of any use to user space
 */
typedef enum
{
	UMP_MGMT_EXTERNAL = (1ul << 0) /**< Handle created via the ump_dd_create_from_phys_blocks interface */
	/* (1ul << 31) not to be used */
} umpp_management_flags;

/**
 * Structure tracking the single global UMP device.
 * Holds global data like the ID map
 */
typedef struct umpp_device
{
	struct mutex secure_id_map_lock; /**< Lock protecting access to the map */
	umpp_descriptor_mapping * secure_id_map; /**< Map of all known secure IDs on the system */
} umpp_device;

/**
 * Structure tracking all memory allocations of a UMP allocation.
 * Tracks info about an mapping so we can verify cache maintenace
 * operations and help in the unmap cleanup.
 */
typedef struct umpp_cpu_mapping
{
	struct list_head        link; /**< link to list of mappings for an allocation */
	void                  *vaddr_start; /**< CPU VA start of the mapping */
	size_t                nr_pages; /**< Size (in pages) of the mapping */
	uint64_t              page_off; /**< Offset (in pages) from start of the allocation where the mapping starts */
	ump_dd_handle         handle; /**< Which handle this mapping is linked to */
	struct umpp_session * session; /**< Which session created the mapping */
} umpp_cpu_mapping;

/**
 * Structure tracking UMP allocation.
 * Represent a memory allocation with its ID.
 * Tracks all needed meta-data about an allocation.
 * */
typedef struct umpp_allocation
{
	ump_secure_id id; /**< Secure ID of the allocation */
	atomic_t refcount; /**< Usage count */

	ump_alloc_flags flags; /**< Flags for all supported devices */
	uint32_t management_flags; /**< Managment flags tracking */

	pid_t owner; /**< The process ID owning the memory if not sharable */

	ump_dd_security_filter filter_func; /**< Hook to verify use, called during retains from new clients */
	ump_dd_final_release_callback final_release_func; /**< Hook called when the last reference is removed */
	void* callback_data; /**< Additional data given to release hook */

	uint64_t size; /**< Size (in bytes) of the allocation */
	uint64_t blocksCount; /**< Number of physsical blocks the allocation is built up of */
	ump_dd_physical_block_64 * block_array; /**< Array, one entry per block, describing block start and length */

	struct list_head map_list; /**< Trackers all CPU VA mappings of this allocation */

	void * backendData; /**< Physical memory backend meta-data */
} umpp_allocation;

/**
 * Structure tracking use of UMP memory by a session.
 * Tracks the use of an allocation by a session so session termination can clean up any outstanding references.
 * Also protects agains non-matched release calls from user space.
 */
typedef struct umpp_session_memory_usage
{
	ump_secure_id id; /**< ID being used. For quick look-up */
	ump_dd_handle mem; /**< Handle being used. */

	/**
	 * Track how many times has the process retained this handle in the kernel.
	 * This should usually just be 1(allocated or resolved) or 2(mapped),
	 * but could be more if someone is playing with the low-level API
	 * */
	atomic_t process_usage_count;

	struct list_head link; /**< link to other usage trackers for a session */
} umpp_session_memory_usage;

/**
 * Structure representing a session/client.
 * Tracks the UMP allocations being used by this client.
 */
typedef struct umpp_session
{
	struct mutex session_lock; /**< Lock for memory usage manipulation */
	struct list_head memory_usage; /**< list of memory currently being used by the this session */
	void*  import_handler_data[UMPP_EXTERNAL_MEM_COUNT]; /**< Import modules per-session data pointer */
} umpp_session;

/**
 * UMP core setup.
 * Called by any OS specific startup function to initialize the common part.
 * @return UMP_OK if core initialized correctly, any other value for errors
 */
ump_result umpp_core_constructor(void);

/**
 * UMP core teardown.
 * Called by any OS specific unload function to clean up the common part.
 */
void umpp_core_destructor(void);

/**
 * UMP session start.
 * Called by any OS specific session handler when a new session is detected
 * @return Non-NULL if a matching core session could be set up. NULL on failure
 */
umpp_session *umpp_core_session_start(void);

/**
 * UMP session end.
 * Called by any OS specific session handler when a session is ended/terminated.
 * @param session The core session object returned by ump_core_session_start
 */
void umpp_core_session_end(umpp_session *session);

/**
 * Find a mapping object (if any) for this allocation.
 * Called by any function needing to identify a mapping from a user virtual address.
 * Verifies that the whole range to be within a mapping object.
 * @param alloc The UMP allocation to find a matching mapping object of
 * @param uaddr User mapping address to find the mapping object for
 * @param size Length of the mapping
 * @return NULL on error (no match found), pointer to mapping object if match found
 */
umpp_cpu_mapping * umpp_dd_find_enclosing_mapping(umpp_allocation * alloc, void* uaddr, size_t size);

/**
 * Register a new mapping of an allocation.
 * Called by functions creating a new mapping of an allocation, typically OS specific handlers.
 * @param alloc The allocation object which has been mapped
 * @param map Info about the mapping
 */
void umpp_dd_add_cpu_mapping(umpp_allocation * alloc, umpp_cpu_mapping * map);

/**
 * Remove and free mapping object from an allocation.
 * @param alloc The allocation object to remove the mapping info from
 * @param target The mapping object to remove
 */
void umpp_dd_remove_cpu_mapping(umpp_allocation * alloc, umpp_cpu_mapping * target);

/**
 * Helper to find a block in the blockArray which holds a given byte offset.
 * @param alloc The allocation object to find the block in
 * @param offset Offset (in bytes) from allocation start to find the block of
 * @param[out] block_index Pointer to the index of the block matching
 * @param[out] block_internal_offset Offset within the returned block of the searched offset
 * @return 0 if a matching block was found, any other value for error
 */
int umpp_dd_find_start_block(const umpp_allocation * alloc, uint64_t offset, uint64_t * block_index, uint64_t * block_internal_offset);

/**
 * Cache maintenance helper.
 * Performs the requested cache operation on the given handle.
 * @param mem Allocation handle
 * @param op Cache maintenance operation to perform
 * @param address User mapping at which to do the operation
 * @param size Length (in bytes) of the range to do the operation on
 */
void umpp_dd_cpu_msync_now(ump_dd_handle mem, ump_cpu_msync_op op, void * address, size_t size);

/**
 * Import module session early init.
 * Calls session_begin on all installed import modules.
 * @param session The core session object to initialized the import handler for
 * */
void umpp_import_handlers_init(umpp_session * session);

/**
 * Import module session cleanup.
 * Calls session_end on all import modules bound to the session.
 * @param session The core session object to initialized the import handler for
 */
void umpp_import_handlers_term(umpp_session * session);

#endif /* _UMP_KERNEL_CORE_H_ */

