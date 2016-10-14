/*
 * Copyright (C) 2016 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 *
 * The driver is based on information gathered from
 * drivers/mxc/security/mxc_scc.c which can be found in
 * the Freescale linux-2.6-imx.git in the imx_2.6.35_maintain branch.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <crypto/algapi.h>
#include <crypto/des.h>

/* Secure Memory (SCM) registers */
#define SCC_SCM_RED_START		0x0000
#define SCC_SCM_BLACK_START		0x0004
#define SCC_SCM_LENGTH			0x0008
#define SCC_SCM_CTRL			0x000C
#define SCC_SCM_STATUS			0x0010
#define SCC_SCM_ERROR_STATUS		0x0014
#define SCC_SCM_INTR_CTRL		0x0018
#define SCC_SCM_CFG			0x001C
#define SCC_SCM_INIT_VECTOR_0		0x0020
#define SCC_SCM_INIT_VECTOR_1		0x0024
#define SCC_SCM_RED_MEMORY		0x0400
#define SCC_SCM_BLACK_MEMORY		0x0800

/* Security Monitor (SMN) Registers */
#define SCC_SMN_STATUS			0x1000
#define SCC_SMN_COMMAND		0x1004
#define SCC_SMN_SEQ_START		0x1008
#define SCC_SMN_SEQ_END		0x100C
#define SCC_SMN_SEQ_CHECK		0x1010
#define SCC_SMN_BIT_COUNT		0x1014
#define SCC_SMN_BITBANK_INC_SIZE	0x1018
#define SCC_SMN_BITBANK_DECREMENT	0x101C
#define SCC_SMN_COMPARE_SIZE		0x1020
#define SCC_SMN_PLAINTEXT_CHECK	0x1024
#define SCC_SMN_CIPHERTEXT_CHECK	0x1028
#define SCC_SMN_TIMER_IV		0x102C
#define SCC_SMN_TIMER_CONTROL		0x1030
#define SCC_SMN_DEBUG_DETECT_STAT	0x1034
#define SCC_SMN_TIMER			0x1038

#define SCC_SCM_CTRL_START_CIPHER	BIT(2)
#define SCC_SCM_CTRL_CBC_MODE		BIT(1)
#define SCC_SCM_CTRL_DECRYPT_MODE	BIT(0)

#define SCC_SCM_STATUS_LEN_ERR		BIT(12)
#define SCC_SCM_STATUS_SMN_UNBLOCKED	BIT(11)
#define SCC_SCM_STATUS_CIPHERING_DONE	BIT(10)
#define SCC_SCM_STATUS_ZEROIZING_DONE	BIT(9)
#define SCC_SCM_STATUS_INTR_STATUS	BIT(8)
#define SCC_SCM_STATUS_SEC_KEY		BIT(7)
#define SCC_SCM_STATUS_INTERNAL_ERR	BIT(6)
#define SCC_SCM_STATUS_BAD_SEC_KEY	BIT(5)
#define SCC_SCM_STATUS_ZEROIZE_FAIL	BIT(4)
#define SCC_SCM_STATUS_SMN_BLOCKED	BIT(3)
#define SCC_SCM_STATUS_CIPHERING	BIT(2)
#define SCC_SCM_STATUS_ZEROIZING	BIT(1)
#define SCC_SCM_STATUS_BUSY		BIT(0)

#define SCC_SMN_STATUS_STATE_MASK	0x0000001F
#define SCC_SMN_STATE_START		0x0
/* The SMN is zeroizing its RAM during reset */
#define SCC_SMN_STATE_ZEROIZE_RAM	0x5
/* SMN has passed internal checks */
#define SCC_SMN_STATE_HEALTH_CHECK	0x6
/* Fatal Security Violation. SMN is locked, SCM is inoperative. */
#define SCC_SMN_STATE_FAIL		0x9
/* SCC is in secure state. SCM is using secret key. */
#define SCC_SMN_STATE_SECURE		0xA
/* SCC is not secure. SCM is using default key. */
#define SCC_SMN_STATE_NON_SECURE	0xC

