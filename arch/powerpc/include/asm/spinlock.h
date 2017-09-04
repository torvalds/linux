#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H
#ifdef __KERNEL__

/*
 * Simple spin lock operations.  
 *
 * Copyright (C) 2001-2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2002 Dave Engebretsen <engebret@us.ibm.com>, IBM
 *	Rework to support virtual processors
 *
 * Type of int is used as a full 64b word is not necessary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */
#include <linux/irqflags.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/hvcall.h>
#endif
#include <asm/asm-compat.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>

#ifdef CONFIG_PPC64
/* use 0x800000yy when locked, where yy == CPU number */
#ifdef __BIG_ENDIAN__
#define LOCK_TOKEN	(*(u32 *)(&get_paca()->lock_token))
#else
#define LOCK_TOKEN	(*(u32 *)(&get_paca()->paca_index))
#endif
#else
#define LOCK_TOKEN	1
#endif

#if defined(CONFIG_PPC64) && defined(CONFIG_SMP)
#define CLEAR_IO_SYNC	(get_paca()->io_sync = 0)
#define SYNC_IO		do {						\
				if (unlikely(get_paca()->io_sync)) {	\
					mb();				\
					get_paca()->io_sync = 0;	\
				}					\
			} while (0)
#else
#define CLEAR_IO_SYNC
#define SYNC_IO
#endif

#ifdef CONFIG_PPC_PSERIES
#define vcpu_is_preempted vcpu_is_preempted
static inline bool vcpu_is_preempted(int cpu)
{
	return !!(be32_to_cpu(lppaca_of(cpu).yield_count) & 1);
}
#endif

static __always_inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.slock == 0;
}

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	smp_mb();
	return !arch_spin_value_unlocked(*lock);
}

/*
 * This returns the old value in the lock, so we succeeded
 * in getting the lock if the return value is 0.
 */
static inline unsigned long __arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long tmp, token;

	token = LOCK_TOKEN;
	__asm__ __volatile__(
"1:	" PPC_LWARX(%0,0,%2,1) "\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n"
	PPC_ACQUIRE_BARRIER
"2:"
	: "=&r" (tmp)
	: "r" (token), "r" (&lock->slock)
	: "cr0", "memory");

	return tmp;
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	CLEAR_IO_SYNC;
	return __arch_spin_trylock(lock) == 0;
}

/*
 * On a system with shared processors (that is, where a physical
 * processor is multiplexed between several virtual processors),
 * there is no point spinning on a lock if the holder of the lock
 * isn't currently scheduled on a physical processor.  Instead
 * we detect this situation and ask the hypervisor to give the
 * rest of our timeslice to the lock holder.
 *
 * So that we can tell which virtual processor is holding a lock,
 * we put 0x80000000 | smp_processor_id() in the lock when it is
 * held.  Conveniently, we have a word in the paca that holds this
 * value.
 */

#if defined(CONFIG_PPC_SPLPAR)
/* We only yield to the hypervisor if we are in shared processor mode */
#define SHARED_PROCESSOR (lppaca_shared_proc(local_paca->lppaca_ptr))
extern void __spin_yield(arch_spinlock_t *lock);
extern void __rw_yield(arch_rwlock_t *lock);
#else /* SPLPAR */
#define __spin_yield(x)	barrier()
#define __rw_yield(x)	barrier()
#define SHARED_PROCESSOR	0
#endif

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	CLEAR_IO_SYNC;
	while (1) {
		if (likely(__arch_spin_trylock(lock) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (unlikely(lock->slock != 0));
		HMT_medium();
	}
}

static inline
void arch_spin_lock_flags(arch_spinlock_t *lock, unsigned long flags)
{
	unsigned long flags_dis;

	CLEAR_IO_SYNC;
	while (1) {
		if (likely(__arch_spin_trylock(lock) == 0))
			break;
		local_save_flags(flags_dis);
		local_irq_restore(flags);
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (unlikely(lock->slock != 0));
		HMT_medium();
		local_irq_restore(flags_dis);
	}
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	SYNC_IO;
	__asm__ __volatile__("# arch_spin_unlock\n\t"
				PPC_RELEASE_BARRIER: : :"memory");
	lock->slock = 0;
}

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

#define arch_read_can_lock(rw)		((rw)->lock >= 0)
#define arch_write_can_lock(rw)	(!(rw)->lock)

#ifdef CONFIG_PPC64
#define __DO_SIGN_EXTEND	"extsw	%0,%0\n"
#define WRLOCK_TOKEN		LOCK_TOKEN	/* it's negative */
#else
#define __DO_SIGN_EXTEND
#define WRLOCK_TOKEN		(-1)
#endif

/*
 * This returns the old value in the lock + 1,
 * so we got a read lock if the return value is > 0.
 */
static inline long __arch_read_trylock(arch_rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
"1:	" PPC_LWARX(%0,0,%1,1) "\n"
	__DO_SIGN_EXTEND
"	addic.		%0,%0,1\n\
	ble-		2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.		%0,0,%1\n\
	bne-		1b\n"
	PPC_ACQUIRE_BARRIER
"2:"	: "=&r" (tmp)
	: "r" (&rw->lock)
	: "cr0", "xer", "memory");

	return tmp;
}

/*
 * This returns the old value in the lock,
 * so we got the write lock if the return value is 0.
 */
static inline long __arch_write_trylock(arch_rwlock_t *rw)
{
	long tmp, token;

	token = WRLOCK_TOKEN;
	__asm__ __volatile__(
"1:	" PPC_LWARX(%0,0,%2,1) "\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.		%1,0,%2\n\
	bne-		1b\n"
	PPC_ACQUIRE_BARRIER
"2:"	: "=&r" (tmp)
	: "r" (token), "r" (&rw->lock)
	: "cr0", "memory");

	return tmp;
}

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	while (1) {
		if (likely(__arch_read_trylock(rw) > 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (unlikely(rw->lock < 0));
		HMT_medium();
	}
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	while (1) {
		if (likely(__arch_write_trylock(rw) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (unlikely(rw->lock != 0));
		HMT_medium();
	}
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	return __arch_read_trylock(rw) > 0;
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	return __arch_write_trylock(rw) == 0;
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
	"# read_unlock\n\t"
	PPC_RELEASE_BARRIER
"1:	lwarx		%0,0,%1\n\
	addic		%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.		%0,0,%1\n\
	bne-		1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "xer", "memory");
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	__asm__ __volatile__("# write_unlock\n\t"
				PPC_RELEASE_BARRIER: : :"memory");
	rw->lock = 0;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	__spin_yield(lock)
#define arch_read_relax(lock)	__rw_yield(lock)
#define arch_write_relax(lock)	__rw_yield(lock)

#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
