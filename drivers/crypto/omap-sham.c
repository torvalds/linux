/*
 * Cryptographic API.
 *
 * Support for OMAP SHA1/MD5 HW acceleration.
 *
 * Copyright (c) 2010 Nokia Corporation
 * Author: Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 * Copyright (c) 2011 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Some ideas are from old omap-sha1-md5.c driver.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>

#define MD5_DIGEST_SIZE			16

#define SHA_REG_IDIGEST(dd, x)		((dd)->pdata->idigest_ofs + ((x)*0x04))
#define SHA_REG_DIN(dd, x)		((dd)->pdata->din_ofs + ((x) * 0x04))
#define SHA_REG_DIGCNT(dd)		((dd)->pdata->digcnt_ofs)

#define SHA_REG_ODIGEST(dd, x)		((dd)->pdata->odigest_ofs + (x * 0x04))

#define SHA_REG_CTRL			0x18
#define SHA_REG_CTRL_LENGTH		(0xFFFFFFFF << 5)
#define SHA_REG_CTRL_CLOSE_HASH		(1 << 4)
#define SHA_REG_CTRL_ALGO_CONST		(1 << 3)
#define SHA_REG_CTRL_ALGO		(1 << 2)
#define SHA_REG_CTRL_INPUT_READY	(1 << 1)
#define SHA_REG_CTRL_OUTPUT_READY	(1 << 0)

#define SHA_REG_REV(dd)			((dd)->pdata->rev_ofs)

#define SHA_REG_MASK(dd)		((dd)->pdata->mask_ofs)
#define SHA_REG_MASK_DMA_EN		(1 << 3)
#define SHA_REG_MASK_IT_EN		(1 << 2)
#define SHA_REG_MASK_SOFTRESET		(1 << 1)
#define SHA_REG_AUTOIDLE		(1 << 0)

#define SHA_REG_SYSSTATUS(dd)		((dd)->pdata->sysstatus_ofs)
#define SHA_REG_SYSSTATUS_RESETDONE	(1 << 0)

#define SHA_REG_MODE(dd)		((dd)->pdata->mode_ofs)
#define SHA_REG_MODE_HMAC_OUTER_HASH	(1 << 7)
#define SHA_REG_MODE_HMAC_KEY_PROC	(1 << 5)
#define SHA_REG_MODE_CLOSE_HASH		(1 << 4)
#define SHA_REG_MODE_ALGO_CONSTANT	(1 << 3)

#define SHA_REG_MODE_ALGO_MASK		(7 << 0)
#define SHA_REG_MODE_ALGO_MD5_128	(0 << 1)
#define SHA_REG_MODE_ALGO_SHA1_160	(1 << 1)
#define SHA_REG_MODE_ALGO_SHA2_224	(2 << 1)
#define SHA_REG_MODE_ALGO_SHA2_256	(3 << 1)
#define SHA_REG_MODE_ALGO_SHA2_384	(1 << 0)
#define SHA_REG_MODE_ALGO_SHA2_512	(3 << 0)

#define SHA_REG_LENGTH(dd)		((dd)->pdata->length_ofs)

#define SHA_REG_IRQSTATUS		0x118
#define SHA_REG_IRQSTATUS_CTX_RDY	(1 << 3)
#define SHA_REG_IRQSTATUS_PARTHASH_RDY (1 << 2)
#define SHA_REG_IRQSTATUS_INPUT_RDY	(1 << 1)
#define SHA_REG_IRQSTATUS_OUTPUT_RDY	(1 << 0)

#define SHA_REG_IRQENA			0x11C
#define SHA_REG_IRQENA_CTX_RDY		(1 << 3)
#define SHA_REG_IRQENA_PARTHASH_RDY	(1 << 2)
#define SHA_REG_IRQENA_INPUT_RDY	(1 << 1)
#define SHA_REG_IRQENA_OUTPUT_RDY	(1 << 0)

#define DEFAULT_TIMEOUT_INTERVAL	HZ

/* mostly device flags */
#define FLAGS_BUSY		0
#define FLAGS_FINAL		1
#define FLAGS_DMA_ACTIVE	2
#define FLAGS_OUTPUT_READY	3
#define FLAGS_INIT		4
#define FLAGS_CPU		5
#define FLAGS_DMA_READY		6
#define FLAGS_AUTO_XOR		7
#define FLAGS_BE32_SHA1		8
/* context flags */
#define FLAGS_FINUP		16
#define FLAGS_SG		17

#define FLAGS_MODE_SHIFT	18
#define FLAGS_MODE_MASK		(SHA_REG_MODE_ALGO_MASK	<< FLAGS_MODE_SHIFT)
#define FLAGS_MODE_MD5		(SHA_REG_MODE_ALGO_MD5_128 << FLAGS_MODE_SHIFT)
#define FLAGS_MODE_SHA1		(SHA_REG_MODE_ALGO_SHA1_160 << FLAGS_MODE_SHIFT)
#define FLAGS_MODE_SHA224	(SHA_REG_MODE_ALGO_SHA2_224 << FLAGS_MODE_SHIFT)
#define FLAGS_MODE_SHA256	(SHA_REG_MODE_ALGO_SHA2_256 << FLAGS_MODE_SHIFT)
#define FLAGS_MODE_SHA384	(SHA_REG_MODE_ALGO_SHA2_384 << FLAGS_MODE_SHIFT)
#define FLAGS_MODE_SHA512	(SHA_REG_MODE_ALGO_SHA2_512 << FLAGS_MODE_SHIFT)

#define FLAGS_HMAC		21
#define FLAGS_ERROR		22

#define OP_UPDATE		1
#define OP_FINAL		2

#define OMAP_ALIGN_MASK		(sizeof(u32)-1)
#define OMAP_ALIGNED		__attribute__((aligned(sizeof(u32))))

#define BUFLEN			PAGE_SIZE

struct omap_sham_dev;

struct omap_sham_reqctx {
	struct omap_sham_dev	*dd;
	unsigned long		flags;
	unsigned long		op;

	u8			digest[SHA512_DIGEST_SIZE] OMAP_ALIGNED;
	size_t			digcnt;
	size_t			bufcnt;
	size_t			buflen;
	dma_addr_t		dma_addr;

	/* walk state */
	struct scatterlist	*sg;
	struct scatterlist	sgl;
	unsigned int		offset;	/* offset in current sg */
	unsigned int		total;	/* total request */

	u8			buffer[0] OMAP_ALIGNED;
};

struct omap_sham_hmac_ctx {
	struct crypto_shash	*shash;
	u8			ipad[SHA512_BLOCK_SIZE] OMAP_ALIGNED;
	u8			opad[SHA512_BLOCK_SIZE] OMAP_ALIGNED;
};

struct omap_sham_ctx {
	struct omap_sham_dev	*dd;

	unsigned long		flags;

	/* fallback stuff */
	struct crypto_shash	*fallback;

	struct omap_sham_hmac_ctx base[0];
};

#define OMAP_SHAM_QUEUE_LENGTH	1

struct omap_sham_algs_info {
	struct ahash_alg	*algs_list;
	unsigned int		size;
	unsigned int		registered;
};

struct omap_sham_pdata {
	struct omap_sham_algs_info	*algs_info;
	unsigned int	algs_info_size;
	unsigned long	flags;
	int		digest_size;

