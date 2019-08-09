// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/acpi.h>
#include <linux/arm_sdei.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched/mm.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/task_stack.h>
#include <linux/interrupt.h>
#include <linux/cache.h>
#include <linux/profile.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/percpu.h>
#include <linux/clockchips.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/irq_work.h>
#include <linux/kexec.h>

#include <asm/alternative.h>
#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/cpu_ops.h>
#include <asm/daifflags.h>
#include <asm/mmu_context.h>
#include <asm/numa.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/smp_plat.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/ptrace.h>
#include <asm/virt.h>

#define CREATE_TRACE_POINTS
#include <trace/events/ipi.h>

DEFINE_PER_CPU_READ_MOSTLY(int, cpu_number);
EXPORT_PER_CPU_SYMBOL(cpu_number);

/*
 * as from 2.5, kernels no longer have an init_tasks structure
 * so we need some other way of telling a new secondary core
 * where to place its SVC stack
 */
struct secondary_data secondary_data;
/* Number of CPUs which aren't online, but looping in kernel text. */
int cpus_stuck_in_kernel;

enum ipi_msg_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
	IPI_CPU_CRASH_STOP,
	IPI_TIMER,
	IPI_IRQ_WORK,
	IPI_WAKEUP
};

#ifdef CONFIG_HOTPLUG_CPU
static int op_cpu_kill(unsigned int cpu);
#else
static inline int op_cpu_kill(unsigned int cpu)
{
	return -ENOSYS;
}
#endif


/*
 * Boot a secondary CPU, and assign it the specified idle task.
 * This also gives us the initial stack to use for this CPU.
 */
static int boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (cpu_ops[cpu]->cpu_boot)
		return cpu_ops[cpu]->cpu_boot(cpu);

	return -EOPNOTSUPP;
}

static DECLARE_COMPLETION(cpu_running);

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret;
	long status;

	/*
	 * We need to tell the secondary core where to find its stack and the
	 * page tables.
	 */
	secondary_data.task = idle;
	secondary_data.stack = task_stack_page(idle) + THREAD_SIZE;
	update_cpu_boot_status(CPU_MMU_OFF);
	__flush_dcache_area(&secondary_data, sizeof(secondary_data));

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(cpu, idle);
	if (ret == 0) {
		/*
		 * CPU was successfully started, wait for it to come online or
		 * time out.
		 */
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_err("CPU%u: failed to boot: %d\n", cpu, ret);
		return ret;
	}

	secondary_data.task = NULL;
	secondary_data.stack = NULL;
	status = READ_ONCE(secondary_data.status);
	if (ret && status) {

		if (status == CPU_MMU_OFF)
			status = READ_ONCE(__early_cpu_boot_status);

		switch (status & CPU_BOOT_STATUS_MASK) {
		default:
			pr_err("CPU%u: failed in unknown state : 0x%lx\n",
					cpu, status);
			break;
		case CPU_KILL_ME:
			if (!op_cpu_kill(cpu)) {
				pr_crit("CPU%u: died during early boot\n", cpu);
				break;
			}
			pr_crit("CPU%u: may not have shut down cleanly\n", cpu);
			/* Fall through */
		case CPU_STUCK_IN_KERNEL:
			pr_crit("CPU%u: is stuck in kernel\n", cpu);
			if (status & CPU_STUCK_REASON_52_BIT_VA)
				pr_crit("CPU%u: does not support 52-bit VAs\n", cpu);
			if (status & CPU_STUCK_REASON_NO_GRAN)
				pr_crit("CPU%u: does not support %luK granule \n", cpu, PAGE_SIZE / SZ_1K);
			cpus_stuck_in_kernel++;
			break;
		case CPU_PANIC_KERNEL:
			panic("CPU%u detected unsupported configuration\n", cpu);
		}
	}

	return ret;
}

static void init_gic_priority_masking(void)
{
	u32 cpuflags;

	if (WARN_ON(!gic_enable_sre()))
		return;

	cpuflags = read_sysreg(daif);

	WARN_ON(!(cpuflags & PSR_I_BIT));

	gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);
}

