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
#include "protocol_yfs.h"

static inline void afs_use_fs_server(struct afs_call *call, struct afs_cb_interest *cbi)
{
	call->cbi = afs_get_cb_interest(cbi);
}

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
static int xdr_decode_AFSFetchStatus(const __be32 **_bp,
				     struct afs_call *call,
				     struct afs_status_cb *scb)
{
	const struct afs_xdr_AFSFetchStatus *xdr = (const void *)*_bp;
	struct afs_file_status *status = &scb->status;
	bool inline_error = (call->operation_ID == afs_FS_InlineBulkStatus);
	u64 data_version, size;
	u32 type, abort_code;
	int ret;

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
			goto good;
		}

		pr_warn("Unknown AFSFetchStatus version %u\n", ntohl(xdr->if_version));
		goto bad;
	}

	if (abort_code != 0 && inline_error) {
		status->abort_code = abort_code;
		scb->have_error = true;
		goto good;
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
good:
	ret = 0;
advance:
	*_bp = (const void *)*_bp + sizeof(*xdr);
	return ret;

bad:
	xdr_dump_bad(*_bp);
	ret = afs_protocol_error(call, -EBADMSG, afs_eproto_bad_status);
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
static int afs_deliver_fs_fetch_status_vnode(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSCallBack(&bp, call, call->out_scb);
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.FetchStatus operation type
 */
static const struct afs_call_type afs_RXFSFetchStatus_vnode = {
	.name		= "FS.FetchStatus(vnode)",
	.op		= afs_FS_FetchStatus,
	.deliver	= afs_deliver_fs_fetch_status_vnode,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch the status information for a file
 */
int afs_fs_fetch_file_status(struct afs_fs_cursor *fc, struct afs_status_cb *scb,
			     struct afs_volsync *volsync)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_fetch_file_status(fc, scb, volsync);

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSFetchStatus_vnode,
				   16, (21 + 3 + 6) * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = volsync;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);

	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.FetchData
 */
static int afs_deliver_fs_fetch_data(struct afs_call *call)
{
	struct afs_read *req = call->read_request;
	const __be32 *bp;
	unsigned int size;
	int ret;

	_enter("{%u,%zu/%llu}",
	       call->unmarshall, iov_iter_count(call->iter), req->actual_len);

	switch (call->unmarshall) {
	case 0:
		req->actual_len = 0;
		req->index = 0;
		req->offset = req->pos & (PAGE_SIZE - 1);
		call->unmarshall++;
		if (call->operation_ID == FSFETCHDATA64) {
			afs_extract_to_tmp64(call);
		} else {
			call->tmp_u = htonl(0);
			afs_extract_to_tmp(call);
		}
		/* Fall through */

		/* extract the returned data length */
	case 1:
		_debug("extract data length");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		req->actual_len = be64_to_cpu(call->tmp64);
		_debug("DATA length: %llu", req->actual_len);
		req->remain = min(req->len, req->actual_len);
		if (req->remain == 0)
			goto no_more_data;

		call->unmarshall++;

	begin_page:
		ASSERTCMP(req->index, <, req->nr_pages);
		if (req->remain > PAGE_SIZE - req->offset)
			size = PAGE_SIZE - req->offset;
		else
			size = req->remain;
		call->bvec[0].bv_len = size;
		call->bvec[0].bv_offset = req->offset;
		call->bvec[0].bv_page = req->pages[req->index];
		iov_iter_bvec(&call->def_iter, READ, call->bvec, 1, size);
		ASSERTCMP(size, <=, PAGE_SIZE);
		/* Fall through */

		/* extract the returned data */
	case 2:
		_debug("extract data %zu/%llu",
		       iov_iter_count(call->iter), req->remain);

		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;
		req->remain -= call->bvec[0].bv_len;
		req->offset += call->bvec[0].bv_len;
		ASSERTCMP(req->offset, <=, PAGE_SIZE);
		if (req->offset == PAGE_SIZE) {
			req->offset = 0;
			req->index++;
			if (req->remain > 0)
				goto begin_page;
		}

		ASSERTCMP(req->remain, ==, 0);
		if (req->actual_len <= req->len)
			goto no_more_data;

		/* Discard any excess data the server gave us */
		afs_extract_discard(call, req->actual_len - req->len);
		call->unmarshall = 3;
		/* Fall through */

	case 3:
		_debug("extract discard %zu/%llu",
		       iov_iter_count(call->iter), req->actual_len - req->len);

		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

	no_more_data:
		call->unmarshall = 4;
		afs_extract_to_buf(call, (21 + 3 + 6) * 4);
		/* Fall through */

		/* extract the metadata */
	case 4:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
		if (ret < 0)
			return ret;
		xdr_decode_AFSCallBack(&bp, call, call->out_scb);
		xdr_decode_AFSVolSync(&bp, call->out_volsync);

		req->data_version = call->out_scb->status.data_version;
		req->file_size = call->out_scb->status.size;

		call->unmarshall++;

	case 5:
		break;
	}

	for (; req->index < req->nr_pages; req->index++) {
		if (req->offset < PAGE_SIZE)
			zero_user_segment(req->pages[req->index],
					  req->offset, PAGE_SIZE);
		req->offset = 0;
	}

	if (req->page_done)
		for (req->index = 0; req->index < req->nr_pages; req->index++)
			req->page_done(req);

	_leave(" = 0 [done]");
	return 0;
}

static void afs_fetch_data_destructor(struct afs_call *call)
{
	struct afs_read *req = call->read_request;

	afs_put_read(req);
	afs_flat_call_destructor(call);
}

/*
 * FS.FetchData operation type
 */
static const struct afs_call_type afs_RXFSFetchData = {
	.name		= "FS.FetchData",
	.op		= afs_FS_FetchData,
	.deliver	= afs_deliver_fs_fetch_data,
	.destructor	= afs_fetch_data_destructor,
};

static const struct afs_call_type afs_RXFSFetchData64 = {
	.name		= "FS.FetchData64",
	.op		= afs_FS_FetchData64,
	.deliver	= afs_deliver_fs_fetch_data,
	.destructor	= afs_fetch_data_destructor,
};

/*
 * fetch data from a very large file
 */
static int afs_fs_fetch_data64(struct afs_fs_cursor *fc,
			       struct afs_status_cb *scb,
			       struct afs_read *req)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSFetchData64, 32, (21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = NULL;
	call->read_request = afs_get_read(req);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHDATA64);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);
	bp[4] = htonl(upper_32_bits(req->pos));
	bp[5] = htonl(lower_32_bits(req->pos));
	bp[6] = 0;
	bp[7] = htonl(lower_32_bits(req->len));

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * fetch data from a file
 */
int afs_fs_fetch_data(struct afs_fs_cursor *fc,
		      struct afs_status_cb *scb,
		      struct afs_read *req)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_fetch_data(fc, scb, req);

	if (upper_32_bits(req->pos) ||
	    upper_32_bits(req->len) ||
	    upper_32_bits(req->pos + req->len))
		return afs_fs_fetch_data64(fc, scb, req);

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSFetchData, 24, (21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = NULL;
	call->read_request = afs_get_read(req);

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHDATA);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);
	bp[4] = htonl(lower_32_bits(req->pos));
	bp[5] = htonl(lower_32_bits(req->len));

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.CreateFile or an FS.MakeDir
 */
