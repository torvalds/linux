// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/unzip_vle.c
 *
 * Copyright (C) 2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "unzip_vle.h"
#include <linux/prefetch.h>

#include <trace/events/erofs.h>

/*
 * a compressed_pages[] placeholder in order to avoid
 * being filled with file pages for in-place decompression.
 */
#define PAGE_UNALLOCATED     ((void *)0x5F0E4B1D)

/* how to allocate cached pages for a workgroup */
enum z_erofs_cache_alloctype {
	DONTALLOC,	/* don't allocate any cached pages */
	DELAYEDALLOC,	/* delayed allocation (at the time of submitting io) */
};

/*
 * tagged pointer with 1-bit tag for all compressed pages
 * tag 0 - the page is just found with an extra page reference
 */
typedef tagptr1_t compressed_page_t;

#define tag_compressed_page_justfound(page) \
	tagptr_fold(compressed_page_t, page, 1)

static struct workqueue_struct *z_erofs_workqueue __read_mostly;
static struct kmem_cache *z_erofs_workgroup_cachep __read_mostly;

void z_erofs_exit_zip_subsystem(void)
{
	destroy_workqueue(z_erofs_workqueue);
	kmem_cache_destroy(z_erofs_workgroup_cachep);
}

static inline int init_unzip_workqueue(void)
{
	const unsigned int onlinecpus = num_possible_cpus();

	/*
	 * we don't need too many threads, limiting threads
	 * could improve scheduling performance.
	 */
	z_erofs_workqueue =
		alloc_workqueue("erofs_unzipd",
				WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE,
				onlinecpus + onlinecpus / 4);

	return z_erofs_workqueue ? 0 : -ENOMEM;
}

static void init_once(void *ptr)
{
	struct z_erofs_vle_workgroup *grp = ptr;
	struct z_erofs_vle_work *const work =
		z_erofs_vle_grab_primary_work(grp);
	unsigned int i;

	mutex_init(&work->lock);
	work->nr_pages = 0;
	work->vcnt = 0;
	for (i = 0; i < Z_EROFS_CLUSTER_MAX_PAGES; ++i)
		grp->compressed_pages[i] = NULL;
}

static void init_always(struct z_erofs_vle_workgroup *grp)
{
	struct z_erofs_vle_work *const work =
		z_erofs_vle_grab_primary_work(grp);

	atomic_set(&grp->obj.refcount, 1);
	grp->flags = 0;

	DBG_BUGON(work->nr_pages);
	DBG_BUGON(work->vcnt);
}

int __init z_erofs_init_zip_subsystem(void)
{
	z_erofs_workgroup_cachep =
		kmem_cache_create("erofs_compress",
				  Z_EROFS_WORKGROUP_SIZE, 0,
				  SLAB_RECLAIM_ACCOUNT, init_once);

	if (z_erofs_workgroup_cachep) {
		if (!init_unzip_workqueue())
			return 0;

		kmem_cache_destroy(z_erofs_workgroup_cachep);
	}
	return -ENOMEM;
}

enum z_erofs_vle_work_role {
	Z_EROFS_VLE_WORK_SECONDARY,
	Z_EROFS_VLE_WORK_PRIMARY,
	/*
	 * The current work was the tail of an exist chain, and the previous
	 * processed chained works are all decided to be hooked up to it.
	 * A new chain should be created for the remaining unprocessed works,
	 * therefore different from Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED,
	 * the next work cannot reuse the whole page in the following scenario:
	 *  ________________________________________________________________
	 * |      tail (partial) page     |       head (partial) page       |
	 * |  (belongs to the next work)  |  (belongs to the current work)  |
	 * |_______PRIMARY_FOLLOWED_______|________PRIMARY_HOOKED___________|
	 */
	Z_EROFS_VLE_WORK_PRIMARY_HOOKED,
	/*
	 * The current work has been linked with the processed chained works,
	 * and could be also linked with the potential remaining works, which
	 * means if the processing page is the tail partial page of the work,
	 * the current work can safely use the whole page (since the next work
	 * is under control) for in-place decompression, as illustrated below:
	 *  ________________________________________________________________
	 * |  tail (partial) page  |          head (partial) page           |
	 * | (of the current work) |         (of the previous work)         |
	 * |  PRIMARY_FOLLOWED or  |                                        |
	 * |_____PRIMARY_HOOKED____|____________PRIMARY_FOLLOWED____________|
	 *
	 * [  (*) the above page can be used for the current work itself.  ]
	 */
	Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED,
	Z_EROFS_VLE_WORK_MAX
};

struct z_erofs_vle_work_builder {
	enum z_erofs_vle_work_role role;
	/*
	 * 'hosted = false' means that the current workgroup doesn't belong to
	 * the owned chained workgroups. In the other words, it is none of our
	 * business to submit this workgroup.
	 */
	bool hosted;

	struct z_erofs_vle_workgroup *grp;
	struct z_erofs_vle_work *work;
	struct z_erofs_pagevec_ctor vector;

	/* pages used for reading the compressed data */
	struct page **compressed_pages;
	unsigned int compressed_deficit;
};

#define VLE_WORK_BUILDER_INIT()	\
	{ .work = NULL, .role = Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED }

#ifdef EROFS_FS_HAS_MANAGED_CACHE
static void preload_compressed_pages(struct z_erofs_vle_work_builder *bl,
				     struct address_space *mc,
				     pgoff_t index,
				     unsigned int clusterpages,
				     enum z_erofs_cache_alloctype type,
				     struct list_head *pagepool,
				     gfp_t gfp)
{
	struct page **const pages = bl->compressed_pages;
	const unsigned int remaining = bl->compressed_deficit;
	bool standalone = true;
	unsigned int i, j = 0;

	if (bl->role < Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED)
		return;

	gfp = mapping_gfp_constraint(mc, gfp) & ~__GFP_RECLAIM;

	index += clusterpages - remaining;

	for (i = 0; i < remaining; ++i) {
		struct page *page;
		compressed_page_t t;

		/* the compressed page was loaded before */
		if (READ_ONCE(pages[i]))
			continue;

		page = find_get_page(mc, index + i);

		if (page) {
			t = tag_compressed_page_justfound(page);
		} else if (type == DELAYEDALLOC) {
			t = tagptr_init(compressed_page_t, PAGE_UNALLOCATED);
		} else {	/* DONTALLOC */
			if (standalone)
				j = i;
			standalone = false;
			continue;
		}

		if (!cmpxchg_relaxed(&pages[i], NULL, tagptr_cast_ptr(t)))
			continue;

		if (page)
			put_page(page);
	}
	bl->compressed_pages += j;
	bl->compressed_deficit = remaining - j;

	if (standalone)
		bl->role = Z_EROFS_VLE_WORK_PRIMARY;
}

