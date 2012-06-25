#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/dmar.h>
#include <linux/cpu.h>

#include <asm/smp.h>
#include <asm/x2apic.h>

static DEFINE_PER_CPU(u32, x86_cpu_to_logical_apicid);
static DEFINE_PER_CPU(cpumask_var_t, cpus_in_cluster);
static DEFINE_PER_CPU(cpumask_var_t, ipi_mask);

static int x2apic_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return x2apic_enabled();
}

static inline u32 x2apic_cluster(int cpu)
{
	return per_cpu(x86_cpu_to_logical_apicid, cpu) >> 16;
}

static void
__x2apic_send_IPI_mask(const struct cpumask *mask, int vector, int apic_dest)
{
	struct cpumask *cpus_in_cluster_ptr;
	struct cpumask *ipi_mask_ptr;
	unsigned int cpu, this_cpu;
	unsigned long flags;
	u32 dest;

	x2apic_wrmsr_fence();

	local_irq_save(flags);

	this_cpu = smp_processor_id();

	/*
	 * We are to modify mask, so we need an own copy
	 * and be sure it's manipulated with irq off.
	 */
	ipi_mask_ptr = __raw_get_cpu_var(ipi_mask);
	cpumask_copy(ipi_mask_ptr, mask);

	/*
	 * The idea is to send one IPI per cluster.
	 */
	for_each_cpu(cpu, ipi_mask_ptr) {
		unsigned long i;

		cpus_in_cluster_ptr = per_cpu(cpus_in_cluster, cpu);
		dest = 0;

		/* Collect cpus in cluster. */
		for_each_cpu_and(i, ipi_mask_ptr, cpus_in_cluster_ptr) {
			if (apic_dest == APIC_DEST_ALLINC || i != this_cpu)
				dest |= per_cpu(x86_cpu_to_logical_apicid, i);
		}

		if (!dest)
			continue;

		__x2apic_send_IPI_dest(dest, vector, apic->dest_logical);
		/*
		 * Cluster sibling cpus should be discared now so
		 * we would not send IPI them second time.
		 */
		cpumask_andnot(ipi_mask_ptr, ipi_mask_ptr, cpus_in_cluster_ptr);
	}

	local_irq_restore(flags);
}

static void x2apic_send_IPI_mask(const struct cpumask *mask, int vector)
{
	__x2apic_send_IPI_mask(mask, vector, APIC_DEST_ALLINC);
}

static void
x2apic_send_IPI_mask_allbutself(const struct cpumask *mask, int vector)
{
	__x2apic_send_IPI_mask(mask, vector, APIC_DEST_ALLBUT);
}

static void x2apic_send_IPI_allbutself(int vector)
{
	__x2apic_send_IPI_mask(cpu_online_mask, vector, APIC_DEST_ALLBUT);
}

static void x2apic_send_IPI_all(int vector)
{
	__x2apic_send_IPI_mask(cpu_online_mask, vector, APIC_DEST_ALLINC);
}

static int
x2apic_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			      const struct cpumask *andmask,
			      unsigned int *apicid)
{
	u32 dest = 0;
	u16 cluster;
	int i;

	for_each_cpu_and(i, cpumask, andmask) {
		if (!cpumask_test_cpu(i, cpu_online_mask))
			continue;
		dest = per_cpu(x86_cpu_to_logical_apicid, i);
		cluster = x2apic_cluster(i);
		break;
	}

	if (!dest)
		return -EINVAL;

	for_each_cpu_and(i, cpumask, andmask) {
		if (!cpumask_test_cpu(i, cpu_online_mask))
			continue;
		if (cluster != x2apic_cluster(i))
			continue;
		dest |= per_cpu(x86_cpu_to_logical_apicid, i);
	}

	*apicid = dest;

	return 0;
}

