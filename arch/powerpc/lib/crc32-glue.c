// SPDX-License-Identifier: GPL-2.0-only
#include <linux/crc32.h>
#include <crypto/internal/simd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/simd.h>
#include <asm/switch_to.h>

#define VMX_ALIGN		16
#define VMX_ALIGN_MASK		(VMX_ALIGN-1)

#define VECTOR_BREAKPOINT	512

static DEFINE_STATIC_KEY_FALSE(have_vec_crypto);

u32 __crc32c_vpmsum(u32 crc, const u8 *p, size_t len);

u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_le_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_le_arch);

u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	unsigned int prealign;
	unsigned int tail;

	if (len < (VECTOR_BREAKPOINT + VMX_ALIGN) ||
	    !static_branch_likely(&have_vec_crypto) || !crypto_simd_usable())
		return crc32c_base(crc, p, len);

	if ((unsigned long)p & VMX_ALIGN_MASK) {
		prealign = VMX_ALIGN - ((unsigned long)p & VMX_ALIGN_MASK);
		crc = crc32c_base(crc, p, prealign);
		len -= prealign;
		p += prealign;
	}

	if (len & ~VMX_ALIGN_MASK) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_altivec();
		crc = __crc32c_vpmsum(crc, p, len & ~VMX_ALIGN_MASK);
		disable_kernel_altivec();
		pagefault_enable();
		preempt_enable();
	}

	tail = len & VMX_ALIGN_MASK;
	if (tail) {
		p += len & ~VMX_ALIGN_MASK;
		crc = crc32c_base(crc, p, tail);
	}

	return crc;
}
EXPORT_SYMBOL(crc32c_arch);

u32 crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_be_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_be_arch);

static int __init crc32_powerpc_init(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		static_branch_enable(&have_vec_crypto);
	return 0;
}
arch_initcall(crc32_powerpc_init);

static void __exit crc32_powerpc_exit(void)
{
}
module_exit(crc32_powerpc_exit);

u32 crc32_optimizations(void)
{
	if (static_key_enabled(&have_vec_crypto))
		return CRC32C_OPTIMIZATION;
	return 0;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_AUTHOR("Anton Blanchard <anton@samba.org>");
MODULE_DESCRIPTION("CRC32C using vector polynomial multiply-sum instructions");
MODULE_LICENSE("GPL");
