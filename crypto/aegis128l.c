/*
 * The AEGIS-128L Authenticated-Encryption Algorithm
 *
 * Copyright (c) 2017-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>

#include "aegis.h"

#define AEGIS128L_CHUNK_BLOCKS 2
#define AEGIS128L_CHUNK_SIZE (AEGIS128L_CHUNK_BLOCKS * AEGIS_BLOCK_SIZE)
#define AEGIS128L_NONCE_SIZE 16
#define AEGIS128L_STATE_BLOCKS 8
#define AEGIS128L_KEY_SIZE 16
#define AEGIS128L_MIN_AUTH_SIZE 8
#define AEGIS128L_MAX_AUTH_SIZE 16

union aegis_chunk {
	union aegis_block blocks[AEGIS128L_CHUNK_BLOCKS];
	u8 bytes[AEGIS128L_CHUNK_SIZE];
};

struct aegis_state {
	union aegis_block blocks[AEGIS128L_STATE_BLOCKS];
};

struct aegis_ctx {
	union aegis_block key;
};

struct aegis128l_ops {
	int (*skcipher_walk_init)(struct skcipher_walk *walk,
				  struct aead_request *req, bool atomic);

	void (*crypt_chunk)(struct aegis_state *state, u8 *dst,
			    const u8 *src, unsigned int size);
};

static void crypto_aegis128l_update(struct aegis_state *state)
{
	union aegis_block tmp;
	unsigned int i;

	tmp = state->blocks[AEGIS128L_STATE_BLOCKS - 1];
	for (i = AEGIS128L_STATE_BLOCKS - 1; i > 0; i--)
		crypto_aegis_aesenc(&state->blocks[i], &state->blocks[i - 1],
				    &state->blocks[i]);
	crypto_aegis_aesenc(&state->blocks[0], &tmp, &state->blocks[0]);
}

static void crypto_aegis128l_update_a(struct aegis_state *state,
				      const union aegis_chunk *msg)
{
	crypto_aegis128l_update(state);
	crypto_aegis_block_xor(&state->blocks[0], &msg->blocks[0]);
	crypto_aegis_block_xor(&state->blocks[4], &msg->blocks[1]);
}

static void crypto_aegis128l_update_u(struct aegis_state *state,
				      const void *msg)
{
	crypto_aegis128l_update(state);
	crypto_xor(state->blocks[0].bytes, msg + 0 * AEGIS_BLOCK_SIZE,
			AEGIS_BLOCK_SIZE);
	crypto_xor(state->blocks[4].bytes, msg + 1 * AEGIS_BLOCK_SIZE,
			AEGIS_BLOCK_SIZE);
}

static void crypto_aegis128l_init(struct aegis_state *state,
				  const union aegis_block *key,
				  const u8 *iv)
{
	union aegis_block key_iv;
	union aegis_chunk chunk;
	unsigned int i;

	memcpy(chunk.blocks[0].bytes, iv, AEGIS_BLOCK_SIZE);
	chunk.blocks[1] = *key;

	key_iv = *key;
	crypto_aegis_block_xor(&key_iv, &chunk.blocks[0]);

	state->blocks[0] = key_iv;
	state->blocks[1] = crypto_aegis_const[1];
	state->blocks[2] = crypto_aegis_const[0];
	state->blocks[3] = crypto_aegis_const[1];
	state->blocks[4] = key_iv;
	state->blocks[5] = *key;
	state->blocks[6] = *key;
	state->blocks[7] = *key;

	crypto_aegis_block_xor(&state->blocks[5], &crypto_aegis_const[0]);
	crypto_aegis_block_xor(&state->blocks[6], &crypto_aegis_const[1]);
	crypto_aegis_block_xor(&state->blocks[7], &crypto_aegis_const[0]);

	for (i = 0; i < 10; i++) {
		crypto_aegis128l_update_a(state, &chunk);
	}
}

static void crypto_aegis128l_ad(struct aegis_state *state,
				const u8 *src, unsigned int size)
{
	if (AEGIS_ALIGNED(src)) {
		const union aegis_chunk *src_chunk =
				(const union aegis_chunk *)src;

		while (size >= AEGIS128L_CHUNK_SIZE) {
                    crypto_aegis128l_update_a(state, src_chunk);

			size -= AEGIS128L_CHUNK_SIZE;
			src_chunk += 1;
		}
	} else {
		while (size >= AEGIS128L_CHUNK_SIZE) {
			crypto_aegis128l_update_u(state, src);

			size -= AEGIS128L_CHUNK_SIZE;
			src += AEGIS128L_CHUNK_SIZE;
		}
	}
}

static void crypto_aegis128l_encrypt_chunk(struct aegis_state *state, u8 *dst,
					   const u8 *src, unsigned int size)
{
	union aegis_chunk tmp;
	union aegis_block *tmp0 = &tmp.blocks[0];
	union aegis_block *tmp1 = &tmp.blocks[1];

	if (AEGIS_ALIGNED(src) && AEGIS_ALIGNED(dst)) {
		while (size >= AEGIS128L_CHUNK_SIZE) {
			union aegis_chunk *dst_blk =
					(union aegis_chunk *)dst;
			const union aegis_chunk *src_blk =
					(const union aegis_chunk *)src;

			*tmp0 = state->blocks[2];
			crypto_aegis_block_and(tmp0, &state->blocks[3]);
			crypto_aegis_block_xor(tmp0, &state->blocks[6]);
			crypto_aegis_block_xor(tmp0, &state->blocks[1]);
			crypto_aegis_block_xor(tmp0, &src_blk->blocks[0]);

			*tmp1 = state->blocks[6];
			crypto_aegis_block_and(tmp1, &state->blocks[7]);
			crypto_aegis_block_xor(tmp1, &state->blocks[5]);
			crypto_aegis_block_xor(tmp1, &state->blocks[2]);
			crypto_aegis_block_xor(tmp1, &src_blk->blocks[1]);

			crypto_aegis128l_update_a(state, src_blk);

			*dst_blk = tmp;

			size -= AEGIS128L_CHUNK_SIZE;
			src += AEGIS128L_CHUNK_SIZE;
			dst += AEGIS128L_CHUNK_SIZE;
		}
	} else {
		while (size >= AEGIS128L_CHUNK_SIZE) {
			*tmp0 = state->blocks[2];
			crypto_aegis_block_and(tmp0, &state->blocks[3]);
			crypto_aegis_block_xor(tmp0, &state->blocks[6]);
			crypto_aegis_block_xor(tmp0, &state->blocks[1]);
			crypto_xor(tmp0->bytes, src + 0 * AEGIS_BLOCK_SIZE,
				   AEGIS_BLOCK_SIZE);

			*tmp1 = state->blocks[6];
			crypto_aegis_block_and(tmp1, &state->blocks[7]);
			crypto_aegis_block_xor(tmp1, &state->blocks[5]);
			crypto_aegis_block_xor(tmp1, &state->blocks[2]);
			crypto_xor(tmp1->bytes, src + 1 * AEGIS_BLOCK_SIZE,
				   AEGIS_BLOCK_SIZE);

			crypto_aegis128l_update_u(state, src);

			memcpy(dst, tmp.bytes, AEGIS128L_CHUNK_SIZE);

			size -= AEGIS128L_CHUNK_SIZE;
			src += AEGIS128L_CHUNK_SIZE;
			dst += AEGIS128L_CHUNK_SIZE;
		}
	}

	if (size > 0) {
		union aegis_chunk msg = {};
		memcpy(msg.bytes, src, size);

		*tmp0 = state->blocks[2];
		crypto_aegis_block_and(tmp0, &state->blocks[3]);
		crypto_aegis_block_xor(tmp0, &state->blocks[6]);
		crypto_aegis_block_xor(tmp0, &state->blocks[1]);

		*tmp1 = state->blocks[6];
		crypto_aegis_block_and(tmp1, &state->blocks[7]);
		crypto_aegis_block_xor(tmp1, &state->blocks[5]);
		crypto_aegis_block_xor(tmp1, &state->blocks[2]);

		crypto_aegis128l_update_a(state, &msg);

		crypto_aegis_block_xor(&msg.blocks[0], tmp0);
		crypto_aegis_block_xor(&msg.blocks[1], tmp1);

		memcpy(dst, msg.bytes, size);
	}
}

static void crypto_aegis128l_decrypt_chunk(struct aegis_state *state, u8 *dst,
					   const u8 *src, unsigned int size)
{
	union aegis_chunk tmp;
	union aegis_block *tmp0 = &tmp.blocks[0];
	union aegis_block *tmp1 = &tmp.blocks[1];

	if (AEGIS_ALIGNED(src) && AEGIS_ALIGNED(dst)) {
		while (size >= AEGIS128L_CHUNK_SIZE) {
			union aegis_chunk *dst_blk =
					(union aegis_chunk *)dst;
			const union aegis_chunk *src_blk =
					(const union aegis_chunk *)src;

			*tmp0 = state->blocks[2];
			crypto_aegis_block_and(tmp0, &state->blocks[3]);
			crypto_aegis_block_xor(tmp0, &state->blocks[6]);
			crypto_aegis_block_xor(tmp0, &state->blocks[1]);
			crypto_aegis_block_xor(tmp0, &src_blk->blocks[0]);

			*tmp1 = state->blocks[6];
			crypto_aegis_block_and(tmp1, &state->blocks[7]);
			crypto_aegis_block_xor(tmp1, &state->blocks[5]);
			crypto_aegis_block_xor(tmp1, &state->blocks[2]);
			crypto_aegis_block_xor(tmp1, &src_blk->blocks[1]);

			crypto_aegis128l_update_a(state, &tmp);

			*dst_blk = tmp;

			size -= AEGIS128L_CHUNK_SIZE;
			src += AEGIS128L_CHUNK_SIZE;
			dst += AEGIS128L_CHUNK_SIZE;
		}
	} else {
		while (size >= AEGIS128L_CHUNK_SIZE) {
			*tmp0 = state->blocks[2];
			crypto_aegis_block_and(tmp0, &state->blocks[3]);
			crypto_aegis_block_xor(tmp0, &state->blocks[6]);
			crypto_aegis_block_xor(tmp0, &state->blocks[1]);
			crypto_xor(tmp0->bytes, src + 0 * AEGIS_BLOCK_SIZE,
				   AEGIS_BLOCK_SIZE);

			*tmp1 = state->blocks[6];
			crypto_aegis_block_and(tmp1, &state->blocks[7]);
			crypto_aegis_block_xor(tmp1, &state->blocks[5]);
			crypto_aegis_block_xor(tmp1, &state->blocks[2]);
			crypto_xor(tmp1->bytes, src + 1 * AEGIS_BLOCK_SIZE,
				   AEGIS_BLOCK_SIZE);

			crypto_aegis128l_update_a(state, &tmp);

			memcpy(dst, tmp.bytes, AEGIS128L_CHUNK_SIZE);

			size -= AEGIS128L_CHUNK_SIZE;
			src += AEGIS128L_CHUNK_SIZE;
			dst += AEGIS128L_CHUNK_SIZE;
		}
	}

	if (size > 0) {
		union aegis_chunk msg = {};
		memcpy(msg.bytes, src, size);

		*tmp0 = state->blocks[2];
		crypto_aegis_block_and(tmp0, &state->blocks[3]);
		crypto_aegis_block_xor(tmp0, &state->blocks[6]);
		crypto_aegis_block_xor(tmp0, &state->blocks[1]);
		crypto_aegis_block_xor(&msg.blocks[0], tmp0);

		*tmp1 = state->blocks[6];
		crypto_aegis_block_and(tmp1, &state->blocks[7]);
		crypto_aegis_block_xor(tmp1, &state->blocks[5]);
		crypto_aegis_block_xor(tmp1, &state->blocks[2]);
		crypto_aegis_block_xor(&msg.blocks[1], tmp1);

		memset(msg.bytes + size, 0, AEGIS128L_CHUNK_SIZE - size);

		crypto_aegis128l_update_a(state, &msg);

		memcpy(dst, msg.bytes, size);
	}
}

static void crypto_aegis128l_process_ad(struct aegis_state *state,
					struct scatterlist *sg_src,
					unsigned int assoclen)
{
	struct scatter_walk walk;
	union aegis_chunk buf;
	unsigned int pos = 0;

	scatterwalk_start(&walk, sg_src);
	while (assoclen != 0) {
		unsigned int size = scatterwalk_clamp(&walk, assoclen);
		unsigned int left = size;
		void *mapped = scatterwalk_map(&walk);
		const u8 *src = (const u8 *)mapped;

		if (pos + size >= AEGIS128L_CHUNK_SIZE) {
			if (pos > 0) {
				unsigned int fill = AEGIS128L_CHUNK_SIZE - pos;
				memcpy(buf.bytes + pos, src, fill);
				crypto_aegis128l_update_a(state, &buf);
				pos = 0;
				left -= fill;
				src += fill;
			}

			crypto_aegis128l_ad(state, src, left);
			src += left & ~(AEGIS128L_CHUNK_SIZE - 1);
			left &= AEGIS128L_CHUNK_SIZE - 1;
		}

		memcpy(buf.bytes + pos, src, left);

		pos += left;
		assoclen -= size;
		scatterwalk_unmap(mapped);
		scatterwalk_advance(&walk, size);
		scatterwalk_done(&walk, 0, assoclen);
	}

	if (pos > 0) {
		memset(buf.bytes + pos, 0, AEGIS128L_CHUNK_SIZE - pos);
		crypto_aegis128l_update_a(state, &buf);
	}
}

static void crypto_aegis128l_process_crypt(struct aegis_state *state,
					   struct aead_request *req,
					   const struct aegis128l_ops *ops)
{
	struct skcipher_walk walk;
	u8 *src, *dst;
	unsigned int chunksize;

	ops->skcipher_walk_init(&walk, req, false);

	while (walk.nbytes) {
		src = walk.src.virt.addr;
		dst = walk.dst.virt.addr;
		chunksize = walk.nbytes;

		ops->crypt_chunk(state, dst, src, chunksize);

		skcipher_walk_done(&walk, 0);
	}
}

static void crypto_aegis128l_final(struct aegis_state *state,
				   union aegis_block *tag_xor,
				   u64 assoclen, u64 cryptlen)
{
	u64 assocbits = assoclen * 8;
	u64 cryptbits = cryptlen * 8;

	union aegis_chunk tmp;
	unsigned int i;

	tmp.blocks[0].words64[0] = cpu_to_le64(assocbits);
	tmp.blocks[0].words64[1] = cpu_to_le64(cryptbits);

	crypto_aegis_block_xor(&tmp.blocks[0], &state->blocks[2]);

	tmp.blocks[1] = tmp.blocks[0];
	for (i = 0; i < 7; i++)
		crypto_aegis128l_update_a(state, &tmp);

	for (i = 0; i < 7; i++)
		crypto_aegis_block_xor(tag_xor, &state->blocks[i]);
}

static int crypto_aegis128l_setkey(struct crypto_aead *aead, const u8 *key,
				   unsigned int keylen)
{
	struct aegis_ctx *ctx = crypto_aead_ctx(aead);

	if (keylen != AEGIS128L_KEY_SIZE) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key.bytes, key, AEGIS128L_KEY_SIZE);
	return 0;
}

static int crypto_aegis128l_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	if (authsize > AEGIS128L_MAX_AUTH_SIZE)
		return -EINVAL;
	if (authsize < AEGIS128L_MIN_AUTH_SIZE)
		return -EINVAL;
	return 0;
}

static void crypto_aegis128l_crypt(struct aead_request *req,
				   union aegis_block *tag_xor,
				   unsigned int cryptlen,
				   const struct aegis128l_ops *ops)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aegis_ctx *ctx = crypto_aead_ctx(tfm);
	struct aegis_state state;

	crypto_aegis128l_init(&state, &ctx->key, req->iv);
	crypto_aegis128l_process_ad(&state, req->src, req->assoclen);
	crypto_aegis128l_process_crypt(&state, req, ops);
	crypto_aegis128l_final(&state, tag_xor, req->assoclen, cryptlen);
}

static int crypto_aegis128l_encrypt(struct aead_request *req)
{
	static const struct aegis128l_ops ops = {
		.skcipher_walk_init = skcipher_walk_aead_encrypt,
		.crypt_chunk = crypto_aegis128l_encrypt_chunk,
	};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	union aegis_block tag = {};
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen;

	crypto_aegis128l_crypt(req, &tag, cryptlen, &ops);

	scatterwalk_map_and_copy(tag.bytes, req->dst, req->assoclen + cryptlen,
				 authsize, 1);
	return 0;
}

static int crypto_aegis128l_decrypt(struct aead_request *req)
{
	static const struct aegis128l_ops ops = {
		.skcipher_walk_init = skcipher_walk_aead_decrypt,
		.crypt_chunk = crypto_aegis128l_decrypt_chunk,
	};
	static const u8 zeros[AEGIS128L_MAX_AUTH_SIZE] = {};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	union aegis_block tag;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen - authsize;

	scatterwalk_map_and_copy(tag.bytes, req->src, req->assoclen + cryptlen,
				 authsize, 0);

	crypto_aegis128l_crypt(req, &tag, cryptlen, &ops);

	return crypto_memneq(tag.bytes, zeros, authsize) ? -EBADMSG : 0;
}

static int crypto_aegis128l_init_tfm(struct crypto_aead *tfm)
{
	return 0;
}

static void crypto_aegis128l_exit_tfm(struct crypto_aead *tfm)
{
}

static struct aead_alg crypto_aegis128l_alg = {
	.setkey = crypto_aegis128l_setkey,
	.setauthsize = crypto_aegis128l_setauthsize,
	.encrypt = crypto_aegis128l_encrypt,
	.decrypt = crypto_aegis128l_decrypt,
	.init = crypto_aegis128l_init_tfm,
	.exit = crypto_aegis128l_exit_tfm,

	.ivsize = AEGIS128L_NONCE_SIZE,
	.maxauthsize = AEGIS128L_MAX_AUTH_SIZE,
	.chunksize = AEGIS128L_CHUNK_SIZE,

	.base = {
		.cra_flags = CRYPTO_ALG_TYPE_AEAD,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct aegis_ctx),
		.cra_alignmask = 0,

		.cra_priority = 100,

		.cra_name = "aegis128l",
		.cra_driver_name = "aegis128l-generic",

		.cra_module = THIS_MODULE,
	}
};

static int __init crypto_aegis128l_module_init(void)
{
	return crypto_register_aead(&crypto_aegis128l_alg);
}

static void __exit crypto_aegis128l_module_exit(void)
{
	crypto_unregister_aead(&crypto_aegis128l_alg);
}

module_init(crypto_aegis128l_module_init);
module_exit(crypto_aegis128l_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("AEGIS-128L AEAD algorithm");
MODULE_ALIAS_CRYPTO("aegis128l");
MODULE_ALIAS_CRYPTO("aegis128l-generic");
