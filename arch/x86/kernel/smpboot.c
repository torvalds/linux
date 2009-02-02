/*
 *	x86 SMP booting functions
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@lxorguk.ukuu.org.uk>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
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

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/nmi.h>

#include <asm/acpi.h>
#include <asm/desc.h>
#include <asm/nmi.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/smp.h>
#include <asm/trampoline.h>
#include <asm/cpu.h>
#include <asm/numa.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/mtrr.h>
#include <asm/vmi.h>
#include <asm/genapic.h>
#include <asm/setup.h>
#include <linux/mc146818rtc.h>

#include <mach_apic.h>
#include <mach_wakecpu.h>
#include <smpboot_hooks.h>

#ifdef CONFIG_X86_32
u8 apicid_2_node[MAX_APICID];
static int low_mappings;
#endif

/* State of each CPU */
DEFINE_PER_CPU(int, cpu_state) = { 0 };

/* Store all idle threads, this can be reused instead of creating
* a new thread. Also avoids complicated thread destroy functionality
* for idle threads.
*/
#ifdef CONFIG_HOTPLUG_CPU
/*
 * Needed only for CONFIG_HOTPLUG_CPU because __cpuinitdata is
 * removed after init for !CONFIG_HOTPLUG_CPU.
 */
static DEFINE_PER_CPU(struct task_struct *, idle_thread_array);
#define get_idle_for_cpu(x)      (per_cpu(idle_thread_array, x))
#define set_idle_for_cpu(x, p)   (per_cpu(idle_thread_array, x) = (p))
#else
static struct task_struct *idle_thread_array[NR_CPUS] __cpuinitdata ;
#define get_idle_for_cpu(x)      (idle_thread_array[(x)])
#define set_idle_for_cpu(x, p)   (idle_thread_array[(x)] = (p))
#endif

/* Number of siblings per CPU package */
int smp_num_siblings = 1;
EXPORT_SYMBOL(smp_num_siblings);

/* Last level cache ID of each logical CPU */
DEFINE_PER_CPU(u16, cpu_llc_id) = BAD_APICID;

/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU(cpumask_t, cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);

