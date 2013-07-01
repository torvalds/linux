/*
 * Cryptographic API.
 *
 * Support for SAHARA cryptographic accelerator.
 *
 * Copyright (c) 2013 Vista Silicon S.L.
 * Author: Javier Martin <javier.martin@vista-silicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Based on omap-aes.c and tegra-aes.c
 */

#include <crypto/algapi.h>
#include <crypto/aes.h>

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define SAHARA_NAME "sahara"
#define SAHARA_VERSION_3	3
#define SAHARA_TIMEOUT_MS	1000
#define SAHARA_MAX_HW_DESC	2
#define SAHARA_MAX_HW_LINK	20

#define FLAGS_MODE_MASK		0x000f
#define FLAGS_ENCRYPT		BIT(0)
#define FLAGS_CBC		BIT(1)
#define FLAGS_NEW_KEY		BIT(3)
#define FLAGS_BUSY		4

#define SAHARA_HDR_BASE			0x00800000
#define SAHARA_HDR_SKHA_ALG_AES	0
#define SAHARA_HDR_SKHA_OP_ENC		(1 << 2)
#define SAHARA_HDR_SKHA_MODE_ECB	(0 << 3)
#define SAHARA_HDR_SKHA_MODE_CBC	(1 << 3)
#define SAHARA_HDR_FORM_DATA		(5 << 16)
#define SAHARA_HDR_FORM_KEY		(8 << 16)
#define SAHARA_HDR_LLO			(1 << 24)
#define SAHARA_HDR_CHA_SKHA		(1 << 28)
#define SAHARA_HDR_CHA_MDHA		(2 << 28)
#define SAHARA_HDR_PARITY_BIT		(1 << 31)

/* SAHARA can only process one request at a time */
#define SAHARA_QUEUE_LENGTH	1

#define SAHARA_REG_VERSION	0x00
#define SAHARA_REG_DAR		0x04
#define SAHARA_REG_CONTROL	0x08
#define		SAHARA_CONTROL_SET_THROTTLE(x)	(((x) & 0xff) << 24)
#define		SAHARA_CONTROL_SET_MAXBURST(x)	(((x) & 0xff) << 16)
#define		SAHARA_CONTROL_RNG_AUTORSD	(1 << 7)
#define		SAHARA_CONTROL_ENABLE_INT	(1 << 4)
#define SAHARA_REG_CMD		0x0C
#define		SAHARA_CMD_RESET		(1 << 0)
#define		SAHARA_CMD_CLEAR_INT		(1 << 8)
#define		SAHARA_CMD_CLEAR_ERR		(1 << 9)
#define		SAHARA_CMD_SINGLE_STEP		(1 << 10)
#define		SAHARA_CMD_MODE_BATCH		(1 << 16)
#define		SAHARA_CMD_MODE_DEBUG		(1 << 18)
#define	SAHARA_REG_STATUS	0x10
#define		SAHARA_STATUS_GET_STATE(x)	((x) & 0x7)
#define			SAHARA_STATE_IDLE	0
#define			SAHARA_STATE_BUSY	1
#define			SAHARA_STATE_ERR	2
#define			SAHARA_STATE_FAULT	3
#define			SAHARA_STATE_COMPLETE	4
#define			SAHARA_STATE_COMP_FLAG	(1 << 2)
#define		SAHARA_STATUS_DAR_FULL		(1 << 3)
#define		SAHARA_STATUS_ERROR		(1 << 4)
#define		SAHARA_STATUS_SECURE		(1 << 5)
#define		SAHARA_STATUS_FAIL		(1 << 6)
#define		SAHARA_STATUS_INIT		(1 << 7)
#define		SAHARA_STATUS_RNG_RESEED	(1 << 8)
#define		SAHARA_STATUS_ACTIVE_RNG	(1 << 9)
#define		SAHARA_STATUS_ACTIVE_MDHA	(1 << 10)
#define		SAHARA_STATUS_ACTIVE_SKHA	(1 << 11)
#define		SAHARA_STATUS_MODE_BATCH	(1 << 16)
#define		SAHARA_STATUS_MODE_DEDICATED	(1 << 17)
#define		SAHARA_STATUS_MODE_DEBUG	(1 << 18)
#define		SAHARA_STATUS_GET_ISTATE(x)	(((x) >> 24) & 0xff)
#define SAHARA_REG_ERRSTATUS	0x14
#define		SAHARA_ERRSTATUS_GET_SOURCE(x)	((x) & 0xf)
#define			SAHARA_ERRSOURCE_CHA	14
#define			SAHARA_ERRSOURCE_DMA	15
#define		SAHARA_ERRSTATUS_DMA_DIR	(1 << 8)
#define		SAHARA_ERRSTATUS_GET_DMASZ(x)(((x) >> 9) & 0x3)
#define		SAHARA_ERRSTATUS_GET_DMASRC(x) (((x) >> 13) & 0x7)
#define		SAHARA_ERRSTATUS_GET_CHASRC(x)	(((x) >> 16) & 0xfff)
#define		SAHARA_ERRSTATUS_GET_CHAERR(x)	(((x) >> 28) & 0x3)
#define SAHARA_REG_FADDR	0x18
#define SAHARA_REG_CDAR		0x1C
#define SAHARA_REG_IDAR		0x20

