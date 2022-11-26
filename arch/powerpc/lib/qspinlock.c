// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <asm/qspinlock.h>

#define MAX_NODES	4

struct qnode {
	struct qnode	*next;
	struct qspinlock *lock;
	u8		locked; /* 1 if lock acquired */
};

struct qnodes {
	int		count;
	struct qnode nodes[MAX_NODES];
};

/* Tuning parameters */
static int steal_spins __read_mostly = (1 << 5);
static bool maybe_stealers __read_mostly = true;

static DEFINE_PER_CPU_ALIGNED(struct qnodes, qnodes);

static __always_inline int get_steal_spins(void)
{
	return steal_spins;
}

static inline u32 encode_tail_cpu(int cpu)
{
	return (cpu + 1) << _Q_TAIL_CPU_OFFSET;
}

static inline int decode_tail_cpu(u32 val)
{
	return (val >> _Q_TAIL_CPU_OFFSET) - 1;
}

/*
 * Try to acquire the lock if it was not already locked. If the tail matches
 * mytail then clear it, otherwise leave it unchnaged. Return previous value.
 *
 * This is used by the head of the queue to acquire the lock and clean up
 * its tail if it was the last one queued.
 */
static __always_inline u32 trylock_clean_tail(struct qspinlock *lock, u32 tail)
{
	u32 newval = _Q_LOCKED_VAL;
	u32 prev, tmp;

	asm volatile(
"1:	lwarx	%0,0,%2,%7	# trylock_clean_tail			\n"
	/* This test is necessary if there could be stealers */
"	andi.	%1,%0,%5						\n"
"	bne	3f							\n"
	/* Test whether the lock tail == mytail */
"	and	%1,%0,%6						\n"
"	cmpw	0,%1,%3							\n"
	/* Merge the new locked value */
"	or	%1,%1,%4						\n"
"	bne	2f							\n"
	/* If the lock tail matched, then clear it, otherwise leave it. */
"	andc	%1,%1,%6						\n"
"2:	stwcx.	%1,0,%2							\n"
"	bne-	1b							\n"
"\t"	PPC_ACQUIRE_BARRIER "						\n"
"3:									\n"
	: "=&r" (prev), "=&r" (tmp)
	: "r" (&lock->val), "r"(tail), "r" (newval),
	  "i" (_Q_LOCKED_VAL),
	  "r" (_Q_TAIL_CPU_MASK),
	  "i" (IS_ENABLED(CONFIG_PPC64))
	: "cr0", "memory");

	return prev;
}

/*
 * Publish our tail, replacing previous tail. Return previous value.
 *
 * This provides a release barrier for publishing node, this pairs with the
 * acquire barrier in get_tail_qnode() when the next CPU finds this tail
 * value.
 */
static __always_inline u32 publish_tail_cpu(struct qspinlock *lock, u32 tail)
{
	u32 prev, tmp;

	asm volatile(
"\t"	PPC_RELEASE_BARRIER "						\n"
"1:	lwarx	%0,0,%2		# publish_tail_cpu			\n"
"	andc	%1,%0,%4						\n"
"	or	%1,%1,%3						\n"
"	stwcx.	%1,0,%2							\n"
"	bne-	1b							\n"
	: "=&r" (prev), "=&r"(tmp)
	: "r" (&lock->val), "r" (tail), "r"(_Q_TAIL_CPU_MASK)
	: "cr0", "memory");

	return prev;
}

static struct qnode *get_tail_qnode(struct qspinlock *lock, u32 val)
{
	int cpu = decode_tail_cpu(val);
	struct qnodes *qnodesp = per_cpu_ptr(&qnodes, cpu);
	int idx;

	/*
	 * After publishing the new tail and finding a previous tail in the
	 * previous val (which is the control dependency), this barrier
	 * orders the release barrier in publish_tail_cpu performed by the
	 * last CPU, with subsequently looking at its qnode structures
	 * after the barrier.
	 */
	smp_acquire__after_ctrl_dep();

	for (idx = 0; idx < MAX_NODES; idx++) {
		struct qnode *qnode = &qnodesp->nodes[idx];
		if (qnode->lock == lock)
			return qnode;
	}

	BUG();
}

