/*
 * P5 specific Machine Check Exception Reporting
 * (C) Copyright 2002 Alan Cox <alan@redhat.com>
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/smp.h>

#include <asm/processor.h> 
#include <asm/system.h>
#include <asm/msr.h>

#include "mce.h"

/* Machine check handler for Pentium class Intel */
static void pentium_machine_check(struct pt_regs * regs, long error_code)
{
	u32 loaddr, hi, lotype;
	rdmsr(MSR_IA32_P5_MC_ADDR, loaddr, hi);
	rdmsr(MSR_IA32_P5_MC_TYPE, lotype, hi);
	printk(KERN_EMERG "CPU#%d: Machine Check Exception:  0x%8X (type 0x%8X).\n", smp_processor_id(), loaddr, lotype);
	if(lotype&(1<<5))
		printk(KERN_EMERG "CPU#%d: Possible thermal failure (CPU on fire ?).\n", smp_processor_id());
	add_taint(TAINT_MACHINE_CHECK);
}

/* Set up machine check reporting for processors with Intel style MCE */
void intel_p5_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 l, h;
	
	/*Check for MCE support */
	if( !cpu_has(c, X86_FEATURE_MCE) )
		return;	

	/* Default P5 to off as its often misconnected */
	if(mce_disabled != -1)
		return;
	machine_check_vector = pentium_machine_check;
	wmb();

	/* Read registers before enabling */
	rdmsr(MSR_IA32_P5_MC_ADDR, l, h);
	rdmsr(MSR_IA32_P5_MC_TYPE, l, h);
	printk(KERN_INFO "Intel old style machine check architecture supported.\n");

 	/* Enable MCE */
	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Intel old style machine check reporting enabled on CPU#%d.\n", smp_processor_id());
}
