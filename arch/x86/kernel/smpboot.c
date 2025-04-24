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
#include <linux/kexec.h>
#include <linux/numa.h>
#include <linux/pgtable.h>
#include <linux/overflow.h>
#include <linux/stackprotector.h>
#include <linux/cpuhotplug.h>
#include <linux/mc146818rtc.h>
#include <linux/acpi.h>

#include <asm/acpi.h>
#include <asm/cacheinfo.h>
#include <asm/cpuid.h>
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
#include <asm/fpu/api.h>
#include <asm/setup.h>
#include <asm/uv/uv.h>
#include <asm/microcode.h>
#include <asm/i8259.h>
#include <asm/misc.h>
#include <asm/qspinlock.h>
#include <asm/intel-family.h>
#include <asm/cpu_device_id.h>
#include <asm/spec-ctrl.h>
#include <asm/hw_irq.h>
#include <asm/stackprotector.h>
#include <asm/sev.h>
#include <asm/spec-ctrl.h>

/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);

/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* representing HT, core, and die siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_die_map);
EXPORT_PER_CPU_SYMBOL(cpu_die_map);

/* CPUs which are the primary SMT threads */
struct cpumask __cpu_primary_thread_mask __read_mostly;

/* Representing CPUs for which sibling maps can be computed */
static cpumask_var_t cpu_sibling_setup_mask;

struct mwait_cpu_dead {
	unsigned int	control;
	unsigned int	status;
};

#define CPUDEAD_MWAIT_WAIT	0xDEADBEEF
#define CPUDEAD_MWAIT_KEXEC_HLT	0x4A17DEAD

/*
 * Cache line aligned data for mwait_play_dead(). Separate on purpose so
 * that it's unlikely to be touched by other CPUs.
 */
static DEFINE_PER_CPU_ALIGNED(struct mwait_cpu_dead, mwait_cpu_dead);

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

static unsigned int smpboot_warm_reset_vector_count;

static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	if (!smpboot_warm_reset_vector_count++) {
		CMOS_WRITE(0xa, 0xf);
		*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH)) = start_eip >> 4;
		*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) = start_eip & 0xf;
	}
	spin_unlock_irqrestore(&rtc_lock, flags);
}

