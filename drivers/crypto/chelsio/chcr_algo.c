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
#include <linux/cryptohash.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/authenc.h>
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

static inline  struct chcr_aead_ctx *AEAD_CTX(struct chcr_context *ctx)
{
	return ctx->crypto_ctx->aeadctx;
}

static inline struct ablk_ctx *ABLK_CTX(struct chcr_context *ctx)
{
	return ctx->crypto_ctx->ablkctx;
}

static inline struct hmac_ctx *HMAC_CTX(struct chcr_context *ctx)
{
	return ctx->crypto_ctx->hmacctx;
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
	return ctx->dev->u_ctx;
}

static inline int is_ofld_imm(const struct sk_buff *skb)
{
	return (skb->len <= CRYPTO_MAX_IMM_TX_PKT_LEN);
}

/*
 *	sgl_len - calculates the size of an SGL of the given capacity
 *	@n: the number of SGL entries
 *	Calculates the number of flits needed for a scatter/gather list that
 *	can hold the given number of entries.
 */
static inline unsigned int sgl_len(unsigned int n)
{
	n--;
	return (3 * n) / 2 + (n & 1) + 2;
}

static void chcr_verify_tag(struct aead_request *req, u8 *input, int *err)
{
	u8 temp[SHA512_DIGEST_SIZE];
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	int authsize = crypto_aead_authsize(tfm);
	struct cpl_fw6_pld *fw6_pld;
	int cmp = 0;

	fw6_pld = (struct cpl_fw6_pld *)input;
	if ((get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106) ||
	    (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_GCM)) {
		cmp = memcmp(&fw6_pld->data[2], (fw6_pld + 1), authsize);
	} else {

		sg_pcopy_to_buffer(req->src, sg_nents(req->src), temp,
				authsize, req->assoclen +
				req->cryptlen - authsize);
		cmp = memcmp(temp, (fw6_pld + 1), authsize);
	}
	if (cmp)
		*err = -EBADMSG;
	else
		*err = 0;
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
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_req_ctx ctx_req;
	struct cpl_fw6_pld *fw6_pld;
	unsigned int digestsize, updated_digestsize;

	switch (tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AEAD:
		ctx_req.req.aead_req = (struct aead_request *)req;
		ctx_req.ctx.reqctx = aead_request_ctx(ctx_req.req.aead_req);
		dma_unmap_sg(&u_ctx->lldi.pdev->dev, ctx_req.req.aead_req->dst,
			     ctx_req.ctx.reqctx->dst_nents, DMA_FROM_DEVICE);
		if (ctx_req.ctx.reqctx->skb) {
			kfree_skb(ctx_req.ctx.reqctx->skb);
			ctx_req.ctx.reqctx->skb = NULL;
		}
		if (ctx_req.ctx.reqctx->verify == VERIFY_SW) {
			chcr_verify_tag(ctx_req.req.aead_req, input,
					&err);
			ctx_req.ctx.reqctx->verify = VERIFY_HW;
		}
		break;

	case CRYPTO_ALG_TYPE_BLKCIPHER:
		ctx_req.req.ablk_req = (struct ablkcipher_request *)req;
		ctx_req.ctx.ablk_ctx =
			ablkcipher_request_ctx(ctx_req.req.ablk_req);
		if (!err) {
			fw6_pld = (struct cpl_fw6_pld *)input;
			memcpy(ctx_req.req.ablk_req->info, &fw6_pld->data[2],
			       AES_BLOCK_SIZE);
		}
		dma_unmap_sg(&u_ctx->lldi.pdev->dev, ctx_req.req.ablk_req->dst,
			     ctx_req.ctx.ablk_ctx->dst_nents, DMA_FROM_DEVICE);
		if (ctx_req.ctx.ablk_ctx->skb) {
			kfree_skb(ctx_req.ctx.ablk_ctx->skb);
			ctx_req.ctx.ablk_ctx->skb = NULL;
		}
		break;

	case CRYPTO_ALG_TYPE_AHASH:
		ctx_req.req.ahash_req = (struct ahash_request *)req;
		ctx_req.ctx.ahash_ctx =
			ahash_request_ctx(ctx_req.req.ahash_req);
		digestsize =
			crypto_ahash_digestsize(crypto_ahash_reqtfm(
							ctx_req.req.ahash_req));
		updated_digestsize = digestsize;
		if (digestsize == SHA224_DIGEST_SIZE)
			updated_digestsize = SHA256_DIGEST_SIZE;
		else if (digestsize == SHA384_DIGEST_SIZE)
			updated_digestsize = SHA512_DIGEST_SIZE;
		if (ctx_req.ctx.ahash_ctx->skb) {
			kfree_skb(ctx_req.ctx.ahash_ctx->skb);
			ctx_req.ctx.ahash_ctx->skb = NULL;
		}
		if (ctx_req.ctx.ahash_ctx->result == 1) {
			ctx_req.ctx.ahash_ctx->result = 0;
			memcpy(ctx_req.req.ahash_req->result, input +
			       sizeof(struct cpl_fw6_pld),
			       digestsize);
		} else {
			memcpy(ctx_req.ctx.ahash_ctx->partial_hash, input +
			       sizeof(struct cpl_fw6_pld),
			       updated_digestsize);
		}
		break;
	}
	return err;
}

/*
 *	calc_tx_flits_ofld - calculate # of flits for an offload packet
 *	@skb: the packet
 *	Returns the number of flits needed for the given offload packet.
 *	These packets are already fully constructed and no additional headers
 *	will be added.
 */
static inline unsigned int calc_tx_flits_ofld(const struct sk_buff *skb)
{
	unsigned int flits, cnt;

	if (is_ofld_imm(skb))
		return DIV_ROUND_UP(skb->len, 8);

	flits = skb_transport_offset(skb) / 8;   /* headers */
	cnt = skb_shinfo(skb)->nr_frags;
	if (skb_tail_pointer(skb) != skb_transport_header(skb))
		cnt++;
	return flits + sgl_len(cnt);
}

static inline void get_aes_decrypt_key(unsigned char *dec_key,
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
		w_ring[i] = be32_to_cpu(*(u32 *)&key[4 * i]);

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
		*((u32 *)dec_key + k) = htonl(w_ring[j]);
		j--;
		if (j < 0)
			j += nk;
	}
}

static struct crypto_shash *chcr_alloc_shash(unsigned int ds)
{
	struct crypto_shash *base_hash = NULL;

	switch (ds) {
	case SHA1_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha1", 0, 0);
		break;
	case SHA224_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha224", 0, 0);
		break;
	case SHA256_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha256", 0, 0);
		break;
	case SHA384_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha384", 0, 0);
		break;
	case SHA512_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha512", 0, 0);
		break;
	}

	return base_hash;
}

static int chcr_compute_partial_hash(struct shash_desc *desc,
				     char *iopad, char *result_hash,
				     int digest_size)
{
	struct sha1_state sha1_st;
	struct sha256_state sha256_st;
	struct sha512_state sha512_st;
	int error;

	if (digest_size == SHA1_DIGEST_SIZE) {
		error = crypto_shash_init(desc) ?:
			crypto_shash_update(desc, iopad, SHA1_BLOCK_SIZE) ?:
			crypto_shash_export(desc, (void *)&sha1_st);
		memcpy(result_hash, sha1_st.state, SHA1_DIGEST_SIZE);
	} else if (digest_size == SHA224_DIGEST_SIZE) {
		error = crypto_shash_init(desc) ?:
			crypto_shash_update(desc, iopad, SHA256_BLOCK_SIZE) ?:
			crypto_shash_export(desc, (void *)&sha256_st);
		memcpy(result_hash, sha256_st.state, SHA256_DIGEST_SIZE);

	} else if (digest_size == SHA256_DIGEST_SIZE) {
		error = crypto_shash_init(desc) ?:
			crypto_shash_update(desc, iopad, SHA256_BLOCK_SIZE) ?:
			crypto_shash_export(desc, (void *)&sha256_st);
		memcpy(result_hash, sha256_st.state, SHA256_DIGEST_SIZE);

	} else if (digest_size == SHA384_DIGEST_SIZE) {
		error = crypto_shash_init(desc) ?:
			crypto_shash_update(desc, iopad, SHA512_BLOCK_SIZE) ?:
			crypto_shash_export(desc, (void *)&sha512_st);
		memcpy(result_hash, sha512_st.state, SHA512_DIGEST_SIZE);

	} else if (digest_size == SHA512_DIGEST_SIZE) {
		error = crypto_shash_init(desc) ?:
			crypto_shash_update(desc, iopad, SHA512_BLOCK_SIZE) ?:
			crypto_shash_export(desc, (void *)&sha512_st);
		memcpy(result_hash, sha512_st.state, SHA512_DIGEST_SIZE);
	} else {
		error = -EINVAL;
		pr_err("Unknown digest size %d\n", digest_size);
	}
	return error;
}

