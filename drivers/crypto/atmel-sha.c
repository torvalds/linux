// SPDX-License-Identifier: GPL-2.0
/*
 * Cryptographic API.
 *
 * Support for ATMEL SHA1/SHA256 HW acceleration.
 *
 * Copyright (c) 2012 Eukréa Electromatique - ATMEL
 * Author: Nicolas Royer <nicolas@eukrea.com>
 *
 * Some ideas are from omap-sham.c drivers.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>

#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include "atmel-sha-regs.h"
#include "atmel-authenc.h"

#define ATMEL_SHA_PRIORITY	300

/* SHA flags */
#define SHA_FLAGS_BUSY			BIT(0)
#define	SHA_FLAGS_FINAL			BIT(1)
#define SHA_FLAGS_DMA_ACTIVE	BIT(2)
#define SHA_FLAGS_OUTPUT_READY	BIT(3)
#define SHA_FLAGS_INIT			BIT(4)
#define SHA_FLAGS_CPU			BIT(5)
#define SHA_FLAGS_DMA_READY		BIT(6)
#define SHA_FLAGS_DUMP_REG	BIT(7)

/* bits[11:8] are reserved. */

#define SHA_FLAGS_FINUP		BIT(16)
#define SHA_FLAGS_SG		BIT(17)
#define SHA_FLAGS_ERROR		BIT(23)
#define SHA_FLAGS_PAD		BIT(24)
#define SHA_FLAGS_RESTORE	BIT(25)
#define SHA_FLAGS_IDATAR0	BIT(26)
#define SHA_FLAGS_WAIT_DATARDY	BIT(27)

#define SHA_OP_INIT	0
#define SHA_OP_UPDATE	1
#define SHA_OP_FINAL	2
#define SHA_OP_DIGEST	3

#define SHA_BUFFER_LEN		(PAGE_SIZE / 16)

#define ATMEL_SHA_DMA_THRESHOLD		56

struct atmel_sha_caps {
	bool	has_dma;
	bool	has_dualbuff;
	bool	has_sha224;
	bool	has_sha_384_512;
	bool	has_uihv;
	bool	has_hmac;
};

struct atmel_sha_dev;

/*
 * .statesize = sizeof(struct atmel_sha_reqctx) must be <= PAGE_SIZE / 8 as
 * tested by the ahash_prepare_alg() function.
 */
struct atmel_sha_reqctx {
	struct atmel_sha_dev	*dd;
	unsigned long	flags;
	unsigned long	op;

	u8	digest[SHA512_DIGEST_SIZE] __aligned(sizeof(u32));
	u64	digcnt[2];
	size_t	bufcnt;
	size_t	buflen;
	dma_addr_t	dma_addr;

	/* walk state */
	struct scatterlist	*sg;
	unsigned int	offset;	/* offset in current sg */
	unsigned int	total;	/* total request */

	size_t block_size;
	size_t hash_size;

	u8 buffer[SHA_BUFFER_LEN + SHA512_BLOCK_SIZE] __aligned(sizeof(u32));
};

typedef int (*atmel_sha_fn_t)(struct atmel_sha_dev *);

struct atmel_sha_ctx {
	struct atmel_sha_dev	*dd;
	atmel_sha_fn_t		start;

	unsigned long		flags;
};

#define ATMEL_SHA_QUEUE_LENGTH	50

struct atmel_sha_dma {
	struct dma_chan			*chan;
	struct dma_slave_config dma_conf;
	struct scatterlist	*sg;
	int			nents;
	unsigned int		last_sg_length;
};

struct atmel_sha_dev {
	struct list_head	list;
	unsigned long		phys_base;
	struct device		*dev;
	struct clk			*iclk;
	int					irq;
	void __iomem		*io_base;

	spinlock_t		lock;
	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	unsigned long		flags;
	struct crypto_queue	queue;
	struct ahash_request	*req;
	bool			is_async;
	bool			force_complete;
	atmel_sha_fn_t		resume;
	atmel_sha_fn_t		cpu_transfer_complete;

	struct atmel_sha_dma	dma_lch_in;

	struct atmel_sha_caps	caps;

	struct scatterlist	tmp;

	u32	hw_version;
};

struct atmel_sha_drv {
	struct list_head	dev_list;
	spinlock_t		lock;
};

static struct atmel_sha_drv atmel_sha = {
	.dev_list = LIST_HEAD_INIT(atmel_sha.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(atmel_sha.lock),
};

#ifdef VERBOSE_DEBUG
static const char *atmel_sha_reg_name(u32 offset, char *tmp, size_t sz, bool wr)
{
	switch (offset) {
	case SHA_CR:
		return "CR";

	case SHA_MR:
		return "MR";

	case SHA_IER:
		return "IER";

	case SHA_IDR:
		return "IDR";

	case SHA_IMR:
		return "IMR";

	case SHA_ISR:
		return "ISR";

	case SHA_MSR:
		return "MSR";

	case SHA_BCR:
		return "BCR";

	case SHA_REG_DIN(0):
	case SHA_REG_DIN(1):
	case SHA_REG_DIN(2):
	case SHA_REG_DIN(3):
	case SHA_REG_DIN(4):
	case SHA_REG_DIN(5):
	case SHA_REG_DIN(6):
	case SHA_REG_DIN(7):
	case SHA_REG_DIN(8):
	case SHA_REG_DIN(9):
	case SHA_REG_DIN(10):
	case SHA_REG_DIN(11):
	case SHA_REG_DIN(12):
	case SHA_REG_DIN(13):
	case SHA_REG_DIN(14):
	case SHA_REG_DIN(15):
		snprintf(tmp, sz, "IDATAR[%u]", (offset - SHA_REG_DIN(0)) >> 2);
		break;

	case SHA_REG_DIGEST(0):
	case SHA_REG_DIGEST(1):
	case SHA_REG_DIGEST(2):
	case SHA_REG_DIGEST(3):
	case SHA_REG_DIGEST(4):
	case SHA_REG_DIGEST(5):
	case SHA_REG_DIGEST(6):
	case SHA_REG_DIGEST(7):
	case SHA_REG_DIGEST(8):
	case SHA_REG_DIGEST(9):
	case SHA_REG_DIGEST(10):
	case SHA_REG_DIGEST(11):
	case SHA_REG_DIGEST(12):
	case SHA_REG_DIGEST(13):
	case SHA_REG_DIGEST(14):
	case SHA_REG_DIGEST(15):
		if (wr)
			snprintf(tmp, sz, "IDATAR[%u]",
				 16u + ((offset - SHA_REG_DIGEST(0)) >> 2));
		else
			snprintf(tmp, sz, "ODATAR[%u]",
				 (offset - SHA_REG_DIGEST(0)) >> 2);
		break;

	case SHA_HW_VERSION:
		return "HWVER";

	default:
		snprintf(tmp, sz, "0x%02x", offset);
		break;
	}

	return tmp;
}

#endif /* VERBOSE_DEBUG */

static inline u32 atmel_sha_read(struct atmel_sha_dev *dd, u32 offset)
{
	u32 value = readl_relaxed(dd->io_base + offset);

#ifdef VERBOSE_DEBUG
	if (dd->flags & SHA_FLAGS_DUMP_REG) {
		char tmp[16];

		dev_vdbg(dd->dev, "read 0x%08x from %s\n", value,
			 atmel_sha_reg_name(offset, tmp, sizeof(tmp), false));
	}
#endif /* VERBOSE_DEBUG */

	return value;
}

static inline void atmel_sha_write(struct atmel_sha_dev *dd,
					u32 offset, u32 value)
{
#ifdef VERBOSE_DEBUG
	if (dd->flags & SHA_FLAGS_DUMP_REG) {
		char tmp[16];

		dev_vdbg(dd->dev, "write 0x%08x into %s\n", value,
			 atmel_sha_reg_name(offset, tmp, sizeof(tmp), true));
	}
#endif /* VERBOSE_DEBUG */

	writel_relaxed(value, dd->io_base + offset);
}

static inline int atmel_sha_complete(struct atmel_sha_dev *dd, int err)
{
	struct ahash_request *req = dd->req;

	dd->flags &= ~(SHA_FLAGS_BUSY | SHA_FLAGS_FINAL | SHA_FLAGS_CPU |
		       SHA_FLAGS_DMA_READY | SHA_FLAGS_OUTPUT_READY |
		       SHA_FLAGS_DUMP_REG);

	clk_disable(dd->iclk);

	if ((dd->is_async || dd->force_complete) && req->base.complete)
		ahash_request_complete(req, err);

	/* handle new request */
	tasklet_schedule(&dd->queue_task);

	return err;
}

static size_t atmel_sha_append_sg(struct atmel_sha_reqctx *ctx)
{
	size_t count;

	while ((ctx->bufcnt < ctx->buflen) && ctx->total) {
		count = min(ctx->sg->length - ctx->offset, ctx->total);
		count = min(count, ctx->buflen - ctx->bufcnt);

		if (count <= 0) {
			/*
			* Check if count <= 0 because the buffer is full or
			* because the sg length is 0. In the latest case,
			* check if there is another sg in the list, a 0 length
			* sg doesn't necessarily mean the end of the sg list.
			*/
			if ((ctx->sg->length == 0) && !sg_is_last(ctx->sg)) {
				ctx->sg = sg_next(ctx->sg);
				continue;
			} else {
				break;
			}
		}

		scatterwalk_map_and_copy(ctx->buffer + ctx->bufcnt, ctx->sg,
			ctx->offset, count, 0);

		ctx->bufcnt += count;
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

/*
 * The purpose of this padding is to ensure that the padded message is a
 * multiple of 512 bits (SHA1/SHA224/SHA256) or 1024 bits (SHA384/SHA512).
 * The bit "1" is appended at the end of the message followed by
 * "padlen-1" zero bits. Then a 64 bits block (SHA1/SHA224/SHA256) or
 * 128 bits block (SHA384/SHA512) equals to the message length in bits
 * is appended.
 *
 * For SHA1/SHA224/SHA256, padlen is calculated as followed:
 *  - if message length < 56 bytes then padlen = 56 - message length
 *  - else padlen = 64 + 56 - message length
 *
 * For SHA384/SHA512, padlen is calculated as followed:
 *  - if message length < 112 bytes then padlen = 112 - message length
 *  - else padlen = 128 + 112 - message length
 */
static void atmel_sha_fill_padding(struct atmel_sha_reqctx *ctx, int length)
{
	unsigned int index, padlen;
	__be64 bits[2];
	u64 size[2];

	size[0] = ctx->digcnt[0];
	size[1] = ctx->digcnt[1];

	size[0] += ctx->bufcnt;
	if (size[0] < ctx->bufcnt)
		size[1]++;

	size[0] += length;
	if (size[0]  < length)
		size[1]++;

	bits[1] = cpu_to_be64(size[0] << 3);
	bits[0] = cpu_to_be64(size[1] << 3 | size[0] >> 61);

	switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
	case SHA_FLAGS_SHA384:
	case SHA_FLAGS_SHA512:
		index = ctx->bufcnt & 0x7f;
		padlen = (index < 112) ? (112 - index) : ((128+112) - index);
		*(ctx->buffer + ctx->bufcnt) = 0x80;
		memset(ctx->buffer + ctx->bufcnt + 1, 0, padlen-1);
		memcpy(ctx->buffer + ctx->bufcnt + padlen, bits, 16);
		ctx->bufcnt += padlen + 16;
		ctx->flags |= SHA_FLAGS_PAD;
		break;

	default:
		index = ctx->bufcnt & 0x3f;
		padlen = (index < 56) ? (56 - index) : ((64+56) - index);
		*(ctx->buffer + ctx->bufcnt) = 0x80;
		memset(ctx->buffer + ctx->bufcnt + 1, 0, padlen-1);
		memcpy(ctx->buffer + ctx->bufcnt + padlen, &bits[1], 8);
		ctx->bufcnt += padlen + 8;
		ctx->flags |= SHA_FLAGS_PAD;
		break;
	}
}

static struct atmel_sha_dev *atmel_sha_find_dev(struct atmel_sha_ctx *tctx)
{
	struct atmel_sha_dev *dd = NULL;
	struct atmel_sha_dev *tmp;

	spin_lock_bh(&atmel_sha.lock);
	if (!tctx->dd) {
		list_for_each_entry(tmp, &atmel_sha.dev_list, list) {
			dd = tmp;
			break;
		}
		tctx->dd = dd;
	} else {
		dd = tctx->dd;
	}

	spin_unlock_bh(&atmel_sha.lock);

	return dd;
}

static int atmel_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_ctx *tctx = crypto_ahash_ctx(tfm);
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = atmel_sha_find_dev(tctx);

	ctx->dd = dd;

	ctx->flags = 0;

	dev_dbg(dd->dev, "init: digest size: %u\n",
		crypto_ahash_digestsize(tfm));

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA1_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA1;
		ctx->block_size = SHA1_BLOCK_SIZE;
		break;
	case SHA224_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA224;
		ctx->block_size = SHA224_BLOCK_SIZE;
		break;
	case SHA256_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA256;
		ctx->block_size = SHA256_BLOCK_SIZE;
		break;
	case SHA384_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA384;
		ctx->block_size = SHA384_BLOCK_SIZE;
		break;
	case SHA512_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA512;
		ctx->block_size = SHA512_BLOCK_SIZE;
		break;
	default:
		return -EINVAL;
	}

