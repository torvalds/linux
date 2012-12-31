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



/**
 * @file mali_osk_mem.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_MEM_H_
#define _OSK_MEM_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_mem.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup oskmem Memory
 *  
 * Provides C standard library style memory allocation functions (e.g. malloc, free).
 *
 * @{
 */

/**
 * @brief Allocate kernel heap memory
 *
 * Returns a buffer capable of containing at least \a size bytes. The
 * contents of the buffer are undefined.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with osk_free().
 * Failure to do so will cause a memory leak.
 *
 * @note For the implementor: most toolchains supply memory allocation
 * functions that meet the compiler's alignment requirements. Therefore, there
 * is often no need to write code to align the pointer returned by your
 * system's memory allocator. Refer to your system's memory allocator for more
 * information (e.g. the malloc() function, if used).
 *
 * The buffer can be accessed by all threads in the kernel. You need
 * not free the buffer from the same thread that allocated the memory.
 *
 * May block while allocating memory and is therefore not allowed in interrupt
 * service routines.
 *
 * @illegal It is illegal to call osk_malloc() with \a size == 0.
 *
 * @param size Number of bytes to allocate
 * @return On success, the buffer allocated. NULL on failure.
 *
 */
OSK_STATIC_INLINE void *osk_malloc(size_t size) CHECK_RESULT;

/**
 * @brief Allocate and zero kernel heap memory
 *
 * Returns a buffer capable of containing at least \a size bytes.
 * The buffer is initialized to zero.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with osk_free().
 * Failure to do so will cause a memory leak.
 *
 * @note For the implementor: most toolchains supply memory allocation
 * functions that meet the compiler's alignment requirements. Therefore, there
 * is often no need to write code to align the pointer returned by your
 * system's memory allocator. Refer to your system's memory allocator for more
 * information (e.g. the malloc() function, if used).
 *
 * The buffer can be accessed by all threads in the kernel. You need
 * not free the buffer from the same thread that allocated the memory.
 *
 * May block while allocating memory and is therefore not allowed in interrupt
 * service routines.
 *
 * @illegal It is illegal to call osk_calloc() with \a size == 0.
 *
 * @param[in] size  number of bytes to allocate
 * @return On success, the zero initialized buffer allocated. NULL on failure
 */
OSK_STATIC_INLINE void *osk_calloc(size_t size);

/**
 * @brief Free kernel heap memory
 *
 * Reclaims the buffer pointed to by the parameter \a ptr for the kernel.
 * All memory returned from osk_malloc() and osk_calloc() must
 * be freed before the kernel driver exits. Otherwise, a memory leak will
 * occur.
 *
 * Memory must be freed once. It is an error to free the same non-NULL pointer
 * more than once.
 *
 * It is legal to free the NULL pointer.
 *
 * @param[in] ptr Pointer to buffer to free
 *
 * May block while freeing memory and is therefore not allowed in interrupt
 * service routines.
 *
 * @param[in] ptr  pointer to memory previously allocated by
 * osk_malloc() or osk_calloc(). If ptr is NULL no operation
 * is performed.
 */
OSK_STATIC_INLINE void osk_free(void * ptr);

/**
 * @brief Allocate kernel memory at page granularity suitable for mapping into user space
 *
 * Allocates a number of pages from kernel virtual memory to store at least \a size bytes.
 *
 * The allocated memory is aligned to OSK_PAGE_SIZE bytes and is allowed to be mapped 
 * into user space with read/write access.
 *
 * The allocated memory is initialized to zero to prevent any data leaking from kernel space.
 * One needs to be aware not to store any kernel objects or pointers here as these 
 * could be modified by the user at any time. 
 *
 * If \a size is not a multiple of OSK_PAGE_SIZE, the last page of the allocation is
 * only partially used. It is not allowed to store any data in the unused area of the
 * last page.
 *
 * @illegal It is illegal to call osk_vmalloc() with \a size == 0.

 * May block while allocating memory and is therefore not allowed in interrupt
 * service routines.
 *
 * @param[in] size  number of bytes to allocate (will be rounded up to
 *                  a multiple of OSK_PAGE_SIZE).
 * @return pointer to allocated memory, NULL on failure
 */
