// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of STM32 Crypto driver for Linux.
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Lionel DEBIEVE <lionel.debieve@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <crypto/engine.h>
#include <crypto/hash.h>
#include <crypto/md5.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/internal/hash.h>

#define HASH_CR				0x00
#define HASH_DIN			0x04
#define HASH_STR			0x08
#define HASH_IMR			0x20
#define HASH_SR				0x24
#define HASH_CSR(x)			(0x0F8 + ((x) * 0x04))
#define HASH_HREG(x)			(0x310 + ((x) * 0x04))
#define HASH_HWCFGR			0x3F0
#define HASH_VER			0x3F4
#define HASH_ID				0x3F8

/* Control Register */
#define HASH_CR_INIT			BIT(2)
#define HASH_CR_DMAE			BIT(3)
#define HASH_CR_DATATYPE_POS		4
#define HASH_CR_MODE			BIT(6)
#define HASH_CR_MDMAT			BIT(13)
#define HASH_CR_DMAA			BIT(14)
#define HASH_CR_LKEY			BIT(16)

#define HASH_CR_ALGO_SHA1		0x0
#define HASH_CR_ALGO_MD5		0x80
#define HASH_CR_ALGO_SHA224		0x40000
#define HASH_CR_ALGO_SHA256		0x40080

/* Interrupt */
#define HASH_DINIE			BIT(0)
#define HASH_DCIE			BIT(1)

/* Interrupt Mask */
#define HASH_MASK_CALC_COMPLETION	BIT(0)
#define HASH_MASK_DATA_INPUT		BIT(1)

/* Context swap register */
#define HASH_CSR_REGISTER_NUMBER	53

/* Status Flags */
#define HASH_SR_DATA_INPUT_READY	BIT(0)
#define HASH_SR_OUTPUT_READY		BIT(1)
#define HASH_SR_DMA_ACTIVE		BIT(2)
#define HASH_SR_BUSY			BIT(3)

/* STR Register */
#define HASH_STR_NBLW_MASK		GENMASK(4, 0)
#define HASH_STR_DCAL			BIT(8)

#define HASH_FLAGS_INIT			BIT(0)
#define HASH_FLAGS_OUTPUT_READY		BIT(1)
#define HASH_FLAGS_CPU			BIT(2)
#define HASH_FLAGS_DMA_READY		BIT(3)
#define HASH_FLAGS_DMA_ACTIVE		BIT(4)
#define HASH_FLAGS_HMAC_INIT		BIT(5)
#define HASH_FLAGS_HMAC_FINAL		BIT(6)
#define HASH_FLAGS_HMAC_KEY		BIT(7)

#define HASH_FLAGS_FINAL		BIT(15)
#define HASH_FLAGS_FINUP		BIT(16)
#define HASH_FLAGS_ALGO_MASK		GENMASK(21, 18)
#define HASH_FLAGS_MD5			BIT(18)
#define HASH_FLAGS_SHA1			BIT(19)
#define HASH_FLAGS_SHA224		BIT(20)
#define HASH_FLAGS_SHA256		BIT(21)
#define HASH_FLAGS_ERRORS		BIT(22)
#define HASH_FLAGS_HMAC			BIT(23)

#define HASH_OP_UPDATE			1
#define HASH_OP_FINAL			2

enum stm32_hash_data_format {
	HASH_DATA_32_BITS		= 0x0,
	HASH_DATA_16_BITS		= 0x1,
	HASH_DATA_8_BITS		= 0x2,
	HASH_DATA_1_BIT			= 0x3
};

#define HASH_BUFLEN			256
#define HASH_LONG_KEY			64
#define HASH_MAX_KEY_SIZE		(SHA256_BLOCK_SIZE * 8)
#define HASH_QUEUE_LENGTH		16
#define HASH_DMA_THRESHOLD		50

#define HASH_AUTOSUSPEND_DELAY		50

struct stm32_hash_ctx {
	struct crypto_engine_ctx enginectx;
	struct stm32_hash_dev	*hdev;
	unsigned long		flags;

	u8			key[HASH_MAX_KEY_SIZE];
	int			keylen;
};

struct stm32_hash_request_ctx {
	struct stm32_hash_dev	*hdev;
	unsigned long		flags;
	unsigned long		op;

	u8 digest[SHA256_DIGEST_SIZE] __aligned(sizeof(u32));
	size_t			digcnt;
	size_t			bufcnt;
	size_t			buflen;

	/* DMA */
	struct scatterlist	*sg;
	unsigned int		offset;
	unsigned int		total;
	struct scatterlist	sg_key;

	dma_addr_t		dma_addr;
	size_t			dma_ct;
	int			nents;

	u8			data_type;

	u8 buffer[HASH_BUFLEN] __aligned(sizeof(u32));

	/* Export Context */
	u32			*hw_context;
};

struct stm32_hash_algs_info {
	struct ahash_alg	*algs_list;
	size_t			size;
};

struct stm32_hash_pdata {
	struct stm32_hash_algs_info	*algs_info;
	size_t				algs_info_size;
};

struct stm32_hash_dev {
	struct list_head	list;
	struct device		*dev;
	struct clk		*clk;
	struct reset_control	*rst;
	void __iomem		*io_base;
	phys_addr_t		phys_base;
	u32			dma_mode;
	u32			dma_maxburst;

