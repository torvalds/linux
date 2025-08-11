/*
 * Cryptographic API.
 *
 * MD5 Message Digest Algorithm (RFC1321).
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/md5.c, which is:
 *
 * Derived from cryptoapi implementation, originally based on the
 * public domain implementation written by Colin Plumb in 1993.
 *
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <asm/octeon/crypto.h>
#include <asm/octeon/octeon.h>
#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

struct octeon_md5_state {
	__le32 hash[MD5_HASH_WORDS];
	u64 byte_count;
};

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void octeon_md5_store_hash(struct octeon_md5_state *ctx)
{
	u64 *hash = (u64 *)ctx->hash;

	write_octeon_64bit_hash_dword(hash[0], 0);
	write_octeon_64bit_hash_dword(hash[1], 1);
}

static void octeon_md5_read_hash(struct octeon_md5_state *ctx)
{
	u64 *hash = (u64 *)ctx->hash;

	hash[0] = read_octeon_64bit_hash_dword(0);
	hash[1] = read_octeon_64bit_hash_dword(1);
}

static void octeon_md5_transform(const void *_block)
{
	const u64 *block = _block;

	write_octeon_64bit_block_dword(block[0], 0);
	write_octeon_64bit_block_dword(block[1], 1);
	write_octeon_64bit_block_dword(block[2], 2);
	write_octeon_64bit_block_dword(block[3], 3);
	write_octeon_64bit_block_dword(block[4], 4);
	write_octeon_64bit_block_dword(block[5], 5);
	write_octeon_64bit_block_dword(block[6], 6);
	octeon_md5_start(block[7]);
}

static int octeon_md5_init(struct shash_desc *desc)
{
	struct octeon_md5_state *mctx = shash_desc_ctx(desc);

	mctx->hash[0] = cpu_to_le32(MD5_H0);
	mctx->hash[1] = cpu_to_le32(MD5_H1);
	mctx->hash[2] = cpu_to_le32(MD5_H2);
	mctx->hash[3] = cpu_to_le32(MD5_H3);
	mctx->byte_count = 0;

	return 0;
}

static int octeon_md5_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	struct octeon_md5_state *mctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;

	mctx->byte_count += len;
	flags = octeon_crypto_enable(&state);
	octeon_md5_store_hash(mctx);

	do {
		octeon_md5_transform(data);
		data += MD5_HMAC_BLOCK_SIZE;
		len -= MD5_HMAC_BLOCK_SIZE;
	} while (len >= MD5_HMAC_BLOCK_SIZE);

	octeon_md5_read_hash(mctx);
	octeon_crypto_disable(&state, flags);
	mctx->byte_count -= len;
	return len;
}

static int octeon_md5_finup(struct shash_desc *desc, const u8 *src,
			    unsigned int offset, u8 *out)
{
	struct octeon_md5_state *mctx = shash_desc_ctx(desc);
	int padding = 56 - (offset + 1);
	struct octeon_cop2_state state;
	u32 block[MD5_BLOCK_WORDS];
	unsigned long flags;
	char *p;

	p = memcpy(block, src, offset);
	p += offset;
	*p++ = 0x80;

	flags = octeon_crypto_enable(&state);
	octeon_md5_store_hash(mctx);

	if (padding < 0) {
		memset(p, 0x00, padding + sizeof(u64));
		octeon_md5_transform(block);
		p = (char *)block;
		padding = 56;
	}

	memset(p, 0, padding);
	mctx->byte_count += offset;
	block[14] = mctx->byte_count << 3;
	block[15] = mctx->byte_count >> 29;
	cpu_to_le32_array(block + 14, 2);
	octeon_md5_transform(block);

	octeon_md5_read_hash(mctx);
	octeon_crypto_disable(&state, flags);

	memzero_explicit(block, sizeof(block));
	memcpy(out, mctx->hash, sizeof(mctx->hash));

	return 0;
}

static int octeon_md5_export(struct shash_desc *desc, void *out)
{
	struct octeon_md5_state *ctx = shash_desc_ctx(desc);
	union {
		u8 *u8;
		u32 *u32;
		u64 *u64;
	} p = { .u8 = out };
	int i;

	for (i = 0; i < MD5_HASH_WORDS; i++)
		put_unaligned(le32_to_cpu(ctx->hash[i]), p.u32++);
	put_unaligned(ctx->byte_count, p.u64);
	return 0;
}

static int octeon_md5_import(struct shash_desc *desc, const void *in)
{
	struct octeon_md5_state *ctx = shash_desc_ctx(desc);
	union {
		const u8 *u8;
		const u32 *u32;
		const u64 *u64;
	} p = { .u8 = in };
	int i;

	for (i = 0; i < MD5_HASH_WORDS; i++)
		ctx->hash[i] = cpu_to_le32(get_unaligned(p.u32++));
	ctx->byte_count = get_unaligned(p.u64);
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	MD5_DIGEST_SIZE,
	.init		=	octeon_md5_init,
	.update		=	octeon_md5_update,
	.finup		=	octeon_md5_finup,
	.export		=	octeon_md5_export,
	.import		=	octeon_md5_import,
	.statesize	=	MD5_STATE_SIZE,
	.descsize	=	sizeof(struct octeon_md5_state),
	.base		=	{
		.cra_name	=	"md5",
		.cra_driver_name=	"octeon-md5",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	MD5_HMAC_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init md5_mod_init(void)
{
	if (!octeon_has_crypto())
		return -ENOTSUPP;
	return crypto_register_shash(&alg);
}

static void __exit md5_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(md5_mod_init);
module_exit(md5_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MD5 Message Digest Algorithm (OCTEON)");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
