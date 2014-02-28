/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/llite/dir.c
 *
 * Directory code for lustre client.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>   // for wait_on_buffer
#include <linux/pagevec.h>
#include <linux/prefetch.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_lib.h>
#include <lustre/lustre_idl.h>
#include <lustre_lite.h>
#include <lustre_dlm.h>
#include <lustre_fid.h>
#include "llite_internal.h"

/*
 * (new) readdir implementation overview.
 *
 * Original lustre readdir implementation cached exact copy of raw directory
 * pages on the client. These pages were indexed in client page cache by
 * logical offset in the directory file. This design, while very simple and
 * intuitive had some inherent problems:
 *
 *     . it implies that byte offset to the directory entry serves as a
 *     telldir(3)/seekdir(3) cookie, but that offset is not stable: in
 *     ext3/htree directory entries may move due to splits, and more
 *     importantly,
 *
 *     . it is incompatible with the design of split directories for cmd3,
 *     that assumes that names are distributed across nodes based on their
 *     hash, and so readdir should be done in hash order.
 *
 * New readdir implementation does readdir in hash order, and uses hash of a
 * file name as a telldir/seekdir cookie. This led to number of complications:
 *
 *     . hash is not unique, so it cannot be used to index cached directory
 *     pages on the client (note, that it requires a whole pageful of hash
 *     collided entries to cause two pages to have identical hashes);
 *
 *     . hash is not unique, so it cannot, strictly speaking, be used as an
 *     entry cookie. ext3/htree has the same problem and lustre implementation
 *     mimics their solution: seekdir(hash) positions directory at the first
 *     entry with the given hash.
 *
 * Client side.
 *
 * 0. caching
 *
 * Client caches directory pages using hash of the first entry as an index. As
 * noted above hash is not unique, so this solution doesn't work as is:
 * special processing is needed for "page hash chains" (i.e., sequences of
 * pages filled with entries all having the same hash value).
 *
 * First, such chains have to be detected. To this end, server returns to the
 * client the hash of the first entry on the page next to one returned. When
 * client detects that this hash is the same as hash of the first entry on the
 * returned page, page hash collision has to be handled. Pages in the
 * hash chain, except first one, are termed "overflow pages".
 *
 * Solution to index uniqueness problem is to not cache overflow
 * pages. Instead, when page hash collision is detected, all overflow pages
 * from emerging chain are immediately requested from the server and placed in
 * a special data structure (struct ll_dir_chain). This data structure is used
 * by ll_readdir() to process entries from overflow pages. When readdir
 * invocation finishes, overflow pages are discarded. If page hash collision
 * chain weren't completely processed, next call to readdir will again detect
 * page hash collision, again read overflow pages in, process next portion of
 * entries and again discard the pages. This is not as wasteful as it looks,
 * because, given reasonable hash, page hash collisions are extremely rare.
 *
 * 1. directory positioning
 *
 * When seekdir(hash) is called, original
 *
 *
 *
 *
 *
 *
 *
 *
 * Server.
 *
 * identification of and access to overflow pages
 *
 * page format
 *
 * Page in MDS_READPAGE RPC is packed in LU_PAGE_SIZE, and each page contains
 * a header lu_dirpage which describes the start/end hash, and whether this
 * page is empty (contains no dir entry) or hash collide with next page.
 * After client receives reply, several pages will be integrated into dir page
 * in PAGE_CACHE_SIZE (if PAGE_CACHE_SIZE greater than LU_PAGE_SIZE), and the
 * lu_dirpage for this integrated page will be adjusted. See
 * lmv_adjust_dirpages().
 *
 */

/* returns the page unlocked, but with a reference */
static int ll_dir_filler(void *_hash, struct page *page0)
{
	struct inode *inode = page0->mapping->host;
	int hash64 = ll_i2sbi(inode)->ll_flags & LL_SBI_64BIT_HASH;
	struct obd_export *exp = ll_i2sbi(inode)->ll_md_exp;
	struct ptlrpc_request *request;
	struct mdt_body *body;
	struct md_op_data *op_data;
	__u64 hash = *((__u64 *)_hash);
	struct page **page_pool;
	struct page *page;
	struct lu_dirpage *dp;
	int max_pages = ll_i2sbi(inode)->ll_md_brw_size >> PAGE_CACHE_SHIFT;
	int nrdpgs = 0; /* number of pages read actually */
	int npages;
	int i;
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode=%lu/%u(%p) hash "LPU64"\n",
	       inode->i_ino, inode->i_generation, inode, hash);

	LASSERT(max_pages > 0 && max_pages <= MD_MAX_BRW_PAGES);

	OBD_ALLOC(page_pool, sizeof(page) * max_pages);
	if (page_pool != NULL) {
		page_pool[0] = page0;
	} else {
		page_pool = &page0;
		max_pages = 1;
	}
	for (npages = 1; npages < max_pages; npages++) {
		page = page_cache_alloc_cold(inode->i_mapping);
		if (!page)
			break;
		page_pool[npages] = page;
	}

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	op_data->op_npages = npages;
	op_data->op_offset = hash;
	rc = md_readpage(exp, op_data, page_pool, &request);
	ll_finish_md_op_data(op_data);
	if (rc == 0) {
		body = req_capsule_server_get(&request->rq_pill, &RMF_MDT_BODY);
		/* Checked by mdc_readpage() */
		LASSERT(body != NULL);

		if (body->valid & OBD_MD_FLSIZE)
			cl_isize_write(inode, body->size);

		nrdpgs = (request->rq_bulk->bd_nob_transferred+PAGE_CACHE_SIZE-1)
			 >> PAGE_CACHE_SHIFT;
		SetPageUptodate(page0);
	}
	unlock_page(page0);
	ptlrpc_req_finished(request);

	CDEBUG(D_VFSTRACE, "read %d/%d pages\n", nrdpgs, npages);

	ll_pagevec_init(&lru_pvec, 0);
	for (i = 1; i < npages; i++) {
		unsigned long offset;
		int ret;

		page = page_pool[i];

		if (rc < 0 || i >= nrdpgs) {
			page_cache_release(page);
			continue;
		}

		SetPageUptodate(page);

		dp = kmap(page);
		hash = le64_to_cpu(dp->ldp_hash_start);
		kunmap(page);

		offset = hash_x_index(hash, hash64);

		prefetchw(&page->flags);
		ret = add_to_page_cache_lru(page, inode->i_mapping, offset,
					    GFP_KERNEL);
		if (ret == 0) {
			unlock_page(page);
			if (ll_pagevec_add(&lru_pvec, page) == 0)
				ll_pagevec_lru_add_file(&lru_pvec);
		} else {
			CDEBUG(D_VFSTRACE, "page %lu add to page cache failed:"
			       " %d\n", offset, ret);
		}
		page_cache_release(page);
	}
	ll_pagevec_lru_add_file(&lru_pvec);

	if (page_pool != &page0)
		OBD_FREE(page_pool, sizeof(struct page *) * max_pages);
	return rc;
}

static void ll_check_page(struct inode *dir, struct page *page)
{
	/* XXX: check page format later */
	SetPageChecked(page);
}

void ll_release_page(struct page *page, int remove)
{
	kunmap(page);
	if (remove) {
		lock_page(page);
		if (likely(page->mapping != NULL))
			truncate_complete_page(page->mapping, page);
		unlock_page(page);
	}
	page_cache_release(page);
}

/*
 * Find, kmap and return page that contains given hash.
 */
