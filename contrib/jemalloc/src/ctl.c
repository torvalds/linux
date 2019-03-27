#define JEMALLOC_CTL_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/ctl.h"
#include "jemalloc/internal/extent_dss.h"
#include "jemalloc/internal/extent_mmap.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/nstime.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/util.h"

/******************************************************************************/
/* Data. */

/*
 * ctl_mtx protects the following:
 * - ctl_stats->*
 */
static malloc_mutex_t	ctl_mtx;
static bool		ctl_initialized;
static ctl_stats_t	*ctl_stats;
static ctl_arenas_t	*ctl_arenas;

/******************************************************************************/
/* Helpers for named and indexed nodes. */

static const ctl_named_node_t *
ctl_named_node(const ctl_node_t *node) {
	return ((node->named) ? (const ctl_named_node_t *)node : NULL);
}

static const ctl_named_node_t *
ctl_named_children(const ctl_named_node_t *node, size_t index) {
	const ctl_named_node_t *children = ctl_named_node(node->children);

	return (children ? &children[index] : NULL);
}

static const ctl_indexed_node_t *
ctl_indexed_node(const ctl_node_t *node) {
	return (!node->named ? (const ctl_indexed_node_t *)node : NULL);
}

/******************************************************************************/
/* Function prototypes for non-inline static functions. */

#define CTL_PROTO(n)							\
static int	n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,	\
    void *oldp, size_t *oldlenp, void *newp, size_t newlen);

#define INDEX_PROTO(n)							\
static const ctl_named_node_t	*n##_index(tsdn_t *tsdn,		\
    const size_t *mib, size_t miblen, size_t i);

CTL_PROTO(version)
CTL_PROTO(epoch)
CTL_PROTO(background_thread)
CTL_PROTO(max_background_threads)
CTL_PROTO(thread_tcache_enabled)
CTL_PROTO(thread_tcache_flush)
CTL_PROTO(thread_prof_name)
CTL_PROTO(thread_prof_active)
CTL_PROTO(thread_arena)
CTL_PROTO(thread_allocated)
CTL_PROTO(thread_allocatedp)
CTL_PROTO(thread_deallocated)
CTL_PROTO(thread_deallocatedp)
CTL_PROTO(config_cache_oblivious)
CTL_PROTO(config_debug)
CTL_PROTO(config_fill)
CTL_PROTO(config_lazy_lock)
CTL_PROTO(config_malloc_conf)
CTL_PROTO(config_prof)
CTL_PROTO(config_prof_libgcc)
CTL_PROTO(config_prof_libunwind)
CTL_PROTO(config_stats)
CTL_PROTO(config_utrace)
CTL_PROTO(config_xmalloc)
CTL_PROTO(opt_abort)
CTL_PROTO(opt_abort_conf)
CTL_PROTO(opt_metadata_thp)
CTL_PROTO(opt_retain)
CTL_PROTO(opt_dss)
CTL_PROTO(opt_narenas)
CTL_PROTO(opt_percpu_arena)
CTL_PROTO(opt_background_thread)
CTL_PROTO(opt_max_background_threads)
CTL_PROTO(opt_dirty_decay_ms)
CTL_PROTO(opt_muzzy_decay_ms)
CTL_PROTO(opt_stats_print)
CTL_PROTO(opt_stats_print_opts)
CTL_PROTO(opt_junk)
CTL_PROTO(opt_zero)
CTL_PROTO(opt_utrace)
CTL_PROTO(opt_xmalloc)
CTL_PROTO(opt_tcache)
CTL_PROTO(opt_thp)
CTL_PROTO(opt_lg_extent_max_active_fit)
CTL_PROTO(opt_lg_tcache_max)
CTL_PROTO(opt_prof)
CTL_PROTO(opt_prof_prefix)
CTL_PROTO(opt_prof_active)
CTL_PROTO(opt_prof_thread_active_init)
CTL_PROTO(opt_lg_prof_sample)
CTL_PROTO(opt_lg_prof_interval)
CTL_PROTO(opt_prof_gdump)
CTL_PROTO(opt_prof_final)
CTL_PROTO(opt_prof_leak)
CTL_PROTO(opt_prof_accum)
CTL_PROTO(tcache_create)
CTL_PROTO(tcache_flush)
CTL_PROTO(tcache_destroy)
CTL_PROTO(arena_i_initialized)
CTL_PROTO(arena_i_decay)
CTL_PROTO(arena_i_purge)
CTL_PROTO(arena_i_reset)
CTL_PROTO(arena_i_destroy)
CTL_PROTO(arena_i_dss)
CTL_PROTO(arena_i_dirty_decay_ms)
CTL_PROTO(arena_i_muzzy_decay_ms)
CTL_PROTO(arena_i_extent_hooks)
CTL_PROTO(arena_i_retain_grow_limit)
INDEX_PROTO(arena_i)
CTL_PROTO(arenas_bin_i_size)
CTL_PROTO(arenas_bin_i_nregs)
CTL_PROTO(arenas_bin_i_slab_size)
INDEX_PROTO(arenas_bin_i)
CTL_PROTO(arenas_lextent_i_size)
INDEX_PROTO(arenas_lextent_i)
CTL_PROTO(arenas_narenas)
CTL_PROTO(arenas_dirty_decay_ms)
CTL_PROTO(arenas_muzzy_decay_ms)
CTL_PROTO(arenas_quantum)
CTL_PROTO(arenas_page)
CTL_PROTO(arenas_tcache_max)
CTL_PROTO(arenas_nbins)
CTL_PROTO(arenas_nhbins)
CTL_PROTO(arenas_nlextents)
CTL_PROTO(arenas_create)
CTL_PROTO(arenas_lookup)
CTL_PROTO(prof_thread_active_init)
CTL_PROTO(prof_active)
CTL_PROTO(prof_dump)
CTL_PROTO(prof_gdump)
CTL_PROTO(prof_reset)
CTL_PROTO(prof_interval)
CTL_PROTO(lg_prof_sample)
CTL_PROTO(stats_arenas_i_small_allocated)
CTL_PROTO(stats_arenas_i_small_nmalloc)
CTL_PROTO(stats_arenas_i_small_ndalloc)
CTL_PROTO(stats_arenas_i_small_nrequests)
CTL_PROTO(stats_arenas_i_large_allocated)
CTL_PROTO(stats_arenas_i_large_nmalloc)
CTL_PROTO(stats_arenas_i_large_ndalloc)
CTL_PROTO(stats_arenas_i_large_nrequests)
CTL_PROTO(stats_arenas_i_bins_j_nmalloc)
CTL_PROTO(stats_arenas_i_bins_j_ndalloc)
CTL_PROTO(stats_arenas_i_bins_j_nrequests)
CTL_PROTO(stats_arenas_i_bins_j_curregs)
CTL_PROTO(stats_arenas_i_bins_j_nfills)
CTL_PROTO(stats_arenas_i_bins_j_nflushes)
CTL_PROTO(stats_arenas_i_bins_j_nslabs)
CTL_PROTO(stats_arenas_i_bins_j_nreslabs)
CTL_PROTO(stats_arenas_i_bins_j_curslabs)
INDEX_PROTO(stats_arenas_i_bins_j)
CTL_PROTO(stats_arenas_i_lextents_j_nmalloc)
CTL_PROTO(stats_arenas_i_lextents_j_ndalloc)
CTL_PROTO(stats_arenas_i_lextents_j_nrequests)
CTL_PROTO(stats_arenas_i_lextents_j_curlextents)
INDEX_PROTO(stats_arenas_i_lextents_j)
CTL_PROTO(stats_arenas_i_nthreads)
CTL_PROTO(stats_arenas_i_uptime)
CTL_PROTO(stats_arenas_i_dss)
CTL_PROTO(stats_arenas_i_dirty_decay_ms)
CTL_PROTO(stats_arenas_i_muzzy_decay_ms)
CTL_PROTO(stats_arenas_i_pactive)
CTL_PROTO(stats_arenas_i_pdirty)
CTL_PROTO(stats_arenas_i_pmuzzy)
CTL_PROTO(stats_arenas_i_mapped)
CTL_PROTO(stats_arenas_i_retained)
CTL_PROTO(stats_arenas_i_dirty_npurge)
CTL_PROTO(stats_arenas_i_dirty_nmadvise)
CTL_PROTO(stats_arenas_i_dirty_purged)
CTL_PROTO(stats_arenas_i_muzzy_npurge)
CTL_PROTO(stats_arenas_i_muzzy_nmadvise)
CTL_PROTO(stats_arenas_i_muzzy_purged)
CTL_PROTO(stats_arenas_i_base)
CTL_PROTO(stats_arenas_i_internal)
CTL_PROTO(stats_arenas_i_metadata_thp)
CTL_PROTO(stats_arenas_i_tcache_bytes)
CTL_PROTO(stats_arenas_i_resident)
INDEX_PROTO(stats_arenas_i)
CTL_PROTO(stats_allocated)
CTL_PROTO(stats_active)
CTL_PROTO(stats_background_thread_num_threads)
CTL_PROTO(stats_background_thread_num_runs)
CTL_PROTO(stats_background_thread_run_interval)
CTL_PROTO(stats_metadata)
CTL_PROTO(stats_metadata_thp)
CTL_PROTO(stats_resident)
CTL_PROTO(stats_mapped)
CTL_PROTO(stats_retained)

