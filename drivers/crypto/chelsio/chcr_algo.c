/*
 * This file is part of the Chelsio T6 Crypto driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Written and Maintained by:
 *	Manoj Malviya (manojmalviya@chelsio.com)
 *	Atul Gupta (atul.gupta@chelsio.com)
 *	Jitendra Lulla (jlulla@chelsio.com)
 *	Yeshaswi M R Gowda (yeshaswi@chelsio.com)
 *	Harsh Jain (harsh@chelsio.com)
 */

#define pr_fmt(fmt) "chcr:" fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/gcm.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/authenc.h>
#include <crypto/ctr.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/aead.h>
#include <crypto/null.h>
#include <crypto/internal/skcipher.h>
#include <crypto/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>

#include "t4fw_api.h"
#include "t4_msg.h"
#include "chcr_core.h"
#include "chcr_algo.h"
#include "chcr_crypto.h"

#define IV AES_BLOCK_SIZE

static unsigned int sgl_ent_len[] = {
	0, 0, 16, 24, 40, 48, 64, 72, 88,
	96, 112, 120, 136, 144, 160, 168, 184,
	192, 208, 216, 232, 240, 256, 264, 280,
	288, 304, 312, 328, 336, 352, 360, 376
};

static unsigned int dsgl_ent_len[] = {
	0, 32, 32, 48, 48, 64, 64, 80, 80,
	112, 112, 128, 128, 144, 144, 160, 160,
	192, 192, 208, 208, 224, 224, 240, 240,
	272, 272, 288, 288, 304, 304, 320, 320
};

static u32 round_constant[11] = {
	0x01000000, 0x02000000, 0x04000000, 0x08000000,
	0x10000000, 0x20000000, 0x40000000, 0x80000000,
	0x1B000000, 0x36000000, 0x6C000000
};

static int chcr_handle_cipher_resp(struct skcipher_request *req,
				   unsigned char *input, int err);

static inline  struct chcr_aead_ctx *AEAD_CTX(struct chcr_context *ctx)
{
	return &ctx->crypto_ctx->aeadctx;
}

static inline struct ablk_ctx *ABLK_CTX(struct chcr_context *ctx)
{
	return &ctx->crypto_ctx->ablkctx;
}

static inline struct hmac_ctx *HMAC_CTX(struct chcr_context *ctx)
{
	return &ctx->crypto_ctx->hmacctx;
}

static inline struct chcr_gcm_ctx *GCM_CTX(struct chcr_aead_ctx *gctx)
{
	return gctx->ctx->gcm;
}

static inline struct chcr_authenc_ctx *AUTHENC_CTX(struct chcr_aead_ctx *gctx)
{
	return gctx->ctx->authenc;
}

static inline struct uld_ctx *ULD_CTX(struct chcr_context *ctx)
{
	return container_of(ctx->dev, struct uld_ctx, dev);
}

static inline void chcr_init_hctx_per_wr(struct chcr_ahash_req_ctx *reqctx)
{
	memset(&reqctx->hctx_wr, 0, sizeof(struct chcr_hctx_per_wr));
}

static int sg_nents_xlen(struct scatterlist *sg, unsigned int reqlen,
			 unsigned int entlen,
			 unsigned int skip)
{
	int nents = 0;
	unsigned int less;
	unsigned int skip_len = 0;

	while (sg && skip) {
		if (sg_dma_len(sg) <= skip) {
			skip -= sg_dma_len(sg);
			skip_len = 0;
			sg = sg_next(sg);
		} else {
			skip_len = skip;
			skip = 0;
		}
	}

	while (sg && reqlen) {
		less = min(reqlen, sg_dma_len(sg) - skip_len);
		nents += DIV_ROUND_UP(less, entlen);
		reqlen -= less;
		skip_len = 0;
		sg = sg_next(sg);
	}
	return nents;
}

static inline int get_aead_subtype(struct crypto_aead *aead)
{
	struct aead_alg *alg = crypto_aead_alg(aead);
	struct chcr_alg_template *chcr_crypto_alg =
		container_of(alg, struct chcr_alg_template, alg.aead);
	return chcr_crypto_alg->type & CRYPTO_ALG_SUB_TYPE_MASK;
}

void chcr_verify_tag(struct aead_request *req, u8 *input, int *err)
{
	u8 temp[SHA512_DIGEST_SIZE];
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	int authsize = crypto_aead_authsize(tfm);
	struct cpl_fw6_pld *fw6_pld;
	int cmp = 0;

	fw6_pld = (struct cpl_fw6_pld *)input;
	if ((get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106) ||
	    (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_GCM)) {
		cmp = crypto_memneq(&fw6_pld->data[2], (fw6_pld + 1), authsize);
	} else {

		sg_pcopy_to_buffer(req->src, sg_nents(req->src), temp,
				authsize, req->assoclen +
				req->cryptlen - authsize);
		cmp = crypto_memneq(temp, (fw6_pld + 1), authsize);
	}
	if (cmp)
		*err = -EBADMSG;
	else
		*err = 0;
}

static int chcr_inc_wrcount(struct chcr_dev *dev)
{
	if (dev->state == CHCR_DETACH)
		return 1;
	atomic_inc(&dev->inflight);
	return 0;
}

static inline void chcr_dec_wrcount(struct chcr_dev *dev)
{
	atomic_dec(&dev->inflight);
}

static inline int chcr_handle_aead_resp(struct aead_request *req,
					 unsigned char *input,
					 int err)
{
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_dev *dev = a_ctx(tfm)->dev;

	chcr_aead_common_exit(req);
	if (reqctx->verify == VERIFY_SW) {
		chcr_verify_tag(req, input, &err);
		reqctx->verify = VERIFY_HW;
	}
	chcr_dec_wrcount(dev);
	aead_request_complete(req, err);

	return err;
}

static void get_aes_decrypt_key(unsigned char *dec_key,
				       const unsigned char *key,
				       unsigned int keylength)
{
	u32 temp;
	u32 w_ring[MAX_NK];
	int i, j, k;
	u8  nr, nk;

	switch (keylength) {
	case AES_KEYLENGTH_128BIT:
		nk = KEYLENGTH_4BYTES;
		nr = NUMBER_OF_ROUNDS_10;
		break;
	case AES_KEYLENGTH_192BIT:
		nk = KEYLENGTH_6BYTES;
		nr = NUMBER_OF_ROUNDS_12;
		break;
	case AES_KEYLENGTH_256BIT:
		nk = KEYLENGTH_8BYTES;
		nr = NUMBER_OF_ROUNDS_14;
		break;
	default:
		return;
	}
	for (i = 0; i < nk; i++)
		w_ring[i] = get_unaligned_be32(&key[i * 4]);

	i = 0;
	temp = w_ring[nk - 1];
	while (i + nk < (nr + 1) * 4) {
		if (!(i % nk)) {
			/* RotWord(temp) */
			temp = (temp << 8) | (temp >> 24);
			temp = aes_ks_subword(temp);
			temp ^= round_constant[i / nk];
		} else if (nk == 8 && (i % 4 == 0)) {
			temp = aes_ks_subword(temp);
		}
		w_ring[i % nk] ^= temp;
		temp = w_ring[i % nk];
		i++;
	}
	i--;
	for (k = 0, j = i % nk; k < nk; k++) {
		put_unaligned_be32(w_ring[j], &dec_key[k * 4]);
		j--;
		if (j < 0)
			j += nk;
	}
}

static int chcr_prepare_hmac_key(const u8 *raw_key, unsigned int raw_key_len,
				 int digestsize, void *istate, void *ostate)
{
	__be32 *istate32 = istate, *ostate32 = ostate;
	__be64 *istate64 = istate, *ostate64 = ostate;
	union {
		struct hmac_sha1_key sha1;
		struct hmac_sha224_key sha224;
		struct hmac_sha256_key sha256;
		struct hmac_sha384_key sha384;
		struct hmac_sha512_key sha512;
	} k;

	switch (digestsize) {
	case SHA1_DIGEST_SIZE:
		hmac_sha1_preparekey(&k.sha1, raw_key, raw_key_len);
		for (int i = 0; i < ARRAY_SIZE(k.sha1.istate.h); i++) {
			istate32[i] = cpu_to_be32(k.sha1.istate.h[i]);
			ostate32[i] = cpu_to_be32(k.sha1.ostate.h[i]);
		}
		break;
	case SHA224_DIGEST_SIZE:
		hmac_sha224_preparekey(&k.sha224, raw_key, raw_key_len);
		for (int i = 0; i < ARRAY_SIZE(k.sha224.key.istate.h); i++) {
			istate32[i] = cpu_to_be32(k.sha224.key.istate.h[i]);
			ostate32[i] = cpu_to_be32(k.sha224.key.ostate.h[i]);
		}
		break;
	case SHA256_DIGEST_SIZE:
		hmac_sha256_preparekey(&k.sha256, raw_key, raw_key_len);
		for (int i = 0; i < ARRAY_SIZE(k.sha256.key.istate.h); i++) {
			istate32[i] = cpu_to_be32(k.sha256.key.istate.h[i]);
			ostate32[i] = cpu_to_be32(k.sha256.key.ostate.h[i]);
		}
		break;
	case SHA384_DIGEST_SIZE:
		hmac_sha384_preparekey(&k.sha384, raw_key, raw_key_len);
		for (int i = 0; i < ARRAY_SIZE(k.sha384.key.istate.h); i++) {
			istate64[i] = cpu_to_be64(k.sha384.key.istate.h[i]);
			ostate64[i] = cpu_to_be64(k.sha384.key.ostate.h[i]);
		}
		break;
	case SHA512_DIGEST_SIZE:
		hmac_sha512_preparekey(&k.sha512, raw_key, raw_key_len);
		for (int i = 0; i < ARRAY_SIZE(k.sha512.key.istate.h); i++) {
			istate64[i] = cpu_to_be64(k.sha512.key.istate.h[i]);
			ostate64[i] = cpu_to_be64(k.sha512.key.ostate.h[i]);
		}
		break;
	default:
		return -EINVAL;
	}
	memzero_explicit(&k, sizeof(k));
	return 0;
}

static inline int is_hmac(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct chcr_alg_template *chcr_crypto_alg =
		container_of(__crypto_ahash_alg(alg), struct chcr_alg_template,
			     alg.hash);
	if (chcr_crypto_alg->type == CRYPTO_ALG_TYPE_HMAC)
		return 1;
	return 0;
}

static inline void dsgl_walk_init(struct dsgl_walk *walk,
				   struct cpl_rx_phys_dsgl *dsgl)
{
	walk->dsgl = dsgl;
	walk->nents = 0;
	walk->to = (struct phys_sge_pairs *)(dsgl + 1);
}

static inline void dsgl_walk_end(struct dsgl_walk *walk, unsigned short qid,
				 int pci_chan_id)
{
	struct cpl_rx_phys_dsgl *phys_cpl;

	phys_cpl = walk->dsgl;

	phys_cpl->op_to_tid = htonl(CPL_RX_PHYS_DSGL_OPCODE_V(CPL_RX_PHYS_DSGL)
				    | CPL_RX_PHYS_DSGL_ISRDMA_V(0));
	phys_cpl->pcirlxorder_to_noofsgentr =
		htonl(CPL_RX_PHYS_DSGL_PCIRLXORDER_V(0) |
		      CPL_RX_PHYS_DSGL_PCINOSNOOP_V(0) |
		      CPL_RX_PHYS_DSGL_PCITPHNTENB_V(0) |
		      CPL_RX_PHYS_DSGL_PCITPHNT_V(0) |
		      CPL_RX_PHYS_DSGL_DCAID_V(0) |
		      CPL_RX_PHYS_DSGL_NOOFSGENTR_V(walk->nents));
	phys_cpl->rss_hdr_int.opcode = CPL_RX_PHYS_ADDR;
	phys_cpl->rss_hdr_int.qid = htons(qid);
	phys_cpl->rss_hdr_int.hash_val = 0;
	phys_cpl->rss_hdr_int.channel = pci_chan_id;
}

static inline void dsgl_walk_add_page(struct dsgl_walk *walk,
					size_t size,
					dma_addr_t addr)
{
	int j;

	if (!size)
		return;
	j = walk->nents;
	walk->to->len[j % 8] = htons(size);
	walk->to->addr[j % 8] = cpu_to_be64(addr);
	j++;
	if ((j % 8) == 0)
		walk->to++;
	walk->nents = j;
}

static void  dsgl_walk_add_sg(struct dsgl_walk *walk,
			   struct scatterlist *sg,
			      unsigned int slen,
			      unsigned int skip)
{
	int skip_len = 0;
	unsigned int left_size = slen, len = 0;
	unsigned int j = walk->nents;
	int offset, ent_len;

	if (!slen)
		return;
	while (sg && skip) {
		if (sg_dma_len(sg) <= skip) {
			skip -= sg_dma_len(sg);
			skip_len = 0;
			sg = sg_next(sg);
		} else {
			skip_len = skip;
			skip = 0;
		}
	}

	while (left_size && sg) {
		len = min_t(u32, left_size, sg_dma_len(sg) - skip_len);
		offset = 0;
		while (len) {
			ent_len =  min_t(u32, len, CHCR_DST_SG_SIZE);
			walk->to->len[j % 8] = htons(ent_len);
			walk->to->addr[j % 8] = cpu_to_be64(sg_dma_address(sg) +
						      offset + skip_len);
			offset += ent_len;
			len -= ent_len;
			j++;
			if ((j % 8) == 0)
				walk->to++;
		}
		walk->last_sg = sg;
		walk->last_sg_len = min_t(u32, left_size, sg_dma_len(sg) -
					  skip_len) + skip_len;
		left_size -= min_t(u32, left_size, sg_dma_len(sg) - skip_len);
		skip_len = 0;
		sg = sg_next(sg);
	}
	walk->nents = j;
}

static inline void ulptx_walk_init(struct ulptx_walk *walk,
				   struct ulptx_sgl *ulp)
{
	walk->sgl = ulp;
	walk->nents = 0;
	walk->pair_idx = 0;
	walk->pair = ulp->sge;
	walk->last_sg = NULL;
	walk->last_sg_len = 0;
}

static inline void ulptx_walk_end(struct ulptx_walk *walk)
{
	walk->sgl->cmd_nsge = htonl(ULPTX_CMD_V(ULP_TX_SC_DSGL) |
			      ULPTX_NSGE_V(walk->nents));
}


static inline void ulptx_walk_add_page(struct ulptx_walk *walk,
					size_t size,
					dma_addr_t addr)
{
	if (!size)
		return;

	if (walk->nents == 0) {
		walk->sgl->len0 = cpu_to_be32(size);
		walk->sgl->addr0 = cpu_to_be64(addr);
	} else {
		walk->pair->addr[walk->pair_idx] = cpu_to_be64(addr);
		walk->pair->len[walk->pair_idx] = cpu_to_be32(size);
		walk->pair_idx = !walk->pair_idx;
		if (!walk->pair_idx)
			walk->pair++;
	}
	walk->nents++;
}

static void  ulptx_walk_add_sg(struct ulptx_walk *walk,
					struct scatterlist *sg,
			       unsigned int len,
			       unsigned int skip)
{
	int small;
	int skip_len = 0;
	unsigned int sgmin;

	if (!len)
		return;
	while (sg && skip) {
		if (sg_dma_len(sg) <= skip) {
			skip -= sg_dma_len(sg);
			skip_len = 0;
			sg = sg_next(sg);
		} else {
			skip_len = skip;
			skip = 0;
		}
	}
	WARN(!sg, "SG should not be null here\n");
	if (sg && (walk->nents == 0)) {
		small = min_t(unsigned int, sg_dma_len(sg) - skip_len, len);
		sgmin = min_t(unsigned int, small, CHCR_SRC_SG_SIZE);
		walk->sgl->len0 = cpu_to_be32(sgmin);
		walk->sgl->addr0 = cpu_to_be64(sg_dma_address(sg) + skip_len);
		walk->nents++;
		len -= sgmin;
		walk->last_sg = sg;
		walk->last_sg_len = sgmin + skip_len;
		skip_len += sgmin;
		if (sg_dma_len(sg) == skip_len) {
			sg = sg_next(sg);
			skip_len = 0;
		}
	}

	while (sg && len) {
		small = min(sg_dma_len(sg) - skip_len, len);
		sgmin = min_t(unsigned int, small, CHCR_SRC_SG_SIZE);
		walk->pair->len[walk->pair_idx] = cpu_to_be32(sgmin);
		walk->pair->addr[walk->pair_idx] =
			cpu_to_be64(sg_dma_address(sg) + skip_len);
		walk->pair_idx = !walk->pair_idx;
		walk->nents++;
		if (!walk->pair_idx)
			walk->pair++;
		len -= sgmin;
		skip_len += sgmin;
		walk->last_sg = sg;
		walk->last_sg_len = skip_len;
		if (sg_dma_len(sg) == skip_len) {
			sg = sg_next(sg);
			skip_len = 0;
		}
	}
}

static inline int get_cryptoalg_subtype(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct chcr_alg_template *chcr_crypto_alg =
		container_of(alg, struct chcr_alg_template, alg.skcipher);

	return chcr_crypto_alg->type & CRYPTO_ALG_SUB_TYPE_MASK;
}

static int cxgb4_is_crypto_q_full(struct net_device *dev, unsigned int idx)
{
	struct adapter *adap = netdev2adap(dev);
	struct sge_uld_txq_info *txq_info =
		adap->sge.uld_txq_info[CXGB4_TX_CRYPTO];
	struct sge_uld_txq *txq;
	int ret = 0;

	local_bh_disable();
	txq = &txq_info->uldtxq[idx];
	spin_lock(&txq->sendq.lock);
	if (txq->full)
		ret = -1;
	spin_unlock(&txq->sendq.lock);
	local_bh_enable();
	return ret;
}

