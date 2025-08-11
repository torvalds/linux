// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS filesystem file handling
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/gfp.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/netfs.h>
#include <trace/events/netfs.h>
#include "internal.h"

static int afs_file_mmap_prepare(struct vm_area_desc *desc);

static ssize_t afs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter);
static ssize_t afs_file_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe,
				    size_t len, unsigned int flags);
static void afs_vm_open(struct vm_area_struct *area);
static void afs_vm_close(struct vm_area_struct *area);
static vm_fault_t afs_vm_map_pages(struct vm_fault *vmf, pgoff_t start_pgoff, pgoff_t end_pgoff);

const struct file_operations afs_file_operations = {
	.open		= afs_open,
	.release	= afs_release,
	.llseek		= generic_file_llseek,
	.read_iter	= afs_file_read_iter,
	.write_iter	= netfs_file_write_iter,
	.mmap_prepare	= afs_file_mmap_prepare,
	.splice_read	= afs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fsync		= afs_fsync,
	.lock		= afs_lock,
	.flock		= afs_flock,
};

const struct inode_operations afs_file_inode_operations = {
	.getattr	= afs_getattr,
	.setattr	= afs_setattr,
	.permission	= afs_permission,
};

const struct address_space_operations afs_file_aops = {
	.direct_IO	= noop_direct_IO,
	.read_folio	= netfs_read_folio,
	.readahead	= netfs_readahead,
	.dirty_folio	= netfs_dirty_folio,
	.release_folio	= netfs_release_folio,
	.invalidate_folio = netfs_invalidate_folio,
	.migrate_folio	= filemap_migrate_folio,
	.writepages	= afs_writepages,
};

static const struct vm_operations_struct afs_vm_ops = {
	.open		= afs_vm_open,
	.close		= afs_vm_close,
	.fault		= filemap_fault,
	.map_pages	= afs_vm_map_pages,
	.page_mkwrite	= afs_page_mkwrite,
};

/*
 * Discard a pin on a writeback key.
 */
void afs_put_wb_key(struct afs_wb_key *wbk)
{
	if (wbk && refcount_dec_and_test(&wbk->usage)) {
		key_put(wbk->key);
		kfree(wbk);
	}
}

/*
 * Cache key for writeback.
 */
int afs_cache_wb_key(struct afs_vnode *vnode, struct afs_file *af)
{
	struct afs_wb_key *wbk, *p;

	wbk = kzalloc(sizeof(struct afs_wb_key), GFP_KERNEL);
	if (!wbk)
		return -ENOMEM;
	refcount_set(&wbk->usage, 2);
	wbk->key = af->key;

	spin_lock(&vnode->wb_lock);
	list_for_each_entry(p, &vnode->wb_keys, vnode_link) {
		if (p->key == wbk->key)
			goto found;
	}

	key_get(wbk->key);
	list_add_tail(&wbk->vnode_link, &vnode->wb_keys);
	spin_unlock(&vnode->wb_lock);
	af->wb = wbk;
	return 0;

found:
	refcount_inc(&p->usage);
	spin_unlock(&vnode->wb_lock);
	af->wb = p;
	kfree(wbk);
	return 0;
}

/*
 * open an AFS file or directory and attach a key to it
 */
int afs_open(struct inode *inode, struct file *file)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af;
	struct key *key;
	int ret;

	_enter("{%llx:%llu},", vnode->fid.vid, vnode->fid.vnode);

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	af = kzalloc(sizeof(*af), GFP_KERNEL);
	if (!af) {
		ret = -ENOMEM;
		goto error_key;
	}
	af->key = key;

	ret = afs_validate(vnode, key);
	if (ret < 0)
		goto error_af;

	if (file->f_mode & FMODE_WRITE) {
		ret = afs_cache_wb_key(vnode, af);
		if (ret < 0)
			goto error_af;
	}

	if (file->f_flags & O_TRUNC)
		set_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);

	fscache_use_cookie(afs_vnode_cache(vnode), file->f_mode & FMODE_WRITE);

	file->private_data = af;
	_leave(" = 0");
	return 0;

