#ifndef __SPARC_SYSTEM_H
#define __SPARC_SYSTEM_H

#include <linux/kernel.h>
#include <linux/threads.h>	/* NR_CPUS */
#include <linux/thread_info.h>

#include <asm/page.h>
#include <asm/psr.h>
#include <asm/ptrace.h>
#include <asm/btfixup.h>
#include <asm/smp.h>

#ifndef __ASSEMBLY__

#include <linux/irqflags.h>

/*
 * Sparc (general) CPU types
 */
enum sparc_cpu {
  sun4        = 0x00,
  sun4c       = 0x01,
  sun4m       = 0x02,
  sun4d       = 0x03,
  sun4e       = 0x04,
  sun4u       = 0x05, /* V8 ploos ploos */
  sun_unknown = 0x06,
  ap1000      = 0x07, /* almost a sun4m */
};

/* Really, userland should not be looking at any of this... */
#ifdef __KERNEL__

extern enum sparc_cpu sparc_cpu_model;

#define ARCH_SUN4C (sparc_cpu_model==sun4c)

#define SUN4M_NCPUS            4              /* Architectural limit of sun4m. */

extern char reboot_command[];

extern struct thread_info *current_set[NR_CPUS];

extern unsigned long empty_bad_page;
extern unsigned long empty_bad_page_table;
extern unsigned long empty_zero_page;

extern void sun_do_break(void);
extern int serial_console;
extern int stop_a_enabled;
extern int scons_pwroff;

static inline int con_is_present(void)
{
	return serial_console ? 0 : 1;
}

/* When a context switch happens we must flush all user windows so that
 * the windows of the current process are flushed onto its stack. This
 * way the windows are all clean for the next process and the stack
 * frames are up to date.
 */
extern void flush_user_windows(void);
extern void kill_user_windows(void);
extern void synchronize_user_stack(void);
extern void fpsave(unsigned long *fpregs, unsigned long *fsr,
		   void *fpqueue, unsigned long *fpqdepth);

#ifdef CONFIG_SMP
#define SWITCH_ENTER(prv) \
	do {			\
	if (test_tsk_thread_flag(prv, TIF_USEDFPU)) { \
		put_psr(get_psr() | PSR_EF); \
		fpsave(&(prv)->thread.float_regs[0], &(prv)->thread.fsr, \
		       &(prv)->thread.fpqueue[0], &(prv)->thread.fpqdepth); \
		clear_tsk_thread_flag(prv, TIF_USEDFPU); \
		(prv)->thread.kregs->psr &= ~PSR_EF; \
	} \
	} while(0)

#define SWITCH_DO_LAZY_FPU(next)	/* */
#else
#define SWITCH_ENTER(prv)		/* */
#define SWITCH_DO_LAZY_FPU(nxt)	\
	do {			\
	if (last_task_used_math != (nxt))		\
		(nxt)->thread.kregs->psr&=~PSR_EF;	\
	} while(0)
#endif

extern void flushw_all(void);

/*
 * Flush windows so that the VM switch which follows
 * would not pull the stack from under us.
 *
 * SWITCH_ENTER and SWITH_DO_LAZY_FPU do not work yet (e.g. SMP does not work)
 * XXX WTF is the above comment? Found in late teen 2.4.x.
 */
#define prepare_arch_switch(next) do { \
	__asm__ __volatile__( \
	".globl\tflush_patch_switch\nflush_patch_switch:\n\t" \
	"save %sp, -0x40, %sp; save %sp, -0x40, %sp; save %sp, -0x40, %sp\n\t" \
	"save %sp, -0x40, %sp; save %sp, -0x40, %sp; save %sp, -0x40, %sp\n\t" \
	"save %sp, -0x40, %sp\n\t" \
	"restore; restore; restore; restore; restore; restore; restore"); \
} while(0)

	/* Much care has gone into this code, do not touch it.
	 *
	 * We need to loadup regs l0/l1 for the newly forked child
	 * case because the trap return path relies on those registers
	 * holding certain values, gcc is told that they are clobbered.
	 * Gcc needs registers for 3 values in and 1 value out, so we
	 * clobber every non-fixed-usage register besides l2/l3/o4/o5.  -DaveM
	 *
	 * Hey Dave, that do not touch sign is too much of an incentive
	 * - Anton & Pete
	 */
