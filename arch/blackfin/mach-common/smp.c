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
#include <linux/sched.h>
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

struct blackfin_initial_pda __cpuinitdata initial_pda_coreb;

#define BFIN_IPI_TIMER	      0
#define BFIN_IPI_RESCHEDULE   1
#define BFIN_IPI_CALL_FUNC    2
#define BFIN_IPI_CPU_STOP     3

struct blackfin_flush_data {
	unsigned long start;
	unsigned long end;
};

void *secondary_stack;


struct smp_call_struct {
	void (*func)(void *info);
	void *info;
	int wait;
	cpumask_t *waitmask;
};

static struct blackfin_flush_data smp_flush_data;

static DEFINE_SPINLOCK(stop_lock);

struct ipi_message {
	unsigned long type;
	struct smp_call_struct call_struct;
};

/* A magic number - stress test shows this is safe for common cases */
#define BFIN_IPI_MSGQ_LEN 5

/* Simple FIFO buffer, overflow leads to panic */
struct ipi_message_queue {
	spinlock_t lock;
	unsigned long count;
	unsigned long head; /* head of the queue */
	struct ipi_message ipi_message[BFIN_IPI_MSGQ_LEN];
};

static DEFINE_PER_CPU(struct ipi_message_queue, ipi_msg_queue);

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

static void ipi_call_function(unsigned int cpu, struct ipi_message *msg)
{
	int wait;
	void (*func)(void *info);
	void *info;
	func = msg->call_struct.func;
	info = msg->call_struct.info;
	wait = msg->call_struct.wait;
	func(info);
	if (wait) {
#ifdef __ARCH_SYNC_CORE_DCACHE
		/*
		 * 'wait' usually means synchronization between CPUs.
		 * Invalidate D cache in case shared data was changed
		 * by func() to ensure cache coherence.
		 */
		resync_core_dcache();
#endif
		cpumask_clear_cpu(cpu, msg->call_struct.waitmask);
	}
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
	struct ipi_message *msg;
	struct ipi_message_queue *msg_queue;
	unsigned int cpu = smp_processor_id();
	unsigned long flags;

	platform_clear_ipi(cpu, IRQ_SUPPLE_1);

	msg_queue = &__get_cpu_var(ipi_msg_queue);

	spin_lock_irqsave(&msg_queue->lock, flags);

	while (msg_queue->count) {
		msg = &msg_queue->ipi_message[msg_queue->head];
		switch (msg->type) {
		case BFIN_IPI_TIMER:
			ipi_timer();
			break;
		case BFIN_IPI_RESCHEDULE:
			scheduler_ipi();
			break;
		case BFIN_IPI_CALL_FUNC:
			ipi_call_function(cpu, msg);
			break;
		case BFIN_IPI_CPU_STOP:
			ipi_cpu_stop(cpu);
			break;
		default:
			printk(KERN_CRIT "CPU%u: Unknown IPI message 0x%lx\n",
			       cpu, msg->type);
			break;
		}
		msg_queue->head++;
		msg_queue->head %= BFIN_IPI_MSGQ_LEN;
		msg_queue->count--;
	}
	spin_unlock_irqrestore(&msg_queue->lock, flags);
	return IRQ_HANDLED;
}

static void ipi_queue_init(void)
{
	unsigned int cpu;
	struct ipi_message_queue *msg_queue;
	for_each_possible_cpu(cpu) {
		msg_queue = &per_cpu(ipi_msg_queue, cpu);
		spin_lock_init(&msg_queue->lock);
		msg_queue->count = 0;
		msg_queue->head = 0;
	}
}

