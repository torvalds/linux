/*
 * Copyright (C) 2004-2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1997-2001  Internet Software Consortium.
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

#ifndef ISC_MEM_H
#define ISC_MEM_H 1

/*! \file isc/mem.h */

#include <stdio.h>

#include <isc/lang.h>
#include <isc/mutex.h>
#include <isc/platform.h>
#include <isc/types.h>
#include <isc/xml.h>

ISC_LANG_BEGINDECLS

#define ISC_MEM_LOWATER 0
#define ISC_MEM_HIWATER 1
typedef void (*isc_mem_water_t)(void *, int);

typedef void * (*isc_memalloc_t)(void *, size_t);
typedef void (*isc_memfree_t)(void *, void *);

/*%
 * Define ISC_MEM_DEBUG=1 to make all functions that free memory
 * set the pointer being freed to NULL after being freed.
 * This is the default; set ISC_MEM_DEBUG=0 to disable it.
 */
#ifndef ISC_MEM_DEBUG
#define ISC_MEM_DEBUG 1
#endif

/*%
 * Define ISC_MEM_TRACKLINES=1 to turn on detailed tracing of memory
 * allocation and freeing by file and line number.
 */
#ifndef ISC_MEM_TRACKLINES
#define ISC_MEM_TRACKLINES 1
#endif

/*%
 * Define ISC_MEM_CHECKOVERRUN=1 to turn on checks for using memory outside
 * the requested space.  This will increase the size of each allocation.
 */
#ifndef ISC_MEM_CHECKOVERRUN
#define ISC_MEM_CHECKOVERRUN 1
#endif

/*%
 * Define ISC_MEM_FILL=1 to fill each block of memory returned to the system
 * with the byte string '0xbe'.  This helps track down uninitialized pointers
 * and the like.  On freeing memory, the space is filled with '0xde' for
 * the same reasons.
 */
#ifndef ISC_MEM_FILL
#define ISC_MEM_FILL 1
#endif

/*%
 * Define ISC_MEMPOOL_NAMES=1 to make memory pools store a symbolic
 * name so that the leaking pool can be more readily identified in
 * case of a memory leak.
 */
#ifndef ISC_MEMPOOL_NAMES
#define ISC_MEMPOOL_NAMES 1
#endif

LIBISC_EXTERNAL_DATA extern unsigned int isc_mem_debugging;
/*@{*/
#define ISC_MEM_DEBUGTRACE		0x00000001U
#define ISC_MEM_DEBUGRECORD		0x00000002U
#define ISC_MEM_DEBUGUSAGE		0x00000004U
#define ISC_MEM_DEBUGSIZE		0x00000008U
#define ISC_MEM_DEBUGCTX		0x00000010U
#define ISC_MEM_DEBUGALL		0x0000001FU
/*!<
 * The variable isc_mem_debugging holds a set of flags for
 * turning certain memory debugging options on or off at
 * runtime.  It is initialized to the value ISC_MEM_DEGBUGGING,
 * which is 0 by default but may be overridden at compile time.
 * The following flags can be specified:
 *
 * \li #ISC_MEM_DEBUGTRACE
 *	Log each allocation and free to isc_lctx.
 *
 * \li #ISC_MEM_DEBUGRECORD
 *	Remember each allocation, and match them up on free.
 *	Crash if a free doesn't match an allocation.
 *
 * \li #ISC_MEM_DEBUGUSAGE
 *	If a hi_water mark is set, print the maximum inuse memory
 *	every time it is raised once it exceeds the hi_water mark.
 *
 * \li #ISC_MEM_DEBUGSIZE
 *	Check the size argument being passed to isc_mem_put() matches
 *	that passed to isc_mem_get().
 *
 * \li #ISC_MEM_DEBUGCTX
 *	Check the mctx argument being passed to isc_mem_put() matches
 *	that passed to isc_mem_get().
 */
/*@}*/

#if ISC_MEM_TRACKLINES
#define _ISC_MEM_FILELINE	, __FILE__, __LINE__
#define _ISC_MEM_FLARG		, const char *, unsigned int
#else
#define _ISC_MEM_FILELINE
#define _ISC_MEM_FLARG
#endif

/*!
 * Define ISC_MEM_USE_INTERNAL_MALLOC=1 to use the internal malloc()
 * implementation in preference to the system one.  The internal malloc()
 * is very space-efficient, and quite fast on uniprocessor systems.  It
 * performs poorly on multiprocessor machines.
 * JT: we can overcome the performance issue on multiprocessor machines
 * by carefully separating memory contexts.
 */