static inline void smpboot_restore_warm_reset_vector(void)
{
	unsigned long flags;

	/*
	 * Paranoid:  Set warm reset code and vector here back
	 * to default values.
	 */
	spin_lock_irqsave(&rtc_lock, flags);
	if (!--smpboot_warm_reset_vector_count) {
		CMOS_WRITE(0, 0xf);
		*((volatile u32 *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) = 0;
	}
	spin_unlock_irqrestore(&rtc_lock, flags);

}

/* Run the next set of setup steps for the upcoming CPU */
static void ap_starting(void)
{
	int cpuid = smp_processor_id();

	/* Mop up eventual mwait_play_dead() wreckage */
	this_cpu_write(mwait_cpu_dead.status, 0);
	this_cpu_write(mwait_cpu_dead.control, 0);

	/*
	 * If woken up by an INIT in an 82489DX configuration the alive
	 * synchronization guarantees that the CPU does not reach this
	 * point before an INIT_deassert IPI reaches the local APIC, so it
	 * is now safe to touch the local APIC.
	 *
	 * Set up this CPU, first the APIC, which is probably redundant on
	 * most boards.
	 */
	apic_ap_setup();

	/* Save the processor parameters. */
	identify_secondary_cpu(cpuid);

	/*
	 * The topology information must be up to date before
	 * notify_cpu_starting().
	 */
	set_cpu_sibling_map(cpuid);

	ap_init_aperfmperf();

	pr_debug("Stack at about %p\n", &cpuid);

	wmb();

	/*
	 * This runs the AP through all the cpuhp states to its target
	 * state CPUHP_ONLINE.
	 */
	notify_cpu_starting(cpuid);
}

static void ap_calibrate_delay(void)
{
	/*
	 * Calibrate the delay loop and update loops_per_jiffy in cpu_data.
	 * identify_secondary_cpu() stored a value that is close but not as
	 * accurate as the value just calculated.
	 *
	 * As this is invoked after the TSC synchronization check,
	 * calibrate_delay_is_known() will skip the calibration routine
	 * when TSC is synchronized across sockets.
	 */
	calibrate_delay();
	cpu_data(smp_processor_id()).loops_per_jiffy = loops_per_jiffy;
}

/*
 * Activate a secondary processor.
 */
static void notrace __noendbr start_secondary(void *unused)
{
	/*
	 * Don't put *anything* except direct CPU state initialization
	 * before cpu_init(), SMP booting is too fragile that we want to
	 * limit the things done here to the most necessary things.
	 */
	cr4_init();

	/*
	 * 32-bit specific. 64-bit reaches this code with the correct page
	 * table established. Yet another historical divergence.
	 */
	if (IS_ENABLED(CONFIG_X86_32)) {
		/* switch away from the initial page table */
		load_cr3(swapper_pg_dir);
		__flush_tlb_all();
	}

	cpu_init_exception_handling(false);

	/*
	 * Load the microcode before reaching the AP alive synchronization
	 * point below so it is not part of the full per CPU serialized
	 * bringup part when "parallel" bringup is enabled.
	 *
	 * That's even safe when hyperthreading is enabled in the CPU as
	 * the core code starts the primary threads first and leaves the
	 * secondary threads waiting for SIPI. Loading microcode on
	 * physical cores concurrently is a safe operation.
	 *
	 * This covers both the Intel specific issue that concurrent
	 * microcode loading on SMT siblings must be prohibited and the
	 * vendor independent issue`that microcode loading which changes
	 * CPUID, MSRs etc. must be strictly serialized to maintain
	 * software state correctness.
	 */
	load_ucode_ap();

	/*
	 * Synchronization point with the hotplug core. Sets this CPUs
	 * synchronization state to ALIVE and spin-waits for the control CPU to
	 * release this CPU for further bringup.
	 */
	cpuhp_ap_sync_alive();

	cpu_init();
	fpu__init_cpu();
	rcutree_report_cpu_starting(raw_smp_processor_id());
	x86_cpuinit.early_percpu_clock_init();

	ap_starting();

	/* Check TSC synchronization with the control CPU. */
	check_tsc_sync_target();

	/*
	 * Calibrate the delay loop after the TSC synchronization check.
	 * This allows to skip the calibration when TSC is synchronized
	 * across sockets.
	 */
	ap_calibrate_delay();

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
	x86_platform.nmi_init();

	/* enable local interrupts */
	local_irq_enable();

	x86_cpuinit.setup_percpu_clockev();

	wmb();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
ANNOTATE_NOENDBR_SYM(start_secondary);

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

		if (c->topo.pkg_id == o->topo.pkg_id &&
		    c->topo.die_id == o->topo.die_id &&
		    c->topo.amd_node_id == o->topo.amd_node_id &&
		    per_cpu_llc_id(cpu1) == per_cpu_llc_id(cpu2)) {
			if (c->topo.core_id == o->topo.core_id)
				return topology_sane(c, o, "smt");

			if ((c->topo.cu_id != 0xff) &&
			    (o->topo.cu_id != 0xff) &&
			    (c->topo.cu_id == o->topo.cu_id))
				return topology_sane(c, o, "smt");
		}

	} else if (c->topo.pkg_id == o->topo.pkg_id &&
		   c->topo.die_id == o->topo.die_id &&
		   c->topo.core_id == o->topo.core_id) {
		return topology_sane(c, o, "smt");
	}

	return false;
}

static bool match_die(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	if (c->topo.pkg_id != o->topo.pkg_id || c->topo.die_id != o->topo.die_id)
		return false;

	if (cpu_feature_enabled(X86_FEATURE_TOPOEXT) && topology_amd_nodes_per_pkg() > 1)
		return c->topo.amd_node_id == o->topo.amd_node_id;

	return true;
}

static bool match_l2c(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	int cpu1 = c->cpu_index, cpu2 = o->cpu_index;

	/* If the arch didn't set up l2c_id, fall back to SMT */
	if (per_cpu_l2c_id(cpu1) == BAD_APICID)
		return match_smt(c, o);

	/* Do not match if L2 cache id does not match: */
	if (per_cpu_l2c_id(cpu1) != per_cpu_l2c_id(cpu2))
		return false;

	return topology_sane(c, o, "l2c");
}

