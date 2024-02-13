// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "sparse-cache.h"

#include <linux/cache.h>
#include <linux/delay.h>
#include <linux/dm-bufio.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "chapter-index.h"
#include "config.h"
#include "index.h"

/*
 * Since the cache is small, it is implemented as a simple array of cache entries. Searching for a
 * specific virtual chapter is implemented as a linear search. The cache replacement policy is
 * least-recently-used (LRU). Again, the small size of the cache allows the LRU order to be
 * maintained by shifting entries in an array list.
 *
 * Changing the contents of the cache requires the coordinated participation of all zone threads
 * via the careful use of barrier messages sent to all the index zones by the triage queue worker
 * thread. The critical invariant for coordination is that the cache membership must not change
 * between updates, so that all calls to uds_sparse_cache_contains() from the zone threads must all
 * receive the same results for every virtual chapter number. To ensure that critical invariant,
 * state changes such as "that virtual chapter is no longer in the volume" and "skip searching that
 * chapter because it has had too many cache misses" are represented separately from the cache
 * membership information (the virtual chapter number).
 *
 * As a result of this invariant, we have the guarantee that every zone thread will call
 * uds_update_sparse_cache() once and exactly once to request a chapter that is not in the cache,
 * and the serialization of the barrier requests from the triage queue ensures they will all
 * request the same chapter number. This means the only synchronization we need can be provided by
 * a pair of thread barriers used only in the uds_update_sparse_cache() call, providing a critical
 * section where a single zone thread can drive the cache update while all the other zone threads
 * are known to be blocked, waiting in the second barrier. Outside that critical section, all the
 * zone threads implicitly hold a shared lock. Inside it, the thread for zone zero holds an
 * exclusive lock. No other threads may access or modify the cache entries.
 *
 * Chapter statistics must only be modified by a single thread, which is also the zone zero thread.
 * All fields that might be frequently updated by that thread are kept in separate cache-aligned
 * structures so they will not cause cache contention via "false sharing" with the fields that are
 * frequently accessed by all of the zone threads.
 *
 * The LRU order is managed independently by each zone thread, and each zone uses its own list for
 * searching and cache membership queries. The zone zero list is used to decide which chapter to
 * evict when the cache is updated, and its search list is copied to the other threads at that
 * time.
 *
 * The virtual chapter number field of the cache entry is the single field indicating whether a
 * chapter is a member of the cache or not. The value NO_CHAPTER is used to represent a null or
 * undefined chapter number. When present in the virtual chapter number field of a
 * cached_chapter_index, it indicates that the cache entry is dead, and all the other fields of
 * that entry (other than immutable pointers to cache memory) are undefined and irrelevant. Any
 * cache entry that is not marked as dead is fully defined and a member of the cache, and
 * uds_sparse_cache_contains() will always return true for any virtual chapter number that appears
 * in any of the cache entries.
 *
 * A chapter index that is a member of the cache may be excluded from searches between calls to
 * uds_update_sparse_cache() in two different ways. First, when a chapter falls off the end of the
 * volume, its virtual chapter number will be less that the oldest virtual chapter number. Since
 * that chapter is no longer part of the volume, there's no point in continuing to search that
 * chapter index. Once invalidated, that virtual chapter will still be considered a member of the
 * cache, but it will no longer be searched for matching names.
 *
 * The second mechanism is a heuristic based on keeping track of the number of consecutive search
 * misses in a given chapter index. Once that count exceeds a threshold, the skip_search flag will
 * be set to true, causing the chapter to be skipped when searching the entire cache, but still
 * allowing it to be found when searching for a hook in that specific chapter. Finding a hook will
 * clear the skip_search flag, once again allowing the non-hook searches to use that cache entry.
 * Again, regardless of the state of the skip_search flag, the virtual chapter must still
 * considered to be a member of the cache for uds_sparse_cache_contains().
 */

#define SKIP_SEARCH_THRESHOLD 20000
#define ZONE_ZERO 0

/*
 * These counters are essentially fields of the struct cached_chapter_index, but are segregated
 * into this structure because they are frequently modified. They are grouped and aligned to keep
 * them on different cache lines from the chapter fields that are accessed far more often than they
 * are updated.
 */
struct __aligned(L1_CACHE_BYTES) cached_index_counters {
	u64 consecutive_misses;
};

