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

/* AES command token */
#define AES_CT_SIZE_ECB		2
#define AES_CT_SIZE_CBC		3
#define AES_CT_CTRL_HDR		cpu_to_le32(0x00220000)
#define AES_COMMAND0		cpu_to_le32(0x05000000)
#define AES_COMMAND1		cpu_to_le32(0x2d060000)
#define AES_COMMAND2		cpu_to_le32(0xe4a63806)

/* AES transform information */
#define AES_TFM_ECB		cpu_to_le32(0x0 << 0)
#define AES_TFM_CBC		cpu_to_le32(0x1 << 0)
#define AES_TFM_DECRYPT		cpu_to_le32(0x5 << 0)
#define AES_TFM_ENCRYPT		cpu_to_le32(0x4 << 0)
#define AES_TFM_SIZE(x)		cpu_to_le32((x) << 8)
#define AES_TFM_128BITS		cpu_to_le32(0xb << 16)
#define AES_TFM_192BITS		cpu_to_le32(0xd << 16)
#define AES_TFM_256BITS		cpu_to_le32(0xf << 16)
#define AES_TFM_FULL_IV		cpu_to_le32(0xf << 5)

/* AES flags */
#define AES_FLAGS_MODE_MSK	0x7
#define AES_FLAGS_ECB		BIT(0)
#define AES_FLAGS_CBC		BIT(1)
#define AES_FLAGS_ENCRYPT	BIT(2)
#define AES_FLAGS_BUSY		BIT(3)

/**
 * mtk_aes_ct is a set of hardware instructions(command token)
 * that are used to control engine's processing flow of AES.
 */
struct mtk_aes_ct {
	__le32 ct_ctrl0;
	__le32 ct_ctrl1;
	__le32 ct_ctrl2;
};

/**
 * mtk_aes_tfm is used to define AES transform state
 * and contains all keys and initial vectors.
 */
struct mtk_aes_tfm {
	__le32 tfm_ctrl0;
	__le32 tfm_ctrl1;
	__le32 state[SIZE_IN_WORDS(AES_KEYSIZE_256 + AES_BLOCK_SIZE)];
};

/**
 * mtk_aes_info consists of command token and transform state of AES,
 * which should be encapsulated in command and result descriptors.
 *
 * The engine requires this information to do:
 * - Commands decoding and control of the engine's data path.
 * - Coordinating hardware data fetch and store operations.
 * - Result token construction and output.
 */
struct mtk_aes_info {
	struct mtk_aes_ct ct;
	struct mtk_aes_tfm tfm;
};

struct mtk_aes_reqctx {
	u64 mode;
};

struct mtk_aes_ctx {
	struct mtk_cryp *cryp;
	struct mtk_aes_info info;
	u32 keylen;
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

static struct mtk_cryp *mtk_aes_find_dev(struct mtk_aes_ctx *ctx)
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

/* Initialize and map transform information of AES */
static int mtk_aes_info_map(struct mtk_cryp *cryp,
			    struct mtk_aes_rec *aes,
			    size_t len)
{
	struct mtk_aes_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(aes->req));
	struct mtk_aes_info *info = aes->info;
	struct mtk_aes_ct *ct = &info->ct;
	struct mtk_aes_tfm *tfm = &info->tfm;

	aes->ct_hdr = AES_CT_CTRL_HDR | cpu_to_le32(len);

	if (aes->flags & AES_FLAGS_ENCRYPT)
		tfm->tfm_ctrl0 = AES_TFM_ENCRYPT;
	else
		tfm->tfm_ctrl0 = AES_TFM_DECRYPT;

	if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_128))
		tfm->tfm_ctrl0 |= AES_TFM_128BITS;
	else if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_256))
		tfm->tfm_ctrl0 |= AES_TFM_256BITS;
	else if (ctx->keylen == SIZE_IN_WORDS(AES_KEYSIZE_192))
		tfm->tfm_ctrl0 |= AES_TFM_192BITS;

	ct->ct_ctrl0 = AES_COMMAND0 | cpu_to_le32(len);
	ct->ct_ctrl1 = AES_COMMAND1;

	if (aes->flags & AES_FLAGS_CBC) {
		const u32 *iv = (const u32 *)aes->req->info;
		u32 *iv_state = tfm->state + ctx->keylen;
		int i;

		aes->ct_size = AES_CT_SIZE_CBC;
		ct->ct_ctrl2 = AES_COMMAND2;

		tfm->tfm_ctrl0 |= AES_TFM_SIZE(ctx->keylen +
				  SIZE_IN_WORDS(AES_BLOCK_SIZE));
		tfm->tfm_ctrl1 = AES_TFM_CBC | AES_TFM_FULL_IV;

		for (i = 0; i < SIZE_IN_WORDS(AES_BLOCK_SIZE); i++)
			iv_state[i] = cpu_to_le32(iv[i]);

	} else if (aes->flags & AES_FLAGS_ECB) {
		aes->ct_size = AES_CT_SIZE_ECB;
		tfm->tfm_ctrl0 |= AES_TFM_SIZE(ctx->keylen);
		tfm->tfm_ctrl1 = AES_TFM_ECB;
	}

	aes->ct_dma = dma_map_single(cryp->dev, info, sizeof(*info),
					DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(cryp->dev, aes->ct_dma))) {
		dev_err(cryp->dev, "dma %d bytes error\n", sizeof(*info));
		return -EINVAL;
	}
	aes->tfm_dma = aes->ct_dma + sizeof(*ct);

	return 0;
}

