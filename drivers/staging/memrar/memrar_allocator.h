/*
 *      Copyright (C) 2010 Intel Corporation. All rights reserved.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of version 2 of the GNU General
 *      Public License as published by the Free Software Foundation.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 59 Temple Place - Suite 330,
 *      Boston, MA  02111-1307, USA.
 *      The full GNU General Public License is included in this
 *      distribution in the file called COPYING.
 */

#ifndef MEMRAR_ALLOCATOR_H
#define MEMRAR_ALLOCATOR_H


#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/kernel.h>


/**
 * struct memrar_address_range - struct that describes a memory range
 * @begin:	Beginning of available address range.
 * @end:	End of available address range, one past the end,
 *		i.e. [begin, end).
 */
struct memrar_address_range {
/* private: internal use only */
	unsigned long begin;
	unsigned long end;
};

/**
 * struct memrar_address_ranges - list of areas of memory.
 * @list:	Linked list of address ranges.
 * @range:	Memory address range corresponding to given list node.
 */
struct memrar_address_ranges {
/* private: internal use only */
	struct list_head list;
	struct memrar_address_range range;
};

/**
 * struct memrar_allocator - encapsulation of the memory allocator state
 * @lock:		Lock used to synchronize access to the memory
 *			allocator state.
 * @base:		Base (start) address of the allocator memory
 *			space.
 * @capacity:		Size of the allocator memory space in bytes.
 * @block_size:		The size in bytes of individual blocks within
 *			the allocator memory space.
 * @largest_free_area:	Largest free area of memory in the allocator
 *			in bytes.
 * @allocated_list:	List of allocated memory block address
 *			ranges.
 * @free_list:		List of free address ranges.
 *
 * This structure contains all memory allocator state, including the
 * base address, capacity, free list, lock, etc.
 */
struct memrar_allocator {
/* private: internal use only */
	struct mutex lock;
	unsigned long base;
	size_t capacity;
	size_t block_size;
	size_t largest_free_area;
	struct memrar_address_ranges allocated_list;
	struct memrar_address_ranges free_list;
};

/**
 * memrar_create_allocator() - create a memory allocator
 * @base:	Address at which the memory allocator begins.
 * @capacity:	Desired size of the memory allocator.  This value must
 *		be larger than the block_size, ideally more than twice
 *		as large since there wouldn't be much point in using a
 *		memory allocator otherwise.
 * @block_size:	The size of individual blocks within the memory
 *		allocator.  This value must smaller than the
 *		capacity.
 *
 * Create a memory allocator with the given capacity and block size.
 * The capacity will be reduced to be a multiple of the block size, if
 * necessary.
 *
 * Returns an instance of the memory allocator, if creation succeeds,
 * otherwise zero if creation fails.  Failure may occur if not enough
 * kernel memory exists to create the memrar_allocator instance
 * itself, or if the capacity and block_size arguments are not
 * compatible or make sense.
 */
struct memrar_allocator *memrar_create_allocator(unsigned long base,
						 size_t capacity,
						 size_t block_size);

/**
 * memrar_destroy_allocator() - destroy allocator
 * @allocator:	The allocator being destroyed.
 *
 * Reclaim resources held by the memory allocator.  The caller must
 * explicitly free all memory reserved by memrar_allocator_alloc()
 * prior to calling this function.  Otherwise leaks will occur.
 */
void memrar_destroy_allocator(struct memrar_allocator *allocator);

/**
 * memrar_allocator_alloc() - reserve an area of memory of given size
 * @allocator:	The allocator instance being used to reserve buffer.
 * @size:	The size in bytes of the buffer to allocate.
 *
 * This functions reserves an area of memory managed by the given
 * allocator.  It returns zero if allocation was not possible.
 * Failure may occur if the allocator no longer has space available.
 */
unsigned long memrar_allocator_alloc(struct memrar_allocator *allocator,
				     size_t size);

/**
 * memrar_allocator_free() - release buffer starting at given address
 * @allocator:	The allocator instance being used to release the buffer.
 * @address:	The address of the buffer being released.
 *
 * Release an area of memory starting at the given address.  Failure
 * could occur if the given address is not in the address space
 * managed by the allocator.  Returns zero on success or an errno
 * (negative value) on failure.
 */
long memrar_allocator_free(struct memrar_allocator *allocator,
			   unsigned long address);

#endif  /* MEMRAR_ALLOCATOR_H */


/*
  Local Variables:
    c-file-style: "linux"
  End:
*/