static struct page *ll_dir_page_locate(struct inode *dir, __u64 *hash,
				       __u64 *start, __u64 *end)
{
	int hash64 = ll_i2sbi(dir)->ll_flags & LL_SBI_64BIT_HASH;
	struct address_space *mapping = dir->i_mapping;
	/*
	 * Complement of hash is used as an index so that
	 * radix_tree_gang_lookup() can be used to find a page with starting
	 * hash _smaller_ than one we are looking for.
	 */
	unsigned long offset = hash_x_index(*hash, hash64);
	struct page *page;
	int found;

	TREE_READ_LOCK_IRQ(mapping);
	found = radix_tree_gang_lookup(&mapping->page_tree,
				       (void **)&page, offset, 1);
	if (found > 0) {
		struct lu_dirpage *dp;

		page_cache_get(page);
		TREE_READ_UNLOCK_IRQ(mapping);
		/*
		 * In contrast to find_lock_page() we are sure that directory
		 * page cannot be truncated (while DLM lock is held) and,
		 * hence, can avoid restart.
		 *
		 * In fact, page cannot be locked here at all, because
		 * ll_dir_filler() does synchronous io.
		 */
		wait_on_page_locked(page);
		if (PageUptodate(page)) {
			dp = kmap(page);
			if (BITS_PER_LONG == 32 && hash64) {
				*start = le64_to_cpu(dp->ldp_hash_start) >> 32;
				*end   = le64_to_cpu(dp->ldp_hash_end) >> 32;
				*hash  = *hash >> 32;
			} else {
				*start = le64_to_cpu(dp->ldp_hash_start);
				*end   = le64_to_cpu(dp->ldp_hash_end);
			}
			LASSERTF(*start <= *hash, "start = "LPX64",end = "
				 LPX64",hash = "LPX64"\n", *start, *end, *hash);
			CDEBUG(D_VFSTRACE, "page %lu [%llu %llu], hash "LPU64"\n",
			       offset, *start, *end, *hash);
			if (*hash > *end) {
				ll_release_page(page, 0);
				page = NULL;
			} else if (*end != *start && *hash == *end) {
				/*
				 * upon hash collision, remove this page,
				 * otherwise put page reference, and
				 * ll_get_dir_page() will issue RPC to fetch
				 * the page we want.
				 */
				ll_release_page(page,
				    le32_to_cpu(dp->ldp_flags) & LDF_COLLIDE);
				page = NULL;
			}
		} else {
			page_cache_release(page);
			page = ERR_PTR(-EIO);
		}

	} else {
		TREE_READ_UNLOCK_IRQ(mapping);
		page = NULL;
	}
	return page;
}

struct page *ll_get_dir_page(struct inode *dir, __u64 hash,
			     struct ll_dir_chain *chain)
{
	ldlm_policy_data_t policy = {.l_inodebits = {MDS_INODELOCK_UPDATE} };
	struct address_space *mapping = dir->i_mapping;
	struct lustre_handle lockh;
	struct lu_dirpage *dp;
	struct page *page;
	ldlm_mode_t mode;
	int rc;
	__u64 start = 0;
	__u64 end = 0;
	__u64 lhash = hash;
	struct ll_inode_info *lli = ll_i2info(dir);
	int hash64 = ll_i2sbi(dir)->ll_flags & LL_SBI_64BIT_HASH;

	mode = LCK_PR;
	rc = md_lock_match(ll_i2sbi(dir)->ll_md_exp, LDLM_FL_BLOCK_GRANTED,
			   ll_inode2fid(dir), LDLM_IBITS, &policy, mode, &lockh);
	if (!rc) {
		struct ldlm_enqueue_info einfo = {
			.ei_type = LDLM_IBITS,
			.ei_mode = mode,
			.ei_cb_bl = ll_md_blocking_ast,
			.ei_cb_cp = ldlm_completion_ast,
		};
		struct lookup_intent it = { .it_op = IT_READDIR };
		struct ptlrpc_request *request;
		struct md_op_data *op_data;

		op_data = ll_prep_md_op_data(NULL, dir, NULL, NULL, 0, 0,
		LUSTRE_OPC_ANY, NULL);
		if (IS_ERR(op_data))
			return (void *)op_data;

		rc = md_enqueue(ll_i2sbi(dir)->ll_md_exp, &einfo, &it,
				op_data, &lockh, NULL, 0, NULL, 0);

		ll_finish_md_op_data(op_data);

		request = (struct ptlrpc_request *)it.d.lustre.it_data;
		if (request)
			ptlrpc_req_finished(request);
		if (rc < 0) {
			CERROR("lock enqueue: "DFID" at "LPU64": rc %d\n",
				PFID(ll_inode2fid(dir)), hash, rc);
			return ERR_PTR(rc);
		}

		CDEBUG(D_INODE, "setting lr_lvb_inode to inode %p (%lu/%u)\n",
		       dir, dir->i_ino, dir->i_generation);
		md_set_lock_data(ll_i2sbi(dir)->ll_md_exp,
				 &it.d.lustre.it_lock_handle, dir, NULL);
	} else {
		/* for cross-ref object, l_ast_data of the lock may not be set,
		 * we reset it here */
		md_set_lock_data(ll_i2sbi(dir)->ll_md_exp, &lockh.cookie,
				 dir, NULL);
	}
	ldlm_lock_dump_handle(D_OTHER, &lockh);

	mutex_lock(&lli->lli_readdir_mutex);
	page = ll_dir_page_locate(dir, &lhash, &start, &end);
	if (IS_ERR(page)) {
		CERROR("dir page locate: "DFID" at "LPU64": rc %ld\n",
		       PFID(ll_inode2fid(dir)), lhash, PTR_ERR(page));
		GOTO(out_unlock, page);
	} else if (page != NULL) {
		/*
		 * XXX nikita: not entirely correct handling of a corner case:
		 * suppose hash chain of entries with hash value HASH crosses
		 * border between pages P0 and P1. First both P0 and P1 are
		 * cached, seekdir() is called for some entry from the P0 part
		 * of the chain. Later P0 goes out of cache. telldir(HASH)
		 * happens and finds P1, as it starts with matching hash
		 * value. Remaining entries from P0 part of the chain are
		 * skipped. (Is that really a bug?)
		 *
		 * Possible solutions: 0. don't cache P1 is such case, handle
		 * it as an "overflow" page. 1. invalidate all pages at
		 * once. 2. use HASH|1 as an index for P1.
		 */
		GOTO(hash_collision, page);
	}

	page = read_cache_page(mapping, hash_x_index(hash, hash64),
			       ll_dir_filler, &lhash);
	if (IS_ERR(page)) {
		CERROR("read cache page: "DFID" at "LPU64": rc %ld\n",
		       PFID(ll_inode2fid(dir)), hash, PTR_ERR(page));
		GOTO(out_unlock, page);
	}

	wait_on_page_locked(page);
	(void)kmap(page);
	if (!PageUptodate(page)) {
		CERROR("page not updated: "DFID" at "LPU64": rc %d\n",
		       PFID(ll_inode2fid(dir)), hash, -5);
		goto fail;
	}
	if (!PageChecked(page))
		ll_check_page(dir, page);
	if (PageError(page)) {
		CERROR("page error: "DFID" at "LPU64": rc %d\n",
		       PFID(ll_inode2fid(dir)), hash, -5);
		goto fail;
	}
hash_collision:
	dp = page_address(page);
	if (BITS_PER_LONG == 32 && hash64) {
		start = le64_to_cpu(dp->ldp_hash_start) >> 32;
		end   = le64_to_cpu(dp->ldp_hash_end) >> 32;
		lhash = hash >> 32;
	} else {
		start = le64_to_cpu(dp->ldp_hash_start);
		end   = le64_to_cpu(dp->ldp_hash_end);
		lhash = hash;
	}
	if (end == start) {
		LASSERT(start == lhash);
		CWARN("Page-wide hash collision: "LPU64"\n", end);
		if (BITS_PER_LONG == 32 && hash64)
			CWARN("Real page-wide hash collision at ["LPU64" "LPU64
			      "] with hash "LPU64"\n",
			      le64_to_cpu(dp->ldp_hash_start),
			      le64_to_cpu(dp->ldp_hash_end), hash);
		/*
		 * Fetch whole overflow chain...
		 *
		 * XXX not yet.
		 */
		goto fail;
	}
out_unlock:
	mutex_unlock(&lli->lli_readdir_mutex);
	ldlm_lock_decref(&lockh, mode);
	return page;

fail:
	ll_release_page(page, 1);
	page = ERR_PTR(-EIO);
	goto out_unlock;
}