static int generate_copy_rrkey(struct ablk_ctx *ablkctx,
			       struct _key_ctx *key_ctx)
{
	if (ablkctx->ciph_mode == CHCR_SCMD_CIPHER_MODE_AES_CBC) {
		memcpy(key_ctx->key, ablkctx->rrkey, ablkctx->enckey_len);
	} else {
		memcpy(key_ctx->key,
		       ablkctx->key + (ablkctx->enckey_len >> 1),
		       ablkctx->enckey_len >> 1);
		memcpy(key_ctx->key + (ablkctx->enckey_len >> 1),
		       ablkctx->rrkey, ablkctx->enckey_len >> 1);
	}
	return 0;
}

static int chcr_hash_ent_in_wr(struct scatterlist *src,
			     unsigned int minsg,
			     unsigned int space,
			     unsigned int srcskip)
{
	int srclen = 0;
	int srcsg = minsg;
	int soffset = 0, sless;

	if (sg_dma_len(src) == srcskip) {
		src = sg_next(src);
		srcskip = 0;
	}
	while (src && space > (sgl_ent_len[srcsg + 1])) {
		sless = min_t(unsigned int, sg_dma_len(src) - soffset -	srcskip,
							CHCR_SRC_SG_SIZE);
		srclen += sless;
		soffset += sless;
		srcsg++;
		if (sg_dma_len(src) == (soffset + srcskip)) {
			src = sg_next(src);
			soffset = 0;
			srcskip = 0;
		}
	}
	return srclen;
}

static int chcr_sg_ent_in_wr(struct scatterlist *src,
			     struct scatterlist *dst,
			     unsigned int minsg,
			     unsigned int space,
			     unsigned int srcskip,
			     unsigned int dstskip)
{
	int srclen = 0, dstlen = 0;
	int srcsg = minsg, dstsg = minsg;
	int offset = 0, soffset = 0, less, sless = 0;

	if (sg_dma_len(src) == srcskip) {
		src = sg_next(src);
		srcskip = 0;
	}
	if (sg_dma_len(dst) == dstskip) {
		dst = sg_next(dst);
		dstskip = 0;
	}

	while (src && dst &&
	       space > (sgl_ent_len[srcsg + 1] + dsgl_ent_len[dstsg])) {
		sless = min_t(unsigned int, sg_dma_len(src) - srcskip - soffset,
				CHCR_SRC_SG_SIZE);
		srclen += sless;
		srcsg++;
		offset = 0;
		while (dst && ((dstsg + 1) <= MAX_DSGL_ENT) &&
		       space > (sgl_ent_len[srcsg] + dsgl_ent_len[dstsg + 1])) {
			if (srclen <= dstlen)
				break;
			less = min_t(unsigned int, sg_dma_len(dst) - offset -
				     dstskip, CHCR_DST_SG_SIZE);
			dstlen += less;
			offset += less;
			if ((offset + dstskip) == sg_dma_len(dst)) {
				dst = sg_next(dst);
				offset = 0;
			}
			dstsg++;
			dstskip = 0;
		}
		soffset += sless;
		if ((soffset + srcskip) == sg_dma_len(src)) {
			src = sg_next(src);
			srcskip = 0;
			soffset = 0;
		}

	}
	return min(srclen, dstlen);
}

static int chcr_cipher_fallback(struct crypto_skcipher *cipher,
				struct skcipher_request *req,
				u8 *iv,
				unsigned short op_type)
{
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	int err;

	skcipher_request_set_tfm(&reqctx->fallback_req, cipher);
	skcipher_request_set_callback(&reqctx->fallback_req, req->base.flags,
				      req->base.complete, req->base.data);
	skcipher_request_set_crypt(&reqctx->fallback_req, req->src, req->dst,
				   req->cryptlen, iv);

	err = op_type ? crypto_skcipher_decrypt(&reqctx->fallback_req) :
			crypto_skcipher_encrypt(&reqctx->fallback_req);

	return err;

}

static inline int get_qidxs(struct crypto_async_request *req,
			    unsigned int *txqidx, unsigned int *rxqidx)
{
	struct crypto_tfm *tfm = req->tfm;
	int ret = 0;

	switch (tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AEAD:
	{
		struct aead_request *aead_req =
			container_of(req, struct aead_request, base);
		struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(aead_req);
		*txqidx = reqctx->txqidx;
		*rxqidx = reqctx->rxqidx;
		break;
	}
	case CRYPTO_ALG_TYPE_SKCIPHER:
	{
		struct skcipher_request *sk_req =
			container_of(req, struct skcipher_request, base);
		struct chcr_skcipher_req_ctx *reqctx =
			skcipher_request_ctx(sk_req);
		*txqidx = reqctx->txqidx;
		*rxqidx = reqctx->rxqidx;
		break;
	}
	case CRYPTO_ALG_TYPE_AHASH:
	{
		struct ahash_request *ahash_req =
			container_of(req, struct ahash_request, base);
		struct chcr_ahash_req_ctx *reqctx =
			ahash_request_ctx(ahash_req);
		*txqidx = reqctx->txqidx;
		*rxqidx = reqctx->rxqidx;
		break;
	}
	default:
		ret = -EINVAL;
		/* should never get here */
		BUG();
		break;
	}
	return ret;
}

static inline void create_wreq(struct chcr_context *ctx,
			       struct chcr_wr *chcr_req,
			       struct crypto_async_request *req,
			       unsigned int imm,
			       int hash_sz,
			       unsigned int len16,
			       unsigned int sc_len,
			       unsigned int lcb)
{
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	unsigned int tx_channel_id, rx_channel_id;
	unsigned int txqidx = 0, rxqidx = 0;
	unsigned int qid, fid, portno;

	get_qidxs(req, &txqidx, &rxqidx);
	qid = u_ctx->lldi.rxq_ids[rxqidx];
	fid = u_ctx->lldi.rxq_ids[0];
	portno = rxqidx / ctx->rxq_perchan;
	tx_channel_id = txqidx / ctx->txq_perchan;
	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[portno]);


	chcr_req->wreq.op_to_cctx_size = FILL_WR_OP_CCTX_SIZE;
	chcr_req->wreq.pld_size_hash_size =
		htonl(FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE_V(hash_sz));
	chcr_req->wreq.len16_pkd =
		htonl(FW_CRYPTO_LOOKASIDE_WR_LEN16_V(DIV_ROUND_UP(len16, 16)));
	chcr_req->wreq.cookie = cpu_to_be64((uintptr_t)req);
	chcr_req->wreq.rx_chid_to_rx_q_id = FILL_WR_RX_Q_ID(rx_channel_id, qid,
							    !!lcb, txqidx);

	chcr_req->ulptx.cmd_dest = FILL_ULPTX_CMD_DEST(tx_channel_id, fid);
	chcr_req->ulptx.len = htonl((DIV_ROUND_UP(len16, 16) -
				((sizeof(chcr_req->wreq)) >> 4)));
	chcr_req->sc_imm.cmd_more = FILL_CMD_MORE(!imm);
	chcr_req->sc_imm.len = cpu_to_be32(sizeof(struct cpl_tx_sec_pdu) +
					   sizeof(chcr_req->key_ctx) + sc_len);
}

/**
 *	create_cipher_wr - form the WR for cipher operations
 *	@wrparam: Container for create_cipher_wr()'s parameters
 */
static struct sk_buff *create_cipher_wr(struct cipher_wr_param *wrparam)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(wrparam->req);
	struct chcr_context *ctx = c_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct ulptx_sgl *ulptx;
	struct chcr_skcipher_req_ctx *reqctx =
		skcipher_request_ctx(wrparam->req);
	unsigned int temp = 0, transhdr_len, dst_size;
	int error;
	int nents;
	unsigned int kctx_len;
	gfp_t flags = wrparam->req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
			GFP_KERNEL : GFP_ATOMIC;
	struct adapter *adap = padap(ctx->dev);
	unsigned int rx_channel_id = reqctx->rxqidx / ctx->rxq_perchan;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);
	nents = sg_nents_xlen(reqctx->dstsg,  wrparam->bytes, CHCR_DST_SG_SIZE,
			      reqctx->dst_ofst);
	dst_size = get_space_for_phys_dsgl(nents);
	kctx_len = roundup(ablkctx->enckey_len, 16);
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	nents = sg_nents_xlen(reqctx->srcsg, wrparam->bytes,
				  CHCR_SRC_SG_SIZE, reqctx->src_ofst);
	temp = reqctx->imm ? roundup(wrparam->bytes, 16) :
				     (sgl_len(nents) * 8);
	transhdr_len += temp;
	transhdr_len = roundup(transhdr_len, 16);
	skb = alloc_skb(SGE_MAX_WR_LEN, flags);
	if (!skb) {
		error = -ENOMEM;
		goto err;
	}
	chcr_req = __skb_put_zero(skb, transhdr_len);
	chcr_req->sec_cpl.op_ivinsrtofst =
			FILL_SEC_CPL_OP_IVINSR(rx_channel_id, 2, 1);

	chcr_req->sec_cpl.pldlen = htonl(IV + wrparam->bytes);
	chcr_req->sec_cpl.aadstart_cipherstop_hi =
			FILL_SEC_CPL_CIPHERSTOP_HI(0, 0, IV + 1, 0);

	chcr_req->sec_cpl.cipherstop_lo_authinsert =
			FILL_SEC_CPL_AUTHINSERT(0, 0, 0, 0);
	chcr_req->sec_cpl.seqno_numivs = FILL_SEC_CPL_SCMD0_SEQNO(reqctx->op, 0,
							 ablkctx->ciph_mode,
							 0, 0, IV >> 1);
	chcr_req->sec_cpl.ivgen_hdrlen = FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 0,
							  0, 1, dst_size);

	chcr_req->key_ctx.ctx_hdr = ablkctx->key_ctx_hdr;
	if ((reqctx->op == CHCR_DECRYPT_OP) &&
	    (!(get_cryptoalg_subtype(tfm) ==
	       CRYPTO_ALG_SUB_TYPE_CTR)) &&
	    (!(get_cryptoalg_subtype(tfm) ==
	       CRYPTO_ALG_SUB_TYPE_CTR_RFC3686))) {
		generate_copy_rrkey(ablkctx, &chcr_req->key_ctx);
	} else {
		if ((ablkctx->ciph_mode == CHCR_SCMD_CIPHER_MODE_AES_CBC) ||
		    (ablkctx->ciph_mode == CHCR_SCMD_CIPHER_MODE_AES_CTR)) {
			memcpy(chcr_req->key_ctx.key, ablkctx->key,
			       ablkctx->enckey_len);
		} else {
			memcpy(chcr_req->key_ctx.key, ablkctx->key +
			       (ablkctx->enckey_len >> 1),
			       ablkctx->enckey_len >> 1);
			memcpy(chcr_req->key_ctx.key +
			       (ablkctx->enckey_len >> 1),
			       ablkctx->key,
			       ablkctx->enckey_len >> 1);
		}
	}
	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	ulptx = (struct ulptx_sgl *)((u8 *)(phys_cpl + 1) + dst_size);
	chcr_add_cipher_src_ent(wrparam->req, ulptx, wrparam);
	chcr_add_cipher_dst_ent(wrparam->req, phys_cpl, wrparam, wrparam->qid);

	atomic_inc(&adap->chcr_stats.cipher_rqst);
	temp = sizeof(struct cpl_rx_phys_dsgl) + dst_size + kctx_len + IV
		+ (reqctx->imm ? (wrparam->bytes) : 0);
	create_wreq(c_ctx(tfm), chcr_req, &(wrparam->req->base), reqctx->imm, 0,
		    transhdr_len, temp,
			ablkctx->ciph_mode == CHCR_SCMD_CIPHER_MODE_AES_CBC);
	reqctx->skb = skb;

	if (reqctx->op && (ablkctx->ciph_mode ==
			   CHCR_SCMD_CIPHER_MODE_AES_CBC))
		sg_pcopy_to_buffer(wrparam->req->src,
			sg_nents(wrparam->req->src), wrparam->req->iv, 16,
			reqctx->processed + wrparam->bytes - AES_BLOCK_SIZE);

	return skb;
err:
	return ERR_PTR(error);
}

static inline int chcr_keyctx_ck_size(unsigned int keylen)
{
	int ck_size = 0;

	if (keylen == AES_KEYSIZE_128)
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	else if (keylen == AES_KEYSIZE_192)
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	else if (keylen == AES_KEYSIZE_256)
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	else
		ck_size = 0;

	return ck_size;
}
static int chcr_cipher_fallback_setkey(struct crypto_skcipher *cipher,
				       const u8 *key,
				       unsigned int keylen)
{
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(cipher));

	crypto_skcipher_clear_flags(ablkctx->sw_cipher,
				CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ablkctx->sw_cipher,
				cipher->base.crt_flags & CRYPTO_TFM_REQ_MASK);
	return crypto_skcipher_setkey(ablkctx->sw_cipher, key, keylen);
}

static int chcr_aes_cbc_setkey(struct crypto_skcipher *cipher,
			       const u8 *key,
			       unsigned int keylen)
{
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(cipher));
	unsigned int ck_size, context_size;
	u16 alignment = 0;
	int err;

	err = chcr_cipher_fallback_setkey(cipher, key, keylen);
	if (err)
		goto badkey_err;

	ck_size = chcr_keyctx_ck_size(keylen);
	alignment = ck_size == CHCR_KEYCTX_CIPHER_KEY_SIZE_192 ? 8 : 0;
	memcpy(ablkctx->key, key, keylen);
	ablkctx->enckey_len = keylen;
	get_aes_decrypt_key(ablkctx->rrkey, ablkctx->key, keylen << 3);
	context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD +
			keylen + alignment) >> 4;

	ablkctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, CHCR_KEYCTX_NO_KEY,
						0, 0, context_size);
	ablkctx->ciph_mode = CHCR_SCMD_CIPHER_MODE_AES_CBC;
	return 0;
badkey_err:
	ablkctx->enckey_len = 0;

	return err;
}

static int chcr_aes_ctr_setkey(struct crypto_skcipher *cipher,
				   const u8 *key,
				   unsigned int keylen)
{
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(cipher));
	unsigned int ck_size, context_size;
	u16 alignment = 0;
	int err;

	err = chcr_cipher_fallback_setkey(cipher, key, keylen);
	if (err)
		goto badkey_err;
	ck_size = chcr_keyctx_ck_size(keylen);
	alignment = (ck_size == CHCR_KEYCTX_CIPHER_KEY_SIZE_192) ? 8 : 0;
	memcpy(ablkctx->key, key, keylen);
	ablkctx->enckey_len = keylen;
	context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD +
			keylen + alignment) >> 4;

	ablkctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, CHCR_KEYCTX_NO_KEY,
						0, 0, context_size);
	ablkctx->ciph_mode = CHCR_SCMD_CIPHER_MODE_AES_CTR;

	return 0;
badkey_err:
	ablkctx->enckey_len = 0;

	return err;
}

static int chcr_aes_rfc3686_setkey(struct crypto_skcipher *cipher,
				   const u8 *key,
				   unsigned int keylen)
{
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(cipher));
	unsigned int ck_size, context_size;
	u16 alignment = 0;
	int err;

	if (keylen < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;
	memcpy(ablkctx->nonce, key + (keylen - CTR_RFC3686_NONCE_SIZE),
	       CTR_RFC3686_NONCE_SIZE);

	keylen -= CTR_RFC3686_NONCE_SIZE;
	err = chcr_cipher_fallback_setkey(cipher, key, keylen);
	if (err)
		goto badkey_err;

	ck_size = chcr_keyctx_ck_size(keylen);
	alignment = (ck_size == CHCR_KEYCTX_CIPHER_KEY_SIZE_192) ? 8 : 0;
	memcpy(ablkctx->key, key, keylen);
	ablkctx->enckey_len = keylen;
	context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD +
			keylen + alignment) >> 4;

	ablkctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, CHCR_KEYCTX_NO_KEY,
						0, 0, context_size);
	ablkctx->ciph_mode = CHCR_SCMD_CIPHER_MODE_AES_CTR;

	return 0;
badkey_err:
	ablkctx->enckey_len = 0;

	return err;
}
static void ctr_add_iv(u8 *dstiv, u8 *srciv, u32 add)
{
	unsigned int size = AES_BLOCK_SIZE;
	__be32 *b = (__be32 *)(dstiv + size);
	u32 c, prev;

	memcpy(dstiv, srciv, AES_BLOCK_SIZE);
	for (; size >= 4; size -= 4) {
		prev = be32_to_cpu(*--b);
		c = prev + add;
		*b = cpu_to_be32(c);
		if (prev < c)
			break;
		add = 1;
	}

}

static unsigned int adjust_ctr_overflow(u8 *iv, u32 bytes)
{
	__be32 *b = (__be32 *)(iv + AES_BLOCK_SIZE);
	u64 c;
	u32 temp = be32_to_cpu(*--b);

	temp = ~temp;
	c = (u64)temp +  1; // No of block can processed without overflow
	if ((bytes / AES_BLOCK_SIZE) >= c)
		bytes = c * AES_BLOCK_SIZE;
	return bytes;
}

