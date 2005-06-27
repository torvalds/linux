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

static DEFINE_PER_CPU(unsigned long, next_check);

asmlinkage void smp_thermal_interrupt(void)
{
	struct mce m;

	ack_APIC_irq();

	irq_enter();
	if (time_before(jiffies, __get_cpu_var(next_check)))
		goto done;

	__get_cpu_var(next_check) = jiffies + HZ*300;
	memset(&m, 0, sizeof(m));
	m.cpu = smp_processor_id();
	m.bank = MCE_THERMAL_BANK;
	rdtscll(m.tsc);
	rdmsrl(MSR_IA32_THERM_STATUS, m.status);
	if (m.status & 0x1) {
		printk(KERN_EMERG
			"CPU%d: Temperature above threshold, cpu clock throttled\n", m.cpu);
		add_taint(TAINT_MACHINE_CHECK);
	} else {
		printk(KERN_EMERG "CPU%d: Temperature/speed normal\n", m.cpu);
	}

	mce_log(&m);
done:
	irq_exit();
}

static void __cpuinit intel_init_thermal(struct cpuinfo_x86 *c)
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
	apic_write_around(APIC_LVTTHMR, h);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT, l | 0x03, h);

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | (1 << 3), h);

	l = apic_read(APIC_LVTTHMR);
	apic_write_around(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);
	printk(KERN_INFO "CPU%d: Thermal monitoring enabled (%s)\n",
		cpu, tm2 ? "TM2" : "TM1");
	return;
}

void __cpuinit mce_intel_feature_init(struct cpuinfo_x86 *c)
{
	intel_init_thermal(c);
}
