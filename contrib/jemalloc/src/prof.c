#define JEMALLOC_PROF_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/ckh.h"
#include "jemalloc/internal/hash.h"
#include "jemalloc/internal/malloc_io.h"
#include "jemalloc/internal/mutex.h"

/******************************************************************************/

#ifdef JEMALLOC_PROF_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

#ifdef JEMALLOC_PROF_LIBGCC
/*
 * We have a circular dependency -- jemalloc_internal.h tells us if we should
 * use libgcc's unwinding functionality, but after we've included that, we've
 * already hooked _Unwind_Backtrace.  We'll temporarily disable hooking.
 */
#undef _Unwind_Backtrace
#include <unwind.h>
#define _Unwind_Backtrace JEMALLOC_HOOK(_Unwind_Backtrace, hooks_libc_hook)
#endif

/******************************************************************************/
/* Data. */

bool		opt_prof = false;
bool		opt_prof_active = true;
bool		opt_prof_thread_active_init = true;
size_t		opt_lg_prof_sample = LG_PROF_SAMPLE_DEFAULT;
ssize_t		opt_lg_prof_interval = LG_PROF_INTERVAL_DEFAULT;
bool		opt_prof_gdump = false;
bool		opt_prof_final = false;
bool		opt_prof_leak = false;
bool		opt_prof_accum = false;
char		opt_prof_prefix[
    /* Minimize memory bloat for non-prof builds. */
#ifdef JEMALLOC_PROF
    PATH_MAX +
#endif
    1];

/*
 * Initialized as opt_prof_active, and accessed via
 * prof_active_[gs]et{_unlocked,}().
 */
bool			prof_active;
static malloc_mutex_t	prof_active_mtx;

/*
 * Initialized as opt_prof_thread_active_init, and accessed via
 * prof_thread_active_init_[gs]et().
 */
static bool		prof_thread_active_init;
static malloc_mutex_t	prof_thread_active_init_mtx;

/*
 * Initialized as opt_prof_gdump, and accessed via
 * prof_gdump_[gs]et{_unlocked,}().
 */
bool			prof_gdump_val;
static malloc_mutex_t	prof_gdump_mtx;

uint64_t	prof_interval = 0;

size_t		lg_prof_sample;

/*
 * Table of mutexes that are shared among gctx's.  These are leaf locks, so
 * there is no problem with using them for more than one gctx at the same time.
 * The primary motivation for this sharing though is that gctx's are ephemeral,
 * and destroying mutexes causes complications for systems that allocate when
 * creating/destroying mutexes.
 */
static malloc_mutex_t	*gctx_locks;
static atomic_u_t	cum_gctxs; /* Atomic counter. */

/*
 * Table of mutexes that are shared among tdata's.  No operations require
 * holding multiple tdata locks, so there is no problem with using them for more
 * than one tdata at the same time, even though a gctx lock may be acquired
 * while holding a tdata lock.
 */
static malloc_mutex_t	*tdata_locks;

/*
 * Global hash of (prof_bt_t *)-->(prof_gctx_t *).  This is the master data
 * structure that knows about all backtraces currently captured.
 */
static ckh_t		bt2gctx;
/* Non static to enable profiling. */
malloc_mutex_t		bt2gctx_mtx;

/*
 * Tree of all extant prof_tdata_t structures, regardless of state,
 * {attached,detached,expired}.
 */
static prof_tdata_tree_t	tdatas;
static malloc_mutex_t	tdatas_mtx;

static uint64_t		next_thr_uid;
static malloc_mutex_t	next_thr_uid_mtx;

static malloc_mutex_t	prof_dump_seq_mtx;
static uint64_t		prof_dump_seq;
static uint64_t		prof_dump_iseq;
static uint64_t		prof_dump_mseq;
static uint64_t		prof_dump_useq;

/*
 * This buffer is rather large for stack allocation, so use a single buffer for
 * all profile dumps.
 */
static malloc_mutex_t	prof_dump_mtx;
static char		prof_dump_buf[
    /* Minimize memory bloat for non-prof builds. */
#ifdef JEMALLOC_PROF
    PROF_DUMP_BUFSIZE
#else
    1
#endif
];
static size_t		prof_dump_buf_end;
static int		prof_dump_fd;

/* Do not dump any profiles until bootstrapping is complete. */
static bool		prof_booted = false;

/******************************************************************************/
/*
 * Function prototypes for static functions that are referenced prior to
 * definition.
 */

static bool	prof_tctx_should_destroy(tsdn_t *tsdn, prof_tctx_t *tctx);
static void	prof_tctx_destroy(tsd_t *tsd, prof_tctx_t *tctx);
static bool	prof_tdata_should_destroy(tsdn_t *tsdn, prof_tdata_t *tdata,
    bool even_if_attached);
static void	prof_tdata_destroy(tsd_t *tsd, prof_tdata_t *tdata,
    bool even_if_attached);
static char	*prof_thread_name_alloc(tsdn_t *tsdn, const char *thread_name);

/******************************************************************************/
/* Red-black trees. */

static int
prof_tctx_comp(const prof_tctx_t *a, const prof_tctx_t *b) {
	uint64_t a_thr_uid = a->thr_uid;
	uint64_t b_thr_uid = b->thr_uid;
	int ret = (a_thr_uid > b_thr_uid) - (a_thr_uid < b_thr_uid);
	if (ret == 0) {
		uint64_t a_thr_discrim = a->thr_discrim;
		uint64_t b_thr_discrim = b->thr_discrim;
		ret = (a_thr_discrim > b_thr_discrim) - (a_thr_discrim <
		    b_thr_discrim);
		if (ret == 0) {
			uint64_t a_tctx_uid = a->tctx_uid;
			uint64_t b_tctx_uid = b->tctx_uid;
			ret = (a_tctx_uid > b_tctx_uid) - (a_tctx_uid <
			    b_tctx_uid);
		}
	}
	return ret;
}

rb_gen(static UNUSED, tctx_tree_, prof_tctx_tree_t, prof_tctx_t,
    tctx_link, prof_tctx_comp)

static int
prof_gctx_comp(const prof_gctx_t *a, const prof_gctx_t *b) {
	unsigned a_len = a->bt.len;
	unsigned b_len = b->bt.len;
	unsigned comp_len = (a_len < b_len) ? a_len : b_len;
	int ret = memcmp(a->bt.vec, b->bt.vec, comp_len * sizeof(void *));
	if (ret == 0) {
		ret = (a_len > b_len) - (a_len < b_len);
	}
	return ret;
}

rb_gen(static UNUSED, gctx_tree_, prof_gctx_tree_t, prof_gctx_t, dump_link,
    prof_gctx_comp)

static int
prof_tdata_comp(const prof_tdata_t *a, const prof_tdata_t *b) {
	int ret;
	uint64_t a_uid = a->thr_uid;
	uint64_t b_uid = b->thr_uid;

	ret = ((a_uid > b_uid) - (a_uid < b_uid));
	if (ret == 0) {
		uint64_t a_discrim = a->thr_discrim;
		uint64_t b_discrim = b->thr_discrim;

		ret = ((a_discrim > b_discrim) - (a_discrim < b_discrim));
	}
	return ret;
}

rb_gen(static UNUSED, tdata_tree_, prof_tdata_tree_t, prof_tdata_t, tdata_link,
    prof_tdata_comp)

/******************************************************************************/

void
prof_alloc_rollback(tsd_t *tsd, prof_tctx_t *tctx, bool updated) {
	prof_tdata_t *tdata;

	cassert(config_prof);

	if (updated) {
		/*
		 * Compute a new sample threshold.  This isn't very important in
		 * practice, because this function is rarely executed, so the
		 * potential for sample bias is minimal except in contrived
		 * programs.
		 */
		tdata = prof_tdata_get(tsd, true);
		if (tdata != NULL) {
			prof_sample_threshold_update(tdata);
		}
	}

	if ((uintptr_t)tctx > (uintptr_t)1U) {
		malloc_mutex_lock(tsd_tsdn(tsd), tctx->tdata->lock);
		tctx->prepared = false;
		if (prof_tctx_should_destroy(tsd_tsdn(tsd), tctx)) {
			prof_tctx_destroy(tsd, tctx);
		} else {
			malloc_mutex_unlock(tsd_tsdn(tsd), tctx->tdata->lock);
		}
	}
}

void
prof_malloc_sample_object(tsdn_t *tsdn, const void *ptr, size_t usize,
    prof_tctx_t *tctx) {
	prof_tctx_set(tsdn, ptr, usize, NULL, tctx);

	malloc_mutex_lock(tsdn, tctx->tdata->lock);
	tctx->cnts.curobjs++;
	tctx->cnts.curbytes += usize;
	if (opt_prof_accum) {
		tctx->cnts.accumobjs++;
		tctx->cnts.accumbytes += usize;
	}
	tctx->prepared = false;
	malloc_mutex_unlock(tsdn, tctx->tdata->lock);
}

void
prof_free_sampled_object(tsd_t *tsd, size_t usize, prof_tctx_t *tctx) {
	malloc_mutex_lock(tsd_tsdn(tsd), tctx->tdata->lock);
	assert(tctx->cnts.curobjs > 0);
	assert(tctx->cnts.curbytes >= usize);
	tctx->cnts.curobjs--;
	tctx->cnts.curbytes -= usize;

	if (prof_tctx_should_destroy(tsd_tsdn(tsd), tctx)) {
		prof_tctx_destroy(tsd, tctx);
	} else {
		malloc_mutex_unlock(tsd_tsdn(tsd), tctx->tdata->lock);
	}
}

void
bt_init(prof_bt_t *bt, void **vec) {
	cassert(config_prof);

	bt->vec = vec;
	bt->len = 0;
}

static void
prof_enter(tsd_t *tsd, prof_tdata_t *tdata) {
	cassert(config_prof);
	assert(tdata == prof_tdata_get(tsd, false));

	if (tdata != NULL) {
		assert(!tdata->enq);
		tdata->enq = true;
	}

	malloc_mutex_lock(tsd_tsdn(tsd), &bt2gctx_mtx);
}

static void
prof_leave(tsd_t *tsd, prof_tdata_t *tdata) {
	cassert(config_prof);
	assert(tdata == prof_tdata_get(tsd, false));

	malloc_mutex_unlock(tsd_tsdn(tsd), &bt2gctx_mtx);

	if (tdata != NULL) {
		bool idump, gdump;

		assert(tdata->enq);
		tdata->enq = false;
		idump = tdata->enq_idump;
		tdata->enq_idump = false;
		gdump = tdata->enq_gdump;
		tdata->enq_gdump = false;

		if (idump) {
			prof_idump(tsd_tsdn(tsd));
		}
		if (gdump) {
			prof_gdump(tsd_tsdn(tsd));
		}
	}
}

