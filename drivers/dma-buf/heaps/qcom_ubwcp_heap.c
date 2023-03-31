// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/ubwcp_dma_heap.h>
#include <trace/hooks/dmabuf.h>
#include <linux/msm_dma_iommu_mapping.h>

#include <linux/qcom-dma-mapping.h>
#include "qcom_system_heap.h"

static struct dma_heap *sys_heap;

struct ubwcp_driver_ops {
	init_buffer init_buffer;
	free_buffer free_buffer;
	lock_buffer lock_buffer;
	unlock_buffer unlock_buffer;
} ubwcp_driver_ops;

struct ubwcp_buffer {
	struct qcom_sg_buffer qcom_sg_buf;
	bool ubwcp_init_complete;

	struct rw_semaphore linear_mode_sem;
	bool is_linear;
	atomic_t cpu_map_count;
	phys_addr_t ula_pa_addr;
	size_t ula_pa_size;
};

struct qcom_ubwcp_heap {
	bool movable;
};

static int ubwcp_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					  enum dma_data_direction direction)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);
	int ret;

	down_read(&buffer->linear_mode_sem);

	if (!buffer->is_linear)
		ret = ubwcp_driver_ops.lock_buffer(dmabuf, direction);
	else
		ret = qcom_sg_dma_buf_begin_cpu_access(dmabuf, direction);

	up_read(&buffer->linear_mode_sem);

	return ret;
}

static int ubwcp_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);
	int ret;

	down_read(&buffer->linear_mode_sem);

	if (!buffer->is_linear)
		ret = ubwcp_driver_ops.unlock_buffer(dmabuf, direction);
	else
		ret = qcom_sg_dma_buf_end_cpu_access(dmabuf, direction);

	up_read(&buffer->linear_mode_sem);

	return ret;
}

static int ubwcp_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
						  enum dma_data_direction direction,
						  unsigned int offset,
						  unsigned int len)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);
	int ret;

	down_read(&buffer->linear_mode_sem);
	if (!buffer->is_linear) {
		pr_err("%s: isn't in linear mode, bailing\n", __func__);
		ret = -EINVAL;
	} else {
		ret = qcom_sg_dma_buf_begin_cpu_access_partial(dmabuf, direction, offset,
							       len);
	}
	up_read(&buffer->linear_mode_sem);

	return ret;
}

static int ubwcp_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
						enum dma_data_direction direction,
						unsigned int offset,
						unsigned int len)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);
	int ret;

	down_read(&buffer->linear_mode_sem);
	if (!buffer->is_linear) {
		pr_err("%s: isn't in linear mode, bailing\n", __func__);
		ret = -EINVAL;
	} else {
		ret = qcom_sg_dma_buf_end_cpu_access_partial(dmabuf, direction, offset,
							     len);
	}
	up_read(&buffer->linear_mode_sem);

	return ret;
}

static void qcom_sg_vm_ops_open(struct vm_area_struct *vma)
{
	struct ubwcp_buffer *buffer = vma->vm_private_data;

	atomic_inc(&buffer->cpu_map_count);
	mem_buf_vmperm_pin(buffer->qcom_sg_buf.vmperm);
}

static void qcom_sg_vm_ops_close(struct vm_area_struct *vma)
{
	struct ubwcp_buffer *buffer = vma->vm_private_data;

	atomic_dec(&buffer->cpu_map_count);
	mem_buf_vmperm_unpin(buffer->qcom_sg_buf.vmperm);
}

static const struct vm_operations_struct qcom_sg_vm_ops = {
	.open = qcom_sg_vm_ops_open,
	.close = qcom_sg_vm_ops_close,
};

static int ubwcp_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);

	unsigned long vaddr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	unsigned long map_len = vma->vm_end - vma->vm_start;
	int ret = 0;

	down_read(&buffer->linear_mode_sem);
	if (buffer->is_linear) {
		ret = qcom_sg_mmap(dmabuf, vma);
		goto unlock;
	}

	if (map_len + offset > buffer->ula_pa_size) {
		pr_err("mmap is too large!\n");
		ret = -EINVAL;
		goto unlock;
	}

	mem_buf_vmperm_pin(buffer->qcom_sg_buf.vmperm);
	if (!mem_buf_vmperm_can_mmap(buffer->qcom_sg_buf.vmperm, vma)) {
		mem_buf_vmperm_unpin(buffer->qcom_sg_buf.vmperm);
		ret = -EPERM;
		goto unlock;
	}

	vma->vm_ops = &qcom_sg_vm_ops;
	vma->vm_private_data = buffer;

	ret = remap_pfn_range(vma, vaddr,
			      (buffer->ula_pa_addr >> PAGE_SHIFT) + offset,
			      map_len, vma->vm_page_prot);
	if (ret) {
		mem_buf_vmperm_unpin(buffer->qcom_sg_buf.vmperm);
		goto unlock;
	}

	atomic_inc(&buffer->cpu_map_count);
