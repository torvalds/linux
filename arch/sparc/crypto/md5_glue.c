// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for MD5 hashing optimized for sparc64 crypto opcodes.
 *
 * This is based largely upon arch/x86/crypto/sha1_ssse3_glue.c
 * and crypto/md5.c which are:
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) Mathias Krause <minipli@googlemail.com>
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>
#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

struct sparc_md5_state {
	__le32 hash[MD5_HASH_WORDS];
	u64 byte_count;
};

asmlinkage void md5_sparc64_transform(__le32 *digest, const char *data,
				      unsigned int rounds);

static int md5_sparc64_init(struct shash_desc *desc)
{
	struct sparc_md5_state *mctx = shash_desc_ctx(desc);

	mctx->hash[0] = cpu_to_le32(MD5_H0);
	mctx->hash[1] = cpu_to_le32(MD5_H1);
	mctx->hash[2] = cpu_to_le32(MD5_H2);
	mctx->hash[3] = cpu_to_le32(MD5_H3);
	mctx->byte_count = 0;

	return 0;
}

static int md5_sparc64_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	struct sparc_md5_state *sctx = shash_desc_ctx(desc);

	sctx->byte_count += round_down(len, MD5_HMAC_BLOCK_SIZE);
	md5_sparc64_transform(sctx->hash, data, len / MD5_HMAC_BLOCK_SIZE);
	return len - round_down(len, MD5_HMAC_BLOCK_SIZE);
}

/* Add padding and return the message digest. */
static int md5_sparc64_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int offset, u8 *out)
{
	struct sparc_md5_state *sctx = shash_desc_ctx(desc);
	__le64 block[MD5_BLOCK_WORDS] = {};
	u8 *p = memcpy(block, src, offset);
	__le32 *dst = (__le32 *)out;
	__le64 *pbits;
	int i;

	src = p;
	p += offset;
	*p++ = 0x80;
	sctx->byte_count += offset;
	pbits = &block[(MD5_BLOCK_WORDS / (offset > 55 ? 1 : 2)) - 1];
	*pbits = cpu_to_le64(sctx->byte_count << 3);
	md5_sparc64_transform(sctx->hash, src, (pbits - block + 1) / 8);
	memzero_explicit(block, sizeof(block));

	/* Store state in digest */
	for (i = 0; i < MD5_HASH_WORDS; i++)
		dst[i] = sctx->hash[i];

	return 0;
}

static int md5_sparc64_export(struct shash_desc *desc, void *out)
{
	struct sparc_md5_state *sctx = shash_desc_ctx(desc);
	union {
		u8 *u8;
		u32 *u32;
		u64 *u64;
	} p = { .u8 = out };
	int i;

	for (i = 0; i < MD5_HASH_WORDS; i++)
		put_unaligned(le32_to_cpu(sctx->hash[i]), p.u32++);
	put_unaligned(sctx->byte_count, p.u64);
	return 0;
}

static int md5_sparc64_import(struct shash_desc *desc, const void *in)
{
	struct sparc_md5_state *sctx = shash_desc_ctx(desc);
	union {
		const u8 *u8;
		const u32 *u32;
		const u64 *u64;
	} p = { .u8 = in };
	int i;

	for (i = 0; i < MD5_HASH_WORDS; i++)
		sctx->hash[i] = cpu_to_le32(get_unaligned(p.u32++));
	sctx->byte_count = get_unaligned(p.u64);
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	MD5_DIGEST_SIZE,
	.init		=	md5_sparc64_init,
	.update		=	md5_sparc64_update,
	.finup		=	md5_sparc64_finup,
	.export		=	md5_sparc64_export,
	.import		=	md5_sparc64_import,
	.descsize	=	sizeof(struct sparc_md5_state),
	.statesize	=	sizeof(struct sparc_md5_state),
	.base		=	{
		.cra_name	=	"md5",
		.cra_driver_name=	"md5-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	MD5_HMAC_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static bool __init sparc64_has_md5_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_MD5))
		return false;

	return true;
}

static int __init md5_sparc64_mod_init(void)
{
	if (sparc64_has_md5_opcode()) {
		pr_info("Using sparc64 md5 opcode optimized MD5 implementation\n");
		return crypto_register_shash(&alg);
	}
	pr_info("sparc64 md5 opcode not available.\n");
	return -ENODEV;
}

static void __exit md5_sparc64_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(md5_sparc64_mod_init);
module_exit(md5_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MD5 Message Digest Algorithm, sparc64 md5 opcode accelerated");

MODULE_ALIAS_CRYPTO("md5");

#include "crop_devid.c"
