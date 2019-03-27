/*
 * Copyright (c) 2000-2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: rpool.c,v 1.29 2013-11-22 20:51:43 ca Exp $")

/*
**  resource pools
**  For documentation, see rpool.html
*/

#include <sm/exc.h>
#include <sm/heap.h>
#include <sm/rpool.h>
#include <sm/varargs.h>
#include <sm/conf.h>
#if _FFR_PERF_RPOOL
# include <syslog.h>
#endif /* _FFR_PERF_RPOOL */

const char SmRpoolMagic[] = "sm_rpool";

typedef union
{
	SM_POOLLINK_T	link;
	char		align[SM_ALIGN_SIZE];
} SM_POOLHDR_T;

static char	*sm_rpool_allocblock_x __P((SM_RPOOL_T *, size_t));
static char	*sm_rpool_allocblock __P((SM_RPOOL_T *, size_t));

/*
**  Tune this later
*/

#define POOLSIZE		4096
#define BIG_OBJECT_RATIO	10

/*
**  SM_RPOOL_ALLOCBLOCK_X -- allocate a new block for an rpool.
**
**	Parameters:
**		rpool -- rpool to which the block should be added.
**		size -- size of block.
**
**	Returns:
**		Pointer to block.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

static char *
sm_rpool_allocblock_x(rpool, size)
	SM_RPOOL_T *rpool;
	size_t size;
{
	SM_POOLLINK_T *p;

	p = sm_malloc_x(sizeof(SM_POOLHDR_T) + size);
	p->sm_pnext = rpool->sm_pools;
	rpool->sm_pools = p;
	return (char*) p + sizeof(SM_POOLHDR_T);
}

/*
**  SM_RPOOL_ALLOCBLOCK -- allocate a new block for an rpool.
**
**	Parameters:
**		rpool -- rpool to which the block should be added.
**		size -- size of block.
**
**	Returns:
**		Pointer to block, NULL on failure.
*/

static char *
sm_rpool_allocblock(rpool, size)
	SM_RPOOL_T *rpool;
	size_t size;
{
	SM_POOLLINK_T *p;

	p = sm_malloc(sizeof(SM_POOLHDR_T) + size);
	if (p == NULL)
		return NULL;
	p->sm_pnext = rpool->sm_pools;
	rpool->sm_pools = p;
	return (char*) p + sizeof(SM_POOLHDR_T);
}

/*
**  SM_RPOOL_MALLOC_TAGGED_X -- allocate memory from rpool
**
**	Parameters:
**		rpool -- rpool from which memory should be allocated;
**			can be NULL, use sm_malloc() then.
**		size -- size of block.
**		file -- filename.
**		line -- line number in file.
**		group -- heap group for debugging.
**
**	Returns:
**		Pointer to block.
**
**	Exceptions:
**		F:sm_heap -- out of memory
**
**	Notice: XXX
**		if size == 0 and the rpool is new (no memory
**		allocated yet) NULL is returned!
**		We could solve this by
**		- wasting 1 byte (size < avail)
**		- checking for rpool->sm_poolptr != NULL
**		- not asking for 0 sized buffer
*/

void *
#if SM_HEAP_CHECK
sm_rpool_malloc_tagged_x(rpool, size, file, line, group)
	SM_RPOOL_T *rpool;
	size_t size;
	char *file;
	int line;
	int group;
#else /* SM_HEAP_CHECK */
sm_rpool_malloc_x(rpool, size)
	SM_RPOOL_T *rpool;
	size_t size;
