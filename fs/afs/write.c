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
static void afs_pages_written_back(struct afs_vanalde *vanalde, loff_t start, unsigned int len)
{
	_enter("{%llx:%llu},{%x @%llx}",
	       vanalde->fid.vid, vanalde->fid.vanalde, len, start);

	afs_prune_wb_keys(vanalde);
	_leave("");
}

/*
 * Find a key to use for the writeback.  We cached the keys used to author the
 * writes on the vanalde.  *_wbk will contain the last writeback key used or NULL
 * and we need to start from there if it's set.
 */
static int afs_get_writeback_key(struct afs_vanalde *vanalde,
				 struct afs_wb_key **_wbk)
{
	struct afs_wb_key *wbk = NULL;
	struct list_head *p;
	int ret = -EANALKEY, ret2;

	spin_lock(&vanalde->wb_lock);
	if (*_wbk)
		p = (*_wbk)->vanalde_link.next;
	else
		p = vanalde->wb_keys.next;

	while (p != &vanalde->wb_keys) {
		wbk = list_entry(p, struct afs_wb_key, vanalde_link);
		_debug("wbk %u", key_serial(wbk->key));
		ret2 = key_validate(wbk->key);
		if (ret2 == 0) {
			refcount_inc(&wbk->usage);
			_debug("USE WB KEY %u", key_serial(wbk->key));
			break;
		}

		wbk = NULL;
		if (ret == -EANALKEY)
			ret = ret2;
		p = p->next;
	}

	spin_unlock(&vanalde->wb_lock);
	if (*_wbk)
		afs_put_wb_key(*_wbk);
	*_wbk = wbk;
	return 0;
}

static void afs_store_data_success(struct afs_operation *op)
{
	struct afs_vanalde *vanalde = op->file[0].vanalde;

	op->ctime = op->file[0].scb.status.mtime_client;
	afs_vanalde_commit_status(op, &op->file[0]);
	if (!afs_op_error(op)) {
		if (!op->store.laundering)
			afs_pages_written_back(vanalde, op->store.pos, op->store.size);
		afs_stat_v(vanalde, n_stores);
		atomic_long_add(op->store.size, &afs_v2net(vanalde)->n_store_bytes);
	}
}

static const struct afs_operation_ops afs_store_data_operation = {
	.issue_afs_rpc	= afs_fs_store_data,
	.issue_yfs_rpc	= yfs_fs_store_data,
	.success	= afs_store_data_success,
};

/*
 * write to a file
 */
static int afs_store_data(struct afs_vanalde *vanalde, struct iov_iter *iter, loff_t pos,
			  bool laundering)
{
	struct afs_operation *op;
	struct afs_wb_key *wbk = NULL;
	loff_t size = iov_iter_count(iter);
	int ret = -EANALKEY;

	_enter("%s{%llx:%llu.%u},%llx,%llx",
	       vanalde->volume->name,
	       vanalde->fid.vid,
	       vanalde->fid.vanalde,
	       vanalde->fid.unique,
	       size, pos);

	ret = afs_get_writeback_key(vanalde, &wbk);
	if (ret) {
		_leave(" = %d [anal keys]", ret);
		return ret;
	}

	op = afs_alloc_operation(wbk->key, vanalde->volume);
	if (IS_ERR(op)) {
		afs_put_wb_key(wbk);
		return -EANALMEM;
	}

	afs_op_set_vanalde(op, 0, vanalde);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->store.pos = pos;
	op->store.size = size;
	op->store.laundering = laundering;
	op->flags |= AFS_OPERATION_UNINTR;
	op->ops = &afs_store_data_operation;

try_next_key:
	afs_begin_vanalde_operation(op);

	op->store.write_iter = iter;
	op->store.i_size = max(pos + size, vanalde->netfs.remote_i_size);
	op->mtime = ianalde_get_mtime(&vanalde->netfs.ianalde);

	afs_wait_for_operation(op);

	switch (afs_op_error(op)) {
	case -EACCES:
	case -EPERM:
	case -EANALKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
		_debug("next");

		ret = afs_get_writeback_key(vanalde, &wbk);
		if (ret == 0) {
			key_put(op->key);
			op->key = key_get(wbk->key);
			goto try_next_key;
		}
		break;
	}

	afs_put_wb_key(wbk);
	_leave(" = %d", afs_op_error(op));
	return afs_put_operation(op);
}

