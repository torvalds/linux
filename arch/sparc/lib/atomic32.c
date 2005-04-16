/*
 * atomic32.c: 32-bit atomic_t implementation
 *
 * Copyright (C) 2004 Keith M Wesolowski
 * 
 * Based on asm-parisc/atomic.h Copyright (C) 2000 Philipp Rumpf
 */

#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#ifdef CONFIG_SMP
#define ATOMIC_HASH_SIZE	4
#define ATOMIC_HASH(a)	(&__atomic_hash[(((unsigned long)a)>>8) & (ATOMIC_HASH_SIZE-1)])

spinlock_t __atomic_hash[ATOMIC_HASH_SIZE] = {
	[0 ... (ATOMIC_HASH_SIZE-1)] = SPIN_LOCK_UNLOCKED
};

#else /* SMP */

static spinlock_t dummy = SPIN_LOCK_UNLOCKED;
#define ATOMIC_HASH_SIZE	1
#define ATOMIC_HASH(a)		(&dummy)

#endif /* SMP */

int __atomic_add_return(int i, atomic_t *v)
{
	int ret;
	unsigned long flags;
	spin_lock_irqsave(ATOMIC_HASH(v), flags);

	ret = (v->counter += i);

	spin_unlock_irqrestore(ATOMIC_HASH(v), flags);
	return ret;
}

void atomic_set(atomic_t *v, int i)
{
	unsigned long flags;
	spin_lock_irqsave(ATOMIC_HASH(v), flags);

	v->counter = i;

	spin_unlock_irqrestore(ATOMIC_HASH(v), flags);
}

EXPORT_SYMBOL(__atomic_add_return);
EXPORT_SYMBOL(atomic_set);

