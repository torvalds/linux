// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for CRC32C optimized for sparc64 crypto opcodes.
 *
 * This is based largely upon arch/x86/crypto/crc32c-intel.c
 *
 * Copyright (C) 2008 Intel Corporation
 * Authors: Austin Zhang <austin_zhang@linux.intel.com>
 *          Kent Liu <kent.liu@intel.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/crc32.h>

#include <crypto/internal/hash.h>

#include <asm/pstate.h>
#include <asm/elf.h>
#include <linux/unaligned.h>

#include "opcodes.h"

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int crc32c_sparc64_setkey(struct crypto_shash *hash, const u8 *key,
				 unsigned int keylen)
{
	u32 *mctx = crypto_shash_ctx(hash);

	if (keylen != sizeof(u32))
		return -EINVAL;
	*mctx = get_unaligned_le32(key);
	return 0;
}

static int crc32c_sparc64_init(struct shash_desc *desc)
{
	u32 *mctx = crypto_shash_ctx(desc->tfm);
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = *mctx;

	return 0;
}

extern void crc32c_sparc64(u32 *crcp, const u64 *data, unsigned int len);

static u32 crc32c_compute(u32 crc, const u8 *data, unsigned int len)
{
	unsigned int n = -(uintptr_t)data & 7;

	if (n) {
		/* Data isn't 8-byte aligned.  Align it. */
		n = min(n, len);
		crc = __crc32c_le(crc, data, n);
		data += n;
		len -= n;
	}
	n = len & ~7U;
	if (n) {
		crc32c_sparc64(&crc, (const u64 *)data, n);
		data += n;
		len -= n;
	}
	if (len)
		crc = __crc32c_le(crc, data, len);
	return crc;
}

static int crc32c_sparc64_update(struct shash_desc *desc, const u8 *data,
				 unsigned int len)
{
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = crc32c_compute(*crcp, data, len);
	return 0;
}

static int __crc32c_sparc64_finup(const u32 *crcp, const u8 *data,
				  unsigned int len, u8 *out)
{
	put_unaligned_le32(~crc32c_compute(*crcp, data, len), out);
	return 0;
}

static int crc32c_sparc64_finup(struct shash_desc *desc, const u8 *data,
				unsigned int len, u8 *out)
{
	return __crc32c_sparc64_finup(shash_desc_ctx(desc), data, len, out);
}

static int crc32c_sparc64_final(struct shash_desc *desc, u8 *out)
{
	u32 *crcp = shash_desc_ctx(desc);

	put_unaligned_le32(~*crcp, out);
	return 0;
}

static int crc32c_sparc64_digest(struct shash_desc *desc, const u8 *data,
				 unsigned int len, u8 *out)
{
	return __crc32c_sparc64_finup(crypto_shash_ctx(desc->tfm), data, len,
				      out);
}

static int crc32c_sparc64_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = ~0;

	return 0;
}

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

static struct shash_alg alg = {
	.setkey			=	crc32c_sparc64_setkey,
	.init			=	crc32c_sparc64_init,
	.update			=	crc32c_sparc64_update,
	.final			=	crc32c_sparc64_final,
	.finup			=	crc32c_sparc64_finup,
	.digest			=	crc32c_sparc64_digest,
	.descsize		=	sizeof(u32),
	.digestsize		=	CHKSUM_DIGEST_SIZE,
	.base			=	{
		.cra_name		=	"crc32c",
		.cra_driver_name	=	"crc32c-sparc64",
		.cra_priority		=	SPARC_CR_OPCODE_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_OPTIONAL_KEY,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(u32),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	crc32c_sparc64_cra_init,
	}
};

static bool __init sparc64_has_crc32c_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_CRC32C))
		return false;

	return true;
}

static int __init crc32c_sparc64_mod_init(void)
{
	if (sparc64_has_crc32c_opcode()) {
		pr_info("Using sparc64 crc32c opcode optimized CRC32C implementation\n");
		return crypto_register_shash(&alg);
	}
	pr_info("sparc64 crc32c opcode not available.\n");
	return -ENODEV;
}

static void __exit crc32c_sparc64_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(crc32c_sparc64_mod_init);
module_exit(crc32c_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CRC32c (Castagnoli), sparc64 crc32c opcode accelerated");

MODULE_ALIAS_CRYPTO("crc32c");

#include "crop_devid.c"
