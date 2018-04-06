/* dir.c: AFS filesystem directory handling
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/dns_resolver.h>
#include "internal.h"

static struct dentry *afs_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags);
static struct dentry *afs_dynroot_lookup(struct inode *dir, struct dentry *dentry,
					 unsigned int flags);
static int afs_dir_open(struct inode *inode, struct file *file);
static int afs_readdir(struct file *file, struct dir_context *ctx);
static int afs_d_revalidate(struct dentry *dentry, unsigned int flags);
static int afs_d_delete(const struct dentry *dentry);
static void afs_d_release(struct dentry *dentry);
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

const struct file_operations afs_dynroot_file_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.iterate_shared	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
};

const struct inode_operations afs_dynroot_inode_operations = {
	.lookup		= afs_dynroot_lookup,
};

const struct dentry_operations afs_fs_dentry_operations = {
	.d_revalidate	= afs_d_revalidate,
	.d_delete	= afs_d_delete,
	.d_release	= afs_d_release,
	.d_automount	= afs_d_automount,
};

#define AFS_DIR_HASHTBL_SIZE	128
#define AFS_DIR_DIRENT_SIZE	32
#define AFS_DIRENT_PER_BLOCK	64

union afs_dirent {
	struct {
		uint8_t		valid;
		uint8_t		unused[1];
		__be16		hash_next;
		__be32		vnode;
		__be32		unique;
		uint8_t		name[16];
		uint8_t		overflow[4];	/* if any char of the name (inc
						 * NUL) reaches here, consume
						 * the next dirent too */
	} u;
	uint8_t	extended_name[32];
};

/* AFS directory page header (one at the beginning of every 2048-byte chunk) */
struct afs_dir_pagehdr {
	__be16		npages;
	__be16		magic;
#define AFS_DIR_MAGIC htons(1234)
	uint8_t		nentries;
	uint8_t		bitmap[8];
	uint8_t		pad[19];
};

/* directory block layout */
union afs_dir_block {

	struct afs_dir_pagehdr pagehdr;

	struct {
		struct afs_dir_pagehdr	pagehdr;
		uint8_t			alloc_ctrs[128];
		/* dir hash table */
		uint16_t		hashtable[AFS_DIR_HASHTBL_SIZE];
	} hdr;

	union afs_dirent dirents[AFS_DIRENT_PER_BLOCK];
};

/* layout on a linux VM page */
struct afs_dir_page {
	union afs_dir_block blocks[PAGE_SIZE / sizeof(union afs_dir_block)];
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
bool afs_dir_check_page(struct inode *dir, struct page *page)
{
	struct afs_dir_page *dbuf;
	struct afs_vnode *vnode = AFS_FS_I(dir);
	loff_t latter, i_size, off;
	int tmp, qty;

#if 0
	/* check the page count */
	qty = desc.size / sizeof(dbuf->blocks[0]);
	if (qty == 0)
		goto error;

	if (page->index == 0 && qty != ntohs(dbuf->blocks[0].pagehdr.npages)) {
		printk("kAFS: %s(%lu): wrong number of dir blocks %d!=%hu\n",
		       __func__, dir->i_ino, qty,
		       ntohs(dbuf->blocks[0].pagehdr.npages));
		goto error;
	}
#endif

	/* Determine how many magic numbers there should be in this page, but
	 * we must take care because the directory may change size under us.
	 */
	off = page_offset(page);
	i_size = i_size_read(dir);
	if (i_size <= off)
		goto checked;

	latter = i_size - off;
	if (latter >= PAGE_SIZE)
		qty = PAGE_SIZE;
	else
		qty = latter;
	qty /= sizeof(union afs_dir_block);

	/* check them */
	dbuf = page_address(page);
	for (tmp = 0; tmp < qty; tmp++) {
		if (dbuf->blocks[tmp].pagehdr.magic != AFS_DIR_MAGIC) {
			printk("kAFS: %s(%lx): bad magic %d/%d is %04hx\n",
			       __func__, dir->i_ino, tmp, qty,
			       ntohs(dbuf->blocks[tmp].pagehdr.magic));
			trace_afs_dir_check_failed(vnode, off, i_size);
			goto error;
		}
	}

checked:
	SetPageChecked(page);
	return true;

error:
	SetPageError(page);
	return false;
}

/*
 * discard a page cached in the pagecache
 */
static inline void afs_dir_put_page(struct page *page)
{
	kunmap(page);
	unlock_page(page);
	put_page(page);
}

/*
 * get a page into the pagecache
 */
static struct page *afs_dir_get_page(struct inode *dir, unsigned long index,
				     struct key *key)
{
	struct page *page;
	_enter("{%lu},%lu", dir->i_ino, index);

