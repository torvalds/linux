/* $Id: semaphore.c,v 1.9 2001/11/18 00:12:56 davem Exp $
 * semaphore.c: Sparc64 semaphore implementation.
 *
 * This is basically the PPC semaphore scheme ported to use
 * the sparc64 atomic instructions, so see the PPC code for
 * credits.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>

/*
 * Atomically update sem->count.
 * This does the equivalent of the following:
 *
 *	old_count = sem->count;
 *	tmp = MAX(old_count, 0) + incr;
 *	sem->count = tmp;
 *	return old_count;
 */
static __inline__ int __sem_update_count(struct semaphore *sem, int incr)
{
	int old_count, tmp;

	__asm__ __volatile__("\n"
"	! __sem_update_count old_count(%0) tmp(%1) incr(%4) &sem->count(%3)\n"
"1:	ldsw	[%3], %0\n"
"	mov	%0, %1\n"
"	cmp	%0, 0\n"
"	movl	%%icc, 0, %1\n"
"	add	%1, %4, %1\n"
"	cas	[%3], %0, %1\n"
"	cmp	%0, %1\n"
"	membar	#StoreLoad | #StoreStore\n"
"	bne,pn	%%icc, 1b\n"
"	 nop\n"
	: "=&r" (old_count), "=&r" (tmp), "=m" (sem->count)
	: "r" (&sem->count), "r" (incr), "m" (sem->count)
	: "cc");

	return old_count;
}

static void __up(struct semaphore *sem)
{
	__sem_update_count(sem, 1);
	wake_up(&sem->wait);
}

void up(struct semaphore *sem)
{
	/* This atomically does:
	 * 	old_val = sem->count;
	 *	new_val = sem->count + 1;
	 *	sem->count = new_val;
	 *	if (old_val < 0)
	 *		__up(sem);
	 *
	 * The (old_val < 0) test is equivalent to
	 * the more straightforward (new_val <= 0),
	 * but it is easier to test the former because
	 * of how the CAS instruction works.
	 */

	__asm__ __volatile__("\n"
"	! up sem(%0)\n"
"	membar	#StoreLoad | #LoadLoad\n"
"1:	lduw	[%0], %%g1\n"
"	add	%%g1, 1, %%g7\n"
"	cas	[%0], %%g1, %%g7\n"
"	cmp	%%g1, %%g7\n"
"	bne,pn	%%icc, 1b\n"
"	 addcc	%%g7, 1, %%g0\n"
"	membar	#StoreLoad | #StoreStore\n"
"	ble,pn	%%icc, 3f\n"
"	 nop\n"
"2:\n"
"	.subsection 2\n"
"3:	mov	%0, %%g1\n"
"	save	%%sp, -160, %%sp\n"
"	call	%1\n"
"	 mov	%%g1, %%o0\n"
"	ba,pt	%%xcc, 2b\n"
"	 restore\n"
"	.previous\n"
	: : "r" (sem), "i" (__up)
	: "g1", "g2", "g3", "g7", "memory", "cc");
}

static void __sched __down(struct semaphore * sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	tsk->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	while (__sem_update_count(sem, -1) <= 0) {
		schedule();
		tsk->state = TASK_UNINTERRUPTIBLE;
	}
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;

	wake_up(&sem->wait);
}