#ifdef JEMALLOC_PROF_LIBUNWIND
void
prof_backtrace(prof_bt_t *bt) {
	int nframes;

	cassert(config_prof);
	assert(bt->len == 0);
	assert(bt->vec != NULL);

	nframes = unw_backtrace(bt->vec, PROF_BT_MAX);
	if (nframes <= 0) {
		return;
	}
	bt->len = nframes;
}
#elif (defined(JEMALLOC_PROF_LIBGCC))
static _Unwind_Reason_Code
prof_unwind_init_callback(struct _Unwind_Context *context, void *arg) {
	cassert(config_prof);

	return _URC_NO_REASON;
}

static _Unwind_Reason_Code
prof_unwind_callback(struct _Unwind_Context *context, void *arg) {
	prof_unwind_data_t *data = (prof_unwind_data_t *)arg;
	void *ip;

	cassert(config_prof);

	ip = (void *)_Unwind_GetIP(context);
	if (ip == NULL) {
		return _URC_END_OF_STACK;
	}
	data->bt->vec[data->bt->len] = ip;
	data->bt->len++;
	if (data->bt->len == data->max) {
		return _URC_END_OF_STACK;
	}

	return _URC_NO_REASON;
}

void
prof_backtrace(prof_bt_t *bt) {
	prof_unwind_data_t data = {bt, PROF_BT_MAX};

	cassert(config_prof);

	_Unwind_Backtrace(prof_unwind_callback, &data);
}
#elif (defined(JEMALLOC_PROF_GCC))
void
prof_backtrace(prof_bt_t *bt) {
#define BT_FRAME(i)							\
	if ((i) < PROF_BT_MAX) {					\
		void *p;						\
		if (__builtin_frame_address(i) == 0) {			\
			return;						\
		}							\
		p = __builtin_return_address(i);			\
		if (p == NULL) {					\
			return;						\
		}							\
		bt->vec[(i)] = p;					\
		bt->len = (i) + 1;					\
	} else {							\
		return;							\
	}

	cassert(config_prof);

	BT_FRAME(0)
	BT_FRAME(1)
	BT_FRAME(2)
	BT_FRAME(3)
	BT_FRAME(4)
	BT_FRAME(5)
	BT_FRAME(6)
	BT_FRAME(7)
	BT_FRAME(8)
	BT_FRAME(9)

	BT_FRAME(10)
	BT_FRAME(11)
	BT_FRAME(12)
	BT_FRAME(13)
	BT_FRAME(14)
	BT_FRAME(15)
	BT_FRAME(16)
	BT_FRAME(17)
	BT_FRAME(18)
	BT_FRAME(19)

	BT_FRAME(20)
	BT_FRAME(21)
	BT_FRAME(22)
	BT_FRAME(23)
	BT_FRAME(24)
	BT_FRAME(25)
	BT_FRAME(26)
	BT_FRAME(27)
	BT_FRAME(28)
	BT_FRAME(29)

	BT_FRAME(30)
	BT_FRAME(31)
	BT_FRAME(32)
	BT_FRAME(33)
	BT_FRAME(34)
	BT_FRAME(35)
	BT_FRAME(36)
	BT_FRAME(37)
	BT_FRAME(38)
	BT_FRAME(39)

	BT_FRAME(40)
	BT_FRAME(41)
	BT_FRAME(42)
	BT_FRAME(43)
	BT_FRAME(44)
	BT_FRAME(45)
	BT_FRAME(46)
	BT_FRAME(47)
	BT_FRAME(48)
	BT_FRAME(49)

	BT_FRAME(50)
	BT_FRAME(51)
	BT_FRAME(52)
	BT_FRAME(53)
	BT_FRAME(54)
	BT_FRAME(55)
	BT_FRAME(56)
	BT_FRAME(57)
	BT_FRAME(58)
	BT_FRAME(59)

	BT_FRAME(60)
	BT_FRAME(61)
	BT_FRAME(62)
	BT_FRAME(63)
	BT_FRAME(64)
	BT_FRAME(65)
	BT_FRAME(66)
	BT_FRAME(67)
	BT_FRAME(68)
	BT_FRAME(69)

	BT_FRAME(70)
	BT_FRAME(71)
	BT_FRAME(72)
	BT_FRAME(73)
	BT_FRAME(74)
	BT_FRAME(75)
	BT_FRAME(76)
	BT_FRAME(77)
	BT_FRAME(78)
	BT_FRAME(79)

	BT_FRAME(80)
	BT_FRAME(81)
	BT_FRAME(82)
	BT_FRAME(83)
	BT_FRAME(84)
	BT_FRAME(85)
	BT_FRAME(86)
	BT_FRAME(87)
	BT_FRAME(88)
	BT_FRAME(89)

	BT_FRAME(90)
	BT_FRAME(91)
	BT_FRAME(92)
	BT_FRAME(93)
	BT_FRAME(94)
	BT_FRAME(95)
	BT_FRAME(96)
	BT_FRAME(97)
	BT_FRAME(98)
	BT_FRAME(99)

	BT_FRAME(100)
	BT_FRAME(101)
	BT_FRAME(102)
	BT_FRAME(103)
	BT_FRAME(104)
	BT_FRAME(105)
	BT_FRAME(106)
	BT_FRAME(107)
	BT_FRAME(108)
	BT_FRAME(109)

	BT_FRAME(110)
	BT_FRAME(111)
	BT_FRAME(112)
	BT_FRAME(113)
	BT_FRAME(114)
	BT_FRAME(115)
	BT_FRAME(116)
	BT_FRAME(117)
	BT_FRAME(118)
	BT_FRAME(119)

	BT_FRAME(120)
	BT_FRAME(121)
	BT_FRAME(122)
	BT_FRAME(123)
	BT_FRAME(124)
	BT_FRAME(125)
	BT_FRAME(126)
	BT_FRAME(127)
#undef BT_FRAME
}
#else
void
prof_backtrace(prof_bt_t *bt) {
	cassert(config_prof);
	not_reached();
}
#endif

static malloc_mutex_t *
prof_gctx_mutex_choose(void) {
	unsigned ngctxs = atomic_fetch_add_u(&cum_gctxs, 1, ATOMIC_RELAXED);

	return &gctx_locks[(ngctxs - 1) % PROF_NCTX_LOCKS];
}

static malloc_mutex_t *
prof_tdata_mutex_choose(uint64_t thr_uid) {
	return &tdata_locks[thr_uid % PROF_NTDATA_LOCKS];
}

static prof_gctx_t *
prof_gctx_create(tsdn_t *tsdn, prof_bt_t *bt) {
	/*
	 * Create a single allocation that has space for vec of length bt->len.
	 */
	size_t size = offsetof(prof_gctx_t, vec) + (bt->len * sizeof(void *));
	prof_gctx_t *gctx = (prof_gctx_t *)iallocztm(tsdn, size,
	    sz_size2index(size), false, NULL, true, arena_get(TSDN_NULL, 0, true),
	    true);
	if (gctx == NULL) {
		return NULL;
	}
	gctx->lock = prof_gctx_mutex_choose();
	/*
	 * Set nlimbo to 1, in order to avoid a race condition with
	 * prof_tctx_destroy()/prof_gctx_try_destroy().
	 */
	gctx->nlimbo = 1;
	tctx_tree_new(&gctx->tctxs);
	/* Duplicate bt. */
	memcpy(gctx->vec, bt->vec, bt->len * sizeof(void *));
	gctx->bt.vec = gctx->vec;
	gctx->bt.len = bt->len;
	return gctx;
}

static void
prof_gctx_try_destroy(tsd_t *tsd, prof_tdata_t *tdata_self, prof_gctx_t *gctx,
    prof_tdata_t *tdata) {
	cassert(config_prof);

	/*
	 * Check that gctx is still unused by any thread cache before destroying
	 * it.  prof_lookup() increments gctx->nlimbo in order to avoid a race
	 * condition with this function, as does prof_tctx_destroy() in order to
	 * avoid a race between the main body of prof_tctx_destroy() and entry
	 * into this function.
	 */
	prof_enter(tsd, tdata_self);
	malloc_mutex_lock(tsd_tsdn(tsd), gctx->lock);
	assert(gctx->nlimbo != 0);
	if (tctx_tree_empty(&gctx->tctxs) && gctx->nlimbo == 1) {
		/* Remove gctx from bt2gctx. */
		if (ckh_remove(tsd, &bt2gctx, &gctx->bt, NULL, NULL)) {
			not_reached();
		}
		prof_leave(tsd, tdata_self);
		/* Destroy gctx. */
		malloc_mutex_unlock(tsd_tsdn(tsd), gctx->lock);
		idalloctm(tsd_tsdn(tsd), gctx, NULL, NULL, true, true);
	} else {
		/*
		 * Compensate for increment in prof_tctx_destroy() or
		 * prof_lookup().
		 */
		gctx->nlimbo--;
		malloc_mutex_unlock(tsd_tsdn(tsd), gctx->lock);
		prof_leave(tsd, tdata_self);
	}
}

static bool
prof_tctx_should_destroy(tsdn_t *tsdn, prof_tctx_t *tctx) {
	malloc_mutex_assert_owner(tsdn, tctx->tdata->lock);

	if (opt_prof_accum) {
		return false;
	}
	if (tctx->cnts.curobjs != 0) {
		return false;
	}
	if (tctx->prepared) {
		return false;
	}
	return true;
}

static bool
prof_gctx_should_destroy(prof_gctx_t *gctx) {
	if (opt_prof_accum) {
		return false;
	}
	if (!tctx_tree_empty(&gctx->tctxs)) {
		return false;
	}
	if (gctx->nlimbo != 0) {
		return false;
	}
	return true;
}

