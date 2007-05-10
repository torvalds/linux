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
 * decode an AFSFid block
 */
static void xdr_decode_AFSFid(const __be32 **_bp, struct afs_fid *fid)
{
	const __be32 *bp = *_bp;

	fid->vid		= ntohl(*bp++);
	fid->vnode		= ntohl(*bp++);
	fid->unique		= ntohl(*bp++);
	*_bp = bp;
}

/*
 * decode an AFSFetchStatus block
 */
static void xdr_decode_AFSFetchStatus(const __be32 **_bp,
				      struct afs_file_status *status,
				      struct afs_vnode *vnode,
				      afs_dataversion_t *store_version)
{
	afs_dataversion_t expected_version;
	const __be32 *bp = *_bp;
	umode_t mode;
	u64 data_version, size;
	u32 changed = 0; /* becomes non-zero if ctime-type changes seen */

#define EXTRACT(DST)				\
	do {					\
		u32 x = ntohl(*bp++);		\
		changed |= DST - x;		\
		DST = x;			\
	} while (0)

	status->if_version = ntohl(*bp++);
	EXTRACT(status->type);
	EXTRACT(status->nlink);
	size = ntohl(*bp++);
	data_version = ntohl(*bp++);
	EXTRACT(status->author);
	EXTRACT(status->owner);
	EXTRACT(status->caller_access); /* call ticket dependent */
	EXTRACT(status->anon_access);
	EXTRACT(status->mode);
	EXTRACT(status->parent.vnode);
	EXTRACT(status->parent.unique);
	bp++; /* seg size */
	status->mtime_client = ntohl(*bp++);
	status->mtime_server = ntohl(*bp++);
	EXTRACT(status->group);
	bp++; /* sync counter */
	data_version |= (u64) ntohl(*bp++) << 32;
	bp++; /* lock count */
	size |= (u64) ntohl(*bp++) << 32;
	bp++; /* spare 4 */
	*_bp = bp;

	if (size != status->size) {
		status->size = size;
		changed |= true;
	}
	status->mode &= S_IALLUGO;

	_debug("vnode time %lx, %lx",
	       status->mtime_client, status->mtime_server);

	if (vnode) {
		status->parent.vid = vnode->fid.vid;
		if (changed && !test_bit(AFS_VNODE_UNSET, &vnode->flags)) {
			_debug("vnode changed");
			i_size_write(&vnode->vfs_inode, size);
			vnode->vfs_inode.i_uid = status->owner;
			vnode->vfs_inode.i_gid = status->group;
			vnode->vfs_inode.i_version = vnode->fid.unique;
			vnode->vfs_inode.i_nlink = status->nlink;

			mode = vnode->vfs_inode.i_mode;
			mode &= ~S_IALLUGO;
			mode |= status->mode;
			barrier();
			vnode->vfs_inode.i_mode = mode;
		}

		vnode->vfs_inode.i_ctime.tv_sec	= status->mtime_server;
		vnode->vfs_inode.i_mtime	= vnode->vfs_inode.i_ctime;
		vnode->vfs_inode.i_atime	= vnode->vfs_inode.i_ctime;
	}

	expected_version = status->data_version;
	if (store_version)
		expected_version = *store_version;

	if (expected_version != data_version) {
		status->data_version = data_version;
		if (vnode && !test_bit(AFS_VNODE_UNSET, &vnode->flags)) {
			_debug("vnode modified %llx on {%x:%u}",
			       (unsigned long long) data_version,
			       vnode->fid.vid, vnode->fid.vnode);
			set_bit(AFS_VNODE_MODIFIED, &vnode->flags);
			set_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
		}
	} else if (store_version) {
		status->data_version = data_version;
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

static void xdr_decode_AFSCallBack_raw(const __be32 **_bp,
				       struct afs_callback *cb)
{
	const __be32 *bp = *_bp;

	cb->version	= ntohl(*bp++);
	cb->expiry	= ntohl(*bp++);
	cb->type	= ntohl(*bp++);
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
 * encode the requested attributes into an AFSStoreStatus block
 */
static void xdr_encode_AFS_StoreStatus(__be32 **_bp, struct iattr *attr)
{
	__be32 *bp = *_bp;
	u32 mask = 0, mtime = 0, owner = 0, group = 0, mode = 0;

	mask = 0;
	if (attr->ia_valid & ATTR_MTIME) {
		mask |= AFS_SET_MTIME;
		mtime = attr->ia_mtime.tv_sec;
	}

	if (attr->ia_valid & ATTR_UID) {
		mask |= AFS_SET_OWNER;
		owner = attr->ia_uid;
	}

	if (attr->ia_valid & ATTR_GID) {
		mask |= AFS_SET_GROUP;
		group = attr->ia_gid;
	}

	if (attr->ia_valid & ATTR_MODE) {
		mask |= AFS_SET_MODE;
		mode = attr->ia_mode & S_IALLUGO;
	}

	*bp++ = htonl(mask);
	*bp++ = htonl(mtime);
	*bp++ = htonl(owner);
	*bp++ = htonl(group);
	*bp++ = htonl(mode);
	*bp++ = 0;		/* segment size */
	*_bp = bp;
}

/*
 * deliver reply data to an FS.FetchStatus
 */
static int afs_deliver_fs_fetch_status(struct afs_call *call,
				       struct sk_buff *skb, bool last)
{
	struct afs_vnode *vnode = call->reply;
	const __be32 *bp;

	_enter(",,%u", last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, NULL);
	xdr_decode_AFSCallBack(&bp, vnode);
	if (call->reply2)
		xdr_decode_AFSVolSync(&bp, call->reply2);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchStatus operation type
 */
static const struct afs_call_type afs_RXFSFetchStatus = {
	.name		= "FS.FetchStatus",
	.deliver	= afs_deliver_fs_fetch_status,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch the status information for a file
 */
int afs_fs_fetch_file_status(struct afs_server *server,
			     struct key *key,
			     struct afs_vnode *vnode,
			     struct afs_volsync *volsync,
			     const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(&afs_RXFSFetchStatus, 16, (21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
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
	struct afs_vnode *vnode = call->reply;
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

		/* extract the returned data */
	case 2:
		_debug("extract data");
		if (call->count > 0) {
			page = call->reply3;
			buffer = kmap_atomic(page, KM_USER0);
			ret = afs_extract_data(call, skb, last, buffer,
					       call->count);
			kunmap_atomic(buffer, KM_USER0);
			switch (ret) {
			case 0:		break;
			case -EAGAIN:	return 0;
			default:	return ret;
			}
		}

		call->offset = 0;
		call->unmarshall++;

		/* extract the metadata */
	case 3:
		ret = afs_extract_data(call, skb, last, call->buffer,
				       (21 + 3 + 6) * 4);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		bp = call->buffer;
		xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, NULL);
		xdr_decode_AFSCallBack(&bp, vnode);
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

	if (call->count < PAGE_SIZE) {
		_debug("clear");
		page = call->reply3;
		buffer = kmap_atomic(page, KM_USER0);
		memset(buffer + call->count, 0, PAGE_SIZE - call->count);
		kunmap_atomic(buffer, KM_USER0);
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchData operation type
 */
static const struct afs_call_type afs_RXFSFetchData = {
	.name		= "FS.FetchData",
	.deliver	= afs_deliver_fs_fetch_data,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch data from a file
 */
int afs_fs_fetch_data(struct afs_server *server,
		      struct key *key,
		      struct afs_vnode *vnode,
		      off_t offset, size_t length,
		      struct page *buffer,
		      const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(&afs_RXFSFetchData, 24, (21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = vnode;
	call->reply2 = NULL; /* volsync */
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
	.name		= "FS.GiveUpCallBacks",
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

/*
 * deliver reply data to an FS.CreateFile or an FS.MakeDir
 */
static int afs_deliver_fs_create_vnode(struct afs_call *call,
				       struct sk_buff *skb, bool last)
{
	struct afs_vnode *vnode = call->reply;
	const __be32 *bp;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, call->reply2);
	xdr_decode_AFSFetchStatus(&bp, call->reply3, NULL, NULL);
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, NULL);
	xdr_decode_AFSCallBack_raw(&bp, call->reply4);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.CreateFile and FS.MakeDir operation type
 */
static const struct afs_call_type afs_RXFSCreateXXXX = {
	.name		= "FS.CreateXXXX",
	.deliver	= afs_deliver_fs_create_vnode,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * create a file or make a directory
 */
int afs_fs_create(struct afs_server *server,
		  struct key *key,
		  struct afs_vnode *vnode,
		  const char *name,
		  umode_t mode,
		  struct afs_fid *newfid,
		  struct afs_file_status *newstatus,
		  struct afs_callback *newcb,
		  const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (6 * 4);

	call = afs_alloc_flat_call(&afs_RXFSCreateXXXX, reqsz,
				   (3 + 21 + 21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = vnode;
	call->reply2 = newfid;
	call->reply3 = newstatus;
	call->reply4 = newcb;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(S_ISDIR(mode) ? FSMAKEDIR : FSCREATEFILE);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(AFS_SET_MODE);
	*bp++ = 0; /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(mode & S_IALLUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.RemoveFile or FS.RemoveDir
 */
static int afs_deliver_fs_remove(struct afs_call *call,
				 struct sk_buff *skb, bool last)
{
	struct afs_vnode *vnode = call->reply;
	const __be32 *bp;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, NULL);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.RemoveDir/FS.RemoveFile operation type
 */
static const struct afs_call_type afs_RXFSRemoveXXXX = {
	.name		= "FS.RemoveXXXX",
	.deliver	= afs_deliver_fs_remove,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * remove a file or directory
 */
int afs_fs_remove(struct afs_server *server,
		  struct key *key,
		  struct afs_vnode *vnode,
		  const char *name,
		  bool isdir,
		  const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz;

	call = afs_alloc_flat_call(&afs_RXFSRemoveXXXX, reqsz, (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = vnode;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(isdir ? FSREMOVEDIR : FSREMOVEFILE);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.Link
 */
static int afs_deliver_fs_link(struct afs_call *call,
			       struct sk_buff *skb, bool last)
{
	struct afs_vnode *dvnode = call->reply, *vnode = call->reply2;
	const __be32 *bp;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, NULL);
	xdr_decode_AFSFetchStatus(&bp, &dvnode->status, dvnode, NULL);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.Link operation type
 */
static const struct afs_call_type afs_RXFSLink = {
	.name		= "FS.Link",
	.deliver	= afs_deliver_fs_link,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * make a hard link
 */
int afs_fs_link(struct afs_server *server,
		struct key *key,
		struct afs_vnode *dvnode,
		struct afs_vnode *vnode,
		const char *name,
		const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (3 * 4);

	call = afs_alloc_flat_call(&afs_RXFSLink, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = dvnode;
	call->reply2 = vnode;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSLINK);
	*bp++ = htonl(dvnode->fid.vid);
	*bp++ = htonl(dvnode->fid.vnode);
	*bp++ = htonl(dvnode->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.Symlink
 */
static int afs_deliver_fs_symlink(struct afs_call *call,
				  struct sk_buff *skb, bool last)
{
	struct afs_vnode *vnode = call->reply;
	const __be32 *bp;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, call->reply2);
	xdr_decode_AFSFetchStatus(&bp, call->reply3, NULL, NULL);
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, NULL);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.Symlink operation type
 */
static const struct afs_call_type afs_RXFSSymlink = {
	.name		= "FS.Symlink",
	.deliver	= afs_deliver_fs_symlink,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * create a symbolic link
 */
int afs_fs_symlink(struct afs_server *server,
		   struct key *key,
		   struct afs_vnode *vnode,
		   const char *name,
		   const char *contents,
		   struct afs_fid *newfid,
		   struct afs_file_status *newstatus,
		   const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	size_t namesz, reqsz, padsz, c_namesz, c_padsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;

	c_namesz = strlen(contents);
	c_padsz = (4 - (c_namesz & 3)) & 3;

	reqsz = (6 * 4) + namesz + padsz + c_namesz + c_padsz + (6 * 4);

	call = afs_alloc_flat_call(&afs_RXFSSymlink, reqsz,
				   (3 + 21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = vnode;
	call->reply2 = newfid;
	call->reply3 = newstatus;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSYMLINK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(c_namesz);
	memcpy(bp, contents, c_namesz);
	bp = (void *) bp + c_namesz;
	if (c_padsz > 0) {
		memset(bp, 0, c_padsz);
		bp = (void *) bp + c_padsz;
	}
	*bp++ = htonl(AFS_SET_MODE);
	*bp++ = 0; /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(S_IRWXUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.Rename
 */
static int afs_deliver_fs_rename(struct afs_call *call,
				  struct sk_buff *skb, bool last)
{
	struct afs_vnode *orig_dvnode = call->reply, *new_dvnode = call->reply2;
	const __be32 *bp;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	afs_transfer_reply(call, skb);
	if (!last)
		return 0;

	if (call->reply_size != call->reply_max)
		return -EBADMSG;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, &orig_dvnode->status, orig_dvnode, NULL);
	if (new_dvnode != orig_dvnode)
		xdr_decode_AFSFetchStatus(&bp, &new_dvnode->status, new_dvnode,
					  NULL);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.Rename operation type
 */
static const struct afs_call_type afs_RXFSRename = {
	.name		= "FS.Rename",
	.deliver	= afs_deliver_fs_rename,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * create a symbolic link
 */
int afs_fs_rename(struct afs_server *server,
		  struct key *key,
		  struct afs_vnode *orig_dvnode,
		  const char *orig_name,
		  struct afs_vnode *new_dvnode,
		  const char *new_name,
		  const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	size_t reqsz, o_namesz, o_padsz, n_namesz, n_padsz;
	__be32 *bp;

	_enter("");

	o_namesz = strlen(orig_name);
	o_padsz = (4 - (o_namesz & 3)) & 3;

	n_namesz = strlen(new_name);
	n_padsz = (4 - (n_namesz & 3)) & 3;

	reqsz = (4 * 4) +
		4 + o_namesz + o_padsz +
		(3 * 4) +
		4 + n_namesz + n_padsz;

	call = afs_alloc_flat_call(&afs_RXFSRename, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = orig_dvnode;
	call->reply2 = new_dvnode;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSRENAME);
	*bp++ = htonl(orig_dvnode->fid.vid);
	*bp++ = htonl(orig_dvnode->fid.vnode);
	*bp++ = htonl(orig_dvnode->fid.unique);
	*bp++ = htonl(o_namesz);
	memcpy(bp, orig_name, o_namesz);
	bp = (void *) bp + o_namesz;
	if (o_padsz > 0) {
		memset(bp, 0, o_padsz);
		bp = (void *) bp + o_padsz;
	}

	*bp++ = htonl(new_dvnode->fid.vid);
	*bp++ = htonl(new_dvnode->fid.vnode);
	*bp++ = htonl(new_dvnode->fid.unique);
	*bp++ = htonl(n_namesz);
	memcpy(bp, new_name, n_namesz);
	bp = (void *) bp + n_namesz;
	if (n_padsz > 0) {
		memset(bp, 0, n_padsz);
		bp = (void *) bp + n_padsz;
	}

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.StoreData
 */
static int afs_deliver_fs_store_data(struct afs_call *call,
				     struct sk_buff *skb, bool last)
{
	struct afs_vnode *vnode = call->reply;
	const __be32 *bp;

	_enter(",,%u", last);

	afs_transfer_reply(call, skb);
	if (!last) {
		_leave(" = 0 [more]");
		return 0;
	}

	if (call->reply_size != call->reply_max) {
		_leave(" = -EBADMSG [%u != %u]",
		       call->reply_size, call->reply_max);
		return -EBADMSG;
	}

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode,
				  &call->store_version);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	afs_pages_written_back(vnode, call);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.StoreData operation type
 */
static const struct afs_call_type afs_RXFSStoreData = {
	.name		= "FS.StoreData",
	.deliver	= afs_deliver_fs_store_data,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * store a set of pages
 */
int afs_fs_store_data(struct afs_server *server, struct afs_writeback *wb,
		      pgoff_t first, pgoff_t last,
		      unsigned offset, unsigned to,
		      const struct afs_wait_mode *wait_mode)
{
	struct afs_vnode *vnode = wb->vnode;
	struct afs_call *call;
	loff_t size, pos, i_size;
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(wb->key), vnode->fid.vid, vnode->fid.vnode);

	size = to - offset;
	if (first != last)
		size += (loff_t)(last - first) << PAGE_SHIFT;
	pos = (loff_t)first << PAGE_SHIFT;
	pos += offset;

	i_size = i_size_read(&vnode->vfs_inode);
	if (pos + size > i_size)
		i_size = size + pos;

	_debug("size %llx, at %llx, i_size %llx",
	       (unsigned long long) size, (unsigned long long) pos,
	       (unsigned long long) i_size);

	BUG_ON(i_size > 0xffffffff); // TODO: use 64-bit store

	call = afs_alloc_flat_call(&afs_RXFSStoreData,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->wb = wb;
	call->key = wb->key;
	call->reply = vnode;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);
	call->mapping = vnode->vfs_inode.i_mapping;
	call->first = first;
	call->last = last;
	call->first_offset = offset;
	call->last_to = to;
	call->send_pages = true;
	call->store_version = vnode->status.data_version + 1;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	*bp++ = 0; /* mask */
	*bp++ = 0; /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = 0; /* unix mode */
	*bp++ = 0; /* segment size */

	*bp++ = htonl(pos);
	*bp++ = htonl(size);
	*bp++ = htonl(i_size);

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * deliver reply data to an FS.StoreStatus
 */
static int afs_deliver_fs_store_status(struct afs_call *call,
				       struct sk_buff *skb, bool last)
{
	afs_dataversion_t *store_version;
	struct afs_vnode *vnode = call->reply;
	const __be32 *bp;

	_enter(",,%u", last);

	afs_transfer_reply(call, skb);
	if (!last) {
		_leave(" = 0 [more]");
		return 0;
	}

	if (call->reply_size != call->reply_max) {
		_leave(" = -EBADMSG [%u != %u]",
		       call->reply_size, call->reply_max);
		return -EBADMSG;
	}

	/* unmarshall the reply once we've received all of it */
	store_version = NULL;
	if (call->operation_ID == FSSTOREDATA)
		store_version = &call->store_version;

	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, &vnode->status, vnode, store_version);
	/* xdr_decode_AFSVolSync(&bp, call->replyX); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.StoreStatus operation type
 */
static const struct afs_call_type afs_RXFSStoreStatus = {
	.name		= "FS.StoreStatus",
	.deliver	= afs_deliver_fs_store_status,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSStoreData_as_Status = {
	.name		= "FS.StoreData",
	.deliver	= afs_deliver_fs_store_status,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_flat_call_destructor,
};

/*
 * set the attributes on a file, using FS.StoreData rather than FS.StoreStatus
 * so as to alter the file size also
 */
static int afs_fs_setattr_size(struct afs_server *server, struct key *key,
			       struct afs_vnode *vnode, struct iattr *attr,
			       const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(key), vnode->fid.vid, vnode->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);
	ASSERTCMP(attr->ia_size, <=, 0xffffffff); // TODO: use 64-bit store

	call = afs_alloc_flat_call(&afs_RXFSStoreData_as_Status,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = vnode;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);
	call->store_version = vnode->status.data_version + 1;
	call->operation_ID = FSSTOREDATA;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	*bp++ = 0;				/* position of start of write */
	*bp++ = 0;				/* size of write */
	*bp++ = htonl(attr->ia_size);		/* new file length */

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}

/*
 * set the attributes on a file, using FS.StoreData if there's a change in file
 * size, and FS.StoreStatus otherwise
 */
int afs_fs_setattr(struct afs_server *server, struct key *key,
		   struct afs_vnode *vnode, struct iattr *attr,
		   const struct afs_wait_mode *wait_mode)
{
	struct afs_call *call;
	__be32 *bp;

	if (attr->ia_valid & ATTR_SIZE)
		return afs_fs_setattr_size(server, key, vnode, attr,
					   wait_mode);

	_enter(",%x,{%x:%u},,",
	       key_serial(key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(&afs_RXFSStoreStatus,
				   (4 + 6) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply = vnode;
	call->service_id = FS_SERVICE;
	call->port = htons(AFS_FS_PORT);
	call->operation_ID = FSSTORESTATUS;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTORESTATUS);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	return afs_make_call(&server->addr, call, GFP_NOFS, wait_mode);
}
