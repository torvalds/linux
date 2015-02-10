/*
 * crc32-arm64.c - CRC32 and CRC32C using optional ARMv8 instructions
 *
 * Module based on crypto/crc32c_generic.c
 *
 * CRC32 loop taken from Ed Nevill's Hadoop CRC patch
 * http://mail-archives.apache.org/mod_mbox/hadoop-common-dev/201406.mbox/%3C1403687030.3355.19.camel%40localhost.localdomain%3E
 *
 * Using inline assembly instead of intrinsics in order to be backwards
 * compatible with older compilers.
 *
 * Copyright (C) 2014 Linaro Ltd <yazen.ghannam@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/unaligned/access_ok.h>
#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include <crypto/internal/hash.h>

MODULE_AUTHOR("Yazen Ghannam <yazen.ghannam@linaro.org>");
MODULE_DESCRIPTION("CRC32 and CRC32C using optional ARMv8 instructions");
MODULE_LICENSE("GPL v2");

#define CRC32X(crc, value) __asm__("crc32x %w[c], %w[c], %x[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32W(crc, value) __asm__("crc32w %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32H(crc, value) __asm__("crc32h %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32B(crc, value) __asm__("crc32b %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CX(crc, value) __asm__("crc32cx %w[c], %w[c], %x[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CW(crc, value) __asm__("crc32cw %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CH(crc, value) __asm__("crc32ch %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CB(crc, value) __asm__("crc32cb %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))

static u32 crc32_arm64_le_hw(u32 crc, const u8 *p, unsigned int len)
{
	s64 length = len;

	while ((length -= sizeof(u64)) >= 0) {
		CRC32X(crc, get_unaligned_le64(p));
		p += sizeof(u64);
	}

	/* The following is more efficient than the straight loop */
	if (length & sizeof(u32)) {
		CRC32W(crc, get_unaligned_le32(p));
		p += sizeof(u32);
	}
	if (length & sizeof(u16)) {
		CRC32H(crc, get_unaligned_le16(p));
		p += sizeof(u16);
	}
	if (length & sizeof(u8))
		CRC32B(crc, *p);

	return crc;
}

static u32 crc32c_arm64_le_hw(u32 crc, const u8 *p, unsigned int len)
{
	s64 length = len;

	while ((length -= sizeof(u64)) >= 0) {
		CRC32CX(crc, get_unaligned_le64(p));
		p += sizeof(u64);
	}

	/* The following is more efficient than the straight loop */
	if (length & sizeof(u32)) {
		CRC32CW(crc, get_unaligned_le32(p));
		p += sizeof(u32);
	}
	if (length & sizeof(u16)) {
		CRC32CH(crc, get_unaligned_le16(p));
		p += sizeof(u16);
	}
	if (length & sizeof(u8))
		CRC32CB(crc, *p);

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

	if (keylen != sizeof(mctx->key)) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	mctx->key = get_unaligned_le32(key);
	return 0;
}

static int chksum_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc32_arm64_le_hw(ctx->crc, data, length);
	return 0;
}

static int chksumc_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc32c_arm64_le_hw(ctx->crc, data, length);
	return 0;
}

static int chksum_final(struct shash_desc *desc, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	put_unaligned_le32(~ctx->crc, out);
	return 0;
}

static int __chksum_finup(u32 crc, const u8 *data, unsigned int len, u8 *out)
{
	put_unaligned_le32(~crc32_arm64_le_hw(crc, data, len), out);
	return 0;
}

static int __chksumc_finup(u32 crc, const u8 *data, unsigned int len, u8 *out)
{
	put_unaligned_le32(~crc32c_arm64_le_hw(crc, data, len), out);
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

static int crc32_cra_init(struct crypto_tfm *tfm)
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
		.cra_driver_name	=	"crc32-arm64-hw",
		.cra_priority		=	300,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_alignmask		=	0,
		.cra_ctxsize		=	sizeof(struct chksum_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	crc32_cra_init,
	}
};

static struct shash_alg crc32c_alg = {
	.digestsize		=	CHKSUM_DIGEST_SIZE,
	.setkey			=	chksum_setkey,
	.init			=	chksum_init,
	.update			=	chksumc_update,
	.final			=	chksum_final,
	.finup			=	chksumc_finup,
	.digest			=	chksumc_digest,
	.descsize		=	sizeof(struct chksum_desc_ctx),
	.base			=	{
		.cra_name		=	"crc32c",
		.cra_driver_name	=	"crc32c-arm64-hw",
		.cra_priority		=	300,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_alignmask		=	0,
		.cra_ctxsize		=	sizeof(struct chksum_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	crc32_cra_init,
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

module_cpu_feature_match(CRC32, crc32_mod_init);
module_exit(crc32_mod_exit);