static void
prof_tctx_destroy(tsd_t *tsd, prof_tctx_t *tctx) {
	prof_tdata_t *tdata = tctx->tdata;
	prof_gctx_t *gctx = tctx->gctx;
	bool destroy_tdata, destroy_tctx, destroy_gctx;

	malloc_mutex_assert_owner(tsd_tsdn(tsd), tctx->tdata->lock);

	assert(tctx->cnts.curobjs == 0);
	assert(tctx->cnts.curbytes == 0);
	assert(!opt_prof_accum);
	assert(tctx->cnts.accumobjs == 0);
	assert(tctx->cnts.accumbytes == 0);

	ckh_remove(tsd, &tdata->bt2tctx, &gctx->bt, NULL, NULL);
	destroy_tdata = prof_tdata_should_destroy(tsd_tsdn(tsd), tdata, false);
	malloc_mutex_unlock(tsd_tsdn(tsd), tdata->lock);

	malloc_mutex_lock(tsd_tsdn(tsd), gctx->lock);
	switch (tctx->state) {
	case prof_tctx_state_nominal:
		tctx_tree_remove(&gctx->tctxs, tctx);
		destroy_tctx = true;
		if (prof_gctx_should_destroy(gctx)) {
			/*
			 * Increment gctx->nlimbo in order to keep another
			 * thread from winning the race to destroy gctx while
			 * this one has gctx->lock dropped.  Without this, it
			 * would be possible for another thread to:
			 *
			 * 1) Sample an allocation associated with gctx.
			 * 2) Deallocate the sampled object.
			 * 3) Successfully prof_gctx_try_destroy(gctx).
			 *
			 * The result would be that gctx no longer exists by the
			 * time this thread accesses it in
			 * prof_gctx_try_destroy().
			 */
			gctx->nlimbo++;
			destroy_gctx = true;
		} else {
			destroy_gctx = false;
		}
		break;
	case prof_tctx_state_dumping:
		/*
		 * A dumping thread needs tctx to remain valid until dumping
		 * has finished.  Change state such that the dumping thread will
		 * complete destruction during a late dump iteration phase.
		 */
		tctx->state = prof_tctx_state_purgatory;
		destroy_tctx = false;
		destroy_gctx = false;
		break;
	default:
		not_reached();
		destroy_tctx = false;
		destroy_gctx = false;
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), gctx->lock);
	if (destroy_gctx) {
		prof_gctx_try_destroy(tsd, prof_tdata_get(tsd, false), gctx,
		    tdata);
	}

	malloc_mutex_assert_not_owner(tsd_tsdn(tsd), tctx->tdata->lock);

	if (destroy_tdata) {
		prof_tdata_destroy(tsd, tdata, false);
	}

	if (destroy_tctx) {
		idalloctm(tsd_tsdn(tsd), tctx, NULL, NULL, true, true);
	}
}

static bool
prof_lookup_global(tsd_t *tsd, prof_bt_t *bt, prof_tdata_t *tdata,
    void **p_btkey, prof_gctx_t **p_gctx, bool *p_new_gctx) {
	union {
		prof_gctx_t	*p;
		void		*v;
	} gctx, tgctx;
	union {
		prof_bt_t	*p;
		void		*v;
	} btkey;
	bool new_gctx;

	prof_enter(tsd, tdata);
	if (ckh_search(&bt2gctx, bt, &btkey.v, &gctx.v)) {
		/* bt has never been seen before.  Insert it. */
		prof_leave(tsd, tdata);
		tgctx.p = prof_gctx_create(tsd_tsdn(tsd), bt);
		if (tgctx.v == NULL) {
			return true;
		}
		prof_enter(tsd, tdata);
		if (ckh_search(&bt2gctx, bt, &btkey.v, &gctx.v)) {
			gctx.p = tgctx.p;
			btkey.p = &gctx.p->bt;
			if (ckh_insert(tsd, &bt2gctx, btkey.v, gctx.v)) {
				/* OOM. */
				prof_leave(tsd, tdata);
				idalloctm(tsd_tsdn(tsd), gctx.v, NULL, NULL,
				    true, true);
				return true;
			}
			new_gctx = true;
		} else {
			new_gctx = false;
		}
	} else {
		tgctx.v = NULL;
		new_gctx = false;
	}

	if (!new_gctx) {
		/*
		 * Increment nlimbo, in order to avoid a race condition with
		 * prof_tctx_destroy()/prof_gctx_try_destroy().
		 */
		malloc_mutex_lock(tsd_tsdn(tsd), gctx.p->lock);
		gctx.p->nlimbo++;
		malloc_mutex_unlock(tsd_tsdn(tsd), gctx.p->lock);
		new_gctx = false;

		if (tgctx.v != NULL) {
			/* Lost race to insert. */
			idalloctm(tsd_tsdn(tsd), tgctx.v, NULL, NULL, true,
			    true);
		}
	}
	prof_leave(tsd, tdata);

	*p_btkey = btkey.v;
	*p_gctx = gctx.p;
	*p_new_gctx = new_gctx;
	return false;
}

prof_tctx_t *
prof_lookup(tsd_t *tsd, prof_bt_t *bt) {
	union {
		prof_tctx_t	*p;
		void		*v;
	} ret;
	prof_tdata_t *tdata;
	bool not_found;

	cassert(config_prof);

	tdata = prof_tdata_get(tsd, false);
	if (tdata == NULL) {
		return NULL;
	}

	malloc_mutex_lock(tsd_tsdn(tsd), tdata->lock);
	not_found = ckh_search(&tdata->bt2tctx, bt, NULL, &ret.v);
	if (!not_found) { /* Note double negative! */
		ret.p->prepared = true;
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), tdata->lock);
	if (not_found) {
		void *btkey;
		prof_gctx_t *gctx;
		bool new_gctx, error;

		/*
		 * This thread's cache lacks bt.  Look for it in the global
		 * cache.
		 */
		if (prof_lookup_global(tsd, bt, tdata, &btkey, &gctx,
		    &new_gctx)) {
			return NULL;
		}

		/* Link a prof_tctx_t into gctx for this thread. */
		ret.v = iallocztm(tsd_tsdn(tsd), sizeof(prof_tctx_t),
		    sz_size2index(sizeof(prof_tctx_t)), false, NULL, true,
		    arena_ichoose(tsd, NULL), true);
		if (ret.p == NULL) {
			if (new_gctx) {
				prof_gctx_try_destroy(tsd, tdata, gctx, tdata);
			}
			return NULL;
		}
		ret.p->tdata = tdata;
		ret.p->thr_uid = tdata->thr_uid;
		ret.p->thr_discrim = tdata->thr_discrim;
		memset(&ret.p->cnts, 0, sizeof(prof_cnt_t));
		ret.p->gctx = gctx;
		ret.p->tctx_uid = tdata->tctx_uid_next++;
		ret.p->prepared = true;
		ret.p->state = prof_tctx_state_initializing;
		malloc_mutex_lock(tsd_tsdn(tsd), tdata->lock);
		error = ckh_insert(tsd, &tdata->bt2tctx, btkey, ret.v);
		malloc_mutex_unlock(tsd_tsdn(tsd), tdata->lock);
		if (error) {
			if (new_gctx) {
				prof_gctx_try_destroy(tsd, tdata, gctx, tdata);
			}
			idalloctm(tsd_tsdn(tsd), ret.v, NULL, NULL, true, true);
			return NULL;
		}
		malloc_mutex_lock(tsd_tsdn(tsd), gctx->lock);
		ret.p->state = prof_tctx_state_nominal;
		tctx_tree_insert(&gctx->tctxs, ret.p);
		gctx->nlimbo--;
		malloc_mutex_unlock(tsd_tsdn(tsd), gctx->lock);
	}

	return ret.p;
}

/*
 * The bodies of this function and prof_leakcheck() are compiled out unless heap
 * profiling is enabled, so that it is possible to compile jemalloc with
 * floating point support completely disabled.  Avoiding floating point code is
 * important on memory-constrained systems, but it also enables a workaround for
 * versions of glibc that don't properly save/restore floating point registers
 * during dynamic lazy symbol loading (which internally calls into whatever
 * malloc implementation happens to be integrated into the application).  Note
 * that some compilers (e.g.  gcc 4.8) may use floating point registers for fast
 * memory moves, so jemalloc must be compiled with such optimizations disabled
 * (e.g.
 * -mno-sse) in order for the workaround to be complete.
 */
void
prof_sample_threshold_update(prof_tdata_t *tdata) {
#ifdef JEMALLOC_PROF
	uint64_t r;
	double u;

	if (!config_prof) {
		return;
	}

	if (lg_prof_sample == 0) {
		tdata->bytes_until_sample = 0;
		return;
	}

	/*
	 * Compute sample interval as a geometrically distributed random
	 * variable with mean (2^lg_prof_sample).
	 *
	 *                             __        __
	 *                             |  log(u)  |                     1
	 * tdata->bytes_until_sample = | -------- |, where p = ---------------
	 *                             | log(1-p) |             lg_prof_sample
	 *                                                     2
	 *
	 * For more information on the math, see:
	 *
	 *   Non-Uniform Random Variate Generation
	 *   Luc Devroye
	 *   Springer-Verlag, New York, 1986
	 *   pp 500
	 *   (http://luc.devroye.org/rnbookindex.html)
	 */
	r = prng_lg_range_u64(&tdata->prng_state, 53);
	u = (double)r * (1.0/9007199254740992.0L);
	tdata->bytes_until_sample = (uint64_t)(log(u) /
	    log(1.0 - (1.0 / (double)((uint64_t)1U << lg_prof_sample))))
	    + (uint64_t)1U;
#endif
}

#ifdef JEMALLOC_JET
static prof_tdata_t *
prof_tdata_count_iter(prof_tdata_tree_t *tdatas, prof_tdata_t *tdata,
    void *arg) {
	size_t *tdata_count = (size_t *)arg;

	(*tdata_count)++;

	return NULL;
}

size_t
prof_tdata_count(void) {
	size_t tdata_count = 0;
	tsdn_t *tsdn;

	tsdn = tsdn_fetch();
	malloc_mutex_lock(tsdn, &tdatas_mtx);
	tdata_tree_iter(&tdatas, NULL, prof_tdata_count_iter,
	    (void *)&tdata_count);
	malloc_mutex_unlock(tsdn, &tdatas_mtx);

	return tdata_count;
}

size_t
prof_bt_count(void) {
	size_t bt_count;
	tsd_t *tsd;
	prof_tdata_t *tdata;

	tsd = tsd_fetch();
	tdata = prof_tdata_get(tsd, false);
	if (tdata == NULL) {
		return 0;
	}

	malloc_mutex_lock(tsd_tsdn(tsd), &bt2gctx_mtx);
	bt_count = ckh_count(&bt2gctx);
	malloc_mutex_unlock(tsd_tsdn(tsd), &bt2gctx_mtx);

	return bt_count;
}
#endif

