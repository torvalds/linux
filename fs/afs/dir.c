// SPDX-License-Identifier: GPL-2.0-or-later
/* dir.c: AFS filesystem directory handling
 *
 * Copyright (C) 2002, 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/iversion.h>
#include <linux/iov_iter.h>
#include <linux/task_io_accounting_ops.h>
#include "internal.h"
#include "afs_fs.h"
#include "xdr_fs.h"

static struct dentry *afs_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags);
static int afs_dir_open(struct inode *inode, struct file *file);
static int afs_readdir(struct file *file, struct dir_context *ctx);
static int afs_d_revalidate(struct inode *dir, const struct qstr *name,
			    struct dentry *dentry, unsigned int flags);
static int afs_d_delete(const struct dentry *dentry);
static void afs_d_iput(struct dentry *dentry, struct inode *inode);
static bool afs_lookup_one_filldir(struct dir_context *ctx, const char *name, int nlen,
				  loff_t fpos, u64 ino, unsigned dtype);
static bool afs_lookup_filldir(struct dir_context *ctx, const char *name, int nlen,
			      loff_t fpos, u64 ino, unsigned dtype);
static int afs_create(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode, bool excl);
static struct dentry *afs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
				struct dentry *dentry, umode_t mode);
static int afs_rmdir(struct inode *dir, struct dentry *dentry);
static int afs_unlink(struct inode *dir, struct dentry *dentry);
static int afs_link(struct dentry *from, struct inode *dir,
		    struct dentry *dentry);
static int afs_symlink(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, const char *content);
static int afs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		      struct dentry *old_dentry, struct inode *new_dir,
		      struct dentry *new_dentry, unsigned int flags);

const struct file_operations afs_dir_file_operations = {
	.open		= afs_dir_open,
	.release	= afs_release,
	.iterate_shared	= afs_readdir,
	.lock		= afs_lock,
	.llseek		= generic_file_llseek,
};

const struct inode_operations afs_dir_inode_operations = {
	.create		= afs_create,
	.lookup		= afs_lookup,
	.link		= afs_link,
	.unlink		= afs_unlink,
	.symlink	= afs_symlink,
	.mkdir		= afs_mkdir,
	.rmdir		= afs_rmdir,
	.rename		= afs_rename,
	.permission	= afs_permission,
	.getattr	= afs_getattr,
	.setattr	= afs_setattr,
};

const struct address_space_operations afs_dir_aops = {
	.writepages	= afs_single_writepages,
};

const struct dentry_operations afs_fs_dentry_operations = {
	.d_revalidate	= afs_d_revalidate,
	.d_delete	= afs_d_delete,
	.d_release	= afs_d_release,
	.d_automount	= afs_d_automount,
	.d_iput		= afs_d_iput,
};

struct afs_lookup_one_cookie {
	struct dir_context	ctx;
	struct qstr		name;
	bool			found;
	struct afs_fid		fid;
};

struct afs_lookup_cookie {
	struct dir_context	ctx;
	struct qstr		name;
	unsigned short		nr_fids;
	struct afs_fid		fids[50];
};

static void afs_dir_unuse_cookie(struct afs_vnode *dvnode, int ret)
{
	if (ret == 0) {
		struct afs_vnode_cache_aux aux;
		loff_t i_size = i_size_read(&dvnode->netfs.inode);

		afs_set_cache_aux(dvnode, &aux);
		fscache_unuse_cookie(afs_vnode_cache(dvnode), &aux, &i_size);
	} else {
		fscache_unuse_cookie(afs_vnode_cache(dvnode), NULL, NULL);
	}
}

/*
 * Iterate through a kmapped directory segment, dumping a summary of
 * the contents.
 */
static size_t afs_dir_dump_step(void *iter_base, size_t progress, size_t len,
				void *priv, void *priv2)
{
	do {
		union afs_xdr_dir_block *block = iter_base;

		pr_warn("[%05zx] %32phN\n", progress, block);
		iter_base += AFS_DIR_BLOCK_SIZE;
		progress += AFS_DIR_BLOCK_SIZE;
		len -= AFS_DIR_BLOCK_SIZE;
	} while (len > 0);

	return len;
}

/*
 * Dump the contents of a directory.
 */
static void afs_dir_dump(struct afs_vnode *dvnode)
{
	struct iov_iter iter;
	unsigned long long i_size = i_size_read(&dvnode->netfs.inode);

	pr_warn("DIR %llx:%llx is=%llx\n",
		dvnode->fid.vid, dvnode->fid.vnode, i_size);

	iov_iter_folio_queue(&iter, ITER_SOURCE, dvnode->directory, 0, 0, i_size);
	iterate_folioq(&iter, iov_iter_count(&iter), NULL, NULL,
		       afs_dir_dump_step);
}

/*
 * check that a directory folio is valid
 */
static bool afs_dir_check_block(struct afs_vnode *dvnode, size_t progress,
				union afs_xdr_dir_block *block)
{
	if (block->hdr.magic != AFS_DIR_MAGIC) {
		pr_warn("%s(%lx): [%zx] bad magic %04x\n",
		       __func__, dvnode->netfs.inode.i_ino,
		       progress, ntohs(block->hdr.magic));
		trace_afs_dir_check_failed(dvnode, progress);
		trace_afs_file_error(dvnode, -EIO, afs_file_error_dir_bad_magic);
		return false;
	}

	/* Make sure each block is NUL terminated so we can reasonably
	 * use string functions on it.  The filenames in the folio
	 * *should* be NUL-terminated anyway.
	 */
	((u8 *)block)[AFS_DIR_BLOCK_SIZE - 1] = 0;
	afs_stat_v(dvnode, n_read_dir);
	return true;
}

/*
 * Iterate through a kmapped directory segment, checking the content.
 */
static size_t afs_dir_check_step(void *iter_base, size_t progress, size_t len,
				 void *priv, void *priv2)
{
	struct afs_vnode *dvnode = priv;

	if (WARN_ON_ONCE(progress % AFS_DIR_BLOCK_SIZE ||
			 len % AFS_DIR_BLOCK_SIZE))
		return len;

	do {
		if (!afs_dir_check_block(dvnode, progress, iter_base))
			break;
		iter_base += AFS_DIR_BLOCK_SIZE;
		len -= AFS_DIR_BLOCK_SIZE;
	} while (len > 0);

	return len;
}

/*
 * Check all the blocks in a directory.
 */
static int afs_dir_check(struct afs_vnode *dvnode)
{
	struct iov_iter iter;
	unsigned long long i_size = i_size_read(&dvnode->netfs.inode);
	size_t checked = 0;

	if (unlikely(!i_size))
		return 0;

	iov_iter_folio_queue(&iter, ITER_SOURCE, dvnode->directory, 0, 0, i_size);
	checked = iterate_folioq(&iter, iov_iter_count(&iter), dvnode, NULL,
				 afs_dir_check_step);
	if (checked != i_size) {
		afs_dir_dump(dvnode);
		return -EIO;
	}
	return 0;
}

/*
 * open an AFS directory file
 */
static int afs_dir_open(struct inode *inode, struct file *file)
{
	_enter("{%lu}", inode->i_ino);

	BUILD_BUG_ON(sizeof(union afs_xdr_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_xdr_dirent) != 32);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(inode)->flags))
		return -ENOENT;

	return afs_open(inode, file);
}

/*
 * Read a file in a single download.
 */
