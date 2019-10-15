/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */
#ifndef HISI_ACC_SGL_H
#define HISI_ACC_SGL_H

struct hisi_acc_sgl_pool {
	struct hisi_acc_hw_sgl *sgl;
	dma_addr_t sgl_dma;
	size_t size;
	u32 count;
	size_t sgl_size;
};

struct hisi_acc_hw_sgl *
hisi_acc_sg_buf_map_to_hw_sgl(struct device *dev,
			      struct scatterlist *sgl,
			      struct hisi_acc_sgl_pool *pool,
			      u32 index, dma_addr_t *hw_sgl_dma);
void hisi_acc_sg_buf_unmap(struct device *dev, struct scatterlist *sgl,
			   struct hisi_acc_hw_sgl *hw_sgl);
int hisi_acc_create_sgl_pool(struct device *dev, struct hisi_acc_sgl_pool *pool,
			     u32 count);
void hisi_acc_free_sgl_pool(struct device *dev, struct hisi_acc_sgl_pool *pool);
#endif
