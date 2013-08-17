/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_uk_types.h
 * Defines the types and constants used in the user-kernel interface
 */

#ifndef __UMP_UK_TYPES_H__
#define __UMP_UK_TYPES_H__

#ifdef __cplusplus
extern "C"
{
#endif

/* Helpers for API version handling */
#define MAKE_VERSION_ID(x) (((x) << 16UL) | (x))
#define IS_VERSION_ID(x) (((x) & 0xFFFF) == (((x) >> 16UL) & 0xFFFF))
#define GET_VERSION(x) (((x) >> 16UL) & 0xFFFF)
#define IS_API_MATCH(x, y) (IS_VERSION_ID((x)) && IS_VERSION_ID((y)) && (GET_VERSION((x)) == GET_VERSION((y))))

/**
 * API version define.
 * Indicates the version of the kernel API
 * The version is a 16bit integer incremented on each API change.
 * The 16bit integer is stored twice in a 32bit integer
 * So for version 1 the value would be 0x00010001
 */
#define UMP_IOCTL_API_VERSION MAKE_VERSION_ID(2)

typedef enum
{
	_UMP_IOC_QUERY_API_VERSION = 1,
	_UMP_IOC_ALLOCATE,
	_UMP_IOC_RELEASE,
	_UMP_IOC_SIZE_GET,
	_UMP_IOC_MAP_MEM,    /* not used in Linux */
	_UMP_IOC_UNMAP_MEM,  /* not used in Linux */
	_UMP_IOC_MSYNC,
	_UMP_IOC_CACHE_OPERATIONS_CONTROL,
	_UMP_IOC_SWITCH_HW_USAGE,
	_UMP_IOC_LOCK,
	_UMP_IOC_UNLOCK,
	_UMP_IOC_ION_IMPORT,
}_ump_uk_functions;

typedef enum
{
	UMP_REF_DRV_UK_CONSTRAINT_NONE = 0,
	UMP_REF_DRV_UK_CONSTRAINT_PHYSICALLY_LINEAR = 1,
	UMP_REF_DRV_UK_CONSTRAINT_USE_CACHE = 128,
} ump_uk_alloc_constraints;

typedef enum
{
	_UMP_UK_MSYNC_CLEAN = 0,
	_UMP_UK_MSYNC_CLEAN_AND_INVALIDATE = 1,
	_UMP_UK_MSYNC_INVALIDATE = 2,
	_UMP_UK_MSYNC_FLUSH_L1   = 3,
	_UMP_UK_MSYNC_READOUT_CACHE_ENABLED = 128,
} ump_uk_msync_op;

typedef enum
{
	_UMP_UK_CACHE_OP_START = 0,
	_UMP_UK_CACHE_OP_FINISH  = 1,
} ump_uk_cache_op_control;

typedef enum
{
	_UMP_UK_READ = 1,
	_UMP_UK_READ_WRITE = 3,
} ump_uk_lock_usage;

typedef enum
{
	_UMP_UK_USED_BY_CPU = 0,
	_UMP_UK_USED_BY_MALI = 1,
	_UMP_UK_USED_BY_UNKNOWN_DEVICE= 100,
} ump_uk_user;

/**
 * Get API version ([in,out] u32 api_version, [out] u32 compatible)
 */
typedef struct _ump_uk_api_version_s
{
	void *ctx;      /**< [in,out] user-kernel context (trashed on output) */
	u32 version;    /**< Set to the user space version on entry, stores the device driver version on exit */
	u32 compatible; /**< Non-null if the device is compatible with the client */
} _ump_uk_api_version_s;

/**
 * ALLOCATE ([out] u32 secure_id, [in,out] u32 size,  [in] contraints)
 */
typedef struct _ump_uk_allocate_s
{
	void *ctx;                              /**< [in,out] user-kernel context (trashed on output) */
	u32 secure_id;                          /**< Return value from DD to Userdriver */
	u32 size;                               /**< Input and output. Requested size; input. Returned size; output */
	ump_uk_alloc_constraints constraints;   /**< Only input to Devicedriver */
} _ump_uk_allocate_s;

typedef struct _ump_uk_ion_import_s
{
	void *ctx;                              /**< [in,out] user-kernel context (trashed on output) */
	int ion_fd;                             /**< ion_fd */
	u32 secure_id;                          /**< Return value from DD to Userdriver */
	u32 size;                               /**< Input and output. Requested size; input. Returned size; output */
	ump_uk_alloc_constraints constraints;   /**< Only input to Devicedriver */
} _ump_uk_ion_import_s;

/**
 * SIZE_GET ([in] u32 secure_id, [out]size )
 */
typedef struct _ump_uk_size_get_s
{
	void *ctx;                              /**< [in,out] user-kernel context (trashed on output) */
	u32 secure_id;                          /**< Input to DD */
	u32 size;                               /**< Returned size; output */
} _ump_uk_size_get_s;

/**
 * Release ([in] u32 secure_id)
 */
typedef struct _ump_uk_release_s
{
	void *ctx;                              /**< [in,out] user-kernel context (trashed on output) */
	u32 secure_id;                          /**< Input to DD */
} _ump_uk_release_s;

typedef struct _ump_uk_map_mem_s
{
	void *ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	void *mapping;                  /**< [out] Returns user-space virtual address for the mapping */
	void *phys_addr;                /**< [in] physical address */
	unsigned long size;             /**< [in] size */
	u32 secure_id;                  /**< [in] secure_id to assign to mapping */
	void * _ukk_private;            /**< Only used inside linux port between kernel frontend and common part to store vma */
	u32 cookie;
	u32 is_cached;            /**< [in,out] caching of CPU mappings */
} _ump_uk_map_mem_s;

typedef struct _ump_uk_unmap_mem_s
{
	void *ctx;            /**< [in,out] user-kernel context (trashed on output) */
	void *mapping;
	u32 size;
	void * _ukk_private;
	u32 cookie;
} _ump_uk_unmap_mem_s;

typedef struct _ump_uk_msync_s
{
	void *ctx;            /**< [in,out] user-kernel context (trashed on output) */
	void *mapping;        /**< [in] mapping addr */
	void *address;        /**< [in] flush start addr */
	u32 size;             /**< [in] size to flush */
	ump_uk_msync_op op;   /**< [in] flush operation */
	u32 cookie;           /**< [in] cookie stored with reference to the kernel mapping internals */
	u32 secure_id;        /**< [in] secure_id that identifies the ump buffer */
	u32 is_cached;        /**< [out] caching of CPU mappings */
} _ump_uk_msync_s;

typedef struct _ump_uk_cache_operations_control_s
{
	void *ctx;                   /**< [in,out] user-kernel context (trashed on output) */
	ump_uk_cache_op_control op;  /**< [in] cache operations start/stop */
} _ump_uk_cache_operations_control_s;


typedef struct _ump_uk_switch_hw_usage_s
{
	void *ctx;            /**< [in,out] user-kernel context (trashed on output) */
	u32 secure_id;        /**< [in] secure_id that identifies the ump buffer */
	ump_uk_user new_user;         /**< [in] cookie stored with reference to the kernel mapping internals */

} _ump_uk_switch_hw_usage_s;

typedef struct _ump_uk_lock_s
{
	void *ctx;            /**< [in,out] user-kernel context (trashed on output) */
	u32 secure_id;        /**< [in] secure_id that identifies the ump buffer */
	ump_uk_lock_usage lock_usage;
} _ump_uk_lock_s;

typedef struct _ump_uk_unlock_s
{
	void *ctx;            /**< [in,out] user-kernel context (trashed on output) */
	u32 secure_id;        /**< [in] secure_id that identifies the ump buffer */
} _ump_uk_unlock_s;

#ifdef __cplusplus
}
#endif

#endif /* __UMP_UK_TYPES_H__ */