	void		(*copy_hash)(struct ahash_request *req, int out);
	void		(*write_ctrl)(struct omap_sham_dev *dd, size_t length,
				      int final, int dma);
	void		(*trigger)(struct omap_sham_dev *dd, size_t length);
	int		(*poll_irq)(struct omap_sham_dev *dd);
	irqreturn_t	(*intr_hdlr)(int irq, void *dev_id);

	u32		odigest_ofs;
	u32		idigest_ofs;
	u32		din_ofs;
	u32		digcnt_ofs;
	u32		rev_ofs;
	u32		mask_ofs;
	u32		sysstatus_ofs;
	u32		mode_ofs;
	u32		length_ofs;

	u32		major_mask;
	u32		major_shift;
	u32		minor_mask;
	u32		minor_shift;
};

struct omap_sham_dev {
	struct list_head	list;
	unsigned long		phys_base;
	struct device		*dev;
	void __iomem		*io_base;
	int			irq;
	spinlock_t		lock;
	int			err;
	struct dma_chan		*dma_lch;
	struct tasklet_struct	done_task;
	u8			polling_mode;

	unsigned long		flags;
	struct crypto_queue	queue;
	struct ahash_request	*req;

	const struct omap_sham_pdata	*pdata;
};

struct omap_sham_drv {
	struct list_head	dev_list;
	spinlock_t		lock;
	unsigned long		flags;
};

static struct omap_sham_drv sham = {
	.dev_list = LIST_HEAD_INIT(sham.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(sham.lock),
};

static inline u32 omap_sham_read(struct omap_sham_dev *dd, u32 offset)
{
	return __raw_readl(dd->io_base + offset);
}

static inline void omap_sham_write(struct omap_sham_dev *dd,
					u32 offset, u32 value)
{
	__raw_writel(value, dd->io_base + offset);
}

static inline void omap_sham_write_mask(struct omap_sham_dev *dd, u32 address,
					u32 value, u32 mask)
{
	u32 val;

	val = omap_sham_read(dd, address);
	val &= ~mask;
	val |= value;
	omap_sham_write(dd, address, val);
}

static inline int omap_sham_wait(struct omap_sham_dev *dd, u32 offset, u32 bit)
{
	unsigned long timeout = jiffies + DEFAULT_TIMEOUT_INTERVAL;

	while (!(omap_sham_read(dd, offset) & bit)) {
		if (time_is_before_jiffies(timeout))
			return -ETIMEDOUT;
	}

	return 0;
}

static void omap_sham_copy_hash_omap2(struct ahash_request *req, int out)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;
	u32 *hash = (u32 *)ctx->digest;
	int i;

	for (i = 0; i < dd->pdata->digest_size / sizeof(u32); i++) {
		if (out)
			hash[i] = omap_sham_read(dd, SHA_REG_IDIGEST(dd, i));
		else
			omap_sham_write(dd, SHA_REG_IDIGEST(dd, i), hash[i]);
	}
}

static void omap_sham_copy_hash_omap4(struct ahash_request *req, int out)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;
	int i;

	if (ctx->flags & BIT(FLAGS_HMAC)) {
		struct crypto_ahash *tfm = crypto_ahash_reqtfm(dd->req);
		struct omap_sham_ctx *tctx = crypto_ahash_ctx(tfm);
		struct omap_sham_hmac_ctx *bctx = tctx->base;
		u32 *opad = (u32 *)bctx->opad;

		for (i = 0; i < dd->pdata->digest_size / sizeof(u32); i++) {
			if (out)
				opad[i] = omap_sham_read(dd,
						SHA_REG_ODIGEST(dd, i));
			else
				omap_sham_write(dd, SHA_REG_ODIGEST(dd, i),
						opad[i]);
		}
	}

	omap_sham_copy_hash_omap2(req, out);
}

static void omap_sham_copy_ready_hash(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	u32 *in = (u32 *)ctx->digest;
	u32 *hash = (u32 *)req->result;
	int i, d, big_endian = 0;

	if (!hash)
		return;

	switch (ctx->flags & FLAGS_MODE_MASK) {
	case FLAGS_MODE_MD5:
		d = MD5_DIGEST_SIZE / sizeof(u32);
		break;
	case FLAGS_MODE_SHA1:
		/* OMAP2 SHA1 is big endian */
		if (test_bit(FLAGS_BE32_SHA1, &ctx->dd->flags))
			big_endian = 1;
		d = SHA1_DIGEST_SIZE / sizeof(u32);
		break;
	case FLAGS_MODE_SHA224:
		d = SHA224_DIGEST_SIZE / sizeof(u32);
		break;
	case FLAGS_MODE_SHA256:
		d = SHA256_DIGEST_SIZE / sizeof(u32);
		break;
	case FLAGS_MODE_SHA384:
		d = SHA384_DIGEST_SIZE / sizeof(u32);
		break;
	case FLAGS_MODE_SHA512:
		d = SHA512_DIGEST_SIZE / sizeof(u32);
		break;
	default:
		d = 0;
	}

	if (big_endian)
		for (i = 0; i < d; i++)
			hash[i] = be32_to_cpu(in[i]);
	else
		for (i = 0; i < d; i++)
			hash[i] = le32_to_cpu(in[i]);
}

static int omap_sham_hw_init(struct omap_sham_dev *dd)
{
	int err;

	err = pm_runtime_get_sync(dd->dev);
	if (err < 0) {
		dev_err(dd->dev, "failed to get sync: %d\n", err);
		return err;
	}

	if (!test_bit(FLAGS_INIT, &dd->flags)) {
		set_bit(FLAGS_INIT, &dd->flags);
		dd->err = 0;
	}

	return 0;
}

static void omap_sham_write_ctrl_omap2(struct omap_sham_dev *dd, size_t length,
				 int final, int dma)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	u32 val = length << 5, mask;

	if (likely(ctx->digcnt))
		omap_sham_write(dd, SHA_REG_DIGCNT(dd), ctx->digcnt);

	omap_sham_write_mask(dd, SHA_REG_MASK(dd),
		SHA_REG_MASK_IT_EN | (dma ? SHA_REG_MASK_DMA_EN : 0),
		SHA_REG_MASK_IT_EN | SHA_REG_MASK_DMA_EN);
	/*
	 * Setting ALGO_CONST only for the first iteration
	 * and CLOSE_HASH only for the last one.
	 */
	if ((ctx->flags & FLAGS_MODE_MASK) == FLAGS_MODE_SHA1)
		val |= SHA_REG_CTRL_ALGO;
	if (!ctx->digcnt)
		val |= SHA_REG_CTRL_ALGO_CONST;
	if (final)
		val |= SHA_REG_CTRL_CLOSE_HASH;

	mask = SHA_REG_CTRL_ALGO_CONST | SHA_REG_CTRL_CLOSE_HASH |
			SHA_REG_CTRL_ALGO | SHA_REG_CTRL_LENGTH;

	omap_sham_write_mask(dd, SHA_REG_CTRL, val, mask);
}

static void omap_sham_trigger_omap2(struct omap_sham_dev *dd, size_t length)
{
}

static int omap_sham_poll_irq_omap2(struct omap_sham_dev *dd)
{
	return omap_sham_wait(dd, SHA_REG_CTRL, SHA_REG_CTRL_INPUT_READY);
}

