/*
 *  arch/s390/lib/spinlock.c
 *    Out of line spinlock code.
 *
 *  S390 version
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
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

static inline void
_diag44(void)
{
#ifdef CONFIG_64BIT
	if (MACHINE_HAS_DIAG44)
#endif
		asm volatile("diag 0,0,0x44");
}

void
_raw_spin_lock_wait(raw_spinlock_t *lp, unsigned int pc)
{
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_diag44();
			count = spin_retry;
		}
		if (_raw_compare_and_swap(&lp->lock, 0, pc) == 0)
			return;
	}
}
EXPORT_SYMBOL(_raw_spin_lock_wait);

int
_raw_spin_trylock_retry(raw_spinlock_t *lp, unsigned int pc)
{
	int count = spin_retry;

	while (count-- > 0) {
		if (_raw_compare_and_swap(&lp->lock, 0, pc) == 0)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_spin_trylock_retry);

void
_raw_read_lock_wait(raw_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_diag44();
			count = spin_retry;
		}
		old = rw->lock & 0x7fffffffU;
		if (_raw_compare_and_swap(&rw->lock, old, old + 1) == old)
			return;
	}
}
EXPORT_SYMBOL(_raw_read_lock_wait);

int
_raw_read_trylock_retry(raw_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (count-- > 0) {
		old = rw->lock & 0x7fffffffU;
		if (_raw_compare_and_swap(&rw->lock, old, old + 1) == old)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_read_trylock_retry);

void
_raw_write_lock_wait(raw_rwlock_t *rw)
{
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_diag44();
			count = spin_retry;
		}
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0)
			return;
	}
}
EXPORT_SYMBOL(_raw_write_lock_wait);

int
_raw_write_trylock_retry(raw_rwlock_t *rw)
{
	int count = spin_retry;

	while (count-- > 0) {
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_write_trylock_retry);
