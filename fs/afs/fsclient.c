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
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/circ_buf.h>
#include <linux/iversion.h>
#include "internal.h"
#include "afs_fs.h"
#include "xdr_fs.h"

static const struct afs_fid afs_zero_fid;

/*
 * We need somewhere to discard into in case the server helpfully returns more
 * than we asked for in FS.FetchData{,64}.
 */
static u8 afs_discard_buffer[64];

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
 * Update the core inode struct from a returned status record.
 */
void afs_update_inode_from_status(struct afs_vnode *vnode,
				  struct afs_file_status *status,
				  const afs_dataversion_t *expected_version,
				  u8 flags)
{
	struct timespec64 t;
	umode_t mode;

	t.tv_sec = status->mtime_client;
	t.tv_nsec = 0;
	vnode->vfs_inode.i_ctime = t;
	vnode->vfs_inode.i_mtime = t;
	vnode->vfs_inode.i_atime = t;

	if (flags & (AFS_VNODE_META_CHANGED | AFS_VNODE_NOT_YET_SET)) {
		vnode->vfs_inode.i_uid = make_kuid(&init_user_ns, status->owner);
		vnode->vfs_inode.i_gid = make_kgid(&init_user_ns, status->group);
		set_nlink(&vnode->vfs_inode, status->nlink);

		mode = vnode->vfs_inode.i_mode;
		mode &= ~S_IALLUGO;
		mode |= status->mode;
		barrier();
		vnode->vfs_inode.i_mode = mode;
	}

	if (!(flags & AFS_VNODE_NOT_YET_SET)) {
		if (expected_version &&
		    *expected_version != status->data_version) {
			_debug("vnode modified %llx on {%x:%u} [exp %llx]",
			       (unsigned long long) status->data_version,
			       vnode->fid.vid, vnode->fid.vnode,
			       (unsigned long long) *expected_version);
			vnode->invalid_before = status->data_version;
			if (vnode->status.type == AFS_FTYPE_DIR) {
				if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
					afs_stat_v(vnode, n_inval);
			} else {
				set_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
			}
		} else if (vnode->status.type == AFS_FTYPE_DIR) {
			/* Expected directory change is handled elsewhere so
			 * that we can locally edit the directory and save on a
			 * download.
			 */
			if (test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
				flags &= ~AFS_VNODE_DATA_CHANGED;
		}
	}

	if (flags & (AFS_VNODE_DATA_CHANGED | AFS_VNODE_NOT_YET_SET)) {
		inode_set_iversion_raw(&vnode->vfs_inode, status->data_version);
		i_size_write(&vnode->vfs_inode, status->size);
	}
}

/*
 * decode an AFSFetchStatus block
 */
static int xdr_decode_AFSFetchStatus(struct afs_call *call,
				     const __be32 **_bp,
				     struct afs_file_status *status,
				     struct afs_vnode *vnode,
				     const afs_dataversion_t *expected_version,
				     struct afs_read *read_req)
{
	const struct afs_xdr_AFSFetchStatus *xdr = (const void *)*_bp;
	bool inline_error = (call->operation_ID == afs_FS_InlineBulkStatus);
	u64 data_version, size;
	u32 type, abort_code;
	u8 flags = 0;
	int ret;

	if (vnode)
		write_seqlock(&vnode->cb_lock);

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
			ret = 0;
			goto out;
		}

		pr_warn("Unknown AFSFetchStatus version %u\n", ntohl(xdr->if_version));
		goto bad;
	}

	if (abort_code != 0 && inline_error) {
		status->abort_code = abort_code;
		ret = 0;
		goto out;
	}

	type = ntohl(xdr->type);
	switch (type) {
	case AFS_FTYPE_FILE:
	case AFS_FTYPE_DIR:
	case AFS_FTYPE_SYMLINK:
		if (type != status->type &&
		    vnode &&
		    !test_bit(AFS_VNODE_UNSET, &vnode->flags)) {
			pr_warning("Vnode %x:%x:%x changed type %u to %u\n",
				   vnode->fid.vid,
				   vnode->fid.vnode,
				   vnode->fid.unique,
				   status->type, type);
			goto bad;
		}
		status->type = type;
		break;
	default:
		goto bad;
	}

