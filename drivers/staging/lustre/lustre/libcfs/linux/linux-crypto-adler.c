/* GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see http://www.gnu.org/licenses
 *
 * Please  visit http://www.xyratex.com/contact if you need additional
 * information or have any questions.
 *
 * GPL HEADER END
 */

/*
 * Copyright 2012 Xyratex Technology Limited
 */

/*
 * This is crypto api shash wrappers to zlib_adler32.
 */

#include <linux/module.h>
#include <linux/zutil.h>
#include <crypto/internal/hash.h>


#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4


static u32 __adler32(u32 cksum, unsigned char const *p, size_t len)
{
	return zlib_adler32(cksum, p, len);
}

static int adler32_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = 1;

	return 0;
}

static int adler32_setkey(struct crypto_shash *hash, const u8 *key,
			  unsigned int keylen)
{
	u32 *mctx = crypto_shash_ctx(hash);

	if (keylen != sizeof(u32)) {
		crypto_shash_set_flags(hash, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	*mctx = *(u32 *)key;
	return 0;
}

static int adler32_init(struct shash_desc *desc)
{
	u32 *mctx = crypto_shash_ctx(desc->tfm);
	u32 *cksump = shash_desc_ctx(desc);

	*cksump = *mctx;

	return 0;
}

static int adler32_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	u32 *cksump = shash_desc_ctx(desc);

	*cksump = __adler32(*cksump, data, len);
	return 0;
}
static int __adler32_finup(u32 *cksump, const u8 *data, unsigned int len,
			   u8 *out)
{
	*(u32 *)out = __adler32(*cksump, data, len);
	return 0;
}

static int adler32_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	return __adler32_finup(shash_desc_ctx(desc), data, len, out);
}

static int adler32_final(struct shash_desc *desc, u8 *out)
{
	u32 *cksump = shash_desc_ctx(desc);

	*(u32 *)out = *cksump;
	return 0;
}

static int adler32_digest(struct shash_desc *desc, const u8 *data,
			  unsigned int len, u8 *out)
{
	return __adler32_finup(crypto_shash_ctx(desc->tfm), data, len,
				    out);
}
static struct shash_alg alg = {
	.setkey		= adler32_setkey,
	.init		= adler32_init,
	.update		= adler32_update,
	.final		= adler32_final,
	.finup		= adler32_finup,
	.digest		= adler32_digest,
	.descsize	= sizeof(u32),
	.digestsize	= CHKSUM_DIGEST_SIZE,
	.base		= {
		.cra_name		= "adler32",
		.cra_driver_name	= "adler32-zlib",
		.cra_priority		= 100,
		.cra_blocksize		= CHKSUM_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(u32),
		.cra_module		= THIS_MODULE,
		.cra_init		= adler32_cra_init,
	}
};


int cfs_crypto_adler32_register(void)
{
	return crypto_register_shash(&alg);
}
EXPORT_SYMBOL(cfs_crypto_adler32_register);

void cfs_crypto_adler32_unregister(void)
{
	crypto_unregister_shash(&alg);
}
EXPORT_SYMBOL(cfs_crypto_adler32_unregister);
