// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Calculate a CRC T10-DIF with vpmsum acceleration
 *
 * Copyright 2017, Daniel Axtens, IBM Corporation.
 * [based on crc32c-vpmsum_glue.c]
 */

#include <linux/crc-t10dif.h>
#include <crypto/internal/simd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/simd.h>
#include <asm/switch_to.h>

#define VMX_ALIGN		16
#define VMX_ALIGN_MASK		(VMX_ALIGN-1)

#define VECTOR_BREAKPOINT	64

static DEFINE_STATIC_KEY_FALSE(have_vec_crypto);

u32 __crct10dif_vpmsum(u32 crc, unsigned char const *p, size_t len);

u16 crc_t10dif_arch(u16 crci, const u8 *p, size_t len)
{
	unsigned int prealign;
	unsigned int tail;
	u32 crc = crci;

	if (len < (VECTOR_BREAKPOINT + VMX_ALIGN) ||
	    !static_branch_likely(&have_vec_crypto) || !crypto_simd_usable())
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
EXPORT_SYMBOL(crc_t10dif_arch);

static int __init crc_t10dif_powerpc_init(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		static_branch_enable(&have_vec_crypto);
	return 0;
}
arch_initcall(crc_t10dif_powerpc_init);

static void __exit crc_t10dif_powerpc_exit(void)
{
}
module_exit(crc_t10dif_powerpc_exit);

MODULE_AUTHOR("Daniel Axtens <dja@axtens.net>");
MODULE_DESCRIPTION("CRCT10DIF using vector polynomial multiply-sum instructions");
MODULE_LICENSE("GPL");