static void chcr_change_order(char *buf, int ds)
{
	int i;

	if (ds == SHA512_DIGEST_SIZE) {
		for (i = 0; i < (ds / sizeof(u64)); i++)
			*((__be64 *)buf + i) =
				cpu_to_be64(*((u64 *)buf + i));
	} else {
		for (i = 0; i < (ds / sizeof(u32)); i++)
			*((__be32 *)buf + i) =
				cpu_to_be32(*((u32 *)buf + i));
	}
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

static void write_phys_cpl(struct cpl_rx_phys_dsgl *phys_cpl,
			   struct scatterlist *sg,
			   struct phys_sge_parm *sg_param)
{
	struct phys_sge_pairs *to;
	int out_buf_size = sg_param->obsize;
	unsigned int nents = sg_param->nents, i, j = 0;

	phys_cpl->op_to_tid = htonl(CPL_RX_PHYS_DSGL_OPCODE_V(CPL_RX_PHYS_DSGL)
				    | CPL_RX_PHYS_DSGL_ISRDMA_V(0));
	phys_cpl->pcirlxorder_to_noofsgentr =
		htonl(CPL_RX_PHYS_DSGL_PCIRLXORDER_V(0) |
		      CPL_RX_PHYS_DSGL_PCINOSNOOP_V(0) |
		      CPL_RX_PHYS_DSGL_PCITPHNTENB_V(0) |
		      CPL_RX_PHYS_DSGL_PCITPHNT_V(0) |
		      CPL_RX_PHYS_DSGL_DCAID_V(0) |
		      CPL_RX_PHYS_DSGL_NOOFSGENTR_V(nents));
	phys_cpl->rss_hdr_int.opcode = CPL_RX_PHYS_ADDR;
	phys_cpl->rss_hdr_int.qid = htons(sg_param->qid);
	phys_cpl->rss_hdr_int.hash_val = 0;
	to = (struct phys_sge_pairs *)((unsigned char *)phys_cpl +
				       sizeof(struct cpl_rx_phys_dsgl));

	for (i = 0; nents; to++) {
		for (j = 0; j < 8 && nents; j++, nents--) {
			out_buf_size -= sg_dma_len(sg);
			to->len[j] = htons(sg_dma_len(sg));
			to->addr[j] = cpu_to_be64(sg_dma_address(sg));
			sg = sg_next(sg);
		}
	}
	if (out_buf_size) {
		j--;
		to--;
		to->len[j] = htons(ntohs(to->len[j]) + (out_buf_size));
	}
}

static inline int map_writesg_phys_cpl(struct device *dev,
					struct cpl_rx_phys_dsgl *phys_cpl,
					struct scatterlist *sg,
					struct phys_sge_parm *sg_param)
{
	if (!sg || !sg_param->nents)
		return 0;

	sg_param->nents = dma_map_sg(dev, sg, sg_param->nents, DMA_FROM_DEVICE);
	if (sg_param->nents == 0) {
		pr_err("CHCR : DMA mapping failed\n");
		return -EINVAL;
	}
	write_phys_cpl(phys_cpl, sg, sg_param);
	return 0;
}

static inline int get_aead_subtype(struct crypto_aead *aead)
{
	struct aead_alg *alg = crypto_aead_alg(aead);
	struct chcr_alg_template *chcr_crypto_alg =
		container_of(alg, struct chcr_alg_template, alg.aead);
	return chcr_crypto_alg->type & CRYPTO_ALG_SUB_TYPE_MASK;
}

static inline int get_cryptoalg_subtype(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct chcr_alg_template *chcr_crypto_alg =
		container_of(alg, struct chcr_alg_template, alg.crypto);

	return chcr_crypto_alg->type & CRYPTO_ALG_SUB_TYPE_MASK;
}

static inline void write_buffer_to_skb(struct sk_buff *skb,
					unsigned int *frags,
					char *bfr,
					u8 bfr_len)
{
	skb->len += bfr_len;
	skb->data_len += bfr_len;
	skb->truesize += bfr_len;
	get_page(virt_to_page(bfr));
	skb_fill_page_desc(skb, *frags, virt_to_page(bfr),
			   offset_in_page(bfr), bfr_len);
	(*frags)++;
}


static inline void
write_sg_to_skb(struct sk_buff *skb, unsigned int *frags,
			struct scatterlist *sg, unsigned int count)
{
	struct page *spage;
	unsigned int page_len;

	skb->len += count;
	skb->data_len += count;
	skb->truesize += count;

	while (count > 0) {
		if (!sg || (!(sg->length)))
			break;
		spage = sg_page(sg);
		get_page(spage);
		page_len = min(sg->length, count);
		skb_fill_page_desc(skb, *frags, spage, sg->offset, page_len);
		(*frags)++;
		count -= page_len;
		sg = sg_next(sg);
	}
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

static inline void create_wreq(struct chcr_context *ctx,
			       struct chcr_wr *chcr_req,
			       void *req, struct sk_buff *skb,
			       int kctx_len, int hash_sz,
			       int is_iv,
			       unsigned int sc_len)
{
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	int iv_loc = IV_DSGL;
	int qid = u_ctx->lldi.rxq_ids[ctx->tx_channel_id];
	unsigned int immdatalen = 0, nr_frags = 0;

	if (is_ofld_imm(skb)) {
		immdatalen = skb->data_len;
		iv_loc = IV_IMMEDIATE;
	} else {
		nr_frags = skb_shinfo(skb)->nr_frags;
	}

	chcr_req->wreq.op_to_cctx_size = FILL_WR_OP_CCTX_SIZE(immdatalen,
				((sizeof(chcr_req->key_ctx) + kctx_len) >> 4));
	chcr_req->wreq.pld_size_hash_size =
		htonl(FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE_V(sgl_lengths[nr_frags]) |
		      FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE_V(hash_sz));
	chcr_req->wreq.len16_pkd =
		htonl(FW_CRYPTO_LOOKASIDE_WR_LEN16_V(DIV_ROUND_UP(
				    (calc_tx_flits_ofld(skb) * 8), 16)));
	chcr_req->wreq.cookie = cpu_to_be64((uintptr_t)req);
	chcr_req->wreq.rx_chid_to_rx_q_id =
		FILL_WR_RX_Q_ID(ctx->dev->tx_channel_id, qid,
				is_iv ? iv_loc : IV_NOP);

	chcr_req->ulptx.cmd_dest = FILL_ULPTX_CMD_DEST(ctx->dev->tx_channel_id);
	chcr_req->ulptx.len = htonl((DIV_ROUND_UP((calc_tx_flits_ofld(skb) * 8),
					16) - ((sizeof(chcr_req->wreq)) >> 4)));

	chcr_req->sc_imm.cmd_more = FILL_CMD_MORE(immdatalen);
	chcr_req->sc_imm.len = cpu_to_be32(sizeof(struct cpl_tx_sec_pdu) +
				   sizeof(chcr_req->key_ctx) +
				   kctx_len + sc_len + immdatalen);
}

/**
 *	create_cipher_wr - form the WR for cipher operations
 *	@req: cipher req.
 *	@ctx: crypto driver context of the request.
 *	@qid: ingress qid where response of this WR should be received.
 *	@op_type:	encryption or decryption
 */
static struct sk_buff
*create_cipher_wr(struct ablkcipher_request *req,
		  unsigned short qid,
		  unsigned short op_type)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct chcr_context *ctx = crypto_ablkcipher_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct chcr_blkcipher_req_ctx *reqctx = ablkcipher_request_ctx(req);
	struct phys_sge_parm sg_param;
	unsigned int frags = 0, transhdr_len, phys_dsgl;
	unsigned int ivsize = crypto_ablkcipher_ivsize(tfm), kctx_len;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
			GFP_ATOMIC;

	if (!req->info)
		return ERR_PTR(-EINVAL);
	reqctx->dst_nents = sg_nents_for_len(req->dst, req->nbytes);
	if (reqctx->dst_nents <= 0) {
		pr_err("AES:Invalid Destination sg lists\n");
		return ERR_PTR(-EINVAL);
	}
	if ((ablkctx->enckey_len == 0) || (ivsize > AES_BLOCK_SIZE) ||
	    (req->nbytes <= 0) || (req->nbytes % AES_BLOCK_SIZE)) {
		pr_err("AES: Invalid value of Key Len %d nbytes %d IV Len %d\n",
		       ablkctx->enckey_len, req->nbytes, ivsize);
		return ERR_PTR(-EINVAL);
	}

	phys_dsgl = get_space_for_phys_dsgl(reqctx->dst_nents);

	kctx_len = (DIV_ROUND_UP(ablkctx->enckey_len, 16) * 16);
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, phys_dsgl);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)), flags);
	if (!skb)
		return ERR_PTR(-ENOMEM);
	skb_reserve(skb, sizeof(struct sge_opaque_hdr));
	chcr_req = (struct chcr_wr *)__skb_put(skb, transhdr_len);
	memset(chcr_req, 0, transhdr_len);
	chcr_req->sec_cpl.op_ivinsrtofst =
		FILL_SEC_CPL_OP_IVINSR(ctx->dev->tx_channel_id, 2, 1);

	chcr_req->sec_cpl.pldlen = htonl(ivsize + req->nbytes);
	chcr_req->sec_cpl.aadstart_cipherstop_hi =
			FILL_SEC_CPL_CIPHERSTOP_HI(0, 0, ivsize + 1, 0);

	chcr_req->sec_cpl.cipherstop_lo_authinsert =
			FILL_SEC_CPL_AUTHINSERT(0, 0, 0, 0);
	chcr_req->sec_cpl.seqno_numivs = FILL_SEC_CPL_SCMD0_SEQNO(op_type, 0,
							 ablkctx->ciph_mode,
							 0, 0, ivsize >> 1);
	chcr_req->sec_cpl.ivgen_hdrlen = FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 0,
							  0, 1, phys_dsgl);

	chcr_req->key_ctx.ctx_hdr = ablkctx->key_ctx_hdr;
	if (op_type == CHCR_DECRYPT_OP) {
		generate_copy_rrkey(ablkctx, &chcr_req->key_ctx);
	} else {
		if (ablkctx->ciph_mode == CHCR_SCMD_CIPHER_MODE_AES_CBC) {
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
	sg_param.nents = reqctx->dst_nents;
	sg_param.obsize = req->nbytes;
	sg_param.qid = qid;
	sg_param.align = 1;
	if (map_writesg_phys_cpl(&u_ctx->lldi.pdev->dev, phys_cpl, req->dst,
				 &sg_param))
		goto map_fail1;

	skb_set_transport_header(skb, transhdr_len);
	memcpy(reqctx->iv, req->info, ivsize);
	write_buffer_to_skb(skb, &frags, reqctx->iv, ivsize);
	write_sg_to_skb(skb, &frags, req->src, req->nbytes);
	create_wreq(ctx, chcr_req, req, skb, kctx_len, 0, 1,
			sizeof(struct cpl_rx_phys_dsgl) + phys_dsgl);
	reqctx->skb = skb;
	skb_get(skb);
	return skb;
map_fail1:
	kfree_skb(skb);
	return ERR_PTR(-ENOMEM);
}

static int chcr_aes_cbc_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			       unsigned int keylen)
{
	struct chcr_context *ctx = crypto_ablkcipher_ctx(tfm);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);
	unsigned int ck_size, context_size;
	u16 alignment = 0;

	if (keylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keylen == AES_KEYSIZE_192) {
		alignment = 8;
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		goto badkey_err;
	}
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
	crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	ablkctx->enckey_len = 0;
	return -EINVAL;
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

