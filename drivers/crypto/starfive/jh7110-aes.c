// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive AES acceleration driver
 *
 * Copyright (c) 2022 StarFive Technology
 */

#include <crypto/engine.h>
#include <crypto/gcm.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include "jh7110-cryp.h"
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#define STARFIVE_AES_REGS_OFFSET	0x100
#define STARFIVE_AES_AESDIO0R		(STARFIVE_AES_REGS_OFFSET + 0x0)
#define STARFIVE_AES_KEY0		(STARFIVE_AES_REGS_OFFSET + 0x4)
#define STARFIVE_AES_KEY1		(STARFIVE_AES_REGS_OFFSET + 0x8)
#define STARFIVE_AES_KEY2		(STARFIVE_AES_REGS_OFFSET + 0xC)
#define STARFIVE_AES_KEY3		(STARFIVE_AES_REGS_OFFSET + 0x10)
#define STARFIVE_AES_KEY4		(STARFIVE_AES_REGS_OFFSET + 0x14)
#define STARFIVE_AES_KEY5		(STARFIVE_AES_REGS_OFFSET + 0x18)
#define STARFIVE_AES_KEY6		(STARFIVE_AES_REGS_OFFSET + 0x1C)
#define STARFIVE_AES_KEY7		(STARFIVE_AES_REGS_OFFSET + 0x20)
#define STARFIVE_AES_CSR		(STARFIVE_AES_REGS_OFFSET + 0x24)
#define STARFIVE_AES_IV0		(STARFIVE_AES_REGS_OFFSET + 0x28)
#define STARFIVE_AES_IV1		(STARFIVE_AES_REGS_OFFSET + 0x2C)
#define STARFIVE_AES_IV2		(STARFIVE_AES_REGS_OFFSET + 0x30)
#define STARFIVE_AES_IV3		(STARFIVE_AES_REGS_OFFSET + 0x34)
#define STARFIVE_AES_NONCE0		(STARFIVE_AES_REGS_OFFSET + 0x3C)
#define STARFIVE_AES_NONCE1		(STARFIVE_AES_REGS_OFFSET + 0x40)
#define STARFIVE_AES_NONCE2		(STARFIVE_AES_REGS_OFFSET + 0x44)
#define STARFIVE_AES_NONCE3		(STARFIVE_AES_REGS_OFFSET + 0x48)
#define STARFIVE_AES_ALEN0		(STARFIVE_AES_REGS_OFFSET + 0x4C)
#define STARFIVE_AES_ALEN1		(STARFIVE_AES_REGS_OFFSET + 0x50)
#define STARFIVE_AES_MLEN0		(STARFIVE_AES_REGS_OFFSET + 0x54)
#define STARFIVE_AES_MLEN1		(STARFIVE_AES_REGS_OFFSET + 0x58)
#define STARFIVE_AES_IVLEN		(STARFIVE_AES_REGS_OFFSET + 0x5C)

#define FLG_MODE_MASK			GENMASK(2, 0)
#define FLG_ENCRYPT			BIT(4)

/* Misc */
#define CCM_B0_ADATA			0x40
#define AES_BLOCK_32			(AES_BLOCK_SIZE / sizeof(u32))

static inline int starfive_aes_wait_busy(struct starfive_cryp_dev *cryp)
{
	u32 status;

	return readl_relaxed_poll_timeout(cryp->base + STARFIVE_AES_CSR, status,
					  !(status & STARFIVE_AES_BUSY), 10, 100000);
}

static inline int starfive_aes_wait_keydone(struct starfive_cryp_dev *cryp)
{
	u32 status;

	return readl_relaxed_poll_timeout(cryp->base + STARFIVE_AES_CSR, status,
					  (status & STARFIVE_AES_KEY_DONE), 10, 100000);
}

static inline int starfive_aes_wait_gcmdone(struct starfive_cryp_dev *cryp)
{
	u32 status;

	return readl_relaxed_poll_timeout(cryp->base + STARFIVE_AES_CSR, status,
					  (status & STARFIVE_AES_GCM_DONE), 10, 100000);
}

static inline int is_gcm(struct starfive_cryp_dev *cryp)
{
	return (cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_GCM;
}

static inline bool is_encrypt(struct starfive_cryp_dev *cryp)
{
	return cryp->flags & FLG_ENCRYPT;
}

static void starfive_aes_aead_hw_start(struct starfive_cryp_ctx *ctx, u32 hw_mode)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	unsigned int value;

	switch (hw_mode) {
	case STARFIVE_AES_MODE_GCM:
		value = readl(ctx->cryp->base + STARFIVE_AES_CSR);
		value |= STARFIVE_AES_GCM_START;
		writel(value, cryp->base + STARFIVE_AES_CSR);
		starfive_aes_wait_gcmdone(cryp);
		break;
	case STARFIVE_AES_MODE_CCM:
		value = readl(ctx->cryp->base + STARFIVE_AES_CSR);
		value |= STARFIVE_AES_CCM_START;
		writel(value, cryp->base + STARFIVE_AES_CSR);
		break;
	}
}

