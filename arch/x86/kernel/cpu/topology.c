// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU/APIC topology
 *
 * The APIC IDs describe the system topology in multiple domain levels.
 * The CPUID topology parser provides the information which part of the
 * APIC ID is associated to the individual levels:
 *
 * [PACKAGE][DIEGRP][DIE][TILE][MODULE][CORE][THREAD]
 *
 * The root space contains the package (socket) IDs.
 *
 * Not enumerated levels consume 0 bits space, but conceptually they are
 * always represented. If e.g. only CORE and THREAD levels are enumerated
 * then the DIE, MODULE and TILE have the same physical ID as the PACKAGE.
 *
 * If SMT is not supported, then the THREAD domain is still used. It then
 * has the same physical ID as the CORE domain and is the only child of
 * the core domain.
 *
 * This allows a unified view on the system independent of the enumerated
 * domain levels without requiring any conditionals in the code.
 */
#define pr_fmt(fmt) "CPU topo: " fmt
#include <linux/cpu.h>

#include <xen/xen.h>

#include <asm/apic.h>
#include <asm/hypervisor.h>
#include <asm/io_apic.h>
#include <asm/mpspec.h>
#include <asm/smp.h>

#include "cpu.h"

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
u32 cpuid_to_apicid[] __ro_after_init = { [0 ... NR_CPUS - 1] = BAD_APICID, };

/* Bitmaps to mark registered APICs at each topology domain */
static struct { DECLARE_BITMAP(map, MAX_LOCAL_APIC); } apic_maps[TOPO_MAX_DOMAIN] __ro_after_init;

/*
 * Keep track of assigned, disabled and rejected CPUs. Present assigned
 * with 1 as CPU #0 is reserved for the boot CPU.
 */
static struct {
	unsigned int		nr_assigned_cpus;
	unsigned int		nr_disabled_cpus;
	unsigned int		nr_rejected_cpus;
	u32			boot_cpu_apic_id;
	u32			real_bsp_apic_id;
} topo_info __ro_after_init = {
	.nr_assigned_cpus	= 1,
	.boot_cpu_apic_id	= BAD_APICID,
	.real_bsp_apic_id	= BAD_APICID,
};

#define domain_weight(_dom)	bitmap_weight(apic_maps[_dom].map, MAX_LOCAL_APIC)

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return phys_id == (u64)cpuid_to_apicid[cpu];
}

#ifdef CONFIG_SMP
static void cpu_mark_primary_thread(unsigned int cpu, unsigned int apicid)
{
	if (!(apicid & (__max_threads_per_core - 1)))
		cpumask_set_cpu(cpu, &__cpu_primary_thread_mask);
}
#else
static inline void cpu_mark_primary_thread(unsigned int cpu, unsigned int apicid) { }
#endif

/*
 * Convert the APIC ID to a domain level ID by masking out the low bits
 * below the domain level @dom.
 */
static inline u32 topo_apicid(u32 apicid, enum x86_topology_domains dom)
{
	if (dom == TOPO_SMT_DOMAIN)
		return apicid;
	return apicid & (UINT_MAX << x86_topo_system.dom_shifts[dom - 1]);
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

static __init int topo_get_cpunr(u32 apic_id)
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
	set_cpu_possible(cpu, true);
	set_cpu_present(cpu, true);
}

static __init bool check_for_real_bsp(u32 apic_id)
{
	/*
	 * There is no real good way to detect whether this a kdump()
	 * kernel, but except on the Voyager SMP monstrosity which is not
	 * longer supported, the real BSP APIC ID is the first one which is
	 * enumerated by firmware. That allows to detect whether the boot
	 * CPU is the real BSP. If it is not, then do not register the APIC
	 * because sending INIT to the real BSP would reset the whole
	 * system.
	 *
	 * The first APIC ID which is enumerated by firmware is detectable
	 * because the boot CPU APIC ID is registered before that without
	 * invoking this code.
	 */
	if (topo_info.real_bsp_apic_id != BAD_APICID)
		return false;

	if (apic_id == topo_info.boot_cpu_apic_id) {
		topo_info.real_bsp_apic_id = apic_id;
		return false;
	}

	pr_warn("Boot CPU APIC ID not the first enumerated APIC ID: %x > %x\n",
		topo_info.boot_cpu_apic_id, apic_id);
	pr_warn("Crash kernel detected. Disabling real BSP to prevent machine INIT\n");

	topo_info.real_bsp_apic_id = apic_id;
	return true;
}