	struct ahash_request	*req;
	struct crypto_engine	*engine;

	int			err;
	unsigned long		flags;

	struct dma_chan		*dma_lch;
	struct completion	dma_completion;

	const struct stm32_hash_pdata	*pdata;
};

struct stm32_hash_drv {
	struct list_head	dev_list;
	spinlock_t		lock; /* List protection access */
};

static struct stm32_hash_drv stm32_hash = {
	.dev_list = LIST_HEAD_INIT(stm32_hash.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(stm32_hash.lock),
};

static void stm32_hash_dma_callback(void *param);

static inline u32 stm32_hash_read(struct stm32_hash_dev *hdev, u32 offset)
{
	return readl_relaxed(hdev->io_base + offset);
}

static inline void stm32_hash_write(struct stm32_hash_dev *hdev,
				    u32 offset, u32 value)
{
	writel_relaxed(value, hdev->io_base + offset);
}

static inline int stm32_hash_wait_busy(struct stm32_hash_dev *hdev)
{
	u32 status;

	return readl_relaxed_poll_timeout(hdev->io_base + HASH_SR, status,
				   !(status & HASH_SR_BUSY), 10, 10000);
}

static void stm32_hash_set_nblw(struct stm32_hash_dev *hdev, int length)
{
	u32 reg;

	reg = stm32_hash_read(hdev, HASH_STR);
	reg &= ~(HASH_STR_NBLW_MASK);
	reg |= (8U * ((length) % 4U));
	stm32_hash_write(hdev, HASH_STR, reg);
}

static int stm32_hash_write_key(struct stm32_hash_dev *hdev)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(hdev->req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 reg;
	int keylen = ctx->keylen;
	void *key = ctx->key;

	if (keylen) {
		stm32_hash_set_nblw(hdev, keylen);

		while (keylen > 0) {
			stm32_hash_write(hdev, HASH_DIN, *(u32 *)key);
			keylen -= 4;
			key += 4;
		}

		reg = stm32_hash_read(hdev, HASH_STR);
		reg |= HASH_STR_DCAL;
		stm32_hash_write(hdev, HASH_STR, reg);

		return -EINPROGRESS;
	}

	return 0;
}

static void stm32_hash_write_ctrl(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(hdev->req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	u32 reg = HASH_CR_INIT;

	if (!(hdev->flags & HASH_FLAGS_INIT)) {
		switch (rctx->flags & HASH_FLAGS_ALGO_MASK) {
		case HASH_FLAGS_MD5:
			reg |= HASH_CR_ALGO_MD5;
			break;
		case HASH_FLAGS_SHA1:
			reg |= HASH_CR_ALGO_SHA1;
			break;
		case HASH_FLAGS_SHA224:
			reg |= HASH_CR_ALGO_SHA224;
			break;
		case HASH_FLAGS_SHA256:
			reg |= HASH_CR_ALGO_SHA256;
			break;
		default:
			reg |= HASH_CR_ALGO_MD5;
		}

		reg |= (rctx->data_type << HASH_CR_DATATYPE_POS);

		if (rctx->flags & HASH_FLAGS_HMAC) {
			hdev->flags |= HASH_FLAGS_HMAC;
			reg |= HASH_CR_MODE;
			if (ctx->keylen > HASH_LONG_KEY)
				reg |= HASH_CR_LKEY;
		}

		stm32_hash_write(hdev, HASH_IMR, HASH_DCIE);

		stm32_hash_write(hdev, HASH_CR, reg);

		hdev->flags |= HASH_FLAGS_INIT;

		dev_dbg(hdev->dev, "Write Control %x\n", reg);
	}
}

static void stm32_hash_append_sg(struct stm32_hash_request_ctx *rctx)
{
	size_t count;

	while ((rctx->bufcnt < rctx->buflen) && rctx->total) {
		count = min(rctx->sg->length - rctx->offset, rctx->total);
		count = min(count, rctx->buflen - rctx->bufcnt);

		if (count <= 0) {
			if ((rctx->sg->length == 0) && !sg_is_last(rctx->sg)) {
				rctx->sg = sg_next(rctx->sg);
				continue;
			} else {
				break;
			}
		}

		scatterwalk_map_and_copy(rctx->buffer + rctx->bufcnt, rctx->sg,
					 rctx->offset, count, 0);

		rctx->bufcnt += count;
		rctx->offset += count;
		rctx->total -= count;

		if (rctx->offset == rctx->sg->length) {
			rctx->sg = sg_next(rctx->sg);
			if (rctx->sg)
				rctx->offset = 0;
			else
				rctx->total = 0;
		}
	}
}

static int stm32_hash_xmit_cpu(struct stm32_hash_dev *hdev,
			       const u8 *buf, size_t length, int final)
{
	unsigned int count, len32;
	const u32 *buffer = (const u32 *)buf;
	u32 reg;

	if (final)
		hdev->flags |= HASH_FLAGS_FINAL;

	len32 = DIV_ROUND_UP(length, sizeof(u32));

	dev_dbg(hdev->dev, "%s: length: %zd, final: %x len32 %i\n",
		__func__, length, final, len32);

	hdev->flags |= HASH_FLAGS_CPU;

	stm32_hash_write_ctrl(hdev);

	if (stm32_hash_wait_busy(hdev))
		return -ETIMEDOUT;

	if ((hdev->flags & HASH_FLAGS_HMAC) &&
	    (!(hdev->flags & HASH_FLAGS_HMAC_KEY))) {
		hdev->flags |= HASH_FLAGS_HMAC_KEY;
		stm32_hash_write_key(hdev);
		if (stm32_hash_wait_busy(hdev))
			return -ETIMEDOUT;
	}

	for (count = 0; count < len32; count++)
		stm32_hash_write(hdev, HASH_DIN, buffer[count]);

	if (final) {
		stm32_hash_set_nblw(hdev, length);
		reg = stm32_hash_read(hdev, HASH_STR);
		reg |= HASH_STR_DCAL;
		stm32_hash_write(hdev, HASH_STR, reg);
		if (hdev->flags & HASH_FLAGS_HMAC) {
			if (stm32_hash_wait_busy(hdev))
				return -ETIMEDOUT;
			stm32_hash_write_key(hdev);
		}
		return -EINPROGRESS;
	}

	return 0;
}

static int stm32_hash_update_cpu(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	int bufcnt, err = 0, final;

	dev_dbg(hdev->dev, "%s flags %lx\n", __func__, rctx->flags);

	final = (rctx->flags & HASH_FLAGS_FINUP);

	while ((rctx->total >= rctx->buflen) ||
	       (rctx->bufcnt + rctx->total >= rctx->buflen)) {
		stm32_hash_append_sg(rctx);
		bufcnt = rctx->bufcnt;
		rctx->bufcnt = 0;
		err = stm32_hash_xmit_cpu(hdev, rctx->buffer, bufcnt, 0);
	}

	stm32_hash_append_sg(rctx);

	if (final) {
		bufcnt = rctx->bufcnt;
		rctx->bufcnt = 0;
		err = stm32_hash_xmit_cpu(hdev, rctx->buffer, bufcnt,
					  (rctx->flags & HASH_FLAGS_FINUP));
	}

	return err;
}

static int stm32_hash_xmit_dma(struct stm32_hash_dev *hdev,
			       struct scatterlist *sg, int length, int mdma)
{
	struct dma_async_tx_descriptor *in_desc;
	dma_cookie_t cookie;
	u32 reg;
	int err;

	in_desc = dmaengine_prep_slave_sg(hdev->dma_lch, sg, 1,
					  DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT |
					  DMA_CTRL_ACK);
	if (!in_desc) {
		dev_err(hdev->dev, "dmaengine_prep_slave error\n");
		return -ENOMEM;
	}

	reinit_completion(&hdev->dma_completion);
	in_desc->callback = stm32_hash_dma_callback;
	in_desc->callback_param = hdev;

	hdev->flags |= HASH_FLAGS_FINAL;
	hdev->flags |= HASH_FLAGS_DMA_ACTIVE;

	reg = stm32_hash_read(hdev, HASH_CR);

	if (mdma)
		reg |= HASH_CR_MDMAT;
	else
		reg &= ~HASH_CR_MDMAT;

	reg |= HASH_CR_DMAE;

	stm32_hash_write(hdev, HASH_CR, reg);

	stm32_hash_set_nblw(hdev, length);

	cookie = dmaengine_submit(in_desc);
	err = dma_submit_error(cookie);
	if (err)
		return -ENOMEM;

	dma_async_issue_pending(hdev->dma_lch);

	if (!wait_for_completion_timeout(&hdev->dma_completion,
					 msecs_to_jiffies(100)))
		err = -ETIMEDOUT;

	if (dma_async_is_tx_complete(hdev->dma_lch, cookie,
				     NULL, NULL) != DMA_COMPLETE)
		err = -ETIMEDOUT;

	if (err) {
		dev_err(hdev->dev, "DMA Error %i\n", err);
		dmaengine_terminate_all(hdev->dma_lch);
		return err;
	}

	return -EINPROGRESS;
}

static void stm32_hash_dma_callback(void *param)
{
	struct stm32_hash_dev *hdev = param;

	complete(&hdev->dma_completion);

	hdev->flags |= HASH_FLAGS_DMA_READY;
}

static int stm32_hash_hmac_dma_send(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(hdev->req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	int err;

	if (ctx->keylen < HASH_DMA_THRESHOLD || (hdev->dma_mode == 1)) {
		err = stm32_hash_write_key(hdev);
		if (stm32_hash_wait_busy(hdev))
			return -ETIMEDOUT;
	} else {
		if (!(hdev->flags & HASH_FLAGS_HMAC_KEY))
			sg_init_one(&rctx->sg_key, ctx->key,
				    ALIGN(ctx->keylen, sizeof(u32)));

		rctx->dma_ct = dma_map_sg(hdev->dev, &rctx->sg_key, 1,
					  DMA_TO_DEVICE);
		if (rctx->dma_ct == 0) {
			dev_err(hdev->dev, "dma_map_sg error\n");
			return -ENOMEM;
		}

		err = stm32_hash_xmit_dma(hdev, &rctx->sg_key, ctx->keylen, 0);

		dma_unmap_sg(hdev->dev, &rctx->sg_key, 1, DMA_TO_DEVICE);
	}

	return err;
}

static int stm32_hash_dma_init(struct stm32_hash_dev *hdev)
{
	struct dma_slave_config dma_conf;
	struct dma_chan *chan;
	int err;

	memset(&dma_conf, 0, sizeof(dma_conf));

	dma_conf.direction = DMA_MEM_TO_DEV;
	dma_conf.dst_addr = hdev->phys_base + HASH_DIN;
	dma_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_conf.src_maxburst = hdev->dma_maxburst;
	dma_conf.dst_maxburst = hdev->dma_maxburst;
	dma_conf.device_fc = false;

	chan = dma_request_chan(hdev->dev, "in");
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	hdev->dma_lch = chan;

	err = dmaengine_slave_config(hdev->dma_lch, &dma_conf);
	if (err) {
		dma_release_channel(hdev->dma_lch);
		hdev->dma_lch = NULL;
		dev_err(hdev->dev, "Couldn't configure DMA slave.\n");
		return err;
	}

	init_completion(&hdev->dma_completion);

	return 0;
}

static int stm32_hash_dma_send(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct scatterlist sg[1], *tsg;
	int err = 0, len = 0, reg, ncp = 0;
	unsigned int i;
	u32 *buffer = (void *)rctx->buffer;

	rctx->sg = hdev->req->src;
	rctx->total = hdev->req->nbytes;

	rctx->nents = sg_nents(rctx->sg);

	if (rctx->nents < 0)
		return -EINVAL;

	stm32_hash_write_ctrl(hdev);

	if (hdev->flags & HASH_FLAGS_HMAC) {
		err = stm32_hash_hmac_dma_send(hdev);
		if (err != -EINPROGRESS)
			return err;
	}

	for_each_sg(rctx->sg, tsg, rctx->nents, i) {
		len = sg->length;

		sg[0] = *tsg;
		if (sg_is_last(sg)) {
			if (hdev->dma_mode == 1) {
				len = (ALIGN(sg->length, 16) - 16);

				ncp = sg_pcopy_to_buffer(
					rctx->sg, rctx->nents,
					rctx->buffer, sg->length - len,
					rctx->total - sg->length + len);

				sg->length = len;
			} else {
				if (!(IS_ALIGNED(sg->length, sizeof(u32)))) {
					len = sg->length;
					sg->length = ALIGN(sg->length,
							   sizeof(u32));
				}
			}
		}

		rctx->dma_ct = dma_map_sg(hdev->dev, sg, 1,
					  DMA_TO_DEVICE);
		if (rctx->dma_ct == 0) {
			dev_err(hdev->dev, "dma_map_sg error\n");
			return -ENOMEM;
		}

		err = stm32_hash_xmit_dma(hdev, sg, len,
					  !sg_is_last(sg));

		dma_unmap_sg(hdev->dev, sg, 1, DMA_TO_DEVICE);

		if (err == -ENOMEM)
			return err;
	}

	if (hdev->dma_mode == 1) {
		if (stm32_hash_wait_busy(hdev))
			return -ETIMEDOUT;
		reg = stm32_hash_read(hdev, HASH_CR);
		reg &= ~HASH_CR_DMAE;
		reg |= HASH_CR_DMAA;
		stm32_hash_write(hdev, HASH_CR, reg);

		if (ncp) {
			memset(buffer + ncp, 0,
			       DIV_ROUND_UP(ncp, sizeof(u32)) - ncp);
			writesl(hdev->io_base + HASH_DIN, buffer,
				DIV_ROUND_UP(ncp, sizeof(u32)));
		}
		stm32_hash_set_nblw(hdev, ncp);
		reg = stm32_hash_read(hdev, HASH_STR);
		reg |= HASH_STR_DCAL;
		stm32_hash_write(hdev, HASH_STR, reg);
		err = -EINPROGRESS;
	}

	if (hdev->flags & HASH_FLAGS_HMAC) {
		if (stm32_hash_wait_busy(hdev))
			return -ETIMEDOUT;
		err = stm32_hash_hmac_dma_send(hdev);
	}

	return err;
}

static struct stm32_hash_dev *stm32_hash_find_dev(struct stm32_hash_ctx *ctx)
{
	struct stm32_hash_dev *hdev = NULL, *tmp;

	spin_lock_bh(&stm32_hash.lock);
	if (!ctx->hdev) {
		list_for_each_entry(tmp, &stm32_hash.dev_list, list) {
			hdev = tmp;
			break;
		}
		ctx->hdev = hdev;
	} else {
		hdev = ctx->hdev;
	}

	spin_unlock_bh(&stm32_hash.lock);

	return hdev;
}

static bool stm32_hash_dma_aligned_data(struct ahash_request *req)
{
	struct scatterlist *sg;
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	int i;

	if (req->nbytes <= HASH_DMA_THRESHOLD)
		return false;

	if (sg_nents(req->src) > 1) {
		if (hdev->dma_mode == 1)
			return false;
		for_each_sg(req->src, sg, sg_nents(req->src), i) {
			if ((!IS_ALIGNED(sg->length, sizeof(u32))) &&
			    (!sg_is_last(sg)))
				return false;
		}
	}

	if (req->src->offset % 4)
		return false;

	return true;
}

static int stm32_hash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);

	rctx->hdev = hdev;

	rctx->flags = HASH_FLAGS_CPU;

	rctx->digcnt = crypto_ahash_digestsize(tfm);
	switch (rctx->digcnt) {
	case MD5_DIGEST_SIZE:
		rctx->flags |= HASH_FLAGS_MD5;
		break;
	case SHA1_DIGEST_SIZE:
		rctx->flags |= HASH_FLAGS_SHA1;
		break;
	case SHA224_DIGEST_SIZE:
		rctx->flags |= HASH_FLAGS_SHA224;
		break;
	case SHA256_DIGEST_SIZE:
		rctx->flags |= HASH_FLAGS_SHA256;
		break;
	default:
		return -EINVAL;
	}

	rctx->bufcnt = 0;
	rctx->buflen = HASH_BUFLEN;
	rctx->total = 0;
	rctx->offset = 0;
	rctx->data_type = HASH_DATA_8_BITS;

	memset(rctx->buffer, 0, HASH_BUFLEN);

	if (ctx->flags & HASH_FLAGS_HMAC)
		rctx->flags |= HASH_FLAGS_HMAC;

	dev_dbg(hdev->dev, "%s Flags %lx\n", __func__, rctx->flags);

	return 0;
}

static int stm32_hash_update_req(struct stm32_hash_dev *hdev)
{
	return stm32_hash_update_cpu(hdev);
}

static int stm32_hash_final_req(struct stm32_hash_dev *hdev)
{
	struct ahash_request *req = hdev->req;
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	int err;
	int buflen = rctx->bufcnt;

	rctx->bufcnt = 0;

	if (!(rctx->flags & HASH_FLAGS_CPU))
		err = stm32_hash_dma_send(hdev);
	else
		err = stm32_hash_xmit_cpu(hdev, rctx->buffer, buflen, 1);


	return err;
}

static void stm32_hash_copy_hash(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	__be32 *hash = (void *)rctx->digest;
	unsigned int i, hashsize;

	switch (rctx->flags & HASH_FLAGS_ALGO_MASK) {
	case HASH_FLAGS_MD5:
		hashsize = MD5_DIGEST_SIZE;
		break;
	case HASH_FLAGS_SHA1:
		hashsize = SHA1_DIGEST_SIZE;
		break;
	case HASH_FLAGS_SHA224:
		hashsize = SHA224_DIGEST_SIZE;
		break;
	case HASH_FLAGS_SHA256:
		hashsize = SHA256_DIGEST_SIZE;
		break;
	default:
		return;
	}

	for (i = 0; i < hashsize / sizeof(u32); i++)
		hash[i] = cpu_to_be32(stm32_hash_read(rctx->hdev,
						      HASH_HREG(i)));
}

static int stm32_hash_finish(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (!req->result)
		return -EINVAL;

	memcpy(req->result, rctx->digest, rctx->digcnt);

	return 0;
}

static void stm32_hash_finish_req(struct ahash_request *req, int err)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_dev *hdev = rctx->hdev;