struct sahara_hw_desc {
	u32		hdr;
	u32		len1;
	dma_addr_t	p1;
	u32		len2;
	dma_addr_t	p2;
	dma_addr_t	next;
};

struct sahara_hw_link {
	u32		len;
	dma_addr_t	p;
	dma_addr_t	next;
};

struct sahara_ctx {
	struct sahara_dev *dev;
	unsigned long flags;
	int keylen;
	u8 key[AES_KEYSIZE_128];
	struct crypto_ablkcipher *fallback;
};

struct sahara_aes_reqctx {
	unsigned long mode;
};

struct sahara_dev {
	struct device		*device;
	void __iomem		*regs_base;
	struct clk		*clk_ipg;
	struct clk		*clk_ahb;

	struct sahara_ctx	*ctx;
	spinlock_t		lock;
	struct crypto_queue	queue;
	unsigned long		flags;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	struct sahara_hw_desc	*hw_desc[SAHARA_MAX_HW_DESC];
	dma_addr_t		hw_phys_desc[SAHARA_MAX_HW_DESC];

	u8			*key_base;
	dma_addr_t		key_phys_base;

	u8			*iv_base;
	dma_addr_t		iv_phys_base;

	struct sahara_hw_link	*hw_link[SAHARA_MAX_HW_LINK];
	dma_addr_t		hw_phys_link[SAHARA_MAX_HW_LINK];

	struct ablkcipher_request *req;
	size_t			total;
	struct scatterlist	*in_sg;
	unsigned int		nb_in_sg;
	struct scatterlist	*out_sg;
	unsigned int		nb_out_sg;

	u32			error;
	struct timer_list	watchdog;
};

static struct sahara_dev *dev_ptr;

static inline void sahara_write(struct sahara_dev *dev, u32 data, u32 reg)
{
	writel(data, dev->regs_base + reg);
}

static inline unsigned int sahara_read(struct sahara_dev *dev, u32 reg)
{
	return readl(dev->regs_base + reg);
}

static u32 sahara_aes_key_hdr(struct sahara_dev *dev)
{
	u32 hdr = SAHARA_HDR_BASE | SAHARA_HDR_SKHA_ALG_AES |
			SAHARA_HDR_FORM_KEY | SAHARA_HDR_LLO |
			SAHARA_HDR_CHA_SKHA | SAHARA_HDR_PARITY_BIT;

	if (dev->flags & FLAGS_CBC) {
		hdr |= SAHARA_HDR_SKHA_MODE_CBC;
		hdr ^= SAHARA_HDR_PARITY_BIT;
	}

	if (dev->flags & FLAGS_ENCRYPT) {
		hdr |= SAHARA_HDR_SKHA_OP_ENC;
		hdr ^= SAHARA_HDR_PARITY_BIT;
	}

	return hdr;
}

static u32 sahara_aes_data_link_hdr(struct sahara_dev *dev)
{
	return SAHARA_HDR_BASE | SAHARA_HDR_FORM_DATA |
			SAHARA_HDR_CHA_SKHA | SAHARA_HDR_PARITY_BIT;
}

static int sahara_sg_length(struct scatterlist *sg,
			    unsigned int total)
{
	int sg_nb;
	unsigned int len;
	struct scatterlist *sg_list;

	sg_nb = 0;
	sg_list = sg;

	while (total) {
		len = min(sg_list->length, total);

		sg_nb++;
		total -= len;

		sg_list = sg_next(sg_list);
		if (!sg_list)
			total = 0;
	}

	return sg_nb;
}

static char *sahara_err_src[16] = {
	"No error",
	"Header error",
	"Descriptor length error",
	"Descriptor length or pointer error",
	"Link length error",
	"Link pointer error",
	"Input buffer error",
	"Output buffer error",
	"Output buffer starvation",
	"Internal state fault",
	"General descriptor problem",
	"Reserved",
	"Descriptor address error",
	"Link address error",
	"CHA error",
	"DMA error"
};