/*
 * Unlike the other levels, we do not enforce keeping a
 * multicore group inside a NUMA node.  If this happens, we will
 * discard the MC level of the topology later.
 */
static bool match_pkg(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	if (c->topo.pkg_id == o->topo.pkg_id)
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
	X86_MATCH_VFM(INTEL_HASWELL_X,	 0),	/* COD */
	X86_MATCH_VFM(INTEL_BROADWELL_X, 0),	/* COD */
	X86_MATCH_VFM(INTEL_ANY,	 1),	/* SNC */
	{}
};

static bool match_llc(struct cpuinfo_x86 *c, struct cpuinfo_x86 *o)
{
	const struct x86_cpu_id *id = x86_match_cpu(intel_cod_cpu);
	int cpu1 = c->cpu_index, cpu2 = o->cpu_index;
	bool intel_snc = id && id->driver_data;

	/* Do not match if we do not have a valid APICID for cpu: */
	if (per_cpu_llc_id(cpu1) == BAD_APICID)
		return false;

	/* Do not match if LLC id does not match: */
	if (per_cpu_llc_id(cpu1) != per_cpu_llc_id(cpu2))
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
#ifdef CONFIG_SCHED_CLUSTER
static int x86_cluster_flags(void)
{
	return cpu_cluster_flags() | x86_sched_itmt_flags();
}
#endif

/*
 * Set if a package/die has multiple NUMA nodes inside.
 * AMD Magny-Cours, Intel Cluster-on-Die, and Intel
 * Sub-NUMA Clustering have this.
 */
static bool x86_has_numa_in_package;

static struct sched_domain_topology_level x86_topology[6];

static void __init build_sched_topology(void)
{
	int i = 0;

#ifdef CONFIG_SCHED_SMT
	x86_topology[i++] = (struct sched_domain_topology_level){
		cpu_smt_mask, cpu_smt_flags, SD_INIT_NAME(SMT)
	};
#endif
#ifdef CONFIG_SCHED_CLUSTER
	x86_topology[i++] = (struct sched_domain_topology_level){
		cpu_clustergroup_mask, x86_cluster_flags, SD_INIT_NAME(CLS)
	};
#endif
#ifdef CONFIG_SCHED_MC
	x86_topology[i++] = (struct sched_domain_topology_level){
		cpu_coregroup_mask, x86_core_flags, SD_INIT_NAME(MC)
	};
#endif
	/*
	 * When there is NUMA topology inside the package skip the PKG domain
	 * since the NUMA domains will auto-magically create the right spanning
	 * domains based on the SLIT.
	 */
	if (!x86_has_numa_in_package) {
		x86_topology[i++] = (struct sched_domain_topology_level){
			cpu_cpu_mask, x86_sched_itmt_flags, SD_INIT_NAME(PKG)
		};
	}

	/*
	 * There must be one trailing NULL entry left.
	 */
	BUG_ON(i >= ARRAY_SIZE(x86_topology)-1);

	set_sched_topology(x86_topology);
}

void set_cpu_sibling_map(int cpu)
{
	bool has_smt = __max_threads_per_core > 1;
	bool has_mp = has_smt || topology_num_cores_per_package() > 1;
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct cpuinfo_x86 *o;
	int i, threads;

	cpumask_set_cpu(cpu, cpu_sibling_setup_mask);

	if (!has_mp) {
		cpumask_set_cpu(cpu, topology_sibling_cpumask(cpu));
		cpumask_set_cpu(cpu, cpu_llc_shared_mask(cpu));
		cpumask_set_cpu(cpu, cpu_l2c_shared_mask(cpu));
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

		if ((i == cpu) || (has_mp && match_l2c(c, o)))
			link_mask(cpu_l2c_shared_mask, cpu, i);

		if ((i == cpu) || (has_mp && match_die(c, o)))
			link_mask(topology_die_cpumask, cpu, i);
	}

	threads = cpumask_weight(topology_sibling_cpumask(cpu));
	if (threads > __max_smt_threads)
		__max_smt_threads = threads;

	for_each_cpu(i, topology_sibling_cpumask(cpu))
		cpu_data(i).smt_active = threads > 1;

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

const struct cpumask *cpu_clustergroup_mask(int cpu)
{
	return cpu_l2c_shared_mask(cpu);
}
EXPORT_SYMBOL_GPL(cpu_clustergroup_mask);

static void impress_friends(void)
{
	int cpu;
	unsigned long bogosum = 0;
	/*
	 * Allow the user to impress friends.
	 */
	pr_debug("Before bogomips\n");
	for_each_online_cpu(cpu)
		bogosum += cpu_data(cpu).loops_per_jiffy;

	pr_info("Total of %d processors activated (%lu.%02lu BogoMIPS)\n",
		num_online_cpus(),
		bogosum/(500000/HZ),
		(bogosum/(5000/HZ))%100);

	pr_debug("Before bogocount - setting activated=1\n");
}

/*
 * The Multiprocessor Specification 1.4 (1997) example code suggests
 * that there should be a 10ms delay between the BSP asserting INIT
 * and de-asserting INIT, when starting a remote processor.
 * But that slows boot and resume on modern processors, which include
 * many cores and don't require that delay.
 *
 * Cmdline "cpu_init_udelay=" is available to override this delay.
 */
#define UDELAY_10MS_LEGACY 10000

static unsigned int init_udelay = UINT_MAX;

static int __init cpu_init_udelay(char *str)
{
	get_option(&str, &init_udelay);

	return 0;
}
early_param("cpu_init_udelay", cpu_init_udelay);

static void __init smp_set_init_udelay(void)
{
	/* if cmdline changed it from default, leave it alone */
	if (init_udelay != UINT_MAX)
		return;

	/* if modern processor, use no delay */
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL && boot_cpu_data.x86_vfm >= INTEL_PENTIUM_PRO) ||
	    (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON && boot_cpu_data.x86 >= 0x18) ||
	    (boot_cpu_data.x86_vendor == X86_VENDOR_AMD   && boot_cpu_data.x86 >= 0xF)) {
		init_udelay = 0;
		return;
	}
	/* else, use legacy delay */
	init_udelay = UDELAY_10MS_LEGACY;
}

