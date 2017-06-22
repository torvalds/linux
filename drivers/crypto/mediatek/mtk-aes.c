/*
 * Cryptographic API.
 *
 * Driver for EIP97 AES acceleration.
 *
 * Copyright (c) 2016 Ryder Lee <ryder.lee@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Some ideas are from atmel-aes.c drivers.
 */

#include <crypto/aes.h>
#include "mtk-platform.h"

#define AES_QUEUE_SIZE		512
#define AES_BUF_ORDER		2
#define AES_BUF_SIZE		((PAGE_SIZE << AES_BUF_ORDER) \
				& ~(AES_BLOCK_SIZE - 1))

/* AES command token size */
#define AES_CT_SIZE_ECB		2
#define AES_CT_SIZE_CBC		3
#define AES_CT_SIZE_CTR		3
#define AES_CT_SIZE_GCM_OUT	5
#define AES_CT_SIZE_GCM_IN	6
#define AES_CT_CTRL_HDR		cpu_to_le32(0x00220000)

/* AES-CBC/ECB/CTR command token */
#define AES_CMD0		cpu_to_le32(0x05000000)
#define AES_CMD1		cpu_to_le32(0x2d060000)
#define AES_CMD2		cpu_to_le32(0xe4a63806)
/* AES-GCM command token */
#define AES_GCM_CMD0		cpu_to_le32(0x0b000000)
#define AES_GCM_CMD1		cpu_to_le32(0xa0800000)
#define AES_GCM_CMD2		cpu_to_le32(0x25000010)
#define AES_GCM_CMD3		cpu_to_le32(0x0f020000)
#define AES_GCM_CMD4		cpu_to_le32(0x21e60000)
#define AES_GCM_CMD5		cpu_to_le32(0x40e60000)
#define AES_GCM_CMD6		cpu_to_le32(0xd0070000)

/* AES transform information word 0 fields */
#define AES_TFM_BASIC_OUT	cpu_to_le32(0x4 << 0)
#define AES_TFM_BASIC_IN	cpu_to_le32(0x5 << 0)
#define AES_TFM_GCM_OUT		cpu_to_le32(0x6 << 0)
#define AES_TFM_GCM_IN		cpu_to_le32(0xf << 0)
#define AES_TFM_SIZE(x)		cpu_to_le32((x) << 8)
#define AES_TFM_128BITS		cpu_to_le32(0xb << 16)
#define AES_TFM_192BITS		cpu_to_le32(0xd << 16)
#define AES_TFM_256BITS		cpu_to_le32(0xf << 16)
/* AES transform information word 1 fields */
#define AES_TFM_ECB		cpu_to_le32(0x0 << 0)
#define AES_TFM_CBC		cpu_to_le32(0x1 << 0)
#define AES_TFM_CTR_INIT	cpu_to_le32(0x2 << 0)	/* init counter to 1 */
#define AES_TFM_CTR_LOAD	cpu_to_le32(0x6 << 0)	/* load/reuse counter */
#define AES_TFM_3IV		cpu_to_le32(0x7 << 5)	/* using IV 0-2 */
#define AES_TFM_FULL_IV		cpu_to_le32(0xf << 5)	/* using IV 0-3 */
#define AES_TFM_IV_CTR_MODE	cpu_to_le32(0x1 << 10)
#define AES_TFM_ENC_HASH	cpu_to_le32(0x1 << 17)
#define AES_TFM_GHASH_DIG	cpu_to_le32(0x2 << 21)
#define AES_TFM_GHASH		cpu_to_le32(0x4 << 23)

/* AES flags */
#define AES_FLAGS_ECB		BIT(0)
#define AES_FLAGS_CBC		BIT(1)
#define AES_FLAGS_CTR		BIT(2)
#define AES_FLAGS_GCM		BIT(3)
#define AES_FLAGS_ENCRYPT	BIT(4)
#define AES_FLAGS_BUSY		BIT(5)

/**
 * Command token(CT) is a set of hardware instructions that
 * are used to control engine's processing flow of AES.
 *
 * Transform information(TFM) is used to define AES state and
 * contains all keys and initial vectors.
 *
 * The engine requires CT and TFM to do:
 * - Commands decoding and control of the engine's data path.
 * - Coordinating hardware data fetch and store operations.
 * - Result token construction and output.
 *
 * Memory map of GCM's TFM:
 * /-----------\
 * |  AES KEY  | 128/196/256 bits
 * |-----------|
 * |  HASH KEY | a string 128 zero bits encrypted using the block cipher
 * |-----------|
 * |    IVs    | 4 * 4 bytes
 * \-----------/
 */
struct mtk_aes_ct {
	__le32 cmd[AES_CT_SIZE_GCM_IN];
};

struct mtk_aes_tfm {
	__le32 ctrl[2];
	__le32 state[SIZE_IN_WORDS(AES_KEYSIZE_256 + AES_BLOCK_SIZE * 2)];
};

struct mtk_aes_reqctx {
	u64 mode;
};

struct mtk_aes_base_ctx {
	struct mtk_cryp *cryp;
	u32 keylen;
	mtk_aes_fn start;

	struct mtk_aes_ct ct;
	dma_addr_t ct_dma;
	struct mtk_aes_tfm tfm;
	dma_addr_t tfm_dma;

	__le32 ct_hdr;
	u32 ct_size;
};

struct mtk_aes_ctx {
	struct mtk_aes_base_ctx	base;
};

struct mtk_aes_ctr_ctx {
	struct mtk_aes_base_ctx base;

