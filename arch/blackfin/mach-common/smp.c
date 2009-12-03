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
#include <linux/profile.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <asm/atomic.h>
#include <asm/cacheflush.h>
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

void __cpuinitdata *init_retx_coreb, *init_saved_retx_coreb,
	*init_saved_seqstat_coreb, *init_saved_icplb_fault_addr_coreb,
	*init_saved_dcplb_fault_addr_coreb;

cpumask_t cpu_possible_map;
EXPORT_SYMBOL(cpu_possible_map);

cpumask_t cpu_online_map;
EXPORT_SYMBOL(cpu_online_map);

#define BFIN_IPI_RESCHEDULE   0
#define BFIN_IPI_CALL_FUNC    1
#define BFIN_IPI_CPU_STOP     2

struct blackfin_flush_data {
	unsigned long start;
	unsigned long end;
};

void *secondary_stack;


struct smp_call_struct {
	void (*func)(void *info);
	void *info;
	int wait;
	cpumask_t pending;
	cpumask_t waitmask;
};

static struct blackfin_flush_data smp_flush_data;

static DEFINE_SPINLOCK(stop_lock);

struct ipi_message {
	struct list_head list;
	unsigned long type;
	struct smp_call_struct call_struct;
};

struct ipi_message_queue {
	struct list_head head;
	spinlock_t lock;
	unsigned long count;
};

static DEFINE_PER_CPU(struct ipi_message_queue, ipi_msg_queue);

static void ipi_cpu_stop(unsigned int cpu)
{
	spin_lock(&stop_lock);
	printk(KERN_CRIT "CPU%u: stopping\n", cpu);
	dump_stack();
	spin_unlock(&stop_lock);

	cpu_clear(cpu, cpu_online_map);

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
	cpu_clear(cpu, msg->call_struct.pending);
	func(info);
	if (wait)
		cpu_clear(cpu, msg->call_struct.waitmask);
	else
		kfree(msg);
}

static irqreturn_t ipi_handler(int irq, void *dev_instance)
{
	struct ipi_message *msg;
	struct ipi_message_queue *msg_queue;
	unsigned int cpu = smp_processor_id();

	platform_clear_ipi(cpu);

	msg_queue = &__get_cpu_var(ipi_msg_queue);
	msg_queue->count++;

	spin_lock(&msg_queue->lock);
	while (!list_empty(&msg_queue->head)) {
		msg = list_entry(msg_queue->head.next, typeof(*msg), list);
		list_del(&msg->list);
		switch (msg->type) {
		case BFIN_IPI_RESCHEDULE:
			/* That's the easiest one; leave it to
			 * return_from_int. */
			kfree(msg);
			break;
		case BFIN_IPI_CALL_FUNC:
			spin_unlock(&msg_queue->lock);
			ipi_call_function(cpu, msg);
			spin_lock(&msg_queue->lock);
			break;
		case BFIN_IPI_CPU_STOP:
			spin_unlock(&msg_queue->lock);
			ipi_cpu_stop(cpu);
			spin_lock(&msg_queue->lock);
			kfree(msg);
			break;
		default:
			printk(KERN_CRIT "CPU%u: Unknown IPI message \
			0x%lx\n", cpu, msg->type);
			kfree(msg);
			break;
		}
	}
	spin_unlock(&msg_queue->lock);
	return IRQ_HANDLED;
}

static void ipi_queue_init(void)
{
	unsigned int cpu;
	struct ipi_message_queue *msg_queue;
	for_each_possible_cpu(cpu) {
		msg_queue = &per_cpu(ipi_msg_queue, cpu);
		INIT_LIST_HEAD(&msg_queue->head);
		spin_lock_init(&msg_queue->lock);
		msg_queue->count = 0;
	}
}

