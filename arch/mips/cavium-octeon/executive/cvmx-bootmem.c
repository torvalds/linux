/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 * Simple allocate only memory allocator.  Used to allocate memory at
 * application start time.
 */

#include <linux/export.h>
#include <linux/kernel.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-spinlock.h>
#include <asm/octeon/cvmx-bootmem.h>

/*#define DEBUG */


static struct cvmx_bootmem_desc *cvmx_bootmem_desc;

/* See header file for descriptions of functions */

/**
 * This macro returns a member of the
 * cvmx_bootmem_named_block_desc_t structure. These members can't
 * be directly addressed as they might be in memory not directly
 * reachable. In the case where bootmem is compiled with
 * LINUX_HOST, the structure itself might be located on a remote
 * Octeon. The argument "field" is the member name of the
 * cvmx_bootmem_named_block_desc_t to read. Regardless of the type
 * of the field, the return type is always a uint64_t. The "addr"
 * parameter is the physical address of the structure.
 */
#define CVMX_BOOTMEM_NAMED_GET_FIELD(addr, field)			\
	__cvmx_bootmem_desc_get(addr,					\
		offsetof(struct cvmx_bootmem_named_block_desc, field),	\
		sizeof_field(struct cvmx_bootmem_named_block_desc, field))

/**
 * This function is the implementation of the get macros defined
 * for individual structure members. The argument are generated
 * by the macros inorder to read only the needed memory.
 *
 * @param base   64bit physical address of the complete structure
 * @param offset Offset from the beginning of the structure to the member being
 *               accessed.
 * @param size   Size of the structure member.
 *
 * @return Value of the structure member promoted into a uint64_t.
 */
static inline uint64_t __cvmx_bootmem_desc_get(uint64_t base, int offset,
					       int size)
{
	base = (1ull << 63) | (base + offset);
	switch (size) {
	case 4:
		return cvmx_read64_uint32(base);
	case 8:
		return cvmx_read64_uint64(base);
	default:
		return 0;
	}
}

/*
 * Wrapper functions are provided for reading/writing the size and
 * next block values as these may not be directly addressible (in 32
 * bit applications, for instance.)  Offsets of data elements in
 * bootmem list, must match cvmx_bootmem_block_header_t.
 */
#define NEXT_OFFSET 0
#define SIZE_OFFSET 8

static void cvmx_bootmem_phy_set_size(uint64_t addr, uint64_t size)
{
	cvmx_write64_uint64((addr + SIZE_OFFSET) | (1ull << 63), size);
}

static void cvmx_bootmem_phy_set_next(uint64_t addr, uint64_t next)
{
	cvmx_write64_uint64((addr + NEXT_OFFSET) | (1ull << 63), next);
}

static uint64_t cvmx_bootmem_phy_get_size(uint64_t addr)
{
	return cvmx_read64_uint64((addr + SIZE_OFFSET) | (1ull << 63));
}

static uint64_t cvmx_bootmem_phy_get_next(uint64_t addr)
{
	return cvmx_read64_uint64((addr + NEXT_OFFSET) | (1ull << 63));
}

/**
 * Allocate a block of memory from the free list that was
 * passed to the application by the bootloader within a specified
 * address range. This is an allocate-only algorithm, so
 * freeing memory is not possible. Allocation will fail if
 * memory cannot be allocated in the requested range.
 *
 * @size:      Size in bytes of block to allocate
 * @min_addr:  defines the minimum address of the range
 * @max_addr:  defines the maximum address of the range
 * @alignment: Alignment required - must be power of 2
 * Returns pointer to block of memory, NULL on error
 */
static void *cvmx_bootmem_alloc_range(uint64_t size, uint64_t alignment,
				      uint64_t min_addr, uint64_t max_addr)
{
	int64_t address;
	address =
	    cvmx_bootmem_phy_alloc(size, min_addr, max_addr, alignment, 0);

	if (address > 0)
		return cvmx_phys_to_ptr(address);
	else
		return NULL;
}