#define MUTEX_STATS_CTL_PROTO_GEN(n)					\
CTL_PROTO(stats_##n##_num_ops)						\
CTL_PROTO(stats_##n##_num_wait)						\
CTL_PROTO(stats_##n##_num_spin_acq)					\
CTL_PROTO(stats_##n##_num_owner_switch)					\
CTL_PROTO(stats_##n##_total_wait_time)					\
CTL_PROTO(stats_##n##_max_wait_time)					\
CTL_PROTO(stats_##n##_max_num_thds)

/* Global mutexes. */
#define OP(mtx) MUTEX_STATS_CTL_PROTO_GEN(mutexes_##mtx)
MUTEX_PROF_GLOBAL_MUTEXES
#undef OP

/* Per arena mutexes. */
#define OP(mtx) MUTEX_STATS_CTL_PROTO_GEN(arenas_i_mutexes_##mtx)
MUTEX_PROF_ARENA_MUTEXES
#undef OP

/* Arena bin mutexes. */
MUTEX_STATS_CTL_PROTO_GEN(arenas_i_bins_j_mutex)
#undef MUTEX_STATS_CTL_PROTO_GEN

CTL_PROTO(stats_mutexes_reset)

/******************************************************************************/
/* mallctl tree. */

#define NAME(n)	{true},	n
#define CHILD(t, c)							\
	sizeof(c##_node) / sizeof(ctl_##t##_node_t),			\
	(ctl_node_t *)c##_node,						\
	NULL
#define CTL(c)	0, NULL, c##_ctl

/*
 * Only handles internal indexed nodes, since there are currently no external
 * ones.
 */
#define INDEX(i)	{false},	i##_index

static const ctl_named_node_t	thread_tcache_node[] = {
	{NAME("enabled"),	CTL(thread_tcache_enabled)},
	{NAME("flush"),		CTL(thread_tcache_flush)}
};

static const ctl_named_node_t	thread_prof_node[] = {
	{NAME("name"),		CTL(thread_prof_name)},
	{NAME("active"),	CTL(thread_prof_active)}
};

static const ctl_named_node_t	thread_node[] = {
	{NAME("arena"),		CTL(thread_arena)},
	{NAME("allocated"),	CTL(thread_allocated)},
	{NAME("allocatedp"),	CTL(thread_allocatedp)},
	{NAME("deallocated"),	CTL(thread_deallocated)},
	{NAME("deallocatedp"),	CTL(thread_deallocatedp)},
	{NAME("tcache"),	CHILD(named, thread_tcache)},
	{NAME("prof"),		CHILD(named, thread_prof)}
};

static const ctl_named_node_t	config_node[] = {
	{NAME("cache_oblivious"), CTL(config_cache_oblivious)},
	{NAME("debug"),		CTL(config_debug)},
	{NAME("fill"),		CTL(config_fill)},
	{NAME("lazy_lock"),	CTL(config_lazy_lock)},
	{NAME("malloc_conf"),	CTL(config_malloc_conf)},
	{NAME("prof"),		CTL(config_prof)},
	{NAME("prof_libgcc"),	CTL(config_prof_libgcc)},
	{NAME("prof_libunwind"), CTL(config_prof_libunwind)},
	{NAME("stats"),		CTL(config_stats)},
	{NAME("utrace"),	CTL(config_utrace)},
	{NAME("xmalloc"),	CTL(config_xmalloc)}
};

static const ctl_named_node_t opt_node[] = {
	{NAME("abort"),		CTL(opt_abort)},
	{NAME("abort_conf"),	CTL(opt_abort_conf)},
	{NAME("metadata_thp"),	CTL(opt_metadata_thp)},
	{NAME("retain"),	CTL(opt_retain)},
	{NAME("dss"),		CTL(opt_dss)},
	{NAME("narenas"),	CTL(opt_narenas)},
	{NAME("percpu_arena"),	CTL(opt_percpu_arena)},
	{NAME("background_thread"),	CTL(opt_background_thread)},
	{NAME("max_background_threads"),	CTL(opt_max_background_threads)},
	{NAME("dirty_decay_ms"), CTL(opt_dirty_decay_ms)},
	{NAME("muzzy_decay_ms"), CTL(opt_muzzy_decay_ms)},
	{NAME("stats_print"),	CTL(opt_stats_print)},
	{NAME("stats_print_opts"),	CTL(opt_stats_print_opts)},
	{NAME("junk"),		CTL(opt_junk)},
	{NAME("zero"),		CTL(opt_zero)},
	{NAME("utrace"),	CTL(opt_utrace)},
	{NAME("xmalloc"),	CTL(opt_xmalloc)},
	{NAME("tcache"),	CTL(opt_tcache)},
	{NAME("thp"),		CTL(opt_thp)},
	{NAME("lg_extent_max_active_fit"), CTL(opt_lg_extent_max_active_fit)},
	{NAME("lg_tcache_max"),	CTL(opt_lg_tcache_max)},
	{NAME("prof"),		CTL(opt_prof)},
	{NAME("prof_prefix"),	CTL(opt_prof_prefix)},
	{NAME("prof_active"),	CTL(opt_prof_active)},
	{NAME("prof_thread_active_init"), CTL(opt_prof_thread_active_init)},
	{NAME("lg_prof_sample"), CTL(opt_lg_prof_sample)},
	{NAME("lg_prof_interval"), CTL(opt_lg_prof_interval)},
	{NAME("prof_gdump"),	CTL(opt_prof_gdump)},
	{NAME("prof_final"),	CTL(opt_prof_final)},
	{NAME("prof_leak"),	CTL(opt_prof_leak)},
	{NAME("prof_accum"),	CTL(opt_prof_accum)}
};

static const ctl_named_node_t	tcache_node[] = {
	{NAME("create"),	CTL(tcache_create)},
	{NAME("flush"),		CTL(tcache_flush)},
	{NAME("destroy"),	CTL(tcache_destroy)}
};

static const ctl_named_node_t arena_i_node[] = {
	{NAME("initialized"),	CTL(arena_i_initialized)},
	{NAME("decay"),		CTL(arena_i_decay)},
	{NAME("purge"),		CTL(arena_i_purge)},
	{NAME("reset"),		CTL(arena_i_reset)},
	{NAME("destroy"),	CTL(arena_i_destroy)},
	{NAME("dss"),		CTL(arena_i_dss)},
	{NAME("dirty_decay_ms"), CTL(arena_i_dirty_decay_ms)},
	{NAME("muzzy_decay_ms"), CTL(arena_i_muzzy_decay_ms)},
	{NAME("extent_hooks"),	CTL(arena_i_extent_hooks)},
	{NAME("retain_grow_limit"),	CTL(arena_i_retain_grow_limit)}
};
static const ctl_named_node_t super_arena_i_node[] = {
	{NAME(""),		CHILD(named, arena_i)}
};

static const ctl_indexed_node_t arena_node[] = {
	{INDEX(arena_i)}
};

static const ctl_named_node_t arenas_bin_i_node[] = {
	{NAME("size"),		CTL(arenas_bin_i_size)},
	{NAME("nregs"),		CTL(arenas_bin_i_nregs)},
	{NAME("slab_size"),	CTL(arenas_bin_i_slab_size)}
};
static const ctl_named_node_t super_arenas_bin_i_node[] = {
	{NAME(""),		CHILD(named, arenas_bin_i)}
};

static const ctl_indexed_node_t arenas_bin_node[] = {
	{INDEX(arenas_bin_i)}
};

static const ctl_named_node_t arenas_lextent_i_node[] = {
	{NAME("size"),		CTL(arenas_lextent_i_size)}
};
static const ctl_named_node_t super_arenas_lextent_i_node[] = {
	{NAME(""),		CHILD(named, arenas_lextent_i)}
};

static const ctl_indexed_node_t arenas_lextent_node[] = {
	{INDEX(arenas_lextent_i)}
};

static const ctl_named_node_t arenas_node[] = {
	{NAME("narenas"),	CTL(arenas_narenas)},
	{NAME("dirty_decay_ms"), CTL(arenas_dirty_decay_ms)},
	{NAME("muzzy_decay_ms"), CTL(arenas_muzzy_decay_ms)},
	{NAME("quantum"),	CTL(arenas_quantum)},
	{NAME("page"),		CTL(arenas_page)},
	{NAME("tcache_max"),	CTL(arenas_tcache_max)},
	{NAME("nbins"),		CTL(arenas_nbins)},
	{NAME("nhbins"),	CTL(arenas_nhbins)},
	{NAME("bin"),		CHILD(indexed, arenas_bin)},
	{NAME("nlextents"),	CTL(arenas_nlextents)},
	{NAME("lextent"),	CHILD(indexed, arenas_lextent)},
	{NAME("create"),	CTL(arenas_create)},
	{NAME("lookup"),	CTL(arenas_lookup)}
};

static const ctl_named_node_t	prof_node[] = {
	{NAME("thread_active_init"), CTL(prof_thread_active_init)},
	{NAME("active"),	CTL(prof_active)},
	{NAME("dump"),		CTL(prof_dump)},
	{NAME("gdump"),		CTL(prof_gdump)},
	{NAME("reset"),		CTL(prof_reset)},
	{NAME("interval"),	CTL(prof_interval)},
	{NAME("lg_sample"),	CTL(lg_prof_sample)}
};

static const ctl_named_node_t stats_arenas_i_small_node[] = {
	{NAME("allocated"),	CTL(stats_arenas_i_small_allocated)},
	{NAME("nmalloc"),	CTL(stats_arenas_i_small_nmalloc)},
	{NAME("ndalloc"),	CTL(stats_arenas_i_small_ndalloc)},
	{NAME("nrequests"),	CTL(stats_arenas_i_small_nrequests)}
};

static const ctl_named_node_t stats_arenas_i_large_node[] = {
	{NAME("allocated"),	CTL(stats_arenas_i_large_allocated)},
	{NAME("nmalloc"),	CTL(stats_arenas_i_large_nmalloc)},
	{NAME("ndalloc"),	CTL(stats_arenas_i_large_ndalloc)},
	{NAME("nrequests"),	CTL(stats_arenas_i_large_nrequests)}
};

#define MUTEX_PROF_DATA_NODE(prefix)					\
static const ctl_named_node_t stats_##prefix##_node[] = {		\
	{NAME("num_ops"),						\
	 CTL(stats_##prefix##_num_ops)},				\
	{NAME("num_wait"),						\
	 CTL(stats_##prefix##_num_wait)},				\
	{NAME("num_spin_acq"),						\
	 CTL(stats_##prefix##_num_spin_acq)},				\
	{NAME("num_owner_switch"),					\
	 CTL(stats_##prefix##_num_owner_switch)},			\
	{NAME("total_wait_time"),					\
	 CTL(stats_##prefix##_total_wait_time)},			\
	{NAME("max_wait_time"),						\
	 CTL(stats_##prefix##_max_wait_time)},				\
	{NAME("max_num_thds"),						\
	 CTL(stats_##prefix##_max_num_thds)}				\
	/* Note that # of current waiting thread not provided. */	\
};

MUTEX_PROF_DATA_NODE(arenas_i_bins_j_mutex)

static const ctl_named_node_t stats_arenas_i_bins_j_node[] = {
	{NAME("nmalloc"),	CTL(stats_arenas_i_bins_j_nmalloc)},
	{NAME("ndalloc"),	CTL(stats_arenas_i_bins_j_ndalloc)},
	{NAME("nrequests"),	CTL(stats_arenas_i_bins_j_nrequests)},
	{NAME("curregs"),	CTL(stats_arenas_i_bins_j_curregs)},
	{NAME("nfills"),	CTL(stats_arenas_i_bins_j_nfills)},
	{NAME("nflushes"),	CTL(stats_arenas_i_bins_j_nflushes)},
	{NAME("nslabs"),	CTL(stats_arenas_i_bins_j_nslabs)},
	{NAME("nreslabs"),	CTL(stats_arenas_i_bins_j_nreslabs)},
	{NAME("curslabs"),	CTL(stats_arenas_i_bins_j_curslabs)},
	{NAME("mutex"),		CHILD(named, stats_arenas_i_bins_j_mutex)}
};

static const ctl_named_node_t super_stats_arenas_i_bins_j_node[] = {
	{NAME(""),		CHILD(named, stats_arenas_i_bins_j)}
};

static const ctl_indexed_node_t stats_arenas_i_bins_node[] = {
	{INDEX(stats_arenas_i_bins_j)}
};

static const ctl_named_node_t stats_arenas_i_lextents_j_node[] = {
	{NAME("nmalloc"),	CTL(stats_arenas_i_lextents_j_nmalloc)},
	{NAME("ndalloc"),	CTL(stats_arenas_i_lextents_j_ndalloc)},
	{NAME("nrequests"),	CTL(stats_arenas_i_lextents_j_nrequests)},
	{NAME("curlextents"),	CTL(stats_arenas_i_lextents_j_curlextents)}
};
static const ctl_named_node_t super_stats_arenas_i_lextents_j_node[] = {
	{NAME(""),		CHILD(named, stats_arenas_i_lextents_j)}
};

static const ctl_indexed_node_t stats_arenas_i_lextents_node[] = {
	{INDEX(stats_arenas_i_lextents_j)}
};

#define OP(mtx)  MUTEX_PROF_DATA_NODE(arenas_i_mutexes_##mtx)
MUTEX_PROF_ARENA_MUTEXES
#undef OP

static const ctl_named_node_t stats_arenas_i_mutexes_node[] = {
#define OP(mtx) {NAME(#mtx), CHILD(named, stats_arenas_i_mutexes_##mtx)},
MUTEX_PROF_ARENA_MUTEXES
#undef OP
};

static const ctl_named_node_t stats_arenas_i_node[] = {
	{NAME("nthreads"),	CTL(stats_arenas_i_nthreads)},
	{NAME("uptime"),	CTL(stats_arenas_i_uptime)},
	{NAME("dss"),		CTL(stats_arenas_i_dss)},
	{NAME("dirty_decay_ms"), CTL(stats_arenas_i_dirty_decay_ms)},
	{NAME("muzzy_decay_ms"), CTL(stats_arenas_i_muzzy_decay_ms)},
	{NAME("pactive"),	CTL(stats_arenas_i_pactive)},
	{NAME("pdirty"),	CTL(stats_arenas_i_pdirty)},
	{NAME("pmuzzy"),	CTL(stats_arenas_i_pmuzzy)},
	{NAME("mapped"),	CTL(stats_arenas_i_mapped)},
	{NAME("retained"),	CTL(stats_arenas_i_retained)},
	{NAME("dirty_npurge"),	CTL(stats_arenas_i_dirty_npurge)},
	{NAME("dirty_nmadvise"), CTL(stats_arenas_i_dirty_nmadvise)},
	{NAME("dirty_purged"),	CTL(stats_arenas_i_dirty_purged)},
	{NAME("muzzy_npurge"),	CTL(stats_arenas_i_muzzy_npurge)},
	{NAME("muzzy_nmadvise"), CTL(stats_arenas_i_muzzy_nmadvise)},
	{NAME("muzzy_purged"),	CTL(stats_arenas_i_muzzy_purged)},
	{NAME("base"),		CTL(stats_arenas_i_base)},
	{NAME("internal"),	CTL(stats_arenas_i_internal)},
	{NAME("metadata_thp"),	CTL(stats_arenas_i_metadata_thp)},
	{NAME("tcache_bytes"),	CTL(stats_arenas_i_tcache_bytes)},
	{NAME("resident"),	CTL(stats_arenas_i_resident)},
	{NAME("small"),		CHILD(named, stats_arenas_i_small)},
	{NAME("large"),		CHILD(named, stats_arenas_i_large)},
	{NAME("bins"),		CHILD(indexed, stats_arenas_i_bins)},
	{NAME("lextents"),	CHILD(indexed, stats_arenas_i_lextents)},
	{NAME("mutexes"),	CHILD(named, stats_arenas_i_mutexes)}
};
static const ctl_named_node_t super_stats_arenas_i_node[] = {
	{NAME(""),		CHILD(named, stats_arenas_i)}
};

static const ctl_indexed_node_t stats_arenas_node[] = {
	{INDEX(stats_arenas_i)}
};

static const ctl_named_node_t stats_background_thread_node[] = {
	{NAME("num_threads"),	CTL(stats_background_thread_num_threads)},
	{NAME("num_runs"),	CTL(stats_background_thread_num_runs)},
	{NAME("run_interval"),	CTL(stats_background_thread_run_interval)}
};

#define OP(mtx) MUTEX_PROF_DATA_NODE(mutexes_##mtx)
MUTEX_PROF_GLOBAL_MUTEXES
#undef OP

static const ctl_named_node_t stats_mutexes_node[] = {
#define OP(mtx) {NAME(#mtx), CHILD(named, stats_mutexes_##mtx)},
MUTEX_PROF_GLOBAL_MUTEXES
#undef OP
	{NAME("reset"),		CTL(stats_mutexes_reset)}
};
#undef MUTEX_PROF_DATA_NODE

static const ctl_named_node_t stats_node[] = {
	{NAME("allocated"),	CTL(stats_allocated)},
	{NAME("active"),	CTL(stats_active)},
	{NAME("metadata"),	CTL(stats_metadata)},
	{NAME("metadata_thp"),	CTL(stats_metadata_thp)},
	{NAME("resident"),	CTL(stats_resident)},
	{NAME("mapped"),	CTL(stats_mapped)},
	{NAME("retained"),	CTL(stats_retained)},
	{NAME("background_thread"),
	 CHILD(named, stats_background_thread)},
	{NAME("mutexes"),	CHILD(named, stats_mutexes)},
	{NAME("arenas"),	CHILD(indexed, stats_arenas)}
};

static const ctl_named_node_t	root_node[] = {
	{NAME("version"),	CTL(version)},
	{NAME("epoch"),		CTL(epoch)},
	{NAME("background_thread"),	CTL(background_thread)},
	{NAME("max_background_threads"),	CTL(max_background_threads)},
	{NAME("thread"),	CHILD(named, thread)},
	{NAME("config"),	CHILD(named, config)},
	{NAME("opt"),		CHILD(named, opt)},
	{NAME("tcache"),	CHILD(named, tcache)},
	{NAME("arena"),		CHILD(indexed, arena)},
	{NAME("arenas"),	CHILD(named, arenas)},
	{NAME("prof"),		CHILD(named, prof)},
	{NAME("stats"),		CHILD(named, stats)}
};
static const ctl_named_node_t super_root_node[] = {
	{NAME(""),		CHILD(named, root)}
};

#undef NAME
#undef CHILD
#undef CTL
#undef INDEX

/******************************************************************************/

/*
 * Sets *dst + *src non-atomically.  This is safe, since everything is
 * synchronized by the ctl mutex.
 */
static void
ctl_accum_arena_stats_u64(arena_stats_u64_t *dst, arena_stats_u64_t *src) {
#ifdef JEMALLOC_ATOMIC_U64
	uint64_t cur_dst = atomic_load_u64(dst, ATOMIC_RELAXED);
	uint64_t cur_src = atomic_load_u64(src, ATOMIC_RELAXED);
	atomic_store_u64(dst, cur_dst + cur_src, ATOMIC_RELAXED);
#else
	*dst += *src;
#endif
}

/* Likewise: with ctl mutex synchronization, reading is simple. */
static uint64_t
ctl_arena_stats_read_u64(arena_stats_u64_t *p) {
#ifdef JEMALLOC_ATOMIC_U64
	return atomic_load_u64(p, ATOMIC_RELAXED);
#else
	return *p;
#endif
}

static void
accum_atomic_zu(atomic_zu_t *dst, atomic_zu_t *src) {
	size_t cur_dst = atomic_load_zu(dst, ATOMIC_RELAXED);
	size_t cur_src = atomic_load_zu(src, ATOMIC_RELAXED);
	atomic_store_zu(dst, cur_dst + cur_src, ATOMIC_RELAXED);
}

/******************************************************************************/

static unsigned
arenas_i2a_impl(size_t i, bool compat, bool validate) {
	unsigned a;

	switch (i) {
	case MALLCTL_ARENAS_ALL:
		a = 0;
		break;
	case MALLCTL_ARENAS_DESTROYED:
		a = 1;
		break;
	default:
		if (compat && i == ctl_arenas->narenas) {
			/*
			 * Provide deprecated backward compatibility for
			 * accessing the merged stats at index narenas rather
			 * than via MALLCTL_ARENAS_ALL.  This is scheduled for
			 * removal in 6.0.0.
			 */
			a = 0;
		} else if (validate && i >= ctl_arenas->narenas) {
			a = UINT_MAX;
		} else {
			/*
			 * This function should never be called for an index
			 * more than one past the range of indices that have
			 * initialized ctl data.
			 */
			assert(i < ctl_arenas->narenas || (!validate && i ==
			    ctl_arenas->narenas));
			a = (unsigned)i + 2;
		}
		break;
	}

	return a;
}

static unsigned
arenas_i2a(size_t i) {
	return arenas_i2a_impl(i, true, false);
}

static ctl_arena_t *
arenas_i_impl(tsd_t *tsd, size_t i, bool compat, bool init) {
	ctl_arena_t *ret;

	assert(!compat || !init);

	ret = ctl_arenas->arenas[arenas_i2a_impl(i, compat, false)];
	if (init && ret == NULL) {
		if (config_stats) {
			struct container_s {
				ctl_arena_t		ctl_arena;
				ctl_arena_stats_t	astats;
			};
			struct container_s *cont =
			    (struct container_s *)base_alloc(tsd_tsdn(tsd),
			    b0get(), sizeof(struct container_s), QUANTUM);
			if (cont == NULL) {
				return NULL;
			}
			ret = &cont->ctl_arena;
			ret->astats = &cont->astats;
		} else {
			ret = (ctl_arena_t *)base_alloc(tsd_tsdn(tsd), b0get(),
			    sizeof(ctl_arena_t), QUANTUM);
			if (ret == NULL) {
				return NULL;
			}
		}
		ret->arena_ind = (unsigned)i;
		ctl_arenas->arenas[arenas_i2a_impl(i, compat, false)] = ret;
	}

	assert(ret == NULL || arenas_i2a(ret->arena_ind) == arenas_i2a(i));
	return ret;
}

static ctl_arena_t *
arenas_i(size_t i) {
	ctl_arena_t *ret = arenas_i_impl(tsd_fetch(), i, true, false);
	assert(ret != NULL);
	return ret;
}

static void
ctl_arena_clear(ctl_arena_t *ctl_arena) {
	ctl_arena->nthreads = 0;
	ctl_arena->dss = dss_prec_names[dss_prec_limit];
	ctl_arena->dirty_decay_ms = -1;
	ctl_arena->muzzy_decay_ms = -1;
	ctl_arena->pactive = 0;
	ctl_arena->pdirty = 0;
	ctl_arena->pmuzzy = 0;
	if (config_stats) {
		memset(&ctl_arena->astats->astats, 0, sizeof(arena_stats_t));
		ctl_arena->astats->allocated_small = 0;
		ctl_arena->astats->nmalloc_small = 0;
		ctl_arena->astats->ndalloc_small = 0;
		ctl_arena->astats->nrequests_small = 0;
		memset(ctl_arena->astats->bstats, 0, NBINS *
		    sizeof(bin_stats_t));
		memset(ctl_arena->astats->lstats, 0, (NSIZES - NBINS) *
		    sizeof(arena_stats_large_t));
	}
}

static void
ctl_arena_stats_amerge(tsdn_t *tsdn, ctl_arena_t *ctl_arena, arena_t *arena) {
	unsigned i;

	if (config_stats) {
		arena_stats_merge(tsdn, arena, &ctl_arena->nthreads,
		    &ctl_arena->dss, &ctl_arena->dirty_decay_ms,
		    &ctl_arena->muzzy_decay_ms, &ctl_arena->pactive,
		    &ctl_arena->pdirty, &ctl_arena->pmuzzy,
		    &ctl_arena->astats->astats, ctl_arena->astats->bstats,
		    ctl_arena->astats->lstats);

		for (i = 0; i < NBINS; i++) {
			ctl_arena->astats->allocated_small +=
			    ctl_arena->astats->bstats[i].curregs *
			    sz_index2size(i);
			ctl_arena->astats->nmalloc_small +=
			    ctl_arena->astats->bstats[i].nmalloc;
			ctl_arena->astats->ndalloc_small +=
			    ctl_arena->astats->bstats[i].ndalloc;
			ctl_arena->astats->nrequests_small +=
			    ctl_arena->astats->bstats[i].nrequests;
		}
	} else {
		arena_basic_stats_merge(tsdn, arena, &ctl_arena->nthreads,
		    &ctl_arena->dss, &ctl_arena->dirty_decay_ms,
		    &ctl_arena->muzzy_decay_ms, &ctl_arena->pactive,
		    &ctl_arena->pdirty, &ctl_arena->pmuzzy);
	}
}

static void
ctl_arena_stats_sdmerge(ctl_arena_t *ctl_sdarena, ctl_arena_t *ctl_arena,
    bool destroyed) {
	unsigned i;

	if (!destroyed) {
		ctl_sdarena->nthreads += ctl_arena->nthreads;
		ctl_sdarena->pactive += ctl_arena->pactive;
		ctl_sdarena->pdirty += ctl_arena->pdirty;
		ctl_sdarena->pmuzzy += ctl_arena->pmuzzy;
	} else {
		assert(ctl_arena->nthreads == 0);
		assert(ctl_arena->pactive == 0);
		assert(ctl_arena->pdirty == 0);
		assert(ctl_arena->pmuzzy == 0);
	}

	if (config_stats) {
		ctl_arena_stats_t *sdstats = ctl_sdarena->astats;
		ctl_arena_stats_t *astats = ctl_arena->astats;

		if (!destroyed) {
			accum_atomic_zu(&sdstats->astats.mapped,
			    &astats->astats.mapped);
			accum_atomic_zu(&sdstats->astats.retained,
			    &astats->astats.retained);
		}

		ctl_accum_arena_stats_u64(&sdstats->astats.decay_dirty.npurge,
		    &astats->astats.decay_dirty.npurge);
		ctl_accum_arena_stats_u64(&sdstats->astats.decay_dirty.nmadvise,
		    &astats->astats.decay_dirty.nmadvise);
		ctl_accum_arena_stats_u64(&sdstats->astats.decay_dirty.purged,
		    &astats->astats.decay_dirty.purged);

		ctl_accum_arena_stats_u64(&sdstats->astats.decay_muzzy.npurge,
		    &astats->astats.decay_muzzy.npurge);
		ctl_accum_arena_stats_u64(&sdstats->astats.decay_muzzy.nmadvise,
		    &astats->astats.decay_muzzy.nmadvise);
		ctl_accum_arena_stats_u64(&sdstats->astats.decay_muzzy.purged,
		    &astats->astats.decay_muzzy.purged);

#define OP(mtx) malloc_mutex_prof_merge(				\
		    &(sdstats->astats.mutex_prof_data[			\
		        arena_prof_mutex_##mtx]),			\
		    &(astats->astats.mutex_prof_data[			\
		        arena_prof_mutex_##mtx]));
MUTEX_PROF_ARENA_MUTEXES
#undef OP
		if (!destroyed) {
			accum_atomic_zu(&sdstats->astats.base,
			    &astats->astats.base);
			accum_atomic_zu(&sdstats->astats.internal,
			    &astats->astats.internal);
			accum_atomic_zu(&sdstats->astats.resident,
			    &astats->astats.resident);
			accum_atomic_zu(&sdstats->astats.metadata_thp,
			    &astats->astats.metadata_thp);
		} else {
			assert(atomic_load_zu(
			    &astats->astats.internal, ATOMIC_RELAXED) == 0);
		}

		if (!destroyed) {
			sdstats->allocated_small += astats->allocated_small;
		} else {
			assert(astats->allocated_small == 0);
		}
		sdstats->nmalloc_small += astats->nmalloc_small;
		sdstats->ndalloc_small += astats->ndalloc_small;
		sdstats->nrequests_small += astats->nrequests_small;

		if (!destroyed) {
			accum_atomic_zu(&sdstats->astats.allocated_large,
			    &astats->astats.allocated_large);
		} else {
			assert(atomic_load_zu(&astats->astats.allocated_large,
			    ATOMIC_RELAXED) == 0);
		}
		ctl_accum_arena_stats_u64(&sdstats->astats.nmalloc_large,
		    &astats->astats.nmalloc_large);
		ctl_accum_arena_stats_u64(&sdstats->astats.ndalloc_large,
		    &astats->astats.ndalloc_large);
		ctl_accum_arena_stats_u64(&sdstats->astats.nrequests_large,
		    &astats->astats.nrequests_large);

		accum_atomic_zu(&sdstats->astats.tcache_bytes,
		    &astats->astats.tcache_bytes);

		if (ctl_arena->arena_ind == 0) {
			sdstats->astats.uptime = astats->astats.uptime;
		}

		for (i = 0; i < NBINS; i++) {
			sdstats->bstats[i].nmalloc += astats->bstats[i].nmalloc;
			sdstats->bstats[i].ndalloc += astats->bstats[i].ndalloc;
			sdstats->bstats[i].nrequests +=
			    astats->bstats[i].nrequests;
			if (!destroyed) {
				sdstats->bstats[i].curregs +=
				    astats->bstats[i].curregs;
			} else {
				assert(astats->bstats[i].curregs == 0);
			}
			sdstats->bstats[i].nfills += astats->bstats[i].nfills;
			sdstats->bstats[i].nflushes +=
			    astats->bstats[i].nflushes;
			sdstats->bstats[i].nslabs += astats->bstats[i].nslabs;
			sdstats->bstats[i].reslabs += astats->bstats[i].reslabs;
			if (!destroyed) {
				sdstats->bstats[i].curslabs +=
				    astats->bstats[i].curslabs;
			} else {
				assert(astats->bstats[i].curslabs == 0);
			}
			malloc_mutex_prof_merge(&sdstats->bstats[i].mutex_data,
			    &astats->bstats[i].mutex_data);
		}

		for (i = 0; i < NSIZES - NBINS; i++) {
			ctl_accum_arena_stats_u64(&sdstats->lstats[i].nmalloc,
			    &astats->lstats[i].nmalloc);
			ctl_accum_arena_stats_u64(&sdstats->lstats[i].ndalloc,
			    &astats->lstats[i].ndalloc);
			ctl_accum_arena_stats_u64(&sdstats->lstats[i].nrequests,
			    &astats->lstats[i].nrequests);
			if (!destroyed) {
				sdstats->lstats[i].curlextents +=
				    astats->lstats[i].curlextents;
			} else {
				assert(astats->lstats[i].curlextents == 0);
			}
		}
	}
}

static void
ctl_arena_refresh(tsdn_t *tsdn, arena_t *arena, ctl_arena_t *ctl_sdarena,
    unsigned i, bool destroyed) {
	ctl_arena_t *ctl_arena = arenas_i(i);

	ctl_arena_clear(ctl_arena);
	ctl_arena_stats_amerge(tsdn, ctl_arena, arena);
	/* Merge into sum stats as well. */
	ctl_arena_stats_sdmerge(ctl_sdarena, ctl_arena, destroyed);
}

static unsigned
ctl_arena_init(tsd_t *tsd, extent_hooks_t *extent_hooks) {
	unsigned arena_ind;
	ctl_arena_t *ctl_arena;

	if ((ctl_arena = ql_last(&ctl_arenas->destroyed, destroyed_link)) !=
	    NULL) {
		ql_remove(&ctl_arenas->destroyed, ctl_arena, destroyed_link);
		arena_ind = ctl_arena->arena_ind;
	} else {
		arena_ind = ctl_arenas->narenas;
	}

	/* Trigger stats allocation. */
	if (arenas_i_impl(tsd, arena_ind, false, true) == NULL) {
		return UINT_MAX;
	}

	/* Initialize new arena. */
	if (arena_init(tsd_tsdn(tsd), arena_ind, extent_hooks) == NULL) {
		return UINT_MAX;
	}

	if (arena_ind == ctl_arenas->narenas) {
		ctl_arenas->narenas++;
	}

	return arena_ind;
}

static void
ctl_background_thread_stats_read(tsdn_t *tsdn) {
	background_thread_stats_t *stats = &ctl_stats->background_thread;
	if (!have_background_thread ||
	    background_thread_stats_read(tsdn, stats)) {
		memset(stats, 0, sizeof(background_thread_stats_t));
		nstime_init(&stats->run_interval, 0);
	}
}

static void
ctl_refresh(tsdn_t *tsdn) {
	unsigned i;
	ctl_arena_t *ctl_sarena = arenas_i(MALLCTL_ARENAS_ALL);
	VARIABLE_ARRAY(arena_t *, tarenas, ctl_arenas->narenas);

	/*
	 * Clear sum stats, since they will be merged into by
	 * ctl_arena_refresh().
	 */
	ctl_arena_clear(ctl_sarena);

	for (i = 0; i < ctl_arenas->narenas; i++) {
		tarenas[i] = arena_get(tsdn, i, false);
	}

	for (i = 0; i < ctl_arenas->narenas; i++) {
		ctl_arena_t *ctl_arena = arenas_i(i);
		bool initialized = (tarenas[i] != NULL);

		ctl_arena->initialized = initialized;
		if (initialized) {
			ctl_arena_refresh(tsdn, tarenas[i], ctl_sarena, i,
			    false);
		}
	}

	if (config_stats) {
		ctl_stats->allocated = ctl_sarena->astats->allocated_small +
		    atomic_load_zu(&ctl_sarena->astats->astats.allocated_large,
			ATOMIC_RELAXED);
		ctl_stats->active = (ctl_sarena->pactive << LG_PAGE);
		ctl_stats->metadata = atomic_load_zu(
		    &ctl_sarena->astats->astats.base, ATOMIC_RELAXED) +
		    atomic_load_zu(&ctl_sarena->astats->astats.internal,
			ATOMIC_RELAXED);
		ctl_stats->metadata_thp = atomic_load_zu(
		    &ctl_sarena->astats->astats.metadata_thp, ATOMIC_RELAXED);
		ctl_stats->resident = atomic_load_zu(
		    &ctl_sarena->astats->astats.resident, ATOMIC_RELAXED);
		ctl_stats->mapped = atomic_load_zu(
		    &ctl_sarena->astats->astats.mapped, ATOMIC_RELAXED);
		ctl_stats->retained = atomic_load_zu(
		    &ctl_sarena->astats->astats.retained, ATOMIC_RELAXED);

		ctl_background_thread_stats_read(tsdn);

#define READ_GLOBAL_MUTEX_PROF_DATA(i, mtx)				\
    malloc_mutex_lock(tsdn, &mtx);					\
    malloc_mutex_prof_read(tsdn, &ctl_stats->mutex_prof_data[i], &mtx);	\
    malloc_mutex_unlock(tsdn, &mtx);

		if (config_prof && opt_prof) {
			READ_GLOBAL_MUTEX_PROF_DATA(global_prof_mutex_prof,
			    bt2gctx_mtx);
		}
		if (have_background_thread) {
			READ_GLOBAL_MUTEX_PROF_DATA(
			    global_prof_mutex_background_thread,
			    background_thread_lock);
		} else {
			memset(&ctl_stats->mutex_prof_data[
			    global_prof_mutex_background_thread], 0,
			    sizeof(mutex_prof_data_t));
		}
		/* We own ctl mutex already. */
		malloc_mutex_prof_read(tsdn,
		    &ctl_stats->mutex_prof_data[global_prof_mutex_ctl],
		    &ctl_mtx);
#undef READ_GLOBAL_MUTEX_PROF_DATA
	}
	ctl_arenas->epoch++;
}

static bool
ctl_init(tsd_t *tsd) {
	bool ret;
	tsdn_t *tsdn = tsd_tsdn(tsd);

	malloc_mutex_lock(tsdn, &ctl_mtx);
	if (!ctl_initialized) {
		ctl_arena_t *ctl_sarena, *ctl_darena;
		unsigned i;

		/*
		 * Allocate demand-zeroed space for pointers to the full
		 * range of supported arena indices.
		 */
		if (ctl_arenas == NULL) {
			ctl_arenas = (ctl_arenas_t *)base_alloc(tsdn,
			    b0get(), sizeof(ctl_arenas_t), QUANTUM);
			if (ctl_arenas == NULL) {
				ret = true;
				goto label_return;
			}
		}

		if (config_stats && ctl_stats == NULL) {
			ctl_stats = (ctl_stats_t *)base_alloc(tsdn, b0get(),
			    sizeof(ctl_stats_t), QUANTUM);
			if (ctl_stats == NULL) {
				ret = true;
				goto label_return;
			}
		}

		/*
		 * Allocate space for the current full range of arenas
		 * here rather than doing it lazily elsewhere, in order
		 * to limit when OOM-caused errors can occur.
		 */
		if ((ctl_sarena = arenas_i_impl(tsd, MALLCTL_ARENAS_ALL, false,
		    true)) == NULL) {
			ret = true;
			goto label_return;
		}
		ctl_sarena->initialized = true;

		if ((ctl_darena = arenas_i_impl(tsd, MALLCTL_ARENAS_DESTROYED,
		    false, true)) == NULL) {
			ret = true;
			goto label_return;
		}
		ctl_arena_clear(ctl_darena);
		/*
		 * Don't toggle ctl_darena to initialized until an arena is
		 * actually destroyed, so that arena.<i>.initialized can be used
		 * to query whether the stats are relevant.
		 */

		ctl_arenas->narenas = narenas_total_get();
		for (i = 0; i < ctl_arenas->narenas; i++) {
			if (arenas_i_impl(tsd, i, false, true) == NULL) {
				ret = true;
				goto label_return;
			}
		}

		ql_new(&ctl_arenas->destroyed);
		ctl_refresh(tsdn);

		ctl_initialized = true;
	}

	ret = false;
label_return:
	malloc_mutex_unlock(tsdn, &ctl_mtx);
	return ret;
}

static int
ctl_lookup(tsdn_t *tsdn, const char *name, ctl_node_t const **nodesp,
    size_t *mibp, size_t *depthp) {
	int ret;
	const char *elm, *tdot, *dot;
	size_t elen, i, j;
	const ctl_named_node_t *node;

	elm = name;
	/* Equivalent to strchrnul(). */
	dot = ((tdot = strchr(elm, '.')) != NULL) ? tdot : strchr(elm, '\0');
	elen = (size_t)((uintptr_t)dot - (uintptr_t)elm);
	if (elen == 0) {
		ret = ENOENT;
		goto label_return;
	}
	node = super_root_node;
	for (i = 0; i < *depthp; i++) {
		assert(node);
		assert(node->nchildren > 0);
		if (ctl_named_node(node->children) != NULL) {
			const ctl_named_node_t *pnode = node;

			/* Children are named. */
			for (j = 0; j < node->nchildren; j++) {
				const ctl_named_node_t *child =
				    ctl_named_children(node, j);
				if (strlen(child->name) == elen &&
				    strncmp(elm, child->name, elen) == 0) {
					node = child;
					if (nodesp != NULL) {
						nodesp[i] =
						    (const ctl_node_t *)node;
					}
					mibp[i] = j;
					break;
				}
			}
			if (node == pnode) {
				ret = ENOENT;
				goto label_return;
			}
		} else {
			uintmax_t index;
			const ctl_indexed_node_t *inode;

			/* Children are indexed. */
			index = malloc_strtoumax(elm, NULL, 10);
			if (index == UINTMAX_MAX || index > SIZE_T_MAX) {
				ret = ENOENT;
				goto label_return;
			}

			inode = ctl_indexed_node(node->children);
			node = inode->index(tsdn, mibp, *depthp, (size_t)index);
			if (node == NULL) {
				ret = ENOENT;
				goto label_return;
			}

			if (nodesp != NULL) {
				nodesp[i] = (const ctl_node_t *)node;
			}
			mibp[i] = (size_t)index;
		}

		if (node->ctl != NULL) {
			/* Terminal node. */
			if (*dot != '\0') {
				/*
				 * The name contains more elements than are
				 * in this path through the tree.
				 */
				ret = ENOENT;
				goto label_return;
			}
			/* Complete lookup successful. */
			*depthp = i + 1;
			break;
		}

		/* Update elm. */
		if (*dot == '\0') {
			/* No more elements. */
			ret = ENOENT;
			goto label_return;
		}
		elm = &dot[1];
		dot = ((tdot = strchr(elm, '.')) != NULL) ? tdot :
		    strchr(elm, '\0');
		elen = (size_t)((uintptr_t)dot - (uintptr_t)elm);
	}

	ret = 0;
label_return:
	return ret;
}

int
ctl_byname(tsd_t *tsd, const char *name, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen) {
	int ret;
	size_t depth;
	ctl_node_t const *nodes[CTL_MAX_DEPTH];
	size_t mib[CTL_MAX_DEPTH];
	const ctl_named_node_t *node;

	if (!ctl_initialized && ctl_init(tsd)) {
		ret = EAGAIN;
		goto label_return;
	}

	depth = CTL_MAX_DEPTH;
	ret = ctl_lookup(tsd_tsdn(tsd), name, nodes, mib, &depth);
	if (ret != 0) {
		goto label_return;
	}

	node = ctl_named_node(nodes[depth-1]);
	if (node != NULL && node->ctl) {
		ret = node->ctl(tsd, mib, depth, oldp, oldlenp, newp, newlen);
	} else {
		/* The name refers to a partial path through the ctl tree. */
		ret = ENOENT;
	}

label_return:
	return(ret);
}

int
ctl_nametomib(tsd_t *tsd, const char *name, size_t *mibp, size_t *miblenp) {
	int ret;

	if (!ctl_initialized && ctl_init(tsd)) {
		ret = EAGAIN;
		goto label_return;
	}

	ret = ctl_lookup(tsd_tsdn(tsd), name, NULL, mibp, miblenp);
label_return:
	return(ret);
}

int
ctl_bymib(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	const ctl_named_node_t *node;
	size_t i;

	if (!ctl_initialized && ctl_init(tsd)) {
		ret = EAGAIN;
		goto label_return;
	}

	/* Iterate down the tree. */
	node = super_root_node;
	for (i = 0; i < miblen; i++) {
		assert(node);
		assert(node->nchildren > 0);
		if (ctl_named_node(node->children) != NULL) {
			/* Children are named. */
			if (node->nchildren <= mib[i]) {
				ret = ENOENT;
				goto label_return;
			}
			node = ctl_named_children(node, mib[i]);
		} else {
			const ctl_indexed_node_t *inode;

			/* Indexed element. */
			inode = ctl_indexed_node(node->children);
			node = inode->index(tsd_tsdn(tsd), mib, miblen, mib[i]);
			if (node == NULL) {
				ret = ENOENT;
				goto label_return;
			}
		}
	}

	/* Call the ctl function. */
	if (node && node->ctl) {
		ret = node->ctl(tsd, mib, miblen, oldp, oldlenp, newp, newlen);
	} else {
		/* Partial MIB. */
		ret = ENOENT;
	}

label_return:
	return(ret);
}

bool
ctl_boot(void) {
	if (malloc_mutex_init(&ctl_mtx, "ctl", WITNESS_RANK_CTL,
	    malloc_mutex_rank_exclusive)) {
		return true;
	}

	ctl_initialized = false;

	return false;
}

void
ctl_prefork(tsdn_t *tsdn) {
	malloc_mutex_prefork(tsdn, &ctl_mtx);
}

void
ctl_postfork_parent(tsdn_t *tsdn) {
	malloc_mutex_postfork_parent(tsdn, &ctl_mtx);
}

void
ctl_postfork_child(tsdn_t *tsdn) {
	malloc_mutex_postfork_child(tsdn, &ctl_mtx);
}

/******************************************************************************/
/* *_ctl() functions. */

#define READONLY()	do {						\
	if (newp != NULL || newlen != 0) {				\
		ret = EPERM;						\
		goto label_return;					\
	}								\
} while (0)

#define WRITEONLY()	do {						\
	if (oldp != NULL || oldlenp != NULL) {				\
		ret = EPERM;						\
		goto label_return;					\
	}								\
} while (0)

#define READ_XOR_WRITE()	do {					\
	if ((oldp != NULL && oldlenp != NULL) && (newp != NULL ||	\
	    newlen != 0)) {						\
		ret = EPERM;						\
		goto label_return;					\
	}								\
} while (0)

#define READ(v, t)	do {						\
	if (oldp != NULL && oldlenp != NULL) {				\
		if (*oldlenp != sizeof(t)) {				\
			size_t	copylen = (sizeof(t) <= *oldlenp)	\
			    ? sizeof(t) : *oldlenp;			\
			memcpy(oldp, (void *)&(v), copylen);		\
			ret = EINVAL;					\
			goto label_return;				\
		}							\
		*(t *)oldp = (v);					\
	}								\
} while (0)

#define WRITE(v, t)	do {						\
	if (newp != NULL) {						\
		if (newlen != sizeof(t)) {				\
			ret = EINVAL;					\
			goto label_return;				\
		}							\
		(v) = *(t *)newp;					\
	}								\
} while (0)

#define MIB_UNSIGNED(v, i) do {						\
	if (mib[i] > UINT_MAX) {					\
		ret = EFAULT;						\
		goto label_return;					\
	}								\
	v = (unsigned)mib[i];						\
} while (0)

/*
 * There's a lot of code duplication in the following macros due to limitations
 * in how nested cpp macros are expanded.
 */
#define CTL_RO_CLGEN(c, l, n, v, t)					\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	if (!(c)) {							\
		return ENOENT;						\
	}								\
	if (l) {							\
		malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);		\
	}								\
	READONLY();							\
	oldval = (v);							\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	if (l) {							\
		malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);		\
	}								\
	return ret;							\
}

#define CTL_RO_CGEN(c, n, v, t)						\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	if (!(c)) {							\
		return ENOENT;						\
	}								\
	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);			\
	READONLY();							\
	oldval = (v);							\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);			\
	return ret;							\
}

#define CTL_RO_GEN(n, v, t)						\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);			\
	READONLY();							\
	oldval = (v);							\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);			\
	return ret;							\
}

/*
 * ctl_mtx is not acquired, under the assumption that no pertinent data will
 * mutate during the call.
 */
#define CTL_RO_NL_CGEN(c, n, v, t)					\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	if (!(c)) {							\
		return ENOENT;						\
	}								\
	READONLY();							\
	oldval = (v);							\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	return ret;							\
}

#define CTL_RO_NL_GEN(n, v, t)						\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	READONLY();							\
	oldval = (v);							\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	return ret;							\
}

#define CTL_TSD_RO_NL_CGEN(c, n, m, t)					\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	if (!(c)) {							\
		return ENOENT;						\
	}								\
	READONLY();							\
	oldval = (m(tsd));						\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	return ret;							\
}

#define CTL_RO_CONFIG_GEN(n, t)						\
static int								\
n##_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,	\
    size_t *oldlenp, void *newp, size_t newlen) {			\
	int ret;							\
	t oldval;							\
									\
	READONLY();							\
	oldval = n;							\
	READ(oldval, t);						\
									\
	ret = 0;							\
label_return:								\
	return ret;							\
}

/******************************************************************************/

CTL_RO_NL_GEN(version, JEMALLOC_VERSION, const char *)

static int
epoch_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	UNUSED uint64_t newval;

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	WRITE(newval, uint64_t);
	if (newp != NULL) {
		ctl_refresh(tsd_tsdn(tsd));
	}
	READ(ctl_arenas->epoch, uint64_t);

	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

static int
background_thread_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	bool oldval;

	if (!have_background_thread) {
		return ENOENT;
	}
	background_thread_ctl_init(tsd_tsdn(tsd));

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	malloc_mutex_lock(tsd_tsdn(tsd), &background_thread_lock);
	if (newp == NULL) {
		oldval = background_thread_enabled();
		READ(oldval, bool);
	} else {
		if (newlen != sizeof(bool)) {
			ret = EINVAL;
			goto label_return;
		}
		oldval = background_thread_enabled();
		READ(oldval, bool);

		bool newval = *(bool *)newp;
		if (newval == oldval) {
			ret = 0;
			goto label_return;
		}

		background_thread_enabled_set(tsd_tsdn(tsd), newval);
		if (newval) {
			if (!can_enable_background_thread) {
				malloc_printf("<jemalloc>: Error in dlsym("
			            "RTLD_NEXT, \"pthread_create\"). Cannot "
				    "enable background_thread\n");
				ret = EFAULT;
				goto label_return;
			}
			if (background_threads_enable(tsd)) {
				ret = EFAULT;
				goto label_return;
			}
		} else {
			if (background_threads_disable(tsd)) {
				ret = EFAULT;
				goto label_return;
			}
		}
	}
	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &background_thread_lock);
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);

	return ret;
}