	if (!err && (HASH_FLAGS_FINAL & hdev->flags)) {
		stm32_hash_copy_hash(req);
		err = stm32_hash_finish(req);
		hdev->flags &= ~(HASH_FLAGS_FINAL | HASH_FLAGS_CPU |
				 HASH_FLAGS_INIT | HASH_FLAGS_DMA_READY |
				 HASH_FLAGS_OUTPUT_READY | HASH_FLAGS_HMAC |
				 HASH_FLAGS_HMAC_INIT | HASH_FLAGS_HMAC_FINAL |
				 HASH_FLAGS_HMAC_KEY);
	} else {
		rctx->flags |= HASH_FLAGS_ERRORS;
	}

	pm_runtime_mark_last_busy(hdev->dev);
	pm_runtime_put_autosuspend(hdev->dev);

	crypto_finalize_hash_request(hdev->engine, req, err);
}

static int stm32_hash_hw_init(struct stm32_hash_dev *hdev,
			      struct stm32_hash_request_ctx *rctx)
{
	pm_runtime_get_sync(hdev->dev);

	if (!(HASH_FLAGS_INIT & hdev->flags)) {
		stm32_hash_write(hdev, HASH_CR, HASH_CR_INIT);
		stm32_hash_write(hdev, HASH_STR, 0);
		stm32_hash_write(hdev, HASH_DIN, 0);
		stm32_hash_write(hdev, HASH_IMR, 0);
		hdev->err = 0;
	}

