// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include "./sgl.h"

#define HISI_ACC_SGL_SGE_NR_MIN		1
#define HISI_ACC_SGL_SGE_NR_MAX		255
#define HISI_ACC_SGL_SGE_NR_DEF		10
#define HISI_ACC_SGL_NR_MAX		256
#define HISI_ACC_SGL_ALIGN_SIZE		64

static int acc_sgl_sge_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	u32 n;

	if (!val)
		return -EINVAL;

	ret = kstrtou32(val, 10, &n);
	if (ret != 0 || n > HISI_ACC_SGL_SGE_NR_MAX || n == 0)
		return -EINVAL;

	return param_set_int(val, kp);
}

static const struct kernel_param_ops acc_sgl_sge_ops = {
	.set = acc_sgl_sge_set,
	.get = param_get_int,
};

static u32 acc_sgl_sge_nr = HISI_ACC_SGL_SGE_NR_DEF;
module_param_cb(acc_sgl_sge_nr, &acc_sgl_sge_ops, &acc_sgl_sge_nr, 0444);
MODULE_PARM_DESC(acc_sgl_sge_nr, "Number of sge in sgl(1-255)");

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

/**
 * hisi_acc_create_sgl_pool() - Create a hw sgl pool.
 * @dev: The device which hw sgl pool belongs to.
 * @pool: Pointer of pool.
 * @count: Count of hisi_acc_hw_sgl in pool.
 *
 * This function creates a hw sgl pool, after this user can get hw sgl memory
 * from it.
 */
int hisi_acc_create_sgl_pool(struct device *dev,
			     struct hisi_acc_sgl_pool *pool, u32 count)
{
	u32 sgl_size;
	u32 size;

	if (!dev || !pool || !count)
		return -EINVAL;

	sgl_size = sizeof(struct acc_hw_sge) * acc_sgl_sge_nr +
		   sizeof(struct hisi_acc_hw_sgl);
	size = sgl_size * count;

	pool->sgl = dma_alloc_coherent(dev, size, &pool->sgl_dma, GFP_KERNEL);
	if (!pool->sgl)
		return -ENOMEM;

	pool->size = size;
	pool->count = count;
	pool->sgl_size = sgl_size;

	return 0;
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
	dma_free_coherent(dev, pool->size, pool->sgl, pool->sgl_dma);
	memset(pool, 0, sizeof(struct hisi_acc_sgl_pool));
}
EXPORT_SYMBOL_GPL(hisi_acc_free_sgl_pool);

struct hisi_acc_hw_sgl *acc_get_sgl(struct hisi_acc_sgl_pool *pool, u32 index,
				    dma_addr_t *hw_sgl_dma)
{
	if (!pool || !hw_sgl_dma || index >= pool->count || !pool->sgl)
		return ERR_PTR(-EINVAL);

	*hw_sgl_dma = pool->sgl_dma + pool->sgl_size * index;
	return (void *)pool->sgl + pool->sgl_size * index;
}

void acc_put_sgl(struct hisi_acc_sgl_pool *pool, u32 index) {}

static void sg_map_to_hw_sg(struct scatterlist *sgl,
			    struct acc_hw_sge *hw_sge)
{
	hw_sge->buf = sgl->dma_address;
	hw_sge->len = sgl->dma_length;
}

static void inc_hw_sgl_sge(struct hisi_acc_hw_sgl *hw_sgl)
{
	hw_sgl->entry_sum_in_sgl++;
}

static void update_hw_sgl_sum_sge(struct hisi_acc_hw_sgl *hw_sgl, u16 sum)
{
	hw_sgl->entry_sum_in_chain = sum;
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
	int sg_n = sg_nents(sgl);
	int i, ret;

	if (!dev || !sgl || !pool || !hw_sgl_dma || sg_n > acc_sgl_sge_nr)
		return ERR_PTR(-EINVAL);

	ret = dma_map_sg(dev, sgl, sg_n, DMA_BIDIRECTIONAL);
	if (!ret)
		return ERR_PTR(-EINVAL);

	curr_hw_sgl = acc_get_sgl(pool, index, &curr_sgl_dma);
	if (!curr_hw_sgl) {
		ret = -ENOMEM;
		goto err_unmap_sg;
	}
	curr_hw_sgl->entry_length_in_sgl = acc_sgl_sge_nr;
	curr_hw_sge = curr_hw_sgl->sge_entries;

	for_each_sg(sgl, sg, sg_n, i) {
		sg_map_to_hw_sg(sg, curr_hw_sge);
		inc_hw_sgl_sge(curr_hw_sgl);
		curr_hw_sge++;
	}

	update_hw_sgl_sum_sge(curr_hw_sgl, acc_sgl_sge_nr);
	*hw_sgl_dma = curr_sgl_dma;

	return curr_hw_sgl;

err_unmap_sg:
	dma_unmap_sg(dev, sgl, sg_n, DMA_BIDIRECTIONAL);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(hisi_acc_sg_buf_map_to_hw_sgl);

/**
 * hisi_acc_sg_buf_unmap() - Unmap allocated hw sgl.
 * @dev: The device which hw sgl belongs to.
 * @sgl: Related scatterlist.
 * @hw_sgl: Virtual address of hw sgl.
 * @hw_sgl_dma: DMA address of hw sgl.
 * @pool: Pool which hw sgl is allocated in.
 *
 * This function unmaps allocated hw sgl.
 */
void hisi_acc_sg_buf_unmap(struct device *dev, struct scatterlist *sgl,
			   struct hisi_acc_hw_sgl *hw_sgl)
{
	dma_unmap_sg(dev, sgl, sg_nents(sgl), DMA_BIDIRECTIONAL);

	hw_sgl->entry_sum_in_chain = 0;
	hw_sgl->entry_sum_in_sgl = 0;
	hw_sgl->entry_length_in_sgl = 0;
}
EXPORT_SYMBOL_GPL(hisi_acc_sg_buf_unmap);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_DESCRIPTION("HiSilicon Accelerator SGL support");