	ctx->bufcnt = 0;
	ctx->digcnt[0] = 0;
	ctx->digcnt[1] = 0;
	ctx->buflen = SHA_BUFFER_LEN;

	return 0;
}

static void atmel_sha_write_ctrl(struct atmel_sha_dev *dd, int dma)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	u32 valmr = SHA_MR_MODE_AUTO;
	unsigned int i, hashsize = 0;

	if (likely(dma)) {
		if (!dd->caps.has_dma)
			atmel_sha_write(dd, SHA_IER, SHA_INT_TXBUFE);
		valmr = SHA_MR_MODE_PDC;
		if (dd->caps.has_dualbuff)
			valmr |= SHA_MR_DUALBUFF;
	} else {
		atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
	}

	switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
	case SHA_FLAGS_SHA1:
		valmr |= SHA_MR_ALGO_SHA1;
		hashsize = SHA1_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA224:
		valmr |= SHA_MR_ALGO_SHA224;
		hashsize = SHA256_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA256:
		valmr |= SHA_MR_ALGO_SHA256;
		hashsize = SHA256_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA384:
		valmr |= SHA_MR_ALGO_SHA384;
		hashsize = SHA512_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA512:
		valmr |= SHA_MR_ALGO_SHA512;
		hashsize = SHA512_DIGEST_SIZE;
		break;

	default:
		break;
	}

	/* Setting CR_FIRST only for the first iteration */
	if (!(ctx->digcnt[0] || ctx->digcnt[1])) {
		atmel_sha_write(dd, SHA_CR, SHA_CR_FIRST);
	} else if (dd->caps.has_uihv && (ctx->flags & SHA_FLAGS_RESTORE)) {
		const u32 *hash = (const u32 *)ctx->digest;

		/*
		 * Restore the hardware context: update the User Initialize
		 * Hash Value (UIHV) with the value saved when the latest
		 * 'update' operation completed on this very same crypto
		 * request.
		 */
		ctx->flags &= ~SHA_FLAGS_RESTORE;
		atmel_sha_write(dd, SHA_CR, SHA_CR_WUIHV);
		for (i = 0; i < hashsize / sizeof(u32); ++i)
			atmel_sha_write(dd, SHA_REG_DIN(i), hash[i]);
		atmel_sha_write(dd, SHA_CR, SHA_CR_FIRST);
		valmr |= SHA_MR_UIHV;
	}
	/*
	 * WARNING: If the UIHV feature is not available, the hardware CANNOT
	 * process concurrent requests: the internal registers used to store
	 * the hash/digest are still set to the partial digest output values
	 * computed during the latest round.
	 */

	atmel_sha_write(dd, SHA_MR, valmr);
}

static inline int atmel_sha_wait_for_data_ready(struct atmel_sha_dev *dd,
						atmel_sha_fn_t resume)
{
	u32 isr = atmel_sha_read(dd, SHA_ISR);

	if (unlikely(isr & SHA_INT_DATARDY))
		return resume(dd);

	dd->resume = resume;
	atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
	return -EINPROGRESS;
}

static int atmel_sha_xmit_cpu(struct atmel_sha_dev *dd, const u8 *buf,
			      size_t length, int final)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	int count, len32;
	const u32 *buffer = (const u32 *)buf;

	dev_dbg(dd->dev, "xmit_cpu: digcnt: 0x%llx 0x%llx, length: %zd, final: %d\n",
		ctx->digcnt[1], ctx->digcnt[0], length, final);

	atmel_sha_write_ctrl(dd, 0);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt[0] += length;
	if (ctx->digcnt[0] < length)
		ctx->digcnt[1]++;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	len32 = DIV_ROUND_UP(length, sizeof(u32));

	dd->flags |= SHA_FLAGS_CPU;

	for (count = 0; count < len32; count++)
		atmel_sha_write(dd, SHA_REG_DIN(count), buffer[count]);

	return -EINPROGRESS;
}

static int atmel_sha_xmit_pdc(struct atmel_sha_dev *dd, dma_addr_t dma_addr1,
		size_t length1, dma_addr_t dma_addr2, size_t length2, int final)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	int len32;

	dev_dbg(dd->dev, "xmit_pdc: digcnt: 0x%llx 0x%llx, length: %zd, final: %d\n",
		ctx->digcnt[1], ctx->digcnt[0], length1, final);

	len32 = DIV_ROUND_UP(length1, sizeof(u32));
	atmel_sha_write(dd, SHA_PTCR, SHA_PTCR_TXTDIS);
	atmel_sha_write(dd, SHA_TPR, dma_addr1);
	atmel_sha_write(dd, SHA_TCR, len32);

	len32 = DIV_ROUND_UP(length2, sizeof(u32));
	atmel_sha_write(dd, SHA_TNPR, dma_addr2);
	atmel_sha_write(dd, SHA_TNCR, len32);

	atmel_sha_write_ctrl(dd, 1);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt[0] += length1;
	if (ctx->digcnt[0] < length1)
		ctx->digcnt[1]++;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	dd->flags |=  SHA_FLAGS_DMA_ACTIVE;

	/* Start DMA transfer */
	atmel_sha_write(dd, SHA_PTCR, SHA_PTCR_TXTEN);

	return -EINPROGRESS;
}

static void atmel_sha_dma_callback(void *data)
{
	struct atmel_sha_dev *dd = data;

	dd->is_async = true;

	/* dma_lch_in - completed - wait DATRDY */
	atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
}

