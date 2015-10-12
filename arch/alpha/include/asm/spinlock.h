#ifndef _ALPHA_SPINLOCK_H
#define _ALPHA_SPINLOCK_H

#include <linux/kernel.h>
#include <asm/current.h>

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)
#define arch_spin_is_locked(x)	((x)->lock != 0)
#define arch_spin_unlock_wait(x) \
		do { cpu_relax(); } while ((x)->lock)

static inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
        return lock.lock == 0;
}

static inline void arch_spin_unlock(arch_spinlock_t * lock)
{
	mb();
	lock->lock = 0;
}

static inline void arch_spin_lock(arch_spinlock_t * lock)
{
	long tmp;

	__asm__ __volatile__(
	"1:	ldl_l	%0,%1\n"
	"	bne	%0,2f\n"
	"	lda	%0,1\n"
	"	stl_c	%0,%1\n"
	"	beq	%0,2f\n"
	"	mb\n"
	".subsection 2\n"
	"2:	ldl	%0,%1\n"
	"	bne	%0,2b\n"
	"	br	1b\n"
	".previous"
	: "=&r" (tmp), "=m" (lock->lock)
	: "m"(lock->lock) : "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	return !test_and_set_bit(0, &lock->lock);
}

/***********************************************************/

static inline int arch_read_can_lock(arch_rwlock_t *lock)
{
	return (lock->lock & 1) == 0;
}

static inline int arch_write_can_lock(arch_rwlock_t *lock)
{
	return lock->lock == 0;
}

static inline void arch_read_lock(arch_rwlock_t *lock)
{
	long regx;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	blbs	%1,6f\n"
	"	subl	%1,2,%1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	"	mb\n"
	".subsection 2\n"
	"6:	ldl	%1,%0\n"
	"	blbs	%1,6b\n"
	"	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx)
	: "m" (*lock) : "memory");
}

static inline void arch_write_lock(arch_rwlock_t *lock)
{
	long regx;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	bne	%1,6f\n"
	"	lda	%1,1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	"	mb\n"
	".subsection 2\n"
	"6:	ldl	%1,%0\n"
	"	bne	%1,6b\n"
	"	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx)
	: "m" (*lock) : "memory");
}

static inline int arch_read_trylock(arch_rwlock_t * lock)
{
	long regx;
	int success;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	lda	%2,0\n"
	"	blbs	%1,2f\n"
	"	subl	%1,2,%2\n"
	"	stl_c	%2,%0\n"
	"	beq	%2,6f\n"
	"2:	mb\n"
	".subsection 2\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx), "=&r" (success)
	: "m" (*lock) : "memory");

	return success;
}

static inline int arch_write_trylock(arch_rwlock_t * lock)
{
	long regx;
	int success;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	lda	%2,0\n"
	"	bne	%1,2f\n"
	"	lda	%2,1\n"
	"	stl_c	%2,%0\n"
	"	beq	%2,6f\n"
	"2:	mb\n"
	".subsection 2\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx), "=&r" (success)
	: "m" (*lock) : "memory");

	return success;
}

static inline void arch_read_unlock(arch_rwlock_t * lock)
{
	long regx;
	__asm__ __volatile__(
	"	mb\n"
	"1:	ldl_l	%1,%0\n"
	"	addl	%1,2,%1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	".subsection 2\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx)
	: "m" (*lock) : "memory");
}

static inline void arch_write_unlock(arch_rwlock_t * lock)
{
	mb();
	lock->lock = 0;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#endif /* _ALPHA_SPINLOCK_H */
