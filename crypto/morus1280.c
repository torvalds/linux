/*
 * The MORUS-1280 Authenticated-Encryption Algorithm
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

#define MORUS1280_WORD_SIZE 8
#define MORUS1280_BLOCK_SIZE (MORUS_BLOCK_WORDS * MORUS1280_WORD_SIZE)
#define MORUS1280_BLOCK_ALIGN (__alignof__(__le64))
#define MORUS1280_ALIGNED(p) IS_ALIGNED((uintptr_t)p, MORUS1280_BLOCK_ALIGN)

struct morus1280_block {
	u64 words[MORUS_BLOCK_WORDS];
};

union morus1280_block_in {
	__le64 words[MORUS_BLOCK_WORDS];
	u8 bytes[MORUS1280_BLOCK_SIZE];
};

struct morus1280_state {
	struct morus1280_block s[MORUS_STATE_BLOCKS];
};

struct morus1280_ctx {
	struct morus1280_block key;
};

struct morus1280_ops {
	int (*skcipher_walk_init)(struct skcipher_walk *walk,
				  struct aead_request *req, bool atomic);

	void (*crypt_chunk)(struct morus1280_state *state,
			    u8 *dst, const u8 *src, unsigned int size);
};

static const struct morus1280_block crypto_morus1280_const[1] = {
	{ .words = {
		U64_C(0x0d08050302010100),
		U64_C(0x6279e99059372215),
		U64_C(0xf12fc26d55183ddb),
		U64_C(0xdd28b57342311120),
	} },
};

static void crypto_morus1280_round(struct morus1280_block *b0,
				   struct morus1280_block *b1,
				   struct morus1280_block *b2,
				   struct morus1280_block *b3,
				   struct morus1280_block *b4,
				   const struct morus1280_block *m,
				   unsigned int b, unsigned int w)
{
	unsigned int i;
	struct morus1280_block tmp;

	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		b0->words[i] ^= b1->words[i] & b2->words[i];
		b0->words[i] ^= b3->words[i];
		b0->words[i] ^= m->words[i];
		b0->words[i] = rol64(b0->words[i], b);
	}

	tmp = *b3;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		b3->words[(i + w) % MORUS_BLOCK_WORDS] = tmp.words[i];
}

static void crypto_morus1280_update(struct morus1280_state *state,
				    const struct morus1280_block *m)
{
	static const struct morus1280_block z = {};

	struct morus1280_block *s = state->s;

	crypto_morus1280_round(&s[0], &s[1], &s[2], &s[3], &s[4], &z, 13, 1);
	crypto_morus1280_round(&s[1], &s[2], &s[3], &s[4], &s[0], m,  46, 2);
	crypto_morus1280_round(&s[2], &s[3], &s[4], &s[0], &s[1], m,  38, 3);
	crypto_morus1280_round(&s[3], &s[4], &s[0], &s[1], &s[2], m,   7, 2);
	crypto_morus1280_round(&s[4], &s[0], &s[1], &s[2], &s[3], m,   4, 1);
}

static void crypto_morus1280_load_a(struct morus1280_block *dst, const u8 *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		dst->words[i] = le64_to_cpu(*(const __le64 *)src);
		src += MORUS1280_WORD_SIZE;
	}
}

static void crypto_morus1280_load_u(struct morus1280_block *dst, const u8 *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		dst->words[i] = get_unaligned_le64(src);
		src += MORUS1280_WORD_SIZE;
	}
}

static void crypto_morus1280_load(struct morus1280_block *dst, const u8 *src)
{
	if (MORUS1280_ALIGNED(src))
		crypto_morus1280_load_a(dst, src);
	else
		crypto_morus1280_load_u(dst, src);
}

static void crypto_morus1280_store_a(u8 *dst, const struct morus1280_block *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		*(__le64 *)dst = cpu_to_le64(src->words[i]);
		dst += MORUS1280_WORD_SIZE;
	}
}

static void crypto_morus1280_store_u(u8 *dst, const struct morus1280_block *src)
{
	unsigned int i;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		put_unaligned_le64(src->words[i], dst);
		dst += MORUS1280_WORD_SIZE;
	}
}

static void crypto_morus1280_store(u8 *dst, const struct morus1280_block *src)
{
	if (MORUS1280_ALIGNED(dst))
		crypto_morus1280_store_a(dst, src);
	else
		crypto_morus1280_store_u(dst, src);
}

static void crypto_morus1280_ad(struct morus1280_state *state, const u8 *src,
				unsigned int size)
{
	struct morus1280_block m;

	if (MORUS1280_ALIGNED(src)) {
		while (size >= MORUS1280_BLOCK_SIZE) {
			crypto_morus1280_load_a(&m, src);
			crypto_morus1280_update(state, &m);

			size -= MORUS1280_BLOCK_SIZE;
			src += MORUS1280_BLOCK_SIZE;
		}
	} else {
		while (size >= MORUS1280_BLOCK_SIZE) {
			crypto_morus1280_load_u(&m, src);
			crypto_morus1280_update(state, &m);

			size -= MORUS1280_BLOCK_SIZE;
			src += MORUS1280_BLOCK_SIZE;
		}
	}
}

static void crypto_morus1280_core(const struct morus1280_state *state,
				  struct morus1280_block *blk)
{
	unsigned int i;

	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		blk->words[(i + 3) % MORUS_BLOCK_WORDS] ^= state->s[1].words[i];

        for (i = 0; i < MORUS_BLOCK_WORDS; i++) {
		blk->words[i] ^= state->s[0].words[i];
		blk->words[i] ^= state->s[2].words[i] & state->s[3].words[i];
	}
}

static void crypto_morus1280_encrypt_chunk(struct morus1280_state *state,
					   u8 *dst, const u8 *src,
					   unsigned int size)
{
	struct morus1280_block c, m;

	if (MORUS1280_ALIGNED(src) && MORUS1280_ALIGNED(dst)) {
		while (size >= MORUS1280_BLOCK_SIZE) {
			crypto_morus1280_load_a(&m, src);
			c = m;
			crypto_morus1280_core(state, &c);
			crypto_morus1280_store_a(dst, &c);
			crypto_morus1280_update(state, &m);

			src += MORUS1280_BLOCK_SIZE;
			dst += MORUS1280_BLOCK_SIZE;
			size -= MORUS1280_BLOCK_SIZE;
		}
	} else {
		while (size >= MORUS1280_BLOCK_SIZE) {
			crypto_morus1280_load_u(&m, src);
			c = m;
			crypto_morus1280_core(state, &c);
			crypto_morus1280_store_u(dst, &c);
			crypto_morus1280_update(state, &m);

			src += MORUS1280_BLOCK_SIZE;
			dst += MORUS1280_BLOCK_SIZE;
			size -= MORUS1280_BLOCK_SIZE;
		}
	}

	if (size > 0) {
		union morus1280_block_in tail;

		memcpy(tail.bytes, src, size);
		memset(tail.bytes + size, 0, MORUS1280_BLOCK_SIZE - size);

		crypto_morus1280_load_a(&m, tail.bytes);
		c = m;
		crypto_morus1280_core(state, &c);
		crypto_morus1280_store_a(tail.bytes, &c);
		crypto_morus1280_update(state, &m);

		memcpy(dst, tail.bytes, size);
	}
}

static void crypto_morus1280_decrypt_chunk(struct morus1280_state *state,
					   u8 *dst, const u8 *src,
					   unsigned int size)
{
	struct morus1280_block m;

	if (MORUS1280_ALIGNED(src) && MORUS1280_ALIGNED(dst)) {
		while (size >= MORUS1280_BLOCK_SIZE) {
			crypto_morus1280_load_a(&m, src);
			crypto_morus1280_core(state, &m);
			crypto_morus1280_store_a(dst, &m);
			crypto_morus1280_update(state, &m);

			src += MORUS1280_BLOCK_SIZE;
			dst += MORUS1280_BLOCK_SIZE;
			size -= MORUS1280_BLOCK_SIZE;
		}
	} else {
		while (size >= MORUS1280_BLOCK_SIZE) {
			crypto_morus1280_load_u(&m, src);
			crypto_morus1280_core(state, &m);
			crypto_morus1280_store_u(dst, &m);
			crypto_morus1280_update(state, &m);

			src += MORUS1280_BLOCK_SIZE;
			dst += MORUS1280_BLOCK_SIZE;
			size -= MORUS1280_BLOCK_SIZE;
		}
	}

	if (size > 0) {
		union morus1280_block_in tail;

		memcpy(tail.bytes, src, size);
		memset(tail.bytes + size, 0, MORUS1280_BLOCK_SIZE - size);

		crypto_morus1280_load_a(&m, tail.bytes);
		crypto_morus1280_core(state, &m);
		crypto_morus1280_store_a(tail.bytes, &m);
		memset(tail.bytes + size, 0, MORUS1280_BLOCK_SIZE - size);
		crypto_morus1280_load_a(&m, tail.bytes);
		crypto_morus1280_update(state, &m);

		memcpy(dst, tail.bytes, size);
	}
}

static void crypto_morus1280_init(struct morus1280_state *state,
				  const struct morus1280_block *key,
				  const u8 *iv)
{
	static const struct morus1280_block z = {};

	union morus1280_block_in tmp;
	unsigned int i;

	memcpy(tmp.bytes, iv, MORUS_NONCE_SIZE);
	memset(tmp.bytes + MORUS_NONCE_SIZE, 0,
	       MORUS1280_BLOCK_SIZE - MORUS_NONCE_SIZE);

	crypto_morus1280_load(&state->s[0], tmp.bytes);
	state->s[1] = *key;
	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		state->s[2].words[i] = U64_C(0xFFFFFFFFFFFFFFFF);
	state->s[3] = z;
	state->s[4] = crypto_morus1280_const[0];

	for (i = 0; i < 16; i++)
		crypto_morus1280_update(state, &z);

	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		state->s[1].words[i] ^= key->words[i];
}

static void crypto_morus1280_process_ad(struct morus1280_state *state,
					struct scatterlist *sg_src,
					unsigned int assoclen)
{
	struct scatter_walk walk;
	struct morus1280_block m;
	union morus1280_block_in buf;
	unsigned int pos = 0;

	scatterwalk_start(&walk, sg_src);
	while (assoclen != 0) {
		unsigned int size = scatterwalk_clamp(&walk, assoclen);
		unsigned int left = size;
		void *mapped = scatterwalk_map(&walk);
		const u8 *src = (const u8 *)mapped;

		if (pos + size >= MORUS1280_BLOCK_SIZE) {
			if (pos > 0) {
				unsigned int fill = MORUS1280_BLOCK_SIZE - pos;
				memcpy(buf.bytes + pos, src, fill);

				crypto_morus1280_load_a(&m, buf.bytes);
				crypto_morus1280_update(state, &m);

				pos = 0;
				left -= fill;
				src += fill;
			}

			crypto_morus1280_ad(state, src, left);
			src += left & ~(MORUS1280_BLOCK_SIZE - 1);
			left &= MORUS1280_BLOCK_SIZE - 1;
		}

		memcpy(buf.bytes + pos, src, left);

		pos += left;
		assoclen -= size;
		scatterwalk_unmap(mapped);
		scatterwalk_advance(&walk, size);
		scatterwalk_done(&walk, 0, assoclen);
	}

	if (pos > 0) {
		memset(buf.bytes + pos, 0, MORUS1280_BLOCK_SIZE - pos);

		crypto_morus1280_load_a(&m, buf.bytes);
		crypto_morus1280_update(state, &m);
	}
}

static void crypto_morus1280_process_crypt(struct morus1280_state *state,
					   struct aead_request *req,
					   const struct morus1280_ops *ops)
{
	struct skcipher_walk walk;

	ops->skcipher_walk_init(&walk, req, false);

	while (walk.nbytes) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		ops->crypt_chunk(state, walk.dst.virt.addr, walk.src.virt.addr,
				 nbytes);

		skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}
}

static void crypto_morus1280_final(struct morus1280_state *state,
				   struct morus1280_block *tag_xor,
				   u64 assoclen, u64 cryptlen)
{
	struct morus1280_block tmp;
	unsigned int i;

	tmp.words[0] = assoclen * 8;
	tmp.words[1] = cryptlen * 8;
	tmp.words[2] = 0;
	tmp.words[3] = 0;

	for (i = 0; i < MORUS_BLOCK_WORDS; i++)
		state->s[4].words[i] ^= state->s[0].words[i];

	for (i = 0; i < 10; i++)
		crypto_morus1280_update(state, &tmp);

	crypto_morus1280_core(state, tag_xor);
}

static int crypto_morus1280_setkey(struct crypto_aead *aead, const u8 *key,
				   unsigned int keylen)
{
	struct morus1280_ctx *ctx = crypto_aead_ctx(aead);
	union morus1280_block_in tmp;

	if (keylen == MORUS1280_BLOCK_SIZE)
		crypto_morus1280_load(&ctx->key, key);
	else if (keylen == MORUS1280_BLOCK_SIZE / 2) {
		memcpy(tmp.bytes, key, keylen);
		memcpy(tmp.bytes + keylen, key, keylen);

		crypto_morus1280_load(&ctx->key, tmp.bytes);
	} else {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	return 0;
}

static int crypto_morus1280_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	return (authsize <= MORUS_MAX_AUTH_SIZE) ? 0 : -EINVAL;
}

static void crypto_morus1280_crypt(struct aead_request *req,
				   struct morus1280_block *tag_xor,
				   unsigned int cryptlen,
				   const struct morus1280_ops *ops)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus1280_ctx *ctx = crypto_aead_ctx(tfm);
	struct morus1280_state state;

	crypto_morus1280_init(&state, &ctx->key, req->iv);
	crypto_morus1280_process_ad(&state, req->src, req->assoclen);
	crypto_morus1280_process_crypt(&state, req, ops);
	crypto_morus1280_final(&state, tag_xor, req->assoclen, cryptlen);
}

static int crypto_morus1280_encrypt(struct aead_request *req)
{
	static const struct morus1280_ops ops = {
		.skcipher_walk_init = skcipher_walk_aead_encrypt,
		.crypt_chunk = crypto_morus1280_encrypt_chunk,
	};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus1280_block tag = {};
	union morus1280_block_in tag_out;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen;

	crypto_morus1280_crypt(req, &tag, cryptlen, &ops);
	crypto_morus1280_store(tag_out.bytes, &tag);

	scatterwalk_map_and_copy(tag_out.bytes, req->dst,
				 req->assoclen + cryptlen, authsize, 1);
	return 0;
}

static int crypto_morus1280_decrypt(struct aead_request *req)
{
	static const struct morus1280_ops ops = {
		.skcipher_walk_init = skcipher_walk_aead_decrypt,
		.crypt_chunk = crypto_morus1280_decrypt_chunk,
	};
	static const u8 zeros[MORUS1280_BLOCK_SIZE] = {};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	union morus1280_block_in tag_in;
	struct morus1280_block tag;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen - authsize;

	scatterwalk_map_and_copy(tag_in.bytes, req->src,
				 req->assoclen + cryptlen, authsize, 0);

	crypto_morus1280_load(&tag, tag_in.bytes);
	crypto_morus1280_crypt(req, &tag, cryptlen, &ops);
	crypto_morus1280_store(tag_in.bytes, &tag);

	return crypto_memneq(tag_in.bytes, zeros, authsize) ? -EBADMSG : 0;
}

static int crypto_morus1280_init_tfm(struct crypto_aead *tfm)
{
	return 0;
}

static void crypto_morus1280_exit_tfm(struct crypto_aead *tfm)
{
}

static struct aead_alg crypto_morus1280_alg = {
	.setkey = crypto_morus1280_setkey,
	.setauthsize = crypto_morus1280_setauthsize,
	.encrypt = crypto_morus1280_encrypt,
	.decrypt = crypto_morus1280_decrypt,
	.init = crypto_morus1280_init_tfm,
	.exit = crypto_morus1280_exit_tfm,

	.ivsize = MORUS_NONCE_SIZE,
	.maxauthsize = MORUS_MAX_AUTH_SIZE,
	.chunksize = MORUS1280_BLOCK_SIZE,

	.base = {
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct morus1280_ctx),
		.cra_alignmask = 0,

		.cra_priority = 100,

		.cra_name = "morus1280",
		.cra_driver_name = "morus1280-generic",

		.cra_module = THIS_MODULE,
	}
};


static int __init crypto_morus1280_module_init(void)
{
	return crypto_register_aead(&crypto_morus1280_alg);
}

static void __exit crypto_morus1280_module_exit(void)
{
	crypto_unregister_aead(&crypto_morus1280_alg);
}

module_init(crypto_morus1280_module_init);
module_exit(crypto_morus1280_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("MORUS-1280 AEAD algorithm");
MODULE_ALIAS_CRYPTO("morus1280");
MODULE_ALIAS_CRYPTO("morus1280-generic");