static char *sahara_err_dmasize[4] = {
	"Byte transfer",
	"Half-word transfer",
	"Word transfer",
	"Reserved"
};

static char *sahara_err_dmasrc[8] = {
	"No error",
	"AHB bus error",
	"Internal IP bus error",
	"Parity error",
	"DMA crosses 256 byte boundary",
	"DMA is busy",
	"Reserved",
	"DMA HW error"
};

static char *sahara_cha_errsrc[12] = {
	"Input buffer non-empty",
	"Illegal address",
	"Illegal mode",
	"Illegal data size",
	"Illegal key size",
	"Write during processing",
	"CTX read during processing",
	"HW error",
	"Input buffer disabled/underflow",
	"Output buffer disabled/overflow",
	"DES key parity error",
	"Reserved"
};

static char *sahara_cha_err[4] = { "No error", "SKHA", "MDHA", "RNG" };

static void sahara_decode_error(struct sahara_dev *dev, unsigned int error)
{
	u8 source = SAHARA_ERRSTATUS_GET_SOURCE(error);
	u16 chasrc = ffs(SAHARA_ERRSTATUS_GET_CHASRC(error));

	dev_err(dev->device, "%s: Error Register = 0x%08x\n", __func__, error);

	dev_err(dev->device, "	- %s.\n", sahara_err_src[source]);

	if (source == SAHARA_ERRSOURCE_DMA) {
		if (error & SAHARA_ERRSTATUS_DMA_DIR)
			dev_err(dev->device, "		* DMA read.\n");
		else
			dev_err(dev->device, "		* DMA write.\n");

		dev_err(dev->device, "		* %s.\n",
		       sahara_err_dmasize[SAHARA_ERRSTATUS_GET_DMASZ(error)]);
		dev_err(dev->device, "		* %s.\n",
		       sahara_err_dmasrc[SAHARA_ERRSTATUS_GET_DMASRC(error)]);
	} else if (source == SAHARA_ERRSOURCE_CHA) {
		dev_err(dev->device, "		* %s.\n",
			sahara_cha_errsrc[chasrc]);
		dev_err(dev->device, "		* %s.\n",
		       sahara_cha_err[SAHARA_ERRSTATUS_GET_CHAERR(error)]);
	}
	dev_err(dev->device, "\n");
}

static char *sahara_state[4] = { "Idle", "Busy", "Error", "HW Fault" };

static void sahara_decode_status(struct sahara_dev *dev, unsigned int status)
{
	u8 state;

	if (!IS_ENABLED(DEBUG))
		return;

	state = SAHARA_STATUS_GET_STATE(status);

	dev_dbg(dev->device, "%s: Status Register = 0x%08x\n",
		__func__, status);

	dev_dbg(dev->device, "	- State = %d:\n", state);
	if (state & SAHARA_STATE_COMP_FLAG)
		dev_dbg(dev->device, "		* Descriptor completed. IRQ pending.\n");

	dev_dbg(dev->device, "		* %s.\n",
	       sahara_state[state & ~SAHARA_STATE_COMP_FLAG]);

	if (status & SAHARA_STATUS_DAR_FULL)
		dev_dbg(dev->device, "	- DAR Full.\n");
	if (status & SAHARA_STATUS_ERROR)
		dev_dbg(dev->device, "	- Error.\n");
	if (status & SAHARA_STATUS_SECURE)
		dev_dbg(dev->device, "	- Secure.\n");
	if (status & SAHARA_STATUS_FAIL)
		dev_dbg(dev->device, "	- Fail.\n");
	if (status & SAHARA_STATUS_RNG_RESEED)
		dev_dbg(dev->device, "	- RNG Reseed Request.\n");
	if (status & SAHARA_STATUS_ACTIVE_RNG)
		dev_dbg(dev->device, "	- RNG Active.\n");
	if (status & SAHARA_STATUS_ACTIVE_MDHA)
		dev_dbg(dev->device, "	- MDHA Active.\n");
	if (status & SAHARA_STATUS_ACTIVE_SKHA)
		dev_dbg(dev->device, "	- SKHA Active.\n");

	if (status & SAHARA_STATUS_MODE_BATCH)
		dev_dbg(dev->device, "	- Batch Mode.\n");
	else if (status & SAHARA_STATUS_MODE_DEDICATED)
		dev_dbg(dev->device, "	- Decidated Mode.\n");
	else if (status & SAHARA_STATUS_MODE_DEBUG)
		dev_dbg(dev->device, "	- Debug Mode.\n");

	dev_dbg(dev->device, "	- Internal state = 0x%02x\n",
	       SAHARA_STATUS_GET_ISTATE(status));

	dev_dbg(dev->device, "Current DAR: 0x%08x\n",
		sahara_read(dev, SAHARA_REG_CDAR));
	dev_dbg(dev->device, "Initial DAR: 0x%08x\n\n",
		sahara_read(dev, SAHARA_REG_IDAR));
}