error_af:
	kfree(af);
error_key:
	key_put(key);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * release an AFS file or directory and discard its key
 */
int afs_release(struct inode *inode, struct file *file)
{
	struct afs_vnode_cache_aux aux;
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af = file->private_data;
	loff_t i_size;
	int ret = 0;

	_enter("{%llx:%llu},", vnode->fid.vid, vnode->fid.vnode);

	if ((file->f_mode & FMODE_WRITE))
		ret = vfs_fsync(file, 0);

	file->private_data = NULL;
	if (af->wb)
		afs_put_wb_key(af->wb);

	if ((file->f_mode & FMODE_WRITE)) {
		i_size = i_size_read(&vnode->netfs.inode);
		afs_set_cache_aux(vnode, &aux);
		fscache_unuse_cookie(afs_vnode_cache(vnode), &aux, &i_size);
	} else {
		fscache_unuse_cookie(afs_vnode_cache(vnode), NULL, NULL);
	}

	key_put(af->key);
	kfree(af);
	afs_prune_wb_keys(vnode);
	_leave(" = %d", ret);
	return ret;
}

static void afs_fetch_data_notify(struct afs_operation *op)
{
	struct netfs_io_subrequest *subreq = op->fetch.subreq;

	subreq->error = afs_op_error(op);
	netfs_read_subreq_terminated(subreq);
}

static void afs_fetch_data_success(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	_enter("op=%08x", op->debug_id);
	afs_vnode_commit_status(op, &op->file[0]);
	afs_stat_v(vnode, n_fetches);
	atomic_long_add(op->fetch.subreq->transferred, &op->net->n_fetch_bytes);
	afs_fetch_data_notify(op);
}

static void afs_fetch_data_aborted(struct afs_operation *op)
{
	afs_check_for_remote_deletion(op);
	afs_fetch_data_notify(op);
}

const struct afs_operation_ops afs_fetch_data_operation = {
	.issue_afs_rpc	= afs_fs_fetch_data,
	.issue_yfs_rpc	= yfs_fs_fetch_data,
	.success	= afs_fetch_data_success,
	.aborted	= afs_fetch_data_aborted,
	.failed		= afs_fetch_data_notify,
};

static void afs_issue_read_call(struct afs_operation *op)
{
	op->call_responded = false;
	op->call_error = 0;
	op->call_abort_code = 0;
	if (test_bit(AFS_SERVER_FL_IS_YFS, &op->server->flags))
		yfs_fs_fetch_data(op);
	else
		afs_fs_fetch_data(op);
}

static void afs_end_read(struct afs_operation *op)
{
	if (op->call_responded && op->server)
		set_bit(AFS_SERVER_FL_RESPONDING, &op->server->flags);

	if (!afs_op_error(op))
		afs_fetch_data_success(op);
	else if (op->cumul_error.aborted)
		afs_fetch_data_aborted(op);
	else
		afs_fetch_data_notify(op);

	afs_end_vnode_operation(op);
	afs_put_operation(op);
}

/*
 * Perform I/O processing on an asynchronous call.  The work item carries a ref
 * to the call struct that we either need to release or to pass on.
 */
static void afs_read_receive(struct afs_call *call)
{
	struct afs_operation *op = call->op;
	enum afs_call_state state;

	_enter("");

	state = READ_ONCE(call->state);
	if (state == AFS_CALL_COMPLETE)
		return;
	trace_afs_read_recv(op, call);

	while (state < AFS_CALL_COMPLETE && READ_ONCE(call->need_attention)) {
		WRITE_ONCE(call->need_attention, false);
		afs_deliver_to_call(call);
		state = READ_ONCE(call->state);
	}

	if (state < AFS_CALL_COMPLETE) {
		netfs_read_subreq_progress(op->fetch.subreq);
		if (rxrpc_kernel_check_life(call->net->socket, call->rxcall))
			return;
		/* rxrpc terminated the call. */
		afs_set_call_complete(call, call->error, call->abort_code);
	}

	op->call_abort_code	= call->abort_code;
	op->call_error		= call->error;
	op->call_responded	= call->responded;
	op->call		= NULL;
	call->op		= NULL;
	afs_put_call(call);

	/* If the call failed, then we need to crank the server rotation
	 * handle and try the next.
	 */
	if (afs_select_fileserver(op)) {
		afs_issue_read_call(op);
		return;
	}

	afs_end_read(op);
}

