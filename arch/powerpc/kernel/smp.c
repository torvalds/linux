// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
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
#include <linux/random.h>
#include <linux/stackprotector.h>
#include <linux/pgtable.h>

#include <asm/ptrace.h>
#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/kvm_ppc.h>
#include <asm/dbell.h>
#include <asm/page.h>
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
#include <asm/ftrace.h>

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

struct task_struct *secondary_current;
bool has_big_cores;

DEFINE_PER_CPU(cpumask_var_t, cpu_sibling_map);
DEFINE_PER_CPU(cpumask_var_t, cpu_smallcore_map);
DEFINE_PER_CPU(cpumask_var_t, cpu_l2_cache_map);
DEFINE_PER_CPU(cpumask_var_t, cpu_core_map);

EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_l2_cache_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);
EXPORT_SYMBOL_GPL(has_big_cores);

#define MAX_THREAD_LIST_SIZE	8
#define THREAD_GROUP_SHARE_L1   1
struct thread_groups {
	unsigned int property;
	unsigned int nr_groups;
	unsigned int threads_per_group;
	unsigned int thread_list[MAX_THREAD_LIST_SIZE];
};

/*
 * On big-cores system, cpu_l1_cache_map for each CPU corresponds to
 * the set its siblings that share the L1-cache.
 */
DEFINE_PER_CPU(cpumask_var_t, cpu_l1_cache_map);

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
	if (!paca_ptrs[nr]->cpu_start) {
		paca_ptrs[nr]->cpu_start = 1;
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

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
static irqreturn_t tick_broadcast_ipi_action(int irq, void *data)
{
	timer_broadcast_interrupt();
	return IRQ_HANDLED;
}
#endif

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
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	[PPC_MSG_TICK_BROADCAST] = tick_broadcast_ipi_action,
#endif
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
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	[PPC_MSG_TICK_BROADCAST] = "ipi tick-broadcast",
#endif
#ifdef CONFIG_NMI_IPI
	[PPC_MSG_NMI_IPI] = "nmi ipi",
#endif
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
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
		if (all & IPI_MESSAGE(PPC_MSG_TICK_BROADCAST))
			timer_broadcast_interrupt();
#endif
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
 * The IPI call waits with interrupts disabled until all targets enter the
 * NMI handler, then returns. Subsequent IPIs can be issued before targets
 * have returned from their handlers, so there is no guarantee about
 * concurrency or re-entrancy.
 *
 * A new NMI can be issued before all targets exit the handler.
 *
 * The IPI call may time out without all targets entering the NMI handler.
 * In that case, there is some logic to recover (and ignore subsequent
 * NMI interrupts that may eventually be raised), but the platform interrupt
 * handler may not be able to distinguish this from other exception causes,
 * which may cause a crash.
 */

static atomic_t __nmi_ipi_lock = ATOMIC_INIT(0);
static struct cpumask nmi_ipi_pending_mask;
static bool nmi_ipi_busy = false;
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
	void (*fn)(struct pt_regs *) = NULL;
	unsigned long flags;
	int me = raw_smp_processor_id();
	int ret = 0;

	/*
	 * Unexpected NMIs are possible here because the interrupt may not
	 * be able to distinguish NMI IPIs from other types of NMIs, or
	 * because the caller may have timed out.
	 */
	nmi_ipi_lock_start(&flags);
	if (cpumask_test_cpu(me, &nmi_ipi_pending_mask)) {
		cpumask_clear_cpu(me, &nmi_ipi_pending_mask);
		fn = READ_ONCE(nmi_ipi_function);
		WARN_ON_ONCE(!fn);
		ret = 1;
	}
	nmi_ipi_unlock_end(&flags);

	if (fn)
		fn(regs);

	return ret;
}

