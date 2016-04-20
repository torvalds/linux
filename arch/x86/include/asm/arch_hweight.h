#ifndef _ASM_X86_HWEIGHT_H
#define _ASM_X86_HWEIGHT_H

#include <asm/cpufeatures.h>

#ifdef CONFIG_64BIT
/* popcnt %edi, %eax -- redundant REX prefix for alignment */
#define POPCNT32 ".byte 0xf3,0x40,0x0f,0xb8,0xc7"
/* popcnt %rdi, %rax */
#define POPCNT64 ".byte 0xf3,0x48,0x0f,0xb8,0xc7"
#define REG_IN "D"
#define REG_OUT "a"
#else
/* popcnt %eax, %eax */
#define POPCNT32 ".byte 0xf3,0x0f,0xb8,0xc0"
#define REG_IN "a"
#define REG_OUT "a"
#endif

/*
 * __sw_hweightXX are called from within the alternatives below
 * and callee-clobbered registers need to be taken care of. See
 * ARCH_HWEIGHT_CFLAGS in <arch/x86/Kconfig> for the respective
 * compiler switches.
 */
static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
	unsigned int res = 0;

	asm (ALTERNATIVE("call __sw_hweight32", POPCNT32, X86_FEATURE_POPCNT)
		     : "="REG_OUT (res)
		     : REG_IN (w));

	return res;
}

static inline unsigned int __arch_hweight16(unsigned int w)
{
	return __arch_hweight32(w & 0xffff);
}

static inline unsigned int __arch_hweight8(unsigned int w)
{
	return __arch_hweight32(w & 0xff);
}

#ifdef CONFIG_X86_32
static inline unsigned long __arch_hweight64(__u64 w)
{
	return  __arch_hweight32((u32)w) +
		__arch_hweight32((u32)(w >> 32));
}
#else
static __always_inline unsigned long __arch_hweight64(__u64 w)
{
	unsigned long res = 0;

	asm (ALTERNATIVE("call __sw_hweight64", POPCNT64, X86_FEATURE_POPCNT)
		     : "="REG_OUT (res)
		     : REG_IN (w));

	return res;
}
#endif /* CONFIG_X86_32 */

#endif
