// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/unaligned.h>
#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/algapi.h>
#include <crypto/b128ops.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

MODULE_DESCRIPTION("GHASH and AES-GCM using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("ghash");

#define GHASH_BLOCK_SIZE	16
#define GHASH_DIGEST_SIZE	16

#define RFC4106_NONCE_SIZE	4

struct ghash_key {
	be128			k;
	u64			h[][2];
};

struct ghash_desc_ctx {
	u64 digest[GHASH_DIGEST_SIZE/sizeof(u64)];
	u8 buf[GHASH_BLOCK_SIZE];
	u32 count;
};

struct gcm_aes_ctx {
	struct crypto_aes_ctx	aes_key;
	u8			nonce[RFC4106_NONCE_SIZE];
	struct ghash_key	ghash_key;
};

asmlinkage void pmull_ghash_update_p64(int blocks, u64 dg[], const char *src,
				       u64 const h[][2], const char *head);

asmlinkage void pmull_ghash_update_p8(int blocks, u64 dg[], const char *src,
				      u64 const h[][2], const char *head);

asmlinkage void pmull_gcm_encrypt(int bytes, u8 dst[], const u8 src[],
				  u64 const h[][2], u64 dg[], u8 ctr[],
				  u32 const rk[], int rounds, u8 tag[]);
asmlinkage int pmull_gcm_decrypt(int bytes, u8 dst[], const u8 src[],
				 u64 const h[][2], u64 dg[], u8 ctr[],
				 u32 const rk[], int rounds, const u8 l[],
				 const u8 tag[], u64 authsize);

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_desc_ctx *ctx = shash_desc_ctx(desc);

	*ctx = (struct ghash_desc_ctx){};
	return 0;
}

static void ghash_do_update(int blocks, u64 dg[], const char *src,
			    struct ghash_key *key, const char *head)
{
	be128 dst = { cpu_to_be64(dg[1]), cpu_to_be64(dg[0]) };

	do {
		const u8 *in = src;

		if (head) {
			in = head;
			blocks++;
			head = NULL;
		} else {
			src += GHASH_BLOCK_SIZE;
		}

		crypto_xor((u8 *)&dst, in, GHASH_BLOCK_SIZE);
		gf128mul_lle(&dst, &key->k);
	} while (--blocks);

	dg[0] = be64_to_cpu(dst.b);
	dg[1] = be64_to_cpu(dst.a);
}

static __always_inline
void ghash_do_simd_update(int blocks, u64 dg[], const char *src,
			  struct ghash_key *key, const char *head,
			  void (*simd_update)(int blocks, u64 dg[],
					      const char *src,
					      u64 const h[][2],
					      const char *head))
{
	if (likely(crypto_simd_usable())) {
		kernel_neon_begin();
		simd_update(blocks, dg, src, key->h, head);
		kernel_neon_end();
	} else {
		ghash_do_update(blocks, dg, src, key, head);
	}
}

/* avoid hogging the CPU for too long */
#define MAX_BLOCKS	(SZ_64K / GHASH_BLOCK_SIZE)

static int ghash_update(struct shash_desc *desc, const u8 *src,
			unsigned int len)
{
	struct ghash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % GHASH_BLOCK_SIZE;

	ctx->count += len;

	if ((partial + len) >= GHASH_BLOCK_SIZE) {
		struct ghash_key *key = crypto_shash_ctx(desc->tfm);
		int blocks;

		if (partial) {
			int p = GHASH_BLOCK_SIZE - partial;

			memcpy(ctx->buf + partial, src, p);
			src += p;
			len -= p;
		}

		blocks = len / GHASH_BLOCK_SIZE;
		len %= GHASH_BLOCK_SIZE;

		do {
			int chunk = min(blocks, MAX_BLOCKS);

			ghash_do_simd_update(chunk, ctx->digest, src, key,
					     partial ? ctx->buf : NULL,
					     pmull_ghash_update_p8);

			blocks -= chunk;
			src += chunk * GHASH_BLOCK_SIZE;
			partial = 0;
		} while (unlikely(blocks > 0));
	}
	if (len)
		memcpy(ctx->buf + partial, src, len);
	return 0;
}

