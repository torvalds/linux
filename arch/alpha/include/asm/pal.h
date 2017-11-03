/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_PAL_H
#define __ALPHA_PAL_H

#include <uapi/asm/pal.h>

#ifndef __ASSEMBLY__

extern void halt(void) __attribute__((noreturn));
#define __halt() __asm__ __volatile__ ("call_pal %0 #halt" : : "i" (PAL_halt))

#define imb() \
__asm__ __volatile__ ("call_pal %0 #imb" : : "i" (PAL_imb) : "memory")

#define draina() \
__asm__ __volatile__ ("call_pal %0 #draina" : : "i" (PAL_draina) : "memory")

#define __CALL_PAL_R0(NAME, TYPE)				\
extern inline TYPE NAME(void)					\
{								\
	register TYPE __r0 __asm__("$0");			\
	__asm__ __volatile__(					\
		"call_pal %1 # " #NAME				\
		:"=r" (__r0)					\
		:"i" (PAL_ ## NAME)				\
		:"$1", "$16", "$22", "$23", "$24", "$25");	\
	return __r0;						\
}

#define __CALL_PAL_W1(NAME, TYPE0)				\
extern inline void NAME(TYPE0 arg0)				\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %1 # "#NAME				\
		: "=r"(__r16)					\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_W2(NAME, TYPE0, TYPE1)			\
extern inline void NAME(TYPE0 arg0, TYPE1 arg1)			\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r17)			\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_RW1(NAME, RTYPE, TYPE0)			\
extern inline RTYPE NAME(TYPE0 arg0)				\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r0)			\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

#define __CALL_PAL_RW2(NAME, RTYPE, TYPE0, TYPE1)		\
extern inline RTYPE NAME(TYPE0 arg0, TYPE1 arg1)		\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %3 # "#NAME				\
		: "=r"(__r16), "=r"(__r17), "=r"(__r0)		\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

__CALL_PAL_W1(cflush, unsigned long);
__CALL_PAL_R0(rdmces, unsigned long);
__CALL_PAL_R0(rdps, unsigned long);
__CALL_PAL_R0(rdusp, unsigned long);
__CALL_PAL_RW1(swpipl, unsigned long, unsigned long);
__CALL_PAL_R0(whami, unsigned long);
__CALL_PAL_W2(wrent, void*, unsigned long);
__CALL_PAL_W1(wripir, unsigned long);
__CALL_PAL_W1(wrkgp, unsigned long);
__CALL_PAL_W1(wrmces, unsigned long);
__CALL_PAL_RW2(wrperfmon, unsigned long, unsigned long, unsigned long);
__CALL_PAL_W1(wrusp, unsigned long);
__CALL_PAL_W1(wrvptptr, unsigned long);
__CALL_PAL_RW1(wtint, unsigned long, unsigned long);

/*
 * TB routines..
 */
#define __tbi(nr,arg,arg1...)					\
({								\
	register unsigned long __r16 __asm__("$16") = (nr);	\
	register unsigned long __r17 __asm__("$17"); arg;	\
	__asm__ __volatile__(					\
		"call_pal %3 #__tbi"				\
		:"=r" (__r16),"=r" (__r17)			\
		:"0" (__r16),"i" (PAL_tbi) ,##arg1		\
		:"$0", "$1", "$22", "$23", "$24", "$25");	\
})

#define tbi(x,y)	__tbi(x,__r17=(y),"1" (__r17))
#define tbisi(x)	__tbi(1,__r17=(x),"1" (__r17))
#define tbisd(x)	__tbi(2,__r17=(x),"1" (__r17))
#define tbis(x)		__tbi(3,__r17=(x),"1" (__r17))
#define tbiap()		__tbi(-1, /* no second argument */)
#define tbia()		__tbi(-2, /* no second argument */)

/*
 * QEMU Cserv routines..
 */

static inline unsigned long
qemu_get_walltime(void)
{
	register unsigned long v0 __asm__("$0");
	register unsigned long a0 __asm__("$16") = 3;

	asm("call_pal %2 # cserve get_time"
	    : "=r"(v0), "+r"(a0)
	    : "i"(PAL_cserve)
	    : "$17", "$18", "$19", "$20", "$21");

	return v0;
}

static inline unsigned long
qemu_get_alarm(void)
{
	register unsigned long v0 __asm__("$0");
	register unsigned long a0 __asm__("$16") = 4;

	asm("call_pal %2 # cserve get_alarm"
	    : "=r"(v0), "+r"(a0)
	    : "i"(PAL_cserve)
	    : "$17", "$18", "$19", "$20", "$21");

	return v0;
}

static inline void
qemu_set_alarm_rel(unsigned long expire)
{
	register unsigned long a0 __asm__("$16") = 5;
	register unsigned long a1 __asm__("$17") = expire;

	asm volatile("call_pal %2 # cserve set_alarm_rel"
		     : "+r"(a0), "+r"(a1)
		     : "i"(PAL_cserve)
		     : "$0", "$18", "$19", "$20", "$21");
}

static inline void
qemu_set_alarm_abs(unsigned long expire)
{
	register unsigned long a0 __asm__("$16") = 6;
	register unsigned long a1 __asm__("$17") = expire;

	asm volatile("call_pal %2 # cserve set_alarm_abs"
		     : "+r"(a0), "+r"(a1)
		     : "i"(PAL_cserve)
		     : "$0", "$18", "$19", "$20", "$21");
}

static inline unsigned long
qemu_get_vmtime(void)
{
	register unsigned long v0 __asm__("$0");
	register unsigned long a0 __asm__("$16") = 7;

	asm("call_pal %2 # cserve get_time"
	    : "=r"(v0), "+r"(a0)
	    : "i"(PAL_cserve)
	    : "$17", "$18", "$19", "$20", "$21");

	return v0;
}

#endif /* !__ASSEMBLY__ */
#endif /* __ALPHA_PAL_H */
