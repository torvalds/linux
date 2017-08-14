/*
 * SMP support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * PowerPC-64 Support added by Dave Engebretsen, Peter Bergner, and
 * Mike Corrigan {engebret|bergner|mikec}@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched/mm.h>
#include <linux/sched/topology.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/topology.h>
#include <linux/profile.h>
#include <linux/processor.h>

#include <asm/ptrace.h>
#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/kvm_ppc.h>
#include <asm/dbell.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/cputhreads.h>
#include <asm/cputable.h>
#include <asm/mpic.h>
#include <asm/vdso_datapage.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#endif
#include <asm/vdso.h>
#include <asm/debug.h>
#include <asm/kexec.h>
#include <asm/asm-prototypes.h>
#include <asm/cpu_has_feature.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

#ifdef CONFIG_HOTPLUG_CPU
/* State of each CPU during hotplug phases */
static DEFINE_PER_CPU(int, cpu_state) = { 0 };
#endif

struct thread_info *secondary_ti;

DEFINE_PER_CPU(cpumask_var_t, cpu_sibling_map);
DEFINE_PER_CPU(cpumask_var_t, cpu_core_map);

EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* SMP operations for this machine */
struct smp_ops_t *smp_ops;

/* Can't be static due to PowerMac hackery */
volatile unsigned int cpu_callin_map[NR_CPUS];

int smt_enabled_at_boot = 1;

/*
 * Returns 1 if the specified cpu should be brought up during boot.
 * Used to inhibit booting threads if they've been disabled or
 * limited on the command line
 */
int smp_generic_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.
	 */
	if (system_state < SYSTEM_RUNNING && cpu_has_feature(CPU_FTR_SMT)) {
		if (!smt_enabled_at_boot && cpu_thread_in_core(nr) != 0)
			return 0;
		if (smt_enabled_at_boot
		    && cpu_thread_in_core(nr) >= smt_enabled_at_boot)
			return 0;
	}

	return 1;
}


#ifdef CONFIG_PPC64
int smp_generic_kick_cpu(int nr)
{
	if (nr < 0 || nr >= nr_cpu_ids)
		return -EINVAL;

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	if (!paca[nr].cpu_start) {
		paca[nr].cpu_start = 1;
		smp_mb();
		return 0;
	}

#ifdef CONFIG_HOTPLUG_CPU
	/*
	 * Ok it's not there, so it might be soft-unplugged, let's
	 * try to bring it back
	 */
	generic_set_cpu_up(nr);
	smp_wmb();
	smp_send_reschedule(nr);
#endif /* CONFIG_HOTPLUG_CPU */

	return 0;
}
#endif /* CONFIG_PPC64 */

static irqreturn_t call_function_action(int irq, void *data)
{
	generic_smp_call_function_interrupt();
	return IRQ_HANDLED;
}

static irqreturn_t reschedule_action(int irq, void *data)
{
	scheduler_ipi();
	return IRQ_HANDLED;
}

static irqreturn_t tick_broadcast_ipi_action(int irq, void *data)
{
	tick_broadcast_ipi_handler();
	return IRQ_HANDLED;
}

#ifdef CONFIG_NMI_IPI
static irqreturn_t nmi_ipi_action(int irq, void *data)
{
	smp_handle_nmi_ipi(get_irq_regs());
	return IRQ_HANDLED;
}
#endif

static irq_handler_t smp_ipi_action[] = {
	[PPC_MSG_CALL_FUNCTION] =  call_function_action,
	[PPC_MSG_RESCHEDULE] = reschedule_action,
	[PPC_MSG_TICK_BROADCAST] = tick_broadcast_ipi_action,
#ifdef CONFIG_NMI_IPI
	[PPC_MSG_NMI_IPI] = nmi_ipi_action,
#endif
};

/*
 * The NMI IPI is a fallback and not truly non-maskable. It is simpler
 * than going through the call function infrastructure, and strongly
 * serialized, so it is more appropriate for debugging.
 */
const char *smp_ipi_name[] = {
	[PPC_MSG_CALL_FUNCTION] =  "ipi call function",
	[PPC_MSG_RESCHEDULE] = "ipi reschedule",
	[PPC_MSG_TICK_BROADCAST] = "ipi tick-broadcast",
	[PPC_MSG_NMI_IPI] = "nmi ipi",
};

