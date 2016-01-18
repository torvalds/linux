/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/processor.h"
 *    Copyright (C) 1994, Linus Torvalds
 */

#ifndef __ASM_S390_PROCESSOR_H
#define __ASM_S390_PROCESSOR_H

#include <linux/const.h>

#define CIF_MCCK_PENDING	0	/* machine check handling is pending */
#define CIF_ASCE		1	/* user asce needs fixup / uaccess */
#define CIF_NOHZ_DELAY		2	/* delay HZ disable for a tick */
#define CIF_FPU			3	/* restore FPU registers */
#define CIF_IGNORE_IRQ		4	/* ignore interrupt (for udelay) */
#define CIF_ENABLED_WAIT	5	/* in enabled wait state */

#define _CIF_MCCK_PENDING	_BITUL(CIF_MCCK_PENDING)
#define _CIF_ASCE		_BITUL(CIF_ASCE)
#define _CIF_NOHZ_DELAY		_BITUL(CIF_NOHZ_DELAY)
#define _CIF_FPU		_BITUL(CIF_FPU)
#define _CIF_IGNORE_IRQ		_BITUL(CIF_IGNORE_IRQ)
#define _CIF_ENABLED_WAIT	_BITUL(CIF_ENABLED_WAIT)

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/irqflags.h>
#include <asm/cpu.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/setup.h>
#include <asm/runtime_instr.h>
#include <asm/fpu/types.h>
#include <asm/fpu/internal.h>

static inline void set_cpu_flag(int flag)
{
	S390_lowcore.cpu_flags |= (1UL << flag);
}

static inline void clear_cpu_flag(int flag)
{
	S390_lowcore.cpu_flags &= ~(1UL << flag);
}

static inline int test_cpu_flag(int flag)
{
	return !!(S390_lowcore.cpu_flags & (1UL << flag));
}

/*
 * Test CIF flag of another CPU. The caller needs to ensure that
 * CPU hotplug can not happen, e.g. by disabling preemption.
 */
static inline int test_cpu_flag_of(int flag, int cpu)
{
	struct lowcore *lc = lowcore_ptr[cpu];
	return !!(lc->cpu_flags & (1UL << flag));
}

#define arch_needs_cpu() test_cpu_flag(CIF_NOHZ_DELAY)

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; asm("basr %0,0" : "=a" (pc)); pc; })

static inline void get_cpu_id(struct cpuid *ptr)
{
	asm volatile("stidp %0" : "=Q" (*ptr));
}

extern void s390_adjust_jiffies(void);
extern const struct seq_operations cpuinfo_op;
extern int sysctl_ieee_emulation_warnings;
extern void execve_tail(void);

/*
 * User space process size: 2GB for 31 bit, 4TB or 8PT for 64 bit.
 */

#define TASK_SIZE_OF(tsk)	((tsk)->mm->context.asce_limit)
#define TASK_UNMAPPED_BASE	(test_thread_flag(TIF_31BIT) ? \
					(1UL << 30) : (1UL << 41))
#define TASK_SIZE		TASK_SIZE_OF(current)
#define TASK_MAX_SIZE		(1UL << 53)

#define STACK_TOP		(1UL << (test_thread_flag(TIF_31BIT) ? 31:42))
#define STACK_TOP_MAX		(1UL << 42)

#define HAVE_ARCH_PICK_MMAP_LAYOUT

typedef struct {
        __u32 ar4;
} mm_segment_t;

/*
 * Thread structure
 */
struct thread_struct {
	struct fpu fpu;			/* FP and VX register save area */
	unsigned int  acrs[NUM_ACRS];
        unsigned long ksp;              /* kernel stack pointer             */
	mm_segment_t mm_segment;
	unsigned long gmap_addr;	/* address of last gmap fault. */
	unsigned int gmap_pfault;	/* signal of a pending guest pfault */
	struct per_regs per_user;	/* User specified PER registers */
	struct per_event per_event;	/* Cause of the last PER trap */
	unsigned long per_flags;	/* Flags to control debug behavior */
        /* pfault_wait is used to block the process on a pfault event */
	unsigned long pfault_wait;
	struct list_head list;
	/* cpu runtime instrumentation */
	struct runtime_instr_cb *ri_cb;
	unsigned char trap_tdb[256];	/* Transaction abort diagnose block */
};

/* Flag to disable transactions. */
#define PER_FLAG_NO_TE			1UL
/* Flag to enable random transaction aborts. */
#define PER_FLAG_TE_ABORT_RAND		2UL
/* Flag to specify random transaction abort mode:
 * - abort each transaction at a random instruction before TEND if set.
 * - abort random transactions at a random instruction if cleared.
 */
#define PER_FLAG_TE_ABORT_RAND_TEND	4UL

typedef struct thread_struct thread_struct;

/*
 * Stack layout of a C stack frame.
 */
#ifndef __PACK_STACK
struct stack_frame {
	unsigned long back_chain;
	unsigned long empty1[5];
	unsigned long gprs[10];
	unsigned int  empty2[8];
};
#else
struct stack_frame {
	unsigned long empty1[5];
	unsigned int  empty2[8];
	unsigned long gprs[10];
	unsigned long back_chain;
};
#endif

#define ARCH_MIN_TASKALIGN	8