static int
max_background_threads_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	size_t oldval;

	if (!have_background_thread) {
		return ENOENT;
	}
	background_thread_ctl_init(tsd_tsdn(tsd));

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	malloc_mutex_lock(tsd_tsdn(tsd), &background_thread_lock);
	if (newp == NULL) {
		oldval = max_background_threads;
		READ(oldval, size_t);
	} else {
		if (newlen != sizeof(size_t)) {
			ret = EINVAL;
			goto label_return;
		}
		oldval = max_background_threads;
		READ(oldval, size_t);

		size_t newval = *(size_t *)newp;
		if (newval == oldval) {
			ret = 0;
			goto label_return;
		}
		if (newval > opt_max_background_threads) {
			ret = EINVAL;
			goto label_return;
		}

		if (background_thread_enabled()) {
			if (!can_enable_background_thread) {
				malloc_printf("<jemalloc>: Error in dlsym("
			            "RTLD_NEXT, \"pthread_create\"). Cannot "
				    "enable background_thread\n");
				ret = EFAULT;
				goto label_return;
			}
			background_thread_enabled_set(tsd_tsdn(tsd), false);
			if (background_threads_disable(tsd)) {
				ret = EFAULT;
				goto label_return;
			}
			max_background_threads = newval;
			background_thread_enabled_set(tsd_tsdn(tsd), true);
			if (background_threads_enable(tsd)) {
				ret = EFAULT;
				goto label_return;
			}
		} else {
			max_background_threads = newval;
		}
	}
	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &background_thread_lock);
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);

	return ret;
}

