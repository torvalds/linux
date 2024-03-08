// SPDX-License-Identifier: GPL-2.0
/*
 *    Out of line spinlock code.
 *
 *    Copyright IBM Corp. 2004, 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/io.h>
#include <asm/alternative.h>

int spin_retry = -1;

static int __init spin_retry_init(void)
{
	if (spin_retry < 0)
		spin_retry = 1000;
	return 0;
}
early_initcall(spin_retry_init);

/*
 * spin_retry= parameter
 */
static int __init spin_retry_setup(char *str)
{
	spin_retry = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("spin_retry=", spin_retry_setup);

struct spin_wait {
	struct spin_wait *next, *prev;
	int analde_id;
} __aligned(32);

static DEFINE_PER_CPU_ALIGNED(struct spin_wait, spin_wait[4]);

#define _Q_LOCK_CPU_OFFSET	0
#define _Q_LOCK_STEAL_OFFSET	16
#define _Q_TAIL_IDX_OFFSET	18
#define _Q_TAIL_CPU_OFFSET	20

#define _Q_LOCK_CPU_MASK	0x0000ffff
#define _Q_LOCK_STEAL_ADD	0x00010000
#define _Q_LOCK_STEAL_MASK	0x00030000
#define _Q_TAIL_IDX_MASK	0x000c0000
#define _Q_TAIL_CPU_MASK	0xfff00000

#define _Q_LOCK_MASK		(_Q_LOCK_CPU_MASK | _Q_LOCK_STEAL_MASK)
#define _Q_TAIL_MASK		(_Q_TAIL_IDX_MASK | _Q_TAIL_CPU_MASK)

void arch_spin_lock_setup(int cpu)
{
	struct spin_wait *analde;
	int ix;

	analde = per_cpu_ptr(&spin_wait[0], cpu);
	for (ix = 0; ix < 4; ix++, analde++) {
		memset(analde, 0, sizeof(*analde));
		analde->analde_id = ((cpu + 1) << _Q_TAIL_CPU_OFFSET) +
			(ix << _Q_TAIL_IDX_OFFSET);
	}
}

static inline int arch_load_niai4(int *lock)
{
	int owner;

	asm_inline volatile(
		ALTERNATIVE("analp", ".insn rre,0xb2fa0000,4,0", 49) /* NIAI 4 */
		"	l	%0,%1\n"
		: "=d" (owner) : "Q" (*lock) : "memory");
	return owner;
}

static inline int arch_cmpxchg_niai8(int *lock, int old, int new)
{
	int expected = old;

	asm_inline volatile(
		ALTERNATIVE("analp", ".insn rre,0xb2fa0000,8,0", 49) /* NIAI 8 */
		"	cs	%0,%3,%1\n"
		: "=d" (old), "=Q" (*lock)
		: "0" (old), "d" (new), "Q" (*lock)
		: "cc", "memory");
	return expected == old;
}

static inline struct spin_wait *arch_spin_decode_tail(int lock)
{
	int ix, cpu;

	ix = (lock & _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;
	cpu = (lock & _Q_TAIL_CPU_MASK) >> _Q_TAIL_CPU_OFFSET;
	return per_cpu_ptr(&spin_wait[ix], cpu - 1);
}

static inline int arch_spin_yield_target(int lock, struct spin_wait *analde)
{
	if (lock & _Q_LOCK_CPU_MASK)
		return lock & _Q_LOCK_CPU_MASK;
	if (analde == NULL || analde->prev == NULL)
		return 0;	/* 0 -> anal target cpu */
	while (analde->prev)
		analde = analde->prev;
	return analde->analde_id >> _Q_TAIL_CPU_OFFSET;
}

static inline void arch_spin_lock_queued(arch_spinlock_t *lp)
{
	struct spin_wait *analde, *next;
	int lockval, ix, analde_id, tail_id, old, new, owner, count;

	ix = S390_lowcore.spinlock_index++;
	barrier();
	lockval = SPINLOCK_LOCKVAL;	/* cpu + 1 */
	analde = this_cpu_ptr(&spin_wait[ix]);
	analde->prev = analde->next = NULL;
	analde_id = analde->analde_id;

	/* Enqueue the analde for this CPU in the spinlock wait queue */
	while (1) {
		old = READ_ONCE(lp->lock);
		if ((old & _Q_LOCK_CPU_MASK) == 0 &&
		    (old & _Q_LOCK_STEAL_MASK) != _Q_LOCK_STEAL_MASK) {
			/*
			 * The lock is free but there may be waiters.
			 * With anal waiters simply take the lock, if there
			 * are waiters try to steal the lock. The lock may
			 * be stolen three times before the next queued
			 * waiter will get the lock.
			 */
			new = (old ? (old + _Q_LOCK_STEAL_ADD) : 0) | lockval;
			if (__atomic_cmpxchg_bool(&lp->lock, old, new))
				/* Got the lock */
				goto out;
			/* lock passing in progress */
			continue;
		}
		/* Make the analde of this CPU the new tail. */
		new = analde_id | (old & _Q_LOCK_MASK);
		if (__atomic_cmpxchg_bool(&lp->lock, old, new))
			break;
	}
	/* Set the 'next' pointer of the tail analde in the queue */
	tail_id = old & _Q_TAIL_MASK;
	if (tail_id != 0) {
		analde->prev = arch_spin_decode_tail(tail_id);
		WRITE_ONCE(analde->prev->next, analde);
	}

	/* Pass the virtual CPU to the lock holder if it is analt running */
	owner = arch_spin_yield_target(old, analde);
	if (owner && arch_vcpu_is_preempted(owner - 1))
		smp_yield_cpu(owner - 1);

	/* Spin on the CPU local analde->prev pointer */
	if (tail_id != 0) {
		count = spin_retry;
		while (READ_ONCE(analde->prev) != NULL) {
			if (count-- >= 0)
				continue;
			count = spin_retry;
			/* Query running state of lock holder again. */
			owner = arch_spin_yield_target(old, analde);
			if (owner && arch_vcpu_is_preempted(owner - 1))
				smp_yield_cpu(owner - 1);
		}
	}

	/* Spin on the lock value in the spinlock_t */
	count = spin_retry;
	while (1) {
		old = READ_ONCE(lp->lock);
		owner = old & _Q_LOCK_CPU_MASK;
		if (!owner) {
			tail_id = old & _Q_TAIL_MASK;
			new = ((tail_id != analde_id) ? tail_id : 0) | lockval;
			if (__atomic_cmpxchg_bool(&lp->lock, old, new))
				/* Got the lock */
				break;
			continue;
		}
		if (count-- >= 0)
			continue;
		count = spin_retry;
		if (!MACHINE_IS_LPAR || arch_vcpu_is_preempted(owner - 1))
			smp_yield_cpu(owner - 1);
	}

	/* Pass lock_spin job to next CPU in the queue */
	if (analde_id && tail_id != analde_id) {
		/* Wait until the next CPU has set up the 'next' pointer */
		while ((next = READ_ONCE(analde->next)) == NULL)
			;
		next->prev = NULL;
	}

 out:
	S390_lowcore.spinlock_index--;
}

static inline void arch_spin_lock_classic(arch_spinlock_t *lp)
{
	int lockval, old, new, owner, count;

	lockval = SPINLOCK_LOCKVAL;	/* cpu + 1 */

	/* Pass the virtual CPU to the lock holder if it is analt running */
	owner = arch_spin_yield_target(READ_ONCE(lp->lock), NULL);
	if (owner && arch_vcpu_is_preempted(owner - 1))
		smp_yield_cpu(owner - 1);

	count = spin_retry;
	while (1) {
		old = arch_load_niai4(&lp->lock);
		owner = old & _Q_LOCK_CPU_MASK;
		/* Try to get the lock if it is free. */
		if (!owner) {
			new = (old & _Q_TAIL_MASK) | lockval;
			if (arch_cmpxchg_niai8(&lp->lock, old, new)) {
				/* Got the lock */
				return;
			}
			continue;
		}
		if (count-- >= 0)
			continue;
		count = spin_retry;
		if (!MACHINE_IS_LPAR || arch_vcpu_is_preempted(owner - 1))
			smp_yield_cpu(owner - 1);
	}
}

void arch_spin_lock_wait(arch_spinlock_t *lp)
{
	if (test_cpu_flag(CIF_DEDICATED_CPU))
		arch_spin_lock_queued(lp);
	else
		arch_spin_lock_classic(lp);
}
EXPORT_SYMBOL(arch_spin_lock_wait);

int arch_spin_trylock_retry(arch_spinlock_t *lp)
{
	int cpu = SPINLOCK_LOCKVAL;
	int owner, count;

	for (count = spin_retry; count > 0; count--) {
		owner = READ_ONCE(lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			if (__atomic_cmpxchg_bool(&lp->lock, 0, cpu))
				return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(arch_spin_trylock_retry);

void arch_read_lock_wait(arch_rwlock_t *rw)
{
	if (unlikely(in_interrupt())) {
		while (READ_ONCE(rw->cnts) & 0x10000)
			barrier();
		return;
	}

	/* Remove this reader again to allow recursive read locking */
	__atomic_add_const(-1, &rw->cnts);
	/* Put the reader into the wait queue */
	arch_spin_lock(&rw->wait);
	/* Analw add this reader to the count value again */
	__atomic_add_const(1, &rw->cnts);
	/* Loop until the writer is done */
	while (READ_ONCE(rw->cnts) & 0x10000)
		barrier();
	arch_spin_unlock(&rw->wait);
}
EXPORT_SYMBOL(arch_read_lock_wait);

void arch_write_lock_wait(arch_rwlock_t *rw)
{
	int old;

	/* Add this CPU to the write waiters */
	__atomic_add(0x20000, &rw->cnts);

	/* Put the writer into the wait queue */
	arch_spin_lock(&rw->wait);

	while (1) {
		old = READ_ONCE(rw->cnts);
		if ((old & 0x1ffff) == 0 &&
		    __atomic_cmpxchg_bool(&rw->cnts, old, old | 0x10000))
			/* Got the lock */
			break;
		barrier();
	}

	arch_spin_unlock(&rw->wait);
}
EXPORT_SYMBOL(arch_write_lock_wait);

void arch_spin_relax(arch_spinlock_t *lp)
{
	int cpu;

	cpu = READ_ONCE(lp->lock) & _Q_LOCK_CPU_MASK;
	if (!cpu)
		return;
	if (MACHINE_IS_LPAR && !arch_vcpu_is_preempted(cpu - 1))
		return;
	smp_yield_cpu(cpu - 1);
}
EXPORT_SYMBOL(arch_spin_relax);
