#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/dmar.h>

#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/genapic.h>

static int x2apic_phys;

static int set_x2apic_phys_mode(char *arg)
{
	x2apic_phys = 1;
	return 0;
}
early_param("x2apic_phys", set_x2apic_phys_mode);

static int __init x2apic_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (cpu_has_x2apic && x2apic_phys)
		return 1;

	return 0;
}

/* Start with all IRQs pointing to boot CPU.  IRQ balancing will shift them. */

static cpumask_t x2apic_target_cpus(void)
{
	return cpumask_of_cpu(0);
}

static cpumask_t x2apic_vector_allocation_domain(int cpu)
{
	cpumask_t domain = CPU_MASK_NONE;
	cpu_set(cpu, domain);
	return domain;
}

static void __x2apic_send_IPI_dest(unsigned int apicid, int vector,
				   unsigned int dest)
{
	unsigned long cfg;

	cfg = __prepare_ICR(0, vector, dest);

	/*
	 * send the IPI.
	 */
	x2apic_icr_write(cfg, apicid);
}

static void x2apic_send_IPI_mask(cpumask_t mask, int vector)
{
	unsigned long flags;
	unsigned long query_cpu;

	local_irq_save(flags);
	for_each_cpu_mask(query_cpu, mask) {
		__x2apic_send_IPI_dest(per_cpu(x86_cpu_to_apicid, query_cpu),
				       vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

static void x2apic_send_IPI_allbutself(int vector)
{
	cpumask_t mask = cpu_online_map;

	cpu_clear(smp_processor_id(), mask);

	if (!cpus_empty(mask))
		x2apic_send_IPI_mask(mask, vector);
}

static void x2apic_send_IPI_all(int vector)
{
	x2apic_send_IPI_mask(cpu_online_map, vector);
}

static int x2apic_apic_id_registered(void)
{
	return 1;
}

static unsigned int x2apic_cpu_mask_to_apicid(cpumask_t cpumask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	cpu = first_cpu(cpumask);
	if ((unsigned)cpu < NR_CPUS)
		return per_cpu(x86_cpu_to_apicid, cpu);
	else
		return BAD_APICID;
}

static unsigned int get_apic_id(unsigned long x)
{
	unsigned int id;

	id = x;
	return id;
}

static unsigned long set_apic_id(unsigned int id)
{
	unsigned long x;

	x = id;
	return x;
}

static unsigned int x2apic_read_id(void)
{
	return apic_read(APIC_ID);
}

static unsigned int phys_pkg_id(int index_msb)
{
	return x2apic_read_id() >> index_msb;
}

void x2apic_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

void init_x2apic_ldr(void)
{
	return;
}

struct genapic apic_x2apic_phys = {
	.name = "physical x2apic",
	.acpi_madt_oem_check = x2apic_acpi_madt_oem_check,
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.target_cpus = x2apic_target_cpus,
	.vector_allocation_domain = x2apic_vector_allocation_domain,
	.apic_id_registered = x2apic_apic_id_registered,
	.init_apic_ldr = init_x2apic_ldr,
	.send_IPI_all = x2apic_send_IPI_all,
	.send_IPI_allbutself = x2apic_send_IPI_allbutself,
	.send_IPI_mask = x2apic_send_IPI_mask,
	.send_IPI_self = x2apic_send_IPI_self,
	.cpu_mask_to_apicid = x2apic_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,
	.get_apic_id = get_apic_id,
	.set_apic_id = set_apic_id,
	.apic_id_mask = (0xFFFFFFFFu),
};
