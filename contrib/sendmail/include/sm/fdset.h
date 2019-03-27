/*
 * Copyright (c) 2001, 2002 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: fdset.h,v 1.6 2013-11-22 20:51:31 ca Exp $
 */

#ifndef	SM_FDSET_H
# define SM_FDSET_H

/*
**  Note: SM_FD_OK_SELECT(fd) requires that ValidSocket(fd) has been checked
**	before.
*/

#define SM_FD_SET(fd, pfdset)	FD_SET(fd, pfdset)
#define SM_FD_ISSET(fd, pfdset)	FD_ISSET(fd, pfdset)
#define SM_FD_SETSIZE		FD_SETSIZE
#define SM_FD_OK_SELECT(fd)	(SM_FD_SETSIZE <= 0 || (fd) < SM_FD_SETSIZE)

#endif /* SM_FDSET_H */