struct __aligned(L1_CACHE_BYTES) cached_chapter_index {
	/*
	 * The virtual chapter number of the cached chapter index. NO_CHAPTER means this cache
	 * entry is unused. This field must only be modified in the critical section in
	 * uds_update_sparse_cache().
	 */
	u64 virtual_chapter;

	u32 index_pages_count;

	/*
	 * These pointers are immutable during the life of the cache. The contents of the arrays
	 * change when the cache entry is replaced.
	 */
	struct delta_index_page *index_pages;
	struct dm_buffer **page_buffers;

	/*
	 * If set, skip the chapter when searching the entire cache. This flag is just a
	 * performance optimization. This flag is mutable between cache updates, but it rarely
	 * changes and is frequently accessed, so it groups with the immutable fields.
	 */
	bool skip_search;

	/*
	 * The cache-aligned counters change often and are placed at the end of the structure to
	 * prevent false sharing with the more stable fields above.
	 */
	struct cached_index_counters counters;
};

/*
 * A search_list represents an ordering of the sparse chapter index cache entry array, from most
 * recently accessed to least recently accessed, which is the order in which the indexes should be
 * searched and the reverse order in which they should be evicted from the cache.
 *
 * Cache entries that are dead or empty are kept at the end of the list, avoiding the need to even
 * iterate over them to search, and ensuring that dead entries are replaced before any live entries
 * are evicted.
 *
 * The search list is instantiated for each zone thread, avoiding any need for synchronization. The
 * structure is allocated on a cache boundary to avoid false sharing of memory cache lines between
 * zone threads.
 */
struct search_list {
	u8 capacity;
	u8 first_dead_entry;
	struct cached_chapter_index *entries[];
};

struct threads_barrier {
	/* Lock for this barrier object */
	struct semaphore lock;
	/* Semaphore for threads waiting at this barrier */
	struct semaphore wait;
	/* Number of threads which have arrived */
	int arrived;
	/* Total number of threads using this barrier */
	int thread_count;
};

struct sparse_cache {
	const struct index_geometry *geometry;
	unsigned int capacity;
	unsigned int zone_count;

	unsigned int skip_threshold;
	struct search_list *search_lists[MAX_ZONES];
	struct cached_chapter_index **scratch_entries;

	struct threads_barrier begin_update_barrier;
	struct threads_barrier end_update_barrier;

	struct cached_chapter_index chapters[];
};

static void initialize_threads_barrier(struct threads_barrier *barrier,
				       unsigned int thread_count)
{
	sema_init(&barrier->lock, 1);
	barrier->arrived = 0;
	barrier->thread_count = thread_count;
	sema_init(&barrier->wait, 0);
}

static inline void __down(struct semaphore *semaphore)
{
	/*
	 * Do not use down(semaphore). Instead use down_interruptible so that
	 * we do not get 120 second stall messages in kern.log.
	 */
	while (down_interruptible(semaphore) != 0) {
		/*
		 * If we're called from a user-mode process (e.g., "dmsetup
		 * remove") while waiting for an operation that may take a
		 * while (e.g., UDS index save), and a signal is sent (SIGINT,
		 * SIGUSR2), then down_interruptible will not block. If that
		 * happens, sleep briefly to avoid keeping the CPU locked up in
		 * this loop. We could just call cond_resched, but then we'd
		 * still keep consuming CPU time slices and swamp other threads
		 * trying to do computational work.
		 */
		fsleep(1000);
	}
}

static void enter_threads_barrier(struct threads_barrier *barrier)
{
	__down(&barrier->lock);
	if (++barrier->arrived == barrier->thread_count) {
		/* last thread */
		int i;

		for (i = 1; i < barrier->thread_count; i++)
			up(&barrier->wait);

		barrier->arrived = 0;
		up(&barrier->lock);
	} else {
		up(&barrier->lock);
		__down(&barrier->wait);
	}
}

static int __must_check initialize_cached_chapter_index(struct cached_chapter_index *chapter,
							const struct index_geometry *geometry)
{
	int result;

	chapter->virtual_chapter = NO_CHAPTER;
	chapter->index_pages_count = geometry->index_pages_per_chapter;

	result = vdo_allocate(chapter->index_pages_count, struct delta_index_page,
			      __func__, &chapter->index_pages);
	if (result != VDO_SUCCESS)
		return result;

	return vdo_allocate(chapter->index_pages_count, struct dm_buffer *,
			    "sparse index volume pages", &chapter->page_buffers);
}

