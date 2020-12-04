/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Memory pools library, Public interface
 *
 * API Overview
 *
 * This package provides a memory allocation subsystem based on pools of
 * homogenous objects.
 *
 * Instrumentation is available for reporting memory utilization both
 * on a per-data-structure basis and system wide.
 *
 * There are two main types defined in this API.
 *
 *    pool manager: A singleton object that acts as a factory for
 *                  pool allocators. It also is used for global
 *                  instrumentation, such as reporting all blocks
 *                  in use across all data structures. The pool manager
 *                  creates and provides individual memory pools
 *                  upon request to application code.
 *
 *    memory pool:  An object for allocating homogenous memory blocks.
 *
 * Global identifiers in this module use the following prefixes:
 *    bcm_mpm_*     Memory pool manager
 *    bcm_mp_*      Memory pool
 *
 * There are two main types of memory pools:
 *
 *    prealloc: The contiguous memory block of objects can either be supplied
 *              by the client or malloc'ed by the memory manager. The objects are
 *              allocated out of a block of memory and freed back to the block.
 *
 *    heap:     The memory pool allocator uses the heap (malloc/free) for memory.
 *              In this case, the pool allocator is just providing statistics
 *              and instrumentation on top of the heap, without modifying the heap
 *              allocation implementation.
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcm_mpool_pub.h 514727 2014-11-12 03:02:48Z $
 */

#ifndef _BCM_MPOOL_PUB_H
#define _BCM_MPOOL_PUB_H 1

#include <typedefs.h> /* needed for uint16 */


/*
**************************************************************************
*
* Type definitions, handles
*
**************************************************************************
*/

/* Forward declaration of OSL handle. */
struct osl_info;

/* Forward declaration of string buffer. */
struct bcmstrbuf;

/*
 * Opaque type definition for the pool manager handle. This object is used for global
 * memory pool operations such as obtaining a new pool, deleting a pool, iterating and
 * instrumentation/debugging.
 */
struct bcm_mpm_mgr;
typedef struct bcm_mpm_mgr *bcm_mpm_mgr_h;

/*
 * Opaque type definition for an instance of a pool. This handle is used for allocating
 * and freeing memory through the pool, as well as management/instrumentation on this
 * specific pool.
 */
struct bcm_mp_pool;
typedef struct bcm_mp_pool *bcm_mp_pool_h;


/*
 * To make instrumentation more readable, every memory
 * pool must have a readable name. Pool names are up to
 * 8 bytes including '\0' termination. (7 printable characters.)
 */
#define BCM_MP_NAMELEN 8


/*
 * Type definition for pool statistics.
 */
typedef struct bcm_mp_stats {
	char name[BCM_MP_NAMELEN];  /* Name of this pool. */
	unsigned int objsz;         /* Object size allocated in this pool */
	uint16 nobj;                /* Total number of objects in this pool */
	uint16 num_alloc;           /* Number of objects currently allocated */
	uint16 high_water;          /* Max number of allocated objects. */
	uint16 failed_alloc;        /* Failed allocations. */
} bcm_mp_stats_t;


/*
**************************************************************************
*
* API Routines on the pool manager.
*
**************************************************************************
*/

/*
 * bcm_mpm_init() - initialize the whole memory pool system.
 *
 * Parameters:
 *    osh:       INPUT  Operating system handle. Needed for heap memory allocation.
 *    max_pools: INPUT Maximum number of mempools supported.
 *    mgr:       OUTPUT The handle is written with the new pools manager object/handle.
 *
 * Returns:
 *    BCME_OK     Object initialized successfully. May be used.
 *    BCME_NOMEM  Initialization failed due to no memory. Object must not be used.
 */
int bcm_mpm_init(struct osl_info *osh, int max_pools, bcm_mpm_mgr_h *mgrp);


/*
 * bcm_mpm_deinit() - de-initialize the whole memory pool system.
 *
 * Parameters:
 *    mgr:     INPUT  Pointer to pool manager handle.
 *
 * Returns:
 *    BCME_OK  Memory pool manager successfully de-initialized.
 *    other    Indicated error occured during de-initialization.
 */
int bcm_mpm_deinit(bcm_mpm_mgr_h *mgrp);

/*
 * bcm_mpm_create_prealloc_pool() - Create a new pool for fixed size objects. The
 *                                  pool uses a contiguous block of pre-alloced
 *                                  memory. The memory block may either be provided
 *                                  by the client or dynamically allocated by the
 *                                  pool manager.
 *
 * Parameters:
 *    mgr:      INPUT  The handle to the pool manager
 *    obj_sz:   INPUT  Size of objects that will be allocated by the new pool
 *                     Must be >= sizeof(void *).
 *    nobj:     INPUT  Maximum number of concurrently existing objects to support
 *    memstart  INPUT  Pointer to the memory to use, or NULL to malloc()
 *    memsize   INPUT  Number of bytes referenced from memstart (for error checking).
 *                     Must be 0 if 'memstart' is NULL.
 *    poolname  INPUT  For instrumentation, the name of the pool
 *    newp:     OUTPUT The handle for the new pool, if creation is successful
 *
 * Returns:
 *    BCME_OK   Pool created ok.
 *    other     Pool not created due to indicated error. newpoolp set to NULL.
 *
 *
 */
int bcm_mpm_create_prealloc_pool(bcm_mpm_mgr_h mgr,
                                 unsigned int obj_sz,
                                 int nobj,
                                 void *memstart,
                                 unsigned int memsize,
                                 const char poolname[BCM_MP_NAMELEN],
                                 bcm_mp_pool_h *newp);


