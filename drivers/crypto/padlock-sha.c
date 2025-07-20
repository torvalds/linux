// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2006  Michal Ludvig <michal@logix.cz>
 */

#include <asm/cpu_device_id.h>
#include <crypto/internal/hash.h>
#include <crypto/padlock.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/cpufeature.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define PADLOCK_SHA_DESCSIZE (128 + ((PADLOCK_ALIGNMENT - 1) & \
				     ~(CRYPTO_MINALIGN - 1)))

struct padlock_sha_ctx {
	struct crypto_ahash *fallback;
};

static inline void *padlock_shash_desc_ctx(struct shash_desc *desc)
{
	return PTR_ALIGN(shash_desc_ctx(desc), PADLOCK_ALIGNMENT);
}

static int padlock_sha1_init(struct shash_desc *desc)
{
	struct sha1_state *sctx = padlock_shash_desc_ctx(desc);

	*sctx = (struct sha1_state){
		.state = { SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4 },
	};

	return 0;
}

static int padlock_sha256_init(struct shash_desc *desc)
{
	struct crypto_sha256_state *sctx = padlock_shash_desc_ctx(desc);

	sha256_block_init(sctx);
	return 0;
}

static int padlock_sha_update(struct shash_desc *desc,
			      const u8 *data, unsigned int length)
{
	u8 *state = padlock_shash_desc_ctx(desc);
	struct crypto_shash *tfm = desc->tfm;
	int err, remain;

	remain = length - round_down(length, crypto_shash_blocksize(tfm));
	{
		struct padlock_sha_ctx *ctx = crypto_shash_ctx(tfm);
		HASH_REQUEST_ON_STACK(req, ctx->fallback);

		ahash_request_set_callback(req, 0, NULL, NULL);
		ahash_request_set_virt(req, data, NULL, length - remain);
		err = crypto_ahash_import_core(req, state) ?:
		      crypto_ahash_update(req) ?:
		      crypto_ahash_export_core(req, state);
		HASH_REQUEST_ZERO(req);
	}

	return err ?: remain;
}

static int padlock_sha_export(struct shash_desc *desc, void *out)
{
	memcpy(out, padlock_shash_desc_ctx(desc),
	       crypto_shash_coresize(desc->tfm));
	return 0;
}

static int padlock_sha_import(struct shash_desc *desc, const void *in)
{
	unsigned int bs = crypto_shash_blocksize(desc->tfm);
	unsigned int ss = crypto_shash_coresize(desc->tfm);
	u64 *state = padlock_shash_desc_ctx(desc);

	memcpy(state, in, ss);

	/* Stop evil imports from generating a fault. */
	state[ss / 8 - 1] &= ~(bs - 1);

	return 0;
}

static inline void padlock_output_block(uint32_t *src,
		 	uint32_t *dst, size_t count)
{
	while (count--)
		*dst++ = swab32(*src++);
}

static int padlock_sha_finup(struct shash_desc *desc, const u8 *in,
			     unsigned int count, u8 *out)
{
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(desc->tfm);
	HASH_REQUEST_ON_STACK(req, ctx->fallback);

	ahash_request_set_callback(req, 0, NULL, NULL);
	ahash_request_set_virt(req, in, out, count);
	return crypto_ahash_import_core(req, padlock_shash_desc_ctx(desc)) ?:
	       crypto_ahash_finup(req);
}

static int padlock_sha1_finup(struct shash_desc *desc, const u8 *in,
			      unsigned int count, u8 *out)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	struct sha1_state *state = padlock_shash_desc_ctx(desc);
	u64 start = state->count;

	if (start + count > ULONG_MAX)
		return padlock_sha_finup(desc, in, count, out);

	asm volatile (".byte 0xf3,0x0f,0xa6,0xc8" /* rep xsha1 */
		      : \
		      : "c"((unsigned long)start + count), \
			"a"((unsigned long)start), \
			"S"(in), "D"(state));

	padlock_output_block(state->state, (uint32_t *)out, 5);
	return 0;
}

static int padlock_sha256_finup(struct shash_desc *desc, const u8 *in,
				unsigned int count, u8 *out)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	struct sha256_state *state = padlock_shash_desc_ctx(desc);
	u64 start = state->count;

	if (start + count > ULONG_MAX)
		return padlock_sha_finup(desc, in, count, out);

	asm volatile (".byte 0xf3,0x0f,0xa6,0xd0" /* rep xsha256 */
		      : \
		      : "c"((unsigned long)start + count), \
			"a"((unsigned long)start), \
			"S"(in), "D"(state));

	padlock_output_block(state->state, (uint32_t *)out, 8);
	return 0;
}

static int padlock_init_tfm(struct crypto_shash *hash)
{
	const char *fallback_driver_name = crypto_shash_alg_name(hash);
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(hash);
	struct crypto_ahash *fallback_tfm;

	/* Allocate a fallback and abort if it failed. */
	fallback_tfm = crypto_alloc_ahash(fallback_driver_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK |
					  CRYPTO_ALG_ASYNC);
	if (IS_ERR(fallback_tfm)) {
		printk(KERN_WARNING PFX "Fallback driver '%s' could not be loaded!\n",
		       fallback_driver_name);
		return PTR_ERR(fallback_tfm);
	}

	if (crypto_shash_statesize(hash) !=
	    crypto_ahash_statesize(fallback_tfm)) {
		crypto_free_ahash(fallback_tfm);
		return -EINVAL;
	}

	ctx->fallback = fallback_tfm;

	return 0;
}

static void padlock_exit_tfm(struct crypto_shash *hash)
{
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(hash);

	crypto_free_ahash(ctx->fallback);
}

