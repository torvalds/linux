// SPDX-License-Identifier: GPL-2.0
/*
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus. 32/64 bits code unification by Miguel Botón.
 */
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/bitmap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/io_bitmap.h>
#include <asm/desc.h>

static atomic64_t io_bitmap_sequence;

void io_bitmap_share(struct task_struct *tsk)
{
	/* Can be NULL when current->thread.iopl_emul == 3 */
	if (current->thread.io_bitmap) {
		/*
		 * Take a refcount on current's bitmap. It can be used by
		 * both tasks as long as none of them changes the bitmap.
		 */
		refcount_inc(&current->thread.io_bitmap->refcnt);
		tsk->thread.io_bitmap = current->thread.io_bitmap;
	}
	set_tsk_thread_flag(tsk, TIF_IO_BITMAP);
}

static void task_update_io_bitmap(void)
{
	struct thread_struct *t = &current->thread;

	if (t->iopl_emul == 3 || t->io_bitmap) {
		/* TSS update is handled on exit to user space */
		set_thread_flag(TIF_IO_BITMAP);
	} else {
		clear_thread_flag(TIF_IO_BITMAP);
		/* Invalidate TSS */
		preempt_disable();
		tss_update_io_bitmap();
		preempt_enable();
	}
}

void io_bitmap_exit(void)
{
	struct io_bitmap *iobm = current->thread.io_bitmap;

	current->thread.io_bitmap = NULL;
	task_update_io_bitmap();
	if (iobm && refcount_dec_and_test(&iobm->refcnt))
		kfree(iobm);
}

/*
 * This changes the io permissions bitmap in the current task.
 */
long ksys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	struct thread_struct *t = &current->thread;
	unsigned int i, max_long;
	struct io_bitmap *iobm;

	if ((from + num <= from) || (from + num > IO_BITMAP_BITS))
		return -EINVAL;
	if (turn_on && (!capable(CAP_SYS_RAWIO) ||
			security_locked_down(LOCKDOWN_IOPORT)))
		return -EPERM;

	/*
	 * If it's the first ioperm() call in this thread's lifetime, set the
	 * IO bitmap up. ioperm() is much less timing critical than clone(),
	 * this is why we delay this operation until now:
	 */
	iobm = t->io_bitmap;
	if (!iobm) {
		/* No point to allocate a bitmap just to clear permissions */
		if (!turn_on)
			return 0;
		iobm = kmalloc(sizeof(*iobm), GFP_KERNEL);
		if (!iobm)
			return -ENOMEM;

		memset(iobm->bitmap, 0xff, sizeof(iobm->bitmap));
		refcount_set(&iobm->refcnt, 1);
	}

	/*
	 * If the bitmap is not shared, then nothing can take a refcount as
	 * current can obviously not fork at the same time. If it's shared
	 * duplicate it and drop the refcount on the original one.
	 */
	if (refcount_read(&iobm->refcnt) > 1) {
		iobm = kmemdup(iobm, sizeof(*iobm), GFP_KERNEL);
		if (!iobm)
			return -ENOMEM;
		refcount_set(&iobm->refcnt, 1);
		io_bitmap_exit();
	}

	/*
	 * Store the bitmap pointer (might be the same if the task already
	 * head one). Must be done here so freeing the bitmap when all
	 * permissions are dropped has the pointer set up.
	 */
	t->io_bitmap = iobm;
	/* Mark it active for context switching and exit to user mode */
	set_thread_flag(TIF_IO_BITMAP);

	/*
	 * Update the tasks bitmap. The update of the TSS bitmap happens on
	 * exit to user mode. So this needs no protection.
	 */
	if (turn_on)
		bitmap_clear(iobm->bitmap, from, num);
	else
		bitmap_set(iobm->bitmap, from, num);

	/*
	 * Search for a (possibly new) maximum. This is simple and stupid,
	 * to keep it obviously correct:
	 */
	max_long = UINT_MAX;
	for (i = 0; i < IO_BITMAP_LONGS; i++) {
		if (iobm->bitmap[i] != ~0UL)
			max_long = i;
	}
	/* All permissions dropped? */
	if (max_long == UINT_MAX) {
		io_bitmap_exit();
		return 0;
	}

	iobm->max = (max_long + 1) * sizeof(unsigned long);

	/*
	 * Update the sequence number to force a TSS update on return to
	 * user mode.
	 */
	iobm->sequence = atomic64_add_return(1, &io_bitmap_sequence);

	return 0;
}

SYSCALL_DEFINE3(ioperm, unsigned long, from, unsigned long, num, int, turn_on)
{
	return ksys_ioperm(from, num, turn_on);
}

/*
 * The sys_iopl functionality depends on the level argument, which if
 * granted for the task is used by the CPU to check I/O instruction and
 * CLI/STI against the current priviledge level (CPL). If CPL is less than
 * or equal the tasks IOPL level the instructions take effect. If not a #GP
 * is raised. The default IOPL is 0, i.e. no permissions.
 *
 * Setting IOPL to level 0-2 is disabling the userspace access. Only level
 * 3 enables it. If set it allows the user space thread:
 *
 * - Unrestricted access to all 65535 I/O ports
 * - The usage of CLI/STI instructions
 *
 * The advantage over ioperm is that the context switch does not require to
 * update the I/O bitmap which is especially true when a large number of
 * ports is accessed. But the allowance of CLI/STI in userspace is
 * considered a major problem.
 *
 * IOPL is strictly per thread and inherited on fork.
 */
SYSCALL_DEFINE1(iopl, unsigned int, level)
{
	struct thread_struct *t = &current->thread;
	struct pt_regs *regs = current_pt_regs();
	unsigned int old;

	/*
	 * Careful: the IOPL bits in regs->flags are undefined under Xen PV
	 * and changing them has no effect.
	 */
	if (IS_ENABLED(CONFIG_X86_IOPL_NONE))
		return -ENOSYS;

	if (level > 3)
		return -EINVAL;

	if (IS_ENABLED(CONFIG_X86_IOPL_EMULATION))
		old = t->iopl_emul;
	else
		old = t->iopl >> X86_EFLAGS_IOPL_BIT;

	/* No point in going further if nothing changes */
	if (level == old)
		return 0;

	/* Trying to gain more privileges? */
	if (level > old) {
		if (!capable(CAP_SYS_RAWIO) ||
		    security_locked_down(LOCKDOWN_IOPORT))
			return -EPERM;
	}

	if (IS_ENABLED(CONFIG_X86_IOPL_EMULATION)) {
		t->iopl_emul = level;
		task_update_io_bitmap();
	} else {
		/*
		 * Change the flags value on the return stack, which has
		 * been set up on system-call entry. See also the fork and
		 * signal handling code how this is handled.
		 */
		regs->flags = (regs->flags & ~X86_EFLAGS_IOPL) |
			(level << X86_EFLAGS_IOPL_BIT);
		/* Store the new level in the thread struct */
		t->iopl = level << X86_EFLAGS_IOPL_BIT;
		/*
		 * X86_32 switches immediately and XEN handles it via
		 * emulation.
		 */
		set_iopl_mask(t->iopl);
	}

	return 0;
}
