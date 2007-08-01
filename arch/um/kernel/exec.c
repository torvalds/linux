/*
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/slab.h"
#include "linux/smp_lock.h"
#include "linux/ptrace.h"
#include "linux/fs.h"
#include "asm/ptrace.h"
#include "asm/pgtable.h"
#include "asm/tlbflush.h"
#include "asm/uaccess.h"
#include "kern_util.h"
#include "as-layout.h"
#include "mem_user.h"
#include "kern.h"
#include "irq_user.h"
#include "tlb.h"
#include "os.h"
#include "choose-mode.h"
#include "mode_kern.h"

void flush_thread(void)
{
	arch_flush_thread(&current->thread.arch);
	CHOOSE_MODE(flush_thread_tt(), flush_thread_skas());
}

void start_thread(struct pt_regs *regs, unsigned long eip, unsigned long esp)
{
	CHOOSE_MODE_PROC(start_thread_tt, start_thread_skas, regs, eip, esp);
}

#ifdef CONFIG_TTY_LOG
extern void log_exec(char **argv, void *tty);
#endif

static long execve1(char *file, char __user * __user *argv,
		    char __user *__user *env)
{
        long error;
#ifdef CONFIG_TTY_LOG
	struct tty_struct *tty;

	mutex_lock(&tty_mutex);
	tty = get_current_tty();
	if (tty)
		log_exec(argv, tty);
	mutex_unlock(&tty_mutex);
#endif
        error = do_execve(file, argv, env, &current->thread.regs);
        if (error == 0){
		task_lock(current);
                current->ptrace &= ~PT_DTRACE;
#ifdef SUBARCH_EXECVE1
		SUBARCH_EXECVE1(&current->thread.regs.regs);
#endif
		task_unlock(current);
                set_cmdline(current_cmd());
        }
        return(error);
}

long um_execve(char *file, char __user *__user *argv, char __user *__user *env)
{
	long err;

	err = execve1(file, argv, env);
	if(!err)
		do_longjmp(current->thread.exec_buf, 1);
	return(err);
}

long sys_execve(char __user *file, char __user *__user *argv,
		char __user *__user *env)
{
	long error;
	char *filename;

	lock_kernel();
	filename = getname(file);
	error = PTR_ERR(filename);
	if (IS_ERR(filename)) goto out;
	error = execve1(filename, argv, env);
	putname(filename);
 out:
	unlock_kernel();
	return(error);
}
