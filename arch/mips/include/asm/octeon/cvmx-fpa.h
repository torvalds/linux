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

/**
 * @file
 *
 * Interface to the hardware Free Pool Allocator.
 *
 *
 */

#ifndef __CVMX_FPA_H__
#define __CVMX_FPA_H__

#include <linux/delay.h>

#include <asm/octeon/cvmx-address.h>
#include <asm/octeon/cvmx-fpa-defs.h>

#define CVMX_FPA_NUM_POOLS	8
#define CVMX_FPA_MIN_BLOCK_SIZE 128
#define CVMX_FPA_ALIGNMENT	128

/**
 * Structure describing the data format used for stores to the FPA.
 */
typedef union {
	uint64_t u64;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		/*
		 * the (64-bit word) location in scratchpad to write
		 * to (if len != 0)
		 */
		uint64_t scraddr:8;
		/* the number of words in the response (0 => no response) */
		uint64_t len:8;
		/* the ID of the device on the non-coherent bus */
		uint64_t did:8;
		/*
		 * the address that will appear in the first tick on
		 * the NCB bus.
		 */
		uint64_t addr:40;
#else
		uint64_t addr:40;
		uint64_t did:8;
		uint64_t len:8;
		uint64_t scraddr:8;
#endif
	} s;
} cvmx_fpa_iobdma_data_t;

/**
 * Structure describing the current state of a FPA pool.
 */
typedef struct {
	/* Name it was created under */
	const char *name;
	/* Size of each block */
	uint64_t size;
	/* The base memory address of whole block */
	void *base;
	/* The number of elements in the pool at creation */
	uint64_t starting_element_count;
} cvmx_fpa_pool_info_t;

/**
 * Current state of all the pools. Use access functions
 * instead of using it directly.
 */
extern cvmx_fpa_pool_info_t cvmx_fpa_pool_info[CVMX_FPA_NUM_POOLS];

/* CSR typedefs have been moved to cvmx-csr-*.h */

/**
 * Return the name of the pool
 *
 * @pool:   Pool to get the name of
 * Returns The name
 */
static inline const char *cvmx_fpa_get_name(uint64_t pool)
{
	return cvmx_fpa_pool_info[pool].name;
}

/**
 * Return the base of the pool
 *
 * @pool:   Pool to get the base of
 * Returns The base
 */
static inline void *cvmx_fpa_get_base(uint64_t pool)
{
	return cvmx_fpa_pool_info[pool].base;
}

/**
 * Check if a pointer belongs to an FPA pool. Return non-zero
 * if the supplied pointer is inside the memory controlled by
 * an FPA pool.
 *
 * @pool:   Pool to check
 * @ptr:    Pointer to check
 * Returns Non-zero if pointer is in the pool. Zero if not
 */
static inline int cvmx_fpa_is_member(uint64_t pool, void *ptr)
{
	return ((ptr >= cvmx_fpa_pool_info[pool].base) &&
		((char *)ptr <
		 ((char *)(cvmx_fpa_pool_info[pool].base)) +
		 cvmx_fpa_pool_info[pool].size *
		 cvmx_fpa_pool_info[pool].starting_element_count));
}

/**
 * Enable the FPA for use. Must be performed after any CSR
 * configuration but before any other FPA functions.
 */
static inline void cvmx_fpa_enable(void)
{
	union cvmx_fpa_ctl_status status;

	status.u64 = cvmx_read_csr(CVMX_FPA_CTL_STATUS);
	if (status.s.enb) {
		cvmx_dprintf
		    ("Warning: Enabling FPA when FPA already enabled.\n");
	}

	/*
	 * Do runtime check as we allow pass1 compiled code to run on
	 * pass2 chips.
	 */
	if (cvmx_octeon_is_pass1()) {
		union cvmx_fpa_fpfx_marks marks;
		int i;
		for (i = 1; i < 8; i++) {
			marks.u64 =
			    cvmx_read_csr(CVMX_FPA_FPF1_MARKS + (i - 1) * 8ull);
			marks.s.fpf_wr = 0xe0;
			cvmx_write_csr(CVMX_FPA_FPF1_MARKS + (i - 1) * 8ull,
				       marks.u64);
		}

		/* Enforce a 10 cycle delay between config and enable */
		__delay(10);
	}

	/* FIXME: CVMX_FPA_CTL_STATUS read is unmodelled */
	status.u64 = 0;
	status.s.enb = 1;
	cvmx_write_csr(CVMX_FPA_CTL_STATUS, status.u64);
}

