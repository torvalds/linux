/*
 * Cryptographic API.
 *
 * T10 Data Integrity Field CRC16 Crypto Transform
 *
 * Copyright (c) 2007 Oracle Corporation.  All rights reserved.
 * Written by Martin K. Petersen <martin.petersen@oracle.com>
 * Copyright (C) 2013 Intel Corporation
 * Author: Tim Chen <tim.c.chen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/crc-t10dif.h>
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/kernel.h>

struct chksum_desc_ctx {
	__u16 crc;
};

/*
 * Steps through buffer one byte at a time, calculates reflected
 * crc using table.
 */

static int chksum_init(struct shash_desc *desc)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = 0;

	return 0;
}

static int chksum_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc_t10dif_generic(ctx->crc, data, length);
	return 0;
}

static int chksum_update_arch(struct shash_desc *desc, const u8 *data,
			      unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc_t10dif_update(ctx->crc, data, length);
	return 0;
}

static int chksum_final(struct shash_desc *desc, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	*(__u16 *)out = ctx->crc;
	return 0;
}

static int __chksum_finup(__u16 crc, const u8 *data, unsigned int len, u8 *out)
{
	*(__u16 *)out = crc_t10dif_generic(crc, data, len);
	return 0;
}

static int __chksum_finup_arch(__u16 crc, const u8 *data, unsigned int len,
			       u8 *out)
{
	*(__u16 *)out = crc_t10dif_update(crc, data, len);
	return 0;
}

static int chksum_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksum_finup(ctx->crc, data, len, out);
}

static int chksum_finup_arch(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksum_finup_arch(ctx->crc, data, len, out);
}

static int chksum_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	return __chksum_finup(0, data, length, out);
}

static int chksum_digest_arch(struct shash_desc *desc, const u8 *data,
			      unsigned int length, u8 *out)
{
	return __chksum_finup_arch(0, data, length, out);
}

static struct shash_alg algs[] = {{
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= chksum_init,
	.update			= chksum_update,
	.final			= chksum_final,
	.finup			= chksum_finup,
	.digest			= chksum_digest,
	.descsize		= sizeof(struct chksum_desc_ctx),
	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-generic",
	.base.cra_priority	= 100,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= chksum_init,
	.update			= chksum_update_arch,
	.final			= chksum_final,
	.finup			= chksum_finup_arch,
	.digest			= chksum_digest_arch,
	.descsize		= sizeof(struct chksum_desc_ctx),
	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-" __stringify(ARCH),
	.base.cra_priority	= 150,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}};

static int num_algs;

static int __init crct10dif_mod_init(void)
{
	/* register the arch flavor only if it differs from the generic one */
	num_algs = 1 + crc_t10dif_is_optimized();

	return crypto_register_shashes(algs, num_algs);
}

static void __exit crct10dif_mod_fini(void)
{
	crypto_unregister_shashes(algs, num_algs);
}

subsys_initcall(crct10dif_mod_init);
module_exit(crct10dif_mod_fini);

MODULE_AUTHOR("Tim Chen <tim.c.chen@linux.intel.com>");
MODULE_DESCRIPTION("T10 DIF CRC calculation.");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("crct10dif");
MODULE_ALIAS_CRYPTO("crct10dif-generic");
