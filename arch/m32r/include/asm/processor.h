#ifndef _ASM_M32R_PROCESSOR_H
#define _ASM_M32R_PROCESSOR_H

/*
 * include/asm-m32r/processor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994  Linus Torvalds
 * Copyright (C) 2001  Hiroyuki Kondo, Hirokazu Takata, and Hitoshi Yamamoto
 * Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/kernel.h>
#include <asm/cache.h>
#include <asm/ptrace.h>  /* pt_regs */

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

struct cpuinfo_m32r {
	unsigned long pgtable_cache_sz;
	unsigned long cpu_clock;
	unsigned long bus_clock;
	unsigned long timer_divide;
	unsigned long loops_per_jiffy;
};

/*
 * capabilities of CPUs
 */

extern struct cpuinfo_m32r boot_cpu_data;

#ifdef CONFIG_SMP
extern struct cpuinfo_m32r cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

/*
 * User space process size: 2GB (default).
 */
#ifdef CONFIG_MMU
#define TASK_SIZE  (0x80000000UL)
#else
#define TASK_SIZE  (0x00400000UL)
#endif

#ifdef __KERNEL__
#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP
#endif

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	PAGE_ALIGN(TASK_SIZE / 3)

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define MAX_TRAPS 10

struct debug_trap {
	int nr_trap;
	unsigned long	addr[MAX_TRAPS];
	unsigned long	insn[MAX_TRAPS];
};

struct thread_struct {
	unsigned long address;
	unsigned long trap_no;		/* Trap number  */
	unsigned long error_code;	/* Error code of trap */
	unsigned long lr;		/* saved pc */
	unsigned long sp;		/* user stack pointer */
	struct debug_trap debug_trap;
};

#define INIT_SP	(sizeof(init_stack) + (unsigned long) &init_stack)

#define INIT_THREAD	{	\
	.sp = INIT_SP,		\
}

/*
 * Do necessary setup to start up a newly executed thread.
 */

/* User process Backup PSW */
#define USERPS_BPSW (M32R_PSW_BSM|M32R_PSW_BIE|M32R_PSW_BPM)

#define start_thread(regs, new_pc, new_spu) 				\
	do {								\
		regs->psw = (regs->psw | USERPS_BPSW) & 0x0000FFFFUL;	\
		regs->bpc = new_pc;					\
		regs->spu = new_spu;					\
	} while (0)

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Copy and release all segment info associated with a VM */
extern void copy_segments(struct task_struct *p, struct mm_struct * mm);
extern void release_segments(struct mm_struct * mm);

extern unsigned long thread_saved_pc(struct task_struct *);

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)  do { } while (0)
#define release_segments(mm)  do { } while (0)

unsigned long get_wchan(struct task_struct *p);
#define KSTK_EIP(tsk)  ((tsk)->thread.lr)
#define KSTK_ESP(tsk)  ((tsk)->thread.sp)

#define cpu_relax()	barrier()
#define cpu_relax_lowlatency() cpu_relax()

#endif /* _ASM_M32R_PROCESSOR_H */
