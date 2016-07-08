/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Copyright (C) 2000, 2001 Kanoj Sarcar
 * Copyright (C) 2000, 2001 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2003 Broadcom Corporation
 */
#include <linux/cache.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/ftrace.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <linux/atomic.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/idle.h>
#include <asm/r4k-timer.h>
#include <asm/mips-cpc.h>
#include <asm/mmu_context.h>
#include <asm/time.h>
#include <asm/setup.h>
#include <asm/maar.h>

cpumask_t cpu_callin_map;		/* Bitmask of started secondaries */

int __cpu_number_map[NR_CPUS];		/* Map physical to logical */
EXPORT_SYMBOL(__cpu_number_map);

int __cpu_logical_map[NR_CPUS];		/* Map logical to physical */
EXPORT_SYMBOL(__cpu_logical_map);

/* Number of TCs (or siblings in Intel speak) per CPU core */
int smp_num_siblings = 1;
EXPORT_SYMBOL(smp_num_siblings);

/* representing the TCs (or siblings in Intel speak) of each logical CPU */
cpumask_t cpu_sibling_map[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(cpu_sibling_map);

/* representing the core map of multi-core chips of each logical CPU */
cpumask_t cpu_core_map[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(cpu_core_map);

/*
 * A logcal cpu mask containing only one VPE per core to
 * reduce the number of IPIs on large MT systems.
 */
cpumask_t cpu_foreign_map __read_mostly;
EXPORT_SYMBOL(cpu_foreign_map);

/* representing cpus for which sibling maps can be computed */
static cpumask_t cpu_sibling_setup_map;

/* representing cpus for which core maps can be computed */
static cpumask_t cpu_core_setup_map;

cpumask_t cpu_coherent_mask;

#ifdef CONFIG_GENERIC_IRQ_IPI
static struct irq_desc *call_desc;
static struct irq_desc *sched_desc;
#endif

static inline void set_cpu_sibling_map(int cpu)
{
	int i;

	cpumask_set_cpu(cpu, &cpu_sibling_setup_map);

	if (smp_num_siblings > 1) {
		for_each_cpu(i, &cpu_sibling_setup_map) {
			if (cpu_data[cpu].package == cpu_data[i].package &&
				    cpu_data[cpu].core == cpu_data[i].core) {
				cpumask_set_cpu(i, &cpu_sibling_map[cpu]);
				cpumask_set_cpu(cpu, &cpu_sibling_map[i]);
			}
		}
	} else
		cpumask_set_cpu(cpu, &cpu_sibling_map[cpu]);
}

static inline void set_cpu_core_map(int cpu)
{
	int i;

	cpumask_set_cpu(cpu, &cpu_core_setup_map);

	for_each_cpu(i, &cpu_core_setup_map) {
		if (cpu_data[cpu].package == cpu_data[i].package) {
			cpumask_set_cpu(i, &cpu_core_map[cpu]);
			cpumask_set_cpu(cpu, &cpu_core_map[i]);
		}
	}
}

/*
 * Calculate a new cpu_foreign_map mask whenever a
 * new cpu appears or disappears.
 */
static inline void calculate_cpu_foreign_map(void)
{
	int i, k, core_present;
	cpumask_t temp_foreign_map;

	/* Re-calculate the mask */
	cpumask_clear(&temp_foreign_map);
	for_each_online_cpu(i) {
		core_present = 0;
		for_each_cpu(k, &temp_foreign_map)
			if (cpu_data[i].package == cpu_data[k].package &&
			    cpu_data[i].core == cpu_data[k].core)
				core_present = 1;
		if (!core_present)
			cpumask_set_cpu(i, &temp_foreign_map);
	}

	cpumask_copy(&cpu_foreign_map, &temp_foreign_map);
}

struct plat_smp_ops *mp_ops;
EXPORT_SYMBOL(mp_ops);

void register_smp_ops(struct plat_smp_ops *ops)
{
	if (mp_ops)
		printk(KERN_WARNING "Overriding previously set SMP ops\n");

	mp_ops = ops;
}

#ifdef CONFIG_GENERIC_IRQ_IPI
void mips_smp_send_ipi_single(int cpu, unsigned int action)
{
	mips_smp_send_ipi_mask(cpumask_of(cpu), action);
}

void mips_smp_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned long flags;
	unsigned int core;
	int cpu;

	local_irq_save(flags);

	switch (action) {
	case SMP_CALL_FUNCTION:
		__ipi_send_mask(call_desc, mask);
		break;

	case SMP_RESCHEDULE_YOURSELF:
		__ipi_send_mask(sched_desc, mask);
		break;

	default:
		BUG();
	}

	if (mips_cpc_present()) {
		for_each_cpu(cpu, mask) {
			core = cpu_data[cpu].core;

			if (core == current_cpu_data.core)
				continue;

			while (!cpumask_test_cpu(cpu, &cpu_coherent_mask)) {
				mips_cpc_lock_other(core);
				write_cpc_co_cmd(CPC_Cx_CMD_PWRUP);
				mips_cpc_unlock_other();
			}
		}
	}

	local_irq_restore(flags);
}


static irqreturn_t ipi_resched_interrupt(int irq, void *dev_id)
{
	scheduler_ipi();

	return IRQ_HANDLED;
}

static irqreturn_t ipi_call_interrupt(int irq, void *dev_id)
{
	generic_smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static struct irqaction irq_resched = {
	.handler	= ipi_resched_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI resched"
};

static struct irqaction irq_call = {
	.handler	= ipi_call_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI call"
};

static __init void smp_ipi_init_one(unsigned int virq,
				    struct irqaction *action)
{
	int ret;

	irq_set_handler(virq, handle_percpu_irq);
	ret = setup_irq(virq, action);
	BUG_ON(ret);
}

static int __init mips_smp_ipi_init(void)
{
	unsigned int call_virq, sched_virq;
	struct irq_domain *ipidomain;
	struct device_node *node;

	node = of_irq_find_parent(of_root);
	ipidomain = irq_find_matching_host(node, DOMAIN_BUS_IPI);

	/*
	 * Some platforms have half DT setup. So if we found irq node but
	 * didn't find an ipidomain, try to search for one that is not in the
	 * DT.
	 */
	if (node && !ipidomain)
		ipidomain = irq_find_matching_host(NULL, DOMAIN_BUS_IPI);

	/*
	 * There are systems which only use IPI domains some of the time,
	 * depending upon configuration we don't know until runtime. An
	 * example is Malta where we may compile in support for GIC & the
	 * MT ASE, but run on a system which has multiple VPEs in a single
	 * core and doesn't include a GIC. Until all IPI implementations
	 * have been converted to use IPI domains the best we can do here
	 * is to return & hope some other code sets up the IPIs.
	 */
	if (!ipidomain)
		return 0;

	call_virq = irq_reserve_ipi(ipidomain, cpu_possible_mask);
	BUG_ON(!call_virq);

	sched_virq = irq_reserve_ipi(ipidomain, cpu_possible_mask);
	BUG_ON(!sched_virq);

	if (irq_domain_is_ipi_per_cpu(ipidomain)) {
		int cpu;

		for_each_cpu(cpu, cpu_possible_mask) {
			smp_ipi_init_one(call_virq + cpu, &irq_call);
			smp_ipi_init_one(sched_virq + cpu, &irq_resched);
		}
	} else {
		smp_ipi_init_one(call_virq, &irq_call);
		smp_ipi_init_one(sched_virq, &irq_resched);
	}

	call_desc = irq_to_desc(call_virq);
	sched_desc = irq_to_desc(sched_virq);

	return 0;
}
early_initcall(mips_smp_ipi_init);
#endif

/*
 * First C code run on the secondary CPUs after being started up by
 * the master.
 */
asmlinkage void start_secondary(void)
{
	unsigned int cpu;

	cpu_probe();
	per_cpu_trap_init(false);
	mips_clockevent_init();
	mp_ops->init_secondary();
	cpu_report();
	maar_init();

	/*
	 * XXX parity protection should be folded in here when it's converted
	 * to an option instead of something based on .cputype
	 */

	calibrate_delay();
	preempt_disable();
	cpu = smp_processor_id();
	cpu_data[cpu].udelay_val = loops_per_jiffy;

	cpumask_set_cpu(cpu, &cpu_coherent_mask);
	notify_cpu_starting(cpu);

	set_cpu_online(cpu, true);

	set_cpu_sibling_map(cpu);
	set_cpu_core_map(cpu);

	calculate_cpu_foreign_map();

	cpumask_set_cpu(cpu, &cpu_callin_map);

	synchronise_count_slave(cpu);

	/*
	 * irq will be enabled in ->smp_finish(), enabling it too early
	 * is dangerous.
	 */
	WARN_ON_ONCE(!irqs_disabled());
	mp_ops->smp_finish();

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

static void stop_this_cpu(void *dummy)
{
	/*
	 * Remove this CPU. Be a bit slow here and
	 * set the bits for every online CPU so we don't miss
	 * any IPI whilst taking this VPE down.
	 */

	cpumask_copy(&cpu_foreign_map, cpu_online_mask);

	/* Make it visible to every other CPU */
	smp_mb();

	set_cpu_online(smp_processor_id(), false);
	calculate_cpu_foreign_map();
	local_irq_disable();
	while (1);
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 0);
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

/* called from main before smp_init() */
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	init_new_context(current, &init_mm);
	current_thread_info()->cpu = 0;
	mp_ops->prepare_cpus(max_cpus);
	set_cpu_sibling_map(0);
	set_cpu_core_map(0);
	calculate_cpu_foreign_map();
#ifndef CONFIG_HOTPLUG_CPU
	init_cpu_present(cpu_possible_mask);
#endif
	cpumask_copy(&cpu_coherent_mask, cpu_possible_mask);
}

/* preload SMP state for boot cpu */
void smp_prepare_boot_cpu(void)
{
	set_cpu_possible(0, true);
	set_cpu_online(0, true);
	cpumask_set_cpu(0, &cpu_callin_map);
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	mp_ops->boot_secondary(cpu, tidle);

	/*
	 * Trust is futile.  We should really have timeouts ...
	 */
	while (!cpumask_test_cpu(cpu, &cpu_callin_map)) {
		udelay(100);
		schedule();
	}

	synchronise_count_master(cpu);
	return 0;
}

/* Not really SMP stuff ... */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

static void flush_tlb_all_ipi(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	on_each_cpu(flush_tlb_all_ipi, NULL, 1);
}

static void flush_tlb_mm_ipi(void *mm)
{
	local_flush_tlb_mm((struct mm_struct *)mm);
}

/*
 * Special Variant of smp_call_function for use by TLB functions:
 *
 *  o No return value
 *  o collapses to normal function call on UP kernels
 *  o collapses to normal function call on systems with a single shared
 *    primary cache.
 */
static inline void smp_on_other_tlbs(void (*func) (void *info), void *info)
{
	smp_call_function(func, info, 1);
}

static inline void smp_on_each_tlb(void (*func) (void *info), void *info)
{
	preempt_disable();

	smp_on_other_tlbs(func, info);
	func(info);

	preempt_enable();
}

/*
 * The following tlb flush calls are invoked when old translations are
 * being torn down, or pte attributes are changing. For single threaded
 * address spaces, a new context is obtained on the current cpu, and tlb
 * context on other cpus are invalidated to force a new context allocation
 * at switch_mm time, should the mm ever be used on other cpus. For
 * multithreaded address spaces, intercpu interrupts have to be sent.
 * Another case where intercpu interrupts are required is when the target
 * mm might be active on another cpu (eg debuggers doing the flushes on
 * behalf of debugees, kswapd stealing pages from another process etc).
 * Kanoj 07/00.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	preempt_disable();

	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		smp_on_other_tlbs(flush_tlb_mm_ipi, mm);
	} else {
		unsigned int cpu;

		for_each_online_cpu(cpu) {
			if (cpu != smp_processor_id() && cpu_context(cpu, mm))
				cpu_context(cpu, mm) = 0;
		}
	}
	local_flush_tlb_mm(mm);

	preempt_enable();
}

struct flush_tlb_data {
	struct vm_area_struct *vma;
	unsigned long addr1;
	unsigned long addr2;
};

static void flush_tlb_range_ipi(void *info)
{
	struct flush_tlb_data *fd = info;

	local_flush_tlb_range(fd->vma, fd->addr1, fd->addr2);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();
	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		struct flush_tlb_data fd = {
			.vma = vma,
			.addr1 = start,
			.addr2 = end,
		};

		smp_on_other_tlbs(flush_tlb_range_ipi, &fd);
	} else {
		unsigned int cpu;

		for_each_online_cpu(cpu) {
			if (cpu != smp_processor_id() && cpu_context(cpu, mm))
				cpu_context(cpu, mm) = 0;
		}
	}
	local_flush_tlb_range(vma, start, end);
	preempt_enable();
}

static void flush_tlb_kernel_range_ipi(void *info)
{
	struct flush_tlb_data *fd = info;

	local_flush_tlb_kernel_range(fd->addr1, fd->addr2);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct flush_tlb_data fd = {
		.addr1 = start,
		.addr2 = end,
	};

	on_each_cpu(flush_tlb_kernel_range_ipi, &fd, 1);
}

static void flush_tlb_page_ipi(void *info)
{
	struct flush_tlb_data *fd = info;

	local_flush_tlb_page(fd->vma, fd->addr1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	preempt_disable();
	if ((atomic_read(&vma->vm_mm->mm_users) != 1) || (current->mm != vma->vm_mm)) {
		struct flush_tlb_data fd = {
			.vma = vma,
			.addr1 = page,
		};

		smp_on_other_tlbs(flush_tlb_page_ipi, &fd);
	} else {
		unsigned int cpu;

		for_each_online_cpu(cpu) {
			if (cpu != smp_processor_id() && cpu_context(cpu, vma->vm_mm))
				cpu_context(cpu, vma->vm_mm) = 0;
		}
	}
	local_flush_tlb_page(vma, page);
	preempt_enable();
}

static void flush_tlb_one_ipi(void *info)
{
	unsigned long vaddr = (unsigned long) info;

	local_flush_tlb_one(vaddr);
}

void flush_tlb_one(unsigned long vaddr)
{
	smp_on_each_tlb(flush_tlb_one_ipi, (void *) vaddr);
}

EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(flush_tlb_one);

#if defined(CONFIG_KEXEC)
void (*dump_ipi_function_ptr)(void *) = NULL;
void dump_send_ipi(void (*dump_ipi_callback)(void *))
{
	int i;
	int cpu = smp_processor_id();

	dump_ipi_function_ptr = dump_ipi_callback;
	smp_mb();
	for_each_online_cpu(i)
		if (i != cpu)
			mp_ops->send_ipi_single(i, SMP_DUMP);

}
EXPORT_SYMBOL(dump_send_ipi);
#endif

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST

static DEFINE_PER_CPU(atomic_t, tick_broadcast_count);
static DEFINE_PER_CPU(struct call_single_data, tick_broadcast_csd);

void tick_broadcast(const struct cpumask *mask)
{
	atomic_t *count;
	struct call_single_data *csd;
	int cpu;

	for_each_cpu(cpu, mask) {
		count = &per_cpu(tick_broadcast_count, cpu);
		csd = &per_cpu(tick_broadcast_csd, cpu);

		if (atomic_inc_return(count) == 1)
			smp_call_function_single_async(cpu, csd);
	}
}

static void tick_broadcast_callee(void *info)
{
	int cpu = smp_processor_id();
	tick_receive_broadcast();
	atomic_set(&per_cpu(tick_broadcast_count, cpu), 0);
}

static int __init tick_broadcast_init(void)
{
	struct call_single_data *csd;
	int cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		csd = &per_cpu(tick_broadcast_csd, cpu);
		csd->func = tick_broadcast_callee;
	}

	return 0;
}
early_initcall(tick_broadcast_init);

#endif /* CONFIG_GENERIC_CLOCKEVENTS_BROADCAST */