/* called by erofs_shrinker to get rid of all compressed_pages */
int erofs_try_to_free_all_cached_pages(struct erofs_sb_info *sbi,
				       struct erofs_workgroup *egrp)
{
	struct z_erofs_vle_workgroup *const grp =
		container_of(egrp, struct z_erofs_vle_workgroup, obj);
	struct address_space *const mapping = MNGD_MAPPING(sbi);
	const int clusterpages = erofs_clusterpages(sbi);
	int i;

	/*
	 * refcount of workgroup is now freezed as 1,
	 * therefore no need to worry about available decompression users.
	 */
	for (i = 0; i < clusterpages; ++i) {
		struct page *page = grp->compressed_pages[i];

		if (!page || page->mapping != mapping)
			continue;

		/* block other users from reclaiming or migrating the page */
		if (!trylock_page(page))
			return -EBUSY;

		/* barrier is implied in the following 'unlock_page' */
		WRITE_ONCE(grp->compressed_pages[i], NULL);

		set_page_private(page, 0);
		ClearPagePrivate(page);

		unlock_page(page);
		put_page(page);
	}
	return 0;
}

int erofs_try_to_free_cached_page(struct address_space *mapping,
				  struct page *page)
{
	struct erofs_sb_info *const sbi = EROFS_SB(mapping->host->i_sb);
	const unsigned int clusterpages = erofs_clusterpages(sbi);
	struct z_erofs_vle_workgroup *const grp = (void *)page_private(page);
	int ret = 0;	/* 0 - busy */

	if (erofs_workgroup_try_to_freeze(&grp->obj, 1)) {
		unsigned int i;

		for (i = 0; i < clusterpages; ++i) {
			if (grp->compressed_pages[i] == page) {
				WRITE_ONCE(grp->compressed_pages[i], NULL);
				ret = 1;
				break;
			}
		}
		erofs_workgroup_unfreeze(&grp->obj, 1);

		if (ret) {
			ClearPagePrivate(page);
			put_page(page);
		}
	}
	return ret;
}
#else
static void preload_compressed_pages(struct z_erofs_vle_work_builder *bl,
				     struct address_space *mc,
				     pgoff_t index,
				     unsigned int clusterpages,
				     enum z_erofs_cache_alloctype type,
				     struct list_head *pagepool,
				     gfp_t gfp)
{
	/* nowhere to load compressed pages from */
}
#endif

/* page_type must be Z_EROFS_PAGE_TYPE_EXCLUSIVE */
static inline bool try_to_reuse_as_compressed_page(
	struct z_erofs_vle_work_builder *b,
	struct page *page)
{
	while (b->compressed_deficit) {
		--b->compressed_deficit;
		if (!cmpxchg(b->compressed_pages++, NULL, page))
			return true;
	}

	return false;
}

/* callers must be with work->lock held */
static int z_erofs_vle_work_add_page(
	struct z_erofs_vle_work_builder *builder,
	struct page *page,
	enum z_erofs_page_type type)
{
	int ret;
	bool occupied;

	/* give priority for the compressed data storage */
	if (builder->role >= Z_EROFS_VLE_WORK_PRIMARY &&
	    type == Z_EROFS_PAGE_TYPE_EXCLUSIVE &&
	    try_to_reuse_as_compressed_page(builder, page))
		return 0;

	ret = z_erofs_pagevec_ctor_enqueue(&builder->vector,
					   page, type, &occupied);
	builder->work->vcnt += (unsigned int)ret;

	return ret ? 0 : -EAGAIN;
}

static enum z_erofs_vle_work_role
try_to_claim_workgroup(struct z_erofs_vle_workgroup *grp,
		       z_erofs_vle_owned_workgrp_t *owned_head,
		       bool *hosted)
{
	DBG_BUGON(*hosted);

	/* let's claim these following types of workgroup */
retry:
	if (grp->next == Z_EROFS_VLE_WORKGRP_NIL) {
		/* type 1, nil workgroup */
		if (cmpxchg(&grp->next, Z_EROFS_VLE_WORKGRP_NIL,
			    *owned_head) != Z_EROFS_VLE_WORKGRP_NIL)
			goto retry;

		*owned_head = &grp->next;
		*hosted = true;
		/* lucky, I am the followee :) */
		return Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED;

	} else if (grp->next == Z_EROFS_VLE_WORKGRP_TAIL) {
		/*
		 * type 2, link to the end of a existing open chain,
		 * be careful that its submission itself is governed
		 * by the original owned chain.
		 */
		if (cmpxchg(&grp->next, Z_EROFS_VLE_WORKGRP_TAIL,
			    *owned_head) != Z_EROFS_VLE_WORKGRP_TAIL)
			goto retry;
		*owned_head = Z_EROFS_VLE_WORKGRP_TAIL;
		return Z_EROFS_VLE_WORK_PRIMARY_HOOKED;
	}

	return Z_EROFS_VLE_WORK_PRIMARY; /* :( better luck next time */
}

struct z_erofs_vle_work_finder {
	struct super_block *sb;
	pgoff_t idx;
	unsigned int pageofs;

	struct z_erofs_vle_workgroup **grp_ret;
	enum z_erofs_vle_work_role *role;
	z_erofs_vle_owned_workgrp_t *owned_head;
	bool *hosted;
};

