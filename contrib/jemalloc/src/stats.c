#define JEMALLOC_STATS_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/ctl.h"
#include "jemalloc/internal/emitter.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/mutex_prof.h"

const char *global_mutex_names[mutex_prof_num_global_mutexes] = {
#define OP(mtx) #mtx,
	MUTEX_PROF_GLOBAL_MUTEXES
#undef OP
};

const char *arena_mutex_names[mutex_prof_num_arena_mutexes] = {
#define OP(mtx) #mtx,
	MUTEX_PROF_ARENA_MUTEXES
#undef OP
};

#define CTL_GET(n, v, t) do {						\
	size_t sz = sizeof(t);						\
	xmallctl(n, (void *)v, &sz, NULL, 0);				\
} while (0)

#define CTL_M2_GET(n, i, v, t) do {					\
	size_t mib[CTL_MAX_DEPTH];					\
	size_t miblen = sizeof(mib) / sizeof(size_t);			\
	size_t sz = sizeof(t);						\
	xmallctlnametomib(n, mib, &miblen);				\
	mib[2] = (i);							\
	xmallctlbymib(mib, miblen, (void *)v, &sz, NULL, 0);		\
} while (0)

#define CTL_M2_M4_GET(n, i, j, v, t) do {				\
	size_t mib[CTL_MAX_DEPTH];					\
	size_t miblen = sizeof(mib) / sizeof(size_t);			\
	size_t sz = sizeof(t);						\
	xmallctlnametomib(n, mib, &miblen);				\
	mib[2] = (i);							\
	mib[4] = (j);							\
	xmallctlbymib(mib, miblen, (void *)v, &sz, NULL, 0);		\
} while (0)

/******************************************************************************/
/* Data. */

bool opt_stats_print = false;
char opt_stats_print_opts[stats_print_tot_num_options+1] = "";

/******************************************************************************/

/* Calculate x.yyy and output a string (takes a fixed sized char array). */
static bool
get_rate_str(uint64_t dividend, uint64_t divisor, char str[6]) {
	if (divisor == 0 || dividend > divisor) {
		/* The rate is not supposed to be greater than 1. */
		return true;
	}
	if (dividend > 0) {
		assert(UINT64_MAX / dividend >= 1000);
	}

	unsigned n = (unsigned)((dividend * 1000) / divisor);
	if (n < 10) {
		malloc_snprintf(str, 6, "0.00%u", n);
	} else if (n < 100) {
		malloc_snprintf(str, 6, "0.0%u", n);
	} else if (n < 1000) {
		malloc_snprintf(str, 6, "0.%u", n);
	} else {
		malloc_snprintf(str, 6, "1");
	}

	return false;
}

#define MUTEX_CTL_STR_MAX_LENGTH 128
static void
gen_mutex_ctl_str(char *str, size_t buf_len, const char *prefix,
    const char *mutex, const char *counter) {
	malloc_snprintf(str, buf_len, "stats.%s.%s.%s", prefix, mutex, counter);
}

static void
mutex_stats_init_cols(emitter_row_t *row, const char *table_name,
    emitter_col_t *name,
    emitter_col_t col_uint64_t[mutex_prof_num_uint64_t_counters],
    emitter_col_t col_uint32_t[mutex_prof_num_uint32_t_counters]) {
	mutex_prof_uint64_t_counter_ind_t k_uint64_t = 0;
	mutex_prof_uint32_t_counter_ind_t k_uint32_t = 0;

	emitter_col_t *col;

	if (name != NULL) {
		emitter_col_init(name, row);
		name->justify = emitter_justify_left;
		name->width = 21;
		name->type = emitter_type_title;
		name->str_val = table_name;
	}

#define WIDTH_uint32_t 12
#define WIDTH_uint64_t 16
#define OP(counter, counter_type, human)				\
	col = &col_##counter_type[k_##counter_type];			\
	++k_##counter_type;						\
	emitter_col_init(col, row);					\
	col->justify = emitter_justify_right;				\
	col->width = WIDTH_##counter_type;				\
	col->type = emitter_type_title;					\
	col->str_val = human;
	MUTEX_PROF_COUNTERS
#undef OP
#undef WIDTH_uint32_t
#undef WIDTH_uint64_t
}

