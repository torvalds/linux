/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/utsname.h>
#include <asm/current.h>
#include <asm/ptrace.h>
#include "sysrq.h"

void __show_regs(struct pt_regs *regs)
{
	printk("\n");
	print_modules();
	printk(KERN_INFO "Pid: %d, comm: %.20s %s %s\n", task_pid_nr(current),
		current->comm, print_tainted(), init_utsname()->release);
	printk(KERN_INFO "RIP: %04lx:[<%016lx>]\n", PT_REGS_CS(regs) & 0xffff,
	       PT_REGS_RIP(regs));
	printk(KERN_INFO "RSP: %016lx  EFLAGS: %08lx\n", PT_REGS_RSP(regs),
	       PT_REGS_EFLAGS(regs));
	printk(KERN_INFO "RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       PT_REGS_RAX(regs), PT_REGS_RBX(regs), PT_REGS_RCX(regs));
	printk(KERN_INFO "RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       PT_REGS_RDX(regs), PT_REGS_RSI(regs), PT_REGS_RDI(regs));
	printk(KERN_INFO "RBP: %016lx R08: %016lx R09: %016lx\n",
	       PT_REGS_RBP(regs), PT_REGS_R8(regs), PT_REGS_R9(regs));
	printk(KERN_INFO "R10: %016lx R11: %016lx R12: %016lx\n",
	       PT_REGS_R10(regs), PT_REGS_R11(regs), PT_REGS_R12(regs));
	printk(KERN_INFO "R13: %016lx R14: %016lx R15: %016lx\n",
	       PT_REGS_R13(regs), PT_REGS_R14(regs), PT_REGS_R15(regs));
}

void show_regs(struct pt_regs *regs)
{
	__show_regs(regs);
	show_trace(current, (unsigned long *) &regs);
}
