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

#ifndef PT_IMAGE_SECTION_CACHE_H
#define PT_IMAGE_SECTION_CACHE_H

#include <stdint.h>

#if defined(FEATURE_THREADS)
#  include <threads.h>
#endif /* defined(FEATURE_THREADS) */

struct pt_section;


/* An image section cache entry. */
struct pt_iscache_entry {
	/* The section object.
	 *
	 * We hold a reference to the section - put it when the section is
	 * removed from the cache.
	 */
	struct pt_section *section;

	/* The base address at which @section has been loaded. */
	uint64_t laddr;
};

/* An image section cache least recently used cache entry. */
struct pt_iscache_lru_entry {
	/* The next entry in a list ordered by recent use. */
	struct pt_iscache_lru_entry *next;

	/* The section mapped by the image section cache. */
	struct pt_section *section;

	/* The amount of memory used by mapping @section in bytes. */
	uint64_t size;
};

/* A cache of image sections and their load addresses.
 *
 * We combine the section with its load address to reduce the amount of
 * information we need to store in order to read from a cached section by
 * virtual address.
 *
 * Internally, the section object will be shared if it is loaded at different
 * addresses in the cache.
 *
 * The cache does not consider the address-space the section is mapped into.
 * This is not relevant for reading from the section.
 */
struct pt_image_section_cache {
	/* The optional name of the cache; NULL if not named. */
	char *name;

	/* An array of @nentries cached sections. */
	struct pt_iscache_entry *entries;

	/* A list of mapped sections ordered by time of last access. */
	struct pt_iscache_lru_entry *lru;

	/* The memory limit for our LRU cache. */
	uint64_t limit;

	/* The current size of our LRU cache. */
	uint64_t used;

#if defined(FEATURE_THREADS)
	/* A lock protecting this image section cache. */
	mtx_t lock;
#endif /* defined(FEATURE_THREADS) */

	/* The capacity of the @entries array.
	 *
	 * Cached sections are identified by a positive integer, the image
	 * section identifier (isid), which is derived from their index into the
	 * @entries array.
	 *
	 * We can't expand the section cache capacity beyond INT_MAX.
	 */
	uint16_t capacity;

	/* The current size of the cache in number of entries.
	 *
	 * This is smaller than @capacity if there is still room in the @entries
	 * array; equal to @capacity if the @entries array is full and needs to
	 * be reallocated.
	 */
	uint16_t size;
};


/* Initialize an image section cache. */
extern int pt_iscache_init(struct pt_image_section_cache *iscache,
			   const char *name);

/* Finalize an image section cache. */
extern void pt_iscache_fini(struct pt_image_section_cache *iscache);

/* Add a section to the cache.
 *
 * Adds @section at @laddr to @iscache and returns its isid.  If a similar
 * section is already cached, returns that section's isid, instead.
 *
 * We take a full section rather than its filename and range in that file to
 * avoid the dependency to pt_section.h.  Callers are expected to query the
 * cache before creating the section, so we should only see unnecessary section
 * creation/destruction on insertion races.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @iscache or @section is NULL.
 * Returns -pte_internal if @section's filename is NULL.
 */
extern int pt_iscache_add(struct pt_image_section_cache *iscache,
			  struct pt_section *section, uint64_t laddr);

/* Find a section in the cache.
 *
 * Returns a positive isid if a section matching @filename, @offset, @size
 * loaded at @laddr is found in @iscache.
 * Returns zero if no such section is found.
 * Returns a negative error code otherwise.
 * Returns -pte_internal if @iscache or @filename is NULL.
 */
extern int pt_iscache_find(struct pt_image_section_cache *iscache,
			   const char *filename, uint64_t offset,
			   uint64_t size, uint64_t laddr);

/* Lookup the section identified by its isid.
 *
 * Provides a reference to the section in @section and its load address in
 * @laddr on success.  The caller is expected to put the returned section after
 * use.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @iscache, @section, or @laddr is NULL.
 * Returns -pte_bad_image if @iscache does not contain @isid.
 */
extern int pt_iscache_lookup(struct pt_image_section_cache *iscache,
			     struct pt_section **section, uint64_t *laddr,
			     int isid);

/* Clear an image section cache. */
extern int pt_iscache_clear(struct pt_image_section_cache *iscache);

/* Notify about the mapping of a cached section.
 *
 * Notifies @iscache that @section has been mapped.
 *
 * The caller guarantees that @iscache contains @section (by using @section's
 * iscache pointer) and prevents @iscache from detaching.
 *
 * The caller must not lock @section to allow @iscache to map it.  This function
 * must not try to detach from @section.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 * Returns -pte_internal if @iscache or @section is NULL.
 * Returns -pte_bad_lock on any locking error.
 */
extern int pt_iscache_notify_map(struct pt_image_section_cache *iscache,
				 struct pt_section *section);

/* Notify about a size change of a mapped section.
 *
 * Notifies @iscache that @section's size has changed while it was mapped.
 *
 * The caller guarantees that @iscache contains @section (by using @section's
 * iscache pointer) and prevents @iscache from detaching.
 *
 * The caller must not lock @section to allow @iscache to map it.  This function
 * must not try to detach from @section.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 * Returns -pte_internal if @iscache or @section is NULL.
 * Returns -pte_bad_lock on any locking error.
 */
extern int pt_iscache_notify_resize(struct pt_image_section_cache *iscache,
				    struct pt_section *section, uint64_t size);

#endif /* PT_IMAGE_SECTION_CACHE_H */
