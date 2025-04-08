/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4-GCM AEAD Algorithm using ARMv8 Crypto Extensions
 * as specified in rfc8998
 * https://datatracker.ietf.org/doc/html/rfc8998
 *
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/neon.h>
#include <crypto/b128ops.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/sm4.h>
#include "sm4-ce.h"

asmlinkage void sm4_ce_pmull_ghash_setup(const u32 *rkey_enc, u8 *ghash_table);
asmlinkage void pmull_ghash_update(const u8 *ghash_table, u8 *ghash,
				   const u8 *src, unsigned int nblocks);
asmlinkage void sm4_ce_pmull_gcm_enc(const u32 *rkey_enc, u8 *dst,
				     const u8 *src, u8 *iv,
				     unsigned int nbytes, u8 *ghash,
				     const u8 *ghash_table, const u8 *lengths);
asmlinkage void sm4_ce_pmull_gcm_dec(const u32 *rkey_enc, u8 *dst,
				     const u8 *src, u8 *iv,
				     unsigned int nbytes, u8 *ghash,
				     const u8 *ghash_table, const u8 *lengths);

#define GHASH_BLOCK_SIZE	16
#define GCM_IV_SIZE		12

struct sm4_gcm_ctx {
	struct sm4_ctx key;
	u8 ghash_table[16 * 4];
};


static int gcm_setkey(struct crypto_aead *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct sm4_gcm_ctx *ctx = crypto_aead_ctx(tfm);

	if (key_len != SM4_KEY_SIZE)
		return -EINVAL;

	kernel_neon_begin();

	sm4_ce_expand_key(key, ctx->key.rkey_enc, ctx->key.rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);
	sm4_ce_pmull_ghash_setup(ctx->key.rkey_enc, ctx->ghash_table);

	kernel_neon_end();
	return 0;
}

static int gcm_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12 ... 16:
		return 0;
	default:
		return -EINVAL;
	}
}

static void gcm_calculate_auth_mac(struct aead_request *req, u8 ghash[])
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct sm4_gcm_ctx *ctx = crypto_aead_ctx(aead);
	u8 __aligned(8) buffer[GHASH_BLOCK_SIZE];
	u32 assoclen = req->assoclen;
	struct scatter_walk walk;
	unsigned int buflen = 0;

	scatterwalk_start(&walk, req->src);

	do {
		unsigned int n, orig_n;
		const u8 *p;

		orig_n = scatterwalk_next(&walk, assoclen);
		p = walk.addr;
		n = orig_n;

		if (n + buflen < GHASH_BLOCK_SIZE) {
			memcpy(&buffer[buflen], p, n);
			buflen += n;
		} else {
			unsigned int nblocks;

			if (buflen) {
				unsigned int l = GHASH_BLOCK_SIZE - buflen;

				memcpy(&buffer[buflen], p, l);
				p += l;
				n -= l;

				pmull_ghash_update(ctx->ghash_table, ghash,
						   buffer, 1);
			}

			nblocks = n / GHASH_BLOCK_SIZE;
			if (nblocks) {
				pmull_ghash_update(ctx->ghash_table, ghash,
						   p, nblocks);
				p += nblocks * GHASH_BLOCK_SIZE;
			}

			buflen = n % GHASH_BLOCK_SIZE;
			if (buflen)
				memcpy(&buffer[0], p, buflen);
		}

		scatterwalk_done_src(&walk, orig_n);
		assoclen -= orig_n;
	} while (assoclen);

	/* padding with '0' */
	if (buflen) {
		memset(&buffer[buflen], 0, GHASH_BLOCK_SIZE - buflen);
		pmull_ghash_update(ctx->ghash_table, ghash, buffer, 1);
	}
}