void *cvmx_bootmem_alloc_address(uint64_t size, uint64_t address,
				 uint64_t alignment)
{
	return cvmx_bootmem_alloc_range(size, alignment, address,
					address + size);
}

void *cvmx_bootmem_alloc_named_range(uint64_t size, uint64_t min_addr,
				     uint64_t max_addr, uint64_t align,
				     char *name)
{
	int64_t addr;

	addr = cvmx_bootmem_phy_named_block_alloc(size, min_addr, max_addr,
						  align, name, 0);
	if (addr >= 0)
		return cvmx_phys_to_ptr(addr);
	else
		return NULL;
}

void *cvmx_bootmem_alloc_named(uint64_t size, uint64_t alignment, char *name)
{
    return cvmx_bootmem_alloc_named_range(size, 0, 0, alignment, name);
}
EXPORT_SYMBOL(cvmx_bootmem_alloc_named);

void cvmx_bootmem_lock(void)
{
	cvmx_spinlock_lock((cvmx_spinlock_t *) &(cvmx_bootmem_desc->lock));
}

void cvmx_bootmem_unlock(void)
{
	cvmx_spinlock_unlock((cvmx_spinlock_t *) &(cvmx_bootmem_desc->lock));
}

int cvmx_bootmem_init(void *mem_desc_ptr)
{
	/* Here we set the global pointer to the bootmem descriptor
	 * block.  This pointer will be used directly, so we will set
	 * it up to be directly usable by the application.  It is set
	 * up as follows for the various runtime/ABI combinations:
	 *
	 * Linux 64 bit: Set XKPHYS bit
	 * Linux 32 bit: use mmap to create mapping, use virtual address
	 * CVMX 64 bit:	 use physical address directly
	 * CVMX 32 bit:	 use physical address directly
	 *
	 * Note that the CVMX environment assumes the use of 1-1 TLB
	 * mappings so that the physical addresses can be used
	 * directly
	 */
	if (!cvmx_bootmem_desc) {
#if   defined(CVMX_ABI_64)
		/* Set XKPHYS bit */
		cvmx_bootmem_desc = cvmx_phys_to_ptr(CAST64(mem_desc_ptr));
#else
		cvmx_bootmem_desc = (struct cvmx_bootmem_desc *) mem_desc_ptr;
#endif
	}

	return 0;
}

/*
 * The cvmx_bootmem_phy* functions below return 64 bit physical
 * addresses, and expose more features that the cvmx_bootmem_functions
 * above.  These are required for full memory space access in 32 bit
 * applications, as well as for using some advance features.  Most
 * applications should not need to use these.
 */

