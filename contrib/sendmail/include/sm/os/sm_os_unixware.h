/*
 * Copyright (c) 2001, 2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_unixware.h,v 1.9 2013-11-22 20:51:34 ca Exp $
 */

#define SM_OS_NAME	"unixware"

#ifndef SM_CONF_LONGLONG
# if defined(__SCO_VERSION__) && __SCO_VERSION__ > 400000000L
#  define SM_CONF_LONGLONG	1
#  define SM_CONF_TEST_LLONG	1
#  define SM_CONF_BROKEN_SIZE_T	0
# endif /* defined(__SCO_VERSION__) && __SCO_VERSION__ > 400000000L */
#endif /* !SM_CONF_LONGLONG */

/* try LLONG tests in libsm/t-types.c? */
#ifndef SM_CONF_TEST_LLONG
# define SM_CONF_TEST_LLONG	0
#endif /* !SM_CONF_TEST_LLONG */

/* needs alarm(), our sleep() otherwise hangs. */
#define SM_CONF_SETITIMER	0

#ifndef SM_CONF_SHM
# define SM_CONF_SHM	1
#endif /* SM_CONF_SHM */

/* size_t seems to be signed */
#ifndef SM_CONF_BROKEN_SIZE_T
# define SM_CONF_BROKEN_SIZE_T	1
#endif /* SM_CONF_BROKEN_SIZE_T */

/* don't use flock() in mail.local.c */
#ifndef LDA_USE_LOCKF
# define LDA_USE_LOCKF	1
#endif /* LDA_USE_LOCKF */
