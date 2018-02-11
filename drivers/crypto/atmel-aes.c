/*
 * Cryptographic API.
 *
 * Support for ATMEL AES HW acceleration.
 *
 * Copyright (c) 2012 Eukréa Electromatique - ATMEL
 * Author: Nicolas Royer <nicolas@eukrea.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Some ideas are from omap-aes.c driver.
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
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/xts.h>
#include <crypto/internal/aead.h>
#include <linux/platform_data/crypto-atmel.h>
#include <dt-bindings/dma/at91.h>
#include "atmel-aes-regs.h"
#include "atmel-authenc.h"

#define ATMEL_AES_PRIORITY	300

#define ATMEL_AES_BUFFER_ORDER	2
#define ATMEL_AES_BUFFER_SIZE	(PAGE_SIZE << ATMEL_AES_BUFFER_ORDER)

#define CFB8_BLOCK_SIZE		1
#define CFB16_BLOCK_SIZE	2
#define CFB32_BLOCK_SIZE	4
#define CFB64_BLOCK_SIZE	8

#define SIZE_IN_WORDS(x)	((x) >> 2)

/* AES flags */
/* Reserve bits [18:16] [14:12] [1:0] for mode (same as for AES_MR) */
#define AES_FLAGS_ENCRYPT	AES_MR_CYPHER_ENC
#define AES_FLAGS_GTAGEN	AES_MR_GTAGEN
#define AES_FLAGS_OPMODE_MASK	(AES_MR_OPMOD_MASK | AES_MR_CFBS_MASK)
#define AES_FLAGS_ECB		AES_MR_OPMOD_ECB
#define AES_FLAGS_CBC		AES_MR_OPMOD_CBC
#define AES_FLAGS_OFB		AES_MR_OPMOD_OFB
#define AES_FLAGS_CFB128	(AES_MR_OPMOD_CFB | AES_MR_CFBS_128b)
#define AES_FLAGS_CFB64		(AES_MR_OPMOD_CFB | AES_MR_CFBS_64b)
#define AES_FLAGS_CFB32		(AES_MR_OPMOD_CFB | AES_MR_CFBS_32b)
#define AES_FLAGS_CFB16		(AES_MR_OPMOD_CFB | AES_MR_CFBS_16b)
#define AES_FLAGS_CFB8		(AES_MR_OPMOD_CFB | AES_MR_CFBS_8b)
#define AES_FLAGS_CTR		AES_MR_OPMOD_CTR
#define AES_FLAGS_GCM		AES_MR_OPMOD_GCM
#define AES_FLAGS_XTS		AES_MR_OPMOD_XTS

#define AES_FLAGS_MODE_MASK	(AES_FLAGS_OPMODE_MASK |	\
				 AES_FLAGS_ENCRYPT |		\
				 AES_FLAGS_GTAGEN)

#define AES_FLAGS_BUSY		BIT(3)
#define AES_FLAGS_DUMP_REG	BIT(4)
#define AES_FLAGS_OWN_SHA	BIT(5)

#define AES_FLAGS_PERSISTENT	AES_FLAGS_BUSY

#define ATMEL_AES_QUEUE_LENGTH	50

#define ATMEL_AES_DMA_THRESHOLD		256


struct atmel_aes_caps {
	bool			has_dualbuff;
	bool			has_cfb64;
	bool			has_ctr32;
	bool			has_gcm;
	bool			has_xts;
	bool			has_authenc;
	u32			max_burst_size;
};

struct atmel_aes_dev;


typedef int (*atmel_aes_fn_t)(struct atmel_aes_dev *);


struct atmel_aes_base_ctx {
	struct atmel_aes_dev	*dd;
	atmel_aes_fn_t		start;
	int			keylen;
	u32			key[AES_KEYSIZE_256 / sizeof(u32)];
	u16			block_size;
	bool			is_aead;
};

struct atmel_aes_ctx {
	struct atmel_aes_base_ctx	base;
};

struct atmel_aes_ctr_ctx {
	struct atmel_aes_base_ctx	base;

	u32			iv[AES_BLOCK_SIZE / sizeof(u32)];
	size_t			offset;
	struct scatterlist	src[2];
	struct scatterlist	dst[2];
};

struct atmel_aes_gcm_ctx {
	struct atmel_aes_base_ctx	base;

	struct scatterlist	src[2];
	struct scatterlist	dst[2];

	u32			j0[AES_BLOCK_SIZE / sizeof(u32)];
	u32			tag[AES_BLOCK_SIZE / sizeof(u32)];
	u32			ghash[AES_BLOCK_SIZE / sizeof(u32)];
	size_t			textlen;

	const u32		*ghash_in;
	u32			*ghash_out;
	atmel_aes_fn_t		ghash_resume;
};

struct atmel_aes_xts_ctx {
	struct atmel_aes_base_ctx	base;

	u32			key2[AES_KEYSIZE_256 / sizeof(u32)];
};

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
struct atmel_aes_authenc_ctx {
	struct atmel_aes_base_ctx	base;
	struct atmel_sha_authenc_ctx	*auth;
};
#endif

struct atmel_aes_reqctx {
	unsigned long		mode;
	u32			lastc[AES_BLOCK_SIZE / sizeof(u32)];
};

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
struct atmel_aes_authenc_reqctx {
	struct atmel_aes_reqctx	base;

	struct scatterlist	src[2];
	struct scatterlist	dst[2];
	size_t			textlen;
	u32			digest[SHA512_DIGEST_SIZE / sizeof(u32)];

	/* auth_req MUST be place last. */
	struct ahash_request	auth_req;
};
#endif

struct atmel_aes_dma {
	struct dma_chan		*chan;
	struct scatterlist	*sg;
	int			nents;
	unsigned int		remainder;
	unsigned int		sg_len;
};

struct atmel_aes_dev {
	struct list_head	list;
	unsigned long		phys_base;
	void __iomem		*io_base;

	struct crypto_async_request	*areq;
	struct atmel_aes_base_ctx	*ctx;

	bool			is_async;
	atmel_aes_fn_t		resume;
	atmel_aes_fn_t		cpu_transfer_complete;

	struct device		*dev;
	struct clk		*iclk;
	int			irq;

	unsigned long		flags;

	spinlock_t		lock;
	struct crypto_queue	queue;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	size_t			total;
	size_t			datalen;
	u32			*data;

	struct atmel_aes_dma	src;
	struct atmel_aes_dma	dst;

	size_t			buflen;
	void			*buf;
	struct scatterlist	aligned_sg;
	struct scatterlist	*real_dst;

	struct atmel_aes_caps	caps;

	u32			hw_version;
};

struct atmel_aes_drv {
	struct list_head	dev_list;
	spinlock_t		lock;
};

static struct atmel_aes_drv atmel_aes = {
	.dev_list = LIST_HEAD_INIT(atmel_aes.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(atmel_aes.lock),
};

#ifdef VERBOSE_DEBUG
static const char *atmel_aes_reg_name(u32 offset, char *tmp, size_t sz)
{
	switch (offset) {
	case AES_CR:
		return "CR";

	case AES_MR:
		return "MR";

	case AES_ISR:
		return "ISR";

	case AES_IMR:
		return "IMR";

	case AES_IER:
		return "IER";

	case AES_IDR:
		return "IDR";

	case AES_KEYWR(0):
	case AES_KEYWR(1):
	case AES_KEYWR(2):
	case AES_KEYWR(3):
	case AES_KEYWR(4):
	case AES_KEYWR(5):
	case AES_KEYWR(6):
	case AES_KEYWR(7):
		snprintf(tmp, sz, "KEYWR[%u]", (offset - AES_KEYWR(0)) >> 2);
		break;

	case AES_IDATAR(0):
	case AES_IDATAR(1):
	case AES_IDATAR(2):
	case AES_IDATAR(3):
		snprintf(tmp, sz, "IDATAR[%u]", (offset - AES_IDATAR(0)) >> 2);
		break;

	case AES_ODATAR(0):
	case AES_ODATAR(1):
	case AES_ODATAR(2):
	case AES_ODATAR(3):
		snprintf(tmp, sz, "ODATAR[%u]", (offset - AES_ODATAR(0)) >> 2);
		break;

	case AES_IVR(0):
	case AES_IVR(1):
	case AES_IVR(2):
	case AES_IVR(3):
		snprintf(tmp, sz, "IVR[%u]", (offset - AES_IVR(0)) >> 2);
		break;

	case AES_AADLENR:
		return "AADLENR";

	case AES_CLENR:
		return "CLENR";

	case AES_GHASHR(0):
	case AES_GHASHR(1):
	case AES_GHASHR(2):
	case AES_GHASHR(3):
		snprintf(tmp, sz, "GHASHR[%u]", (offset - AES_GHASHR(0)) >> 2);
		break;

	case AES_TAGR(0):
	case AES_TAGR(1):
	case AES_TAGR(2):
	case AES_TAGR(3):
		snprintf(tmp, sz, "TAGR[%u]", (offset - AES_TAGR(0)) >> 2);
		break;

	case AES_CTRR:
		return "CTRR";

	case AES_GCMHR(0):
	case AES_GCMHR(1):
	case AES_GCMHR(2):
	case AES_GCMHR(3):
		snprintf(tmp, sz, "GCMHR[%u]", (offset - AES_GCMHR(0)) >> 2);
		break;

	case AES_EMR:
		return "EMR";

	case AES_TWR(0):
	case AES_TWR(1):
	case AES_TWR(2):
	case AES_TWR(3):
		snprintf(tmp, sz, "TWR[%u]", (offset - AES_TWR(0)) >> 2);
		break;

	case AES_ALPHAR(0):
	case AES_ALPHAR(1):
	case AES_ALPHAR(2):
	case AES_ALPHAR(3):
		snprintf(tmp, sz, "ALPHAR[%u]", (offset - AES_ALPHAR(0)) >> 2);
		break;

	default:
		snprintf(tmp, sz, "0x%02x", offset);
		break;
	}

	return tmp;
}
#endif /* VERBOSE_DEBUG */

/* Shared functions */

static inline u32 atmel_aes_read(struct atmel_aes_dev *dd, u32 offset)
{
	u32 value = readl_relaxed(dd->io_base + offset);

#ifdef VERBOSE_DEBUG
	if (dd->flags & AES_FLAGS_DUMP_REG) {
		char tmp[16];

		dev_vdbg(dd->dev, "read 0x%08x from %s\n", value,
			 atmel_aes_reg_name(offset, tmp, sizeof(tmp)));
	}
#endif /* VERBOSE_DEBUG */

	return value;
}

static inline void atmel_aes_write(struct atmel_aes_dev *dd,
					u32 offset, u32 value)
{
#ifdef VERBOSE_DEBUG
	if (dd->flags & AES_FLAGS_DUMP_REG) {
		char tmp[16];

		dev_vdbg(dd->dev, "write 0x%08x into %s\n", value,
			 atmel_aes_reg_name(offset, tmp, sizeof(tmp)));
	}
#endif /* VERBOSE_DEBUG */

	writel_relaxed(value, dd->io_base + offset);
}

static void atmel_aes_read_n(struct atmel_aes_dev *dd, u32 offset,
					u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		*value = atmel_aes_read(dd, offset);
}

static void atmel_aes_write_n(struct atmel_aes_dev *dd, u32 offset,
			      const u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		atmel_aes_write(dd, offset, *value);
}

static inline void atmel_aes_read_block(struct atmel_aes_dev *dd, u32 offset,
					u32 *value)
{
	atmel_aes_read_n(dd, offset, value, SIZE_IN_WORDS(AES_BLOCK_SIZE));
}

static inline void atmel_aes_write_block(struct atmel_aes_dev *dd, u32 offset,
					 const u32 *value)
{
	atmel_aes_write_n(dd, offset, value, SIZE_IN_WORDS(AES_BLOCK_SIZE));
}

static inline int atmel_aes_wait_for_data_ready(struct atmel_aes_dev *dd,
						atmel_aes_fn_t resume)
{
	u32 isr = atmel_aes_read(dd, AES_ISR);

	if (unlikely(isr & AES_INT_DATARDY))
		return resume(dd);

	dd->resume = resume;
	atmel_aes_write(dd, AES_IER, AES_INT_DATARDY);
	return -EINPROGRESS;
}

static inline size_t atmel_aes_padlen(size_t len, size_t block_size)
{
	len &= block_size - 1;
	return len ? block_size - len : 0;
}

static struct atmel_aes_dev *atmel_aes_find_dev(struct atmel_aes_base_ctx *ctx)
{
	struct atmel_aes_dev *aes_dd = NULL;
	struct atmel_aes_dev *tmp;