OSK_STATIC_INLINE void *osk_vmalloc(size_t size) CHECK_RESULT;

/**
 * @brief Free kernel memory
 *
 * Releases memory to the kernel, previously allocated with osk_vmalloc().
 * The same pointer returned from osk_vmalloc() needs to be provided -- 
 * freeing portions of an allocation is not allowed.
 *
 * May block while freeing memory and is therefore not allowed in interrupt
 * service routines.
 *
 * @param[in] vaddr  pointer to memory previously allocated by osk_vmalloc(). 
 * If vaddr is NULL no operation is performed.
 */
OSK_STATIC_INLINE void osk_vfree(void *vaddr);

#ifndef OSK_MEMSET
/** @brief Fills memory.
 *
 * Sets the first \a size bytes of the block of memory pointed to by \a ptr to
 * the specified value
 * @param[out] ptr Pointer to the block of memory to fill.
 * @param[in] chr  Value to be set, passed as an int. The byte written into
 *                 memory will be the smallest positive integer equal to (\a
 *                 chr mod 256).
 * @param[in] size Number of bytes to be set to the value.
 * @return \a ptr is always passed through unmodified
 *
 * @note the prototype of the function is:
 * @code void *OSK_MEMSET( void *ptr, int chr, size_t size ); @endcode
 */
#define OSK_MEMSET( ptr, chr, size ) You_must_define_the_OSK_MEMSET_macro_in_the_platform_layer

#error You must define the OSK_MEMSET macro in the mali_osk_arch_mem.h layer.

/* The definition was only provided for documentation purposes; remove it now. */
#undef OSK_MEMSET
#endif /* OSK_MEMSET */

#ifndef OSK_MEMCPY
/** @brief Copies memory.
 *
 * Copies the \a len bytes from the buffer pointed by the parameter \a src
 * directly to the buffer pointed by \a dst.
 *
 * @illegal It is illegal to call OSK_MEMCPY with \a src overlapping \a
 * dst anywhere in \a len bytes. 
 *
 * @param[out] dst Pointer to the destination array where the content is to be copied.
 * @param[in] src  Pointer to the source of data to be copied.
 * @param[in] len  Number of bytes to copy.
 * @return \a dst is always passed through unmodified.
 *
 * @note the prototype of the function is:
 * @code void *OSK_MEMCPY( void *dst, CONST void *src, size_t len ); @endcode
 */
#define OSK_MEMCPY( dst, src, len ) You_must_define_the_OSK_MEMCPY_macro_in_the_platform_layer

#error You must define the OSK_MEMCPY macro in the mali_osk_arch_mem.h file.

/* The definition was only provided for documentation purposes; remove it now. */
#undef OSK_MEMCPY
#endif /* OSK_MEMCPY */

#ifndef OSK_MEMCMP
/** @brief Compare memory areas
 *
 * Compares \a len bytes of the memory areas pointed by the parameter \a s1 and
 * \a s2.
 *
 * @param[in] s1  Pointer to the first area of memory to compare.
 * @param[in] s2  Pointer to the second area of memory to compare.
 * @param[in] len Number of bytes to compare.
 * @return an integer less than, equal to, or greater than zero if the first 
 * \a len bytes of s1 is found, respectively, to be less than, to match, or 
 * be greater than the first \a len bytes of s2.
 *
 * @note the prototype of the function is:
 * @code int OSK_MEMCMP( CONST void *s1, CONST void *s2, size_t len ); @endcode
 */
#define OSK_MEMCMP( s1, s2, len ) You_must_define_the_OSK_MEMCMP_macro_in_the_platform_layer

#error You must define the OSK_MEMCMP macro in the mali_osk_arch_mem.h file.

/* The definition was only provided for documentation purposes; remove it now. */
#undef OSK_MEMCMP
#endif /* OSK_MEMCMP */

/** @} */ /* end group oskmem */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* Include osk alloc wrappers header */

#if (1 == MALI_BASE_TRACK_MEMLEAK)
#include "osk/include/mali_osk_mem_wrappers.h"
#endif
#ifdef __cplusplus
}
#endif

#endif /* _OSK_MEM_H_ */
