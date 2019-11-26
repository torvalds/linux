// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Driver for EIP97 AES acceleration.
 *
 * Copyright (c) 2016 Ryder Lee <ryder.lee@mediatek.com>
 *
 * Some ideas are from atmel-aes.c drivers.
 */

#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/internal/skcipher.h>
#include "mtk-platform.h"

#define AES_QUEUE_SIZE		512
#define AES_BUF_ORDER		2
#define AES_BUF_SIZE		((PAGE_SIZE << AES_BUF_ORDER) \
				& ~(AES_BLOCK_SIZE - 1))
#define AES_MAX_STATE_BUF_SIZE	SIZE_IN_WORDS(AES_KEYSIZE_256 + \
				AES_BLOCK_SIZE * 2)
#define AES_MAX_CT_SIZE		6

#define AES_CT_CTRL_HDR		cpu_to_le32(0x00220000)

/* AES-CBC/ECB/CTR/OFB/CFB command token */
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
#define AES_TFM_GHASH_DIGEST	cpu_to_le32(0x2 << 21)
#define AES_TFM_GHASH		cpu_to_le32(0x4 << 23)
/* AES transform information word 1 fields */
#define AES_TFM_ECB		cpu_to_le32(0x0 << 0)
#define AES_TFM_CBC		cpu_to_le32(0x1 << 0)
#define AES_TFM_OFB		cpu_to_le32(0x4 << 0)
#define AES_TFM_CFB128		cpu_to_le32(0x5 << 0)
#define AES_TFM_CTR_INIT	cpu_to_le32(0x2 << 0)	/* init counter to 1 */
#define AES_TFM_CTR_LOAD	cpu_to_le32(0x6 << 0)	/* load/reuse counter */
#define AES_TFM_3IV		cpu_to_le32(0x7 << 5)	/* using IV 0-2 */
#define AES_TFM_FULL_IV		cpu_to_le32(0xf << 5)	/* using IV 0-3 */
#define AES_TFM_IV_CTR_MODE	cpu_to_le32(0x1 << 10)
#define AES_TFM_ENC_HASH	cpu_to_le32(0x1 << 17)

/* AES flags */
#define AES_FLAGS_CIPHER_MSK	GENMASK(4, 0)
#define AES_FLAGS_ECB		BIT(0)
#define AES_FLAGS_CBC		BIT(1)
#define AES_FLAGS_CTR		BIT(2)
#define AES_FLAGS_OFB		BIT(3)
#define AES_FLAGS_CFB128	BIT(4)
#define AES_FLAGS_GCM		BIT(5)
#define AES_FLAGS_ENCRYPT	BIT(6)
#define AES_FLAGS_BUSY		BIT(7)

#define AES_AUTH_TAG_ERR	cpu_to_le32(BIT(26))

/**
 * mtk_aes_info - hardware information of AES
 * @cmd:	command token, hardware instruction
 * @tfm:	transform state of cipher algorithm.
 * @state:	contains keys and initial vectors.
 *
 * Memory layout of GCM buffer:
 * /-----------\
 * |  AES KEY  | 128/196/256 bits
 * |-----------|
 * |  HASH KEY | a string 128 zero bits encrypted using the block cipher
 * |-----------|
 * |    IVs    | 4 * 4 bytes
 * \-----------/
 *
 * The engine requires all these info to do:
 * - Commands decoding and control of the engine's data path.
 * - Coordinating hardware data fetch and store operations.
 * - Result token construction and output.
 */
struct mtk_aes_info {
	__le32 cmd[AES_MAX_CT_SIZE];
	__le32 tfm[2];
	__le32 state[AES_MAX_STATE_BUF_SIZE];
};

struct mtk_aes_reqctx {
	u64 mode;
};

struct mtk_aes_base_ctx {
	struct mtk_cryp *cryp;
	u32 keylen;
	__le32 key[12];
	__le32 keymode;

	mtk_aes_fn start;

