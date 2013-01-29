/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_PROCESSOR_H
#define _ASM_TILE_PROCESSOR_H

#ifndef __ASSEMBLY__

/*
 * NOTE: we don't include <linux/ptrace.h> or <linux/percpu.h> as one
 * normally would, due to #include dependencies.
 */
#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/percpu.h>

#include <arch/chip.h>
#include <arch/spr_def.h>

struct task_struct;
struct thread_struct;

typedef struct {
	unsigned long seg;
} mm_segment_t;

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
void *current_text_addr(void);

#if CHIP_HAS_TILE_DMA()
/* Capture the state of a suspended DMA. */
struct tile_dma_state {
	int enabled;
	unsigned long src;
	unsigned long dest;
	unsigned long strides;
	unsigned long chunk_size;
	unsigned long src_chunk;
	unsigned long dest_chunk;
	unsigned long byte;
	unsigned long status;
};

/*
 * A mask of the DMA status register for selecting only the 'running'
 * and 'done' bits.
 */
#define DMA_STATUS_MASK \
  (SPR_DMA_STATUS__RUNNING_MASK | SPR_DMA_STATUS__DONE_MASK)
#endif

/*
 * Track asynchronous TLB events (faults and access violations)
 * that occur while we are in kernel mode from DMA or the SN processor.
 */
struct async_tlb {
	short fault_num;         /* original fault number; 0 if none */
	char is_fault;           /* was it a fault (vs an access violation) */
	char is_write;           /* for fault: was it caused by a write? */
	unsigned long address;   /* what address faulted? */
};

#ifdef CONFIG_HARDWALL
struct hardwall_info;
struct hardwall_task {
	/* Which hardwall is this task tied to? (or NULL if none) */
	struct hardwall_info *info;
	/* Chains this task into the list at info->task_head. */
	struct list_head list;
};
#ifdef __tilepro__
#define HARDWALL_TYPES 1   /* udn */
#else
#define HARDWALL_TYPES 3   /* udn, idn, and ipi */
#endif
#endif

struct thread_struct {
	/* kernel stack pointer */
	unsigned long  ksp;
	/* kernel PC */
	unsigned long  pc;
	/* starting user stack pointer (for page migration) */
	unsigned long  usp0;
	/* pid of process that created this one */
	pid_t creator_pid;
#if CHIP_HAS_TILE_DMA()
	/* DMA info for suspended threads (byte == 0 means no DMA state) */
	struct tile_dma_state tile_dma_state;
#endif
	/* User EX_CONTEXT registers */
	unsigned long ex_context[2];
	/* User SYSTEM_SAVE registers */
	unsigned long system_save[4];
	/* User interrupt mask */
	unsigned long long interrupt_mask;
	/* User interrupt-control 0 state */
	unsigned long intctrl_0;
#if CHIP_HAS_PROC_STATUS_SPR()
	/* Any other miscellaneous processor state bits */
	unsigned long proc_status;
#endif
#if !CHIP_HAS_FIXED_INTVEC_BASE()
	/* Interrupt base for PL0 interrupts */
	unsigned long interrupt_vector_base;
#endif
#if CHIP_HAS_TILE_RTF_HWM()
	/* Tile cache retry fifo high-water mark */
	unsigned long tile_rtf_hwm;
#endif
#if CHIP_HAS_DSTREAM_PF()
	/* Data stream prefetch control */
	unsigned long dstream_pf;
#endif
#ifdef CONFIG_HARDWALL
	/* Hardwall information for various resources. */
	struct hardwall_task hardwall[HARDWALL_TYPES];
#endif
#if CHIP_HAS_TILE_DMA()
	/* Async DMA TLB fault information */
	struct async_tlb dma_async_tlb;
#endif
#if CHIP_HAS_SN_PROC()
	/* Was static network processor when we were switched out? */
	int sn_proc_running;
	/* Async SNI TLB fault information */
	struct async_tlb sn_async_tlb;
#endif
};

#endif /* !__ASSEMBLY__ */

/*
 * Start with "sp" this many bytes below the top of the kernel stack.
 * This preserves the invariant that a called function may write to *sp.
 */
#define STACK_TOP_DELTA 8

