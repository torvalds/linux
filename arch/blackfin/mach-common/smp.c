/*
 * IPI management based on arch/arm/kernel/smp.c (Copyright 2002 ARM Limited)
 *
 * Copyright 2007-2009 Analog Devices Inc.
 *                         Philippe Gerum <rpm@xenomai.org>
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/interrupt.h>
#include <linux/cache.h>
#include <linux/clockchips.h>
#include <linux/profile.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <asm/cacheflush.h>
#include <asm/irq_handler.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <linux/err.h>

/*
 * Anomaly notes:
 * 05000120 - we always define corelock as 32-bit integer in L2
 */
struct corelock_slot corelock __attribute__ ((__section__(".l2.bss")));

#ifdef CONFIG_ICACHE_FLUSH_L1
unsigned long blackfin_iflush_l1_entry[NR_CPUS];
#endif

struct blackfin_initial_pda initial_pda_coreb;

enum ipi_message_type {
	BFIN_IPI_NONE,
	BFIN_IPI_TIMER,
	BFIN_IPI_RESCHEDULE,
	BFIN_IPI_CALL_FUNC,
	BFIN_IPI_CPU_STOP,
};

struct blackfin_flush_data {
	unsigned long start;
	unsigned long end;
};

void *secondary_stack;

static struct blackfin_flush_data smp_flush_data;

static DEFINE_SPINLOCK(stop_lock);

/* A magic number - stress test shows this is safe for common cases */
#define BFIN_IPI_MSGQ_LEN 5

/* Simple FIFO buffer, overflow leads to panic */
struct ipi_data {
	atomic_t count;
	atomic_t bits;
};

static DEFINE_PER_CPU(struct ipi_data, bfin_ipi);

static void ipi_cpu_stop(unsigned int cpu)
{
	spin_lock(&stop_lock);
	printk(KERN_CRIT "CPU%u: stopping\n", cpu);
	dump_stack();
	spin_unlock(&stop_lock);

	set_cpu_online(cpu, false);

	local_irq_disable();

	while (1)
		SSYNC();
}

static void ipi_flush_icache(void *info)
{
	struct blackfin_flush_data *fdata = info;

	/* Invalidate the memory holding the bounds of the flushed region. */
	blackfin_dcache_invalidate_range((unsigned long)fdata,
					 (unsigned long)fdata + sizeof(*fdata));

	/* Make sure all write buffers in the data side of the core
	 * are flushed before trying to invalidate the icache.  This
	 * needs to be after the data flush and before the icache
	 * flush so that the SSYNC does the right thing in preventing
	 * the instruction prefetcher from hitting things in cached
	 * memory at the wrong time -- it runs much further ahead than
	 * the pipeline.
	 */
	SSYNC();

	/* ipi_flaush_icache is invoked by generic flush_icache_range,
	 * so call blackfin arch icache flush directly here.
	 */
	blackfin_icache_flush_range(fdata->start, fdata->end);
}

/* Use IRQ_SUPPLE_0 to request reschedule.
 * When returning from interrupt to user space,
 * there is chance to reschedule */
static irqreturn_t ipi_handler_int0(int irq, void *dev_instance)
{
	unsigned int cpu = smp_processor_id();

	platform_clear_ipi(cpu, IRQ_SUPPLE_0);
	return IRQ_HANDLED;
}

DECLARE_PER_CPU(struct clock_event_device, coretmr_events);
void ipi_timer(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(coretmr_events, cpu);
	evt->event_handler(evt);
}