static void sahara_dump_descriptors(struct sahara_dev *dev)
{
	int i;

	if (!IS_ENABLED(DEBUG))
		return;

	for (i = 0; i < SAHARA_MAX_HW_DESC; i++) {
		dev_dbg(dev->device, "Descriptor (%d) (0x%08x):\n",
			i, dev->hw_phys_desc[i]);
		dev_dbg(dev->device, "\thdr = 0x%08x\n", dev->hw_desc[i]->hdr);
		dev_dbg(dev->device, "\tlen1 = %u\n", dev->hw_desc[i]->len1);
		dev_dbg(dev->device, "\tp1 = 0x%08x\n", dev->hw_desc[i]->p1);
		dev_dbg(dev->device, "\tlen2 = %u\n", dev->hw_desc[i]->len2);
		dev_dbg(dev->device, "\tp2 = 0x%08x\n", dev->hw_desc[i]->p2);
		dev_dbg(dev->device, "\tnext = 0x%08x\n",
			dev->hw_desc[i]->next);
	}
	dev_dbg(dev->device, "\n");
}

static void sahara_dump_links(struct sahara_dev *dev)
{
	int i;

	if (!IS_ENABLED(DEBUG))
		return;

	for (i = 0; i < SAHARA_MAX_HW_LINK; i++) {
		dev_dbg(dev->device, "Link (%d) (0x%08x):\n",
			i, dev->hw_phys_link[i]);
		dev_dbg(dev->device, "\tlen = %u\n", dev->hw_link[i]->len);
		dev_dbg(dev->device, "\tp = 0x%08x\n", dev->hw_link[i]->p);
		dev_dbg(dev->device, "\tnext = 0x%08x\n",
			dev->hw_link[i]->next);
	}
	dev_dbg(dev->device, "\n");
}

static void sahara_aes_done_task(unsigned long data)
{
	struct sahara_dev *dev = (struct sahara_dev *)data;

	dma_unmap_sg(dev->device, dev->out_sg, dev->nb_out_sg,
		DMA_TO_DEVICE);
	dma_unmap_sg(dev->device, dev->in_sg, dev->nb_in_sg,
		DMA_FROM_DEVICE);

	spin_lock(&dev->lock);
	clear_bit(FLAGS_BUSY, &dev->flags);
	spin_unlock(&dev->lock);

	dev->req->base.complete(&dev->req->base, dev->error);
}

void sahara_watchdog(unsigned long data)
{
	struct sahara_dev *dev = (struct sahara_dev *)data;
	unsigned int err = sahara_read(dev, SAHARA_REG_ERRSTATUS);
	unsigned int stat = sahara_read(dev, SAHARA_REG_STATUS);

	sahara_decode_status(dev, stat);
	sahara_decode_error(dev, err);
	dev->error = -ETIMEDOUT;
	sahara_aes_done_task(data);
}