static int atmel_sha_xmit_dma(struct atmel_sha_dev *dd, dma_addr_t dma_addr1,
		size_t length1, dma_addr_t dma_addr2, size_t length2, int final)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	struct dma_async_tx_descriptor	*in_desc;
	struct scatterlist sg[2];

	dev_dbg(dd->dev, "xmit_dma: digcnt: 0x%llx 0x%llx, length: %zd, final: %d\n",
		ctx->digcnt[1], ctx->digcnt[0], length1, final);

	dd->dma_lch_in.dma_conf.src_maxburst = 16;
	dd->dma_lch_in.dma_conf.dst_maxburst = 16;

	dmaengine_slave_config(dd->dma_lch_in.chan, &dd->dma_lch_in.dma_conf);

	if (length2) {
		sg_init_table(sg, 2);
		sg_dma_address(&sg[0]) = dma_addr1;
		sg_dma_len(&sg[0]) = length1;
		sg_dma_address(&sg[1]) = dma_addr2;
		sg_dma_len(&sg[1]) = length2;
		in_desc = dmaengine_prep_slave_sg(dd->dma_lch_in.chan, sg, 2,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	} else {
		sg_init_table(sg, 1);
		sg_dma_address(&sg[0]) = dma_addr1;
		sg_dma_len(&sg[0]) = length1;
		in_desc = dmaengine_prep_slave_sg(dd->dma_lch_in.chan, sg, 1,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}
	if (!in_desc)
		return atmel_sha_complete(dd, -EINVAL);

	in_desc->callback = atmel_sha_dma_callback;
	in_desc->callback_param = dd;

	atmel_sha_write_ctrl(dd, 1);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt[0] += length1;
	if (ctx->digcnt[0] < length1)
		ctx->digcnt[1]++;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	dd->flags |=  SHA_FLAGS_DMA_ACTIVE;

	/* Start DMA transfer */
	dmaengine_submit(in_desc);
	dma_async_issue_pending(dd->dma_lch_in.chan);

	return -EINPROGRESS;
}

static int atmel_sha_xmit_start(struct atmel_sha_dev *dd, dma_addr_t dma_addr1,
		size_t length1, dma_addr_t dma_addr2, size_t length2, int final)
{
	if (dd->caps.has_dma)
		return atmel_sha_xmit_dma(dd, dma_addr1, length1,
				dma_addr2, length2, final);
	else
		return atmel_sha_xmit_pdc(dd, dma_addr1, length1,
				dma_addr2, length2, final);
}

static int atmel_sha_update_cpu(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	int bufcnt;

	atmel_sha_append_sg(ctx);
	atmel_sha_fill_padding(ctx, 0);
	bufcnt = ctx->bufcnt;
	ctx->bufcnt = 0;

	return atmel_sha_xmit_cpu(dd, ctx->buffer, bufcnt, 1);
}

static int atmel_sha_xmit_dma_map(struct atmel_sha_dev *dd,
					struct atmel_sha_reqctx *ctx,
					size_t length, int final)
{
	ctx->dma_addr = dma_map_single(dd->dev, ctx->buffer,
				ctx->buflen + ctx->block_size, DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
		dev_err(dd->dev, "dma %zu bytes error\n", ctx->buflen +
				ctx->block_size);
		return atmel_sha_complete(dd, -EINVAL);
	}

	ctx->flags &= ~SHA_FLAGS_SG;

	/* next call does not fail... so no unmap in the case of error */
	return atmel_sha_xmit_start(dd, ctx->dma_addr, length, 0, 0, final);
}

static int atmel_sha_update_dma_slow(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int final;
	size_t count;

	atmel_sha_append_sg(ctx);

	final = (ctx->flags & SHA_FLAGS_FINUP) && !ctx->total;

	dev_dbg(dd->dev, "slow: bufcnt: %zu, digcnt: 0x%llx 0x%llx, final: %d\n",
		 ctx->bufcnt, ctx->digcnt[1], ctx->digcnt[0], final);

	if (final)
		atmel_sha_fill_padding(ctx, 0);

	if (final || (ctx->bufcnt == ctx->buflen)) {
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		return atmel_sha_xmit_dma_map(dd, ctx, count, final);
	}

	return 0;
}

static int atmel_sha_update_dma_start(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int length, final, tail;
	struct scatterlist *sg;
	unsigned int count;

	if (!ctx->total)
		return 0;

	if (ctx->bufcnt || ctx->offset)
		return atmel_sha_update_dma_slow(dd);

	dev_dbg(dd->dev, "fast: digcnt: 0x%llx 0x%llx, bufcnt: %zd, total: %u\n",
		ctx->digcnt[1], ctx->digcnt[0], ctx->bufcnt, ctx->total);

	sg = ctx->sg;

	if (!IS_ALIGNED(sg->offset, sizeof(u32)))
		return atmel_sha_update_dma_slow(dd);

	if (!sg_is_last(sg) && !IS_ALIGNED(sg->length, ctx->block_size))
		/* size is not ctx->block_size aligned */
		return atmel_sha_update_dma_slow(dd);

	length = min(ctx->total, sg->length);

	if (sg_is_last(sg)) {
		if (!(ctx->flags & SHA_FLAGS_FINUP)) {
			/* not last sg must be ctx->block_size aligned */
			tail = length & (ctx->block_size - 1);
			length -= tail;
		}
	}

	ctx->total -= length;
	ctx->offset = length; /* offset where to start slow */

	final = (ctx->flags & SHA_FLAGS_FINUP) && !ctx->total;

	/* Add padding */
	if (final) {
		tail = length & (ctx->block_size - 1);
		length -= tail;
		ctx->total += tail;
		ctx->offset = length; /* offset where to start slow */

		sg = ctx->sg;
		atmel_sha_append_sg(ctx);

		atmel_sha_fill_padding(ctx, length);

		ctx->dma_addr = dma_map_single(dd->dev, ctx->buffer,
			ctx->buflen + ctx->block_size, DMA_TO_DEVICE);
		if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
			dev_err(dd->dev, "dma %zu bytes error\n",
				ctx->buflen + ctx->block_size);
			return atmel_sha_complete(dd, -EINVAL);
		}

		if (length == 0) {
			ctx->flags &= ~SHA_FLAGS_SG;
			count = ctx->bufcnt;
			ctx->bufcnt = 0;
			return atmel_sha_xmit_start(dd, ctx->dma_addr, count, 0,
					0, final);
		} else {
			ctx->sg = sg;
			if (!dma_map_sg(dd->dev, ctx->sg, 1,
				DMA_TO_DEVICE)) {
					dev_err(dd->dev, "dma_map_sg  error\n");
					return atmel_sha_complete(dd, -EINVAL);
			}

			ctx->flags |= SHA_FLAGS_SG;

			count = ctx->bufcnt;
			ctx->bufcnt = 0;
			return atmel_sha_xmit_start(dd, sg_dma_address(ctx->sg),
					length, ctx->dma_addr, count, final);
		}
	}

	if (!dma_map_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE)) {
		dev_err(dd->dev, "dma_map_sg  error\n");
		return atmel_sha_complete(dd, -EINVAL);
	}

	ctx->flags |= SHA_FLAGS_SG;

	/* next call does not fail... so no unmap in the case of error */
	return atmel_sha_xmit_start(dd, sg_dma_address(ctx->sg), length, 0,
								0, final);
}

static void atmel_sha_update_dma_stop(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);

	if (ctx->flags & SHA_FLAGS_SG) {
		dma_unmap_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE);
		if (ctx->sg->length == ctx->offset) {
			ctx->sg = sg_next(ctx->sg);
			if (ctx->sg)
				ctx->offset = 0;
		}
		if (ctx->flags & SHA_FLAGS_PAD) {
			dma_unmap_single(dd->dev, ctx->dma_addr,
				ctx->buflen + ctx->block_size, DMA_TO_DEVICE);
		}
	} else {
		dma_unmap_single(dd->dev, ctx->dma_addr, ctx->buflen +
						ctx->block_size, DMA_TO_DEVICE);
	}
}

static int atmel_sha_update_req(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err;

	dev_dbg(dd->dev, "update_req: total: %u, digcnt: 0x%llx 0x%llx\n",
		ctx->total, ctx->digcnt[1], ctx->digcnt[0]);

	if (ctx->flags & SHA_FLAGS_CPU)
		err = atmel_sha_update_cpu(dd);
	else
		err = atmel_sha_update_dma_start(dd);

	/* wait for dma completion before can take more data */
	dev_dbg(dd->dev, "update: err: %d, digcnt: 0x%llx 0%llx\n",
			err, ctx->digcnt[1], ctx->digcnt[0]);

	return err;
}

static int atmel_sha_final_req(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err = 0;
	int count;

	if (ctx->bufcnt >= ATMEL_SHA_DMA_THRESHOLD) {
		atmel_sha_fill_padding(ctx, 0);
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		err = atmel_sha_xmit_dma_map(dd, ctx, count, 1);
	}
	/* faster to handle last block with cpu */
	else {
		atmel_sha_fill_padding(ctx, 0);
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		err = atmel_sha_xmit_cpu(dd, ctx->buffer, count, 1);
	}

	dev_dbg(dd->dev, "final_req: err: %d\n", err);

	return err;
}

static void atmel_sha_copy_hash(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	u32 *hash = (u32 *)ctx->digest;
	unsigned int i, hashsize;

	switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
	case SHA_FLAGS_SHA1:
		hashsize = SHA1_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA224:
	case SHA_FLAGS_SHA256:
		hashsize = SHA256_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA384:
	case SHA_FLAGS_SHA512:
		hashsize = SHA512_DIGEST_SIZE;
		break;

	default:
		/* Should not happen... */
		return;
	}

	for (i = 0; i < hashsize / sizeof(u32); ++i)
		hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
	ctx->flags |= SHA_FLAGS_RESTORE;
}

static void atmel_sha_copy_ready_hash(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	if (!req->result)
		return;

	switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
	default:
	case SHA_FLAGS_SHA1:
		memcpy(req->result, ctx->digest, SHA1_DIGEST_SIZE);
		break;

	case SHA_FLAGS_SHA224:
		memcpy(req->result, ctx->digest, SHA224_DIGEST_SIZE);
		break;

	case SHA_FLAGS_SHA256:
		memcpy(req->result, ctx->digest, SHA256_DIGEST_SIZE);
		break;

	case SHA_FLAGS_SHA384:
		memcpy(req->result, ctx->digest, SHA384_DIGEST_SIZE);
		break;

	case SHA_FLAGS_SHA512:
		memcpy(req->result, ctx->digest, SHA512_DIGEST_SIZE);
		break;
	}
}

static int atmel_sha_finish(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = ctx->dd;

	if (ctx->digcnt[0] || ctx->digcnt[1])
		atmel_sha_copy_ready_hash(req);

	dev_dbg(dd->dev, "digcnt: 0x%llx 0x%llx, bufcnt: %zd\n", ctx->digcnt[1],
		ctx->digcnt[0], ctx->bufcnt);

	return 0;
}

static void atmel_sha_finish_req(struct ahash_request *req, int err)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = ctx->dd;

	if (!err) {
		atmel_sha_copy_hash(req);
		if (SHA_FLAGS_FINAL & dd->flags)
			err = atmel_sha_finish(req);
	} else {
		ctx->flags |= SHA_FLAGS_ERROR;
	}

	/* atomic operation is not needed here */
	(void)atmel_sha_complete(dd, err);
}

static int atmel_sha_hw_init(struct atmel_sha_dev *dd)
{
	int err;

	err = clk_enable(dd->iclk);
	if (err)
		return err;

	if (!(SHA_FLAGS_INIT & dd->flags)) {
		atmel_sha_write(dd, SHA_CR, SHA_CR_SWRST);
		dd->flags |= SHA_FLAGS_INIT;
	}

	return 0;
}

static inline unsigned int atmel_sha_get_version(struct atmel_sha_dev *dd)
{
	return atmel_sha_read(dd, SHA_HW_VERSION) & 0x00000fff;
}

