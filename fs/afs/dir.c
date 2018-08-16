/* dir.c: AFS filesystem directory handling
 *
 * Copyright (C) 2002, 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
#include "xdr_fs.h"

static struct dentry *afs_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags);
static int afs_dir_open(struct inode *inode, struct file *file);
static int afs_readdir(struct file *file, struct dir_context *ctx);
static int afs_d_revalidate(struct dentry *dentry, unsigned int flags);
static int afs_d_delete(const struct dentry *dentry);
static int afs_lookup_one_filldir(struct dir_context *ctx, const char *name, int nlen,
				  loff_t fpos, u64 ino, unsigned dtype);
static int afs_lookup_filldir(struct dir_context *ctx, const char *name, int nlen,
			      loff_t fpos, u64 ino, unsigned dtype);
static int afs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		      bool excl);
static int afs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int afs_rmdir(struct inode *dir, struct dentry *dentry);
static int afs_unlink(struct inode *dir, struct dentry *dentry);
static int afs_link(struct dentry *from, struct inode *dir,
		    struct dentry *dentry);
static int afs_symlink(struct inode *dir, struct dentry *dentry,
		       const char *content);
static int afs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry,
		      unsigned int flags);
static int afs_dir_releasepage(struct page *page, gfp_t gfp_flags);
static void afs_dir_invalidatepage(struct page *page, unsigned int offset,
				   unsigned int length);

static int afs_dir_set_page_dirty(struct page *page)
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
	.listxattr	= afs_listxattr,
};

const struct address_space_operations afs_dir_aops = {
	.set_page_dirty	= afs_dir_set_page_dirty,
	.releasepage	= afs_dir_releasepage,
	.invalidatepage	= afs_dir_invalidatepage,
};

const struct dentry_operations afs_fs_dentry_operations = {
	.d_revalidate	= afs_d_revalidate,
	.d_delete	= afs_d_delete,
	.d_release	= afs_d_release,
	.d_automount	= afs_d_automount,
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
	struct afs_file_status	*statuses;
	struct afs_callback	*callbacks;
	struct afs_fid		fids[50];
};

/*
 * check that a directory page is valid
 */
