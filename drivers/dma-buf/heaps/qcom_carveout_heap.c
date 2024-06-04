// SPDX-License-Identifier: GPL-2.0
/*
 * DMA-BUF heap carveout heap allocator. Copied from
 * drivers/staging/android/ion/heaps/ion_carveout_heap.c as of commit
 * aeb022cc01ecc ("dma-heap: qcom: Change symbol names to let module be built
 * in")
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/qcom_dma_heap.h>
#include <linux/types.h>

#include "qcom_dma_heap_secure_utils.h"
#include "qcom_sg_ops.h"
#include "qcom_carveout_heap.h"

#define CARVEOUT_ALLOCATE_FAIL -1

static LIST_HEAD(secure_carveout_heaps);


/*
 * @pool_refcount_priv -
 *	Cookie set by carveout_heap_add_memory for use with its callbacks.
 *	Cookie provider will call carveout_heap_remove_memory if refcount
 *	reaches zero.
 * @pool_refcount_get -
 *	Function callback to Increase refcount. Returns 0
 *	on success and fails if refcount is already zero.
 * @pool_refcount_put - Function callback to decrease refcount.
 */
struct carveout_heap {
	struct dma_heap *heap;
	struct rw_semaphore mem_sem;
	struct gen_pool *pool;
	struct device *dev;
	bool is_secure;
	phys_addr_t base;
	ssize_t size;
};

struct secure_carveout_heap {
	u32 token;
	bool is_unmapped;
	struct carveout_heap carveout_heap;
	struct list_head list;
	atomic_long_t total_allocated;
};

static void sc_heap_free(struct qcom_sg_buffer *buffer);

void __maybe_unused pages_sync_for_device(struct device *dev, struct page *page,
			       size_t size, enum dma_data_direction dir)
{
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, page, size, 0);
	/*
	 * This is not correct - sg_dma_address needs a dma_addr_t that is valid
	 * for the targeted device, but this works on the currently targeted
	 * hardware.
	 */
	sg_dma_address(&sg) = page_to_phys(page);
	dma_sync_sg_for_device(dev, &sg, 1, dir);
}

static phys_addr_t carveout_allocate(struct carveout_heap *carveout_heap,
				     unsigned long size)
{
	unsigned long offset = CARVEOUT_ALLOCATE_FAIL;

	down_read(&carveout_heap->mem_sem);
	if (carveout_heap->pool) {
		offset = gen_pool_alloc(carveout_heap->pool, size);
		if (!offset) {
			offset = CARVEOUT_ALLOCATE_FAIL;
			goto unlock;
		}
	}

unlock:
	up_read(&carveout_heap->mem_sem);
	return offset;
}

static void carveout_free(struct carveout_heap *carveout_heap,
			  phys_addr_t addr, unsigned long size)
{
	if (addr == CARVEOUT_ALLOCATE_FAIL)
		return;

	down_read(&carveout_heap->mem_sem);
	if (carveout_heap->pool)
		gen_pool_free(carveout_heap->pool, addr, size);
	up_read(&carveout_heap->mem_sem);
}

struct mem_buf_vmperm *
carveout_setup_vmperm(struct carveout_heap *carveout_heap,
			struct sg_table *sgt)
{
	struct secure_carveout_heap *sc_heap;
	struct mem_buf_vmperm *vmperm;
	int *vmids, *perms;
	u32 nr;
	int ret;

	if (!carveout_heap->is_secure) {
		vmperm = mem_buf_vmperm_alloc(sgt);
		return vmperm;
	}

	sc_heap = container_of(carveout_heap,
			struct secure_carveout_heap, carveout_heap);

	ret = get_vmperm_from_ion_flags(sc_heap->token,
			&vmids, &perms, &nr);
	if (ret)
		return ERR_PTR(ret);

	vmperm = mem_buf_vmperm_alloc_staticvm(sgt, vmids, perms, nr);
	kfree(vmids);
	kfree(perms);

	return vmperm;
}

static struct dma_buf *__carveout_heap_allocate(struct carveout_heap *carveout_heap,
						unsigned long len,
						unsigned long fd_flags,
						unsigned long heap_flags,
						void (*buffer_free)(struct qcom_sg_buffer *))
{
	struct sg_table *table;
	struct qcom_sg_buffer *buffer;
	phys_addr_t paddr;
	int ret;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	/* Initialize the buffer */
	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = carveout_heap->heap;
	buffer->len = len;
	buffer->free = buffer_free;
	buffer->uncached = true;

	table = &buffer->sg_table;
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto err_free;

	paddr = carveout_allocate(carveout_heap, len);
	if (paddr == CARVEOUT_ALLOCATE_FAIL) {
		ret = -ENOMEM;
		goto err_free_table;
	}

	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(paddr)), len, 0);

	buffer->vmperm = carveout_setup_vmperm(carveout_heap, &buffer->sg_table);
	if (IS_ERR(buffer->vmperm))
		goto err_free_carveout;


	/* Instantiate our dma_buf */
	exp_info.exp_name = dma_heap_get_name(carveout_heap->heap);
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = qcom_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_free_vmperm;
	}

	return dmabuf;