/**
 * Get a new block from the FPA
 *
 * @pool:   Pool to get the block from
 * Returns Pointer to the block or NULL on failure
 */
static inline void *cvmx_fpa_alloc(uint64_t pool)
{
	uint64_t address =
	    cvmx_read_csr(CVMX_ADDR_DID(CVMX_FULL_DID(CVMX_OCT_DID_FPA, pool)));
	if (address)
		return cvmx_phys_to_ptr(address);
	else
		return NULL;
}

/**
 * Asynchronously get a new block from the FPA
 *
 * @scr_addr: Local scratch address to put response in.	 This is a byte address,
 *		    but must be 8 byte aligned.
 * @pool:      Pool to get the block from
 */
static inline void cvmx_fpa_async_alloc(uint64_t scr_addr, uint64_t pool)
{
	cvmx_fpa_iobdma_data_t data;

	/*
	 * Hardware only uses 64 bit aligned locations, so convert
	 * from byte address to 64-bit index
	 */
	data.s.scraddr = scr_addr >> 3;
	data.s.len = 1;
	data.s.did = CVMX_FULL_DID(CVMX_OCT_DID_FPA, pool);
	data.s.addr = 0;
	cvmx_send_single(data.u64);
}

/**
 * Free a block allocated with a FPA pool.  Does NOT provide memory
 * ordering in cases where the memory block was modified by the core.
 *
 * @ptr:    Block to free
 * @pool:   Pool to put it in
 * @num_cache_lines:
 *		 Cache lines to invalidate
 */
static inline void cvmx_fpa_free_nosync(void *ptr, uint64_t pool,
					uint64_t num_cache_lines)
{
	cvmx_addr_t newptr;
	newptr.u64 = cvmx_ptr_to_phys(ptr);
	newptr.sfilldidspace.didspace =
	    CVMX_ADDR_DIDSPACE(CVMX_FULL_DID(CVMX_OCT_DID_FPA, pool));
	/* Prevent GCC from reordering around free */
	barrier();
	/* value written is number of cache lines not written back */
	cvmx_write_io(newptr.u64, num_cache_lines);
}

/**
 * Free a block allocated with a FPA pool.  Provides required memory
 * ordering in cases where memory block was modified by core.
 *
 * @ptr:    Block to free
 * @pool:   Pool to put it in
 * @num_cache_lines:
 *		 Cache lines to invalidate
 */
static inline void cvmx_fpa_free(void *ptr, uint64_t pool,
				 uint64_t num_cache_lines)
{
	cvmx_addr_t newptr;
	newptr.u64 = cvmx_ptr_to_phys(ptr);
	newptr.sfilldidspace.didspace =
	    CVMX_ADDR_DIDSPACE(CVMX_FULL_DID(CVMX_OCT_DID_FPA, pool));
	/*
	 * Make sure that any previous writes to memory go out before
	 * we free this buffer.	 This also serves as a barrier to
	 * prevent GCC from reordering operations to after the
	 * free.
	 */
	CVMX_SYNCWS;
	/* value written is number of cache lines not written back */
	cvmx_write_io(newptr.u64, num_cache_lines);
}

/**
 * Setup a FPA pool to control a new block of memory.
 * This can only be called once per pool. Make sure proper
 * locking enforces this.
 *
 * @pool:	Pool to initialize
 *		     0 <= pool < 8
 * @name:	Constant character string to name this pool.
 *		     String is not copied.
 * @buffer:	Pointer to the block of memory to use. This must be
 *		     accessible by all processors and external hardware.
 * @block_size: Size for each block controlled by the FPA
 * @num_blocks: Number of blocks
 *
 * Returns 0 on Success,
 *	   -1 on failure
 */
extern int cvmx_fpa_setup_pool(uint64_t pool, const char *name, void *buffer,
			       uint64_t block_size, uint64_t num_blocks);

/**
 * Shutdown a Memory pool and validate that it had all of
 * the buffers originally placed in it. This should only be
 * called by one processor after all hardware has finished
 * using the pool.
 *
 * @pool:   Pool to shutdown
 * Returns Zero on success
 *	   - Positive is count of missing buffers
 *	   - Negative is too many buffers or corrupted pointers
 */
extern uint64_t cvmx_fpa_shutdown_pool(uint64_t pool);

/**
 * Get the size of blocks controlled by the pool
 * This is resolved to a constant at compile time.
 *
 * @pool:   Pool to access
 * Returns Size of the block in bytes
 */
uint64_t cvmx_fpa_get_block_size(uint64_t pool);

#endif /*  __CVM_FPA_H__ */
