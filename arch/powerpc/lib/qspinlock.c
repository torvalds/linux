// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/export.h>
#include <linux/processor.h>
#include <asm/qspinlock.h>

void queued_spin_lock_slowpath(struct qspinlock *lock)
{
	while (!queued_spin_trylock(lock))
		cpu_relax();
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void pv_spinlocks_init(void)
{
}
#endif