/*
 * This is the secondary CPU boot entry.  We're using this CPUs
 * idle thread stack, but a set of temporary page tables.
 */
asmlinkage notrace void secondary_start_kernel(void)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
	struct mm_struct *mm = &init_mm;
	unsigned int cpu;

	cpu = task_cpu(current);
	set_my_cpu_offset(per_cpu_offset(cpu));

	/*
	 * All kernel threads share the same mm context; grab a
	 * reference and switch to it.
	 */
	mmgrab(mm);
	current->active_mm = mm;

	/*
	 * TTBR0 is only used for the identity mapping at this stage. Make it
	 * point to zero page to avoid speculatively fetching new entries.
	 */
	cpu_uninstall_idmap();

	if (system_uses_irq_prio_masking())
		init_gic_priority_masking();

	preempt_disable();
	trace_hardirqs_off();

	/*
	 * If the system has established the capabilities, make sure
	 * this CPU ticks all of those. If it doesn't, the CPU will
	 * fail to come online.
	 */
	check_local_cpu_capabilities();

	if (cpu_ops[cpu]->cpu_postboot)
		cpu_ops[cpu]->cpu_postboot();

	/*
	 * Log the CPU info before it is marked online and might get read.
	 */
	cpuinfo_store_cpu();

	/*
	 * Enable GIC and timers.
	 */
	notify_cpu_starting(cpu);

	store_cpu_topology(cpu);
	numa_add_cpu(cpu);

	/*
	 * OK, now it's safe to let the boot CPU continue.  Wait for
	 * the CPU migration code to notice that the CPU is online
	 * before we continue.
	 */
	pr_info("CPU%u: Booted secondary processor 0x%010lx [0x%08x]\n",
					 cpu, (unsigned long)mpidr,
					 read_cpuid_id());
	update_cpu_boot_status(CPU_BOOT_SUCCESS);
	set_cpu_online(cpu, true);
	complete(&cpu_running);

	local_daif_restore(DAIF_PROCCTX);

	/*
	 * OK, it's off to the idle thread for us
	 */
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

#ifdef CONFIG_HOTPLUG_CPU
static int op_cpu_disable(unsigned int cpu)
{
	/*
	 * If we don't have a cpu_die method, abort before we reach the point
	 * of no return. CPU0 may not have an cpu_ops, so test for it.
	 */
	if (!cpu_ops[cpu] || !cpu_ops[cpu]->cpu_die)
		return -EOPNOTSUPP;

	/*
	 * We may need to abort a hot unplug for some other mechanism-specific
	 * reason.
	 */
	if (cpu_ops[cpu]->cpu_disable)
		return cpu_ops[cpu]->cpu_disable(cpu);

	return 0;
}

/*
 * __cpu_disable runs on the processor to be shutdown.
 */
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();
	int ret;

	ret = op_cpu_disable(cpu);
	if (ret)
		return ret;

	remove_cpu_topology(cpu);
	numa_remove_cpu(cpu);

	/*
	 * Take this CPU offline.  Once we clear this, we can't return,
	 * and we must not schedule until we're ready to give up the cpu.
	 */
	set_cpu_online(cpu, false);

	/*
	 * OK - migrate IRQs away from this CPU
	 */
	irq_migrate_all_off_this_cpu();

	return 0;
}

static int op_cpu_kill(unsigned int cpu)
{
	/*
	 * If we have no means of synchronising with the dying CPU, then assume
	 * that it is really dead. We can only wait for an arbitrary length of
	 * time and hope that it's dead, so let's skip the wait and just hope.
	 */
	if (!cpu_ops[cpu]->cpu_kill)
		return 0;

	return cpu_ops[cpu]->cpu_kill(cpu);
}

/*
 * called on the thread which is asking for a CPU to be shutdown -
 * waits until shutdown has completed, or it is timed out.
 */