#endif /* SM_HEAP_CHECK */
{
	char *ptr;

	if (rpool == NULL)
		return sm_malloc_tagged_x(size, file, line, group);

	/* Ensure that size is properly aligned. */
	if (size & SM_ALIGN_BITS)
		size = (size & ~SM_ALIGN_BITS) + SM_ALIGN_SIZE;

	/* The common case.  This is optimized for speed. */
	if (size <= rpool->sm_poolavail)
	{
		ptr = rpool->sm_poolptr;
		rpool->sm_poolptr += size;
		rpool->sm_poolavail -= size;
		return ptr;
	}

	/*
	**  The slow case: we need to call malloc.
	**  The SM_REQUIRE assertion is deferred until now, for speed.
	**  That's okay: we set rpool->sm_poolavail to 0 when we free an rpool,
	**  so the common case code won't be triggered on a dangling pointer.
	*/

	SM_REQUIRE(rpool->sm_magic == SmRpoolMagic);

	/*
	**  If size > sm_poolsize, then malloc a new block especially for
	**  this request.  Future requests will be allocated from the
	**  current pool.
	**
	**  What if the current pool is mostly unallocated, and the current
	**  request is larger than the available space, but < sm_poolsize?
	**  If we discard the current pool, and start allocating from a new
	**  pool, then we will be wasting a lot of space.  For this reason,
	**  we malloc a block just for the current request if size >
	**  sm_bigobjectsize, where sm_bigobjectsize <= sm_poolsize.
	**  Thus, the most space that we will waste at the end of a pool
	**  is sm_bigobjectsize - 1.
	*/

	if (size > rpool->sm_bigobjectsize)
	{
#if _FFR_PERF_RPOOL
		++rpool->sm_nbigblocks;
#endif /* _FFR_PERF_RPOOL */
		return sm_rpool_allocblock_x(rpool, size);
	}
	SM_ASSERT(rpool->sm_bigobjectsize <= rpool->sm_poolsize);
	ptr = sm_rpool_allocblock_x(rpool, rpool->sm_poolsize);
	rpool->sm_poolptr = ptr + size;
	rpool->sm_poolavail = rpool->sm_poolsize - size;
#if _FFR_PERF_RPOOL
	++rpool->sm_npools;
#endif /* _FFR_PERF_RPOOL */
	return ptr;
}

/*
**  SM_RPOOL_MALLOC_TAGGED -- allocate memory from rpool
**
**	Parameters:
**		rpool -- rpool from which memory should be allocated;
**			can be NULL, use sm_malloc() then.
**		size -- size of block.
**		file -- filename.
**		line -- line number in file.
**		group -- heap group for debugging.
**
**	Returns:
**		Pointer to block, NULL on failure.
**
**	Notice: XXX
**		if size == 0 and the rpool is new (no memory
**		allocated yet) NULL is returned!
**		We could solve this by
**		- wasting 1 byte (size < avail)
**		- checking for rpool->sm_poolptr != NULL
**		- not asking for 0 sized buffer
*/

void *
#if SM_HEAP_CHECK
sm_rpool_malloc_tagged(rpool, size, file, line, group)
	SM_RPOOL_T *rpool;
	size_t size;
	char *file;
	int line;
	int group;
#else /* SM_HEAP_CHECK */
sm_rpool_malloc(rpool, size)
	SM_RPOOL_T *rpool;
	size_t size;
