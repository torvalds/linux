/* AFS File Server client stubs
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/circ_buf.h>
#include "internal.h"
#include "afs_fs.h"

/*
 * decode an AFSFetchStatus block
 */
static void xdr_decode_AFSFetchStatus(const __be32 **_bp,
				      struct afs_vnode *vnode)
{
	const __be32 *bp = *_bp;
	umode_t mode;
	u64 data_version;
	u32 changed = 0; /* becomes non-zero if ctime-type changes seen */

#define EXTRACT(DST)				\
	do {					\
		u32 x = ntohl(*bp++);		\
		changed |= DST - x;		\
		DST = x;			\
	} while (0)

	vnode->status.if_version = ntohl(*bp++);
	EXTRACT(vnode->status.type);
	vnode->status.nlink = ntohl(*bp++);
	EXTRACT(vnode->status.size);
	data_version = ntohl(*bp++);
	EXTRACT(vnode->status.author);
	EXTRACT(vnode->status.owner);
	EXTRACT(vnode->status.caller_access); /* call ticket dependent */
	EXTRACT(vnode->status.anon_access);
	EXTRACT(vnode->status.mode);
	vnode->status.parent.vid = vnode->fid.vid;
	EXTRACT(vnode->status.parent.vnode);
	EXTRACT(vnode->status.parent.unique);
	bp++; /* seg size */
	vnode->status.mtime_client = ntohl(*bp++);
	vnode->status.mtime_server = ntohl(*bp++);
	bp++; /* group */
	bp++; /* sync counter */
	data_version |= (u64) ntohl(*bp++) << 32;
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */
	*_bp = bp;

	if (changed) {
		_debug("vnode changed");
		set_bit(AFS_VNODE_CHANGED, &vnode->flags);
		vnode->vfs_inode.i_uid		= vnode->status.owner;
		vnode->vfs_inode.i_size		= vnode->status.size;
		vnode->vfs_inode.i_version	= vnode->fid.unique;

		vnode->status.mode &= S_IALLUGO;
		mode = vnode->vfs_inode.i_mode;
		mode &= ~S_IALLUGO;
		mode |= vnode->status.mode;
		vnode->vfs_inode.i_mode = mode;
	}

	_debug("vnode time %lx, %lx",
	       vnode->status.mtime_client, vnode->status.mtime_server);
	vnode->vfs_inode.i_ctime.tv_sec	= vnode->status.mtime_server;
	vnode->vfs_inode.i_mtime	= vnode->vfs_inode.i_ctime;
	vnode->vfs_inode.i_atime	= vnode->vfs_inode.i_ctime;

	if (vnode->status.data_version != data_version) {
		_debug("vnode modified %llx", data_version);
		vnode->status.data_version = data_version;
		set_bit(AFS_VNODE_MODIFIED, &vnode->flags);
		set_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
	}
}

/*
 * decode an AFSCallBack block
 */
static void xdr_decode_AFSCallBack(const __be32 **_bp, struct afs_vnode *vnode)
{
	const __be32 *bp = *_bp;

	vnode->cb_version	= ntohl(*bp++);
	vnode->cb_expiry	= ntohl(*bp++);
	vnode->cb_type		= ntohl(*bp++);
	vnode->cb_expires	= vnode->cb_expiry + get_seconds();
	*_bp = bp;
}

/*
 * decode an AFSVolSync block
 */
static void xdr_decode_AFSVolSync(const __be32 **_bp,
				  struct afs_volsync *volsync)
{
	const __be32 *bp = *_bp;

	volsync->creation = ntohl(*bp++);
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */
	bp++; /* spare5 */
	bp++; /* spare6 */
	*_bp = bp;
}

/*
 * deliver reply data to an FS.FetchStatus
 */
static int afs_deliver_fs_fetch_status(struct afs_call *call,
				       struct sk_buff *skb, bool last)
{
	const __be32 *bp;

