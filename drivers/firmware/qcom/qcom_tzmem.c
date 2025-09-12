// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory allocator for buffers shared with the TrustZone.
 *
 * Copyright (C) 2023-2024 Linaro Ltd.
 */

#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/radix-tree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "qcom_scm.h"
#include "qcom_tzmem.h"

struct qcom_tzmem_area {
	struct list_head list;
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
	void *priv;
};

struct qcom_tzmem_pool {
	struct gen_pool *genpool;
	struct list_head areas;
	enum qcom_tzmem_policy policy;
	size_t increment;
	size_t max_size;
	spinlock_t lock;
};

struct qcom_tzmem_chunk {
	size_t size;
	struct qcom_tzmem_pool *owner;
};

static struct device *qcom_tzmem_dev;
static RADIX_TREE(qcom_tzmem_chunks, GFP_ATOMIC);
static DEFINE_SPINLOCK(qcom_tzmem_chunks_lock);

#if IS_ENABLED(CONFIG_QCOM_TZMEM_MODE_GENERIC)

static int qcom_tzmem_init(void)
{
	return 0;
}

static int qcom_tzmem_init_area(struct qcom_tzmem_area *area)
{
	return 0;
}

static void qcom_tzmem_cleanup_area(struct qcom_tzmem_area *area)
{

}

#elif IS_ENABLED(CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE)

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/of.h>

#define QCOM_SHM_BRIDGE_NUM_VM_SHIFT 9

static bool qcom_tzmem_using_shm_bridge;

/* List of machines that are known to not support SHM bridge correctly. */
static const char *const qcom_tzmem_blacklist[] = {
	"qcom,sc8180x",
	"qcom,sdm670", /* failure in GPU firmware loading */
	"qcom,sdm845", /* reset in rmtfs memory assignment */
	"qcom,sm7150", /* reset in rmtfs memory assignment */
	"qcom,sm8150", /* reset in rmtfs memory assignment */
	NULL
};

static int qcom_tzmem_init(void)
{
	const char *const *platform;
	int ret;

	for (platform = qcom_tzmem_blacklist; *platform; platform++) {
		if (of_machine_is_compatible(*platform))
			goto notsupp;
	}

	ret = qcom_scm_shm_bridge_enable(qcom_tzmem_dev);
	if (ret == -EOPNOTSUPP)
		goto notsupp;

	if (!ret)
		qcom_tzmem_using_shm_bridge = true;

	return ret;

notsupp:
	dev_info(qcom_tzmem_dev, "SHM Bridge not supported\n");
	return 0;
}

/**
 * qcom_tzmem_shm_bridge_create() - Create a SHM bridge.
 * @paddr: Physical address of the memory to share.
 * @size: Size of the memory to share.
 * @handle: Handle to the SHM bridge.
 *
 * On platforms that support SHM bridge, this function creates a SHM bridge
 * for the given memory region with QTEE. The handle returned by this function
 * must be passed to qcom_tzmem_shm_bridge_delete() to free the SHM bridge.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcom_tzmem_shm_bridge_create(phys_addr_t paddr, size_t size, u64 *handle)
{
	u64 pfn_and_ns_perm, ipfn_and_s_perm, size_and_flags;
	int ret;

	if (!qcom_tzmem_using_shm_bridge)
		return 0;

	pfn_and_ns_perm = paddr | QCOM_SCM_PERM_RW;
	ipfn_and_s_perm = paddr | QCOM_SCM_PERM_RW;
	size_and_flags = size | (1 << QCOM_SHM_BRIDGE_NUM_VM_SHIFT);

	ret = qcom_scm_shm_bridge_create(pfn_and_ns_perm, ipfn_and_s_perm,
					 size_and_flags, QCOM_SCM_VMID_HLOS,
					 handle);
	if (ret) {
		dev_err(qcom_tzmem_dev,
			"SHM Bridge failed: ret %d paddr 0x%pa, size %zu\n",
			ret, &paddr, size);

		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_tzmem_shm_bridge_create);

/**
 * qcom_tzmem_shm_bridge_delete() - Delete a SHM bridge.
 * @handle: Handle to the SHM bridge.
 *
 * On platforms that support SHM bridge, this function deletes the SHM bridge
 * for the given memory region. The handle must be the same as the one
 * returned by qcom_tzmem_shm_bridge_create().
 */
void qcom_tzmem_shm_bridge_delete(u64 handle)
{
	if (qcom_tzmem_using_shm_bridge)
		qcom_scm_shm_bridge_delete(handle);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_shm_bridge_delete);

static int qcom_tzmem_init_area(struct qcom_tzmem_area *area)
{
	int ret;

	u64 *handle __free(kfree) = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	ret = qcom_tzmem_shm_bridge_create(area->paddr, area->size, handle);
	if (ret)
		return ret;

	area->priv = no_free_ptr(handle);

	return 0;
}

static void qcom_tzmem_cleanup_area(struct qcom_tzmem_area *area)
{
	u64 *handle = area->priv;

	qcom_tzmem_shm_bridge_delete(*handle);
	kfree(handle);
}

