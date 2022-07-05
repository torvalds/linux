// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Support for OMAP SHA1/MD5 HW acceleration.
 *
 * Copyright (c) 2010 Nokia Corporation
 * Author: Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 * Copyright (c) 2011 Texas Instruments Incorporated
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
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/hash.h>
#include <crypto/hmac.h>
#include <crypto/internal/hash.h>
#include <crypto/engine.h>

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

#define DEFAULT_AUTOSUSPEND_DELAY	1000

/* mostly device flags */
#define FLAGS_FINAL		1
#define FLAGS_DMA_ACTIVE	2
#define FLAGS_OUTPUT_READY	3
#define FLAGS_CPU		5
#define FLAGS_DMA_READY		6
#define FLAGS_AUTO_XOR		7
#define FLAGS_BE32_SHA1		8
#define FLAGS_SGS_COPIED	9
#define FLAGS_SGS_ALLOCED	10
#define FLAGS_HUGE		11

/* context flags */
#define FLAGS_FINUP		16

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

#define BUFLEN			SHA512_BLOCK_SIZE
#define OMAP_SHA_DMA_THRESHOLD	256

#define OMAP_SHA_MAX_DMA_LEN	(1024 * 2048)

struct omap_sham_dev;

struct omap_sham_reqctx {
	struct omap_sham_dev	*dd;
	unsigned long		flags;
	u8			op;

	u8			digest[SHA512_DIGEST_SIZE] OMAP_ALIGNED;
	size_t			digcnt;
	size_t			bufcnt;
	size_t			buflen;

	/* walk state */
	struct scatterlist	*sg;
	struct scatterlist	sgl[2];
	int			offset;	/* offset in current sg */
	int			sg_len;
	unsigned int		total;	/* total request */

	u8			buffer[] OMAP_ALIGNED;
};

struct omap_sham_hmac_ctx {
	struct crypto_shash	*shash;
	u8			ipad[SHA512_BLOCK_SIZE] OMAP_ALIGNED;
	u8			opad[SHA512_BLOCK_SIZE] OMAP_ALIGNED;
};

struct omap_sham_ctx {
	struct crypto_engine_ctx	enginectx;
	unsigned long		flags;

	/* fallback stuff */
	struct crypto_shash	*fallback;

	struct omap_sham_hmac_ctx base[];
};

#define OMAP_SHAM_QUEUE_LENGTH	10

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
	int			err;
	struct dma_chan		*dma_lch;
	struct tasklet_struct	done_task;
	u8			polling_mode;
	u8			xmit_buf[BUFLEN] OMAP_ALIGNED;

	unsigned long		flags;
	int			fallback_sz;
	struct crypto_queue	queue;
	struct ahash_request	*req;
	struct crypto_engine	*engine;

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

static int omap_sham_enqueue(struct ahash_request *req, unsigned int op);
static void omap_sham_finish_req(struct ahash_request *req, int err);

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
			hash[i] = be32_to_cpup((__be32 *)in + i);
	else
		for (i = 0; i < d; i++)
			hash[i] = le32_to_cpup((__le32 *)in + i);
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

	if (likely(ctx->digcnt))
		omap_sham_write(dd, SHA_REG_DIGCNT(dd), ctx->digcnt);

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

static int omap_sham_xmit_cpu(struct omap_sham_dev *dd, size_t length,
			      int final)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	int count, len32, bs32, offset = 0;
	const u32 *buffer;
	int mlen;
	struct sg_mapping_iter mi;

	dev_dbg(dd->dev, "xmit_cpu: digcnt: %zd, length: %zd, final: %d\n",
						ctx->digcnt, length, final);

	dd->pdata->write_ctrl(dd, length, final, 0);
	dd->pdata->trigger(dd, length);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt += length;
	ctx->total -= length;

	if (final)
		set_bit(FLAGS_FINAL, &dd->flags); /* catch last interrupt */

	set_bit(FLAGS_CPU, &dd->flags);

	len32 = DIV_ROUND_UP(length, sizeof(u32));
	bs32 = get_block_size(ctx) / sizeof(u32);

	sg_miter_start(&mi, ctx->sg, ctx->sg_len,
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);

	mlen = 0;

	while (len32) {
		if (dd->pdata->poll_irq(dd))
			return -ETIMEDOUT;

		for (count = 0; count < min(len32, bs32); count++, offset++) {
			if (!mlen) {
				sg_miter_next(&mi);
				mlen = mi.length;
				if (!mlen) {
					pr_err("sg miter failure.\n");
					return -EINVAL;
				}
				offset = 0;
				buffer = mi.addr;
			}
			omap_sham_write(dd, SHA_REG_DIN(dd, count),
					buffer[offset]);
			mlen -= 4;
		}
		len32 -= min(len32, bs32);
	}

	sg_miter_stop(&mi);

	return -EINPROGRESS;
}

