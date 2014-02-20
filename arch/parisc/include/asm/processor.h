/*
 * include/asm-parisc/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 * Copyright (C) 2001 Grant Grundler
 */

#ifndef __ASM_PARISC_PROCESSOR_H
#define __ASM_PARISC_PROCESSOR_H

#ifndef __ASSEMBLY__
#include <linux/threads.h>

#include <asm/prefetch.h>
#include <asm/hardware.h>
#include <asm/pdc.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/percpu.h>
#endif /* __ASSEMBLY__ */

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#ifdef CONFIG_PA20
#define current_ia(x)	__asm__("mfia %0" : "=r"(x))
#else /* mfia added in pa2.0 */
#define current_ia(x)	__asm__("blr 0,%0\n\tnop" : "=r"(x))
#endif
#define current_text_addr() ({ void *pc; current_ia(pc); pc; })

#define HAVE_ARCH_PICK_MMAP_LAYOUT

#define TASK_SIZE_OF(tsk)       ((tsk)->thread.task_size)
#define TASK_SIZE	        TASK_SIZE_OF(current)
#define TASK_UNMAPPED_BASE      (current->thread.map_base)

#define DEFAULT_TASK_SIZE32	(0xFFF00000UL)
#define DEFAULT_MAP_BASE32	(0x40000000UL)

#ifdef CONFIG_64BIT
#define DEFAULT_TASK_SIZE       (MAX_ADDRESS-0xf000000)
#define DEFAULT_MAP_BASE        (0x200000000UL)
#else
#define DEFAULT_TASK_SIZE	DEFAULT_TASK_SIZE32
#define DEFAULT_MAP_BASE	DEFAULT_MAP_BASE32
#endif

#ifdef __KERNEL__

/* XXX: STACK_TOP actually should be STACK_BOTTOM for parisc.
 * prumpf */

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	DEFAULT_TASK_SIZE

#endif

#ifndef __ASSEMBLY__

/*
 * Data detected about CPUs at boot time which is the same for all CPU's.
 * HP boxes are SMP - ie identical processors.
 *
 * FIXME: some CPU rev info may be processor specific...
 */
struct system_cpuinfo_parisc {
	unsigned int	cpu_count;
	unsigned int	cpu_hz;
	unsigned int	hversion;
	unsigned int	sversion;
	enum cpu_type	cpu_type;

	struct {
		struct pdc_model model;
		unsigned long versions;
		unsigned long cpuid;
		unsigned long capabilities;
		char   sys_model_name[81]; /* PDC-ROM returnes this model name */
	} pdc;

	const char	*cpu_name;	/* e.g. "PA7300LC (PCX-L2)" */
	const char	*family_name;	/* e.g. "1.1e" */
};


/* Per CPU data structure - ie varies per CPU.  */
struct cpuinfo_parisc {
	unsigned long it_value;     /* Interval Timer at last timer Intr */
	unsigned long it_delta;     /* Interval delta (tic_10ms / HZ * 100) */
	unsigned long irq_count;    /* number of IRQ's since boot */
	unsigned long irq_max_cr16; /* longest time to handle a single IRQ */
	unsigned long cpuid;        /* aka slot_number or set to NO_PROC_ID */
	unsigned long hpa;          /* Host Physical address */
	unsigned long txn_addr;     /* MMIO addr of EIR or id_eid */
#ifdef CONFIG_SMP
	unsigned long pending_ipi;  /* bitmap of type ipi_message_type */
#endif
	unsigned long bh_count;     /* number of times bh was invoked */
	unsigned long prof_counter; /* per CPU profiling support */
	unsigned long prof_multiplier;	/* per CPU profiling support */
	unsigned long fp_rev;
	unsigned long fp_model;
	unsigned int state;
	struct parisc_device *dev;
	unsigned long loops_per_jiffy;
};

extern struct system_cpuinfo_parisc boot_cpu_data;
DECLARE_PER_CPU(struct cpuinfo_parisc, cpu_data);

#define CPU_HVERSION ((boot_cpu_data.hversion >> 4) & 0x0FFF)

typedef struct {
	int seg;  
} mm_segment_t;

#define ARCH_MIN_TASKALIGN	8

struct thread_struct {
	struct pt_regs regs;
	unsigned long  task_size;
	unsigned long  map_base;
	unsigned long  flags;
}; 

