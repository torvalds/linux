/*
 * Copyright (c) 2000-2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: config.h,v 1.49 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm configuration macros.
**  The values of these macros are platform dependent.
**  The default values are given here.
**  If the default is incorrect, then the correct value can be specified
**  in the m4 configuration file in devtools/OS.
*/

#ifndef SM_CONFIG_H
# define SM_CONFIG_H

#  include "sm_os.h"

/*
**  SM_CONF_STDBOOL_H is 1 if <stdbool.h> exists
**
**  Note, unlike gcc, clang doesn't apply full prototypes to K&R definitions.
*/

# ifndef SM_CONF_STDBOOL_H
#  if !defined(__clang__) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#   define SM_CONF_STDBOOL_H		1
#  else /* !defined(__clang__) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L */
#   define SM_CONF_STDBOOL_H		0
#  endif /* !defined(__clang__) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L */
# endif /* ! SM_CONF_STDBOOL_H */

/*
**  Configuration macros that specify how __P is defined.
*/

# ifndef SM_CONF_SYS_CDEFS_H
#  define SM_CONF_SYS_CDEFS_H		0
# endif /* ! SM_CONF_SYS_CDEFS_H */

/*
**  SM_CONF_STDDEF_H is 1 if <stddef.h> exists
*/

# ifndef SM_CONF_STDDEF_H
#  define SM_CONF_STDDEF_H		1
# endif /* ! SM_CONF_STDDEF_H */

/*
**  Configuration macro that specifies whether strlcpy/strlcat are available.
**  Note: this is the default so that the libsm version (optimized) will
**  be used by default (sm_strlcpy/sm_strlcat).
*/

# ifndef SM_CONF_STRL
#  define SM_CONF_STRL			0
# endif /* ! SM_CONF_STRL */

/*
**  Configuration macro indicating that setitimer is available
*/

# ifndef SM_CONF_SETITIMER
#  define SM_CONF_SETITIMER		1
# endif /* ! SM_CONF_SETITIMER */

/*
**  Does <sys/types.h> define uid_t and gid_t?
*/

# ifndef SM_CONF_UID_GID
#  define SM_CONF_UID_GID		1
# endif /* ! SM_CONF_UID_GID */

/*
**  Does <sys/types.h> define ssize_t?
*/
# ifndef SM_CONF_SSIZE_T
#  define SM_CONF_SSIZE_T		1
# endif /* ! SM_CONF_SSIZE_T */

/*
**  Does the C compiler support long long?
*/

# ifndef SM_CONF_LONGLONG
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#   define SM_CONF_LONGLONG		1
#  else /* defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L */
#   if defined(__GNUC__)
#    define SM_CONF_LONGLONG		1
#   else /* defined(__GNUC__) */
#    define SM_CONF_LONGLONG		0
#   endif /* defined(__GNUC__) */
#  endif /* defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L */
# endif /* ! SM_CONF_LONGLONG */

/*
**  Does <sys/types.h> define quad_t and u_quad_t?
**  We only care if long long is not available.
*/

# ifndef SM_CONF_QUAD_T
#  define SM_CONF_QUAD_T		0
# endif /* ! SM_CONF_QUAD_T */

/*
**  Configuration macro indicating that shared memory is available
*/

# ifndef SM_CONF_SHM
#  define SM_CONF_SHM		0
# endif /* ! SM_CONF_SHM */

/*
**  Does <setjmp.h> define sigsetjmp?
*/

# ifndef SM_CONF_SIGSETJMP
#  define SM_CONF_SIGSETJMP	1
# endif /* ! SM_CONF_SIGSETJMP */

/*
**  Does <sysexits.h> exist, and define the EX_* macros with values
**  that differ from the default BSD values in <sm/sysexits.h>?
*/

# ifndef SM_CONF_SYSEXITS_H
#  define SM_CONF_SYSEXITS_H	0
# endif /* ! SM_CONF_SYSEXITS_H */

/* has memchr() prototype? (if not: needs memory.h) */
# ifndef SM_CONF_MEMCHR
#  define SM_CONF_MEMCHR	1
# endif /* ! SM_CONF_MEMCHR */

/* try LLONG tests in libsm/t-types.c? */
# ifndef SM_CONF_TEST_LLONG
#  define SM_CONF_TEST_LLONG	1
# endif /* !SM_CONF_TEST_LLONG */

/* LDAP Checks */
# if LDAPMAP
#  include <lber.h>
#  include <ldap.h>

/* Does the LDAP library have ldap_memfree()? */
#  ifndef SM_CONF_LDAP_MEMFREE

/*
**  The new LDAP C API (draft-ietf-ldapext-ldap-c-api-04.txt) includes
**  ldap_memfree() in the API.  That draft states to use LDAP_API_VERSION
**  of 2004 to identify the API.
*/

#   if USING_NETSCAPE_LDAP || LDAP_API_VERSION >= 2004
#    define SM_CONF_LDAP_MEMFREE	1
#   else /* USING_NETSCAPE_LDAP || LDAP_API_VERSION >= 2004 */
#    define SM_CONF_LDAP_MEMFREE	0
#   endif /* USING_NETSCAPE_LDAP || LDAP_API_VERSION >= 2004 */
#  endif /* ! SM_CONF_LDAP_MEMFREE */

/* Does the LDAP library have ldap_initialize()? */
#  ifndef SM_CONF_LDAP_INITIALIZE

/*
**  Check for ldap_initialize() support for support for LDAP URI's with
**  non-ldap:// schemes.
*/

/* OpenLDAP does it with LDAP_OPT_URI */
#   ifdef LDAP_OPT_URI
#    define SM_CONF_LDAP_INITIALIZE	1
#   endif /* LDAP_OPT_URI */
#  endif /* !SM_CONF_LDAP_INITIALIZE */
# endif /* LDAPMAP */

/* don't use strcpy() */
# ifndef DO_NOT_USE_STRCPY
#  define DO_NOT_USE_STRCPY	1
# endif /* ! DO_NOT_USE_STRCPY */

#endif /* ! SM_CONFIG_H */
