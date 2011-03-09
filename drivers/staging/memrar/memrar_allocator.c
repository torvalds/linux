/*
 *      memrar_allocator 1.0:  An allocator for Intel RAR.
 *
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
 *
 *
 *  ------------------------------------------------------------------
 *
 *      This simple allocator implementation provides a
 *      malloc()/free()-like interface for reserving space within a
 *      previously reserved block of memory.  It is not specific to
 *      any hardware, nor is it coupled with the lower level paging
 *      mechanism.
 *
 *      The primary goal of this implementation is to provide a means
 *      to partition an arbitrary block of memory without actually
 *      accessing the memory or incurring any hardware side-effects
 *      (e.g. paging).  It is, in effect, a bookkeeping mechanism for
 *      buffers.
 */


#include "memrar_allocator.h"
#include <linux/slab.h>
#include <linux/bug.h>
#include <linux/kernel.h>


struct memrar_allocator *memrar_create_allocator(unsigned long base,
						 size_t capacity,
						 size_t block_size)
{
	struct memrar_allocator *allocator  = NULL;
	struct memrar_address_ranges *first_node = NULL;

	/*
	 * Make sure the base address is aligned on a block_size
	 * boundary.
	 *
	 * @todo Is this necessary?
	 */
	/* base = ALIGN(base, block_size); */

	/* Validate parameters.
	 *
	 * Make sure we can allocate the entire memory space.  Zero
	 * capacity or block size are obviously invalid.
	 */
	if (base == 0
	    || capacity == 0
	    || block_size == 0
	    || ULONG_MAX - capacity < base
	    || capacity < block_size)
		return allocator;

	/*
	 * There isn't much point in creating a memory allocator that
	 * is only capable of holding one block but we'll allow it,
	 * and issue a diagnostic.
	 */
	WARN(capacity < block_size * 2,
	     "memrar: Only one block available to allocator.\n");

	allocator = kmalloc(sizeof(*allocator), GFP_KERNEL);

	if (allocator == NULL)
		return allocator;

	mutex_init(&allocator->lock);
	allocator->base = base;

	/* Round the capacity down to a multiple of block_size. */
	allocator->capacity = (capacity / block_size) * block_size;

	allocator->block_size = block_size;

	allocator->largest_free_area = allocator->capacity;

	/* Initialize the handle and free lists. */
	INIT_LIST_HEAD(&allocator->allocated_list.list);
	INIT_LIST_HEAD(&allocator->free_list.list);

	first_node = kmalloc(sizeof(*first_node), GFP_KERNEL);
	if (first_node == NULL)	{
		kfree(allocator);
		allocator = NULL;
	} else {
		/* Full range of blocks is available. */
		first_node->range.begin = base;
		first_node->range.end   = base + allocator->capacity;
		list_add(&first_node->list,
			 &allocator->free_list.list);
	}

	return allocator;
}

void memrar_destroy_allocator(struct memrar_allocator *allocator)
{
	/*
	 * Assume that the memory allocator lock isn't held at this
	 * point in time.  Caller must ensure that.
	 */

	struct memrar_address_ranges *pos = NULL;
	struct memrar_address_ranges *n   = NULL;

	if (allocator == NULL)
		return;

	mutex_lock(&allocator->lock);

	/* Reclaim free list resources. */
	list_for_each_entry_safe(pos,
				 n,
				 &allocator->free_list.list,
				 list) {
		list_del(&pos->list);
		kfree(pos);
	}

	mutex_unlock(&allocator->lock);

	kfree(allocator);
}

unsigned long memrar_allocator_alloc(struct memrar_allocator *allocator,
				     size_t size)
{
	struct memrar_address_ranges *pos = NULL;

	size_t num_blocks;
	unsigned long reserved_bytes;

	/*
	 * Address of allocated buffer.  We assume that zero is not a
	 * valid address.
	 */
	unsigned long addr = 0;

	if (allocator == NULL || size == 0)
		return addr;

	/* Reserve enough blocks to hold the amount of bytes requested. */
	num_blocks = DIV_ROUND_UP(size, allocator->block_size);

	reserved_bytes = num_blocks * allocator->block_size;

	mutex_lock(&allocator->lock);

	if (reserved_bytes > allocator->largest_free_area) {
		mutex_unlock(&allocator->lock);
		return addr;
	}

	/*
	 * Iterate through the free list to find a suitably sized
	 * range of free contiguous memory blocks.
	 *
	 * We also take the opportunity to reset the size of the
	 * largest free area size statistic.
	 */
	list_for_each_entry(pos, &allocator->free_list.list, list) {
		struct memrar_address_range * const fr = &pos->range;
		size_t const curr_size = fr->end - fr->begin;

		if (curr_size >= reserved_bytes && addr == 0) {
			struct memrar_address_range *range = NULL;
			struct memrar_address_ranges * const new_node =
				kmalloc(sizeof(*new_node), GFP_KERNEL);

			if (new_node == NULL)
				break;

			list_add(&new_node->list,
				 &allocator->allocated_list.list);

			/*
			 * Carve out area of memory from end of free
			 * range.
			 */
			range        = &new_node->range;
			range->end   = fr->end;
			fr->end     -= reserved_bytes;
			range->begin = fr->end;
			addr         = range->begin;

			/*
			 * Check if largest area has decreased in
			 * size.  We'll need to continue scanning for
			 * the next largest area if it has.
			 */
			if (curr_size == allocator->largest_free_area)
				allocator->largest_free_area -=
					reserved_bytes;
			else
				break;
		}

		/*
		 * Reset largest free area size statistic as needed,
		 * but only if we've actually allocated memory.
		 */
		if (addr != 0
		    && curr_size > allocator->largest_free_area) {
			allocator->largest_free_area = curr_size;
			break;
		}
	}

	mutex_unlock(&allocator->lock);

	return addr;
}