	spin_lock_bh(&atmel_aes.lock);
	if (!ctx->dd) {
		list_for_each_entry(tmp, &atmel_aes.dev_list, list) {
			aes_dd = tmp;
			break;
		}
		ctx->dd = aes_dd;
	} else {
		aes_dd = ctx->dd;
	}

	spin_unlock_bh(&atmel_aes.lock);

	return aes_dd;
}

static int atmel_aes_hw_init(struct atmel_aes_dev *dd)
{
	int err;

	err = clk_enable(dd->iclk);
	if (err)
		return err;

	atmel_aes_write(dd, AES_CR, AES_CR_SWRST);
	atmel_aes_write(dd, AES_MR, 0xE << AES_MR_CKEY_OFFSET);

	return 0;
}

static inline unsigned int atmel_aes_get_version(struct atmel_aes_dev *dd)
{
	return atmel_aes_read(dd, AES_HW_VERSION) & 0x00000fff;
}

static int atmel_aes_hw_version_init(struct atmel_aes_dev *dd)
{
	int err;

	err = atmel_aes_hw_init(dd);
	if (err)
		return err;

	dd->hw_version = atmel_aes_get_version(dd);

	dev_info(dd->dev, "version: 0x%x\n", dd->hw_version);

	clk_disable(dd->iclk);
	return 0;
}

static inline void atmel_aes_set_mode(struct atmel_aes_dev *dd,
				      const struct atmel_aes_reqctx *rctx)
{
	/* Clear all but persistent flags and set request flags. */
	dd->flags = (dd->flags & AES_FLAGS_PERSISTENT) | rctx->mode;
}

static inline bool atmel_aes_is_encrypt(const struct atmel_aes_dev *dd)
{
	return (dd->flags & AES_FLAGS_ENCRYPT);
}

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
static void atmel_aes_authenc_complete(struct atmel_aes_dev *dd, int err);
#endif

static inline int atmel_aes_complete(struct atmel_aes_dev *dd, int err)
{
#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
	if (dd->ctx->is_aead)
		atmel_aes_authenc_complete(dd, err);
#endif

	clk_disable(dd->iclk);
	dd->flags &= ~AES_FLAGS_BUSY;

	if (!dd->ctx->is_aead) {
		struct ablkcipher_request *req =
			ablkcipher_request_cast(dd->areq);
		struct atmel_aes_reqctx *rctx = ablkcipher_request_ctx(req);
		struct crypto_ablkcipher *ablkcipher =
			crypto_ablkcipher_reqtfm(req);
		int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

		if (rctx->mode & AES_FLAGS_ENCRYPT) {
			scatterwalk_map_and_copy(req->info, req->dst,
				req->nbytes - ivsize, ivsize, 0);
		} else {
			if (req->src == req->dst) {
				memcpy(req->info, rctx->lastc, ivsize);
			} else {
				scatterwalk_map_and_copy(req->info, req->src,
					req->nbytes - ivsize, ivsize, 0);
			}
		}
	}

	if (dd->is_async)
		dd->areq->complete(dd->areq, err);

	tasklet_schedule(&dd->queue_task);

	return err;
}

static void atmel_aes_write_ctrl_key(struct atmel_aes_dev *dd, bool use_dma,
				     const u32 *iv, const u32 *key, int keylen)
{
	u32 valmr = 0;

	/* MR register must be set before IV registers */
	if (keylen == AES_KEYSIZE_128)
		valmr |= AES_MR_KEYSIZE_128;
	else if (keylen == AES_KEYSIZE_192)
		valmr |= AES_MR_KEYSIZE_192;
	else
		valmr |= AES_MR_KEYSIZE_256;

	valmr |= dd->flags & AES_FLAGS_MODE_MASK;

	if (use_dma) {
		valmr |= AES_MR_SMOD_IDATAR0;
		if (dd->caps.has_dualbuff)
			valmr |= AES_MR_DUALBUFF;
	} else {
		valmr |= AES_MR_SMOD_AUTO;
	}

	atmel_aes_write(dd, AES_MR, valmr);

	atmel_aes_write_n(dd, AES_KEYWR(0), key, SIZE_IN_WORDS(keylen));

	if (iv && (valmr & AES_MR_OPMOD_MASK) != AES_MR_OPMOD_ECB)
		atmel_aes_write_block(dd, AES_IVR(0), iv);
}

static inline void atmel_aes_write_ctrl(struct atmel_aes_dev *dd, bool use_dma,
					const u32 *iv)

{
	atmel_aes_write_ctrl_key(dd, use_dma, iv,
				 dd->ctx->key, dd->ctx->keylen);
}

/* CPU transfer */

static int atmel_aes_cpu_transfer(struct atmel_aes_dev *dd)
{
	int err = 0;
	u32 isr;

	for (;;) {
		atmel_aes_read_block(dd, AES_ODATAR(0), dd->data);
		dd->data += 4;
		dd->datalen -= AES_BLOCK_SIZE;

		if (dd->datalen < AES_BLOCK_SIZE)
			break;

		atmel_aes_write_block(dd, AES_IDATAR(0), dd->data);

		isr = atmel_aes_read(dd, AES_ISR);
		if (!(isr & AES_INT_DATARDY)) {
			dd->resume = atmel_aes_cpu_transfer;
			atmel_aes_write(dd, AES_IER, AES_INT_DATARDY);
			return -EINPROGRESS;
		}
	}

	if (!sg_copy_from_buffer(dd->real_dst, sg_nents(dd->real_dst),
				 dd->buf, dd->total))
		err = -EINVAL;

	if (err)
		return atmel_aes_complete(dd, err);

	return dd->cpu_transfer_complete(dd);
}

static int atmel_aes_cpu_start(struct atmel_aes_dev *dd,
			       struct scatterlist *src,
			       struct scatterlist *dst,
			       size_t len,
			       atmel_aes_fn_t resume)
{
	size_t padlen = atmel_aes_padlen(len, AES_BLOCK_SIZE);

	if (unlikely(len == 0))
		return -EINVAL;

	sg_copy_to_buffer(src, sg_nents(src), dd->buf, len);

	dd->total = len;
	dd->real_dst = dst;
	dd->cpu_transfer_complete = resume;
	dd->datalen = len + padlen;
	dd->data = (u32 *)dd->buf;
	atmel_aes_write_block(dd, AES_IDATAR(0), dd->data);
	return atmel_aes_wait_for_data_ready(dd, atmel_aes_cpu_transfer);
}


/* DMA transfer */

static void atmel_aes_dma_callback(void *data);

static bool atmel_aes_check_aligned(struct atmel_aes_dev *dd,
				    struct scatterlist *sg,
				    size_t len,
				    struct atmel_aes_dma *dma)
{
	int nents;

	if (!IS_ALIGNED(len, dd->ctx->block_size))
		return false;

	for (nents = 0; sg; sg = sg_next(sg), ++nents) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32)))
			return false;

		if (len <= sg->length) {
			if (!IS_ALIGNED(len, dd->ctx->block_size))
				return false;

			dma->nents = nents+1;
			dma->remainder = sg->length - len;
			sg->length = len;
			return true;
		}

		if (!IS_ALIGNED(sg->length, dd->ctx->block_size))
			return false;

		len -= sg->length;
	}

	return false;
}

static inline void atmel_aes_restore_sg(const struct atmel_aes_dma *dma)
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

