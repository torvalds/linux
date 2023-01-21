// SPDX-License-Identifier: GPL-2.0-or-later
 /*
 *	x86 SMP booting functions
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@lxorguk.ukuu.org.uk>
 *	(c) 1998, 1999, 2000, 2009 Ingo Molnar <mingo@redhat.com>
 *	Copyright 2001 Andi Kleen, SuSE Labs.
 *
 *	Much of the core SMP work is based on previous work by Thomas Radke, to
 *	whom a great many thanks are extended.
 *
 *	Thanks to Intel for making available several different Pentium,
 *	Pentium Pro and Pentium-II/Xeon MP machines.
 *	Original development of Linux SMP code supported by Caldera.
 *
 *	Fixes
 *		Felix Koop	:	NR_CPUS used properly
 *		Jose Renau	:	Handle single CPU case.
 *		Alan Cox	:	By repeated request 8) - Total BogoMIPS report.
 *		Greg Wright	:	Fix for kernel stacks panic.
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *	Matthias Sattler	:	Changes for 2.1 kernel map.
 *	Michel Lespinasse	:	Changes for 2.1 kernel map.
 *	Michael Chastain	:	Change trampoline.S to gnu as.
 *		Alan Cox	:	Dumb bug: 'B' step PPro's are fine
 *		Ingo Molnar	:	Added APIC timers, based on code
 *					from Jose Renau
 *		Ingo Molnar	:	various cleanups and rewrites
 *		Tigran Aivazian	:	fixed "0.00 in /proc/uptime on SMP" bug.
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs
 *	Andi Kleen		:	Changed for SMP boot into long mode.
 *		Martin J. Bligh	: 	Added support for multi-quad systems
 *		Dave Jones	:	Report invalid combinations of Athlon CPUs.
 *		Rusty Russell	:	Hacked into shape for new "hotplug" boot process.
 *      Andi Kleen              :       Converted to new state machine.
 *	Ashok Raj		: 	CPU hotplug support
 *	Glauber Costa		:	i386 and x86_64 integration
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/topology.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/task_stack.h>
#include <linux/percpu.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/nmi.h>
#include <linux/tboot.h>
#include <linux/gfp.h>
#include <linux/cpuidle.h>
#include <linux/numa.h>
#include <linux/pgtable.h>
#include <linux/overflow.h>

#include <asm/acpi.h>
#include <asm/desc.h>
#include <asm/nmi.h>
#include <asm/irq.h>
#include <asm/realmode.h>
#include <asm/cpu.h>
#include <asm/numa.h>
#include <asm/tlbflush.h>
#include <asm/mtrr.h>
#include <asm/mwait.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/fpu/internal.h>
#include <asm/setup.h>
#include <asm/uv/uv.h>
#include <linux/mc146818rtc.h>
#include <asm/i8259.h>
#include <asm/misc.h>
#include <asm/qspinlock.h>
#include <asm/intel-family.h>
#include <asm/cpu_device_id.h>
#include <asm/spec-ctrl.h>
#include <asm/hw_irq.h>
#include <asm/stackprotector.h>

/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);

/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* representing HT, core, and die siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_die_map);
EXPORT_PER_CPU_SYMBOL(cpu_die_map);

DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);

/* Per CPU bogomips and other parameters */
DEFINE_PER_CPU_READ_MOSTLY(struct cpuinfo_x86, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

/* Logical package management. We might want to allocate that dynamically */
unsigned int __max_logical_packages __read_mostly;
EXPORT_SYMBOL(__max_logical_packages);
static unsigned int logical_packages __read_mostly;
static unsigned int logical_die __read_mostly;

/* Maximum number of SMT threads on any online core */
int __read_mostly __max_smt_threads = 1;

/* Flag to indicate if a complete sched domain rebuild is required */
bool x86_topology_update;

int arch_update_cpu_topology(void)
{
	int retval = x86_topology_update;

	x86_topology_update = false;
	return retval;
}

static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0xa, 0xf);
	spin_unlock_irqrestore(&rtc_lock, flags);
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH)) =
							start_eip >> 4;
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) =
							start_eip & 0xf;
}

static inline void smpboot_restore_warm_reset_vector(void)
{
	unsigned long flags;

	/*
	 * Paranoid:  Set warm reset code and vector here back
	 * to default values.
	 */
	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0, 0xf);
	spin_unlock_irqrestore(&rtc_lock, flags);

	*((volatile u32 *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) = 0;
}

static void init_freq_invariance(bool secondary);

/*
 * Report back to the Boot Processor during boot time or to the caller processor
 * during CPU online.
 */
static void smp_callin(void)
{
	int cpuid;

	/*
	 * If waken up by an INIT in an 82489DX configuration
	 * cpu_callout_mask guarantees we don't get here before
	 * an INIT_deassert IPI reaches our local APIC, so it is
	 * now safe to touch our local APIC.
	 */
	cpuid = smp_processor_id();

	/*
	 * the boot CPU has finished the init stage and is spinning
	 * on callin_map until we finish. We are free to set up this
	 * CPU, first the APIC. (this is probably redundant on most
	 * boards)
	 */
	apic_ap_setup();

	/*
	 * Save our processor parameters. Note: this information
	 * is needed for clock calibration.
	 */
	smp_store_cpu_info(cpuid);

	/*
	 * The topology information must be up to date before
	 * calibrate_delay() and notify_cpu_starting().
	 */
	set_cpu_sibling_map(raw_smp_processor_id());

	init_freq_invariance(true);

	/*
	 * Get our bogomips.
	 * Update loops_per_jiffy in cpu_data. Previous call to
	 * smp_store_cpu_info() stored a value that is close but not as
	 * accurate as the value just calculated.
	 */
	calibrate_delay();
	cpu_data(cpuid).loops_per_jiffy = loops_per_jiffy;
	pr_debug("Stack at about %p\n", &cpuid);

	wmb();

	notify_cpu_starting(cpuid);

	/*
	 * Allow the master to continue.
	 */
	cpumask_set_cpu(cpuid, cpu_callin_mask);
}

static int cpu0_logical_apicid;
static int enable_start_cpu0;
/*
 * Activate a secondary processor.
 */
