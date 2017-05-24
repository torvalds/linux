/* spinlock.h: 64-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SPINLOCK_H
#define __SPARC64_SPINLOCK_H

#ifndef __ASSEMBLY__

#include <asm/processor.h>
#include <asm/barrier.h>
#include <asm/qrwlock.h>

/* To get debugging spinlocks which detect and catch
 * deadlock situations, set CONFIG_DEBUG_SPINLOCK
 * and rebuild your kernel.
 */

/* Because we play games to save cycles in the non-contention case, we
 * need to be extra careful about branch targets into the "spinning"
 * code.  They live in their own section, but the newer V9 branches
 * have a shorter range than the traditional 32-bit sparc branch
 * variants.  The rule is that the branches that go into and out of
 * the spinner sections must be pre-V9 branches.
 */

#define arch_spin_is_locked(lp)	((lp)->lock != 0)

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	smp_cond_load_acquire(&lock->lock, !VAL);
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
"1:	ldstub		[%1], %0\n"
"	brnz,pn		%0, 2f\n"
"	 nop\n"
"	.subsection	2\n"
"2:	ldub		[%1], %0\n"
"	brnz,pt		%0, 2b\n"
"	 nop\n"
"	ba,a,pt		%%xcc, 1b\n"
"	.previous"
	: "=&r" (tmp)
	: "r" (lock)
	: "memory");
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long result;

	__asm__ __volatile__(
"	ldstub		[%1], %0\n"
	: "=r" (result)
	: "r" (lock)
	: "memory");

	return (result == 0UL);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	__asm__ __volatile__(
"	stb		%%g0, [%0]"
	: /* No outputs */
	: "r" (lock)
	: "memory");
}

static inline void arch_spin_lock_flags(arch_spinlock_t *lock, unsigned long flags)
{
	unsigned long tmp1, tmp2;

	__asm__ __volatile__(
"1:	ldstub		[%2], %0\n"
"	brnz,pn		%0, 2f\n"
"	 nop\n"
"	.subsection	2\n"
"2:	rdpr		%%pil, %1\n"
"	wrpr		%3, %%pil\n"
"3:	ldub		[%2], %0\n"
"	brnz,pt		%0, 3b\n"
"	 nop\n"
"	ba,pt		%%xcc, 1b\n"
"	 wrpr		%1, %%pil\n"
"	.previous"
	: "=&r" (tmp1), "=&r" (tmp2)
	: "r"(lock), "r"(flags)
	: "memory");
}

#define arch_read_lock_flags(p, f) arch_read_lock(p)
#define arch_write_lock_flags(p, f) arch_write_lock(p)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SPINLOCK_H) */
