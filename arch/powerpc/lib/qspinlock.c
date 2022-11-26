// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/atomic.h>
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

static DEFINE_PER_CPU_ALIGNED(struct qnodes, qnodes);

static inline int encode_tail_cpu(int cpu)
{
	return (cpu + 1) << _Q_TAIL_CPU_OFFSET;
}

static inline int decode_tail_cpu(int val)
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
static __always_inline int set_locked_clean_tail(struct qspinlock *lock, int tail)
{
	int val = atomic_read(&lock->val);

	BUG_ON(val & _Q_LOCKED_VAL);

	/* If we're the last queued, must clean up the tail. */
	if ((val & _Q_TAIL_CPU_MASK) == tail) {
		if (atomic_cmpxchg_acquire(&lock->val, val, _Q_LOCKED_VAL) == val)
			return val;
		/* Another waiter must have enqueued */
		val = atomic_read(&lock->val);
		BUG_ON(val & _Q_LOCKED_VAL);
	}

	/* We must be the owner, just set the lock bit and acquire */
	atomic_or(_Q_LOCKED_VAL, &lock->val);
	__atomic_acquire_fence();

	return val;
}

/*
 * Publish our tail, replacing previous tail. Return previous value.
 *
 * This provides a release barrier for publishing node, this pairs with the
 * acquire barrier in get_tail_qnode() when the next CPU finds this tail
 * value.
 */
static __always_inline int publish_tail_cpu(struct qspinlock *lock, int tail)
{
	for (;;) {
		int val = atomic_read(&lock->val);
		int newval = (val & ~_Q_TAIL_CPU_MASK) | tail;
		int old;

		old = atomic_cmpxchg_release(&lock->val, val, newval);
		if (old == val)
			return old;
	}
}

static struct qnode *get_tail_qnode(struct qspinlock *lock, int val)
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

static inline void queued_spin_lock_mcs_queue(struct qspinlock *lock)
{
	struct qnodes *qnodesp;
	struct qnode *next, *node;
	int val, old, tail;
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

	/* We're at the head of the waitqueue, wait for the lock. */
	for (;;) {
		val = atomic_read(&lock->val);
		if (!(val & _Q_LOCKED_VAL))
			break;

		cpu_relax();
	}

	/* If we're the last queued, must clean up the tail. */
	old = set_locked_clean_tail(lock, tail);
	if ((old & _Q_TAIL_CPU_MASK) == tail)
		goto release; /* Another waiter must have enqueued */

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
	queued_spin_lock_mcs_queue(lock);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void pv_spinlocks_init(void)
{
}
#endif
