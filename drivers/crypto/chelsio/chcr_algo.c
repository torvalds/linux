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
#include <crypto/internal/hash.h>

#include "t4fw_api.h"
#include "t4_msg.h"
#include "chcr_core.h"
#include "chcr_algo.h"
#include "chcr_crypto.h"

static inline struct ablk_ctx *ABLK_CTX(struct chcr_context *ctx)
{
	return ctx->crypto_ctx->ablkctx;
}

static inline struct hmac_ctx *HMAC_CTX(struct chcr_context *ctx)
{
	return ctx->crypto_ctx->hmacctx;
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

/*
 *	chcr_handle_resp - Unmap the DMA buffers associated with the request
 *	@req: crypto request
 */
int chcr_handle_resp(struct crypto_async_request *req, unsigned char *input,
		     int error_status)
{
	struct crypto_tfm *tfm = req->tfm;
	struct chcr_context *ctx = crypto_tfm_ctx(tfm);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct chcr_req_ctx ctx_req;
	struct cpl_fw6_pld *fw6_pld;
	unsigned int digestsize, updated_digestsize;

	switch (tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_BLKCIPHER:
		ctx_req.req.ablk_req = (struct ablkcipher_request *)req;
		ctx_req.ctx.ablk_ctx =
			ablkcipher_request_ctx(ctx_req.req.ablk_req);
		if (!error_status) {
			fw6_pld = (struct cpl_fw6_pld *)input;
			memcpy(ctx_req.req.ablk_req->info, &fw6_pld->data[2],
			       AES_BLOCK_SIZE);
		}
		dma_unmap_sg(&u_ctx->lldi.pdev->dev, ctx_req.req.ablk_req->dst,
			     ABLK_CTX(ctx)->dst_nents, DMA_FROM_DEVICE);
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
		if (ctx_req.ctx.ahash_ctx->skb)
			ctx_req.ctx.ahash_ctx->skb = NULL;
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
		kfree(ctx_req.ctx.ahash_ctx->dummy_payload_ptr);
		ctx_req.ctx.ahash_ctx->dummy_payload_ptr = NULL;
		break;
	}
	return 0;
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

static struct shash_desc *chcr_alloc_shash(unsigned int ds)
{
	struct crypto_shash *base_hash = NULL;
	struct shash_desc *desc;

	switch (ds) {
	case SHA1_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha1-generic", 0, 0);
		break;
	case SHA224_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha224-generic", 0, 0);
		break;
	case SHA256_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha256-generic", 0, 0);
		break;
	case SHA384_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha384-generic", 0, 0);
		break;
	case SHA512_DIGEST_SIZE:
		base_hash = crypto_alloc_shash("sha512-generic", 0, 0);
		break;
	}
	if (IS_ERR(base_hash)) {
		pr_err("Can not allocate sha-generic algo.\n");
		return (void *)base_hash;
	}

	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(base_hash),
		       GFP_KERNEL);
	if (!desc)
		return ERR_PTR(-ENOMEM);
	desc->tfm = base_hash;
	desc->flags = crypto_shash_get_flags(base_hash);
	return desc;
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
	if ((chcr_crypto_alg->type & CRYPTO_ALG_SUB_TYPE_MASK) ==
	    CRYPTO_ALG_SUB_TYPE_HASH_HMAC)
		return 1;
	return 0;
}

static inline unsigned int ch_nents(struct scatterlist *sg,
				    unsigned int *total_size)
{
	unsigned int nents;

	for (nents = 0, *total_size = 0; sg; sg = sg_next(sg)) {
		nents++;
		*total_size += sg->length;
	}
	return nents;
}

static void write_phys_cpl(struct cpl_rx_phys_dsgl *phys_cpl,
			   struct scatterlist *sg,
			   struct phys_sge_parm *sg_param)
{
	struct phys_sge_pairs *to;
	unsigned int out_buf_size = sg_param->obsize;
	unsigned int nents = sg_param->nents, i, j, tot_len = 0;

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
		for (j = i; (nents && (j < (8 + i))); j++, nents--) {
			to->len[j] = htons(sg->length);
			to->addr[j] = cpu_to_be64(sg_dma_address(sg));
			if (out_buf_size) {
				if (tot_len + sg_dma_len(sg) >= out_buf_size) {
					to->len[j] = htons(out_buf_size -
							   tot_len);
					return;
				}
				tot_len += sg_dma_len(sg);
			}
			sg = sg_next(sg);
		}
	}
}

