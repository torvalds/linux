/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_SYSTEM_H
#define _ASM_C6X_SYSTEM_H

#include <linux/linkage.h>
#include <linux/irqflags.h>

#define prepare_to_switch()    do { } while (0)

struct task_struct;
struct thread_struct;
asmlinkage void *__switch_to(struct thread_struct *prev,
			     struct thread_struct *next,
			     struct task_struct *tsk);

#define switch_to(prev, next, last)				\
	do {							\
		current->thread.wchan = (u_long) __builtin_return_address(0); \
		(last) = __switch_to(&(prev)->thread,		\
				     &(next)->thread, (prev));	\
		mb();						\
		current->thread.wchan = 0;			\
	} while (0)

/* Reset the board */
#define HARD_RESET_NOW()

#define get_creg(reg) \
	({ unsigned int __x; \
	   asm volatile ("mvc .s2 " #reg ",%0\n" : "=b"(__x)); __x; })

#define set_creg(reg, v) \
	do { unsigned int __x = (unsigned int)(v); \
		asm volatile ("mvc .s2 %0," #reg "\n" : : "b"(__x)); \
	} while (0)

#define or_creg(reg, n) \
	do { unsigned __x, __n = (unsigned)(n);		  \
		asm volatile ("mvc .s2 " #reg ",%0\n"	  \
			      "or  .l2 %1,%0,%0\n"	  \
			      "mvc .s2 %0," #reg "\n"	  \
			      "nop\n"			  \
			      : "=&b"(__x) : "b"(__n));	  \
	} while (0)

#define and_creg(reg, n) \
	do { unsigned __x, __n = (unsigned)(n);		  \
		asm volatile ("mvc .s2 " #reg ",%0\n"	  \
			      "and .l2 %1,%0,%0\n"	  \
			      "mvc .s2 %0," #reg "\n"	  \
			      "nop\n"    \
			      : "=&b"(__x) : "b"(__n));	  \
	} while (0)

#define get_coreid() (get_creg(DNUM) & 0xff)

/* Set/get IST */
#define set_ist(x)	set_creg(ISTP, x)
#define get_ist()       get_creg(ISTP)

/*
 * Exception management
 */
asmlinkage void enable_exception(void);
#define disable_exception()
#define get_except_type()        get_creg(EFR)
#define ack_exception(type)      set_creg(ECR, 1 << (type))
#define get_iexcept()            get_creg(IERR)
#define set_iexcept(mask)        set_creg(IERR, (mask))

/*
 * Misc. functions
 */
#define nop()                    asm("NOP\n");
#define mb()                     barrier()
#define rmb()                    barrier()
#define wmb()                    barrier()
#define set_mb(var, value)       do { var = value;  mb(); } while (0)
#define set_wmb(var, value)      do { var = value; wmb(); } while (0)

#define smp_mb()	         barrier()
#define smp_rmb()	         barrier()
#define smp_wmb()	         barrier()
#define smp_read_barrier_depends()	do { } while (0)

#define xchg(ptr, x) \
	((__typeof__(*(ptr)))__xchg((unsigned int)(x), (void *) (ptr), \
				    sizeof(*(ptr))))
#define tas(ptr)    xchg((ptr), 1)

unsigned int _lmbd(unsigned int, unsigned int);
unsigned int _bitr(unsigned int);

struct __xchg_dummy { unsigned int a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

static inline unsigned int __xchg(unsigned int x, volatile void *ptr, int size)
{
	unsigned int tmp;
	unsigned long flags;

	local_irq_save(flags);

	switch (size) {
	case 1:
		tmp = 0;
		tmp = *((unsigned char *) ptr);
		*((unsigned char *) ptr) = (unsigned char) x;
		break;
	case 2:
		tmp = 0;
		tmp = *((unsigned short *) ptr);
		*((unsigned short *) ptr) = x;
		break;
	case 4:
		tmp = 0;
		tmp = *((unsigned int *) ptr);
		*((unsigned int *) ptr) = x;
		break;
	}
	local_irq_restore(flags);
	return tmp;
}

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr),		\
						     (unsigned long)(o), \
						     (unsigned long)(n), \
						     sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#include <asm-generic/cmpxchg.h>

#define _extu(x, s, e)							\
	({      unsigned int __x;					\
		asm volatile ("extu .S2 %3,%1,%2,%0\n" :		\
			      "=b"(__x) : "n"(s), "n"(e), "b"(x));	\
	       __x; })


extern unsigned int c6x_core_freq;

struct pt_regs;

extern void die(char *str, struct pt_regs *fp, int nr);
extern asmlinkage int process_exception(struct pt_regs *regs);
extern void time_init(void);
extern void free_initmem(void);

extern void (*c6x_restart)(void);
extern void (*c6x_halt)(void);

#endif /* _ASM_C6X_SYSTEM_H */