void afs_fetch_data_async_rx(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, async_work);

	afs_read_receive(call);
	afs_put_call(call);
}

void afs_fetch_data_immediate_cancel(struct afs_call *call)
{
	if (call->async) {
		afs_get_call(call, afs_call_trace_wake);
		if (!queue_work(afs_async_calls, &call->async_work))
			afs_deferred_put_call(call);
		flush_work(&call->async_work);
	}
}

/*
 * Fetch file data from the volume.
 */
static void afs_issue_read(struct netfs_io_subrequest *subreq)
{
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(subreq->rreq->inode);
	struct key *key = subreq->rreq->netfs_priv;

	_enter("%s{%llx:%llu.%u},%x,,,",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	op = afs_alloc_operation(key, vnode->volume);
	if (IS_ERR(op)) {
		subreq->error = PTR_ERR(op);
		netfs_read_subreq_terminated(subreq);
		return;
	}

	afs_op_set_vnode(op, 0, vnode);

	op->fetch.subreq = subreq;
	op->ops		= &afs_fetch_data_operation;

	trace_netfs_sreq(subreq, netfs_sreq_trace_submit);

	if (subreq->rreq->origin == NETFS_READAHEAD ||
	    subreq->rreq->iocb) {
		op->flags |= AFS_OPERATION_ASYNC;

		if (!afs_begin_vnode_operation(op)) {
			subreq->error = afs_put_operation(op);
			netfs_read_subreq_terminated(subreq);
			return;
		}

		if (!afs_select_fileserver(op)) {
			afs_end_read(op);
			return;
		}

		afs_issue_read_call(op);
	} else {
		afs_do_sync_operation(op);
	}
}

static int afs_init_request(struct netfs_io_request *rreq, struct file *file)
{
	struct afs_vnode *vnode = AFS_FS_I(rreq->inode);

	if (file)
		rreq->netfs_priv = key_get(afs_file_key(file));
	rreq->rsize = 256 * 1024;
	rreq->wsize = 256 * 1024 * 1024;

	switch (rreq->origin) {
	case NETFS_READ_SINGLE:
		if (!file) {
			struct key *key = afs_request_key(vnode->volume->cell);

			if (IS_ERR(key))
				return PTR_ERR(key);
			rreq->netfs_priv = key;
		}
		break;
	case NETFS_WRITEBACK:
	case NETFS_WRITETHROUGH:
	case NETFS_UNBUFFERED_WRITE:
	case NETFS_DIO_WRITE:
		if (S_ISREG(rreq->inode->i_mode))
			rreq->io_streams[0].avail = true;
		break;
	case NETFS_WRITEBACK_SINGLE:
	default:
		break;
	}
	return 0;
}

static int afs_check_write_begin(struct file *file, loff_t pos, unsigned len,
				 struct folio **foliop, void **_fsdata)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));

	return test_bit(AFS_VNODE_DELETED, &vnode->flags) ? -ESTALE : 0;
}

static void afs_free_request(struct netfs_io_request *rreq)
{
	key_put(rreq->netfs_priv);
	afs_put_wb_key(rreq->netfs_priv2);
}

static void afs_update_i_size(struct inode *inode, loff_t new_i_size)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	loff_t i_size;

	write_seqlock(&vnode->cb_lock);
	i_size = i_size_read(&vnode->netfs.inode);
	if (new_i_size > i_size) {
		i_size_write(&vnode->netfs.inode, new_i_size);
		inode_set_bytes(&vnode->netfs.inode, new_i_size);
	}
	write_sequnlock(&vnode->cb_lock);
	fscache_update_cookie(afs_vnode_cache(vnode), NULL, &new_i_size);
}

