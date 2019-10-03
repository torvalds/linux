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
	struct inode		**inodes;
	struct afs_status_cb	*statuses;
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
			trace_afs_file_error(dvnode, -EIO, afs_file_error_dir_bad_magic);
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
 * Check the contents of a directory that we've just read.
 */
static bool afs_dir_check_pages(struct afs_vnode *dvnode, struct afs_read *req)
{
	struct afs_xdr_dir_page *dbuf;
	unsigned int i, j, qty = PAGE_SIZE / sizeof(union afs_xdr_dir_block);

	for (i = 0; i < req->nr_pages; i++)
		if (!afs_dir_check_page(dvnode, req->pages[i], req->actual_len))
			goto bad;
	return true;

bad:
	pr_warn("DIR %llx:%llx f=%llx l=%llx al=%llx r=%llx\n",
		dvnode->fid.vid, dvnode->fid.vnode,
		req->file_size, req->len, req->actual_len, req->remain);
	pr_warn("DIR %llx %x %x %x\n",
		req->pos, req->index, req->nr_pages, req->offset);

	for (i = 0; i < req->nr_pages; i++) {
		dbuf = kmap(req->pages[i]);
		for (j = 0; j < qty; j++) {
			union afs_xdr_dir_block *block = &dbuf->blocks[j];

			pr_warn("[%02x] %32phN\n", i * qty + j, block);
		}
		kunmap(req->pages[i]);
	}
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
		return ERR_PTR(afs_bad(dvnode, afs_file_error_dir_small));
	if (i_size > 2048 * 1024) {
		trace_afs_file_error(dvnode, -EFBIG, afs_file_error_dir_big);
		return ERR_PTR(-EFBIG);
	}

	_enter("%llu", i_size);

	/* Get a request record to hold the page list.  We want to hold it
	 * inline if we can, but we don't want to make an order 1 allocation.
	 */
	nr_pages = (i_size + PAGE_SIZE - 1) / PAGE_SIZE;
	nr_inline = nr_pages;
	if (nr_inline > (PAGE_SIZE - sizeof(*req)) / sizeof(struct page *))
		nr_inline = 0;

	req = kzalloc(struct_size(req, array, nr_inline), GFP_KERNEL);
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
		trace_afs_reload_dir(dvnode);
		ret = afs_fetch_data(dvnode, key, req);
		if (ret < 0)
			goto error_unlock;

		task_io_account_read(PAGE_SIZE * req->nr_pages);

		if (req->len < req->file_size)
			goto content_has_grown;

		/* Validate the data we just read. */
		ret = -EIO;
		if (!afs_dir_check_pages(dvnode, req))
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
static int afs_dir_iterate_block(struct afs_vnode *dvnode,
				 struct dir_context *ctx,
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
				return afs_bad(dvnode, afs_file_error_dir_over_end);
			}
			if (!(block->hdr.bitmap[next / 8] &
			      (1 << (next % 8)))) {
				_debug("ENT[%zu.%u]:"
				       " %u unmarked extension (len %u/%zu)",
				       blkoff / sizeof(union afs_xdr_dir_block),
				       offset, next, tmp, nlen);
				return afs_bad(dvnode, afs_file_error_dir_unmarked_ext);
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
			   struct key *key, afs_dataversion_t *_dir_version)
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
	*_dir_version = req->data_version;

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
			ret = afs_bad(dvnode, afs_file_error_dir_missing_page);
			break;
		}
		mark_page_accessed(page);

		limit = blkoff & ~(PAGE_SIZE - 1);

		dbuf = kmap(page);

		/* deal with the individual blocks stashed on this page */
		do {
			dblock = &dbuf->blocks[(blkoff % PAGE_SIZE) /
					       sizeof(union afs_xdr_dir_block)];
			ret = afs_dir_iterate_block(dvnode, ctx, dblock, blkoff);
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
	afs_dataversion_t dir_version;

	return afs_dir_iterate(file_inode(file), ctx, afs_file_key(file),
			       &dir_version);
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

	ret = -ENOENT;
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
	struct afs_cb_interest *dcbi, *cbi = NULL;
	struct afs_super_info *as = dir->i_sb->s_fs_info;
	struct afs_status_cb *scb;
	struct afs_iget_data iget_data;
	struct afs_fs_cursor fc;
	struct afs_server *server;
	struct afs_vnode *dvnode = AFS_FS_I(dir), *vnode;
	struct inode *inode = NULL, *ti;
	afs_dataversion_t data_version = READ_ONCE(dvnode->status.data_version);
	int ret, i;

	_enter("{%lu},%p{%pd},", dir->i_ino, dentry, dentry);

	cookie = kzalloc(sizeof(struct afs_lookup_cookie), GFP_KERNEL);
	if (!cookie)
		return ERR_PTR(-ENOMEM);

	cookie->ctx.actor = afs_lookup_filldir;
	cookie->name = dentry->d_name;
	cookie->nr_fids = 1; /* slot 0 is saved for the fid we actually want */

	read_seqlock_excl(&dvnode->cb_lock);
	dcbi = rcu_dereference_protected(dvnode->cb_interest,
					 lockdep_is_held(&dvnode->cb_lock.lock));
	if (dcbi) {
		server = dcbi->server;
		if (server &&
		    test_bit(AFS_SERVER_FL_NO_IBULK, &server->flags))
			cookie->one_only = true;
	}
	read_sequnlock_excl(&dvnode->cb_lock);

	for (i = 0; i < 50; i++)
		cookie->fids[i].vid = as->volume->vid;

	/* search the directory */
	ret = afs_dir_iterate(dir, &cookie->ctx, key, &data_version);
	if (ret < 0) {
		inode = ERR_PTR(ret);
		goto out;
	}

	dentry->d_fsdata = (void *)(unsigned long)data_version;

	inode = ERR_PTR(-ENOENT);
	if (!cookie->found)
		goto out;

	/* Check to see if we already have an inode for the primary fid. */
	iget_data.fid = cookie->fids[0];
	iget_data.volume = dvnode->volume;
	iget_data.cb_v_break = dvnode->volume->cb_v_break;
	iget_data.cb_s_break = 0;
	inode = ilookup5(dir->i_sb, cookie->fids[0].vnode,
			 afs_iget5_test, &iget_data);
	if (inode)
		goto out;

	/* Need space for examining all the selected files */
	inode = ERR_PTR(-ENOMEM);
	cookie->statuses = kvcalloc(cookie->nr_fids, sizeof(struct afs_status_cb),
				    GFP_KERNEL);
	if (!cookie->statuses)
		goto out;

	cookie->inodes = kcalloc(cookie->nr_fids, sizeof(struct inode *),
				 GFP_KERNEL);
	if (!cookie->inodes)
		goto out_s;

	for (i = 1; i < cookie->nr_fids; i++) {
		scb = &cookie->statuses[i];

		/* Find any inodes that already exist and get their
		 * callback counters.
		 */
		iget_data.fid = cookie->fids[i];
		ti = ilookup5_nowait(dir->i_sb, iget_data.fid.vnode,
				     afs_iget5_test, &iget_data);
		if (!IS_ERR_OR_NULL(ti)) {
			vnode = AFS_FS_I(ti);
			scb->cb_break = afs_calc_vnode_cb_break(vnode);
			cookie->inodes[i] = ti;
		}
	}

	/* Try FS.InlineBulkStatus first.  Abort codes for the individual
	 * lookups contained therein are stored in the reply without aborting
	 * the whole operation.
	 */
	if (cookie->one_only)
		goto no_inline_bulk_status;

	inode = ERR_PTR(-ERESTARTSYS);
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		while (afs_select_fileserver(&fc)) {
			if (test_bit(AFS_SERVER_FL_NO_IBULK,
				      &fc.cbi->server->flags)) {
				fc.ac.abort_code = RX_INVALID_OPERATION;
				fc.ac.error = -ECONNABORTED;
				break;
			}
			iget_data.cb_v_break = dvnode->volume->cb_v_break;
			iget_data.cb_s_break = fc.cbi->server->cb_s_break;
			afs_fs_inline_bulk_status(&fc,
						  afs_v2net(dvnode),
						  cookie->fids,
						  cookie->statuses,
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
	inode = ERR_PTR(-ERESTARTSYS);
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		while (afs_select_fileserver(&fc)) {
			iget_data.cb_v_break = dvnode->volume->cb_v_break;
			iget_data.cb_s_break = fc.cbi->server->cb_s_break;
			scb = &cookie->statuses[0];
			afs_fs_fetch_status(&fc,
					    afs_v2net(dvnode),
					    cookie->fids,
					    scb,
					    NULL);
		}

		if (fc.ac.error == 0)
			cbi = afs_get_cb_interest(fc.cbi);
		inode = ERR_PTR(afs_end_vnode_operation(&fc));
	}

	if (IS_ERR(inode))
		goto out_c;

success:
	/* Turn all the files into inodes and save the first one - which is the
	 * one we actually want.
	 */
	scb = &cookie->statuses[0];
	if (scb->status.abort_code != 0)
		inode = ERR_PTR(afs_abort_to_error(scb->status.abort_code));

	for (i = 0; i < cookie->nr_fids; i++) {
		struct afs_status_cb *scb = &cookie->statuses[i];

		if (!scb->have_status && !scb->have_error)
			continue;

		if (cookie->inodes[i]) {
			afs_vnode_commit_status(&fc, AFS_FS_I(cookie->inodes[i]),
						scb->cb_break, NULL, scb);
			continue;
		}

		if (scb->status.abort_code != 0)
			continue;

		iget_data.fid = cookie->fids[i];
		ti = afs_iget(dir->i_sb, key, &iget_data, scb, cbi, dvnode);
		if (!IS_ERR(ti))
			afs_cache_permit(AFS_FS_I(ti), key,
					 0 /* Assume vnode->cb_break is 0 */ +
					 iget_data.cb_v_break,
					 scb);
		if (i == 0) {
			inode = ti;
		} else {
			if (!IS_ERR(ti))
				iput(ti);
		}
	}

out_c:
	afs_put_cb_interest(afs_v2net(dvnode), cbi);
	if (cookie->inodes) {
		for (i = 0; i < cookie->nr_fids; i++)
			iput(cookie->inodes[i]);
		kfree(cookie->inodes);
	}
out_s:
	kvfree(cookie->statuses);
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
	if (inode == ERR_PTR(-ENOENT)) {
		inode = afs_try_auto_mntpt(dentry, dir);
	} else {
		dentry->d_fsdata =
			(void *)(unsigned long)dvnode->status.data_version;
	}
	d = d_splice_alias(inode, dentry);
	if (!IS_ERR_OR_NULL(d)) {
		d->d_fsdata = dentry->d_fsdata;
		trace_afs_lookup(dvnode, &d->d_name,
				 inode ? AFS_FS_I(inode) : NULL);
	} else {
		trace_afs_lookup(dvnode, &dentry->d_name,
				 IS_ERR_OR_NULL(inode) ? NULL
				 : AFS_FS_I(inode));
	}
	return d;
}

/*
 * Check the validity of a dentry under RCU conditions.
 */
static int afs_d_revalidate_rcu(struct dentry *dentry)
{
	struct afs_vnode *dvnode, *vnode;
	struct dentry *parent;
	struct inode *dir, *inode;
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

	/* Check to see if the vnode referred to by the dentry still
	 * has a callback.
	 */
	if (d_really_is_positive(dentry)) {
		inode = d_inode_rcu(dentry);
		if (inode) {
			vnode = AFS_FS_I(inode);
			if (!afs_check_validity(vnode))
				return -ECHILD;
		}
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
	struct afs_fid uninitialized_var(fid);
	struct dentry *parent;
	struct inode *inode;
	struct key *key;
	afs_dataversion_t dir_version;
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
	dir_version = dir->status.data_version;
	de_version = (long)dentry->d_fsdata;
	if (de_version == (long)dir_version)
		goto out_valid_noupdate;

	dir_version = dir->invalid_before;
	if (de_version - (long)dir_version >= 0)
		goto out_valid;

	_debug("dir modified");
	afs_stat_v(dir, n_reval);

	/* search the directory for this vnode */
	ret = afs_do_lookup_one(&dir->vfs_inode, dentry, &fid, key, &dir_version);
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
	dentry->d_fsdata = (void *)(unsigned long)dir_version;
out_valid_noupdate:
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

/*
 * Create a new inode for create/mkdir/symlink
 */
static void afs_vnode_new_inode(struct afs_fs_cursor *fc,
				struct dentry *new_dentry,
				struct afs_iget_data *new_data,
				struct afs_status_cb *new_scb)
{
	struct afs_vnode *vnode;
	struct inode *inode;

	if (fc->ac.error < 0)
		return;

	inode = afs_iget(fc->vnode->vfs_inode.i_sb, fc->key,
			 new_data, new_scb, fc->cbi, fc->vnode);
	if (IS_ERR(inode)) {
		/* ENOMEM or EINTR at a really inconvenient time - just abandon
		 * the new directory on the server.
		 */
		fc->ac.error = PTR_ERR(inode);
		return;
	}

	vnode = AFS_FS_I(inode);
	set_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	if (fc->ac.error == 0)
		afs_cache_permit(vnode, fc->key, vnode->cb_break, new_scb);
	d_instantiate(new_dentry, inode);
}

static void afs_prep_for_new_inode(struct afs_fs_cursor *fc,
				   struct afs_iget_data *iget_data)
{
	iget_data->volume = fc->vnode->volume;
	iget_data->cb_v_break = fc->vnode->volume->cb_v_break;
	iget_data->cb_s_break = fc->cbi->server->cb_s_break;
}

/*
 * Note that a dentry got changed.  We need to set d_fsdata to the data version
 * number derived from the result of the operation.  It doesn't matter if
 * d_fsdata goes backwards as we'll just revalidate.
 */
static void afs_update_dentry_version(struct afs_fs_cursor *fc,
				      struct dentry *dentry,
				      struct afs_status_cb *scb)
{
	if (fc->ac.error == 0)
		dentry->d_fsdata =
			(void *)(unsigned long)scb->status.data_version;
}

/*
 * create a directory on an AFS filesystem
 */
static int afs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct afs_iget_data iget_data;
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct key *key;
	int ret;

	mode |= S_IFDIR;

	_enter("{%llx:%llu},{%pd},%ho",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	ret = -ENOMEM;
	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		afs_dataversion_t data_version = dvnode->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_prep_for_new_inode(&fc, &iget_data);
			afs_fs_create(&fc, dentry->d_name.name, mode,
				      &scb[0], &iget_data.fid, &scb[1]);
		}

		afs_check_for_remote_deletion(&fc, dvnode);
		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&data_version, &scb[0]);
		afs_update_dentry_version(&fc, dentry, &scb[0]);
		afs_vnode_new_inode(&fc, dentry, &iget_data, &scb[1]);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (ret == 0 &&
	    test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &iget_data.fid,
				 afs_edit_dir_for_create);

	key_put(key);
	kfree(scb);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error_scb:
	kfree(scb);
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
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	struct afs_vnode *dvnode = AFS_FS_I(dir), *vnode = NULL;
	struct key *key;
	int ret;

	_enter("{%llx:%llu},{%pd}",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

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

	if (vnode) {
		ret = down_write_killable(&vnode->rmdir_lock);
		if (ret < 0)
			goto error_key;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		afs_dataversion_t data_version = dvnode->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_fs_remove(&fc, vnode, dentry->d_name.name, true, scb);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&data_version, scb);
		afs_update_dentry_version(&fc, dentry, scb);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0) {
			afs_dir_remove_subdir(dentry);
			if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
				afs_edit_dir_remove(dvnode, &dentry->d_name,
						    afs_edit_dir_for_rmdir);
		}
	}

	if (vnode)
		up_write(&vnode->rmdir_lock);
error_key:
	key_put(key);
error:
	kfree(scb);
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
static int afs_dir_remove_link(struct afs_vnode *dvnode, struct dentry *dentry,
			       struct key *key)
{
	int ret = 0;

	if (d_really_is_positive(dentry)) {
		struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));

		if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
			/* Already done */
		} else if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags)) {
			write_seqlock(&vnode->cb_lock);
			drop_nlink(&vnode->vfs_inode);
			if (vnode->vfs_inode.i_nlink == 0) {
				set_bit(AFS_VNODE_DELETED, &vnode->flags);
				__afs_break_callback(vnode, afs_cb_break_for_unlink);
			}
			write_sequnlock(&vnode->cb_lock);
			ret = 0;
		} else {
			afs_break_callback(vnode, afs_cb_break_for_unlink);

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
	struct afs_status_cb *scb;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));
	struct key *key;
	bool need_rehash = false;
	int ret;

	_enter("{%llx:%llu},{%pd}",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry);

	if (dentry->d_name.len >= AFSNAMEMAX)
		return -ENAMETOOLONG;

	ret = -ENOMEM;
	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

	/* Try to make sure we have a callback promise on the victim. */
	ret = afs_validate(vnode, key);
	if (ret < 0)
		goto error_key;

	spin_lock(&dentry->d_lock);
	if (d_count(dentry) > 1) {
		spin_unlock(&dentry->d_lock);
		/* Start asynchronous writeout of the inode */
		write_inode_now(d_inode(dentry), 0);
		ret = afs_sillyrename(dvnode, vnode, dentry, key);
		goto error_key;
	}
	if (!d_unhashed(dentry)) {
		/* Prevent a race with RCU lookup. */
		__d_drop(dentry);
		need_rehash = true;
	}
	spin_unlock(&dentry->d_lock);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		afs_dataversion_t data_version = dvnode->status.data_version + 1;
		afs_dataversion_t data_version_2 = vnode->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			fc.cb_break_2 = afs_calc_vnode_cb_break(vnode);

			if (test_bit(AFS_SERVER_FL_IS_YFS, &fc.cbi->server->flags) &&
			    !test_bit(AFS_SERVER_FL_NO_RM2, &fc.cbi->server->flags)) {
				yfs_fs_remove_file2(&fc, vnode, dentry->d_name.name,
						    &scb[0], &scb[1]);
				if (fc.ac.error != -ECONNABORTED ||
				    fc.ac.abort_code != RXGEN_OPCODE)
					continue;
				set_bit(AFS_SERVER_FL_NO_RM2, &fc.cbi->server->flags);
			}

			afs_fs_remove(&fc, vnode, dentry->d_name.name, false, &scb[0]);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&data_version, &scb[0]);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break_2,
					&data_version_2, &scb[1]);
		afs_update_dentry_version(&fc, dentry, &scb[0]);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0 && !(scb[1].have_status || scb[1].have_error))
			ret = afs_dir_remove_link(dvnode, dentry, key);
		if (ret == 0 &&
		    test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
			afs_edit_dir_remove(dvnode, &dentry->d_name,
					    afs_edit_dir_for_unlink);
	}

	if (need_rehash && ret < 0 && ret != -ENOENT)
		d_rehash(dentry);