static void omap_sham_dma_callback(void *param)
{
	struct omap_sham_dev *dd = param;

	set_bit(FLAGS_DMA_READY, &dd->flags);
	tasklet_schedule(&dd->done_task);
}

static int omap_sham_xmit_dma(struct omap_sham_dev *dd, size_t length,
			      int final)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config cfg;
	int ret;

	dev_dbg(dd->dev, "xmit_dma: digcnt: %zd, length: %zd, final: %d\n",
						ctx->digcnt, length, final);

	if (!dma_map_sg(dd->dev, ctx->sg, ctx->sg_len, DMA_TO_DEVICE)) {
		dev_err(dd->dev, "dma_map_sg error\n");
		return -EINVAL;
	}

	memset(&cfg, 0, sizeof(cfg));

	cfg.dst_addr = dd->phys_base + SHA_REG_DIN(dd, 0);
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_maxburst = get_block_size(ctx) / DMA_SLAVE_BUSWIDTH_4_BYTES;

	ret = dmaengine_slave_config(dd->dma_lch, &cfg);
	if (ret) {
		pr_err("omap-sham: can't configure dmaengine slave: %d\n", ret);
		return ret;
	}

	tx = dmaengine_prep_slave_sg(dd->dma_lch, ctx->sg, ctx->sg_len,
				     DMA_MEM_TO_DEV,
				     DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!tx) {
		dev_err(dd->dev, "prep_slave_sg failed\n");
		return -EINVAL;
	}

	tx->callback = omap_sham_dma_callback;
	tx->callback_param = dd;

	dd->pdata->write_ctrl(dd, length, final, 1);

	ctx->digcnt += length;
	ctx->total -= length;

	if (final)
		set_bit(FLAGS_FINAL, &dd->flags); /* catch last interrupt */

	set_bit(FLAGS_DMA_ACTIVE, &dd->flags);

	dmaengine_submit(tx);
	dma_async_issue_pending(dd->dma_lch);

	dd->pdata->trigger(dd, length);

	return -EINPROGRESS;
}

static int omap_sham_copy_sg_lists(struct omap_sham_reqctx *ctx,
				   struct scatterlist *sg, int bs, int new_len)
{
	int n = sg_nents(sg);
	struct scatterlist *tmp;
	int offset = ctx->offset;

	ctx->total = new_len;

	if (ctx->bufcnt)
		n++;

	ctx->sg = kmalloc_array(n, sizeof(*sg), GFP_KERNEL);
	if (!ctx->sg)
		return -ENOMEM;

	sg_init_table(ctx->sg, n);

	tmp = ctx->sg;

	ctx->sg_len = 0;

	if (ctx->bufcnt) {
		sg_set_buf(tmp, ctx->dd->xmit_buf, ctx->bufcnt);
		tmp = sg_next(tmp);
		ctx->sg_len++;
		new_len -= ctx->bufcnt;
	}

	while (sg && new_len) {
		int len = sg->length - offset;

		if (len <= 0) {
			offset -= sg->length;
			sg = sg_next(sg);
			continue;
		}

		if (new_len < len)
			len = new_len;

		if (len > 0) {
			new_len -= len;
			sg_set_page(tmp, sg_page(sg), len, sg->offset + offset);
			offset = 0;
			ctx->offset = 0;
			ctx->sg_len++;
			if (new_len <= 0)
				break;
			tmp = sg_next(tmp);
		}

		sg = sg_next(sg);
	}

	if (tmp)
		sg_mark_end(tmp);

	set_bit(FLAGS_SGS_ALLOCED, &ctx->dd->flags);

	ctx->offset += new_len - ctx->bufcnt;
	ctx->bufcnt = 0;