	return 0;
}

static int stm32_hash_one_request(struct crypto_engine *engine, void *areq);
static int stm32_hash_prepare_req(struct crypto_engine *engine, void *areq);

static int stm32_hash_handle_queue(struct stm32_hash_dev *hdev,
				   struct ahash_request *req)
{
	return crypto_transfer_hash_request_to_engine(hdev->engine, req);
}

static int stm32_hash_prepare_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	struct stm32_hash_request_ctx *rctx;

	if (!hdev)
		return -ENODEV;

	hdev->req = req;

	rctx = ahash_request_ctx(req);

	dev_dbg(hdev->dev, "processing new req, op: %lu, nbytes %d\n",
		rctx->op, req->nbytes);

	return stm32_hash_hw_init(hdev, rctx);
}

static int stm32_hash_one_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	struct stm32_hash_request_ctx *rctx;
	int err = 0;

	if (!hdev)
		return -ENODEV;

	hdev->req = req;

	rctx = ahash_request_ctx(req);

	if (rctx->op == HASH_OP_UPDATE)
		err = stm32_hash_update_req(hdev);
	else if (rctx->op == HASH_OP_FINAL)
		err = stm32_hash_final_req(hdev);

	if (err != -EINPROGRESS)
	/* done task will not finish it, so do it here */
		stm32_hash_finish_req(req, err);

	return 0;
}