static inline unsigned
int map_writesg_phys_cpl(struct device *dev, struct cpl_rx_phys_dsgl *phys_cpl,
			 struct scatterlist *sg, struct phys_sge_parm *sg_param)
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

static inline int get_cryptoalg_subtype(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct chcr_alg_template *chcr_crypto_alg =
		container_of(alg, struct chcr_alg_template, alg.crypto);

	return chcr_crypto_alg->type & CRYPTO_ALG_SUB_TYPE_MASK;
}

static inline void
write_sg_data_page_desc(struct sk_buff *skb, unsigned int *frags,
			struct scatterlist *sg, unsigned int count)
{
	struct page *spage;
	unsigned int page_len;

	skb->len += count;
	skb->data_len += count;
	skb->truesize += count;
	while (count > 0) {
		if (sg && (!(sg->length)))
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
		get_aes_decrypt_key(key_ctx->key, ablkctx->key,
				    ablkctx->enckey_len << 3);
		memset(key_ctx->key + ablkctx->enckey_len, 0,
		       CHCR_AES_MAX_KEY_LEN - ablkctx->enckey_len);
	} else {
		memcpy(key_ctx->key,
		       ablkctx->key + (ablkctx->enckey_len >> 1),
		       ablkctx->enckey_len >> 1);
		get_aes_decrypt_key(key_ctx->key + (ablkctx->enckey_len >> 1),
				    ablkctx->key, ablkctx->enckey_len << 2);
	}
	return 0;
}

static inline void create_wreq(struct chcr_context *ctx,
			       struct fw_crypto_lookaside_wr *wreq,
			       void *req, struct sk_buff *skb,
			       int kctx_len, int hash_sz,
			       unsigned int phys_dsgl)
{
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct ulp_txpkt *ulptx = (struct ulp_txpkt *)(wreq + 1);
	struct ulptx_idata *sc_imm = (struct ulptx_idata *)(ulptx + 1);
	int iv_loc = IV_DSGL;
	int qid = u_ctx->lldi.rxq_ids[ctx->tx_channel_id];
	unsigned int immdatalen = 0, nr_frags = 0;

	if (is_ofld_imm(skb)) {
		immdatalen = skb->data_len;
		iv_loc = IV_IMMEDIATE;
	} else {
		nr_frags = skb_shinfo(skb)->nr_frags;
	}

	wreq->op_to_cctx_size = FILL_WR_OP_CCTX_SIZE(immdatalen,
						     (kctx_len >> 4));
	wreq->pld_size_hash_size =
		htonl(FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE_V(sgl_lengths[nr_frags]) |
		      FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE_V(hash_sz));
	wreq->len16_pkd = htonl(FW_CRYPTO_LOOKASIDE_WR_LEN16_V(DIV_ROUND_UP(
				    (calc_tx_flits_ofld(skb) * 8), 16)));
	wreq->cookie = cpu_to_be64((uintptr_t)req);
	wreq->rx_chid_to_rx_q_id =
		FILL_WR_RX_Q_ID(ctx->dev->tx_channel_id, qid,
				(hash_sz) ? IV_NOP : iv_loc);

	ulptx->cmd_dest = FILL_ULPTX_CMD_DEST(ctx->dev->tx_channel_id);
	ulptx->len = htonl((DIV_ROUND_UP((calc_tx_flits_ofld(skb) * 8),
					 16) - ((sizeof(*wreq)) >> 4)));

	sc_imm->cmd_more = FILL_CMD_MORE(immdatalen);
	sc_imm->len = cpu_to_be32(sizeof(struct cpl_tx_sec_pdu) + kctx_len +
				  ((hash_sz) ? DUMMY_BYTES :
				  (sizeof(struct cpl_rx_phys_dsgl) +
				   phys_dsgl)) + immdatalen);
}

/**
 *	create_cipher_wr - form the WR for cipher operations
 *	@req: cipher req.
 *	@ctx: crypto driver context of the request.
 *	@qid: ingress qid where response of this WR should be received.
 *	@op_type:	encryption or decryption
 */