static int chcr_update_tweak(struct skcipher_request *req, u8 *iv,
			     u32 isfinal)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(tfm));
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	struct crypto_aes_ctx aes;
	int ret, i;
	u8 *key;
	unsigned int keylen;
	int round = reqctx->last_req_len / AES_BLOCK_SIZE;
	int round8 = round / 8;

	memcpy(iv, reqctx->iv, AES_BLOCK_SIZE);

	keylen = ablkctx->enckey_len / 2;
	key = ablkctx->key + keylen;
	/* For a 192 bit key remove the padded zeroes which was
	 * added in chcr_xts_setkey
	 */
	if (KEY_CONTEXT_CK_SIZE_G(ntohl(ablkctx->key_ctx_hdr))
			== CHCR_KEYCTX_CIPHER_KEY_SIZE_192)
		ret = aes_expandkey(&aes, key, keylen - 8);
	else
		ret = aes_expandkey(&aes, key, keylen);
	if (ret)
		return ret;
	aes_encrypt(&aes, iv, iv);
	for (i = 0; i < round8; i++)
		gf128mul_x8_ble((le128 *)iv, (le128 *)iv);

	for (i = 0; i < (round % 8); i++)
		gf128mul_x_ble((le128 *)iv, (le128 *)iv);

	if (!isfinal)
		aes_decrypt(&aes, iv, iv);

	memzero_explicit(&aes, sizeof(aes));
	return 0;
}

static int chcr_update_cipher_iv(struct skcipher_request *req,
				   struct cpl_fw6_pld *fw6_pld, u8 *iv)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	int subtype = get_cryptoalg_subtype(tfm);
	int ret = 0;

	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR)
		ctr_add_iv(iv, req->iv, (reqctx->processed /
			   AES_BLOCK_SIZE));
	else if (subtype == CRYPTO_ALG_SUB_TYPE_CTR_RFC3686)
		*(__be32 *)(reqctx->iv + CTR_RFC3686_NONCE_SIZE +
			CTR_RFC3686_IV_SIZE) = cpu_to_be32((reqctx->processed /
						AES_BLOCK_SIZE) + 1);
	else if (subtype == CRYPTO_ALG_SUB_TYPE_XTS)
		ret = chcr_update_tweak(req, iv, 0);
	else if (subtype == CRYPTO_ALG_SUB_TYPE_CBC) {
		if (reqctx->op)
			/*Updated before sending last WR*/
			memcpy(iv, req->iv, AES_BLOCK_SIZE);
		else
			memcpy(iv, &fw6_pld->data[2], AES_BLOCK_SIZE);
	}

	return ret;

}

/* We need separate function for final iv because in rfc3686  Initial counter
 * starts from 1 and buffer size of iv is 8 byte only which remains constant
 * for subsequent update requests
 */

static int chcr_final_cipher_iv(struct skcipher_request *req,
				   struct cpl_fw6_pld *fw6_pld, u8 *iv)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	int subtype = get_cryptoalg_subtype(tfm);
	int ret = 0;

	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR)
		ctr_add_iv(iv, req->iv, DIV_ROUND_UP(reqctx->processed,
						       AES_BLOCK_SIZE));
	else if (subtype == CRYPTO_ALG_SUB_TYPE_XTS) {
		if (!reqctx->partial_req)
			memcpy(iv, reqctx->iv, AES_BLOCK_SIZE);
		else
			ret = chcr_update_tweak(req, iv, 1);
	}
	else if (subtype == CRYPTO_ALG_SUB_TYPE_CBC) {
		/*Already updated for Decrypt*/
		if (!reqctx->op)
			memcpy(iv, &fw6_pld->data[2], AES_BLOCK_SIZE);

	}
	return ret;

}

static int chcr_handle_cipher_resp(struct skcipher_request *req,
				   unsigned char *input, int err)
{
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cpl_fw6_pld *fw6_pld = (struct cpl_fw6_pld *)input;
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(tfm));
	struct uld_ctx *u_ctx = ULD_CTX(c_ctx(tfm));
	struct chcr_dev *dev = c_ctx(tfm)->dev;
	struct chcr_context *ctx = c_ctx(tfm);
	struct adapter *adap = padap(ctx->dev);
	struct cipher_wr_param wrparam;
	struct sk_buff *skb;
	int bytes;

	if (err)
		goto unmap;
	if (req->cryptlen == reqctx->processed) {
		chcr_cipher_dma_unmap(&ULD_CTX(c_ctx(tfm))->lldi.pdev->dev,
				      req);
		err = chcr_final_cipher_iv(req, fw6_pld, req->iv);
		goto complete;
	}

	if (!reqctx->imm) {
		bytes = chcr_sg_ent_in_wr(reqctx->srcsg, reqctx->dstsg, 0,
					  CIP_SPACE_LEFT(ablkctx->enckey_len),
					  reqctx->src_ofst, reqctx->dst_ofst);
		if ((bytes + reqctx->processed) >= req->cryptlen)
			bytes  = req->cryptlen - reqctx->processed;
		else
			bytes = rounddown(bytes, 16);
	} else {
		/*CTR mode counter overflow*/
		bytes  = req->cryptlen - reqctx->processed;
	}
	err = chcr_update_cipher_iv(req, fw6_pld, reqctx->iv);
	if (err)
		goto unmap;

	if (unlikely(bytes == 0)) {
		chcr_cipher_dma_unmap(&ULD_CTX(c_ctx(tfm))->lldi.pdev->dev,
				      req);
		memcpy(req->iv, reqctx->init_iv, IV);
		atomic_inc(&adap->chcr_stats.fallback);
		err = chcr_cipher_fallback(ablkctx->sw_cipher, req, req->iv,
					   reqctx->op);
		goto complete;
	}

	if (get_cryptoalg_subtype(tfm) ==
	    CRYPTO_ALG_SUB_TYPE_CTR)
		bytes = adjust_ctr_overflow(reqctx->iv, bytes);
	wrparam.qid = u_ctx->lldi.rxq_ids[reqctx->rxqidx];
	wrparam.req = req;
	wrparam.bytes = bytes;
	skb = create_cipher_wr(&wrparam);
	if (IS_ERR(skb)) {
		pr_err("%s : Failed to form WR. No memory\n", __func__);
		err = PTR_ERR(skb);
		goto unmap;
	}
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, reqctx->txqidx);
	chcr_send_wr(skb);
	reqctx->last_req_len = bytes;
	reqctx->processed += bytes;
	if (get_cryptoalg_subtype(tfm) ==
		CRYPTO_ALG_SUB_TYPE_CBC && req->base.flags ==
			CRYPTO_TFM_REQ_MAY_SLEEP ) {
		complete(&ctx->cbc_aes_aio_done);
	}
	return 0;
unmap:
	chcr_cipher_dma_unmap(&ULD_CTX(c_ctx(tfm))->lldi.pdev->dev, req);
complete:
	if (get_cryptoalg_subtype(tfm) ==
		CRYPTO_ALG_SUB_TYPE_CBC && req->base.flags ==
			CRYPTO_TFM_REQ_MAY_SLEEP ) {
		complete(&ctx->cbc_aes_aio_done);
	}
	chcr_dec_wrcount(dev);
	skcipher_request_complete(req, err);
	return err;
}

static int process_cipher(struct skcipher_request *req,
				  unsigned short qid,
				  struct sk_buff **skb,
				  unsigned short op_type)
{
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	unsigned int ivsize = crypto_skcipher_ivsize(tfm);
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(tfm));
	struct adapter *adap = padap(c_ctx(tfm)->dev);
	struct	cipher_wr_param wrparam;
	int bytes, err = -EINVAL;
	int subtype;

	reqctx->processed = 0;
	reqctx->partial_req = 0;
	if (!req->iv)
		goto error;
	subtype = get_cryptoalg_subtype(tfm);
	if ((ablkctx->enckey_len == 0) || (ivsize > AES_BLOCK_SIZE) ||
	    (req->cryptlen == 0) ||
	    (req->cryptlen % crypto_skcipher_blocksize(tfm))) {
		if (req->cryptlen == 0 && subtype != CRYPTO_ALG_SUB_TYPE_XTS)
			goto fallback;
		else if (req->cryptlen % crypto_skcipher_blocksize(tfm) &&
			 subtype == CRYPTO_ALG_SUB_TYPE_XTS)
			goto fallback;
		pr_err("AES: Invalid value of Key Len %d nbytes %d IV Len %d\n",
		       ablkctx->enckey_len, req->cryptlen, ivsize);
		goto error;
	}

	err = chcr_cipher_dma_map(&ULD_CTX(c_ctx(tfm))->lldi.pdev->dev, req);
	if (err)
		goto error;
	if (req->cryptlen < (SGE_MAX_WR_LEN - (sizeof(struct chcr_wr) +
					    AES_MIN_KEY_SIZE +
					    sizeof(struct cpl_rx_phys_dsgl) +
					/*Min dsgl size*/
					    32))) {
		/* Can be sent as Imm*/
		unsigned int dnents = 0, transhdr_len, phys_dsgl, kctx_len;

		dnents = sg_nents_xlen(req->dst, req->cryptlen,
				       CHCR_DST_SG_SIZE, 0);
		phys_dsgl = get_space_for_phys_dsgl(dnents);
		kctx_len = roundup(ablkctx->enckey_len, 16);
		transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, phys_dsgl);
		reqctx->imm = (transhdr_len + IV + req->cryptlen) <=
			SGE_MAX_WR_LEN;
		bytes = IV + req->cryptlen;

	} else {
		reqctx->imm = 0;
	}

	if (!reqctx->imm) {
		bytes = chcr_sg_ent_in_wr(req->src, req->dst, 0,
					  CIP_SPACE_LEFT(ablkctx->enckey_len),
					  0, 0);
		if ((bytes + reqctx->processed) >= req->cryptlen)
			bytes  = req->cryptlen - reqctx->processed;
		else
			bytes = rounddown(bytes, 16);
	} else {
		bytes = req->cryptlen;
	}
	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR) {
		bytes = adjust_ctr_overflow(req->iv, bytes);
	}
	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR_RFC3686) {
		memcpy(reqctx->iv, ablkctx->nonce, CTR_RFC3686_NONCE_SIZE);
		memcpy(reqctx->iv + CTR_RFC3686_NONCE_SIZE, req->iv,
				CTR_RFC3686_IV_SIZE);

		/* initialize counter portion of counter block */
		*(__be32 *)(reqctx->iv + CTR_RFC3686_NONCE_SIZE +
			CTR_RFC3686_IV_SIZE) = cpu_to_be32(1);
		memcpy(reqctx->init_iv, reqctx->iv, IV);

	} else {

		memcpy(reqctx->iv, req->iv, IV);
		memcpy(reqctx->init_iv, req->iv, IV);
	}
	if (unlikely(bytes == 0)) {
		chcr_cipher_dma_unmap(&ULD_CTX(c_ctx(tfm))->lldi.pdev->dev,
				      req);
fallback:       atomic_inc(&adap->chcr_stats.fallback);
		err = chcr_cipher_fallback(ablkctx->sw_cipher, req,
					   subtype ==
					   CRYPTO_ALG_SUB_TYPE_CTR_RFC3686 ?
					   reqctx->iv : req->iv,
					   op_type);
		goto error;
	}
	reqctx->op = op_type;
	reqctx->srcsg = req->src;
	reqctx->dstsg = req->dst;
	reqctx->src_ofst = 0;
	reqctx->dst_ofst = 0;
	wrparam.qid = qid;
	wrparam.req = req;
	wrparam.bytes = bytes;
	*skb = create_cipher_wr(&wrparam);
	if (IS_ERR(*skb)) {
		err = PTR_ERR(*skb);
		goto unmap;
	}
	reqctx->processed = bytes;
	reqctx->last_req_len = bytes;
	reqctx->partial_req = !!(req->cryptlen - reqctx->processed);

	return 0;
unmap:
	chcr_cipher_dma_unmap(&ULD_CTX(c_ctx(tfm))->lldi.pdev->dev, req);
error:
	return err;
}

static int chcr_aes_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	struct chcr_dev *dev = c_ctx(tfm)->dev;
	struct sk_buff *skb = NULL;
	int err;
	struct uld_ctx *u_ctx = ULD_CTX(c_ctx(tfm));
	struct chcr_context *ctx = c_ctx(tfm);
	unsigned int cpu;

	cpu = get_cpu();
	reqctx->txqidx = cpu % ctx->ntxq;
	reqctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	err = chcr_inc_wrcount(dev);
	if (err)
		return -ENXIO;
	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
						reqctx->txqidx) &&
		(!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)))) {
			err = -ENOSPC;
			goto error;
	}

	err = process_cipher(req, u_ctx->lldi.rxq_ids[reqctx->rxqidx],
			     &skb, CHCR_ENCRYPT_OP);
	if (err || !skb)
		return  err;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, reqctx->txqidx);
	chcr_send_wr(skb);
	if (get_cryptoalg_subtype(tfm) ==
		CRYPTO_ALG_SUB_TYPE_CBC && req->base.flags ==
			CRYPTO_TFM_REQ_MAY_SLEEP ) {
			reqctx->partial_req = 1;
			wait_for_completion(&ctx->cbc_aes_aio_done);
        }
	return -EINPROGRESS;
error:
	chcr_dec_wrcount(dev);
	return err;
}

static int chcr_aes_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	struct uld_ctx *u_ctx = ULD_CTX(c_ctx(tfm));
	struct chcr_dev *dev = c_ctx(tfm)->dev;
	struct sk_buff *skb = NULL;
	int err;
	struct chcr_context *ctx = c_ctx(tfm);
	unsigned int cpu;

	cpu = get_cpu();
	reqctx->txqidx = cpu % ctx->ntxq;
	reqctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	err = chcr_inc_wrcount(dev);
	if (err)
		return -ENXIO;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
						reqctx->txqidx) &&
		(!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))))
			return -ENOSPC;
	err = process_cipher(req, u_ctx->lldi.rxq_ids[reqctx->rxqidx],
			     &skb, CHCR_DECRYPT_OP);
	if (err || !skb)
		return err;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, reqctx->txqidx);
	chcr_send_wr(skb);
	return -EINPROGRESS;
}
static int chcr_device_init(struct chcr_context *ctx)
{
	struct uld_ctx *u_ctx = NULL;
	int txq_perchan, ntxq;
	int err = 0, rxq_perchan;

	if (!ctx->dev) {
		u_ctx = assign_chcr_device();
		if (!u_ctx) {
			err = -ENXIO;
			pr_err("chcr device assignment fails\n");
			goto out;
		}
		ctx->dev = &u_ctx->dev;
		ntxq = u_ctx->lldi.ntxq;
		rxq_perchan = u_ctx->lldi.nrxq / u_ctx->lldi.nchan;
		txq_perchan = ntxq / u_ctx->lldi.nchan;
		ctx->ntxq = ntxq;
		ctx->nrxq = u_ctx->lldi.nrxq;
		ctx->rxq_perchan = rxq_perchan;
		ctx->txq_perchan = txq_perchan;
	}
out:
	return err;
}

static int chcr_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct chcr_context *ctx = crypto_skcipher_ctx(tfm);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);

	ablkctx->sw_cipher = crypto_alloc_skcipher(alg->base.cra_name, 0,
				CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ablkctx->sw_cipher)) {
		pr_err("failed to allocate fallback for %s\n", alg->base.cra_name);
		return PTR_ERR(ablkctx->sw_cipher);
	}
	init_completion(&ctx->cbc_aes_aio_done);
	crypto_skcipher_set_reqsize(tfm, sizeof(struct chcr_skcipher_req_ctx) +
					 crypto_skcipher_reqsize(ablkctx->sw_cipher));

	return chcr_device_init(ctx);
}

static int chcr_rfc3686_init(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct chcr_context *ctx = crypto_skcipher_ctx(tfm);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);

	/*RFC3686 initialises IV counter value to 1, rfc3686(ctr(aes))
	 * cannot be used as fallback in chcr_handle_cipher_response
	 */
	ablkctx->sw_cipher = crypto_alloc_skcipher("ctr(aes)", 0,
				CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ablkctx->sw_cipher)) {
		pr_err("failed to allocate fallback for %s\n", alg->base.cra_name);
		return PTR_ERR(ablkctx->sw_cipher);
	}
	crypto_skcipher_set_reqsize(tfm, sizeof(struct chcr_skcipher_req_ctx) +
				    crypto_skcipher_reqsize(ablkctx->sw_cipher));
	return chcr_device_init(ctx);
}


static void chcr_exit_tfm(struct crypto_skcipher *tfm)
{
	struct chcr_context *ctx = crypto_skcipher_ctx(tfm);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);

	crypto_free_skcipher(ablkctx->sw_cipher);
}

static int get_alg_config(struct algo_param *params,
			  unsigned int auth_size)
{
	switch (auth_size) {
	case SHA1_DIGEST_SIZE:
		params->mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_160;
		params->auth_mode = CHCR_SCMD_AUTH_MODE_SHA1;
		params->result_size = SHA1_DIGEST_SIZE;
		break;
	case SHA224_DIGEST_SIZE:
		params->mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_256;
		params->auth_mode = CHCR_SCMD_AUTH_MODE_SHA224;
		params->result_size = SHA256_DIGEST_SIZE;
		break;
	case SHA256_DIGEST_SIZE:
		params->mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_256;
		params->auth_mode = CHCR_SCMD_AUTH_MODE_SHA256;
		params->result_size = SHA256_DIGEST_SIZE;
		break;
	case SHA384_DIGEST_SIZE:
		params->mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_512;
		params->auth_mode = CHCR_SCMD_AUTH_MODE_SHA512_384;
		params->result_size = SHA512_DIGEST_SIZE;
		break;
	case SHA512_DIGEST_SIZE:
		params->mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_512;
		params->auth_mode = CHCR_SCMD_AUTH_MODE_SHA512_512;
		params->result_size = SHA512_DIGEST_SIZE;
		break;
	default:
		pr_err("ERROR, unsupported digest size\n");
		return -EINVAL;
	}
	return 0;
}