static int chcr_aes_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct chcr_context *ctx = crypto_ablkcipher_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	skb = create_cipher_wr(req, u_ctx->lldi.rxq_ids[ctx->tx_channel_id],
			       CHCR_ENCRYPT_OP);
	if (IS_ERR(skb)) {
		pr_err("chcr : %s : Failed to form WR. No memory\n", __func__);
		return  PTR_ERR(skb);
	}
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);
	return -EINPROGRESS;
}

static int chcr_aes_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct chcr_context *ctx = crypto_ablkcipher_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	skb = create_cipher_wr(req, u_ctx->lldi.rxq_ids[0],
			       CHCR_DECRYPT_OP);
	if (IS_ERR(skb)) {
		pr_err("chcr : %s : Failed to form WR. No memory\n", __func__);
		return PTR_ERR(skb);
	}
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);
	return -EINPROGRESS;
}

static int chcr_device_init(struct chcr_context *ctx)
{
	struct uld_ctx *u_ctx;
	unsigned int id;
	int err = 0, rxq_perchan, rxq_idx;

	id = smp_processor_id();
	if (!ctx->dev) {
		err = assign_chcr_device(&ctx->dev);
		if (err) {
			pr_err("chcr device assignment fails\n");
			goto out;
		}
		u_ctx = ULD_CTX(ctx);
		rxq_perchan = u_ctx->lldi.nrxq / u_ctx->lldi.nchan;
		rxq_idx = ctx->dev->tx_channel_id * rxq_perchan;
		rxq_idx += id % rxq_perchan;
		spin_lock(&ctx->dev->lock_chcr_dev);
		ctx->tx_channel_id = rxq_idx;
		ctx->dev->tx_channel_id = !ctx->dev->tx_channel_id;
		spin_unlock(&ctx->dev->lock_chcr_dev);
	}
out:
	return err;
}

static int chcr_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize =  sizeof(struct chcr_blkcipher_req_ctx);
	return chcr_device_init(crypto_tfm_ctx(tfm));
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
		pr_err("chcr : ERROR, unsupported digest size\n");
		return -EINVAL;
	}
	return 0;
}

static inline void chcr_free_shash(struct crypto_shash *base_hash)
{
		crypto_free_shash(base_hash);
}

/**
 *	create_hash_wr - Create hash work request
 *	@req - Cipher req base
 */
static struct sk_buff *create_hash_wr(struct ahash_request *req,
				      struct hash_wr_param *param)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	unsigned int frags = 0, transhdr_len, iopad_alignment = 0;
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int kctx_len = 0;
	u8 hash_size_in_response = 0;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;

	iopad_alignment = KEYCTX_ALIGN_PAD(digestsize);
	kctx_len = param->alg_prm.result_size + iopad_alignment;
	if (param->opad_needed)
		kctx_len += param->alg_prm.result_size + iopad_alignment;

	if (req_ctx->result)
		hash_size_in_response = digestsize;
	else
		hash_size_in_response = param->alg_prm.result_size;
	transhdr_len = HASH_TRANSHDR_SIZE(kctx_len);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)), flags);
	if (!skb)
		return skb;

	skb_reserve(skb, sizeof(struct sge_opaque_hdr));
	chcr_req = (struct chcr_wr *)__skb_put(skb, transhdr_len);
	memset(chcr_req, 0, transhdr_len);

	chcr_req->sec_cpl.op_ivinsrtofst =
		FILL_SEC_CPL_OP_IVINSR(ctx->dev->tx_channel_id, 2, 0);
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
					    ((kctx_len +
					     sizeof(chcr_req->key_ctx)) >> 4));
	chcr_req->sec_cpl.scmd1 = cpu_to_be64((u64)param->scmd1);

	skb_set_transport_header(skb, transhdr_len);
	if (param->bfr_len != 0)
		write_buffer_to_skb(skb, &frags, req_ctx->reqbfr,
				    param->bfr_len);
	if (param->sg_len != 0)
		write_sg_to_skb(skb, &frags, req->src, param->sg_len);

	create_wreq(ctx, chcr_req, req, skb, kctx_len, hash_size_in_response, 0,
			DUMMY_BYTES);
	req_ctx->skb = skb;
	skb_get(skb);
	return skb;
}

static int chcr_ahash_update(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(rtfm));
	struct uld_ctx *u_ctx = NULL;
	struct sk_buff *skb;
	u8 remainder = 0, bs;
	unsigned int nbytes = req->nbytes;
	struct hash_wr_param params;

	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));

	u_ctx = ULD_CTX(ctx);
	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	if (nbytes + req_ctx->reqlen >= bs) {
		remainder = (nbytes + req_ctx->reqlen) % bs;
		nbytes = nbytes + req_ctx->reqlen - remainder;
	} else {
		sg_pcopy_to_buffer(req->src, sg_nents(req->src), req_ctx->reqbfr
				   + req_ctx->reqlen, nbytes, 0);
		req_ctx->reqlen += nbytes;
		return 0;
	}

	params.opad_needed = 0;
	params.more = 1;
	params.last = 0;
	params.sg_len = nbytes - req_ctx->reqlen;
	params.bfr_len = req_ctx->reqlen;
	params.scmd1 = 0;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	req_ctx->result = 0;
	req_ctx->data_len += params.sg_len + params.bfr_len;
	skb = create_hash_wr(req, &params);
	if (!skb)
		return -ENOMEM;

	if (remainder) {
		u8 *temp;
		/* Swap buffers */
		temp = req_ctx->reqbfr;
		req_ctx->reqbfr = req_ctx->skbfr;
		req_ctx->skbfr = temp;
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   req_ctx->reqbfr, remainder, req->nbytes -
				   remainder);
	}
	req_ctx->reqlen = remainder;
	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);

	return -EINPROGRESS;
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
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(rtfm));
	struct hash_wr_param params;
	struct sk_buff *skb;
	struct uld_ctx *u_ctx = NULL;
	u8 bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));

	u_ctx = ULD_CTX(ctx);
	if (is_hmac(crypto_ahash_tfm(rtfm)))
		params.opad_needed = 1;
	else
		params.opad_needed = 0;
	params.sg_len = 0;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	req_ctx->result = 1;
	params.bfr_len = req_ctx->reqlen;
	req_ctx->data_len += params.bfr_len + params.sg_len;
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
	skb = create_hash_wr(req, &params);
	if (!skb)
		return -ENOMEM;

	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);
	return -EINPROGRESS;
}

static int chcr_ahash_finup(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(rtfm));
	struct uld_ctx *u_ctx = NULL;
	struct sk_buff *skb;
	struct hash_wr_param params;
	u8  bs;

	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));
	u_ctx = ULD_CTX(ctx);

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	if (is_hmac(crypto_ahash_tfm(rtfm)))
		params.opad_needed = 1;
	else
		params.opad_needed = 0;

	params.sg_len = req->nbytes;
	params.bfr_len = req_ctx->reqlen;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	req_ctx->data_len += params.bfr_len + params.sg_len;
	req_ctx->result = 1;
	if ((req_ctx->reqlen + req->nbytes) == 0) {
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

	skb = create_hash_wr(req, &params);
	if (!skb)
		return -ENOMEM;

	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);

	return -EINPROGRESS;
}

