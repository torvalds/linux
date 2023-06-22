// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF System heap exporter
 * Originally copied from: drivers/dma-buf/heaps/system_heap.c as of commit
 * 263e38f82cbb ("dma-buf: heaps: Remove redundant heap identifier from system
 * heap name")
 *
 * Additions taken from modifications to drivers/dma-buf/heaps/system-heap.c,
 * from patches submitted, are listed below:
 *
 * Addition that modifies dma_buf ops to use SG tables taken from
 * drivers/dma-buf/heaps/system-heap.c in:
 * https://lore.kernel.org/lkml/20201017013255.43568-2-john.stultz@linaro.org/
 *
 * Addition that skips unneeded syncs in the dma_buf ops taken from
 * https://lore.kernel.org/lkml/20201017013255.43568-5-john.stultz@linaro.org/
 *
 * Addition that tries to allocate higher order pages taken from
 * https://lore.kernel.org/lkml/20201017013255.43568-6-john.stultz@linaro.org/
 *
 * Addition that implements an uncached heap taken from
 * https://lore.kernel.org/lkml/20201017013255.43568-8-john.stultz@linaro.org/,
 * with our own modificaitons made to account for core kernel changes that are
 * a part of the patch series.
 *
 * Pooling functionality taken from:
 * Git-repo: https://git.linaro.org/people/john.stultz/android-dev.git
 * Branch: dma-buf-heap-perf
 * Git-commit: 6f080eb67dce63c6efa57ef564ca4cd762ccebb0
 * Git-commit: 6fb9593b928c4cb485bef4e88c59c6b9fdf11352
 *
 * Branched off from qcom_system_heap.c as of commit a4e135a8e482
 * ("dt-bindings: ipcc: Add WPSS client to IPCC header") to accommodate secure
 * pooling.
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019, 2020 Linaro Ltd.
 *
 * Portions based off of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/qcom_dma_heap.h>

#include "qcom_dma_heap_secure_utils.h"
#include "qcom_dynamic_page_pool.h"
#include "qcom_sg_ops.h"
#include "qcom_secure_system_heap.h"
#include "qcom_system_heap.h"

#define MAX_NR_PREFETCH_REGIONS 32

/*
 * The video client may not hold the last reference count on the
 * ion_buffer(s). Delay for a short time after the video client sends
 * the IOC_DRAIN event to increase the chance that the reference
 * count drops to zero. Time in milliseconds.
 */
#define SHRINK_DELAY 1000

struct prefetch_info {
	struct list_head list;
	struct dma_heap *heap;
	u64 size;
	bool shrink;
};

static LIST_HEAD(prefetch_list);
static DEFINE_SPINLOCK(work_lock);
static struct delayed_work prefetch_work;
static struct workqueue_struct *prefetch_wq;

static LIST_HEAD(secure_heaps);

static inline struct dynamic_page_pool **get_sys_heap_page_pool(void)
{
	static struct dynamic_page_pool **qcom_sys_heap_pools;
	struct dma_heap *dma_heap;
	struct qcom_system_heap *sys_heap;

	if (qcom_sys_heap_pools)
		return qcom_sys_heap_pools;

	dma_heap = dma_heap_find("qcom,system");
	if (!dma_heap) {
		pr_err("Unable to find the system heap\n");
		goto out;
	}

	sys_heap = (struct qcom_system_heap *) dma_heap_get_drvdata(dma_heap);
	qcom_sys_heap_pools = sys_heap->pool_list;

out:
	return qcom_sys_heap_pools;
}

static struct sg_table *get_pages(u64 size, struct dma_heap *heap)
{
	LIST_HEAD(pages);
	struct dynamic_page_pool **qcom_sys_heap_pools;
	unsigned long size_remaining = size;
	struct page *page, *tmp_page;
	int max_order = orders[0];
	int num_pages = 0;
	struct sg_table *sgt;
	struct scatterlist *sg;
	int ret;

	qcom_sys_heap_pools = get_sys_heap_page_pool();
	if (!qcom_sys_heap_pools) {
		pr_err("%s: Couldn't obtain the pools for the system heap!\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	while (size_remaining > 0) {
		page = qcom_sys_heap_alloc_largest_available(qcom_sys_heap_pools,
							     size_remaining,
							     max_order,
							     false);

		if (!page) {
			pr_err("%s: Failed to get pages from the system heap: %d, %d!\n",
			       __func__, size_remaining, size);
			ret = -ENOMEM;
			goto err;
		}

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		max_order = compound_order(page);
		num_pages++;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err;
	}

	ret = sg_alloc_table(sgt, num_pages, GFP_KERNEL);
	if (ret) {
		pr_err("%s: sg table allocation failed with %d\n", __func__, ret);
		goto free_table;
	}

	sg = sgt->sgl;
	list_for_each_entry(page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
	}

	dma_map_sgtable(dma_heap_get_dev(heap), sgt, DMA_BIDIRECTIONAL, 0);
	dma_unmap_sgtable(dma_heap_get_dev(heap), sgt, DMA_BIDIRECTIONAL, 0);

	return sgt;

free_table:
	kfree(sgt);
err:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_pages(page, compound_order(page));

	return ERR_PTR(ret);
}

static void process_one_prefetch(struct prefetch_info *info)
{
	struct qcom_secure_system_heap *secure_heap = dma_heap_get_drvdata(info->heap);

	struct sg_table *sgt;
	struct scatterlist *sg;
	struct page *page;
	int ret;
	int i, j;

	sgt = get_pages(info->size, info->heap);
	if (IS_ERR(sgt))
		return;

	ret = hyp_assign_sg_from_flags(sgt, secure_heap->vmid, true);
	if (ret)
		goto err;

	for_each_sgtable_sg(sgt, sg, i) {
		page = sg_page(sg);

		for (j = 0; j < NUM_ORDERS; j++) {
			if (compound_order(page) == orders[j])
				break;
		}
		dynamic_page_pool_free(secure_heap->pool_list[j], page);
	}

	sg_free_table(sgt);

	return;

err:
	for_each_sgtable_sg(sgt, sg, i) {
		page = sg_page(sg);
		__free_pages(page, compound_order(page));
	}
	sg_free_table(sgt);
}

static void process_one_shrink(struct prefetch_info *info)
{
	struct qcom_secure_system_heap *secure_heap = dma_heap_get_drvdata(info->heap);

	dynamic_page_pool_shrink_high_and_low(secure_heap->pool_list,
					      NUM_ORDERS, info->size / PAGE_SIZE);
}

static void secure_system_heap_prefetch_work(struct work_struct *work)
{
	struct prefetch_info *info, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&work_lock, flags);
	list_for_each_entry_safe(info, tmp, &prefetch_list, list) {
		list_del(&info->list);
		spin_unlock_irqrestore(&work_lock, flags);

		if (info->shrink)
			process_one_shrink(info);
		else
			process_one_prefetch(info);

		kfree(info);
		spin_lock_irqsave(&work_lock, flags);
	}
	spin_unlock_irqrestore(&work_lock, flags);
}

static int alloc_prefetch_info(struct dma_buf_heap_prefetch_region *region,
			       bool shrink, struct list_head *items)
{
	struct prefetch_info *info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->size = PAGE_ALIGN(region->size);
	info->heap = region->heap;
	info->shrink = shrink;
	INIT_LIST_HEAD(&info->list);
	list_add_tail(&info->list, items);
	return 0;
}

static int __qcom_secure_system_heap_resize(struct dma_buf_heap_prefetch_region *regions,
					    size_t nr_regions,
					    bool shrink)
{
	struct qcom_secure_system_heap *list_ptr, *secure_heap = NULL;
	int i, ret = 0;
	struct prefetch_info *info, *tmp;
	unsigned long flags;
	LIST_HEAD(items);

	if (!regions || nr_regions > MAX_NR_PREFETCH_REGIONS) {
		pr_err("%s: Invalid input for %s\n", __func__,
		       (shrink) ? "drain" : "prefetch");
		return -EINVAL;
	}

	for (i = 0; i < nr_regions; i++) {
		list_for_each_entry(list_ptr, &secure_heaps, list) {
			if (list_ptr == dma_heap_get_drvdata(regions->heap)) {
				secure_heap = list_ptr;
				break;
			}
		}

		if (!secure_heap) {
			pr_err("%s: %s is not a secure system heap!\n", __func__,
			       dma_heap_get_name(regions->heap));
			ret = -EINVAL;
			goto out_free;
		}

		ret = alloc_prefetch_info(&regions[i], shrink, &items);
		if (ret)
			goto out_free;
	}

	spin_lock_irqsave(&work_lock, flags);
	list_splice_tail_init(&items, &prefetch_list);
	queue_delayed_work(prefetch_wq, &prefetch_work,
			   shrink ?  msecs_to_jiffies(SHRINK_DELAY) : 0);
	spin_unlock_irqrestore(&work_lock, flags);

	return 0;

out_free:
	list_for_each_entry_safe(info, tmp, &items, list) {
		list_del(&info->list);
		kfree(info);
	}
	return ret;
}

int qcom_secure_system_heap_prefetch(struct dma_buf_heap_prefetch_region *regions,
				     size_t nr_regions)
{
	return __qcom_secure_system_heap_resize(regions, nr_regions, false);
}
EXPORT_SYMBOL(qcom_secure_system_heap_prefetch);

int qcom_secure_system_heap_drain(struct dma_buf_heap_prefetch_region *regions,
				  size_t nr_regions)
{
	return __qcom_secure_system_heap_resize(regions, nr_regions, true);
}
EXPORT_SYMBOL(qcom_secure_system_heap_drain);

static enum dynamic_pool_callback_ret free_secure_pages(struct dynamic_page_pool *pool,
							struct list_head *pages,
							int num_pages)
{
	struct page *page;
	struct sg_table sgt;
	struct scatterlist *sg;
	int ret;

	if (!num_pages)
		return DYNAMIC_POOL_SUCCESS;

	ret = sg_alloc_table(&sgt, num_pages, GFP_KERNEL);
	if (ret)
		return DYNAMIC_POOL_FAILURE;

	sg = sgt.sgl;
	list_for_each_entry(page, pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
	}

	ret = hyp_unassign_sg_from_flags(&sgt, pool->vmid, true);
	sg_free_table(&sgt);

	if (ret)
		return DYNAMIC_POOL_FAILURE;

	return DYNAMIC_POOL_SUCCESS;
}

static void page_list_merge(struct list_head *secure_pages,
			    struct list_head *non_secure_pages,
			    struct scatterlist *sg,
			    struct scatterlist *non_secure_sg)
{
	bool is_secure_head, is_non_secure_head;
	struct page *secure_page = list_first_entry(secure_pages, struct page, lru);
	struct page *non_secure_page = list_first_entry(non_secure_pages, struct page, lru);

	do {
		is_secure_head = list_entry_is_head(secure_page, secure_pages, lru);
		is_non_secure_head = list_entry_is_head(non_secure_page, non_secure_pages, lru);

		if (!is_secure_head && !is_non_secure_head) {
			if (page_size(non_secure_page) >= page_size(secure_page)) {
				sg_set_page(sg, non_secure_page,
					    page_size(non_secure_page), 0);
				sg_set_page(non_secure_sg, non_secure_page,
					    page_size(non_secure_page), 0);

				non_secure_page = list_next_entry(non_secure_page, lru);
				non_secure_sg = sg_next(non_secure_sg);
			} else {
				sg_set_page(sg, secure_page,
					    page_size(secure_page), 0);
				secure_page = list_next_entry(secure_page, lru);
			}
		} else if (!is_non_secure_head) {
			sg_set_page(sg, non_secure_page,
					page_size(non_secure_page), 0);
			sg_set_page(non_secure_sg, non_secure_page,
					page_size(non_secure_page), 0);

			non_secure_page = list_next_entry(non_secure_page, lru);
			non_secure_sg = sg_next(non_secure_sg);
		} else if (!is_secure_head) {
			sg_set_page(sg, secure_page,
					page_size(secure_page), 0);
			secure_page = list_next_entry(secure_page, lru);
		}

		sg = sg_next(sg);
	} while (sg);
}

static int page_to_pool_ind(struct page *page)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++)
		if (compound_order(page) == orders[i])
			break;
	return i;
}