error_key:
	key_put(key);
error_scb:
	kfree(scb);
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
	struct afs_iget_data iget_data;
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct key *key;
	int ret;

	mode |= S_IFREG;

	_enter("{%llx:%llu},{%pd},%ho,",
	       dvnode->fid.vid, dvnode->fid.vnode, dentry, mode);

	ret = -ENAMETOOLONG;
	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error;
	}

	ret = -ENOMEM;
	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error_scb;

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		afs_dataversion_t data_version = dvnode->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_prep_for_new_inode(&fc, &iget_data);
			afs_fs_create(&fc, dentry->d_name.name, mode,
				      &scb[0], &iget_data.fid, &scb[1]);
		}

		afs_check_for_remote_deletion(&fc, dvnode);
		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&data_version, &scb[0]);
		afs_update_dentry_version(&fc, dentry, &scb[0]);
		afs_vnode_new_inode(&fc, dentry, &iget_data, &scb[1]);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &iget_data.fid,
				 afs_edit_dir_for_create);

	kfree(scb);
	key_put(key);
	_leave(" = 0");
	return 0;

error_scb:
	kfree(scb);
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
	struct afs_status_cb *scb;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct afs_vnode *vnode = AFS_FS_I(d_inode(from));
	struct key *key;
	int ret;

	_enter("{%llx:%llu},{%llx:%llu},{%pd}",
	       vnode->fid.vid, vnode->fid.vnode,
	       dvnode->fid.vid, dvnode->fid.vnode,
	       dentry);

	ret = -ENAMETOOLONG;
	if (dentry->d_name.len >= AFSNAMEMAX)
		goto error;

	ret = -ENOMEM;
	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		afs_dataversion_t data_version = dvnode->status.data_version + 1;

		if (mutex_lock_interruptible_nested(&vnode->io_lock, 1) < 0) {
			afs_end_vnode_operation(&fc);
			goto error_key;
		}

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			fc.cb_break_2 = afs_calc_vnode_cb_break(vnode);
			afs_fs_link(&fc, vnode, dentry->d_name.name,
				    &scb[0], &scb[1]);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&data_version, &scb[0]);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break_2,
					NULL, &scb[1]);
		ihold(&vnode->vfs_inode);
		afs_update_dentry_version(&fc, dentry, &scb[0]);
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
	kfree(scb);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error_scb:
	kfree(scb);
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
	struct afs_iget_data iget_data;
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	struct afs_vnode *dvnode = AFS_FS_I(dir);
	struct key *key;
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

	ret = -ENOMEM;
	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	key = afs_request_key(dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, dvnode, key, true)) {
		afs_dataversion_t data_version = dvnode->status.data_version + 1;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(dvnode);
			afs_prep_for_new_inode(&fc, &iget_data);
			afs_fs_symlink(&fc, dentry->d_name.name, content,
				       &scb[0], &iget_data.fid, &scb[1]);
		}

		afs_check_for_remote_deletion(&fc, dvnode);
		afs_vnode_commit_status(&fc, dvnode, fc.cb_break,
					&data_version, &scb[0]);
		afs_update_dentry_version(&fc, dentry, &scb[0]);
		afs_vnode_new_inode(&fc, dentry, &iget_data, &scb[1]);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	} else {
		goto error_key;
	}

	if (test_bit(AFS_VNODE_DIR_VALID, &dvnode->flags))
		afs_edit_dir_add(dvnode, &dentry->d_name, &iget_data.fid,
				 afs_edit_dir_for_symlink);

	key_put(key);
	kfree(scb);
	_leave(" = 0");
	return 0;