static int chcr_ahash_digest(struct ahash_request *req)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *rtfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(rtfm));
	struct uld_ctx *u_ctx = NULL;
	struct sk_buff *skb;
	struct hash_wr_param params;
	u8  bs;

	rtfm->init(req);
	bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(rtfm));

	u_ctx = ULD_CTX(ctx);
	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	if (is_hmac(crypto_ahash_tfm(rtfm)))
		params.opad_needed = 1;
	else
		params.opad_needed = 0;

	params.last = 0;
	params.more = 0;
	params.sg_len = req->nbytes;
	params.bfr_len = 0;
	params.scmd1 = 0;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	req_ctx->result = 1;
	req_ctx->data_len += params.bfr_len + params.sg_len;

	if (req->nbytes == 0) {
		create_last_hash_block(req_ctx->reqbfr, bs, 0);
		params.more = 1;
		params.bfr_len = bs;
	}

	skb = create_hash_wr(req, &params);
	if (!skb)
		return -ENOMEM;

	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);
	return -EINPROGRESS;
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
	return 0;
}

static int chcr_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int bs = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int i, err = 0, updated_digestsize;

	SHASH_DESC_ON_STACK(shash, hmacctx->base_hash);

	/* use the key to calculate the ipad and opad. ipad will sent with the
	 * first request's data. opad will be sent with the final hash result
	 * ipad in hmacctx->ipad and opad in hmacctx->opad location
	 */
	shash->tfm = hmacctx->base_hash;
	shash->flags = crypto_shash_get_flags(hmacctx->base_hash);
	if (keylen > bs) {
		err = crypto_shash_digest(shash, key, keylen,
					  hmacctx->ipad);
		if (err)
			goto out;
		keylen = digestsize;
	} else {
		memcpy(hmacctx->ipad, key, keylen);
	}
	memset(hmacctx->ipad + keylen, 0, bs - keylen);
	memcpy(hmacctx->opad, hmacctx->ipad, bs);

	for (i = 0; i < bs / sizeof(int); i++) {
		*((unsigned int *)(&hmacctx->ipad) + i) ^= IPAD_DATA;
		*((unsigned int *)(&hmacctx->opad) + i) ^= OPAD_DATA;
	}

	updated_digestsize = digestsize;
	if (digestsize == SHA224_DIGEST_SIZE)
		updated_digestsize = SHA256_DIGEST_SIZE;
	else if (digestsize == SHA384_DIGEST_SIZE)
		updated_digestsize = SHA512_DIGEST_SIZE;
	err = chcr_compute_partial_hash(shash, hmacctx->ipad,
					hmacctx->ipad, digestsize);
	if (err)
		goto out;
	chcr_change_order(hmacctx->ipad, updated_digestsize);

	err = chcr_compute_partial_hash(shash, hmacctx->opad,
					hmacctx->opad, digestsize);
	if (err)
		goto out;
	chcr_change_order(hmacctx->opad, updated_digestsize);
out:
	return err;
}

static int chcr_aes_xts_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			       unsigned int key_len)
{
	struct chcr_context *ctx = crypto_ablkcipher_ctx(tfm);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);
	unsigned short context_size = 0;

	if ((key_len != (AES_KEYSIZE_128 << 1)) &&
	    (key_len != (AES_KEYSIZE_256 << 1))) {
		crypto_tfm_set_flags((struct crypto_tfm *)tfm,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		ablkctx->enckey_len = 0;
		return -EINVAL;

	}

	memcpy(ablkctx->key, key, key_len);
	ablkctx->enckey_len = key_len;
	get_aes_decrypt_key(ablkctx->rrkey, ablkctx->key, key_len << 2);
	context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD + key_len) >> 4;
	ablkctx->key_ctx_hdr =
		FILL_KEY_CTX_HDR((key_len == AES_KEYSIZE_256) ?
				 CHCR_KEYCTX_CIPHER_KEY_SIZE_128 :
				 CHCR_KEYCTX_CIPHER_KEY_SIZE_256,
				 CHCR_KEYCTX_NO_KEY, 1,
				 0, context_size);
	ablkctx->ciph_mode = CHCR_SCMD_CIPHER_MODE_AES_XTS;
	return 0;
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
	req_ctx->skb = NULL;
	req_ctx->result = 0;
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
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(rtfm));
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);
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
	struct chcr_context *ctx = crypto_tfm_ctx(tfm);
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);
	unsigned int digestsize =
		crypto_ahash_digestsize(__crypto_ahash_cast(tfm));

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct chcr_ahash_req_ctx));
	hmacctx->base_hash = chcr_alloc_shash(digestsize);
	if (IS_ERR(hmacctx->base_hash))
		return PTR_ERR(hmacctx->base_hash);
	return chcr_device_init(crypto_tfm_ctx(tfm));
}

static void chcr_hmac_cra_exit(struct crypto_tfm *tfm)
{
	struct chcr_context *ctx = crypto_tfm_ctx(tfm);
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);

	if (hmacctx->base_hash) {
		chcr_free_shash(hmacctx->base_hash);
		hmacctx->base_hash = NULL;
	}
}

static int chcr_copy_assoc(struct aead_request *req,
				struct chcr_aead_ctx *ctx)
{
	SKCIPHER_REQUEST_ON_STACK(skreq, ctx->null);

	skcipher_request_set_tfm(skreq, ctx->null);
	skcipher_request_set_callback(skreq, aead_request_flags(req),
			NULL, NULL);
	skcipher_request_set_crypt(skreq, req->src, req->dst, req->assoclen,
			NULL);

	return crypto_skcipher_encrypt(skreq);
}

static unsigned char get_hmac(unsigned int authsize)
{
	switch (authsize) {
	case ICV_8:
		return CHCR_SCMD_HMAC_CTRL_PL1;
	case ICV_10:
		return CHCR_SCMD_HMAC_CTRL_TRUNC_RFC4366;
	case ICV_12:
		return CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT;
	}
	return CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
}


static struct sk_buff *create_authenc_wr(struct aead_request *req,
					 unsigned short qid,
					 int size,
					 unsigned short op_type)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = crypto_aead_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_authenc_ctx *actx = AUTHENC_CTX(aeadctx);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct phys_sge_parm sg_param;
	struct scatterlist *src, *dst;
	struct scatterlist src_sg[2], dst_sg[2];
	unsigned int frags = 0, transhdr_len;
	unsigned int ivsize = crypto_aead_ivsize(tfm), dst_size = 0;
	unsigned int   kctx_len = 0;
	unsigned short stop_offset = 0;
	unsigned int  assoclen = req->assoclen;
	unsigned int  authsize = crypto_aead_authsize(tfm);
	int err = 0;
	int null = 0;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;

	if (aeadctx->enckey_len == 0 || (req->cryptlen == 0))
		goto err;

	if (op_type && req->cryptlen < crypto_aead_authsize(tfm))
		goto err;

	if (sg_nents_for_len(req->src, req->assoclen + req->cryptlen) < 0)
		goto err;
	src = scatterwalk_ffwd(src_sg, req->src, req->assoclen);
	dst = src;
	if (req->src != req->dst) {
		err = chcr_copy_assoc(req, aeadctx);
		if (err)
			return ERR_PTR(err);
		dst = scatterwalk_ffwd(dst_sg, req->dst, req->assoclen);
	}
	if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_NULL) {
		null = 1;
		assoclen = 0;
	}
	reqctx->dst_nents = sg_nents_for_len(dst, req->cryptlen +
					     (op_type ? -authsize : authsize));
	if (reqctx->dst_nents <= 0) {
		pr_err("AUTHENC:Invalid Destination sg entries\n");
		goto err;
	}
	dst_size = get_space_for_phys_dsgl(reqctx->dst_nents);
	kctx_len = (ntohl(KEY_CONTEXT_CTX_LEN_V(aeadctx->key_ctx_hdr)) << 4)
		- sizeof(chcr_req->key_ctx);
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)), flags);
	if (!skb)
		goto err;

	/* LLD is going to write the sge hdr. */
	skb_reserve(skb, sizeof(struct sge_opaque_hdr));

	/* Write WR */
	chcr_req = (struct chcr_wr *) __skb_put(skb, transhdr_len);
	memset(chcr_req, 0, transhdr_len);

	stop_offset = (op_type == CHCR_ENCRYPT_OP) ? 0 : authsize;

	/*
	 * Input order	is AAD,IV and Payload. where IV should be included as
	 * the part of authdata. All other fields should be filled according
	 * to the hardware spec
	 */
	chcr_req->sec_cpl.op_ivinsrtofst =
		FILL_SEC_CPL_OP_IVINSR(ctx->dev->tx_channel_id, 2,
				       (ivsize ? (assoclen + 1) : 0));
	chcr_req->sec_cpl.pldlen = htonl(assoclen + ivsize + req->cryptlen);
	chcr_req->sec_cpl.aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
					assoclen ? 1 : 0, assoclen,
					assoclen + ivsize + 1,
					(stop_offset & 0x1F0) >> 4);
	chcr_req->sec_cpl.cipherstop_lo_authinsert = FILL_SEC_CPL_AUTHINSERT(
					stop_offset & 0xF,
					null ? 0 : assoclen + ivsize + 1,
					stop_offset, stop_offset);
	chcr_req->sec_cpl.seqno_numivs = FILL_SEC_CPL_SCMD0_SEQNO(op_type,
					(op_type == CHCR_ENCRYPT_OP) ? 1 : 0,
					CHCR_SCMD_CIPHER_MODE_AES_CBC,
					actx->auth_mode, aeadctx->hmac_ctrl,
					ivsize >> 1);
	chcr_req->sec_cpl.ivgen_hdrlen =  FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1,
					 0, 1, dst_size);

	chcr_req->key_ctx.ctx_hdr = aeadctx->key_ctx_hdr;
	if (op_type == CHCR_ENCRYPT_OP)
		memcpy(chcr_req->key_ctx.key, aeadctx->key,
		       aeadctx->enckey_len);
	else
		memcpy(chcr_req->key_ctx.key, actx->dec_rrkey,
		       aeadctx->enckey_len);

	memcpy(chcr_req->key_ctx.key + (DIV_ROUND_UP(aeadctx->enckey_len, 16) <<
					4), actx->h_iopad, kctx_len -
				(DIV_ROUND_UP(aeadctx->enckey_len, 16) << 4));

	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	sg_param.nents = reqctx->dst_nents;
	sg_param.obsize = req->cryptlen + (op_type ? -authsize : authsize);
	sg_param.qid = qid;
	sg_param.align = 0;
	if (map_writesg_phys_cpl(&u_ctx->lldi.pdev->dev, phys_cpl, dst,
				  &sg_param))
		goto dstmap_fail;

	skb_set_transport_header(skb, transhdr_len);

	if (assoclen) {
		/* AAD buffer in */
		write_sg_to_skb(skb, &frags, req->src, assoclen);

	}
	write_buffer_to_skb(skb, &frags, req->iv, ivsize);
	write_sg_to_skb(skb, &frags, src, req->cryptlen);
	create_wreq(ctx, chcr_req, req, skb, kctx_len, size, 1,
		   sizeof(struct cpl_rx_phys_dsgl) + dst_size);
	reqctx->skb = skb;
	skb_get(skb);

	return skb;