static int get_block_size(struct omap_sham_reqctx *ctx)
{
	int d;

	switch (ctx->flags & FLAGS_MODE_MASK) {
	case FLAGS_MODE_MD5:
	case FLAGS_MODE_SHA1:
		d = SHA1_BLOCK_SIZE;
		break;
	case FLAGS_MODE_SHA224:
	case FLAGS_MODE_SHA256:
		d = SHA256_BLOCK_SIZE;
		break;
	case FLAGS_MODE_SHA384:
	case FLAGS_MODE_SHA512:
		d = SHA512_BLOCK_SIZE;
		break;
	default:
		d = 0;
	}

	return d;
}

static void omap_sham_write_n(struct omap_sham_dev *dd, u32 offset,
				    u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		omap_sham_write(dd, offset, *value);
}

static void omap_sham_write_ctrl_omap4(struct omap_sham_dev *dd, size_t length,
				 int final, int dma)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	u32 val, mask;

	/*
	 * Setting ALGO_CONST only for the first iteration and
	 * CLOSE_HASH only for the last one. Note that flags mode bits
	 * correspond to algorithm encoding in mode register.
	 */
	val = (ctx->flags & FLAGS_MODE_MASK) >> (FLAGS_MODE_SHIFT);
	if (!ctx->digcnt) {
		struct crypto_ahash *tfm = crypto_ahash_reqtfm(dd->req);
		struct omap_sham_ctx *tctx = crypto_ahash_ctx(tfm);
		struct omap_sham_hmac_ctx *bctx = tctx->base;
		int bs, nr_dr;

		val |= SHA_REG_MODE_ALGO_CONSTANT;

		if (ctx->flags & BIT(FLAGS_HMAC)) {
			bs = get_block_size(ctx);
			nr_dr = bs / (2 * sizeof(u32));
			val |= SHA_REG_MODE_HMAC_KEY_PROC;
			omap_sham_write_n(dd, SHA_REG_ODIGEST(dd, 0),
					  (u32 *)bctx->ipad, nr_dr);
			omap_sham_write_n(dd, SHA_REG_IDIGEST(dd, 0),
					  (u32 *)bctx->ipad + nr_dr, nr_dr);
			ctx->digcnt += bs;
		}
	}

	if (final) {
		val |= SHA_REG_MODE_CLOSE_HASH;

		if (ctx->flags & BIT(FLAGS_HMAC))
			val |= SHA_REG_MODE_HMAC_OUTER_HASH;
	}

	mask = SHA_REG_MODE_ALGO_CONSTANT | SHA_REG_MODE_CLOSE_HASH |
	       SHA_REG_MODE_ALGO_MASK | SHA_REG_MODE_HMAC_OUTER_HASH |
	       SHA_REG_MODE_HMAC_KEY_PROC;

	dev_dbg(dd->dev, "ctrl: %08x, flags: %08lx\n", val, ctx->flags);
	omap_sham_write_mask(dd, SHA_REG_MODE(dd), val, mask);
	omap_sham_write(dd, SHA_REG_IRQENA, SHA_REG_IRQENA_OUTPUT_RDY);
	omap_sham_write_mask(dd, SHA_REG_MASK(dd),
			     SHA_REG_MASK_IT_EN |
				     (dma ? SHA_REG_MASK_DMA_EN : 0),
			     SHA_REG_MASK_IT_EN | SHA_REG_MASK_DMA_EN);
}

static void omap_sham_trigger_omap4(struct omap_sham_dev *dd, size_t length)
{
	omap_sham_write(dd, SHA_REG_LENGTH(dd), length);
}

static int omap_sham_poll_irq_omap4(struct omap_sham_dev *dd)
{
	return omap_sham_wait(dd, SHA_REG_IRQSTATUS,
			      SHA_REG_IRQSTATUS_INPUT_RDY);
}

static int omap_sham_xmit_cpu(struct omap_sham_dev *dd, const u8 *buf,
			      size_t length, int final)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	int count, len32, bs32, offset = 0;
	const u32 *buffer = (const u32 *)buf;

	dev_dbg(dd->dev, "xmit_cpu: digcnt: %d, length: %d, final: %d\n",
						ctx->digcnt, length, final);

	dd->pdata->write_ctrl(dd, length, final, 0);
	dd->pdata->trigger(dd, length);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt += length;

	if (final)
		set_bit(FLAGS_FINAL, &dd->flags); /* catch last interrupt */

	set_bit(FLAGS_CPU, &dd->flags);

	len32 = DIV_ROUND_UP(length, sizeof(u32));
	bs32 = get_block_size(ctx) / sizeof(u32);

	while (len32) {
		if (dd->pdata->poll_irq(dd))
			return -ETIMEDOUT;

		for (count = 0; count < min(len32, bs32); count++, offset++)
			omap_sham_write(dd, SHA_REG_DIN(dd, count),
					buffer[offset]);
		len32 -= min(len32, bs32);
	}

	return -EINPROGRESS;
}

static void omap_sham_dma_callback(void *param)
{
	struct omap_sham_dev *dd = param;

	set_bit(FLAGS_DMA_READY, &dd->flags);
	tasklet_schedule(&dd->done_task);
}

