#ifndef _ASM_IA64_INTRINSICS_H
#define _ASM_IA64_INTRINSICS_H

/*
 * Compiler-dependent intrinsics.
 *
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifndef __ASSEMBLY__

#include <linux/types.h>
/* include compiler specific intrinsics */
#include <asm/ia64regs.h>
#ifdef __INTEL_COMPILER
# include <asm/intel_intrin.h>
#else
# include <asm/gcc_intrin.h>
#endif
#include <asm/cmpxchg.h>

#define ia64_native_get_psr_i()	(ia64_native_getreg(_IA64_REG_PSR) & IA64_PSR_I)

#define ia64_native_set_rr0_to_rr4(val0, val1, val2, val3, val4)	\
do {									\
	ia64_native_set_rr(0x0000000000000000UL, (val0));		\
	ia64_native_set_rr(0x2000000000000000UL, (val1));		\
	ia64_native_set_rr(0x4000000000000000UL, (val2));		\
	ia64_native_set_rr(0x6000000000000000UL, (val3));		\
	ia64_native_set_rr(0x8000000000000000UL, (val4));		\
} while (0)

/*
 * Force an unresolved reference if someone tries to use
 * ia64_fetch_and_add() with a bad value.
 */
extern unsigned long __bad_size_for_ia64_fetch_and_add (void);
extern unsigned long __bad_increment_for_ia64_fetch_and_add (void);

#define IA64_FETCHADD(tmp,v,n,sz,sem)						\
({										\
	switch (sz) {								\
	      case 4:								\
	        tmp = ia64_fetchadd4_##sem((unsigned int *) v, n);		\
		break;								\
										\
	      case 8:								\
	        tmp = ia64_fetchadd8_##sem((unsigned long *) v, n);		\
		break;								\
										\
	      default:								\
		__bad_size_for_ia64_fetch_and_add();				\
	}									\
})

#define ia64_fetchadd(i,v,sem)								\
({											\
	__u64 _tmp;									\
	volatile __typeof__(*(v)) *_v = (v);						\
	/* Can't use a switch () here: gcc isn't always smart enough for that... */	\
	if ((i) == -16)									\
		IA64_FETCHADD(_tmp, _v, -16, sizeof(*(v)), sem);			\
	else if ((i) == -8)								\
		IA64_FETCHADD(_tmp, _v, -8, sizeof(*(v)), sem);				\
	else if ((i) == -4)								\
		IA64_FETCHADD(_tmp, _v, -4, sizeof(*(v)), sem);				\
	else if ((i) == -1)								\
		IA64_FETCHADD(_tmp, _v, -1, sizeof(*(v)), sem);				\
	else if ((i) == 1)								\
		IA64_FETCHADD(_tmp, _v, 1, sizeof(*(v)), sem);				\
	else if ((i) == 4)								\
		IA64_FETCHADD(_tmp, _v, 4, sizeof(*(v)), sem);				\
	else if ((i) == 8)								\
		IA64_FETCHADD(_tmp, _v, 8, sizeof(*(v)), sem);				\
	else if ((i) == 16)								\
		IA64_FETCHADD(_tmp, _v, 16, sizeof(*(v)), sem);				\
	else										\
		_tmp = __bad_increment_for_ia64_fetch_and_add();			\
	(__typeof__(*(v))) (_tmp);	/* return old value */				\
})

#define ia64_fetch_and_add(i,v)	(ia64_fetchadd(i, v, rel) + (i)) /* return new value */

#endif

#ifdef __KERNEL__
#include <asm/paravirt_privop.h>
#endif

#ifndef __ASSEMBLY__

#define IA64_INTRINSIC_API(name)	ia64_native_ ## name
#define IA64_INTRINSIC_MACRO(name)	ia64_native_ ## name

#if defined(__KERNEL__)
#if defined(CONFIG_PARAVIRT)
# undef IA64_INTRINSIC_API
# undef IA64_INTRINSIC_MACRO
# ifdef ASM_SUPPORTED
#  define IA64_INTRINSIC_API(name)	paravirt_ ## name
# else
#  define IA64_INTRINSIC_API(name)	pv_cpu_ops.name
# endif
#define IA64_INTRINSIC_MACRO(name)	paravirt_ ## name
#endif
#endif

/************************************************/
/* Instructions paravirtualized for correctness */
/************************************************/
/* fc, thash, get_cpuid, get_pmd, get_eflags, set_eflags */
/* Note that "ttag" and "cover" are also privilege-sensitive; "ttag"
 * is not currently used (though it may be in a long-format VHPT system!)
 */
#define ia64_fc				IA64_INTRINSIC_API(fc)
#define ia64_thash			IA64_INTRINSIC_API(thash)
#define ia64_get_cpuid			IA64_INTRINSIC_API(get_cpuid)
#define ia64_get_pmd			IA64_INTRINSIC_API(get_pmd)


/************************************************/
/* Instructions paravirtualized for performance */
/************************************************/
#define ia64_ssm			IA64_INTRINSIC_MACRO(ssm)
#define ia64_rsm			IA64_INTRINSIC_MACRO(rsm)
#define ia64_getreg			IA64_INTRINSIC_MACRO(getreg)
#define ia64_setreg			IA64_INTRINSIC_API(setreg)
#define ia64_set_rr			IA64_INTRINSIC_API(set_rr)
#define ia64_get_rr			IA64_INTRINSIC_API(get_rr)
#define ia64_ptcga			IA64_INTRINSIC_API(ptcga)
#define ia64_get_psr_i			IA64_INTRINSIC_API(get_psr_i)
#define ia64_intrin_local_irq_restore	\
	IA64_INTRINSIC_API(intrin_local_irq_restore)
#define ia64_set_rr0_to_rr4		IA64_INTRINSIC_API(set_rr0_to_rr4)

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_INTRINSICS_H */
