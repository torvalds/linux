/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file ump_kernel_interface.h
 *
 * This file contains the kernel space part of the UMP API.
 *
 */

#ifndef _UMP_KERNEL_INTERFACE_H_
#define _UMP_KERNEL_INTERFACE_H_

/**
 * @addtogroup ump_api
 * @{
 */

/** @defgroup ump_kernel_space_api UMP Kernel Space API
 * @{ */

/**
 * External representation of a UMP handle in kernel space.
 */
typedef void * ump_dd_handle;

#include <ump/ump_common.h>
#include <ump/ump_kernel_platform.h>

#if defined(__KERNEL__)
#include <ump/src/devicedrv/imports/ump_import.h>
#else
#include <ump/src/library/common/ump_user.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Value to indicate an invalid UMP memory handle.
 */
#define UMP_DD_INVALID_MEMORY_HANDLE ((ump_dd_handle)0)

/**
 * Struct used to describe a physical block used by UMP memory
 */
typedef struct ump_dd_physical_block_64
{
	u64 addr; /**< The physical address of the block */
	u64 size; /**< The length of the block, in bytes, typically page aligned */
} ump_dd_physical_block_64;

/**
 * Security filter hook.
 *
 * Each allocation can have a security filter attached to it.@n
 * The hook receives
 * @li the secure ID
 * @li a handle to the allocation
 * @li  the callback_data argument provided to @ref ump_dd_allocate_64 or @ref ump_dd_create_from_phys_blocks_64
 *
 * The hook must return @a MALI_TRUE to indicate that access to the handle is allowed or @n
 * @a MALI_FALSE to state that no access is permitted.@n
 * This hook is guaranteed to be called in the context of the requesting process/address space.
 *
 * The arguments provided to the hook are;
 * @li the secure ID
 * @li handle to the allocation
 * @li the callback_data set when registering the hook
 *
 * Return value;
 * @li @a TRUE to permit access
 * @li @a FALSE to deny access
 */
typedef bool (*ump_dd_security_filter)(ump_secure_id, ump_dd_handle, void *);

/**
 * Final release notify hook.
 *
 * Allocations can have a hook attached to them which is called when the last reference to the allocation is released.
 * No reference count manipulation is allowed on the provided handle, just property querying (ID get, size get, phys block get).
 * This is similar to finalizers in OO languages.
 *
 * The arguments provided to the hook are;
 * * handle to the allocation
 * * the callback_data set when registering the hook
 */
typedef void (*ump_dd_final_release_callback)(const ump_dd_handle, void *);

/**
 * Allocate a buffer.
 * The lifetime of the allocation is controlled by a reference count.
 * The reference count of the returned buffer is set to 1.
 * The memory will be freed once the reference count reaches 0.
 * Use @ref ump_dd_retain and @ref ump_dd_release to control the reference count.
 * @param size Number of bytes to allocate. Will be padded up to a multiple of the page size.
 * @param flags Bit-wise OR of zero or more of the allocation flag bits.
 * @param[in] filter_func Pointer to a function which will be called when an allocation is required from a
 * secure id before the allocation itself is returned to user-space.
 * NULL permitted if no need for a callback.
 * @param[in] final_release_func Pointer to a function which will be called when the last reference is removed,
 * just before the allocation is freed. NULL permitted if no need for a callback.
 * @param[in] callback_data An opaque pointer which will be provided to @a filter_func and @a final_release_func
 * @return Handle to the new allocation, or @a UMP_DD_INVALID_MEMORY_HANDLE on allocation failure.
 */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_allocate_64(u64 size, ump_alloc_flags flags, ump_dd_security_filter filter_func, ump_dd_final_release_callback final_release_func, void* callback_data);


/**
 * Allocation bits getter.
 * Retrieves the allocation flags used when instantiating the given handle.
 * Just a copy of the flag given to @ref ump_dd_allocate_64 and @ref ump_dd_create_from_phys_blocks_64
 * @param mem The handle retrieve the bits for
 * @return The allocation bits used to instantiate the allocation
 */
UMP_KERNEL_API_EXPORT ump_alloc_flags ump_dd_allocation_flags_get(const ump_dd_handle mem);


