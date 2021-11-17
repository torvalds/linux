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

#include <crypto/md5.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#include <asm/octeon/octeon.h>
#include <crypto/internal/hash.h>

#include "octeon-crypto.h"

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void octeon_md5_store_hash(struct md5_state *ctx)
{
	u64 *hash = (u64 *)ctx->hash;

	write_octeon_64bit_hash_dword(hash[0], 0);
	write_octeon_64bit_hash_dword(hash[1], 1);
}

static void octeon_md5_read_hash(struct md5_state *ctx)
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
	struct md5_state *mctx = shash_desc_ctx(desc);

	mctx->hash[0] = MD5_H0;
	mctx->hash[1] = MD5_H1;
	mctx->hash[2] = MD5_H2;
	mctx->hash[3] = MD5_H3;
	cpu_to_le32_array(mctx->hash, 4);
	mctx->byte_count = 0;

	return 0;
}

static int octeon_md5_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	struct md5_state *mctx = shash_desc_ctx(desc);
	const u32 avail = sizeof(mctx->block) - (mctx->byte_count & 0x3f);
	struct octeon_cop2_state state;
	unsigned long flags;

	mctx->byte_count += len;

	if (avail > len) {
		memcpy((char *)mctx->block + (sizeof(mctx->block) - avail),
		       data, len);
		return 0;
	}

	memcpy((char *)mctx->block + (sizeof(mctx->block) - avail), data,
	       avail);

	flags = octeon_crypto_enable(&state);
	octeon_md5_store_hash(mctx);

	octeon_md5_transform(mctx->block);
	data += avail;
	len -= avail;

	while (len >= sizeof(mctx->block)) {
		octeon_md5_transform(data);
		data += sizeof(mctx->block);
		len -= sizeof(mctx->block);
	}

	octeon_md5_read_hash(mctx);
	octeon_crypto_disable(&state, flags);

	memcpy(mctx->block, data, len);

	return 0;
}

static int octeon_md5_final(struct shash_desc *desc, u8 *out)
{
	struct md5_state *mctx = shash_desc_ctx(desc);
	const unsigned int offset = mctx->byte_count & 0x3f;
	char *p = (char *)mctx->block + offset;
	int padding = 56 - (offset + 1);
	struct octeon_cop2_state state;
	unsigned long flags;

	*p++ = 0x80;

	flags = octeon_crypto_enable(&state);
	octeon_md5_store_hash(mctx);

	if (padding < 0) {
		memset(p, 0x00, padding + sizeof(u64));
		octeon_md5_transform(mctx->block);
		p = (char *)mctx->block;
		padding = 56;
	}

	memset(p, 0, padding);
	mctx->block[14] = mctx->byte_count << 3;
	mctx->block[15] = mctx->byte_count >> 29;
	cpu_to_le32_array(mctx->block + 14, 2);
	octeon_md5_transform(mctx->block);

	octeon_md5_read_hash(mctx);
	octeon_crypto_disable(&state, flags);

	memcpy(out, mctx->hash, sizeof(mctx->hash));
	memset(mctx, 0, sizeof(*mctx));

	return 0;
}

static int octeon_md5_export(struct shash_desc *desc, void *out)
{
	struct md5_state *ctx = shash_desc_ctx(desc);

	memcpy(out, ctx, sizeof(*ctx));
	return 0;
}

static int octeon_md5_import(struct shash_desc *desc, const void *in)
{
	struct md5_state *ctx = shash_desc_ctx(desc);

	memcpy(ctx, in, sizeof(*ctx));
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	MD5_DIGEST_SIZE,
	.init		=	octeon_md5_init,
	.update		=	octeon_md5_update,
	.final		=	octeon_md5_final,
	.export		=	octeon_md5_export,
	.import		=	octeon_md5_import,
	.descsize	=	sizeof(struct md5_state),
	.statesize	=	sizeof(struct md5_state),
	.base		=	{
		.cra_name	=	"md5",
		.cra_driver_name=	"octeon-md5",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
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