static bool afs_dir_check_page(struct afs_vnode *dvnode, struct page *page,
			       loff_t i_size)
{
	struct afs_xdr_dir_page *dbuf;
	loff_t latter, off;
	int tmp, qty;

	/* Determine how many magic numbers there should be in this page, but
	 * we must take care because the directory may change size under us.
	 */
	off = page_offset(page);
	if (i_size <= off)
		goto checked;

	latter = i_size - off;
	if (latter >= PAGE_SIZE)
		qty = PAGE_SIZE;
	else
		qty = latter;
	qty /= sizeof(union afs_xdr_dir_block);

	/* check them */
	dbuf = kmap(page);
	for (tmp = 0; tmp < qty; tmp++) {
		if (dbuf->blocks[tmp].hdr.magic != AFS_DIR_MAGIC) {
			printk("kAFS: %s(%lx): bad magic %d/%d is %04hx\n",
			       __func__, dvnode->vfs_inode.i_ino, tmp, qty,
			       ntohs(dbuf->blocks[tmp].hdr.magic));
			trace_afs_dir_check_failed(dvnode, off, i_size);
			kunmap(page);
			goto error;
		}

		/* Make sure each block is NUL terminated so we can reasonably
		 * use string functions on it.  The filenames in the page
		 * *should* be NUL-terminated anyway.
		 */
		((u8 *)&dbuf->blocks[tmp])[AFS_DIR_BLOCK_SIZE - 1] = 0;
	}

	kunmap(page);

checked:
	afs_stat_v(dvnode, n_read_dir);
	return true;

error:
	return false;
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
 * contents.  The list of pages is returned, pinning them so that they don't
 * get reclaimed during the iteration.
 */
static struct afs_read *afs_read_dir(struct afs_vnode *dvnode, struct key *key)
	__acquires(&dvnode->validate_lock)
{
	struct afs_read *req;
	loff_t i_size;
	int nr_pages, nr_inline, i, n;
	int ret = -ENOMEM;

retry:
	i_size = i_size_read(&dvnode->vfs_inode);
	if (i_size < 2048)
		return ERR_PTR(-EIO);
	if (i_size > 2048 * 1024)
		return ERR_PTR(-EFBIG);

	_enter("%llu", i_size);

	/* Get a request record to hold the page list.  We want to hold it
	 * inline if we can, but we don't want to make an order 1 allocation.
	 */
	nr_pages = (i_size + PAGE_SIZE - 1) / PAGE_SIZE;
	nr_inline = nr_pages;
	if (nr_inline > (PAGE_SIZE - sizeof(*req)) / sizeof(struct page *))
		nr_inline = 0;

	req = kzalloc(sizeof(*req) + sizeof(struct page *) * nr_inline,
		      GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	refcount_set(&req->usage, 1);
	req->nr_pages = nr_pages;
	req->actual_len = i_size; /* May change */
	req->len = nr_pages * PAGE_SIZE; /* We can ask for more than there is */
	req->data_version = dvnode->status.data_version; /* May change */
	if (nr_inline > 0) {
		req->pages = req->array;
	} else {
		req->pages = kcalloc(nr_pages, sizeof(struct page *),
				     GFP_KERNEL);
		if (!req->pages)
			goto error;
	}

	/* Get a list of all the pages that hold or will hold the directory
	 * content.  We need to fill in any gaps that we might find where the
	 * memory reclaimer has been at work.  If there are any gaps, we will
	 * need to reread the entire directory contents.
	 */
	i = 0;
	do {
		n = find_get_pages_contig(dvnode->vfs_inode.i_mapping, i,
					  req->nr_pages - i,
					  req->pages + i);
		_debug("find %u at %u/%u", n, i, req->nr_pages);
		if (n == 0) {
			gfp_t gfp = dvnode->vfs_inode.i_mapping->gfp_mask;

			if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
				afs_stat_v(dvnode, n_inval);

			ret = -ENOMEM;
			req->pages[i] = __page_cache_alloc(gfp);
			if (!req->pages[i])
				goto error;
			ret = add_to_page_cache_lru(req->pages[i],
						    dvnode->vfs_inode.i_mapping,
						    i, gfp);
			if (ret < 0)
				goto error;

			set_page_private(req->pages[i], 1);
			SetPagePrivate(req->pages[i]);
			unlock_page(req->pages[i]);
			i++;
		} else {
			i += n;
		}
	} while (i < req->nr_pages);

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
		ret = afs_fetch_data(dvnode, key, req);
		if (ret < 0)
			goto error_unlock;

		task_io_account_read(PAGE_SIZE * req->nr_pages);

		if (req->len < req->file_size)
			goto content_has_grown;

		/* Validate the data we just read. */
		ret = -EIO;
		for (i = 0; i < req->nr_pages; i++)
			if (!afs_dir_check_page(dvnode, req->pages[i],
						req->actual_len))
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

content_has_grown:
	up_write(&dvnode->validate_lock);
	afs_put_read(req);
	goto retry;
}

/*
 * deal with one block in an AFS directory
 */
static int afs_dir_iterate_block(struct dir_context *ctx,
				 union afs_xdr_dir_block *block,
				 unsigned blkoff)
{
	union afs_xdr_dirent *dire;
	unsigned offset, next, curr;
	size_t nlen;
	int tmp;

	_enter("%u,%x,%p,,",(unsigned)ctx->pos,blkoff,block);

	curr = (ctx->pos - blkoff) / sizeof(union afs_xdr_dirent);

	/* walk through the block, an entry at a time */
	for (offset = (blkoff == 0 ? AFS_DIR_RESV_BLOCKS0 : AFS_DIR_RESV_BLOCKS);
	     offset < AFS_DIR_SLOTS_PER_BLOCK;
	     offset = next
	     ) {
		next = offset + 1;

		/* skip entries marked unused in the bitmap */
		if (!(block->hdr.bitmap[offset / 8] &
		      (1 << (offset % 8)))) {
			_debug("ENT[%zu.%u]: unused",
			       blkoff / sizeof(union afs_xdr_dir_block), offset);
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

		_debug("ENT[%zu.%u]: %s %zu \"%s\"",
		       blkoff / sizeof(union afs_xdr_dir_block), offset,
		       (offset < curr ? "skip" : "fill"),
		       nlen, dire->u.name);

		/* work out where the next possible entry is */
		for (tmp = nlen; tmp > 15; tmp -= sizeof(union afs_xdr_dirent)) {
			if (next >= AFS_DIR_SLOTS_PER_BLOCK) {
				_debug("ENT[%zu.%u]:"
				       " %u travelled beyond end dir block"
				       " (len %u/%zu)",
				       blkoff / sizeof(union afs_xdr_dir_block),
				       offset, next, tmp, nlen);
				return -EIO;
			}
			if (!(block->hdr.bitmap[next / 8] &
			      (1 << (next % 8)))) {
				_debug("ENT[%zu.%u]:"
				       " %u unmarked extension (len %u/%zu)",
				       blkoff / sizeof(union afs_xdr_dir_block),
				       offset, next, tmp, nlen);
				return -EIO;
			}

			_debug("ENT[%zu.%u]: ext %u/%zu",
			       blkoff / sizeof(union afs_xdr_dir_block),
			       next, tmp, nlen);
			next++;
		}

		/* skip if starts before the current position */
		if (offset < curr)
			continue;

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
			   struct key *key)
{
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_xdr_dir_page *dbuf;
	union afs_xdr_dir_block *dblock;
	struct afs_read *req;
	struct page *page;
	unsigned blkoff, limit;
	int ret;

	_enter("{%lu},%u,,", dir->i_ino, (unsigned)ctx->pos);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dir)->flags)) {
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	req = afs_read_dir(dvnode, key);
	if (IS_ERR(req))
		return PTR_ERR(req);

	/* round the file position up to the next entry boundary */
	ctx->pos += sizeof(union afs_xdr_dirent) - 1;
	ctx->pos &= ~(sizeof(union afs_xdr_dirent) - 1);

	/* walk through the blocks in sequence */
	ret = 0;
	while (ctx->pos < req->actual_len) {
		blkoff = ctx->pos & ~(sizeof(union afs_xdr_dir_block) - 1);

		/* Fetch the appropriate page from the directory and re-add it
		 * to the LRU.
		 */
		page = req->pages[blkoff / PAGE_SIZE];
		if (!page) {
			ret = -EIO;
			break;
		}
		mark_page_accessed(page);

		limit = blkoff & ~(PAGE_SIZE - 1);

		dbuf = kmap(page);

		/* deal with the individual blocks stashed on this page */
		do {
			dblock = &dbuf->blocks[(blkoff % PAGE_SIZE) /
					       sizeof(union afs_xdr_dir_block)];
			ret = afs_dir_iterate_block(ctx, dblock, blkoff);
			if (ret != 1) {
				kunmap(page);
				goto out;
			}

			blkoff += sizeof(union afs_xdr_dir_block);

		} while (ctx->pos < dir->i_size && blkoff < limit);

		kunmap(page);
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
	return afs_dir_iterate(file_inode(file), ctx, afs_file_key(file));
}

/*
 * Search the directory for a single name
 * - if afs_dir_iterate_block() spots this function, it'll pass the FID
 *   uniquifier through dtype
 */
static int afs_lookup_one_filldir(struct dir_context *ctx, const char *name,
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
		_leave(" = 0 [no]");
		return 0;
	}

	cookie->fid.vnode = ino;
	cookie->fid.unique = dtype;
	cookie->found = 1;

	_leave(" = -1 [found]");
	return -1;
}

/*
 * Do a lookup of a single name in a directory
 * - just returns the FID the dentry name maps to if found
 */
static int afs_do_lookup_one(struct inode *dir, struct dentry *dentry,
			     struct afs_fid *fid, struct key *key)
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
	ret = afs_dir_iterate(dir, &cookie.ctx, key);
	if (ret < 0) {
		_leave(" = %d [iter]", ret);
		return ret;
	}

	ret = -ENOENT;
	if (!cookie.found) {
		_leave(" = -ENOENT [not found]");
		return -ENOENT;
	}

	*fid = cookie.fid;
	_leave(" = 0 { vn=%u u=%u }", fid->vnode, fid->unique);
	return 0;
}

/*
 * search the directory for a name
 * - if afs_dir_iterate_block() spots this function, it'll pass the FID
 *   uniquifier through dtype
 */
static int afs_lookup_filldir(struct dir_context *ctx, const char *name,
			      int nlen, loff_t fpos, u64 ino, unsigned dtype)
{
	struct afs_lookup_cookie *cookie =
		container_of(ctx, struct afs_lookup_cookie, ctx);
	int ret;

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
		cookie->fids[0].vnode	= ino;
		cookie->fids[0].unique	= dtype;
		cookie->found = 1;
		if (cookie->one_only)
			return -1;
	}

	ret = cookie->nr_fids >= 50 ? -1 : 0;
	_leave(" = %d", ret);
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
	struct afs_cb_interest *cbi = NULL;
	struct afs_super_info *as = dir->i_sb->s_fs_info;
	struct afs_iget_data data;
	struct afs_fs_cursor fc;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct inode *inode = NULL;
	int ret, i;

	_enter("{%lu},%p{%pd},", dir->i_ino, dentry, dentry);

	cookie = kzalloc(sizeof(struct afs_lookup_cookie), GFP_KERNEL);
	if (!cookie)
		return ERR_PTR(-ENOMEM);

	cookie->ctx.actor = afs_lookup_filldir;
	cookie->name = dentry->d_name;
	cookie->nr_fids = 1; /* slot 0 is saved for the fid we actually want */

	read_seqlock_excl(&dvnode->cb_lock);
	if (dvnode->cb_interest &&
	    dvnode->cb_interest->server &&
	    test_bit(AFS_SERVER_FL_NO_IBULK, &dvnode->cb_interest->server->flags))
		cookie->one_only = true;
	read_sequnlock_excl(&dvnode->cb_lock);

	for (i = 0; i < 50; i++)
		cookie->fids[i].vid = as->volume->vid;

	/* search the directory */
	ret = afs_dir_iterate(dir, &cookie->ctx, key);
	if (ret < 0) {
		inode = ERR_PTR(ret);
		goto out;
	}

	inode = ERR_PTR(-ENOENT);
	if (!cookie->found)
		goto out;

	/* Check to see if we already have an inode for the primary fid. */
	data.volume = dvnode->volume;
	data.fid = cookie->fids[0];
	inode = ilookup5(dir->i_sb, cookie->fids[0].vnode, afs_iget5_test, &data);
	if (inode)
		goto out;

	/* Need space for examining all the selected files */
	inode = ERR_PTR(-ENOMEM);
	cookie->statuses = kcalloc(cookie->nr_fids, sizeof(struct afs_file_status),
				   GFP_KERNEL);
	if (!cookie->statuses)
		goto out;

	cookie->callbacks = kcalloc(cookie->nr_fids, sizeof(struct afs_callback),
				    GFP_KERNEL);
	if (!cookie->callbacks)
		goto out_s;

	/* Try FS.InlineBulkStatus first.  Abort codes for the individual
	 * lookups contained therein are stored in the reply without aborting
	 * the whole operation.
	 */
	if (cookie->one_only)
		goto no_inline_bulk_status;

	inode = ERR_PTR(-ERESTARTSYS);
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			if (test_bit(AFS_SERVER_FL_NO_IBULK,
				      &fc.cbi->server->flags)) {
				fc.ac.abort_code = RX_INVALID_OPERATION;
				fc.ac.error = -ECONNABORTED;
				break;
			}
			afs_fs_inline_bulk_status(&fc,
						  afs_v2net(dvnode),
						  cookie->fids,
						  cookie->statuses,
						  cookie->callbacks,
						  cookie->nr_fids, NULL);
		}

		if (fc.ac.error == 0)
			cbi = afs_get_cb_interest(fc.cbi);
		if (fc.ac.abort_code == RX_INVALID_OPERATION)
			set_bit(AFS_SERVER_FL_NO_IBULK, &fc.cbi->server->flags);
		inode = ERR_PTR(afs_end_vnode_operation(&fc));
	}

	if (!IS_ERR(inode))
		goto success;
	if (fc.ac.abort_code != RX_INVALID_OPERATION)
		goto out_c;