	return 0;
}

static int omap_sham_copy_sgs(struct omap_sham_reqctx *ctx,
			      struct scatterlist *sg, int bs,
			      unsigned int new_len)
{
	int pages;
	void *buf;

	pages = get_order(new_len);

	buf = (void *)__get_free_pages(GFP_ATOMIC, pages);
	if (!buf) {
		pr_err("Couldn't allocate pages for unaligned cases.\n");
		return -ENOMEM;
	}

	if (ctx->bufcnt)
		memcpy(buf, ctx->dd->xmit_buf, ctx->bufcnt);

	scatterwalk_map_and_copy(buf + ctx->bufcnt, sg, ctx->offset,
				 min(new_len, ctx->total) - ctx->bufcnt, 0);
	sg_init_table(ctx->sgl, 1);
	sg_set_buf(ctx->sgl, buf, new_len);
	ctx->sg = ctx->sgl;
	set_bit(FLAGS_SGS_COPIED, &ctx->dd->flags);
	ctx->sg_len = 1;
	ctx->offset += new_len - ctx->bufcnt;
	ctx->bufcnt = 0;
	ctx->total = new_len;

	return 0;
}

static int omap_sham_align_sgs(struct scatterlist *sg,
			       int nbytes, int bs, bool final,
			       struct omap_sham_reqctx *rctx)
{
	int n = 0;
	bool aligned = true;
	bool list_ok = true;
	struct scatterlist *sg_tmp = sg;
	int new_len;
	int offset = rctx->offset;
	int bufcnt = rctx->bufcnt;

	if (!sg || !sg->length || !nbytes) {
		if (bufcnt) {
			bufcnt = DIV_ROUND_UP(bufcnt, bs) * bs;
			sg_init_table(rctx->sgl, 1);
			sg_set_buf(rctx->sgl, rctx->dd->xmit_buf, bufcnt);
			rctx->sg = rctx->sgl;
			rctx->sg_len = 1;
		}

		return 0;
	}

	new_len = nbytes;

	if (offset)
		list_ok = false;

	if (final)
		new_len = DIV_ROUND_UP(new_len, bs) * bs;
	else
		new_len = (new_len - 1) / bs * bs;

	if (!new_len)
		return 0;

	if (nbytes != new_len)
		list_ok = false;

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
				list_ok = false;

			continue;
		}

#ifdef CONFIG_ZONE_DMA
		if (page_zonenum(sg_page(sg_tmp)) != ZONE_DMA) {
			aligned = false;
			break;
		}
#endif

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
			list_ok = false;
			break;
		}
	}

	if (new_len > OMAP_SHA_MAX_DMA_LEN) {
		new_len = OMAP_SHA_MAX_DMA_LEN;
		aligned = false;
	}

	if (!aligned)
		return omap_sham_copy_sgs(rctx, sg, bs, new_len);
	else if (!list_ok)
		return omap_sham_copy_sg_lists(rctx, sg, bs, new_len);

	rctx->total = new_len;
	rctx->offset += new_len;
	rctx->sg_len = n;
	if (rctx->bufcnt) {
		sg_init_table(rctx->sgl, 2);
		sg_set_buf(rctx->sgl, rctx->dd->xmit_buf, rctx->bufcnt);
		sg_chain(rctx->sgl, 2, sg);
		rctx->sg = rctx->sgl;
	} else {
		rctx->sg = sg;
	}

	return 0;
}