static inline void starfive_aes_set_alen(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;

	writel(upper_32_bits(cryp->assoclen), cryp->base + STARFIVE_AES_ALEN0);
	writel(lower_32_bits(cryp->assoclen), cryp->base + STARFIVE_AES_ALEN1);
}

static inline void starfive_aes_set_mlen(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;

	writel(upper_32_bits(cryp->total_in), cryp->base + STARFIVE_AES_MLEN0);
	writel(lower_32_bits(cryp->total_in), cryp->base + STARFIVE_AES_MLEN1);
}

static inline int starfive_aes_ccm_check_iv(const u8 *iv)
{
	/* 2 <= L <= 8, so 1 <= L' <= 7. */
	if (iv[0] < 1 || iv[0] > 7)
		return -EINVAL;

	return 0;
}

static int starfive_aes_write_iv(struct starfive_cryp_ctx *ctx, u32 *iv)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;

	writel(iv[0], cryp->base + STARFIVE_AES_IV0);
	writel(iv[1], cryp->base + STARFIVE_AES_IV1);
	writel(iv[2], cryp->base + STARFIVE_AES_IV2);

	if (is_gcm(cryp)) {
		if (starfive_aes_wait_gcmdone(cryp))
			return -ETIMEDOUT;

		return 0;
	}

	writel(iv[3], cryp->base + STARFIVE_AES_IV3);

	return 0;
}

static inline void starfive_aes_get_iv(struct starfive_cryp_dev *cryp, u32 *iv)
{
	iv[0] = readl(cryp->base + STARFIVE_AES_IV0);
	iv[1] = readl(cryp->base + STARFIVE_AES_IV1);
	iv[2] = readl(cryp->base + STARFIVE_AES_IV2);
	iv[3] = readl(cryp->base + STARFIVE_AES_IV3);
}

static inline void starfive_aes_write_nonce(struct starfive_cryp_ctx *ctx, u32 *nonce)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;

	writel(nonce[0], cryp->base + STARFIVE_AES_NONCE0);
	writel(nonce[1], cryp->base + STARFIVE_AES_NONCE1);
	writel(nonce[2], cryp->base + STARFIVE_AES_NONCE2);
	writel(nonce[3], cryp->base + STARFIVE_AES_NONCE3);
}

static int starfive_aes_write_key(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	u32 *key = (u32 *)ctx->key;

	if (ctx->keylen >= AES_KEYSIZE_128) {
		writel(key[0], cryp->base + STARFIVE_AES_KEY0);
		writel(key[1], cryp->base + STARFIVE_AES_KEY1);
		writel(key[2], cryp->base + STARFIVE_AES_KEY2);
		writel(key[3], cryp->base + STARFIVE_AES_KEY3);
	}

	if (ctx->keylen >= AES_KEYSIZE_192) {
		writel(key[4], cryp->base + STARFIVE_AES_KEY4);
		writel(key[5], cryp->base + STARFIVE_AES_KEY5);
	}

	if (ctx->keylen >= AES_KEYSIZE_256) {
		writel(key[6], cryp->base + STARFIVE_AES_KEY6);
		writel(key[7], cryp->base + STARFIVE_AES_KEY7);
	}

	if (starfive_aes_wait_keydone(cryp))
		return -ETIMEDOUT;

	return 0;
}

static int starfive_aes_ccm_init(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	u8 iv[AES_BLOCK_SIZE], b0[AES_BLOCK_SIZE];
	unsigned int textlen;

	memcpy(iv, cryp->req.areq->iv, AES_BLOCK_SIZE);
	memset(iv + AES_BLOCK_SIZE - 1 - iv[0], 0, iv[0] + 1);

	/* Build B0 */
	memcpy(b0, iv, AES_BLOCK_SIZE);

	b0[0] |= (8 * ((cryp->authsize - 2) / 2));

	if (cryp->assoclen)
		b0[0] |= CCM_B0_ADATA;

	textlen = cryp->total_in;

	b0[AES_BLOCK_SIZE - 2] = textlen >> 8;
	b0[AES_BLOCK_SIZE - 1] = textlen & 0xFF;

	starfive_aes_write_nonce(ctx, (u32 *)b0);

	return 0;
}