static struct z_erofs_vle_work *
z_erofs_vle_work_lookup(const struct z_erofs_vle_work_finder *f)
{
	bool tag, primary;
	struct erofs_workgroup *egrp;
	struct z_erofs_vle_workgroup *grp;
	struct z_erofs_vle_work *work;

	egrp = erofs_find_workgroup(f->sb, f->idx, &tag);
	if (!egrp) {
		*f->grp_ret = NULL;
		return NULL;
	}

	grp = container_of(egrp, struct z_erofs_vle_workgroup, obj);
	*f->grp_ret = grp;

	work = z_erofs_vle_grab_work(grp, f->pageofs);
	/* if multiref is disabled, `primary' is always true */
	primary = true;

	DBG_BUGON(work->pageofs != f->pageofs);

	/*
	 * lock must be taken first to avoid grp->next == NIL between
	 * claiming workgroup and adding pages:
	 *                        grp->next != NIL
	 *   grp->next = NIL
	 *   mutex_unlock_all
	 *                        mutex_lock(&work->lock)
	 *                        add all pages to pagevec
	 *
	 * [correct locking case 1]:
	 *   mutex_lock(grp->work[a])
	 *   ...
	 *   mutex_lock(grp->work[b])     mutex_lock(grp->work[c])
	 *   ...                          *role = SECONDARY
	 *                                add all pages to pagevec
	 *                                ...
	 *                                mutex_unlock(grp->work[c])
	 *   mutex_lock(grp->work[c])
	 *   ...
	 *   grp->next = NIL
	 *   mutex_unlock_all
	 *
	 * [correct locking case 2]:
	 *   mutex_lock(grp->work[b])
	 *   ...
	 *   mutex_lock(grp->work[a])
	 *   ...
	 *   mutex_lock(grp->work[c])
	 *   ...
	 *   grp->next = NIL
	 *   mutex_unlock_all
	 *                                mutex_lock(grp->work[a])
	 *                                *role = PRIMARY_OWNER
	 *                                add all pages to pagevec
	 *                                ...
	 */
	mutex_lock(&work->lock);

	*f->hosted = false;
	if (!primary)
		*f->role = Z_EROFS_VLE_WORK_SECONDARY;
	else	/* claim the workgroup if possible */
		*f->role = try_to_claim_workgroup(grp, f->owned_head,
						  f->hosted);
	return work;
}

static struct z_erofs_vle_work *
z_erofs_vle_work_register(const struct z_erofs_vle_work_finder *f,
			  struct erofs_map_blocks *map)
{
	bool gnew = false;
	struct z_erofs_vle_workgroup *grp = *f->grp_ret;
	struct z_erofs_vle_work *work;

	/* if multiref is disabled, grp should never be nullptr */
	if (unlikely(grp)) {
		DBG_BUGON(1);
		return ERR_PTR(-EINVAL);
	}

	/* no available workgroup, let's allocate one */
	grp = kmem_cache_alloc(z_erofs_workgroup_cachep, GFP_NOFS);
	if (unlikely(!grp))
		return ERR_PTR(-ENOMEM);

	init_always(grp);
	grp->obj.index = f->idx;
	grp->llen = map->m_llen;

	z_erofs_vle_set_workgrp_fmt(grp, (map->m_flags & EROFS_MAP_ZIPPED) ?
				    Z_EROFS_VLE_WORKGRP_FMT_LZ4 :
				    Z_EROFS_VLE_WORKGRP_FMT_PLAIN);

	/* new workgrps have been claimed as type 1 */
	WRITE_ONCE(grp->next, *f->owned_head);
	/* primary and followed work for all new workgrps */
	*f->role = Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED;
	/* it should be submitted by ourselves */
	*f->hosted = true;

	gnew = true;
	work = z_erofs_vle_grab_primary_work(grp);
	work->pageofs = f->pageofs;

	/*
	 * lock all primary followed works before visible to others
	 * and mutex_trylock *never* fails for a new workgroup.
	 */
	mutex_trylock(&work->lock);

	if (gnew) {
		int err = erofs_register_workgroup(f->sb, &grp->obj, 0);

		if (err) {
			mutex_unlock(&work->lock);
			kmem_cache_free(z_erofs_workgroup_cachep, grp);
			return ERR_PTR(-EAGAIN);
		}
	}

	*f->owned_head = &grp->next;
	*f->grp_ret = grp;
	return work;
}

#define builder_is_hooked(builder) \
	((builder)->role >= Z_EROFS_VLE_WORK_PRIMARY_HOOKED)

#define builder_is_followed(builder) \
	((builder)->role >= Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED)

static int z_erofs_vle_work_iter_begin(struct z_erofs_vle_work_builder *builder,
				       struct super_block *sb,
				       struct erofs_map_blocks *map,
				       z_erofs_vle_owned_workgrp_t *owned_head)
{
	const unsigned int clusterpages = erofs_clusterpages(EROFS_SB(sb));
	struct z_erofs_vle_workgroup *grp;
	const struct z_erofs_vle_work_finder finder = {
		.sb = sb,
		.idx = erofs_blknr(map->m_pa),
		.pageofs = map->m_la & ~PAGE_MASK,
		.grp_ret = &grp,
		.role = &builder->role,
		.owned_head = owned_head,
		.hosted = &builder->hosted
	};
	struct z_erofs_vle_work *work;

	DBG_BUGON(builder->work);

	/* must be Z_EROFS_WORK_TAIL or the next chained work */
	DBG_BUGON(*owned_head == Z_EROFS_VLE_WORKGRP_NIL);
	DBG_BUGON(*owned_head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);

	DBG_BUGON(erofs_blkoff(map->m_pa));

repeat:
	work = z_erofs_vle_work_lookup(&finder);
	if (work) {
		unsigned int orig_llen;

		/* increase workgroup `llen' if needed */
		while ((orig_llen = READ_ONCE(grp->llen)) < map->m_llen &&
		       orig_llen != cmpxchg_relaxed(&grp->llen,
						    orig_llen, map->m_llen))
			cpu_relax();
		goto got_it;
	}

	work = z_erofs_vle_work_register(&finder, map);
	if (unlikely(work == ERR_PTR(-EAGAIN)))
		goto repeat;

	if (IS_ERR(work))
		return PTR_ERR(work);
got_it:
	z_erofs_pagevec_ctor_init(&builder->vector, Z_EROFS_NR_INLINE_PAGEVECS,
				  work->pagevec, work->vcnt);

	if (builder->role >= Z_EROFS_VLE_WORK_PRIMARY) {
		/* enable possibly in-place decompression */
		builder->compressed_pages = grp->compressed_pages;
		builder->compressed_deficit = clusterpages;
	} else {
		builder->compressed_pages = NULL;
		builder->compressed_deficit = 0;
	}

	builder->grp = grp;
	builder->work = work;
	return 0;
}

/*
 * keep in mind that no referenced workgroups will be freed
 * only after a RCU grace period, so rcu_read_lock() could
 * prevent a workgroup from being freed.
 */
static void z_erofs_rcu_callback(struct rcu_head *head)
{
	struct z_erofs_vle_work *work =	container_of(head,
		struct z_erofs_vle_work, rcu);
	struct z_erofs_vle_workgroup *grp =
		z_erofs_vle_work_workgroup(work, true);

	kmem_cache_free(z_erofs_workgroup_cachep, grp);
}

