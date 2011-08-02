/*
 * Copyright (c) 2010-2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/md5.h>
#include <crypto/sha.h>
#include <crypto/internal/skcipher.h>
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/rtnetlink.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "picoxcell_crypto_regs.h"

/*
 * The threshold for the number of entries in the CMD FIFO available before
 * the CMD0_CNT interrupt is raised. Increasing this value will reduce the
 * number of interrupts raised to the CPU.
 */
#define CMD0_IRQ_THRESHOLD   1

/*
 * The timeout period (in jiffies) for a PDU. When the the number of PDUs in
 * flight is greater than the STAT_IRQ_THRESHOLD or 0 the timer is disabled.
 * When there are packets in flight but lower than the threshold, we enable
 * the timer and at expiry, attempt to remove any processed packets from the
 * queue and if there are still packets left, schedule the timer again.
 */
#define PACKET_TIMEOUT	    1

/* The priority to register each algorithm with. */
#define SPACC_CRYPTO_ALG_PRIORITY	10000

#define SPACC_CRYPTO_KASUMI_F8_KEY_LEN	16
#define SPACC_CRYPTO_IPSEC_CIPHER_PG_SZ 64
#define SPACC_CRYPTO_IPSEC_HASH_PG_SZ	64
#define SPACC_CRYPTO_IPSEC_MAX_CTXS	32
#define SPACC_CRYPTO_IPSEC_FIFO_SZ	32
#define SPACC_CRYPTO_L2_CIPHER_PG_SZ	64
#define SPACC_CRYPTO_L2_HASH_PG_SZ	64
#define SPACC_CRYPTO_L2_MAX_CTXS	128
#define SPACC_CRYPTO_L2_FIFO_SZ		128

#define MAX_DDT_LEN			16

/* DDT format. This must match the hardware DDT format exactly. */
struct spacc_ddt {
	dma_addr_t	p;
	u32		len;
};

/*
 * Asynchronous crypto request structure.
 *
 * This structure defines a request that is either queued for processing or
 * being processed.
 */
struct spacc_req {
	struct list_head		list;
	struct spacc_engine		*engine;
	struct crypto_async_request	*req;
	int				result;
	bool				is_encrypt;
	unsigned			ctx_id;
	dma_addr_t			src_addr, dst_addr;
	struct spacc_ddt		*src_ddt, *dst_ddt;
	void				(*complete)(struct spacc_req *req);

	/* AEAD specific bits. */
	u8				*giv;
	size_t				giv_len;
	dma_addr_t			giv_pa;
};

struct spacc_engine {
	void __iomem			*regs;
	struct list_head		pending;
	int				next_ctx;
	spinlock_t			hw_lock;
	int				in_flight;
	struct list_head		completed;
	struct list_head		in_progress;
	struct tasklet_struct		complete;
	unsigned long			fifo_sz;
	void __iomem			*cipher_ctx_base;
	void __iomem			*hash_key_base;
	struct spacc_alg		*algs;
	unsigned			num_algs;
	struct list_head		registered_algs;
	size_t				cipher_pg_sz;
	size_t				hash_pg_sz;
	const char			*name;
	struct clk			*clk;
	struct device			*dev;
	unsigned			max_ctxs;
	struct timer_list		packet_timeout;
	unsigned			stat_irq_thresh;
	struct dma_pool			*req_pool;
};

/* Algorithm type mask. */
#define SPACC_CRYPTO_ALG_MASK		0x7

/* SPACC definition of a crypto algorithm. */
struct spacc_alg {
	unsigned long			ctrl_default;
	unsigned long			type;
	struct crypto_alg		alg;
	struct spacc_engine		*engine;
	struct list_head		entry;
	int				key_offs;
	int				iv_offs;
};

/* Generic context structure for any algorithm type. */
struct spacc_generic_ctx {
	struct spacc_engine		*engine;
	int				flags;
	int				key_offs;
	int				iv_offs;
};

/* Block cipher context. */
struct spacc_ablk_ctx {
	struct spacc_generic_ctx	generic;
	u8				key[AES_MAX_KEY_SIZE];
	u8				key_len;
	/*
	 * The fallback cipher. If the operation can't be done in hardware,
	 * fallback to a software version.
	 */
	struct crypto_ablkcipher	*sw_cipher;
};

/* AEAD cipher context. */
struct spacc_aead_ctx {
	struct spacc_generic_ctx	generic;
	u8				cipher_key[AES_MAX_KEY_SIZE];
	u8				hash_ctx[SPACC_CRYPTO_IPSEC_HASH_PG_SZ];
	u8				cipher_key_len;
	u8				hash_key_len;
	struct crypto_aead		*sw_cipher;
	size_t				auth_size;
	u8				salt[AES_BLOCK_SIZE];
};

static int spacc_ablk_submit(struct spacc_req *req);

static inline struct spacc_alg *to_spacc_alg(struct crypto_alg *alg)
{
	return alg ? container_of(alg, struct spacc_alg, alg) : NULL;
}

static inline int spacc_fifo_cmd_full(struct spacc_engine *engine)
{
	u32 fifo_stat = readl(engine->regs + SPA_FIFO_STAT_REG_OFFSET);

	return fifo_stat & SPA_FIFO_CMD_FULL;
}

/*
 * Given a cipher context, and a context number, get the base address of the
 * context page.
 *
 * Returns the address of the context page where the key/context may
 * be written.
 */
static inline void __iomem *spacc_ctx_page_addr(struct spacc_generic_ctx *ctx,
						unsigned indx,
						bool is_cipher_ctx)
{
	return is_cipher_ctx ? ctx->engine->cipher_ctx_base +
			(indx * ctx->engine->cipher_pg_sz) :
		ctx->engine->hash_key_base + (indx * ctx->engine->hash_pg_sz);
}

/* The context pages can only be written with 32-bit accesses. */
static inline void memcpy_toio32(u32 __iomem *dst, const void *src,
				 unsigned count)
{
	const u32 *src32 = (const u32 *) src;

	while (count--)
		writel(*src32++, dst++);
}

static void spacc_cipher_write_ctx(struct spacc_generic_ctx *ctx,
				   void __iomem *page_addr, const u8 *key,
				   size_t key_len, const u8 *iv, size_t iv_len)
{
	void __iomem *key_ptr = page_addr + ctx->key_offs;
	void __iomem *iv_ptr = page_addr + ctx->iv_offs;

	memcpy_toio32(key_ptr, key, key_len / 4);
	memcpy_toio32(iv_ptr, iv, iv_len / 4);
}

/*
 * Load a context into the engines context memory.
 *
 * Returns the index of the context page where the context was loaded.
 */
static unsigned spacc_load_ctx(struct spacc_generic_ctx *ctx,
			       const u8 *ciph_key, size_t ciph_len,
			       const u8 *iv, size_t ivlen, const u8 *hash_key,
			       size_t hash_len)
{
	unsigned indx = ctx->engine->next_ctx++;
	void __iomem *ciph_page_addr, *hash_page_addr;

	ciph_page_addr = spacc_ctx_page_addr(ctx, indx, 1);
	hash_page_addr = spacc_ctx_page_addr(ctx, indx, 0);

	ctx->engine->next_ctx &= ctx->engine->fifo_sz - 1;
	spacc_cipher_write_ctx(ctx, ciph_page_addr, ciph_key, ciph_len, iv,
			       ivlen);
	writel(ciph_len | (indx << SPA_KEY_SZ_CTX_INDEX_OFFSET) |
	       (1 << SPA_KEY_SZ_CIPHER_OFFSET),
	       ctx->engine->regs + SPA_KEY_SZ_REG_OFFSET);

	if (hash_key) {
		memcpy_toio32(hash_page_addr, hash_key, hash_len / 4);
		writel(hash_len | (indx << SPA_KEY_SZ_CTX_INDEX_OFFSET),
		       ctx->engine->regs + SPA_KEY_SZ_REG_OFFSET);
	}

	return indx;
}