static void notrace start_secondary(void *unused)
{
	/*
	 * Don't put *anything* except direct CPU state initialization
	 * before cpu_init(), SMP booting is too fragile that we want to
	 * limit the things done here to the most necessary things.
	 */
	cr4_init();

#ifdef CONFIG_X86_32
	/* switch away from the initial page table */
	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
#endif
	cpu_init_exception_handling();
	cpu_init();
	rcu_cpu_starting(raw_smp_processor_id());
	x86_cpuinit.early_percpu_clock_init();
	smp_callin();

	enable_start_cpu0 = 0;

	/* otherwise gcc will move up smp_processor_id before the cpu_init */
	barrier();
	/*
	 * Check TSC synchronization with the boot CPU:
	 */
	check_tsc_sync_target();

	speculative_store_bypass_ht_init();

	/*
	 * Lock vector_lock, set CPU online and bring the vector
	 * allocator online. Online must be set with vector_lock held
	 * to prevent a concurrent irq setup/teardown from seeing a
	 * half valid vector space.
	 */
	lock_vector_lock();
	set_cpu_online(smp_processor_id(), true);
	lapic_online();
	unlock_vector_lock();
	cpu_set_state_online(smp_processor_id());
	x86_platform.nmi_init();

	/* enable local interrupts */
	local_irq_enable();

	x86_cpuinit.setup_percpu_clockev();

	wmb();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

/**
 * topology_is_primary_thread - Check whether CPU is the primary SMT thread
 * @cpu:	CPU to check
 */
bool topology_is_primary_thread(unsigned int cpu)
{
	return apic_id_is_primary_thread(per_cpu(x86_cpu_to_apicid, cpu));
}

/**
 * topology_smt_supported - Check whether SMT is supported by the CPUs
 */
bool topology_smt_supported(void)
{
	return smp_num_siblings > 1;
}

/**
 * topology_phys_to_logical_pkg - Map a physical package id to a logical
 *
 * Returns logical package id or -1 if not found
 */
int topology_phys_to_logical_pkg(unsigned int phys_pkg)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct cpuinfo_x86 *c = &cpu_data(cpu);

		if (c->initialized && c->phys_proc_id == phys_pkg)
			return c->logical_proc_id;
	}
	return -1;
}
EXPORT_SYMBOL(topology_phys_to_logical_pkg);
/**
 * topology_phys_to_logical_die - Map a physical die id to logical
 *
 * Returns logical die id or -1 if not found
 */
int topology_phys_to_logical_die(unsigned int die_id, unsigned int cur_cpu)
{
	int cpu;
	int proc_id = cpu_data(cur_cpu).phys_proc_id;

	for_each_possible_cpu(cpu) {
		struct cpuinfo_x86 *c = &cpu_data(cpu);

		if (c->initialized && c->cpu_die_id == die_id &&
		    c->phys_proc_id == proc_id)
			return c->logical_die_id;
	}
	return -1;
}
EXPORT_SYMBOL(topology_phys_to_logical_die);

/**
 * topology_update_package_map - Update the physical to logical package map
 * @pkg:	The physical package id as retrieved via CPUID
 * @cpu:	The cpu for which this is updated
 */
int topology_update_package_map(unsigned int pkg, unsigned int cpu)
{
	int new;

	/* Already available somewhere? */
	new = topology_phys_to_logical_pkg(pkg);
	if (new >= 0)
		goto found;

	new = logical_packages++;
	if (new != pkg) {
		pr_info("CPU %u Converting physical %u to logical package %u\n",
			cpu, pkg, new);
	}
found:
	cpu_data(cpu).logical_proc_id = new;
	return 0;
}
/**
 * topology_update_die_map - Update the physical to logical die map
 * @die:	The die id as retrieved via CPUID
 * @cpu:	The cpu for which this is updated
 */
int topology_update_die_map(unsigned int die, unsigned int cpu)
{
	int new;

	/* Already available somewhere? */
	new = topology_phys_to_logical_die(die, cpu);
	if (new >= 0)
		goto found;

	new = logical_die++;
	if (new != die) {
		pr_info("CPU %u Converting physical %u to logical die %u\n",
			cpu, die, new);
	}
found:
	cpu_data(cpu).logical_die_id = new;
	return 0;
}

void __init smp_store_boot_cpu_info(void)
{
	int id = 0; /* CPU 0 */
	struct cpuinfo_x86 *c = &cpu_data(id);

	*c = boot_cpu_data;
	c->cpu_index = id;
	topology_update_package_map(c->phys_proc_id, id);
	topology_update_die_map(c->cpu_die_id, id);
	c->initialized = true;
}

/*
 * The bootstrap kernel entry code has set these up. Save them for
 * a given CPU
 */
void smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = &cpu_data(id);

	/* Copy boot_cpu_data only on the first bringup */
	if (!c->initialized)
		*c = boot_cpu_data;
	c->cpu_index = id;
	/*
	 * During boot time, CPU0 has this setup already. Save the info when
	 * bringing up AP or offlined CPU0.
	 */
	identify_secondary_cpu(c);
	c->initialized = true;
}

static bool
topology_same_node(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	int cpu1 = c->cpu_index, cpu2 = o->cpu_index;

	return (cpu_to_node(cpu1) == cpu_to_node(cpu2));
}

static bool
topology_sane(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o, const char *name)
{
	int cpu1 = c->cpu_index, cpu2 = o->cpu_index;

	return !WARN_ONCE(!topology_same_node(c, o),
		"sched: CPU #%d's %s-sibling CPU #%d is not on the same node! "
		"[node: %d != %d]. Ignoring dependency.\n",
		cpu1, name, cpu2, cpu_to_node(cpu1), cpu_to_node(cpu2));
}

#define link_mask(mfunc, c1, c2)					\
do {									\
	cpumask_set_cpu((c1), mfunc(c2));				\
	cpumask_set_cpu((c2), mfunc(c1));				\
} while (0)

static bool match_smt(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		int cpu1 = c->cpu_index, cpu2 = o->cpu_index;

		if (c->phys_proc_id == o->phys_proc_id &&
		    c->cpu_die_id == o->cpu_die_id &&
		    per_cpu(cpu_llc_id, cpu1) == per_cpu(cpu_llc_id, cpu2)) {
			if (c->cpu_core_id == o->cpu_core_id)
				return topology_sane(c, o, "smt");

			if ((c->cu_id != 0xff) &&
			    (o->cu_id != 0xff) &&
			    (c->cu_id == o->cu_id))
				return topology_sane(c, o, "smt");
		}

	} else if (c->phys_proc_id == o->phys_proc_id &&
		   c->cpu_die_id == o->cpu_die_id &&
		   c->cpu_core_id == o->cpu_core_id) {
		return topology_sane(c, o, "smt");
	}

	return false;
}

static bool match_die(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	if (c->phys_proc_id == o->phys_proc_id &&
	    c->cpu_die_id == o->cpu_die_id)
		return true;
	return false;
}

/*
 * Unlike the other levels, we do not enforce keeping a
 * multicore group inside a NUMA node.  If this happens, we will
 * discard the MC level of the topology later.
 */
static bool match_pkg(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	if (c->phys_proc_id == o->phys_proc_id)
		return true;
	return false;
}

/*
 * Define intel_cod_cpu[] for Intel COD (Cluster-on-Die) CPUs.
 *
 * Any Intel CPU that has multiple nodes per package and does not
 * match intel_cod_cpu[] has the SNC (Sub-NUMA Cluster) topology.
 *
 * When in SNC mode, these CPUs enumerate an LLC that is shared
 * by multiple NUMA nodes. The LLC is shared for off-package data
 * access but private to the NUMA node (half of the package) for
 * on-package access. CPUID (the source of the information about
 * the LLC) can only enumerate the cache as shared or unshared,
 * but not this particular configuration.
 */

static const struct x86_cpu_id intel_cod_cpu[] = {
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_X, 0),	/* COD */
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_X, 0),	/* COD */
	X86_MATCH_INTEL_FAM6_MODEL(ANY, 1),		/* SNC */
	{}
};