static int
prof_dump_open_impl(bool propagate_err, const char *filename) {
	int fd;

	fd = creat(filename, 0644);
	if (fd == -1 && !propagate_err) {
		malloc_printf("<jemalloc>: creat(\"%s\"), 0644) failed\n",
		    filename);
		if (opt_abort) {
			abort();
		}
	}

	return fd;
}
prof_dump_open_t *JET_MUTABLE prof_dump_open = prof_dump_open_impl;

static bool
prof_dump_flush(bool propagate_err) {
	bool ret = false;
	ssize_t err;

	cassert(config_prof);

	err = malloc_write_fd(prof_dump_fd, prof_dump_buf, prof_dump_buf_end);
	if (err == -1) {
		if (!propagate_err) {
			malloc_write("<jemalloc>: write() failed during heap "
			    "profile flush\n");
			if (opt_abort) {
				abort();
			}
		}
		ret = true;
	}
	prof_dump_buf_end = 0;

	return ret;
}

static bool
prof_dump_close(bool propagate_err) {
	bool ret;

	assert(prof_dump_fd != -1);
	ret = prof_dump_flush(propagate_err);
	close(prof_dump_fd);
	prof_dump_fd = -1;

	return ret;
}

static bool
prof_dump_write(bool propagate_err, const char *s) {
	size_t i, slen, n;

	cassert(config_prof);

	i = 0;
	slen = strlen(s);
	while (i < slen) {
		/* Flush the buffer if it is full. */
		if (prof_dump_buf_end == PROF_DUMP_BUFSIZE) {
			if (prof_dump_flush(propagate_err) && propagate_err) {
				return true;
			}
		}

		if (prof_dump_buf_end + slen <= PROF_DUMP_BUFSIZE) {
			/* Finish writing. */
			n = slen - i;
		} else {
			/* Write as much of s as will fit. */
			n = PROF_DUMP_BUFSIZE - prof_dump_buf_end;
		}
		memcpy(&prof_dump_buf[prof_dump_buf_end], &s[i], n);
		prof_dump_buf_end += n;
		i += n;
	}

	return false;
}

JEMALLOC_FORMAT_PRINTF(2, 3)
static bool
prof_dump_printf(bool propagate_err, const char *format, ...) {
	bool ret;
	va_list ap;
	char buf[PROF_PRINTF_BUFSIZE];

	va_start(ap, format);
	malloc_vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	ret = prof_dump_write(propagate_err, buf);

	return ret;
}

static void
prof_tctx_merge_tdata(tsdn_t *tsdn, prof_tctx_t *tctx, prof_tdata_t *tdata) {
	malloc_mutex_assert_owner(tsdn, tctx->tdata->lock);

	malloc_mutex_lock(tsdn, tctx->gctx->lock);

	switch (tctx->state) {
	case prof_tctx_state_initializing:
		malloc_mutex_unlock(tsdn, tctx->gctx->lock);
		return;
	case prof_tctx_state_nominal:
		tctx->state = prof_tctx_state_dumping;
		malloc_mutex_unlock(tsdn, tctx->gctx->lock);

		memcpy(&tctx->dump_cnts, &tctx->cnts, sizeof(prof_cnt_t));

		tdata->cnt_summed.curobjs += tctx->dump_cnts.curobjs;
		tdata->cnt_summed.curbytes += tctx->dump_cnts.curbytes;
		if (opt_prof_accum) {
			tdata->cnt_summed.accumobjs +=
			    tctx->dump_cnts.accumobjs;
			tdata->cnt_summed.accumbytes +=
			    tctx->dump_cnts.accumbytes;
		}
		break;
	case prof_tctx_state_dumping:
	case prof_tctx_state_purgatory:
		not_reached();
	}
}

static void
prof_tctx_merge_gctx(tsdn_t *tsdn, prof_tctx_t *tctx, prof_gctx_t *gctx) {
	malloc_mutex_assert_owner(tsdn, gctx->lock);

	gctx->cnt_summed.curobjs += tctx->dump_cnts.curobjs;
	gctx->cnt_summed.curbytes += tctx->dump_cnts.curbytes;
	if (opt_prof_accum) {
		gctx->cnt_summed.accumobjs += tctx->dump_cnts.accumobjs;
		gctx->cnt_summed.accumbytes += tctx->dump_cnts.accumbytes;
	}
}

static prof_tctx_t *
prof_tctx_merge_iter(prof_tctx_tree_t *tctxs, prof_tctx_t *tctx, void *arg) {
	tsdn_t *tsdn = (tsdn_t *)arg;

	malloc_mutex_assert_owner(tsdn, tctx->gctx->lock);

	switch (tctx->state) {
	case prof_tctx_state_nominal:
		/* New since dumping started; ignore. */
		break;
	case prof_tctx_state_dumping:
	case prof_tctx_state_purgatory:
		prof_tctx_merge_gctx(tsdn, tctx, tctx->gctx);
		break;
	default:
		not_reached();
	}

	return NULL;
}

struct prof_tctx_dump_iter_arg_s {
	tsdn_t	*tsdn;
	bool	propagate_err;
};

static prof_tctx_t *
prof_tctx_dump_iter(prof_tctx_tree_t *tctxs, prof_tctx_t *tctx, void *opaque) {
	struct prof_tctx_dump_iter_arg_s *arg =
	    (struct prof_tctx_dump_iter_arg_s *)opaque;

	malloc_mutex_assert_owner(arg->tsdn, tctx->gctx->lock);

	switch (tctx->state) {
	case prof_tctx_state_initializing:
	case prof_tctx_state_nominal:
		/* Not captured by this dump. */
		break;
	case prof_tctx_state_dumping:
	case prof_tctx_state_purgatory:
		if (prof_dump_printf(arg->propagate_err,
		    "  t%"FMTu64": %"FMTu64": %"FMTu64" [%"FMTu64": "
		    "%"FMTu64"]\n", tctx->thr_uid, tctx->dump_cnts.curobjs,
		    tctx->dump_cnts.curbytes, tctx->dump_cnts.accumobjs,
		    tctx->dump_cnts.accumbytes)) {
			return tctx;
		}
		break;
	default:
		not_reached();
	}
	return NULL;
}

static prof_tctx_t *
prof_tctx_finish_iter(prof_tctx_tree_t *tctxs, prof_tctx_t *tctx, void *arg) {
	tsdn_t *tsdn = (tsdn_t *)arg;
	prof_tctx_t *ret;

	malloc_mutex_assert_owner(tsdn, tctx->gctx->lock);

	switch (tctx->state) {
	case prof_tctx_state_nominal:
		/* New since dumping started; ignore. */
		break;
	case prof_tctx_state_dumping:
		tctx->state = prof_tctx_state_nominal;
		break;
	case prof_tctx_state_purgatory:
		ret = tctx;
		goto label_return;
	default:
		not_reached();
	}

	ret = NULL;
label_return:
	return ret;
}

static void
prof_dump_gctx_prep(tsdn_t *tsdn, prof_gctx_t *gctx, prof_gctx_tree_t *gctxs) {
	cassert(config_prof);

	malloc_mutex_lock(tsdn, gctx->lock);

	/*
	 * Increment nlimbo so that gctx won't go away before dump.
	 * Additionally, link gctx into the dump list so that it is included in
	 * prof_dump()'s second pass.
	 */
	gctx->nlimbo++;
	gctx_tree_insert(gctxs, gctx);

	memset(&gctx->cnt_summed, 0, sizeof(prof_cnt_t));

	malloc_mutex_unlock(tsdn, gctx->lock);
}

struct prof_gctx_merge_iter_arg_s {
	tsdn_t	*tsdn;
	size_t	leak_ngctx;
};

static prof_gctx_t *
prof_gctx_merge_iter(prof_gctx_tree_t *gctxs, prof_gctx_t *gctx, void *opaque) {
	struct prof_gctx_merge_iter_arg_s *arg =
	    (struct prof_gctx_merge_iter_arg_s *)opaque;

	malloc_mutex_lock(arg->tsdn, gctx->lock);
	tctx_tree_iter(&gctx->tctxs, NULL, prof_tctx_merge_iter,
	    (void *)arg->tsdn);
	if (gctx->cnt_summed.curobjs != 0) {
		arg->leak_ngctx++;
	}
	malloc_mutex_unlock(arg->tsdn, gctx->lock);

	return NULL;
}

static void
prof_gctx_finish(tsd_t *tsd, prof_gctx_tree_t *gctxs) {
	prof_tdata_t *tdata = prof_tdata_get(tsd, false);
	prof_gctx_t *gctx;

	/*
	 * Standard tree iteration won't work here, because as soon as we
	 * decrement gctx->nlimbo and unlock gctx, another thread can
	 * concurrently destroy it, which will corrupt the tree.  Therefore,
	 * tear down the tree one node at a time during iteration.
	 */
	while ((gctx = gctx_tree_first(gctxs)) != NULL) {
		gctx_tree_remove(gctxs, gctx);
		malloc_mutex_lock(tsd_tsdn(tsd), gctx->lock);
		{
			prof_tctx_t *next;

			next = NULL;
			do {
				prof_tctx_t *to_destroy =
				    tctx_tree_iter(&gctx->tctxs, next,
				    prof_tctx_finish_iter,
				    (void *)tsd_tsdn(tsd));
				if (to_destroy != NULL) {
					next = tctx_tree_next(&gctx->tctxs,
					    to_destroy);
					tctx_tree_remove(&gctx->tctxs,
					    to_destroy);
					idalloctm(tsd_tsdn(tsd), to_destroy,
					    NULL, NULL, true, true);
				} else {
					next = NULL;
				}
			} while (next != NULL);
		}
		gctx->nlimbo--;
		if (prof_gctx_should_destroy(gctx)) {
			gctx->nlimbo++;
			malloc_mutex_unlock(tsd_tsdn(tsd), gctx->lock);
			prof_gctx_try_destroy(tsd, tdata, gctx, tdata);
		} else {
			malloc_mutex_unlock(tsd_tsdn(tsd), gctx->lock);
		}
	}
}

struct prof_tdata_merge_iter_arg_s {
	tsdn_t		*tsdn;
	prof_cnt_t	cnt_all;
};