extern __vector128 init_task_fpu_regs[__NUM_VXRS];
#define INIT_THREAD {							\
	.ksp = sizeof(init_stack) + (unsigned long) &init_stack,	\
	.fpu.regs = (void *)&init_task_fpu_regs,			\
}

/*
 * Do necessary setup to start up a new thread.
 */
#define start_thread(regs, new_psw, new_stackp) do {			\
	regs->psw.mask	= PSW_USER_BITS | PSW_MASK_EA | PSW_MASK_BA;	\
	regs->psw.addr	= new_psw;					\
	regs->gprs[15]	= new_stackp;					\
	execve_tail();							\
} while (0)

#define start_thread31(regs, new_psw, new_stackp) do {			\
	regs->psw.mask	= PSW_USER_BITS | PSW_MASK_BA;			\
	regs->psw.addr	= new_psw;					\
	regs->gprs[15]	= new_stackp;					\
	crst_table_downgrade(current->mm, 1UL << 31);			\
	execve_tail();							\
} while (0)

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;
struct seq_file;

void show_cacheinfo(struct seq_file *m);

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/*
 * Return saved PC of a blocked thread.
 */
extern unsigned long thread_saved_pc(struct task_struct *t);

unsigned long get_wchan(struct task_struct *p);
#define task_pt_regs(tsk) ((struct pt_regs *) \
        (task_stack_page(tsk) + THREAD_SIZE) - 1)
#define KSTK_EIP(tsk)	(task_pt_regs(tsk)->psw.addr)
#define KSTK_ESP(tsk)	(task_pt_regs(tsk)->gprs[15])

/* Has task runtime instrumentation enabled ? */
#define is_ri_task(tsk) (!!(tsk)->thread.ri_cb)

static inline unsigned short stap(void)
{
	unsigned short cpu_address;

	asm volatile("stap %0" : "=m" (cpu_address));
	return cpu_address;
}

/*
 * Give up the time slice of the virtual PU.
 */
void cpu_relax(void);

#define cpu_relax_lowlatency()  barrier()

static inline void psw_set_key(unsigned int key)
{
	asm volatile("spka 0(%0)" : : "d" (key));
}

/*
 * Set PSW to specified value.
 */
static inline void __load_psw(psw_t psw)
{
	asm volatile("lpswe %0" : : "Q" (psw) : "cc");
}

/*
 * Set PSW mask to specified value, while leaving the
 * PSW addr pointing to the next instruction.
 */
static inline void __load_psw_mask(unsigned long mask)
{
	unsigned long addr;
	psw_t psw;

	psw.mask = mask;

	asm volatile(
		"	larl	%0,1f\n"
		"	stg	%0,%O1+8(%R1)\n"
		"	lpswe	%1\n"
		"1:"
		: "=&d" (addr), "=Q" (psw) : "Q" (psw) : "memory", "cc");
}

/*
 * Extract current PSW mask
 */
static inline unsigned long __extract_psw(void)
{
	unsigned int reg1, reg2;

	asm volatile("epsw %0,%1" : "=d" (reg1), "=a" (reg2));
	return (((unsigned long) reg1) << 32) | ((unsigned long) reg2);
}

static inline void local_mcck_enable(void)
{
	__load_psw_mask(__extract_psw() | PSW_MASK_MCHECK);
}

static inline void local_mcck_disable(void)
{
	__load_psw_mask(__extract_psw() & ~PSW_MASK_MCHECK);
}

/*
 * Rewind PSW instruction address by specified number of bytes.
 */
static inline unsigned long __rewind_psw(psw_t psw, unsigned long ilc)
{
	unsigned long mask;

	mask = (psw.mask & PSW_MASK_EA) ? -1UL :
	       (psw.mask & PSW_MASK_BA) ? (1UL << 31) - 1 :
					  (1UL << 24) - 1;
	return (psw.addr - ilc) & mask;
}

/*
 * Function to stop a processor until the next interrupt occurs
 */
void enabled_wait(void);

/*
 * Function to drop a processor into disabled wait state
 */
static inline void __noreturn disabled_wait(unsigned long code)
{
	psw_t psw;

	psw.mask = PSW_MASK_BASE | PSW_MASK_WAIT | PSW_MASK_BA | PSW_MASK_EA;
	psw.addr = code;
	__load_psw(psw);
	while (1);
}

/*
 * Basic Machine Check/Program Check Handler.
 */

extern void s390_base_mcck_handler(void);
extern void s390_base_pgm_handler(void);
extern void s390_base_ext_handler(void);

extern void (*s390_base_mcck_handler_fn)(void);
extern void (*s390_base_pgm_handler_fn)(void);
extern void (*s390_base_ext_handler_fn)(void);

#define ARCH_LOW_ADDRESS_LIMIT	0x7fffffffUL

extern int memcpy_real(void *, void *, size_t);
extern void memcpy_absolute(void *, void *, size_t);

#define mem_assign_absolute(dest, val) {			\
	__typeof__(dest) __tmp = (val);				\
								\
	BUILD_BUG_ON(sizeof(__tmp) != sizeof(val));		\
	memcpy_absolute(&(dest), &__tmp, sizeof(__tmp));	\
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_S390_PROCESSOR_H */
