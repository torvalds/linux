/*
 *	linux/arch/i386/kernel/ioport.c
 *
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/thread_info.h>

/* Set EXTENT bits starting at BASE in BITMAP to value TURN_ON. */
static void set_bitmap(unsigned long *bitmap, unsigned int base, unsigned int extent, int new_value)
{
	unsigned long mask;
	unsigned long *bitmap_base = bitmap + (base / BITS_PER_LONG);
	unsigned int low_index = base & (BITS_PER_LONG-1);
	int length = low_index + extent;

	if (low_index != 0) {
		mask = (~0UL << low_index);
		if (length < BITS_PER_LONG)
			mask &= ~(~0UL << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
		length -= BITS_PER_LONG;
	}

	mask = (new_value ? ~0UL : 0UL);
	while (length >= BITS_PER_LONG) {
		*bitmap_base++ = mask;
		length -= BITS_PER_LONG;
	}

	if (length > 0) {
		mask = ~(~0UL << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
	}
}


/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	unsigned long i, max_long, bytes, bytes_updated;
	struct thread_struct * t = &current->thread;
	struct tss_struct * tss;
	unsigned long *bitmap;

	if ((from + num <= from) || (from + num > IO_BITMAP_BITS))
		return -EINVAL;
	if (turn_on && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/*
	 * If it's the first ioperm() call in this thread's lifetime, set the
	 * IO bitmap up. ioperm() is much less timing critical than clone(),
	 * this is why we delay this operation until now:
	 */
	if (!t->io_bitmap_ptr) {
		bitmap = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
		if (!bitmap)
			return -ENOMEM;

		memset(bitmap, 0xff, IO_BITMAP_BYTES);
		t->io_bitmap_ptr = bitmap;
	}

	/*
	 * do it in the per-thread copy and in the TSS ...
	 *
	 * Disable preemption via get_cpu() - we must not switch away
	 * because the ->io_bitmap_max value must match the bitmap
	 * contents:
	 */
	tss = &per_cpu(init_tss, get_cpu());

	set_bitmap(t->io_bitmap_ptr, from, num, !turn_on);

	/*
	 * Search for a (possibly new) maximum. This is simple and stupid,
	 * to keep it obviously correct:
	 */
	max_long = 0;
	for (i = 0; i < IO_BITMAP_LONGS; i++)
		if (t->io_bitmap_ptr[i] != ~0UL)
			max_long = i;

	bytes = (max_long + 1) * sizeof(long);
	bytes_updated = max(bytes, t->io_bitmap_max);

	t->io_bitmap_max = bytes;

	/*
	 * Sets the lazy trigger so that the next I/O operation will
	 * reload the correct bitmap.
	 */
	tss->io_bitmap_base = INVALID_IO_BITMAP_OFFSET_LAZY;

	put_cpu();

	return 0;
}

/*
 * sys_iopl has to be used when you want to access the IO ports
 * beyond the 0x3ff range: to get the full 65536 ports bitmapped
 * you'd need 8kB of bitmaps/process, which is a bit excessive.
 *
 * Here we just change the eflags value on the stack: we allow
 * only the super-user to do it. This depends on the stack-layout
 * on system-call entry - see also fork() and the signal handling
 * code.
 */

asmlinkage long sys_iopl(unsigned long unused)
{
	volatile struct pt_regs * regs = (struct pt_regs *) &unused;
	unsigned int level = regs->ebx;
	unsigned int old = (regs->eflags >> 12) & 3;

	if (level > 3)
		return -EINVAL;
	/* Trying to gain more privileges? */
	if (level > old) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}
	regs->eflags = (regs->eflags &~ 0x3000UL) | (level << 12);
	/* Make sure we return the long way (not sysenter) */
	set_thread_flag(TIF_IRET);
	return 0;
}