static int starfive_aes_hw_init(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_request_ctx *rctx = ctx->rctx;
	struct starfive_cryp_dev *cryp = ctx->cryp;
	u32 hw_mode;

	/* reset */
	rctx->csr.aes.v = 0;
	rctx->csr.aes.aesrst = 1;
	writel(rctx->csr.aes.v, cryp->base + STARFIVE_AES_CSR);

	/* csr setup */
	hw_mode = cryp->flags & FLG_MODE_MASK;

	rctx->csr.aes.v = 0;

	switch (ctx->keylen) {
	case AES_KEYSIZE_128:
		rctx->csr.aes.keymode = STARFIVE_AES_KEYMODE_128;
		break;
	case AES_KEYSIZE_192:
		rctx->csr.aes.keymode = STARFIVE_AES_KEYMODE_192;
		break;
	case AES_KEYSIZE_256:
		rctx->csr.aes.keymode = STARFIVE_AES_KEYMODE_256;
		break;
	}

	rctx->csr.aes.mode  = hw_mode;
	rctx->csr.aes.cmode = !is_encrypt(cryp);
	rctx->csr.aes.stmode = STARFIVE_AES_MODE_XFB_1;

	if (cryp->side_chan) {
		rctx->csr.aes.delay_aes = 1;
		rctx->csr.aes.vaes_start = 1;
	}

	writel(rctx->csr.aes.v, cryp->base + STARFIVE_AES_CSR);

	cryp->err = starfive_aes_write_key(ctx);
	if (cryp->err)
		return cryp->err;

	switch (hw_mode) {
	case STARFIVE_AES_MODE_GCM:
		starfive_aes_set_alen(ctx);
		starfive_aes_set_mlen(ctx);
		writel(GCM_AES_IV_SIZE, cryp->base + STARFIVE_AES_IVLEN);
		starfive_aes_aead_hw_start(ctx, hw_mode);
		starfive_aes_write_iv(ctx, (void *)cryp->req.areq->iv);
		break;
	case STARFIVE_AES_MODE_CCM:
		starfive_aes_set_alen(ctx);
		starfive_aes_set_mlen(ctx);
		starfive_aes_ccm_init(ctx);
		starfive_aes_aead_hw_start(ctx, hw_mode);
		break;
	case STARFIVE_AES_MODE_CBC:
	case STARFIVE_AES_MODE_CTR:
		starfive_aes_write_iv(ctx, (void *)cryp->req.sreq->iv);
		break;
	default:
		break;
	}

	return cryp->err;
}

