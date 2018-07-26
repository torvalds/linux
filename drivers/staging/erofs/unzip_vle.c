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

static struct workqueue_struct *z_erofs_workqueue __read_mostly;
static struct kmem_cache *z_erofs_workgroup_cachep __read_mostly;

void z_erofs_exit_zip_subsystem(void)
{
	BUG_ON(z_erofs_workqueue == NULL);
	BUG_ON(z_erofs_workgroup_cachep == NULL);

	destroy_workqueue(z_erofs_workqueue);
	kmem_cache_destroy(z_erofs_workgroup_cachep);
}

static inline int init_unzip_workqueue(void)
{
	const unsigned onlinecpus = num_possible_cpus();

	/*
	 * we don't need too many threads, limiting threads
	 * could improve scheduling performance.
	 */
	z_erofs_workqueue = alloc_workqueue("erofs_unzipd",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE,
		onlinecpus + onlinecpus / 4);

	return z_erofs_workqueue != NULL ? 0 : -ENOMEM;
}

int z_erofs_init_zip_subsystem(void)
{
	z_erofs_workgroup_cachep =
		kmem_cache_create("erofs_compress",
		Z_EROFS_WORKGROUP_SIZE, 0,
		SLAB_RECLAIM_ACCOUNT, NULL);

	if (z_erofs_workgroup_cachep != NULL) {
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
	 * The current work has at least been linked with the following
	 * processed chained works, which means if the processing page
	 * is the tail partial page of the work, the current work can
	 * safely use the whole page, as illustrated below:
	 * +--------------+-------------------------------------------+
	 * |  tail page   |      head page (of the previous work)     |
	 * +--------------+-------------------------------------------+
	 *   /\  which belongs to the current work
	 * [  (*) this page can be used for the current work itself.  ]
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
	unsigned compressed_deficit;
};

#define VLE_WORK_BUILDER_INIT()	\
	{ .work = NULL, .role = Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED }

#ifdef EROFS_FS_HAS_MANAGED_CACHE

static bool grab_managed_cache_pages(struct address_space *mapping,
				     erofs_blk_t start,
				     struct page **compressed_pages,
				     int clusterblks,
				     bool reserve_allocation)
{
	bool noio = true;
	unsigned int i;

	/* TODO: optimize by introducing find_get_pages_range */
	for (i = 0; i < clusterblks; ++i) {
		struct page *page, *found;

		if (READ_ONCE(compressed_pages[i]) != NULL)
			continue;

		page = found = find_get_page(mapping, start + i);
		if (found == NULL) {
			noio = false;
			if (!reserve_allocation)
				continue;
			page = EROFS_UNALLOCATED_CACHED_PAGE;
		}

		if (NULL == cmpxchg(compressed_pages + i, NULL, page))
			continue;

		if (found != NULL)
			put_page(found);
	}
	return noio;
}

/* called by erofs_shrinker to get rid of all compressed_pages */
int try_to_free_all_cached_pages(struct erofs_sb_info *sbi,
				 struct erofs_workgroup *egrp)
{
	struct z_erofs_vle_workgroup *const grp =
		container_of(egrp, struct z_erofs_vle_workgroup, obj);
	struct address_space *const mapping = sbi->managed_cache->i_mapping;
	const int clusterpages = erofs_clusterpages(sbi);
	int i;

	/*
	 * refcount of workgroup is now freezed as 1,
	 * therefore no need to worry about available decompression users.
	 */
	for (i = 0; i < clusterpages; ++i) {
		struct page *page = grp->compressed_pages[i];

		if (page == NULL || page->mapping != mapping)
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

int try_to_free_cached_page(struct address_space *mapping, struct page *page)
{
	struct erofs_sb_info *const sbi = EROFS_SB(mapping->host->i_sb);
	const unsigned int clusterpages = erofs_clusterpages(sbi);

	struct z_erofs_vle_workgroup *grp;
	int ret = 0;	/* 0 - busy */

	/* prevent the workgroup from being freed */
	rcu_read_lock();
	grp = (void *)page_private(page);

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
	}
	rcu_read_unlock();

	if (ret) {
		ClearPagePrivate(page);
		put_page(page);
	}
	return ret;
}
#endif

/* page_type must be Z_EROFS_PAGE_TYPE_EXCLUSIVE */
static inline bool try_to_reuse_as_compressed_page(
	struct z_erofs_vle_work_builder *b,
	struct page *page)
{
	while (b->compressed_deficit) {
		--b->compressed_deficit;
		if (NULL == cmpxchg(b->compressed_pages++, NULL, page))
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
	builder->work->vcnt += (unsigned)ret;

	return ret ? 0 : -EAGAIN;
}

static inline bool try_to_claim_workgroup(
	struct z_erofs_vle_workgroup *grp,
	z_erofs_vle_owned_workgrp_t *owned_head,
	bool *hosted)
{
	DBG_BUGON(*hosted == true);

	/* let's claim these following types of workgroup */
retry:
	if (grp->next == Z_EROFS_VLE_WORKGRP_NIL) {
		/* type 1, nil workgroup */
		if (Z_EROFS_VLE_WORKGRP_NIL != cmpxchg(&grp->next,
			Z_EROFS_VLE_WORKGRP_NIL, *owned_head))
			goto retry;

		*owned_head = grp;
		*hosted = true;
	} else if (grp->next == Z_EROFS_VLE_WORKGRP_TAIL) {
		/*
		 * type 2, link to the end of a existing open chain,
		 * be careful that its submission itself is governed
		 * by the original owned chain.
		 */
		if (Z_EROFS_VLE_WORKGRP_TAIL != cmpxchg(&grp->next,
			Z_EROFS_VLE_WORKGRP_TAIL, *owned_head))
			goto retry;

		*owned_head = Z_EROFS_VLE_WORKGRP_TAIL;
	} else
		return false;	/* :( better luck next time */

	return true;	/* lucky, I am the followee :) */
}

static struct z_erofs_vle_work *
z_erofs_vle_work_lookup(struct super_block *sb,
			pgoff_t idx, unsigned pageofs,
			struct z_erofs_vle_workgroup **grp_ret,
			enum z_erofs_vle_work_role *role,
			z_erofs_vle_owned_workgrp_t *owned_head,
			bool *hosted)
{
	bool tag, primary;
	struct erofs_workgroup *egrp;
	struct z_erofs_vle_workgroup *grp;
	struct z_erofs_vle_work *work;

	egrp = erofs_find_workgroup(sb, idx, &tag);
	if (egrp == NULL) {
		*grp_ret = NULL;
		return NULL;
	}

	*grp_ret = grp = container_of(egrp,
		struct z_erofs_vle_workgroup, obj);

#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
	work = z_erofs_vle_grab_work(grp, pageofs);
	primary = true;
#else
	BUG();
#endif

	DBG_BUGON(work->pageofs != pageofs);

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

	*hosted = false;
	if (!primary)
		*role = Z_EROFS_VLE_WORK_SECONDARY;
	/* claim the workgroup if possible */
	else if (try_to_claim_workgroup(grp, owned_head, hosted))
		*role = Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED;
	else
		*role = Z_EROFS_VLE_WORK_PRIMARY;

	return work;
}

static struct z_erofs_vle_work *
z_erofs_vle_work_register(struct super_block *sb,
			  struct z_erofs_vle_workgroup **grp_ret,
			  struct erofs_map_blocks *map,
			  pgoff_t index, unsigned pageofs,
			  enum z_erofs_vle_work_role *role,
			  z_erofs_vle_owned_workgrp_t *owned_head,
			  bool *hosted)
{
	bool newgrp = false;
	struct z_erofs_vle_workgroup *grp = *grp_ret;
	struct z_erofs_vle_work *work;

#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
	BUG_ON(grp != NULL);
#else
	if (grp != NULL)
		goto skip;
#endif
	/* no available workgroup, let's allocate one */
	grp = kmem_cache_zalloc(z_erofs_workgroup_cachep, GFP_NOFS);
	if (unlikely(grp == NULL))
		return ERR_PTR(-ENOMEM);

	grp->obj.index = index;
	grp->llen = map->m_llen;

	z_erofs_vle_set_workgrp_fmt(grp,
		(map->m_flags & EROFS_MAP_ZIPPED) ?
			Z_EROFS_VLE_WORKGRP_FMT_LZ4 :
			Z_EROFS_VLE_WORKGRP_FMT_PLAIN);
	atomic_set(&grp->obj.refcount, 1);

	/* new workgrps have been claimed as type 1 */
	WRITE_ONCE(grp->next, *owned_head);
	/* primary and followed work for all new workgrps */
	*role = Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED;
	/* it should be submitted by ourselves */
	*hosted = true;

	newgrp = true;
#ifdef CONFIG_EROFS_FS_ZIP_MULTIREF
skip:
	/* currently unimplemented */
	BUG();
#else
	work = z_erofs_vle_grab_primary_work(grp);
#endif
	work->pageofs = pageofs;

	mutex_init(&work->lock);

	if (newgrp) {
		int err = erofs_register_workgroup(sb, &grp->obj, 0);

		if (err) {
			kmem_cache_free(z_erofs_workgroup_cachep, grp);
			return ERR_PTR(-EAGAIN);
		}
	}

	*owned_head = *grp_ret = grp;

	mutex_lock(&work->lock);
	return work;
}

static inline void __update_workgrp_llen(struct z_erofs_vle_workgroup *grp,
					 unsigned int llen)
{
	while (1) {
		unsigned int orig_llen = grp->llen;

		if (orig_llen >= llen || orig_llen ==
			cmpxchg(&grp->llen, orig_llen, llen))
			break;
	}
}

#define builder_is_followed(builder) \
	((builder)->role >= Z_EROFS_VLE_WORK_PRIMARY_FOLLOWED)

static int z_erofs_vle_work_iter_begin(struct z_erofs_vle_work_builder *builder,
				       struct super_block *sb,
				       struct erofs_map_blocks *map,
				       z_erofs_vle_owned_workgrp_t *owned_head)
{
	const unsigned clusterpages = erofs_clusterpages(EROFS_SB(sb));
	const erofs_blk_t index = erofs_blknr(map->m_pa);
	const unsigned pageofs = map->m_la & ~PAGE_MASK;
	struct z_erofs_vle_workgroup *grp;
	struct z_erofs_vle_work *work;

	DBG_BUGON(builder->work != NULL);

	/* must be Z_EROFS_WORK_TAIL or the next chained work */
	DBG_BUGON(*owned_head == Z_EROFS_VLE_WORKGRP_NIL);
	DBG_BUGON(*owned_head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);

	DBG_BUGON(erofs_blkoff(map->m_pa));

repeat:
	work = z_erofs_vle_work_lookup(sb, index,
		pageofs, &grp, &builder->role, owned_head, &builder->hosted);
	if (work != NULL) {
		__update_workgrp_llen(grp, map->m_llen);
		goto got_it;
	}

	work = z_erofs_vle_work_register(sb, &grp, map, index, pageofs,
		&builder->role, owned_head, &builder->hosted);

	if (unlikely(work == ERR_PTR(-EAGAIN)))
		goto repeat;

	if (unlikely(IS_ERR(work)))
		return PTR_ERR(work);
got_it:
	z_erofs_pagevec_ctor_init(&builder->vector,
		Z_EROFS_VLE_INLINE_PAGEVECS, work->pagevec, work->vcnt);

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

static void __z_erofs_vle_work_release(struct z_erofs_vle_workgroup *grp,
	struct z_erofs_vle_work *work __maybe_unused)
{
	erofs_workgroup_put(&grp->obj);
}

void z_erofs_vle_work_release(struct z_erofs_vle_work *work)
{
	struct z_erofs_vle_workgroup *grp =
		z_erofs_vle_work_workgroup(work, true);

	__z_erofs_vle_work_release(grp, work);
}

static inline bool
z_erofs_vle_work_iter_end(struct z_erofs_vle_work_builder *builder)
{
	struct z_erofs_vle_work *work = builder->work;

	if (work == NULL)
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

	if (unlikely(page == NULL))
		return NULL;

	page->mapping = Z_EROFS_MAPPING_STAGING;
	return page;
}

struct z_erofs_vle_frontend {
	struct inode *const inode;

	struct z_erofs_vle_work_builder builder;
	struct erofs_map_blocks_iter m_iter;

	z_erofs_vle_owned_workgrp_t owned_head;

	bool initial;
#if (EROFS_FS_ZIP_CACHE_LVL >= 2)
	erofs_off_t cachedzone_la;
#endif
};

#define VLE_FRONTEND_INIT(__i) { \
	.inode = __i, \
	.m_iter = { \
		{ .m_llen = 0, .m_plen = 0 }, \
		.mpage = NULL \
	}, \
	.builder = VLE_WORK_BUILDER_INIT(), \
	.owned_head = Z_EROFS_VLE_WORKGRP_TAIL, \
	.initial = true, }

static int z_erofs_do_read_page(struct z_erofs_vle_frontend *fe,
				struct page *page,
				struct list_head *page_pool)
{
	struct super_block *const sb = fe->inode->i_sb;
	struct erofs_sb_info *const sbi __maybe_unused = EROFS_SB(sb);
	struct erofs_map_blocks_iter *const m = &fe->m_iter;
	struct erofs_map_blocks *const map = &m->map;
	struct z_erofs_vle_work_builder *const builder = &fe->builder;
	const loff_t offset = page_offset(page);

	bool tight = builder_is_followed(builder);
	struct z_erofs_vle_work *work = builder->work;

#ifdef EROFS_FS_HAS_MANAGED_CACHE
	struct address_space *const mngda = sbi->managed_cache->i_mapping;
	struct z_erofs_vle_workgroup *grp;
	bool noio_outoforder;
#endif

	enum z_erofs_page_type page_type;
	unsigned cur, end, spiltted, index;
	int err;

	/* register locked file pages as online pages in pack */
	z_erofs_onlinepage_init(page);

	spiltted = 0;
	end = PAGE_SIZE;
repeat:
	cur = end - 1;

	/* lucky, within the range of the current map_blocks */
	if (offset + cur >= map->m_la &&
		offset + cur < map->m_la + map->m_llen)
		goto hitted;

	/* go ahead the next map_blocks */
	debugln("%s: [out-of-range] pos %llu", __func__, offset + cur);

	if (!z_erofs_vle_work_iter_end(builder))
		fe->initial = false;

	map->m_la = offset + cur;
	map->m_llen = 0;
	err = erofs_map_blocks_iter(fe->inode, map, &m->mpage, 0);
	if (unlikely(err))
		goto err_out;

	/* deal with hole (FIXME! broken now) */
	if (unlikely(!(map->m_flags & EROFS_MAP_MAPPED)))
		goto hitted;

	DBG_BUGON(map->m_plen != 1 << sbi->clusterbits);
	BUG_ON(erofs_blkoff(map->m_pa));

	err = z_erofs_vle_work_iter_begin(builder, sb, map, &fe->owned_head);
	if (unlikely(err))
		goto err_out;

#ifdef EROFS_FS_HAS_MANAGED_CACHE
	grp = fe->builder.grp;

	/* let's do out-of-order decompression for noio */
	noio_outoforder = grab_managed_cache_pages(mngda,
		erofs_blknr(map->m_pa),
		grp->compressed_pages, erofs_blknr(map->m_plen),
		/* compressed page caching selection strategy */
		fe->initial | (EROFS_FS_ZIP_CACHE_LVL >= 2 ?
			map->m_la < fe->cachedzone_la : 0));

	if (noio_outoforder && builder_is_followed(builder))
		builder->role = Z_EROFS_VLE_WORK_PRIMARY;
#endif

	tight &= builder_is_followed(builder);
	work = builder->work;
hitted:
	cur = end - min_t(unsigned, offset + end - map->m_la, end);
	if (unlikely(!(map->m_flags & EROFS_MAP_MAPPED))) {
		zero_user_segment(page, cur, end);
		goto next_part;
	}

	/* let's derive page type */
	page_type = cur ? Z_EROFS_VLE_PAGE_TYPE_HEAD :
		(!spiltted ? Z_EROFS_PAGE_TYPE_EXCLUSIVE :
			(tight ? Z_EROFS_PAGE_TYPE_EXCLUSIVE :
				Z_EROFS_VLE_PAGE_TYPE_TAIL_SHARED));

retry:
	err = z_erofs_vle_work_add_page(builder, page, page_type);
	/* should allocate an additional staging page for pagevec */
	if (err == -EAGAIN) {
		struct page *const newpage =
			__stagingpage_alloc(page_pool, GFP_NOFS);

		err = z_erofs_vle_work_add_page(builder,
			newpage, Z_EROFS_PAGE_TYPE_EXCLUSIVE);
		if (!err)
			goto retry;
	}

	if (unlikely(err))
		goto err_out;

	index = page->index - map->m_la / PAGE_SIZE;

	/* FIXME! avoid the last relundant fixup & endio */
	z_erofs_onlinepage_fixup(page, index, true);
	++spiltted;

	/* also update nr_pages and increase queued_pages */
	work->nr_pages = max_t(pgoff_t, work->nr_pages, index + 1);
next_part:
	/* can be used for verification */
	map->m_llen = offset + cur - map->m_la;

	if ((end = cur) > 0)
		goto repeat;

	/* FIXME! avoid the last relundant fixup & endio */
	z_erofs_onlinepage_endio(page);

	debugln("%s, finish page: %pK spiltted: %u map->m_llen %llu",
		__func__, page, spiltted, map->m_llen);
	return 0;

err_out:
	/* TODO: the missing error handing cases */
	return err;
}

static void z_erofs_vle_unzip_kickoff(void *ptr, int bios)
{
	tagptr1_t t = tagptr_init(tagptr1_t, ptr);
	struct z_erofs_vle_unzip_io *io = tagptr_unfold_ptr(t);
	bool background = tagptr_unfold_tags(t);

	if (atomic_add_return(bios, &io->pending_bios))
		return;

	if (background)
		queue_work(z_erofs_workqueue, &io->u.work);
	else
		wake_up(&io->u.wait);
}

static inline void z_erofs_vle_read_endio(struct bio *bio)
{
	const blk_status_t err = bio->bi_status;
	unsigned i;
	struct bio_vec *bvec;
#ifdef EROFS_FS_HAS_MANAGED_CACHE
	struct address_space *mngda = NULL;
#endif

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;
		bool cachemngd = false;

		DBG_BUGON(PageUptodate(page));
		BUG_ON(page->mapping == NULL);

#ifdef EROFS_FS_HAS_MANAGED_CACHE
		if (unlikely(mngda == NULL && !z_erofs_is_stagingpage(page))) {
			struct inode *const inode = page->mapping->host;
			struct super_block *const sb = inode->i_sb;

			mngda = EROFS_SB(sb)->managed_cache->i_mapping;
		}

		/*
		 * If mngda has not gotten, it equals NULL,
		 * however, page->mapping never be NULL if working properly.
		 */
		cachemngd = (page->mapping == mngda);
#endif

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
#ifdef EROFS_FS_HAS_MANAGED_CACHE
	struct address_space *const mngda = sbi->managed_cache->i_mapping;
#endif
	const unsigned clusterpages = erofs_clusterpages(sbi);

	struct z_erofs_pagevec_ctor ctor;
	unsigned nr_pages;
#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
	unsigned sparsemem_pages = 0;
#endif
	struct page *pages_onstack[Z_EROFS_VLE_VMAP_ONSTACK_PAGES];
	struct page **pages, **compressed_pages, *page;
	unsigned i, llen;

	enum z_erofs_page_type page_type;
	bool overlapped;
	struct z_erofs_vle_work *work;
	void *vout;
	int err;

	might_sleep();
#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
	work = z_erofs_vle_grab_primary_work(grp);
#else
	BUG();
#endif
	BUG_ON(!READ_ONCE(work->nr_pages));

	mutex_lock(&work->lock);
	nr_pages = work->nr_pages;

	if (likely(nr_pages <= Z_EROFS_VLE_VMAP_ONSTACK_PAGES))
		pages = pages_onstack;
	else if (nr_pages <= Z_EROFS_VLE_VMAP_GLOBAL_PAGES &&
		mutex_trylock(&z_pagemap_global_lock))
		pages = z_pagemap_global;
	else {
repeat:
		pages = kvmalloc_array(nr_pages,
			sizeof(struct page *), GFP_KERNEL);

		/* fallback to global pagemap for the lowmem scenario */
		if (unlikely(pages == NULL)) {
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

	z_erofs_pagevec_ctor_init(&ctor,
		Z_EROFS_VLE_INLINE_PAGEVECS, work->pagevec, 0);

	for (i = 0; i < work->vcnt; ++i) {
		unsigned pagenr;

		page = z_erofs_pagevec_ctor_dequeue(&ctor, &page_type);

		/* all pages in pagevec ought to be valid */
		DBG_BUGON(page == NULL);
		DBG_BUGON(page->mapping == NULL);

		if (z_erofs_gather_if_stagingpage(page_pool, page))
			continue;

		if (page_type == Z_EROFS_VLE_PAGE_TYPE_HEAD)
			pagenr = 0;
		else
			pagenr = z_erofs_onlinepage_index(page);

		BUG_ON(pagenr >= nr_pages);

#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
		BUG_ON(pages[pagenr] != NULL);
		++sparsemem_pages;
#endif
		pages[pagenr] = page;
	}

	z_erofs_pagevec_ctor_exit(&ctor, true);

	overlapped = false;
	compressed_pages = grp->compressed_pages;

	for (i = 0; i < clusterpages; ++i) {
		unsigned pagenr;

		page = compressed_pages[i];

		/* all compressed pages ought to be valid */
		DBG_BUGON(page == NULL);
		DBG_BUGON(page->mapping == NULL);

		if (z_erofs_is_stagingpage(page))
			continue;
#ifdef EROFS_FS_HAS_MANAGED_CACHE
		else if (page->mapping == mngda) {
			BUG_ON(PageLocked(page));
			BUG_ON(!PageUptodate(page));
			continue;
		}
#endif

		/* only non-head page could be reused as a compressed page */
		pagenr = z_erofs_onlinepage_index(page);

		BUG_ON(pagenr >= nr_pages);
#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
		BUG_ON(pages[pagenr] != NULL);
		++sparsemem_pages;
#endif
		pages[pagenr] = page;

		overlapped = true;
	}

	llen = (nr_pages << PAGE_SHIFT) - work->pageofs;

	if (z_erofs_vle_workgrp_fmt(grp) == Z_EROFS_VLE_WORKGRP_FMT_PLAIN) {
		/* FIXME! this should be fixed in the future */
		BUG_ON(grp->llen != llen);

		err = z_erofs_vle_plain_copy(compressed_pages, clusterpages,
			pages, nr_pages, work->pageofs);
		goto out;
	}

	if (llen > grp->llen)
		llen = grp->llen;

	err = z_erofs_vle_unzip_fast_percpu(compressed_pages,
		clusterpages, pages, llen, work->pageofs,
		z_erofs_onlinepage_endio);
	if (err != -ENOTSUPP)
		goto out_percpu;

#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
	if (sparsemem_pages >= nr_pages) {
		BUG_ON(sparsemem_pages > nr_pages);
		goto skip_allocpage;
	}
#endif

	for (i = 0; i < nr_pages; ++i) {
		if (pages[i] != NULL)
			continue;

		pages[i] = __stagingpage_alloc(page_pool, GFP_NOFS);
	}

#ifndef CONFIG_EROFS_FS_ZIP_MULTIREF
skip_allocpage:
#endif
	vout = erofs_vmap(pages, nr_pages);

	err = z_erofs_vle_unzip_vmap(compressed_pages,
		clusterpages, vout, llen, work->pageofs, overlapped);

	erofs_vunmap(vout, nr_pages);

out:
	for (i = 0; i < nr_pages; ++i) {
		page = pages[i];
		DBG_BUGON(page->mapping == NULL);

		/* recycle all individual staging pages */
		if (z_erofs_gather_if_stagingpage(page_pool, page))
			continue;

		if (unlikely(err < 0))
			SetPageError(page);

		z_erofs_onlinepage_endio(page);
	}

out_percpu:
	for (i = 0; i < clusterpages; ++i) {
		page = compressed_pages[i];

#ifdef EROFS_FS_HAS_MANAGED_CACHE
		if (page->mapping == mngda)
			continue;
#endif
		/* recycle all individual staging pages */
		(void)z_erofs_gather_if_stagingpage(page_pool, page);

		WRITE_ONCE(compressed_pages[i], NULL);
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

		grp = owned;
		owned = READ_ONCE(grp->next);

		z_erofs_vle_unzip(sb, grp, page_pool);
	};
}

static void z_erofs_vle_unzip_wq(struct work_struct *work)
{
	struct z_erofs_vle_unzip_io_sb *iosb = container_of(work,
		struct z_erofs_vle_unzip_io_sb, io.u.work);
	LIST_HEAD(page_pool);

	BUG_ON(iosb->io.head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);
	z_erofs_vle_unzip_all(iosb->sb, &iosb->io, &page_pool);

	put_pages_list(&page_pool);
	kvfree(iosb);
}

static inline struct z_erofs_vle_unzip_io *
prepare_io_handler(struct super_block *sb,
		   struct z_erofs_vle_unzip_io *io,
		   bool background)
{
	struct z_erofs_vle_unzip_io_sb *iosb;

	if (!background) {
		/* waitqueue available for foreground io */
		BUG_ON(io == NULL);

		init_waitqueue_head(&io->u.wait);
		atomic_set(&io->pending_bios, 0);
		goto out;
	}

	if (io != NULL)
		BUG();
	else {
		/* allocate extra io descriptor for background io */
		iosb = kvzalloc(sizeof(struct z_erofs_vle_unzip_io_sb),
			GFP_KERNEL | __GFP_NOFAIL);
		BUG_ON(iosb == NULL);

		io = &iosb->io;
	}

	iosb->sb = sb;
	INIT_WORK(&io->u.work, z_erofs_vle_unzip_wq);
out:
	io->head = Z_EROFS_VLE_WORKGRP_TAIL_CLOSED;
	return io;
}

#ifdef EROFS_FS_HAS_MANAGED_CACHE
/* true - unlocked (noio), false - locked (need submit io) */
static inline bool recover_managed_page(struct z_erofs_vle_workgroup *grp,
					struct page *page)
{
	wait_on_page_locked(page);
	if (PagePrivate(page) && PageUptodate(page))
		return true;

	lock_page(page);
	if (unlikely(!PagePrivate(page))) {
		set_page_private(page, (unsigned long)grp);
		SetPagePrivate(page);
	}
	if (unlikely(PageUptodate(page))) {
		unlock_page(page);
		return true;
	}
	return false;
}

#define __FSIO_1 1
#else
#define __FSIO_1 0
#endif

static bool z_erofs_vle_submit_all(struct super_block *sb,
				   z_erofs_vle_owned_workgrp_t owned_head,
				   struct list_head *pagepool,
				   struct z_erofs_vle_unzip_io *fg_io,
				   bool force_fg)
{
	struct erofs_sb_info *const sbi = EROFS_SB(sb);
	const unsigned clusterpages = erofs_clusterpages(sbi);
	const gfp_t gfp = GFP_NOFS;
#ifdef EROFS_FS_HAS_MANAGED_CACHE
	struct address_space *const mngda = sbi->managed_cache->i_mapping;
	struct z_erofs_vle_workgroup *lstgrp_noio = NULL, *lstgrp_io = NULL;
#endif
	struct z_erofs_vle_unzip_io *ios[1 + __FSIO_1];
	struct bio *bio;
	tagptr1_t bi_private;
	/* since bio will be NULL, no need to initialize last_index */
	pgoff_t uninitialized_var(last_index);
	bool force_submit = false;
	unsigned nr_bios;

	if (unlikely(owned_head == Z_EROFS_VLE_WORKGRP_TAIL))
		return false;

	/*
	 * force_fg == 1, (io, fg_io[0]) no io, (io, fg_io[1]) need submit io
	 * force_fg == 0, (io, fg_io[0]) no io; (io[1], bg_io) need submit io
	 */
#ifdef EROFS_FS_HAS_MANAGED_CACHE
	ios[0] = prepare_io_handler(sb, fg_io + 0, false);
#endif

	if (force_fg) {
		ios[__FSIO_1] = prepare_io_handler(sb, fg_io + __FSIO_1, false);
		bi_private = tagptr_fold(tagptr1_t, ios[__FSIO_1], 0);
	} else {
		ios[__FSIO_1] = prepare_io_handler(sb, NULL, true);
		bi_private = tagptr_fold(tagptr1_t, ios[__FSIO_1], 1);
	}

	nr_bios = 0;
	force_submit = false;
	bio = NULL;

	/* by default, all need io submission */
	ios[__FSIO_1]->head = owned_head;

	do {
		struct z_erofs_vle_workgroup *grp;
		struct page **compressed_pages, *oldpage, *page;
		pgoff_t first_index;
		unsigned i = 0;
#ifdef EROFS_FS_HAS_MANAGED_CACHE
		unsigned int noio = 0;
		bool cachemngd;
#endif
		int err;

		/* no possible 'owned_head' equals the following */
		DBG_BUGON(owned_head == Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);
		DBG_BUGON(owned_head == Z_EROFS_VLE_WORKGRP_NIL);

		grp = owned_head;

		/* close the main owned chain at first */
		owned_head = cmpxchg(&grp->next, Z_EROFS_VLE_WORKGRP_TAIL,
			Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);

		first_index = grp->obj.index;
		compressed_pages = grp->compressed_pages;

		force_submit |= (first_index != last_index + 1);
repeat:
		/* fulfill all compressed pages */
		oldpage = page = READ_ONCE(compressed_pages[i]);

#ifdef EROFS_FS_HAS_MANAGED_CACHE
		cachemngd = false;

		if (page == EROFS_UNALLOCATED_CACHED_PAGE) {
			cachemngd = true;
			goto do_allocpage;
		} else if (page != NULL) {
			if (page->mapping != mngda)
				BUG_ON(PageUptodate(page));
			else if (recover_managed_page(grp, page)) {
				/* page is uptodate, skip io submission */
				force_submit = true;
				++noio;
				goto skippage;
			}
		} else {
do_allocpage:
#else
		if (page != NULL)
			BUG_ON(PageUptodate(page));
		else {
#endif
			page = __stagingpage_alloc(pagepool, gfp);

			if (oldpage != cmpxchg(compressed_pages + i,
				oldpage, page)) {
				list_add(&page->lru, pagepool);
				goto repeat;
#ifdef EROFS_FS_HAS_MANAGED_CACHE
			} else if (cachemngd && !add_to_page_cache_lru(page,
				mngda, first_index + i, gfp)) {
				set_page_private(page, (unsigned long)grp);
				SetPagePrivate(page);
#endif
			}
		}

		if (bio != NULL && force_submit) {
submit_bio_retry:
			__submit_bio(bio, REQ_OP_READ, 0);
			bio = NULL;
		}

		if (bio == NULL) {
			bio = prepare_bio(sb, first_index + i,
				BIO_MAX_PAGES, z_erofs_vle_read_endio);
			bio->bi_private = tagptr_cast_ptr(bi_private);

			++nr_bios;
		}

		err = bio_add_page(bio, page, PAGE_SIZE, 0);
		if (err < PAGE_SIZE)
			goto submit_bio_retry;

		force_submit = false;
		last_index = first_index + i;
#ifdef EROFS_FS_HAS_MANAGED_CACHE
skippage:
#endif
		if (++i < clusterpages)
			goto repeat;

#ifdef EROFS_FS_HAS_MANAGED_CACHE
		if (noio < clusterpages) {
			lstgrp_io = grp;
		} else {
			z_erofs_vle_owned_workgrp_t iogrp_next =
				owned_head == Z_EROFS_VLE_WORKGRP_TAIL ?
				Z_EROFS_VLE_WORKGRP_TAIL_CLOSED :
				owned_head;

			if (lstgrp_io == NULL)
				ios[1]->head = iogrp_next;
			else
				WRITE_ONCE(lstgrp_io->next, iogrp_next);

			if (lstgrp_noio == NULL)
				ios[0]->head = grp;
			else
				WRITE_ONCE(lstgrp_noio->next, grp);

			lstgrp_noio = grp;
		}
#endif
	} while (owned_head != Z_EROFS_VLE_WORKGRP_TAIL);

	if (bio != NULL)
		__submit_bio(bio, REQ_OP_READ, 0);

#ifndef EROFS_FS_HAS_MANAGED_CACHE
	BUG_ON(!nr_bios);
#else
	if (lstgrp_noio != NULL)
		WRITE_ONCE(lstgrp_noio->next, Z_EROFS_VLE_WORKGRP_TAIL_CLOSED);

	if (!force_fg && !nr_bios) {
		kvfree(container_of(ios[1],
			struct z_erofs_vle_unzip_io_sb, io));
		return true;
	}
#endif

	z_erofs_vle_unzip_kickoff(tagptr_cast_ptr(bi_private), nr_bios);
	return true;
}

static void z_erofs_submit_and_unzip(struct z_erofs_vle_frontend *f,
				     struct list_head *pagepool,
				     bool force_fg)
{
	struct super_block *sb = f->inode->i_sb;
	struct z_erofs_vle_unzip_io io[1 + __FSIO_1];

	if (!z_erofs_vle_submit_all(sb, f->owned_head, pagepool, io, force_fg))
		return;

#ifdef EROFS_FS_HAS_MANAGED_CACHE
	z_erofs_vle_unzip_all(sb, &io[0], pagepool);
#endif
	if (!force_fg)
		return;

	/* wait until all bios are completed */
	wait_event(io[__FSIO_1].u.wait,
		!atomic_read(&io[__FSIO_1].pending_bios));

	/* let's synchronous decompression */
	z_erofs_vle_unzip_all(sb, &io[__FSIO_1], pagepool);
}

static int z_erofs_vle_normalaccess_readpage(struct file *file,
					     struct page *page)
{
	struct inode *const inode = page->mapping->host;
	struct z_erofs_vle_frontend f = VLE_FRONTEND_INIT(inode);
	int err;
	LIST_HEAD(pagepool);

#if (EROFS_FS_ZIP_CACHE_LVL >= 2)
	f.cachedzone_la = page->index << PAGE_SHIFT;
#endif
	err = z_erofs_do_read_page(&f, page, &pagepool);
	(void)z_erofs_vle_work_iter_end(&f.builder);

	if (err) {
		errln("%s, failed to read, err [%d]", __func__, err);
		goto out;
	}

	z_erofs_submit_and_unzip(&f, &pagepool, true);
out:
	if (f.m_iter.mpage != NULL)
		put_page(f.m_iter.mpage);

	/* clean up the remaining free pages */
	put_pages_list(&pagepool);
	return 0;
}

static inline int __z_erofs_vle_normalaccess_readpages(
	struct file *filp,
	struct address_space *mapping,
	struct list_head *pages, unsigned nr_pages, bool sync)
{
	struct inode *const inode = mapping->host;

	struct z_erofs_vle_frontend f = VLE_FRONTEND_INIT(inode);
	gfp_t gfp = mapping_gfp_constraint(mapping, GFP_KERNEL);
	struct page *head = NULL;
	LIST_HEAD(pagepool);

#if (EROFS_FS_ZIP_CACHE_LVL >= 2)
	f.cachedzone_la = lru_to_page(pages)->index << PAGE_SHIFT;
#endif
	for (; nr_pages; --nr_pages) {
		struct page *page = lru_to_page(pages);

		prefetchw(&page->flags);
		list_del(&page->lru);

		if (add_to_page_cache_lru(page, mapping, page->index, gfp)) {
			list_add(&page->lru, &pagepool);
			continue;
		}

		BUG_ON(PagePrivate(page));
		set_page_private(page, (unsigned long)head);
		head = page;
	}

	while (head != NULL) {
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

	if (f.m_iter.mpage != NULL)
		put_page(f.m_iter.mpage);

	/* clean up the remaining free pages */
	put_pages_list(&pagepool);
	return 0;
}

static int z_erofs_vle_normalaccess_readpages(
	struct file *filp,
	struct address_space *mapping,
	struct list_head *pages, unsigned nr_pages)
{
	return __z_erofs_vle_normalaccess_readpages(filp,
		mapping, pages, nr_pages,
		nr_pages < 4 /* sync */);
}

const struct address_space_operations z_erofs_vle_normalaccess_aops = {
	.readpage = z_erofs_vle_normalaccess_readpage,
	.readpages = z_erofs_vle_normalaccess_readpages,
};

#define __vle_cluster_advise(x, bit, bits) \
	((le16_to_cpu(x) >> (bit)) & ((1 << (bits)) - 1))

#define __vle_cluster_type(advise) __vle_cluster_advise(advise, \
	Z_EROFS_VLE_DI_CLUSTER_TYPE_BIT, Z_EROFS_VLE_DI_CLUSTER_TYPE_BITS)

enum {
	Z_EROFS_VLE_CLUSTER_TYPE_PLAIN,
	Z_EROFS_VLE_CLUSTER_TYPE_HEAD,
	Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD,
	Z_EROFS_VLE_CLUSTER_TYPE_RESERVED,
	Z_EROFS_VLE_CLUSTER_TYPE_MAX
};

#define vle_cluster_type(di)	\
	__vle_cluster_type((di)->di_advise)

static inline unsigned
vle_compressed_index_clusterofs(unsigned clustersize,
	struct z_erofs_vle_decompressed_index *di)
{
	debugln("%s, vle=%pK, advise=%x (type %u), clusterofs=%x blkaddr=%x",
		__func__, di, di->di_advise, vle_cluster_type(di),
		di->di_clusterofs, di->di_u.blkaddr);

	switch (vle_cluster_type(di)) {
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		break;
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		return di->di_clusterofs;
	default:
		BUG_ON(1);
	}
	return clustersize;
}

static inline erofs_blk_t
vle_extent_blkaddr(struct inode *inode, pgoff_t index)
{
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);
	struct erofs_vnode *vi = EROFS_V(inode);

	unsigned ofs = Z_EROFS_VLE_EXTENT_ALIGN(vi->inode_isize +
		vi->xattr_isize) + sizeof(struct erofs_extent_header) +
		index * sizeof(struct z_erofs_vle_decompressed_index);

	return erofs_blknr(iloc(sbi, vi->nid) + ofs);
}

static inline unsigned int
vle_extent_blkoff(struct inode *inode, pgoff_t index)
{
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);
	struct erofs_vnode *vi = EROFS_V(inode);

	unsigned ofs = Z_EROFS_VLE_EXTENT_ALIGN(vi->inode_isize +
		vi->xattr_isize) + sizeof(struct erofs_extent_header) +
		index * sizeof(struct z_erofs_vle_decompressed_index);

	return erofs_blkoff(iloc(sbi, vi->nid) + ofs);
}

/*
 * Variable-sized Logical Extent (Fixed Physical Cluster) Compression Mode
 * ---
 * VLE compression mode attempts to compress a number of logical data into
 * a physical cluster with a fixed size.
 * VLE compression mode uses "struct z_erofs_vle_decompressed_index".
 */
static erofs_off_t vle_get_logical_extent_head(
	struct inode *inode,
	struct page **page_iter,
	void **kaddr_iter,
	unsigned lcn,	/* logical cluster number */
	erofs_blk_t *pcn,
	unsigned *flags)
{
	/* for extent meta */
	struct page *page = *page_iter;
	erofs_blk_t blkaddr = vle_extent_blkaddr(inode, lcn);
	struct z_erofs_vle_decompressed_index *di;
	unsigned long long ofs;
	const unsigned int clusterbits = EROFS_SB(inode->i_sb)->clusterbits;
	const unsigned int clustersize = 1 << clusterbits;

	if (page->index != blkaddr) {
		kunmap_atomic(*kaddr_iter);
		unlock_page(page);
		put_page(page);

		*page_iter = page = erofs_get_meta_page(inode->i_sb,
			blkaddr, false);
		*kaddr_iter = kmap_atomic(page);
	}

	di = *kaddr_iter + vle_extent_blkoff(inode, lcn);
	switch (vle_cluster_type(di)) {
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		BUG_ON(!di->di_u.delta[0]);
		BUG_ON(lcn < di->di_u.delta[0]);

		ofs = vle_get_logical_extent_head(inode,
			page_iter, kaddr_iter,
			lcn - di->di_u.delta[0], pcn, flags);
		break;
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
		*flags ^= EROFS_MAP_ZIPPED;
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		/* clustersize should be a power of two */
		ofs = ((unsigned long long)lcn << clusterbits) +
			(le16_to_cpu(di->di_clusterofs) & (clustersize - 1));
		*pcn = le32_to_cpu(di->di_u.blkaddr);
		break;
	default:
		BUG_ON(1);
	}
	return ofs;
}

int z_erofs_map_blocks_iter(struct inode *inode,
	struct erofs_map_blocks *map,
	struct page **mpage_ret, int flags)
{
	/* logicial extent (start, end) offset */
	unsigned long long ofs, end;
	struct z_erofs_vle_decompressed_index *di;
	erofs_blk_t e_blkaddr, pcn;
	unsigned lcn, logical_cluster_ofs;
	u32 ofs_rem;
	struct page *mpage = *mpage_ret;
	void *kaddr;
	bool initial;
	const unsigned int clusterbits = EROFS_SB(inode->i_sb)->clusterbits;
	const unsigned int clustersize = 1 << clusterbits;

	/* if both m_(l,p)len are 0, regularize l_lblk, l_lofs, etc... */
	initial = !map->m_llen;

	/* when trying to read beyond EOF, leave it unmapped */
	if (unlikely(map->m_la >= inode->i_size)) {
		BUG_ON(!initial);
		map->m_llen = map->m_la + 1 - inode->i_size;
		map->m_la = inode->i_size - 1;
		map->m_flags = 0;
		goto out;
	}

	debugln("%s, m_la %llu m_llen %llu --- start", __func__,
		map->m_la, map->m_llen);

	ofs = map->m_la + map->m_llen;

	/* clustersize should be power of two */
	lcn = ofs >> clusterbits;
	ofs_rem = ofs & (clustersize - 1);

	e_blkaddr = vle_extent_blkaddr(inode, lcn);

	if (mpage == NULL || mpage->index != e_blkaddr) {
		if (mpage != NULL)
			put_page(mpage);

		mpage = erofs_get_meta_page(inode->i_sb, e_blkaddr, false);
		*mpage_ret = mpage;
	} else {
		lock_page(mpage);
		DBG_BUGON(!PageUptodate(mpage));
	}

	kaddr = kmap_atomic(mpage);
	di = kaddr + vle_extent_blkoff(inode, lcn);

	debugln("%s, lcn %u e_blkaddr %u e_blkoff %u", __func__, lcn,
		e_blkaddr, vle_extent_blkoff(inode, lcn));

	logical_cluster_ofs = vle_compressed_index_clusterofs(clustersize, di);
	if (!initial) {
		/* [walking mode] 'map' has been already initialized */
		map->m_llen += logical_cluster_ofs;
		goto unmap_out;
	}

	/* by default, compressed */
	map->m_flags |= EROFS_MAP_ZIPPED;

	end = (u64)(lcn + 1) * clustersize;

	switch (vle_cluster_type(di)) {
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
		if (ofs_rem >= logical_cluster_ofs)
			map->m_flags ^= EROFS_MAP_ZIPPED;
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		if (ofs_rem == logical_cluster_ofs) {
			pcn = le32_to_cpu(di->di_u.blkaddr);
			goto exact_hitted;
		}

		if (ofs_rem > logical_cluster_ofs) {
			ofs = lcn * clustersize | logical_cluster_ofs;
			pcn = le32_to_cpu(di->di_u.blkaddr);
			break;
		}

		BUG_ON(!lcn);	/* logical cluster number >= 1 */
		end = (lcn-- * clustersize) | logical_cluster_ofs;
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		/* get the correspoinding first chunk */
		ofs = vle_get_logical_extent_head(inode, mpage_ret,
			&kaddr, lcn, &pcn, &map->m_flags);
		mpage = *mpage_ret;
	}

	map->m_la = ofs;
exact_hitted:
	map->m_llen = end - ofs;
	map->m_plen = clustersize;
	map->m_pa = blknr_to_addr(pcn);
	map->m_flags |= EROFS_MAP_MAPPED;
unmap_out:
	kunmap_atomic(kaddr);
	unlock_page(mpage);
out:
	debugln("%s, m_la %llu m_pa %llu m_llen %llu m_plen %llu m_flags 0%o",
		__func__, map->m_la, map->m_pa,
		map->m_llen, map->m_plen, map->m_flags);
	return 0;
}