static int omap_sham_xmit_dma(struct omap_sham_dev *dd, dma_addr_t dma_addr,
			      size_t length, int final, int is_sg)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config cfg;
	int len32, ret, dma_min = get_block_size(ctx);

	dev_dbg(dd->dev, "xmit_dma: digcnt: %d, length: %d, final: %d\n",
						ctx->digcnt, length, final);

	memset(&cfg, 0, sizeof(cfg));

	cfg.dst_addr = dd->phys_base + SHA_REG_DIN(dd, 0);
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_maxburst = dma_min / DMA_SLAVE_BUSWIDTH_4_BYTES;

	ret = dmaengine_slave_config(dd->dma_lch, &cfg);
	if (ret) {
		pr_err("omap-sham: can't configure dmaengine slave: %d\n", ret);
		return ret;
	}

	len32 = DIV_ROUND_UP(length, dma_min) * dma_min;

	if (is_sg) {
		/*
		 * The SG entry passed in may not have the 'length' member
		 * set correctly so use a local SG entry (sgl) with the
		 * proper value for 'length' instead.  If this is not done,
		 * the dmaengine may try to DMA the incorrect amount of data.
		 */
		sg_init_table(&ctx->sgl, 1);
		sg_assign_page(&ctx->sgl, sg_page(ctx->sg));
		ctx->sgl.offset = ctx->sg->offset;
		sg_dma_len(&ctx->sgl) = len32;
		sg_dma_address(&ctx->sgl) = sg_dma_address(ctx->sg);

		tx = dmaengine_prep_slave_sg(dd->dma_lch, &ctx->sgl, 1,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	} else {
		tx = dmaengine_prep_slave_single(dd->dma_lch, dma_addr, len32,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (!tx) {
		dev_err(dd->dev, "prep_slave_sg/single() failed\n");
		return -EINVAL;
	}

	tx->callback = omap_sham_dma_callback;
	tx->callback_param = dd;

	dd->pdata->write_ctrl(dd, length, final, 1);

	ctx->digcnt += length;

	if (final)
		set_bit(FLAGS_FINAL, &dd->flags); /* catch last interrupt */

	set_bit(FLAGS_DMA_ACTIVE, &dd->flags);

	dmaengine_submit(tx);
	dma_async_issue_pending(dd->dma_lch);

	dd->pdata->trigger(dd, length);

	return -EINPROGRESS;
}

static size_t omap_sham_append_buffer(struct omap_sham_reqctx *ctx,
				const u8 *data, size_t length)
{
	size_t count = min(length, ctx->buflen - ctx->bufcnt);

	count = min(count, ctx->total);
	if (count <= 0)
		return 0;
	memcpy(ctx->buffer + ctx->bufcnt, data, count);
	ctx->bufcnt += count;

	return count;
}

static size_t omap_sham_append_sg(struct omap_sham_reqctx *ctx)
{
	size_t count;
	const u8 *vaddr;

	while (ctx->sg) {
		vaddr = kmap_atomic(sg_page(ctx->sg));
		vaddr += ctx->sg->offset;

		count = omap_sham_append_buffer(ctx,
				vaddr + ctx->offset,
				ctx->sg->length - ctx->offset);

		kunmap_atomic((void *)vaddr);

		if (!count)
			break;
		ctx->offset += count;
		ctx->total -= count;
		if (ctx->offset == ctx->sg->length) {
			ctx->sg = sg_next(ctx->sg);
			if (ctx->sg)
				ctx->offset = 0;
			else
				ctx->total = 0;
		}
	}

	return 0;
}

static int omap_sham_xmit_dma_map(struct omap_sham_dev *dd,
					struct omap_sham_reqctx *ctx,
					size_t length, int final)
{
	int ret;

	ctx->dma_addr = dma_map_single(dd->dev, ctx->buffer, ctx->buflen,
				       DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
		dev_err(dd->dev, "dma %u bytes error\n", ctx->buflen);
		return -EINVAL;
	}

	ctx->flags &= ~BIT(FLAGS_SG);

	ret = omap_sham_xmit_dma(dd, ctx->dma_addr, length, final, 0);
	if (ret != -EINPROGRESS)
		dma_unmap_single(dd->dev, ctx->dma_addr, ctx->buflen,
				 DMA_TO_DEVICE);

	return ret;
}

static int omap_sham_update_dma_slow(struct omap_sham_dev *dd)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int final;
	size_t count;

	omap_sham_append_sg(ctx);

	final = (ctx->flags & BIT(FLAGS_FINUP)) && !ctx->total;

	dev_dbg(dd->dev, "slow: bufcnt: %u, digcnt: %d, final: %d\n",
					 ctx->bufcnt, ctx->digcnt, final);

	if (final || (ctx->bufcnt == ctx->buflen && ctx->total)) {
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		return omap_sham_xmit_dma_map(dd, ctx, count, final);
	}

	return 0;
}

/* Start address alignment */
#define SG_AA(sg)	(IS_ALIGNED(sg->offset, sizeof(u32)))
/* SHA1 block size alignment */
#define SG_SA(sg, bs)	(IS_ALIGNED(sg->length, bs))

static int omap_sham_update_dma_start(struct omap_sham_dev *dd)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int length, final, tail;
	struct scatterlist *sg;
	int ret, bs;

	if (!ctx->total)
		return 0;

	if (ctx->bufcnt || ctx->offset)
		return omap_sham_update_dma_slow(dd);

	/*
	 * Don't use the sg interface when the transfer size is less
	 * than the number of elements in a DMA frame.  Otherwise,
	 * the dmaengine infrastructure will calculate that it needs
	 * to transfer 0 frames which ultimately fails.
	 */
	if (ctx->total < get_block_size(ctx))
		return omap_sham_update_dma_slow(dd);

	dev_dbg(dd->dev, "fast: digcnt: %d, bufcnt: %u, total: %u\n",
			ctx->digcnt, ctx->bufcnt, ctx->total);

	sg = ctx->sg;
	bs = get_block_size(ctx);

	if (!SG_AA(sg))
		return omap_sham_update_dma_slow(dd);

	if (!sg_is_last(sg) && !SG_SA(sg, bs))
		/* size is not BLOCK_SIZE aligned */
		return omap_sham_update_dma_slow(dd);

	length = min(ctx->total, sg->length);

	if (sg_is_last(sg)) {
		if (!(ctx->flags & BIT(FLAGS_FINUP))) {
			/* not last sg must be BLOCK_SIZE aligned */
			tail = length & (bs - 1);
			/* without finup() we need one block to close hash */
			if (!tail)
				tail = bs;
			length -= tail;
		}
	}

	if (!dma_map_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE)) {
		dev_err(dd->dev, "dma_map_sg  error\n");
		return -EINVAL;
	}

	ctx->flags |= BIT(FLAGS_SG);

	ctx->total -= length;
	ctx->offset = length; /* offset where to start slow */

	final = (ctx->flags & BIT(FLAGS_FINUP)) && !ctx->total;

	ret = omap_sham_xmit_dma(dd, sg_dma_address(ctx->sg), length, final, 1);
	if (ret != -EINPROGRESS)
		dma_unmap_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE);

	return ret;
}

static int omap_sham_update_cpu(struct omap_sham_dev *dd)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	int bufcnt, final;

	if (!ctx->total)
		return 0;

	omap_sham_append_sg(ctx);

	final = (ctx->flags & BIT(FLAGS_FINUP)) && !ctx->total;

	dev_dbg(dd->dev, "cpu: bufcnt: %u, digcnt: %d, final: %d\n",
		ctx->bufcnt, ctx->digcnt, final);

	if (final || (ctx->bufcnt == ctx->buflen && ctx->total)) {
		bufcnt = ctx->bufcnt;
		ctx->bufcnt = 0;
		return omap_sham_xmit_cpu(dd, ctx->buffer, bufcnt, final);
	}

	return 0;
}

static int omap_sham_update_dma_stop(struct omap_sham_dev *dd)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);

	dmaengine_terminate_all(dd->dma_lch);

	if (ctx->flags & BIT(FLAGS_SG)) {
		dma_unmap_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE);
		if (ctx->sg->length == ctx->offset) {
			ctx->sg = sg_next(ctx->sg);
			if (ctx->sg)
				ctx->offset = 0;
		}
	} else {
		dma_unmap_single(dd->dev, ctx->dma_addr, ctx->buflen,
				 DMA_TO_DEVICE);
	}

	return 0;
}