	u32	iv[AES_BLOCK_SIZE / sizeof(u32)];
	size_t offset;
	struct scatterlist src[2];
	struct scatterlist dst[2];
};

struct mtk_aes_gcm_ctx {
	struct mtk_aes_base_ctx base;

	u32 authsize;
	size_t textlen;

	struct crypto_skcipher *ctr;
};

struct mtk_aes_gcm_setkey_result {
	int err;
	struct completion completion;
};

struct mtk_aes_drv {
	struct list_head dev_list;
	/* Device list lock */
	spinlock_t lock;
};

static struct mtk_aes_drv mtk_aes = {
	.dev_list = LIST_HEAD_INIT(mtk_aes.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(mtk_aes.lock),
};

static inline u32 mtk_aes_read(struct mtk_cryp *cryp, u32 offset)
{
	return readl_relaxed(cryp->base + offset);
}

static inline void mtk_aes_write(struct mtk_cryp *cryp,
				 u32 offset, u32 value)
{
	writel_relaxed(value, cryp->base + offset);
}

static struct mtk_cryp *mtk_aes_find_dev(struct mtk_aes_base_ctx *ctx)
{
	struct mtk_cryp *cryp = NULL;
	struct mtk_cryp *tmp;

	spin_lock_bh(&mtk_aes.lock);
	if (!ctx->cryp) {
		list_for_each_entry(tmp, &mtk_aes.dev_list, aes_list) {
			cryp = tmp;
			break;
		}
		ctx->cryp = cryp;
	} else {
		cryp = ctx->cryp;
	}
	spin_unlock_bh(&mtk_aes.lock);

	return cryp;
}

static inline size_t mtk_aes_padlen(size_t len)
{
	len &= AES_BLOCK_SIZE - 1;
	return len ? AES_BLOCK_SIZE - len : 0;
}

static bool mtk_aes_check_aligned(struct scatterlist *sg, size_t len,
				  struct mtk_aes_dma *dma)
{
	int nents;

	if (!IS_ALIGNED(len, AES_BLOCK_SIZE))
		return false;

	for (nents = 0; sg; sg = sg_next(sg), ++nents) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32)))
			return false;

		if (len <= sg->length) {
			if (!IS_ALIGNED(len, AES_BLOCK_SIZE))
				return false;

			dma->nents = nents + 1;
			dma->remainder = sg->length - len;
			sg->length = len;
			return true;
		}

		if (!IS_ALIGNED(sg->length, AES_BLOCK_SIZE))
			return false;

		len -= sg->length;
	}

	return false;
}

static inline void mtk_aes_set_mode(struct mtk_aes_rec *aes,
				    const struct mtk_aes_reqctx *rctx)
{
	/* Clear all but persistent flags and set request flags. */
	aes->flags = (aes->flags & AES_FLAGS_BUSY) | rctx->mode;
}

static inline void mtk_aes_restore_sg(const struct mtk_aes_dma *dma)
{
	struct scatterlist *sg = dma->sg;
	int nents = dma->nents;

	if (!dma->remainder)
		return;

	while (--nents > 0 && sg)
		sg = sg_next(sg);

	if (!sg)
		return;

	sg->length += dma->remainder;
}

/*
 * Write descriptors for processing. This will configure the engine, load
 * the transform information and then start the packet processing.
 */
static int mtk_aes_xmit(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_ring *ring = cryp->ring[aes->id];
	struct mtk_desc *cmd = NULL, *res = NULL;
	struct scatterlist *ssg = aes->src.sg, *dsg = aes->dst.sg;
	u32 slen = aes->src.sg_len, dlen = aes->dst.sg_len;
	int nents;

	/* Write command descriptors */
	for (nents = 0; nents < slen; ++nents, ssg = sg_next(ssg)) {
		cmd = ring->cmd_base + ring->cmd_pos;
		cmd->hdr = MTK_DESC_BUF_LEN(ssg->length);
		cmd->buf = cpu_to_le32(sg_dma_address(ssg));

		if (nents == 0) {
			cmd->hdr |= MTK_DESC_FIRST |
				    MTK_DESC_CT_LEN(aes->ctx->ct_size);
			cmd->ct = cpu_to_le32(aes->ctx->ct_dma);
			cmd->ct_hdr = aes->ctx->ct_hdr;
			cmd->tfm = cpu_to_le32(aes->ctx->tfm_dma);
		}

		if (++ring->cmd_pos == MTK_DESC_NUM)
			ring->cmd_pos = 0;
	}
	cmd->hdr |= MTK_DESC_LAST;

	/* Prepare result descriptors */
	for (nents = 0; nents < dlen; ++nents, dsg = sg_next(dsg)) {
		res = ring->res_base + ring->res_pos;
		res->hdr = MTK_DESC_BUF_LEN(dsg->length);
		res->buf = cpu_to_le32(sg_dma_address(dsg));

		if (nents == 0)
			res->hdr |= MTK_DESC_FIRST;

		if (++ring->res_pos == MTK_DESC_NUM)
			ring->res_pos = 0;
	}
	res->hdr |= MTK_DESC_LAST;

	/* Prepare enough space for authenticated tag */
	if (aes->flags & AES_FLAGS_GCM)
		res->hdr += AES_BLOCK_SIZE;

	/*
	 * Make sure that all changes to the DMA ring are done before we
	 * start engine.
	 */
	wmb();
	/* Start DMA transfer */
	mtk_aes_write(cryp, RDR_PREP_COUNT(aes->id), MTK_DESC_CNT(dlen));
	mtk_aes_write(cryp, CDR_PREP_COUNT(aes->id), MTK_DESC_CNT(slen));

	return -EINPROGRESS;
}