static void system_heap_free(struct qcom_sg_buffer *buffer)
{
	struct qcom_secure_system_heap *sys_heap;
	struct sg_table *table;
	struct scatterlist *sg;
	int i, j;

	sys_heap = dma_heap_get_drvdata(buffer->heap);
	table = &buffer->sg_table;

	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page = sg_page(sg);

		for (j = 0; j < NUM_ORDERS; j++) {
			if (compound_order(page) == orders[j])
				break;
		}
		dynamic_page_pool_free(sys_heap->pool_list[j], page);
	}
	sg_free_table(table);
	kfree(buffer);
}

static struct page *alloc_largest_available(struct dynamic_page_pool **pools,
					    unsigned long size,
					    unsigned int max_order,
					    bool *page_from_secure_pool)
{
	struct page *page = NULL;
	int i;
	struct dynamic_page_pool **qcom_sys_heap_pools = get_sys_heap_page_pool();

	if (!qcom_sys_heap_pools) {
		pr_err("%s: Couldn't obtain the pools for the system heap!\n", __func__);
		return NULL;
	}

	*page_from_secure_pool = true;

	for (i = 0; i < NUM_ORDERS; i++) {
		unsigned long flags;

		if (size <  (PAGE_SIZE << orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		spin_lock_irqsave(&pools[i]->lock, flags);
		if (pools[i]->high_count)
			page = dynamic_page_pool_remove(pools[i], true);
		else if (pools[i]->low_count)
			page = dynamic_page_pool_remove(pools[i], false);
		spin_unlock_irqrestore(&pools[i]->lock, flags);

		if (!page)
			continue;
		return page;
	}

	*page_from_secure_pool = false;

	return qcom_sys_heap_alloc_largest_available(qcom_sys_heap_pools,
						     size,
						     max_order,
						     false);
}

static struct dma_buf *system_heap_allocate(struct dma_heap *heap,
					       unsigned long len,
					       unsigned long fd_flags,
					       unsigned long heap_flags)
{
	struct qcom_secure_system_heap *sys_heap;
	struct qcom_sg_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	bool page_from_secure_pool;
	struct dma_buf *dmabuf;
	struct sg_table *table, non_secure_table;
	struct scatterlist *sg, *non_secure_sg = NULL;
	LIST_HEAD(secure_pages);
	LIST_HEAD(non_secure_pages);
	struct page *page, *tmp_page;
	int total_pages = 0, num_non_secure_pages = 0;
	int ret = -ENOMEM, hyp_ret = 0;
	int perms;
	int vmid;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	sys_heap = dma_heap_get_drvdata(heap);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = heap;
	buffer->len = len;
	buffer->uncached = true;
	buffer->free = system_heap_free;

	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current))
			goto free_buffer;

		page = alloc_largest_available(sys_heap->pool_list,
					       size_remaining,
					       max_order,
					       &page_from_secure_pool);
		if (!page)
			goto free_buffer;

		if (page_from_secure_pool) {
			list_add_tail(&page->lru, &secure_pages);
		} else {
			num_non_secure_pages++;
			list_add_tail(&page->lru, &non_secure_pages);
		}

		size_remaining -= page_size(page);
		max_order = compound_order(page);
		total_pages++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, total_pages, GFP_KERNEL))
		goto free_buffer;
	sg = table->sgl;

	if (num_non_secure_pages) {
		if (sg_alloc_table(&non_secure_table, num_non_secure_pages, GFP_KERNEL))
			goto free_sg;
		non_secure_sg = non_secure_table.sgl;
	}

	page_list_merge(&secure_pages, &non_secure_pages, sg, non_secure_sg);

	if (num_non_secure_pages) {
		dma_map_sgtable(dma_heap_get_dev(heap), &non_secure_table, DMA_BIDIRECTIONAL, 0);
		dma_unmap_sgtable(dma_heap_get_dev(heap), &non_secure_table, DMA_BIDIRECTIONAL, 0);

		hyp_ret = hyp_assign_sg_from_flags(&non_secure_table, sys_heap->vmid, true);
		if (hyp_ret)
			goto free_non_secure_sg;
	}

	perms = msm_secure_get_vmid_perms(sys_heap->vmid);
	vmid = get_secure_vmid(sys_heap->vmid);
	buffer->vmperm = mem_buf_vmperm_alloc_staticvm(table,
				&vmid, &perms, 1);

	if (IS_ERR(buffer->vmperm)) {
		ret = PTR_ERR(buffer->vmperm);
		goto hyp_unassign;
	}

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &qcom_sg_buf_ops.dma_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = qcom_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto vmperm_release;
	}

	if (num_non_secure_pages)
		sg_free_table(&non_secure_table);

	return dmabuf;

