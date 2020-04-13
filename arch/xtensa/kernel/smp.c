/*
 * Xtensa SMP support functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2013 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com>
 * Pete Delaney <piet@tensilica.com
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/sched/mm.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/task_stack.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/thread_info.h>

#include <asm/cacheflush.h>
#include <asm/kdebug.h>
#include <asm/mmu_context.h>
#include <asm/mxregs.h>
#include <asm/platform.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>

#ifdef CONFIG_SMP
# if XCHAL_HAVE_S32C1I == 0
#  error "The S32C1I option is required for SMP."
# endif
#endif

static void system_invalidate_dcache_range(unsigned long start,
		unsigned long size);
static void system_flush_invalidate_dcache_range(unsigned long start,
		unsigned long size);

/* IPI (Inter Process Interrupt) */

#define IPI_IRQ	0

static irqreturn_t ipi_interrupt(int irq, void *dev_id);

void ipi_init(void)
{
	unsigned irq = irq_create_mapping(NULL, IPI_IRQ);
	if (request_irq(irq, ipi_interrupt, IRQF_PERCPU, "ipi", NULL))
		pr_err("Failed to request irq %u (ipi)\n", irq);
}

static inline unsigned int get_core_count(void)
{
	/* Bits 18..21 of SYSCFGID contain the core count minus 1. */
	unsigned int syscfgid = get_er(SYSCFGID);
	return ((syscfgid >> 18) & 0xf) + 1;
}

static inline int get_core_id(void)
{
	/* Bits 0...18 of SYSCFGID contain the core id  */
	unsigned int core_id = get_er(SYSCFGID);
	return core_id & 0x3fff;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned i;

	for_each_possible_cpu(i)
		set_cpu_present(i, true);
}

void __init smp_init_cpus(void)
{
	unsigned i;
	unsigned int ncpus = get_core_count();
	unsigned int core_id = get_core_id();

	pr_info("%s: Core Count = %d\n", __func__, ncpus);
	pr_info("%s: Core Id = %d\n", __func__, core_id);

	if (ncpus > NR_CPUS) {
		ncpus = NR_CPUS;
		pr_info("%s: limiting core count by %d\n", __func__, ncpus);
	}

	for (i = 0; i < ncpus; ++i)
		set_cpu_possible(i, true);
}

void __init smp_prepare_boot_cpu(void)
{
	unsigned int cpu = smp_processor_id();
	BUG_ON(cpu != 0);
	cpu_asid_cache(cpu) = ASID_USER_FIRST;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

static int boot_secondary_processors = 1; /* Set with xt-gdb via .xt-gdb */
static DECLARE_COMPLETION(cpu_running);

void secondary_start_kernel(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	init_mmu();

#ifdef CONFIG_DEBUG_MISC
	if (boot_secondary_processors == 0) {
		pr_debug("%s: boot_secondary_processors:%d; Hanging cpu:%d\n",
			__func__, boot_secondary_processors, cpu);
		for (;;)
			__asm__ __volatile__ ("waiti " __stringify(LOCKLEVEL));
	}

	pr_debug("%s: boot_secondary_processors:%d; Booting cpu:%d\n",
		__func__, boot_secondary_processors, cpu);
#endif
	/* Init EXCSAVE1 */

	secondary_trap_init();

	/* All kernel threads share the same mm context. */

	mmget(mm);
	mmgrab(mm);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));
	enter_lazy_tlb(mm, current);

	preempt_disable();
	trace_hardirqs_off();

	calibrate_delay();

	notify_cpu_starting(cpu);

	secondary_init_irq();
	local_timer_setup(cpu);

	set_cpu_online(cpu, true);

	local_irq_enable();

	complete(&cpu_running);

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