/*
 * Wake up AP by INIT, INIT, STARTUP sequence.
 */
static void send_init_sequence(u32 phys_apicid)
{
	int maxlvt = lapic_get_maxlvt();

	/* Be paranoid about clearing APIC errors. */
	if (APIC_INTEGRATED(boot_cpu_apic_version)) {
		/* Due to the Pentium erratum 3AP.  */
		if (maxlvt > 3)
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	/* Assert INIT on the target CPU */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT, phys_apicid);
	safe_apic_wait_icr_idle();

	udelay(init_udelay);

	/* Deassert INIT on the target CPU */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);
	safe_apic_wait_icr_idle();
}

/*
 * Wake up AP by INIT, INIT, STARTUP sequence.
 */
static int wakeup_secondary_cpu_via_init(u32 phys_apicid, unsigned long start_eip)
{
	unsigned long send_status = 0, accept_status = 0;
	int num_starts, j, maxlvt;

	preempt_disable();
	maxlvt = lapic_get_maxlvt();
	send_init_sequence(phys_apicid);

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

	preempt_enable();
	return (send_status | accept_status);
}

/* reduce the number of lines printed when booting a large cpu count system */
static void announce_cpu(int cpu, int apicid)
{
	static int width, node_width, first = 1;
	static int current_node = NUMA_NO_NODE;
	int node = early_cpu_to_node(cpu);

	if (!width)
		width = num_digits(num_possible_cpus()) + 1; /* + '#' sign */

	if (!node_width)
		node_width = num_digits(num_possible_nodes()) + 1; /* + '#' */

	if (system_state < SYSTEM_RUNNING) {
		if (first)
			pr_info("x86: Booting SMP configuration:\n");

		if (node != current_node) {
			if (current_node > (-1))
				pr_cont("\n");
			current_node = node;

			printk(KERN_INFO ".... node %*s#%d, CPUs:  ",
			       node_width - num_digits(node), " ", node);
		}

		/* Add padding for the BSP */
		if (first)
			pr_cont("%*s", width + 1, " ");
		first = 0;

		pr_cont("%*s#%d", width - num_digits(cpu), " ", cpu);
	} else
		pr_info("Booting Node %d Processor %d APIC 0x%x\n",
			node, cpu, apicid);
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
#endif
	return 0;
}