#define task_pt_regs(tsk) ((struct pt_regs *)&((tsk)->thread.regs))

/* Thread struct flags. */
#define PARISC_UAC_NOPRINT	(1UL << 0)	/* see prctl and unaligned.c */
#define PARISC_UAC_SIGBUS	(1UL << 1)
#define PARISC_KERNEL_DEATH	(1UL << 31)	/* see die_if_kernel()... */

#define PARISC_UAC_SHIFT	0
#define PARISC_UAC_MASK		(PARISC_UAC_NOPRINT|PARISC_UAC_SIGBUS)

#define SET_UNALIGN_CTL(task,value)                                       \
        ({                                                                \
        (task)->thread.flags = (((task)->thread.flags & ~PARISC_UAC_MASK) \
                                | (((value) << PARISC_UAC_SHIFT) &        \
                                   PARISC_UAC_MASK));                     \
        0;                                                                \
        })

#define GET_UNALIGN_CTL(task,addr)                                        \
        ({                                                                \
        put_user(((task)->thread.flags & PARISC_UAC_MASK)                 \
                 >> PARISC_UAC_SHIFT, (int __user *) (addr));             \
        })

#define INIT_THREAD { \
	.regs = {	.gr	= { 0, }, \
			.fr	= { 0, }, \
			.sr	= { 0, }, \
			.iasq	= { 0, }, \
			.iaoq	= { 0, }, \
			.cr27	= 0, \
		}, \
	.task_size	= DEFAULT_TASK_SIZE, \
	.map_base	= DEFAULT_MAP_BASE, \
	.flags		= 0 \
	}

/*
 * Return saved PC of a blocked thread.  This is used by ps mostly.
 */

struct task_struct;
unsigned long thread_saved_pc(struct task_struct *t);
void show_trace(struct task_struct *task, unsigned long *stack);

/*
 * Start user thread in another space.
 *
 * Note that we set both the iaoq and r31 to the new pc. When
 * the kernel initially calls execve it will return through an
 * rfi path that will use the values in the iaoq. The execve
 * syscall path will return through the gateway page, and
 * that uses r31 to branch to.
 *
 * For ELF we clear r23, because the dynamic linker uses it to pass
 * the address of the finalizer function.
 *
 * We also initialize sr3 to an illegal value (illegal for our
 * implementation, not for the architecture).
 */
typedef unsigned int elf_caddr_t;

#define start_thread_som(regs, new_pc, new_sp) do {	\
	unsigned long *sp = (unsigned long *)new_sp;	\
	__u32 spaceid = (__u32)current->mm->context;	\
	unsigned long pc = (unsigned long)new_pc;	\
	/* offset pc for priv. level */			\
	pc |= 3;					\
							\
	regs->iasq[0] = spaceid;			\
	regs->iasq[1] = spaceid;			\
	regs->iaoq[0] = pc;				\
	regs->iaoq[1] = pc + 4;                         \
	regs->sr[2] = LINUX_GATEWAY_SPACE;              \
	regs->sr[3] = 0xffff;				\
	regs->sr[4] = spaceid;				\
	regs->sr[5] = spaceid;				\
	regs->sr[6] = spaceid;				\
	regs->sr[7] = spaceid;				\
	regs->gr[ 0] = USER_PSW;                        \
	regs->gr[30] = ((new_sp)+63)&~63;		\
	regs->gr[31] = pc;				\
							\
	get_user(regs->gr[26],&sp[0]);			\
	get_user(regs->gr[25],&sp[-1]); 		\
	get_user(regs->gr[24],&sp[-2]); 		\
	get_user(regs->gr[23],&sp[-3]); 		\
} while(0)