static int __must_check make_search_list(struct sparse_cache *cache,
					 struct search_list **list_ptr)
{
	struct search_list *list;
	unsigned int bytes;
	u8 i;
	int result;

	bytes = (sizeof(struct search_list) +
		 (cache->capacity * sizeof(struct cached_chapter_index *)));
	result = vdo_allocate_cache_aligned(bytes, "search list", &list);
	if (result != VDO_SUCCESS)
		return result;

	list->capacity = cache->capacity;
	list->first_dead_entry = 0;

	for (i = 0; i < list->capacity; i++)
		list->entries[i] = &cache->chapters[i];

	*list_ptr = list;
	return UDS_SUCCESS;
}

int uds_make_sparse_cache(const struct index_geometry *geometry, unsigned int capacity,
			  unsigned int zone_count, struct sparse_cache **cache_ptr)
{
	int result;
	unsigned int i;
	struct sparse_cache *cache;
	unsigned int bytes;

	bytes = (sizeof(struct sparse_cache) + (capacity * sizeof(struct cached_chapter_index)));
	result = vdo_allocate_cache_aligned(bytes, "sparse cache", &cache);
	if (result != VDO_SUCCESS)
		return result;

	cache->geometry = geometry;
	cache->capacity = capacity;
	cache->zone_count = zone_count;

	/*
	 * Scale down the skip threshold since the cache only counts cache misses in zone zero, but
	 * requests are being handled in all zones.
	 */
	cache->skip_threshold = (SKIP_SEARCH_THRESHOLD / zone_count);

	initialize_threads_barrier(&cache->begin_update_barrier, zone_count);
	initialize_threads_barrier(&cache->end_update_barrier, zone_count);

	for (i = 0; i < capacity; i++) {
		result = initialize_cached_chapter_index(&cache->chapters[i], geometry);
		if (result != UDS_SUCCESS)
			goto out;
	}

	for (i = 0; i < zone_count; i++) {
		result = make_search_list(cache, &cache->search_lists[i]);
		if (result != UDS_SUCCESS)
			goto out;
	}

	/* purge_search_list() needs some temporary lists for sorting. */
	result = vdo_allocate(capacity * 2, struct cached_chapter_index *,
			      "scratch entries", &cache->scratch_entries);
	if (result != VDO_SUCCESS)
		goto out;

	*cache_ptr = cache;
	return UDS_SUCCESS;
out:
	uds_free_sparse_cache(cache);
	return result;
}

static inline void set_skip_search(struct cached_chapter_index *chapter,
				   bool skip_search)
{
	/* Check before setting to reduce cache line contention. */
	if (READ_ONCE(chapter->skip_search) != skip_search)
		WRITE_ONCE(chapter->skip_search, skip_search);
}

static void score_search_hit(struct cached_chapter_index *chapter)
{
	chapter->counters.consecutive_misses = 0;
	set_skip_search(chapter, false);
}

static void score_search_miss(struct sparse_cache *cache,
			      struct cached_chapter_index *chapter)
{
	chapter->counters.consecutive_misses++;
	if (chapter->counters.consecutive_misses > cache->skip_threshold)
		set_skip_search(chapter, true);
}

static void release_cached_chapter_index(struct cached_chapter_index *chapter)
{
	unsigned int i;

	chapter->virtual_chapter = NO_CHAPTER;
	if (chapter->page_buffers == NULL)
		return;

	for (i = 0; i < chapter->index_pages_count; i++) {
		if (chapter->page_buffers[i] != NULL)
			dm_bufio_release(vdo_forget(chapter->page_buffers[i]));
	}
}

void uds_free_sparse_cache(struct sparse_cache *cache)
{
	unsigned int i;

	if (cache == NULL)
		return;

	vdo_free(cache->scratch_entries);

	for (i = 0; i < cache->zone_count; i++)
		vdo_free(cache->search_lists[i]);

	for (i = 0; i < cache->capacity; i++) {
		release_cached_chapter_index(&cache->chapters[i]);
		vdo_free(cache->chapters[i].index_pages);
		vdo_free(cache->chapters[i].page_buffers);
	}

	vdo_free(cache);
}

/*
 * Take the indicated element of the search list and move it to the start, pushing the pointers
 * previously before it back down the list.
 */
