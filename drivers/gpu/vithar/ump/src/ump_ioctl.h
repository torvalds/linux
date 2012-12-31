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



#ifndef _UMP_IOCTL_H_
#define _UMP_IOCTL_H

#include <ump/ump_common.h>

/*
 * The order and size of the members of these have been chosen so the structures look the same in 32-bit and 64-bit builds.
 * If any changes are done build the ump_struct_size_checker test for 32-bit and 64-bit targets. Both must compile successfully to commit.
 */

/** 32/64-bit neutral way to represent pointers */
typedef union ump_pointer
{
	void * value; /**< client should store their pointers here */
	u32 compat_value; /**< 64-bit kernels should fetch value here when handling 32-bit clients */
	u64 sizer; /**< Force 64-bit storage for all clients regardless */
} ump_pointer;

/**
 * UMP allocation request.
 * Used when performing ump_allocate
 */
typedef struct ump_k_allocate
{
	u64 size; /**< [in] Size in bytes to allocate */
	ump_secure_id secure_id; /**< [out] Secure ID of allocation on success */
	ump_alloc_flags alloc_flags; /**< [in] Flags to use when allocating */
} ump_k_allocate;

/**
 * UMP size query request.
 * Used when performing ump_size_get
 */
typedef struct ump_k_sizequery
{
	u64 size; /**< [out] Size of allocation */
	ump_secure_id secure_id; /**< [in] ID of allocation to query the size of */
	u32 padding; /* don't remove */
} ump_k_sizequery;

/**
 * UMP cache synchronization request.
 * Used when performing ump_cpu_msync_now
 */
typedef struct ump_k_msync
{
	ump_pointer mapped_ptr; /**< [in] CPU VA to perform cache operation on */
	ump_secure_id secure_id; /**< [in] ID of allocation to perform cache operation on */
	ump_cpu_msync_op cache_operation; /**< [in] Cache operation to perform */
	u64 size; /**< [in] Size in bytes of the range to synchronize */
} ump_k_msync;

/**
 * UMP memory retain request.
 * Used when performing ump_retain
 */
typedef struct ump_k_retain
{
	ump_secure_id secure_id; /**< [in] ID of allocation to retain a reference to */
	u32 padding; /* don't remove */
} ump_k_retain;

/**
 * UMP memory release request.
 * Used when performing ump_release
 */
typedef struct ump_k_release
{
	ump_secure_id secure_id; /**< [in] ID of allocation to release a reference to */
	u32 padding; /* don't remove */
} ump_k_release;

typedef struct ump_k_import
{
	ump_pointer phandle;                /**< [in]  Pointer to handle to import */
	u32 type;                           /**< [in]  Type of handle to import */
	ump_alloc_flags alloc_flags;        /**< [in]  Flags to assign to the imported memory */
	ump_secure_id secure_id;            /**< [out] UMP ID representing the imported memory */
	u32 padding;                        /* don't remove */
} ump_k_import;

/**
 * UMP allocation flags request.
 * Used when performing umpp_get_allocation_flags
 *
 * used only by v1 API
 */
typedef struct ump_k_allocation_flags
{
	ump_secure_id secure_id; /**< [in] Secure ID of allocation on success */
	ump_alloc_flags alloc_flags; /**< [out] Flags to use when allocating */
} ump_k_allocation_flags;

#define UMP_CALL_MAX_SIZE 512
/*
 * Ioctl definitions
 */

/* Use '~' as magic number */

#define UMP_IOC_MAGIC  '~'

#define UMP_FUNC_ALLOCATE _IOWR(UMP_IOC_MAGIC,  1, ump_k_allocate)
#define UMP_FUNC_SIZEQUERY _IOWR(UMP_IOC_MAGIC,  2, ump_k_sizequery)
#define UMP_FUNC_MSYNC _IOWR(UMP_IOC_MAGIC,  3, ump_k_msync)
#define UMP_FUNC_RETAIN _IOW(UMP_IOC_MAGIC,  4, ump_k_retain)
#define UMP_FUNC_RELEASE _IOW(UMP_IOC_MAGIC,  5, ump_k_release)
#define UMP_FUNC_ALLOCATION_FLAGS_GET _IOWR(UMP_IOC_MAGIC,  6, ump_k_allocation_flags)
#define UMP_FUNC_IMPORT _IOWR(UMP_IOC_MAGIC, 7, ump_k_import)

/*max ioctl sequential number*/
#define UMP_IOC_MAXNR 7

/* 15 bits for the UMP ID (allowing 32768 IDs) */
#define UMP_LINUX_ID_BITS 15
#define UMP_LINUX_ID_MASK ((1ULL << UMP_LINUX_ID_BITS) - 1ULL)

/* 64-bit (really 52 bits) encoding: 15 bits for the ID, 37 bits for the offset */
#define UMP_LINUX_OFFSET_BITS_64 37
#define UMP_LINUX_OFFSET_MASK_64 ((1ULL << UMP_LINUX_OFFSET_BITS_64)-1)
/* 32-bit encoding: 15 bits for the ID, 17 bits for the offset */
#define UMP_LINUX_OFFSET_BITS_32 17
#define UMP_LINUX_OFFSET_MASK_32 ((1ULL << UMP_LINUX_OFFSET_BITS_32)-1)

#if __SIZEOF_LONG__ == 8
#define UMP_LINUX_OFFSET_BITS UMP_LINUX_OFFSET_BITS_64
#define UMP_LINUX_OFFSET_MASK UMP_LINUX_OFFSET_MASK_64
#else
#define UMP_LINUX_OFFSET_BITS UMP_LINUX_OFFSET_BITS_32
#define UMP_LINUX_OFFSET_MASK UMP_LINUX_OFFSET_MASK_32
#endif

#endif /* _UMP_IOCTL_H_ */
