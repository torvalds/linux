// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "tui_heap: %s"  fmt, __func__

#include <linux/genalloc.h>
#include <linux/dma-heap.h>
#include <linux/slab.h>
#include <linux/mem-buf.h>
#include <linux/anon_inodes.h>
#include <linux/kref.h>
#include <linux/qcom_tui_heap.h>
#include "qcom_dt_parser.h"
#include "qcom_sg_ops.h"

struct tui_heap;
struct tui_pool {
	struct tui_heap *heap;
	void *membuf;
	struct kref kref;
	struct gen_pool *pool;
	struct file *filp;
};

struct tui_heap_obj {
	struct qcom_sg_buffer buffer;
	struct tui_pool *pool;
};

struct tui_heap {
	struct dma_heap *heap;
	/* Protects the pool pointer, not the pool itself */
	struct rw_semaphore pool_sem;
	struct tui_pool *pool;
};
#define CARVEOUT_ALLOCATE_FAIL -1

static struct tui_pool *tui_pool_create(struct mem_buf_allocation_data *alloc_data)
{
	struct tui_pool *pool;
	struct gh_sgl_desc *sgl_desc;
	phys_addr_t base;
	size_t size;
	int ret;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->membuf = mem_buf_alloc(alloc_data);
	if (IS_ERR(pool->membuf)) {
		ret = PTR_ERR(pool->membuf);
		goto err_mem_buf_alloc;
	}
	sgl_desc = mem_buf_get_sgl(pool->membuf);

	ret = mem_buf_map_mem_s1(sgl_desc);
	if (ret)
		goto err_mem_buf_map_mem_s1;

	kref_init(&pool->kref);
	pool->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!pool->pool) {
		ret = -ENOMEM;
		goto err_gen_pool_create;
	}

	base = sgl_desc->sgl_entries[0].ipa_base;
	size = sgl_desc->sgl_entries[0].size;
	ret = gen_pool_add(pool->pool, base, size, -1);
	if (ret)
		goto err_gen_pool_add;

	return pool;

err_gen_pool_add:
	gen_pool_destroy(pool->pool);
err_gen_pool_create:
	if (WARN_ON(mem_buf_unmap_mem_s1(sgl_desc)))
		return ERR_PTR(ret);
err_mem_buf_map_mem_s1:
	mem_buf_free(pool->membuf);
err_mem_buf_alloc:
	kfree(pool);
	return ERR_PTR(ret);
}

static void tui_pool_release(struct kref *kref)
{
	struct tui_pool *pool;
	struct gh_sgl_desc *sgl_desc;

	pool = container_of(kref, struct tui_pool, kref);

	gen_pool_destroy(pool->pool);
	sgl_desc = mem_buf_get_sgl(pool->membuf);
	if (WARN_ON(mem_buf_unmap_mem_s1(sgl_desc)))
		return;
	mem_buf_free(pool->membuf);
	kfree(pool);
}

/* Prevent new allocations from this pool, and drop our refcount */
static void qcom_tui_heap_remove_pool(void *handle)
{
	struct tui_pool *pool = handle;
	struct tui_heap *heap = pool->heap;

	down_write(&heap->pool_sem);
	heap->pool = NULL;
	pool->heap = NULL;
	kref_put(&pool->kref, tui_pool_release);
	up_write(&heap->pool_sem);
}

static void *qcom_tui_heap_add_pool(struct mem_buf_allocation_data *alloc_data)
{
	struct dma_heap *dma_heap;
	struct tui_heap *heap;
	struct tui_pool *pool;
	char *heap_name;

	if (alloc_data->dst_mem_type != MEM_BUF_DMAHEAP_MEM_TYPE)
		return ERR_PTR(-EINVAL);

	heap_name = alloc_data->dst_data;
	dma_heap = dma_heap_find(heap_name);
	if (!dma_heap) {
		pr_err_ratelimited("No heap named %s\n", heap_name);
		return ERR_PTR(-EINVAL);
	}
	heap = dma_heap_get_drvdata(dma_heap);

	/*
	 * Subsection alignment is a requirement of SPARSEMEM_VMEMMAP.
	 * refer to check_pfn_span(). This is the minimum granularity
	 * pfn_valid() and pfn_to_online_page() operate at.
	 */
	alloc_data->size = ALIGN(alloc_data->size, 1UL << SUBSECTION_SHIFT);
	alloc_data->trans_type = GH_RM_TRANS_TYPE_DONATE;
	alloc_data->sgl_desc = NULL;
	pool = tui_pool_create(alloc_data);
	if (IS_ERR(pool))
		return ERR_CAST(pool);

	down_write(&heap->pool_sem);
	if (heap->pool) {
		pr_err_ratelimited("%s already has a pool\n", heap_name);
		kref_put(&pool->kref, tui_pool_release);
		pool = ERR_PTR(-EBUSY);
	} else {
		pool->heap = heap;
		heap->pool = pool;
	}
	up_write(&heap->pool_sem);

	return pool;
}

/*
 * Transfer the requested amount of memory from primary VM to the given heap
 * int the current VM.
 * Returns a handle which can be released via qcom_tvm_heap_free_kernel_pool.
 *
 * The current VM allocates ~1/64 of the requested size for 'struct page'
 * array and other metadata.
 */