	struct mtk_aes_info info;
	dma_addr_t ct_dma;
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

static inline void mtk_aes_write_state_le(__le32 *dst, const u32 *src, u32 size)
{
	int i;

	for (i = 0; i < SIZE_IN_WORDS(size); i++)
		dst[i] = cpu_to_le32(src[i]);
}

static inline void mtk_aes_write_state_be(__be32 *dst, const u32 *src, u32 size)
{
	int i;

	for (i = 0; i < SIZE_IN_WORDS(size); i++)
		dst[i] = cpu_to_be32(src[i]);
}

static inline int mtk_aes_complete(struct mtk_cryp *cryp,
				   struct mtk_aes_rec *aes,
				   int err)
{
	aes->flags &= ~AES_FLAGS_BUSY;
	aes->areq->complete(aes->areq, err);
	/* Handle new request */
	tasklet_schedule(&aes->queue_task);
	return err;
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
		cmd = ring->cmd_next;
		cmd->hdr = MTK_DESC_BUF_LEN(ssg->length);
		cmd->buf = cpu_to_le32(sg_dma_address(ssg));

		if (nents == 0) {
			cmd->hdr |= MTK_DESC_FIRST |
				    MTK_DESC_CT_LEN(aes->ctx->ct_size);
			cmd->ct = cpu_to_le32(aes->ctx->ct_dma);
			cmd->ct_hdr = aes->ctx->ct_hdr;
			cmd->tfm = cpu_to_le32(aes->ctx->tfm_dma);
		}

		/* Shift ring buffer and check boundary */
		if (++ring->cmd_next == ring->cmd_base + MTK_DESC_NUM)
			ring->cmd_next = ring->cmd_base;
	}
	cmd->hdr |= MTK_DESC_LAST;

	/* Prepare result descriptors */
	for (nents = 0; nents < dlen; ++nents, dsg = sg_next(dsg)) {
		res = ring->res_next;
		res->hdr = MTK_DESC_BUF_LEN(dsg->length);
		res->buf = cpu_to_le32(sg_dma_address(dsg));

		if (nents == 0)
			res->hdr |= MTK_DESC_FIRST;

		/* Shift ring buffer and check boundary */
		if (++ring->res_next == ring->res_base + MTK_DESC_NUM)
			ring->res_next = ring->res_base;
	}
	res->hdr |= MTK_DESC_LAST;

	/* Pointer to current result descriptor */
	ring->res_prev = res;

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

	dma_unmap_single(cryp->dev, ctx->ct_dma, sizeof(ctx->info),
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
	struct mtk_aes_info *info = &ctx->info;

	ctx->ct_dma = dma_map_single(cryp->dev, info, sizeof(*info),
				     DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(cryp->dev, ctx->ct_dma)))
		goto exit;

	ctx->tfm_dma = ctx->ct_dma + sizeof(info->cmd);

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
	dma_unmap_single(cryp->dev, ctx->ct_dma, sizeof(*info), DMA_TO_DEVICE);
exit:
	return mtk_aes_complete(cryp, aes, -EINVAL);
}

/* Initialize transform information of CBC/ECB/CTR/OFB/CFB mode */
static void mtk_aes_info_init(struct mtk_cryp *cryp, struct mtk_aes_rec *aes,
			      size_t len)
{
	struct skcipher_request *req = skcipher_request_cast(aes->areq);
	struct mtk_aes_base_ctx *ctx = aes->ctx;
	struct mtk_aes_info *info = &ctx->info;
	u32 cnt = 0;

	ctx->ct_hdr = AES_CT_CTRL_HDR | cpu_to_le32(len);
	info->cmd[cnt++] = AES_CMD0 | cpu_to_le32(len);
	info->cmd[cnt++] = AES_CMD1;

	info->tfm[0] = AES_TFM_SIZE(ctx->keylen) | ctx->keymode;
	if (aes->flags & AES_FLAGS_ENCRYPT)
		info->tfm[0] |= AES_TFM_BASIC_OUT;
	else
		info->tfm[0] |= AES_TFM_BASIC_IN;

	switch (aes->flags & AES_FLAGS_CIPHER_MSK) {
	case AES_FLAGS_CBC:
		info->tfm[1] = AES_TFM_CBC;
		break;
	case AES_FLAGS_ECB:
		info->tfm[1] = AES_TFM_ECB;
		goto ecb;
	case AES_FLAGS_CTR:
		info->tfm[1] = AES_TFM_CTR_LOAD;
		goto ctr;
	case AES_FLAGS_OFB:
		info->tfm[1] = AES_TFM_OFB;
		break;
	case AES_FLAGS_CFB128:
		info->tfm[1] = AES_TFM_CFB128;
		break;
	default:
		/* Should not happen... */
		return;
	}

	mtk_aes_write_state_le(info->state + ctx->keylen, (void *)req->iv,
			       AES_BLOCK_SIZE);
ctr:
	info->tfm[0] += AES_TFM_SIZE(SIZE_IN_WORDS(AES_BLOCK_SIZE));
	info->tfm[1] |= AES_TFM_FULL_IV;
	info->cmd[cnt++] = AES_CMD2;
ecb:
	ctx->ct_size = cnt;
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
			return mtk_aes_complete(cryp, aes, -ENOMEM);

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
	/* Write key into state buffer */
	memcpy(ctx->info.state, ctx->key, sizeof(ctx->key));

	aes->areq = areq;
	aes->ctx = ctx;

	return ctx->start(cryp, aes);
}