#ifndef ISC_MEM_USE_INTERNAL_MALLOC
#define ISC_MEM_USE_INTERNAL_MALLOC 1
#endif

/*
 * Flags for isc_mem_create2()calls.
 */
#define ISC_MEMFLAG_NOLOCK	0x00000001	 /* no lock is necessary */
#define ISC_MEMFLAG_INTERNAL	0x00000002	 /* use internal malloc */
#if ISC_MEM_USE_INTERNAL_MALLOC
#define ISC_MEMFLAG_DEFAULT 	ISC_MEMFLAG_INTERNAL
#else
#define ISC_MEMFLAG_DEFAULT 	0
#endif


/*%<
 * We use either isc___mem (three underscores) or isc__mem (two) depending on
 * whether it's for BIND9's internal purpose (with -DBIND9) or generic export
 * library.  This condition is generally handled in isc/namespace.h, but for
 * Windows it doesn't work if it involves multiple times of macro expansion
 * (such as isc_mem to isc__mem then to isc___mem).  The following definitions
 * are used to work around this portability issue.  Right now, we don't support
 * the export library for Windows, so we always use the three-underscore
 * version.
 */
#ifdef WIN32
#define ISCMEMFUNC(sfx) isc___mem_ ## sfx
#define ISCMEMPOOLFUNC(sfx) isc___mempool_ ## sfx
#else
#define ISCMEMFUNC(sfx) isc__mem_ ## sfx
#define ISCMEMPOOLFUNC(sfx) isc__mempool_ ## sfx
#endif

#define isc_mem_get(c, s)	ISCMEMFUNC(get)((c), (s) _ISC_MEM_FILELINE)
#define isc_mem_allocate(c, s)	ISCMEMFUNC(allocate)((c), (s) _ISC_MEM_FILELINE)
#define isc_mem_reallocate(c, p, s) ISCMEMFUNC(reallocate)((c), (p), (s) _ISC_MEM_FILELINE)
#define isc_mem_strdup(c, p)	ISCMEMFUNC(strdup)((c), (p) _ISC_MEM_FILELINE)
#define isc_mempool_get(c)	ISCMEMPOOLFUNC(get)((c) _ISC_MEM_FILELINE)

/*%
 * isc_mem_putanddetach() is a convenience function for use where you
 * have a structure with an attached memory context.
 *
 * Given:
 *
 * \code
 * struct {
 *	...
 *	isc_mem_t *mctx;
 *	...
 * } *ptr;
 *
 * isc_mem_t *mctx;
 *
 * isc_mem_putanddetach(&ptr->mctx, ptr, sizeof(*ptr));
 * \endcode
 *
 * is the equivalent of:
 *
 * \code
 * mctx = NULL;
 * isc_mem_attach(ptr->mctx, &mctx);
 * isc_mem_detach(&ptr->mctx);
 * isc_mem_put(mctx, ptr, sizeof(*ptr));
 * isc_mem_detach(&mctx);
 * \endcode
 */

/*% memory and memory pool methods */
typedef struct isc_memmethods {
	void (*attach)(isc_mem_t *source, isc_mem_t **targetp);
	void (*detach)(isc_mem_t **mctxp);
	void (*destroy)(isc_mem_t **mctxp);
	void *(*memget)(isc_mem_t *mctx, size_t size _ISC_MEM_FLARG);
	void (*memput)(isc_mem_t *mctx, void *ptr, size_t size _ISC_MEM_FLARG);
	void (*memputanddetach)(isc_mem_t **mctxp, void *ptr,
				size_t size _ISC_MEM_FLARG);
	void *(*memallocate)(isc_mem_t *mctx, size_t size _ISC_MEM_FLARG);
	void *(*memreallocate)(isc_mem_t *mctx, void *ptr,
			       size_t size _ISC_MEM_FLARG);
	char *(*memstrdup)(isc_mem_t *mctx, const char *s _ISC_MEM_FLARG);
	void (*memfree)(isc_mem_t *mctx, void *ptr _ISC_MEM_FLARG);
	void (*setdestroycheck)(isc_mem_t *mctx, isc_boolean_t flag);
	void (*setwater)(isc_mem_t *ctx, isc_mem_water_t water,
			 void *water_arg, size_t hiwater, size_t lowater);
	void (*waterack)(isc_mem_t *ctx, int flag);
	size_t (*inuse)(isc_mem_t *mctx);
	isc_boolean_t (*isovermem)(isc_mem_t *mctx);
	isc_result_t (*mpcreate)(isc_mem_t *mctx, size_t size,
				 isc_mempool_t **mpctxp);
} isc_memmethods_t;

