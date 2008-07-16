/*
 * P4 specific Machine Check Exception Reporting
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/smp.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/msr.h>
#include <asm/apic.h>

#include <asm/therm_throt.h>

#include "mce.h"

/* as supported by the P4/Xeon family */
struct intel_mce_extended_msrs {
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
	u32 esi;
	u32 edi;
	u32 ebp;
	u32 esp;
	u32 eflags;
	u32 eip;
	/* u32 *reserved[]; */
};

static int mce_num_extended_msrs;


#ifdef CONFIG_X86_MCE_P4THERMAL
static void unexpected_thermal_interrupt(struct pt_regs *regs)
{
	printk(KERN_ERR "CPU%d: Unexpected LVT TMR interrupt!\n",
			smp_processor_id());
	add_taint(TAINT_MACHINE_CHECK);
}

/* P4/Xeon Thermal transition interrupt handler */
static void intel_thermal_interrupt(struct pt_regs *regs)
{
	__u64 msr_val;

	ack_APIC_irq();

	rdmsrl(MSR_IA32_THERM_STATUS, msr_val);
	therm_throt_process(msr_val & 0x1);
}

/* Thermal interrupt handler for this CPU setup */
static void (*vendor_thermal_interrupt)(struct pt_regs *regs) = unexpected_thermal_interrupt;

void smp_thermal_interrupt(struct pt_regs *regs)
{
	irq_enter();
	vendor_thermal_interrupt(regs);
	__get_cpu_var(irq_stat).irq_thermal_count++;
	irq_exit();
}

/* P4/Xeon Thermal regulation detect and init */
static void intel_init_thermal(struct cpuinfo_x86 *c)
{
	u32 l, h;
	unsigned int cpu = smp_processor_id();

	/* Thermal monitoring */
	if (!cpu_has(c, X86_FEATURE_ACPI))
		return;	/* -ENODEV */

	/* Clock modulation */
	if (!cpu_has(c, X86_FEATURE_ACC))
		return;	/* -ENODEV */

	/* first check if its enabled already, in which case there might
	 * be some SMM goo which handles it, so we can't even put a handler
	 * since it might be delivered via SMI already -zwanem.
	 */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	h = apic_read(APIC_LVTTHMR);
	if ((l & (1<<3)) && (h & APIC_DM_SMI)) {
		printk(KERN_DEBUG "CPU%d: Thermal monitoring handled by SMI\n",
				cpu);
		return; /* -EBUSY */
	}

	/* check whether a vector already exists, temporarily masked? */
	if (h & APIC_VECTOR_MASK) {
		printk(KERN_DEBUG "CPU%d: Thermal LVT vector (%#x) already "
				"installed\n",
			cpu, (h & APIC_VECTOR_MASK));
		return; /* -EBUSY */
	}

	/* The temperature transition interrupt handler setup */
	h = THERMAL_APIC_VECTOR;		/* our delivery vector */
	h |= (APIC_DM_FIXED | APIC_LVT_MASKED);	/* we'll mask till we're ready */
	apic_write(APIC_LVTTHMR, h);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT, l | 0x03 , h);

	/* ok we're good to go... */
	vendor_thermal_interrupt = intel_thermal_interrupt;

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | (1<<3), h);

	l = apic_read(APIC_LVTTHMR);
	apic_write(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);
	printk(KERN_INFO "CPU%d: Thermal monitoring enabled\n", cpu);

	/* enable thermal throttle processing */
	atomic_set(&therm_throt_en, 1);
	return;
}
#endif /* CONFIG_X86_MCE_P4THERMAL */