static bool match_llc(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	const struct x86_cpu_id *id = x86_match_cpu(intel_cod_cpu);
	int cpu1 = c->cpu_index, cpu2 = o->cpu_index;
	bool intel_snc = id && id->driver_data;

	/* Do not match if we do not have a valid APICID for cpu: */
	if (per_cpu(cpu_llc_id, cpu1) == BAD_APICID)
		return false;

	/* Do not match if LLC id does not match: */
	if (per_cpu(cpu_llc_id, cpu1) != per_cpu(cpu_llc_id, cpu2))
		return false;

	/*
	 * Allow the SNC topology without warning. Return of false
	 * means 'c' does not share the LLC of 'o'. This will be
	 * reflected to userspace.
	 */
	if (match_pkg(c, o) && !topology_same_node(c, o) && intel_snc)
		return false;

	return topology_sane(c, o, "llc");
}


#if defined(CONFIG_SCHED_SMT) || defined(CONFIG_SCHED_MC)
static inline int x86_sched_itmt_flags(void)
{
	return sysctl_sched_itmt_enabled ? SD_ASYM_PACKING : 0;
}

#ifdef CONFIG_SCHED_MC
static int x86_core_flags(void)
{
	return cpu_core_flags() | x86_sched_itmt_flags();
}
#endif
#ifdef CONFIG_SCHED_SMT
static int x86_smt_flags(void)
{
	return cpu_smt_flags() | x86_sched_itmt_flags();
}
#endif
#endif

static struct sched_domain_topology_level x86_numa_in_package_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, x86_smt_flags, SD_INIT_NAME(SMT) },
#endif
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, x86_core_flags, SD_INIT_NAME(MC) },
#endif
	{ NULL, },
};

static struct sched_domain_topology_level x86_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, x86_smt_flags, SD_INIT_NAME(SMT) },
#endif
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, x86_core_flags, SD_INIT_NAME(MC) },
#endif
	{ cpu_cpu_mask, SD_INIT_NAME(DIE) },
	{ NULL, },
};

/*
 * Set if a package/die has multiple NUMA nodes inside.
 * AMD Magny-Cours, Intel Cluster-on-Die, and Intel
 * Sub-NUMA Clustering have this.
 */
static bool x86_has_numa_in_package;

void set_cpu_sibling_map(int cpu)
{
	bool has_smt = smp_num_siblings > 1;
	bool has_mp = has_smt || boot_cpu_data.x86_max_cores > 1;
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct cpuinfo_x86 *o;
	int i, threads;

	cpumask_set_cpu(cpu, cpu_sibling_setup_mask);

	if (!has_mp) {
		cpumask_set_cpu(cpu, topology_sibling_cpumask(cpu));
		cpumask_set_cpu(cpu, cpu_llc_shared_mask(cpu));
		cpumask_set_cpu(cpu, topology_core_cpumask(cpu));
		cpumask_set_cpu(cpu, topology_die_cpumask(cpu));
		c->booted_cores = 1;
		return;
	}

	for_each_cpu(i, cpu_sibling_setup_mask) {
		o = &cpu_data(i);

		if (match_pkg(c, o) && !topology_same_node(c, o))
			x86_has_numa_in_package = true;

		if ((i == cpu) || (has_smt && match_smt(c, o)))
			link_mask(topology_sibling_cpumask, cpu, i);

		if ((i == cpu) || (has_mp && match_llc(c, o)))
			link_mask(cpu_llc_shared_mask, cpu, i);

		if ((i == cpu) || (has_mp && match_die(c, o)))
			link_mask(topology_die_cpumask, cpu, i);
	}

	threads = cpumask_weight(topology_sibling_cpumask(cpu));
	if (threads > __max_smt_threads)
		__max_smt_threads = threads;

	/*
	 * This needs a separate iteration over the cpus because we rely on all
	 * topology_sibling_cpumask links to be set-up.
	 */
	for_each_cpu(i, cpu_sibling_setup_mask) {
		o = &cpu_data(i);

		if ((i == cpu) || (has_mp && match_pkg(c, o))) {
			link_mask(topology_core_cpumask, cpu, i);

			/*
			 *  Does this new cpu bringup a new core?
			 */
			if (threads == 1) {
				/*
				 * for each core in package, increment
				 * the booted_cores for this new cpu
				 */
				if (cpumask_first(
				    topology_sibling_cpumask(i)) == i)
					c->booted_cores++;
				/*
				 * increment the core count for all
				 * the other cpus in this package
				 */
				if (i != cpu)
					cpu_data(i).booted_cores++;
			} else if (i != cpu && !c->booted_cores)
				c->booted_cores = cpu_data(i).booted_cores;
		}
	}
}

/* maps the cpu to the sched domain representing multi-core */
const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return cpu_llc_shared_mask(cpu);
}

static void impress_friends(void)
{
	int cpu;
	unsigned long bogosum = 0;
	/*
	 * Allow the user to impress friends.
	 */
	pr_debug("Before bogomips\n");
	for_each_possible_cpu(cpu)
		if (cpumask_test_cpu(cpu, cpu_callout_mask))
			bogosum += cpu_data(cpu).loops_per_jiffy;
	pr_info("Total of %d processors activated (%lu.%02lu BogoMIPS)\n",
		num_online_cpus(),
		bogosum/(500000/HZ),
		(bogosum/(5000/HZ))%100);

	pr_debug("Before bogocount - setting activated=1\n");
}

void __inquire_remote_apic(int apicid)
{
	unsigned i, regs[] = { APIC_ID >> 4, APIC_LVR >> 4, APIC_SPIV >> 4 };
	const char * const names[] = { "ID", "VERSION", "SPIV" };
	int timeout;
	u32 status;

	pr_info("Inquiring remote APIC 0x%x...\n", apicid);

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		pr_info("... APIC 0x%x %s: ", apicid, names[i]);

		/*
		 * Wait for idle.
		 */
		status = safe_apic_wait_icr_idle();
		if (status)
			pr_cont("a previous APIC delivery may have failed\n");

		apic_icr_write(APIC_DM_REMRD | regs[i], apicid);

		timeout = 0;
		do {
			udelay(100);
			status = apic_read(APIC_ICR) & APIC_ICR_RR_MASK;
		} while (status == APIC_ICR_RR_INPROG && timeout++ < 1000);

		switch (status) {
		case APIC_ICR_RR_VALID:
			status = apic_read(APIC_RRR);
			pr_cont("%08x\n", status);
			break;
		default:
			pr_cont("failed\n");
		}
	}
}

/*
 * The Multiprocessor Specification 1.4 (1997) example code suggests
 * that there should be a 10ms delay between the BSP asserting INIT
 * and de-asserting INIT, when starting a remote processor.
 * But that slows boot and resume on modern processors, which include
 * many cores and don't require that delay.
 *
 * Cmdline "init_cpu_udelay=" is available to over-ride this delay.
 * Modern processor families are quirked to remove the delay entirely.
 */
#define UDELAY_10MS_DEFAULT 10000

static unsigned int init_udelay = UINT_MAX;

static int __init cpu_init_udelay(char *str)
{
	get_option(&str, &init_udelay);

	return 0;
}
early_param("cpu_init_udelay", cpu_init_udelay);

static void __init smp_quirk_init_udelay(void)
{
	/* if cmdline changed it from default, leave it alone */
	if (init_udelay != UINT_MAX)
		return;

	/* if modern processor, use no delay */
	if (((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) && (boot_cpu_data.x86 == 6)) ||
	    ((boot_cpu_data.x86_vendor == X86_VENDOR_HYGON) && (boot_cpu_data.x86 >= 0x18)) ||
	    ((boot_cpu_data.x86_vendor == X86_VENDOR_AMD) && (boot_cpu_data.x86 >= 0xF))) {
		init_udelay = 0;
		return;
	}
	/* else, use legacy delay */
	init_udelay = UDELAY_10MS_DEFAULT;
}

