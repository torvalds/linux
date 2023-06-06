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
#include <linux/task_io_accounting_ops.h>
#include "internal.h"
#include "afs_fs.h"
#include "xdr_fs.h"

static struct dentry *afs_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags);
static int afs_dir_open(struct inode *inode, struct file *file);
static int afs_readdir(struct file *file, struct dir_context *ctx);
static int afs_d_revalidate(struct dentry *dentry, unsigned int flags);
static int afs_d_delete(const struct dentry *dentry);
static void afs_d_iput(struct dentry *dentry, struct inode *inode);
static bool afs_lookup_one_filldir(struct dir_context *ctx, const char *name, int nlen,
				  loff_t fpos, u64 ino, unsigned dtype);
static bool afs_lookup_filldir(struct dir_context *ctx, const char *name, int nlen,
			      loff_t fpos, u64 ino, unsigned dtype);
static int afs_create(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode, bool excl);
static int afs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
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
static bool afs_dir_release_folio(struct folio *folio, gfp_t gfp_flags);
static void afs_dir_invalidate_folio(struct folio *folio, size_t offset,
				   size_t length);

static bool afs_dir_dirty_folio(struct address_space *mapping,
		struct folio *folio)
{
	BUG(); /* This should never happen. */
}

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
	.dirty_folio	= afs_dir_dirty_folio,
	.release_folio	= afs_dir_release_folio,
	.invalidate_folio = afs_dir_invalidate_folio,
	.migrate_folio	= filemap_migrate_folio,
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
	bool			found;
	bool			one_only;
	unsigned short		nr_fids;
	struct afs_fid		fids[50];
};

/*
 * Drop the refs that we're holding on the folios we were reading into.  We've
 * got refs on the first nr_pages pages.
 */
static void afs_dir_read_cleanup(struct afs_read *req)
{
	struct address_space *mapping = req->vnode->netfs.inode.i_mapping;
	struct folio *folio;
	pgoff_t last = req->nr_pages - 1;

	XA_STATE(xas, &mapping->i_pages, 0);

	if (unlikely(!req->nr_pages))
		return;

	rcu_read_lock();
	xas_for_each(&xas, folio, last) {
		if (xas_retry(&xas, folio))
			continue;
		BUG_ON(xa_is_value(folio));
		ASSERTCMP(folio_file_mapping(folio), ==, mapping);

		folio_put(folio);
	}

	rcu_read_unlock();
}

/*
 * check that a directory folio is valid
 */
static bool afs_dir_check_folio(struct afs_vnode *dvnode, struct folio *folio,
				loff_t i_size)
{
	union afs_xdr_dir_block *block;
	size_t offset, size;
	loff_t pos;

	/* Determine how many magic numbers there should be in this folio, but
	 * we must take care because the directory may change size under us.
	 */
	pos = folio_pos(folio);
	if (i_size <= pos)
		goto checked;

	size = min_t(loff_t, folio_size(folio), i_size - pos);
	for (offset = 0; offset < size; offset += sizeof(*block)) {
		block = kmap_local_folio(folio, offset);
		if (block->hdr.magic != AFS_DIR_MAGIC) {
			printk("kAFS: %s(%lx): [%llx] bad magic %zx/%zx is %04hx\n",
			       __func__, dvnode->netfs.inode.i_ino,
			       pos, offset, size, ntohs(block->hdr.magic));
			trace_afs_dir_check_failed(dvnode, pos + offset, i_size);
			kunmap_local(block);
			trace_afs_file_error(dvnode, -EIO, afs_file_error_dir_bad_magic);
			goto error;
		}

		/* Make sure each block is NUL terminated so we can reasonably
		 * use string functions on it.  The filenames in the folio
		 * *should* be NUL-terminated anyway.
		 */
		((u8 *)block)[AFS_DIR_BLOCK_SIZE - 1] = 0;

		kunmap_local(block);
	}
checked:
	afs_stat_v(dvnode, n_read_dir);
	return true;

error:
	return false;
}

/*
 * Dump the contents of a directory.
 */