/* optional function to request ipi, for controllers with >= 4 ipis */
int smp_request_message_ipi(int virq, int msg)
{
	int err;

	if (msg < 0 || msg > PPC_MSG_NMI_IPI)
		return -EINVAL;
#ifndef CONFIG_NMI_IPI
	if (msg == PPC_MSG_NMI_IPI)
		return 1;
#endif

	err = request_irq(virq, smp_ipi_action[msg],
			  IRQF_PERCPU | IRQF_NO_THREAD | IRQF_NO_SUSPEND,
			  smp_ipi_name[msg], NULL);
	WARN(err < 0, "unable to request_irq %d for %s (rc %d)\n",
		virq, smp_ipi_name[msg], err);

	return err;
}

#ifdef CONFIG_PPC_SMP_MUXED_IPI
struct cpu_messages {
	long messages;			/* current messages */
};
static DEFINE_PER_CPU_SHARED_ALIGNED(struct cpu_messages, ipi_message);

void smp_muxed_ipi_set_message(int cpu, int msg)
{
	struct cpu_messages *info = &per_cpu(ipi_message, cpu);
	char *message = (char *)&info->messages;

	/*
	 * Order previous accesses before accesses in the IPI handler.
	 */
	smp_mb();
	message[msg] = 1;
}

void smp_muxed_ipi_message_pass(int cpu, int msg)
{
	smp_muxed_ipi_set_message(cpu, msg);

	/*
	 * cause_ipi functions are required to include a full barrier
	 * before doing whatever causes the IPI.
	 */
	smp_ops->cause_ipi(cpu);
}

#ifdef __BIG_ENDIAN__
#define IPI_MESSAGE(A) (1uL << ((BITS_PER_LONG - 8) - 8 * (A)))
#else
#define IPI_MESSAGE(A) (1uL << (8 * (A)))
#endif

irqreturn_t smp_ipi_demux(void)
{
	mb();	/* order any irq clear */

	return smp_ipi_demux_relaxed();
}

/* sync-free variant. Callers should ensure synchronization */
irqreturn_t smp_ipi_demux_relaxed(void)
{
	struct cpu_messages *info;
	unsigned long all;

	info = this_cpu_ptr(&ipi_message);
	do {
		all = xchg(&info->messages, 0);
#if defined(CONFIG_KVM_XICS) && defined(CONFIG_KVM_BOOK3S_HV_POSSIBLE)
		/*
		 * Must check for PPC_MSG_RM_HOST_ACTION messages
		 * before PPC_MSG_CALL_FUNCTION messages because when
		 * a VM is destroyed, we call kick_all_cpus_sync()
		 * to ensure that any pending PPC_MSG_RM_HOST_ACTION
		 * messages have completed before we free any VCPUs.
		 */
		if (all & IPI_MESSAGE(PPC_MSG_RM_HOST_ACTION))
			kvmppc_xics_ipi_action();
#endif
		if (all & IPI_MESSAGE(PPC_MSG_CALL_FUNCTION))
			generic_smp_call_function_interrupt();
		if (all & IPI_MESSAGE(PPC_MSG_RESCHEDULE))
			scheduler_ipi();
		if (all & IPI_MESSAGE(PPC_MSG_TICK_BROADCAST))
			tick_broadcast_ipi_handler();
#ifdef CONFIG_NMI_IPI
		if (all & IPI_MESSAGE(PPC_MSG_NMI_IPI))
			nmi_ipi_action(0, NULL);
#endif
	} while (info->messages);

	return IRQ_HANDLED;
}
#endif /* CONFIG_PPC_SMP_MUXED_IPI */

static inline void do_message_pass(int cpu, int msg)
{
	if (smp_ops->message_pass)
		smp_ops->message_pass(cpu, msg);
#ifdef CONFIG_PPC_SMP_MUXED_IPI
	else
		smp_muxed_ipi_message_pass(cpu, msg);
#endif
}

void smp_send_reschedule(int cpu)
{
	if (likely(smp_ops))
		do_message_pass(cpu, PPC_MSG_RESCHEDULE);
}
EXPORT_SYMBOL_GPL(smp_send_reschedule);

void arch_send_call_function_single_ipi(int cpu)
{
	do_message_pass(cpu, PPC_MSG_CALL_FUNCTION);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		do_message_pass(cpu, PPC_MSG_CALL_FUNCTION);
}

