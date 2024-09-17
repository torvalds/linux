/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Compiler-dependent intrinsics.
 *
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef _UAPI_ASM_IA64_INTRINSICS_H
#define _UAPI_ASM_IA64_INTRINSICS_H


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

#define ia64_set_rr0_to_rr4(val0, val1, val2, val3, val4)		\
do {									\
	ia64_set_rr(0x0000000000000000UL, (val0));			\
	ia64_set_rr(0x2000000000000000UL, (val1));			\
	ia64_set_rr(0x4000000000000000UL, (val2));			\
	ia64_set_rr(0x6000000000000000UL, (val3));			\
	ia64_set_rr(0x8000000000000000UL, (val4));			\
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

#endif /* _UAPI_ASM_IA64_INTRINSICS_H */
