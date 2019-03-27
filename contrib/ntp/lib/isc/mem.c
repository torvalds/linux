/*
 * Copyright (C) 2004-2010, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1997-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <limits.h>

#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/msgs.h>
#include <isc/once.h>
#include <isc/ondestroy.h>
#include <isc/string.h>
#include <isc/mutex.h>
#include <isc/print.h>
#include <isc/util.h>
#include <isc/xml.h>

#define MCTXLOCK(m, l) if (((m)->flags & ISC_MEMFLAG_NOLOCK) == 0) LOCK(l)
#define MCTXUNLOCK(m, l) if (((m)->flags & ISC_MEMFLAG_NOLOCK) == 0) UNLOCK(l)

#ifndef ISC_MEM_DEBUGGING
#define ISC_MEM_DEBUGGING 0
#endif
LIBISC_EXTERNAL_DATA unsigned int isc_mem_debugging = ISC_MEM_DEBUGGING;

/*
 * Constants.
 */

#define DEF_MAX_SIZE		1100
#define DEF_MEM_TARGET		4096
#define ALIGNMENT_SIZE		8U		/*%< must be a power of 2 */
#define NUM_BASIC_BLOCKS	64		/*%< must be > 1 */
#define TABLE_INCREMENT		1024
#define DEBUGLIST_COUNT		1024

/*
 * Types.
 */
typedef struct isc__mem isc__mem_t;
typedef struct isc__mempool isc__mempool_t;

#if ISC_MEM_TRACKLINES
typedef struct debuglink debuglink_t;
struct debuglink {
	ISC_LINK(debuglink_t)	link;
	const void	       *ptr[DEBUGLIST_COUNT];
	unsigned int		size[DEBUGLIST_COUNT];
	const char	       *file[DEBUGLIST_COUNT];
	unsigned int		line[DEBUGLIST_COUNT];
	unsigned int		count;
};

#define FLARG_PASS	, file, line
#define FLARG		, const char *file, unsigned int line
#else
#define FLARG_PASS
#define FLARG
#endif

typedef struct element element;
struct element {
	element *		next;
};

typedef struct {
	/*!
	 * This structure must be ALIGNMENT_SIZE bytes.
	 */
	union {
		size_t		size;
		isc__mem_t	*ctx;
		char		bytes[ALIGNMENT_SIZE];
	} u;
} size_info;

struct stats {
	unsigned long		gets;
	unsigned long		totalgets;
	unsigned long		blocks;
	unsigned long		freefrags;
};

#define MEM_MAGIC		ISC_MAGIC('M', 'e', 'm', 'C')
#define VALID_CONTEXT(c)	ISC_MAGIC_VALID(c, MEM_MAGIC)

#if ISC_MEM_TRACKLINES
typedef ISC_LIST(debuglink_t)	debuglist_t;
#endif

/* List of all active memory contexts. */

static ISC_LIST(isc__mem_t)	contexts;
static isc_once_t		once = ISC_ONCE_INIT;
static isc_mutex_t		lock;

/*%
 * Total size of lost memory due to a bug of external library.
 * Locked by the global lock.
 */
static isc_uint64_t		totallost;

struct isc__mem {
	isc_mem_t		common;
	isc_ondestroy_t		ondestroy;
	unsigned int		flags;
	isc_mutex_t		lock;
	isc_memalloc_t		memalloc;
	isc_memfree_t		memfree;
	void *			arg;
	size_t			max_size;
	isc_boolean_t		checkfree;
	struct stats *		stats;
	unsigned int		references;
	char			name[16];
	void *			tag;
	size_t			quota;
	size_t			total;
	size_t			inuse;
	size_t			maxinuse;
	size_t			hi_water;
	size_t			lo_water;
	isc_boolean_t		hi_called;
	isc_boolean_t		is_overmem;
	isc_mem_water_t		water;
	void *			water_arg;
	ISC_LIST(isc__mempool_t) pools;
	unsigned int		poolcnt;

	/*  ISC_MEMFLAG_INTERNAL */
	size_t			mem_target;
	element **		freelists;
	element *		basic_blocks;
	unsigned char **	basic_table;
	unsigned int		basic_table_count;
	unsigned int		basic_table_size;
	unsigned char *		lowest;
	unsigned char *		highest;

#if ISC_MEM_TRACKLINES
	debuglist_t *	 	debuglist;
	unsigned int		debuglistcnt;
#endif

	unsigned int		memalloc_failures;
	ISC_LINK(isc__mem_t)	link;
};

#define MEMPOOL_MAGIC		ISC_MAGIC('M', 'E', 'M', 'p')
#define VALID_MEMPOOL(c)	ISC_MAGIC_VALID(c, MEMPOOL_MAGIC)

struct isc__mempool {
	/* always unlocked */
	isc_mempool_t	common;		/*%< common header of mempool's */
	isc_mutex_t    *lock;		/*%< optional lock */
	isc__mem_t      *mctx;		/*%< our memory context */
	/*%< locked via the memory context's lock */
	ISC_LINK(isc__mempool_t)	link;	/*%< next pool in this mem context */
	/*%< optionally locked from here down */
	element	       *items;		/*%< low water item list */
	size_t		size;		/*%< size of each item on this pool */
	unsigned int	maxalloc;	/*%< max number of items allowed */
	unsigned int	allocated;	/*%< # of items currently given out */
	unsigned int	freecount;	/*%< # of items on reserved list */
	unsigned int	freemax;	/*%< # of items allowed on free list */
	unsigned int	fillcount;	/*%< # of items to fetch on each fill */
	/*%< Stats only. */
	unsigned int	gets;		/*%< # of requests to this pool */
	/*%< Debugging only. */
#if ISC_MEMPOOL_NAMES
	char		name[16];	/*%< printed name in stats reports */
#endif
};

/*
 * Private Inline-able.
 */

#if ! ISC_MEM_TRACKLINES
#define ADD_TRACE(a, b, c, d, e)
#define DELETE_TRACE(a, b, c, d, e)
#else
#define ADD_TRACE(a, b, c, d, e) \
	do { \
		if ((isc_mem_debugging & (ISC_MEM_DEBUGTRACE | \
					  ISC_MEM_DEBUGRECORD)) != 0 && \
		     b != NULL) \
			 add_trace_entry(a, b, c, d, e); \
	} while (0)
#define DELETE_TRACE(a, b, c, d, e)	delete_trace_entry(a, b, c, d, e)

static void
print_active(isc__mem_t *ctx, FILE *out);

/*%
 * The following can be either static or public, depending on build environment.
 */

#ifdef BIND9
#define ISC_MEMFUNC_SCOPE
#else
#define ISC_MEMFUNC_SCOPE static
#endif

ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_createx(size_t init_max_size, size_t target_size,
		 isc_memalloc_t memalloc, isc_memfree_t memfree, void *arg,
		 isc_mem_t **ctxp);
ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_createx2(size_t init_max_size, size_t target_size,
		  isc_memalloc_t memalloc, isc_memfree_t memfree, void *arg,
		  isc_mem_t **ctxp, unsigned int flags);
ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_create(size_t init_max_size, size_t target_size, isc_mem_t **ctxp);
ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_create2(size_t init_max_size, size_t target_size,
		 isc_mem_t **ctxp, unsigned int flags);
ISC_MEMFUNC_SCOPE void
isc__mem_attach(isc_mem_t *source, isc_mem_t **targetp);
ISC_MEMFUNC_SCOPE void
isc__mem_detach(isc_mem_t **ctxp);
ISC_MEMFUNC_SCOPE void
isc___mem_putanddetach(isc_mem_t **ctxp, void *ptr, size_t size FLARG);
ISC_MEMFUNC_SCOPE void
isc__mem_destroy(isc_mem_t **ctxp);
ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_ondestroy(isc_mem_t *ctx, isc_task_t *task, isc_event_t **event);
ISC_MEMFUNC_SCOPE void *
isc___mem_get(isc_mem_t *ctx, size_t size FLARG);
ISC_MEMFUNC_SCOPE void
isc___mem_put(isc_mem_t *ctx, void *ptr, size_t size FLARG);
ISC_MEMFUNC_SCOPE void
isc__mem_stats(isc_mem_t *ctx, FILE *out);
ISC_MEMFUNC_SCOPE void *
isc___mem_allocate(isc_mem_t *ctx, size_t size FLARG);
ISC_MEMFUNC_SCOPE void *
isc___mem_reallocate(isc_mem_t *ctx, void *ptr, size_t size FLARG);
ISC_MEMFUNC_SCOPE void
isc___mem_free(isc_mem_t *ctx, void *ptr FLARG);
ISC_MEMFUNC_SCOPE char *
isc___mem_strdup(isc_mem_t *mctx, const char *s FLARG);
ISC_MEMFUNC_SCOPE void
isc__mem_setdestroycheck(isc_mem_t *ctx, isc_boolean_t flag);
ISC_MEMFUNC_SCOPE void
isc__mem_setquota(isc_mem_t *ctx, size_t quota);
ISC_MEMFUNC_SCOPE size_t
isc__mem_getquota(isc_mem_t *ctx);
ISC_MEMFUNC_SCOPE size_t
isc__mem_inuse(isc_mem_t *ctx);
ISC_MEMFUNC_SCOPE isc_boolean_t
isc__mem_isovermem(isc_mem_t *ctx);
ISC_MEMFUNC_SCOPE void
isc__mem_setwater(isc_mem_t *ctx, isc_mem_water_t water, void *water_arg,
		  size_t hiwater, size_t lowater);