static prof_tdata_t *
prof_tdata_merge_iter(prof_tdata_tree_t *tdatas, prof_tdata_t *tdata,
    void *opaque) {
	struct prof_tdata_merge_iter_arg_s *arg =
	    (struct prof_tdata_merge_iter_arg_s *)opaque;

	malloc_mutex_lock(arg->tsdn, tdata->lock);
	if (!tdata->expired) {
		size_t tabind;
		union {
			prof_tctx_t	*p;
			void		*v;
		} tctx;

		tdata->dumping = true;
		memset(&tdata->cnt_summed, 0, sizeof(prof_cnt_t));
		for (tabind = 0; !ckh_iter(&tdata->bt2tctx, &tabind, NULL,
		    &tctx.v);) {
			prof_tctx_merge_tdata(arg->tsdn, tctx.p, tdata);
		}

		arg->cnt_all.curobjs += tdata->cnt_summed.curobjs;
		arg->cnt_all.curbytes += tdata->cnt_summed.curbytes;
		if (opt_prof_accum) {
			arg->cnt_all.accumobjs += tdata->cnt_summed.accumobjs;
			arg->cnt_all.accumbytes += tdata->cnt_summed.accumbytes;
		}
	} else {
		tdata->dumping = false;
	}
	malloc_mutex_unlock(arg->tsdn, tdata->lock);

	return NULL;
}

static prof_tdata_t *
prof_tdata_dump_iter(prof_tdata_tree_t *tdatas, prof_tdata_t *tdata,
    void *arg) {
	bool propagate_err = *(bool *)arg;

	if (!tdata->dumping) {
		return NULL;
	}

	if (prof_dump_printf(propagate_err,
	    "  t%"FMTu64": %"FMTu64": %"FMTu64" [%"FMTu64": %"FMTu64"]%s%s\n",
	    tdata->thr_uid, tdata->cnt_summed.curobjs,
	    tdata->cnt_summed.curbytes, tdata->cnt_summed.accumobjs,
	    tdata->cnt_summed.accumbytes,
	    (tdata->thread_name != NULL) ? " " : "",
	    (tdata->thread_name != NULL) ? tdata->thread_name : "")) {
		return tdata;
	}
	return NULL;
}

static bool
prof_dump_header_impl(tsdn_t *tsdn, bool propagate_err,
    const prof_cnt_t *cnt_all) {
	bool ret;

	if (prof_dump_printf(propagate_err,
	    "heap_v2/%"FMTu64"\n"
	    "  t*: %"FMTu64": %"FMTu64" [%"FMTu64": %"FMTu64"]\n",
	    ((uint64_t)1U << lg_prof_sample), cnt_all->curobjs,
	    cnt_all->curbytes, cnt_all->accumobjs, cnt_all->accumbytes)) {
		return true;
	}

	malloc_mutex_lock(tsdn, &tdatas_mtx);
	ret = (tdata_tree_iter(&tdatas, NULL, prof_tdata_dump_iter,
	    (void *)&propagate_err) != NULL);
	malloc_mutex_unlock(tsdn, &tdatas_mtx);
	return ret;
}
prof_dump_header_t *JET_MUTABLE prof_dump_header = prof_dump_header_impl;

static bool
prof_dump_gctx(tsdn_t *tsdn, bool propagate_err, prof_gctx_t *gctx,
    const prof_bt_t *bt, prof_gctx_tree_t *gctxs) {
	bool ret;
	unsigned i;
	struct prof_tctx_dump_iter_arg_s prof_tctx_dump_iter_arg;

	cassert(config_prof);
	malloc_mutex_assert_owner(tsdn, gctx->lock);

	/* Avoid dumping such gctx's that have no useful data. */
	if ((!opt_prof_accum && gctx->cnt_summed.curobjs == 0) ||
	    (opt_prof_accum && gctx->cnt_summed.accumobjs == 0)) {
		assert(gctx->cnt_summed.curobjs == 0);
		assert(gctx->cnt_summed.curbytes == 0);
		assert(gctx->cnt_summed.accumobjs == 0);
		assert(gctx->cnt_summed.accumbytes == 0);
		ret = false;
		goto label_return;
	}

	if (prof_dump_printf(propagate_err, "@")) {
		ret = true;
		goto label_return;
	}
	for (i = 0; i < bt->len; i++) {
		if (prof_dump_printf(propagate_err, " %#"FMTxPTR,
		    (uintptr_t)bt->vec[i])) {
			ret = true;
			goto label_return;
		}
	}

	if (prof_dump_printf(propagate_err,
	    "\n"
	    "  t*: %"FMTu64": %"FMTu64" [%"FMTu64": %"FMTu64"]\n",
	    gctx->cnt_summed.curobjs, gctx->cnt_summed.curbytes,
	    gctx->cnt_summed.accumobjs, gctx->cnt_summed.accumbytes)) {
		ret = true;
		goto label_return;
	}

	prof_tctx_dump_iter_arg.tsdn = tsdn;
	prof_tctx_dump_iter_arg.propagate_err = propagate_err;
	if (tctx_tree_iter(&gctx->tctxs, NULL, prof_tctx_dump_iter,
	    (void *)&prof_tctx_dump_iter_arg) != NULL) {
		ret = true;
		goto label_return;
	}

	ret = false;
label_return:
	return ret;
}

#ifndef _WIN32
JEMALLOC_FORMAT_PRINTF(1, 2)
static int
prof_open_maps(const char *format, ...) {
	int mfd;
	va_list ap;
	char filename[PATH_MAX + 1];

	va_start(ap, format);
	malloc_vsnprintf(filename, sizeof(filename), format, ap);
	va_end(ap);

#if defined(O_CLOEXEC)
	mfd = open(filename, O_RDONLY | O_CLOEXEC);
#else
	mfd = open(filename, O_RDONLY);
	if (mfd != -1) {
		fcntl(mfd, F_SETFD, fcntl(mfd, F_GETFD) | FD_CLOEXEC);
	}
#endif

	return mfd;
}
#endif

static int
prof_getpid(void) {
#ifdef _WIN32
	return GetCurrentProcessId();
#else
	return getpid();
#endif
}

static bool
prof_dump_maps(bool propagate_err) {
	bool ret;
	int mfd;

	cassert(config_prof);
#ifdef __FreeBSD__
	mfd = prof_open_maps("/proc/curproc/map");
#elif defined(_WIN32)
	mfd = -1; // Not implemented
#else
	{
		int pid = prof_getpid();

		mfd = prof_open_maps("/proc/%d/task/%d/maps", pid, pid);
		if (mfd == -1) {
			mfd = prof_open_maps("/proc/%d/maps", pid);
		}
	}
#endif
	if (mfd != -1) {
		ssize_t nread;

		if (prof_dump_write(propagate_err, "\nMAPPED_LIBRARIES:\n") &&
		    propagate_err) {
			ret = true;
			goto label_return;
		}
		nread = 0;
		do {
			prof_dump_buf_end += nread;
			if (prof_dump_buf_end == PROF_DUMP_BUFSIZE) {
				/* Make space in prof_dump_buf before read(). */
				if (prof_dump_flush(propagate_err) &&
				    propagate_err) {
					ret = true;
					goto label_return;
				}
			}
			nread = malloc_read_fd(mfd,
			    &prof_dump_buf[prof_dump_buf_end], PROF_DUMP_BUFSIZE
			    - prof_dump_buf_end);
		} while (nread > 0);
	} else {
		ret = true;
		goto label_return;
	}

	ret = false;
label_return:
	if (mfd != -1) {
		close(mfd);
	}
	return ret;
}

/*
 * See prof_sample_threshold_update() comment for why the body of this function
 * is conditionally compiled.
 */
static void
prof_leakcheck(const prof_cnt_t *cnt_all, size_t leak_ngctx,
    const char *filename) {
#ifdef JEMALLOC_PROF
	/*
	 * Scaling is equivalent AdjustSamples() in jeprof, but the result may
	 * differ slightly from what jeprof reports, because here we scale the
	 * summary values, whereas jeprof scales each context individually and
	 * reports the sums of the scaled values.
	 */
	if (cnt_all->curbytes != 0) {
		double sample_period = (double)((uint64_t)1 << lg_prof_sample);
		double ratio = (((double)cnt_all->curbytes) /
		    (double)cnt_all->curobjs) / sample_period;
		double scale_factor = 1.0 / (1.0 - exp(-ratio));
		uint64_t curbytes = (uint64_t)round(((double)cnt_all->curbytes)
		    * scale_factor);
		uint64_t curobjs = (uint64_t)round(((double)cnt_all->curobjs) *
		    scale_factor);

		malloc_printf("<jemalloc>: Leak approximation summary: ~%"FMTu64
		    " byte%s, ~%"FMTu64" object%s, >= %zu context%s\n",
		    curbytes, (curbytes != 1) ? "s" : "", curobjs, (curobjs !=
		    1) ? "s" : "", leak_ngctx, (leak_ngctx != 1) ? "s" : "");
		malloc_printf(
		    "<jemalloc>: Run jeprof on \"%s\" for leak detail\n",
		    filename);
	}
#endif
}

struct prof_gctx_dump_iter_arg_s {
	tsdn_t	*tsdn;
	bool	propagate_err;
};

static prof_gctx_t *
prof_gctx_dump_iter(prof_gctx_tree_t *gctxs, prof_gctx_t *gctx, void *opaque) {
	prof_gctx_t *ret;
	struct prof_gctx_dump_iter_arg_s *arg =
	    (struct prof_gctx_dump_iter_arg_s *)opaque;

	malloc_mutex_lock(arg->tsdn, gctx->lock);

	if (prof_dump_gctx(arg->tsdn, arg->propagate_err, gctx, &gctx->bt,
	    gctxs)) {
		ret = gctx;
		goto label_return;
	}

	ret = NULL;
label_return:
	malloc_mutex_unlock(arg->tsdn, gctx->lock);
	return ret;
}

static void
prof_dump_prep(tsd_t *tsd, prof_tdata_t *tdata,
    struct prof_tdata_merge_iter_arg_s *prof_tdata_merge_iter_arg,
    struct prof_gctx_merge_iter_arg_s *prof_gctx_merge_iter_arg,
    prof_gctx_tree_t *gctxs) {
	size_t tabind;
	union {
		prof_gctx_t	*p;
		void		*v;
	} gctx;

	prof_enter(tsd, tdata);

	/*
	 * Put gctx's in limbo and clear their counters in preparation for
	 * summing.
	 */
	gctx_tree_new(gctxs);
	for (tabind = 0; !ckh_iter(&bt2gctx, &tabind, NULL, &gctx.v);) {
		prof_dump_gctx_prep(tsd_tsdn(tsd), gctx.p, gctxs);
	}

	/*
	 * Iterate over tdatas, and for the non-expired ones snapshot their tctx
	 * stats and merge them into the associated gctx's.
	 */
	prof_tdata_merge_iter_arg->tsdn = tsd_tsdn(tsd);
	memset(&prof_tdata_merge_iter_arg->cnt_all, 0, sizeof(prof_cnt_t));
	malloc_mutex_lock(tsd_tsdn(tsd), &tdatas_mtx);
	tdata_tree_iter(&tdatas, NULL, prof_tdata_merge_iter,
	    (void *)prof_tdata_merge_iter_arg);
	malloc_mutex_unlock(tsd_tsdn(tsd), &tdatas_mtx);

	/* Merge tctx stats into gctx's. */
	prof_gctx_merge_iter_arg->tsdn = tsd_tsdn(tsd);
	prof_gctx_merge_iter_arg->leak_ngctx = 0;
	gctx_tree_iter(gctxs, NULL, prof_gctx_merge_iter,
	    (void *)prof_gctx_merge_iter_arg);

	prof_leave(tsd, tdata);
}