int64_t cvmx_bootmem_phy_alloc(uint64_t req_size, uint64_t address_min,
			       uint64_t address_max, uint64_t alignment,
			       uint32_t flags)
{

	uint64_t head_addr;
	uint64_t ent_addr;
	/* points to previous list entry, NULL current entry is head of list */
	uint64_t prev_addr = 0;
	uint64_t new_ent_addr = 0;
	uint64_t desired_min_addr;

#ifdef DEBUG
	cvmx_dprintf("cvmx_bootmem_phy_alloc: req_size: 0x%llx, "
		     "min_addr: 0x%llx, max_addr: 0x%llx, align: 0x%llx\n",
		     (unsigned long long)req_size,
		     (unsigned long long)address_min,
		     (unsigned long long)address_max,
		     (unsigned long long)alignment);
#endif

	if (cvmx_bootmem_desc->major_version > 3) {
		cvmx_dprintf("ERROR: Incompatible bootmem descriptor "
			     "version: %d.%d at addr: %p\n",
			     (int)cvmx_bootmem_desc->major_version,
			     (int)cvmx_bootmem_desc->minor_version,
			     cvmx_bootmem_desc);
		goto error_out;
	}

	/*
	 * Do a variety of checks to validate the arguments.  The
	 * allocator code will later assume that these checks have
	 * been made.  We validate that the requested constraints are
	 * not self-contradictory before we look through the list of
	 * available memory.
	 */

	/* 0 is not a valid req_size for this allocator */
	if (!req_size)
		goto error_out;

	/* Round req_size up to mult of minimum alignment bytes */
	req_size = (req_size + (CVMX_BOOTMEM_ALIGNMENT_SIZE - 1)) &
		~(CVMX_BOOTMEM_ALIGNMENT_SIZE - 1);

	/*
	 * Convert !0 address_min and 0 address_max to special case of
	 * range that specifies an exact memory block to allocate.  Do
	 * this before other checks and adjustments so that this
	 * tranformation will be validated.
	 */
	if (address_min && !address_max)
		address_max = address_min + req_size;
	else if (!address_min && !address_max)
		address_max = ~0ull;  /* If no limits given, use max limits */


	/*
	 * Enforce minimum alignment (this also keeps the minimum free block
	 * req_size the same as the alignment req_size.
	 */
	if (alignment < CVMX_BOOTMEM_ALIGNMENT_SIZE)
		alignment = CVMX_BOOTMEM_ALIGNMENT_SIZE;

	/*
	 * Adjust address minimum based on requested alignment (round
	 * up to meet alignment).  Do this here so we can reject
	 * impossible requests up front. (NOP for address_min == 0)
	 */
	if (alignment)
		address_min = ALIGN(address_min, alignment);

	/*
	 * Reject inconsistent args.  We have adjusted these, so this
	 * may fail due to our internal changes even if this check
	 * would pass for the values the user supplied.
	 */
	if (req_size > address_max - address_min)
		goto error_out;

	/* Walk through the list entries - first fit found is returned */

	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_bootmem_lock();
	head_addr = cvmx_bootmem_desc->head_addr;
	ent_addr = head_addr;
	for (; ent_addr;
	     prev_addr = ent_addr,
	     ent_addr = cvmx_bootmem_phy_get_next(ent_addr)) {
		uint64_t usable_base, usable_max;
		uint64_t ent_size = cvmx_bootmem_phy_get_size(ent_addr);

		if (cvmx_bootmem_phy_get_next(ent_addr)
		    && ent_addr > cvmx_bootmem_phy_get_next(ent_addr)) {
			cvmx_dprintf("Internal bootmem_alloc() error: ent: "
				"0x%llx, next: 0x%llx\n",
				(unsigned long long)ent_addr,
				(unsigned long long)
				cvmx_bootmem_phy_get_next(ent_addr));
			goto error_out;
		}

		/*
		 * Determine if this is an entry that can satisify the
		 * request Check to make sure entry is large enough to
		 * satisfy request.
		 */
		usable_base =
		    ALIGN(max(address_min, ent_addr), alignment);
		usable_max = min(address_max, ent_addr + ent_size);
		/*
		 * We should be able to allocate block at address
		 * usable_base.
		 */

		desired_min_addr = usable_base;
		/*
		 * Determine if request can be satisfied from the
		 * current entry.
		 */
		if (!((ent_addr + ent_size) > usable_base
				&& ent_addr < address_max
				&& req_size <= usable_max - usable_base))
			continue;
		/*
		 * We have found an entry that has room to satisfy the
		 * request, so allocate it from this entry.  If end
		 * CVMX_BOOTMEM_FLAG_END_ALLOC set, then allocate from
		 * the end of this block rather than the beginning.
		 */
		if (flags & CVMX_BOOTMEM_FLAG_END_ALLOC) {
			desired_min_addr = usable_max - req_size;
			/*
			 * Align desired address down to required
			 * alignment.
			 */
			desired_min_addr &= ~(alignment - 1);
		}

		/* Match at start of entry */
		if (desired_min_addr == ent_addr) {
			if (req_size < ent_size) {
				/*
				 * big enough to create a new block
				 * from top portion of block.
				 */
				new_ent_addr = ent_addr + req_size;
				cvmx_bootmem_phy_set_next(new_ent_addr,
					cvmx_bootmem_phy_get_next(ent_addr));
				cvmx_bootmem_phy_set_size(new_ent_addr,
							ent_size -
							req_size);

				/*
				 * Adjust next pointer as following
				 * code uses this.
				 */
				cvmx_bootmem_phy_set_next(ent_addr,
							new_ent_addr);
			}

			/*
			 * adjust prev ptr or head to remove this
			 * entry from list.
			 */
			if (prev_addr)
				cvmx_bootmem_phy_set_next(prev_addr,
					cvmx_bootmem_phy_get_next(ent_addr));
			else
				/*
				 * head of list being returned, so
				 * update head ptr.
				 */
				cvmx_bootmem_desc->head_addr =
					cvmx_bootmem_phy_get_next(ent_addr);

			if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
				cvmx_bootmem_unlock();
			return desired_min_addr;
		}
		/*
		 * block returned doesn't start at beginning of entry,
		 * so we know that we will be splitting a block off
		 * the front of this one.  Create a new block from the
		 * beginning, add to list, and go to top of loop
		 * again.
		 *
		 * create new block from high portion of
		 * block, so that top block starts at desired
		 * addr.
		 */
		new_ent_addr = desired_min_addr;
		cvmx_bootmem_phy_set_next(new_ent_addr,
					cvmx_bootmem_phy_get_next
					(ent_addr));
		cvmx_bootmem_phy_set_size(new_ent_addr,
					cvmx_bootmem_phy_get_size
					(ent_addr) -
					(desired_min_addr -
						ent_addr));
		cvmx_bootmem_phy_set_size(ent_addr,
					desired_min_addr - ent_addr);
		cvmx_bootmem_phy_set_next(ent_addr, new_ent_addr);
		/* Loop again to handle actual alloc from new block */
	}
error_out:
	/* We didn't find anything, so return error */
	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_bootmem_unlock();
	return -1;
}