static ssize_t afs_do_read_single(struct afs_vnode *dvnode, struct file *file)
{
	struct iov_iter iter;
	ssize_t ret;
	loff_t i_size;
	bool is_dir = (S_ISDIR(dvnode->netfs.inode.i_mode) &&
		       !test_bit(AFS_VNODE_MOUNTPOINT, &dvnode->flags));

	i_size = i_size_read(&dvnode->netfs.inode);
	if (is_dir) {
		if (i_size < AFS_DIR_BLOCK_SIZE)
			return afs_bad(dvnode, afs_file_error_dir_small);
		if (i_size > AFS_DIR_BLOCK_SIZE * 1024) {
			trace_afs_file_error(dvnode, -EFBIG, afs_file_error_dir_big);
			return -EFBIG;
		}
	} else {
		if (i_size > AFSPATHMAX) {
			trace_afs_file_error(dvnode, -EFBIG, afs_file_error_dir_big);
			return -EFBIG;
		}
	}

	/* Expand the storage.  TODO: Shrink the storage too. */
	if (dvnode->directory_size < i_size) {
		size_t cur_size = dvnode->directory_size;

		ret = netfs_alloc_folioq_buffer(NULL,
						&dvnode->directory, &cur_size, i_size,
						mapping_gfp_mask(dvnode->netfs.inode.i_mapping));
		dvnode->directory_size = cur_size;
		if (ret < 0)
			return ret;
	}

	iov_iter_folio_queue(&iter, ITER_DEST, dvnode->directory, 0, 0, dvnode->directory_size);

	/* AFS requires us to perform the read of a directory synchronously as
	 * a single unit to avoid issues with the directory contents being
	 * changed between reads.
	 */
	ret = netfs_read_single(&dvnode->netfs.inode, file, &iter);
	if (ret >= 0) {
		i_size = i_size_read(&dvnode->netfs.inode);
		if (i_size > ret) {
			/* The content has grown, so we need to expand the
			 * buffer.
			 */
			ret = -ESTALE;
		} else if (is_dir) {
			int ret2 = afs_dir_check(dvnode);

			if (ret2 < 0)
				ret = ret2;
		} else if (i_size < folioq_folio_size(dvnode->directory, 0)) {
			/* NUL-terminate a symlink. */
			char *symlink = kmap_local_folio(folioq_folio(dvnode->directory, 0), 0);

			symlink[i_size] = 0;
			kunmap_local(symlink);
		}
	}

	return ret;
}

ssize_t afs_read_single(struct afs_vnode *dvnode, struct file *file)
{
	ssize_t ret;

	fscache_use_cookie(afs_vnode_cache(dvnode), false);
	ret = afs_do_read_single(dvnode, file);
	fscache_unuse_cookie(afs_vnode_cache(dvnode), NULL, NULL);
	return ret;
}

/*
 * Read the directory into a folio_queue buffer in one go, scrubbing the
 * previous contents.  We return -ESTALE if the caller needs to call us again.
 */
ssize_t afs_read_dir(struct afs_vnode *dvnode, struct file *file)
	__acquires(&dvnode->validate_lock)
{
	ssize_t ret;
	loff_t i_size;

	i_size = i_size_read(&dvnode->netfs.inode);

	ret = -ERESTARTSYS;
	if (down_read_killable(&dvnode->validate_lock) < 0)
		goto error;

	/* We only need to reread the data if it became invalid - or if we
	 * haven't read it yet.
	 */
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    test_bit(AFS_VNODE_DIR_READ, &dvnode->flags)) {
		ret = i_size;
		goto valid;
	}

	up_read(&dvnode->validate_lock);
	if (down_write_killable(&dvnode->validate_lock) < 0)
		goto error;

	if (!test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_invalidate_cache(dvnode, 0);

	if (!test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) ||
	    !test_bit(AFS_VNODE_DIR_READ, &dvnode->flags)) {
		trace_afs_reload_dir(dvnode);
		ret = afs_read_single(dvnode, file);
		if (ret < 0)
			goto error_unlock;

		// TODO: Trim excess pages

		set_bit(AFS_VNODE_DIR_VALID, &dvnode->flags);
		set_bit(AFS_VNODE_DIR_READ, &dvnode->flags);
	} else {
		ret = i_size;
	}

	downgrade_write(&dvnode->validate_lock);
valid:
	return ret;

error_unlock:
	up_write(&dvnode->validate_lock);
error:
	_leave(" = %zd", ret);
	return ret;
}

/*
 * deal with one block in an AFS directory
 */
static int afs_dir_iterate_block(struct afs_vnode *dvnode,
				 struct dir_context *ctx,
				 union afs_xdr_dir_block *block)
{
	union afs_xdr_dirent *dire;
	unsigned int blknum, base, hdr, pos, next, nr_slots;
	size_t nlen;
	int tmp;

	blknum	= ctx->pos / AFS_DIR_BLOCK_SIZE;
	base	= blknum * AFS_DIR_SLOTS_PER_BLOCK;
	hdr	= (blknum == 0 ? AFS_DIR_RESV_BLOCKS0 : AFS_DIR_RESV_BLOCKS);
	pos	= DIV_ROUND_UP(ctx->pos, AFS_DIR_DIRENT_SIZE) - base;

	_enter("%llx,%x", ctx->pos, blknum);

	/* walk through the block, an entry at a time */
	for (unsigned int slot = hdr; slot < AFS_DIR_SLOTS_PER_BLOCK; slot = next) {
		/* skip entries marked unused in the bitmap */
		if (!(block->hdr.bitmap[slot / 8] &
		      (1 << (slot % 8)))) {
			_debug("ENT[%x]: Unused", base + slot);
			next = slot + 1;
			if (next >= pos)
				ctx->pos = (base + next) * sizeof(union afs_xdr_dirent);
			continue;
		}

		/* got a valid entry */
		dire = &block->dirents[slot];
		nlen = strnlen(dire->u.name,
			       (unsigned long)(block + 1) - (unsigned long)dire->u.name - 1);
		if (nlen > AFSNAMEMAX - 1) {
			_debug("ENT[%x]: Name too long (len %zx)",
			       base + slot, nlen);
			return afs_bad(dvnode, afs_file_error_dir_name_too_long);
		}

		_debug("ENT[%x]: %s %zx \"%s\"",
		       base + slot, (slot < pos ? "skip" : "fill"),
		       nlen, dire->u.name);

		nr_slots = afs_dir_calc_slots(nlen);
		next = slot + nr_slots;
		if (next > AFS_DIR_SLOTS_PER_BLOCK) {
			_debug("ENT[%x]: extends beyond end dir block (len %zx)",
			       base + slot, nlen);
			return afs_bad(dvnode, afs_file_error_dir_over_end);
		}

		/* Check that the name-extension dirents are all allocated */
		for (tmp = 1; tmp < nr_slots; tmp++) {
			unsigned int xslot = slot + tmp;

			if (!(block->hdr.bitmap[xslot / 8] & (1 << (xslot % 8)))) {
				_debug("ENT[%x]: Unmarked extension (%x/%x)",
				       base + slot, tmp, nr_slots);
				return afs_bad(dvnode, afs_file_error_dir_unmarked_ext);
			}
		}

		/* skip if starts before the current position */
		if (slot < pos) {
			if (next > pos)
				ctx->pos = (base + next) * sizeof(union afs_xdr_dirent);
			continue;
		}

		/* found the next entry */
		if (!dir_emit(ctx, dire->u.name, nlen,
			      ntohl(dire->u.vnode),
			      (ctx->actor == afs_lookup_filldir ||
			       ctx->actor == afs_lookup_one_filldir)?
			      ntohl(dire->u.unique) : DT_UNKNOWN)) {
			_leave(" = 0 [full]");
			return 0;
		}

		ctx->pos = (base + next) * sizeof(union afs_xdr_dirent);
	}

	_leave(" = 1 [more]");
	return 1;
}

struct afs_dir_iteration_ctx {
	struct dir_context	*dir_ctx;
	int			error;
};

/*
 * Iterate through a kmapped directory segment.
 */