static void mx_cpu_start(void *p)
{
	unsigned cpu = (unsigned)p;
	unsigned long run_stall_mask = get_er(MPSCORE);

	set_er(run_stall_mask & ~(1u << cpu), MPSCORE);
	pr_debug("%s: cpu: %d, run_stall_mask: %lx ---> %lx\n",
			__func__, cpu, run_stall_mask, get_er(MPSCORE));
}

static void mx_cpu_stop(void *p)
{
	unsigned cpu = (unsigned)p;
	unsigned long run_stall_mask = get_er(MPSCORE);

	set_er(run_stall_mask | (1u << cpu), MPSCORE);
	pr_debug("%s: cpu: %d, run_stall_mask: %lx ---> %lx\n",
			__func__, cpu, run_stall_mask, get_er(MPSCORE));
}

#ifdef CONFIG_HOTPLUG_CPU
unsigned long cpu_start_id __cacheline_aligned;
#endif
unsigned long cpu_start_ccount;

static int boot_secondary(unsigned int cpu, struct task_struct *ts)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	unsigned long ccount;
	int i;

#ifdef CONFIG_HOTPLUG_CPU
	WRITE_ONCE(cpu_start_id, cpu);
	/* Pairs with the third memw in the cpu_restart */
	mb();
	system_flush_invalidate_dcache_range((unsigned long)&cpu_start_id,
					     sizeof(cpu_start_id));
#endif
	smp_call_function_single(0, mx_cpu_start, (void *)cpu, 1);

	for (i = 0; i < 2; ++i) {
		do
			ccount = get_ccount();
		while (!ccount);

		WRITE_ONCE(cpu_start_ccount, ccount);

		do {
			/*
			 * Pairs with the first two memws in the
			 * .Lboot_secondary.
			 */
			mb();
			ccount = READ_ONCE(cpu_start_ccount);
		} while (ccount && time_before(jiffies, timeout));

		if (ccount) {
			smp_call_function_single(0, mx_cpu_stop,
						 (void *)cpu, 1);
			WRITE_ONCE(cpu_start_ccount, 0);
			return -EIO;
		}
	}
	return 0;
}

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret = 0;

	if (cpu_asid_cache(cpu) == 0)
		cpu_asid_cache(cpu) = ASID_USER_FIRST;

	start_info.stack = (unsigned long)task_pt_regs(idle);
	wmb();

	pr_debug("%s: Calling wakeup_secondary(cpu:%d, idle:%p, sp: %08lx)\n",
			__func__, cpu, idle, start_info.stack);

	init_completion(&cpu_running);
	ret = boot_secondary(cpu, idle);
	if (ret == 0) {
		wait_for_completion_timeout(&cpu_running,
				msecs_to_jiffies(1000));
		if (!cpu_online(cpu))
			ret = -EIO;
	}

	if (ret)
		pr_err("CPU %u failed to boot\n", cpu);

	return ret;
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * __cpu_disable runs on the processor to be shutdown.
 */
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	/*
	 * Take this CPU offline.  Once we clear this, we can't return,
	 * and we must not schedule until we're ready to give up the cpu.
	 */
	set_cpu_online(cpu, false);

	/*
	 * OK - migrate IRQs away from this CPU
	 */
	migrate_irqs();

	/*
	 * Flush user cache and TLB mappings, and then remove this CPU
	 * from the vm mask set of all processes.
	 */
	local_flush_cache_all();
	local_flush_tlb_all();
	invalidate_page_directory();

	clear_tasks_mm_cpumask(cpu);

	return 0;
}

static void platform_cpu_kill(unsigned int cpu)
{
	smp_call_function_single(0, mx_cpu_stop, (void *)cpu, true);
}

/*
 * called on the thread which is asking for a CPU to be shutdown -
 * waits until shutdown has completed, or it is timed out.
 */