static void init_x2apic_ldr(void)
{
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;

	per_cpu(x86_cpu_to_logical_apicid, this_cpu) = apic_read(APIC_LDR);

	__cpu_set(this_cpu, per_cpu(cpus_in_cluster, this_cpu));
	for_each_online_cpu(cpu) {
		if (x2apic_cluster(this_cpu) != x2apic_cluster(cpu))
			continue;
		__cpu_set(this_cpu, per_cpu(cpus_in_cluster, cpu));
		__cpu_set(cpu, per_cpu(cpus_in_cluster, this_cpu));
	}
}

 /*
  * At CPU state changes, update the x2apic cluster sibling info.
  */
static int __cpuinit
update_clusterinfo(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned int this_cpu = (unsigned long)hcpu;
	unsigned int cpu;
	int err = 0;

	switch (action) {
	case CPU_UP_PREPARE:
		if (!zalloc_cpumask_var(&per_cpu(cpus_in_cluster, this_cpu),
					GFP_KERNEL)) {
			err = -ENOMEM;
		} else if (!zalloc_cpumask_var(&per_cpu(ipi_mask, this_cpu),
					       GFP_KERNEL)) {
			free_cpumask_var(per_cpu(cpus_in_cluster, this_cpu));
			err = -ENOMEM;
		}
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
		for_each_online_cpu(cpu) {
			if (x2apic_cluster(this_cpu) != x2apic_cluster(cpu))
				continue;
			__cpu_clear(this_cpu, per_cpu(cpus_in_cluster, cpu));
			__cpu_clear(cpu, per_cpu(cpus_in_cluster, this_cpu));
		}
		free_cpumask_var(per_cpu(cpus_in_cluster, this_cpu));
		free_cpumask_var(per_cpu(ipi_mask, this_cpu));
		break;
	}

	return notifier_from_errno(err);
}

static struct notifier_block __refdata x2apic_cpu_notifier = {
	.notifier_call = update_clusterinfo,
};

static int x2apic_init_cpu_notifier(void)
{
	int cpu = smp_processor_id();

	zalloc_cpumask_var(&per_cpu(cpus_in_cluster, cpu), GFP_KERNEL);
	zalloc_cpumask_var(&per_cpu(ipi_mask, cpu), GFP_KERNEL);

	BUG_ON(!per_cpu(cpus_in_cluster, cpu) || !per_cpu(ipi_mask, cpu));

	__cpu_set(cpu, per_cpu(cpus_in_cluster, cpu));
	register_hotcpu_notifier(&x2apic_cpu_notifier);
	return 1;
}

static int x2apic_cluster_probe(void)
{
	if (x2apic_mode)
		return x2apic_init_cpu_notifier();
	else
		return 0;
}

/*
 * Each x2apic cluster is an allocation domain.
 */
static void cluster_vector_allocation_domain(int cpu, struct cpumask *retmask,
					     const struct cpumask *mask)
{
	cpumask_and(retmask, mask, per_cpu(cpus_in_cluster, cpu));
}

static struct apic apic_x2apic_cluster = {

	.name				= "cluster x2apic",
	.probe				= x2apic_cluster_probe,
	.acpi_madt_oem_check		= x2apic_acpi_madt_oem_check,
	.apic_id_valid			= x2apic_apic_id_valid,
	.apic_id_registered		= x2apic_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	.irq_dest_mode			= 1, /* logical */

	.target_cpus			= online_target_cpus,
	.disable_esr			= 0,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= NULL,
	.check_apicid_present		= NULL,

	.vector_allocation_domain	= cluster_vector_allocation_domain,
	.init_apic_ldr			= init_x2apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.multi_timer_check		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= x2apic_phys_pkg_id,
	.mps_oem_check			= NULL,

	.get_apic_id			= x2apic_get_apic_id,
	.set_apic_id			= x2apic_set_apic_id,
	.apic_id_mask			= 0xFFFFFFFFu,

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
	.eoi_write			= native_apic_msr_eoi_write,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
	.wait_icr_idle			= native_x2apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_x2apic_wait_icr_idle,
};

apic_driver(apic_x2apic_cluster);
