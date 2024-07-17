// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of STM32 Crypto driver for Linux.
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Lionel DEBIEVE <lionel.debieve@st.com> for STMicroelectronics.
 */

#include <crypto/engine.h>
#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/string.h>

#define HASH_CR				0x00
#define HASH_DIN			0x04
#define HASH_STR			0x08
#define HASH_UX500_HREG(x)		(0x0c + ((x) * 0x04))
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
#define HASH_CR_ALGO_POS		7
#define HASH_CR_MDMAT			BIT(13)
#define HASH_CR_DMAA			BIT(14)
#define HASH_CR_LKEY			BIT(16)

/* Interrupt */
#define HASH_DINIE			BIT(0)
#define HASH_DCIE			BIT(1)

/* Interrupt Mask */
#define HASH_MASK_CALC_COMPLETION	BIT(0)
#define HASH_MASK_DATA_INPUT		BIT(1)

/* Status Flags */
#define HASH_SR_DATA_INPUT_READY	BIT(0)
#define HASH_SR_OUTPUT_READY		BIT(1)
#define HASH_SR_DMA_ACTIVE		BIT(2)
#define HASH_SR_BUSY			BIT(3)

/* STR Register */
#define HASH_STR_NBLW_MASK		GENMASK(4, 0)
#define HASH_STR_DCAL			BIT(8)

/* HWCFGR Register */
#define HASH_HWCFG_DMA_MASK		GENMASK(3, 0)

/* Context swap register */
#define HASH_CSR_NB_SHA256_HMAC		54
#define HASH_CSR_NB_SHA256		38
#define HASH_CSR_NB_SHA512_HMAC		103
#define HASH_CSR_NB_SHA512		91
#define HASH_CSR_NB_SHA3_HMAC		88
#define HASH_CSR_NB_SHA3		72
#define HASH_CSR_NB_MAX			HASH_CSR_NB_SHA512_HMAC

#define HASH_FLAGS_INIT			BIT(0)
#define HASH_FLAGS_OUTPUT_READY		BIT(1)
#define HASH_FLAGS_CPU			BIT(2)
#define HASH_FLAGS_DMA_ACTIVE		BIT(3)
#define HASH_FLAGS_HMAC_INIT		BIT(4)
#define HASH_FLAGS_HMAC_FINAL		BIT(5)
#define HASH_FLAGS_HMAC_KEY		BIT(6)
#define HASH_FLAGS_SHA3_MODE		BIT(7)
#define HASH_FLAGS_FINAL		BIT(15)
#define HASH_FLAGS_FINUP		BIT(16)
#define HASH_FLAGS_ALGO_MASK		GENMASK(20, 17)
#define HASH_FLAGS_ALGO_SHIFT		17
#define HASH_FLAGS_ERRORS		BIT(21)
#define HASH_FLAGS_EMPTY		BIT(22)
#define HASH_FLAGS_HMAC			BIT(23)
#define HASH_FLAGS_SGS_COPIED		BIT(24)

#define HASH_OP_UPDATE			1
#define HASH_OP_FINAL			2

#define HASH_BURST_LEVEL		4

enum stm32_hash_data_format {
	HASH_DATA_32_BITS		= 0x0,
	HASH_DATA_16_BITS		= 0x1,
	HASH_DATA_8_BITS		= 0x2,
	HASH_DATA_1_BIT			= 0x3
};

#define HASH_BUFLEN			(SHA3_224_BLOCK_SIZE + 4)
#define HASH_MAX_KEY_SIZE		(SHA512_BLOCK_SIZE * 8)

enum stm32_hash_algo {
	HASH_SHA1			= 0,
	HASH_MD5			= 1,
	HASH_SHA224			= 2,
	HASH_SHA256			= 3,
	HASH_SHA3_224			= 4,
	HASH_SHA3_256			= 5,
	HASH_SHA3_384			= 6,
	HASH_SHA3_512			= 7,
	HASH_SHA384			= 12,
	HASH_SHA512			= 15,
};

enum ux500_hash_algo {
	HASH_SHA256_UX500		= 0,
	HASH_SHA1_UX500			= 1,
};

#define HASH_AUTOSUSPEND_DELAY		50

struct stm32_hash_ctx {
	struct stm32_hash_dev	*hdev;
	struct crypto_shash	*xtfm;
	unsigned long		flags;

	u8			key[HASH_MAX_KEY_SIZE];
	int			keylen;
};

struct stm32_hash_state {
	u32			flags;

	u16			bufcnt;
	u16			blocklen;

	u8 buffer[HASH_BUFLEN] __aligned(sizeof(u32));

	/* hash state */
	u32			hw_context[3 + HASH_CSR_NB_MAX];
};

struct stm32_hash_request_ctx {
	struct stm32_hash_dev	*hdev;
	unsigned long		op;

	u8 digest[SHA512_DIGEST_SIZE] __aligned(sizeof(u32));
	size_t			digcnt;

	struct scatterlist	*sg;
	struct scatterlist	sgl[2]; /* scatterlist used to realize alignment */
	unsigned int		offset;
	unsigned int		total;
	struct scatterlist	sg_key;

	dma_addr_t		dma_addr;
	size_t			dma_ct;
	int			nents;

	u8			data_type;

	struct stm32_hash_state state;
};

struct stm32_hash_algs_info {
	struct ahash_engine_alg	*algs_list;
	size_t			size;
};

struct stm32_hash_pdata {
	const int				alg_shift;
	const struct stm32_hash_algs_info	*algs_info;
	size_t					algs_info_size;
	bool					has_sr;
	bool					has_mdmat;
	bool					context_secured;
	bool					broken_emptymsg;
	bool					ux500;
};

struct stm32_hash_dev {
	struct list_head	list;
	struct device		*dev;
	struct clk		*clk;
	struct reset_control	*rst;
	void __iomem		*io_base;
	phys_addr_t		phys_base;
	u8			xmit_buf[HASH_BUFLEN] __aligned(sizeof(u32));
	u32			dma_mode;
	bool			polled;

