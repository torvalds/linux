/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
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
 * This file implements the Linux crypto algorithms.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/spinlock_types.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/hash.h>
#include <crypto/internal/hash.h>
#include <linux/dma-mapping.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/sha.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_sa.h"
#include "crypto4xx_core.h"

static void set_dynamic_sa_command_0(struct dynamic_sa_ctl *sa, u32 save_h,
				     u32 save_iv, u32 ld_h, u32 ld_iv,
				     u32 hdr_proc, u32 h, u32 c, u32 pad_type,
				     u32 op_grp, u32 op, u32 dir)
{
	sa->sa_command_0.w = 0;
	sa->sa_command_0.bf.save_hash_state = save_h;
	sa->sa_command_0.bf.save_iv = save_iv;
	sa->sa_command_0.bf.load_hash_state = ld_h;
	sa->sa_command_0.bf.load_iv = ld_iv;
	sa->sa_command_0.bf.hdr_proc = hdr_proc;
	sa->sa_command_0.bf.hash_alg = h;
	sa->sa_command_0.bf.cipher_alg = c;
	sa->sa_command_0.bf.pad_type = pad_type & 3;
	sa->sa_command_0.bf.extend_pad = pad_type >> 2;
	sa->sa_command_0.bf.op_group = op_grp;
	sa->sa_command_0.bf.opcode = op;
	sa->sa_command_0.bf.dir = dir;
}

static void set_dynamic_sa_command_1(struct dynamic_sa_ctl *sa, u32 cm,
				     u32 hmac_mc, u32 cfb, u32 esn,
				     u32 sn_mask, u32 mute, u32 cp_pad,
				     u32 cp_pay, u32 cp_hdr)
{
	sa->sa_command_1.w = 0;
	sa->sa_command_1.bf.crypto_mode31 = (cm & 4) >> 2;
	sa->sa_command_1.bf.crypto_mode9_8 = cm & 3;
	sa->sa_command_1.bf.feedback_mode = cfb,
	sa->sa_command_1.bf.sa_rev = 1;
	sa->sa_command_1.bf.extended_seq_num = esn;
	sa->sa_command_1.bf.seq_num_mask = sn_mask;
	sa->sa_command_1.bf.mutable_bit_proc = mute;
	sa->sa_command_1.bf.copy_pad = cp_pad;
	sa->sa_command_1.bf.copy_payload = cp_pay;
	sa->sa_command_1.bf.copy_hdr = cp_hdr;
}

int crypto4xx_encrypt(struct ablkcipher_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	ctx->direction = DIR_OUTBOUND;
	ctx->hash_final = 0;
	ctx->is_hash = 0;
	ctx->pd_ctl = 0x1;

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
				  req->nbytes, req->info,
				  get_dynamic_sa_iv_size(ctx));
}

int crypto4xx_decrypt(struct ablkcipher_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	ctx->direction = DIR_INBOUND;
	ctx->hash_final = 0;
	ctx->is_hash = 0;
	ctx->pd_ctl = 1;

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
				  req->nbytes, req->info,
				  get_dynamic_sa_iv_size(ctx));
}

/**
 * AES Functions
 */
static int crypto4xx_setkey_aes(struct crypto_ablkcipher *cipher,
				const u8 *key,
				unsigned int keylen,
				unsigned char cm,
				u8 fb)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_ctl *sa;
	int    rc;

	if (keylen != AES_KEYSIZE_256 &&
		keylen != AES_KEYSIZE_192 && keylen != AES_KEYSIZE_128) {
		crypto_ablkcipher_set_flags(cipher,
				CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	/* Create SA */
	if (ctx->sa_in_dma_addr || ctx->sa_out_dma_addr)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, SA_AES128_LEN + (keylen-16) / 4);
	if (rc)
		return rc;

	if (ctx->state_record_dma_addr == 0) {
		rc = crypto4xx_alloc_state_record(ctx);
		if (rc) {
			crypto4xx_free_sa(ctx);
			return rc;
		}
	}
	/* Setup SA */
	sa = (struct dynamic_sa_ctl *) ctx->sa_in;
	ctx->hash_final = 0;

	set_dynamic_sa_command_0(sa, SA_NOT_SAVE_HASH, SA_NOT_SAVE_IV,
				 SA_LOAD_HASH_FROM_SA, SA_LOAD_IV_FROM_STATE,
				 SA_NO_HEADER_PROC, SA_HASH_ALG_NULL,
				 SA_CIPHER_ALG_AES, SA_PAD_TYPE_ZERO,
				 SA_OP_GROUP_BASIC, SA_OPCODE_DECRYPT,
				 DIR_INBOUND);

	set_dynamic_sa_command_1(sa, cm, SA_HASH_MODE_HASH,
				 fb, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_OFF, SA_MC_ENABLE,
				 SA_NOT_COPY_PAD, SA_NOT_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);
	crypto4xx_memcpy_le(ctx->sa_in + get_dynamic_sa_offset_key_field(ctx),
			    key, keylen);
	sa->sa_contents = SA_AES_CONTENTS | (keylen << 2);
	sa->sa_command_1.bf.key_len = keylen >> 3;
	ctx->is_hash = 0;
	ctx->direction = DIR_INBOUND;
	memcpy(ctx->sa_in + get_dynamic_sa_offset_state_ptr_field(ctx),
			(void *)&ctx->state_record_dma_addr, 4);
	ctx->offset_to_sr_ptr = get_dynamic_sa_offset_state_ptr_field(ctx);

	memcpy(ctx->sa_out, ctx->sa_in, ctx->sa_len * 4);
	sa = (struct dynamic_sa_ctl *) ctx->sa_out;
	sa->sa_command_0.bf.dir = DIR_OUTBOUND;

	return 0;
}