static void afs_upload_to_server(struct netfs_io_subrequest *subreq)
{
	struct afs_vanalde *vanalde = AFS_FS_I(subreq->rreq->ianalde);
	ssize_t ret;

	_enter("%x[%x],%zx",
	       subreq->rreq->debug_id, subreq->debug_index, subreq->io_iter.count);

	trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
	ret = afs_store_data(vanalde, &subreq->io_iter, subreq->start,
			     subreq->rreq->origin == NETFS_LAUNDER_WRITE);
	netfs_write_subrequest_terminated(subreq, ret < 0 ? ret : subreq->len,
					  false);
}

static void afs_upload_to_server_worker(struct work_struct *work)
{
	struct netfs_io_subrequest *subreq =
		container_of(work, struct netfs_io_subrequest, work);

	afs_upload_to_server(subreq);
}

/*
 * Set up write requests for a writeback slice.  We need to add a write request
 * for each write we want to make.
 */
void afs_create_write_requests(struct netfs_io_request *wreq, loff_t start, size_t len)
{
	struct netfs_io_subrequest *subreq;

	_enter("%x,%llx-%llx", wreq->debug_id, start, start + len);

	subreq = netfs_create_write_request(wreq, NETFS_UPLOAD_TO_SERVER,
					    start, len, afs_upload_to_server_worker);
	if (subreq)
		netfs_queue_write_request(subreq);
}

/*
 * write some of the pending data back to the server
 */
int afs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct afs_vanalde *vanalde = AFS_FS_I(mapping->host);
	int ret;

	/* We have to be careful as we can end up racing with setattr()
	 * truncating the pagecache since the caller doesn't take a lock here
	 * to prevent it.
	 */
	if (wbc->sync_mode == WB_SYNC_ALL)
		down_read(&vanalde->validate_lock);
	else if (!down_read_trylock(&vanalde->validate_lock))
		return 0;

	ret = netfs_writepages(mapping, wbc);
	up_read(&vanalde->validate_lock);
	return ret;
}

/*
 * flush any dirty pages for this process, and check for write errors.
 * - the return status from this call provides a reliable indication of
 *   whether any write errors occurred for this process.
 */
int afs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct afs_vanalde *vanalde = AFS_FS_I(file_ianalde(file));
	struct afs_file *af = file->private_data;
	int ret;

	_enter("{%llx:%llu},{n=%pD},%d",
	       vanalde->fid.vid, vanalde->fid.vanalde, file,
	       datasync);

	ret = afs_validate(vanalde, af->key);
	if (ret < 0)
		return ret;

	return file_write_and_wait_range(file, start, end);
}

/*
 * analtification that a previously read-only page is about to become writable
 * - if it returns an error, the caller will deliver a bus error signal
 */
vm_fault_t afs_page_mkwrite(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;

	if (afs_validate(AFS_FS_I(file_ianalde(file)), afs_file_key(file)) < 0)
		return VM_FAULT_SIGBUS;
	return netfs_page_mkwrite(vmf, NULL);
}

/*
 * Prune the keys cached for writeback.  The caller must hold vanalde->wb_lock.
 */
void afs_prune_wb_keys(struct afs_vanalde *vanalde)
{
	LIST_HEAD(graveyard);
	struct afs_wb_key *wbk, *tmp;

	/* Discard unused keys */
	spin_lock(&vanalde->wb_lock);

	if (!mapping_tagged(&vanalde->netfs.ianalde.i_data, PAGECACHE_TAG_WRITEBACK) &&
	    !mapping_tagged(&vanalde->netfs.ianalde.i_data, PAGECACHE_TAG_DIRTY)) {
		list_for_each_entry_safe(wbk, tmp, &vanalde->wb_keys, vanalde_link) {
			if (refcount_read(&wbk->usage) == 1)
				list_move(&wbk->vanalde_link, &graveyard);
		}
	}

	spin_unlock(&vanalde->wb_lock);

	while (!list_empty(&graveyard)) {
		wbk = list_entry(graveyard.next, struct afs_wb_key, vanalde_link);
		list_del(&wbk->vanalde_link);
		afs_put_wb_key(wbk);
	}
}