/**
 *	create_hash_wr - Create hash work request
 *	@req: Cipher req base
 *	@param: Container for create_hash_wr()'s parameters
 */
static struct sk_buff *create_hash_wr(struct ahash_request *req,
				      struct hash_wr_param *param)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = h_ctx(tfm);
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);
	struct sk_buff *skb = NULL;
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_wr *chcr_req;
	struct ulptx_sgl *ulptx;
	unsigned int nents = 0, transhdr_len;
	unsigned int temp = 0;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;
	struct adapter *adap = padap(h_ctx(tfm)->dev);
	int error = 0;
	unsigned int rx_channel_id = req_ctx->rxqidx / ctx->rxq_perchan;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);
	transhdr_len = HASH_TRANSHDR_SIZE(param->kctx_len);
	req_ctx->hctx_wr.imm = (transhdr_len + param->bfr_len +
				param->sg_len) <= SGE_MAX_WR_LEN;
	nents = sg_nents_xlen(req_ctx->hctx_wr.srcsg, param->sg_len,
		      CHCR_SRC_SG_SIZE, req_ctx->hctx_wr.src_ofst);
	nents += param->bfr_len ? 1 : 0;
	transhdr_len += req_ctx->hctx_wr.imm ? roundup(param->bfr_len +
				param->sg_len, 16) : (sgl_len(nents) * 8);
	transhdr_len = roundup(transhdr_len, 16);

	skb = alloc_skb(transhdr_len, flags);
	if (!skb)
		return ERR_PTR(-ENOMEM);
	chcr_req = __skb_put_zero(skb, transhdr_len);

	chcr_req->sec_cpl.op_ivinsrtofst =
		FILL_SEC_CPL_OP_IVINSR(rx_channel_id, 2, 0);

	chcr_req->sec_cpl.pldlen = htonl(param->bfr_len + param->sg_len);

	chcr_req->sec_cpl.aadstart_cipherstop_hi =
		FILL_SEC_CPL_CIPHERSTOP_HI(0, 0, 0, 0);
	chcr_req->sec_cpl.cipherstop_lo_authinsert =
		FILL_SEC_CPL_AUTHINSERT(0, 1, 0, 0);
	chcr_req->sec_cpl.seqno_numivs =
		FILL_SEC_CPL_SCMD0_SEQNO(0, 0, 0, param->alg_prm.auth_mode,
					 param->opad_needed, 0);

	chcr_req->sec_cpl.ivgen_hdrlen =
		FILL_SEC_CPL_IVGEN_HDRLEN(param->last, param->more, 0, 1, 0, 0);

	memcpy(chcr_req->key_ctx.key, req_ctx->partial_hash,
	       param->alg_prm.result_size);

	if (param->opad_needed)
		memcpy(chcr_req->key_ctx.key +
		       ((param->alg_prm.result_size <= 32) ? 32 :
			CHCR_HASH_MAX_DIGEST_SIZE),
		       hmacctx->opad, param->alg_prm.result_size);

	chcr_req->key_ctx.ctx_hdr = FILL_KEY_CTX_HDR(CHCR_KEYCTX_NO_KEY,
					    param->alg_prm.mk_size, 0,
					    param->opad_needed,
					    ((param->kctx_len +
					     sizeof(chcr_req->key_ctx)) >> 4));
	chcr_req->sec_cpl.scmd1 = cpu_to_be64((u64)param->scmd1);
	ulptx = (struct ulptx_sgl *)((u8 *)(chcr_req + 1) + param->kctx_len +
				     DUMMY_BYTES);
	if (param->bfr_len != 0) {
		req_ctx->hctx_wr.dma_addr =
			dma_map_single(&u_ctx->lldi.pdev->dev, req_ctx->reqbfr,
				       param->bfr_len, DMA_TO_DEVICE);
		if (dma_mapping_error(&u_ctx->lldi.pdev->dev,
				       req_ctx->hctx_wr. dma_addr)) {
			error = -ENOMEM;
			goto err;
		}
		req_ctx->hctx_wr.dma_len = param->bfr_len;
	} else {
		req_ctx->hctx_wr.dma_addr = 0;
	}
	chcr_add_hash_src_ent(req, ulptx, param);
	/* Request upto max wr size */
	temp = param->kctx_len + DUMMY_BYTES + (req_ctx->hctx_wr.imm ?
				(param->sg_len + param->bfr_len) : 0);
	atomic_inc(&adap->chcr_stats.digest_rqst);
	create_wreq(h_ctx(tfm), chcr_req, &req->base, req_ctx->hctx_wr.imm,
		    param->hash_size, transhdr_len,
		    temp,  0);
	req_ctx->hctx_wr.skb = skb;
	return skb;
err:
	kfree_skb(skb);
	return  ERR_PTR(error);
}

static int chcr_ahash_update(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct uld_ctx *u_ctx = ULD_CTX(h_ctx(rtfm));
	struct chcr_context *ctx = h_ctx(rtfm);
	struct chcr_dev *dev = h_ctx(rtfm)->dev;
	struct sk_buff *skb;
	u8 remainder = 0, bs;
	unsigned int nbytes = req->nbytes;
	struct hash_wr_param params;
	int error;
	unsigned int cpu;

	cpu = get_cpu();
	req_ctx->txqidx = cpu % ctx->ntxq;
	req_ctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));

	if (nbytes + req_ctx->reqlen >= bs) {
		remainder = (nbytes + req_ctx->reqlen) % bs;
		nbytes = nbytes + req_ctx->reqlen - remainder;
	} else {
		sg_pcopy_to_buffer(req->src, sg_nents(req->src), req_ctx->reqbfr
				   + req_ctx->reqlen, nbytes, 0);
		req_ctx->reqlen += nbytes;
		return 0;
	}
	error = chcr_inc_wrcount(dev);
	if (error)
		return -ENXIO;
	/* Detach state for CHCR means lldi or padap is freed. Increasing
	 * inflight count for dev guarantees that lldi and padap is valid
	 */
	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
						req_ctx->txqidx) &&
		(!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)))) {
			error = -ENOSPC;
			goto err;
	}

	chcr_init_hctx_per_wr(req_ctx);
	error = chcr_hash_dma_map(&u_ctx->lldi.pdev->dev, req);
	if (error) {
		error = -ENOMEM;
		goto err;
	}
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	params.kctx_len = roundup(params.alg_prm.result_size, 16);
	params.sg_len = chcr_hash_ent_in_wr(req->src, !!req_ctx->reqlen,
				     HASH_SPACE_LEFT(params.kctx_len), 0);
	if (params.sg_len > req->nbytes)
		params.sg_len = req->nbytes;
	params.sg_len = rounddown(params.sg_len + req_ctx->reqlen, bs) -
			req_ctx->reqlen;
	params.opad_needed = 0;
	params.more = 1;
	params.last = 0;
	params.bfr_len = req_ctx->reqlen;
	params.scmd1 = 0;
	req_ctx->hctx_wr.srcsg = req->src;

	params.hash_size = params.alg_prm.result_size;
	req_ctx->data_len += params.sg_len + params.bfr_len;
	skb = create_hash_wr(req, &params);
	if (IS_ERR(skb)) {
		error = PTR_ERR(skb);
		goto unmap;
	}

	req_ctx->hctx_wr.processed += params.sg_len;
	if (remainder) {
		/* Swap buffers */
		swap(req_ctx->reqbfr, req_ctx->skbfr);
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   req_ctx->reqbfr, remainder, req->nbytes -
				   remainder);
	}
	req_ctx->reqlen = remainder;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, req_ctx->txqidx);
	chcr_send_wr(skb);
	return -EINPROGRESS;
unmap:
	chcr_hash_dma_unmap(&u_ctx->lldi.pdev->dev, req);
err:
	chcr_dec_wrcount(dev);
	return error;
}

static void create_last_hash_block(char *bfr_ptr, unsigned int bs, u64 scmd1)
{
	memset(bfr_ptr, 0, bs);
	*bfr_ptr = 0x80;
	if (bs == 64)
		*(__be64 *)(bfr_ptr + 56) = cpu_to_be64(scmd1  << 3);
	else
		*(__be64 *)(bfr_ptr + 120) =  cpu_to_be64(scmd1  << 3);
}

static int chcr_ahash_final(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_dev *dev = h_ctx(rtfm)->dev;
	struct hash_wr_param params;
	struct sk_buff *skb;
	struct uld_ctx *u_ctx = ULD_CTX(h_ctx(rtfm));
	struct chcr_context *ctx = h_ctx(rtfm);
	u8 bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));
	int error;
	unsigned int cpu;

	cpu = get_cpu();
	req_ctx->txqidx = cpu % ctx->ntxq;
	req_ctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	error = chcr_inc_wrcount(dev);
	if (error)
		return -ENXIO;

	chcr_init_hctx_per_wr(req_ctx);
	if (is_hmac(crypto_ahash_tfm(rtfm)))
		params.opad_needed = 1;
	else
		params.opad_needed = 0;
	params.sg_len = 0;
	req_ctx->hctx_wr.isfinal = 1;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	params.kctx_len = roundup(params.alg_prm.result_size, 16);
	if (is_hmac(crypto_ahash_tfm(rtfm))) {
		params.opad_needed = 1;
		params.kctx_len *= 2;
	} else {
		params.opad_needed = 0;
	}

	req_ctx->hctx_wr.result = 1;
	params.bfr_len = req_ctx->reqlen;
	req_ctx->data_len += params.bfr_len + params.sg_len;
	req_ctx->hctx_wr.srcsg = req->src;
	if (req_ctx->reqlen == 0) {
		create_last_hash_block(req_ctx->reqbfr, bs, req_ctx->data_len);
		params.last = 0;
		params.more = 1;
		params.scmd1 = 0;
		params.bfr_len = bs;

	} else {
		params.scmd1 = req_ctx->data_len;
		params.last = 1;
		params.more = 0;
	}
	params.hash_size = crypto_ahash_digestsize(rtfm);
	skb = create_hash_wr(req, &params);
	if (IS_ERR(skb)) {
		error = PTR_ERR(skb);
		goto err;
	}
	req_ctx->reqlen = 0;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, req_ctx->txqidx);
	chcr_send_wr(skb);
	return -EINPROGRESS;
err:
	chcr_dec_wrcount(dev);
	return error;
}

static int chcr_ahash_finup(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_dev *dev = h_ctx(rtfm)->dev;
	struct uld_ctx *u_ctx = ULD_CTX(h_ctx(rtfm));
	struct chcr_context *ctx = h_ctx(rtfm);
	struct sk_buff *skb;
	struct hash_wr_param params;
	u8  bs;
	int error;
	unsigned int cpu;

	cpu = get_cpu();
	req_ctx->txqidx = cpu % ctx->ntxq;
	req_ctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));
	error = chcr_inc_wrcount(dev);
	if (error)
		return -ENXIO;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
						req_ctx->txqidx) &&
		(!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)))) {
			error = -ENOSPC;
			goto err;
	}
	chcr_init_hctx_per_wr(req_ctx);
	error = chcr_hash_dma_map(&u_ctx->lldi.pdev->dev, req);
	if (error) {
		error = -ENOMEM;
		goto err;
	}

	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	params.kctx_len = roundup(params.alg_prm.result_size, 16);
	if (is_hmac(crypto_ahash_tfm(rtfm))) {
		params.kctx_len *= 2;
		params.opad_needed = 1;
	} else {
		params.opad_needed = 0;
	}

	params.sg_len = chcr_hash_ent_in_wr(req->src, !!req_ctx->reqlen,
				    HASH_SPACE_LEFT(params.kctx_len), 0);
	if (params.sg_len < req->nbytes) {
		if (is_hmac(crypto_ahash_tfm(rtfm))) {
			params.kctx_len /= 2;
			params.opad_needed = 0;
		}
		params.last = 0;
		params.more = 1;
		params.sg_len = rounddown(params.sg_len + req_ctx->reqlen, bs)
					- req_ctx->reqlen;
		params.hash_size = params.alg_prm.result_size;
		params.scmd1 = 0;
	} else {
		params.last = 1;
		params.more = 0;
		params.sg_len = req->nbytes;
		params.hash_size = crypto_ahash_digestsize(rtfm);
		params.scmd1 = req_ctx->data_len + req_ctx->reqlen +
				params.sg_len;
	}
	params.bfr_len = req_ctx->reqlen;
	req_ctx->data_len += params.bfr_len + params.sg_len;
	req_ctx->hctx_wr.result = 1;
	req_ctx->hctx_wr.srcsg = req->src;
	if ((req_ctx->reqlen + req->nbytes) == 0) {
		create_last_hash_block(req_ctx->reqbfr, bs, req_ctx->data_len);
		params.last = 0;
		params.more = 1;
		params.scmd1 = 0;
		params.bfr_len = bs;
	}
	skb = create_hash_wr(req, &params);
	if (IS_ERR(skb)) {
		error = PTR_ERR(skb);
		goto unmap;
	}
	req_ctx->reqlen = 0;
	req_ctx->hctx_wr.processed += params.sg_len;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, req_ctx->txqidx);
	chcr_send_wr(skb);
	return -EINPROGRESS;
unmap:
	chcr_hash_dma_unmap(&u_ctx->lldi.pdev->dev, req);
err:
	chcr_dec_wrcount(dev);
	return error;
}

static int chcr_hmac_init(struct ahash_request *areq);
static int chcr_sha_init(struct ahash_request *areq);

static int chcr_ahash_digest(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_dev *dev = h_ctx(rtfm)->dev;
	struct uld_ctx *u_ctx = ULD_CTX(h_ctx(rtfm));
	struct chcr_context *ctx = h_ctx(rtfm);
	struct sk_buff *skb;
	struct hash_wr_param params;
	u8  bs;
	int error;
	unsigned int cpu;

	cpu = get_cpu();
	req_ctx->txqidx = cpu % ctx->ntxq;
	req_ctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	if (is_hmac(crypto_ahash_tfm(rtfm)))
		chcr_hmac_init(req);
	else
		chcr_sha_init(req);

	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));
	error = chcr_inc_wrcount(dev);
	if (error)
		return -ENXIO;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
						req_ctx->txqidx) &&
		(!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)))) {
			error = -ENOSPC;
			goto err;
	}

	chcr_init_hctx_per_wr(req_ctx);
	error = chcr_hash_dma_map(&u_ctx->lldi.pdev->dev, req);
	if (error) {
		error = -ENOMEM;
		goto err;
	}

	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	params.kctx_len = roundup(params.alg_prm.result_size, 16);
	if (is_hmac(crypto_ahash_tfm(rtfm))) {
		params.kctx_len *= 2;
		params.opad_needed = 1;
	} else {
		params.opad_needed = 0;
	}
	params.sg_len = chcr_hash_ent_in_wr(req->src, !!req_ctx->reqlen,
				HASH_SPACE_LEFT(params.kctx_len), 0);
	if (params.sg_len < req->nbytes) {
		if (is_hmac(crypto_ahash_tfm(rtfm))) {
			params.kctx_len /= 2;
			params.opad_needed = 0;
		}
		params.last = 0;
		params.more = 1;
		params.scmd1 = 0;
		params.sg_len = rounddown(params.sg_len, bs);
		params.hash_size = params.alg_prm.result_size;
	} else {
		params.sg_len = req->nbytes;
		params.hash_size = crypto_ahash_digestsize(rtfm);
		params.last = 1;
		params.more = 0;
		params.scmd1 = req->nbytes + req_ctx->data_len;

	}
	params.bfr_len = 0;
	req_ctx->hctx_wr.result = 1;
	req_ctx->hctx_wr.srcsg = req->src;
	req_ctx->data_len += params.bfr_len + params.sg_len;

	if (req->nbytes == 0) {
		create_last_hash_block(req_ctx->reqbfr, bs, req_ctx->data_len);
		params.more = 1;
		params.bfr_len = bs;
	}

	skb = create_hash_wr(req, &params);
	if (IS_ERR(skb)) {
		error = PTR_ERR(skb);
		goto unmap;
	}
	req_ctx->hctx_wr.processed += params.sg_len;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, req_ctx->txqidx);
	chcr_send_wr(skb);
	return -EINPROGRESS;
unmap:
	chcr_hash_dma_unmap(&u_ctx->lldi.pdev->dev, req);
err:
	chcr_dec_wrcount(dev);
	return error;
}

static int chcr_ahash_continue(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *reqctx = ahash_request_ctx(req);
	struct chcr_hctx_per_wr *hctx_wr = &reqctx->hctx_wr;
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = h_ctx(rtfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;
	struct hash_wr_param params;
	u8  bs;
	int error;
	unsigned int cpu;

	cpu = get_cpu();
	reqctx->txqidx = cpu % ctx->ntxq;
	reqctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	params.kctx_len = roundup(params.alg_prm.result_size, 16);
	if (is_hmac(crypto_ahash_tfm(rtfm))) {
		params.kctx_len *= 2;
		params.opad_needed = 1;
	} else {
		params.opad_needed = 0;
	}
	params.sg_len = chcr_hash_ent_in_wr(hctx_wr->srcsg, 0,
					    HASH_SPACE_LEFT(params.kctx_len),
					    hctx_wr->src_ofst);
	if ((params.sg_len + hctx_wr->processed) > req->nbytes)
		params.sg_len = req->nbytes - hctx_wr->processed;
	if (!hctx_wr->result ||
	    ((params.sg_len + hctx_wr->processed) < req->nbytes)) {
		if (is_hmac(crypto_ahash_tfm(rtfm))) {
			params.kctx_len /= 2;
			params.opad_needed = 0;
		}
		params.last = 0;
		params.more = 1;
		params.sg_len = rounddown(params.sg_len, bs);
		params.hash_size = params.alg_prm.result_size;
		params.scmd1 = 0;
	} else {
		params.last = 1;
		params.more = 0;
		params.hash_size = crypto_ahash_digestsize(rtfm);
		params.scmd1 = reqctx->data_len + params.sg_len;
	}
	params.bfr_len = 0;
	reqctx->data_len += params.sg_len;
	skb = create_hash_wr(req, &params);
	if (IS_ERR(skb)) {
		error = PTR_ERR(skb);
		goto err;
	}
	hctx_wr->processed += params.sg_len;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, reqctx->txqidx);
	chcr_send_wr(skb);
	return 0;
err:
	return error;
}

