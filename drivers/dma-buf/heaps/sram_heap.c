// SPDX-License-Identifier: GPL-2.0
/*
 * SRAM DMA-Heap exporter && support alloc page and dmabuf on kernel
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Author: Andrew F. Davis <afd@ti.com>
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */
#define pr_fmt(fmt) "sram_heap: " fmt

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>

#include <linux/sram_heap.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define RK3588_SRAM_BASE 0xff001000

struct sram_dma_heap {
	struct dma_heap *heap;
	struct gen_pool *pool;
};

struct sram_dma_heap_buffer {
	struct gen_pool *pool;
	struct list_head attachments;
	struct mutex attachments_lock;
	unsigned long len;
	void *vaddr;
	phys_addr_t paddr;
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
};

static int dma_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	struct sg_table *table;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		goto table_alloc_failed;

	if (sg_alloc_table(table, 1, GFP_KERNEL))
		goto sg_alloc_failed;

	/*
	 * The referenced pfn and page are for setting the sram address to the
	 * sgtable, and cannot be used for other purposes, and cannot be accessed
	 * directly or indirectly.
	 *
	 * And not sure if there is a problem with the 32-bit system.
	 *
	 * page cannot support kmap func.
	 */
	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(buffer->paddr)), buffer->len, 0);

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);

	attachment->priv = a;

	mutex_lock(&buffer->attachments_lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->attachments_lock);

	return 0;

sg_alloc_failed:
	kfree(table);
table_alloc_failed:
	kfree(a);
	return -ENOMEM;
}

static void dma_heap_detatch(struct dma_buf *dmabuf,
			     struct dma_buf_attachment *attachment)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->attachments_lock);
	list_del(&a->list);
	mutex_unlock(&buffer->attachments_lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *dma_heap_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	int ret = 0;

	ret = dma_map_sgtable(attachment->dev, table, direction, DMA_ATTR_SKIP_CPU_SYNC);
	if (ret)
		return ERR_PTR(-ENOMEM);

	return table;
}

static void dma_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
	dma_unmap_sgtable(attachment->dev, table, direction, DMA_ATTR_SKIP_CPU_SYNC);
}

static void dma_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;

	gen_pool_free(buffer->pool, (unsigned long)buffer->vaddr, buffer->len);
	kfree(buffer);
}

static int dma_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;
	int ret;

	/* SRAM mappings are not cached */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	ret = vm_iomap_memory(vma, buffer->paddr, buffer->len);
	if (ret)
		pr_err("Could not map buffer to userspace\n");

	return ret;
}

static void *dma_heap_vmap(struct dma_buf *dmabuf)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;

	return buffer->vaddr;
}

static const struct dma_buf_ops sram_dma_heap_buf_ops = {
	.attach = dma_heap_attach,
	.detach = dma_heap_detatch,
	.map_dma_buf = dma_heap_map_dma_buf,
	.unmap_dma_buf = dma_heap_unmap_dma_buf,
	.release = dma_heap_dma_buf_release,
	.mmap = dma_heap_mmap,
	.vmap = dma_heap_vmap,
};

static struct dma_buf *sram_dma_heap_allocate(struct dma_heap *heap,
				unsigned long len,
				unsigned long fd_flags,
				unsigned long heap_flags)
{
	struct sram_dma_heap *sram_dma_heap = dma_heap_get_drvdata(heap);
	struct sram_dma_heap_buffer *buffer;

	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	buffer->pool = sram_dma_heap->pool;
	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->attachments_lock);
	buffer->len = len;

	buffer->vaddr = (void *)gen_pool_alloc(buffer->pool, buffer->len);
	if (!buffer->vaddr) {
		ret = -ENOMEM;
		goto free_buffer;
	}

	buffer->paddr = gen_pool_virt_to_phys(buffer->pool, (unsigned long)buffer->vaddr);
	if (buffer->paddr == -1) {
		ret = -ENOMEM;
		goto free_pool;
	}

	/* create the dmabuf */
	exp_info.ops = &sram_dma_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pool;
	}

	return dmabuf;

free_pool:
	gen_pool_free(buffer->pool, (unsigned long)buffer->vaddr, buffer->len);
free_buffer:
	kfree(buffer);

	return ERR_PTR(ret);
}

static struct dma_heap_ops sram_dma_heap_ops = {
	.allocate = sram_dma_heap_allocate,
};

static struct sram_dma_heap *sram_dma_heap_global;

static int sram_dma_heap_export(const char *name,
			 struct gen_pool *sram_gp)
{
	struct sram_dma_heap *sram_dma_heap;
	struct dma_heap_export_info exp_info;

	pr_info("Exporting SRAM pool '%s'\n", name);

	sram_dma_heap = kzalloc(sizeof(*sram_dma_heap), GFP_KERNEL);
	if (!sram_dma_heap)
		return -ENOMEM;
	sram_dma_heap->pool = sram_gp;