/******************************************************************************/

CTL_RO_CONFIG_GEN(config_cache_oblivious, bool)
CTL_RO_CONFIG_GEN(config_debug, bool)
CTL_RO_CONFIG_GEN(config_fill, bool)
CTL_RO_CONFIG_GEN(config_lazy_lock, bool)
CTL_RO_CONFIG_GEN(config_malloc_conf, const char *)
CTL_RO_CONFIG_GEN(config_prof, bool)
CTL_RO_CONFIG_GEN(config_prof_libgcc, bool)
CTL_RO_CONFIG_GEN(config_prof_libunwind, bool)
CTL_RO_CONFIG_GEN(config_stats, bool)
CTL_RO_CONFIG_GEN(config_utrace, bool)
CTL_RO_CONFIG_GEN(config_xmalloc, bool)

/******************************************************************************/

CTL_RO_NL_GEN(opt_abort, opt_abort, bool)
CTL_RO_NL_GEN(opt_abort_conf, opt_abort_conf, bool)
CTL_RO_NL_GEN(opt_metadata_thp, metadata_thp_mode_names[opt_metadata_thp],
    const char *)
CTL_RO_NL_GEN(opt_retain, opt_retain, bool)
CTL_RO_NL_GEN(opt_dss, opt_dss, const char *)
CTL_RO_NL_GEN(opt_narenas, opt_narenas, unsigned)
CTL_RO_NL_GEN(opt_percpu_arena, percpu_arena_mode_names[opt_percpu_arena],
    const char *)
