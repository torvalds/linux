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
#include <linux/sysctl.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/io.h>
#include <asm/alternative.h>
#include <asm/machine.h>
#include <asm/asm.h>

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

static const struct ctl_table s390_spin_sysctl_table[] = {
	{
		.procname	= "spin_retry",
		.data		= &spin_retry,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
};

static int __init init_s390_spin_sysctls(void)
{
	register_sysctl_init("kernel", s390_spin_sysctl_table);
	return 0;
}
arch_initcall(init_s390_spin_sysctls);

struct spin_wait {
	struct spin_wait *next, *prev;
	int node_id;
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
	struct spin_wait *node;
	int ix;

	node = per_cpu_ptr(&spin_wait[0], cpu);
	for (ix = 0; ix < 4; ix++, node++) {
		memset(node, 0, sizeof(*node));
		node->node_id = ((cpu + 1) << _Q_TAIL_CPU_OFFSET) +
			(ix << _Q_TAIL_IDX_OFFSET);
	}
}

static inline int arch_load_niai4(int *lock)
{
	int owner;

	asm_inline volatile(
		ALTERNATIVE("nop", ".insn rre,0xb2fa0000,4,0", ALT_FACILITY(49)) /* NIAI 4 */
		"	l	%[owner],%[lock]\n"
		: [owner] "=d" (owner) : [lock] "R" (*lock) : "memory");
	return owner;
}

#ifdef __HAVE_ASM_FLAG_OUTPUTS__

static inline int arch_try_cmpxchg_niai8(int *lock, int old, int new)
{
	int cc;

	asm_inline volatile(
		ALTERNATIVE("nop", ".insn rre,0xb2fa0000,8,0", ALT_FACILITY(49)) /* NIAI 8 */
		"	cs	%[old],%[new],%[lock]\n"
		: [old] "+d" (old), [lock] "+Q" (*lock), "=@cc" (cc)
		: [new] "d" (new)
		: "memory");
	return cc == 0;
}

#else /* __HAVE_ASM_FLAG_OUTPUTS__ */

static inline int arch_try_cmpxchg_niai8(int *lock, int old, int new)
{
	int expected = old;

	asm_inline volatile(
		ALTERNATIVE("nop", ".insn rre,0xb2fa0000,8,0", ALT_FACILITY(49)) /* NIAI 8 */
		"	cs	%[old],%[new],%[lock]\n"
		: [old] "+d" (old), [lock] "+Q" (*lock)
		: [new] "d" (new)
		: "cc", "memory");
	return expected == old;
}

#endif /* __HAVE_ASM_FLAG_OUTPUTS__ */

static inline struct spin_wait *arch_spin_decode_tail(int lock)
{
	int ix, cpu;

	ix = (lock & _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;
	cpu = (lock & _Q_TAIL_CPU_MASK) >> _Q_TAIL_CPU_OFFSET;
	return per_cpu_ptr(&spin_wait[ix], cpu - 1);
}

static inline int arch_spin_yield_target(int lock, struct spin_wait *node)
{
	if (lock & _Q_LOCK_CPU_MASK)
		return lock & _Q_LOCK_CPU_MASK;
	if (node == NULL || node->prev == NULL)
		return 0;	/* 0 -> no target cpu */
	while (node->prev)
		node = node->prev;
	return node->node_id >> _Q_TAIL_CPU_OFFSET;
}

static inline void arch_spin_lock_queued(arch_spinlock_t *lp)
{
	struct spin_wait *node, *next;
	int lockval, ix, node_id, tail_id, old, new, owner, count;

	ix = get_lowcore()->spinlock_index++;
	barrier();
	lockval = spinlock_lockval();	/* cpu + 1 */
	node = this_cpu_ptr(&spin_wait[ix]);
	node->prev = node->next = NULL;
	node_id = node->node_id;

	/* Enqueue the node for this CPU in the spinlock wait queue */
	old = READ_ONCE(lp->lock);
	while (1) {
		if ((old & _Q_LOCK_CPU_MASK) == 0 &&
		    (old & _Q_LOCK_STEAL_MASK) != _Q_LOCK_STEAL_MASK) {
			/*
			 * The lock is free but there may be waiters.
			 * With no waiters simply take the lock, if there
			 * are waiters try to steal the lock. The lock may
			 * be stolen three times before the next queued
			 * waiter will get the lock.
			 */
			new = (old ? (old + _Q_LOCK_STEAL_ADD) : 0) | lockval;
			if (arch_try_cmpxchg(&lp->lock, &old, new))
				/* Got the lock */
				goto out;
			/* lock passing in progress */
			continue;
		}
		/* Make the node of this CPU the new tail. */
		new = node_id | (old & _Q_LOCK_MASK);
		if (arch_try_cmpxchg(&lp->lock, &old, new))
			break;
	}
	/* Set the 'next' pointer of the tail node in the queue */
	tail_id = old & _Q_TAIL_MASK;
	if (tail_id != 0) {
		node->prev = arch_spin_decode_tail(tail_id);
		WRITE_ONCE(node->prev->next, node);
	}

	/* Pass the virtual CPU to the lock holder if it is not running */
	owner = arch_spin_yield_target(old, node);
	if (owner && arch_vcpu_is_preempted(owner - 1))
		smp_yield_cpu(owner - 1);

	/* Spin on the CPU local node->prev pointer */
	if (tail_id != 0) {
		count = spin_retry;
		while (READ_ONCE(node->prev) != NULL) {
			if (count-- >= 0)
				continue;
			count = spin_retry;
			/* Query running state of lock holder again. */
			owner = arch_spin_yield_target(old, node);
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
			new = ((tail_id != node_id) ? tail_id : 0) | lockval;
			if (arch_try_cmpxchg(&lp->lock, &old, new))
				/* Got the lock */
				break;
			continue;
		}
		if (count-- >= 0)
			continue;
		count = spin_retry;
		if (!machine_is_lpar() || arch_vcpu_is_preempted(owner - 1))
			smp_yield_cpu(owner - 1);
	}

	/* Pass lock_spin job to next CPU in the queue */
	if (node_id && tail_id != node_id) {
		/* Wait until the next CPU has set up the 'next' pointer */
		while ((next = READ_ONCE(node->next)) == NULL)
			;
		next->prev = NULL;
	}

 out:
	get_lowcore()->spinlock_index--;
}

static inline void arch_spin_lock_classic(arch_spinlock_t *lp)
{
	int lockval, old, new, owner, count;

	lockval = spinlock_lockval();	/* cpu + 1 */

	/* Pass the virtual CPU to the lock holder if it is not running */
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
			if (arch_try_cmpxchg_niai8(&lp->lock, old, new)) {
				/* Got the lock */
				return;
			}
			continue;
		}
		if (count-- >= 0)
			continue;
		count = spin_retry;
		if (!machine_is_lpar() || arch_vcpu_is_preempted(owner - 1))
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
	int cpu = spinlock_lockval();
	int owner, count;

	for (count = spin_retry; count > 0; count--) {
		owner = READ_ONCE(lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			if (arch_try_cmpxchg(&lp->lock, &owner, cpu))
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
	/* Now add this reader to the count value again */
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
		    arch_try_cmpxchg(&rw->cnts, &old, old | 0x10000))
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
	if (machine_is_lpar() && !arch_vcpu_is_preempted(cpu - 1))
		return;
	smp_yield_cpu(cpu - 1);
}
EXPORT_SYMBOL(arch_spin_relax);
