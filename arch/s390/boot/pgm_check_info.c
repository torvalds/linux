// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/stacktrace.h>
#include <asm/boot_data.h>
#include <asm/lowcore.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include <asm/uv.h>
#include "boot.h"

void print_stacktrace(unsigned long sp)
{
	struct stack_info boot_stack = { STACK_TYPE_TASK, (unsigned long)_stack_start,
					 (unsigned long)_stack_end };
	bool first = true;

	boot_printk("Call Trace:\n");
	while (!(sp & 0x7) && on_stack(&boot_stack, sp, sizeof(struct stack_frame))) {
		struct stack_frame *sf = (struct stack_frame *)sp;

		boot_printk(first ? "(sp:%016lx [<%016lx>] %pS)\n" :
				    " sp:%016lx [<%016lx>] %pS\n",
			    sp, sf->gprs[8], (void *)sf->gprs[8]);
		if (sf->back_chain <= sp)
			break;
		sp = sf->back_chain;
		first = false;
	}
}

void print_pgm_check_info(void)
{
	unsigned long *gpregs = (unsigned long *)get_lowcore()->gpregs_save_area;
	struct psw_bits *psw = &psw_bits(get_lowcore()->psw_save_area);

	boot_printk("Linux version %s\n", kernel_version);
	if (!is_prot_virt_guest() && early_command_line[0])
		boot_printk("Kernel command line: %s\n", early_command_line);
	boot_printk("Kernel fault: interruption code %04x ilc:%x\n",
		    get_lowcore()->pgm_code, get_lowcore()->pgm_ilc >> 1);
	if (kaslr_enabled()) {
		boot_printk("Kernel random base: %lx\n", __kaslr_offset);
		boot_printk("Kernel random base phys: %lx\n", __kaslr_offset_phys);
	}
	boot_printk("PSW : %016lx %016lx (%pS)\n",
		    get_lowcore()->psw_save_area.mask,
		    get_lowcore()->psw_save_area.addr,
		    (void *)get_lowcore()->psw_save_area.addr);
	boot_printk(
		"      R:%x T:%x IO:%x EX:%x Key:%x M:%x W:%x P:%x AS:%x CC:%x PM:%x RI:%x EA:%x\n",
		psw->per, psw->dat, psw->io, psw->ext, psw->key, psw->mcheck,
		psw->wait, psw->pstate, psw->as, psw->cc, psw->pm, psw->ri,
		psw->eaba);
	boot_printk("GPRS: %016lx %016lx %016lx %016lx\n", gpregs[0], gpregs[1], gpregs[2], gpregs[3]);
	boot_printk("      %016lx %016lx %016lx %016lx\n", gpregs[4], gpregs[5], gpregs[6], gpregs[7]);
	boot_printk("      %016lx %016lx %016lx %016lx\n", gpregs[8], gpregs[9], gpregs[10], gpregs[11]);
	boot_printk("      %016lx %016lx %016lx %016lx\n", gpregs[12], gpregs[13], gpregs[14], gpregs[15]);
	print_stacktrace(get_lowcore()->gpregs_save_area[15]);
	boot_printk("Last Breaking-Event-Address:\n");
	boot_printk(" [<%016lx>] %pS\n", (unsigned long)get_lowcore()->pgm_last_break,
		    (void *)get_lowcore()->pgm_last_break);
}
