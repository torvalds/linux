// SPDX-License-Identifier: GPL-2.0

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/xxhash.h>
#include <linux/unaligned.h>

#define XXHASH64_BLOCK_SIZE	32
#define XXHASH64_DIGEST_SIZE	8

struct xxhash64_tfm_ctx {
	u64 seed;
};

struct xxhash64_desc_ctx {
	struct xxh64_state xxhstate;
};

static int xxhash64_setkey(struct crypto_shash *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct xxhash64_tfm_ctx *tctx = crypto_shash_ctx(tfm);

	if (keylen != sizeof(tctx->seed))
		return -EINVAL;
	tctx->seed = get_unaligned_le64(key);
	return 0;
}

static int xxhash64_init(struct shash_desc *desc)
{
	struct xxhash64_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct xxhash64_desc_ctx *dctx = shash_desc_ctx(desc);

	xxh64_reset(&dctx->xxhstate, tctx->seed);

	return 0;
}

static int xxhash64_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct xxhash64_desc_ctx *dctx = shash_desc_ctx(desc);

	xxh64_update(&dctx->xxhstate, data, length);

	return 0;
}

static int xxhash64_final(struct shash_desc *desc, u8 *out)
{
	struct xxhash64_desc_ctx *dctx = shash_desc_ctx(desc);

	put_unaligned_le64(xxh64_digest(&dctx->xxhstate), out);

	return 0;
}

static int xxhash64_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	struct xxhash64_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);

	put_unaligned_le64(xxh64(data, length, tctx->seed), out);

	return 0;
}

static struct shash_alg alg = {
	.digestsize	= XXHASH64_DIGEST_SIZE,
	.setkey		= xxhash64_setkey,
	.init		= xxhash64_init,
	.update		= xxhash64_update,
	.final		= xxhash64_final,
	.digest		= xxhash64_digest,
	.descsize	= sizeof(struct xxhash64_desc_ctx),
	.base		= {
		.cra_name	 = "xxhash64",
		.cra_driver_name = "xxhash64-generic",
		.cra_priority	 = 100,
		.cra_flags	 = CRYPTO_ALG_OPTIONAL_KEY,
		.cra_blocksize	 = XXHASH64_BLOCK_SIZE,
		.cra_ctxsize	 = sizeof(struct xxhash64_tfm_ctx),
		.cra_module	 = THIS_MODULE,
	}
};

static int __init xxhash_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit xxhash_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

subsys_initcall(xxhash_mod_init);
module_exit(xxhash_mod_fini);

MODULE_AUTHOR("Nikolay Borisov <nborisov@suse.com>");
MODULE_DESCRIPTION("xxhash calculations wrapper for lib/xxhash.c");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("xxhash64");
MODULE_ALIAS_CRYPTO("xxhash64-generic");