static int starfive_aes_read_authtag(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	struct starfive_cryp_request_ctx *rctx = ctx->rctx;
	int i;

	if (starfive_aes_wait_busy(cryp))
		return dev_err_probe(cryp->dev, -ETIMEDOUT,
				     "Timeout waiting for tag generation.");

	if ((cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_GCM) {
		cryp->tag_out[0] = readl(cryp->base + STARFIVE_AES_NONCE0);
		cryp->tag_out[1] = readl(cryp->base + STARFIVE_AES_NONCE1);
		cryp->tag_out[2] = readl(cryp->base + STARFIVE_AES_NONCE2);
		cryp->tag_out[3] = readl(cryp->base + STARFIVE_AES_NONCE3);
	} else {
		for (i = 0; i < AES_BLOCK_32; i++)
			cryp->tag_out[i] = readl(cryp->base + STARFIVE_AES_AESDIO0R);
	}

	if (is_encrypt(cryp)) {
		scatterwalk_map_and_copy(cryp->tag_out, rctx->out_sg,
					 cryp->total_in, cryp->authsize, 1);
	} else {
		if (crypto_memneq(cryp->tag_in, cryp->tag_out, cryp->authsize))
			return -EBADMSG;
	}

	return 0;
}

static void starfive_aes_finish_req(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	int err = cryp->err;

	if (!err && cryp->authsize)
		err = starfive_aes_read_authtag(ctx);

	if (!err && ((cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_CBC ||
		     (cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_CTR))
		starfive_aes_get_iv(cryp, (void *)cryp->req.sreq->iv);

	if (cryp->authsize)
		crypto_finalize_aead_request(cryp->engine, cryp->req.areq, err);
	else
		crypto_finalize_skcipher_request(cryp->engine, cryp->req.sreq,
						 err);
}

static int starfive_aes_gcm_write_adata(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	struct starfive_cryp_request_ctx *rctx = ctx->rctx;
	u32 *buffer;
	int total_len, loop;

	total_len = ALIGN(cryp->assoclen, AES_BLOCK_SIZE) / sizeof(unsigned int);
	buffer = (u32 *)rctx->adata;

	for (loop = 0; loop < total_len; loop += 4) {
		writel(*buffer, cryp->base + STARFIVE_AES_NONCE0);
		buffer++;
		writel(*buffer, cryp->base + STARFIVE_AES_NONCE1);
		buffer++;
		writel(*buffer, cryp->base + STARFIVE_AES_NONCE2);
		buffer++;
		writel(*buffer, cryp->base + STARFIVE_AES_NONCE3);
		buffer++;
	}

	if (starfive_aes_wait_gcmdone(cryp))
		return dev_err_probe(cryp->dev, -ETIMEDOUT,
				     "Timeout processing gcm aad block");

	return 0;
}

static int starfive_aes_ccm_write_adata(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = ctx->cryp;
	struct starfive_cryp_request_ctx *rctx = ctx->rctx;
	u32 *buffer;
	u8 *ci;
	int total_len, loop;

	total_len = cryp->assoclen;

	ci = rctx->adata;
	writeb(*ci, cryp->base + STARFIVE_AES_AESDIO0R);
	ci++;
	writeb(*ci, cryp->base + STARFIVE_AES_AESDIO0R);
	ci++;
	total_len -= 2;
	buffer = (u32 *)ci;

	for (loop = 0; loop < 3; loop++, buffer++)
		writel(*buffer, cryp->base + STARFIVE_AES_AESDIO0R);

	total_len -= 12;

	while (total_len > 0) {
		for (loop = 0; loop < AES_BLOCK_32; loop++, buffer++)
			writel(*buffer, cryp->base + STARFIVE_AES_AESDIO0R);

		total_len -= AES_BLOCK_SIZE;
	}

	if (starfive_aes_wait_busy(cryp))
		return dev_err_probe(cryp->dev, -ETIMEDOUT,
				     "Timeout processing ccm aad block");

	return 0;
}

static void starfive_aes_dma_done(void *param)
{
	struct starfive_cryp_dev *cryp = param;

	complete(&cryp->dma_done);
}

static void starfive_aes_dma_init(struct starfive_cryp_dev *cryp)
{
	cryp->cfg_in.direction = DMA_MEM_TO_DEV;
	cryp->cfg_in.src_addr_width = DMA_SLAVE_BUSWIDTH_16_BYTES;
	cryp->cfg_in.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cryp->cfg_in.src_maxburst = cryp->dma_maxburst;
	cryp->cfg_in.dst_maxburst = cryp->dma_maxburst;
	cryp->cfg_in.dst_addr = cryp->phys_base + STARFIVE_ALG_FIFO_OFFSET;

	dmaengine_slave_config(cryp->tx, &cryp->cfg_in);

	cryp->cfg_out.direction = DMA_DEV_TO_MEM;
	cryp->cfg_out.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cryp->cfg_out.dst_addr_width = DMA_SLAVE_BUSWIDTH_16_BYTES;
	cryp->cfg_out.src_maxburst = 4;
	cryp->cfg_out.dst_maxburst = 4;
	cryp->cfg_out.src_addr = cryp->phys_base + STARFIVE_ALG_FIFO_OFFSET;

	dmaengine_slave_config(cryp->rx, &cryp->cfg_out);

	init_completion(&cryp->dma_done);
}

static int starfive_aes_dma_xfer(struct starfive_cryp_dev *cryp,
				 struct scatterlist *src,
				 struct scatterlist *dst,
				 int len)
{
	struct dma_async_tx_descriptor *in_desc, *out_desc;
	union starfive_alg_cr alg_cr;
	int ret = 0, in_save, out_save;

	alg_cr.v = 0;
	alg_cr.start = 1;
	alg_cr.aes_dma_en = 1;
	writel(alg_cr.v, cryp->base + STARFIVE_ALG_CR_OFFSET);

	in_save = sg_dma_len(src);
	out_save = sg_dma_len(dst);

	writel(ALIGN(len, AES_BLOCK_SIZE), cryp->base + STARFIVE_DMA_IN_LEN_OFFSET);
	writel(ALIGN(len, AES_BLOCK_SIZE), cryp->base + STARFIVE_DMA_OUT_LEN_OFFSET);

	sg_dma_len(src) = ALIGN(len, AES_BLOCK_SIZE);
	sg_dma_len(dst) = ALIGN(len, AES_BLOCK_SIZE);

	out_desc = dmaengine_prep_slave_sg(cryp->rx, dst, 1, DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!out_desc) {
		ret = -EINVAL;
		goto dma_err;
	}

	out_desc->callback = starfive_aes_dma_done;
	out_desc->callback_param = cryp;

	reinit_completion(&cryp->dma_done);
	dmaengine_submit(out_desc);
	dma_async_issue_pending(cryp->rx);

	in_desc = dmaengine_prep_slave_sg(cryp->tx, src, 1, DMA_MEM_TO_DEV,
					  DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!in_desc) {
		ret = -EINVAL;
		goto dma_err;
	}

	dmaengine_submit(in_desc);
	dma_async_issue_pending(cryp->tx);

	if (!wait_for_completion_timeout(&cryp->dma_done,
					 msecs_to_jiffies(1000)))
		ret = -ETIMEDOUT;

dma_err:
	sg_dma_len(src) = in_save;
	sg_dma_len(dst) = out_save;

	alg_cr.v = 0;
	alg_cr.clear = 1;
	writel(alg_cr.v, cryp->base + STARFIVE_ALG_CR_OFFSET);

	return ret;
}

static int starfive_aes_map_sg(struct starfive_cryp_dev *cryp,
			       struct scatterlist *src,
			       struct scatterlist *dst)
{
	struct scatterlist *stsg, *dtsg;
	struct scatterlist _src[2], _dst[2];
	unsigned int remain = cryp->total_in;
	unsigned int len, src_nents, dst_nents;
	int ret;

	if (src == dst) {
		for (stsg = src, dtsg = dst; remain > 0;
		     stsg = sg_next(stsg), dtsg = sg_next(dtsg)) {
			src_nents = dma_map_sg(cryp->dev, stsg, 1, DMA_BIDIRECTIONAL);
			if (src_nents == 0)
				return -ENOMEM;

			dst_nents = src_nents;
			len = min(sg_dma_len(stsg), remain);

			ret = starfive_aes_dma_xfer(cryp, stsg, dtsg, len);
			dma_unmap_sg(cryp->dev, stsg, 1, DMA_BIDIRECTIONAL);
			if (ret)
				return ret;

			remain -= len;
		}
	} else {
		for (stsg = src, dtsg = dst;;) {
			src_nents = dma_map_sg(cryp->dev, stsg, 1, DMA_TO_DEVICE);
			if (src_nents == 0)
				return -ENOMEM;

			dst_nents = dma_map_sg(cryp->dev, dtsg, 1, DMA_FROM_DEVICE);
			if (dst_nents == 0)
				return -ENOMEM;

			len = min(sg_dma_len(stsg), sg_dma_len(dtsg));
			len = min(len, remain);

			ret = starfive_aes_dma_xfer(cryp, stsg, dtsg, len);
			dma_unmap_sg(cryp->dev, stsg, 1, DMA_TO_DEVICE);
			dma_unmap_sg(cryp->dev, dtsg, 1, DMA_FROM_DEVICE);
			if (ret)
				return ret;

			remain -= len;
			if (remain == 0)
				break;

			if (sg_dma_len(stsg) - len) {
				stsg = scatterwalk_ffwd(_src, stsg, len);
				dtsg = sg_next(dtsg);
			} else if (sg_dma_len(dtsg) - len) {
				dtsg = scatterwalk_ffwd(_dst, dtsg, len);
				stsg = sg_next(stsg);
			} else {
				stsg = sg_next(stsg);
				dtsg = sg_next(dtsg);
			}
		}
	}

	return 0;
}

static int starfive_aes_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req =
		container_of(areq, struct skcipher_request, base);
	struct starfive_cryp_ctx *ctx =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct starfive_cryp_request_ctx *rctx = skcipher_request_ctx(req);
	struct starfive_cryp_dev *cryp = ctx->cryp;
	int ret;

	cryp->req.sreq = req;
	cryp->total_in = req->cryptlen;
	cryp->total_out = req->cryptlen;
	cryp->assoclen = 0;
	cryp->authsize = 0;

	rctx->in_sg = req->src;
	rctx->out_sg = req->dst;

	ctx->rctx = rctx;

	ret = starfive_aes_hw_init(ctx);
	if (ret)
		return ret;

	if (!cryp->total_in)
		goto finish_req;

	starfive_aes_dma_init(cryp);

	ret = starfive_aes_map_sg(cryp, rctx->in_sg, rctx->out_sg);
	if (ret)
		return ret;

finish_req:
	starfive_aes_finish_req(ctx);

	return 0;
}

static int starfive_aes_init_tfm(struct crypto_skcipher *tfm,
				 const char *alg_name)
{
	struct starfive_cryp_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->cryp = starfive_cryp_find_dev(ctx);
	if (!ctx->cryp)
		return -ENODEV;

	ctx->skcipher_fbk = crypto_alloc_skcipher(alg_name, 0,
						  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->skcipher_fbk))
		return dev_err_probe(ctx->cryp->dev, PTR_ERR(ctx->skcipher_fbk),
				     "%s() failed to allocate fallback for %s\n",
				     __func__, alg_name);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct starfive_cryp_request_ctx) +
				    crypto_skcipher_reqsize(ctx->skcipher_fbk));

	return 0;
}