ISC_MEMFUNC_SCOPE void
isc__mem_waterack(isc_mem_t *ctx0, int flag);
ISC_MEMFUNC_SCOPE void
isc__mem_setname(isc_mem_t *ctx, const char *name, void *tag);
ISC_MEMFUNC_SCOPE const char *
isc__mem_getname(isc_mem_t *ctx);
ISC_MEMFUNC_SCOPE void *
isc__mem_gettag(isc_mem_t *ctx);
ISC_MEMFUNC_SCOPE isc_result_t
isc__mempool_create(isc_mem_t *mctx, size_t size, isc_mempool_t **mpctxp);
ISC_MEMFUNC_SCOPE void
isc__mempool_setname(isc_mempool_t *mpctx, const char *name);
ISC_MEMFUNC_SCOPE void
isc__mempool_destroy(isc_mempool_t **mpctxp);
ISC_MEMFUNC_SCOPE void
isc__mempool_associatelock(isc_mempool_t *mpctx, isc_mutex_t *lock);
ISC_MEMFUNC_SCOPE void *
isc___mempool_get(isc_mempool_t *mpctx FLARG);
ISC_MEMFUNC_SCOPE void
isc___mempool_put(isc_mempool_t *mpctx, void *mem FLARG);
ISC_MEMFUNC_SCOPE void
isc__mempool_setfreemax(isc_mempool_t *mpctx, unsigned int limit);
ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getfreemax(isc_mempool_t *mpctx);
ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getfreecount(isc_mempool_t *mpctx);
ISC_MEMFUNC_SCOPE void
isc__mempool_setmaxalloc(isc_mempool_t *mpctx, unsigned int limit);
ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getmaxalloc(isc_mempool_t *mpctx);
ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getallocated(isc_mempool_t *mpctx);
ISC_MEMFUNC_SCOPE void
isc__mempool_setfillcount(isc_mempool_t *mpctx, unsigned int limit);
ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getfillcount(isc_mempool_t *mpctx);
#ifdef BIND9
ISC_MEMFUNC_SCOPE void
isc__mem_printactive(isc_mem_t *ctx0, FILE *file);
ISC_MEMFUNC_SCOPE void
isc__mem_printallactive(FILE *file);
ISC_MEMFUNC_SCOPE void
isc__mem_checkdestroyed(FILE *file);
ISC_MEMFUNC_SCOPE unsigned int
isc__mem_references(isc_mem_t *ctx0);
#endif

static struct isc__memmethods {
	isc_memmethods_t methods;

	/*%
	 * The following are defined just for avoiding unused static functions.
	 */
#ifndef BIND9
	void *createx, *create, *create2, *ondestroy, *stats,
		*setquota, *getquota, *setname, *getname, *gettag;
#endif
} memmethods = {
	{
		isc__mem_attach,
		isc__mem_detach,
		isc__mem_destroy,
		isc___mem_get,
		isc___mem_put,
		isc___mem_putanddetach,
		isc___mem_allocate,
		isc___mem_reallocate,
		isc___mem_strdup,
		isc___mem_free,
		isc__mem_setdestroycheck,
		isc__mem_setwater,
		isc__mem_waterack,
		isc__mem_inuse,
		isc__mem_isovermem,
		isc__mempool_create
	}
#ifndef BIND9
	,
	(void *)isc__mem_createx, (void *)isc__mem_create,
	(void *)isc__mem_create2, (void *)isc__mem_ondestroy,
	(void *)isc__mem_stats, (void *)isc__mem_setquota,
	(void *)isc__mem_getquota, (void *)isc__mem_setname,
	(void *)isc__mem_getname, (void *)isc__mem_gettag
#endif
};

static struct isc__mempoolmethods {
	isc_mempoolmethods_t methods;

	/*%
	 * The following are defined just for avoiding unused static functions.
	 */
#ifndef BIND9
	void *getfreemax, *getfreecount, *getmaxalloc, *getfillcount;
#endif
} mempoolmethods = {
	{
		isc__mempool_destroy,
		isc___mempool_get,
		isc___mempool_put,
		isc__mempool_getallocated,
		isc__mempool_setmaxalloc,
		isc__mempool_setfreemax,
		isc__mempool_setname,
		isc__mempool_associatelock,
		isc__mempool_setfillcount
	}
#ifndef BIND9
	,
	(void *)isc__mempool_getfreemax, (void *)isc__mempool_getfreecount,
	(void *)isc__mempool_getmaxalloc, (void *)isc__mempool_getfillcount
#endif
};

/*!
 * mctx must be locked.
 */
static inline void
add_trace_entry(isc__mem_t *mctx, const void *ptr, unsigned int size
		FLARG)
{
	debuglink_t *dl;
	unsigned int i;
	unsigned int mysize = size;

	if ((isc_mem_debugging & ISC_MEM_DEBUGTRACE) != 0)
		fprintf(stderr, isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
					       ISC_MSG_ADDTRACE,
					       "add %p size %u "
					       "file %s line %u mctx %p\n"),
			ptr, size, file, line, mctx);

	if (mctx->debuglist == NULL)
		return;

	if (mysize > mctx->max_size)
		mysize = mctx->max_size;

	dl = ISC_LIST_HEAD(mctx->debuglist[mysize]);
	while (dl != NULL) {
		if (dl->count == DEBUGLIST_COUNT)
			goto next;
		for (i = 0; i < DEBUGLIST_COUNT; i++) {
			if (dl->ptr[i] == NULL) {
				dl->ptr[i] = ptr;
				dl->size[i] = size;
				dl->file[i] = file;
				dl->line[i] = line;
				dl->count++;
				return;
			}
		}
	next:
		dl = ISC_LIST_NEXT(dl, link);
	}

	dl = malloc(sizeof(debuglink_t));
	INSIST(dl != NULL);

	ISC_LINK_INIT(dl, link);
	for (i = 1; i < DEBUGLIST_COUNT; i++) {
		dl->ptr[i] = NULL;
		dl->size[i] = 0;
		dl->file[i] = NULL;
		dl->line[i] = 0;
	}

	dl->ptr[0] = ptr;
	dl->size[0] = size;
	dl->file[0] = file;
	dl->line[0] = line;
	dl->count = 1;

	ISC_LIST_PREPEND(mctx->debuglist[mysize], dl, link);
	mctx->debuglistcnt++;
}

static inline void
delete_trace_entry(isc__mem_t *mctx, const void *ptr, unsigned int size,
		   const char *file, unsigned int line)
{
	debuglink_t *dl;
	unsigned int i;

	if ((isc_mem_debugging & ISC_MEM_DEBUGTRACE) != 0)
		fprintf(stderr, isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
					       ISC_MSG_DELTRACE,
					       "del %p size %u "
					       "file %s line %u mctx %p\n"),
			ptr, size, file, line, mctx);

	if (mctx->debuglist == NULL)
		return;

	if (size > mctx->max_size)
		size = mctx->max_size;

	dl = ISC_LIST_HEAD(mctx->debuglist[size]);
	while (dl != NULL) {
		for (i = 0; i < DEBUGLIST_COUNT; i++) {
			if (dl->ptr[i] == ptr) {
				dl->ptr[i] = NULL;
				dl->size[i] = 0;
				dl->file[i] = NULL;
				dl->line[i] = 0;

				INSIST(dl->count > 0);
				dl->count--;
				if (dl->count == 0) {
					ISC_LIST_UNLINK(mctx->debuglist[size],
							dl, link);
					free(dl);
				}
				return;
			}
		}
		dl = ISC_LIST_NEXT(dl, link);
	}

	/*
	 * If we get here, we didn't find the item on the list.  We're
	 * screwed.
	 */
	INSIST(dl != NULL);
}
#endif /* ISC_MEM_TRACKLINES */

static inline size_t
rmsize(size_t size) {
	/*
	 * round down to ALIGNMENT_SIZE
	 */
	return (size & (~(ALIGNMENT_SIZE - 1)));
}

static inline size_t
quantize(size_t size) {
	/*!
	 * Round up the result in order to get a size big
	 * enough to satisfy the request and be aligned on ALIGNMENT_SIZE
	 * byte boundaries.
	 */

	if (size == 0U)
		return (ALIGNMENT_SIZE);
	return ((size + ALIGNMENT_SIZE - 1) & (~(ALIGNMENT_SIZE - 1)));
}

static inline isc_boolean_t
more_basic_blocks(isc__mem_t *ctx) {
	void *new;
	unsigned char *curr, *next;
	unsigned char *first, *last;
	unsigned char **table;
	unsigned int table_size;
	size_t increment;
	int i;

	/* Require: we hold the context lock. */

	/*
	 * Did we hit the quota for this context?
	 */
	increment = NUM_BASIC_BLOCKS * ctx->mem_target;
	if (ctx->quota != 0U && ctx->total + increment > ctx->quota)
		return (ISC_FALSE);

	INSIST(ctx->basic_table_count <= ctx->basic_table_size);
	if (ctx->basic_table_count == ctx->basic_table_size) {
		table_size = ctx->basic_table_size + TABLE_INCREMENT;
		table = (ctx->memalloc)(ctx->arg,
					table_size * sizeof(unsigned char *));
		if (table == NULL) {
			ctx->memalloc_failures++;
			return (ISC_FALSE);
		}
		if (ctx->basic_table_size != 0) {
			memcpy(table, ctx->basic_table,
			       ctx->basic_table_size *
			       sizeof(unsigned char *));
			(ctx->memfree)(ctx->arg, ctx->basic_table);
		}
		ctx->basic_table = table;
		ctx->basic_table_size = table_size;
	}

	new = (ctx->memalloc)(ctx->arg, NUM_BASIC_BLOCKS * ctx->mem_target);
	if (new == NULL) {
		ctx->memalloc_failures++;
		return (ISC_FALSE);
	}
	ctx->total += increment;
	ctx->basic_table[ctx->basic_table_count] = new;
	ctx->basic_table_count++;

	curr = new;
	next = curr + ctx->mem_target;
	for (i = 0; i < (NUM_BASIC_BLOCKS - 1); i++) {
		((element *)curr)->next = (element *)next;
		curr = next;
		next += ctx->mem_target;
	}
	/*
	 * curr is now pointing at the last block in the
	 * array.
	 */
	((element *)curr)->next = NULL;
	first = new;
	last = first + NUM_BASIC_BLOCKS * ctx->mem_target - 1;
	if (first < ctx->lowest || ctx->lowest == NULL)
		ctx->lowest = first;
	if (last > ctx->highest)
		ctx->highest = last;
	ctx->basic_blocks = new;

	return (ISC_TRUE);
}