CTL_RO_NL_GEN(opt_background_thread, opt_background_thread, bool)
CTL_RO_NL_GEN(opt_max_background_threads, opt_max_background_threads, size_t)
CTL_RO_NL_GEN(opt_dirty_decay_ms, opt_dirty_decay_ms, ssize_t)
CTL_RO_NL_GEN(opt_muzzy_decay_ms, opt_muzzy_decay_ms, ssize_t)
CTL_RO_NL_GEN(opt_stats_print, opt_stats_print, bool)
CTL_RO_NL_GEN(opt_stats_print_opts, opt_stats_print_opts, const char *)
CTL_RO_NL_CGEN(config_fill, opt_junk, opt_junk, const char *)
CTL_RO_NL_CGEN(config_fill, opt_zero, opt_zero, bool)
CTL_RO_NL_CGEN(config_utrace, opt_utrace, opt_utrace, bool)
CTL_RO_NL_CGEN(config_xmalloc, opt_xmalloc, opt_xmalloc, bool)
CTL_RO_NL_GEN(opt_tcache, opt_tcache, bool)
CTL_RO_NL_GEN(opt_thp, thp_mode_names[opt_thp], const char *)
CTL_RO_NL_GEN(opt_lg_extent_max_active_fit, opt_lg_extent_max_active_fit,
    size_t)