static void starfive_aes_exit_tfm(struct crypto_skcipher *tfm)
{
	struct starfive_cryp_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->skcipher_fbk);
}

static int starfive_aes_aead_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req =
		container_of(areq, struct aead_request, base);
	struct starfive_cryp_ctx *ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct starfive_cryp_dev *cryp = ctx->cryp;
	struct starfive_cryp_request_ctx *rctx = aead_request_ctx(req);
	struct scatterlist _src[2], _dst[2];
	int ret;

	cryp->req.areq = req;
	cryp->assoclen = req->assoclen;
	cryp->authsize = crypto_aead_authsize(crypto_aead_reqtfm(req));

	rctx->in_sg = scatterwalk_ffwd(_src, req->src, cryp->assoclen);
	if (req->src == req->dst)
		rctx->out_sg = rctx->in_sg;
	else
		rctx->out_sg = scatterwalk_ffwd(_dst, req->dst, cryp->assoclen);

	if (is_encrypt(cryp)) {
		cryp->total_in = req->cryptlen;
		cryp->total_out = req->cryptlen;
	} else {
		cryp->total_in = req->cryptlen - cryp->authsize;
		cryp->total_out = cryp->total_in;
		scatterwalk_map_and_copy(cryp->tag_in, req->src,
					 cryp->total_in + cryp->assoclen,
					 cryp->authsize, 0);
	}

	if (cryp->assoclen) {
		rctx->adata = kzalloc(cryp->assoclen + AES_BLOCK_SIZE, GFP_KERNEL);
		if (!rctx->adata)
			return -ENOMEM;

		if (sg_copy_to_buffer(req->src, sg_nents_for_len(req->src, cryp->assoclen),
				      rctx->adata, cryp->assoclen) != cryp->assoclen)
			return -EINVAL;
	}

	if (cryp->total_in)
		sg_zero_buffer(rctx->in_sg, sg_nents(rctx->in_sg),
			       sg_dma_len(rctx->in_sg) - cryp->total_in,
			       cryp->total_in);

	ctx->rctx = rctx;

	ret = starfive_aes_hw_init(ctx);
	if (ret)
		return ret;

	if (!cryp->assoclen)
		goto write_text;

	if ((cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_CCM)
		ret = starfive_aes_ccm_write_adata(ctx);
	else
		ret = starfive_aes_gcm_write_adata(ctx);

	kfree(rctx->adata);

	if (ret)
		return ret;

write_text:
	if (!cryp->total_in)
		goto finish_req;

	starfive_aes_dma_init(cryp);

	ret = starfive_aes_map_sg(cryp, rctx->in_sg, rctx->out_sg);
	if (ret)
		return ret;

finish_req:
	starfive_aes_finish_req(ctx);
	return 0;
}