int ll_dir_read(struct inode *inode, struct dir_context *ctx)
{
	struct ll_inode_info *info       = ll_i2info(inode);
	struct ll_sb_info    *sbi	= ll_i2sbi(inode);
	__u64		   pos		= ctx->pos;
	int		   api32      = ll_need_32bit_api(sbi);
	int		   hash64     = sbi->ll_flags & LL_SBI_64BIT_HASH;
	struct page	  *page;
	struct ll_dir_chain   chain;
	int		   done = 0;
	int		   rc = 0;

	ll_dir_chain_init(&chain);

	page = ll_get_dir_page(inode, pos, &chain);

	while (rc == 0 && !done) {
		struct lu_dirpage *dp;
		struct lu_dirent  *ent;

		if (!IS_ERR(page)) {
			/*
			 * If page is empty (end of directory is reached),
			 * use this value.
			 */
			__u64 hash = MDS_DIR_END_OFF;
			__u64 next;

			dp = page_address(page);
			for (ent = lu_dirent_start(dp); ent != NULL && !done;
			     ent = lu_dirent_next(ent)) {
				__u16	  type;
				int	    namelen;
				struct lu_fid  fid;
				__u64	  lhash;
				__u64	  ino;

				/*
				 * XXX: implement correct swabbing here.
				 */

				hash = le64_to_cpu(ent->lde_hash);
				if (hash < pos)
					/*
					 * Skip until we find target hash
					 * value.
					 */
					continue;

				namelen = le16_to_cpu(ent->lde_namelen);
				if (namelen == 0)
					/*
					 * Skip dummy record.
					 */
					continue;

				if (api32 && hash64)
					lhash = hash >> 32;
				else
					lhash = hash;
				fid_le_to_cpu(&fid, &ent->lde_fid);
				ino = cl_fid_build_ino(&fid, api32);
				type = ll_dirent_type_get(ent);
				ctx->pos = lhash;
				/* For 'll_nfs_get_name_filldir()', it will try
				 * to access the 'ent' through its 'lde_name',
				 * so the parameter 'name' for 'ctx->actor()'
				 * must be part of the 'ent'.
				 */
				done = !dir_emit(ctx, ent->lde_name,
						 namelen, ino, type);
			}
			next = le64_to_cpu(dp->ldp_hash_end);
			if (!done) {
				pos = next;
				if (pos == MDS_DIR_END_OFF) {
					/*
					 * End of directory reached.
					 */
					done = 1;
					ll_release_page(page, 0);
				} else if (1 /* chain is exhausted*/) {
					/*
					 * Normal case: continue to the next
					 * page.
					 */
					ll_release_page(page,
					    le32_to_cpu(dp->ldp_flags) &
							LDF_COLLIDE);
					next = pos;
					page = ll_get_dir_page(inode, pos,
							       &chain);
				} else {
					/*
					 * go into overflow page.
					 */
					LASSERT(le32_to_cpu(dp->ldp_flags) &
						LDF_COLLIDE);
					ll_release_page(page, 1);
				}
			} else {
				pos = hash;
				ll_release_page(page, 0);
			}
		} else {
			rc = PTR_ERR(page);
			CERROR("error reading dir "DFID" at %lu: rc %d\n",
			       PFID(&info->lli_fid), (unsigned long)pos, rc);
		}
	}

	ctx->pos = pos;
	ll_dir_chain_fini(&chain);
	return rc;
}

static int ll_readdir(struct file *filp, struct dir_context *ctx)
{
	struct inode		*inode	= filp->f_dentry->d_inode;
	struct ll_file_data	*lfd	= LUSTRE_FPRIVATE(filp);
	struct ll_sb_info	*sbi	= ll_i2sbi(inode);
	int			hash64	= sbi->ll_flags & LL_SBI_64BIT_HASH;
	int			api32	= ll_need_32bit_api(sbi);
	int			rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode=%lu/%u(%p) pos %lu/%llu "
	       " 32bit_api %d\n", inode->i_ino, inode->i_generation,
	       inode, (unsigned long)lfd->lfd_pos, i_size_read(inode), api32);

	if (lfd->lfd_pos == MDS_DIR_END_OFF)
		/*
		 * end-of-file.
		 */
		GOTO(out, rc = 0);

	ctx->pos = lfd->lfd_pos;
	rc = ll_dir_read(inode, ctx);
	lfd->lfd_pos = ctx->pos;
	if (ctx->pos == MDS_DIR_END_OFF) {
		if (api32)
			ctx->pos = LL_DIR_END_OFF_32BIT;
		else
			ctx->pos = LL_DIR_END_OFF;
	} else {
		if (api32 && hash64)
			ctx->pos >>= 32;
	}
	filp->f_version = inode->i_version;

out:
	if (!rc)
		ll_stats_ops_tally(sbi, LPROC_LL_READDIR, 1);

	return rc;
}

int ll_send_mgc_param(struct obd_export *mgc, char *string)
{
	struct mgs_send_param *msp;
	int rc = 0;

	OBD_ALLOC_PTR(msp);
	if (!msp)
		return -ENOMEM;

	strncpy(msp->mgs_param, string, MGS_PARAM_MAXLEN);
	rc = obd_set_info_async(NULL, mgc, sizeof(KEY_SET_INFO), KEY_SET_INFO,
				sizeof(struct mgs_send_param), msp, NULL);
	if (rc)
		CERROR("Failed to set parameter: %d\n", rc);
	OBD_FREE_PTR(msp);

	return rc;
}

int ll_dir_setdirstripe(struct inode *dir, struct lmv_user_md *lump,
			char *filename)
{
	struct ptlrpc_request *request = NULL;
	struct md_op_data *op_data;
	struct ll_sb_info *sbi = ll_i2sbi(dir);
	int mode;
	int err;

	mode = (0755 & (S_IRWXUGO|S_ISVTX) & ~current->fs->umask) | S_IFDIR;
	op_data = ll_prep_md_op_data(NULL, dir, NULL, filename,
				     strlen(filename), mode, LUSTRE_OPC_MKDIR,
				     lump);
	if (IS_ERR(op_data))
		GOTO(err_exit, err = PTR_ERR(op_data));

	op_data->op_cli_flags |= CLI_SET_MEA;
	err = md_create(sbi->ll_md_exp, op_data, lump, sizeof(*lump), mode,
			from_kuid(&init_user_ns, current_fsuid()),
			from_kgid(&init_user_ns, current_fsgid()),
			cfs_curproc_cap_pack(), 0, &request);
	ll_finish_md_op_data(op_data);
	if (err)
		GOTO(err_exit, err);
err_exit:
	ptlrpc_req_finished(request);
	return err;
}

int ll_dir_setstripe(struct inode *inode, struct lov_user_md *lump,
		     int set_default)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct md_op_data *op_data;
	struct ptlrpc_request *req = NULL;
	int rc = 0;
	struct lustre_sb_info *lsi = s2lsi(inode->i_sb);
	struct obd_device *mgc = lsi->lsi_mgc;
	int lum_size;

	if (lump != NULL) {
		/*
		 * This is coming from userspace, so should be in
		 * local endian.  But the MDS would like it in little
		 * endian, so we swab it before we send it.
		 */
		switch (lump->lmm_magic) {
		case LOV_USER_MAGIC_V1: {
			if (lump->lmm_magic != cpu_to_le32(LOV_USER_MAGIC_V1))
				lustre_swab_lov_user_md_v1(lump);
			lum_size = sizeof(struct lov_user_md_v1);
			break;
		}
		case LOV_USER_MAGIC_V3: {
			if (lump->lmm_magic != cpu_to_le32(LOV_USER_MAGIC_V3))
				lustre_swab_lov_user_md_v3(
					(struct lov_user_md_v3 *)lump);
			lum_size = sizeof(struct lov_user_md_v3);
			break;
		}
		default: {
			CDEBUG(D_IOCTL, "bad userland LOV MAGIC:"
					" %#08x != %#08x nor %#08x\n",
					lump->lmm_magic, LOV_USER_MAGIC_V1,
					LOV_USER_MAGIC_V3);
			return -EINVAL;
		}
		}
	} else {
		lum_size = sizeof(struct lov_user_md_v1);
	}

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	if (lump != NULL && lump->lmm_magic == cpu_to_le32(LMV_USER_MAGIC))
		op_data->op_cli_flags |= CLI_SET_MEA;

	/* swabbing is done in lov_setstripe() on server side */
	rc = md_setattr(sbi->ll_md_exp, op_data, lump, lum_size,
			NULL, 0, &req, NULL);
	ll_finish_md_op_data(op_data);
	ptlrpc_req_finished(req);
	if (rc) {
		if (rc != -EPERM && rc != -EACCES)
			CERROR("mdc_setattr fails: rc = %d\n", rc);
	}

	/* In the following we use the fact that LOV_USER_MAGIC_V1 and
	 LOV_USER_MAGIC_V3 have the same initial fields so we do not
	 need the make the distiction between the 2 versions */
	if (set_default && mgc->u.cli.cl_mgc_mgsexp) {
		char *param = NULL;
		char *buf;

		OBD_ALLOC(param, MGS_PARAM_MAXLEN);
		if (param == NULL)
			GOTO(end, rc = -ENOMEM);

		buf = param;
		/* Get fsname and assume devname to be -MDT0000. */
		ll_get_fsname(inode->i_sb, buf, MTI_NAME_MAXLEN);
		strcat(buf, "-MDT0000.lov");
		buf += strlen(buf);

		/* Set root stripesize */
		sprintf(buf, ".stripesize=%u",
			lump ? le32_to_cpu(lump->lmm_stripe_size) : 0);
		rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
		if (rc)
			GOTO(end, rc);

		/* Set root stripecount */
		sprintf(buf, ".stripecount=%hd",
			lump ? le16_to_cpu(lump->lmm_stripe_count) : 0);
		rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
		if (rc)
			GOTO(end, rc);

		/* Set root stripeoffset */
		sprintf(buf, ".stripeoffset=%hd",
			lump ? le16_to_cpu(lump->lmm_stripe_offset) :
			(typeof(lump->lmm_stripe_offset))(-1));
		rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);