static int mtk_aes_transfer_complete(struct mtk_cryp *cryp,
				     struct mtk_aes_rec *aes)
{
	return mtk_aes_complete(cryp, aes, 0);
}

static int mtk_aes_start(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct skcipher_request *req = skcipher_request_cast(aes->areq);
	struct mtk_aes_reqctx *rctx = skcipher_request_ctx(req);

	mtk_aes_set_mode(aes, rctx);
	aes->resume = mtk_aes_transfer_complete;

	return mtk_aes_dma(cryp, aes, req->src, req->dst, req->cryptlen);
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
	struct skcipher_request *req = skcipher_request_cast(aes->areq);
	struct scatterlist *src, *dst;
	u32 start, end, ctr, blocks;
	size_t datalen;
	bool fragmented = false;

	/* Check for transfer completion. */
	cctx->offset += aes->total;
	if (cctx->offset >= req->cryptlen)
		return mtk_aes_transfer_complete(cryp, aes);

	/* Compute data length. */
	datalen = req->cryptlen - cctx->offset;
	blocks = DIV_ROUND_UP(datalen, AES_BLOCK_SIZE);
	ctr = be32_to_cpu(cctx->iv[3]);

	/* Check 32bit counter overflow. */
	start = ctr;
	end = start + blocks - 1;
	if (end < start) {
		ctr = 0xffffffff;
		datalen = AES_BLOCK_SIZE * -start;
		fragmented = true;
	}

	/* Jump to offset. */
	src = scatterwalk_ffwd(cctx->src, req->src, cctx->offset);
	dst = ((req->src == req->dst) ? src :
	       scatterwalk_ffwd(cctx->dst, req->dst, cctx->offset));

	/* Write IVs into transform state buffer. */
	mtk_aes_write_state_le(ctx->info.state + ctx->keylen, cctx->iv,
			       AES_BLOCK_SIZE);

	if (unlikely(fragmented)) {
	/*
	 * Increment the counter manually to cope with the hardware
	 * counter overflow.
	 */
		cctx->iv[3] = cpu_to_be32(ctr);
		crypto_inc((u8 *)cctx->iv, AES_BLOCK_SIZE);
	}

	return mtk_aes_dma(cryp, aes, src, dst, datalen);
}

static int mtk_aes_ctr_start(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_aes_ctr_ctx *cctx = mtk_aes_ctr_ctx_cast(aes->ctx);
	struct skcipher_request *req = skcipher_request_cast(aes->areq);
	struct mtk_aes_reqctx *rctx = skcipher_request_ctx(req);

	mtk_aes_set_mode(aes, rctx);

	memcpy(cctx->iv, req->iv, AES_BLOCK_SIZE);
	cctx->offset = 0;
	aes->total = 0;
	aes->resume = mtk_aes_ctr_transfer;

	return mtk_aes_ctr_transfer(cryp, aes);
}

/* Check and set the AES key to transform state buffer */
static int mtk_aes_setkey(struct crypto_skcipher *tfm,
			  const u8 *key, u32 keylen)
{
	struct mtk_aes_base_ctx *ctx = crypto_skcipher_ctx(tfm);

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->keymode = AES_TFM_128BITS;
		break;
	case AES_KEYSIZE_192:
		ctx->keymode = AES_TFM_192BITS;
		break;
	case AES_KEYSIZE_256:
		ctx->keymode = AES_TFM_256BITS;
		break;

	default:
		crypto_skcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->keylen = SIZE_IN_WORDS(keylen);
	mtk_aes_write_state_le(ctx->key, (const u32 *)key, keylen);

	return 0;
}

