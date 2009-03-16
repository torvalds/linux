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

#include <crypto/algapi.h>
#include <crypto/sha.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/cryptohash.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <asm/i387.h>
#include "padlock.h"

#define SHA1_DEFAULT_FALLBACK	"sha1-generic"
#define SHA256_DEFAULT_FALLBACK "sha256-generic"

struct padlock_sha_ctx {
	char		*data;
	size_t		used;
	int		bypass;
	void (*f_sha_padlock)(const char *in, char *out, int count);
	struct hash_desc fallback;
};

static inline struct padlock_sha_ctx *ctx(struct crypto_tfm *tfm)
{
	return crypto_tfm_ctx(tfm);
}

/* We'll need aligned address on the stack */
#define NEAREST_ALIGNED(ptr) \
	((void *)ALIGN((size_t)(ptr), PADLOCK_ALIGNMENT))

static struct crypto_alg sha1_alg, sha256_alg;

static void padlock_sha_bypass(struct crypto_tfm *tfm)
{
	if (ctx(tfm)->bypass)
		return;

	crypto_hash_init(&ctx(tfm)->fallback);
	if (ctx(tfm)->data && ctx(tfm)->used) {
		struct scatterlist sg;

		sg_init_one(&sg, ctx(tfm)->data, ctx(tfm)->used);
		crypto_hash_update(&ctx(tfm)->fallback, &sg, sg.length);
	}

	ctx(tfm)->used = 0;
	ctx(tfm)->bypass = 1;
}

static void padlock_sha_init(struct crypto_tfm *tfm)
{
	ctx(tfm)->used = 0;
	ctx(tfm)->bypass = 0;
}

static void padlock_sha_update(struct crypto_tfm *tfm,
			const uint8_t *data, unsigned int length)
{
	/* Our buffer is always one page. */
	if (unlikely(!ctx(tfm)->bypass &&
		     (ctx(tfm)->used + length > PAGE_SIZE)))
		padlock_sha_bypass(tfm);

	if (unlikely(ctx(tfm)->bypass)) {
		struct scatterlist sg;
		sg_init_one(&sg, (uint8_t *)data, length);
		crypto_hash_update(&ctx(tfm)->fallback, &sg, length);
		return;
	}

	memcpy(ctx(tfm)->data + ctx(tfm)->used, data, length);
	ctx(tfm)->used += length;
}

static inline void padlock_output_block(uint32_t *src,
		 	uint32_t *dst, size_t count)
{
	while (count--)
		*dst++ = swab32(*src++);
}

static void padlock_do_sha1(const char *in, char *out, int count)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	char buf[128+16];
	char *result = NEAREST_ALIGNED(buf);
	int ts_state;

	((uint32_t *)result)[0] = SHA1_H0;
	((uint32_t *)result)[1] = SHA1_H1;
	((uint32_t *)result)[2] = SHA1_H2;
	((uint32_t *)result)[3] = SHA1_H3;
	((uint32_t *)result)[4] = SHA1_H4;
 
	/* prevent taking the spurious DNA fault with padlock. */
	ts_state = irq_ts_save();
	asm volatile (".byte 0xf3,0x0f,0xa6,0xc8" /* rep xsha1 */
		      : "+S"(in), "+D"(result)
		      : "c"(count), "a"(0));
	irq_ts_restore(ts_state);

	padlock_output_block((uint32_t *)result, (uint32_t *)out, 5);
}

static void padlock_do_sha256(const char *in, char *out, int count)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	char buf[128+16];
	char *result = NEAREST_ALIGNED(buf);
	int ts_state;

	((uint32_t *)result)[0] = SHA256_H0;
	((uint32_t *)result)[1] = SHA256_H1;
	((uint32_t *)result)[2] = SHA256_H2;
	((uint32_t *)result)[3] = SHA256_H3;
	((uint32_t *)result)[4] = SHA256_H4;
	((uint32_t *)result)[5] = SHA256_H5;
	((uint32_t *)result)[6] = SHA256_H6;
	((uint32_t *)result)[7] = SHA256_H7;

	/* prevent taking the spurious DNA fault with padlock. */
	ts_state = irq_ts_save();
	asm volatile (".byte 0xf3,0x0f,0xa6,0xd0" /* rep xsha256 */
		      : "+S"(in), "+D"(result)
		      : "c"(count), "a"(0));
	irq_ts_restore(ts_state);

	padlock_output_block((uint32_t *)result, (uint32_t *)out, 8);
}

