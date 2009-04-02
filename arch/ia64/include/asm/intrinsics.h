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

/*
 * This function doesn't exist, so you'll get a linker error if
 * something tries to do an invalid xchg().
 */
extern void ia64_xchg_called_with_bad_pointer (void);

#define __xchg(x,ptr,size)						\
({									\
	unsigned long __xchg_result;					\
									\
	switch (size) {							\
	      case 1:							\
		__xchg_result = ia64_xchg1((__u8 *)ptr, x);		\
		break;							\
									\
	      case 2:							\
		__xchg_result = ia64_xchg2((__u16 *)ptr, x);		\
		break;							\
									\
	      case 4:							\
		__xchg_result = ia64_xchg4((__u32 *)ptr, x);		\
		break;							\
									\
	      case 8:							\
		__xchg_result = ia64_xchg8((__u64 *)ptr, x);		\
		break;							\
	      default:							\
		ia64_xchg_called_with_bad_pointer();			\
	}								\
	__xchg_result;							\
})

#define xchg(ptr,x)							     \
  ((__typeof__(*(ptr))) __xchg ((unsigned long) (x), (ptr), sizeof(*(ptr))))

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG 1

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg().
 */
extern long ia64_cmpxchg_called_with_bad_pointer (void);

#define ia64_cmpxchg(sem,ptr,old,new,size)						\
({											\
	__u64 _o_, _r_;									\
											\
	switch (size) {									\
	      case 1: _o_ = (__u8 ) (long) (old); break;				\
	      case 2: _o_ = (__u16) (long) (old); break;				\
	      case 4: _o_ = (__u32) (long) (old); break;				\
	      case 8: _o_ = (__u64) (long) (old); break;				\
	      default: break;								\
	}										\
	switch (size) {									\
	      case 1:									\
	      	_r_ = ia64_cmpxchg1_##sem((__u8 *) ptr, new, _o_);			\
		break;									\
											\
	      case 2:									\
	       _r_ = ia64_cmpxchg2_##sem((__u16 *) ptr, new, _o_);			\
		break;									\
											\
	      case 4:									\
	      	_r_ = ia64_cmpxchg4_##sem((__u32 *) ptr, new, _o_);			\
		break;									\
											\
	      case 8:									\
		_r_ = ia64_cmpxchg8_##sem((__u64 *) ptr, new, _o_);			\
		break;									\
											\
	      default:									\
		_r_ = ia64_cmpxchg_called_with_bad_pointer();				\
		break;									\
	}										\
	(__typeof__(old)) _r_;								\
})

#define cmpxchg_acq(ptr, o, n)	\
	ia64_cmpxchg(acq, (ptr), (o), (n), sizeof(*(ptr)))
#define cmpxchg_rel(ptr, o, n)	\
	ia64_cmpxchg(rel, (ptr), (o), (n), sizeof(*(ptr)))

/* for compatibility with other platforms: */
#define cmpxchg(ptr, o, n)	cmpxchg_acq((ptr), (o), (n))
#define cmpxchg64(ptr, o, n)	cmpxchg_acq((ptr), (o), (n))

#define cmpxchg_local		cmpxchg
#define cmpxchg64_local		cmpxchg64

#ifdef CONFIG_IA64_DEBUG_CMPXCHG
# define CMPXCHG_BUGCHECK_DECL	int _cmpxchg_bugcheck_count = 128;
# define CMPXCHG_BUGCHECK(v)							\
  do {										\
	if (_cmpxchg_bugcheck_count-- <= 0) {					\
		void *ip;							\
		extern int printk(const char *fmt, ...);			\
		ip = (void *) ia64_getreg(_IA64_REG_IP);			\
		printk("CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));	\
		break;								\
	}									\
  } while (0)
#else /* !CONFIG_IA64_DEBUG_CMPXCHG */
# define CMPXCHG_BUGCHECK_DECL
# define CMPXCHG_BUGCHECK(v)
#endif /* !CONFIG_IA64_DEBUG_CMPXCHG */

#endif

#ifdef __KERNEL__
#include <asm/paravirt_privop.h>
#endif

#ifndef __ASSEMBLY__
#if defined(CONFIG_PARAVIRT) && defined(__KERNEL__)
#ifdef ASM_SUPPORTED
# define IA64_INTRINSIC_API(name)	paravirt_ ## name
#else
# define IA64_INTRINSIC_API(name)	pv_cpu_ops.name
#endif
#define IA64_INTRINSIC_MACRO(name)	paravirt_ ## name
#else
#define IA64_INTRINSIC_API(name)	ia64_native_ ## name
#define IA64_INTRINSIC_MACRO(name)	ia64_native_ ## name
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