static size_t afs_dir_iterate_step(void *iter_base, size_t progress, size_t len,
				   void *priv, void *priv2)
{
	struct afs_dir_iteration_ctx *ctx = priv2;
	struct afs_vnode *dvnode = priv;
	int ret;

	if (WARN_ON_ONCE(progress % AFS_DIR_BLOCK_SIZE ||
			 len % AFS_DIR_BLOCK_SIZE)) {
		pr_err("Mis-iteration prog=%zx len=%zx\n",
		       progress % AFS_DIR_BLOCK_SIZE,
		       len % AFS_DIR_BLOCK_SIZE);
		return len;
	}

	do {
		ret = afs_dir_iterate_block(dvnode, ctx->dir_ctx, iter_base);
		if (ret != 1)
			break;

		ctx->dir_ctx->pos = round_up(ctx->dir_ctx->pos, AFS_DIR_BLOCK_SIZE);
		iter_base += AFS_DIR_BLOCK_SIZE;
		len -= AFS_DIR_BLOCK_SIZE;
	} while (len > 0);

	return len;
}

/*
 * Iterate through the directory folios.
 */
static int afs_dir_iterate_contents(struct inode *dir, struct dir_context *dir_ctx)
{
	struct afs_dir_iteration_ctx ctx = { .dir_ctx = dir_ctx };
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct iov_iter iter;
	unsigned long long i_size = i_size_read(dir);

	/* Round the file position up to the next entry boundary */
	dir_ctx->pos = round_up(dir_ctx->pos, sizeof(union afs_xdr_dirent));

	if (i_size <= 0 || dir_ctx->pos >= i_size)
		return 0;

	iov_iter_folio_queue(&iter, ITER_SOURCE, dvnode->directory, 0, 0, i_size);
	iov_iter_advance(&iter, round_down(dir_ctx->pos, AFS_DIR_BLOCK_SIZE));

	iterate_folioq(&iter, iov_iter_count(&iter), dvnode, &ctx,
		       afs_dir_iterate_step);

	if (ctx.error == -ESTALE)
		afs_invalidate_dir(dvnode, afs_dir_invalid_iter_stale);
	return ctx.error;
}

/*
 * iterate through the data blob that lists the contents of an AFS directory
 */
static int afs_dir_iterate(struct inode *dir, struct dir_context *ctx,
			   struct file *file, afs_dataversion_t *_dir_version)
{
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	int retry_limit = 100;
	int ret;

	_enter("{%lu},%llx,,", dir->i_ino, ctx->pos);

	do {
		if (--retry_limit < 0) {
			pr_warn("afs_read_dir(): Too many retries\n");
			ret = -ESTALE;
			break;
		}
		ret = afs_read_dir(dvnode, file);
		if (ret < 0) {
			if (ret != -ESTALE)
				break;
			if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dir)->flags)) {
				ret = -ESTALE;
				break;
			}
			continue;
		}
		*_dir_version = inode_peek_iversion_raw(dir);

		ret = afs_dir_iterate_contents(dir, ctx);
		up_read(&dvnode->validate_lock);
	} while (ret == -ESTALE);

	_leave(" = %d", ret);
	return ret;
}

/*
 * read an AFS directory
 */
static int afs_readdir(struct file *file, struct dir_context *ctx)
{
	afs_dataversion_t dir_version;

	return afs_dir_iterate(file_inode(file), ctx, file, &dir_version);
}

/*
 * Search the directory for a single name
 * - if afs_dir_iterate_block() spots this function, it'll pass the FID
 *   uniquifier through dtype
 */
static bool afs_lookup_one_filldir(struct dir_context *ctx, const char *name,
				  int nlen, loff_t fpos, u64 ino, unsigned dtype)
{
	struct afs_lookup_one_cookie *cookie =
		container_of(ctx, struct afs_lookup_one_cookie, ctx);

	_enter("{%s,%u},%s,%u,,%llu,%u",
	       cookie->name.name, cookie->name.len, name, nlen,
	       (unsigned long long) ino, dtype);

	/* insanity checks first */
	BUILD_BUG_ON(sizeof(union afs_xdr_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_xdr_dirent) != 32);

	if (cookie->name.len != nlen ||
	    memcmp(cookie->name.name, name, nlen) != 0) {
		_leave(" = true [keep looking]");
		return true;
	}

	cookie->fid.vnode = ino;
	cookie->fid.unique = dtype;
	cookie->found = 1;

	_leave(" = false [found]");
	return false;
}

/*
 * Do a lookup of a single name in a directory
 * - just returns the FID the dentry name maps to if found
 */
static int afs_do_lookup_one(struct inode *dir, const struct qstr *name,
			     struct afs_fid *fid,
			     afs_dataversion_t *_dir_version)
{
	struct afs_super_info *as = dir->i_sb->s_fs_info;
	struct afs_lookup_one_cookie cookie = {
		.ctx.actor = afs_lookup_one_filldir,
		.name = *name,
		.fid.vid = as->volume->vid
	};
	int ret;

	_enter("{%lu},{%.*s},", dir->i_ino, name->len, name->name);

	/* search the directory */
	ret = afs_dir_iterate(dir, &cookie.ctx, NULL, _dir_version);
	if (ret < 0) {
		_leave(" = %d [iter]", ret);
		return ret;
	}

	if (!cookie.found) {
		_leave(" = -ENOENT [not found]");
		return -ENOENT;
	}

	*fid = cookie.fid;
	_leave(" = 0 { vn=%llu u=%u }", fid->vnode, fid->unique);
	return 0;
}

/*
 * search the directory for a name
 * - if afs_dir_iterate_block() spots this function, it'll pass the FID
 *   uniquifier through dtype
 */
static bool afs_lookup_filldir(struct dir_context *ctx, const char *name,
			      int nlen, loff_t fpos, u64 ino, unsigned dtype)
{
	struct afs_lookup_cookie *cookie =
		container_of(ctx, struct afs_lookup_cookie, ctx);

	_enter("{%s,%u},%s,%u,,%llu,%u",
	       cookie->name.name, cookie->name.len, name, nlen,
	       (unsigned long long) ino, dtype);

	/* insanity checks first */
	BUILD_BUG_ON(sizeof(union afs_xdr_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_xdr_dirent) != 32);

	if (cookie->nr_fids < 50) {
		cookie->fids[cookie->nr_fids].vnode	= ino;
		cookie->fids[cookie->nr_fids].unique	= dtype;
		cookie->nr_fids++;
	}

	return cookie->nr_fids < 50;
}

/*
 * Deal with the result of a successful lookup operation.  Turn all the files
 * into inodes and save the first one - which is the one we actually want.
 */
static void afs_do_lookup_success(struct afs_operation *op)
{
	struct afs_vnode_param *vp;
	struct afs_vnode *vnode;
	struct inode *inode;
	u32 abort_code;
	int i;

	_enter("");

	for (i = 0; i < op->nr_files; i++) {
		switch (i) {
		case 0:
			vp = &op->file[0];
			abort_code = vp->scb.status.abort_code;
			if (abort_code != 0) {
				op->call_abort_code = abort_code;
				afs_op_set_error(op, afs_abort_to_error(abort_code));
				op->cumul_error.abort_code = abort_code;
			}
			break;

		case 1:
			vp = &op->file[1];
			break;

		default:
			vp = &op->more_files[i - 2];
			break;
		}

		if (vp->scb.status.abort_code)
			trace_afs_bulkstat_error(op, &vp->fid, i, vp->scb.status.abort_code);
		if (!vp->scb.have_status && !vp->scb.have_error)
			continue;

		_debug("do [%u]", i);
		if (vp->vnode) {
			if (!test_bit(AFS_VNODE_UNSET, &vp->vnode->flags))
				afs_vnode_commit_status(op, vp);
		} else if (vp->scb.status.abort_code == 0) {
			inode = afs_iget(op, vp);
			if (!IS_ERR(inode)) {
				vnode = AFS_FS_I(inode);
				afs_cache_permit(vnode, op->key,
						 0 /* Assume vnode->cb_break is 0 */ +
						 op->cb_v_break,
						 &vp->scb);
				vp->vnode = vnode;
				vp->put_vnode = true;
			}
		} else {
			_debug("- abort %d %llx:%llx.%x",
			       vp->scb.status.abort_code,
			       vp->fid.vid, vp->fid.vnode, vp->fid.unique);
		}
	}

	_leave("");
}

