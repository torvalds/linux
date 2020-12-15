// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "qm.h"

#define HISI_ACC_SGL_SGE_NR_MIN		1
#define HISI_ACC_SGL_NR_MAX		256
#define HISI_ACC_SGL_ALIGN_SIZE		64
#define HISI_ACC_MEM_BLOCK_NR		5

struct acc_hw_sge {
	dma_addr_t buf;
	void *page_ctrl;
	__le32 len;
	__le32 pad;
	__le32 pad0;
	__le32 pad1;
};

/* use default sgl head size 64B */
struct hisi_acc_hw_sgl {
	dma_addr_t next_dma;
	__le16 entry_sum_in_chain;
	__le16 entry_sum_in_sgl;
	__le16 entry_length_in_sgl;
	__le16 pad0;
	__le64 pad1[5];
	struct hisi_acc_hw_sgl *next;
	struct acc_hw_sge sge_entries[];
} __aligned(1);

struct hisi_acc_sgl_pool {
	struct mem_block {
		struct hisi_acc_hw_sgl *sgl;
		dma_addr_t sgl_dma;
		size_t size;
	} mem_block[HISI_ACC_MEM_BLOCK_NR];
	u32 sgl_num_per_block;
	u32 block_num;
	u32 count;
	u32 sge_nr;
	size_t sgl_size;
};

/**
 * hisi_acc_create_sgl_pool() - Create a hw sgl pool.
 * @dev: The device which hw sgl pool belongs to.
 * @count: Count of hisi_acc_hw_sgl in pool.
 * @sge_nr: The count of sge in hw_sgl
 *
 * This function creates a hw sgl pool, after this user can get hw sgl memory
 * from it.
 */
struct hisi_acc_sgl_pool *hisi_acc_create_sgl_pool(struct device *dev,
						   u32 count, u32 sge_nr)
{
	u32 sgl_size, block_size, sgl_num_per_block, block_num, remain_sgl = 0;
	struct hisi_acc_sgl_pool *pool;
	struct mem_block *block;
	u32 i, j;

	if (!dev || !count || !sge_nr || sge_nr > HISI_ACC_SGL_SGE_NR_MAX)
		return ERR_PTR(-EINVAL);

	sgl_size = sizeof(struct acc_hw_sge) * sge_nr +
		   sizeof(struct hisi_acc_hw_sgl);
	block_size = 1 << (PAGE_SHIFT + MAX_ORDER <= 32 ?
			   PAGE_SHIFT + MAX_ORDER - 1 : 31);
	sgl_num_per_block = block_size / sgl_size;
	block_num = count / sgl_num_per_block;
	remain_sgl = count % sgl_num_per_block;

	if ((!remain_sgl && block_num > HISI_ACC_MEM_BLOCK_NR) ||
	    (remain_sgl > 0 && block_num > HISI_ACC_MEM_BLOCK_NR - 1))
		return ERR_PTR(-EINVAL);

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);
	block = pool->mem_block;

	for (i = 0; i < block_num; i++) {
		block[i].sgl = dma_alloc_coherent(dev, block_size,
						  &block[i].sgl_dma,
						  GFP_KERNEL);
		if (!block[i].sgl)
			goto err_free_mem;

		block[i].size = block_size;
	}

	if (remain_sgl > 0) {
		block[i].sgl = dma_alloc_coherent(dev, remain_sgl * sgl_size,
						  &block[i].sgl_dma,
						  GFP_KERNEL);
		if (!block[i].sgl)
			goto err_free_mem;

		block[i].size = remain_sgl * sgl_size;
	}

	pool->sgl_num_per_block = sgl_num_per_block;
	pool->block_num = remain_sgl ? block_num + 1 : block_num;
	pool->count = count;
	pool->sgl_size = sgl_size;
	pool->sge_nr = sge_nr;

	return pool;

err_free_mem:
	for (j = 0; j < i; j++) {
		dma_free_coherent(dev, block_size, block[j].sgl,
				  block[j].sgl_dma);
		memset(block + j, 0, sizeof(*block));
	}
	kfree(pool);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(hisi_acc_create_sgl_pool);

/**
 * hisi_acc_free_sgl_pool() - Free a hw sgl pool.
 * @dev: The device which hw sgl pool belongs to.
 * @pool: Pointer of pool.
 *
 * This function frees memory of a hw sgl pool.
 */
void hisi_acc_free_sgl_pool(struct device *dev, struct hisi_acc_sgl_pool *pool)
{
	struct mem_block *block;
	int i;

	if (!dev || !pool)
		return;

	block = pool->mem_block;

	for (i = 0; i < pool->block_num; i++)
		dma_free_coherent(dev, block[i].size, block[i].sgl,
				  block[i].sgl_dma);

	kfree(pool);
}
EXPORT_SYMBOL_GPL(hisi_acc_free_sgl_pool);