int __cvmx_bootmem_phy_free(uint64_t phy_addr, uint64_t size, uint32_t flags)
{
	uint64_t cur_addr;
	uint64_t prev_addr = 0; /* zero is invalid */
	int retval = 0;

#ifdef DEBUG
	cvmx_dprintf("__cvmx_bootmem_phy_free addr: 0x%llx, size: 0x%llx\n",
		     (unsigned long long)phy_addr, (unsigned long long)size);
#endif
	if (cvmx_bootmem_desc->major_version > 3) {
		cvmx_dprintf("ERROR: Incompatible bootmem descriptor "
			     "version: %d.%d at addr: %p\n",
			     (int)cvmx_bootmem_desc->major_version,
			     (int)cvmx_bootmem_desc->minor_version,
			     cvmx_bootmem_desc);
		return 0;
	}

	/* 0 is not a valid size for this allocator */
	if (!size)
		return 0;

	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_bootmem_lock();
	cur_addr = cvmx_bootmem_desc->head_addr;
	if (cur_addr == 0 || phy_addr < cur_addr) {
		/* add at front of list - special case with changing head ptr */
		if (cur_addr && phy_addr + size > cur_addr)
			goto bootmem_free_done; /* error, overlapping section */
		else if (phy_addr + size == cur_addr) {
			/* Add to front of existing first block */
			cvmx_bootmem_phy_set_next(phy_addr,
						  cvmx_bootmem_phy_get_next
						  (cur_addr));
			cvmx_bootmem_phy_set_size(phy_addr,
						  cvmx_bootmem_phy_get_size
						  (cur_addr) + size);
			cvmx_bootmem_desc->head_addr = phy_addr;

		} else {
			/* New block before first block.  OK if cur_addr is 0 */
			cvmx_bootmem_phy_set_next(phy_addr, cur_addr);
			cvmx_bootmem_phy_set_size(phy_addr, size);
			cvmx_bootmem_desc->head_addr = phy_addr;
		}
		retval = 1;
		goto bootmem_free_done;
	}

	/* Find place in list to add block */
	while (cur_addr && phy_addr > cur_addr) {
		prev_addr = cur_addr;
		cur_addr = cvmx_bootmem_phy_get_next(cur_addr);
	}

	if (!cur_addr) {
		/*
		 * We have reached the end of the list, add on to end,
		 * checking to see if we need to combine with last
		 * block
		 */
		if (prev_addr + cvmx_bootmem_phy_get_size(prev_addr) ==
		    phy_addr) {
			cvmx_bootmem_phy_set_size(prev_addr,
						  cvmx_bootmem_phy_get_size
						  (prev_addr) + size);
		} else {
			cvmx_bootmem_phy_set_next(prev_addr, phy_addr);
			cvmx_bootmem_phy_set_size(phy_addr, size);
			cvmx_bootmem_phy_set_next(phy_addr, 0);
		}
		retval = 1;
		goto bootmem_free_done;
	} else {
		/*
		 * insert between prev and cur nodes, checking for
		 * merge with either/both.
		 */
		if (prev_addr + cvmx_bootmem_phy_get_size(prev_addr) ==
		    phy_addr) {
			/* Merge with previous */
			cvmx_bootmem_phy_set_size(prev_addr,
						  cvmx_bootmem_phy_get_size
						  (prev_addr) + size);
			if (phy_addr + size == cur_addr) {
				/* Also merge with current */
				cvmx_bootmem_phy_set_size(prev_addr,
					cvmx_bootmem_phy_get_size(cur_addr) +
					cvmx_bootmem_phy_get_size(prev_addr));
				cvmx_bootmem_phy_set_next(prev_addr,
					cvmx_bootmem_phy_get_next(cur_addr));
			}
			retval = 1;
			goto bootmem_free_done;
		} else if (phy_addr + size == cur_addr) {
			/* Merge with current */
			cvmx_bootmem_phy_set_size(phy_addr,
						  cvmx_bootmem_phy_get_size
						  (cur_addr) + size);
			cvmx_bootmem_phy_set_next(phy_addr,
						  cvmx_bootmem_phy_get_next
						  (cur_addr));
			cvmx_bootmem_phy_set_next(prev_addr, phy_addr);
			retval = 1;
			goto bootmem_free_done;
		}

		/* It is a standalone block, add in between prev and cur */
		cvmx_bootmem_phy_set_size(phy_addr, size);
		cvmx_bootmem_phy_set_next(phy_addr, cur_addr);
		cvmx_bootmem_phy_set_next(prev_addr, phy_addr);

	}
	retval = 1;

bootmem_free_done:
	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_bootmem_unlock();
	return retval;

}