static inline void set_newest_entry(struct search_list *search_list, u8 index)
{
	struct cached_chapter_index *newest;

	if (index > 0) {
		newest = search_list->entries[index];
		memmove(&search_list->entries[1], &search_list->entries[0],
			index * sizeof(struct cached_chapter_index *));
		search_list->entries[0] = newest;
	}

	/*
	 * This function may have moved a dead chapter to the front of the list for reuse, in which
	 * case the set of dead chapters becomes smaller.
	 */
	if (search_list->first_dead_entry <= index)
		search_list->first_dead_entry++;
}

bool uds_sparse_cache_contains(struct sparse_cache *cache, u64 virtual_chapter,
			       unsigned int zone_number)
{
	struct search_list *search_list;
	struct cached_chapter_index *chapter;
	u8 i;

	/*
	 * The correctness of the barriers depends on the invariant that between calls to
	 * uds_update_sparse_cache(), the answers this function returns must never vary: the result
	 * for a given chapter must be identical across zones. That invariant must be maintained
	 * even if the chapter falls off the end of the volume, or if searching it is disabled
	 * because of too many search misses.
	 */
	search_list = cache->search_lists[zone_number];
	for (i = 0; i < search_list->first_dead_entry; i++) {
		chapter = search_list->entries[i];

		if (virtual_chapter == chapter->virtual_chapter) {
			if (zone_number == ZONE_ZERO)
				score_search_hit(chapter);

			set_newest_entry(search_list, i);
			return true;
		}
	}

	return false;
}

/*
 * Re-sort cache entries into three sets (active, skippable, and dead) while maintaining the LRU
 * ordering that already existed. This operation must only be called during the critical section in
 * uds_update_sparse_cache().
 */
static void purge_search_list(struct search_list *search_list,
			      struct sparse_cache *cache, u64 oldest_virtual_chapter)
{
	struct cached_chapter_index **entries;
	struct cached_chapter_index **skipped;
	struct cached_chapter_index **dead;
	struct cached_chapter_index *chapter;
	unsigned int next_alive = 0;
	unsigned int next_skipped = 0;
	unsigned int next_dead = 0;
	unsigned int i;

	entries = &search_list->entries[0];
	skipped = &cache->scratch_entries[0];
	dead = &cache->scratch_entries[search_list->capacity];

	for (i = 0; i < search_list->first_dead_entry; i++) {
		chapter = search_list->entries[i];
		if ((chapter->virtual_chapter < oldest_virtual_chapter) ||
		    (chapter->virtual_chapter == NO_CHAPTER))
			dead[next_dead++] = chapter;
		else if (chapter->skip_search)
			skipped[next_skipped++] = chapter;
		else
			entries[next_alive++] = chapter;
	}

	memcpy(&entries[next_alive], skipped,
	       next_skipped * sizeof(struct cached_chapter_index *));
	memcpy(&entries[next_alive + next_skipped], dead,
	       next_dead * sizeof(struct cached_chapter_index *));
	search_list->first_dead_entry = next_alive + next_skipped;
}

static int __must_check cache_chapter_index(struct cached_chapter_index *chapter,
					    u64 virtual_chapter,
					    const struct volume *volume)
{
	int result;

	release_cached_chapter_index(chapter);

	result = uds_read_chapter_index_from_volume(volume, virtual_chapter,
						    chapter->page_buffers,
						    chapter->index_pages);
	if (result != UDS_SUCCESS)
		return result;

	chapter->counters.consecutive_misses = 0;
	chapter->virtual_chapter = virtual_chapter;
	chapter->skip_search = false;

	return UDS_SUCCESS;
}

static inline void copy_search_list(const struct search_list *source,
				    struct search_list *target)
{
	*target = *source;
	memcpy(target->entries, source->entries,
	       source->capacity * sizeof(struct cached_chapter_index *));
}

/*
 * Update the sparse cache to contain a chapter index. This function must be called by all the zone
 * threads with the same chapter number to correctly enter the thread barriers used to synchronize
 * the cache updates.
 */