static int stm32_hash_enqueue(struct ahash_request *req, unsigned int op)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct stm32_hash_dev *hdev = ctx->hdev;

	rctx->op = op;

	return stm32_hash_handle_queue(hdev, req);
}

static int stm32_hash_update(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);

	if (!req->nbytes || !(rctx->flags & HASH_FLAGS_CPU))
		return 0;

	rctx->total = req->nbytes;
	rctx->sg = req->src;
	rctx->offset = 0;

	if ((rctx->bufcnt + rctx->total < rctx->buflen)) {
		stm32_hash_append_sg(rctx);
		return 0;
	}

	return stm32_hash_enqueue(req, HASH_OP_UPDATE);
}

static int stm32_hash_final(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);

	rctx->flags |= HASH_FLAGS_FINUP;

	return stm32_hash_enqueue(req, HASH_OP_FINAL);
}

static int stm32_hash_finup(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	int err1, err2;

	rctx->flags |= HASH_FLAGS_FINUP;

	if (hdev->dma_lch && stm32_hash_dma_aligned_data(req))
		rctx->flags &= ~HASH_FLAGS_CPU;

	err1 = stm32_hash_update(req);

	if (err1 == -EINPROGRESS || err1 == -EBUSY)
		return err1;

	/*
	 * final() has to be always called to cleanup resources
	 * even if update() failed, except EINPROGRESS
	 */
	err2 = stm32_hash_final(req);

	return err1 ?: err2;
}