typedef struct isc_mempoolmethods {
	void (*destroy)(isc_mempool_t **mpctxp);
	void *(*get)(isc_mempool_t *mpctx _ISC_MEM_FLARG);
	void (*put)(isc_mempool_t *mpctx, void *mem _ISC_MEM_FLARG);
	unsigned int (*getallocated)(isc_mempool_t *mpctx);
	void (*setmaxalloc)(isc_mempool_t *mpctx, unsigned int limit);
	void (*setfreemax)(isc_mempool_t *mpctx, unsigned int limit);
	void (*setname)(isc_mempool_t *mpctx, const char *name);
	void (*associatelock)(isc_mempool_t *mpctx, isc_mutex_t *lock);
	void (*setfillcount)(isc_mempool_t *mpctx, unsigned int limit);
} isc_mempoolmethods_t;

/*%
 * This structure is actually just the common prefix of a memory context
 * implementation's version of an isc_mem_t.
 * \brief
 * Direct use of this structure by clients is forbidden.  mctx implementations
 * may change the structure.  'magic' must be ISCAPI_MCTX_MAGIC for any of the
 * isc_mem_ routines to work.  mctx implementations must maintain all mctx
 * invariants.
 */
struct isc_mem {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_memmethods_t	*methods;
};

#define ISCAPI_MCTX_MAGIC	ISC_MAGIC('A','m','c','x')
#define ISCAPI_MCTX_VALID(m)	((m) != NULL && \
				 (m)->magic == ISCAPI_MCTX_MAGIC)

/*%
 * This is the common prefix of a memory pool context.  The same note as
 * that for the mem structure applies.
 */
struct isc_mempool {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_mempoolmethods_t	*methods;
};

#define ISCAPI_MPOOL_MAGIC	ISC_MAGIC('A','m','p','l')
#define ISCAPI_MPOOL_VALID(mp)	((mp) != NULL && \
				 (mp)->magic == ISCAPI_MPOOL_MAGIC)

#if ISC_MEM_DEBUG
#define isc_mem_put(c, p, s) \
	do { \
		ISCMEMFUNC(put)((c), (p), (s) _ISC_MEM_FILELINE);	\
		(p) = NULL; \
	} while (0)
#define isc_mem_putanddetach(c, p, s) \
	do { \
		ISCMEMFUNC(putanddetach)((c), (p), (s) _ISC_MEM_FILELINE); \
		(p) = NULL; \
	} while (0)
#define isc_mem_free(c, p) \
	do { \
		ISCMEMFUNC(free)((c), (p) _ISC_MEM_FILELINE);	\
		(p) = NULL; \
	} while (0)
#define isc_mempool_put(c, p) \
	do { \
		ISCMEMPOOLFUNC(put)((c), (p) _ISC_MEM_FILELINE);	\
		(p) = NULL; \
	} while (0)
#else
#define isc_mem_put(c, p, s)	ISCMEMFUNC(put)((c), (p), (s) _ISC_MEM_FILELINE)
#define isc_mem_putanddetach(c, p, s) \
	ISCMEMFUNC(putanddetach)((c), (p), (s) _ISC_MEM_FILELINE)
#define isc_mem_free(c, p)	ISCMEMFUNC(free)((c), (p) _ISC_MEM_FILELINE)
#define isc_mempool_put(c, p)	ISCMEMPOOLFUNC(put)((c), (p) _ISC_MEM_FILELINE)
#endif

/*@{*/
isc_result_t
isc_mem_create(size_t max_size, size_t target_size,
	       isc_mem_t **mctxp);

isc_result_t
isc_mem_create2(size_t max_size, size_t target_size,
		isc_mem_t **mctxp, unsigned int flags);

isc_result_t
isc_mem_createx(size_t max_size, size_t target_size,
		isc_memalloc_t memalloc, isc_memfree_t memfree,
		void *arg, isc_mem_t **mctxp);

isc_result_t
isc_mem_createx2(size_t max_size, size_t target_size,
		 isc_memalloc_t memalloc, isc_memfree_t memfree,
		 void *arg, isc_mem_t **mctxp, unsigned int flags);