#define SCC_SCM_INTR_CTRL_ZEROIZE_MEM	BIT(2)
#define SCC_SCM_INTR_CTRL_CLR_INTR	BIT(1)
#define SCC_SCM_INTR_CTRL_MASK_INTR	BIT(0)

/* Size, in blocks, of Red memory. */
#define SCC_SCM_CFG_BLACK_SIZE_MASK	0x07fe0000
#define SCC_SCM_CFG_BLACK_SIZE_SHIFT	17
/* Size, in blocks, of Black memory. */
#define SCC_SCM_CFG_RED_SIZE_MASK	0x0001ff80
#define SCC_SCM_CFG_RED_SIZE_SHIFT	7
/* Number of bytes per block. */
#define SCC_SCM_CFG_BLOCK_SIZE_MASK	0x0000007f

#define SCC_SMN_COMMAND_TAMPER_LOCK	BIT(4)
#define SCC_SMN_COMMAND_CLR_INTR	BIT(3)
#define SCC_SMN_COMMAND_CLR_BIT_BANK	BIT(2)
#define SCC_SMN_COMMAND_EN_INTR	BIT(1)
#define SCC_SMN_COMMAND_SET_SOFTWARE_ALARM  BIT(0)

#define SCC_KEY_SLOTS			20
#define SCC_MAX_KEY_SIZE		32
#define SCC_KEY_SLOT_SIZE		32

#define SCC_CRC_CCITT_START		0xFFFF

/*
 * Offset into each RAM of the base of the area which is not
 * used for Stored Keys.
 */
#define SCC_NON_RESERVED_OFFSET	(SCC_KEY_SLOTS * SCC_KEY_SLOT_SIZE)

/* Fixed padding for appending to plaintext to fill out a block */
static char scc_block_padding[8] = { 0x80, 0, 0, 0, 0, 0, 0, 0 };

enum mxc_scc_state {
	SCC_STATE_OK,
	SCC_STATE_UNIMPLEMENTED,
	SCC_STATE_FAILED
};

struct mxc_scc {
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	bool			hw_busy;
	spinlock_t		lock;
	struct crypto_queue	queue;
	struct crypto_async_request *req;
	int			block_size_bytes;
	int			black_ram_size_blocks;
	int			memory_size_bytes;
	int			bytes_remaining;

	void __iomem		*red_memory;
	void __iomem		*black_memory;
};

struct mxc_scc_ctx {
	struct mxc_scc		*scc;
	struct scatterlist	*sg_src;
	size_t			src_nents;
	struct scatterlist	*sg_dst;
	size_t			dst_nents;
	unsigned int		offset;
	unsigned int		size;
	unsigned int		ctrl;
};

struct mxc_scc_crypto_tmpl {
	struct mxc_scc *scc;
	struct crypto_alg alg;
};

static int mxc_scc_get_data(struct mxc_scc_ctx *ctx,
			    struct crypto_async_request *req)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mxc_scc *scc = ctx->scc;
	size_t len;
	void __iomem *from;

	if (ctx->ctrl & SCC_SCM_CTRL_DECRYPT_MODE)
		from = scc->red_memory;
	else
		from = scc->black_memory;

	dev_dbg(scc->dev, "pcopy: from 0x%p %d bytes\n", from,
		ctx->dst_nents * 8);
	len = sg_pcopy_from_buffer(ablkreq->dst, ctx->dst_nents,
				   from, ctx->size, ctx->offset);
	if (!len) {
		dev_err(scc->dev, "pcopy err from 0x%p (len=%d)\n", from, len);
		return -EINVAL;
	}

#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "red memory@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->red_memory, ctx->size, 1);
	print_hex_dump(KERN_ERR,
		       "black memory@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->black_memory, ctx->size, 1);
#endif

	ctx->offset += len;

	if (ctx->offset < ablkreq->nbytes)
		return -EINPROGRESS;

	return 0;
}