#define switch_to(prev, next, last) do {						\
	SWITCH_ENTER(prev);								\
	SWITCH_DO_LAZY_FPU(next);							\
	cpu_set(smp_processor_id(), next->active_mm->cpu_vm_mask);			\
	__asm__ __volatile__(								\
	"sethi	%%hi(here - 0x8), %%o7\n\t"						\
	"mov	%%g6, %%g3\n\t"								\
	"or	%%o7, %%lo(here - 0x8), %%o7\n\t"					\
	"rd	%%psr, %%g4\n\t"							\
	"std	%%sp, [%%g6 + %4]\n\t"							\
	"rd	%%wim, %%g5\n\t"							\
	"wr	%%g4, 0x20, %%psr\n\t"							\
	"nop\n\t"									\
	"std	%%g4, [%%g6 + %3]\n\t"							\
	"ldd	[%2 + %3], %%g4\n\t"							\
	"mov	%2, %%g6\n\t"								\
	".globl	patchme_store_new_current\n"						\
"patchme_store_new_current:\n\t"							\
	"st	%2, [%1]\n\t"								\
	"wr	%%g4, 0x20, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"nop\n\t"	/* LEON needs all 3 nops: load to %sp depends on CWP. */		\
	"ldd	[%%g6 + %4], %%sp\n\t"							\
	"wr	%%g5, 0x0, %%wim\n\t"							\
	"ldd	[%%sp + 0x00], %%l0\n\t"						\
	"ldd	[%%sp + 0x38], %%i6\n\t"						\
	"wr	%%g4, 0x0, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"jmpl	%%o7 + 0x8, %%g0\n\t"							\
	" ld	[%%g3 + %5], %0\n\t"							\
	"here:\n"									\
        : "=&r" (last)									\
        : "r" (&(current_set[hard_smp_processor_id()])),	\
	  "r" (task_thread_info(next)),				\
	  "i" (TI_KPSR),					\
	  "i" (TI_KSP),						\
	  "i" (TI_TASK)						\
	:       "g1", "g2", "g3", "g4", "g5",       "g7",	\
	  "l0", "l1",       "l3", "l4", "l5", "l6", "l7",	\
	  "i0", "i1", "i2", "i3", "i4", "i5",			\
	  "o0", "o1", "o2", "o3",                   "o7");	\
	} while(0)

/* XXX Change this if we ever use a PSO mode kernel. */
#define mb()	__asm__ __volatile__ ("" : : : "memory")
#define rmb()	mb()
#define wmb()	mb()
#define read_barrier_depends()	do { } while(0)
#define set_mb(__var, __value)  do { __var = __value; mb(); } while(0)
#define smp_mb()	__asm__ __volatile__("":::"memory")
#define smp_rmb()	__asm__ __volatile__("":::"memory")
#define smp_wmb()	__asm__ __volatile__("":::"memory")
#define smp_read_barrier_depends()	do { } while(0)

#define nop() __asm__ __volatile__ ("nop")

/* This has special calling conventions */
#ifndef CONFIG_SMP
BTFIXUPDEF_CALL(void, ___xchg32, void)
#endif

static inline unsigned long xchg_u32(__volatile__ unsigned long *m, unsigned long val)
{
#ifdef CONFIG_SMP
	__asm__ __volatile__("swap [%2], %0"
			     : "=&r" (val)
			     : "0" (val), "r" (m)
			     : "memory");
	return val;
#else
	register unsigned long *ptr asm("g1");
	register unsigned long ret asm("g2");

	ptr = (unsigned long *) m;
	ret = val;

	/* Note: this is magic and the nop there is
	   really needed. */
	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___f____xchg32\n\t"
	" nop\n\t"
	: "=&r" (ret)
	: "0" (ret), "r" (ptr)
	: "g3", "g4", "g7", "memory", "cc");

	return ret;
#endif
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, __volatile__ void * ptr, int size)
{
	switch (size) {
	case 4:
		return xchg_u32(ptr, x);
	};
	__xchg_called_with_bad_pointer();
	return x;
}

/* Emulate cmpxchg() the same way we emulate atomics,
 * by hashing the object address and indexing into an array
 * of spinlocks to get a bit of performance...
 *
 * See arch/sparc/lib/atomic32.c for implementation.
 *
 * Cribbed from <asm-parisc/atomic.h>
 */
#define __HAVE_ARCH_CMPXCHG	1

/* bug catcher for when unsupported size is used - won't link */
extern void __cmpxchg_called_with_bad_pointer(void);
/* we only need to support cmpxchg of a u32 on sparc */
extern unsigned long __cmpxchg_u32(volatile u32 *m, u32 old, u32 new_);

/* don't worry...optimizer will get rid of most of this */
static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new_, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32((u32 *)ptr, (u32)old, (u32)new_);
	default:
		__cmpxchg_called_with_bad_pointer();
		break;
	}
	return old;
}

#define cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,	\
			(unsigned long)_n_, sizeof(*(ptr)));		\
})

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

extern void die_if_kernel(char *str, struct pt_regs *regs) __attribute__ ((noreturn));

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#define arch_align_stack(x) (x)

#endif /* !(__SPARC_SYSTEM_H) */
