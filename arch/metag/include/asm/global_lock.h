#ifndef __ASM_METAG_GLOBAL_LOCK_H
#define __ASM_METAG_GLOBAL_LOCK_H

#include <asm/metag_mem.h>

/**
 * __global_lock1() - Acquire global voluntary lock (LOCK1).
 * @flags:	Variable to store flags into.
 *
 * Acquires the Meta global voluntary lock (LOCK1), also taking care to disable
 * all triggers so we cannot be interrupted, and to enforce a compiler barrier
 * so that the compiler cannot reorder memory accesses across the lock.
 *
 * No other hardware thread will be able to acquire the voluntary or exclusive
 * locks until the voluntary lock is released with @__global_unlock1, but they
 * may continue to execute as long as they aren't trying to acquire either of
 * the locks.
 */
#define __global_lock1(flags) do {					\
	unsigned int __trval;						\
	asm volatile("MOV	%0,#0\n\t"				\
		     "SWAP	%0,TXMASKI\n\t"				\
		     "LOCK1"						\
		     : "=r" (__trval)					\
		     :							\
		     : "memory");					\
	(flags) = __trval;						\
} while (0)

/**
 * __global_unlock1() - Release global voluntary lock (LOCK1).
 * @flags:	Variable to restore flags from.
 *
 * Releases the Meta global voluntary lock (LOCK1) acquired with
 * @__global_lock1, also taking care to re-enable triggers, and to enforce a
 * compiler barrier so that the compiler cannot reorder memory accesses across
 * the unlock.
 *
 * This immediately allows another hardware thread to acquire the voluntary or
 * exclusive locks.
 */
#define __global_unlock1(flags) do {					\
	unsigned int __trval = (flags);					\
	asm volatile("LOCK0\n\t"					\
		     "MOV	TXMASKI,%0"				\
		     :							\
		     : "r" (__trval)					\
		     : "memory");					\
} while (0)

/**
 * __global_lock2() - Acquire global exclusive lock (LOCK2).
 * @flags:	Variable to store flags into.
 *
 * Acquires the Meta global voluntary lock and global exclusive lock (LOCK2),
 * also taking care to disable all triggers so we cannot be interrupted, to take
 * the atomic lock (system event) and to enforce a compiler barrier so that the
 * compiler cannot reorder memory accesses across the lock.
 *
 * No other hardware thread will be able to execute code until the locks are
 * released with @__global_unlock2.
 */
#define __global_lock2(flags) do {					\
	unsigned int __trval;						\
	unsigned int __aloc_hi = LINSYSEVENT_WR_ATOMIC_LOCK & 0xFFFF0000; \
	asm volatile("MOV	%0,#0\n\t"				\
		     "SWAP	%0,TXMASKI\n\t"				\
		     "LOCK2\n\t"					\
		     "SETD	[%1+#0x40],D1RtP"			\
		     : "=r&" (__trval)					\
		     : "u" (__aloc_hi)					\
		     : "memory");					\
	(flags) = __trval;						\
} while (0)

/**
 * __global_unlock2() - Release global exclusive lock (LOCK2).
 * @flags:	Variable to restore flags from.
 *
 * Releases the Meta global exclusive lock (LOCK2) and global voluntary lock
 * acquired with @__global_lock2, also taking care to release the atomic lock
 * (system event), re-enable triggers, and to enforce a compiler barrier so that
 * the compiler cannot reorder memory accesses across the unlock.
 *
 * This immediately allows other hardware threads to continue executing and one
 * of them to acquire locks.
 */
#define __global_unlock2(flags) do {					\
	unsigned int __trval = (flags);					\
	unsigned int __alock_hi = LINSYSEVENT_WR_ATOMIC_LOCK & 0xFFFF0000; \
	asm volatile("SETD	[%1+#0x00],D1RtP\n\t"			\
		     "LOCK0\n\t"					\
		     "MOV	TXMASKI,%0"				\
		     :							\
		     : "r" (__trval),					\
		       "u" (__alock_hi)					\
		     : "memory");					\
} while (0)

#endif /* __ASM_METAG_GLOBAL_LOCK_H */