static int mtk_aes_crypt(struct skcipher_request *req, u64 mode)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct mtk_aes_base_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct mtk_aes_reqctx *rctx;
	struct mtk_cryp *cryp;

	cryp = mtk_aes_find_dev(ctx);
	if (!cryp)
		return -ENODEV;

	rctx = skcipher_request_ctx(req);
	rctx->mode = mode;

	return mtk_aes_handle_queue(cryp, !(mode & AES_FLAGS_ENCRYPT),
				    &req->base);
}

static int mtk_aes_ecb_encrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_ECB);
}

static int mtk_aes_ecb_decrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ECB);
}

static int mtk_aes_cbc_encrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_CBC);
}

static int mtk_aes_cbc_decrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_CBC);
}

static int mtk_aes_ctr_encrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_CTR);
}

static int mtk_aes_ctr_decrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_CTR);
}

static int mtk_aes_ofb_encrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_OFB);
}

static int mtk_aes_ofb_decrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_OFB);
}

static int mtk_aes_cfb_encrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_CFB128);
}

static int mtk_aes_cfb_decrypt(struct skcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_CFB128);
}

static int mtk_aes_init_tfm(struct crypto_skcipher *tfm)
{
	struct mtk_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct mtk_aes_reqctx));
	ctx->base.start = mtk_aes_start;
	return 0;
}

static int mtk_aes_ctr_init_tfm(struct crypto_skcipher *tfm)
{
	struct mtk_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct mtk_aes_reqctx));
	ctx->base.start = mtk_aes_ctr_start;
	return 0;
}

static struct skcipher_alg aes_algs[] = {
{
	.base.cra_name		= "cbc(aes)",
	.base.cra_driver_name	= "cbc-aes-mtk",
	.base.cra_priority	= 400,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mtk_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.setkey			= mtk_aes_setkey,
	.encrypt		= mtk_aes_cbc_encrypt,
	.decrypt		= mtk_aes_cbc_decrypt,
	.ivsize			= AES_BLOCK_SIZE,
	.init			= mtk_aes_init_tfm,
},
{
	.base.cra_name		= "ecb(aes)",
	.base.cra_driver_name	= "ecb-aes-mtk",
	.base.cra_priority	= 400,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mtk_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.setkey			= mtk_aes_setkey,
	.encrypt		= mtk_aes_ecb_encrypt,
	.decrypt		= mtk_aes_ecb_decrypt,
	.init			= mtk_aes_init_tfm,
},
{
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "ctr-aes-mtk",
	.base.cra_priority	= 400,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct mtk_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= mtk_aes_setkey,
	.encrypt		= mtk_aes_ctr_encrypt,
	.decrypt		= mtk_aes_ctr_decrypt,
	.init			= mtk_aes_ctr_init_tfm,
},
{
	.base.cra_name		= "ofb(aes)",
	.base.cra_driver_name	= "ofb-aes-mtk",
	.base.cra_priority	= 400,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mtk_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= mtk_aes_setkey,
	.encrypt		= mtk_aes_ofb_encrypt,
	.decrypt		= mtk_aes_ofb_decrypt,
},
{
	.base.cra_name		= "cfb(aes)",
	.base.cra_driver_name	= "cfb-aes-mtk",
	.base.cra_priority	= 400,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct mtk_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= mtk_aes_setkey,
	.encrypt		= mtk_aes_cfb_encrypt,
	.decrypt		= mtk_aes_cfb_decrypt,
},
};

static inline struct mtk_aes_gcm_ctx *
mtk_aes_gcm_ctx_cast(struct mtk_aes_base_ctx *ctx)
{
	return container_of(ctx, struct mtk_aes_gcm_ctx, base);
}

/*
 * Engine will verify and compare tag automatically, so we just need
 * to check returned status which stored in the result descriptor.
 */
static int mtk_aes_gcm_tag_verify(struct mtk_cryp *cryp,
				  struct mtk_aes_rec *aes)
{
	u32 status = cryp->ring[aes->id]->res_prev->ct;

	return mtk_aes_complete(cryp, aes, (status & AES_AUTH_TAG_ERR) ?
				-EBADMSG : 0);
}