static struct hisi_acc_hw_sgl *acc_get_sgl(struct hisi_acc_sgl_pool *pool,
					   u32 index, dma_addr_t *hw_sgl_dma)
{
	struct mem_block *block;
	u32 block_index, offset;

	if (!pool || !hw_sgl_dma || index >= pool->count)
		return ERR_PTR(-EINVAL);

	block = pool->mem_block;
	block_index = index / pool->sgl_num_per_block;
	offset = index % pool->sgl_num_per_block;

	*hw_sgl_dma = block[block_index].sgl_dma + pool->sgl_size * offset;
	return (void *)block[block_index].sgl + pool->sgl_size * offset;
}

static void sg_map_to_hw_sg(struct scatterlist *sgl,
			    struct acc_hw_sge *hw_sge)
{
	hw_sge->buf = sg_dma_address(sgl);
	hw_sge->len = cpu_to_le32(sg_dma_len(sgl));
}

static void inc_hw_sgl_sge(struct hisi_acc_hw_sgl *hw_sgl)
{
	u16 var = le16_to_cpu(hw_sgl->entry_sum_in_sgl);

	var++;
	hw_sgl->entry_sum_in_sgl = cpu_to_le16(var);
}

static void update_hw_sgl_sum_sge(struct hisi_acc_hw_sgl *hw_sgl, u16 sum)
{
	hw_sgl->entry_sum_in_chain = cpu_to_le16(sum);
}

/**
 * hisi_acc_sg_buf_map_to_hw_sgl - Map a scatterlist to a hw sgl.
 * @dev: The device which hw sgl belongs to.
 * @sgl: Scatterlist which will be mapped to hw sgl.
 * @pool: Pool which hw sgl memory will be allocated in.
 * @index: Index of hisi_acc_hw_sgl in pool.
 * @hw_sgl_dma: The dma address of allocated hw sgl.
 *
 * This function builds hw sgl according input sgl, user can use hw_sgl_dma
 * as src/dst in its BD. Only support single hw sgl currently.
 */
struct hisi_acc_hw_sgl *
hisi_acc_sg_buf_map_to_hw_sgl(struct device *dev,
			      struct scatterlist *sgl,
			      struct hisi_acc_sgl_pool *pool,
			      u32 index, dma_addr_t *hw_sgl_dma)
{
	struct hisi_acc_hw_sgl *curr_hw_sgl;
	dma_addr_t curr_sgl_dma = 0;
	struct acc_hw_sge *curr_hw_sge;
	struct scatterlist *sg;
	int i, sg_n, sg_n_mapped;

	if (!dev || !sgl || !pool || !hw_sgl_dma)
		return ERR_PTR(-EINVAL);

	sg_n = sg_nents(sgl);

	sg_n_mapped = dma_map_sg(dev, sgl, sg_n, DMA_BIDIRECTIONAL);
	if (!sg_n_mapped)
		return ERR_PTR(-EINVAL);

	if (sg_n_mapped > pool->sge_nr) {
		dma_unmap_sg(dev, sgl, sg_n, DMA_BIDIRECTIONAL);
		return ERR_PTR(-EINVAL);
	}

	curr_hw_sgl = acc_get_sgl(pool, index, &curr_sgl_dma);
	if (IS_ERR(curr_hw_sgl)) {
		dma_unmap_sg(dev, sgl, sg_n, DMA_BIDIRECTIONAL);
		return ERR_PTR(-ENOMEM);

	}
	curr_hw_sgl->entry_length_in_sgl = cpu_to_le16(pool->sge_nr);
	curr_hw_sge = curr_hw_sgl->sge_entries;

	for_each_sg(sgl, sg, sg_n_mapped, i) {
		sg_map_to_hw_sg(sg, curr_hw_sge);
		inc_hw_sgl_sge(curr_hw_sgl);
		curr_hw_sge++;
	}

	update_hw_sgl_sum_sge(curr_hw_sgl, pool->sge_nr);
	*hw_sgl_dma = curr_sgl_dma;

	return curr_hw_sgl;
}
EXPORT_SYMBOL_GPL(hisi_acc_sg_buf_map_to_hw_sgl);

/**
 * hisi_acc_sg_buf_unmap() - Unmap allocated hw sgl.
 * @dev: The device which hw sgl belongs to.
 * @sgl: Related scatterlist.
 * @hw_sgl: Virtual address of hw sgl.
 *
 * This function unmaps allocated hw sgl.
 */
void hisi_acc_sg_buf_unmap(struct device *dev, struct scatterlist *sgl,
			   struct hisi_acc_hw_sgl *hw_sgl)
{
	if (!dev || !sgl || !hw_sgl)
		return;

	dma_unmap_sg(dev, sgl, sg_nents(sgl), DMA_BIDIRECTIONAL);

	hw_sgl->entry_sum_in_chain = 0;
	hw_sgl->entry_sum_in_sgl = 0;
	hw_sgl->entry_length_in_sgl = 0;
}
EXPORT_SYMBOL_GPL(hisi_acc_sg_buf_unmap);
