// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common helpers for stackable filesystems and backing files.
 *
 * Forked from fs/overlayfs/file.c.
 *
 * Copyright (C) 2017 Red Hat, Inc.
 * Copyright (C) 2023 CTERA Networks.
 */

#include <linux/fs.h>
#include <linux/backing-file.h>
#include <linux/splice.h>
#include <linux/mm.h>

#include "internal.h"

/**
 * backing_file_open - open a backing file for kernel internal use
 * @user_path:	path that the user reuqested to open
 * @flags:	open flags
 * @real_path:	path of the backing file
 * @cred:	credentials for open
 *
 * Open a backing file for a stackable filesystem (e.g., overlayfs).
 * @user_path may be on the stackable filesystem and @real_path on the
 * underlying filesystem.  In this case, we want to be able to return the
 * @user_path of the stackable filesystem. This is done by embedding the
 * returned file into a container structure that also stores the stacked
 * file's path, which can be retrieved using backing_file_user_path().
 */
struct file *backing_file_open(const struct path *user_path, int flags,
			       const struct path *real_path,
			       const struct cred *cred)
{
	struct file *f;
	int error;

	f = alloc_empty_backing_file(flags, cred);
	if (IS_ERR(f))
		return f;

	path_get(user_path);
	*backing_file_user_path(f) = *user_path;
	error = vfs_open(real_path, f);
	if (error) {
		fput(f);
		f = ERR_PTR(error);
	}

	return f;
}
EXPORT_SYMBOL_GPL(backing_file_open);

struct file *backing_tmpfile_open(const struct path *user_path, int flags,
				  const struct path *real_parentpath,
				  umode_t mode, const struct cred *cred)
{
	struct mnt_idmap *real_idmap = mnt_idmap(real_parentpath->mnt);
	struct file *f;
	int error;

	f = alloc_empty_backing_file(flags, cred);
	if (IS_ERR(f))
		return f;

	path_get(user_path);
	*backing_file_user_path(f) = *user_path;
	error = vfs_tmpfile(real_idmap, real_parentpath, f, mode);
	if (error) {
		fput(f);
		f = ERR_PTR(error);
	}
	return f;
}
EXPORT_SYMBOL(backing_tmpfile_open);

struct backing_aio {
	struct kiocb iocb;
	refcount_t ref;
	struct kiocb *orig_iocb;
	/* used for aio completion */
	void (*end_write)(struct kiocb *iocb, ssize_t);
	struct work_struct work;
	long res;
};

static struct kmem_cache *backing_aio_cachep;

#define BACKING_IOCB_MASK \
	(IOCB_NOWAIT | IOCB_HIPRI | IOCB_DSYNC | IOCB_SYNC | IOCB_APPEND)

static rwf_t iocb_to_rw_flags(int flags)
{
	return (__force rwf_t)(flags & BACKING_IOCB_MASK);
}

static void backing_aio_put(struct backing_aio *aio)
{
	if (refcount_dec_and_test(&aio->ref)) {
		fput(aio->iocb.ki_filp);
		kmem_cache_free(backing_aio_cachep, aio);
	}
}

static void backing_aio_cleanup(struct backing_aio *aio, long res)
{
	struct kiocb *iocb = &aio->iocb;
	struct kiocb *orig_iocb = aio->orig_iocb;

	orig_iocb->ki_pos = iocb->ki_pos;
	if (aio->end_write)
		aio->end_write(orig_iocb, res);

	backing_aio_put(aio);
}

static void backing_aio_rw_complete(struct kiocb *iocb, long res)
{
	struct backing_aio *aio = container_of(iocb, struct backing_aio, iocb);
	struct kiocb *orig_iocb = aio->orig_iocb;

	if (iocb->ki_flags & IOCB_WRITE)
		kiocb_end_write(iocb);

	backing_aio_cleanup(aio, res);
	orig_iocb->ki_complete(orig_iocb, res);
}

static void backing_aio_complete_work(struct work_struct *work)
{
	struct backing_aio *aio = container_of(work, struct backing_aio, work);

	backing_aio_rw_complete(&aio->iocb, aio->res);
}

static void backing_aio_queue_completion(struct kiocb *iocb, long res)
{
	struct backing_aio *aio = container_of(iocb, struct backing_aio, iocb);

	/*
	 * Punt to a work queue to serialize updates of mtime/size.
	 */
	aio->res = res;
	INIT_WORK(&aio->work, backing_aio_complete_work);
	queue_work(file_inode(aio->orig_iocb->ki_filp)->i_sb->s_dio_done_wq,
		   &aio->work);
}

static int backing_aio_init_wq(struct kiocb *iocb)
{
	struct super_block *sb = file_inode(iocb->ki_filp)->i_sb;

	if (sb->s_dio_done_wq)
		return 0;

	return sb_init_dio_done_wq(sb);
}