#define EXTRACT_M(FIELD)					\
	do {							\
		u32 x = ntohl(xdr->FIELD);			\
		if (status->FIELD != x) {			\
			flags |= AFS_VNODE_META_CHANGED;	\
			status->FIELD = x;			\
		}						\
	} while (0)

	EXTRACT_M(nlink);
	EXTRACT_M(author);
	EXTRACT_M(owner);
	EXTRACT_M(caller_access); /* call ticket dependent */
	EXTRACT_M(anon_access);
	EXTRACT_M(mode);
	EXTRACT_M(group);

	status->mtime_client = ntohl(xdr->mtime_client);
	status->mtime_server = ntohl(xdr->mtime_server);
	status->lock_count   = ntohl(xdr->lock_count);

	size  = (u64)ntohl(xdr->size_lo);
	size |= (u64)ntohl(xdr->size_hi) << 32;
	status->size = size;

	data_version  = (u64)ntohl(xdr->data_version_lo);
	data_version |= (u64)ntohl(xdr->data_version_hi) << 32;
	if (data_version != status->data_version) {
		status->data_version = data_version;
		flags |= AFS_VNODE_DATA_CHANGED;
	}

	if (read_req) {
		read_req->data_version = data_version;
		read_req->file_size = size;
	}

	*_bp = (const void *)*_bp + sizeof(*xdr);

	if (vnode) {
		if (test_bit(AFS_VNODE_UNSET, &vnode->flags))
			flags |= AFS_VNODE_NOT_YET_SET;
		afs_update_inode_from_status(vnode, status, expected_version,
					     flags);
	}

	ret = 0;

out:
	if (vnode)
		write_sequnlock(&vnode->cb_lock);
	return ret;

bad:
	xdr_dump_bad(*_bp);
	ret = afs_protocol_error(call, -EBADMSG);
	goto out;
}

/*
 * decode an AFSCallBack block
 */
static void xdr_decode_AFSCallBack(struct afs_call *call,
				   struct afs_vnode *vnode,
				   const __be32 **_bp)
{
	struct afs_cb_interest *old, *cbi = call->cbi;
	const __be32 *bp = *_bp;
	u32 cb_expiry;

	write_seqlock(&vnode->cb_lock);

	if (call->cb_break == afs_cb_break_sum(vnode, cbi)) {
		vnode->cb_version	= ntohl(*bp++);
		cb_expiry		= ntohl(*bp++);
		vnode->cb_type		= ntohl(*bp++);
		vnode->cb_expires_at	= cb_expiry + ktime_get_real_seconds();
		old = vnode->cb_interest;
		if (old != call->cbi) {
			vnode->cb_interest = cbi;
			cbi = old;
		}
		set_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
	} else {
		bp += 3;
	}

	write_sequnlock(&vnode->cb_lock);
	call->cbi = cbi;
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
	*_bp = bp;
}

/*
 * deliver reply data to an FS.FetchStatus
 */
