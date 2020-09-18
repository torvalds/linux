// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS File Server client stubs
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/circ_buf.h>
#include <linux/iversion.h>
#include "internal.h"
#include "afs_fs.h"
#include "xdr_fs.h"

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
 * Dump a bad file status record.
 */
static void xdr_dump_bad(const __be32 *bp)
{
	__be32 x[4];
	int i;

	pr_notice("AFS XDR: Bad status record\n");
	for (i = 0; i < 5 * 4 * 4; i += 16) {
		memcpy(x, bp, 16);
		bp += 4;
		pr_notice("%03x: %08x %08x %08x %08x\n",
			  i, ntohl(x[0]), ntohl(x[1]), ntohl(x[2]), ntohl(x[3]));
	}

	memcpy(x, bp, 4);
	pr_notice("0x50: %08x\n", ntohl(x[0]));
}

/*
 * decode an AFSFetchStatus block
 */
static void xdr_decode_AFSFetchStatus(const __be32 **_bp,
				      struct afs_call *call,
				      struct afs_status_cb *scb)
{
	const struct afs_xdr_AFSFetchStatus *xdr = (const void *)*_bp;
	struct afs_file_status *status = &scb->status;
	bool inline_error = (call->operation_ID == afs_FS_InlineBulkStatus);
	u64 data_version, size;
	u32 type, abort_code;

	abort_code = ntohl(xdr->abort_code);

	if (xdr->if_version != htonl(AFS_FSTATUS_VERSION)) {
		if (xdr->if_version == htonl(0) &&
		    abort_code != 0 &&
		    inline_error) {
			/* The OpenAFS fileserver has a bug in FS.InlineBulkStatus
			 * whereby it doesn't set the interface version in the error
			 * case.
			 */
			status->abort_code = abort_code;
			scb->have_error = true;
			goto advance;
		}

		pr_warn("Unknown AFSFetchStatus version %u\n", ntohl(xdr->if_version));
		goto bad;
	}

	if (abort_code != 0 && inline_error) {
		status->abort_code = abort_code;
		scb->have_error = true;
		goto advance;
	}

	type = ntohl(xdr->type);
	switch (type) {
	case AFS_FTYPE_FILE:
	case AFS_FTYPE_DIR:
	case AFS_FTYPE_SYMLINK:
		status->type = type;
		break;
	default:
		goto bad;
	}

	status->nlink		= ntohl(xdr->nlink);
	status->author		= ntohl(xdr->author);
	status->owner		= ntohl(xdr->owner);
	status->caller_access	= ntohl(xdr->caller_access); /* Ticket dependent */
	status->anon_access	= ntohl(xdr->anon_access);
	status->mode		= ntohl(xdr->mode) & S_IALLUGO;
	status->group		= ntohl(xdr->group);
	status->lock_count	= ntohl(xdr->lock_count);

	status->mtime_client.tv_sec = ntohl(xdr->mtime_client);
	status->mtime_client.tv_nsec = 0;
	status->mtime_server.tv_sec = ntohl(xdr->mtime_server);
	status->mtime_server.tv_nsec = 0;

	size  = (u64)ntohl(xdr->size_lo);
	size |= (u64)ntohl(xdr->size_hi) << 32;
	status->size = size;

	data_version  = (u64)ntohl(xdr->data_version_lo);
	data_version |= (u64)ntohl(xdr->data_version_hi) << 32;
	status->data_version = data_version;
	scb->have_status = true;
advance:
	*_bp = (const void *)*_bp + sizeof(*xdr);
	return;

bad:
	xdr_dump_bad(*_bp);
	afs_protocol_error(call, afs_eproto_bad_status);
	goto advance;
}

static time64_t xdr_decode_expiry(struct afs_call *call, u32 expiry)
{
	return ktime_divns(call->reply_time, NSEC_PER_SEC) + expiry;
}

static void xdr_decode_AFSCallBack(const __be32 **_bp,
				   struct afs_call *call,
				   struct afs_status_cb *scb)
{
	struct afs_callback *cb = &scb->callback;
	const __be32 *bp = *_bp;

	bp++; /* version */
	cb->expires_at	= xdr_decode_expiry(call, ntohl(*bp++));
	bp++; /* type */
	scb->have_cb	= true;
	*_bp = bp;
}

/*
 * decode an AFSVolSync block
 */
static void xdr_decode_AFSVolSync(const __be32 **_bp,
				  struct afs_volsync *volsync)
{
	const __be32 *bp = *_bp;
	u32 creation;

	creation = ntohl(*bp++);
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */
	bp++; /* spare5 */
	bp++; /* spare6 */
	*_bp = bp;