static unsigned int topo_unit_count(u32 lvlid, enum x86_topology_domains at_level,
				    unsigned long *map)
{
	unsigned int id, end, cnt = 0;

	/* Calculate the exclusive end */
	end = lvlid + (1U << x86_topo_system.dom_shifts[at_level]);

	/* Unfortunately there is no bitmap_weight_range() */
	for (id = find_next_bit(map, end, lvlid); id < end; id = find_next_bit(map, end, ++id))
		cnt++;
	return cnt;
}

static __init void topo_register_apic(u32 apic_id, u32 acpi_id, bool present)
{
	int cpu, dom;

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

		cpuid_to_apicid[cpu] = apic_id;
		topo_set_cpuids(cpu, apic_id, acpi_id);
	} else {
		u32 pkgid = topo_apicid(apic_id, TOPO_PKG_DOMAIN);

		/*
		 * Check for present APICs in the same package when running
		 * on bare metal. Allow the bogosity in a guest.
		 */
		if (hypervisor_is_type(X86_HYPER_NATIVE) &&
		    topo_unit_count(pkgid, TOPO_PKG_DOMAIN, phys_cpu_present_map)) {
			pr_info_once("Ignoring hot-pluggable APIC ID %x in present package.\n",
				     apic_id);
			topo_info.nr_rejected_cpus++;
			return;
		}

		topo_info.nr_disabled_cpus++;
	}

	/* Register present and possible CPUs in the domain maps */
	for (dom = TOPO_SMT_DOMAIN; dom < TOPO_MAX_DOMAIN; dom++)
		set_bit(topo_apicid(apic_id, dom), apic_maps[dom].map);
}

/**
 * topology_register_apic - Register an APIC in early topology maps
 * @apic_id:	The APIC ID to set up
 * @acpi_id:	The ACPI ID associated to the APIC
 * @present:	True if the corresponding CPU is present
 */
void __init topology_register_apic(u32 apic_id, u32 acpi_id, bool present)
{
	if (apic_id >= MAX_LOCAL_APIC) {
		pr_err_once("APIC ID %x exceeds kernel limit of: %x\n", apic_id, MAX_LOCAL_APIC - 1);
		topo_info.nr_rejected_cpus++;
		return;
	}

	if (check_for_real_bsp(apic_id)) {
		topo_info.nr_rejected_cpus++;
		return;
	}

	/* CPU numbers exhausted? */
	if (apic_id != topo_info.boot_cpu_apic_id && topo_info.nr_assigned_cpus >= nr_cpu_ids) {
		pr_warn_once("CPU limit of %d reached. Ignoring further CPUs\n", nr_cpu_ids);
		topo_info.nr_rejected_cpus++;
		return;
	}

	topo_register_apic(apic_id, acpi_id, present);
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
	topo_register_apic(apic_id, CPU_ACPIID_INVALID, true);
}

/**
 * topology_get_logical_id - Retrieve the logical ID at a given topology domain level
 * @apicid:		The APIC ID for which to lookup the logical ID
 * @at_level:		The topology domain level to use
 *
 * @apicid must be a full APIC ID, not the normalized variant. It's valid to have
 * all bits below the domain level specified by @at_level to be clear. So both
 * real APIC IDs and backshifted normalized APIC IDs work correctly.
 *
 * Returns:
 *  - >= 0:	The requested logical ID
 *  - -ERANGE:	@apicid is out of range
 *  - -ENODEV:	@apicid is not registered
 */
int topology_get_logical_id(u32 apicid, enum x86_topology_domains at_level)
{
	/* Remove the bits below @at_level to get the proper level ID of @apicid */
	unsigned int lvlid = topo_apicid(apicid, at_level);

	if (lvlid >= MAX_LOCAL_APIC)
		return -ERANGE;
	if (!test_bit(lvlid, apic_maps[at_level].map))
		return -ENODEV;
	/* Get the number of set bits before @lvlid. */
	return bitmap_weight(apic_maps[at_level].map, lvlid);
}
EXPORT_SYMBOL_GPL(topology_get_logical_id);