static int sahara_hw_descriptor_create(struct sahara_dev *dev)
{
	struct sahara_ctx *ctx = dev->ctx;
	struct scatterlist *sg;
	int ret;
	int i, j;

	/* Copy new key if necessary */
	if (ctx->flags & FLAGS_NEW_KEY) {
		memcpy(dev->key_base, ctx->key, ctx->keylen);
		ctx->flags &= ~FLAGS_NEW_KEY;

		if (dev->flags & FLAGS_CBC) {
			dev->hw_desc[0]->len1 = AES_BLOCK_SIZE;
			dev->hw_desc[0]->p1 = dev->iv_phys_base;
		} else {
			dev->hw_desc[0]->len1 = 0;
			dev->hw_desc[0]->p1 = 0;
		}
		dev->hw_desc[0]->len2 = ctx->keylen;
		dev->hw_desc[0]->p2 = dev->key_phys_base;
		dev->hw_desc[0]->next = dev->hw_phys_desc[1];
	}
	dev->hw_desc[0]->hdr = sahara_aes_key_hdr(dev);

	dev->nb_in_sg = sahara_sg_length(dev->in_sg, dev->total);
	dev->nb_out_sg = sahara_sg_length(dev->out_sg, dev->total);
	if ((dev->nb_in_sg + dev->nb_out_sg) > SAHARA_MAX_HW_LINK) {
		dev_err(dev->device, "not enough hw links (%d)\n",
			dev->nb_in_sg + dev->nb_out_sg);
		return -EINVAL;
	}

	ret = dma_map_sg(dev->device, dev->in_sg, dev->nb_in_sg,
			 DMA_TO_DEVICE);
	if (ret != dev->nb_in_sg) {
		dev_err(dev->device, "couldn't map in sg\n");
		goto unmap_in;
	}
	ret = dma_map_sg(dev->device, dev->out_sg, dev->nb_out_sg,
			 DMA_FROM_DEVICE);
	if (ret != dev->nb_out_sg) {
		dev_err(dev->device, "couldn't map out sg\n");
		goto unmap_out;
	}

	/* Create input links */
	dev->hw_desc[1]->p1 = dev->hw_phys_link[0];
	sg = dev->in_sg;
	for (i = 0; i < dev->nb_in_sg; i++) {
		dev->hw_link[i]->len = sg->length;
		dev->hw_link[i]->p = sg->dma_address;
		if (i == (dev->nb_in_sg - 1)) {
			dev->hw_link[i]->next = 0;
		} else {
			dev->hw_link[i]->next = dev->hw_phys_link[i + 1];
			sg = sg_next(sg);
		}
	}

	/* Create output links */
	dev->hw_desc[1]->p2 = dev->hw_phys_link[i];
	sg = dev->out_sg;
	for (j = i; j < dev->nb_out_sg + i; j++) {
		dev->hw_link[j]->len = sg->length;
		dev->hw_link[j]->p = sg->dma_address;
		if (j == (dev->nb_out_sg + i - 1)) {
			dev->hw_link[j]->next = 0;
		} else {
			dev->hw_link[j]->next = dev->hw_phys_link[j + 1];
			sg = sg_next(sg);
		}
	}

	/* Fill remaining fields of hw_desc[1] */
	dev->hw_desc[1]->hdr = sahara_aes_data_link_hdr(dev);
	dev->hw_desc[1]->len1 = dev->total;
	dev->hw_desc[1]->len2 = dev->total;
	dev->hw_desc[1]->next = 0;

	sahara_dump_descriptors(dev);
	sahara_dump_links(dev);

	/* Start processing descriptor chain. */
	mod_timer(&dev->watchdog,
		  jiffies + msecs_to_jiffies(SAHARA_TIMEOUT_MS));
	sahara_write(dev, dev->hw_phys_desc[0], SAHARA_REG_DAR);

	return 0;

unmap_out:
	dma_unmap_sg(dev->device, dev->out_sg, dev->nb_out_sg,
		DMA_TO_DEVICE);
unmap_in:
	dma_unmap_sg(dev->device, dev->in_sg, dev->nb_in_sg,
		DMA_FROM_DEVICE);

	return -EINVAL;
}

static void sahara_aes_queue_task(unsigned long data)
{
	struct sahara_dev *dev = (struct sahara_dev *)data;
	struct crypto_async_request *async_req, *backlog;
	struct sahara_ctx *ctx;
	struct sahara_aes_reqctx *rctx;
	struct ablkcipher_request *req;
	int ret;

	spin_lock(&dev->lock);
	backlog = crypto_get_backlog(&dev->queue);
	async_req = crypto_dequeue_request(&dev->queue);
	if (!async_req)
		clear_bit(FLAGS_BUSY, &dev->flags);
	spin_unlock(&dev->lock);

	if (!async_req)
		return;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ablkcipher_request_cast(async_req);

	/* Request is ready to be dispatched by the device */
	dev_dbg(dev->device,
		"dispatch request (nbytes=%d, src=%p, dst=%p)\n",
		req->nbytes, req->src, req->dst);

	/* assign new request to device */
	dev->req = req;
	dev->total = req->nbytes;
	dev->in_sg = req->src;
	dev->out_sg = req->dst;

	rctx = ablkcipher_request_ctx(req);
	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));
	rctx->mode &= FLAGS_MODE_MASK;
	dev->flags = (dev->flags & ~FLAGS_MODE_MASK) | rctx->mode;

	if ((dev->flags & FLAGS_CBC) && req->info)
		memcpy(dev->iv_base, req->info, AES_KEYSIZE_128);

	/* assign new context to device */
	ctx->dev = dev;
	dev->ctx = ctx;

	ret = sahara_hw_descriptor_create(dev);
	if (ret < 0) {
		spin_lock(&dev->lock);
		clear_bit(FLAGS_BUSY, &dev->flags);
		spin_unlock(&dev->lock);
		dev->req->base.complete(&dev->req->base, ret);
	}
}