static int starfive_aes_aead_init_tfm(struct crypto_aead *tfm,
				      const char *alg_name)
{
	struct starfive_cryp_ctx *ctx = crypto_aead_ctx(tfm);

	ctx->cryp = starfive_cryp_find_dev(ctx);
	if (!ctx->cryp)
		return -ENODEV;

	ctx->aead_fbk = crypto_alloc_aead(alg_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->aead_fbk))
		return dev_err_probe(ctx->cryp->dev, PTR_ERR(ctx->aead_fbk),
				     "%s() failed to allocate fallback for %s\n",
				     __func__, alg_name);

	crypto_aead_set_reqsize(tfm, sizeof(struct starfive_cryp_request_ctx) +
				crypto_aead_reqsize(ctx->aead_fbk));

	return 0;
}

static void starfive_aes_aead_exit_tfm(struct crypto_aead *tfm)
{
	struct starfive_cryp_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->aead_fbk);
}

static bool starfive_aes_check_unaligned(struct starfive_cryp_dev *cryp,
					 struct scatterlist *src,
					 struct scatterlist *dst)
{
	struct scatterlist *tsg;
	int i;

	for_each_sg(src, tsg, sg_nents(src), i)
		if (!IS_ALIGNED(tsg->offset, sizeof(u32)) ||
		    (!IS_ALIGNED(tsg->length, AES_BLOCK_SIZE) &&
		     !sg_is_last(tsg)))
			return true;

	if (src != dst)
		for_each_sg(dst, tsg, sg_nents(dst), i)
			if (!IS_ALIGNED(tsg->offset, sizeof(u32)) ||
			    (!IS_ALIGNED(tsg->length, AES_BLOCK_SIZE) &&
			     !sg_is_last(tsg)))
				return true;

	return false;
}

static int starfive_aes_do_fallback(struct skcipher_request *req, bool enc)
{
	struct starfive_cryp_ctx *ctx =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct skcipher_request *subreq = skcipher_request_ctx(req);

	skcipher_request_set_tfm(subreq, ctx->skcipher_fbk);
	skcipher_request_set_callback(subreq, req->base.flags,
				      req->base.complete,
				      req->base.data);
	skcipher_request_set_crypt(subreq, req->src, req->dst,
				   req->cryptlen, req->iv);

	return enc ? crypto_skcipher_encrypt(subreq) :
		     crypto_skcipher_decrypt(subreq);
}