static void do_smp_send_nmi_ipi(int cpu, bool safe)
{
	if (!safe && smp_ops->cause_nmi_ipi && smp_ops->cause_nmi_ipi(cpu))
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

/*
 * - cpu is the target CPU (must not be this CPU), or NMI_IPI_ALL_OTHERS.
 * - fn is the target callback function.
 * - delay_us > 0 is the delay before giving up waiting for targets to
 *   begin executing the handler, == 0 specifies indefinite delay.
 */
static int __smp_send_nmi_ipi(int cpu, void (*fn)(struct pt_regs *),
				u64 delay_us, bool safe)
{
	unsigned long flags;
	int me = raw_smp_processor_id();
	int ret = 1;

	BUG_ON(cpu == me);
	BUG_ON(cpu < 0 && cpu != NMI_IPI_ALL_OTHERS);

	if (unlikely(!smp_ops))
		return 0;

	nmi_ipi_lock_start(&flags);
	while (nmi_ipi_busy) {
		nmi_ipi_unlock_end(&flags);
		spin_until_cond(!nmi_ipi_busy);
		nmi_ipi_lock_start(&flags);
	}
	nmi_ipi_busy = true;
	nmi_ipi_function = fn;

	WARN_ON_ONCE(!cpumask_empty(&nmi_ipi_pending_mask));

	if (cpu < 0) {
		/* ALL_OTHERS */
		cpumask_copy(&nmi_ipi_pending_mask, cpu_online_mask);
		cpumask_clear_cpu(me, &nmi_ipi_pending_mask);
	} else {
		cpumask_set_cpu(cpu, &nmi_ipi_pending_mask);
	}

	nmi_ipi_unlock();

	/* Interrupts remain hard disabled */

	do_smp_send_nmi_ipi(cpu, safe);

	nmi_ipi_lock();
	/* nmi_ipi_busy is set here, so unlock/lock is okay */
	while (!cpumask_empty(&nmi_ipi_pending_mask)) {
		nmi_ipi_unlock();
		udelay(1);
		nmi_ipi_lock();
		if (delay_us) {
			delay_us--;
			if (!delay_us)
				break;
		}
	}

	if (!cpumask_empty(&nmi_ipi_pending_mask)) {
		/* Timeout waiting for CPUs to call smp_handle_nmi_ipi */
		ret = 0;
		cpumask_clear(&nmi_ipi_pending_mask);
	}

	nmi_ipi_function = NULL;
	nmi_ipi_busy = false;

	nmi_ipi_unlock_end(&flags);

	return ret;
}

int smp_send_nmi_ipi(int cpu, void (*fn)(struct pt_regs *), u64 delay_us)
{
	return __smp_send_nmi_ipi(cpu, fn, delay_us, false);
}

int smp_send_safe_nmi_ipi(int cpu, void (*fn)(struct pt_regs *), u64 delay_us)
{
	return __smp_send_nmi_ipi(cpu, fn, delay_us, true);
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
	int cpu;

	smp_send_nmi_ipi(NMI_IPI_ALL_OTHERS, crash_ipi_callback, 1000000);
	if (kdump_in_progress() && crash_wake_offline) {
		for_each_present_cpu(cpu) {
			if (cpu_online(cpu))
				continue;
			/*
			 * crash_ipi_callback will wait for
			 * all cpus, including offline CPUs.
			 * We don't care about nmi_ipi_function.
			 * Offline cpus will jump straight into
			 * crash_ipi_callback, we can skip the
			 * entire NMI dance and waiting for
			 * cpus to clear pending mask, etc.
			 */
			do_smp_send_nmi_ipi(cpu, false);
		}
	}
}
#endif

#ifdef CONFIG_NMI_IPI
static void nmi_stop_this_cpu(struct pt_regs *regs)
{
	/*
	 * IRQs are already hard disabled by the smp_handle_nmi_ipi.
	 */
	spin_begin();
	while (1)
		spin_cpu_relax();
}

void smp_send_stop(void)
{
	smp_send_nmi_ipi(NMI_IPI_ALL_OTHERS, nmi_stop_this_cpu, 1000000);
}

#else /* CONFIG_NMI_IPI */

static void stop_this_cpu(void *dummy)
{
	hard_irq_disable();
	spin_begin();
	while (1)
		spin_cpu_relax();
}

void smp_send_stop(void)
{
	static bool stopped = false;

	/*
	 * Prevent waiting on csd lock from a previous smp_send_stop.
	 * This is racy, but in general callers try to do the right
	 * thing and only fire off one smp_send_stop (e.g., see
	 * kernel/panic.c)
	 */
	if (stopped)
		return;

	stopped = true;

	smp_call_function(stop_this_cpu, NULL, 0);
}
#endif /* CONFIG_NMI_IPI */

struct task_struct *current_set[NR_CPUS];

static void smp_store_cpu_info(int id)
{
	per_cpu(cpu_pvr, id) = mfspr(SPRN_PVR);
#ifdef CONFIG_PPC_FSL_BOOK3E
	per_cpu(next_tlbcam_idx, id)
		= (mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) - 1;
#endif
}

/*
 * Relationships between CPUs are maintained in a set of per-cpu cpumasks so
 * rather than just passing around the cpumask we pass around a function that
 * returns the that cpumask for the given CPU.
 */
static void set_cpus_related(int i, int j, struct cpumask *(*get_cpumask)(int))
{
	cpumask_set_cpu(i, get_cpumask(j));
	cpumask_set_cpu(j, get_cpumask(i));
}

#ifdef CONFIG_HOTPLUG_CPU
static void set_cpus_unrelated(int i, int j,
		struct cpumask *(*get_cpumask)(int))
{
	cpumask_clear_cpu(i, get_cpumask(j));
	cpumask_clear_cpu(j, get_cpumask(i));
}
#endif

/*
 * parse_thread_groups: Parses the "ibm,thread-groups" device tree
 *                      property for the CPU device node @dn and stores
 *                      the parsed output in the thread_groups
 *                      structure @tg if the ibm,thread-groups[0]
 *                      matches @property.
 *
 * @dn: The device node of the CPU device.
 * @tg: Pointer to a thread group structure into which the parsed
 *      output of "ibm,thread-groups" is stored.
 * @property: The property of the thread-group that the caller is
 *            interested in.
 *
 * ibm,thread-groups[0..N-1] array defines which group of threads in
 * the CPU-device node can be grouped together based on the property.
 *
 * ibm,thread-groups[0] tells us the property based on which the
 * threads are being grouped together. If this value is 1, it implies
 * that the threads in the same group share L1, translation cache.
 *
 * ibm,thread-groups[1] tells us how many such thread groups exist.
 *
 * ibm,thread-groups[2] tells us the number of threads in each such
 * group.
 *
 * ibm,thread-groups[3..N-1] is the list of threads identified by
 * "ibm,ppc-interrupt-server#s" arranged as per their membership in
 * the grouping.
 *
 * Example: If ibm,thread-groups = [1,2,4,5,6,7,8,9,10,11,12] it
 * implies that there are 2 groups of 4 threads each, where each group
 * of threads share L1, translation cache.
 *
 * The "ibm,ppc-interrupt-server#s" of the first group is {5,6,7,8}
 * and the "ibm,ppc-interrupt-server#s" of the second group is {9, 10,
 * 11, 12} structure
 *
 * Returns 0 on success, -EINVAL if the property does not exist,
 * -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data isn't large enough.
 */
static int parse_thread_groups(struct device_node *dn,
			       struct thread_groups *tg,
			       unsigned int property)
{
	int i;
	u32 thread_group_array[3 + MAX_THREAD_LIST_SIZE];
	u32 *thread_list;
	size_t total_threads;
	int ret;

	ret = of_property_read_u32_array(dn, "ibm,thread-groups",
					 thread_group_array, 3);
	if (ret)
		return ret;

	tg->property = thread_group_array[0];
	tg->nr_groups = thread_group_array[1];
	tg->threads_per_group = thread_group_array[2];
	if (tg->property != property ||
	    tg->nr_groups < 1 ||
	    tg->threads_per_group < 1)
		return -ENODATA;

	total_threads = tg->nr_groups * tg->threads_per_group;

	ret = of_property_read_u32_array(dn, "ibm,thread-groups",
					 thread_group_array,
					 3 + total_threads);
	if (ret)
		return ret;

	thread_list = &thread_group_array[3];

	for (i = 0 ; i < total_threads; i++)
		tg->thread_list[i] = thread_list[i];

	return 0;
}

/*
 * get_cpu_thread_group_start : Searches the thread group in tg->thread_list
 *                              that @cpu belongs to.
 *
 * @cpu : The logical CPU whose thread group is being searched.
 * @tg : The thread-group structure of the CPU node which @cpu belongs
 *       to.
 *
 * Returns the index to tg->thread_list that points to the the start
 * of the thread_group that @cpu belongs to.
 *
 * Returns -1 if cpu doesn't belong to any of the groups pointed to by
 * tg->thread_list.
 */
static int get_cpu_thread_group_start(int cpu, struct thread_groups *tg)
{
	int hw_cpu_id = get_hard_smp_processor_id(cpu);
	int i, j;

	for (i = 0; i < tg->nr_groups; i++) {
		int group_start = i * tg->threads_per_group;

		for (j = 0; j < tg->threads_per_group; j++) {
			int idx = group_start + j;

			if (tg->thread_list[idx] == hw_cpu_id)
				return group_start;
		}
	}

	return -1;
}

static int init_cpu_l1_cache_map(int cpu)

{
	struct device_node *dn = of_get_cpu_node(cpu, NULL);
	struct thread_groups tg = {.property = 0,
				   .nr_groups = 0,
				   .threads_per_group = 0};
	int first_thread = cpu_first_thread_sibling(cpu);
	int i, cpu_group_start = -1, err = 0;

	if (!dn)
		return -ENODATA;

	err = parse_thread_groups(dn, &tg, THREAD_GROUP_SHARE_L1);
	if (err)
		goto out;

	zalloc_cpumask_var_node(&per_cpu(cpu_l1_cache_map, cpu),
				GFP_KERNEL,
				cpu_to_node(cpu));

	cpu_group_start = get_cpu_thread_group_start(cpu, &tg);

	if (unlikely(cpu_group_start == -1)) {
		WARN_ON_ONCE(1);
		err = -ENODATA;
		goto out;
	}

	for (i = first_thread; i < first_thread + threads_per_core; i++) {
		int i_group_start = get_cpu_thread_group_start(i, &tg);

		if (unlikely(i_group_start == -1)) {
			WARN_ON_ONCE(1);
			err = -ENODATA;
			goto out;
		}

		if (i_group_start == cpu_group_start)
			cpumask_set_cpu(i, per_cpu(cpu_l1_cache_map, cpu));
	}

out:
	of_node_put(dn);
	return err;
}

static int init_big_cores(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		int err = init_cpu_l1_cache_map(cpu);

		if (err)
			return err;

		zalloc_cpumask_var_node(&per_cpu(cpu_smallcore_map, cpu),
					GFP_KERNEL,
					cpu_to_node(cpu));
	}

	has_big_cores = true;
	return 0;
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
		zalloc_cpumask_var_node(&per_cpu(cpu_l2_cache_map, cpu),
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

	/* Init the cpumasks so the boot CPU is related to itself */
	cpumask_set_cpu(boot_cpuid, cpu_sibling_mask(boot_cpuid));
	cpumask_set_cpu(boot_cpuid, cpu_l2_cache_mask(boot_cpuid));
	cpumask_set_cpu(boot_cpuid, cpu_core_mask(boot_cpuid));

	init_big_cores();
	if (has_big_cores) {
		cpumask_set_cpu(boot_cpuid,
				cpu_smallcore_mask(boot_cpuid));
	}

	if (smp_ops && smp_ops->probe)
		smp_ops->probe();
}

void smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != boot_cpuid);
#ifdef CONFIG_PPC64
	paca_ptrs[boot_cpuid]->__current = current;
