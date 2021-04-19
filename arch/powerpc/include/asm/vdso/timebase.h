/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Common timebase prototypes and such for all ppc machines.
 */

#ifndef _ASM_POWERPC_VDSO_TIMEBASE_H
#define _ASM_POWERPC_VDSO_TIMEBASE_H

#include <asm/reg.h>

/*
 * We use __powerpc64__ here because we want the compat VDSO to use the 32-bit
 * version below in the else case of the ifdef.
 */
#if defined(__powerpc64__) && (defined(CONFIG_PPC_CELL) || defined(CONFIG_E500))
#define mftb()		({unsigned long rval;				\
			asm volatile(					\
				"90:	mfspr %0, %2;\n"		\
				ASM_FTR_IFSET(				\
					"97:	cmpwi %0,0;\n"		\
					"	beq- 90b;\n", "", %1)	\
			: "=r" (rval) \
			: "i" (CPU_FTR_CELL_TB_BUG), "i" (SPRN_TBRL) : "cr0"); \
			rval;})
#elif defined(CONFIG_PPC_8xx)
#define mftb()		({unsigned long rval;	\
			asm volatile("mftbl %0" : "=r" (rval)); rval;})
#else
#define mftb()		({unsigned long rval;	\
			asm volatile("mfspr %0, %1" : \
				     "=r" (rval) : "i" (SPRN_TBRL)); rval;})
#endif /* !CONFIG_PPC_CELL */

#if defined(CONFIG_PPC_8xx)
#define mftbu()		({unsigned long rval;	\
			asm volatile("mftbu %0" : "=r" (rval)); rval;})
#else
#define mftbu()		({unsigned long rval;	\
			asm volatile("mfspr %0, %1" : "=r" (rval) : \
				"i" (SPRN_TBRU)); rval;})
#endif

#define mttbl(v)	asm volatile("mttbl %0":: "r"(v))
#define mttbu(v)	asm volatile("mttbu %0":: "r"(v))

static __always_inline u64 get_tb(void)
{
	unsigned int tbhi, tblo, tbhi2;

	/*
	 * We use __powerpc64__ here not CONFIG_PPC64 because we want the compat
	 * VDSO to use the 32-bit compatible version in the while loop below.
	 */
	if (__is_defined(__powerpc64__))
		return mftb();

	do {
		tbhi = mftbu();
		tblo = mftb();
		tbhi2 = mftbu();
	} while (tbhi != tbhi2);

	return ((u64)tbhi << 32) | tblo;
}

static inline void set_tb(unsigned int upper, unsigned int lower)
{
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, upper);
	mtspr(SPRN_TBWL, lower);
}

#endif /* _ASM_POWERPC_VDSO_TIMEBASE_H */