	struct ahash_request	*req;
	struct crypto_engine	*engine;

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
static int stm32_hash_prepare_request(struct ahash_request *req);
static void stm32_hash_unprepare_request(struct ahash_request *req);

static inline u32 stm32_hash_read(struct stm32_hash_dev *hdev, u32 offset)
{
	return readl_relaxed(hdev->io_base + offset);
}

static inline void stm32_hash_write(struct stm32_hash_dev *hdev,
				    u32 offset, u32 value)
{
	writel_relaxed(value, hdev->io_base + offset);
}

/**
 * stm32_hash_wait_busy - wait until hash processor is available. It return an
 * error if the hash core is processing a block of data for more than 10 ms.
 * @hdev: the stm32_hash_dev device.
 */
static inline int stm32_hash_wait_busy(struct stm32_hash_dev *hdev)
{
	u32 status;

	/* The Ux500 lacks the special status register, we poll the DCAL bit instead */
	if (!hdev->pdata->has_sr)
		return readl_relaxed_poll_timeout(hdev->io_base + HASH_STR, status,
						  !(status & HASH_STR_DCAL), 10, 10000);

	return readl_relaxed_poll_timeout(hdev->io_base + HASH_SR, status,
				   !(status & HASH_SR_BUSY), 10, 10000);
}

/**
 * stm32_hash_set_nblw - set the number of valid bytes in the last word.
 * @hdev: the stm32_hash_dev device.
 * @length: the length of the final word.
 */
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

/**
 * stm32_hash_write_ctrl - Initialize the hash processor, only if
 * HASH_FLAGS_INIT is set.
 * @hdev: the stm32_hash_dev device
 */
static void stm32_hash_write_ctrl(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(hdev->req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct stm32_hash_state *state = &rctx->state;
	u32 alg = (state->flags & HASH_FLAGS_ALGO_MASK) >> HASH_FLAGS_ALGO_SHIFT;

	u32 reg = HASH_CR_INIT;

	if (!(hdev->flags & HASH_FLAGS_INIT)) {
		if (hdev->pdata->ux500) {
			reg |= ((alg & BIT(0)) << HASH_CR_ALGO_POS);
		} else {
			if (hdev->pdata->alg_shift == HASH_CR_ALGO_POS)
				reg |= ((alg & BIT(1)) << 17) |
				       ((alg & BIT(0)) << HASH_CR_ALGO_POS);
			else
				reg |= alg << hdev->pdata->alg_shift;
		}

		reg |= (rctx->data_type << HASH_CR_DATATYPE_POS);

		if (state->flags & HASH_FLAGS_HMAC) {
			hdev->flags |= HASH_FLAGS_HMAC;
			reg |= HASH_CR_MODE;
			if (ctx->keylen > crypto_ahash_blocksize(tfm))
				reg |= HASH_CR_LKEY;
		}

		if (!hdev->polled)
			stm32_hash_write(hdev, HASH_IMR, HASH_DCIE);

		stm32_hash_write(hdev, HASH_CR, reg);

		hdev->flags |= HASH_FLAGS_INIT;

		/*
		 * After first block + 1 words are fill up,
		 * we only need to fill 1 block to start partial computation
		 */
		rctx->state.blocklen -= sizeof(u32);

		dev_dbg(hdev->dev, "Write Control %x\n", reg);
	}
}

static void stm32_hash_append_sg(struct stm32_hash_request_ctx *rctx)
{
	struct stm32_hash_state *state = &rctx->state;
	size_t count;

	while ((state->bufcnt < state->blocklen) && rctx->total) {
		count = min(rctx->sg->length - rctx->offset, rctx->total);
		count = min_t(size_t, count, state->blocklen - state->bufcnt);

		if (count <= 0) {
			if ((rctx->sg->length == 0) && !sg_is_last(rctx->sg)) {
				rctx->sg = sg_next(rctx->sg);
				continue;
			} else {
				break;
			}
		}

		scatterwalk_map_and_copy(state->buffer + state->bufcnt,
					 rctx->sg, rctx->offset, count, 0);

		state->bufcnt += count;
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
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct stm32_hash_state *state = &rctx->state;
	unsigned int count, len32;
	const u32 *buffer = (const u32 *)buf;
	u32 reg;

	if (final) {
		hdev->flags |= HASH_FLAGS_FINAL;

		/* Do not process empty messages if hw is buggy. */
		if (!(hdev->flags & HASH_FLAGS_INIT) && !length &&
		    hdev->pdata->broken_emptymsg) {
			state->flags |= HASH_FLAGS_EMPTY;
			return 0;
		}
	}

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
		if (stm32_hash_wait_busy(hdev))
			return -ETIMEDOUT;

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

static int hash_swap_reg(struct stm32_hash_request_ctx *rctx)
{
	struct stm32_hash_state *state = &rctx->state;

	switch ((state->flags & HASH_FLAGS_ALGO_MASK) >>
		HASH_FLAGS_ALGO_SHIFT) {
	case HASH_MD5:
	case HASH_SHA1:
	case HASH_SHA224:
	case HASH_SHA256:
		if (state->flags & HASH_FLAGS_HMAC)
			return HASH_CSR_NB_SHA256_HMAC;
		else
			return HASH_CSR_NB_SHA256;
		break;

	case HASH_SHA384:
	case HASH_SHA512:
		if (state->flags & HASH_FLAGS_HMAC)
			return HASH_CSR_NB_SHA512_HMAC;
		else
			return HASH_CSR_NB_SHA512;
		break;

	case HASH_SHA3_224:
	case HASH_SHA3_256:
	case HASH_SHA3_384:
	case HASH_SHA3_512:
		if (state->flags & HASH_FLAGS_HMAC)
			return HASH_CSR_NB_SHA3_HMAC;
		else
			return HASH_CSR_NB_SHA3;
		break;

	default:
		return -EINVAL;
	}
}

static int stm32_hash_update_cpu(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct stm32_hash_state *state = &rctx->state;
	int bufcnt, err = 0, final;

	dev_dbg(hdev->dev, "%s flags %x\n", __func__, state->flags);

	final = state->flags & HASH_FLAGS_FINAL;

	while ((rctx->total >= state->blocklen) ||
	       (state->bufcnt + rctx->total >= state->blocklen)) {
		stm32_hash_append_sg(rctx);
		bufcnt = state->bufcnt;
		state->bufcnt = 0;
		err = stm32_hash_xmit_cpu(hdev, state->buffer, bufcnt, 0);
		if (err)
			return err;
	}

	stm32_hash_append_sg(rctx);

	if (final) {
		bufcnt = state->bufcnt;
		state->bufcnt = 0;
		return stm32_hash_xmit_cpu(hdev, state->buffer, bufcnt, 1);
	}

	return err;
}

static int stm32_hash_xmit_dma(struct stm32_hash_dev *hdev,
			       struct scatterlist *sg, int length, int mdmat)
{
	struct dma_async_tx_descriptor *in_desc;
	dma_cookie_t cookie;
	u32 reg;
	int err;

	dev_dbg(hdev->dev, "%s mdmat: %x length: %d\n", __func__, mdmat, length);

	/* do not use dma if there is no data to send */
	if (length <= 0)
		return 0;

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

	hdev->flags |= HASH_FLAGS_DMA_ACTIVE;

	reg = stm32_hash_read(hdev, HASH_CR);

	if (hdev->pdata->has_mdmat) {
		if (mdmat)
			reg |= HASH_CR_MDMAT;
		else
			reg &= ~HASH_CR_MDMAT;
	}
	reg |= HASH_CR_DMAE;

	stm32_hash_write(hdev, HASH_CR, reg);


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
}

static int stm32_hash_hmac_dma_send(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(hdev->req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	int err;

	if (ctx->keylen < rctx->state.blocklen || hdev->dma_mode > 0) {
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
	dma_conf.src_maxburst = HASH_BURST_LEVEL;
	dma_conf.dst_maxburst = HASH_BURST_LEVEL;
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
	u32 *buffer = (void *)rctx->state.buffer;
	struct scatterlist sg[1], *tsg;
	int err = 0, reg, ncp = 0;
	unsigned int i, len = 0, bufcnt = 0;
	bool final = hdev->flags & HASH_FLAGS_FINAL;
	bool is_last = false;
	u32 last_word;

	dev_dbg(hdev->dev, "%s total: %d bufcnt: %d final: %d\n",
		__func__, rctx->total, rctx->state.bufcnt, final);

	if (rctx->nents < 0)
		return -EINVAL;

	stm32_hash_write_ctrl(hdev);

	if (hdev->flags & HASH_FLAGS_HMAC && (!(hdev->flags & HASH_FLAGS_HMAC_KEY))) {
		hdev->flags |= HASH_FLAGS_HMAC_KEY;
		err = stm32_hash_hmac_dma_send(hdev);
		if (err != -EINPROGRESS)
			return err;
	}

	for_each_sg(rctx->sg, tsg, rctx->nents, i) {
		sg[0] = *tsg;
		len = sg->length;

		if (sg_is_last(sg) || (bufcnt + sg[0].length) >= rctx->total) {
			if (!final) {
				/* Always manually put the last word of a non-final transfer. */
				len -= sizeof(u32);
				sg_pcopy_to_buffer(rctx->sg, rctx->nents, &last_word, 4, len);
				sg->length -= sizeof(u32);
			} else {
				/*
				 * In Multiple DMA mode, DMA must be aborted before the final
				 * transfer.
				 */
				sg->length = rctx->total - bufcnt;
				if (hdev->dma_mode > 0) {
					len = (ALIGN(sg->length, 16) - 16);

					ncp = sg_pcopy_to_buffer(rctx->sg, rctx->nents,
								 rctx->state.buffer,
								 sg->length - len,
								 rctx->total - sg->length + len);

					if (!len)
						break;

					sg->length = len;
				} else {
					is_last = true;
					if (!(IS_ALIGNED(sg->length, sizeof(u32)))) {
						len = sg->length;
						sg->length = ALIGN(sg->length,
								   sizeof(u32));
					}
				}
			}
		}

		rctx->dma_ct = dma_map_sg(hdev->dev, sg, 1,
					  DMA_TO_DEVICE);
		if (rctx->dma_ct == 0) {
			dev_err(hdev->dev, "dma_map_sg error\n");
			return -ENOMEM;
		}

		err = stm32_hash_xmit_dma(hdev, sg, len, !is_last);

		/* The last word of a non final transfer is sent manually. */
		if (!final) {
			stm32_hash_write(hdev, HASH_DIN, last_word);
			len += sizeof(u32);
		}

		rctx->total -= len;

		bufcnt += sg[0].length;
		dma_unmap_sg(hdev->dev, sg, 1, DMA_TO_DEVICE);

		if (err == -ENOMEM || err == -ETIMEDOUT)
			return err;
		if (is_last)
			break;
	}

	/*
	 * When the second last block transfer of 4 words is performed by the DMA,
	 * the software must set the DMA Abort bit (DMAA) to 1 before completing the
	 * last transfer of 4 words or less.
	 */
	if (final) {
		if (hdev->dma_mode > 0) {
			if (stm32_hash_wait_busy(hdev))
				return -ETIMEDOUT;
			reg = stm32_hash_read(hdev, HASH_CR);
			reg &= ~HASH_CR_DMAE;
			reg |= HASH_CR_DMAA;
			stm32_hash_write(hdev, HASH_CR, reg);

			if (ncp) {
				memset(buffer + ncp, 0, 4 - DIV_ROUND_UP(ncp, sizeof(u32)));
				writesl(hdev->io_base + HASH_DIN, buffer,
					DIV_ROUND_UP(ncp, sizeof(u32)));
			}

			stm32_hash_set_nblw(hdev, ncp);
			reg = stm32_hash_read(hdev, HASH_STR);
			reg |= HASH_STR_DCAL;
			stm32_hash_write(hdev, HASH_STR, reg);
			err = -EINPROGRESS;
		}

		/*
		 * The hash processor needs the key to be loaded a second time in order
		 * to process the HMAC.
		 */
		if (hdev->flags & HASH_FLAGS_HMAC) {
			if (stm32_hash_wait_busy(hdev))
				return -ETIMEDOUT;
			err = stm32_hash_hmac_dma_send(hdev);
		}

		return err;
	}

	if (err != -EINPROGRESS)
		return err;

	return 0;
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

static int stm32_hash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	struct stm32_hash_state *state = &rctx->state;
	bool sha3_mode = ctx->flags & HASH_FLAGS_SHA3_MODE;

	rctx->hdev = hdev;
	state->flags = 0;

	if (!(hdev->dma_lch &&  hdev->pdata->has_mdmat))
		state->flags |= HASH_FLAGS_CPU;

	if (sha3_mode)
		state->flags |= HASH_FLAGS_SHA3_MODE;

	rctx->digcnt = crypto_ahash_digestsize(tfm);
	switch (rctx->digcnt) {
	case MD5_DIGEST_SIZE:
		state->flags |= HASH_MD5 << HASH_FLAGS_ALGO_SHIFT;
		break;
	case SHA1_DIGEST_SIZE:
		if (hdev->pdata->ux500)
			state->flags |= HASH_SHA1_UX500 << HASH_FLAGS_ALGO_SHIFT;
		else
			state->flags |= HASH_SHA1 << HASH_FLAGS_ALGO_SHIFT;
		break;
	case SHA224_DIGEST_SIZE:
		if (sha3_mode)
			state->flags |= HASH_SHA3_224 << HASH_FLAGS_ALGO_SHIFT;
		else
			state->flags |= HASH_SHA224 << HASH_FLAGS_ALGO_SHIFT;
		break;
	case SHA256_DIGEST_SIZE:
		if (sha3_mode) {
			state->flags |= HASH_SHA3_256 << HASH_FLAGS_ALGO_SHIFT;
		} else {
			if (hdev->pdata->ux500)
				state->flags |= HASH_SHA256_UX500 << HASH_FLAGS_ALGO_SHIFT;
			else
				state->flags |= HASH_SHA256 << HASH_FLAGS_ALGO_SHIFT;
		}
		break;
	case SHA384_DIGEST_SIZE:
		if (sha3_mode)
			state->flags |= HASH_SHA3_384 << HASH_FLAGS_ALGO_SHIFT;
		else
			state->flags |= HASH_SHA384 << HASH_FLAGS_ALGO_SHIFT;
		break;
	case SHA512_DIGEST_SIZE:
		if (sha3_mode)
			state->flags |= HASH_SHA3_512 << HASH_FLAGS_ALGO_SHIFT;
		else
			state->flags |= HASH_SHA512 << HASH_FLAGS_ALGO_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	rctx->state.bufcnt = 0;
	rctx->state.blocklen = crypto_ahash_blocksize(tfm) + sizeof(u32);
	if (rctx->state.blocklen > HASH_BUFLEN) {
		dev_err(hdev->dev, "Error, block too large");
		return -EINVAL;
	}
	rctx->nents = 0;
	rctx->total = 0;
	rctx->offset = 0;
	rctx->data_type = HASH_DATA_8_BITS;

	if (ctx->flags & HASH_FLAGS_HMAC)
		state->flags |= HASH_FLAGS_HMAC;

	dev_dbg(hdev->dev, "%s Flags %x\n", __func__, state->flags);

	return 0;
}

static int stm32_hash_update_req(struct stm32_hash_dev *hdev)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(hdev->req);
	struct stm32_hash_state *state = &rctx->state;

	dev_dbg(hdev->dev, "update_req: total: %u, digcnt: %zd, final: 0",
		rctx->total, rctx->digcnt);

	if (!(state->flags & HASH_FLAGS_CPU))
		return stm32_hash_dma_send(hdev);

	return stm32_hash_update_cpu(hdev);
}

static int stm32_hash_final_req(struct stm32_hash_dev *hdev)
{
	struct ahash_request *req = hdev->req;
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_state *state = &rctx->state;
	int buflen = state->bufcnt;

	if (!(state->flags & HASH_FLAGS_CPU)) {
		hdev->flags |= HASH_FLAGS_FINAL;
		return stm32_hash_dma_send(hdev);
	}

	if (state->flags & HASH_FLAGS_FINUP)
		return stm32_hash_update_req(hdev);

	state->bufcnt = 0;

	return stm32_hash_xmit_cpu(hdev, state->buffer, buflen, 1);
}

static void stm32_hash_emptymsg_fallback(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_dev *hdev = rctx->hdev;
	int ret;

	dev_dbg(hdev->dev, "use fallback message size 0 key size %d\n",
		ctx->keylen);

	if (!ctx->xtfm) {
		dev_err(hdev->dev, "no fallback engine\n");
		return;
	}

	if (ctx->keylen) {
		ret = crypto_shash_setkey(ctx->xtfm, ctx->key, ctx->keylen);
		if (ret) {
			dev_err(hdev->dev, "failed to set key ret=%d\n", ret);
			return;
		}
	}

	ret = crypto_shash_tfm_digest(ctx->xtfm, NULL, 0, rctx->digest);
	if (ret)
		dev_err(hdev->dev, "shash digest error\n");
}

static void stm32_hash_copy_hash(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_state *state = &rctx->state;
	struct stm32_hash_dev *hdev = rctx->hdev;
	__be32 *hash = (void *)rctx->digest;
	unsigned int i, hashsize;

	if (hdev->pdata->broken_emptymsg && (state->flags & HASH_FLAGS_EMPTY))
		return stm32_hash_emptymsg_fallback(req);

	hashsize = crypto_ahash_digestsize(tfm);

	for (i = 0; i < hashsize / sizeof(u32); i++) {
		if (hdev->pdata->ux500)
			hash[i] = cpu_to_be32(stm32_hash_read(hdev,
					      HASH_UX500_HREG(i)));
		else
			hash[i] = cpu_to_be32(stm32_hash_read(hdev,
					      HASH_HREG(i)));
	}
}

static int stm32_hash_finish(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	u32 reg;

	reg = stm32_hash_read(rctx->hdev, HASH_SR);
	reg &= ~HASH_SR_OUTPUT_READY;
	stm32_hash_write(rctx->hdev, HASH_SR, reg);

	if (!req->result)
		return -EINVAL;

	memcpy(req->result, rctx->digest, rctx->digcnt);

	return 0;
}

static void stm32_hash_finish_req(struct ahash_request *req, int err)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_state *state = &rctx->state;
	struct stm32_hash_dev *hdev = rctx->hdev;

	if (hdev->flags & HASH_FLAGS_DMA_ACTIVE)
		state->flags |= HASH_FLAGS_DMA_ACTIVE;
	else
		state->flags &= ~HASH_FLAGS_DMA_ACTIVE;

	if (!err && (HASH_FLAGS_FINAL & hdev->flags)) {
		stm32_hash_copy_hash(req);
		err = stm32_hash_finish(req);
	}

	/* Finalized request mist be unprepared here */
	stm32_hash_unprepare_request(req);

	crypto_finalize_hash_request(hdev->engine, req, err);
}

static int stm32_hash_handle_queue(struct stm32_hash_dev *hdev,
				   struct ahash_request *req)
{
	return crypto_transfer_hash_request_to_engine(hdev->engine, req);
}

static int stm32_hash_one_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	struct stm32_hash_state *state = &rctx->state;
	int swap_reg;
	int err = 0;

	if (!hdev)
		return -ENODEV;

	dev_dbg(hdev->dev, "processing new req, op: %lu, nbytes %d\n",
		rctx->op, req->nbytes);

	pm_runtime_get_sync(hdev->dev);

	err = stm32_hash_prepare_request(req);
	if (err)
		return err;

	hdev->req = req;
	hdev->flags = 0;
	swap_reg = hash_swap_reg(rctx);

	if (state->flags & HASH_FLAGS_INIT) {
		u32 *preg = rctx->state.hw_context;
		u32 reg;
		int i;

		if (!hdev->pdata->ux500)
			stm32_hash_write(hdev, HASH_IMR, *preg++);
		stm32_hash_write(hdev, HASH_STR, *preg++);
		stm32_hash_write(hdev, HASH_CR, *preg);
		reg = *preg++ | HASH_CR_INIT;
		stm32_hash_write(hdev, HASH_CR, reg);

		for (i = 0; i < swap_reg; i++)
			stm32_hash_write(hdev, HASH_CSR(i), *preg++);

		hdev->flags |= HASH_FLAGS_INIT;

		if (state->flags & HASH_FLAGS_HMAC)
			hdev->flags |= HASH_FLAGS_HMAC |
				       HASH_FLAGS_HMAC_KEY;

		if (state->flags & HASH_FLAGS_CPU)
			hdev->flags |= HASH_FLAGS_CPU;

		if (state->flags & HASH_FLAGS_DMA_ACTIVE)
			hdev->flags |= HASH_FLAGS_DMA_ACTIVE;
	}

	if (rctx->op == HASH_OP_UPDATE)
		err = stm32_hash_update_req(hdev);
	else if (rctx->op == HASH_OP_FINAL)
		err = stm32_hash_final_req(hdev);

	/* If we have an IRQ, wait for that, else poll for completion */
	if (err == -EINPROGRESS && hdev->polled) {
		if (stm32_hash_wait_busy(hdev))
			err = -ETIMEDOUT;
		else {
			hdev->flags |= HASH_FLAGS_OUTPUT_READY;
			err = 0;
		}
	}

	if (err != -EINPROGRESS)
	/* done task will not finish it, so do it here */
		stm32_hash_finish_req(req, err);

	return 0;
}

static int stm32_hash_copy_sgs(struct stm32_hash_request_ctx *rctx,
			       struct scatterlist *sg, int bs,
			       unsigned int new_len)
{
	struct stm32_hash_state *state = &rctx->state;
	int pages;
	void *buf;

	pages = get_order(new_len);

	buf = (void *)__get_free_pages(GFP_ATOMIC, pages);
	if (!buf) {
		pr_err("Couldn't allocate pages for unaligned cases.\n");
		return -ENOMEM;
	}

	if (state->bufcnt)
		memcpy(buf, rctx->hdev->xmit_buf, state->bufcnt);

	scatterwalk_map_and_copy(buf + state->bufcnt, sg, rctx->offset,
				 min(new_len, rctx->total) - state->bufcnt, 0);
	sg_init_table(rctx->sgl, 1);
	sg_set_buf(rctx->sgl, buf, new_len);
	rctx->sg = rctx->sgl;
	state->flags |= HASH_FLAGS_SGS_COPIED;
	rctx->nents = 1;
	rctx->offset += new_len - state->bufcnt;
	state->bufcnt = 0;
	rctx->total = new_len;

	return 0;
}

static int stm32_hash_align_sgs(struct scatterlist *sg,
				int nbytes, int bs, bool init, bool final,
				struct stm32_hash_request_ctx *rctx)
{
	struct stm32_hash_state *state = &rctx->state;
	struct stm32_hash_dev *hdev = rctx->hdev;
	struct scatterlist *sg_tmp = sg;
	int offset = rctx->offset;
	int new_len;
	int n = 0;
	int bufcnt = state->bufcnt;
	bool secure_ctx = hdev->pdata->context_secured;
	bool aligned = true;

	if (!sg || !sg->length || !nbytes) {
		if (bufcnt) {
			bufcnt = DIV_ROUND_UP(bufcnt, bs) * bs;
			sg_init_table(rctx->sgl, 1);
			sg_set_buf(rctx->sgl, rctx->hdev->xmit_buf, bufcnt);
			rctx->sg = rctx->sgl;
			rctx->nents = 1;
		}

		return 0;
	}

	new_len = nbytes;

	if (offset)
		aligned = false;

	if (final) {
		new_len = DIV_ROUND_UP(new_len, bs) * bs;
	} else {
		new_len = (new_len - 1) / bs * bs; // return n block - 1 block

		/*
		 * Context save in some version of HASH IP can only be done when the
		 * FIFO is ready to get a new block. This implies to send n block plus a
		 * 32 bit word in the first DMA send.
		 */
		if (init && secure_ctx) {
			new_len += sizeof(u32);
			if (unlikely(new_len > nbytes))
				new_len -= bs;
		}
	}

	if (!new_len)
		return 0;

	if (nbytes != new_len)
		aligned = false;

	while (nbytes > 0 && sg_tmp) {
		n++;

		if (bufcnt) {
			if (!IS_ALIGNED(bufcnt, bs)) {
				aligned = false;
				break;
			}
			nbytes -= bufcnt;
			bufcnt = 0;
			if (!nbytes)
				aligned = false;

			continue;
		}

		if (offset < sg_tmp->length) {
			if (!IS_ALIGNED(offset + sg_tmp->offset, 4)) {
				aligned = false;
				break;
			}

			if (!IS_ALIGNED(sg_tmp->length - offset, bs)) {
				aligned = false;
				break;
			}
		}

		if (offset) {
			offset -= sg_tmp->length;
			if (offset < 0) {
				nbytes += offset;
				offset = 0;
			}
		} else {
			nbytes -= sg_tmp->length;
		}

		sg_tmp = sg_next(sg_tmp);

		if (nbytes < 0) {
			aligned = false;
			break;
		}
	}

	if (!aligned)
		return stm32_hash_copy_sgs(rctx, sg, bs, new_len);

	rctx->total = new_len;
	rctx->offset += new_len;
	rctx->nents = n;
	if (state->bufcnt) {
		sg_init_table(rctx->sgl, 2);
		sg_set_buf(rctx->sgl, rctx->hdev->xmit_buf, state->bufcnt);
		sg_chain(rctx->sgl, 2, sg);
		rctx->sg = rctx->sgl;
	} else {
		rctx->sg = sg;
	}

	return 0;
}

static int stm32_hash_prepare_request(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	struct stm32_hash_state *state = &rctx->state;
	unsigned int nbytes;
	int ret, hash_later, bs;
	bool update = rctx->op & HASH_OP_UPDATE;
	bool init = !(state->flags & HASH_FLAGS_INIT);
	bool finup = state->flags & HASH_FLAGS_FINUP;
	bool final = state->flags & HASH_FLAGS_FINAL;

	if (!hdev->dma_lch || state->flags & HASH_FLAGS_CPU)
		return 0;

	bs = crypto_ahash_blocksize(tfm);

	nbytes = state->bufcnt;

	/*
	 * In case of update request nbytes must correspond to the content of the
	 * buffer + the offset minus the content of the request already in the
	 * buffer.
	 */
	if (update || finup)
		nbytes += req->nbytes - rctx->offset;

	dev_dbg(hdev->dev,
		"%s: nbytes=%d, bs=%d, total=%d, offset=%d, bufcnt=%d\n",
		__func__, nbytes, bs, rctx->total, rctx->offset, state->bufcnt);

	if (!nbytes)
		return 0;

	rctx->total = nbytes;

	if (update && req->nbytes && (!IS_ALIGNED(state->bufcnt, bs))) {
		int len = bs - state->bufcnt % bs;

		if (len > req->nbytes)
			len = req->nbytes;
		scatterwalk_map_and_copy(state->buffer + state->bufcnt, req->src,
					 0, len, 0);
		state->bufcnt += len;
		rctx->offset = len;
	}

	/* copy buffer in a temporary one that is used for sg alignment */
	if (state->bufcnt)
		memcpy(hdev->xmit_buf, state->buffer, state->bufcnt);

	ret = stm32_hash_align_sgs(req->src, nbytes, bs, init, final, rctx);
	if (ret)
		return ret;

	hash_later = nbytes - rctx->total;
	if (hash_later < 0)
		hash_later = 0;

	if (hash_later && hash_later <= state->blocklen) {
		scatterwalk_map_and_copy(state->buffer,
					 req->src,
					 req->nbytes - hash_later,
					 hash_later, 0);

		state->bufcnt = hash_later;
	} else {
		state->bufcnt = 0;
	}

	if (hash_later > state->blocklen) {
		/* FIXME: add support of this case */
		pr_err("Buffer contains more than one block.\n");
		return -ENOMEM;
	}

	rctx->total = min(nbytes, rctx->total);

	return 0;
}

static void stm32_hash_unprepare_request(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_state *state = &rctx->state;
	struct stm32_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	u32 *preg = state->hw_context;
	int swap_reg, i;

	if (hdev->dma_lch)
		dmaengine_terminate_sync(hdev->dma_lch);

	if (state->flags & HASH_FLAGS_SGS_COPIED)
		free_pages((unsigned long)sg_virt(rctx->sg), get_order(rctx->sg->length));

	rctx->sg = NULL;
	rctx->offset = 0;

	state->flags &= ~(HASH_FLAGS_SGS_COPIED);

	if (!(hdev->flags & HASH_FLAGS_INIT))
		goto pm_runtime;

	state->flags |= HASH_FLAGS_INIT;

	if (stm32_hash_wait_busy(hdev)) {
		dev_warn(hdev->dev, "Wait busy failed.");
		return;
	}

	swap_reg = hash_swap_reg(rctx);

	if (!hdev->pdata->ux500)
		*preg++ = stm32_hash_read(hdev, HASH_IMR);
	*preg++ = stm32_hash_read(hdev, HASH_STR);
	*preg++ = stm32_hash_read(hdev, HASH_CR);
	for (i = 0; i < swap_reg; i++)
		*preg++ = stm32_hash_read(hdev, HASH_CSR(i));

pm_runtime:
	pm_runtime_mark_last_busy(hdev->dev);
	pm_runtime_put_autosuspend(hdev->dev);
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
	struct stm32_hash_state *state = &rctx->state;

	if (!req->nbytes)
		return 0;


	if (state->flags & HASH_FLAGS_CPU) {
		rctx->total = req->nbytes;
		rctx->sg = req->src;
		rctx->offset = 0;

		if ((state->bufcnt + rctx->total < state->blocklen)) {
			stm32_hash_append_sg(rctx);
			return 0;
		}
	} else { /* DMA mode */
		if (state->bufcnt + req->nbytes <= state->blocklen) {
			scatterwalk_map_and_copy(state->buffer + state->bufcnt, req->src,
						 0, req->nbytes, 0);
			state->bufcnt += req->nbytes;
			return 0;
		}
	}

	return stm32_hash_enqueue(req, HASH_OP_UPDATE);
}

static int stm32_hash_final(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_state *state = &rctx->state;

	state->flags |= HASH_FLAGS_FINAL;

	return stm32_hash_enqueue(req, HASH_OP_FINAL);
}

static int stm32_hash_finup(struct ahash_request *req)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);
	struct stm32_hash_state *state = &rctx->state;

	if (!req->nbytes)
		goto out;

	state->flags |= HASH_FLAGS_FINUP;

	if ((state->flags & HASH_FLAGS_CPU)) {
		rctx->total = req->nbytes;
		rctx->sg = req->src;
		rctx->offset = 0;
	}

out:
	return stm32_hash_final(req);
}

static int stm32_hash_digest(struct ahash_request *req)
{
	return stm32_hash_init(req) ?: stm32_hash_finup(req);
}

static int stm32_hash_export(struct ahash_request *req, void *out)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);