#endif /* SM_HEAP_CHECK */
{
	char *ptr;

	if (rpool == NULL)
		return sm_malloc_tagged(size, file, line, group);

	/* Ensure that size is properly aligned. */
	if (size & SM_ALIGN_BITS)
		size = (size & ~SM_ALIGN_BITS) + SM_ALIGN_SIZE;

	/* The common case.  This is optimized for speed. */
	if (size <= rpool->sm_poolavail)
	{
		ptr = rpool->sm_poolptr;
		rpool->sm_poolptr += size;
		rpool->sm_poolavail -= size;
		return ptr;
	}

	/*
	**  The slow case: we need to call malloc.
	**  The SM_REQUIRE assertion is deferred until now, for speed.
	**  That's okay: we set rpool->sm_poolavail to 0 when we free an rpool,
	**  so the common case code won't be triggered on a dangling pointer.
	*/

	SM_REQUIRE(rpool->sm_magic == SmRpoolMagic);

	/*
	**  If size > sm_poolsize, then malloc a new block especially for
	**  this request.  Future requests will be allocated from the
	**  current pool.
	**
	**  What if the current pool is mostly unallocated, and the current
	**  request is larger than the available space, but < sm_poolsize?
	**  If we discard the current pool, and start allocating from a new
	**  pool, then we will be wasting a lot of space.  For this reason,
	**  we malloc a block just for the current request if size >
	**  sm_bigobjectsize, where sm_bigobjectsize <= sm_poolsize.
	**  Thus, the most space that we will waste at the end of a pool
	**  is sm_bigobjectsize - 1.
	*/

	if (size > rpool->sm_bigobjectsize)
	{
#if _FFR_PERF_RPOOL
		++rpool->sm_nbigblocks;
#endif /* _FFR_PERF_RPOOL */
		return sm_rpool_allocblock(rpool, size);
	}
	SM_ASSERT(rpool->sm_bigobjectsize <= rpool->sm_poolsize);
	ptr = sm_rpool_allocblock(rpool, rpool->sm_poolsize);
	if (ptr == NULL)
		return NULL;
	rpool->sm_poolptr = ptr + size;
	rpool->sm_poolavail = rpool->sm_poolsize - size;
#if _FFR_PERF_RPOOL
	++rpool->sm_npools;
#endif /* _FFR_PERF_RPOOL */
	return ptr;
}

/*
**  SM_RPOOL_NEW_X -- create a new rpool.
**
**	Parameters:
**		parent -- pointer to parent rpool, can be NULL.
**
**	Returns:
**		Pointer to new rpool.
*/

SM_RPOOL_T *
sm_rpool_new_x(parent)
	SM_RPOOL_T *parent;
{
	SM_RPOOL_T *rpool;

	rpool = sm_malloc_x(sizeof(SM_RPOOL_T));
	if (parent == NULL)
		rpool->sm_parentlink = NULL;
	else
	{
		SM_TRY
			rpool->sm_parentlink = sm_rpool_attach_x(parent,
					(SM_RPOOL_RFREE_T) sm_rpool_free,
					(void *) rpool);
		SM_EXCEPT(exc, "*")
			sm_free(rpool);
			sm_exc_raise_x(exc);
		SM_END_TRY
	}
	rpool->sm_magic = SmRpoolMagic;

	rpool->sm_poolsize = POOLSIZE - sizeof(SM_POOLHDR_T);
	rpool->sm_bigobjectsize = rpool->sm_poolsize / BIG_OBJECT_RATIO;
	rpool->sm_poolptr = NULL;
	rpool->sm_poolavail = 0;
	rpool->sm_pools = NULL;

	rpool->sm_rptr = NULL;
	rpool->sm_ravail = 0;
	rpool->sm_rlists = NULL;
#if _FFR_PERF_RPOOL
	rpool->sm_nbigblocks = 0;
	rpool->sm_npools = 0;
#endif /* _FFR_PERF_RPOOL */

	return rpool;
}

/*
**  SM_RPOOL_SETSIZES -- set sizes for rpool.
**
**	Parameters:
**		poolsize -- size of a single rpool block.
**		bigobjectsize -- if this size is exceeded, an individual
**			block is allocated (must be less or equal poolsize).
**
**	Returns:
**		none.
*/

void
sm_rpool_setsizes(rpool, poolsize, bigobjectsize)
	SM_RPOOL_T *rpool;
	size_t poolsize;
	size_t bigobjectsize;
{
	SM_REQUIRE(poolsize >= bigobjectsize);
	if (poolsize == 0)
		poolsize = POOLSIZE - sizeof(SM_POOLHDR_T);
	if (bigobjectsize == 0)
		bigobjectsize = poolsize / BIG_OBJECT_RATIO;
	rpool->sm_poolsize = poolsize;
	rpool->sm_bigobjectsize = bigobjectsize;
}

/*
**  SM_RPOOL_FREE -- free an rpool and release all of its resources.
**
**	Parameters:
**		rpool -- rpool to free.
**
**	Returns:
**		none.
*/