static int afs_deliver_fs_create_vnode(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, call->out_fid);
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSCallBack(&bp, call, call->out_scb);
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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

static const struct afs_call_type afs_RXFSMakeDir = {
	.name		= "FS.MakeDir",
	.op		= afs_FS_MakeDir,
	.deliver	= afs_deliver_fs_create_vnode,
	.destructor	= afs_flat_call_destructor,
};

/*
 * create a file or make a directory
 */
int afs_fs_create(struct afs_fs_cursor *fc,
		  const char *name,
		  umode_t mode,
		  struct afs_status_cb *dvnode_scb,
		  struct afs_fid *newfid,
		  struct afs_status_cb *new_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags)){
		if (S_ISDIR(mode))
			return yfs_fs_make_dir(fc, name, mode, dvnode_scb,
					       newfid, new_scb);
		else
			return yfs_fs_create_file(fc, name, mode, dvnode_scb,
						  newfid, new_scb);
	}

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (6 * 4);

	call = afs_alloc_flat_call(
		net, S_ISDIR(mode) ? &afs_RXFSMakeDir : &afs_RXFSCreateFile,
		reqsz, (3 + 21 + 21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_fid = newfid;
	call->out_scb = new_scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(S_ISDIR(mode) ? FSMAKEDIR : FSCREATEFILE);
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
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(dvnode->vfs_inode.i_mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(mode & S_IALLUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to any operation that returns directory status and volume
 * sync.
 */
static int afs_deliver_fs_dir_status_and_vol(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.RemoveDir/FS.RemoveFile operation type
 */
static const struct afs_call_type afs_RXFSRemoveFile = {
	.name		= "FS.RemoveFile",
	.op		= afs_FS_RemoveFile,
	.deliver	= afs_deliver_fs_dir_status_and_vol,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSRemoveDir = {
	.name		= "FS.RemoveDir",
	.op		= afs_FS_RemoveDir,
	.deliver	= afs_deliver_fs_dir_status_and_vol,
	.destructor	= afs_flat_call_destructor,
};

/*
 * remove a file or directory
 */
int afs_fs_remove(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
		  const char *name, bool isdir, struct afs_status_cb *dvnode_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_remove(fc, vnode, name, isdir, dvnode_scb);

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz;

	call = afs_alloc_flat_call(
		net, isdir ? &afs_RXFSRemoveDir : &afs_RXFSRemoveFile,
		reqsz, (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(isdir ? FSREMOVEDIR : FSREMOVEFILE);
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

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.Link
 */
static int afs_deliver_fs_link(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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
int afs_fs_link(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
		const char *name,
		struct afs_status_cb *dvnode_scb,
		struct afs_status_cb *vnode_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_link(fc, vnode, name, dvnode_scb, vnode_scb);

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (3 * 4);

	call = afs_alloc_flat_call(net, &afs_RXFSLink, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_scb = vnode_scb;

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

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &vnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.Symlink
 */
static int afs_deliver_fs_symlink(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, call->out_fid);
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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
int afs_fs_symlink(struct afs_fs_cursor *fc,
		   const char *name,
		   const char *contents,
		   struct afs_status_cb *dvnode_scb,
		   struct afs_fid *newfid,
		   struct afs_status_cb *new_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz, reqsz, padsz, c_namesz, c_padsz;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_symlink(fc, name, contents, dvnode_scb,
				      newfid, new_scb);

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;

	c_namesz = strlen(contents);
	c_padsz = (4 - (c_namesz & 3)) & 3;

	reqsz = (6 * 4) + namesz + padsz + c_namesz + c_padsz + (6 * 4);

	call = afs_alloc_flat_call(net, &afs_RXFSSymlink, reqsz,
				   (3 + 21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_fid = newfid;
	call->out_scb = new_scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSYMLINK);
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
	*bp++ = htonl(c_namesz);
	memcpy(bp, contents, c_namesz);
	bp = (void *) bp + c_namesz;
	if (c_padsz > 0) {
		memset(bp, 0, c_padsz);
		bp = (void *) bp + c_padsz;
	}
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(dvnode->vfs_inode.i_mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(S_IRWXUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.Rename
 */
static int afs_deliver_fs_rename(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* If the two dirs are the same, we have two copies of the same status
	 * report, so we just decode it twice.
	 */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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
int afs_fs_rename(struct afs_fs_cursor *fc,
		  const char *orig_name,
		  struct afs_vnode *new_dvnode,
		  const char *new_name,
		  struct afs_status_cb *orig_dvnode_scb,
		  struct afs_status_cb *new_dvnode_scb)
{
	struct afs_vnode *orig_dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(orig_dvnode);
	size_t reqsz, o_namesz, o_padsz, n_namesz, n_padsz;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_rename(fc, orig_name,
				     new_dvnode, new_name,
				     orig_dvnode_scb,
				     new_dvnode_scb);

	_enter("");

	o_namesz = strlen(orig_name);
	o_padsz = (4 - (o_namesz & 3)) & 3;

	n_namesz = strlen(new_name);
	n_padsz = (4 - (n_namesz & 3)) & 3;

	reqsz = (4 * 4) +
		4 + o_namesz + o_padsz +
		(3 * 4) +
		4 + n_namesz + n_padsz;

	call = afs_alloc_flat_call(net, &afs_RXFSRename, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = orig_dvnode_scb;
	call->out_scb = new_dvnode_scb;

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

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call2(call, &orig_dvnode->fid, orig_name, new_name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.StoreData
 */
static int afs_deliver_fs_store_data(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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
static int afs_fs_store_data64(struct afs_fs_cursor *fc,
			       struct address_space *mapping,
			       pgoff_t first, pgoff_t last,
			       unsigned offset, unsigned to,
			       loff_t size, loff_t pos, loff_t i_size,
			       struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData64,
				   (4 + 6 + 3 * 2) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->mapping = mapping;
	call->first = first;
	call->last = last;
	call->first_offset = offset;
	call->last_to = to;
	call->send_pages = true;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA64);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	*bp++ = htonl(AFS_SET_MTIME); /* mask */
	*bp++ = htonl(vnode->vfs_inode.i_mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = 0; /* unix mode */
	*bp++ = 0; /* segment size */

	*bp++ = htonl(pos >> 32);
	*bp++ = htonl((u32) pos);
	*bp++ = htonl(size >> 32);
	*bp++ = htonl((u32) size);
	*bp++ = htonl(i_size >> 32);
	*bp++ = htonl((u32) i_size);

	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * store a set of pages
 */
int afs_fs_store_data(struct afs_fs_cursor *fc, struct address_space *mapping,
		      pgoff_t first, pgoff_t last,
		      unsigned offset, unsigned to,
		      struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	loff_t size, pos, i_size;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_store_data(fc, mapping, first, last, offset, to, scb);

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	size = (loff_t)to - (loff_t)offset;
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

	if (pos >> 32 || i_size >> 32 || size >> 32 || (pos + size) >> 32)
		return afs_fs_store_data64(fc, mapping, first, last, offset, to,
					   size, pos, i_size, scb);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->mapping = mapping;
	call->first = first;
	call->last = last;
	call->first_offset = offset;
	call->last_to = to;
	call->send_pages = true;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	*bp++ = htonl(AFS_SET_MTIME); /* mask */
	*bp++ = htonl(vnode->vfs_inode.i_mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = 0; /* unix mode */
	*bp++ = 0; /* segment size */

	*bp++ = htonl(pos);
	*bp++ = htonl(size);
	*bp++ = htonl(i_size);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.StoreStatus
 */
static int afs_deliver_fs_store_status(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.StoreStatus operation type
 */
static const struct afs_call_type afs_RXFSStoreStatus = {
	.name		= "FS.StoreStatus",
	.op		= afs_FS_StoreStatus,
	.deliver	= afs_deliver_fs_store_status,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSStoreData_as_Status = {
	.name		= "FS.StoreData",
	.op		= afs_FS_StoreData,
	.deliver	= afs_deliver_fs_store_status,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSStoreData64_as_Status = {
	.name		= "FS.StoreData64",
	.op		= afs_FS_StoreData64,
	.deliver	= afs_deliver_fs_store_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * set the attributes on a very large file, using FS.StoreData rather than
 * FS.StoreStatus so as to alter the file size also
 */
static int afs_fs_setattr_size64(struct afs_fs_cursor *fc, struct iattr *attr,
				 struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData64_as_Status,
				   (4 + 6 + 3 * 2) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA64);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	*bp++ = htonl(attr->ia_size >> 32);	/* position of start of write */
	*bp++ = htonl((u32) attr->ia_size);
	*bp++ = 0;				/* size of write */
	*bp++ = 0;
	*bp++ = htonl(attr->ia_size >> 32);	/* new file length */
	*bp++ = htonl((u32) attr->ia_size);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * set the attributes on a file, using FS.StoreData rather than FS.StoreStatus
 * so as to alter the file size also
 */
static int afs_fs_setattr_size(struct afs_fs_cursor *fc, struct iattr *attr,
			       struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);
	if (attr->ia_size >> 32)
		return afs_fs_setattr_size64(fc, attr, scb);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData_as_Status,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	*bp++ = htonl(attr->ia_size);		/* position of start of write */
	*bp++ = 0;				/* size of write */
	*bp++ = htonl(attr->ia_size);		/* new file length */

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * set the attributes on a file, using FS.StoreData if there's a change in file
 * size, and FS.StoreStatus otherwise
 */
int afs_fs_setattr(struct afs_fs_cursor *fc, struct iattr *attr,
		   struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_setattr(fc, attr, scb);

	if (attr->ia_valid & ATTR_SIZE)
		return afs_fs_setattr_size(fc, attr, scb);

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreStatus,
				   (4 + 6) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTORESTATUS);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.GetVolumeStatus
 */
static int afs_deliver_fs_get_volume_status(struct afs_call *call)
{
	const __be32 *bp;
	char *p;
	u32 size;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->unmarshall++;
		afs_extract_to_buf(call, 12 * 4);
		/* Fall through */

		/* extract the returned status record */
	case 1:
		_debug("extract status");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSFetchVolumeStatus(&bp, call->out_volstatus);
		call->unmarshall++;
		afs_extract_to_tmp(call);
		/* Fall through */

		/* extract the volume name length */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("volname length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_volname_len);
		size = (call->count + 3) & ~3; /* It's padded */
		afs_extract_to_buf(call, size);
		call->unmarshall++;
		/* Fall through */

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
		/* Fall through */

		/* extract the offline message length */
	case 4:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("offline msg length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_offline_msg_len);
		size = (call->count + 3) & ~3; /* It's padded */
		afs_extract_to_buf(call, size);
		call->unmarshall++;
		/* Fall through */

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
		/* Fall through */

		/* extract the message of the day length */
	case 6:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("motd length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_motd_len);
		size = (call->count + 3) & ~3; /* It's padded */
		afs_extract_to_buf(call, size);
		call->unmarshall++;
		/* Fall through */

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
int afs_fs_get_volume_status(struct afs_fs_cursor *fc,
			     struct afs_volume_status *vs)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_get_volume_status(fc, vs);

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSGetVolumeStatus, 2 * 4,
				   max(12 * 4, AFSOPAQUEMAX + 1));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_volstatus = vs;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSGETVOLUMESTATUS);
	bp[1] = htonl(vnode->fid.vid);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.SetLock, FS.ExtendLock or FS.ReleaseLock
 */
static int afs_deliver_fs_xxxx_lock(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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
int afs_fs_set_lock(struct afs_fs_cursor *fc, afs_lock_type_t type,
		    struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_set_lock(fc, type, scb);

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSSetLock, 5 * 4, 6 * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->lvnode = vnode;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSETLOCK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);
	*bp++ = htonl(type);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_calli(call, &vnode->fid, type);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * extend a lock on a file
 */
int afs_fs_extend_lock(struct afs_fs_cursor *fc, struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_extend_lock(fc, scb);

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSExtendLock, 4 * 4, 6 * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->lvnode = vnode;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSEXTENDLOCK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * release a lock on a file
 */
int afs_fs_release_lock(struct afs_fs_cursor *fc, struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_release_lock(fc, scb);

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSReleaseLock, 4 * 4, 6 * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->lvnode = vnode;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSRELEASELOCK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
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

	/* Can't take a ref on server */
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
		/* Fall through */

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
		/* Fall through */

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
 * Probe a fileserver for the capabilities that it supports.  This can
 * return up to 196 words.
 */
struct afs_call *afs_fs_get_capabilities(struct afs_net *net,
					 struct afs_server *server,
					 struct afs_addr_cursor *ac,
					 struct key *key,
					 unsigned int server_index)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSGetCapabilities, 1 * 4, 16 * 4);
	if (!call)
		return ERR_PTR(-ENOMEM);

	call->key = key;
	call->server = afs_get_server(server, afs_server_trace_get_caps);
	call->server_index = server_index;
	call->upgrade = true;
	call->async = true;
	call->max_lifespan = AFS_PROBE_MAX_LIFESPAN;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSGETCAPABILITIES);

	/* Can't take a ref on server */
	trace_afs_make_fs_call(call, NULL);
	afs_make_call(ac, call, GFP_NOFS);
	return call;
}

/*
 * Deliver reply data to an FS.FetchStatus with no vnode.
 */
static int afs_deliver_fs_fetch_status(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSCallBack(&bp, call, call->out_scb);
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

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
 * Fetch the status information for a fid without needing a vnode handle.
 */
int afs_fs_fetch_status(struct afs_fs_cursor *fc,
			struct afs_net *net,
			struct afs_fid *fid,
			struct afs_status_cb *scb,
			struct afs_volsync *volsync)
{
	struct afs_call *call;
	__be32 *bp;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_fetch_status(fc, net, fid, scb, volsync);

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), fid->vid, fid->vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSFetchStatus, 16, (21 + 3 + 6) * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_fid = fid;
	call->out_scb = scb;
	call->out_volsync = volsync;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(fid->vid);
	bp[2] = htonl(fid->vnode);
	bp[3] = htonl(fid->unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to an FS.InlineBulkStatus call
 */
static int afs_deliver_fs_inline_bulk_status(struct afs_call *call)
{
	struct afs_status_cb *scb;
	const __be32 *bp;
	u32 tmp;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;
		/* Fall through */

		/* Extract the file status count and array in two steps */
	case 1:
		_debug("extract status count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		tmp = ntohl(call->tmp);
		_debug("status count: %u/%u", tmp, call->count2);
		if (tmp != call->count2)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_ibulkst_count);

		call->count = 0;
		call->unmarshall++;
	more_counts:
		afs_extract_to_buf(call, 21 * sizeof(__be32));
		/* Fall through */

	case 2:
		_debug("extract status array %u", call->count);
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		scb = &call->out_scb[call->count];
		ret = xdr_decode_AFSFetchStatus(&bp, call, scb);
		if (ret < 0)
			return ret;

		call->count++;
		if (call->count < call->count2)
			goto more_counts;

		call->count = 0;
		call->unmarshall++;
		afs_extract_to_tmp(call);
		/* Fall through */

		/* Extract the callback count and array in two steps */
	case 3:
		_debug("extract CB count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		tmp = ntohl(call->tmp);
		_debug("CB count: %u", tmp);
		if (tmp != call->count2)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_ibulkst_cb_count);
		call->count = 0;
		call->unmarshall++;
	more_cbs:
		afs_extract_to_buf(call, 3 * sizeof(__be32));
		/* Fall through */

	case 4:
		_debug("extract CB array");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		_debug("unmarshall CB array");
		bp = call->buffer;
		scb = &call->out_scb[call->count];
		xdr_decode_AFSCallBack(&bp, call, scb);
		call->count++;
		if (call->count < call->count2)
			goto more_cbs;

		afs_extract_to_buf(call, 6 * sizeof(__be32));
		call->unmarshall++;
		/* Fall through */

	case 5:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSVolSync(&bp, call->out_volsync);

		call->unmarshall++;

	case 6:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.InlineBulkStatus operation type
 */
static const struct afs_call_type afs_RXFSInlineBulkStatus = {
	.name		= "FS.InlineBulkStatus",
	.op		= afs_FS_InlineBulkStatus,
	.deliver	= afs_deliver_fs_inline_bulk_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the status information for up to 50 files
 */
int afs_fs_inline_bulk_status(struct afs_fs_cursor *fc,
			      struct afs_net *net,
			      struct afs_fid *fids,
			      struct afs_status_cb *statuses,
			      unsigned int nr_fids,
			      struct afs_volsync *volsync)
{
	struct afs_call *call;
	__be32 *bp;
	int i;

	if (test_bit(AFS_SERVER_FL_IS_YFS, &fc->cbi->server->flags))
		return yfs_fs_inline_bulk_status(fc, net, fids, statuses,
						 nr_fids, volsync);

	_enter(",%x,{%llx:%llu},%u",
	       key_serial(fc->key), fids[0].vid, fids[1].vnode, nr_fids);

	call = afs_alloc_flat_call(net, &afs_RXFSInlineBulkStatus,
				   (2 + nr_fids * 3) * 4,
				   21 * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_scb = statuses;
	call->out_volsync = volsync;
	call->count2 = nr_fids;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSINLINEBULKSTATUS);
	*bp++ = htonl(nr_fids);
	for (i = 0; i < nr_fids; i++) {
		*bp++ = htonl(fids[i].vid);
		*bp++ = htonl(fids[i].vnode);
		*bp++ = htonl(fids[i].unique);
	}

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &fids[0]);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * deliver reply data to an FS.FetchACL
 */
static int afs_deliver_fs_fetch_acl(struct afs_call *call)
{
	struct afs_acl *acl;
	const __be32 *bp;
	unsigned int size;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;
		/* Fall through */

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
		call->ret_acl = acl;
		acl->size = call->count2;
		afs_extract_begin(call, acl->data, size);
		call->unmarshall++;
		/* Fall through */

		/* extract the returned data */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		afs_extract_to_buf(call, (21 + 6) * 4);
		call->unmarshall++;
		/* Fall through */

		/* extract the metadata */
	case 3:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
		if (ret < 0)
			return ret;
		xdr_decode_AFSVolSync(&bp, call->out_volsync);

		call->unmarshall++;

	case 4:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

static void afs_destroy_fs_fetch_acl(struct afs_call *call)
{
	kfree(call->ret_acl);
	afs_flat_call_destructor(call);
}

/*
 * FS.FetchACL operation type
 */
static const struct afs_call_type afs_RXFSFetchACL = {
	.name		= "FS.FetchACL",
	.op		= afs_FS_FetchACL,
	.deliver	= afs_deliver_fs_fetch_acl,
	.destructor	= afs_destroy_fs_fetch_acl,
};

/*
 * Fetch the ACL for a file.
 */
struct afs_acl *afs_fs_fetch_acl(struct afs_fs_cursor *fc,
				 struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSFetchACL, 16, (21 + 6) * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return ERR_PTR(-ENOMEM);
	}

	call->key = fc->key;
	call->ret_acl = NULL;
	call->out_scb = scb;
	call->out_volsync = NULL;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHACL);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_make_call(&fc->ac, call, GFP_KERNEL);
	return (struct afs_acl *)afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to any operation that returns file status and volume
 * sync.
 */
static int afs_deliver_fs_file_status_and_vol(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	ret = xdr_decode_AFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_AFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
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
int afs_fs_store_acl(struct afs_fs_cursor *fc, const struct afs_acl *acl,
		     struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t size;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	size = round_up(acl->size, 4);
	call = afs_alloc_flat_call(net, &afs_RXFSStoreACL,
				   5 * 4 + size, (21 + 6) * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = NULL;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSSTOREACL);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);
	bp[4] = htonl(acl->size);
	memcpy(&bp[5], acl->data, acl->size);
	if (acl->size != size)
		memset((void *)&bp[5] + acl->size, 0, size - acl->size);

	trace_afs_make_fs_call(call, &vnode->fid);
	afs_make_call(&fc->ac, call, GFP_KERNEL);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}