static void mtk_aes_unmap(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_aes_base_ctx *ctx = aes->ctx;

	dma_unmap_single(cryp->dev, ctx->ct_dma, sizeof(ctx->ct),
			 DMA_TO_DEVICE);
	dma_unmap_single(cryp->dev, ctx->tfm_dma, sizeof(ctx->tfm),
			 DMA_TO_DEVICE);

	if (aes->src.sg == aes->dst.sg) {
		dma_unmap_sg(cryp->dev, aes->src.sg, aes->src.nents,
			     DMA_BIDIRECTIONAL);

		if (aes->src.sg != &aes->aligned_sg)
			mtk_aes_restore_sg(&aes->src);
	} else {
		dma_unmap_sg(cryp->dev, aes->dst.sg, aes->dst.nents,
			     DMA_FROM_DEVICE);

		if (aes->dst.sg != &aes->aligned_sg)
			mtk_aes_restore_sg(&aes->dst);

		dma_unmap_sg(cryp->dev, aes->src.sg, aes->src.nents,
			     DMA_TO_DEVICE);

		if (aes->src.sg != &aes->aligned_sg)
			mtk_aes_restore_sg(&aes->src);
	}

	if (aes->dst.sg == &aes->aligned_sg)
		sg_copy_from_buffer(aes->real_dst, sg_nents(aes->real_dst),
				    aes->buf, aes->total);
}

static int mtk_aes_map(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_aes_base_ctx *ctx = aes->ctx;

	ctx->ct_dma = dma_map_single(cryp->dev, &ctx->ct, sizeof(ctx->ct),
				     DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(cryp->dev, ctx->ct_dma)))
		return -EINVAL;

	ctx->tfm_dma = dma_map_single(cryp->dev, &ctx->tfm, sizeof(ctx->tfm),
				      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(cryp->dev, ctx->tfm_dma)))
		goto tfm_map_err;

	if (aes->src.sg == aes->dst.sg) {
		aes->src.sg_len = dma_map_sg(cryp->dev, aes->src.sg,
					     aes->src.nents,
					     DMA_BIDIRECTIONAL);
		aes->dst.sg_len = aes->src.sg_len;
		if (unlikely(!aes->src.sg_len))
			goto sg_map_err;
	} else {
		aes->src.sg_len = dma_map_sg(cryp->dev, aes->src.sg,
					     aes->src.nents, DMA_TO_DEVICE);
		if (unlikely(!aes->src.sg_len))
			goto sg_map_err;

		aes->dst.sg_len = dma_map_sg(cryp->dev, aes->dst.sg,
					     aes->dst.nents, DMA_FROM_DEVICE);
		if (unlikely(!aes->dst.sg_len)) {
			dma_unmap_sg(cryp->dev, aes->src.sg, aes->src.nents,
				     DMA_TO_DEVICE);
			goto sg_map_err;
		}
	}

	return mtk_aes_xmit(cryp, aes);

sg_map_err:
	dma_unmap_single(cryp->dev, ctx->tfm_dma, sizeof(ctx->tfm),
			 DMA_TO_DEVICE);
tfm_map_err:
	dma_unmap_single(cryp->dev, ctx->ct_dma, sizeof(ctx->ct),
			 DMA_TO_DEVICE);

	return -EINVAL;
}

/* Initialize transform information of CBC/ECB/CTR mode */
static void mtk_aes_info_init(struct mtk_cryp *cryp, struct mtk_aes_rec *aes,
			      size_t len)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(aes->areq);
	struct mtk_aes_base_ctx *ctx = aes->ctx;

	ctx->ct_hdr = AES_CT_CTRL_HDR | cpu_to_le32(len);
	ctx->ct.cmd[0] = AES_CMD0 | cpu_to_le32(len);
	ctx->ct.cmd[1] = AES_CMD1;

	if (aes->flags & AES_FLAGS_ENCRYPT)
		ctx->tfm.ctrl[0] = AES_TFM_BASIC_OUT;
	else
		ctx->tfm.ctrl[0] = AES_TFM_BASIC_IN;

	if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_128))
		ctx->tfm.ctrl[0] |= AES_TFM_128BITS;
	else if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_256))
		ctx->tfm.ctrl[0] |= AES_TFM_256BITS;
	else
		ctx->tfm.ctrl[0] |= AES_TFM_192BITS;

	if (aes->flags & AES_FLAGS_CBC) {
		const u32 *iv = (const u32 *)req->info;
		u32 *iv_state = ctx->tfm.state + ctx->keylen;
		int i;

		ctx->tfm.ctrl[0] |= AES_TFM_SIZE(ctx->keylen +
				    SIZE_IN_WORDS(AES_BLOCK_SIZE));
		ctx->tfm.ctrl[1] = AES_TFM_CBC | AES_TFM_FULL_IV;

		for (i = 0; i < SIZE_IN_WORDS(AES_BLOCK_SIZE); i++)
			iv_state[i] = cpu_to_le32(iv[i]);

		ctx->ct.cmd[2] = AES_CMD2;
		ctx->ct_size = AES_CT_SIZE_CBC;
	} else if (aes->flags & AES_FLAGS_ECB) {
		ctx->tfm.ctrl[0] |= AES_TFM_SIZE(ctx->keylen);
		ctx->tfm.ctrl[1] = AES_TFM_ECB;

		ctx->ct_size = AES_CT_SIZE_ECB;
	} else if (aes->flags & AES_FLAGS_CTR) {
		ctx->tfm.ctrl[0] |= AES_TFM_SIZE(ctx->keylen +
				    SIZE_IN_WORDS(AES_BLOCK_SIZE));
		ctx->tfm.ctrl[1] = AES_TFM_CTR_LOAD | AES_TFM_FULL_IV;

		ctx->ct.cmd[2] = AES_CMD2;
		ctx->ct_size = AES_CT_SIZE_CTR;
	}
}