/*
 * NOTE - on most systems this is a PHYSICAL apic ID, but on multiquad
 * (ie clustered apic addressing mode), this is a LOGICAL apic ID.
 * Returns zero if startup was successfully sent, else error code from
 * ->wakeup_secondary_cpu.
 */
static int do_boot_cpu(u32 apicid, int cpu, struct task_struct *idle)
{
	unsigned long start_ip = real_mode_header->trampoline_start;
	int ret;

#ifdef CONFIG_X86_64
	/* If 64-bit wakeup method exists, use the 64-bit mode trampoline IP */
	if (apic->wakeup_secondary_cpu_64)
		start_ip = real_mode_header->trampoline_start64;
#endif
	idle->thread.sp = (unsigned long)task_pt_regs(idle);
	initial_code = (unsigned long)start_secondary;

	if (IS_ENABLED(CONFIG_X86_32)) {
		early_gdt_descr.address = (unsigned long)get_cpu_gdt_rw(cpu);
		initial_stack  = idle->thread.sp;
	} else if (!(smpboot_control & STARTUP_PARALLEL_MASK)) {
		smpboot_control = cpu;
	}

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

	smp_mb();

	/*
	 * Wake up a CPU in difference cases:
	 * - Use a method from the APIC driver if one defined, with wakeup
	 *   straight to 64-bit mode preferred over wakeup to RM.
	 * Otherwise,
	 * - Use an INIT boot APIC message
	 */
	if (apic->wakeup_secondary_cpu_64)
		ret = apic->wakeup_secondary_cpu_64(apicid, start_ip);
	else if (apic->wakeup_secondary_cpu)
		ret = apic->wakeup_secondary_cpu(apicid, start_ip);
	else
		ret = wakeup_secondary_cpu_via_init(apicid, start_ip);

	/* If the wakeup mechanism failed, cleanup the warm reset vector */
	if (ret)
		arch_cpuhp_cleanup_kick_cpu(cpu);
	return ret;
}

int native_kick_ap(unsigned int cpu, struct task_struct *tidle)
{
	u32 apicid = apic->cpu_present_to_apicid(cpu);
	int err;

	lockdep_assert_irqs_enabled();

	pr_debug("++++++++++++++++++++=_---CPU UP  %u\n", cpu);

	if (apicid == BAD_APICID || !apic_id_valid(apicid)) {
		pr_err("CPU %u has invalid APIC ID %x. Aborting bringup\n", cpu, apicid);
		return -EINVAL;
	}

	if (!test_bit(apicid, phys_cpu_present_map)) {
		pr_err("CPU %u APIC ID %x is not present. Aborting bringup\n", cpu, apicid);
		return -EINVAL;
	}

	/*
	 * Save current MTRR state in case it was changed since early boot
	 * (e.g. by the ACPI SMI) to initialize new CPUs with MTRRs in sync:
	 */
	mtrr_save_state();

	/* the FPU context is blank, nobody can own it */
	per_cpu(fpu_fpregs_owner_ctx, cpu) = NULL;

	err = common_cpu_up(cpu, tidle);
	if (err)
		return err;

	err = do_boot_cpu(apicid, cpu, tidle);
	if (err)
		pr_err("do_boot_cpu failed(%d) to wakeup CPU#%u\n", err, cpu);

	return err;
}

int arch_cpuhp_kick_ap_alive(unsigned int cpu, struct task_struct *tidle)
{
	return smp_ops.kick_ap_alive(cpu, tidle);
}

void arch_cpuhp_cleanup_kick_cpu(unsigned int cpu)
{
	/* Cleanup possible dangling ends... */
	if (smp_ops.kick_ap_alive == native_kick_ap && x86_platform.legacy.warm_reset)
		smpboot_restore_warm_reset_vector();
}

void arch_cpuhp_cleanup_dead_cpu(unsigned int cpu)
{
	if (smp_ops.cleanup_dead_cpu)
		smp_ops.cleanup_dead_cpu(cpu);

	if (system_state == SYSTEM_RUNNING)
		pr_info("CPU %u is now offline\n", cpu);
}