static int sahara_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct sahara_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	int ret;

	ctx->keylen = keylen;

	/* SAHARA only supports 128bit keys */
	if (keylen == AES_KEYSIZE_128) {
		memcpy(ctx->key, key, keylen);
		ctx->flags |= FLAGS_NEW_KEY;
		return 0;
	}

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 && keylen != AES_KEYSIZE_256)
		return -EINVAL;

	/*
	 * The requested key size is not supported by HW, do a fallback.
	 */
	ctx->fallback->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	ctx->fallback->base.crt_flags |=
		(tfm->base.crt_flags & CRYPTO_TFM_REQ_MASK);

	ret = crypto_ablkcipher_setkey(ctx->fallback, key, keylen);
	if (ret) {
		struct crypto_tfm *tfm_aux = crypto_ablkcipher_tfm(tfm);

		tfm_aux->crt_flags &= ~CRYPTO_TFM_RES_MASK;
		tfm_aux->crt_flags |=
			(ctx->fallback->base.crt_flags & CRYPTO_TFM_RES_MASK);
	}
	return ret;
}

static int sahara_aes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct sahara_ctx *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));
	struct sahara_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	struct sahara_dev *dev = dev_ptr;
	int err = 0;
	int busy;

	dev_dbg(dev->device, "nbytes: %d, enc: %d, cbc: %d\n",
		req->nbytes, !!(mode & FLAGS_ENCRYPT), !!(mode & FLAGS_CBC));

	if (!IS_ALIGNED(req->nbytes, AES_BLOCK_SIZE)) {
		dev_err(dev->device,
			"request size is not exact amount of AES blocks\n");
		return -EINVAL;
	}

	ctx->dev = dev;

	rctx->mode = mode;
	spin_lock_bh(&dev->lock);
	err = ablkcipher_enqueue_request(&dev->queue, req);
	busy = test_and_set_bit(FLAGS_BUSY, &dev->flags);
	spin_unlock_bh(&dev->lock);

	if (!busy)
		tasklet_schedule(&dev->queue_task);

	return err;
}

static int sahara_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm =
		crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct sahara_ctx *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));
	int err;

	if (unlikely(ctx->keylen != AES_KEYSIZE_128)) {
		ablkcipher_request_set_tfm(req, ctx->fallback);
		err = crypto_ablkcipher_encrypt(req);
		ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(tfm));
		return err;
	}

	return sahara_aes_crypt(req, FLAGS_ENCRYPT);
}

static int sahara_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm =
		crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct sahara_ctx *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));
	int err;

	if (unlikely(ctx->keylen != AES_KEYSIZE_128)) {
		ablkcipher_request_set_tfm(req, ctx->fallback);
		err = crypto_ablkcipher_decrypt(req);
		ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(tfm));
		return err;
	}

	return sahara_aes_crypt(req, 0);
}

static int sahara_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm =
		crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct sahara_ctx *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));
	int err;

	if (unlikely(ctx->keylen != AES_KEYSIZE_128)) {
		ablkcipher_request_set_tfm(req, ctx->fallback);
		err = crypto_ablkcipher_encrypt(req);
		ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(tfm));
		return err;
	}

	return sahara_aes_crypt(req, FLAGS_ENCRYPT | FLAGS_CBC);
}

static int sahara_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm =
		crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct sahara_ctx *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));
	int err;

	if (unlikely(ctx->keylen != AES_KEYSIZE_128)) {
		ablkcipher_request_set_tfm(req, ctx->fallback);
		err = crypto_ablkcipher_decrypt(req);
		ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(tfm));
		return err;
	}

	return sahara_aes_crypt(req, FLAGS_CBC);
}

static int sahara_aes_cra_init(struct crypto_tfm *tfm)
{
	const char *name = tfm->__crt_alg->cra_name;
	struct sahara_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->fallback = crypto_alloc_ablkcipher(name, 0,
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback)) {
		pr_err("Error allocating fallback algo %s\n", name);
		return PTR_ERR(ctx->fallback);
	}

	tfm->crt_ablkcipher.reqsize = sizeof(struct sahara_aes_reqctx);

	return 0;
}

static void sahara_aes_cra_exit(struct crypto_tfm *tfm)
{
	struct sahara_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->fallback)
		crypto_free_ablkcipher(ctx->fallback);
	ctx->fallback = NULL;
}