no_inline_bulk_status:
	/* We could try FS.BulkStatus next, but this aborts the entire op if
	 * any of the lookups fails - so, for the moment, revert to
	 * FS.FetchStatus for just the primary fid.
	 */
	cookie->nr_fids = 1;
	inode = ERR_PTR(-ERESTARTSYS);
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			afs_fs_fetch_status(&fc,
					    afs_v2net(dvnode),
					    cookie->fids,
					    cookie->statuses,
					    cookie->callbacks,
					    NULL);
		}

		if (fc.ac.error == 0)
			cbi = afs_get_cb_interest(fc.cbi);
		inode = ERR_PTR(afs_end_vnode_operation(&fc));
	}

	if (IS_ERR(inode))
		goto out_c;

	for (i = 0; i < cookie->nr_fids; i++)
		cookie->statuses[i].abort_code = 0;

success:
	/* Turn all the files into inodes and save the first one - which is the
	 * one we actually want.
	 */
	if (cookie->statuses[0].abort_code != 0)
		inode = ERR_PTR(afs_abort_to_error(cookie->statuses[0].abort_code));

	for (i = 0; i < cookie->nr_fids; i++) {
		struct inode *ti;

		if (cookie->statuses[i].abort_code != 0)
			continue;

		ti = afs_iget(dir->i_sb, key, &cookie->fids[i],
			      &cookie->statuses[i],
			      &cookie->callbacks[i],
			      cbi);
		if (i == 0) {
			inode = ti;
		} else {
			if (!IS_ERR(ti))
				iput(ti);
		}
	}