vmperm_release:
	mem_buf_vmperm_release(buffer->vmperm);

hyp_unassign:
	/* We check PagePrivate() below to see if we've reclaimed a particular page */
	hyp_ret = hyp_unassign_sg_from_flags(&non_secure_table, sys_heap->vmid, true);

free_non_secure_sg:
	if (num_non_secure_pages)
		sg_free_table(&non_secure_table);
free_sg:
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &secure_pages, lru)
		dynamic_page_pool_free(sys_heap->pool_list[page_to_pool_ind(page)], page);
	if (!hyp_ret)
		list_for_each_entry_safe(page, tmp_page, &non_secure_pages, lru)
			__free_pages(page, compound_order(page));

	kfree(buffer);

	return ERR_PTR(ret);
}

static long get_pool_size_bytes(struct dma_heap *heap)
{
	long total_size = 0;
	int i;
	struct qcom_secure_system_heap *sys_heap = dma_heap_get_drvdata(heap);

	for (i = 0; i < NUM_ORDERS; i++)
		total_size += dynamic_page_pool_total(sys_heap->pool_list[i], true);

	return total_size << PAGE_SHIFT;
}

static const struct dma_heap_ops system_heap_ops = {
	.allocate = system_heap_allocate,
	.get_pool_size = get_pool_size_bytes,
};