static inline bool try_to_steal_lock(struct qspinlock *lock)
{
	int iters = 0;

	if (!steal_spins)
		return false;

	/* Attempt to steal the lock */
	do {
		u32 val = READ_ONCE(lock->val);

		if (unlikely(!(val & _Q_LOCKED_VAL))) {
			if (__queued_spin_trylock_steal(lock))
				return true;
		} else {
			cpu_relax();
		}

		iters++;
	} while (iters < get_steal_spins());

	return false;
}

static inline void queued_spin_lock_mcs_queue(struct qspinlock *lock)
{
	struct qnodes *qnodesp;
	struct qnode *next, *node;
	u32 val, old, tail;
	int idx;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	qnodesp = this_cpu_ptr(&qnodes);
	if (unlikely(qnodesp->count >= MAX_NODES)) {
		while (!queued_spin_trylock(lock))
			cpu_relax();
		return;
	}

	idx = qnodesp->count++;
	/*
	 * Ensure that we increment the head node->count before initialising
	 * the actual node. If the compiler is kind enough to reorder these
	 * stores, then an IRQ could overwrite our assignments.
	 */
	barrier();
	node = &qnodesp->nodes[idx];
	node->next = NULL;
	node->lock = lock;
	node->locked = 0;

	tail = encode_tail_cpu(smp_processor_id());

	old = publish_tail_cpu(lock, tail);

	/*
	 * If there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_CPU_MASK) {
		struct qnode *prev = get_tail_qnode(lock, old);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node);

		/* Wait for mcs node lock to be released */
		while (!node->locked)
			cpu_relax();

		smp_rmb(); /* acquire barrier for the mcs lock */
	}

again:
	/* We're at the head of the waitqueue, wait for the lock. */
	for (;;) {
		val = READ_ONCE(lock->val);
		if (!(val & _Q_LOCKED_VAL))
			break;

		cpu_relax();
	}

	/* If we're the last queued, must clean up the tail. */
	old = trylock_clean_tail(lock, tail);
	if (unlikely(old & _Q_LOCKED_VAL)) {
		BUG_ON(!maybe_stealers);
		goto again; /* Can only be true if maybe_stealers. */
	}

	if ((old & _Q_TAIL_CPU_MASK) == tail)
		goto release; /* We were the tail, no next. */

	/* There is a next, must wait for node->next != NULL (MCS protocol) */
	while (!(next = READ_ONCE(node->next)))
		cpu_relax();

	/*
	 * Unlock the next mcs waiter node. Release barrier is not required
	 * here because the acquirer is only accessing the lock word, and
	 * the acquire barrier we took the lock with orders that update vs
	 * this store to locked. The corresponding barrier is the smp_rmb()
	 * acquire barrier for mcs lock, above.
	 */
	WRITE_ONCE(next->locked, 1);

release:
	qnodesp->count--; /* release the node */
}

void queued_spin_lock_slowpath(struct qspinlock *lock)
{
	if (try_to_steal_lock(lock))
		return;

	queued_spin_lock_mcs_queue(lock);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void pv_spinlocks_init(void)
{
}
#endif

#include <linux/debugfs.h>
static int steal_spins_set(void *data, u64 val)
{
	static DEFINE_MUTEX(lock);

	/*
	 * The lock slow path has a !maybe_stealers case that can assume
	 * the head of queue will not see concurrent waiters. That waiter
	 * is unsafe in the presence of stealers, so must keep them away
	 * from one another.
	 */

	mutex_lock(&lock);
	if (val && !steal_spins) {
		maybe_stealers = true;
		/* wait for queue head waiter to go away */
		synchronize_rcu();
		steal_spins = val;
	} else if (!val && steal_spins) {
		steal_spins = val;
		/* wait for all possible stealers to go away */
		synchronize_rcu();
		maybe_stealers = false;
	} else {
		steal_spins = val;
	}
	mutex_unlock(&lock);

	return 0;
}

static int steal_spins_get(void *data, u64 *val)
{
	*val = steal_spins;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_steal_spins, steal_spins_get, steal_spins_set, "%llu\n");

static __init int spinlock_debugfs_init(void)
{
	debugfs_create_file("qspl_steal_spins", 0600, arch_debugfs_dir, NULL, &fops_steal_spins);

	return 0;
}
device_initcall(spinlock_debugfs_init);