CTL_RO_NL_GEN(opt_lg_tcache_max, opt_lg_tcache_max, ssize_t)
CTL_RO_NL_CGEN(config_prof, opt_prof, opt_prof, bool)
CTL_RO_NL_CGEN(config_prof, opt_prof_prefix, opt_prof_prefix, const char *)
CTL_RO_NL_CGEN(config_prof, opt_prof_active, opt_prof_active, bool)
CTL_RO_NL_CGEN(config_prof, opt_prof_thread_active_init,
    opt_prof_thread_active_init, bool)
CTL_RO_NL_CGEN(config_prof, opt_lg_prof_sample, opt_lg_prof_sample, size_t)
CTL_RO_NL_CGEN(config_prof, opt_prof_accum, opt_prof_accum, bool)
CTL_RO_NL_CGEN(config_prof, opt_lg_prof_interval, opt_lg_prof_interval, ssize_t)
CTL_RO_NL_CGEN(config_prof, opt_prof_gdump, opt_prof_gdump, bool)
CTL_RO_NL_CGEN(config_prof, opt_prof_final, opt_prof_final, bool)
CTL_RO_NL_CGEN(config_prof, opt_prof_leak, opt_prof_leak, bool)

/******************************************************************************/

static int
thread_arena_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	arena_t *oldarena;
	unsigned newind, oldind;

	oldarena = arena_choose(tsd, NULL);
	if (oldarena == NULL) {
		return EAGAIN;
	}
	newind = oldind = arena_ind_get(oldarena);
	WRITE(newind, unsigned);
	READ(oldind, unsigned);

	if (newind != oldind) {
		arena_t *newarena;

		if (newind >= narenas_total_get()) {
			/* New arena index is out of range. */
			ret = EFAULT;
			goto label_return;
		}

		if (have_percpu_arena &&
		    PERCPU_ARENA_ENABLED(opt_percpu_arena)) {
			if (newind < percpu_arena_ind_limit(opt_percpu_arena)) {
				/*
				 * If perCPU arena is enabled, thread_arena
				 * control is not allowed for the auto arena
				 * range.
				 */
				ret = EPERM;
				goto label_return;
			}
		}

		/* Initialize arena if necessary. */
		newarena = arena_get(tsd_tsdn(tsd), newind, true);
		if (newarena == NULL) {
			ret = EAGAIN;
			goto label_return;
		}
		/* Set new arena/tcache associations. */
		arena_migrate(tsd, oldind, newind);
		if (tcache_available(tsd)) {
			tcache_arena_reassociate(tsd_tsdn(tsd),
			    tsd_tcachep_get(tsd), newarena);
		}
	}

	ret = 0;
label_return:
	return ret;
}

CTL_TSD_RO_NL_CGEN(config_stats, thread_allocated, tsd_thread_allocated_get,
    uint64_t)
CTL_TSD_RO_NL_CGEN(config_stats, thread_allocatedp, tsd_thread_allocatedp_get,
    uint64_t *)
CTL_TSD_RO_NL_CGEN(config_stats, thread_deallocated, tsd_thread_deallocated_get,
    uint64_t)
CTL_TSD_RO_NL_CGEN(config_stats, thread_deallocatedp,
    tsd_thread_deallocatedp_get, uint64_t *)

static int
thread_tcache_enabled_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	bool oldval;

	oldval = tcache_enabled_get(tsd);
	if (newp != NULL) {
		if (newlen != sizeof(bool)) {
			ret = EINVAL;
			goto label_return;
		}
		tcache_enabled_set(tsd, *(bool *)newp);
	}
	READ(oldval, bool);

	ret = 0;
label_return:
	return ret;
}

static int
thread_tcache_flush_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;

	if (!tcache_available(tsd)) {
		ret = EFAULT;
		goto label_return;
	}

	READONLY();
	WRITEONLY();

	tcache_flush(tsd);

	ret = 0;
label_return:
	return ret;
}

static int
thread_prof_name_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;

	if (!config_prof) {
		return ENOENT;
	}

	READ_XOR_WRITE();

	if (newp != NULL) {
		if (newlen != sizeof(const char *)) {
			ret = EINVAL;
			goto label_return;
		}

		if ((ret = prof_thread_name_set(tsd, *(const char **)newp)) !=
		    0) {
			goto label_return;
		}
	} else {
		const char *oldname = prof_thread_name_get(tsd);
		READ(oldname, const char *);
	}

	ret = 0;
label_return:
	return ret;
}

static int
thread_prof_active_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	bool oldval;

	if (!config_prof) {
		return ENOENT;
	}

	oldval = prof_thread_active_get(tsd);
	if (newp != NULL) {
		if (newlen != sizeof(bool)) {
			ret = EINVAL;
			goto label_return;
		}
		if (prof_thread_active_set(tsd, *(bool *)newp)) {
			ret = EAGAIN;
			goto label_return;
		}
	}
	READ(oldval, bool);

	ret = 0;
label_return:
	return ret;
}

/******************************************************************************/

static int
tcache_create_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned tcache_ind;

	READONLY();
	if (tcaches_create(tsd, &tcache_ind)) {
		ret = EFAULT;
		goto label_return;
	}
	READ(tcache_ind, unsigned);

	ret = 0;
label_return:
	return ret;
}

static int
tcache_flush_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned tcache_ind;

	WRITEONLY();
	tcache_ind = UINT_MAX;
	WRITE(tcache_ind, unsigned);
	if (tcache_ind == UINT_MAX) {
		ret = EFAULT;
		goto label_return;
	}
	tcaches_flush(tsd, tcache_ind);

	ret = 0;
label_return:
	return ret;
}

static int
tcache_destroy_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned tcache_ind;

	WRITEONLY();
	tcache_ind = UINT_MAX;
	WRITE(tcache_ind, unsigned);
	if (tcache_ind == UINT_MAX) {
		ret = EFAULT;
		goto label_return;
	}
	tcaches_destroy(tsd, tcache_ind);

	ret = 0;
label_return:
	return ret;
}

/******************************************************************************/

static int
arena_i_initialized_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	tsdn_t *tsdn = tsd_tsdn(tsd);
	unsigned arena_ind;
	bool initialized;

	READONLY();
	MIB_UNSIGNED(arena_ind, 1);

	malloc_mutex_lock(tsdn, &ctl_mtx);
	initialized = arenas_i(arena_ind)->initialized;
	malloc_mutex_unlock(tsdn, &ctl_mtx);

	READ(initialized, bool);

	ret = 0;
label_return:
	return ret;
}

static void
arena_i_decay(tsdn_t *tsdn, unsigned arena_ind, bool all) {
	malloc_mutex_lock(tsdn, &ctl_mtx);
	{
		unsigned narenas = ctl_arenas->narenas;

		/*
		 * Access via index narenas is deprecated, and scheduled for
		 * removal in 6.0.0.
		 */
		if (arena_ind == MALLCTL_ARENAS_ALL || arena_ind == narenas) {
			unsigned i;
			VARIABLE_ARRAY(arena_t *, tarenas, narenas);

			for (i = 0; i < narenas; i++) {
				tarenas[i] = arena_get(tsdn, i, false);
			}

			/*
			 * No further need to hold ctl_mtx, since narenas and
			 * tarenas contain everything needed below.
			 */
			malloc_mutex_unlock(tsdn, &ctl_mtx);

			for (i = 0; i < narenas; i++) {
				if (tarenas[i] != NULL) {
					arena_decay(tsdn, tarenas[i], false,
					    all);
				}
			}
		} else {
			arena_t *tarena;

			assert(arena_ind < narenas);

			tarena = arena_get(tsdn, arena_ind, false);

			/* No further need to hold ctl_mtx. */
			malloc_mutex_unlock(tsdn, &ctl_mtx);

			if (tarena != NULL) {
				arena_decay(tsdn, tarena, false, all);
			}
		}
	}
}

static int
arena_i_decay_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;

	READONLY();
	WRITEONLY();
	MIB_UNSIGNED(arena_ind, 1);
	arena_i_decay(tsd_tsdn(tsd), arena_ind, false);

	ret = 0;
label_return:
	return ret;
}

static int
arena_i_purge_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;

	READONLY();
	WRITEONLY();
	MIB_UNSIGNED(arena_ind, 1);
	arena_i_decay(tsd_tsdn(tsd), arena_ind, true);

	ret = 0;
label_return:
	return ret;
}

static int
arena_i_reset_destroy_helper(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen, unsigned *arena_ind,
    arena_t **arena) {
	int ret;

	READONLY();
	WRITEONLY();
	MIB_UNSIGNED(*arena_ind, 1);

	*arena = arena_get(tsd_tsdn(tsd), *arena_ind, false);
	if (*arena == NULL || arena_is_auto(*arena)) {
		ret = EFAULT;
		goto label_return;
	}

	ret = 0;
label_return:
	return ret;
}

static void
arena_reset_prepare_background_thread(tsd_t *tsd, unsigned arena_ind) {
	/* Temporarily disable the background thread during arena reset. */
	if (have_background_thread) {
		malloc_mutex_lock(tsd_tsdn(tsd), &background_thread_lock);
		if (background_thread_enabled()) {
			unsigned ind = arena_ind % ncpus;
			background_thread_info_t *info =
			    &background_thread_info[ind];
			assert(info->state == background_thread_started);
			malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
			info->state = background_thread_paused;
			malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);
		}
	}
}

static void
arena_reset_finish_background_thread(tsd_t *tsd, unsigned arena_ind) {
	if (have_background_thread) {
		if (background_thread_enabled()) {
			unsigned ind = arena_ind % ncpus;
			background_thread_info_t *info =
			    &background_thread_info[ind];
			assert(info->state == background_thread_paused);
			malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
			info->state = background_thread_started;
			malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);
		}
		malloc_mutex_unlock(tsd_tsdn(tsd), &background_thread_lock);
	}
}

static int
arena_i_reset_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;
	arena_t *arena;

	ret = arena_i_reset_destroy_helper(tsd, mib, miblen, oldp, oldlenp,
	    newp, newlen, &arena_ind, &arena);
	if (ret != 0) {
		return ret;
	}

	arena_reset_prepare_background_thread(tsd, arena_ind);
	arena_reset(tsd, arena);
	arena_reset_finish_background_thread(tsd, arena_ind);

	return ret;
}