	if (volsync)
		volsync->creation = creation;
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
		owner = from_kuid(&init_user_ns, attr->ia_uid);
	}

	if (attr->ia_valid & ATTR_GID) {
		mask |= AFS_SET_GROUP;
		group = from_kgid(&init_user_ns, attr->ia_gid);
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
 * decode an AFSFetchVolumeStatus block
 */
static void xdr_decode_AFSFetchVolumeStatus(const __be32 **_bp,
					    struct afs_volume_status *vs)
{
	const __be32 *bp = *_bp;

	vs->vid			= ntohl(*bp++);
	vs->parent_id		= ntohl(*bp++);
	vs->online		= ntohl(*bp++);
	vs->in_service		= ntohl(*bp++);
	vs->blessed		= ntohl(*bp++);
	vs->needs_salvage	= ntohl(*bp++);
	vs->type		= ntohl(*bp++);
	vs->min_quota		= ntohl(*bp++);
	vs->max_quota		= ntohl(*bp++);
	vs->blocks_in_use	= ntohl(*bp++);
	vs->part_blocks_avail	= ntohl(*bp++);
	vs->part_max_blocks	= ntohl(*bp++);
	vs->vol_copy_date	= 0;
	vs->vol_backup_date	= 0;
	*_bp = bp;
}

/*
 * deliver reply data to an FS.FetchStatus
 */
static int afs_deliver_fs_fetch_status(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *vp = &op->file[op->fetch_status.which];
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
	xdr_decode_AFSCallBack(&bp, call, &vp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchStatus operation type
 */
static const struct afs_call_type afs_RXFSFetchStatus = {
	.name		= "FS.FetchStatus",
	.op		= afs_FS_FetchStatus,
	.deliver	= afs_deliver_fs_fetch_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch the status information for a file
 */
void afs_fs_fetch_status(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[op->fetch_status.which];
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	call = afs_alloc_flat_call(op->net, &afs_RXFSFetchStatus,
				   16, (21 + 3 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(vp->fid.vid);
	bp[2] = htonl(vp->fid.vnode);
	bp[3] = htonl(vp->fid.unique);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.FetchData
 */
static int afs_deliver_fs_fetch_data(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_read *req = op->fetch.req;
	const __be32 *bp;
	int ret;

	_enter("{%u,%zu,%zu/%llu}",
	       call->unmarshall, call->iov_len, iov_iter_count(call->iter),
	       req->actual_len);

	switch (call->unmarshall) {
	case 0:
		req->actual_len = 0;
		call->unmarshall++;
		if (call->operation_ID == FSFETCHDATA64) {
			afs_extract_to_tmp64(call);
		} else {
			call->tmp_u = htonl(0);
			afs_extract_to_tmp(call);
		}
		fallthrough;

		/* Extract the returned data length into
		 * ->actual_len.  This may indicate more or less data than was
		 * requested will be returned.
		 */
	case 1:
		_debug("extract data length");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		req->actual_len = be64_to_cpu(call->tmp64);
		_debug("DATA length: %llu", req->actual_len);

		if (req->actual_len == 0)
			goto no_more_data;

		call->iter = req->iter;
		call->iov_len = min(req->actual_len, req->len);
		call->unmarshall++;
		fallthrough;

		/* extract the returned data */
	case 2:
		_debug("extract data %zu/%llu",
		       iov_iter_count(call->iter), req->actual_len);

		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->iter = &call->def_iter;
		if (req->actual_len <= req->len)
			goto no_more_data;

		/* Discard any excess data the server gave us */
		afs_extract_discard(call, req->actual_len - req->len);
		call->unmarshall = 3;
		fallthrough;

	case 3:
		_debug("extract discard %zu/%llu",
		       iov_iter_count(call->iter), req->actual_len - req->len);

		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

	no_more_data:
		call->unmarshall = 4;
		afs_extract_to_buf(call, (21 + 3 + 6) * 4);
		fallthrough;

		/* extract the metadata */
	case 4:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
		xdr_decode_AFSCallBack(&bp, call, &vp->scb);
		xdr_decode_AFSVolSync(&bp, &op->volsync);

		req->data_version = vp->scb.status.data_version;
		req->file_size = vp->scb.status.size;

		call->unmarshall++;

	case 5:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchData operation type
 */
static const struct afs_call_type afs_RXFSFetchData = {
	.name		= "FS.FetchData",
	.op		= afs_FS_FetchData,
	.deliver	= afs_deliver_fs_fetch_data,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSFetchData64 = {
	.name		= "FS.FetchData64",
	.op		= afs_FS_FetchData64,
	.deliver	= afs_deliver_fs_fetch_data,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch data from a very large file
 */
static void afs_fs_fetch_data64(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_read *req = op->fetch.req;
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(op->net, &afs_RXFSFetchData64, 32, (21 + 3 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHDATA64);
	bp[1] = htonl(vp->fid.vid);
	bp[2] = htonl(vp->fid.vnode);
	bp[3] = htonl(vp->fid.unique);
	bp[4] = htonl(upper_32_bits(req->pos));
	bp[5] = htonl(lower_32_bits(req->pos));
	bp[6] = 0;
	bp[7] = htonl(lower_32_bits(req->len));

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * fetch data from a file
 */
void afs_fs_fetch_data(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	struct afs_read *req = op->fetch.req;
	__be32 *bp;

	if (upper_32_bits(req->pos) ||
	    upper_32_bits(req->len) ||
	    upper_32_bits(req->pos + req->len))
		return afs_fs_fetch_data64(op);

	_enter("");

	call = afs_alloc_flat_call(op->net, &afs_RXFSFetchData, 24, (21 + 3 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	req->call_debug_id = call->debug_id;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHDATA);
	bp[1] = htonl(vp->fid.vid);
	bp[2] = htonl(vp->fid.vnode);
	bp[3] = htonl(vp->fid.unique);
	bp[4] = htonl(lower_32_bits(req->pos));
	bp[5] = htonl(lower_32_bits(req->len));

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.CreateFile or an FS.MakeDir
 */
static int afs_deliver_fs_create_vnode(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, &op->file[1].fid);
	xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
	xdr_decode_AFSFetchStatus(&bp, call, &dvp->scb);
	xdr_decode_AFSCallBack(&bp, call, &vp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.CreateFile and FS.MakeDir operation type
 */
static const struct afs_call_type afs_RXFSCreateFile = {
	.name		= "FS.CreateFile",
	.op		= afs_FS_CreateFile,
	.deliver	= afs_deliver_fs_create_vnode,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Create a file.
 */
void afs_fs_create_file(struct afs_operation *op)
{
	const struct qstr *name = &op->dentry->d_name;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = name->len;
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (6 * 4);

	call = afs_alloc_flat_call(op->net, &afs_RXFSCreateFile,
				   reqsz, (3 + 21 + 21 + 3 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSCREATEFILE);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name->name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(op->mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(op->create.mode & S_IALLUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	trace_afs_make_fs_call1(call, &dvp->fid, name);
	afs_make_op_call(op, call, GFP_NOFS);
}

static const struct afs_call_type afs_RXFSMakeDir = {
	.name		= "FS.MakeDir",
	.op		= afs_FS_MakeDir,
	.deliver	= afs_deliver_fs_create_vnode,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Create a new directory
 */
void afs_fs_make_dir(struct afs_operation *op)
{
	const struct qstr *name = &op->dentry->d_name;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = name->len;
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (6 * 4);

	call = afs_alloc_flat_call(op->net, &afs_RXFSMakeDir,
				   reqsz, (3 + 21 + 21 + 3 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSMAKEDIR);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name->name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(op->mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(op->create.mode & S_IALLUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	trace_afs_make_fs_call1(call, &dvp->fid, name);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * Deliver reply data to any operation that returns status and volume sync.
 */
static int afs_deliver_fs_file_status_and_vol(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *vp = &op->file[0];
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.RemoveFile operation type
 */
static const struct afs_call_type afs_RXFSRemoveFile = {
	.name		= "FS.RemoveFile",
	.op		= afs_FS_RemoveFile,
	.deliver	= afs_deliver_fs_file_status_and_vol,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Remove a file.
 */
void afs_fs_remove_file(struct afs_operation *op)
{
	const struct qstr *name = &op->dentry->d_name;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = name->len;
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz;

	call = afs_alloc_flat_call(op->net, &afs_RXFSRemoveFile,
				   reqsz, (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSREMOVEFILE);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name->name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}

	trace_afs_make_fs_call1(call, &dvp->fid, name);
	afs_make_op_call(op, call, GFP_NOFS);
}

static const struct afs_call_type afs_RXFSRemoveDir = {
	.name		= "FS.RemoveDir",
	.op		= afs_FS_RemoveDir,
	.deliver	= afs_deliver_fs_file_status_and_vol,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Remove a directory.
 */
void afs_fs_remove_dir(struct afs_operation *op)
{
	const struct qstr *name = &op->dentry->d_name;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = name->len;
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz;

	call = afs_alloc_flat_call(op->net, &afs_RXFSRemoveDir,
				   reqsz, (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSREMOVEDIR);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name->name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}

	trace_afs_make_fs_call1(call, &dvp->fid, name);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.Link
 */
static int afs_deliver_fs_link(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
	xdr_decode_AFSFetchStatus(&bp, call, &dvp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.Link operation type
 */
static const struct afs_call_type afs_RXFSLink = {
	.name		= "FS.Link",
	.op		= afs_FS_Link,
	.deliver	= afs_deliver_fs_link,
	.destructor	= afs_flat_call_destructor,
};

/*
 * make a hard link
 */
void afs_fs_link(struct afs_operation *op)
{
	const struct qstr *name = &op->dentry->d_name;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	struct afs_call *call;
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = name->len;
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (3 * 4);

	call = afs_alloc_flat_call(op->net, &afs_RXFSLink, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSLINK);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name->name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	trace_afs_make_fs_call1(call, &vp->fid, name);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.Symlink
 */
static int afs_deliver_fs_symlink(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, &vp->fid);
	xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
	xdr_decode_AFSFetchStatus(&bp, call, &dvp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.Symlink operation type
 */
static const struct afs_call_type afs_RXFSSymlink = {
	.name		= "FS.Symlink",
	.op		= afs_FS_Symlink,
	.deliver	= afs_deliver_fs_symlink,
	.destructor	= afs_flat_call_destructor,
};

/*
 * create a symbolic link
 */
void afs_fs_symlink(struct afs_operation *op)
{
	const struct qstr *name = &op->dentry->d_name;
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_call *call;
	size_t namesz, reqsz, padsz, c_namesz, c_padsz;
	__be32 *bp;

	_enter("");

	namesz = name->len;
	padsz = (4 - (namesz & 3)) & 3;

	c_namesz = strlen(op->create.symlink);
	c_padsz = (4 - (c_namesz & 3)) & 3;

	reqsz = (6 * 4) + namesz + padsz + c_namesz + c_padsz + (6 * 4);

	call = afs_alloc_flat_call(op->net, &afs_RXFSSymlink, reqsz,
				   (3 + 21 + 21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSYMLINK);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(namesz);
	memcpy(bp, name->name, namesz);
	bp = (void *) bp + namesz;
	if (padsz > 0) {
		memset(bp, 0, padsz);
		bp = (void *) bp + padsz;
	}
	*bp++ = htonl(c_namesz);
	memcpy(bp, op->create.symlink, c_namesz);
	bp = (void *) bp + c_namesz;
	if (c_padsz > 0) {
		memset(bp, 0, c_padsz);
		bp = (void *) bp + c_padsz;
	}
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(op->mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(S_IRWXUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	trace_afs_make_fs_call1(call, &dvp->fid, name);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.Rename
 */
static int afs_deliver_fs_rename(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *orig_dvp = &op->file[0];
	struct afs_vnode_param *new_dvp = &op->file[1];
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	/* If the two dirs are the same, we have two copies of the same status
	 * report, so we just decode it twice.
	 */
	xdr_decode_AFSFetchStatus(&bp, call, &orig_dvp->scb);
	xdr_decode_AFSFetchStatus(&bp, call, &new_dvp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.Rename operation type
 */
static const struct afs_call_type afs_RXFSRename = {
	.name		= "FS.Rename",
	.op		= afs_FS_Rename,
	.deliver	= afs_deliver_fs_rename,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Rename/move a file or directory.
 */
void afs_fs_rename(struct afs_operation *op)
{
	struct afs_vnode_param *orig_dvp = &op->file[0];
	struct afs_vnode_param *new_dvp = &op->file[1];
	const struct qstr *orig_name = &op->dentry->d_name;
	const struct qstr *new_name = &op->dentry_2->d_name;
	struct afs_call *call;
	size_t reqsz, o_namesz, o_padsz, n_namesz, n_padsz;
	__be32 *bp;

	_enter("");

	o_namesz = orig_name->len;
	o_padsz = (4 - (o_namesz & 3)) & 3;

	n_namesz = new_name->len;
	n_padsz = (4 - (n_namesz & 3)) & 3;

	reqsz = (4 * 4) +
		4 + o_namesz + o_padsz +
		(3 * 4) +
		4 + n_namesz + n_padsz;

	call = afs_alloc_flat_call(op->net, &afs_RXFSRename, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSRENAME);
	*bp++ = htonl(orig_dvp->fid.vid);
	*bp++ = htonl(orig_dvp->fid.vnode);
	*bp++ = htonl(orig_dvp->fid.unique);
	*bp++ = htonl(o_namesz);
	memcpy(bp, orig_name->name, o_namesz);
	bp = (void *) bp + o_namesz;
	if (o_padsz > 0) {
		memset(bp, 0, o_padsz);
		bp = (void *) bp + o_padsz;
	}

	*bp++ = htonl(new_dvp->fid.vid);
	*bp++ = htonl(new_dvp->fid.vnode);
	*bp++ = htonl(new_dvp->fid.unique);
	*bp++ = htonl(n_namesz);
	memcpy(bp, new_name->name, n_namesz);
	bp = (void *) bp + n_namesz;
	if (n_padsz > 0) {
		memset(bp, 0, n_padsz);
		bp = (void *) bp + n_padsz;
	}

	trace_afs_make_fs_call2(call, &orig_dvp->fid, orig_name, new_name);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * Deliver reply data to FS.StoreData or FS.StoreStatus
 */
static int afs_deliver_fs_store_data(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *vp = &op->file[0];
	const __be32 *bp;
	int ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.StoreData operation type
 */
static const struct afs_call_type afs_RXFSStoreData = {
	.name		= "FS.StoreData",
	.op		= afs_FS_StoreData,
	.deliver	= afs_deliver_fs_store_data,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSStoreData64 = {
	.name		= "FS.StoreData64",
	.op		= afs_FS_StoreData64,
	.deliver	= afs_deliver_fs_store_data,
	.destructor	= afs_flat_call_destructor,
};

/*
 * store a set of pages to a very large file
 */
static void afs_fs_store_data64(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	call = afs_alloc_flat_call(op->net, &afs_RXFSStoreData64,
				   (4 + 6 + 3 * 2) * 4,
				   (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	call->write_iter = op->store.write_iter;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA64);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	*bp++ = htonl(AFS_SET_MTIME); /* mask */
	*bp++ = htonl(op->mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = 0; /* unix mode */
	*bp++ = 0; /* segment size */

	*bp++ = htonl(upper_32_bits(op->store.pos));
	*bp++ = htonl(lower_32_bits(op->store.pos));
	*bp++ = htonl(upper_32_bits(op->store.size));
	*bp++ = htonl(lower_32_bits(op->store.size));
	*bp++ = htonl(upper_32_bits(op->store.i_size));
	*bp++ = htonl(lower_32_bits(op->store.i_size));

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * Write data to a file on the server.
 */
void afs_fs_store_data(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	_debug("size %llx, at %llx, i_size %llx",
	       (unsigned long long)op->store.size,
	       (unsigned long long)op->store.pos,
	       (unsigned long long)op->store.i_size);

	if (upper_32_bits(op->store.pos) ||
	    upper_32_bits(op->store.size) ||
	    upper_32_bits(op->store.i_size))
		return afs_fs_store_data64(op);

	call = afs_alloc_flat_call(op->net, &afs_RXFSStoreData,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	call->write_iter = op->store.write_iter;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	*bp++ = htonl(AFS_SET_MTIME); /* mask */
	*bp++ = htonl(op->mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = 0; /* unix mode */
	*bp++ = 0; /* segment size */

	*bp++ = htonl(lower_32_bits(op->store.pos));
	*bp++ = htonl(lower_32_bits(op->store.size));
	*bp++ = htonl(lower_32_bits(op->store.i_size));

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * FS.StoreStatus operation type
 */
static const struct afs_call_type afs_RXFSStoreStatus = {
	.name		= "FS.StoreStatus",
	.op		= afs_FS_StoreStatus,
	.deliver	= afs_deliver_fs_store_data,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSStoreData_as_Status = {
	.name		= "FS.StoreData",
	.op		= afs_FS_StoreData,
	.deliver	= afs_deliver_fs_store_data,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSStoreData64_as_Status = {
	.name		= "FS.StoreData64",
	.op		= afs_FS_StoreData64,
	.deliver	= afs_deliver_fs_store_data,
	.destructor	= afs_flat_call_destructor,
};

/*
 * set the attributes on a very large file, using FS.StoreData rather than
 * FS.StoreStatus so as to alter the file size also
 */
static void afs_fs_setattr_size64(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	struct iattr *attr = op->setattr.attr;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);

	call = afs_alloc_flat_call(op->net, &afs_RXFSStoreData64_as_Status,
				   (4 + 6 + 3 * 2) * 4,
				   (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA64);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	*bp++ = htonl(upper_32_bits(attr->ia_size));	/* position of start of write */
	*bp++ = htonl(lower_32_bits(attr->ia_size));
	*bp++ = 0;					/* size of write */
	*bp++ = 0;
	*bp++ = htonl(upper_32_bits(attr->ia_size));	/* new file length */
	*bp++ = htonl(lower_32_bits(attr->ia_size));

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * set the attributes on a file, using FS.StoreData rather than FS.StoreStatus
 * so as to alter the file size also
 */
static void afs_fs_setattr_size(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	struct iattr *attr = op->setattr.attr;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);
	if (upper_32_bits(attr->ia_size))
		return afs_fs_setattr_size64(op);

	call = afs_alloc_flat_call(op->net, &afs_RXFSStoreData_as_Status,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	*bp++ = htonl(attr->ia_size);		/* position of start of write */
	*bp++ = 0;				/* size of write */
	*bp++ = htonl(attr->ia_size);		/* new file length */

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * set the attributes on a file, using FS.StoreData if there's a change in file
 * size, and FS.StoreStatus otherwise
 */
void afs_fs_setattr(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	struct iattr *attr = op->setattr.attr;
	__be32 *bp;

	if (attr->ia_valid & ATTR_SIZE)
		return afs_fs_setattr_size(op);

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	call = afs_alloc_flat_call(op->net, &afs_RXFSStoreStatus,
				   (4 + 6) * 4,
				   (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTORESTATUS);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, op->setattr.attr);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.GetVolumeStatus
 */
static int afs_deliver_fs_get_volume_status(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	const __be32 *bp;
	char *p;
	u32 size;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->unmarshall++;
		afs_extract_to_buf(call, 12 * 4);
		fallthrough;

		/* extract the returned status record */
	case 1:
		_debug("extract status");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSFetchVolumeStatus(&bp, &op->volstatus.vs);
		call->unmarshall++;
		afs_extract_to_tmp(call);
		fallthrough;

		/* extract the volume name length */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("volname length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, afs_eproto_volname_len);
		size = (call->count + 3) & ~3; /* It's padded */
		afs_extract_to_buf(call, size);
		call->unmarshall++;
		fallthrough;

		/* extract the volume name */
	case 3:
		_debug("extract volname");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		p = call->buffer;
		p[call->count] = 0;
		_debug("volname '%s'", p);
		afs_extract_to_tmp(call);
		call->unmarshall++;
		fallthrough;

		/* extract the offline message length */
	case 4:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("offline msg length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, afs_eproto_offline_msg_len);
		size = (call->count + 3) & ~3; /* It's padded */
		afs_extract_to_buf(call, size);
		call->unmarshall++;
		fallthrough;

		/* extract the offline message */
	case 5:
		_debug("extract offline");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		p = call->buffer;
		p[call->count] = 0;
		_debug("offline '%s'", p);

		afs_extract_to_tmp(call);
		call->unmarshall++;
		fallthrough;

		/* extract the message of the day length */
	case 6:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("motd length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, afs_eproto_motd_len);
		size = (call->count + 3) & ~3; /* It's padded */
		afs_extract_to_buf(call, size);
		call->unmarshall++;
		fallthrough;

		/* extract the message of the day */
	case 7:
		_debug("extract motd");
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		p = call->buffer;
		p[call->count] = 0;
		_debug("motd '%s'", p);

		call->unmarshall++;

	case 8:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.GetVolumeStatus operation type
 */
static const struct afs_call_type afs_RXFSGetVolumeStatus = {
	.name		= "FS.GetVolumeStatus",
	.op		= afs_FS_GetVolumeStatus,
	.deliver	= afs_deliver_fs_get_volume_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch the status of a volume
 */
void afs_fs_get_volume_status(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(op->net, &afs_RXFSGetVolumeStatus, 2 * 4,
				   max(12 * 4, AFSOPAQUEMAX + 1));
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSGETVOLUMESTATUS);
	bp[1] = htonl(vp->fid.vid);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.SetLock, FS.ExtendLock or FS.ReleaseLock
 */
static int afs_deliver_fs_xxxx_lock(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSVolSync(&bp, &op->volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.SetLock operation type
 */
static const struct afs_call_type afs_RXFSSetLock = {
	.name		= "FS.SetLock",
	.op		= afs_FS_SetLock,
	.deliver	= afs_deliver_fs_xxxx_lock,
	.done		= afs_lock_op_done,
	.destructor	= afs_flat_call_destructor,
};

/*
 * FS.ExtendLock operation type
 */
static const struct afs_call_type afs_RXFSExtendLock = {
	.name		= "FS.ExtendLock",
	.op		= afs_FS_ExtendLock,
	.deliver	= afs_deliver_fs_xxxx_lock,
	.done		= afs_lock_op_done,
	.destructor	= afs_flat_call_destructor,
};

/*
 * FS.ReleaseLock operation type
 */
static const struct afs_call_type afs_RXFSReleaseLock = {
	.name		= "FS.ReleaseLock",
	.op		= afs_FS_ReleaseLock,
	.deliver	= afs_deliver_fs_xxxx_lock,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Set a lock on a file
 */
void afs_fs_set_lock(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(op->net, &afs_RXFSSetLock, 5 * 4, 6 * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSETLOCK);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);
	*bp++ = htonl(op->lock.type);

	trace_afs_make_fs_calli(call, &vp->fid, op->lock.type);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * extend a lock on a file
 */
void afs_fs_extend_lock(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(op->net, &afs_RXFSExtendLock, 4 * 4, 6 * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSEXTENDLOCK);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * release a lock on a file
 */
void afs_fs_release_lock(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(op->net, &afs_RXFSReleaseLock, 4 * 4, 6 * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSRELEASELOCK);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * Deliver reply data to an FS.GiveUpAllCallBacks operation.
 */
static int afs_deliver_fs_give_up_all_callbacks(struct afs_call *call)
{
	return afs_transfer_reply(call);
}

/*
 * FS.GiveUpAllCallBacks operation type
 */
static const struct afs_call_type afs_RXFSGiveUpAllCallBacks = {
	.name		= "FS.GiveUpAllCallBacks",
	.op		= afs_FS_GiveUpAllCallBacks,
	.deliver	= afs_deliver_fs_give_up_all_callbacks,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Flush all the callbacks we have on a server.
 */
int afs_fs_give_up_all_callbacks(struct afs_net *net,
				 struct afs_server *server,
				 struct afs_addr_cursor *ac,
				 struct key *key)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSGiveUpAllCallBacks, 1 * 4, 0);
	if (!call)
		return -ENOMEM;

	call->key = key;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSGIVEUPALLCALLBACKS);

	call->server = afs_use_server(server, afs_server_trace_give_up_cb);
	afs_make_call(ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, ac);
}

/*
 * Deliver reply data to an FS.GetCapabilities operation.
 */
static int afs_deliver_fs_get_capabilities(struct afs_call *call)
{
	u32 count;
	int ret;

	_enter("{%u,%zu}", call->unmarshall, iov_iter_count(call->iter));

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;
		fallthrough;

		/* Extract the capabilities word count */
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		count = ntohl(call->tmp);

		call->count = count;
		call->count2 = count;
		afs_extract_discard(call, count * sizeof(__be32));
		call->unmarshall++;
		fallthrough;

		/* Extract capabilities words */
	case 2:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		/* TODO: Examine capabilities */

		call->unmarshall++;
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.GetCapabilities operation type
 */
static const struct afs_call_type afs_RXFSGetCapabilities = {
	.name		= "FS.GetCapabilities",
	.op		= afs_FS_GetCapabilities,
	.deliver	= afs_deliver_fs_get_capabilities,
	.done		= afs_fileserver_probe_result,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Probe a fileserver for the capabilities that it supports.  This RPC can
 * reply with up to 196 words.  The operation is asynchronous and if we managed
 * to allocate a call, true is returned the result is delivered through the
 * ->done() - otherwise we return false to indicate we didn't even try.
 */
bool afs_fs_get_capabilities(struct afs_net *net, struct afs_server *server,
			     struct afs_addr_cursor *ac, struct key *key)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSGetCapabilities, 1 * 4, 16 * 4);
	if (!call)
		return false;

	call->key = key;
	call->server = afs_use_server(server, afs_server_trace_get_caps);
	call->upgrade = true;
	call->async = true;
	call->max_lifespan = AFS_PROBE_MAX_LIFESPAN;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSGETCAPABILITIES);

	trace_afs_make_fs_call(call, NULL);
	afs_make_call(ac, call, GFP_NOFS);
	afs_put_call(call);
	return true;
}

/*
 * Deliver reply data to an FS.InlineBulkStatus call
 */
static int afs_deliver_fs_inline_bulk_status(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_status_cb *scb;
	const __be32 *bp;
	u32 tmp;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;
		fallthrough;

		/* Extract the file status count and array in two steps */
	case 1:
		_debug("extract status count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		tmp = ntohl(call->tmp);
		_debug("status count: %u/%u", tmp, op->nr_files);
		if (tmp != op->nr_files)
			return afs_protocol_error(call, afs_eproto_ibulkst_count);

		call->count = 0;
		call->unmarshall++;
	more_counts:
		afs_extract_to_buf(call, 21 * sizeof(__be32));
		fallthrough;

	case 2:
		_debug("extract status array %u", call->count);
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		switch (call->count) {
		case 0:
			scb = &op->file[0].scb;
			break;
		case 1:
			scb = &op->file[1].scb;
			break;
		default:
			scb = &op->more_files[call->count - 2].scb;
			break;
		}

		bp = call->buffer;
		xdr_decode_AFSFetchStatus(&bp, call, scb);

		call->count++;
		if (call->count < op->nr_files)
			goto more_counts;

		call->count = 0;
		call->unmarshall++;
		afs_extract_to_tmp(call);
		fallthrough;

		/* Extract the callback count and array in two steps */
	case 3:
		_debug("extract CB count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		tmp = ntohl(call->tmp);
		_debug("CB count: %u", tmp);
		if (tmp != op->nr_files)
			return afs_protocol_error(call, afs_eproto_ibulkst_cb_count);
		call->count = 0;
		call->unmarshall++;
	more_cbs:
		afs_extract_to_buf(call, 3 * sizeof(__be32));
		fallthrough;

	case 4:
		_debug("extract CB array");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		_debug("unmarshall CB array");
		switch (call->count) {
		case 0:
			scb = &op->file[0].scb;
			break;
		case 1:
			scb = &op->file[1].scb;
			break;
		default:
			scb = &op->more_files[call->count - 2].scb;
			break;
		}

		bp = call->buffer;
		xdr_decode_AFSCallBack(&bp, call, scb);
		call->count++;
		if (call->count < op->nr_files)
			goto more_cbs;

		afs_extract_to_buf(call, 6 * sizeof(__be32));
		call->unmarshall++;
		fallthrough;

	case 5:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSVolSync(&bp, &op->volsync);

		call->unmarshall++;

	case 6:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

static void afs_done_fs_inline_bulk_status(struct afs_call *call)
{
	if (call->error == -ECONNABORTED &&
	    call->abort_code == RX_INVALID_OPERATION) {
		set_bit(AFS_SERVER_FL_NO_IBULK, &call->server->flags);
		if (call->op)
			set_bit(AFS_VOLUME_MAYBE_NO_IBULK, &call->op->volume->flags);
	}
}

/*
 * FS.InlineBulkStatus operation type
 */
static const struct afs_call_type afs_RXFSInlineBulkStatus = {
	.name		= "FS.InlineBulkStatus",
	.op		= afs_FS_InlineBulkStatus,
	.deliver	= afs_deliver_fs_inline_bulk_status,
	.done		= afs_done_fs_inline_bulk_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the status information for up to 50 files
 */
void afs_fs_inline_bulk_status(struct afs_operation *op)
{
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	struct afs_call *call;
	__be32 *bp;
	int i;

	if (test_bit(AFS_SERVER_FL_NO_IBULK, &op->server->flags)) {
		op->error = -ENOTSUPP;
		return;
	}

	_enter(",%x,{%llx:%llu},%u",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode, op->nr_files);

	call = afs_alloc_flat_call(op->net, &afs_RXFSInlineBulkStatus,
				   (2 + op->nr_files * 3) * 4,
				   21 * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSINLINEBULKSTATUS);
	*bp++ = htonl(op->nr_files);
	*bp++ = htonl(dvp->fid.vid);
	*bp++ = htonl(dvp->fid.vnode);
	*bp++ = htonl(dvp->fid.unique);
	*bp++ = htonl(vp->fid.vid);
	*bp++ = htonl(vp->fid.vnode);
	*bp++ = htonl(vp->fid.unique);
	for (i = 0; i < op->nr_files - 2; i++) {
		*bp++ = htonl(op->more_files[i].fid.vid);
		*bp++ = htonl(op->more_files[i].fid.vnode);
		*bp++ = htonl(op->more_files[i].fid.unique);
	}

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_NOFS);
}

/*
 * deliver reply data to an FS.FetchACL
 */
static int afs_deliver_fs_fetch_acl(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_acl *acl;
	const __be32 *bp;
	unsigned int size;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;
		fallthrough;

		/* extract the returned data length */
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		size = call->count2 = ntohl(call->tmp);
		size = round_up(size, 4);

		acl = kmalloc(struct_size(acl, data, size), GFP_KERNEL);
		if (!acl)
			return -ENOMEM;
		op->acl = acl;
		acl->size = call->count2;
		afs_extract_begin(call, acl->data, size);
		call->unmarshall++;
		fallthrough;

		/* extract the returned data */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		afs_extract_to_buf(call, (21 + 6) * 4);
		call->unmarshall++;
		fallthrough;

		/* extract the metadata */
	case 3:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSFetchStatus(&bp, call, &vp->scb);
		xdr_decode_AFSVolSync(&bp, &op->volsync);

		call->unmarshall++;

	case 4:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchACL operation type
 */
static const struct afs_call_type afs_RXFSFetchACL = {
	.name		= "FS.FetchACL",
	.op		= afs_FS_FetchACL,
	.deliver	= afs_deliver_fs_fetch_acl,
};

/*
 * Fetch the ACL for a file.
 */
void afs_fs_fetch_acl(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	call = afs_alloc_flat_call(op->net, &afs_RXFSFetchACL, 16, (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHACL);
	bp[1] = htonl(vp->fid.vid);
	bp[2] = htonl(vp->fid.vnode);
	bp[3] = htonl(vp->fid.unique);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_KERNEL);
}

/*
 * FS.StoreACL operation type
 */
static const struct afs_call_type afs_RXFSStoreACL = {
	.name		= "FS.StoreACL",
	.op		= afs_FS_StoreACL,
	.deliver	= afs_deliver_fs_file_status_and_vol,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the ACL for a file.
 */
void afs_fs_store_acl(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct afs_call *call;
	const struct afs_acl *acl = op->acl;
	size_t size;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(op->key), vp->fid.vid, vp->fid.vnode);

	size = round_up(acl->size, 4);
	call = afs_alloc_flat_call(op->net, &afs_RXFSStoreACL,
				   5 * 4 + size, (21 + 6) * 4);
	if (!call)
		return afs_op_nomem(op);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSSTOREACL);
	bp[1] = htonl(vp->fid.vid);
	bp[2] = htonl(vp->fid.vnode);
	bp[3] = htonl(vp->fid.unique);
	bp[4] = htonl(acl->size);
	memcpy(&bp[5], acl->data, acl->size);
	if (acl->size != size)
		memset((void *)&bp[5] + acl->size, 0, size - acl->size);

	trace_afs_make_fs_call(call, &vp->fid);
	afs_make_op_call(op, call, GFP_KERNEL);
}