	memcpy(out, &rctx->state, sizeof(rctx->state));

	return 0;
}

static int stm32_hash_import(struct ahash_request *req, const void *in)
{
	struct stm32_hash_request_ctx *rctx = ahash_request_ctx(req);

	stm32_hash_init(req);
	memcpy(&rctx->state, in, sizeof(rctx->state));

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

static int stm32_hash_init_fallback(struct crypto_tfm *tfm)
{
	struct stm32_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct stm32_hash_dev *hdev = stm32_hash_find_dev(ctx);
	const char *name = crypto_tfm_alg_name(tfm);
	struct crypto_shash *xtfm;

	/* The fallback is only needed on Ux500 */
	if (!hdev->pdata->ux500)
		return 0;

	xtfm = crypto_alloc_shash(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(xtfm)) {
		dev_err(hdev->dev, "failed to allocate %s fallback\n",
			name);
		return PTR_ERR(xtfm);
	}
	dev_info(hdev->dev, "allocated %s fallback\n", name);
	ctx->xtfm = xtfm;

	return 0;
}

static int stm32_hash_cra_init_algs(struct crypto_tfm *tfm, u32 algs_flags)
{
	struct stm32_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct stm32_hash_request_ctx));

	ctx->keylen = 0;

	if (algs_flags)
		ctx->flags |= algs_flags;

	return stm32_hash_init_fallback(tfm);
}