#endif
	set_numa_node(numa_cpu_lookup_table[boot_cpuid]);
	current_set[boot_cpuid] = current;
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
#ifdef CONFIG_PPC64
	paca_ptrs[cpu]->__current = idle;
	paca_ptrs[cpu]->kstack = (unsigned long)task_stack_page(idle) +
				 THREAD_SIZE - STACK_FRAME_OVERHEAD;
#endif
	idle->cpu = cpu;
	secondary_current = current_set[cpu] = idle;
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

static bool update_mask_by_l2(int cpu, struct cpumask *(*mask_fn)(int))
{
	struct device_node *l2_cache, *np;
	int i;

	l2_cache = cpu_to_l2cache(cpu);
	if (!l2_cache)
		return false;

	for_each_cpu(i, cpu_online_mask) {
		/*
		 * when updating the marks the current CPU has not been marked
		 * online, but we need to update the cache masks
		 */
		np = cpu_to_l2cache(i);
		if (!np)
			continue;

		if (np == l2_cache)
			set_cpus_related(cpu, i, mask_fn);

		of_node_put(np);
	}
	of_node_put(l2_cache);

	return true;
}

#ifdef CONFIG_HOTPLUG_CPU
static void remove_cpu_from_masks(int cpu)
{
	int i;

	/* NB: cpu_core_mask is a superset of the others */
	for_each_cpu(i, cpu_core_mask(cpu)) {
		set_cpus_unrelated(cpu, i, cpu_core_mask);
		set_cpus_unrelated(cpu, i, cpu_l2_cache_mask);
		set_cpus_unrelated(cpu, i, cpu_sibling_mask);
		if (has_big_cores)
			set_cpus_unrelated(cpu, i, cpu_smallcore_mask);
	}
}
#endif

