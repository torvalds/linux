#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/bootmem.h>

#include <asm/nmi.h>
#include <asm/irq.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/numa.h>

/* Number of siblings per CPU package */
int smp_num_siblings = 1;
EXPORT_SYMBOL(smp_num_siblings);

/* Last level cache ID of each logical CPU */
DEFINE_PER_CPU(u16, cpu_llc_id) = BAD_APICID;

/* bitmap of online cpus */
cpumask_t cpu_online_map __read_mostly;
EXPORT_SYMBOL(cpu_online_map);

cpumask_t cpu_callin_map;
cpumask_t cpu_callout_map;
cpumask_t cpu_possible_map;
EXPORT_SYMBOL(cpu_possible_map);

/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU(cpumask_t, cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);

/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU(cpumask_t, cpu_core_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* Per CPU bogomips and other parameters */
DEFINE_PER_CPU_SHARED_ALIGNED(struct cpuinfo_x86, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

/* ready for x86_64, no harm for x86, since it will overwrite after alloc */
unsigned char *trampoline_base = __va(SMP_TRAMPOLINE_BASE);

/* representing cpus for which sibling maps can be computed */
static cpumask_t cpu_sibling_setup_map;

/* Set if we find a B stepping CPU */
int __cpuinitdata smp_b_stepping;

static void __cpuinit smp_apply_quirks(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
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
		add_taint(TAINT_UNSAFE_SMP);
	}

valid_k7:
	;
#endif
}

void smp_checks(void)
{
	if (smp_b_stepping)
		printk(KERN_WARNING "WARNING: SMP operation may be unreliable"
				    "with B stepping processors.\n");

	/*
	 * Don't taint if we are running SMP kernel on a single non-MP
	 * approved Athlon
	 */
	if (tainted & TAINT_UNSAFE_SMP) {
		if (num_online_cpus())
			printk(KERN_INFO "WARNING: This combination of AMD"
				"processors is not suitable for SMP.\n");
		else
			tainted &= ~TAINT_UNSAFE_SMP;
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

	cpu_set(cpu, cpu_sibling_setup_map);

	if (smp_num_siblings > 1) {
		for_each_cpu_mask(i, cpu_sibling_setup_map) {
			if (c->phys_proc_id == cpu_data(i).phys_proc_id &&
			    c->cpu_core_id == cpu_data(i).cpu_core_id) {
				cpu_set(i, per_cpu(cpu_sibling_map, cpu));
				cpu_set(cpu, per_cpu(cpu_sibling_map, i));
				cpu_set(i, per_cpu(cpu_core_map, cpu));
				cpu_set(cpu, per_cpu(cpu_core_map, i));
				cpu_set(i, c->llc_shared_map);
				cpu_set(cpu, cpu_data(i).llc_shared_map);
			}
		}
	} else {
		cpu_set(cpu, per_cpu(cpu_sibling_map, cpu));
	}

	cpu_set(cpu, c->llc_shared_map);

	if (current_cpu_data.x86_max_cores == 1) {
		per_cpu(cpu_core_map, cpu) = per_cpu(cpu_sibling_map, cpu);
		c->booted_cores = 1;
		return;
	}

	for_each_cpu_mask(i, cpu_sibling_setup_map) {
		if (per_cpu(cpu_llc_id, cpu) != BAD_APICID &&
		    per_cpu(cpu_llc_id, cpu) == per_cpu(cpu_llc_id, i)) {
			cpu_set(i, c->llc_shared_map);
			cpu_set(cpu, cpu_data(i).llc_shared_map);
		}
		if (c->phys_proc_id == cpu_data(i).phys_proc_id) {
			cpu_set(i, per_cpu(cpu_core_map, cpu));
			cpu_set(cpu, per_cpu(cpu_core_map, i));
			/*
			 *  Does this new cpu bringup a new core?
			 */
			if (cpus_weight(per_cpu(cpu_sibling_map, cpu)) == 1) {
				/*
				 * for each core in package, increment
				 * the booted_cores for this new cpu
				 */
				if (first_cpu(per_cpu(cpu_sibling_map, i)) == i)
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
cpumask_t cpu_coregroup_map(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	/*
	 * For perf, we return last level cache shared map.
	 * And for power savings, we return cpu_core_map
	 */
	if (sched_mc_power_savings || sched_smt_power_savings)
		return per_cpu(cpu_core_map, cpu);
	else
		return c->llc_shared_map;
}

/*
 * Currently trivial. Write the real->protected mode
 * bootstrap into the page concerned. The caller
 * has made sure it's suitably aligned.
 */

unsigned long __cpuinit setup_trampoline(void)
{
	memcpy(trampoline_base, trampoline_data,
	       trampoline_end - trampoline_data);
	return virt_to_phys(trampoline_base);
}

#ifdef CONFIG_X86_32
/*
 * We are called very early to get the low memory for the
 * SMP bootup trampoline page.
 */
void __init smp_alloc_memory(void)
{
	trampoline_base = alloc_bootmem_low_pages(PAGE_SIZE);
	/*
	 * Has to be in very low memory so we can execute
	 * real-mode AP code.
	 */
	if (__pa(trampoline_base) >= 0x9F000)
		BUG();
}
#endif

void impress_friends(void)
{
	int cpu;
	unsigned long bogosum = 0;
	/*
	 * Allow the user to impress friends.
	 */
	Dprintk("Before bogomips.\n");
	for_each_possible_cpu(cpu)
		if (cpu_isset(cpu, cpu_callout_map))
			bogosum += cpu_data(cpu).loops_per_jiffy;
	printk(KERN_INFO
		"Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		num_online_cpus(),
		bogosum/(500000/HZ),
		(bogosum/(5000/HZ))%100);

	Dprintk("Before bogocount - setting activated=1.\n");
}

#ifdef CONFIG_HOTPLUG_CPU
void remove_siblinginfo(int cpu)
{
	int sibling;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	for_each_cpu_mask(sibling, per_cpu(cpu_core_map, cpu)) {
		cpu_clear(cpu, per_cpu(cpu_core_map, sibling));
		/*/
		 * last thread sibling in this cpu core going down
		 */
		if (cpus_weight(per_cpu(cpu_sibling_map, cpu)) == 1)
			cpu_data(sibling).booted_cores--;
	}

	for_each_cpu_mask(sibling, per_cpu(cpu_sibling_map, cpu))
		cpu_clear(cpu, per_cpu(cpu_sibling_map, sibling));
	cpus_clear(per_cpu(cpu_sibling_map, cpu));
	cpus_clear(per_cpu(cpu_core_map, cpu));
	c->phys_proc_id = 0;
	c->cpu_core_id = 0;
	cpu_clear(cpu, cpu_sibling_setup_map);
}

int additional_cpus __initdata = -1;

static __init int setup_additional_cpus(char *s)
{
	return s && get_option(&s, &additional_cpus) ? 0 : -EINVAL;
}
early_param("additional_cpus", setup_additional_cpus);

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
 * - The user can overwrite it with additional_cpus=NUM
 * - Otherwise don't reserve additional CPUs.
 * We do this because additional CPUs waste a lot of memory.
 * -AK
 */
__init void prefill_possible_map(void)
{
	int i;
	int possible;

	if (additional_cpus == -1) {
		if (disabled_cpus > 0)
			additional_cpus = disabled_cpus;
		else
			additional_cpus = 0;
	}
	possible = num_processors + additional_cpus;
	if (possible > NR_CPUS)
		possible = NR_CPUS;

	printk(KERN_INFO "SMP: Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max_t(int, possible - num_processors, 0));

	for (i = 0; i < possible; i++)
		cpu_set(i, cpu_possible_map);
}

static void __ref remove_cpu_from_maps(int cpu)
{
	cpu_clear(cpu, cpu_online_map);
#ifdef CONFIG_X86_64
	cpu_clear(cpu, cpu_callout_map);
	cpu_clear(cpu, cpu_callin_map);
	/* was set by cpu_init() */
	clear_bit(cpu, (unsigned long *)&cpu_initialized);
	clear_node_cpumask(cpu);
#endif
}

int __cpu_disable(void)
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
	remove_cpu_from_maps(cpu);
	fixup_irqs(cpu_online_map);
	return 0;
}

void __cpu_die(unsigned int cpu)
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
#else /* ... !CONFIG_HOTPLUG_CPU */
int __cpu_disable(void)
{
	return -ENOSYS;
}

void __cpu_die(unsigned int cpu)
{
	/* We said "no" in __cpu_disable */
	BUG();
}
#endif

/*
 * If the BIOS enumerates physical processors before logical,
 * maxcpus=N at enumeration-time can be used to disable HT.
 */
static int __init parse_maxcpus(char *arg)
{
	extern unsigned int maxcpus;

	maxcpus = simple_strtoul(arg, NULL, 0);
	return 0;
}
early_param("maxcpus", parse_maxcpus);