end:
		if (param != NULL)
			OBD_FREE(param, MGS_PARAM_MAXLEN);
	}
	return rc;
}

int ll_dir_getstripe(struct inode *inode, struct lov_mds_md **lmmp,
		     int *lmm_size, struct ptlrpc_request **request)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct mdt_body   *body;
	struct lov_mds_md *lmm = NULL;
	struct ptlrpc_request *req = NULL;
	int rc, lmmsize;
	struct md_op_data *op_data;

	rc = ll_get_max_mdsize(sbi, &lmmsize);
	if (rc)
		return rc;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL,
				     0, lmmsize, LUSTRE_OPC_ANY,
				     NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	op_data->op_valid = OBD_MD_FLEASIZE | OBD_MD_FLDIREA;
	rc = md_getattr(sbi->ll_md_exp, op_data, &req);
	ll_finish_md_op_data(op_data);
	if (rc < 0) {
		CDEBUG(D_INFO, "md_getattr failed on inode "
		       "%lu/%u: rc %d\n", inode->i_ino,
		       inode->i_generation, rc);
		GOTO(out, rc);
	}

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	LASSERT(body != NULL);

	lmmsize = body->eadatasize;

	if (!(body->valid & (OBD_MD_FLEASIZE | OBD_MD_FLDIREA)) ||
	    lmmsize == 0) {
		GOTO(out, rc = -ENODATA);
	}

	lmm = req_capsule_server_sized_get(&req->rq_pill,
					   &RMF_MDT_MD, lmmsize);
	LASSERT(lmm != NULL);

	/*
	 * This is coming from the MDS, so is probably in
	 * little endian.  We convert it to host endian before
	 * passing it to userspace.
	 */
	/* We don't swab objects for directories */
	switch (le32_to_cpu(lmm->lmm_magic)) {
	case LOV_MAGIC_V1:
		if (LOV_MAGIC != cpu_to_le32(LOV_MAGIC))
			lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lmm);
		break;
	case LOV_MAGIC_V3:
		if (LOV_MAGIC != cpu_to_le32(LOV_MAGIC))
			lustre_swab_lov_user_md_v3((struct lov_user_md_v3 *)lmm);
		break;
	default:
		CERROR("unknown magic: %lX\n", (unsigned long)lmm->lmm_magic);
		rc = -EPROTO;
	}
out:
	*lmmp = lmm;
	*lmm_size = lmmsize;
	*request = req;
	return rc;
}

/*
 *  Get MDT index for the inode.
 */
int ll_get_mdt_idx(struct inode *inode)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct md_op_data *op_data;
	int rc, mdtidx;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0,
				     0, LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	op_data->op_flags |= MF_GET_MDT_IDX;
	rc = md_getattr(sbi->ll_md_exp, op_data, NULL);
	mdtidx = op_data->op_mds;
	ll_finish_md_op_data(op_data);
	if (rc < 0) {
		CDEBUG(D_INFO, "md_getattr_name: %d\n", rc);
		return rc;
	}
	return mdtidx;
}

/**
 * Generic handler to do any pre-copy work.
 *
 * It send a first hsm_progress (with extent length == 0) to coordinator as a
 * first information for it that real work has started.
 *
 * Moreover, for a ARCHIVE request, it will sample the file data version and
 * store it in \a copy.
 *
 * \return 0 on success.
 */
static int ll_ioc_copy_start(struct super_block *sb, struct hsm_copy *copy)
{
	struct ll_sb_info		*sbi = ll_s2sbi(sb);
	struct hsm_progress_kernel	 hpk;
	int				 rc;

	/* Forge a hsm_progress based on data from copy. */
	hpk.hpk_fid = copy->hc_hai.hai_fid;
	hpk.hpk_cookie = copy->hc_hai.hai_cookie;
	hpk.hpk_extent.offset = copy->hc_hai.hai_extent.offset;
	hpk.hpk_extent.length = 0;
	hpk.hpk_flags = 0;
	hpk.hpk_errval = 0;
	hpk.hpk_data_version = 0;


	/* For archive request, we need to read the current file version. */
	if (copy->hc_hai.hai_action == HSMA_ARCHIVE) {
		struct inode	*inode;
		__u64		 data_version = 0;

		/* Get inode for this fid */
		inode = search_inode_for_lustre(sb, &copy->hc_hai.hai_fid);
		if (IS_ERR(inode)) {
			hpk.hpk_flags |= HP_FLAG_RETRY;
			/* hpk_errval is >= 0 */
			hpk.hpk_errval = -PTR_ERR(inode);
			GOTO(progress, rc = PTR_ERR(inode));
		}

		/* Read current file data version */
		rc = ll_data_version(inode, &data_version, 1);
		iput(inode);
		if (rc != 0) {
			CDEBUG(D_HSM, "Could not read file data version of "
				      DFID" (rc = %d). Archive request ("
				      LPX64") could not be done.\n",
				      PFID(&copy->hc_hai.hai_fid), rc,
				      copy->hc_hai.hai_cookie);
			hpk.hpk_flags |= HP_FLAG_RETRY;
			/* hpk_errval must be >= 0 */
			hpk.hpk_errval = -rc;
			GOTO(progress, rc);
		}

		/* Store it the hsm_copy for later copytool use.
		 * Always modified even if no lsm. */
		copy->hc_data_version = data_version;
	}

progress:
	rc = obd_iocontrol(LL_IOC_HSM_PROGRESS, sbi->ll_md_exp, sizeof(hpk),
			   &hpk, NULL);

	return rc;
}

/**
 * Generic handler to do any post-copy work.
 *
 * It will send the last hsm_progress update to coordinator to inform it
 * that copy is finished and whether it was successful or not.
 *
 * Moreover,
 * - for ARCHIVE request, it will sample the file data version and compare it
 *   with the version saved in ll_ioc_copy_start(). If they do not match, copy
 *   will be considered as failed.
 * - for RESTORE request, it will sample the file data version and send it to
 *   coordinator which is useful if the file was imported as 'released'.
 *
 * \return 0 on success.
 */
static int ll_ioc_copy_end(struct super_block *sb, struct hsm_copy *copy)
{
	struct ll_sb_info		*sbi = ll_s2sbi(sb);
	struct hsm_progress_kernel	 hpk;
	int				 rc;

	/* If you modify the logic here, also check llapi_hsm_copy_end(). */
	/* Take care: copy->hc_hai.hai_action, len, gid and data are not
	 * initialized if copy_end was called with copy == NULL.
	 */

	/* Forge a hsm_progress based on data from copy. */
	hpk.hpk_fid = copy->hc_hai.hai_fid;
	hpk.hpk_cookie = copy->hc_hai.hai_cookie;
	hpk.hpk_extent = copy->hc_hai.hai_extent;
	hpk.hpk_flags = copy->hc_flags | HP_FLAG_COMPLETED;
	hpk.hpk_errval = copy->hc_errval;
	hpk.hpk_data_version = 0;

	/* For archive request, we need to check the file data was not changed.
	 *
	 * For restore request, we need to send the file data version, this is
	 * useful when the file was created using hsm_import.
	 */
	if (((copy->hc_hai.hai_action == HSMA_ARCHIVE) ||
	     (copy->hc_hai.hai_action == HSMA_RESTORE)) &&
	    (copy->hc_errval == 0)) {
		struct inode	*inode;
		__u64		 data_version = 0;

		/* Get lsm for this fid */
		inode = search_inode_for_lustre(sb, &copy->hc_hai.hai_fid);
		if (IS_ERR(inode)) {
			hpk.hpk_flags |= HP_FLAG_RETRY;
			/* hpk_errval must be >= 0 */
			hpk.hpk_errval = -PTR_ERR(inode);
			GOTO(progress, rc = PTR_ERR(inode));
		}

		rc = ll_data_version(inode, &data_version,
				     copy->hc_hai.hai_action == HSMA_ARCHIVE);
		iput(inode);
		if (rc) {
			CDEBUG(D_HSM, "Could not read file data version. "
				      "Request could not be confirmed.\n");
			if (hpk.hpk_errval == 0)
				hpk.hpk_errval = -rc;
			GOTO(progress, rc);
		}

		/* Store it the hsm_copy for later copytool use.
		 * Always modified even if no lsm. */
		hpk.hpk_data_version = data_version;

		/* File could have been stripped during archiving, so we need
		 * to check anyway. */
		if ((copy->hc_hai.hai_action == HSMA_ARCHIVE) &&
		    (copy->hc_data_version != data_version)) {
			CDEBUG(D_HSM, "File data version mismatched. "
			      "File content was changed during archiving. "
			       DFID", start:"LPX64" current:"LPX64"\n",
			       PFID(&copy->hc_hai.hai_fid),
			       copy->hc_data_version, data_version);
			/* File was changed, send error to cdt. Do not ask for
			 * retry because if a file is modified frequently,
			 * the cdt will loop on retried archive requests.
			 * The policy engine will ask for a new archive later
			 * when the file will not be modified for some tunable
			 * time */
			/* we do not notify caller */
			hpk.hpk_flags &= ~HP_FLAG_RETRY;
			/* hpk_errval must be >= 0 */
			hpk.hpk_errval = EBUSY;
		}

	}

progress:
	rc = obd_iocontrol(LL_IOC_HSM_PROGRESS, sbi->ll_md_exp, sizeof(hpk),
			   &hpk, NULL);

	return rc;
}


