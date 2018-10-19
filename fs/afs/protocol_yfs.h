/* YFS protocol bits
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define YFS_FS_SERVICE	2500
#define YFS_CM_SERVICE	2501

#define YFSCBMAX 1024

enum YFS_CM_Operations {
	YFSCBProbe		= 206,	/* probe client */
	YFSCBGetLock		= 207,	/* get contents of CM lock table */
	YFSCBXStatsVersion	= 209,	/* get version of extended statistics */
	YFSCBGetXStats		= 210,	/* get contents of extended statistics data */
	YFSCBInitCallBackState3	= 213,	/* initialise callback state, version 3 */
	YFSCBProbeUuid		= 214,	/* check the client hasn't rebooted */
	YFSCBGetServerPrefs	= 215,
	YFSCBGetCellServDV	= 216,
	YFSCBGetLocalCell	= 217,
	YFSCBGetCacheConfig	= 218,
	YFSCBGetCellByNum	= 65537,
	YFSCBTellMeAboutYourself = 65538, /* get client capabilities */
	YFSCBCallBack		= 64204,
};

struct yfs_xdr_u64 {
	__be32			msw;
	__be32			lsw;
} __packed;

static inline u64 xdr_to_u64(const struct yfs_xdr_u64 x)
{
	return ((u64)ntohl(x.msw) << 32) | ntohl(x.lsw);
}

static inline struct yfs_xdr_u64 u64_to_xdr(const u64 x)
{
	return (struct yfs_xdr_u64){ .msw = htonl(x >> 32), .lsw = htonl(x) };
}

struct yfs_xdr_vnode {
	struct yfs_xdr_u64	lo;
	__be32			hi;
	__be32			unique;
} __packed;

struct yfs_xdr_YFSFid {
	struct yfs_xdr_u64	volume;
	struct yfs_xdr_vnode	vnode;
} __packed;
