// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip crypto uitls
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include <crypto/scatterwalk.h>
#include <linux/scatterlist.h>

#include "rk_crypto_core.h"
#include "rk_crypto_utils.h"

static inline void word2byte_be(u32 word, u8 *ch)
{
	ch[0] = (word >> 24) & 0xff;
	ch[1] = (word >> 16) & 0xff;
	ch[2] = (word >> 8) & 0xff;
	ch[3] = (word >> 0) & 0xff;
}

static inline u32 byte2word_be(const u8 *ch)
{
	return (*ch << 24) + (*(ch + 1) << 16) +
	       (*(ch + 2) << 8) + *(ch + 3);
}

void rk_crypto_write_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, const u8 *data, u32 bytes)
{
	u32 i;
	u8 tmp_buf[4];

	for (i = 0; i < bytes / 4; i++, base_addr += 4)
		CRYPTO_WRITE(rk_dev, base_addr, byte2word_be(data + i * 4));

	if (bytes % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, data + (bytes / 4) * 4, bytes % 4);
		CRYPTO_WRITE(rk_dev, base_addr, byte2word_be(tmp_buf));
	}
}

void rk_crypto_clear_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u32 words)
{
	u32 i;

	for (i = 0; i < words; i++, base_addr += 4)
		CRYPTO_WRITE(rk_dev, base_addr, 0);
}

void rk_crypto_read_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u8 *data, u32 bytes)
{
	u32 i;

	for (i = 0; i < bytes / 4; i++, base_addr += 4)
		word2byte_be(CRYPTO_READ(rk_dev, base_addr), data + i * 4);

	if (bytes % 4) {
		uint8_t tmp_buf[4];

		word2byte_be(CRYPTO_READ(rk_dev, base_addr), tmp_buf);
		memcpy(data + i * 4, tmp_buf, bytes % 4);
	}
}

static int check_scatter_align(struct scatterlist *sg_src,
			       struct scatterlist *sg_dst,
			       int align_mask)
{
	int in, out, align;

	in = IS_ALIGNED((u32)sg_src->offset, 4) &&
	     IS_ALIGNED((u32)sg_src->length, align_mask) &&
	     (sg_phys(sg_src) < SZ_4G);
	if (!sg_dst)
		return in;

	out = IS_ALIGNED((u32)sg_dst->offset, 4) &&
	      IS_ALIGNED((u32)sg_dst->length, align_mask) &&
	      (sg_phys(sg_dst) < SZ_4G);
	align = in && out;

	return (align && (sg_src->length == sg_dst->length));
}

bool rk_crypto_check_align(struct scatterlist *src_sg, size_t src_nents,
			   struct scatterlist *dst_sg, size_t dst_nents,
			   int align_mask)
{
	struct scatterlist *src_tmp = NULL;
	struct scatterlist *dst_tmp = NULL;
	unsigned int i;

	if (dst_sg && src_nents != dst_nents)
		return false;

	src_tmp = src_sg;
	dst_tmp = dst_sg;

	for (i = 0; i < src_nents; i++) {
		if (!src_tmp)
			return false;

		if (!check_scatter_align(src_tmp, dst_tmp, align_mask))
			return false;

		src_tmp = sg_next(src_tmp);

		if (dst_sg)
			dst_tmp = sg_next(dst_tmp);
	}

	return true;
}

bool rk_crypto_check_dmafd(struct scatterlist *sgl, size_t nents)
{
	struct scatterlist *src_tmp = NULL;
	unsigned int i;

	for_each_sg(sgl, src_tmp, nents, i) {
		if (!src_tmp)
			return false;

		if (src_tmp->length && !sg_dma_address(src_tmp))
			return false;
	}

	return true;
}

void rk_crypto_dump_hw_desc(struct rk_hw_desc *hw_desc)
{
	struct crypto_lli_desc *cur_lli = NULL;
	u32 i;

	cur_lli = hw_desc->lli_head;

	CRYPTO_TRACE("lli_head = %lx, lli_tail = %lx",
		     (unsigned long)hw_desc->lli_head, (unsigned long)hw_desc->lli_tail);

	for (i = 0; i < hw_desc->total; i++, cur_lli++) {
		CRYPTO_TRACE("cur_lli = %lx", (unsigned long)cur_lli);
		CRYPTO_TRACE("src_addr = %08x", cur_lli->src_addr);
		CRYPTO_TRACE("src_len  = %08x", cur_lli->src_len);
		CRYPTO_TRACE("dst_addr = %08x", cur_lli->dst_addr);
		CRYPTO_TRACE("dst_len  = %08x", cur_lli->dst_len);
		CRYPTO_TRACE("user_def = %08x", cur_lli->user_define);
		CRYPTO_TRACE("dma_ctl  = %08x", cur_lli->dma_ctrl);
		CRYPTO_TRACE("next     = %08x\n", cur_lli->next_addr);

		if (cur_lli == hw_desc->lli_tail)
			break;
	}
}

u64 rk_crypto_hw_desc_maxlen(struct scatterlist *sg, u64 len, u32 *max_nents)
{
	int nents;
	u64 total;

	if (!len)
		return 0;

	for (nents = 0, total = 0; sg; sg = sg_next(sg)) {
		if (!sg)
			goto exit;

		nents++;
		total += sg->length;

		if (nents >= RK_DEFAULT_LLI_CNT || total >= len)
			goto exit;
	}

exit:
	*max_nents = nents;
	return total > len ? len : total;
}

