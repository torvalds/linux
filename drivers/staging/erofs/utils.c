// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/utils.c
 *
 * Copyright (C) 2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */

#include "internal.h"
#include <linux/pagevec.h>

struct page *erofs_allocpage(struct list_head *pool, gfp_t gfp)
{
	struct page *page;

	if (!list_empty(pool)) {
		page = lru_to_page(pool);
		list_del(&page->lru);
	} else {
		page = alloc_pages(gfp | __GFP_NOFAIL, 0);
	}
	return page;
}

/* global shrink count (for all mounted EROFS instances) */
static atomic_long_t erofs_global_shrink_cnt;

#ifdef CONFIG_EROFS_FS_ZIP

struct erofs_workgroup *erofs_find_workgroup(
	struct super_block *sb, pgoff_t index, bool *tag)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_workgroup *grp;
	int oldcount;

repeat:
	rcu_read_lock();
	grp = radix_tree_lookup(&sbi->workstn_tree, index);
	if (grp != NULL) {
		*tag = xa_pointer_tag(grp);
		grp = xa_untag_pointer(grp);

		if (erofs_workgroup_get(grp, &oldcount)) {
			/* prefer to relax rcu read side */
			rcu_read_unlock();
			goto repeat;
		}

		/* decrease refcount added by erofs_workgroup_put */
		if (unlikely(oldcount == 1))
			atomic_long_dec(&erofs_global_shrink_cnt);
		DBG_BUGON(index != grp->index);
	}
	rcu_read_unlock();
	return grp;
}

int erofs_register_workgroup(struct super_block *sb,
			     struct erofs_workgroup *grp,
			     bool tag)
{
	struct erofs_sb_info *sbi;
	int err;

	/* grp shouldn't be broken or used before */
	if (unlikely(atomic_read(&grp->refcount) != 1)) {
		DBG_BUGON(1);
		return -EINVAL;
	}

	err = radix_tree_preload(GFP_NOFS);
	if (err)
		return err;

	sbi = EROFS_SB(sb);
	erofs_workstn_lock(sbi);

	grp = xa_tag_pointer(grp, tag);

	/*
	 * Bump up reference count before making this workgroup
	 * visible to other users in order to avoid potential UAF
	 * without serialized by erofs_workstn_lock.
	 */
	__erofs_workgroup_get(grp);

	err = radix_tree_insert(&sbi->workstn_tree,
				grp->index, grp);
	if (unlikely(err))
		/*
		 * it's safe to decrease since the workgroup isn't visible
		 * and refcount >= 2 (cannot be freezed).
		 */
		__erofs_workgroup_put(grp);

	erofs_workstn_unlock(sbi);
	radix_tree_preload_end();
	return err;
}

extern void erofs_workgroup_free_rcu(struct erofs_workgroup *grp);

static void  __erofs_workgroup_free(struct erofs_workgroup *grp)
{
	atomic_long_dec(&erofs_global_shrink_cnt);
	erofs_workgroup_free_rcu(grp);
}

int erofs_workgroup_put(struct erofs_workgroup *grp)
{
	int count = atomic_dec_return(&grp->refcount);

	if (count == 1)
		atomic_long_inc(&erofs_global_shrink_cnt);
	else if (!count)
		__erofs_workgroup_free(grp);
	return count;
}

#ifdef EROFS_FS_HAS_MANAGED_CACHE
/* for cache-managed case, customized reclaim paths exist */
static void erofs_workgroup_unfreeze_final(struct erofs_workgroup *grp)
{
	erofs_workgroup_unfreeze(grp, 0);
	__erofs_workgroup_free(grp);
}

bool erofs_try_to_release_workgroup(struct erofs_sb_info *sbi,
				    struct erofs_workgroup *grp,
				    bool cleanup)
{
	/*
	 * for managed cache enabled, the refcount of workgroups
	 * themselves could be < 0 (freezed). So there is no guarantee
	 * that all refcount > 0 if managed cache is enabled.
	 */
	if (!erofs_workgroup_try_to_freeze(grp, 1))
		return false;

	/*
	 * note that all cached pages should be unlinked
	 * before delete it from the radix tree.
	 * Otherwise some cached pages of an orphan old workgroup
	 * could be still linked after the new one is available.
	 */
	if (erofs_try_to_free_all_cached_pages(sbi, grp)) {
		erofs_workgroup_unfreeze(grp, 1);
		return false;
	}

	/*
	 * it is impossible to fail after the workgroup is freezed,
	 * however in order to avoid some race conditions, add a
	 * DBG_BUGON to observe this in advance.
	 */
	DBG_BUGON(xa_untag_pointer(radix_tree_delete(&sbi->workstn_tree,
						     grp->index)) != grp);

	/*
	 * if managed cache is enable, the last refcount
	 * should indicate the related workstation.
	 */
	erofs_workgroup_unfreeze_final(grp);
	return true;
}