static int atmel_sha_hw_version_init(struct atmel_sha_dev *dd)
{
	int err;

	err = atmel_sha_hw_init(dd);
	if (err)
		return err;

	dd->hw_version = atmel_sha_get_version(dd);

	dev_info(dd->dev,
			"version: 0x%x\n", dd->hw_version);

	clk_disable(dd->iclk);

	return 0;
}

static int atmel_sha_handle_queue(struct atmel_sha_dev *dd,
				  struct ahash_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct atmel_sha_ctx *ctx;
	unsigned long flags;
	bool start_async;
	int err = 0, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = ahash_enqueue_request(&dd->queue, req);

	if (SHA_FLAGS_BUSY & dd->flags) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}

	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (async_req)
		dd->flags |= SHA_FLAGS_BUSY;

	spin_unlock_irqrestore(&dd->lock, flags);

	if (!async_req)
		return ret;

	if (backlog)
		crypto_request_complete(backlog, -EINPROGRESS);

	ctx = crypto_tfm_ctx(async_req->tfm);

	dd->req = ahash_request_cast(async_req);
	start_async = (dd->req != req);
	dd->is_async = start_async;
	dd->force_complete = false;

	/* WARNING: ctx->start() MAY change dd->is_async. */
	err = ctx->start(dd);
	return (start_async) ? ret : err;
}

static int atmel_sha_done(struct atmel_sha_dev *dd);

static int atmel_sha_start(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err;

	dev_dbg(dd->dev, "handling new req, op: %lu, nbytes: %u\n",
						ctx->op, req->nbytes);

	err = atmel_sha_hw_init(dd);
	if (err)
		return atmel_sha_complete(dd, err);

	/*
	 * atmel_sha_update_req() and atmel_sha_final_req() can return either:
	 *  -EINPROGRESS: the hardware is busy and the SHA driver will resume
	 *                its job later in the done_task.
	 *                This is the main path.
	 *
	 * 0: the SHA driver can continue its job then release the hardware
	 *    later, if needed, with atmel_sha_finish_req().
	 *    This is the alternate path.
	 *
	 * < 0: an error has occurred so atmel_sha_complete(dd, err) has already
	 *      been called, hence the hardware has been released.
	 *      The SHA driver must stop its job without calling
	 *      atmel_sha_finish_req(), otherwise atmel_sha_complete() would be
	 *      called a second time.
	 *
	 * Please note that currently, atmel_sha_final_req() never returns 0.
	 */

	dd->resume = atmel_sha_done;
	if (ctx->op == SHA_OP_UPDATE) {
		err = atmel_sha_update_req(dd);
		if (!err && (ctx->flags & SHA_FLAGS_FINUP))
			/* no final() after finup() */
			err = atmel_sha_final_req(dd);
	} else if (ctx->op == SHA_OP_FINAL) {
		err = atmel_sha_final_req(dd);
	}

	if (!err)
		/* done_task will not finish it, so do it here */
		atmel_sha_finish_req(req, err);

	dev_dbg(dd->dev, "exit, err: %d\n", err);

	return err;
}

static int atmel_sha_enqueue(struct ahash_request *req, unsigned int op)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct atmel_sha_dev *dd = tctx->dd;

	ctx->op = op;

	return atmel_sha_handle_queue(dd, req);
}

static int atmel_sha_update(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	if (!req->nbytes)
		return 0;

	ctx->total = req->nbytes;
	ctx->sg = req->src;
	ctx->offset = 0;

	if (ctx->flags & SHA_FLAGS_FINUP) {
		if (ctx->bufcnt + ctx->total < ATMEL_SHA_DMA_THRESHOLD)
			/* faster to use CPU for short transfers */
			ctx->flags |= SHA_FLAGS_CPU;
	} else if (ctx->bufcnt + ctx->total < ctx->buflen) {
		atmel_sha_append_sg(ctx);
		return 0;
	}
	return atmel_sha_enqueue(req, SHA_OP_UPDATE);
}

static int atmel_sha_final(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	ctx->flags |= SHA_FLAGS_FINUP;

	if (ctx->flags & SHA_FLAGS_ERROR)
		return 0; /* uncompleted hash is not needed */

	if (ctx->flags & SHA_FLAGS_PAD)
		/* copy ready hash (+ finalize hmac) */
		return atmel_sha_finish(req);

	return atmel_sha_enqueue(req, SHA_OP_FINAL);
}

static int atmel_sha_finup(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err1, err2;

	ctx->flags |= SHA_FLAGS_FINUP;

	err1 = atmel_sha_update(req);
	if (err1 == -EINPROGRESS ||
	    (err1 == -EBUSY && (ahash_request_flags(req) &
				CRYPTO_TFM_REQ_MAY_BACKLOG)))
		return err1;

	/*
	 * final() has to be always called to cleanup resources
	 * even if udpate() failed, except EINPROGRESS
	 */
	err2 = atmel_sha_final(req);

	return err1 ?: err2;
}

static int atmel_sha_digest(struct ahash_request *req)
{
	return atmel_sha_init(req) ?: atmel_sha_finup(req);
}


static int atmel_sha_export(struct ahash_request *req, void *out)
{
	const struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	memcpy(out, ctx, sizeof(*ctx));
	return 0;
}

static int atmel_sha_import(struct ahash_request *req, const void *in)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	memcpy(ctx, in, sizeof(*ctx));
	return 0;
}

static int atmel_sha_cra_init(struct crypto_tfm *tfm)
{
	struct atmel_sha_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct atmel_sha_reqctx));
	ctx->start = atmel_sha_start;

	return 0;
}

static void atmel_sha_alg_init(struct ahash_alg *alg)
{
	alg->halg.base.cra_priority = ATMEL_SHA_PRIORITY;
	alg->halg.base.cra_flags = CRYPTO_ALG_ASYNC;
	alg->halg.base.cra_ctxsize = sizeof(struct atmel_sha_ctx);
	alg->halg.base.cra_module = THIS_MODULE;
	alg->halg.base.cra_init = atmel_sha_cra_init;

	alg->halg.statesize = sizeof(struct atmel_sha_reqctx);

	alg->init = atmel_sha_init;
	alg->update = atmel_sha_update;
	alg->final = atmel_sha_final;
	alg->finup = atmel_sha_finup;
	alg->digest = atmel_sha_digest;
	alg->export = atmel_sha_export;
	alg->import = atmel_sha_import;
}

static struct ahash_alg sha_1_256_algs[] = {
{
	.halg.base.cra_name		= "sha1",
	.halg.base.cra_driver_name	= "atmel-sha1",
	.halg.base.cra_blocksize	= SHA1_BLOCK_SIZE,

	.halg.digestsize = SHA1_DIGEST_SIZE,
},
{
	.halg.base.cra_name		= "sha256",
	.halg.base.cra_driver_name	= "atmel-sha256",
	.halg.base.cra_blocksize	= SHA256_BLOCK_SIZE,

	.halg.digestsize = SHA256_DIGEST_SIZE,
},
};

static struct ahash_alg sha_224_alg = {
	.halg.base.cra_name		= "sha224",
	.halg.base.cra_driver_name	= "atmel-sha224",
	.halg.base.cra_blocksize	= SHA224_BLOCK_SIZE,

	.halg.digestsize = SHA224_DIGEST_SIZE,
};

static struct ahash_alg sha_384_512_algs[] = {
{
	.halg.base.cra_name		= "sha384",
	.halg.base.cra_driver_name	= "atmel-sha384",
	.halg.base.cra_blocksize	= SHA384_BLOCK_SIZE,

	.halg.digestsize = SHA384_DIGEST_SIZE,
},
{
	.halg.base.cra_name		= "sha512",
	.halg.base.cra_driver_name	= "atmel-sha512",
	.halg.base.cra_blocksize	= SHA512_BLOCK_SIZE,

	.halg.digestsize = SHA512_DIGEST_SIZE,
},
};

static void atmel_sha_queue_task(unsigned long data)
{
	struct atmel_sha_dev *dd = (struct atmel_sha_dev *)data;

	atmel_sha_handle_queue(dd, NULL);
}

static int atmel_sha_done(struct atmel_sha_dev *dd)
{
	int err = 0;

	if (SHA_FLAGS_CPU & dd->flags) {
		if (SHA_FLAGS_OUTPUT_READY & dd->flags) {
			dd->flags &= ~SHA_FLAGS_OUTPUT_READY;
			goto finish;
		}
	} else if (SHA_FLAGS_DMA_READY & dd->flags) {
		if (SHA_FLAGS_DMA_ACTIVE & dd->flags) {
			dd->flags &= ~SHA_FLAGS_DMA_ACTIVE;
			atmel_sha_update_dma_stop(dd);
		}
		if (SHA_FLAGS_OUTPUT_READY & dd->flags) {
			/* hash or semi-hash ready */
			dd->flags &= ~(SHA_FLAGS_DMA_READY |
						SHA_FLAGS_OUTPUT_READY);
			err = atmel_sha_update_dma_start(dd);
			if (err != -EINPROGRESS)
				goto finish;
		}
	}
	return err;

finish:
	/* finish curent request */
	atmel_sha_finish_req(dd->req, err);

	return err;
}

static void atmel_sha_done_task(unsigned long data)
{
	struct atmel_sha_dev *dd = (struct atmel_sha_dev *)data;

	dd->is_async = true;
	(void)dd->resume(dd);
}

static irqreturn_t atmel_sha_irq(int irq, void *dev_id)
{
	struct atmel_sha_dev *sha_dd = dev_id;
	u32 reg;

	reg = atmel_sha_read(sha_dd, SHA_ISR);
	if (reg & atmel_sha_read(sha_dd, SHA_IMR)) {
		atmel_sha_write(sha_dd, SHA_IDR, reg);
		if (SHA_FLAGS_BUSY & sha_dd->flags) {
			sha_dd->flags |= SHA_FLAGS_OUTPUT_READY;
			if (!(SHA_FLAGS_CPU & sha_dd->flags))
				sha_dd->flags |= SHA_FLAGS_DMA_READY;
			tasklet_schedule(&sha_dd->done_task);
		} else {
			dev_warn(sha_dd->dev, "SHA interrupt when no active requests.\n");
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}


/* DMA transfer functions */

static bool atmel_sha_dma_check_aligned(struct atmel_sha_dev *dd,
					struct scatterlist *sg,
					size_t len)
{
	struct atmel_sha_dma *dma = &dd->dma_lch_in;
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	size_t bs = ctx->block_size;
	int nents;

	for (nents = 0; sg; sg = sg_next(sg), ++nents) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32)))
			return false;

		/*
		 * This is the last sg, the only one that is allowed to
		 * have an unaligned length.
		 */
		if (len <= sg->length) {
			dma->nents = nents + 1;
			dma->last_sg_length = sg->length;
			sg->length = ALIGN(len, sizeof(u32));
			return true;
		}

		/* All other sg lengths MUST be aligned to the block size. */
		if (!IS_ALIGNED(sg->length, bs))
			return false;

		len -= sg->length;
	}

	return false;
}

