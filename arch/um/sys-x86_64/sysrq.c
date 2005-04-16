/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/utsname.h"
#include "linux/module.h"
#include "asm/current.h"
#include "asm/ptrace.h"
#include "sysrq.h"

void __show_regs(struct pt_regs * regs)
{
	printk("\n");
	print_modules();
	printk("Pid: %d, comm: %.20s %s %s\n",
	       current->pid, current->comm, print_tainted(), system_utsname.release);
	printk("RIP: %04lx:[<%016lx>] ", PT_REGS_CS(regs) & 0xffff,
	       PT_REGS_RIP(regs));
	printk("\nRSP: %016lx  EFLAGS: %08lx\n", PT_REGS_RSP(regs),
	       PT_REGS_EFLAGS(regs));
	printk("RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       PT_REGS_RAX(regs), PT_REGS_RBX(regs), PT_REGS_RCX(regs));
	printk("RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       PT_REGS_RDX(regs), PT_REGS_RSI(regs), PT_REGS_RDI(regs));
	printk("RBP: %016lx R08: %016lx R09: %016lx\n",
	       PT_REGS_RBP(regs), PT_REGS_R8(regs), PT_REGS_R9(regs));
	printk("R10: %016lx R11: %016lx R12: %016lx\n",
	       PT_REGS_R10(regs), PT_REGS_R11(regs), PT_REGS_R12(regs));
	printk("R13: %016lx R14: %016lx R15: %016lx\n",
	       PT_REGS_R13(regs), PT_REGS_R14(regs), PT_REGS_R15(regs));
}

void show_regs(struct pt_regs *regs)
{
	__show_regs(regs);
	show_trace((unsigned long *) &regs);
}

/* Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
