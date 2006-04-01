/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __USER_UTIL_H__
#define __USER_UTIL_H__

#include "sysdep/ptrace.h"

/* Copied from kernel.h */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define CATCH_EINTR(expr) while ((errno = 0, ((expr) < 0)) && (errno == EINTR))

extern int mode_tt;

extern int grantpt(int __fd);
extern int unlockpt(int __fd);
extern char *ptsname(int __fd);

struct cpu_task {
	int pid;
	void *task;
};

extern struct cpu_task cpu_tasks[];

extern void (*sig_info[])(int, union uml_pt_regs *);

extern unsigned long low_physmem;
extern unsigned long high_physmem;
extern unsigned long uml_physmem;
extern unsigned long uml_reserved;
extern unsigned long end_vm;
extern unsigned long start_vm;
extern unsigned long long highmem;

extern char host_info[];

extern char saved_command_line[];

extern unsigned long _stext, _etext, _sdata, _edata, __bss_start, _end;
extern unsigned long _unprotected_end;
extern unsigned long brk_start;

extern int pty_output_sigio;
extern int pty_close_sigio;

extern void *add_signal_handler(int sig, void (*handler)(int));
extern int linux_main(int argc, char **argv);
extern void set_cmdline(char *cmd);
extern void input_cb(void (*proc)(void *), void *arg, int arg_len);
extern int get_pty(void);
extern void *um_kmalloc(int size);
extern int switcheroo(int fd, int prot, void *from, void *to, int size);
extern void do_exec(int old_pid, int new_pid);
extern void tracer_panic(char *msg, ...);
extern int detach(int pid, int sig);
extern int attach(int pid);
extern void kill_child_dead(int pid);
extern int cont(int pid);
extern void check_sigio(void);
extern void arch_check_bugs(void);
extern int cpu_feature(char *what, char *buf, int len);
extern int arch_handle_signal(int sig, union uml_pt_regs *regs);
extern int arch_fixup(unsigned long address, void *sc_ptr);
extern void arch_init_thread(void);
extern int raw(int fd);

#endif