/*
 * Poke the other CPU in the eye via NMI to wake it up. Remember that the normal
 * INIT, INIT, STARTUP sequence will reset the chip hard for us, and this
 * won't ... remember to clear down the APIC, etc later.
 */
int
wakeup_secondary_cpu_via_nmi(int apicid, unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt;

	/* Target chip */
	/* Boot on the stack */
	/* Kick the second */
	apic_icr_write(APIC_DM_NMI | apic->dest_logical, apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	/*
	 * Give the other CPU some time to accept the IPI.
	 */
	udelay(200);
	if (APIC_INTEGRATED(boot_cpu_apic_version)) {
		maxlvt = lapic_get_maxlvt();
		if (maxlvt > 3)			/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
	}
	pr_debug("NMI sent\n");

	if (send_status)
		pr_err("APIC never delivered???\n");
	if (accept_status)
		pr_err("APIC delivery error (%lx)\n", accept_status);

	return (send_status | accept_status);
}

static int
wakeup_secondary_cpu_via_init(int phys_apicid, unsigned long start_eip)
{
	unsigned long send_status = 0, accept_status = 0;
	int maxlvt, num_starts, j;

	maxlvt = lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(boot_cpu_apic_version)) {
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
		       phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	udelay(init_udelay);

	pr_debug("Deasserting INIT\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mb();

	/*
	 * Should we send STARTUP IPIs ?
	 *
	 * Determine this based on the APIC version.
	 * If we don't have an integrated APIC, don't send the STARTUP IPIs.
	 */
	if (APIC_INTEGRATED(boot_cpu_apic_version))
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 * Run STARTUP IPI loop.
	 */
	pr_debug("#startup loops: %d\n", num_starts);

	for (j = 1; j <= num_starts; j++) {
		pr_debug("Sending STARTUP #%d\n", j);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		pr_debug("After apic_write\n");

		/*
		 * STARTUP IPI
		 */

		/* Target chip */
		/* Boot on the stack */
		/* Kick the second */
		apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12),
			       phys_apicid);

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		if (init_udelay == 0)
			udelay(10);
		else
			udelay(300);

		pr_debug("Startup point 1\n");

		pr_debug("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		if (init_udelay == 0)
			udelay(10);
		else
			udelay(200);

		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	pr_debug("After Startup\n");

	if (send_status)
		pr_err("APIC never delivered???\n");
	if (accept_status)
		pr_err("APIC delivery error (%lx)\n", accept_status);

	return (send_status | accept_status);
}

/* reduce the number of lines printed when booting a large cpu count system */
static void announce_cpu(int cpu, int apicid)
{
	static int current_node = NUMA_NO_NODE;
	int node = early_cpu_to_node(cpu);
	static int width, node_width;

	if (!width)
		width = num_digits(num_possible_cpus()) + 1; /* + '#' sign */

	if (!node_width)
		node_width = num_digits(num_possible_nodes()) + 1; /* + '#' */

	if (cpu == 1)
		printk(KERN_INFO "x86: Booting SMP configuration:\n");

	if (system_state < SYSTEM_RUNNING) {
		if (node != current_node) {
			if (current_node > (-1))
				pr_cont("\n");
			current_node = node;

			printk(KERN_INFO ".... node %*s#%d, CPUs:  ",
			       node_width - num_digits(node), " ", node);
		}

		/* Add padding for the BSP */
		if (cpu == 1)
			pr_cont("%*s", width + 1, " ");

		pr_cont("%*s#%d", width - num_digits(cpu), " ", cpu);

	} else
		pr_info("Booting Node %d Processor %d APIC 0x%x\n",
			node, cpu, apicid);
}

static int wakeup_cpu0_nmi(unsigned int cmd, struct pt_regs *regs)
{
	int cpu;

	cpu = smp_processor_id();
	if (cpu == 0 && !cpu_online(cpu) && enable_start_cpu0)
		return NMI_HANDLED;

	return NMI_DONE;
}

/*
 * Wake up AP by INIT, INIT, STARTUP sequence.
 *
 * Instead of waiting for STARTUP after INITs, BSP will execute the BIOS
 * boot-strap code which is not a desired behavior for waking up BSP. To
 * void the boot-strap code, wake up CPU0 by NMI instead.
 *
 * This works to wake up soft offlined CPU0 only. If CPU0 is hard offlined
 * (i.e. physically hot removed and then hot added), NMI won't wake it up.
 * We'll change this code in the future to wake up hard offlined CPU0 if
 * real platform and request are available.
 */
static int
wakeup_cpu_via_init_nmi(int cpu, unsigned long start_ip, int apicid,
	       int *cpu0_nmi_registered)
{
	int id;
	int boot_error;

	preempt_disable();

	/*
	 * Wake up AP by INIT, INIT, STARTUP sequence.
	 */
	if (cpu) {
		boot_error = wakeup_secondary_cpu_via_init(apicid, start_ip);
		goto out;
	}

	/*
	 * Wake up BSP by nmi.
	 *
	 * Register a NMI handler to help wake up CPU0.
	 */
	boot_error = register_nmi_handler(NMI_LOCAL,
					  wakeup_cpu0_nmi, 0, "wake_cpu0");

	if (!boot_error) {
		enable_start_cpu0 = 1;
		*cpu0_nmi_registered = 1;
		if (apic->dest_logical == APIC_DEST_LOGICAL)
			id = cpu0_logical_apicid;
		else
			id = apicid;
		boot_error = wakeup_secondary_cpu_via_nmi(id, start_ip);
	}

out:
	preempt_enable();

	return boot_error;
}

int common_cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	/* Just in case we booted with a single CPU. */
	alternatives_enable_smp();

	per_cpu(current_task, cpu) = idle;
	cpu_init_stack_canary(cpu, idle);

	/* Initialize the interrupt stack(s) */
	ret = irq_init_percpu_irqstack(cpu);
	if (ret)
		return ret;

#ifdef CONFIG_X86_32
	/* Stack for startup_32 can be just as for start_secondary onwards */
	per_cpu(cpu_current_top_of_stack, cpu) = task_top_of_stack(idle);
#else
	initial_gs = per_cpu_offset(cpu);
#endif
	return 0;
}

/*
 * NOTE - on most systems this is a PHYSICAL apic ID, but on multiquad
 * (ie clustered apic addressing mode), this is a LOGICAL apic ID.
 * Returns zero if CPU booted OK, else error code from
 * ->wakeup_secondary_cpu.
 */
static int do_boot_cpu(int apicid, int cpu, struct task_struct *idle,
		       int *cpu0_nmi_registered)
{
	/* start_ip had better be page-aligned! */
	unsigned long start_ip = real_mode_header->trampoline_start;

	unsigned long boot_error = 0;
	unsigned long timeout;

	idle->thread.sp = (unsigned long)task_pt_regs(idle);
	early_gdt_descr.address = (unsigned long)get_cpu_gdt_rw(cpu);
	initial_code = (unsigned long)start_secondary;
	initial_stack  = idle->thread.sp;

	/* Enable the espfix hack for this CPU */
	init_espfix_ap(cpu);

	/* So we see what's up */
	announce_cpu(cpu, apicid);

	/*
	 * This grunge runs the startup process for
	 * the targeted processor.
	 */

	if (x86_platform.legacy.warm_reset) {

		pr_debug("Setting warm reset code and vector.\n");

		smpboot_setup_warm_reset_vector(start_ip);
		/*
		 * Be paranoid about clearing APIC errors.
		*/
		if (APIC_INTEGRATED(boot_cpu_apic_version)) {
			apic_write(APIC_ESR, 0);
			apic_read(APIC_ESR);
		}
	}

	/*
	 * AP might wait on cpu_callout_mask in cpu_init() with
	 * cpu_initialized_mask set if previous attempt to online
	 * it timed-out. Clear cpu_initialized_mask so that after
	 * INIT/SIPI it could start with a clean state.
	 */
	cpumask_clear_cpu(cpu, cpu_initialized_mask);
	smp_mb();

	/*
	 * Wake up a CPU in difference cases:
	 * - Use the method in the APIC driver if it's defined
	 * Otherwise,
	 * - Use an INIT boot APIC message for APs or NMI for BSP.
	 */
	if (apic->wakeup_secondary_cpu)
		boot_error = apic->wakeup_secondary_cpu(apicid, start_ip);
	else
		boot_error = wakeup_cpu_via_init_nmi(cpu, start_ip, apicid,
						     cpu0_nmi_registered);

	if (!boot_error) {
		/*
		 * Wait 10s total for first sign of life from AP
		 */
		boot_error = -1;
		timeout = jiffies + 10*HZ;
		while (time_before(jiffies, timeout)) {
			if (cpumask_test_cpu(cpu, cpu_initialized_mask)) {
				/*
				 * Tell AP to proceed with initialization
				 */
				cpumask_set_cpu(cpu, cpu_callout_mask);
				boot_error = 0;
				break;
			}
			schedule();
		}
	}

	if (!boot_error) {
		/*
		 * Wait till AP completes initial initialization
		 */
		while (!cpumask_test_cpu(cpu, cpu_callin_mask)) {
			/*
			 * Allow other tasks to run while we wait for the
			 * AP to come online. This also gives a chance
			 * for the MTRR work(triggered by the AP coming online)
			 * to be completed in the stop machine context.
			 */
			schedule();
		}
	}

	if (x86_platform.legacy.warm_reset) {
		/*
		 * Cleanup possible dangling ends...
		 */
		smpboot_restore_warm_reset_vector();
	}

	return boot_error;
}

int native_cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int apicid = apic->cpu_present_to_apicid(cpu);
	int cpu0_nmi_registered = 0;
	unsigned long flags;
	int err, ret = 0;

	lockdep_assert_irqs_enabled();

	pr_debug("++++++++++++++++++++=_---CPU UP  %u\n", cpu);

	if (apicid == BAD_APICID ||
	    !physid_isset(apicid, phys_cpu_present_map) ||
	    !apic->apic_id_valid(apicid)) {
		pr_err("%s: bad cpu %d\n", __func__, cpu);
		return -EINVAL;
	}

	/*
	 * Already booted CPU?
	 */
	if (cpumask_test_cpu(cpu, cpu_callin_mask)) {
		pr_debug("do_boot_cpu %d Already started\n", cpu);
		return -ENOSYS;
	}

	/*
	 * Save current MTRR state in case it was changed since early boot
	 * (e.g. by the ACPI SMI) to initialize new CPUs with MTRRs in sync:
	 */
	mtrr_save_state();

	/* x86 CPUs take themselves offline, so delayed offline is OK. */
	err = cpu_check_up_prepare(cpu);
	if (err && err != -EBUSY)
		return err;

	/* the FPU context is blank, nobody can own it */
	per_cpu(fpu_fpregs_owner_ctx, cpu) = NULL;

	err = common_cpu_up(cpu, tidle);
	if (err)
		return err;

	err = do_boot_cpu(apicid, cpu, tidle, &cpu0_nmi_registered);
	if (err) {
		pr_err("do_boot_cpu failed(%d) to wakeup CPU#%u\n", err, cpu);
		ret = -EIO;
		goto unreg_nmi;
	}

	/*
	 * Check TSC synchronization with the AP (keep irqs disabled
	 * while doing so):
	 */
	local_irq_save(flags);
	check_tsc_sync_source(cpu);
	local_irq_restore(flags);

	while (!cpu_online(cpu)) {
		cpu_relax();
		touch_nmi_watchdog();
	}

unreg_nmi:
	/*
	 * Clean up the nmi handler. Do this after the callin and callout sync
	 * to avoid impact of possible long unregister time.
	 */
	if (cpu0_nmi_registered)
		unregister_nmi_handler(NMI_LOCAL, "wake_cpu0");

	return ret;
}