static struct sk_buff
*create_cipher_wr(struct crypto_async_request *req_base,
		  struct chcr_context *ctx, unsigned short qid,
		  unsigned short op_type)
{
	struct ablkcipher_request *req = (struct ablkcipher_request *)req_base;
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct ablk_ctx *ablkctx = ABLK_CTX(ctx);
	struct sk_buff *skb = NULL;
	struct _key_ctx *key_ctx;
	struct fw_crypto_lookaside_wr *wreq;
	struct cpl_tx_sec_pdu *sec_cpl;
	struct cpl_rx_phys_dsgl *phys_cpl;
	struct chcr_blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	struct phys_sge_parm sg_param;
	unsigned int frags = 0, transhdr_len, phys_dsgl, dst_bufsize = 0;
	unsigned int ivsize = crypto_ablkcipher_ivsize(tfm), kctx_len;

	if (!req->info)
		return ERR_PTR(-EINVAL);
	ablkctx->dst_nents = ch_nents(req->dst, &dst_bufsize);
	ablkctx->enc = op_type;

	if ((ablkctx->enckey_len == 0) || (ivsize > AES_BLOCK_SIZE) ||
	    (req->nbytes <= 0) || (req->nbytes % AES_BLOCK_SIZE))
		return ERR_PTR(-EINVAL);

	phys_dsgl = get_space_for_phys_dsgl(ablkctx->dst_nents);

	kctx_len = sizeof(*key_ctx) +
		(DIV_ROUND_UP(ablkctx->enckey_len, 16) * 16);
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, phys_dsgl);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)),
			GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);
	skb_reserve(skb, sizeof(struct sge_opaque_hdr));
	wreq = (struct fw_crypto_lookaside_wr *)__skb_put(skb, transhdr_len);

	sec_cpl = (struct cpl_tx_sec_pdu *)((u8 *)wreq + SEC_CPL_OFFSET);
	sec_cpl->op_ivinsrtofst =
		FILL_SEC_CPL_OP_IVINSR(ctx->dev->tx_channel_id, 2, 1, 1);

	sec_cpl->pldlen = htonl(ivsize + req->nbytes);
	sec_cpl->aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(0, 0,
								ivsize + 1, 0);

	sec_cpl->cipherstop_lo_authinsert =  FILL_SEC_CPL_AUTHINSERT(0, 0,
								     0, 0);
	sec_cpl->seqno_numivs = FILL_SEC_CPL_SCMD0_SEQNO(op_type, 0,
							 ablkctx->ciph_mode,
							 0, 0, ivsize >> 1, 1);
	sec_cpl->ivgen_hdrlen = FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 0,
							  0, 1, phys_dsgl);

	key_ctx = (struct _key_ctx *)((u8 *)sec_cpl + sizeof(*sec_cpl));
	key_ctx->ctx_hdr = ablkctx->key_ctx_hdr;
	if (op_type == CHCR_DECRYPT_OP) {
		if (generate_copy_rrkey(ablkctx, key_ctx))
			goto map_fail1;
	} else {
		if (ablkctx->ciph_mode == CHCR_SCMD_CIPHER_MODE_AES_CBC) {
			memcpy(key_ctx->key, ablkctx->key, ablkctx->enckey_len);
		} else {
			memcpy(key_ctx->key, ablkctx->key +
			       (ablkctx->enckey_len >> 1),
			       ablkctx->enckey_len >> 1);
			memcpy(key_ctx->key +
			       (ablkctx->enckey_len >> 1),
			       ablkctx->key,
			       ablkctx->enckey_len >> 1);
		}
	}
	phys_cpl = (struct cpl_rx_phys_dsgl *)((u8 *)key_ctx + kctx_len);

	memcpy(ablkctx->iv, req->info, ivsize);
	sg_init_table(&ablkctx->iv_sg, 1);
	sg_set_buf(&ablkctx->iv_sg, ablkctx->iv, ivsize);
	sg_param.nents = ablkctx->dst_nents;
	sg_param.obsize = dst_bufsize;
	sg_param.qid = qid;
	sg_param.align = 1;
	if (map_writesg_phys_cpl(&u_ctx->lldi.pdev->dev, phys_cpl, req->dst,
				 &sg_param))
		goto map_fail1;

	skb_set_transport_header(skb, transhdr_len);
	write_sg_data_page_desc(skb, &frags, &ablkctx->iv_sg, ivsize);
	write_sg_data_page_desc(skb, &frags, req->src, req->nbytes);
	create_wreq(ctx, wreq, req, skb, kctx_len, 0, phys_dsgl);
	req_ctx->skb = skb;
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
	struct ablkcipher_alg *alg = crypto_ablkcipher_alg(tfm);
	unsigned int ck_size, context_size;
	u16 alignment = 0;

	if ((keylen < alg->min_keysize) || (keylen > alg->max_keysize))
		goto badkey_err;

	memcpy(ablkctx->key, key, keylen);
	ablkctx->enckey_len = keylen;
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

