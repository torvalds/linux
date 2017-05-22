/*
 * Calculate a CRC T10-DIF with vpmsum acceleration
 *
 * Copyright 2017, Daniel Axtens, IBM Corporation.
 * [based on crc32c-vpmsum_glue.c]
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/crc-t10dif.h>
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/switch_to.h>

#define VMX_ALIGN		16
#define VMX_ALIGN_MASK		(VMX_ALIGN-1)

#define VECTOR_BREAKPOINT	64

u32 __crct10dif_vpmsum(u32 crc, unsigned char const *p, size_t len);

static u16 crct10dif_vpmsum(u16 crci, unsigned char const *p, size_t len)
{
	unsigned int prealign;
	unsigned int tail;
	u32 crc = crci;

	if (len < (VECTOR_BREAKPOINT + VMX_ALIGN) || in_interrupt())
		return crc_t10dif_generic(crc, p, len);

	if ((unsigned long)p & VMX_ALIGN_MASK) {
		prealign = VMX_ALIGN - ((unsigned long)p & VMX_ALIGN_MASK);
		crc = crc_t10dif_generic(crc, p, prealign);
		len -= prealign;
		p += prealign;
	}

	if (len & ~VMX_ALIGN_MASK) {
		crc <<= 16;
		preempt_disable();
		pagefault_disable();
		enable_kernel_altivec();
		crc = __crct10dif_vpmsum(crc, p, len & ~VMX_ALIGN_MASK);
		disable_kernel_altivec();
		pagefault_enable();
		preempt_enable();
		crc >>= 16;
	}

	tail = len & VMX_ALIGN_MASK;
	if (tail) {
		p += len & ~VMX_ALIGN_MASK;
		crc = crc_t10dif_generic(crc, p, tail);
	}

	return crc & 0xffff;
}

static int crct10dif_vpmsum_init(struct shash_desc *desc)
{
	u16 *crc = shash_desc_ctx(desc);

	*crc = 0;
	return 0;
}

static int crct10dif_vpmsum_update(struct shash_desc *desc, const u8 *data,
			    unsigned int length)
{
	u16 *crc = shash_desc_ctx(desc);

	*crc = crct10dif_vpmsum(*crc, data, length);

	return 0;
}


static int crct10dif_vpmsum_final(struct shash_desc *desc, u8 *out)
{
	u16 *crcp = shash_desc_ctx(desc);

	*(u16 *)out = *crcp;
	return 0;
}

static struct shash_alg alg = {
	.init		= crct10dif_vpmsum_init,
	.update		= crct10dif_vpmsum_update,
	.final		= crct10dif_vpmsum_final,
	.descsize	= CRC_T10DIF_DIGEST_SIZE,
	.digestsize	= CRC_T10DIF_DIGEST_SIZE,
	.base		= {
		.cra_name		= "crct10dif",
		.cra_driver_name	= "crct10dif-vpmsum",
		.cra_priority		= 200,
		.cra_blocksize		= CRC_T10DIF_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static int __init crct10dif_vpmsum_mod_init(void)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	return crypto_register_shash(&alg);
}

static void __exit crct10dif_vpmsum_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_cpu_feature_match(PPC_MODULE_FEATURE_VEC_CRYPTO, crct10dif_vpmsum_mod_init);
module_exit(crct10dif_vpmsum_mod_fini);

MODULE_AUTHOR("Daniel Axtens <dja@axtens.net>");
MODULE_DESCRIPTION("CRCT10DIF using vector polynomial multiply-sum instructions");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("crct10dif");
MODULE_ALIAS_CRYPTO("crct10dif-vpmsum");