/* The ELF abi wants things done a "wee bit" differently than
 * som does.  Supporting this behavior here avoids
 * having our own version of create_elf_tables.
 *
 * Oh, and yes, that is not a typo, we are really passing argc in r25
 * and argv in r24 (rather than r26 and r25).  This is because that's
 * where __libc_start_main wants them.
 *
 * Duplicated from dl-machine.h for the benefit of readers:
 *
 *  Our initial stack layout is rather different from everyone else's
 *  due to the unique PA-RISC ABI.  As far as I know it looks like
 *  this:

   -----------------------------------  (user startup code creates this frame)
   |         32 bytes of magic       |
   |---------------------------------|
   | 32 bytes argument/sp save area  |
   |---------------------------------| (bprm->p)
   |	    ELF auxiliary info	     |
   |         (up to 28 words)        |
   |---------------------------------|
   |		   NULL		     |
   |---------------------------------|
   |	   Environment pointers	     |
   |---------------------------------|
   |		   NULL		     |
   |---------------------------------|
   |        Argument pointers        |
   |---------------------------------| <- argv
   |          argc (1 word)          |
   |---------------------------------| <- bprm->exec (HACK!)
   |         N bytes of slack        |
   |---------------------------------|
   |	filename passed to execve    |
   |---------------------------------| (mm->env_end)
   |           env strings           |
   |---------------------------------| (mm->env_start, mm->arg_end)
   |           arg strings           |
   |---------------------------------|
   | additional faked arg strings if |
   | we're invoked via binfmt_script |
   |---------------------------------| (mm->arg_start)
   stack base is at TASK_SIZE - rlim_max.

on downward growing arches, it looks like this:
   stack base at TASK_SIZE
   | filename passed to execve
   | env strings
   | arg strings
   | faked arg strings
   | slack
   | ELF
   | envps
   | argvs
   | argc

 *  The pleasant part of this is that if we need to skip arguments we
 *  can just decrement argc and move argv, because the stack pointer
 *  is utterly unrelated to the location of the environment and
 *  argument vectors.
 *
 * Note that the S/390 people took the easy way out and hacked their
 * GCC to make the stack grow downwards.
 *
 * Final Note: For entry from syscall, the W (wide) bit of the PSW
 * is stuffed into the lowest bit of the user sp (%r30), so we fill
 * it in here from the current->personality
 */

#ifdef CONFIG_64BIT
#define USER_WIDE_MODE	(!test_thread_flag(TIF_32BIT))
#else
#define USER_WIDE_MODE	0
#endif

#define start_thread(regs, new_pc, new_sp) do {		\
	elf_addr_t *sp = (elf_addr_t *)new_sp;		\
	__u32 spaceid = (__u32)current->mm->context;	\
	elf_addr_t pc = (elf_addr_t)new_pc | 3;		\
	elf_caddr_t *argv = (elf_caddr_t *)bprm->exec + 1;	\
							\
	regs->iasq[0] = spaceid;			\
	regs->iasq[1] = spaceid;			\
	regs->iaoq[0] = pc;				\
	regs->iaoq[1] = pc + 4;                         \
	regs->sr[2] = LINUX_GATEWAY_SPACE;              \
	regs->sr[3] = 0xffff;				\
	regs->sr[4] = spaceid;				\
	regs->sr[5] = spaceid;				\
	regs->sr[6] = spaceid;				\
	regs->sr[7] = spaceid;				\
	regs->gr[ 0] = USER_PSW | (USER_WIDE_MODE ? PSW_W : 0); \
	regs->fr[ 0] = 0LL;                            	\
	regs->fr[ 1] = 0LL;                            	\
	regs->fr[ 2] = 0LL;                            	\
	regs->fr[ 3] = 0LL;                            	\
	regs->gr[30] = (((unsigned long)sp + 63) &~ 63) | (USER_WIDE_MODE ? 1 : 0); \
	regs->gr[31] = pc;				\
							\
	get_user(regs->gr[25], (argv - 1));		\
	regs->gr[24] = (long) argv;			\
	regs->gr[23] = 0;				\
} while(0)

struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

extern void map_hpux_gateway_page(struct task_struct *tsk, struct mm_struct *mm);

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)	((tsk)->thread.regs.iaoq[0])
#define KSTK_ESP(tsk)	((tsk)->thread.regs.gr[30])

#define cpu_relax()	barrier()

/* Used as a macro to identify the combined VIPT/PIPT cached
 * CPUs which require a guarantee of coherency (no inequivalent
 * aliases with different data, whether clean or not) to operate */
static inline int parisc_requires_coherency(void)
{
#ifdef CONFIG_PA8X00
	return (boot_cpu_data.cpu_type == mako) ||
		(boot_cpu_data.cpu_type == mako2);
#else
	return 0;
#endif
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_PARISC_PROCESSOR_H */
