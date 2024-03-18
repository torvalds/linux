/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_MEMORY_ALLOC_H
#define VDO_MEMORY_ALLOC_H

#include <linux/cache.h>
#include <linux/io.h> /* for PAGE_SIZE */

#include "permassert.h"
#include "thread-registry.h"

/* Custom memory allocation function that tracks memory usage */
int __must_check vdo_allocate_memory(size_t size, size_t align, const char *what, void *ptr);

/*
 * Allocate storage based on element counts, sizes, and alignment.
 *
 * This is a generalized form of our allocation use case: It allocates an array of objects,
 * optionally preceded by one object of another type (i.e., a struct with trailing variable-length
 * array), with the alignment indicated.
 *
 * Why is this inline? The sizes and alignment will always be constant, when invoked through the
 * macros below, and often the count will be a compile-time constant 1 or the number of extra bytes
 * will be a compile-time constant 0. So at least some of the arithmetic can usually be optimized
 * away, and the run-time selection between allocation functions always can. In many cases, it'll
 * boil down to just a function call with a constant size.
 *
 * @count: The number of objects to allocate
 * @size: The size of an object
 * @extra: The number of additional bytes to allocate
 * @align: The required alignment
 * @what: What is being allocated (for error logging)
 * @ptr: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
static inline int __vdo_do_allocation(size_t count, size_t size, size_t extra,
				      size_t align, const char *what, void *ptr)
{
	size_t total_size = count * size + extra;

	/* Overflow check: */
	if ((size > 0) && (count > ((SIZE_MAX - extra) / size))) {
		/*
		 * This is kind of a hack: We rely on the fact that SIZE_MAX would cover the entire
		 * address space (minus one byte) and thus the system can never allocate that much
		 * and the call will always fail. So we can report an overflow as "out of memory"
		 * by asking for "merely" SIZE_MAX bytes.
		 */
		total_size = SIZE_MAX;
	}

	return vdo_allocate_memory(total_size, align, what, ptr);
}

/*
 * Allocate one or more elements of the indicated type, logging an error if the allocation fails.
 * The memory will be zeroed.
 *
 * @COUNT: The number of objects to allocate
 * @TYPE: The type of objects to allocate. This type determines the alignment of the allocation.
 * @WHAT: What is being allocated (for error logging)
 * @PTR: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
#define vdo_allocate(COUNT, TYPE, WHAT, PTR) \
	__vdo_do_allocation(COUNT, sizeof(TYPE), 0, __alignof__(TYPE), WHAT, PTR)

/*
 * Allocate one object of an indicated type, followed by one or more elements of a second type,
 * logging an error if the allocation fails. The memory will be zeroed.
 *
 * @TYPE1: The type of the primary object to allocate. This type determines the alignment of the
 *         allocated memory.
 * @COUNT: The number of objects to allocate
 * @TYPE2: The type of array objects to allocate
 * @WHAT: What is being allocated (for error logging)
 * @PTR: A pointer to hold the allocated memory
 *
 * Return: VDO_SUCCESS or an error code
 */
#define vdo_allocate_extended(TYPE1, COUNT, TYPE2, WHAT, PTR)		\
	__extension__({							\
		int _result;						\
		TYPE1 **_ptr = (PTR);					\
		BUILD_BUG_ON(__alignof__(TYPE1) < __alignof__(TYPE2));	\
		_result = __vdo_do_allocation(COUNT,			\
					      sizeof(TYPE2),		\
					      sizeof(TYPE1),		\
					      __alignof__(TYPE1),	\
					      WHAT,			\
					      _ptr);			\
		_result;						\
	})

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
