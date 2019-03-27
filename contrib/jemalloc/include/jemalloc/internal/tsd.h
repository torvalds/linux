#ifndef JEMALLOC_INTERNAL_TSD_H
#define JEMALLOC_INTERNAL_TSD_H

#include "jemalloc/internal/arena_types.h"
#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/jemalloc_internal_externs.h"
#include "jemalloc/internal/prof_types.h"
#include "jemalloc/internal/ql.h"
#include "jemalloc/internal/rtree_tsd.h"
#include "jemalloc/internal/tcache_types.h"
#include "jemalloc/internal/tcache_structs.h"
#include "jemalloc/internal/util.h"
#include "jemalloc/internal/witness.h"

/*
 * Thread-Specific-Data layout
 * --- data accessed on tcache fast path: state, rtree_ctx, stats, prof ---
 * s: state
 * e: tcache_enabled
 * m: thread_allocated (config_stats)
 * f: thread_deallocated (config_stats)
 * p: prof_tdata (config_prof)
 * c: rtree_ctx (rtree cache accessed on deallocation)
 * t: tcache
 * --- data not accessed on tcache fast path: arena-related fields ---
 * d: arenas_tdata_bypass
 * r: reentrancy_level
 * x: narenas_tdata
 * i: iarena
 * a: arena
 * o: arenas_tdata
 * Loading TSD data is on the critical path of basically all malloc operations.
 * In particular, tcache and rtree_ctx rely on hot CPU cache to be effective.
 * Use a compact layout to reduce cache footprint.
 * +--- 64-bit and 64B cacheline; 1B each letter; First byte on the left. ---+
 * |----------------------------  1st cacheline  ----------------------------|
 * | sedrxxxx mmmmmmmm ffffffff pppppppp [c * 32  ........ ........ .......] |
 * |----------------------------  2nd cacheline  ----------------------------|
 * | [c * 64  ........ ........ ........ ........ ........ ........ .......] |
 * |----------------------------  3nd cacheline  ----------------------------|
 * | [c * 32  ........ ........ .......] iiiiiiii aaaaaaaa oooooooo [t...... |
 * +-------------------------------------------------------------------------+
 * Note: the entire tcache is embedded into TSD and spans multiple cachelines.
 *
 * The last 3 members (i, a and o) before tcache isn't really needed on tcache
 * fast path.  However we have a number of unused tcache bins and witnesses
 * (never touched unless config_debug) at the end of tcache, so we place them
 * there to avoid breaking the cachelines and possibly paging in an extra page.
 */
#ifdef JEMALLOC_JET
typedef void (*test_callback_t)(int *);
#  define MALLOC_TSD_TEST_DATA_INIT 0x72b65c10
#  define MALLOC_TEST_TSD \
    O(test_data,		int,			int)		\
    O(test_callback,		test_callback_t,	int)
#  define MALLOC_TEST_TSD_INITIALIZER , MALLOC_TSD_TEST_DATA_INIT, NULL
#else
#  define MALLOC_TEST_TSD
#  define MALLOC_TEST_TSD_INITIALIZER
#endif

/*  O(name,			type,			nullable type */
#define MALLOC_TSD							\
    O(tcache_enabled,		bool,			bool)		\
    O(arenas_tdata_bypass,	bool,			bool)		\
    O(reentrancy_level,		int8_t,			int8_t)		\
    O(narenas_tdata,		uint32_t,		uint32_t)	\
    O(offset_state,		uint64_t,		uint64_t)	\
    O(thread_allocated,		uint64_t,		uint64_t)	\
    O(thread_deallocated,	uint64_t,		uint64_t)	\
    O(prof_tdata,		prof_tdata_t *,		prof_tdata_t *)	\
    O(rtree_ctx,		rtree_ctx_t,		rtree_ctx_t)	\
    O(iarena,			arena_t *,		arena_t *)	\
    O(arena,			arena_t *,		arena_t *)	\
    O(arenas_tdata,		arena_tdata_t *,	arena_tdata_t *)\
    O(tcache,			tcache_t,		tcache_t)	\
    O(witness_tsd,              witness_tsd_t,		witness_tsdn_t)	\
    MALLOC_TEST_TSD