#ifdef CONFIG_NMI_IPI

/*
 * "NMI IPI" system.
 *
 * NMI IPIs may not be recoverable, so should not be used as ongoing part of
 * a running system. They can be used for crash, debug, halt/reboot, etc.
 *
 * NMI IPIs are globally single threaded. No more than one in progress at
 * any time.
 *
 * The IPI call waits with interrupts disabled until all targets enter the
 * NMI handler, then the call returns.
 *
 * No new NMI can be initiated until targets exit the handler.
 *
 * The IPI call may time out without all targets entering the NMI handler.
 * In that case, there is some logic to recover (and ignore subsequent
 * NMI interrupts that may eventually be raised), but the platform interrupt
 * handler may not be able to distinguish this from other exception causes,
 * which may cause a crash.
 */

static atomic_t __nmi_ipi_lock = ATOMIC_INIT(0);
static struct cpumask nmi_ipi_pending_mask;
static int nmi_ipi_busy_count = 0;
static void (*nmi_ipi_function)(struct pt_regs *) = NULL;

static void nmi_ipi_lock_start(unsigned long *flags)
{
	raw_local_irq_save(*flags);
	hard_irq_disable();
	while (atomic_cmpxchg(&__nmi_ipi_lock, 0, 1) == 1) {
		raw_local_irq_restore(*flags);
		spin_until_cond(atomic_read(&__nmi_ipi_lock) == 0);
		raw_local_irq_save(*flags);
		hard_irq_disable();
	}
}

static void nmi_ipi_lock(void)
{
	while (atomic_cmpxchg(&__nmi_ipi_lock, 0, 1) == 1)
		spin_until_cond(atomic_read(&__nmi_ipi_lock) == 0);
}

static void nmi_ipi_unlock(void)
{
	smp_mb();
	WARN_ON(atomic_read(&__nmi_ipi_lock) != 1);
	atomic_set(&__nmi_ipi_lock, 0);
}

static void nmi_ipi_unlock_end(unsigned long *flags)
{
	nmi_ipi_unlock();
	raw_local_irq_restore(*flags);
}

/*
 * Platform NMI handler calls this to ack
 */
int smp_handle_nmi_ipi(struct pt_regs *regs)
{
	void (*fn)(struct pt_regs *);
	unsigned long flags;
	int me = raw_smp_processor_id();
	int ret = 0;

	/*
	 * Unexpected NMIs are possible here because the interrupt may not
	 * be able to distinguish NMI IPIs from other types of NMIs, or
	 * because the caller may have timed out.
	 */
	nmi_ipi_lock_start(&flags);
	if (!nmi_ipi_busy_count)
		goto out;
	if (!cpumask_test_cpu(me, &nmi_ipi_pending_mask))
		goto out;

	fn = nmi_ipi_function;
	if (!fn)
		goto out;

	cpumask_clear_cpu(me, &nmi_ipi_pending_mask);
	nmi_ipi_busy_count++;
	nmi_ipi_unlock();

	ret = 1;

	fn(regs);

	nmi_ipi_lock();
	nmi_ipi_busy_count--;
out:
	nmi_ipi_unlock_end(&flags);

	return ret;
}

static void do_smp_send_nmi_ipi(int cpu)
{
	if (smp_ops->cause_nmi_ipi && smp_ops->cause_nmi_ipi(cpu))
		return;

	if (cpu >= 0) {
		do_message_pass(cpu, PPC_MSG_NMI_IPI);
	} else {
		int c;

		for_each_online_cpu(c) {
			if (c == raw_smp_processor_id())
				continue;
			do_message_pass(c, PPC_MSG_NMI_IPI);
		}
	}
}

void smp_flush_nmi_ipi(u64 delay_us)
{
	unsigned long flags;

	nmi_ipi_lock_start(&flags);
	while (nmi_ipi_busy_count) {
		nmi_ipi_unlock_end(&flags);
		udelay(1);
		if (delay_us) {
			delay_us--;
			if (!delay_us)
				return;
		}
		nmi_ipi_lock_start(&flags);
	}
	nmi_ipi_unlock_end(&flags);
}

/*
 * - cpu is the target CPU (must not be this CPU), or NMI_IPI_ALL_OTHERS.
 * - fn is the target callback function.
 * - delay_us > 0 is the delay before giving up waiting for targets to
 *   enter the handler, == 0 specifies indefinite delay.
 */