/*
 * When entering the kernel via a fault, start with the top of the
 * pt_regs structure this many bytes below the top of the page.
 * This aligns the pt_regs structure optimally for cache-line access.
 */
#ifdef __tilegx__
#define KSTK_PTREGS_GAP  48
#else
#define KSTK_PTREGS_GAP  56
#endif

#ifndef __ASSEMBLY__

#ifdef __tilegx__
#define TASK_SIZE_MAX		(MEM_LOW_END + 1)
#else
#define TASK_SIZE_MAX		PAGE_OFFSET
#endif

/* TASK_SIZE and related variables are always checked in "current" context. */
#ifdef CONFIG_COMPAT
#define COMPAT_TASK_SIZE	(1UL << 31)
#define TASK_SIZE		((current_thread_info()->status & TS_COMPAT) ?\
				 COMPAT_TASK_SIZE : TASK_SIZE_MAX)
#else
#define TASK_SIZE		TASK_SIZE_MAX
#endif

/* We provide a minimal "vdso" a la x86; just the sigreturn code for now. */
#define VDSO_BASE		(TASK_SIZE - PAGE_SIZE)

#define STACK_TOP		VDSO_BASE

/* STACK_TOP_MAX is used temporarily in execve and should not check COMPAT. */
#define STACK_TOP_MAX		TASK_SIZE_MAX

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's, if it is using bottom-up mapping.
 */
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))

#define HAVE_ARCH_PICK_MMAP_LAYOUT

#define INIT_THREAD {                                                   \
	.ksp = (unsigned long)init_stack + THREAD_SIZE - STACK_TOP_DELTA, \
	.interrupt_mask = -1ULL                                         \
}

/* Kernel stack top for the task that first boots on this cpu. */
DECLARE_PER_CPU(unsigned long, boot_sp);

/* PC to boot from on this cpu. */
DECLARE_PER_CPU(unsigned long, boot_pc);

/* Do necessary setup to start up a newly executed thread. */
static inline void start_thread(struct pt_regs *regs,
				unsigned long pc, unsigned long usp)
{
	regs->pc = pc;
	regs->sp = usp;
	single_step_execve();
}

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
	/* Nothing for now */
}

extern int do_work_pending(struct pt_regs *regs, u32 flags);


/*
 * Return saved (kernel) PC of a blocked thread.
 * Only used in a printk() in kernel/sched.c, so don't work too hard.
 */
#define thread_saved_pc(t)   ((t)->thread.pc)

unsigned long get_wchan(struct task_struct *p);

/* Return initial ksp value for given task. */
#define task_ksp0(task) ((unsigned long)(task)->stack + THREAD_SIZE)

/* Return some info about the user process TASK. */
#define KSTK_TOP(task)	(task_ksp0(task) - STACK_TOP_DELTA)
#define task_pt_regs(task) \
  ((struct pt_regs *)(task_ksp0(task) - KSTK_PTREGS_GAP) - 1)
#define current_pt_regs()                                   \
  ((struct pt_regs *)((stack_pointer | (THREAD_SIZE - 1)) - \
                      (KSTK_PTREGS_GAP - 1)) - 1)
#define task_sp(task)	(task_pt_regs(task)->sp)
#define task_pc(task)	(task_pt_regs(task)->pc)
/* Aliases for pc and sp (used in fs/proc/array.c) */
#define KSTK_EIP(task)	task_pc(task)
#define KSTK_ESP(task)	task_sp(task)

/* Standard format for printing registers and other word-size data. */
#ifdef __tilegx__
# define REGFMT "0x%016lx"
#else
# define REGFMT "0x%08lx"
#endif

/*
 * Do some slow action (e.g. read a slow SPR).
 * Note that this must also have compiler-barrier semantics since
 * it may be used in a busy loop reading memory.
 */
static inline void cpu_relax(void)
{
	__insn_mfspr(SPR_PASS);
	barrier();
}

/* Info on this processor (see fs/proc/cpuinfo.c) */
struct seq_operations;
extern const struct seq_operations cpuinfo_op;

/* Provide information about the chip model. */
extern char chip_model[64];

/* Data on which physical memory controller corresponds to which NUMA node. */
extern int node_controller[];