dstmap_fail:
	/* ivmap_fail: */
	kfree_skb(skb);
err:
	return ERR_PTR(-EINVAL);
}

static void aes_gcm_empty_pld_pad(struct scatterlist *sg,
				  unsigned short offset)
{
	struct page *spage;
	unsigned char *addr;

	spage = sg_page(sg);
	get_page(spage); /* so that it is not freed by NIC */
#ifdef KMAP_ATOMIC_ARGS
	addr = kmap_atomic(spage, KM_SOFTIRQ0);
#else
	addr = kmap_atomic(spage);
#endif
	memset(addr + sg->offset, 0, offset + 1);

	kunmap_atomic(addr);
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

static void generate_b0(struct aead_request *req,
			struct chcr_aead_ctx *aeadctx,
			unsigned short op_type)
{
	unsigned int l, lp, m;
	int rc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);
	u8 *b0 = reqctx->scratch_pad;

	m = crypto_aead_authsize(aead);

	memcpy(b0, reqctx->iv, 16);

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
}

static inline int crypto_ccm_check_iv(const u8 *iv)
{
	/* 2 <= L <= 8, so 1 <= L' <= 7. */
	if (iv[0] < 1 || iv[0] > 7)
		return -EINVAL;

	return 0;
}

static int ccm_format_packet(struct aead_request *req,
			     struct chcr_aead_ctx *aeadctx,
			     unsigned int sub_type,
			     unsigned short op_type)
{
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);
	int rc = 0;

	if (req->assoclen > T5_MAX_AAD_SIZE) {
		pr_err("CCM: Unsupported AAD data. It should be < %d\n",
		       T5_MAX_AAD_SIZE);
		return -EINVAL;
	}
	if (sub_type == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309) {
		reqctx->iv[0] = 3;
		memcpy(reqctx->iv + 1, &aeadctx->salt[0], 3);
		memcpy(reqctx->iv + 4, req->iv, 8);
		memset(reqctx->iv + 12, 0, 4);
		*((unsigned short *)(reqctx->scratch_pad + 16)) =
			htons(req->assoclen - 8);
	} else {
		memcpy(reqctx->iv, req->iv, 16);
		*((unsigned short *)(reqctx->scratch_pad + 16)) =
			htons(req->assoclen);
	}
	generate_b0(req, aeadctx, op_type);
	/* zero the ctr value */
	memset(reqctx->iv + 15 - reqctx->iv[0], 0, reqctx->iv[0] + 1);
	return rc;
}

static void fill_sec_cpl_for_aead(struct cpl_tx_sec_pdu *sec_cpl,
				  unsigned int dst_size,
				  struct aead_request *req,
				  unsigned short op_type,
					  struct chcr_context *chcrctx)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	unsigned int ivsize = AES_BLOCK_SIZE;
	unsigned int cipher_mode = CHCR_SCMD_CIPHER_MODE_AES_CCM;
	unsigned int mac_mode = CHCR_SCMD_AUTH_MODE_CBCMAC;
	unsigned int c_id = chcrctx->dev->tx_channel_id;
	unsigned int ccm_xtra;
	unsigned char tag_offset = 0, auth_offset = 0;
	unsigned char hmac_ctrl = get_hmac(crypto_aead_authsize(tfm));
	unsigned int assoclen;

	if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309)
		assoclen = req->assoclen - 8;
	else
		assoclen = req->assoclen;
	ccm_xtra = CCM_B0_SIZE +
		((assoclen) ? CCM_AAD_FIELD_SIZE : 0);

	auth_offset = req->cryptlen ?
		(assoclen + ivsize + 1 + ccm_xtra) : 0;
	if (op_type == CHCR_DECRYPT_OP) {
		if (crypto_aead_authsize(tfm) != req->cryptlen)
			tag_offset = crypto_aead_authsize(tfm);
		else
			auth_offset = 0;
	}


	sec_cpl->op_ivinsrtofst = FILL_SEC_CPL_OP_IVINSR(c_id,
					 2, (ivsize ?  (assoclen + 1) :  0) +
					 ccm_xtra);
	sec_cpl->pldlen =
		htonl(assoclen + ivsize + req->cryptlen + ccm_xtra);
	/* For CCM there wil be b0 always. So AAD start will be 1 always */
	sec_cpl->aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
					1, assoclen + ccm_xtra, assoclen
					+ ivsize + 1 + ccm_xtra, 0);

	sec_cpl->cipherstop_lo_authinsert = FILL_SEC_CPL_AUTHINSERT(0,
					auth_offset, tag_offset,
					(op_type == CHCR_ENCRYPT_OP) ? 0 :
					crypto_aead_authsize(tfm));
	sec_cpl->seqno_numivs =  FILL_SEC_CPL_SCMD0_SEQNO(op_type,
					(op_type == CHCR_ENCRYPT_OP) ? 0 : 1,
					cipher_mode, mac_mode, hmac_ctrl,
					ivsize >> 1);

	sec_cpl->ivgen_hdrlen = FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1, 0,
					1, dst_size);
}

int aead_ccm_validate_input(unsigned short op_type,
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
	if (aeadctx->enckey_len == 0) {
		pr_err("CCM: Encryption key not set\n");
		return -EINVAL;
	}
	return 0;
}

unsigned int fill_aead_req_fields(struct sk_buff *skb,
				  struct aead_request *req,
				  struct scatterlist *src,
				  unsigned int ivsize,
				  struct chcr_aead_ctx *aeadctx)
{
	unsigned int frags = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);
	/* b0 and aad length(if available) */

	write_buffer_to_skb(skb, &frags, reqctx->scratch_pad, CCM_B0_SIZE +
				(req->assoclen ?  CCM_AAD_FIELD_SIZE : 0));
	if (req->assoclen) {
		if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309)
			write_sg_to_skb(skb, &frags, req->src,
					req->assoclen - 8);
		else
			write_sg_to_skb(skb, &frags, req->src, req->assoclen);
	}
	write_buffer_to_skb(skb, &frags, reqctx->iv, ivsize);
	if (req->cryptlen)
		write_sg_to_skb(skb, &frags, src, req->cryptlen);

	return frags;
}