/**
 * arch_disable_smp_support() - disables SMP support for x86 at runtime
 */
void arch_disable_smp_support(void)
{
	disable_ioapic_support();
}

/*
 * Fall back to non SMP mode after errors.
 *
 * RED-PEN audit/test this more. I bet there is more state messed up here.
 */
static __init void disable_smp(void)
{
	pr_info("SMP disabled\n");

	disable_ioapic_support();

	init_cpu_present(cpumask_of(0));
	init_cpu_possible(cpumask_of(0));

	if (smp_found_config)
		physid_set_mask_of_physid(boot_cpu_physical_apicid, &phys_cpu_present_map);
	else
		physid_set_mask_of_physid(0, &phys_cpu_present_map);
	cpumask_set_cpu(0, topology_sibling_cpumask(0));
	cpumask_set_cpu(0, topology_core_cpumask(0));
	cpumask_set_cpu(0, topology_die_cpumask(0));
}

/*
 * Various sanity checks.
 */
static void __init smp_sanity_check(void)
{
	preempt_disable();

#if !defined(CONFIG_X86_BIGSMP) && defined(CONFIG_X86_32)
	if (def_to_bigsmp && nr_cpu_ids > 8) {
		unsigned int cpu;
		unsigned nr;

		pr_warn("More than 8 CPUs detected - skipping them\n"
			"Use CONFIG_X86_BIGSMP\n");

		nr = 0;
		for_each_present_cpu(cpu) {
			if (nr >= 8)
				set_cpu_present(cpu, false);
			nr++;
		}

		nr = 0;
		for_each_possible_cpu(cpu) {
			if (nr >= 8)
				set_cpu_possible(cpu, false);
			nr++;
		}

		nr_cpu_ids = 8;
	}
#endif

	if (!physid_isset(hard_smp_processor_id(), phys_cpu_present_map)) {
		pr_warn("weird, boot CPU (#%d) not listed by the BIOS\n",
			hard_smp_processor_id());

		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}

	/*
	 * Should not be necessary because the MP table should list the boot
	 * CPU too, but we do it for the sake of robustness anyway.
	 */
	if (!apic->check_phys_apicid_present(boot_cpu_physical_apicid)) {
		pr_notice("weird, boot CPU (#%d) not listed by the BIOS\n",
			  boot_cpu_physical_apicid);
		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}
	preempt_enable();
}