void __cpu_die(unsigned int cpu)
{
	int err;

	if (!cpu_wait_death(cpu, 5)) {
		pr_crit("CPU%u: cpu didn't die\n", cpu);
		return;
	}
	pr_notice("CPU%u: shutdown\n", cpu);

	/*
	 * Now that the dying CPU is beyond the point of no return w.r.t.
	 * in-kernel synchronisation, try to get the firwmare to help us to
	 * verify that it has really left the kernel before we consider
	 * clobbering anything it might still be using.
	 */
	err = op_cpu_kill(cpu);
	if (err)
		pr_warn("CPU%d may not have shut down cleanly: %d\n",
			cpu, err);
}

/*
 * Called from the idle thread for the CPU which has been shutdown.
 *
 */
void cpu_die(void)
{
	unsigned int cpu = smp_processor_id();

	idle_task_exit();

	local_daif_mask();

	/* Tell __cpu_die() that this CPU is now safe to dispose of */
	(void)cpu_report_death();

	/*
	 * Actually shutdown the CPU. This must never fail. The specific hotplug
	 * mechanism must perform all required cache maintenance to ensure that
	 * no dirty lines are lost in the process of shutting down the CPU.
	 */
	cpu_ops[cpu]->cpu_die(cpu);

	BUG();
}
#endif

/*
 * Kill the calling secondary CPU, early in bringup before it is turned
 * online.
 */
void cpu_die_early(void)
{
	int cpu = smp_processor_id();

	pr_crit("CPU%d: will not boot\n", cpu);

	/* Mark this CPU absent */
	set_cpu_present(cpu, 0);

#ifdef CONFIG_HOTPLUG_CPU
	update_cpu_boot_status(CPU_KILL_ME);
	/* Check if we can park ourselves */
	if (cpu_ops[cpu] && cpu_ops[cpu]->cpu_die)
		cpu_ops[cpu]->cpu_die(cpu);
#endif
	update_cpu_boot_status(CPU_STUCK_IN_KERNEL);

	cpu_park_loop();
}

static void __init hyp_mode_check(void)
{
	if (is_hyp_mode_available())
		pr_info("CPU: All CPU(s) started at EL2\n");
	else if (is_hyp_mode_mismatched())
		WARN_TAINT(1, TAINT_CPU_OUT_OF_SPEC,
			   "CPU: CPUs started in inconsistent modes");
	else
		pr_info("CPU: All CPU(s) started at EL1\n");
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	pr_info("SMP: Total of %d processors activated.\n", num_online_cpus());
	setup_cpu_features();
	hyp_mode_check();
	apply_alternatives_all();
	mark_linear_text_alias_ro();
}

void __init smp_prepare_boot_cpu(void)
{
	set_my_cpu_offset(per_cpu_offset(smp_processor_id()));
	cpuinfo_store_boot_cpu();

	/*
	 * We now know enough about the boot CPU to apply the
	 * alternatives that cannot wait until interrupt handling
	 * and/or scheduling is enabled.
	 */
	apply_boot_alternatives();

	/* Conditionally switch to GIC PMR for interrupt masking */
	if (system_uses_irq_prio_masking())
		init_gic_priority_masking();
}

static u64 __init of_get_cpu_mpidr(struct device_node *dn)
{
	const __be32 *cell;
	u64 hwid;

	/*
	 * A cpu node with missing "reg" property is
	 * considered invalid to build a cpu_logical_map
	 * entry.
	 */
	cell = of_get_property(dn, "reg", NULL);
	if (!cell) {
		pr_err("%pOF: missing reg property\n", dn);
		return INVALID_HWID;
	}

	hwid = of_read_number(cell, of_n_addr_cells(dn));
	/*
	 * Non affinity bits must be set to 0 in the DT
	 */
	if (hwid & ~MPIDR_HWID_BITMASK) {
		pr_err("%pOF: invalid reg property\n", dn);
		return INVALID_HWID;
	}
	return hwid;
}

/*
 * Duplicate MPIDRs are a recipe for disaster. Scan all initialized
 * entries and check for duplicates. If any is found just ignore the
 * cpu. cpu_logical_map was initialized to INVALID_HWID to avoid
 * matching valid MPIDR values.
 */