static inline void add_cpu_to_smallcore_masks(int cpu)
{
	struct cpumask *this_l1_cache_map = per_cpu(cpu_l1_cache_map, cpu);
	int i, first_thread = cpu_first_thread_sibling(cpu);

	if (!has_big_cores)
		return;

	cpumask_set_cpu(cpu, cpu_smallcore_mask(cpu));

	for (i = first_thread; i < first_thread + threads_per_core; i++) {
		if (cpu_online(i) && cpumask_test_cpu(i, this_l1_cache_map))
			set_cpus_related(i, cpu, cpu_smallcore_mask);
	}
}

int get_physical_package_id(int cpu)
{
	int pkg_id = cpu_to_chip_id(cpu);

	/*
	 * If the platform is PowerNV or Guest on KVM, ibm,chip-id is
	 * defined. Hence we would return the chip-id as the result of
	 * get_physical_package_id.
	 */
	if (pkg_id == -1 && firmware_has_feature(FW_FEATURE_LPAR) &&
	    IS_ENABLED(CONFIG_PPC_SPLPAR)) {
		struct device_node *np = of_get_cpu_node(cpu, NULL);
		pkg_id = of_node_to_nid(np);
		of_node_put(np);
	}

	return pkg_id;
}
EXPORT_SYMBOL_GPL(get_physical_package_id);

