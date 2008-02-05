/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __KERN_UTIL_H__
#define __KERN_UTIL_H__

#include "sysdep/ptrace.h"
#include "sysdep/faultinfo.h"

extern int ncpus;
extern int kmalloc_ok;
extern int nsyscalls;

#define UML_ROUND_UP(addr) \
	((((unsigned long) addr) + PAGE_SIZE - 1) & PAGE_MASK)

extern unsigned long alloc_stack(int order, int atomic);
extern void free_stack(unsigned long stack, int order);

extern int do_signal(void);
extern void copy_sc(struct uml_pt_regs *regs, void *from);
extern void interrupt_end(void);
extern void relay_signal(int sig, struct uml_pt_regs *regs);

extern unsigned long segv(struct faultinfo fi, unsigned long ip,
			  int is_user, struct uml_pt_regs *regs);
extern int handle_page_fault(unsigned long address, unsigned long ip,
			     int is_write, int is_user, int *code_out);

extern unsigned int do_IRQ(int irq, struct uml_pt_regs *regs);
extern int smp_sigio_handler(void);
extern void initial_thread_cb(void (*proc)(void *), void *arg);
extern int is_syscall(unsigned long addr);
extern void timer_handler(int sig, struct uml_pt_regs *regs);

extern void timer_handler(int sig, struct uml_pt_regs *regs);

extern int start_uml(void);
extern void paging_init(void);

extern void uml_cleanup(void);
extern void do_uml_exitcalls(void);

/*
 * Are we disallowed to sleep? Used to choose between GFP_KERNEL and
 * GFP_ATOMIC.
 */
extern int __cant_sleep(void);
extern void *get_current(void);
extern int copy_from_user_proc(void *to, void *from, int size);
extern int cpu(void);
extern char *uml_strdup(const char *string);

extern unsigned long to_irq_stack(unsigned long *mask_out);
extern unsigned long from_irq_stack(int nested);

extern void syscall_trace(struct uml_pt_regs *regs, int entryexit);
extern int singlestepping(void *t);

extern void segv_handler(int sig, struct uml_pt_regs *regs);
extern void bus_handler(int sig, struct uml_pt_regs *regs);
extern void winch(int sig, struct uml_pt_regs *regs);


#endif
