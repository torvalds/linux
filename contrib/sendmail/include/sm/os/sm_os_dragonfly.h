/*
 * Copyright (c) 2000-2001, 2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_dragonfly.h,v 1.2 2013-11-22 20:51:34 ca Exp $
 */

/*
**  Platform definitions for DragonFly BSD
*/

#define SM_OS_NAME	"dragonfly"

#define SM_CONF_SYS_CDEFS_H	1

#define MI_SOMAXCONN	-1	/* listen() max backlog for milter */

#ifndef SM_CONF_STRL
# define SM_CONF_STRL	1
#endif /* SM_CONF_STRL */
#ifndef SM_CONF_SHM
# define SM_CONF_SHM	1
#endif /* SM_CONF_SHM */
#ifndef SM_CONF_SEM
# define SM_CONF_SEM	1
#endif /* SM_CONF_SEM */
#ifndef SM_CONF_MSG
# define SM_CONF_MSG	1
#endif /* SM_CONF_MSG */
