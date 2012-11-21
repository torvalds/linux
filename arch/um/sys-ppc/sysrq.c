/* 
 * Copyright (C) 2001 Chris Emerson (cemerson@chiark.greenend.org.uk)
 * Licensed under the GPL
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include "asm/ptrace.h"
#include "sysrq.h"

void show_regs(struct pt_regs_subarch *regs)
{
	printk("\n");
	printk("show_regs(): insert regs here.\n");
#if 0
        printk("\n");
        printk("EIP: %04x:[<%08lx>] CPU: %d",0xffff & regs->xcs, regs->eip,
	       smp_processor_id());
        if (regs->xcs & 3)
                printk(" ESP: %04x:%08lx",0xffff & regs->xss, regs->esp);
        printk(" EFLAGS: %08lx\n", regs->eflags);
        printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
                regs->eax, regs->ebx, regs->ecx, regs->edx);
        printk("ESI: %08lx EDI: %08lx EBP: %08lx",
                regs->esi, regs->edi, regs->ebp);
        printk(" DS: %04x ES: %04x\n",
                0xffff & regs->xds, 0xffff & regs->xes);
#endif

        show_trace(current, &regs->gpr[1]);
}