/* Count the number of scatterlist entries in a scatterlist. */
static int sg_count(struct scatterlist *sg_list, int nbytes)
{
	struct scatterlist *sg = sg_list;
	int sg_nents = 0;

	while (nbytes > 0) {
		++sg_nents;
		nbytes -= sg->length;
		sg = sg_next(sg);
	}

	return sg_nents;
}

static inline void ddt_set(struct spacc_ddt *ddt, dma_addr_t phys, size_t len)
{
	ddt->p = phys;
	ddt->len = len;
}

/*
 * Take a crypto request and scatterlists for the data and turn them into DDTs
 * for passing to the crypto engines. This also DMA maps the data so that the
 * crypto engines can DMA to/from them.
 */
static struct spacc_ddt *spacc_sg_to_ddt(struct spacc_engine *engine,
					 struct scatterlist *payload,
					 unsigned nbytes,
					 enum dma_data_direction dir,
					 dma_addr_t *ddt_phys)
{
	unsigned nents, mapped_ents;
	struct scatterlist *cur;
	struct spacc_ddt *ddt;
	int i;

	nents = sg_count(payload, nbytes);
	mapped_ents = dma_map_sg(engine->dev, payload, nents, dir);

	if (mapped_ents + 1 > MAX_DDT_LEN)
		goto out;

	ddt = dma_pool_alloc(engine->req_pool, GFP_ATOMIC, ddt_phys);
	if (!ddt)
		goto out;

	for_each_sg(payload, cur, mapped_ents, i)
		ddt_set(&ddt[i], sg_dma_address(cur), sg_dma_len(cur));
	ddt_set(&ddt[mapped_ents], 0, 0);

	return ddt;

out:
	dma_unmap_sg(engine->dev, payload, nents, dir);
	return NULL;
}

static int spacc_aead_make_ddts(struct spacc_req *req, u8 *giv)
{
	struct aead_request *areq = container_of(req->req, struct aead_request,
						 base);
	struct spacc_engine *engine = req->engine;
	struct spacc_ddt *src_ddt, *dst_ddt;
	unsigned ivsize = crypto_aead_ivsize(crypto_aead_reqtfm(areq));
	unsigned nents = sg_count(areq->src, areq->cryptlen);
	dma_addr_t iv_addr;
	struct scatterlist *cur;
	int i, dst_ents, src_ents, assoc_ents;
	u8 *iv = giv ? giv : areq->iv;

	src_ddt = dma_pool_alloc(engine->req_pool, GFP_ATOMIC, &req->src_addr);
	if (!src_ddt)
		return -ENOMEM;

	dst_ddt = dma_pool_alloc(engine->req_pool, GFP_ATOMIC, &req->dst_addr);
	if (!dst_ddt) {
		dma_pool_free(engine->req_pool, src_ddt, req->src_addr);
		return -ENOMEM;
	}

	req->src_ddt = src_ddt;
	req->dst_ddt = dst_ddt;

	assoc_ents = dma_map_sg(engine->dev, areq->assoc,
		sg_count(areq->assoc, areq->assoclen), DMA_TO_DEVICE);
	if (areq->src != areq->dst) {
		src_ents = dma_map_sg(engine->dev, areq->src, nents,
				      DMA_TO_DEVICE);
		dst_ents = dma_map_sg(engine->dev, areq->dst, nents,
				      DMA_FROM_DEVICE);
	} else {
		src_ents = dma_map_sg(engine->dev, areq->src, nents,
				      DMA_BIDIRECTIONAL);
		dst_ents = 0;
	}

	/*
	 * Map the IV/GIV. For the GIV it needs to be bidirectional as it is
	 * formed by the crypto block and sent as the ESP IV for IPSEC.
	 */
	iv_addr = dma_map_single(engine->dev, iv, ivsize,
				 giv ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE);
	req->giv_pa = iv_addr;

	/*
	 * Map the associated data. For decryption we don't copy the
	 * associated data.
	 */
	for_each_sg(areq->assoc, cur, assoc_ents, i) {
		ddt_set(src_ddt++, sg_dma_address(cur), sg_dma_len(cur));
		if (req->is_encrypt)
			ddt_set(dst_ddt++, sg_dma_address(cur),
				sg_dma_len(cur));
	}
	ddt_set(src_ddt++, iv_addr, ivsize);

	if (giv || req->is_encrypt)
		ddt_set(dst_ddt++, iv_addr, ivsize);

	/*
	 * Now map in the payload for the source and destination and terminate
	 * with the NULL pointers.
	 */
	for_each_sg(areq->src, cur, src_ents, i) {
		ddt_set(src_ddt++, sg_dma_address(cur), sg_dma_len(cur));
		if (areq->src == areq->dst)
			ddt_set(dst_ddt++, sg_dma_address(cur),
				sg_dma_len(cur));
	}

	for_each_sg(areq->dst, cur, dst_ents, i)
		ddt_set(dst_ddt++, sg_dma_address(cur),
			sg_dma_len(cur));

	ddt_set(src_ddt, 0, 0);
	ddt_set(dst_ddt, 0, 0);

	return 0;
}

static void spacc_aead_free_ddts(struct spacc_req *req)
{
	struct aead_request *areq = container_of(req->req, struct aead_request,
						 base);
	struct spacc_alg *alg = to_spacc_alg(req->req->tfm->__crt_alg);
	struct spacc_ablk_ctx *aead_ctx = crypto_tfm_ctx(req->req->tfm);
	struct spacc_engine *engine = aead_ctx->generic.engine;
	unsigned ivsize = alg->alg.cra_aead.ivsize;
	unsigned nents = sg_count(areq->src, areq->cryptlen);

	if (areq->src != areq->dst) {
		dma_unmap_sg(engine->dev, areq->src, nents, DMA_TO_DEVICE);
		dma_unmap_sg(engine->dev, areq->dst,
			     sg_count(areq->dst, areq->cryptlen),
			     DMA_FROM_DEVICE);
	} else
		dma_unmap_sg(engine->dev, areq->src, nents, DMA_BIDIRECTIONAL);

	dma_unmap_sg(engine->dev, areq->assoc,
		     sg_count(areq->assoc, areq->assoclen), DMA_TO_DEVICE);

	dma_unmap_single(engine->dev, req->giv_pa, ivsize, DMA_BIDIRECTIONAL);

	dma_pool_free(engine->req_pool, req->src_ddt, req->src_addr);
	dma_pool_free(engine->req_pool, req->dst_ddt, req->dst_addr);
}

static void spacc_free_ddt(struct spacc_req *req, struct spacc_ddt *ddt,
			   dma_addr_t ddt_addr, struct scatterlist *payload,
			   unsigned nbytes, enum dma_data_direction dir)
{
	unsigned nents = sg_count(payload, nbytes);

	dma_unmap_sg(req->engine->dev, payload, nents, dir);
	dma_pool_free(req->engine->req_pool, ddt, ddt_addr);
}

/*
 * Set key for a DES operation in an AEAD cipher. This also performs weak key
 * checking if required.
 */