#define TSD_INITIALIZER {						\
    tsd_state_uninitialized,						\
    TCACHE_ENABLED_ZERO_INITIALIZER,					\
    false,								\
    0,									\
    0,									\
    0,									\
    0,									\
    0,									\
    NULL,								\
    RTREE_CTX_ZERO_INITIALIZER,						\
    NULL,								\
    NULL,								\
    NULL,								\
    TCACHE_ZERO_INITIALIZER,						\
    WITNESS_TSD_INITIALIZER						\
    MALLOC_TEST_TSD_INITIALIZER						\
}

enum {
	tsd_state_nominal = 0, /* Common case --> jnz. */
	tsd_state_nominal_slow = 1, /* Initialized but on slow path. */
	/* the above 2 nominal states should be lower values. */
	tsd_state_nominal_max = 1, /* used for comparison only. */
	tsd_state_minimal_initialized = 2,
	tsd_state_purgatory = 3,
	tsd_state_reincarnated = 4,
	tsd_state_uninitialized = 5
};

/* Manually limit tsd_state_t to a single byte. */
typedef uint8_t tsd_state_t;

/* The actual tsd. */
struct tsd_s {
	/*
	 * The contents should be treated as totally opaque outside the tsd
	 * module.  Access any thread-local state through the getters and
	 * setters below.
	 */
	tsd_state_t	state;
#define O(n, t, nt)							\
	t use_a_getter_or_setter_instead_##n;
MALLOC_TSD
#undef O
/* AddressSanitizer requires TLS data to be aligned to at least 8 bytes. */
} JEMALLOC_ALIGNED(16);

/*
 * Wrapper around tsd_t that makes it possible to avoid implicit conversion
 * between tsd_t and tsdn_t, where tsdn_t is "nullable" and has to be
 * explicitly converted to tsd_t, which is non-nullable.
 */
struct tsdn_s {
	tsd_t tsd;
};
#define TSDN_NULL ((tsdn_t *)0)
JEMALLOC_ALWAYS_INLINE tsdn_t *
tsd_tsdn(tsd_t *tsd) {
	return (tsdn_t *)tsd;
}

JEMALLOC_ALWAYS_INLINE bool
tsdn_null(const tsdn_t *tsdn) {
	return tsdn == NULL;
}

JEMALLOC_ALWAYS_INLINE tsd_t *
tsdn_tsd(tsdn_t *tsdn) {
	assert(!tsdn_null(tsdn));

	return &tsdn->tsd;
}

void *malloc_tsd_malloc(size_t size);
void malloc_tsd_dalloc(void *wrapper);
void malloc_tsd_cleanup_register(bool (*f)(void));
tsd_t *malloc_tsd_boot0(void);
void malloc_tsd_boot1(void);
void tsd_cleanup(void *arg);
tsd_t *tsd_fetch_slow(tsd_t *tsd, bool internal);
void tsd_slow_update(tsd_t *tsd);

/*
 * We put the platform-specific data declarations and inlines into their own
 * header files to avoid cluttering this file.  They define tsd_boot0,
 * tsd_boot1, tsd_boot, tsd_booted_get, tsd_get_allocates, tsd_get, and tsd_set.
 */
#ifdef JEMALLOC_MALLOC_THREAD_CLEANUP
#include "jemalloc/internal/tsd_malloc_thread_cleanup.h"
#elif (defined(JEMALLOC_TLS))
#include "jemalloc/internal/tsd_tls.h"
#elif (defined(_WIN32))
#include "jemalloc/internal/tsd_win.h"
#else
#include "jemalloc/internal/tsd_generic.h"
#endif

/*
 * tsd_foop_get_unsafe(tsd) returns a pointer to the thread-local instance of
 * foo.  This omits some safety checks, and so can be used during tsd
 * initialization and cleanup.
 */
#define O(n, t, nt)							\
JEMALLOC_ALWAYS_INLINE t *						\
tsd_##n##p_get_unsafe(tsd_t *tsd) {					\
	return &tsd->use_a_getter_or_setter_instead_##n;		\
}
MALLOC_TSD
#undef O

/* tsd_foop_get(tsd) returns a pointer to the thread-local instance of foo. */
#define O(n, t, nt)							\
JEMALLOC_ALWAYS_INLINE t *						\
tsd_##n##p_get(tsd_t *tsd) {						\
	assert(tsd->state == tsd_state_nominal ||			\
	    tsd->state == tsd_state_nominal_slow ||			\
	    tsd->state == tsd_state_reincarnated ||			\
	    tsd->state == tsd_state_minimal_initialized);		\
	return tsd_##n##p_get_unsafe(tsd);				\
}
MALLOC_TSD
#undef O