static int stm32_hash_cra_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, 0);
}

static int stm32_hash_cra_hmac_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, HASH_FLAGS_HMAC);
}

static int stm32_hash_cra_sha3_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, HASH_FLAGS_SHA3_MODE);
}

static int stm32_hash_cra_sha3_hmac_init(struct crypto_tfm *tfm)
{
	return stm32_hash_cra_init_algs(tfm, HASH_FLAGS_SHA3_MODE |
					HASH_FLAGS_HMAC);
}

static void stm32_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct stm32_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->xtfm)
		crypto_free_shash(ctx->xtfm);
}

static irqreturn_t stm32_hash_irq_thread(int irq, void *dev_id)
{
	struct stm32_hash_dev *hdev = dev_id;

	if (HASH_FLAGS_OUTPUT_READY & hdev->flags) {
		hdev->flags &= ~HASH_FLAGS_OUTPUT_READY;
		goto finish;
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
		hdev->flags |= HASH_FLAGS_OUTPUT_READY;
		/* Disable IT*/
		stm32_hash_write(hdev, HASH_IMR, 0);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static struct ahash_engine_alg algs_md5[] = {
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "stm32-md5",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(md5)",
				.cra_driver_name = "stm32-hmac-md5",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	}
};

static struct ahash_engine_alg algs_sha1[] = {
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "stm32-sha1",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha1)",
				.cra_driver_name = "stm32-hmac-sha1",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
};