static inline void chcr_handle_ahash_resp(struct ahash_request *req,
					  unsigned char *input,
					  int err)
{
	struct chcr_ahash_req_ctx *reqctx = ahash_request_ctx(req);
	struct chcr_hctx_per_wr *hctx_wr = &reqctx->hctx_wr;
	int digestsize, updated_digestsize;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct uld_ctx *u_ctx = ULD_CTX(h_ctx(tfm));
	struct chcr_dev *dev = h_ctx(tfm)->dev;

	if (input == NULL)
		goto out;
	digestsize = crypto_ahash_digestsize(crypto_ahash_reqtfm(req));
	updated_digestsize = digestsize;
	if (digestsize == SHA224_DIGEST_SIZE)
		updated_digestsize = SHA256_DIGEST_SIZE;
	else if (digestsize == SHA384_DIGEST_SIZE)
		updated_digestsize = SHA512_DIGEST_SIZE;

	if (hctx_wr->dma_addr) {
		dma_unmap_single(&u_ctx->lldi.pdev->dev, hctx_wr->dma_addr,
				 hctx_wr->dma_len, DMA_TO_DEVICE);
		hctx_wr->dma_addr = 0;
	}
	if (hctx_wr->isfinal || ((hctx_wr->processed + reqctx->reqlen) ==
				 req->nbytes)) {
		if (hctx_wr->result == 1) {
			hctx_wr->result = 0;
			memcpy(req->result, input + sizeof(struct cpl_fw6_pld),
			       digestsize);
		} else {
			memcpy(reqctx->partial_hash,
			       input + sizeof(struct cpl_fw6_pld),
			       updated_digestsize);

		}
		goto unmap;
	}
	memcpy(reqctx->partial_hash, input + sizeof(struct cpl_fw6_pld),
	       updated_digestsize);

	err = chcr_ahash_continue(req);
	if (err)
		goto unmap;
	return;
unmap:
	if (hctx_wr->is_sg_map)
		chcr_hash_dma_unmap(&u_ctx->lldi.pdev->dev, req);


out:
	chcr_dec_wrcount(dev);
	ahash_request_complete(req, err);
}

/*
 *	chcr_handle_resp - Unmap the DMA buffers associated with the request
 *	@req: crypto request
 */
int chcr_handle_resp(struct crypto_async_request *req, unsigned char *input,
			 int err)
{
	struct crypto_tfm *tfm = req->tfm;
	struct chcr_context *ctx = crypto_tfm_ctx(tfm);
	struct adapter *adap = padap(ctx->dev);

	switch (tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AEAD:
		err = chcr_handle_aead_resp(aead_request_cast(req), input, err);
		break;

	case CRYPTO_ALG_TYPE_SKCIPHER:
		 chcr_handle_cipher_resp(skcipher_request_cast(req),
					       input, err);
		break;
	case CRYPTO_ALG_TYPE_AHASH:
		chcr_handle_ahash_resp(ahash_request_cast(req), input, err);
		}
	atomic_inc(&adap->chcr_stats.complete);
	return err;
}
static int chcr_ahash_export(struct ahash_request *areq, void *out)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct chcr_ahash_req_ctx *state = out;

	state->reqlen = req_ctx->reqlen;
	state->data_len = req_ctx->data_len;
	memcpy(state->bfr1, req_ctx->reqbfr, req_ctx->reqlen);
	memcpy(state->partial_hash, req_ctx->partial_hash,
	       CHCR_HASH_MAX_DIGEST_SIZE);
	chcr_init_hctx_per_wr(state);
	return 0;
}

static int chcr_ahash_import(struct ahash_request *areq, const void *in)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct chcr_ahash_req_ctx *state = (struct chcr_ahash_req_ctx *)in;

	req_ctx->reqlen = state->reqlen;
	req_ctx->data_len = state->data_len;
	req_ctx->reqbfr = req_ctx->bfr1;
	req_ctx->skbfr = req_ctx->bfr2;
	memcpy(req_ctx->bfr1, state->bfr1, CHCR_HASH_MAX_BLOCK_SIZE_128);
	memcpy(req_ctx->partial_hash, state->partial_hash,
	       CHCR_HASH_MAX_DIGEST_SIZE);
	chcr_init_hctx_per_wr(req_ctx);
	return 0;
}

static int chcr_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct hmac_ctx *hmacctx = HMAC_CTX(h_ctx(tfm));

	/* use the key to calculate the ipad and opad. ipad will sent with the
	 * first request's data. opad will be sent with the final hash result
	 * ipad in hmacctx->ipad and opad in hmacctx->opad location
	 */
	return chcr_prepare_hmac_key(key, keylen, crypto_ahash_digestsize(tfm),
				     hmacctx->ipad, hmacctx->opad);
}

static int chcr_aes_xts_setkey(struct crypto_skcipher *cipher, const u8 *key,
			       unsigned int key_len)
{
	struct ablk_ctx *ablkctx = ABLK_CTX(c_ctx(cipher));
	unsigned short context_size = 0;
	int err;

	err = chcr_cipher_fallback_setkey(cipher, key, key_len);
	if (err)
		goto badkey_err;

	memcpy(ablkctx->key, key, key_len);
	ablkctx->enckey_len = key_len;
	get_aes_decrypt_key(ablkctx->rrkey, ablkctx->key, key_len << 2);
	context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD + key_len) >> 4;
	/* Both keys for xts must be aligned to 16 byte boundary
	 * by padding with zeros. So for 24 byte keys padding 8 zeroes.
	 */
	if (key_len == 48) {
		context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD + key_len
				+ 16) >> 4;
		memmove(ablkctx->key + 32, ablkctx->key + 24, 24);
		memset(ablkctx->key + 24, 0, 8);
		memset(ablkctx->key + 56, 0, 8);
		ablkctx->enckey_len = 64;
		ablkctx->key_ctx_hdr =
			FILL_KEY_CTX_HDR(CHCR_KEYCTX_CIPHER_KEY_SIZE_192,
					 CHCR_KEYCTX_NO_KEY, 1,
					 0, context_size);
	} else {
		ablkctx->key_ctx_hdr =
		FILL_KEY_CTX_HDR((key_len == AES_KEYSIZE_256) ?
				 CHCR_KEYCTX_CIPHER_KEY_SIZE_128 :
				 CHCR_KEYCTX_CIPHER_KEY_SIZE_256,
				 CHCR_KEYCTX_NO_KEY, 1,
				 0, context_size);
	}
	ablkctx->ciph_mode = CHCR_SCMD_CIPHER_MODE_AES_XTS;
	return 0;
badkey_err:
	ablkctx->enckey_len = 0;

	return err;
}

static int chcr_sha_init(struct ahash_request *areq)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	int digestsize =  crypto_ahash_digestsize(tfm);

	req_ctx->data_len = 0;
	req_ctx->reqlen = 0;
	req_ctx->reqbfr = req_ctx->bfr1;
	req_ctx->skbfr = req_ctx->bfr2;
	copy_hash_init_values(req_ctx->partial_hash, digestsize);

	return 0;
}

static int chcr_sha_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct chcr_ahash_req_ctx));
	return chcr_device_init(crypto_tfm_ctx(tfm));
}

static int chcr_hmac_init(struct ahash_request *areq)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(areq);
	struct hmac_ctx *hmacctx = HMAC_CTX(h_ctx(rtfm));
	unsigned int digestsize = crypto_ahash_digestsize(rtfm);
	unsigned int bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));

	chcr_sha_init(areq);
	req_ctx->data_len = bs;
	if (is_hmac(crypto_ahash_tfm(rtfm))) {
		if (digestsize == SHA224_DIGEST_SIZE)
			memcpy(req_ctx->partial_hash, hmacctx->ipad,
			       SHA256_DIGEST_SIZE);
		else if (digestsize == SHA384_DIGEST_SIZE)
			memcpy(req_ctx->partial_hash, hmacctx->ipad,
			       SHA512_DIGEST_SIZE);
		else
			memcpy(req_ctx->partial_hash, hmacctx->ipad,
			       digestsize);
	}
	return 0;
}

static int chcr_hmac_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct chcr_ahash_req_ctx));
	return chcr_device_init(crypto_tfm_ctx(tfm));
}

inline void chcr_aead_common_exit(struct aead_request *req)
{
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct uld_ctx *u_ctx = ULD_CTX(a_ctx(tfm));

	chcr_aead_dma_unmap(&u_ctx->lldi.pdev->dev, req, reqctx->op);
}

static int chcr_aead_common_init(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	unsigned int authsize = crypto_aead_authsize(tfm);
	int error = -EINVAL;

	/* validate key size */
	if (aeadctx->enckey_len == 0)
		goto err;
	if (reqctx->op && req->cryptlen < authsize)
		goto err;
	if (reqctx->b0_len)
		reqctx->scratch_pad = reqctx->iv + IV;
	else
		reqctx->scratch_pad = NULL;

	error = chcr_aead_dma_map(&ULD_CTX(a_ctx(tfm))->lldi.pdev->dev, req,
				  reqctx->op);
	if (error) {
		error = -ENOMEM;
		goto err;
	}

	return 0;
err:
	return error;
}

static int chcr_aead_need_fallback(struct aead_request *req, int dst_nents,
				   int aadmax, int wrlen,
				   unsigned short op_type)
{
	unsigned int authsize = crypto_aead_authsize(crypto_aead_reqtfm(req));

	if (((req->cryptlen - (op_type ? authsize : 0)) == 0) ||
	    dst_nents > MAX_DSGL_ENT ||
	    (req->assoclen > aadmax) ||
	    (wrlen > SGE_MAX_WR_LEN))
		return 1;
	return 0;
}

static int chcr_aead_fallback(struct aead_request *req, unsigned short op_type)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));
	struct aead_request *subreq = aead_request_ctx_dma(req);

	aead_request_set_tfm(subreq, aeadctx->sw_cipher);
	aead_request_set_callback(subreq, req->base.flags,
				  req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
				 req->iv);
	aead_request_set_ad(subreq, req->assoclen);
	return op_type ? crypto_aead_decrypt(subreq) :
		crypto_aead_encrypt(subreq);
}

static struct sk_buff *create_authenc_wr(struct aead_request *req,
					 unsigned short qid,
					 int size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = a_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_authenc_ctx *actx = AUTHENC_CTX(aeadctx);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct ulptx_sgl *ulptx;
	unsigned int transhdr_len;
	unsigned int dst_size = 0, temp, subtype = get_aead_subtype(tfm);
	unsigned int   kctx_len = 0, dnents, snents;
	unsigned int  authsize = crypto_aead_authsize(tfm);
	int error = -EINVAL;
	u8 *ivptr;
	int null = 0;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;
	struct adapter *adap = padap(ctx->dev);
	unsigned int rx_channel_id = reqctx->rxqidx / ctx->rxq_perchan;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);
	if (req->cryptlen == 0)
		return NULL;

	reqctx->b0_len = 0;
	error = chcr_aead_common_init(req);
	if (error)
		return ERR_PTR(error);

	if (subtype == CRYPTO_ALG_SUB_TYPE_CBC_NULL ||
		subtype == CRYPTO_ALG_SUB_TYPE_CTR_NULL) {
		null = 1;
	}
	dnents = sg_nents_xlen(req->dst, req->assoclen + req->cryptlen +
		(reqctx->op ? -authsize : authsize), CHCR_DST_SG_SIZE, 0);
	dnents += MIN_AUTH_SG; // For IV
	snents = sg_nents_xlen(req->src, req->assoclen + req->cryptlen,
			       CHCR_SRC_SG_SIZE, 0);
	dst_size = get_space_for_phys_dsgl(dnents);
	kctx_len = (KEY_CONTEXT_CTX_LEN_G(ntohl(aeadctx->key_ctx_hdr)) << 4)
		- sizeof(chcr_req->key_ctx);
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	reqctx->imm = (transhdr_len + req->assoclen + req->cryptlen) <
			SGE_MAX_WR_LEN;
	temp = reqctx->imm ? roundup(req->assoclen + req->cryptlen, 16)
			: (sgl_len(snents) * 8);
	transhdr_len += temp;
	transhdr_len = roundup(transhdr_len, 16);

	if (chcr_aead_need_fallback(req, dnents, T6_MAX_AAD_SIZE,
				    transhdr_len, reqctx->op)) {
		atomic_inc(&adap->chcr_stats.fallback);
		chcr_aead_common_exit(req);
		return ERR_PTR(chcr_aead_fallback(req, reqctx->op));
	}
	skb = alloc_skb(transhdr_len, flags);
	if (!skb) {
		error = -ENOMEM;
		goto err;
	}

	chcr_req = __skb_put_zero(skb, transhdr_len);

	temp  = (reqctx->op == CHCR_ENCRYPT_OP) ? 0 : authsize;

	/*
	 * Input order	is AAD,IV and Payload. where IV should be included as
	 * the part of authdata. All other fields should be filled according
	 * to the hardware spec
	 */
	chcr_req->sec_cpl.op_ivinsrtofst =
				FILL_SEC_CPL_OP_IVINSR(rx_channel_id, 2, 1);
	chcr_req->sec_cpl.pldlen = htonl(req->assoclen + IV + req->cryptlen);
	chcr_req->sec_cpl.aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
					null ? 0 : 1 + IV,
					null ? 0 : IV + req->assoclen,
					req->assoclen + IV + 1,
					(temp & 0x1F0) >> 4);
	chcr_req->sec_cpl.cipherstop_lo_authinsert = FILL_SEC_CPL_AUTHINSERT(
					temp & 0xF,
					null ? 0 : req->assoclen + IV + 1,
					temp, temp);
	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR_NULL ||
	    subtype == CRYPTO_ALG_SUB_TYPE_CTR_SHA)
		temp = CHCR_SCMD_CIPHER_MODE_AES_CTR;
	else
		temp = CHCR_SCMD_CIPHER_MODE_AES_CBC;
	chcr_req->sec_cpl.seqno_numivs = FILL_SEC_CPL_SCMD0_SEQNO(reqctx->op,
					(reqctx->op == CHCR_ENCRYPT_OP) ? 1 : 0,
					temp,
					actx->auth_mode, aeadctx->hmac_ctrl,
					IV >> 1);
	chcr_req->sec_cpl.ivgen_hdrlen =  FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1,
					 0, 0, dst_size);

	chcr_req->key_ctx.ctx_hdr = aeadctx->key_ctx_hdr;
	if (reqctx->op == CHCR_ENCRYPT_OP ||
		subtype == CRYPTO_ALG_SUB_TYPE_CTR_SHA ||
		subtype == CRYPTO_ALG_SUB_TYPE_CTR_NULL)
		memcpy(chcr_req->key_ctx.key, aeadctx->key,
		       aeadctx->enckey_len);
	else
		memcpy(chcr_req->key_ctx.key, actx->dec_rrkey,
		       aeadctx->enckey_len);

	memcpy(chcr_req->key_ctx.key + roundup(aeadctx->enckey_len, 16),
	       actx->h_iopad, kctx_len - roundup(aeadctx->enckey_len, 16));
	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	ivptr = (u8 *)(phys_cpl + 1) + dst_size;
	ulptx = (struct ulptx_sgl *)(ivptr + IV);
	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR_SHA ||
	    subtype == CRYPTO_ALG_SUB_TYPE_CTR_NULL) {
		memcpy(ivptr, aeadctx->nonce, CTR_RFC3686_NONCE_SIZE);
		memcpy(ivptr + CTR_RFC3686_NONCE_SIZE, req->iv,
				CTR_RFC3686_IV_SIZE);
		*(__be32 *)(ivptr + CTR_RFC3686_NONCE_SIZE +
			CTR_RFC3686_IV_SIZE) = cpu_to_be32(1);
	} else {
		memcpy(ivptr, req->iv, IV);
	}
	chcr_add_aead_dst_ent(req, phys_cpl, qid);
	chcr_add_aead_src_ent(req, ulptx);
	atomic_inc(&adap->chcr_stats.cipher_rqst);
	temp = sizeof(struct cpl_rx_phys_dsgl) + dst_size + IV +
		kctx_len + (reqctx->imm ? (req->assoclen + req->cryptlen) : 0);
	create_wreq(a_ctx(tfm), chcr_req, &req->base, reqctx->imm, size,
		   transhdr_len, temp, 0);
	reqctx->skb = skb;

	return skb;
err:
	chcr_aead_common_exit(req);

	return ERR_PTR(error);
}

