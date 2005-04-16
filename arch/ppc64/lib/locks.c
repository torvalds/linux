/*
 * Spin and read/write lock operations.
 *
 * Copyright (C) 2001-2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2002 Dave Engebretsen <engebret@us.ibm.com>, IBM
 *   Rework to support virtual processors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <asm/hvcall.h>
#include <asm/iSeries/HvCall.h>

/* waiting for a spinlock... */
#if defined(CONFIG_PPC_SPLPAR) || defined(CONFIG_PPC_ISERIES)

void __spin_yield(spinlock_t *lock)
{
	unsigned int lock_value, holder_cpu, yield_count;
	struct paca_struct *holder_paca;

	lock_value = lock->lock;
	if (lock_value == 0)
		return;
	holder_cpu = lock_value & 0xffff;
	BUG_ON(holder_cpu >= NR_CPUS);
	holder_paca = &paca[holder_cpu];
	yield_count = holder_paca->lppaca.yield_count;
	if ((yield_count & 1) == 0)
		return;		/* virtual cpu is currently running */
	rmb();
	if (lock->lock != lock_value)
		return;		/* something has changed */
#ifdef CONFIG_PPC_ISERIES
	HvCall2(HvCallBaseYieldProcessor, HvCall_YieldToProc,
		((u64)holder_cpu << 32) | yield_count);
#else
	plpar_hcall_norets(H_CONFER, get_hard_smp_processor_id(holder_cpu),
			   yield_count);
#endif
}

/*
 * Waiting for a read lock or a write lock on a rwlock...
 * This turns out to be the same for read and write locks, since
 * we only know the holder if it is write-locked.
 */
void __rw_yield(rwlock_t *rw)
{
	int lock_value;
	unsigned int holder_cpu, yield_count;
	struct paca_struct *holder_paca;

	lock_value = rw->lock;
	if (lock_value >= 0)
		return;		/* no write lock at present */
	holder_cpu = lock_value & 0xffff;
	BUG_ON(holder_cpu >= NR_CPUS);
	holder_paca = &paca[holder_cpu];
	yield_count = holder_paca->lppaca.yield_count;
	if ((yield_count & 1) == 0)
		return;		/* virtual cpu is currently running */
	rmb();
	if (rw->lock != lock_value)
		return;		/* something has changed */
#ifdef CONFIG_PPC_ISERIES
	HvCall2(HvCallBaseYieldProcessor, HvCall_YieldToProc,
		((u64)holder_cpu << 32) | yield_count);
#else
	plpar_hcall_norets(H_CONFER, get_hard_smp_processor_id(holder_cpu),
			   yield_count);
#endif
}
#endif

void spin_unlock_wait(spinlock_t *lock)
{
	while (lock->lock) {
		HMT_low();
		if (SHARED_PROCESSOR)
			__spin_yield(lock);
	}
	HMT_medium();
}

EXPORT_SYMBOL(spin_unlock_wait);
