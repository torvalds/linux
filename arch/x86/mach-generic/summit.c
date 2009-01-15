/*
 * APIC driver for the IBM "Summit" chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/summit/apicdef.h>
#include <linux/smp.h>
#include <asm/summit/apic.h>
#include <asm/summit/ipi.h>
#include <asm/summit/mpparse.h>
#include <asm/mach-default/mach_wakecpu.h>

static int probe_summit(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static void vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	*retmask = (cpumask_t){ { [0] = APIC_ALL_CPUS, } };
}

struct genapic apic_summit = APIC_INIT("summit", probe_summit);
