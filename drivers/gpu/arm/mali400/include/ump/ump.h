/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump.h
 *
 * This file contains the user space part of the UMP API.
 */

#ifndef _UNIFIED_MEMORY_PROVIDER_H_
#define _UNIFIED_MEMORY_PROVIDER_H_


/** @defgroup ump_user_space_api UMP User Space API
 * @{ */


#include "ump_platform.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * External representation of a UMP handle in user space.
 */
typedef void *ump_handle;

/**
 * Typedef for a secure ID, a system wide identificator for UMP memory buffers.
 */
typedef unsigned int ump_secure_id;

/**
 * Value to indicate an invalid UMP memory handle.
 */
#define UMP_INVALID_MEMORY_HANDLE ((ump_handle)0)

/**
 * Value to indicate an invalid secure Id.
 */
#define UMP_INVALID_SECURE_ID     ((ump_secure_id)-1)

/**
 * UMP error codes for user space.
 */
typedef enum
{
	UMP_OK = 0, /**< indicates success */
	UMP_ERROR,  /**< indicates failure */
} ump_result;


/**
 * Opens and initializes the UMP library.
 *
 * This function must be called at least once before calling any other UMP API functions.
 * Each open is reference counted and must be matched with a call to @ref ump_close "ump_close".
 *
 * @see ump_close
 *
 * @return UMP_OK indicates success, UMP_ERROR indicates failure.
 */
UMP_API_EXPORT ump_result ump_open(void);


/**
 * Terminate the UMP library.
 *
 * This must be called once for every successful @ref ump_open "ump_open". The UMP library is
 * terminated when, and only when, the last open reference to the UMP interface is closed.
 *
 * @see ump_open
 */
UMP_API_EXPORT void ump_close(void);


/**
 * Retrieves the secure ID for the specified UMP memory.
 *
 * This identificator is unique across the entire system, and uniquely identifies
 * the specified UMP memory. This identificator can later be used through the
 * @ref ump_handle_create_from_secure_id "ump_handle_create_from_secure_id" or
 * @ref ump_dd_handle_create_from_secure_id "ump_dd_handle_create_from_secure_id"
 * functions in order to access this UMP memory, for instance from another process.
 *
 * @note There is a kernel space equivalent function called @ref ump_dd_secure_id_get "ump_dd_secure_id_get"
 *
 * @see ump_handle_create_from_secure_id
 * @see ump_dd_handle_create_from_secure_id
 * @see ump_dd_secure_id_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the secure ID for the specified UMP memory.
 */
UMP_API_EXPORT ump_secure_id ump_secure_id_get(ump_handle mem);


/**
 * Retrieves a handle to allocated UMP memory.
 *
 * The usage of UMP memory is reference counted, so this will increment the reference
 * count by one for the specified UMP memory.
 * Use @ref ump_reference_release "ump_reference_release" when there is no longer any
 * use for the retrieved handle.
 *
 * @note There is a kernel space equivalent function called @ref ump_dd_handle_create_from_secure_id "ump_dd_handle_create_from_secure_id"
 *
 * @see ump_reference_release
 * @see ump_dd_handle_create_from_secure_id
 *
 * @param secure_id The secure ID of the UMP memory to open, that can be retrieved using the @ref ump_secure_id_get "ump_secure_id_get " function.
 *
 * @return UMP_INVALID_MEMORY_HANDLE indicates failure, otherwise a valid handle is returned.
 */
UMP_API_EXPORT ump_handle ump_handle_create_from_secure_id(ump_secure_id secure_id);


/**
 * Retrieves the actual size of the specified UMP memory.
 *
 * The size is reported in bytes, and is typically page aligned.
 *
 * @note There is a kernel space equivalent function called @ref ump_dd_size_get "ump_dd_size_get"
 *
 * @see ump_dd_size_get
 *
 * @param mem Handle to UMP memory.
 *
 * @return Returns the allocated size of the specified UMP memory, in bytes.
 */
UMP_API_EXPORT unsigned long ump_size_get(ump_handle mem);


