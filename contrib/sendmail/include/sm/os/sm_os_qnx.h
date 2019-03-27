/*
 * Copyright (c) 2007 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_qnx.h,v 1.2 2013-11-22 20:51:34 ca Exp $
 */

/*
**  sm_os_qnx.h -- platform definitions for QNX
*/

#define SM_CONF_SYS_CDEFS_H	1

#ifndef SM_CONF_SETITIMER
# define SM_CONF_SETITIMER	0
#endif /* SM_CONF_SETITIMER */