static int mtk_aes_dma(struct mtk_cryp *cryp, struct mtk_aes_rec *aes,
		       struct scatterlist *src, struct scatterlist *dst,
		       size_t len)
{
	size_t padlen = 0;
	bool src_aligned, dst_aligned;

	aes->total = len;
	aes->src.sg = src;
	aes->dst.sg = dst;
	aes->real_dst = dst;

	src_aligned = mtk_aes_check_aligned(src, len, &aes->src);
	if (src == dst)
		dst_aligned = src_aligned;
	else
		dst_aligned = mtk_aes_check_aligned(dst, len, &aes->dst);

	if (!src_aligned || !dst_aligned) {
		padlen = mtk_aes_padlen(len);

		if (len + padlen > AES_BUF_SIZE)
			return -ENOMEM;

		if (!src_aligned) {
			sg_copy_to_buffer(src, sg_nents(src), aes->buf, len);
			aes->src.sg = &aes->aligned_sg;
			aes->src.nents = 1;
			aes->src.remainder = 0;
		}

		if (!dst_aligned) {
			aes->dst.sg = &aes->aligned_sg;
			aes->dst.nents = 1;
			aes->dst.remainder = 0;
		}

		sg_init_table(&aes->aligned_sg, 1);
		sg_set_buf(&aes->aligned_sg, aes->buf, len + padlen);
	}

	mtk_aes_info_init(cryp, aes, len + padlen);

	return mtk_aes_map(cryp, aes);
}

static int mtk_aes_handle_queue(struct mtk_cryp *cryp, u8 id,
				struct crypto_async_request *new_areq)
{
	struct mtk_aes_rec *aes = cryp->aes[id];
	struct crypto_async_request *areq, *backlog;
	struct mtk_aes_base_ctx *ctx;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&aes->lock, flags);
	if (new_areq)
		ret = crypto_enqueue_request(&aes->queue, new_areq);
	if (aes->flags & AES_FLAGS_BUSY) {
		spin_unlock_irqrestore(&aes->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&aes->queue);
	areq = crypto_dequeue_request(&aes->queue);
	if (areq)
		aes->flags |= AES_FLAGS_BUSY;
	spin_unlock_irqrestore(&aes->lock, flags);

	if (!areq)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	ctx = crypto_tfm_ctx(areq->tfm);

	aes->areq = areq;
	aes->ctx = ctx;

	return ctx->start(cryp, aes);
}

static int mtk_aes_complete(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	aes->flags &= ~AES_FLAGS_BUSY;
	aes->areq->complete(aes->areq, 0);

	/* Handle new request */
	return mtk_aes_handle_queue(cryp, aes->id, NULL);
}

static int mtk_aes_start(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(aes->areq);
	struct mtk_aes_reqctx *rctx = ablkcipher_request_ctx(req);

	mtk_aes_set_mode(aes, rctx);
	aes->resume = mtk_aes_complete;

	return mtk_aes_dma(cryp, aes, req->src, req->dst, req->nbytes);
}

static inline struct mtk_aes_ctr_ctx *
mtk_aes_ctr_ctx_cast(struct mtk_aes_base_ctx *ctx)
{
	return container_of(ctx, struct mtk_aes_ctr_ctx, base);
}

static int mtk_aes_ctr_transfer(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_aes_base_ctx *ctx = aes->ctx;
	struct mtk_aes_ctr_ctx *cctx = mtk_aes_ctr_ctx_cast(ctx);
	struct ablkcipher_request *req = ablkcipher_request_cast(aes->areq);
	struct scatterlist *src, *dst;
	int i;
	u32 start, end, ctr, blocks, *iv_state;
	size_t datalen;
	bool fragmented = false;

	/* Check for transfer completion. */
	cctx->offset += aes->total;
	if (cctx->offset >= req->nbytes)
		return mtk_aes_complete(cryp, aes);

	/* Compute data length. */
	datalen = req->nbytes - cctx->offset;
	blocks = DIV_ROUND_UP(datalen, AES_BLOCK_SIZE);
	ctr = be32_to_cpu(cctx->iv[3]);

	/* Check 32bit counter overflow. */
	start = ctr;
	end = start + blocks - 1;
	if (end < start) {
		ctr |= 0xffffffff;
		datalen = AES_BLOCK_SIZE * -start;
		fragmented = true;
	}

	/* Jump to offset. */
	src = scatterwalk_ffwd(cctx->src, req->src, cctx->offset);
	dst = ((req->src == req->dst) ? src :
	       scatterwalk_ffwd(cctx->dst, req->dst, cctx->offset));

	/* Write IVs into transform state buffer. */
	iv_state = ctx->tfm.state + ctx->keylen;
	for (i = 0; i < SIZE_IN_WORDS(AES_BLOCK_SIZE); i++)
		iv_state[i] = cpu_to_le32(cctx->iv[i]);

	if (unlikely(fragmented)) {
	/*
	 * Increment the counter manually to cope with the hardware
	 * counter overflow.
	 */
		cctx->iv[3] = cpu_to_be32(ctr);
		crypto_inc((u8 *)cctx->iv, AES_BLOCK_SIZE);
	}
	aes->resume = mtk_aes_ctr_transfer;

	return mtk_aes_dma(cryp, aes, src, dst, datalen);
}