err_free_vmperm:
	mem_buf_vmperm_release(buffer->vmperm);
err_free_carveout:
	carveout_free(carveout_heap, paddr, len);
err_free_table:
	sg_free_table(table);
err_free:
	kfree(buffer);
	return ERR_PTR(ret);
}

static int carveout_pages_zero(struct page *page, size_t size);

static void carveout_heap_free(struct qcom_sg_buffer *buffer)
{
	struct carveout_heap *carveout_heap;
	struct sg_table *table = &buffer->sg_table;
	struct page *page = sg_page(table->sgl);
	phys_addr_t paddr = page_to_phys(page);
	struct device *dev;

	carveout_heap = dma_heap_get_drvdata(buffer->heap);

	dev = carveout_heap->dev;

	carveout_pages_zero(page, buffer->len);
	carveout_free(carveout_heap, paddr, buffer->len);
	sg_free_table(table);
	kfree(buffer);
}


static struct dma_buf *carveout_heap_allocate(struct dma_heap *heap,
					      unsigned long len,
					      unsigned long fd_flags,
					      unsigned long heap_flags)
{
	struct carveout_heap *carveout_heap = dma_heap_get_drvdata(heap);

	return __carveout_heap_allocate(carveout_heap, len, fd_flags,
					heap_flags, carveout_heap_free);
}

static int carveout_pages_zero(struct page *page, size_t size)
{
	void __iomem *addr;

	addr = ioremap_wc(page_to_phys(page), size);
	if (!addr)
		return -ENOMEM;
	memset(addr, 0, size);
	iounmap(addr);

	return 0;
}

static int carveout_init_heap_memory(struct carveout_heap *co_heap,
				     phys_addr_t base, ssize_t size,
				     bool is_unmapped)
{
	struct page *page = pfn_to_page(PFN_DOWN(base));
	int ret = 0;

	if (!is_unmapped) {
		ret = carveout_pages_zero(page, size);
		if (ret)
			return ret;
	}

	co_heap->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!co_heap->pool)
		return -ENOMEM;

	co_heap->base = base;
	co_heap->size = size;
	gen_pool_add(co_heap->pool, co_heap->base, size, -1);

	return 0;
}

static int __carveout_heap_init(struct platform_heap *heap_data,
				struct carveout_heap *carveout_heap,
				bool is_unmapped)
{
	struct device *dev = heap_data->dev;
	int ret = 0;

	carveout_heap->dev = dev;
	ret = carveout_init_heap_memory(carveout_heap,
					heap_data->base,
					heap_data->size, is_unmapped);

	init_rwsem(&carveout_heap->mem_sem);

	return ret;
}

static const struct dma_heap_ops carveout_heap_ops = {
	.allocate = carveout_heap_allocate,
};

static void carveout_heap_destroy(struct carveout_heap *heap);

int qcom_carveout_heap_create(struct platform_heap *heap_data)
{
	struct dma_heap_export_info exp_info;
	struct carveout_heap *carveout_heap;
	int ret;

	if (!heap_data->is_nomap) {
		pr_err("carveout heap memory regions need to be created with no-map\n");
		return -EINVAL;
	}

	carveout_heap = kzalloc(sizeof(*carveout_heap), GFP_KERNEL);
	if (!carveout_heap)
		return -ENOMEM;

	ret = __carveout_heap_init(heap_data, carveout_heap, false);
	if (ret)
		goto err;

	carveout_heap->is_secure = false;

	exp_info.name = heap_data->name;
	exp_info.ops = &carveout_heap_ops;
	exp_info.priv = carveout_heap;

	carveout_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(carveout_heap->heap)) {
		ret = PTR_ERR(carveout_heap->heap);
		goto destroy_heap;
	}

	return 0;

destroy_heap:
	carveout_heap_destroy(carveout_heap);
err:
	kfree(carveout_heap);

	return ret;
}

static void carveout_heap_destroy(struct carveout_heap *carveout_heap)
{
	down_write(&carveout_heap->mem_sem);
	if (carveout_heap->pool)
		gen_pool_destroy(carveout_heap->pool);
	up_write(&carveout_heap->mem_sem);
	carveout_heap = NULL;
}

