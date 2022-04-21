/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_UTILS_H__
#define __RK_CRYPTO_UTILS_H__

#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include "rk_crypto_core.h"

/* Default 256 x 4K = 1MByte */
#define RK_DEFAULT_LLI_CNT	256

struct crypto_lli_desc {
	u32 src_addr;
	u32 src_len;
	u32 dst_addr;
	u32 dst_len;
	u32 user_define;
	u32 reserve;
	u32 dma_ctrl;
	u32 next_addr;
};

struct rk_hw_desc {
	struct device			*dev;
	struct crypto_lli_desc		*lli_aad;
	struct crypto_lli_desc		*lli_head;
	struct crypto_lli_desc		*lli_tail;
	dma_addr_t			lli_head_dma;
	dma_addr_t			lli_aad_dma;
	u32				total;
};

void rk_crypto_write_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, const u8 *data, u32 bytes);

void rk_crypto_clear_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u32 words);

void rk_crypto_read_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u8 *data, u32 bytes);

bool rk_crypto_check_align(struct scatterlist *src_sg, size_t src_nents,
			   struct scatterlist *dst_sg, size_t dst_nents,
			   int align_mask);

bool rk_crypto_check_dmafd(struct scatterlist *sgl, size_t nents);

u64 rk_crypto_hw_desc_maxlen(struct scatterlist *sg, u64 len, u32 *max_nents);

int rk_crypto_hw_desc_alloc(struct device *dev, struct rk_hw_desc *hw_desc);

int rk_crypto_hw_desc_init(struct rk_hw_desc *hw_desc,
			   struct scatterlist *src_sg,
			   struct scatterlist *dst_sg,
			   u64 len);

void rk_crypto_hw_desc_free(struct rk_hw_desc *hw_desc);

void rk_crypto_dump_hw_desc(struct rk_hw_desc *hw_desc);

#endif