/**
 * topology_unit_count - Retrieve the count of specified units at a given topology domain level
 * @apicid:		The APIC ID which specifies the search range
 * @which_units:	The domain level specifying the units to count
 * @at_level:		The domain level at which @which_units have to be counted
 *
 * This returns the number of possible units according to the enumerated
 * information.
 *
 * E.g. topology_count_units(apicid, TOPO_CORE_DOMAIN, TOPO_PKG_DOMAIN)
 * counts the number of possible cores in the package to which @apicid
 * belongs.
 *
 * @at_level must obviously be greater than @which_level to produce useful
 * results.  If @at_level is equal to @which_units the result is
 * unsurprisingly 1. If @at_level is less than @which_units the results
 * is by definition undefined and the function returns 0.
 */
unsigned int topology_unit_count(u32 apicid, enum x86_topology_domains which_units,
				 enum x86_topology_domains at_level)
{
	/* Remove the bits below @at_level to get the proper level ID of @apicid */
	unsigned int lvlid = topo_apicid(apicid, at_level);

	if (lvlid >= MAX_LOCAL_APIC)
		return 0;
	if (!test_bit(lvlid, apic_maps[at_level].map))
		return 0;
	if (which_units > at_level)
		return 0;
	if (which_units == at_level)
		return 1;
	return topo_unit_count(lvlid, at_level, apic_maps[which_units].map);
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

	/* Reject if the APIC ID was not registered during enumeration. */
	if (!test_bit(apic_id, apic_maps[TOPO_SMT_DOMAIN].map))
		return -ENODEV;

	cpu = topo_lookup_cpuid(apic_id);
	if (cpu < 0)
		return -ENOSPC;