static int atmel_aes_map(struct atmel_aes_dev *dd,
			 struct scatterlist *src,
			 struct scatterlist *dst,
			 size_t len)
{
	bool src_aligned, dst_aligned;
	size_t padlen;

	dd->total = len;
	dd->src.sg = src;
	dd->dst.sg = dst;
	dd->real_dst = dst;

	src_aligned = atmel_aes_check_aligned(dd, src, len, &dd->src);
	if (src == dst)
		dst_aligned = src_aligned;
	else
		dst_aligned = atmel_aes_check_aligned(dd, dst, len, &dd->dst);
	if (!src_aligned || !dst_aligned) {
		padlen = atmel_aes_padlen(len, dd->ctx->block_size);

		if (dd->buflen < len + padlen)
			return -ENOMEM;

		if (!src_aligned) {
			sg_copy_to_buffer(src, sg_nents(src), dd->buf, len);
			dd->src.sg = &dd->aligned_sg;
			dd->src.nents = 1;
			dd->src.remainder = 0;
		}

		if (!dst_aligned) {
			dd->dst.sg = &dd->aligned_sg;
			dd->dst.nents = 1;
			dd->dst.remainder = 0;
		}

		sg_init_table(&dd->aligned_sg, 1);
		sg_set_buf(&dd->aligned_sg, dd->buf, len + padlen);
	}

	if (dd->src.sg == dd->dst.sg) {
		dd->src.sg_len = dma_map_sg(dd->dev, dd->src.sg, dd->src.nents,
					    DMA_BIDIRECTIONAL);
		dd->dst.sg_len = dd->src.sg_len;
		if (!dd->src.sg_len)
			return -EFAULT;
	} else {
		dd->src.sg_len = dma_map_sg(dd->dev, dd->src.sg, dd->src.nents,
					    DMA_TO_DEVICE);
		if (!dd->src.sg_len)
			return -EFAULT;

		dd->dst.sg_len = dma_map_sg(dd->dev, dd->dst.sg, dd->dst.nents,
					    DMA_FROM_DEVICE);
		if (!dd->dst.sg_len) {
			dma_unmap_sg(dd->dev, dd->src.sg, dd->src.nents,
				     DMA_TO_DEVICE);
			return -EFAULT;
		}
	}

	return 0;
}

static void atmel_aes_unmap(struct atmel_aes_dev *dd)
{
	if (dd->src.sg == dd->dst.sg) {
		dma_unmap_sg(dd->dev, dd->src.sg, dd->src.nents,
			     DMA_BIDIRECTIONAL);

		if (dd->src.sg != &dd->aligned_sg)
			atmel_aes_restore_sg(&dd->src);
	} else {
		dma_unmap_sg(dd->dev, dd->dst.sg, dd->dst.nents,
			     DMA_FROM_DEVICE);

		if (dd->dst.sg != &dd->aligned_sg)
			atmel_aes_restore_sg(&dd->dst);

		dma_unmap_sg(dd->dev, dd->src.sg, dd->src.nents,
			     DMA_TO_DEVICE);

		if (dd->src.sg != &dd->aligned_sg)
			atmel_aes_restore_sg(&dd->src);
	}

	if (dd->dst.sg == &dd->aligned_sg)
		sg_copy_from_buffer(dd->real_dst, sg_nents(dd->real_dst),
				    dd->buf, dd->total);
}

static int atmel_aes_dma_transfer_start(struct atmel_aes_dev *dd,
					enum dma_slave_buswidth addr_width,
					enum dma_transfer_direction dir,
					u32 maxburst)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config;
	dma_async_tx_callback callback;
	struct atmel_aes_dma *dma;
	int err;

	memset(&config, 0, sizeof(config));
	config.direction = dir;
	config.src_addr_width = addr_width;
	config.dst_addr_width = addr_width;
	config.src_maxburst = maxburst;
	config.dst_maxburst = maxburst;

	switch (dir) {
	case DMA_MEM_TO_DEV:
		dma = &dd->src;
		callback = NULL;
		config.dst_addr = dd->phys_base + AES_IDATAR(0);
		break;

	case DMA_DEV_TO_MEM:
		dma = &dd->dst;
		callback = atmel_aes_dma_callback;
		config.src_addr = dd->phys_base + AES_ODATAR(0);
		break;

	default:
		return -EINVAL;
	}

	err = dmaengine_slave_config(dma->chan, &config);
	if (err)
		return err;

	desc = dmaengine_prep_slave_sg(dma->chan, dma->sg, dma->sg_len, dir,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -ENOMEM;

	desc->callback = callback;
	desc->callback_param = dd;
	dmaengine_submit(desc);
	dma_async_issue_pending(dma->chan);

	return 0;
}

static void atmel_aes_dma_transfer_stop(struct atmel_aes_dev *dd,
					enum dma_transfer_direction dir)
{
	struct atmel_aes_dma *dma;

	switch (dir) {
	case DMA_MEM_TO_DEV:
		dma = &dd->src;
		break;

	case DMA_DEV_TO_MEM:
		dma = &dd->dst;
		break;

	default:
		return;
	}

	dmaengine_terminate_all(dma->chan);
}

static int atmel_aes_dma_start(struct atmel_aes_dev *dd,
			       struct scatterlist *src,
			       struct scatterlist *dst,
			       size_t len,
			       atmel_aes_fn_t resume)
{
	enum dma_slave_buswidth addr_width;
	u32 maxburst;
	int err;

	switch (dd->ctx->block_size) {
	case CFB8_BLOCK_SIZE:
		addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		maxburst = 1;
		break;

	case CFB16_BLOCK_SIZE:
		addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		maxburst = 1;
		break;

	case CFB32_BLOCK_SIZE:
	case CFB64_BLOCK_SIZE:
		addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		maxburst = 1;
		break;

	case AES_BLOCK_SIZE:
		addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		maxburst = dd->caps.max_burst_size;
		break;

	default:
		err = -EINVAL;
		goto exit;
	}

	err = atmel_aes_map(dd, src, dst, len);
	if (err)
		goto exit;

	dd->resume = resume;

	/* Set output DMA transfer first */
	err = atmel_aes_dma_transfer_start(dd, addr_width, DMA_DEV_TO_MEM,
					   maxburst);
	if (err)
		goto unmap;

	/* Then set input DMA transfer */
	err = atmel_aes_dma_transfer_start(dd, addr_width, DMA_MEM_TO_DEV,
					   maxburst);
	if (err)
		goto output_transfer_stop;

	return -EINPROGRESS;

output_transfer_stop:
	atmel_aes_dma_transfer_stop(dd, DMA_DEV_TO_MEM);
unmap:
	atmel_aes_unmap(dd);
exit:
	return atmel_aes_complete(dd, err);
}

static void atmel_aes_dma_stop(struct atmel_aes_dev *dd)
{
	atmel_aes_dma_transfer_stop(dd, DMA_MEM_TO_DEV);
	atmel_aes_dma_transfer_stop(dd, DMA_DEV_TO_MEM);
	atmel_aes_unmap(dd);
}

static void atmel_aes_dma_callback(void *data)
{
	struct atmel_aes_dev *dd = data;

	atmel_aes_dma_stop(dd);
	dd->is_async = true;
	(void)dd->resume(dd);
}

static int atmel_aes_handle_queue(struct atmel_aes_dev *dd,
				  struct crypto_async_request *new_areq)
{
	struct crypto_async_request *areq, *backlog;
	struct atmel_aes_base_ctx *ctx;
	unsigned long flags;
	bool start_async;
	int err, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (new_areq)
		ret = crypto_enqueue_request(&dd->queue, new_areq);
	if (dd->flags & AES_FLAGS_BUSY) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&dd->queue);
	areq = crypto_dequeue_request(&dd->queue);
	if (areq)
		dd->flags |= AES_FLAGS_BUSY;
	spin_unlock_irqrestore(&dd->lock, flags);

	if (!areq)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	ctx = crypto_tfm_ctx(areq->tfm);

	dd->areq = areq;
	dd->ctx = ctx;
	start_async = (areq != new_areq);
	dd->is_async = start_async;

	/* WARNING: ctx->start() MAY change dd->is_async. */
	err = ctx->start(dd);
	return (start_async) ? ret : err;
}


/* AES async block ciphers */

static int atmel_aes_transfer_complete(struct atmel_aes_dev *dd)
{
	return atmel_aes_complete(dd, 0);
}

static int atmel_aes_start(struct atmel_aes_dev *dd)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(dd->areq);
	struct atmel_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	bool use_dma = (req->nbytes >= ATMEL_AES_DMA_THRESHOLD ||
			dd->ctx->block_size != AES_BLOCK_SIZE);
	int err;

	atmel_aes_set_mode(dd, rctx);

	err = atmel_aes_hw_init(dd);
	if (err)
		return atmel_aes_complete(dd, err);

	atmel_aes_write_ctrl(dd, use_dma, req->info);
	if (use_dma)
		return atmel_aes_dma_start(dd, req->src, req->dst, req->nbytes,
					   atmel_aes_transfer_complete);

	return atmel_aes_cpu_start(dd, req->src, req->dst, req->nbytes,
				   atmel_aes_transfer_complete);
}

static inline struct atmel_aes_ctr_ctx *
atmel_aes_ctr_ctx_cast(struct atmel_aes_base_ctx *ctx)
{
	return container_of(ctx, struct atmel_aes_ctr_ctx, base);
}