static int stm32_hash_digest(struct ahash_request *req)
{
	return stm32_hash_init(req) ?: stm32_hash_finup(req);
}

static int stm32_hash_export(struct ahash_request *req, void *out)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	u32 *preg;
	unsigned int i;

	pm_runtime_get_sync(hdev->dev);

	while ((stm32_hash_read(hdev, HASH_SR) & HASH_SR_BUSY))
		cpu_relax();

	rctx->hw_context = kmalloc_array(3 + HASH_CSR_REGISTER_NUMBER,
					 sizeof(u32),
					 GFP_KERNEL);

	preg = rctx->hw_context;

	*preg++ = stm32_hash_read(hdev, HASH_IMR);
	*preg++ = stm32_hash_read(hdev, HASH_STR);
	*preg++ = stm32_hash_read(hdev, HASH_CR);
	for (i = 0; i < HASH_CSR_REGISTER_NUMBER; i++)
		*preg++ = stm32_hash_read(hdev, HASH_CSR(i));

	pm_runtime_mark_last_busy(hdev->dev);
	pm_runtime_put_autosuspend(hdev->dev);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int stm32_hash_import(struct ahash_request *req, const void *in)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	const u32 *preg = in;
	u32 reg;
	unsigned int i;

	memcpy(rctx, in, sizeof(*rctx));

	preg = rctx->hw_context;

	pm_runtime_get_sync(hdev->dev);

	stm32_hash_write(hdev, HASH_IMR, *preg++);
	stm32_hash_write(hdev, HASH_STR, *preg++);
	stm32_hash_write(hdev, HASH_CR, *preg);
	reg = *preg++ | HASH_CR_INIT;
	stm32_hash_write(hdev, HASH_CR, reg);

	for (i = 0; i < HASH_CSR_REGISTER_NUMBER; i++)
		stm32_hash_write(hdev, HASH_CSR(i), *preg++);

	pm_runtime_mark_last_busy(hdev->dev);
	pm_runtime_put_autosuspend(hdev->dev);

	kfree(rctx->hw_context);

	return 0;
}

static int stm32_hash_setkey(struct crypto_ahash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	if (keylen <= HASH_MAX_KEY_SIZE) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		return -ENOMEM;
	}

	return 0;
}

static int stm32_hash_cra_init_algs(struct crypto_tfm *tfm,
				    const char *algs_hmac_name)
{
	struct stm32_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct stm32_hash_request_ctx));

	ctx->keylen = 0;

	if (algs_hmac_name)
		ctx->flags |= HASH_FLAGS_HMAC;

	ctx->enginectx.op.do_one_request = stm32_hash_one_request;
	ctx->enginectx.op.prepare_request = stm32_hash_prepare_req;
	ctx->enginectx.op.unprepare_request = NULL;
	return 0;
}

static int stm32_hash_cra_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, NULL);
}

static int stm32_hash_cra_md5_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, "md5");
}

static int stm32_hash_cra_sha1_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, "sha1");
}

static int stm32_hash_cra_sha224_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, "sha224");
}

static int stm32_hash_cra_sha256_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, "sha256");
}