static const struct afs_operation_ops afs_inline_bulk_status_operation = {
	.issue_afs_rpc	= afs_fs_inline_bulk_status,
	.issue_yfs_rpc	= yfs_fs_inline_bulk_status,
	.success	= afs_do_lookup_success,
};

static const struct afs_operation_ops afs_lookup_fetch_status_operation = {
	.issue_afs_rpc	= afs_fs_fetch_status,
	.issue_yfs_rpc	= yfs_fs_fetch_status,
	.success	= afs_do_lookup_success,
	.aborted	= afs_check_for_remote_deletion,
};

/*
 * See if we know that the server we expect to use doesn't support
 * FS.InlineBulkStatus.
 */
static bool afs_server_supports_ibulk(struct afs_vnode *dvnode)
{
	struct afs_server_list *slist;
	struct afs_volume *volume = dvnode->volume;
	struct afs_server *server;
	bool ret = true;
	int i;

	if (!test_bit(AFS_VOLUME_MAYBE_NO_IBULK, &volume->flags))
		return true;

	rcu_read_lock();
	slist = rcu_dereference(volume->servers);

	for (i = 0; i < slist->nr_servers; i++) {
		server = slist->servers[i].server;
		if (server == dvnode->cb_server) {
			if (test_bit(AFS_SERVER_FL_NO_IBULK, &server->flags))
				ret = false;
			break;
		}
	}

	rcu_read_unlock();
	return ret;
}

/*
 * Do a lookup in a directory.  We make use of bulk lookup to query a slew of
 * files in one go and create inodes for them.  The inode of the file we were
 * asked for is returned.
 */
static struct inode *afs_do_lookup(struct inode *dir, struct dentry *dentry)
{
	struct afs_lookup_cookie *cookie;
	struct afs_vnode_param *vp;
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir), *vnode;
	struct inode *inode = NULL, *ti;
	afs_dataversion_t data_version = READ_ONCE(dvnode->status.data_version);
	bool supports_ibulk;
	long ret;
	int i;

	_enter("{%lu},%p{%pd},", dir->i_ino, dentry, dentry);

	cookie = kzalloc(sizeof(struct afs_lookup_cookie), GFP_KERNEL);
	if (!cookie)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(cookie->fids); i++)
		cookie->fids[i].vid = dvnode->fid.vid;
	cookie->ctx.actor = afs_lookup_filldir;
	cookie->name = dentry->d_name;
	cookie->nr_fids = 2; /* slot 1 is saved for the fid we actually want
			      * and slot 0 for the directory */

	/* Search the directory for the named entry using the hash table... */
	ret = afs_dir_search(dvnode, &dentry->d_name, &cookie->fids[1], &data_version);
	if (ret < 0)
		goto out;

	supports_ibulk = afs_server_supports_ibulk(dvnode);
	if (supports_ibulk) {
		/* ...then scan linearly from that point for entries to lookup-ahead. */
		cookie->ctx.pos = (ret + 1) * AFS_DIR_DIRENT_SIZE;
		afs_dir_iterate(dir, &cookie->ctx, NULL, &data_version);
	}

	dentry->d_fsdata = (void *)(unsigned long)data_version;

	/* Check to see if we already have an inode for the primary fid. */
	inode = ilookup5(dir->i_sb, cookie->fids[1].vnode,
			 afs_ilookup5_test_by_fid, &cookie->fids[1]);
	if (inode)
		goto out; /* We do */

	/* Okay, we didn't find it.  We need to query the server - and whilst
	 * we're doing that, we're going to attempt to look up a bunch of other
	 * vnodes also.
	 */
	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto out;
	}

	afs_op_set_vnode(op, 0, dvnode);
	afs_op_set_fid(op, 1, &cookie->fids[1]);

	op->nr_files = cookie->nr_fids;
	_debug("nr_files %u", op->nr_files);

	/* Need space for examining all the selected files */
	if (op->nr_files > 2) {
		op->more_files = kvcalloc(op->nr_files - 2,
					  sizeof(struct afs_vnode_param),
					  GFP_KERNEL);
		if (!op->more_files) {
			afs_op_nomem(op);
			goto out_op;
		}

		for (i = 2; i < op->nr_files; i++) {
			vp = &op->more_files[i - 2];
			vp->fid = cookie->fids[i];

			/* Find any inodes that already exist and get their
			 * callback counters.
			 */
			ti = ilookup5_nowait(dir->i_sb, vp->fid.vnode,
					     afs_ilookup5_test_by_fid, &vp->fid);
			if (!IS_ERR_OR_NULL(ti)) {
				vnode = AFS_FS_I(ti);
				vp->dv_before = vnode->status.data_version;
				vp->cb_break_before = afs_calc_vnode_cb_break(vnode);
				vp->vnode = vnode;
				vp->put_vnode = true;
				vp->speculative = true; /* vnode not locked */
			}
		}
	}

	/* Try FS.InlineBulkStatus first.  Abort codes for the individual
	 * lookups contained therein are stored in the reply without aborting
	 * the whole operation.
	 */
	afs_op_set_error(op, -ENOTSUPP);
	if (supports_ibulk) {
		op->ops = &afs_inline_bulk_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}

	if (afs_op_error(op) == -ENOTSUPP) {
		/* We could try FS.BulkStatus next, but this aborts the entire
		 * op if any of the lookups fails - so, for the moment, revert
		 * to FS.FetchStatus for op->file[1].
		 */
		op->fetch_status.which = 1;
		op->ops = &afs_lookup_fetch_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}

out_op:
	if (!afs_op_error(op)) {
		if (op->file[1].scb.status.abort_code) {
			afs_op_accumulate_error(op, -ECONNABORTED,
						op->file[1].scb.status.abort_code);
		} else {
			inode = &op->file[1].vnode->netfs.inode;
			op->file[1].vnode = NULL;
		}
	}

	if (op->file[0].scb.have_status)
		dentry->d_fsdata = (void *)(unsigned long)op->file[0].scb.status.data_version;
	else
		dentry->d_fsdata = (void *)(unsigned long)op->file[0].dv_before;
	ret = afs_put_operation(op);
out:
	kfree(cookie);
	_leave("");
	return inode ?: ERR_PTR(ret);
}

/*
 * Look up an entry in a directory with @sys substitution.
 */
static struct dentry *afs_lookup_atsys(struct inode *dir, struct dentry *dentry)
{
	struct afs_sysnames *subs;
	struct afs_net *net = afs_i2net(dir);
	struct dentry *ret;
	char *buf, *p, *name;
	int len, i;

	_enter("");

	ret = ERR_PTR(-ENOMEM);
	p = buf = kmalloc(AFSNAMEMAX, GFP_KERNEL);
	if (!buf)
		goto out_p;
	if (dentry->d_name.len > 4) {
		memcpy(p, dentry->d_name.name, dentry->d_name.len - 4);
		p += dentry->d_name.len - 4;
	}

	/* There is an ordered list of substitutes that we have to try. */
	read_lock(&net->sysnames_lock);
	subs = net->sysnames;
	refcount_inc(&subs->usage);
	read_unlock(&net->sysnames_lock);

	for (i = 0; i < subs->nr; i++) {
		name = subs->subs[i];
		len = dentry->d_name.len - 4 + strlen(name);
		if (len >= AFSNAMEMAX) {
			ret = ERR_PTR(-ENAMETOOLONG);
			goto out_s;
		}

		strcpy(p, name);
		ret = lookup_one_len(buf, dentry->d_parent, len);
		if (IS_ERR(ret) || d_is_positive(ret))
			goto out_s;
		dput(ret);
	}

	/* We don't want to d_add() the @sys dentry here as we don't want to
	 * the cached dentry to hide changes to the sysnames list.
	 */
	ret = NULL;
out_s:
	afs_put_sysnames(subs);
	kfree(buf);
out_p:
	return ret;
}

/*
 * look up an entry in a directory
 */
static struct dentry *afs_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags)
{
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_fid fid = {};
	struct inode *inode;
	struct dentry *d;
	int ret;

