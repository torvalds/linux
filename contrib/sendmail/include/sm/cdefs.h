/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: cdefs.h,v 1.17 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm C language portability macros
**  See libsm/cdefs.html for documentation.
*/

#ifndef SM_CDEFS_H
# define SM_CDEFS_H

# include <sm/config.h>

/*
**  BSD and Linux have <sys/cdefs.h> which defines a set of C language
**  portability macros that are a defacto standard in the open source
**  community.
*/

# if SM_CONF_SYS_CDEFS_H
#  include <sys/cdefs.h>
# endif /* SM_CONF_SYS_CDEFS_H */

/*
**  Define the standard C language portability macros
**  for platforms that lack <sys/cdefs.h>.
*/

# if !SM_CONF_SYS_CDEFS_H
#  if defined(__cplusplus)
#   define	__BEGIN_DECLS	extern "C" {
#   define	__END_DECLS	};
#  else /* defined(__cplusplus) */
#   define	__BEGIN_DECLS
#   define	__END_DECLS
#  endif /* defined(__cplusplus) */
#  if defined(__STDC__) || defined(__cplusplus)
#   ifndef __P
#    define	__P(protos)	protos
#   endif /* __P */
#   define	__CONCAT(x,y)	x ## y
#   define	__STRING(x)	#x
#  else /* defined(__STDC__) || defined(__cplusplus) */
#   define	__P(protos)	()
#   define	__CONCAT(x,y)	x/**/y
#   define	__STRING(x)	"x"
#   define	const
#   define	signed
#   define	volatile
#  endif /* defined(__STDC__) || defined(__cplusplus) */
# endif /* !SM_CONF_SYS_CDEFS_H */

/*
**  Define SM_DEAD, a macro used to declare functions that do not return
**  to their caller.
*/

# ifndef SM_DEAD
#  if __GNUC__ >= 2
#   if __GNUC__ == 2 && __GNUC_MINOR__ < 5
#    define SM_DEAD(proto) volatile proto
#    define SM_DEAD_D volatile
#   else /* __GNUC__ == 2 && __GNUC_MINOR__ < 5 */
#    define SM_DEAD(proto) proto __attribute__((__noreturn__))
#    define SM_DEAD_D
#   endif /* __GNUC__ == 2 && __GNUC_MINOR__ < 5 */
#  else /* __GNUC__ >= 2 */
#   define SM_DEAD(proto) proto
#   define SM_DEAD_D
#  endif /* __GNUC__ >= 2 */
# endif /* SM_DEAD */

/*
**  Define SM_UNUSED, a macro used to declare variables that may be unused.
*/

# ifndef SM_UNUSED
#  if __GNUC__ >= 2
#   if __GNUC__ == 2 && __GNUC_MINOR__ < 7
#    define SM_UNUSED(decl) decl
#   else /* __GNUC__ == 2 && __GNUC_MINOR__ < 7 */
#    define SM_UNUSED(decl) decl __attribute__((__unused__))
#   endif /* __GNUC__ == 2 && __GNUC_MINOR__ < 7 */
#  else /* __GNUC__ >= 2 */
#   define SM_UNUSED(decl) decl
#  endif /* __GNUC__ >= 2 */
# endif /* SM_UNUSED */

/*
**  The SM_NONVOLATILE macro is used to declare variables that are not
**  volatile, but which must be declared volatile when compiling with
**  gcc -O -Wall in order to suppress bogus warning messages.
**
**  Variables that actually are volatile should be declared volatile
**  using the "volatile" keyword.  If a variable actually is volatile,
**  then SM_NONVOLATILE should not be used.
**
**  To compile sendmail with gcc and see all non-bogus warnings,
**  you should use
**	gcc -O -Wall -DSM_OMIT_BOGUS_WARNINGS ...
**  Do not use -DSM_OMIT_BOGUS_WARNINGS when compiling the production
**  version of sendmail, because there is a performance hit.
*/

# ifdef SM_OMIT_BOGUS_WARNINGS
#  define SM_NONVOLATILE volatile
# else /* SM_OMIT_BOGUS_WARNINGS */
#  define SM_NONVOLATILE
# endif /* SM_OMIT_BOGUS_WARNINGS */

/*
**  Turn on format string argument checking.
*/

# ifndef SM_CONF_FORMAT_TEST
#  if (__GNUC__ == 2 && __GNUC_MINOR__ >= 7) || __GNUC__ > 2
#   define SM_CONF_FORMAT_TEST	1
#  else
#   define SM_CONF_FORMAT_TEST	0
#  endif
# endif /* SM_CONF_FORMAT_TEST */

# ifndef PRINTFLIKE
#  if SM_CONF_FORMAT_TEST
#   define PRINTFLIKE(x,y) __attribute__ ((__format__ (__printf__, x, y)))
#  else /* SM_CONF_FORMAT_TEST */
#   define PRINTFLIKE(x,y)
#  endif /* SM_CONF_FORMAT_TEST */
# endif /* ! PRINTFLIKE */

# ifndef SCANFLIKE
#  if SM_CONF_FORMAT_TEST
#   define SCANFLIKE(x,y) __attribute__ ((__format__ (__scanf__, x, y)))
#  else /* SM_CONF_FORMAT_TEST */
#   define SCANFLIKE(x,y)
#  endif /* SM_CONF_FORMAT_TEST */
# endif /* ! SCANFLIKE */

#endif /* ! SM_CDEFS_H */