#endif /* CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE */

static int qcom_tzmem_pool_add_memory(struct qcom_tzmem_pool *pool,
				      size_t size, gfp_t gfp)
{
	int ret;

	struct qcom_tzmem_area *area __free(kfree) = kzalloc(sizeof(*area),
							     gfp);
	if (!area)
		return -ENOMEM;

	area->size = PAGE_ALIGN(size);

	area->vaddr = dma_alloc_coherent(qcom_tzmem_dev, area->size,
					 &area->paddr, gfp);
	if (!area->vaddr)
		return -ENOMEM;

	ret = qcom_tzmem_init_area(area);
	if (ret) {
		dma_free_coherent(qcom_tzmem_dev, area->size,
				  area->vaddr, area->paddr);
		return ret;
	}

	ret = gen_pool_add_virt(pool->genpool, (unsigned long)area->vaddr,
				(phys_addr_t)area->paddr, size, -1);
	if (ret) {
		dma_free_coherent(qcom_tzmem_dev, area->size,
				  area->vaddr, area->paddr);
		return ret;
	}

	scoped_guard(spinlock_irqsave, &pool->lock)
		list_add_tail(&area->list, &pool->areas);

	area = NULL;
	return 0;
}

/**
 * qcom_tzmem_pool_new() - Create a new TZ memory pool.
 * @config: Pool configuration.
 *
 * Create a new pool of memory suitable for sharing with the TrustZone.
 *
 * Must not be used in atomic context.
 *
 * Return: New memory pool address or ERR_PTR() on error.
 */
