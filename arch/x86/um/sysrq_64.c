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
#include <asm/sysrq.h>

void show_regs(struct pt_regs *regs)
{
	printk("\n");
	print_modules();
	printk(KERN_INFO "Pid: %d, comm: %.20s %s %s\n", task_pid_nr(current),
		current->comm, print_tainted(), init_utsname()->release);
	printk(KERN_INFO "RIP: %04lx:[<%016lx>]\n", PT_REGS_CS(regs) & 0xffff,
	       PT_REGS_IP(regs));
	printk(KERN_INFO "RSP: %016lx  EFLAGS: %08lx\n", PT_REGS_SP(regs),
	       PT_REGS_EFLAGS(regs));
	printk(KERN_INFO "RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       PT_REGS_AX(regs), PT_REGS_BX(regs), PT_REGS_CX(regs));
	printk(KERN_INFO "RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       PT_REGS_DX(regs), PT_REGS_SI(regs), PT_REGS_DI(regs));
	printk(KERN_INFO "RBP: %016lx R08: %016lx R09: %016lx\n",
	       PT_REGS_BP(regs), PT_REGS_R8(regs), PT_REGS_R9(regs));
	printk(KERN_INFO "R10: %016lx R11: %016lx R12: %016lx\n",
	       PT_REGS_R10(regs), PT_REGS_R11(regs), PT_REGS_R12(regs));
	printk(KERN_INFO "R13: %016lx R14: %016lx R15: %016lx\n",
	       PT_REGS_R13(regs), PT_REGS_R14(regs), PT_REGS_R15(regs));
}