static struct crypto_alg aes_algs[] = {
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "sahara-ecb-aes",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
			CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sahara_ctx),
	.cra_alignmask		= 0x0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sahara_aes_cra_init,
	.cra_exit		= sahara_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE ,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sahara_aes_setkey,
		.encrypt	= sahara_aes_ecb_encrypt,
		.decrypt	= sahara_aes_ecb_decrypt,
	}
}, {
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "sahara-cbc-aes",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
			CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sahara_ctx),
	.cra_alignmask		= 0x0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sahara_aes_cra_init,
	.cra_exit		= sahara_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE ,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= sahara_aes_setkey,
		.encrypt	= sahara_aes_cbc_encrypt,
		.decrypt	= sahara_aes_cbc_decrypt,
	}
}
};

static irqreturn_t sahara_irq_handler(int irq, void *data)
{
	struct sahara_dev *dev = (struct sahara_dev *)data;
	unsigned int stat = sahara_read(dev, SAHARA_REG_STATUS);
	unsigned int err = sahara_read(dev, SAHARA_REG_ERRSTATUS);

	del_timer(&dev->watchdog);

	sahara_write(dev, SAHARA_CMD_CLEAR_INT | SAHARA_CMD_CLEAR_ERR,
		     SAHARA_REG_CMD);

	sahara_decode_status(dev, stat);

	if (SAHARA_STATUS_GET_STATE(stat) == SAHARA_STATE_BUSY) {
		return IRQ_NONE;
	} else if (SAHARA_STATUS_GET_STATE(stat) == SAHARA_STATE_COMPLETE) {
		dev->error = 0;
	} else {
		sahara_decode_error(dev, err);
		dev->error = -EINVAL;
	}

	tasklet_schedule(&dev->done_task);

	return IRQ_HANDLED;
}


static int sahara_register_algs(struct sahara_dev *dev)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		INIT_LIST_HEAD(&aes_algs[i].cra_list);
		err = crypto_register_alg(&aes_algs[i]);
		if (err)
			goto err_aes_algs;
	}

	return 0;

err_aes_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&aes_algs[j]);

	return err;
}

static void sahara_unregister_algs(struct sahara_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++)
		crypto_unregister_alg(&aes_algs[i]);
}

static struct platform_device_id sahara_platform_ids[] = {
	{ .name = "sahara-imx27" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, sahara_platform_ids);

static struct of_device_id sahara_dt_ids[] = {
	{ .compatible = "fsl,imx27-sahara" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sahara_dt_ids);

static int sahara_probe(struct platform_device *pdev)
{
	struct sahara_dev *dev;
	struct resource *res;
	u32 version;
	int irq;
	int err;
	int i;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct sahara_dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&pdev->dev, "unable to alloc data struct.\n");
		return -ENOMEM;
	}

	dev->device = &pdev->dev;
	platform_set_drvdata(pdev, dev);

	/* Get the base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory region resource\n");
		return -ENODEV;
	}

	if (devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), SAHARA_NAME) == NULL) {
		dev_err(&pdev->dev, "failed to request memory region\n");
		return -ENOENT;
	}
	dev->regs_base = devm_ioremap(&pdev->dev, res->start,
				      resource_size(res));
	if (!dev->regs_base) {
		dev_err(&pdev->dev, "failed to ioremap address region\n");
		return -ENOENT;
	}

	/* Get the IRQ */
	irq = platform_get_irq(pdev,  0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		return irq;
	}

