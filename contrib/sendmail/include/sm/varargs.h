/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: varargs.h,v 1.9 2013-11-22 20:51:32 ca Exp $
 */

/*
**  libsm variable argument lists
*/

#ifndef SM_VARARGS_H
# define SM_VARARGS_H

# if defined(__STDC__) || defined(__cplusplus)
#  define SM_VA_STD 1
#  include <stdarg.h>
#  define SM_VA_START(ap, f)	va_start(ap, f)
# else /* defined(__STDC__) || defined(__cplusplus) */
#  define SM_VA_STD 0
#  include <varargs.h>
#  define SM_VA_START(ap, f)	va_start(ap)
# endif /* defined(__STDC__) || defined(__cplusplus) */

# if defined(va_copy)
#  define SM_VA_COPY(dst, src)	va_copy((dst), (src))
# elif defined(__va_copy)
#  define SM_VA_COPY(dst, src)	__va_copy((dst), (src))
# else
#  define SM_VA_COPY(dst, src)	memcpy(&(dst), &(src), sizeof((dst)))
# endif

/*
**  The following macros are useless, but are provided for symmetry.
*/

# define SM_VA_LOCAL_DECL	va_list ap;
# define SM_VA_ARG(ap, type)	va_arg(ap, type)
# define SM_VA_END(ap)		va_end(ap)

#endif /* ! SM_VARARGS_H */
