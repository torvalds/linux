/*
 * Accelerated CRC32(C) using ARM CRC, NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpufeature.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>

#define PMULL_MIN_LEN		64L	/* minimum size of buffer
					 * for crc32_pmull_le_16 */
#define SCALE_F			16L	/* size of NEON register */

asmlinkage u32 crc32_pmull_le(const u8 buf[], u32 len, u32 init_crc);
asmlinkage u32 crc32_armv8_le(u32 init_crc, const u8 buf[], u32 len);

asmlinkage u32 crc32c_pmull_le(const u8 buf[], u32 len, u32 init_crc);
asmlinkage u32 crc32c_armv8_le(u32 init_crc, const u8 buf[], u32 len);

static u32 (*fallback_crc32)(u32 init_crc, const u8 buf[], u32 len);
static u32 (*fallback_crc32c)(u32 init_crc, const u8 buf[], u32 len);

static int crc32_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = 0;
	return 0;
}

static int crc32c_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = ~0;
	return 0;
}

static int crc32_setkey(struct crypto_shash *hash, const u8 *key,
			unsigned int keylen)
{
	u32 *mctx = crypto_shash_ctx(hash);

	if (keylen != sizeof(u32)) {
		crypto_shash_set_flags(hash, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	*mctx = le32_to_cpup((__le32 *)key);
	return 0;
}

static int crc32_init(struct shash_desc *desc)
{
	u32 *mctx = crypto_shash_ctx(desc->tfm);
	u32 *crc = shash_desc_ctx(desc);

	*crc = *mctx;
	return 0;
}

static int crc32_update(struct shash_desc *desc, const u8 *data,
			unsigned int length)
{
	u32 *crc = shash_desc_ctx(desc);

	*crc = crc32_armv8_le(*crc, data, length);
	return 0;
}

static int crc32c_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	u32 *crc = shash_desc_ctx(desc);

	*crc = crc32c_armv8_le(*crc, data, length);
	return 0;
}

static int crc32_final(struct shash_desc *desc, u8 *out)
{
	u32 *crc = shash_desc_ctx(desc);

	put_unaligned_le32(*crc, out);
	return 0;
}

static int crc32c_final(struct shash_desc *desc, u8 *out)
{
	u32 *crc = shash_desc_ctx(desc);

	put_unaligned_le32(~*crc, out);
	return 0;
}

static int crc32_pmull_update(struct shash_desc *desc, const u8 *data,
			      unsigned int length)
{
	u32 *crc = shash_desc_ctx(desc);
	unsigned int l;

	if (crypto_simd_usable()) {
		if ((u32)data % SCALE_F) {
			l = min_t(u32, length, SCALE_F - ((u32)data % SCALE_F));

			*crc = fallback_crc32(*crc, data, l);

			data += l;
			length -= l;
		}

		if (length >= PMULL_MIN_LEN) {
			l = round_down(length, SCALE_F);

			kernel_neon_begin();
			*crc = crc32_pmull_le(data, l, *crc);
			kernel_neon_end();

			data += l;
			length -= l;
		}
	}

	if (length > 0)
		*crc = fallback_crc32(*crc, data, length);

	return 0;
}

static int crc32c_pmull_update(struct shash_desc *desc, const u8 *data,
			       unsigned int length)
{
	u32 *crc = shash_desc_ctx(desc);
	unsigned int l;

	if (crypto_simd_usable()) {
		if ((u32)data % SCALE_F) {
			l = min_t(u32, length, SCALE_F - ((u32)data % SCALE_F));

			*crc = fallback_crc32c(*crc, data, l);

			data += l;
			length -= l;
		}

		if (length >= PMULL_MIN_LEN) {
			l = round_down(length, SCALE_F);

			kernel_neon_begin();
			*crc = crc32c_pmull_le(data, l, *crc);
			kernel_neon_end();

			data += l;
			length -= l;
		}
	}

	if (length > 0)
		*crc = fallback_crc32c(*crc, data, length);

	return 0;
}

static struct shash_alg crc32_pmull_algs[] = { {
	.setkey			= crc32_setkey,
	.init			= crc32_init,
	.update			= crc32_update,
	.final			= crc32_final,
	.descsize		= sizeof(u32),
	.digestsize		= sizeof(u32),

	.base.cra_ctxsize	= sizeof(u32),
	.base.cra_init		= crc32_cra_init,
	.base.cra_name		= "crc32",
	.base.cra_driver_name	= "crc32-arm-ce",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_blocksize	= 1,
	.base.cra_module	= THIS_MODULE,
}, {
	.setkey			= crc32_setkey,
	.init			= crc32_init,
	.update			= crc32c_update,
	.final			= crc32c_final,
	.descsize		= sizeof(u32),
	.digestsize		= sizeof(u32),

	.base.cra_ctxsize	= sizeof(u32),
	.base.cra_init		= crc32c_cra_init,
	.base.cra_name		= "crc32c",
	.base.cra_driver_name	= "crc32c-arm-ce",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
	.base.cra_blocksize	= 1,
	.base.cra_module	= THIS_MODULE,
} };

static int __init crc32_pmull_mod_init(void)
{
	if (elf_hwcap2 & HWCAP2_PMULL) {
		crc32_pmull_algs[0].update = crc32_pmull_update;
		crc32_pmull_algs[1].update = crc32c_pmull_update;

		if (elf_hwcap2 & HWCAP2_CRC32) {
			fallback_crc32 = crc32_armv8_le;
			fallback_crc32c = crc32c_armv8_le;
		} else {
			fallback_crc32 = crc32_le;
			fallback_crc32c = __crc32c_le;
		}
	} else if (!(elf_hwcap2 & HWCAP2_CRC32)) {
		return -ENODEV;
	}

	return crypto_register_shashes(crc32_pmull_algs,
				       ARRAY_SIZE(crc32_pmull_algs));
}

static void __exit crc32_pmull_mod_exit(void)
{
	crypto_unregister_shashes(crc32_pmull_algs,
				  ARRAY_SIZE(crc32_pmull_algs));
}

static const struct cpu_feature __maybe_unused crc32_cpu_feature[] = {
	{ cpu_feature(CRC32) }, { cpu_feature(PMULL) }, { }
};
MODULE_DEVICE_TABLE(cpu, crc32_cpu_feature);

module_init(crc32_pmull_mod_init);
module_exit(crc32_pmull_mod_exit);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("crc32");
MODULE_ALIAS_CRYPTO("crc32c");
