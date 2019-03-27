/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_freebsd.h,v 1.12 2013-11-22 20:51:34 ca Exp $
 */

/*
**  Platform definitions for FreeBSD
*/

#define SM_OS_NAME	"freebsd"

#define SM_CONF_SYS_CDEFS_H	1

#if __FreeBSD__ >= 2
#  include <osreldate.h> /* defines __FreeBSD_version */
#  if __FreeBSD_version >= 199512	/* 2.2-current when it appeared */
#     define MI_SOMAXCONN	-1	/* listen() max backlog for milter */
#  endif /* __FreeBSD_version >= 199512 */
#  if __FreeBSD_version >= 330000
     /* 3.3.0-release and later have strlcpy()/strlcat() */
#    ifndef SM_CONF_STRL
#       define SM_CONF_STRL		1
#    endif
#  endif
#endif

#ifndef SM_CONF_SHM
# define SM_CONF_SHM	1
#endif /* SM_CONF_SHM */
#ifndef SM_CONF_SEM
# define SM_CONF_SEM	2
#endif /* SM_CONF_SEM */
#ifndef SM_CONF_MSG
# define SM_CONF_MSG	1
#endif /* SM_CONF_MSG */