static int omap_sham_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct omap_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = NULL, *tmp;
	int bs = 0;

	spin_lock_bh(&sham.lock);
	if (!tctx->dd) {
		list_for_each_entry(tmp, &sham.dev_list, list) {
			dd = tmp;
			break;
		}
		tctx->dd = dd;
	} else {
		dd = tctx->dd;
	}
	spin_unlock_bh(&sham.lock);

	ctx->dd = dd;

	ctx->flags = 0;

	dev_dbg(dd->dev, "init: digest size: %d\n",
		crypto_ahash_digestsize(tfm));

	switch (crypto_ahash_digestsize(tfm)) {
	case MD5_DIGEST_SIZE:
		ctx->flags |= FLAGS_MODE_MD5;
		bs = SHA1_BLOCK_SIZE;
		break;
	case SHA1_DIGEST_SIZE:
		ctx->flags |= FLAGS_MODE_SHA1;
		bs = SHA1_BLOCK_SIZE;
		break;
	case SHA224_DIGEST_SIZE:
		ctx->flags |= FLAGS_MODE_SHA224;
		bs = SHA224_BLOCK_SIZE;
		break;
	case SHA256_DIGEST_SIZE:
		ctx->flags |= FLAGS_MODE_SHA256;
		bs = SHA256_BLOCK_SIZE;
		break;
	case SHA384_DIGEST_SIZE:
		ctx->flags |= FLAGS_MODE_SHA384;
		bs = SHA384_BLOCK_SIZE;
		break;
	case SHA512_DIGEST_SIZE:
		ctx->flags |= FLAGS_MODE_SHA512;
		bs = SHA512_BLOCK_SIZE;
		break;
	}

	ctx->bufcnt = 0;
	ctx->digcnt = 0;
	ctx->buflen = BUFLEN;

	if (tctx->flags & BIT(FLAGS_HMAC)) {
		if (!test_bit(FLAGS_AUTO_XOR, &dd->flags)) {
			struct omap_sham_hmac_ctx *bctx = tctx->base;

			memcpy(ctx->buffer, bctx->ipad, bs);
			ctx->bufcnt = bs;
		}

		ctx->flags |= BIT(FLAGS_HMAC);
	}

	return 0;

}

static int omap_sham_update_req(struct omap_sham_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	int err;

	dev_dbg(dd->dev, "update_req: total: %u, digcnt: %d, finup: %d\n",
		 ctx->total, ctx->digcnt, (ctx->flags & BIT(FLAGS_FINUP)) != 0);

	if (ctx->flags & BIT(FLAGS_CPU))
		err = omap_sham_update_cpu(dd);
	else
		err = omap_sham_update_dma_start(dd);

	/* wait for dma completion before can take more data */
	dev_dbg(dd->dev, "update: err: %d, digcnt: %d\n", err, ctx->digcnt);

	return err;
}

static int omap_sham_final_req(struct omap_sham_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	int err = 0, use_dma = 1;

	if ((ctx->bufcnt <= get_block_size(ctx)) || dd->polling_mode)
		/*
		 * faster to handle last block with cpu or
		 * use cpu when dma is not present.
		 */
		use_dma = 0;

	if (use_dma)
		err = omap_sham_xmit_dma_map(dd, ctx, ctx->bufcnt, 1);
	else
		err = omap_sham_xmit_cpu(dd, ctx->buffer, ctx->bufcnt, 1);

	ctx->bufcnt = 0;

	dev_dbg(dd->dev, "final_req: err: %d\n", err);

	return err;
}

static int omap_sham_finish_hmac(struct ahash_request *req)
{
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct omap_sham_hmac_ctx *bctx = tctx->base;
	int bs = crypto_shash_blocksize(bctx->shash);
	int ds = crypto_shash_digestsize(bctx->shash);
	SHASH_DESC_ON_STACK(shash, bctx->shash);

	shash->tfm = bctx->shash;
	shash->flags = 0; /* not CRYPTO_TFM_REQ_MAY_SLEEP */

	return crypto_shash_init(shash) ?:
	       crypto_shash_update(shash, bctx->opad, bs) ?:
	       crypto_shash_finup(shash, req->result, ds, req->result);
}

static int omap_sham_finish(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;
	int err = 0;

	if (ctx->digcnt) {
		omap_sham_copy_ready_hash(req);
		if ((ctx->flags & BIT(FLAGS_HMAC)) &&
				!test_bit(FLAGS_AUTO_XOR, &dd->flags))
			err = omap_sham_finish_hmac(req);
	}

	dev_dbg(dd->dev, "digcnt: %d, bufcnt: %d\n", ctx->digcnt, ctx->bufcnt);

	return err;
}

static void omap_sham_finish_req(struct ahash_request *req, int err)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;

	if (!err) {
		dd->pdata->copy_hash(req, 1);
		if (test_bit(FLAGS_FINAL, &dd->flags))
			err = omap_sham_finish(req);
	} else {
		ctx->flags |= BIT(FLAGS_ERROR);
	}

	/* atomic operation is not needed here */
	dd->flags &= ~(BIT(FLAGS_BUSY) | BIT(FLAGS_FINAL) | BIT(FLAGS_CPU) |
			BIT(FLAGS_DMA_READY) | BIT(FLAGS_OUTPUT_READY));

	pm_runtime_put(dd->dev);

	if (req->base.complete)
		req->base.complete(&req->base, err);

	/* handle new request */
	tasklet_schedule(&dd->done_task);
}

static int omap_sham_handle_queue(struct omap_sham_dev *dd,
				  struct ahash_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct omap_sham_reqctx *ctx;
	unsigned long flags;
	int err = 0, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = ahash_enqueue_request(&dd->queue, req);
	if (test_bit(FLAGS_BUSY, &dd->flags)) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (async_req)
		set_bit(FLAGS_BUSY, &dd->flags);
	spin_unlock_irqrestore(&dd->lock, flags);

	if (!async_req)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ahash_request_cast(async_req);
	dd->req = req;
	ctx = ahash_request_ctx(req);

	dev_dbg(dd->dev, "handling new req, op: %lu, nbytes: %d\n",
						ctx->op, req->nbytes);

	err = omap_sham_hw_init(dd);
	if (err)
		goto err1;

	if (ctx->digcnt)
		/* request has changed - restore hash */
		dd->pdata->copy_hash(req, 0);

	if (ctx->op == OP_UPDATE) {
		err = omap_sham_update_req(dd);
		if (err != -EINPROGRESS && (ctx->flags & BIT(FLAGS_FINUP)))
			/* no final() after finup() */
			err = omap_sham_final_req(dd);
	} else if (ctx->op == OP_FINAL) {
		err = omap_sham_final_req(dd);
	}
err1:
	if (err != -EINPROGRESS)
		/* done_task will not finish it, so do it here */
		omap_sham_finish_req(req, err);

	dev_dbg(dd->dev, "exit, err: %d\n", err);

	return ret;
}

static int omap_sham_enqueue(struct ahash_request *req, unsigned int op)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct omap_sham_dev *dd = tctx->dd;

	ctx->op = op;

	return omap_sham_handle_queue(dd, req);
}

static int omap_sham_update(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;
	int bs = get_block_size(ctx);

	if (!req->nbytes)
		return 0;

	ctx->total = req->nbytes;
	ctx->sg = req->src;
	ctx->offset = 0;

	if (ctx->flags & BIT(FLAGS_FINUP)) {
		if ((ctx->digcnt + ctx->bufcnt + ctx->total) < 9) {
			/*
			* OMAP HW accel works only with buffers >= 9
			* will switch to bypass in final()
			* final has the same request and data
			*/
			omap_sham_append_sg(ctx);
			return 0;
		} else if ((ctx->bufcnt + ctx->total <= bs) ||
			   dd->polling_mode) {
			/*
			 * faster to use CPU for short transfers or
			 * use cpu when dma is not present.
			 */
			ctx->flags |= BIT(FLAGS_CPU);
		}
	} else if (ctx->bufcnt + ctx->total < ctx->buflen) {
		omap_sham_append_sg(ctx);
		return 0;
	}

	if (dd->polling_mode)
		ctx->flags |= BIT(FLAGS_CPU);

	return omap_sham_enqueue(req, OP_UPDATE);
}

