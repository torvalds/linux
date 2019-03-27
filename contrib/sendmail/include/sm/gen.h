/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: gen.h,v 1.24 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm general definitions
**  See libsm/gen.html for documentation.
*/

#ifndef SM_GEN_H
# define SM_GEN_H

# include <sm/config.h>
# include <sm/cdefs.h>
# include <sm/types.h>

/*
**  Define SM_RCSID and SM_IDSTR,
**  macros used to embed RCS and SCCS identification strings in object files.
*/

# ifdef lint
#  define SM_RCSID(str)
#  define SM_IDSTR(id,str)
# else /* lint */
#  define SM_RCSID(str) SM_UNUSED(static const char RcsId[]) = str;
#  define SM_IDSTR(id,str) SM_UNUSED(static const char id[]) = str;
# endif /* lint */

/*
**  Define NULL and offsetof (from the C89 standard)
*/

# if SM_CONF_STDDEF_H
#  include <stddef.h>
# else /* SM_CONF_STDDEF_H */
#  ifndef NULL
#   define NULL	0
#  endif /* ! NULL */
#  define offsetof(type, member)	((size_t)(&((type *)0)->member))
# endif /* SM_CONF_STDDEF_H */

/*
**  Define bool, true, false (from the C99 standard)
*/

# if SM_CONF_STDBOOL_H
#  include <stdbool.h>
# else /* SM_CONF_STDBOOL_H */
#  ifndef __cplusplus
    typedef int bool;
#   define false	0
#   define true		1
#   define __bool_true_false_are_defined	1
#  endif /* ! __cplusplus */
# endif /* SM_CONF_STDBOOL_H */

/*
**  Define SM_MAX and SM_MIN
*/

# define SM_MAX(a, b)	((a) > (b) ? (a) : (b))
# define SM_MIN(a, b)	((a) < (b) ? (a) : (b))

/* Define SM_SUCCESS and SM_FAILURE */
# define SM_SUCCESS	0
# define SM_FAILURE	(-1)

/* XXX This needs to be fixed when we start to use threads: */
typedef int SM_ATOMIC_INT_T;
typedef unsigned int SM_ATOMIC_UINT_T;

#endif /* SM_GEN_H */
