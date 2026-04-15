/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_MEMORY_ALLOC_H
#define VDO_MEMORY_ALLOC_H

#include <linux/cache.h>
#include <linux/io.h> /* for PAGE_SIZE */
#include <linux/overflow.h>

#include "permassert.h"
#include "thread-registry.h"

/* Custom memory allocation function that tracks memory usage */
int __must_check vdo_allocate_memory(size_t size, size_t align, const char *what, void *ptr);

/*
 * Allocate one or more elements of the indicated type, logging an error if the allocation fails.
 * The memory will be zeroed.
 *
 * @COUNT: The number of objects to allocate
 * @WHAT: What is being allocated (for error logging)
 * @PTR: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
#define vdo_allocate(COUNT, WHAT, PTR)					\
	vdo_allocate_memory(size_mul((COUNT), sizeof(typeof(**(PTR)))),	\
			    __alignof__(typeof(**(PTR))), WHAT, PTR)

/*
 * Allocate a structure with a flexible array member, with a specified number of elements, logging
 * an error if the allocation fails. The memory will be zeroed.
 *
 * @COUNT: The number of objects to allocate
 * @FIELD: The flexible array field at the end of the structure
 * @WHAT: What is being allocated (for error logging)
 * @PTR: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
#define vdo_allocate_extended(COUNT, FIELD, WHAT, PTR)			\
	vdo_allocate_memory(struct_size(*(PTR), FIELD, (COUNT)),	\
			    __alignof__(typeof(**(PTR))),		\
			    WHAT,					\
			    (PTR))

/*
 * Allocate memory starting on a cache line boundary, logging an error if the allocation fails. The
 * memory will be zeroed.
 *
 * @size: The number of bytes to allocate
 * @what: What is being allocated (for error logging)
 * @ptr: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
static inline int __must_check vdo_allocate_cache_aligned(size_t size, const char *what, void *ptr)
{
	return vdo_allocate_memory(size, L1_CACHE_BYTES, what, ptr);
}

/*
 * Allocate one element of the indicated type immediately, failing if the required memory is not
 * immediately available.
 *
 * @size: The number of bytes to allocate
 * @what: What is being allocated (for error logging)
 *
 * Return: pointer to the memory, or NULL if the memory is not available.
 */
void *__must_check vdo_allocate_memory_nowait(size_t size, const char *what);

int __must_check vdo_reallocate_memory(void *ptr, size_t old_size, size_t size,
				       const char *what, void *new_ptr);

int __must_check vdo_duplicate_string(const char *string, const char *what,
				      char **new_string);

/* Free memory allocated with vdo_allocate(). */
void vdo_free(void *ptr);

static inline void *__vdo_forget(void **ptr_ptr)
{
	void *ptr = *ptr_ptr;

	*ptr_ptr = NULL;
	return ptr;
}

/*
 * Null out a pointer and return a copy to it. This macro should be used when passing a pointer to
 * a function for which it is not safe to access the pointer once the function returns.
 */
#define vdo_forget(ptr) __vdo_forget((void **) &(ptr))

void vdo_memory_init(void);

void vdo_memory_exit(void);

void vdo_register_allocating_thread(struct registered_thread *new_thread,
				    const bool *flag_ptr);

void vdo_unregister_allocating_thread(void);

void vdo_get_memory_stats(u64 *bytes_used, u64 *peak_bytes_used);

void vdo_report_memory_usage(void);

#endif /* VDO_MEMORY_ALLOC_H */