static void afs_dir_dump(struct afs_vnode *dvnode, struct afs_read *req)
{
	union afs_xdr_dir_block *block;
	struct address_space *mapping = dvnode->netfs.inode.i_mapping;
	struct folio *folio;
	pgoff_t last = req->nr_pages - 1;
	size_t offset, size;

	XA_STATE(xas, &mapping->i_pages, 0);

	pr_warn("DIR %llx:%llx f=%llx l=%llx al=%llx\n",
		dvnode->fid.vid, dvnode->fid.vnode,
		req->file_size, req->len, req->actual_len);
	pr_warn("DIR %llx %x %zx %zx\n",
		req->pos, req->nr_pages,
		req->iter->iov_offset,  iov_iter_count(req->iter));

	xas_for_each(&xas, folio, last) {
		if (xas_retry(&xas, folio))
			continue;

		BUG_ON(folio_file_mapping(folio) != mapping);

		size = min_t(loff_t, folio_size(folio), req->actual_len - folio_pos(folio));
		for (offset = 0; offset < size; offset += sizeof(*block)) {
			block = kmap_local_folio(folio, offset);
			pr_warn("[%02lx] %32phN\n", folio_index(folio) + offset, block);
			kunmap_local(block);
		}
	}
}

/*
 * Check all the blocks in a directory.  All the folios are held pinned.
 */