int smp_send_nmi_ipi(int cpu, void (*fn)(struct pt_regs *), u64 delay_us)
{
	unsigned long flags;
	int me = raw_smp_processor_id();
	int ret = 1;

	BUG_ON(cpu == me);
	BUG_ON(cpu < 0 && cpu != NMI_IPI_ALL_OTHERS);

	if (unlikely(!smp_ops))
		return 0;

	/* Take the nmi_ipi_busy count/lock with interrupts hard disabled */
	nmi_ipi_lock_start(&flags);
	while (nmi_ipi_busy_count) {
		nmi_ipi_unlock_end(&flags);
		spin_until_cond(nmi_ipi_busy_count == 0);
		nmi_ipi_lock_start(&flags);
	}

	nmi_ipi_function = fn;

	if (cpu < 0) {
		/* ALL_OTHERS */
		cpumask_copy(&nmi_ipi_pending_mask, cpu_online_mask);
		cpumask_clear_cpu(me, &nmi_ipi_pending_mask);
	} else {
		/* cpumask starts clear */
		cpumask_set_cpu(cpu, &nmi_ipi_pending_mask);
	}
	nmi_ipi_busy_count++;
	nmi_ipi_unlock();

	do_smp_send_nmi_ipi(cpu);

	while (!cpumask_empty(&nmi_ipi_pending_mask)) {
		udelay(1);
		if (delay_us) {
			delay_us--;
			if (!delay_us)
				break;
		}
	}

	nmi_ipi_lock();
	if (!cpumask_empty(&nmi_ipi_pending_mask)) {
		/* Could not gather all CPUs */
		ret = 0;
		cpumask_clear(&nmi_ipi_pending_mask);
	}
	nmi_ipi_busy_count--;
	nmi_ipi_unlock_end(&flags);

	return ret;
}
#endif /* CONFIG_NMI_IPI */

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
void tick_broadcast(const struct cpumask *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		do_message_pass(cpu, PPC_MSG_TICK_BROADCAST);
}
#endif

#ifdef CONFIG_DEBUGGER
void debugger_ipi_callback(struct pt_regs *regs)
{
	debugger_ipi(regs);
}

void smp_send_debugger_break(void)
{
	smp_send_nmi_ipi(NMI_IPI_ALL_OTHERS, debugger_ipi_callback, 1000000);
}
#endif

#ifdef CONFIG_KEXEC_CORE
void crash_send_ipi(void (*crash_ipi_callback)(struct pt_regs *))
{
	smp_send_nmi_ipi(NMI_IPI_ALL_OTHERS, crash_ipi_callback, 1000000);
}
#endif

static void stop_this_cpu(void *dummy)
{
	/* Remove this CPU */
	set_cpu_online(smp_processor_id(), false);

	local_irq_disable();
	while (1)
		;
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 0);
}

struct thread_info *current_set[NR_CPUS];

static void smp_store_cpu_info(int id)
{
	per_cpu(cpu_pvr, id) = mfspr(SPRN_PVR);
#ifdef CONFIG_PPC_FSL_BOOK3E
	per_cpu(next_tlbcam_idx, id)
		= (mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) - 1;
#endif
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu;

	DBG("smp_prepare_cpus\n");

	/* 
	 * setup_cpu may need to be called on the boot cpu. We havent
	 * spun any cpus up but lets be paranoid.
	 */
	BUG_ON(boot_cpuid != smp_processor_id());

	/* Fixup boot cpu */
	smp_store_cpu_info(boot_cpuid);
	cpu_callin_map[boot_cpuid] = 1;

	for_each_possible_cpu(cpu) {
		zalloc_cpumask_var_node(&per_cpu(cpu_sibling_map, cpu),
					GFP_KERNEL, cpu_to_node(cpu));
		zalloc_cpumask_var_node(&per_cpu(cpu_core_map, cpu),
					GFP_KERNEL, cpu_to_node(cpu));
		/*
		 * numa_node_id() works after this.
		 */
		if (cpu_present(cpu)) {
			set_cpu_numa_node(cpu, numa_cpu_lookup_table[cpu]);
			set_cpu_numa_mem(cpu,
				local_memory_node(numa_cpu_lookup_table[cpu]));
		}
	}

	cpumask_set_cpu(boot_cpuid, cpu_sibling_mask(boot_cpuid));
	cpumask_set_cpu(boot_cpuid, cpu_core_mask(boot_cpuid));

	if (smp_ops && smp_ops->probe)
		smp_ops->probe();
}

void smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != boot_cpuid);
#ifdef CONFIG_PPC64
	paca[boot_cpuid].__current = current;
#endif
	set_numa_node(numa_cpu_lookup_table[boot_cpuid]);
	current_set[boot_cpuid] = task_thread_info(current);
}

#ifdef CONFIG_HOTPLUG_CPU

int generic_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == boot_cpuid)
		return -EBUSY;

	set_cpu_online(cpu, false);
#ifdef CONFIG_PPC64
	vdso_data->processorCount--;
#endif
	/* Update affinity of all IRQs previously aimed at this CPU */
	irq_migrate_all_off_this_cpu();

	/*
	 * Depending on the details of the interrupt controller, it's possible
	 * that one of the interrupts we just migrated away from this CPU is
	 * actually already pending on this CPU. If we leave it in that state
	 * the interrupt will never be EOI'ed, and will never fire again. So
	 * temporarily enable interrupts here, to allow any pending interrupt to
	 * be received (and EOI'ed), before we take this CPU offline.
	 */
	local_irq_enable();
	mdelay(1);
	local_irq_disable();

	return 0;
}

void generic_cpu_die(unsigned int cpu)
{
	int i;

	for (i = 0; i < 100; i++) {
		smp_rmb();
		if (is_cpu_dead(cpu))
			return;
		msleep(100);
	}
	printk(KERN_ERR "CPU%d didn't die...\n", cpu);
}

void generic_set_cpu_dead(unsigned int cpu)
{
	per_cpu(cpu_state, cpu) = CPU_DEAD;
}

/*
 * The cpu_state should be set to CPU_UP_PREPARE in kick_cpu(), otherwise
 * the cpu_state is always CPU_DEAD after calling generic_set_cpu_dead(),
 * which makes the delay in generic_cpu_die() not happen.
 */
void generic_set_cpu_up(unsigned int cpu)
{
	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;
}

int generic_check_cpu_restart(unsigned int cpu)
{
	return per_cpu(cpu_state, cpu) == CPU_UP_PREPARE;
}

int is_cpu_dead(unsigned int cpu)
{
	return per_cpu(cpu_state, cpu) == CPU_DEAD;
}

static bool secondaries_inhibited(void)
{
	return kvm_hv_mode_active();
}

#else /* HOTPLUG_CPU */

#define secondaries_inhibited()		0

#endif

static void cpu_idle_thread_init(unsigned int cpu, struct task_struct *idle)
{
	struct thread_info *ti = task_thread_info(idle);

#ifdef CONFIG_PPC64
	paca[cpu].__current = idle;
	paca[cpu].kstack = (unsigned long)ti + THREAD_SIZE - STACK_FRAME_OVERHEAD;
#endif
	ti->cpu = cpu;
	secondary_ti = current_set[cpu] = ti;
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int rc, c;

	/*
	 * Don't allow secondary threads to come online if inhibited
	 */
	if (threads_per_core > 1 && secondaries_inhibited() &&
	    cpu_thread_in_subcore(cpu))
		return -EBUSY;

	if (smp_ops == NULL ||
	    (smp_ops->cpu_bootable && !smp_ops->cpu_bootable(cpu)))
		return -EINVAL;

	cpu_idle_thread_init(cpu, tidle);

	/*
	 * The platform might need to allocate resources prior to bringing
	 * up the CPU
	 */
	if (smp_ops->prepare_cpu) {
		rc = smp_ops->prepare_cpu(cpu);
		if (rc)
			return rc;
	}

	/* Make sure callin-map entry is 0 (can be leftover a CPU
	 * hotplug
	 */
	cpu_callin_map[cpu] = 0;

	/* The information for processor bringup must
	 * be written out to main store before we release
	 * the processor.
	 */
	smp_mb();

	/* wake up cpus */
	DBG("smp: kicking cpu %d\n", cpu);
	rc = smp_ops->kick_cpu(cpu);
	if (rc) {
		pr_err("smp: failed starting cpu %d (rc %d)\n", cpu, rc);
		return rc;
	}

	/*
	 * wait to see if the cpu made a callin (is actually up).
	 * use this value that I found through experimentation.
	 * -- Cort
	 */
	if (system_state < SYSTEM_RUNNING)
		for (c = 50000; c && !cpu_callin_map[cpu]; c--)
			udelay(100);
#ifdef CONFIG_HOTPLUG_CPU
	else
		/*
		 * CPUs can take much longer to come up in the
		 * hotplug case.  Wait five seconds.
		 */
		for (c = 5000; c && !cpu_callin_map[cpu]; c--)
			msleep(1);
#endif

	if (!cpu_callin_map[cpu]) {
		printk(KERN_ERR "Processor %u is stuck.\n", cpu);
		return -ENOENT;
	}

	DBG("Processor %u found.\n", cpu);

	if (smp_ops->give_timebase)
		smp_ops->give_timebase();

	/* Wait until cpu puts itself in the online & active maps */
	spin_until_cond(cpu_online(cpu));

	return 0;
}

