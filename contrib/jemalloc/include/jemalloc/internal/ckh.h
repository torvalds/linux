#ifndef JEMALLOC_INTERNAL_CKH_H
#define JEMALLOC_INTERNAL_CKH_H

#include "jemalloc/internal/tsd.h"

/* Cuckoo hashing implementation.  Skip to the end for the interface. */

/******************************************************************************/
/* INTERNAL DEFINITIONS -- IGNORE */
/******************************************************************************/

/* Maintain counters used to get an idea of performance. */
/* #define CKH_COUNT */
/* Print counter values in ckh_delete() (requires CKH_COUNT). */
/* #define CKH_VERBOSE */

/*
 * There are 2^LG_CKH_BUCKET_CELLS cells in each hash table bucket.  Try to fit
 * one bucket per L1 cache line.
 */
#define LG_CKH_BUCKET_CELLS (LG_CACHELINE - LG_SIZEOF_PTR - 1)

/* Typedefs to allow easy function pointer passing. */
typedef void ckh_hash_t (const void *, size_t[2]);
typedef bool ckh_keycomp_t (const void *, const void *);

/* Hash table cell. */
typedef struct {
	const void *key;
	const void *data;
} ckhc_t;

/* The hash table itself. */
typedef struct {
#ifdef CKH_COUNT
	/* Counters used to get an idea of performance. */
	uint64_t ngrows;
	uint64_t nshrinks;
	uint64_t nshrinkfails;
	uint64_t ninserts;
	uint64_t nrelocs;
#endif

	/* Used for pseudo-random number generation. */
	uint64_t prng_state;

	/* Total number of items. */
	size_t count;

	/*
	 * Minimum and current number of hash table buckets.  There are
	 * 2^LG_CKH_BUCKET_CELLS cells per bucket.
	 */
	unsigned lg_minbuckets;
	unsigned lg_curbuckets;

	/* Hash and comparison functions. */
	ckh_hash_t *hash;
	ckh_keycomp_t *keycomp;

	/* Hash table with 2^lg_curbuckets buckets. */
	ckhc_t *tab;
} ckh_t;

/******************************************************************************/
/* BEGIN PUBLIC API */
/******************************************************************************/

/* Lifetime management.  Minitems is the initial capacity. */
bool ckh_new(tsd_t *tsd, ckh_t *ckh, size_t minitems, ckh_hash_t *hash,
    ckh_keycomp_t *keycomp);
void ckh_delete(tsd_t *tsd, ckh_t *ckh);

/* Get the number of elements in the set. */
size_t ckh_count(ckh_t *ckh);

/*
 * To iterate over the elements in the table, initialize *tabind to 0 and call
 * this function until it returns true.  Each call that returns false will
 * update *key and *data to the next element in the table, assuming the pointers
 * are non-NULL.
 */
bool ckh_iter(ckh_t *ckh, size_t *tabind, void **key, void **data);

/*
 * Basic hash table operations -- insert, removal, lookup.  For ckh_remove and
 * ckh_search, key or data can be NULL.  The hash-table only stores pointers to
 * the key and value, and doesn't do any lifetime management.
 */
bool ckh_insert(tsd_t *tsd, ckh_t *ckh, const void *key, const void *data);
bool ckh_remove(tsd_t *tsd, ckh_t *ckh, const void *searchkey, void **key,
    void **data);
bool ckh_search(ckh_t *ckh, const void *searchkey, void **key, void **data);

/* Some useful hash and comparison functions for strings and pointers. */
void ckh_string_hash(const void *key, size_t r_hash[2]);
bool ckh_string_keycomp(const void *k1, const void *k2);
void ckh_pointer_hash(const void *key, size_t r_hash[2]);
bool ckh_pointer_keycomp(const void *k1, const void *k2);

#endif /* JEMALLOC_INTERNAL_CKH_H */