	_enter("{%llx:%llu},%p{%pd},",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, dentry);

	ASSERTCMP(d_inode(dentry), ==, NULL);

	if (dentry->d_name.len >= AFSNAMEMAX) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	if (test_bit(AFS_VNODE_DELETED, &dvnode->flags)) {
		_leave(" = -ESTALE");
		return ERR_PTR(-ESTALE);
	}

	ret = afs_validate(dvnode, NULL);
	if (ret < 0) {
		afs_dir_unuse_cookie(dvnode, ret);
		_leave(" = %d [val]", ret);
		return ERR_PTR(ret);
	}

	if (dentry->d_name.len >= 4 &&
	    dentry->d_name.name[dentry->d_name.len - 4] == '@' &&
	    dentry->d_name.name[dentry->d_name.len - 3] == 's' &&
	    dentry->d_name.name[dentry->d_name.len - 2] == 'y' &&
	    dentry->d_name.name[dentry->d_name.len - 1] == 's')
		return afs_lookup_atsys(dir, dentry);

	afs_stat_v(dvnode, n_lookup);
	inode = afs_do_lookup(dir, dentry);
	if (inode == ERR_PTR(-ENOENT))
		inode = NULL;
	else if (!IS_ERR_OR_NULL(inode))
		fid = AFS_FS_I(inode)->fid;

	_debug("splice %p", dentry->d_inode);
	d = d_splice_alias(inode, dentry);
	if (!IS_ERR_OR_NULL(d)) {
		d->d_fsdata = dentry->d_fsdata;
		trace_afs_lookup(dvnode, &d->d_name, &fid);
	} else {
		trace_afs_lookup(dvnode, &dentry->d_name, &fid);
	}
	_leave("");
	return d;
}

/*
 * Check the validity of a dentry under RCU conditions.
 */
static int afs_d_revalidate_rcu(struct afs_vnode *dvnode, struct dentry *dentry)
{
	long dir_version, de_version;

	_enter("%p", dentry);

	if (test_bit(AFS_VNODE_DELETED, &dvnode->flags))
		return -ECHILD;

	if (!afs_check_validity(dvnode))
		return -ECHILD;

	/* We only need to invalidate a dentry if the server's copy changed
	 * behind our back.  If we made the change, it's no problem.  Note that
	 * on a 32-bit system, we only have 32 bits in the dentry to store the
	 * version.
	 */
	dir_version = (long)READ_ONCE(dvnode->status.data_version);
	de_version = (long)READ_ONCE(dentry->d_fsdata);
	if (de_version != dir_version) {
		dir_version = (long)READ_ONCE(dvnode->invalid_before);
		if (de_version - dir_version < 0)
			return -ECHILD;
	}

	return 1; /* Still valid */
}

/*
 * check that a dentry lookup hit has found a valid entry
 * - NOTE! the hit can be a negative hit too, so we can't assume we have an
 *   inode
 */
static int afs_d_revalidate(struct inode *parent_dir, const struct qstr *name,
			    struct dentry *dentry, unsigned int flags)
{
	struct afs_vnode *vnode, *dir = AFS_FS_I(parent_dir);
	struct afs_fid fid;
	struct inode *inode;
	struct key *key;
	afs_dataversion_t dir_version, invalid_before;
	long de_version;
	int ret;

	if (flags & LOOKUP_RCU)
		return afs_d_revalidate_rcu(dir, dentry);

	if (d_really_is_positive(dentry)) {
		vnode = AFS_FS_I(d_inode(dentry));
		_enter("{v={%llx:%llu} n=%pd fl=%lx},",
		       vnode->fid.vid, vnode->fid.vnode, dentry,
		       vnode->flags);
	} else {
		_enter("{neg n=%pd}", dentry);
	}

	key = afs_request_key(AFS_FS_S(dentry->d_sb)->volume->cell);
	if (IS_ERR(key))
		key = NULL;

	/* validate the parent directory */
	ret = afs_validate(dir, key);
	if (ret == -ERESTARTSYS) {
		key_put(key);
		return ret;
	}

	if (test_bit(AFS_VNODE_DELETED, &dir->flags)) {
		_debug("%pd: parent dir deleted", dentry);
		goto not_found;
	}

	/* We only need to invalidate a dentry if the server's copy changed
	 * behind our back.  If we made the change, it's no problem.  Note that
	 * on a 32-bit system, we only have 32 bits in the dentry to store the
	 * version.
	 */
	dir_version = dir->status.data_version;
	de_version = (long)dentry->d_fsdata;
	if (de_version == (long)dir_version)
		goto out_valid_noupdate;

	invalid_before = dir->invalid_before;
	if (de_version - (long)invalid_before >= 0)
		goto out_valid;

	_debug("dir modified");
	afs_stat_v(dir, n_reval);

	/* search the directory for this vnode */
	ret = afs_do_lookup_one(&dir->netfs.inode, name, &fid, &dir_version);
	switch (ret) {
	case 0:
		/* the filename maps to something */
		if (d_really_is_negative(dentry))
			goto not_found;
		inode = d_inode(dentry);
		if (is_bad_inode(inode)) {
			printk("kAFS: afs_d_revalidate: %pd2 has bad inode\n",
			       dentry);
			goto not_found;
		}

		vnode = AFS_FS_I(inode);

		/* if the vnode ID has changed, then the dirent points to a
		 * different file */
		if (fid.vnode != vnode->fid.vnode) {
			_debug("%pd: dirent changed [%llu != %llu]",
			       dentry, fid.vnode,
			       vnode->fid.vnode);
			goto not_found;
		}

		/* if the vnode ID uniqifier has changed, then the file has
		 * been deleted and replaced, and the original vnode ID has
		 * been reused */
		if (fid.unique != vnode->fid.unique) {
			_debug("%pd: file deleted (uq %u -> %u I:%u)",
			       dentry, fid.unique,
			       vnode->fid.unique,
			       vnode->netfs.inode.i_generation);
			goto not_found;
		}
		goto out_valid;

	case -ENOENT:
		/* the filename is unknown */
		_debug("%pd: dirent not found", dentry);
		if (d_really_is_positive(dentry))
			goto not_found;
		goto out_valid;

	default:
		_debug("failed to iterate parent %pd2: %d", dentry, ret);
		goto not_found;
	}

out_valid:
	dentry->d_fsdata = (void *)(unsigned long)dir_version;
out_valid_noupdate:
	key_put(key);
	_leave(" = 1 [valid]");
	return 1;

not_found:
	_debug("dropping dentry %pd2", dentry);
	key_put(key);

	_leave(" = 0 [bad]");
	return 0;
}

/*
 * allow the VFS to enquire as to whether a dentry should be unhashed (mustn't
 * sleep)
 * - called from dput() when d_count is going to 0.
 * - return 1 to request dentry be unhashed, 0 otherwise
 */
static int afs_d_delete(const struct dentry *dentry)
{
	_enter("%pd", dentry);

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto zap;

	if (d_really_is_positive(dentry) &&
	    (test_bit(AFS_VNODE_DELETED,   &AFS_FS_I(d_inode(dentry))->flags) ||
	     test_bit(AFS_VNODE_PSEUDODIR, &AFS_FS_I(d_inode(dentry))->flags)))
		goto zap;

	_leave(" = 0 [keep]");
	return 0;

zap:
	_leave(" = 1 [zap]");
	return 1;
}

/*
 * Clean up sillyrename files on dentry removal.
 */
static void afs_d_iput(struct dentry *dentry, struct inode *inode)
{
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		afs_silly_iput(dentry, inode);
	iput(inode);
}

/*
 * handle dentry release
 */
void afs_d_release(struct dentry *dentry)
{
	_enter("%pd", dentry);
}

void afs_check_for_remote_deletion(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	switch (afs_op_abort_code(op)) {
	case VNOVNODE:
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		clear_nlink(&vnode->netfs.inode);
		afs_break_callback(vnode, afs_cb_break_for_deleted);
	}
}

/*
 * Create a new inode for create/mkdir/symlink
 */