static void atmel_sha_dma_callback2(void *data)
{
	struct atmel_sha_dev *dd = data;
	struct atmel_sha_dma *dma = &dd->dma_lch_in;
	struct scatterlist *sg;
	int nents;

	dma_unmap_sg(dd->dev, dma->sg, dma->nents, DMA_TO_DEVICE);

	sg = dma->sg;
	for (nents = 0; nents < dma->nents - 1; ++nents)
		sg = sg_next(sg);
	sg->length = dma->last_sg_length;

	dd->is_async = true;
	(void)atmel_sha_wait_for_data_ready(dd, dd->resume);
}

static int atmel_sha_dma_start(struct atmel_sha_dev *dd,
			       struct scatterlist *src,
			       size_t len,
			       atmel_sha_fn_t resume)
{
	struct atmel_sha_dma *dma = &dd->dma_lch_in;
	struct dma_slave_config *config = &dma->dma_conf;
	struct dma_chan *chan = dma->chan;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	unsigned int sg_len;
	int err;

	dd->resume = resume;

	/*
	 * dma->nents has already been initialized by
	 * atmel_sha_dma_check_aligned().
	 */
	dma->sg = src;
	sg_len = dma_map_sg(dd->dev, dma->sg, dma->nents, DMA_TO_DEVICE);
	if (!sg_len) {
		err = -ENOMEM;
		goto exit;
	}

	config->src_maxburst = 16;
	config->dst_maxburst = 16;
	err = dmaengine_slave_config(chan, config);
	if (err)
		goto unmap_sg;

	desc = dmaengine_prep_slave_sg(chan, dma->sg, sg_len, DMA_MEM_TO_DEV,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		err = -ENOMEM;
		goto unmap_sg;
	}

	desc->callback = atmel_sha_dma_callback2;
	desc->callback_param = dd;
	cookie = dmaengine_submit(desc);
	err = dma_submit_error(cookie);
	if (err)
		goto unmap_sg;

	dma_async_issue_pending(chan);

	return -EINPROGRESS;

unmap_sg:
	dma_unmap_sg(dd->dev, dma->sg, dma->nents, DMA_TO_DEVICE);
exit:
	return atmel_sha_complete(dd, err);
}


/* CPU transfer functions */

static int atmel_sha_cpu_transfer(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	const u32 *words = (const u32 *)ctx->buffer;
	size_t i, num_words;
	u32 isr, din, din_inc;

	din_inc = (ctx->flags & SHA_FLAGS_IDATAR0) ? 0 : 1;
	for (;;) {
		/* Write data into the Input Data Registers. */
		num_words = DIV_ROUND_UP(ctx->bufcnt, sizeof(u32));
		for (i = 0, din = 0; i < num_words; ++i, din += din_inc)
			atmel_sha_write(dd, SHA_REG_DIN(din), words[i]);

		ctx->offset += ctx->bufcnt;
		ctx->total -= ctx->bufcnt;

		if (!ctx->total)
			break;

		/*
		 * Prepare next block:
		 * Fill ctx->buffer now with the next data to be written into
		 * IDATARx: it gives time for the SHA hardware to process
		 * the current data so the SHA_INT_DATARDY flag might be set
		 * in SHA_ISR when polling this register at the beginning of
		 * the next loop.
		 */
		ctx->bufcnt = min_t(size_t, ctx->block_size, ctx->total);
		scatterwalk_map_and_copy(ctx->buffer, ctx->sg,
					 ctx->offset, ctx->bufcnt, 0);

		/* Wait for hardware to be ready again. */
		isr = atmel_sha_read(dd, SHA_ISR);
		if (!(isr & SHA_INT_DATARDY)) {
			/* Not ready yet. */
			dd->resume = atmel_sha_cpu_transfer;
			atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
			return -EINPROGRESS;
		}
	}

	if (unlikely(!(ctx->flags & SHA_FLAGS_WAIT_DATARDY)))
		return dd->cpu_transfer_complete(dd);

	return atmel_sha_wait_for_data_ready(dd, dd->cpu_transfer_complete);
}

static int atmel_sha_cpu_start(struct atmel_sha_dev *dd,
			       struct scatterlist *sg,
			       unsigned int len,
			       bool idatar0_only,
			       bool wait_data_ready,
			       atmel_sha_fn_t resume)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	if (!len)
		return resume(dd);

	ctx->flags &= ~(SHA_FLAGS_IDATAR0 | SHA_FLAGS_WAIT_DATARDY);

	if (idatar0_only)
		ctx->flags |= SHA_FLAGS_IDATAR0;

	if (wait_data_ready)
		ctx->flags |= SHA_FLAGS_WAIT_DATARDY;

	ctx->sg = sg;
	ctx->total = len;
	ctx->offset = 0;

	/* Prepare the first block to be written. */
	ctx->bufcnt = min_t(size_t, ctx->block_size, ctx->total);
	scatterwalk_map_and_copy(ctx->buffer, ctx->sg,
				 ctx->offset, ctx->bufcnt, 0);

	dd->cpu_transfer_complete = resume;
	return atmel_sha_cpu_transfer(dd);
}

static int atmel_sha_cpu_hash(struct atmel_sha_dev *dd,
			      const void *data, unsigned int datalen,
			      bool auto_padding,
			      atmel_sha_fn_t resume)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	u32 msglen = (auto_padding) ? datalen : 0;
	u32 mr = SHA_MR_MODE_AUTO;

	if (!(IS_ALIGNED(datalen, ctx->block_size) || auto_padding))
		return atmel_sha_complete(dd, -EINVAL);

	mr |= (ctx->flags & SHA_FLAGS_ALGO_MASK);
	atmel_sha_write(dd, SHA_MR, mr);
	atmel_sha_write(dd, SHA_MSR, msglen);
	atmel_sha_write(dd, SHA_BCR, msglen);
	atmel_sha_write(dd, SHA_CR, SHA_CR_FIRST);

	sg_init_one(&dd->tmp, data, datalen);
	return atmel_sha_cpu_start(dd, &dd->tmp, datalen, false, true, resume);
}


/* hmac functions */

struct atmel_sha_hmac_key {
	bool			valid;
	unsigned int		keylen;
	u8			buffer[SHA512_BLOCK_SIZE];
	u8			*keydup;
};

static inline void atmel_sha_hmac_key_init(struct atmel_sha_hmac_key *hkey)
{
	memset(hkey, 0, sizeof(*hkey));
}

static inline void atmel_sha_hmac_key_release(struct atmel_sha_hmac_key *hkey)
{
	kfree(hkey->keydup);
	memset(hkey, 0, sizeof(*hkey));
}

static inline int atmel_sha_hmac_key_set(struct atmel_sha_hmac_key *hkey,
					 const u8 *key,
					 unsigned int keylen)
{
	atmel_sha_hmac_key_release(hkey);

	if (keylen > sizeof(hkey->buffer)) {
		hkey->keydup = kmemdup(key, keylen, GFP_KERNEL);
		if (!hkey->keydup)
			return -ENOMEM;

	} else {
		memcpy(hkey->buffer, key, keylen);
	}

	hkey->valid = true;
	hkey->keylen = keylen;
	return 0;
}

static inline bool atmel_sha_hmac_key_get(const struct atmel_sha_hmac_key *hkey,
					  const u8 **key,
					  unsigned int *keylen)
{
	if (!hkey->valid)
		return false;

	*keylen = hkey->keylen;
	*key = (hkey->keydup) ? hkey->keydup : hkey->buffer;
	return true;
}


struct atmel_sha_hmac_ctx {
	struct atmel_sha_ctx	base;

	struct atmel_sha_hmac_key	hkey;
	u32			ipad[SHA512_BLOCK_SIZE / sizeof(u32)];
	u32			opad[SHA512_BLOCK_SIZE / sizeof(u32)];
	atmel_sha_fn_t		resume;
};

static int atmel_sha_hmac_setup(struct atmel_sha_dev *dd,
				atmel_sha_fn_t resume);
static int atmel_sha_hmac_prehash_key(struct atmel_sha_dev *dd,
				      const u8 *key, unsigned int keylen);
static int atmel_sha_hmac_prehash_key_done(struct atmel_sha_dev *dd);
static int atmel_sha_hmac_compute_ipad_hash(struct atmel_sha_dev *dd);
static int atmel_sha_hmac_compute_opad_hash(struct atmel_sha_dev *dd);
static int atmel_sha_hmac_setup_done(struct atmel_sha_dev *dd);

static int atmel_sha_hmac_init_done(struct atmel_sha_dev *dd);
static int atmel_sha_hmac_final(struct atmel_sha_dev *dd);
static int atmel_sha_hmac_final_done(struct atmel_sha_dev *dd);
static int atmel_sha_hmac_digest2(struct atmel_sha_dev *dd);

static int atmel_sha_hmac_setup(struct atmel_sha_dev *dd,
				atmel_sha_fn_t resume)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	unsigned int keylen;
	const u8 *key;
	size_t bs;

	hmac->resume = resume;
	switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
	case SHA_FLAGS_SHA1:
		ctx->block_size = SHA1_BLOCK_SIZE;
		ctx->hash_size = SHA1_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA224:
		ctx->block_size = SHA224_BLOCK_SIZE;
		ctx->hash_size = SHA256_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA256:
		ctx->block_size = SHA256_BLOCK_SIZE;
		ctx->hash_size = SHA256_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA384:
		ctx->block_size = SHA384_BLOCK_SIZE;
		ctx->hash_size = SHA512_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA512:
		ctx->block_size = SHA512_BLOCK_SIZE;
		ctx->hash_size = SHA512_DIGEST_SIZE;
		break;

	default:
		return atmel_sha_complete(dd, -EINVAL);
	}
	bs = ctx->block_size;

	if (likely(!atmel_sha_hmac_key_get(&hmac->hkey, &key, &keylen)))
		return resume(dd);

	/* Compute K' from K. */
	if (unlikely(keylen > bs))
		return atmel_sha_hmac_prehash_key(dd, key, keylen);

	/* Prepare ipad. */
	memcpy((u8 *)hmac->ipad, key, keylen);
	memset((u8 *)hmac->ipad + keylen, 0, bs - keylen);
	return atmel_sha_hmac_compute_ipad_hash(dd);
}

