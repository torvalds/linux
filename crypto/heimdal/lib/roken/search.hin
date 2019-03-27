/*-
 * Written by J.T. Conklin <jtc@netbsd.org>
 * Public domain.
 *
 * $NetBSD: search.h,v 1.12 1999/02/22 10:34:28 christos Exp $
 */

#ifndef _rk_SEARCH_H_
#define _rk_SEARCH_H_ 1

#ifndef ROKEN_LIB_FUNCTION
#ifdef _WIN32
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL     __cdecl
#else
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL
#endif
#endif

#ifndef _WIN32
#include <sys/cdefs.h>
#endif
#include <sys/types.h>

typedef	enum {
	preorder,
	postorder,
	endorder,
	leaf
} VISIT;

ROKEN_CPP_START

ROKEN_LIB_FUNCTION void	* ROKEN_LIB_CALL rk_tdelete(const void *, void **,
		 int (*)(const void *, const void *));
ROKEN_LIB_FUNCTION void	* ROKEN_LIB_CALL rk_tfind(const void *, void * const *,
	       int (*)(const void *, const void *));
ROKEN_LIB_FUNCTION void	* ROKEN_LIB_CALL rk_tsearch(const void *, void **, int (*)(const void *, const void *));
ROKEN_LIB_FUNCTION void	ROKEN_LIB_CALL rk_twalk(const void *, void (*)(const void *, VISIT, int));

ROKEN_CPP_END

#endif /* !_rk_SEARCH_H_ */