static int ghash_final(struct shash_desc *desc, u8 *dst)
{
	struct ghash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % GHASH_BLOCK_SIZE;

	if (partial) {
		struct ghash_key *key = crypto_shash_ctx(desc->tfm);

		memset(ctx->buf + partial, 0, GHASH_BLOCK_SIZE - partial);

		ghash_do_simd_update(1, ctx->digest, ctx->buf, key, NULL,
				     pmull_ghash_update_p8);
	}
	put_unaligned_be64(ctx->digest[1], dst);
	put_unaligned_be64(ctx->digest[0], dst + 8);

	memzero_explicit(ctx, sizeof(*ctx));
	return 0;
}

static void ghash_reflect(u64 h[], const be128 *k)
{
	u64 carry = be64_to_cpu(k->a) & BIT(63) ? 1 : 0;

	h[0] = (be64_to_cpu(k->b) << 1) | carry;
	h[1] = (be64_to_cpu(k->a) << 1) | (be64_to_cpu(k->b) >> 63);

	if (carry)
		h[1] ^= 0xc200000000000000UL;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *inkey, unsigned int keylen)
{
	struct ghash_key *key = crypto_shash_ctx(tfm);

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	/* needed for the fallback */
	memcpy(&key->k, inkey, GHASH_BLOCK_SIZE);

	ghash_reflect(key->h[0], &key->k);
	return 0;
}

static struct shash_alg ghash_alg = {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon",
	.base.cra_priority	= 150,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_key) + sizeof(u64[2]),
	.base.cra_module	= THIS_MODULE,

	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_update,
	.final			= ghash_final,
	.setkey			= ghash_setkey,
	.descsize		= sizeof(struct ghash_desc_ctx),
};

static int num_rounds(struct crypto_aes_ctx *ctx)
{
	/*
	 * # of rounds specified by AES:
	 * 128 bit key		10 rounds
	 * 192 bit key		12 rounds
	 * 256 bit key		14 rounds
	 * => n byte key	=> 6 + (n/4) rounds
	 */
	return 6 + ctx->key_length / 4;
}

static int gcm_aes_setkey(struct crypto_aead *tfm, const u8 *inkey,
			  unsigned int keylen)
{
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(tfm);
	u8 key[GHASH_BLOCK_SIZE];
	be128 h;
	int ret;

	ret = aes_expandkey(&ctx->aes_key, inkey, keylen);
	if (ret)
		return -EINVAL;

	aes_encrypt(&ctx->aes_key, key, (u8[AES_BLOCK_SIZE]){});

	/* needed for the fallback */
	memcpy(&ctx->ghash_key.k, key, GHASH_BLOCK_SIZE);

	ghash_reflect(ctx->ghash_key.h[0], &ctx->ghash_key.k);

	h = ctx->ghash_key.k;
	gf128mul_lle(&h, &ctx->ghash_key.k);
	ghash_reflect(ctx->ghash_key.h[1], &h);

	gf128mul_lle(&h, &ctx->ghash_key.k);
	ghash_reflect(ctx->ghash_key.h[2], &h);

	gf128mul_lle(&h, &ctx->ghash_key.k);
	ghash_reflect(ctx->ghash_key.h[3], &h);

	return 0;
}

static int gcm_aes_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	return crypto_gcm_check_authsize(authsize);
}

static void gcm_update_mac(u64 dg[], const u8 *src, int count, u8 buf[],
			   int *buf_count, struct gcm_aes_ctx *ctx)
{
	if (*buf_count > 0) {
		int buf_added = min(count, GHASH_BLOCK_SIZE - *buf_count);

		memcpy(&buf[*buf_count], src, buf_added);

		*buf_count += buf_added;
		src += buf_added;
		count -= buf_added;
	}

	if (count >= GHASH_BLOCK_SIZE || *buf_count == GHASH_BLOCK_SIZE) {
		int blocks = count / GHASH_BLOCK_SIZE;

		ghash_do_simd_update(blocks, dg, src, &ctx->ghash_key,
				     *buf_count ? buf : NULL,
				     pmull_ghash_update_p64);

		src += blocks * GHASH_BLOCK_SIZE;
		count %= GHASH_BLOCK_SIZE;
		*buf_count = 0;
	}

	if (count > 0) {
		memcpy(buf, src, count);
		*buf_count = count;
	}
}

