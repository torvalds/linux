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
 * Keep track of assigned, disabled and rejected CPUs. Present assigned
 * with 1 as CPU #0 is reserved for the boot CPU.
 */
static struct {
	unsigned int		nr_assigned_cpus;
	unsigned int		nr_disabled_cpus;
	unsigned int		nr_rejected_cpus;
	u32			boot_cpu_apic_id;
} topo_info __read_mostly = {
	.nr_assigned_cpus	= 1,
	.boot_cpu_apic_id	= BAD_APICID,
};

/*
 * Processor to be disabled specified by kernel parameter
 * disable_cpu_apicid=<int>, mostly used for the kdump 2nd kernel to
 * avoid undefined behaviour caused by sending INIT from AP to BSP.
 */
static u32 disabled_cpu_apicid __ro_after_init = BAD_APICID;

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

	for (cpu = 0; cpu < topo_info.nr_assigned_cpus; cpu++)
		cpu_mark_primary_thread(cpu, cpuid_to_apicid[cpu]);
	return 0;
}
early_initcall(smp_init_primary_thread_mask);
#else
static inline void cpu_mark_primary_thread(unsigned int cpu, unsigned int apicid) { }
#endif

static int __initdata setup_possible_cpus = -1;

/*
 * cpu_possible_mask should be static, it cannot change as cpu's
 * are onlined, or offlined. The reason is per-cpu data-structures
 * are allocated by some modules at init time, and don't expect to
 * do this dynamically on cpu arrival/departure.
 * cpu_present_mask on the other hand can change dynamically.
 * In case when cpu_hotplug is not compiled, then we resort to current
 * behaviour, which is cpu_possible == cpu_present.
 * - Ashok Raj
 *
 * Three ways to find out the number of additional hotplug CPUs:
 * - If the BIOS specified disabled CPUs in ACPI/mptables use that.
 * - The user can overwrite it with possible_cpus=NUM
 * - Otherwise don't reserve additional CPUs.
 * We do this because additional CPUs waste a lot of memory.
 * -AK
 */
__init void prefill_possible_map(void)
{
	unsigned int num_processors = topo_info.nr_assigned_cpus;
	unsigned int disabled_cpus = topo_info.nr_disabled_cpus;
	int i, possible;

	i = setup_max_cpus ?: 1;
	if (setup_possible_cpus == -1) {
		possible = topo_info.nr_assigned_cpus;
#ifdef CONFIG_HOTPLUG_CPU
		if (setup_max_cpus)
			possible += num_processors;
#else
		if (possible > i)
			possible = i;
#endif
	} else
		possible = setup_possible_cpus;

	total_cpus = max_t(int, possible, num_processors + disabled_cpus);

	/* nr_cpu_ids could be reduced via nr_cpus= */
	if (possible > nr_cpu_ids) {
		pr_warn("%d Processors exceeds NR_CPUS limit of %u\n",
			possible, nr_cpu_ids);
		possible = nr_cpu_ids;
	}

#ifdef CONFIG_HOTPLUG_CPU
	if (!setup_max_cpus)
#endif
	if (possible > i) {
		pr_warn("%d Processors exceeds max_cpus limit of %u\n",
			possible, setup_max_cpus);
		possible = i;
	}

	set_nr_cpu_ids(possible);

	pr_info("Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max_t(int, possible - num_processors, 0));

	reset_cpu_possible_mask();

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
}

static int topo_lookup_cpuid(u32 apic_id)
{
	int i;

	/* CPU# to APICID mapping is persistent once it is established */
	for (i = 0; i < topo_info.nr_assigned_cpus; i++) {
		if (cpuid_to_apicid[i] == apic_id)
			return i;
	}
	return -ENODEV;
}

static int topo_get_cpunr(u32 apic_id)
{
	int cpu = topo_lookup_cpuid(apic_id);

	if (cpu >= 0)
		return cpu;

	return topo_info.nr_assigned_cpus++;
}

static void topo_set_cpuids(unsigned int cpu, u32 apic_id, u32 acpi_id)
{
#if defined(CONFIG_SMP) || defined(CONFIG_X86_64)
	early_per_cpu(x86_cpu_to_apicid, cpu) = apic_id;
	early_per_cpu(x86_cpu_to_acpiid, cpu) = acpi_id;
#endif
	cpuid_to_apicid[cpu] = apic_id;

	set_cpu_possible(cpu, true);
	set_cpu_present(cpu, true);

	if (system_state != SYSTEM_BOOTING)
		cpu_mark_primary_thread(cpu, apic_id);
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
		topo_info.nr_rejected_cpus++;
		return;
	}

	if (disabled_cpu_apicid == apic_id) {
		pr_info("Disabling CPU as requested via 'disable_cpu_apicid=0x%x'.\n", apic_id);
		topo_info.nr_rejected_cpus++;
		return;
	}

	/* CPU numbers exhausted? */
	if (apic_id != topo_info.boot_cpu_apic_id && topo_info.nr_assigned_cpus >= nr_cpu_ids) {
		pr_warn_once("CPU limit of %d reached. Ignoring further CPUs\n", nr_cpu_ids);
		topo_info.nr_rejected_cpus++;
		return;
	}

	if (present) {
		set_bit(apic_id, phys_cpu_present_map);

		/*
		 * Double registration is valid in case of the boot CPU
		 * APIC because that is registered before the enumeration
		 * of the APICs via firmware parsers or VM guest
		 * mechanisms.
		 */
		if (apic_id == topo_info.boot_cpu_apic_id)
			cpu = 0;
		else
			cpu = topo_get_cpunr(apic_id);
		topo_set_cpuids(cpu, apic_id, acpi_id);
	} else {
		topo_info.nr_disabled_cpus++;
	}
}

/**
 * topology_register_boot_apic - Register the boot CPU APIC
 * @apic_id:	The APIC ID to set up
 *
 * Separate so CPU #0 can be assigned
 */
void __init topology_register_boot_apic(u32 apic_id)
{
	WARN_ON_ONCE(topo_info.boot_cpu_apic_id != BAD_APICID);

	topo_info.boot_cpu_apic_id = apic_id;
	topology_register_apic(apic_id, CPU_ACPIID_INVALID, true);
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
		if (topo_info.nr_assigned_cpus >= nr_cpu_ids)
			return -ENOSPC;

		cpu = topo_assign_cpunr(apic_id);
	}
	set_bit(apic_id, phys_cpu_present_map);
	topo_set_cpuids(cpu, apic_id, acpi_id);
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
}
#endif

static int __init _setup_possible_cpus(char *str)
{
	get_option(&str, &setup_possible_cpus);
	return 0;
}
early_param("possible_cpus", _setup_possible_cpus);

static int __init apic_set_disabled_cpu_apicid(char *arg)
{
	if (!arg || !get_option(&arg, &disabled_cpu_apicid))
		return -EINVAL;

	return 0;
}
early_param("disable_cpu_apicid", apic_set_disabled_cpu_apicid);