static int create_prefetch_workqueue(void)
{
	static bool registered;

	if (registered)
		return 0;

	INIT_DELAYED_WORK(&prefetch_work,
			  secure_system_heap_prefetch_work);

	prefetch_wq = alloc_workqueue("system_secure_prefetch_wq",
				      WQ_UNBOUND | WQ_FREEZABLE, 0);
	if (!prefetch_wq) {
		pr_err("Failed to create system secure prefetch workqueue\n");
		return -ENOMEM;
	}

	registered = true;
	return 0;
}

void qcom_secure_system_heap_create(const char *name, const char *secure_system_alias, int vmid)
{
	struct dma_heap_export_info exp_info;
	struct dma_heap *heap;
	struct qcom_secure_system_heap *sys_heap;
	int ret;

	if (get_secure_vmid(vmid) == -EINVAL || hweight_long(vmid) != 1) {
		pr_err("Invalid VMID or supplied more than one VMID\n");
		ret = -EINVAL;
		goto out;
	}

	ret = dynamic_page_pool_init_shrinker();
	if (ret)
		goto out;

	ret = create_prefetch_workqueue();
	if (ret)
		goto out;

	sys_heap = kzalloc(sizeof(*sys_heap), GFP_KERNEL);
	if (!sys_heap) {
		ret = -ENOMEM;
		goto out;
	}

	sys_heap->vmid = vmid;
	sys_heap->pool_list = dynamic_page_pool_create_pools(vmid, free_secure_pages);
	if (IS_ERR(sys_heap->pool_list)) {
		ret = PTR_ERR(sys_heap->pool_list);
		goto free_heap;
	}

	exp_info.name = name;
	exp_info.ops = &system_heap_ops;
	exp_info.priv = sys_heap;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		ret = PTR_ERR(heap);
		goto free_pools;
	}

	dma_coerce_mask_and_coherent(dma_heap_get_dev(heap), DMA_BIT_MASK(64));

	list_add(&sys_heap->list, &secure_heaps);

	pr_info("%s: DMA-BUF Heap: Created '%s'\n", __func__, name);

	if (secure_system_alias != NULL) {
		exp_info.name = secure_system_alias;

		heap = dma_heap_add(&exp_info);
		if (IS_ERR(heap)) {
			pr_err("%s: Failed to create '%s', error is %d\n", __func__,
			       secure_system_alias, PTR_ERR(heap));
			return;
		}

		dma_coerce_mask_and_coherent(dma_heap_get_dev(heap), DMA_BIT_MASK(64));

		pr_info("%s: DMA-BUF Heap: Created '%s'\n", __func__, secure_system_alias);
	}

	return;

free_pools:
	dynamic_page_pool_release_pools(sys_heap->pool_list);

free_heap:
	kfree(sys_heap);

out:
	pr_err("%s: Failed to create '%s', error is %d\n", __func__, name, ret);
}