/* Initialize transform information of GCM mode */
static void mtk_aes_gcm_info_init(struct mtk_cryp *cryp,
				  struct mtk_aes_rec *aes,
				  size_t len)
{
	struct aead_request *req = aead_request_cast(aes->areq);
	struct mtk_aes_base_ctx *ctx = aes->ctx;
	struct mtk_aes_gcm_ctx *gctx = mtk_aes_gcm_ctx_cast(ctx);
	struct mtk_aes_info *info = &ctx->info;
	u32 ivsize = crypto_aead_ivsize(crypto_aead_reqtfm(req));
	u32 cnt = 0;

	ctx->ct_hdr = AES_CT_CTRL_HDR | len;

	info->cmd[cnt++] = AES_GCM_CMD0 | cpu_to_le32(req->assoclen);
	info->cmd[cnt++] = AES_GCM_CMD1 | cpu_to_le32(req->assoclen);
	info->cmd[cnt++] = AES_GCM_CMD2;
	info->cmd[cnt++] = AES_GCM_CMD3 | cpu_to_le32(gctx->textlen);

	if (aes->flags & AES_FLAGS_ENCRYPT) {
		info->cmd[cnt++] = AES_GCM_CMD4 | cpu_to_le32(gctx->authsize);
		info->tfm[0] = AES_TFM_GCM_OUT;
	} else {
		info->cmd[cnt++] = AES_GCM_CMD5 | cpu_to_le32(gctx->authsize);
		info->cmd[cnt++] = AES_GCM_CMD6 | cpu_to_le32(gctx->authsize);
		info->tfm[0] = AES_TFM_GCM_IN;
	}
	ctx->ct_size = cnt;

	info->tfm[0] |= AES_TFM_GHASH_DIGEST | AES_TFM_GHASH | AES_TFM_SIZE(
			ctx->keylen + SIZE_IN_WORDS(AES_BLOCK_SIZE + ivsize)) |
			ctx->keymode;
	info->tfm[1] = AES_TFM_CTR_INIT | AES_TFM_IV_CTR_MODE | AES_TFM_3IV |
		       AES_TFM_ENC_HASH;

	mtk_aes_write_state_le(info->state + ctx->keylen + SIZE_IN_WORDS(
			       AES_BLOCK_SIZE), (const u32 *)req->iv, ivsize);
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
			return mtk_aes_complete(cryp, aes, -ENOMEM);

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

		aes->resume = mtk_aes_transfer_complete;
		/* Compute total process length. */
		aes->total = len + gctx->authsize;
		/* Hardware will append authenticated tag to output buffer */
		scatterwalk_map_and_copy(tag, req->dst, len, gctx->authsize, 1);
	} else {
		aes->resume = mtk_aes_gcm_tag_verify;
		aes->total = len;
	}

	return mtk_aes_gcm_dma(cryp, aes, req->src, req->dst, len);
}

static int mtk_aes_gcm_crypt(struct aead_request *req, u64 mode)
{
	struct mtk_aes_base_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct mtk_aes_gcm_ctx *gctx = mtk_aes_gcm_ctx_cast(ctx);
	struct mtk_aes_reqctx *rctx = aead_request_ctx(req);
	struct mtk_cryp *cryp;
	bool enc = !!(mode & AES_FLAGS_ENCRYPT);

	cryp = mtk_aes_find_dev(ctx);
	if (!cryp)
		return -ENODEV;

	/* Compute text length. */
	gctx->textlen = req->cryptlen - (enc ? 0 : gctx->authsize);

	/* Empty messages are not supported yet */
	if (!gctx->textlen && !req->assoclen)
		return -EINVAL;

	rctx->mode = AES_FLAGS_GCM | mode;

	return mtk_aes_handle_queue(cryp, enc, &req->base);
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

		struct crypto_wait wait;

		struct scatterlist sg[1];
		struct skcipher_request req;
	} *data;
	int err;

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->keymode = AES_TFM_128BITS;
		break;
	case AES_KEYSIZE_192:
		ctx->keymode = AES_TFM_192BITS;
		break;
	case AES_KEYSIZE_256:
		ctx->keymode = AES_TFM_256BITS;
		break;

	default:
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->keylen = SIZE_IN_WORDS(keylen);

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

	crypto_init_wait(&data->wait);
	sg_init_one(data->sg, &data->hash, AES_BLOCK_SIZE);
	skcipher_request_set_tfm(&data->req, ctr);
	skcipher_request_set_callback(&data->req, CRYPTO_TFM_REQ_MAY_SLEEP |
				      CRYPTO_TFM_REQ_MAY_BACKLOG,
				      crypto_req_done, &data->wait);
	skcipher_request_set_crypt(&data->req, data->sg, data->sg,
				   AES_BLOCK_SIZE, data->iv);

	err = crypto_wait_req(crypto_skcipher_encrypt(&data->req),
			      &data->wait);
	if (err)
		goto out;

	mtk_aes_write_state_le(ctx->key, (const u32 *)key, keylen);
	mtk_aes_write_state_be(ctx->key + ctx->keylen, data->hash,
			       AES_BLOCK_SIZE);
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
	.ivsize		= GCM_AES_IV_SIZE,
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