/**
 * Finds a named memory block by name.
 * Also used for finding an unused entry in the named block table.
 *
 * @name: Name of memory block to find.	 If NULL pointer given, then
 *	  finds unused descriptor, if available.
 *
 * @flags: Flags to control options for the allocation.
 *
 * Returns Pointer to memory block descriptor, NULL if not found.
 *	   If NULL returned when name parameter is NULL, then no memory
 *	   block descriptors are available.
 */
static struct cvmx_bootmem_named_block_desc *
	cvmx_bootmem_phy_named_block_find(char *name, uint32_t flags)
{
	unsigned int i;
	struct cvmx_bootmem_named_block_desc *named_block_array_ptr;

#ifdef DEBUG
	cvmx_dprintf("cvmx_bootmem_phy_named_block_find: %s\n", name);
#endif
	/*
	 * Lock the structure to make sure that it is not being
	 * changed while we are examining it.
	 */
	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_bootmem_lock();

	/* Use XKPHYS for 64 bit linux */
	named_block_array_ptr = (struct cvmx_bootmem_named_block_desc *)
	    cvmx_phys_to_ptr(cvmx_bootmem_desc->named_block_array_addr);

#ifdef DEBUG
	cvmx_dprintf
	    ("cvmx_bootmem_phy_named_block_find: named_block_array_ptr: %p\n",
	     named_block_array_ptr);
#endif
	if (cvmx_bootmem_desc->major_version == 3) {
		for (i = 0;
		     i < cvmx_bootmem_desc->named_block_num_blocks; i++) {
			if ((name && named_block_array_ptr[i].size
			     && !strncmp(name, named_block_array_ptr[i].name,
					 cvmx_bootmem_desc->named_block_name_len
					 - 1))
			    || (!name && !named_block_array_ptr[i].size)) {
				if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
					cvmx_bootmem_unlock();

				return &(named_block_array_ptr[i]);
			}
		}
	} else {
		cvmx_dprintf("ERROR: Incompatible bootmem descriptor "
			     "version: %d.%d at addr: %p\n",
			     (int)cvmx_bootmem_desc->major_version,
			     (int)cvmx_bootmem_desc->minor_version,
			     cvmx_bootmem_desc);
	}
	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_bootmem_unlock();

	return NULL;
}