static int afs_dir_check(struct afs_vnode *dvnode, struct afs_read *req)
{
	struct address_space *mapping = dvnode->netfs.inode.i_mapping;
	struct folio *folio;
	pgoff_t last = req->nr_pages - 1;
	int ret = 0;

	XA_STATE(xas, &mapping->i_pages, 0);

	if (unlikely(!req->nr_pages))
		return 0;

	rcu_read_lock();
	xas_for_each(&xas, folio, last) {
		if (xas_retry(&xas, folio))
			continue;

		BUG_ON(folio_file_mapping(folio) != mapping);

		if (!afs_dir_check_folio(dvnode, folio, req->actual_len)) {
			afs_dir_dump(dvnode, req);
			ret = -EIO;
			break;
		}
	}

	rcu_read_unlock();
	return ret;
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
 * Read the directory into the pagecache in one go, scrubbing the previous
 * contents.  The list of folios is returned, pinning them so that they don't
 * get reclaimed during the iteration.
 */
static struct afs_read *afs_read_dir(struct afs_vnode *dvnode, struct key *key)
	__acquires(&dvnode->validate_lock)
{
	struct address_space *mapping = dvnode->netfs.inode.i_mapping;
	struct afs_read *req;
	loff_t i_size;
	int nr_pages, i;
	int ret;
	loff_t remote_size = 0;

	_enter("");

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	refcount_set(&req->usage, 1);
	req->vnode = dvnode;
	req->key = key_get(key);
	req->cleanup = afs_dir_read_cleanup;

expand:
	i_size = i_size_read(&dvnode->netfs.inode);
	if (i_size < remote_size)
	    i_size = remote_size;
	if (i_size < 2048) {
		ret = afs_bad(dvnode, afs_file_error_dir_small);
		goto error;
	}
	if (i_size > 2048 * 1024) {
		trace_afs_file_error(dvnode, -EFBIG, afs_file_error_dir_big);
		ret = -EFBIG;
		goto error;
	}

	_enter("%llu", i_size);

	nr_pages = (i_size + PAGE_SIZE - 1) / PAGE_SIZE;

	req->actual_len = i_size; /* May change */
	req->len = nr_pages * PAGE_SIZE; /* We can ask for more than there is */
	req->data_version = dvnode->status.data_version; /* May change */
	iov_iter_xarray(&req->def_iter, ITER_DEST, &dvnode->netfs.inode.i_mapping->i_pages,
			0, i_size);
	req->iter = &req->def_iter;

	/* Fill in any gaps that we might find where the memory reclaimer has
	 * been at work and pin all the folios.  If there are any gaps, we will
	 * need to reread the entire directory contents.
	 */
	i = req->nr_pages;
	while (i < nr_pages) {
		struct folio *folio;

		folio = filemap_get_folio(mapping, i);
		if (IS_ERR(folio)) {
			if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
				afs_stat_v(dvnode, n_inval);
			folio = __filemap_get_folio(mapping,
						    i, FGP_LOCK | FGP_CREAT,
						    mapping->gfp_mask);
			if (IS_ERR(folio)) {
				ret = PTR_ERR(folio);
				goto error;
			}
			folio_attach_private(folio, (void *)1);
			folio_unlock(folio);
		}

		req->nr_pages += folio_nr_pages(folio);
		i += folio_nr_pages(folio);
	}

	/* If we're going to reload, we need to lock all the pages to prevent
	 * races.
	 */
	ret = -ERESTARTSYS;
	if (down_read_killable(&dvnode->validate_lock) < 0)
		goto error;

	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		goto success;

	up_read(&dvnode->validate_lock);
	if (down_write_killable(&dvnode->validate_lock) < 0)
		goto error;

	if (!test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags)) {
		trace_afs_reload_dir(dvnode);
		ret = afs_fetch_data(dvnode, req);
		if (ret < 0)
			goto error_unlock;

		task_io_account_read(PAGE_SIZE * req->nr_pages);

		if (req->len < req->file_size) {
			/* The content has grown, so we need to expand the
			 * buffer.
			 */
			up_write(&dvnode->validate_lock);
			remote_size = req->file_size;
			goto expand;
		}

		/* Validate the data we just read. */
		ret = afs_dir_check(dvnode, req);
		if (ret < 0)
			goto error_unlock;

		// TODO: Trim excess pages

		set_bit(AFS_VNODE_DIR_VALID, &dvnode->flags);
	}

	downgrade_write(&dvnode->validate_lock);
success:
	return req;

error_unlock:
	up_write(&dvnode->validate_lock);
error:
	afs_put_read(req);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * deal with one block in an AFS directory
 */
static int afs_dir_iterate_block(struct afs_vnode *dvnode,
				 struct dir_context *ctx,
				 union afs_xdr_dir_block *block,
				 unsigned blkoff)
{
	union afs_xdr_dirent *dire;
	unsigned offset, next, curr, nr_slots;
	size_t nlen;
	int tmp;

	_enter("%llx,%x", ctx->pos, blkoff);

	curr = (ctx->pos - blkoff) / sizeof(union afs_xdr_dirent);

	/* walk through the block, an entry at a time */
	for (offset = (blkoff == 0 ? AFS_DIR_RESV_BLOCKS0 : AFS_DIR_RESV_BLOCKS);
	     offset < AFS_DIR_SLOTS_PER_BLOCK;
	     offset = next
	     ) {
		/* skip entries marked unused in the bitmap */
		if (!(block->hdr.bitmap[offset / 8] &
		      (1 << (offset % 8)))) {
			_debug("ENT[%zu.%u]: unused",
			       blkoff / sizeof(union afs_xdr_dir_block), offset);
			next = offset + 1;
			if (offset >= curr)
				ctx->pos = blkoff +
					next * sizeof(union afs_xdr_dirent);
			continue;
		}

		/* got a valid entry */
		dire = &block->dirents[offset];
		nlen = strnlen(dire->u.name,
			       sizeof(*block) -
			       offset * sizeof(union afs_xdr_dirent));
		if (nlen > AFSNAMEMAX - 1) {
			_debug("ENT[%zu]: name too long (len %u/%zu)",
			       blkoff / sizeof(union afs_xdr_dir_block),
			       offset, nlen);
			return afs_bad(dvnode, afs_file_error_dir_name_too_long);
		}

		_debug("ENT[%zu.%u]: %s %zu \"%s\"",
		       blkoff / sizeof(union afs_xdr_dir_block), offset,
		       (offset < curr ? "skip" : "fill"),
		       nlen, dire->u.name);

		nr_slots = afs_dir_calc_slots(nlen);
		next = offset + nr_slots;
		if (next > AFS_DIR_SLOTS_PER_BLOCK) {
			_debug("ENT[%zu.%u]:"
			       " %u extends beyond end dir block"
			       " (len %zu)",
			       blkoff / sizeof(union afs_xdr_dir_block),
			       offset, next, nlen);
			return afs_bad(dvnode, afs_file_error_dir_over_end);
		}

		/* Check that the name-extension dirents are all allocated */
		for (tmp = 1; tmp < nr_slots; tmp++) {
			unsigned int ix = offset + tmp;
			if (!(block->hdr.bitmap[ix / 8] & (1 << (ix % 8)))) {
				_debug("ENT[%zu.u]:"
				       " %u unmarked extension (%u/%u)",
				       blkoff / sizeof(union afs_xdr_dir_block),
				       offset, tmp, nr_slots);
				return afs_bad(dvnode, afs_file_error_dir_unmarked_ext);
			}
		}

		/* skip if starts before the current position */
		if (offset < curr) {
			if (next > curr)
				ctx->pos = blkoff + next * sizeof(union afs_xdr_dirent);
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

		ctx->pos = blkoff + next * sizeof(union afs_xdr_dirent);
	}

	_leave(" = 1 [more]");
	return 1;
}

/*
 * iterate through the data blob that lists the contents of an AFS directory
 */
static int afs_dir_iterate(struct inode *dir, struct dir_context *ctx,
			   struct key *key, afs_dataversion_t *_dir_version)
{
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	union afs_xdr_dir_block *dblock;
	struct afs_read *req;
	struct folio *folio;
	unsigned offset, size;
	int ret;

	_enter("{%lu},%u,,", dir->i_ino, (unsigned)ctx->pos);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dir)->flags)) {
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	req = afs_read_dir(dvnode, key);
	if (IS_ERR(req))
		return PTR_ERR(req);
	*_dir_version = req->data_version;

	/* round the file position up to the next entry boundary */
	ctx->pos += sizeof(union afs_xdr_dirent) - 1;
	ctx->pos &= ~(sizeof(union afs_xdr_dirent) - 1);

	/* walk through the blocks in sequence */
	ret = 0;
	while (ctx->pos < req->actual_len) {
		/* Fetch the appropriate folio from the directory and re-add it
		 * to the LRU.  We have all the pages pinned with an extra ref.
		 */
		folio = __filemap_get_folio(dir->i_mapping, ctx->pos / PAGE_SIZE,
					    FGP_ACCESSED, 0);
		if (IS_ERR(folio)) {
			ret = afs_bad(dvnode, afs_file_error_dir_missing_page);
			break;
		}

		offset = round_down(ctx->pos, sizeof(*dblock)) - folio_file_pos(folio);
		size = min_t(loff_t, folio_size(folio),
			     req->actual_len - folio_file_pos(folio));

		do {
			dblock = kmap_local_folio(folio, offset);
			ret = afs_dir_iterate_block(dvnode, ctx, dblock,
						    folio_file_pos(folio) + offset);
			kunmap_local(dblock);
			if (ret != 1)
				goto out;

		} while (offset += sizeof(*dblock), offset < size);

		ret = 0;
	}

out:
	up_read(&dvnode->validate_lock);
	afs_put_read(req);
	_leave(" = %d", ret);
	return ret;
}