static struct shash_alg sha1_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init   	= 	padlock_sha1_init,
	.update 	=	padlock_sha_update,
	.finup  	=	padlock_sha1_finup,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.init_tfm	=	padlock_init_tfm,
	.exit_tfm	=	padlock_exit_tfm,
	.descsize	=	PADLOCK_SHA_DESCSIZE,
	.statesize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name		=	"sha1",
		.cra_driver_name	=	"sha1-padlock",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_NEED_FALLBACK |
						CRYPTO_AHASH_ALG_BLOCK_ONLY |
						CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		=	SHA1_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
		.cra_module		=	THIS_MODULE,
	}
};

static struct shash_alg sha256_alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init   	= 	padlock_sha256_init,
	.update 	=	padlock_sha_update,
	.finup  	=	padlock_sha256_finup,
	.init_tfm	=	padlock_init_tfm,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.exit_tfm	=	padlock_exit_tfm,
	.descsize	=	PADLOCK_SHA_DESCSIZE,
	.statesize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name		=	"sha256",
		.cra_driver_name	=	"sha256-padlock",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_NEED_FALLBACK |
						CRYPTO_AHASH_ALG_BLOCK_ONLY |
						CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		=	SHA256_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
		.cra_module		=	THIS_MODULE,
	}
};

/* Add two shash_alg instance for hardware-implemented *
* multiple-parts hash supported by VIA Nano Processor.*/

static int padlock_sha1_update_nano(struct shash_desc *desc,
				    const u8 *src, unsigned int len)
{
	/*The PHE require the out buffer must 128 bytes and 16-bytes aligned*/
	struct sha1_state *state = padlock_shash_desc_ctx(desc);
	int blocks = len / SHA1_BLOCK_SIZE;

	len -= blocks * SHA1_BLOCK_SIZE;
	state->count += blocks * SHA1_BLOCK_SIZE;

	/* Process the left bytes from the input data */
	asm volatile (".byte 0xf3,0x0f,0xa6,0xc8"
		      : "+S"(src), "+D"(state)
		      : "a"((long)-1),
			"c"((unsigned long)blocks));
	return len;
}

static int padlock_sha256_update_nano(struct shash_desc *desc, const u8 *src,
			  unsigned int len)
{
	/*The PHE require the out buffer must 128 bytes and 16-bytes aligned*/
	struct crypto_sha256_state *state = padlock_shash_desc_ctx(desc);
	int blocks = len / SHA256_BLOCK_SIZE;

	len -= blocks * SHA256_BLOCK_SIZE;
	state->count += blocks * SHA256_BLOCK_SIZE;

	/* Process the left bytes from input data*/
	asm volatile (".byte 0xf3,0x0f,0xa6,0xd0"
		      : "+S"(src), "+D"(state)
		      : "a"((long)-1),
		      "c"((unsigned long)blocks));
	return len;
}

static struct shash_alg sha1_alg_nano = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	padlock_sha1_init,
	.update		=	padlock_sha1_update_nano,
	.finup  	=	padlock_sha1_finup,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.descsize	=	PADLOCK_SHA_DESCSIZE,
	.statesize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name		=	"sha1",
		.cra_driver_name	=	"sha1-padlock-nano",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
						CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		=	SHA1_BLOCK_SIZE,
		.cra_module		=	THIS_MODULE,
	}
};

static struct shash_alg sha256_alg_nano = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	padlock_sha256_init,
	.update		=	padlock_sha256_update_nano,
	.finup		=	padlock_sha256_finup,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.descsize	=	PADLOCK_SHA_DESCSIZE,
	.statesize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name		=	"sha256",
		.cra_driver_name	=	"sha256-padlock-nano",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
						CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize		=	SHA256_BLOCK_SIZE,
		.cra_module		=	THIS_MODULE,
	}
};

static const struct x86_cpu_id padlock_sha_ids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_PHE, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, padlock_sha_ids);

static int __init padlock_init(void)
{
	int rc = -ENODEV;
	struct cpuinfo_x86 *c = &cpu_data(0);
	struct shash_alg *sha1;
	struct shash_alg *sha256;

	if (!x86_match_cpu(padlock_sha_ids) || !boot_cpu_has(X86_FEATURE_PHE_EN))
		return -ENODEV;

	/* Register the newly added algorithm module if on *
	* VIA Nano processor, or else just do as before */
	if (c->x86_model < 0x0f) {
		sha1 = &sha1_alg;
		sha256 = &sha256_alg;
	} else {
		sha1 = &sha1_alg_nano;
		sha256 = &sha256_alg_nano;
	}

	rc = crypto_register_shash(sha1);
	if (rc)
		goto out;

	rc = crypto_register_shash(sha256);
	if (rc)
		goto out_unreg1;

	printk(KERN_NOTICE PFX "Using VIA PadLock ACE for SHA1/SHA256 algorithms.\n");

	return 0;

out_unreg1:
	crypto_unregister_shash(sha1);

out:
	printk(KERN_ERR PFX "VIA PadLock SHA1/SHA256 initialization failed.\n");
	return rc;
}

static void __exit padlock_fini(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);

	if (c->x86_model >= 0x0f) {
		crypto_unregister_shash(&sha1_alg_nano);
		crypto_unregister_shash(&sha256_alg_nano);
	} else {
		crypto_unregister_shash(&sha1_alg);
		crypto_unregister_shash(&sha256_alg);
	}
}

module_init(padlock_init);
module_exit(padlock_fini);

MODULE_DESCRIPTION("VIA PadLock SHA1/SHA256 algorithms support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig");

MODULE_ALIAS_CRYPTO("sha1-all");
MODULE_ALIAS_CRYPTO("sha256-all");
MODULE_ALIAS_CRYPTO("sha1-padlock");
MODULE_ALIAS_CRYPTO("sha256-padlock");
