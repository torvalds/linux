/*
 *  arch/s390/lib/spinlock.c
 *    Out of line spinlock code.
 *
 *    Copyright (C) IBM Corp. 2004, 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <asm/io.h>

int spin_retry = 1000;

/**
 * spin_retry= parameter
 */
static int __init spin_retry_setup(char *str)
{
	spin_retry = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("spin_retry=", spin_retry_setup);

static inline void _raw_yield(void)
{
	if (MACHINE_HAS_DIAG44)
		asm volatile("diag 0,0,0x44");
}

static inline void _raw_yield_cpu(int cpu)
{
	if (MACHINE_HAS_DIAG9C)
		asm volatile("diag %0,0,0x9c"
			     : : "d" (__cpu_logical_map[cpu]));
	else
		_raw_yield();
}

void _raw_spin_lock_wait(raw_spinlock_t *lp, unsigned int pc)
{
	int count = spin_retry;
	unsigned int cpu = ~smp_processor_id();

	while (1) {
		if (count-- <= 0) {
			unsigned int owner = lp->owner_cpu;
			if (owner != 0)
				_raw_yield_cpu(~owner);
			count = spin_retry;
		}
		if (__raw_spin_is_locked(lp))
			continue;
		if (_raw_compare_and_swap(&lp->owner_cpu, 0, cpu) == 0) {
			lp->owner_pc = pc;
			return;
		}
	}
}
EXPORT_SYMBOL(_raw_spin_lock_wait);

int _raw_spin_trylock_retry(raw_spinlock_t *lp, unsigned int pc)
{
	unsigned int cpu = ~smp_processor_id();
	int count;

	for (count = spin_retry; count > 0; count--) {
		if (__raw_spin_is_locked(lp))
			continue;
		if (_raw_compare_and_swap(&lp->owner_cpu, 0, cpu) == 0) {
			lp->owner_pc = pc;
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(_raw_spin_trylock_retry);

void _raw_spin_relax(raw_spinlock_t *lock)
{
	unsigned int cpu = lock->owner_cpu;
	if (cpu != 0)
		_raw_yield_cpu(~cpu);
}
EXPORT_SYMBOL(_raw_spin_relax);

void _raw_read_lock_wait(raw_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_raw_yield();
			count = spin_retry;
		}
		if (!__raw_read_can_lock(rw))
			continue;
		old = rw->lock & 0x7fffffffU;
		if (_raw_compare_and_swap(&rw->lock, old, old + 1) == old)
			return;
	}
}
EXPORT_SYMBOL(_raw_read_lock_wait);

int _raw_read_trylock_retry(raw_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (count-- > 0) {
		if (!__raw_read_can_lock(rw))
			continue;
		old = rw->lock & 0x7fffffffU;
		if (_raw_compare_and_swap(&rw->lock, old, old + 1) == old)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_read_trylock_retry);

void _raw_write_lock_wait(raw_rwlock_t *rw)
{
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_raw_yield();
			count = spin_retry;
		}
		if (!__raw_write_can_lock(rw))
			continue;
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0)
			return;
	}
}
EXPORT_SYMBOL(_raw_write_lock_wait);

int _raw_write_trylock_retry(raw_rwlock_t *rw)
{
	int count = spin_retry;

	while (count-- > 0) {
		if (!__raw_write_can_lock(rw))
			continue;
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_write_trylock_retry);
