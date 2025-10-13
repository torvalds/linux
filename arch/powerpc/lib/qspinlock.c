// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/processor.h>
#include <linux/smp.h>
#include <linux/topology.h>
#include <linux/sched/clock.h>
#include <asm/qspinlock.h>
#include <asm/paravirt.h>
#include <trace/events/lock.h>

#define MAX_NODES	4

struct qnode {
	struct qnode	*next;
	struct qspinlock *lock;
	int		cpu;
	u8		sleepy; /* 1 if the previous vCPU was preempted or
				 * if the previous node was sleepy */
	u8		locked; /* 1 if lock acquired */
};

struct qnodes {
	int		count;
	struct qnode nodes[MAX_NODES];
};

/* Tuning parameters */
static int steal_spins __read_mostly = (1 << 5);
static int remote_steal_spins __read_mostly = (1 << 2);
#if _Q_SPIN_TRY_LOCK_STEAL == 1
static const bool maybe_stealers = true;
#else
static bool maybe_stealers __read_mostly = true;
#endif
static int head_spins __read_mostly = (1 << 8);

static bool pv_yield_owner __read_mostly = true;
static bool pv_yield_allow_steal __read_mostly = false;
static bool pv_spin_on_preempted_owner __read_mostly = false;
static bool pv_sleepy_lock __read_mostly = true;
static bool pv_sleepy_lock_sticky __read_mostly = false;
static u64 pv_sleepy_lock_interval_ns __read_mostly = 0;
static int pv_sleepy_lock_factor __read_mostly = 256;
static bool pv_yield_prev __read_mostly = true;
static bool pv_yield_sleepy_owner __read_mostly = true;
static bool pv_prod_head __read_mostly = false;

static DEFINE_PER_CPU_ALIGNED(struct qnodes, qnodes);
static DEFINE_PER_CPU_ALIGNED(u64, sleepy_lock_seen_clock);

#if _Q_SPIN_SPEC_BARRIER == 1
#define spec_barrier() do { asm volatile("ori 31,31,0" ::: "memory"); } while (0)
#else
#define spec_barrier() do { } while (0)
#endif

static __always_inline bool recently_sleepy(void)
{
	/* pv_sleepy_lock is true when this is called */
	if (pv_sleepy_lock_interval_ns) {
		u64 seen = this_cpu_read(sleepy_lock_seen_clock);

		if (seen) {
			u64 delta = sched_clock() - seen;
			if (delta < pv_sleepy_lock_interval_ns)
				return true;
			this_cpu_write(sleepy_lock_seen_clock, 0);
		}
	}

	return false;
}

static __always_inline int get_steal_spins(bool paravirt, bool sleepy)
{
	if (paravirt && sleepy)
		return steal_spins * pv_sleepy_lock_factor;
	else
		return steal_spins;
}

static __always_inline int get_remote_steal_spins(bool paravirt, bool sleepy)
{
	if (paravirt && sleepy)
		return remote_steal_spins * pv_sleepy_lock_factor;
	else
		return remote_steal_spins;
}

static __always_inline int get_head_spins(bool paravirt, bool sleepy)
{
	if (paravirt && sleepy)
		return head_spins * pv_sleepy_lock_factor;
	else
		return head_spins;
}

static inline u32 encode_tail_cpu(int cpu)
{
	return (cpu + 1) << _Q_TAIL_CPU_OFFSET;
}

static inline int decode_tail_cpu(u32 val)
{
	return (val >> _Q_TAIL_CPU_OFFSET) - 1;
}

