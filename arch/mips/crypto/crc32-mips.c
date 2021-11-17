// SPDX-License-Identifier: GPL-2.0
/*
 * crc32-mips.c - CRC32 and CRC32C using optional MIPSr6 instructions
 *
 * Module based on arm64/crypto/crc32-arm.c
 *
 * Copyright (C) 2014 Linaro Ltd <yazen.ghannam@linaro.org>
 * Copyright (C) 2018 MIPS Tech, LLC
 */

#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <asm/mipsregs.h>
#include <asm/unaligned.h>

#include <crypto/internal/hash.h>

enum crc_op_size {
	b, h, w, d,
};

enum crc_type {
	crc32,
	crc32c,
};

#ifndef TOOLCHAIN_SUPPORTS_CRC
#define _ASM_MACRO_CRC32(OP, SZ, TYPE)					  \
_ASM_MACRO_3R(OP, rt, rs, rt2,						  \
	".ifnc	\\rt, \\rt2\n\t"					  \
	".error	\"invalid operands \\\"" #OP " \\rt,\\rs,\\rt2\\\"\"\n\t" \
	".endif\n\t"							  \
	_ASM_INSN_IF_MIPS(0x7c00000f | (__rt << 16) | (__rs << 21) |	  \
			  ((SZ) <<  6) | ((TYPE) << 8))			  \
	_ASM_INSN32_IF_MM(0x00000030 | (__rs << 16) | (__rt << 21) |	  \
			  ((SZ) << 14) | ((TYPE) << 3)))
_ASM_MACRO_CRC32(crc32b,  0, 0);
_ASM_MACRO_CRC32(crc32h,  1, 0);
_ASM_MACRO_CRC32(crc32w,  2, 0);
_ASM_MACRO_CRC32(crc32d,  3, 0);
_ASM_MACRO_CRC32(crc32cb, 0, 1);
_ASM_MACRO_CRC32(crc32ch, 1, 1);
_ASM_MACRO_CRC32(crc32cw, 2, 1);
_ASM_MACRO_CRC32(crc32cd, 3, 1);
#define _ASM_SET_CRC ""
#else /* !TOOLCHAIN_SUPPORTS_CRC */
#define _ASM_SET_CRC ".set\tcrc\n\t"
#endif

#define _CRC32(crc, value, size, type)		\
do {						\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		_ASM_SET_CRC			\
		#type #size "	%0, %1, %0\n\t"	\
		".set	pop"			\
		: "+r" (crc)			\
		: "r" (value));			\
} while (0)

#define CRC32(crc, value, size) \
	_CRC32(crc, value, size, crc32)

#define CRC32C(crc, value, size) \
	_CRC32(crc, value, size, crc32c)

static u32 crc32_mips_le_hw(u32 crc_, const u8 *p, unsigned int len)
{
	u32 crc = crc_;

#ifdef CONFIG_64BIT
	while (len >= sizeof(u64)) {
		u64 value = get_unaligned_le64(p);

		CRC32(crc, value, d);
		p += sizeof(u64);
		len -= sizeof(u64);
	}

	if (len & sizeof(u32)) {
#else /* !CONFIG_64BIT */
	while (len >= sizeof(u32)) {
#endif
		u32 value = get_unaligned_le32(p);

		CRC32(crc, value, w);
		p += sizeof(u32);
		len -= sizeof(u32);
	}

	if (len & sizeof(u16)) {
		u16 value = get_unaligned_le16(p);

		CRC32(crc, value, h);
		p += sizeof(u16);
	}

	if (len & sizeof(u8)) {
		u8 value = *p++;

		CRC32(crc, value, b);
	}

	return crc;
}

static u32 crc32c_mips_le_hw(u32 crc_, const u8 *p, unsigned int len)
{
	u32 crc = crc_;

#ifdef CONFIG_64BIT
	while (len >= sizeof(u64)) {
		u64 value = get_unaligned_le64(p);

		CRC32C(crc, value, d);
		p += sizeof(u64);
		len -= sizeof(u64);
	}

	if (len & sizeof(u32)) {
#else /* !CONFIG_64BIT */
	while (len >= sizeof(u32)) {
#endif
		u32 value = get_unaligned_le32(p);

		CRC32C(crc, value, w);
		p += sizeof(u32);
		len -= sizeof(u32);
	}

	if (len & sizeof(u16)) {
		u16 value = get_unaligned_le16(p);

		CRC32C(crc, value, h);
		p += sizeof(u16);
	}

	if (len & sizeof(u8)) {
		u8 value = *p++;

		CRC32C(crc, value, b);
	}
	return crc;
}

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

struct chksum_ctx {
	u32 key;
};

struct chksum_desc_ctx {
	u32 crc;
};

static int chksum_init(struct shash_desc *desc)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(desc->tfm);
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = mctx->key;

	return 0;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int chksum_setkey(struct crypto_shash *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(tfm);

	if (keylen != sizeof(mctx->key))
		return -EINVAL;
	mctx->key = get_unaligned_le32(key);
	return 0;
}

static int chksum_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc32_mips_le_hw(ctx->crc, data, length);
	return 0;
}

static int chksumc_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc32c_mips_le_hw(ctx->crc, data, length);
	return 0;
}