/*
 * read an AFS directory
 */
static int afs_readdir(struct file *file, struct dir_context *ctx)
{
	afs_dataversion_t dir_version;

	return afs_dir_iterate(file_inode(file), ctx, afs_file_key(file),
			       &dir_version);
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
static int afs_do_lookup_one(struct inode *dir, struct dentry *dentry,
			     struct afs_fid *fid, struct key *key,
			     afs_dataversion_t *_dir_version)
{
	struct afs_super_info *as = dir->i_sb->s_fs_info;
	struct afs_lookup_one_cookie cookie = {
		.ctx.actor = afs_lookup_one_filldir,
		.name = dentry->d_name,
		.fid.vid = as->volume->vid
	};
	int ret;

	_enter("{%lu},%p{%pd},", dir->i_ino, dentry, dentry);

	/* search the directory */
	ret = afs_dir_iterate(dir, &cookie.ctx, key, _dir_version);
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

	if (cookie->found) {
		if (cookie->nr_fids < 50) {
			cookie->fids[cookie->nr_fids].vnode	= ino;
			cookie->fids[cookie->nr_fids].unique	= dtype;
			cookie->nr_fids++;
		}
	} else if (cookie->name.len == nlen &&
		   memcmp(cookie->name.name, name, nlen) == 0) {
		cookie->fids[1].vnode	= ino;
		cookie->fids[1].unique	= dtype;
		cookie->found = 1;
		if (cookie->one_only)
			return false;
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
				op->ac.abort_code = abort_code;
				op->error = afs_abort_to_error(abort_code);
			}
			break;

		case 1:
			vp = &op->file[1];
			break;

		default:
			vp = &op->more_files[i - 2];
			break;
		}

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
static struct inode *afs_do_lookup(struct inode *dir, struct dentry *dentry,
				   struct key *key)
{
	struct afs_lookup_cookie *cookie;
	struct afs_vnode_param *vp;
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir), *vnode;
	struct inode *inode = NULL, *ti;
	afs_dataversion_t data_version = READ_ONCE(dvnode->status.data_version);
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
	cookie->nr_fids = 2; /* slot 0 is saved for the fid we actually want
			      * and slot 1 for the directory */

	if (!afs_server_supports_ibulk(dvnode))
		cookie->one_only = true;

	/* search the directory */
	ret = afs_dir_iterate(dir, &cookie->ctx, key, &data_version);
	if (ret < 0)
		goto out;

	dentry->d_fsdata = (void *)(unsigned long)data_version;

	ret = -ENOENT;
	if (!cookie->found)
		goto out;

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
	op->error = -ENOMEM;
	if (op->nr_files > 2) {
		op->more_files = kvcalloc(op->nr_files - 2,
					  sizeof(struct afs_vnode_param),
					  GFP_KERNEL);
		if (!op->more_files)
			goto out_op;

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
	op->error = -ENOTSUPP;
	if (!cookie->one_only) {
		op->ops = &afs_inline_bulk_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}

	if (op->error == -ENOTSUPP) {
		/* We could try FS.BulkStatus next, but this aborts the entire
		 * op if any of the lookups fails - so, for the moment, revert
		 * to FS.FetchStatus for op->file[1].
		 */
		op->fetch_status.which = 1;
		op->ops = &afs_lookup_fetch_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}
	inode = ERR_PTR(op->error);

out_op:
	if (op->error == 0) {
		inode = &op->file[1].vnode->netfs.inode;
		op->file[1].vnode = NULL;
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
static struct dentry *afs_lookup_atsys(struct inode *dir, struct dentry *dentry,
				       struct key *key)
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
	key_put(key);
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
	struct key *key;
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

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		_leave(" = %ld [key]", PTR_ERR(key));
		return ERR_CAST(key);
	}

	ret = afs_validate(dvnode, key);
	if (ret < 0) {
		key_put(key);
		_leave(" = %d [val]", ret);
		return ERR_PTR(ret);
	}

	if (dentry->d_name.len >= 4 &&
	    dentry->d_name.name[dentry->d_name.len - 4] == '@' &&
	    dentry->d_name.name[dentry->d_name.len - 3] == 's' &&
	    dentry->d_name.name[dentry->d_name.len - 2] == 'y' &&
	    dentry->d_name.name[dentry->d_name.len - 1] == 's')
		return afs_lookup_atsys(dir, dentry, key);

	afs_stat_v(dvnode, n_lookup);
	inode = afs_do_lookup(dir, dentry, key);
	key_put(key);
	if (inode == ERR_PTR(-ENOENT))
		inode = afs_try_auto_mntpt(dentry, dir);

	if (!IS_ERR_OR_NULL(inode))
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
static int afs_d_revalidate_rcu(struct dentry *dentry)
{
	struct afs_vnode *dvnode;
	struct dentry *parent;
	struct inode *dir;
	long dir_version, de_version;

	_enter("%p", dentry);

	/* Check the parent directory is still valid first. */
	parent = READ_ONCE(dentry->d_parent);
	dir = d_inode_rcu(parent);
	if (!dir)
		return -ECHILD;
	dvnode = AFS_FS_I(dir);
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
static int afs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct afs_vnode *vnode, *dir;
	struct afs_fid fid;
	struct dentry *parent;
	struct inode *inode;
	struct key *key;
	afs_dataversion_t dir_version, invalid_before;
	long de_version;
	int ret;

	if (flags & LOOKUP_RCU)
		return afs_d_revalidate_rcu(dentry);

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

	/* Hold the parent dentry so we can peer at it */
	parent = dget_parent(dentry);
	dir = AFS_FS_I(d_inode(parent));

	/* validate the parent directory */
	afs_validate(dir, key);

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
	ret = afs_do_lookup_one(&dir->netfs.inode, dentry, &fid, key, &dir_version);
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
		_debug("failed to iterate dir %pd: %d",
		       parent, ret);
		goto not_found;
	}

out_valid:
	dentry->d_fsdata = (void *)(unsigned long)dir_version;
out_valid_noupdate:
	dput(parent);
	key_put(key);
	_leave(" = 1 [valid]");
	return 1;

not_found:
	_debug("dropping dentry %pd2", dentry);
	dput(parent);
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

	switch (op->ac.abort_code) {
	case VNOVNODE:
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		afs_break_callback(vnode, afs_cb_break_for_deleted);
	}
}

/*
 * Create a new inode for create/mkdir/symlink
 */
static void afs_vnode_new_inode(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[1];
	struct afs_vnode *vnode;
	struct inode *inode;

	_enter("");

	ASSERTCMP(op->error, ==, 0);

	inode = afs_iget(op, vp);
	if (IS_ERR(inode)) {
		/* ENOMEM or EINTR at a really inconvenient time - just abandon
		 * the new directory on the server.
		 */
		op->error = PTR_ERR(inode);
		return;
	}

	vnode = AFS_FS_I(inode);
	set_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	if (!op->error)
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
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode_param *vp = &op->file[1];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);

	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_add(dvnode, &op->dentry->d_name, &vp->fid,
				 op->create.reason);
	up_write(&dvnode->validate_lock);
}