void __sched down(struct semaphore *sem)
{
	might_sleep();
	/* This atomically does:
	 * 	old_val = sem->count;
	 *	new_val = sem->count - 1;
	 *	sem->count = new_val;
	 *	if (old_val < 1)
	 *		__down(sem);
	 *
	 * The (old_val < 1) test is equivalent to
	 * the more straightforward (new_val < 0),
	 * but it is easier to test the former because
	 * of how the CAS instruction works.
	 */

	__asm__ __volatile__("\n"
"	! down sem(%0)\n"
"1:	lduw	[%0], %%g1\n"
"	sub	%%g1, 1, %%g7\n"
"	cas	[%0], %%g1, %%g7\n"
"	cmp	%%g1, %%g7\n"
"	bne,pn	%%icc, 1b\n"
"	 cmp	%%g7, 1\n"
"	membar	#StoreLoad | #StoreStore\n"
"	bl,pn	%%icc, 3f\n"
"	 nop\n"
"2:\n"
"	.subsection 2\n"
"3:	mov	%0, %%g1\n"
"	save	%%sp, -160, %%sp\n"
"	call	%1\n"
"	 mov	%%g1, %%o0\n"
"	ba,pt	%%xcc, 2b\n"
"	 restore\n"
"	.previous\n"
	: : "r" (sem), "i" (__down)
	: "g1", "g2", "g3", "g7", "memory", "cc");
}

int down_trylock(struct semaphore *sem)
{
	int ret;

	/* This atomically does:
	 * 	old_val = sem->count;
	 *	new_val = sem->count - 1;
	 *	if (old_val < 1) {
	 *		ret = 1;
	 *	} else {
	 *		sem->count = new_val;
	 *		ret = 0;
	 *	}
	 *
	 * The (old_val < 1) test is equivalent to
	 * the more straightforward (new_val < 0),
	 * but it is easier to test the former because
	 * of how the CAS instruction works.
	 */

	__asm__ __volatile__("\n"
"	! down_trylock sem(%1) ret(%0)\n"
"1:	lduw	[%1], %%g1\n"
"	sub	%%g1, 1, %%g7\n"
"	cmp	%%g1, 1\n"
"	bl,pn	%%icc, 2f\n"
"	 mov	1, %0\n"
"	cas	[%1], %%g1, %%g7\n"
"	cmp	%%g1, %%g7\n"
"	bne,pn	%%icc, 1b\n"
"	 mov	0, %0\n"
"	membar	#StoreLoad | #StoreStore\n"
"2:\n"
	: "=&r" (ret)
	: "r" (sem)
	: "g1", "g7", "memory", "cc");

	return ret;
}

static int __sched __down_interruptible(struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	tsk->state = TASK_INTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	while (__sem_update_count(sem, -1) <= 0) {
		if (signal_pending(current)) {
			__sem_update_count(sem, 0);
			retval = -EINTR;
			break;
		}
		schedule();
		tsk->state = TASK_INTERRUPTIBLE;
	}
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
	wake_up(&sem->wait);
	return retval;
}

int __sched down_interruptible(struct semaphore *sem)
{
	int ret = 0;
	
	might_sleep();
	/* This atomically does:
	 * 	old_val = sem->count;
	 *	new_val = sem->count - 1;
	 *	sem->count = new_val;
	 *	if (old_val < 1)
	 *		ret = __down_interruptible(sem);
	 *
	 * The (old_val < 1) test is equivalent to
	 * the more straightforward (new_val < 0),
	 * but it is easier to test the former because
	 * of how the CAS instruction works.
	 */

	__asm__ __volatile__("\n"
"	! down_interruptible sem(%2) ret(%0)\n"
"1:	lduw	[%2], %%g1\n"
"	sub	%%g1, 1, %%g7\n"
"	cas	[%2], %%g1, %%g7\n"
"	cmp	%%g1, %%g7\n"
"	bne,pn	%%icc, 1b\n"
"	 cmp	%%g7, 1\n"
"	membar	#StoreLoad | #StoreStore\n"
"	bl,pn	%%icc, 3f\n"
"	 nop\n"
"2:\n"
"	.subsection 2\n"
"3:	mov	%2, %%g1\n"
"	save	%%sp, -160, %%sp\n"
"	call	%3\n"
"	 mov	%%g1, %%o0\n"
"	ba,pt	%%xcc, 2b\n"
"	 restore\n"
"	.previous\n"
	: "=r" (ret)
	: "0" (ret), "r" (sem), "i" (__down_interruptible)
	: "g1", "g2", "g3", "g7", "memory", "cc");
	return ret;
}