static void __init smp_cpu_index_default(void)
{
	int i;
	struct cpuinfo_x86 *c;

	for_each_possible_cpu(i) {
		c = &cpu_data(i);
		/* mark all to hotplug */
		c->cpu_index = nr_cpu_ids;
	}
}

static void __init smp_get_logical_apicid(void)
{
	if (x2apic_mode)
		cpu0_logical_apicid = apic_read(APIC_LDR);
	else
		cpu0_logical_apicid = GET_APIC_LOGICAL_ID(apic_read(APIC_LDR));
}

/*
 * Prepare for SMP bootup.
 * @max_cpus: configured maximum number of CPUs, It is a legacy parameter
 *            for common interface support.
 */
void __init native_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int i;

	smp_cpu_index_default();

	/*
	 * Setup boot CPU information
	 */
	smp_store_boot_cpu_info(); /* Final full version of the data */
	cpumask_copy(cpu_callin_mask, cpumask_of(0));
	mb();

	for_each_possible_cpu(i) {
		zalloc_cpumask_var(&per_cpu(cpu_sibling_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_core_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_die_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_llc_shared_map, i), GFP_KERNEL);
	}

	/*
	 * Set 'default' x86 topology, this matches default_topology() in that
	 * it has NUMA nodes as a topology level. See also
	 * native_smp_cpus_done().
	 *
	 * Must be done before set_cpus_sibling_map() is ran.
	 */
	set_sched_topology(x86_topology);

	set_cpu_sibling_map(0);
	init_freq_invariance(false);
	smp_sanity_check();

	switch (apic_intr_mode) {
	case APIC_PIC:
	case APIC_VIRTUAL_WIRE_NO_CONFIG:
		disable_smp();
		return;
	case APIC_SYMMETRIC_IO_NO_ROUTING:
		disable_smp();
		/* Setup local timer */
		x86_init.timers.setup_percpu_clockev();
		return;
	case APIC_VIRTUAL_WIRE:
	case APIC_SYMMETRIC_IO:
		break;
	}

	/* Setup local timer */
	x86_init.timers.setup_percpu_clockev();

	smp_get_logical_apicid();

	pr_info("CPU0: ");
	print_cpu_info(&cpu_data(0));

	uv_system_init();

	set_mtrr_aps_delayed_init();

	smp_quirk_init_udelay();

	speculative_store_bypass_ht_init();
}

void arch_thaw_secondary_cpus_begin(void)
{
	set_mtrr_aps_delayed_init();
}

void arch_thaw_secondary_cpus_end(void)
{
	mtrr_aps_init();
}

/*
 * Early setup to make printk work.
 */
void __init native_smp_prepare_boot_cpu(void)
{
	int me = smp_processor_id();
	switch_to_new_gdt(me);
	/* already set me in cpu_online_mask in boot_cpu_init() */
	cpumask_set_cpu(me, cpu_callout_mask);
	cpu_set_state_online(me);
	native_pv_lock_init();
}

void __init calculate_max_logical_packages(void)
{
	int ncpus;

	/*
	 * Today neither Intel nor AMD support heterogenous systems so
	 * extrapolate the boot cpu's data to all packages.
	 */
	ncpus = cpu_data(0).booted_cores * topology_max_smt_threads();
	__max_logical_packages = DIV_ROUND_UP(total_cpus, ncpus);
	pr_info("Max logical packages: %u\n", __max_logical_packages);
}

void __init native_smp_cpus_done(unsigned int max_cpus)
{
	pr_debug("Boot done\n");

	calculate_max_logical_packages();

	if (x86_has_numa_in_package)
		set_sched_topology(x86_numa_in_package_topology);

	nmi_selftest();
	impress_friends();
	mtrr_aps_init();
}