/**
 * Retrieves the secure ID for the specified UMP memory.
 *
 * This identifier is unique across the entire system, and uniquely identifies
 * the specified UMP memory allocation. This identifier can later be used through the
 * @ref ump_dd_from_secure_id or
 * @ref ump_from_secure_id
 * functions in order to access this UMP memory, for instance from another process (if shared of course).
 * Unless the allocation was marked as shared the returned ID will only be resolvable in the same process as did the allocation.
 *
 * Calling on an @a UMP_DD_INVALID_MEMORY_HANDLE will result in undefined behavior.
 * Debug builds will assert on this.
 *
 * @note There is a user space equivalent function called @ref ump_secure_id_get
 *
 * @see ump_dd_from_secure_id
 * @see ump_from_secure_id
 * @see ump_secure_id_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the secure ID for the specified UMP memory.
 */
UMP_KERNEL_API_EXPORT ump_secure_id ump_dd_secure_id_get(const ump_dd_handle mem);


/**
 * Retrieves a handle to allocated UMP memory.
 *
 * The usage of UMP memory is reference counted, so this will increment the reference
 * count by one for the specified UMP memory.
 * Use @ref ump_dd_release when there is no longer any
 * use for the retrieved handle.
 *
 * If called on an non-shared allocation and this is a different process @a UMP_DD_INVALID_MEMORY_HANDLE will be returned.
 *
 * Calling on an @a UMP_INVALID_SECURE_ID will return @a UMP_DD_INVALID_MEMORY_HANDLE
 *
 * @note There is a user space equivalent function called @ref ump_from_secure_id
 *
 * @see ump_dd_release
 * @see ump_from_secure_id
 *
 * @param secure_id The secure ID of the UMP memory to open, that can be retrieved using the @ref ump_secure_id_get function.
 *
 * @return @a UMP_DD_INVALID_MEMORY_HANDLE indicates failure, otherwise a valid handle is returned.
 */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_from_secure_id(ump_secure_id secure_id);


/**
 * Retrieves all physical memory block information for specified UMP memory.
 *
 * This function can be used by other device drivers in order to create MMU tables.
 * This function will return a pointer to an array of @ref ump_dd_physical_block_64 in @a pArray and the number of array elements in @a pCount
 *
 * Calling on an @a UMP_DD_INVALID_MEMORY_HANDLE results in undefined behavior.
 * Debug builds will assert on this.
 *
 * @param mem Handle to UMP memory.
 * @param[out] pCount Pointer to where to store the number of items in the returned array
 * @param[out] pArray Pointer to where to store a pointer to the physical blocks array
 */
UMP_KERNEL_API_EXPORT void ump_dd_phys_blocks_get_64(const ump_dd_handle mem, u64 * pCount, const ump_dd_physical_block_64 ** pArray);

/**
 * Retrieves the actual size of the specified UMP memory.
 *
 * The size is reported in bytes, and is typically page aligned.
 *
 * Calling on an @a UMP_DD_INVALID_MEMORY_HANDLE results in undefined behavior.
 * Debug builds will assert on this.
 *
 * @note There is a user space equivalent function called @ref ump_size_get
 *
 * @see ump_size_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the allocated size of the specified UMP memory, in bytes.
 */
UMP_KERNEL_API_EXPORT u64 ump_dd_size_get_64(const ump_dd_handle mem);


/**
 * Adds an extra reference to the specified UMP memory allocation.
 *
 * The function @ref ump_dd_release must then be used
 * to release each copy of the UMP memory handle.
 *
 * Calling on an @a UMP_DD_INVALID_MEMORY_HANDLE results in undefined behavior.
 * Debug builds will assert on this.
 *
 * @note You are not required to call @ref ump_dd_retain
 * for UMP handles returned from
 * @ref ump_dd_from_secure_id,
 * because these handles are already reference counted by this function.
 *
 * @note There is a user space equivalent function called @ref ump_retain
 *
 * @see ump_retain
 *
 * @param mem Handle to UMP memory.
 * @return 0 indicates success, any other value indicates failure.
 */
UMP_KERNEL_API_EXPORT int ump_dd_retain(ump_dd_handle mem);