/*!<
 * \brief Create a memory context.
 *
 * 'max_size' and 'target_size' are tuning parameters.  When
 * ISC_MEMFLAG_INTERNAL is set, allocations smaller than 'max_size'
 * will be satisfied by getting blocks of size 'target_size' from the
 * system allocator and breaking them up into pieces; larger allocations
 * will use the system allocator directly. If 'max_size' and/or
 * 'target_size' are zero, default values will be * used.  When
 * ISC_MEMFLAG_INTERNAL is not set, 'target_size' is ignored.
 *
 * 'max_size' is also used to size the statistics arrays and the array
 * used to record active memory when ISC_MEM_DEBUGRECORD is set.  Setting
 * 'max_size' too low can have detrimental effects on performance.
 *
 * A memory context created using isc_mem_createx() will obtain
 * memory from the system by calling 'memalloc' and 'memfree',
 * passing them the argument 'arg'.  A memory context created
 * using isc_mem_create() will use the standard library malloc()
 * and free().
 *
 * If ISC_MEMFLAG_NOLOCK is set in 'flags', the corresponding memory context
 * will be accessed without locking.  The user who creates the context must
 * ensure there be no race.  Since this can be a source of bug, it is generally
 * inadvisable to use this flag unless the user is very sure about the race
 * condition and the access to the object is highly performance sensitive.
 *
 * Requires:
 * mctxp != NULL && *mctxp == NULL */
/*@}*/

/*@{*/
void
isc_mem_attach(isc_mem_t *, isc_mem_t **);
void
isc_mem_detach(isc_mem_t **);
/*!<
 * \brief Attach to / detach from a memory context.
 *
 * This is intended for applications that use multiple memory contexts
 * in such a way that it is not obvious when the last allocations from
 * a given context has been freed and destroying the context is safe.
 *
 * Most applications do not need to call these functions as they can
 * simply create a single memory context at the beginning of main()
 * and destroy it at the end of main(), thereby guaranteeing that it
 * is not destroyed while there are outstanding allocations.
 */
/*@}*/

void
isc_mem_destroy(isc_mem_t **);
/*%<
 * Destroy a memory context.
 */

isc_result_t
isc_mem_ondestroy(isc_mem_t *ctx,
		  isc_task_t *task,
		  isc_event_t **event);
/*%<
 * Request to be notified with an event when a memory context has
 * been successfully destroyed.
 */

void
isc_mem_stats(isc_mem_t *mctx, FILE *out);
/*%<
 * Print memory usage statistics for 'mctx' on the stream 'out'.
 */

void
isc_mem_setdestroycheck(isc_mem_t *mctx,
			isc_boolean_t on);
/*%<
 * If 'on' is ISC_TRUE, 'mctx' will check for memory leaks when
 * destroyed and abort the program if any are present.
 */

/*@{*/
void
isc_mem_setquota(isc_mem_t *, size_t);
size_t
isc_mem_getquota(isc_mem_t *);
/*%<
 * Set/get the memory quota of 'mctx'.  This is a hard limit
 * on the amount of memory that may be allocated from mctx;
 * if it is exceeded, allocations will fail.
 */
/*@}*/

size_t
isc_mem_inuse(isc_mem_t *mctx);
/*%<
 * Get an estimate of the number of memory in use in 'mctx', in bytes.
 * This includes quantization overhead, but does not include memory
 * allocated from the system but not yet used.
 */

isc_boolean_t
isc_mem_isovermem(isc_mem_t *mctx);
/*%<
 * Return true iff the memory context is in "over memory" state, i.e.,
 * a hiwater mark has been set and the used amount of memory has exceeds
 * the mark.
 */

void
isc_mem_setwater(isc_mem_t *mctx, isc_mem_water_t water, void *water_arg,
		 size_t hiwater, size_t lowater);
/*%<
 * Set high and low water marks for this memory context.
 *
 * When the memory usage of 'mctx' exceeds 'hiwater',
 * '(water)(water_arg, #ISC_MEM_HIWATER)' will be called.  'water' needs to
 * call isc_mem_waterack() with #ISC_MEM_HIWATER to acknowledge the state
 * change.  'water' may be called multiple times.
 *
 * When the usage drops below 'lowater', 'water' will again be called, this
 * time with #ISC_MEM_LOWATER.  'water' need to calls isc_mem_waterack() with
 * #ISC_MEM_LOWATER to acknowledge the change.
 *
 *	static void
 *	water(void *arg, int mark) {
 *		struct foo *foo = arg;
 *
 *		LOCK(&foo->marklock);
 *		if (foo->mark != mark) {
 * 			foo->mark = mark;
 *			....
 *			isc_mem_waterack(foo->mctx, mark);
 *		}
 *		UNLOCK(&foo->marklock);
 *	}
 *
 * If 'water' is NULL then 'water_arg', 'hi_water' and 'lo_water' are
 * ignored and the state is reset.
 *
 * Requires:
 *
 *	'water' is not NULL.
 *	hi_water >= lo_water
 */