void erofs_workgroup_free_rcu(struct erofs_workgroup *grp)
{
	struct z_erofs_vle_workgroup *const vgrp = container_of(grp,
		struct z_erofs_vle_workgroup, obj);
	struct z_erofs_vle_work *const work = &vgrp->work;

	call_rcu(&work->rcu, z_erofs_rcu_callback);
}

static void
__z_erofs_vle_work_release(struct z_erofs_vle_workgroup *grp,
			   struct z_erofs_vle_work *work __maybe_unused)
{
	erofs_workgroup_put(&grp->obj);
}

static void z_erofs_vle_work_release(struct z_erofs_vle_work *work)
{
	struct z_erofs_vle_workgroup *grp =
		z_erofs_vle_work_workgroup(work, true);

	__z_erofs_vle_work_release(grp, work);
}

static inline bool
z_erofs_vle_work_iter_end(struct z_erofs_vle_work_builder *builder)
{
	struct z_erofs_vle_work *work = builder->work;

	if (!work)
		return false;

	z_erofs_pagevec_ctor_exit(&builder->vector, false);
	mutex_unlock(&work->lock);

	/*
	 * if all pending pages are added, don't hold work reference
	 * any longer if the current work isn't hosted by ourselves.
	 */
	if (!builder->hosted)
		__z_erofs_vle_work_release(builder->grp, work);

	builder->work = NULL;
	builder->grp = NULL;
	return true;
}

static inline struct page *__stagingpage_alloc(struct list_head *pagepool,
					       gfp_t gfp)
{
	struct page *page = erofs_allocpage(pagepool, gfp);

	if (unlikely(!page))
		return NULL;

	page->mapping = Z_EROFS_MAPPING_STAGING;
	return page;
}

struct z_erofs_vle_frontend {
	struct inode *const inode;

	struct z_erofs_vle_work_builder builder;
	struct erofs_map_blocks map;

	z_erofs_vle_owned_workgrp_t owned_head;

	/* used for applying cache strategy on the fly */
	bool backmost;
	erofs_off_t headoffset;
};

#define VLE_FRONTEND_INIT(__i) { \
	.inode = __i, \
	.map = { \
		.m_llen = 0, \
		.m_plen = 0, \
		.mpage = NULL \
	}, \
	.builder = VLE_WORK_BUILDER_INIT(), \
	.owned_head = Z_EROFS_VLE_WORKGRP_TAIL, \
	.backmost = true, }

#ifdef EROFS_FS_HAS_MANAGED_CACHE
static inline bool
should_alloc_managed_pages(struct z_erofs_vle_frontend *fe, erofs_off_t la)
{
	if (fe->backmost)
		return true;

	if (EROFS_FS_ZIP_CACHE_LVL >= 2)
		return la < fe->headoffset;

	return false;
}
#else
static inline bool
should_alloc_managed_pages(struct z_erofs_vle_frontend *fe, erofs_off_t la)
{
	return false;
}
#endif

static int z_erofs_do_read_page(struct z_erofs_vle_frontend *fe,
				struct page *page,
				struct list_head *page_pool)
{
	struct super_block *const sb = fe->inode->i_sb;
	struct erofs_sb_info *const sbi __maybe_unused = EROFS_SB(sb);
	struct erofs_map_blocks *const map = &fe->map;
	struct z_erofs_vle_work_builder *const builder = &fe->builder;
	const loff_t offset = page_offset(page);

	bool tight = builder_is_hooked(builder);
	struct z_erofs_vle_work *work = builder->work;

	enum z_erofs_cache_alloctype cache_strategy;
	enum z_erofs_page_type page_type;
	unsigned int cur, end, spiltted, index;
	int err = 0;

	/* register locked file pages as online pages in pack */
	z_erofs_onlinepage_init(page);

	spiltted = 0;
	end = PAGE_SIZE;
repeat:
	cur = end - 1;

	/* lucky, within the range of the current map_blocks */
	if (offset + cur >= map->m_la &&
	    offset + cur < map->m_la + map->m_llen) {
		/* didn't get a valid unzip work previously (very rare) */
		if (!builder->work)
			goto restart_now;
		goto hitted;
	}

	/* go ahead the next map_blocks */
	debugln("%s: [out-of-range] pos %llu", __func__, offset + cur);

	if (z_erofs_vle_work_iter_end(builder))
		fe->backmost = false;

	map->m_la = offset + cur;
	map->m_llen = 0;
	err = z_erofs_map_blocks_iter(fe->inode, map, 0);
	if (unlikely(err))
		goto err_out;

restart_now:
	if (unlikely(!(map->m_flags & EROFS_MAP_MAPPED)))
		goto hitted;

	DBG_BUGON(map->m_plen != 1 << sbi->clusterbits);
	DBG_BUGON(erofs_blkoff(map->m_pa));

	err = z_erofs_vle_work_iter_begin(builder, sb, map, &fe->owned_head);
	if (unlikely(err))
		goto err_out;

	/* preload all compressed pages (maybe downgrade role if necessary) */
	if (should_alloc_managed_pages(fe, map->m_la))
		cache_strategy = DELAYEDALLOC;
	else
		cache_strategy = DONTALLOC;

	preload_compressed_pages(builder, MNGD_MAPPING(sbi),
				 map->m_pa / PAGE_SIZE,
				 map->m_plen / PAGE_SIZE,
				 cache_strategy, page_pool, GFP_KERNEL);

	tight &= builder_is_hooked(builder);
	work = builder->work;
hitted:
	cur = end - min_t(unsigned int, offset + end - map->m_la, end);
	if (unlikely(!(map->m_flags & EROFS_MAP_MAPPED))) {
		zero_user_segment(page, cur, end);
		goto next_part;
	}

	/* let's derive page type */
	page_type = cur ? Z_EROFS_VLE_PAGE_TYPE_HEAD :
		(!spiltted ? Z_EROFS_PAGE_TYPE_EXCLUSIVE :
			(tight ? Z_EROFS_PAGE_TYPE_EXCLUSIVE :
				Z_EROFS_VLE_PAGE_TYPE_TAIL_SHARED));

	if (cur)
		tight &= builder_is_followed(builder);

retry:
	err = z_erofs_vle_work_add_page(builder, page, page_type);
	/* should allocate an additional staging page for pagevec */
	if (err == -EAGAIN) {
		struct page *const newpage =
			__stagingpage_alloc(page_pool, GFP_NOFS);

		err = z_erofs_vle_work_add_page(builder, newpage,
						Z_EROFS_PAGE_TYPE_EXCLUSIVE);
		if (likely(!err))
			goto retry;
	}

