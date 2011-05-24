/*
 * Common bits for X2APIC cluster/physical modes.
 */

#ifndef _ASM_X86_X2APIC_H
#define _ASM_X86_X2APIC_H

#include <asm/apic.h>
#include <asm/ipi.h>
#include <linux/cpumask.h>

/*
 * Need to use more than cpu 0, because we need more vectors
 * when MSI-X are used.
 */
static const struct cpumask *x2apic_target_cpus(void)
{
	return cpu_online_mask;
}

static int x2apic_apic_id_registered(void)
{
	return 1;
}

/*
 * For now each logical cpu is in its own vector allocation domain.
 */
static void x2apic_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	cpumask_clear(retmask);
	cpumask_set_cpu(cpu, retmask);
}

static void
__x2apic_send_IPI_dest(unsigned int apicid, int vector, unsigned int dest)
{
	unsigned long cfg = __prepare_ICR(0, vector, dest);
	native_x2apic_icr_write(cfg, apicid);
}

static unsigned int x2apic_get_apic_id(unsigned long id)
{
	return id;
}

static unsigned long x2apic_set_apic_id(unsigned int id)
{
	return id;
}

static int x2apic_phys_pkg_id(int initial_apicid, int index_msb)
{
	return initial_apicid >> index_msb;
}

static void x2apic_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

#endif /* _ASM_X86_X2APIC_H */