static int omap_sham_prepare_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct omap_sham_reqctx *rctx = ahash_request_ctx(req);
	int bs;
	int ret;
	unsigned int nbytes;
	bool final = rctx->flags & BIT(FLAGS_FINUP);
	bool update = rctx->op == OP_UPDATE;
	int hash_later;

	bs = get_block_size(rctx);

	nbytes = rctx->bufcnt;

	if (update)
		nbytes += req->nbytes - rctx->offset;

	dev_dbg(rctx->dd->dev,
		"%s: nbytes=%d, bs=%d, total=%d, offset=%d, bufcnt=%zd\n",
		__func__, nbytes, bs, rctx->total, rctx->offset,
		rctx->bufcnt);

	if (!nbytes)
		return 0;

	rctx->total = nbytes;

	if (update && req->nbytes && (!IS_ALIGNED(rctx->bufcnt, bs))) {
		int len = bs - rctx->bufcnt % bs;

		if (len > req->nbytes)
			len = req->nbytes;
		scatterwalk_map_and_copy(rctx->buffer + rctx->bufcnt, req->src,
					 0, len, 0);
		rctx->bufcnt += len;
		rctx->offset = len;
	}

	if (rctx->bufcnt)
		memcpy(rctx->dd->xmit_buf, rctx->buffer, rctx->bufcnt);

	ret = omap_sham_align_sgs(req->src, nbytes, bs, final, rctx);
	if (ret)
		return ret;

	hash_later = nbytes - rctx->total;
	if (hash_later < 0)
		hash_later = 0;

	if (hash_later && hash_later <= rctx->buflen) {
		scatterwalk_map_and_copy(rctx->buffer,
					 req->src,
					 req->nbytes - hash_later,
					 hash_later, 0);

		rctx->bufcnt = hash_later;
	} else {
		rctx->bufcnt = 0;
	}

	if (hash_later > rctx->buflen)
		set_bit(FLAGS_HUGE, &rctx->dd->flags);

	rctx->total = min(nbytes, rctx->total);

	return 0;
}

static int omap_sham_update_dma_stop(struct omap_sham_dev *dd)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(dd->req);

	dma_unmap_sg(dd->dev, ctx->sg, ctx->sg_len, DMA_TO_DEVICE);

	clear_bit(FLAGS_DMA_ACTIVE, &dd->flags);

	return 0;
}

static struct omap_sham_dev *omap_sham_find_dev(struct omap_sham_reqctx *ctx)
{
	struct omap_sham_dev *dd;

	if (ctx->dd)
		return ctx->dd;

	spin_lock_bh(&sham.lock);
	dd = list_first_entry(&sham.dev_list, struct omap_sham_dev, list);
	list_move_tail(&dd->list, &sham.dev_list);
	ctx->dd = dd;
	spin_unlock_bh(&sham.lock);

	return dd;
}

static int omap_sham_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct omap_sham_ctx *tctx = crypto_ahash_ctx(tfm);
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd;
	int bs = 0;

	ctx->dd = NULL;

	dd = omap_sham_find_dev(ctx);
	if (!dd)
		return -ENODEV;

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
	ctx->total = 0;
	ctx->offset = 0;
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
	bool final = (ctx->flags & BIT(FLAGS_FINUP)) &&
		!(dd->flags & BIT(FLAGS_HUGE));

	dev_dbg(dd->dev, "update_req: total: %u, digcnt: %zd, final: %d",
		ctx->total, ctx->digcnt, final);

	if (ctx->total < get_block_size(ctx) ||
	    ctx->total < dd->fallback_sz)
		ctx->flags |= BIT(FLAGS_CPU);

	if (ctx->flags & BIT(FLAGS_CPU))
		err = omap_sham_xmit_cpu(dd, ctx->total, final);
	else
		err = omap_sham_xmit_dma(dd, ctx->total, final);

	/* wait for dma completion before can take more data */
	dev_dbg(dd->dev, "update: err: %d, digcnt: %zd\n", err, ctx->digcnt);

	return err;
}

static int omap_sham_final_req(struct omap_sham_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	int err = 0, use_dma = 1;

	if (dd->flags & BIT(FLAGS_HUGE))
		return 0;

	if ((ctx->total <= get_block_size(ctx)) || dd->polling_mode)
		/*
		 * faster to handle last block with cpu or
		 * use cpu when dma is not present.
		 */
		use_dma = 0;

	if (use_dma)
		err = omap_sham_xmit_dma(dd, ctx->total, 1);
	else
		err = omap_sham_xmit_cpu(dd, ctx->total, 1);

	ctx->bufcnt = 0;

	dev_dbg(dd->dev, "final_req: err: %d\n", err);

	return err;
}

