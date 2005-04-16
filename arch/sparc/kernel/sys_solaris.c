/*
 * linux/arch/sparc/sys_solaris.c
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

asmlinkage int
do_solaris_syscall (struct pt_regs *regs)
{
	static int cnt = 0;
	if (++cnt < 10) printk ("No solaris handler\n");
	force_sig(SIGSEGV, current);
	return 0;
}

#ifndef CONFIG_SUNOS_EMUL
asmlinkage int
do_sunos_syscall (struct pt_regs *regs)
{
	static int cnt = 0;
	if (++cnt < 10) printk ("SunOS binary emulation not compiled in\n");
	force_sig (SIGSEGV, current);
	return 0;
}
#endif
