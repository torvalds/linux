// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated GHASH implementation with Intel PCLMULQDQ-NI
 * instructions. This file contains glue code.
 *
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/cryptd.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <asm/cpu_device_id.h>
#include <asm/simd.h>
#include <linux/unaligned.h>

#define GHASH_BLOCK_SIZE	16
#define GHASH_DIGEST_SIZE	16

void clmul_ghash_mul(char *dst, const le128 *shash);

void clmul_ghash_update(char *dst, const char *src, unsigned int srclen,
			const le128 *shash);

struct ghash_async_ctx {
	struct cryptd_ahash *cryptd_tfm;
};

struct ghash_ctx {
	le128 shash;
};

struct ghash_desc_ctx {
	u8 buffer[GHASH_BLOCK_SIZE];
	u32 bytes;
};

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct ghash_ctx *ctx = crypto_shash_ctx(tfm);
	u64 a, b;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	/*
	 * GHASH maps bits to polynomial coefficients backwards, which makes it
	 * hard to implement.  But it can be shown that the GHASH multiplication
	 *
	 *	D * K (mod x^128 + x^7 + x^2 + x + 1)
	 *
	 * (where D is a data block and K is the key) is equivalent to:
	 *
	 *	bitreflect(D) * bitreflect(K) * x^(-127)
	 *		(mod x^128 + x^127 + x^126 + x^121 + 1)
	 *
	 * So, the code below precomputes:
	 *
	 *	bitreflect(K) * x^(-127) (mod x^128 + x^127 + x^126 + x^121 + 1)
	 *
	 * ... but in Montgomery form (so that Montgomery multiplication can be
	 * used), i.e. with an extra x^128 factor, which means actually:
	 *
	 *	bitreflect(K) * x (mod x^128 + x^127 + x^126 + x^121 + 1)
	 *
	 * The within-a-byte part of bitreflect() cancels out GHASH's built-in
	 * reflection, and thus bitreflect() is actually a byteswap.
	 */
	a = get_unaligned_be64(key);
	b = get_unaligned_be64(key + 8);
	ctx->shash.a = cpu_to_le64((a << 1) | (b >> 63));
	ctx->shash.b = cpu_to_le64((b << 1) | (a >> 63));
	if (a >> 63)
		ctx->shash.a ^= cpu_to_le64((u64)0xc2 << 56);
	return 0;
}

static int ghash_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	struct ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *dst = dctx->buffer;

	kernel_fpu_begin();
	if (dctx->bytes) {
		int n = min(srclen, dctx->bytes);
		u8 *pos = dst + (GHASH_BLOCK_SIZE - dctx->bytes);

		dctx->bytes -= n;
		srclen -= n;

		while (n--)
			*pos++ ^= *src++;

		if (!dctx->bytes)
			clmul_ghash_mul(dst, &ctx->shash);
	}

	clmul_ghash_update(dst, src, srclen, &ctx->shash);
	kernel_fpu_end();

	if (srclen & 0xf) {
		src += srclen - (srclen & 0xf);
		srclen &= 0xf;
		dctx->bytes = GHASH_BLOCK_SIZE - srclen;
		while (srclen--)
			*dst++ ^= *src++;
	}

	return 0;
}

static void ghash_flush(struct ghash_ctx *ctx, struct ghash_desc_ctx *dctx)
{
	u8 *dst = dctx->buffer;

	if (dctx->bytes) {
		u8 *tmp = dst + (GHASH_BLOCK_SIZE - dctx->bytes);

		while (dctx->bytes--)
			*tmp++ ^= 0;

		kernel_fpu_begin();
		clmul_ghash_mul(dst, &ctx->shash);
		kernel_fpu_end();
	}

	dctx->bytes = 0;
}

static int ghash_final(struct shash_desc *desc, u8 *dst)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	struct ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *buf = dctx->buffer;

	ghash_flush(ctx, dctx);
	memcpy(dst, buf, GHASH_BLOCK_SIZE);

	return 0;
}

static struct shash_alg ghash_alg = {
	.digestsize	= GHASH_DIGEST_SIZE,
	.init		= ghash_init,
	.update		= ghash_update,
	.final		= ghash_final,
	.setkey		= ghash_setkey,
	.descsize	= sizeof(struct ghash_desc_ctx),
	.base		= {
		.cra_name		= "__ghash",
		.cra_driver_name	= "__ghash-pclmulqdqni",
		.cra_priority		= 0,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= GHASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct ghash_ctx),
		.cra_module		= THIS_MODULE,
	},
};

static int ghash_async_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ghash_async_ctx *ctx = crypto_ahash_ctx(tfm);
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct cryptd_ahash *cryptd_tfm = ctx->cryptd_tfm;
	struct shash_desc *desc = cryptd_shash_desc(cryptd_req);
	struct crypto_shash *child = cryptd_ahash_child(cryptd_tfm);

	desc->tfm = child;
	return crypto_shash_init(desc);
}

static void ghash_init_cryptd_req(struct ahash_request *req)
{
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ghash_async_ctx *ctx = crypto_ahash_ctx(tfm);
	struct cryptd_ahash *cryptd_tfm = ctx->cryptd_tfm;

	ahash_request_set_tfm(cryptd_req, &cryptd_tfm->base);
	ahash_request_set_callback(cryptd_req, req->base.flags,
				   req->base.complete, req->base.data);
	ahash_request_set_crypt(cryptd_req, req->src, req->result,
				req->nbytes);
}

