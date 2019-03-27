/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_sunos.h,v 1.15 2013-11-22 20:51:34 ca Exp $
 */

/*
**  platform definitions for SunOS 4.0.3, SunOS 4.1.x and Solaris 2.x
*/

#define SM_OS_NAME "sunos"

#ifdef SOLARIS
/*
**  Solaris 2.x (aka SunOS 5.x)
**  M4 config file is devtools/OS/SunOS.5.x, which defines the SOLARIS macro.
*/

# define SM_CONF_LONGLONG	1
# ifndef SM_CONF_SHM
#  define SM_CONF_SHM		1
# endif /* SM_CONF_SHM */
# ifndef SM_CONF_SEM
#  define SM_CONF_SEM		2
# endif /* SM_CONF_SEM */
# ifndef SM_CONF_MSG
#  define SM_CONF_MSG		1
# endif /* SM_CONF_MSG */

#else /* SOLARIS */

/*
**  SunOS 4.0.3 or 4.1.x
*/

# define SM_CONF_SSIZE_T	0
# ifndef SM_CONF_BROKEN_SIZE_T
#  define SM_CONF_BROKEN_SIZE_T	1	/* size_t is signed? */
# endif /* SM_CONF_BROKEN_SIZE_T */

# ifndef SM_CONF_BROKEN_STRTOD
#  define SM_CONF_BROKEN_STRTOD	1
# endif /* ! SM_CONF_BROKEN_STRTOD */

/* has memchr() prototype? (if not: needs memory.h) */
# ifndef SM_CONF_MEMCHR
#  define SM_CONF_MEMCHR	0
# endif /* ! SM_CONF_MEMCHR */

# ifdef SUNOS403

/*
**  SunOS 4.0.3
**  M4 config file is devtools/OS/SunOS4.0, which defines the SUNOS403 macro.
*/

# else /* SUNOS403 */

/*
**  SunOS 4.1.x
**  M4 config file is devtools/OS/SunOS, which defines no macros.
*/

# endif /* SUNOS403 */
#endif /* SOLARIS */