static int omap_sham_shash_digest(struct crypto_shash *tfm, u32 flags,
				  const u8 *data, unsigned int len, u8 *out)
{
	SHASH_DESC_ON_STACK(shash, tfm);

	shash->tfm = tfm;
	shash->flags = flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_shash_digest(shash, data, len, out);
}

static int omap_sham_final_shash(struct ahash_request *req)
{
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);

	return omap_sham_shash_digest(tctx->fallback, req->base.flags,
				      ctx->buffer, ctx->bufcnt, req->result);
}

static int omap_sham_final(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);

	ctx->flags |= BIT(FLAGS_FINUP);

	if (ctx->flags & BIT(FLAGS_ERROR))
		return 0; /* uncompleted hash is not needed */

	/* OMAP HW accel works only with buffers >= 9 */
	/* HMAC is always >= 9 because ipad == block size */
	if ((ctx->digcnt + ctx->bufcnt) < 9)
		return omap_sham_final_shash(req);
	else if (ctx->bufcnt)
		return omap_sham_enqueue(req, OP_FINAL);

	/* copy ready hash (+ finalize hmac) */
	return omap_sham_finish(req);
}

static int omap_sham_finup(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	int err1, err2;

	ctx->flags |= BIT(FLAGS_FINUP);

	err1 = omap_sham_update(req);
	if (err1 == -EINPROGRESS || err1 == -EBUSY)
		return err1;
	/*
	 * final() has to be always called to cleanup resources
	 * even if udpate() failed, except EINPROGRESS
	 */
	err2 = omap_sham_final(req);

	return err1 ?: err2;
}

static int omap_sham_digest(struct ahash_request *req)
{
	return omap_sham_init(req) ?: omap_sham_finup(req);
}

static int omap_sham_setkey(struct crypto_ahash *tfm, const u8 *key,
		      unsigned int keylen)
{
	struct omap_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct omap_sham_hmac_ctx *bctx = tctx->base;
	int bs = crypto_shash_blocksize(bctx->shash);
	int ds = crypto_shash_digestsize(bctx->shash);
	struct omap_sham_dev *dd = NULL, *tmp;
	int err, i;

	spin_lock_bh(&sham.lock);
	if (!tctx->dd) {
		list_for_each_entry(tmp, &sham.dev_list, list) {
			dd = tmp;
			break;
		}
		tctx->dd = dd;
	} else {
		dd = tctx->dd;
	}
	spin_unlock_bh(&sham.lock);

	err = crypto_shash_setkey(tctx->fallback, key, keylen);
	if (err)
		return err;

	if (keylen > bs) {
		err = omap_sham_shash_digest(bctx->shash,
				crypto_shash_get_flags(bctx->shash),
				key, keylen, bctx->ipad);
		if (err)
			return err;
		keylen = ds;
	} else {
		memcpy(bctx->ipad, key, keylen);
	}

	memset(bctx->ipad + keylen, 0, bs - keylen);

	if (!test_bit(FLAGS_AUTO_XOR, &dd->flags)) {
		memcpy(bctx->opad, bctx->ipad, bs);

		for (i = 0; i < bs; i++) {
			bctx->ipad[i] ^= 0x36;
			bctx->opad[i] ^= 0x5c;
		}
	}

	return err;
}

static int omap_sham_cra_init_alg(struct crypto_tfm *tfm, const char *alg_base)
{
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(tfm);

	/* Allocate a fallback and abort if it failed. */
	tctx->fallback = crypto_alloc_shash(alg_name, 0,
					    CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tctx->fallback)) {
		pr_err("omap-sham: fallback driver '%s' "
				"could not be loaded.\n", alg_name);
		return PTR_ERR(tctx->fallback);
	}

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct omap_sham_reqctx) + BUFLEN);

	if (alg_base) {
		struct omap_sham_hmac_ctx *bctx = tctx->base;
		tctx->flags |= BIT(FLAGS_HMAC);
		bctx->shash = crypto_alloc_shash(alg_base, 0,
						CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(bctx->shash)) {
			pr_err("omap-sham: base driver '%s' "
					"could not be loaded.\n", alg_base);
			crypto_free_shash(tctx->fallback);
			return PTR_ERR(bctx->shash);
		}

	}

	return 0;
}

static int omap_sham_cra_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, NULL);
}

static int omap_sham_cra_sha1_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, "sha1");
}

static int omap_sham_cra_sha224_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, "sha224");
}

static int omap_sham_cra_sha256_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, "sha256");
}

static int omap_sham_cra_md5_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, "md5");
}

static int omap_sham_cra_sha384_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, "sha384");
}

static int omap_sham_cra_sha512_init(struct crypto_tfm *tfm)
{
	return omap_sham_cra_init_alg(tfm, "sha512");
}

static void omap_sham_cra_exit(struct crypto_tfm *tfm)
{
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(tctx->fallback);
	tctx->fallback = NULL;

	if (tctx->flags & BIT(FLAGS_HMAC)) {
		struct omap_sham_hmac_ctx *bctx = tctx->base;
		crypto_free_shash(bctx->shash);
	}
}

static struct ahash_alg algs_sha1_md5[] = {
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.halg.digestsize	= SHA1_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha1",
		.cra_driver_name	= "omap-sha1",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.halg.digestsize	= MD5_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "md5",
		.cra_driver_name	= "omap-md5",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.setkey		= omap_sham_setkey,
	.halg.digestsize	= SHA1_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha1)",
		.cra_driver_name	= "omap-hmac-sha1",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx) +
					sizeof(struct omap_sham_hmac_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_sha1_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.setkey		= omap_sham_setkey,
	.halg.digestsize	= MD5_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(md5)",
		.cra_driver_name	= "omap-hmac-md5",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx) +
					sizeof(struct omap_sham_hmac_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_md5_init,
		.cra_exit		= omap_sham_cra_exit,
	}
}
};

/* OMAP4 has some algs in addition to what OMAP2 has */
static struct ahash_alg algs_sha224_sha256[] = {
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.halg.digestsize	= SHA224_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha224",
		.cra_driver_name	= "omap-sha224",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA224_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.halg.digestsize	= SHA256_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha256",
		.cra_driver_name	= "omap-sha256",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.setkey		= omap_sham_setkey,
	.halg.digestsize	= SHA224_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha224)",
		.cra_driver_name	= "omap-hmac-sha224",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA224_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx) +
					sizeof(struct omap_sham_hmac_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_sha224_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.setkey		= omap_sham_setkey,
	.halg.digestsize	= SHA256_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha256)",
		.cra_driver_name	= "omap-hmac-sha256",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx) +
					sizeof(struct omap_sham_hmac_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_sha256_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
};

static struct ahash_alg algs_sha384_sha512[] = {
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.halg.digestsize	= SHA384_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha384",
		.cra_driver_name	= "omap-sha384",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA384_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.halg.digestsize	= SHA512_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha512",
		.cra_driver_name	= "omap-sha512",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.setkey		= omap_sham_setkey,
	.halg.digestsize	= SHA384_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha384)",
		.cra_driver_name	= "omap-hmac-sha384",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA384_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx) +
					sizeof(struct omap_sham_hmac_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_sha384_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
{
	.init		= omap_sham_init,
	.update		= omap_sham_update,
	.final		= omap_sham_final,
	.finup		= omap_sham_finup,
	.digest		= omap_sham_digest,
	.setkey		= omap_sham_setkey,
	.halg.digestsize	= SHA512_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha512)",
		.cra_driver_name	= "omap-hmac-sha512",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct omap_sham_ctx) +
					sizeof(struct omap_sham_hmac_ctx),
		.cra_alignmask		= OMAP_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= omap_sham_cra_sha512_init,
		.cra_exit		= omap_sham_cra_exit,
	}
},
};

