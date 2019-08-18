/*
 * Cryptographic API.
 *
 * T10 Data Integrity Field CRC16 Crypto Transform using PCLMULQDQ Instructions
 *
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

#include <linux/types.h>
#include <linux/module.h>
#include <linux/crc-t10dif.h>
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/fpu/api.h>
#include <asm/cpufeatures.h>
#include <asm/cpu_device_id.h>

asmlinkage u16 crc_t10dif_pcl(u16 init_crc, const u8 *buf, size_t len);

struct chksum_desc_ctx {
	__u16 crc;
};

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

	if (length >= 16 && irq_fpu_usable()) {
		kernel_fpu_begin();
		ctx->crc = crc_t10dif_pcl(ctx->crc, data, length);
		kernel_fpu_end();
	} else
		ctx->crc = crc_t10dif_generic(ctx->crc, data, length);
	return 0;
}

static int chksum_final(struct shash_desc *desc, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	*(__u16 *)out = ctx->crc;
	return 0;
}

static int __chksum_finup(__u16 *crcp, const u8 *data, unsigned int len,
			u8 *out)
{
	if (len >= 16 && irq_fpu_usable()) {
		kernel_fpu_begin();
		*(__u16 *)out = crc_t10dif_pcl(*crcp, data, len);
		kernel_fpu_end();
	} else
		*(__u16 *)out = crc_t10dif_generic(*crcp, data, len);
	return 0;
}

static int chksum_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksum_finup(&ctx->crc, data, len, out);
}

static int chksum_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksum_finup(&ctx->crc, data, length, out);
}

static struct shash_alg alg = {
	.digestsize		=	CRC_T10DIF_DIGEST_SIZE,
	.init		=	chksum_init,
	.update		=	chksum_update,
	.final		=	chksum_final,
	.finup		=	chksum_finup,
	.digest		=	chksum_digest,
	.descsize		=	sizeof(struct chksum_desc_ctx),
	.base			=	{
		.cra_name		=	"crct10dif",
		.cra_driver_name	=	"crct10dif-pclmul",
		.cra_priority		=	200,
		.cra_blocksize		=	CRC_T10DIF_BLOCK_SIZE,
		.cra_module		=	THIS_MODULE,
	}
};

static const struct x86_cpu_id crct10dif_cpu_id[] = {
	X86_FEATURE_MATCH(X86_FEATURE_PCLMULQDQ),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, crct10dif_cpu_id);

static int __init crct10dif_intel_mod_init(void)
{
	if (!x86_match_cpu(crct10dif_cpu_id))
		return -ENODEV;

	return crypto_register_shash(&alg);
}

static void __exit crct10dif_intel_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(crct10dif_intel_mod_init);
module_exit(crct10dif_intel_mod_fini);

MODULE_AUTHOR("Tim Chen <tim.c.chen@linux.intel.com>");
MODULE_DESCRIPTION("T10 DIF CRC calculation accelerated with PCLMULQDQ.");
MODULE_LICENSE("GPL");

MODULE_ALIAS_CRYPTO("crct10dif");
MODULE_ALIAS_CRYPTO("crct10dif-pclmul");
