// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/qcom_scm.h>

#include <soc/qcom/secure_buffer.h>

struct io_pgtable_pool {
	u32 vmid;
	struct kref ref;
	spinlock_t pool_lock;
	struct list_head page_pool;
};

static DEFINE_MUTEX(page_pool_xa_lock);
static DEFINE_XARRAY(page_pool_xa);
static atomic_long_t page_pool_count = ATOMIC_LONG_INIT(0);

static bool is_secure_vmid(u32 vmid)
{
	return !!vmid;
}

static int io_pgtable_hyp_assign_page(u32 vmid, struct page *page)
{
	struct qcom_scm_vmperm dst_vmids[] = {{QCOM_SCM_VMID_HLOS,
					       PERM_READ | PERM_WRITE},
					      {vmid, PERM_READ}};
	u64 src_vmid_list = BIT(QCOM_SCM_VMID_HLOS);
	phys_addr_t page_addr = page_to_phys(page);
	int ret;

	ret = qcom_scm_assign_mem(page_addr, PAGE_SIZE, &src_vmid_list,
			      dst_vmids, ARRAY_SIZE(dst_vmids));
	if (ret)
		pr_debug("failed qcom_assign for %pa address of size %zx - subsys VMid %d rc:%d\n",
			 &page_addr, PAGE_SIZE, vmid, ret);

	WARN(ret, "failed to assign memory to VMID: %u rc:%d\n", vmid, ret);
	return ret ? -EADDRNOTAVAIL : 0;
}

static int io_pgtable_hyp_unassign_page(u32 vmid, struct page *page)
{
	struct qcom_scm_vmperm dst_vmids[] = {{QCOM_SCM_VMID_HLOS,
					      PERM_READ | PERM_WRITE | PERM_EXEC}};
	u64 src_vmid_list = BIT(QCOM_SCM_VMID_HLOS) | BIT(vmid);
	phys_addr_t page_addr = page_to_phys(page);
	int ret;

	ret = qcom_scm_assign_mem(page_addr, PAGE_SIZE, &src_vmid_list,
			      dst_vmids, ARRAY_SIZE(dst_vmids));
	if (ret)
		pr_debug("failed qcom_assign for unassigning %pa address of size %zx - subsys VMid %d rc:%d\n",
			 &page_addr, PAGE_SIZE, vmid, ret);

	WARN(ret, "failed to unassign memory from VMID: %u rc: %d\n", vmid, ret);
	return ret ? -EADDRNOTAVAIL : 0;
}

static struct page *__alloc_page_from_pool(struct list_head *page_pool)
{
	struct page *page;

	page = list_first_entry_or_null(page_pool, struct page, lru);
	if (page) {
		list_del(&page->lru);
		atomic_long_dec(&page_pool_count);
		dec_node_page_state(page, NR_KERNEL_MISC_RECLAIMABLE);
	}

	return page;
}

static struct page *alloc_page_from_pool(u32 vmid)
{
	struct io_pgtable_pool *pool = xa_load(&page_pool_xa, vmid);
	struct page *page;
	unsigned long flags;

	spin_lock_irqsave(&pool->pool_lock, flags);
	page = __alloc_page_from_pool(&pool->page_pool);
	spin_unlock_irqrestore(&pool->pool_lock, flags);

	return page;
}

static void free_page_to_pool(struct page *page)
{
	u32 vmid = page_private(page);
	struct io_pgtable_pool *pool = xa_load(&page_pool_xa, vmid);
	unsigned long flags;

	clear_page(page_address(page));
	spin_lock_irqsave(&pool->pool_lock, flags);
	list_add(&page->lru, &pool->page_pool);
	atomic_long_inc(&page_pool_count);
	inc_node_page_state(page, NR_KERNEL_MISC_RECLAIMABLE);
	spin_unlock_irqrestore(&pool->pool_lock, flags);
}

/* Assumes that page_pool_xa_lock is held. */
static void io_pgtable_pool_release(struct kref *ref)
{
	struct io_pgtable_pool *pool = container_of(ref, struct io_pgtable_pool, ref);
	struct page *page;
	bool secure_vmid = is_secure_vmid(pool->vmid);

	xa_erase(&page_pool_xa, pool->vmid);

	/*
	 * There's no need to take the pool lock, as the pool is no longer accessible to other
	 * IOMMU clients. There's no possibility for concurrent access either as this
	 * function is only invoked when the last reference is removed.
	 */
	page = __alloc_page_from_pool(&pool->page_pool);
	while (page) {
		if (!secure_vmid || !io_pgtable_hyp_unassign_page(pool->vmid, page))
			__free_page(page);

		page = __alloc_page_from_pool(&pool->page_pool);
	}

	kfree(pool);
}

/*
 * qcom_io_pgtable_allocator_register: Register with the io-pgtable allocator interface.
 *
 * @vmid: The VMID that io-pgtable memory needs to be shared with when allocated. If VMID
 *        is 0, then page table memory will not be shared with any other VMs.
 *
 * On success, 0 is returned and there will be a reference held for metadata associated with
 * @vmid. Otherwise, an error code will be returned.
 */