static int mtk_aes_ctr_start(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_aes_ctr_ctx *cctx = mtk_aes_ctr_ctx_cast(aes->ctx);
	struct ablkcipher_request *req = ablkcipher_request_cast(aes->areq);
	struct mtk_aes_reqctx *rctx = ablkcipher_request_ctx(req);

	mtk_aes_set_mode(aes, rctx);

	memcpy(cctx->iv, req->info, AES_BLOCK_SIZE);
	cctx->offset = 0;
	aes->total = 0;

	return mtk_aes_ctr_transfer(cryp, aes);
}

/* Check and set the AES key to transform state buffer */
static int mtk_aes_setkey(struct crypto_ablkcipher *tfm,
			  const u8 *key, u32 keylen)
{
	struct mtk_aes_base_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	const u32 *aes_key = (const u32 *)key;
	u32 *key_state = ctx->tfm.state;
	int i;

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->keylen = SIZE_IN_WORDS(keylen);

	for (i = 0; i < ctx->keylen; i++)
		key_state[i] = cpu_to_le32(aes_key[i]);

	return 0;
}

static int mtk_aes_crypt(struct ablkcipher_request *req, u64 mode)
{
	struct mtk_aes_base_ctx *ctx;
	struct mtk_aes_reqctx *rctx;

	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));
	rctx = ablkcipher_request_ctx(req);
	rctx->mode = mode;

	return mtk_aes_handle_queue(ctx->cryp, !(mode & AES_FLAGS_ENCRYPT),
				    &req->base);
}

static int mtk_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_ECB);
}

static int mtk_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ECB);
}

static int mtk_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_CBC);
}

static int mtk_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_CBC);
}

static int mtk_aes_ctr_encrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_CTR);
}

static int mtk_aes_ctr_decrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_CTR);
}

static int mtk_aes_cra_init(struct crypto_tfm *tfm)
{
	struct mtk_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct mtk_cryp *cryp = NULL;

	cryp = mtk_aes_find_dev(&ctx->base);
	if (!cryp) {
		pr_err("can't find crypto device\n");
		return -ENODEV;
	}

	tfm->crt_ablkcipher.reqsize = sizeof(struct mtk_aes_reqctx);
	ctx->base.start = mtk_aes_start;
	return 0;
}

static int mtk_aes_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct mtk_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct mtk_cryp *cryp = NULL;

	cryp = mtk_aes_find_dev(&ctx->base);
	if (!cryp) {
		pr_err("can't find crypto device\n");
		return -ENODEV;
	}

	tfm->crt_ablkcipher.reqsize = sizeof(struct mtk_aes_reqctx);
	ctx->base.start = mtk_aes_ctr_start;
	return 0;
}

static struct crypto_alg aes_algs[] = {
{
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "cbc-aes-mtk",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_init		= mtk_aes_cra_init,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct mtk_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= mtk_aes_setkey,
		.encrypt	= mtk_aes_cbc_encrypt,
		.decrypt	= mtk_aes_cbc_decrypt,
		.ivsize		= AES_BLOCK_SIZE,
	}
},
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "ecb-aes-mtk",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_init		= mtk_aes_cra_init,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct mtk_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= mtk_aes_setkey,
		.encrypt	= mtk_aes_ecb_encrypt,
		.decrypt	= mtk_aes_ecb_decrypt,
	}
},
{
	.cra_name		= "ctr(aes)",
	.cra_driver_name	= "ctr-aes-mtk",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_init		= mtk_aes_ctr_cra_init,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct mtk_aes_ctr_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= mtk_aes_setkey,
		.encrypt	= mtk_aes_ctr_encrypt,
		.decrypt	= mtk_aes_ctr_decrypt,
	}
},
};

static inline struct mtk_aes_gcm_ctx *
mtk_aes_gcm_ctx_cast(struct mtk_aes_base_ctx *ctx)
{
	return container_of(ctx, struct mtk_aes_gcm_ctx, base);
}

