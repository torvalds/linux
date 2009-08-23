/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_SYSTEM_H
#define _ASM_POWERPC_SYSTEM_H

#include <linux/kernel.h>
#include <linux/irqflags.h>

#include <asm/hw_irq.h>

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 * read_barrier_depends() prevents data-dependent loads being reordered
 *	across this point (nop on PPC).
 *
 * *mb() variants without smp_ prefix must order all types of memory
 * operations with one another. sync is the only instruction sufficient
 * to do this.
 *
 * For the smp_ barriers, ordering is for cacheable memory operations
 * only. We have to use the sync instruction for smp_mb(), since lwsync
 * doesn't order loads with respect to previous stores.  Lwsync can be
 * used for smp_rmb() and smp_wmb().
 *
 * However, on CPUs that don't support lwsync, lwsync actually maps to a
 * heavy-weight sync, so smp_wmb() can be a lighter-weight eieio.
 */
#define mb()   __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define read_barrier_depends()  do { } while(0)

#define set_mb(var, value)	do { var = value; mb(); } while (0)

#ifdef __KERNEL__
#define AT_VECTOR_SIZE_ARCH 6 /* entries in ARCH_DLINFO */
#ifdef CONFIG_SMP

#ifdef __SUBARCH_HAS_LWSYNC
#    define SMPWMB      LWSYNC
#else
#    define SMPWMB      eieio
#endif

#define smp_mb()	mb()
#define smp_rmb()	__asm__ __volatile__ (stringify_in_c(LWSYNC) : : :"memory")
#define smp_wmb()	__asm__ __volatile__ (stringify_in_c(SMPWMB) : : :"memory")
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#endif /* CONFIG_SMP */

/*
 * This is a barrier which prevents following instructions from being
 * started until the value of the argument x is known.  For example, if
 * x is a variable loaded from memory, this prevents following
 * instructions from being executed until the load has been performed.
 */
#define data_barrier(x)	\
	asm volatile("twi 0,%0,0; isync" : : "r" (x) : "memory");

struct task_struct;
struct pt_regs;

#if defined(CONFIG_DEBUGGER) || defined(CONFIG_KEXEC)

extern int (*__debugger)(struct pt_regs *regs);
extern int (*__debugger_ipi)(struct pt_regs *regs);
extern int (*__debugger_bpt)(struct pt_regs *regs);
extern int (*__debugger_sstep)(struct pt_regs *regs);
extern int (*__debugger_iabr_match)(struct pt_regs *regs);
extern int (*__debugger_dabr_match)(struct pt_regs *regs);
extern int (*__debugger_fault_handler)(struct pt_regs *regs);