static int mxc_scc_ablkcipher_req_init(struct ablkcipher_request *req,
				       struct mxc_scc_ctx *ctx)
{
	struct mxc_scc *scc = ctx->scc;
	int nents;

	nents = sg_nents_for_len(req->src, req->nbytes);
	if (nents < 0) {
		dev_err(scc->dev, "Invalid number of src SC");
		return nents;
	}
	ctx->src_nents = nents;

	nents = sg_nents_for_len(req->dst, req->nbytes);
	if (nents < 0) {
		dev_err(scc->dev, "Invalid number of dst SC");
		return nents;
	}
	ctx->dst_nents = nents;

	ctx->size = 0;
	ctx->offset = 0;

	return 0;
}

static int mxc_scc_ablkcipher_req_complete(struct crypto_async_request *req,
					   struct mxc_scc_ctx *ctx,
					   int result)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mxc_scc *scc = ctx->scc;

	scc->req = NULL;
	scc->bytes_remaining = scc->memory_size_bytes;

	if (ctx->ctrl & SCC_SCM_CTRL_CBC_MODE)
		memcpy(ablkreq->info, scc->base + SCC_SCM_INIT_VECTOR_0,
		       scc->block_size_bytes);

	req->complete(req, result);
	scc->hw_busy = false;

	return 0;
}

static int mxc_scc_put_data(struct mxc_scc_ctx *ctx,
			     struct ablkcipher_request *req)
{
	u8 padding_buffer[sizeof(u16) + sizeof(scc_block_padding)];
	size_t len = min_t(size_t, req->nbytes - ctx->offset,
			   ctx->scc->bytes_remaining);
	unsigned int padding_byte_count = 0;
	struct mxc_scc *scc = ctx->scc;
	void __iomem *to;

	if (ctx->ctrl & SCC_SCM_CTRL_DECRYPT_MODE)
		to = scc->black_memory;
	else
		to = scc->red_memory;

	if (ctx->ctrl & SCC_SCM_CTRL_CBC_MODE && req->info)
		memcpy(scc->base + SCC_SCM_INIT_VECTOR_0, req->info,
		       scc->block_size_bytes);

	len = sg_pcopy_to_buffer(req->src, ctx->src_nents,
				 to, len, ctx->offset);
	if (!len) {
		dev_err(scc->dev, "pcopy err to 0x%p (len=%d)\n", to, len);
		return -EINVAL;
	}

