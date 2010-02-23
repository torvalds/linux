/*
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2006  Michal Ludvig <michal@logix.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <asm/i387.h>
#include "padlock.h"

#ifdef CONFIG_64BIT
#define STACK_ALIGN 16
#else
#define STACK_ALIGN 4
#endif

struct padlock_sha_desc {
	struct shash_desc fallback;
};

struct padlock_sha_ctx {
	struct crypto_shash *fallback;
};

static int padlock_sha_init(struct shash_desc *desc)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(desc->tfm);

	dctx->fallback.tfm = ctx->fallback;
	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_shash_init(&dctx->fallback);
}

static int padlock_sha_update(struct shash_desc *desc,
			      const u8 *data, unsigned int length)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);

	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_shash_update(&dctx->fallback, data, length);
}

static int padlock_sha_export(struct shash_desc *desc, void *out)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);

	return crypto_shash_export(&dctx->fallback, out);
}

static int padlock_sha_import(struct shash_desc *desc, const void *in)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(desc->tfm);

	dctx->fallback.tfm = ctx->fallback;
	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_shash_import(&dctx->fallback, in);
}

static inline void padlock_output_block(uint32_t *src,
		 	uint32_t *dst, size_t count)
{
	while (count--)
		*dst++ = swab32(*src++);
}

static int padlock_sha1_finup(struct shash_desc *desc, const u8 *in,
			      unsigned int count, u8 *out)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	char buf[128 + PADLOCK_ALIGNMENT - STACK_ALIGN] __attribute__
		((aligned(STACK_ALIGN)));
	char *result = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct sha1_state state;
	unsigned int space;
	unsigned int leftover;
	int ts_state;
	int err;

	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_shash_export(&dctx->fallback, &state);
	if (err)
		goto out;

	if (state.count + count > ULONG_MAX)
		return crypto_shash_finup(&dctx->fallback, in, count, out);

	leftover = ((state.count - 1) & (SHA1_BLOCK_SIZE - 1)) + 1;
	space =  SHA1_BLOCK_SIZE - leftover;
	if (space) {
		if (count > space) {
			err = crypto_shash_update(&dctx->fallback, in, space) ?:
			      crypto_shash_export(&dctx->fallback, &state);
			if (err)
				goto out;
			count -= space;
			in += space;
		} else {
			memcpy(state.buffer + leftover, in, count);
			in = state.buffer;
			count += leftover;
			state.count &= ~(SHA1_BLOCK_SIZE - 1);
		}
	}

	memcpy(result, &state.state, SHA1_DIGEST_SIZE);

	/* prevent taking the spurious DNA fault with padlock. */
	ts_state = irq_ts_save();
	asm volatile (".byte 0xf3,0x0f,0xa6,0xc8" /* rep xsha1 */
		      : \
		      : "c"((unsigned long)state.count + count), \
			"a"((unsigned long)state.count), \
			"S"(in), "D"(result));
	irq_ts_restore(ts_state);

	padlock_output_block((uint32_t *)result, (uint32_t *)out, 5);

out:
	return err;
}

static int padlock_sha1_final(struct shash_desc *desc, u8 *out)
{
	u8 buf[4];

	return padlock_sha1_finup(desc, buf, 0, out);
}

static int padlock_sha256_finup(struct shash_desc *desc, const u8 *in,
				unsigned int count, u8 *out)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	char buf[128 + PADLOCK_ALIGNMENT - STACK_ALIGN] __attribute__
		((aligned(STACK_ALIGN)));
	char *result = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct sha256_state state;
	unsigned int space;
	unsigned int leftover;
	int ts_state;
	int err;

	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_shash_export(&dctx->fallback, &state);
	if (err)
		goto out;

	if (state.count + count > ULONG_MAX)
		return crypto_shash_finup(&dctx->fallback, in, count, out);

	leftover = ((state.count - 1) & (SHA256_BLOCK_SIZE - 1)) + 1;
	space =  SHA256_BLOCK_SIZE - leftover;
	if (space) {
		if (count > space) {
			err = crypto_shash_update(&dctx->fallback, in, space) ?:
			      crypto_shash_export(&dctx->fallback, &state);
			if (err)
				goto out;
			count -= space;
			in += space;
		} else {
			memcpy(state.buf + leftover, in, count);
			in = state.buf;
			count += leftover;
			state.count &= ~(SHA1_BLOCK_SIZE - 1);
		}
	}

	memcpy(result, &state.state, SHA256_DIGEST_SIZE);

	/* prevent taking the spurious DNA fault with padlock. */
	ts_state = irq_ts_save();
	asm volatile (".byte 0xf3,0x0f,0xa6,0xd0" /* rep xsha256 */
		      : \
		      : "c"((unsigned long)state.count + count), \
			"a"((unsigned long)state.count), \
			"S"(in), "D"(result));
	irq_ts_restore(ts_state);

	padlock_output_block((uint32_t *)result, (uint32_t *)out, 8);