/**
 * Read from specified UMP memory.
 *
 * Another way of reading from (and writing to) UMP memory is to use the
 * @ref ump_mapped_pointer_get "ump_mapped_pointer_get" to retrieve
 * a CPU mapped pointer to the memory.
 *
 * @see ump_mapped_pointer_get
 *
 * @param dst Destination buffer.
 * @param src Handle to UMP memory to read from.
 * @param offset Where to start reading, given in bytes.
 * @param length How much to read, given in bytes.
 */
UMP_API_EXPORT void ump_read(void *dst, ump_handle src, unsigned long offset, unsigned long length);


/**
 * Write to specified UMP memory.
 *
 * Another way of writing to (and reading from) UMP memory is to use the
 * @ref ump_mapped_pointer_get "ump_mapped_pointer_get" to retrieve
 * a CPU mapped pointer to the memory.
 *
 * @see ump_mapped_pointer_get
 *
 * @param dst Handle to UMP memory to write to.
 * @param offset Where to start writing, given in bytes.
 * @param src Buffer to read from.
 * @param length How much to write, given in bytes.
 */
UMP_API_EXPORT void ump_write(ump_handle dst, unsigned long offset, const void *src, unsigned long length);


/**
 * Retrieves a memory mapped pointer to the specified UMP memory.
 *
 * This function retrieves a memory mapped pointer to the specified UMP memory,
 * that can be used by the CPU. Every successful call to
 * @ref ump_mapped_pointer_get "ump_mapped_pointer_get" is reference counted,
 * and must therefore be followed by a call to
 * @ref ump_mapped_pointer_release "ump_mapped_pointer_release " when the
 * memory mapping is no longer needed.
 *
 * @note Systems without a MMU for the CPU only return the physical address, because no mapping is required.
 *
 * @see ump_mapped_pointer_release
 *
 * @param mem Handle to UMP memory.
 *
 * @return NULL indicates failure, otherwise a CPU mapped pointer is returned.
 */
UMP_API_EXPORT void *ump_mapped_pointer_get(ump_handle mem);


/**
 * Releases a previously mapped pointer to the specified UMP memory.
 *
 * The CPU mapping of the specified UMP memory memory is reference counted,
 * so every call to @ref ump_mapped_pointer_get "ump_mapped_pointer_get" must
 * be matched with a call to this function when the mapping is no longer needed.
 *
 * The CPU mapping is not removed before all references to the mapping is released.
 *
 * @note Systems without a MMU must still implement this function, even though no unmapping should be needed.
 *
 * @param mem Handle to UMP memory.
 */
UMP_API_EXPORT void ump_mapped_pointer_release(ump_handle mem);


/**
 * Adds an extra reference to the specified UMP memory.
 *
 * This function adds an extra reference to the specified UMP memory. This function should
 * be used every time a UMP memory handle is duplicated, that is, assigned to another ump_handle
 * variable. The function @ref ump_reference_release "ump_reference_release" must then be used
 * to release each copy of the UMP memory handle.
 *
 * @note You are not required to call @ref ump_reference_add "ump_reference_add"
 * for UMP handles returned from
 * @ref ump_handle_create_from_secure_id "ump_handle_create_from_secure_id",
 * because these handles are already reference counted by this function.
 *
 * @note There is a kernel space equivalent function called @ref ump_dd_reference_add "ump_dd_reference_add"
 *
 * @see ump_dd_reference_add
 *
 * @param mem Handle to UMP memory.
 */
UMP_API_EXPORT void ump_reference_add(ump_handle mem);


/**
 * Releases a reference from the specified UMP memory.
 *
 * This function should be called once for every reference to the UMP memory handle.
 * When the last reference is released, all resources associated with this UMP memory
 * handle are freed.
 *
 * @note There is a kernel space equivalent function called @ref ump_dd_reference_release "ump_dd_reference_release"
 *
 * @see ump_dd_reference_release
 *
 * @param mem Handle to UMP memory.
 */
UMP_API_EXPORT void ump_reference_release(ump_handle mem);


#ifdef __cplusplus
}
#endif


/** @} */ /* end group ump_user_space_api */


#endif /*_UNIFIED_MEMORY_PROVIDER_H_ */