	ctx->size = len;

#ifdef DEBUG
	dev_dbg(scc->dev, "copied %d bytes to 0x%p\n", len, to);
	print_hex_dump(KERN_ERR,
		       "init vector0@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->base + SCC_SCM_INIT_VECTOR_0, scc->block_size_bytes,
		       1);
	print_hex_dump(KERN_ERR,
		       "red memory@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->red_memory, ctx->size, 1);
	print_hex_dump(KERN_ERR,
		       "black memory@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->black_memory, ctx->size, 1);
#endif

	scc->bytes_remaining -= len;

	padding_byte_count = len % scc->block_size_bytes;

	if (padding_byte_count) {
		memcpy(padding_buffer, scc_block_padding, padding_byte_count);
		memcpy(to + len, padding_buffer, padding_byte_count);
		ctx->size += padding_byte_count;
	}

#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "data to encrypt@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       to, ctx->size, 1);
#endif

	return 0;
}

static void mxc_scc_ablkcipher_next(struct mxc_scc_ctx *ctx,
				    struct crypto_async_request *req)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mxc_scc *scc = ctx->scc;
	int err;

	dev_dbg(scc->dev, "dispatch request (nbytes=%d, src=%p, dst=%p)\n",
		ablkreq->nbytes, ablkreq->src, ablkreq->dst);

	writel(0, scc->base + SCC_SCM_ERROR_STATUS);

	err = mxc_scc_put_data(ctx, ablkreq);
	if (err) {
		mxc_scc_ablkcipher_req_complete(req, ctx, err);
		return;
	}

	dev_dbg(scc->dev, "Start encryption (0x%p/0x%p)\n",
		(void *)readl(scc->base + SCC_SCM_RED_START),
		(void *)readl(scc->base + SCC_SCM_BLACK_START));

	/* clear interrupt control registers */
	writel(SCC_SCM_INTR_CTRL_CLR_INTR,
	       scc->base + SCC_SCM_INTR_CTRL);

	writel((ctx->size / ctx->scc->block_size_bytes) - 1,
	       scc->base + SCC_SCM_LENGTH);

	dev_dbg(scc->dev, "Process %d block(s) in 0x%p\n",
		ctx->size / ctx->scc->block_size_bytes,
		(ctx->ctrl & SCC_SCM_CTRL_DECRYPT_MODE) ? scc->black_memory :
		scc->red_memory);

	writel(ctx->ctrl, scc->base + SCC_SCM_CTRL);
}

static irqreturn_t mxc_scc_int(int irq, void *priv)
{
	struct crypto_async_request *req;
	struct mxc_scc_ctx *ctx;
	struct mxc_scc *scc = priv;
	int status;
	int ret;

	status = readl(scc->base + SCC_SCM_STATUS);

	/* clear interrupt control registers */
	writel(SCC_SCM_INTR_CTRL_CLR_INTR, scc->base + SCC_SCM_INTR_CTRL);

	if (status & SCC_SCM_STATUS_BUSY)
		return IRQ_NONE;

	req = scc->req;
	if (req) {
		ctx = crypto_tfm_ctx(req->tfm);
		ret = mxc_scc_get_data(ctx, req);
		if (ret != -EINPROGRESS)
			mxc_scc_ablkcipher_req_complete(req, ctx, ret);
		else
			mxc_scc_ablkcipher_next(ctx, req);
	}

	return IRQ_HANDLED;
}

static int mxc_scc_cra_init(struct crypto_tfm *tfm)
{
	struct mxc_scc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct mxc_scc_crypto_tmpl *algt;

	algt = container_of(alg, struct mxc_scc_crypto_tmpl, alg);

	ctx->scc = algt->scc;
	return 0;
}

static void mxc_scc_dequeue_req_unlocked(struct mxc_scc_ctx *ctx)
{
	struct crypto_async_request *req, *backlog;

	if (ctx->scc->hw_busy)
		return;

	spin_lock_bh(&ctx->scc->lock);
	backlog = crypto_get_backlog(&ctx->scc->queue);
	req = crypto_dequeue_request(&ctx->scc->queue);
	ctx->scc->req = req;
	ctx->scc->hw_busy = true;
	spin_unlock_bh(&ctx->scc->lock);

	if (!req)
		return;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	mxc_scc_ablkcipher_next(ctx, req);
}

static int mxc_scc_queue_req(struct mxc_scc_ctx *ctx,
			     struct crypto_async_request *req)
{
	int ret;

	spin_lock_bh(&ctx->scc->lock);
	ret = crypto_enqueue_request(&ctx->scc->queue, req);
	spin_unlock_bh(&ctx->scc->lock);

	if (ret != -EINPROGRESS)
		return ret;

	mxc_scc_dequeue_req_unlocked(ctx);

	return -EINPROGRESS;
}

static int mxc_scc_des3_op(struct mxc_scc_ctx *ctx,
			   struct ablkcipher_request *req)
{
	int err;

	err = mxc_scc_ablkcipher_req_init(req, ctx);
	if (err)
		return err;

	return mxc_scc_queue_req(ctx, &req->base);
}

static int mxc_scc_ecb_des_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCC_SCM_CTRL_START_CIPHER;

	return mxc_scc_des3_op(ctx, req);
}

static int mxc_scc_ecb_des_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCC_SCM_CTRL_START_CIPHER;
	ctx->ctrl |= SCC_SCM_CTRL_DECRYPT_MODE;

	return mxc_scc_des3_op(ctx, req);
}