static int atmel_sha_hmac_prehash_key(struct atmel_sha_dev *dd,
				      const u8 *key, unsigned int keylen)
{
	return atmel_sha_cpu_hash(dd, key, keylen, true,
				  atmel_sha_hmac_prehash_key_done);
}

static int atmel_sha_hmac_prehash_key_done(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	size_t ds = crypto_ahash_digestsize(tfm);
	size_t bs = ctx->block_size;
	size_t i, num_words = ds / sizeof(u32);

	/* Prepare ipad. */
	for (i = 0; i < num_words; ++i)
		hmac->ipad[i] = atmel_sha_read(dd, SHA_REG_DIGEST(i));
	memset((u8 *)hmac->ipad + ds, 0, bs - ds);
	return atmel_sha_hmac_compute_ipad_hash(dd);
}

static int atmel_sha_hmac_compute_ipad_hash(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	size_t bs = ctx->block_size;
	size_t i, num_words = bs / sizeof(u32);

	unsafe_memcpy(hmac->opad, hmac->ipad, bs,
		      "fortified memcpy causes -Wrestrict warning");
	for (i = 0; i < num_words; ++i) {
		hmac->ipad[i] ^= 0x36363636;
		hmac->opad[i] ^= 0x5c5c5c5c;
	}

	return atmel_sha_cpu_hash(dd, hmac->ipad, bs, false,
				  atmel_sha_hmac_compute_opad_hash);
}

static int atmel_sha_hmac_compute_opad_hash(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	size_t bs = ctx->block_size;
	size_t hs = ctx->hash_size;
	size_t i, num_words = hs / sizeof(u32);

	for (i = 0; i < num_words; ++i)
		hmac->ipad[i] = atmel_sha_read(dd, SHA_REG_DIGEST(i));
	return atmel_sha_cpu_hash(dd, hmac->opad, bs, false,
				  atmel_sha_hmac_setup_done);
}

static int atmel_sha_hmac_setup_done(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	size_t hs = ctx->hash_size;
	size_t i, num_words = hs / sizeof(u32);

	for (i = 0; i < num_words; ++i)
		hmac->opad[i] = atmel_sha_read(dd, SHA_REG_DIGEST(i));
	atmel_sha_hmac_key_release(&hmac->hkey);
	return hmac->resume(dd);
}

static int atmel_sha_hmac_start(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err;

	err = atmel_sha_hw_init(dd);
	if (err)
		return atmel_sha_complete(dd, err);

	switch (ctx->op) {
	case SHA_OP_INIT:
		err = atmel_sha_hmac_setup(dd, atmel_sha_hmac_init_done);
		break;

	case SHA_OP_UPDATE:
		dd->resume = atmel_sha_done;
		err = atmel_sha_update_req(dd);
		break;

	case SHA_OP_FINAL:
		dd->resume = atmel_sha_hmac_final;
		err = atmel_sha_final_req(dd);
		break;

	case SHA_OP_DIGEST:
		err = atmel_sha_hmac_setup(dd, atmel_sha_hmac_digest2);
		break;

	default:
		return atmel_sha_complete(dd, -EINVAL);
	}

	return err;
}

static int atmel_sha_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
				 unsigned int keylen)
{
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);

	return atmel_sha_hmac_key_set(&hmac->hkey, key, keylen);
}

static int atmel_sha_hmac_init(struct ahash_request *req)
{
	int err;

	err = atmel_sha_init(req);
	if (err)
		return err;

	return atmel_sha_enqueue(req, SHA_OP_INIT);
}

static int atmel_sha_hmac_init_done(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	size_t bs = ctx->block_size;
	size_t hs = ctx->hash_size;

	ctx->bufcnt = 0;
	ctx->digcnt[0] = bs;
	ctx->digcnt[1] = 0;
	ctx->flags |= SHA_FLAGS_RESTORE;
	memcpy(ctx->digest, hmac->ipad, hs);
	return atmel_sha_complete(dd, 0);
}

static int atmel_sha_hmac_final(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	u32 *digest = (u32 *)ctx->digest;
	size_t ds = crypto_ahash_digestsize(tfm);
	size_t bs = ctx->block_size;
	size_t hs = ctx->hash_size;
	size_t i, num_words;
	u32 mr;

	/* Save d = SHA((K' + ipad) | msg). */
	num_words = ds / sizeof(u32);
	for (i = 0; i < num_words; ++i)
		digest[i] = atmel_sha_read(dd, SHA_REG_DIGEST(i));

	/* Restore context to finish computing SHA((K' + opad) | d). */
	atmel_sha_write(dd, SHA_CR, SHA_CR_WUIHV);
	num_words = hs / sizeof(u32);
	for (i = 0; i < num_words; ++i)
		atmel_sha_write(dd, SHA_REG_DIN(i), hmac->opad[i]);

	mr = SHA_MR_MODE_AUTO | SHA_MR_UIHV;
	mr |= (ctx->flags & SHA_FLAGS_ALGO_MASK);
	atmel_sha_write(dd, SHA_MR, mr);
	atmel_sha_write(dd, SHA_MSR, bs + ds);
	atmel_sha_write(dd, SHA_BCR, ds);
	atmel_sha_write(dd, SHA_CR, SHA_CR_FIRST);

	sg_init_one(&dd->tmp, digest, ds);
	return atmel_sha_cpu_start(dd, &dd->tmp, ds, false, true,
				   atmel_sha_hmac_final_done);
}

static int atmel_sha_hmac_final_done(struct atmel_sha_dev *dd)
{
	/*
	 * req->result might not be sizeof(u32) aligned, so copy the
	 * digest into ctx->digest[] before memcpy() the data into
	 * req->result.
	 */
	atmel_sha_copy_hash(dd->req);
	atmel_sha_copy_ready_hash(dd->req);
	return atmel_sha_complete(dd, 0);
}

static int atmel_sha_hmac_digest(struct ahash_request *req)
{
	int err;

	err = atmel_sha_init(req);
	if (err)
		return err;

	return atmel_sha_enqueue(req, SHA_OP_DIGEST);
}

static int atmel_sha_hmac_digest2(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	struct scatterlist *sgbuf;
	size_t hs = ctx->hash_size;
	size_t i, num_words = hs / sizeof(u32);
	bool use_dma = false;
	u32 mr;

	/* Special case for empty message. */
	if (!req->nbytes) {
		req->nbytes = 0;
		ctx->bufcnt = 0;
		ctx->digcnt[0] = 0;
		ctx->digcnt[1] = 0;
		switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
		case SHA_FLAGS_SHA1:
		case SHA_FLAGS_SHA224:
		case SHA_FLAGS_SHA256:
			atmel_sha_fill_padding(ctx, 64);
			break;

		case SHA_FLAGS_SHA384:
		case SHA_FLAGS_SHA512:
			atmel_sha_fill_padding(ctx, 128);
			break;
		}
		sg_init_one(&dd->tmp, ctx->buffer, ctx->bufcnt);
	}

	/* Check DMA threshold and alignment. */
	if (req->nbytes > ATMEL_SHA_DMA_THRESHOLD &&
	    atmel_sha_dma_check_aligned(dd, req->src, req->nbytes))
		use_dma = true;

	/* Write both initial hash values to compute a HMAC. */
	atmel_sha_write(dd, SHA_CR, SHA_CR_WUIHV);
	for (i = 0; i < num_words; ++i)
		atmel_sha_write(dd, SHA_REG_DIN(i), hmac->ipad[i]);

	atmel_sha_write(dd, SHA_CR, SHA_CR_WUIEHV);
	for (i = 0; i < num_words; ++i)
		atmel_sha_write(dd, SHA_REG_DIN(i), hmac->opad[i]);

	/* Write the Mode, Message Size, Bytes Count then Control Registers. */
	mr = (SHA_MR_HMAC | SHA_MR_DUALBUFF);
	mr |= ctx->flags & SHA_FLAGS_ALGO_MASK;
	if (use_dma)
		mr |= SHA_MR_MODE_IDATAR0;
	else
		mr |= SHA_MR_MODE_AUTO;
	atmel_sha_write(dd, SHA_MR, mr);

	atmel_sha_write(dd, SHA_MSR, req->nbytes);
	atmel_sha_write(dd, SHA_BCR, req->nbytes);

	atmel_sha_write(dd, SHA_CR, SHA_CR_FIRST);

	/* Special case for empty message. */
	if (!req->nbytes) {
		sgbuf = &dd->tmp;
		req->nbytes = ctx->bufcnt;
	} else {
		sgbuf = req->src;
	}

	/* Process data. */
	if (use_dma)
		return atmel_sha_dma_start(dd, sgbuf, req->nbytes,
					   atmel_sha_hmac_final_done);

	return atmel_sha_cpu_start(dd, sgbuf, req->nbytes, false, true,
				   atmel_sha_hmac_final_done);
}

static int atmel_sha_hmac_cra_init(struct crypto_tfm *tfm)
{
	struct atmel_sha_hmac_ctx *hmac = crypto_tfm_ctx(tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct atmel_sha_reqctx));
	hmac->base.start = atmel_sha_hmac_start;
	atmel_sha_hmac_key_init(&hmac->hkey);

	return 0;
}

static void atmel_sha_hmac_cra_exit(struct crypto_tfm *tfm)
{
	struct atmel_sha_hmac_ctx *hmac = crypto_tfm_ctx(tfm);

	atmel_sha_hmac_key_release(&hmac->hkey);
}

