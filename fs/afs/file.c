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
#include "internal.h"

static int afs_file_mmap(struct file *file, struct vm_area_struct *vma);
static int afs_symlink_read_folio(struct file *file, struct folio *folio);

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
	.mmap		= afs_file_mmap,
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
	.launder_folio	= netfs_launder_folio,
	.release_folio	= netfs_release_folio,
	.invalidate_folio = netfs_invalidate_folio,
	.migrate_folio	= filemap_migrate_folio,
	.writepages	= afs_writepages,
};

const struct address_space_operations afs_symlink_aops = {
	.read_folio	= afs_symlink_read_folio,
	.release_folio	= netfs_release_folio,
	.invalidate_folio = netfs_invalidate_folio,
	.migrate_folio	= filemap_migrate_folio,
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

/*
 * Allocate a new read record.
 */
struct afs_read *afs_alloc_read(gfp_t gfp)
{
	struct afs_read *req;

	req = kzalloc(sizeof(struct afs_read), gfp);
	if (req)
		refcount_set(&req->usage, 1);

	return req;
}

/*
 * Dispose of a ref to a read record.
 */
void afs_put_read(struct afs_read *req)
{
	if (refcount_dec_and_test(&req->usage)) {
		if (req->cleanup)
			req->cleanup(req);
		key_put(req->key);
		kfree(req);
	}
}

static void afs_fetch_data_notify(struct afs_operation *op)
{
	struct afs_read *req = op->fetch.req;
	struct netfs_io_subrequest *subreq = req->subreq;
	int error = afs_op_error(op);

	req->error = error;
	if (subreq) {
		__set_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags);
		netfs_subreq_terminated(subreq, error ?: req->actual_len, false);
		req->subreq = NULL;
	} else if (req->done) {
		req->done(req);
	}
}

static void afs_fetch_data_success(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	_enter("op=%08x", op->debug_id);
	afs_vnode_commit_status(op, &op->file[0]);
	afs_stat_v(vnode, n_fetches);
	atomic_long_add(op->fetch.req->actual_len, &op->net->n_fetch_bytes);
	afs_fetch_data_notify(op);
}

static void afs_fetch_data_put(struct afs_operation *op)
{
	op->fetch.req->error = afs_op_error(op);
	afs_put_read(op->fetch.req);
}

static const struct afs_operation_ops afs_fetch_data_operation = {
	.issue_afs_rpc	= afs_fs_fetch_data,
	.issue_yfs_rpc	= yfs_fs_fetch_data,
	.success	= afs_fetch_data_success,
	.aborted	= afs_check_for_remote_deletion,
	.failed		= afs_fetch_data_notify,
	.put		= afs_fetch_data_put,
};

/*
 * Fetch file data from the volume.
 */
int afs_fetch_data(struct afs_vnode *vnode, struct afs_read *req)
{
	struct afs_operation *op;

	_enter("%s{%llx:%llu.%u},%x,,,",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(req->key));

	op = afs_alloc_operation(req->key, vnode->volume);
	if (IS_ERR(op)) {
		if (req->subreq)
			netfs_subreq_terminated(req->subreq, PTR_ERR(op), false);
		return PTR_ERR(op);
	}

	afs_op_set_vnode(op, 0, vnode);

	op->fetch.req	= afs_get_read(req);
	op->ops		= &afs_fetch_data_operation;
	return afs_do_sync_operation(op);
}

static void afs_issue_read(struct netfs_io_subrequest *subreq)
{
	struct afs_vnode *vnode = AFS_FS_I(subreq->rreq->inode);
	struct afs_read *fsreq;

	fsreq = afs_alloc_read(GFP_NOFS);
	if (!fsreq)
		return netfs_subreq_terminated(subreq, -ENOMEM, false);

	fsreq->subreq	= subreq;
	fsreq->pos	= subreq->start + subreq->transferred;
	fsreq->len	= subreq->len   - subreq->transferred;
	fsreq->key	= key_get(subreq->rreq->netfs_priv);
	fsreq->vnode	= vnode;
	fsreq->iter	= &subreq->io_iter;

	afs_fetch_data(fsreq->vnode, fsreq);
	afs_put_read(fsreq);
}

static int afs_symlink_read_folio(struct file *file, struct folio *folio)
{
	struct afs_vnode *vnode = AFS_FS_I(folio->mapping->host);
	struct afs_read *fsreq;
	int ret;

	fsreq = afs_alloc_read(GFP_NOFS);
	if (!fsreq)
		return -ENOMEM;

	fsreq->pos	= folio_pos(folio);
	fsreq->len	= folio_size(folio);
	fsreq->vnode	= vnode;
	fsreq->iter	= &fsreq->def_iter;
	iov_iter_xarray(&fsreq->def_iter, ITER_DEST, &folio->mapping->i_pages,
			fsreq->pos, fsreq->len);

	ret = afs_fetch_data(fsreq->vnode, fsreq);
	if (ret == 0)
		folio_mark_uptodate(folio);
	folio_unlock(folio);
	return ret;
}

static int afs_init_request(struct netfs_io_request *rreq, struct file *file)
{
	if (file)
		rreq->netfs_priv = key_get(afs_file_key(file));
	rreq->rsize = 256 * 1024;
	rreq->wsize = 256 * 1024;
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
	.create_write_requests	= afs_create_write_requests,
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
	if (!atomic_dec_and_test(&vnode->cb_nr_mmap))
		return;

	down_write(&vnode->volume->open_mmaps_lock);

	if (atomic_read(&vnode->cb_nr_mmap) == 0)
		list_del_init(&vnode->cb_mmap_link);

	up_write(&vnode->volume->open_mmaps_lock);
	flush_work(&vnode->cb_work);
}

/*
 * Handle setting up a memory mapping on an AFS file.
 */
static int afs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct afs_vnode *vnode = AFS_FS_I(file_inode(file));
	int ret;

	afs_add_open_mmap(vnode);

	ret = generic_file_mmap(file, vma);
	if (ret == 0)
		vma->vm_ops = &afs_vm_ops;
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
