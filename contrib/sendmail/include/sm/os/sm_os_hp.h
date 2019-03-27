/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_hp.h,v 1.9 2013-11-22 20:51:34 ca Exp $
 */

/*
**  sm_os_hp.h -- platform definitions for HP
*/

#define SM_OS_NAME	"hp"

#ifndef SM_CONF_SHM
# define SM_CONF_SHM	1
#endif /* SM_CONF_SHM */
#ifndef SM_CONF_SEM
# define SM_CONF_SEM	2
#endif /* SM_CONF_SEM */
#ifndef SM_CONF_MSG
# define SM_CONF_MSG	1
#endif /* SM_CONF_MSG */

/* max/min buffer size of other than regular files */
#ifndef SM_IO_MAX_BUF
# define SM_IO_MAX_BUF	8192
#endif /* SM_IO_MAX_BUF */
#ifndef SM_IO_MIN_BUF
# define SM_IO_MIN_BUF	4096
#endif /* SM_IO_MIN_BUF */
