/*
 * The MORUS-640 Authenticated-Encryption Algorithm
 *
 * Copyright (c) 2016-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/morus_common.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>

#define MORUS640_WORD_SIZE 4
#define MORUS640_BLOCK_SIZE (MORUS_BLOCK_WORDS * MORUS640_WORD_SIZE)
#define MORUS640_BLOCK_ALIGN (__alignof__(__le32))
#define MORUS640_ALIGNED(p) IS_ALIGNED((uintptr_t)p, MORUS640_BLOCK_ALIGN)

struct morus640_block {
	u32 words[MORUS_BLOCK_WORDS];
};

union morus640_block_in {
	__le32 words[MORUS_BLOCK_WORDS];
	u8 bytes[MORUS640_BLOCK_SIZE];
};

struct morus640_state {
	struct morus640_block s[MORUS_STATE_BLOCKS];
};

struct morus640_ctx {
	struct morus640_block key;
};

struct morus640_ops {
	int (*skcipher_walk_init)(struct skcipher_walk *walk,
				  struct aead_request *req, bool atomic);

	void (*crypt_chunk)(struct morus640_state *state,
			    u8 *dst, const u8 *src, unsigned int size);
};

static const struct morus640_block crypto_morus640_const[2] = {
	{ .words = {
		U32_C(0x02010100),
		U32_C(0x0d080503),
		U32_C(0x59372215),
		U32_C(0x6279e990),
	} },
	{ .words = {
		U32_C(0x55183ddb),
		U32_C(0xf12fc26d),
		U32_C(0x42311120),
		U32_C(0xdd28b573),
	} },
};

static void crypto_morus640_round(struct morus640_block *b0,
				  struct morus640_block *b1,
				  struct morus640_block *b2,
				  struct morus640_block *b3,
				  struct morus640_block *b4,
				  const struct morus640_block *m,
				  unsigned int b, unsigned int w)
{
	unsigned int i;
	struct morus640_block tmp;

	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		b0->words[i] ^= b1->words[i] & b2->words[i];
		b0->words[i] ^= b3->words[i];
		b0->words[i] ^= m->words[i];
		b0->words[i] = rol32(b0->words[i], b);
	}

	tmp = *b3;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		b3->words[(i + w) % MORUS_BLOCK_WORDS] = tmp.words[i];
}

static void crypto_morus640_update(struct morus640_state *state,
				   const struct morus640_block *m)
{
	static const struct morus640_block z = {};

	struct morus640_block *s = state->s;

	crypto_morus640_round(&s[0], &s[1], &s[2], &s[3], &s[4], &z,  5, 1);
	crypto_morus640_round(&s[1], &s[2], &s[3], &s[4], &s[0], m,  31, 2);
	crypto_morus640_round(&s[2], &s[3], &s[4], &s[0], &s[1], m,   7, 3);
	crypto_morus640_round(&s[3], &s[4], &s[0], &s[1], &s[2], m,  22, 2);
	crypto_morus640_round(&s[4], &s[0], &s[1], &s[2], &s[3], m,  13, 1);
}

static void crypto_morus640_load_a(struct morus640_block *dst, const u8 *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		dst->words[i] = le32_to_cpu(*(const __le32 *)src);
		src += MORUS640_WORD_SIZE;
	}
}

static void crypto_morus640_load_u(struct morus640_block *dst, const u8 *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		dst->words[i] = get_unaligned_le32(src);
		src += MORUS640_WORD_SIZE;
	}
}

static void crypto_morus640_load(struct morus640_block *dst, const u8 *src)
{
	if (MORUS640_ALIGNED(src))
		crypto_morus640_load_a(dst, src);
	else
		crypto_morus640_load_u(dst, src);
}

static void crypto_morus640_store_a(u8 *dst, const struct morus640_block *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		*(__le32 *)dst = cpu_to_le32(src->words[i]);
		dst += MORUS640_WORD_SIZE;
	}
}

static void crypto_morus640_store_u(u8 *dst, const struct morus640_block *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		put_unaligned_le32(src->words[i], dst);
		dst += MORUS640_WORD_SIZE;
	}
}

static void crypto_morus640_store(u8 *dst, const struct morus640_block *src)
{
	if (MORUS640_ALIGNED(dst))
		crypto_morus640_store_a(dst, src);
	else
		crypto_morus640_store_u(dst, src);
}

static void crypto_morus640_ad(struct morus640_state *state, const u8 *src,
			       unsigned int size)
{
	struct morus640_block m;

	if (MORUS640_ALIGNED(src)) {
		while (size >= MORUS640_BLOCK_SIZE) {
			crypto_morus640_load_a(&m, src);
			crypto_morus640_update(state, &m);

			size -= MORUS640_BLOCK_SIZE;
			src += MORUS640_BLOCK_SIZE;
		}
	} else {
		while (size >= MORUS640_BLOCK_SIZE) {
			crypto_morus640_load_u(&m, src);
			crypto_morus640_update(state, &m);

			size -= MORUS640_BLOCK_SIZE;
			src += MORUS640_BLOCK_SIZE;
		}
	}
}

static void crypto_morus640_core(const struct morus640_state *state,
				 struct morus640_block *blk)
{
	unsigned int i;

	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		blk->words[(i + 3) % MORUS_BLOCK_WORDS] ^= state->s[1].words[i];

	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		blk->words[i] ^= state->s[0].words[i];
		blk->words[i] ^= state->s[2].words[i] & state->s[3].words[i];
	}
}

static void crypto_morus640_encrypt_chunk(struct morus640_state *state, u8 *dst,
					  const u8 *src, unsigned int size)
{
	struct morus640_block c, m;

	if (MORUS640_ALIGNED(src) && MORUS640_ALIGNED(dst)) {
		while (size >= MORUS640_BLOCK_SIZE) {
			crypto_morus640_load_a(&m, src);
			c = m;
			crypto_morus640_core(state, &c);
			crypto_morus640_store_a(dst, &c);
			crypto_morus640_update(state, &m);

			src += MORUS640_BLOCK_SIZE;
			dst += MORUS640_BLOCK_SIZE;
			size -= MORUS640_BLOCK_SIZE;
		}
	} else {
		while (size >= MORUS640_BLOCK_SIZE) {
			crypto_morus640_load_u(&m, src);
			c = m;
			crypto_morus640_core(state, &c);
			crypto_morus640_store_u(dst, &c);
			crypto_morus640_update(state, &m);

			src += MORUS640_BLOCK_SIZE;
			dst += MORUS640_BLOCK_SIZE;
			size -= MORUS640_BLOCK_SIZE;
		}
	}

	if (size > 0) {
		union morus640_block_in tail;

		memcpy(tail.bytes, src, size);
		memset(tail.bytes + size, 0, MORUS640_BLOCK_SIZE - size);

		crypto_morus640_load_a(&m, tail.bytes);
		c = m;
		crypto_morus640_core(state, &c);
		crypto_morus640_store_a(tail.bytes, &c);
		crypto_morus640_update(state, &m);

		memcpy(dst, tail.bytes, size);
	}
}

static void crypto_morus640_decrypt_chunk(struct morus640_state *state, u8 *dst,
					  const u8 *src, unsigned int size)
{
	struct morus640_block m;

	if (MORUS640_ALIGNED(src) && MORUS640_ALIGNED(dst)) {
		while (size >= MORUS640_BLOCK_SIZE) {
			crypto_morus640_load_a(&m, src);
			crypto_morus640_core(state, &m);
			crypto_morus640_store_a(dst, &m);
			crypto_morus640_update(state, &m);

			src += MORUS640_BLOCK_SIZE;
			dst += MORUS640_BLOCK_SIZE;
			size -= MORUS640_BLOCK_SIZE;
		}
	} else {
		while (size >= MORUS640_BLOCK_SIZE) {
			crypto_morus640_load_u(&m, src);
			crypto_morus640_core(state, &m);
			crypto_morus640_store_u(dst, &m);
			crypto_morus640_update(state, &m);

			src += MORUS640_BLOCK_SIZE;
			dst += MORUS640_BLOCK_SIZE;
			size -= MORUS640_BLOCK_SIZE;
		}
	}

	if (size > 0) {
		union morus640_block_in tail;

		memcpy(tail.bytes, src, size);
		memset(tail.bytes + size, 0, MORUS640_BLOCK_SIZE - size);

		crypto_morus640_load_a(&m, tail.bytes);
		crypto_morus640_core(state, &m);
		crypto_morus640_store_a(tail.bytes, &m);
		memset(tail.bytes + size, 0, MORUS640_BLOCK_SIZE - size);
		crypto_morus640_load_a(&m, tail.bytes);
		crypto_morus640_update(state, &m);

		memcpy(dst, tail.bytes, size);
	}
}

static void crypto_morus640_init(struct morus640_state *state,
				 const struct morus640_block *key,
				 const u8 *iv)
{
	static const struct morus640_block z = {};

	unsigned int i;

	crypto_morus640_load(&state->s[0], iv);
	state->s[1] = *key;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		state->s[2].words[i] = U32_C(0xFFFFFFFF);
	state->s[3] = crypto_morus640_const[0];
	state->s[4] = crypto_morus640_const[1];

	for (i = 0; i < 16; i++)
		crypto_morus640_update(state, &z);

	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		state->s[1].words[i] ^= key->words[i];
}

static void crypto_morus640_process_ad(struct morus640_state *state,
				       struct scatterlist *sg_src,
				       unsigned int assoclen)
{
	struct scatter_walk walk;
	struct morus640_block m;
	union morus640_block_in buf;
	unsigned int pos = 0;

	scatterwalk_start(&walk, sg_src);
	while (assoclen != 0) {
		unsigned int size = scatterwalk_clamp(&walk, assoclen);
		unsigned int left = size;
		void *mapped = scatterwalk_map(&walk);
		const u8 *src = (const u8 *)mapped;

		if (pos + size >= MORUS640_BLOCK_SIZE) {
			if (pos > 0) {
				unsigned int fill = MORUS640_BLOCK_SIZE - pos;
				memcpy(buf.bytes + pos, src, fill);

				crypto_morus640_load_a(&m, buf.bytes);
				crypto_morus640_update(state, &m);

				pos = 0;
				left -= fill;
				src += fill;
			}

			crypto_morus640_ad(state, src, left);
			src += left & ~(MORUS640_BLOCK_SIZE - 1);
			left &= MORUS640_BLOCK_SIZE - 1;
		}

		memcpy(buf.bytes + pos, src, left);

		pos += left;
		assoclen -= size;
		scatterwalk_unmap(mapped);
		scatterwalk_advance(&walk, size);
		scatterwalk_done(&walk, 0, assoclen);
	}

	if (pos > 0) {
		memset(buf.bytes + pos, 0, MORUS640_BLOCK_SIZE - pos);

		crypto_morus640_load_a(&m, buf.bytes);
		crypto_morus640_update(state, &m);
	}
}

static void crypto_morus640_process_crypt(struct morus640_state *state,
					  struct aead_request *req,
					  const struct morus640_ops *ops)
{
	struct skcipher_walk walk;
	u8 *dst;
	const u8 *src;

	ops->skcipher_walk_init(&walk, req, false);

	while (walk.nbytes) {
		src = walk.src.virt.addr;
		dst = walk.dst.virt.addr;

		ops->crypt_chunk(state, dst, src, walk.nbytes);

		skcipher_walk_done(&walk, 0);
	}
}

static void crypto_morus640_final(struct morus640_state *state,
				  struct morus640_block *tag_xor,
				  u64 assoclen, u64 cryptlen)
{
	u64 assocbits = assoclen * 8;
	u64 cryptbits = cryptlen * 8;

	u32 assocbits_lo = (u32)assocbits;
	u32 assocbits_hi = (u32)(assocbits >> 32);
	u32 cryptbits_lo = (u32)cryptbits;
	u32 cryptbits_hi = (u32)(cryptbits >> 32);

	struct morus640_block tmp;
	unsigned int i;

	tmp.words[0] = cpu_to_le32(assocbits_lo);
	tmp.words[1] = cpu_to_le32(assocbits_hi);
	tmp.words[2] = cpu_to_le32(cryptbits_lo);
	tmp.words[3] = cpu_to_le32(cryptbits_hi);

	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		state->s[4].words[i] ^= state->s[0].words[i];

	for (i = 0; i < 10; i++)
		crypto_morus640_update(state, &tmp);

	crypto_morus640_core(state, tag_xor);
}

static int crypto_morus640_setkey(struct crypto_aead *aead, const u8 *key,
				  unsigned int keylen)
{
	struct morus640_ctx *ctx = crypto_aead_ctx(aead);

	if (keylen != MORUS640_BLOCK_SIZE) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	crypto_morus640_load(&ctx->key, key);
	return 0;
}

static int crypto_morus640_setauthsize(struct crypto_aead *tfm,
				       unsigned int authsize)
{
	return (authsize <= MORUS_MAX_AUTH_SIZE) ? 0 : -EINVAL;
}

static void crypto_morus640_crypt(struct aead_request *req,
				  struct morus640_block *tag_xor,
				  unsigned int cryptlen,
				  const struct morus640_ops *ops)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus640_ctx *ctx = crypto_aead_ctx(tfm);
	struct morus640_state state;

	crypto_morus640_init(&state, &ctx->key, req->iv);
	crypto_morus640_process_ad(&state, req->src, req->assoclen);
	crypto_morus640_process_crypt(&state, req, ops);
	crypto_morus640_final(&state, tag_xor, req->assoclen, cryptlen);
}

static int crypto_morus640_encrypt(struct aead_request *req)
{
	static const struct morus640_ops ops = {
		.skcipher_walk_init = skcipher_walk_aead_encrypt,
		.crypt_chunk = crypto_morus640_encrypt_chunk,
	};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus640_block tag = {};
	union morus640_block_in tag_out;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen;

	crypto_morus640_crypt(req, &tag, cryptlen, &ops);
	crypto_morus640_store(tag_out.bytes, &tag);

	scatterwalk_map_and_copy(tag_out.bytes, req->dst,
				 req->assoclen + cryptlen, authsize, 1);
	return 0;
}

static int crypto_morus640_decrypt(struct aead_request *req)
{
	static const struct morus640_ops ops = {
		.skcipher_walk_init = skcipher_walk_aead_decrypt,
		.crypt_chunk = crypto_morus640_decrypt_chunk,
	};
	static const u8 zeros[MORUS640_BLOCK_SIZE] = {};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	union morus640_block_in tag_in;
	struct morus640_block tag;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen - authsize;

	scatterwalk_map_and_copy(tag_in.bytes, req->src,
				 req->assoclen + cryptlen, authsize, 0);

	crypto_morus640_load(&tag, tag_in.bytes);
	crypto_morus640_crypt(req, &tag, cryptlen, &ops);
	crypto_morus640_store(tag_in.bytes, &tag);

	return crypto_memneq(tag_in.bytes, zeros, authsize) ? -EBADMSG : 0;
}

static int crypto_morus640_init_tfm(struct crypto_aead *tfm)
{
	return 0;
}

static void crypto_morus640_exit_tfm(struct crypto_aead *tfm)
{
}

static struct aead_alg crypto_morus640_alg = {
	.setkey = crypto_morus640_setkey,
	.setauthsize = crypto_morus640_setauthsize,
	.encrypt = crypto_morus640_encrypt,
	.decrypt = crypto_morus640_decrypt,
	.init = crypto_morus640_init_tfm,
	.exit = crypto_morus640_exit_tfm,

	.ivsize = MORUS_NONCE_SIZE,
	.maxauthsize = MORUS_MAX_AUTH_SIZE,
	.chunksize = MORUS640_BLOCK_SIZE,

	.base = {
		.cra_flags = CRYPTO_ALG_TYPE_AEAD,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct morus640_ctx),
		.cra_alignmask = 0,

		.cra_priority = 100,

		.cra_name = "morus640",
		.cra_driver_name = "morus640-generic",

		.cra_module = THIS_MODULE,
	}
};

static int __init crypto_morus640_module_init(void)
{
	return crypto_register_aead(&crypto_morus640_alg);
}

static void __exit crypto_morus640_module_exit(void)
{
	crypto_unregister_aead(&crypto_morus640_alg);
}

module_init(crypto_morus640_module_init);
module_exit(crypto_morus640_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("MORUS-640 AEAD algorithm");
MODULE_ALIAS_CRYPTO("morus640");
MODULE_ALIAS_CRYPTO("morus640-generic");
