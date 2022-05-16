// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/ptrace.h>

#include <asm/reg.h>
#include <asm/cacheflush.h>

int machine_check_440A(struct pt_regs *regs)
{
	unsigned long reason = regs->dsisr;

	printk("Machine check in kernel mode.\n");
	if (reason & ESR_IMCP){
		printk("Instruction Synchronous Machine Check exception\n");
		mtspr(SPRN_ESR, reason & ~ESR_IMCP);
	}
	else {
		u32 mcsr = mfspr(SPRN_MCSR);
		if (mcsr & MCSR_IB)
			printk("Instruction Read PLB Error\n");
		if (mcsr & MCSR_DRB)
			printk("Data Read PLB Error\n");
		if (mcsr & MCSR_DWB)
			printk("Data Write PLB Error\n");
		if (mcsr & MCSR_TLBP)
			printk("TLB Parity Error\n");
		if (mcsr & MCSR_ICP){
			flush_instruction_cache();
			printk("I-Cache Parity Error\n");
		}
		if (mcsr & MCSR_DCSP)
			printk("D-Cache Search Parity Error\n");
		if (mcsr & MCSR_DCFP)
			printk("D-Cache Flush Parity Error\n");
		if (mcsr & MCSR_IMPE)
			printk("Machine Check exception is imprecise\n");

		/* Clear MCSR */
		mtspr(SPRN_MCSR, mcsr);
	}
	return 0;
}

#ifdef CONFIG_PPC_47x
int machine_check_47x(struct pt_regs *regs)
{
	unsigned long reason = regs->dsisr;
	u32 mcsr;

	printk(KERN_ERR "Machine check in kernel mode.\n");
	if (reason & ESR_IMCP) {
		printk(KERN_ERR "Instruction Synchronous Machine Check exception\n");
		mtspr(SPRN_ESR, reason & ~ESR_IMCP);
		return 0;
	}
	mcsr = mfspr(SPRN_MCSR);
	if (mcsr & MCSR_IB)
		printk(KERN_ERR "Instruction Read PLB Error\n");
	if (mcsr & MCSR_DRB)
		printk(KERN_ERR "Data Read PLB Error\n");
	if (mcsr & MCSR_DWB)
		printk(KERN_ERR "Data Write PLB Error\n");
	if (mcsr & MCSR_TLBP)
		printk(KERN_ERR "TLB Parity Error\n");
	if (mcsr & MCSR_ICP) {
		flush_instruction_cache();
		printk(KERN_ERR "I-Cache Parity Error\n");
	}
	if (mcsr & MCSR_DCSP)
		printk(KERN_ERR "D-Cache Search Parity Error\n");
	if (mcsr & PPC47x_MCSR_GPR)
		printk(KERN_ERR "GPR Parity Error\n");
	if (mcsr & PPC47x_MCSR_FPR)
		printk(KERN_ERR "FPR Parity Error\n");
	if (mcsr & PPC47x_MCSR_IPR)
		printk(KERN_ERR "Machine Check exception is imprecise\n");

	/* Clear MCSR */
	mtspr(SPRN_MCSR, mcsr);

	return 0;
}
#endif /* CONFIG_PPC_47x */
