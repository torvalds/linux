// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES-GCM using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <crypto/aes.h>
#include <crypto/b128ops.h>
#include <crypto/gcm.h>
#include <crypto/ghash.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include <asm/simd.h>

MODULE_DESCRIPTION("AES-GCM using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("gcm(aes)");
MODULE_ALIAS_CRYPTO("rfc4106(gcm(aes))");

#define RFC4106_NONCE_SIZE	4

struct arm_ghash_key {
	be128			k;
	u64			h[4][2];
};

struct gcm_aes_ctx {
	struct aes_enckey	aes_key;
	u8			nonce[RFC4106_NONCE_SIZE];
	struct arm_ghash_key	ghash_key;
};

asmlinkage void pmull_ghash_update_p64(int blocks, u64 dg[], const char *src,
				       u64 const h[4][2], const char *head);

asmlinkage void pmull_gcm_encrypt(int bytes, u8 dst[], const u8 src[],
				  u64 const h[4][2], u64 dg[], u8 ctr[],
				  u32 const rk[], int rounds, u8 tag[]);
asmlinkage int pmull_gcm_decrypt(int bytes, u8 dst[], const u8 src[],
				 u64 const h[4][2], u64 dg[], u8 ctr[],
				 u32 const rk[], int rounds, const u8 l[],
				 const u8 tag[], u64 authsize);

static void ghash_do_simd_update(int blocks, u64 dg[], const char *src,
				 struct arm_ghash_key *key, const char *head)
{
	scoped_ksimd()
		pmull_ghash_update_p64(blocks, dg, src, key->h, head);
}

static void ghash_reflect(u64 h[], const be128 *k)
{
	u64 carry = be64_to_cpu(k->a) & BIT(63) ? 1 : 0;

	h[0] = (be64_to_cpu(k->b) << 1) | carry;
	h[1] = (be64_to_cpu(k->a) << 1) | (be64_to_cpu(k->b) >> 63);

	if (carry)
		h[1] ^= 0xc200000000000000UL;
}

static int gcm_aes_setkey(struct crypto_aead *tfm, const u8 *inkey,
			  unsigned int keylen)
{
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(tfm);
	u8 key[GHASH_BLOCK_SIZE];
	be128 h;
	int ret;

	ret = aes_prepareenckey(&ctx->aes_key, inkey, keylen);
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
				     *buf_count ? buf : NULL);
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
		ghash_do_simd_update(1, dg, buf, &ctx->ghash_key, NULL);
	}
}

static int gcm_encrypt(struct aead_request *req, char *iv, int assoclen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_aes_ctx *ctx = crypto_aead_ctx(aead);
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

		scoped_ksimd()
			pmull_gcm_encrypt(nbytes, dst, src, ctx->ghash_key.h,
					  dg, iv, ctx->aes_key.k.rndkeys,
					  ctx->aes_key.nrounds, tag);

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

		scoped_ksimd()
			ret = pmull_gcm_decrypt(nbytes, dst, src,
						ctx->ghash_key.h,
						dg, iv, ctx->aes_key.k.rndkeys,
						ctx->aes_key.nrounds, tag, otag,
						authsize);

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
	.base.cra_ctxsize	= sizeof(struct gcm_aes_ctx),
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
	.base.cra_ctxsize	= sizeof(struct gcm_aes_ctx),
	.base.cra_module	= THIS_MODULE,
}};

static int __init ghash_ce_mod_init(void)
{
	if (!cpu_have_named_feature(ASIMD) || !cpu_have_named_feature(PMULL))
		return -ENODEV;

	return crypto_register_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs));
}

static void __exit ghash_ce_mod_exit(void)
{
	crypto_unregister_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs));
}

static const struct cpu_feature __maybe_unused ghash_cpu_feature[] = {
	{ cpu_feature(PMULL) }, { }
};
MODULE_DEVICE_TABLE(cpu, ghash_cpu_feature);

module_init(ghash_ce_mod_init);
module_exit(ghash_ce_mod_exit);