static inline isc_boolean_t
more_frags(isc__mem_t *ctx, size_t new_size) {
	int i, frags;
	size_t total_size;
	void *new;
	unsigned char *curr, *next;

	/*!
	 * Try to get more fragments by chopping up a basic block.
	 */

	if (ctx->basic_blocks == NULL) {
		if (!more_basic_blocks(ctx)) {
			/*
			 * We can't get more memory from the OS, or we've
			 * hit the quota for this context.
			 */
			/*
			 * XXXRTH  "At quota" notification here.
			 */
			return (ISC_FALSE);
		}
	}

	total_size = ctx->mem_target;
	new = ctx->basic_blocks;
	ctx->basic_blocks = ctx->basic_blocks->next;
	frags = total_size / new_size;
	ctx->stats[new_size].blocks++;
	ctx->stats[new_size].freefrags += frags;
	/*
	 * Set up a linked-list of blocks of size
	 * "new_size".
	 */
	curr = new;
	next = curr + new_size;
	total_size -= new_size;
	for (i = 0; i < (frags - 1); i++) {
		((element *)curr)->next = (element *)next;
		curr = next;
		next += new_size;
		total_size -= new_size;
	}
	/*
	 * Add the remaining fragment of the basic block to a free list.
	 */
	total_size = rmsize(total_size);
	if (total_size > 0U) {
		((element *)next)->next = ctx->freelists[total_size];
		ctx->freelists[total_size] = (element *)next;
		ctx->stats[total_size].freefrags++;
	}
	/*
	 * curr is now pointing at the last block in the
	 * array.
	 */
	((element *)curr)->next = NULL;
	ctx->freelists[new_size] = new;

	return (ISC_TRUE);
}

static inline void *
mem_getunlocked(isc__mem_t *ctx, size_t size) {
	size_t new_size = quantize(size);
	void *ret;

	if (size >= ctx->max_size || new_size >= ctx->max_size) {
		/*
		 * memget() was called on something beyond our upper limit.
		 */
		if (ctx->quota != 0U && ctx->total + size > ctx->quota) {
			ret = NULL;
			goto done;
		}
		ret = (ctx->memalloc)(ctx->arg, size);
		if (ret == NULL) {
			ctx->memalloc_failures++;
			goto done;
		}
		ctx->total += size;
		ctx->inuse += size;
		ctx->stats[ctx->max_size].gets++;
		ctx->stats[ctx->max_size].totalgets++;
		/*
		 * If we don't set new_size to size, then the
		 * ISC_MEM_FILL code might write over bytes we
		 * don't own.
		 */
		new_size = size;
		goto done;
	}

	/*
	 * If there are no blocks in the free list for this size, get a chunk
	 * of memory and then break it up into "new_size"-sized blocks, adding
	 * them to the free list.
	 */
	if (ctx->freelists[new_size] == NULL && !more_frags(ctx, new_size))
		return (NULL);

	/*
	 * The free list uses the "rounded-up" size "new_size".
	 */
	ret = ctx->freelists[new_size];
	ctx->freelists[new_size] = ctx->freelists[new_size]->next;

	/*
	 * The stats[] uses the _actual_ "size" requested by the
	 * caller, with the caveat (in the code above) that "size" >= the
	 * max. size (max_size) ends up getting recorded as a call to
	 * max_size.
	 */
	ctx->stats[size].gets++;
	ctx->stats[size].totalgets++;
	ctx->stats[new_size].freefrags--;
	ctx->inuse += new_size;

 done:

#if ISC_MEM_FILL
	if (ret != NULL)
		memset(ret, 0xbe, new_size); /* Mnemonic for "beef". */
#endif

	return (ret);
}

#if ISC_MEM_FILL && ISC_MEM_CHECKOVERRUN
static inline void
check_overrun(void *mem, size_t size, size_t new_size) {
	unsigned char *cp;

	cp = (unsigned char *)mem;
	cp += size;
	while (size < new_size) {
		INSIST(*cp == 0xbe);
		cp++;
		size++;
	}
}
#endif

static inline void
mem_putunlocked(isc__mem_t *ctx, void *mem, size_t size) {
	size_t new_size = quantize(size);

	if (size == ctx->max_size || new_size >= ctx->max_size) {
		/*
		 * memput() called on something beyond our upper limit.
		 */
#if ISC_MEM_FILL
		memset(mem, 0xde, size); /* Mnemonic for "dead". */
#endif
		(ctx->memfree)(ctx->arg, mem);
		INSIST(ctx->stats[ctx->max_size].gets != 0U);
		ctx->stats[ctx->max_size].gets--;
		INSIST(size <= ctx->total);
		ctx->inuse -= size;
		ctx->total -= size;
		return;
	}

#if ISC_MEM_FILL
#if ISC_MEM_CHECKOVERRUN
	check_overrun(mem, size, new_size);
#endif
	memset(mem, 0xde, new_size); /* Mnemonic for "dead". */
#endif

	/*
	 * The free list uses the "rounded-up" size "new_size".
	 */
	((element *)mem)->next = ctx->freelists[new_size];
	ctx->freelists[new_size] = (element *)mem;

	/*
	 * The stats[] uses the _actual_ "size" requested by the
	 * caller, with the caveat (in the code above) that "size" >= the
	 * max. size (max_size) ends up getting recorded as a call to
	 * max_size.
	 */
	INSIST(ctx->stats[size].gets != 0U);
	ctx->stats[size].gets--;
	ctx->stats[new_size].freefrags++;
	ctx->inuse -= new_size;
}

/*!
 * Perform a malloc, doing memory filling and overrun detection as necessary.
 */
static inline void *
mem_get(isc__mem_t *ctx, size_t size) {
	char *ret;

#if ISC_MEM_CHECKOVERRUN
	size += 1;
#endif

	ret = (ctx->memalloc)(ctx->arg, size);
	if (ret == NULL)
		ctx->memalloc_failures++;

#if ISC_MEM_FILL
	if (ret != NULL)
		memset(ret, 0xbe, size); /* Mnemonic for "beef". */
#else
#  if ISC_MEM_CHECKOVERRUN
	if (ret != NULL)
		ret[size-1] = 0xbe;
#  endif
#endif

	return (ret);
}

/*!
 * Perform a free, doing memory filling and overrun detection as necessary.
 */
static inline void
mem_put(isc__mem_t *ctx, void *mem, size_t size) {
#if ISC_MEM_CHECKOVERRUN
	INSIST(((unsigned char *)mem)[size] == 0xbe);
#endif
#if ISC_MEM_FILL
	memset(mem, 0xde, size); /* Mnemonic for "dead". */
#else
	UNUSED(size);
#endif
	(ctx->memfree)(ctx->arg, mem);
}

/*!
 * Update internal counters after a memory get.
 */
static inline void
mem_getstats(isc__mem_t *ctx, size_t size) {
	ctx->total += size;
	ctx->inuse += size;

	if (size > ctx->max_size) {
		ctx->stats[ctx->max_size].gets++;
		ctx->stats[ctx->max_size].totalgets++;
	} else {
		ctx->stats[size].gets++;
		ctx->stats[size].totalgets++;
	}
}

/*!
 * Update internal counters after a memory put.
 */
static inline void
mem_putstats(isc__mem_t *ctx, void *ptr, size_t size) {
	UNUSED(ptr);

	INSIST(ctx->inuse >= size);
	ctx->inuse -= size;

	if (size > ctx->max_size) {
		INSIST(ctx->stats[ctx->max_size].gets > 0U);
		ctx->stats[ctx->max_size].gets--;
	} else {
		INSIST(ctx->stats[size].gets > 0U);
		ctx->stats[size].gets--;
	}
}

/*
 * Private.
 */

static void *
default_memalloc(void *arg, size_t size) {
	UNUSED(arg);
	if (size == 0U)
		size = 1;
	return (malloc(size));
}

static void
default_memfree(void *arg, void *ptr) {
	UNUSED(arg);
	free(ptr);
}

static void
initialize_action(void) {
	RUNTIME_CHECK(isc_mutex_init(&lock) == ISC_R_SUCCESS);
	ISC_LIST_INIT(contexts);
	totallost = 0;
}