static int gcm_crypt(struct aead_request *req, struct skcipher_walk *walk,
		     u8 ghash[], int err,
		     void (*sm4_ce_pmull_gcm_crypt)(const u32 *rkey_enc,
				u8 *dst, const u8 *src, u8 *iv,
				unsigned int nbytes, u8 *ghash,
				const u8 *ghash_table, const u8 *lengths))
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct sm4_gcm_ctx *ctx = crypto_aead_ctx(aead);
	u8 __aligned(8) iv[SM4_BLOCK_SIZE];
	be128 __aligned(8) lengths;

	memset(ghash, 0, SM4_BLOCK_SIZE);

	lengths.a = cpu_to_be64(req->assoclen * 8);
	lengths.b = cpu_to_be64(walk->total * 8);

	memcpy(iv, req->iv, GCM_IV_SIZE);
	put_unaligned_be32(2, iv + GCM_IV_SIZE);

	kernel_neon_begin();

	if (req->assoclen)
		gcm_calculate_auth_mac(req, ghash);

	while (walk->nbytes) {
		unsigned int tail = walk->nbytes % SM4_BLOCK_SIZE;
		const u8 *src = walk->src.virt.addr;
		u8 *dst = walk->dst.virt.addr;

		if (walk->nbytes == walk->total) {
			sm4_ce_pmull_gcm_crypt(ctx->key.rkey_enc, dst, src, iv,
					       walk->nbytes, ghash,
					       ctx->ghash_table,
					       (const u8 *)&lengths);

			kernel_neon_end();

			return skcipher_walk_done(walk, 0);
		}

		sm4_ce_pmull_gcm_crypt(ctx->key.rkey_enc, dst, src, iv,
				       walk->nbytes - tail, ghash,
				       ctx->ghash_table, NULL);

		kernel_neon_end();

		err = skcipher_walk_done(walk, tail);

		kernel_neon_begin();
	}

	sm4_ce_pmull_gcm_crypt(ctx->key.rkey_enc, NULL, NULL, iv,
			       walk->nbytes, ghash, ctx->ghash_table,
			       (const u8 *)&lengths);

	kernel_neon_end();

	return err;
}

static int gcm_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	u8 __aligned(8) ghash[SM4_BLOCK_SIZE];
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_aead_encrypt(&walk, req, false);
	err = gcm_crypt(req, &walk, ghash, err, sm4_ce_pmull_gcm_enc);
	if (err)
		return err;

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(ghash, req->dst, req->assoclen + req->cryptlen,
				 crypto_aead_authsize(aead), 1);

	return 0;
}

static int gcm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(aead);
	u8 __aligned(8) ghash[SM4_BLOCK_SIZE];
	u8 authtag[SM4_BLOCK_SIZE];
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_aead_decrypt(&walk, req, false);
	err = gcm_crypt(req, &walk, ghash, err, sm4_ce_pmull_gcm_dec);
	if (err)
		return err;

	/* compare calculated auth tag with the stored one */
	scatterwalk_map_and_copy(authtag, req->src,
				 req->assoclen + req->cryptlen - authsize,
				 authsize, 0);

	if (crypto_memneq(authtag, ghash, authsize))
		return -EBADMSG;

	return 0;
}

static struct aead_alg sm4_gcm_alg = {
	.base = {
		.cra_name		= "gcm(sm4)",
		.cra_driver_name	= "gcm-sm4-ce",
		.cra_priority		= 400,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct sm4_gcm_ctx),
		.cra_module		= THIS_MODULE,
	},
	.ivsize		= GCM_IV_SIZE,
	.chunksize	= SM4_BLOCK_SIZE,
	.maxauthsize	= SM4_BLOCK_SIZE,
	.setkey		= gcm_setkey,
	.setauthsize	= gcm_setauthsize,
	.encrypt	= gcm_encrypt,
	.decrypt	= gcm_decrypt,
};

static int __init sm4_ce_gcm_init(void)
{
	if (!cpu_have_named_feature(PMULL))
		return -ENODEV;

	return crypto_register_aead(&sm4_gcm_alg);
}

static void __exit sm4_ce_gcm_exit(void)
{
	crypto_unregister_aead(&sm4_gcm_alg);
}

static const struct cpu_feature __maybe_unused sm4_ce_gcm_cpu_feature[] = {
	{ cpu_feature(PMULL) },
	{}
};
MODULE_DEVICE_TABLE(cpu, sm4_ce_gcm_cpu_feature);

module_cpu_feature_match(SM4, sm4_ce_gcm_init);
module_exit(sm4_ce_gcm_exit);

MODULE_DESCRIPTION("Synchronous SM4 in GCM mode using ARMv8 Crypto Extensions");
MODULE_ALIAS_CRYPTO("gcm(sm4)");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_LICENSE("GPL v2");