static struct ahash_engine_alg algs_sha224[] = {
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha224",
				.cra_driver_name = "stm32-sha224",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.setkey = stm32_hash_setkey,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha224)",
				.cra_driver_name = "stm32-hmac-sha224",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
};

static struct ahash_engine_alg algs_sha256[] = {
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha256",
				.cra_driver_name = "stm32-sha256",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha256)",
				.cra_driver_name = "stm32-hmac-sha256",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
};

static struct ahash_engine_alg algs_sha384_sha512[] = {
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA384_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha384",
				.cra_driver_name = "stm32-sha384",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.setkey = stm32_hash_setkey,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA384_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha384)",
				.cra_driver_name = "stm32-hmac-sha384",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha512",
				.cra_driver_name = "stm32-sha512",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha512)",
				.cra_driver_name = "stm32-hmac-sha512",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
};

static struct ahash_engine_alg algs_sha3[] = {
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA3_224_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha3-224",
				.cra_driver_name = "stm32-sha3-224",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA3_224_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha3-224)",
				.cra_driver_name = "stm32-hmac-sha3-224",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA3_256_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha3-256",
				.cra_driver_name = "stm32-sha3-256",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA3_256_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha3-256)",
				.cra_driver_name = "stm32-hmac-sha3-256",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA3_384_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha3-384",
				.cra_driver_name = "stm32-sha3-384",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA3_384_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha3-384)",
				.cra_driver_name = "stm32-hmac-sha3-384",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.halg = {
			.digestsize = SHA3_512_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "sha3-512",
				.cra_driver_name = "stm32-sha3-512",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	},
	{
		.base.init = stm32_hash_init,
		.base.update = stm32_hash_update,
		.base.final = stm32_hash_final,
		.base.finup = stm32_hash_finup,
		.base.digest = stm32_hash_digest,
		.base.export = stm32_hash_export,
		.base.import = stm32_hash_import,
		.base.setkey = stm32_hash_setkey,
		.base.halg = {
			.digestsize = SHA3_512_DIGEST_SIZE,
			.statesize = sizeof(struct stm32_hash_state),
			.base = {
				.cra_name = "hmac(sha3-512)",
				.cra_driver_name = "stm32-hmac-sha3-512",
				.cra_priority = 200,
				.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize = SHA3_512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct stm32_hash_ctx),
				.cra_init = stm32_hash_cra_sha3_hmac_init,
				.cra_exit = stm32_hash_cra_exit,
				.cra_module = THIS_MODULE,
			}
		},
		.op = {
			.do_one_request = stm32_hash_one_request,
		},
	}
};

