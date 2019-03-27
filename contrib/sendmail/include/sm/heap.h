/*
 * Copyright (c) 2000-2001, 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: heap.h,v 1.24 2013-11-22 20:51:31 ca Exp $
 */

/*
**  Sendmail debugging memory allocation package.
**  See libsm/heap.html for documentation.
*/

#ifndef SM_HEAP_H
# define SM_HEAP_H

# include <sm/io.h>
# include <stdlib.h>
# include <sm/debug.h>
# include <sm/exc.h>

/* change default to 0 for production? */
# ifndef SM_HEAP_CHECK
#  define SM_HEAP_CHECK		1
# endif /* ! SM_HEAP_CHECK */

# if SM_HEAP_CHECK
#  define sm_malloc_x(sz) sm_malloc_tagged_x(sz, __FILE__, __LINE__, SmHeapGroup)
#  define sm_malloc(size) sm_malloc_tagged(size, __FILE__, __LINE__, SmHeapGroup)
#  define sm_free(ptr) sm_free_tagged(ptr, __FILE__, __LINE__)

extern void *sm_malloc_tagged __P((size_t, char *, int, int));
extern void *sm_malloc_tagged_x __P((size_t, char *, int, int));
extern void sm_free_tagged __P((void *, char *, int));
extern void *sm_realloc_x __P((void *, size_t));
extern bool sm_heap_register __P((void *, size_t, char *, int, int));
extern void sm_heap_checkptr_tagged  __P((void *, char *, int));
extern void sm_heap_report __P((SM_FILE_T *, int));

# else /* SM_HEAP_CHECK */
#  define sm_malloc_tagged(size, file, line, grp)	sm_malloc(size)
#  define sm_malloc_tagged_x(size, file, line, grp)	sm_malloc_x(size)
#  define sm_free_tagged(ptr, file, line)		sm_free(ptr)
#  define sm_heap_register(ptr, size, file, line, grp)	(true)
#  define sm_heap_checkptr_tagged(ptr, tag, num)	((void)0)
#  define sm_heap_report(file, verbose)			((void)0)

extern void *sm_malloc __P((size_t));
extern void *sm_malloc_x __P((size_t));
extern void *sm_realloc_x __P((void *, size_t));
extern void sm_free __P((void *));
# endif /* SM_HEAP_CHECK */

extern void *sm_realloc __P((void *, size_t));

# define sm_heap_checkptr(ptr) sm_heap_checkptr_tagged(ptr, __FILE__, __LINE__)

#if 0
/*
**  sm_f[mc]alloc are plug in replacements for malloc and calloc
**  which can be used in a context requiring a function pointer,
**  and which are compatible with sm_free.  Warning: sm_heap_report
**  cannot report where storage leaked by sm_f[mc]alloc was allocated.
*/

/* XXX unused right now */

extern void *
sm_fmalloc __P((
	size_t));

extern void *
sm_fcalloc __P((
	size_t,
	size_t));
#endif /* 0 */

/*
**  Allocate 'permanent' storage that can be freed but may still be
**  allocated when the process exits.  sm_heap_report will not complain
**  about a storage leak originating from a call to sm_pmalloc.
*/

# define sm_pmalloc(size)   sm_malloc_tagged(size, __FILE__, __LINE__, 0)
# define sm_pmalloc_x(size) sm_malloc_tagged_x(size, __FILE__, __LINE__, 0)

# define sm_heap_group()	SmHeapGroup
# define sm_heap_setgroup(g)	(SmHeapGroup = (g))
# define sm_heap_newgroup()	(SmHeapGroup = ++SmHeapMaxGroup)

#define SM_FREE(ptr)			\
	do				\
	{				\
		if ((ptr) != NULL)	\
		{			\
			sm_free(ptr);	\
			(ptr) = NULL;	\
		}			\
	} while (0)

extern int SmHeapGroup;
extern int SmHeapMaxGroup;

extern SM_DEBUG_T SmHeapTrace;
extern SM_DEBUG_T SmHeapCheck;
extern SM_EXC_T SmHeapOutOfMemory;

#endif /* ! SM_HEAP_H */