static int atmel_aes_ctr_transfer(struct atmel_aes_dev *dd)
{
	struct atmel_aes_ctr_ctx *ctx = atmel_aes_ctr_ctx_cast(dd->ctx);
	struct ablkcipher_request *req = ablkcipher_request_cast(dd->areq);
	struct scatterlist *src, *dst;
	u32 ctr, blocks;
	size_t datalen;
	bool use_dma, fragmented = false;

	/* Check for transfer completion. */
	ctx->offset += dd->total;
	if (ctx->offset >= req->nbytes)
		return atmel_aes_transfer_complete(dd);

	/* Compute data length. */
	datalen = req->nbytes - ctx->offset;
	blocks = DIV_ROUND_UP(datalen, AES_BLOCK_SIZE);
	ctr = be32_to_cpu(ctx->iv[3]);
	if (dd->caps.has_ctr32) {
		/* Check 32bit counter overflow. */
		u32 start = ctr;
		u32 end = start + blocks - 1;

		if (end < start) {
			ctr |= 0xffffffff;
			datalen = AES_BLOCK_SIZE * -start;
			fragmented = true;
		}
	} else {
		/* Check 16bit counter overflow. */
		u16 start = ctr & 0xffff;
		u16 end = start + (u16)blocks - 1;

		if (blocks >> 16 || end < start) {
			ctr |= 0xffff;
			datalen = AES_BLOCK_SIZE * (0x10000-start);
			fragmented = true;
		}
	}
	use_dma = (datalen >= ATMEL_AES_DMA_THRESHOLD);

	/* Jump to offset. */
	src = scatterwalk_ffwd(ctx->src, req->src, ctx->offset);
	dst = ((req->src == req->dst) ? src :
	       scatterwalk_ffwd(ctx->dst, req->dst, ctx->offset));

	/* Configure hardware. */
	atmel_aes_write_ctrl(dd, use_dma, ctx->iv);
	if (unlikely(fragmented)) {
		/*
		 * Increment the counter manually to cope with the hardware
		 * counter overflow.
		 */
		ctx->iv[3] = cpu_to_be32(ctr);
		crypto_inc((u8 *)ctx->iv, AES_BLOCK_SIZE);
	}

	if (use_dma)
		return atmel_aes_dma_start(dd, src, dst, datalen,
					   atmel_aes_ctr_transfer);

	return atmel_aes_cpu_start(dd, src, dst, datalen,
				   atmel_aes_ctr_transfer);
}

static int atmel_aes_ctr_start(struct atmel_aes_dev *dd)
{
	struct atmel_aes_ctr_ctx *ctx = atmel_aes_ctr_ctx_cast(dd->ctx);
	struct ablkcipher_request *req = ablkcipher_request_cast(dd->areq);
	struct atmel_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	int err;

	atmel_aes_set_mode(dd, rctx);

	err = atmel_aes_hw_init(dd);
	if (err)
		return atmel_aes_complete(dd, err);

	memcpy(ctx->iv, req->info, AES_BLOCK_SIZE);
	ctx->offset = 0;
	dd->total = 0;
	return atmel_aes_ctr_transfer(dd);
}

static int atmel_aes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct atmel_aes_base_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct atmel_aes_reqctx *rctx;
	struct atmel_aes_dev *dd;

	switch (mode & AES_FLAGS_OPMODE_MASK) {
	case AES_FLAGS_CFB8:
		ctx->block_size = CFB8_BLOCK_SIZE;
		break;

	case AES_FLAGS_CFB16:
		ctx->block_size = CFB16_BLOCK_SIZE;
		break;

	case AES_FLAGS_CFB32:
		ctx->block_size = CFB32_BLOCK_SIZE;
		break;

	case AES_FLAGS_CFB64:
		ctx->block_size = CFB64_BLOCK_SIZE;
		break;

	default:
		ctx->block_size = AES_BLOCK_SIZE;
		break;
	}
	ctx->is_aead = false;

	dd = atmel_aes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	rctx = ablkcipher_request_ctx(req);
	rctx->mode = mode;

	if (!(mode & AES_FLAGS_ENCRYPT) && (req->src == req->dst)) {
		int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

		scatterwalk_map_and_copy(rctx->lastc, req->src,
			(req->nbytes - ivsize), ivsize, 0);
	}

	return atmel_aes_handle_queue(dd, &req->base);
}

static int atmel_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct atmel_aes_base_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_ECB | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_ECB);
}

static int atmel_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CBC | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CBC);
}

static int atmel_aes_ofb_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_OFB | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_ofb_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_OFB);
}

static int atmel_aes_cfb_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB128 | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_cfb_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB128);
}

static int atmel_aes_cfb64_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB64 | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_cfb64_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB64);
}

static int atmel_aes_cfb32_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB32 | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_cfb32_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB32);
}

static int atmel_aes_cfb16_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB16 | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_cfb16_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB16);
}

static int atmel_aes_cfb8_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB8 | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_cfb8_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CFB8);
}

static int atmel_aes_ctr_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CTR | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_ctr_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_CTR);
}

static int atmel_aes_cra_init(struct crypto_tfm *tfm)
{
	struct atmel_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	tfm->crt_ablkcipher.reqsize = sizeof(struct atmel_aes_reqctx);
	ctx->base.start = atmel_aes_start;

	return 0;
}

static int atmel_aes_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct atmel_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	tfm->crt_ablkcipher.reqsize = sizeof(struct atmel_aes_reqctx);
	ctx->base.start = atmel_aes_ctr_start;

	return 0;
}

static struct crypto_alg aes_algs[] = {
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "atmel-ecb-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_ecb_encrypt,
		.decrypt	= atmel_aes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "atmel-cbc-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cbc_encrypt,
		.decrypt	= atmel_aes_cbc_decrypt,
	}
},
{
	.cra_name		= "ofb(aes)",
	.cra_driver_name	= "atmel-ofb-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_ofb_encrypt,
		.decrypt	= atmel_aes_ofb_decrypt,
	}
},
{
	.cra_name		= "cfb(aes)",
	.cra_driver_name	= "atmel-cfb-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb_encrypt,
		.decrypt	= atmel_aes_cfb_decrypt,
	}
},
{
	.cra_name		= "cfb32(aes)",
	.cra_driver_name	= "atmel-cfb32-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB32_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x3,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb32_encrypt,
		.decrypt	= atmel_aes_cfb32_decrypt,
	}
},
{
	.cra_name		= "cfb16(aes)",
	.cra_driver_name	= "atmel-cfb16-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB16_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x1,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb16_encrypt,
		.decrypt	= atmel_aes_cfb16_decrypt,
	}
},
{
	.cra_name		= "cfb8(aes)",
	.cra_driver_name	= "atmel-cfb8-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB8_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb8_encrypt,
		.decrypt	= atmel_aes_cfb8_decrypt,
	}
},
{
	.cra_name		= "ctr(aes)",
	.cra_driver_name	= "atmel-ctr-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctr_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_ctr_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_ctr_encrypt,
		.decrypt	= atmel_aes_ctr_decrypt,
	}
},
};

static struct crypto_alg aes_cfb64_alg = {
	.cra_name		= "cfb64(aes)",
	.cra_driver_name	= "atmel-cfb64-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB64_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb64_encrypt,
		.decrypt	= atmel_aes_cfb64_decrypt,
	}
};


/* gcm aead functions */

static int atmel_aes_gcm_ghash(struct atmel_aes_dev *dd,
			       const u32 *data, size_t datalen,
			       const u32 *ghash_in, u32 *ghash_out,
			       atmel_aes_fn_t resume);