void __cpu_die(unsigned int cpu)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	while (time_before(jiffies, timeout)) {
		system_invalidate_dcache_range((unsigned long)&cpu_start_id,
					       sizeof(cpu_start_id));
		/* Pairs with the second memw in the cpu_restart */
		mb();
		if (READ_ONCE(cpu_start_id) == -cpu) {
			platform_cpu_kill(cpu);
			return;
		}
	}
	pr_err("CPU%u: unable to kill\n", cpu);
}

void arch_cpu_idle_dead(void)
{
	cpu_die();
}
/*
 * Called from the idle thread for the CPU which has been shutdown.
 *
 * Note that we disable IRQs here, but do not re-enable them
 * before returning to the caller. This is also the behaviour
 * of the other hotplug-cpu capable cores, so presumably coming
 * out of idle fixes this.
 */
void __ref cpu_die(void)
{
	idle_task_exit();
	local_irq_disable();
	__asm__ __volatile__(
			"	movi	a2, cpu_restart\n"
			"	jx	a2\n");
}

#endif /* CONFIG_HOTPLUG_CPU */

enum ipi_msg_type {
	IPI_RESCHEDULE = 0,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
	IPI_MAX
};

static const struct {
	const char *short_text;
	const char *long_text;
} ipi_text[] = {
	{ .short_text = "RES", .long_text = "Rescheduling interrupts" },
	{ .short_text = "CAL", .long_text = "Function call interrupts" },
	{ .short_text = "DIE", .long_text = "CPU shutdown interrupts" },
};

struct ipi_data {
	unsigned long ipi_count[IPI_MAX];
};

static DEFINE_PER_CPU(struct ipi_data, ipi_data);

static void send_ipi_message(const struct cpumask *callmask,
		enum ipi_msg_type msg_id)
{
	int index;
	unsigned long mask = 0;

	for_each_cpu(index, callmask)
		mask |= 1 << index;

	set_er(mask, MIPISET(msg_id));
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	send_ipi_message(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_CALL_FUNC);
}

void smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

void smp_send_stop(void)
{
	struct cpumask targets;

	cpumask_copy(&targets, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &targets);
	send_ipi_message(&targets, IPI_CPU_STOP);
}

static void ipi_cpu_stop(unsigned int cpu)
{
	set_cpu_online(cpu, false);
	machine_halt();
}

irqreturn_t ipi_interrupt(int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();
	struct ipi_data *ipi = &per_cpu(ipi_data, cpu);

	for (;;) {
		unsigned int msg;

		msg = get_er(MIPICAUSE(cpu));
		set_er(msg, MIPICAUSE(cpu));

		if (!msg)
			break;

		if (msg & (1 << IPI_CALL_FUNC)) {
			++ipi->ipi_count[IPI_CALL_FUNC];
			generic_smp_call_function_interrupt();
		}

		if (msg & (1 << IPI_RESCHEDULE)) {
			++ipi->ipi_count[IPI_RESCHEDULE];
			scheduler_ipi();
		}

		if (msg & (1 << IPI_CPU_STOP)) {
			++ipi->ipi_count[IPI_CPU_STOP];
			ipi_cpu_stop(cpu);
		}
	}

	return IRQ_HANDLED;
}

void show_ipi_list(struct seq_file *p, int prec)
{
	unsigned int cpu;
	unsigned i;

	for (i = 0; i < IPI_MAX; ++i) {
		seq_printf(p, "%*s:", prec, ipi_text[i].short_text);
		for_each_online_cpu(cpu)
			seq_printf(p, " %10lu",
					per_cpu(ipi_data, cpu).ipi_count[i]);
		seq_printf(p, "   %s\n", ipi_text[i].long_text);
	}
}

int setup_profiling_timer(unsigned int multiplier)
{
	pr_debug("setup_profiling_timer %d\n", multiplier);
	return 0;
}

/* TLB flush functions */

struct flush_data {
	struct vm_area_struct *vma;
	unsigned long addr1;
	unsigned long addr2;
};

static void ipi_flush_tlb_all(void *arg)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	on_each_cpu(ipi_flush_tlb_all, NULL, 1);
}