static void omap_sham_done_task(unsigned long data)
{
	struct omap_sham_dev *dd = (struct omap_sham_dev *)data;
	int err = 0;

	if (!test_bit(FLAGS_BUSY, &dd->flags)) {
		omap_sham_handle_queue(dd, NULL);
		return;
	}

	if (test_bit(FLAGS_CPU, &dd->flags)) {
		if (test_and_clear_bit(FLAGS_OUTPUT_READY, &dd->flags)) {
			/* hash or semi-hash ready */
			err = omap_sham_update_cpu(dd);
			if (err != -EINPROGRESS)
				goto finish;
		}
	} else if (test_bit(FLAGS_DMA_READY, &dd->flags)) {
		if (test_and_clear_bit(FLAGS_DMA_ACTIVE, &dd->flags)) {
			omap_sham_update_dma_stop(dd);
			if (dd->err) {
				err = dd->err;
				goto finish;
			}
		}
		if (test_and_clear_bit(FLAGS_OUTPUT_READY, &dd->flags)) {
			/* hash or semi-hash ready */
			clear_bit(FLAGS_DMA_READY, &dd->flags);
			err = omap_sham_update_dma_start(dd);
			if (err != -EINPROGRESS)
				goto finish;
		}
	}

	return;

finish:
	dev_dbg(dd->dev, "update done: err: %d\n", err);
	/* finish curent request */
	omap_sham_finish_req(dd->req, err);
}

static irqreturn_t omap_sham_irq_common(struct omap_sham_dev *dd)
{
	if (!test_bit(FLAGS_BUSY, &dd->flags)) {
		dev_warn(dd->dev, "Interrupt when no active requests.\n");
	} else {
		set_bit(FLAGS_OUTPUT_READY, &dd->flags);
		tasklet_schedule(&dd->done_task);
	}

	return IRQ_HANDLED;
}

static irqreturn_t omap_sham_irq_omap2(int irq, void *dev_id)
{
	struct omap_sham_dev *dd = dev_id;

	if (unlikely(test_bit(FLAGS_FINAL, &dd->flags)))
		/* final -> allow device to go to power-saving mode */
		omap_sham_write_mask(dd, SHA_REG_CTRL, 0, SHA_REG_CTRL_LENGTH);

	omap_sham_write_mask(dd, SHA_REG_CTRL, SHA_REG_CTRL_OUTPUT_READY,
				 SHA_REG_CTRL_OUTPUT_READY);
	omap_sham_read(dd, SHA_REG_CTRL);

	return omap_sham_irq_common(dd);
}

static irqreturn_t omap_sham_irq_omap4(int irq, void *dev_id)
{
	struct omap_sham_dev *dd = dev_id;

	omap_sham_write_mask(dd, SHA_REG_MASK(dd), 0, SHA_REG_MASK_IT_EN);

	return omap_sham_irq_common(dd);
}

static struct omap_sham_algs_info omap_sham_algs_info_omap2[] = {
	{
		.algs_list	= algs_sha1_md5,
		.size		= ARRAY_SIZE(algs_sha1_md5),
	},
};

static const struct omap_sham_pdata omap_sham_pdata_omap2 = {
	.algs_info	= omap_sham_algs_info_omap2,
	.algs_info_size	= ARRAY_SIZE(omap_sham_algs_info_omap2),
	.flags		= BIT(FLAGS_BE32_SHA1),
	.digest_size	= SHA1_DIGEST_SIZE,
	.copy_hash	= omap_sham_copy_hash_omap2,
	.write_ctrl	= omap_sham_write_ctrl_omap2,
	.trigger	= omap_sham_trigger_omap2,
	.poll_irq	= omap_sham_poll_irq_omap2,
	.intr_hdlr	= omap_sham_irq_omap2,
	.idigest_ofs	= 0x00,
	.din_ofs	= 0x1c,
	.digcnt_ofs	= 0x14,
	.rev_ofs	= 0x5c,
	.mask_ofs	= 0x60,
	.sysstatus_ofs	= 0x64,
	.major_mask	= 0xf0,
	.major_shift	= 4,
	.minor_mask	= 0x0f,
	.minor_shift	= 0,
};

#ifdef CONFIG_OF
static struct omap_sham_algs_info omap_sham_algs_info_omap4[] = {
	{
		.algs_list	= algs_sha1_md5,
		.size		= ARRAY_SIZE(algs_sha1_md5),
	},
	{
		.algs_list	= algs_sha224_sha256,
		.size		= ARRAY_SIZE(algs_sha224_sha256),
	},
};

static const struct omap_sham_pdata omap_sham_pdata_omap4 = {
	.algs_info	= omap_sham_algs_info_omap4,
	.algs_info_size	= ARRAY_SIZE(omap_sham_algs_info_omap4),
	.flags		= BIT(FLAGS_AUTO_XOR),
	.digest_size	= SHA256_DIGEST_SIZE,
	.copy_hash	= omap_sham_copy_hash_omap4,
	.write_ctrl	= omap_sham_write_ctrl_omap4,
	.trigger	= omap_sham_trigger_omap4,
	.poll_irq	= omap_sham_poll_irq_omap4,
	.intr_hdlr	= omap_sham_irq_omap4,
	.idigest_ofs	= 0x020,
	.odigest_ofs	= 0x0,
	.din_ofs	= 0x080,
	.digcnt_ofs	= 0x040,
	.rev_ofs	= 0x100,
	.mask_ofs	= 0x110,
	.sysstatus_ofs	= 0x114,
	.mode_ofs	= 0x44,
	.length_ofs	= 0x48,
	.major_mask	= 0x0700,
	.major_shift	= 8,
	.minor_mask	= 0x003f,
	.minor_shift	= 0,
};

static struct omap_sham_algs_info omap_sham_algs_info_omap5[] = {
	{
		.algs_list	= algs_sha1_md5,
		.size		= ARRAY_SIZE(algs_sha1_md5),
	},
	{
		.algs_list	= algs_sha224_sha256,
		.size		= ARRAY_SIZE(algs_sha224_sha256),
	},
	{
		.algs_list	= algs_sha384_sha512,
		.size		= ARRAY_SIZE(algs_sha384_sha512),
	},
};

static const struct omap_sham_pdata omap_sham_pdata_omap5 = {
	.algs_info	= omap_sham_algs_info_omap5,
	.algs_info_size	= ARRAY_SIZE(omap_sham_algs_info_omap5),
	.flags		= BIT(FLAGS_AUTO_XOR),
	.digest_size	= SHA512_DIGEST_SIZE,
	.copy_hash	= omap_sham_copy_hash_omap4,
	.write_ctrl	= omap_sham_write_ctrl_omap4,
	.trigger	= omap_sham_trigger_omap4,
	.poll_irq	= omap_sham_poll_irq_omap4,
	.intr_hdlr	= omap_sham_irq_omap4,
	.idigest_ofs	= 0x240,
	.odigest_ofs	= 0x200,
	.din_ofs	= 0x080,
	.digcnt_ofs	= 0x280,
	.rev_ofs	= 0x100,
	.mask_ofs	= 0x110,
	.sysstatus_ofs	= 0x114,
	.mode_ofs	= 0x284,
	.length_ofs	= 0x288,
	.major_mask	= 0x0700,
	.major_shift	= 8,
	.minor_mask	= 0x003f,
	.minor_shift	= 0,
};