static void add_cpu_to_masks(int cpu)
{
	int first_thread = cpu_first_thread_sibling(cpu);
	int pkg_id = get_physical_package_id(cpu);
	int i;

	/*
	 * This CPU will not be in the online mask yet so we need to manually
	 * add it to it's own thread sibling mask.
	 */
	cpumask_set_cpu(cpu, cpu_sibling_mask(cpu));

	for (i = first_thread; i < first_thread + threads_per_core; i++)
		if (cpu_online(i))
			set_cpus_related(i, cpu, cpu_sibling_mask);

	add_cpu_to_smallcore_masks(cpu);
	/*
	 * Copy the thread sibling mask into the cache sibling mask
	 * and mark any CPUs that share an L2 with this CPU.
	 */
	for_each_cpu(i, cpu_sibling_mask(cpu))
		set_cpus_related(cpu, i, cpu_l2_cache_mask);
	update_mask_by_l2(cpu, cpu_l2_cache_mask);

	/*
	 * Copy the cache sibling mask into core sibling mask and mark
	 * any CPUs on the same chip as this CPU.
	 */
	for_each_cpu(i, cpu_l2_cache_mask(cpu))
		set_cpus_related(cpu, i, cpu_core_mask);

	if (pkg_id == -1)
		return;

	for_each_cpu(i, cpu_online_mask)
		if (get_physical_package_id(i) == pkg_id)
			set_cpus_related(cpu, i, cpu_core_mask);
}

