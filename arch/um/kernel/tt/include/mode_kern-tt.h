/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TT_MODE_KERN_H__
#define __TT_MODE_KERN_H__

#include "linux/sched.h"
#include "asm/page.h"
#include "asm/ptrace.h"
#include "asm/uaccess.h"

extern void switch_to_tt(void *prev, void *next);
extern void flush_thread_tt(void);
extern void start_thread_tt(struct pt_regs *regs, unsigned long eip,
			   unsigned long esp);
extern int copy_thread_tt(int nr, unsigned long clone_flags, unsigned long sp,
			  unsigned long stack_top, struct task_struct *p,
			  struct pt_regs *regs);
extern void release_thread_tt(struct task_struct *task);
extern void initial_thread_cb_tt(void (*proc)(void *), void *arg);
extern void init_idle_tt(void);
extern void flush_tlb_kernel_range_tt(unsigned long start, unsigned long end);
extern void flush_tlb_kernel_vm_tt(void);
extern void __flush_tlb_one_tt(unsigned long addr);
extern void flush_tlb_range_tt(struct vm_area_struct *vma,
			       unsigned long start, unsigned long end);
extern void flush_tlb_mm_tt(struct mm_struct *mm);
extern void force_flush_all_tt(void);
extern long execute_syscall_tt(void *r);
extern void before_mem_tt(unsigned long brk_start);
extern unsigned long set_task_sizes_tt(int arg, unsigned long *host_size_out,
				       unsigned long *task_size_out);
extern int start_uml_tt(void);
extern int external_pid_tt(struct task_struct *task);
extern int thread_pid_tt(struct task_struct *task);

#define kmem_end_tt (host_task_size - ABOVE_KMEM)

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