static struct dma_buf *sc_heap_allocate(struct dma_heap *heap,
					unsigned long len,
					unsigned long fd_flags,
					unsigned long heap_flags)
{
	struct secure_carveout_heap *sc_heap;
	struct dma_buf *dbuf;

	sc_heap = dma_heap_get_drvdata(heap);
	dbuf = __carveout_heap_allocate(&sc_heap->carveout_heap, len,
					 fd_flags, heap_flags, sc_heap_free);
	if (IS_ERR(dbuf))
		return dbuf;
	atomic_long_add(len, &sc_heap->total_allocated);

	return dbuf;
}

static void sc_heap_free(struct qcom_sg_buffer *buffer)
{
	struct secure_carveout_heap *sc_heap;
	struct sg_table *table = &buffer->sg_table;
	struct page *page = sg_page(table->sgl);
	phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	sc_heap = dma_heap_get_drvdata(buffer->heap);

	if (qcom_is_buffer_hlos_accessible(sc_heap->token))
		carveout_pages_zero(page, buffer->len);
	carveout_free(&sc_heap->carveout_heap, paddr, buffer->len);
	sg_free_table(table);
	atomic_long_sub(buffer->len, &sc_heap->total_allocated);
	kfree(buffer);
}

int qcom_secure_carveout_heap_freeze(void)
{
	long sz;
	struct secure_carveout_heap *sc_heap;

	/*
	 * It is expected that the buffers are freed by the clients
	 * before the freeze. DMABUF framework tracks the unfreed memory
	 * by the total_allocated struct member.
	 */
	list_for_each_entry(sc_heap, &secure_carveout_heaps, list) {
		sz = atomic_long_read(&sc_heap->total_allocated);
		if (sz) {
			pr_err("%s: %s: %lx bytes of allocations not freed. Aborting freeze\n",
				__func__,
				dma_heap_get_name(sc_heap->carveout_heap.heap),
				sz);
			return -EBUSY;
		}
	}
	return 0;
}

int qcom_secure_carveout_heap_restore(void)
{
	struct secure_carveout_heap *sc_heap;
	int ret;

	list_for_each_entry(sc_heap, &secure_carveout_heaps, list) {
		ret = hyp_assign_from_flags(sc_heap->carveout_heap.base,
				sc_heap->carveout_heap.size,
				sc_heap->token);
		BUG_ON(ret);
	}
	return 0;
}

static struct dma_heap_ops sc_heap_ops = {
	.allocate = sc_heap_allocate,
};

static bool qcom_secure_carveout_is_unmapped(struct device *dev)
{
	struct device_node *mem_region;
	bool val = false;

	mem_region = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!mem_region)
		goto err;

	val = of_property_read_bool(mem_region, "qcom,unmapped");
err:
	of_node_put(mem_region);
	return val;
}


int qcom_secure_carveout_heap_create(struct platform_heap *heap_data)
{
	struct dma_heap_export_info exp_info;
	struct secure_carveout_heap *sc_heap;
	int ret;

	if (!heap_data->is_nomap) {
		pr_err("secure carveout heap memory regions need to be created with no-map\n");
		return -EINVAL;
	}

	sc_heap = kzalloc(sizeof(*sc_heap), GFP_KERNEL);
	if (!sc_heap)
		return -ENOMEM;

	sc_heap->is_unmapped = qcom_secure_carveout_is_unmapped(heap_data->dev);

	ret = __carveout_heap_init(heap_data, &sc_heap->carveout_heap, sc_heap->is_unmapped);
	if (ret)
		goto err;

	if (!sc_heap->is_unmapped) {
		ret = hyp_assign_from_flags(heap_data->base, heap_data->size,
				    heap_data->token);
		if (ret) {
			pr_err("secure_carveout_heap: Assign token 0x%x failed\n",
				heap_data->token);
			goto destroy_heap;
		}
	}

	sc_heap->token = heap_data->token;
	sc_heap->carveout_heap.is_secure = true;

	exp_info.name = heap_data->name;
	exp_info.ops = &sc_heap_ops;
	exp_info.priv = sc_heap;

	sc_heap->carveout_heap.heap = dma_heap_add(&exp_info);
	if (IS_ERR(sc_heap->carveout_heap.heap)) {
		ret = PTR_ERR(sc_heap->carveout_heap.heap);
		goto destroy_heap;
	}

	list_add(&sc_heap->list, &secure_carveout_heaps);
	return 0;

destroy_heap:
	carveout_heap_destroy(&sc_heap->carveout_heap);
err:
	kfree(sc_heap);

	return ret;
}