static void atmel_sha_hmac_alg_init(struct ahash_alg *alg)
{
	alg->halg.base.cra_priority = ATMEL_SHA_PRIORITY;
	alg->halg.base.cra_flags = CRYPTO_ALG_ASYNC;
	alg->halg.base.cra_ctxsize = sizeof(struct atmel_sha_hmac_ctx);
	alg->halg.base.cra_module = THIS_MODULE;
	alg->halg.base.cra_init	= atmel_sha_hmac_cra_init;
	alg->halg.base.cra_exit	= atmel_sha_hmac_cra_exit;

	alg->halg.statesize = sizeof(struct atmel_sha_reqctx);

	alg->init = atmel_sha_hmac_init;
	alg->update = atmel_sha_update;
	alg->final = atmel_sha_final;
	alg->digest = atmel_sha_hmac_digest;
	alg->setkey = atmel_sha_hmac_setkey;
	alg->export = atmel_sha_export;
	alg->import = atmel_sha_import;
}

static struct ahash_alg sha_hmac_algs[] = {
{
	.halg.base.cra_name		= "hmac(sha1)",
	.halg.base.cra_driver_name	= "atmel-hmac-sha1",
	.halg.base.cra_blocksize	= SHA1_BLOCK_SIZE,

	.halg.digestsize = SHA1_DIGEST_SIZE,
},
{
	.halg.base.cra_name		= "hmac(sha224)",
	.halg.base.cra_driver_name	= "atmel-hmac-sha224",
	.halg.base.cra_blocksize	= SHA224_BLOCK_SIZE,

	.halg.digestsize = SHA224_DIGEST_SIZE,
},
{
	.halg.base.cra_name		= "hmac(sha256)",
	.halg.base.cra_driver_name	= "atmel-hmac-sha256",
	.halg.base.cra_blocksize	= SHA256_BLOCK_SIZE,

	.halg.digestsize = SHA256_DIGEST_SIZE,
},
{
	.halg.base.cra_name		= "hmac(sha384)",
	.halg.base.cra_driver_name	= "atmel-hmac-sha384",
	.halg.base.cra_blocksize	= SHA384_BLOCK_SIZE,

	.halg.digestsize = SHA384_DIGEST_SIZE,
},
{
	.halg.base.cra_name		= "hmac(sha512)",
	.halg.base.cra_driver_name	= "atmel-hmac-sha512",
	.halg.base.cra_blocksize	= SHA512_BLOCK_SIZE,

	.halg.digestsize = SHA512_DIGEST_SIZE,
},
};

#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_AUTHENC)
/* authenc functions */

static int atmel_sha_authenc_init2(struct atmel_sha_dev *dd);
static int atmel_sha_authenc_init_done(struct atmel_sha_dev *dd);
static int atmel_sha_authenc_final_done(struct atmel_sha_dev *dd);


struct atmel_sha_authenc_ctx {
	struct crypto_ahash	*tfm;
};

struct atmel_sha_authenc_reqctx {
	struct atmel_sha_reqctx	base;

	atmel_aes_authenc_fn_t	cb;
	struct atmel_aes_dev	*aes_dev;

	/* _init() parameters. */
	struct scatterlist	*assoc;
	u32			assoclen;
	u32			textlen;

	/* _final() parameters. */
	u32			*digest;
	unsigned int		digestlen;
};

static void atmel_sha_authenc_complete(void *data, int err)
{
	struct ahash_request *req = data;
	struct atmel_sha_authenc_reqctx *authctx  = ahash_request_ctx(req);

	authctx->cb(authctx->aes_dev, err, authctx->base.dd->is_async);
}

static int atmel_sha_authenc_start(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	int err;

	/*
	 * Force atmel_sha_complete() to call req->base.complete(), ie
	 * atmel_sha_authenc_complete(), which in turn calls authctx->cb().
	 */
	dd->force_complete = true;

	err = atmel_sha_hw_init(dd);
	return authctx->cb(authctx->aes_dev, err, dd->is_async);
}

bool atmel_sha_authenc_is_ready(void)
{
	struct atmel_sha_ctx dummy;

	dummy.dd = NULL;
	return (atmel_sha_find_dev(&dummy) != NULL);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_is_ready);

unsigned int atmel_sha_authenc_get_reqsize(void)
{
	return sizeof(struct atmel_sha_authenc_reqctx);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_get_reqsize);

struct atmel_sha_authenc_ctx *atmel_sha_authenc_spawn(unsigned long mode)
{
	struct atmel_sha_authenc_ctx *auth;
	struct crypto_ahash *tfm;
	struct atmel_sha_ctx *tctx;
	const char *name;
	int err = -EINVAL;

	switch (mode & SHA_FLAGS_MODE_MASK) {
	case SHA_FLAGS_HMAC_SHA1:
		name = "atmel-hmac-sha1";
		break;

	case SHA_FLAGS_HMAC_SHA224:
		name = "atmel-hmac-sha224";
		break;

	case SHA_FLAGS_HMAC_SHA256:
		name = "atmel-hmac-sha256";
		break;

	case SHA_FLAGS_HMAC_SHA384:
		name = "atmel-hmac-sha384";
		break;

	case SHA_FLAGS_HMAC_SHA512:
		name = "atmel-hmac-sha512";
		break;

	default:
		goto error;
	}

	tfm = crypto_alloc_ahash(name, 0, 0);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		goto error;
	}
	tctx = crypto_ahash_ctx(tfm);
	tctx->start = atmel_sha_authenc_start;
	tctx->flags = mode;

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth) {
		err = -ENOMEM;
		goto err_free_ahash;
	}
	auth->tfm = tfm;

	return auth;

err_free_ahash:
	crypto_free_ahash(tfm);
error:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_spawn);

void atmel_sha_authenc_free(struct atmel_sha_authenc_ctx *auth)
{
	if (auth)
		crypto_free_ahash(auth->tfm);
	kfree(auth);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_free);

int atmel_sha_authenc_setkey(struct atmel_sha_authenc_ctx *auth,
			     const u8 *key, unsigned int keylen, u32 flags)
{
	struct crypto_ahash *tfm = auth->tfm;

	crypto_ahash_clear_flags(tfm, CRYPTO_TFM_REQ_MASK);
	crypto_ahash_set_flags(tfm, flags & CRYPTO_TFM_REQ_MASK);
	return crypto_ahash_setkey(tfm, key, keylen);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_setkey);

int atmel_sha_authenc_schedule(struct ahash_request *req,
			       struct atmel_sha_authenc_ctx *auth,
			       atmel_aes_authenc_fn_t cb,
			       struct atmel_aes_dev *aes_dev)
{
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	struct atmel_sha_reqctx *ctx = &authctx->base;
	struct crypto_ahash *tfm = auth->tfm;
	struct atmel_sha_ctx *tctx = crypto_ahash_ctx(tfm);
	struct atmel_sha_dev *dd;

	/* Reset request context (MUST be done first). */
	memset(authctx, 0, sizeof(*authctx));

	/* Get SHA device. */
	dd = atmel_sha_find_dev(tctx);
	if (!dd)
		return cb(aes_dev, -ENODEV, false);

	/* Init request context. */
	ctx->dd = dd;
	ctx->buflen = SHA_BUFFER_LEN;
	authctx->cb = cb;
	authctx->aes_dev = aes_dev;
	ahash_request_set_tfm(req, tfm);
	ahash_request_set_callback(req, 0, atmel_sha_authenc_complete, req);

	return atmel_sha_handle_queue(dd, req);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_schedule);

int atmel_sha_authenc_init(struct ahash_request *req,
			   struct scatterlist *assoc, unsigned int assoclen,
			   unsigned int textlen,
			   atmel_aes_authenc_fn_t cb,
			   struct atmel_aes_dev *aes_dev)
{
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	struct atmel_sha_reqctx *ctx = &authctx->base;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	struct atmel_sha_dev *dd = ctx->dd;

	if (unlikely(!IS_ALIGNED(assoclen, sizeof(u32))))
		return atmel_sha_complete(dd, -EINVAL);

	authctx->cb = cb;
	authctx->aes_dev = aes_dev;
	authctx->assoc = assoc;
	authctx->assoclen = assoclen;
	authctx->textlen = textlen;

	ctx->flags = hmac->base.flags;
	return atmel_sha_hmac_setup(dd, atmel_sha_authenc_init2);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_init);

static int atmel_sha_authenc_init2(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	struct atmel_sha_reqctx *ctx = &authctx->base;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_hmac_ctx *hmac = crypto_ahash_ctx(tfm);
	size_t hs = ctx->hash_size;
	size_t i, num_words = hs / sizeof(u32);
	u32 mr, msg_size;

	atmel_sha_write(dd, SHA_CR, SHA_CR_WUIHV);
	for (i = 0; i < num_words; ++i)
		atmel_sha_write(dd, SHA_REG_DIN(i), hmac->ipad[i]);

	atmel_sha_write(dd, SHA_CR, SHA_CR_WUIEHV);
	for (i = 0; i < num_words; ++i)
		atmel_sha_write(dd, SHA_REG_DIN(i), hmac->opad[i]);

	mr = (SHA_MR_MODE_IDATAR0 |
	      SHA_MR_HMAC |
	      SHA_MR_DUALBUFF);
	mr |= ctx->flags & SHA_FLAGS_ALGO_MASK;
	atmel_sha_write(dd, SHA_MR, mr);

	msg_size = authctx->assoclen + authctx->textlen;
	atmel_sha_write(dd, SHA_MSR, msg_size);
	atmel_sha_write(dd, SHA_BCR, msg_size);

	atmel_sha_write(dd, SHA_CR, SHA_CR_FIRST);

	/* Process assoc data. */
	return atmel_sha_cpu_start(dd, authctx->assoc, authctx->assoclen,
				   true, false,
				   atmel_sha_authenc_init_done);
}

static int atmel_sha_authenc_init_done(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);

	return authctx->cb(authctx->aes_dev, 0, dd->is_async);
}

int atmel_sha_authenc_final(struct ahash_request *req,
			    u32 *digest, unsigned int digestlen,
			    atmel_aes_authenc_fn_t cb,
			    struct atmel_aes_dev *aes_dev)
{
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	struct atmel_sha_reqctx *ctx = &authctx->base;
	struct atmel_sha_dev *dd = ctx->dd;