static irqreturn_t ipi_handler_int1(int irq, void *dev_instance)
{
	struct ipi_data *bfin_ipi_data;
	unsigned int cpu = smp_processor_id();
	unsigned long pending;
	unsigned long msg;

	platform_clear_ipi(cpu, IRQ_SUPPLE_1);

	smp_rmb();
	bfin_ipi_data = this_cpu_ptr(&bfin_ipi);
	while ((pending = atomic_xchg(&bfin_ipi_data->bits, 0)) != 0) {
		msg = 0;
		do {
			msg = find_next_bit(&pending, BITS_PER_LONG, msg + 1);
			switch (msg) {
			case BFIN_IPI_TIMER:
				ipi_timer();
				break;
			case BFIN_IPI_RESCHEDULE:
				scheduler_ipi();
				break;
			case BFIN_IPI_CALL_FUNC:
				generic_smp_call_function_interrupt();
				break;
			case BFIN_IPI_CPU_STOP:
				ipi_cpu_stop(cpu);
				break;
			default:
				goto out;
			}
			atomic_dec(&bfin_ipi_data->count);
		} while (msg < BITS_PER_LONG);

	}
out:
	return IRQ_HANDLED;
}

static void bfin_ipi_init(void)
{
	unsigned int cpu;
	struct ipi_data *bfin_ipi_data;
	for_each_possible_cpu(cpu) {
		bfin_ipi_data = &per_cpu(bfin_ipi, cpu);
		atomic_set(&bfin_ipi_data->bits, 0);
		atomic_set(&bfin_ipi_data->count, 0);
	}
}

void send_ipi(const struct cpumask *cpumask, enum ipi_message_type msg)
{
	unsigned int cpu;
	struct ipi_data *bfin_ipi_data;
	unsigned long flags;

	local_irq_save(flags);
	for_each_cpu(cpu, cpumask) {
		bfin_ipi_data = &per_cpu(bfin_ipi, cpu);
		atomic_or((1 << msg), &bfin_ipi_data->bits);
		atomic_inc(&bfin_ipi_data->count);
	}
	local_irq_restore(flags);
	smp_wmb();
	for_each_cpu(cpu, cpumask)
		platform_send_ipi_cpu(cpu, IRQ_SUPPLE_1);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi(cpumask_of(cpu), BFIN_IPI_CALL_FUNC);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	send_ipi(mask, BFIN_IPI_CALL_FUNC);
}

void smp_send_reschedule(int cpu)
{
	send_ipi(cpumask_of(cpu), BFIN_IPI_RESCHEDULE);

	return;
}

void smp_send_msg(const struct cpumask *mask, unsigned long type)
{
	send_ipi(mask, type);
}

void smp_timer_broadcast(const struct cpumask *mask)
{
	smp_send_msg(mask, BFIN_IPI_TIMER);
}

void smp_send_stop(void)
{
	cpumask_t callmap;

	preempt_disable();
	cpumask_copy(&callmap, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &callmap);
	if (!cpumask_empty(&callmap))
		send_ipi(&callmap, BFIN_IPI_CPU_STOP);

	preempt_enable();

	return;
}

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	secondary_stack = task_stack_page(idle) + THREAD_SIZE;

	ret = platform_boot_secondary(cpu, idle);

	secondary_stack = NULL;

	return ret;
}

static void setup_secondary(unsigned int cpu)
{
	unsigned long ilat;

	bfin_write_IMASK(0);
	CSYNC();
	ilat = bfin_read_ILAT();
	CSYNC();
	bfin_write_ILAT(ilat);
	CSYNC();

	/* Enable interrupt levels IVG7-15. IARs have been already
	 * programmed by the boot CPU.  */
	bfin_irq_flags |= IMASK_IVG15 |
	    IMASK_IVG14 | IMASK_IVG13 | IMASK_IVG12 | IMASK_IVG11 |
	    IMASK_IVG10 | IMASK_IVG9 | IMASK_IVG8 | IMASK_IVG7 | IMASK_IVGHW;
}

