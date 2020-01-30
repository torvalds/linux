// SPDX-License-Identifier: GPL-2.0-or-later
/* YFS File Server client stubs
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
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

static const struct afs_fid afs_zero_fid;

static inline void afs_use_fs_server(struct afs_call *call, struct afs_cb_interest *cbi)
{
	call->cbi = afs_get_cb_interest(cbi);
}

#define xdr_size(x) (sizeof(*x) / sizeof(__be32))

static void xdr_decode_YFSFid(const __be32 **_bp, struct afs_fid *fid)
{
	const struct yfs_xdr_YFSFid *x = (const void *)*_bp;

	fid->vid	= xdr_to_u64(x->volume);
	fid->vnode	= xdr_to_u64(x->vnode.lo);
	fid->vnode_hi	= ntohl(x->vnode.hi);
	fid->unique	= ntohl(x->vnode.unique);
	*_bp += xdr_size(x);
}

static __be32 *xdr_encode_u32(__be32 *bp, u32 n)
{
	*bp++ = htonl(n);
	return bp;
}

static __be32 *xdr_encode_u64(__be32 *bp, u64 n)
{
	struct yfs_xdr_u64 *x = (void *)bp;

	*x = u64_to_xdr(n);
	return bp + xdr_size(x);
}

static __be32 *xdr_encode_YFSFid(__be32 *bp, struct afs_fid *fid)
{
	struct yfs_xdr_YFSFid *x = (void *)bp;

	x->volume	= u64_to_xdr(fid->vid);
	x->vnode.lo	= u64_to_xdr(fid->vnode);
	x->vnode.hi	= htonl(fid->vnode_hi);
	x->vnode.unique	= htonl(fid->unique);
	return bp + xdr_size(x);
}

static size_t xdr_strlen(unsigned int len)
{
	return sizeof(__be32) + round_up(len, sizeof(__be32));
}

static __be32 *xdr_encode_string(__be32 *bp, const char *p, unsigned int len)
{
	bp = xdr_encode_u32(bp, len);
	bp = memcpy(bp, p, len);
	if (len & 3) {
		unsigned int pad = 4 - (len & 3);

		memset((u8 *)bp + len, 0, pad);
		len += pad;
	}

	return bp + len / sizeof(__be32);
}

static s64 linux_to_yfs_time(const struct timespec64 *t)
{
	/* Convert to 100ns intervals. */
	return (u64)t->tv_sec * 10000000 + t->tv_nsec/100;
}

static __be32 *xdr_encode_YFSStoreStatus_mode(__be32 *bp, mode_t mode)
{
	struct yfs_xdr_YFSStoreStatus *x = (void *)bp;

	x->mask		= htonl(AFS_SET_MODE);
	x->mode		= htonl(mode & S_IALLUGO);
	x->mtime_client	= u64_to_xdr(0);
	x->owner	= u64_to_xdr(0);
	x->group	= u64_to_xdr(0);
	return bp + xdr_size(x);
}

static __be32 *xdr_encode_YFSStoreStatus_mtime(__be32 *bp, const struct timespec64 *t)
{
	struct yfs_xdr_YFSStoreStatus *x = (void *)bp;
	s64 mtime = linux_to_yfs_time(t);

	x->mask		= htonl(AFS_SET_MTIME);
	x->mode		= htonl(0);
	x->mtime_client	= u64_to_xdr(mtime);
	x->owner	= u64_to_xdr(0);
	x->group	= u64_to_xdr(0);
	return bp + xdr_size(x);
}

/*
 * Convert a signed 100ns-resolution 64-bit time into a timespec.
 */
static struct timespec64 yfs_time_to_linux(s64 t)
{
	struct timespec64 ts;
	u64 abs_t;

	/*
	 * Unfortunately can not use normal 64 bit division on 32 bit arch, but
	 * the alternative, do_div, does not work with negative numbers so have
	 * to special case them
	 */
	if (t < 0) {
		abs_t = -t;
		ts.tv_nsec = (time64_t)(do_div(abs_t, 10000000) * 100);
		ts.tv_nsec = -ts.tv_nsec;
		ts.tv_sec = -abs_t;
	} else {
		abs_t = t;
		ts.tv_nsec = (time64_t)do_div(abs_t, 10000000) * 100;
		ts.tv_sec = abs_t;
	}

	return ts;
}

static struct timespec64 xdr_to_time(const struct yfs_xdr_u64 xdr)
{
	s64 t = xdr_to_u64(xdr);

	return yfs_time_to_linux(t);
}

static void yfs_check_req(struct afs_call *call, __be32 *bp)
{
	size_t len = (void *)bp - call->request;

	if (len > call->request_size)
		pr_err("kAFS: %s: Request buffer overflow (%zu>%u)\n",
		       call->type->name, len, call->request_size);
	else if (len < call->request_size)
		pr_warning("kAFS: %s: Request buffer underflow (%zu<%u)\n",
			   call->type->name, len, call->request_size);
}

/*
 * Dump a bad file status record.
 */