/* Return the value of the reg property corresponding to the given
 * logical cpu.
 */
int cpu_to_core_id(int cpu)
{
	struct device_node *np;
	const __be32 *reg;
	int id = -1;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		goto out;

	reg = of_get_property(np, "reg", NULL);
	if (!reg)
		goto out;

	id = be32_to_cpup(reg);
out:
	of_node_put(np);
	return id;
}
EXPORT_SYMBOL_GPL(cpu_to_core_id);

/* Helper routines for cpu to core mapping */
int cpu_core_index_of_thread(int cpu)
{
	return cpu >> threads_shift;
}
EXPORT_SYMBOL_GPL(cpu_core_index_of_thread);

int cpu_first_thread_of_core(int core)
{
	return core << threads_shift;
}
EXPORT_SYMBOL_GPL(cpu_first_thread_of_core);

static void traverse_siblings_chip_id(int cpu, bool add, int chipid)
{
	const struct cpumask *mask;
	struct device_node *np;
	int i, plen;
	const __be32 *prop;

	mask = add ? cpu_online_mask : cpu_present_mask;
	for_each_cpu(i, mask) {
		np = of_get_cpu_node(i, NULL);
		if (!np)
			continue;
		prop = of_get_property(np, "ibm,chip-id", &plen);
		if (prop && plen == sizeof(int) &&
		    of_read_number(prop, 1) == chipid) {
			if (add) {
				cpumask_set_cpu(cpu, cpu_core_mask(i));
				cpumask_set_cpu(i, cpu_core_mask(cpu));
			} else {
				cpumask_clear_cpu(cpu, cpu_core_mask(i));
				cpumask_clear_cpu(i, cpu_core_mask(cpu));
			}
		}
		of_node_put(np);
	}
}

/* Must be called when no change can occur to cpu_present_mask,
 * i.e. during cpu online or offline.
 */
static struct device_node *cpu_to_l2cache(int cpu)
{
	struct device_node *np;
	struct device_node *cache;

	if (!cpu_present(cpu))
		return NULL;

	np = of_get_cpu_node(cpu, NULL);
	if (np == NULL)
		return NULL;

	cache = of_find_next_cache_node(np);

	of_node_put(np);

	return cache;
}

static void traverse_core_siblings(int cpu, bool add)
{
	struct device_node *l2_cache, *np;
	const struct cpumask *mask;
	int i, chip, plen;
	const __be32 *prop;

	/* First see if we have ibm,chip-id properties in cpu nodes */
	np = of_get_cpu_node(cpu, NULL);
	if (np) {
		chip = -1;
		prop = of_get_property(np, "ibm,chip-id", &plen);
		if (prop && plen == sizeof(int))
			chip = of_read_number(prop, 1);
		of_node_put(np);
		if (chip >= 0) {
			traverse_siblings_chip_id(cpu, add, chip);
			return;
		}
	}

	l2_cache = cpu_to_l2cache(cpu);
	mask = add ? cpu_online_mask : cpu_present_mask;
	for_each_cpu(i, mask) {
		np = cpu_to_l2cache(i);
		if (!np)
			continue;
		if (np == l2_cache) {
			if (add) {
				cpumask_set_cpu(cpu, cpu_core_mask(i));
				cpumask_set_cpu(i, cpu_core_mask(cpu));
			} else {
				cpumask_clear_cpu(cpu, cpu_core_mask(i));
				cpumask_clear_cpu(i, cpu_core_mask(cpu));
			}
		}
		of_node_put(np);
	}
	of_node_put(l2_cache);
}