static void afs_vnode_new_inode(struct afs_operation *op)
{
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	struct afs_vnode *vnode;
	struct inode *inode;

	_enter("");

	ASSERTCMP(afs_op_error(op), ==, 0);

	inode = afs_iget(op, vp);
	if (IS_ERR(inode)) {
		/* ENOMEM or EINTR at a really inconvenient time - just abandon
		 * the new directory on the server.
		 */
		afs_op_accumulate_error(op, PTR_ERR(inode), 0);
		return;
	}

	vnode = AFS_FS_I(inode);
	set_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	if (S_ISDIR(inode->i_mode))
		afs_mkdir_init_dir(vnode, dvp->vnode);
	else if (S_ISLNK(inode->i_mode))
		afs_init_new_symlink(vnode, op);
	if (!afs_op_error(op))
		afs_cache_permit(vnode, op->key, vnode->cb_break, &vp->scb);
	d_instantiate(op->dentry, inode);
}

static void afs_create_success(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	op->ctime = op->file[0].scb.status.mtime_client;
	afs_vnode_commit_status(op, &op->file[0]);
	afs_update_dentry_version(op, &op->file[0], op->dentry);
	afs_vnode_new_inode(op);
}

static void afs_create_edit_dir(struct afs_operation *op)
{
	struct netfs_cache_resources cres = {};
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);

	fscache_begin_write_operation(&cres, afs_vnode_cache(dvnode));
	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_add(dvnode, &op->dentry->d_name, &vp->fid,
				 op->create.reason);
	up_write(&dvnode->validate_lock);
	fscache_end_operation(&cres);
}

static void afs_create_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);

	if (afs_op_error(op))
		d_drop(op->dentry);
}

static const struct afs_operation_ops afs_mkdir_operation = {
	.issue_afs_rpc	= afs_fs_make_dir,
	.issue_yfs_rpc	= yfs_fs_make_dir,
	.success	= afs_create_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_create_edit_dir,
	.put		= afs_create_put,
};

/*
 * create a directory on an AFS filesystem
 */
static struct dentry *afs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
				struct dentry *dentry, umode_t mode)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	int ret;

	_enter("{%llx:%llu},{%pd},%ho",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op)) {
		d_drop(dentry);
		return ERR_CAST(op);
	}

	fscache_use_cookie(afs_vnode_cache(dvnode), true);

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;
	op->dentry	= dentry;
	op->create.mode	= S_IFDIR | mode;
	op->create.reason = afs_edit_dir_for_mkdir;
	op->mtime	= current_time(dir);
	op->ops		= &afs_mkdir_operation;
	ret = afs_do_sync_operation(op);
	afs_dir_unuse_cookie(dvnode, ret);
	return ERR_PTR(ret);
}

/*
 * Remove a subdir from a directory.
 */
static void afs_dir_remove_subdir(struct dentry *dentry)
{
	if (d_really_is_positive(dentry)) {
		struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));

		clear_nlink(&vnode->netfs.inode);
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		afs_clear_cb_promise(vnode, afs_cb_promise_clear_rmdir);
		afs_invalidate_dir(vnode, afs_dir_invalid_subdir_removed);
	}
}

static void afs_rmdir_success(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	op->ctime = op->file[0].scb.status.mtime_client;
	afs_vnode_commit_status(op, &op->file[0]);
	afs_update_dentry_version(op, &op->file[0], op->dentry);
}

static void afs_rmdir_edit_dir(struct afs_operation *op)
{
	struct netfs_cache_resources cres = {};
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);
	afs_dir_remove_subdir(op->dentry);

	fscache_begin_write_operation(&cres, afs_vnode_cache(dvnode));
	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_remove(dvnode, &op->dentry->d_name,
				    afs_edit_dir_for_rmdir);
	up_write(&dvnode->validate_lock);
	fscache_end_operation(&cres);
}

static void afs_rmdir_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	if (op->file[1].vnode)
		up_write(&op->file[1].vnode->rmdir_lock);
}

static const struct afs_operation_ops afs_rmdir_operation = {
	.issue_afs_rpc	= afs_fs_remove_dir,
	.issue_yfs_rpc	= yfs_fs_remove_dir,
	.success	= afs_rmdir_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_rmdir_edit_dir,
	.put		= afs_rmdir_put,
};

/*
 * remove a directory from an AFS filesystem
 */
static int afs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir), *vnode = NULL;
	int ret;

	_enter("{%llx:%llu},{%pd}",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry);

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	fscache_use_cookie(afs_vnode_cache(dvnode), true);

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;

	op->dentry	= dentry;
	op->ops		= &afs_rmdir_operation;

	/* Try to make sure we have a callback promise on the victim. */
	if (d_really_is_positive(dentry)) {
		vnode = AFS_FS_I(d_inode(dentry));
		ret = afs_validate(vnode, op->key);
		if (ret < 0)
			goto error;
	}

	if (vnode) {
		ret = down_write_killable(&vnode->rmdir_lock);
		if (ret < 0)
			goto error;
		op->file[1].vnode = vnode;
	}

	ret = afs_do_sync_operation(op);

	/* Not all systems that can host afs servers have ENOTEMPTY. */
	if (ret == -EEXIST)
		ret = -ENOTEMPTY;
out:
	afs_dir_unuse_cookie(dvnode, ret);
	return ret;

error:
	ret = afs_put_operation(op);
	goto out;
}

/*
 * Remove a link to a file or symlink from a directory.
 *
 * If the file was not deleted due to excess hard links, the fileserver will
 * break the callback promise on the file - if it had one - before it returns
 * to us, and if it was deleted, it won't
 *
 * However, if we didn't have a callback promise outstanding, or it was
 * outstanding on a different server, then it won't break it either...
 */
static void afs_dir_remove_link(struct afs_operation *op)
{
	struct afs_vnode *dvnode = op->file[0].vnode;
	struct afs_vnode *vnode = op->file[1].vnode;
	struct dentry *dentry = op->dentry;
	int ret;

	if (afs_op_error(op) ||
	    (op->file[1].scb.have_status && op->file[1].scb.have_error))
		return;
	if (d_really_is_positive(dentry))
		return;

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		/* Already done */
	} else if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags)) {
		write_seqlock(&vnode->cb_lock);
		drop_nlink(&vnode->netfs.inode);
		if (vnode->netfs.inode.i_nlink == 0) {
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			__afs_break_callback(vnode, afs_cb_break_for_unlink);
		}
		write_sequnlock(&vnode->cb_lock);
	} else {
		afs_break_callback(vnode, afs_cb_break_for_unlink);

		if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
			_debug("AFS_VNODE_DELETED");

		ret = afs_validate(vnode, op->key);
		if (ret != -ESTALE)
			afs_op_set_error(op, ret);
	}

	_debug("nlink %d [val %d]", vnode->netfs.inode.i_nlink, afs_op_error(op));
}

static void afs_unlink_success(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	op->ctime = op->file[0].scb.status.mtime_client;
	afs_check_dir_conflict(op, &op->file[0]);
	afs_vnode_commit_status(op, &op->file[0]);
	afs_vnode_commit_status(op, &op->file[1]);
	afs_update_dentry_version(op, &op->file[0], op->dentry);
	afs_dir_remove_link(op);
}

static void afs_unlink_edit_dir(struct afs_operation *op)
{
	struct netfs_cache_resources cres = {};
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);
	fscache_begin_write_operation(&cres, afs_vnode_cache(dvnode));
	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_remove(dvnode, &op->dentry->d_name,
				    afs_edit_dir_for_unlink);
	up_write(&dvnode->validate_lock);
	fscache_end_operation(&cres);
}

static void afs_unlink_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	if (op->unlink.need_rehash && afs_op_error(op) < 0 && afs_op_error(op) != -ENOENT)
		d_rehash(op->dentry);
}

static const struct afs_operation_ops afs_unlink_operation = {
	.issue_afs_rpc	= afs_fs_remove_file,
	.issue_yfs_rpc	= yfs_fs_remove_file,
	.success	= afs_unlink_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_unlink_edit_dir,
	.put		= afs_unlink_put,
};