/**
 * Releases a reference from the specified UMP memory.
 *
 * This function must be called once for every reference to the UMP memory handle.
 * When the last reference is released, all resources associated with this UMP memory
 * handle are freed.
 *
 * One can only call ump_release when matched with a successful ump_dd_retain, ump_dd_allocate_64 or ump_dd_from_secure_id
 * If called on an @a UMP_DD_INVALID_MEMORY_HANDLE the function will early out.
 *
 * @note There is a user space equivalent function called @ref ump_release
 *
 * @see ump_release
 *
 * @param mem Handle to UMP memory.
 */
UMP_KERNEL_API_EXPORT void ump_dd_release(ump_dd_handle mem);

/**
 * Create an ump allocation handle based on externally managed memory.
 * Used to wrap an existing allocation as an UMP memory handle.
 * Once wrapped the memory acts just like a normal allocation coming from @ref ump_dd_allocate_64.
 * The only exception is that the freed physical memory is not put into the pool of free memory, but instead considered returned to the caller once @a final_release_func returns.
 * The blocks array will be copied, so no need to hold on to it after this function returns.
 * @param[in] blocks Array of @ref ump_dd_physical_block_64
 * @param num_blocks Number of elements in the array pointed to by @a blocks
 * @param flags Allocation flags to mark the handle with
 * @param[in] filter_func Pointer to a function which will be called when an allocation is required from a secure id before the allocation itself is returned to user-space.
 * NULL permitted if no need for a callback.
 * @param[in] final_release_func Pointer to a function which will be called when the last reference is removed, just before the allocation is freed. NULL permitted if no need for a callback.
 * @param[in] callback_data An opaque pointer which will be provided to @a filter_func and @a final_release_func
 * @return Handle to the UMP allocation handle created, or @a UMP_DD_INVALID_MEMORY_HANDLE if no such handle could be created.
 */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_create_from_phys_blocks_64(const ump_dd_physical_block_64 * blocks, u64 num_blocks, ump_alloc_flags flags, ump_dd_security_filter filter_func, ump_dd_final_release_callback final_release_func, void* callback_data);


/** @name UMP v1 API
 * Functions provided to support compatibility with UMP v1 API
 *
 *@{
 */

/**
 * Value to indicate an invalid UMP memory handle.
 */
#define UMP_DD_HANDLE_INVALID UMP_DD_INVALID_MEMORY_HANDLE

/**
 * UMP error codes for kernel space.
 */
typedef enum
{
	UMP_DD_SUCCESS, /**< indicates success */
	UMP_DD_INVALID /**< indicates failure */
} ump_dd_status_code;


/**
 * Struct used to describe a physical block used by UMP memory
 */
typedef struct ump_dd_physical_block
{
	unsigned long addr; /**< The physical address of the block */
	unsigned long size; /**< The length of the block, typically page aligned */
} ump_dd_physical_block;


/**
 * Retrieves a handle to allocated UMP memory.
 *
 * The usage of UMP memory is reference counted, so this will increment the reference
 * count by one for the specified UMP memory.
 * Use @ref ump_dd_reference_release "ump_dd_reference_release" when there is no longer any
 * use for the retrieved handle.
 *
 * @note There is a user space equivalent function called @ref ump_handle_create_from_secure_id "ump_handle_create_from_secure_id"
 *
 * @see ump_dd_reference_release
 * @see ump_handle_create_from_secure_id
 *
 * @param secure_id The secure ID of the UMP memory to open, that can be retrieved using the @ref ump_secure_id_get "ump_secure_id_get " function.
 *
 * @return UMP_INVALID_MEMORY_HANDLE indicates failure, otherwise a valid handle is returned.
 */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_secure_id(ump_secure_id secure_id);



/**
 * Create an ump allocation handle based on externally managed memory.
 * Used to wrap an existing allocation as an UMP memory handle.
 *
 * @param[in] blocks Array of @ref ump_dd_physical_block
 * @param num_blocks Number of elements in the array pointed to by @a blocks
 *
 * @return Handle to the UMP allocation handle created, or @a UMP_DD_INVALID_MEMORY_HANDLE if no such handle could be created.
 */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_phys_blocks(ump_dd_physical_block * blocks, unsigned long num_blocks);