	set_bit(apic_id, phys_cpu_present_map);
	topo_set_cpuids(cpu, apic_id, acpi_id);
	cpu_mark_primary_thread(cpu, apic_id);
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

#ifdef CONFIG_X86_LOCAL_APIC
static unsigned int max_possible_cpus __initdata = NR_CPUS;

/**
 * topology_apply_cmdline_limits_early - Apply topology command line limits early
 *
 * Ensure that command line limits are in effect before firmware parsing
 * takes place.
 */
void __init topology_apply_cmdline_limits_early(void)
{
	unsigned int possible = nr_cpu_ids;

	/* 'maxcpus=0' 'nosmp' 'nolapic' 'disableapic' 'noapic' */
	if (!setup_max_cpus || ioapic_is_disabled || apic_is_disabled)
		possible = 1;

	/* 'possible_cpus=N' */
	possible = min_t(unsigned int, max_possible_cpus, possible);

	if (possible < nr_cpu_ids) {
		pr_info("Limiting to %u possible CPUs\n", possible);
		set_nr_cpu_ids(possible);
	}
}

static __init bool restrict_to_up(void)
{
	if (!smp_found_config || ioapic_is_disabled)
		return true;
	/*
	 * XEN PV is special as it does not advertise the local APIC
	 * properly, but provides a fake topology for it so that the
	 * infrastructure works. So don't apply the restrictions vs. APIC
	 * here.
	 */
	if (xen_pv_domain())
		return false;

	return apic_is_disabled;
}

void __init topology_init_possible_cpus(void)
{
	unsigned int assigned = topo_info.nr_assigned_cpus;
	unsigned int disabled = topo_info.nr_disabled_cpus;
	unsigned int cnta, cntb, cpu, allowed = 1;
	unsigned int total = assigned + disabled;
	u32 apicid, firstid;

	/*
	 * If there was no APIC registered, then fake one so that the
	 * topology bitmap is populated. That ensures that the code below
	 * is valid and the various query interfaces can be used
	 * unconditionally. This does not affect the actual APIC code in
	 * any way because either the local APIC address has not been
	 * registered or the local APIC was disabled on the command line.
	 */
	if (topo_info.boot_cpu_apic_id == BAD_APICID)
		topology_register_boot_apic(0);

	if (!restrict_to_up()) {
		if (WARN_ON_ONCE(assigned > nr_cpu_ids)) {
			disabled += assigned - nr_cpu_ids;
			assigned = nr_cpu_ids;
		}
		allowed = min_t(unsigned int, total, nr_cpu_ids);
	}

	if (total > allowed)
		pr_warn("%u possible CPUs exceed the limit of %u\n", total, allowed);

	assigned = min_t(unsigned int, allowed, assigned);
	disabled = allowed - assigned;

	topo_info.nr_assigned_cpus = assigned;
	topo_info.nr_disabled_cpus = disabled;

	total_cpus = allowed;
	set_nr_cpu_ids(allowed);

	cnta = domain_weight(TOPO_PKG_DOMAIN);
	cntb = domain_weight(TOPO_DIE_DOMAIN);
	__max_logical_packages = cnta;
	__max_dies_per_package = 1U << (get_count_order(cntb) - get_count_order(cnta));

	pr_info("Max. logical packages: %3u\n", cnta);
	pr_info("Max. logical dies:     %3u\n", cntb);
	pr_info("Max. dies per package: %3u\n", __max_dies_per_package);

	cnta = domain_weight(TOPO_CORE_DOMAIN);
	cntb = domain_weight(TOPO_SMT_DOMAIN);
	/*
	 * Can't use order delta here as order(cnta) can be equal
	 * order(cntb) even if cnta != cntb.
	 */
	__max_threads_per_core = DIV_ROUND_UP(cntb, cnta);
	pr_info("Max. threads per core: %3u\n", __max_threads_per_core);

	firstid = find_first_bit(apic_maps[TOPO_SMT_DOMAIN].map, MAX_LOCAL_APIC);
	__num_cores_per_package = topology_unit_count(firstid, TOPO_CORE_DOMAIN, TOPO_PKG_DOMAIN);
	pr_info("Num. cores per package:   %3u\n", __num_cores_per_package);
	__num_threads_per_package = topology_unit_count(firstid, TOPO_SMT_DOMAIN, TOPO_PKG_DOMAIN);
	pr_info("Num. threads per package: %3u\n", __num_threads_per_package);

	pr_info("Allowing %u present CPUs plus %u hotplug CPUs\n", assigned, disabled);
	if (topo_info.nr_rejected_cpus)
		pr_info("Rejected CPUs %u\n", topo_info.nr_rejected_cpus);

	init_cpu_present(cpumask_of(0));
	init_cpu_possible(cpumask_of(0));

	/* Assign CPU numbers to non-present CPUs */
	for (apicid = 0; disabled; disabled--, apicid++) {
		apicid = find_next_andnot_bit(apic_maps[TOPO_SMT_DOMAIN].map, phys_cpu_present_map,
					      MAX_LOCAL_APIC, apicid);
		if (apicid >= MAX_LOCAL_APIC)
			break;
		cpuid_to_apicid[topo_info.nr_assigned_cpus++] = apicid;
	}

	for (cpu = 0; cpu < allowed; cpu++) {
		apicid = cpuid_to_apicid[cpu];

		set_cpu_possible(cpu, true);

		if (apicid == BAD_APICID)
			continue;

		cpu_mark_primary_thread(cpu, apicid);
		set_cpu_present(cpu, test_bit(apicid, phys_cpu_present_map));
	}
}

/*
 * Late SMP disable after sizing CPU masks when APIC/IOAPIC setup failed.
 */
void __init topology_reset_possible_cpus_up(void)
{
	init_cpu_present(cpumask_of(0));
	init_cpu_possible(cpumask_of(0));

	bitmap_zero(phys_cpu_present_map, MAX_LOCAL_APIC);
	if (topo_info.boot_cpu_apic_id != BAD_APICID)
		set_bit(topo_info.boot_cpu_apic_id, phys_cpu_present_map);
}

static int __init setup_possible_cpus(char *str)
{
	get_option(&str, &max_possible_cpus);
	return 0;
}
early_param("possible_cpus", setup_possible_cpus);
#endif
