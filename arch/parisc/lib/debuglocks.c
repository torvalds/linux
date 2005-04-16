/* 
 *    Debugging versions of SMP locking primitives.
 *
 *    Copyright (C) 2004 Thibaut VARENE <varenet@parisc-linux.org>
 *
 *    Some code stollen from alpha & sparc64 ;)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    We use pdc_printf() throughout the file for all output messages, to avoid
 *    losing messages because of disabled interrupts. Since we're using these
 *    messages for debugging purposes, it makes sense not to send them to the
 *    linux console.
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>	/* in_interrupt() */
#include <asm/system.h>
#include <asm/hardirq.h>	/* in_interrupt() */
#include <asm/pdc.h>

#undef INIT_STUCK
#define INIT_STUCK 1L << 30

#ifdef CONFIG_DEBUG_SPINLOCK


void _dbg_spin_lock(spinlock_t * lock, const char *base_file, int line_no)
{
	volatile unsigned int *a;
	long stuck = INIT_STUCK;
	void *inline_pc = __builtin_return_address(0);
	unsigned long started = jiffies;
	int printed = 0;
	int cpu = smp_processor_id();

try_again:

	/* Do the actual locking */
	/* <T-Bone> ggg: we can't get stuck on the outter loop?
	 * <ggg> T-Bone: We can hit the outer loop
	 *	alot if multiple CPUs are constantly racing for a lock
	 *	and the backplane is NOT fair about which CPU sees
	 *	the update first. But it won't hang since every failed
	 *	attempt will drop us back into the inner loop and
	 *	decrement `stuck'.
	 * <ggg> K-class and some of the others are NOT fair in the HW
	 * 	implementation so we could see false positives.
	 * 	But fixing the lock contention is easier than
	 * 	fixing the HW to be fair.
	 * <tausq> __ldcw() returns 1 if we get the lock; otherwise we
	 * 	spin until the value of the lock changes, or we time out.
	 */
	mb();
	a = __ldcw_align(lock);
	while (stuck && (__ldcw(a) == 0))
		while ((*a == 0) && --stuck);
	mb();

	if (unlikely(stuck <= 0)) {
		pdc_printf(
			"%s:%d: spin_lock(%s/%p) stuck in %s at %p(%d)"
			" owned by %s:%d in %s at %p(%d)\n",
			base_file, line_no, lock->module, lock,
			current->comm, inline_pc, cpu,
			lock->bfile, lock->bline, lock->task->comm,
			lock->previous, lock->oncpu);
		stuck = INIT_STUCK;
		printed = 1;
		goto try_again;
	}

	/* Exiting.  Got the lock.  */
	lock->oncpu = cpu;
	lock->previous = inline_pc;
	lock->task = current;
	lock->bfile = (char *)base_file;
	lock->bline = line_no;

	if (unlikely(printed)) {
		pdc_printf(
			"%s:%d: spin_lock grabbed in %s at %p(%d) %ld ticks\n",
			base_file, line_no, current->comm, inline_pc,
			cpu, jiffies - started);
	}
}

void _dbg_spin_unlock(spinlock_t * lock, const char *base_file, int line_no)
{
	CHECK_LOCK(lock);
	volatile unsigned int *a;
	mb();
	a = __ldcw_align(lock);
	if (unlikely((*a != 0) && lock->babble)) {
		lock->babble--;
		pdc_printf(
			"%s:%d: spin_unlock(%s:%p) not locked\n",
			base_file, line_no, lock->module, lock);
	}
	*a = 1;	
	mb();
}

int _dbg_spin_trylock(spinlock_t * lock, const char *base_file, int line_no)
{
	int ret;
	volatile unsigned int *a;
	mb();
	a = __ldcw_align(lock);
	ret = (__ldcw(a) != 0);
	mb();
	if (ret) {
		lock->oncpu = smp_processor_id();
		lock->previous = __builtin_return_address(0);
		lock->task = current;
	} else {
		lock->bfile = (char *)base_file;
		lock->bline = line_no;
	}
	return ret;
}

#endif /* CONFIG_DEBUG_SPINLOCK */

#ifdef CONFIG_DEBUG_RWLOCK

