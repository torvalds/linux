/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2010, 2012-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_ref_drv.h
 *
 * Reference driver extensions to the UMP user space API for allocating UMP memory
 */

#ifndef _UNIFIED_MEMORY_PROVIDER_REF_DRV_H_
#define _UNIFIED_MEMORY_PROVIDER_REF_DRV_H_

#include "ump.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	/* This enum must match with the IOCTL enum in ump_ioctl.h */
	UMP_REF_DRV_CONSTRAINT_NONE = 0,
	UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR = 1,
	UMP_REF_DRV_CONSTRAINT_USE_CACHE = 4,
	UMP_REF_DRV_CONSTRAINT_PRE_RESERVE = 8,
} ump_alloc_constraints;

/** Allocate an UMP handle containing a memory buffer.
 * Input: Size: The minimum size for the allocation.
 * Usage: If this is UMP_REF_DRV_CONSTRAINT_USE_CACHE, the allocation is mapped as cached by the cpu.
 *        If it is UMP_REF_DRV_CONSTRAINT_NONE it is mapped as noncached.
 *        The flag UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR is not supported.*/
UMP_API_EXPORT ump_handle ump_ref_drv_allocate(unsigned long size, ump_alloc_constraints usage);

UMP_API_EXPORT int ump_phy_addr_get(ump_handle memh);

typedef enum
{
	UMP_MSYNC_CLEAN = 0 ,
	UMP_MSYNC_CLEAN_AND_INVALIDATE = 1,
	UMP_MSYNC_INVALIDATE = 2,
	UMP_MSYNC_READOUT_CACHE_ENABLED = 128,
} ump_cpu_msync_op;

typedef enum
{
	UMP_READ = 1,
	UMP_READ_WRITE = 3,
} ump_lock_usage;

/** Flushing cache for an ump_handle.
 * The function will always CLEAN_AND_INVALIDATE as long as the \a op is not UMP_MSYNC_READOUT_CACHE_ENABLED.
 * If so it will only report back if the given ump_handle is cacheable.
 * At the momement the implementation does not use \a address or \a size.
 * Return value is 1 if cache is enabled, and 0 if it is disabled for the given allocation.*/
UMP_API_EXPORT int ump_cpu_msync_now(ump_handle mem, ump_cpu_msync_op op, void *address, int size);


typedef enum
{
	UMP_USED_BY_CPU = 0,
	UMP_USED_BY_MALI = 1,
	UMP_USED_BY_UNKNOWN_DEVICE = 100,
} ump_hw_usage;

typedef enum
{
	UMP_CACHE_OP_START = 0,
	UMP_CACHE_OP_FINISH  = 1,
} ump_cache_op_control;

/** Cache operation control. Tell when cache maintenance operations start and end.
This will allow the kernel to merge cache operations togheter, thus making them faster */
UMP_API_EXPORT int ump_cache_operations_control(ump_cache_op_control op);

/** Memory synchronization - cache flushing if previous user was different hardware */
UMP_API_EXPORT int ump_switch_hw_usage(ump_handle mem, ump_hw_usage new_user);

/** Memory synchronization - cache flushing if previous user was different hardware */
UMP_API_EXPORT int ump_switch_hw_usage_secure_id(ump_secure_id ump_id, ump_hw_usage new_user);

/** Locking buffer. Blocking call if the buffer is already locked. */
UMP_API_EXPORT int ump_lock(ump_handle mem, ump_lock_usage lock_usage);

/** Locking buffer. Blocking call if the buffer is already locked. */
UMP_API_EXPORT int ump_lock_secure_id(ump_secure_id ump_id, ump_lock_usage lock_usage);

/** Unlocking buffer. Let other users lock the buffer for their usage */
UMP_API_EXPORT int ump_unlock(ump_handle mem);

/** Unlocking buffer. Let other users lock the buffer for their usage */
UMP_API_EXPORT int ump_unlock_secure_id(ump_secure_id ump_id);


#ifdef __cplusplus
}
#endif

#endif /*_UNIFIED_MEMORY_PROVIDER_REF_DRV_H_ */
