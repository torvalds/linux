/*
 * Copyright (C) 2005 Intel Corporation
 * 	Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 * 	- Added _PDC for SMP C-states on Intel CPUs
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/sched.h>

#include <acpi/processor.h>
#include <asm/acpi.h>

/*
 * Initialize bm_flags based on the CPU cache properties
 * On SMP it depends on cache configuration
 * - When cache is not shared among all CPUs, we flush cache
 *   before entering C3.
 * - When cache is shared among all CPUs, we use bm_check
 *   mechanism as in UP case
 *
 * This routine is called only after all the CPUs are online
 */
void acpi_processor_power_init_bm_check(struct acpi_processor_flags *flags,
					unsigned int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	flags->bm_check = 0;
	if (num_online_cpus() == 1)
		flags->bm_check = 1;
	else if (c->x86_vendor == X86_VENDOR_INTEL) {
		/*
		 * Today all CPUs that support C3 share cache.
		 * TBD: This needs to look at cache shared map, once
		 * multi-core detection patch makes to the base.
		 */
		flags->bm_check = 1;
	}
}
EXPORT_SYMBOL(acpi_processor_power_init_bm_check);

/* The code below handles cstate entry with monitor-mwait pair on Intel*/

struct cstate_entry {
	struct {
		unsigned int eax;
		unsigned int ecx;
	} states[ACPI_PROCESSOR_MAX_POWER];
};
static struct cstate_entry *cpu_cstate_entry;	/* per CPU ptr */

static short mwait_supported[ACPI_PROCESSOR_MAX_POWER];

#define MWAIT_SUBSTATE_MASK	(0xf)
#define MWAIT_SUBSTATE_SIZE	(4)

#define CPUID_MWAIT_LEAF (5)
#define CPUID5_ECX_EXTENSIONS_SUPPORTED (0x1)
#define CPUID5_ECX_INTERRUPT_BREAK	(0x2)

#define MWAIT_ECX_INTERRUPT_BREAK	(0x1)

#define NATIVE_CSTATE_BEYOND_HALT	(2)

int acpi_processor_ffh_cstate_probe(unsigned int cpu,
		struct acpi_processor_cx *cx, struct acpi_power_register *reg)
{
	struct cstate_entry *percpu_entry;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	cpumask_t saved_mask;
	int retval;
	unsigned int eax, ebx, ecx, edx;
	unsigned int edx_part;
	unsigned int cstate_type; /* C-state type and not ACPI C-state type */
	unsigned int num_cstate_subtype;

	if (!cpu_cstate_entry || c->cpuid_level < CPUID_MWAIT_LEAF )
		return -1;

	if (reg->bit_offset != NATIVE_CSTATE_BEYOND_HALT)
		return -1;

	percpu_entry = per_cpu_ptr(cpu_cstate_entry, cpu);
	percpu_entry->states[cx->index].eax = 0;
	percpu_entry->states[cx->index].ecx = 0;

	/* Make sure we are running on right CPU */
	saved_mask = current->cpus_allowed;
	retval = set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
	if (retval)
		return -1;

	cpuid(CPUID_MWAIT_LEAF, &eax, &ebx, &ecx, &edx);

	/* Check whether this particular cx_type (in CST) is supported or not */
	cstate_type = (cx->address >> MWAIT_SUBSTATE_SIZE) + 1;
	edx_part = edx >> (cstate_type * MWAIT_SUBSTATE_SIZE);
	num_cstate_subtype = edx_part & MWAIT_SUBSTATE_MASK;

	retval = 0;
	if (num_cstate_subtype < (cx->address & MWAIT_SUBSTATE_MASK)) {
		retval = -1;
		goto out;
	}

	/* mwait ecx extensions INTERRUPT_BREAK should be supported for C2/C3 */
	if (!(ecx & CPUID5_ECX_EXTENSIONS_SUPPORTED) ||
	    !(ecx & CPUID5_ECX_INTERRUPT_BREAK)) {
		retval = -1;
		goto out;
	}
	percpu_entry->states[cx->index].ecx = MWAIT_ECX_INTERRUPT_BREAK;

	/* Use the hint in CST */
	percpu_entry->states[cx->index].eax = cx->address;

	if (!mwait_supported[cstate_type]) {
		mwait_supported[cstate_type] = 1;
		printk(KERN_DEBUG "Monitor-Mwait will be used to enter C-%d "
		       "state\n", cx->type);
	}
	snprintf(cx->desc, ACPI_CX_DESC_LEN, "ACPI FFH INTEL MWAIT 0x%x",
		 cx->address);

out:
	set_cpus_allowed_ptr(current, &saved_mask);
	return retval;
}
EXPORT_SYMBOL_GPL(acpi_processor_ffh_cstate_probe);

void acpi_processor_ffh_cstate_enter(struct acpi_processor_cx *cx)
{
	unsigned int cpu = smp_processor_id();
	struct cstate_entry *percpu_entry;

	percpu_entry = per_cpu_ptr(cpu_cstate_entry, cpu);
	mwait_idle_with_hints(percpu_entry->states[cx->index].eax,
	                      percpu_entry->states[cx->index].ecx);
}
EXPORT_SYMBOL_GPL(acpi_processor_ffh_cstate_enter);

static int __init ffh_cstate_init(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;
	if (c->x86_vendor != X86_VENDOR_INTEL)
		return -1;

	cpu_cstate_entry = alloc_percpu(struct cstate_entry);
	return 0;
}

static void __exit ffh_cstate_exit(void)
{
	free_percpu(cpu_cstate_entry);
	cpu_cstate_entry = NULL;
}

arch_initcall(ffh_cstate_init);
__exitcall(ffh_cstate_exit);
