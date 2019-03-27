/*
 * Copyright (c) 2016-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PT_BLOCK_CACHE_H
#define PT_BLOCK_CACHE_H

#include "intel-pt.h"

#include <stdint.h>


/* A block cache entry qualifier.
 *
 * This describes what to do at the decision point determined by a block cache
 * entry.
 */
enum pt_bcache_qualifier {
	/* This is not a decision point.
	 *
	 * The next decision point is too far away and one or more fields
	 * threatened to overflow so we had to stop somewhere on our way.
	 *
	 * Apply the displacement and number of instructions and continue from
	 * the resulting IP.
	 */
	ptbq_again,

	/* The decision point is a conditional branch.
	 *
	 * This requires a conditional branch query.
	 *
	 * The isize field should provide the size of the branch instruction so
	 * only taken branches require the instruction to be decoded.
	 */
	ptbq_cond,

	/* The decision point is a near indirect call.
	 *
	 * This requires a return-address stack update and an indirect branch
	 * query.
	 *
	 * The isize field should provide the size of the call instruction so
	 * the return address can be computed by adding it to the displacement
	 * that brings us to the call instruction.
	 *
	 * No instruction decode is required.
	 */
	ptbq_ind_call,

	/* The decision point is a near return.
	 *
	 * The return may be compressed so this requires a conditional branch
	 * query to determine the compression state and either a return-address
	 * stack lookup or an indirect branch query.
	 *
	 * No instruction decode is required.
	 */
	ptbq_return,

	/* The decision point is an indirect jump or far branch.
	 *
	 * This requires an indirect branch query.
	 *
	 * No instruction decode is required.
	 */
	ptbq_indirect,

	/* The decision point requires the instruction at the decision point IP
	 * to be decoded to determine the next step.
	 *
	 * This is used for
	 *
	 *   - near direct calls that need to maintain the return-address stack.
	 *
	 *   - near direct jumps that are too far away to be handled with a
	 *     block cache entry as they would overflow the displacement field.
	 */
	ptbq_decode
};

/* A block cache entry.
 *
 * There will be one such entry per byte of decoded memory image.  Each entry
 * corresponds to an IP in the traced memory image.  The cache is initialized
 * with invalid entries for all IPs.
 *
 * Only entries for the first byte of each instruction will be used; other
 * entries are ignored and will remain invalid.
 *
 * Each valid entry gives the distance from the entry's IP to the next decision
 * point both in bytes and in the number of instructions.
 */
struct pt_bcache_entry {
	/* The displacement to the next decision point in bytes.
	 *
	 * This is zero if we are at a decision point except for ptbq_again
	 * where it gives the displacement to the next block cache entry to be
	 * used.
	 */
	int32_t displacement:16;

	/* The number of instructions to the next decision point.
	 *
	 * This is typically one at a decision point since we are already
	 * accounting for the instruction at the decision point.
	 *
	 * Note that this field must be smaller than the respective struct
	 * pt_block field so we can fit one block cache entry into an empty
	 * block.
	 */
	uint32_t ninsn:8;

	/* The execution mode for all instruction between here and the next
	 * decision point.
	 *
	 * This is enum pt_exec_mode.
	 *
	 * This is ptem_unknown if the entry is not valid.
	 */
	uint32_t mode:2;

	/* The decision point qualifier.
	 *
	 * This is enum pt_bcache_qualifier.
	 */
	uint32_t qualifier:3;

	/* The size of the instruction at the decision point.
	 *
	 * This is zero if the size is too big to fit into the field.  In this
	 * case, the instruction needs to be decoded to determine its size.
	 */
	uint32_t isize:3;
};

/* Get the execution mode of a block cache entry. */
static inline enum pt_exec_mode pt_bce_exec_mode(struct pt_bcache_entry bce)
{
	return (enum pt_exec_mode) bce.mode;
}

/* Get the block cache qualifier of a block cache entry. */
static inline enum pt_bcache_qualifier
pt_bce_qualifier(struct pt_bcache_entry bce)
{
	return (enum pt_bcache_qualifier) bce.qualifier;
}

/* Check if a block cache entry is valid. */
static inline int pt_bce_is_valid(struct pt_bcache_entry bce)
{
	return pt_bce_exec_mode(bce) != ptem_unknown;
}



/* A block cache. */
struct pt_block_cache {
	/* The number of cache entries. */
	uint32_t nentries;

	/* A variable-length array of @nentries entries. */
	struct pt_bcache_entry entry[];
};

/* Create a block cache.
 *
 * @nentries is the number of entries in the cache and should match the size of
 * the to-be-cached section in bytes.
 */
extern struct pt_block_cache *pt_bcache_alloc(uint64_t nentries);

/* Destroy a block cache. */
extern void pt_bcache_free(struct pt_block_cache *bcache);

/* Cache a block.
 *
 * It is expected that all calls for the same @index write the same @bce.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @bcache is NULL.
 * Returns -pte_internal if @index is outside of @bcache.
 */
extern int pt_bcache_add(struct pt_block_cache *bcache, uint64_t index,
			 struct pt_bcache_entry bce);

/* Lookup a cached block.
 *
 * The returned cache entry need not be valid.  The caller is expected to check
 * for validity using pt_bce_is_valid(*@bce).
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @bcache or @bce is NULL.
 * Returns -pte_internal if @index is outside of @bcache.
 */
extern int pt_bcache_lookup(struct pt_bcache_entry *bce,
			    const struct pt_block_cache *bcache,
			    uint64_t index);

#endif /* PT_BLOCK_CACHE_H */