/* Initialize transform information of GCM mode */
static void mtk_aes_gcm_info_init(struct mtk_cryp *cryp,
				  struct mtk_aes_rec *aes,
				  size_t len)
{
	struct aead_request *req = aead_request_cast(aes->areq);
	struct mtk_aes_base_ctx *ctx = aes->ctx;
	struct mtk_aes_gcm_ctx *gctx = mtk_aes_gcm_ctx_cast(ctx);
	const u32 *iv = (const u32 *)req->iv;
	u32 *iv_state = ctx->tfm.state + ctx->keylen +
			SIZE_IN_WORDS(AES_BLOCK_SIZE);
	u32 ivsize = crypto_aead_ivsize(crypto_aead_reqtfm(req));
	int i;

	ctx->ct_hdr = AES_CT_CTRL_HDR | len;

	ctx->ct.cmd[0] = AES_GCM_CMD0 | cpu_to_le32(req->assoclen);
	ctx->ct.cmd[1] = AES_GCM_CMD1 | cpu_to_le32(req->assoclen);
	ctx->ct.cmd[2] = AES_GCM_CMD2;
	ctx->ct.cmd[3] = AES_GCM_CMD3 | cpu_to_le32(gctx->textlen);

	if (aes->flags & AES_FLAGS_ENCRYPT) {
		ctx->ct.cmd[4] = AES_GCM_CMD4 | cpu_to_le32(gctx->authsize);
		ctx->ct_size = AES_CT_SIZE_GCM_OUT;
		ctx->tfm.ctrl[0] = AES_TFM_GCM_OUT;
	} else {
		ctx->ct.cmd[4] = AES_GCM_CMD5 | cpu_to_le32(gctx->authsize);
		ctx->ct.cmd[5] = AES_GCM_CMD6 | cpu_to_le32(gctx->authsize);
		ctx->ct_size = AES_CT_SIZE_GCM_IN;
		ctx->tfm.ctrl[0] = AES_TFM_GCM_IN;
	}

	if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_128))
		ctx->tfm.ctrl[0] |= AES_TFM_128BITS;
	else if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_256))
		ctx->tfm.ctrl[0] |= AES_TFM_256BITS;
	else
		ctx->tfm.ctrl[0] |= AES_TFM_192BITS;

	ctx->tfm.ctrl[0] |= AES_TFM_GHASH_DIG | AES_TFM_GHASH |
			    AES_TFM_SIZE(ctx->keylen + SIZE_IN_WORDS(
			    AES_BLOCK_SIZE + ivsize));
	ctx->tfm.ctrl[1] = AES_TFM_CTR_INIT | AES_TFM_IV_CTR_MODE |
			   AES_TFM_3IV | AES_TFM_ENC_HASH;

	for (i = 0; i < SIZE_IN_WORDS(ivsize); i++)
		iv_state[i] = cpu_to_le32(iv[i]);
}

static int mtk_aes_gcm_dma(struct mtk_cryp *cryp, struct mtk_aes_rec *aes,
			   struct scatterlist *src, struct scatterlist *dst,
			   size_t len)
{
	bool src_aligned, dst_aligned;

	aes->src.sg = src;
	aes->dst.sg = dst;
	aes->real_dst = dst;

	src_aligned = mtk_aes_check_aligned(src, len, &aes->src);
	if (src == dst)
		dst_aligned = src_aligned;
	else
		dst_aligned = mtk_aes_check_aligned(dst, len, &aes->dst);

	if (!src_aligned || !dst_aligned) {
		if (aes->total > AES_BUF_SIZE)
			return -ENOMEM;

		if (!src_aligned) {
			sg_copy_to_buffer(src, sg_nents(src), aes->buf, len);
			aes->src.sg = &aes->aligned_sg;
			aes->src.nents = 1;
			aes->src.remainder = 0;
		}

		if (!dst_aligned) {
			aes->dst.sg = &aes->aligned_sg;
			aes->dst.nents = 1;
			aes->dst.remainder = 0;
		}

		sg_init_table(&aes->aligned_sg, 1);
		sg_set_buf(&aes->aligned_sg, aes->buf, aes->total);
	}

	mtk_aes_gcm_info_init(cryp, aes, len);

	return mtk_aes_map(cryp, aes);
}

/* Todo: GMAC */
static int mtk_aes_gcm_start(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_aes_gcm_ctx *gctx = mtk_aes_gcm_ctx_cast(aes->ctx);
	struct aead_request *req = aead_request_cast(aes->areq);
	struct mtk_aes_reqctx *rctx = aead_request_ctx(req);
	u32 len = req->assoclen + req->cryptlen;

	mtk_aes_set_mode(aes, rctx);

	if (aes->flags & AES_FLAGS_ENCRYPT) {
		u32 tag[4];
		/* Compute total process length. */
		aes->total = len + gctx->authsize;
		/* Compute text length. */
		gctx->textlen = req->cryptlen;
		/* Hardware will append authenticated tag to output buffer */
		scatterwalk_map_and_copy(tag, req->dst, len, gctx->authsize, 1);
	} else {
		aes->total = len;
		gctx->textlen = req->cryptlen - gctx->authsize;
	}
	aes->resume = mtk_aes_complete;

	return mtk_aes_gcm_dma(cryp, aes, req->src, req->dst, len);
}

static int mtk_aes_gcm_crypt(struct aead_request *req, u64 mode)
{
	struct mtk_aes_base_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct mtk_aes_reqctx *rctx = aead_request_ctx(req);

	rctx->mode = AES_FLAGS_GCM | mode;

	return mtk_aes_handle_queue(ctx->cryp, !!(mode & AES_FLAGS_ENCRYPT),
								&req->base);
}

static void mtk_gcm_setkey_done(struct crypto_async_request *req, int err)
{
	struct mtk_aes_gcm_setkey_result *result = req->data;

	if (err == -EINPROGRESS)
		return;

	result->err = err;
	complete(&result->completion);
}

/*
 * Because of the hardware limitation, we need to pre-calculate key(H)
 * for the GHASH operation. The result of the encryption operation
 * need to be stored in the transform state buffer.
 */