static irqreturn_t stm32_hash_irq_thread(int irq, void *dev_id)
{
	struct stm32_hash_dev *hdev = dev_id;

	if (HASH_FLAGS_CPU & hdev->flags) {
		if (HASH_FLAGS_OUTPUT_READY & hdev->flags) {
			hdev->flags &= ~HASH_FLAGS_OUTPUT_READY;
			goto finish;
		}
	} else if (HASH_FLAGS_DMA_READY & hdev->flags) {
		if (HASH_FLAGS_DMA_ACTIVE & hdev->flags) {
			hdev->flags &= ~HASH_FLAGS_DMA_ACTIVE;
				goto finish;
		}
	}

	return IRQ_HANDLED;

finish:
	/* Finish current request */
	stm32_hash_finish_req(hdev->req, 0);

	return IRQ_HANDLED;
}

static irqreturn_t stm32_hash_irq_handler(int irq, void *dev_id)
{
	struct stm32_hash_dev *hdev = dev_id;
	u32 reg;

	reg = stm32_hash_read(hdev, HASH_SR);
	if (reg & HASH_SR_OUTPUT_READY) {
		reg &= ~HASH_SR_OUTPUT_READY;
		stm32_hash_write(hdev, HASH_SR, reg);
		hdev->flags |= HASH_FLAGS_OUTPUT_READY;
		/* Disable IT*/
		stm32_hash_write(hdev, HASH_IMR, 0);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static struct ahash_alg algs_md5_sha1[] = {
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "stm32-md5",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.setkey = stm32_hash_setkey,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "hmac(md5)",
				.cra_driver_name = "stm32-hmac-md5",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_md5_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "stm32-sha1",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.setkey = stm32_hash_setkey,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "hmac(sha1)",
				.cra_driver_name = "stm32-hmac-sha1",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_sha1_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
};

static struct ahash_alg algs_sha224_sha256[] = {
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "sha224",
				.cra_driver_name = "stm32-sha224",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.setkey = stm32_hash_setkey,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "hmac(sha224)",
				.cra_driver_name = "stm32-hmac-sha224",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_sha224_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "sha256",
				.cra_driver_name = "stm32-sha256",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
	{
		.init = stm32_hash_init,
		.update = stm32_hash_update,
		.final = stm32_hash_final,
		.finup = stm32_hash_finup,
		.digest = stm32_hash_digest,
		.export = stm32_hash_export,
		.import = stm32_hash_import,
		.setkey = stm32_hash_setkey,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_request_ctx),
			.base = {
				.cra_name = "hmac(sha256)",
				.cra_driver_name = "stm32-hmac-sha256",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_alignmask = 3,
				.cra_init = stm32_hash_cra_sha256_init,
				.cra_module = THIS_MODULE,
			}
		}
	},
};

static int stm32_hash_register_algs(struct stm32_hash_dev *hdev)
{
	unsigned int i, j;
	int err;

	for (i = 0; i < hdev->pdata->algs_info_size; i++) {
		for (j = 0; j < hdev->pdata->algs_info[i].size; j++) {
			err = crypto_register_ahash(
				&hdev->pdata->algs_info[i].algs_list[j]);
			if (err)
				goto err_algs;
		}
	}

	return 0;
err_algs:
	dev_err(hdev->dev, "Algo %d : %d failed\n", i, j);
	for (; i--; ) {
		for (; j--;)
			crypto_unregister_ahash(
				&hdev->pdata->algs_info[i].algs_list[j]);
	}

	return err;
}

static int stm32_hash_unregister_algs(struct stm32_hash_dev *hdev)
{
	unsigned int i, j;

	for (i = 0; i < hdev->pdata->algs_info_size; i++) {
		for (j = 0; j < hdev->pdata->algs_info[i].size; j++)
			crypto_unregister_ahash(
				&hdev->pdata->algs_info[i].algs_list[j]);
	}

	return 0;
}

static struct stm32_hash_algs_info stm32_hash_algs_info_stm32f4[] = {
	{
		.algs_list	= algs_md5_sha1,
		.size		= ARRAY_SIZE(algs_md5_sha1),
	},
};

static const struct stm32_hash_pdata stm32_hash_pdata_stm32f4 = {
	.algs_info	= stm32_hash_algs_info_stm32f4,
	.algs_info_size	= ARRAY_SIZE(stm32_hash_algs_info_stm32f4),
};

static struct stm32_hash_algs_info stm32_hash_algs_info_stm32f7[] = {
	{
		.algs_list	= algs_md5_sha1,
		.size		= ARRAY_SIZE(algs_md5_sha1),
	},
	{
		.algs_list	= algs_sha224_sha256,
		.size		= ARRAY_SIZE(algs_sha224_sha256),
	},
};

static const struct stm32_hash_pdata stm32_hash_pdata_stm32f7 = {
	.algs_info	= stm32_hash_algs_info_stm32f7,
	.algs_info_size	= ARRAY_SIZE(stm32_hash_algs_info_stm32f7),
};

static const struct of_device_id stm32_hash_of_match[] = {
	{
		.compatible = "st,stm32f456-hash",
		.data = &stm32_hash_pdata_stm32f4,
	},
	{
		.compatible = "st,stm32f756-hash",
		.data = &stm32_hash_pdata_stm32f7,
	},
	{},
};

MODULE_DEVICE_TABLE(of, stm32_hash_of_match);

