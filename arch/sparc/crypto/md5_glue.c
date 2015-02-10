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

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/md5.h>

#include <asm/pstate.h>
#include <asm/elf.h>

#include "opcodes.h"

asmlinkage void md5_sparc64_transform(u32 *digest, const char *data,
				      unsigned int rounds);

static int md5_sparc64_init(struct shash_desc *desc)
{
	struct md5_state *mctx = shash_desc_ctx(desc);

	mctx->hash[0] = cpu_to_le32(0x67452301);
	mctx->hash[1] = cpu_to_le32(0xefcdab89);
	mctx->hash[2] = cpu_to_le32(0x98badcfe);
	mctx->hash[3] = cpu_to_le32(0x10325476);
	mctx->byte_count = 0;

	return 0;
}

static void __md5_sparc64_update(struct md5_state *sctx, const u8 *data,
				 unsigned int len, unsigned int partial)
{
	unsigned int done = 0;

	sctx->byte_count += len;
	if (partial) {
		done = MD5_HMAC_BLOCK_SIZE - partial;
		memcpy((u8 *)sctx->block + partial, data, done);
		md5_sparc64_transform(sctx->hash, (u8 *)sctx->block, 1);
	}
	if (len - done >= MD5_HMAC_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / MD5_HMAC_BLOCK_SIZE;

		md5_sparc64_transform(sctx->hash, data + done, rounds);
		done += rounds * MD5_HMAC_BLOCK_SIZE;
	}

	memcpy(sctx->block, data + done, len - done);
}

static int md5_sparc64_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	struct md5_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->byte_count % MD5_HMAC_BLOCK_SIZE;

	/* Handle the fast case right here */
	if (partial + len < MD5_HMAC_BLOCK_SIZE) {
		sctx->byte_count += len;
		memcpy((u8 *)sctx->block + partial, data, len);
	} else
		__md5_sparc64_update(sctx, data, len, partial);

	return 0;
}

/* Add padding and return the message digest. */
static int md5_sparc64_final(struct shash_desc *desc, u8 *out)
{
	struct md5_state *sctx = shash_desc_ctx(desc);
	unsigned int i, index, padlen;
	u32 *dst = (u32 *)out;
	__le64 bits;
	static const u8 padding[MD5_HMAC_BLOCK_SIZE] = { 0x80, };

	bits = cpu_to_le64(sctx->byte_count << 3);

	/* Pad out to 56 mod 64 and append length */
	index = sctx->byte_count % MD5_HMAC_BLOCK_SIZE;
	padlen = (index < 56) ? (56 - index) : ((MD5_HMAC_BLOCK_SIZE+56) - index);

	/* We need to fill a whole block for __md5_sparc64_update() */
	if (padlen <= 56) {
		sctx->byte_count += padlen;
		memcpy((u8 *)sctx->block + index, padding, padlen);
	} else {
		__md5_sparc64_update(sctx, padding, padlen, index);
	}
	__md5_sparc64_update(sctx, (const u8 *)&bits, sizeof(bits), 56);

	/* Store state in digest */
	for (i = 0; i < MD5_HASH_WORDS; i++)
		dst[i] = sctx->hash[i];

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int md5_sparc64_export(struct shash_desc *desc, void *out)
{
	struct md5_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

static int md5_sparc64_import(struct shash_desc *desc, const void *in)
{
	struct md5_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	MD5_DIGEST_SIZE,
	.init		=	md5_sparc64_init,
	.update		=	md5_sparc64_update,
	.final		=	md5_sparc64_final,
	.export		=	md5_sparc64_export,
	.import		=	md5_sparc64_import,
	.descsize	=	sizeof(struct md5_state),
	.statesize	=	sizeof(struct md5_state),
	.base		=	{
		.cra_name	=	"md5",
		.cra_driver_name=	"md5-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
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
MODULE_DESCRIPTION("MD5 Secure Hash Algorithm, sparc64 md5 opcode accelerated");

MODULE_ALIAS_CRYPTO("md5");

#include "crop_devid.c"