static bool __init is_mpidr_duplicate(unsigned int cpu, u64 hwid)
{
	unsigned int i;

	for (i = 1; (i < cpu) && (i < NR_CPUS); i++)
		if (cpu_logical_map(i) == hwid)
			return true;
	return false;
}

/*
 * Initialize cpu operations for a logical cpu and
 * set it in the possible mask on success
 */
static int __init smp_cpu_setup(int cpu)
{
	if (cpu_read_ops(cpu))
		return -ENODEV;

	if (cpu_ops[cpu]->cpu_init(cpu))
		return -ENODEV;

	set_cpu_possible(cpu, true);

	return 0;
}

static bool bootcpu_valid __initdata;
static unsigned int cpu_count = 1;

#ifdef CONFIG_ACPI
static struct acpi_madt_generic_interrupt cpu_madt_gicc[NR_CPUS];

struct acpi_madt_generic_interrupt *acpi_cpu_get_madt_gicc(int cpu)
{
	return &cpu_madt_gicc[cpu];
}

/*
 * acpi_map_gic_cpu_interface - parse processor MADT entry
 *
 * Carry out sanity checks on MADT processor entry and initialize
 * cpu_logical_map on success
 */
static void __init
acpi_map_gic_cpu_interface(struct acpi_madt_generic_interrupt *processor)
{
	u64 hwid = processor->arm_mpidr;

	if (!(processor->flags & ACPI_MADT_ENABLED)) {
		pr_debug("skipping disabled CPU entry with 0x%llx MPIDR\n", hwid);
		return;
	}

	if (hwid & ~MPIDR_HWID_BITMASK || hwid == INVALID_HWID) {
		pr_err("skipping CPU entry with invalid MPIDR 0x%llx\n", hwid);
		return;
	}

	if (is_mpidr_duplicate(cpu_count, hwid)) {
		pr_err("duplicate CPU MPIDR 0x%llx in MADT\n", hwid);
		return;
	}

	/* Check if GICC structure of boot CPU is available in the MADT */
	if (cpu_logical_map(0) == hwid) {
		if (bootcpu_valid) {
			pr_err("duplicate boot CPU MPIDR: 0x%llx in MADT\n",
			       hwid);
			return;
		}
		bootcpu_valid = true;
		cpu_madt_gicc[0] = *processor;
		return;
	}

	if (cpu_count >= NR_CPUS)
		return;

	/* map the logical cpu id to cpu MPIDR */
	cpu_logical_map(cpu_count) = hwid;

	cpu_madt_gicc[cpu_count] = *processor;

	/*
	 * Set-up the ACPI parking protocol cpu entries
	 * while initializing the cpu_logical_map to
	 * avoid parsing MADT entries multiple times for
	 * nothing (ie a valid cpu_logical_map entry should
	 * contain a valid parking protocol data set to
	 * initialize the cpu if the parking protocol is
	 * the only available enable method).
	 */
	acpi_set_mailbox_entry(cpu_count, processor);

	cpu_count++;
}

static int __init
acpi_parse_gic_cpu_interface(union acpi_subtable_headers *header,
			     const unsigned long end)
{
	struct acpi_madt_generic_interrupt *processor;

	processor = (struct acpi_madt_generic_interrupt *)header;
	if (BAD_MADT_GICC_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(&header->common);

	acpi_map_gic_cpu_interface(processor);

	return 0;
}

static void __init acpi_parse_and_init_cpus(void)
{
	int i;

	/*
	 * do a walk of MADT to determine how many CPUs
	 * we have including disabled CPUs, and get information
	 * we need for SMP init.
	 */
	acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				      acpi_parse_gic_cpu_interface, 0);

	/*
	 * In ACPI, SMP and CPU NUMA information is provided in separate
	 * static tables, namely the MADT and the SRAT.
	 *
	 * Thus, it is simpler to first create the cpu logical map through
	 * an MADT walk and then map the logical cpus to their node ids
	 * as separate steps.
	 */
	acpi_map_cpus_to_nodes();

	for (i = 0; i < nr_cpu_ids; i++)
		early_map_cpu_to_node(i, acpi_numa_get_nid(i));
}
#else
#define acpi_parse_and_init_cpus(...)	do { } while (0)
#endif