out_c:
	afs_put_cb_interest(afs_v2net(dvnode), cbi);
	kfree(cookie->callbacks);
out_s:
	kfree(cookie->statuses);
out:
	kfree(cookie);
	return inode;
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
	struct inode *inode;
	struct dentry *d;
	struct key *key;
	int ret;

	_enter("{%x:%u},%p{%pd},",
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
	if (inode == ERR_PTR(-ENOENT)) {
		inode = afs_try_auto_mntpt(dentry, dir);
	} else {
		dentry->d_fsdata =
			(void *)(unsigned long)dvnode->status.data_version;
	}
	d = d_splice_alias(inode, dentry);
	if (!IS_ERR_OR_NULL(d))
		d->d_fsdata = dentry->d_fsdata;
	return d;
}

/*
 * check that a dentry lookup hit has found a valid entry
 * - NOTE! the hit can be a negative hit too, so we can't assume we have an
 *   inode
 */
static int afs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct afs_vnode *vnode, *dir;
	struct afs_fid uninitialized_var(fid);
	struct dentry *parent;
	struct inode *inode;
	struct key *key;
	long dir_version, de_version;
	int ret;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (d_really_is_positive(dentry)) {
		vnode = AFS_FS_I(d_inode(dentry));
		_enter("{v={%x:%u} n=%pd fl=%lx},",
		       vnode->fid.vid, vnode->fid.vnode, dentry,
		       vnode->flags);
	} else {
		_enter("{neg n=%pd}", dentry);
	}

	key = afs_request_key(AFS_FS_S(dentry->d_sb)->volume->cell);
	if (IS_ERR(key))
		key = NULL;

	if (d_really_is_positive(dentry)) {
		inode = d_inode(dentry);
		if (inode) {
			vnode = AFS_FS_I(inode);
			afs_validate(vnode, key);
			if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
				goto out_bad;
		}
	}

	/* lock down the parent dentry so we can peer at it */
	parent = dget_parent(dentry);
	dir = AFS_FS_I(d_inode(parent));

	/* validate the parent directory */
	afs_validate(dir, key);

	if (test_bit(AFS_VNODE_DELETED, &dir->flags)) {
		_debug("%pd: parent dir deleted", dentry);
		goto out_bad_parent;
	}

	/* We only need to invalidate a dentry if the server's copy changed
	 * behind our back.  If we made the change, it's no problem.  Note that
	 * on a 32-bit system, we only have 32 bits in the dentry to store the
	 * version.
	 */
	dir_version = (long)dir->status.data_version;
	de_version = (long)dentry->d_fsdata;
	if (de_version == dir_version)
		goto out_valid;

	dir_version = (long)dir->invalid_before;
	if (de_version - dir_version >= 0)
		goto out_valid;

	_debug("dir modified");
	afs_stat_v(dir, n_reval);

	/* search the directory for this vnode */
	ret = afs_do_lookup_one(&dir->vfs_inode, dentry, &fid, key);
	switch (ret) {
	case 0:
		/* the filename maps to something */
		if (d_really_is_negative(dentry))
			goto out_bad_parent;
		inode = d_inode(dentry);
		if (is_bad_inode(inode)) {
			printk("kAFS: afs_d_revalidate: %pd2 has bad inode\n",
			       dentry);
			goto out_bad_parent;
		}

		vnode = AFS_FS_I(inode);

		/* if the vnode ID has changed, then the dirent points to a
		 * different file */
		if (fid.vnode != vnode->fid.vnode) {
			_debug("%pd: dirent changed [%u != %u]",
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
			       vnode->vfs_inode.i_generation);
			write_seqlock(&vnode->cb_lock);
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			write_sequnlock(&vnode->cb_lock);
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
		goto out_bad_parent;
	}

out_valid:
	dentry->d_fsdata = (void *)dir_version;
	dput(parent);
	key_put(key);
	_leave(" = 1 [valid]");
	return 1;

	/* the dirent, if it exists, now points to a different vnode */
not_found:
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_NFSFS_RENAMED;
	spin_unlock(&dentry->d_lock);

out_bad_parent:
	_debug("dropping dentry %pd2", dentry);
	dput(parent);
out_bad:
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
 * handle dentry release
 */
void afs_d_release(struct dentry *dentry)
{
	_enter("%pd", dentry);
}

/*
 * Create a new inode for create/mkdir/symlink
 */
static void afs_vnode_new_inode(struct afs_fs_cursor *fc,
				struct dentry *new_dentry,
				struct afs_fid *newfid,
				struct afs_file_status *newstatus,
				struct afs_callback *newcb)
{
	struct afs_vnode *vnode;
	struct inode *inode;

	if (fc->ac.error < 0)
		return;

	d_drop(new_dentry);

	inode = afs_iget(fc->vnode->vfs_inode.i_sb, fc->key,
			 newfid, newstatus, newcb, fc->cbi);
	if (IS_ERR(inode)) {
		/* ENOMEM or EINTR at a really inconvenient time - just abandon
		 * the new directory on the server.
		 */
		fc->ac.error = PTR_ERR(inode);
		return;
	}

	vnode = AFS_FS_I(inode);
	set_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	d_add(new_dentry, inode);
}

/*
 * create a directory on an AFS filesystem
 */
static int afs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct afs_file_status newstatus;
	struct afs_fs_cursor fc;
	struct afs_callback newcb;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_fid newfid;
	struct key *key;
	u64 data_version = dvnode->status.data_version;
	int ret;

	mode |= S_IFDIR;

	_enter("{%x:%u},{%pd},%ho",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_create(&fc, dentry->d_name.name, mode, data_version,
				      &newfid, &newstatus, &newcb);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		afs_vnode_new_inode(&fc, dentry, &newfid, &newstatus, &newcb);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (ret == 0 &&
	    test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &newfid,
				 afs_edit_dir_for_create);

	key_put(key);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Remove a subdir from a directory.
 */
static void afs_dir_remove_subdir(struct dentry *dentry)
{
	if (d_really_is_positive(dentry)) {
		struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));

		clear_nlink(&vnode->vfs_inode);
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
		clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	}
}

