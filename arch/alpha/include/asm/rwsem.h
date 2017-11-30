/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_RWSEM_H
#define _ALPHA_RWSEM_H

/*
 * Written by Ivan Kokshaysky <ink@jurassic.park.msu.ru>, 2001.
 * Based on asm-alpha/semaphore.h and asm-i386/rwsem.h
 */

#ifndef _LINUX_RWSEM_H
#error "please don't include asm/rwsem.h directly, use linux/rwsem.h instead"
#endif

#ifdef __KERNEL__

#include <linux/compiler.h>

#define RWSEM_UNLOCKED_VALUE		0x0000000000000000L
#define RWSEM_ACTIVE_BIAS		0x0000000000000001L
#define RWSEM_ACTIVE_MASK		0x00000000ffffffffL
#define RWSEM_WAITING_BIAS		(-0x0000000100000000L)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)

static inline int ___down_read(struct rw_semaphore *sem)
{
	long oldcount;
#ifndef	CONFIG_SMP
	oldcount = sem->count.counter;
	sem->count.counter += RWSEM_ACTIVE_READ_BIAS;
#else
	long temp;
	__asm__ __volatile__(
	"1:	ldq_l	%0,%1\n"
	"	addq	%0,%3,%2\n"
	"	stq_c	%2,%1\n"
	"	beq	%2,2f\n"
	"	mb\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	:"=&r" (oldcount), "=m" (sem->count), "=&r" (temp)
	:"Ir" (RWSEM_ACTIVE_READ_BIAS), "m" (sem->count) : "memory");
#endif
	return (oldcount < 0);
}

static inline void __down_read(struct rw_semaphore *sem)
{
	if (unlikely(___down_read(sem)))
		rwsem_down_read_failed(sem);
}

static inline int __down_read_killable(struct rw_semaphore *sem)
{
	if (unlikely(___down_read(sem)))
		if (IS_ERR(rwsem_down_read_failed_killable(sem)))
			return -EINTR;

	return 0;
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	long old, new, res;

	res = atomic_long_read(&sem->count);
	do {
		new = res + RWSEM_ACTIVE_READ_BIAS;
		if (new <= 0)
			break;
		old = res;
		res = atomic_long_cmpxchg(&sem->count, old, new);
	} while (res != old);
	return res >= 0 ? 1 : 0;
}

static inline long ___down_write(struct rw_semaphore *sem)
{
	long oldcount;
#ifndef	CONFIG_SMP
	oldcount = sem->count.counter;
	sem->count.counter += RWSEM_ACTIVE_WRITE_BIAS;
#else
	long temp;
	__asm__ __volatile__(
	"1:	ldq_l	%0,%1\n"
	"	addq	%0,%3,%2\n"
	"	stq_c	%2,%1\n"
	"	beq	%2,2f\n"
	"	mb\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	:"=&r" (oldcount), "=m" (sem->count), "=&r" (temp)
	:"Ir" (RWSEM_ACTIVE_WRITE_BIAS), "m" (sem->count) : "memory");
#endif
	return oldcount;
}

static inline void __down_write(struct rw_semaphore *sem)
{
	if (unlikely(___down_write(sem)))
		rwsem_down_write_failed(sem);
}

static inline int __down_write_killable(struct rw_semaphore *sem)
{
	if (unlikely(___down_write(sem))) {
		if (IS_ERR(rwsem_down_write_failed_killable(sem)))
			return -EINTR;
	}

	return 0;
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	long ret = atomic_long_cmpxchg(&sem->count, RWSEM_UNLOCKED_VALUE,
			   RWSEM_ACTIVE_WRITE_BIAS);
	if (ret == RWSEM_UNLOCKED_VALUE)
		return 1;
	return 0;
}

static inline void __up_read(struct rw_semaphore *sem)
{
	long oldcount;
#ifndef	CONFIG_SMP
	oldcount = sem->count.counter;
	sem->count.counter -= RWSEM_ACTIVE_READ_BIAS;
#else
	long temp;
	__asm__ __volatile__(
	"	mb\n"
	"1:	ldq_l	%0,%1\n"
	"	subq	%0,%3,%2\n"
	"	stq_c	%2,%1\n"
	"	beq	%2,2f\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	:"=&r" (oldcount), "=m" (sem->count), "=&r" (temp)
	:"Ir" (RWSEM_ACTIVE_READ_BIAS), "m" (sem->count) : "memory");
#endif
	if (unlikely(oldcount < 0))
		if ((int)oldcount - RWSEM_ACTIVE_READ_BIAS == 0)
			rwsem_wake(sem);
}

static inline void __up_write(struct rw_semaphore *sem)
{
	long count;
#ifndef	CONFIG_SMP
	sem->count.counter -= RWSEM_ACTIVE_WRITE_BIAS;
	count = sem->count.counter;
#else
	long temp;
	__asm__ __volatile__(
	"	mb\n"
	"1:	ldq_l	%0,%1\n"
	"	subq	%0,%3,%2\n"
	"	stq_c	%2,%1\n"
	"	beq	%2,2f\n"
	"	subq	%0,%3,%0\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	:"=&r" (count), "=m" (sem->count), "=&r" (temp)
	:"Ir" (RWSEM_ACTIVE_WRITE_BIAS), "m" (sem->count) : "memory");
#endif
	if (unlikely(count))
		if ((int)count == 0)
			rwsem_wake(sem);
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct rw_semaphore *sem)
{
	long oldcount;
#ifndef	CONFIG_SMP
	oldcount = sem->count.counter;
	sem->count.counter -= RWSEM_WAITING_BIAS;
#else
	long temp;
	__asm__ __volatile__(
	"1:	ldq_l	%0,%1\n"
	"	addq	%0,%3,%2\n"
	"	stq_c	%2,%1\n"
	"	beq	%2,2f\n"
	"	mb\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	:"=&r" (oldcount), "=m" (sem->count), "=&r" (temp)
	:"Ir" (-RWSEM_WAITING_BIAS), "m" (sem->count) : "memory");
#endif
	if (unlikely(oldcount < 0))
		rwsem_downgrade_wake(sem);
}

#endif /* __KERNEL__ */
#endif /* _ALPHA_RWSEM_H */