/*
 * Enumerate the possible CPU set from the device tree and build the
 * cpu logical map array containing MPIDR values related to logical
 * cpus. Assumes that cpu_logical_map(0) has already been initialized.
 */
static void __init of_parse_and_init_cpus(void)
{
	struct device_node *dn;

	for_each_of_cpu_node(dn) {
		u64 hwid = of_get_cpu_mpidr(dn);

		if (hwid == INVALID_HWID)
			goto next;

		if (is_mpidr_duplicate(cpu_count, hwid)) {
			pr_err("%pOF: duplicate cpu reg properties in the DT\n",
				dn);
			goto next;
		}

		/*
		 * The numbering scheme requires that the boot CPU
		 * must be assigned logical id 0. Record it so that
		 * the logical map built from DT is validated and can
		 * be used.
		 */
		if (hwid == cpu_logical_map(0)) {
			if (bootcpu_valid) {
				pr_err("%pOF: duplicate boot cpu reg property in DT\n",
					dn);
				goto next;
			}

			bootcpu_valid = true;
			early_map_cpu_to_node(0, of_node_to_nid(dn));

			/*
			 * cpu_logical_map has already been
			 * initialized and the boot cpu doesn't need
			 * the enable-method so continue without
			 * incrementing cpu.
			 */
			continue;
		}

		if (cpu_count >= NR_CPUS)
			goto next;

		pr_debug("cpu logical map 0x%llx\n", hwid);
		cpu_logical_map(cpu_count) = hwid;

		early_map_cpu_to_node(cpu_count, of_node_to_nid(dn));
next:
		cpu_count++;
	}
}

/*
 * Enumerate the possible CPU set from the device tree or ACPI and build the
 * cpu logical map array containing MPIDR values related to logical
 * cpus. Assumes that cpu_logical_map(0) has already been initialized.
 */
void __init smp_init_cpus(void)
{
	int i;

	if (acpi_disabled)
		of_parse_and_init_cpus();
	else
		acpi_parse_and_init_cpus();

	if (cpu_count > nr_cpu_ids)
		pr_warn("Number of cores (%d) exceeds configured maximum of %u - clipping\n",
			cpu_count, nr_cpu_ids);

	if (!bootcpu_valid) {
		pr_err("missing boot CPU MPIDR, not enabling secondaries\n");
		return;
	}

	/*
	 * We need to set the cpu_logical_map entries before enabling
	 * the cpus so that cpu processor description entries (DT cpu nodes
	 * and ACPI MADT entries) can be retrieved by matching the cpu hwid
	 * with entries in cpu_logical_map while initializing the cpus.
	 * If the cpu set-up fails, invalidate the cpu_logical_map entry.
	 */
	for (i = 1; i < nr_cpu_ids; i++) {
		if (cpu_logical_map(i) != INVALID_HWID) {
			if (smp_cpu_setup(i))
				cpu_logical_map(i) = INVALID_HWID;
		}
	}
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int err;
	unsigned int cpu;
	unsigned int this_cpu;

	init_cpu_topology();

	this_cpu = smp_processor_id();
	store_cpu_topology(this_cpu);
	numa_store_cpu_info(this_cpu);
	numa_add_cpu(this_cpu);

	/*
	 * If UP is mandated by "nosmp" (which implies "maxcpus=0"), don't set
	 * secondary CPUs present.
	 */
	if (max_cpus == 0)
		return;

	/*
	 * Initialise the present map (which describes the set of CPUs
	 * actually populated at the present time) and release the
	 * secondaries from the bootloader.
	 */
	for_each_possible_cpu(cpu) {

		per_cpu(cpu_number, cpu) = cpu;

		if (cpu == smp_processor_id())
			continue;

		if (!cpu_ops[cpu])
			continue;

		err = cpu_ops[cpu]->cpu_prepare(cpu);
		if (err)
			continue;

		set_cpu_present(cpu, true);
		numa_store_cpu_info(cpu);
	}
}

