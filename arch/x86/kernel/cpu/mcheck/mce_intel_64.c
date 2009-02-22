/*
 * Intel specific MCE features.
 * Copyright 2004 Zwane Mwaikambo <zwane@linuxpower.ca>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/mce.h>
#include <asm/hw_irq.h>
#include <asm/idle.h>
#include <asm/therm_throt.h>

asmlinkage void smp_thermal_interrupt(void)
{
	__u64 msr_val;

	ack_APIC_irq();

	exit_idle();
	irq_enter();

	rdmsrl(MSR_IA32_THERM_STATUS, msr_val);
	if (therm_throt_process(msr_val & 1))
		mce_log_therm_throt_event(smp_processor_id(), msr_val);

	inc_irq_stat(irq_thermal_count);
	irq_exit();
}

static void intel_init_thermal(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int tm2 = 0;
	unsigned int cpu = smp_processor_id();

	if (!cpu_has(c, X86_FEATURE_ACPI))
		return;

	if (!cpu_has(c, X86_FEATURE_ACC))
		return;

	/* first check if TM1 is already enabled by the BIOS, in which
	 * case there might be some SMM goo which handles it, so we can't even
	 * put a handler since it might be delivered via SMI already.
	 */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	h = apic_read(APIC_LVTTHMR);
	if ((l & (1 << 3)) && (h & APIC_DM_SMI)) {
		printk(KERN_DEBUG
		       "CPU%d: Thermal monitoring handled by SMI\n", cpu);
		return;
	}

	if (cpu_has(c, X86_FEATURE_TM2) && (l & (1 << 13)))
		tm2 = 1;

	if (h & APIC_VECTOR_MASK) {
		printk(KERN_DEBUG
		       "CPU%d: Thermal LVT vector (%#x) already "
		       "installed\n", cpu, (h & APIC_VECTOR_MASK));
		return;
	}

	h = THERMAL_APIC_VECTOR;
	h |= (APIC_DM_FIXED | APIC_LVT_MASKED);
	apic_write(APIC_LVTTHMR, h);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT, l | 0x03, h);

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | (1 << 3), h);

	l = apic_read(APIC_LVTTHMR);
	apic_write(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);
	printk(KERN_INFO "CPU%d: Thermal monitoring enabled (%s)\n",
		cpu, tm2 ? "TM2" : "TM1");

	/* enable thermal throttle processing */
	atomic_set(&therm_throt_en, 1);
	return;
}

void mce_intel_feature_init(struct cpuinfo_x86 *c)
{
	intel_init_thermal(c);
}