	switch (ctx->flags & SHA_FLAGS_ALGO_MASK) {
	case SHA_FLAGS_SHA1:
		authctx->digestlen = SHA1_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA224:
		authctx->digestlen = SHA224_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA256:
		authctx->digestlen = SHA256_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA384:
		authctx->digestlen = SHA384_DIGEST_SIZE;
		break;

	case SHA_FLAGS_SHA512:
		authctx->digestlen = SHA512_DIGEST_SIZE;
		break;

	default:
		return atmel_sha_complete(dd, -EINVAL);
	}
	if (authctx->digestlen > digestlen)
		authctx->digestlen = digestlen;

	authctx->cb = cb;
	authctx->aes_dev = aes_dev;
	authctx->digest = digest;
	return atmel_sha_wait_for_data_ready(dd,
					     atmel_sha_authenc_final_done);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_final);

static int atmel_sha_authenc_final_done(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	size_t i, num_words = authctx->digestlen / sizeof(u32);

	for (i = 0; i < num_words; ++i)
		authctx->digest[i] = atmel_sha_read(dd, SHA_REG_DIGEST(i));

	return atmel_sha_complete(dd, 0);
}

void atmel_sha_authenc_abort(struct ahash_request *req)
{
	struct atmel_sha_authenc_reqctx *authctx = ahash_request_ctx(req);
	struct atmel_sha_reqctx *ctx = &authctx->base;
	struct atmel_sha_dev *dd = ctx->dd;

	/* Prevent atmel_sha_complete() from calling req->base.complete(). */
	dd->is_async = false;
	dd->force_complete = false;
	(void)atmel_sha_complete(dd, 0);
}
EXPORT_SYMBOL_GPL(atmel_sha_authenc_abort);

#endif /* CONFIG_CRYPTO_DEV_ATMEL_AUTHENC */


static void atmel_sha_unregister_algs(struct atmel_sha_dev *dd)
{
	int i;

	if (dd->caps.has_hmac)
		for (i = 0; i < ARRAY_SIZE(sha_hmac_algs); i++)
			crypto_unregister_ahash(&sha_hmac_algs[i]);

	for (i = 0; i < ARRAY_SIZE(sha_1_256_algs); i++)
		crypto_unregister_ahash(&sha_1_256_algs[i]);

	if (dd->caps.has_sha224)
		crypto_unregister_ahash(&sha_224_alg);

	if (dd->caps.has_sha_384_512) {
		for (i = 0; i < ARRAY_SIZE(sha_384_512_algs); i++)
			crypto_unregister_ahash(&sha_384_512_algs[i]);
	}
}

static int atmel_sha_register_algs(struct atmel_sha_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(sha_1_256_algs); i++) {
		atmel_sha_alg_init(&sha_1_256_algs[i]);

		err = crypto_register_ahash(&sha_1_256_algs[i]);
		if (err)
			goto err_sha_1_256_algs;
	}

	if (dd->caps.has_sha224) {
		atmel_sha_alg_init(&sha_224_alg);

		err = crypto_register_ahash(&sha_224_alg);
		if (err)
			goto err_sha_224_algs;
	}

	if (dd->caps.has_sha_384_512) {
		for (i = 0; i < ARRAY_SIZE(sha_384_512_algs); i++) {
			atmel_sha_alg_init(&sha_384_512_algs[i]);

			err = crypto_register_ahash(&sha_384_512_algs[i]);
			if (err)
				goto err_sha_384_512_algs;
		}
	}

	if (dd->caps.has_hmac) {
		for (i = 0; i < ARRAY_SIZE(sha_hmac_algs); i++) {
			atmel_sha_hmac_alg_init(&sha_hmac_algs[i]);

			err = crypto_register_ahash(&sha_hmac_algs[i]);
			if (err)
				goto err_sha_hmac_algs;
		}
	}

	return 0;

	/*i = ARRAY_SIZE(sha_hmac_algs);*/
err_sha_hmac_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(&sha_hmac_algs[j]);
	i = ARRAY_SIZE(sha_384_512_algs);
err_sha_384_512_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(&sha_384_512_algs[j]);
	crypto_unregister_ahash(&sha_224_alg);
err_sha_224_algs:
	i = ARRAY_SIZE(sha_1_256_algs);
err_sha_1_256_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(&sha_1_256_algs[j]);

	return err;
}

static int atmel_sha_dma_init(struct atmel_sha_dev *dd)
{
	dd->dma_lch_in.chan = dma_request_chan(dd->dev, "tx");
	if (IS_ERR(dd->dma_lch_in.chan)) {
		return dev_err_probe(dd->dev, PTR_ERR(dd->dma_lch_in.chan),
			"DMA channel is not available\n");
	}

	dd->dma_lch_in.dma_conf.dst_addr = dd->phys_base +
		SHA_REG_DIN(0);
	dd->dma_lch_in.dma_conf.src_maxburst = 1;
	dd->dma_lch_in.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.dst_maxburst = 1;
	dd->dma_lch_in.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.device_fc = false;

	return 0;
}

static void atmel_sha_dma_cleanup(struct atmel_sha_dev *dd)
{
	dma_release_channel(dd->dma_lch_in.chan);
}

static void atmel_sha_get_cap(struct atmel_sha_dev *dd)
{

	dd->caps.has_dma = 0;
	dd->caps.has_dualbuff = 0;
	dd->caps.has_sha224 = 0;
	dd->caps.has_sha_384_512 = 0;
	dd->caps.has_uihv = 0;
	dd->caps.has_hmac = 0;

	/* keep only major version number */
	switch (dd->hw_version & 0xff0) {
	case 0x700:
	case 0x600:
	case 0x510:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		dd->caps.has_sha_384_512 = 1;
		dd->caps.has_uihv = 1;
		dd->caps.has_hmac = 1;
		break;
	case 0x420:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		dd->caps.has_sha_384_512 = 1;
		dd->caps.has_uihv = 1;
		break;
	case 0x410:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		dd->caps.has_sha_384_512 = 1;
		break;
	case 0x400:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		break;
	case 0x320:
		break;
	default:
		dev_warn(dd->dev,
				"Unmanaged sha version, set minimum capabilities\n");
		break;
	}
}

static const struct of_device_id atmel_sha_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g46-sha" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_sha_dt_ids);

static int atmel_sha_probe(struct platform_device *pdev)
{
	struct atmel_sha_dev *sha_dd;
	struct device *dev = &pdev->dev;
	struct resource *sha_res;
	int err;

	sha_dd = devm_kzalloc(&pdev->dev, sizeof(*sha_dd), GFP_KERNEL);
	if (!sha_dd)
		return -ENOMEM;

	sha_dd->dev = dev;

	platform_set_drvdata(pdev, sha_dd);

	INIT_LIST_HEAD(&sha_dd->list);
	spin_lock_init(&sha_dd->lock);

	tasklet_init(&sha_dd->done_task, atmel_sha_done_task,
					(unsigned long)sha_dd);
	tasklet_init(&sha_dd->queue_task, atmel_sha_queue_task,
					(unsigned long)sha_dd);

	crypto_init_queue(&sha_dd->queue, ATMEL_SHA_QUEUE_LENGTH);

	sha_dd->io_base = devm_platform_get_and_ioremap_resource(pdev, 0, &sha_res);
	if (IS_ERR(sha_dd->io_base)) {
		err = PTR_ERR(sha_dd->io_base);
		goto err_tasklet_kill;
	}
	sha_dd->phys_base = sha_res->start;

	/* Get the IRQ */
	sha_dd->irq = platform_get_irq(pdev,  0);
	if (sha_dd->irq < 0) {
		err = sha_dd->irq;
		goto err_tasklet_kill;
	}

	err = devm_request_irq(&pdev->dev, sha_dd->irq, atmel_sha_irq,
			       IRQF_SHARED, "atmel-sha", sha_dd);
	if (err) {
		dev_err(dev, "unable to request sha irq.\n");
		goto err_tasklet_kill;
	}

	/* Initializing the clock */
	sha_dd->iclk = devm_clk_get_prepared(&pdev->dev, "sha_clk");
	if (IS_ERR(sha_dd->iclk)) {
		dev_err(dev, "clock initialization failed.\n");
		err = PTR_ERR(sha_dd->iclk);
		goto err_tasklet_kill;
	}

	err = atmel_sha_hw_version_init(sha_dd);
	if (err)
		goto err_tasklet_kill;

	atmel_sha_get_cap(sha_dd);

	if (sha_dd->caps.has_dma) {
		err = atmel_sha_dma_init(sha_dd);
		if (err)
			goto err_tasklet_kill;

		dev_info(dev, "using %s for DMA transfers\n",
				dma_chan_name(sha_dd->dma_lch_in.chan));
	}

	spin_lock(&atmel_sha.lock);
	list_add_tail(&sha_dd->list, &atmel_sha.dev_list);
	spin_unlock(&atmel_sha.lock);

	err = atmel_sha_register_algs(sha_dd);
	if (err)
		goto err_algs;

	dev_info(dev, "Atmel SHA1/SHA256%s%s\n",
			sha_dd->caps.has_sha224 ? "/SHA224" : "",
			sha_dd->caps.has_sha_384_512 ? "/SHA384/SHA512" : "");

	return 0;

err_algs:
	spin_lock(&atmel_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&atmel_sha.lock);
	if (sha_dd->caps.has_dma)
		atmel_sha_dma_cleanup(sha_dd);
err_tasklet_kill:
	tasklet_kill(&sha_dd->queue_task);
	tasklet_kill(&sha_dd->done_task);

	return err;
}

static void atmel_sha_remove(struct platform_device *pdev)
{
	struct atmel_sha_dev *sha_dd = platform_get_drvdata(pdev);

	spin_lock(&atmel_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&atmel_sha.lock);

	atmel_sha_unregister_algs(sha_dd);

	tasklet_kill(&sha_dd->queue_task);
	tasklet_kill(&sha_dd->done_task);

	if (sha_dd->caps.has_dma)
		atmel_sha_dma_cleanup(sha_dd);
}

static struct platform_driver atmel_sha_driver = {
	.probe		= atmel_sha_probe,
	.remove_new	= atmel_sha_remove,
	.driver		= {
		.name	= "atmel_sha",
		.of_match_table	= atmel_sha_dt_ids,
	},
};

module_platform_driver(atmel_sha_driver);

MODULE_DESCRIPTION("Atmel SHA (1/256/224/384/512) hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