static int copy_and_ioctl(int cmd, struct obd_export *exp, void *data, int len)
{
	void *ptr;
	int rc;

	OBD_ALLOC(ptr, len);
	if (ptr == NULL)
		return -ENOMEM;
	if (copy_from_user(ptr, data, len)) {
		OBD_FREE(ptr, len);
		return -EFAULT;
	}
	rc = obd_iocontrol(cmd, exp, len, data, NULL);
	OBD_FREE(ptr, len);
	return rc;
}

static int quotactl_ioctl(struct ll_sb_info *sbi, struct if_quotactl *qctl)
{
	int cmd = qctl->qc_cmd;
	int type = qctl->qc_type;
	int id = qctl->qc_id;
	int valid = qctl->qc_valid;
	int rc = 0;

	switch (cmd) {
	case LUSTRE_Q_INVALIDATE:
	case LUSTRE_Q_FINVALIDATE:
	case Q_QUOTAON:
	case Q_QUOTAOFF:
	case Q_SETQUOTA:
	case Q_SETINFO:
		if (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
		    sbi->ll_flags & LL_SBI_RMT_CLIENT)
			return -EPERM;
		break;
	case Q_GETQUOTA:
		if (((type == USRQUOTA &&
		      !uid_eq(current_euid(), make_kuid(&init_user_ns, id))) ||
		     (type == GRPQUOTA &&
		      !in_egroup_p(make_kgid(&init_user_ns, id)))) &&
		    (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
		     sbi->ll_flags & LL_SBI_RMT_CLIENT))
			return -EPERM;
		break;
	case Q_GETINFO:
		break;
	default:
		CERROR("unsupported quotactl op: %#x\n", cmd);
		return -ENOTTY;
	}

	if (valid != QC_GENERAL) {
		if (sbi->ll_flags & LL_SBI_RMT_CLIENT)
			return -EOPNOTSUPP;

		if (cmd == Q_GETINFO)
			qctl->qc_cmd = Q_GETOINFO;
		else if (cmd == Q_GETQUOTA)
			qctl->qc_cmd = Q_GETOQUOTA;
		else
			return -EINVAL;

		switch (valid) {
		case QC_MDTIDX:
			rc = obd_iocontrol(OBD_IOC_QUOTACTL, sbi->ll_md_exp,
					   sizeof(*qctl), qctl, NULL);
			break;
		case QC_OSTIDX:
			rc = obd_iocontrol(OBD_IOC_QUOTACTL, sbi->ll_dt_exp,
					   sizeof(*qctl), qctl, NULL);
			break;
		case QC_UUID:
			rc = obd_iocontrol(OBD_IOC_QUOTACTL, sbi->ll_md_exp,
					   sizeof(*qctl), qctl, NULL);
			if (rc == -EAGAIN)
				rc = obd_iocontrol(OBD_IOC_QUOTACTL,
						   sbi->ll_dt_exp,
						   sizeof(*qctl), qctl, NULL);
			break;
		default:
			rc = -EINVAL;
			break;
		}

		if (rc)
			return rc;

		qctl->qc_cmd = cmd;
	} else {
		struct obd_quotactl *oqctl;

		OBD_ALLOC_PTR(oqctl);
		if (oqctl == NULL)
			return -ENOMEM;

		QCTL_COPY(oqctl, qctl);
		rc = obd_quotactl(sbi->ll_md_exp, oqctl);
		if (rc) {
			if (rc != -EALREADY && cmd == Q_QUOTAON) {
				oqctl->qc_cmd = Q_QUOTAOFF;
				obd_quotactl(sbi->ll_md_exp, oqctl);
			}
			OBD_FREE_PTR(oqctl);
			return rc;
		}
		/* If QIF_SPACE is not set, client should collect the
		 * space usage from OSSs by itself */
		if (cmd == Q_GETQUOTA &&
		    !(oqctl->qc_dqblk.dqb_valid & QIF_SPACE) &&
		    !oqctl->qc_dqblk.dqb_curspace) {
			struct obd_quotactl *oqctl_tmp;

			OBD_ALLOC_PTR(oqctl_tmp);
			if (oqctl_tmp == NULL)
				GOTO(out, rc = -ENOMEM);

			oqctl_tmp->qc_cmd = Q_GETOQUOTA;
			oqctl_tmp->qc_id = oqctl->qc_id;
			oqctl_tmp->qc_type = oqctl->qc_type;

			/* collect space usage from OSTs */
			oqctl_tmp->qc_dqblk.dqb_curspace = 0;
			rc = obd_quotactl(sbi->ll_dt_exp, oqctl_tmp);
			if (!rc || rc == -EREMOTEIO) {
				oqctl->qc_dqblk.dqb_curspace =
					oqctl_tmp->qc_dqblk.dqb_curspace;
				oqctl->qc_dqblk.dqb_valid |= QIF_SPACE;
			}

			/* collect space & inode usage from MDTs */
			oqctl_tmp->qc_dqblk.dqb_curspace = 0;
			oqctl_tmp->qc_dqblk.dqb_curinodes = 0;
			rc = obd_quotactl(sbi->ll_md_exp, oqctl_tmp);
			if (!rc || rc == -EREMOTEIO) {
				oqctl->qc_dqblk.dqb_curspace +=
					oqctl_tmp->qc_dqblk.dqb_curspace;
				oqctl->qc_dqblk.dqb_curinodes =
					oqctl_tmp->qc_dqblk.dqb_curinodes;
				oqctl->qc_dqblk.dqb_valid |= QIF_INODES;
			} else {
				oqctl->qc_dqblk.dqb_valid &= ~QIF_SPACE;
			}

			OBD_FREE_PTR(oqctl_tmp);
		}
out:
		QCTL_COPY(qctl, oqctl);
		OBD_FREE_PTR(oqctl);
	}

	return rc;
}

static char *
ll_getname(const char __user *filename)
{
	int ret = 0, len;
	char *tmp = __getname();

	if (!tmp)
		return ERR_PTR(-ENOMEM);

	len = strncpy_from_user(tmp, filename, PATH_MAX);
	if (len == 0)
		ret = -ENOENT;
	else if (len > PATH_MAX)
		ret = -ENAMETOOLONG;

	if (ret) {
		__putname(tmp);
		tmp =  ERR_PTR(ret);
	}
	return tmp;
}

#define ll_putname(filename) __putname(filename)