static bool
prof_dump_file(tsd_t *tsd, bool propagate_err, const char *filename,
    bool leakcheck, prof_tdata_t *tdata,
    struct prof_tdata_merge_iter_arg_s *prof_tdata_merge_iter_arg,
    struct prof_gctx_merge_iter_arg_s *prof_gctx_merge_iter_arg,
    struct prof_gctx_dump_iter_arg_s *prof_gctx_dump_iter_arg,
    prof_gctx_tree_t *gctxs) {
	/* Create dump file. */
	if ((prof_dump_fd = prof_dump_open(propagate_err, filename)) == -1) {
		return true;
	}

	/* Dump profile header. */
	if (prof_dump_header(tsd_tsdn(tsd), propagate_err,
	    &prof_tdata_merge_iter_arg->cnt_all)) {
		goto label_write_error;
	}

	/* Dump per gctx profile stats. */
	prof_gctx_dump_iter_arg->tsdn = tsd_tsdn(tsd);
	prof_gctx_dump_iter_arg->propagate_err = propagate_err;
	if (gctx_tree_iter(gctxs, NULL, prof_gctx_dump_iter,
	    (void *)prof_gctx_dump_iter_arg) != NULL) {
		goto label_write_error;
	}

	/* Dump /proc/<pid>/maps if possible. */
	if (prof_dump_maps(propagate_err)) {
		goto label_write_error;
	}

	if (prof_dump_close(propagate_err)) {
		return true;
	}

	return false;
label_write_error:
	prof_dump_close(propagate_err);
	return true;
}

static bool
prof_dump(tsd_t *tsd, bool propagate_err, const char *filename,
    bool leakcheck) {
	cassert(config_prof);
	assert(tsd_reentrancy_level_get(tsd) == 0);

	prof_tdata_t * tdata = prof_tdata_get(tsd, true);
	if (tdata == NULL) {
		return true;
	}

	pre_reentrancy(tsd, NULL);
	malloc_mutex_lock(tsd_tsdn(tsd), &prof_dump_mtx);

	prof_gctx_tree_t gctxs;
	struct prof_tdata_merge_iter_arg_s prof_tdata_merge_iter_arg;
	struct prof_gctx_merge_iter_arg_s prof_gctx_merge_iter_arg;
	struct prof_gctx_dump_iter_arg_s prof_gctx_dump_iter_arg;
	prof_dump_prep(tsd, tdata, &prof_tdata_merge_iter_arg,
	    &prof_gctx_merge_iter_arg, &gctxs);
	bool err = prof_dump_file(tsd, propagate_err, filename, leakcheck, tdata,
	    &prof_tdata_merge_iter_arg, &prof_gctx_merge_iter_arg,
	    &prof_gctx_dump_iter_arg, &gctxs);
	prof_gctx_finish(tsd, &gctxs);

	malloc_mutex_unlock(tsd_tsdn(tsd), &prof_dump_mtx);
	post_reentrancy(tsd);

	if (err) {
		return true;
	}

	if (leakcheck) {
		prof_leakcheck(&prof_tdata_merge_iter_arg.cnt_all,
		    prof_gctx_merge_iter_arg.leak_ngctx, filename);
	}
	return false;
}

#ifdef JEMALLOC_JET
void
prof_cnt_all(uint64_t *curobjs, uint64_t *curbytes, uint64_t *accumobjs,
    uint64_t *accumbytes) {
	tsd_t *tsd;
	prof_tdata_t *tdata;
	struct prof_tdata_merge_iter_arg_s prof_tdata_merge_iter_arg;
	struct prof_gctx_merge_iter_arg_s prof_gctx_merge_iter_arg;
	prof_gctx_tree_t gctxs;

	tsd = tsd_fetch();
	tdata = prof_tdata_get(tsd, false);
	if (tdata == NULL) {
		if (curobjs != NULL) {
			*curobjs = 0;
		}
		if (curbytes != NULL) {
			*curbytes = 0;
		}
		if (accumobjs != NULL) {
			*accumobjs = 0;
		}
		if (accumbytes != NULL) {
			*accumbytes = 0;
		}
		return;
	}

	prof_dump_prep(tsd, tdata, &prof_tdata_merge_iter_arg,
	    &prof_gctx_merge_iter_arg, &gctxs);
	prof_gctx_finish(tsd, &gctxs);

	if (curobjs != NULL) {
		*curobjs = prof_tdata_merge_iter_arg.cnt_all.curobjs;
	}
	if (curbytes != NULL) {
		*curbytes = prof_tdata_merge_iter_arg.cnt_all.curbytes;
	}
	if (accumobjs != NULL) {
		*accumobjs = prof_tdata_merge_iter_arg.cnt_all.accumobjs;
	}
	if (accumbytes != NULL) {
		*accumbytes = prof_tdata_merge_iter_arg.cnt_all.accumbytes;
	}
}
#endif

#define DUMP_FILENAME_BUFSIZE	(PATH_MAX + 1)
#define VSEQ_INVALID		UINT64_C(0xffffffffffffffff)
static void
prof_dump_filename(char *filename, char v, uint64_t vseq) {
	cassert(config_prof);

	if (vseq != VSEQ_INVALID) {
	        /* "<prefix>.<pid>.<seq>.v<vseq>.heap" */
		malloc_snprintf(filename, DUMP_FILENAME_BUFSIZE,
		    "%s.%d.%"FMTu64".%c%"FMTu64".heap",
		    opt_prof_prefix, prof_getpid(), prof_dump_seq, v, vseq);
	} else {
	        /* "<prefix>.<pid>.<seq>.<v>.heap" */
		malloc_snprintf(filename, DUMP_FILENAME_BUFSIZE,
		    "%s.%d.%"FMTu64".%c.heap",
		    opt_prof_prefix, prof_getpid(), prof_dump_seq, v);
	}
	prof_dump_seq++;
}

static void
prof_fdump(void) {
	tsd_t *tsd;
	char filename[DUMP_FILENAME_BUFSIZE];

	cassert(config_prof);
	assert(opt_prof_final);
	assert(opt_prof_prefix[0] != '\0');

	if (!prof_booted) {
		return;
	}
	tsd = tsd_fetch();
	assert(tsd_reentrancy_level_get(tsd) == 0);

	malloc_mutex_lock(tsd_tsdn(tsd), &prof_dump_seq_mtx);
	prof_dump_filename(filename, 'f', VSEQ_INVALID);
	malloc_mutex_unlock(tsd_tsdn(tsd), &prof_dump_seq_mtx);
	prof_dump(tsd, false, filename, opt_prof_leak);
}

bool
prof_accum_init(tsdn_t *tsdn, prof_accum_t *prof_accum) {
	cassert(config_prof);

#ifndef JEMALLOC_ATOMIC_U64
	if (malloc_mutex_init(&prof_accum->mtx, "prof_accum",
	    WITNESS_RANK_PROF_ACCUM, malloc_mutex_rank_exclusive)) {
		return true;
	}
	prof_accum->accumbytes = 0;
#else
	atomic_store_u64(&prof_accum->accumbytes, 0, ATOMIC_RELAXED);
#endif
	return false;
}

void
prof_idump(tsdn_t *tsdn) {
	tsd_t *tsd;
	prof_tdata_t *tdata;

	cassert(config_prof);

	if (!prof_booted || tsdn_null(tsdn) || !prof_active_get_unlocked()) {
		return;
	}
	tsd = tsdn_tsd(tsdn);
	if (tsd_reentrancy_level_get(tsd) > 0) {
		return;
	}

	tdata = prof_tdata_get(tsd, false);
	if (tdata == NULL) {
		return;
	}
	if (tdata->enq) {
		tdata->enq_idump = true;
		return;
	}

	if (opt_prof_prefix[0] != '\0') {
		char filename[PATH_MAX + 1];
		malloc_mutex_lock(tsd_tsdn(tsd), &prof_dump_seq_mtx);
		prof_dump_filename(filename, 'i', prof_dump_iseq);
		prof_dump_iseq++;
		malloc_mutex_unlock(tsd_tsdn(tsd), &prof_dump_seq_mtx);
		prof_dump(tsd, false, filename, false);
	}
}

bool
prof_mdump(tsd_t *tsd, const char *filename) {
	cassert(config_prof);
	assert(tsd_reentrancy_level_get(tsd) == 0);

	if (!opt_prof || !prof_booted) {
		return true;
	}
	char filename_buf[DUMP_FILENAME_BUFSIZE];
	if (filename == NULL) {
		/* No filename specified, so automatically generate one. */
		if (opt_prof_prefix[0] == '\0') {
			return true;
		}
		malloc_mutex_lock(tsd_tsdn(tsd), &prof_dump_seq_mtx);
		prof_dump_filename(filename_buf, 'm', prof_dump_mseq);
		prof_dump_mseq++;
		malloc_mutex_unlock(tsd_tsdn(tsd), &prof_dump_seq_mtx);
		filename = filename_buf;
	}
	return prof_dump(tsd, true, filename, false);
}

void
prof_gdump(tsdn_t *tsdn) {
	tsd_t *tsd;
	prof_tdata_t *tdata;

	cassert(config_prof);

	if (!prof_booted || tsdn_null(tsdn) || !prof_active_get_unlocked()) {
		return;
	}
	tsd = tsdn_tsd(tsdn);
	if (tsd_reentrancy_level_get(tsd) > 0) {
		return;
	}

	tdata = prof_tdata_get(tsd, false);
	if (tdata == NULL) {
		return;
	}
	if (tdata->enq) {
		tdata->enq_gdump = true;
		return;
	}

	if (opt_prof_prefix[0] != '\0') {
		char filename[DUMP_FILENAME_BUFSIZE];
		malloc_mutex_lock(tsdn, &prof_dump_seq_mtx);
		prof_dump_filename(filename, 'u', prof_dump_useq);
		prof_dump_useq++;
		malloc_mutex_unlock(tsdn, &prof_dump_seq_mtx);
		prof_dump(tsd, false, filename, false);
	}
}

