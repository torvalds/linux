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

static DEFINE_SPINLOCK(dummy);
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
EXPORT_SYMBOL(__atomic_add_return);

int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(ATOMIC_HASH(v), flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;

	spin_unlock_irqrestore(ATOMIC_HASH(v), flags);
	return ret;
}

int atomic_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(ATOMIC_HASH(v), flags);
	ret = v->counter;
	if (ret != u)
		v->counter += a;
	spin_unlock_irqrestore(ATOMIC_HASH(v), flags);
	return ret != u;
}

/* Atomic operations are already serializing */
void atomic_set(atomic_t *v, int i)
{
	unsigned long flags;

	spin_lock_irqsave(ATOMIC_HASH(v), flags);
	v->counter = i;
	spin_unlock_irqrestore(ATOMIC_HASH(v), flags);
}
EXPORT_SYMBOL(atomic_set);

unsigned long ___set_bit(unsigned long *addr, unsigned long mask)
{
	unsigned long old, flags;

	spin_lock_irqsave(ATOMIC_HASH(addr), flags);
	old = *addr;
	*addr = old | mask;
	spin_unlock_irqrestore(ATOMIC_HASH(addr), flags);

	return old & mask;
}
EXPORT_SYMBOL(___set_bit);

unsigned long ___clear_bit(unsigned long *addr, unsigned long mask)
{
	unsigned long old, flags;

	spin_lock_irqsave(ATOMIC_HASH(addr), flags);
	old = *addr;
	*addr = old & ~mask;
	spin_unlock_irqrestore(ATOMIC_HASH(addr), flags);

	return old & mask;
}
EXPORT_SYMBOL(___clear_bit);

unsigned long ___change_bit(unsigned long *addr, unsigned long mask)
{
	unsigned long old, flags;

	spin_lock_irqsave(ATOMIC_HASH(addr), flags);
	old = *addr;
	*addr = old ^ mask;
	spin_unlock_irqrestore(ATOMIC_HASH(addr), flags);

	return old & mask;
}
EXPORT_SYMBOL(___change_bit);