int cxgb4_is_crypto_q_full(struct net_device *dev, unsigned int idx)
{
	int ret = 0;
	struct sge_ofld_txq *q;
	struct adapter *adap = netdev2adap(dev);

	local_bh_disable();
	q = &adap->sge.ofldtxq[idx];
	spin_lock(&q->sendq.lock);
	if (q->full)
		ret = -1;
	spin_unlock(&q->sendq.lock);
	local_bh_enable();
	return ret;
}

static int chcr_aes_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct chcr_context *ctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_async_request *req_base = &req->base;
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	skb = create_cipher_wr(req_base, ctx,
			       u_ctx->lldi.rxq_ids[ctx->tx_channel_id],
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
	struct crypto_async_request *req_base = &req->base;
	struct uld_ctx *u_ctx = ULD_CTX(ctx);
	struct sk_buff *skb;

	if (unlikely(cxgb4_is_crypto_q_full(u_ctx->lldi.ports[0],
					    ctx->tx_channel_id))) {
		if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			return -EBUSY;
	}

	skb = create_cipher_wr(req_base, ctx, u_ctx->lldi.rxq_ids[0],
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
		ctx->dev->tx_channel_id = 0;
		rxq_idx = ctx->dev->tx_channel_id * rxq_perchan;
		rxq_idx += id % rxq_perchan;
		spin_lock(&ctx->dev->lock_chcr_dev);
		ctx->tx_channel_id = rxq_idx;
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

static inline int
write_buffer_data_page_desc(struct chcr_ahash_req_ctx *req_ctx,
			    struct sk_buff *skb, unsigned int *frags, char *bfr,
			    u8 bfr_len)
{
	void *page_ptr = NULL;

	skb->len += bfr_len;
	skb->data_len += bfr_len;
	skb->truesize += bfr_len;
	page_ptr = kmalloc(CHCR_HASH_MAX_BLOCK_SIZE_128, GFP_ATOMIC | GFP_DMA);
	if (!page_ptr)
		return -ENOMEM;
	get_page(virt_to_page(page_ptr));
	req_ctx->dummy_payload_ptr = page_ptr;
	memcpy(page_ptr, bfr, bfr_len);
	skb_fill_page_desc(skb, *frags, virt_to_page(page_ptr),
			   offset_in_page(page_ptr), bfr_len);
	(*frags)++;
	return 0;
}

/**
 *	create_final_hash_wr - Create hash work request
 *	@req - Cipher req base
 */
static struct sk_buff *create_final_hash_wr(struct ahash_request *req,
					    struct hash_wr_param *param)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct chcr_context *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);
	struct sk_buff *skb = NULL;
	struct _key_ctx *key_ctx;
	struct fw_crypto_lookaside_wr *wreq;
	struct cpl_tx_sec_pdu *sec_cpl;
	unsigned int frags = 0, transhdr_len, iopad_alignment = 0;
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int kctx_len = sizeof(*key_ctx);
	u8 hash_size_in_response = 0;

	iopad_alignment = KEYCTX_ALIGN_PAD(digestsize);
	kctx_len += param->alg_prm.result_size + iopad_alignment;
	if (param->opad_needed)
		kctx_len += param->alg_prm.result_size + iopad_alignment;

	if (req_ctx->result)
		hash_size_in_response = digestsize;
	else
		hash_size_in_response = param->alg_prm.result_size;
	transhdr_len = HASH_TRANSHDR_SIZE(kctx_len);
	skb = alloc_skb((transhdr_len + sizeof(struct sge_opaque_hdr)),
			GFP_ATOMIC);
	if (!skb)
		return skb;

	skb_reserve(skb, sizeof(struct sge_opaque_hdr));
	wreq = (struct fw_crypto_lookaside_wr *)__skb_put(skb, transhdr_len);
	memset(wreq, 0, transhdr_len);

	sec_cpl = (struct cpl_tx_sec_pdu *)((u8 *)wreq + SEC_CPL_OFFSET);
	sec_cpl->op_ivinsrtofst =
		FILL_SEC_CPL_OP_IVINSR(ctx->dev->tx_channel_id, 2, 0, 0);
	sec_cpl->pldlen = htonl(param->bfr_len + param->sg_len);

	sec_cpl->aadstart_cipherstop_hi =
		FILL_SEC_CPL_CIPHERSTOP_HI(0, 0, 0, 0);
	sec_cpl->cipherstop_lo_authinsert =
		FILL_SEC_CPL_AUTHINSERT(0, 1, 0, 0);
	sec_cpl->seqno_numivs =
		FILL_SEC_CPL_SCMD0_SEQNO(0, 0, 0, param->alg_prm.auth_mode,
					 param->opad_needed, 0, 0);

	sec_cpl->ivgen_hdrlen =
		FILL_SEC_CPL_IVGEN_HDRLEN(param->last, param->more, 0, 1, 0, 0);

	key_ctx = (struct _key_ctx *)((u8 *)sec_cpl + sizeof(*sec_cpl));
	memcpy(key_ctx->key, req_ctx->partial_hash, param->alg_prm.result_size);

	if (param->opad_needed)
		memcpy(key_ctx->key + ((param->alg_prm.result_size <= 32) ? 32 :
				       CHCR_HASH_MAX_DIGEST_SIZE),
		       hmacctx->opad, param->alg_prm.result_size);

	key_ctx->ctx_hdr = FILL_KEY_CTX_HDR(CHCR_KEYCTX_NO_KEY,
					    param->alg_prm.mk_size, 0,
					    param->opad_needed,
					    (kctx_len >> 4));
	sec_cpl->scmd1 = cpu_to_be64((u64)param->scmd1);

	skb_set_transport_header(skb, transhdr_len);
	if (param->bfr_len != 0)
		write_buffer_data_page_desc(req_ctx, skb, &frags, req_ctx->bfr,
					    param->bfr_len);
	if (param->sg_len != 0)
		write_sg_data_page_desc(skb, &frags, req->src, param->sg_len);

	create_wreq(ctx, wreq, req, skb, kctx_len, hash_size_in_response,
		    0);
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

	if (nbytes + req_ctx->bfr_len >= bs) {
		remainder = (nbytes + req_ctx->bfr_len) % bs;
		nbytes = nbytes + req_ctx->bfr_len - remainder;
	} else {
		sg_pcopy_to_buffer(req->src, sg_nents(req->src), req_ctx->bfr +
				   req_ctx->bfr_len, nbytes, 0);
		req_ctx->bfr_len += nbytes;
		return 0;
	}

	params.opad_needed = 0;
	params.more = 1;
	params.last = 0;
	params.sg_len = nbytes - req_ctx->bfr_len;
	params.bfr_len = req_ctx->bfr_len;
	params.scmd1 = 0;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	req_ctx->result = 0;
	req_ctx->data_len += params.sg_len + params.bfr_len;
	skb = create_final_hash_wr(req, &params);
	if (!skb)
		return -ENOMEM;

	req_ctx->bfr_len = remainder;
	if (remainder)
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   req_ctx->bfr, remainder, req->nbytes -
				   remainder);
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
	params.bfr_len = req_ctx->bfr_len;
	req_ctx->data_len += params.bfr_len + params.sg_len;
	if (req_ctx->bfr && (req_ctx->bfr_len == 0)) {
		create_last_hash_block(req_ctx->bfr, bs, req_ctx->data_len);
		params.last = 0;
		params.more = 1;
		params.scmd1 = 0;
		params.bfr_len = bs;

	} else {
		params.scmd1 = req_ctx->data_len;
		params.last = 1;
		params.more = 0;
	}
	skb = create_final_hash_wr(req, &params);
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
	params.bfr_len = req_ctx->bfr_len;
	get_alg_config(&params.alg_prm, crypto_ahash_digestsize(rtfm));
	req_ctx->data_len += params.bfr_len + params.sg_len;
	req_ctx->result = 1;
	if (req_ctx->bfr && (req_ctx->bfr_len + req->nbytes) == 0) {
		create_last_hash_block(req_ctx->bfr, bs, req_ctx->data_len);
		params.last = 0;
		params.more = 1;
		params.scmd1 = 0;
		params.bfr_len = bs;
	} else {
		params.scmd1 = req_ctx->data_len;
		params.last = 1;
		params.more = 0;
	}

	skb = create_final_hash_wr(req, &params);
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

	if (req_ctx->bfr && req->nbytes == 0) {
		create_last_hash_block(req_ctx->bfr, bs, 0);
		params.more = 1;
		params.bfr_len = bs;
	}

	skb = create_final_hash_wr(req, &params);
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

	state->bfr_len = req_ctx->bfr_len;
	state->data_len = req_ctx->data_len;
	memcpy(state->bfr, req_ctx->bfr, CHCR_HASH_MAX_BLOCK_SIZE_128);
	memcpy(state->partial_hash, req_ctx->partial_hash,
	       CHCR_HASH_MAX_DIGEST_SIZE);
	return 0;
}

