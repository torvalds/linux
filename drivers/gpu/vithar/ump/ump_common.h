/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file ump_common.h
 *
 * This file contains some common enum values used both in both the user and kernel space side of UMP.
 */

#ifndef _UMP_COMMON_H_
#define _UMP_COMMON_H_

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/**
 * Values to identify major and minor version of UMP
 */
#define UMP_VERSION_MAJOR 2
#define UMP_VERSION_MINOR 0

/**
 * Typedef for a secure ID, a system wide identifier for UMP memory buffers.
 */
typedef int32_t ump_secure_id;

/**
 * Value to indicate an invalid secure Id.
 */
#define UMP_INVALID_SECURE_ID  ((ump_secure_id)0)

/**
 * UMP error codes.
 */
typedef enum
{
	UMP_OK    = 0, /**< indicates success */
	UMP_ERROR = 1  /**< indicates failure */
} ump_result;

/**
 * Allocation flag bits.
 *
 * ump_allocate accepts zero or more flags to specify the type of memory to allocate and how to expose it to devices.
 *
 * For each supported device there are 4 flags to control access permissions and give usage characteristic hints to optimize the allocation/mapping.
 * They are;
 * @li @a UMP_PROT_<device>_RD   read permission
 * @li @a UMP_PROT_<device>_WR   write permission
 * @li @a UMP_HINT_<device>_RD   read often
 * @li @a UMP_HINT_<device>_WR   written often
 *
 * 5 devices are currently supported, with a device being the CPU itself.
 * The other 4 devices will be mapped to real devices per SoC design.
 * They are just named W,X,Y,Z by UMP as it has no knowledge of their real names/identifiers.
 * As an example device W could be a camera device while device Z could be an ARM GPU device, leaving X and Y unused.
 *
 * 2 additional flags control the allocation;
 * @li @a UMP_CONSTRAINT_PHYSICALLY_LINEAR   the allocation must be physical linear. Typical for devices without an MMU and no IOMMU to help it. 
 * @li @a UMP_PROT_SHAREABLE                 the allocation can be shared with other processes on the system. Without this flag the returned allocation won't be resolvable in other processes.
 *
 * All UMP allocation are growable unless they're @a UMP_PROT_SHAREABLE.
 * The hint bits should be used to indicate the access pattern so the driver can select the most optimal memory type and cache settings based on the what the system supports.
 */
typedef enum
{
	/* Generic helpers */
	UMP_PROT_DEVICE_RD = (1u << 0),
	UMP_PROT_DEVICE_WR = (1u << 1),
	UMP_HINT_DEVICE_RD = (1u << 2),
	UMP_HINT_DEVICE_WR = (1u << 3),
	UMP_DEVICE_MASK = 0xF,
	UMP_DEVICE_CPU_SHIFT = 0,
	UMP_DEVICE_W_SHIFT = 4,
	UMP_DEVICE_X_SHIFT = 8,
	UMP_DEVICE_Y_SHIFT = 12,
	UMP_DEVICE_Z_SHIFT = 16,

	/* CPU protection and hints. */
	UMP_PROT_CPU_RD = (1u <<  0),
	UMP_PROT_CPU_WR = (1u <<  1),
	UMP_HINT_CPU_RD = (1u <<  2),
	UMP_HINT_CPU_WR = (1u <<  3),

	/* device W */
	UMP_PROT_W_RD = (1u <<  4),
	UMP_PROT_W_WR = (1u <<  5),
	UMP_HINT_W_RD = (1u <<  6),
	UMP_HINT_W_WR = (1u <<  7),

	/* device X */
	UMP_PROT_X_RD = (1u <<  8),
	UMP_PROT_X_WR = (1u <<  9),
	UMP_HINT_X_RD = (1u << 10),
	UMP_HINT_X_WR = (1u << 11),
	
	/* device Y */
	UMP_PROT_Y_RD = (1u << 12),
	UMP_PROT_Y_WR = (1u << 13),
	UMP_HINT_Y_RD = (1u << 14),
	UMP_HINT_Y_WR = (1u << 15),

	/* device Z */
	UMP_PROT_Z_RD = (1u << 16),
	UMP_PROT_Z_WR = (1u << 17),
	UMP_HINT_Z_RD = (1u << 18),
	UMP_HINT_Z_WR = (1u << 19),

	/* 20-26 reserved for future use */
	UMPP_ALLOCBITS_UNUSED = (0x7Fu << 20),
	/** Allocations marked as @ UMP_CONSTRAINT_UNCACHED won't be mapped as cached by the cpu  */
	UMP_CONSTRAINT_UNCACHED = (1u << 27),
	/** Require 32-bit physically addressable memory */
	UMP_CONSTRAINT_32BIT_ADDRESSABLE = (1u << 28),
	/** For devices without an MMU and with no IOMMU assistance. */
	UMP_CONSTRAINT_PHYSICALLY_LINEAR = (1u << 29),
	/** Shareable must be set to allow the allocation to be used by other processes, the default is non-shared */
	UMP_PROT_SHAREABLE = (1u << 30)
	/* (1u << 31) should not be used to ensure compiler portability */
} ump_allocation_bits;