int chcr_aead_dma_map(struct device *dev,
		      struct aead_request *req,
		      unsigned short op_type)
{
	int error;
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(tfm);
	int src_len, dst_len;

	/* calculate and handle src and dst sg length separately
	 * for inplace and out-of place operations
	 */
	if (req->src == req->dst) {
		src_len = req->assoclen + req->cryptlen + (op_type ?
							0 : authsize);
		dst_len = src_len;
	} else {
		src_len = req->assoclen + req->cryptlen;
		dst_len = req->assoclen + req->cryptlen + (op_type ?
							-authsize : authsize);
	}

	if (!req->cryptlen || !src_len || !dst_len)
		return 0;
	reqctx->iv_dma = dma_map_single(dev, reqctx->iv, (IV + reqctx->b0_len),
					DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, reqctx->iv_dma))
		return -ENOMEM;
	if (reqctx->b0_len)
		reqctx->b0_dma = reqctx->iv_dma + IV;
	else
		reqctx->b0_dma = 0;
	if (req->src == req->dst) {
		error = dma_map_sg(dev, req->src,
				sg_nents_for_len(req->src, src_len),
					DMA_BIDIRECTIONAL);
		if (!error)
			goto err;
	} else {
		error = dma_map_sg(dev, req->src,
				   sg_nents_for_len(req->src, src_len),
				   DMA_TO_DEVICE);
		if (!error)
			goto err;
		error = dma_map_sg(dev, req->dst,
				   sg_nents_for_len(req->dst, dst_len),
				   DMA_FROM_DEVICE);
		if (!error) {
			dma_unmap_sg(dev, req->src,
				     sg_nents_for_len(req->src, src_len),
				     DMA_TO_DEVICE);
			goto err;
		}
	}

	return 0;
err:
	dma_unmap_single(dev, reqctx->iv_dma, IV, DMA_BIDIRECTIONAL);
	return -ENOMEM;
}

void chcr_aead_dma_unmap(struct device *dev,
			 struct aead_request *req,
			 unsigned short op_type)
{
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(tfm);
	int src_len, dst_len;

	/* calculate and handle src and dst sg length separately
	 * for inplace and out-of place operations
	 */
	if (req->src == req->dst) {
		src_len = req->assoclen + req->cryptlen + (op_type ?
							0 : authsize);
		dst_len = src_len;
	} else {
		src_len = req->assoclen + req->cryptlen;
		dst_len = req->assoclen + req->cryptlen + (op_type ?
						-authsize : authsize);
	}

	if (!req->cryptlen || !src_len || !dst_len)
		return;

	dma_unmap_single(dev, reqctx->iv_dma, (IV + reqctx->b0_len),
					DMA_BIDIRECTIONAL);
	if (req->src == req->dst) {
		dma_unmap_sg(dev, req->src,
			     sg_nents_for_len(req->src, src_len),
			     DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(dev, req->src,
			     sg_nents_for_len(req->src, src_len),
			     DMA_TO_DEVICE);
		dma_unmap_sg(dev, req->dst,
			     sg_nents_for_len(req->dst, dst_len),
			     DMA_FROM_DEVICE);
	}
}

void chcr_add_aead_src_ent(struct aead_request *req,
			   struct ulptx_sgl *ulptx)
{
	struct ulptx_walk ulp_walk;
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);

	if (reqctx->imm) {
		u8 *buf = (u8 *)ulptx;

		if (reqctx->b0_len) {
			memcpy(buf, reqctx->scratch_pad, reqctx->b0_len);
			buf += reqctx->b0_len;
		}
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   buf, req->cryptlen + req->assoclen, 0);
	} else {
		ulptx_walk_init(&ulp_walk, ulptx);
		if (reqctx->b0_len)
			ulptx_walk_add_page(&ulp_walk, reqctx->b0_len,
					    reqctx->b0_dma);
		ulptx_walk_add_sg(&ulp_walk, req->src, req->cryptlen +
				  req->assoclen,  0);
		ulptx_walk_end(&ulp_walk);
	}
}

void chcr_add_aead_dst_ent(struct aead_request *req,
			   struct cpl_rx_phys_dsgl *phys_cpl,
			   unsigned short qid)
{
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct dsgl_walk dsgl_walk;
	unsigned int authsize = crypto_aead_authsize(tfm);
	struct chcr_context *ctx = a_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	u32 temp;
	unsigned int rx_channel_id = reqctx->rxqidx / ctx->rxq_perchan;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);
	dsgl_walk_init(&dsgl_walk, phys_cpl);
	dsgl_walk_add_page(&dsgl_walk, IV + reqctx->b0_len, reqctx->iv_dma);
	temp = req->assoclen + req->cryptlen +
		(reqctx->op ? -authsize : authsize);
	dsgl_walk_add_sg(&dsgl_walk, req->dst, temp, 0);
	dsgl_walk_end(&dsgl_walk, qid, rx_channel_id);
}

void chcr_add_cipher_src_ent(struct skcipher_request *req,
			     void *ulptx,
			     struct  cipher_wr_param *wrparam)
{
	struct ulptx_walk ulp_walk;
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	u8 *buf = ulptx;

	memcpy(buf, reqctx->iv, IV);
	buf += IV;
	if (reqctx->imm) {
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   buf, wrparam->bytes, reqctx->processed);
	} else {
		ulptx_walk_init(&ulp_walk, (struct ulptx_sgl *)buf);
		ulptx_walk_add_sg(&ulp_walk, reqctx->srcsg, wrparam->bytes,
				  reqctx->src_ofst);
		reqctx->srcsg = ulp_walk.last_sg;
		reqctx->src_ofst = ulp_walk.last_sg_len;
		ulptx_walk_end(&ulp_walk);
	}
}

void chcr_add_cipher_dst_ent(struct skcipher_request *req,
			     struct cpl_rx_phys_dsgl *phys_cpl,
			     struct  cipher_wr_param *wrparam,
			     unsigned short qid)
{
	struct chcr_skcipher_req_ctx *reqctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(wrparam->req);
	struct chcr_context *ctx = c_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct dsgl_walk dsgl_walk;
	unsigned int rx_channel_id = reqctx->rxqidx / ctx->rxq_perchan;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);
	dsgl_walk_init(&dsgl_walk, phys_cpl);
	dsgl_walk_add_sg(&dsgl_walk, reqctx->dstsg, wrparam->bytes,
			 reqctx->dst_ofst);
	reqctx->dstsg = dsgl_walk.last_sg;
	reqctx->dst_ofst = dsgl_walk.last_sg_len;
	dsgl_walk_end(&dsgl_walk, qid, rx_channel_id);
}

void chcr_add_hash_src_ent(struct ahash_request *req,
			   struct ulptx_sgl *ulptx,
			   struct hash_wr_param *param)
{
	struct ulptx_walk ulp_walk;
	struct chcr_ahash_req_ctx *reqctx = ahash_request_ctx(req);

	if (reqctx->hctx_wr.imm) {
		u8 *buf = (u8 *)ulptx;

		if (param->bfr_len) {
			memcpy(buf, reqctx->reqbfr, param->bfr_len);
			buf += param->bfr_len;
		}

		sg_pcopy_to_buffer(reqctx->hctx_wr.srcsg,
				   sg_nents(reqctx->hctx_wr.srcsg), buf,
				   param->sg_len, 0);
	} else {
		ulptx_walk_init(&ulp_walk, ulptx);
		if (param->bfr_len)
			ulptx_walk_add_page(&ulp_walk, param->bfr_len,
					    reqctx->hctx_wr.dma_addr);
		ulptx_walk_add_sg(&ulp_walk, reqctx->hctx_wr.srcsg,
				  param->sg_len, reqctx->hctx_wr.src_ofst);
		reqctx->hctx_wr.srcsg = ulp_walk.last_sg;
		reqctx->hctx_wr.src_ofst = ulp_walk.last_sg_len;
		ulptx_walk_end(&ulp_walk);
	}
}

int chcr_hash_dma_map(struct device *dev,
		      struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	int error = 0;

	if (!req->nbytes)
		return 0;
	error = dma_map_sg(dev, req->src, sg_nents(req->src),
			   DMA_TO_DEVICE);
	if (!error)
		return -ENOMEM;
	req_ctx->hctx_wr.is_sg_map = 1;
	return 0;
}

void chcr_hash_dma_unmap(struct device *dev,
			 struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);

	if (!req->nbytes)
		return;

	dma_unmap_sg(dev, req->src, sg_nents(req->src),
			   DMA_TO_DEVICE);
	req_ctx->hctx_wr.is_sg_map = 0;

}

int chcr_cipher_dma_map(struct device *dev,
			struct skcipher_request *req)
{
	int error;

	if (req->src == req->dst) {
		error = dma_map_sg(dev, req->src, sg_nents(req->src),
				   DMA_BIDIRECTIONAL);
		if (!error)
			goto err;
	} else {
		error = dma_map_sg(dev, req->src, sg_nents(req->src),
				   DMA_TO_DEVICE);
		if (!error)
			goto err;
		error = dma_map_sg(dev, req->dst, sg_nents(req->dst),
				   DMA_FROM_DEVICE);
		if (!error) {
			dma_unmap_sg(dev, req->src, sg_nents(req->src),
				   DMA_TO_DEVICE);
			goto err;
		}
	}

	return 0;
err:
	return -ENOMEM;
}

void chcr_cipher_dma_unmap(struct device *dev,
			   struct skcipher_request *req)
{
	if (req->src == req->dst) {
		dma_unmap_sg(dev, req->src, sg_nents(req->src),
				   DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(dev, req->src, sg_nents(req->src),
				   DMA_TO_DEVICE);
		dma_unmap_sg(dev, req->dst, sg_nents(req->dst),
				   DMA_FROM_DEVICE);
	}
}

static int set_msg_len(u8 *block, unsigned int msglen, int csize)
{
	__be32 data;

	memset(block, 0, csize);
	block += csize;

	if (csize >= 4)
		csize = 4;
	else if (msglen > (unsigned int)(1 << (8 * csize)))
		return -EOVERFLOW;

	data = cpu_to_be32(msglen);
	memcpy(block - csize, (u8 *)&data + 4 - csize, csize);

	return 0;
}

static int generate_b0(struct aead_request *req, u8 *ivptr,
			unsigned short op_type)
{
	unsigned int l, lp, m;
	int rc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	u8 *b0 = reqctx->scratch_pad;

	m = crypto_aead_authsize(aead);

	memcpy(b0, ivptr, 16);

	lp = b0[0];
	l = lp + 1;

	/* set m, bits 3-5 */
	*b0 |= (8 * ((m - 2) / 2));

	/* set adata, bit 6, if associated data is used */
	if (req->assoclen)
		*b0 |= 64;
	rc = set_msg_len(b0 + 16 - l,
			 (op_type == CHCR_DECRYPT_OP) ?
			 req->cryptlen - m : req->cryptlen, l);

	return rc;
}

static inline int crypto_ccm_check_iv(const u8 *iv)
{
	/* 2 <= L <= 8, so 1 <= L' <= 7. */
	if (iv[0] < 1 || iv[0] > 7)
		return -EINVAL;

	return 0;
}

static int ccm_format_packet(struct aead_request *req,
			     u8 *ivptr,
			     unsigned int sub_type,
			     unsigned short op_type,
			     unsigned int assoclen)
{
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));
	int rc = 0;

	if (sub_type == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309) {
		ivptr[0] = 3;
		memcpy(ivptr + 1, &aeadctx->salt[0], 3);
		memcpy(ivptr + 4, req->iv, 8);
		memset(ivptr + 12, 0, 4);
	} else {
		memcpy(ivptr, req->iv, 16);
	}
	if (assoclen)
		put_unaligned_be16(assoclen, &reqctx->scratch_pad[16]);

	rc = generate_b0(req, ivptr, op_type);
	/* zero the ctr value */
	memset(ivptr + 15 - ivptr[0], 0, ivptr[0] + 1);
	return rc;
}

static void fill_sec_cpl_for_aead(struct cpl_tx_sec_pdu *sec_cpl,
				  unsigned int dst_size,
				  struct aead_request *req,
				  unsigned short op_type)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = a_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	unsigned int cipher_mode = CHCR_SCMD_CIPHER_MODE_AES_CCM;
	unsigned int mac_mode = CHCR_SCMD_AUTH_MODE_CBCMAC;
	unsigned int rx_channel_id = reqctx->rxqidx / ctx->rxq_perchan;
	unsigned int ccm_xtra;
	unsigned int tag_offset = 0, auth_offset = 0;
	unsigned int assoclen;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);

	if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309)
		assoclen = req->assoclen - 8;
	else
		assoclen = req->assoclen;
	ccm_xtra = CCM_B0_SIZE +
		((assoclen) ? CCM_AAD_FIELD_SIZE : 0);

	auth_offset = req->cryptlen ?
		(req->assoclen + IV + 1 + ccm_xtra) : 0;
	if (op_type == CHCR_DECRYPT_OP) {
		if (crypto_aead_authsize(tfm) != req->cryptlen)
			tag_offset = crypto_aead_authsize(tfm);
		else
			auth_offset = 0;
	}

	sec_cpl->op_ivinsrtofst = FILL_SEC_CPL_OP_IVINSR(rx_channel_id, 2, 1);
	sec_cpl->pldlen =
		htonl(req->assoclen + IV + req->cryptlen + ccm_xtra);
	/* For CCM there wil be b0 always. So AAD start will be 1 always */
	sec_cpl->aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
				1 + IV,	IV + assoclen + ccm_xtra,
				req->assoclen + IV + 1 + ccm_xtra, 0);

	sec_cpl->cipherstop_lo_authinsert = FILL_SEC_CPL_AUTHINSERT(0,
					auth_offset, tag_offset,
					(op_type == CHCR_ENCRYPT_OP) ? 0 :
					crypto_aead_authsize(tfm));
	sec_cpl->seqno_numivs =  FILL_SEC_CPL_SCMD0_SEQNO(op_type,
					(op_type == CHCR_ENCRYPT_OP) ? 0 : 1,
					cipher_mode, mac_mode,
					aeadctx->hmac_ctrl, IV >> 1);

	sec_cpl->ivgen_hdrlen = FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1, 0,
					0, dst_size);
}

static int aead_ccm_validate_input(unsigned short op_type,
				   struct aead_request *req,
				   struct chcr_aead_ctx *aeadctx,
				   unsigned int sub_type)
{
	if (sub_type != CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309) {
		if (crypto_ccm_check_iv(req->iv)) {
			pr_err("CCM: IV check fails\n");
			return -EINVAL;
		}
	} else {
		if (req->assoclen != 16 && req->assoclen != 20) {
			pr_err("RFC4309: Invalid AAD length %d\n",
			       req->assoclen);
			return -EINVAL;
		}
	}
	return 0;
}

static struct sk_buff *create_aead_ccm_wr(struct aead_request *req,
					  unsigned short qid,
					  int size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct ulptx_sgl *ulptx;
	unsigned int transhdr_len;
	unsigned int dst_size = 0, kctx_len, dnents, temp, snents;
	unsigned int sub_type, assoclen = req->assoclen;
	unsigned int authsize = crypto_aead_authsize(tfm);
	int error = -EINVAL;
	u8 *ivptr;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;
	struct adapter *adap = padap(a_ctx(tfm)->dev);

	sub_type = get_aead_subtype(tfm);
	if (sub_type == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309)
		assoclen -= 8;
	reqctx->b0_len = CCM_B0_SIZE + (assoclen ? CCM_AAD_FIELD_SIZE : 0);
	error = chcr_aead_common_init(req);
	if (error)
		return ERR_PTR(error);

	error = aead_ccm_validate_input(reqctx->op, req, aeadctx, sub_type);
	if (error)
		goto err;
	dnents = sg_nents_xlen(req->dst, req->assoclen + req->cryptlen
			+ (reqctx->op ? -authsize : authsize),
			CHCR_DST_SG_SIZE, 0);
	dnents += MIN_CCM_SG; // For IV and B0
	dst_size = get_space_for_phys_dsgl(dnents);
	snents = sg_nents_xlen(req->src, req->assoclen + req->cryptlen,
			       CHCR_SRC_SG_SIZE, 0);
	snents += MIN_CCM_SG; //For B0
	kctx_len = roundup(aeadctx->enckey_len, 16) * 2;
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	reqctx->imm = (transhdr_len + req->assoclen + req->cryptlen +
		       reqctx->b0_len) <= SGE_MAX_WR_LEN;
	temp = reqctx->imm ? roundup(req->assoclen + req->cryptlen +
				     reqctx->b0_len, 16) :
		(sgl_len(snents) *  8);
	transhdr_len += temp;
	transhdr_len = roundup(transhdr_len, 16);

	if (chcr_aead_need_fallback(req, dnents, T6_MAX_AAD_SIZE -
				reqctx->b0_len, transhdr_len, reqctx->op)) {
		atomic_inc(&adap->chcr_stats.fallback);
		chcr_aead_common_exit(req);
		return ERR_PTR(chcr_aead_fallback(req, reqctx->op));
	}
	skb = alloc_skb(transhdr_len,  flags);

	if (!skb) {
		error = -ENOMEM;
		goto err;
	}

	chcr_req = __skb_put_zero(skb, transhdr_len);

	fill_sec_cpl_for_aead(&chcr_req->sec_cpl, dst_size, req, reqctx->op);

	chcr_req->key_ctx.ctx_hdr = aeadctx->key_ctx_hdr;
	memcpy(chcr_req->key_ctx.key, aeadctx->key, aeadctx->enckey_len);
	memcpy(chcr_req->key_ctx.key + roundup(aeadctx->enckey_len, 16),
			aeadctx->key, aeadctx->enckey_len);

	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	ivptr = (u8 *)(phys_cpl + 1) + dst_size;
	ulptx = (struct ulptx_sgl *)(ivptr + IV);
	error = ccm_format_packet(req, ivptr, sub_type, reqctx->op, assoclen);
	if (error)
		goto dstmap_fail;
	chcr_add_aead_dst_ent(req, phys_cpl, qid);
	chcr_add_aead_src_ent(req, ulptx);

	atomic_inc(&adap->chcr_stats.aead_rqst);
	temp = sizeof(struct cpl_rx_phys_dsgl) + dst_size + IV +
		kctx_len + (reqctx->imm ? (req->assoclen + req->cryptlen +
		reqctx->b0_len) : 0);
	create_wreq(a_ctx(tfm), chcr_req, &req->base, reqctx->imm, 0,
		    transhdr_len, temp, 0);
	reqctx->skb = skb;

	return skb;
dstmap_fail:
	kfree_skb(skb);
err:
	chcr_aead_common_exit(req);
	return ERR_PTR(error);
}