int smp_call_function(void (*func)(void *info), void *info, int wait)
{
	unsigned int cpu;
	cpumask_t callmap;
	unsigned long flags;
	struct ipi_message_queue *msg_queue;
	struct ipi_message *msg;

	callmap = cpu_online_map;
	cpu_clear(smp_processor_id(), callmap);
	if (cpus_empty(callmap))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;
	INIT_LIST_HEAD(&msg->list);
	msg->call_struct.func = func;
	msg->call_struct.info = info;
	msg->call_struct.wait = wait;
	msg->call_struct.pending = callmap;
	msg->call_struct.waitmask = callmap;
	msg->type = BFIN_IPI_CALL_FUNC;

	for_each_cpu_mask(cpu, callmap) {
		msg_queue = &per_cpu(ipi_msg_queue, cpu);
		spin_lock_irqsave(&msg_queue->lock, flags);
		list_add_tail(&msg->list, &msg_queue->head);
		spin_unlock_irqrestore(&msg_queue->lock, flags);
		platform_send_ipi_cpu(cpu);
	}
	if (wait) {
		while (!cpus_empty(msg->call_struct.waitmask))
			blackfin_dcache_invalidate_range(
				(unsigned long)(&msg->call_struct.waitmask),
				(unsigned long)(&msg->call_struct.waitmask));
		kfree(msg);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(smp_call_function);

int smp_call_function_single(int cpuid, void (*func) (void *info), void *info,
				int wait)
{
	unsigned int cpu = cpuid;
	cpumask_t callmap;
	unsigned long flags;
	struct ipi_message_queue *msg_queue;
	struct ipi_message *msg;

	if (cpu_is_offline(cpu))
		return 0;
	cpus_clear(callmap);
	cpu_set(cpu, callmap);

	msg = kmalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;
	INIT_LIST_HEAD(&msg->list);
	msg->call_struct.func = func;
	msg->call_struct.info = info;
	msg->call_struct.wait = wait;
	msg->call_struct.pending = callmap;
	msg->call_struct.waitmask = callmap;
	msg->type = BFIN_IPI_CALL_FUNC;

	msg_queue = &per_cpu(ipi_msg_queue, cpu);
	spin_lock_irqsave(&msg_queue->lock, flags);
	list_add_tail(&msg->list, &msg_queue->head);
	spin_unlock_irqrestore(&msg_queue->lock, flags);
	platform_send_ipi_cpu(cpu);

	if (wait) {
		while (!cpus_empty(msg->call_struct.waitmask))
			blackfin_dcache_invalidate_range(
				(unsigned long)(&msg->call_struct.waitmask),
				(unsigned long)(&msg->call_struct.waitmask));
		kfree(msg);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(smp_call_function_single);

void smp_send_reschedule(int cpu)
{
	unsigned long flags;
	struct ipi_message_queue *msg_queue;
	struct ipi_message *msg;

	if (cpu_is_offline(cpu))
		return;

	msg = kzalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return;
	INIT_LIST_HEAD(&msg->list);
	msg->type = BFIN_IPI_RESCHEDULE;

	msg_queue = &per_cpu(ipi_msg_queue, cpu);
	spin_lock_irqsave(&msg_queue->lock, flags);
	list_add_tail(&msg->list, &msg_queue->head);
	spin_unlock_irqrestore(&msg_queue->lock, flags);
	platform_send_ipi_cpu(cpu);

	return;
}

void smp_send_stop(void)
{
	unsigned int cpu;
	cpumask_t callmap;
	unsigned long flags;
	struct ipi_message_queue *msg_queue;
	struct ipi_message *msg;

	callmap = cpu_online_map;
	cpu_clear(smp_processor_id(), callmap);
	if (cpus_empty(callmap))
		return;

	msg = kzalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return;
	INIT_LIST_HEAD(&msg->list);
	msg->type = BFIN_IPI_CPU_STOP;

	for_each_cpu_mask(cpu, callmap) {
		msg_queue = &per_cpu(ipi_msg_queue, cpu);
		spin_lock_irqsave(&msg_queue->lock, flags);
		list_add_tail(&msg->list, &msg_queue->head);
		spin_unlock_irqrestore(&msg_queue->lock, flags);
		platform_send_ipi_cpu(cpu);
	}
	return;
}

int __cpuinit __cpu_up(unsigned int cpu)
{
	struct task_struct *idle;
	int ret;

	idle = fork_idle(cpu);
	if (IS_ERR(idle)) {
		printk(KERN_ERR "CPU%u: fork() failed\n", cpu);
		return PTR_ERR(idle);
	}

	secondary_stack = task_stack_page(idle) + THREAD_SIZE;
	smp_wmb();

	ret = platform_boot_secondary(cpu, idle);

	if (ret) {
		cpu_clear(cpu, cpu_present_map);
		printk(KERN_CRIT "CPU%u: processor failed to boot (%d)\n", cpu, ret);
		free_task(idle);
	} else
		cpu_set(cpu, cpu_online_map);

	secondary_stack = NULL;

	return ret;
}

static void __cpuinit setup_secondary(unsigned int cpu)
{
#if !defined(CONFIG_TICKSOURCE_GPTMR0)
	struct irq_desc *timer_desc;
#endif
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

#if defined(CONFIG_TICKSOURCE_GPTMR0)
	/* Power down the core timer, just to play safe. */
	bfin_write_TCNTL(0);

	/* system timer0 has been setup by CoreA. */
#else
	timer_desc = irq_desc + IRQ_CORETMR;
	setup_core_timer();
	timer_desc->chip->enable(IRQ_CORETMR);
#endif
}

void __cpuinit secondary_start_kernel(void)
{
	unsigned int cpu = smp_processor_id();
	struct mm_struct *mm = &init_mm;

	if (_bfin_swrst & SWRST_DBL_FAULT_B) {
		printk(KERN_EMERG "CoreB Recovering from DOUBLE FAULT event\n");
#ifdef CONFIG_DEBUG_DOUBLEFAULT
		printk(KERN_EMERG " While handling exception (EXCAUSE = 0x%x) at %pF\n",
			(int)init_saved_seqstat_coreb & SEQSTAT_EXCAUSE, init_saved_retx_coreb);
		printk(KERN_NOTICE "   DCPLB_FAULT_ADDR: %pF\n", init_saved_dcplb_fault_addr_coreb);
		printk(KERN_NOTICE "   ICPLB_FAULT_ADDR: %pF\n", init_saved_icplb_fault_addr_coreb);
#endif
		printk(KERN_NOTICE " The instruction at %pF caused a double exception\n",
			init_retx_coreb);
	}

	/*
	 * We want the D-cache to be enabled early, in case the atomic
	 * support code emulates cache coherence (see
	 * __ARCH_SYNC_CORE_DCACHE).
	 */
	init_exception_vectors();

	bfin_setup_caches(cpu);

	local_irq_disable();

	/* Attach the new idle task to the global mm. */
	atomic_inc(&mm->mm_users);
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;
	BUG_ON(current->mm);	/* Can't be, but better be safe than sorry. */

	preempt_disable();

	setup_secondary(cpu);

	local_irq_enable();

	platform_secondary_init(cpu);

	cpu_idle();
}

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	platform_prepare_cpus(max_cpus);
	ipi_queue_init();
	platform_request_ipi(&ipi_handler);
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

	if (smp_call_function(&ipi_flush_icache, &smp_flush_data, 0))
		printk(KERN_WARNING "SMP: failed to run I-cache flush request on other CPUs\n");
}
EXPORT_SYMBOL_GPL(smp_icache_flush_range_others);

#ifdef __ARCH_SYNC_CORE_ICACHE
void resync_core_icache(void)
{
	unsigned int cpu = get_cpu();
	blackfin_invalidate_entire_icache();
	++per_cpu(cpu_data, cpu).icache_invld_count;
	put_cpu();
}
EXPORT_SYMBOL(resync_core_icache);
#endif

#ifdef __ARCH_SYNC_CORE_DCACHE
unsigned long barrier_mask __attribute__ ((__section__(".l2.bss")));

void resync_core_dcache(void)
{
	unsigned int cpu = get_cpu();
	blackfin_invalidate_entire_dcache();
	++per_cpu(cpu_data, cpu).dcache_invld_count;
	put_cpu();
}
EXPORT_SYMBOL(resync_core_dcache);
#endif