/**
 * Retrieves the number of physical blocks used by the specified UMP memory.
 *
 * This function retrieves the number of @ref ump_dd_physical_block "ump_dd_physical_block" structs needed
 * to describe the physical memory layout of the given UMP memory. This can later be used when calling
 * the functions @ref ump_dd_phys_blocks_get "ump_dd_phys_blocks_get" and
 * @ref ump_dd_phys_block_get "ump_dd_phys_block_get".
 *
 * @see ump_dd_phys_blocks_get
 * @see ump_dd_phys_block_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return The number of ump_dd_physical_block structs required to describe the physical memory layout of the specified UMP memory.
 */
UMP_KERNEL_API_EXPORT unsigned long ump_dd_phys_block_count_get(ump_dd_handle mem);


/**
 * Retrieves all physical memory block information for specified UMP memory.
 *
 * This function can be used by other device drivers in order to create MMU tables.
 *
 * @note This function will fail if the num_blocks parameter is either to large or to small.
 *
 * @see ump_dd_phys_block_get
 *
 * @param mem Handle to UMP memory.
 * @param blocks An array of @ref ump_dd_physical_block "ump_dd_physical_block" structs that will receive the physical description.
 * @param num_blocks The number of blocks to return in the blocks array. Use the function
 *                   @ref ump_dd_phys_block_count_get "ump_dd_phys_block_count_get" first to determine the number of blocks required.
 *
 * @return UMP_DD_SUCCESS indicates success, UMP_DD_INVALID indicates failure.
 */
UMP_KERNEL_API_EXPORT ump_dd_status_code ump_dd_phys_blocks_get(ump_dd_handle mem, ump_dd_physical_block * blocks, unsigned long num_blocks);


/**
 * Retrieves the physical memory block information for specified block for the specified UMP memory.
 *
 * This function can be used by other device drivers in order to create MMU tables.
 *
 * @note This function will return UMP_DD_INVALID if the specified index is out of range.
 *
 * @see ump_dd_phys_blocks_get
 *
 * @param mem Handle to UMP memory.
 * @param index Which physical info block to retrieve.
 * @param block Pointer to a @ref ump_dd_physical_block "ump_dd_physical_block" struct which will receive the requested information.
 *
 * @return UMP_DD_SUCCESS indicates success, UMP_DD_INVALID indicates failure.
 */
UMP_KERNEL_API_EXPORT ump_dd_status_code ump_dd_phys_block_get(ump_dd_handle mem, unsigned long index, ump_dd_physical_block * block);


/**
 * Retrieves the actual size of the specified UMP memory.
 *
 * The size is reported in bytes, and is typically page aligned.
 *
 * @note There is a user space equivalent function called @ref ump_size_get "ump_size_get"
 *
 * @see ump_size_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the allocated size of the specified UMP memory, in bytes.
 */
UMP_KERNEL_API_EXPORT unsigned long ump_dd_size_get(ump_dd_handle mem);


/**
 * Adds an extra reference to the specified UMP memory.
 *
 * This function adds an extra reference to the specified UMP memory. This function should
 * be used every time a UMP memory handle is duplicated, that is, assigned to another ump_dd_handle
 * variable. The function @ref ump_dd_reference_release "ump_dd_reference_release" must then be used
 * to release each copy of the UMP memory handle.
 *
 * @note You are not required to call @ref ump_dd_reference_add "ump_dd_reference_add"
 * for UMP handles returned from
 * @ref ump_dd_handle_create_from_secure_id "ump_dd_handle_create_from_secure_id",
 * because these handles are already reference counted by this function.
 *
 * @note There is a user space equivalent function called @ref ump_reference_add "ump_reference_add"
 *
 * @see ump_reference_add
 *
 * @param mem Handle to UMP memory.
 */
UMP_KERNEL_API_EXPORT void ump_dd_reference_add(ump_dd_handle mem);


/**
 * Releases a reference from the specified UMP memory.
 *
 * This function should be called once for every reference to the UMP memory handle.
 * When the last reference is released, all resources associated with this UMP memory
 * handle are freed.
 *
 * @note There is a user space equivalent function called @ref ump_reference_release "ump_reference_release"
 *
 * @see ump_reference_release
 *
 * @param mem Handle to UMP memory.
 */
UMP_KERNEL_API_EXPORT void ump_dd_reference_release(ump_dd_handle mem);

/* @} */

#ifdef __cplusplus
}
#endif


/** @} */ /* end group ump_kernel_space_api */

/** @} */ /* end group ump_api */

#endif /* _UMP_KERNEL_INTERFACE_H_ */