static int __initdata setup_possible_cpus = -1;
static int __init _setup_possible_cpus(char *str)
{
	get_option(&str, &setup_possible_cpus);
	return 0;
}
early_param("possible_cpus", _setup_possible_cpus);


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
	int i, possible;

	/* No boot processor was found in mptable or ACPI MADT */
	if (!num_processors) {
		if (boot_cpu_has(X86_FEATURE_APIC)) {
			int apicid = boot_cpu_physical_apicid;
			int cpu = hard_smp_processor_id();

			pr_warn("Boot CPU (id %d) not listed by BIOS\n", cpu);

			/* Make sure boot cpu is enumerated */
			if (apic->cpu_present_to_apicid(0) == BAD_APICID &&
			    apic->apic_id_valid(apicid))
				generic_processor_info(apicid, boot_cpu_apic_version);
		}

		if (!num_processors)
			num_processors = 1;
	}

	i = setup_max_cpus ?: 1;
	if (setup_possible_cpus == -1) {
		possible = num_processors;
#ifdef CONFIG_HOTPLUG_CPU
		if (setup_max_cpus)
			possible += disabled_cpus;
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

	nr_cpu_ids = possible;

	pr_info("Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max_t(int, possible - num_processors, 0));

	reset_cpu_possible_mask();

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
}

#ifdef CONFIG_HOTPLUG_CPU

/* Recompute SMT state for all CPUs on offline */
static void recompute_smt_state(void)
{
	int max_threads, cpu;

	max_threads = 0;
	for_each_online_cpu (cpu) {
		int threads = cpumask_weight(topology_sibling_cpumask(cpu));

		if (threads > max_threads)
			max_threads = threads;
	}
	__max_smt_threads = max_threads;
}

static void remove_siblinginfo(int cpu)
{
	int sibling;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	for_each_cpu(sibling, topology_core_cpumask(cpu)) {
		cpumask_clear_cpu(cpu, topology_core_cpumask(sibling));
		/*/
		 * last thread sibling in this cpu core going down
		 */
		if (cpumask_weight(topology_sibling_cpumask(cpu)) == 1)
			cpu_data(sibling).booted_cores--;
	}

	for_each_cpu(sibling, topology_die_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_die_cpumask(sibling));
	for_each_cpu(sibling, topology_sibling_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_sibling_cpumask(sibling));
	for_each_cpu(sibling, cpu_llc_shared_mask(cpu))
		cpumask_clear_cpu(cpu, cpu_llc_shared_mask(sibling));
	cpumask_clear(cpu_llc_shared_mask(cpu));
	cpumask_clear(topology_sibling_cpumask(cpu));
	cpumask_clear(topology_core_cpumask(cpu));
	cpumask_clear(topology_die_cpumask(cpu));
	c->cpu_core_id = 0;
	c->booted_cores = 0;
	cpumask_clear_cpu(cpu, cpu_sibling_setup_mask);
	recompute_smt_state();
}

static void remove_cpu_from_maps(int cpu)
{
	set_cpu_online(cpu, false);
	cpumask_clear_cpu(cpu, cpu_callout_mask);
	cpumask_clear_cpu(cpu, cpu_callin_mask);
	/* was set by cpu_init() */
	cpumask_clear_cpu(cpu, cpu_initialized_mask);
	numa_remove_cpu(cpu);
}

void cpu_disable_common(void)
{
	int cpu = smp_processor_id();

	remove_siblinginfo(cpu);

	/* It's now safe to remove this processor from the online map */
	lock_vector_lock();
	remove_cpu_from_maps(cpu);
	unlock_vector_lock();
	fixup_irqs();
	lapic_offline();
}

int native_cpu_disable(void)
{
	int ret;

	ret = lapic_can_unplug_cpu();
	if (ret)
		return ret;

	cpu_disable_common();

        /*
         * Disable the local APIC. Otherwise IPI broadcasts will reach
         * it. It still responds normally to INIT, NMI, SMI, and SIPI
         * messages.
         *
         * Disabling the APIC must happen after cpu_disable_common()
         * which invokes fixup_irqs().
         *
         * Disabling the APIC preserves already set bits in IRR, but
         * an interrupt arriving after disabling the local APIC does not
         * set the corresponding IRR bit.
         *
         * fixup_irqs() scans IRR for set bits so it can raise a not
         * yet handled interrupt on the new destination CPU via an IPI
         * but obviously it can't do so for IRR bits which are not set.
         * IOW, interrupts arriving after disabling the local APIC will
         * be lost.
         */
	apic_soft_disable();

	return 0;
}

int common_cpu_die(unsigned int cpu)
{
	int ret = 0;

	/* We don't do anything here: idle task is faking death itself. */

	/* They ack this in play_dead() by setting CPU_DEAD */
	if (cpu_wait_death(cpu, 5)) {
		if (system_state == SYSTEM_RUNNING)
			pr_info("CPU %u is now offline\n", cpu);
	} else {
		pr_err("CPU %u didn't die...\n", cpu);
		ret = -1;
	}

	return ret;
}

void native_cpu_die(unsigned int cpu)
{
	common_cpu_die(cpu);
}

void play_dead_common(void)
{
	idle_task_exit();

	/* Ack it */
	(void)cpu_report_death();

	/*
	 * With physical CPU hotplug, we should halt the cpu
	 */
	local_irq_disable();
}

/**
 * cond_wakeup_cpu0 - Wake up CPU0 if needed.
 *
 * If NMI wants to wake up CPU0, start CPU0.
 */
void cond_wakeup_cpu0(void)
{
	if (smp_processor_id() == 0 && enable_start_cpu0)
		start_cpu0();
}
EXPORT_SYMBOL_GPL(cond_wakeup_cpu0);

/*
 * We need to flush the caches before going to sleep, lest we have
 * dirty data in our caches when we come back up.
 */
static inline void mwait_play_dead(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int highest_cstate = 0;
	unsigned int highest_subcstate = 0;
	void *mwait_ptr;
	int i;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	    boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		return;
	if (!this_cpu_has(X86_FEATURE_MWAIT))
		return;
	if (!this_cpu_has(X86_FEATURE_CLFLUSH))
		return;
	if (__this_cpu_read(cpu_info.cpuid_level) < CPUID_MWAIT_LEAF)
		return;

	eax = CPUID_MWAIT_LEAF;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	/*
	 * eax will be 0 if EDX enumeration is not valid.
	 * Initialized below to cstate, sub_cstate value when EDX is valid.
	 */
	if (!(ecx & CPUID5_ECX_EXTENSIONS_SUPPORTED)) {
		eax = 0;
	} else {
		edx >>= MWAIT_SUBSTATE_SIZE;
		for (i = 0; i < 7 && edx; i++, edx >>= MWAIT_SUBSTATE_SIZE) {
			if (edx & MWAIT_SUBSTATE_MASK) {
				highest_cstate = i;
				highest_subcstate = edx & MWAIT_SUBSTATE_MASK;
			}
		}
		eax = (highest_cstate << MWAIT_SUBSTATE_SIZE) |
			(highest_subcstate - 1);
	}

	/*
	 * This should be a memory location in a cache line which is
	 * unlikely to be touched by other processors.  The actual
	 * content is immaterial as it is not actually modified in any way.
	 */
	mwait_ptr = &current_thread_info()->flags;

	wbinvd();

	while (1) {
		/*
		 * The CLFLUSH is a workaround for erratum AAI65 for
		 * the Xeon 7400 series.  It's not clear it is actually
		 * needed, but it should be harmless in either case.
		 * The WBINVD is insufficient due to the spurious-wakeup
		 * case where we return around the loop.
		 */
		mb();
		clflush(mwait_ptr);
		mb();
		__monitor(mwait_ptr, 0, 0);
		mb();
		__mwait(eax, 0);

		cond_wakeup_cpu0();
	}
}

void hlt_play_dead(void)
{
	if (__this_cpu_read(cpu_info.x86) >= 4)
		wbinvd();

	while (1) {
		native_halt();

		cond_wakeup_cpu0();
	}
}

void native_play_dead(void)
{
	play_dead_common();
	tboot_shutdown(TB_SHUTDOWN_WFS);

	mwait_play_dead();	/* Only returns on failure */
	if (cpuidle_play_dead())
		hlt_play_dead();
}

#else /* ... !CONFIG_HOTPLUG_CPU */
int native_cpu_disable(void)
{
	return -ENOSYS;
}

void native_cpu_die(unsigned int cpu)
{
	/* We said "no" in __cpu_disable */
	BUG();
}

void native_play_dead(void)
{
	BUG();
}

#endif

#ifdef CONFIG_X86_64
/*
 * APERF/MPERF frequency ratio computation.
 *
 * The scheduler wants to do frequency invariant accounting and needs a <1
 * ratio to account for the 'current' frequency, corresponding to
 * freq_curr / freq_max.
 *
 * Since the frequency freq_curr on x86 is controlled by micro-controller and
 * our P-state setting is little more than a request/hint, we need to observe
 * the effective frequency 'BusyMHz', i.e. the average frequency over a time
 * interval after discarding idle time. This is given by:
 *
 *   BusyMHz = delta_APERF / delta_MPERF * freq_base
 *
 * where freq_base is the max non-turbo P-state.
 *
 * The freq_max term has to be set to a somewhat arbitrary value, because we
 * can't know which turbo states will be available at a given point in time:
 * it all depends on the thermal headroom of the entire package. We set it to
 * the turbo level with 4 cores active.
 *
 * Benchmarks show that's a good compromise between the 1C turbo ratio
 * (freq_curr/freq_max would rarely reach 1) and something close to freq_base,
 * which would ignore the entire turbo range (a conspicuous part, making
 * freq_curr/freq_max always maxed out).
 *
 * An exception to the heuristic above is the Atom uarch, where we choose the
 * highest turbo level for freq_max since Atom's are generally oriented towards
 * power efficiency.
 *
 * Setting freq_max to anything less than the 1C turbo ratio makes the ratio
 * freq_curr / freq_max to eventually grow >1, in which case we clip it to 1.
 */

DEFINE_STATIC_KEY_FALSE(arch_scale_freq_key);

static DEFINE_PER_CPU(u64, arch_prev_aperf);
static DEFINE_PER_CPU(u64, arch_prev_mperf);
static u64 arch_turbo_freq_ratio = SCHED_CAPACITY_SCALE;
static u64 arch_max_freq_ratio = SCHED_CAPACITY_SCALE;

void arch_set_max_freq_ratio(bool turbo_disabled)
{
	arch_max_freq_ratio = turbo_disabled ? SCHED_CAPACITY_SCALE :
					arch_turbo_freq_ratio;
}
EXPORT_SYMBOL_GPL(arch_set_max_freq_ratio);

static bool turbo_disabled(void)
{
	u64 misc_en;
	int err;

	err = rdmsrl_safe(MSR_IA32_MISC_ENABLE, &misc_en);
	if (err)
		return false;

	return (misc_en & MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
}

static bool slv_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq)
{
	int err;

	err = rdmsrl_safe(MSR_ATOM_CORE_RATIOS, base_freq);
	if (err)
		return false;

	err = rdmsrl_safe(MSR_ATOM_CORE_TURBO_RATIOS, turbo_freq);
	if (err)
		return false;

	*base_freq = (*base_freq >> 16) & 0x3F;     /* max P state */
	*turbo_freq = *turbo_freq & 0x3F;           /* 1C turbo    */

	return true;
}

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define X86_MATCH(model)					\
	X86_MATCH_VENDOR_FAM_MODEL_FEATURE(INTEL, 6,		\
		INTEL_FAM6_##model, X86_FEATURE_APERFMPERF, NULL)

static const struct x86_cpu_id has_knl_turbo_ratio_limits[] = {
	X86_MATCH(XEON_PHI_KNL),
	X86_MATCH(XEON_PHI_KNM),
	{}
};

static const struct x86_cpu_id has_skx_turbo_ratio_limits[] = {
	X86_MATCH(SKYLAKE_X),
	{}
};

static const struct x86_cpu_id has_glm_turbo_ratio_limits[] = {
	X86_MATCH(ATOM_GOLDMONT),
	X86_MATCH(ATOM_GOLDMONT_D),
	X86_MATCH(ATOM_GOLDMONT_PLUS),
	{}
};

static bool knl_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq,
				int num_delta_fratio)
{
	int fratio, delta_fratio, found;
	int err, i;
	u64 msr;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, base_freq);
	if (err)
		return false;

	*base_freq = (*base_freq >> 8) & 0xFF;	    /* max P state */

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT, &msr);
	if (err)
		return false;

	fratio = (msr >> 8) & 0xFF;
	i = 16;
	found = 0;
	do {
		if (found >= num_delta_fratio) {
			*turbo_freq = fratio;
			return true;
		}

		delta_fratio = (msr >> (i + 5)) & 0x7;

		if (delta_fratio) {
			found += 1;
			fratio -= delta_fratio;
		}

		i += 8;
	} while (i < 64);

	return true;
}

