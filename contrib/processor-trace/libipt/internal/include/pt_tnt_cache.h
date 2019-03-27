/*
 * Copyright (c) 2013-2018, Intel Corporation
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

#ifndef PT_TNT_CACHE_H
#define PT_TNT_CACHE_H

#include <stdint.h>

struct pt_packet_tnt;
struct pt_config;


/* Keeping track of tnt indicators. */
struct pt_tnt_cache {
	/* The last tnt. */
	uint64_t tnt;

	/* The index into the above tnt.
	 *
	 * (tnt & index) gives the current tnt entry.
	 * (index >>= 1) moves the index to the next tnt entry.
	 * (index == 0) means that the current tnt is empty.
	 */
	uint64_t index;
};


/* Initialize (or reset) the tnt cache. */
extern void pt_tnt_cache_init(struct pt_tnt_cache *cache);

/* Check if the tnt cache is empty.
 *
 * Returns 0 if the tnt cache is not empty.
 * Returns > 0 if the tnt cache is empty.
 * Returns -pte_invalid if @cache is NULL.
 */
extern int pt_tnt_cache_is_empty(const struct pt_tnt_cache *cache);

/* Query the next tnt indicator.
 *
 * This consumes the returned tnt indicator in the cache.
 *
 * Returns 0 if the next branch is not taken.
 * Returns > 0 if the next branch is taken.
 * Returns -pte_invalid if @cache is NULL.
 * Returns -pte_bad_query if there is no tnt cached.
 */
extern int pt_tnt_cache_query(struct pt_tnt_cache *cache);

/* Update the tnt cache based on Intel PT packets.
 *
 * Updates @cache based on @packet and, if non-null, @config.
 *
 * Returns zero on success.
 * Returns -pte_invalid if @cache or @packet is NULL.
 * Returns -pte_bad_packet if @packet appears to be corrupted.
 * Returns -pte_bad_context if the tnt cache is not empty.
 */
extern int pt_tnt_cache_update_tnt(struct pt_tnt_cache *cache,
				   const struct pt_packet_tnt *packet,
				   const struct pt_config *config);

#endif /* PT_TNT_CACHE_H */