/*
 * Public.
 */

ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_createx(size_t init_max_size, size_t target_size,
		 isc_memalloc_t memalloc, isc_memfree_t memfree, void *arg,
		 isc_mem_t **ctxp)
{
	return (isc__mem_createx2(init_max_size, target_size, memalloc, memfree,
				  arg, ctxp, ISC_MEMFLAG_DEFAULT));

}

ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_createx2(size_t init_max_size, size_t target_size,
		  isc_memalloc_t memalloc, isc_memfree_t memfree, void *arg,
		  isc_mem_t **ctxp, unsigned int flags)
{
	isc__mem_t *ctx;
	isc_result_t result;

	REQUIRE(ctxp != NULL && *ctxp == NULL);
	REQUIRE(memalloc != NULL);
	REQUIRE(memfree != NULL);

	INSIST((ALIGNMENT_SIZE & (ALIGNMENT_SIZE - 1)) == 0);

	RUNTIME_CHECK(isc_once_do(&once, initialize_action) == ISC_R_SUCCESS);

	ctx = (memalloc)(arg, sizeof(*ctx));
	if (ctx == NULL)
		return (ISC_R_NOMEMORY);

	if ((flags & ISC_MEMFLAG_NOLOCK) == 0) {
		result = isc_mutex_init(&ctx->lock);
		if (result != ISC_R_SUCCESS) {
			(memfree)(arg, ctx);
			return (result);
		}
	}

	if (init_max_size == 0U)
		ctx->max_size = DEF_MAX_SIZE;
	else
		ctx->max_size = init_max_size;
	ctx->flags = flags;
	ctx->references = 1;
	memset(ctx->name, 0, sizeof(ctx->name));
	ctx->tag = NULL;
	ctx->quota = 0;
	ctx->total = 0;
	ctx->inuse = 0;
	ctx->maxinuse = 0;
	ctx->hi_water = 0;
	ctx->lo_water = 0;
	ctx->hi_called = ISC_FALSE;
	ctx->is_overmem = ISC_FALSE;
	ctx->water = NULL;
	ctx->water_arg = NULL;
	ctx->common.impmagic = MEM_MAGIC;
	ctx->common.magic = ISCAPI_MCTX_MAGIC;
	ctx->common.methods = (isc_memmethods_t *)&memmethods;
	isc_ondestroy_init(&ctx->ondestroy);
	ctx->memalloc = memalloc;
	ctx->memfree = memfree;
	ctx->arg = arg;
	ctx->stats = NULL;
	ctx->checkfree = ISC_TRUE;
#if ISC_MEM_TRACKLINES
	ctx->debuglist = NULL;
	ctx->debuglistcnt = 0;
#endif
	ISC_LIST_INIT(ctx->pools);
	ctx->poolcnt = 0;
	ctx->freelists = NULL;
	ctx->basic_blocks = NULL;
	ctx->basic_table = NULL;
	ctx->basic_table_count = 0;
	ctx->basic_table_size = 0;
	ctx->lowest = NULL;
	ctx->highest = NULL;

	ctx->stats = (memalloc)(arg,
				(ctx->max_size+1) * sizeof(struct stats));
	if (ctx->stats == NULL) {
		result = ISC_R_NOMEMORY;
		goto error;
	}
	memset(ctx->stats, 0, (ctx->max_size + 1) * sizeof(struct stats));

	if ((flags & ISC_MEMFLAG_INTERNAL) != 0) {
		if (target_size == 0U)
			ctx->mem_target = DEF_MEM_TARGET;
		else
			ctx->mem_target = target_size;
		ctx->freelists = (memalloc)(arg, ctx->max_size *
						 sizeof(element *));
		if (ctx->freelists == NULL) {
			result = ISC_R_NOMEMORY;
			goto error;
		}
		memset(ctx->freelists, 0,
		       ctx->max_size * sizeof(element *));
	}

#if ISC_MEM_TRACKLINES
	if ((isc_mem_debugging & ISC_MEM_DEBUGRECORD) != 0) {
		unsigned int i;

		ctx->debuglist = (memalloc)(arg,
				      (ctx->max_size+1) * sizeof(debuglist_t));
		if (ctx->debuglist == NULL) {
			result = ISC_R_NOMEMORY;
			goto error;
		}
		for (i = 0; i <= ctx->max_size; i++)
			ISC_LIST_INIT(ctx->debuglist[i]);
	}
#endif

	ctx->memalloc_failures = 0;

	LOCK(&lock);
	ISC_LIST_INITANDAPPEND(contexts, ctx, link);
	UNLOCK(&lock);

	*ctxp = (isc_mem_t *)ctx;
	return (ISC_R_SUCCESS);

  error:
	if (ctx != NULL) {
		if (ctx->stats != NULL)
			(memfree)(arg, ctx->stats);
		if (ctx->freelists != NULL)
			(memfree)(arg, ctx->freelists);
#if ISC_MEM_TRACKLINES
		if (ctx->debuglist != NULL)
			(ctx->memfree)(ctx->arg, ctx->debuglist);
#endif /* ISC_MEM_TRACKLINES */
		if ((ctx->flags & ISC_MEMFLAG_NOLOCK) == 0)
			DESTROYLOCK(&ctx->lock);
		(memfree)(arg, ctx);
	}

	return (result);
}

ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_create(size_t init_max_size, size_t target_size, isc_mem_t **ctxp) {
	return (isc__mem_createx2(init_max_size, target_size,
				  default_memalloc, default_memfree, NULL,
				  ctxp, ISC_MEMFLAG_DEFAULT));
}

ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_create2(size_t init_max_size, size_t target_size,
		 isc_mem_t **ctxp, unsigned int flags)
{
	return (isc__mem_createx2(init_max_size, target_size,
				  default_memalloc, default_memfree, NULL,
				  ctxp, flags));
}

static void
destroy(isc__mem_t *ctx) {
	unsigned int i;
	isc_ondestroy_t ondest;

	LOCK(&lock);
	ISC_LIST_UNLINK(contexts, ctx, link);
	totallost += ctx->inuse;
	UNLOCK(&lock);

	ctx->common.impmagic = 0;
	ctx->common.magic = 0;

	INSIST(ISC_LIST_EMPTY(ctx->pools));

#if ISC_MEM_TRACKLINES
	if (ctx->debuglist != NULL) {
		if (ctx->checkfree) {
			for (i = 0; i <= ctx->max_size; i++) {
				if (!ISC_LIST_EMPTY(ctx->debuglist[i]))
					print_active(ctx, stderr);
				INSIST(ISC_LIST_EMPTY(ctx->debuglist[i]));
			}
		} else {
			debuglink_t *dl;

			for (i = 0; i <= ctx->max_size; i++)
				for (dl = ISC_LIST_HEAD(ctx->debuglist[i]);
				     dl != NULL;
				     dl = ISC_LIST_HEAD(ctx->debuglist[i])) {
					ISC_LIST_UNLINK(ctx->debuglist[i],
							dl, link);
					free(dl);
				}
		}
		(ctx->memfree)(ctx->arg, ctx->debuglist);
	}
#endif
	INSIST(ctx->references == 0);

	if (ctx->checkfree) {
		for (i = 0; i <= ctx->max_size; i++) {
#if ISC_MEM_TRACKLINES
			if (ctx->stats[i].gets != 0U)
				print_active(ctx, stderr);
#endif
			INSIST(ctx->stats[i].gets == 0U);
		}
	}

	(ctx->memfree)(ctx->arg, ctx->stats);

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		for (i = 0; i < ctx->basic_table_count; i++)
			(ctx->memfree)(ctx->arg, ctx->basic_table[i]);
		(ctx->memfree)(ctx->arg, ctx->freelists);
		if (ctx->basic_table != NULL)
			(ctx->memfree)(ctx->arg, ctx->basic_table);
	}

	ondest = ctx->ondestroy;

	if ((ctx->flags & ISC_MEMFLAG_NOLOCK) == 0)
		DESTROYLOCK(&ctx->lock);
	(ctx->memfree)(ctx->arg, ctx);

	isc_ondestroy_notify(&ondest, ctx);
}

ISC_MEMFUNC_SCOPE void
isc__mem_attach(isc_mem_t *source0, isc_mem_t **targetp) {
	isc__mem_t *source = (isc__mem_t *)source0;

	REQUIRE(VALID_CONTEXT(source));
	REQUIRE(targetp != NULL && *targetp == NULL);

	MCTXLOCK(source, &source->lock);
	source->references++;
	MCTXUNLOCK(source, &source->lock);

	*targetp = (isc_mem_t *)source;
}

ISC_MEMFUNC_SCOPE void
isc__mem_detach(isc_mem_t **ctxp) {
	isc__mem_t *ctx;
	isc_boolean_t want_destroy = ISC_FALSE;

	REQUIRE(ctxp != NULL);
	ctx = (isc__mem_t *)*ctxp;
	REQUIRE(VALID_CONTEXT(ctx));

	MCTXLOCK(ctx, &ctx->lock);
	INSIST(ctx->references > 0);
	ctx->references--;
	if (ctx->references == 0)
		want_destroy = ISC_TRUE;
	MCTXUNLOCK(ctx, &ctx->lock);

	if (want_destroy)
		destroy(ctx);

	*ctxp = NULL;
}

/*
 * isc_mem_putanddetach() is the equivalent of:
 *
 * mctx = NULL;
 * isc_mem_attach(ptr->mctx, &mctx);
 * isc_mem_detach(&ptr->mctx);
 * isc_mem_put(mctx, ptr, sizeof(*ptr);
 * isc_mem_detach(&mctx);
 */