void
isc_mem_waterack(isc_mem_t *ctx, int mark);
/*%<
 * Called to acknowledge changes in signaled by calls to 'water'.
 */

void
isc_mem_printactive(isc_mem_t *mctx, FILE *file);
/*%<
 * Print to 'file' all active memory in 'mctx'.
 *
 * Requires ISC_MEM_DEBUGRECORD to have been set.
 */

void
isc_mem_printallactive(FILE *file);
/*%<
 * Print to 'file' all active memory in all contexts.
 *
 * Requires ISC_MEM_DEBUGRECORD to have been set.
 */

void
isc_mem_checkdestroyed(FILE *file);
/*%<
 * Check that all memory contexts have been destroyed.
 * Prints out those that have not been.
 * Fatally fails if there are still active contexts.
 */

unsigned int
isc_mem_references(isc_mem_t *ctx);
/*%<
 * Return the current reference count.
 */

void
isc_mem_setname(isc_mem_t *ctx, const char *name, void *tag);
/*%<
 * Name 'ctx'.
 *
 * Notes:
 *
 *\li	Only the first 15 characters of 'name' will be copied.
 *
 *\li	'tag' is for debugging purposes only.
 *
 * Requires:
 *
 *\li	'ctx' is a valid ctx.
 */

const char *
isc_mem_getname(isc_mem_t *ctx);
/*%<
 * Get the name of 'ctx', as previously set using isc_mem_setname().
 *
 * Requires:
 *\li	'ctx' is a valid ctx.
 *
 * Returns:
 *\li	A non-NULL pointer to a null-terminated string.
 * 	If the ctx has not been named, the string is
 * 	empty.
 */

void *
isc_mem_gettag(isc_mem_t *ctx);
/*%<
 * Get the tag value for  'task', as previously set using isc_mem_setname().
 *
 * Requires:
 *\li	'ctx' is a valid ctx.
 *
 * Notes:
 *\li	This function is for debugging purposes only.
 *
 * Requires:
 *\li	'ctx' is a valid task.
 */

#ifdef HAVE_LIBXML2
void
isc_mem_renderxml(xmlTextWriterPtr writer);
/*%<
 * Render all contexts' statistics and status in XML for writer.
 */
#endif /* HAVE_LIBXML2 */

/*
 * Memory pools
 */

isc_result_t
isc_mempool_create(isc_mem_t *mctx, size_t size, isc_mempool_t **mpctxp);
/*%<
 * Create a memory pool.
 *
 * Requires:
 *\li	mctx is a valid memory context.
 *\li	size > 0
 *\li	mpctxp != NULL and *mpctxp == NULL
 *
 * Defaults:
 *\li	maxalloc = UINT_MAX
 *\li	freemax = 1
 *\li	fillcount = 1
 *
 * Returns:
 *\li	#ISC_R_NOMEMORY		-- not enough memory to create pool
 *\li	#ISC_R_SUCCESS		-- all is well.
 */

void
isc_mempool_destroy(isc_mempool_t **mpctxp);
/*%<
 * Destroy a memory pool.
 *
 * Requires:
 *\li	mpctxp != NULL && *mpctxp is a valid pool.
 *\li	The pool has no un"put" allocations outstanding
 */

void
isc_mempool_setname(isc_mempool_t *mpctx, const char *name);
/*%<
 * Associate a name with a memory pool.  At most 15 characters may be used.
 *
 * Requires:
 *\li	mpctx is a valid pool.
 *\li	name != NULL;
 */