	exp_info.name = "sram_dma_heap";
	exp_info.ops = &sram_dma_heap_ops;
	exp_info.priv = sram_dma_heap;

	sram_dma_heap_global = sram_dma_heap;

	sram_dma_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(sram_dma_heap->heap)) {
		int ret = PTR_ERR(sram_dma_heap->heap);

		kfree(sram_dma_heap);
		return ret;
	}

	return 0;
}

struct dma_buf *sram_heap_alloc_dma_buf(size_t size)
{
	struct sram_dma_heap *sram_dma_heap = sram_dma_heap_global;
	struct sram_dma_heap_buffer *buffer;

	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->pool = sram_dma_heap->pool;
	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->attachments_lock);
	buffer->len = size;

	buffer->vaddr = (void *)gen_pool_alloc(buffer->pool, buffer->len);
	if (!buffer->vaddr) {
		ret = -ENOMEM;
		goto free_buffer;
	}

	buffer->paddr = gen_pool_virt_to_phys(buffer->pool, (unsigned long)buffer->vaddr);
	if (buffer->paddr == -1) {
		ret = -ENOMEM;
		goto free_pool;
	}

	/* create the dmabuf */
	exp_info.ops = &sram_dma_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pool;
	}

	return dmabuf;

free_pool:
	gen_pool_free(buffer->pool, (unsigned long)buffer->vaddr, buffer->len);
free_buffer:
	kfree(buffer);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(sram_heap_alloc_dma_buf);

struct page *sram_heap_alloc_pages(size_t size)
{
	struct sram_dma_heap *sram_dma_heap = sram_dma_heap_global;

	void *vaddr;
	phys_addr_t paddr;
	struct page *p;

	int ret = -ENOMEM;

	vaddr = (void *)gen_pool_alloc(sram_dma_heap->pool, size);
	if (!vaddr) {
		ret = -ENOMEM;
		pr_err("no memory");
		goto failed;
	}

	paddr = gen_pool_virt_to_phys(sram_dma_heap->pool, (unsigned long)vaddr);
	if (paddr == -1) {
		ret = -ENOMEM;
		pr_err("gen_pool_virt_to_phys failed");
		goto free_pool;
	}

	p = pfn_to_page(PFN_DOWN(paddr));

	return p;

free_pool:
	gen_pool_free(sram_dma_heap->pool, (unsigned long)vaddr, size);
failed:

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(sram_heap_alloc_pages);

static u64 gen_pool_phys_to_virt(struct gen_pool *pool, phys_addr_t paddr)
{
	struct gen_pool_chunk *chunk;
	u64 vaddr = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		/* TODO: only suit for simple chunk now */
		vaddr = chunk->start_addr + (paddr - chunk->phys_addr);
	}
	rcu_read_unlock();

	return vaddr;
}

void sram_heap_free_pages(struct page *p)
{
	struct sram_dma_heap *sram_dma_heap = sram_dma_heap_global;
	void *vaddr;

	vaddr = (void *)gen_pool_phys_to_virt(sram_dma_heap->pool, page_to_phys(p));

	gen_pool_free(sram_dma_heap->pool, (unsigned long)vaddr, PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(sram_heap_free_pages);

void sram_heap_free_dma_buf(struct dma_buf *dmabuf)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;

	gen_pool_free(buffer->pool, (unsigned long)buffer->vaddr, buffer->len);
	kfree(buffer);
}
EXPORT_SYMBOL_GPL(sram_heap_free_dma_buf);

void *sram_heap_get_vaddr(struct dma_buf *dmabuf)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;

	return buffer->vaddr;
}
EXPORT_SYMBOL_GPL(sram_heap_get_vaddr);

phys_addr_t sram_heap_get_paddr(struct dma_buf *dmabuf)
{
	struct sram_dma_heap_buffer *buffer = dmabuf->priv;

	return buffer->paddr;
}
EXPORT_SYMBOL_GPL(sram_heap_get_paddr);

static int rk_add_default_sram_heap(void)
{
	struct device_node *np = NULL;
	struct gen_pool *sram_gp = NULL;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "rockchip,sram-heap");
	if (!np) {
		pr_info("failed to get device node of sram-heap\n");
		return -ENODEV;
	}

	if (!of_device_is_available(np)) {
		of_node_put(np);
		return ret;
	}

	sram_gp = of_gen_pool_get(np, "rockchip,sram", 0);
	/* release node */
	of_node_put(np);
	if (sram_gp == NULL) {
		pr_err("sram gen pool is NULL");
		return -ENOMEM;
	}

	ret = sram_dma_heap_export("sram-heap", sram_gp);

	return ret;
}
module_init(rk_add_default_sram_heap);
MODULE_DESCRIPTION("Rockchip DMA-BUF SRAM Heap");
MODULE_LICENSE("GPL");
