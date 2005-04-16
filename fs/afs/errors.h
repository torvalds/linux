/* errors.h: AFS abort/error codes
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_ERRORS_H
#define _LINUX_AFS_ERRORS_H

#include "types.h"

/* file server abort codes */
typedef enum {
	VSALVAGE	= 101,	/* volume needs salvaging */
	VNOVNODE	= 102,	/* no such file/dir (vnode) */
	VNOVOL		= 103,	/* no such volume or volume unavailable */
	VVOLEXISTS	= 104,	/* volume name already exists */
	VNOSERVICE	= 105,	/* volume not currently in service */
	VOFFLINE	= 106,	/* volume is currently offline (more info available [VVL-spec]) */
	VONLINE		= 107,	/* volume is already online */
	VDISKFULL	= 108,	/* disk partition is full */
	VOVERQUOTA	= 109,	/* volume's maximum quota exceeded */
	VBUSY		= 110,	/* volume is temporarily unavailable */
	VMOVED		= 111,	/* volume moved to new server - ask this FS where */
} afs_rxfs_abort_t;

extern int afs_abort_to_error(int abortcode);

#endif /* _LINUX_AFS_ERRORS_H */