	if (unlikely(err))
		goto err_out;

	index = page->index - map->m_la / PAGE_SIZE;

	/* FIXME! avoid the last relundant fixup & endio */
	z_erofs_onlinepage_fixup(page, index, true);

	/* bump up the number of spiltted parts of a page */
	++spiltted;
	/* also update nr_pages */
	work->nr_pages = max_t(pgoff_t, work->nr_pages, index + 1);
next_part:
	/* can be used for verification */
	map->m_llen = offset + cur - map->m_la;

	end = cur;
	if (end > 0)
		goto repeat;

out:
	/* FIXME! avoid the last relundant fixup & endio */
	z_erofs_onlinepage_endio(page);

	debugln("%s, finish page: %pK spiltted: %u map->m_llen %llu",
		__func__, page, spiltted, map->m_llen);
	return err;

	/* if some error occurred while processing this page */
err_out:
	SetPageError(page);
	goto out;
}

static void z_erofs_vle_unzip_kickoff(void *ptr, int bios)
{
	tagptr1_t t = tagptr_init(tagptr1_t, ptr);
	struct z_erofs_vle_unzip_io *io = tagptr_unfold_ptr(t);
	bool background = tagptr_unfold_tags(t);

	if (!background) {
		unsigned long flags;

		spin_lock_irqsave(&io->u.wait.lock, flags);
		if (!atomic_add_return(bios, &io->pending_bios))
			wake_up_locked(&io->u.wait);
		spin_unlock_irqrestore(&io->u.wait.lock, flags);
		return;
	}

	if (!atomic_add_return(bios, &io->pending_bios))
		queue_work(z_erofs_workqueue, &io->u.work);
}

static inline void z_erofs_vle_read_endio(struct bio *bio)
{
	struct erofs_sb_info *sbi = NULL;
	blk_status_t err = bio->bi_status;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;
		bool cachemngd = false;

		DBG_BUGON(PageUptodate(page));
		DBG_BUGON(!page->mapping);

		if (unlikely(!sbi && !z_erofs_is_stagingpage(page))) {
			sbi = EROFS_SB(page->mapping->host->i_sb);

			if (time_to_inject(sbi, FAULT_READ_IO)) {
				erofs_show_injection_info(FAULT_READ_IO);
				err = BLK_STS_IOERR;
			}
		}

		/* sbi should already be gotten if the page is managed */
		if (sbi)
			cachemngd = erofs_page_is_managed(sbi, page);

		if (unlikely(err))
			SetPageError(page);
		else if (cachemngd)
			SetPageUptodate(page);

		if (cachemngd)
			unlock_page(page);
	}

	z_erofs_vle_unzip_kickoff(bio->bi_private, -1);
	bio_put(bio);
}

static struct page *z_pagemap_global[Z_EROFS_VLE_VMAP_GLOBAL_PAGES];
static DEFINE_MUTEX(z_pagemap_global_lock);

static int z_erofs_vle_unzip(struct super_block *sb,
			     struct z_erofs_vle_workgroup *grp,
			     struct list_head *page_pool)
{
	struct erofs_sb_info *const sbi = EROFS_SB(sb);
	const unsigned int clusterpages = erofs_clusterpages(sbi);

	struct z_erofs_pagevec_ctor ctor;
	unsigned int nr_pages;
	unsigned int sparsemem_pages = 0;
	struct page *pages_onstack[Z_EROFS_VLE_VMAP_ONSTACK_PAGES];
	struct page **pages, **compressed_pages, *page;
	unsigned int i, llen;

	enum z_erofs_page_type page_type;
	bool overlapped;
	struct z_erofs_vle_work *work;
	void *vout;
	int err;

	might_sleep();
	work = z_erofs_vle_grab_primary_work(grp);
	DBG_BUGON(!READ_ONCE(work->nr_pages));

	mutex_lock(&work->lock);
	nr_pages = work->nr_pages;

	if (likely(nr_pages <= Z_EROFS_VLE_VMAP_ONSTACK_PAGES))
		pages = pages_onstack;
	else if (nr_pages <= Z_EROFS_VLE_VMAP_GLOBAL_PAGES &&
		 mutex_trylock(&z_pagemap_global_lock))
		pages = z_pagemap_global;
	else {
repeat:
		pages = kvmalloc_array(nr_pages, sizeof(struct page *),
				       GFP_KERNEL);

		/* fallback to global pagemap for the lowmem scenario */
		if (unlikely(!pages)) {
			if (nr_pages > Z_EROFS_VLE_VMAP_GLOBAL_PAGES)
				goto repeat;
			else {
				mutex_lock(&z_pagemap_global_lock);
				pages = z_pagemap_global;
			}
		}
	}

	for (i = 0; i < nr_pages; ++i)
		pages[i] = NULL;

	z_erofs_pagevec_ctor_init(&ctor, Z_EROFS_NR_INLINE_PAGEVECS,
				  work->pagevec, 0);

	for (i = 0; i < work->vcnt; ++i) {
		unsigned int pagenr;

		page = z_erofs_pagevec_ctor_dequeue(&ctor, &page_type);

		/* all pages in pagevec ought to be valid */
		DBG_BUGON(!page);
		DBG_BUGON(!page->mapping);

		if (z_erofs_gather_if_stagingpage(page_pool, page))
			continue;

		if (page_type == Z_EROFS_VLE_PAGE_TYPE_HEAD)
			pagenr = 0;
		else
			pagenr = z_erofs_onlinepage_index(page);

		DBG_BUGON(pagenr >= nr_pages);
		DBG_BUGON(pages[pagenr]);

		pages[pagenr] = page;
	}
	sparsemem_pages = i;

	z_erofs_pagevec_ctor_exit(&ctor, true);

	overlapped = false;
	compressed_pages = grp->compressed_pages;

	err = 0;
	for (i = 0; i < clusterpages; ++i) {
		unsigned int pagenr;

		page = compressed_pages[i];

		/* all compressed pages ought to be valid */
		DBG_BUGON(!page);
		DBG_BUGON(!page->mapping);

		if (!z_erofs_is_stagingpage(page)) {
			if (erofs_page_is_managed(sbi, page)) {
				if (unlikely(!PageUptodate(page)))
					err = -EIO;
				continue;
			}

			/*
			 * only if non-head page can be selected
			 * for inplace decompression
			 */
			pagenr = z_erofs_onlinepage_index(page);

			DBG_BUGON(pagenr >= nr_pages);
			DBG_BUGON(pages[pagenr]);
			++sparsemem_pages;
			pages[pagenr] = page;

			overlapped = true;
		}

		/* PG_error needs checking for inplaced and staging pages */
		if (unlikely(PageError(page))) {
			DBG_BUGON(PageUptodate(page));
			err = -EIO;
		}
	}

