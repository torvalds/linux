/* mount.h: mount parameters
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_MOUNT_H
#define _LINUX_AFS_MOUNT_H

struct afs_mountdata {
	const char		*volume;	/* name of volume */
	const char		*cell;		/* name of cell containing volume */
	const char		*cache;		/* name of cache block device */
	size_t			nservers;	/* number of server addresses listed */
	uint32_t		servers[10];	/* IP addresses of servers in this cell */
};

#endif /* _LINUX_AFS_MOUNT_H */
