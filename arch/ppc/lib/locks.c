/*
 * Locks for smp ppc
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/ppc_asm.h>
#include <asm/smp.h>

#ifdef CONFIG_DEBUG_SPINLOCK

#undef INIT_STUCK
#define INIT_STUCK 200000000 /*0xffffffff*/

/*
 * Try to acquire a spinlock.
 * Only does the stwcx. if the load returned 0 - the Programming
 * Environments Manual suggests not doing unnecessary stcwx.'s
 * since they may inhibit forward progress by other CPUs in getting
 * a lock.
 */
static inline unsigned long __spin_trylock(volatile unsigned long *lock)
{
	unsigned long ret;

	__asm__ __volatile__ ("\n\
1:	lwarx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne	2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%2,0,%1\n\
	bne-	1b\n\
	isync\n\
2:"
	: "=&r"(ret)
	: "r"(lock), "r"(1)
	: "cr0", "memory");

	return ret;
}

void _raw_spin_lock(spinlock_t *lock)
{
	int cpu = smp_processor_id();
	unsigned int stuck = INIT_STUCK;
	while (__spin_trylock(&lock->lock)) {
		while ((unsigned volatile long)lock->lock != 0) {
			if (!--stuck) {
				printk("_spin_lock(%p) CPU#%d NIP %p"
				       " holder: cpu %ld pc %08lX\n",
				       lock, cpu, __builtin_return_address(0),
				       lock->owner_cpu,lock->owner_pc);
				stuck = INIT_STUCK;
				/* steal the lock */
				/*xchg_u32((void *)&lock->lock,0);*/
			}
		}
	}
	lock->owner_pc = (unsigned long)__builtin_return_address(0);
	lock->owner_cpu = cpu;
}
EXPORT_SYMBOL(_raw_spin_lock);

int _raw_spin_trylock(spinlock_t *lock)
{
	if (__spin_trylock(&lock->lock))
		return 0;
	lock->owner_cpu = smp_processor_id();
	lock->owner_pc = (unsigned long)__builtin_return_address(0);
	return 1;
}
EXPORT_SYMBOL(_raw_spin_trylock);

void _raw_spin_unlock(spinlock_t *lp)
{
  	if ( !lp->lock )
		printk("_spin_unlock(%p): no lock cpu %d curr PC %p %s/%d\n",
		       lp, smp_processor_id(), __builtin_return_address(0),
		       current->comm, current->pid);
	if ( lp->owner_cpu != smp_processor_id() )
		printk("_spin_unlock(%p): cpu %d trying clear of cpu %d pc %lx val %lx\n",
		      lp, smp_processor_id(), (int)lp->owner_cpu,
		      lp->owner_pc,lp->lock);
	lp->owner_pc = lp->owner_cpu = 0;
	wmb();
	lp->lock = 0;
}
EXPORT_SYMBOL(_raw_spin_unlock);

/*
 * For rwlocks, zero is unlocked, -1 is write-locked,
 * positive is read-locked.
 */
static __inline__ int __read_trylock(rwlock_t *rw)
{
	signed int tmp;

	__asm__ __volatile__(
"2:	lwarx	%0,0,%1		# __read_trylock\n\
	addic.	%0,%0,1\n\
	ble-	1f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	2b\n\
	isync\n\
1:"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");

	return tmp;
}

int _raw_read_trylock(rwlock_t *rw)
{
	return __read_trylock(rw) > 0;
}
EXPORT_SYMBOL(_raw_read_trylock);

void _raw_read_lock(rwlock_t *rw)
{
	unsigned int stuck;

	while (__read_trylock(rw) <= 0) {
		stuck = INIT_STUCK;
		while (!read_can_lock(rw)) {
			if (--stuck == 0) {
				printk("_read_lock(%p) CPU#%d lock %d\n",
				       rw, _smp_processor_id(), rw->lock);
				stuck = INIT_STUCK;
			}
		}
	}
}
EXPORT_SYMBOL(_raw_read_lock);

void _raw_read_unlock(rwlock_t *rw)
{
	if ( rw->lock == 0 )
		printk("_read_unlock(): %s/%d (nip %08lX) lock %d\n",
		       current->comm,current->pid,current->thread.regs->nip,
		      rw->lock);
	wmb();
	atomic_dec((atomic_t *) &(rw)->lock);
}
EXPORT_SYMBOL(_raw_read_unlock);

void _raw_write_lock(rwlock_t *rw)
{
	unsigned int stuck;

	while (cmpxchg(&rw->lock, 0, -1) != 0) {
		stuck = INIT_STUCK;
		while (!write_can_lock(rw)) {
			if (--stuck == 0) {
				printk("write_lock(%p) CPU#%d lock %d)\n",
				       rw, _smp_processor_id(), rw->lock);
				stuck = INIT_STUCK;
			}
		}
	}
	wmb();
}
EXPORT_SYMBOL(_raw_write_lock);

int _raw_write_trylock(rwlock_t *rw)
{
	if (cmpxchg(&rw->lock, 0, -1) != 0)
		return 0;
	wmb();
	return 1;
}
EXPORT_SYMBOL(_raw_write_trylock);

void _raw_write_unlock(rwlock_t *rw)
{
	if (rw->lock >= 0)
		printk("_write_lock(): %s/%d (nip %08lX) lock %d\n",
		      current->comm,current->pid,current->thread.regs->nip,
		      rw->lock);
	wmb();
	rw->lock = 0;
}
EXPORT_SYMBOL(_raw_write_unlock);

#endif