unlock:
	up_read(&buffer->linear_mode_sem);

	return ret;
}

static int ubwcp_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);
	int ret;

	down_read(&buffer->linear_mode_sem);
	if (!buffer->is_linear) {
		pr_err("%s: isn't in linear mode, bailing\n", __func__);
		ret = -EINVAL;
		goto unlock;
	}

	ret = qcom_sg_vmap(dmabuf, map);
	if (ret)
		goto unlock;

	atomic_inc(&buffer->cpu_map_count);
unlock:
	up_read(&buffer->linear_mode_sem);

	return ret;
}

static void ubwcp_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);

	down_read(&buffer->linear_mode_sem);

	if (!buffer->is_linear)
		pr_err("%s: isn't in linear mode, bailing\n", __func__);
	else
		qcom_sg_vunmap(dmabuf, map);

	WARN_ON(atomic_read(&buffer->cpu_map_count) <= 0);
	atomic_dec(&buffer->cpu_map_count);
	up_read(&buffer->linear_mode_sem);
}

static void ubwcp_release(struct dma_buf *dmabuf)
{
	int ret;
	struct ubwcp_buffer *buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);

	if (buffer->ubwcp_init_complete) {
		ret = ubwcp_driver_ops.free_buffer(dmabuf);
		if (ret) {
			pr_err("%s: UBWC-P buffer not freed, err: %d\n", __func__, ret);
			return;
		}
	}

	ret = mem_buf_vmperm_release(buffer->qcom_sg_buf.vmperm);
	if (ret) {
		pr_err("%s: Failed to release vmperm, err: %d\n", __func__, ret);
		return;
	}

	msm_dma_buf_freed(dmabuf->priv);
	qcom_system_heap_free(&buffer->qcom_sg_buf);
}

struct mem_buf_dma_buf_ops ubwcp_ops = {
	.attach = qcom_sg_attach,
	.lookup = qcom_sg_lookup_vmperm,
	.dma_ops = {
		.attach = NULL, /* Will be set by mem_buf_dma_buf_export */
		.detach = qcom_sg_detach,
		.map_dma_buf = qcom_sg_map_dma_buf,
		.unmap_dma_buf = qcom_sg_unmap_dma_buf,
		.begin_cpu_access = ubwcp_dma_buf_begin_cpu_access,
		.end_cpu_access = ubwcp_dma_buf_end_cpu_access,
		.begin_cpu_access_partial = ubwcp_dma_buf_begin_cpu_access_partial,
		.end_cpu_access_partial = ubwcp_dma_buf_end_cpu_access_partial,
		.mmap = ubwcp_mmap,
		.vmap = ubwcp_vmap,
		.vunmap = ubwcp_vunmap,
		.release = ubwcp_release,
	}
};

int msm_ubwcp_dma_buf_configure_mmap(struct dma_buf *dmabuf, bool linear,
				     phys_addr_t ula_pa_addr,
				     size_t ula_pa_size)
{
	struct ubwcp_buffer *buffer;
	int ret = 0;

	if (dmabuf->ops != &ubwcp_ops.dma_ops) {
		pr_err("%s: User didn't pass in DMA-BUF!\n", __func__);
		return -EINVAL;
	}

	if (ula_pa_addr % PAGE_SIZE || ula_pa_size % PAGE_SIZE) {
		pr_err("%s: ULA PA addr and ULA PA map size must be page_aligned!\n",
		       __func__);
		return -EINVAL;
	}

	buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);

	down_write(&buffer->linear_mode_sem);
	if (atomic_read(&buffer->cpu_map_count)) {
		pr_err("%s: Buffer already mapped!\n", __func__);
		ret = -EINVAL;
		goto unlock;
	}

	buffer->is_linear = linear;
	buffer->ula_pa_addr = ula_pa_addr;
	buffer->ula_pa_size = ula_pa_size;
unlock:
	up_write(&buffer->linear_mode_sem);

	return ret;
}
EXPORT_SYMBOL(msm_ubwcp_dma_buf_configure_mmap);