/*
 * remove a directory from an AFS filesystem
 */
static int afs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct key *key;
	u64 data_version = dvnode->status.data_version;
	int ret;

	_enter("{%x:%u},{%pd}",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry);

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_remove(&fc, dentry->d_name.name, true,
				      data_version);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0) {
			afs_dir_remove_subdir(dentry);
			if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
				afs_edit_dir_remove(dvnode, &dentry->d_name,
						    afs_edit_dir_for_rmdir);
		}
	}

	key_put(key);
error:
	return ret;
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
static int afs_dir_remove_link(struct dentry *dentry, struct key *key,
			       unsigned long d_version_before,
			       unsigned long d_version_after)
{
	bool dir_valid;
	int ret = 0;

	/* There were no intervening changes on the server if the version
	 * number we got back was incremented by exactly 1.
	 */
	dir_valid = (d_version_after == d_version_before + 1);

	if (d_really_is_positive(dentry)) {
		struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));

		if (dir_valid) {
			drop_nlink(&vnode->vfs_inode);
			if (vnode->vfs_inode.i_nlink == 0) {
				set_bit(AFS_VNODE_DELETED, &vnode->flags);
				clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
			}
			ret = 0;
		} else {
			clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);

			if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
				kdebug("AFS_VNODE_DELETED");

			ret = afs_validate(vnode, key);
			if (ret == -ESTALE)
				ret = 0;
		}
		_debug("nlink %d [val %d]", vnode->vfs_inode.i_nlink, ret);
	}

	return ret;
}

