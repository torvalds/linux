/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/kernel_stat.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/log2.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#include <asm/paravirt.h>

#include <xen/interface/xen.h>
#include <xen/events.h>

#include "xen-ops.h"
#include "debugfs.h"

enum xen_contention_stat {
	TAKEN_SLOW,
	TAKEN_SLOW_PICKUP,
	TAKEN_SLOW_SPURIOUS,
	RELEASED_SLOW,
	RELEASED_SLOW_KICKED,
	NR_CONTENTION_STATS
};


#ifdef CONFIG_XEN_DEBUG_FS
#define HISTO_BUCKETS	30
static struct xen_spinlock_stats
{
	u32 contention_stats[NR_CONTENTION_STATS];
	u32 histo_spin_blocked[HISTO_BUCKETS+1];
	u64 time_blocked;
} spinlock_stats;

static u8 zero_stats;

static inline void check_zero(void)
{
	u8 ret;
	u8 old = ACCESS_ONCE(zero_stats);
	if (unlikely(old)) {
		ret = cmpxchg(&zero_stats, old, 0);
		/* This ensures only one fellow resets the stat */
		if (ret == old)
			memset(&spinlock_stats, 0, sizeof(spinlock_stats));
	}
}

static inline void add_stats(enum xen_contention_stat var, u32 val)
{
	check_zero();
	spinlock_stats.contention_stats[var] += val;
}

static inline u64 spin_time_start(void)
{
	return xen_clocksource_read();
}

static void __spin_time_accum(u64 delta, u32 *array)
{
	unsigned index = ilog2(delta);

	check_zero();

	if (index < HISTO_BUCKETS)
		array[index]++;
	else
		array[HISTO_BUCKETS]++;
}

static inline void spin_time_accum_blocked(u64 start)
{
	u32 delta = xen_clocksource_read() - start;

	__spin_time_accum(delta, spinlock_stats.histo_spin_blocked);
	spinlock_stats.time_blocked += delta;
}
#else  /* !CONFIG_XEN_DEBUG_FS */
#define TIMEOUT			(1 << 10)
static inline void add_stats(enum xen_contention_stat var, u32 val)
{
}

static inline u64 spin_time_start(void)
{
	return 0;
}

static inline void spin_time_accum_blocked(u64 start)
{
}
#endif  /* CONFIG_XEN_DEBUG_FS */

/*
 * Size struct xen_spinlock so it's the same as arch_spinlock_t.
 */
#if NR_CPUS < 256
typedef u8 xen_spinners_t;
# define inc_spinners(xl) \
	asm(LOCK_PREFIX " incb %0" : "+m" ((xl)->spinners) : : "memory");
# define dec_spinners(xl) \
	asm(LOCK_PREFIX " decb %0" : "+m" ((xl)->spinners) : : "memory");
#else
typedef u16 xen_spinners_t;
# define inc_spinners(xl) \
	asm(LOCK_PREFIX " incw %0" : "+m" ((xl)->spinners) : : "memory");
# define dec_spinners(xl) \
	asm(LOCK_PREFIX " decw %0" : "+m" ((xl)->spinners) : : "memory");
#endif

struct xen_lock_waiting {
	struct arch_spinlock *lock;
	__ticket_t want;
};

static DEFINE_PER_CPU(int, lock_kicker_irq) = -1;
static DEFINE_PER_CPU(char *, irq_name);
static DEFINE_PER_CPU(struct xen_lock_waiting, lock_waiting);
static cpumask_t waiting_cpus;

static void xen_lock_spinning(struct arch_spinlock *lock, __ticket_t want)
{
	int irq = __this_cpu_read(lock_kicker_irq);
	struct xen_lock_waiting *w = &__get_cpu_var(lock_waiting);
	int cpu = smp_processor_id();
	u64 start;
	unsigned long flags;

	/* If kicker interrupts not initialized yet, just spin */
	if (irq == -1)
		return;

	start = spin_time_start();

	/*
	 * Make sure an interrupt handler can't upset things in a
	 * partially setup state.
	 */
	local_irq_save(flags);
	/*
	 * We don't really care if we're overwriting some other
	 * (lock,want) pair, as that would mean that we're currently
	 * in an interrupt context, and the outer context had
	 * interrupts enabled.  That has already kicked the VCPU out
	 * of xen_poll_irq(), so it will just return spuriously and
	 * retry with newly setup (lock,want).
	 *
	 * The ordering protocol on this is that the "lock" pointer
	 * may only be set non-NULL if the "want" ticket is correct.
	 * If we're updating "want", we must first clear "lock".
	 */
	w->lock = NULL;
	smp_wmb();
	w->want = want;
	smp_wmb();
	w->lock = lock;

	/* This uses set_bit, which atomic and therefore a barrier */
	cpumask_set_cpu(cpu, &waiting_cpus);
	add_stats(TAKEN_SLOW, 1);

	/* clear pending */
	xen_clear_irq_pending(irq);

	/* Only check lock once pending cleared */
	barrier();

	/*
	 * Mark entry to slowpath before doing the pickup test to make
	 * sure we don't deadlock with an unlocker.
	 */
	__ticket_enter_slowpath(lock);

	/*
	 * check again make sure it didn't become free while
	 * we weren't looking
	 */
	if (ACCESS_ONCE(lock->tickets.head) == want) {
		add_stats(TAKEN_SLOW_PICKUP, 1);
		goto out;
	}

	/* Allow interrupts while blocked */
	local_irq_restore(flags);

	/*
	 * If an interrupt happens here, it will leave the wakeup irq
	 * pending, which will cause xen_poll_irq() to return
	 * immediately.
	 */

	/* Block until irq becomes pending (or perhaps a spurious wakeup) */
	xen_poll_irq(irq);
	add_stats(TAKEN_SLOW_SPURIOUS, !xen_test_irq_pending(irq));

	local_irq_save(flags);

	kstat_incr_irqs_this_cpu(irq, irq_to_desc(irq));
out:
	cpumask_clear_cpu(cpu, &waiting_cpus);
	w->lock = NULL;

	local_irq_restore(flags);

	spin_time_accum_blocked(start);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_lock_spinning);