/*
 * bcm_mpm_delete_prealloc_pool() - Delete a memory pool. This should only be called after
 *                                  all memory objects have been freed back to the pool.
 *
 * Parameters:
 *    mgr:     INPUT The handle to the pools manager
 *    pool:    INPUT The handle of the  pool to delete
 *
 * Returns:
 *    BCME_OK   Pool deleted ok.
 *    other     Pool not deleted due to indicated error.
 *
 */
int bcm_mpm_delete_prealloc_pool(bcm_mpm_mgr_h mgr, bcm_mp_pool_h *poolp);

/*
 * bcm_mpm_create_heap_pool() - Create a new pool for fixed size objects. The memory
 *                              pool allocator uses the heap (malloc/free) for memory.
 *                              In this case, the pool allocator is just providing
 *                              statistics and instrumentation on top of the heap,
 *                              without modifying the heap allocation implementation.
 *
 * Parameters:
 *    mgr:      INPUT  The handle to the pool manager
 *    obj_sz:   INPUT  Size of objects that will be allocated by the new pool
 *    poolname  INPUT  For instrumentation, the name of the pool
 *    newp:     OUTPUT The handle for the new pool, if creation is successful
 *
 * Returns:
 *    BCME_OK   Pool created ok.
 *    other     Pool not created due to indicated error. newpoolp set to NULL.
 *
 *
 */
int bcm_mpm_create_heap_pool(bcm_mpm_mgr_h mgr, unsigned int obj_sz,
                             const char poolname[BCM_MP_NAMELEN],
                             bcm_mp_pool_h *newp);


/*
 * bcm_mpm_delete_heap_pool() - Delete a memory pool. This should only be called after
 *                              all memory objects have been freed back to the pool.
 *
 * Parameters:
 *    mgr:     INPUT The handle to the pools manager
 *    pool:    INPUT The handle of the  pool to delete
 *
 * Returns:
 *    BCME_OK   Pool deleted ok.
 *    other     Pool not deleted due to indicated error.
 *
 */
int bcm_mpm_delete_heap_pool(bcm_mpm_mgr_h mgr, bcm_mp_pool_h *poolp);


/*
 * bcm_mpm_stats() - Return stats for all pools
 *
 * Parameters:
 *    mgr:         INPUT   The handle to the pools manager
 *    stats:       OUTPUT  Array of pool statistics.
 *    nentries:    MOD     Max elements in 'stats' array on INPUT. Actual number
 *                         of array elements copied to 'stats' on OUTPUT.
 *
 * Returns:
 *    BCME_OK   Ok
 *    other     Error getting stats.
 *
 */
int bcm_mpm_stats(bcm_mpm_mgr_h mgr, bcm_mp_stats_t *stats, int *nentries);


/*
 * bcm_mpm_dump() - Display statistics on all pools
 *
 * Parameters:
 *    mgr:     INPUT  The handle to the pools manager
 *    b:       OUTPUT Output buffer.
 *
 * Returns:
 *    BCME_OK   Ok
 *    other     Error during dump.
 *
 */
int bcm_mpm_dump(bcm_mpm_mgr_h mgr, struct bcmstrbuf *b);


/*
 * bcm_mpm_get_obj_size() - The size of memory objects may need to be padded to
 *                          compensate for alignment requirements of the objects.
 *                          This function provides the padded object size. If clients
 *                          pre-allocate a memory slab for a memory pool, the
 *                          padded object size should be used by the client to allocate
 *                          the memory slab (in order to provide sufficent space for
 *                          the maximum number of objects).
 *
 * Parameters:
 *    mgr:            INPUT   The handle to the pools manager.
 *    obj_sz:         INPUT   Input object size.
 *    padded_obj_sz:  OUTPUT  Padded object size.
 *
 * Returns:
 *    BCME_OK      Ok
 *    BCME_BADARG  Bad arguments.
 *
 */
int bcm_mpm_get_obj_size(bcm_mpm_mgr_h mgr, unsigned int obj_sz, unsigned int *padded_obj_sz);


/*
***************************************************************************
*
* API Routines on a specific pool.
*
***************************************************************************
*/


/*
 * bcm_mp_alloc() - Allocate a memory pool object.
 *
 * Parameters:
 *    pool:    INPUT    The handle to the pool.
 *
 * Returns:
 *    A pointer to the new object. NULL on error.
 *
 */
void* bcm_mp_alloc(bcm_mp_pool_h pool);

/*
 * bcm_mp_free() - Free a memory pool object.
 *
 * Parameters:
 *    pool:  INPUT   The handle to the pool.
 *    objp:  INPUT   A pointer to the object to free.
 *
 * Returns:
 *    BCME_OK   Ok
 *    other     Error during free.
 *
 */
int bcm_mp_free(bcm_mp_pool_h pool, void *objp);

/*
 * bcm_mp_stats() - Return stats for this pool
 *
 * Parameters:
 *    pool:     INPUT    The handle to the pool
 *    stats:    OUTPUT   Pool statistics
 *
 * Returns:
 *    BCME_OK   Ok
 *    other     Error getting statistics.
 *
 */
int bcm_mp_stats(bcm_mp_pool_h pool, bcm_mp_stats_t *stats);


/*
 * bcm_mp_dump() - Dump a pool
 *
 * Parameters:
 *    pool:    INPUT    The handle to the pool
 *    b        OUTPUT   Output buffer
 *
 * Returns:
 *    BCME_OK   Ok
 *    other     Error during dump.
 *
 */
int bcm_mp_dump(bcm_mp_pool_h pool, struct bcmstrbuf *b);


#endif /* _BCM_MPOOL_PUB_H */