	if (unlikely(err))
		goto out;

	llen = (nr_pages << PAGE_SHIFT) - work->pageofs;

	if (z_erofs_vle_workgrp_fmt(grp) == Z_EROFS_VLE_WORKGRP_FMT_PLAIN) {
		err = z_erofs_vle_plain_copy(compressed_pages, clusterpages,
					     pages, nr_pages, work->pageofs);
		goto out;
	}

	if (llen > grp->llen)
		llen = grp->llen;

	err = z_erofs_vle_unzip_fast_percpu(compressed_pages, clusterpages,
					    pages, llen, work->pageofs);
	if (err != -ENOTSUPP)
		goto out;

	if (sparsemem_pages >= nr_pages)
		goto skip_allocpage;

	for (i = 0; i < nr_pages; ++i) {
		if (pages[i])
			continue;

		pages[i] = __stagingpage_alloc(page_pool, GFP_NOFS);
	}

skip_allocpage:
	vout = erofs_vmap(pages, nr_pages);
	if (!vout) {
		err = -ENOMEM;
		goto out;
	}

	err = z_erofs_vle_unzip_vmap(compressed_pages, clusterpages, vout,
				     llen, work->pageofs, overlapped);

	erofs_vunmap(vout, nr_pages);

out:
	/* must handle all compressed pages before endding pages */
	for (i = 0; i < clusterpages; ++i) {
		page = compressed_pages[i];

		if (erofs_page_is_managed(sbi, page))
			continue;

		/* recycle all individual staging pages */
		(void)z_erofs_gather_if_stagingpage(page_pool, page);

		WRITE_ONCE(compressed_pages[i], NULL);
	}

	for (i = 0; i < nr_pages; ++i) {
		page = pages[i];
		if (!page)
			continue;

		DBG_BUGON(!page->mapping);

		/* recycle all individual staging pages */
		if (z_erofs_gather_if_stagingpage(page_pool, page))
			continue;

		if (unlikely(err < 0))
			SetPageError(page);

		z_erofs_onlinepage_endio(page);
	}

	if (pages == z_pagemap_global)
		mutex_unlock(&z_pagemap_global_lock);
	else if (unlikely(pages != pages_onstack))
		kvfree(pages);

	work->nr_pages = 0;
	work->vcnt = 0;

	/* all work locks MUST be taken before the following line */

	WRITE_ONCE(grp->next, Z_EROFS_VLE_WORKGRP_NIL);

	/* all work locks SHOULD be released right now */
	mutex_unlock(&work->lock);

	z_erofs_vle_work_release(work);
	return err;
}

static void z_erofs_vle_unzip_all(struct super_block *sb,
				  struct z_erofs_vle_unzip_io *io,
				  struct list_head *page_pool)
{
	z_erofs_vle_owned_workgrp_t owned = io->head;

	while (owned != Z_EROFS_VLE_WORKGRP_TAIL_CLOSED) {
		struct z_erofs_vle_workgroup *grp;

		/* no possible that 'owned' equals Z_EROFS_WORK_TPTR_TAIL */
		DBG_BUGON(owned == Z_EROFS_VLE_WORKGRP_TAIL);

		/* no possible that 'owned' equals NULL */
		DBG_BUGON(owned == Z_EROFS_VLE_WORKGRP_NIL);

		grp = container_of(owned, struct z_erofs_vle_workgroup, next);
		owned = READ_ONCE(grp->next);

		z_erofs_vle_unzip(sb, grp, page_pool);
	}
}

static void z_erofs_vle_unzip_wq(struct work_struct *work)
{
	struct z_erofs_vle_unzip_io_sb *iosb = container_of(work,
		struct z_erofs_vle_unzip_io_sb, io.u.work);
	LIST_HEAD(page_pool);

	DBG_BUGON(iosb->io.head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);
	z_erofs_vle_unzip_all(iosb->sb, &iosb->io, &page_pool);

	put_pages_list(&page_pool);
	kvfree(iosb);
}

static struct page *
pickup_page_for_submission(struct z_erofs_vle_workgroup *grp,
			   unsigned int nr,
			   struct list_head *pagepool,
			   struct address_space *mc,
			   gfp_t gfp)
{
	/* determined at compile time to avoid too many #ifdefs */
	const bool nocache = __builtin_constant_p(mc) ? !mc : false;
	const pgoff_t index = grp->obj.index;
	bool tocache = false;

	struct address_space *mapping;
	struct page *oldpage, *page;

	compressed_page_t t;
	int justfound;

repeat:
	page = READ_ONCE(grp->compressed_pages[nr]);
	oldpage = page;

	if (!page)
		goto out_allocpage;

	/*
	 * the cached page has not been allocated and
	 * an placeholder is out there, prepare it now.
	 */
	if (!nocache && page == PAGE_UNALLOCATED) {
		tocache = true;
		goto out_allocpage;
	}

	/* process the target tagged pointer */
	t = tagptr_init(compressed_page_t, page);
	justfound = tagptr_unfold_tags(t);
	page = tagptr_unfold_ptr(t);

	mapping = READ_ONCE(page->mapping);

	/*
	 * if managed cache is disabled, it's no way to
	 * get such a cached-like page.
	 */
	if (nocache) {
		/* if managed cache is disabled, it is impossible `justfound' */
		DBG_BUGON(justfound);

		/* and it should be locked, not uptodate, and not truncated */
		DBG_BUGON(!PageLocked(page));
		DBG_BUGON(PageUptodate(page));
		DBG_BUGON(!mapping);
		goto out;
	}

	/*
	 * unmanaged (file) pages are all locked solidly,
	 * therefore it is impossible for `mapping' to be NULL.
	 */
	if (mapping && mapping != mc)
		/* ought to be unmanaged pages */
		goto out;

	lock_page(page);

	/* only true if page reclaim goes wrong, should never happen */
	DBG_BUGON(justfound && PagePrivate(page));

	/* the page is still in manage cache */
	if (page->mapping == mc) {
		WRITE_ONCE(grp->compressed_pages[nr], page);

		ClearPageError(page);
		if (!PagePrivate(page)) {
			/*
			 * impossible to be !PagePrivate(page) for
			 * the current restriction as well if
			 * the page is already in compressed_pages[].
			 */
			DBG_BUGON(!justfound);

			justfound = 0;
			set_page_private(page, (unsigned long)grp);
			SetPagePrivate(page);
		}

		/* no need to submit io if it is already up-to-date */
		if (PageUptodate(page)) {
			unlock_page(page);
			page = NULL;
		}
		goto out;
	}

	/*
	 * the managed page has been truncated, it's unsafe to
	 * reuse this one, let's allocate a new cache-managed page.
	 */
	DBG_BUGON(page->mapping);
	DBG_BUGON(!justfound);

	tocache = true;
	unlock_page(page);
	put_page(page);
out_allocpage:
	page = __stagingpage_alloc(pagepool, gfp);
	if (oldpage != cmpxchg(&grp->compressed_pages[nr], oldpage, page)) {
		list_add(&page->lru, pagepool);
		cpu_relax();
		goto repeat;
	}
	if (nocache || !tocache)
		goto out;
	if (add_to_page_cache_lru(page, mc, index + nr, gfp)) {
		page->mapping = Z_EROFS_MAPPING_STAGING;
		goto out;
	}

	set_page_private(page, (unsigned long)grp);
	SetPagePrivate(page);
out:	/* the only exit (for tracing and debugging) */
	return page;
}