static int spacc_aead_des_setkey(struct crypto_aead *aead, const u8 *key,
				 unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];

	if (unlikely(!des_ekey(tmp, key)) &&
	    (crypto_aead_get_flags(aead)) & CRYPTO_TFM_REQ_WEAK_KEY) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->cipher_key, key, len);
	ctx->cipher_key_len = len;

	return 0;
}

/* Set the key for the AES block cipher component of the AEAD transform. */
static int spacc_aead_aes_setkey(struct crypto_aead *aead, const u8 *key,
				 unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	/*
	 * IPSec engine only supports 128 and 256 bit AES keys. If we get a
	 * request for any other size (192 bits) then we need to do a software
	 * fallback.
	 */
	if (len != AES_KEYSIZE_128 && len != AES_KEYSIZE_256) {
		/*
		 * Set the fallback transform to use the same request flags as
		 * the hardware transform.
		 */
		ctx->sw_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
		ctx->sw_cipher->base.crt_flags |=
			tfm->crt_flags & CRYPTO_TFM_REQ_MASK;
		return crypto_aead_setkey(ctx->sw_cipher, key, len);
	}

	memcpy(ctx->cipher_key, key, len);
	ctx->cipher_key_len = len;

	return 0;
}

static int spacc_aead_setkey(struct crypto_aead *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct spacc_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct spacc_alg *alg = to_spacc_alg(tfm->base.__crt_alg);
	struct rtattr *rta = (void *)key;
	struct crypto_authenc_key_param *param;
	unsigned int authkeylen, enckeylen;
	int err = -EINVAL;

	if (!RTA_OK(rta, keylen))
		goto badkey;

	if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
		goto badkey;

	if (RTA_PAYLOAD(rta) < sizeof(*param))
		goto badkey;

	param = RTA_DATA(rta);
	enckeylen = be32_to_cpu(param->enckeylen);

	key += RTA_ALIGN(rta->rta_len);
	keylen -= RTA_ALIGN(rta->rta_len);

	if (keylen < enckeylen)
		goto badkey;

	authkeylen = keylen - enckeylen;

	if (enckeylen > AES_MAX_KEY_SIZE)
		goto badkey;

	if ((alg->ctrl_default & SPACC_CRYPTO_ALG_MASK) ==
	    SPA_CTRL_CIPH_ALG_AES)
		err = spacc_aead_aes_setkey(tfm, key + authkeylen, enckeylen);
	else
		err = spacc_aead_des_setkey(tfm, key + authkeylen, enckeylen);

	if (err)
		goto badkey;

	memcpy(ctx->hash_ctx, key, authkeylen);
	ctx->hash_key_len = authkeylen;

	return 0;

badkey:
	crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int spacc_aead_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(crypto_aead_tfm(tfm));

	ctx->auth_size = authsize;

	return 0;
}

/*
 * Check if an AEAD request requires a fallback operation. Some requests can't
 * be completed in hardware because the hardware may not support certain key
 * sizes. In these cases we need to complete the request in software.
 */
static int spacc_aead_need_fallback(struct spacc_req *req)
{
	struct aead_request *aead_req;
	struct crypto_tfm *tfm = req->req->tfm;
	struct crypto_alg *alg = req->req->tfm->__crt_alg;
	struct spacc_alg *spacc_alg = to_spacc_alg(alg);
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	aead_req = container_of(req->req, struct aead_request, base);
	/*
	 * If we have a non-supported key-length, then we need to do a
	 * software fallback.
	 */
	if ((spacc_alg->ctrl_default & SPACC_CRYPTO_ALG_MASK) ==
	    SPA_CTRL_CIPH_ALG_AES &&
	    ctx->cipher_key_len != AES_KEYSIZE_128 &&
	    ctx->cipher_key_len != AES_KEYSIZE_256)
		return 1;

	return 0;
}

static int spacc_aead_do_fallback(struct aead_request *req, unsigned alg_type,
				  bool is_encrypt)
{
	struct crypto_tfm *old_tfm = crypto_aead_tfm(crypto_aead_reqtfm(req));
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(old_tfm);
	int err;

	if (ctx->sw_cipher) {
		/*
		 * Change the request to use the software fallback transform,
		 * and once the ciphering has completed, put the old transform
		 * back into the request.
		 */
		aead_request_set_tfm(req, ctx->sw_cipher);
		err = is_encrypt ? crypto_aead_encrypt(req) :
		    crypto_aead_decrypt(req);
		aead_request_set_tfm(req, __crypto_aead_cast(old_tfm));
	} else
		err = -EINVAL;

	return err;
}

static void spacc_aead_complete(struct spacc_req *req)
{
	spacc_aead_free_ddts(req);
	req->req->complete(req->req, req->result);
}

static int spacc_aead_submit(struct spacc_req *req)
{
	struct crypto_tfm *tfm = req->req->tfm;
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = req->req->tfm->__crt_alg;
	struct spacc_alg *spacc_alg = to_spacc_alg(alg);
	struct spacc_engine *engine = ctx->generic.engine;
	u32 ctrl, proc_len, assoc_len;
	struct aead_request *aead_req =
		container_of(req->req, struct aead_request, base);

	req->result = -EINPROGRESS;
	req->ctx_id = spacc_load_ctx(&ctx->generic, ctx->cipher_key,
		ctx->cipher_key_len, aead_req->iv, alg->cra_aead.ivsize,
		ctx->hash_ctx, ctx->hash_key_len);

	/* Set the source and destination DDT pointers. */
	writel(req->src_addr, engine->regs + SPA_SRC_PTR_REG_OFFSET);
	writel(req->dst_addr, engine->regs + SPA_DST_PTR_REG_OFFSET);
	writel(0, engine->regs + SPA_OFFSET_REG_OFFSET);

	assoc_len = aead_req->assoclen;
	proc_len = aead_req->cryptlen + assoc_len;

	/*
	 * If we aren't generating an IV, then we need to include the IV in the
	 * associated data so that it is included in the hash.
	 */
	if (!req->giv) {
		assoc_len += crypto_aead_ivsize(crypto_aead_reqtfm(aead_req));
		proc_len += crypto_aead_ivsize(crypto_aead_reqtfm(aead_req));
	} else
		proc_len += req->giv_len;

	/*
	 * If we are decrypting, we need to take the length of the ICV out of
	 * the processing length.
	 */
	if (!req->is_encrypt)
		proc_len -= ctx->auth_size;

	writel(proc_len, engine->regs + SPA_PROC_LEN_REG_OFFSET);
	writel(assoc_len, engine->regs + SPA_AAD_LEN_REG_OFFSET);
	writel(ctx->auth_size, engine->regs + SPA_ICV_LEN_REG_OFFSET);
	writel(0, engine->regs + SPA_ICV_OFFSET_REG_OFFSET);
	writel(0, engine->regs + SPA_AUX_INFO_REG_OFFSET);

	ctrl = spacc_alg->ctrl_default | (req->ctx_id << SPA_CTRL_CTX_IDX) |
		(1 << SPA_CTRL_ICV_APPEND);
	if (req->is_encrypt)
		ctrl |= (1 << SPA_CTRL_ENCRYPT_IDX) | (1 << SPA_CTRL_AAD_COPY);
	else
		ctrl |= (1 << SPA_CTRL_KEY_EXP);

	mod_timer(&engine->packet_timeout, jiffies + PACKET_TIMEOUT);

	writel(ctrl, engine->regs + SPA_CTRL_REG_OFFSET);

	return -EINPROGRESS;
}

static int spacc_req_submit(struct spacc_req *req);