	if (devm_request_irq(&pdev->dev, irq, sahara_irq_handler,
		0, SAHARA_NAME, dev) < 0) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return -ENOENT;
	}

	/* clocks */
	dev->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(dev->clk_ipg)) {
		dev_err(&pdev->dev, "Could not get ipg clock\n");
		return PTR_ERR(dev->clk_ipg);
	}

	dev->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(dev->clk_ahb)) {
		dev_err(&pdev->dev, "Could not get ahb clock\n");
		return PTR_ERR(dev->clk_ahb);
	}

	/* Allocate HW descriptors */
	dev->hw_desc[0] = dma_alloc_coherent(&pdev->dev,
			SAHARA_MAX_HW_DESC * sizeof(struct sahara_hw_desc),
			&dev->hw_phys_desc[0], GFP_KERNEL);
	if (!dev->hw_desc[0]) {
		dev_err(&pdev->dev, "Could not allocate hw descriptors\n");
		return -ENOMEM;
	}
	dev->hw_desc[1] = dev->hw_desc[0] + 1;
	dev->hw_phys_desc[1] = dev->hw_phys_desc[0] +
				sizeof(struct sahara_hw_desc);

	/* Allocate space for iv and key */
	dev->key_base = dma_alloc_coherent(&pdev->dev, 2 * AES_KEYSIZE_128,
				&dev->key_phys_base, GFP_KERNEL);
	if (!dev->key_base) {
		dev_err(&pdev->dev, "Could not allocate memory for key\n");
		err = -ENOMEM;
		goto err_key;
	}
	dev->iv_base = dev->key_base + AES_KEYSIZE_128;
	dev->iv_phys_base = dev->key_phys_base + AES_KEYSIZE_128;

	/* Allocate space for HW links */
	dev->hw_link[0] = dma_alloc_coherent(&pdev->dev,
			SAHARA_MAX_HW_LINK * sizeof(struct sahara_hw_link),
			&dev->hw_phys_link[0], GFP_KERNEL);
	if (!dev->hw_link) {
		dev_err(&pdev->dev, "Could not allocate hw links\n");
		err = -ENOMEM;
		goto err_link;
	}
	for (i = 1; i < SAHARA_MAX_HW_LINK; i++) {
		dev->hw_phys_link[i] = dev->hw_phys_link[i - 1] +
					sizeof(struct sahara_hw_link);
		dev->hw_link[i] = dev->hw_link[i - 1] + 1;
	}

	crypto_init_queue(&dev->queue, SAHARA_QUEUE_LENGTH);

	dev_ptr = dev;

	tasklet_init(&dev->queue_task, sahara_aes_queue_task,
		     (unsigned long)dev);
	tasklet_init(&dev->done_task, sahara_aes_done_task,
		     (unsigned long)dev);

	init_timer(&dev->watchdog);
	dev->watchdog.function = &sahara_watchdog;
	dev->watchdog.data = (unsigned long)dev;

	clk_prepare_enable(dev->clk_ipg);
	clk_prepare_enable(dev->clk_ahb);

	version = sahara_read(dev, SAHARA_REG_VERSION);
	if (version != SAHARA_VERSION_3) {
		dev_err(&pdev->dev, "SAHARA version %d not supported\n",
			version);
		err = -ENODEV;
		goto err_algs;
	}

	sahara_write(dev, SAHARA_CMD_RESET | SAHARA_CMD_MODE_BATCH,
		     SAHARA_REG_CMD);
	sahara_write(dev, SAHARA_CONTROL_SET_THROTTLE(0) |
			SAHARA_CONTROL_SET_MAXBURST(8) |
			SAHARA_CONTROL_RNG_AUTORSD |
			SAHARA_CONTROL_ENABLE_INT,
			SAHARA_REG_CONTROL);

	err = sahara_register_algs(dev);
	if (err)
		goto err_algs;

	dev_info(&pdev->dev, "SAHARA version %d initialized\n", version);

	return 0;

err_algs:
	dma_free_coherent(&pdev->dev,
			  SAHARA_MAX_HW_LINK * sizeof(struct sahara_hw_link),
			  dev->hw_link[0], dev->hw_phys_link[0]);
	clk_disable_unprepare(dev->clk_ipg);
	clk_disable_unprepare(dev->clk_ahb);
	dev_ptr = NULL;
err_link:
	dma_free_coherent(&pdev->dev,
			  2 * AES_KEYSIZE_128,
			  dev->key_base, dev->key_phys_base);
err_key:
	dma_free_coherent(&pdev->dev,
			  SAHARA_MAX_HW_DESC * sizeof(struct sahara_hw_desc),
			  dev->hw_desc[0], dev->hw_phys_desc[0]);

	return err;
}

static int sahara_remove(struct platform_device *pdev)
{
	struct sahara_dev *dev = platform_get_drvdata(pdev);

	dma_free_coherent(&pdev->dev,
			  SAHARA_MAX_HW_LINK * sizeof(struct sahara_hw_link),
			  dev->hw_link[0], dev->hw_phys_link[0]);
	dma_free_coherent(&pdev->dev,
			  2 * AES_KEYSIZE_128,
			  dev->key_base, dev->key_phys_base);
	dma_free_coherent(&pdev->dev,
			  SAHARA_MAX_HW_DESC * sizeof(struct sahara_hw_desc),
			  dev->hw_desc[0], dev->hw_phys_desc[0]);

	tasklet_kill(&dev->done_task);
	tasklet_kill(&dev->queue_task);

	sahara_unregister_algs(dev);

	clk_disable_unprepare(dev->clk_ipg);
	clk_disable_unprepare(dev->clk_ahb);

	dev_ptr = NULL;

	return 0;
}

static struct platform_driver sahara_driver = {
	.probe		= sahara_probe,
	.remove		= sahara_remove,
	.driver		= {
		.name	= SAHARA_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sahara_dt_ids),
	},
	.id_table = sahara_platform_ids,
};

module_platform_driver(sahara_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_DESCRIPTION("SAHARA2 HW crypto accelerator");
