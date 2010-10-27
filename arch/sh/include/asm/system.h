#ifndef __ASM_SH_SYSTEM_H
#define __ASM_SH_SYSTEM_H

/*
 * Copyright (C) 1999, 2000  Niibe Yutaka  &  Kaz Kojima
 * Copyright (C) 2002 Paul Mundt
 */

#include <linux/irqflags.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <asm/types.h>

#define AT_VECTOR_SIZE_ARCH 5 /* entries in ARCH_DLINFO */

/*
 * A brief note on ctrl_barrier(), the control register write barrier.
 *
 * Legacy SH cores typically require a sequence of 8 nops after
 * modification of a control register in order for the changes to take
 * effect. On newer cores (like the sh4a and sh5) this is accomplished
 * with icbi.
 *
 * Also note that on sh4a in the icbi case we can forego a synco for the
 * write barrier, as it's not necessary for control registers.
 *
 * Historically we have only done this type of barrier for the MMUCR, but
 * it's also necessary for the CCR, so we make it generic here instead.
 */
#if defined(CONFIG_CPU_SH4A) || defined(CONFIG_CPU_SH5)
#define mb()		__asm__ __volatile__ ("synco": : :"memory")
#define rmb()		mb()
#define wmb()		__asm__ __volatile__ ("synco": : :"memory")
#define ctrl_barrier()	__icbi(PAGE_OFFSET)
#define read_barrier_depends()	do { } while(0)
#else
#define mb()		__asm__ __volatile__ ("": : :"memory")
#define rmb()		mb()
#define wmb()		__asm__ __volatile__ ("": : :"memory")
#define ctrl_barrier()	__asm__ __volatile__ ("nop;nop;nop;nop;nop;nop;nop;nop")
#define read_barrier_depends()	do { } while(0)
#endif

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#endif

#define set_mb(var, value) do { (void)xchg(&var, value); } while (0)

#ifdef CONFIG_GUSA_RB
#include <asm/cmpxchg-grb.h>
#elif defined(CONFIG_CPU_SH4A)
#include <asm/cmpxchg-llsc.h>
#else
#include <asm/cmpxchg-irq.h>
#endif

extern void __xchg_called_with_bad_pointer(void);

#define __xchg(ptr, x, size)				\
({							\
	unsigned long __xchg__res;			\
	volatile void *__xchg_ptr = (ptr);		\
	switch (size) {					\
	case 4:						\
		__xchg__res = xchg_u32(__xchg_ptr, x);	\
		break;					\
	case 1:						\
		__xchg__res = xchg_u8(__xchg_ptr, x);	\
		break;					\
	default:					\
		__xchg_called_with_bad_pointer();	\
		__xchg__res = x;			\
		break;					\
	}						\
							\
	__xchg__res;					\
})

#define xchg(ptr,x)	\
	((__typeof__(*(ptr)))__xchg((ptr),(unsigned long)(x), sizeof(*(ptr))))

/* This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg(). */
extern void __cmpxchg_called_with_bad_pointer(void);

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long __cmpxchg(volatile void * ptr, unsigned long old,
		unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

struct pt_regs;

extern void die(const char *str, struct pt_regs *regs, long err) __attribute__ ((noreturn));
void free_initmem(void);
void free_initrd_mem(unsigned long start, unsigned long end);

extern void *set_exception_table_vec(unsigned int vec, void *handler);

static inline void *set_exception_table_evt(unsigned int evt, void *handler)
{
	return set_exception_table_vec(evt >> 5, handler);
}

/*
 * SH-2A has both 16 and 32-bit opcodes, do lame encoding checks.
 */
#ifdef CONFIG_CPU_SH2A
extern unsigned int instruction_size(unsigned int insn);
#elif defined(CONFIG_SUPERH32)
#define instruction_size(insn)	(2)
#else
#define instruction_size(insn)	(4)
#endif

extern unsigned long cached_to_uncached;
extern unsigned long uncached_size;

extern struct dentry *sh_debugfs_root;

void per_cpu_trap_init(void);
void default_idle(void);
void cpu_idle_wait(void);
void stop_this_cpu(void *);

#ifdef CONFIG_SUPERH32
#define BUILD_TRAP_HANDLER(name)					\
asmlinkage void name##_trap_handler(unsigned long r4, unsigned long r5,	\
				    unsigned long r6, unsigned long r7,	\
				    struct pt_regs __regs)

#define TRAP_HANDLER_DECL				\
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);	\
	unsigned int vec = regs->tra;			\
	(void)vec;
#else
#define BUILD_TRAP_HANDLER(name)	\
asmlinkage void name##_trap_handler(unsigned int vec, struct pt_regs *regs)
#define TRAP_HANDLER_DECL
#endif

BUILD_TRAP_HANDLER(address_error);
BUILD_TRAP_HANDLER(debug);
BUILD_TRAP_HANDLER(bug);
BUILD_TRAP_HANDLER(breakpoint);
BUILD_TRAP_HANDLER(singlestep);
BUILD_TRAP_HANDLER(fpu_error);
BUILD_TRAP_HANDLER(fpu_state_restore);
BUILD_TRAP_HANDLER(nmi);

#define arch_align_stack(x) (x)

struct mem_access {
	unsigned long (*from)(void *dst, const void __user *src, unsigned long cnt);
	unsigned long (*to)(void __user *dst, const void *src, unsigned long cnt);
};

#ifdef CONFIG_SUPERH32
# include "system_32.h"
#else
# include "system_64.h"
#endif

#endif