/*
 * Remove a file or symlink from an AFS filesystem.
 */
static int afs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));
	int ret;

	_enter("{%llx:%llu},{%pd}",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry);

	if (dentry->d_name.len >= AFSNAMEMAX)
		return -ENAMETOOLONG;

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	fscache_use_cookie(afs_vnode_cache(dvnode), true);

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;

	/* Try to make sure we have a callback promise on the victim. */
	ret = afs_validate(vnode, op->key);
	if (ret < 0) {
		afs_op_set_error(op, ret);
		goto error;
	}

	spin_lock(&dentry->d_lock);
	if (d_count(dentry) > 1) {
		spin_unlock(&dentry->d_lock);
		/* Start asynchronous writeout of the inode */
		write_inode_now(d_inode(dentry), 0);
		afs_op_set_error(op, afs_sillyrename(dvnode, vnode, dentry, op->key));
		goto error;
	}
	if (!d_unhashed(dentry)) {
		/* Prevent a race with RCU lookup. */
		__d_drop(dentry);
		op->unlink.need_rehash = true;
	}
	spin_unlock(&dentry->d_lock);

	op->file[1].vnode = vnode;
	op->file[1].update_ctime = true;
	op->file[1].op_unlinked = true;
	op->dentry	= dentry;
	op->ops		= &afs_unlink_operation;
	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);

	/* If there was a conflict with a third party, check the status of the
	 * unlinked vnode.
	 */
	if (afs_op_error(op) == 0 && (op->flags & AFS_OPERATION_DIR_CONFLICT)) {
		op->file[1].update_ctime = false;
		op->fetch_status.which = 1;
		op->ops = &afs_fetch_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}

error:
	ret = afs_put_operation(op);
	afs_dir_unuse_cookie(dvnode, ret);
	return ret;
}

static const struct afs_operation_ops afs_create_operation = {
	.issue_afs_rpc	= afs_fs_create_file,
	.issue_yfs_rpc	= yfs_fs_create_file,
	.success	= afs_create_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_create_edit_dir,
	.put		= afs_create_put,
};

/*
 * create a regular file on an AFS filesystem
 */
static int afs_create(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode, bool excl)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	int ret = -ENAMETOOLONG;

	_enter("{%llx:%llu},{%pd},%ho",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto error;
	}

	fscache_use_cookie(afs_vnode_cache(dvnode), true);

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;

	op->dentry	= dentry;
	op->create.mode	= S_IFREG | mode;
	op->create.reason = afs_edit_dir_for_create;
	op->mtime	= current_time(dir);
	op->ops		= &afs_create_operation;
	ret = afs_do_sync_operation(op);
	afs_dir_unuse_cookie(dvnode, ret);
	return ret;

error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

static void afs_link_success(struct afs_operation *op)
{
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];

	_enter("op=%08x", op->debug_id);
	op->ctime = dvp->scb.status.mtime_client;
	afs_vnode_commit_status(op, dvp);
	afs_vnode_commit_status(op, vp);
	afs_update_dentry_version(op, dvp, op->dentry);
	if (op->dentry_2->d_parent == op->dentry->d_parent)
		afs_update_dentry_version(op, dvp, op->dentry_2);
	ihold(&vp->vnode->netfs.inode);
	d_instantiate(op->dentry, &vp->vnode->netfs.inode);
}

static void afs_link_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	if (afs_op_error(op))
		d_drop(op->dentry);
}

static const struct afs_operation_ops afs_link_operation = {
	.issue_afs_rpc	= afs_fs_link,
	.issue_yfs_rpc	= yfs_fs_link,
	.success	= afs_link_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_create_edit_dir,
	.put		= afs_link_put,
};

/*
 * create a hard link between files in an AFS filesystem
 */
static int afs_link(struct dentry *from, struct inode *dir,
		    struct dentry *dentry)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_vnode *vnode = AFS_FS_I(d_inode(from));
	int ret = -ENAMETOOLONG;

	_enter("{%llx:%llu},{%llx:%llu},{%pd}",
	       vnode->fid.vid, vnode->fid.vnode,
	       dvnode->fid.vid, dvnode->fid.vnode,
	       dentry);

	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto error;
	}

	fscache_use_cookie(afs_vnode_cache(dvnode), true);

	ret = afs_validate(vnode, op->key);
	if (ret < 0)
		goto error_op;

	afs_op_set_vnode(op, 0, dvnode);
	afs_op_set_vnode(op, 1, vnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;
	op->file[1].update_ctime = true;

	op->dentry		= dentry;
	op->dentry_2		= from;
	op->ops			= &afs_link_operation;
	op->create.reason	= afs_edit_dir_for_link;
	ret = afs_do_sync_operation(op);
	afs_dir_unuse_cookie(dvnode, ret);
	return ret;

error_op:
	afs_put_operation(op);
	afs_dir_unuse_cookie(dvnode, ret);
error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

static const struct afs_operation_ops afs_symlink_operation = {
	.issue_afs_rpc	= afs_fs_symlink,
	.issue_yfs_rpc	= yfs_fs_symlink,
	.success	= afs_create_success,
	.aborted	= afs_check_for_remote_deletion,
	.edit_dir	= afs_create_edit_dir,
	.put		= afs_create_put,
};

/*
 * create a symlink in an AFS filesystem
 */
static int afs_symlink(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, const char *content)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	int ret;

	_enter("{%llx:%llu},{%pd},%s",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry,
	       content);

	ret = -ENAMETOOLONG;
	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	ret = -EINVAL;
	if (strlen(content) >= AFSPATHMAX)
		goto error;

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto error;
	}

	fscache_use_cookie(afs_vnode_cache(dvnode), true);

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;

	op->dentry		= dentry;
	op->ops			= &afs_symlink_operation;
	op->create.reason	= afs_edit_dir_for_symlink;
	op->create.symlink	= content;
	op->mtime		= current_time(dir);
	ret = afs_do_sync_operation(op);
	afs_dir_unuse_cookie(dvnode, ret);
	return ret;

error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

static void afs_rename_success(struct afs_operation *op)
{
	struct afs_vnode *vnode = AFS_FS_I(d_inode(op->dentry));

	_enter("op=%08x", op->debug_id);

	op->ctime = op->file[0].scb.status.mtime_client;
	afs_check_dir_conflict(op, &op->file[1]);
	afs_vnode_commit_status(op, &op->file[0]);
	if (op->file[1].vnode != op->file[0].vnode) {
		op->ctime = op->file[1].scb.status.mtime_client;
		afs_vnode_commit_status(op, &op->file[1]);
	}

	/* If we're moving a subdir between dirs, we need to update
	 * its DV counter too as the ".." will be altered.
	 */
	if (S_ISDIR(vnode->netfs.inode.i_mode) &&
	    op->file[0].vnode != op->file[1].vnode) {
		u64 new_dv;

		write_seqlock(&vnode->cb_lock);

		new_dv = vnode->status.data_version + 1;
		trace_afs_set_dv(vnode, new_dv);
		vnode->status.data_version = new_dv;
		inode_set_iversion_raw(&vnode->netfs.inode, new_dv);

		write_sequnlock(&vnode->cb_lock);
	}
}