static long ll_dir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct obd_ioctl_data *data;
	int rc = 0;

	CDEBUG(D_VFSTRACE, "VFS Op:inode=%lu/%u(%p), cmd=%#x\n",
	       inode->i_ino, inode->i_generation, inode, cmd);

	/* asm-ppc{,64} declares TCGETS, et. al. as type 't' not 'T' */
	if (_IOC_TYPE(cmd) == 'T' || _IOC_TYPE(cmd) == 't') /* tty ioctls */
		return -ENOTTY;

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_IOCTL, 1);
	switch(cmd) {
	case FSFILT_IOC_GETFLAGS:
	case FSFILT_IOC_SETFLAGS:
		return ll_iocontrol(inode, file, cmd, arg);
	case FSFILT_IOC_GETVERSION_OLD:
	case FSFILT_IOC_GETVERSION:
		return put_user(inode->i_generation, (int *)arg);
	/* We need to special case any other ioctls we want to handle,
	 * to send them to the MDS/OST as appropriate and to properly
	 * network encode the arg field.
	case FSFILT_IOC_SETVERSION_OLD:
	case FSFILT_IOC_SETVERSION:
	*/
	case LL_IOC_GET_MDTIDX: {
		int mdtidx;

		mdtidx = ll_get_mdt_idx(inode);
		if (mdtidx < 0)
			return mdtidx;

		if (put_user((int)mdtidx, (int*)arg))
			return -EFAULT;

		return 0;
	}
	case IOC_MDC_LOOKUP: {
		struct ptlrpc_request *request = NULL;
		int namelen, len = 0;
		char *buf = NULL;
		char *filename;
		struct md_op_data *op_data;

		rc = obd_ioctl_getdata(&buf, &len, (void *)arg);
		if (rc)
			return rc;
		data = (void *)buf;

		filename = data->ioc_inlbuf1;
		namelen = strlen(filename);

		if (namelen < 1) {
			CDEBUG(D_INFO, "IOC_MDC_LOOKUP missing filename\n");
			GOTO(out_free, rc = -EINVAL);
		}

		op_data = ll_prep_md_op_data(NULL, inode, NULL, filename, namelen,
					     0, LUSTRE_OPC_ANY, NULL);
		if (IS_ERR(op_data))
			GOTO(out_free, rc = PTR_ERR(op_data));

		op_data->op_valid = OBD_MD_FLID;
		rc = md_getattr_name(sbi->ll_md_exp, op_data, &request);
		ll_finish_md_op_data(op_data);
		if (rc < 0) {
			CDEBUG(D_INFO, "md_getattr_name: %d\n", rc);
			GOTO(out_free, rc);
		}
		ptlrpc_req_finished(request);
out_free:
		obd_ioctl_freedata(buf, len);
		return rc;
	}
	case LL_IOC_LMV_SETSTRIPE: {
		struct lmv_user_md  *lum;
		char		*buf = NULL;
		char		*filename;
		int		 namelen = 0;
		int		 lumlen = 0;
		int		 len;
		int		 rc;

		rc = obd_ioctl_getdata(&buf, &len, (void *)arg);
		if (rc)
			return rc;

		data = (void *)buf;
		if (data->ioc_inlbuf1 == NULL || data->ioc_inlbuf2 == NULL ||
		    data->ioc_inllen1 == 0 || data->ioc_inllen2 == 0)
			GOTO(lmv_out_free, rc = -EINVAL);

		filename = data->ioc_inlbuf1;
		namelen = data->ioc_inllen1;

		if (namelen < 1) {
			CDEBUG(D_INFO, "IOC_MDC_LOOKUP missing filename\n");
			GOTO(lmv_out_free, rc = -EINVAL);
		}
		lum = (struct lmv_user_md *)data->ioc_inlbuf2;
		lumlen = data->ioc_inllen2;

		if (lum->lum_magic != LMV_USER_MAGIC ||
		    lumlen != sizeof(*lum)) {
			CERROR("%s: wrong lum magic %x or size %d: rc = %d\n",
			       filename, lum->lum_magic, lumlen, -EFAULT);
			GOTO(lmv_out_free, rc = -EINVAL);
		}

		/**
		 * ll_dir_setdirstripe will be used to set dir stripe
		 *  mdc_create--->mdt_reint_create (with dirstripe)
		 */
		rc = ll_dir_setdirstripe(inode, lum, filename);
lmv_out_free:
		obd_ioctl_freedata(buf, len);
		return rc;

	}
	case LL_IOC_LOV_SETSTRIPE: {
		struct lov_user_md_v3 lumv3;
		struct lov_user_md_v1 *lumv1 = (struct lov_user_md_v1 *)&lumv3;
		struct lov_user_md_v1 *lumv1p = (struct lov_user_md_v1 *)arg;
		struct lov_user_md_v3 *lumv3p = (struct lov_user_md_v3 *)arg;

		int set_default = 0;

		LASSERT(sizeof(lumv3) == sizeof(*lumv3p));
		LASSERT(sizeof(lumv3.lmm_objects[0]) ==
			sizeof(lumv3p->lmm_objects[0]));
		/* first try with v1 which is smaller than v3 */
		if (copy_from_user(lumv1, lumv1p, sizeof(*lumv1)))
			return -EFAULT;

		if ((lumv1->lmm_magic == LOV_USER_MAGIC_V3) ) {
			if (copy_from_user(&lumv3, lumv3p, sizeof(lumv3)))
				return -EFAULT;
		}

		if (inode->i_sb->s_root == file->f_dentry)
			set_default = 1;

		/* in v1 and v3 cases lumv1 points to data */
		rc = ll_dir_setstripe(inode, lumv1, set_default);

		return rc;
	}
	case LL_IOC_LMV_GETSTRIPE: {
		struct lmv_user_md *lump = (struct lmv_user_md *)arg;
		struct lmv_user_md lum;
		struct lmv_user_md *tmp;
		int lum_size;
		int rc = 0;
		int mdtindex;

		if (copy_from_user(&lum, lump, sizeof(struct lmv_user_md)))
			return -EFAULT;

		if (lum.lum_magic != LMV_MAGIC_V1)
			return -EINVAL;

		lum_size = lmv_user_md_size(1, LMV_MAGIC_V1);
		OBD_ALLOC(tmp, lum_size);
		if (tmp == NULL)
			GOTO(free_lmv, rc = -ENOMEM);

		memcpy(tmp, &lum, sizeof(lum));
		tmp->lum_type = LMV_STRIPE_TYPE;
		tmp->lum_stripe_count = 1;
		mdtindex = ll_get_mdt_idx(inode);
		if (mdtindex < 0)
			GOTO(free_lmv, rc = -ENOMEM);

		tmp->lum_stripe_offset = mdtindex;
		tmp->lum_objects[0].lum_mds = mdtindex;
		memcpy(&tmp->lum_objects[0].lum_fid, ll_inode2fid(inode),
		       sizeof(struct lu_fid));
		if (copy_to_user((void *)arg, tmp, lum_size))
			GOTO(free_lmv, rc = -EFAULT);
free_lmv:
		if (tmp)
			OBD_FREE(tmp, lum_size);
		return rc;
	}
	case LL_IOC_REMOVE_ENTRY: {
		char		*filename = NULL;
		int		 namelen = 0;
		int		 rc;

		/* Here is a little hack to avoid sending REINT_RMENTRY to
		 * unsupported server, which might crash the server(LU-2730),
		 * Because both LVB_TYPE and REINT_RMENTRY will be supported
		 * on 2.4, we use OBD_CONNECT_LVB_TYPE to detect whether the
		 * server will support REINT_RMENTRY XXX*/
		if (!(exp_connect_flags(sbi->ll_md_exp) & OBD_CONNECT_LVB_TYPE))
			return -ENOTSUPP;

		filename = ll_getname((const char *)arg);
		if (IS_ERR(filename))
			return PTR_ERR(filename);

		namelen = strlen(filename);
		if (namelen < 1)
			GOTO(out_rmdir, rc = -EINVAL);

		rc = ll_rmdir_entry(inode, filename, namelen);
out_rmdir:
		if (filename)
			ll_putname(filename);
		return rc;
	}
	case LL_IOC_LOV_SWAP_LAYOUTS:
		return -EPERM;
	case LL_IOC_OBD_STATFS:
		return ll_obd_statfs(inode, (void *)arg);
	case LL_IOC_LOV_GETSTRIPE:
	case LL_IOC_MDC_GETINFO:
	case IOC_MDC_GETFILEINFO:
	case IOC_MDC_GETFILESTRIPE: {
		struct ptlrpc_request *request = NULL;
		struct lov_user_md *lump;
		struct lov_mds_md *lmm = NULL;
		struct mdt_body *body;
		char *filename = NULL;
		int lmmsize;

		if (cmd == IOC_MDC_GETFILEINFO ||
		    cmd == IOC_MDC_GETFILESTRIPE) {
			filename = ll_getname((const char *)arg);
			if (IS_ERR(filename))
				return PTR_ERR(filename);

			rc = ll_lov_getstripe_ea_info(inode, filename, &lmm,
						      &lmmsize, &request);
		} else {
			rc = ll_dir_getstripe(inode, &lmm, &lmmsize, &request);
		}

		if (request) {
			body = req_capsule_server_get(&request->rq_pill,
						      &RMF_MDT_BODY);
			LASSERT(body != NULL);
		} else {
			GOTO(out_req, rc);
		}

		if (rc < 0) {
			if (rc == -ENODATA && (cmd == IOC_MDC_GETFILEINFO ||
					       cmd == LL_IOC_MDC_GETINFO))
				GOTO(skip_lmm, rc = 0);
			else
				GOTO(out_req, rc);
		}

		if (cmd == IOC_MDC_GETFILESTRIPE ||
		    cmd == LL_IOC_LOV_GETSTRIPE) {
			lump = (struct lov_user_md *)arg;
		} else {
			struct lov_user_mds_data *lmdp;
			lmdp = (struct lov_user_mds_data *)arg;
			lump = &lmdp->lmd_lmm;
		}
		if (copy_to_user(lump, lmm, lmmsize)) {
			if (copy_to_user(lump, lmm, sizeof(*lump)))
				GOTO(out_req, rc = -EFAULT);
			rc = -EOVERFLOW;
		}
	skip_lmm:
		if (cmd == IOC_MDC_GETFILEINFO || cmd == LL_IOC_MDC_GETINFO) {
			struct lov_user_mds_data *lmdp;
			lstat_t st = { 0 };

			st.st_dev     = inode->i_sb->s_dev;
			st.st_mode    = body->mode;
			st.st_nlink   = body->nlink;
			st.st_uid     = body->uid;
			st.st_gid     = body->gid;
			st.st_rdev    = body->rdev;
			st.st_size    = body->size;
			st.st_blksize = PAGE_CACHE_SIZE;
			st.st_blocks  = body->blocks;
			st.st_atime   = body->atime;
			st.st_mtime   = body->mtime;
			st.st_ctime   = body->ctime;
			st.st_ino     = inode->i_ino;

			lmdp = (struct lov_user_mds_data *)arg;
			if (copy_to_user(&lmdp->lmd_st, &st, sizeof(st)))
				GOTO(out_req, rc = -EFAULT);
		}

	out_req:
		ptlrpc_req_finished(request);
		if (filename)
			ll_putname(filename);
		return rc;
	}
	case IOC_LOV_GETINFO: {
		struct lov_user_mds_data *lumd;
		struct lov_stripe_md *lsm;
		struct lov_user_md *lum;
		struct lov_mds_md *lmm;
		int lmmsize;
		lstat_t st;

		lumd = (struct lov_user_mds_data *)arg;
		lum = &lumd->lmd_lmm;

		rc = ll_get_max_mdsize(sbi, &lmmsize);
		if (rc)
			return rc;

		OBD_ALLOC_LARGE(lmm, lmmsize);
		if (lmm == NULL)
			return -ENOMEM;
		if (copy_from_user(lmm, lum, lmmsize))
			GOTO(free_lmm, rc = -EFAULT);

		switch (lmm->lmm_magic) {
		case LOV_USER_MAGIC_V1:
			if (LOV_USER_MAGIC_V1 == cpu_to_le32(LOV_USER_MAGIC_V1))
				break;
			/* swab objects first so that stripes num will be sane */
			lustre_swab_lov_user_md_objects(
				((struct lov_user_md_v1 *)lmm)->lmm_objects,
				((struct lov_user_md_v1 *)lmm)->lmm_stripe_count);
			lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lmm);
			break;
		case LOV_USER_MAGIC_V3:
			if (LOV_USER_MAGIC_V3 == cpu_to_le32(LOV_USER_MAGIC_V3))
				break;
			/* swab objects first so that stripes num will be sane */
			lustre_swab_lov_user_md_objects(
				((struct lov_user_md_v3 *)lmm)->lmm_objects,
				((struct lov_user_md_v3 *)lmm)->lmm_stripe_count);
			lustre_swab_lov_user_md_v3((struct lov_user_md_v3 *)lmm);
			break;
		default:
			GOTO(free_lmm, rc = -EINVAL);
		}

		rc = obd_unpackmd(sbi->ll_dt_exp, &lsm, lmm, lmmsize);
		if (rc < 0)
			GOTO(free_lmm, rc = -ENOMEM);

		/* Perform glimpse_size operation. */
		memset(&st, 0, sizeof(st));

		rc = ll_glimpse_ioctl(sbi, lsm, &st);
		if (rc)
			GOTO(free_lsm, rc);

		if (copy_to_user(&lumd->lmd_st, &st, sizeof(st)))
			GOTO(free_lsm, rc = -EFAULT);

	free_lsm:
		obd_free_memmd(sbi->ll_dt_exp, &lsm);
	free_lmm:
		OBD_FREE_LARGE(lmm, lmmsize);
		return rc;
	}
	case OBD_IOC_LLOG_CATINFO: {
		return -EOPNOTSUPP;
	}
	case OBD_IOC_QUOTACHECK: {
		struct obd_quotactl *oqctl;
		int error = 0;

		if (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
		    sbi->ll_flags & LL_SBI_RMT_CLIENT)
			return -EPERM;

		OBD_ALLOC_PTR(oqctl);
		if (!oqctl)
			return -ENOMEM;
		oqctl->qc_type = arg;
		rc = obd_quotacheck(sbi->ll_md_exp, oqctl);
		if (rc < 0) {
			CDEBUG(D_INFO, "md_quotacheck failed: rc %d\n", rc);
			error = rc;
		}

		rc = obd_quotacheck(sbi->ll_dt_exp, oqctl);
		if (rc < 0)
			CDEBUG(D_INFO, "obd_quotacheck failed: rc %d\n", rc);

		OBD_FREE_PTR(oqctl);
		return error ?: rc;
	}
	case OBD_IOC_POLL_QUOTACHECK: {
		struct if_quotacheck *check;

		if (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
		    sbi->ll_flags & LL_SBI_RMT_CLIENT)
			return -EPERM;

		OBD_ALLOC_PTR(check);
		if (!check)
			return -ENOMEM;

		rc = obd_iocontrol(cmd, sbi->ll_md_exp, 0, (void *)check,
				   NULL);
		if (rc) {
			CDEBUG(D_QUOTA, "mdc ioctl %d failed: %d\n", cmd, rc);
			if (copy_to_user((void *)arg, check,
					     sizeof(*check)))
				CDEBUG(D_QUOTA, "copy_to_user failed\n");
			GOTO(out_poll, rc);
		}

		rc = obd_iocontrol(cmd, sbi->ll_dt_exp, 0, (void *)check,
				   NULL);
		if (rc) {
			CDEBUG(D_QUOTA, "osc ioctl %d failed: %d\n", cmd, rc);
			if (copy_to_user((void *)arg, check,
					     sizeof(*check)))
				CDEBUG(D_QUOTA, "copy_to_user failed\n");
			GOTO(out_poll, rc);
		}
	out_poll:
		OBD_FREE_PTR(check);
		return rc;
	}