/* Activate a secondary processor. */
void start_secondary(void *unused)
{
	unsigned int cpu = smp_processor_id();
	int i, base;

	mmgrab(&init_mm);
	current->active_mm = &init_mm;

	smp_store_cpu_info(cpu);
	set_dec(tb_ticks_per_jiffy);
	preempt_disable();
	cpu_callin_map[cpu] = 1;

	if (smp_ops->setup_cpu)
		smp_ops->setup_cpu(cpu);
	if (smp_ops->take_timebase)
		smp_ops->take_timebase();

	secondary_cpu_time_init();

#ifdef CONFIG_PPC64
	if (system_state == SYSTEM_RUNNING)
		vdso_data->processorCount++;

	vdso_getcpu_init();
#endif
	/* Update sibling maps */
	base = cpu_first_thread_sibling(cpu);
	for (i = 0; i < threads_per_core; i++) {
		if (cpu_is_offline(base + i) && (cpu != base + i))
			continue;
		cpumask_set_cpu(cpu, cpu_sibling_mask(base + i));
		cpumask_set_cpu(base + i, cpu_sibling_mask(cpu));

		/* cpu_core_map should be a superset of
		 * cpu_sibling_map even if we don't have cache
		 * information, so update the former here, too.
		 */
		cpumask_set_cpu(cpu, cpu_core_mask(base + i));
		cpumask_set_cpu(base + i, cpu_core_mask(cpu));
	}
	traverse_core_siblings(cpu, true);

	set_numa_node(numa_cpu_lookup_table[cpu]);
	set_numa_mem(local_memory_node(numa_cpu_lookup_table[cpu]));

	smp_wmb();
	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);

	local_irq_enable();

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);

	BUG();
}

int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

#ifdef CONFIG_SCHED_SMT
/* cpumask of CPUs with asymetric SMT dependancy */
static int powerpc_smt_flags(void)
{
	int flags = SD_SHARE_CPUCAPACITY | SD_SHARE_PKG_RESOURCES;

	if (cpu_has_feature(CPU_FTR_ASYM_SMT)) {
		printk_once(KERN_INFO "Enabling Asymmetric SMT scheduling\n");
		flags |= SD_ASYM_PACKING;
	}
	return flags;
}
#endif

static struct sched_domain_topology_level powerpc_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, powerpc_smt_flags, SD_INIT_NAME(SMT) },
#endif
	{ cpu_cpu_mask, SD_INIT_NAME(DIE) },
	{ NULL, },
};

void __init smp_cpus_done(unsigned int max_cpus)
{
	/*
	 * We are running pinned to the boot CPU, see rest_init().
	 */
	if (smp_ops && smp_ops->setup_cpu)
		smp_ops->setup_cpu(boot_cpuid);

	if (smp_ops && smp_ops->bringup_done)
		smp_ops->bringup_done();

	dump_numa_cpu_topology();

	set_sched_topology(powerpc_topology);
}

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void)
{
	int cpu = smp_processor_id();
	int base, i;
	int err;

	if (!smp_ops->cpu_disable)
		return -ENOSYS;

	err = smp_ops->cpu_disable();
	if (err)
		return err;

	/* Update sibling maps */
	base = cpu_first_thread_sibling(cpu);
	for (i = 0; i < threads_per_core && base + i < nr_cpu_ids; i++) {
		cpumask_clear_cpu(cpu, cpu_sibling_mask(base + i));
		cpumask_clear_cpu(base + i, cpu_sibling_mask(cpu));
		cpumask_clear_cpu(cpu, cpu_core_mask(base + i));
		cpumask_clear_cpu(base + i, cpu_core_mask(cpu));
	}
	traverse_core_siblings(cpu, false);

	return 0;
}

void __cpu_die(unsigned int cpu)
{
	if (smp_ops->cpu_die)
		smp_ops->cpu_die(cpu);
}

void cpu_die(void)
{
	if (ppc_md.cpu_die)
		ppc_md.cpu_die();

	/* If we return, we re-enter start_secondary */
	start_secondary_resume();
}

#endif