static void afs_rename_edit_dir(struct afs_operation *op)
{
	struct netfs_cache_resources orig_cres = {}, new_cres = {};
	struct afs_vnode_param *orig_dvp = &op->file[0];
	struct afs_vnode_param *new_dvp = &op->file[1];
	struct afs_vnode *orig_dvnode = orig_dvp->vnode;
	struct afs_vnode *new_dvnode = new_dvp->vnode;
	struct afs_vnode *vnode = AFS_FS_I(d_inode(op->dentry));
	struct dentry *old_dentry = op->dentry;
	struct dentry *new_dentry = op->dentry_2;
	struct inode *new_inode;

	_enter("op=%08x", op->debug_id);

	if (op->rename.rehash) {
		d_rehash(op->rename.rehash);
		op->rename.rehash = NULL;
	}

	fscache_begin_write_operation(&orig_cres, afs_vnode_cache(orig_dvnode));
	if (new_dvnode != orig_dvnode)
		fscache_begin_write_operation(&new_cres, afs_vnode_cache(new_dvnode));

	down_write(&orig_dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &orig_dvnode->flags) &&
	    orig_dvnode->status.data_version == orig_dvp->dv_before + orig_dvp->dv_delta)
		afs_edit_dir_remove(orig_dvnode, &old_dentry->d_name,
				    afs_edit_dir_for_rename_0);

	if (new_dvnode != orig_dvnode) {
		up_write(&orig_dvnode->validate_lock);
		down_write(&new_dvnode->validate_lock);
	}

	if (test_bit(AFS_VNODE_DIR_VALID, &new_dvnode->flags) &&
	    new_dvnode->status.data_version == new_dvp->dv_before + new_dvp->dv_delta) {
		if (!op->rename.new_negative)
			afs_edit_dir_remove(new_dvnode, &new_dentry->d_name,
					    afs_edit_dir_for_rename_1);

		afs_edit_dir_add(new_dvnode, &new_dentry->d_name,
				 &vnode->fid, afs_edit_dir_for_rename_2);
	}

	if (S_ISDIR(vnode->netfs.inode.i_mode) &&
	    new_dvnode != orig_dvnode &&
	    test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
		afs_edit_dir_update_dotdot(vnode, new_dvnode,
					   afs_edit_dir_for_rename_sub);

	new_inode = d_inode(new_dentry);
	if (new_inode) {
		spin_lock(&new_inode->i_lock);
		if (S_ISDIR(new_inode->i_mode))
			clear_nlink(new_inode);
		else if (new_inode->i_nlink > 0)
			drop_nlink(new_inode);
		spin_unlock(&new_inode->i_lock);
	}

	/* Now we can update d_fsdata on the dentries to reflect their
	 * new parent's data_version.
	 *
	 * Note that if we ever implement RENAME_EXCHANGE, we'll have
	 * to update both dentries with opposing dir versions.
	 */
	afs_update_dentry_version(op, new_dvp, op->dentry);
	afs_update_dentry_version(op, new_dvp, op->dentry_2);

	d_move(old_dentry, new_dentry);

	up_write(&new_dvnode->validate_lock);
	fscache_end_operation(&orig_cres);
	if (new_dvnode != orig_dvnode)
		fscache_end_operation(&new_cres);
}

static void afs_rename_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	if (op->rename.rehash)
		d_rehash(op->rename.rehash);
	dput(op->rename.tmp);
	if (afs_op_error(op))
		d_rehash(op->dentry);
}

static const struct afs_operation_ops afs_rename_operation = {
	.issue_afs_rpc	= afs_fs_rename,
	.issue_yfs_rpc	= yfs_fs_rename,
	.success	= afs_rename_success,
	.edit_dir	= afs_rename_edit_dir,
	.put		= afs_rename_put,
};

/*
 * rename a file in an AFS filesystem and/or move it between directories
 */
static int afs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		      struct dentry *old_dentry, struct inode *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct afs_operation *op;
	struct afs_vnode *orig_dvnode, *new_dvnode, *vnode;
	int ret;

	if (flags)
		return -EINVAL;

	/* Don't allow silly-rename files be moved around. */
	if (old_dentry->d_flags & DCACHE_NFSFS_RENAMED)
		return -EINVAL;

	vnode = AFS_FS_I(d_inode(old_dentry));
	orig_dvnode = AFS_FS_I(old_dir);
	new_dvnode = AFS_FS_I(new_dir);

	_enter("{%llx:%llu},{%llx:%llu},{%llx:%llu},{%pd}",
	       orig_dvnode->fid.vid, orig_dvnode->fid.vnode,
	       vnode->fid.vid, vnode->fid.vnode,
	       new_dvnode->fid.vid, new_dvnode->fid.vnode,
	       new_dentry);

	op = afs_alloc_operation(NULL, orig_dvnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	fscache_use_cookie(afs_vnode_cache(orig_dvnode), true);
	if (new_dvnode != orig_dvnode)
		fscache_use_cookie(afs_vnode_cache(new_dvnode), true);

	ret = afs_validate(vnode, op->key);
	afs_op_set_error(op, ret);
	if (ret < 0)
		goto error;

	afs_op_set_vnode(op, 0, orig_dvnode);
	afs_op_set_vnode(op, 1, new_dvnode); /* May be same as orig_dvnode */
	op->file[0].dv_delta = 1;
	op->file[1].dv_delta = 1;
	op->file[0].modification = true;
	op->file[1].modification = true;
	op->file[0].update_ctime = true;
	op->file[1].update_ctime = true;

	op->dentry		= old_dentry;
	op->dentry_2		= new_dentry;
	op->rename.new_negative	= d_is_negative(new_dentry);
	op->ops			= &afs_rename_operation;

	/* For non-directories, check whether the target is busy and if so,
	 * make a copy of the dentry and then do a silly-rename.  If the
	 * silly-rename succeeds, the copied dentry is hashed and becomes the
	 * new target.
	 */
	if (d_is_positive(new_dentry) && !d_is_dir(new_dentry)) {
		/* To prevent any new references to the target during the
		 * rename, we unhash the dentry in advance.
		 */
		if (!d_unhashed(new_dentry)) {
			d_drop(new_dentry);
			op->rename.rehash = new_dentry;
		}

		if (d_count(new_dentry) > 2) {
			/* copy the target dentry's name */
			op->rename.tmp = d_alloc(new_dentry->d_parent,
						 &new_dentry->d_name);
			if (!op->rename.tmp) {
				afs_op_nomem(op);
				goto error;
			}

			ret = afs_sillyrename(new_dvnode,
					      AFS_FS_I(d_inode(new_dentry)),
					      new_dentry, op->key);
			if (ret) {
				afs_op_set_error(op, ret);
				goto error;
			}

			op->dentry_2 = op->rename.tmp;
			op->rename.rehash = NULL;
			op->rename.new_negative = true;
		}
	}

	/* This bit is potentially nasty as there's a potential race with
	 * afs_d_revalidate{,_rcu}().  We have to change d_fsdata on the dentry
	 * to reflect it's new parent's new data_version after the op, but
	 * d_revalidate may see old_dentry between the op having taken place
	 * and the version being updated.
	 *
	 * So drop the old_dentry for now to make other threads go through
	 * lookup instead - which we hold a lock against.
	 */
	d_drop(old_dentry);

	ret = afs_do_sync_operation(op);
out:
	afs_dir_unuse_cookie(orig_dvnode, ret);
	if (new_dvnode != orig_dvnode)
		afs_dir_unuse_cookie(new_dvnode, ret);
	return ret;

error:
	ret = afs_put_operation(op);
	goto out;
}

/*
 * Write the file contents to the cache as a single blob.
 */
int afs_single_writepages(struct address_space *mapping,
			  struct writeback_control *wbc)
{
	struct afs_vnode *dvnode = AFS_FS_I(mapping->host);
	struct iov_iter iter;
	bool is_dir = (S_ISDIR(dvnode->netfs.inode.i_mode) &&
		       !test_bit(AFS_VNODE_MOUNTPOINT, &dvnode->flags));
	int ret = 0;

	/* Need to lock to prevent the folio queue and folios from being thrown
	 * away.
	 */
	down_read(&dvnode->validate_lock);

	if (is_dir ?
	    test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) :
	    atomic64_read(&dvnode->cb_expires_at) != AFS_NO_CB_PROMISE) {
		iov_iter_folio_queue(&iter, ITER_SOURCE, dvnode->directory, 0, 0,
				     i_size_read(&dvnode->netfs.inode));
		ret = netfs_writeback_single(mapping, wbc, &iter);
	}

	up_read(&dvnode->validate_lock);
	return ret;
}