error_key:
	key_put(key);
error_scb:
	kfree(scb);
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
	struct afs_status_cb *scb;
	struct afs_vnode *orig_dvnode, *new_dvnode, *vnode;
	struct dentry *tmp = NULL, *rehash = NULL;
	struct inode *new_inode;
	struct key *key;
	bool new_negative = d_is_negative(new_dentry);
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

	ret = -ENOMEM;
	scb = kcalloc(2, sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	key = afs_request_key(orig_dvnode->volume->cell);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error_scb;
	}

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
			rehash = new_dentry;
		}

		if (d_count(new_dentry) > 2) {
			/* copy the target dentry's name */
			ret = -ENOMEM;
			tmp = d_alloc(new_dentry->d_parent,
				      &new_dentry->d_name);
			if (!tmp)
				goto error_rehash;

			ret = afs_sillyrename(new_dvnode,
					      AFS_FS_I(d_inode(new_dentry)),
					      new_dentry, key);
			if (ret)
				goto error_rehash;

			new_dentry = tmp;
			rehash = NULL;
			new_negative = true;
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

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, orig_dvnode, key, true)) {
		afs_dataversion_t orig_data_version;
		afs_dataversion_t new_data_version;
		struct afs_status_cb *new_scb = &scb[1];

		orig_data_version = orig_dvnode->status.data_version + 1;

		if (orig_dvnode != new_dvnode) {
			if (mutex_lock_interruptible_nested(&new_dvnode->io_lock, 1) < 0) {
				afs_end_vnode_operation(&fc);
				goto error_rehash_old;
			}
			new_data_version = new_dvnode->status.data_version + 1;
		} else {
			new_data_version = orig_data_version;
			new_scb = &scb[0];
		}

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(orig_dvnode);
			fc.cb_break_2 = afs_calc_vnode_cb_break(new_dvnode);
			afs_fs_rename(&fc, old_dentry->d_name.name,
				      new_dvnode, new_dentry->d_name.name,
				      &scb[0], new_scb);
		}

		afs_vnode_commit_status(&fc, orig_dvnode, fc.cb_break,
					&orig_data_version, &scb[0]);
		if (new_dvnode != orig_dvnode) {
			afs_vnode_commit_status(&fc, new_dvnode, fc.cb_break_2,
						&new_data_version, &scb[1]);
			mutex_unlock(&new_dvnode->io_lock);
		}
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_rehash_old;
	}

	if (ret == 0) {
		if (rehash)
			d_rehash(rehash);
		if (test_bit(AFS_VNODE_DIR_VALID, &orig_dvnode->flags))
		    afs_edit_dir_remove(orig_dvnode, &old_dentry->d_name,
					afs_edit_dir_for_rename_0);

		if (!new_negative &&
		    test_bit(AFS_VNODE_DIR_VALID, &new_dvnode->flags))
			afs_edit_dir_remove(new_dvnode, &new_dentry->d_name,
					    afs_edit_dir_for_rename_1);

		if (test_bit(AFS_VNODE_DIR_VALID, &new_dvnode->flags))
			afs_edit_dir_add(new_dvnode, &new_dentry->d_name,
					 &vnode->fid, afs_edit_dir_for_rename_2);

		new_inode = d_inode(new_dentry);
		if (new_inode) {
			spin_lock(&new_inode->i_lock);
			if (new_inode->i_nlink > 0)
				drop_nlink(new_inode);
			spin_unlock(&new_inode->i_lock);
		}

		/* Now we can update d_fsdata on the dentries to reflect their
		 * new parent's data_version.
		 *
		 * Note that if we ever implement RENAME_EXCHANGE, we'll have
		 * to update both dentries with opposing dir versions.
		 */
		if (new_dvnode != orig_dvnode) {
			afs_update_dentry_version(&fc, old_dentry, &scb[1]);
			afs_update_dentry_version(&fc, new_dentry, &scb[1]);
		} else {
			afs_update_dentry_version(&fc, old_dentry, &scb[0]);
			afs_update_dentry_version(&fc, new_dentry, &scb[0]);
		}
		d_move(old_dentry, new_dentry);
		goto error_tmp;
	}

error_rehash_old:
	d_rehash(new_dentry);
error_rehash:
	if (rehash)
		d_rehash(rehash);
error_tmp:
	if (tmp)
		dput(tmp);
	key_put(key);
error_scb:
	kfree(scb);
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

	_enter("{{%llx:%llu}[%lu]}", dvnode->fid.vid, dvnode->fid.vnode, page->index);

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