static int atmel_aes_gcm_ghash_init(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_ghash_finalize(struct atmel_aes_dev *dd);

static int atmel_aes_gcm_start(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_process(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_length(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_data(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_tag_init(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_tag(struct atmel_aes_dev *dd);
static int atmel_aes_gcm_finalize(struct atmel_aes_dev *dd);

static inline struct atmel_aes_gcm_ctx *
atmel_aes_gcm_ctx_cast(struct atmel_aes_base_ctx *ctx)
{
	return container_of(ctx, struct atmel_aes_gcm_ctx, base);
}

static int atmel_aes_gcm_ghash(struct atmel_aes_dev *dd,
			       const u32 *data, size_t datalen,
			       const u32 *ghash_in, u32 *ghash_out,
			       atmel_aes_fn_t resume)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);

	dd->data = (u32 *)data;
	dd->datalen = datalen;
	ctx->ghash_in = ghash_in;
	ctx->ghash_out = ghash_out;
	ctx->ghash_resume = resume;

	atmel_aes_write_ctrl(dd, false, NULL);
	return atmel_aes_wait_for_data_ready(dd, atmel_aes_gcm_ghash_init);
}

static int atmel_aes_gcm_ghash_init(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);

	/* Set the data length. */
	atmel_aes_write(dd, AES_AADLENR, dd->total);
	atmel_aes_write(dd, AES_CLENR, 0);

	/* If needed, overwrite the GCM Intermediate Hash Word Registers */
	if (ctx->ghash_in)
		atmel_aes_write_block(dd, AES_GHASHR(0), ctx->ghash_in);

	return atmel_aes_gcm_ghash_finalize(dd);
}

static int atmel_aes_gcm_ghash_finalize(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	u32 isr;

	/* Write data into the Input Data Registers. */
	while (dd->datalen > 0) {
		atmel_aes_write_block(dd, AES_IDATAR(0), dd->data);
		dd->data += 4;
		dd->datalen -= AES_BLOCK_SIZE;

		isr = atmel_aes_read(dd, AES_ISR);
		if (!(isr & AES_INT_DATARDY)) {
			dd->resume = atmel_aes_gcm_ghash_finalize;
			atmel_aes_write(dd, AES_IER, AES_INT_DATARDY);
			return -EINPROGRESS;
		}
	}

	/* Read the computed hash from GHASHRx. */
	atmel_aes_read_block(dd, AES_GHASHR(0), ctx->ghash_out);

	return ctx->ghash_resume(dd);
}


static int atmel_aes_gcm_start(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	struct aead_request *req = aead_request_cast(dd->areq);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct atmel_aes_reqctx *rctx = aead_request_ctx(req);
	size_t ivsize = crypto_aead_ivsize(tfm);
	size_t datalen, padlen;
	const void *iv = req->iv;
	u8 *data = dd->buf;
	int err;

	atmel_aes_set_mode(dd, rctx);

	err = atmel_aes_hw_init(dd);
	if (err)
		return atmel_aes_complete(dd, err);

	if (likely(ivsize == GCM_AES_IV_SIZE)) {
		memcpy(ctx->j0, iv, ivsize);
		ctx->j0[3] = cpu_to_be32(1);
		return atmel_aes_gcm_process(dd);
	}

	padlen = atmel_aes_padlen(ivsize, AES_BLOCK_SIZE);
	datalen = ivsize + padlen + AES_BLOCK_SIZE;
	if (datalen > dd->buflen)
		return atmel_aes_complete(dd, -EINVAL);

	memcpy(data, iv, ivsize);
	memset(data + ivsize, 0, padlen + sizeof(u64));
	((u64 *)(data + datalen))[-1] = cpu_to_be64(ivsize * 8);

	return atmel_aes_gcm_ghash(dd, (const u32 *)data, datalen,
				   NULL, ctx->j0, atmel_aes_gcm_process);
}

static int atmel_aes_gcm_process(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	struct aead_request *req = aead_request_cast(dd->areq);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	bool enc = atmel_aes_is_encrypt(dd);
	u32 authsize;

	/* Compute text length. */
	authsize = crypto_aead_authsize(tfm);
	ctx->textlen = req->cryptlen - (enc ? 0 : authsize);

	/*
	 * According to tcrypt test suite, the GCM Automatic Tag Generation
	 * fails when both the message and its associated data are empty.
	 */
	if (likely(req->assoclen != 0 || ctx->textlen != 0))
		dd->flags |= AES_FLAGS_GTAGEN;

	atmel_aes_write_ctrl(dd, false, NULL);
	return atmel_aes_wait_for_data_ready(dd, atmel_aes_gcm_length);
}

static int atmel_aes_gcm_length(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	struct aead_request *req = aead_request_cast(dd->areq);
	u32 j0_lsw, *j0 = ctx->j0;
	size_t padlen;

	/* Write incr32(J0) into IV. */
	j0_lsw = j0[3];
	j0[3] = cpu_to_be32(be32_to_cpu(j0[3]) + 1);
	atmel_aes_write_block(dd, AES_IVR(0), j0);
	j0[3] = j0_lsw;

	/* Set aad and text lengths. */
	atmel_aes_write(dd, AES_AADLENR, req->assoclen);
	atmel_aes_write(dd, AES_CLENR, ctx->textlen);

	/* Check whether AAD are present. */
	if (unlikely(req->assoclen == 0)) {
		dd->datalen = 0;
		return atmel_aes_gcm_data(dd);
	}

	/* Copy assoc data and add padding. */
	padlen = atmel_aes_padlen(req->assoclen, AES_BLOCK_SIZE);
	if (unlikely(req->assoclen + padlen > dd->buflen))
		return atmel_aes_complete(dd, -EINVAL);
	sg_copy_to_buffer(req->src, sg_nents(req->src), dd->buf, req->assoclen);

	/* Write assoc data into the Input Data register. */
	dd->data = (u32 *)dd->buf;
	dd->datalen = req->assoclen + padlen;
	return atmel_aes_gcm_data(dd);
}

static int atmel_aes_gcm_data(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	struct aead_request *req = aead_request_cast(dd->areq);
	bool use_dma = (ctx->textlen >= ATMEL_AES_DMA_THRESHOLD);
	struct scatterlist *src, *dst;
	u32 isr, mr;

	/* Write AAD first. */
	while (dd->datalen > 0) {
		atmel_aes_write_block(dd, AES_IDATAR(0), dd->data);
		dd->data += 4;
		dd->datalen -= AES_BLOCK_SIZE;

		isr = atmel_aes_read(dd, AES_ISR);
		if (!(isr & AES_INT_DATARDY)) {
			dd->resume = atmel_aes_gcm_data;
			atmel_aes_write(dd, AES_IER, AES_INT_DATARDY);
			return -EINPROGRESS;
		}
	}

	/* GMAC only. */
	if (unlikely(ctx->textlen == 0))
		return atmel_aes_gcm_tag_init(dd);

	/* Prepare src and dst scatter lists to transfer cipher/plain texts */
	src = scatterwalk_ffwd(ctx->src, req->src, req->assoclen);
	dst = ((req->src == req->dst) ? src :
	       scatterwalk_ffwd(ctx->dst, req->dst, req->assoclen));

	if (use_dma) {
		/* Update the Mode Register for DMA transfers. */
		mr = atmel_aes_read(dd, AES_MR);
		mr &= ~(AES_MR_SMOD_MASK | AES_MR_DUALBUFF);
		mr |= AES_MR_SMOD_IDATAR0;
		if (dd->caps.has_dualbuff)
			mr |= AES_MR_DUALBUFF;
		atmel_aes_write(dd, AES_MR, mr);

		return atmel_aes_dma_start(dd, src, dst, ctx->textlen,
					   atmel_aes_gcm_tag_init);
	}

	return atmel_aes_cpu_start(dd, src, dst, ctx->textlen,
				   atmel_aes_gcm_tag_init);
}

static int atmel_aes_gcm_tag_init(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	struct aead_request *req = aead_request_cast(dd->areq);
	u64 *data = dd->buf;

	if (likely(dd->flags & AES_FLAGS_GTAGEN)) {
		if (!(atmel_aes_read(dd, AES_ISR) & AES_INT_TAGRDY)) {
			dd->resume = atmel_aes_gcm_tag_init;
			atmel_aes_write(dd, AES_IER, AES_INT_TAGRDY);
			return -EINPROGRESS;
		}

		return atmel_aes_gcm_finalize(dd);
	}

	/* Read the GCM Intermediate Hash Word Registers. */
	atmel_aes_read_block(dd, AES_GHASHR(0), ctx->ghash);

	data[0] = cpu_to_be64(req->assoclen * 8);
	data[1] = cpu_to_be64(ctx->textlen * 8);

	return atmel_aes_gcm_ghash(dd, (const u32 *)data, AES_BLOCK_SIZE,
				   ctx->ghash, ctx->ghash, atmel_aes_gcm_tag);
}

static int atmel_aes_gcm_tag(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	unsigned long flags;

	/*
	 * Change mode to CTR to complete the tag generation.
	 * Use J0 as Initialization Vector.
	 */
	flags = dd->flags;
	dd->flags &= ~(AES_FLAGS_OPMODE_MASK | AES_FLAGS_GTAGEN);
	dd->flags |= AES_FLAGS_CTR;
	atmel_aes_write_ctrl(dd, false, ctx->j0);
	dd->flags = flags;

	atmel_aes_write_block(dd, AES_IDATAR(0), ctx->ghash);
	return atmel_aes_wait_for_data_ready(dd, atmel_aes_gcm_finalize);
}

static int atmel_aes_gcm_finalize(struct atmel_aes_dev *dd)
{
	struct atmel_aes_gcm_ctx *ctx = atmel_aes_gcm_ctx_cast(dd->ctx);
	struct aead_request *req = aead_request_cast(dd->areq);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	bool enc = atmel_aes_is_encrypt(dd);
	u32 offset, authsize, itag[4], *otag = ctx->tag;
	int err;

	/* Read the computed tag. */
	if (likely(dd->flags & AES_FLAGS_GTAGEN))
		atmel_aes_read_block(dd, AES_TAGR(0), ctx->tag);
	else
		atmel_aes_read_block(dd, AES_ODATAR(0), ctx->tag);

	offset = req->assoclen + ctx->textlen;
	authsize = crypto_aead_authsize(tfm);
	if (enc) {
		scatterwalk_map_and_copy(otag, req->dst, offset, authsize, 1);
		err = 0;
	} else {
		scatterwalk_map_and_copy(itag, req->src, offset, authsize, 0);
		err = crypto_memneq(itag, otag, authsize) ? -EBADMSG : 0;
	}

	return atmel_aes_complete(dd, err);
}

static int atmel_aes_gcm_crypt(struct aead_request *req,
			       unsigned long mode)
{
	struct atmel_aes_base_ctx *ctx;
	struct atmel_aes_reqctx *rctx;
	struct atmel_aes_dev *dd;

	ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	ctx->block_size = AES_BLOCK_SIZE;
	ctx->is_aead = true;

	dd = atmel_aes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	rctx = aead_request_ctx(req);
	rctx->mode = AES_FLAGS_GCM | mode;

	return atmel_aes_handle_queue(dd, &req->base);
}

static int atmel_aes_gcm_setkey(struct crypto_aead *tfm, const u8 *key,
				unsigned int keylen)
{
	struct atmel_aes_base_ctx *ctx = crypto_aead_ctx(tfm);

	if (keylen != AES_KEYSIZE_256 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_128) {
		crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_aes_gcm_setauthsize(struct crypto_aead *tfm,
				     unsigned int authsize)
{
	/* Same as crypto_gcm_authsize() from crypto/gcm.c */
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int atmel_aes_gcm_encrypt(struct aead_request *req)
{
	return atmel_aes_gcm_crypt(req, AES_FLAGS_ENCRYPT);
}

static int atmel_aes_gcm_decrypt(struct aead_request *req)
{
	return atmel_aes_gcm_crypt(req, 0);
}

static int atmel_aes_gcm_init(struct crypto_aead *tfm)
{
	struct atmel_aes_gcm_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_aead_set_reqsize(tfm, sizeof(struct atmel_aes_reqctx));
	ctx->base.start = atmel_aes_gcm_start;

	return 0;
}

static struct aead_alg aes_gcm_alg = {
	.setkey		= atmel_aes_gcm_setkey,
	.setauthsize	= atmel_aes_gcm_setauthsize,
	.encrypt	= atmel_aes_gcm_encrypt,
	.decrypt	= atmel_aes_gcm_decrypt,
	.init		= atmel_aes_gcm_init,
	.ivsize		= GCM_AES_IV_SIZE,
	.maxauthsize	= AES_BLOCK_SIZE,

	.base = {
		.cra_name		= "gcm(aes)",
		.cra_driver_name	= "atmel-gcm-aes",
		.cra_priority		= ATMEL_AES_PRIORITY,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct atmel_aes_gcm_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
};


/* xts functions */

static inline struct atmel_aes_xts_ctx *
atmel_aes_xts_ctx_cast(struct atmel_aes_base_ctx *ctx)
{
	return container_of(ctx, struct atmel_aes_xts_ctx, base);
}

static int atmel_aes_xts_process_data(struct atmel_aes_dev *dd);

static int atmel_aes_xts_start(struct atmel_aes_dev *dd)
{
	struct atmel_aes_xts_ctx *ctx = atmel_aes_xts_ctx_cast(dd->ctx);
	struct ablkcipher_request *req = ablkcipher_request_cast(dd->areq);
	struct atmel_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	unsigned long flags;
	int err;

	atmel_aes_set_mode(dd, rctx);

	err = atmel_aes_hw_init(dd);
	if (err)
		return atmel_aes_complete(dd, err);

	/* Compute the tweak value from req->info with ecb(aes). */
	flags = dd->flags;
	dd->flags &= ~AES_FLAGS_MODE_MASK;
	dd->flags |= (AES_FLAGS_ECB | AES_FLAGS_ENCRYPT);
	atmel_aes_write_ctrl_key(dd, false, NULL,
				 ctx->key2, ctx->base.keylen);
	dd->flags = flags;

	atmel_aes_write_block(dd, AES_IDATAR(0), req->info);
	return atmel_aes_wait_for_data_ready(dd, atmel_aes_xts_process_data);
}

static int atmel_aes_xts_process_data(struct atmel_aes_dev *dd)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(dd->areq);
	bool use_dma = (req->nbytes >= ATMEL_AES_DMA_THRESHOLD);
	u32 tweak[AES_BLOCK_SIZE / sizeof(u32)];
	static const u32 one[AES_BLOCK_SIZE / sizeof(u32)] = {cpu_to_le32(1), };
	u8 *tweak_bytes = (u8 *)tweak;
	int i;

	/* Read the computed ciphered tweak value. */
	atmel_aes_read_block(dd, AES_ODATAR(0), tweak);
	/*
	 * Hardware quirk:
	 * the order of the ciphered tweak bytes need to be reversed before
	 * writing them into the ODATARx registers.
	 */
	for (i = 0; i < AES_BLOCK_SIZE/2; ++i) {
		u8 tmp = tweak_bytes[AES_BLOCK_SIZE - 1 - i];

		tweak_bytes[AES_BLOCK_SIZE - 1 - i] = tweak_bytes[i];
		tweak_bytes[i] = tmp;
	}

	/* Process the data. */
	atmel_aes_write_ctrl(dd, use_dma, NULL);
	atmel_aes_write_block(dd, AES_TWR(0), tweak);
	atmel_aes_write_block(dd, AES_ALPHAR(0), one);
	if (use_dma)
		return atmel_aes_dma_start(dd, req->src, req->dst, req->nbytes,
					   atmel_aes_transfer_complete);

	return atmel_aes_cpu_start(dd, req->src, req->dst, req->nbytes,
				   atmel_aes_transfer_complete);
}

static int atmel_aes_xts_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
				unsigned int keylen)
{
	struct atmel_aes_xts_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	int err;

	err = xts_check_key(crypto_ablkcipher_tfm(tfm), key, keylen);
	if (err)
		return err;

	memcpy(ctx->base.key, key, keylen/2);
	memcpy(ctx->key2, key + keylen/2, keylen/2);
	ctx->base.keylen = keylen/2;

	return 0;
}

static int atmel_aes_xts_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_XTS | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_xts_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req, AES_FLAGS_XTS);
}

static int atmel_aes_xts_cra_init(struct crypto_tfm *tfm)
{
	struct atmel_aes_xts_ctx *ctx = crypto_tfm_ctx(tfm);

	tfm->crt_ablkcipher.reqsize = sizeof(struct atmel_aes_reqctx);
	ctx->base.start = atmel_aes_xts_start;

	return 0;
}

static struct crypto_alg aes_xts_alg = {
	.cra_name		= "xts(aes)",
	.cra_driver_name	= "atmel-xts-aes",
	.cra_priority		= ATMEL_AES_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_xts_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_xts_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2 * AES_MIN_KEY_SIZE,
		.max_keysize	= 2 * AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_xts_setkey,
		.encrypt	= atmel_aes_xts_encrypt,
		.decrypt	= atmel_aes_xts_decrypt,
	}
};

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
/* authenc aead functions */

static int atmel_aes_authenc_start(struct atmel_aes_dev *dd);
static int atmel_aes_authenc_init(struct atmel_aes_dev *dd, int err,
				  bool is_async);
static int atmel_aes_authenc_transfer(struct atmel_aes_dev *dd, int err,
				      bool is_async);
static int atmel_aes_authenc_digest(struct atmel_aes_dev *dd);
static int atmel_aes_authenc_final(struct atmel_aes_dev *dd, int err,
				   bool is_async);

static void atmel_aes_authenc_complete(struct atmel_aes_dev *dd, int err)
{
	struct aead_request *req = aead_request_cast(dd->areq);
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);

	if (err && (dd->flags & AES_FLAGS_OWN_SHA))
		atmel_sha_authenc_abort(&rctx->auth_req);
	dd->flags &= ~AES_FLAGS_OWN_SHA;
}

static int atmel_aes_authenc_start(struct atmel_aes_dev *dd)
{
	struct aead_request *req = aead_request_cast(dd->areq);
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct atmel_aes_authenc_ctx *ctx = crypto_aead_ctx(tfm);
	int err;

	atmel_aes_set_mode(dd, &rctx->base);

	err = atmel_aes_hw_init(dd);
	if (err)
		return atmel_aes_complete(dd, err);

	return atmel_sha_authenc_schedule(&rctx->auth_req, ctx->auth,
					  atmel_aes_authenc_init, dd);
}

static int atmel_aes_authenc_init(struct atmel_aes_dev *dd, int err,
				  bool is_async)
{
	struct aead_request *req = aead_request_cast(dd->areq);
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);

	if (is_async)
		dd->is_async = true;
	if (err)
		return atmel_aes_complete(dd, err);

	/* If here, we've got the ownership of the SHA device. */
	dd->flags |= AES_FLAGS_OWN_SHA;

	/* Configure the SHA device. */
	return atmel_sha_authenc_init(&rctx->auth_req,
				      req->src, req->assoclen,
				      rctx->textlen,
				      atmel_aes_authenc_transfer, dd);
}

