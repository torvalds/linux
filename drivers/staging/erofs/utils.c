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

struct page *erofs_allocpage(struct list_head *pool, gfp_t gfp)
{
	struct page *page;

	if (!list_empty(pool)) {
		page = lru_to_page(pool);
		list_del(&page->lru);
	} else {
		page = alloc_pages(gfp | __GFP_NOFAIL, 0);

		BUG_ON(page == NULL);
		BUG_ON(page->mapping != NULL);
	}
	return page;
}


/* protected by 'erofs_sb_list_lock' */
static unsigned int shrinker_run_no;

/* protects the mounted 'erofs_sb_list' */
static DEFINE_SPINLOCK(erofs_sb_list_lock);
static LIST_HEAD(erofs_sb_list);

/* global shrink count (for all mounted EROFS instances) */
static atomic_long_t erofs_global_shrink_cnt;

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

		/* add scan handlers here */

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