static void afs_create_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);

	if (op->error)
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
static int afs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		     struct dentry *dentry, umode_t mode)
{
	struct afs_operation *op;
	struct afs_vnode *dvnode = AFS_FS_I(dir);

	_enter("{%llx:%llu},{%pd},%ho",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	op = afs_alloc_operation(NULL, dvnode->volume);
	if (IS_ERR(op)) {
		d_drop(dentry);
		return PTR_ERR(op);
	}

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;
	op->dentry	= dentry;
	op->create.mode	= S_IFDIR | mode;
	op->create.reason = afs_edit_dir_for_mkdir;
	op->ops		= &afs_mkdir_operation;
	return afs_do_sync_operation(op);
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
		clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
		clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
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
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);
	afs_dir_remove_subdir(op->dentry);

	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_remove(dvnode, &op->dentry->d_name,
				    afs_edit_dir_for_rmdir);
	up_write(&dvnode->validate_lock);
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

	return afs_do_sync_operation(op);

error:
	return afs_put_operation(op);
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

	if (op->error != 0 ||
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
			op->error = ret;
	}

	_debug("nlink %d [val %d]", vnode->netfs.inode.i_nlink, op->error);
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
	struct afs_vnode_param *dvp = &op->file[0];
	struct afs_vnode *dvnode = dvp->vnode;

	_enter("op=%08x", op->debug_id);
	down_write(&dvnode->validate_lock);
	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags) &&
	    dvnode->status.data_version == dvp->dv_before + dvp->dv_delta)
		afs_edit_dir_remove(dvnode, &op->dentry->d_name,
				    afs_edit_dir_for_unlink);
	up_write(&dvnode->validate_lock);
}

