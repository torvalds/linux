/*
 * regional.h -- region based memory allocator.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * Regional allocator. Allocates small portions of of larger chunks.
 * Based on region-allocator from NSD, but rewritten to be light.
 *
 * Different from (nsd) region-allocator.h
 * 	o does not have recycle bin
 * 	o does not collect stats; just enough to answer get_mem() in use.
 * 	o does not keep cleanup list
 * 	o does not have function pointers to setup
 * 	o allocs the regional struct inside the first block.
 * 	o can take a block to create regional from.
 * 	o blocks and large allocations are kept on singly linked lists.
 */

#ifndef UTIL_REGIONAL_H_
#define UTIL_REGIONAL_H_

/** 
 * the regional* is the first block*.
 * every block has a ptr to the next in first bytes.
 * and so does the regional struct, which is the first block.
 */
struct regional
{
	/** 
	 * next chunk. NULL if first chunk is the only chunk. 
	 * first inside that chunk is the char* next pointer. 
	 * When regional_free_all() has been called this value is NULL.
	 */
	char* next;
	/** first large object, cast to char** to obtain next ptr */
	char* large_list;
	/** total large size */
	size_t total_large;
	/** initial chunk size */
	size_t first_size;
	/** number of bytes available in the current chunk. */
	size_t available;
	/** current chunk data position. */
	char* data;
};

/**
 * Create a new regional.
 * @return: newly allocated regional.
 */
struct regional* regional_create(void);

/**
 * Create a new region, with custom settings.
 * @param size: length of first block.
 * @return: newly allocated regional.
 */
struct regional* regional_create_custom(size_t size);
	
/**
 * Free all memory associated with regional. Only keeps the first block with
 * the regional inside it.
 * @param r: the region.
 */
void regional_free_all(struct regional *r);

/**
 * Destroy regional.  All memory associated with regional is freed as if
 * regional_free_all was called, as well as destroying the regional struct.
 * @param r: to delete.
 */
void regional_destroy(struct regional *r);

/**
 * Allocate size bytes of memory inside regional.  The memory is
 * deallocated when region_free_all is called for this region.
 * @param r: the region.
 * @param size: number of bytes.
 * @return: pointer to memory allocated.
 */
void *regional_alloc(struct regional *r, size_t size);

/**
 * Allocate size bytes of memory inside regional and copy INIT into it.
 * The memory is deallocated when region_free_all is called for this
 * region.
 * @param r: the region.
 * @param init: to copy.
 * @param size: number of bytes.
 * @return: pointer to memory allocated.
 */
void *regional_alloc_init(struct regional* r, const void *init, size_t size);

/**
 * Allocate size bytes of memory inside regional that are initialized to
 * 0.  The memory is deallocated when region_free_all is called for
 * this region.
 * @param r: the region.
 * @param size: number of bytes.
 * @return: pointer to memory allocated.
 */
void *regional_alloc_zero(struct regional *r, size_t size);

/**
 * Duplicate string and allocate the result in regional.
 * @param r: the region.
 * @param string: null terminated string.
 * @return: pointer to memory allocated.
 */
char *regional_strdup(struct regional *r, const char *string);

/** Debug print regional statistics to log */
void regional_log_stats(struct regional *r);

/** get total memory size in use by region */
size_t regional_get_mem(struct regional* r);

#endif /* UTIL_REGIONAL_H_ */