static int omap_sham_hash_one_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;
	int err;
	bool final = (ctx->flags & BIT(FLAGS_FINUP)) &&
			!(dd->flags & BIT(FLAGS_HUGE));

	dev_dbg(dd->dev, "hash-one: op: %u, total: %u, digcnt: %zd, final: %d",
		ctx->op, ctx->total, ctx->digcnt, final);

	err = pm_runtime_resume_and_get(dd->dev);
	if (err < 0) {
		dev_err(dd->dev, "failed to get sync: %d\n", err);
		return err;
	}

	dd->err = 0;
	dd->req = req;

	if (ctx->digcnt)
		dd->pdata->copy_hash(req, 0);

	if (ctx->op == OP_UPDATE)
		err = omap_sham_update_req(dd);
	else if (ctx->op == OP_FINAL)
		err = omap_sham_final_req(dd);

	if (err != -EINPROGRESS)
		omap_sham_finish_req(req, err);

	return 0;
}

static int omap_sham_finish_hmac(struct ahash_request *req)
{
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct omap_sham_hmac_ctx *bctx = tctx->base;
	int bs = crypto_shash_blocksize(bctx->shash);
	int ds = crypto_shash_digestsize(bctx->shash);
	SHASH_DESC_ON_STACK(shash, bctx->shash);

	shash->tfm = bctx->shash;

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

	dev_dbg(dd->dev, "digcnt: %zd, bufcnt: %zd\n", ctx->digcnt, ctx->bufcnt);

	return err;
}

static void omap_sham_finish_req(struct ahash_request *req, int err)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;

	if (test_bit(FLAGS_SGS_COPIED, &dd->flags))
		free_pages((unsigned long)sg_virt(ctx->sg),
			   get_order(ctx->sg->length));

	if (test_bit(FLAGS_SGS_ALLOCED, &dd->flags))
		kfree(ctx->sg);

	ctx->sg = NULL;

	dd->flags &= ~(BIT(FLAGS_SGS_ALLOCED) | BIT(FLAGS_SGS_COPIED) |
		       BIT(FLAGS_CPU) | BIT(FLAGS_DMA_READY) |
		       BIT(FLAGS_OUTPUT_READY));

	if (!err)
		dd->pdata->copy_hash(req, 1);

	if (dd->flags & BIT(FLAGS_HUGE)) {
		/* Re-enqueue the request */
		omap_sham_enqueue(req, ctx->op);
		return;
	}

	if (!err) {
		if (test_bit(FLAGS_FINAL, &dd->flags))
			err = omap_sham_finish(req);
	} else {
		ctx->flags |= BIT(FLAGS_ERROR);
	}

	/* atomic operation is not needed here */
	dd->flags &= ~(BIT(FLAGS_FINAL) | BIT(FLAGS_CPU) |
			BIT(FLAGS_DMA_READY) | BIT(FLAGS_OUTPUT_READY));

	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);

	ctx->offset = 0;

	crypto_finalize_hash_request(dd->engine, req, err);
}

static int omap_sham_handle_queue(struct omap_sham_dev *dd,
				  struct ahash_request *req)
{
	return crypto_transfer_hash_request_to_engine(dd->engine, req);
}

static int omap_sham_enqueue(struct ahash_request *req, unsigned int op)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = ctx->dd;

	ctx->op = op;

	return omap_sham_handle_queue(dd, req);
}

static int omap_sham_update(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	struct omap_sham_dev *dd = omap_sham_find_dev(ctx);

	if (!req->nbytes)
		return 0;

	if (ctx->bufcnt + req->nbytes <= ctx->buflen) {
		scatterwalk_map_and_copy(ctx->buffer + ctx->bufcnt, req->src,
					 0, req->nbytes, 0);
		ctx->bufcnt += req->nbytes;
		return 0;
	}

	if (dd->polling_mode)
		ctx->flags |= BIT(FLAGS_CPU);

	return omap_sham_enqueue(req, OP_UPDATE);
}

static int omap_sham_final_shash(struct ahash_request *req)
{
	struct omap_sham_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);
	int offset = 0;

	/*
	 * If we are running HMAC on limited hardware support, skip
	 * the ipad in the beginning of the buffer if we are going for
	 * software fallback algorithm.
	 */
	if (test_bit(FLAGS_HMAC, &ctx->flags) &&
	    !test_bit(FLAGS_AUTO_XOR, &ctx->dd->flags))
		offset = get_block_size(ctx);

	return crypto_shash_tfm_digest(tctx->fallback, ctx->buffer + offset,
				       ctx->bufcnt - offset, req->result);
}