static int mtk_aes_gcm_setkey(struct crypto_aead *aead, const u8 *key,
			      u32 keylen)
{
	struct mtk_aes_base_ctx *ctx = crypto_aead_ctx(aead);
	struct mtk_aes_gcm_ctx *gctx = mtk_aes_gcm_ctx_cast(ctx);
	struct crypto_skcipher *ctr = gctx->ctr;
	struct {
		u32 hash[4];
		u8 iv[8];

		struct mtk_aes_gcm_setkey_result result;

		struct scatterlist sg[1];
		struct skcipher_request req;
	} *data;
	const u32 *aes_key;
	u32 *key_state, *hash_state;
	int err, i;

	if (keylen != AES_KEYSIZE_256 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_128) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	key_state = ctx->tfm.state;
	aes_key = (u32 *)key;
	ctx->keylen = SIZE_IN_WORDS(keylen);

	for (i = 0; i < ctx->keylen; i++)
		ctx->tfm.state[i] = cpu_to_le32(aes_key[i]);

	/* Same as crypto_gcm_setkey() from crypto/gcm.c */
	crypto_skcipher_clear_flags(ctr, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctr, crypto_aead_get_flags(aead) &
				  CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(ctr, key, keylen);
	crypto_aead_set_flags(aead, crypto_skcipher_get_flags(ctr) &
			      CRYPTO_TFM_RES_MASK);
	if (err)
		return err;

	data = kzalloc(sizeof(*data) + crypto_skcipher_reqsize(ctr),
		       GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	init_completion(&data->result.completion);
	sg_init_one(data->sg, &data->hash, AES_BLOCK_SIZE);
	skcipher_request_set_tfm(&data->req, ctr);
	skcipher_request_set_callback(&data->req, CRYPTO_TFM_REQ_MAY_SLEEP |
				      CRYPTO_TFM_REQ_MAY_BACKLOG,
				      mtk_gcm_setkey_done, &data->result);
	skcipher_request_set_crypt(&data->req, data->sg, data->sg,
				   AES_BLOCK_SIZE, data->iv);

	err = crypto_skcipher_encrypt(&data->req);
	if (err == -EINPROGRESS || err == -EBUSY) {
		err = wait_for_completion_interruptible(
			&data->result.completion);
		if (!err)
			err = data->result.err;
	}
	if (err)
		goto out;

	hash_state = key_state + ctx->keylen;

	for (i = 0; i < 4; i++)
		hash_state[i] = cpu_to_be32(data->hash[i]);
out:
	kzfree(data);
	return err;
}

static int mtk_aes_gcm_setauthsize(struct crypto_aead *aead,
				   u32 authsize)
{
	struct mtk_aes_base_ctx *ctx = crypto_aead_ctx(aead);
	struct mtk_aes_gcm_ctx *gctx = mtk_aes_gcm_ctx_cast(ctx);

	/* Same as crypto_gcm_authsize() from crypto/gcm.c */
	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	gctx->authsize = authsize;
	return 0;
}

static int mtk_aes_gcm_encrypt(struct aead_request *req)
{
	return mtk_aes_gcm_crypt(req, AES_FLAGS_ENCRYPT);
}

static int mtk_aes_gcm_decrypt(struct aead_request *req)
{
	return mtk_aes_gcm_crypt(req, 0);
}

static int mtk_aes_gcm_init(struct crypto_aead *aead)
{
	struct mtk_aes_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct mtk_cryp *cryp = NULL;

	cryp = mtk_aes_find_dev(&ctx->base);
	if (!cryp) {
		pr_err("can't find crypto device\n");
		return -ENODEV;
	}

	ctx->ctr = crypto_alloc_skcipher("ctr(aes)", 0,
					 CRYPTO_ALG_ASYNC);
	if (IS_ERR(ctx->ctr)) {
		pr_err("Error allocating ctr(aes)\n");
		return PTR_ERR(ctx->ctr);
	}

	crypto_aead_set_reqsize(aead, sizeof(struct mtk_aes_reqctx));
	ctx->base.start = mtk_aes_gcm_start;
	return 0;
}

static void mtk_aes_gcm_exit(struct crypto_aead *aead)
{
	struct mtk_aes_gcm_ctx *ctx = crypto_aead_ctx(aead);

	crypto_free_skcipher(ctx->ctr);
}

static struct aead_alg aes_gcm_alg = {
	.setkey		= mtk_aes_gcm_setkey,
	.setauthsize	= mtk_aes_gcm_setauthsize,
	.encrypt	= mtk_aes_gcm_encrypt,
	.decrypt	= mtk_aes_gcm_decrypt,
	.init		= mtk_aes_gcm_init,
	.exit		= mtk_aes_gcm_exit,
	.ivsize		= 12,
	.maxauthsize	= AES_BLOCK_SIZE,

	.base = {
		.cra_name		= "gcm(aes)",
		.cra_driver_name	= "gcm-aes-mtk",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct mtk_aes_gcm_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
};

static void mtk_aes_enc_task(unsigned long data)
{
	struct mtk_cryp *cryp = (struct mtk_cryp *)data;
	struct mtk_aes_rec *aes = cryp->aes[0];

	mtk_aes_unmap(cryp, aes);
	aes->resume(cryp, aes);
}

static void mtk_aes_dec_task(unsigned long data)
{
	struct mtk_cryp *cryp = (struct mtk_cryp *)data;
	struct mtk_aes_rec *aes = cryp->aes[1];

	mtk_aes_unmap(cryp, aes);
	aes->resume(cryp, aes);
}

static irqreturn_t mtk_aes_enc_irq(int irq, void *dev_id)
{
	struct mtk_cryp *cryp = (struct mtk_cryp *)dev_id;
	struct mtk_aes_rec *aes = cryp->aes[0];
	u32 val = mtk_aes_read(cryp, RDR_STAT(RING0));

	mtk_aes_write(cryp, RDR_STAT(RING0), val);

	if (likely(AES_FLAGS_BUSY & aes->flags)) {
		mtk_aes_write(cryp, RDR_PROC_COUNT(RING0), MTK_CNT_RST);
		mtk_aes_write(cryp, RDR_THRESH(RING0),
			      MTK_RDR_PROC_THRESH | MTK_RDR_PROC_MODE);

		tasklet_schedule(&aes->task);
	} else {
		dev_warn(cryp->dev, "AES interrupt when no active requests.\n");
	}
	return IRQ_HANDLED;
}

static irqreturn_t mtk_aes_dec_irq(int irq, void *dev_id)
{
	struct mtk_cryp *cryp = (struct mtk_cryp *)dev_id;
	struct mtk_aes_rec *aes = cryp->aes[1];
	u32 val = mtk_aes_read(cryp, RDR_STAT(RING1));

	mtk_aes_write(cryp, RDR_STAT(RING1), val);

	if (likely(AES_FLAGS_BUSY & aes->flags)) {
		mtk_aes_write(cryp, RDR_PROC_COUNT(RING1), MTK_CNT_RST);
		mtk_aes_write(cryp, RDR_THRESH(RING1),
			      MTK_RDR_PROC_THRESH | MTK_RDR_PROC_MODE);

		tasklet_schedule(&aes->task);
	} else {
		dev_warn(cryp->dev, "AES interrupt when no active requests.\n");
	}
	return IRQ_HANDLED;
}

/*
 * The purpose of creating encryption and decryption records is
 * to process outbound/inbound data in parallel, it can improve
 * performance in most use cases, such as IPSec VPN, especially
 * under heavy network traffic.
 */
static int mtk_aes_record_init(struct mtk_cryp *cryp)
{
	struct mtk_aes_rec **aes = cryp->aes;
	int i, err = -ENOMEM;

	for (i = 0; i < MTK_REC_NUM; i++) {
		aes[i] = kzalloc(sizeof(**aes), GFP_KERNEL);
		if (!aes[i])
			goto err_cleanup;

		aes[i]->buf = (void *)__get_free_pages(GFP_KERNEL,
						AES_BUF_ORDER);
		if (!aes[i]->buf)
			goto err_cleanup;

		aes[i]->id = i;

		spin_lock_init(&aes[i]->lock);
		crypto_init_queue(&aes[i]->queue, AES_QUEUE_SIZE);
	}

	tasklet_init(&aes[0]->task, mtk_aes_enc_task, (unsigned long)cryp);
	tasklet_init(&aes[1]->task, mtk_aes_dec_task, (unsigned long)cryp);

	return 0;

err_cleanup:
	for (; i--; ) {
		free_page((unsigned long)aes[i]->buf);
		kfree(aes[i]);
	}

	return err;
}

static void mtk_aes_record_free(struct mtk_cryp *cryp)
{
	int i;

	for (i = 0; i < MTK_REC_NUM; i++) {
		tasklet_kill(&cryp->aes[i]->task);
		free_page((unsigned long)cryp->aes[i]->buf);
		kfree(cryp->aes[i]);
	}
}

static void mtk_aes_unregister_algs(void)
{
	int i;

	crypto_unregister_aead(&aes_gcm_alg);

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++)
		crypto_unregister_alg(&aes_algs[i]);
}

static int mtk_aes_register_algs(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		err = crypto_register_alg(&aes_algs[i]);
		if (err)
			goto err_aes_algs;
	}

	err = crypto_register_aead(&aes_gcm_alg);
	if (err)
		goto err_aes_algs;

	return 0;

err_aes_algs:
	for (; i--; )
		crypto_unregister_alg(&aes_algs[i]);

	return err;
}

int mtk_cipher_alg_register(struct mtk_cryp *cryp)
{
	int ret;

	INIT_LIST_HEAD(&cryp->aes_list);

	/* Initialize two cipher records */
	ret = mtk_aes_record_init(cryp);
	if (ret)
		goto err_record;

	/* Ring0 is use by encryption record */
	ret = devm_request_irq(cryp->dev, cryp->irq[RING0], mtk_aes_enc_irq,
			       IRQF_TRIGGER_LOW, "mtk-aes", cryp);
	if (ret) {
		dev_err(cryp->dev, "unable to request AES encryption irq.\n");
		goto err_res;
	}

	/* Ring1 is use by decryption record */
	ret = devm_request_irq(cryp->dev, cryp->irq[RING1], mtk_aes_dec_irq,
			       IRQF_TRIGGER_LOW, "mtk-aes", cryp);
	if (ret) {
		dev_err(cryp->dev, "unable to request AES decryption irq.\n");
		goto err_res;
	}

	/* Enable ring0 and ring1 interrupt */
	mtk_aes_write(cryp, AIC_ENABLE_SET(RING0), MTK_IRQ_RDR0);
	mtk_aes_write(cryp, AIC_ENABLE_SET(RING1), MTK_IRQ_RDR1);

	spin_lock(&mtk_aes.lock);
	list_add_tail(&cryp->aes_list, &mtk_aes.dev_list);
	spin_unlock(&mtk_aes.lock);

	ret = mtk_aes_register_algs();
	if (ret)
		goto err_algs;

	return 0;

err_algs:
	spin_lock(&mtk_aes.lock);
	list_del(&cryp->aes_list);
	spin_unlock(&mtk_aes.lock);
err_res:
	mtk_aes_record_free(cryp);
err_record:

	dev_err(cryp->dev, "mtk-aes initialization failed.\n");
	return ret;
}

void mtk_cipher_alg_release(struct mtk_cryp *cryp)
{
	spin_lock(&mtk_aes.lock);
	list_del(&cryp->aes_list);
	spin_unlock(&mtk_aes.lock);

	mtk_aes_unregister_algs();
	mtk_aes_record_free(cryp);
}