static int mxc_scc_cbc_des_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCC_SCM_CTRL_START_CIPHER;
	ctx->ctrl |= SCC_SCM_CTRL_CBC_MODE;

	return mxc_scc_des3_op(ctx, req);
}

static int mxc_scc_cbc_des_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCC_SCM_CTRL_START_CIPHER;
	ctx->ctrl |= SCC_SCM_CTRL_CBC_MODE;
	ctx->ctrl |= SCC_SCM_CTRL_DECRYPT_MODE;

	return mxc_scc_des3_op(ctx, req);
}

static void mxc_scc_hw_init(struct mxc_scc *scc)
{
	int offset;

	offset = SCC_NON_RESERVED_OFFSET / scc->block_size_bytes;

	/* Fill the RED_START register */
	writel(offset, scc->base + SCC_SCM_RED_START);

	/* Fill the BLACK_START register */
	writel(offset, scc->base + SCC_SCM_BLACK_START);

	scc->red_memory = scc->base + SCC_SCM_RED_MEMORY +
			  SCC_NON_RESERVED_OFFSET;

	scc->black_memory = scc->base + SCC_SCM_BLACK_MEMORY +
			    SCC_NON_RESERVED_OFFSET;

	scc->bytes_remaining = scc->memory_size_bytes;
}

static int mxc_scc_get_config(struct mxc_scc *scc)
{
	int config;

	config = readl(scc->base + SCC_SCM_CFG);

	scc->block_size_bytes = config & SCC_SCM_CFG_BLOCK_SIZE_MASK;

	scc->black_ram_size_blocks = config & SCC_SCM_CFG_BLACK_SIZE_MASK;

	scc->memory_size_bytes = (scc->block_size_bytes *
				  scc->black_ram_size_blocks) -
				  SCC_NON_RESERVED_OFFSET;

	return 0;
}

static enum mxc_scc_state mxc_scc_get_state(struct mxc_scc *scc)
{
	enum mxc_scc_state state;
	int status;

	status = readl(scc->base + SCC_SMN_STATUS) &
		       SCC_SMN_STATUS_STATE_MASK;

	/* If in Health Check, try to bringup to secure state */
	if (status & SCC_SMN_STATE_HEALTH_CHECK) {
		/*
		 * Write a simple algorithm to the Algorithm Sequence
		 * Checker (ASC)
		 */
		writel(0xaaaa, scc->base + SCC_SMN_SEQ_START);
		writel(0x5555, scc->base + SCC_SMN_SEQ_END);
		writel(0x5555, scc->base + SCC_SMN_SEQ_CHECK);

		status = readl(scc->base + SCC_SMN_STATUS) &
			       SCC_SMN_STATUS_STATE_MASK;
	}

	switch (status) {
	case SCC_SMN_STATE_NON_SECURE:
	case SCC_SMN_STATE_SECURE:
		state = SCC_STATE_OK;
		break;
	case SCC_SMN_STATE_FAIL:
		state = SCC_STATE_FAILED;
		break;
	default:
		state = SCC_STATE_UNIMPLEMENTED;
		break;
	}

	return state;
}

static struct mxc_scc_crypto_tmpl scc_ecb_des = {
	.alg = {
		.cra_name = "ecb(des3_ede)",
		.cra_driver_name = "ecb-des3-scc",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mxc_scc_ctx),
		.cra_alignmask = 0,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
		.cra_init = mxc_scc_cra_init,
		.cra_u.ablkcipher = {
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.encrypt = mxc_scc_ecb_des_encrypt,
			.decrypt = mxc_scc_ecb_des_decrypt,
		}
	}
};

static struct mxc_scc_crypto_tmpl scc_cbc_des = {
	.alg = {
		.cra_name = "cbc(des3_ede)",
		.cra_driver_name = "cbc-des3-scc",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mxc_scc_ctx),
		.cra_alignmask = 0,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
		.cra_init = mxc_scc_cra_init,
		.cra_u.ablkcipher = {
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.encrypt = mxc_scc_cbc_des_encrypt,
			.decrypt = mxc_scc_cbc_des_decrypt,
		}
	}
};