/* P4/Xeon Extended MCE MSR retrieval, return 0 if unsupported */
static inline void intel_get_extended_msrs(struct intel_mce_extended_msrs *r)
{
	u32 h;

	rdmsr(MSR_IA32_MCG_EAX, r->eax, h);
	rdmsr(MSR_IA32_MCG_EBX, r->ebx, h);
	rdmsr(MSR_IA32_MCG_ECX, r->ecx, h);
	rdmsr(MSR_IA32_MCG_EDX, r->edx, h);
	rdmsr(MSR_IA32_MCG_ESI, r->esi, h);
	rdmsr(MSR_IA32_MCG_EDI, r->edi, h);
	rdmsr(MSR_IA32_MCG_EBP, r->ebp, h);
	rdmsr(MSR_IA32_MCG_ESP, r->esp, h);
	rdmsr(MSR_IA32_MCG_EFLAGS, r->eflags, h);
	rdmsr(MSR_IA32_MCG_EIP, r->eip, h);
}

static void intel_machine_check(struct pt_regs *regs, long error_code)
{
	int recover = 1;
	u32 alow, ahigh, high, low;
	u32 mcgstl, mcgsth;
	int i;

	rdmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
	if (mcgstl & (1<<0))	/* Recoverable ? */
		recover = 0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x\n",
		smp_processor_id(), mcgsth, mcgstl);

	if (mce_num_extended_msrs > 0) {
		struct intel_mce_extended_msrs dbg;
		intel_get_extended_msrs(&dbg);
		printk(KERN_DEBUG "CPU %d: EIP: %08x EFLAGS: %08x\n"
			"\teax: %08x ebx: %08x ecx: %08x edx: %08x\n"
			"\tesi: %08x edi: %08x ebp: %08x esp: %08x\n",
			smp_processor_id(), dbg.eip, dbg.eflags,
			dbg.eax, dbg.ebx, dbg.ecx, dbg.edx,
			dbg.esi, dbg.edi, dbg.ebp, dbg.esp);
	}

	for (i = 0; i < nr_mce_banks; i++) {
		rdmsr(MSR_IA32_MC0_STATUS+i*4, low, high);
		if (high & (1<<31)) {
			char misc[20];
			char addr[24];
			misc[0] = addr[0] = '\0';
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
	 * for errors if the OS could not log the error.
	 */
	for (i = 0; i < nr_mce_banks; i++) {
		u32 msr;
		msr = MSR_IA32_MC0_STATUS+i*4;
		rdmsr(msr, low, high);
		if (high&(1<<31)) {
			/* Clear it */
			wrmsr(msr, 0UL, 0UL);
			/* Serialize */
			wmb();
			add_taint(TAINT_MACHINE_CHECK);
		}
	}
	mcgstl &= ~(1<<2);
	wrmsr(MSR_IA32_MCG_STATUS, mcgstl, mcgsth);
}


void intel_p4_mcheck_init(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int i;

	machine_check_vector = intel_machine_check;
	wmb();

	printk(KERN_INFO "Intel machine check architecture supported.\n");
	rdmsr(MSR_IA32_MCG_CAP, l, h);
	if (l & (1<<8))	/* Control register present ? */
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);
	nr_mce_banks = l & 0xff;

	for (i = 0; i < nr_mce_banks; i++) {
		wrmsr(MSR_IA32_MC0_CTL+4*i, 0xffffffff, 0xffffffff);
		wrmsr(MSR_IA32_MC0_STATUS+4*i, 0x0, 0x0);
	}

	set_in_cr4(X86_CR4_MCE);
	printk(KERN_INFO "Intel machine check reporting enabled on CPU#%d.\n",
		smp_processor_id());

	/* Check for P4/Xeon extended MCE MSRs */
	rdmsr(MSR_IA32_MCG_CAP, l, h);
	if (l & (1<<9))	{/* MCG_EXT_P */
		mce_num_extended_msrs = (l >> 16) & 0xff;
		printk(KERN_INFO "CPU%d: Intel P4/Xeon Extended MCE MSRs (%d)"
				" available\n",
			smp_processor_id(), mce_num_extended_msrs);

#ifdef CONFIG_X86_MCE_P4THERMAL
		/* Check for P4/Xeon Thermal monitor */
		intel_init_thermal(c);
#endif
	}
}
