// SPDX-License-Identifier: GPL-2.0-only
/*
 * Using hardware provided CRC32 instruction to accelerate the CRC32 disposal.
 * CRC32C polynomial:0x1EDC6F41(BE)/0x82F63B78(LE)
 * CRC32 is a new instruction in Intel SSE4.2, the reference can be found at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2A: Instruction Set Reference, A-M
 *
 * Copyright (C) 2008 Intel Corporation
 * Authors: Austin Zhang <austin_zhang@linux.intel.com>
 *          Kent Liu <kent.liu@intel.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>

#include <asm/cpufeatures.h>
#include <asm/cpu_device_id.h>
#include <asm/simd.h>

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

#define SCALE_F	sizeof(unsigned long)

#ifdef CONFIG_X86_64
#define CRC32_INST "crc32q %1, %q0"
#else
#define CRC32_INST "crc32l %1, %0"
#endif

#ifdef CONFIG_X86_64
/*
 * use carryless multiply version of crc32c when buffer
 * size is >= 512 to account
 * for fpu state save/restore overhead.
 */
#define CRC32C_PCL_BREAKEVEN	512

asmlinkage unsigned int crc_pcl(const u8 *buffer, int len,
				unsigned int crc_init);
#endif /* CONFIG_X86_64 */

static u32 crc32c_intel_le_hw_byte(u32 crc, unsigned char const *data, size_t length)
{
	while (length--) {
		asm("crc32b %1, %0"
		    : "+r" (crc) : "rm" (*data));
		data++;
	}

	return crc;
}

static u32 __pure crc32c_intel_le_hw(u32 crc, unsigned char const *p, size_t len)
{
	unsigned int iquotient = len / SCALE_F;
	unsigned int iremainder = len % SCALE_F;
	unsigned long *ptmp = (unsigned long *)p;

	while (iquotient--) {
		asm(CRC32_INST
		    : "+r" (crc) : "rm" (*ptmp));
		ptmp++;
	}

	if (iremainder)
		crc = crc32c_intel_le_hw_byte(crc, (unsigned char *)ptmp,
				 iremainder);

	return crc;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int crc32c_intel_setkey(struct crypto_shash *hash, const u8 *key,
			unsigned int keylen)
{
	u32 *mctx = crypto_shash_ctx(hash);

	if (keylen != sizeof(u32))
		return -EINVAL;
	*mctx = le32_to_cpup((__le32 *)key);
	return 0;
}

static int crc32c_intel_init(struct shash_desc *desc)
{
	u32 *mctx = crypto_shash_ctx(desc->tfm);
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = *mctx;

	return 0;
}

static int crc32c_intel_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len)
{
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = crc32c_intel_le_hw(*crcp, data, len);
	return 0;
}

static int __crc32c_intel_finup(u32 *crcp, const u8 *data, unsigned int len,
				u8 *out)
{
	*(__le32 *)out = ~cpu_to_le32(crc32c_intel_le_hw(*crcp, data, len));
	return 0;
}

static int crc32c_intel_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return __crc32c_intel_finup(shash_desc_ctx(desc), data, len, out);
}

static int crc32c_intel_final(struct shash_desc *desc, u8 *out)
{
	u32 *crcp = shash_desc_ctx(desc);

	*(__le32 *)out = ~cpu_to_le32p(crcp);
	return 0;
}

static int crc32c_intel_digest(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *out)
{
	return __crc32c_intel_finup(crypto_shash_ctx(desc->tfm), data, len,
				    out);
}

static int crc32c_intel_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = ~0;

	return 0;
}

#ifdef CONFIG_X86_64
static int crc32c_pcl_intel_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len)
{
	u32 *crcp = shash_desc_ctx(desc);

	/*
	 * use faster PCL version if datasize is large enough to
	 * overcome kernel fpu state save/restore overhead
	 */
	if (len >= CRC32C_PCL_BREAKEVEN && crypto_simd_usable()) {
		kernel_fpu_begin();
		*crcp = crc_pcl(data, len, *crcp);
		kernel_fpu_end();
	} else
		*crcp = crc32c_intel_le_hw(*crcp, data, len);
	return 0;
}

static int __crc32c_pcl_intel_finup(u32 *crcp, const u8 *data, unsigned int len,
				u8 *out)
{
	if (len >= CRC32C_PCL_BREAKEVEN && crypto_simd_usable()) {
		kernel_fpu_begin();
		*(__le32 *)out = ~cpu_to_le32(crc_pcl(data, len, *crcp));
		kernel_fpu_end();
	} else
		*(__le32 *)out =
			~cpu_to_le32(crc32c_intel_le_hw(*crcp, data, len));
	return 0;
}

static int crc32c_pcl_intel_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return __crc32c_pcl_intel_finup(shash_desc_ctx(desc), data, len, out);
}

static int crc32c_pcl_intel_digest(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *out)
{
	return __crc32c_pcl_intel_finup(crypto_shash_ctx(desc->tfm), data, len,
				    out);
}
#endif /* CONFIG_X86_64 */

static struct shash_alg alg = {
	.setkey			=	crc32c_intel_setkey,
	.init			=	crc32c_intel_init,
	.update			=	crc32c_intel_update,
	.final			=	crc32c_intel_final,
	.finup			=	crc32c_intel_finup,
	.digest			=	crc32c_intel_digest,
	.descsize		=	sizeof(u32),
	.digestsize		=	CHKSUM_DIGEST_SIZE,
	.base			=	{
		.cra_name		=	"crc32c",
		.cra_driver_name	=	"crc32c-intel",
		.cra_priority		=	200,
		.cra_flags		=	CRYPTO_ALG_OPTIONAL_KEY,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(u32),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	crc32c_intel_cra_init,
	}
};

static const struct x86_cpu_id crc32c_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_XMM4_2, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, crc32c_cpu_id);

static int __init crc32c_intel_mod_init(void)
{
	if (!x86_match_cpu(crc32c_cpu_id))
		return -ENODEV;
#ifdef CONFIG_X86_64
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		alg.update = crc32c_pcl_intel_update;
		alg.finup = crc32c_pcl_intel_finup;
		alg.digest = crc32c_pcl_intel_digest;
	}
#endif
	return crypto_register_shash(&alg);
}

static void __exit crc32c_intel_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(crc32c_intel_mod_init);
module_exit(crc32c_intel_mod_fini);

MODULE_AUTHOR("Austin Zhang <austin.zhang@intel.com>, Kent Liu <kent.liu@intel.com>");
MODULE_DESCRIPTION("CRC32c (Castagnoli) optimization using Intel Hardware.");
MODULE_LICENSE("GPL");

MODULE_ALIAS_CRYPTO("crc32c");
MODULE_ALIAS_CRYPTO("crc32c-intel");