void secondary_start_kernel(void)
{
	unsigned int cpu = smp_processor_id();
	struct mm_struct *mm = &init_mm;

	if (_bfin_swrst & SWRST_DBL_FAULT_B) {
		printk(KERN_EMERG "CoreB Recovering from DOUBLE FAULT event\n");
#ifdef CONFIG_DEBUG_DOUBLEFAULT
		printk(KERN_EMERG " While handling exception (EXCAUSE = %#x) at %pF\n",
			initial_pda_coreb.seqstat_doublefault & SEQSTAT_EXCAUSE,
			initial_pda_coreb.retx_doublefault);
		printk(KERN_NOTICE "   DCPLB_FAULT_ADDR: %pF\n",
			initial_pda_coreb.dcplb_doublefault_addr);
		printk(KERN_NOTICE "   ICPLB_FAULT_ADDR: %pF\n",
			initial_pda_coreb.icplb_doublefault_addr);
#endif
		printk(KERN_NOTICE " The instruction at %pF caused a double exception\n",
			initial_pda_coreb.retx);
	}

	/*
	 * We want the D-cache to be enabled early, in case the atomic
	 * support code emulates cache coherence (see
	 * __ARCH_SYNC_CORE_DCACHE).
	 */
	init_exception_vectors();

	local_irq_disable();

	/* Attach the new idle task to the global mm. */
	mmget(mm);
	mmgrab(mm);
	current->active_mm = mm;

	preempt_disable();

	setup_secondary(cpu);

	platform_secondary_init(cpu);
	/* setup local core timer */
	bfin_local_timer_setup();

	local_irq_enable();

	bfin_setup_caches(cpu);

	notify_cpu_starting(cpu);
	/*
	 * Calibrate loops per jiffy value.
	 * IRQs need to be enabled here - D-cache can be invalidated
	 * in timer irq handler, so core B can read correct jiffies.
	 */
	calibrate_delay();

	/* We are done with local CPU inits, unblock the boot CPU. */
	set_cpu_online(cpu, true);
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	platform_prepare_cpus(max_cpus);
	bfin_ipi_init();
	platform_request_ipi(IRQ_SUPPLE_0, ipi_handler_int0);
	platform_request_ipi(IRQ_SUPPLE_1, ipi_handler_int1);
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	unsigned long bogosum = 0;
	unsigned int cpu;

	for_each_online_cpu(cpu)
		bogosum += loops_per_jiffy;

	printk(KERN_INFO "SMP: Total of %d processors activated "
	       "(%lu.%02lu BogoMIPS).\n",
	       num_online_cpus(),
	       bogosum / (500000/HZ),
	       (bogosum / (5000/HZ)) % 100);
}

void smp_icache_flush_range_others(unsigned long start, unsigned long end)
{
	smp_flush_data.start = start;
	smp_flush_data.end = end;

	preempt_disable();
	if (smp_call_function(&ipi_flush_icache, &smp_flush_data, 1))
		printk(KERN_WARNING "SMP: failed to run I-cache flush request on other CPUs\n");
	preempt_enable();
}
EXPORT_SYMBOL_GPL(smp_icache_flush_range_others);

#ifdef __ARCH_SYNC_CORE_ICACHE
unsigned long icache_invld_count[NR_CPUS];
void resync_core_icache(void)
{
	unsigned int cpu = get_cpu();
	blackfin_invalidate_entire_icache();
	icache_invld_count[cpu]++;
	put_cpu();
}
EXPORT_SYMBOL(resync_core_icache);
#endif

#ifdef __ARCH_SYNC_CORE_DCACHE
unsigned long dcache_invld_count[NR_CPUS];
unsigned long barrier_mask __attribute__ ((__section__(".l2.bss")));

void resync_core_dcache(void)
{
	unsigned int cpu = get_cpu();
	blackfin_invalidate_entire_dcache();
	dcache_invld_count[cpu]++;
	put_cpu();
}
EXPORT_SYMBOL(resync_core_dcache);
#endif

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		return -EPERM;

	set_cpu_online(cpu, false);
	return 0;
}

int __cpu_die(unsigned int cpu)
{
	return cpu_wait_death(cpu, 5);
}

void cpu_die(void)
{
	(void)cpu_report_death();

	atomic_dec(&init_mm.mm_users);
	atomic_dec(&init_mm.mm_count);

	local_irq_disable();
	platform_cpu_die();
}
#endif