static void xdr_dump_bad(const __be32 *bp)
{
	__be32 x[4];
	int i;

	pr_notice("YFS XDR: Bad status record\n");
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
 * Decode a YFSFetchStatus block
 */
static int xdr_decode_YFSFetchStatus(const __be32 **_bp,
				     struct afs_call *call,
				     struct afs_status_cb *scb)
{
	const struct yfs_xdr_YFSFetchStatus *xdr = (const void *)*_bp;
	struct afs_file_status *status = &scb->status;
	u32 type;

	status->abort_code = ntohl(xdr->abort_code);
	if (status->abort_code != 0) {
		if (status->abort_code == VNOVNODE)
			status->nlink = 0;
		scb->have_error = true;
		return 0;
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
	status->author		= xdr_to_u64(xdr->author);
	status->owner		= xdr_to_u64(xdr->owner);
	status->caller_access	= ntohl(xdr->caller_access); /* Ticket dependent */
	status->anon_access	= ntohl(xdr->anon_access);
	status->mode		= ntohl(xdr->mode) & S_IALLUGO;
	status->group		= xdr_to_u64(xdr->group);
	status->lock_count	= ntohl(xdr->lock_count);

	status->mtime_client	= xdr_to_time(xdr->mtime_client);
	status->mtime_server	= xdr_to_time(xdr->mtime_server);
	status->size		= xdr_to_u64(xdr->size);
	status->data_version	= xdr_to_u64(xdr->data_version);
	scb->have_status	= true;

	*_bp += xdr_size(xdr);
	return 0;

bad:
	xdr_dump_bad(*_bp);
	return afs_protocol_error(call, -EBADMSG, afs_eproto_bad_status);
}

/*
 * Decode a YFSCallBack block
 */
static void xdr_decode_YFSCallBack(const __be32 **_bp,
				   struct afs_call *call,
				   struct afs_status_cb *scb)
{
	struct yfs_xdr_YFSCallBack *x = (void *)*_bp;
	struct afs_callback *cb = &scb->callback;
	ktime_t cb_expiry;

	cb_expiry = call->reply_time;
	cb_expiry = ktime_add(cb_expiry, xdr_to_u64(x->expiration_time) * 100);
	cb->expires_at	= ktime_divns(cb_expiry, NSEC_PER_SEC);
	scb->have_cb	= true;
	*_bp += xdr_size(x);
}

/*
 * Decode a YFSVolSync block
 */
static void xdr_decode_YFSVolSync(const __be32 **_bp,
				  struct afs_volsync *volsync)
{
	struct yfs_xdr_YFSVolSync *x = (void *)*_bp;
	u64 creation;

	if (volsync) {
		creation = xdr_to_u64(x->vol_creation_date);
		do_div(creation, 10 * 1000 * 1000);
		volsync->creation = creation;
	}

	*_bp += xdr_size(x);
}

/*
 * Encode the requested attributes into a YFSStoreStatus block
 */
static __be32 *xdr_encode_YFS_StoreStatus(__be32 *bp, struct iattr *attr)
{
	struct yfs_xdr_YFSStoreStatus *x = (void *)bp;
	s64 mtime = 0, owner = 0, group = 0;
	u32 mask = 0, mode = 0;

	mask = 0;
	if (attr->ia_valid & ATTR_MTIME) {
		mask |= AFS_SET_MTIME;
		mtime = linux_to_yfs_time(&attr->ia_mtime);
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

	x->mask		= htonl(mask);
	x->mode		= htonl(mode);
	x->mtime_client	= u64_to_xdr(mtime);
	x->owner	= u64_to_xdr(owner);
	x->group	= u64_to_xdr(group);
	return bp + xdr_size(x);
}

/*
 * Decode a YFSFetchVolumeStatus block.
 */
static void xdr_decode_YFSFetchVolumeStatus(const __be32 **_bp,
					    struct afs_volume_status *vs)
{
	const struct yfs_xdr_YFSFetchVolumeStatus *x = (const void *)*_bp;
	u32 flags;

	vs->vid			= xdr_to_u64(x->vid);
	vs->parent_id		= xdr_to_u64(x->parent_id);
	flags			= ntohl(x->flags);
	vs->online		= flags & yfs_FVSOnline;
	vs->in_service		= flags & yfs_FVSInservice;
	vs->blessed		= flags & yfs_FVSBlessed;
	vs->needs_salvage	= flags & yfs_FVSNeedsSalvage;
	vs->type		= ntohl(x->type);
	vs->min_quota		= 0;
	vs->max_quota		= xdr_to_u64(x->max_quota);
	vs->blocks_in_use	= xdr_to_u64(x->blocks_in_use);
	vs->part_blocks_avail	= xdr_to_u64(x->part_blocks_avail);
	vs->part_max_blocks	= xdr_to_u64(x->part_max_blocks);
	vs->vol_copy_date	= xdr_to_u64(x->vol_copy_date);
	vs->vol_backup_date	= xdr_to_u64(x->vol_backup_date);
	*_bp += sizeof(*x) / sizeof(__be32);
}

/*
 * Deliver a reply that's a status, callback and volsync.
 */
static int yfs_deliver_fs_status_cb_and_volsync(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_YFSCallBack(&bp, call, call->out_scb);
	xdr_decode_YFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * Deliver reply data to operations that just return a file status and a volume
 * sync record.
 */
static int yfs_deliver_status_and_volsync(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	xdr_decode_YFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * YFS.FetchStatus operation type
 */
static const struct afs_call_type yfs_RXYFSFetchStatus_vnode = {
	.name		= "YFS.FetchStatus(vnode)",
	.op		= yfs_FS_FetchStatus,
	.deliver	= yfs_deliver_fs_status_cb_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the status information for a file.
 */
int yfs_fs_fetch_file_status(struct afs_fs_cursor *fc, struct afs_status_cb *scb,
			     struct afs_volsync *volsync)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &yfs_RXYFSFetchStatus_vnode,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSCallBack) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = volsync;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSFETCHSTATUS);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to an YFS.FetchData64.
 */
static int yfs_deliver_fs_fetch_data64(struct afs_call *call)
{
	struct afs_read *req = call->read_request;
	const __be32 *bp;
	unsigned int size;
	int ret;

	_enter("{%u,%zu/%llu}",
	       call->unmarshall, iov_iter_count(&call->iter), req->actual_len);

	switch (call->unmarshall) {
	case 0:
		req->actual_len = 0;
		req->index = 0;
		req->offset = req->pos & (PAGE_SIZE - 1);
		afs_extract_to_tmp64(call);
		call->unmarshall++;
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
		iov_iter_bvec(&call->iter, READ, call->bvec, 1, size);
		ASSERTCMP(size, <=, PAGE_SIZE);
		/* Fall through */

		/* extract the returned data */
	case 2:
		_debug("extract data %zu/%llu",
		       iov_iter_count(&call->iter), req->remain);

		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;
		req->remain -= call->bvec[0].bv_len;
		req->offset += call->bvec[0].bv_len;
		ASSERTCMP(req->offset, <=, PAGE_SIZE);
		if (req->offset == PAGE_SIZE) {
			req->offset = 0;
			if (req->page_done)
				req->page_done(req);
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
		       iov_iter_count(&call->iter), req->actual_len - req->len);

		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

	no_more_data:
		call->unmarshall = 4;
		afs_extract_to_buf(call,
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSCallBack) +
				   sizeof(struct yfs_xdr_YFSVolSync));
		/* Fall through */

		/* extract the metadata */
	case 4:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
		if (ret < 0)
			return ret;
		xdr_decode_YFSCallBack(&bp, call, call->out_scb);
		xdr_decode_YFSVolSync(&bp, call->out_volsync);

		req->data_version = call->out_scb->status.data_version;
		req->file_size = call->out_scb->status.size;

		call->unmarshall++;
		/* Fall through */

	case 5:
		break;
	}

	for (; req->index < req->nr_pages; req->index++) {
		if (req->offset < PAGE_SIZE)
			zero_user_segment(req->pages[req->index],
					  req->offset, PAGE_SIZE);
		if (req->page_done)
			req->page_done(req);
		req->offset = 0;
	}

	_leave(" = 0 [done]");
	return 0;
}

static void yfs_fetch_data_destructor(struct afs_call *call)
{
	afs_put_read(call->read_request);
	afs_flat_call_destructor(call);
}

/*
 * YFS.FetchData64 operation type
 */
static const struct afs_call_type yfs_RXYFSFetchData64 = {
	.name		= "YFS.FetchData64",
	.op		= yfs_FS_FetchData64,
	.deliver	= yfs_deliver_fs_fetch_data64,
	.destructor	= yfs_fetch_data_destructor,
};

/*
 * Fetch data from a file.
 */
int yfs_fs_fetch_data(struct afs_fs_cursor *fc, struct afs_status_cb *scb,
		      struct afs_read *req)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},%llx,%llx",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode,
	       req->pos, req->len);

	call = afs_alloc_flat_call(net, &yfs_RXYFSFetchData64,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(struct yfs_xdr_u64) * 2,
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSCallBack) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = NULL;
	call->read_request = req;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSFETCHDATA64);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	bp = xdr_encode_u64(bp, req->pos);
	bp = xdr_encode_u64(bp, req->len);
	yfs_check_req(call, bp);

	refcount_inc(&req->usage);
	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data for YFS.CreateFile or YFS.MakeDir.
 */