ISC_MEMFUNC_SCOPE void
isc___mem_putanddetach(isc_mem_t **ctxp, void *ptr, size_t size FLARG) {
	isc__mem_t *ctx;
	isc_boolean_t want_destroy = ISC_FALSE;
	size_info *si;
	size_t oldsize;

	REQUIRE(ctxp != NULL);
	ctx = (isc__mem_t *)*ctxp;
	REQUIRE(VALID_CONTEXT(ctx));
	REQUIRE(ptr != NULL);

	/*
	 * Must be before mem_putunlocked() as ctxp is usually within
	 * [ptr..ptr+size).
	 */
	*ctxp = NULL;

	if ((isc_mem_debugging & (ISC_MEM_DEBUGSIZE|ISC_MEM_DEBUGCTX)) != 0) {
		if ((isc_mem_debugging & ISC_MEM_DEBUGSIZE) != 0) {
			si = &(((size_info *)ptr)[-1]);
			oldsize = si->u.size - ALIGNMENT_SIZE;
			if ((isc_mem_debugging & ISC_MEM_DEBUGCTX) != 0)
				oldsize -= ALIGNMENT_SIZE;
			INSIST(oldsize == size);
		}
		isc_mem_free((isc_mem_t *)ctx, ptr);

		MCTXLOCK(ctx, &ctx->lock);
		ctx->references--;
		if (ctx->references == 0)
			want_destroy = ISC_TRUE;
		MCTXUNLOCK(ctx, &ctx->lock);
		if (want_destroy)
			destroy(ctx);

		return;
	}

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		MCTXLOCK(ctx, &ctx->lock);
		mem_putunlocked(ctx, ptr, size);
	} else {
		mem_put(ctx, ptr, size);
		MCTXLOCK(ctx, &ctx->lock);
		mem_putstats(ctx, ptr, size);
	}

	DELETE_TRACE(ctx, ptr, size, file, line);
	INSIST(ctx->references > 0);
	ctx->references--;
	if (ctx->references == 0)
		want_destroy = ISC_TRUE;

	MCTXUNLOCK(ctx, &ctx->lock);

	if (want_destroy)
		destroy(ctx);
}

ISC_MEMFUNC_SCOPE void
isc__mem_destroy(isc_mem_t **ctxp) {
	isc__mem_t *ctx;

	/*
	 * This routine provides legacy support for callers who use mctxs
	 * without attaching/detaching.
	 */

	REQUIRE(ctxp != NULL);
	ctx = (isc__mem_t *)*ctxp;
	REQUIRE(VALID_CONTEXT(ctx));

	MCTXLOCK(ctx, &ctx->lock);
#if ISC_MEM_TRACKLINES
	if (ctx->references != 1)
		print_active(ctx, stderr);
#endif
	REQUIRE(ctx->references == 1);
	ctx->references--;
	MCTXUNLOCK(ctx, &ctx->lock);

	destroy(ctx);

	*ctxp = NULL;
}

ISC_MEMFUNC_SCOPE isc_result_t
isc__mem_ondestroy(isc_mem_t *ctx0, isc_task_t *task, isc_event_t **event) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	isc_result_t res;

	MCTXLOCK(ctx, &ctx->lock);
	res = isc_ondestroy_register(&ctx->ondestroy, task, event);
	MCTXUNLOCK(ctx, &ctx->lock);

	return (res);
}

ISC_MEMFUNC_SCOPE void *
isc___mem_get(isc_mem_t *ctx0, size_t size FLARG) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	void *ptr;
	isc_boolean_t call_water = ISC_FALSE;

	REQUIRE(VALID_CONTEXT(ctx));

	if ((isc_mem_debugging & (ISC_MEM_DEBUGSIZE|ISC_MEM_DEBUGCTX)) != 0)
		return (isc__mem_allocate(ctx0, size FLARG_PASS));

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		MCTXLOCK(ctx, &ctx->lock);
		ptr = mem_getunlocked(ctx, size);
	} else {
		ptr = mem_get(ctx, size);
		MCTXLOCK(ctx, &ctx->lock);
		if (ptr != NULL)
			mem_getstats(ctx, size);
	}

	ADD_TRACE(ctx, ptr, size, file, line);
	if (ctx->hi_water != 0U && ctx->inuse > ctx->hi_water &&
	    !ctx->is_overmem) {
		ctx->is_overmem = ISC_TRUE;
	}
	if (ctx->hi_water != 0U && !ctx->hi_called &&
	    ctx->inuse > ctx->hi_water) {
		call_water = ISC_TRUE;
	}
	if (ctx->inuse > ctx->maxinuse) {
		ctx->maxinuse = ctx->inuse;
		if (ctx->hi_water != 0U && ctx->inuse > ctx->hi_water &&
		    (isc_mem_debugging & ISC_MEM_DEBUGUSAGE) != 0)
			fprintf(stderr, "maxinuse = %lu\n",
				(unsigned long)ctx->inuse);
	}
	MCTXUNLOCK(ctx, &ctx->lock);

	if (call_water)
		(ctx->water)(ctx->water_arg, ISC_MEM_HIWATER);

	return (ptr);
}

ISC_MEMFUNC_SCOPE void
isc___mem_put(isc_mem_t *ctx0, void *ptr, size_t size FLARG) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	isc_boolean_t call_water = ISC_FALSE;
	size_info *si;
	size_t oldsize;

	REQUIRE(VALID_CONTEXT(ctx));
	REQUIRE(ptr != NULL);

	if ((isc_mem_debugging & (ISC_MEM_DEBUGSIZE|ISC_MEM_DEBUGCTX)) != 0) {
		if ((isc_mem_debugging & ISC_MEM_DEBUGSIZE) != 0) {
			si = &(((size_info *)ptr)[-1]);
			oldsize = si->u.size - ALIGNMENT_SIZE;
			if ((isc_mem_debugging & ISC_MEM_DEBUGCTX) != 0)
				oldsize -= ALIGNMENT_SIZE;
			INSIST(oldsize == size);
		}
		isc_mem_free((isc_mem_t *)ctx, ptr);
		return;
	}

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		MCTXLOCK(ctx, &ctx->lock);
		mem_putunlocked(ctx, ptr, size);
	} else {
		mem_put(ctx, ptr, size);
		MCTXLOCK(ctx, &ctx->lock);
		mem_putstats(ctx, ptr, size);
	}

	DELETE_TRACE(ctx, ptr, size, file, line);

	/*
	 * The check against ctx->lo_water == 0 is for the condition
	 * when the context was pushed over hi_water but then had
	 * isc_mem_setwater() called with 0 for hi_water and lo_water.
	 */
	if (ctx->is_overmem &&
	    (ctx->inuse < ctx->lo_water || ctx->lo_water == 0U)) {
		ctx->is_overmem = ISC_FALSE;
	}
	if (ctx->hi_called &&
	    (ctx->inuse < ctx->lo_water || ctx->lo_water == 0U)) {
		if (ctx->water != NULL)
			call_water = ISC_TRUE;
	}
	MCTXUNLOCK(ctx, &ctx->lock);

	if (call_water)
		(ctx->water)(ctx->water_arg, ISC_MEM_LOWATER);
}

ISC_MEMFUNC_SCOPE void
isc__mem_waterack(isc_mem_t *ctx0, int flag) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));

	MCTXLOCK(ctx, &ctx->lock);
	if (flag == ISC_MEM_LOWATER)
		ctx->hi_called = ISC_FALSE;
	else if (flag == ISC_MEM_HIWATER)
		ctx->hi_called = ISC_TRUE;
	MCTXUNLOCK(ctx, &ctx->lock);
}

#if ISC_MEM_TRACKLINES
static void
print_active(isc__mem_t *mctx, FILE *out) {
	if (mctx->debuglist != NULL) {
		debuglink_t *dl;
		unsigned int i, j;
		const char *format;
		isc_boolean_t found;

		fprintf(out, "%s", isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
					    ISC_MSG_DUMPALLOC,
					    "Dump of all outstanding "
					    "memory allocations:\n"));
		found = ISC_FALSE;
		format = isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
					ISC_MSG_PTRFILELINE,
					"\tptr %p size %u file %s line %u\n");
		for (i = 0; i <= mctx->max_size; i++) {
			dl = ISC_LIST_HEAD(mctx->debuglist[i]);

			if (dl != NULL)
				found = ISC_TRUE;

			while (dl != NULL) {
				for (j = 0; j < DEBUGLIST_COUNT; j++)
					if (dl->ptr[j] != NULL)
						fprintf(out, format,
							dl->ptr[j],
							dl->size[j],
							dl->file[j],
							dl->line[j]);
				dl = ISC_LIST_NEXT(dl, link);
			}
		}
		if (!found)
			fprintf(out, "%s", isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
						    ISC_MSG_NONE, "\tNone.\n"));
	}
}
#endif

/*
 * Print the stats[] on the stream "out" with suitable formatting.
 */
ISC_MEMFUNC_SCOPE void
isc__mem_stats(isc_mem_t *ctx0, FILE *out) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	size_t i;
	const struct stats *s;
	const isc__mempool_t *pool;

	REQUIRE(VALID_CONTEXT(ctx));
	MCTXLOCK(ctx, &ctx->lock);

	for (i = 0; i <= ctx->max_size; i++) {
		s = &ctx->stats[i];

		if (s->totalgets == 0U && s->gets == 0U)
			continue;
		fprintf(out, "%s%5lu: %11lu gets, %11lu rem",
			(i == ctx->max_size) ? ">=" : "  ",
			(unsigned long) i, s->totalgets, s->gets);
		if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0 &&
		    (s->blocks != 0U || s->freefrags != 0U))
			fprintf(out, " (%lu bl, %lu ff)",
				s->blocks, s->freefrags);
		fputc('\n', out);
	}

	/*
	 * Note that since a pool can be locked now, these stats might be
	 * somewhat off if the pool is in active use at the time the stats
	 * are dumped.  The link fields are protected by the isc_mem_t's
	 * lock, however, so walking this list and extracting integers from
	 * stats fields is always safe.
	 */
	pool = ISC_LIST_HEAD(ctx->pools);
	if (pool != NULL) {
		fprintf(out, "%s", isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
					    ISC_MSG_POOLSTATS,
					    "[Pool statistics]\n"));
		fprintf(out, "%15s %10s %10s %10s %10s %10s %10s %10s %1s\n",
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLNAME, "name"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLSIZE, "size"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLMAXALLOC, "maxalloc"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLALLOCATED, "allocated"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLFREECOUNT, "freecount"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLFREEMAX, "freemax"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLFILLCOUNT, "fillcount"),
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_MEM,
				       ISC_MSG_POOLGETS, "gets"),
			"L");
	}
	while (pool != NULL) {
		fprintf(out, "%15s %10lu %10u %10u %10u %10u %10u %10u %s\n",
			pool->name, (unsigned long) pool->size, pool->maxalloc,
			pool->allocated, pool->freecount, pool->freemax,
			pool->fillcount, pool->gets,
			(pool->lock == NULL ? "N" : "Y"));
		pool = ISC_LIST_NEXT(pool, link);
	}

