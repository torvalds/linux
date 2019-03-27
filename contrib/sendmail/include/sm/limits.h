/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: limits.h,v 1.7 2013-11-22 20:51:31 ca Exp $
 */

/*
**  <sm/limits.h>
**  This header file is a portability wrapper for <limits.h>.
**  It includes <limits.h>, then it ensures that the following macros
**  from the C 1999 standard for <limits.h> are defined:
**	LLONG_MIN, LLONG_MAX
**	ULLONG_MAX
*/

#ifndef SM_LIMITS_H
# define SM_LIMITS_H

# include <limits.h>
# include <sm/types.h>
# include <sys/param.h>

/*
**  The following assumes two's complement binary arithmetic.
*/

# ifndef LLONG_MIN
#  define LLONG_MIN	((LONGLONG_T)(~(ULLONG_MAX >> 1)))
# endif /* ! LLONG_MIN */
# ifndef LLONG_MAX
#  define LLONG_MAX	((LONGLONG_T)(ULLONG_MAX >> 1))
# endif /* ! LLONG_MAX */
# ifndef ULLONG_MAX
#  define ULLONG_MAX	((ULONGLONG_T)(-1))
# endif /* ! ULLONG_MAX */

/*
**  PATH_MAX is defined by the POSIX standard.  All modern systems
**  provide it.  Older systems define MAXPATHLEN in <sys/param.h> instead.
*/

# ifndef PATH_MAX
#  ifdef MAXPATHLEN
#   define PATH_MAX	MAXPATHLEN
#  else /* MAXPATHLEN */
#   define PATH_MAX	2048
#  endif /* MAXPATHLEN */
# endif /* ! PATH_MAX */

#endif /* ! SM_LIMITS_H */