static struct z_erofs_vle_unzip_io *
jobqueue_init(struct super_block *sb,
	      struct z_erofs_vle_unzip_io *io,
	      bool foreground)
{
	struct z_erofs_vle_unzip_io_sb *iosb;

	if (foreground) {
		/* waitqueue available for foreground io */
		DBG_BUGON(!io);

		init_waitqueue_head(&io->u.wait);
		atomic_set(&io->pending_bios, 0);
		goto out;
	}

	iosb = kvzalloc(sizeof(struct z_erofs_vle_unzip_io_sb),
			GFP_KERNEL | __GFP_NOFAIL);
	DBG_BUGON(!iosb);

	/* initialize fields in the allocated descriptor */
	io = &iosb->io;
	iosb->sb = sb;
	INIT_WORK(&io->u.work, z_erofs_vle_unzip_wq);
out:
	io->head = Z_EROFS_VLE_WORKGRP_TAIL_CLOSED;
	return io;
}

/* define workgroup jobqueue types */
enum {
#ifdef EROFS_FS_HAS_MANAGED_CACHE
	JQ_BYPASS,
#endif
	JQ_SUBMIT,
	NR_JOBQUEUES,
};

static void *jobqueueset_init(struct super_block *sb,
			      z_erofs_vle_owned_workgrp_t qtail[],
			      struct z_erofs_vle_unzip_io *q[],
			      struct z_erofs_vle_unzip_io *fgq,
			      bool forcefg)
{
#ifdef EROFS_FS_HAS_MANAGED_CACHE
	/*
	 * if managed cache is enabled, bypass jobqueue is needed,
	 * no need to read from device for all workgroups in this queue.
	 */
	q[JQ_BYPASS] = jobqueue_init(sb, fgq + JQ_BYPASS, true);
	qtail[JQ_BYPASS] = &q[JQ_BYPASS]->head;
#endif

	q[JQ_SUBMIT] = jobqueue_init(sb, fgq + JQ_SUBMIT, forcefg);
	qtail[JQ_SUBMIT] = &q[JQ_SUBMIT]->head;

	return tagptr_cast_ptr(tagptr_fold(tagptr1_t, q[JQ_SUBMIT], !forcefg));
}

#ifdef EROFS_FS_HAS_MANAGED_CACHE
static void move_to_bypass_jobqueue(struct z_erofs_vle_workgroup *grp,
				    z_erofs_vle_owned_workgrp_t qtail[],
				    z_erofs_vle_owned_workgrp_t owned_head)
{
	z_erofs_vle_owned_workgrp_t *const submit_qtail = qtail[JQ_SUBMIT];
	z_erofs_vle_owned_workgrp_t *const bypass_qtail = qtail[JQ_BYPASS];

	DBG_BUGON(owned_head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);
	if (owned_head == Z_EROFS_VLE_WORKGRP_TAIL)
		owned_head = Z_EROFS_VLE_WORKGRP_TAIL_CLOSED;

	WRITE_ONCE(grp->next, Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);

	WRITE_ONCE(*submit_qtail, owned_head);
	WRITE_ONCE(*bypass_qtail, &grp->next);

	qtail[JQ_BYPASS] = &grp->next;
}

static bool postsubmit_is_all_bypassed(struct z_erofs_vle_unzip_io *q[],
				       unsigned int nr_bios,
				       bool force_fg)
{
	/*
	 * although background is preferred, no one is pending for submission.
	 * don't issue workqueue for decompression but drop it directly instead.
	 */
	if (force_fg || nr_bios)
		return false;

	kvfree(container_of(q[JQ_SUBMIT],
			    struct z_erofs_vle_unzip_io_sb,
			    io));
	return true;
}
#else
static void move_to_bypass_jobqueue(struct z_erofs_vle_workgroup *grp,
				    z_erofs_vle_owned_workgrp_t qtail[],
				    z_erofs_vle_owned_workgrp_t owned_head)
{
	/* impossible to bypass submission for managed cache disabled */
	DBG_BUGON(1);
}

static bool postsubmit_is_all_bypassed(struct z_erofs_vle_unzip_io *q[],
				       unsigned int nr_bios,
				       bool force_fg)
{
	/* bios should be >0 if managed cache is disabled */
	DBG_BUGON(!nr_bios);
	return false;
}
#endif