/**
 * ump_allocation_bits combination argument type.
 *
 * Type used to pass zero or more bits from the @ref ump_allocation_bits enum
 */
typedef uint32_t ump_alloc_flags;


/**
 *  Default allocation flags for UMP v1 compatible allocations.
 */
#define UMP_V1_API_DEFAULT_ALLOCATION_FLAGS		UMP_PROT_CPU_RD | UMP_PROT_CPU_WR | \
												UMP_PROT_W_RD | UMP_PROT_W_WR |	\
												UMP_PROT_X_RD | UMP_PROT_X_WR |	\
												UMP_PROT_Y_RD | UMP_PROT_Y_WR |	\
												UMP_PROT_Z_RD | UMP_PROT_Z_WR |	\
												UMP_PROT_SHAREABLE |	\
												UMP_CONSTRAINT_32BIT_ADDRESSABLE

/**
 * CPU cache sync operations.
 *
 * Cache synchronization operations to pass to @ref ump_cpu_msync_now
 */
enum
{
	/** 
	 * Cleans any dirty cache lines to main memory, but the data will still be available in the cache.
	 * After a clean the contents of memory is considered to be "owned" by the device.
	 * */
	UMP_MSYNC_CLEAN = 1,

	/** Cleans any dirty cache lines to main memory and Ejects all lines from the cache.
	 * After an clean&invalidate the contents of memory is considered to be "owned" by the CPU.
	 * Any subsequent access will fetch data from main memory.
	 * 
	 * @note Due to CPUs doing speculative prefetching a UMP_MSYNC_CLEAN_AND_INVALIDATE must be done before and after interacting with hardware.
	 * */
	UMP_MSYNC_CLEAN_AND_INVALIDATE

};

typedef u32 ump_cpu_msync_op;

/**
 * Memory import types supported.
 * If new import types are added they will appear here.
 * They must be added before UMPP_EXTERNAL_MEM_COUNT and
 * must be assigned an explicit sequantial number.
 *
 * @li UMP_EXTERNAL_MEM_TYPE_ION - Import an ION allocation
 *                                 Takes a int* (pointer to a file descriptor)
 *                                 Another ION reference is taken which is released on the final ump_release
 */
enum ump_external_memory_type
{
	UMPP_EXTERNAL_MEM_TYPE_UNUSED = 0, /* reserve type 0 */
	UMP_EXTERNAL_MEM_TYPE_ION = 1,
	UMPP_EXTERNAL_MEM_COUNT
};

/** @name UMP v1 API
 *
 *@{
 */

/**
 * Allocation constraints.
 *
 * Allocation flags to pass @ref ump_ref_drv_allocate
 *
 * UMP v1 API only.
 */
typedef enum
{
	/** the allocation is mapped as noncached. */
	UMP_REF_DRV_CONSTRAINT_NONE = 0,
	/** not supported. */
	UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR = 1,
	/** the allocation is mapped as cached by the cpu. */
	UMP_REF_DRV_CONSTRAINT_USE_CACHE = 4
} ump_alloc_constraints;

/* @} */


#ifdef __cplusplus
}
#endif


#endif /* _UMP_COMMON_H_ */