#if LUSTRE_VERSION_CODE < OBD_OCD_VERSION(2, 7, 50, 0)
	case LL_IOC_QUOTACTL_18: {
		/* copy the old 1.x quota struct for internal use, then copy
		 * back into old format struct.  For 1.8 compatibility. */
		struct if_quotactl_18 *qctl_18;
		struct if_quotactl *qctl_20;

		OBD_ALLOC_PTR(qctl_18);
		if (!qctl_18)
			return -ENOMEM;

		OBD_ALLOC_PTR(qctl_20);
		if (!qctl_20)
			GOTO(out_quotactl_18, rc = -ENOMEM);

		if (copy_from_user(qctl_18, (void *)arg, sizeof(*qctl_18)))
			GOTO(out_quotactl_20, rc = -ENOMEM);

		QCTL_COPY(qctl_20, qctl_18);
		qctl_20->qc_idx = 0;

		/* XXX: dqb_valid was borrowed as a flag to mark that
		 *      only mds quota is wanted */
		if (qctl_18->qc_cmd == Q_GETQUOTA &&
		    qctl_18->qc_dqblk.dqb_valid) {
			qctl_20->qc_valid = QC_MDTIDX;
			qctl_20->qc_dqblk.dqb_valid = 0;
		} else if (qctl_18->obd_uuid.uuid[0] != '\0') {
			qctl_20->qc_valid = QC_UUID;
			qctl_20->obd_uuid = qctl_18->obd_uuid;
		} else {
			qctl_20->qc_valid = QC_GENERAL;
		}

		rc = quotactl_ioctl(sbi, qctl_20);

		if (rc == 0) {
			QCTL_COPY(qctl_18, qctl_20);
			qctl_18->obd_uuid = qctl_20->obd_uuid;

			if (copy_to_user((void *)arg, qctl_18,
					     sizeof(*qctl_18)))
				rc = -EFAULT;
		}

	out_quotactl_20:
		OBD_FREE_PTR(qctl_20);
	out_quotactl_18:
		OBD_FREE_PTR(qctl_18);
		return rc;
	}