static int chksum_final(struct shash_desc *desc, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	put_unaligned_le32(ctx->crc, out);
	return 0;
}

static int chksumc_final(struct shash_desc *desc, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	put_unaligned_le32(~ctx->crc, out);
	return 0;
}

static int __chksum_finup(u32 crc, const u8 *data, unsigned int len, u8 *out)
{
	put_unaligned_le32(crc32_mips_le_hw(crc, data, len), out);
	return 0;
}

static int __chksumc_finup(u32 crc, const u8 *data, unsigned int len, u8 *out)
{
	put_unaligned_le32(~crc32c_mips_le_hw(crc, data, len), out);
	return 0;
}

static int chksum_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksum_finup(ctx->crc, data, len, out);
}

static int chksumc_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksumc_finup(ctx->crc, data, len, out);
}

static int chksum_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(desc->tfm);

	return __chksum_finup(mctx->key, data, length, out);
}

static int chksumc_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(desc->tfm);

	return __chksumc_finup(mctx->key, data, length, out);
}

static int chksum_cra_init(struct crypto_tfm *tfm)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->key = ~0;
	return 0;
}

static struct shash_alg crc32_alg = {
	.digestsize		=	CHKSUM_DIGEST_SIZE,
	.setkey			=	chksum_setkey,
	.init			=	chksum_init,
	.update			=	chksum_update,
	.final			=	chksum_final,
	.finup			=	chksum_finup,
	.digest			=	chksum_digest,
	.descsize		=	sizeof(struct chksum_desc_ctx),
	.base			=	{
		.cra_name		=	"crc32",
		.cra_driver_name	=	"crc32-mips-hw",
		.cra_priority		=	300,
		.cra_flags		=	CRYPTO_ALG_OPTIONAL_KEY,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_alignmask		=	0,
		.cra_ctxsize		=	sizeof(struct chksum_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	chksum_cra_init,
	}
};

static struct shash_alg crc32c_alg = {
	.digestsize		=	CHKSUM_DIGEST_SIZE,
	.setkey			=	chksum_setkey,
	.init			=	chksum_init,
	.update			=	chksumc_update,
	.final			=	chksumc_final,
	.finup			=	chksumc_finup,
	.digest			=	chksumc_digest,
	.descsize		=	sizeof(struct chksum_desc_ctx),
	.base			=	{
		.cra_name		=	"crc32c",
		.cra_driver_name	=	"crc32c-mips-hw",
		.cra_priority		=	300,
		.cra_flags		=	CRYPTO_ALG_OPTIONAL_KEY,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_alignmask		=	0,
		.cra_ctxsize		=	sizeof(struct chksum_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	chksum_cra_init,
	}
};

static int __init crc32_mod_init(void)
{
	int err;

	err = crypto_register_shash(&crc32_alg);

	if (err)
		return err;

	err = crypto_register_shash(&crc32c_alg);

	if (err) {
		crypto_unregister_shash(&crc32_alg);
		return err;
	}

	return 0;
}

static void __exit crc32_mod_exit(void)
{
	crypto_unregister_shash(&crc32_alg);
	crypto_unregister_shash(&crc32c_alg);
}

MODULE_AUTHOR("Marcin Nowakowski <marcin.nowakowski@mips.com");
MODULE_DESCRIPTION("CRC32 and CRC32C using optional MIPS instructions");
MODULE_LICENSE("GPL v2");

module_cpu_feature_match(MIPS_CRC32, crc32_mod_init);
module_exit(crc32_mod_exit);