#else
/* for nocache case, no customized reclaim path at all */
bool erofs_try_to_release_workgroup(struct erofs_sb_info *sbi,
				    struct erofs_workgroup *grp,
				    bool cleanup)
{
	int cnt = atomic_read(&grp->refcount);

	DBG_BUGON(cnt <= 0);
	DBG_BUGON(cleanup && cnt != 1);

	if (cnt > 1)
		return false;

	DBG_BUGON(xa_untag_pointer(radix_tree_delete(&sbi->workstn_tree,
						     grp->index)) != grp);

	/* (rarely) could be grabbed again when freeing */
	erofs_workgroup_put(grp);
	return true;
}

#endif

unsigned long erofs_shrink_workstation(struct erofs_sb_info *sbi,
				       unsigned long nr_shrink,
				       bool cleanup)
{
	pgoff_t first_index = 0;
	void *batch[PAGEVEC_SIZE];
	unsigned int freed = 0;

	int i, found;
repeat:
	erofs_workstn_lock(sbi);

	found = radix_tree_gang_lookup(&sbi->workstn_tree,
		batch, first_index, PAGEVEC_SIZE);

	for (i = 0; i < found; ++i) {
		struct erofs_workgroup *grp = xa_untag_pointer(batch[i]);

		first_index = grp->index + 1;

		/* try to shrink each valid workgroup */
		if (!erofs_try_to_release_workgroup(sbi, grp, cleanup))
			continue;

		++freed;
		if (unlikely(!--nr_shrink))
			break;
	}
	erofs_workstn_unlock(sbi);

	if (i && nr_shrink)
		goto repeat;
	return freed;
}

#endif

/* protected by 'erofs_sb_list_lock' */
static unsigned int shrinker_run_no;

/* protects the mounted 'erofs_sb_list' */
static DEFINE_SPINLOCK(erofs_sb_list_lock);
static LIST_HEAD(erofs_sb_list);

void erofs_register_super(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	mutex_init(&sbi->umount_mutex);

	spin_lock(&erofs_sb_list_lock);
	list_add(&sbi->list, &erofs_sb_list);
	spin_unlock(&erofs_sb_list_lock);
}

void erofs_unregister_super(struct super_block *sb)
{
	spin_lock(&erofs_sb_list_lock);
	list_del(&EROFS_SB(sb)->list);
	spin_unlock(&erofs_sb_list_lock);
}

unsigned long erofs_shrink_count(struct shrinker *shrink,
				 struct shrink_control *sc)
{
	return atomic_long_read(&erofs_global_shrink_cnt);
}

unsigned long erofs_shrink_scan(struct shrinker *shrink,
				struct shrink_control *sc)
{
	struct erofs_sb_info *sbi;
	struct list_head *p;

	unsigned long nr = sc->nr_to_scan;
	unsigned int run_no;
	unsigned long freed = 0;

	spin_lock(&erofs_sb_list_lock);
	do
		run_no = ++shrinker_run_no;
	while (run_no == 0);

	/* Iterate over all mounted superblocks and try to shrink them */
	p = erofs_sb_list.next;
	while (p != &erofs_sb_list) {
		sbi = list_entry(p, struct erofs_sb_info, list);

		/*
		 * We move the ones we do to the end of the list, so we stop
		 * when we see one we have already done.
		 */
		if (sbi->shrinker_run_no == run_no)
			break;

		if (!mutex_trylock(&sbi->umount_mutex)) {
			p = p->next;
			continue;
		}

		spin_unlock(&erofs_sb_list_lock);
		sbi->shrinker_run_no = run_no;

#ifdef CONFIG_EROFS_FS_ZIP
		freed += erofs_shrink_workstation(sbi, nr, false);
#endif

		spin_lock(&erofs_sb_list_lock);
		/* Get the next list element before we move this one */
		p = p->next;

		/*
		 * Move this one to the end of the list to provide some
		 * fairness.
		 */
		list_move_tail(&sbi->list, &erofs_sb_list);
		mutex_unlock(&sbi->umount_mutex);

		if (freed >= nr)
			break;
	}
	spin_unlock(&erofs_sb_list_lock);
	return freed;
}

