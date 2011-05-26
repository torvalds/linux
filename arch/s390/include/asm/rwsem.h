#ifndef _S390_RWSEM_H
#define _S390_RWSEM_H

/*
 *  include/asm-s390/rwsem.h
 *
 *  S390 version
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Based on asm-alpha/semaphore.h and asm-i386/rwsem.h
 */

/*
 *
 * The MSW of the count is the negated number of active writers and waiting
 * lockers, and the LSW is the total number of active locks
 *
 * The lock count is initialized to 0 (no active and no waiting lockers).
 *
 * When a writer subtracts WRITE_BIAS, it'll get 0xffff0001 for the case of an
 * uncontended lock. This can be determined because XADD returns the old value.
 * Readers increment by 1 and see a positive value when uncontended, negative
 * if there are writers (and maybe) readers waiting (in which case it goes to
 * sleep).
 *
 * The value of WAITING_BIAS supports up to 32766 waiting processes. This can
 * be extended to 65534 by manually checking the whole MSW rather than relying
 * on the S flag.
 *
 * The value of ACTIVE_BIAS supports up to 65535 active processes.
 *
 * This should be totally fair - if anything is waiting, a process that wants a
 * lock will go to the back of the queue. When the currently active lock is
 * released, if there's a writer at the front of the queue, then that and only
 * that will be woken up; if there's a bunch of consequtive readers at the
 * front, then they'll all be woken up, but no other readers will be.
 */

#ifndef _LINUX_RWSEM_H
#error "please don't include asm/rwsem.h directly, use linux/rwsem.h instead"
#endif

#ifdef __KERNEL__

#ifndef __s390x__
#define RWSEM_UNLOCKED_VALUE	0x00000000
#define RWSEM_ACTIVE_BIAS	0x00000001
#define RWSEM_ACTIVE_MASK	0x0000ffff
#define RWSEM_WAITING_BIAS	(-0x00010000)
#else /* __s390x__ */
#define RWSEM_UNLOCKED_VALUE	0x0000000000000000L
#define RWSEM_ACTIVE_BIAS	0x0000000000000001L
#define RWSEM_ACTIVE_MASK	0x00000000ffffffffL
#define RWSEM_WAITING_BIAS	(-0x0000000100000000L)
#endif /* __s390x__ */
#define RWSEM_ACTIVE_READ_BIAS	RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS	(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	signed long old, new;

	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	ahi	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	aghi	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "i" (RWSEM_ACTIVE_READ_BIAS)
		: "cc", "memory");
	if (old < 0)
		rwsem_down_read_failed(sem);
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	signed long old, new;

	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	ltr	%1,%0\n"
		"	jm	1f\n"
		"	ahi	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b\n"
		"1:"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	ltgr	%1,%0\n"
		"	jm	1f\n"
		"	aghi	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b\n"
		"1:"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "i" (RWSEM_ACTIVE_READ_BIAS)
		: "cc", "memory");
	return old >= 0 ? 1 : 0;
}

/*
 * lock for writing
 */
static inline void __down_write_nested(struct rw_semaphore *sem, int subclass)
{
	signed long old, new, tmp;

	tmp = RWSEM_ACTIVE_WRITE_BIAS;
	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	a	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	ag	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "m" (tmp)
		: "cc", "memory");
	if (old != 0)
		rwsem_down_write_failed(sem);
}

static inline void __down_write(struct rw_semaphore *sem)
{
	__down_write_nested(sem, 0);
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	signed long old;

	asm volatile(
#ifndef __s390x__
		"	l	%0,%1\n"
		"0:	ltr	%0,%0\n"
		"	jnz	1f\n"
		"	cs	%0,%3,%1\n"
		"	jl	0b\n"
#else /* __s390x__ */
		"	lg	%0,%1\n"
		"0:	ltgr	%0,%0\n"
		"	jnz	1f\n"
		"	csg	%0,%3,%1\n"
		"	jl	0b\n"
#endif /* __s390x__ */
		"1:"
		: "=&d" (old), "=Q" (sem->count)
		: "Q" (sem->count), "d" (RWSEM_ACTIVE_WRITE_BIAS)
		: "cc", "memory");
	return (old == RWSEM_UNLOCKED_VALUE) ? 1 : 0;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	signed long old, new;

	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	ahi	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	aghi	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "i" (-RWSEM_ACTIVE_READ_BIAS)
		: "cc", "memory");
	if (new < 0)
		if ((new & RWSEM_ACTIVE_MASK) == 0)
			rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	signed long old, new, tmp;

	tmp = -RWSEM_ACTIVE_WRITE_BIAS;
	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	a	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	ag	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "m" (tmp)
		: "cc", "memory");
	if (new < 0)
		if ((new & RWSEM_ACTIVE_MASK) == 0)
			rwsem_wake(sem);
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct rw_semaphore *sem)
{
	signed long old, new, tmp;

	tmp = -RWSEM_WAITING_BIAS;
	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	a	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	ag	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "m" (tmp)
		: "cc", "memory");
	if (new > 1)
		rwsem_downgrade_wake(sem);
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(long delta, struct rw_semaphore *sem)
{
	signed long old, new;

	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	ar	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	agr	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "d" (delta)
		: "cc", "memory");
}

/*
 * implement exchange and add functionality
 */
static inline long rwsem_atomic_update(long delta, struct rw_semaphore *sem)
{
	signed long old, new;

	asm volatile(
#ifndef __s390x__
		"	l	%0,%2\n"
		"0:	lr	%1,%0\n"
		"	ar	%1,%4\n"
		"	cs	%0,%1,%2\n"
		"	jl	0b"
#else /* __s390x__ */
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	agr	%1,%4\n"
		"	csg	%0,%1,%2\n"
		"	jl	0b"
#endif /* __s390x__ */
		: "=&d" (old), "=&d" (new), "=Q" (sem->count)
		: "Q" (sem->count), "d" (delta)
		: "cc", "memory");
	return new;
}

#endif /* __KERNEL__ */
#endif /* _S390_RWSEM_H */
