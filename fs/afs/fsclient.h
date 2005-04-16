/* fsclient.h: AFS File Server client stub declarations
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_FSCLIENT_H
#define _LINUX_AFS_FSCLIENT_H

#include "server.h"

extern int afs_rxfs_get_volume_info(struct afs_server *server,
				    const char *name,
				    struct afs_volume_info *vinfo);

extern int afs_rxfs_fetch_file_status(struct afs_server *server,
				      struct afs_vnode *vnode,
				      struct afs_volsync *volsync);

struct afs_rxfs_fetch_descriptor {
	struct afs_fid	fid;		/* file ID to fetch */
	size_t		size;		/* total number of bytes to fetch */
	off_t		offset;		/* offset in file to start from */
	void		*buffer;	/* read buffer */
	size_t		actual;		/* actual size sent back by server */
};

extern int afs_rxfs_fetch_file_data(struct afs_server *server,
				    struct afs_vnode *vnode,
				    struct afs_rxfs_fetch_descriptor *desc,
				    struct afs_volsync *volsync);

extern int afs_rxfs_give_up_callback(struct afs_server *server,
				     struct afs_vnode *vnode);

/* this doesn't appear to work in OpenAFS server */
extern int afs_rxfs_lookup(struct afs_server *server,
			   struct afs_vnode *dir,
			   const char *filename,
			   struct afs_vnode *vnode,
			   struct afs_volsync *volsync);

/* this is apparently mis-implemented in OpenAFS server */
extern int afs_rxfs_get_root_volume(struct afs_server *server,
				    char *buf,
				    size_t *buflen);


#endif /* _LINUX_AFS_FSCLIENT_H */
