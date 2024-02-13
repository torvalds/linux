// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cpu.h>

#include <xen/xen.h>

#include <asm/apic.h>
#include <asm/mpspec.h>
#include <asm/smp.h>

/*
 * Map cpu index to physical APIC ID
 */
DEFINE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_acpiid, CPU_ACPIID_INVALID);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_acpiid);

/* Bitmap of physically present CPUs. */
DECLARE_BITMAP(phys_cpu_present_map, MAX_LOCAL_APIC) __read_mostly;

/* Used for CPU number allocation and parallel CPU bringup */
u32 cpuid_to_apicid[] __read_mostly = { [0 ... NR_CPUS - 1] = BAD_APICID, };

/*
 * Processor to be disabled specified by kernel parameter
 * disable_cpu_apicid=<int>, mostly used for the kdump 2nd kernel to
 * avoid undefined behaviour caused by sending INIT from AP to BSP.
 */
static u32 disabled_cpu_apicid __ro_after_init = BAD_APICID;

unsigned int num_processors;
unsigned disabled_cpus;

/*
 * The number of allocated logical CPU IDs. Since logical CPU IDs are allocated
 * contiguously, it equals to current allocated max logical CPU ID plus 1.
 * All allocated CPU IDs should be in the [0, nr_logical_cpuids) range,
 * so the maximum of nr_logical_cpuids is nr_cpu_ids.
 *
 * NOTE: Reserve 0 for BSP.
 */
static int nr_logical_cpuids = 1;

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return phys_id == (u64)cpuid_to_apicid[cpu];
}

#ifdef CONFIG_SMP
static void cpu_mark_primary_thread(unsigned int cpu, unsigned int apicid)
{
	/* Isolate the SMT bit(s) in the APICID and check for 0 */
	u32 mask = (1U << (fls(smp_num_siblings) - 1)) - 1;

	if (smp_num_siblings == 1 || !(apicid & mask))
		cpumask_set_cpu(cpu, &__cpu_primary_thread_mask);
}

/*
 * Due to the utter mess of CPUID evaluation smp_num_siblings is not valid
 * during early boot. Initialize the primary thread mask before SMP
 * bringup.
 */
static int __init smp_init_primary_thread_mask(void)
{
	unsigned int cpu;

	/*
	 * XEN/PV provides either none or useless topology information.
	 * Pretend that all vCPUs are primary threads.
	 */
	if (xen_pv_domain()) {
		cpumask_copy(&__cpu_primary_thread_mask, cpu_possible_mask);
		return 0;
	}

	for (cpu = 0; cpu < nr_logical_cpuids; cpu++)
		cpu_mark_primary_thread(cpu, cpuid_to_apicid[cpu]);
	return 0;
}
early_initcall(smp_init_primary_thread_mask);
#else
static inline void cpu_mark_primary_thread(unsigned int cpu, unsigned int apicid) { }
#endif

static int topo_lookup_cpuid(u32 apic_id)
{
	int i;

	/* CPU# to APICID mapping is persistent once it is established */
	for (i = 0; i < nr_logical_cpuids; i++) {
		if (cpuid_to_apicid[i] == apic_id)
			return i;
	}
	return -ENODEV;
}

/*
 * Should use this API to allocate logical CPU IDs to keep nr_logical_cpuids
 * and cpuid_to_apicid[] synchronized.
 */
static int allocate_logical_cpuid(u32 apic_id)
{
	int cpu = topo_lookup_cpuid(apic_id);

	if (cpu >= 0)
		return cpu;

	/* Allocate a new cpuid. */
	if (nr_logical_cpuids >= nr_cpu_ids) {
		WARN_ONCE(1, "APIC: NR_CPUS/possible_cpus limit of %u reached. "
			     "Processor %d/0x%x and the rest are ignored.\n",
			     nr_cpu_ids, nr_logical_cpuids, apic_id);
		return -EINVAL;
	}

	cpuid_to_apicid[nr_logical_cpuids] = apic_id;
	return nr_logical_cpuids++;
}