	_enter(",,%u", last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, call->reply);
	xdr_decode_AFSCallBack(&bp, call->reply);
	if (call->reply2)
		xdr_decode_AFSVolSync(&bp, call->reply2);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchStatus operation type
 */
static const struct afs_call_type afs_RXFSFetchStatus = {
	.deliver	= afs_deliver_fs_fetch_status,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch the status information for a file
 */
int afs_fs_fetch_file_status(struct afs_server *server,
			     struct afs_vnode *vnode,
			     struct afs_volsync *volsync,
			     const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(&afs_RXFSFetchStatus, 16, 120);
	if (!call)
		return -ENOMEM;

	call->reply = vnode;
	call->reply2 = volsync;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.FetchData
 */
static int afs_deliver_fs_fetch_data(struct afs_call *call,
				     struct sk_buff *skb, bool last)
{
	const __be32 *bp;
	struct page *page;
	void *buffer;
	int ret;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->unmarshall++;

		/* extract the returned data length */
	case 1:
		_debug("extract data length");
		ret = afs_extract_data(call, skb, last, &call->tmp, 4);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		call->count = ntohl(call->tmp);
		_debug("DATA length: %u", call->count);
		if (call->count > PAGE_SIZE)
			return -EBADMSG;
		call->offset = 0;
		call->unmarshall++;

		if (call->count < PAGE_SIZE) {
			buffer = kmap_atomic(call->reply3, KM_USER0);
			memset(buffer + PAGE_SIZE - call->count, 0,
			       call->count);
			kunmap_atomic(buffer, KM_USER0);
		}

		/* extract the returned data */
	case 2:
		_debug("extract data");
		page = call->reply3;
		buffer = kmap_atomic(page, KM_USER0);
		ret = afs_extract_data(call, skb, last, buffer, call->count);
		kunmap_atomic(buffer, KM_USER0);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		call->offset = 0;
		call->unmarshall++;

		/* extract the metadata */
	case 3:
		ret = afs_extract_data(call, skb, last, call->buffer, 120);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		bp = call->buffer;
		xdr_decode_AFSFetchStatus(&bp, call->reply);
		xdr_decode_AFSCallBack(&bp, call->reply);
		if (call->reply2)
			xdr_decode_AFSVolSync(&bp, call->reply2);

		call->offset = 0;
		call->unmarshall++;

	case 4:
		_debug("trailer");
		if (skb->len != 0)
			return -EBADMSG;
		break;
	}

	if (!last)
		return 0;

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchData operation type
 */
static const struct afs_call_type afs_RXFSFetchData = {
	.deliver	= afs_deliver_fs_fetch_data,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch data from a file
 */
int afs_fs_fetch_data(struct afs_server *server,
		      struct afs_vnode *vnode,
		      off_t offset, size_t length,
		      struct page *buffer,
		      struct afs_volsync *volsync,
		      const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(&afs_RXFSFetchData, 24, 120);
	if (!call)
		return -ENOMEM;

	call->reply = vnode;
	call->reply2 = volsync;
	call->reply3 = buffer;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHDATA);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);
	bp[4] = htonl(offset);
	bp[5] = htonl(length);

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.GiveUpCallBacks
 */
static int afs_deliver_fs_give_up_callbacks(struct afs_call *call,
					    struct sk_buff *skb, bool last)
{
	_enter(",{%u},%d", skb->len, last);

	if (skb->len > 0)
		return -EBADMSG; /* shouldn't be any reply data */
	return 0;
}

/*
 * FS.GiveUpCallBacks operation type
 */
static const struct afs_call_type afs_RXFSGiveUpCallBacks = {
	.deliver	= afs_deliver_fs_give_up_callbacks,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * give up a set of callbacks
 * - the callbacks are held in the server->cb_break ring
 */
int afs_fs_give_up_callbacks(struct afs_server *server,
			     const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	size_t ncallbacks;
	__be32 *bp, *tp;
	int loop;

	ncallbacks = CIRC_CNT(server->cb_break_head, server->cb_break_tail,
			      ARRAY_SIZE(server->cb_break));

	_enter("{%zu},", ncallbacks);

	if (ncallbacks == 0)
		return 0;
	if (ncallbacks > AFSCBMAX)
		ncallbacks = AFSCBMAX;

	_debug("break %zu callbacks", ncallbacks);

	call = afs_alloc_flat_call(&afs_RXFSGiveUpCallBacks,
				   12 + ncallbacks * 6 * 4, 0);
	if (!call)
		return -ENOMEM;

	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	tp = bp + 2 + ncallbacks * 3;
	*bp++ = htonl(FSGIVEUPCALLBACKS);
	*bp++ = htonl(ncallbacks);
	*tp++ = htonl(ncallbacks);

	atomic_sub(ncallbacks, &server->cb_break_n);
	for (loop = ncallbacks; loop > 0; loop--) {
		struct afs_callback *cb =
			&server->cb_break[server->cb_break_tail];

		*bp++ = htonl(cb->fid.vid);
		*bp++ = htonl(cb->fid.vnode);
		*bp++ = htonl(cb->fid.unique);
		*tp++ = htonl(cb->version);
		*tp++ = htonl(cb->expiry);
		*tp++ = htonl(cb->type);
		smp_mb();
		server->cb_break_tail =
			(server->cb_break_tail + 1) &
			(ARRAY_SIZE(server->cb_break) - 1);
	}

	ASSERT(ncallbacks > 0);
	wake_up_nr(&server->cb_break_waitq, ncallbacks);

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}