struct qcom_tzmem_pool *
qcom_tzmem_pool_new(const struct qcom_tzmem_pool_config *config)
{
	int ret = -ENOMEM;

	might_sleep();

	switch (config->policy) {
	case QCOM_TZMEM_POLICY_STATIC:
		if (!config->initial_size)
			return ERR_PTR(-EINVAL);
		break;
	case QCOM_TZMEM_POLICY_MULTIPLIER:
		if (!config->increment)
			return ERR_PTR(-EINVAL);
		break;
	case QCOM_TZMEM_POLICY_ON_DEMAND:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	struct qcom_tzmem_pool *pool __free(kfree) = kzalloc(sizeof(*pool),
							     GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->genpool = gen_pool_create(PAGE_SHIFT, -1);
	if (!pool->genpool)
		return ERR_PTR(-ENOMEM);

	gen_pool_set_algo(pool->genpool, gen_pool_best_fit, NULL);

	pool->policy = config->policy;
	pool->increment = config->increment;
	pool->max_size = config->max_size;
	INIT_LIST_HEAD(&pool->areas);
	spin_lock_init(&pool->lock);

	if (config->initial_size) {
		ret = qcom_tzmem_pool_add_memory(pool, config->initial_size,
						 GFP_KERNEL);
		if (ret) {
			gen_pool_destroy(pool->genpool);
			return ERR_PTR(ret);
		}
	}

	return_ptr(pool);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_pool_new);

/**
 * qcom_tzmem_pool_free() - Destroy a TZ memory pool and free all resources.
 * @pool: Memory pool to free.
 *
 * Must not be called if any of the allocated chunks has not been freed.
 * Must not be used in atomic context.
 */
void qcom_tzmem_pool_free(struct qcom_tzmem_pool *pool)
{
	struct qcom_tzmem_area *area, *next;
	struct qcom_tzmem_chunk *chunk;
	struct radix_tree_iter iter;
	bool non_empty = false;
	void __rcu **slot;

	might_sleep();

	if (!pool)
		return;

	scoped_guard(spinlock_irqsave, &qcom_tzmem_chunks_lock) {
		radix_tree_for_each_slot(slot, &qcom_tzmem_chunks, &iter, 0) {
			chunk = radix_tree_deref_slot_protected(slot,
						&qcom_tzmem_chunks_lock);

			if (chunk->owner == pool)
				non_empty = true;
		}
	}

	WARN(non_empty, "Freeing TZ memory pool with memory still allocated");

	list_for_each_entry_safe(area, next, &pool->areas, list) {
		list_del(&area->list);
		qcom_tzmem_cleanup_area(area);
		dma_free_coherent(qcom_tzmem_dev, area->size,
				  area->vaddr, area->paddr);
		kfree(area);
	}

	gen_pool_destroy(pool->genpool);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_pool_free);

static void devm_qcom_tzmem_pool_free(void *data)
{
	struct qcom_tzmem_pool *pool = data;

	qcom_tzmem_pool_free(pool);
}

/**
 * devm_qcom_tzmem_pool_new() - Managed variant of qcom_tzmem_pool_new().
 * @dev: Device managing this resource.
 * @config: Pool configuration.
 *
 * Must not be used in atomic context.
 *
 * Return: Address of the managed pool or ERR_PTR() on failure.
 */
struct qcom_tzmem_pool *
devm_qcom_tzmem_pool_new(struct device *dev,
			 const struct qcom_tzmem_pool_config *config)
{
	struct qcom_tzmem_pool *pool;
	int ret;

	pool = qcom_tzmem_pool_new(config);
	if (IS_ERR(pool))
		return pool;

	ret = devm_add_action_or_reset(dev, devm_qcom_tzmem_pool_free, pool);
	if (ret)
		return ERR_PTR(ret);

	return pool;
}
EXPORT_SYMBOL_GPL(devm_qcom_tzmem_pool_new);

static bool qcom_tzmem_try_grow_pool(struct qcom_tzmem_pool *pool,
				     size_t requested, gfp_t gfp)
{
	size_t current_size = gen_pool_size(pool->genpool);

	if (pool->max_size && (current_size + requested) > pool->max_size)
		return false;

	switch (pool->policy) {
	case QCOM_TZMEM_POLICY_STATIC:
		return false;
	case QCOM_TZMEM_POLICY_MULTIPLIER:
		requested = current_size * pool->increment;
		break;
	case QCOM_TZMEM_POLICY_ON_DEMAND:
		break;
	}

	return !qcom_tzmem_pool_add_memory(pool, requested, gfp);
}

/**
 * qcom_tzmem_alloc() - Allocate a memory chunk suitable for sharing with TZ.
 * @pool: TZ memory pool from which to allocate memory.
 * @size: Number of bytes to allocate.
 * @gfp: GFP flags.
 *
 * Can be used in any context.
 *
 * Return:
 * Address of the allocated buffer or NULL if no more memory can be allocated.
 * The buffer must be released using qcom_tzmem_free().
 */
void *qcom_tzmem_alloc(struct qcom_tzmem_pool *pool, size_t size, gfp_t gfp)
{
	unsigned long vaddr;
	int ret;

	if (!size)
		return NULL;

	size = PAGE_ALIGN(size);

	struct qcom_tzmem_chunk *chunk __free(kfree) = kzalloc(sizeof(*chunk),
							       gfp);
	if (!chunk)
		return NULL;

again:
	vaddr = gen_pool_alloc(pool->genpool, size);
	if (!vaddr) {
		if (qcom_tzmem_try_grow_pool(pool, size, gfp))
			goto again;

		return NULL;
	}

	chunk->size = size;
	chunk->owner = pool;

	scoped_guard(spinlock_irqsave, &qcom_tzmem_chunks_lock) {
		ret = radix_tree_insert(&qcom_tzmem_chunks, vaddr, chunk);
		if (ret) {
			gen_pool_free(pool->genpool, vaddr, size);
			return NULL;
		}

		chunk = NULL;
	}

	return (void *)vaddr;
}
EXPORT_SYMBOL_GPL(qcom_tzmem_alloc);

/**
 * qcom_tzmem_free() - Release a buffer allocated from a TZ memory pool.
 * @vaddr: Virtual address of the buffer.
 *
 * Can be used in any context.
 */
void qcom_tzmem_free(void *vaddr)
{
	struct qcom_tzmem_chunk *chunk;

	scoped_guard(spinlock_irqsave, &qcom_tzmem_chunks_lock)
		chunk = radix_tree_delete_item(&qcom_tzmem_chunks,
					       (unsigned long)vaddr, NULL);

	if (!chunk) {
		WARN(1, "Virtual address %p not owned by TZ memory allocator",
		     vaddr);
		return;
	}

	scoped_guard(spinlock_irqsave, &chunk->owner->lock)
		gen_pool_free(chunk->owner->genpool, (unsigned long)vaddr,
			      chunk->size);
	kfree(chunk);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_free);

/**
 * qcom_tzmem_to_phys() - Map the virtual address of TZ memory to physical.
 * @vaddr: Virtual address of memory allocated from a TZ memory pool.
 *
 * Can be used in any context. The address must point to memory allocated
 * using qcom_tzmem_alloc().
 *
 * Returns:
 * Physical address mapped from the virtual or 0 if the mapping failed.
 */
phys_addr_t qcom_tzmem_to_phys(void *vaddr)
{
	struct qcom_tzmem_chunk *chunk;
	struct radix_tree_iter iter;
	void __rcu **slot;
	phys_addr_t ret;

	guard(spinlock_irqsave)(&qcom_tzmem_chunks_lock);

	radix_tree_for_each_slot(slot, &qcom_tzmem_chunks, &iter, 0) {
		chunk = radix_tree_deref_slot_protected(slot,
						&qcom_tzmem_chunks_lock);

		ret = gen_pool_virt_to_phys(chunk->owner->genpool,
					    (unsigned long)vaddr);
		if (ret == -1)
			continue;

		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_tzmem_to_phys);

int qcom_tzmem_enable(struct device *dev)
{
	if (qcom_tzmem_dev)
		return -EBUSY;

	qcom_tzmem_dev = dev;

	return qcom_tzmem_init();
}
EXPORT_SYMBOL_GPL(qcom_tzmem_enable);

MODULE_DESCRIPTION("TrustZone memory allocator for Qualcomm firmware drivers");
MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_LICENSE("GPL");