static void
prof_bt_hash(const void *key, size_t r_hash[2]) {
	prof_bt_t *bt = (prof_bt_t *)key;

	cassert(config_prof);

	hash(bt->vec, bt->len * sizeof(void *), 0x94122f33U, r_hash);
}

static bool
prof_bt_keycomp(const void *k1, const void *k2) {
	const prof_bt_t *bt1 = (prof_bt_t *)k1;
	const prof_bt_t *bt2 = (prof_bt_t *)k2;

	cassert(config_prof);

	if (bt1->len != bt2->len) {
		return false;
	}
	return (memcmp(bt1->vec, bt2->vec, bt1->len * sizeof(void *)) == 0);
}

static uint64_t
prof_thr_uid_alloc(tsdn_t *tsdn) {
	uint64_t thr_uid;

	malloc_mutex_lock(tsdn, &next_thr_uid_mtx);
	thr_uid = next_thr_uid;
	next_thr_uid++;
	malloc_mutex_unlock(tsdn, &next_thr_uid_mtx);

	return thr_uid;
}

static prof_tdata_t *
prof_tdata_init_impl(tsd_t *tsd, uint64_t thr_uid, uint64_t thr_discrim,
    char *thread_name, bool active) {
	prof_tdata_t *tdata;

	cassert(config_prof);

	/* Initialize an empty cache for this thread. */
	tdata = (prof_tdata_t *)iallocztm(tsd_tsdn(tsd), sizeof(prof_tdata_t),
	    sz_size2index(sizeof(prof_tdata_t)), false, NULL, true,
	    arena_get(TSDN_NULL, 0, true), true);
	if (tdata == NULL) {
		return NULL;
	}

	tdata->lock = prof_tdata_mutex_choose(thr_uid);
	tdata->thr_uid = thr_uid;
	tdata->thr_discrim = thr_discrim;
	tdata->thread_name = thread_name;
	tdata->attached = true;
	tdata->expired = false;
	tdata->tctx_uid_next = 0;

	if (ckh_new(tsd, &tdata->bt2tctx, PROF_CKH_MINITEMS, prof_bt_hash,
	    prof_bt_keycomp)) {
		idalloctm(tsd_tsdn(tsd), tdata, NULL, NULL, true, true);
		return NULL;
	}

	tdata->prng_state = (uint64_t)(uintptr_t)tdata;
	prof_sample_threshold_update(tdata);

	tdata->enq = false;
	tdata->enq_idump = false;
	tdata->enq_gdump = false;

	tdata->dumping = false;
	tdata->active = active;

	malloc_mutex_lock(tsd_tsdn(tsd), &tdatas_mtx);
	tdata_tree_insert(&tdatas, tdata);
	malloc_mutex_unlock(tsd_tsdn(tsd), &tdatas_mtx);

	return tdata;
}

prof_tdata_t *
prof_tdata_init(tsd_t *tsd) {
	return prof_tdata_init_impl(tsd, prof_thr_uid_alloc(tsd_tsdn(tsd)), 0,
	    NULL, prof_thread_active_init_get(tsd_tsdn(tsd)));
}

static bool
prof_tdata_should_destroy_unlocked(prof_tdata_t *tdata, bool even_if_attached) {
	if (tdata->attached && !even_if_attached) {
		return false;
	}
	if (ckh_count(&tdata->bt2tctx) != 0) {
		return false;
	}
	return true;
}

static bool
prof_tdata_should_destroy(tsdn_t *tsdn, prof_tdata_t *tdata,
    bool even_if_attached) {
	malloc_mutex_assert_owner(tsdn, tdata->lock);

	return prof_tdata_should_destroy_unlocked(tdata, even_if_attached);
}

static void
prof_tdata_destroy_locked(tsd_t *tsd, prof_tdata_t *tdata,
    bool even_if_attached) {
	malloc_mutex_assert_owner(tsd_tsdn(tsd), &tdatas_mtx);

	tdata_tree_remove(&tdatas, tdata);

	assert(prof_tdata_should_destroy_unlocked(tdata, even_if_attached));

	if (tdata->thread_name != NULL) {
		idalloctm(tsd_tsdn(tsd), tdata->thread_name, NULL, NULL, true,
		    true);
	}
	ckh_delete(tsd, &tdata->bt2tctx);
	idalloctm(tsd_tsdn(tsd), tdata, NULL, NULL, true, true);
}

static void
prof_tdata_destroy(tsd_t *tsd, prof_tdata_t *tdata, bool even_if_attached) {
	malloc_mutex_lock(tsd_tsdn(tsd), &tdatas_mtx);
	prof_tdata_destroy_locked(tsd, tdata, even_if_attached);
	malloc_mutex_unlock(tsd_tsdn(tsd), &tdatas_mtx);
}

static void
prof_tdata_detach(tsd_t *tsd, prof_tdata_t *tdata) {
	bool destroy_tdata;

	malloc_mutex_lock(tsd_tsdn(tsd), tdata->lock);
	if (tdata->attached) {
		destroy_tdata = prof_tdata_should_destroy(tsd_tsdn(tsd), tdata,
		    true);
		/*
		 * Only detach if !destroy_tdata, because detaching would allow
		 * another thread to win the race to destroy tdata.
		 */
		if (!destroy_tdata) {
			tdata->attached = false;
		}
		tsd_prof_tdata_set(tsd, NULL);
	} else {
		destroy_tdata = false;
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), tdata->lock);
	if (destroy_tdata) {
		prof_tdata_destroy(tsd, tdata, true);
	}
}

prof_tdata_t *
prof_tdata_reinit(tsd_t *tsd, prof_tdata_t *tdata) {
	uint64_t thr_uid = tdata->thr_uid;
	uint64_t thr_discrim = tdata->thr_discrim + 1;
	char *thread_name = (tdata->thread_name != NULL) ?
	    prof_thread_name_alloc(tsd_tsdn(tsd), tdata->thread_name) : NULL;
	bool active = tdata->active;

	prof_tdata_detach(tsd, tdata);
	return prof_tdata_init_impl(tsd, thr_uid, thr_discrim, thread_name,
	    active);
}

static bool
prof_tdata_expire(tsdn_t *tsdn, prof_tdata_t *tdata) {
	bool destroy_tdata;

	malloc_mutex_lock(tsdn, tdata->lock);
	if (!tdata->expired) {
		tdata->expired = true;
		destroy_tdata = tdata->attached ? false :
		    prof_tdata_should_destroy(tsdn, tdata, false);
	} else {
		destroy_tdata = false;
	}
	malloc_mutex_unlock(tsdn, tdata->lock);

	return destroy_tdata;
}

static prof_tdata_t *
prof_tdata_reset_iter(prof_tdata_tree_t *tdatas, prof_tdata_t *tdata,
    void *arg) {
	tsdn_t *tsdn = (tsdn_t *)arg;

	return (prof_tdata_expire(tsdn, tdata) ? tdata : NULL);
}

void
prof_reset(tsd_t *tsd, size_t lg_sample) {
	prof_tdata_t *next;

	assert(lg_sample < (sizeof(uint64_t) << 3));

	malloc_mutex_lock(tsd_tsdn(tsd), &prof_dump_mtx);
	malloc_mutex_lock(tsd_tsdn(tsd), &tdatas_mtx);

	lg_prof_sample = lg_sample;

	next = NULL;
	do {
		prof_tdata_t *to_destroy = tdata_tree_iter(&tdatas, next,
		    prof_tdata_reset_iter, (void *)tsd);
		if (to_destroy != NULL) {
			next = tdata_tree_next(&tdatas, to_destroy);
			prof_tdata_destroy_locked(tsd, to_destroy, false);
		} else {
			next = NULL;
		}
	} while (next != NULL);

	malloc_mutex_unlock(tsd_tsdn(tsd), &tdatas_mtx);
	malloc_mutex_unlock(tsd_tsdn(tsd), &prof_dump_mtx);
}

void
prof_tdata_cleanup(tsd_t *tsd) {
	prof_tdata_t *tdata;

	if (!config_prof) {
		return;
	}

	tdata = tsd_prof_tdata_get(tsd);
	if (tdata != NULL) {
		prof_tdata_detach(tsd, tdata);
	}
}

bool
prof_active_get(tsdn_t *tsdn) {
	bool prof_active_current;

	malloc_mutex_lock(tsdn, &prof_active_mtx);
	prof_active_current = prof_active;
	malloc_mutex_unlock(tsdn, &prof_active_mtx);
	return prof_active_current;
}

bool
prof_active_set(tsdn_t *tsdn, bool active) {
	bool prof_active_old;

	malloc_mutex_lock(tsdn, &prof_active_mtx);
	prof_active_old = prof_active;
	prof_active = active;
	malloc_mutex_unlock(tsdn, &prof_active_mtx);
	return prof_active_old;
}

const char *
prof_thread_name_get(tsd_t *tsd) {
	prof_tdata_t *tdata;

	tdata = prof_tdata_get(tsd, true);
	if (tdata == NULL) {
		return "";
	}
	return (tdata->thread_name != NULL ? tdata->thread_name : "");
}

static char *
prof_thread_name_alloc(tsdn_t *tsdn, const char *thread_name) {
	char *ret;
	size_t size;

	if (thread_name == NULL) {
		return NULL;
	}

	size = strlen(thread_name) + 1;
	if (size == 1) {
		return "";
	}

	ret = iallocztm(tsdn, size, sz_size2index(size), false, NULL, true,
	    arena_get(TSDN_NULL, 0, true), true);
	if (ret == NULL) {
		return NULL;
	}
	memcpy(ret, thread_name, size);
	return ret;
}

int
prof_thread_name_set(tsd_t *tsd, const char *thread_name) {
	prof_tdata_t *tdata;
	unsigned i;
	char *s;

	tdata = prof_tdata_get(tsd, true);
	if (tdata == NULL) {
		return EAGAIN;
	}

	/* Validate input. */
	if (thread_name == NULL) {
		return EFAULT;
	}
	for (i = 0; thread_name[i] != '\0'; i++) {
		char c = thread_name[i];
		if (!isgraph(c) && !isblank(c)) {
			return EFAULT;
		}
	}

	s = prof_thread_name_alloc(tsd_tsdn(tsd), thread_name);
	if (s == NULL) {
		return EAGAIN;
	}

	if (tdata->thread_name != NULL) {
		idalloctm(tsd_tsdn(tsd), tdata->thread_name, NULL, NULL, true,
		    true);
		tdata->thread_name = NULL;
	}
	if (strlen(s) > 0) {
		tdata->thread_name = s;
	}
	return 0;
}