static void spacc_push(struct spacc_engine *engine)
{
	struct spacc_req *req;

	while (!list_empty(&engine->pending) &&
	       engine->in_flight + 1 <= engine->fifo_sz) {

		++engine->in_flight;
		req = list_first_entry(&engine->pending, struct spacc_req,
				       list);
		list_move_tail(&req->list, &engine->in_progress);

		req->result = spacc_req_submit(req);
	}
}

/*
 * Setup an AEAD request for processing. This will configure the engine, load
 * the context and then start the packet processing.
 *
 * @giv Pointer to destination address for a generated IV. If the
 *	request does not need to generate an IV then this should be set to NULL.
 */
static int spacc_aead_setup(struct aead_request *req, u8 *giv,
			    unsigned alg_type, bool is_encrypt)
{
	struct crypto_alg *alg = req->base.tfm->__crt_alg;
	struct spacc_engine *engine = to_spacc_alg(alg)->engine;
	struct spacc_req *dev_req = aead_request_ctx(req);
	int err = -EINPROGRESS;
	unsigned long flags;
	unsigned ivsize = crypto_aead_ivsize(crypto_aead_reqtfm(req));

	dev_req->giv		= giv;
	dev_req->giv_len	= ivsize;
	dev_req->req		= &req->base;
	dev_req->is_encrypt	= is_encrypt;
	dev_req->result		= -EBUSY;
	dev_req->engine		= engine;
	dev_req->complete	= spacc_aead_complete;

	if (unlikely(spacc_aead_need_fallback(dev_req)))
		return spacc_aead_do_fallback(req, alg_type, is_encrypt);

	spacc_aead_make_ddts(dev_req, dev_req->giv);

	err = -EINPROGRESS;
	spin_lock_irqsave(&engine->hw_lock, flags);
	if (unlikely(spacc_fifo_cmd_full(engine)) ||
	    engine->in_flight + 1 > engine->fifo_sz) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
			err = -EBUSY;
			spin_unlock_irqrestore(&engine->hw_lock, flags);
			goto out_free_ddts;
		}
		list_add_tail(&dev_req->list, &engine->pending);
	} else {
		list_add_tail(&dev_req->list, &engine->pending);
		spacc_push(engine);
	}
	spin_unlock_irqrestore(&engine->hw_lock, flags);

	goto out;

out_free_ddts:
	spacc_aead_free_ddts(dev_req);
out:
	return err;
}

static int spacc_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct spacc_alg *alg = to_spacc_alg(tfm->__crt_alg);

	return spacc_aead_setup(req, NULL, alg->type, 1);
}

static int spacc_aead_givencrypt(struct aead_givcrypt_request *req)
{
	struct crypto_aead *tfm = aead_givcrypt_reqtfm(req);
	struct spacc_aead_ctx *ctx = crypto_aead_ctx(tfm);
	size_t ivsize = crypto_aead_ivsize(tfm);
	struct spacc_alg *alg = to_spacc_alg(tfm->base.__crt_alg);
	unsigned len;
	__be64 seq;

	memcpy(req->areq.iv, ctx->salt, ivsize);
	len = ivsize;
	if (ivsize > sizeof(u64)) {
		memset(req->giv, 0, ivsize - sizeof(u64));
		len = sizeof(u64);
	}
	seq = cpu_to_be64(req->seq);
	memcpy(req->giv + ivsize - len, &seq, len);

	return spacc_aead_setup(&req->areq, req->giv, alg->type, 1);
}

static int spacc_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct spacc_alg *alg = to_spacc_alg(tfm->__crt_alg);

	return spacc_aead_setup(req, NULL, alg->type, 0);
}

/*
 * Initialise a new AEAD context. This is responsible for allocating the
 * fallback cipher and initialising the context.
 */
static int spacc_aead_cra_init(struct crypto_tfm *tfm)
{
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct spacc_alg *spacc_alg = to_spacc_alg(alg);
	struct spacc_engine *engine = spacc_alg->engine;

	ctx->generic.flags = spacc_alg->type;
	ctx->generic.engine = engine;
	ctx->sw_cipher = crypto_alloc_aead(alg->cra_name, 0,
					   CRYPTO_ALG_ASYNC |
					   CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->sw_cipher)) {
		dev_warn(engine->dev, "failed to allocate fallback for %s\n",
			 alg->cra_name);
		ctx->sw_cipher = NULL;
	}
	ctx->generic.key_offs = spacc_alg->key_offs;
	ctx->generic.iv_offs = spacc_alg->iv_offs;

	get_random_bytes(ctx->salt, sizeof(ctx->salt));

	tfm->crt_aead.reqsize = sizeof(struct spacc_req);

	return 0;
}

/*
 * Destructor for an AEAD context. This is called when the transform is freed
 * and must free the fallback cipher.
 */
static void spacc_aead_cra_exit(struct crypto_tfm *tfm)
{
	struct spacc_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->sw_cipher)
		crypto_free_aead(ctx->sw_cipher);
	ctx->sw_cipher = NULL;
}

/*
 * Set the DES key for a block cipher transform. This also performs weak key
 * checking if the transform has requested it.
 */
static int spacc_des_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			    unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];

	if (len > DES3_EDE_KEY_SIZE) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if (unlikely(!des_ekey(tmp, key)) &&
	    (crypto_ablkcipher_get_flags(cipher) & CRYPTO_TFM_REQ_WEAK_KEY)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->key, key, len);
	ctx->key_len = len;

	return 0;
}

/*
 * Set the key for an AES block cipher. Some key lengths are not supported in
 * hardware so this must also check whether a fallback is needed.
 */
static int spacc_aes_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			    unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(tfm);
	int err = 0;

	if (len > AES_MAX_KEY_SIZE) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	/*
	 * IPSec engine only supports 128 and 256 bit AES keys. If we get a
	 * request for any other size (192 bits) then we need to do a software
	 * fallback.
	 */
	if ((len != AES_KEYSIZE_128 || len != AES_KEYSIZE_256) &&
	    ctx->sw_cipher) {
		/*
		 * Set the fallback transform to use the same request flags as
		 * the hardware transform.
		 */
		ctx->sw_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
		ctx->sw_cipher->base.crt_flags |=
			cipher->base.crt_flags & CRYPTO_TFM_REQ_MASK;

		err = crypto_ablkcipher_setkey(ctx->sw_cipher, key, len);
		if (err)
			goto sw_setkey_failed;
	} else if ((len != AES_KEYSIZE_128 || len != AES_KEYSIZE_256) &&
		   !ctx->sw_cipher)
		err = -EINVAL;

	memcpy(ctx->key, key, len);
	ctx->key_len = len;

sw_setkey_failed:
	if (err && ctx->sw_cipher) {
		tfm->crt_flags &= ~CRYPTO_TFM_RES_MASK;
		tfm->crt_flags |=
			ctx->sw_cipher->base.crt_flags & CRYPTO_TFM_RES_MASK;
	}

	return err;
}

static int spacc_kasumi_f8_setkey(struct crypto_ablkcipher *cipher,
				  const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(tfm);
	int err = 0;

	if (len > AES_MAX_KEY_SIZE) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		err = -EINVAL;
		goto out;
	}

	memcpy(ctx->key, key, len);
	ctx->key_len = len;

out:
	return err;
}

static int spacc_ablk_need_fallback(struct spacc_req *req)
{
	struct spacc_ablk_ctx *ctx;
	struct crypto_tfm *tfm = req->req->tfm;
	struct crypto_alg *alg = req->req->tfm->__crt_alg;
	struct spacc_alg *spacc_alg = to_spacc_alg(alg);

	ctx = crypto_tfm_ctx(tfm);

	return (spacc_alg->ctrl_default & SPACC_CRYPTO_ALG_MASK) ==
			SPA_CTRL_CIPH_ALG_AES &&
			ctx->key_len != AES_KEYSIZE_128 &&
			ctx->key_len != AES_KEYSIZE_256;
}