/*
 * Remove a file or symlink from an AFS filesystem.
 */
static int afs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *dvnode = AFS_FS_I(dir), *vnode;
	struct key *key;
	unsigned long d_version = (unsigned long)dentry->d_fsdata;
	u64 data_version = dvnode->status.data_version;
	int ret;

	_enter("{%x:%u},{%pd}",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry);

	if (dentry->d_name.len >= AFSNAMEMAX)
		return -ENAMETOOLONG;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	/* Try to make sure we have a callback promise on the victim. */
	if (d_really_is_positive(dentry)) {
		vnode = AFS_FS_I(d_inode(dentry));
		ret = afs_validate(vnode, key);
		if (ret < 0)
			goto error_key;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_remove(&fc, dentry->d_name.name, false,
				      data_version);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0)
			ret = afs_dir_remove_link(
				dentry, key, d_version,
				(unsigned long)dvnode->status.data_version);
		if (ret == 0 &&
		    test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
			afs_edit_dir_remove(dvnode, &dentry->d_name,
					    afs_edit_dir_for_unlink);
	}

error_key:
	key_put(key);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * create a regular file on an AFS filesystem
 */
static int afs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		      bool excl)
{
	struct afs_fs_cursor fc;
	struct afs_file_status newstatus;
	struct afs_callback newcb;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_fid newfid;
	struct key *key;
	u64 data_version = dvnode->status.data_version;
	int ret;

	mode |= S_IFREG;

	_enter("{%x:%u},{%pd},%ho,",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	ret = -ENAMETOOLONG;
	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_create(&fc, dentry->d_name.name, mode, data_version,
				      &newfid, &newstatus, &newcb);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		afs_vnode_new_inode(&fc, dentry, &newfid, &newstatus, &newcb);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &newfid,
				 afs_edit_dir_for_create);

	key_put(key);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

/*
 * create a hard link between files in an AFS filesystem
 */
static int afs_link(struct dentry *from, struct inode *dir,
		    struct dentry *dentry)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *dvnode, *vnode;
	struct key *key;
	u64 data_version;
	int ret;

	vnode = AFS_FS_I(d_inode(from));
	dvnode = AFS_FS_I(dir);
	data_version = dvnode->status.data_version;

	_enter("{%x:%u},{%x:%u},{%pd}",
	       vnode->fid.vid, vnode->fid.vnode,
	       dvnode->fid.vid, dvnode->fid.vnode,
	       dentry);

	ret = -ENAMETOOLONG;
	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		if (mutex_lock_interruptible_nested(&vnode->io_lock, 1) < 0) {
			afs_end_vnode_operation(&fc);
			goto error_key;
		}

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			fc.cb_break_2 = afs_calc_vnode_cb_break(vnode);
			afs_fs_link(&fc, vnode, dentry->d_name.name, data_version);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break_2);
		ihold(&vnode->vfs_inode);
		d_instantiate(dentry, &vnode->vfs_inode);

		mutex_unlock(&vnode->io_lock);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &vnode->fid,
				 afs_edit_dir_for_link);

	key_put(key);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

