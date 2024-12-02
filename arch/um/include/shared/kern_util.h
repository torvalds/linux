/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __KERN_UTIL_H__
#define __KERN_UTIL_H__

#include <sysdep/ptrace.h>
#include <sysdep/faultinfo.h>

struct siginfo;

extern int uml_exitcode;

extern int ncpus;
extern int kmalloc_ok;

#define UML_ROUND_UP(addr) \
	((((unsigned long) addr) + PAGE_SIZE - 1) & PAGE_MASK)

extern unsigned long alloc_stack(int order, int atomic);
extern void free_stack(unsigned long stack, int order);

struct pt_regs;
extern void do_signal(struct pt_regs *regs);
extern void interrupt_end(void);
extern void relay_signal(int sig, struct siginfo *si, struct uml_pt_regs *regs);

extern unsigned long segv(struct faultinfo fi, unsigned long ip,
			  int is_user, struct uml_pt_regs *regs);
extern int handle_page_fault(unsigned long address, unsigned long ip,
			     int is_write, int is_user, int *code_out);

extern unsigned int do_IRQ(int irq, struct uml_pt_regs *regs);
extern void initial_thread_cb(void (*proc)(void *), void *arg);
extern int is_syscall(unsigned long addr);

extern void timer_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs);

extern void uml_pm_wake(void);

extern int start_uml(void);
extern void paging_init(void);

extern void uml_cleanup(void);
extern void do_uml_exitcalls(void);

/*
 * Are we disallowed to sleep? Used to choose between GFP_KERNEL and
 * GFP_ATOMIC.
 */
extern int __uml_cant_sleep(void);
extern int get_current_pid(void);
extern int copy_from_user_proc(void *to, void *from, int size);
extern char *uml_strdup(const char *string);

extern unsigned long to_irq_stack(unsigned long *mask_out);
extern unsigned long from_irq_stack(int nested);

extern int singlestepping(void *t);

extern void segv_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs);
extern void bus_handler(int sig, struct siginfo *si, struct uml_pt_regs *regs);
extern void winch(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs);
extern void fatal_sigsegv(void) __attribute__ ((noreturn));

void um_idle_sleep(void);

void kasan_map_memory(void *start, size_t len);

#endif