static void spacc_ablk_complete(struct spacc_req *req)
{
	struct ablkcipher_request *ablk_req =
		container_of(req->req, struct ablkcipher_request, base);

	if (ablk_req->src != ablk_req->dst) {
		spacc_free_ddt(req, req->src_ddt, req->src_addr, ablk_req->src,
			       ablk_req->nbytes, DMA_TO_DEVICE);
		spacc_free_ddt(req, req->dst_ddt, req->dst_addr, ablk_req->dst,
			       ablk_req->nbytes, DMA_FROM_DEVICE);
	} else
		spacc_free_ddt(req, req->dst_ddt, req->dst_addr, ablk_req->dst,
			       ablk_req->nbytes, DMA_BIDIRECTIONAL);

	req->req->complete(req->req, req->result);
}

static int spacc_ablk_submit(struct spacc_req *req)
{
	struct crypto_tfm *tfm = req->req->tfm;
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(tfm);
	struct ablkcipher_request *ablk_req = ablkcipher_request_cast(req->req);
	struct crypto_alg *alg = req->req->tfm->__crt_alg;
	struct spacc_alg *spacc_alg = to_spacc_alg(alg);
	struct spacc_engine *engine = ctx->generic.engine;
	u32 ctrl;

	req->ctx_id = spacc_load_ctx(&ctx->generic, ctx->key,
		ctx->key_len, ablk_req->info, alg->cra_ablkcipher.ivsize,
		NULL, 0);

	writel(req->src_addr, engine->regs + SPA_SRC_PTR_REG_OFFSET);
	writel(req->dst_addr, engine->regs + SPA_DST_PTR_REG_OFFSET);
	writel(0, engine->regs + SPA_OFFSET_REG_OFFSET);

	writel(ablk_req->nbytes, engine->regs + SPA_PROC_LEN_REG_OFFSET);
	writel(0, engine->regs + SPA_ICV_OFFSET_REG_OFFSET);
	writel(0, engine->regs + SPA_AUX_INFO_REG_OFFSET);
	writel(0, engine->regs + SPA_AAD_LEN_REG_OFFSET);

	ctrl = spacc_alg->ctrl_default | (req->ctx_id << SPA_CTRL_CTX_IDX) |
		(req->is_encrypt ? (1 << SPA_CTRL_ENCRYPT_IDX) :
		 (1 << SPA_CTRL_KEY_EXP));

	mod_timer(&engine->packet_timeout, jiffies + PACKET_TIMEOUT);

	writel(ctrl, engine->regs + SPA_CTRL_REG_OFFSET);

	return -EINPROGRESS;
}

static int spacc_ablk_do_fallback(struct ablkcipher_request *req,
				  unsigned alg_type, bool is_encrypt)
{
	struct crypto_tfm *old_tfm =
	    crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(old_tfm);
	int err;

	if (!ctx->sw_cipher)
		return -EINVAL;

	/*
	 * Change the request to use the software fallback transform, and once
	 * the ciphering has completed, put the old transform back into the
	 * request.
	 */
	ablkcipher_request_set_tfm(req, ctx->sw_cipher);
	err = is_encrypt ? crypto_ablkcipher_encrypt(req) :
		crypto_ablkcipher_decrypt(req);
	ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(old_tfm));

	return err;
}

static int spacc_ablk_setup(struct ablkcipher_request *req, unsigned alg_type,
			    bool is_encrypt)
{
	struct crypto_alg *alg = req->base.tfm->__crt_alg;
	struct spacc_engine *engine = to_spacc_alg(alg)->engine;
	struct spacc_req *dev_req = ablkcipher_request_ctx(req);
	unsigned long flags;
	int err = -ENOMEM;

	dev_req->req		= &req->base;
	dev_req->is_encrypt	= is_encrypt;
	dev_req->engine		= engine;
	dev_req->complete	= spacc_ablk_complete;
	dev_req->result		= -EINPROGRESS;

	if (unlikely(spacc_ablk_need_fallback(dev_req)))
		return spacc_ablk_do_fallback(req, alg_type, is_encrypt);

	/*
	 * Create the DDT's for the engine. If we share the same source and
	 * destination then we can optimize by reusing the DDT's.
	 */
	if (req->src != req->dst) {
		dev_req->src_ddt = spacc_sg_to_ddt(engine, req->src,
			req->nbytes, DMA_TO_DEVICE, &dev_req->src_addr);
		if (!dev_req->src_ddt)
			goto out;

		dev_req->dst_ddt = spacc_sg_to_ddt(engine, req->dst,
			req->nbytes, DMA_FROM_DEVICE, &dev_req->dst_addr);
		if (!dev_req->dst_ddt)
			goto out_free_src;
	} else {
		dev_req->dst_ddt = spacc_sg_to_ddt(engine, req->dst,
			req->nbytes, DMA_BIDIRECTIONAL, &dev_req->dst_addr);
		if (!dev_req->dst_ddt)
			goto out;

		dev_req->src_ddt = NULL;
		dev_req->src_addr = dev_req->dst_addr;
	}

	err = -EINPROGRESS;
	spin_lock_irqsave(&engine->hw_lock, flags);
	/*
	 * Check if the engine will accept the operation now. If it won't then
	 * we either stick it on the end of a pending list if we can backlog,
	 * or bailout with an error if not.
	 */
	if (unlikely(spacc_fifo_cmd_full(engine)) ||
	    engine->in_flight + 1 > engine->fifo_sz) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
			err = -EBUSY;
			spin_unlock_irqrestore(&engine->hw_lock, flags);
			goto out_free_ddts;
		}
		list_add_tail(&dev_req->list, &engine->pending);
	} else {
		list_add_tail(&dev_req->list, &engine->pending);
		spacc_push(engine);
	}
	spin_unlock_irqrestore(&engine->hw_lock, flags);

	goto out;

out_free_ddts:
	spacc_free_ddt(dev_req, dev_req->dst_ddt, dev_req->dst_addr, req->dst,
		       req->nbytes, req->src == req->dst ?
		       DMA_BIDIRECTIONAL : DMA_FROM_DEVICE);
out_free_src:
	if (req->src != req->dst)
		spacc_free_ddt(dev_req, dev_req->src_ddt, dev_req->src_addr,
			       req->src, req->nbytes, DMA_TO_DEVICE);
out:
	return err;
}

static int spacc_ablk_cra_init(struct crypto_tfm *tfm)
{
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct spacc_alg *spacc_alg = to_spacc_alg(alg);
	struct spacc_engine *engine = spacc_alg->engine;

	ctx->generic.flags = spacc_alg->type;
	ctx->generic.engine = engine;
	if (alg->cra_flags & CRYPTO_ALG_NEED_FALLBACK) {
		ctx->sw_cipher = crypto_alloc_ablkcipher(alg->cra_name, 0,
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(ctx->sw_cipher)) {
			dev_warn(engine->dev, "failed to allocate fallback for %s\n",
				 alg->cra_name);
			ctx->sw_cipher = NULL;
		}
	}
	ctx->generic.key_offs = spacc_alg->key_offs;
	ctx->generic.iv_offs = spacc_alg->iv_offs;

	tfm->crt_ablkcipher.reqsize = sizeof(struct spacc_req);

	return 0;
}

