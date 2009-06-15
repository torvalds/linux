/*
 * IDT Winchip specific Machine Check Exception Reporting
 * (C) Copyright 2002 Alan Cox <alan@lxorguk.ukuu.org.uk>
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/msr.h>

#include "mce.h"

/* Machine check handler for WinChip C6: */
static void winchip_machine_check(struct pt_regs *regs, long error_code)
{
	printk(KERN_EMERG "CPU0: Machine Check Exception.\n");
	add_taint(TAINT_MACHINE_CHECK);
}

/* Set up machine check reporting on the Winchip C6 series */
void winchip_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 lo, hi;

	machine_check_vector = winchip_machine_check;
	/* Make sure the vector pointer is visible before we enable MCEs: */
	wmb();

	rdmsr(MSR_IDT_FCR1, lo, hi);
	lo |= (1<<2);	/* Enable EIERRINT (int 18 MCE) */
	lo &= ~(1<<4);	/* Enable MCE */
	wrmsr(MSR_IDT_FCR1, lo, hi);

	set_in_cr4(X86_CR4_MCE);

	printk(KERN_INFO
	       "Winchip machine check reporting enabled on CPU#0.\n");
}