static inline int get_owner_cpu(u32 val)
{
	return (val & _Q_OWNER_CPU_MASK) >> _Q_OWNER_CPU_OFFSET;
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
	u32 newval = queued_spin_encode_locked_val();
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
	  "i" (_Q_SPIN_EH_HINT)
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

	kcsan_release();

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

static __always_inline u32 set_mustq(struct qspinlock *lock)
{
	u32 prev;

	asm volatile(
"1:	lwarx	%0,0,%1		# set_mustq				\n"
"	or	%0,%0,%2						\n"
"	stwcx.	%0,0,%1							\n"
"	bne-	1b							\n"
	: "=&r" (prev)
	: "r" (&lock->val), "r" (_Q_MUST_Q_VAL)
	: "cr0", "memory");

	return prev;
}

static __always_inline u32 clear_mustq(struct qspinlock *lock)
{
	u32 prev;

	asm volatile(
"1:	lwarx	%0,0,%1		# clear_mustq				\n"
"	andc	%0,%0,%2						\n"
"	stwcx.	%0,0,%1							\n"
"	bne-	1b							\n"
	: "=&r" (prev)
	: "r" (&lock->val), "r" (_Q_MUST_Q_VAL)
	: "cr0", "memory");

	return prev;
}

static __always_inline bool try_set_sleepy(struct qspinlock *lock, u32 old)
{
	u32 prev;
	u32 new = old | _Q_SLEEPY_VAL;

	BUG_ON(!(old & _Q_LOCKED_VAL));
	BUG_ON(old & _Q_SLEEPY_VAL);

	asm volatile(
"1:	lwarx	%0,0,%1		# try_set_sleepy			\n"
"	cmpw	0,%0,%2							\n"
"	bne-	2f							\n"
"	stwcx.	%3,0,%1							\n"
"	bne-	1b							\n"
"2:									\n"
	: "=&r" (prev)
	: "r" (&lock->val), "r"(old), "r" (new)
	: "cr0", "memory");

	return likely(prev == old);
}

static __always_inline void seen_sleepy_owner(struct qspinlock *lock, u32 val)
{
	if (pv_sleepy_lock) {
		if (pv_sleepy_lock_interval_ns)
			this_cpu_write(sleepy_lock_seen_clock, sched_clock());
		if (!(val & _Q_SLEEPY_VAL))
			try_set_sleepy(lock, val);
	}
}

static __always_inline void seen_sleepy_lock(void)
{
	if (pv_sleepy_lock && pv_sleepy_lock_interval_ns)
		this_cpu_write(sleepy_lock_seen_clock, sched_clock());
}

static __always_inline void seen_sleepy_node(void)
{
	if (pv_sleepy_lock) {
		if (pv_sleepy_lock_interval_ns)
			this_cpu_write(sleepy_lock_seen_clock, sched_clock());
		/* Don't set sleepy because we likely have a stale val */
	}
}

static struct qnode *get_tail_qnode(struct qspinlock *lock, int prev_cpu)
{
	struct qnodes *qnodesp = per_cpu_ptr(&qnodes, prev_cpu);
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

/* Called inside spin_begin(). Returns whether or not the vCPU was preempted. */
static __always_inline bool __yield_to_locked_owner(struct qspinlock *lock, u32 val, bool paravirt, bool mustq)
{
	int owner;
	u32 yield_count;
	bool preempted = false;

	BUG_ON(!(val & _Q_LOCKED_VAL));

	if (!paravirt)
		goto relax;

	if (!pv_yield_owner)
		goto relax;

	owner = get_owner_cpu(val);
	yield_count = yield_count_of(owner);

	if ((yield_count & 1) == 0)
		goto relax; /* owner vcpu is running */

	spin_end();

	seen_sleepy_owner(lock, val);
	preempted = true;

	/*
	 * Read the lock word after sampling the yield count. On the other side
	 * there may a wmb because the yield count update is done by the
	 * hypervisor preemption and the value update by the OS, however this
	 * ordering might reduce the chance of out of order accesses and
	 * improve the heuristic.
	 */
	smp_rmb();

	if (READ_ONCE(lock->val) == val) {
		if (mustq)
			clear_mustq(lock);
		yield_to_preempted(owner, yield_count);
		if (mustq)
			set_mustq(lock);
		spin_begin();

		/* Don't relax if we yielded. Maybe we should? */
		return preempted;
	}
	spin_begin();
relax:
	spin_cpu_relax();

	return preempted;
}

/* Called inside spin_begin(). Returns whether or not the vCPU was preempted. */
static __always_inline bool yield_to_locked_owner(struct qspinlock *lock, u32 val, bool paravirt)
{
	return __yield_to_locked_owner(lock, val, paravirt, false);
}

/* Called inside spin_begin(). Returns whether or not the vCPU was preempted. */
static __always_inline bool yield_head_to_locked_owner(struct qspinlock *lock, u32 val, bool paravirt)
{
	bool mustq = false;

	if ((val & _Q_MUST_Q_VAL) && pv_yield_allow_steal)
		mustq = true;

	return __yield_to_locked_owner(lock, val, paravirt, mustq);
}

static __always_inline void propagate_sleepy(struct qnode *node, u32 val, bool paravirt)
{
	struct qnode *next;
	int owner;

	if (!paravirt)
		return;
	if (!pv_yield_sleepy_owner)
		return;

	next = READ_ONCE(node->next);
	if (!next)
		return;

	if (next->sleepy)
		return;

	owner = get_owner_cpu(val);
	if (vcpu_is_preempted(owner))
		next->sleepy = 1;
}

/* Called inside spin_begin() */
static __always_inline bool yield_to_prev(struct qspinlock *lock, struct qnode *node, int prev_cpu, bool paravirt)
{
	u32 yield_count;
	bool preempted = false;

	if (!paravirt)
		goto relax;

	if (!pv_yield_sleepy_owner)
		goto yield_prev;

	/*
	 * If the previous waiter was preempted it might not be able to
	 * propagate sleepy to us, so check the lock in that case too.
	 */
	if (node->sleepy || vcpu_is_preempted(prev_cpu)) {
		u32 val = READ_ONCE(lock->val);

		if (val & _Q_LOCKED_VAL) {
			if (node->next && !node->next->sleepy) {
				/*
				 * Propagate sleepy to next waiter. Only if
				 * owner is preempted, which allows the queue
				 * to become "non-sleepy" if vCPU preemption
				 * ceases to occur, even if the lock remains
				 * highly contended.
				 */
				if (vcpu_is_preempted(get_owner_cpu(val)))
					node->next->sleepy = 1;
			}

			preempted = yield_to_locked_owner(lock, val, paravirt);
			if (preempted)
				return preempted;
		}
		node->sleepy = false;
	}

yield_prev:
	if (!pv_yield_prev)
		goto relax;

	yield_count = yield_count_of(prev_cpu);
	if ((yield_count & 1) == 0)
		goto relax; /* owner vcpu is running */

	spin_end();

	preempted = true;
	seen_sleepy_node();

	smp_rmb(); /* See __yield_to_locked_owner comment */

	if (!READ_ONCE(node->locked)) {
		yield_to_preempted(prev_cpu, yield_count);
		spin_begin();
		return preempted;
	}
	spin_begin();

relax:
	spin_cpu_relax();

	return preempted;
}

static __always_inline bool steal_break(u32 val, int iters, bool paravirt, bool sleepy)
{
	if (iters >= get_steal_spins(paravirt, sleepy))
		return true;

	if (IS_ENABLED(CONFIG_NUMA) &&
	    (iters >= get_remote_steal_spins(paravirt, sleepy))) {
		int cpu = get_owner_cpu(val);
		if (numa_node_id() != cpu_to_node(cpu))
			return true;
	}
	return false;
}

static __always_inline bool try_to_steal_lock(struct qspinlock *lock, bool paravirt)
{
	bool seen_preempted = false;
	bool sleepy = false;
	int iters = 0;
	u32 val;

	if (!steal_spins) {
		/* XXX: should spin_on_preempted_owner do anything here? */
		return false;
	}

	/* Attempt to steal the lock */
	spin_begin();
	do {
		bool preempted = false;

		val = READ_ONCE(lock->val);
		if (val & _Q_MUST_Q_VAL)
			break;
		spec_barrier();

		if (unlikely(!(val & _Q_LOCKED_VAL))) {
			spin_end();
			if (__queued_spin_trylock_steal(lock))
				return true;
			spin_begin();
		} else {
			preempted = yield_to_locked_owner(lock, val, paravirt);
		}

		if (paravirt && pv_sleepy_lock) {
			if (!sleepy) {
				if (val & _Q_SLEEPY_VAL) {
					seen_sleepy_lock();
					sleepy = true;
				} else if (recently_sleepy()) {
					sleepy = true;
				}
			}
			if (pv_sleepy_lock_sticky && seen_preempted &&
			    !(val & _Q_SLEEPY_VAL)) {
				if (try_set_sleepy(lock, val))
					val |= _Q_SLEEPY_VAL;
			}
		}

		if (preempted) {
			seen_preempted = true;
			sleepy = true;
			if (!pv_spin_on_preempted_owner)
				iters++;
			/*
			 * pv_spin_on_preempted_owner don't increase iters
			 * while the owner is preempted -- we won't interfere
			 * with it by definition. This could introduce some
			 * latency issue if we continually observe preempted
			 * owners, but hopefully that's a rare corner case of
			 * a badly oversubscribed system.
			 */
		} else {
			iters++;
		}
	} while (!steal_break(val, iters, paravirt, sleepy));

	spin_end();

	return false;
}

static __always_inline void queued_spin_lock_mcs_queue(struct qspinlock *lock, bool paravirt)
{
	struct qnodes *qnodesp;
	struct qnode *next, *node;
	u32 val, old, tail;
	bool seen_preempted = false;
	bool sleepy = false;
	bool mustq = false;
	int idx;
	int iters = 0;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	qnodesp = this_cpu_ptr(&qnodes);
	if (unlikely(qnodesp->count >= MAX_NODES)) {
		spec_barrier();
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
	node->cpu = smp_processor_id();
	node->sleepy = 0;
	node->locked = 0;

	tail = encode_tail_cpu(node->cpu);

	/*
	 * Assign all attributes of a node before it can be published.
	 * Issues an lwsync, serving as a release barrier, as well as a
	 * compiler barrier.
	 */
	old = publish_tail_cpu(lock, tail);

	/*
	 * If there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_CPU_MASK) {
		int prev_cpu = decode_tail_cpu(old);
		struct qnode *prev = get_tail_qnode(lock, prev_cpu);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node);

		/* Wait for mcs node lock to be released */
		spin_begin();
		while (!READ_ONCE(node->locked)) {
			spec_barrier();

			if (yield_to_prev(lock, node, prev_cpu, paravirt))
				seen_preempted = true;
		}
		spec_barrier();
		spin_end();

		smp_rmb(); /* acquire barrier for the mcs lock */

		/*
		 * Generic qspinlocks have this prefetch here, but it seems
		 * like it could cause additional line transitions because
		 * the waiter will keep loading from it.
		 */
		if (_Q_SPIN_PREFETCH_NEXT) {
			next = READ_ONCE(node->next);
			if (next)
				prefetchw(next);
		}
	}

	/* We're at the head of the waitqueue, wait for the lock. */
again:
	spin_begin();
	for (;;) {
		bool preempted;

		val = READ_ONCE(lock->val);
		if (!(val & _Q_LOCKED_VAL))
			break;
		spec_barrier();

		if (paravirt && pv_sleepy_lock && maybe_stealers) {
			if (!sleepy) {
				if (val & _Q_SLEEPY_VAL) {
					seen_sleepy_lock();
					sleepy = true;
				} else if (recently_sleepy()) {
					sleepy = true;
				}
			}
			if (pv_sleepy_lock_sticky && seen_preempted &&
			    !(val & _Q_SLEEPY_VAL)) {
				if (try_set_sleepy(lock, val))
					val |= _Q_SLEEPY_VAL;
			}
		}

		propagate_sleepy(node, val, paravirt);
		preempted = yield_head_to_locked_owner(lock, val, paravirt);
		if (!maybe_stealers)
			continue;

		if (preempted)
			seen_preempted = true;

		if (paravirt && preempted) {
			sleepy = true;

			if (!pv_spin_on_preempted_owner)
				iters++;
		} else {
			iters++;
		}

		if (!mustq && iters >= get_head_spins(paravirt, sleepy)) {
			mustq = true;
			set_mustq(lock);
			val |= _Q_MUST_Q_VAL;
		}
	}
	spec_barrier();
	spin_end();

	/* If we're the last queued, must clean up the tail. */
	old = trylock_clean_tail(lock, tail);
	if (unlikely(old & _Q_LOCKED_VAL)) {
		BUG_ON(!maybe_stealers);
		goto again; /* Can only be true if maybe_stealers. */
	}

	if ((old & _Q_TAIL_CPU_MASK) == tail)
		goto release; /* We were the tail, no next. */

	/* There is a next, must wait for node->next != NULL (MCS protocol) */
	next = READ_ONCE(node->next);
	if (!next) {
		spin_begin();
		while (!(next = READ_ONCE(node->next)))
			cpu_relax();
		spin_end();
	}
	spec_barrier();

	/*
	 * Unlock the next mcs waiter node. Release barrier is not required
	 * here because the acquirer is only accessing the lock word, and
	 * the acquire barrier we took the lock with orders that update vs
	 * this store to locked. The corresponding barrier is the smp_rmb()
	 * acquire barrier for mcs lock, above.
	 */
	if (paravirt && pv_prod_head) {
		int next_cpu = next->cpu;
		WRITE_ONCE(next->locked, 1);
		if (_Q_SPIN_MISO)
			asm volatile("miso" ::: "memory");
		if (vcpu_is_preempted(next_cpu))
			prod_cpu(next_cpu);
	} else {
		WRITE_ONCE(next->locked, 1);
		if (_Q_SPIN_MISO)
			asm volatile("miso" ::: "memory");
	}

release:
	/*
	 * Clear the lock before releasing the node, as another CPU might see stale
	 * values if an interrupt occurs after we increment qnodesp->count
	 * but before node->lock is initialized. The barrier ensures that
	 * there are no further stores to the node after it has been released.
	 */
	node->lock = NULL;
	barrier();
	qnodesp->count--;
}

void __lockfunc queued_spin_lock_slowpath(struct qspinlock *lock)
{
	trace_contention_begin(lock, LCB_F_SPIN);
	/*
	 * This looks funny, but it induces the compiler to inline both
	 * sides of the branch rather than share code as when the condition
	 * is passed as the paravirt argument to the functions.
	 */
	if (IS_ENABLED(CONFIG_PARAVIRT_SPINLOCKS) && is_shared_processor()) {
		if (try_to_steal_lock(lock, true))
			spec_barrier();
		else
			queued_spin_lock_mcs_queue(lock, true);
	} else {
		if (try_to_steal_lock(lock, false))
			spec_barrier();
		else
			queued_spin_lock_mcs_queue(lock, false);
	}
	trace_contention_end(lock, 0);
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
#if _Q_SPIN_TRY_LOCK_STEAL == 1
	/* MAYBE_STEAL remains true */
	steal_spins = val;
#else
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
#endif

	return 0;
}

static int steal_spins_get(void *data, u64 *val)
{
	*val = steal_spins;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_steal_spins, steal_spins_get, steal_spins_set, "%llu\n");

static int remote_steal_spins_set(void *data, u64 val)
{
	remote_steal_spins = val;

	return 0;
}

static int remote_steal_spins_get(void *data, u64 *val)
{
	*val = remote_steal_spins;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_remote_steal_spins, remote_steal_spins_get, remote_steal_spins_set, "%llu\n");

static int head_spins_set(void *data, u64 val)
{
	head_spins = val;

	return 0;
}

static int head_spins_get(void *data, u64 *val)
{
	*val = head_spins;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_head_spins, head_spins_get, head_spins_set, "%llu\n");

static int pv_yield_owner_set(void *data, u64 val)
{
	pv_yield_owner = !!val;

	return 0;
}

static int pv_yield_owner_get(void *data, u64 *val)
{
	*val = pv_yield_owner;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_yield_owner, pv_yield_owner_get, pv_yield_owner_set, "%llu\n");

static int pv_yield_allow_steal_set(void *data, u64 val)
{
	pv_yield_allow_steal = !!val;

	return 0;
}

static int pv_yield_allow_steal_get(void *data, u64 *val)
{
	*val = pv_yield_allow_steal;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_yield_allow_steal, pv_yield_allow_steal_get, pv_yield_allow_steal_set, "%llu\n");

static int pv_spin_on_preempted_owner_set(void *data, u64 val)
{
	pv_spin_on_preempted_owner = !!val;

	return 0;
}

static int pv_spin_on_preempted_owner_get(void *data, u64 *val)
{
	*val = pv_spin_on_preempted_owner;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_spin_on_preempted_owner, pv_spin_on_preempted_owner_get, pv_spin_on_preempted_owner_set, "%llu\n");

static int pv_sleepy_lock_set(void *data, u64 val)
{
	pv_sleepy_lock = !!val;

	return 0;
}

static int pv_sleepy_lock_get(void *data, u64 *val)
{
	*val = pv_sleepy_lock;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_sleepy_lock, pv_sleepy_lock_get, pv_sleepy_lock_set, "%llu\n");

static int pv_sleepy_lock_sticky_set(void *data, u64 val)
{
	pv_sleepy_lock_sticky = !!val;

	return 0;
}

static int pv_sleepy_lock_sticky_get(void *data, u64 *val)
{
	*val = pv_sleepy_lock_sticky;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_sleepy_lock_sticky, pv_sleepy_lock_sticky_get, pv_sleepy_lock_sticky_set, "%llu\n");

static int pv_sleepy_lock_interval_ns_set(void *data, u64 val)
{
	pv_sleepy_lock_interval_ns = val;

	return 0;
}

static int pv_sleepy_lock_interval_ns_get(void *data, u64 *val)
{
	*val = pv_sleepy_lock_interval_ns;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_sleepy_lock_interval_ns, pv_sleepy_lock_interval_ns_get, pv_sleepy_lock_interval_ns_set, "%llu\n");

static int pv_sleepy_lock_factor_set(void *data, u64 val)
{
	pv_sleepy_lock_factor = val;

	return 0;
}

static int pv_sleepy_lock_factor_get(void *data, u64 *val)
{
	*val = pv_sleepy_lock_factor;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_sleepy_lock_factor, pv_sleepy_lock_factor_get, pv_sleepy_lock_factor_set, "%llu\n");

static int pv_yield_prev_set(void *data, u64 val)
{
	pv_yield_prev = !!val;

	return 0;
}

static int pv_yield_prev_get(void *data, u64 *val)
{
	*val = pv_yield_prev;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_yield_prev, pv_yield_prev_get, pv_yield_prev_set, "%llu\n");

static int pv_yield_sleepy_owner_set(void *data, u64 val)
{
	pv_yield_sleepy_owner = !!val;

	return 0;
}

static int pv_yield_sleepy_owner_get(void *data, u64 *val)
{
	*val = pv_yield_sleepy_owner;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_yield_sleepy_owner, pv_yield_sleepy_owner_get, pv_yield_sleepy_owner_set, "%llu\n");

static int pv_prod_head_set(void *data, u64 val)
{
	pv_prod_head = !!val;

	return 0;
}

static int pv_prod_head_get(void *data, u64 *val)
{
	*val = pv_prod_head;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_pv_prod_head, pv_prod_head_get, pv_prod_head_set, "%llu\n");

static __init int spinlock_debugfs_init(void)
{
	debugfs_create_file("qspl_steal_spins", 0600, arch_debugfs_dir, NULL, &fops_steal_spins);
	debugfs_create_file("qspl_remote_steal_spins", 0600, arch_debugfs_dir, NULL, &fops_remote_steal_spins);
	debugfs_create_file("qspl_head_spins", 0600, arch_debugfs_dir, NULL, &fops_head_spins);
	if (is_shared_processor()) {
		debugfs_create_file("qspl_pv_yield_owner", 0600, arch_debugfs_dir, NULL, &fops_pv_yield_owner);
		debugfs_create_file("qspl_pv_yield_allow_steal", 0600, arch_debugfs_dir, NULL, &fops_pv_yield_allow_steal);
		debugfs_create_file("qspl_pv_spin_on_preempted_owner", 0600, arch_debugfs_dir, NULL, &fops_pv_spin_on_preempted_owner);
		debugfs_create_file("qspl_pv_sleepy_lock", 0600, arch_debugfs_dir, NULL, &fops_pv_sleepy_lock);
		debugfs_create_file("qspl_pv_sleepy_lock_sticky", 0600, arch_debugfs_dir, NULL, &fops_pv_sleepy_lock_sticky);
		debugfs_create_file("qspl_pv_sleepy_lock_interval_ns", 0600, arch_debugfs_dir, NULL, &fops_pv_sleepy_lock_interval_ns);
		debugfs_create_file("qspl_pv_sleepy_lock_factor", 0600, arch_debugfs_dir, NULL, &fops_pv_sleepy_lock_factor);
		debugfs_create_file("qspl_pv_yield_prev", 0600, arch_debugfs_dir, NULL, &fops_pv_yield_prev);
		debugfs_create_file("qspl_pv_yield_sleepy_owner", 0600, arch_debugfs_dir, NULL, &fops_pv_yield_sleepy_owner);
		debugfs_create_file("qspl_pv_prod_head", 0600, arch_debugfs_dir, NULL, &fops_pv_prod_head);
	}

	return 0;
}
device_initcall(spinlock_debugfs_init);