static int
arena_i_destroy_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;
	arena_t *arena;
	ctl_arena_t *ctl_darena, *ctl_arena;

	ret = arena_i_reset_destroy_helper(tsd, mib, miblen, oldp, oldlenp,
	    newp, newlen, &arena_ind, &arena);
	if (ret != 0) {
		goto label_return;
	}

	if (arena_nthreads_get(arena, false) != 0 || arena_nthreads_get(arena,
	    true) != 0) {
		ret = EFAULT;
		goto label_return;
	}

	arena_reset_prepare_background_thread(tsd, arena_ind);
	/* Merge stats after resetting and purging arena. */
	arena_reset(tsd, arena);
	arena_decay(tsd_tsdn(tsd), arena, false, true);
	ctl_darena = arenas_i(MALLCTL_ARENAS_DESTROYED);
	ctl_darena->initialized = true;
	ctl_arena_refresh(tsd_tsdn(tsd), arena, ctl_darena, arena_ind, true);
	/* Destroy arena. */
	arena_destroy(tsd, arena);
	ctl_arena = arenas_i(arena_ind);
	ctl_arena->initialized = false;
	/* Record arena index for later recycling via arenas.create. */
	ql_elm_new(ctl_arena, destroyed_link);
	ql_tail_insert(&ctl_arenas->destroyed, ctl_arena, destroyed_link);
	arena_reset_finish_background_thread(tsd, arena_ind);

	assert(ret == 0);
label_return:
	return ret;
}

static int
arena_i_dss_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	const char *dss = NULL;
	unsigned arena_ind;
	dss_prec_t dss_prec_old = dss_prec_limit;
	dss_prec_t dss_prec = dss_prec_limit;

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	WRITE(dss, const char *);
	MIB_UNSIGNED(arena_ind, 1);
	if (dss != NULL) {
		int i;
		bool match = false;

		for (i = 0; i < dss_prec_limit; i++) {
			if (strcmp(dss_prec_names[i], dss) == 0) {
				dss_prec = i;
				match = true;
				break;
			}
		}

		if (!match) {
			ret = EINVAL;
			goto label_return;
		}
	}

	/*
	 * Access via index narenas is deprecated, and scheduled for removal in
	 * 6.0.0.
	 */
	if (arena_ind == MALLCTL_ARENAS_ALL || arena_ind ==
	    ctl_arenas->narenas) {
		if (dss_prec != dss_prec_limit &&
		    extent_dss_prec_set(dss_prec)) {
			ret = EFAULT;
			goto label_return;
		}
		dss_prec_old = extent_dss_prec_get();
	} else {
		arena_t *arena = arena_get(tsd_tsdn(tsd), arena_ind, false);
		if (arena == NULL || (dss_prec != dss_prec_limit &&
		    arena_dss_prec_set(arena, dss_prec))) {
			ret = EFAULT;
			goto label_return;
		}
		dss_prec_old = arena_dss_prec_get(arena);
	}

	dss = dss_prec_names[dss_prec_old];
	READ(dss, const char *);

	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

static int
arena_i_decay_ms_ctl_impl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen, bool dirty) {
	int ret;
	unsigned arena_ind;
	arena_t *arena;

	MIB_UNSIGNED(arena_ind, 1);
	arena = arena_get(tsd_tsdn(tsd), arena_ind, false);
	if (arena == NULL) {
		ret = EFAULT;
		goto label_return;
	}

	if (oldp != NULL && oldlenp != NULL) {
		size_t oldval = dirty ? arena_dirty_decay_ms_get(arena) :
		    arena_muzzy_decay_ms_get(arena);
		READ(oldval, ssize_t);
	}
	if (newp != NULL) {
		if (newlen != sizeof(ssize_t)) {
			ret = EINVAL;
			goto label_return;
		}
		if (dirty ? arena_dirty_decay_ms_set(tsd_tsdn(tsd), arena,
		    *(ssize_t *)newp) : arena_muzzy_decay_ms_set(tsd_tsdn(tsd),
		    arena, *(ssize_t *)newp)) {
			ret = EFAULT;
			goto label_return;
		}
	}

	ret = 0;
label_return:
	return ret;
}

static int
arena_i_dirty_decay_ms_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	return arena_i_decay_ms_ctl_impl(tsd, mib, miblen, oldp, oldlenp, newp,
	    newlen, true);
}

static int
arena_i_muzzy_decay_ms_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	return arena_i_decay_ms_ctl_impl(tsd, mib, miblen, oldp, oldlenp, newp,
	    newlen, false);
}

static int
arena_i_extent_hooks_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;
	arena_t *arena;

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	MIB_UNSIGNED(arena_ind, 1);
	if (arena_ind < narenas_total_get()) {
		extent_hooks_t *old_extent_hooks;
		arena = arena_get(tsd_tsdn(tsd), arena_ind, false);
		if (arena == NULL) {
			if (arena_ind >= narenas_auto) {
				ret = EFAULT;
				goto label_return;
			}
			old_extent_hooks =
			    (extent_hooks_t *)&extent_hooks_default;
			READ(old_extent_hooks, extent_hooks_t *);
			if (newp != NULL) {
				/* Initialize a new arena as a side effect. */
				extent_hooks_t *new_extent_hooks
				    JEMALLOC_CC_SILENCE_INIT(NULL);
				WRITE(new_extent_hooks, extent_hooks_t *);
				arena = arena_init(tsd_tsdn(tsd), arena_ind,
				    new_extent_hooks);
				if (arena == NULL) {
					ret = EFAULT;
					goto label_return;
				}
			}
		} else {
			if (newp != NULL) {
				extent_hooks_t *new_extent_hooks
				    JEMALLOC_CC_SILENCE_INIT(NULL);
				WRITE(new_extent_hooks, extent_hooks_t *);
				old_extent_hooks = extent_hooks_set(tsd, arena,
				    new_extent_hooks);
				READ(old_extent_hooks, extent_hooks_t *);
			} else {
				old_extent_hooks = extent_hooks_get(arena);
				READ(old_extent_hooks, extent_hooks_t *);
			}
		}
	} else {
		ret = EFAULT;
		goto label_return;
	}
	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

static int
arena_i_retain_grow_limit_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;
	arena_t *arena;

	if (!opt_retain) {
		/* Only relevant when retain is enabled. */
		return ENOENT;
	}

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	MIB_UNSIGNED(arena_ind, 1);
	if (arena_ind < narenas_total_get() && (arena =
	    arena_get(tsd_tsdn(tsd), arena_ind, false)) != NULL) {
		size_t old_limit, new_limit;
		if (newp != NULL) {
			WRITE(new_limit, size_t);
		}
		bool err = arena_retain_grow_limit_get_set(tsd, arena,
		    &old_limit, newp != NULL ? &new_limit : NULL);
		if (!err) {
			READ(old_limit, size_t);
			ret = 0;
		} else {
			ret = EFAULT;
		}
	} else {
		ret = EFAULT;
	}
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

static const ctl_named_node_t *
arena_i_index(tsdn_t *tsdn, const size_t *mib, size_t miblen, size_t i) {
	const ctl_named_node_t *ret;

	malloc_mutex_lock(tsdn, &ctl_mtx);
	switch (i) {
	case MALLCTL_ARENAS_ALL:
	case MALLCTL_ARENAS_DESTROYED:
		break;
	default:
		if (i > ctl_arenas->narenas) {
			ret = NULL;
			goto label_return;
		}
		break;
	}

	ret = super_arena_i_node;
label_return:
	malloc_mutex_unlock(tsdn, &ctl_mtx);
	return ret;
}

/******************************************************************************/

static int
arenas_narenas_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned narenas;

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	READONLY();
	if (*oldlenp != sizeof(unsigned)) {
		ret = EINVAL;
		goto label_return;
	}
	narenas = ctl_arenas->narenas;
	READ(narenas, unsigned);

	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

static int
arenas_decay_ms_ctl_impl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen, bool dirty) {
	int ret;

	if (oldp != NULL && oldlenp != NULL) {
		size_t oldval = (dirty ? arena_dirty_decay_ms_default_get() :
		    arena_muzzy_decay_ms_default_get());
		READ(oldval, ssize_t);
	}
	if (newp != NULL) {
		if (newlen != sizeof(ssize_t)) {
			ret = EINVAL;
			goto label_return;
		}
		if (dirty ? arena_dirty_decay_ms_default_set(*(ssize_t *)newp)
		    : arena_muzzy_decay_ms_default_set(*(ssize_t *)newp)) {
			ret = EFAULT;
			goto label_return;
		}
	}

	ret = 0;
label_return:
	return ret;
}

static int
arenas_dirty_decay_ms_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	return arenas_decay_ms_ctl_impl(tsd, mib, miblen, oldp, oldlenp, newp,
	    newlen, true);
}

static int
arenas_muzzy_decay_ms_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	return arenas_decay_ms_ctl_impl(tsd, mib, miblen, oldp, oldlenp, newp,
	    newlen, false);
}

CTL_RO_NL_GEN(arenas_quantum, QUANTUM, size_t)
CTL_RO_NL_GEN(arenas_page, PAGE, size_t)
CTL_RO_NL_GEN(arenas_tcache_max, tcache_maxclass, size_t)
CTL_RO_NL_GEN(arenas_nbins, NBINS, unsigned)
CTL_RO_NL_GEN(arenas_nhbins, nhbins, unsigned)
CTL_RO_NL_GEN(arenas_bin_i_size, bin_infos[mib[2]].reg_size, size_t)
CTL_RO_NL_GEN(arenas_bin_i_nregs, bin_infos[mib[2]].nregs, uint32_t)
CTL_RO_NL_GEN(arenas_bin_i_slab_size, bin_infos[mib[2]].slab_size, size_t)
static const ctl_named_node_t *
arenas_bin_i_index(tsdn_t *tsdn, const size_t *mib, size_t miblen, size_t i) {
	if (i > NBINS) {
		return NULL;
	}
	return super_arenas_bin_i_node;
}

CTL_RO_NL_GEN(arenas_nlextents, NSIZES - NBINS, unsigned)
CTL_RO_NL_GEN(arenas_lextent_i_size, sz_index2size(NBINS+(szind_t)mib[2]),
    size_t)
static const ctl_named_node_t *
arenas_lextent_i_index(tsdn_t *tsdn, const size_t *mib, size_t miblen,
    size_t i) {
	if (i > NSIZES - NBINS) {
		return NULL;
	}
	return super_arenas_lextent_i_node;
}

static int
arenas_create_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	extent_hooks_t *extent_hooks;
	unsigned arena_ind;

	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);

	extent_hooks = (extent_hooks_t *)&extent_hooks_default;
	WRITE(extent_hooks, extent_hooks_t *);
	if ((arena_ind = ctl_arena_init(tsd, extent_hooks)) == UINT_MAX) {
		ret = EAGAIN;
		goto label_return;
	}
	READ(arena_ind, unsigned);

	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

static int
arenas_lookup_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	unsigned arena_ind;
	void *ptr;
	extent_t *extent;
	arena_t *arena;

	ptr = NULL;
	ret = EINVAL;
	malloc_mutex_lock(tsd_tsdn(tsd), &ctl_mtx);
	WRITE(ptr, void *);
	extent = iealloc(tsd_tsdn(tsd), ptr);
	if (extent == NULL)
		goto label_return;

	arena = extent_arena_get(extent);
	if (arena == NULL)
		goto label_return;

	arena_ind = arena_ind_get(arena);
	READ(arena_ind, unsigned);

	ret = 0;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &ctl_mtx);
	return ret;
}

/******************************************************************************/

static int
prof_thread_active_init_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	bool oldval;

	if (!config_prof) {
		return ENOENT;
	}

	if (newp != NULL) {
		if (newlen != sizeof(bool)) {
			ret = EINVAL;
			goto label_return;
		}
		oldval = prof_thread_active_init_set(tsd_tsdn(tsd),
		    *(bool *)newp);
	} else {
		oldval = prof_thread_active_init_get(tsd_tsdn(tsd));
	}
	READ(oldval, bool);

	ret = 0;
label_return:
	return ret;
}

static int
prof_active_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	bool oldval;

	if (!config_prof) {
		return ENOENT;
	}

	if (newp != NULL) {
		if (newlen != sizeof(bool)) {
			ret = EINVAL;
			goto label_return;
		}
		oldval = prof_active_set(tsd_tsdn(tsd), *(bool *)newp);
	} else {
		oldval = prof_active_get(tsd_tsdn(tsd));
	}
	READ(oldval, bool);

	ret = 0;
