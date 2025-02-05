// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Xyratex Technology Limited
 */

/*
 * This is crypto api shash wrappers to crc32_le.
 */

#include <linux/unaligned.h>
#include <linux/crc32.h>
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

/** No default init with ~0 */
static int crc32_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = 0;

	return 0;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int crc32_setkey(struct crypto_shash *hash, const u8 *key,
			unsigned int keylen)
{
	u32 *mctx = crypto_shash_ctx(hash);

	if (keylen != sizeof(u32))
		return -EINVAL;
	*mctx = get_unaligned_le32(key);
	return 0;
}

static int crc32_init(struct shash_desc *desc)
{
	u32 *mctx = crypto_shash_ctx(desc->tfm);
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = *mctx;

	return 0;
}

static int crc32_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = crc32_le_base(*crcp, data, len);
	return 0;
}

static int crc32_update_arch(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = crc32_le(*crcp, data, len);
	return 0;
}

/* No final XOR 0xFFFFFFFF, like crc32_le */
static int __crc32_finup(u32 *crcp, const u8 *data, unsigned int len,
			 u8 *out)
{
	put_unaligned_le32(crc32_le_base(*crcp, data, len), out);
	return 0;
}

static int __crc32_finup_arch(u32 *crcp, const u8 *data, unsigned int len,
			      u8 *out)
{
	put_unaligned_le32(crc32_le(*crcp, data, len), out);
	return 0;
}

static int crc32_finup(struct shash_desc *desc, const u8 *data,
		       unsigned int len, u8 *out)
{
	return __crc32_finup(shash_desc_ctx(desc), data, len, out);
}

static int crc32_finup_arch(struct shash_desc *desc, const u8 *data,
		       unsigned int len, u8 *out)
{
	return __crc32_finup_arch(shash_desc_ctx(desc), data, len, out);
}

static int crc32_final(struct shash_desc *desc, u8 *out)
{
	u32 *crcp = shash_desc_ctx(desc);

	put_unaligned_le32(*crcp, out);
	return 0;
}

static int crc32_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	return __crc32_finup(crypto_shash_ctx(desc->tfm), data, len, out);
}

static int crc32_digest_arch(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	return __crc32_finup_arch(crypto_shash_ctx(desc->tfm), data, len, out);
}

static struct shash_alg algs[] = {{
	.setkey			= crc32_setkey,
	.init			= crc32_init,
	.update			= crc32_update,
	.final			= crc32_final,
	.finup			= crc32_finup,
	.digest			= crc32_digest,
	.descsize		= sizeof(u32),
	.digestsize		= CHKSUM_DIGEST_SIZE,

	.base.cra_name		= "crc32",
	.base.cra_driver_name	= "crc32-generic",
	.base.cra_priority	= 100,
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_blocksize	= CHKSUM_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(u32),
	.base.cra_module	= THIS_MODULE,
	.base.cra_init		= crc32_cra_init,
}, {
	.setkey			= crc32_setkey,
	.init			= crc32_init,
	.update			= crc32_update_arch,
	.final			= crc32_final,
	.finup			= crc32_finup_arch,
	.digest			= crc32_digest_arch,
	.descsize		= sizeof(u32),
	.digestsize		= CHKSUM_DIGEST_SIZE,

	.base.cra_name		= "crc32",
	.base.cra_driver_name	= "crc32-" __stringify(ARCH),
	.base.cra_priority	= 150,
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_blocksize	= CHKSUM_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(u32),
	.base.cra_module	= THIS_MODULE,
	.base.cra_init		= crc32_cra_init,
}};

static int num_algs;

static int __init crc32_mod_init(void)
{
	/* register the arch flavor only if it differs from the generic one */
	num_algs = 1 + ((crc32_optimizations() & CRC32_LE_OPTIMIZATION) != 0);

	return crypto_register_shashes(algs, num_algs);
}

static void __exit crc32_mod_fini(void)
{
	crypto_unregister_shashes(algs, num_algs);
}

subsys_initcall(crc32_mod_init);
module_exit(crc32_mod_fini);

MODULE_AUTHOR("Alexander Boyko <alexander_boyko@xyratex.com>");
MODULE_DESCRIPTION("CRC32 calculations wrapper for lib/crc32");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("crc32");
MODULE_ALIAS_CRYPTO("crc32-generic");