void *cvmx_bootmem_alloc_named_range_once(uint64_t size, uint64_t min_addr,
					  uint64_t max_addr, uint64_t align,
					  char *name,
					  void (*init) (void *))
{
	int64_t addr;
	void *ptr;
	uint64_t named_block_desc_addr;

	named_block_desc_addr = (uint64_t)
		cvmx_bootmem_phy_named_block_find(name,
						  (uint32_t)CVMX_BOOTMEM_FLAG_NO_LOCKING);

	if (named_block_desc_addr) {
		addr = CVMX_BOOTMEM_NAMED_GET_FIELD(named_block_desc_addr,
						    base_addr);
		return cvmx_phys_to_ptr(addr);
	}

	addr = cvmx_bootmem_phy_named_block_alloc(size, min_addr, max_addr,
						  align, name,
						  (uint32_t)CVMX_BOOTMEM_FLAG_NO_LOCKING);

	if (addr < 0)
		return NULL;
	ptr = cvmx_phys_to_ptr(addr);

	if (init)
		init(ptr);
	else
		memset(ptr, 0, size);

	return ptr;
}
EXPORT_SYMBOL(cvmx_bootmem_alloc_named_range_once);

struct cvmx_bootmem_named_block_desc *cvmx_bootmem_find_named_block(char *name)
{
	return cvmx_bootmem_phy_named_block_find(name, 0);
}
EXPORT_SYMBOL(cvmx_bootmem_find_named_block);

/**
 * Frees a named block.
 *
 * @name:   name of block to free
 * @flags:  flags for passing options
 *
 * Returns 0 on failure
 *	   1 on success
 */
static int cvmx_bootmem_phy_named_block_free(char *name, uint32_t flags)
{
	struct cvmx_bootmem_named_block_desc *named_block_ptr;

	if (cvmx_bootmem_desc->major_version != 3) {
		cvmx_dprintf("ERROR: Incompatible bootmem descriptor version: "
			     "%d.%d at addr: %p\n",
			     (int)cvmx_bootmem_desc->major_version,
			     (int)cvmx_bootmem_desc->minor_version,
			     cvmx_bootmem_desc);
		return 0;
	}
#ifdef DEBUG
	cvmx_dprintf("cvmx_bootmem_phy_named_block_free: %s\n", name);
#endif

	/*
	 * Take lock here, as name lookup/block free/name free need to
	 * be atomic.
	 */
	cvmx_bootmem_lock();

	named_block_ptr =
	    cvmx_bootmem_phy_named_block_find(name,
					      CVMX_BOOTMEM_FLAG_NO_LOCKING);
	if (named_block_ptr) {
#ifdef DEBUG
		cvmx_dprintf("cvmx_bootmem_phy_named_block_free: "
			     "%s, base: 0x%llx, size: 0x%llx\n",
			     name,
			     (unsigned long long)named_block_ptr->base_addr,
			     (unsigned long long)named_block_ptr->size);
#endif
		__cvmx_bootmem_phy_free(named_block_ptr->base_addr,
					named_block_ptr->size,
					CVMX_BOOTMEM_FLAG_NO_LOCKING);
		named_block_ptr->size = 0;
		/* Set size to zero to indicate block not used. */
	}

	cvmx_bootmem_unlock();
	return named_block_ptr != NULL; /* 0 on failure, 1 on success */
}