static struct mxc_scc_crypto_tmpl *scc_crypto_algs[] = {
	&scc_ecb_des,
	&scc_cbc_des,
};

static int mxc_scc_crypto_register(struct mxc_scc *scc)
{
	int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(scc_crypto_algs); i++) {
		scc_crypto_algs[i]->scc = scc;
		err = crypto_register_alg(&scc_crypto_algs[i]->alg);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (--i >= 0)
		crypto_unregister_alg(&scc_crypto_algs[i]->alg);

	return err;
}

static void mxc_scc_crypto_unregister(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(scc_crypto_algs); i++)
		crypto_unregister_alg(&scc_crypto_algs[i]->alg);
}

static int mxc_scc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct mxc_scc *scc;
	enum mxc_scc_state state;
	int irq;
	int ret;
	int i;

	scc = devm_kzalloc(dev, sizeof(*scc), GFP_KERNEL);
	if (!scc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	scc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(scc->base))
		return PTR_ERR(scc->base);

	scc->clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(scc->clk)) {
		dev_err(dev, "Could not get ipg clock\n");
		return PTR_ERR(scc->clk);
	}

	clk_prepare_enable(scc->clk);

	/* clear error status register */
	writel(0x0, scc->base + SCC_SCM_ERROR_STATUS);

	/* clear interrupt control registers */
	writel(SCC_SCM_INTR_CTRL_CLR_INTR |
	       SCC_SCM_INTR_CTRL_MASK_INTR,
	       scc->base + SCC_SCM_INTR_CTRL);

	writel(SCC_SMN_COMMAND_CLR_INTR |
	       SCC_SMN_COMMAND_EN_INTR,
	       scc->base + SCC_SMN_COMMAND);

	scc->dev = dev;
	platform_set_drvdata(pdev, scc);

	ret = mxc_scc_get_config(scc);
	if (ret)
		goto err_out;

	state = mxc_scc_get_state(scc);

	if (state != SCC_STATE_OK) {
		dev_err(dev, "SCC in unusable state %d\n", state);
		ret = -EINVAL;
		goto err_out;
	}

	mxc_scc_hw_init(scc);

	spin_lock_init(&scc->lock);
	/* FIXME: calculate queue from RAM slots */
	crypto_init_queue(&scc->queue, 50);

	for (i = 0; i < 2; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			dev_err(dev, "failed to get irq resource\n");
			ret = -EINVAL;
			goto err_out;
		}

		ret = devm_request_threaded_irq(dev, irq, NULL, mxc_scc_int,
						IRQF_ONESHOT, dev_name(dev), scc);
		if (ret)
			goto err_out;
	}

	ret = mxc_scc_crypto_register(scc);
	if (ret) {
		dev_err(dev, "could not register algorithms");
		goto err_out;
	}

	dev_info(dev, "registered successfully.\n");

	return 0;

err_out:
	clk_disable_unprepare(scc->clk);

	return ret;
}

static int mxc_scc_remove(struct platform_device *pdev)
{
	struct mxc_scc *scc = platform_get_drvdata(pdev);

	mxc_scc_crypto_unregister();

	clk_disable_unprepare(scc->clk);

	return 0;
}

static const struct of_device_id mxc_scc_dt_ids[] = {
	{ .compatible = "fsl,imx25-scc", .data = NULL, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxc_scc_dt_ids);

static struct platform_driver mxc_scc_driver = {
	.probe	= mxc_scc_probe,
	.remove	= mxc_scc_remove,
	.driver	= {
		.name		= "mxc-scc",
		.of_match_table	= mxc_scc_dt_ids,
	},
};

module_platform_driver(mxc_scc_driver);
MODULE_AUTHOR("Steffen Trumtrar <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Freescale i.MX25 SCC Crypto driver");
MODULE_LICENSE("GPL v2");