static int mtk_aes_xmit(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct mtk_ring *ring = cryp->ring[aes->id];
	struct mtk_desc *cmd = NULL, *res = NULL;
	struct scatterlist *ssg, *dsg;
	u32 len = aes->src.sg_len;
	int nents;

	/* Fill in the command/result descriptors */
	for (nents = 0; nents < len; ++nents) {
		ssg = &aes->src.sg[nents];
		dsg = &aes->dst.sg[nents];

		cmd = ring->cmd_base + ring->pos;
		cmd->hdr = MTK_DESC_BUF_LEN(ssg->length);
		cmd->buf = cpu_to_le32(sg_dma_address(ssg));

		res = ring->res_base + ring->pos;
		res->hdr = MTK_DESC_BUF_LEN(dsg->length);
		res->buf = cpu_to_le32(sg_dma_address(dsg));

		if (nents == 0) {
			res->hdr |= MTK_DESC_FIRST;
			cmd->hdr |= MTK_DESC_FIRST |
				    MTK_DESC_CT_LEN(aes->ct_size);
			cmd->ct = cpu_to_le32(aes->ct_dma);
			cmd->ct_hdr = aes->ct_hdr;
			cmd->tfm = cpu_to_le32(aes->tfm_dma);
		}

		if (++ring->pos == MTK_DESC_NUM)
			ring->pos = 0;
	}

	cmd->hdr |= MTK_DESC_LAST;
	res->hdr |= MTK_DESC_LAST;

	/*
	 * Make sure that all changes to the DMA ring are done before we
	 * start engine.
	 */
	wmb();
	/* Start DMA transfer */
	mtk_aes_write(cryp, RDR_PREP_COUNT(aes->id), MTK_DESC_CNT(len));
	mtk_aes_write(cryp, CDR_PREP_COUNT(aes->id), MTK_DESC_CNT(len));

	return -EINPROGRESS;
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

static int mtk_aes_map(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	struct scatterlist *src = aes->req->src;
	struct scatterlist *dst = aes->req->dst;
	size_t len = aes->req->nbytes;
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

	if (aes->src.sg == aes->dst.sg) {
		aes->src.sg_len = dma_map_sg(cryp->dev, aes->src.sg,
				aes->src.nents, DMA_BIDIRECTIONAL);
		aes->dst.sg_len = aes->src.sg_len;
		if (unlikely(!aes->src.sg_len))
			return -EFAULT;
	} else {
		aes->src.sg_len = dma_map_sg(cryp->dev, aes->src.sg,
				aes->src.nents, DMA_TO_DEVICE);
		if (unlikely(!aes->src.sg_len))
			return -EFAULT;

		aes->dst.sg_len = dma_map_sg(cryp->dev, aes->dst.sg,
				aes->dst.nents, DMA_FROM_DEVICE);
		if (unlikely(!aes->dst.sg_len)) {
			dma_unmap_sg(cryp->dev, aes->src.sg,
				     aes->src.nents, DMA_TO_DEVICE);
			return -EFAULT;
		}
	}

	return mtk_aes_info_map(cryp, aes, len + padlen);
}

static int mtk_aes_handle_queue(struct mtk_cryp *cryp, u8 id,
				struct ablkcipher_request *req)
{
	struct mtk_aes_rec *aes = cryp->aes[id];
	struct crypto_async_request *areq, *backlog;
	struct mtk_aes_reqctx *rctx;
	struct mtk_aes_ctx *ctx;
	unsigned long flags;
	int err, ret = 0;

	spin_lock_irqsave(&aes->lock, flags);
	if (req)
		ret = ablkcipher_enqueue_request(&aes->queue, req);
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

	req = ablkcipher_request_cast(areq);
	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));
	rctx = ablkcipher_request_ctx(req);
	rctx->mode &= AES_FLAGS_MODE_MSK;
	/* Assign new request to device */
	aes->req = req;
	aes->info = &ctx->info;
	aes->flags = (aes->flags & ~AES_FLAGS_MODE_MSK) | rctx->mode;

	err = mtk_aes_map(cryp, aes);
	if (err)
		return err;

	return mtk_aes_xmit(cryp, aes);
}