int cvmx_bootmem_free_named(char *name)
{
	return cvmx_bootmem_phy_named_block_free(name, 0);
}

int64_t cvmx_bootmem_phy_named_block_alloc(uint64_t size, uint64_t min_addr,
					   uint64_t max_addr,
					   uint64_t alignment,
					   char *name,
					   uint32_t flags)
{
	int64_t addr_allocated;
	struct cvmx_bootmem_named_block_desc *named_block_desc_ptr;

#ifdef DEBUG
	cvmx_dprintf("cvmx_bootmem_phy_named_block_alloc: size: 0x%llx, min: "
		     "0x%llx, max: 0x%llx, align: 0x%llx, name: %s\n",
		     (unsigned long long)size,
		     (unsigned long long)min_addr,
		     (unsigned long long)max_addr,
		     (unsigned long long)alignment,
		     name);
#endif
	if (cvmx_bootmem_desc->major_version != 3) {
		cvmx_dprintf("ERROR: Incompatible bootmem descriptor version: "
			     "%d.%d at addr: %p\n",
			     (int)cvmx_bootmem_desc->major_version,
			     (int)cvmx_bootmem_desc->minor_version,
			     cvmx_bootmem_desc);
		return -1;
	}

	/*
	 * Take lock here, as name lookup/block alloc/name add need to
	 * be atomic.
	 */
	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_spinlock_lock((cvmx_spinlock_t *)&(cvmx_bootmem_desc->lock));

	/* Get pointer to first available named block descriptor */
	named_block_desc_ptr =
		cvmx_bootmem_phy_named_block_find(NULL,
						  flags | CVMX_BOOTMEM_FLAG_NO_LOCKING);

	/*
	 * Check to see if name already in use, return error if name
	 * not available or no more room for blocks.
	 */
	if (cvmx_bootmem_phy_named_block_find(name,
					      flags | CVMX_BOOTMEM_FLAG_NO_LOCKING) || !named_block_desc_ptr) {
		if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
			cvmx_spinlock_unlock((cvmx_spinlock_t *)&(cvmx_bootmem_desc->lock));
		return -1;
	}


	/*
	 * Round size up to mult of minimum alignment bytes We need
	 * the actual size allocated to allow for blocks to be
	 * coalesced when they are freed. The alloc routine does the
	 * same rounding up on all allocations.
	 */
	size = ALIGN(size, CVMX_BOOTMEM_ALIGNMENT_SIZE);

	addr_allocated = cvmx_bootmem_phy_alloc(size, min_addr, max_addr,
						alignment,
						flags | CVMX_BOOTMEM_FLAG_NO_LOCKING);
	if (addr_allocated >= 0) {
		named_block_desc_ptr->base_addr = addr_allocated;
		named_block_desc_ptr->size = size;
		strncpy(named_block_desc_ptr->name, name,
			cvmx_bootmem_desc->named_block_name_len);
		named_block_desc_ptr->name[cvmx_bootmem_desc->named_block_name_len - 1] = 0;
	}

	if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
		cvmx_spinlock_unlock((cvmx_spinlock_t *)&(cvmx_bootmem_desc->lock));
	return addr_allocated;
}

struct cvmx_bootmem_desc *cvmx_bootmem_get_desc(void)
{
	return cvmx_bootmem_desc;
}