static int atmel_aes_authenc_transfer(struct atmel_aes_dev *dd, int err,
				      bool is_async)
{
	struct aead_request *req = aead_request_cast(dd->areq);
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);
	bool enc = atmel_aes_is_encrypt(dd);
	struct scatterlist *src, *dst;
	u32 iv[AES_BLOCK_SIZE / sizeof(u32)];
	u32 emr;

	if (is_async)
		dd->is_async = true;
	if (err)
		return atmel_aes_complete(dd, err);

	/* Prepare src and dst scatter-lists to transfer cipher/plain texts. */
	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen);
	dst = src;

	if (req->src != req->dst)
		dst = scatterwalk_ffwd(rctx->dst, req->dst, req->assoclen);

	/* Configure the AES device. */
	memcpy(iv, req->iv, sizeof(iv));

	/*
	 * Here we always set the 2nd parameter of atmel_aes_write_ctrl() to
	 * 'true' even if the data transfer is actually performed by the CPU (so
	 * not by the DMA) because we must force the AES_MR_SMOD bitfield to the
	 * value AES_MR_SMOD_IDATAR0. Indeed, both AES_MR_SMOD and SHA_MR_SMOD
	 * must be set to *_MR_SMOD_IDATAR0.
	 */
	atmel_aes_write_ctrl(dd, true, iv);
	emr = AES_EMR_PLIPEN;
	if (!enc)
		emr |= AES_EMR_PLIPD;
	atmel_aes_write(dd, AES_EMR, emr);

	/* Transfer data. */
	return atmel_aes_dma_start(dd, src, dst, rctx->textlen,
				   atmel_aes_authenc_digest);
}

static int atmel_aes_authenc_digest(struct atmel_aes_dev *dd)
{
	struct aead_request *req = aead_request_cast(dd->areq);
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);

	/* atmel_sha_authenc_final() releases the SHA device. */
	dd->flags &= ~AES_FLAGS_OWN_SHA;
	return atmel_sha_authenc_final(&rctx->auth_req,
				       rctx->digest, sizeof(rctx->digest),
				       atmel_aes_authenc_final, dd);
}

static int atmel_aes_authenc_final(struct atmel_aes_dev *dd, int err,
				   bool is_async)
{
	struct aead_request *req = aead_request_cast(dd->areq);
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	bool enc = atmel_aes_is_encrypt(dd);
	u32 idigest[SHA512_DIGEST_SIZE / sizeof(u32)], *odigest = rctx->digest;
	u32 offs, authsize;

	if (is_async)
		dd->is_async = true;
	if (err)
		goto complete;

	offs = req->assoclen + rctx->textlen;
	authsize = crypto_aead_authsize(tfm);
	if (enc) {
		scatterwalk_map_and_copy(odigest, req->dst, offs, authsize, 1);
	} else {
		scatterwalk_map_and_copy(idigest, req->src, offs, authsize, 0);
		if (crypto_memneq(idigest, odigest, authsize))
			err = -EBADMSG;
	}

complete:
	return atmel_aes_complete(dd, err);
}

static int atmel_aes_authenc_setkey(struct crypto_aead *tfm, const u8 *key,
				    unsigned int keylen)
{
	struct atmel_aes_authenc_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_authenc_keys keys;
	u32 flags;
	int err;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

	if (keys.enckeylen > sizeof(ctx->base.key))
		goto badkey;

	/* Save auth key. */
	flags = crypto_aead_get_flags(tfm);
	err = atmel_sha_authenc_setkey(ctx->auth,
				       keys.authkey, keys.authkeylen,
				       &flags);
	crypto_aead_set_flags(tfm, flags & CRYPTO_TFM_RES_MASK);
	if (err) {
		memzero_explicit(&keys, sizeof(keys));
		return err;
	}

	/* Save enc key. */
	ctx->base.keylen = keys.enckeylen;
	memcpy(ctx->base.key, keys.enckey, keys.enckeylen);

	memzero_explicit(&keys, sizeof(keys));
	return 0;

badkey:
	crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	memzero_explicit(&key, sizeof(keys));
	return -EINVAL;
}