static void afs_netfs_invalidate_cache(struct netfs_io_request *wreq)
{
	struct afs_vnode *vnode = AFS_FS_I(wreq->inode);

	afs_invalidate_cache(vnode, 0);
}

const struct netfs_request_ops afs_req_ops = {
	.init_request		= afs_init_request,
	.free_request		= afs_free_request,
	.check_write_begin	= afs_check_write_begin,
	.issue_read		= afs_issue_read,
	.update_i_size		= afs_update_i_size,
	.invalidate_cache	= afs_netfs_invalidate_cache,
	.begin_writeback	= afs_begin_writeback,
	.prepare_write		= afs_prepare_write,
	.issue_write		= afs_issue_write,
	.retry_request		= afs_retry_request,
};

static void afs_add_open_mmap(struct afs_vnode *vnode)
{
	if (atomic_inc_return(&vnode->cb_nr_mmap) == 1) {
		down_write(&vnode->volume->open_mmaps_lock);

		if (list_empty(&vnode->cb_mmap_link))
			list_add_tail(&vnode->cb_mmap_link, &vnode->volume->open_mmaps);

		up_write(&vnode->volume->open_mmaps_lock);
	}
}

static void afs_drop_open_mmap(struct afs_vnode *vnode)
{
	if (atomic_add_unless(&vnode->cb_nr_mmap, -1, 1))
		return;

	down_write(&vnode->volume->open_mmaps_lock);

	read_seqlock_excl(&vnode->cb_lock);
	// the only place where ->cb_nr_mmap may hit 0
	// see __afs_break_callback() for the other side...
	if (atomic_dec_and_test(&vnode->cb_nr_mmap))
		list_del_init(&vnode->cb_mmap_link);
	read_sequnlock_excl(&vnode->cb_lock);

	up_write(&vnode->volume->open_mmaps_lock);
	flush_work(&vnode->cb_work);
}

/*
 * Handle setting up a memory mapping on an AFS file.
 */
static int afs_file_mmap_prepare(struct vm_area_desc *desc)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(desc->file));
	int ret;

	afs_add_open_mmap(vnode);

	ret = generic_file_mmap_prepare(desc);
	if (ret == 0)
		desc->vm_ops = &afs_vm_ops;
	else
		afs_drop_open_mmap(vnode);
	return ret;
}

static void afs_vm_open(struct vm_area_struct *vma)
{
	afs_add_open_mmap(AFS_FS_I(file_inode(vma->vm_file)));
}

static void afs_vm_close(struct vm_area_struct *vma)
{
	afs_drop_open_mmap(AFS_FS_I(file_inode(vma->vm_file)));
}

static vm_fault_t afs_vm_map_pages(struct vm_fault *vmf, pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(vmf->vma->vm_file));

	if (afs_check_validity(vnode))
		return filemap_map_pages(vmf, start_pgoff, end_pgoff);
	return 0;
}

static ssize_t afs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af = iocb->ki_filp->private_data;
	ssize_t ret;

	if (iocb->ki_flags & IOCB_DIRECT)
		return netfs_unbuffered_read_iter(iocb, iter);

	ret = netfs_start_io_read(inode);
	if (ret < 0)
		return ret;
	ret = afs_validate(vnode, af->key);
	if (ret == 0)
		ret = filemap_read(iocb, iter, 0);
	netfs_end_io_read(inode);
	return ret;
}

static ssize_t afs_file_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe,
				    size_t len, unsigned int flags)
{
	struct inode *inode = file_inode(in);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_file *af = in->private_data;
	ssize_t ret;

	ret = netfs_start_io_read(inode);
	if (ret < 0)
		return ret;
	ret = afs_validate(vnode, af->key);
	if (ret == 0)
		ret = filemap_splice_read(in, ppos, pipe, len, flags);
	netfs_end_io_read(inode);
	return ret;
}
