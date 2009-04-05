#ifndef _ALPHA_SPINLOCK_H
#define _ALPHA_SPINLOCK_H

#include <asm/system.h>
#include <linux/kernel.h>
#include <asm/current.h>

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)
#define __raw_spin_is_locked(x)	((x)->lock != 0)
#define __raw_spin_unlock_wait(x) \
		do { cpu_relax(); } while ((x)->lock)

static inline void __raw_spin_unlock(raw_spinlock_t * lock)
{
	mb();
	lock->lock = 0;
}

static inline void __raw_spin_lock(raw_spinlock_t * lock)
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

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	return !test_and_set_bit(0, &lock->lock);
}

/***********************************************************/

static inline int __raw_read_can_lock(raw_rwlock_t *lock)
{
	return (lock->lock & 1) == 0;
}

static inline int __raw_write_can_lock(raw_rwlock_t *lock)
{
	return lock->lock == 0;
}

static inline void __raw_read_lock(raw_rwlock_t *lock)
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

static inline void __raw_write_lock(raw_rwlock_t *lock)
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

static inline int __raw_read_trylock(raw_rwlock_t * lock)
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

static inline int __raw_write_trylock(raw_rwlock_t * lock)
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

static inline void __raw_read_unlock(raw_rwlock_t * lock)
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

static inline void __raw_write_unlock(raw_rwlock_t * lock)
{
	mb();
	lock->lock = 0;
}

#define __raw_read_lock_flags(lock, flags) __raw_read_lock(lock)
#define __raw_write_lock_flags(lock, flags) __raw_write_lock(lock)

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* _ALPHA_SPINLOCK_H */
