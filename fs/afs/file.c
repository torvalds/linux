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
				    struct pipe_ianalde_info *pipe,
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

const struct ianalde_operations afs_file_ianalde_operations = {
	.getattr	= afs_getattr,
	.setattr	= afs_setattr,
	.permission	= afs_permission,
};

const struct address_space_operations afs_file_aops = {
	.direct_IO	= analop_direct_IO,
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
int afs_cache_wb_key(struct afs_vanalde *vanalde, struct afs_file *af)
{
	struct afs_wb_key *wbk, *p;

	wbk = kzalloc(sizeof(struct afs_wb_key), GFP_KERNEL);
	if (!wbk)
		return -EANALMEM;
	refcount_set(&wbk->usage, 2);
	wbk->key = af->key;

	spin_lock(&vanalde->wb_lock);
	list_for_each_entry(p, &vanalde->wb_keys, vanalde_link) {
		if (p->key == wbk->key)
			goto found;
	}

	key_get(wbk->key);
	list_add_tail(&wbk->vanalde_link, &vanalde->wb_keys);
	spin_unlock(&vanalde->wb_lock);
	af->wb = wbk;
	return 0;

found:
	refcount_inc(&p->usage);
	spin_unlock(&vanalde->wb_lock);
	af->wb = p;
	kfree(wbk);
	return 0;
}

/*
 * open an AFS file or directory and attach a key to it
 */
int afs_open(struct ianalde *ianalde, struct file *file)
{
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct afs_file *af;
	struct key *key;
	int ret;

	_enter("{%llx:%llu},", vanalde->fid.vid, vanalde->fid.vanalde);

	key = afs_request_key(vanalde->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	af = kzalloc(sizeof(*af), GFP_KERNEL);
	if (!af) {
		ret = -EANALMEM;
		goto error_key;
	}
	af->key = key;

	ret = afs_validate(vanalde, key);
	if (ret < 0)
		goto error_af;

	if (file->f_mode & FMODE_WRITE) {
		ret = afs_cache_wb_key(vanalde, af);
		if (ret < 0)
			goto error_af;
	}

	if (file->f_flags & O_TRUNC)
		set_bit(AFS_VANALDE_NEW_CONTENT, &vanalde->flags);

	fscache_use_cookie(afs_vanalde_cache(vanalde), file->f_mode & FMODE_WRITE);

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
int afs_release(struct ianalde *ianalde, struct file *file)
{
	struct afs_vanalde_cache_aux aux;
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct afs_file *af = file->private_data;
	loff_t i_size;
	int ret = 0;

	_enter("{%llx:%llu},", vanalde->fid.vid, vanalde->fid.vanalde);

	if ((file->f_mode & FMODE_WRITE))
		ret = vfs_fsync(file, 0);

	file->private_data = NULL;
	if (af->wb)
		afs_put_wb_key(af->wb);

	if ((file->f_mode & FMODE_WRITE)) {
		i_size = i_size_read(&vanalde->netfs.ianalde);
		afs_set_cache_aux(vanalde, &aux);
		fscache_unuse_cookie(afs_vanalde_cache(vanalde), &aux, &i_size);
	} else {
		fscache_unuse_cookie(afs_vanalde_cache(vanalde), NULL, NULL);
	}

	key_put(af->key);
	kfree(af);
	afs_prune_wb_keys(vanalde);
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

static void afs_fetch_data_analtify(struct afs_operation *op)
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
	struct afs_vanalde *vanalde = op->file[0].vanalde;

	_enter("op=%08x", op->debug_id);
	afs_vanalde_commit_status(op, &op->file[0]);
	afs_stat_v(vanalde, n_fetches);
	atomic_long_add(op->fetch.req->actual_len, &op->net->n_fetch_bytes);
	afs_fetch_data_analtify(op);
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
	.failed		= afs_fetch_data_analtify,
	.put		= afs_fetch_data_put,
};

/*
 * Fetch file data from the volume.
 */
int afs_fetch_data(struct afs_vanalde *vanalde, struct afs_read *req)
{
	struct afs_operation *op;

	_enter("%s{%llx:%llu.%u},%x,,,",
	       vanalde->volume->name,
	       vanalde->fid.vid,
	       vanalde->fid.vanalde,
	       vanalde->fid.unique,
	       key_serial(req->key));

	op = afs_alloc_operation(req->key, vanalde->volume);
	if (IS_ERR(op)) {
		if (req->subreq)
			netfs_subreq_terminated(req->subreq, PTR_ERR(op), false);
		return PTR_ERR(op);
	}

	afs_op_set_vanalde(op, 0, vanalde);

	op->fetch.req	= afs_get_read(req);
	op->ops		= &afs_fetch_data_operation;
	return afs_do_sync_operation(op);
}

static void afs_issue_read(struct netfs_io_subrequest *subreq)
{
	struct afs_vanalde *vanalde = AFS_FS_I(subreq->rreq->ianalde);
	struct afs_read *fsreq;

	fsreq = afs_alloc_read(GFP_ANALFS);
	if (!fsreq)
		return netfs_subreq_terminated(subreq, -EANALMEM, false);

	fsreq->subreq	= subreq;
	fsreq->pos	= subreq->start + subreq->transferred;
	fsreq->len	= subreq->len   - subreq->transferred;
	fsreq->key	= key_get(subreq->rreq->netfs_priv);
	fsreq->vanalde	= vanalde;
	fsreq->iter	= &subreq->io_iter;

	afs_fetch_data(fsreq->vanalde, fsreq);
	afs_put_read(fsreq);
}

static int afs_symlink_read_folio(struct file *file, struct folio *folio)
{
	struct afs_vanalde *vanalde = AFS_FS_I(folio->mapping->host);
	struct afs_read *fsreq;
	int ret;

	fsreq = afs_alloc_read(GFP_ANALFS);
	if (!fsreq)
		return -EANALMEM;

	fsreq->pos	= folio_pos(folio);
	fsreq->len	= folio_size(folio);
	fsreq->vanalde	= vanalde;
	fsreq->iter	= &fsreq->def_iter;
	iov_iter_xarray(&fsreq->def_iter, ITER_DEST, &folio->mapping->i_pages,
			fsreq->pos, fsreq->len);

	ret = afs_fetch_data(fsreq->vanalde, fsreq);
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
	struct afs_vanalde *vanalde = AFS_FS_I(file_ianalde(file));

	return test_bit(AFS_VANALDE_DELETED, &vanalde->flags) ? -ESTALE : 0;
}

static void afs_free_request(struct netfs_io_request *rreq)
{
	key_put(rreq->netfs_priv);
}

static void afs_update_i_size(struct ianalde *ianalde, loff_t new_i_size)
{
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	loff_t i_size;

	write_seqlock(&vanalde->cb_lock);
	i_size = i_size_read(&vanalde->netfs.ianalde);
	if (new_i_size > i_size) {
		i_size_write(&vanalde->netfs.ianalde, new_i_size);
		ianalde_set_bytes(&vanalde->netfs.ianalde, new_i_size);
	}
	write_sequnlock(&vanalde->cb_lock);
	fscache_update_cookie(afs_vanalde_cache(vanalde), NULL, &new_i_size);
}

static void afs_netfs_invalidate_cache(struct netfs_io_request *wreq)
{
	struct afs_vanalde *vanalde = AFS_FS_I(wreq->ianalde);

	afs_invalidate_cache(vanalde, 0);
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

static void afs_add_open_mmap(struct afs_vanalde *vanalde)
{
	if (atomic_inc_return(&vanalde->cb_nr_mmap) == 1) {
		down_write(&vanalde->volume->open_mmaps_lock);

		if (list_empty(&vanalde->cb_mmap_link))
			list_add_tail(&vanalde->cb_mmap_link, &vanalde->volume->open_mmaps);

		up_write(&vanalde->volume->open_mmaps_lock);
	}
}

static void afs_drop_open_mmap(struct afs_vanalde *vanalde)
{
	if (atomic_add_unless(&vanalde->cb_nr_mmap, -1, 1))
		return;

	down_write(&vanalde->volume->open_mmaps_lock);

	read_seqlock_excl(&vanalde->cb_lock);
	// the only place where ->cb_nr_mmap may hit 0
	// see __afs_break_callback() for the other side...
	if (atomic_dec_and_test(&vanalde->cb_nr_mmap))
		list_del_init(&vanalde->cb_mmap_link);
	read_sequnlock_excl(&vanalde->cb_lock);

	up_write(&vanalde->volume->open_mmaps_lock);
	flush_work(&vanalde->cb_work);
}

/*
 * Handle setting up a memory mapping on an AFS file.
 */
static int afs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct afs_vanalde *vanalde = AFS_FS_I(file_ianalde(file));
	int ret;

	afs_add_open_mmap(vanalde);

	ret = generic_file_mmap(file, vma);
	if (ret == 0)
		vma->vm_ops = &afs_vm_ops;
	else
		afs_drop_open_mmap(vanalde);
	return ret;
}

static void afs_vm_open(struct vm_area_struct *vma)
{
	afs_add_open_mmap(AFS_FS_I(file_ianalde(vma->vm_file)));
}

static void afs_vm_close(struct vm_area_struct *vma)
{
	afs_drop_open_mmap(AFS_FS_I(file_ianalde(vma->vm_file)));
}

static vm_fault_t afs_vm_map_pages(struct vm_fault *vmf, pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	struct afs_vanalde *vanalde = AFS_FS_I(file_ianalde(vmf->vma->vm_file));

	if (afs_check_validity(vanalde))
		return filemap_map_pages(vmf, start_pgoff, end_pgoff);
	return 0;
}

static ssize_t afs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct afs_file *af = iocb->ki_filp->private_data;
	ssize_t ret;

	if (iocb->ki_flags & IOCB_DIRECT)
		return netfs_unbuffered_read_iter(iocb, iter);

	ret = netfs_start_io_read(ianalde);
	if (ret < 0)
		return ret;
	ret = afs_validate(vanalde, af->key);
	if (ret == 0)
		ret = filemap_read(iocb, iter, 0);
	netfs_end_io_read(ianalde);
	return ret;
}

static ssize_t afs_file_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_ianalde_info *pipe,
				    size_t len, unsigned int flags)
{
	struct ianalde *ianalde = file_ianalde(in);
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct afs_file *af = in->private_data;
	ssize_t ret;

	ret = netfs_start_io_read(ianalde);
	if (ret < 0)
		return ret;
	ret = afs_validate(vanalde, af->key);
	if (ret == 0)
		ret = filemap_splice_read(in, ppos, pipe, len, flags);
	netfs_end_io_read(ianalde);
	return ret;
}
