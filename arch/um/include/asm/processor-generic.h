/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_PROCESSOR_GENERIC_H
#define __UM_PROCESSOR_GENERIC_H

struct pt_regs;

struct task_struct;

#include <asm/ptrace.h>
#include <sysdep/archsetjmp.h>

#include <linux/prefetch.h>

#include <asm/cpufeatures.h>

struct mm_struct;

struct thread_struct {
	struct pt_regs regs;
	struct pt_regs *segv_regs;
	void *fault_addr;
	jmp_buf *fault_catcher;
	struct task_struct *prev_sched;
	struct arch_thread arch;
	jmp_buf switch_buf;
	struct {
		int op;
		union {
			struct {
				int pid;
			} fork, exec;
			struct {
				int (*proc)(void *);
				void *arg;
			} thread;
			struct {
				void (*proc)(void *);
				void *arg;
			} cb;
		} u;
	} request;
};

#define INIT_THREAD \
{ \
	.regs		   	= EMPTY_REGS,	\
	.fault_addr		= NULL, \
	.prev_sched		= NULL, \
	.arch			= INIT_ARCH_THREAD, \
	.request		= { 0 } \
}

/*
 * User space process size: 3GB (default).
 */
extern unsigned long task_size;

#define TASK_SIZE (task_size)

#undef STACK_TOP
#undef STACK_TOP_MAX

extern unsigned long stacksizelim;

#define STACK_ROOM	(stacksizelim)
#define STACK_TOP	(TASK_SIZE - 2 * PAGE_SIZE)
#define STACK_TOP_MAX	STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(0x40000000)

extern void start_thread(struct pt_regs *regs, unsigned long entry, 
			 unsigned long stack);

struct cpuinfo_um {
	unsigned long loops_per_jiffy;
	int ipi_pipe[2];
	int cache_alignment;
	union {
		__u32		x86_capability[NCAPINTS + NBUGINTS];
		unsigned long	x86_capability_alignment;
	};
};

extern struct cpuinfo_um boot_cpu_data;

#define cpu_data(cpu)    boot_cpu_data
#define current_cpu_data boot_cpu_data
#define cache_line_size()	(boot_cpu_data.cache_alignment)

extern unsigned long get_thread_reg(int reg, jmp_buf *buf);
#define KSTK_REG(tsk, reg) get_thread_reg(reg, &tsk->thread.switch_buf)
extern unsigned long __get_wchan(struct task_struct *p);

#endif
