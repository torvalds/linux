/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_MODE_KERN_H__
#define __SKAS_MODE_KERN_H__

#include "linux/sched.h"
#include "asm/page.h"
#include "asm/ptrace.h"

extern void flush_thread_skas(void);
extern void switch_to_skas(void *prev, void *next);
extern void start_thread_skas(struct pt_regs *regs, unsigned long eip,
			      unsigned long esp);
extern int copy_thread_skas(int nr, unsigned long clone_flags,
			    unsigned long sp, unsigned long stack_top,
			    struct task_struct *p, struct pt_regs *regs);
extern void release_thread_skas(struct task_struct *task);
extern void init_idle_skas(void);
extern void flush_tlb_kernel_range_skas(unsigned long start,
					unsigned long end);
extern void flush_tlb_kernel_vm_skas(void);
extern void __flush_tlb_one_skas(unsigned long addr);
extern void flush_tlb_range_skas(struct vm_area_struct *vma,
				 unsigned long start, unsigned long end);
extern void flush_tlb_mm_skas(struct mm_struct *mm);
extern void force_flush_all_skas(void);
extern long execute_syscall_skas(void *r);
extern void before_mem_skas(unsigned long unused);
extern unsigned long set_task_sizes_skas(unsigned long *task_size_out);
extern int start_uml_skas(void);
extern int external_pid_skas(struct task_struct *task);
extern int thread_pid_skas(struct task_struct *task);
extern void flush_tlb_page_skas(struct vm_area_struct *vma,
				unsigned long address);

#define kmem_end_skas (host_task_size - 1024 * 1024)

#endif