static inline void smp_send_message(cpumask_t callmap, unsigned long type,
					void (*func) (void *info), void *info, int wait)
{
	unsigned int cpu;
	struct ipi_message_queue *msg_queue;
	struct ipi_message *msg;
	unsigned long flags, next_msg;
	cpumask_t waitmask; /* waitmask is shared by all cpus */

	cpumask_copy(&waitmask, &callmap);
	for_each_cpu(cpu, &callmap) {
		msg_queue = &per_cpu(ipi_msg_queue, cpu);
		spin_lock_irqsave(&msg_queue->lock, flags);
		if (msg_queue->count < BFIN_IPI_MSGQ_LEN) {
			next_msg = (msg_queue->head + msg_queue->count)
					% BFIN_IPI_MSGQ_LEN;
			msg = &msg_queue->ipi_message[next_msg];
			msg->type = type;
			if (type == BFIN_IPI_CALL_FUNC) {
				msg->call_struct.func = func;
				msg->call_struct.info = info;
				msg->call_struct.wait = wait;
				msg->call_struct.waitmask = &waitmask;
			}
			msg_queue->count++;
		} else
			panic("IPI message queue overflow\n");
		spin_unlock_irqrestore(&msg_queue->lock, flags);
		platform_send_ipi_cpu(cpu, IRQ_SUPPLE_1);
	}

	if (wait) {
		while (!cpumask_empty(&waitmask))
			blackfin_dcache_invalidate_range(
				(unsigned long)(&waitmask),
				(unsigned long)(&waitmask));
#ifdef __ARCH_SYNC_CORE_DCACHE
		/*
		 * Invalidate D cache in case shared data was changed by
		 * other processors to ensure cache coherence.
		 */
		resync_core_dcache();
#endif
	}
}

int smp_call_function(void (*func)(void *info), void *info, int wait)
{
	cpumask_t callmap;

	preempt_disable();
	cpumask_copy(&callmap, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &callmap);
	if (!cpumask_empty(&callmap))
		smp_send_message(callmap, BFIN_IPI_CALL_FUNC, func, info, wait);

	preempt_enable();

	return 0;
}
EXPORT_SYMBOL_GPL(smp_call_function);

int smp_call_function_single(int cpuid, void (*func) (void *info), void *info,
				int wait)
{
	unsigned int cpu = cpuid;
	cpumask_t callmap;

	if (cpu_is_offline(cpu))
		return 0;
	cpumask_clear(&callmap);
	cpumask_set_cpu(cpu, &callmap);

	smp_send_message(callmap, BFIN_IPI_CALL_FUNC, func, info, wait);

	return 0;
}
EXPORT_SYMBOL_GPL(smp_call_function_single);

void smp_send_reschedule(int cpu)
{
	cpumask_t callmap;
	/* simply trigger an ipi */

	cpumask_clear(&callmap);
	cpumask_set_cpu(cpu, &callmap);

	smp_send_message(callmap, BFIN_IPI_RESCHEDULE, NULL, NULL, 0);

	return;
}

void smp_send_msg(const struct cpumask *mask, unsigned long type)
{
	smp_send_message(*mask, type, NULL, NULL, 0);
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
		smp_send_message(callmap, BFIN_IPI_CPU_STOP, NULL, NULL, 0);

	preempt_enable();

	return;
}

int __cpuinit __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret;
	struct blackfin_cpudata *ci = &per_cpu(cpu_data, cpu);
	struct task_struct *idle = ci->idle;

	if (idle) {
		free_task(idle);
		idle = NULL;
	}

	if (!idle) {
		idle = fork_idle(cpu);
		if (IS_ERR(idle)) {
			printk(KERN_ERR "CPU%u: fork() failed\n", cpu);
			return PTR_ERR(idle);
		}
		ci->idle = idle;
	} else {
		init_idle(idle, cpu);
	}
	secondary_stack = task_stack_page(idle) + THREAD_SIZE;

	ret = platform_boot_secondary(cpu, idle);

	secondary_stack = NULL;

	return ret;
}

static void __cpuinit setup_secondary(unsigned int cpu)
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

void __cpuinit secondary_start_kernel(void)
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
	atomic_inc(&mm->mm_users);
	atomic_inc(&mm->mm_count);
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

	cpu_idle();
}

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	platform_prepare_cpus(max_cpus);
	ipi_queue_init();
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
int __cpuexit __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		return -EPERM;

	set_cpu_online(cpu, false);
	return 0;
}

static DECLARE_COMPLETION(cpu_killed);

int __cpuexit __cpu_die(unsigned int cpu)
{
	return wait_for_completion_timeout(&cpu_killed, 5000);
}

void cpu_die(void)
{
	complete(&cpu_killed);

	atomic_dec(&init_mm.mm_users);
	atomic_dec(&init_mm.mm_count);

	local_irq_disable();
	platform_cpu_die();
}
#endif
