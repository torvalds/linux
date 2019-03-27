/*
 * Copyright (c) 2017-2018, Intel Corporation
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

#ifndef PT_MSEC_CACHE_H
#define PT_MSEC_CACHE_H

#include "pt_mapped_section.h"

#include "intel-pt.h"


/* A single-entry mapped section cache.
 *
 * The cached section is implicitly mapped and unmapped.  The cache is not
 * thread-safe.
 */
struct pt_msec_cache {
	/* The cached section.
	 *
	 * The cache is valid if and only if @msec.section is not NULL.
	 *
	 * It needs to be unmapped and put.  Use pt_blk_scache_invalidate() to
	 * release the cached section and to invalidate the cache.
	 */
	struct pt_mapped_section msec;

	/* The section identifier. */
	int isid;
};

/* Initialize the cache. */
extern int pt_msec_cache_init(struct pt_msec_cache *cache);

/* Finalize the cache. */
extern void pt_msec_cache_fini(struct pt_msec_cache *cache);

/* Invalidate the cache. */
extern int pt_msec_cache_invalidate(struct pt_msec_cache *cache);

/* Read the cached section.
 *
 * If @cache is not empty and @image would find it when looking up @vaddr in
 * @*pmsec->asid, provide a pointer to the cached section in @pmsec and return
 * its image section identifier.
 *
 * The provided pointer remains valid until @cache is invalidated.
 *
 * Returns @*pmsec's isid on success, a negative pt_error_code otherwise.
 */
extern int pt_msec_cache_read(struct pt_msec_cache *cache,
			      const struct pt_mapped_section **pmsec,
			      struct pt_image *image, uint64_t vaddr);

/* Fill the cache.
 *
 * Look up @vaddr in @asid in @image and cache as well as provide the found
 * section in @pmsec and return its image section identifier.
 *
 * Invalidates @cache.
 *
 * The provided pointer remains valid until @cache is invalidated.
 *
 * Returns @*pmsec's isid on success, a negative pt_error_code otherwise.
 */
extern int pt_msec_cache_fill(struct pt_msec_cache *cache,
			      const struct pt_mapped_section **pmsec,
			      struct pt_image *image,
			      const struct pt_asid *asid, uint64_t vaddr);

#endif /* PT_MSEC_CACHE_H */