static int starfive_aes_crypt(struct skcipher_request *req, unsigned long flags)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct starfive_cryp_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct starfive_cryp_dev *cryp = ctx->cryp;
	unsigned int blocksize_align = crypto_skcipher_blocksize(tfm) - 1;

	cryp->flags = flags;

	if ((cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_ECB ||
	    (cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_CBC)
		if (req->cryptlen & blocksize_align)
			return -EINVAL;

	if (starfive_aes_check_unaligned(cryp, req->src, req->dst))
		return starfive_aes_do_fallback(req, is_encrypt(cryp));

	return crypto_transfer_skcipher_request_to_engine(cryp->engine, req);
}

static int starfive_aes_aead_do_fallback(struct aead_request *req, bool enc)
{
	struct starfive_cryp_ctx *ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct aead_request *subreq = aead_request_ctx(req);

	aead_request_set_tfm(subreq, ctx->aead_fbk);
	aead_request_set_callback(subreq, req->base.flags,
				  req->base.complete,
				  req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	return enc ? crypto_aead_encrypt(subreq) :
		     crypto_aead_decrypt(subreq);
}

static int starfive_aes_aead_crypt(struct aead_request *req, unsigned long flags)
{
	struct starfive_cryp_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct starfive_cryp_dev *cryp = ctx->cryp;
	struct scatterlist *src, *dst, _src[2], _dst[2];

	cryp->flags = flags;

	/* aes-ccm does not support tag verification for non-aligned text,
	 * use fallback for ccm decryption instead.
	 */
	if (((cryp->flags & FLG_MODE_MASK) == STARFIVE_AES_MODE_CCM) &&
	    !is_encrypt(cryp))
		return starfive_aes_aead_do_fallback(req, 0);

	src = scatterwalk_ffwd(_src, req->src, req->assoclen);

	if (req->src == req->dst)
		dst = src;
	else
		dst = scatterwalk_ffwd(_dst, req->dst, req->assoclen);

	if (starfive_aes_check_unaligned(cryp, src, dst))
		return starfive_aes_aead_do_fallback(req, is_encrypt(cryp));

	return crypto_transfer_aead_request_to_engine(cryp->engine, req);
}

static int starfive_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			       unsigned int keylen)
{
	struct starfive_cryp_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (!key || !keylen)
		return -EINVAL;

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return crypto_skcipher_setkey(ctx->skcipher_fbk, key, keylen);
}

static int starfive_aes_aead_setkey(struct crypto_aead *tfm, const u8 *key,
				    unsigned int keylen)
{
	struct starfive_cryp_ctx *ctx = crypto_aead_ctx(tfm);

	if (!key || !keylen)
		return -EINVAL;

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return crypto_aead_setkey(ctx->aead_fbk, key, keylen);
}

static int starfive_aes_gcm_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	struct starfive_cryp_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	ret = crypto_gcm_check_authsize(authsize);
	if (ret)
		return ret;

	return crypto_aead_setauthsize(ctx->aead_fbk, authsize);
}

static int starfive_aes_ccm_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	struct starfive_cryp_ctx *ctx = crypto_aead_ctx(tfm);

	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return crypto_aead_setauthsize(ctx->aead_fbk, authsize);
}

static int starfive_aes_ecb_encrypt(struct skcipher_request *req)
{
	return starfive_aes_crypt(req, STARFIVE_AES_MODE_ECB | FLG_ENCRYPT);
}

static int starfive_aes_ecb_decrypt(struct skcipher_request *req)
{
	return starfive_aes_crypt(req, STARFIVE_AES_MODE_ECB);
}

static int starfive_aes_cbc_encrypt(struct skcipher_request *req)
{
	return starfive_aes_crypt(req, STARFIVE_AES_MODE_CBC | FLG_ENCRYPT);
}

static int starfive_aes_cbc_decrypt(struct skcipher_request *req)
{
	return starfive_aes_crypt(req, STARFIVE_AES_MODE_CBC);
}

static int starfive_aes_ctr_encrypt(struct skcipher_request *req)
{
	return starfive_aes_crypt(req, STARFIVE_AES_MODE_CTR | FLG_ENCRYPT);
}

static int starfive_aes_ctr_decrypt(struct skcipher_request *req)
{
	return starfive_aes_crypt(req, STARFIVE_AES_MODE_CTR);
}

static int starfive_aes_gcm_encrypt(struct aead_request *req)
{
	return starfive_aes_aead_crypt(req, STARFIVE_AES_MODE_GCM | FLG_ENCRYPT);
}

static int starfive_aes_gcm_decrypt(struct aead_request *req)
{
	return starfive_aes_aead_crypt(req, STARFIVE_AES_MODE_GCM);
}

static int starfive_aes_ccm_encrypt(struct aead_request *req)
{
	int ret;

	ret = starfive_aes_ccm_check_iv(req->iv);
	if (ret)
		return ret;

	return starfive_aes_aead_crypt(req, STARFIVE_AES_MODE_CCM | FLG_ENCRYPT);
}

static int starfive_aes_ccm_decrypt(struct aead_request *req)
{
	int ret;

	ret = starfive_aes_ccm_check_iv(req->iv);
	if (ret)
		return ret;

	return starfive_aes_aead_crypt(req, STARFIVE_AES_MODE_CCM);
}

static int starfive_aes_ecb_init_tfm(struct crypto_skcipher *tfm)
{
	return starfive_aes_init_tfm(tfm, "ecb(aes-generic)");
}

static int starfive_aes_cbc_init_tfm(struct crypto_skcipher *tfm)
{
	return starfive_aes_init_tfm(tfm, "cbc(aes-generic)");
}

static int starfive_aes_ctr_init_tfm(struct crypto_skcipher *tfm)
{
	return starfive_aes_init_tfm(tfm, "ctr(aes-generic)");
}

static int starfive_aes_ccm_init_tfm(struct crypto_aead *tfm)
{
	return starfive_aes_aead_init_tfm(tfm, "ccm_base(ctr(aes-generic),cbcmac(aes-generic))");
}