int qcom_io_pgtable_allocator_register(u32 vmid)
{
	struct io_pgtable_pool *pool;
	int ret = 0;

	mutex_lock(&page_pool_xa_lock);
	pool = xa_load(&page_pool_xa, vmid);
	if (pool) {
		kref_get(&pool->ref);
		goto out;
	}

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		ret = -ENOMEM;
		goto out;
	}

	pool->vmid = vmid;
	kref_init(&pool->ref);
	spin_lock_init(&pool->pool_lock);
	INIT_LIST_HEAD(&pool->page_pool);

	ret = xa_err(xa_store(&page_pool_xa, vmid, pool, GFP_KERNEL));
	if (ret < 0)
		kfree(pool);
out:
	mutex_unlock(&page_pool_xa_lock);
	return ret;
}

/*
 * qcom_io_pgtable_allocator_unregister: Unregister with the io-pgtable allocator interface.
 *
 * @vmid: The VMID that was used when registering with the interface with
 *        qcom_io_pgtable_allocator_register().
 *
 * Decrements the references to allocator metadata for @vmid.
 *
 * If this call results in references to @vmid dropping to 0, then all metadata and pages
 * associated with @vmid are released.
 */
void qcom_io_pgtable_allocator_unregister(u32 vmid)
{
	struct io_pgtable_pool *pool;

	mutex_lock(&page_pool_xa_lock);
	pool = xa_load(&page_pool_xa, vmid);
	kref_put(&pool->ref, io_pgtable_pool_release);
	mutex_unlock(&page_pool_xa_lock);
}

/*
 * qcom_io_pgtable_alloc_page: Allocate page table memory from the io-pgtable allocator.
 *
 * @vmid: The VMID that the page table memory should be shared with.
 * @gfp: The GFP flags to be used for allocating the page table memory.
 *
 * This function may sleep if memory needs to be shared with other VMs.
 *
 * On success, a page will be returned. The page will also have been shared with other
 * VMs--if any. In case of an error, this function returns NULL.
 */
struct page *qcom_io_pgtable_alloc_page(u32 vmid, gfp_t gfp)
{
	struct page *page;

	/*
	 * Mapping memory for secure domains may result in having to assign page table
	 * memory to another VMID, which can sleep. Atomic and secure domains are
	 * not a legal combination. We can use the GFP flags to detect atomic domains,
	 * as they will have GFP_ATOMIC set.
	 */
	BUG_ON(!gfpflags_allow_blocking(gfp) && is_secure_vmid(vmid));

	page = alloc_page_from_pool(vmid);
	if (page)
		return page;

	page = alloc_page(gfp);
	if (!page)
		return NULL;
	/* The page may be inaccessible if this is true, so leak it. */
	else if (is_secure_vmid(vmid) && io_pgtable_hyp_assign_page(vmid, page))
		return NULL;

	set_page_private(page, (unsigned long)vmid);
	return page;
}

/*
 * qcom_io_pgtable_free_page: Frees page table memory.
 *
 * @page: The page to be freed.
 *
 * We cache pages in their respective page pools to improve performance
 * for future allocations.
 *
 * Export this symbol for the IOMMU driver, since it decides when
 * page table memory is freed after TLB maintenance.
 */
void qcom_io_pgtable_free_page(struct page *page)
{
	free_page_to_pool(page);
}
EXPORT_SYMBOL(qcom_io_pgtable_free_page);

static unsigned long io_pgtable_alloc_count_objects(struct shrinker *shrinker,
						    struct shrink_control *sc)
{
	unsigned long count = atomic_long_read(&page_pool_count);

	return count ? count : SHRINK_EMPTY;
}

static unsigned long scan_page_pool(struct io_pgtable_pool *pool, struct list_head *freelist,
				    unsigned long nr_to_scan)
{
	struct page *page;
	unsigned long count = 0, flags;

	spin_lock_irqsave(&pool->pool_lock, flags);
	while (count < nr_to_scan) {
		page = __alloc_page_from_pool(&pool->page_pool);
		if (page) {
			list_add(&page->lru, freelist);
			count++;
		} else {
			break;
		}
	}
	spin_unlock_irqrestore(&pool->pool_lock, flags);

	return count;
}

static unsigned long io_pgtable_alloc_scan_objects(struct shrinker *shrinker,
						   struct shrink_control *sc)
{
	struct page *page, *tmp;
	struct io_pgtable_pool *pool;
	unsigned long index;
	unsigned long nr_to_scan = sc->nr_to_scan, count = 0;
	u32 vmid;
	LIST_HEAD(freelist);

	mutex_lock(&page_pool_xa_lock);
	xa_for_each(&page_pool_xa, index, pool) {
		count += scan_page_pool(pool, &freelist, nr_to_scan - count);
		if (count >= nr_to_scan)
			break;
	}
	mutex_unlock(&page_pool_xa_lock);

	list_for_each_entry_safe(page, tmp, &freelist, lru) {
		vmid = page_private(page);
		list_del(&page->lru);

		if (!is_secure_vmid(vmid) || !io_pgtable_hyp_unassign_page(vmid, page))
			__free_page(page);
		else
			count--;
	}

	return count;
}

static struct shrinker io_pgtable_alloc_shrinker = {
	.count_objects = io_pgtable_alloc_count_objects,
	.scan_objects = io_pgtable_alloc_scan_objects,
	.seeks = DEFAULT_SEEKS,
};

int qcom_io_pgtable_alloc_init(void)
{
	return register_shrinker(&io_pgtable_alloc_shrinker, "io_pgtable_alloc");
}

void qcom_io_pgtable_alloc_exit(void)
{
	unregister_shrinker(&io_pgtable_alloc_shrinker);
}
