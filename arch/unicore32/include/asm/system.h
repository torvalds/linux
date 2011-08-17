/*
 * linux/arch/unicore32/include/asm/system.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_SYSTEM_H__
#define __UNICORE_SYSTEM_H__

#ifdef __KERNEL__

/*
 * CR1 bits (CP#0 CR1)
 */
#define CR_M	(1 << 0)	/* MMU enable				*/
#define CR_A	(1 << 1)	/* Alignment abort enable		*/
#define CR_D	(1 << 2)	/* Dcache enable			*/
#define CR_I	(1 << 3)	/* Icache enable			*/
#define CR_B	(1 << 4)	/* Dcache write mechanism: write back	*/
#define CR_T	(1 << 5)	/* Burst enable				*/
#define CR_V	(1 << 13)	/* Vectors relocated to 0xffff0000	*/

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/irqflags.h>

struct thread_info;
struct task_struct;

struct pt_regs;

void die(const char *msg, struct pt_regs *regs, int err);

struct siginfo;
void uc32_notify_die(const char *str, struct pt_regs *regs,
		struct siginfo *info, unsigned long err, unsigned long trap);

void hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int,
				       struct pt_regs *),
		     int sig, int code, const char *name);

#define xchg(ptr, x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))

extern asmlinkage void __backtrace(void);
extern asmlinkage void c_backtrace(unsigned long fp, int pmode);

struct mm_struct;
extern void show_pte(struct mm_struct *mm, unsigned long addr);
extern void __show_regs(struct pt_regs *);

extern int cpu_architecture(void);
extern void cpu_init(void);

#define vectors_high()	(cr_alignment & CR_V)

#define isb() __asm__ __volatile__ ("" : : : "memory")
#define dsb() __asm__ __volatile__ ("" : : : "memory")
#define dmb() __asm__ __volatile__ ("" : : : "memory")

#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define read_barrier_depends()		do { } while (0)
#define smp_read_barrier_depends()	do { } while (0)

#define set_mb(var, value)	do { var = value; smp_mb(); } while (0)
#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

extern unsigned long cr_no_alignment;	/* defined in entry-unicore.S */
extern unsigned long cr_alignment;	/* defined in entry-unicore.S */

static inline unsigned int get_cr(void)
{
	unsigned int val;
	asm("movc %0, p0.c1, #0" : "=r" (val) : : "cc");
	return val;
}

static inline void set_cr(unsigned int val)
{
	asm volatile("movc p0.c1, %0, #0	@set CR"
	  : : "r" (val) : "cc");
	isb();
}

extern void adjust_cr(unsigned long mask, unsigned long set);

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.  schedule() itself
 * contains the memory barrier to tell GCC not to cache `current'.
 */
extern struct task_struct *__switch_to(struct task_struct *,
		struct thread_info *, struct thread_info *);
extern void panic(const char *fmt, ...);

#define switch_to(prev, next, last)					\
do {									\
	last = __switch_to(prev,					\
		task_thread_info(prev), task_thread_info(next));	\
} while (0)

static inline unsigned long
__xchg(unsigned long x, volatile void *ptr, int size)
{
	unsigned long ret;

	switch (size) {
	case 1:
		asm volatile("@	__xchg1\n"
		"	swapb	%0, %1, [%2]"
			: "=&r" (ret)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	case 4:
		asm volatile("@	__xchg4\n"
		"	swapw	%0, %1, [%2]"
			: "=&r" (ret)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	default:
		panic("xchg: bad data size: ptr 0x%p, size %d\n",
			ptr, size);
	}

	return ret;
}

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
		((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr),	\
		(unsigned long)(o), (unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n)					\
		__cmpxchg64_local_generic((ptr), (o), (n))

#include <asm-generic/cmpxchg.h>

#endif /* __ASSEMBLY__ */

#define arch_align_stack(x) (x)

#endif /* __KERNEL__ */

#endif