long memrar_allocator_free(struct memrar_allocator *allocator,
			   unsigned long addr)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct list_head *dst = NULL;

	struct memrar_address_ranges      *allocated = NULL;
	struct memrar_address_range const *handle    = NULL;

	unsigned long old_end        = 0;
	unsigned long new_chunk_size = 0;

	if (allocator == NULL)
		return -EINVAL;

	if (addr == 0)
		return 0;  /* Ignore "free(0)". */

	mutex_lock(&allocator->lock);

	/* Find the corresponding handle. */
	list_for_each_entry(allocated,
			    &allocator->allocated_list.list,
			    list) {
		if (allocated->range.begin == addr) {
			handle = &allocated->range;
			break;
		}
	}

	/* No such buffer created by this allocator. */
	if (handle == NULL) {
		mutex_unlock(&allocator->lock);
		return -EFAULT;
	}

	/*
	 * Coalesce adjacent chunks of memory if possible.
	 *
	 * @note This isn't full blown coalescing since we're only
	 *       coalescing at most three chunks of memory.
	 */
	list_for_each_safe(pos, tmp, &allocator->free_list.list) {
		/* @todo O(n) performance.  Optimize. */

		struct memrar_address_range * const chunk =
			&list_entry(pos,
				    struct memrar_address_ranges,
				    list)->range;

		/* Extend size of existing free adjacent chunk. */
		if (chunk->end == handle->begin) {
			/*
			 * Chunk "less than" than the one we're
			 * freeing is adjacent.
			 *
			 * Before:
			 *
			 *   +-----+------+
			 *   |chunk|handle|
			 *   +-----+------+
			 *
			 * After:
			 *
			 *   +------------+
			 *   |   chunk    |
			 *   +------------+
			 */

			struct memrar_address_ranges const * const next =
				list_entry(pos->next,
					   struct memrar_address_ranges,
					   list);

			chunk->end = handle->end;

			/*
			 * Now check if next free chunk is adjacent to
			 * the current extended free chunk.
			 *
			 * Before:
			 *
			 *   +------------+----+
			 *   |   chunk    |next|
			 *   +------------+----+
			 *
			 * After:
			 *
			 *   +-----------------+
			 *   |      chunk      |
			 *   +-----------------+
			 */
			if (!list_is_singular(pos)
			    && chunk->end == next->range.begin) {
				chunk->end = next->range.end;
				list_del(pos->next);
				kfree(next);
			}

			list_del(&allocated->list);

			new_chunk_size = chunk->end - chunk->begin;

			goto exit_memrar_free;

		} else if (handle->end == chunk->begin) {
			/*
			 * Chunk "greater than" than the one we're
			 * freeing is adjacent.
			 *
			 *   +------+-----+
			 *   |handle|chunk|
			 *   +------+-----+
			 *
			 * After:
			 *
			 *   +------------+
			 *   |   chunk    |
			 *   +------------+
			 */

			struct memrar_address_ranges const * const prev =
				list_entry(pos->prev,
					   struct memrar_address_ranges,
					   list);

			chunk->begin = handle->begin;

			/*
			 * Now check if previous free chunk is
			 * adjacent to the current extended free
			 * chunk.
			 *
			 *
			 * Before:
			 *
			 *   +----+------------+
			 *   |prev|   chunk    |
			 *   +----+------------+
			 *
			 * After:
			 *
			 *   +-----------------+
			 *   |      chunk      |
			 *   +-----------------+
			 */
			if (!list_is_singular(pos)
			    && prev->range.end == chunk->begin) {
				chunk->begin = prev->range.begin;
				list_del(pos->prev);
				kfree(prev);
			}

			list_del(&allocated->list);

			new_chunk_size = chunk->end - chunk->begin;

			goto exit_memrar_free;

		} else if (chunk->end < handle->begin
			   && chunk->end > old_end) {
			/* Keep track of where the entry could be
			 * potentially moved from the "allocated" list
			 * to the "free" list if coalescing doesn't
			 * occur, making sure the "free" list remains
			 * sorted.
			 */
			old_end = chunk->end;
			dst = pos;
		}
	}

	/*
	 * Nothing to coalesce.
	 *
	 * Move the entry from the "allocated" list to the "free"
	 * list.
	 */
	list_move(&allocated->list, dst);
	new_chunk_size = handle->end - handle->begin;
	allocated = NULL;

exit_memrar_free:

	if (new_chunk_size > allocator->largest_free_area)
		allocator->largest_free_area = new_chunk_size;

	mutex_unlock(&allocator->lock);

	kfree(allocated);

	return 0;
}



/*
  Local Variables:
    c-file-style: "linux"
  End:
*/