#if ISC_MEM_TRACKLINES
	print_active(ctx, out);
#endif

	MCTXUNLOCK(ctx, &ctx->lock);
}

/*
 * Replacements for malloc() and free() -- they implicitly remember the
 * size of the object allocated (with some additional overhead).
 */

static void *
isc__mem_allocateunlocked(isc_mem_t *ctx0, size_t size) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	size_info *si;

	size += ALIGNMENT_SIZE;
	if ((isc_mem_debugging & ISC_MEM_DEBUGCTX) != 0)
		size += ALIGNMENT_SIZE;

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0)
		si = mem_getunlocked(ctx, size);
	else
		si = mem_get(ctx, size);

	if (si == NULL)
		return (NULL);
	if ((isc_mem_debugging & ISC_MEM_DEBUGCTX) != 0) {
		si->u.ctx = ctx;
		si++;
	}
	si->u.size = size;
	return (&si[1]);
}

ISC_MEMFUNC_SCOPE void *
isc___mem_allocate(isc_mem_t *ctx0, size_t size FLARG) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	size_info *si;
	isc_boolean_t call_water = ISC_FALSE;

	REQUIRE(VALID_CONTEXT(ctx));

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		MCTXLOCK(ctx, &ctx->lock);
		si = isc__mem_allocateunlocked((isc_mem_t *)ctx, size);
	} else {
		si = isc__mem_allocateunlocked((isc_mem_t *)ctx, size);
		MCTXLOCK(ctx, &ctx->lock);
		if (si != NULL)
			mem_getstats(ctx, si[-1].u.size);
	}

#if ISC_MEM_TRACKLINES
	ADD_TRACE(ctx, si, si[-1].u.size, file, line);
#endif
	if (ctx->hi_water != 0U && ctx->inuse > ctx->hi_water &&
	    !ctx->is_overmem) {
		ctx->is_overmem = ISC_TRUE;
	}

	if (ctx->hi_water != 0U && !ctx->hi_called &&
	    ctx->inuse > ctx->hi_water) {
		ctx->hi_called = ISC_TRUE;
		call_water = ISC_TRUE;
	}
	if (ctx->inuse > ctx->maxinuse) {
		ctx->maxinuse = ctx->inuse;
		if (ctx->hi_water != 0U && ctx->inuse > ctx->hi_water &&
		    (isc_mem_debugging & ISC_MEM_DEBUGUSAGE) != 0)
			fprintf(stderr, "maxinuse = %lu\n",
				(unsigned long)ctx->inuse);
	}
	MCTXUNLOCK(ctx, &ctx->lock);

	if (call_water)
		(ctx->water)(ctx->water_arg, ISC_MEM_HIWATER);

	return (si);
}

ISC_MEMFUNC_SCOPE void *
isc___mem_reallocate(isc_mem_t *ctx0, void *ptr, size_t size FLARG) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	void *new_ptr = NULL;
	size_t oldsize, copysize;

	REQUIRE(VALID_CONTEXT(ctx));

	/*
	 * This function emulates the realloc(3) standard library function:
	 * - if size > 0, allocate new memory; and if ptr is non NULL, copy
	 *   as much of the old contents to the new buffer and free the old one.
	 *   Note that when allocation fails the original pointer is intact;
	 *   the caller must free it.
	 * - if size is 0 and ptr is non NULL, simply free the given ptr.
	 * - this function returns:
	 *     pointer to the newly allocated memory, or
	 *     NULL if allocation fails or doesn't happen.
	 */
	if (size > 0U) {
		new_ptr = isc__mem_allocate(ctx0, size FLARG_PASS);
		if (new_ptr != NULL && ptr != NULL) {
			oldsize = (((size_info *)ptr)[-1]).u.size;
			INSIST(oldsize >= ALIGNMENT_SIZE);
			oldsize -= ALIGNMENT_SIZE;
			copysize = oldsize > size ? size : oldsize;
			memcpy(new_ptr, ptr, copysize);
			isc__mem_free(ctx0, ptr FLARG_PASS);
		}
	} else if (ptr != NULL)
		isc__mem_free(ctx0, ptr FLARG_PASS);

	return (new_ptr);
}

ISC_MEMFUNC_SCOPE void
isc___mem_free(isc_mem_t *ctx0, void *ptr FLARG) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	size_info *si;
	size_t size;
	isc_boolean_t call_water= ISC_FALSE;

	REQUIRE(VALID_CONTEXT(ctx));
	REQUIRE(ptr != NULL);

	if ((isc_mem_debugging & ISC_MEM_DEBUGCTX) != 0) {
		si = &(((size_info *)ptr)[-2]);
		REQUIRE(si->u.ctx == ctx);
		size = si[1].u.size;
	} else {
		si = &(((size_info *)ptr)[-1]);
		size = si->u.size;
	}

	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		MCTXLOCK(ctx, &ctx->lock);
		mem_putunlocked(ctx, si, size);
	} else {
		mem_put(ctx, si, size);
		MCTXLOCK(ctx, &ctx->lock);
		mem_putstats(ctx, si, size);
	}

	DELETE_TRACE(ctx, ptr, size, file, line);

	/*
	 * The check against ctx->lo_water == 0 is for the condition
	 * when the context was pushed over hi_water but then had
	 * isc_mem_setwater() called with 0 for hi_water and lo_water.
	 */
	if (ctx->is_overmem &&
	    (ctx->inuse < ctx->lo_water || ctx->lo_water == 0U)) {
		ctx->is_overmem = ISC_FALSE;
	}

	if (ctx->hi_called &&
	    (ctx->inuse < ctx->lo_water || ctx->lo_water == 0U)) {
		ctx->hi_called = ISC_FALSE;

		if (ctx->water != NULL)
			call_water = ISC_TRUE;
	}
	MCTXUNLOCK(ctx, &ctx->lock);

	if (call_water)
		(ctx->water)(ctx->water_arg, ISC_MEM_LOWATER);
}


/*
 * Other useful things.
 */

ISC_MEMFUNC_SCOPE char *
isc___mem_strdup(isc_mem_t *mctx0, const char *s FLARG) {
	isc__mem_t *mctx = (isc__mem_t *)mctx0;
	size_t len;
	char *ns;

	REQUIRE(VALID_CONTEXT(mctx));
	REQUIRE(s != NULL);

	len = strlen(s);

	ns = isc___mem_allocate((isc_mem_t *)mctx, len + 1 FLARG_PASS);

	if (ns != NULL)
		strncpy(ns, s, len + 1);

	return (ns);
}

ISC_MEMFUNC_SCOPE void
isc__mem_setdestroycheck(isc_mem_t *ctx0, isc_boolean_t flag) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));
	MCTXLOCK(ctx, &ctx->lock);

	ctx->checkfree = flag;

	MCTXUNLOCK(ctx, &ctx->lock);
}

/*
 * Quotas
 */

ISC_MEMFUNC_SCOPE void
isc__mem_setquota(isc_mem_t *ctx0, size_t quota) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));
	MCTXLOCK(ctx, &ctx->lock);

	ctx->quota = quota;

	MCTXUNLOCK(ctx, &ctx->lock);
}

ISC_MEMFUNC_SCOPE size_t
isc__mem_getquota(isc_mem_t *ctx0) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	size_t quota;

	REQUIRE(VALID_CONTEXT(ctx));
	MCTXLOCK(ctx, &ctx->lock);

	quota = ctx->quota;

	MCTXUNLOCK(ctx, &ctx->lock);

	return (quota);
}

ISC_MEMFUNC_SCOPE size_t
isc__mem_inuse(isc_mem_t *ctx0) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	size_t inuse;

	REQUIRE(VALID_CONTEXT(ctx));
	MCTXLOCK(ctx, &ctx->lock);

	inuse = ctx->inuse;

	MCTXUNLOCK(ctx, &ctx->lock);

	return (inuse);
}