/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU(cpumask_t, cpu_core_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* Per CPU bogomips and other parameters */
DEFINE_PER_CPU_SHARED_ALIGNED(struct cpuinfo_x86, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

static atomic_t init_deasserted;


/* Set if we find a B stepping CPU */
static int __cpuinitdata smp_b_stepping;

#if defined(CONFIG_NUMA) && defined(CONFIG_X86_32)

/* which logical CPUs are on which nodes */
cpumask_t node_to_cpumask_map[MAX_NUMNODES] __read_mostly =
				{ [0 ... MAX_NUMNODES-1] = CPU_MASK_NONE };
EXPORT_SYMBOL(node_to_cpumask_map);
/* which node each logical CPU is on */
int cpu_to_node_map[NR_CPUS] __read_mostly = { [0 ... NR_CPUS-1] = 0 };
EXPORT_SYMBOL(cpu_to_node_map);

/* set up a mapping between cpu and node. */
static void map_cpu_to_node(int cpu, int node)
{
	printk(KERN_INFO "Mapping cpu %d to node %d\n", cpu, node);
	cpumask_set_cpu(cpu, &node_to_cpumask_map[node]);
	cpu_to_node_map[cpu] = node;
}

/* undo a mapping between cpu and node. */
static void unmap_cpu_to_node(int cpu)
{
	int node;

	printk(KERN_INFO "Unmapping cpu %d from all nodes\n", cpu);
	for (node = 0; node < MAX_NUMNODES; node++)
		cpumask_clear_cpu(cpu, &node_to_cpumask_map[node]);
	cpu_to_node_map[cpu] = 0;
}
#else /* !(CONFIG_NUMA && CONFIG_X86_32) */
#define map_cpu_to_node(cpu, node)	({})
#define unmap_cpu_to_node(cpu)	({})
#endif

#ifdef CONFIG_X86_32
static int boot_cpu_logical_apicid;

u8 cpu_2_logical_apicid[NR_CPUS] __read_mostly =
					{ [0 ... NR_CPUS-1] = BAD_APICID };

static void map_cpu_to_logical_apicid(void)
{
	int cpu = smp_processor_id();
	int apicid = logical_smp_processor_id();
	int node = apicid_to_node(apicid);

	if (!node_online(node))
		node = first_online_node;

	cpu_2_logical_apicid[cpu] = apicid;
	map_cpu_to_node(cpu, node);
}

void numa_remove_cpu(int cpu)
{
	cpu_2_logical_apicid[cpu] = BAD_APICID;
	unmap_cpu_to_node(cpu);
}
#else
#define map_cpu_to_logical_apicid()  do {} while (0)
#endif

/*
 * Report back to the Boot Processor.
 * Running on AP.
 */
static void __cpuinit smp_callin(void)
{
	int cpuid, phys_id;
	unsigned long timeout;

	/*
	 * If waken up by an INIT in an 82489DX configuration
	 * we may get here before an INIT-deassert IPI reaches
	 * our local APIC.  We have to wait for the IPI or we'll
	 * lock up on an APIC access.
	 */
	wait_for_init_deassert(&init_deasserted);

	/*
	 * (This works even if the APIC is not enabled.)
	 */
	phys_id = read_apic_id();
	cpuid = smp_processor_id();
	if (cpumask_test_cpu(cpuid, cpu_callin_mask)) {
		panic("%s: phys CPU#%d, CPU#%d already present??\n", __func__,
					phys_id, cpuid);
	}
	pr_debug("CPU#%d (phys ID: %d) waiting for CALLOUT\n", cpuid, phys_id);

	/*
	 * STARTUP IPIs are fragile beasts as they might sometimes
	 * trigger some glue motherboard logic. Complete APIC bus
	 * silence for 1 second, this overestimates the time the
	 * boot CPU is spending to send the up to 2 STARTUP IPIs
	 * by a factor of two. This should be enough.
	 */

	/*
	 * Waiting 2s total for startup (udelay is not yet working)
	 */
	timeout = jiffies + 2*HZ;
	while (time_before(jiffies, timeout)) {
		/*
		 * Has the boot CPU finished it's STARTUP sequence?
		 */
		if (cpumask_test_cpu(cpuid, cpu_callout_mask))
			break;
		cpu_relax();
	}

	if (!time_before(jiffies, timeout)) {
		panic("%s: CPU%d started up but did not get a callout!\n",
		      __func__, cpuid);
	}

	/*
	 * the boot CPU has finished the init stage and is spinning
	 * on callin_map until we finish. We are free to set up this
	 * CPU, first the APIC. (this is probably redundant on most
	 * boards)
	 */

	pr_debug("CALLIN, before setup_local_APIC().\n");
	smp_callin_clear_local_apic();
	setup_local_APIC();
	end_local_APIC_setup();
	map_cpu_to_logical_apicid();

	notify_cpu_starting(cpuid);
	/*
	 * Get our bogomips.
	 *
	 * Need to enable IRQs because it can take longer and then
	 * the NMI watchdog might kill us.
	 */
	local_irq_enable();
	calibrate_delay();
	local_irq_disable();
	pr_debug("Stack at about %p\n", &cpuid);

	/*
	 * Save our processor parameters
	 */
	smp_store_cpu_info(cpuid);

	/*
	 * Allow the master to continue.
	 */
	cpumask_set_cpu(cpuid, cpu_callin_mask);
}

static int __cpuinitdata unsafe_smp;

/*
 * Activate a secondary processor.
 */
notrace static void __cpuinit start_secondary(void *unused)
{
	/*
	 * Don't put *anything* before cpu_init(), SMP booting is too
	 * fragile that we want to limit the things done here to the
	 * most necessary things.
	 */
	vmi_bringup();
	cpu_init();
	preempt_disable();
	smp_callin();

	/* otherwise gcc will move up smp_processor_id before the cpu_init */
	barrier();
	/*
	 * Check TSC synchronization with the BP:
	 */
	check_tsc_sync_target();

	if (nmi_watchdog == NMI_IO_APIC) {
		disable_8259A_irq(0);
		enable_NMI_through_LVT0();
		enable_8259A_irq(0);
	}

#ifdef CONFIG_X86_32
	while (low_mappings)
		cpu_relax();
	__flush_tlb_all();
#endif

	/* This must be done before setting cpu_online_map */
	set_cpu_sibling_map(raw_smp_processor_id());
	wmb();

	/*
	 * We need to hold call_lock, so there is no inconsistency
	 * between the time smp_call_function() determines number of
	 * IPI recipients, and the time when the determination is made
	 * for which cpus receive the IPI. Holding this
	 * lock helps us to not include this cpu in a currently in progress
	 * smp_call_function().
	 *
	 * We need to hold vector_lock so there the set of online cpus
	 * does not change while we are assigning vectors to cpus.  Holding
	 * this lock ensures we don't half assign or remove an irq from a cpu.
	 */
	ipi_call_lock();
	lock_vector_lock();
	__setup_vector_irq(smp_processor_id());
	set_cpu_online(smp_processor_id(), true);
	unlock_vector_lock();
	ipi_call_unlock();
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;

	/* enable local interrupts */
	local_irq_enable();

	setup_secondary_clock();

	wmb();
	cpu_idle();
}

static void __cpuinit smp_apply_quirks(struct cpuinfo_x86 *c)
{
	/*
	 * Mask B, Pentium, but not Pentium MMX
	 */
	if (c->x86_vendor == X86_VENDOR_INTEL &&
	    c->x86 == 5 &&
	    c->x86_mask >= 1 && c->x86_mask <= 4 &&
	    c->x86_model <= 3)
		/*
		 * Remember we have B step Pentia with bugs
		 */
		smp_b_stepping = 1;

	/*
	 * Certain Athlons might work (for various values of 'work') in SMP
	 * but they are not certified as MP capable.
	 */
	if ((c->x86_vendor == X86_VENDOR_AMD) && (c->x86 == 6)) {

		if (num_possible_cpus() == 1)
			goto valid_k7;

		/* Athlon 660/661 is valid. */
		if ((c->x86_model == 6) && ((c->x86_mask == 0) ||
		    (c->x86_mask == 1)))
			goto valid_k7;

		/* Duron 670 is valid */
		if ((c->x86_model == 7) && (c->x86_mask == 0))
			goto valid_k7;

		/*
		 * Athlon 662, Duron 671, and Athlon >model 7 have capability
		 * bit. It's worth noting that the A5 stepping (662) of some
		 * Athlon XP's have the MP bit set.
		 * See http://www.heise.de/newsticker/data/jow-18.10.01-000 for
		 * more.
		 */
		if (((c->x86_model == 6) && (c->x86_mask >= 2)) ||
		    ((c->x86_model == 7) && (c->x86_mask >= 1)) ||
		     (c->x86_model > 7))
			if (cpu_has_mp)
				goto valid_k7;

		/* If we get here, not a certified SMP capable AMD system. */
		unsafe_smp = 1;
	}

valid_k7:
	;
}

static void __cpuinit smp_checks(void)
{
	if (smp_b_stepping)
		printk(KERN_WARNING "WARNING: SMP operation may be unreliable"
				    "with B stepping processors.\n");

	/*
	 * Don't taint if we are running SMP kernel on a single non-MP
	 * approved Athlon
	 */
	if (unsafe_smp && num_online_cpus() > 1) {
		printk(KERN_INFO "WARNING: This combination of AMD"
			"processors is not suitable for SMP.\n");
		add_taint(TAINT_UNSAFE_SMP);
	}
}

/*
 * The bootstrap kernel entry code has set these up. Save them for
 * a given CPU
 */

void __cpuinit smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = &cpu_data(id);

	*c = boot_cpu_data;
	c->cpu_index = id;
	if (id != 0)
		identify_secondary_cpu(c);
	smp_apply_quirks(c);
}


void __cpuinit set_cpu_sibling_map(int cpu)
{
	int i;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	cpumask_set_cpu(cpu, cpu_sibling_setup_mask);

	if (smp_num_siblings > 1) {
		for_each_cpu(i, cpu_sibling_setup_mask) {
			struct cpuinfo_x86 *o = &cpu_data(i);

			if (c->phys_proc_id == o->phys_proc_id &&
			    c->cpu_core_id == o->cpu_core_id) {
				cpumask_set_cpu(i, cpu_sibling_mask(cpu));
				cpumask_set_cpu(cpu, cpu_sibling_mask(i));
				cpumask_set_cpu(i, cpu_core_mask(cpu));
				cpumask_set_cpu(cpu, cpu_core_mask(i));
				cpumask_set_cpu(i, &c->llc_shared_map);
				cpumask_set_cpu(cpu, &o->llc_shared_map);
			}
		}
	} else {
		cpumask_set_cpu(cpu, cpu_sibling_mask(cpu));
	}

	cpumask_set_cpu(cpu, &c->llc_shared_map);

	if (current_cpu_data.x86_max_cores == 1) {
		cpumask_copy(cpu_core_mask(cpu), cpu_sibling_mask(cpu));
		c->booted_cores = 1;
		return;
	}

	for_each_cpu(i, cpu_sibling_setup_mask) {
		if (per_cpu(cpu_llc_id, cpu) != BAD_APICID &&
		    per_cpu(cpu_llc_id, cpu) == per_cpu(cpu_llc_id, i)) {
			cpumask_set_cpu(i, &c->llc_shared_map);
			cpumask_set_cpu(cpu, &cpu_data(i).llc_shared_map);
		}
		if (c->phys_proc_id == cpu_data(i).phys_proc_id) {
			cpumask_set_cpu(i, cpu_core_mask(cpu));
			cpumask_set_cpu(cpu, cpu_core_mask(i));
			/*
			 *  Does this new cpu bringup a new core?
			 */
			if (cpumask_weight(cpu_sibling_mask(cpu)) == 1) {
				/*
				 * for each core in package, increment
				 * the booted_cores for this new cpu
				 */
				if (cpumask_first(cpu_sibling_mask(i)) == i)
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
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	/*
	 * For perf, we return last level cache shared map.
	 * And for power savings, we return cpu_core_map
	 */
	if (sched_mc_power_savings || sched_smt_power_savings)
		return cpu_core_mask(cpu);
	else
		return &c->llc_shared_map;
}

cpumask_t cpu_coregroup_map(int cpu)
{
	return *cpu_coregroup_mask(cpu);
}

static void impress_friends(void)
{
	int cpu;
	unsigned long bogosum = 0;
	/*
	 * Allow the user to impress friends.
	 */
	pr_debug("Before bogomips.\n");
	for_each_possible_cpu(cpu)
		if (cpumask_test_cpu(cpu, cpu_callout_mask))
			bogosum += cpu_data(cpu).loops_per_jiffy;
	printk(KERN_INFO
		"Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		num_online_cpus(),
		bogosum/(500000/HZ),
		(bogosum/(5000/HZ))%100);

	pr_debug("Before bogocount - setting activated=1.\n");
}

void __inquire_remote_apic(int apicid)
{
	unsigned i, regs[] = { APIC_ID >> 4, APIC_LVR >> 4, APIC_SPIV >> 4 };
	char *names[] = { "ID", "VERSION", "SPIV" };
	int timeout;
	u32 status;

	printk(KERN_INFO "Inquiring remote APIC 0x%x...\n", apicid);

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		printk(KERN_INFO "... APIC 0x%x %s: ", apicid, names[i]);

		/*
		 * Wait for idle.
		 */
		status = safe_apic_wait_icr_idle();
		if (status)
			printk(KERN_CONT
			       "a previous APIC delivery may have failed\n");

		apic_icr_write(APIC_DM_REMRD | regs[i], apicid);

		timeout = 0;
		do {
			udelay(100);
			status = apic_read(APIC_ICR) & APIC_ICR_RR_MASK;
		} while (status == APIC_ICR_RR_INPROG && timeout++ < 1000);

		switch (status) {
		case APIC_ICR_RR_VALID:
			status = apic_read(APIC_RRR);
			printk(KERN_CONT "%08x\n", status);
			break;
		default:
			printk(KERN_CONT "failed\n");
		}
	}
}

/*
 * Poke the other CPU in the eye via NMI to wake it up. Remember that the normal
 * INIT, INIT, STARTUP sequence will reset the chip hard for us, and this
 * won't ... remember to clear down the APIC, etc later.
 */
int __devinit
wakeup_secondary_cpu_via_nmi(int logical_apicid, unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt;

	/* Target chip */
	/* Boot on the stack */
	/* Kick the second */
	apic_icr_write(APIC_DM_NMI | APIC_DEST_LOGICAL, logical_apicid);

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
	pr_debug("NMI sent.\n");

	if (send_status)
		printk(KERN_ERR "APIC never delivered???\n");
	if (accept_status)
		printk(KERN_ERR "APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}

int __devinit
wakeup_secondary_cpu_via_init(int phys_apicid, unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt, num_starts, j;

	if (get_uv_system_type() == UV_NON_UNIQUE_APIC) {
		send_status = uv_wakeup_secondary(phys_apicid, start_eip);
		atomic_set(&init_deasserted, 1);
		return send_status;
	}

	maxlvt = lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

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

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mb();
	atomic_set(&init_deasserted, 1);

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
	 * Paravirt / VMI wants a startup IPI hook here to set up the
	 * target processor state.
	 */
	startup_ipi_hook(phys_apicid, (unsigned long) start_secondary,
			 (unsigned long)stack_start.sp);

	/*
	 * Run STARTUP IPI loop.
	 */
	pr_debug("#startup loops: %d.\n", num_starts);

	for (j = 1; j <= num_starts; j++) {
		pr_debug("Sending STARTUP #%d.\n", j);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		pr_debug("After apic_write.\n");

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
		udelay(300);

		pr_debug("Startup point 1.\n");

		pr_debug("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	pr_debug("After Startup.\n");

	if (send_status)
		printk(KERN_ERR "APIC never delivered???\n");
	if (accept_status)
		printk(KERN_ERR "APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}

struct create_idle {
	struct work_struct work;
	struct task_struct *idle;
	struct completion done;
	int cpu;
};

static void __cpuinit do_fork_idle(struct work_struct *work)
{
	struct create_idle *c_idle =
		container_of(work, struct create_idle, work);

	c_idle->idle = fork_idle(c_idle->cpu);
	complete(&c_idle->done);
}

#ifdef CONFIG_X86_64

/* __ref because it's safe to call free_bootmem when after_bootmem == 0. */
static void __ref free_bootmem_pda(struct x8664_pda *oldpda)
{
	if (!after_bootmem)
		free_bootmem((unsigned long)oldpda, sizeof(*oldpda));
}

/*
 * Allocate node local memory for the AP pda.
 *
 * Must be called after the _cpu_pda pointer table is initialized.
 */
int __cpuinit get_local_pda(int cpu)
{
	struct x8664_pda *oldpda, *newpda;
	unsigned long size = sizeof(struct x8664_pda);
	int node = cpu_to_node(cpu);

	if (cpu_pda(cpu) && !cpu_pda(cpu)->in_bootmem)
		return 0;

	oldpda = cpu_pda(cpu);
	newpda = kmalloc_node(size, GFP_ATOMIC, node);
	if (!newpda) {
		printk(KERN_ERR "Could not allocate node local PDA "
			"for CPU %d on node %d\n", cpu, node);

		if (oldpda)
			return 0;	/* have a usable pda */
		else
			return -1;
	}

	if (oldpda) {
		memcpy(newpda, oldpda, size);
		free_bootmem_pda(oldpda);
	}

	newpda->in_bootmem = 0;
	cpu_pda(cpu) = newpda;
	return 0;
}
#endif /* CONFIG_X86_64 */

static int __cpuinit do_boot_cpu(int apicid, int cpu)
/*
 * NOTE - on most systems this is a PHYSICAL apic ID, but on multiquad
 * (ie clustered apic addressing mode), this is a LOGICAL apic ID.
 * Returns zero if CPU booted OK, else error code from wakeup_secondary_cpu.
 */
{
	unsigned long boot_error = 0;
	int timeout;
	unsigned long start_ip;
	unsigned short nmi_high = 0, nmi_low = 0;
	struct create_idle c_idle = {
		.cpu = cpu,
		.done = COMPLETION_INITIALIZER_ONSTACK(c_idle.done),
	};
	INIT_WORK(&c_idle.work, do_fork_idle);

#ifdef CONFIG_X86_64
	/* Allocate node local memory for AP pdas */
	if (cpu > 0) {
		boot_error = get_local_pda(cpu);
		if (boot_error)
			goto restore_state;
			/* if can't get pda memory, can't start cpu */
	}
#endif

	alternatives_smp_switch(1);

	c_idle.idle = get_idle_for_cpu(cpu);

	/*
	 * We can't use kernel_thread since we must avoid to
	 * reschedule the child.
	 */
	if (c_idle.idle) {
		c_idle.idle->thread.sp = (unsigned long) (((struct pt_regs *)
			(THREAD_SIZE +  task_stack_page(c_idle.idle))) - 1);
		init_idle(c_idle.idle, cpu);
		goto do_rest;
	}

	if (!keventd_up() || current_is_keventd())
		c_idle.work.func(&c_idle.work);
	else {
		schedule_work(&c_idle.work);
		wait_for_completion(&c_idle.done);
	}

	if (IS_ERR(c_idle.idle)) {
		printk("failed fork for CPU %d\n", cpu);
		return PTR_ERR(c_idle.idle);
	}

	set_idle_for_cpu(cpu, c_idle.idle);
do_rest:
#ifdef CONFIG_X86_32
	per_cpu(current_task, cpu) = c_idle.idle;
	init_gdt(cpu);
	/* Stack for startup_32 can be just as for start_secondary onwards */
	irq_ctx_init(cpu);
#else
	cpu_pda(cpu)->pcurrent = c_idle.idle;
	clear_tsk_thread_flag(c_idle.idle, TIF_FORK);
#endif
	early_gdt_descr.address = (unsigned long)get_cpu_gdt_table(cpu);
	initial_code = (unsigned long)start_secondary;
	stack_start.sp = (void *) c_idle.idle->thread.sp;

	/* start_ip had better be page-aligned! */
	start_ip = setup_trampoline();

	/* So we see what's up   */
	printk(KERN_INFO "Booting processor %d APIC 0x%x ip 0x%lx\n",
			  cpu, apicid, start_ip);

	/*
	 * This grunge runs the startup process for
	 * the targeted processor.
	 */

	atomic_set(&init_deasserted, 0);

	if (get_uv_system_type() != UV_NON_UNIQUE_APIC) {

		pr_debug("Setting warm reset code and vector.\n");

		store_NMI_vector(&nmi_high, &nmi_low);

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
	 * Starting actual IPI sequence...
	 */
	boot_error = wakeup_secondary_cpu(apicid, start_ip);

	if (!boot_error) {
		/*
		 * allow APs to start initializing.
		 */
		pr_debug("Before Callout %d.\n", cpu);
		cpumask_set_cpu(cpu, cpu_callout_mask);
		pr_debug("After Callout %d.\n", cpu);

		/*
		 * Wait 5s total for a response
		 */
		for (timeout = 0; timeout < 50000; timeout++) {
			if (cpumask_test_cpu(cpu, cpu_callin_mask))
				break;	/* It has booted */
			udelay(100);
		}

		if (cpumask_test_cpu(cpu, cpu_callin_mask)) {
			/* number CPUs logically, starting from 1 (BSP is 0) */
			pr_debug("OK.\n");
			printk(KERN_INFO "CPU%d: ", cpu);
			print_cpu_info(&cpu_data(cpu));
			pr_debug("CPU has booted.\n");
		} else {
			boot_error = 1;
			if (*((volatile unsigned char *)trampoline_base)
					== 0xA5)
				/* trampoline started but...? */
				printk(KERN_ERR "Stuck ??\n");
			else
				/* trampoline code not run */
				printk(KERN_ERR "Not responding.\n");
			if (get_uv_system_type() != UV_NON_UNIQUE_APIC)
				inquire_remote_apic(apicid);
		}
	}
#ifdef CONFIG_X86_64
restore_state:
#endif
	if (boot_error) {
		/* Try to put things back the way they were before ... */
		numa_remove_cpu(cpu); /* was set by numa_add_cpu */

		/* was set by do_boot_cpu() */
		cpumask_clear_cpu(cpu, cpu_callout_mask);

		/* was set by cpu_init() */
		cpumask_clear_cpu(cpu, cpu_initialized_mask);

		set_cpu_present(cpu, false);
		per_cpu(x86_cpu_to_apicid, cpu) = BAD_APICID;
	}

	/* mark "stuck" area as not stuck */
	*((volatile unsigned long *)trampoline_base) = 0;

	/*
	 * Cleanup possible dangling ends...
	 */
	smpboot_restore_warm_reset_vector();

	return boot_error;
}

int __cpuinit native_cpu_up(unsigned int cpu)
{
	int apicid = cpu_present_to_apicid(cpu);
	unsigned long flags;
	int err;

	WARN_ON(irqs_disabled());

	pr_debug("++++++++++++++++++++=_---CPU UP  %u\n", cpu);

	if (apicid == BAD_APICID || apicid == boot_cpu_physical_apicid ||
	    !physid_isset(apicid, phys_cpu_present_map)) {
		printk(KERN_ERR "%s: bad cpu %d\n", __func__, cpu);
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

	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;

#ifdef CONFIG_X86_32
	/* init low mem mapping */
	clone_pgd_range(swapper_pg_dir, swapper_pg_dir + KERNEL_PGD_BOUNDARY,
		min_t(unsigned long, KERNEL_PGD_PTRS, KERNEL_PGD_BOUNDARY));
	flush_tlb_all();
	low_mappings = 1;

	err = do_boot_cpu(apicid, cpu);

	zap_low_mappings();
	low_mappings = 0;
#else
	err = do_boot_cpu(apicid, cpu);
#endif
	if (err) {
		pr_debug("do_boot_cpu failed %d\n", err);
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

	return 0;
}

/*
 * Fall back to non SMP mode after errors.
 *
 * RED-PEN audit/test this more. I bet there is more state messed up here.
 */
static __init void disable_smp(void)
{
	/* use the read/write pointers to the present and possible maps */
	cpumask_copy(&cpu_present_map, cpumask_of(0));
	cpumask_copy(&cpu_possible_map, cpumask_of(0));
	smpboot_clear_io_apic_irqs();

	if (smp_found_config)
		physid_set_mask_of_physid(boot_cpu_physical_apicid, &phys_cpu_present_map);
	else
		physid_set_mask_of_physid(0, &phys_cpu_present_map);
	map_cpu_to_logical_apicid();
	cpumask_set_cpu(0, cpu_sibling_mask(0));
	cpumask_set_cpu(0, cpu_core_mask(0));
}

/*
 * Various sanity checks.
 */
static int __init smp_sanity_check(unsigned max_cpus)
{
	preempt_disable();

#if defined(CONFIG_X86_PC) && defined(CONFIG_X86_32)
	if (def_to_bigsmp && nr_cpu_ids > 8) {
		unsigned int cpu;
		unsigned nr;

		printk(KERN_WARNING
		       "More than 8 CPUs detected - skipping them.\n"
		       "Use CONFIG_X86_GENERICARCH and CONFIG_X86_BIGSMP.\n");

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
		printk(KERN_WARNING
			"weird, boot CPU (#%d) not listed by the BIOS.\n",
			hard_smp_processor_id());

		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}

	/*
	 * If we couldn't find an SMP configuration at boot time,
	 * get out of here now!
	 */
	if (!smp_found_config && !acpi_lapic) {
		preempt_enable();
		printk(KERN_NOTICE "SMP motherboard not detected.\n");
		disable_smp();
		if (APIC_init_uniprocessor())
			printk(KERN_NOTICE "Local APIC not detected."
					   " Using dummy APIC emulation.\n");
		return -1;
	}

	/*
	 * Should not be necessary because the MP table should list the boot
	 * CPU too, but we do it for the sake of robustness anyway.
	 */
	if (!check_phys_apicid_present(boot_cpu_physical_apicid)) {
		printk(KERN_NOTICE
			"weird, boot CPU (#%d) not listed by the BIOS.\n",
			boot_cpu_physical_apicid);
		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}
	preempt_enable();

	/*
	 * If we couldn't find a local APIC, then get out of here now!
	 */
	if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid]) &&
	    !cpu_has_apic) {
		printk(KERN_ERR "BIOS bug, local APIC #%d not detected!...\n",
			boot_cpu_physical_apicid);
		printk(KERN_ERR "... forcing use of dummy APIC emulation."
				"(tell your hw vendor)\n");
		smpboot_clear_io_apic();
		return -1;
	}

	verify_local_APIC();

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		printk(KERN_INFO "SMP mode deactivated.\n");
		smpboot_clear_io_apic();

		localise_nmi_watchdog();

		connect_bsp_APIC();
		setup_local_APIC();
		end_local_APIC_setup();
		return -1;
	}

	return 0;
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
	preempt_disable();
	smp_cpu_index_default();
	current_cpu_data = boot_cpu_data;
	cpumask_copy(cpu_callin_mask, cpumask_of(0));
	mb();
	/*
	 * Setup boot CPU information
	 */
	smp_store_cpu_info(0); /* Final full version of the data */
#ifdef CONFIG_X86_32
	boot_cpu_logical_apicid = logical_smp_processor_id();
#endif
	current_thread_info()->cpu = 0;  /* needed? */
	set_cpu_sibling_map(0);

#ifdef CONFIG_X86_64
	enable_IR_x2apic();
	setup_apic_routing();
#endif

	if (smp_sanity_check(max_cpus) < 0) {
		printk(KERN_INFO "SMP disabled\n");
		disable_smp();
		goto out;
	}

	preempt_disable();
	if (read_apic_id() != boot_cpu_physical_apicid) {
		panic("Boot APIC ID in local APIC unexpected (%d vs %d)",
		     read_apic_id(), boot_cpu_physical_apicid);
		/* Or can we switch back to PIC here? */
	}
	preempt_enable();

	connect_bsp_APIC();

	/*
	 * Switch from PIC to APIC mode.
	 */
	setup_local_APIC();

#ifdef CONFIG_X86_64
	/*
	 * Enable IO APIC before setting up error vector
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		enable_IO_APIC();
#endif
	end_local_APIC_setup();

	map_cpu_to_logical_apicid();

	setup_portio_remap();

	smpboot_setup_io_apic();
	/*
	 * Set up local APIC timer on boot CPU.
	 */

	printk(KERN_INFO "CPU%d: ", 0);
	print_cpu_info(&cpu_data(0));
	setup_boot_clock();

	if (is_uv_system())
		uv_system_init();
out:
	preempt_enable();
}
/*
 * Early setup to make printk work.
 */
void __init native_smp_prepare_boot_cpu(void)
{
	int me = smp_processor_id();
#ifdef CONFIG_X86_32
	init_gdt(me);
#endif
	switch_to_new_gdt();
	/* already set me in cpu_online_mask in boot_cpu_init() */
	cpumask_set_cpu(me, cpu_callout_mask);
	per_cpu(cpu_state, me) = CPU_ONLINE;
}

void __init native_smp_cpus_done(unsigned int max_cpus)
{
	pr_debug("Boot done.\n");

	impress_friends();
	smp_checks();
#ifdef CONFIG_X86_IO_APIC
	setup_ioapic_dest();
#endif
	check_nmi_watchdog();
}

static int __initdata setup_possible_cpus = -1;
static int __init _setup_possible_cpus(char *str)
{
	get_option(&str, &setup_possible_cpus);
	return 0;
}
early_param("possible_cpus", _setup_possible_cpus);


/*
 * cpu_possible_map should be static, it cannot change as cpu's
 * are onlined, or offlined. The reason is per-cpu data-structures
 * are allocated by some modules at init time, and dont expect to
 * do this dynamically on cpu arrival/departure.
 * cpu_present_map on the other hand can change dynamically.
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

	if (setup_possible_cpus == -1)
		possible = num_processors + disabled_cpus;
	else
		possible = setup_possible_cpus;

	total_cpus = max_t(int, possible, num_processors + disabled_cpus);

	if (possible > CONFIG_NR_CPUS) {
		printk(KERN_WARNING
			"%d Processors exceeds NR_CPUS limit of %d\n",
			possible, CONFIG_NR_CPUS);
		possible = CONFIG_NR_CPUS;
	}

	printk(KERN_INFO "SMP: Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max_t(int, possible - num_processors, 0));

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);

	nr_cpu_ids = possible;
}

#ifdef CONFIG_HOTPLUG_CPU

static void remove_siblinginfo(int cpu)
{
	int sibling;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	for_each_cpu(sibling, cpu_core_mask(cpu)) {
		cpumask_clear_cpu(cpu, cpu_core_mask(sibling));
		/*/
		 * last thread sibling in this cpu core going down
		 */
		if (cpumask_weight(cpu_sibling_mask(cpu)) == 1)
			cpu_data(sibling).booted_cores--;
	}

	for_each_cpu(sibling, cpu_sibling_mask(cpu))
		cpumask_clear_cpu(cpu, cpu_sibling_mask(sibling));
	cpumask_clear(cpu_sibling_mask(cpu));
	cpumask_clear(cpu_core_mask(cpu));
	c->phys_proc_id = 0;
	c->cpu_core_id = 0;
	cpumask_clear_cpu(cpu, cpu_sibling_setup_mask);
}

static void __ref remove_cpu_from_maps(int cpu)
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
	/*
	 * HACK:
	 * Allow any queued timer interrupts to get serviced
	 * This is only a temporary solution until we cleanup
	 * fixup_irqs as we do for IA64.
	 */
	local_irq_enable();
	mdelay(1);

	local_irq_disable();
	remove_siblinginfo(cpu);

	/* It's now safe to remove this processor from the online map */
	lock_vector_lock();
	remove_cpu_from_maps(cpu);
	unlock_vector_lock();
	fixup_irqs();
}

int native_cpu_disable(void)
{
	int cpu = smp_processor_id();

	/*
	 * Perhaps use cpufreq to drop frequency, but that could go
	 * into generic code.
	 *
	 * We won't take down the boot processor on i386 due to some
	 * interrupts only being able to be serviced by the BSP.
	 * Especially so if we're not using an IOAPIC	-zwane
	 */
	if (cpu == 0)
		return -EBUSY;

	if (nmi_watchdog == NMI_LOCAL_APIC)
		stop_apic_nmi_watchdog(NULL);
	clear_local_APIC();

	cpu_disable_common();
	return 0;
}

void native_cpu_die(unsigned int cpu)
{
	/* We don't do anything here: idle task is faking death itself. */
	unsigned int i;

	for (i = 0; i < 10; i++) {
		/* They ack this in play_dead by setting CPU_DEAD */
		if (per_cpu(cpu_state, cpu) == CPU_DEAD) {
			printk(KERN_INFO "CPU %d is now offline\n", cpu);
			if (1 == num_online_cpus())
				alternatives_smp_switch(0);
			return;
		}
		msleep(100);
	}
	printk(KERN_ERR "CPU %u didn't die...\n", cpu);
}

void play_dead_common(void)
{
	idle_task_exit();
	reset_lazy_tlbstate();
	irq_ctx_exit(raw_smp_processor_id());
	c1e_remove_cpu(raw_smp_processor_id());

	mb();
	/* Ack it */
	__get_cpu_var(cpu_state) = CPU_DEAD;

	/*
	 * With physical CPU hotplug, we should halt the cpu
	 */
	local_irq_disable();
}

void native_play_dead(void)
{
	play_dead_common();
	wbinvd_halt();
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
