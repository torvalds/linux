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
 *	This code is released under the GNU General Public License version 2 or
 *	later.
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
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/nmi.h>
#include <linux/tboot.h>
#include <linux/stackprotector.h>
#include <linux/gfp.h>
#include <linux/cpuidle.h>

#include <asm/acpi.h>
#include <asm/desc.h>
#include <asm/nmi.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/realmode.h>
#include <asm/cpu.h>
#include <asm/numa.h>
#include <asm/pgtable.h>
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
#include <asm/realmode.h>
#include <asm/misc.h>

/* Number of siblings per CPU package */
int smp_num_siblings = 1;
EXPORT_SYMBOL(smp_num_siblings);

/* Last level cache ID of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(u16, cpu_llc_id) = BAD_APICID;

/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);

/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);

/* Per CPU bogomips and other parameters */
DEFINE_PER_CPU_READ_MOSTLY(struct cpuinfo_x86, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

/* Logical package management. We might want to allocate that dynamically */
static int *physical_to_logical_pkg __read_mostly;
static unsigned long *physical_package_map __read_mostly;;
static unsigned long *logical_package_map  __read_mostly;
static unsigned int max_physical_pkg_id __read_mostly;
unsigned int __max_logical_packages __read_mostly;
EXPORT_SYMBOL(__max_logical_packages);

static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0xa, 0xf);
	spin_unlock_irqrestore(&rtc_lock, flags);
	local_flush_tlb();
	pr_debug("1.\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH)) =
							start_eip >> 4;
	pr_debug("2.\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) =
							start_eip & 0xf;
	pr_debug("3.\n");
}

static inline void smpboot_restore_warm_reset_vector(void)
{
	unsigned long flags;

	/*
	 * Install writable page 0 entry to set BIOS data area.
	 */
	local_flush_tlb();

	/*
	 * Paranoid:  Set warm reset code and vector here back
	 * to default values.
	 */
	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0, 0xf);
	spin_unlock_irqrestore(&rtc_lock, flags);

	*((volatile u32 *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) = 0;
}

/*
 * Report back to the Boot Processor during boot time or to the caller processor
 * during CPU online.
 */
static void smp_callin(void)
{
	int cpuid, phys_id;

	/*
	 * If waken up by an INIT in an 82489DX configuration
	 * cpu_callout_mask guarantees we don't get here before
	 * an INIT_deassert IPI reaches our local APIC, so it is
	 * now safe to touch our local APIC.
	 */
	cpuid = smp_processor_id();

	/*
	 * (This works even if the APIC is not enabled.)
	 */
	phys_id = read_apic_id();

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
	 * Get our bogomips.
	 * Update loops_per_jiffy in cpu_data. Previous call to
	 * smp_store_cpu_info() stored a value that is close but not as
	 * accurate as the value just calculated.
	 */
	calibrate_delay();
	cpu_data(cpuid).loops_per_jiffy = loops_per_jiffy;
	pr_debug("Stack at about %p\n", &cpuid);

	/*
	 * This must be done before setting cpu_online_mask
	 * or calling notify_cpu_starting.
	 */
	set_cpu_sibling_map(raw_smp_processor_id());
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
	 * Don't put *anything* before cpu_init(), SMP booting is too
	 * fragile that we want to limit the things done here to the
	 * most necessary things.
	 */
	cpu_init();
	x86_cpuinit.early_percpu_clock_init();
	preempt_disable();
	smp_callin();

	enable_start_cpu0 = 0;

#ifdef CONFIG_X86_32
	/* switch away from the initial page table */
	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
#endif

	/* otherwise gcc will move up smp_processor_id before the cpu_init */
	barrier();
	/*
	 * Check TSC synchronization with the BP:
	 */
	check_tsc_sync_target();

	/*
	 * Lock vector_lock and initialize the vectors on this cpu
	 * before setting the cpu online. We must set it online with
	 * vector_lock held to prevent a concurrent setup/teardown
	 * from seeing a half valid vector space.
	 */
	lock_vector_lock();
	setup_vector_irq(smp_processor_id());
	set_cpu_online(smp_processor_id(), true);
	unlock_vector_lock();
	cpu_set_state_online(smp_processor_id());
	x86_platform.nmi_init();

	/* enable local interrupts */
	local_irq_enable();

	/* to prevent fake stack check failure in clock setup */
	boot_init_stack_canary();

	x86_cpuinit.setup_percpu_clockev();

	wmb();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

int topology_update_package_map(unsigned int apicid, unsigned int cpu)
{
	unsigned int new, pkg = apicid >> boot_cpu_data.x86_coreid_bits;

	/* Called from early boot ? */
	if (!physical_package_map)
		return 0;

	if (pkg >= max_physical_pkg_id)
		return -EINVAL;

	/* Set the logical package id */
	if (test_and_set_bit(pkg, physical_package_map))
		goto found;

	new = find_first_zero_bit(logical_package_map, __max_logical_packages);
	if (new >= __max_logical_packages) {
		physical_to_logical_pkg[pkg] = -1;
		pr_warn("APIC(%x) Package %u exceeds logical package map\n",
			apicid, pkg);
		return -ENOSPC;
	}
	set_bit(new, logical_package_map);
	pr_info("APIC(%x) Converting physical %u to logical package %u\n",
		apicid, pkg, new);
	physical_to_logical_pkg[pkg] = new;

found:
	cpu_data(cpu).logical_proc_id = physical_to_logical_pkg[pkg];
	return 0;
}

/**
 * topology_phys_to_logical_pkg - Map a physical package id to a logical
 *
 * Returns logical package id or -1 if not found
 */
int topology_phys_to_logical_pkg(unsigned int phys_pkg)
{
	if (phys_pkg >= max_physical_pkg_id)
		return -1;
	return physical_to_logical_pkg[phys_pkg];
}
EXPORT_SYMBOL(topology_phys_to_logical_pkg);

static void __init smp_init_package_map(void)
{
	unsigned int ncpus, cpu;
	size_t size;

	/*
	 * Today neither Intel nor AMD support heterogenous systems. That
	 * might change in the future....
	 *
	 * While ideally we'd want '* smp_num_siblings' in the below @ncpus
	 * computation, this won't actually work since some Intel BIOSes
	 * report inconsistent HT data when they disable HT.
	 *
	 * In particular, they reduce the APIC-IDs to only include the cores,
	 * but leave the CPUID topology to say there are (2) siblings.
	 * This means we don't know how many threads there will be until
	 * after the APIC enumeration.
	 *
	 * By not including this we'll sometimes over-estimate the number of
	 * logical packages by the amount of !present siblings, but this is
	 * still better than MAX_LOCAL_APIC.
	 *
	 * We use total_cpus not nr_cpu_ids because nr_cpu_ids can be limited
	 * on the command line leading to a similar issue as the HT disable
	 * problem because the hyperthreads are usually enumerated after the
	 * primary cores.
	 */
	ncpus = boot_cpu_data.x86_max_cores;
	__max_logical_packages = DIV_ROUND_UP(total_cpus, ncpus);

	/*
	 * Possibly larger than what we need as the number of apic ids per
	 * package can be smaller than the actual used apic ids.
	 */
	max_physical_pkg_id = DIV_ROUND_UP(MAX_LOCAL_APIC, ncpus);
	size = max_physical_pkg_id * sizeof(unsigned int);
	physical_to_logical_pkg = kmalloc(size, GFP_KERNEL);
	memset(physical_to_logical_pkg, 0xff, size);
	size = BITS_TO_LONGS(max_physical_pkg_id) * sizeof(unsigned long);
	physical_package_map = kzalloc(size, GFP_KERNEL);
	size = BITS_TO_LONGS(__max_logical_packages) * sizeof(unsigned long);
	logical_package_map = kzalloc(size, GFP_KERNEL);

	pr_info("Max logical packages: %u\n", __max_logical_packages);

	for_each_present_cpu(cpu) {
		unsigned int apicid = apic->cpu_present_to_apicid(cpu);

		if (apicid == BAD_APICID || !apic->apic_id_valid(apicid))
			continue;
		if (!topology_update_package_map(apicid, cpu))
			continue;
		pr_warn("CPU %u APICId %x disabled\n", cpu, apicid);
		per_cpu(x86_bios_cpu_apicid, cpu) = BAD_APICID;
		set_cpu_possible(cpu, false);
		set_cpu_present(cpu, false);
	}
}

void __init smp_store_boot_cpu_info(void)
{
	int id = 0; /* CPU 0 */
	struct cpuinfo_x86 *c = &cpu_data(id);

	*c = boot_cpu_data;
	c->cpu_index = id;
	smp_init_package_map();
}

/*
 * The bootstrap kernel entry code has set these up. Save them for
 * a given CPU
 */
void smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = &cpu_data(id);

	*c = boot_cpu_data;
	c->cpu_index = id;
	/*
	 * During boot time, CPU0 has this setup already. Save the info when
	 * bringing up AP or offlined CPU0.
	 */
	identify_secondary_cpu(c);
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
		    per_cpu(cpu_llc_id, cpu1) == per_cpu(cpu_llc_id, cpu2) &&
		    c->cpu_core_id == o->cpu_core_id)
			return topology_sane(c, o, "smt");

	} else if (c->phys_proc_id == o->phys_proc_id &&
		   c->cpu_core_id == o->cpu_core_id) {
		return topology_sane(c, o, "smt");
	}

	return false;
}