bool
prof_thread_active_get(tsd_t *tsd) {
	prof_tdata_t *tdata;

	tdata = prof_tdata_get(tsd, true);
	if (tdata == NULL) {
		return false;
	}
	return tdata->active;
}

bool
prof_thread_active_set(tsd_t *tsd, bool active) {
	prof_tdata_t *tdata;

	tdata = prof_tdata_get(tsd, true);
	if (tdata == NULL) {
		return true;
	}
	tdata->active = active;
	return false;
}

bool
prof_thread_active_init_get(tsdn_t *tsdn) {
	bool active_init;

	malloc_mutex_lock(tsdn, &prof_thread_active_init_mtx);
	active_init = prof_thread_active_init;
	malloc_mutex_unlock(tsdn, &prof_thread_active_init_mtx);
	return active_init;
}

bool
prof_thread_active_init_set(tsdn_t *tsdn, bool active_init) {
	bool active_init_old;

	malloc_mutex_lock(tsdn, &prof_thread_active_init_mtx);
	active_init_old = prof_thread_active_init;
	prof_thread_active_init = active_init;
	malloc_mutex_unlock(tsdn, &prof_thread_active_init_mtx);
	return active_init_old;
}

bool
prof_gdump_get(tsdn_t *tsdn) {
	bool prof_gdump_current;

	malloc_mutex_lock(tsdn, &prof_gdump_mtx);
	prof_gdump_current = prof_gdump_val;
	malloc_mutex_unlock(tsdn, &prof_gdump_mtx);
	return prof_gdump_current;
}

bool
prof_gdump_set(tsdn_t *tsdn, bool gdump) {
	bool prof_gdump_old;

	malloc_mutex_lock(tsdn, &prof_gdump_mtx);
	prof_gdump_old = prof_gdump_val;
	prof_gdump_val = gdump;
	malloc_mutex_unlock(tsdn, &prof_gdump_mtx);
	return prof_gdump_old;
}

void
prof_boot0(void) {
	cassert(config_prof);

	memcpy(opt_prof_prefix, PROF_PREFIX_DEFAULT,
	    sizeof(PROF_PREFIX_DEFAULT));
}

void
prof_boot1(void) {
	cassert(config_prof);

	/*
	 * opt_prof must be in its final state before any arenas are
	 * initialized, so this function must be executed early.
	 */

	if (opt_prof_leak && !opt_prof) {
		/*
		 * Enable opt_prof, but in such a way that profiles are never
		 * automatically dumped.
		 */
		opt_prof = true;
		opt_prof_gdump = false;
	} else if (opt_prof) {
		if (opt_lg_prof_interval >= 0) {
			prof_interval = (((uint64_t)1U) <<
			    opt_lg_prof_interval);
		}
	}
}

bool
prof_boot2(tsd_t *tsd) {
	cassert(config_prof);

	if (opt_prof) {
		unsigned i;

		lg_prof_sample = opt_lg_prof_sample;

		prof_active = opt_prof_active;
		if (malloc_mutex_init(&prof_active_mtx, "prof_active",
		    WITNESS_RANK_PROF_ACTIVE, malloc_mutex_rank_exclusive)) {
			return true;
		}

		prof_gdump_val = opt_prof_gdump;
		if (malloc_mutex_init(&prof_gdump_mtx, "prof_gdump",
		    WITNESS_RANK_PROF_GDUMP, malloc_mutex_rank_exclusive)) {
			return true;
		}

		prof_thread_active_init = opt_prof_thread_active_init;
		if (malloc_mutex_init(&prof_thread_active_init_mtx,
		    "prof_thread_active_init",
		    WITNESS_RANK_PROF_THREAD_ACTIVE_INIT,
		    malloc_mutex_rank_exclusive)) {
			return true;
		}

		if (ckh_new(tsd, &bt2gctx, PROF_CKH_MINITEMS, prof_bt_hash,
		    prof_bt_keycomp)) {
			return true;
		}
		if (malloc_mutex_init(&bt2gctx_mtx, "prof_bt2gctx",
		    WITNESS_RANK_PROF_BT2GCTX, malloc_mutex_rank_exclusive)) {
			return true;
		}

		tdata_tree_new(&tdatas);
		if (malloc_mutex_init(&tdatas_mtx, "prof_tdatas",
		    WITNESS_RANK_PROF_TDATAS, malloc_mutex_rank_exclusive)) {
			return true;
		}

		next_thr_uid = 0;
		if (malloc_mutex_init(&next_thr_uid_mtx, "prof_next_thr_uid",
		    WITNESS_RANK_PROF_NEXT_THR_UID, malloc_mutex_rank_exclusive)) {
			return true;
		}

		if (malloc_mutex_init(&prof_dump_seq_mtx, "prof_dump_seq",
		    WITNESS_RANK_PROF_DUMP_SEQ, malloc_mutex_rank_exclusive)) {
			return true;
		}
		if (malloc_mutex_init(&prof_dump_mtx, "prof_dump",
		    WITNESS_RANK_PROF_DUMP, malloc_mutex_rank_exclusive)) {
			return true;
		}

		if (opt_prof_final && opt_prof_prefix[0] != '\0' &&
		    atexit(prof_fdump) != 0) {
			malloc_write("<jemalloc>: Error in atexit()\n");
			if (opt_abort) {
				abort();
			}
		}

		gctx_locks = (malloc_mutex_t *)base_alloc(tsd_tsdn(tsd),
		    b0get(), PROF_NCTX_LOCKS * sizeof(malloc_mutex_t),
		    CACHELINE);
		if (gctx_locks == NULL) {
			return true;
		}
		for (i = 0; i < PROF_NCTX_LOCKS; i++) {
			if (malloc_mutex_init(&gctx_locks[i], "prof_gctx",
			    WITNESS_RANK_PROF_GCTX,
			    malloc_mutex_rank_exclusive)) {
				return true;
			}
		}

		tdata_locks = (malloc_mutex_t *)base_alloc(tsd_tsdn(tsd),
		    b0get(), PROF_NTDATA_LOCKS * sizeof(malloc_mutex_t),
		    CACHELINE);
		if (tdata_locks == NULL) {
			return true;
		}
		for (i = 0; i < PROF_NTDATA_LOCKS; i++) {
			if (malloc_mutex_init(&tdata_locks[i], "prof_tdata",
			    WITNESS_RANK_PROF_TDATA,
			    malloc_mutex_rank_exclusive)) {
				return true;
			}
		}
	}

#ifdef JEMALLOC_PROF_LIBGCC
	/*
	 * Cause the backtracing machinery to allocate its internal state
	 * before enabling profiling.
	 */
	_Unwind_Backtrace(prof_unwind_init_callback, NULL);
#endif

	prof_booted = true;

	return false;
}

void
prof_prefork0(tsdn_t *tsdn) {
	if (config_prof && opt_prof) {
		unsigned i;

		malloc_mutex_prefork(tsdn, &prof_dump_mtx);
		malloc_mutex_prefork(tsdn, &bt2gctx_mtx);
		malloc_mutex_prefork(tsdn, &tdatas_mtx);
		for (i = 0; i < PROF_NTDATA_LOCKS; i++) {
			malloc_mutex_prefork(tsdn, &tdata_locks[i]);
		}
		for (i = 0; i < PROF_NCTX_LOCKS; i++) {
			malloc_mutex_prefork(tsdn, &gctx_locks[i]);
		}
	}
}

void
prof_prefork1(tsdn_t *tsdn) {
	if (config_prof && opt_prof) {
		malloc_mutex_prefork(tsdn, &prof_active_mtx);
		malloc_mutex_prefork(tsdn, &prof_dump_seq_mtx);
		malloc_mutex_prefork(tsdn, &prof_gdump_mtx);
		malloc_mutex_prefork(tsdn, &next_thr_uid_mtx);
		malloc_mutex_prefork(tsdn, &prof_thread_active_init_mtx);
	}
}

void
prof_postfork_parent(tsdn_t *tsdn) {
	if (config_prof && opt_prof) {
		unsigned i;

		malloc_mutex_postfork_parent(tsdn,
		    &prof_thread_active_init_mtx);
		malloc_mutex_postfork_parent(tsdn, &next_thr_uid_mtx);
		malloc_mutex_postfork_parent(tsdn, &prof_gdump_mtx);
		malloc_mutex_postfork_parent(tsdn, &prof_dump_seq_mtx);
		malloc_mutex_postfork_parent(tsdn, &prof_active_mtx);
		for (i = 0; i < PROF_NCTX_LOCKS; i++) {
			malloc_mutex_postfork_parent(tsdn, &gctx_locks[i]);
		}
		for (i = 0; i < PROF_NTDATA_LOCKS; i++) {
			malloc_mutex_postfork_parent(tsdn, &tdata_locks[i]);
		}
		malloc_mutex_postfork_parent(tsdn, &tdatas_mtx);
		malloc_mutex_postfork_parent(tsdn, &bt2gctx_mtx);
		malloc_mutex_postfork_parent(tsdn, &prof_dump_mtx);
	}
}

void
prof_postfork_child(tsdn_t *tsdn) {
	if (config_prof && opt_prof) {
		unsigned i;

		malloc_mutex_postfork_child(tsdn, &prof_thread_active_init_mtx);
		malloc_mutex_postfork_child(tsdn, &next_thr_uid_mtx);
		malloc_mutex_postfork_child(tsdn, &prof_gdump_mtx);
		malloc_mutex_postfork_child(tsdn, &prof_dump_seq_mtx);
		malloc_mutex_postfork_child(tsdn, &prof_active_mtx);
		for (i = 0; i < PROF_NCTX_LOCKS; i++) {
			malloc_mutex_postfork_child(tsdn, &gctx_locks[i]);
		}
		for (i = 0; i < PROF_NTDATA_LOCKS; i++) {
			malloc_mutex_postfork_child(tsdn, &tdata_locks[i]);
		}
		malloc_mutex_postfork_child(tsdn, &tdatas_mtx);
		malloc_mutex_postfork_child(tsdn, &bt2gctx_mtx);
		malloc_mutex_postfork_child(tsdn, &prof_dump_mtx);
	}
}

/******************************************************************************/
