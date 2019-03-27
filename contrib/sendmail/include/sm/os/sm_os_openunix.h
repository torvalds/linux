/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_openunix.h,v 1.6 2013-11-22 20:51:34 ca Exp $
 */

#define SM_OS_NAME	"openunix"

/* needs alarm(), our sleep() otherwise hangs. */
#define SM_CONF_SETITIMER	0

/* long long seems to work */
#define SM_CONF_LONGLONG	1

/* don't use flock() in mail.local.c */
#define LDA_USE_LOCKF	1

#ifndef SM_CONF_SHM
# define SM_CONF_SHM	1
#endif /* SM_CONF_SHM */
