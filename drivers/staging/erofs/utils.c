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

static DEFINE_MUTEX(erofs_sb_list_lock);
static LIST_HEAD(erofs_sb_list);

void erofs_register_super(struct super_block *sb)
{
	mutex_lock(&erofs_sb_list_lock);
	list_add(&EROFS_SB(sb)->list, &erofs_sb_list);
	mutex_unlock(&erofs_sb_list_lock);
}

void erofs_unregister_super(struct super_block *sb)
{
	mutex_lock(&erofs_sb_list_lock);
	list_del(&EROFS_SB(sb)->list);
	mutex_unlock(&erofs_sb_list_lock);
}