static bool skx_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq, int size)
{
	u64 ratios, counts;
	u32 group_size;
	int err, i;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, base_freq);
	if (err)
		return false;

	*base_freq = (*base_freq >> 8) & 0xFF;      /* max P state */

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT, &ratios);
	if (err)
		return false;

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT1, &counts);
	if (err)
		return false;

	for (i = 0; i < 64; i += 8) {
		group_size = (counts >> i) & 0xFF;
		if (group_size >= size) {
			*turbo_freq = (ratios >> i) & 0xFF;
			return true;
		}
	}

	return false;
}

static bool core_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq)
{
	u64 msr;
	int err;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, base_freq);
	if (err)
		return false;

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT, &msr);
	if (err)
		return false;

	*base_freq = (*base_freq >> 8) & 0xFF;    /* max P state */
	*turbo_freq = (msr >> 24) & 0xFF;         /* 4C turbo    */

	/* The CPU may have less than 4 cores */
	if (!*turbo_freq)
		*turbo_freq = msr & 0xFF;         /* 1C turbo    */

	return true;
}

static bool intel_set_max_freq_ratio(void)
{
	u64 base_freq, turbo_freq;
	u64 turbo_ratio;

	if (slv_set_max_freq_ratio(&base_freq, &turbo_freq))
		goto out;

	if (x86_match_cpu(has_glm_turbo_ratio_limits) &&
	    skx_set_max_freq_ratio(&base_freq, &turbo_freq, 1))
		goto out;

	if (x86_match_cpu(has_knl_turbo_ratio_limits) &&
	    knl_set_max_freq_ratio(&base_freq, &turbo_freq, 1))
		goto out;

	if (x86_match_cpu(has_skx_turbo_ratio_limits) &&
	    skx_set_max_freq_ratio(&base_freq, &turbo_freq, 4))
		goto out;

	if (core_set_max_freq_ratio(&base_freq, &turbo_freq))
		goto out;

	return false;

out:
	/*
	 * Some hypervisors advertise X86_FEATURE_APERFMPERF
	 * but then fill all MSR's with zeroes.
	 * Some CPUs have turbo boost but don't declare any turbo ratio
	 * in MSR_TURBO_RATIO_LIMIT.
	 */
	if (!base_freq || !turbo_freq) {
		pr_debug("Couldn't determine cpu base or turbo frequency, necessary for scale-invariant accounting.\n");
		return false;
	}

	turbo_ratio = div_u64(turbo_freq * SCHED_CAPACITY_SCALE, base_freq);
	if (!turbo_ratio) {
		pr_debug("Non-zero turbo and base frequencies led to a 0 ratio.\n");
		return false;
	}

	arch_turbo_freq_ratio = turbo_ratio;
	arch_set_max_freq_ratio(turbo_disabled());

	return true;
}

static void init_counter_refs(void)
{
	u64 aperf, mperf;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);

	this_cpu_write(arch_prev_aperf, aperf);
	this_cpu_write(arch_prev_mperf, mperf);
}

static void init_freq_invariance(bool secondary)
{
	bool ret = false;

	if (!boot_cpu_has(X86_FEATURE_APERFMPERF))
		return;

	if (secondary) {
		if (static_branch_likely(&arch_scale_freq_key)) {
			init_counter_refs();
		}
		return;
	}

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		ret = intel_set_max_freq_ratio();

	if (ret) {
		init_counter_refs();
		static_branch_enable(&arch_scale_freq_key);
	} else {
		pr_debug("Couldn't determine max cpu frequency, necessary for scale-invariant accounting.\n");
	}
}

static void disable_freq_invariance_workfn(struct work_struct *work)
{
	static_branch_disable(&arch_scale_freq_key);
}

static DECLARE_WORK(disable_freq_invariance_work,
		    disable_freq_invariance_workfn);

DEFINE_PER_CPU(unsigned long, arch_freq_scale) = SCHED_CAPACITY_SCALE;

void arch_scale_freq_tick(void)
{
	u64 freq_scale = SCHED_CAPACITY_SCALE;
	u64 aperf, mperf;
	u64 acnt, mcnt;

	if (!arch_scale_freq_invariant())
		return;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);

	acnt = aperf - this_cpu_read(arch_prev_aperf);
	mcnt = mperf - this_cpu_read(arch_prev_mperf);

	this_cpu_write(arch_prev_aperf, aperf);
	this_cpu_write(arch_prev_mperf, mperf);

	if (check_shl_overflow(acnt, 2*SCHED_CAPACITY_SHIFT, &acnt))
		goto error;

	if (check_mul_overflow(mcnt, arch_max_freq_ratio, &mcnt) || !mcnt)
		goto error;

	freq_scale = div64_u64(acnt, mcnt);
	if (!freq_scale)
		goto error;

	if (freq_scale > SCHED_CAPACITY_SCALE)
		freq_scale = SCHED_CAPACITY_SCALE;

	this_cpu_write(arch_freq_scale, freq_scale);
	return;

error:
	pr_warn("Scheduler frequency invariance went wobbly, disabling!\n");
	schedule_work(&disable_freq_invariance_work);
}
#else
static inline void init_freq_invariance(bool secondary)
{
}
#endif /* CONFIG_X86_64 */
