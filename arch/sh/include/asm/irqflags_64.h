#ifndef __ASM_SH_IRQFLAGS_64_H
#define __ASM_SH_IRQFLAGS_64_H

#include <cpu/registers.h>

#define SR_MASK_LL	0x00000000000000f0LL
#define SR_BL_LL	0x0000000010000000LL

static inline void raw_local_irq_enable(void)
{
	unsigned long long __dummy0, __dummy1 = ~SR_MASK_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "and	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));
}

static inline void raw_local_irq_disable(void)
{
	unsigned long long __dummy0, __dummy1 = SR_MASK_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "or	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));
}

static inline void set_bl_bit(void)
{
	unsigned long long __dummy0, __dummy1 = SR_BL_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "or	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));

}

static inline void clear_bl_bit(void)
{
	unsigned long long __dummy0, __dummy1 = ~SR_BL_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "and	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));
}

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long long __dummy = SR_MASK_LL;
	unsigned long flags;

	__asm__ __volatile__ (
		"getcon	" __SR ", %0\n\t"
		"and	%0, %1, %0"
		: "=&r" (flags)
		: "r" (__dummy));

	return flags;
}

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long long __dummy0, __dummy1 = SR_MASK_LL;
	unsigned long flags;

	__asm__ __volatile__ (
		"getcon	" __SR ", %1\n\t"
		"or	%1, r63, %0\n\t"
		"or	%1, %2, %1\n\t"
		"putcon	%1, " __SR "\n\t"
		"and	%0, %2, %0"
		: "=&r" (flags), "=&r" (__dummy0)
		: "r" (__dummy1));

	return flags;
}

#endif /* __ASM_SH_IRQFLAGS_64_H */
