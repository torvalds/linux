/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/kernel_stat.h>
#include <linux/spinlock.h>

#include <asm/paravirt.h>

#include <xen/interface/xen.h>
#include <xen/events.h>

#include "xen-ops.h"

struct xen_spinlock {
	unsigned char lock;		/* 0 -> free; 1 -> locked */
	unsigned short spinners;	/* count of waiting cpus */
};

static int xen_spin_is_locked(struct raw_spinlock *lock)
{
	struct xen_spinlock *xl = (struct xen_spinlock *)lock;

	return xl->lock != 0;
}

static int xen_spin_is_contended(struct raw_spinlock *lock)
{
	struct xen_spinlock *xl = (struct xen_spinlock *)lock;

	/* Not strictly true; this is only the count of contended
	   lock-takers entering the slow path. */
	return xl->spinners != 0;
}

static int xen_spin_trylock(struct raw_spinlock *lock)
{
	struct xen_spinlock *xl = (struct xen_spinlock *)lock;
	u8 old = 1;

	asm("xchgb %b0,%1"
	    : "+q" (old), "+m" (xl->lock) : : "memory");

	return old == 0;
}

static DEFINE_PER_CPU(int, lock_kicker_irq) = -1;
static DEFINE_PER_CPU(struct xen_spinlock *, lock_spinners);

/*
 * Mark a cpu as interested in a lock.  Returns the CPU's previous
 * lock of interest, in case we got preempted by an interrupt.
 */
static inline struct xen_spinlock *spinning_lock(struct xen_spinlock *xl)
{
	struct xen_spinlock *prev;

	prev = __get_cpu_var(lock_spinners);
	__get_cpu_var(lock_spinners) = xl;

	wmb();			/* set lock of interest before count */

	asm(LOCK_PREFIX " incw %0"
	    : "+m" (xl->spinners) : : "memory");

	return prev;
}

/*
 * Mark a cpu as no longer interested in a lock.  Restores previous
 * lock of interest (NULL for none).
 */
static inline void unspinning_lock(struct xen_spinlock *xl, struct xen_spinlock *prev)
{
	asm(LOCK_PREFIX " decw %0"
	    : "+m" (xl->spinners) : : "memory");
	wmb();			/* decrement count before restoring lock */
	__get_cpu_var(lock_spinners) = prev;
}

static noinline int xen_spin_lock_slow(struct raw_spinlock *lock)
{
	struct xen_spinlock *xl = (struct xen_spinlock *)lock;
	struct xen_spinlock *prev;
	int irq = __get_cpu_var(lock_kicker_irq);
	int ret;

	/* If kicker interrupts not initialized yet, just spin */
	if (irq == -1)
		return 0;

	/* announce we're spinning */
	prev = spinning_lock(xl);

	do {
		/* clear pending */
		xen_clear_irq_pending(irq);

		/* check again make sure it didn't become free while
		   we weren't looking  */
		ret = xen_spin_trylock(lock);
		if (ret) {
			/*
			 * If we interrupted another spinlock while it
			 * was blocking, make sure it doesn't block
			 * without rechecking the lock.
			 */
			if (prev != NULL)
				xen_set_irq_pending(irq);
			goto out;
		}

		/*
		 * Block until irq becomes pending.  If we're
		 * interrupted at this point (after the trylock but
		 * before entering the block), then the nested lock
		 * handler guarantees that the irq will be left
		 * pending if there's any chance the lock became free;
		 * xen_poll_irq() returns immediately if the irq is
		 * pending.
		 */
		xen_poll_irq(irq);
	} while (!xen_test_irq_pending(irq)); /* check for spurious wakeups */

	kstat_this_cpu.irqs[irq]++;

out:
	unspinning_lock(xl, prev);
	return ret;
}

static void xen_spin_lock(struct raw_spinlock *lock)
{
	struct xen_spinlock *xl = (struct xen_spinlock *)lock;
	int timeout;
	u8 oldval;

	do {
		timeout = 1 << 10;

		asm("1: xchgb %1,%0\n"
		    "   testb %1,%1\n"
		    "   jz 3f\n"
		    "2: rep;nop\n"
		    "   cmpb $0,%0\n"
		    "   je 1b\n"
		    "   dec %2\n"
		    "   jnz 2b\n"
		    "3:\n"
		    : "+m" (xl->lock), "=q" (oldval), "+r" (timeout)
		    : "1" (1)
		    : "memory");

	} while (unlikely(oldval != 0 && !xen_spin_lock_slow(lock)));
}

static noinline void xen_spin_unlock_slow(struct xen_spinlock *xl)
{
	int cpu;

	for_each_online_cpu(cpu) {
		/* XXX should mix up next cpu selection */
		if (per_cpu(lock_spinners, cpu) == xl) {
			xen_send_IPI_one(cpu, XEN_SPIN_UNLOCK_VECTOR);
			break;
		}
	}
}

static void xen_spin_unlock(struct raw_spinlock *lock)
{
	struct xen_spinlock *xl = (struct xen_spinlock *)lock;

	smp_wmb();		/* make sure no writes get moved after unlock */
	xl->lock = 0;		/* release lock */

	/* make sure unlock happens before kick */
	barrier();

	if (unlikely(xl->spinners))
		xen_spin_unlock_slow(xl);
}

static irqreturn_t dummy_handler(int irq, void *dev_id)
{
	BUG();
	return IRQ_HANDLED;
}

void __cpuinit xen_init_lock_cpu(int cpu)
{
	int irq;
	const char *name;

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
	}

	printk("cpu %d spinlock event irq %d\n", cpu, irq);
}

void __init xen_init_spinlocks(void)
{
	pv_lock_ops.spin_is_locked = xen_spin_is_locked;
	pv_lock_ops.spin_is_contended = xen_spin_is_contended;
	pv_lock_ops.spin_lock = xen_spin_lock;
	pv_lock_ops.spin_trylock = xen_spin_trylock;
	pv_lock_ops.spin_unlock = xen_spin_unlock;
}
