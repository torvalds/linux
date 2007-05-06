/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __KERN_UTIL_H__
#define __KERN_UTIL_H__

#include "sysdep/ptrace.h"
#include "sysdep/faultinfo.h"
#include "uml-config.h"

typedef void (*kern_hndl)(int, union uml_pt_regs *);

struct kern_handlers {
	kern_hndl relay_signal;
	kern_hndl winch;
	kern_hndl bus_handler;
	kern_hndl page_fault;
	kern_hndl sigio_handler;
	kern_hndl timer_handler;
};

extern const struct kern_handlers handlinfo_kern;

extern int ncpus;
extern char *gdb_init;
extern int kmalloc_ok;
extern int jail;
extern int nsyscalls;

#define UML_ROUND_DOWN(addr) ((void *)(((unsigned long) addr) & PAGE_MASK))
#define UML_ROUND_UP(addr) \
	UML_ROUND_DOWN(((unsigned long) addr) + PAGE_SIZE - 1)

extern int kernel_fork(unsigned long flags, int (*fn)(void *), void * arg);
#ifdef UML_CONFIG_MODE_TT
extern unsigned long stack_sp(unsigned long page);
#endif
extern int kernel_thread_proc(void *data);
extern void syscall_segv(int sig);
extern int current_pid(void);
extern unsigned long alloc_stack(int order, int atomic);
extern int do_signal(void);
extern int is_stack_fault(unsigned long sp);
extern unsigned long segv(struct faultinfo fi, unsigned long ip,
			  int is_user, union uml_pt_regs *regs);
extern int handle_page_fault(unsigned long address, unsigned long ip,
			     int is_write, int is_user, int *code_out);
extern void syscall_ready(void);
extern void set_tracing(void *t, int tracing);
extern int is_tracing(void *task);
extern int segv_syscall(void);
extern void kern_finish_exec(void *task, int new_pid, unsigned long stack);
extern unsigned long page_mask(void);
extern int need_finish_fork(void);
extern void free_stack(unsigned long stack, int order);
extern void add_input_request(int op, void (*proc)(int), void *arg);
extern char *current_cmd(void);
extern void timer_handler(int sig, union uml_pt_regs *regs);
extern int set_signals(int enable);
extern int pid_to_processor_id(int pid);
extern void deliver_signals(void *t);
extern int next_trap_index(int max);
extern void default_idle(void);
extern void finish_fork(void);
extern void paging_init(void);
extern void init_flush_vm(void);
extern void *syscall_sp(void *t);
extern void syscall_trace(union uml_pt_regs *regs, int entryexit);
extern int hz(void);
extern unsigned int do_IRQ(int irq, union uml_pt_regs *regs);
extern void interrupt_end(void);
extern void initial_thread_cb(void (*proc)(void *), void *arg);
extern int debugger_signal(int status, int pid);
extern void debugger_parent_signal(int status, int pid);
extern void child_signal(int pid, int status);
extern int init_ptrace_proxy(int idle_pid, int startup, int stop);
extern int init_parent_proxy(int pid);
extern int singlestepping(void *t);
extern void check_stack_overflow(void *ptr);
extern void relay_signal(int sig, union uml_pt_regs *regs);
extern int user_context(unsigned long sp);
extern void timer_irq(union uml_pt_regs *regs);
extern void unprotect_stack(unsigned long stack);
extern void do_uml_exitcalls(void);
extern int attach_debugger(int idle_pid, int pid, int stop);
extern int config_gdb(char *str);
extern int remove_gdb(void);
extern char *uml_strdup(char *string);
extern void unprotect_kernel_mem(void);
extern void protect_kernel_mem(void);
extern void uml_cleanup(void);
extern void lock_signalled_task(void *t);
extern void IPI_handler(int cpu);
extern int jail_setup(char *line, int *add);
extern void *get_init_task(void);
extern int clear_user_proc(void *buf, int size);
extern int copy_to_user_proc(void *to, void *from, int size);
extern int copy_from_user_proc(void *to, void *from, int size);
extern int strlen_user_proc(char *str);
extern long execute_syscall(void *r);
extern int smp_sigio_handler(void);
extern void *get_current(void);
extern struct task_struct *get_task(int pid, int require);
extern void machine_halt(void);
extern int is_syscall(unsigned long addr);

extern void free_irq(unsigned int, void *);
extern int cpu(void);

extern void time_init_kern(void);

/* Are we disallowed to sleep? Used to choose between GFP_KERNEL and GFP_ATOMIC. */
extern int __cant_sleep(void);
extern void sigio_handler(int sig, union uml_pt_regs *regs);

#endif
