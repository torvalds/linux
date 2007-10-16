/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/fs.h"
#include "linux/smp_lock.h"
#include "linux/ptrace.h"
#include "linux/sched.h"
#include "asm/current.h"
#include "asm/processor.h"
#include "asm/uaccess.h"
#include "mem_user.h"
#include "skas.h"
#include "os.h"

void flush_thread(void)
{
	void *data = NULL;
	unsigned long end = proc_mm ? task_size : CONFIG_STUB_START;
	int ret;

	arch_flush_thread(&current->thread.arch);

	ret = unmap(&current->mm->context.skas.id, 0, end, 1, &data);
	if (ret) {
		printk(KERN_ERR "flush_thread - clearing address space failed, "
		       "err = %d\n", ret);
		force_sig(SIGKILL, current);
	}

	__switch_mm(&current->mm->context.skas.id);
}

void start_thread(struct pt_regs *regs, unsigned long eip, unsigned long esp)
{
	set_fs(USER_DS);
	PT_REGS_IP(regs) = eip;
	PT_REGS_SP(regs) = esp;
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
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
#ifdef SUBARCH_EXECVE1
		SUBARCH_EXECVE1(&current->thread.regs.regs);
#endif
		task_unlock(current);
	}
	return error;
}

long um_execve(char *file, char __user *__user *argv, char __user *__user *env)
{
	long err;

	err = execve1(file, argv, env);
	if (!err)
		do_longjmp(current->thread.exec_buf, 1);
	return err;
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
	return error;
}