int uds_update_sparse_cache(struct index_zone *zone, u64 virtual_chapter)
{
	int result = UDS_SUCCESS;
	const struct uds_index *index = zone->index;
	struct sparse_cache *cache = index->volume->sparse_cache;

	if (uds_sparse_cache_contains(cache, virtual_chapter, zone->id))
		return UDS_SUCCESS;

	/*
	 * Wait for every zone thread to reach its corresponding barrier request and invoke this
	 * function before starting to modify the cache.
	 */
	enter_threads_barrier(&cache->begin_update_barrier);

	/*
	 * This is the start of the critical section: the zone zero thread is captain, effectively
	 * holding an exclusive lock on the sparse cache. All the other zone threads must do
	 * nothing between the two barriers. They will wait at the end_update_barrier again for the
	 * captain to finish the update.
	 */

	if (zone->id == ZONE_ZERO) {
		unsigned int z;
		struct search_list *list = cache->search_lists[ZONE_ZERO];

		purge_search_list(list, cache, zone->oldest_virtual_chapter);

		if (virtual_chapter >= index->oldest_virtual_chapter) {
			set_newest_entry(list, list->capacity - 1);
			result = cache_chapter_index(list->entries[0], virtual_chapter,
						     index->volume);
		}

		for (z = 1; z < cache->zone_count; z++)
			copy_search_list(list, cache->search_lists[z]);
	}

	/*
	 * This is the end of the critical section. All cache invariants must have been restored.
	 */
	enter_threads_barrier(&cache->end_update_barrier);
	return result;
}

void uds_invalidate_sparse_cache(struct sparse_cache *cache)
{
	unsigned int i;

	for (i = 0; i < cache->capacity; i++)
		release_cached_chapter_index(&cache->chapters[i]);
}

static inline bool should_skip_chapter(struct cached_chapter_index *chapter,
				       u64 oldest_chapter, u64 requested_chapter)
{
	if ((chapter->virtual_chapter == NO_CHAPTER) ||
	    (chapter->virtual_chapter < oldest_chapter))
		return true;

	if (requested_chapter != NO_CHAPTER)
		return requested_chapter != chapter->virtual_chapter;
	else
		return READ_ONCE(chapter->skip_search);
}

static int __must_check search_cached_chapter_index(struct cached_chapter_index *chapter,
						    const struct index_geometry *geometry,
						    const struct index_page_map *index_page_map,
						    const struct uds_record_name *name,
						    u16 *record_page_ptr)
{
	u32 physical_chapter =
		uds_map_to_physical_chapter(geometry, chapter->virtual_chapter);
	u32 index_page_number =
		uds_find_index_page_number(index_page_map, name, physical_chapter);
	struct delta_index_page *index_page =
		&chapter->index_pages[index_page_number];

	return uds_search_chapter_index_page(index_page, geometry, name,
					     record_page_ptr);
}

int uds_search_sparse_cache(struct index_zone *zone, const struct uds_record_name *name,
			    u64 *virtual_chapter_ptr, u16 *record_page_ptr)
{
	int result;
	struct volume *volume = zone->index->volume;
	struct sparse_cache *cache = volume->sparse_cache;
	struct cached_chapter_index *chapter;
	struct search_list *search_list;
	u8 i;
	/* Search the entire cache unless a specific chapter was requested. */
	bool search_one = (*virtual_chapter_ptr != NO_CHAPTER);

	*record_page_ptr = NO_CHAPTER_INDEX_ENTRY;
	search_list = cache->search_lists[zone->id];
	for (i = 0; i < search_list->first_dead_entry; i++) {
		chapter = search_list->entries[i];

		if (should_skip_chapter(chapter, zone->oldest_virtual_chapter,
					*virtual_chapter_ptr))
			continue;

		result = search_cached_chapter_index(chapter, cache->geometry,
						     volume->index_page_map, name,
						     record_page_ptr);
		if (result != UDS_SUCCESS)
			return result;

		if (*record_page_ptr != NO_CHAPTER_INDEX_ENTRY) {
			/*
			 * In theory, this might be a false match while a true match exists in
			 * another chapter, but that's a very rare case and not worth the extra
			 * search complexity.
			 */
			set_newest_entry(search_list, i);
			if (zone->id == ZONE_ZERO)
				score_search_hit(chapter);

			*virtual_chapter_ptr = chapter->virtual_chapter;
			return UDS_SUCCESS;
		}

		if (zone->id == ZONE_ZERO)
			score_search_miss(cache, chapter);

		if (search_one)
			break;
	}

	return UDS_SUCCESS;
}