static void spacc_ablk_cra_exit(struct crypto_tfm *tfm)
{
	struct spacc_ablk_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->sw_cipher)
		crypto_free_ablkcipher(ctx->sw_cipher);
	ctx->sw_cipher = NULL;
}

static int spacc_ablk_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct spacc_alg *alg = to_spacc_alg(tfm->__crt_alg);

	return spacc_ablk_setup(req, alg->type, 1);
}

static int spacc_ablk_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct spacc_alg *alg = to_spacc_alg(tfm->__crt_alg);

	return spacc_ablk_setup(req, alg->type, 0);
}

static inline int spacc_fifo_stat_empty(struct spacc_engine *engine)
{
	return readl(engine->regs + SPA_FIFO_STAT_REG_OFFSET) &
		SPA_FIFO_STAT_EMPTY;
}

static void spacc_process_done(struct spacc_engine *engine)
{
	struct spacc_req *req;
	unsigned long flags;

	spin_lock_irqsave(&engine->hw_lock, flags);

	while (!spacc_fifo_stat_empty(engine)) {
		req = list_first_entry(&engine->in_progress, struct spacc_req,
				       list);
		list_move_tail(&req->list, &engine->completed);
		--engine->in_flight;

		/* POP the status register. */
		writel(~0, engine->regs + SPA_STAT_POP_REG_OFFSET);
		req->result = (readl(engine->regs + SPA_STATUS_REG_OFFSET) &
		     SPA_STATUS_RES_CODE_MASK) >> SPA_STATUS_RES_CODE_OFFSET;

		/*
		 * Convert the SPAcc error status into the standard POSIX error
		 * codes.
		 */
		if (unlikely(req->result)) {
			switch (req->result) {
			case SPA_STATUS_ICV_FAIL:
				req->result = -EBADMSG;
				break;

			case SPA_STATUS_MEMORY_ERROR:
				dev_warn(engine->dev,
					 "memory error triggered\n");
				req->result = -EFAULT;
				break;

			case SPA_STATUS_BLOCK_ERROR:
				dev_warn(engine->dev,
					 "block error triggered\n");
				req->result = -EIO;
				break;
			}
		}
	}

	tasklet_schedule(&engine->complete);

	spin_unlock_irqrestore(&engine->hw_lock, flags);
}

static irqreturn_t spacc_spacc_irq(int irq, void *dev)
{
	struct spacc_engine *engine = (struct spacc_engine *)dev;
	u32 spacc_irq_stat = readl(engine->regs + SPA_IRQ_STAT_REG_OFFSET);

	writel(spacc_irq_stat, engine->regs + SPA_IRQ_STAT_REG_OFFSET);
	spacc_process_done(engine);

	return IRQ_HANDLED;
}

static void spacc_packet_timeout(unsigned long data)
{
	struct spacc_engine *engine = (struct spacc_engine *)data;

	spacc_process_done(engine);
}

static int spacc_req_submit(struct spacc_req *req)
{
	struct crypto_alg *alg = req->req->tfm->__crt_alg;

	if (CRYPTO_ALG_TYPE_AEAD == (CRYPTO_ALG_TYPE_MASK & alg->cra_flags))
		return spacc_aead_submit(req);
	else
		return spacc_ablk_submit(req);
}

static void spacc_spacc_complete(unsigned long data)
{
	struct spacc_engine *engine = (struct spacc_engine *)data;
	struct spacc_req *req, *tmp;
	unsigned long flags;
	LIST_HEAD(completed);

	spin_lock_irqsave(&engine->hw_lock, flags);

	list_splice_init(&engine->completed, &completed);
	spacc_push(engine);
	if (engine->in_flight)
		mod_timer(&engine->packet_timeout, jiffies + PACKET_TIMEOUT);

	spin_unlock_irqrestore(&engine->hw_lock, flags);

	list_for_each_entry_safe(req, tmp, &completed, list) {
		list_del(&req->list);
		req->complete(req);
	}
}

#ifdef CONFIG_PM
static int spacc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spacc_engine *engine = platform_get_drvdata(pdev);

	/*
	 * We only support standby mode. All we have to do is gate the clock to
	 * the spacc. The hardware will preserve state until we turn it back
	 * on again.
	 */
	clk_disable(engine->clk);

	return 0;
}

static int spacc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spacc_engine *engine = platform_get_drvdata(pdev);

	return clk_enable(engine->clk);
}

static const struct dev_pm_ops spacc_pm_ops = {
	.suspend	= spacc_suspend,
	.resume		= spacc_resume,
};
#endif /* CONFIG_PM */

static inline struct spacc_engine *spacc_dev_to_engine(struct device *dev)
{
	return dev ? platform_get_drvdata(to_platform_device(dev)) : NULL;
}

static ssize_t spacc_stat_irq_thresh_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct spacc_engine *engine = spacc_dev_to_engine(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", engine->stat_irq_thresh);
}

static ssize_t spacc_stat_irq_thresh_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t len)
{
	struct spacc_engine *engine = spacc_dev_to_engine(dev);
	unsigned long thresh;

	if (strict_strtoul(buf, 0, &thresh))
		return -EINVAL;

	thresh = clamp(thresh, 1UL, engine->fifo_sz - 1);

	engine->stat_irq_thresh = thresh;
	writel(engine->stat_irq_thresh << SPA_IRQ_CTRL_STAT_CNT_OFFSET,
	       engine->regs + SPA_IRQ_CTRL_REG_OFFSET);

	return len;
}
static DEVICE_ATTR(stat_irq_thresh, 0644, spacc_stat_irq_thresh_show,
		   spacc_stat_irq_thresh_store);