label_return:
	return ret;
}

static int
prof_dump_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	const char *filename = NULL;

	if (!config_prof) {
		return ENOENT;
	}

	WRITEONLY();
	WRITE(filename, const char *);

	if (prof_mdump(tsd, filename)) {
		ret = EFAULT;
		goto label_return;
	}

	ret = 0;
label_return:
	return ret;
}

static int
prof_gdump_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	bool oldval;

	if (!config_prof) {
		return ENOENT;
	}

	if (newp != NULL) {
		if (newlen != sizeof(bool)) {
			ret = EINVAL;
			goto label_return;
		}
		oldval = prof_gdump_set(tsd_tsdn(tsd), *(bool *)newp);
	} else {
		oldval = prof_gdump_get(tsd_tsdn(tsd));
	}
	READ(oldval, bool);

	ret = 0;
label_return:
	return ret;
}

static int
prof_reset_ctl(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
	int ret;
	size_t lg_sample = lg_prof_sample;

	if (!config_prof) {
		return ENOENT;
	}

	WRITEONLY();
	WRITE(lg_sample, size_t);
	if (lg_sample >= (sizeof(uint64_t) << 3)) {
		lg_sample = (sizeof(uint64_t) << 3) - 1;
	}

	prof_reset(tsd, lg_sample);

	ret = 0;
label_return:
	return ret;
}

CTL_RO_NL_CGEN(config_prof, prof_interval, prof_interval, uint64_t)
CTL_RO_NL_CGEN(config_prof, lg_prof_sample, lg_prof_sample, size_t)

/******************************************************************************/

CTL_RO_CGEN(config_stats, stats_allocated, ctl_stats->allocated, size_t)
CTL_RO_CGEN(config_stats, stats_active, ctl_stats->active, size_t)
CTL_RO_CGEN(config_stats, stats_metadata, ctl_stats->metadata, size_t)
CTL_RO_CGEN(config_stats, stats_metadata_thp, ctl_stats->metadata_thp, size_t)
CTL_RO_CGEN(config_stats, stats_resident, ctl_stats->resident, size_t)
CTL_RO_CGEN(config_stats, stats_mapped, ctl_stats->mapped, size_t)
CTL_RO_CGEN(config_stats, stats_retained, ctl_stats->retained, size_t)

CTL_RO_CGEN(config_stats, stats_background_thread_num_threads,
    ctl_stats->background_thread.num_threads, size_t)
CTL_RO_CGEN(config_stats, stats_background_thread_num_runs,
    ctl_stats->background_thread.num_runs, uint64_t)
CTL_RO_CGEN(config_stats, stats_background_thread_run_interval,
    nstime_ns(&ctl_stats->background_thread.run_interval), uint64_t)

CTL_RO_GEN(stats_arenas_i_dss, arenas_i(mib[2])->dss, const char *)
CTL_RO_GEN(stats_arenas_i_dirty_decay_ms, arenas_i(mib[2])->dirty_decay_ms,
    ssize_t)
CTL_RO_GEN(stats_arenas_i_muzzy_decay_ms, arenas_i(mib[2])->muzzy_decay_ms,
    ssize_t)
CTL_RO_GEN(stats_arenas_i_nthreads, arenas_i(mib[2])->nthreads, unsigned)
CTL_RO_GEN(stats_arenas_i_uptime,
    nstime_ns(&arenas_i(mib[2])->astats->astats.uptime), uint64_t)
CTL_RO_GEN(stats_arenas_i_pactive, arenas_i(mib[2])->pactive, size_t)
CTL_RO_GEN(stats_arenas_i_pdirty, arenas_i(mib[2])->pdirty, size_t)
CTL_RO_GEN(stats_arenas_i_pmuzzy, arenas_i(mib[2])->pmuzzy, size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_mapped,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.mapped, ATOMIC_RELAXED),
    size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_retained,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.retained, ATOMIC_RELAXED),
    size_t)

CTL_RO_CGEN(config_stats, stats_arenas_i_dirty_npurge,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.decay_dirty.npurge), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_dirty_nmadvise,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.decay_dirty.nmadvise), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_dirty_purged,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.decay_dirty.purged), uint64_t)

CTL_RO_CGEN(config_stats, stats_arenas_i_muzzy_npurge,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.decay_muzzy.npurge), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_muzzy_nmadvise,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.decay_muzzy.nmadvise), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_muzzy_purged,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.decay_muzzy.purged), uint64_t)

CTL_RO_CGEN(config_stats, stats_arenas_i_base,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.base, ATOMIC_RELAXED),
    size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_internal,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.internal, ATOMIC_RELAXED),
    size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_metadata_thp,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.metadata_thp,
    ATOMIC_RELAXED), size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_tcache_bytes,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.tcache_bytes,
    ATOMIC_RELAXED), size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_resident,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.resident, ATOMIC_RELAXED),
    size_t)

CTL_RO_CGEN(config_stats, stats_arenas_i_small_allocated,
    arenas_i(mib[2])->astats->allocated_small, size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_small_nmalloc,
    arenas_i(mib[2])->astats->nmalloc_small, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_small_ndalloc,
    arenas_i(mib[2])->astats->ndalloc_small, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_small_nrequests,
    arenas_i(mib[2])->astats->nrequests_small, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_large_allocated,
    atomic_load_zu(&arenas_i(mib[2])->astats->astats.allocated_large,
    ATOMIC_RELAXED), size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_large_nmalloc,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.nmalloc_large), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_large_ndalloc,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.ndalloc_large), uint64_t)
/*
 * Note: "nmalloc" here instead of "nrequests" in the read.  This is intentional.
 */
CTL_RO_CGEN(config_stats, stats_arenas_i_large_nrequests,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->astats.nmalloc_large), uint64_t) /* Intentional. */

/* Lock profiling related APIs below. */
#define RO_MUTEX_CTL_GEN(n, l)						\
CTL_RO_CGEN(config_stats, stats_##n##_num_ops,				\
    l.n_lock_ops, uint64_t)						\
CTL_RO_CGEN(config_stats, stats_##n##_num_wait,				\
    l.n_wait_times, uint64_t)						\
CTL_RO_CGEN(config_stats, stats_##n##_num_spin_acq,			\
    l.n_spin_acquired, uint64_t)					\
CTL_RO_CGEN(config_stats, stats_##n##_num_owner_switch,			\
    l.n_owner_switches, uint64_t) 					\
CTL_RO_CGEN(config_stats, stats_##n##_total_wait_time,			\
    nstime_ns(&l.tot_wait_time), uint64_t)				\
CTL_RO_CGEN(config_stats, stats_##n##_max_wait_time,			\
    nstime_ns(&l.max_wait_time), uint64_t)				\
CTL_RO_CGEN(config_stats, stats_##n##_max_num_thds,			\
    l.max_n_thds, uint32_t)

/* Global mutexes. */
#define OP(mtx)								\
    RO_MUTEX_CTL_GEN(mutexes_##mtx,					\
        ctl_stats->mutex_prof_data[global_prof_mutex_##mtx])
MUTEX_PROF_GLOBAL_MUTEXES
#undef OP

/* Per arena mutexes */
#define OP(mtx) RO_MUTEX_CTL_GEN(arenas_i_mutexes_##mtx,		\
    arenas_i(mib[2])->astats->astats.mutex_prof_data[arena_prof_mutex_##mtx])
MUTEX_PROF_ARENA_MUTEXES
#undef OP

/* tcache bin mutex */
RO_MUTEX_CTL_GEN(arenas_i_bins_j_mutex,
    arenas_i(mib[2])->astats->bstats[mib[4]].mutex_data)
#undef RO_MUTEX_CTL_GEN

/* Resets all mutex stats, including global, arena and bin mutexes. */
static int
stats_mutexes_reset_ctl(tsd_t *tsd, const size_t *mib, size_t miblen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
	if (!config_stats) {
		return ENOENT;
	}

	tsdn_t *tsdn = tsd_tsdn(tsd);

#define MUTEX_PROF_RESET(mtx)						\
    malloc_mutex_lock(tsdn, &mtx);					\
    malloc_mutex_prof_data_reset(tsdn, &mtx);				\
    malloc_mutex_unlock(tsdn, &mtx);

	/* Global mutexes: ctl and prof. */
	MUTEX_PROF_RESET(ctl_mtx);
	if (have_background_thread) {
		MUTEX_PROF_RESET(background_thread_lock);
	}
	if (config_prof && opt_prof) {
		MUTEX_PROF_RESET(bt2gctx_mtx);
	}


	/* Per arena mutexes. */
	unsigned n = narenas_total_get();

	for (unsigned i = 0; i < n; i++) {
		arena_t *arena = arena_get(tsdn, i, false);
		if (!arena) {
			continue;
		}
		MUTEX_PROF_RESET(arena->large_mtx);
		MUTEX_PROF_RESET(arena->extent_avail_mtx);
		MUTEX_PROF_RESET(arena->extents_dirty.mtx);
		MUTEX_PROF_RESET(arena->extents_muzzy.mtx);
		MUTEX_PROF_RESET(arena->extents_retained.mtx);
		MUTEX_PROF_RESET(arena->decay_dirty.mtx);
		MUTEX_PROF_RESET(arena->decay_muzzy.mtx);
		MUTEX_PROF_RESET(arena->tcache_ql_mtx);
		MUTEX_PROF_RESET(arena->base->mtx);

		for (szind_t i = 0; i < NBINS; i++) {
			bin_t *bin = &arena->bins[i];
			MUTEX_PROF_RESET(bin->lock);
		}
	}
#undef MUTEX_PROF_RESET
	return 0;
}

CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_nmalloc,
    arenas_i(mib[2])->astats->bstats[mib[4]].nmalloc, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_ndalloc,
    arenas_i(mib[2])->astats->bstats[mib[4]].ndalloc, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_nrequests,
    arenas_i(mib[2])->astats->bstats[mib[4]].nrequests, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_curregs,
    arenas_i(mib[2])->astats->bstats[mib[4]].curregs, size_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_nfills,
    arenas_i(mib[2])->astats->bstats[mib[4]].nfills, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_nflushes,
    arenas_i(mib[2])->astats->bstats[mib[4]].nflushes, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_nslabs,
    arenas_i(mib[2])->astats->bstats[mib[4]].nslabs, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_nreslabs,
    arenas_i(mib[2])->astats->bstats[mib[4]].reslabs, uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_bins_j_curslabs,
    arenas_i(mib[2])->astats->bstats[mib[4]].curslabs, size_t)

static const ctl_named_node_t *
stats_arenas_i_bins_j_index(tsdn_t *tsdn, const size_t *mib, size_t miblen,
    size_t j) {
	if (j > NBINS) {
		return NULL;
	}
	return super_stats_arenas_i_bins_j_node;
}

CTL_RO_CGEN(config_stats, stats_arenas_i_lextents_j_nmalloc,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->lstats[mib[4]].nmalloc), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_lextents_j_ndalloc,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->lstats[mib[4]].ndalloc), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_lextents_j_nrequests,
    ctl_arena_stats_read_u64(
    &arenas_i(mib[2])->astats->lstats[mib[4]].nrequests), uint64_t)
CTL_RO_CGEN(config_stats, stats_arenas_i_lextents_j_curlextents,
    arenas_i(mib[2])->astats->lstats[mib[4]].curlextents, size_t)

static const ctl_named_node_t *
stats_arenas_i_lextents_j_index(tsdn_t *tsdn, const size_t *mib, size_t miblen,
    size_t j) {
	if (j > NSIZES - NBINS) {
		return NULL;
	}
	return super_stats_arenas_i_lextents_j_node;
}

static const ctl_named_node_t *
stats_arenas_i_index(tsdn_t *tsdn, const size_t *mib, size_t miblen, size_t i) {
	const ctl_named_node_t *ret;
	size_t a;

	malloc_mutex_lock(tsdn, &ctl_mtx);
	a = arenas_i2a_impl(i, true, true);
	if (a == UINT_MAX || !ctl_arenas->arenas[a]->initialized) {
		ret = NULL;
		goto label_return;
	}

	ret = super_stats_arenas_i_node;
label_return:
	malloc_mutex_unlock(tsdn, &ctl_mtx);
	return ret;
}