static struct sk_buff *create_gcm_wr(struct aead_request *req,
				     unsigned short qid,
				     int size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = a_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct ulptx_sgl *ulptx;
	unsigned int transhdr_len, dnents = 0, snents;
	unsigned int dst_size = 0, temp = 0, kctx_len, assoclen = req->assoclen;
	unsigned int authsize = crypto_aead_authsize(tfm);
	int error = -EINVAL;
	u8 *ivptr;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;
	struct adapter *adap = padap(ctx->dev);
	unsigned int rx_channel_id = reqctx->rxqidx / ctx->rxq_perchan;

	rx_channel_id = cxgb4_port_e2cchan(u_ctx->lldi.ports[rx_channel_id]);
	if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106)
		assoclen = req->assoclen - 8;

	reqctx->b0_len = 0;
	error = chcr_aead_common_init(req);
	if (error)
		return ERR_PTR(error);
	dnents = sg_nents_xlen(req->dst, req->assoclen + req->cryptlen +
				(reqctx->op ? -authsize : authsize),
				CHCR_DST_SG_SIZE, 0);
	snents = sg_nents_xlen(req->src, req->assoclen + req->cryptlen,
			       CHCR_SRC_SG_SIZE, 0);
	dnents += MIN_GCM_SG; // For IV
	dst_size = get_space_for_phys_dsgl(dnents);
	kctx_len = roundup(aeadctx->enckey_len, 16) + AEAD_H_SIZE;
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	reqctx->imm = (transhdr_len + req->assoclen + req->cryptlen) <=
			SGE_MAX_WR_LEN;
	temp = reqctx->imm ? roundup(req->assoclen + req->cryptlen, 16) :
		(sgl_len(snents) * 8);
	transhdr_len += temp;
	transhdr_len = roundup(transhdr_len, 16);
	if (chcr_aead_need_fallback(req, dnents, T6_MAX_AAD_SIZE,
			    transhdr_len, reqctx->op)) {

		atomic_inc(&adap->chcr_stats.fallback);
		chcr_aead_common_exit(req);
		return ERR_PTR(chcr_aead_fallback(req, reqctx->op));
	}
	skb = alloc_skb(transhdr_len, flags);
	if (!skb) {
		error = -ENOMEM;
		goto err;
	}

	chcr_req = __skb_put_zero(skb, transhdr_len);

	//Offset of tag from end
	temp = (reqctx->op == CHCR_ENCRYPT_OP) ? 0 : authsize;
	chcr_req->sec_cpl.op_ivinsrtofst = FILL_SEC_CPL_OP_IVINSR(
						rx_channel_id, 2, 1);
	chcr_req->sec_cpl.pldlen =
		htonl(req->assoclen + IV + req->cryptlen);
	chcr_req->sec_cpl.aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
					assoclen ? 1 + IV : 0,
					assoclen ? IV + assoclen : 0,
					req->assoclen + IV + 1, 0);
	chcr_req->sec_cpl.cipherstop_lo_authinsert =
			FILL_SEC_CPL_AUTHINSERT(0, req->assoclen + IV + 1,
						temp, temp);
	chcr_req->sec_cpl.seqno_numivs =
			FILL_SEC_CPL_SCMD0_SEQNO(reqctx->op, (reqctx->op ==
					CHCR_ENCRYPT_OP) ? 1 : 0,
					CHCR_SCMD_CIPHER_MODE_AES_GCM,
					CHCR_SCMD_AUTH_MODE_GHASH,
					aeadctx->hmac_ctrl, IV >> 1);
	chcr_req->sec_cpl.ivgen_hdrlen =  FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1,
					0, 0, dst_size);
	chcr_req->key_ctx.ctx_hdr = aeadctx->key_ctx_hdr;
	memcpy(chcr_req->key_ctx.key, aeadctx->key, aeadctx->enckey_len);
	memcpy(chcr_req->key_ctx.key + roundup(aeadctx->enckey_len, 16),
	       GCM_CTX(aeadctx)->ghash_h, AEAD_H_SIZE);

	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	ivptr = (u8 *)(phys_cpl + 1) + dst_size;
	/* prepare a 16 byte iv */
	/* S   A   L  T |  IV | 0x00000001 */
	if (get_aead_subtype(tfm) ==
	    CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106) {
		memcpy(ivptr, aeadctx->salt, 4);
		memcpy(ivptr + 4, req->iv, GCM_RFC4106_IV_SIZE);
	} else {
		memcpy(ivptr, req->iv, GCM_AES_IV_SIZE);
	}
	put_unaligned_be32(0x01, &ivptr[12]);
	ulptx = (struct ulptx_sgl *)(ivptr + 16);

	chcr_add_aead_dst_ent(req, phys_cpl, qid);
	chcr_add_aead_src_ent(req, ulptx);
	atomic_inc(&adap->chcr_stats.aead_rqst);
	temp = sizeof(struct cpl_rx_phys_dsgl) + dst_size + IV +
		kctx_len + (reqctx->imm ? (req->assoclen + req->cryptlen) : 0);
	create_wreq(a_ctx(tfm), chcr_req, &req->base, reqctx->imm, size,
		    transhdr_len, temp, reqctx->verify);
	reqctx->skb = skb;
	return skb;

err:
	chcr_aead_common_exit(req);
	return ERR_PTR(error);
}



static int chcr_aead_cra_init(struct crypto_aead *tfm)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));
	struct aead_alg *alg = crypto_aead_alg(tfm);

	aeadctx->sw_cipher = crypto_alloc_aead(alg->base.cra_name, 0,
					       CRYPTO_ALG_NEED_FALLBACK |
					       CRYPTO_ALG_ASYNC);
	if  (IS_ERR(aeadctx->sw_cipher))
		return PTR_ERR(aeadctx->sw_cipher);
	crypto_aead_set_reqsize_dma(
		tfm, max(sizeof(struct chcr_aead_reqctx),
			 sizeof(struct aead_request) +
			 crypto_aead_reqsize(aeadctx->sw_cipher)));
	return chcr_device_init(a_ctx(tfm));
}

static void chcr_aead_cra_exit(struct crypto_aead *tfm)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));

	crypto_free_aead(aeadctx->sw_cipher);
}

static int chcr_authenc_null_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));

	aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NOP;
	aeadctx->mayverify = VERIFY_HW;
	return crypto_aead_setauthsize(aeadctx->sw_cipher, authsize);
}
static int chcr_authenc_setauthsize(struct crypto_aead *tfm,
				    unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));
	u32 maxauth = crypto_aead_maxauthsize(tfm);

	/*SHA1 authsize in ipsec is 12 instead of 10 i.e maxauthsize / 2 is not
	 * true for sha1. authsize == 12 condition should be before
	 * authsize == (maxauth >> 1)
	 */
	if (authsize == ICV_4) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL1;
		aeadctx->mayverify = VERIFY_HW;
	} else if (authsize == ICV_6) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL2;
		aeadctx->mayverify = VERIFY_HW;
	} else if (authsize == ICV_10) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_TRUNC_RFC4366;
		aeadctx->mayverify = VERIFY_HW;
	} else if (authsize == ICV_12) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT;
		aeadctx->mayverify = VERIFY_HW;
	} else if (authsize == ICV_14) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL3;
		aeadctx->mayverify = VERIFY_HW;
	} else if (authsize == (maxauth >> 1)) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_DIV2;
		aeadctx->mayverify = VERIFY_HW;
	} else if (authsize == maxauth) {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		aeadctx->mayverify = VERIFY_HW;
	} else {
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		aeadctx->mayverify = VERIFY_SW;
	}
	return crypto_aead_setauthsize(aeadctx->sw_cipher, authsize);
}


static int chcr_gcm_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));

	switch (authsize) {
	case ICV_4:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL1;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_8:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_DIV2;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_12:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_14:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL3;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_16:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_13:
	case ICV_15:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		aeadctx->mayverify = VERIFY_SW;
		break;
	default:
		return -EINVAL;
	}
	return crypto_aead_setauthsize(aeadctx->sw_cipher, authsize);
}

static int chcr_4106_4309_setauthsize(struct crypto_aead *tfm,
					  unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));

	switch (authsize) {
	case ICV_8:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_DIV2;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_12:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_16:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		aeadctx->mayverify = VERIFY_HW;
		break;
	default:
		return -EINVAL;
	}
	return crypto_aead_setauthsize(aeadctx->sw_cipher, authsize);
}

static int chcr_ccm_setauthsize(struct crypto_aead *tfm,
				unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(tfm));

	switch (authsize) {
	case ICV_4:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL1;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_6:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL2;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_8:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_DIV2;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_10:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_TRUNC_RFC4366;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_12:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_14:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_PL3;
		aeadctx->mayverify = VERIFY_HW;
		break;
	case ICV_16:
		aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		aeadctx->mayverify = VERIFY_HW;
		break;
	default:
		return -EINVAL;
	}
	return crypto_aead_setauthsize(aeadctx->sw_cipher, authsize);
}

static int chcr_ccm_common_setkey(struct crypto_aead *aead,
				const u8 *key,
				unsigned int keylen)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(aead));
	unsigned char ck_size, mk_size;
	int key_ctx_size = 0;

	key_ctx_size = sizeof(struct _key_ctx) + roundup(keylen, 16) * 2;
	if (keylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
		mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_128;
	} else if (keylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
		mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_192;
	} else if (keylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
		mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_256;
	} else {
		aeadctx->enckey_len = 0;
		return	-EINVAL;
	}
	aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, mk_size, 0, 0,
						key_ctx_size >> 4);
	memcpy(aeadctx->key, key, keylen);
	aeadctx->enckey_len = keylen;

	return 0;
}

static int chcr_aead_ccm_setkey(struct crypto_aead *aead,
				const u8 *key,
				unsigned int keylen)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(aead));
	int error;

	crypto_aead_clear_flags(aeadctx->sw_cipher, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(aeadctx->sw_cipher, crypto_aead_get_flags(aead) &
			      CRYPTO_TFM_REQ_MASK);
	error = crypto_aead_setkey(aeadctx->sw_cipher, key, keylen);
	if (error)
		return error;
	return chcr_ccm_common_setkey(aead, key, keylen);
}

static int chcr_aead_rfc4309_setkey(struct crypto_aead *aead, const u8 *key,
				    unsigned int keylen)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(aead));
	int error;

	if (keylen < 3) {
		aeadctx->enckey_len = 0;
		return	-EINVAL;
	}
	crypto_aead_clear_flags(aeadctx->sw_cipher, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(aeadctx->sw_cipher, crypto_aead_get_flags(aead) &
			      CRYPTO_TFM_REQ_MASK);
	error = crypto_aead_setkey(aeadctx->sw_cipher, key, keylen);
	if (error)
		return error;
	keylen -= 3;
	memcpy(aeadctx->salt, key + keylen, 3);
	return chcr_ccm_common_setkey(aead, key, keylen);
}

static int chcr_gcm_setkey(struct crypto_aead *aead, const u8 *key,
			   unsigned int keylen)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(aead));
	struct chcr_gcm_ctx *gctx = GCM_CTX(aeadctx);
	unsigned int ck_size;
	int ret = 0, key_ctx_size = 0;
	struct crypto_aes_ctx aes;

	aeadctx->enckey_len = 0;
	crypto_aead_clear_flags(aeadctx->sw_cipher, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(aeadctx->sw_cipher, crypto_aead_get_flags(aead)
			      & CRYPTO_TFM_REQ_MASK);
	ret = crypto_aead_setkey(aeadctx->sw_cipher, key, keylen);
	if (ret)
		goto out;

	if (get_aead_subtype(aead) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106 &&
	    keylen > 3) {
		keylen -= 4;  /* nonce/salt is present in the last 4 bytes */
		memcpy(aeadctx->salt, key + keylen, 4);
	}
	if (keylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		pr_err("GCM: Invalid key length %d\n", keylen);
		ret = -EINVAL;
		goto out;
	}

	memcpy(aeadctx->key, key, keylen);
	aeadctx->enckey_len = keylen;
	key_ctx_size = sizeof(struct _key_ctx) + roundup(keylen, 16) +
		AEAD_H_SIZE;
	aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size,
						CHCR_KEYCTX_MAC_KEY_SIZE_128,
						0, 0,
						key_ctx_size >> 4);
	/* Calculate the H = CIPH(K, 0 repeated 16 times).
	 * It will go in key context
	 */
	ret = aes_expandkey(&aes, key, keylen);
	if (ret) {
		aeadctx->enckey_len = 0;
		goto out;
	}
	memset(gctx->ghash_h, 0, AEAD_H_SIZE);
	aes_encrypt(&aes, gctx->ghash_h, gctx->ghash_h);
	memzero_explicit(&aes, sizeof(aes));

out:
	return ret;
}

static int chcr_authenc_setkey(struct crypto_aead *authenc, const u8 *key,
				   unsigned int keylen)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(authenc));
	struct chcr_authenc_ctx *actx = AUTHENC_CTX(aeadctx);
	/* it contains auth and cipher key both*/
	struct crypto_authenc_keys keys;
	unsigned int subtype;
	unsigned int max_authsize = crypto_aead_alg(authenc)->maxauthsize;
	int err = 0, key_ctx_len = 0;
	unsigned char ck_size = 0;
	struct algo_param param;
	int align;

	crypto_aead_clear_flags(aeadctx->sw_cipher, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(aeadctx->sw_cipher, crypto_aead_get_flags(authenc)
			      & CRYPTO_TFM_REQ_MASK);
	err = crypto_aead_setkey(aeadctx->sw_cipher, key, keylen);
	if (err)
		goto out;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto out;

	if (get_alg_config(&param, max_authsize)) {
		pr_err("Unsupported digest size\n");
		goto out;
	}
	subtype = get_aead_subtype(authenc);
	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR_SHA ||
		subtype == CRYPTO_ALG_SUB_TYPE_CTR_NULL) {
		if (keys.enckeylen < CTR_RFC3686_NONCE_SIZE)
			goto out;
		memcpy(aeadctx->nonce, keys.enckey + (keys.enckeylen
		- CTR_RFC3686_NONCE_SIZE), CTR_RFC3686_NONCE_SIZE);
		keys.enckeylen -= CTR_RFC3686_NONCE_SIZE;
	}
	if (keys.enckeylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keys.enckeylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keys.enckeylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		pr_err("Unsupported cipher key\n");
		goto out;
	}

	/* Copy only encryption key. We use authkey to generate h(ipad) and
	 * h(opad) so authkey is not needed again. authkeylen size have the
	 * size of the hash digest size.
	 */
	memcpy(aeadctx->key, keys.enckey, keys.enckeylen);
	aeadctx->enckey_len = keys.enckeylen;
	if (subtype == CRYPTO_ALG_SUB_TYPE_CBC_SHA ||
		subtype == CRYPTO_ALG_SUB_TYPE_CBC_NULL) {

		get_aes_decrypt_key(actx->dec_rrkey, aeadctx->key,
			    aeadctx->enckey_len << 3);
	}

	align = KEYCTX_ALIGN_PAD(max_authsize);
	err = chcr_prepare_hmac_key(keys.authkey, keys.authkeylen, max_authsize,
				    actx->h_iopad,
				    actx->h_iopad + param.result_size + align);
	if (err)
		goto out;

	key_ctx_len = sizeof(struct _key_ctx) + roundup(keys.enckeylen, 16) +
		      (param.result_size + align) * 2;
	aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, param.mk_size, 0, 1,
						key_ctx_len >> 4);
	actx->auth_mode = param.auth_mode;

	memzero_explicit(&keys, sizeof(keys));
	return 0;

out:
	aeadctx->enckey_len = 0;
	memzero_explicit(&keys, sizeof(keys));
	return -EINVAL;
}

static int chcr_aead_digest_null_setkey(struct crypto_aead *authenc,
					const u8 *key, unsigned int keylen)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(a_ctx(authenc));
	struct chcr_authenc_ctx *actx = AUTHENC_CTX(aeadctx);
	struct crypto_authenc_keys keys;
	int err;
	/* it contains auth and cipher key both*/
	unsigned int subtype;
	int key_ctx_len = 0;
	unsigned char ck_size = 0;

	crypto_aead_clear_flags(aeadctx->sw_cipher, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(aeadctx->sw_cipher, crypto_aead_get_flags(authenc)
			      & CRYPTO_TFM_REQ_MASK);
	err = crypto_aead_setkey(aeadctx->sw_cipher, key, keylen);
	if (err)
		goto out;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto out;

	subtype = get_aead_subtype(authenc);
	if (subtype == CRYPTO_ALG_SUB_TYPE_CTR_SHA ||
	    subtype == CRYPTO_ALG_SUB_TYPE_CTR_NULL) {
		if (keys.enckeylen < CTR_RFC3686_NONCE_SIZE)
			goto out;
		memcpy(aeadctx->nonce, keys.enckey + (keys.enckeylen
			- CTR_RFC3686_NONCE_SIZE), CTR_RFC3686_NONCE_SIZE);
		keys.enckeylen -= CTR_RFC3686_NONCE_SIZE;
	}
	if (keys.enckeylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keys.enckeylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keys.enckeylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		pr_err("Unsupported cipher key %d\n", keys.enckeylen);
		goto out;
	}
	memcpy(aeadctx->key, keys.enckey, keys.enckeylen);
	aeadctx->enckey_len = keys.enckeylen;
	if (subtype == CRYPTO_ALG_SUB_TYPE_CBC_SHA ||
	    subtype == CRYPTO_ALG_SUB_TYPE_CBC_NULL) {
		get_aes_decrypt_key(actx->dec_rrkey, aeadctx->key,
				aeadctx->enckey_len << 3);
	}
	key_ctx_len =  sizeof(struct _key_ctx) + roundup(keys.enckeylen, 16);

	aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, CHCR_KEYCTX_NO_KEY, 0,
						0, key_ctx_len >> 4);
	actx->auth_mode = CHCR_SCMD_AUTH_MODE_NOP;
	memzero_explicit(&keys, sizeof(keys));
	return 0;