static bool z_erofs_vle_submit_all(struct super_block *sb,
				   z_erofs_vle_owned_workgrp_t owned_head,
				   struct list_head *pagepool,
				   struct z_erofs_vle_unzip_io *fgq,
				   bool force_fg)
{
	struct erofs_sb_info *const sbi = EROFS_SB(sb);
	const unsigned int clusterpages = erofs_clusterpages(sbi);
	const gfp_t gfp = GFP_NOFS;

	z_erofs_vle_owned_workgrp_t qtail[NR_JOBQUEUES];
	struct z_erofs_vle_unzip_io *q[NR_JOBQUEUES];
	struct bio *bio;
	void *bi_private;
	/* since bio will be NULL, no need to initialize last_index */
	pgoff_t uninitialized_var(last_index);
	bool force_submit = false;
	unsigned int nr_bios;

	if (unlikely(owned_head == Z_EROFS_VLE_WORKGRP_TAIL))
		return false;

	force_submit = false;
	bio = NULL;
	nr_bios = 0;
	bi_private = jobqueueset_init(sb, qtail, q, fgq, force_fg);

	/* by default, all need io submission */
	q[JQ_SUBMIT]->head = owned_head;

	do {
		struct z_erofs_vle_workgroup *grp;
		pgoff_t first_index;
		struct page *page;
		unsigned int i = 0, bypass = 0;
		int err;

		/* no possible 'owned_head' equals the following */
		DBG_BUGON(owned_head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);
		DBG_BUGON(owned_head == Z_EROFS_VLE_WORKGRP_NIL);

		grp = container_of(owned_head,
				   struct z_erofs_vle_workgroup, next);

		/* close the main owned chain at first */
		owned_head = cmpxchg(&grp->next, Z_EROFS_VLE_WORKGRP_TAIL,
				     Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);

		first_index = grp->obj.index;
		force_submit |= (first_index != last_index + 1);

repeat:
		page = pickup_page_for_submission(grp, i, pagepool,
						  MNGD_MAPPING(sbi), gfp);
		if (!page) {
			force_submit = true;
			++bypass;
			goto skippage;
		}

		if (bio && force_submit) {
submit_bio_retry:
			__submit_bio(bio, REQ_OP_READ, 0);
			bio = NULL;
		}

		if (!bio) {
			bio = erofs_grab_bio(sb, first_index + i,
					     BIO_MAX_PAGES, bi_private,
					     z_erofs_vle_read_endio, true);
			++nr_bios;
		}

		err = bio_add_page(bio, page, PAGE_SIZE, 0);
		if (err < PAGE_SIZE)
			goto submit_bio_retry;

		force_submit = false;
		last_index = first_index + i;
skippage:
		if (++i < clusterpages)
			goto repeat;

		if (bypass < clusterpages)
			qtail[JQ_SUBMIT] = &grp->next;
		else
			move_to_bypass_jobqueue(grp, qtail, owned_head);
	} while (owned_head != Z_EROFS_VLE_WORKGRP_TAIL);

	if (bio)
		__submit_bio(bio, REQ_OP_READ, 0);

	if (postsubmit_is_all_bypassed(q, nr_bios, force_fg))
		return true;

	z_erofs_vle_unzip_kickoff(bi_private, nr_bios);
	return true;
}

static void z_erofs_submit_and_unzip(struct z_erofs_vle_frontend *f,
				     struct list_head *pagepool,
				     bool force_fg)
{
	struct super_block *sb = f->inode->i_sb;
	struct z_erofs_vle_unzip_io io[NR_JOBQUEUES];

	if (!z_erofs_vle_submit_all(sb, f->owned_head, pagepool, io, force_fg))
		return;

#ifdef EROFS_FS_HAS_MANAGED_CACHE
	z_erofs_vle_unzip_all(sb, &io[JQ_BYPASS], pagepool);
#endif
	if (!force_fg)
		return;

	/* wait until all bios are completed */
	wait_event(io[JQ_SUBMIT].u.wait,
		   !atomic_read(&io[JQ_SUBMIT].pending_bios));

	/* let's synchronous decompression */
	z_erofs_vle_unzip_all(sb, &io[JQ_SUBMIT], pagepool);
}

static int z_erofs_vle_normalaccess_readpage(struct file *file,
					     struct page *page)
{
	struct inode *const inode = page->mapping->host;
	struct z_erofs_vle_frontend f = VLE_FRONTEND_INIT(inode);
	int err;
	LIST_HEAD(pagepool);

	trace_erofs_readpage(page, false);

	f.headoffset = (erofs_off_t)page->index << PAGE_SHIFT;

	err = z_erofs_do_read_page(&f, page, &pagepool);
	(void)z_erofs_vle_work_iter_end(&f.builder);

	if (err) {
		errln("%s, failed to read, err [%d]", __func__, err);
		goto out;
	}

	z_erofs_submit_and_unzip(&f, &pagepool, true);
out:
	if (f.map.mpage)
		put_page(f.map.mpage);

	/* clean up the remaining free pages */
	put_pages_list(&pagepool);
	return 0;
}

static int z_erofs_vle_normalaccess_readpages(struct file *filp,
					      struct address_space *mapping,
					      struct list_head *pages,
					      unsigned int nr_pages)
{
	struct inode *const inode = mapping->host;
	struct erofs_sb_info *const sbi = EROFS_I_SB(inode);

	bool sync = __should_decompress_synchronously(sbi, nr_pages);
	struct z_erofs_vle_frontend f = VLE_FRONTEND_INIT(inode);
	gfp_t gfp = mapping_gfp_constraint(mapping, GFP_KERNEL);
	struct page *head = NULL;
	LIST_HEAD(pagepool);

	trace_erofs_readpages(mapping->host, lru_to_page(pages),
			      nr_pages, false);

	f.headoffset = (erofs_off_t)lru_to_page(pages)->index << PAGE_SHIFT;

	for (; nr_pages; --nr_pages) {
		struct page *page = lru_to_page(pages);

		prefetchw(&page->flags);
		list_del(&page->lru);

		/*
		 * A pure asynchronous readahead is indicated if
		 * a PG_readahead marked page is hitted at first.
		 * Let's also do asynchronous decompression for this case.
		 */
		sync &= !(PageReadahead(page) && !head);

		if (add_to_page_cache_lru(page, mapping, page->index, gfp)) {
			list_add(&page->lru, &pagepool);
			continue;
		}

		set_page_private(page, (unsigned long)head);
		head = page;
	}

	while (head) {
		struct page *page = head;
		int err;

		/* traversal in reverse order */
		head = (void *)page_private(page);

		err = z_erofs_do_read_page(&f, page, &pagepool);
		if (err) {
			struct erofs_vnode *vi = EROFS_V(inode);

			errln("%s, readahead error at page %lu of nid %llu",
			      __func__, page->index, vi->nid);
		}

		put_page(page);
	}

	(void)z_erofs_vle_work_iter_end(&f.builder);

	z_erofs_submit_and_unzip(&f, &pagepool, sync);

	if (f.map.mpage)
		put_page(f.map.mpage);

	/* clean up the remaining free pages */
	put_pages_list(&pagepool);
	return 0;
}

const struct address_space_operations z_erofs_vle_normalaccess_aops = {
	.readpage = z_erofs_vle_normalaccess_readpage,
	.readpages = z_erofs_vle_normalaccess_readpages,
};