static void mtk_aes_queue_task(unsigned long data)
{
	struct mtk_aes_rec *aes = (struct mtk_aes_rec *)data;

	mtk_aes_handle_queue(aes->cryp, aes->id, NULL);
}

static void mtk_aes_done_task(unsigned long data)
{
	struct mtk_aes_rec *aes = (struct mtk_aes_rec *)data;
	struct mtk_cryp *cryp = aes->cryp;

	mtk_aes_unmap(cryp, aes);
	aes->resume(cryp, aes);
}

static irqreturn_t mtk_aes_irq(int irq, void *dev_id)
{
	struct mtk_aes_rec *aes  = (struct mtk_aes_rec *)dev_id;
	struct mtk_cryp *cryp = aes->cryp;
	u32 val = mtk_aes_read(cryp, RDR_STAT(aes->id));

	mtk_aes_write(cryp, RDR_STAT(aes->id), val);

	if (likely(AES_FLAGS_BUSY & aes->flags)) {
		mtk_aes_write(cryp, RDR_PROC_COUNT(aes->id), MTK_CNT_RST);
		mtk_aes_write(cryp, RDR_THRESH(aes->id),
			      MTK_RDR_PROC_THRESH | MTK_RDR_PROC_MODE);

		tasklet_schedule(&aes->done_task);
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

		aes[i]->cryp = cryp;

		spin_lock_init(&aes[i]->lock);
		crypto_init_queue(&aes[i]->queue, AES_QUEUE_SIZE);

		tasklet_init(&aes[i]->queue_task, mtk_aes_queue_task,
			     (unsigned long)aes[i]);
		tasklet_init(&aes[i]->done_task, mtk_aes_done_task,
			     (unsigned long)aes[i]);
	}

	/* Link to ring0 and ring1 respectively */
	aes[0]->id = MTK_RING0;
	aes[1]->id = MTK_RING1;

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
		tasklet_kill(&cryp->aes[i]->done_task);
		tasklet_kill(&cryp->aes[i]->queue_task);

		free_page((unsigned long)cryp->aes[i]->buf);
		kfree(cryp->aes[i]);
	}
}

static void mtk_aes_unregister_algs(void)
{
	int i;

	crypto_unregister_aead(&aes_gcm_alg);

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++)
		crypto_unregister_skcipher(&aes_algs[i]);
}

static int mtk_aes_register_algs(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		err = crypto_register_skcipher(&aes_algs[i]);
		if (err)
			goto err_aes_algs;
	}

	err = crypto_register_aead(&aes_gcm_alg);
	if (err)
		goto err_aes_algs;

	return 0;

err_aes_algs:
	for (; i--; )
		crypto_unregister_skcipher(&aes_algs[i]);

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

	ret = devm_request_irq(cryp->dev, cryp->irq[MTK_RING0], mtk_aes_irq,
			       0, "mtk-aes", cryp->aes[0]);
	if (ret) {
		dev_err(cryp->dev, "unable to request AES irq.\n");
		goto err_res;
	}

	ret = devm_request_irq(cryp->dev, cryp->irq[MTK_RING1], mtk_aes_irq,
			       0, "mtk-aes", cryp->aes[1]);
	if (ret) {
		dev_err(cryp->dev, "unable to request AES irq.\n");
		goto err_res;
	}

	/* Enable ring0 and ring1 interrupt */
	mtk_aes_write(cryp, AIC_ENABLE_SET(MTK_RING0), MTK_IRQ_RDR0);
	mtk_aes_write(cryp, AIC_ENABLE_SET(MTK_RING1), MTK_IRQ_RDR1);

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