/* Interrupts trouble detailed explanation, thx Grant:
 *
 * o writer (wants to modify data) attempts to acquire the rwlock
 * o He gets the write lock.
 * o Interupts are still enabled, we take an interrupt with the
 *   write still holding the lock.
 * o interrupt handler tries to acquire the rwlock for read.
 * o deadlock since the writer can't release it at this point.
 * 
 * In general, any use of spinlocks that competes between "base"
 * level and interrupt level code will risk deadlock. Interrupts
 * need to be disabled in the base level routines to avoid it.
 * Or more precisely, only the IRQ the base level routine
 * is competing with for the lock.  But it's more efficient/faster
 * to just disable all interrupts on that CPU to guarantee
 * once it gets the lock it can release it quickly too.
 */
 
void _dbg_write_lock(rwlock_t *rw, const char *bfile, int bline)
{
	void *inline_pc = __builtin_return_address(0);
	unsigned long started = jiffies;
	long stuck = INIT_STUCK;
	int printed = 0;
	int cpu = smp_processor_id();
	
	if(unlikely(in_interrupt())) {	/* acquiring write lock in interrupt context, bad idea */
		pdc_printf("write_lock caller: %s:%d, IRQs enabled,\n", bfile, bline);
		BUG();
	}

	/* Note: if interrupts are disabled (which is most likely), the printk
	will never show on the console. We might need a polling method to flush
	the dmesg buffer anyhow. */
	
retry:
	_raw_spin_lock(&rw->lock);

	if(rw->counter != 0) {
		/* this basically never happens */
		_raw_spin_unlock(&rw->lock);
		
		stuck--;
		if ((unlikely(stuck <= 0)) && (rw->counter < 0)) {
			pdc_printf(
				"%s:%d: write_lock stuck on writer"
				" in %s at %p(%d) %ld ticks\n",
				bfile, bline, current->comm, inline_pc,
				cpu, jiffies - started);
			stuck = INIT_STUCK;
			printed = 1;
		}
		else if (unlikely(stuck <= 0)) {
			pdc_printf(
				"%s:%d: write_lock stuck on reader"
				" in %s at %p(%d) %ld ticks\n",
				bfile, bline, current->comm, inline_pc,
				cpu, jiffies - started);
			stuck = INIT_STUCK;
			printed = 1;
		}
		
		while(rw->counter != 0);

		goto retry;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */

	if (unlikely(printed)) {
		pdc_printf(
			"%s:%d: write_lock grabbed in %s at %p(%d) %ld ticks\n",
			bfile, bline, current->comm, inline_pc,
			cpu, jiffies - started);
	}
}

int _dbg_write_trylock(rwlock_t *rw, const char *bfile, int bline)
{
#if 0
	void *inline_pc = __builtin_return_address(0);
	int cpu = smp_processor_id();
#endif
	
	if(unlikely(in_interrupt())) {	/* acquiring write lock in interrupt context, bad idea */
		pdc_printf("write_lock caller: %s:%d, IRQs enabled,\n", bfile, bline);
		BUG();
	}

	/* Note: if interrupts are disabled (which is most likely), the printk
	will never show on the console. We might need a polling method to flush
	the dmesg buffer anyhow. */
	
	_raw_spin_lock(&rw->lock);

	if(rw->counter != 0) {
		/* this basically never happens */
		_raw_spin_unlock(&rw->lock);
		return 0;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */
#if 0
	pdc_printf("%s:%d: try write_lock grabbed in %s at %p(%d)\n",
		   bfile, bline, current->comm, inline_pc, cpu);
#endif
	return 1;
}

void _dbg_read_lock(rwlock_t * rw, const char *bfile, int bline)
{
#if 0
	void *inline_pc = __builtin_return_address(0);
	unsigned long started = jiffies;
	int cpu = smp_processor_id();
#endif
	unsigned long flags;

	local_irq_save(flags);
	_raw_spin_lock(&rw->lock); 

	rw->counter++;
#if 0
	pdc_printf(
		"%s:%d: read_lock grabbed in %s at %p(%d) %ld ticks\n",
		bfile, bline, current->comm, inline_pc,
		cpu, jiffies - started);
#endif
	_raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

#endif /* CONFIG_DEBUG_RWLOCK */