#else
#warning "remove old LL_IOC_QUOTACTL_18 compatibility code"
#endif /* LUSTRE_VERSION_CODE < OBD_OCD_VERSION(2, 7, 50, 0) */
	case LL_IOC_QUOTACTL: {
		struct if_quotactl *qctl;

		OBD_ALLOC_PTR(qctl);
		if (!qctl)
			return -ENOMEM;

		if (copy_from_user(qctl, (void *)arg, sizeof(*qctl)))
			GOTO(out_quotactl, rc = -EFAULT);

		rc = quotactl_ioctl(sbi, qctl);

		if (rc == 0 && copy_to_user((void *)arg,qctl,sizeof(*qctl)))
			rc = -EFAULT;

	out_quotactl:
		OBD_FREE_PTR(qctl);
		return rc;
	}
	case OBD_IOC_GETDTNAME:
	case OBD_IOC_GETMDNAME:
		return ll_get_obd_name(inode, cmd, arg);
	case LL_IOC_FLUSHCTX:
		return ll_flush_ctx(inode);
#ifdef CONFIG_FS_POSIX_ACL
	case LL_IOC_RMTACL: {
	    if (sbi->ll_flags & LL_SBI_RMT_CLIENT &&
		inode == inode->i_sb->s_root->d_inode) {
		struct ll_file_data *fd = LUSTRE_FPRIVATE(file);

		LASSERT(fd != NULL);
		rc = rct_add(&sbi->ll_rct, current_pid(), arg);
		if (!rc)
			fd->fd_flags |= LL_FILE_RMTACL;
		return rc;
	    } else
		return 0;
	}
#endif
	case LL_IOC_GETOBDCOUNT: {
		int count, vallen;
		struct obd_export *exp;

		if (copy_from_user(&count, (int *)arg, sizeof(int)))
			return -EFAULT;

		/* get ost count when count is zero, get mdt count otherwise */
		exp = count ? sbi->ll_md_exp : sbi->ll_dt_exp;
		vallen = sizeof(count);
		rc = obd_get_info(NULL, exp, sizeof(KEY_TGT_COUNT),
				  KEY_TGT_COUNT, &vallen, &count, NULL);
		if (rc) {
			CERROR("get target count failed: %d\n", rc);
			return rc;
		}

		if (copy_to_user((int *)arg, &count, sizeof(int)))
			return -EFAULT;

		return 0;
	}
	case LL_IOC_PATH2FID:
		if (copy_to_user((void *)arg, ll_inode2fid(inode),
				     sizeof(struct lu_fid)))
			return -EFAULT;
		return 0;
	case LL_IOC_GET_CONNECT_FLAGS: {
		return obd_iocontrol(cmd, sbi->ll_md_exp, 0, NULL, (void*)arg);
	}
	case OBD_IOC_CHANGELOG_SEND:
	case OBD_IOC_CHANGELOG_CLEAR:
		rc = copy_and_ioctl(cmd, sbi->ll_md_exp, (void *)arg,
				    sizeof(struct ioc_changelog));
		return rc;
	case OBD_IOC_FID2PATH:
		return ll_fid2path(inode, (void *)arg);
	case LL_IOC_HSM_REQUEST: {
		struct hsm_user_request	*hur;
		int			 totalsize;

		OBD_ALLOC_PTR(hur);
		if (hur == NULL)
			return -ENOMEM;

		/* We don't know the true size yet; copy the fixed-size part */
		if (copy_from_user(hur, (void *)arg, sizeof(*hur))) {
			OBD_FREE_PTR(hur);
			return -EFAULT;
		}

		/* Compute the whole struct size */
		totalsize = hur_len(hur);
		OBD_FREE_PTR(hur);
		OBD_ALLOC_LARGE(hur, totalsize);
		if (hur == NULL)
			return -ENOMEM;

		/* Copy the whole struct */
		if (copy_from_user(hur, (void *)arg, totalsize)) {
			OBD_FREE_LARGE(hur, totalsize);
			return -EFAULT;
		}

		rc = obd_iocontrol(cmd, ll_i2mdexp(inode), totalsize,
				   hur, NULL);

		OBD_FREE_LARGE(hur, totalsize);

		return rc;
	}
	case LL_IOC_HSM_PROGRESS: {
		struct hsm_progress_kernel	hpk;
		struct hsm_progress		hp;

		if (copy_from_user(&hp, (void *)arg, sizeof(hp)))
			return -EFAULT;

		hpk.hpk_fid = hp.hp_fid;
		hpk.hpk_cookie = hp.hp_cookie;
		hpk.hpk_extent = hp.hp_extent;
		hpk.hpk_flags = hp.hp_flags;
		hpk.hpk_errval = hp.hp_errval;
		hpk.hpk_data_version = 0;

		/* File may not exist in Lustre; all progress
		 * reported to Lustre root */
		rc = obd_iocontrol(cmd, sbi->ll_md_exp, sizeof(hpk), &hpk,
				   NULL);
		return rc;
	}
	case LL_IOC_HSM_CT_START:
		rc = copy_and_ioctl(cmd, sbi->ll_md_exp, (void *)arg,
				    sizeof(struct lustre_kernelcomm));
		return rc;

	case LL_IOC_HSM_COPY_START: {
		struct hsm_copy	*copy;
		int		 rc;

		OBD_ALLOC_PTR(copy);
		if (copy == NULL)
			return -ENOMEM;
		if (copy_from_user(copy, (char *)arg, sizeof(*copy))) {
			OBD_FREE_PTR(copy);
			return -EFAULT;
		}

		rc = ll_ioc_copy_start(inode->i_sb, copy);
		if (copy_to_user((char *)arg, copy, sizeof(*copy)))
			rc = -EFAULT;

		OBD_FREE_PTR(copy);
		return rc;
	}
	case LL_IOC_HSM_COPY_END: {
		struct hsm_copy	*copy;
		int		 rc;

		OBD_ALLOC_PTR(copy);
		if (copy == NULL)
			return -ENOMEM;
		if (copy_from_user(copy, (char *)arg, sizeof(*copy))) {
			OBD_FREE_PTR(copy);
			return -EFAULT;
		}

		rc = ll_ioc_copy_end(inode->i_sb, copy);
		if (copy_to_user((char *)arg, copy, sizeof(*copy)))
			rc = -EFAULT;

		OBD_FREE_PTR(copy);
		return rc;
	}
	default:
		return obd_iocontrol(cmd, sbi->ll_dt_exp, 0, NULL, (void *)arg);
	}
}

static loff_t ll_dir_seek(struct file *file, loff_t offset, int origin)
{
	struct inode *inode = file->f_mapping->host;
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	int api32 = ll_need_32bit_api(sbi);
	loff_t ret = -EINVAL;

	mutex_lock(&inode->i_mutex);
	switch (origin) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			offset += file->f_pos;
			break;
		case SEEK_END:
			if (offset > 0)
				GOTO(out, ret);
			if (api32)
				offset += LL_DIR_END_OFF_32BIT;
			else
				offset += LL_DIR_END_OFF;
			break;
		default:
			GOTO(out, ret);
	}

	if (offset >= 0 &&
	    ((api32 && offset <= LL_DIR_END_OFF_32BIT) ||
	     (!api32 && offset <= LL_DIR_END_OFF))) {
		if (offset != file->f_pos) {
			if ((api32 && offset == LL_DIR_END_OFF_32BIT) ||
			    (!api32 && offset == LL_DIR_END_OFF))
				fd->lfd_pos = MDS_DIR_END_OFF;
			else if (api32 && sbi->ll_flags & LL_SBI_64BIT_HASH)
				fd->lfd_pos = offset << 32;
			else
				fd->lfd_pos = offset;
			file->f_pos = offset;
			file->f_version = 0;
		}
		ret = offset;
	}
	GOTO(out, ret);

out:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

int ll_dir_open(struct inode *inode, struct file *file)
{
	return ll_file_open(inode, file);
}

int ll_dir_release(struct inode *inode, struct file *file)
{
	return ll_file_release(inode, file);
}

struct file_operations ll_dir_operations = {
	.llseek   = ll_dir_seek,
	.open     = ll_dir_open,
	.release  = ll_dir_release,
	.read     = generic_read_dir,
	.iterate  = ll_readdir,
	.unlocked_ioctl   = ll_dir_ioctl,
	.fsync    = ll_fsync,
};