void (*__smp_cross_call)(const struct cpumask *, unsigned int);

void __init set_smp_cross_call(void (*fn)(const struct cpumask *, unsigned int))
{
	__smp_cross_call = fn;
}

static const char *ipi_types[NR_IPI] __tracepoint_string = {
#define S(x,s)	[x] = s
	S(IPI_RESCHEDULE, "Rescheduling interrupts"),
	S(IPI_CALL_FUNC, "Function call interrupts"),
	S(IPI_CPU_STOP, "CPU stop interrupts"),
	S(IPI_CPU_CRASH_STOP, "CPU stop (for crash dump) interrupts"),
	S(IPI_TIMER, "Timer broadcast interrupts"),
	S(IPI_IRQ_WORK, "IRQ work interrupts"),
	S(IPI_WAKEUP, "CPU wake-up interrupts"),
};

static void smp_cross_call(const struct cpumask *target, unsigned int ipinr)
{
	trace_ipi_raise(target, ipi_types[ipinr]);
	__smp_cross_call(target, ipinr);
}

void show_ipi_list(struct seq_file *p, int prec)
{
	unsigned int cpu, i;

	for (i = 0; i < NR_IPI; i++) {
		seq_printf(p, "%*s%u:%s", prec - 1, "IPI", i,
			   prec >= 4 ? " " : "");
		for_each_online_cpu(cpu)
			seq_printf(p, "%10u ",
				   __get_irq_stat(cpu, ipi_irqs[i]));
		seq_printf(p, "      %s\n", ipi_types[i]);
	}
}

u64 smp_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = 0;
	int i;

	for (i = 0; i < NR_IPI; i++)
		sum += __get_irq_stat(cpu, ipi_irqs[i]);

	return sum;
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	smp_cross_call(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	smp_cross_call(cpumask_of(cpu), IPI_CALL_FUNC);
}

#ifdef CONFIG_ARM64_ACPI_PARKING_PROTOCOL
void arch_send_wakeup_ipi_mask(const struct cpumask *mask)
{
	smp_cross_call(mask, IPI_WAKEUP);
}
#endif

#ifdef CONFIG_IRQ_WORK
void arch_irq_work_raise(void)
{
	if (__smp_cross_call)
		smp_cross_call(cpumask_of(smp_processor_id()), IPI_IRQ_WORK);
}
#endif

static void local_cpu_stop(void)
{
	set_cpu_online(smp_processor_id(), false);

	local_daif_mask();
	sdei_mask_local_cpu();
	cpu_park_loop();
}

/*
 * We need to implement panic_smp_self_stop() for parallel panic() calls, so
 * that cpu_online_mask gets correctly updated and smp_send_stop() can skip
 * CPUs that have already stopped themselves.
 */
void panic_smp_self_stop(void)
{
	local_cpu_stop();
}

#ifdef CONFIG_KEXEC_CORE
static atomic_t waiting_for_crash_ipi = ATOMIC_INIT(0);
#endif

static void ipi_cpu_crash_stop(unsigned int cpu, struct pt_regs *regs)
{
#ifdef CONFIG_KEXEC_CORE
	crash_save_cpu(regs, cpu);

	atomic_dec(&waiting_for_crash_ipi);

	local_irq_disable();
	sdei_mask_local_cpu();

#ifdef CONFIG_HOTPLUG_CPU
	if (cpu_ops[cpu]->cpu_die)
		cpu_ops[cpu]->cpu_die(cpu);
#endif

	/* just in case */
	cpu_park_loop();
#endif
}

/*
 * Main handler for inter-processor interrupts
 */