static struct sk_buff *create_aead_ccm_wr(struct aead_request *req,
					  unsigned short qid,
					  int size,
					  unsigned short op_type)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = crypto_aead_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct phys_sge_parm sg_param;
	struct scatterlist *src, *dst;
	struct scatterlist src_sg[2], dst_sg[2];
	unsigned int frags = 0, transhdr_len, ivsize = AES_BLOCK_SIZE;
	unsigned int dst_size = 0, kctx_len;
	unsigned int sub_type;
	unsigned int authsize = crypto_aead_authsize(tfm);
	int err = 0;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;


	if (op_type && req->cryptlen < crypto_aead_authsize(tfm))
		goto err;

	if (sg_nents_for_len(req->src, req->assoclen + req->cryptlen) < 0)
		goto err;
	sub_type = get_aead_subtype(tfm);
	src = scatterwalk_ffwd(src_sg, req->src, req->assoclen);
	dst = src;
	if (req->src != req->dst) {
		err = chcr_copy_assoc(req, aeadctx);
		if (err) {
			pr_err("AAD copy to destination buffer fails\n");
			return ERR_PTR(err);
		}
		dst = scatterwalk_ffwd(dst_sg, req->dst, req->assoclen);
	}
	reqctx->dst_nents = sg_nents_for_len(dst, req->cryptlen +
					     (op_type ? -authsize : authsize));
	if (reqctx->dst_nents <= 0) {
		pr_err("CCM:Invalid Destination sg entries\n");
		goto err;
	}


	if (aead_ccm_validate_input(op_type, req, aeadctx, sub_type))
		goto err;

	dst_size = get_space_for_phys_dsgl(reqctx->dst_nents);
	kctx_len = ((DIV_ROUND_UP(aeadctx->enckey_len, 16)) << 4) * 2;
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)),  flags);

	if (!skb)
		goto err;

	skb_reserve(skb, sizeof(struct sge_opaque_hdr));

	chcr_req = (struct chcr_wr *) __skb_put(skb, transhdr_len);
	memset(chcr_req, 0, transhdr_len);

	fill_sec_cpl_for_aead(&chcr_req->sec_cpl, dst_size, req, op_type, ctx);

	chcr_req->key_ctx.ctx_hdr = aeadctx->key_ctx_hdr;
	memcpy(chcr_req->key_ctx.key, aeadctx->key, aeadctx->enckey_len);
	memcpy(chcr_req->key_ctx.key + (DIV_ROUND_UP(aeadctx->enckey_len, 16) *
					16), aeadctx->key, aeadctx->enckey_len);

	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	if (ccm_format_packet(req, aeadctx, sub_type, op_type))
		goto dstmap_fail;

	sg_param.nents = reqctx->dst_nents;
	sg_param.obsize = req->cryptlen + (op_type ? -authsize : authsize);
	sg_param.qid = qid;
	sg_param.align = 0;
	if (map_writesg_phys_cpl(&u_ctx->lldi.pdev->dev, phys_cpl, dst,
				  &sg_param))
		goto dstmap_fail;

	skb_set_transport_header(skb, transhdr_len);
	frags = fill_aead_req_fields(skb, req, src, ivsize, aeadctx);
	create_wreq(ctx, chcr_req, req, skb, kctx_len, 0, 1,
		    sizeof(struct cpl_rx_phys_dsgl) + dst_size);
	reqctx->skb = skb;
	skb_get(skb);
	return skb;
dstmap_fail:
	kfree_skb(skb);
	skb = NULL;
err:
	return ERR_PTR(-EINVAL);
}

static struct sk_buff *create_gcm_wr(struct aead_request *req,
				     unsigned short qid,
				     int size,
				     unsigned short op_type)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = crypto_aead_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_aead_reqctx  *reqctx = aead_request_ctx(req);
	struct sk_buff *skb = NULL;
	struct chcr_wr *chcr_req;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct phys_sge_parm sg_param;
	struct scatterlist *src, *dst;
	struct scatterlist src_sg[2], dst_sg[2];
	unsigned int frags = 0, transhdr_len;
	unsigned int ivsize = AES_BLOCK_SIZE;
	unsigned int dst_size = 0, kctx_len;
	unsigned char tag_offset = 0;
	unsigned int crypt_len = 0;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned char hmac_ctrl = get_hmac(authsize);
	int err = 0;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		GFP_ATOMIC;

	/* validate key size */
	if (aeadctx->enckey_len == 0)
		goto err;

	if (op_type && req->cryptlen < crypto_aead_authsize(tfm))
		goto err;

	if (sg_nents_for_len(req->src, req->assoclen + req->cryptlen) < 0)
		goto err;

	src = scatterwalk_ffwd(src_sg, req->src, req->assoclen);
	dst = src;
	if (req->src != req->dst) {
		err = chcr_copy_assoc(req, aeadctx);
		if (err)
			return	ERR_PTR(err);
		dst = scatterwalk_ffwd(dst_sg, req->dst, req->assoclen);
	}

	if (!req->cryptlen)
		/* null-payload is not supported in the hardware.
		 * software is sending block size
		 */
		crypt_len = AES_BLOCK_SIZE;
	else
		crypt_len = req->cryptlen;
	reqctx->dst_nents = sg_nents_for_len(dst, req->cryptlen +
					     (op_type ? -authsize : authsize));
	if (reqctx->dst_nents <= 0) {
		pr_err("GCM:Invalid Destination sg entries\n");
		goto err;
	}


	dst_size = get_space_for_phys_dsgl(reqctx->dst_nents);
	kctx_len = ((DIV_ROUND_UP(aeadctx->enckey_len, 16)) << 4) +
		AEAD_H_SIZE;
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dst_size);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)), flags);
	if (!skb)
		goto err;

	/* NIC driver is going to write the sge hdr. */
	skb_reserve(skb, sizeof(struct sge_opaque_hdr));

	chcr_req = (struct chcr_wr *)__skb_put(skb, transhdr_len);
	memset(chcr_req, 0, transhdr_len);

	if (get_aead_subtype(tfm) == CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106)
		req->assoclen -= 8;

	tag_offset = (op_type == CHCR_ENCRYPT_OP) ? 0 : authsize;
	chcr_req->sec_cpl.op_ivinsrtofst = FILL_SEC_CPL_OP_IVINSR(
					ctx->dev->tx_channel_id, 2, (ivsize ?
					(req->assoclen + 1) : 0));
	chcr_req->sec_cpl.pldlen = htonl(req->assoclen + ivsize + crypt_len);
	chcr_req->sec_cpl.aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
					req->assoclen ? 1 : 0, req->assoclen,
					req->assoclen + ivsize + 1, 0);
	if (req->cryptlen) {
		chcr_req->sec_cpl.cipherstop_lo_authinsert =
			FILL_SEC_CPL_AUTHINSERT(0, req->assoclen + ivsize + 1,
						tag_offset, tag_offset);
		chcr_req->sec_cpl.seqno_numivs =
			FILL_SEC_CPL_SCMD0_SEQNO(op_type, (op_type ==
					CHCR_ENCRYPT_OP) ? 1 : 0,
					CHCR_SCMD_CIPHER_MODE_AES_GCM,
					CHCR_SCMD_AUTH_MODE_GHASH, hmac_ctrl,
					ivsize >> 1);
	} else {
		chcr_req->sec_cpl.cipherstop_lo_authinsert =
			FILL_SEC_CPL_AUTHINSERT(0, 0, 0, 0);
		chcr_req->sec_cpl.seqno_numivs =
			FILL_SEC_CPL_SCMD0_SEQNO(op_type,
					(op_type ==  CHCR_ENCRYPT_OP) ?
					1 : 0, CHCR_SCMD_CIPHER_MODE_AES_CBC,
					0, 0, ivsize >> 1);
	}
	chcr_req->sec_cpl.ivgen_hdrlen =  FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1,
					0, 1, dst_size);
	chcr_req->key_ctx.ctx_hdr = aeadctx->key_ctx_hdr;
	memcpy(chcr_req->key_ctx.key, aeadctx->key, aeadctx->enckey_len);
	memcpy(chcr_req->key_ctx.key + (DIV_ROUND_UP(aeadctx->enckey_len, 16) *
				16), GCM_CTX(aeadctx)->ghash_h, AEAD_H_SIZE);

	/* prepare a 16 byte iv */
	/* S   A   L  T |  IV | 0x00000001 */
	if (get_aead_subtype(tfm) ==
	    CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106) {
		memcpy(reqctx->iv, aeadctx->salt, 4);
		memcpy(reqctx->iv + 4, req->iv, 8);
	} else {
		memcpy(reqctx->iv, req->iv, 12);
	}
	*((unsigned int *)(reqctx->iv + 12)) = htonl(0x01);

	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)(chcr_req + 1) + kctx_len);
	sg_param.nents = reqctx->dst_nents;
	sg_param.obsize = req->cryptlen + (op_type ? -authsize : authsize);
	sg_param.qid = qid;
	sg_param.align = 0;
	if (map_writesg_phys_cpl(&u_ctx->lldi.pdev->dev, phys_cpl, dst,
				  &sg_param))
		goto dstmap_fail;

	skb_set_transport_header(skb, transhdr_len);

	write_sg_to_skb(skb, &frags, req->src, req->assoclen);

	write_buffer_to_skb(skb, &frags, reqctx->iv, ivsize);

	if (req->cryptlen) {
		write_sg_to_skb(skb, &frags, src, req->cryptlen);
	} else {
		aes_gcm_empty_pld_pad(req->dst, authsize - 1);
		write_sg_to_skb(skb, &frags, dst, crypt_len);
	}

	create_wreq(ctx, chcr_req, req, skb, kctx_len, size, 1,
			sizeof(struct cpl_rx_phys_dsgl) + dst_size);
	reqctx->skb = skb;
	skb_get(skb);
	return skb;