ISC_MEMFUNC_SCOPE void
isc__mem_setwater(isc_mem_t *ctx0, isc_mem_water_t water, void *water_arg,
		 size_t hiwater, size_t lowater)
{
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	isc_boolean_t callwater = ISC_FALSE;
	isc_mem_water_t oldwater;
	void *oldwater_arg;

	REQUIRE(VALID_CONTEXT(ctx));
	REQUIRE(hiwater >= lowater);

	MCTXLOCK(ctx, &ctx->lock);
	oldwater = ctx->water;
	oldwater_arg = ctx->water_arg;
	if (water == NULL) {
		callwater = ctx->hi_called;
		ctx->water = NULL;
		ctx->water_arg = NULL;
		ctx->hi_water = 0;
		ctx->lo_water = 0;
		ctx->hi_called = ISC_FALSE;
	} else {
		if (ctx->hi_called &&
		    (ctx->water != water || ctx->water_arg != water_arg ||
		     ctx->inuse < lowater || lowater == 0U))
			callwater = ISC_TRUE;
		ctx->water = water;
		ctx->water_arg = water_arg;
		ctx->hi_water = hiwater;
		ctx->lo_water = lowater;
		ctx->hi_called = ISC_FALSE;
	}
	MCTXUNLOCK(ctx, &ctx->lock);

	if (callwater && oldwater != NULL)
		(oldwater)(oldwater_arg, ISC_MEM_LOWATER);
}

ISC_MEMFUNC_SCOPE isc_boolean_t
isc__mem_isovermem(isc_mem_t *ctx0) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));

	/*
	 * We don't bother to lock the context because 100% accuracy isn't
	 * necessary (and even if we locked the context the returned value
	 * could be different from the actual state when it's used anyway)
	 */
	return (ctx->is_overmem);
}

ISC_MEMFUNC_SCOPE void
isc__mem_setname(isc_mem_t *ctx0, const char *name, void *tag) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));

	LOCK(&ctx->lock);
	memset(ctx->name, 0, sizeof(ctx->name));
	strncpy(ctx->name, name, sizeof(ctx->name) - 1);
	ctx->tag = tag;
	UNLOCK(&ctx->lock);
}

ISC_MEMFUNC_SCOPE const char *
isc__mem_getname(isc_mem_t *ctx0) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));

	return (ctx->name);
}

ISC_MEMFUNC_SCOPE void *
isc__mem_gettag(isc_mem_t *ctx0) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));

	return (ctx->tag);
}

/*
 * Memory pool stuff
 */

ISC_MEMFUNC_SCOPE isc_result_t
isc__mempool_create(isc_mem_t *mctx0, size_t size, isc_mempool_t **mpctxp) {
	isc__mem_t *mctx = (isc__mem_t *)mctx0;
	isc__mempool_t *mpctx;

	REQUIRE(VALID_CONTEXT(mctx));
	REQUIRE(size > 0U);
	REQUIRE(mpctxp != NULL && *mpctxp == NULL);

	/*
	 * Allocate space for this pool, initialize values, and if all works
	 * well, attach to the memory context.
	 */
	mpctx = isc_mem_get((isc_mem_t *)mctx, sizeof(isc__mempool_t));
	if (mpctx == NULL)
		return (ISC_R_NOMEMORY);

	mpctx->common.methods = (isc_mempoolmethods_t *)&mempoolmethods;
	mpctx->common.impmagic = MEMPOOL_MAGIC;
	mpctx->common.magic = ISCAPI_MPOOL_MAGIC;
	mpctx->lock = NULL;
	mpctx->mctx = mctx;
	mpctx->size = size;
	mpctx->maxalloc = UINT_MAX;
	mpctx->allocated = 0;
	mpctx->freecount = 0;
	mpctx->freemax = 1;
	mpctx->fillcount = 1;
	mpctx->gets = 0;
#if ISC_MEMPOOL_NAMES
	mpctx->name[0] = 0;
#endif
	mpctx->items = NULL;

	*mpctxp = (isc_mempool_t *)mpctx;

	MCTXLOCK(mctx, &mctx->lock);
	ISC_LIST_INITANDAPPEND(mctx->pools, mpctx, link);
	mctx->poolcnt++;
	MCTXUNLOCK(mctx, &mctx->lock);

	return (ISC_R_SUCCESS);
}

ISC_MEMFUNC_SCOPE void
isc__mempool_setname(isc_mempool_t *mpctx0, const char *name) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;

	REQUIRE(name != NULL);
	REQUIRE(VALID_MEMPOOL(mpctx));

#if ISC_MEMPOOL_NAMES
	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	strncpy(mpctx->name, name, sizeof(mpctx->name) - 1);
	mpctx->name[sizeof(mpctx->name) - 1] = '\0';

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);
#else
	UNUSED(mpctx);
	UNUSED(name);
#endif
}

ISC_MEMFUNC_SCOPE void
isc__mempool_destroy(isc_mempool_t **mpctxp) {
	isc__mempool_t *mpctx;
	isc__mem_t *mctx;
	isc_mutex_t *lock;
	element *item;

	REQUIRE(mpctxp != NULL);
	mpctx = (isc__mempool_t *)*mpctxp;
	REQUIRE(VALID_MEMPOOL(mpctx));
#if ISC_MEMPOOL_NAMES
	if (mpctx->allocated > 0)
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc__mempool_destroy(): mempool %s "
				 "leaked memory",
				 mpctx->name);
#endif
	REQUIRE(mpctx->allocated == 0);

	mctx = mpctx->mctx;

	lock = mpctx->lock;

	if (lock != NULL)
		LOCK(lock);

	/*
	 * Return any items on the free list
	 */
	MCTXLOCK(mctx, &mctx->lock);
	while (mpctx->items != NULL) {
		INSIST(mpctx->freecount > 0);
		mpctx->freecount--;
		item = mpctx->items;
		mpctx->items = item->next;

		if ((mctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
			mem_putunlocked(mctx, item, mpctx->size);
		} else {
			mem_put(mctx, item, mpctx->size);
			mem_putstats(mctx, item, mpctx->size);
		}
	}
	MCTXUNLOCK(mctx, &mctx->lock);

	/*
	 * Remove our linked list entry from the memory context.
	 */
	MCTXLOCK(mctx, &mctx->lock);
	ISC_LIST_UNLINK(mctx->pools, mpctx, link);
	mctx->poolcnt--;
	MCTXUNLOCK(mctx, &mctx->lock);

	mpctx->common.impmagic = 0;
	mpctx->common.magic = 0;

	isc_mem_put((isc_mem_t *)mpctx->mctx, mpctx, sizeof(isc__mempool_t));

	if (lock != NULL)
		UNLOCK(lock);

	*mpctxp = NULL;
}

ISC_MEMFUNC_SCOPE void
isc__mempool_associatelock(isc_mempool_t *mpctx0, isc_mutex_t *lock) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;

	REQUIRE(VALID_MEMPOOL(mpctx));
	REQUIRE(mpctx->lock == NULL);
	REQUIRE(lock != NULL);

	mpctx->lock = lock;
}

ISC_MEMFUNC_SCOPE void *
isc___mempool_get(isc_mempool_t *mpctx0 FLARG) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;
	element *item;
	isc__mem_t *mctx;
	unsigned int i;

	REQUIRE(VALID_MEMPOOL(mpctx));

	mctx = mpctx->mctx;

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	/*
	 * Don't let the caller go over quota
	 */
	if (mpctx->allocated >= mpctx->maxalloc) {
		item = NULL;
		goto out;
	}

	/*
	 * if we have a free list item, return the first here
	 */
	item = mpctx->items;
	if (item != NULL) {
		mpctx->items = item->next;
		INSIST(mpctx->freecount > 0);
		mpctx->freecount--;
		mpctx->gets++;
		mpctx->allocated++;
		goto out;
	}

	/*
	 * We need to dip into the well.  Lock the memory context here and
	 * fill up our free list.
	 */
	MCTXLOCK(mctx, &mctx->lock);
	for (i = 0; i < mpctx->fillcount; i++) {
		if ((mctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
			item = mem_getunlocked(mctx, mpctx->size);
		} else {
			item = mem_get(mctx, mpctx->size);
			if (item != NULL)
				mem_getstats(mctx, mpctx->size);
		}
		if (item == NULL)
			break;
		item->next = mpctx->items;
		mpctx->items = item;
		mpctx->freecount++;
	}
	MCTXUNLOCK(mctx, &mctx->lock);

	/*
	 * If we didn't get any items, return NULL.
	 */
	item = mpctx->items;
	if (item == NULL)
		goto out;

	mpctx->items = item->next;
	mpctx->freecount--;
	mpctx->gets++;
	mpctx->allocated++;

 out:
	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);

#if ISC_MEM_TRACKLINES
	if (item != NULL) {
		MCTXLOCK(mctx, &mctx->lock);
		ADD_TRACE(mctx, item, mpctx->size, file, line);
		MCTXUNLOCK(mctx, &mctx->lock);
	}
#endif /* ISC_MEM_TRACKLINES */

	return (item);
}

ISC_MEMFUNC_SCOPE void
isc___mempool_put(isc_mempool_t *mpctx0, void *mem FLARG) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;
	isc__mem_t *mctx;
	element *item;

	REQUIRE(VALID_MEMPOOL(mpctx));
	REQUIRE(mem != NULL);

	mctx = mpctx->mctx;

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	INSIST(mpctx->allocated > 0);
	mpctx->allocated--;

#if ISC_MEM_TRACKLINES
	MCTXLOCK(mctx, &mctx->lock);
	DELETE_TRACE(mctx, mem, mpctx->size, file, line);
	MCTXUNLOCK(mctx, &mctx->lock);
#endif /* ISC_MEM_TRACKLINES */

	/*
	 * If our free list is full, return this to the mctx directly.
	 */
	if (mpctx->freecount >= mpctx->freemax) {
		if ((mctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
			MCTXLOCK(mctx, &mctx->lock);
			mem_putunlocked(mctx, mem, mpctx->size);
			MCTXUNLOCK(mctx, &mctx->lock);
		} else {
			mem_put(mctx, mem, mpctx->size);
			MCTXLOCK(mctx, &mctx->lock);
			mem_putstats(mctx, mem, mpctx->size);
			MCTXUNLOCK(mctx, &mctx->lock);
		}
		if (mpctx->lock != NULL)
			UNLOCK(mpctx->lock);
		return;
	}

	/*
	 * Otherwise, attach it to our free list and bump the counter.
	 */
	mpctx->freecount++;
	item = (element *)mem;
	item->next = mpctx->items;
	mpctx->items = item;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);
}

