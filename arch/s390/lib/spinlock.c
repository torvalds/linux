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
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/io.h>

int spin_retry = -1;

static int __init spin_retry_init(void)
{
	if (spin_retry < 0)
		spin_retry = 1000;
	return 0;
}
early_initcall(spin_retry_init);

/**
 * spin_retry= parameter
 */
static int __init spin_retry_setup(char *str)
{
	spin_retry = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("spin_retry=", spin_retry_setup);

static inline int arch_load_niai4(int *lock)
{
	int owner;

	asm volatile(
#ifdef CONFIG_HAVE_MARCH_ZEC12_FEATURES
		"	.long	0xb2fa0040\n"	/* NIAI 4 */
#endif
		"	l	%0,%1\n"
		: "=d" (owner) : "Q" (*lock) : "memory");
       return owner;
}

static inline int arch_cmpxchg_niai8(int *lock, int old, int new)
{
	int expected = old;

	asm volatile(
#ifdef CONFIG_HAVE_MARCH_ZEC12_FEATURES
		"	.long	0xb2fa0080\n"	/* NIAI 8 */
#endif
		"	cs	%0,%3,%1\n"
		: "=d" (old), "=Q" (*lock)
		: "0" (old), "d" (new), "Q" (*lock)
		: "cc", "memory");
	return expected == old;
}

void arch_spin_lock_wait(arch_spinlock_t *lp)
{
	int cpu = SPINLOCK_LOCKVAL;
	int owner, count;

	/* Pass the virtual CPU to the lock holder if it is not running */
	owner = arch_load_niai4(&lp->lock);
	if (owner && arch_vcpu_is_preempted(~owner))
		smp_yield_cpu(~owner);

	count = spin_retry;
	while (1) {
		owner = arch_load_niai4(&lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			if (arch_cmpxchg_niai8(&lp->lock, 0, cpu))
				return;
			continue;
		}
		if (count-- >= 0)
			continue;
		count = spin_retry;
		/*
		 * For multiple layers of hypervisors, e.g. z/VM + LPAR
		 * yield the CPU unconditionally. For LPAR rely on the
		 * sense running status.
		 */
		if (!MACHINE_IS_LPAR || arch_vcpu_is_preempted(~owner))
			smp_yield_cpu(~owner);
	}
}
EXPORT_SYMBOL(arch_spin_lock_wait);

void arch_spin_lock_wait_flags(arch_spinlock_t *lp, unsigned long flags)
{
	int cpu = SPINLOCK_LOCKVAL;
	int owner, count;

	local_irq_restore(flags);

	/* Pass the virtual CPU to the lock holder if it is not running */
	owner = arch_load_niai4(&lp->lock);
	if (owner && arch_vcpu_is_preempted(~owner))
		smp_yield_cpu(~owner);

	count = spin_retry;
	while (1) {
		owner = arch_load_niai4(&lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			local_irq_disable();
			if (arch_cmpxchg_niai8(&lp->lock, 0, cpu))
				return;
			local_irq_restore(flags);
			continue;
		}
		if (count-- >= 0)
			continue;
		count = spin_retry;
		/*
		 * For multiple layers of hypervisors, e.g. z/VM + LPAR
		 * yield the CPU unconditionally. For LPAR rely on the
		 * sense running status.
		 */
		if (!MACHINE_IS_LPAR || arch_vcpu_is_preempted(~owner))
			smp_yield_cpu(~owner);
	}
}
EXPORT_SYMBOL(arch_spin_lock_wait_flags);

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

void _raw_read_lock_wait(arch_rwlock_t *rw)
{
	int count = spin_retry;
	int owner, old;

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	__RAW_LOCK(&rw->lock, -1, __RAW_OP_ADD);
#endif
	owner = 0;
	while (1) {
		if (count-- <= 0) {
			if (owner && arch_vcpu_is_preempted(~owner))
				smp_yield_cpu(~owner);
			count = spin_retry;
		}
		old = ACCESS_ONCE(rw->lock);
		owner = ACCESS_ONCE(rw->owner);
		if (old < 0)
			continue;
		if (__atomic_cmpxchg_bool(&rw->lock, old, old + 1))
			return;
	}
}
EXPORT_SYMBOL(_raw_read_lock_wait);

int _raw_read_trylock_retry(arch_rwlock_t *rw)
{
	int count = spin_retry;
	int old;

	while (count-- > 0) {
		old = ACCESS_ONCE(rw->lock);
		if (old < 0)
			continue;
		if (__atomic_cmpxchg_bool(&rw->lock, old, old + 1))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_read_trylock_retry);

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

void _raw_write_lock_wait(arch_rwlock_t *rw, int prev)
{
	int count = spin_retry;
	int owner, old;

	owner = 0;
	while (1) {
		if (count-- <= 0) {
			if (owner && arch_vcpu_is_preempted(~owner))
				smp_yield_cpu(~owner);
			count = spin_retry;
		}
		old = ACCESS_ONCE(rw->lock);
		owner = ACCESS_ONCE(rw->owner);
		smp_mb();
		if (old >= 0) {
			prev = __RAW_LOCK(&rw->lock, 0x80000000, __RAW_OP_OR);
			old = prev;
		}
		if ((old & 0x7fffffff) == 0 && prev >= 0)
			break;
	}
}
EXPORT_SYMBOL(_raw_write_lock_wait);

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

void _raw_write_lock_wait(arch_rwlock_t *rw)
{
	int count = spin_retry;
	int owner, old, prev;

	prev = 0x80000000;
	owner = 0;
	while (1) {
		if (count-- <= 0) {
			if (owner && arch_vcpu_is_preempted(~owner))
				smp_yield_cpu(~owner);
			count = spin_retry;
		}
		old = ACCESS_ONCE(rw->lock);
		owner = ACCESS_ONCE(rw->owner);
		if (old >= 0 &&
		    __atomic_cmpxchg_bool(&rw->lock, old, old | 0x80000000))
			prev = old;
		else
			smp_mb();
		if ((old & 0x7fffffff) == 0 && prev >= 0)
			break;
	}
}
EXPORT_SYMBOL(_raw_write_lock_wait);

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

int _raw_write_trylock_retry(arch_rwlock_t *rw)
{
	int count = spin_retry;
	int old;

	while (count-- > 0) {
		old = ACCESS_ONCE(rw->lock);
		if (old)
			continue;
		if (__atomic_cmpxchg_bool(&rw->lock, 0, 0x80000000))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_write_trylock_retry);

void arch_lock_relax(int cpu)
{
	if (!cpu)
		return;
	if (MACHINE_IS_LPAR && !arch_vcpu_is_preempted(~cpu))
		return;
	smp_yield_cpu(~cpu);
}
EXPORT_SYMBOL(arch_lock_relax);