dstmap_fail:
	/* ivmap_fail: */
	kfree_skb(skb);
	skb = NULL;
err:
	return skb;
}



static int chcr_aead_cra_init(struct crypto_aead *tfm)
{
	struct chcr_context *ctx = crypto_aead_ctx(tfm);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);

	crypto_aead_set_reqsize(tfm, sizeof(struct chcr_aead_reqctx));
	aeadctx->null = crypto_get_default_null_skcipher();
	if (IS_ERR(aeadctx->null))
		return PTR_ERR(aeadctx->null);
	return chcr_device_init(ctx);
}

static void chcr_aead_cra_exit(struct crypto_aead *tfm)
{
	crypto_put_default_null_skcipher();
}

static int chcr_authenc_null_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(crypto_aead_ctx(tfm));

	aeadctx->hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NOP;
	aeadctx->mayverify = VERIFY_HW;
	return 0;
}
static int chcr_authenc_setauthsize(struct crypto_aead *tfm,
				    unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(crypto_aead_ctx(tfm));
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
	return 0;
}


static int chcr_gcm_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(crypto_aead_ctx(tfm));

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

		  crypto_tfm_set_flags((struct crypto_tfm *) tfm,
			CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return 0;
}

static int chcr_4106_4309_setauthsize(struct crypto_aead *tfm,
					  unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(crypto_aead_ctx(tfm));

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
		crypto_tfm_set_flags((struct crypto_tfm *)tfm,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return 0;
}

static int chcr_ccm_setauthsize(struct crypto_aead *tfm,
				unsigned int authsize)
{
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(crypto_aead_ctx(tfm));

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
		crypto_tfm_set_flags((struct crypto_tfm *)tfm,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return 0;
}

static int chcr_aead_ccm_setkey(struct crypto_aead *aead,
				const u8 *key,
				unsigned int keylen)
{
	struct chcr_context *ctx = crypto_aead_ctx(aead);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	unsigned char ck_size, mk_size;
	int key_ctx_size = 0;

	memcpy(aeadctx->key, key, keylen);
	aeadctx->enckey_len = keylen;
	key_ctx_size = sizeof(struct _key_ctx) +
		((DIV_ROUND_UP(keylen, 16)) << 4)  * 2;
	if (keylen == AES_KEYSIZE_128) {
		mk_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
		mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_192;
	} else if (keylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
		mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_256;
	} else {
		crypto_tfm_set_flags((struct crypto_tfm *)aead,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		aeadctx->enckey_len = 0;
		return	-EINVAL;
	}
	aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, mk_size, 0, 0,
						key_ctx_size >> 4);
	return 0;
}

static int chcr_aead_rfc4309_setkey(struct crypto_aead *aead, const u8 *key,
				    unsigned int keylen)
{
	struct chcr_context *ctx = crypto_aead_ctx(aead);
	 struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);

	if (keylen < 3) {
		crypto_tfm_set_flags((struct crypto_tfm *)aead,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		aeadctx->enckey_len = 0;
		return	-EINVAL;
	}
	keylen -= 3;
	memcpy(aeadctx->salt, key + keylen, 3);
	return chcr_aead_ccm_setkey(aead, key, keylen);
}

static int chcr_gcm_setkey(struct crypto_aead *aead, const u8 *key,
			   unsigned int keylen)
{
	struct chcr_context *ctx = crypto_aead_ctx(aead);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_gcm_ctx *gctx = GCM_CTX(aeadctx);
	struct blkcipher_desc h_desc;
	struct scatterlist src[1];
	unsigned int ck_size;
	int ret = 0, key_ctx_size = 0;

	if (get_aead_subtype(aead) ==
	    CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106) {
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
		crypto_tfm_set_flags((struct crypto_tfm *)aead,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		aeadctx->enckey_len = 0;
		pr_err("GCM: Invalid key length %d", keylen);
		ret = -EINVAL;
		goto out;
	}

	memcpy(aeadctx->key, key, keylen);
	aeadctx->enckey_len = keylen;
	key_ctx_size = sizeof(struct _key_ctx) +
		((DIV_ROUND_UP(keylen, 16)) << 4) +
		AEAD_H_SIZE;
		aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size,
						CHCR_KEYCTX_MAC_KEY_SIZE_128,
						0, 0,
						key_ctx_size >> 4);
	/* Calculate the H = CIPH(K, 0 repeated 16 times) using sync aes
	 * blkcipher It will go on key context
	 */
	h_desc.tfm = crypto_alloc_blkcipher("cbc(aes-generic)", 0, 0);
	if (IS_ERR(h_desc.tfm)) {
		aeadctx->enckey_len = 0;
		ret = -ENOMEM;
		goto out;
	}
	h_desc.flags = 0;
	ret = crypto_blkcipher_setkey(h_desc.tfm, key, keylen);
	if (ret) {
		aeadctx->enckey_len = 0;
		goto out1;
	}
	memset(gctx->ghash_h, 0, AEAD_H_SIZE);
	sg_init_one(&src[0], gctx->ghash_h, AEAD_H_SIZE);
	ret = crypto_blkcipher_encrypt(&h_desc, &src[0], &src[0], AEAD_H_SIZE);

out1:
	crypto_free_blkcipher(h_desc.tfm);
out:
	return ret;
}

static int chcr_authenc_setkey(struct crypto_aead *authenc, const u8 *key,
				   unsigned int keylen)
{
	struct chcr_context *ctx = crypto_aead_ctx(authenc);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_authenc_ctx *actx = AUTHENC_CTX(aeadctx);
	/* it contains auth and cipher key both*/
	struct crypto_authenc_keys keys;
	unsigned int bs;
	unsigned int max_authsize = crypto_aead_alg(authenc)->maxauthsize;
	int err = 0, i, key_ctx_len = 0;
	unsigned char ck_size = 0;
	unsigned char pad[CHCR_HASH_MAX_BLOCK_SIZE_128] = { 0 };
	struct crypto_shash *base_hash = NULL;
	struct algo_param param;
	int align;
	u8 *o_ptr = NULL;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0) {
		crypto_aead_set_flags(authenc, CRYPTO_TFM_RES_BAD_KEY_LEN);
		goto out;
	}

	if (get_alg_config(&param, max_authsize)) {
		pr_err("chcr : Unsupported digest size\n");
		goto out;
	}
	if (keys.enckeylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keys.enckeylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keys.enckeylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		pr_err("chcr : Unsupported cipher key\n");
		goto out;
	}

	/* Copy only encryption key. We use authkey to generate h(ipad) and
	 * h(opad) so authkey is not needed again. authkeylen size have the
	 * size of the hash digest size.
	 */
	memcpy(aeadctx->key, keys.enckey, keys.enckeylen);
	aeadctx->enckey_len = keys.enckeylen;
	get_aes_decrypt_key(actx->dec_rrkey, aeadctx->key,
			    aeadctx->enckey_len << 3);

	base_hash  = chcr_alloc_shash(max_authsize);
	if (IS_ERR(base_hash)) {
		pr_err("chcr : Base driver cannot be loaded\n");
		goto out;
	}
	{
		SHASH_DESC_ON_STACK(shash, base_hash);
		shash->tfm = base_hash;
		shash->flags = crypto_shash_get_flags(base_hash);
		bs = crypto_shash_blocksize(base_hash);
		align = KEYCTX_ALIGN_PAD(max_authsize);
		o_ptr =  actx->h_iopad + param.result_size + align;

		if (keys.authkeylen > bs) {
			err = crypto_shash_digest(shash, keys.authkey,
						  keys.authkeylen,
						  o_ptr);
			if (err) {
				pr_err("chcr : Base driver cannot be loaded\n");
				goto out;
			}
			keys.authkeylen = max_authsize;
		} else
			memcpy(o_ptr, keys.authkey, keys.authkeylen);

		/* Compute the ipad-digest*/
		memset(pad + keys.authkeylen, 0, bs - keys.authkeylen);
		memcpy(pad, o_ptr, keys.authkeylen);
		for (i = 0; i < bs >> 2; i++)
			*((unsigned int *)pad + i) ^= IPAD_DATA;

		if (chcr_compute_partial_hash(shash, pad, actx->h_iopad,
					      max_authsize))
			goto out;
		/* Compute the opad-digest */
		memset(pad + keys.authkeylen, 0, bs - keys.authkeylen);
		memcpy(pad, o_ptr, keys.authkeylen);
		for (i = 0; i < bs >> 2; i++)
			*((unsigned int *)pad + i) ^= OPAD_DATA;

		if (chcr_compute_partial_hash(shash, pad, o_ptr, max_authsize))
			goto out;

		/* convert the ipad and opad digest to network order */
		chcr_change_order(actx->h_iopad, param.result_size);
		chcr_change_order(o_ptr, param.result_size);
		key_ctx_len = sizeof(struct _key_ctx) +
			((DIV_ROUND_UP(keys.enckeylen, 16)) << 4) +
			(param.result_size + align) * 2;
		aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, param.mk_size,
						0, 1, key_ctx_len >> 4);
		actx->auth_mode = param.auth_mode;
		chcr_free_shash(base_hash);

		return 0;
	}
out:
	aeadctx->enckey_len = 0;
	if (base_hash)
		chcr_free_shash(base_hash);
	return -EINVAL;
}