static bool match_llc(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	int cpu1 = c->cpu_index, cpu2 = o->cpu_index;

	if (per_cpu(cpu_llc_id, cpu1) != BAD_APICID &&
	    per_cpu(cpu_llc_id, cpu1) == per_cpu(cpu_llc_id, cpu2))
		return topology_sane(c, o, "llc");

	return false;
}

/*
 * Unlike the other levels, we do not enforce keeping a
 * multicore group inside a NUMA node.  If this happens, we will
 * discard the MC level of the topology later.
 */
static bool match_die(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	if (c->phys_proc_id == o->phys_proc_id)
		return true;
	return false;
}

static struct sched_domain_topology_level numa_inside_package_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, cpu_smt_flags, SD_INIT_NAME(SMT) },
#endif
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, cpu_core_flags, SD_INIT_NAME(MC) },
#endif
	{ NULL, },
};
/*
 * set_sched_topology() sets the topology internal to a CPU.  The
 * NUMA topologies are layered on top of it to build the full
 * system topology.
 *
 * If NUMA nodes are observed to occur within a CPU package, this
 * function should be called.  It forces the sched domain code to
 * only use the SMT level for the CPU portion of the topology.
 * This essentially falls back to relying on NUMA information
 * from the SRAT table to describe the entire system topology
 * (except for hyperthreads).
 */
