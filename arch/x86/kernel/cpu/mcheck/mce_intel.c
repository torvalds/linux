/*
 * Common code for Intel machine checks
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/therm_throt.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/apic.h>
#include <asm/msr.h>

#include "mce.h"

void intel_init_thermal(struct cpuinfo_x86 *c)
{
	unsigned int cpu = smp_processor_id();
	int tm2 = 0;
	u32 l, h;

	/* Thermal monitoring depends on ACPI and clock modulation*/
	if (!cpu_has(c, X86_FEATURE_ACPI) || !cpu_has(c, X86_FEATURE_ACC))
		return;

	/*
	 * First check if its enabled already, in which case there might
	 * be some SMM goo which handles it, so we can't even put a handler
	 * since it might be delivered via SMI already:
	 */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	h = apic_read(APIC_LVTTHMR);
	if ((l & MSR_IA32_MISC_ENABLE_TM1) && (h & APIC_DM_SMI)) {
		printk(KERN_DEBUG
		       "CPU%d: Thermal monitoring handled by SMI\n", cpu);
		return;
	}

	if (cpu_has(c, X86_FEATURE_TM2) && (l & MSR_IA32_MISC_ENABLE_TM2))
		tm2 = 1;

	/* Check whether a vector already exists */
	if (h & APIC_VECTOR_MASK) {
		printk(KERN_DEBUG
		       "CPU%d: Thermal LVT vector (%#x) already installed\n",
		       cpu, (h & APIC_VECTOR_MASK));
		return;
	}

	/* We'll mask the thermal vector in the lapic till we're ready: */
	h = THERMAL_APIC_VECTOR | APIC_DM_FIXED | APIC_LVT_MASKED;
	apic_write(APIC_LVTTHMR, h);

	rdmsr(MSR_IA32_THERM_INTERRUPT, l, h);
	wrmsr(MSR_IA32_THERM_INTERRUPT,
		l | (THERM_INT_LOW_ENABLE | THERM_INT_HIGH_ENABLE), h);

	intel_set_thermal_handler();

	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
	wrmsr(MSR_IA32_MISC_ENABLE, l | MSR_IA32_MISC_ENABLE_TM1, h);

	/* Unmask the thermal vector: */
	l = apic_read(APIC_LVTTHMR);
	apic_write(APIC_LVTTHMR, l & ~APIC_LVT_MASKED);

	printk(KERN_INFO "CPU%d: Thermal monitoring enabled (%s)\n",
	       cpu, tm2 ? "TM2" : "TM1");

	/* enable thermal throttle processing */
	atomic_set(&therm_throt_en, 1);
}