int crypto4xx_setkey_aes_cbc(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	return crypto4xx_setkey_aes(cipher, key, keylen, CRYPTO_MODE_CBC,
				    CRYPTO_FEEDBACK_MODE_NO_FB);
}

/**
 * HASH SHA1 Functions
 */
static int crypto4xx_hash_alg_init(struct crypto_tfm *tfm,
				   unsigned int sa_len,
				   unsigned char ha,
				   unsigned char hm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct crypto4xx_alg *my_alg = crypto_alg_to_crypto4xx_alg(alg);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_ctl *sa;
	struct dynamic_sa_hash160 *sa_in;
	int rc;

	ctx->dev   = my_alg->dev;
	ctx->is_hash = 1;
	ctx->hash_final = 0;

	/* Create SA */
	if (ctx->sa_in_dma_addr || ctx->sa_out_dma_addr)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, sa_len);
	if (rc)
		return rc;

	if (ctx->state_record_dma_addr == 0) {
		crypto4xx_alloc_state_record(ctx);
		if (!ctx->state_record_dma_addr) {
			crypto4xx_free_sa(ctx);
			return -ENOMEM;
		}
	}

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct crypto4xx_ctx));
	sa = (struct dynamic_sa_ctl *) ctx->sa_in;
	set_dynamic_sa_command_0(sa, SA_SAVE_HASH, SA_NOT_SAVE_IV,
				 SA_NOT_LOAD_HASH, SA_LOAD_IV_FROM_SA,
				 SA_NO_HEADER_PROC, ha, SA_CIPHER_ALG_NULL,
				 SA_PAD_TYPE_ZERO, SA_OP_GROUP_BASIC,
				 SA_OPCODE_HASH, DIR_INBOUND);
	set_dynamic_sa_command_1(sa, 0, SA_HASH_MODE_HASH,
				 CRYPTO_FEEDBACK_MODE_NO_FB, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_OFF, SA_MC_ENABLE,
				 SA_NOT_COPY_PAD, SA_NOT_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);
	ctx->direction = DIR_INBOUND;
	sa->sa_contents = SA_HASH160_CONTENTS;
	sa_in = (struct dynamic_sa_hash160 *) ctx->sa_in;
	/* Need to zero hash digest in SA */
	memset(sa_in->inner_digest, 0, sizeof(sa_in->inner_digest));
	memset(sa_in->outer_digest, 0, sizeof(sa_in->outer_digest));
	sa_in->state_ptr = ctx->state_record_dma_addr;
	ctx->offset_to_sr_ptr = get_dynamic_sa_offset_state_ptr_field(ctx);

	return 0;
}

int crypto4xx_hash_init(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	int ds;
	struct dynamic_sa_ctl *sa;

	sa = (struct dynamic_sa_ctl *) ctx->sa_in;
	ds = crypto_ahash_digestsize(
			__crypto_ahash_cast(req->base.tfm));
	sa->sa_command_0.bf.digest_len = ds >> 2;
	sa->sa_command_0.bf.load_hash_state = SA_LOAD_HASH_FROM_SA;
	ctx->is_hash = 1;
	ctx->direction = DIR_INBOUND;

	return 0;
}

int crypto4xx_hash_update(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	ctx->is_hash = 1;
	ctx->hash_final = 0;
	ctx->pd_ctl = 0x11;
	ctx->direction = DIR_INBOUND;

	return crypto4xx_build_pd(&req->base, ctx, req->src,
				  (struct scatterlist *) req->result,
				  req->nbytes, NULL, 0);
}

int crypto4xx_hash_final(struct ahash_request *req)
{
	return 0;
}

int crypto4xx_hash_digest(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	ctx->hash_final = 1;
	ctx->pd_ctl = 0x11;
	ctx->direction = DIR_INBOUND;

	return crypto4xx_build_pd(&req->base, ctx, req->src,
				  (struct scatterlist *) req->result,
				  req->nbytes, NULL, 0);
}

/**
 * SHA1 Algorithm
 */
int crypto4xx_sha1_alg_init(struct crypto_tfm *tfm)
{
	return crypto4xx_hash_alg_init(tfm, SA_HASH160_LEN, SA_HASH_ALG_SHA1,
				       SA_HASH_MODE_HASH);
}