#define DEBUGGER_BOILERPLATE(__NAME) \
static inline int __NAME(struct pt_regs *regs) \
{ \
	if (unlikely(__ ## __NAME)) \
		return __ ## __NAME(regs); \
	return 0; \
}

DEBUGGER_BOILERPLATE(debugger)
DEBUGGER_BOILERPLATE(debugger_ipi)
DEBUGGER_BOILERPLATE(debugger_bpt)
DEBUGGER_BOILERPLATE(debugger_sstep)
DEBUGGER_BOILERPLATE(debugger_iabr_match)
DEBUGGER_BOILERPLATE(debugger_dabr_match)
DEBUGGER_BOILERPLATE(debugger_fault_handler)

#else
static inline int debugger(struct pt_regs *regs) { return 0; }
static inline int debugger_ipi(struct pt_regs *regs) { return 0; }
static inline int debugger_bpt(struct pt_regs *regs) { return 0; }
static inline int debugger_sstep(struct pt_regs *regs) { return 0; }
static inline int debugger_iabr_match(struct pt_regs *regs) { return 0; }
static inline int debugger_dabr_match(struct pt_regs *regs) { return 0; }
static inline int debugger_fault_handler(struct pt_regs *regs) { return 0; }
#endif

extern int set_dabr(unsigned long dabr);
extern void do_dabr(struct pt_regs *regs, unsigned long address,
		    unsigned long error_code);
extern void print_backtrace(unsigned long *);
extern void show_regs(struct pt_regs * regs);
extern void flush_instruction_cache(void);
extern void hard_reset_now(void);
extern void poweroff_now(void);

#ifdef CONFIG_6xx
extern long _get_L2CR(void);
extern long _get_L3CR(void);
extern void _set_L2CR(unsigned long);
extern void _set_L3CR(unsigned long);
#else
#define _get_L2CR()	0L
#define _get_L3CR()	0L
#define _set_L2CR(val)	do { } while(0)
#define _set_L3CR(val)	do { } while(0)
#endif

extern void via_cuda_init(void);
extern void read_rtc_time(void);
extern void pmac_find_display(void);
extern void giveup_fpu(struct task_struct *);
extern void disable_kernel_fp(void);
extern void enable_kernel_fp(void);
extern void flush_fp_to_thread(struct task_struct *);
extern void enable_kernel_altivec(void);
extern void giveup_altivec(struct task_struct *);
extern void load_up_altivec(struct task_struct *);
extern int emulate_altivec(struct pt_regs *);
extern void __giveup_vsx(struct task_struct *);
extern void giveup_vsx(struct task_struct *);
extern void enable_kernel_spe(void);
extern void giveup_spe(struct task_struct *);
extern void load_up_spe(struct task_struct *);
extern int fix_alignment(struct pt_regs *);
extern void cvt_fd(float *from, double *to, struct thread_struct *thread);
extern void cvt_df(double *from, float *to, struct thread_struct *thread);

#ifndef CONFIG_SMP
extern void discard_lazy_cpu_state(void);
#else
static inline void discard_lazy_cpu_state(void)
{
}
#endif

#ifdef CONFIG_ALTIVEC
extern void flush_altivec_to_thread(struct task_struct *);
#else
static inline void flush_altivec_to_thread(struct task_struct *t)
{
}
#endif

#ifdef CONFIG_VSX
extern void flush_vsx_to_thread(struct task_struct *);
#else
static inline void flush_vsx_to_thread(struct task_struct *t)
{
}
#endif

#ifdef CONFIG_SPE
extern void flush_spe_to_thread(struct task_struct *);
#else
static inline void flush_spe_to_thread(struct task_struct *t)
{
}
#endif

extern int call_rtas(const char *, int, int, unsigned long *, ...);
extern void cacheable_memzero(void *p, unsigned int nb);
extern void *cacheable_memcpy(void *, const void *, unsigned int);
extern int do_page_fault(struct pt_regs *, unsigned long, unsigned long);
extern void bad_page_fault(struct pt_regs *, unsigned long, int);
extern int die(const char *, struct pt_regs *, long);
extern void _exception(int, struct pt_regs *, int, unsigned long);
extern void _nmask_and_or_msr(unsigned long nmask, unsigned long or_val);

#ifdef CONFIG_BOOKE_WDT
extern u32 booke_wdt_enabled;
extern u32 booke_wdt_period;
#endif /* CONFIG_BOOKE_WDT */

struct device_node;
extern void note_scsi_host(struct device_node *, void *);

extern struct task_struct *__switch_to(struct task_struct *,
	struct task_struct *);
#define switch_to(prev, next, last)	((last) = __switch_to((prev), (next)))

struct thread_struct;
extern struct task_struct *_switch(struct thread_struct *prev,
				   struct thread_struct *next);

extern unsigned int rtas_data;
extern int mem_init_done;	/* set on boot once kmalloc can be called */
extern int init_bootmem_done;	/* set once bootmem is available */
extern phys_addr_t memory_limit;
extern unsigned long klimit;

extern void *alloc_maybe_bootmem(size_t size, gfp_t mask);
extern void *zalloc_maybe_bootmem(size_t size, gfp_t mask);

extern int powersave_nap;	/* set if nap mode can be used in idle loop */

/*
 * Atomic exchange
 *
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 */
static __always_inline unsigned long
__xchg_u32(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%2 \n"
	PPC405_ERR77(0,%2)
"	stwcx.	%3,0,%2 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (prev), "+m" (*(volatile unsigned int *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

/*
 * Atomic exchange
 *
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 */
static __always_inline unsigned long
__xchg_u32_local(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2 \n"
	PPC405_ERR77(0,%2)
"	stwcx.	%3,0,%2 \n\
	bne-	1b"
	: "=&r" (prev), "+m" (*(volatile unsigned int *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

#ifdef CONFIG_PPC64
static __always_inline unsigned long
__xchg_u64(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%2 \n"
	PPC405_ERR77(0,%2)
"	stdcx.	%3,0,%2 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (prev), "+m" (*(volatile unsigned long *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__xchg_u64_local(volatile void *p, unsigned long val)
{
	unsigned long prev;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2 \n"
	PPC405_ERR77(0,%2)
"	stdcx.	%3,0,%2 \n\
	bne-	1b"
	: "=&r" (prev), "+m" (*(volatile unsigned long *)p)
	: "r" (p), "r" (val)
	: "cc", "memory");

	return prev;
}
#endif

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 */
extern void __xchg_called_with_bad_pointer(void);

static __always_inline unsigned long
__xchg(volatile void *ptr, unsigned long x, unsigned int size)
{
	switch (size) {
	case 4:
		return __xchg_u32(ptr, x);
#ifdef CONFIG_PPC64
	case 8:
		return __xchg_u64(ptr, x);
#endif
	}
	__xchg_called_with_bad_pointer();
	return x;
}

static __always_inline unsigned long
__xchg_local(volatile void *ptr, unsigned long x, unsigned int size)
{
	switch (size) {
	case 4:
		return __xchg_u32_local(ptr, x);
#ifdef CONFIG_PPC64
	case 8:
		return __xchg_u64_local(ptr, x);
#endif
	}
	__xchg_called_with_bad_pointer();
	return x;
}
#define xchg(ptr,x)							     \
  ({									     \
     __typeof__(*(ptr)) _x_ = (x);					     \
     (__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_, sizeof(*(ptr))); \
  })

#define xchg_local(ptr,x)						     \
  ({									     \
     __typeof__(*(ptr)) _x_ = (x);					     \
     (__typeof__(*(ptr))) __xchg_local((ptr),				     \
     		(unsigned long)_x_, sizeof(*(ptr))); 			     \
  })

/*
 * Compare and exchange - if *p == old, set it to new,
 * and return the old value of *p.
 */
#define __HAVE_ARCH_CMPXCHG	1

static __always_inline unsigned long
__cmpxchg_u32(volatile unsigned int *p, unsigned long old, unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
	LWSYNC_ON_SMP
"1:	lwarx	%0,0,%2		# __cmpxchg_u32\n\
	cmpw	0,%0,%3\n\
	bne-	2f\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%4,0,%2\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u32_local(volatile unsigned int *p, unsigned long old,
			unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
"1:	lwarx	%0,0,%2		# __cmpxchg_u32\n\
	cmpw	0,%0,%3\n\
	bne-	2f\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%4,0,%2\n\
	bne-	1b"
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

#ifdef CONFIG_PPC64
static __always_inline unsigned long
__cmpxchg_u64(volatile unsigned long *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
	LWSYNC_ON_SMP
"1:	ldarx	%0,0,%2		# __cmpxchg_u64\n\
	cmpd	0,%0,%3\n\
	bne-	2f\n\
	stdcx.	%4,0,%2\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}

static __always_inline unsigned long
__cmpxchg_u64_local(volatile unsigned long *p, unsigned long old,
			unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
"1:	ldarx	%0,0,%2		# __cmpxchg_u64\n\
	cmpd	0,%0,%3\n\
	bne-	2f\n\
	stdcx.	%4,0,%2\n\
	bne-	1b"
	"\n\
2:"
	: "=&r" (prev), "+m" (*p)
	: "r" (p), "r" (old), "r" (new)
	: "cc", "memory");

	return prev;
}
#endif

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static __always_inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new,
	  unsigned int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
#ifdef CONFIG_PPC64
	case 8:
		return __cmpxchg_u64(ptr, old, new);
#endif
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

static __always_inline unsigned long
__cmpxchg_local(volatile void *ptr, unsigned long old, unsigned long new,
	  unsigned int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32_local(ptr, old, new);
#ifdef CONFIG_PPC64
	case 8:
		return __cmpxchg_u64_local(ptr, old, new);
#endif
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, o, n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })


#define cmpxchg_local(ptr, o, n)					 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg_local((ptr), (unsigned long)_o_,	 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#ifdef CONFIG_PPC64
/*
 * We handle most unaligned accesses in hardware. On the other hand 
 * unaligned DMA can be very expensive on some ppc64 IO chips (it does
 * powers of 2 writes until it reaches sufficient alignment).
 *
 * Based on this we disable the IP header alignment in network drivers.
 * We also modify NET_SKB_PAD to be a cacheline in size, thus maintaining
 * cacheline alignment of buffers.
 */
#define NET_IP_ALIGN	0
#define NET_SKB_PAD	L1_CACHE_BYTES

#define cmpxchg64(ptr, o, n)						\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg((ptr), (o), (n));					\
  })
#define cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
  })
#else
#include <asm-generic/cmpxchg-local.h>
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))
#endif

extern unsigned long arch_align_stack(unsigned long sp);

/* Used in very early kernel initialization. */
extern unsigned long reloc_offset(void);
extern unsigned long add_reloc_offset(unsigned long);
extern void reloc_got2(unsigned long);

#define PTRRELOC(x)	((typeof(x)) add_reloc_offset((unsigned long)(x)))

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
extern void account_system_vtime(struct task_struct *);
#endif

extern struct dentry *powerpc_debugfs_root;

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_SYSTEM_H */
