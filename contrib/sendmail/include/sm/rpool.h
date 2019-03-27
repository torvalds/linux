/*
 * Copyright (c) 2000-2001, 2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: rpool.h,v 1.17 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm resource pools
**  See libsm/rpool.html for documentation.
*/

#ifndef SM_RPOOL_H
# define SM_RPOOL_H

# include <sm/gen.h>
# include <sm/heap.h>
# include <sm/string.h>

/*
**  Each memory pool object consists of an SM_POOLLINK_T,
**  followed by a platform specific amount of padding,
**  followed by 'poolsize' bytes of pool data,
**  where 'poolsize' is the value of rpool->sm_poolsize at the time
**  the pool is allocated.
*/

typedef struct sm_poollink SM_POOLLINK_T;
struct sm_poollink
{
	SM_POOLLINK_T *sm_pnext;
};

typedef void (*SM_RPOOL_RFREE_T) __P((void *_rcontext));

typedef SM_RPOOL_RFREE_T *SM_RPOOL_ATTACH_T;

typedef struct sm_resource SM_RESOURCE_T;
struct sm_resource
{
	/*
	**  Function for freeing this resource.  It may be NULL,
	**  meaning that this resource has already been freed.
	*/

	SM_RPOOL_RFREE_T sm_rfree;
	void *sm_rcontext;	/* resource data */
};

# define SM_RLIST_MAX 511

typedef struct sm_rlist SM_RLIST_T;
struct sm_rlist
{
	SM_RESOURCE_T sm_rvec[SM_RLIST_MAX];
	SM_RLIST_T *sm_rnext;
};

typedef struct
{
	/* Points to SmRpoolMagic, or is NULL if rpool is freed. */
	const char *sm_magic;

	/*
	**  If this rpool object has no parent, then sm_parentlink
	**  is NULL.  Otherwise, we set *sm_parentlink = NULL
	**  when this rpool is freed, so that it isn't freed a
	**  second time when the parent is freed.
	*/

	SM_RPOOL_RFREE_T *sm_parentlink;

	/*
	**  Memory pools
	*/

	/* Size of the next pool to be allocated, not including the header. */
	size_t sm_poolsize;

	/*
	**  If an sm_rpool_malloc_x request is too big to fit
	**  in the current pool, and the request size > bigobjectsize,
	**  then the object will be given its own malloc'ed block.
	**  sm_bigobjectsize <= sm_poolsize.  The maximum wasted space
	**  at the end of a pool is maxpooledobjectsize - 1.
	*/

	size_t sm_bigobjectsize;

	/* Points to next free byte in the current pool. */
	char *sm_poolptr;

	/*
	**  Number of bytes available in the current pool.
	**	Initially 0. Set to 0 by sm_rpool_free.
	*/

	size_t sm_poolavail;

	/* Linked list of memory pools.  Initially NULL. */
	SM_POOLLINK_T *sm_pools;

	/*
	** Resource lists
	*/

	SM_RESOURCE_T *sm_rptr; /* Points to next free resource slot. */

	/*
	**  Number of available resource slots in current list.
	**	Initially 0. Set to 0 by sm_rpool_free.
	*/

	size_t sm_ravail;

	/* Linked list of resource lists. Initially NULL. */
	SM_RLIST_T *sm_rlists;

#if _FFR_PERF_RPOOL
	int	sm_nbigblocks;
	int	sm_npools;
#endif /* _FFR_PERF_RPOOL */

} SM_RPOOL_T;

extern SM_RPOOL_T *
sm_rpool_new_x __P((
	SM_RPOOL_T *_parent));

extern void
sm_rpool_free __P((
	SM_RPOOL_T *_rpool));

# if SM_HEAP_CHECK
extern void *
sm_rpool_malloc_tagged_x __P((
	SM_RPOOL_T *_rpool,
	size_t _size,
	char *_file,
	int _line,
	int _group));
#  define sm_rpool_malloc_x(rpool, size) \
	sm_rpool_malloc_tagged_x(rpool, size, __FILE__, __LINE__, SmHeapGroup)
extern void *
sm_rpool_malloc_tagged __P((
	SM_RPOOL_T *_rpool,
	size_t _size,
	char *_file,
	int _line,
	int _group));
#  define sm_rpool_malloc(rpool, size) \
	sm_rpool_malloc_tagged(rpool, size, __FILE__, __LINE__, SmHeapGroup)
# else /* SM_HEAP_CHECK */
extern void *
sm_rpool_malloc_x __P((
	SM_RPOOL_T *_rpool,
	size_t _size));
extern void *
sm_rpool_malloc __P((
	SM_RPOOL_T *_rpool,
	size_t _size));
# endif /* SM_HEAP_CHECK */

#if DO_NOT_USE_STRCPY
extern char *sm_rpool_strdup_x __P((SM_RPOOL_T *rpool, const char *s));
#else /* DO_NOT_USE_STRCPY */
# define sm_rpool_strdup_x(rpool, str) \
	strcpy(sm_rpool_malloc_x(rpool, strlen(str) + 1), str)
#endif /* DO_NOT_USE_STRCPY */

extern SM_RPOOL_ATTACH_T
sm_rpool_attach_x __P((
	SM_RPOOL_T *_rpool,
	SM_RPOOL_RFREE_T _rfree,
	void *_rcontext));

# define sm_rpool_detach(a) ((void)(*(a) = NULL))

extern void
sm_rpool_setsizes __P((
	SM_RPOOL_T *_rpool,
	size_t _poolsize,
	size_t _bigobjectsize));

#endif /* ! SM_RPOOL_H */