static int atmel_aes_authenc_init_tfm(struct crypto_aead *tfm,
				      unsigned long auth_mode)
{
	struct atmel_aes_authenc_ctx *ctx = crypto_aead_ctx(tfm);
	unsigned int auth_reqsize = atmel_sha_authenc_get_reqsize();

	ctx->auth = atmel_sha_authenc_spawn(auth_mode);
	if (IS_ERR(ctx->auth))
		return PTR_ERR(ctx->auth);

	crypto_aead_set_reqsize(tfm, (sizeof(struct atmel_aes_authenc_reqctx) +
				      auth_reqsize));
	ctx->base.start = atmel_aes_authenc_start;

	return 0;
}

static int atmel_aes_authenc_hmac_sha1_init_tfm(struct crypto_aead *tfm)
{
	return atmel_aes_authenc_init_tfm(tfm, SHA_FLAGS_HMAC_SHA1);
}

static int atmel_aes_authenc_hmac_sha224_init_tfm(struct crypto_aead *tfm)
{
	return atmel_aes_authenc_init_tfm(tfm, SHA_FLAGS_HMAC_SHA224);
}

static int atmel_aes_authenc_hmac_sha256_init_tfm(struct crypto_aead *tfm)
{
	return atmel_aes_authenc_init_tfm(tfm, SHA_FLAGS_HMAC_SHA256);
}

static int atmel_aes_authenc_hmac_sha384_init_tfm(struct crypto_aead *tfm)
{
	return atmel_aes_authenc_init_tfm(tfm, SHA_FLAGS_HMAC_SHA384);
}

static int atmel_aes_authenc_hmac_sha512_init_tfm(struct crypto_aead *tfm)
{
	return atmel_aes_authenc_init_tfm(tfm, SHA_FLAGS_HMAC_SHA512);
}

static void atmel_aes_authenc_exit_tfm(struct crypto_aead *tfm)
{
	struct atmel_aes_authenc_ctx *ctx = crypto_aead_ctx(tfm);

	atmel_sha_authenc_free(ctx->auth);
}

static int atmel_aes_authenc_crypt(struct aead_request *req,
				   unsigned long mode)
{
	struct atmel_aes_authenc_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct atmel_aes_base_ctx *ctx = crypto_aead_ctx(tfm);
	u32 authsize = crypto_aead_authsize(tfm);
	bool enc = (mode & AES_FLAGS_ENCRYPT);
	struct atmel_aes_dev *dd;

	/* Compute text length. */
	if (!enc && req->cryptlen < authsize)
		return -EINVAL;
	rctx->textlen = req->cryptlen - (enc ? 0 : authsize);

	/*
	 * Currently, empty messages are not supported yet:
	 * the SHA auto-padding can be used only on non-empty messages.
	 * Hence a special case needs to be implemented for empty message.
	 */
	if (!rctx->textlen && !req->assoclen)
		return -EINVAL;

	rctx->base.mode = mode;
	ctx->block_size = AES_BLOCK_SIZE;
	ctx->is_aead = true;

	dd = atmel_aes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	return atmel_aes_handle_queue(dd, &req->base);
}

static int atmel_aes_authenc_cbc_aes_encrypt(struct aead_request *req)
{
	return atmel_aes_authenc_crypt(req, AES_FLAGS_CBC | AES_FLAGS_ENCRYPT);
}

static int atmel_aes_authenc_cbc_aes_decrypt(struct aead_request *req)
{
	return atmel_aes_authenc_crypt(req, AES_FLAGS_CBC);
}

static struct aead_alg aes_authenc_algs[] = {
{
	.setkey		= atmel_aes_authenc_setkey,
	.encrypt	= atmel_aes_authenc_cbc_aes_encrypt,
	.decrypt	= atmel_aes_authenc_cbc_aes_decrypt,
	.init		= atmel_aes_authenc_hmac_sha1_init_tfm,
	.exit		= atmel_aes_authenc_exit_tfm,
	.ivsize		= AES_BLOCK_SIZE,
	.maxauthsize	= SHA1_DIGEST_SIZE,

