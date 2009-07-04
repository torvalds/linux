/*
 * P6 specific Machine Check Exception Reporting
 * (C) Copyright 2002 Alan Cox <alan@lxorguk.ukuu.org.uk>
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/mce.h>
#include <asm/msr.h>

/* Machine Check Handler For PII/PIII */
static void intel_machine_check(struct pt_regs *regs, long error_code)
{
	u32 alow, ahigh, high, low;
	u32 mcgstl, mcgsth;
	int recover = 1;
	int i;

	rdmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
	if (mcgstl & (1<<0))	/* Recoverable ? */
		recover = 0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x\n",
		smp_processor_id(), mcgsth, mcgstl);

	for (i = 0; i < nr_mce_banks; i++) {
		rdmsr(MSR_IA32_MC0_STATUS+i*4, low, high);
		if (high & (1<<31)) {
			char misc[20];
			char addr[24];

			misc[0] = '\0';
			addr[0] = '\0';

			if (high & (1<<29))
				recover |= 1;
			if (high & (1<<25))
				recover |= 2;
			high &= ~(1<<31);

			if (high & (1<<27)) {
				rdmsr(MSR_IA32_MC0_MISC+i*4, alow, ahigh);
				snprintf(misc, 20, "[%08x%08x]", ahigh, alow);
			}
			if (high & (1<<26)) {
				rdmsr(MSR_IA32_MC0_ADDR+i*4, alow, ahigh);
				snprintf(addr, 24, " at %08x%08x", ahigh, alow);
			}

			printk(KERN_EMERG "CPU %d: Bank %d: %08x%08x%s%s\n",
				smp_processor_id(), i, high, low, misc, addr);
		}
	}

	if (recover & 2)
		panic("CPU context corrupt");
	if (recover & 1)
		panic("Unable to continue");

	printk(KERN_EMERG "Attempting to continue.\n");
	/*
	 * Do not clear the MSR_IA32_MCi_STATUS if the error is not
	 * recoverable/continuable.This will allow BIOS to look at the MSRs
	 * for errors if the OS could not log the error:
	 */
	for (i = 0; i < nr_mce_banks; i++) {
		unsigned int msr;

		msr = MSR_IA32_MC0_STATUS+i*4;
		rdmsr(msr, low, high);
		if (high & (1<<31)) {
			/* Clear it: */
			wrmsr(msr, 0UL, 0UL);
			/* Serialize: */
			wmb();
			add_taint(TAINT_MACHINE_CHECK);
		}
	}
	mcgstl &= ~(1<<2);
	wrmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
}

/* Set up machine check reporting for processors with Intel style MCE: */
void intel_p6_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int i;

	/* Check for MCE support */
	if (!cpu_has(c, X86_FEATURE_MCE))
		return;

	/* Check for PPro style MCA */
	if (!cpu_has(c, X86_FEATURE_MCA))
		return;

	/* Ok machine check is available */
	machine_check_vector = intel_machine_check;
	/* Make sure the vector pointer is visible before we enable MCEs: */
	wmb();

	printk(KERN_INFO "Intel machine check architecture supported.\n");
	rdmsr(MSR_IA32_MCG_CAP, l, h);
	if (l & (1<<8))	/* Control register present ? */
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);
	nr_mce_banks = l & 0xff;

	/*
	 * Following the example in IA-32 SDM Vol 3:
	 * - MC0_CTL should not be written
	 * - Status registers on all banks should be cleared on reset
	 */
	for (i = 1; i < nr_mce_banks; i++)
		wrmsr(MSR_IA32_MC0_CTL+4*i, 0xffffffff, 0xffffffff);

	for (i = 0; i < nr_mce_banks; i++)
		wrmsr(MSR_IA32_MC0_STATUS+4*i, 0x0, 0x0);

	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Intel machine check reporting enabled on CPU#%d.\n",
		smp_processor_id());
}
