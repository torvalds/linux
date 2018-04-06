/* AFS fileserver XDR types
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef XDR_FS_H
#define XDR_FS_H

struct afs_xdr_AFSFetchStatus {
	__be32	if_version;
#define AFS_FSTATUS_VERSION	1
	__be32	type;
	__be32	nlink;
	__be32	size_lo;
	__be32	data_version_lo;
	__be32	author;
	__be32	owner;
	__be32	caller_access;
	__be32	anon_access;
	__be32	mode;
	__be32	parent_vnode;
	__be32	parent_unique;
	__be32	seg_size;
	__be32	mtime_client;
	__be32	mtime_server;
	__be32	group;
	__be32	sync_counter;
	__be32	data_version_hi;
	__be32	lock_count;
	__be32	size_hi;
	__be32	abort_code;
} __packed;

#endif /* XDR_FS_H */