	.base = {
		.cra_name		= "authenc(hmac(sha1),cbc(aes))",
		.cra_driver_name	= "atmel-authenc-hmac-sha1-cbc-aes",
		.cra_priority		= ATMEL_AES_PRIORITY,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct atmel_aes_authenc_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
{
	.setkey		= atmel_aes_authenc_setkey,
	.encrypt	= atmel_aes_authenc_cbc_aes_encrypt,
	.decrypt	= atmel_aes_authenc_cbc_aes_decrypt,
	.init		= atmel_aes_authenc_hmac_sha224_init_tfm,
	.exit		= atmel_aes_authenc_exit_tfm,
	.ivsize		= AES_BLOCK_SIZE,
	.maxauthsize	= SHA224_DIGEST_SIZE,

	.base = {
		.cra_name		= "authenc(hmac(sha224),cbc(aes))",
		.cra_driver_name	= "atmel-authenc-hmac-sha224-cbc-aes",
		.cra_priority		= ATMEL_AES_PRIORITY,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct atmel_aes_authenc_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
{
	.setkey		= atmel_aes_authenc_setkey,
	.encrypt	= atmel_aes_authenc_cbc_aes_encrypt,
	.decrypt	= atmel_aes_authenc_cbc_aes_decrypt,
	.init		= atmel_aes_authenc_hmac_sha256_init_tfm,
	.exit		= atmel_aes_authenc_exit_tfm,
	.ivsize		= AES_BLOCK_SIZE,
	.maxauthsize	= SHA256_DIGEST_SIZE,

	.base = {
		.cra_name		= "authenc(hmac(sha256),cbc(aes))",
		.cra_driver_name	= "atmel-authenc-hmac-sha256-cbc-aes",
		.cra_priority		= ATMEL_AES_PRIORITY,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct atmel_aes_authenc_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
{
	.setkey		= atmel_aes_authenc_setkey,
	.encrypt	= atmel_aes_authenc_cbc_aes_encrypt,
	.decrypt	= atmel_aes_authenc_cbc_aes_decrypt,
	.init		= atmel_aes_authenc_hmac_sha384_init_tfm,
	.exit		= atmel_aes_authenc_exit_tfm,
	.ivsize		= AES_BLOCK_SIZE,
	.maxauthsize	= SHA384_DIGEST_SIZE,

	.base = {
		.cra_name		= "authenc(hmac(sha384),cbc(aes))",
		.cra_driver_name	= "atmel-authenc-hmac-sha384-cbc-aes",
		.cra_priority		= ATMEL_AES_PRIORITY,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct atmel_aes_authenc_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
{
	.setkey		= atmel_aes_authenc_setkey,
	.encrypt	= atmel_aes_authenc_cbc_aes_encrypt,
	.decrypt	= atmel_aes_authenc_cbc_aes_decrypt,
	.init		= atmel_aes_authenc_hmac_sha512_init_tfm,
	.exit		= atmel_aes_authenc_exit_tfm,
	.ivsize		= AES_BLOCK_SIZE,
	.maxauthsize	= SHA512_DIGEST_SIZE,

	.base = {
		.cra_name		= "authenc(hmac(sha512),cbc(aes))",
		.cra_driver_name	= "atmel-authenc-hmac-sha512-cbc-aes",
		.cra_priority		= ATMEL_AES_PRIORITY,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct atmel_aes_authenc_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
};
#endif /* CONFIG_CRYPTO_DEV_ATMEL_AUTHENC */

/* Probe functions */

static int atmel_aes_buff_init(struct atmel_aes_dev *dd)
{
	dd->buf = (void *)__get_free_pages(GFP_KERNEL, ATMEL_AES_BUFFER_ORDER);
	dd->buflen = ATMEL_AES_BUFFER_SIZE;
	dd->buflen &= ~(AES_BLOCK_SIZE - 1);

	if (!dd->buf) {
		dev_err(dd->dev, "unable to alloc pages.\n");
		return -ENOMEM;
	}

	return 0;
}

static void atmel_aes_buff_cleanup(struct atmel_aes_dev *dd)
{
	free_page((unsigned long)dd->buf);
}

static bool atmel_aes_filter(struct dma_chan *chan, void *slave)
{
	struct at_dma_slave	*sl = slave;

	if (sl && sl->dma_dev == chan->device->dev) {
		chan->private = sl;
		return true;
	} else {
		return false;
	}
}

static int atmel_aes_dma_init(struct atmel_aes_dev *dd,
			      struct crypto_platform_data *pdata)
{
	struct at_dma_slave *slave;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Try to grab 2 DMA channels */
	slave = &pdata->dma_slave->rxdata;
	dd->src.chan = dma_request_slave_channel_compat(mask, atmel_aes_filter,
							slave, dd->dev, "tx");
	if (!dd->src.chan)
		goto err_dma_in;

	slave = &pdata->dma_slave->txdata;
	dd->dst.chan = dma_request_slave_channel_compat(mask, atmel_aes_filter,
							slave, dd->dev, "rx");
	if (!dd->dst.chan)
		goto err_dma_out;

	return 0;

err_dma_out:
	dma_release_channel(dd->src.chan);
err_dma_in:
	dev_warn(dd->dev, "no DMA channel available\n");
	return -ENODEV;
}

static void atmel_aes_dma_cleanup(struct atmel_aes_dev *dd)
{
	dma_release_channel(dd->dst.chan);
	dma_release_channel(dd->src.chan);
}

static void atmel_aes_queue_task(unsigned long data)
{
	struct atmel_aes_dev *dd = (struct atmel_aes_dev *)data;

	atmel_aes_handle_queue(dd, NULL);
}

static void atmel_aes_done_task(unsigned long data)
{
	struct atmel_aes_dev *dd = (struct atmel_aes_dev *)data;

	dd->is_async = true;
	(void)dd->resume(dd);
}

static irqreturn_t atmel_aes_irq(int irq, void *dev_id)
{
	struct atmel_aes_dev *aes_dd = dev_id;
	u32 reg;

	reg = atmel_aes_read(aes_dd, AES_ISR);
	if (reg & atmel_aes_read(aes_dd, AES_IMR)) {
		atmel_aes_write(aes_dd, AES_IDR, reg);
		if (AES_FLAGS_BUSY & aes_dd->flags)
			tasklet_schedule(&aes_dd->done_task);
		else
			dev_warn(aes_dd->dev, "AES interrupt when no active requests.\n");
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void atmel_aes_unregister_algs(struct atmel_aes_dev *dd)
{
	int i;

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
	if (dd->caps.has_authenc)
		for (i = 0; i < ARRAY_SIZE(aes_authenc_algs); i++)
			crypto_unregister_aead(&aes_authenc_algs[i]);
#endif

	if (dd->caps.has_xts)
		crypto_unregister_alg(&aes_xts_alg);

	if (dd->caps.has_gcm)
		crypto_unregister_aead(&aes_gcm_alg);

	if (dd->caps.has_cfb64)
		crypto_unregister_alg(&aes_cfb64_alg);

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++)
		crypto_unregister_alg(&aes_algs[i]);
}

static int atmel_aes_register_algs(struct atmel_aes_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		err = crypto_register_alg(&aes_algs[i]);
		if (err)
			goto err_aes_algs;
	}

	if (dd->caps.has_cfb64) {
		err = crypto_register_alg(&aes_cfb64_alg);
		if (err)
			goto err_aes_cfb64_alg;
	}

	if (dd->caps.has_gcm) {
		err = crypto_register_aead(&aes_gcm_alg);
		if (err)
			goto err_aes_gcm_alg;
	}

	if (dd->caps.has_xts) {
		err = crypto_register_alg(&aes_xts_alg);
		if (err)
			goto err_aes_xts_alg;
	}

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
	if (dd->caps.has_authenc) {
		for (i = 0; i < ARRAY_SIZE(aes_authenc_algs); i++) {
			err = crypto_register_aead(&aes_authenc_algs[i]);
			if (err)
				goto err_aes_authenc_alg;
		}
	}
#endif

	return 0;

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
	/* i = ARRAY_SIZE(aes_authenc_algs); */
err_aes_authenc_alg:
	for (j = 0; j < i; j++)
		crypto_unregister_aead(&aes_authenc_algs[j]);
	crypto_unregister_alg(&aes_xts_alg);
#endif
err_aes_xts_alg:
	crypto_unregister_aead(&aes_gcm_alg);
err_aes_gcm_alg:
	crypto_unregister_alg(&aes_cfb64_alg);
err_aes_cfb64_alg:
	i = ARRAY_SIZE(aes_algs);
err_aes_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&aes_algs[j]);

	return err;
}

static void atmel_aes_get_cap(struct atmel_aes_dev *dd)
{
	dd->caps.has_dualbuff = 0;
	dd->caps.has_cfb64 = 0;
	dd->caps.has_ctr32 = 0;
	dd->caps.has_gcm = 0;
	dd->caps.has_xts = 0;
	dd->caps.has_authenc = 0;
	dd->caps.max_burst_size = 1;

	/* keep only major version number */
	switch (dd->hw_version & 0xff0) {
	case 0x500:
		dd->caps.has_dualbuff = 1;
		dd->caps.has_cfb64 = 1;
		dd->caps.has_ctr32 = 1;
		dd->caps.has_gcm = 1;
		dd->caps.has_xts = 1;
		dd->caps.has_authenc = 1;
		dd->caps.max_burst_size = 4;
		break;
	case 0x200:
		dd->caps.has_dualbuff = 1;
		dd->caps.has_cfb64 = 1;
		dd->caps.has_ctr32 = 1;
		dd->caps.has_gcm = 1;
		dd->caps.max_burst_size = 4;
		break;
	case 0x130:
		dd->caps.has_dualbuff = 1;
		dd->caps.has_cfb64 = 1;
		dd->caps.max_burst_size = 4;
		break;
	case 0x120:
		break;
	default:
		dev_warn(dd->dev,
				"Unmanaged aes version, set minimum capabilities\n");
		break;
	}
}

#if defined(CONFIG_OF)
static const struct of_device_id atmel_aes_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g46-aes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_aes_dt_ids);

static struct crypto_platform_data *atmel_aes_of_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct crypto_platform_data *pdata;

	if (!np) {
		dev_err(&pdev->dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	pdata->dma_slave = devm_kzalloc(&pdev->dev,
					sizeof(*(pdata->dma_slave)),
					GFP_KERNEL);
	if (!pdata->dma_slave) {
		dev_err(&pdev->dev, "could not allocate memory for dma_slave\n");
		devm_kfree(&pdev->dev, pdata);
		return ERR_PTR(-ENOMEM);
	}

	return pdata;
}
#else
static inline struct crypto_platform_data *atmel_aes_of_init(struct platform_device *pdev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int atmel_aes_probe(struct platform_device *pdev)
{
	struct atmel_aes_dev *aes_dd;
	struct crypto_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct resource *aes_res;
	int err;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		pdata = atmel_aes_of_init(pdev);
		if (IS_ERR(pdata)) {
			err = PTR_ERR(pdata);
			goto aes_dd_err;
		}
	}

	if (!pdata->dma_slave) {
		err = -ENXIO;
		goto aes_dd_err;
	}

	aes_dd = devm_kzalloc(&pdev->dev, sizeof(*aes_dd), GFP_KERNEL);
	if (aes_dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto aes_dd_err;
	}

	aes_dd->dev = dev;

	platform_set_drvdata(pdev, aes_dd);

	INIT_LIST_HEAD(&aes_dd->list);
	spin_lock_init(&aes_dd->lock);

	tasklet_init(&aes_dd->done_task, atmel_aes_done_task,
					(unsigned long)aes_dd);
	tasklet_init(&aes_dd->queue_task, atmel_aes_queue_task,
					(unsigned long)aes_dd);

	crypto_init_queue(&aes_dd->queue, ATMEL_AES_QUEUE_LENGTH);

	/* Get the base address */
	aes_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!aes_res) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto res_err;
	}
	aes_dd->phys_base = aes_res->start;

	/* Get the IRQ */
	aes_dd->irq = platform_get_irq(pdev,  0);
	if (aes_dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = aes_dd->irq;
		goto res_err;
	}

	err = devm_request_irq(&pdev->dev, aes_dd->irq, atmel_aes_irq,
			       IRQF_SHARED, "atmel-aes", aes_dd);
	if (err) {
		dev_err(dev, "unable to request aes irq.\n");
		goto res_err;
	}

	/* Initializing the clock */
	aes_dd->iclk = devm_clk_get(&pdev->dev, "aes_clk");
	if (IS_ERR(aes_dd->iclk)) {
		dev_err(dev, "clock initialization failed.\n");
		err = PTR_ERR(aes_dd->iclk);
		goto res_err;
	}

	aes_dd->io_base = devm_ioremap_resource(&pdev->dev, aes_res);
	if (IS_ERR(aes_dd->io_base)) {
		dev_err(dev, "can't ioremap\n");
		err = PTR_ERR(aes_dd->io_base);
		goto res_err;
	}

	err = clk_prepare(aes_dd->iclk);
	if (err)
		goto res_err;

	err = atmel_aes_hw_version_init(aes_dd);
	if (err)
		goto iclk_unprepare;

	atmel_aes_get_cap(aes_dd);

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC
	if (aes_dd->caps.has_authenc && !atmel_sha_authenc_is_ready()) {
		err = -EPROBE_DEFER;
		goto iclk_unprepare;
	}
#endif

	err = atmel_aes_buff_init(aes_dd);
	if (err)
		goto err_aes_buff;

	err = atmel_aes_dma_init(aes_dd, pdata);
	if (err)
		goto err_aes_dma;

	spin_lock(&atmel_aes.lock);
	list_add_tail(&aes_dd->list, &atmel_aes.dev_list);
	spin_unlock(&atmel_aes.lock);

	err = atmel_aes_register_algs(aes_dd);
	if (err)
		goto err_algs;

	dev_info(dev, "Atmel AES - Using %s, %s for DMA transfers\n",
			dma_chan_name(aes_dd->src.chan),
			dma_chan_name(aes_dd->dst.chan));

	return 0;

err_algs:
	spin_lock(&atmel_aes.lock);
	list_del(&aes_dd->list);
	spin_unlock(&atmel_aes.lock);
	atmel_aes_dma_cleanup(aes_dd);
err_aes_dma:
	atmel_aes_buff_cleanup(aes_dd);
err_aes_buff:
iclk_unprepare:
	clk_unprepare(aes_dd->iclk);
res_err:
	tasklet_kill(&aes_dd->done_task);
	tasklet_kill(&aes_dd->queue_task);
aes_dd_err:
	if (err != -EPROBE_DEFER)
		dev_err(dev, "initialization failed.\n");

	return err;
}

static int atmel_aes_remove(struct platform_device *pdev)
{
	struct atmel_aes_dev *aes_dd;

	aes_dd = platform_get_drvdata(pdev);
	if (!aes_dd)
		return -ENODEV;
	spin_lock(&atmel_aes.lock);
	list_del(&aes_dd->list);
	spin_unlock(&atmel_aes.lock);

	atmel_aes_unregister_algs(aes_dd);

	tasklet_kill(&aes_dd->done_task);
	tasklet_kill(&aes_dd->queue_task);

	atmel_aes_dma_cleanup(aes_dd);
	atmel_aes_buff_cleanup(aes_dd);

	clk_unprepare(aes_dd->iclk);

	return 0;
}

static struct platform_driver atmel_aes_driver = {
	.probe		= atmel_aes_probe,
	.remove		= atmel_aes_remove,
	.driver		= {
		.name	= "atmel_aes",
		.of_match_table = of_match_ptr(atmel_aes_dt_ids),
	},
};

module_platform_driver(atmel_aes_driver);

MODULE_DESCRIPTION("Atmel AES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