/*
 * create a symlink in an AFS filesystem
 */
static int afs_symlink(struct inode *dir, struct dentry *dentry,
		       const char *content)
{
	struct afs_fs_cursor fc;
	struct afs_file_status newstatus;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_fid newfid;
	struct key *key;
	u64 data_version = dvnode->status.data_version;
	int ret;

	_enter("{%x:%u},{%pd},%s",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry,
	       content);

	ret = -ENAMETOOLONG;
	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	ret = -EINVAL;
	if (strlen(content) >= AFSPATHMAX)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_symlink(&fc, dentry->d_name.name,
				       content, data_version,
				       &newfid, &newstatus);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		afs_vnode_new_inode(&fc, dentry, &newfid, &newstatus, NULL);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &newfid,
				 afs_edit_dir_for_symlink);

	key_put(key);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error:
	d_drop(dentry);
	_leave(" = %d", ret);
	return ret;
}

/*
 * rename a file in an AFS filesystem and/or move it between directories
 */
static int afs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry,
		      unsigned int flags)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *orig_dvnode, *new_dvnode, *vnode;
	struct key *key;
	u64 orig_data_version, new_data_version;
	bool new_negative = d_is_negative(new_dentry);
	int ret;

	if (flags)
		return -EINVAL;

	vnode = AFS_FS_I(d_inode(old_dentry));
	orig_dvnode = AFS_FS_I(old_dir);
	new_dvnode = AFS_FS_I(new_dir);
	orig_data_version = orig_dvnode->status.data_version;
	new_data_version = new_dvnode->status.data_version;

	_enter("{%x:%u},{%x:%u},{%x:%u},{%pd}",
	       orig_dvnode->fid.vid, orig_dvnode->fid.vnode,
	       vnode->fid.vid, vnode->fid.vnode,
	       new_dvnode->fid.vid, new_dvnode->fid.vnode,
	       new_dentry);

	key = afs_request_key(orig_dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, orig_dvnode, key)) {
		if (orig_dvnode != new_dvnode) {
			if (mutex_lock_interruptible_nested(&new_dvnode->io_lock, 1) < 0) {
				afs_end_vnode_operation(&fc);
				goto error_key;
			}
		}
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(orig_dvnode);
			fc.cb_break_2 = afs_calc_vnode_cb_break(new_dvnode);
			afs_fs_rename(&fc, old_dentry->d_name.name,
				      new_dvnode, new_dentry->d_name.name,
				      orig_data_version, new_data_version);
		}

		afs_vnode_commit_status(&fc, orig_dvnode, fc.cb_break);
		afs_vnode_commit_status(&fc, new_dvnode, fc.cb_break_2);
		if (orig_dvnode != new_dvnode)
			mutex_unlock(&new_dvnode->io_lock);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	}

	if (ret == 0) {
		if (test_bit(AFS_VNODE_DIR_VALID, &orig_dvnode->flags))
		    afs_edit_dir_remove(orig_dvnode, &old_dentry->d_name,
					afs_edit_dir_for_rename);

		if (!new_negative &&
		    test_bit(AFS_VNODE_DIR_VALID, &new_dvnode->flags))
			afs_edit_dir_remove(new_dvnode, &new_dentry->d_name,
					    afs_edit_dir_for_rename);

		if (test_bit(AFS_VNODE_DIR_VALID, &new_dvnode->flags))
			afs_edit_dir_add(new_dvnode, &new_dentry->d_name,
					 &vnode->fid,  afs_edit_dir_for_rename);
	}

error_key:
	key_put(key);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Release a directory page and clean up its private state if it's not busy
 * - return true if the page can now be released, false if not
 */
static int afs_dir_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct afs_vnode *dvnode = AFS_FS_I(page->mapping->host);

	_enter("{{%x:%u}[%lu]}", dvnode->fid.vid, dvnode->fid.vnode, page->index);

	set_page_private(page, 0);
	ClearPagePrivate(page);

	/* The directory will need reloading. */
	if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_stat_v(dvnode, n_relpg);
	return 1;
}

/*
 * invalidate part or all of a page
 * - release a page and clean up its private data if offset is 0 (indicating
 *   the entire page)
 */
static void afs_dir_invalidatepage(struct page *page, unsigned int offset,
				   unsigned int length)
{
	struct afs_vnode *dvnode = AFS_FS_I(page->mapping->host);

	_enter("{%lu},%u,%u", page->index, offset, length);

	BUG_ON(!PageLocked(page));

	/* The directory will need reloading. */
	if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_stat_v(dvnode, n_inval);

	/* we clean up only if the entire page is being invalidated */
	if (offset == 0 && length == PAGE_SIZE) {
		set_page_private(page, 0);
		ClearPagePrivate(page);
	}
}
