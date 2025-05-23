// SPDX-License-Identifier: GPL-2.0-or-later
/*
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
#include <linux/profile.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched/mm.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/ftrace.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <linux/atomic.h>
#include <asm/cpu.h>
#include <asm/ginvt.h>
#include <asm/processor.h>
#include <asm/idle.h>
#include <asm/r4k-timer.h>
#include <asm/mips-cps.h>
#include <asm/mmu_context.h>
#include <asm/time.h>
#include <asm/setup.h>
#include <asm/maar.h>

int __cpu_number_map[CONFIG_MIPS_NR_CPU_NR_MAP];   /* Map physical to logical */
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

#ifndef CONFIG_HOTPLUG_PARALLEL
static DECLARE_COMPLETION(cpu_starting);
static DECLARE_COMPLETION(cpu_running);
#endif

/*
 * A logical cpu mask containing only one VPE per core to
 * reduce the number of IPIs on large MT systems.
 */
cpumask_t cpu_foreign_map[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(cpu_foreign_map);

/* representing cpus for which sibling maps can be computed */
static cpumask_t cpu_sibling_setup_map;

/* representing cpus for which core maps can be computed */
static cpumask_t cpu_core_setup_map;

cpumask_t cpu_coherent_mask;

struct cpumask __cpu_primary_thread_mask __read_mostly;

unsigned int smp_max_threads __initdata = UINT_MAX;

static int __init early_nosmt(char *s)
{
	smp_max_threads = 1;
	return 0;
}
early_param("nosmt", early_nosmt);

static int __init early_smt(char *s)
{
	get_option(&s, &smp_max_threads);
	/* Ensure at least one thread is available */
	smp_max_threads = clamp_val(smp_max_threads, 1U, UINT_MAX);
	return 0;
}
early_param("smt", early_smt);

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
			if (cpus_are_siblings(cpu, i)) {
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
void calculate_cpu_foreign_map(void)
{
	int i, k, core_present;
	cpumask_t temp_foreign_map;

	/* Re-calculate the mask */
	cpumask_clear(&temp_foreign_map);
	for_each_online_cpu(i) {
		core_present = 0;
		for_each_cpu(k, &temp_foreign_map)
			if (cpus_are_siblings(i, k))
				core_present = 1;
		if (!core_present)
			cpumask_set_cpu(i, &temp_foreign_map);
	}

	for_each_online_cpu(i)
		cpumask_andnot(&cpu_foreign_map[i],
			       &temp_foreign_map, &cpu_sibling_map[i]);
}

const struct plat_smp_ops *mp_ops;
EXPORT_SYMBOL(mp_ops);

void register_smp_ops(const struct plat_smp_ops *ops)
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
			if (cpus_are_siblings(cpu, smp_processor_id()))
				continue;

			core = cpu_core(&cpu_data[cpu]);

			while (!cpumask_test_cpu(cpu, &cpu_coherent_mask)) {
				mips_cm_lock_other_cpu(cpu, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
				mips_cpc_lock_other(core);
				write_cpc_co_cmd(CPC_Cx_CMD_PWRUP);
				mips_cpc_unlock_other();
				mips_cm_unlock_other();
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

static void smp_ipi_init_one(unsigned int virq, const char *name,
			     irq_handler_t handler)
{
	int ret;

	irq_set_handler(virq, handle_percpu_irq);
	ret = request_irq(virq, handler, IRQF_PERCPU, name, NULL);
	BUG_ON(ret);
}

static unsigned int call_virq, sched_virq;

int mips_smp_ipi_allocate(const struct cpumask *mask)
{
	int virq;
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
	 * There are systems which use IPI IRQ domains, but only have one
	 * registered when some runtime condition is met. For example a Malta
	 * kernel may include support for GIC & CPU interrupt controller IPI
	 * IRQ domains, but if run on a system with no GIC & no MT ASE then
	 * neither will be supported or registered.
	 *
	 * We only have a problem if we're actually using multiple CPUs so fail
	 * loudly if that is the case. Otherwise simply return, skipping IPI
	 * setup, if we're running with only a single CPU.
	 */
	if (!ipidomain) {
		BUG_ON(num_present_cpus() > 1);
		return 0;
	}

	virq = irq_reserve_ipi(ipidomain, mask);
	BUG_ON(!virq);
	if (!call_virq)
		call_virq = virq;

	virq = irq_reserve_ipi(ipidomain, mask);
	BUG_ON(!virq);
	if (!sched_virq)
		sched_virq = virq;

	if (irq_domain_is_ipi_per_cpu(ipidomain)) {
		int cpu;

		for_each_cpu(cpu, mask) {
			smp_ipi_init_one(call_virq + cpu, "IPI call",
					 ipi_call_interrupt);
			smp_ipi_init_one(sched_virq + cpu, "IPI resched",
					 ipi_resched_interrupt);
		}
	} else {
		smp_ipi_init_one(call_virq, "IPI call", ipi_call_interrupt);
		smp_ipi_init_one(sched_virq, "IPI resched",
				 ipi_resched_interrupt);
	}

	return 0;
}

int mips_smp_ipi_free(const struct cpumask *mask)
{
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

	BUG_ON(!ipidomain);

	if (irq_domain_is_ipi_per_cpu(ipidomain)) {
		int cpu;

		for_each_cpu(cpu, mask) {
			free_irq(call_virq + cpu, NULL);
			free_irq(sched_virq + cpu, NULL);
		}
	}
	irq_destroy_ipi(call_virq, mask);
	irq_destroy_ipi(sched_virq, mask);
	return 0;
}


static int __init mips_smp_ipi_init(void)
{
	if (num_possible_cpus() == 1)
		return 0;

	mips_smp_ipi_allocate(cpu_possible_mask);

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
	unsigned int cpu = raw_smp_processor_id();

	cpu_probe();
	per_cpu_trap_init(false);
	rcutree_report_cpu_starting(cpu);
	mips_clockevent_init();
	mp_ops->init_secondary();
	cpu_report();
	maar_init();

	/*
	 * XXX parity protection should be folded in here when it's converted
	 * to an option instead of something based on .cputype
	 */

#ifdef CONFIG_HOTPLUG_PARALLEL
	cpuhp_ap_sync_alive();
#endif
	calibrate_delay();
	cpu_data[cpu].udelay_val = loops_per_jiffy;

	set_cpu_sibling_map(cpu);
	set_cpu_core_map(cpu);

	cpumask_set_cpu(cpu, &cpu_coherent_mask);
	notify_cpu_starting(cpu);

#ifndef CONFIG_HOTPLUG_PARALLEL
	/* Notify boot CPU that we're starting & ready to sync counters */
	complete(&cpu_starting);
#endif

	synchronise_count_slave(cpu);

	/* The CPU is running and counters synchronised, now mark it online */
	set_cpu_online(cpu, true);

	calculate_cpu_foreign_map();

#ifndef CONFIG_HOTPLUG_PARALLEL
	/*
	 * Notify boot CPU that we're up & online and it can safely return
	 * from __cpu_up
	 */
	complete(&cpu_running);
#endif

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
	 * Remove this CPU:
	 */

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
void __init smp_prepare_boot_cpu(void)
{
	if (mp_ops->prepare_boot_cpu)
		mp_ops->prepare_boot_cpu();
	set_cpu_possible(0, true);
	set_cpu_online(0, true);
}

#ifdef CONFIG_HOTPLUG_PARALLEL
int arch_cpuhp_kick_ap_alive(unsigned int cpu, struct task_struct *tidle)
{
	return mp_ops->boot_secondary(cpu, tidle);
}
#else
int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int err;

	err = mp_ops->boot_secondary(cpu, tidle);
	if (err)
		return err;

	/* Wait for CPU to start and be ready to sync counters */
	if (!wait_for_completion_timeout(&cpu_starting,
					 msecs_to_jiffies(1000))) {
		pr_crit("CPU%u: failed to start\n", cpu);
		return -EIO;
	}

	/* Wait for CPU to finish startup & mark itself online before return */
	wait_for_completion(&cpu_running);
	return 0;
}
#endif

#ifdef CONFIG_PROFILING
/* Not really SMP stuff ... */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}
#endif

static void flush_tlb_all_ipi(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	if (cpu_has_mmid) {
		htw_stop();
		ginvt_full();
		sync_ginv();
		instruction_hazard();
		htw_start();
		return;
	}

	on_each_cpu(flush_tlb_all_ipi, NULL, 1);
}

static void flush_tlb_mm_ipi(void *mm)
{
	drop_mmu_context((struct mm_struct *)mm);
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
 * multithreaded address spaces, inter-CPU interrupts have to be sent.
 * Another case where inter-CPU interrupts are required is when the target
 * mm might be active on another cpu (eg debuggers doing the flushes on
 * behalf of debugees, kswapd stealing pages from another process etc).
 * Kanoj 07/00.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	if (!mm)
		return;

	if (atomic_read(&mm->mm_users) == 0)
		return;		/* happens as a result of exit_mmap() */

	preempt_disable();

	if (cpu_has_mmid) {
		/*
		 * No need to worry about other CPUs - the ginvt in
		 * drop_mmu_context() will be globalized.
		 */
	} else if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		smp_on_other_tlbs(flush_tlb_mm_ipi, mm);
	} else {
		unsigned int cpu;

		for_each_online_cpu(cpu) {
			if (cpu != smp_processor_id() && cpu_context(cpu, mm))
				set_cpu_context(cpu, mm, 0);
		}
	}
	drop_mmu_context(mm);

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
	unsigned long addr;
	u32 old_mmid;

	preempt_disable();
	if (cpu_has_mmid) {
		htw_stop();
		old_mmid = read_c0_memorymapid();
		write_c0_memorymapid(cpu_asid(0, mm));
		mtc0_tlbw_hazard();
		addr = round_down(start, PAGE_SIZE * 2);
		end = round_up(end, PAGE_SIZE * 2);
		do {
			ginvt_va_mmid(addr);
			sync_ginv();
			addr += PAGE_SIZE * 2;
		} while (addr < end);
		write_c0_memorymapid(old_mmid);
		instruction_hazard();
		htw_start();
	} else if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		struct flush_tlb_data fd = {
			.vma = vma,
			.addr1 = start,
			.addr2 = end,
		};

		smp_on_other_tlbs(flush_tlb_range_ipi, &fd);
		local_flush_tlb_range(vma, start, end);
	} else {
		unsigned int cpu;
		int exec = vma->vm_flags & VM_EXEC;

		for_each_online_cpu(cpu) {
			/*
			 * flush_cache_range() will only fully flush icache if
			 * the VMA is executable, otherwise we must invalidate
			 * ASID without it appearing to has_valid_asid() as if
			 * mm has been completely unused by that CPU.
			 */
			if (cpu != smp_processor_id() && cpu_context(cpu, mm))
				set_cpu_context(cpu, mm, !exec);
		}
		local_flush_tlb_range(vma, start, end);
	}
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
	u32 old_mmid;

	preempt_disable();
	if (cpu_has_mmid) {
		htw_stop();
		old_mmid = read_c0_memorymapid();
		write_c0_memorymapid(cpu_asid(0, vma->vm_mm));
		mtc0_tlbw_hazard();
		ginvt_va_mmid(page);
		sync_ginv();
		write_c0_memorymapid(old_mmid);
		instruction_hazard();
		htw_start();
	} else if ((atomic_read(&vma->vm_mm->mm_users) != 1) ||
		   (current->mm != vma->vm_mm)) {
		struct flush_tlb_data fd = {
			.vma = vma,
			.addr1 = page,
		};

		smp_on_other_tlbs(flush_tlb_page_ipi, &fd);
		local_flush_tlb_page(vma, page);
	} else {
		unsigned int cpu;

		for_each_online_cpu(cpu) {
			/*
			 * flush_cache_page() only does partial flushes, so
			 * invalidate ASID without it appearing to
			 * has_valid_asid() as if mm has been completely unused
			 * by that CPU.
			 */
			if (cpu != smp_processor_id() && cpu_context(cpu, vma->vm_mm))
				set_cpu_context(cpu, vma->vm_mm, 1);
		}
		local_flush_tlb_page(vma, page);
	}
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

#ifdef CONFIG_HOTPLUG_CORE_SYNC_DEAD
void arch_cpuhp_cleanup_dead_cpu(unsigned int cpu)
{
	if (mp_ops->cleanup_dead_cpu)
		mp_ops->cleanup_dead_cpu(cpu);
}
#endif

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST

static void tick_broadcast_callee(void *info)
{
	tick_receive_broadcast();
}

static DEFINE_PER_CPU(call_single_data_t, tick_broadcast_csd) =
	CSD_INIT(tick_broadcast_callee, NULL);

void tick_broadcast(const struct cpumask *mask)
{
	call_single_data_t *csd;
	int cpu;

	for_each_cpu(cpu, mask) {
		csd = &per_cpu(tick_broadcast_csd, cpu);
		smp_call_function_single_async(cpu, csd);
	}
}

#endif /* CONFIG_GENERIC_CLOCKEVENTS_BROADCAST */