static int afs_deliver_fs_fetch_status_vnode(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	if (xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	xdr_decode_AFSCallBack(call, vnode, &bp);
	if (call->reply[1])
		xdr_decode_AFSVolSync(&bp, call->reply[1]);

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
int afs_fs_fetch_file_status(struct afs_fs_cursor *fc, struct afs_volsync *volsync,
			     bool new_inode)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSFetchStatus_vnode,
				   16, (21 + 3 + 6) * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->reply[0] = vnode;
	call->reply[1] = volsync;
	call->expected_version = new_inode ? 1 : vnode->status.data_version;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);

	call->cb_break = fc->cb_break;
	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.FetchData
 */
static int afs_deliver_fs_fetch_data(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	struct afs_read *req = call->reply[2];
	const __be32 *bp;
	unsigned int size;
	void *buffer;
	int ret;

	_enter("{%u,%zu/%u;%llu/%llu}",
	       call->unmarshall, call->offset, call->count,
	       req->remain, req->actual_len);

	switch (call->unmarshall) {
	case 0:
		req->actual_len = 0;
		call->offset = 0;
		call->unmarshall++;
		if (call->operation_ID != FSFETCHDATA64) {
			call->unmarshall++;
			goto no_msw;
		}

		/* extract the upper part of the returned data length of an
		 * FSFETCHDATA64 op (which should always be 0 using this
		 * client) */
	case 1:
		_debug("extract data length (MSW)");
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		req->actual_len = ntohl(call->tmp);
		req->actual_len <<= 32;
		call->offset = 0;
		call->unmarshall++;

	no_msw:
		/* extract the returned data length */
	case 2:
		_debug("extract data length");
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		req->actual_len |= ntohl(call->tmp);
		_debug("DATA length: %llu", req->actual_len);

		req->remain = req->actual_len;
		call->offset = req->pos & (PAGE_SIZE - 1);
		req->index = 0;
		if (req->actual_len == 0)
			goto no_more_data;
		call->unmarshall++;

	begin_page:
		ASSERTCMP(req->index, <, req->nr_pages);
		if (req->remain > PAGE_SIZE - call->offset)
			size = PAGE_SIZE - call->offset;
		else
			size = req->remain;
		call->count = call->offset + size;
		ASSERTCMP(call->count, <=, PAGE_SIZE);
		req->remain -= size;

		/* extract the returned data */
	case 3:
		_debug("extract data %llu/%llu %zu/%u",
		       req->remain, req->actual_len, call->offset, call->count);

		buffer = kmap(req->pages[req->index]);
		ret = afs_extract_data(call, buffer, call->count, true);
		kunmap(req->pages[req->index]);
		if (ret < 0)
			return ret;
		if (call->offset == PAGE_SIZE) {
			if (req->page_done)
				req->page_done(call, req);
			req->index++;
			if (req->remain > 0) {
				call->offset = 0;
				if (req->index >= req->nr_pages) {
					call->unmarshall = 4;
					goto begin_discard;
				}
				goto begin_page;
			}
		}
		goto no_more_data;

		/* Discard any excess data the server gave us */
	begin_discard:
	case 4:
		size = min_t(loff_t, sizeof(afs_discard_buffer), req->remain);
		call->count = size;
		_debug("extract discard %llu/%llu %zu/%u",
		       req->remain, req->actual_len, call->offset, call->count);

		call->offset = 0;
		ret = afs_extract_data(call, afs_discard_buffer, call->count, true);
		req->remain -= call->offset;
		if (ret < 0)
			return ret;
		if (req->remain > 0)
			goto begin_discard;

	no_more_data:
		call->offset = 0;
		call->unmarshall = 5;

		/* extract the metadata */
	case 5:
		ret = afs_extract_data(call, call->buffer,
				       (21 + 3 + 6) * 4, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		if (xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
					      &vnode->status.data_version, req) < 0)
			return afs_protocol_error(call, -EBADMSG);
		xdr_decode_AFSCallBack(call, vnode, &bp);
		if (call->reply[1])
			xdr_decode_AFSVolSync(&bp, call->reply[1]);

		call->offset = 0;
		call->unmarshall++;

	case 6:
		break;
	}

	for (; req->index < req->nr_pages; req->index++) {
		if (call->count < PAGE_SIZE)
			zero_user_segment(req->pages[req->index],
					  call->count, PAGE_SIZE);
		if (req->page_done)
			req->page_done(call, req);
		call->count = 0;
	}

	_leave(" = 0 [done]");
	return 0;
}

static void afs_fetch_data_destructor(struct afs_call *call)
{
	struct afs_read *req = call->reply[2];

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
static int afs_fs_fetch_data64(struct afs_fs_cursor *fc, struct afs_read *req)
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
	call->reply[0] = vnode;
	call->reply[1] = NULL; /* volsync */
	call->reply[2] = req;
	call->expected_version = vnode->status.data_version;

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

	refcount_inc(&req->usage);
	call->cb_break = fc->cb_break;
	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * fetch data from a file
 */
int afs_fs_fetch_data(struct afs_fs_cursor *fc, struct afs_read *req)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (upper_32_bits(req->pos) ||
	    upper_32_bits(req->len) ||
	    upper_32_bits(req->pos + req->len))
		return afs_fs_fetch_data64(fc, req);

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSFetchData, 24, (21 + 3 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;
	call->reply[1] = NULL; /* volsync */
	call->reply[2] = req;
	call->expected_version = vnode->status.data_version;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHDATA);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);
	bp[4] = htonl(lower_32_bits(req->pos));
	bp[5] = htonl(lower_32_bits(req->len));

	refcount_inc(&req->usage);
	call->cb_break = fc->cb_break;
	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.CreateFile or an FS.MakeDir
 */
static int afs_deliver_fs_create_vnode(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, call->reply[1]);
	if (xdr_decode_AFSFetchStatus(call, &bp, call->reply[2], NULL, NULL, NULL) < 0 ||
	    xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	xdr_decode_AFSCallBack_raw(&bp, call->reply[3]);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

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
		  u64 current_data_version,
		  struct afs_fid *newfid,
		  struct afs_file_status *newstatus,
		  struct afs_callback *newcb)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t namesz, reqsz, padsz;
	__be32 *bp;

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
	call->reply[0] = vnode;
	call->reply[1] = newfid;
	call->reply[2] = newstatus;
	call->reply[3] = newcb;
	call->expected_version = current_data_version + 1;

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
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(vnode->vfs_inode.i_mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(mode & S_IALLUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.RemoveFile or FS.RemoveDir
 */
static int afs_deliver_fs_remove(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	if (xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

	_leave(" = 0 [done]");
	return 0;
}

/*
 * FS.RemoveDir/FS.RemoveFile operation type
 */
static const struct afs_call_type afs_RXFSRemoveFile = {
	.name		= "FS.RemoveFile",
	.op		= afs_FS_RemoveFile,
	.deliver	= afs_deliver_fs_remove,
	.destructor	= afs_flat_call_destructor,
};

static const struct afs_call_type afs_RXFSRemoveDir = {
	.name		= "FS.RemoveDir",
	.op		= afs_FS_RemoveDir,
	.deliver	= afs_deliver_fs_remove,
	.destructor	= afs_flat_call_destructor,
};

/*
 * remove a file or directory
 */
int afs_fs_remove(struct afs_fs_cursor *fc, const char *name, bool isdir,
		  u64 current_data_version)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t namesz, reqsz, padsz;
	__be32 *bp;

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
	call->reply[0] = vnode;
	call->expected_version = current_data_version + 1;

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

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.Link
 */
static int afs_deliver_fs_link(struct afs_call *call)
{
	struct afs_vnode *dvnode = call->reply[0], *vnode = call->reply[1];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	if (xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode, NULL, NULL) < 0 ||
	    xdr_decode_AFSFetchStatus(call, &bp, &dvnode->status, dvnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

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
		const char *name, u64 current_data_version)
{
	struct afs_vnode *dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t namesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	namesz = strlen(name);
	padsz = (4 - (namesz & 3)) & 3;
	reqsz = (5 * 4) + namesz + padsz + (3 * 4);

	call = afs_alloc_flat_call(net, &afs_RXFSLink, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = dvnode;
	call->reply[1] = vnode;
	call->expected_version = current_data_version + 1;

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
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.Symlink
 */
static int afs_deliver_fs_symlink(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFid(&bp, call->reply[1]);
	if (xdr_decode_AFSFetchStatus(call, &bp, call->reply[2], NULL, NULL, NULL) ||
	    xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

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
		   u64 current_data_version,
		   struct afs_fid *newfid,
		   struct afs_file_status *newstatus)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	size_t namesz, reqsz, padsz, c_namesz, c_padsz;
	__be32 *bp;

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
	call->reply[0] = vnode;
	call->reply[1] = newfid;
	call->reply[2] = newstatus;
	call->expected_version = current_data_version + 1;

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
	*bp++ = htonl(AFS_SET_MODE | AFS_SET_MTIME);
	*bp++ = htonl(vnode->vfs_inode.i_mtime.tv_sec); /* mtime */
	*bp++ = 0; /* owner */
	*bp++ = 0; /* group */
	*bp++ = htonl(S_IRWXUGO); /* unix mode */
	*bp++ = 0; /* segment size */

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.Rename
 */
static int afs_deliver_fs_rename(struct afs_call *call)
{
	struct afs_vnode *orig_dvnode = call->reply[0], *new_dvnode = call->reply[1];
	const __be32 *bp;
	int ret;

	_enter("{%u}", call->unmarshall);

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	if (xdr_decode_AFSFetchStatus(call, &bp, &orig_dvnode->status, orig_dvnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	if (new_dvnode != orig_dvnode &&
	    xdr_decode_AFSFetchStatus(call, &bp, &new_dvnode->status, new_dvnode,
				      &call->expected_version_2, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

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
 * create a symbolic link
 */
int afs_fs_rename(struct afs_fs_cursor *fc,
		  const char *orig_name,
		  struct afs_vnode *new_dvnode,
		  const char *new_name,
		  u64 current_orig_data_version,
		  u64 current_new_data_version)
{
	struct afs_vnode *orig_dvnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(orig_dvnode);
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

	call = afs_alloc_flat_call(net, &afs_RXFSRename, reqsz, (21 + 21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = orig_dvnode;
	call->reply[1] = new_dvnode;
	call->expected_version = current_orig_data_version + 1;
	call->expected_version_2 = current_new_data_version + 1;

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
	trace_afs_make_fs_call(call, &orig_dvnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.StoreData
 */
static int afs_deliver_fs_store_data(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	if (xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

	afs_pages_written_back(vnode, call);

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
			       loff_t size, loff_t pos, loff_t i_size)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData64,
				   (4 + 6 + 3 * 2) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->mapping = mapping;
	call->reply[0] = vnode;
	call->first = first;
	call->last = last;
	call->first_offset = offset;
	call->last_to = to;
	call->send_pages = true;
	call->expected_version = vnode->status.data_version + 1;

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
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * store a set of pages
 */
int afs_fs_store_data(struct afs_fs_cursor *fc, struct address_space *mapping,
		      pgoff_t first, pgoff_t last,
		      unsigned offset, unsigned to)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	loff_t size, pos, i_size;
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
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
					   size, pos, i_size);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->mapping = mapping;
	call->reply[0] = vnode;
	call->first = first;
	call->last = last;
	call->first_offset = offset;
	call->last_to = to;
	call->send_pages = true;
	call->expected_version = vnode->status.data_version + 1;

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
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.StoreStatus
 */
static int afs_deliver_fs_store_status(struct afs_call *call)
{
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	if (xdr_decode_AFSFetchStatus(call, &bp, &vnode->status, vnode,
				      &call->expected_version, NULL) < 0)
		return afs_protocol_error(call, -EBADMSG);
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

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
static int afs_fs_setattr_size64(struct afs_fs_cursor *fc, struct iattr *attr)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData64_as_Status,
				   (4 + 6 + 3 * 2) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;
	call->expected_version = vnode->status.data_version + 1;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTOREDATA64);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	*bp++ = 0;				/* position of start of write */
	*bp++ = 0;
	*bp++ = 0;				/* size of write */
	*bp++ = 0;
	*bp++ = htonl(attr->ia_size >> 32);	/* new file length */
	*bp++ = htonl((u32) attr->ia_size);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * set the attributes on a file, using FS.StoreData rather than FS.StoreStatus
 * so as to alter the file size also
 */
static int afs_fs_setattr_size(struct afs_fs_cursor *fc, struct iattr *attr)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	ASSERT(attr->ia_valid & ATTR_SIZE);
	if (attr->ia_size >> 32)
		return afs_fs_setattr_size64(fc, attr);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreData_as_Status,
				   (4 + 6 + 3) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;
	call->expected_version = vnode->status.data_version + 1;

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

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * set the attributes on a file, using FS.StoreData if there's a change in file
 * size, and FS.StoreStatus otherwise
 */
int afs_fs_setattr(struct afs_fs_cursor *fc, struct iattr *attr)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	if (attr->ia_valid & ATTR_SIZE)
		return afs_fs_setattr_size(fc, attr);

	_enter(",%x,{%x:%u},,",
	       key_serial(fc->key), vnode->fid.vid, vnode->fid.vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSStoreStatus,
				   (4 + 6) * 4,
				   (21 + 6) * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;
	call->expected_version = vnode->status.data_version;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSTORESTATUS);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	xdr_encode_AFS_StoreStatus(&bp, attr);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * deliver reply data to an FS.GetVolumeStatus
 */
static int afs_deliver_fs_get_volume_status(struct afs_call *call)
{
	const __be32 *bp;
	char *p;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->unmarshall++;

		/* extract the returned status record */
	case 1:
		_debug("extract status");
		ret = afs_extract_data(call, call->buffer,
				       12 * 4, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		xdr_decode_AFSFetchVolumeStatus(&bp, call->reply[1]);
		call->offset = 0;
		call->unmarshall++;

		/* extract the volume name length */
	case 2:
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("volname length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, -EBADMSG);
		call->offset = 0;
		call->unmarshall++;

		/* extract the volume name */
	case 3:
		_debug("extract volname");
		if (call->count > 0) {
			ret = afs_extract_data(call, call->reply[2],
					       call->count, true);
			if (ret < 0)
				return ret;
		}

		p = call->reply[2];
		p[call->count] = 0;
		_debug("volname '%s'", p);

		call->offset = 0;
		call->unmarshall++;

		/* extract the volume name padding */
		if ((call->count & 3) == 0) {
			call->unmarshall++;
			goto no_volname_padding;
		}
		call->count = 4 - (call->count & 3);

	case 4:
		ret = afs_extract_data(call, call->buffer,
				       call->count, true);
		if (ret < 0)
			return ret;

		call->offset = 0;
		call->unmarshall++;
	no_volname_padding:

		/* extract the offline message length */
	case 5:
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("offline msg length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, -EBADMSG);
		call->offset = 0;
		call->unmarshall++;

		/* extract the offline message */
	case 6:
		_debug("extract offline");
		if (call->count > 0) {
			ret = afs_extract_data(call, call->reply[2],
					       call->count, true);
			if (ret < 0)
				return ret;
		}

		p = call->reply[2];
		p[call->count] = 0;
		_debug("offline '%s'", p);

		call->offset = 0;
		call->unmarshall++;

		/* extract the offline message padding */
		if ((call->count & 3) == 0) {
			call->unmarshall++;
			goto no_offline_padding;
		}
		call->count = 4 - (call->count & 3);

	case 7:
		ret = afs_extract_data(call, call->buffer,
				       call->count, true);
		if (ret < 0)
			return ret;

		call->offset = 0;
		call->unmarshall++;
	no_offline_padding:

		/* extract the message of the day length */
	case 8:
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("motd length: %u", call->count);
		if (call->count >= AFSNAMEMAX)
			return afs_protocol_error(call, -EBADMSG);
		call->offset = 0;
		call->unmarshall++;

		/* extract the message of the day */
	case 9:
		_debug("extract motd");
		if (call->count > 0) {
			ret = afs_extract_data(call, call->reply[2],
					       call->count, true);
			if (ret < 0)
				return ret;
		}

		p = call->reply[2];
		p[call->count] = 0;
		_debug("motd '%s'", p);

		call->offset = 0;
		call->unmarshall++;

		/* extract the message of the day padding */
		call->count = (4 - (call->count & 3)) & 3;

	case 10:
		ret = afs_extract_data(call, call->buffer,
				       call->count, false);
		if (ret < 0)
			return ret;

		call->offset = 0;
		call->unmarshall++;
	case 11:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * destroy an FS.GetVolumeStatus call
 */
static void afs_get_volume_status_call_destructor(struct afs_call *call)
{
	kfree(call->reply[2]);
	call->reply[2] = NULL;
	afs_flat_call_destructor(call);
}

/*
 * FS.GetVolumeStatus operation type
 */
static const struct afs_call_type afs_RXFSGetVolumeStatus = {
	.name		= "FS.GetVolumeStatus",
	.op		= afs_FS_GetVolumeStatus,
	.deliver	= afs_deliver_fs_get_volume_status,
	.destructor	= afs_get_volume_status_call_destructor,
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
	void *tmpbuf;

	_enter("");

	tmpbuf = kmalloc(AFSOPAQUEMAX, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	call = afs_alloc_flat_call(net, &afs_RXFSGetVolumeStatus, 2 * 4, 12 * 4);
	if (!call) {
		kfree(tmpbuf);
		return -ENOMEM;
	}

	call->key = fc->key;
	call->reply[0] = vnode;
	call->reply[1] = vs;
	call->reply[2] = tmpbuf;

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSGETVOLUMESTATUS);
	bp[1] = htonl(vnode->fid.vid);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
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
	/* xdr_decode_AFSVolSync(&bp, call->reply[X]); */

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
	.destructor	= afs_flat_call_destructor,
};

/*
 * FS.ExtendLock operation type
 */
static const struct afs_call_type afs_RXFSExtendLock = {
	.name		= "FS.ExtendLock",
	.op		= afs_FS_ExtendLock,
	.deliver	= afs_deliver_fs_xxxx_lock,
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
int afs_fs_set_lock(struct afs_fs_cursor *fc, afs_lock_type_t type)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSSetLock, 5 * 4, 6 * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSSETLOCK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);
	*bp++ = htonl(type);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * extend a lock on a file
 */
int afs_fs_extend_lock(struct afs_fs_cursor *fc)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSExtendLock, 4 * 4, 6 * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSEXTENDLOCK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * release a lock on a file
 */
int afs_fs_release_lock(struct afs_fs_cursor *fc)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_call *call;
	struct afs_net *net = afs_v2net(vnode);
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSReleaseLock, 4 * 4, 6 * 4);
	if (!call)
		return -ENOMEM;

	call->key = fc->key;
	call->reply[0] = vnode;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSRELEASELOCK);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);

	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &vnode->fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
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
	return afs_make_call(ac, call, GFP_NOFS, false);
}

/*
 * Deliver reply data to an FS.GetCapabilities operation.
 */
static int afs_deliver_fs_get_capabilities(struct afs_call *call)
{
	u32 count;
	int ret;

	_enter("{%u,%zu/%u}", call->unmarshall, call->offset, call->count);

again:
	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->unmarshall++;

		/* Extract the capabilities word count */
	case 1:
		ret = afs_extract_data(call, &call->tmp,
				       1 * sizeof(__be32),
				       true);
		if (ret < 0)
			return ret;

		count = ntohl(call->tmp);

		call->count = count;
		call->count2 = count;
		call->offset = 0;
		call->unmarshall++;

		/* Extract capabilities words */
	case 2:
		count = min(call->count, 16U);
		ret = afs_extract_data(call, call->buffer,
				       count * sizeof(__be32),
				       call->count > 16);
		if (ret < 0)
			return ret;

		/* TODO: Examine capabilities */

		call->count -= count;
		if (call->count > 0)
			goto again;
		call->offset = 0;
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
	.destructor	= afs_flat_call_destructor,
};

/*
 * Probe a fileserver for the capabilities that it supports.  This can
 * return up to 196 words.
 */
int afs_fs_get_capabilities(struct afs_net *net,
			    struct afs_server *server,
			    struct afs_addr_cursor *ac,
			    struct key *key)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXFSGetCapabilities, 1 * 4, 16 * 4);
	if (!call)
		return -ENOMEM;

	call->key = key;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(FSGETCAPABILITIES);

	/* Can't take a ref on server */
	trace_afs_make_fs_call(call, NULL);
	return afs_make_call(ac, call, GFP_NOFS, false);
}

/*
 * Deliver reply data to an FS.FetchStatus with no vnode.
 */
static int afs_deliver_fs_fetch_status(struct afs_call *call)
{
	struct afs_file_status *status = call->reply[1];
	struct afs_callback *callback = call->reply[2];
	struct afs_volsync *volsync = call->reply[3];
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	int ret;

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	/* unmarshall the reply once we've received all of it */
	bp = call->buffer;
	xdr_decode_AFSFetchStatus(call, &bp, status, vnode,
				  &call->expected_version, NULL);
	callback[call->count].version	= ntohl(bp[0]);
	callback[call->count].expiry	= ntohl(bp[1]);
	callback[call->count].type	= ntohl(bp[2]);
	if (vnode)
		xdr_decode_AFSCallBack(call, vnode, &bp);
	else
		bp += 3;
	if (volsync)
		xdr_decode_AFSVolSync(&bp, volsync);

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
			struct afs_file_status *status,
			struct afs_callback *callback,
			struct afs_volsync *volsync)
{
	struct afs_call *call;
	__be32 *bp;

	_enter(",%x,{%x:%u},,",
	       key_serial(fc->key), fid->vid, fid->vnode);

	call = afs_alloc_flat_call(net, &afs_RXFSFetchStatus, 16, (21 + 3 + 6) * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->reply[0] = NULL; /* vnode for fid[0] */
	call->reply[1] = status;
	call->reply[2] = callback;
	call->reply[3] = volsync;
	call->expected_version = 1; /* vnode->status.data_version */

	/* marshall the parameters */
	bp = call->request;
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(fid->vid);
	bp[2] = htonl(fid->vnode);
	bp[3] = htonl(fid->unique);

	call->cb_break = fc->cb_break;
	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, fid);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}

/*
 * Deliver reply data to an FS.InlineBulkStatus call
 */
static int afs_deliver_fs_inline_bulk_status(struct afs_call *call)
{
	struct afs_file_status *statuses;
	struct afs_callback *callbacks;
	struct afs_vnode *vnode = call->reply[0];
	const __be32 *bp;
	u32 tmp;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->unmarshall++;

		/* Extract the file status count and array in two steps */
	case 1:
		_debug("extract status count");
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		tmp = ntohl(call->tmp);
		_debug("status count: %u/%u", tmp, call->count2);
		if (tmp != call->count2)
			return afs_protocol_error(call, -EBADMSG);

		call->count = 0;
		call->unmarshall++;
	more_counts:
		call->offset = 0;

	case 2:
		_debug("extract status array %u", call->count);
		ret = afs_extract_data(call, call->buffer, 21 * 4, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		statuses = call->reply[1];
		if (xdr_decode_AFSFetchStatus(call, &bp, &statuses[call->count],
					      call->count == 0 ? vnode : NULL,
					      NULL, NULL) < 0)
			return afs_protocol_error(call, -EBADMSG);

		call->count++;
		if (call->count < call->count2)
			goto more_counts;

		call->count = 0;
		call->unmarshall++;
		call->offset = 0;

		/* Extract the callback count and array in two steps */
	case 3:
		_debug("extract CB count");
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		tmp = ntohl(call->tmp);
		_debug("CB count: %u", tmp);
		if (tmp != call->count2)
			return afs_protocol_error(call, -EBADMSG);
		call->count = 0;
		call->unmarshall++;
	more_cbs:
		call->offset = 0;

	case 4:
		_debug("extract CB array");
		ret = afs_extract_data(call, call->buffer, 3 * 4, true);
		if (ret < 0)
			return ret;

		_debug("unmarshall CB array");
		bp = call->buffer;
		callbacks = call->reply[2];
		callbacks[call->count].version	= ntohl(bp[0]);
		callbacks[call->count].expiry	= ntohl(bp[1]);
		callbacks[call->count].type	= ntohl(bp[2]);
		statuses = call->reply[1];
		if (call->count == 0 && vnode && statuses[0].abort_code == 0)
			xdr_decode_AFSCallBack(call, vnode, &bp);
		call->count++;
		if (call->count < call->count2)
			goto more_cbs;

		call->offset = 0;
		call->unmarshall++;

	case 5:
		ret = afs_extract_data(call, call->buffer, 6 * 4, false);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		if (call->reply[3])
			xdr_decode_AFSVolSync(&bp, call->reply[3]);

		call->offset = 0;
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
			      struct afs_file_status *statuses,
			      struct afs_callback *callbacks,
			      unsigned int nr_fids,
			      struct afs_volsync *volsync)
{
	struct afs_call *call;
	__be32 *bp;
	int i;

	_enter(",%x,{%x:%u},%u",
	       key_serial(fc->key), fids[0].vid, fids[1].vnode, nr_fids);

	call = afs_alloc_flat_call(net, &afs_RXFSInlineBulkStatus,
				   (2 + nr_fids * 3) * 4,
				   21 * 4);
	if (!call) {
		fc->ac.error = -ENOMEM;
		return -ENOMEM;
	}

	call->key = fc->key;
	call->reply[0] = NULL; /* vnode for fid[0] */
	call->reply[1] = statuses;
	call->reply[2] = callbacks;
	call->reply[3] = volsync;
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

	call->cb_break = fc->cb_break;
	afs_use_fs_server(call, fc->cbi);
	trace_afs_make_fs_call(call, &fids[0]);
	return afs_make_call(&fc->ac, call, GFP_NOFS, false);
}