static int chcr_aead_digest_null_setkey(struct crypto_aead *authenc,
					const u8 *key, unsigned int keylen)
{
	struct chcr_context *ctx = crypto_aead_ctx(authenc);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(ctx);
	struct chcr_authenc_ctx *actx = AUTHENC_CTX(aeadctx);
	struct crypto_authenc_keys keys;

	/* it contains auth and cipher key both*/
	int key_ctx_len = 0;
	unsigned char ck_size = 0;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0) {
		crypto_aead_set_flags(authenc, CRYPTO_TFM_RES_BAD_KEY_LEN);
		goto out;
	}
	if (keys.enckeylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keys.enckeylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keys.enckeylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		pr_err("chcr : Unsupported cipher key\n");
		goto out;
	}
	memcpy(aeadctx->key, keys.enckey, keys.enckeylen);
	aeadctx->enckey_len = keys.enckeylen;
	get_aes_decrypt_key(actx->dec_rrkey, aeadctx->key,
				    aeadctx->enckey_len << 3);
	key_ctx_len =  sizeof(struct _key_ctx)
		+ ((DIV_ROUND_UP(keys.enckeylen, 16)) << 4);

	aeadctx->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size, CHCR_KEYCTX_NO_KEY, 0,
						0, key_ctx_len >> 4);
	actx->auth_mode = CHCR_SCMD_AUTH_MODE_NOP;
	return 0;
out:
	aeadctx->enckey_len = 0;
	return -EINVAL;
}
static int chcr_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);

	reqctx->verify = VERIFY_HW;

	switch (get_aead_subtype(tfm)) {
	case CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC:
	case CRYPTO_ALG_SUB_TYPE_AEAD_NULL:
		return chcr_aead_op(req, CHCR_ENCRYPT_OP, 0,
				    create_authenc_wr);
	case CRYPTO_ALG_SUB_TYPE_AEAD_CCM:
	case CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309:
		return chcr_aead_op(req, CHCR_ENCRYPT_OP, 0,
				    create_aead_ccm_wr);
	default:
		return chcr_aead_op(req, CHCR_ENCRYPT_OP, 0,
				    create_gcm_wr);
	}
}

static int chcr_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_aead_ctx *aeadctx = AEAD_CTX(crypto_aead_ctx(tfm));
	struct chcr_aead_reqctx *reqctx = aead_request_ctx(req);
	int size;

	if (aeadctx->mayverify == VERIFY_SW) {
		size = crypto_aead_maxauthsize(tfm);
		reqctx->verify = VERIFY_SW;
	} else {
		size = 0;
		reqctx->verify = VERIFY_HW;
	}

	switch (get_aead_subtype(tfm)) {
	case CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC:
	case CRYPTO_ALG_SUB_TYPE_AEAD_NULL:
		return chcr_aead_op(req, CHCR_DECRYPT_OP, size,
				    create_authenc_wr);
	case CRYPTO_ALG_SUB_TYPE_AEAD_CCM:
	case CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309:
		return chcr_aead_op(req, CHCR_DECRYPT_OP, size,
				    create_aead_ccm_wr);
	default:
		return chcr_aead_op(req, CHCR_DECRYPT_OP, size,
				    create_gcm_wr);
	}
}

static int chcr_aead_op(struct aead_request *req,
			  unsigned short op_type,
			  int size,
			  create_wr_t create_wr_fn)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chcr_context *ctx = crypto_aead_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;

	if (ctx && !ctx->dev) {
		pr_err("chcr : %s : No crypto device.\n", __func__);
		return -ENXIO;
	}
	if (cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
				   ctx->tx_channel_id)) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	/* Form a WR from req */
	skb = create_wr_fn(req, u_ctx->lldi.rxq_ids[ctx->tx_channel_id], size,
			   op_type);

	if (IS_ERR(skb) || skb == NULL) {
		pr_err("chcr : %s : failed to form WR. No memory\n", __func__);
		return PTR_ERR(skb);
	}

	skb->dev = u_ctx->lldi.ports[0];
	set_wr_txq(skb, CPL_PRIORITY_DATA, ctx->tx_channel_id);
	chcr_send_wr(skb);
	return -EINPROGRESS;
}
static struct chcr_alg_template driver_algs[] = {
	/* AES-CBC */
	{
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.is_registered = 0,
		.alg.crypto = {
			.cra_name		= "cbc(aes)",
			.cra_driver_name	= "cbc-aes-chcr",
			.cra_priority		= CHCR_CRA_PRIORITY,
			.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				CRYPTO_ALG_ASYNC,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct chcr_context)
				+ sizeof(struct ablk_ctx),
			.cra_alignmask		= 0,
			.cra_type		= &crypto_ablkcipher_type,
			.cra_module		= THIS_MODULE,
			.cra_init		= chcr_cra_init,
			.cra_exit		= NULL,
			.cra_u.ablkcipher	= {
				.min_keysize	= AES_MIN_KEY_SIZE,
				.max_keysize	= AES_MAX_KEY_SIZE,
				.ivsize		= AES_BLOCK_SIZE,
				.setkey			= chcr_aes_cbc_setkey,
				.encrypt		= chcr_aes_encrypt,
				.decrypt		= chcr_aes_decrypt,
			}
		}
	},
	{
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.is_registered = 0,
		.alg.crypto =   {
			.cra_name		= "xts(aes)",
			.cra_driver_name	= "xts-aes-chcr",
			.cra_priority		= CHCR_CRA_PRIORITY,
			.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				CRYPTO_ALG_ASYNC,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct chcr_context) +
				sizeof(struct ablk_ctx),
			.cra_alignmask		= 0,
			.cra_type		= &crypto_ablkcipher_type,
			.cra_module		= THIS_MODULE,
			.cra_init		= chcr_cra_init,
			.cra_exit		= NULL,
			.cra_u = {
				.ablkcipher = {
					.min_keysize	= 2 * AES_MIN_KEY_SIZE,
					.max_keysize	= 2 * AES_MAX_KEY_SIZE,
					.ivsize		= AES_BLOCK_SIZE,
					.setkey		= chcr_aes_xts_setkey,
					.encrypt	= chcr_aes_encrypt,
					.decrypt	= chcr_aes_decrypt,
				}
			}
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
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_gcm_ctx),
			},
			.ivsize = 12,
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
				.cra_ctxsize =	sizeof(struct chcr_context) +
						sizeof(struct chcr_aead_ctx) +
						sizeof(struct chcr_gcm_ctx),

			},
			.ivsize = 8,
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
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha1-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
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
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC,
		.is_registered = 0,
		.alg.aead = {
			.base = {

				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha256-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
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
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha224-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
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
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha384-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
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
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(aes))",
				.cra_driver_name =
					"authenc-hmac-sha512-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
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
		.type = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_SUB_TYPE_AEAD_NULL,
		.is_registered = 0,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(digest_null,cbc(aes))",
				.cra_driver_name =
					"authenc-digest_null-cbc-aes-chcr",
				.cra_blocksize	 = AES_BLOCK_SIZE,
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
		case CRYPTO_ALG_TYPE_ABLKCIPHER:
			if (driver_algs[i].is_registered)
				crypto_unregister_alg(
						&driver_algs[i].alg.crypto);
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			if (driver_algs[i].is_registered)
				crypto_unregister_aead(
						&driver_algs[i].alg.aead);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			if (driver_algs[i].is_registered)
				crypto_unregister_ahash(
						&driver_algs[i].alg.hash);
			break;
		}
		driver_algs[i].is_registered = 0;
	}
	return 0;
}

#define SZ_AHASH_CTX sizeof(struct chcr_context)
#define SZ_AHASH_H_CTX (sizeof(struct chcr_context) + sizeof(struct hmac_ctx))
#define SZ_AHASH_REQ_CTX sizeof(struct chcr_ahash_req_ctx)
#define AHASH_CRA_FLAGS (CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC)

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
		case CRYPTO_ALG_TYPE_ABLKCIPHER:
			err = crypto_register_alg(&driver_algs[i].alg.crypto);
			name = driver_algs[i].alg.crypto.cra_driver_name;
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			driver_algs[i].alg.aead.base.cra_priority =
				CHCR_CRA_PRIORITY;
			driver_algs[i].alg.aead.base.cra_flags =
				CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC;
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
			a_hash->halg.base.cra_flags = AHASH_CRA_FLAGS;
			a_hash->halg.base.cra_alignmask = 0;
			a_hash->halg.base.cra_exit = NULL;
			a_hash->halg.base.cra_type = &crypto_ahash_type;

			if (driver_algs[i].type == CRYPTO_ALG_TYPE_HMAC) {
				a_hash->halg.base.cra_init = chcr_hmac_cra_init;
				a_hash->halg.base.cra_exit = chcr_hmac_cra_exit;
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
			pr_err("chcr : %s : Algorithm registration failed\n",
			       name);
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