static void ipi_flush_tlb_mm(void *arg)
{
	local_flush_tlb_mm(arg);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	on_each_cpu(ipi_flush_tlb_mm, mm, 1);
}

static void ipi_flush_tlb_page(void *arg)
{
	struct flush_data *fd = arg;
	local_flush_tlb_page(fd->vma, fd->addr1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct flush_data fd = {
		.vma = vma,
		.addr1 = addr,
	};
	on_each_cpu(ipi_flush_tlb_page, &fd, 1);
}

static void ipi_flush_tlb_range(void *arg)
{
	struct flush_data *fd = arg;
	local_flush_tlb_range(fd->vma, fd->addr1, fd->addr2);
}

void flush_tlb_range(struct vm_area_struct *vma,
		     unsigned long start, unsigned long end)
{
	struct flush_data fd = {
		.vma = vma,
		.addr1 = start,
		.addr2 = end,
	};
	on_each_cpu(ipi_flush_tlb_range, &fd, 1);
}

static void ipi_flush_tlb_kernel_range(void *arg)
{
	struct flush_data *fd = arg;
	local_flush_tlb_kernel_range(fd->addr1, fd->addr2);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct flush_data fd = {
		.addr1 = start,
		.addr2 = end,
	};
	on_each_cpu(ipi_flush_tlb_kernel_range, &fd, 1);
}

/* Cache flush functions */

static void ipi_flush_cache_all(void *arg)
{
	local_flush_cache_all();
}

void flush_cache_all(void)
{
	on_each_cpu(ipi_flush_cache_all, NULL, 1);
}

static void ipi_flush_cache_page(void *arg)
{
	struct flush_data *fd = arg;
	local_flush_cache_page(fd->vma, fd->addr1, fd->addr2);
}

void flush_cache_page(struct vm_area_struct *vma,
		     unsigned long address, unsigned long pfn)
{
	struct flush_data fd = {
		.vma = vma,
		.addr1 = address,
		.addr2 = pfn,
	};
	on_each_cpu(ipi_flush_cache_page, &fd, 1);
}

static void ipi_flush_cache_range(void *arg)
{
	struct flush_data *fd = arg;
	local_flush_cache_range(fd->vma, fd->addr1, fd->addr2);
}

void flush_cache_range(struct vm_area_struct *vma,
		     unsigned long start, unsigned long end)
{
	struct flush_data fd = {
		.vma = vma,
		.addr1 = start,
		.addr2 = end,
	};
	on_each_cpu(ipi_flush_cache_range, &fd, 1);
}

static void ipi_flush_icache_range(void *arg)
{
	struct flush_data *fd = arg;
	local_flush_icache_range(fd->addr1, fd->addr2);
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	struct flush_data fd = {
		.addr1 = start,
		.addr2 = end,
	};
	on_each_cpu(ipi_flush_icache_range, &fd, 1);
}
EXPORT_SYMBOL(flush_icache_range);

/* ------------------------------------------------------------------------- */

static void ipi_invalidate_dcache_range(void *arg)
{
	struct flush_data *fd = arg;
	__invalidate_dcache_range(fd->addr1, fd->addr2);
}

static void system_invalidate_dcache_range(unsigned long start,
		unsigned long size)
{
	struct flush_data fd = {
		.addr1 = start,
		.addr2 = size,
	};
	on_each_cpu(ipi_invalidate_dcache_range, &fd, 1);
}

static void ipi_flush_invalidate_dcache_range(void *arg)
{
	struct flush_data *fd = arg;
	__flush_invalidate_dcache_range(fd->addr1, fd->addr2);
}

static void system_flush_invalidate_dcache_range(unsigned long start,
		unsigned long size)
{
	struct flush_data fd = {
		.addr1 = start,
		.addr2 = size,
	};
	on_each_cpu(ipi_flush_invalidate_dcache_range, &fd, 1);
}
