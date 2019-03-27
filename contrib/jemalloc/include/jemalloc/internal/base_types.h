#ifndef JEMALLOC_INTERNAL_BASE_TYPES_H
#define JEMALLOC_INTERNAL_BASE_TYPES_H

typedef struct base_block_s base_block_t;
typedef struct base_s base_t;

#define METADATA_THP_DEFAULT metadata_thp_disabled

/*
 * In auto mode, arenas switch to huge pages for the base allocator on the
 * second base block.  a0 switches to thp on the 5th block (after 20 megabytes
 * of metadata), since more metadata (e.g. rtree nodes) come from a0's base.
 */

#define BASE_AUTO_THP_THRESHOLD    2
#define BASE_AUTO_THP_THRESHOLD_A0 5

typedef enum {
	metadata_thp_disabled   = 0,
	/*
	 * Lazily enable hugepage for metadata. To avoid high RSS caused by THP
	 * + low usage arena (i.e. THP becomes a significant percentage), the
	 * "auto" option only starts using THP after a base allocator used up
	 * the first THP region.  Starting from the second hugepage (in a single
	 * arena), "auto" behaves the same as "always", i.e. madvise hugepage
	 * right away.
	 */
	metadata_thp_auto       = 1,
	metadata_thp_always     = 2,
	metadata_thp_mode_limit = 3
} metadata_thp_mode_t;

#endif /* JEMALLOC_INTERNAL_BASE_TYPES_H */