static int starfive_aes_gcm_init_tfm(struct crypto_aead *tfm)
{
	return starfive_aes_aead_init_tfm(tfm, "gcm_base(ctr(aes-generic),ghash-generic)");
}

static struct skcipher_engine_alg skcipher_algs[] = {
{
	.base.init			= starfive_aes_ecb_init_tfm,
	.base.exit			= starfive_aes_exit_tfm,
	.base.setkey			= starfive_aes_setkey,
	.base.encrypt			= starfive_aes_ecb_encrypt,
	.base.decrypt			= starfive_aes_ecb_decrypt,
	.base.min_keysize		= AES_MIN_KEY_SIZE,
	.base.max_keysize		= AES_MAX_KEY_SIZE,
	.base.base = {
		.cra_name		= "ecb(aes)",
		.cra_driver_name	= "starfive-ecb-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct starfive_cryp_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
	.op = {
		.do_one_request = starfive_aes_do_one_req,
	},
}, {
	.base.init			= starfive_aes_cbc_init_tfm,
	.base.exit			= starfive_aes_exit_tfm,
	.base.setkey			= starfive_aes_setkey,
	.base.encrypt			= starfive_aes_cbc_encrypt,
	.base.decrypt			= starfive_aes_cbc_decrypt,
	.base.min_keysize		= AES_MIN_KEY_SIZE,
	.base.max_keysize		= AES_MAX_KEY_SIZE,
	.base.ivsize			= AES_BLOCK_SIZE,
	.base.base = {
		.cra_name		= "cbc(aes)",
		.cra_driver_name	= "starfive-cbc-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct starfive_cryp_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
	.op = {
		.do_one_request = starfive_aes_do_one_req,
	},
}, {
	.base.init			= starfive_aes_ctr_init_tfm,
	.base.exit			= starfive_aes_exit_tfm,
	.base.setkey			= starfive_aes_setkey,
	.base.encrypt			= starfive_aes_ctr_encrypt,
	.base.decrypt			= starfive_aes_ctr_decrypt,
	.base.min_keysize		= AES_MIN_KEY_SIZE,
	.base.max_keysize		= AES_MAX_KEY_SIZE,
	.base.ivsize			= AES_BLOCK_SIZE,
	.base.base = {
		.cra_name		= "ctr(aes)",
		.cra_driver_name	= "starfive-ctr-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct starfive_cryp_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
	.op = {
		.do_one_request = starfive_aes_do_one_req,
	},
},
};

static struct aead_engine_alg aead_algs[] = {
{
	.base.setkey			= starfive_aes_aead_setkey,
	.base.setauthsize		= starfive_aes_gcm_setauthsize,
	.base.encrypt			= starfive_aes_gcm_encrypt,
	.base.decrypt			= starfive_aes_gcm_decrypt,
	.base.init			= starfive_aes_gcm_init_tfm,
	.base.exit			= starfive_aes_aead_exit_tfm,
	.base.ivsize			= GCM_AES_IV_SIZE,
	.base.maxauthsize		= AES_BLOCK_SIZE,
	.base.base = {
		.cra_name               = "gcm(aes)",
		.cra_driver_name        = "starfive-gcm-aes",
		.cra_priority           = 200,
		.cra_flags              = CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize          = 1,
		.cra_ctxsize            = sizeof(struct starfive_cryp_ctx),
		.cra_alignmask          = 0xf,
		.cra_module             = THIS_MODULE,
	},
	.op = {
		.do_one_request = starfive_aes_aead_do_one_req,
	},
}, {
	.base.setkey			= starfive_aes_aead_setkey,
	.base.setauthsize		= starfive_aes_ccm_setauthsize,
	.base.encrypt			= starfive_aes_ccm_encrypt,
	.base.decrypt			= starfive_aes_ccm_decrypt,
	.base.init			= starfive_aes_ccm_init_tfm,
	.base.exit			= starfive_aes_aead_exit_tfm,
	.base.ivsize			= AES_BLOCK_SIZE,
	.base.maxauthsize		= AES_BLOCK_SIZE,
	.base.base = {
		.cra_name		= "ccm(aes)",
		.cra_driver_name	= "starfive-ccm-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct starfive_cryp_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
	.op = {
		.do_one_request = starfive_aes_aead_do_one_req,
	},
},
};

int starfive_aes_register_algs(void)
{
	int ret;

	ret = crypto_engine_register_skciphers(skcipher_algs, ARRAY_SIZE(skcipher_algs));
	if (ret)
		return ret;

	ret = crypto_engine_register_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	if (ret)
		crypto_engine_unregister_skciphers(skcipher_algs, ARRAY_SIZE(skcipher_algs));

	return ret;
}

void starfive_aes_unregister_algs(void)
{
	crypto_engine_unregister_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	crypto_engine_unregister_skciphers(skcipher_algs, ARRAY_SIZE(skcipher_algs));
}