void
isc_mempool_associatelock(isc_mempool_t *mpctx, isc_mutex_t *lock);
/*%<
 * Associate a lock with this memory pool.
 *
 * This lock is used when getting or putting items using this memory pool,
 * and it is also used to set or get internal state via the isc_mempool_get*()
 * and isc_mempool_set*() set of functions.
 *
 * Multiple pools can each share a single lock.  For instance, if "manager"
 * type object contained pools for various sizes of events, and each of
 * these pools used a common lock.  Note that this lock must NEVER be used
 * by other than mempool routines once it is given to a pool, since that can
 * easily cause double locking.
 *
 * Requires:
 *
 *\li	mpctpx is a valid pool.
 *
 *\li	lock != NULL.
 *
 *\li	No previous lock is assigned to this pool.
 *
 *\li	The lock is initialized before calling this function via the normal
 *	means of doing that.
 */

/*
 * The following functions get/set various parameters.  Note that due to
 * the unlocked nature of pools these are potentially random values unless
 * the imposed externally provided locking protocols are followed.
 *
 * Also note that the quota limits will not always take immediate effect.
 * For instance, setting "maxalloc" to a number smaller than the currently
 * allocated count is permitted.  New allocations will be refused until
 * the count drops below this threshold.
 *
 * All functions require (in addition to other requirements):
 *	mpctx is a valid memory pool
 */

unsigned int
isc_mempool_getfreemax(isc_mempool_t *mpctx);
/*%<
 * Returns the maximum allowed size of the free list.
 */

void
isc_mempool_setfreemax(isc_mempool_t *mpctx, unsigned int limit);
/*%<
 * Sets the maximum allowed size of the free list.
 */

unsigned int
isc_mempool_getfreecount(isc_mempool_t *mpctx);
/*%<
 * Returns current size of the free list.
 */

unsigned int
isc_mempool_getmaxalloc(isc_mempool_t *mpctx);
/*!<
 * Returns the maximum allowed number of allocations.
 */

void
isc_mempool_setmaxalloc(isc_mempool_t *mpctx, unsigned int limit);
/*%<
 * Sets the maximum allowed number of allocations.
 *
 * Additional requirements:
 *\li	limit > 0
 */

unsigned int
isc_mempool_getallocated(isc_mempool_t *mpctx);
/*%<
 * Returns the number of items allocated from this pool.
 */

unsigned int
isc_mempool_getfillcount(isc_mempool_t *mpctx);
/*%<
 * Returns the number of items allocated as a block from the parent memory
 * context when the free list is empty.
 */

void
isc_mempool_setfillcount(isc_mempool_t *mpctx, unsigned int limit);
/*%<
 * Sets the fillcount.
 *
 * Additional requirements:
 *\li	limit > 0
 */


/*
 * Pseudo-private functions for use via macros.  Do not call directly.
 */
void *
ISCMEMFUNC(get)(isc_mem_t *, size_t _ISC_MEM_FLARG);
void
ISCMEMFUNC(putanddetach)(isc_mem_t **, void *, size_t _ISC_MEM_FLARG);
void
ISCMEMFUNC(put)(isc_mem_t *, void *, size_t _ISC_MEM_FLARG);
void *
ISCMEMFUNC(allocate)(isc_mem_t *, size_t _ISC_MEM_FLARG);
void *
ISCMEMFUNC(reallocate)(isc_mem_t *, void *, size_t _ISC_MEM_FLARG);
void
ISCMEMFUNC(free)(isc_mem_t *, void * _ISC_MEM_FLARG);
char *
ISCMEMFUNC(strdup)(isc_mem_t *, const char *_ISC_MEM_FLARG);
void *
ISCMEMPOOLFUNC(get)(isc_mempool_t * _ISC_MEM_FLARG);
void
ISCMEMPOOLFUNC(put)(isc_mempool_t *, void * _ISC_MEM_FLARG);

#ifdef USE_MEMIMPREGISTER

/*%<
 * See isc_mem_create2() above.
 */
typedef isc_result_t
(*isc_memcreatefunc_t)(size_t init_max_size, size_t target_size,
		       isc_mem_t **ctxp, unsigned int flags);

isc_result_t
isc_mem_register(isc_memcreatefunc_t createfunc);
/*%<
 * Register a new memory management implementation and add it to the list of
 * supported implementations.  This function must be called when a different
 * memory management library is used than the one contained in the ISC library.
 */

isc_result_t
isc__mem_register(void);
/*%<
 * A short cut function that specifies the memory management module in the ISC
 * library for isc_mem_register().  An application that uses the ISC library
 * usually do not have to care about this function: it would call
 * isc_lib_register(), which internally calls this function.
 */
#endif /* USE_MEMIMPREGISTER */

ISC_LANG_ENDDECLS

#endif /* ISC_MEM_H */