static void gcm_calculate_auth_mac(struct aead_request *req, u64 dg[], u32 len)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(aead);
	u8 buf[GHASH_BLOCK_SIZE];
	struct scatter_walk walk;
	int buf_count = 0;

	scatterwalk_start(&walk, req->src);

	do {
		unsigned int n;

		n = scatterwalk_next(&walk, len);
		gcm_update_mac(dg, walk.addr, n, buf, &buf_count, ctx);
		scatterwalk_done_src(&walk, n);
		len -= n;
	} while (len);

	if (buf_count) {
		memset(&buf[buf_count], 0, GHASH_BLOCK_SIZE - buf_count);
		ghash_do_simd_update(1, dg, buf, &ctx->ghash_key, NULL,
				     pmull_ghash_update_p64);
	}
}

static int gcm_encrypt(struct aead_request *req, char *iv, int assoclen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(aead);
	int nrounds = num_rounds(&ctx->aes_key);
	struct skcipher_walk walk;
	u8 buf[AES_BLOCK_SIZE];
	u64 dg[2] = {};
	be128 lengths;
	u8 *tag;
	int err;

	lengths.a = cpu_to_be64(assoclen * 8);
	lengths.b = cpu_to_be64(req->cryptlen * 8);

	if (assoclen)
		gcm_calculate_auth_mac(req, dg, assoclen);

	put_unaligned_be32(2, iv + GCM_AES_IV_SIZE);

	err = skcipher_walk_aead_encrypt(&walk, req, false);

	do {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		int nbytes = walk.nbytes;

		tag = (u8 *)&lengths;

		if (unlikely(nbytes > 0 && nbytes < AES_BLOCK_SIZE)) {
			src = dst = memcpy(buf + sizeof(buf) - nbytes,
					   src, nbytes);
		} else if (nbytes < walk.total) {
			nbytes &= ~(AES_BLOCK_SIZE - 1);
			tag = NULL;
		}

		kernel_neon_begin();
		pmull_gcm_encrypt(nbytes, dst, src, ctx->ghash_key.h,
				  dg, iv, ctx->aes_key.key_enc, nrounds,
				  tag);
		kernel_neon_end();

		if (unlikely(!nbytes))
			break;

		if (unlikely(nbytes > 0 && nbytes < AES_BLOCK_SIZE))
			memcpy(walk.dst.virt.addr,
			       buf + sizeof(buf) - nbytes, nbytes);

		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	} while (walk.nbytes);

	if (err)
		return err;

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(tag, req->dst, req->assoclen + req->cryptlen,
				 crypto_aead_authsize(aead), 1);

	return 0;
}

static int gcm_decrypt(struct aead_request *req, char *iv, int assoclen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int authsize = crypto_aead_authsize(aead);
	int nrounds = num_rounds(&ctx->aes_key);
	struct skcipher_walk walk;
	u8 otag[AES_BLOCK_SIZE];
	u8 buf[AES_BLOCK_SIZE];
	u64 dg[2] = {};
	be128 lengths;
	u8 *tag;
	int ret;
	int err;

	lengths.a = cpu_to_be64(assoclen * 8);
	lengths.b = cpu_to_be64((req->cryptlen - authsize) * 8);

	if (assoclen)
		gcm_calculate_auth_mac(req, dg, assoclen);

	put_unaligned_be32(2, iv + GCM_AES_IV_SIZE);

	scatterwalk_map_and_copy(otag, req->src,
				 req->assoclen + req->cryptlen - authsize,
				 authsize, 0);

	err = skcipher_walk_aead_decrypt(&walk, req, false);

	do {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		int nbytes = walk.nbytes;

		tag = (u8 *)&lengths;

		if (unlikely(nbytes > 0 && nbytes < AES_BLOCK_SIZE)) {
			src = dst = memcpy(buf + sizeof(buf) - nbytes,
					   src, nbytes);
		} else if (nbytes < walk.total) {
			nbytes &= ~(AES_BLOCK_SIZE - 1);
			tag = NULL;
		}

		kernel_neon_begin();
		ret = pmull_gcm_decrypt(nbytes, dst, src, ctx->ghash_key.h,
					dg, iv, ctx->aes_key.key_enc,
					nrounds, tag, otag, authsize);
		kernel_neon_end();

		if (unlikely(!nbytes))
			break;

		if (unlikely(nbytes > 0 && nbytes < AES_BLOCK_SIZE))
			memcpy(walk.dst.virt.addr,
			       buf + sizeof(buf) - nbytes, nbytes);

		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	} while (walk.nbytes);

	if (err)
		return err;

	return ret ? -EBADMSG : 0;
}

