// SPDX-License-Identifier: GPL-2.0-or-later
/* handling of writes to regular files and writing back to the server
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/backing-dev.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/netfs.h>
#include <trace/events/netfs.h>
#include "internal.h"

/*
 * completion of write to server
 */
static void afs_pages_written_back(struct afs_vnode *vnode, loff_t start, unsigned int len)
{
	_enter("{%llx:%llu},{%x @%llx}",
	       vnode->fid.vid, vnode->fid.vnode, len, start);

	afs_prune_wb_keys(vnode);
	_leave("");
}

/*
 * Find a key to use for the writeback.  We cached the keys used to author the
 * writes on the vnode.  wreq->netfs_priv2 will contain the last writeback key
 * record used or NULL and we need to start from there if it's set.
 * wreq->netfs_priv will be set to the key itself or NULL.
 */
static void afs_get_writeback_key(struct netfs_io_request *wreq)
{
	struct afs_wb_key *wbk, *old = wreq->netfs_priv2;
	struct afs_vnode *vnode = AFS_FS_I(wreq->inode);

	key_put(wreq->netfs_priv);
	wreq->netfs_priv = NULL;
	wreq->netfs_priv2 = NULL;

	spin_lock(&vnode->wb_lock);
	if (old)
		wbk = list_next_entry(old, vnode_link);
	else
		wbk = list_first_entry(&vnode->wb_keys, struct afs_wb_key, vnode_link);

	list_for_each_entry_from(wbk, &vnode->wb_keys, vnode_link) {
		_debug("wbk %u", key_serial(wbk->key));
		if (key_validate(wbk->key) == 0) {
			refcount_inc(&wbk->usage);
			wreq->netfs_priv = key_get(wbk->key);
			wreq->netfs_priv2 = wbk;
			_debug("USE WB KEY %u", key_serial(wbk->key));
			break;
		}
	}

	spin_unlock(&vnode->wb_lock);

	afs_put_wb_key(old);
}

static void afs_store_data_success(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	op->ctime = op->file[0].scb.status.mtime_client;
	afs_vnode_commit_status(op, &op->file[0]);
	if (!afs_op_error(op)) {
		afs_pages_written_back(vnode, op->store.pos, op->store.size);
		afs_stat_v(vnode, n_stores);
		atomic_long_add(op->store.size, &afs_v2net(vnode)->n_store_bytes);
	}
}

static const struct afs_operation_ops afs_store_data_operation = {
	.issue_afs_rpc	= afs_fs_store_data,
	.issue_yfs_rpc	= yfs_fs_store_data,
	.success	= afs_store_data_success,
};

/*
 * Prepare a subrequest to write to the server.  This sets the max_len
 * parameter.
 */
void afs_prepare_write(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_stream *stream = &subreq->rreq->io_streams[subreq->stream_nr];

	//if (test_bit(NETFS_SREQ_RETRYING, &subreq->flags))
	//	subreq->max_len = 512 * 1024;
	//else
	stream->sreq_max_len = 256 * 1024 * 1024;
}

/*
 * Issue a subrequest to write to the server.
 */
static void afs_issue_write_worker(struct work_struct *work)
{
	struct netfs_io_subrequest *subreq = container_of(work, struct netfs_io_subrequest, work);
	struct netfs_io_request *wreq = subreq->rreq;
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(wreq->inode);
	unsigned long long pos = subreq->start + subreq->transferred;
	size_t len = subreq->len - subreq->transferred;
	int ret = -ENOKEY;

	_enter("R=%x[%x],%s{%llx:%llu.%u},%llx,%zx",
	       wreq->debug_id, subreq->debug_index,
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       pos, len);

#if 0 // Error injection
	if (subreq->debug_index == 3)
		return netfs_write_subrequest_terminated(subreq, -ENOANO, false);

	if (!subreq->retry_count) {
		set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
		return netfs_write_subrequest_terminated(subreq, -EAGAIN, false);
	}
#endif

	op = afs_alloc_operation(wreq->netfs_priv, vnode->volume);
	if (IS_ERR(op))
		return netfs_write_subrequest_terminated(subreq, -EAGAIN, false);

	afs_op_set_vnode(op, 0, vnode);
	op->file[0].dv_delta	= 1;
	op->file[0].modification = true;
	op->store.pos		= pos;
	op->store.size		= len;
	op->flags		|= AFS_OPERATION_UNINTR;
	op->ops			= &afs_store_data_operation;

	afs_begin_vnode_operation(op);

	op->store.write_iter	= &subreq->io_iter;
	op->store.i_size	= umax(pos + len, vnode->netfs.remote_i_size);
	op->mtime		= inode_get_mtime(&vnode->netfs.inode);

	afs_wait_for_operation(op);
	ret = afs_put_operation(op);
	switch (ret) {
	case 0:
		__set_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags);
		break;
	case -EACCES:
	case -EPERM:
	case -ENOKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
		/* If there are more keys we can try, use the retry algorithm
		 * to rotate the keys.
		 */
		if (wreq->netfs_priv2)
			set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
		break;
	}

	netfs_write_subrequest_terminated(subreq, ret < 0 ? ret : subreq->len, false);
}

