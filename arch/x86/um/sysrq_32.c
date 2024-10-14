/*
 * Copyright (C) 2001 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/kallsyms.h>
#include <asm/ptrace.h>

/* This is declared by <linux/sched.h> */
void show_regs(struct pt_regs *regs)
{
        printk("\n");
        printk("EIP: %04lx:[<%08lx>] CPU: %d %s", 
	       0xffff & PT_REGS_CS(regs), PT_REGS_IP(regs),
	       smp_processor_id(), print_tainted());
        if (PT_REGS_CS(regs) & 3)
                printk(" ESP: %04lx:%08lx", 0xffff & PT_REGS_SS(regs),
		       PT_REGS_SP(regs));
        printk(" EFLAGS: %08lx\n    %s\n", PT_REGS_EFLAGS(regs),
	       print_tainted());
        printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
               PT_REGS_AX(regs), PT_REGS_BX(regs), 
	       PT_REGS_CX(regs), PT_REGS_DX(regs));
        printk("ESI: %08lx EDI: %08lx EBP: %08lx",
	       PT_REGS_SI(regs), PT_REGS_DI(regs), PT_REGS_BP(regs));
        printk(" DS: %04lx ES: %04lx\n",
	       0xffff & PT_REGS_DS(regs), 
	       0xffff & PT_REGS_ES(regs));
}