static void primarily_use_numa_for_topology(void)
{
	set_sched_topology(numa_inside_package_topology);
}

void set_cpu_sibling_map(int cpu)
{
	bool has_smt = smp_num_siblings > 1;
	bool has_mp = has_smt || boot_cpu_data.x86_max_cores > 1;
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct cpuinfo_x86 *o;
	int i;

	cpumask_set_cpu(cpu, cpu_sibling_setup_mask);

	if (!has_mp) {
		cpumask_set_cpu(cpu, topology_sibling_cpumask(cpu));
		cpumask_set_cpu(cpu, cpu_llc_shared_mask(cpu));
		cpumask_set_cpu(cpu, topology_core_cpumask(cpu));
		c->booted_cores = 1;
		return;
	}

	for_each_cpu(i, cpu_sibling_setup_mask) {
		o = &cpu_data(i);

		if ((i == cpu) || (has_smt && match_smt(c, o)))
			link_mask(topology_sibling_cpumask, cpu, i);

		if ((i == cpu) || (has_mp && match_llc(c, o)))
			link_mask(cpu_llc_shared_mask, cpu, i);

	}

	/*
	 * This needs a separate iteration over the cpus because we rely on all
	 * topology_sibling_cpumask links to be set-up.
	 */
	for_each_cpu(i, cpu_sibling_setup_mask) {
		o = &cpu_data(i);

		if ((i == cpu) || (has_mp && match_die(c, o))) {
			link_mask(topology_core_cpumask, cpu, i);

			/*
			 *  Does this new cpu bringup a new core?
			 */
			if (cpumask_weight(
			    topology_sibling_cpumask(cpu)) == 1) {
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
		if (match_die(c, o) && !topology_same_node(c, o))
			primarily_use_numa_for_topology();
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
	if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
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
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
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
	if (APIC_INTEGRATED(apic_version[phys_apicid]))
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

void smp_announce(void)
{
	int num_nodes = num_online_nodes();

	printk(KERN_INFO "x86: Booted up %d node%s, %d CPUs\n",
	       num_nodes, (num_nodes > 1 ? "s" : ""), num_online_cpus());
}

/* reduce the number of lines printed when booting a large cpu count system */
static void announce_cpu(int cpu, int apicid)
{
	static int current_node = -1;
	int node = early_cpu_to_node(cpu);
	static int width, node_width;

	if (!width)
		width = num_digits(num_possible_cpus()) + 1; /* + '#' sign */

	if (!node_width)
		node_width = num_digits(num_possible_nodes()) + 1; /* + '#' */

	if (cpu == 1)
		printk(KERN_INFO "x86: Booting SMP configuration:\n");

	if (system_state == SYSTEM_BOOTING) {
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

void common_cpu_up(unsigned int cpu, struct task_struct *idle)
{
	/* Just in case we booted with a single CPU. */
	alternatives_enable_smp();

	per_cpu(current_task, cpu) = idle;

#ifdef CONFIG_X86_32
	/* Stack for startup_32 can be just as for start_secondary onwards */
	irq_ctx_init(cpu);
	per_cpu(cpu_current_top_of_stack, cpu) =
		(unsigned long)task_stack_page(idle) + THREAD_SIZE;
#else
	clear_tsk_thread_flag(idle, TIF_FORK);
	initial_gs = per_cpu_offset(cpu);
#endif
}

/*
 * NOTE - on most systems this is a PHYSICAL apic ID, but on multiquad
 * (ie clustered apic addressing mode), this is a LOGICAL apic ID.
 * Returns zero if CPU booted OK, else error code from
 * ->wakeup_secondary_cpu.
 */
static int do_boot_cpu(int apicid, int cpu, struct task_struct *idle)
{
	volatile u32 *trampoline_status =
		(volatile u32 *) __va(real_mode_header->trampoline_status);
	/* start_ip had better be page-aligned! */
	unsigned long start_ip = real_mode_header->trampoline_start;

	unsigned long boot_error = 0;
	int cpu0_nmi_registered = 0;
	unsigned long timeout;

	idle->thread.sp = (unsigned long) (((struct pt_regs *)
			  (THREAD_SIZE +  task_stack_page(idle))) - 1);

	early_gdt_descr.address = (unsigned long)get_cpu_gdt_table(cpu);
	initial_code = (unsigned long)start_secondary;
	stack_start  = idle->thread.sp;

	/*
	 * Enable the espfix hack for this CPU
	*/
#ifdef CONFIG_X86_ESPFIX64
	init_espfix_ap(cpu);
#endif

	/* So we see what's up */
	announce_cpu(cpu, apicid);

	/*
	 * This grunge runs the startup process for
	 * the targeted processor.
	 */

	if (get_uv_system_type() != UV_NON_UNIQUE_APIC) {

		pr_debug("Setting warm reset code and vector.\n");

		smpboot_setup_warm_reset_vector(start_ip);
		/*
		 * Be paranoid about clearing APIC errors.
		*/
		if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
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
						     &cpu0_nmi_registered);

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

	/* mark "stuck" area as not stuck */
	*trampoline_status = 0;

	if (get_uv_system_type() != UV_NON_UNIQUE_APIC) {
		/*
		 * Cleanup possible dangling ends...
		 */
		smpboot_restore_warm_reset_vector();
	}
	/*
	 * Clean up the nmi handler. Do this after the callin and callout sync
	 * to avoid impact of possible long unregister time.
	 */
	if (cpu0_nmi_registered)
		unregister_nmi_handler(NMI_LOCAL, "wake_cpu0");

	return boot_error;
}

int native_cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int apicid = apic->cpu_present_to_apicid(cpu);
	unsigned long flags;
	int err;

	WARN_ON(irqs_disabled());

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
	__cpu_disable_lazy_restore(cpu);

	common_cpu_up(cpu, tidle);

	/*
	 * We have to walk the irq descriptors to setup the vector
	 * space for the cpu which comes online.  Prevent irq
	 * alloc/free across the bringup.
	 */
	irq_lock_sparse();

	err = do_boot_cpu(apicid, cpu, tidle);

	if (err) {
		irq_unlock_sparse();
		pr_err("do_boot_cpu failed(%d) to wakeup CPU#%u\n", err, cpu);
		return -EIO;
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

	irq_unlock_sparse();

	return 0;
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
}

enum {
	SMP_OK,
	SMP_NO_CONFIG,
	SMP_NO_APIC,
	SMP_FORCE_UP,
};

/*
 * Various sanity checks.
 */
static int __init smp_sanity_check(unsigned max_cpus)
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
	 * If we couldn't find an SMP configuration at boot time,
	 * get out of here now!
	 */
	if (!smp_found_config && !acpi_lapic) {
		preempt_enable();
		pr_notice("SMP motherboard not detected\n");
		return SMP_NO_CONFIG;
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

	/*
	 * If we couldn't find a local APIC, then get out of here now!
	 */
	if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid]) &&
	    !cpu_has_apic) {
		if (!disable_apic) {
			pr_err("BIOS bug, local APIC #%d not detected!...\n",
				boot_cpu_physical_apicid);
			pr_err("... forcing use of dummy APIC emulation (tell your hw vendor)\n");
		}
		return SMP_NO_APIC;
	}

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		pr_info("SMP mode deactivated\n");
		return SMP_FORCE_UP;
	}

	return SMP_OK;
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

/*
 * Prepare for SMP bootup.  The MP table or ACPI has been read
 * earlier.  Just do some sanity checking here and enable APIC mode.
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

	current_thread_info()->cpu = 0;  /* needed? */
	for_each_possible_cpu(i) {
		zalloc_cpumask_var(&per_cpu(cpu_sibling_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_core_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_llc_shared_map, i), GFP_KERNEL);
	}
	set_cpu_sibling_map(0);

	switch (smp_sanity_check(max_cpus)) {
	case SMP_NO_CONFIG:
		disable_smp();
		if (APIC_init_uniprocessor())
			pr_notice("Local APIC not detected. Using dummy APIC emulation.\n");
		return;
	case SMP_NO_APIC:
		disable_smp();
		return;
	case SMP_FORCE_UP:
		disable_smp();
		apic_bsp_setup(false);
		return;
	case SMP_OK:
		break;
	}

	default_setup_apic_routing();

	if (read_apic_id() != boot_cpu_physical_apicid) {
		panic("Boot APIC ID in local APIC unexpected (%d vs %d)",
		     read_apic_id(), boot_cpu_physical_apicid);
		/* Or can we switch back to PIC here? */
	}

	cpu0_logical_apicid = apic_bsp_setup(false);

	pr_info("CPU%d: ", 0);
	print_cpu_info(&cpu_data(0));

	if (is_uv_system())
		uv_system_init();

	set_mtrr_aps_delayed_init();

	smp_quirk_init_udelay();
}

void arch_enable_nonboot_cpus_begin(void)
{
	set_mtrr_aps_delayed_init();
}

void arch_enable_nonboot_cpus_end(void)
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
}

void __init native_smp_cpus_done(unsigned int max_cpus)
{
	pr_debug("Boot done\n");

	nmi_selftest();
	impress_friends();
	setup_ioapic_dest();
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
 * are allocated by some modules at init time, and dont expect to
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

	/* no processor from mptable or madt */
	if (!num_processors)
		num_processors = 1;

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
		pr_warn("%d Processors exceeds NR_CPUS limit of %d\n",
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

	pr_info("Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max_t(int, possible - num_processors, 0));

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
	for (; i < NR_CPUS; i++)
		set_cpu_possible(i, false);

	nr_cpu_ids = possible;
}

#ifdef CONFIG_HOTPLUG_CPU

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

	for_each_cpu(sibling, topology_sibling_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_sibling_cpumask(sibling));
	for_each_cpu(sibling, cpu_llc_shared_mask(cpu))
		cpumask_clear_cpu(cpu, cpu_llc_shared_mask(sibling));
	cpumask_clear(cpu_llc_shared_mask(cpu));
	cpumask_clear(topology_sibling_cpumask(cpu));
	cpumask_clear(topology_core_cpumask(cpu));
	c->phys_proc_id = 0;
	c->cpu_core_id = 0;
	cpumask_clear_cpu(cpu, cpu_sibling_setup_mask);
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
}

int native_cpu_disable(void)
{
	int ret;

	ret = check_irq_vectors_for_cpu_disable();
	if (ret)
		return ret;

	clear_local_APIC();
	cpu_disable_common();

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
	reset_lazy_tlbstate();
	amd_e400_remove_cpu(raw_smp_processor_id());

	/* Ack it */
	(void)cpu_report_death();

	/*
	 * With physical CPU hotplug, we should halt the cpu
	 */
	local_irq_disable();
}

static bool wakeup_cpu0(void)
{
	if (smp_processor_id() == 0 && enable_start_cpu0)
		return true;

	return false;
}

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
		/*
		 * If NMI wants to wake up CPU0, start CPU0.
		 */
		if (wakeup_cpu0())
			start_cpu0();
	}
}

static inline void hlt_play_dead(void)
{
	if (__this_cpu_read(cpu_info.x86) >= 4)
		wbinvd();

	while (1) {
		native_halt();
		/*
		 * If NMI wants to wake up CPU0, start CPU0.
		 */
		if (wakeup_cpu0())
			start_cpu0();
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
