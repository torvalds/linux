/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_linux.h,v 1.13 2013-11-22 20:51:34 ca Exp $
 */

/*
**  Platform definitions for Linux
*/

#define SM_OS_NAME	"linux"

/* to get version number */
#include <linux/version.h>

# if !defined(KERNEL_VERSION)	/* not defined in 2.0.x kernel series */
#  define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
# endif /* ! KERNEL_VERSION */

/* doesn't seem to work on Linux */
#ifndef SM_CONF_SETITIMER
# define SM_CONF_SETITIMER	0
#endif /* SM_CONF_SETITIMER */

#ifndef SM_CONF_SHM
# if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,19))
#  define SM_CONF_SHM	1
# endif /* LINUX_VERSION_CODE */
#endif /* SM_CONF_SHM */

#define SM_CONF_SYS_CDEFS_H	1
#ifndef SM_CONF_SEM
# define SM_CONF_SEM	2
#endif /* SM_CONF_SEM */
#ifndef SM_CONF_MSG
# define SM_CONF_MSG	1
#endif /* SM_CONF_MSG */