ssize_t backing_file_read_iter(struct file *file, struct iov_iter *iter,
			       struct kiocb *iocb, int flags,
			       struct backing_file_ctx *ctx)
{
	struct backing_aio *aio = NULL;
	const struct cred *old_cred;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_BACKING)))
		return -EIO;

	if (!iov_iter_count(iter))
		return 0;

	if (iocb->ki_flags & IOCB_DIRECT &&
	    !(file->f_mode & FMODE_CAN_ODIRECT))
		return -EINVAL;

	old_cred = override_creds_light(ctx->cred);
	if (is_sync_kiocb(iocb)) {
		rwf_t rwf = iocb_to_rw_flags(flags);

		ret = vfs_iter_read(file, iter, &iocb->ki_pos, rwf);
	} else {
		ret = -ENOMEM;
		aio = kmem_cache_zalloc(backing_aio_cachep, GFP_KERNEL);
		if (!aio)
			goto out;

		aio->orig_iocb = iocb;
		kiocb_clone(&aio->iocb, iocb, get_file(file));
		aio->iocb.ki_complete = backing_aio_rw_complete;
		refcount_set(&aio->ref, 2);
		ret = vfs_iocb_iter_read(file, &aio->iocb, iter);
		backing_aio_put(aio);
		if (ret != -EIOCBQUEUED)
			backing_aio_cleanup(aio, ret);
	}
out:
	revert_creds_light(old_cred);

	if (ctx->accessed)
		ctx->accessed(iocb->ki_filp);

	return ret;
}
EXPORT_SYMBOL_GPL(backing_file_read_iter);

ssize_t backing_file_write_iter(struct file *file, struct iov_iter *iter,
				struct kiocb *iocb, int flags,
				struct backing_file_ctx *ctx)
{
	const struct cred *old_cred;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_BACKING)))
		return -EIO;

	if (!iov_iter_count(iter))
		return 0;

	ret = file_remove_privs(iocb->ki_filp);
	if (ret)
		return ret;

	if (iocb->ki_flags & IOCB_DIRECT &&
	    !(file->f_mode & FMODE_CAN_ODIRECT))
		return -EINVAL;

	/*
	 * Stacked filesystems don't support deferred completions, don't copy
	 * this property in case it is set by the issuer.
	 */
	flags &= ~IOCB_DIO_CALLER_COMP;

	old_cred = override_creds_light(ctx->cred);
	if (is_sync_kiocb(iocb)) {
		rwf_t rwf = iocb_to_rw_flags(flags);

		ret = vfs_iter_write(file, iter, &iocb->ki_pos, rwf);
		if (ctx->end_write)
			ctx->end_write(iocb, ret);
	} else {
		struct backing_aio *aio;

		ret = backing_aio_init_wq(iocb);
		if (ret)
			goto out;

		ret = -ENOMEM;
		aio = kmem_cache_zalloc(backing_aio_cachep, GFP_KERNEL);
		if (!aio)
			goto out;

		aio->orig_iocb = iocb;
		aio->end_write = ctx->end_write;
		kiocb_clone(&aio->iocb, iocb, get_file(file));
		aio->iocb.ki_flags = flags;
		aio->iocb.ki_complete = backing_aio_queue_completion;
		refcount_set(&aio->ref, 2);
		ret = vfs_iocb_iter_write(file, &aio->iocb, iter);
		backing_aio_put(aio);
		if (ret != -EIOCBQUEUED)
			backing_aio_cleanup(aio, ret);
	}
out:
	revert_creds_light(old_cred);

	return ret;
}
EXPORT_SYMBOL_GPL(backing_file_write_iter);

ssize_t backing_file_splice_read(struct file *in, struct kiocb *iocb,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags,
				 struct backing_file_ctx *ctx)
{
	const struct cred *old_cred;
	ssize_t ret;

	if (WARN_ON_ONCE(!(in->f_mode & FMODE_BACKING)))
		return -EIO;

	old_cred = override_creds_light(ctx->cred);
	ret = vfs_splice_read(in, &iocb->ki_pos, pipe, len, flags);
	revert_creds_light(old_cred);

	if (ctx->accessed)
		ctx->accessed(iocb->ki_filp);

	return ret;
}
EXPORT_SYMBOL_GPL(backing_file_splice_read);

ssize_t backing_file_splice_write(struct pipe_inode_info *pipe,
				  struct file *out, struct kiocb *iocb,
				  size_t len, unsigned int flags,
				  struct backing_file_ctx *ctx)
{
	const struct cred *old_cred;
	ssize_t ret;

	if (WARN_ON_ONCE(!(out->f_mode & FMODE_BACKING)))
		return -EIO;

	if (!out->f_op->splice_write)
		return -EINVAL;

	ret = file_remove_privs(iocb->ki_filp);
	if (ret)
		return ret;

	old_cred = override_creds_light(ctx->cred);
	file_start_write(out);
	ret = out->f_op->splice_write(pipe, out, &iocb->ki_pos, len, flags);
	file_end_write(out);
	revert_creds_light(old_cred);

	if (ctx->end_write)
		ctx->end_write(iocb, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(backing_file_splice_write);

int backing_file_mmap(struct file *file, struct vm_area_struct *vma,
		      struct backing_file_ctx *ctx)
{
	const struct cred *old_cred;
	int ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_BACKING)))
		return -EIO;

	if (!file->f_op->mmap)
		return -ENODEV;

	vma_set_file(vma, file);

	old_cred = override_creds_light(ctx->cred);
	ret = call_mmap(vma->vm_file, vma);
	revert_creds_light(old_cred);

	if (ctx->accessed)
		ctx->accessed(vma->vm_file);

	return ret;
}
EXPORT_SYMBOL_GPL(backing_file_mmap);

static int __init backing_aio_init(void)
{
	backing_aio_cachep = KMEM_CACHE(backing_aio, SLAB_HWCACHE_ALIGN);
	if (!backing_aio_cachep)
		return -ENOMEM;

	return 0;
}
fs_initcall(backing_aio_init);