void
sm_rpool_free(rpool)
	SM_RPOOL_T *rpool;
{
	SM_RLIST_T *rl, *rnext;
	SM_RESOURCE_T *r, *rmax;
	SM_POOLLINK_T *pp, *pnext;

	if (rpool == NULL)
		return;

	/*
	**  It's important to free the resources before the memory pools,
	**  because the resource free functions might modify the contents
	**  of the memory pools.
	*/

	rl = rpool->sm_rlists;
	if (rl != NULL)
	{
		rmax = rpool->sm_rptr;
		for (;;)
		{
			for (r = rl->sm_rvec; r < rmax; ++r)
			{
				if (r->sm_rfree != NULL)
					r->sm_rfree(r->sm_rcontext);
			}
			rnext = rl->sm_rnext;
			sm_free(rl);
			if (rnext == NULL)
				break;
			rl = rnext;
			rmax = &rl->sm_rvec[SM_RLIST_MAX];
		}
	}

	/*
	**  Now free the memory pools.
	*/

	for (pp = rpool->sm_pools; pp != NULL; pp = pnext)
	{
		pnext = pp->sm_pnext;
		sm_free(pp);
	}

	/*
	**  Disconnect rpool from its parent.
	*/

	if (rpool->sm_parentlink != NULL)
		*rpool->sm_parentlink = NULL;

	/*
	**  Setting these fields to zero means that any future to attempt
	**  to use the rpool after it is freed will cause an assertion failure.
	*/

	rpool->sm_magic = NULL;
	rpool->sm_poolavail = 0;
	rpool->sm_ravail = 0;

#if _FFR_PERF_RPOOL
	if (rpool->sm_nbigblocks > 0 || rpool->sm_npools > 1)
		syslog(LOG_NOTICE,
			"perf: rpool=%lx, sm_nbigblocks=%d, sm_npools=%d",
			(long) rpool, rpool->sm_nbigblocks, rpool->sm_npools);
	rpool->sm_nbigblocks = 0;
	rpool->sm_npools = 0;
#endif /* _FFR_PERF_RPOOL */
	sm_free(rpool);
}

/*
**  SM_RPOOL_ATTACH_X -- attach a resource to an rpool.
**
**	Parameters:
**		rpool -- rpool to which resource should be attached.
**		rfree -- function to call when rpool is freed.
**		rcontext -- argument for function to call when rpool is freed.
**
**	Returns:
**		Pointer to allocated function.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

SM_RPOOL_ATTACH_T
sm_rpool_attach_x(rpool, rfree, rcontext)
	SM_RPOOL_T *rpool;
	SM_RPOOL_RFREE_T rfree;
	void *rcontext;
{
	SM_RLIST_T *rl;
	SM_RPOOL_ATTACH_T a;

	SM_REQUIRE_ISA(rpool, SmRpoolMagic);

	if (rpool->sm_ravail == 0)
	{
		rl = sm_malloc_x(sizeof(SM_RLIST_T));
		rl->sm_rnext = rpool->sm_rlists;
		rpool->sm_rlists = rl;
		rpool->sm_rptr = rl->sm_rvec;
		rpool->sm_ravail = SM_RLIST_MAX;
	}

	a = &rpool->sm_rptr->sm_rfree;
	rpool->sm_rptr->sm_rfree = rfree;
	rpool->sm_rptr->sm_rcontext = rcontext;
	++rpool->sm_rptr;
	--rpool->sm_ravail;
	return a;
}

#if DO_NOT_USE_STRCPY
/*
**  SM_RPOOL_STRDUP_X -- Create a copy of a C string
**
**	Parameters:
**		rpool -- rpool to use.
**		s -- the string to copy.
**
**	Returns:
**		pointer to newly allocated string.
*/

char *
sm_rpool_strdup_x(rpool, s)
	SM_RPOOL_T *rpool;
	const char *s;
{
	size_t l;
	char *n;

	l = strlen(s);
	SM_ASSERT(l + 1 > l);
	n = sm_rpool_malloc_x(rpool, l + 1);
	sm_strlcpy(n, s, l + 1);
	return n;
}
#endif /* DO_NOT_USE_STRCPY */