static const struct of_device_id omap_sham_of_match[] = {
	{
		.compatible	= "ti,omap2-sham",
		.data		= &omap_sham_pdata_omap2,
	},
	{
		.compatible	= "ti,omap3-sham",
		.data		= &omap_sham_pdata_omap2,
	},
	{
		.compatible	= "ti,omap4-sham",
		.data		= &omap_sham_pdata_omap4,
	},
	{
		.compatible	= "ti,omap5-sham",
		.data		= &omap_sham_pdata_omap5,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_sham_of_match);

static int omap_sham_get_res_of(struct omap_sham_dev *dd,
		struct device *dev, struct resource *res)
{
	struct device_node *node = dev->of_node;
	const struct of_device_id *match;
	int err = 0;

	match = of_match_device(of_match_ptr(omap_sham_of_match), dev);
	if (!match) {
		dev_err(dev, "no compatible OF match\n");
		err = -EINVAL;
		goto err;
	}

	err = of_address_to_resource(node, 0, res);
	if (err < 0) {
		dev_err(dev, "can't translate OF node address\n");
		err = -EINVAL;
		goto err;
	}

	dd->irq = irq_of_parse_and_map(node, 0);
	if (!dd->irq) {
		dev_err(dev, "can't translate OF irq value\n");
		err = -EINVAL;
		goto err;
	}

	dd->pdata = match->data;

err:
	return err;
}
#else
static const struct of_device_id omap_sham_of_match[] = {
	{},
};

static int omap_sham_get_res_of(struct omap_sham_dev *dd,
		struct device *dev, struct resource *res)
{
	return -EINVAL;
}
#endif

static int omap_sham_get_res_pdev(struct omap_sham_dev *dd,
		struct platform_device *pdev, struct resource *res)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	int err = 0;

	/* Get the base address */
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto err;
	}
	memcpy(res, r, sizeof(*res));

	/* Get the IRQ */
	dd->irq = platform_get_irq(pdev, 0);
	if (dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = dd->irq;
		goto err;
	}

	/* Only OMAP2/3 can be non-DT */
	dd->pdata = &omap_sham_pdata_omap2;

err:
	return err;
}

static int omap_sham_probe(struct platform_device *pdev)
{
	struct omap_sham_dev *dd;
	struct device *dev = &pdev->dev;
	struct resource res;
	dma_cap_mask_t mask;
	int err, i, j;
	u32 rev;

	dd = devm_kzalloc(dev, sizeof(struct omap_sham_dev), GFP_KERNEL);
	if (dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto data_err;
	}
	dd->dev = dev;
	platform_set_drvdata(pdev, dd);

	INIT_LIST_HEAD(&dd->list);
	spin_lock_init(&dd->lock);
	tasklet_init(&dd->done_task, omap_sham_done_task, (unsigned long)dd);
	crypto_init_queue(&dd->queue, OMAP_SHAM_QUEUE_LENGTH);

	err = (dev->of_node) ? omap_sham_get_res_of(dd, dev, &res) :
			       omap_sham_get_res_pdev(dd, pdev, &res);
	if (err)
		goto data_err;

	dd->io_base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(dd->io_base)) {
		err = PTR_ERR(dd->io_base);
		goto data_err;
	}
	dd->phys_base = res.start;

	err = devm_request_irq(dev, dd->irq, dd->pdata->intr_hdlr,
			       IRQF_TRIGGER_NONE, dev_name(dev), dd);
	if (err) {
		dev_err(dev, "unable to request irq %d, err = %d\n",
			dd->irq, err);
		goto data_err;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	dd->dma_lch = dma_request_chan(dev, "rx");
	if (IS_ERR(dd->dma_lch)) {
		err = PTR_ERR(dd->dma_lch);
		if (err == -EPROBE_DEFER)
			goto data_err;

		dd->polling_mode = 1;
		dev_dbg(dev, "using polling mode instead of dma\n");
	}

	dd->flags |= dd->pdata->flags;

	pm_runtime_enable(dev);
	pm_runtime_irq_safe(dev);

	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		dev_err(dev, "failed to get sync: %d\n", err);
		goto err_pm;
	}

	rev = omap_sham_read(dd, SHA_REG_REV(dd));
	pm_runtime_put_sync(&pdev->dev);

	dev_info(dev, "hw accel on OMAP rev %u.%u\n",
		(rev & dd->pdata->major_mask) >> dd->pdata->major_shift,
		(rev & dd->pdata->minor_mask) >> dd->pdata->minor_shift);

	spin_lock(&sham.lock);
	list_add_tail(&dd->list, &sham.dev_list);
	spin_unlock(&sham.lock);

	for (i = 0; i < dd->pdata->algs_info_size; i++) {
		for (j = 0; j < dd->pdata->algs_info[i].size; j++) {
			err = crypto_register_ahash(
					&dd->pdata->algs_info[i].algs_list[j]);
			if (err)
				goto err_algs;

			dd->pdata->algs_info[i].registered++;
		}
	}

	return 0;

err_algs:
	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--)
			crypto_unregister_ahash(
					&dd->pdata->algs_info[i].algs_list[j]);
err_pm:
	pm_runtime_disable(dev);
	if (dd->polling_mode)
		dma_release_channel(dd->dma_lch);
data_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int omap_sham_remove(struct platform_device *pdev)
{
	static struct omap_sham_dev *dd;
	int i, j;

	dd = platform_get_drvdata(pdev);
	if (!dd)
		return -ENODEV;
	spin_lock(&sham.lock);
	list_del(&dd->list);
	spin_unlock(&sham.lock);
	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--)
			crypto_unregister_ahash(
					&dd->pdata->algs_info[i].algs_list[j]);
	tasklet_kill(&dd->done_task);
	pm_runtime_disable(&pdev->dev);

	if (!dd->polling_mode)
		dma_release_channel(dd->dma_lch);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int omap_sham_suspend(struct device *dev)
{
	pm_runtime_put_sync(dev);
	return 0;
}

static int omap_sham_resume(struct device *dev)
{
	int err = pm_runtime_get_sync(dev);
	if (err < 0) {
		dev_err(dev, "failed to get sync: %d\n", err);
		return err;
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(omap_sham_pm_ops, omap_sham_suspend, omap_sham_resume);

static struct platform_driver omap_sham_driver = {
	.probe	= omap_sham_probe,
	.remove	= omap_sham_remove,
	.driver	= {
		.name	= "omap-sham",
		.pm	= &omap_sham_pm_ops,
		.of_match_table	= omap_sham_of_match,
	},
};

module_platform_driver(omap_sham_driver);

MODULE_DESCRIPTION("OMAP SHA1/MD5 hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Dmitry Kasatkin");
MODULE_ALIAS("platform:omap-sham");