out:
	return err;
}

static int padlock_sha256_final(struct shash_desc *desc, u8 *out)
{
	u8 buf[4];

	return padlock_sha256_finup(desc, buf, 0, out);
}

static int padlock_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_shash *hash = __crypto_shash_cast(tfm);
	const char *fallback_driver_name = tfm->__crt_alg->cra_name;
	struct padlock_sha_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *fallback_tfm;
	int err = -ENOMEM;

	/* Allocate a fallback and abort if it failed. */
	fallback_tfm = crypto_alloc_shash(fallback_driver_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		printk(KERN_WARNING PFX "Fallback driver '%s' could not be loaded!\n",
		       fallback_driver_name);
		err = PTR_ERR(fallback_tfm);
		goto out;
	}

	ctx->fallback = fallback_tfm;
	hash->descsize += crypto_shash_descsize(fallback_tfm);
	return 0;

out:
	return err;
}

static void padlock_cra_exit(struct crypto_tfm *tfm)
{
	struct padlock_sha_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(ctx->fallback);
}

static struct shash_alg sha1_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init   	= 	padlock_sha_init,
	.update 	=	padlock_sha_update,
	.finup  	=	padlock_sha1_finup,
	.final  	=	padlock_sha1_final,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.descsize	=	sizeof(struct padlock_sha_desc),
	.statesize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name		=	"sha1",
		.cra_driver_name	=	"sha1-padlock",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_TYPE_SHASH |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		=	SHA1_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	padlock_cra_init,
		.cra_exit		=	padlock_cra_exit,
	}
};

static struct shash_alg sha256_alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init   	= 	padlock_sha_init,
	.update 	=	padlock_sha_update,
	.finup  	=	padlock_sha256_finup,
	.final  	=	padlock_sha256_final,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.descsize	=	sizeof(struct padlock_sha_desc),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name		=	"sha256",
		.cra_driver_name	=	"sha256-padlock",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_TYPE_SHASH |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		=	SHA256_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	padlock_cra_init,
		.cra_exit		=	padlock_cra_exit,
	}
};

static int __init padlock_init(void)
{
	int rc = -ENODEV;

	if (!cpu_has_phe) {
		printk(KERN_NOTICE PFX "VIA PadLock Hash Engine not detected.\n");
		return -ENODEV;
	}

	if (!cpu_has_phe_enabled) {
		printk(KERN_NOTICE PFX "VIA PadLock detected, but not enabled. Hmm, strange...\n");
		return -ENODEV;
	}

	rc = crypto_register_shash(&sha1_alg);
	if (rc)
		goto out;

	rc = crypto_register_shash(&sha256_alg);
	if (rc)
		goto out_unreg1;

	printk(KERN_NOTICE PFX "Using VIA PadLock ACE for SHA1/SHA256 algorithms.\n");

	return 0;

out_unreg1:
	crypto_unregister_shash(&sha1_alg);
out:
	printk(KERN_ERR PFX "VIA PadLock SHA1/SHA256 initialization failed.\n");
	return rc;
}

static void __exit padlock_fini(void)
{
	crypto_unregister_shash(&sha1_alg);
	crypto_unregister_shash(&sha256_alg);
}

module_init(padlock_init);
module_exit(padlock_fini);

MODULE_DESCRIPTION("VIA PadLock SHA1/SHA256 algorithms support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig");

MODULE_ALIAS("sha1-all");
MODULE_ALIAS("sha256-all");
MODULE_ALIAS("sha1-padlock");
MODULE_ALIAS("sha256-padlock");