static int gcm_aes_encrypt(struct aead_request *req)
{
	u8 iv[AES_BLOCK_SIZE];

	memcpy(iv, req->iv, GCM_AES_IV_SIZE);
	return gcm_encrypt(req, iv, req->assoclen);
}

static int gcm_aes_decrypt(struct aead_request *req)
{
	u8 iv[AES_BLOCK_SIZE];

	memcpy(iv, req->iv, GCM_AES_IV_SIZE);
	return gcm_decrypt(req, iv, req->assoclen);
}

static int rfc4106_setkey(struct crypto_aead *tfm, const u8 *inkey,
			  unsigned int keylen)
{
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(tfm);
	int err;

	keylen -= RFC4106_NONCE_SIZE;
	err = gcm_aes_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(ctx->nonce, inkey + keylen, RFC4106_NONCE_SIZE);
	return 0;
}

static int rfc4106_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	return crypto_rfc4106_check_authsize(authsize);
}

static int rfc4106_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(aead);
	u8 iv[AES_BLOCK_SIZE];

	memcpy(iv, ctx->nonce, RFC4106_NONCE_SIZE);
	memcpy(iv + RFC4106_NONCE_SIZE, req->iv, GCM_RFC4106_IV_SIZE);

	return crypto_ipsec_check_assoclen(req->assoclen) ?:
	       gcm_encrypt(req, iv, req->assoclen - GCM_RFC4106_IV_SIZE);
}

static int rfc4106_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(aead);
	u8 iv[AES_BLOCK_SIZE];

	memcpy(iv, ctx->nonce, RFC4106_NONCE_SIZE);
	memcpy(iv + RFC4106_NONCE_SIZE, req->iv, GCM_RFC4106_IV_SIZE);

	return crypto_ipsec_check_assoclen(req->assoclen) ?:
	       gcm_decrypt(req, iv, req->assoclen - GCM_RFC4106_IV_SIZE);
}

static struct aead_alg gcm_aes_algs[] = {{
	.ivsize			= GCM_AES_IV_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.maxauthsize		= AES_BLOCK_SIZE,
	.setkey			= gcm_aes_setkey,
	.setauthsize		= gcm_aes_setauthsize,
	.encrypt		= gcm_aes_encrypt,
	.decrypt		= gcm_aes_decrypt,

	.base.cra_name		= "gcm(aes)",
	.base.cra_driver_name	= "gcm-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct gcm_aes_ctx) +
				  4 * sizeof(u64[2]),
	.base.cra_module	= THIS_MODULE,
}, {
	.ivsize			= GCM_RFC4106_IV_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.maxauthsize		= AES_BLOCK_SIZE,
	.setkey			= rfc4106_setkey,
	.setauthsize		= rfc4106_setauthsize,
	.encrypt		= rfc4106_encrypt,
	.decrypt		= rfc4106_decrypt,

	.base.cra_name		= "rfc4106(gcm(aes))",
	.base.cra_driver_name	= "rfc4106-gcm-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct gcm_aes_ctx) +
				  4 * sizeof(u64[2]),
	.base.cra_module	= THIS_MODULE,
}};

static int __init ghash_ce_mod_init(void)
{
	if (!cpu_have_named_feature(ASIMD))
		return -ENODEV;

	if (cpu_have_named_feature(PMULL))
		return crypto_register_aeads(gcm_aes_algs,
					     ARRAY_SIZE(gcm_aes_algs));

	return crypto_register_shash(&ghash_alg);
}

static void __exit ghash_ce_mod_exit(void)
{
	if (cpu_have_named_feature(PMULL))
		crypto_unregister_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs));
	else
		crypto_unregister_shash(&ghash_alg);
}

static const struct cpu_feature __maybe_unused ghash_cpu_feature[] = {
	{ cpu_feature(PMULL) }, { }
};
MODULE_DEVICE_TABLE(cpu, ghash_cpu_feature);

module_init(ghash_ce_mod_init);
module_exit(ghash_ce_mod_exit);