void arch_cpuhp_sync_state_poll(void)
{
	if (smp_ops.poll_sync_state)
		smp_ops.poll_sync_state();
}

/**
 * arch_disable_smp_support() - Disables SMP support for x86 at boottime
 */
void __init arch_disable_smp_support(void)
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
	topology_reset_possible_cpus_up();

	cpumask_set_cpu(0, topology_sibling_cpumask(0));
	cpumask_set_cpu(0, topology_core_cpumask(0));
	cpumask_set_cpu(0, topology_die_cpumask(0));
}

void __init smp_prepare_cpus_common(void)
{
	unsigned int cpu, node;

	/* Mark all except the boot CPU as hotpluggable */
	for_each_possible_cpu(cpu) {
		if (cpu)
			per_cpu(cpu_info.cpu_index, cpu) = nr_cpu_ids;
	}

	for_each_possible_cpu(cpu) {
		node = cpu_to_node(cpu);

		zalloc_cpumask_var_node(&per_cpu(cpu_sibling_map,    cpu), GFP_KERNEL, node);
		zalloc_cpumask_var_node(&per_cpu(cpu_core_map,       cpu), GFP_KERNEL, node);
		zalloc_cpumask_var_node(&per_cpu(cpu_die_map,        cpu), GFP_KERNEL, node);
		zalloc_cpumask_var_node(&per_cpu(cpu_llc_shared_map, cpu), GFP_KERNEL, node);
		zalloc_cpumask_var_node(&per_cpu(cpu_l2c_shared_map, cpu), GFP_KERNEL, node);
	}

	set_cpu_sibling_map(0);
}

void __init smp_prepare_boot_cpu(void)
{
	smp_ops.smp_prepare_boot_cpu();
}

#ifdef CONFIG_X86_64
/* Establish whether parallel bringup can be supported. */
bool __init arch_cpuhp_init_parallel_bringup(void)
{
	if (!x86_cpuinit.parallel_bringup) {
		pr_info("Parallel CPU startup disabled by the platform\n");
		return false;
	}

	smpboot_control = STARTUP_READ_APICID;
	pr_debug("Parallel CPU startup enabled: 0x%08x\n", smpboot_control);
	return true;
}
#endif

/*
 * Prepare for SMP bootup.
 * @max_cpus: configured maximum number of CPUs, It is a legacy parameter
 *            for common interface support.
 */
void __init native_smp_prepare_cpus(unsigned int max_cpus)
{
	smp_prepare_cpus_common();

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

	pr_info("CPU0: ");
	print_cpu_info(&cpu_data(0));

	uv_system_init();

	smp_set_init_udelay();

	speculative_store_bypass_ht_init();

	snp_set_wakeup_secondary_cpu();
}

void arch_thaw_secondary_cpus_begin(void)
{
	set_cache_aps_delayed_init(true);
}

void arch_thaw_secondary_cpus_end(void)
{
	cache_aps_init();
}

/*
 * Early setup to make printk work.
 */
void __init native_smp_prepare_boot_cpu(void)
{
	int me = smp_processor_id();

	/* SMP handles this from setup_per_cpu_areas() */
	if (!IS_ENABLED(CONFIG_SMP))
		switch_gdt_and_percpu_base(me);

	native_pv_lock_init();
}

void __init native_smp_cpus_done(unsigned int max_cpus)
{
	pr_debug("Boot done\n");

	build_sched_topology();
	nmi_selftest();
	impress_friends();
	cache_aps_init();
}