static void padlock_sha_final(struct crypto_tfm *tfm, uint8_t *out)
{
	if (unlikely(ctx(tfm)->bypass)) {
		crypto_hash_final(&ctx(tfm)->fallback, out);
		ctx(tfm)->bypass = 0;
		return;
	}

	/* Pass the input buffer to PadLock microcode... */
	ctx(tfm)->f_sha_padlock(ctx(tfm)->data, out, ctx(tfm)->used);

	ctx(tfm)->used = 0;
}

static int padlock_cra_init(struct crypto_tfm *tfm)
{
	const char *fallback_driver_name = tfm->__crt_alg->cra_name;
	struct crypto_hash *fallback_tfm;

	/* For now we'll allocate one page. This
	 * could eventually be configurable one day. */
	ctx(tfm)->data = (char *)__get_free_page(GFP_KERNEL);
	if (!ctx(tfm)->data)
		return -ENOMEM;

	/* Allocate a fallback and abort if it failed. */
	fallback_tfm = crypto_alloc_hash(fallback_driver_name, 0,
					 CRYPTO_ALG_ASYNC |
					 CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		printk(KERN_WARNING PFX "Fallback driver '%s' could not be loaded!\n",
		       fallback_driver_name);
		free_page((unsigned long)(ctx(tfm)->data));
		return PTR_ERR(fallback_tfm);
	}

	ctx(tfm)->fallback.tfm = fallback_tfm;
	return 0;
}

static int padlock_sha1_cra_init(struct crypto_tfm *tfm)
{
	ctx(tfm)->f_sha_padlock = padlock_do_sha1;

	return padlock_cra_init(tfm);
}

static int padlock_sha256_cra_init(struct crypto_tfm *tfm)
{
	ctx(tfm)->f_sha_padlock = padlock_do_sha256;

	return padlock_cra_init(tfm);
}

static void padlock_cra_exit(struct crypto_tfm *tfm)
{
	if (ctx(tfm)->data) {
		free_page((unsigned long)(ctx(tfm)->data));
		ctx(tfm)->data = NULL;
	}

	crypto_free_hash(ctx(tfm)->fallback.tfm);
	ctx(tfm)->fallback.tfm = NULL;
}

static struct crypto_alg sha1_alg = {
	.cra_name		=	"sha1",
	.cra_driver_name	=	"sha1-padlock",
	.cra_priority		=	PADLOCK_CRA_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_DIGEST |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	SHA1_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(sha1_alg.cra_list),
	.cra_init		=	padlock_sha1_cra_init,
	.cra_exit		=	padlock_cra_exit,
	.cra_u			=	{
		.digest = {
			.dia_digestsize	=	SHA1_DIGEST_SIZE,
			.dia_init   	= 	padlock_sha_init,
			.dia_update 	=	padlock_sha_update,
			.dia_final  	=	padlock_sha_final,
		}
	}
};

static struct crypto_alg sha256_alg = {
	.cra_name		=	"sha256",
	.cra_driver_name	=	"sha256-padlock",
	.cra_priority		=	PADLOCK_CRA_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_DIGEST |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	SHA256_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(sha256_alg.cra_list),
	.cra_init		=	padlock_sha256_cra_init,
	.cra_exit		=	padlock_cra_exit,
	.cra_u			=	{
		.digest = {
			.dia_digestsize	=	SHA256_DIGEST_SIZE,
			.dia_init   	= 	padlock_sha_init,
			.dia_update 	=	padlock_sha_update,
			.dia_final  	=	padlock_sha_final,
		}
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

	rc = crypto_register_alg(&sha1_alg);
	if (rc)
		goto out;

	rc = crypto_register_alg(&sha256_alg);
	if (rc)
		goto out_unreg1;

	printk(KERN_NOTICE PFX "Using VIA PadLock ACE for SHA1/SHA256 algorithms.\n");

	return 0;

out_unreg1:
	crypto_unregister_alg(&sha1_alg);
out:
	printk(KERN_ERR PFX "VIA PadLock SHA1/SHA256 initialization failed.\n");
	return rc;
}

static void __exit padlock_fini(void)
{
	crypto_unregister_alg(&sha1_alg);
	crypto_unregister_alg(&sha256_alg);
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