void afs_issue_write(struct netfs_io_subrequest *subreq)
{
	subreq->work.func = afs_issue_write_worker;
	if (!queue_work(system_unbound_wq, &subreq->work))
		WARN_ON_ONCE(1);
}

/*
 * Writeback calls this when it finds a folio that needs uploading.  This isn't
 * called if writeback only has copy-to-cache to deal with.
 */
void afs_begin_writeback(struct netfs_io_request *wreq)
{
	afs_get_writeback_key(wreq);
	wreq->io_streams[0].avail = true;
}

/*
 * Prepare to retry the writes in request.  Use this to try rotating the
 * available writeback keys.
 */
void afs_retry_request(struct netfs_io_request *wreq, struct netfs_io_stream *stream)
{
	struct netfs_io_subrequest *subreq =
		list_first_entry(&stream->subrequests,
				 struct netfs_io_subrequest, rreq_link);

	switch (subreq->error) {
	case -EACCES:
	case -EPERM:
	case -ENOKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
		afs_get_writeback_key(wreq);
		if (!wreq->netfs_priv)
			stream->failed = true;
		break;
	}
}

/*
 * write some of the pending data back to the server
 */
int afs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct afs_vnode *vnode = AFS_FS_I(mapping->host);
	int ret;

	/* We have to be careful as we can end up racing with setattr()
	 * truncating the pagecache since the caller doesn't take a lock here
	 * to prevent it.
	 */
	if (wbc->sync_mode == WB_SYNC_ALL)
		down_read(&vnode->validate_lock);
	else if (!down_read_trylock(&vnode->validate_lock))
		return 0;

	ret = netfs_writepages(mapping, wbc);
	up_read(&vnode->validate_lock);
	return ret;
}

/*
 * flush any dirty pages for this process, and check for write errors.
 * - the return status from this call provides a reliable indication of
 *   whether any write errors occurred for this process.
 */
int afs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	struct afs_file *af = file->private_data;
	int ret;

	_enter("{%llx:%llu},{n=%pD},%d",
	       vnode->fid.vid, vnode->fid.vnode, file,
	       datasync);

	ret = afs_validate(vnode, af->key);
	if (ret < 0)
		return ret;

	return file_write_and_wait_range(file, start, end);
}

/*
 * notification that a previously read-only page is about to become writable
 * - if it returns an error, the caller will deliver a bus error signal
 */
vm_fault_t afs_page_mkwrite(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;

	if (afs_validate(AFS_FS_I(file_inode(file)), afs_file_key(file)) < 0)
		return VM_FAULT_SIGBUS;
	return netfs_page_mkwrite(vmf, NULL);
}

/*
 * Prune the keys cached for writeback.  The caller must hold vnode->wb_lock.
 */
void afs_prune_wb_keys(struct afs_vnode *vnode)
{
	LIST_HEAD(graveyard);
	struct afs_wb_key *wbk, *tmp;

	/* Discard unused keys */
	spin_lock(&vnode->wb_lock);

	if (!mapping_tagged(&vnode->netfs.inode.i_data, PAGECACHE_TAG_WRITEBACK) &&
	    !mapping_tagged(&vnode->netfs.inode.i_data, PAGECACHE_TAG_DIRTY)) {
		list_for_each_entry_safe(wbk, tmp, &vnode->wb_keys, vnode_link) {
			if (refcount_read(&wbk->usage) == 1)
				list_move(&wbk->vnode_link, &graveyard);
		}
	}

	spin_unlock(&vnode->wb_lock);

	while (!list_empty(&graveyard)) {
		wbk = list_entry(graveyard.next, struct afs_wb_key, vnode_link);
		list_del(&wbk->vnode_link);
		afs_put_wb_key(wbk);
	}
}