int rk_crypto_hw_desc_alloc(struct device *dev, struct rk_hw_desc *hw_desc)
{
	u32 lli_cnt = RK_DEFAULT_LLI_CNT;
	u32 lli_len = lli_cnt * sizeof(struct crypto_lli_desc);

	if (!dev || !hw_desc)
		return -EINVAL;

	memset(hw_desc, 0x00, sizeof(*hw_desc));

	hw_desc->lli_aad = dma_alloc_coherent(dev, sizeof(struct crypto_lli_desc),
					      &hw_desc->lli_aad_dma, GFP_KERNEL);
	if (!hw_desc->lli_aad)
		return -ENOMEM;

	///TODO: cma
	hw_desc->lli_head = dma_alloc_coherent(dev, lli_len, &hw_desc->lli_head_dma, GFP_KERNEL);
	if (!hw_desc->lli_head) {
		dma_free_coherent(dev, sizeof(struct crypto_lli_desc),
				  hw_desc->lli_aad, hw_desc->lli_aad_dma);
		return -ENOMEM;
	}

	hw_desc->lli_tail = hw_desc->lli_head;
	hw_desc->total    = lli_cnt;
	hw_desc->dev      = dev;

	memset(hw_desc->lli_head, 0x00, lli_len);

	CRYPTO_TRACE("dev = %lx, buffer_len = %u, lli_head = %lx, lli_head_dma = %lx",
		     (unsigned long)hw_desc->dev, lli_len,
		     (unsigned long)hw_desc->lli_head, (unsigned long)hw_desc->lli_head_dma);

	return 0;
}

void rk_crypto_hw_desc_free(struct rk_hw_desc *hw_desc)
{
	if (!hw_desc || !hw_desc->dev || !hw_desc->lli_head)
		return;

	CRYPTO_TRACE("dev = %lx, buffer_len = %lu, lli_head = %lx, lli_head_dma = %lx",
		     (unsigned long)hw_desc->dev,
		     (unsigned long)hw_desc->total * sizeof(struct crypto_lli_desc),
		     (unsigned long)hw_desc->lli_head, (unsigned long)hw_desc->lli_head_dma);

	dma_free_coherent(hw_desc->dev, sizeof(struct crypto_lli_desc),
			  hw_desc->lli_aad, hw_desc->lli_aad_dma);

	dma_free_coherent(hw_desc->dev, hw_desc->total * sizeof(struct crypto_lli_desc),
			  hw_desc->lli_head, hw_desc->lli_head_dma);

	memset(hw_desc, 0x00, sizeof(*hw_desc));
}

int rk_crypto_hw_desc_init(struct rk_hw_desc *hw_desc,
			   struct scatterlist *src_sg,
			   struct scatterlist *dst_sg,
			   u64 len)
{
	struct crypto_lli_desc *cur_lli = NULL;
	struct scatterlist *tmp_src, *tmp_dst;
	dma_addr_t tmp_next_dma;
	u32 src_nents, dst_nents;
	u32 i, data_cnt = 0;

	if (!hw_desc || !hw_desc->dev || !hw_desc->lli_head)
		return -EINVAL;

	if (!src_sg || len == 0)
		return -EINVAL;

	src_nents = sg_nents_for_len(src_sg, len);
	dst_nents = dst_sg ? sg_nents_for_len(dst_sg, len) : src_nents;

	if (src_nents != dst_nents)
		return -EINVAL;

	CRYPTO_TRACE("src_nents = %u, total = %u, len = %llu", src_nents, hw_desc->total, len);

	if (src_nents > hw_desc->total) {
		pr_err("crypto: nents overflow, %u > %u", src_nents, hw_desc->total);
		return -ENOMEM;
	}

	memset(hw_desc->lli_head, 0x00, src_nents * sizeof(struct crypto_lli_desc));

	cur_lli      = hw_desc->lli_head;
	tmp_src      = src_sg;
	tmp_dst      = dst_sg;
	tmp_next_dma = hw_desc->lli_head_dma + sizeof(*cur_lli);

	if (dst_sg) {
		for (i = 0; i < src_nents - 1; i++, cur_lli++, tmp_next_dma += sizeof(*cur_lli)) {
			cur_lli->src_addr  = sg_dma_address(tmp_src);
			cur_lli->src_len   = sg_dma_len(tmp_src);
			cur_lli->dst_addr  = sg_dma_address(tmp_dst);
			cur_lli->dst_len   = sg_dma_len(tmp_dst);
			cur_lli->next_addr = tmp_next_dma;

			data_cnt += sg_dma_len(tmp_src);
			tmp_src   = sg_next(tmp_src);
			tmp_dst   = sg_next(tmp_dst);
		}
	} else {
		for (i = 0; i < src_nents - 1; i++, cur_lli++, tmp_next_dma += sizeof(*cur_lli)) {
			cur_lli->src_addr  = sg_dma_address(tmp_src);
			cur_lli->src_len   = sg_dma_len(tmp_src);
			cur_lli->next_addr = tmp_next_dma;

			data_cnt += sg_dma_len(tmp_src);
			tmp_src   = sg_next(tmp_src);
		}
	}

	/* for last lli */
	cur_lli->src_addr  = sg_dma_address(tmp_src);
	cur_lli->src_len   = len - data_cnt;
	cur_lli->next_addr = 0;

	if (dst_sg) {
		cur_lli->dst_addr  = sg_dma_address(tmp_dst);
		cur_lli->dst_len   = len - data_cnt;
	}

	hw_desc->lli_tail = cur_lli;

	return 0;
}