static void cpu_update_apic(int cpu, u32 apicid)
{
#if defined(CONFIG_SMP) || defined(CONFIG_X86_64)
	early_per_cpu(x86_cpu_to_apicid, cpu) = apicid;
#endif
	set_cpu_possible(cpu, true);
	set_bit(apicid, phys_cpu_present_map);
	set_cpu_present(cpu, true);
	num_processors++;

	if (system_state != SYSTEM_BOOTING)
		cpu_mark_primary_thread(cpu, apicid);
}

static int generic_processor_info(int apicid)
{
	int cpu, max = nr_cpu_ids;

	/* The boot CPU must be set before MADT/MPTABLE parsing happens */
	if (cpuid_to_apicid[0] == BAD_APICID)
		panic("Boot CPU APIC not registered yet\n");

	if (apicid == boot_cpu_physical_apicid)
		return 0;

	if (disabled_cpu_apicid == apicid) {
		int thiscpu = num_processors + disabled_cpus;

		pr_warn("APIC: Disabling requested cpu. Processor %d/0x%x ignored.\n",
			thiscpu, apicid);

		disabled_cpus++;
		return -ENODEV;
	}

	if (num_processors >= nr_cpu_ids) {
		int thiscpu = max + disabled_cpus;

		pr_warn("APIC: NR_CPUS/possible_cpus limit of %i reached. "
			"Processor %d/0x%x ignored.\n", max, thiscpu, apicid);

		disabled_cpus++;
		return -EINVAL;
	}

	cpu = allocate_logical_cpuid(apicid);
	if (cpu < 0) {
		disabled_cpus++;
		return -EINVAL;
	}

	cpu_update_apic(cpu, apicid);
	return cpu;
}

/**
 * topology_register_apic - Register an APIC in early topology maps
 * @apic_id:	The APIC ID to set up
 * @acpi_id:	The ACPI ID associated to the APIC
 * @present:	True if the corresponding CPU is present
 */
void __init topology_register_apic(u32 apic_id, u32 acpi_id, bool present)
{
	int cpu;

	if (apic_id >= MAX_LOCAL_APIC) {
		pr_err_once("APIC ID %x exceeds kernel limit of: %x\n", apic_id, MAX_LOCAL_APIC - 1);
		return;
	}

	if (!present) {
		disabled_cpus++;
		return;
	}

	cpu = generic_processor_info(apic_id);
	if (cpu >= 0)
		early_per_cpu(x86_cpu_to_acpiid, cpu) = acpi_id;
}

/**
 * topology_register_boot_apic - Register the boot CPU APIC
 * @apic_id:	The APIC ID to set up
 *
 * Separate so CPU #0 can be assigned
 */
void __init topology_register_boot_apic(u32 apic_id)
{
	cpuid_to_apicid[0] = apic_id;
	cpu_update_apic(0, apic_id);
}

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/**
 * topology_hotplug_apic - Handle a physical hotplugged APIC after boot
 * @apic_id:	The APIC ID to set up
 * @acpi_id:	The ACPI ID associated to the APIC
 */
int topology_hotplug_apic(u32 apic_id, u32 acpi_id)
{
	int cpu;

	if (apic_id >= MAX_LOCAL_APIC)
		return -EINVAL;

	cpu = topo_lookup_cpuid(apic_id);
	if (cpu < 0) {
		cpu = generic_processor_info(apic_id);
		if (cpu >= 0)
			per_cpu(x86_cpu_to_acpiid, cpu) = acpi_id;
	}
	return cpu;
}

/**
 * topology_hotunplug_apic - Remove a physical hotplugged APIC after boot
 * @cpu:	The CPU number for which the APIC ID is removed
 */
void topology_hotunplug_apic(unsigned int cpu)
{
	u32 apic_id = cpuid_to_apicid[cpu];

	if (apic_id == BAD_APICID)
		return;

	per_cpu(x86_cpu_to_apicid, cpu) = BAD_APICID;
	clear_bit(apic_id, phys_cpu_present_map);
	set_cpu_present(cpu, false);
	num_processors--;
}
#endif

static int __init apic_set_disabled_cpu_apicid(char *arg)
{
	if (!arg || !get_option(&arg, &disabled_cpu_apicid))
		return -EINVAL;

	return 0;
}
early_param("disable_cpu_apicid", apic_set_disabled_cpu_apicid);