void handle_IPI(int ipinr, struct pt_regs *regs)
{
	unsigned int cpu = smp_processor_id();
	struct pt_regs *old_regs = set_irq_regs(regs);

	if ((unsigned)ipinr < NR_IPI) {
		trace_ipi_entry_rcuidle(ipi_types[ipinr]);
		__inc_irq_stat(cpu, ipi_irqs[ipinr]);
	}

	switch (ipinr) {
	case IPI_RESCHEDULE:
		scheduler_ipi();
		break;

	case IPI_CALL_FUNC:
		irq_enter();
		generic_smp_call_function_interrupt();
		irq_exit();
		break;

	case IPI_CPU_STOP:
		irq_enter();
		local_cpu_stop();
		irq_exit();
		break;

	case IPI_CPU_CRASH_STOP:
		if (IS_ENABLED(CONFIG_KEXEC_CORE)) {
			irq_enter();
			ipi_cpu_crash_stop(cpu, regs);

			unreachable();
		}
		break;

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	case IPI_TIMER:
		irq_enter();
		tick_receive_broadcast();
		irq_exit();
		break;
#endif

#ifdef CONFIG_IRQ_WORK
	case IPI_IRQ_WORK:
		irq_enter();
		irq_work_run();
		irq_exit();
		break;
#endif

#ifdef CONFIG_ARM64_ACPI_PARKING_PROTOCOL
	case IPI_WAKEUP:
		WARN_ONCE(!acpi_parking_protocol_valid(cpu),
			  "CPU%u: Wake-up IPI outside the ACPI parking protocol\n",
			  cpu);
		break;
#endif

	default:
		pr_crit("CPU%u: Unknown IPI message 0x%x\n", cpu, ipinr);
		break;
	}

	if ((unsigned)ipinr < NR_IPI)
		trace_ipi_exit_rcuidle(ipi_types[ipinr]);
	set_irq_regs(old_regs);
}

void smp_send_reschedule(int cpu)
{
	smp_cross_call(cpumask_of(cpu), IPI_RESCHEDULE);
}

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
void tick_broadcast(const struct cpumask *mask)
{
	smp_cross_call(mask, IPI_TIMER);
}
#endif

void smp_send_stop(void)
{
	unsigned long timeout;

	if (num_online_cpus() > 1) {
		cpumask_t mask;

		cpumask_copy(&mask, cpu_online_mask);
		cpumask_clear_cpu(smp_processor_id(), &mask);

		if (system_state <= SYSTEM_RUNNING)
			pr_crit("SMP: stopping secondary CPUs\n");
		smp_cross_call(&mask, IPI_CPU_STOP);
	}

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while (num_online_cpus() > 1 && timeout--)
		udelay(1);

	if (num_online_cpus() > 1)
		pr_warning("SMP: failed to stop secondary CPUs %*pbl\n",
			   cpumask_pr_args(cpu_online_mask));

	sdei_mask_local_cpu();
}

#ifdef CONFIG_KEXEC_CORE
void crash_smp_send_stop(void)
{
	static int cpus_stopped;
	cpumask_t mask;
	unsigned long timeout;

	/*
	 * This function can be called twice in panic path, but obviously
	 * we execute this only once.
	 */
	if (cpus_stopped)
		return;

	cpus_stopped = 1;

	if (num_online_cpus() == 1) {
		sdei_mask_local_cpu();
		return;
	}

	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &mask);

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);

	pr_crit("SMP: stopping secondary CPUs\n");
	smp_cross_call(&mask, IPI_CPU_CRASH_STOP);

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && timeout--)
		udelay(1);

	if (atomic_read(&waiting_for_crash_ipi) > 0)
		pr_warning("SMP: failed to stop secondary CPUs %*pbl\n",
			   cpumask_pr_args(&mask));

	sdei_mask_local_cpu();
}

bool smp_crash_stop_failed(void)
{
	return (atomic_read(&waiting_for_crash_ipi) > 0);
}
#endif

/*
 * not supported here
 */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

static bool have_cpu_die(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	int any_cpu = raw_smp_processor_id();

	if (cpu_ops[any_cpu] && cpu_ops[any_cpu]->cpu_die)
		return true;
#endif
	return false;
}

bool cpus_are_stuck_in_kernel(void)
{
	bool smp_spin_tables = (num_possible_cpus() > 1 && !have_cpu_die());

	return !!cpus_stuck_in_kernel || smp_spin_tables;
}
