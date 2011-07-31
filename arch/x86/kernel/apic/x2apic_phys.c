#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/dmar.h>

#include <asm/smp.h>
#include <asm/apic.h>
#include <asm/ipi.h>

int x2apic_phys;

static int set_x2apic_phys_mode(char *arg)
{
	x2apic_phys = 1;
	return 0;
}
early_param("x2apic_phys", set_x2apic_phys_mode);

static int x2apic_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (x2apic_phys)
		return x2apic_enabled();
	else
		return 0;
}

/*
 * need to use more than cpu 0, because we need more vectors when
 * MSI-X are used.
 */
static const struct cpumask *x2apic_target_cpus(void)
{
	return cpu_online_mask;
}

static void x2apic_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	cpumask_clear(retmask);
	cpumask_set_cpu(cpu, retmask);
}

static void __x2apic_send_IPI_dest(unsigned int apicid, int vector,
				   unsigned int dest)
{
	unsigned long cfg;

	cfg = __prepare_ICR(0, vector, dest);

	/*
	 * send the IPI.
	 */
	native_x2apic_icr_write(cfg, apicid);
}

static void x2apic_send_IPI_mask(const struct cpumask *mask, int vector)
{
	unsigned long query_cpu;
	unsigned long flags;

	x2apic_wrmsr_fence();

	local_irq_save(flags);
	for_each_cpu(query_cpu, mask) {
		__x2apic_send_IPI_dest(per_cpu(x86_cpu_to_apicid, query_cpu),
				       vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

static void
 x2apic_send_IPI_mask_allbutself(const struct cpumask *mask, int vector)
{
	unsigned long this_cpu = smp_processor_id();
	unsigned long query_cpu;
	unsigned long flags;

	x2apic_wrmsr_fence();

	local_irq_save(flags);
	for_each_cpu(query_cpu, mask) {
		if (query_cpu != this_cpu)
			__x2apic_send_IPI_dest(
				per_cpu(x86_cpu_to_apicid, query_cpu),
				vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

static void x2apic_send_IPI_allbutself(int vector)
{
	unsigned long this_cpu = smp_processor_id();
	unsigned long query_cpu;
	unsigned long flags;

	x2apic_wrmsr_fence();

	local_irq_save(flags);
	for_each_online_cpu(query_cpu) {
		if (query_cpu == this_cpu)
			continue;
		__x2apic_send_IPI_dest(per_cpu(x86_cpu_to_apicid, query_cpu),
				       vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

static void x2apic_send_IPI_all(int vector)
{
	x2apic_send_IPI_mask(cpu_online_mask, vector);
}

static int x2apic_apic_id_registered(void)
{
	return 1;
}

static unsigned int x2apic_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	int cpu = cpumask_first(cpumask);

	if ((unsigned)cpu < nr_cpu_ids)
		return per_cpu(x86_cpu_to_apicid, cpu);
	else
		return BAD_APICID;
}

static unsigned int
x2apic_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			      const struct cpumask *andmask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	for_each_cpu_and(cpu, cpumask, andmask) {
		if (cpumask_test_cpu(cpu, cpu_online_mask))
			break;
	}

	return per_cpu(x86_cpu_to_apicid, cpu);
}

static unsigned int x2apic_phys_get_apic_id(unsigned long x)
{
	return x;
}

static unsigned long set_apic_id(unsigned int id)
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

static void init_x2apic_ldr(void)
{
}

struct apic apic_x2apic_phys = {

	.name				= "physical x2apic",
	.probe				= NULL,
	.acpi_madt_oem_check		= x2apic_acpi_madt_oem_check,
	.apic_id_registered		= x2apic_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	.irq_dest_mode			= 0, /* physical */

	.target_cpus			= x2apic_target_cpus,
	.disable_esr			= 0,
	.dest_logical			= 0,
	.check_apicid_used		= NULL,
	.check_apicid_present		= NULL,

	.vector_allocation_domain	= x2apic_vector_allocation_domain,
	.init_apic_ldr			= init_x2apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.multi_timer_check		= NULL,
	.apicid_to_node			= NULL,
	.cpu_to_logical_apicid		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= x2apic_phys_pkg_id,
	.mps_oem_check			= NULL,

	.get_apic_id			= x2apic_phys_get_apic_id,
	.set_apic_id			= set_apic_id,
	.apic_id_mask			= 0xFFFFFFFFu,

	.cpu_mask_to_apicid		= x2apic_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= x2apic_cpu_mask_to_apicid_and,

	.send_IPI_mask			= x2apic_send_IPI_mask,
	.send_IPI_mask_allbutself	= x2apic_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= x2apic_send_IPI_allbutself,
	.send_IPI_all			= x2apic_send_IPI_all,
	.send_IPI_self			= x2apic_send_IPI_self,

	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,
	.wait_for_init_deassert		= NULL,
	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= NULL,

	.read				= native_apic_msr_read,
	.write				= native_apic_msr_write,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
	.wait_icr_idle			= native_x2apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_x2apic_wait_icr_idle,
};