	page = read_cache_page(dir->i_mapping, index, afs_page_filler, key);
	if (!IS_ERR(page)) {
		lock_page(page);
		kmap(page);
		if (unlikely(!PageChecked(page))) {
			if (PageError(page))
				goto fail;
		}
	}
	return page;

fail:
	afs_dir_put_page(page);
	_leave(" = -EIO");
	return ERR_PTR(-EIO);
}

/*
 * open an AFS directory file
 */
static int afs_dir_open(struct inode *inode, struct file *file)
{
	_enter("{%lu}", inode->i_ino);

	BUILD_BUG_ON(sizeof(union afs_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_dirent) != 32);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(inode)->flags))
		return -ENOENT;

	return afs_open(inode, file);
}

/*
 * deal with one block in an AFS directory
 */
static int afs_dir_iterate_block(struct dir_context *ctx,
				 union afs_dir_block *block,
				 unsigned blkoff)
{
	union afs_dirent *dire;
	unsigned offset, next, curr;
	size_t nlen;
	int tmp;

	_enter("%u,%x,%p,,",(unsigned)ctx->pos,blkoff,block);

	curr = (ctx->pos - blkoff) / sizeof(union afs_dirent);

	/* walk through the block, an entry at a time */
	for (offset = AFS_DIRENT_PER_BLOCK - block->pagehdr.nentries;
	     offset < AFS_DIRENT_PER_BLOCK;
	     offset = next
	     ) {
		next = offset + 1;

		/* skip entries marked unused in the bitmap */
		if (!(block->pagehdr.bitmap[offset / 8] &
		      (1 << (offset % 8)))) {
			_debug("ENT[%zu.%u]: unused",
			       blkoff / sizeof(union afs_dir_block), offset);
			if (offset >= curr)
				ctx->pos = blkoff +
					next * sizeof(union afs_dirent);
			continue;
		}

		/* got a valid entry */
		dire = &block->dirents[offset];
		nlen = strnlen(dire->u.name,
			       sizeof(*block) -
			       offset * sizeof(union afs_dirent));

		_debug("ENT[%zu.%u]: %s %zu \"%s\"",
		       blkoff / sizeof(union afs_dir_block), offset,
		       (offset < curr ? "skip" : "fill"),
		       nlen, dire->u.name);

		/* work out where the next possible entry is */
		for (tmp = nlen; tmp > 15; tmp -= sizeof(union afs_dirent)) {
			if (next >= AFS_DIRENT_PER_BLOCK) {
				_debug("ENT[%zu.%u]:"
				       " %u travelled beyond end dir block"
				       " (len %u/%zu)",
				       blkoff / sizeof(union afs_dir_block),
				       offset, next, tmp, nlen);
				return -EIO;
			}
			if (!(block->pagehdr.bitmap[next / 8] &
			      (1 << (next % 8)))) {
				_debug("ENT[%zu.%u]:"
				       " %u unmarked extension (len %u/%zu)",
				       blkoff / sizeof(union afs_dir_block),
				       offset, next, tmp, nlen);
				return -EIO;
			}

			_debug("ENT[%zu.%u]: ext %u/%zu",
			       blkoff / sizeof(union afs_dir_block),
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

		ctx->pos = blkoff + next * sizeof(union afs_dirent);
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
	union afs_dir_block *dblock;
	struct afs_dir_page *dbuf;
	struct page *page;
	unsigned blkoff, limit;
	int ret;

	_enter("{%lu},%u,,", dir->i_ino, (unsigned)ctx->pos);

	if (test_bit(AFS_VNODE_DELETED, &AFS_FS_I(dir)->flags)) {
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	/* round the file position up to the next entry boundary */
	ctx->pos += sizeof(union afs_dirent) - 1;
	ctx->pos &= ~(sizeof(union afs_dirent) - 1);

	/* walk through the blocks in sequence */
	ret = 0;
	while (ctx->pos < dir->i_size) {
		blkoff = ctx->pos & ~(sizeof(union afs_dir_block) - 1);

		/* fetch the appropriate page from the directory */
		page = afs_dir_get_page(dir, blkoff / PAGE_SIZE, key);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			break;
		}

		limit = blkoff & ~(PAGE_SIZE - 1);

		dbuf = page_address(page);

		/* deal with the individual blocks stashed on this page */
		do {
			dblock = &dbuf->blocks[(blkoff % PAGE_SIZE) /
					       sizeof(union afs_dir_block)];
			ret = afs_dir_iterate_block(ctx, dblock, blkoff);
			if (ret != 1) {
				afs_dir_put_page(page);
				goto out;
			}

			blkoff += sizeof(union afs_dir_block);

		} while (ctx->pos < dir->i_size && blkoff < limit);

		afs_dir_put_page(page);
		ret = 0;
	}

out:
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
	BUILD_BUG_ON(sizeof(union afs_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_dirent) != 32);

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
	BUILD_BUG_ON(sizeof(union afs_dir_block) != 2048);
	BUILD_BUG_ON(sizeof(union afs_dirent) != 32);

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
 * Probe to see if a cell may exist.  This prevents positive dentries from
 * being created unnecessarily.
 */
static int afs_probe_cell_name(struct dentry *dentry)
{
	struct afs_cell *cell;
	const char *name = dentry->d_name.name;
	size_t len = dentry->d_name.len;
	int ret;

	/* Names prefixed with a dot are R/W mounts. */
	if (name[0] == '.') {
		if (len == 1)
			return -EINVAL;
		name++;
		len--;
	}

	cell = afs_lookup_cell_rcu(afs_d2net(dentry), name, len);
	if (!IS_ERR(cell)) {
		afs_put_cell(afs_d2net(dentry), cell);
		return 0;
	}

	ret = dns_query("afsdb", name, len, "ipv4", NULL, NULL);
	if (ret == -ENODATA)
		ret = -EDESTADDRREQ;
	return ret;
}

/*
 * Try to auto mount the mountpoint with pseudo directory, if the autocell
 * operation is setted.
 */
static struct inode *afs_try_auto_mntpt(struct dentry *dentry, struct inode *dir)
{
	struct afs_vnode *vnode = AFS_FS_I(dir);
	struct inode *inode;
	int ret = -ENOENT;

	_enter("%p{%pd}, {%x:%u}",
	       dentry, dentry, vnode->fid.vid, vnode->fid.vnode);

	if (!test_bit(AFS_VNODE_AUTOCELL, &vnode->flags))
		goto out;

	ret = afs_probe_cell_name(dentry);
	if (ret < 0)
		goto out;

	inode = afs_iget_pseudo_dir(dir->i_sb, false);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto out;
	}

	_leave("= %p", inode);
	return inode;

out:
	_leave("= %d", ret);
	return ERR_PTR(ret);
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

	inode = afs_do_lookup(dir, dentry, key);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		if (ret == -ENOENT) {
			inode = afs_try_auto_mntpt(dentry, dir);
			if (!IS_ERR(inode)) {
				key_put(key);
				goto success;
			}

			ret = PTR_ERR(inode);
		}

		key_put(key);
		if (ret == -ENOENT) {
			d_add(dentry, NULL);
			_leave(" = NULL [negative]");
			return NULL;
		}
		_leave(" = %d [do]", ret);
		return ERR_PTR(ret);
	}
	dentry->d_fsdata = (void *)(unsigned long)dvnode->status.data_version;

	/* instantiate the dentry */
	key_put(key);
	if (IS_ERR(inode)) {
		_leave(" = %ld", PTR_ERR(inode));
		return ERR_CAST(inode);
	}

success:
	d_add(dentry, inode);
	_leave(" = 0 { ino=%lu v=%u }",
	       d_inode(dentry)->i_ino,
	       d_inode(dentry)->i_generation);

	return NULL;
}

/*
 * Look up @cell in a dynroot directory.  This is a substitution for the
 * local cell name for the net namespace.
 */
static struct dentry *afs_lookup_atcell(struct dentry *dentry)
{
	struct afs_cell *cell;
	struct afs_net *net = afs_d2net(dentry);
	struct dentry *ret;
	unsigned int seq = 0;
	char *name;
	int len;

	if (!net->ws_cell)
		return ERR_PTR(-ENOENT);

	ret = ERR_PTR(-ENOMEM);
	name = kmalloc(AFS_MAXCELLNAME + 1, GFP_KERNEL);
	if (!name)
		goto out_p;

	rcu_read_lock();
	do {
		read_seqbegin_or_lock(&net->cells_lock, &seq);
		cell = rcu_dereference_raw(net->ws_cell);
		if (cell) {
			len = cell->name_len;
			memcpy(name, cell->name, len + 1);
		}
	} while (need_seqretry(&net->cells_lock, seq));
	done_seqretry(&net->cells_lock, seq);
	rcu_read_unlock();

	ret = ERR_PTR(-ENOENT);
	if (!cell)
		goto out_n;

	ret = lookup_one_len(name, dentry->d_parent, len);

	/* We don't want to d_add() the @cell dentry here as we don't want to
	 * the cached dentry to hide changes to the local cell name.
	 */

out_n:
	kfree(name);
out_p:
	return ret;
}

/*
 * Look up an entry in a dynroot directory.
 */
static struct dentry *afs_dynroot_lookup(struct inode *dir, struct dentry *dentry,
					 unsigned int flags)
{
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	vnode = AFS_FS_I(dir);

	_enter("%pd", dentry);

	ASSERTCMP(d_inode(dentry), ==, NULL);

	if (dentry->d_name.len >= AFSNAMEMAX) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	if (dentry->d_name.len == 5 &&
	    memcmp(dentry->d_name.name, "@cell", 5) == 0)
		return afs_lookup_atcell(dentry);

	inode = afs_try_auto_mntpt(dentry, dir);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		if (ret == -ENOENT) {
			d_add(dentry, NULL);
			_leave(" = NULL [negative]");
			return NULL;
		}
		_leave(" = %d [do]", ret);
		return ERR_PTR(ret);
	}

	d_add(dentry, inode);
	_leave(" = 0 { ino=%lu v=%u }",
	       d_inode(dentry)->i_ino, d_inode(dentry)->i_generation);
	return NULL;
}

/*
 * check that a dentry lookup hit has found a valid entry
 * - NOTE! the hit can be a negative hit too, so we can't assume we have an
 *   inode
 */
static int afs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct afs_super_info *as = dentry->d_sb->s_fs_info;
	struct afs_vnode *vnode, *dir;
	struct afs_fid uninitialized_var(fid);
	struct dentry *parent;
	struct inode *inode;
	struct key *key;
	void *dir_version;
	int ret;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (as->dyn_root)
		return 1;

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

	dir_version = (void *) (unsigned long) dir->status.data_version;
	if (dentry->d_fsdata == dir_version)
		goto out_valid; /* the dir contents are unchanged */

	_debug("dir modified");

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
	dentry->d_fsdata = dir_version;
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
static void afs_d_release(struct dentry *dentry)
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
			fc.cb_break = dvnode->cb_break + dvnode->cb_s_break;
			afs_fs_create(&fc, dentry->d_name.name, mode,
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
			fc.cb_break = dvnode->cb_break + dvnode->cb_s_break;
			afs_fs_remove(&fc, dentry->d_name.name, true);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0)
			afs_dir_remove_subdir(dentry);
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
			fc.cb_break = dvnode->cb_break + dvnode->cb_s_break;
			afs_fs_remove(&fc, dentry->d_name.name, false);
		}

		afs_vnode_commit_status(&fc, dvnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
		if (ret == 0)
			ret = afs_dir_remove_link(
				dentry, key, d_version,
				(unsigned long)dvnode->status.data_version);
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
			fc.cb_break = dvnode->cb_break + dvnode->cb_s_break;
			afs_fs_create(&fc, dentry->d_name.name, mode,
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
	int ret;

	vnode = AFS_FS_I(d_inode(from));
	dvnode = AFS_FS_I(dir);

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
			fc.cb_break = dvnode->cb_break + dvnode->cb_s_break;
			fc.cb_break_2 = vnode->cb_break + vnode->cb_s_break;
			afs_fs_link(&fc, vnode, dentry->d_name.name);
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
			fc.cb_break = dvnode->cb_break + dvnode->cb_s_break;
			afs_fs_symlink(&fc, dentry->d_name.name, content,
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
	int ret;

	if (flags)
		return -EINVAL;

	vnode = AFS_FS_I(d_inode(old_dentry));
	orig_dvnode = AFS_FS_I(old_dir);
	new_dvnode = AFS_FS_I(new_dir);

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
			fc.cb_break = orig_dvnode->cb_break + orig_dvnode->cb_s_break;
			fc.cb_break_2 = new_dvnode->cb_break + new_dvnode->cb_s_break;
			afs_fs_rename(&fc, old_dentry->d_name.name,
				      new_dvnode, new_dentry->d_name.name);
		}

		afs_vnode_commit_status(&fc, orig_dvnode, fc.cb_break);
		afs_vnode_commit_status(&fc, new_dvnode, fc.cb_break_2);
		if (orig_dvnode != new_dvnode)
			mutex_unlock(&new_dvnode->io_lock);
		ret = afs_end_vnode_operation(&fc);
		if (ret < 0)
			goto error_key;
	}

error_key:
	key_put(key);
error:
	_leave(" = %d", ret);
	return ret;
}