static struct spacc_alg ipsec_engine_algs[] = {
	{
		.ctrl_default = SPA_CTRL_CIPH_ALG_AES | SPA_CTRL_CIPH_MODE_CBC,
		.key_offs = 0,
		.iv_offs = AES_MAX_KEY_SIZE,
		.alg = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_aes_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = AES_MIN_KEY_SIZE,
				.max_keysize = AES_MAX_KEY_SIZE,
				.ivsize = AES_BLOCK_SIZE,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
	{
		.key_offs = 0,
		.iv_offs = AES_MAX_KEY_SIZE,
		.ctrl_default = SPA_CTRL_CIPH_ALG_AES | SPA_CTRL_CIPH_MODE_ECB,
		.alg = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_aes_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = AES_MIN_KEY_SIZE,
				.max_keysize = AES_MAX_KEY_SIZE,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_DES | SPA_CTRL_CIPH_MODE_CBC,
		.alg = {
			.cra_name = "cbc(des)",
			.cra_driver_name = "cbc-des-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_des_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = DES_KEY_SIZE,
				.max_keysize = DES_KEY_SIZE,
				.ivsize = DES_BLOCK_SIZE,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_DES | SPA_CTRL_CIPH_MODE_ECB,
		.alg = {
			.cra_name = "ecb(des)",
			.cra_driver_name = "ecb-des-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_des_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = DES_KEY_SIZE,
				.max_keysize = DES_KEY_SIZE,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_DES | SPA_CTRL_CIPH_MODE_CBC,
		.alg = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "cbc-des3-ede-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_des_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = DES3_EDE_KEY_SIZE,
				.max_keysize = DES3_EDE_KEY_SIZE,
				.ivsize = DES3_EDE_BLOCK_SIZE,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_DES | SPA_CTRL_CIPH_MODE_ECB,
		.alg = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "ecb-des3-ede-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_des_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = DES3_EDE_KEY_SIZE,
				.max_keysize = DES3_EDE_KEY_SIZE,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
	{
		.ctrl_default = SPA_CTRL_CIPH_ALG_AES | SPA_CTRL_CIPH_MODE_CBC |
				SPA_CTRL_HASH_ALG_SHA | SPA_CTRL_HASH_MODE_HMAC,
		.key_offs = 0,
		.iv_offs = AES_MAX_KEY_SIZE,
		.alg = {
			.cra_name = "authenc(hmac(sha1),cbc(aes))",
			.cra_driver_name = "authenc-hmac-sha1-cbc-aes-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_aead_ctx),
			.cra_type = &crypto_aead_type,
			.cra_module = THIS_MODULE,
			.cra_aead = {
				.setkey = spacc_aead_setkey,
				.setauthsize = spacc_aead_setauthsize,
				.encrypt = spacc_aead_encrypt,
				.decrypt = spacc_aead_decrypt,
				.givencrypt = spacc_aead_givencrypt,
				.ivsize = AES_BLOCK_SIZE,
				.maxauthsize = SHA1_DIGEST_SIZE,
			},
			.cra_init = spacc_aead_cra_init,
			.cra_exit = spacc_aead_cra_exit,
		},
	},
	{
		.ctrl_default = SPA_CTRL_CIPH_ALG_AES | SPA_CTRL_CIPH_MODE_CBC |
				SPA_CTRL_HASH_ALG_SHA256 |
				SPA_CTRL_HASH_MODE_HMAC,
		.key_offs = 0,
		.iv_offs = AES_MAX_KEY_SIZE,
		.alg = {
			.cra_name = "authenc(hmac(sha256),cbc(aes))",
			.cra_driver_name = "authenc-hmac-sha256-cbc-aes-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_aead_ctx),
			.cra_type = &crypto_aead_type,
			.cra_module = THIS_MODULE,
			.cra_aead = {
				.setkey = spacc_aead_setkey,
				.setauthsize = spacc_aead_setauthsize,
				.encrypt = spacc_aead_encrypt,
				.decrypt = spacc_aead_decrypt,
				.givencrypt = spacc_aead_givencrypt,
				.ivsize = AES_BLOCK_SIZE,
				.maxauthsize = SHA256_DIGEST_SIZE,
			},
			.cra_init = spacc_aead_cra_init,
			.cra_exit = spacc_aead_cra_exit,
		},
	},
	{
		.key_offs = 0,
		.iv_offs = AES_MAX_KEY_SIZE,
		.ctrl_default = SPA_CTRL_CIPH_ALG_AES | SPA_CTRL_CIPH_MODE_CBC |
				SPA_CTRL_HASH_ALG_MD5 | SPA_CTRL_HASH_MODE_HMAC,
		.alg = {
			.cra_name = "authenc(hmac(md5),cbc(aes))",
			.cra_driver_name = "authenc-hmac-md5-cbc-aes-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_aead_ctx),
			.cra_type = &crypto_aead_type,
			.cra_module = THIS_MODULE,
			.cra_aead = {
				.setkey = spacc_aead_setkey,
				.setauthsize = spacc_aead_setauthsize,
				.encrypt = spacc_aead_encrypt,
				.decrypt = spacc_aead_decrypt,
				.givencrypt = spacc_aead_givencrypt,
				.ivsize = AES_BLOCK_SIZE,
				.maxauthsize = MD5_DIGEST_SIZE,
			},
			.cra_init = spacc_aead_cra_init,
			.cra_exit = spacc_aead_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_DES | SPA_CTRL_CIPH_MODE_CBC |
				SPA_CTRL_HASH_ALG_SHA | SPA_CTRL_HASH_MODE_HMAC,
		.alg = {
			.cra_name = "authenc(hmac(sha1),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha1-cbc-3des-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_aead_ctx),
			.cra_type = &crypto_aead_type,
			.cra_module = THIS_MODULE,
			.cra_aead = {
				.setkey = spacc_aead_setkey,
				.setauthsize = spacc_aead_setauthsize,
				.encrypt = spacc_aead_encrypt,
				.decrypt = spacc_aead_decrypt,
				.givencrypt = spacc_aead_givencrypt,
				.ivsize = DES3_EDE_BLOCK_SIZE,
				.maxauthsize = SHA1_DIGEST_SIZE,
			},
			.cra_init = spacc_aead_cra_init,
			.cra_exit = spacc_aead_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_AES | SPA_CTRL_CIPH_MODE_CBC |
				SPA_CTRL_HASH_ALG_SHA256 |
				SPA_CTRL_HASH_MODE_HMAC,
		.alg = {
			.cra_name = "authenc(hmac(sha256),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha256-cbc-3des-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_aead_ctx),
			.cra_type = &crypto_aead_type,
			.cra_module = THIS_MODULE,
			.cra_aead = {
				.setkey = spacc_aead_setkey,
				.setauthsize = spacc_aead_setauthsize,
				.encrypt = spacc_aead_encrypt,
				.decrypt = spacc_aead_decrypt,
				.givencrypt = spacc_aead_givencrypt,
				.ivsize = DES3_EDE_BLOCK_SIZE,
				.maxauthsize = SHA256_DIGEST_SIZE,
			},
			.cra_init = spacc_aead_cra_init,
			.cra_exit = spacc_aead_cra_exit,
		},
	},
	{
		.key_offs = DES_BLOCK_SIZE,
		.iv_offs = 0,
		.ctrl_default = SPA_CTRL_CIPH_ALG_DES | SPA_CTRL_CIPH_MODE_CBC |
				SPA_CTRL_HASH_ALG_MD5 | SPA_CTRL_HASH_MODE_HMAC,
		.alg = {
			.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-md5-cbc-3des-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct spacc_aead_ctx),
			.cra_type = &crypto_aead_type,
			.cra_module = THIS_MODULE,
			.cra_aead = {
				.setkey = spacc_aead_setkey,
				.setauthsize = spacc_aead_setauthsize,
				.encrypt = spacc_aead_encrypt,
				.decrypt = spacc_aead_decrypt,
				.givencrypt = spacc_aead_givencrypt,
				.ivsize = DES3_EDE_BLOCK_SIZE,
				.maxauthsize = MD5_DIGEST_SIZE,
			},
			.cra_init = spacc_aead_cra_init,
			.cra_exit = spacc_aead_cra_exit,
		},
	},
};

static struct spacc_alg l2_engine_algs[] = {
	{
		.key_offs = 0,
		.iv_offs = SPACC_CRYPTO_KASUMI_F8_KEY_LEN,
		.ctrl_default = SPA_CTRL_CIPH_ALG_KASUMI |
				SPA_CTRL_CIPH_MODE_F8,
		.alg = {
			.cra_name = "f8(kasumi)",
			.cra_driver_name = "f8-kasumi-picoxcell",
			.cra_priority = SPACC_CRYPTO_ALG_PRIORITY,
			.cra_flags = CRYPTO_ALG_TYPE_GIVCIPHER | CRYPTO_ALG_ASYNC,
			.cra_blocksize = 8,
			.cra_ctxsize = sizeof(struct spacc_ablk_ctx),
			.cra_type = &crypto_ablkcipher_type,
			.cra_module = THIS_MODULE,
			.cra_ablkcipher = {
				.setkey = spacc_kasumi_f8_setkey,
				.encrypt = spacc_ablk_encrypt,
				.decrypt = spacc_ablk_decrypt,
				.min_keysize = 16,
				.max_keysize = 16,
				.ivsize = 8,
			},
			.cra_init = spacc_ablk_cra_init,
			.cra_exit = spacc_ablk_cra_exit,
		},
	},
};

#ifdef CONFIG_OF
static const struct of_device_id spacc_of_id_table[] = {
	{ .compatible = "picochip,spacc-ipsec" },
	{ .compatible = "picochip,spacc-l2" },
	{}
};
#else /* CONFIG_OF */
#define spacc_of_id_table NULL
#endif /* CONFIG_OF */

static bool spacc_is_compatible(struct platform_device *pdev,
				const char *spacc_type)
{
	const struct platform_device_id *platid = platform_get_device_id(pdev);

	if (platid && !strcmp(platid->name, spacc_type))
		return true;

#ifdef CONFIG_OF
	if (of_device_is_compatible(pdev->dev.of_node, spacc_type))
		return true;
#endif /* CONFIG_OF */

	return false;
}

static int __devinit spacc_probe(struct platform_device *pdev)
{
	int i, err, ret = -EINVAL;
	struct resource *mem, *irq;
	struct spacc_engine *engine = devm_kzalloc(&pdev->dev, sizeof(*engine),
						   GFP_KERNEL);
	if (!engine)
		return -ENOMEM;

	if (spacc_is_compatible(pdev, "picochip,spacc-ipsec")) {
		engine->max_ctxs	= SPACC_CRYPTO_IPSEC_MAX_CTXS;
		engine->cipher_pg_sz	= SPACC_CRYPTO_IPSEC_CIPHER_PG_SZ;
		engine->hash_pg_sz	= SPACC_CRYPTO_IPSEC_HASH_PG_SZ;
		engine->fifo_sz		= SPACC_CRYPTO_IPSEC_FIFO_SZ;
		engine->algs		= ipsec_engine_algs;
		engine->num_algs	= ARRAY_SIZE(ipsec_engine_algs);
	} else if (spacc_is_compatible(pdev, "picochip,spacc-l2")) {
		engine->max_ctxs	= SPACC_CRYPTO_L2_MAX_CTXS;
		engine->cipher_pg_sz	= SPACC_CRYPTO_L2_CIPHER_PG_SZ;
		engine->hash_pg_sz	= SPACC_CRYPTO_L2_HASH_PG_SZ;
		engine->fifo_sz		= SPACC_CRYPTO_L2_FIFO_SZ;
		engine->algs		= l2_engine_algs;
		engine->num_algs	= ARRAY_SIZE(l2_engine_algs);
	} else {
		return -EINVAL;
	}

	engine->name = dev_name(&pdev->dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!mem || !irq) {
		dev_err(&pdev->dev, "no memory/irq resource for engine\n");
		return -ENXIO;
	}

	if (!devm_request_mem_region(&pdev->dev, mem->start, resource_size(mem),
				     engine->name))
		return -ENOMEM;

	engine->regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!engine->regs) {
		dev_err(&pdev->dev, "memory map failed\n");
		return -ENOMEM;
	}

	if (devm_request_irq(&pdev->dev, irq->start, spacc_spacc_irq, 0,
			     engine->name, engine)) {
		dev_err(engine->dev, "failed to request IRQ\n");
		return -EBUSY;
	}

	engine->dev		= &pdev->dev;
	engine->cipher_ctx_base = engine->regs + SPA_CIPH_KEY_BASE_REG_OFFSET;
	engine->hash_key_base	= engine->regs + SPA_HASH_KEY_BASE_REG_OFFSET;

	engine->req_pool = dmam_pool_create(engine->name, engine->dev,
		MAX_DDT_LEN * sizeof(struct spacc_ddt), 8, SZ_64K);
	if (!engine->req_pool)
		return -ENOMEM;

	spin_lock_init(&engine->hw_lock);

	engine->clk = clk_get(&pdev->dev, "ref");
	if (IS_ERR(engine->clk)) {
		dev_info(&pdev->dev, "clk unavailable\n");
		device_remove_file(&pdev->dev, &dev_attr_stat_irq_thresh);
		return PTR_ERR(engine->clk);
	}

	if (clk_enable(engine->clk)) {
		dev_info(&pdev->dev, "unable to enable clk\n");
		clk_put(engine->clk);
		return -EIO;
	}

	err = device_create_file(&pdev->dev, &dev_attr_stat_irq_thresh);
	if (err) {
		clk_disable(engine->clk);
		clk_put(engine->clk);
		return err;
	}


	/*
	 * Use an IRQ threshold of 50% as a default. This seems to be a
	 * reasonable trade off of latency against throughput but can be
	 * changed at runtime.
	 */
	engine->stat_irq_thresh = (engine->fifo_sz / 2);

	/*
	 * Configure the interrupts. We only use the STAT_CNT interrupt as we
	 * only submit a new packet for processing when we complete another in
	 * the queue. This minimizes time spent in the interrupt handler.
	 */
	writel(engine->stat_irq_thresh << SPA_IRQ_CTRL_STAT_CNT_OFFSET,
	       engine->regs + SPA_IRQ_CTRL_REG_OFFSET);
	writel(SPA_IRQ_EN_STAT_EN | SPA_IRQ_EN_GLBL_EN,
	       engine->regs + SPA_IRQ_EN_REG_OFFSET);

	setup_timer(&engine->packet_timeout, spacc_packet_timeout,
		    (unsigned long)engine);

	INIT_LIST_HEAD(&engine->pending);
	INIT_LIST_HEAD(&engine->completed);
	INIT_LIST_HEAD(&engine->in_progress);
	engine->in_flight = 0;
	tasklet_init(&engine->complete, spacc_spacc_complete,
		     (unsigned long)engine);

	platform_set_drvdata(pdev, engine);

	INIT_LIST_HEAD(&engine->registered_algs);
	for (i = 0; i < engine->num_algs; ++i) {
		engine->algs[i].engine = engine;
		err = crypto_register_alg(&engine->algs[i].alg);
		if (!err) {
			list_add_tail(&engine->algs[i].entry,
				      &engine->registered_algs);
			ret = 0;
		}
		if (err)
			dev_err(engine->dev, "failed to register alg \"%s\"\n",
				engine->algs[i].alg.cra_name);
		else
			dev_dbg(engine->dev, "registered alg \"%s\"\n",
				engine->algs[i].alg.cra_name);
	}

	return ret;
}

static int __devexit spacc_remove(struct platform_device *pdev)
{
	struct spacc_alg *alg, *next;
	struct spacc_engine *engine = platform_get_drvdata(pdev);

	del_timer_sync(&engine->packet_timeout);
	device_remove_file(&pdev->dev, &dev_attr_stat_irq_thresh);

	list_for_each_entry_safe(alg, next, &engine->registered_algs, entry) {
		list_del(&alg->entry);
		crypto_unregister_alg(&alg->alg);
	}

	clk_disable(engine->clk);
	clk_put(engine->clk);

	return 0;
}

static const struct platform_device_id spacc_id_table[] = {
	{ "picochip,spacc-ipsec", },
	{ "picochip,spacc-l2", },
};

static struct platform_driver spacc_driver = {
	.probe		= spacc_probe,
	.remove		= __devexit_p(spacc_remove),
	.driver		= {
		.name	= "picochip,spacc",
#ifdef CONFIG_PM
		.pm	= &spacc_pm_ops,
#endif /* CONFIG_PM */
		.of_match_table	= spacc_of_id_table,
	},
	.id_table	= spacc_id_table,
};

static int __init spacc_init(void)
{
	return platform_driver_register(&spacc_driver);
}
module_init(spacc_init);

static void __exit spacc_exit(void)
{
	platform_driver_unregister(&spacc_driver);
}
module_exit(spacc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