void *qcom_tvm_heap_add_kernel_pool(struct dma_heap *heap, size_t size)
{
	struct mem_buf_allocation_data args = {};
	int vmids[1];
	int perms[1];

	vmids[0] = mem_buf_current_vmid();
	perms[0] = PERM_READ | PERM_WRITE;

	args.size = size;
	args.nr_acl_entries = ARRAY_SIZE(vmids);
	args.vmids = vmids;
	args.perms = perms;
	args.trans_type = GH_RM_TRANS_TYPE_LEND;
	args.sgl_desc = NULL;
	args.src_mem_type = MEM_BUF_DMAHEAP_MEM_TYPE;
	args.src_data = "qcom,system";
	args.dst_mem_type = MEM_BUF_DMAHEAP_MEM_TYPE;
	args.dst_data = (void *)dma_heap_get_name(heap);

	return qcom_tui_heap_add_pool(&args);
}
EXPORT_SYMBOL_GPL(qcom_tvm_heap_add_kernel_pool);

/*
 * Releases a handle created by qcom_tvm_heap_add_kernel_pool.
 */
void qcom_tvm_heap_remove_kernel_pool(void *handle)
{
	qcom_tui_heap_remove_pool(handle);
}
EXPORT_SYMBOL_GPL(qcom_tvm_heap_remove_kernel_pool);

static int tui_heap_file_release(struct inode *inode, struct file *filp)
{
	qcom_tui_heap_remove_pool(filp->private_data);
	return 0;
}

static const struct file_operations tui_heap_fops = {
	.release = tui_heap_file_release,
};

int qcom_tui_heap_add_pool_fd(struct mem_buf_allocation_data *alloc_data)
{
	struct tui_pool *pool;
	int fd;

	/*
	 * Tui heap specific ioctl parsing should move here eventually.
	 */

	pool = qcom_tui_heap_add_pool(alloc_data);
	if (IS_ERR(pool))
		return PTR_ERR(pool);

	pool->filp = anon_inode_getfile("tui_heap", &tui_heap_fops, pool, O_RDWR);
	if (IS_ERR(pool->filp)) {
		int ret = PTR_ERR(pool->filp);

		qcom_tui_heap_remove_pool(pool);
		return ret;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		fput(pool->filp);
		return fd;
	}

	fd_install(fd, pool->filp);
	return fd;
}

static void tui_heap_obj_release(struct qcom_sg_buffer *buffer)
{
	struct tui_heap_obj *obj = container_of(buffer, struct tui_heap_obj, buffer);
	struct sg_table *table = &buffer->sg_table;

	/* Prevent data from previous user from leaking to next user */
	memset(sg_virt(table->sgl), 0, buffer->len);
	gen_pool_free(obj->pool->pool, sg_phys(table->sgl), buffer->len);
	kref_put(&obj->pool->kref, tui_pool_release);
	sg_free_table(table);
	kfree(obj);
}

static struct dma_buf *tui_heap_allocate(struct dma_heap *dma_heap,
						unsigned long len,
						unsigned long fd_flags,
						unsigned long heap_flags)
{
	struct tui_heap *heap = dma_heap_get_drvdata(dma_heap);
	struct tui_pool *pool;
	struct sg_table *table;
	struct tui_heap_obj *obj;
	struct qcom_sg_buffer *buffer;
	u64 paddr;
	int ret;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;

	len = PAGE_ALIGN(len);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);
	buffer = &obj->buffer;

	down_read(&heap->pool_sem);
	pool = heap->pool;
	if (!pool) {
		pr_err_ratelimited("%s does not have a pool\n",
				dma_heap_get_name(heap->heap));
		ret = -ENODEV;
		goto err_no_pool;
	}

	obj->pool = pool;
	kref_get(&pool->kref);

	/* Returns 0 on failure */
	paddr = gen_pool_alloc(pool->pool, len);
	if (!paddr) {
		ret = -ENOMEM;
		goto err_tui_pool_alloc;
	}

	/* Initialize the buffer */
	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = heap->heap;
	buffer->len = len;
	buffer->free = tui_heap_obj_release;
	buffer->uncached = false;

	table = &buffer->sg_table;
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc_table;
	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(paddr)), len, 0);

	buffer->vmperm = mem_buf_vmperm_alloc(table);
	if (IS_ERR(buffer->vmperm)) {
		ret = PTR_ERR(buffer->vmperm);
		goto err_vmperm_alloc;
	}

	/* Instantiate our dma_buf */
	exp_info.exp_name = dma_heap_get_name(heap->heap);
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = qcom_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_export;
	}

	up_read(&heap->pool_sem);
	return dmabuf;

err_export:
	mem_buf_vmperm_release(buffer->vmperm);
err_vmperm_alloc:
	sg_free_table(table);
err_sg_alloc_table:
	gen_pool_free(pool->pool, paddr, len);
err_tui_pool_alloc:
	kref_put(&pool->kref, tui_pool_release);
err_no_pool:
	up_read(&heap->pool_sem);
	kfree(obj);
	return ERR_PTR(ret);
}

static const struct dma_heap_ops tui_heap_ops = {
	.allocate = tui_heap_allocate,
};

int qcom_tui_carveout_heap_create(struct platform_heap *heap_data)
{
	struct dma_heap_export_info exp_info;
	struct tui_heap *heap;
	int ret;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return -ENOMEM;

	init_rwsem(&heap->pool_sem);
	exp_info.name = heap_data->name;
	exp_info.ops = &tui_heap_ops;
	exp_info.priv = heap;

	heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap->heap)) {
		ret = PTR_ERR(heap->heap);
		goto err_dma_heap_add;
	}

	return 0;

err_dma_heap_add:
	kfree(heap);
	return ret;
}