static void xen_unlock_kick(struct arch_spinlock *lock, __ticket_t next)
{
	int cpu;

	add_stats(RELEASED_SLOW, 1);

	for_each_cpu(cpu, &waiting_cpus) {
		const struct xen_lock_waiting *w = &per_cpu(lock_waiting, cpu);

		/* Make sure we read lock before want */
		if (ACCESS_ONCE(w->lock) == lock &&
		    ACCESS_ONCE(w->want) == next) {
			add_stats(RELEASED_SLOW_KICKED, 1);
			xen_send_IPI_one(cpu, XEN_SPIN_UNLOCK_VECTOR);
			break;
		}
	}
}

static irqreturn_t dummy_handler(int irq, void *dev_id)
{
	BUG();
	return IRQ_HANDLED;
}

void xen_init_lock_cpu(int cpu)
{
	int irq;
	char *name;

	WARN(per_cpu(lock_kicker_irq, cpu) >= 0, "spinlock on CPU%d exists on IRQ%d!\n",
	     cpu, per_cpu(lock_kicker_irq, cpu));

	/*
	 * See git commit f10cd522c5fbfec9ae3cc01967868c9c2401ed23
	 * (xen: disable PV spinlocks on HVM)
	 */
	if (xen_hvm_domain())
		return;

	name = kasprintf(GFP_KERNEL, "spinlock%d", cpu);
	irq = bind_ipi_to_irqhandler(XEN_SPIN_UNLOCK_VECTOR,
				     cpu,
				     dummy_handler,
				     IRQF_DISABLED|IRQF_PERCPU|IRQF_NOBALANCING,
				     name,
				     NULL);

	if (irq >= 0) {
		disable_irq(irq); /* make sure it's never delivered */
		per_cpu(lock_kicker_irq, cpu) = irq;
		per_cpu(irq_name, cpu) = name;
	}

	printk("cpu %d spinlock event irq %d\n", cpu, irq);
}

void xen_uninit_lock_cpu(int cpu)
{
	/*
	 * See git commit f10cd522c5fbfec9ae3cc01967868c9c2401ed23
	 * (xen: disable PV spinlocks on HVM)
	 */
	if (xen_hvm_domain())
		return;

	unbind_from_irqhandler(per_cpu(lock_kicker_irq, cpu), NULL);
	per_cpu(lock_kicker_irq, cpu) = -1;
	kfree(per_cpu(irq_name, cpu));
	per_cpu(irq_name, cpu) = NULL;
}

static bool xen_pvspin __initdata = true;

void __init xen_init_spinlocks(void)
{
	/*
	 * See git commit f10cd522c5fbfec9ae3cc01967868c9c2401ed23
	 * (xen: disable PV spinlocks on HVM)
	 */
	if (xen_hvm_domain())
		return;

	if (!xen_pvspin) {
		printk(KERN_DEBUG "xen: PV spinlocks disabled\n");
		return;
	}

	static_key_slow_inc(&paravirt_ticketlocks_enabled);

	pv_lock_ops.lock_spinning = PV_CALLEE_SAVE(xen_lock_spinning);
	pv_lock_ops.unlock_kick = xen_unlock_kick;
}

static __init int xen_parse_nopvspin(char *arg)
{
	xen_pvspin = false;
	return 0;
}
early_param("xen_nopvspin", xen_parse_nopvspin);

#ifdef CONFIG_XEN_DEBUG_FS

static struct dentry *d_spin_debug;

static int __init xen_spinlock_debugfs(void)
{
	struct dentry *d_xen = xen_init_debugfs();

	if (d_xen == NULL)
		return -ENOMEM;

	d_spin_debug = debugfs_create_dir("spinlocks", d_xen);

	debugfs_create_u8("zero_stats", 0644, d_spin_debug, &zero_stats);

	debugfs_create_u32("taken_slow", 0444, d_spin_debug,
			   &spinlock_stats.contention_stats[TAKEN_SLOW]);
	debugfs_create_u32("taken_slow_pickup", 0444, d_spin_debug,
			   &spinlock_stats.contention_stats[TAKEN_SLOW_PICKUP]);
	debugfs_create_u32("taken_slow_spurious", 0444, d_spin_debug,
			   &spinlock_stats.contention_stats[TAKEN_SLOW_SPURIOUS]);

	debugfs_create_u32("released_slow", 0444, d_spin_debug,
			   &spinlock_stats.contention_stats[RELEASED_SLOW]);
	debugfs_create_u32("released_slow_kicked", 0444, d_spin_debug,
			   &spinlock_stats.contention_stats[RELEASED_SLOW_KICKED]);

	debugfs_create_u64("time_blocked", 0444, d_spin_debug,
			   &spinlock_stats.time_blocked);

	debugfs_create_u32_array("histo_blocked", 0444, d_spin_debug,
				spinlock_stats.histo_spin_blocked, HISTO_BUCKETS + 1);

	return 0;
}
fs_initcall(xen_spinlock_debugfs);

#endif	/* CONFIG_XEN_DEBUG_FS */