static int yfs_deliver_fs_create_vnode(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_YFSFid(&bp, call->out_fid);
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_YFSCallBack(&bp, call, call->out_scb);
	xdr_decode_YFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.CreateFile and FS.MakeDir operation type
 */
static const struct afs_call_type afs_RXFSCreateFile = {
	.name		= "YFS.CreateFile",
	.op		= yfs_FS_CreateFile,
	.deliver	= yfs_deliver_fs_create_vnode,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Create a file.
 */
int yfs_fs_create_file(struct afs_fs_cursor *fc,
		       const char *name,
		       umode_t mode,
		       struct afs_status_cb *dvnode_scb,
		       struct afs_fid *newfid,
		       struct afs_status_cb *new_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz, reqsz, rplsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	reqsz = (sizeof(__be32) +
		 sizeof(__be32) +
		 sizeof(struct yfs_xdr_YFSFid) +
		 xdr_strlen(namesz) +
		 sizeof(struct yfs_xdr_YFSStoreStatus) +
		 sizeof(__be32));
	rplsz = (sizeof(struct yfs_xdr_YFSFid) +
		 sizeof(struct yfs_xdr_YFSFetchStatus) +
		 sizeof(struct yfs_xdr_YFSFetchStatus) +
		 sizeof(struct yfs_xdr_YFSCallBack) +
		 sizeof(struct yfs_xdr_YFSVolSync));

	call = afs_alloc_flat_call(net, &afs_RXFSCreateFile, reqsz, rplsz);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_fid = newfid;
	call->out_scb = new_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSCREATEFILE);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &dvnode->fid);
	bp = xdr_encode_string(bp, name, namesz);
	bp = xdr_encode_YFSStoreStatus_mode(bp, mode);
	bp = xdr_encode_u32(bp, yfs_LockNone); /* ViceLockType */
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

static const struct afs_call_type yfs_RXFSMakeDir = {
	.name		= "YFS.MakeDir",
	.op		= yfs_FS_MakeDir,
	.deliver	= yfs_deliver_fs_create_vnode,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Make a directory.
 */
int yfs_fs_make_dir(struct afs_fs_cursor *fc,
		    const char *name,
		    umode_t mode,
		    struct afs_status_cb *dvnode_scb,
		    struct afs_fid *newfid,
		    struct afs_status_cb *new_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz, reqsz, rplsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	reqsz = (sizeof(__be32) +
		 sizeof(struct yfs_xdr_RPCFlags) +
		 sizeof(struct yfs_xdr_YFSFid) +
		 xdr_strlen(namesz) +
		 sizeof(struct yfs_xdr_YFSStoreStatus));
	rplsz = (sizeof(struct yfs_xdr_YFSFid) +
		 sizeof(struct yfs_xdr_YFSFetchStatus) +
		 sizeof(struct yfs_xdr_YFSFetchStatus) +
		 sizeof(struct yfs_xdr_YFSCallBack) +
		 sizeof(struct yfs_xdr_YFSVolSync));

	call = afs_alloc_flat_call(net, &yfs_RXFSMakeDir, reqsz, rplsz);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_fid = newfid;
	call->out_scb = new_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSMAKEDIR);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &dvnode->fid);
	bp = xdr_encode_string(bp, name, namesz);
	bp = xdr_encode_YFSStoreStatus_mode(bp, mode);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to a YFS.RemoveFile2 operation.
 */
static int yfs_deliver_fs_remove_file2(struct afs_call *call)
{
	struct afs_fid fid;
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;

	xdr_decode_YFSFid(&bp, &fid);
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	/* Was deleted if vnode->status.abort_code == VNOVNODE. */

	xdr_decode_YFSVolSync(&bp, call->out_volsync);
	return 0;
}

/*
 * YFS.RemoveFile2 operation type.
 */
static const struct afs_call_type yfs_RXYFSRemoveFile2 = {
	.name		= "YFS.RemoveFile2",
	.op		= yfs_FS_RemoveFile2,
	.deliver	= yfs_deliver_fs_remove_file2,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Remove a file and retrieve new file status.
 */
int yfs_fs_remove_file2(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
			const char *name, struct afs_status_cb *dvnode_scb,
			struct afs_status_cb *vnode_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);

	call = afs_alloc_flat_call(net, &yfs_RXYFSRemoveFile2,
				   sizeof(__be32) +
				   sizeof(struct yfs_xdr_RPCFlags) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   xdr_strlen(namesz),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_scb = vnode_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSREMOVEFILE2);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &dvnode->fid);
	bp = xdr_encode_string(bp, name, namesz);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to a YFS.RemoveFile or YFS.RemoveDir operation.
 */
static int yfs_deliver_fs_remove(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;

	xdr_decode_YFSVolSync(&bp, call->out_volsync);
	return 0;
}

/*
 * FS.RemoveDir and FS.RemoveFile operation types.
 */
static const struct afs_call_type yfs_RXYFSRemoveFile = {
	.name		= "YFS.RemoveFile",
	.op		= yfs_FS_RemoveFile,
	.deliver	= yfs_deliver_fs_remove,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type yfs_RXYFSRemoveDir = {
	.name		= "YFS.RemoveDir",
	.op		= yfs_FS_RemoveDir,
	.deliver	= yfs_deliver_fs_remove,
	.destructor	= afs_flat_call_destructor,
};

/*
 * remove a file or directory
 */
int yfs_fs_remove(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
		  const char *name, bool isdir,
		  struct afs_status_cb *dvnode_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	call = afs_alloc_flat_call(
		net, isdir ? &yfs_RXYFSRemoveDir : &yfs_RXYFSRemoveFile,
		sizeof(__be32) +
		sizeof(struct yfs_xdr_RPCFlags) +
		sizeof(struct yfs_xdr_YFSFid) +
		xdr_strlen(namesz),
		sizeof(struct yfs_xdr_YFSFetchStatus) +
		sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, isdir ? YFSREMOVEDIR : YFSREMOVEFILE);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &dvnode->fid);
	bp = xdr_encode_string(bp, name, namesz);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to a YFS.Link operation.
 */
static int yfs_deliver_fs_link(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_YFSVolSync(&bp, call->out_volsync);
	_leave(" = 0 [done]");
	return 0;
}

/*
 * YFS.Link operation type.
 */
static const struct afs_call_type yfs_RXYFSLink = {
	.name		= "YFS.Link",
	.op		= yfs_FS_Link,
	.deliver	= yfs_deliver_fs_link,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Make a hard link.
 */
int yfs_fs_link(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
		const char *name,
		struct afs_status_cb *dvnode_scb,
		struct afs_status_cb *vnode_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t namesz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	call = afs_alloc_flat_call(net, &yfs_RXYFSLink,
				   sizeof(__be32) +
				   sizeof(struct yfs_xdr_RPCFlags) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   xdr_strlen(namesz) +
				   sizeof(struct yfs_xdr_YFSFid),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_scb = vnode_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSLINK);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &dvnode->fid);
	bp = xdr_encode_string(bp, name, namesz);
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &vnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to a YFS.Symlink operation.
 */
static int yfs_deliver_fs_symlink(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_YFSFid(&bp, call->out_fid);
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
	if (ret < 0)
		return ret;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	xdr_decode_YFSVolSync(&bp, call->out_volsync);

	_leave(" = 0 [done]");
	return 0;
}

/*
 * YFS.Symlink operation type
 */
static const struct afs_call_type yfs_RXYFSSymlink = {
	.name		= "YFS.Symlink",
	.op		= yfs_FS_Symlink,
	.deliver	= yfs_deliver_fs_symlink,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Create a symbolic link.
 */
int yfs_fs_symlink(struct afs_fs_cursor *fc,
		   const char *name,
		   const char *contents,
		   struct afs_status_cb *dvnode_scb,
		   struct afs_fid *newfid,
		   struct afs_status_cb *vnode_scb)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(dvnode);
	size_t namesz, contents_sz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	contents_sz = strlen(contents);
	call = afs_alloc_flat_call(net, &yfs_RXYFSSymlink,
				   sizeof(__be32) +
				   sizeof(struct yfs_xdr_RPCFlags) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   xdr_strlen(namesz) +
				   xdr_strlen(contents_sz) +
				   sizeof(struct yfs_xdr_YFSStoreStatus),
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = dvnode_scb;
	call->out_fid = newfid;
	call->out_scb = vnode_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSSYMLINK);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &dvnode->fid);
	bp = xdr_encode_string(bp, name, namesz);
	bp = xdr_encode_string(bp, contents, contents_sz);
	bp = xdr_encode_YFSStoreStatus_mode(bp, S_IRWXUGO);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call1(call, &dvnode->fid, name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to a YFS.Rename operation.
 */
static int yfs_deliver_fs_rename(struct afs_call *call)
{
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	bp = call->buffer;
	ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_dir_scb);
	if (ret < 0)
		return ret;
	if (call->out_dir_scb != call->out_scb) {
		ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
		if (ret < 0)
			return ret;
	}

	xdr_decode_YFSVolSync(&bp, call->out_volsync);
	_leave(" = 0 [done]");
	return 0;
}

/*
 * YFS.Rename operation type
 */
static const struct afs_call_type yfs_RXYFSRename = {
	.name		= "FS.Rename",
	.op		= yfs_FS_Rename,
	.deliver	= yfs_deliver_fs_rename,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Rename a file or directory.
 */
int yfs_fs_rename(struct afs_fs_cursor *fc,
		  const char *orig_name,
		  struct afs_vnode *new_dvnode,
		  const char *new_name,
		  struct afs_status_cb *orig_dvnode_scb,
		  struct afs_status_cb *new_dvnode_scb)
{
	struct afs_vnode *orig_dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(orig_dvnode);
	size_t o_namesz, n_namesz;
	__be32 *bp;

	_enter("");

	o_namesz = strlen(orig_name);
	n_namesz = strlen(new_name);
	call = afs_alloc_flat_call(net, &yfs_RXYFSRename,
				   sizeof(__be32) +
				   sizeof(struct yfs_xdr_RPCFlags) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   xdr_strlen(o_namesz) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   xdr_strlen(n_namesz),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_dir_scb = orig_dvnode_scb;
	call->out_scb = new_dvnode_scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSRENAME);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &orig_dvnode->fid);
	bp = xdr_encode_string(bp, orig_name, o_namesz);
	bp = xdr_encode_YFSFid(bp, &new_dvnode->fid);
	bp = xdr_encode_string(bp, new_name, n_namesz);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call2(call, &orig_dvnode->fid, orig_name, new_name);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * YFS.StoreData64 operation type.
 */
static const struct afs_call_type yfs_RXYFSStoreData64 = {
	.name		= "YFS.StoreData64",
	.op		= yfs_FS_StoreData64,
	.deliver	= yfs_deliver_status_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Store a set of pages to a large file.
 */
int yfs_fs_store_data(struct afs_fs_cursor *fc, struct address_space *mapping,
		      pgoff_t first, pgoff_t last,
		      unsigned offset, unsigned to,
		      struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	loff_t size, pos, i_size;
	__be32 *bp;

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
	       (unsigned long long)size, (unsigned long long)pos,
	       (unsigned long long)i_size);

	call = afs_alloc_flat_call(net, &yfs_RXYFSStoreData64,
				   sizeof(__be32) +
				   sizeof(__be32) +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(struct yfs_xdr_YFSStoreStatus) +
				   sizeof(struct yfs_xdr_u64) * 3,
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
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
	bp = xdr_encode_u32(bp, YFSSTOREDATA64);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	bp = xdr_encode_YFSStoreStatus_mtime(bp, &vnode->vfs_inode.i_mtime);
	bp = xdr_encode_u64(bp, pos);
	bp = xdr_encode_u64(bp, size);
	bp = xdr_encode_u64(bp, i_size);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * YFS.StoreStatus operation type
 */
static const struct afs_call_type yfs_RXYFSStoreStatus = {
	.name		= "YFS.StoreStatus",
	.op		= yfs_FS_StoreStatus,
	.deliver	= yfs_deliver_status_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type yfs_RXYFSStoreData64_as_Status = {
	.name		= "YFS.StoreData64",
	.op		= yfs_FS_StoreData64,
	.deliver	= yfs_deliver_status_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Set the attributes on a file, using YFS.StoreData64 rather than
 * YFS.StoreStatus so as to alter the file size also.
 */
static int yfs_fs_setattr_size(struct afs_fs_cursor *fc, struct iattr *attr,
			       struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &yfs_RXYFSStoreData64_as_Status,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(struct yfs_xdr_YFSStoreStatus) +
				   sizeof(struct yfs_xdr_u64) * 3,
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSSTOREDATA64);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	bp = xdr_encode_YFS_StoreStatus(bp, attr);
	bp = xdr_encode_u64(bp, attr->ia_size);	/* position of start of write */
	bp = xdr_encode_u64(bp, 0);		/* size of write */
	bp = xdr_encode_u64(bp, attr->ia_size);	/* new file length */
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Set the attributes on a file, using YFS.StoreData64 if there's a change in
 * file size, and YFS.StoreStatus otherwise.
 */
int yfs_fs_setattr(struct afs_fs_cursor *fc, struct iattr *attr,
		   struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (attr->ia_valid & ATTR_SIZE)
		return yfs_fs_setattr_size(fc, attr, scb);

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &yfs_RXYFSStoreStatus,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(struct yfs_xdr_YFSStoreStatus),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSSTORESTATUS);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	bp = xdr_encode_YFS_StoreStatus(bp, attr);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to a YFS.GetVolumeStatus operation.
 */
static int yfs_deliver_fs_get_volume_status(struct afs_call *call)
{
	const __be32 *bp;
	char *p;
	u32 size;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->unmarshall++;
		afs_extract_to_buf(call, sizeof(struct yfs_xdr_YFSFetchVolumeStatus));
		/* Fall through */

		/* extract the returned status record */
	case 1:
		_debug("extract status");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_YFSFetchVolumeStatus(&bp, call->out_volstatus);
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
		/* Fall through */

	case 8:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * YFS.GetVolumeStatus operation type
 */
static const struct afs_call_type yfs_RXYFSGetVolumeStatus = {
	.name		= "YFS.GetVolumeStatus",
	.op		= yfs_FS_GetVolumeStatus,
	.deliver	= yfs_deliver_fs_get_volume_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * fetch the status of a volume
 */
int yfs_fs_get_volume_status(struct afs_fs_cursor *fc,
			     struct afs_volume_status *vs)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &yfs_RXYFSGetVolumeStatus,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_u64),
				   max_t(size_t,
					 sizeof(struct yfs_xdr_YFSFetchVolumeStatus) +
					 sizeof(__be32),
					 AFSOPAQUEMAX + 1));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->out_volstatus = vs;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSGETVOLUMESTATUS);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_u64(bp, vnode->fid.vid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * YFS.SetLock operation type
 */
static const struct afs_call_type yfs_RXYFSSetLock = {
	.name		= "YFS.SetLock",
	.op		= yfs_FS_SetLock,
	.deliver	= yfs_deliver_status_and_volsync,
	.done		= afs_lock_op_done,
	.destructor	= afs_flat_call_destructor,
};

/*
 * YFS.ExtendLock operation type
 */
static const struct afs_call_type yfs_RXYFSExtendLock = {
	.name		= "YFS.ExtendLock",
	.op		= yfs_FS_ExtendLock,
	.deliver	= yfs_deliver_status_and_volsync,
	.done		= afs_lock_op_done,
	.destructor	= afs_flat_call_destructor,
};

/*
 * YFS.ReleaseLock operation type
 */
static const struct afs_call_type yfs_RXYFSReleaseLock = {
	.name		= "YFS.ReleaseLock",
	.op		= yfs_FS_ReleaseLock,
	.deliver	= yfs_deliver_status_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Set a lock on a file
 */
int yfs_fs_set_lock(struct afs_fs_cursor *fc, afs_lock_type_t type,
		    struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &yfs_RXYFSSetLock,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(__be32),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->lvnode = vnode;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSSETLOCK);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	bp = xdr_encode_u32(bp, type);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_calli(call, &vnode->fid, type);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * extend a lock on a file
 */
int yfs_fs_extend_lock(struct afs_fs_cursor *fc, struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &yfs_RXYFSExtendLock,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->lvnode = vnode;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSEXTENDLOCK);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * release a lock on a file
 */
int yfs_fs_release_lock(struct afs_fs_cursor *fc, struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &yfs_RXYFSReleaseLock,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->lvnode = vnode;
	call->out_scb = scb;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSRELEASELOCK);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * YFS.FetchStatus operation type
 */
static const struct afs_call_type yfs_RXYFSFetchStatus = {
	.name		= "YFS.FetchStatus",
	.op		= yfs_FS_FetchStatus,
	.deliver	= yfs_deliver_fs_status_cb_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the status information for a fid without needing a vnode handle.
 */
int yfs_fs_fetch_status(struct afs_fs_cursor *fc,
			struct afs_net *net,
			struct afs_fid *fid,
			struct afs_status_cb *scb,
			struct afs_volsync *volsync)
{
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), fid->vid, fid->vnode);

	call = afs_alloc_flat_call(net, &yfs_RXYFSFetchStatus,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid),
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSCallBack) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = volsync;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSFETCHSTATUS);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, fid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, fid);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to an YFS.InlineBulkStatus call
 */
static int yfs_deliver_fs_inline_bulk_status(struct afs_call *call)
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
		afs_extract_to_buf(call, sizeof(struct yfs_xdr_YFSFetchStatus));
		/* Fall through */

	case 2:
		_debug("extract status array %u", call->count);
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		scb = &call->out_scb[call->count];
		ret = xdr_decode_YFSFetchStatus(&bp, call, scb);
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
		afs_extract_to_buf(call, sizeof(struct yfs_xdr_YFSCallBack));
		/* Fall through */

	case 4:
		_debug("extract CB array");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		_debug("unmarshall CB array");
		bp = call->buffer;
		scb = &call->out_scb[call->count];
		xdr_decode_YFSCallBack(&bp, call, scb);
		call->count++;
		if (call->count < call->count2)
			goto more_cbs;

		afs_extract_to_buf(call, sizeof(struct yfs_xdr_YFSVolSync));
		call->unmarshall++;
		/* Fall through */

	case 5:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_YFSVolSync(&bp, call->out_volsync);

		call->unmarshall++;
		/* Fall through */

	case 6:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.InlineBulkStatus operation type
 */
static const struct afs_call_type yfs_RXYFSInlineBulkStatus = {
	.name		= "YFS.InlineBulkStatus",
	.op		= yfs_FS_InlineBulkStatus,
	.deliver	= yfs_deliver_fs_inline_bulk_status,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the status information for up to 1024 files
 */
int yfs_fs_inline_bulk_status(struct afs_fs_cursor *fc,
			      struct afs_net *net,
			      struct afs_fid *fids,
			      struct afs_status_cb *statuses,
			      unsigned int nr_fids,
			      struct afs_volsync *volsync)
{
	struct afs_call *call;
	__be32 *bp;
	int i;

	_enter(",%x,{%llx:%llu},%u",
	       key_serial(fc->key), fids[0].vid, fids[1].vnode, nr_fids);

	call = afs_alloc_flat_call(net, &yfs_RXYFSInlineBulkStatus,
				   sizeof(__be32) +
				   sizeof(__be32) +
				   sizeof(__be32) +
				   sizeof(struct yfs_xdr_YFSFid) * nr_fids,
				   sizeof(struct yfs_xdr_YFSFetchStatus));
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
	bp = xdr_encode_u32(bp, YFSINLINEBULKSTATUS);
	bp = xdr_encode_u32(bp, 0); /* RPCFlags */
	bp = xdr_encode_u32(bp, nr_fids);
	for (i = 0; i < nr_fids; i++)
		bp = xdr_encode_YFSFid(bp, &fids[i]);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &fids[0]);
	afs_set_fc_call(call, fc);
	afs_make_call(&fc->ac, call, GFP_NOFS);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * Deliver reply data to an YFS.FetchOpaqueACL.
 */
static int yfs_deliver_fs_fetch_opaque_acl(struct afs_call *call)
{
	struct yfs_acl *yacl = call->out_yacl;
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

		/* Extract the file ACL length */
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		size = call->count2 = ntohl(call->tmp);
		size = round_up(size, 4);

		if (yacl->flags & YFS_ACL_WANT_ACL) {
			acl = kmalloc(struct_size(acl, data, size), GFP_KERNEL);
			if (!acl)
				return -ENOMEM;
			yacl->acl = acl;
			acl->size = call->count2;
			afs_extract_begin(call, acl->data, size);
		} else {
			afs_extract_discard(call, size);
		}
		call->unmarshall++;
		/* Fall through */

		/* Extract the file ACL */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		afs_extract_to_tmp(call);
		call->unmarshall++;
		/* Fall through */

		/* Extract the volume ACL length */
	case 3:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		size = call->count2 = ntohl(call->tmp);
		size = round_up(size, 4);

		if (yacl->flags & YFS_ACL_WANT_VOL_ACL) {
			acl = kmalloc(struct_size(acl, data, size), GFP_KERNEL);
			if (!acl)
				return -ENOMEM;
			yacl->vol_acl = acl;
			acl->size = call->count2;
			afs_extract_begin(call, acl->data, size);
		} else {
			afs_extract_discard(call, size);
		}
		call->unmarshall++;
		/* Fall through */

		/* Extract the volume ACL */
	case 4:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		afs_extract_to_buf(call,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
		call->unmarshall++;
		/* Fall through */

		/* extract the metadata */
	case 5:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		yacl->inherit_flag = ntohl(*bp++);
		yacl->num_cleaned = ntohl(*bp++);
		ret = xdr_decode_YFSFetchStatus(&bp, call, call->out_scb);
		if (ret < 0)
			return ret;
		xdr_decode_YFSVolSync(&bp, call->out_volsync);

		call->unmarshall++;
		/* Fall through */

	case 6:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

void yfs_free_opaque_acl(struct yfs_acl *yacl)
{
	if (yacl) {
		kfree(yacl->acl);
		kfree(yacl->vol_acl);
		kfree(yacl);
	}
}

/*
 * YFS.FetchOpaqueACL operation type
 */
static const struct afs_call_type yfs_RXYFSFetchOpaqueACL = {
	.name		= "YFS.FetchOpaqueACL",
	.op		= yfs_FS_FetchOpaqueACL,
	.deliver	= yfs_deliver_fs_fetch_opaque_acl,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the YFS advanced ACLs for a file.
 */
struct yfs_acl *yfs_fs_fetch_opaque_acl(struct afs_fs_cursor *fc,
					struct yfs_acl *yacl,
					struct afs_status_cb *scb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%llx:%llu},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &yfs_RXYFSFetchOpaqueACL,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid),
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call) {
		fc->ac.error = -ENOMEM;
		return ERR_PTR(-ENOMEM);
	}

	call->key = fc->key;
	call->out_yacl = yacl;
	call->out_scb = scb;
	call->out_volsync = NULL;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSFETCHOPAQUEACL);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	yfs_check_req(call, bp);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	afs_make_call(&fc->ac, call, GFP_KERNEL);
	return (struct yfs_acl *)afs_wait_for_call_to_complete(call, &fc->ac);
}

/*
 * YFS.StoreOpaqueACL2 operation type
 */
static const struct afs_call_type yfs_RXYFSStoreOpaqueACL2 = {
	.name		= "YFS.StoreOpaqueACL2",
	.op		= yfs_FS_StoreOpaqueACL2,
	.deliver	= yfs_deliver_status_and_volsync,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Fetch the YFS ACL for a file.
 */
int yfs_fs_store_opaque_acl2(struct afs_fs_cursor *fc, const struct afs_acl *acl,
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
	call = afs_alloc_flat_call(net, &yfs_RXYFSStoreOpaqueACL2,
				   sizeof(__be32) * 2 +
				   sizeof(struct yfs_xdr_YFSFid) +
				   sizeof(__be32) + size,
				   sizeof(struct yfs_xdr_YFSFetchStatus) +
				   sizeof(struct yfs_xdr_YFSVolSync));
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->out_scb = scb;
	call->out_volsync = NULL;

	/* marshall the parameters */
	bp = call->request;
	bp = xdr_encode_u32(bp, YFSSTOREOPAQUEACL2);
	bp = xdr_encode_u32(bp, 0); /* RPC flags */
	bp = xdr_encode_YFSFid(bp, &vnode->fid);
	bp = xdr_encode_u32(bp, acl->size);
	memcpy(bp, acl->data, acl->size);
	if (acl->size != size)
		memset((void *)bp + acl->size, 0, size - acl->size);
	yfs_check_req(call, bp);

	trace_afs_make_fs_call(call, &vnode->fid);
	afs_make_call(&fc->ac, call, GFP_KERNEL);
	return afs_wait_for_call_to_complete(call, &fc->ac);
}