static void
mutex_stats_read_global(const char *name, emitter_col_t *col_name,
    emitter_col_t col_uint64_t[mutex_prof_num_uint64_t_counters],
    emitter_col_t col_uint32_t[mutex_prof_num_uint32_t_counters]) {
	char cmd[MUTEX_CTL_STR_MAX_LENGTH];

	col_name->str_val = name;

	emitter_col_t *dst;
#define EMITTER_TYPE_uint32_t emitter_type_uint32
#define EMITTER_TYPE_uint64_t emitter_type_uint64
#define OP(counter, counter_type, human)				\
	dst = &col_##counter_type[mutex_counter_##counter];		\
	dst->type = EMITTER_TYPE_##counter_type;			\
	gen_mutex_ctl_str(cmd, MUTEX_CTL_STR_MAX_LENGTH,		\
	    "mutexes", name, #counter);					\
	CTL_GET(cmd, (counter_type *)&dst->bool_val, counter_type);
	MUTEX_PROF_COUNTERS
#undef OP
#undef EMITTER_TYPE_uint32_t
#undef EMITTER_TYPE_uint64_t
}

static void
mutex_stats_read_arena(unsigned arena_ind, mutex_prof_arena_ind_t mutex_ind,
    const char *name, emitter_col_t *col_name,
    emitter_col_t col_uint64_t[mutex_prof_num_uint64_t_counters],
    emitter_col_t col_uint32_t[mutex_prof_num_uint32_t_counters]) {
	char cmd[MUTEX_CTL_STR_MAX_LENGTH];

	col_name->str_val = name;

	emitter_col_t *dst;
#define EMITTER_TYPE_uint32_t emitter_type_uint32
#define EMITTER_TYPE_uint64_t emitter_type_uint64
#define OP(counter, counter_type, human)				\
	dst = &col_##counter_type[mutex_counter_##counter];		\
	dst->type = EMITTER_TYPE_##counter_type;			\
	gen_mutex_ctl_str(cmd, MUTEX_CTL_STR_MAX_LENGTH,		\
	    "arenas.0.mutexes",	arena_mutex_names[mutex_ind], #counter);\
	CTL_M2_GET(cmd, arena_ind,					\
	    (counter_type *)&dst->bool_val, counter_type);
	MUTEX_PROF_COUNTERS
#undef OP
#undef EMITTER_TYPE_uint32_t
#undef EMITTER_TYPE_uint64_t
}

static void
mutex_stats_read_arena_bin(unsigned arena_ind, unsigned bin_ind,
    emitter_col_t col_uint64_t[mutex_prof_num_uint64_t_counters],
    emitter_col_t col_uint32_t[mutex_prof_num_uint32_t_counters]) {
	char cmd[MUTEX_CTL_STR_MAX_LENGTH];
	emitter_col_t *dst;

#define EMITTER_TYPE_uint32_t emitter_type_uint32
#define EMITTER_TYPE_uint64_t emitter_type_uint64
#define OP(counter, counter_type, human)				\
	dst = &col_##counter_type[mutex_counter_##counter];		\
	dst->type = EMITTER_TYPE_##counter_type;			\
	gen_mutex_ctl_str(cmd, MUTEX_CTL_STR_MAX_LENGTH,		\
	    "arenas.0.bins.0","mutex", #counter);			\
	CTL_M2_M4_GET(cmd, arena_ind, bin_ind,				\
	    (counter_type *)&dst->bool_val, counter_type);
	MUTEX_PROF_COUNTERS
#undef OP
#undef EMITTER_TYPE_uint32_t
#undef EMITTER_TYPE_uint64_t
}

/* "row" can be NULL to avoid emitting in table mode. */
static void
mutex_stats_emit(emitter_t *emitter, emitter_row_t *row,
    emitter_col_t col_uint64_t[mutex_prof_num_uint64_t_counters],
    emitter_col_t col_uint32_t[mutex_prof_num_uint32_t_counters]) {
	if (row != NULL) {
		emitter_table_row(emitter, row);
	}

	mutex_prof_uint64_t_counter_ind_t k_uint64_t = 0;
	mutex_prof_uint32_t_counter_ind_t k_uint32_t = 0;

	emitter_col_t *col;

#define EMITTER_TYPE_uint32_t emitter_type_uint32
#define EMITTER_TYPE_uint64_t emitter_type_uint64
#define OP(counter, type, human)					\
	col = &col_##type[k_##type];						\
	++k_##type;							\
	emitter_json_kv(emitter, #counter, EMITTER_TYPE_##type,		\
	    (const void *)&col->bool_val);
	MUTEX_PROF_COUNTERS;
#undef OP
#undef EMITTER_TYPE_uint32_t
#undef EMITTER_TYPE_uint64_t
}

static void
stats_arena_bins_print(emitter_t *emitter, bool mutex, unsigned i) {
	size_t page;
	bool in_gap, in_gap_prev;
	unsigned nbins, j;

	CTL_GET("arenas.page", &page, size_t);

	CTL_GET("arenas.nbins", &nbins, unsigned);

	emitter_row_t header_row;
	emitter_row_init(&header_row);

	emitter_row_t row;
	emitter_row_init(&row);
#define COL(name, left_or_right, col_width, etype)			\
	emitter_col_t col_##name;					\
	emitter_col_init(&col_##name, &row);				\
	col_##name.justify = emitter_justify_##left_or_right;		\
	col_##name.width = col_width;					\
	col_##name.type = emitter_type_##etype;				\
	emitter_col_t header_col_##name;				\
	emitter_col_init(&header_col_##name, &header_row);		\
	header_col_##name.justify = emitter_justify_##left_or_right;	\
	header_col_##name.width = col_width;				\
	header_col_##name.type = emitter_type_title;			\
	header_col_##name.str_val = #name;

	COL(size, right, 20, size)
	COL(ind, right, 4, unsigned)
	COL(allocated, right, 13, uint64)
	COL(nmalloc, right, 13, uint64)
	COL(ndalloc, right, 13, uint64)
	COL(nrequests, right, 13, uint64)
	COL(curregs, right, 13, size)
	COL(curslabs, right, 13, size)
	COL(regs, right, 5, unsigned)
	COL(pgs, right, 4, size)
	/* To buffer a right- and left-justified column. */
	COL(justify_spacer, right, 1, title)
	COL(util, right, 6, title)
	COL(nfills, right, 13, uint64)
	COL(nflushes, right, 13, uint64)
	COL(nslabs, right, 13, uint64)
	COL(nreslabs, right, 13, uint64)
#undef COL

	/* Don't want to actually print the name. */
	header_col_justify_spacer.str_val = " ";
	col_justify_spacer.str_val = " ";


	emitter_col_t col_mutex64[mutex_prof_num_uint64_t_counters];
	emitter_col_t col_mutex32[mutex_prof_num_uint32_t_counters];

	emitter_col_t header_mutex64[mutex_prof_num_uint64_t_counters];
	emitter_col_t header_mutex32[mutex_prof_num_uint32_t_counters];

	if (mutex) {
		mutex_stats_init_cols(&row, NULL, NULL, col_mutex64,
		    col_mutex32);
		mutex_stats_init_cols(&header_row, NULL, NULL, header_mutex64,
		    header_mutex32);
	}

	/*
	 * We print a "bins:" header as part of the table row; we need to adjust
	 * the header size column to compensate.
	 */
	header_col_size.width -=5;
	emitter_table_printf(emitter, "bins:");
	emitter_table_row(emitter, &header_row);
	emitter_json_arr_begin(emitter, "bins");

	for (j = 0, in_gap = false; j < nbins; j++) {
		uint64_t nslabs;
		size_t reg_size, slab_size, curregs;
		size_t curslabs;
		uint32_t nregs;
		uint64_t nmalloc, ndalloc, nrequests, nfills, nflushes;
		uint64_t nreslabs;

		CTL_M2_M4_GET("stats.arenas.0.bins.0.nslabs", i, j, &nslabs,
		    uint64_t);
		in_gap_prev = in_gap;
		in_gap = (nslabs == 0);

		if (in_gap_prev && !in_gap) {
			emitter_table_printf(emitter,
			    "                     ---\n");
		}

		CTL_M2_GET("arenas.bin.0.size", j, &reg_size, size_t);
		CTL_M2_GET("arenas.bin.0.nregs", j, &nregs, uint32_t);
		CTL_M2_GET("arenas.bin.0.slab_size", j, &slab_size, size_t);

		CTL_M2_M4_GET("stats.arenas.0.bins.0.nmalloc", i, j, &nmalloc,
		    uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.ndalloc", i, j, &ndalloc,
		    uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.curregs", i, j, &curregs,
		    size_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.nrequests", i, j,
		    &nrequests, uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.nfills", i, j, &nfills,
		    uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.nflushes", i, j, &nflushes,
		    uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.nreslabs", i, j, &nreslabs,
		    uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.bins.0.curslabs", i, j, &curslabs,
		    size_t);

		if (mutex) {
			mutex_stats_read_arena_bin(i, j, col_mutex64,
			    col_mutex32);
		}

		emitter_json_arr_obj_begin(emitter);
		emitter_json_kv(emitter, "nmalloc", emitter_type_uint64,
		    &nmalloc);
		emitter_json_kv(emitter, "ndalloc", emitter_type_uint64,
		    &ndalloc);
		emitter_json_kv(emitter, "curregs", emitter_type_size,
		    &curregs);
		emitter_json_kv(emitter, "nrequests", emitter_type_uint64,
		    &nrequests);
		emitter_json_kv(emitter, "nfills", emitter_type_uint64,
		    &nfills);
		emitter_json_kv(emitter, "nflushes", emitter_type_uint64,
		    &nflushes);
		emitter_json_kv(emitter, "nreslabs", emitter_type_uint64,
		    &nreslabs);
		emitter_json_kv(emitter, "curslabs", emitter_type_size,
		    &curslabs);
		if (mutex) {
			emitter_json_dict_begin(emitter, "mutex");
			mutex_stats_emit(emitter, NULL, col_mutex64,
			    col_mutex32);
			emitter_json_dict_end(emitter);
		}
		emitter_json_arr_obj_end(emitter);

		size_t availregs = nregs * curslabs;
		char util[6];
		if (get_rate_str((uint64_t)curregs, (uint64_t)availregs, util))
		{
			if (availregs == 0) {
				malloc_snprintf(util, sizeof(util), "1");
			} else if (curregs > availregs) {
				/*
				 * Race detected: the counters were read in
				 * separate mallctl calls and concurrent
				 * operations happened in between.  In this case
				 * no meaningful utilization can be computed.
				 */
				malloc_snprintf(util, sizeof(util), " race");
			} else {
				not_reached();
			}
		}

		col_size.size_val = reg_size;
		col_ind.unsigned_val = j;
		col_allocated.size_val = curregs * reg_size;
		col_nmalloc.uint64_val = nmalloc;
		col_ndalloc.uint64_val = ndalloc;
		col_nrequests.uint64_val = nrequests;
		col_curregs.size_val = curregs;
		col_curslabs.size_val = curslabs;
		col_regs.unsigned_val = nregs;
		col_pgs.size_val = slab_size / page;
		col_util.str_val = util;
		col_nfills.uint64_val = nfills;
		col_nflushes.uint64_val = nflushes;
		col_nslabs.uint64_val = nslabs;
		col_nreslabs.uint64_val = nreslabs;

		/*
		 * Note that mutex columns were initialized above, if mutex ==
		 * true.
		 */

		emitter_table_row(emitter, &row);
	}
	emitter_json_arr_end(emitter); /* Close "bins". */

	if (in_gap) {
		emitter_table_printf(emitter, "                     ---\n");
	}
}

static void
stats_arena_lextents_print(emitter_t *emitter, unsigned i) {
	unsigned nbins, nlextents, j;
	bool in_gap, in_gap_prev;

	CTL_GET("arenas.nbins", &nbins, unsigned);
	CTL_GET("arenas.nlextents", &nlextents, unsigned);

	emitter_row_t header_row;
	emitter_row_init(&header_row);
	emitter_row_t row;
	emitter_row_init(&row);

#define COL(name, left_or_right, col_width, etype)			\
	emitter_col_t header_##name;					\
	emitter_col_init(&header_##name, &header_row);			\
	header_##name.justify = emitter_justify_##left_or_right;	\
	header_##name.width = col_width;				\
	header_##name.type = emitter_type_title;			\
	header_##name.str_val = #name;					\
									\
	emitter_col_t col_##name;					\
	emitter_col_init(&col_##name, &row);				\
	col_##name.justify = emitter_justify_##left_or_right;		\
	col_##name.width = col_width;					\
	col_##name.type = emitter_type_##etype;

	COL(size, right, 20, size)
	COL(ind, right, 4, unsigned)
	COL(allocated, right, 13, size)
	COL(nmalloc, right, 13, uint64)
	COL(ndalloc, right, 13, uint64)
	COL(nrequests, right, 13, uint64)
	COL(curlextents, right, 13, size)
#undef COL

	/* As with bins, we label the large extents table. */
	header_size.width -= 6;
	emitter_table_printf(emitter, "large:");
	emitter_table_row(emitter, &header_row);
	emitter_json_arr_begin(emitter, "lextents");

	for (j = 0, in_gap = false; j < nlextents; j++) {
		uint64_t nmalloc, ndalloc, nrequests;
		size_t lextent_size, curlextents;

		CTL_M2_M4_GET("stats.arenas.0.lextents.0.nmalloc", i, j,
		    &nmalloc, uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.lextents.0.ndalloc", i, j,
		    &ndalloc, uint64_t);
		CTL_M2_M4_GET("stats.arenas.0.lextents.0.nrequests", i, j,
		    &nrequests, uint64_t);
		in_gap_prev = in_gap;
		in_gap = (nrequests == 0);

		if (in_gap_prev && !in_gap) {
			emitter_table_printf(emitter,
			    "                     ---\n");
		}

		CTL_M2_GET("arenas.lextent.0.size", j, &lextent_size, size_t);
		CTL_M2_M4_GET("stats.arenas.0.lextents.0.curlextents", i, j,
		    &curlextents, size_t);

		emitter_json_arr_obj_begin(emitter);
		emitter_json_kv(emitter, "curlextents", emitter_type_size,
		    &curlextents);
		emitter_json_arr_obj_end(emitter);

		col_size.size_val = lextent_size;
		col_ind.unsigned_val = nbins + j;
		col_allocated.size_val = curlextents * lextent_size;
		col_nmalloc.uint64_val = nmalloc;
		col_ndalloc.uint64_val = ndalloc;
		col_nrequests.uint64_val = nrequests;
		col_curlextents.size_val = curlextents;

		if (!in_gap) {
			emitter_table_row(emitter, &row);
		}
	}
	emitter_json_arr_end(emitter); /* Close "lextents". */
	if (in_gap) {
		emitter_table_printf(emitter, "                     ---\n");
	}
}

static void
stats_arena_mutexes_print(emitter_t *emitter, unsigned arena_ind) {
	emitter_row_t row;
	emitter_col_t col_name;
	emitter_col_t col64[mutex_prof_num_uint64_t_counters];
	emitter_col_t col32[mutex_prof_num_uint32_t_counters];

	emitter_row_init(&row);
	mutex_stats_init_cols(&row, "", &col_name, col64, col32);

	emitter_json_dict_begin(emitter, "mutexes");
	emitter_table_row(emitter, &row);

	for (mutex_prof_arena_ind_t i = 0; i < mutex_prof_num_arena_mutexes;
	    i++) {
		const char *name = arena_mutex_names[i];
		emitter_json_dict_begin(emitter, name);
		mutex_stats_read_arena(arena_ind, i, name, &col_name, col64,
		    col32);
		mutex_stats_emit(emitter, &row, col64, col32);
		emitter_json_dict_end(emitter); /* Close the mutex dict. */
	}
	emitter_json_dict_end(emitter); /* End "mutexes". */
}

static void
stats_arena_print(emitter_t *emitter, unsigned i, bool bins, bool large,
    bool mutex) {
	unsigned nthreads;
	const char *dss;
	ssize_t dirty_decay_ms, muzzy_decay_ms;
	size_t page, pactive, pdirty, pmuzzy, mapped, retained;
	size_t base, internal, resident, metadata_thp;
	uint64_t dirty_npurge, dirty_nmadvise, dirty_purged;
	uint64_t muzzy_npurge, muzzy_nmadvise, muzzy_purged;
	size_t small_allocated;
	uint64_t small_nmalloc, small_ndalloc, small_nrequests;
	size_t large_allocated;
	uint64_t large_nmalloc, large_ndalloc, large_nrequests;
	size_t tcache_bytes;
	uint64_t uptime;

	CTL_GET("arenas.page", &page, size_t);

	CTL_M2_GET("stats.arenas.0.nthreads", i, &nthreads, unsigned);
	emitter_kv(emitter, "nthreads", "assigned threads",
	    emitter_type_unsigned, &nthreads);

	CTL_M2_GET("stats.arenas.0.uptime", i, &uptime, uint64_t);
	emitter_kv(emitter, "uptime_ns", "uptime", emitter_type_uint64,
	    &uptime);

	CTL_M2_GET("stats.arenas.0.dss", i, &dss, const char *);
	emitter_kv(emitter, "dss", "dss allocation precedence",
	    emitter_type_string, &dss);

	CTL_M2_GET("stats.arenas.0.dirty_decay_ms", i, &dirty_decay_ms,
	    ssize_t);
	CTL_M2_GET("stats.arenas.0.muzzy_decay_ms", i, &muzzy_decay_ms,
	    ssize_t);
	CTL_M2_GET("stats.arenas.0.pactive", i, &pactive, size_t);
	CTL_M2_GET("stats.arenas.0.pdirty", i, &pdirty, size_t);
	CTL_M2_GET("stats.arenas.0.pmuzzy", i, &pmuzzy, size_t);
	CTL_M2_GET("stats.arenas.0.dirty_npurge", i, &dirty_npurge, uint64_t);
	CTL_M2_GET("stats.arenas.0.dirty_nmadvise", i, &dirty_nmadvise,
	    uint64_t);
	CTL_M2_GET("stats.arenas.0.dirty_purged", i, &dirty_purged, uint64_t);
	CTL_M2_GET("stats.arenas.0.muzzy_npurge", i, &muzzy_npurge, uint64_t);
	CTL_M2_GET("stats.arenas.0.muzzy_nmadvise", i, &muzzy_nmadvise,
	    uint64_t);
	CTL_M2_GET("stats.arenas.0.muzzy_purged", i, &muzzy_purged, uint64_t);

	emitter_row_t decay_row;
	emitter_row_init(&decay_row);

	/* JSON-style emission. */
	emitter_json_kv(emitter, "dirty_decay_ms", emitter_type_ssize,
	    &dirty_decay_ms);
	emitter_json_kv(emitter, "muzzy_decay_ms", emitter_type_ssize,
	    &muzzy_decay_ms);

	emitter_json_kv(emitter, "pactive", emitter_type_size, &pactive);
	emitter_json_kv(emitter, "pdirty", emitter_type_size, &pdirty);
	emitter_json_kv(emitter, "pmuzzy", emitter_type_size, &pmuzzy);

	emitter_json_kv(emitter, "dirty_npurge", emitter_type_uint64,
	    &dirty_npurge);
	emitter_json_kv(emitter, "dirty_nmadvise", emitter_type_uint64,
	    &dirty_nmadvise);
	emitter_json_kv(emitter, "dirty_purged", emitter_type_uint64,
	    &dirty_purged);

	emitter_json_kv(emitter, "muzzy_npurge", emitter_type_uint64,
	    &muzzy_npurge);
	emitter_json_kv(emitter, "muzzy_nmadvise", emitter_type_uint64,
	    &muzzy_nmadvise);
	emitter_json_kv(emitter, "muzzy_purged", emitter_type_uint64,
	    &muzzy_purged);

	/* Table-style emission. */
	emitter_col_t decay_type;
	emitter_col_init(&decay_type, &decay_row);
	decay_type.justify = emitter_justify_right;
	decay_type.width = 9;
	decay_type.type = emitter_type_title;
	decay_type.str_val = "decaying:";

	emitter_col_t decay_time;
	emitter_col_init(&decay_time, &decay_row);
	decay_time.justify = emitter_justify_right;
	decay_time.width = 6;
	decay_time.type = emitter_type_title;
	decay_time.str_val = "time";

	emitter_col_t decay_npages;
	emitter_col_init(&decay_npages, &decay_row);
	decay_npages.justify = emitter_justify_right;
	decay_npages.width = 13;
	decay_npages.type = emitter_type_title;
	decay_npages.str_val = "npages";

	emitter_col_t decay_sweeps;
	emitter_col_init(&decay_sweeps, &decay_row);
	decay_sweeps.justify = emitter_justify_right;
	decay_sweeps.width = 13;
	decay_sweeps.type = emitter_type_title;
	decay_sweeps.str_val = "sweeps";

	emitter_col_t decay_madvises;
	emitter_col_init(&decay_madvises, &decay_row);
	decay_madvises.justify = emitter_justify_right;
	decay_madvises.width = 13;
	decay_madvises.type = emitter_type_title;
	decay_madvises.str_val = "madvises";

	emitter_col_t decay_purged;
	emitter_col_init(&decay_purged, &decay_row);
	decay_purged.justify = emitter_justify_right;
	decay_purged.width = 13;
	decay_purged.type = emitter_type_title;
	decay_purged.str_val = "purged";

	/* Title row. */
	emitter_table_row(emitter, &decay_row);

	/* Dirty row. */
	decay_type.str_val = "dirty:";

	if (dirty_decay_ms >= 0) {
		decay_time.type = emitter_type_ssize;
		decay_time.ssize_val = dirty_decay_ms;
	} else {
		decay_time.type = emitter_type_title;
		decay_time.str_val = "N/A";
	}

	decay_npages.type = emitter_type_size;
	decay_npages.size_val = pdirty;

	decay_sweeps.type = emitter_type_uint64;
	decay_sweeps.uint64_val = dirty_npurge;

	decay_madvises.type = emitter_type_uint64;
	decay_madvises.uint64_val = dirty_nmadvise;

	decay_purged.type = emitter_type_uint64;
	decay_purged.uint64_val = dirty_purged;

	emitter_table_row(emitter, &decay_row);

	/* Muzzy row. */
	decay_type.str_val = "muzzy:";

	if (muzzy_decay_ms >= 0) {
		decay_time.type = emitter_type_ssize;
		decay_time.ssize_val = muzzy_decay_ms;
	} else {
		decay_time.type = emitter_type_title;
		decay_time.str_val = "N/A";
	}

	decay_npages.type = emitter_type_size;
	decay_npages.size_val = pmuzzy;

	decay_sweeps.type = emitter_type_uint64;
	decay_sweeps.uint64_val = muzzy_npurge;

	decay_madvises.type = emitter_type_uint64;
	decay_madvises.uint64_val = muzzy_nmadvise;

	decay_purged.type = emitter_type_uint64;
	decay_purged.uint64_val = muzzy_purged;

	emitter_table_row(emitter, &decay_row);

	/* Small / large / total allocation counts. */
	emitter_row_t alloc_count_row;
	emitter_row_init(&alloc_count_row);

	emitter_col_t alloc_count_title;
	emitter_col_init(&alloc_count_title, &alloc_count_row);
	alloc_count_title.justify = emitter_justify_left;
	alloc_count_title.width = 25;
	alloc_count_title.type = emitter_type_title;
	alloc_count_title.str_val = "";

	emitter_col_t alloc_count_allocated;
	emitter_col_init(&alloc_count_allocated, &alloc_count_row);
	alloc_count_allocated.justify = emitter_justify_right;
	alloc_count_allocated.width = 12;
	alloc_count_allocated.type = emitter_type_title;
	alloc_count_allocated.str_val = "allocated";

	emitter_col_t alloc_count_nmalloc;
	emitter_col_init(&alloc_count_nmalloc, &alloc_count_row);
	alloc_count_nmalloc.justify = emitter_justify_right;
	alloc_count_nmalloc.width = 12;
	alloc_count_nmalloc.type = emitter_type_title;
	alloc_count_nmalloc.str_val = "nmalloc";

	emitter_col_t alloc_count_ndalloc;
	emitter_col_init(&alloc_count_ndalloc, &alloc_count_row);
	alloc_count_ndalloc.justify = emitter_justify_right;
	alloc_count_ndalloc.width = 12;
	alloc_count_ndalloc.type = emitter_type_title;
	alloc_count_ndalloc.str_val = "ndalloc";

	emitter_col_t alloc_count_nrequests;
	emitter_col_init(&alloc_count_nrequests, &alloc_count_row);
	alloc_count_nrequests.justify = emitter_justify_right;
	alloc_count_nrequests.width = 12;
	alloc_count_nrequests.type = emitter_type_title;
	alloc_count_nrequests.str_val = "nrequests";

	emitter_table_row(emitter, &alloc_count_row);

#define GET_AND_EMIT_ALLOC_STAT(small_or_large, name, valtype)		\
	CTL_M2_GET("stats.arenas.0." #small_or_large "." #name, i,	\
	    &small_or_large##_##name, valtype##_t);			\
	emitter_json_kv(emitter, #name, emitter_type_##valtype,		\
	    &small_or_large##_##name);					\
	alloc_count_##name.type = emitter_type_##valtype;		\
	alloc_count_##name.valtype##_val = small_or_large##_##name;

	emitter_json_dict_begin(emitter, "small");
	alloc_count_title.str_val = "small:";

	GET_AND_EMIT_ALLOC_STAT(small, allocated, size)
	GET_AND_EMIT_ALLOC_STAT(small, nmalloc, uint64)
	GET_AND_EMIT_ALLOC_STAT(small, ndalloc, uint64)
	GET_AND_EMIT_ALLOC_STAT(small, nrequests, uint64)

	emitter_table_row(emitter, &alloc_count_row);
	emitter_json_dict_end(emitter); /* Close "small". */

	emitter_json_dict_begin(emitter, "large");
	alloc_count_title.str_val = "large:";

	GET_AND_EMIT_ALLOC_STAT(large, allocated, size)
	GET_AND_EMIT_ALLOC_STAT(large, nmalloc, uint64)
	GET_AND_EMIT_ALLOC_STAT(large, ndalloc, uint64)
	GET_AND_EMIT_ALLOC_STAT(large, nrequests, uint64)

	emitter_table_row(emitter, &alloc_count_row);
	emitter_json_dict_end(emitter); /* Close "large". */

#undef GET_AND_EMIT_ALLOC_STAT

	/* Aggregated small + large stats are emitter only in table mode. */
	alloc_count_title.str_val = "total:";
	alloc_count_allocated.size_val = small_allocated + large_allocated;
	alloc_count_nmalloc.uint64_val = small_nmalloc + large_nmalloc;
	alloc_count_ndalloc.uint64_val = small_ndalloc + large_ndalloc;
	alloc_count_nrequests.uint64_val = small_nrequests + large_nrequests;
	emitter_table_row(emitter, &alloc_count_row);

	emitter_row_t mem_count_row;
	emitter_row_init(&mem_count_row);

	emitter_col_t mem_count_title;
	emitter_col_init(&mem_count_title, &mem_count_row);
	mem_count_title.justify = emitter_justify_left;
	mem_count_title.width = 25;
	mem_count_title.type = emitter_type_title;
	mem_count_title.str_val = "";

	emitter_col_t mem_count_val;
	emitter_col_init(&mem_count_val, &mem_count_row);
	mem_count_val.justify = emitter_justify_right;
	mem_count_val.width = 12;
	mem_count_val.type = emitter_type_title;
	mem_count_val.str_val = "";

	emitter_table_row(emitter, &mem_count_row);
	mem_count_val.type = emitter_type_size;

	/* Active count in bytes is emitted only in table mode. */
	mem_count_title.str_val = "active:";
	mem_count_val.size_val = pactive * page;
	emitter_table_row(emitter, &mem_count_row);

#define GET_AND_EMIT_MEM_STAT(stat)					\
	CTL_M2_GET("stats.arenas.0."#stat, i, &stat, size_t);		\
	emitter_json_kv(emitter, #stat, emitter_type_size, &stat);	\
	mem_count_title.str_val = #stat":";				\
	mem_count_val.size_val = stat;					\
	emitter_table_row(emitter, &mem_count_row);

	GET_AND_EMIT_MEM_STAT(mapped)
	GET_AND_EMIT_MEM_STAT(retained)
	GET_AND_EMIT_MEM_STAT(base)
	GET_AND_EMIT_MEM_STAT(internal)
	GET_AND_EMIT_MEM_STAT(metadata_thp)
	GET_AND_EMIT_MEM_STAT(tcache_bytes)
	GET_AND_EMIT_MEM_STAT(resident)
#undef GET_AND_EMIT_MEM_STAT

	if (mutex) {
		stats_arena_mutexes_print(emitter, i);
	}
	if (bins) {
		stats_arena_bins_print(emitter, mutex, i);
	}
	if (large) {
		stats_arena_lextents_print(emitter, i);
	}
}

static void
stats_general_print(emitter_t *emitter) {
	const char *cpv;
	bool bv, bv2;
	unsigned uv;
	uint32_t u32v;
	uint64_t u64v;
	ssize_t ssv, ssv2;
	size_t sv, bsz, usz, ssz, sssz, cpsz;

	bsz = sizeof(bool);
	usz = sizeof(unsigned);
	ssz = sizeof(size_t);
	sssz = sizeof(ssize_t);
	cpsz = sizeof(const char *);

	CTL_GET("version", &cpv, const char *);
	emitter_kv(emitter, "version", "Version", emitter_type_string, &cpv);

	/* config. */
	emitter_dict_begin(emitter, "config", "Build-time option settings");
#define CONFIG_WRITE_BOOL(name)						\
	do {								\
		CTL_GET("config."#name, &bv, bool);			\
		emitter_kv(emitter, #name, "config."#name,		\
		    emitter_type_bool, &bv);				\
	} while (0)

	CONFIG_WRITE_BOOL(cache_oblivious);
	CONFIG_WRITE_BOOL(debug);
	CONFIG_WRITE_BOOL(fill);
	CONFIG_WRITE_BOOL(lazy_lock);
	emitter_kv(emitter, "malloc_conf", "config.malloc_conf",
	    emitter_type_string, &config_malloc_conf);

	CONFIG_WRITE_BOOL(prof);
	CONFIG_WRITE_BOOL(prof_libgcc);
	CONFIG_WRITE_BOOL(prof_libunwind);
	CONFIG_WRITE_BOOL(stats);
	CONFIG_WRITE_BOOL(utrace);
	CONFIG_WRITE_BOOL(xmalloc);
#undef CONFIG_WRITE_BOOL
	emitter_dict_end(emitter); /* Close "config" dict. */

	/* opt. */
#define OPT_WRITE(name, var, size, emitter_type)			\
	if (je_mallctl("opt."name, (void *)&var, &size, NULL, 0) ==	\
	    0) {							\
		emitter_kv(emitter, name, "opt."name, emitter_type,	\
		    &var);						\
	}

#define OPT_WRITE_MUTABLE(name, var1, var2, size, emitter_type,		\
    altname)								\
	if (je_mallctl("opt."name, (void *)&var1, &size, NULL, 0) ==	\
	    0 && je_mallctl(altname, (void *)&var2, &size, NULL, 0)	\
	    == 0) {							\
		emitter_kv_note(emitter, name, "opt."name,		\
		    emitter_type, &var1, altname, emitter_type,		\
		    &var2);						\
	}

#define OPT_WRITE_BOOL(name) OPT_WRITE(name, bv, bsz, emitter_type_bool)
#define OPT_WRITE_BOOL_MUTABLE(name, altname)				\
	OPT_WRITE_MUTABLE(name, bv, bv2, bsz, emitter_type_bool, altname)

#define OPT_WRITE_UNSIGNED(name)					\
	OPT_WRITE(name, uv, usz, emitter_type_unsigned)

#define OPT_WRITE_SSIZE_T(name)						\
	OPT_WRITE(name, ssv, sssz, emitter_type_ssize)
#define OPT_WRITE_SSIZE_T_MUTABLE(name, altname)			\
	OPT_WRITE_MUTABLE(name, ssv, ssv2, sssz, emitter_type_ssize,	\
	    altname)

#define OPT_WRITE_CHAR_P(name)						\
	OPT_WRITE(name, cpv, cpsz, emitter_type_string)

	emitter_dict_begin(emitter, "opt", "Run-time option settings");

	OPT_WRITE_BOOL("abort")
	OPT_WRITE_BOOL("abort_conf")
	OPT_WRITE_BOOL("retain")
	OPT_WRITE_CHAR_P("dss")
	OPT_WRITE_UNSIGNED("narenas")
	OPT_WRITE_CHAR_P("percpu_arena")
	OPT_WRITE_CHAR_P("metadata_thp")
	OPT_WRITE_BOOL_MUTABLE("background_thread", "background_thread")
	OPT_WRITE_SSIZE_T_MUTABLE("dirty_decay_ms", "arenas.dirty_decay_ms")
	OPT_WRITE_SSIZE_T_MUTABLE("muzzy_decay_ms", "arenas.muzzy_decay_ms")
	OPT_WRITE_UNSIGNED("lg_extent_max_active_fit")
	OPT_WRITE_CHAR_P("junk")
	OPT_WRITE_BOOL("zero")
	OPT_WRITE_BOOL("utrace")
	OPT_WRITE_BOOL("xmalloc")
	OPT_WRITE_BOOL("tcache")
	OPT_WRITE_SSIZE_T("lg_tcache_max")
	OPT_WRITE_CHAR_P("thp")
	OPT_WRITE_BOOL("prof")
	OPT_WRITE_CHAR_P("prof_prefix")
	OPT_WRITE_BOOL_MUTABLE("prof_active", "prof.active")
	OPT_WRITE_BOOL_MUTABLE("prof_thread_active_init",
	    "prof.thread_active_init")
	OPT_WRITE_SSIZE_T_MUTABLE("lg_prof_sample", "prof.lg_sample")
	OPT_WRITE_BOOL("prof_accum")
	OPT_WRITE_SSIZE_T("lg_prof_interval")
	OPT_WRITE_BOOL("prof_gdump")
	OPT_WRITE_BOOL("prof_final")
	OPT_WRITE_BOOL("prof_leak")
	OPT_WRITE_BOOL("stats_print")
	OPT_WRITE_CHAR_P("stats_print_opts")

	emitter_dict_end(emitter);

#undef OPT_WRITE
#undef OPT_WRITE_MUTABLE
#undef OPT_WRITE_BOOL
#undef OPT_WRITE_BOOL_MUTABLE
#undef OPT_WRITE_UNSIGNED
#undef OPT_WRITE_SSIZE_T
#undef OPT_WRITE_SSIZE_T_MUTABLE
#undef OPT_WRITE_CHAR_P

	/* prof. */
	if (config_prof) {
		emitter_dict_begin(emitter, "prof", "Profiling settings");

		CTL_GET("prof.thread_active_init", &bv, bool);
		emitter_kv(emitter, "thread_active_init",
		    "prof.thread_active_init", emitter_type_bool, &bv);

		CTL_GET("prof.active", &bv, bool);
		emitter_kv(emitter, "active", "prof.active", emitter_type_bool,
		    &bv);

		CTL_GET("prof.gdump", &bv, bool);
		emitter_kv(emitter, "gdump", "prof.gdump", emitter_type_bool,
		    &bv);

		CTL_GET("prof.interval", &u64v, uint64_t);
		emitter_kv(emitter, "interval", "prof.interval",
		    emitter_type_uint64, &u64v);

		CTL_GET("prof.lg_sample", &ssv, ssize_t);
		emitter_kv(emitter, "lg_sample", "prof.lg_sample",
		    emitter_type_ssize, &ssv);

		emitter_dict_end(emitter); /* Close "prof". */
	}

	/* arenas. */
	/*
	 * The json output sticks arena info into an "arenas" dict; the table
	 * output puts them at the top-level.
	 */
	emitter_json_dict_begin(emitter, "arenas");

	CTL_GET("arenas.narenas", &uv, unsigned);
	emitter_kv(emitter, "narenas", "Arenas", emitter_type_unsigned, &uv);

	/*
	 * Decay settings are emitted only in json mode; in table mode, they're
	 * emitted as notes with the opt output, above.
	 */
	CTL_GET("arenas.dirty_decay_ms", &ssv, ssize_t);
	emitter_json_kv(emitter, "dirty_decay_ms", emitter_type_ssize, &ssv);

	CTL_GET("arenas.muzzy_decay_ms", &ssv, ssize_t);
	emitter_json_kv(emitter, "muzzy_decay_ms", emitter_type_ssize, &ssv);

	CTL_GET("arenas.quantum", &sv, size_t);
	emitter_kv(emitter, "quantum", "Quantum size", emitter_type_size, &sv);

	CTL_GET("arenas.page", &sv, size_t);
	emitter_kv(emitter, "page", "Page size", emitter_type_size, &sv);

	if (je_mallctl("arenas.tcache_max", (void *)&sv, &ssz, NULL, 0) == 0) {
		emitter_kv(emitter, "tcache_max",
		    "Maximum thread-cached size class", emitter_type_size, &sv);
	}

	unsigned nbins;
	CTL_GET("arenas.nbins", &nbins, unsigned);
	emitter_kv(emitter, "nbins", "Number of bin size classes",
	    emitter_type_unsigned, &nbins);

	unsigned nhbins;
	CTL_GET("arenas.nhbins", &nhbins, unsigned);
	emitter_kv(emitter, "nhbins", "Number of thread-cache bin size classes",
	    emitter_type_unsigned, &nhbins);

	/*
	 * We do enough mallctls in a loop that we actually want to omit them
	 * (not just omit the printing).
	 */
	if (emitter->output == emitter_output_json) {
		emitter_json_arr_begin(emitter, "bin");
		for (unsigned i = 0; i < nbins; i++) {
			emitter_json_arr_obj_begin(emitter);

			CTL_M2_GET("arenas.bin.0.size", i, &sv, size_t);
			emitter_json_kv(emitter, "size", emitter_type_size,
			    &sv);

			CTL_M2_GET("arenas.bin.0.nregs", i, &u32v, uint32_t);
			emitter_json_kv(emitter, "nregs", emitter_type_uint32,
			    &u32v);

			CTL_M2_GET("arenas.bin.0.slab_size", i, &sv, size_t);
			emitter_json_kv(emitter, "slab_size", emitter_type_size,
			    &sv);

			emitter_json_arr_obj_end(emitter);
		}
		emitter_json_arr_end(emitter); /* Close "bin". */
	}

	unsigned nlextents;
	CTL_GET("arenas.nlextents", &nlextents, unsigned);
	emitter_kv(emitter, "nlextents", "Number of large size classes",
	    emitter_type_unsigned, &nlextents);

	if (emitter->output == emitter_output_json) {
		emitter_json_arr_begin(emitter, "lextent");
		for (unsigned i = 0; i < nlextents; i++) {
			emitter_json_arr_obj_begin(emitter);

			CTL_M2_GET("arenas.lextent.0.size", i, &sv, size_t);
			emitter_json_kv(emitter, "size", emitter_type_size,
			    &sv);

			emitter_json_arr_obj_end(emitter);
		}
		emitter_json_arr_end(emitter); /* Close "lextent". */
	}

	emitter_json_dict_end(emitter); /* Close "arenas" */
}

static void
stats_print_helper(emitter_t *emitter, bool merged, bool destroyed,
    bool unmerged, bool bins, bool large, bool mutex) {
	/*
	 * These should be deleted.  We keep them around for a while, to aid in
	 * the transition to the emitter code.
	 */
	size_t allocated, active, metadata, metadata_thp, resident, mapped,
	    retained;
	size_t num_background_threads;
	uint64_t background_thread_num_runs, background_thread_run_interval;

	CTL_GET("stats.allocated", &allocated, size_t);
	CTL_GET("stats.active", &active, size_t);
	CTL_GET("stats.metadata", &metadata, size_t);
	CTL_GET("stats.metadata_thp", &metadata_thp, size_t);
	CTL_GET("stats.resident", &resident, size_t);
	CTL_GET("stats.mapped", &mapped, size_t);
	CTL_GET("stats.retained", &retained, size_t);

	if (have_background_thread) {
		CTL_GET("stats.background_thread.num_threads",
		    &num_background_threads, size_t);
		CTL_GET("stats.background_thread.num_runs",
		    &background_thread_num_runs, uint64_t);
		CTL_GET("stats.background_thread.run_interval",
		    &background_thread_run_interval, uint64_t);
	} else {
		num_background_threads = 0;
		background_thread_num_runs = 0;
		background_thread_run_interval = 0;
	}

	/* Generic global stats. */
	emitter_json_dict_begin(emitter, "stats");
	emitter_json_kv(emitter, "allocated", emitter_type_size, &allocated);
	emitter_json_kv(emitter, "active", emitter_type_size, &active);
	emitter_json_kv(emitter, "metadata", emitter_type_size, &metadata);
	emitter_json_kv(emitter, "metadata_thp", emitter_type_size,
	    &metadata_thp);
	emitter_json_kv(emitter, "resident", emitter_type_size, &resident);
	emitter_json_kv(emitter, "mapped", emitter_type_size, &mapped);
	emitter_json_kv(emitter, "retained", emitter_type_size, &retained);

	emitter_table_printf(emitter, "Allocated: %zu, active: %zu, "
	    "metadata: %zu (n_thp %zu), resident: %zu, mapped: %zu, "
	    "retained: %zu\n", allocated, active, metadata, metadata_thp,
	    resident, mapped, retained);

	/* Background thread stats. */
	emitter_json_dict_begin(emitter, "background_thread");
	emitter_json_kv(emitter, "num_threads", emitter_type_size,
	    &num_background_threads);
	emitter_json_kv(emitter, "num_runs", emitter_type_uint64,
	    &background_thread_num_runs);
	emitter_json_kv(emitter, "run_interval", emitter_type_uint64,
	    &background_thread_run_interval);
	emitter_json_dict_end(emitter); /* Close "background_thread". */

	emitter_table_printf(emitter, "Background threads: %zu, "
	    "num_runs: %"FMTu64", run_interval: %"FMTu64" ns\n",
	    num_background_threads, background_thread_num_runs,
	    background_thread_run_interval);

	if (mutex) {
		emitter_row_t row;
		emitter_col_t name;
		emitter_col_t col64[mutex_prof_num_uint64_t_counters];
		emitter_col_t col32[mutex_prof_num_uint32_t_counters];

		emitter_row_init(&row);
		mutex_stats_init_cols(&row, "", &name, col64, col32);

		emitter_table_row(emitter, &row);
		emitter_json_dict_begin(emitter, "mutexes");

		for (int i = 0; i < mutex_prof_num_global_mutexes; i++) {
			mutex_stats_read_global(global_mutex_names[i], &name,
			    col64, col32);
			emitter_json_dict_begin(emitter, global_mutex_names[i]);
			mutex_stats_emit(emitter, &row, col64, col32);
			emitter_json_dict_end(emitter);
		}

		emitter_json_dict_end(emitter); /* Close "mutexes". */
	}

	emitter_json_dict_end(emitter); /* Close "stats". */

	if (merged || destroyed || unmerged) {
		unsigned narenas;

		emitter_json_dict_begin(emitter, "stats.arenas");

		CTL_GET("arenas.narenas", &narenas, unsigned);
		size_t mib[3];
		size_t miblen = sizeof(mib) / sizeof(size_t);
		size_t sz;
		VARIABLE_ARRAY(bool, initialized, narenas);
		bool destroyed_initialized;
		unsigned i, j, ninitialized;

		xmallctlnametomib("arena.0.initialized", mib, &miblen);
		for (i = ninitialized = 0; i < narenas; i++) {
			mib[1] = i;
			sz = sizeof(bool);
			xmallctlbymib(mib, miblen, &initialized[i], &sz,
			    NULL, 0);
			if (initialized[i]) {
				ninitialized++;
			}
		}
		mib[1] = MALLCTL_ARENAS_DESTROYED;
		sz = sizeof(bool);
		xmallctlbymib(mib, miblen, &destroyed_initialized, &sz,
		    NULL, 0);

		/* Merged stats. */
		if (merged && (ninitialized > 1 || !unmerged)) {
			/* Print merged arena stats. */
			emitter_table_printf(emitter, "Merged arenas stats:\n");
			emitter_json_dict_begin(emitter, "merged");
			stats_arena_print(emitter, MALLCTL_ARENAS_ALL, bins,
			    large, mutex);
			emitter_json_dict_end(emitter); /* Close "merged". */
		}

		/* Destroyed stats. */
		if (destroyed_initialized && destroyed) {
			/* Print destroyed arena stats. */
			emitter_table_printf(emitter,
			    "Destroyed arenas stats:\n");
			emitter_json_dict_begin(emitter, "destroyed");
			stats_arena_print(emitter, MALLCTL_ARENAS_DESTROYED,
			    bins, large, mutex);
			emitter_json_dict_end(emitter); /* Close "destroyed". */
		}

		/* Unmerged stats. */
		if (unmerged) {
			for (i = j = 0; i < narenas; i++) {
				if (initialized[i]) {
					char arena_ind_str[20];
					malloc_snprintf(arena_ind_str,
					    sizeof(arena_ind_str), "%u", i);
					emitter_json_dict_begin(emitter,
					    arena_ind_str);
					emitter_table_printf(emitter,
					    "arenas[%s]:\n", arena_ind_str);
					stats_arena_print(emitter, i, bins,
					    large, mutex);
					/* Close "<arena-ind>". */
					emitter_json_dict_end(emitter);
				}
			}
		}
		emitter_json_dict_end(emitter); /* Close "stats.arenas". */
	}
}

void
stats_print(void (*write_cb)(void *, const char *), void *cbopaque,
    const char *opts) {
	int err;
	uint64_t epoch;
	size_t u64sz;
#define OPTION(o, v, d, s) bool v = d;
	STATS_PRINT_OPTIONS
#undef OPTION

	/*
	 * Refresh stats, in case mallctl() was called by the application.
	 *
	 * Check for OOM here, since refreshing the ctl cache can trigger
	 * allocation.  In practice, none of the subsequent mallctl()-related
	 * calls in this function will cause OOM if this one succeeds.
	 * */
	epoch = 1;
	u64sz = sizeof(uint64_t);
	err = je_mallctl("epoch", (void *)&epoch, &u64sz, (void *)&epoch,
	    sizeof(uint64_t));
	if (err != 0) {
		if (err == EAGAIN) {
			malloc_write("<jemalloc>: Memory allocation failure in "
			    "mallctl(\"epoch\", ...)\n");
			return;
		}
		malloc_write("<jemalloc>: Failure in mallctl(\"epoch\", "
		    "...)\n");
		abort();
	}

	if (opts != NULL) {
		for (unsigned i = 0; opts[i] != '\0'; i++) {
			switch (opts[i]) {
#define OPTION(o, v, d, s) case o: v = s; break;
				STATS_PRINT_OPTIONS
#undef OPTION
			default:;
			}
		}
	}

	emitter_t emitter;
	emitter_init(&emitter,
	    json ? emitter_output_json : emitter_output_table, write_cb,
	    cbopaque);
	emitter_begin(&emitter);
	emitter_table_printf(&emitter, "___ Begin jemalloc statistics ___\n");
	emitter_json_dict_begin(&emitter, "jemalloc");

	if (general) {
		stats_general_print(&emitter);
	}
	if (config_stats) {
		stats_print_helper(&emitter, merged, destroyed, unmerged,
		    bins, large, mutex);
	}

	emitter_json_dict_end(&emitter); /* Closes the "jemalloc" dict. */
	emitter_table_printf(&emitter, "--- End jemalloc statistics ---\n");
	emitter_end(&emitter);
}
