/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_PROCESSOR_H
#define __ASM_AVR32_PROCESSOR_H

#include <asm/page.h>
#include <asm/cache.h>

#define TASK_SIZE	0x80000000

#ifdef __KERNEL__
#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP
#endif

#ifndef __ASSEMBLY__

static inline void *current_text_addr(void)
{
	register void *pc asm("pc");
	return pc;
}

enum arch_type {
	ARCH_AVR32A,
	ARCH_AVR32B,
	ARCH_MAX
};

enum cpu_type {
	CPU_MORGAN,
	CPU_AT32AP,
	CPU_MAX
};

enum tlb_config {
	TLB_NONE,
	TLB_SPLIT,
	TLB_UNIFIED,
	TLB_INVALID
};

#define AVR32_FEATURE_RMW	(1 << 0)
#define AVR32_FEATURE_DSP	(1 << 1)
#define AVR32_FEATURE_SIMD	(1 << 2)
#define AVR32_FEATURE_OCD	(1 << 3)
#define AVR32_FEATURE_PCTR	(1 << 4)
#define AVR32_FEATURE_JAVA	(1 << 5)
#define AVR32_FEATURE_FPU	(1 << 6)

struct avr32_cpuinfo {
	struct clk *clk;
	unsigned long loops_per_jiffy;
	enum arch_type arch_type;
	enum cpu_type cpu_type;
	unsigned short arch_revision;
	unsigned short cpu_revision;
	enum tlb_config tlb_config;
	unsigned long features;
	u32 device_id;

	struct cache_info icache;
	struct cache_info dcache;
};

static inline unsigned int avr32_get_manufacturer_id(struct avr32_cpuinfo *cpu)
{
	return (cpu->device_id >> 1) & 0x7f;
}
static inline unsigned int avr32_get_product_number(struct avr32_cpuinfo *cpu)
{
	return (cpu->device_id >> 12) & 0xffff;
}
static inline unsigned int avr32_get_chip_revision(struct avr32_cpuinfo *cpu)
{
	return (cpu->device_id >> 28) & 0x0f;
}

extern struct avr32_cpuinfo boot_cpu_data;

#ifdef CONFIG_SMP
extern struct avr32_cpuinfo cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's
 */
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))

#define cpu_relax()		barrier()
#define cpu_sync_pipeline()	asm volatile("sub pc, -2" : : : "memory")

struct cpu_context {
	unsigned long sr;
	unsigned long pc;
	unsigned long ksp;	/* Kernel stack pointer */
	unsigned long r7;
	unsigned long r6;
	unsigned long r5;
	unsigned long r4;
	unsigned long r3;
	unsigned long r2;
	unsigned long r1;
	unsigned long r0;
};

/* This struct contains the CPU context as stored by switch_to() */
struct thread_struct {
	struct cpu_context cpu_context;
	unsigned long single_step_addr;
	u16 single_step_insn;
};

#define INIT_THREAD {						\
	.cpu_context = {					\
		.ksp = sizeof(init_stack) + (long)&init_stack,	\
	},							\
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define start_thread(regs, new_pc, new_sp)	 \
	do {					 \
		memset(regs, 0, sizeof(*regs));	 \
		regs->sr = MODE_USER;		 \
		regs->pc = new_pc & ~1;		 \
		regs->sp = new_sp;		 \
	} while(0)

struct task_struct;

/* Free all resources held by a thread */
extern void release_thread(struct task_struct *);

/* Create a kernel thread without removing it from tasklists */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/* Return saved PC of a blocked thread */
#define thread_saved_pc(tsk)    ((tsk)->thread.cpu_context.pc)

struct pt_regs;
extern unsigned long get_wchan(struct task_struct *p);
extern void show_regs_log_lvl(struct pt_regs *regs, const char *log_lvl);
extern void show_stack_log_lvl(struct task_struct *tsk, unsigned long sp,
			       struct pt_regs *regs, const char *log_lvl);

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)

#define KSTK_EIP(tsk)	((tsk)->thread.cpu_context.pc)
#define KSTK_ESP(tsk)	((tsk)->thread.cpu_context.ksp)

#define ARCH_HAS_PREFETCH

static inline void prefetch(const void *x)
{
	const char *c = x;
	asm volatile("pref %0" : : "r"(c));
}
#define PREFETCH_STRIDE	L1_CACHE_BYTES

#endif /* __ASSEMBLY__ */

#endif /* __ASM_AVR32_PROCESSOR_H */