static void mtk_aes_unmap(struct mtk_cryp *cryp, struct mtk_aes_rec *aes)
{
	dma_unmap_single(cryp->dev, aes->ct_dma,
			 sizeof(struct mtk_aes_info), DMA_TO_DEVICE);

	if (aes->src.sg == aes->dst.sg) {
		dma_unmap_sg(cryp->dev, aes->src.sg,
			     aes->src.nents, DMA_BIDIRECTIONAL);

		if (aes->src.sg != &aes->aligned_sg)
			mtk_aes_restore_sg(&aes->src);
	} else {
		dma_unmap_sg(cryp->dev, aes->dst.sg,
			     aes->dst.nents, DMA_FROM_DEVICE);

		if (aes->dst.sg != &aes->aligned_sg)
			mtk_aes_restore_sg(&aes->dst);

		dma_unmap_sg(cryp->dev, aes->src.sg,
			     aes->src.nents, DMA_TO_DEVICE);

		if (aes->src.sg != &aes->aligned_sg)
			mtk_aes_restore_sg(&aes->src);
	}

	if (aes->dst.sg == &aes->aligned_sg)
		sg_copy_from_buffer(aes->real_dst,
				    sg_nents(aes->real_dst),
				    aes->buf, aes->total);
}

static inline void mtk_aes_complete(struct mtk_cryp *cryp,
				    struct mtk_aes_rec *aes)
{
	aes->flags &= ~AES_FLAGS_BUSY;

	aes->req->base.complete(&aes->req->base, 0);

	/* Handle new request */
	mtk_aes_handle_queue(cryp, aes->id, NULL);
}

/* Check and set the AES key to transform state buffer */
static int mtk_aes_setkey(struct crypto_ablkcipher *tfm,
			  const u8 *key, u32 keylen)
{
	struct mtk_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	const u32 *key_tmp = (const u32 *)key;
	u32 *key_state = ctx->info.tfm.state;
	int i;

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->keylen = SIZE_IN_WORDS(keylen);

	for (i = 0; i < ctx->keylen; i++)
		key_state[i] = cpu_to_le32(key_tmp[i]);

	return 0;
}

static int mtk_aes_crypt(struct ablkcipher_request *req, u64 mode)
{
	struct mtk_aes_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct mtk_aes_reqctx *rctx = ablkcipher_request_ctx(req);

	rctx->mode = mode;

	return mtk_aes_handle_queue(ctx->cryp,
			!(mode & AES_FLAGS_ENCRYPT), req);
}

static int mtk_ecb_encrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_ECB);
}

static int mtk_ecb_decrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ECB);
}

static int mtk_cbc_encrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_ENCRYPT | AES_FLAGS_CBC);
}

static int mtk_cbc_decrypt(struct ablkcipher_request *req)
{
	return mtk_aes_crypt(req, AES_FLAGS_CBC);
}

static int mtk_aes_cra_init(struct crypto_tfm *tfm)
{
	struct mtk_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct mtk_cryp *cryp = NULL;

	tfm->crt_ablkcipher.reqsize = sizeof(struct mtk_aes_reqctx);

	cryp = mtk_aes_find_dev(ctx);
	if (!cryp) {
		pr_err("can't find crypto device\n");
		return -ENODEV;
	}

	return 0;
}

static struct crypto_alg aes_algs[] = {
{
	.cra_name		=	"cbc(aes)",
	.cra_driver_name	=	"cbc-aes-mtk",
	.cra_priority		=	400,
	.cra_flags		=	CRYPTO_ALG_TYPE_ABLKCIPHER |
						CRYPTO_ALG_ASYNC,
	.cra_init		=	mtk_aes_cra_init,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct mtk_aes_ctx),
	.cra_alignmask		=	15,
	.cra_type		=	&crypto_ablkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u.ablkcipher	=	{
		.min_keysize	=	AES_MIN_KEY_SIZE,
		.max_keysize	=	AES_MAX_KEY_SIZE,
		.setkey		=	mtk_aes_setkey,
		.encrypt	=	mtk_cbc_encrypt,
		.decrypt	=	mtk_cbc_decrypt,
		.ivsize		=	AES_BLOCK_SIZE,
	}
},
{
	.cra_name		=	"ecb(aes)",
	.cra_driver_name	=	"ecb-aes-mtk",
	.cra_priority		=	400,
	.cra_flags		=	CRYPTO_ALG_TYPE_ABLKCIPHER |
						CRYPTO_ALG_ASYNC,
	.cra_init		=	mtk_aes_cra_init,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct mtk_aes_ctx),
	.cra_alignmask		=	15,
	.cra_type		=	&crypto_ablkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u.ablkcipher	=	{
		.min_keysize	=	AES_MIN_KEY_SIZE,
		.max_keysize	=	AES_MAX_KEY_SIZE,
		.setkey		=	mtk_aes_setkey,
		.encrypt	=	mtk_ecb_encrypt,
		.decrypt	=	mtk_ecb_decrypt,
	}
},
};

static void mtk_aes_enc_task(unsigned long data)
{
	struct mtk_cryp *cryp = (struct mtk_cryp *)data;
	struct mtk_aes_rec *aes = cryp->aes[0];

	mtk_aes_unmap(cryp, aes);
	mtk_aes_complete(cryp, aes);
}

static void mtk_aes_dec_task(unsigned long data)
{
	struct mtk_cryp *cryp = (struct mtk_cryp *)data;
	struct mtk_aes_rec *aes = cryp->aes[1];

	mtk_aes_unmap(cryp, aes);
	mtk_aes_complete(cryp, aes);
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