/* correctly size the local cpu masks */
void __init setup_cpu_local_masks(void)
{
	alloc_bootmem_cpumask_var(&cpu_sibling_setup_mask);
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

	for_each_cpu(sibling, topology_sibling_cpumask(cpu)) {
		cpumask_clear_cpu(cpu, topology_sibling_cpumask(sibling));
		if (cpumask_weight(topology_sibling_cpumask(sibling)) == 1)
			cpu_data(sibling).smt_active = false;
	}

	for_each_cpu(sibling, cpu_llc_shared_mask(cpu))
		cpumask_clear_cpu(cpu, cpu_llc_shared_mask(sibling));
	for_each_cpu(sibling, cpu_l2c_shared_mask(cpu))
		cpumask_clear_cpu(cpu, cpu_l2c_shared_mask(sibling));
	cpumask_clear(cpu_llc_shared_mask(cpu));
	cpumask_clear(cpu_l2c_shared_mask(cpu));
	cpumask_clear(topology_sibling_cpumask(cpu));
	cpumask_clear(topology_core_cpumask(cpu));
	cpumask_clear(topology_die_cpumask(cpu));
	c->topo.core_id = 0;
	c->booted_cores = 0;
	cpumask_clear_cpu(cpu, cpu_sibling_setup_mask);
	recompute_smt_state();
}

static void remove_cpu_from_maps(int cpu)
{
	set_cpu_online(cpu, false);
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

void play_dead_common(void)
{
	idle_task_exit();

	cpuhp_ap_report_dead();

	local_irq_disable();
}

/*
 * We need to flush the caches before going to sleep, lest we have
 * dirty data in our caches when we come back up.
 */
void __noreturn mwait_play_dead(unsigned int eax_hint)
{
	struct mwait_cpu_dead *md = this_cpu_ptr(&mwait_cpu_dead);

	/* Set up state for the kexec() hack below */
	md->status = CPUDEAD_MWAIT_WAIT;
	md->control = CPUDEAD_MWAIT_WAIT;

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
		clflush(md);
		mb();
		__monitor(md, 0, 0);
		mb();
		__mwait(eax_hint, 0);

		if (READ_ONCE(md->control) == CPUDEAD_MWAIT_KEXEC_HLT) {
			/*
			 * Kexec is about to happen. Don't go back into mwait() as
			 * the kexec kernel might overwrite text and data including
			 * page tables and stack. So mwait() would resume when the
			 * monitor cache line is written to and then the CPU goes
			 * south due to overwritten text, page tables and stack.
			 *
			 * Note: This does _NOT_ protect against a stray MCE, NMI,
			 * SMI. They will resume execution at the instruction
			 * following the HLT instruction and run into the problem
			 * which this is trying to prevent.
			 */
			WRITE_ONCE(md->status, CPUDEAD_MWAIT_KEXEC_HLT);
			while(1)
				native_halt();
		}
	}
}

/*
 * Kick all "offline" CPUs out of mwait on kexec(). See comment in
 * mwait_play_dead().
 */
void smp_kick_mwait_play_dead(void)
{
	u32 newstate = CPUDEAD_MWAIT_KEXEC_HLT;
	struct mwait_cpu_dead *md;
	unsigned int cpu, i;

	for_each_cpu_andnot(cpu, cpu_present_mask, cpu_online_mask) {
		md = per_cpu_ptr(&mwait_cpu_dead, cpu);

		/* Does it sit in mwait_play_dead() ? */
		if (READ_ONCE(md->status) != CPUDEAD_MWAIT_WAIT)
			continue;

		/* Wait up to 5ms */
		for (i = 0; READ_ONCE(md->status) != newstate && i < 1000; i++) {
			/* Bring it out of mwait */
			WRITE_ONCE(md->control, newstate);
			udelay(5);
		}

		if (READ_ONCE(md->status) != newstate)
			pr_err_once("CPU%u is stuck in mwait_play_dead()\n", cpu);
	}
}

void __noreturn hlt_play_dead(void)
{
	if (__this_cpu_read(cpu_info.x86) >= 4)
		wbinvd();

	while (1)
		native_halt();
}

/*
 * native_play_dead() is essentially a __noreturn function, but it can't
 * be marked as such as the compiler may complain about it.
 */
void native_play_dead(void)
{
	if (cpu_feature_enabled(X86_FEATURE_KERNEL_IBRS))
		__update_spec_ctrl(0);

	play_dead_common();
	tboot_shutdown(TB_SHUTDOWN_WFS);

	/* Below returns only on error. */
	cpuidle_play_dead();
	hlt_play_dead();
}

#else /* ... !CONFIG_HOTPLUG_CPU */
int native_cpu_disable(void)
{
	return -ENOSYS;
}

void native_play_dead(void)
{
	BUG();
}

#endif
