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

#include <asm/desc.h>

/*
 * this changes the io permissions bitmap in the current task.
 */
long ksys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	unsigned int i, max_long, bytes, bytes_updated;
	struct thread_struct *t = &current->thread;
	struct tss_struct *tss;
	unsigned long *bitmap;

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
	bitmap = t->io_bitmap_ptr;
	if (!bitmap) {
		/* No point to allocate a bitmap just to clear permissions */
		if (!turn_on)
			return 0;
		bitmap = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
		if (!bitmap)
			return -ENOMEM;

		memset(bitmap, 0xff, IO_BITMAP_BYTES);
	}

	/*
	 * Update the bitmap and the TSS copy with preemption disabled to
	 * prevent a race against context switch.
	 */
	preempt_disable();
	if (turn_on)
		bitmap_clear(bitmap, from, num);
	else
		bitmap_set(bitmap, from, num);

	/*
	 * Search for a (possibly new) maximum. This is simple and stupid,
	 * to keep it obviously correct:
	 */
	max_long = 0;
	for (i = 0; i < IO_BITMAP_LONGS; i++) {
		if (bitmap[i] != ~0UL)
			max_long = i;
	}

	bytes = (max_long + 1) * sizeof(unsigned long);
	bytes_updated = max(bytes, t->io_bitmap_max);

	/* Update the thread data */
	t->io_bitmap_max = bytes;
	/*
	 * Store the bitmap pointer (might be the same if the task already
	 * head one). Set the TIF flag, just in case this is the first
	 * invocation.
	 */
	t->io_bitmap_ptr = bitmap;
	set_thread_flag(TIF_IO_BITMAP);

	/* Update the TSS */
	tss = this_cpu_ptr(&cpu_tss_rw);
	memcpy(tss->io_bitmap, t->io_bitmap_ptr, bytes_updated);
	/* Store the new end of the zero bits */
	tss->io_bitmap_prev_max = bytes;
	/* Make the bitmap base in the TSS valid */
	tss->x86_tss.io_bitmap_base = IO_BITMAP_OFFSET_VALID;
	/* Make sure the TSS limit covers the I/O bitmap. */
	refresh_tss_limit();

	preempt_enable();

	return 0;
}

SYSCALL_DEFINE3(ioperm, unsigned long, from, unsigned long, num, int, turn_on)
{
	return ksys_ioperm(from, num, turn_on);
}

/*
 * sys_iopl has to be used when you want to access the IO ports
 * beyond the 0x3ff range: to get the full 65536 ports bitmapped
 * you'd need 8kB of bitmaps/process, which is a bit excessive.
 *
 * Here we just change the flags value on the stack: we allow
 * only the super-user to do it. This depends on the stack-layout
 * on system-call entry - see also fork() and the signal handling
 * code.
 */
SYSCALL_DEFINE1(iopl, unsigned int, level)
{
	struct pt_regs *regs = current_pt_regs();
	struct thread_struct *t = &current->thread;

	/*
	 * Careful: the IOPL bits in regs->flags are undefined under Xen PV
	 * and changing them has no effect.
	 */
	unsigned int old = t->iopl >> X86_EFLAGS_IOPL_BIT;

	if (level > 3)
		return -EINVAL;
	/* Trying to gain more privileges? */
	if (level > old) {
		if (!capable(CAP_SYS_RAWIO) ||
		    security_locked_down(LOCKDOWN_IOPORT))
			return -EPERM;
	}
	regs->flags = (regs->flags & ~X86_EFLAGS_IOPL) |
		(level << X86_EFLAGS_IOPL_BIT);
	t->iopl = level << X86_EFLAGS_IOPL_BIT;
	set_iopl_mask(t->iopl);

	return 0;
}