/*
 * tsdn_foop_get(tsdn) returns either the thread-local instance of foo (if tsdn
 * isn't NULL), or NULL (if tsdn is NULL), cast to the nullable pointer type.
 */
#define O(n, t, nt)							\
JEMALLOC_ALWAYS_INLINE nt *						\
tsdn_##n##p_get(tsdn_t *tsdn) {						\
	if (tsdn_null(tsdn)) {						\
		return NULL;						\
	}								\
	tsd_t *tsd = tsdn_tsd(tsdn);					\
	return (nt *)tsd_##n##p_get(tsd);				\
}
MALLOC_TSD
#undef O

/* tsd_foo_get(tsd) returns the value of the thread-local instance of foo. */
#define O(n, t, nt)							\
JEMALLOC_ALWAYS_INLINE t						\
tsd_##n##_get(tsd_t *tsd) {						\
	return *tsd_##n##p_get(tsd);					\
}
MALLOC_TSD
#undef O

/* tsd_foo_set(tsd, val) updates the thread-local instance of foo to be val. */
#define O(n, t, nt)							\
JEMALLOC_ALWAYS_INLINE void						\
tsd_##n##_set(tsd_t *tsd, t val) {					\
	assert(tsd->state != tsd_state_reincarnated &&			\
	    tsd->state != tsd_state_minimal_initialized);		\
	*tsd_##n##p_get(tsd) = val;					\
}
MALLOC_TSD
#undef O

JEMALLOC_ALWAYS_INLINE void
tsd_assert_fast(tsd_t *tsd) {
	assert(!malloc_slow && tsd_tcache_enabled_get(tsd) &&
	    tsd_reentrancy_level_get(tsd) == 0);
}

JEMALLOC_ALWAYS_INLINE bool
tsd_fast(tsd_t *tsd) {
	bool fast = (tsd->state == tsd_state_nominal);
	if (fast) {
		tsd_assert_fast(tsd);
	}

	return fast;
}

JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_fetch_impl(bool init, bool minimal) {
	tsd_t *tsd = tsd_get(init);

	if (!init && tsd_get_allocates() && tsd == NULL) {
		return NULL;
	}
	assert(tsd != NULL);

	if (unlikely(tsd->state != tsd_state_nominal)) {
		return tsd_fetch_slow(tsd, minimal);
	}
	assert(tsd_fast(tsd));
	tsd_assert_fast(tsd);

	return tsd;
}

/* Get a minimal TSD that requires no cleanup.  See comments in free(). */
JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_fetch_min(void) {
	return tsd_fetch_impl(true, true);
}

/* For internal background threads use only. */
JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_internal_fetch(void) {
	tsd_t *tsd = tsd_fetch_min();
	/* Use reincarnated state to prevent full initialization. */
	tsd->state = tsd_state_reincarnated;

	return tsd;
}

JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_fetch(void) {
	return tsd_fetch_impl(true, false);
}

static inline bool
tsd_nominal(tsd_t *tsd) {
	return (tsd->state <= tsd_state_nominal_max);
}

JEMALLOC_ALWAYS_INLINE tsdn_t *
tsdn_fetch(void) {
	if (!tsd_booted_get()) {
		return NULL;
	}

	return tsd_tsdn(tsd_fetch_impl(false, false));
}

JEMALLOC_ALWAYS_INLINE rtree_ctx_t *
tsd_rtree_ctx(tsd_t *tsd) {
	return tsd_rtree_ctxp_get(tsd);
}

JEMALLOC_ALWAYS_INLINE rtree_ctx_t *
tsdn_rtree_ctx(tsdn_t *tsdn, rtree_ctx_t *fallback) {
	/*
	 * If tsd cannot be accessed, initialize the fallback rtree_ctx and
	 * return a pointer to it.
	 */
	if (unlikely(tsdn_null(tsdn))) {
		rtree_ctx_data_init(fallback);
		return fallback;
	}
	return tsd_rtree_ctx(tsdn_tsd(tsdn));
}

#endif /* JEMALLOC_INTERNAL_TSD_H */
