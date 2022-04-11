/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

/* \file cc_buffer_mgr.h
 * Buffer Manager
 */

#ifndef __CC_BUFFER_MGR_H__
#define __CC_BUFFER_MGR_H__

#include <crypto/algapi.h>

#include "cc_driver.h"

enum cc_req_dma_buf_type {
	CC_DMA_BUF_NULL = 0,
	CC_DMA_BUF_DLLI,
	CC_DMA_BUF_MLLI
};

enum cc_sg_cpy_direct {
	CC_SG_TO_BUF = 0,
	CC_SG_FROM_BUF = 1
};

struct cc_mlli {
	cc_sram_addr_t sram_addr;
	unsigned int nents; //sg nents
	unsigned int mlli_nents; //mlli nents might be different than the above
};

struct mlli_params {
	struct dma_pool *curr_pool;
	u8 *mlli_virt_addr;
	dma_addr_t mlli_dma_addr;
	u32 mlli_len;
};

int cc_buffer_mgr_init(struct cc_drvdata *drvdata);

int cc_buffer_mgr_fini(struct cc_drvdata *drvdata);

int cc_map_cipher_request(struct cc_drvdata *drvdata, void *ctx,
			  unsigned int ivsize, unsigned int nbytes,
			  void *info, struct scatterlist *src,
			  struct scatterlist *dst, gfp_t flags);

void cc_unmap_cipher_request(struct device *dev, void *ctx, unsigned int ivsize,
			     struct scatterlist *src, struct scatterlist *dst);

int cc_map_aead_request(struct cc_drvdata *drvdata, struct aead_request *req);

void cc_unmap_aead_request(struct device *dev, struct aead_request *req);

int cc_map_hash_request_final(struct cc_drvdata *drvdata, void *ctx,
			      struct scatterlist *src, unsigned int nbytes,
			      bool do_update, gfp_t flags);

int cc_map_hash_request_update(struct cc_drvdata *drvdata, void *ctx,
			       struct scatterlist *src, unsigned int nbytes,
			       unsigned int block_size, gfp_t flags);

void cc_unmap_hash_request(struct device *dev, void *ctx,
			   struct scatterlist *src, bool do_revert);

void cc_copy_sg_portion(struct device *dev, u8 *dest, struct scatterlist *sg,
			u32 to_skip, u32 end, enum cc_sg_cpy_direct direct);

#endif /*__BUFFER_MGR_H__*/