static int stm32_hash_get_of_match(struct stm32_hash_dev *hdev,
				   struct device *dev)
{
	hdev->pdata = of_device_get_match_data(dev);
	if (!hdev->pdata) {
		dev_err(dev, "no compatible OF match\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dev->of_node, "dma-maxburst",
				 &hdev->dma_maxburst)) {
		dev_info(dev, "dma-maxburst not specified, using 0\n");
		hdev->dma_maxburst = 0;
	}

	return 0;
}

static int stm32_hash_probe(struct platform_device *pdev)
{
	struct stm32_hash_dev *hdev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret, irq;

	hdev = devm_kzalloc(dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdev->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdev->io_base))
		return PTR_ERR(hdev->io_base);

	hdev->phys_base = res->start;

	ret = stm32_hash_get_of_match(hdev, dev);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, stm32_hash_irq_handler,
					stm32_hash_irq_thread, IRQF_ONESHOT,
					dev_name(dev), hdev);
	if (ret) {
		dev_err(dev, "Cannot grab IRQ\n");
		return ret;
	}

	hdev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(hdev->clk))
		return dev_err_probe(dev, PTR_ERR(hdev->clk),
				     "failed to get clock for hash\n");

	ret = clk_prepare_enable(hdev->clk);
	if (ret) {
		dev_err(dev, "failed to enable hash clock (%d)\n", ret);
		return ret;
	}

	pm_runtime_set_autosuspend_delay(dev, HASH_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	hdev->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(hdev->rst)) {
		if (PTR_ERR(hdev->rst) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_reset;
		}
	} else {
		reset_control_assert(hdev->rst);
		udelay(2);
		reset_control_deassert(hdev->rst);
	}

	hdev->dev = dev;

	platform_set_drvdata(pdev, hdev);

	ret = stm32_hash_dma_init(hdev);
	switch (ret) {
	case 0:
		break;
	case -ENOENT:
		dev_dbg(dev, "DMA mode not available\n");
		break;
	default:
		goto err_dma;
	}

	spin_lock(&stm32_hash.lock);
	list_add_tail(&hdev->list, &stm32_hash.dev_list);
	spin_unlock(&stm32_hash.lock);

	/* Initialize crypto engine */
	hdev->engine = crypto_engine_alloc_init(dev, 1);
	if (!hdev->engine) {
		ret = -ENOMEM;
		goto err_engine;
	}

	ret = crypto_engine_start(hdev->engine);
	if (ret)
		goto err_engine_start;

	hdev->dma_mode = stm32_hash_read(hdev, HASH_HWCFGR);

	/* Register algos */
	ret = stm32_hash_register_algs(hdev);
	if (ret)
		goto err_algs;

	dev_info(dev, "Init HASH done HW ver %x DMA mode %u\n",
		 stm32_hash_read(hdev, HASH_VER), hdev->dma_mode);

	pm_runtime_put_sync(dev);

	return 0;

err_algs:
err_engine_start:
	crypto_engine_exit(hdev->engine);
err_engine:
	spin_lock(&stm32_hash.lock);
	list_del(&hdev->list);
	spin_unlock(&stm32_hash.lock);
err_dma:
	if (hdev->dma_lch)
		dma_release_channel(hdev->dma_lch);
err_reset:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);

	clk_disable_unprepare(hdev->clk);

	return ret;
}

static int stm32_hash_remove(struct platform_device *pdev)
{
	struct stm32_hash_dev *hdev;
	int ret;

	hdev = platform_get_drvdata(pdev);
	if (!hdev)
		return -ENODEV;

	ret = pm_runtime_get_sync(hdev->dev);
	if (ret < 0)
		return ret;

	stm32_hash_unregister_algs(hdev);

	crypto_engine_exit(hdev->engine);

	spin_lock(&stm32_hash.lock);
	list_del(&hdev->list);
	spin_unlock(&stm32_hash.lock);

	if (hdev->dma_lch)
		dma_release_channel(hdev->dma_lch);

	pm_runtime_disable(hdev->dev);
	pm_runtime_put_noidle(hdev->dev);

	clk_disable_unprepare(hdev->clk);

	return 0;
}

#ifdef CONFIG_PM
static int stm32_hash_runtime_suspend(struct device *dev)
{
	struct stm32_hash_dev *hdev = dev_get_drvdata(dev);

	clk_disable_unprepare(hdev->clk);

	return 0;
}

static int stm32_hash_runtime_resume(struct device *dev)
{
	struct stm32_hash_dev *hdev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(hdev->clk);
	if (ret) {
		dev_err(hdev->dev, "Failed to prepare_enable clock\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops stm32_hash_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(stm32_hash_runtime_suspend,
			   stm32_hash_runtime_resume, NULL)
};

static struct platform_driver stm32_hash_driver = {
	.probe		= stm32_hash_probe,
	.remove		= stm32_hash_remove,
	.driver		= {
		.name	= "stm32-hash",
		.pm = &stm32_hash_pm_ops,
		.of_match_table	= stm32_hash_of_match,
	}
};

module_platform_driver(stm32_hash_driver);

MODULE_DESCRIPTION("STM32 SHA1/224/256 & MD5 (HMAC) hw accelerator driver");
MODULE_AUTHOR("Lionel Debieve <lionel.debieve@st.com>");
MODULE_LICENSE("GPL v2");