static int ghash_async_update(struct ahash_request *req)
{
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ghash_async_ctx *ctx = crypto_ahash_ctx(tfm);
	struct cryptd_ahash *cryptd_tfm = ctx->cryptd_tfm;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_ahash_queued(cryptd_tfm))) {
		ghash_init_cryptd_req(req);
		return crypto_ahash_update(cryptd_req);
	} else {
		struct shash_desc *desc = cryptd_shash_desc(cryptd_req);
		return shash_ahash_update(req, desc);
	}
}

static int ghash_async_final(struct ahash_request *req)
{
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ghash_async_ctx *ctx = crypto_ahash_ctx(tfm);
	struct cryptd_ahash *cryptd_tfm = ctx->cryptd_tfm;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_ahash_queued(cryptd_tfm))) {
		ghash_init_cryptd_req(req);
		return crypto_ahash_final(cryptd_req);
	} else {
		struct shash_desc *desc = cryptd_shash_desc(cryptd_req);
		return crypto_shash_final(desc, req->result);
	}
}

static int ghash_async_import(struct ahash_request *req, const void *in)
{
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct shash_desc *desc = cryptd_shash_desc(cryptd_req);
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	ghash_async_init(req);
	memcpy(dctx, in, sizeof(*dctx));
	return 0;

}

static int ghash_async_export(struct ahash_request *req, void *out)
{
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct shash_desc *desc = cryptd_shash_desc(cryptd_req);
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memcpy(out, dctx, sizeof(*dctx));
	return 0;

}

static int ghash_async_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ghash_async_ctx *ctx = crypto_ahash_ctx(tfm);
	struct ahash_request *cryptd_req = ahash_request_ctx(req);
	struct cryptd_ahash *cryptd_tfm = ctx->cryptd_tfm;

	if (!crypto_simd_usable() ||
	    (in_atomic() && cryptd_ahash_queued(cryptd_tfm))) {
		ghash_init_cryptd_req(req);
		return crypto_ahash_digest(cryptd_req);
	} else {
		struct shash_desc *desc = cryptd_shash_desc(cryptd_req);
		struct crypto_shash *child = cryptd_ahash_child(cryptd_tfm);

		desc->tfm = child;
		return shash_ahash_digest(req, desc);
	}
}

static int ghash_async_setkey(struct crypto_ahash *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct ghash_async_ctx *ctx = crypto_ahash_ctx(tfm);
	struct crypto_ahash *child = &ctx->cryptd_tfm->base;

	crypto_ahash_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_ahash_set_flags(child, crypto_ahash_get_flags(tfm)
			       & CRYPTO_TFM_REQ_MASK);
	return crypto_ahash_setkey(child, key, keylen);
}

static int ghash_async_init_tfm(struct crypto_tfm *tfm)
{
	struct cryptd_ahash *cryptd_tfm;
	struct ghash_async_ctx *ctx = crypto_tfm_ctx(tfm);

	cryptd_tfm = cryptd_alloc_ahash("__ghash-pclmulqdqni",
					CRYPTO_ALG_INTERNAL,
					CRYPTO_ALG_INTERNAL);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ctx->cryptd_tfm = cryptd_tfm;
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct ahash_request) +
				 crypto_ahash_reqsize(&cryptd_tfm->base));

	return 0;
}

static void ghash_async_exit_tfm(struct crypto_tfm *tfm)
{
	struct ghash_async_ctx *ctx = crypto_tfm_ctx(tfm);

	cryptd_free_ahash(ctx->cryptd_tfm);
}

static struct ahash_alg ghash_async_alg = {
	.init		= ghash_async_init,
	.update		= ghash_async_update,
	.final		= ghash_async_final,
	.setkey		= ghash_async_setkey,
	.digest		= ghash_async_digest,
	.export		= ghash_async_export,
	.import		= ghash_async_import,
	.halg = {
		.digestsize	= GHASH_DIGEST_SIZE,
		.statesize = sizeof(struct ghash_desc_ctx),
		.base = {
			.cra_name		= "ghash",
			.cra_driver_name	= "ghash-clmulni",
			.cra_priority		= 400,
			.cra_ctxsize		= sizeof(struct ghash_async_ctx),
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= GHASH_BLOCK_SIZE,
			.cra_module		= THIS_MODULE,
			.cra_init		= ghash_async_init_tfm,
			.cra_exit		= ghash_async_exit_tfm,
		},
	},
};

static const struct x86_cpu_id pcmul_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_PCLMULQDQ, NULL), /* Pickle-Mickle-Duck */
	{}
};
MODULE_DEVICE_TABLE(x86cpu, pcmul_cpu_id);

static int __init ghash_pclmulqdqni_mod_init(void)
{
	int err;

	if (!x86_match_cpu(pcmul_cpu_id))
		return -ENODEV;

	err = crypto_register_shash(&ghash_alg);
	if (err)
		goto err_out;
	err = crypto_register_ahash(&ghash_async_alg);
	if (err)
		goto err_shash;

	return 0;

err_shash:
	crypto_unregister_shash(&ghash_alg);
err_out:
	return err;
}

static void __exit ghash_pclmulqdqni_mod_exit(void)
{
	crypto_unregister_ahash(&ghash_async_alg);
	crypto_unregister_shash(&ghash_alg);
}

module_init(ghash_pclmulqdqni_mod_init);
module_exit(ghash_pclmulqdqni_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GHASH hash function, accelerated by PCLMULQDQ-NI");
MODULE_ALIAS_CRYPTO("ghash");