static void afs_unlink_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	if (op->unlink.need_rehash && op->error < 0 && op->error != -ENOENT)
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

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;

	/* Try to make sure we have a callback promise on the victim. */
	ret = afs_validate(vnode, op->key);
	if (ret < 0) {
		op->error = ret;
		goto error;
	}

	spin_lock(&dentry->d_lock);
	if (d_count(dentry) > 1) {
		spin_unlock(&dentry->d_lock);
		/* Start asynchronous writeout of the inode */
		write_inode_now(d_inode(dentry), 0);
		op->error = afs_sillyrename(dvnode, vnode, dentry, op->key);
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
	if (op->error == 0 && (op->flags & AFS_OPERATION_DIR_CONFLICT)) {
		op->file[1].update_ctime = false;
		op->fetch_status.which = 1;
		op->ops = &afs_fetch_status_operation;
		afs_begin_vnode_operation(op);
		afs_wait_for_operation(op);
	}

	return afs_put_operation(op);

error:
	return afs_put_operation(op);
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

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;
	op->file[0].modification = true;
	op->file[0].update_ctime = true;

	op->dentry	= dentry;
	op->create.mode	= S_IFREG | mode;
	op->create.reason = afs_edit_dir_for_create;
	op->ops		= &afs_create_operation;
	return afs_do_sync_operation(op);

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
	if (op->error)
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
	return afs_do_sync_operation(op);

error_op:
	afs_put_operation(op);
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

	afs_op_set_vnode(op, 0, dvnode);
	op->file[0].dv_delta = 1;

	op->dentry		= dentry;
	op->ops			= &afs_symlink_operation;
	op->create.reason	= afs_edit_dir_for_symlink;
	op->create.symlink	= content;
	return afs_do_sync_operation(op);

error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

static void afs_rename_success(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);

	op->ctime = op->file[0].scb.status.mtime_client;
	afs_check_dir_conflict(op, &op->file[1]);
	afs_vnode_commit_status(op, &op->file[0]);
	if (op->file[1].vnode != op->file[0].vnode) {
		op->ctime = op->file[1].scb.status.mtime_client;
		afs_vnode_commit_status(op, &op->file[1]);
	}
}

