/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */
/*
 * Copyright (c) 2017 Hisilicon Limited.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
 */

#include <linux/platform_device.h>
#include <rdma/ib_umem.h>
#include "hns_roce_device.h"

int hns_roce_db_map_user(struct hns_roce_ucontext *context, unsigned long virt,
			 struct hns_roce_db *db)
{
	struct hns_roce_user_db_page *page;
	int ret = 0;

	mutex_lock(&context->page_mutex);

	list_for_each_entry(page, &context->page_list, list)
		if (page->user_virt == (virt & PAGE_MASK))
			goto found;

	page = kmalloc(sizeof(*page), GFP_KERNEL);
	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	refcount_set(&page->refcount, 1);
	page->user_virt = (virt & PAGE_MASK);
	page->umem = ib_umem_get(&context->ibucontext, virt & PAGE_MASK,
				 PAGE_SIZE, 0, 0);
	if (IS_ERR(page->umem)) {
		ret = PTR_ERR(page->umem);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &context->page_list);

found:
	db->dma = sg_dma_address(page->umem->sg_head.sgl) +
		  (virt & ~PAGE_MASK);
	db->u.user_page = page;
	refcount_inc(&page->refcount);

out:
	mutex_unlock(&context->page_mutex);

	return ret;
}
EXPORT_SYMBOL(hns_roce_db_map_user);

void hns_roce_db_unmap_user(struct hns_roce_ucontext *context,
			    struct hns_roce_db *db)
{
	mutex_lock(&context->page_mutex);

	refcount_dec(&db->u.user_page->refcount);
	if (refcount_dec_if_one(&db->u.user_page->refcount)) {
		list_del(&db->u.user_page->list);
		ib_umem_release(db->u.user_page->umem);
		kfree(db->u.user_page);
	}

	mutex_unlock(&context->page_mutex);
}
EXPORT_SYMBOL(hns_roce_db_unmap_user);