static int chcr_ahash_import(struct ahash_request *areq, const void *in)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct chcr_ahash_req_ctx *state = (struct chcr_ahash_req_ctx *)in;

	req_ctx->bfr_len = state->bfr_len;
	req_ctx->data_len = state->data_len;
	req_ctx->dummy_payload_ptr = NULL;
	memcpy(req_ctx->bfr, state->bfr, CHCR_HASH_MAX_BLOCK_SIZE_128);
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

	/*
	 * use the key to calculate the ipad and opad. ipad will sent with the
	 * first request's data. opad will be sent with the final hash result
	 * ipad in hmacctx->ipad and opad in hmacctx->opad location
	 */
	if (!hmacctx->desc)
		return -EINVAL;
	if (keylen > bs) {
		err = crypto_shash_digest(hmacctx->desc, key, keylen,
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
	err = chcr_compute_partial_hash(hmacctx->desc, hmacctx->ipad,
					hmacctx->ipad, digestsize);
	if (err)
		goto out;
	chcr_change_order(hmacctx->ipad, updated_digestsize);

	err = chcr_compute_partial_hash(hmacctx->desc, hmacctx->opad,
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
	int status = 0;
	unsigned short context_size = 0;

	if ((key_len == (AES_KEYSIZE_128 << 1)) ||
	    (key_len == (AES_KEYSIZE_256 << 1))) {
		memcpy(ablkctx->key, key, key_len);
		ablkctx->enckey_len = key_len;
		context_size = (KEY_CONTEXT_HDR_SALT_AND_PAD + key_len) >> 4;
		ablkctx->key_ctx_hdr =
			FILL_KEY_CTX_HDR((key_len == AES_KEYSIZE_256) ?
					 CHCR_KEYCTX_CIPHER_KEY_SIZE_128 :
					 CHCR_KEYCTX_CIPHER_KEY_SIZE_256,
					 CHCR_KEYCTX_NO_KEY, 1,
					 0, context_size);
		ablkctx->ciph_mode = CHCR_SCMD_CIPHER_MODE_AES_XTS;
	} else {
		crypto_tfm_set_flags((struct crypto_tfm *)tfm,
				     CRYPTO_TFM_RES_BAD_KEY_LEN);
		ablkctx->enckey_len = 0;
		status = -EINVAL;
	}
	return status;
}

static int chcr_sha_init(struct ahash_request *areq)
{
	struct chcr_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	int digestsize =  crypto_ahash_digestsize(tfm);

	req_ctx->data_len = 0;
	req_ctx->dummy_payload_ptr = NULL;
	req_ctx->bfr_len = 0;
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
	hmacctx->desc = chcr_alloc_shash(digestsize);
	if (IS_ERR(hmacctx->desc))
		return PTR_ERR(hmacctx->desc);
	return chcr_device_init(crypto_tfm_ctx(tfm));
}

static void chcr_free_shash(struct shash_desc *desc)
{
	crypto_free_shash(desc->tfm);
	kfree(desc);
}

static void chcr_hmac_cra_exit(struct crypto_tfm *tfm)
{
	struct chcr_context *ctx = crypto_tfm_ctx(tfm);
	struct hmac_ctx *hmacctx = HMAC_CTX(ctx);

	if (hmacctx->desc) {
		chcr_free_shash(hmacctx->desc);
		hmacctx->desc = NULL;
	}
}

static struct chcr_alg_template driver_algs[] = {
	/* AES-CBC */
	{
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.is_registered = 0,
		.alg.crypto = {
			.cra_name		= "cbc(aes)",
			.cra_driver_name	= "cbc(aes-chcr)",
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
			.cra_driver_name	= "xts(aes-chcr)",
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
				.cra_driver_name = "hmac(sha1-chcr)",
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
				.cra_driver_name = "hmac(sha224-chcr)",
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
				.cra_driver_name = "hmac(sha256-chcr)",
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
				.cra_driver_name = "hmac(sha384-chcr)",
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
				.cra_driver_name = "hmac(sha512-chcr)",
				.cra_blocksize = SHA512_BLOCK_SIZE,
			}
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
