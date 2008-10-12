/*
 * Using hardware provided CRC32 instruction to accelerate the CRC32 disposal.
 * CRC32C polynomial:0x1EDC6F41(BE)/0x82F63B78(LE)
 * CRC32 is a new instruction in Intel SSE4.2, the reference can be found at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2A: Instruction Set Reference, A-M
 *
 * Copyright (c) 2008 Austin Zhang <austin_zhang@linux.intel.com>
 * Copyright (c) 2008 Kent Liu <kent.liu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <crypto/internal/hash.h>

#include <asm/cpufeature.h>

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

#define SCALE_F	sizeof(unsigned long)

#ifdef CONFIG_X86_64
#define REX_PRE "0x48, "
#else
#define REX_PRE
#endif

static u32 crc32c_intel_le_hw_byte(u32 crc, unsigned char const *data, size_t length)
{
	while (length--) {
		__asm__ __volatile__(
			".byte 0xf2, 0xf, 0x38, 0xf0, 0xf1"
			:"=S"(crc)
			:"0"(crc), "c"(*data)
		);
		data++;
	}

	return crc;
}

static u32 __pure crc32c_intel_le_hw(u32 crc, unsigned char const *p, size_t len)
{
	unsigned int iquotient = len / SCALE_F;
	unsigned int iremainder = len % SCALE_F;
	unsigned long *ptmp = (unsigned long *)p;

	while (iquotient--) {
		__asm__ __volatile__(
			".byte 0xf2, " REX_PRE "0xf, 0x38, 0xf1, 0xf1;"
			:"=S"(crc)
			:"0"(crc), "c"(*ptmp)
		);
		ptmp++;
	}

	if (iremainder)
		crc = crc32c_intel_le_hw_byte(crc, (unsigned char *)ptmp,
				 iremainder);

	return crc;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int crc32c_intel_setkey(struct crypto_ahash *hash, const u8 *key,
			unsigned int keylen)
{
	u32 *mctx = crypto_ahash_ctx(hash);

	if (keylen != sizeof(u32)) {
		crypto_ahash_set_flags(hash, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	*mctx = le32_to_cpup((__le32 *)key);
	return 0;
}

static int crc32c_intel_init(struct ahash_request *req)
{
	u32 *mctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	u32 *crcp = ahash_request_ctx(req);

	*crcp = *mctx;

	return 0;
}

static int crc32c_intel_update(struct ahash_request *req)
{
	struct crypto_hash_walk walk;
	u32 *crcp = ahash_request_ctx(req);
	u32 crc = *crcp;
	int nbytes;

	for (nbytes = crypto_hash_walk_first(req, &walk); nbytes;
	   nbytes = crypto_hash_walk_done(&walk, 0))
	crc = crc32c_intel_le_hw(crc, walk.data, nbytes);

	*crcp = crc;
	return 0;
}

static int crc32c_intel_final(struct ahash_request *req)
{
	u32 *crcp = ahash_request_ctx(req);

	*(__le32 *)req->result = ~cpu_to_le32p(crcp);
	return 0;
}

static int crc32c_intel_digest(struct ahash_request *req)
{
	struct crypto_hash_walk walk;
	u32 *mctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	u32 crc = *mctx;
	int nbytes;

	for (nbytes = crypto_hash_walk_first(req, &walk); nbytes;
	   nbytes = crypto_hash_walk_done(&walk, 0))
		crc = crc32c_intel_le_hw(crc, walk.data, nbytes);

	*(__le32 *)req->result = ~cpu_to_le32(crc);
	return 0;
}

static int crc32c_intel_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = ~0;

	tfm->crt_ahash.reqsize = sizeof(u32);

	return 0;
}

static struct crypto_alg alg = {
	.cra_name               =       "crc32c",
	.cra_driver_name        =       "crc32c-intel",
	.cra_priority           =       200,
	.cra_flags              =       CRYPTO_ALG_TYPE_AHASH,
	.cra_blocksize          =       CHKSUM_BLOCK_SIZE,
	.cra_alignmask          =       3,
	.cra_ctxsize            =       sizeof(u32),
	.cra_module             =       THIS_MODULE,
	.cra_list               =       LIST_HEAD_INIT(alg.cra_list),
	.cra_init               =       crc32c_intel_cra_init,
	.cra_type               =       &crypto_ahash_type,
	.cra_u                  =       {
		.ahash = {
			.digestsize    =       CHKSUM_DIGEST_SIZE,
			.setkey        =       crc32c_intel_setkey,
			.init          =       crc32c_intel_init,
			.update        =       crc32c_intel_update,
			.final         =       crc32c_intel_final,
			.digest        =       crc32c_intel_digest,
		}
	}
};


static int __init crc32c_intel_mod_init(void)
{
	if (cpu_has_xmm4_2)
		return crypto_register_alg(&alg);
	else
		return -ENODEV;
}

static void __exit crc32c_intel_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(crc32c_intel_mod_init);
module_exit(crc32c_intel_mod_fini);

MODULE_AUTHOR("Austin Zhang <austin.zhang@intel.com>, Kent Liu <kent.liu@intel.com>");
MODULE_DESCRIPTION("CRC32c (Castagnoli) optimization using Intel Hardware.");
MODULE_LICENSE("GPL");

MODULE_ALIAS("crc32c");
MODULE_ALIAS("crc32c-intel");