static struct dma_buf *ubwcp_allocate(struct dma_heap *heap,
				      unsigned long len,
				      unsigned long fd_flags,
				      unsigned long heap_flags)
{
	struct ubwcp_buffer *buffer;
	struct qcom_ubwcp_heap *ubwcp_heap;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;

	ubwcp_heap = dma_heap_get_drvdata(heap);

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	init_rwsem(&buffer->linear_mode_sem);

	ret = system_qcom_sg_buffer_alloc(sys_heap, &buffer->qcom_sg_buf, len, ubwcp_heap->movable);
	if (ret)
		goto free_buf_struct;

	buffer->qcom_sg_buf.vmperm = mem_buf_vmperm_alloc(&buffer->qcom_sg_buf.sg_table);
	if (IS_ERR(buffer->qcom_sg_buf.vmperm)) {
		ret = PTR_ERR(buffer->qcom_sg_buf.vmperm);
		goto free_sys_heap_mem;
	}

	/* Make the buffer linear by default */
	buffer->is_linear = true;

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.size = buffer->qcom_sg_buf.len;
	exp_info.flags = fd_flags;
	exp_info.priv = &buffer->qcom_sg_buf;
	dmabuf = qcom_dma_buf_export(&exp_info, &ubwcp_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_vmperm;
	}

	ret = ubwcp_driver_ops.init_buffer(dmabuf);
	if (ret)
		goto buf_release;
	buffer->ubwcp_init_complete = true;

	return dmabuf;

buf_release:
	dma_buf_put(dmabuf);
	return ERR_PTR(ret);

free_vmperm:
	mem_buf_vmperm_release(buffer->qcom_sg_buf.vmperm);
free_sys_heap_mem:
	qcom_system_heap_free(&buffer->qcom_sg_buf);
	return ERR_PTR(ret);
free_buf_struct:
	kfree(buffer);

	return ERR_PTR(ret);
}

static const struct dma_heap_ops ubwcp_heap_ops = {
	.allocate = ubwcp_allocate,
};

static void ignore_vmap_bounds_check(void *unused, struct dma_buf *dmabuf, bool *result)
{
	struct ubwcp_buffer *buffer;

	if (dmabuf->ops != &ubwcp_ops.dma_ops) {
		*result = false;
		return;
	}

	buffer = container_of(dmabuf->priv, struct ubwcp_buffer, qcom_sg_buf);

	if (buffer->is_linear)
		*result = false;
	else
		*result = true;
}

int qcom_ubwcp_heap_create(char *name, bool movable)
{
	struct dma_heap_export_info exp_info;
	struct dma_heap *heap;
	struct qcom_ubwcp_heap *ubwcp_heap;
	static bool vmap_registered;
	int ret;

	/* This function should only be called once */
	if (!vmap_registered) {
		ret = register_trace_android_vh_ignore_dmabuf_vmap_bounds(ignore_vmap_bounds_check,
								  NULL);
		if (ret) {
			pr_err("%s: Unable to register vmap bounds tracehook\n", __func__);
			goto out;
		}
		vmap_registered = true;
	}

	sys_heap = dma_heap_find("qcom,system");
	if (!sys_heap) {
		pr_err("%s: Unable to find 'qcom,system'\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	ubwcp_heap = kzalloc(sizeof(*ubwcp_heap), GFP_KERNEL);
	if (!ubwcp_heap) {
		ret = -ENOMEM;
		goto ubwcp_alloc_free;
	}
	ubwcp_heap->movable = movable;

	exp_info.name = name;
	exp_info.ops = &ubwcp_heap_ops;
	exp_info.priv = ubwcp_heap;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		ret = PTR_ERR(heap);
		goto ubwcp_alloc_free;
	}

	pr_info("%s: DMA-BUF Heap: Created '%s'\n", __func__, name);

	return 0;

ubwcp_alloc_free:
	kfree(ubwcp_heap);
out:
	pr_err("%s: Failed to create '%s', error is %d\n", __func__, name, ret);

	return ret;
}

int msm_ubwcp_set_ops(init_buffer init_buf_fn_ptr,
		      free_buffer free_buf_fn_ptr,
		      lock_buffer lock_buf_fn_ptr,
		      unlock_buffer unlock_buf_fn_ptr)
{
	int ret = 0;
	if (!init_buf_fn_ptr || !free_buf_fn_ptr || !lock_buf_fn_ptr ||
	    !unlock_buf_fn_ptr) {
		pr_err("%s: Missing function pointer\n", __func__);
		return -EINVAL;
	}

	ubwcp_driver_ops.init_buffer = init_buf_fn_ptr;
	ubwcp_driver_ops.free_buffer = free_buf_fn_ptr;
	ubwcp_driver_ops.lock_buffer = lock_buf_fn_ptr;
	ubwcp_driver_ops.unlock_buffer = unlock_buf_fn_ptr;

	ret = qcom_ubwcp_heap_create("qcom,ubwcp", false);
	if (ret)
		return ret;

#ifdef CONFIG_QCOM_DMABUF_HEAPS_UBWCP_MOVABLE
	ret = qcom_ubwcp_heap_create("qcom,ubwcp-movable", true);
#endif
	return ret;
}
EXPORT_SYMBOL(msm_ubwcp_set_ops);

MODULE_IMPORT_NS(DMA_BUF);
