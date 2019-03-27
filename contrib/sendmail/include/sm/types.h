/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: types.h,v 1.14 2013-11-22 20:51:32 ca Exp $
 */

/*
**  This header file defines standard integral types.
**  - It includes <sys/types.h>, and fixes portability problems that
**    exist on older Unix platforms.
**  - It defines LONGLONG_T and ULONGLONG_T, which are portable locutions
**    for 'long long' and 'unsigned long long'.
*/

#ifndef SM_TYPES_H
# define SM_TYPES_H

# include <sm/config.h>

/*
**  On BSD 4.2 systems, <sys/types.h> was not idempotent.
**  This problem is circumvented by replacing all occurrences
**  of <sys/types.h> with <sm/types.h>, which is idempotent.
*/

# include <sys/types.h>

/*
**  On some old Unix platforms, some of the standard types are missing.
**  We fix that here.
*/

# if !SM_CONF_UID_GID
#  define uid_t		int
#  define gid_t		int
# endif /* !SM_CONF_UID_GID */

# if !SM_CONF_SSIZE_T
#  define ssize_t	int
# endif /* !SM_CONF_SSIZE_T */

/*
**  Define LONGLONG_T and ULONGLONG_T, which are portable locutions
**  for 'long long' and 'unsigned long long' from the C 1999 standard.
*/

# if SM_CONF_LONGLONG
   typedef long long		LONGLONG_T;
   typedef unsigned long long	ULONGLONG_T;
# else /* SM_CONF_LONGLONG */
#  if SM_CONF_QUAD_T
     typedef quad_t		LONGLONG_T;
     typedef u_quad_t		ULONGLONG_T;
#  else /* SM_CONF_QUAD_T */
     typedef long		LONGLONG_T;
     typedef unsigned long	ULONGLONG_T;
#  endif /* SM_CONF_QUAD_T */
# endif /* SM_CONF_LONGLONG */

#endif /* ! SM_TYPES_H */