#if CHIP_HAS_CBOX_HOME_MAP()
/* Does the heap allocator return hash-for-home pages by default? */
extern int hash_default;

/* Should kernel stack pages be hash-for-home? */
extern int kstack_hash;

/* Does MAP_ANONYMOUS return hash-for-home pages by default? */
#define uheap_hash hash_default

#else
#define hash_default 0
#define kstack_hash 0
#define uheap_hash 0
#endif

/* Are we using huge pages in the TLB for kernel data? */
extern int kdata_huge;

/* Support standard Linux prefetching. */
#define ARCH_HAS_PREFETCH
#define prefetch(x) __builtin_prefetch(x)
#define PREFETCH_STRIDE CHIP_L2_LINE_SIZE()

/* Bring a value into the L1D, faulting the TLB if necessary. */
#ifdef __tilegx__
#define prefetch_L1(x) __insn_prefetch_l1_fault((void *)(x))
#else
#define prefetch_L1(x) __insn_prefetch_L1((void *)(x))
#endif

#else /* __ASSEMBLY__ */

/* Do some slow action (e.g. read a slow SPR). */
#define CPU_RELAX       mfspr zero, SPR_PASS

#endif /* !__ASSEMBLY__ */

/* Assembly code assumes that the PL is in the low bits. */
#if SPR_EX_CONTEXT_1_1__PL_SHIFT != 0
# error Fix assembly assumptions about PL
#endif

/* We sometimes use these macros for EX_CONTEXT_0_1 as well. */
#if SPR_EX_CONTEXT_1_1__PL_SHIFT != SPR_EX_CONTEXT_0_1__PL_SHIFT || \
    SPR_EX_CONTEXT_1_1__PL_RMASK != SPR_EX_CONTEXT_0_1__PL_RMASK || \
    SPR_EX_CONTEXT_1_1__ICS_SHIFT != SPR_EX_CONTEXT_0_1__ICS_SHIFT || \
    SPR_EX_CONTEXT_1_1__ICS_RMASK != SPR_EX_CONTEXT_0_1__ICS_RMASK
# error Fix assumptions that EX1 macros work for both PL0 and PL1
#endif

/* Allow pulling apart and recombining the PL and ICS bits in EX_CONTEXT. */
#define EX1_PL(ex1) \
  (((ex1) >> SPR_EX_CONTEXT_1_1__PL_SHIFT) & SPR_EX_CONTEXT_1_1__PL_RMASK)
#define EX1_ICS(ex1) \
  (((ex1) >> SPR_EX_CONTEXT_1_1__ICS_SHIFT) & SPR_EX_CONTEXT_1_1__ICS_RMASK)
#define PL_ICS_EX1(pl, ics) \
  (((pl) << SPR_EX_CONTEXT_1_1__PL_SHIFT) | \
   ((ics) << SPR_EX_CONTEXT_1_1__ICS_SHIFT))

/*
 * Provide symbolic constants for PLs.
 * Note that assembly code assumes that USER_PL is zero.
 */
#define USER_PL 0
#if CONFIG_KERNEL_PL == 2
#define GUEST_PL 1
#endif
#define KERNEL_PL CONFIG_KERNEL_PL

/* SYSTEM_SAVE_K_0 holds the current cpu number ORed with ksp0. */
#define CPU_LOG_MASK_VALUE 12
#define CPU_MASK_VALUE ((1 << CPU_LOG_MASK_VALUE) - 1)
#if CONFIG_NR_CPUS > CPU_MASK_VALUE
# error Too many cpus!
#endif
#define raw_smp_processor_id() \
	((int)__insn_mfspr(SPR_SYSTEM_SAVE_K_0) & CPU_MASK_VALUE)
#define get_current_ksp0() \
	(__insn_mfspr(SPR_SYSTEM_SAVE_K_0) & ~CPU_MASK_VALUE)
#define next_current_ksp0(task) ({ \
	unsigned long __ksp0 = task_ksp0(task); \
	int __cpu = raw_smp_processor_id(); \
	BUG_ON(__ksp0 & CPU_MASK_VALUE); \
	__ksp0 | __cpu; \
})

#endif /* _ASM_TILE_PROCESSOR_H */