/*
 * Quotas
 */

ISC_MEMFUNC_SCOPE void
isc__mempool_setfreemax(isc_mempool_t *mpctx0, unsigned int limit) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	mpctx->freemax = limit;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);
}

ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getfreemax(isc_mempool_t *mpctx0) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;
	unsigned int freemax;

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	freemax = mpctx->freemax;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);

	return (freemax);
}

ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getfreecount(isc_mempool_t *mpctx0) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;
	unsigned int freecount;

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	freecount = mpctx->freecount;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);

	return (freecount);
}

ISC_MEMFUNC_SCOPE void
isc__mempool_setmaxalloc(isc_mempool_t *mpctx0, unsigned int limit) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;

	REQUIRE(limit > 0);

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	mpctx->maxalloc = limit;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);
}

ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getmaxalloc(isc_mempool_t *mpctx0) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;
	unsigned int maxalloc;

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	maxalloc = mpctx->maxalloc;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);

	return (maxalloc);
}

ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getallocated(isc_mempool_t *mpctx0) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;
	unsigned int allocated;

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	allocated = mpctx->allocated;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);

	return (allocated);
}

ISC_MEMFUNC_SCOPE void
isc__mempool_setfillcount(isc_mempool_t *mpctx0, unsigned int limit) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;

	REQUIRE(limit > 0);
	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	mpctx->fillcount = limit;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);
}

ISC_MEMFUNC_SCOPE unsigned int
isc__mempool_getfillcount(isc_mempool_t *mpctx0) {
	isc__mempool_t *mpctx = (isc__mempool_t *)mpctx0;

	unsigned int fillcount;

	REQUIRE(VALID_MEMPOOL(mpctx));

	if (mpctx->lock != NULL)
		LOCK(mpctx->lock);

	fillcount = mpctx->fillcount;

	if (mpctx->lock != NULL)
		UNLOCK(mpctx->lock);

	return (fillcount);
}

#ifdef USE_MEMIMPREGISTER
isc_result_t
isc__mem_register() {
	return (isc_mem_register(isc__mem_create2));
}
#endif

#ifdef BIND9
ISC_MEMFUNC_SCOPE void
isc__mem_printactive(isc_mem_t *ctx0, FILE *file) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;

	REQUIRE(VALID_CONTEXT(ctx));
	REQUIRE(file != NULL);

#if !ISC_MEM_TRACKLINES
	UNUSED(ctx);
	UNUSED(file);
#else
	print_active(ctx, file);
#endif
}

ISC_MEMFUNC_SCOPE void
isc__mem_printallactive(FILE *file) {
#if !ISC_MEM_TRACKLINES
	UNUSED(file);
#else
	isc__mem_t *ctx;

	RUNTIME_CHECK(isc_once_do(&once, initialize_action) == ISC_R_SUCCESS);

	LOCK(&lock);
	for (ctx = ISC_LIST_HEAD(contexts);
	     ctx != NULL;
	     ctx = ISC_LIST_NEXT(ctx, link)) {
		fprintf(file, "context: %p\n", ctx);
		print_active(ctx, file);
	}
	UNLOCK(&lock);
#endif
}

ISC_MEMFUNC_SCOPE void
isc__mem_checkdestroyed(FILE *file) {

	RUNTIME_CHECK(isc_once_do(&once, initialize_action) == ISC_R_SUCCESS);

	LOCK(&lock);
	if (!ISC_LIST_EMPTY(contexts))  {
#if ISC_MEM_TRACKLINES
		isc__mem_t *ctx;

		for (ctx = ISC_LIST_HEAD(contexts);
		     ctx != NULL;
		     ctx = ISC_LIST_NEXT(ctx, link)) {
			fprintf(file, "context: %p\n", ctx);
			print_active(ctx, file);
		}
		fflush(file);
#endif
		INSIST(0);
	}
	UNLOCK(&lock);
}

ISC_MEMFUNC_SCOPE unsigned int
isc_mem_references(isc_mem_t *ctx0) {
	isc__mem_t *ctx = (isc__mem_t *)ctx0;
	unsigned int references;

	REQUIRE(VALID_CONTEXT(ctx));

	MCTXLOCK(ctx, &ctx->lock);
	references = ctx->references;
	MCTXUNLOCK(ctx, &ctx->lock);

	return (references);
}

#ifdef HAVE_LIBXML2

typedef struct summarystat {
	isc_uint64_t	total;
	isc_uint64_t	inuse;
	isc_uint64_t	blocksize;
	isc_uint64_t	contextsize;
} summarystat_t;

static void
renderctx(isc__mem_t *ctx, summarystat_t *summary, xmlTextWriterPtr writer) {
	REQUIRE(VALID_CONTEXT(ctx));

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "context");

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "id");
	xmlTextWriterWriteFormatString(writer, "%p", ctx);
	xmlTextWriterEndElement(writer); /* id */

	if (ctx->name[0] != 0) {
		xmlTextWriterStartElement(writer, ISC_XMLCHAR "name");
		xmlTextWriterWriteFormatString(writer, "%s", ctx->name);
		xmlTextWriterEndElement(writer); /* name */
	}

	REQUIRE(VALID_CONTEXT(ctx));
	MCTXLOCK(ctx, &ctx->lock);

	summary->contextsize += sizeof(*ctx) +
		(ctx->max_size + 1) * sizeof(struct stats) +
		ctx->max_size * sizeof(element *) +
		ctx->basic_table_count * sizeof(char *);
#if ISC_MEM_TRACKLINES
	if (ctx->debuglist != NULL) {
		summary->contextsize +=
			(ctx->max_size + 1) * sizeof(debuglist_t) +
			ctx->debuglistcnt * sizeof(debuglink_t);
	}
#endif
	xmlTextWriterStartElement(writer, ISC_XMLCHAR "references");
	xmlTextWriterWriteFormatString(writer, "%d", ctx->references);
	xmlTextWriterEndElement(writer); /* references */

	summary->total += ctx->total;
	xmlTextWriterStartElement(writer, ISC_XMLCHAR "total");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       (isc_uint64_t)ctx->total);
	xmlTextWriterEndElement(writer); /* total */

	summary->inuse += ctx->inuse;
	xmlTextWriterStartElement(writer, ISC_XMLCHAR "inuse");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       (isc_uint64_t)ctx->inuse);
	xmlTextWriterEndElement(writer); /* inuse */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "maxinuse");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       (isc_uint64_t)ctx->maxinuse);
	xmlTextWriterEndElement(writer); /* maxinuse */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "blocksize");
	if ((ctx->flags & ISC_MEMFLAG_INTERNAL) != 0) {
		summary->blocksize += ctx->basic_table_count *
			NUM_BASIC_BLOCKS * ctx->mem_target;
		xmlTextWriterWriteFormatString(writer,
					       "%" ISC_PRINT_QUADFORMAT "u",
					       (isc_uint64_t)
					       ctx->basic_table_count *
					       NUM_BASIC_BLOCKS *
					       ctx->mem_target);
	} else
		xmlTextWriterWriteFormatString(writer, "%s", "-");
	xmlTextWriterEndElement(writer); /* blocksize */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "pools");
	xmlTextWriterWriteFormatString(writer, "%u", ctx->poolcnt);
	xmlTextWriterEndElement(writer); /* pools */
	summary->contextsize += ctx->poolcnt * sizeof(isc_mempool_t);

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "hiwater");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       (isc_uint64_t)ctx->hi_water);
	xmlTextWriterEndElement(writer); /* hiwater */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "lowater");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       (isc_uint64_t)ctx->lo_water);
	xmlTextWriterEndElement(writer); /* lowater */

	MCTXUNLOCK(ctx, &ctx->lock);

	xmlTextWriterEndElement(writer); /* context */
}

void
isc_mem_renderxml(xmlTextWriterPtr writer) {
	isc__mem_t *ctx;
	summarystat_t summary;
	isc_uint64_t lost;

	memset(&summary, 0, sizeof(summary));

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "contexts");

	RUNTIME_CHECK(isc_once_do(&once, initialize_action) == ISC_R_SUCCESS);

	LOCK(&lock);
	lost = totallost;
	for (ctx = ISC_LIST_HEAD(contexts);
	     ctx != NULL;
	     ctx = ISC_LIST_NEXT(ctx, link)) {
		renderctx(ctx, &summary, writer);
	}
	UNLOCK(&lock);

	xmlTextWriterEndElement(writer); /* contexts */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "summary");

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "TotalUse");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       summary.total);
	xmlTextWriterEndElement(writer); /* TotalUse */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "InUse");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       summary.inuse);
	xmlTextWriterEndElement(writer); /* InUse */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "BlockSize");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       summary.blocksize);
	xmlTextWriterEndElement(writer); /* BlockSize */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "ContextSize");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       summary.contextsize);
	xmlTextWriterEndElement(writer); /* ContextSize */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "Lost");
	xmlTextWriterWriteFormatString(writer, "%" ISC_PRINT_QUADFORMAT "u",
				       lost);
	xmlTextWriterEndElement(writer); /* Lost */

	xmlTextWriterEndElement(writer); /* summary */
}

#endif /* HAVE_LIBXML2 */
#endif /* BIND9 */