static int omap_sham_final(struct ahash_request *req)
{
	struct omap_sham_reqctx *ctx = ahash_request_ctx(req);

	ctx->flags |= BIT(FLAGS_FINUP);

	if (ctx->flags & BIT(FLAGS_ERROR))
		return 0; /* uncompleted hash is not needed */

	/*
	 * OMAP HW accel works only with buffers >= 9.
	 * HMAC is always >= 9 because ipad == block size.
	 * If buffersize is less than fallback_sz, we use fallback
	 * SW encoding, as using DMA + HW in this case doesn't provide
	 * any benefit.
	 */
	if (!ctx->digcnt && ctx->bufcnt < ctx->dd->fallback_sz)
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
	int err, i;

	err = crypto_shash_setkey(tctx->fallback, key, keylen);
	if (err)
		return err;

	if (keylen > bs) {
		err = crypto_shash_tfm_digest(bctx->shash, key, keylen,
					      bctx->ipad);
		if (err)
			return err;
		keylen = ds;
	} else {
		memcpy(bctx->ipad, key, keylen);
	}

	memset(bctx->ipad + keylen, 0, bs - keylen);

	if (!test_bit(FLAGS_AUTO_XOR, &sham.flags)) {
		memcpy(bctx->opad, bctx->ipad, bs);

		for (i = 0; i < bs; i++) {
			bctx->ipad[i] ^= HMAC_IPAD_VALUE;
			bctx->opad[i] ^= HMAC_OPAD_VALUE;
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

	tctx->enginectx.op.do_one_request = omap_sham_hash_one_req;
	tctx->enginectx.op.prepare_request = omap_sham_prepare_request;
	tctx->enginectx.op.unprepare_request = NULL;

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

static int omap_sham_export(struct ahash_request *req, void *out)
{
	struct omap_sham_reqctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx) + rctx->bufcnt);

	return 0;
}

static int omap_sham_import(struct ahash_request *req, const void *in)
{
	struct omap_sham_reqctx *rctx = ahash_request_ctx(req);
	const struct omap_sham_reqctx *ctx_in = in;

	memcpy(rctx, in, sizeof(*rctx) + ctx_in->bufcnt);

	return 0;
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
	.halg.digestsize	= MD5_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "md5",
		.cra_driver_name	= "omap-md5",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA224_BLOCK_SIZE,
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
	.halg.digestsize	= SHA256_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha256",
		.cra_driver_name	= "omap-sha256",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
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
	.halg.digestsize	= SHA224_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha224)",
		.cra_driver_name	= "omap-hmac-sha224",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA384_BLOCK_SIZE,
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
	.halg.digestsize	= SHA512_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha512",
		.cra_driver_name	= "omap-sha512",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
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
	.halg.digestsize	= SHA384_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "hmac(sha384)",
		.cra_driver_name	= "omap-hmac-sha384",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
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

	dev_dbg(dd->dev, "%s: flags=%lx\n", __func__, dd->flags);

	if (test_bit(FLAGS_CPU, &dd->flags)) {
		if (test_and_clear_bit(FLAGS_OUTPUT_READY, &dd->flags))
			goto finish;
	} else if (test_bit(FLAGS_DMA_READY, &dd->flags)) {
		if (test_bit(FLAGS_DMA_ACTIVE, &dd->flags)) {
			omap_sham_update_dma_stop(dd);
			if (dd->err) {
				err = dd->err;
				goto finish;
			}
		}
		if (test_and_clear_bit(FLAGS_OUTPUT_READY, &dd->flags)) {
			/* hash or semi-hash ready */
			clear_bit(FLAGS_DMA_READY, &dd->flags);
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
	set_bit(FLAGS_OUTPUT_READY, &dd->flags);
	tasklet_schedule(&dd->done_task);

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
	int err = 0;

	dd->pdata = of_device_get_match_data(dev);
	if (!dd->pdata) {
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
		err = dd->irq;
		goto err;
	}

	/* Only OMAP2/3 can be non-DT */
	dd->pdata = &omap_sham_pdata_omap2;

err:
	return err;
}

static ssize_t fallback_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct omap_sham_dev *dd = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", dd->fallback_sz);
}

static ssize_t fallback_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct omap_sham_dev *dd = dev_get_drvdata(dev);
	ssize_t status;
	long value;

	status = kstrtol(buf, 0, &value);
	if (status)
		return status;

	/* HW accelerator only works with buffers > 9 */
	if (value < 9) {
		dev_err(dev, "minimum fallback size 9\n");
		return -EINVAL;
	}

	dd->fallback_sz = value;

	return size;
}