static void afs_rename_edit_dir(struct afs_operation *op)
{
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
}

static void afs_rename_put(struct afs_operation *op)
{
	_enter("op=%08x", op->debug_id);
	if (op->rename.rehash)
		d_rehash(op->rename.rehash);
	dput(op->rename.tmp);
	if (op->error)
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

	ret = afs_validate(vnode, op->key);
	op->error = ret;
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
				op->error = -ENOMEM;
				goto error;
			}

			ret = afs_sillyrename(new_dvnode,
					      AFS_FS_I(d_inode(new_dentry)),
					      new_dentry, op->key);
			if (ret) {
				op->error = ret;
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

	return afs_do_sync_operation(op);

error:
	return afs_put_operation(op);
}

/*
 * Release a directory folio and clean up its private state if it's not busy
 * - return true if the folio can now be released, false if not
 */
static bool afs_dir_release_folio(struct folio *folio, gfp_t gfp_flags)
{
	struct afs_vnode *dvnode = AFS_FS_I(folio_inode(folio));

	_enter("{{%llx:%llu}[%lu]}", dvnode->fid.vid, dvnode->fid.vnode, folio_index(folio));

	folio_detach_private(folio);

	/* The directory will need reloading. */
	if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_stat_v(dvnode, n_relpg);
	return true;
}

/*
 * Invalidate part or all of a folio.
 */
static void afs_dir_invalidate_folio(struct folio *folio, size_t offset,
				   size_t length)
{
	struct afs_vnode *dvnode = AFS_FS_I(folio_inode(folio));

	_enter("{%lu},%zu,%zu", folio->index, offset, length);

	BUG_ON(!folio_test_locked(folio));

	/* The directory will need reloading. */
	if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_stat_v(dvnode, n_inval);

	/* we clean up only if the entire folio is being invalidated */
	if (offset == 0 && length == folio_size(folio))
		folio_detach_private(folio);
}