out:
	aeadctx->enckey_len = 0;
	memzero_explicit(&keys, sizeof(keys));
	return -EINVAL;
}

static int chcr_aead_op(struct aead_request *req,
			int size,
			create_wr_t create_wr_fn)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct chcr_context *ctx = a_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;
	struct chcr_dev *cdev;

	cdev = a_ctx(tfm)->dev;
	if (!cdev) {
		pr_err("%s : No crypto device.\n", __func__);
		return -ENXIO;
	}

	if (chcr_inc_wrcount(cdev)) {
	/* Detach state for CHCR means lldi or padap is freed.
	 * We cannot increment fallback here.
	 */
		return chcr_aead_fallback(req, reqctx->op);
	}

	if (cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					reqctx->txqidx) &&
		(!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))) {
			chcr_dec_wrcount(cdev);
			return -ENOSPC;
	}

	if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106 &&
	    crypto_ipsec_check_assoclen(req->assoclen) != 0) {
		pr_err("RFC4106: Invalid value of assoclen %d\n",
		       req->assoclen);
		return -EINVAL;
	}

	/* Form a WR from req */
	skb = create_wr_fn(req, u_ctx->lldi.rxq_ids[reqctx->rxqidx], size);

	if (IS_ERR_OR_NULL(skb)) {
		chcr_dec_wrcount(cdev);
		return PTR_ERR_OR_ZERO(skb);
	}

	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, reqctx->txqidx);
	chcr_send_wr(skb);
	return -EINPROGRESS;
}

static int chcr_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	struct chcr_context *ctx = a_ctx(tfm);
	unsigned int cpu;

	cpu = get_cpu();
	reqctx->txqidx = cpu % ctx->ntxq;
	reqctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	reqctx->verify = VERIFY_HW;
	reqctx->op = CHCR_ENCRYPT_OP;

	switch (get_aead_subtype(tfm)) {
	case CRYPTO_ALG_SUB_TYPE_CTR_SHA:
	case CRYPTO_ALG_SUB_TYPE_CBC_SHA:
	case CRYPTO_ALG_SUB_TYPE_CBC_NULL:
	case CRYPTO_ALG_SUB_TYPE_CTR_NULL:
		return chcr_aead_op(req, 0, create_authenc_wr);
	case CRYPTO_ALG_SUB_TYPE_AEAD_CCM:
	case CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309:
		return chcr_aead_op(req, 0, create_aead_ccm_wr);
	default:
		return chcr_aead_op(req, 0, create_gcm_wr);
	}
}

static int chcr_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = a_ctx(tfm);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx_dma(req);
	int size;
	unsigned int cpu;

	cpu = get_cpu();
	reqctx->txqidx = cpu % ctx->ntxq;
	reqctx->rxqidx = cpu % ctx->nrxq;
	put_cpu();

	if (aeadctx->mayverify == VERIFY_SW) {
		size = crypto_aead_maxauthsize(tfm);
		reqctx->verify = VERIFY_SW;
	} else {
		size = 0;
		reqctx->verify = VERIFY_HW;
	}
	reqctx->op = CHCR_DECRYPT_OP;
	switch (get_aead_subtype(tfm)) {
	case CRYPTO_ALG_SUB_TYPE_CBC_SHA:
	case CRYPTO_ALG_SUB_TYPE_CTR_SHA:
	case CRYPTO_ALG_SUB_TYPE_CBC_NULL:
	case CRYPTO_ALG_SUB_TYPE_CTR_NULL:
		return chcr_aead_op(req, size, create_authenc_wr);
	case CRYPTO_ALG_SUB_TYPE_AEAD_CCM:
	case CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309:
		return chcr_aead_op(req, size, create_aead_ccm_wr);
	default:
		return chcr_aead_op(req, size, create_gcm_wr);
	}
}

static struct chcr_alg_template driver_algs[] = {
	/* AES-CBC */
	{
		.type = CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_SUB_TYPE_CBC,
		.is_registered = 0,
		.alg.skcipher = {
			.base.cra_name		= "cbc(aes)",
			.base.cra_driver_name	= "cbc-aes-chcr",
			.base.cra_blocksize	= AES_BLOCK_SIZE,

			.init			= chcr_init_tfm,
			.exit			= chcr_exit_tfm,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.setkey			= chcr_aes_cbc_setkey,
			.encrypt		= chcr_aes_encrypt,
			.decrypt		= chcr_aes_decrypt,
			}
	},
	{
		.type = CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_SUB_TYPE_XTS,
		.is_registered = 0,
		.alg.skcipher = {
			.base.cra_name		= "xts(aes)",
			.base.cra_driver_name	= "xts-aes-chcr",
			.base.cra_blocksize	= AES_BLOCK_SIZE,

			.init			= chcr_init_tfm,
			.exit			= chcr_exit_tfm,
			.min_keysize		= 2 * AES_MIN_KEY_SIZE,
			.max_keysize		= 2 * AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.setkey			= chcr_aes_xts_setkey,
			.encrypt		= chcr_aes_encrypt,
			.decrypt		= chcr_aes_decrypt,
			}
	},
	{
		.type = CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_SUB_TYPE_CTR,
		.is_registered = 0,
		.alg.skcipher = {
			.base.cra_name		= "ctr(aes)",
			.base.cra_driver_name	= "ctr-aes-chcr",
			.base.cra_blocksize	= 1,

			.init			= chcr_init_tfm,
			.exit			= chcr_exit_tfm,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.setkey			= chcr_aes_ctr_setkey,
			.encrypt		= chcr_aes_encrypt,
			.decrypt		= chcr_aes_decrypt,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_SKCIPHER |
			CRYPTO_ALG_SUB_TYPE_CTR_RFC3686,
		.is_registered = 0,
		.alg.skcipher = {
			.base.cra_name		= "rfc3686(ctr(aes))",
			.base.cra_driver_name	= "rfc3686-ctr-aes-chcr",
			.base.cra_blocksize	= 1,

			.init			= chcr_rfc3686_init,
			.exit			= chcr_exit_tfm,
			.min_keysize		= AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
			.ivsize			= CTR_RFC3686_IV_SIZE,
			.setkey			= chcr_aes_rfc3686_setkey,
			.encrypt		= chcr_aes_encrypt,
			.decrypt		= chcr_aes_decrypt,
		}
	},
	/* SHA */
	{
		.type = CRYPTO_ALG_TYPE_AHASH,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA1_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-chcr",
				.cra_blocksize = SHA1_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AHASH,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA256_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "sha256",
				.cra_driver_name = "sha256-chcr",
				.cra_blocksize = SHA256_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AHASH,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "sha224",
				.cra_driver_name = "sha224-chcr",
				.cra_blocksize = SHA224_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AHASH,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA384_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "sha384",
				.cra_driver_name = "sha384-chcr",
				.cra_blocksize = SHA384_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AHASH,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA512_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "sha512",
				.cra_driver_name = "sha512-chcr",
				.cra_blocksize = SHA512_BLOCK_SIZE,
			}
		}
	},
	/* HMAC */
	{
		.type = CRYPTO_ALG_TYPE_HMAC,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA1_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "hmac(sha1)",
				.cra_driver_name = "hmac-sha1-chcr",
				.cra_blocksize = SHA1_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_HMAC,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "hmac(sha224)",
				.cra_driver_name = "hmac-sha224-chcr",
				.cra_blocksize = SHA224_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_HMAC,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA256_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "hmac(sha256)",
				.cra_driver_name = "hmac-sha256-chcr",
				.cra_blocksize = SHA256_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_HMAC,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA384_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "hmac(sha384)",
				.cra_driver_name = "hmac-sha384-chcr",
				.cra_blocksize = SHA384_BLOCK_SIZE,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_HMAC,
		.is_registered = 0,
		.alg.hash = {
			.halg.digestsize = SHA512_DIGEST_SIZE,
			.halg.base = {
				.cra_name = "hmac(sha512)",
				.cra_driver_name = "hmac-sha512-chcr",
				.cra_blocksize = SHA512_BLOCK_SIZE,
			}
		}
	},
	/* Add AEAD Algorithms */
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_GCM,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "gcm(aes)",
				.cra_driver_name = "gcm-aes-chcr",
				.cra_blocksize	= 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_gcm_ctx),
			},
			.ivsize = GCM_AES_IV_SIZE,
			.maxauthsize = GHASH_DIGEST_SIZE,
			.setkey = chcr_gcm_setkey,
			.setauthsize = chcr_gcm_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "rfc4106(gcm(aes))",
				.cra_driver_name = "rfc4106-gcm-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY + 1,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_gcm_ctx),

			},
			.ivsize = GCM_RFC4106_IV_SIZE,
			.maxauthsize	= GHASH_DIGEST_SIZE,
			.setkey = chcr_gcm_setkey,
			.setauthsize	= chcr_4106_4309_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_CCM,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "ccm(aes)",
				.cra_driver_name = "ccm-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx),

			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize	= GHASH_DIGEST_SIZE,
			.setkey = chcr_aead_ccm_setkey,
			.setauthsize	= chcr_ccm_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "rfc4309(ccm(aes))",
				.cra_driver_name = "rfc4309-ccm-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY + 1,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx),

			},
			.ivsize = 8,
			.maxauthsize	= GHASH_DIGEST_SIZE,
			.setkey = chcr_aead_rfc4309_setkey,
			.setauthsize = chcr_4106_4309_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CBC_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha1-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CBC_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {

				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha256-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize	= SHA256_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CBC_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha224-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CBC_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha384-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CBC_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha512-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CBC_NULL,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(digest_null,cbc(aes))",
				.cra_driver_name =
					"authenc-digest_null-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize  = AES_BLOCK_SIZE,
			.maxauthsize = 0,
			.setkey  = chcr_aead_digest_null_setkey,
			.setauthsize = chcr_authenc_null_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CTR_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),rfc3686(ctr(aes)))",
				.cra_driver_name =
				"authenc-hmac-sha1-rfc3686-ctr-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CTR_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {

				.cra_name = "authenc(hmac(sha256),rfc3686(ctr(aes)))",
				.cra_driver_name =
				"authenc-hmac-sha256-rfc3686-ctr-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize	= SHA256_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CTR_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),rfc3686(ctr(aes)))",
				.cra_driver_name =
				"authenc-hmac-sha224-rfc3686-ctr-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),
			},
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CTR_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),rfc3686(ctr(aes)))",
				.cra_driver_name =
				"authenc-hmac-sha384-rfc3686-ctr-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CTR_SHA,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),rfc3686(ctr(aes)))",
				.cra_driver_name =
				"authenc-hmac-sha512-rfc3686-ctr-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
			.setkey = chcr_authenc_setkey,
			.setauthsize = chcr_authenc_setauthsize,
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_CTR_NULL,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(digest_null,rfc3686(ctr(aes)))",
				.cra_driver_name =
				"authenc-digest_null-rfc3686-ctr-aes-chcr",
				.cra_blocksize	 = 1,
				.cra_priority = CHCR_AEAD_PRIORITY,
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_authenc_ctx),

			},
			.ivsize  = CTR_RFC3686_IV_SIZE,
			.maxauthsize = 0,
			.setkey  = chcr_aead_digest_null_setkey,
			.setauthsize = chcr_authenc_null_setauthsize,
		}
	},
};

/*
 *	chcr_unregister_alg - Deregister crypto algorithms with
 *	kernel framework.
 */
static int chcr_unregister_alg(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		switch (driver_algs[i].type & CRYPTO_ALG_TYPE_MASK) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			if (driver_algs[i].is_registered && refcount_read(
			    &driver_algs[i].alg.skcipher.base.cra_refcnt)
			    == 1) {
				crypto_unregister_skcipher(
						&driver_algs[i].alg.skcipher);
				driver_algs[i].is_registered = 0;
			}
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			if (driver_algs[i].is_registered && refcount_read(
			    &driver_algs[i].alg.aead.base.cra_refcnt) == 1) {
				crypto_unregister_aead(
						&driver_algs[i].alg.aead);
				driver_algs[i].is_registered = 0;
			}
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			if (driver_algs[i].is_registered && refcount_read(
			    &driver_algs[i].alg.hash.halg.base.cra_refcnt)
			    == 1) {
				crypto_unregister_ahash(
						&driver_algs[i].alg.hash);
				driver_algs[i].is_registered = 0;
			}
			break;
		}
	}
	return 0;
}

#define SZ_AHASH_CTX sizeof(struct chcr_context)
#define SZ_AHASH_H_CTX (sizeof(struct chcr_context) + sizeof(struct hmac_ctx))
#define SZ_AHASH_REQ_CTX sizeof(struct chcr_ahash_req_ctx)

/*
 *	chcr_register_alg - Register crypto algorithms with kernel framework.
 */
static int chcr_register_alg(void)
{
	struct crypto_alg ai;
	struct ahash_alg *a_hash;
	int err = 0, i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		if (driver_algs[i].is_registered)
			continue;
		switch (driver_algs[i].type & CRYPTO_ALG_TYPE_MASK) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			driver_algs[i].alg.skcipher.base.cra_priority =
				CHCR_CRA_PRIORITY;
			driver_algs[i].alg.skcipher.base.cra_module = THIS_MODULE;
			driver_algs[i].alg.skcipher.base.cra_flags =
				CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK;
			driver_algs[i].alg.skcipher.base.cra_ctxsize =
				sizeof(struct chcr_context) +
				sizeof(struct ablk_ctx);
			driver_algs[i].alg.skcipher.base.cra_alignmask = 0;

			err = crypto_register_skcipher(&driver_algs[i].alg.skcipher);
			name = driver_algs[i].alg.skcipher.base.cra_driver_name;
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			driver_algs[i].alg.aead.base.cra_flags =
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK |
				CRYPTO_ALG_ALLOCATES_MEMORY;
			driver_algs[i].alg.aead.encrypt = chcr_aead_encrypt;
			driver_algs[i].alg.aead.decrypt = chcr_aead_decrypt;
			driver_algs[i].alg.aead.init = chcr_aead_cra_init;
			driver_algs[i].alg.aead.exit = chcr_aead_cra_exit;
			driver_algs[i].alg.aead.base.cra_module = THIS_MODULE;
			err = crypto_register_aead(&driver_algs[i].alg.aead);
			name = driver_algs[i].alg.aead.base.cra_driver_name;
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			a_hash = &driver_algs[i].alg.hash;
			a_hash->update = chcr_ahash_update;
			a_hash->final = chcr_ahash_final;
			a_hash->finup = chcr_ahash_finup;
			a_hash->digest = chcr_ahash_digest;
			a_hash->export = chcr_ahash_export;
			a_hash->import = chcr_ahash_import;
			a_hash->halg.statesize = SZ_AHASH_REQ_CTX;
			a_hash->halg.base.cra_priority = CHCR_CRA_PRIORITY;
			a_hash->halg.base.cra_module = THIS_MODULE;
			a_hash->halg.base.cra_flags =
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY;
			a_hash->halg.base.cra_alignmask = 0;
			a_hash->halg.base.cra_exit = NULL;

			if (driver_algs[i].type == CRYPTO_ALG_TYPE_HMAC) {
				a_hash->halg.base.cra_init = chcr_hmac_cra_init;
				a_hash->init = chcr_hmac_init;
				a_hash->setkey = chcr_ahash_setkey;
				a_hash->halg.base.cra_ctxsize = SZ_AHASH_H_CTX;
			} else {
				a_hash->init = chcr_sha_init;
				a_hash->halg.base.cra_ctxsize = SZ_AHASH_CTX;
				a_hash->halg.base.cra_init = chcr_sha_cra_init;
			}
			err = crypto_register_ahash(&driver_algs[i].alg.hash);
			ai = driver_algs[i].alg.hash.halg.base;
			name = ai.cra_driver_name;
			break;
		}
		if (err) {
			pr_err("%s : Algorithm registration failed\n", name);
			goto register_err;
		} else {
			driver_algs[i].is_registered = 1;
		}
	}
	return 0;

register_err:
	chcr_unregister_alg();
	return err;
}

/*
 *	start_crypto - Register the crypto algorithms.
 *	This should called once when the first device comesup. After this
 *	kernel will start calling driver APIs for crypto operations.
 */
int start_crypto(void)
{
	return chcr_register_alg();
}

/*
 *	stop_crypto - Deregister all the crypto algorithms with kernel.
 *	This should be called once when the last device goes down. After this
 *	kernel will not call the driver API for crypto operations.
 */
int stop_crypto(void)
{
	chcr_unregister_alg();
	return 0;
}