static ssize_t queue_len_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct omap_sham_dev *dd = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", dd->queue.max_qlen);
}

static ssize_t queue_len_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t size)
{
	struct omap_sham_dev *dd = dev_get_drvdata(dev);
	ssize_t status;
	long value;

	status = kstrtol(buf, 0, &value);
	if (status)
		return status;

	if (value < 1)
		return -EINVAL;

	/*
	 * Changing the queue size in fly is safe, if size becomes smaller
	 * than current size, it will just not accept new entries until
	 * it has shrank enough.
	 */
	dd->queue.max_qlen = value;

	return size;
}

static DEVICE_ATTR_RW(queue_len);
static DEVICE_ATTR_RW(fallback);

static struct attribute *omap_sham_attrs[] = {
	&dev_attr_queue_len.attr,
	&dev_attr_fallback.attr,
	NULL,
};

static const struct attribute_group omap_sham_attr_group = {
	.attrs = omap_sham_attrs,
};

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
	sham.flags |= dd->pdata->flags;

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, DEFAULT_AUTOSUSPEND_DELAY);

	dd->fallback_sz = OMAP_SHA_DMA_THRESHOLD;

	pm_runtime_enable(dev);

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

	spin_lock_bh(&sham.lock);
	list_add_tail(&dd->list, &sham.dev_list);
	spin_unlock_bh(&sham.lock);

	dd->engine = crypto_engine_alloc_init(dev, 1);
	if (!dd->engine) {
		err = -ENOMEM;
		goto err_engine;
	}

	err = crypto_engine_start(dd->engine);
	if (err)
		goto err_engine_start;

	for (i = 0; i < dd->pdata->algs_info_size; i++) {
		if (dd->pdata->algs_info[i].registered)
			break;

		for (j = 0; j < dd->pdata->algs_info[i].size; j++) {
			struct ahash_alg *alg;

			alg = &dd->pdata->algs_info[i].algs_list[j];
			alg->export = omap_sham_export;
			alg->import = omap_sham_import;
			alg->halg.statesize = sizeof(struct omap_sham_reqctx) +
					      BUFLEN;
			err = crypto_register_ahash(alg);
			if (err)
				goto err_algs;

			dd->pdata->algs_info[i].registered++;
		}
	}

	err = sysfs_create_group(&dev->kobj, &omap_sham_attr_group);
	if (err) {
		dev_err(dev, "could not create sysfs device attrs\n");
		goto err_algs;
	}

	return 0;

err_algs:
	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--)
			crypto_unregister_ahash(
					&dd->pdata->algs_info[i].algs_list[j]);
err_engine_start:
	crypto_engine_exit(dd->engine);
err_engine:
	spin_lock_bh(&sham.lock);
	list_del(&dd->list);
	spin_unlock_bh(&sham.lock);
err_pm:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	if (!dd->polling_mode)
		dma_release_channel(dd->dma_lch);
data_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int omap_sham_remove(struct platform_device *pdev)
{
	struct omap_sham_dev *dd;
	int i, j;

	dd = platform_get_drvdata(pdev);

	spin_lock_bh(&sham.lock);
	list_del(&dd->list);
	spin_unlock_bh(&sham.lock);
	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--) {
			crypto_unregister_ahash(
					&dd->pdata->algs_info[i].algs_list[j]);
			dd->pdata->algs_info[i].registered--;
		}
	tasklet_kill(&dd->done_task);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if (!dd->polling_mode)
		dma_release_channel(dd->dma_lch);

	sysfs_remove_group(&dd->dev->kobj, &omap_sham_attr_group);

	return 0;
}

static struct platform_driver omap_sham_driver = {
	.probe	= omap_sham_probe,
	.remove	= omap_sham_remove,
	.driver	= {
		.name	= "omap-sham",
		.of_match_table	= omap_sham_of_match,
	},
};

module_platform_driver(omap_sham_driver);

MODULE_DESCRIPTION("OMAP SHA1/MD5 hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Dmitry Kasatkin");
MODULE_ALIAS("platform:omap-sham");