static int stm32_hash_register_algs(struct stm32_hash_dev *hdev)
{
	unsigned int i, j;
	int err;

	for (i = 0; i < hdev->pdata->algs_info_size; i++) {
		for (j = 0; j < hdev->pdata->algs_info[i].size; j++) {
			err = crypto_engine_register_ahash(
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
			crypto_engine_unregister_ahash(
				&hdev->pdata->algs_info[i].algs_list[j]);
	}

	return err;
}

static int stm32_hash_unregister_algs(struct stm32_hash_dev *hdev)
{
	unsigned int i, j;

	for (i = 0; i < hdev->pdata->algs_info_size; i++) {
		for (j = 0; j < hdev->pdata->algs_info[i].size; j++)
			crypto_engine_unregister_ahash(
				&hdev->pdata->algs_info[i].algs_list[j]);
	}

	return 0;
}

static struct stm32_hash_algs_info stm32_hash_algs_info_ux500[] = {
	{
		.algs_list	= algs_sha1,
		.size		= ARRAY_SIZE(algs_sha1),
	},
	{
		.algs_list	= algs_sha256,
		.size		= ARRAY_SIZE(algs_sha256),
	},
};

static const struct stm32_hash_pdata stm32_hash_pdata_ux500 = {
	.alg_shift	= 7,
	.algs_info	= stm32_hash_algs_info_ux500,
	.algs_info_size	= ARRAY_SIZE(stm32_hash_algs_info_ux500),
	.broken_emptymsg = true,
	.ux500		= true,
};

static struct stm32_hash_algs_info stm32_hash_algs_info_stm32f4[] = {
	{
		.algs_list	= algs_md5,
		.size		= ARRAY_SIZE(algs_md5),
	},
	{
		.algs_list	= algs_sha1,
		.size		= ARRAY_SIZE(algs_sha1),
	},
};

static const struct stm32_hash_pdata stm32_hash_pdata_stm32f4 = {
	.alg_shift	= 7,
	.algs_info	= stm32_hash_algs_info_stm32f4,
	.algs_info_size	= ARRAY_SIZE(stm32_hash_algs_info_stm32f4),
	.has_sr		= true,
	.has_mdmat	= true,
};

static struct stm32_hash_algs_info stm32_hash_algs_info_stm32f7[] = {
	{
		.algs_list	= algs_md5,
		.size		= ARRAY_SIZE(algs_md5),
	},
	{
		.algs_list	= algs_sha1,
		.size		= ARRAY_SIZE(algs_sha1),
	},
	{
		.algs_list	= algs_sha224,
		.size		= ARRAY_SIZE(algs_sha224),
	},
	{
		.algs_list	= algs_sha256,
		.size		= ARRAY_SIZE(algs_sha256),
	},
};

static const struct stm32_hash_pdata stm32_hash_pdata_stm32f7 = {
	.alg_shift	= 7,
	.algs_info	= stm32_hash_algs_info_stm32f7,
	.algs_info_size	= ARRAY_SIZE(stm32_hash_algs_info_stm32f7),
	.has_sr		= true,
	.has_mdmat	= true,
};

static struct stm32_hash_algs_info stm32_hash_algs_info_stm32mp13[] = {
	{
		.algs_list	= algs_sha1,
		.size		= ARRAY_SIZE(algs_sha1),
	},
	{
		.algs_list	= algs_sha224,
		.size		= ARRAY_SIZE(algs_sha224),
	},
	{
		.algs_list	= algs_sha256,
		.size		= ARRAY_SIZE(algs_sha256),
	},
	{
		.algs_list	= algs_sha384_sha512,
		.size		= ARRAY_SIZE(algs_sha384_sha512),
	},
	{
		.algs_list	= algs_sha3,
		.size		= ARRAY_SIZE(algs_sha3),
	},
};

static const struct stm32_hash_pdata stm32_hash_pdata_stm32mp13 = {
	.alg_shift	= 17,
	.algs_info	= stm32_hash_algs_info_stm32mp13,
	.algs_info_size	= ARRAY_SIZE(stm32_hash_algs_info_stm32mp13),
	.has_sr		= true,
	.has_mdmat	= true,
	.context_secured = true,
};

static const struct of_device_id stm32_hash_of_match[] = {
	{ .compatible = "stericsson,ux500-hash", .data = &stm32_hash_pdata_ux500 },
	{ .compatible = "st,stm32f456-hash", .data = &stm32_hash_pdata_stm32f4 },
	{ .compatible = "st,stm32f756-hash", .data = &stm32_hash_pdata_stm32f7 },
	{ .compatible = "st,stm32mp13-hash", .data = &stm32_hash_pdata_stm32mp13 },
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

	hdev->io_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(hdev->io_base))
		return PTR_ERR(hdev->io_base);

	hdev->phys_base = res->start;

	ret = stm32_hash_get_of_match(hdev, dev);
	if (ret)
		return ret;

	irq = platform_get_irq_optional(pdev, 0);
	if (irq < 0 && irq != -ENXIO)
		return irq;

	if (irq > 0) {
		ret = devm_request_threaded_irq(dev, irq,
						stm32_hash_irq_handler,
						stm32_hash_irq_thread,
						IRQF_ONESHOT,
						dev_name(dev), hdev);
		if (ret) {
			dev_err(dev, "Cannot grab IRQ\n");
			return ret;
		}
	} else {
		dev_info(dev, "No IRQ, use polling mode\n");
		hdev->polled = true;
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
	case -ENODEV:
		dev_info(dev, "DMA mode not available\n");
		break;
	default:
		dev_err(dev, "DMA init error %d\n", ret);
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

	if (hdev->pdata->ux500)
		/* FIXME: implement DMA mode for Ux500 */
		hdev->dma_mode = 0;
	else
		hdev->dma_mode = stm32_hash_read(hdev, HASH_HWCFGR) & HASH_HWCFG_DMA_MASK;

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

static void stm32_hash_remove(struct platform_device *pdev)
{
	struct stm32_hash_dev *hdev = platform_get_drvdata(pdev);
	int ret;

	ret = pm_runtime_get_sync(hdev->dev);

	stm32_hash_unregister_algs(hdev);

	crypto_engine_exit(hdev->engine);

	spin_lock(&stm32_hash.lock);
	list_del(&hdev->list);
	spin_unlock(&stm32_hash.lock);

	if (hdev->dma_lch)
		dma_release_channel(hdev->dma_lch);

	pm_runtime_disable(hdev->dev);
	pm_runtime_put_noidle(hdev->dev);

	if (ret >= 0)
		clk_disable_unprepare(hdev->clk);
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
	.remove_new	= stm32_hash_remove,
	.driver		= {
		.name	= "stm32-hash",
		.pm = &stm32_hash_pm_ops,
		.of_match_table	= stm32_hash_of_match,
	}
};

module_platform_driver(stm32_hash_driver);

MODULE_DESCRIPTION("STM32 SHA1/SHA2/SHA3 & MD5 (HMAC) hw accelerator driver");
MODULE_AUTHOR("Lionel Debieve <lionel.debieve@st.com>");
MODULE_LICENSE("GPL v2");