static bool shared_caches;

/* Activate a secondary processor. */
void start_secondary(void *unused)
{
	unsigned int cpu = smp_processor_id();
	struct cpumask *(*sibling_mask)(int) = cpu_sibling_mask;

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
	/* Update topology CPU masks */
	add_cpu_to_masks(cpu);

	if (has_big_cores)
		sibling_mask = cpu_smallcore_mask;
	/*
	 * Check for any shared caches. Note that this must be done on a
	 * per-core basis because one core in the pair might be disabled.
	 */
	if (!cpumask_equal(cpu_l2_cache_mask(cpu), sibling_mask(cpu)))
		shared_caches = true;

	set_numa_node(numa_cpu_lookup_table[cpu]);
	set_numa_mem(local_memory_node(numa_cpu_lookup_table[cpu]));

	smp_wmb();
	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);

	boot_init_stack_canary();

	local_irq_enable();

	/* We can enable ftrace for secondary cpus now */
	this_cpu_enable_ftrace();

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

/*
 * P9 has a slightly odd architecture where pairs of cores share an L2 cache.
 * This topology makes it *much* cheaper to migrate tasks between adjacent cores
 * since the migrated task remains cache hot. We want to take advantage of this
 * at the scheduler level so an extra topology level is required.
 */
static int powerpc_shared_cache_flags(void)
{
	return SD_SHARE_PKG_RESOURCES;
}

/*
 * We can't just pass cpu_l2_cache_mask() directly because
 * returns a non-const pointer and the compiler barfs on that.
 */
static const struct cpumask *shared_cache_mask(int cpu)
{
	return cpu_l2_cache_mask(cpu);
}

#ifdef CONFIG_SCHED_SMT
static const struct cpumask *smallcore_smt_mask(int cpu)
{
	return cpu_smallcore_mask(cpu);
}
#endif

static struct sched_domain_topology_level power9_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, powerpc_smt_flags, SD_INIT_NAME(SMT) },
#endif
	{ shared_cache_mask, powerpc_shared_cache_flags, SD_INIT_NAME(CACHE) },
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

#ifdef CONFIG_SCHED_SMT
	if (has_big_cores) {
		pr_info("Big cores detected but using small core scheduling\n");
		power9_topology[0].mask = smallcore_smt_mask;
		powerpc_topology[0].mask = smallcore_smt_mask;
	}
#endif
	/*
	 * If any CPU detects that it's sharing a cache with another CPU then
	 * use the deeper topology that is aware of this sharing.
	 */
	if (shared_caches) {
		pr_info("Using shared cache scheduler topology\n");
		set_sched_topology(power9_topology);
	} else {
		pr_info("Using standard scheduler topology\n");
		set_sched_topology(powerpc_topology);
	}
}

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void)
{
	int cpu = smp_processor_id();
	int err;

	if (!smp_ops->cpu_disable)
		return -ENOSYS;

	this_cpu_disable_ftrace();

	err = smp_ops->cpu_disable();
	if (err)
		return err;

	/* Update sibling maps */
	remove_cpu_from_masks(cpu);

	return 0;
}

void __cpu_die(unsigned int cpu)
{
	if (smp_ops->cpu_die)
		smp_ops->cpu_die(cpu);
}

void cpu_die(void)
{
	/*
	 * Disable on the down path. This will be re-enabled by
	 * start_secondary() via start_secondary_resume() below
	 */
	this_cpu_disable_ftrace();

	if (ppc_md.cpu_die)
		ppc_md.cpu_die();

	/* If we return, we re-enter start_secondary */
	start_secondary_resume();
}

#endif
