/*
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus. 32/64 bits code unification by Miguel Botón.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/syscalls.h>
#include <linux/bitmap.h>
#include <asm/syscalls.h>
#include <asm/desc.h>

/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	struct thread_struct *t = &current->thread;
	struct tss_struct *tss;
	unsigned int i, max_long, bytes, bytes_updated;

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
		unsigned long *bitmap = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);

		if (!bitmap)
			return -ENOMEM;

		memset(bitmap, 0xff, IO_BITMAP_BYTES);
		t->io_bitmap_ptr = bitmap;
		set_thread_flag(TIF_IO_BITMAP);

		preempt_disable();
		refresh_TR();
		preempt_enable();
	}

	/*
	 * do it in the per-thread copy and in the TSS ...
	 *
	 * Disable preemption via get_cpu() - we must not switch away
	 * because the ->io_bitmap_max value must match the bitmap
	 * contents:
	 */
	tss = &per_cpu(cpu_tss, get_cpu());

	if (turn_on)
		bitmap_clear(t->io_bitmap_ptr, from, num);
	else
		bitmap_set(t->io_bitmap_ptr, from, num);

	/*
	 * Search for a (possibly new) maximum. This is simple and stupid,
	 * to keep it obviously correct:
	 */
	max_long = 0;
	for (i = 0; i < IO_BITMAP_LONGS; i++)
		if (t->io_bitmap_ptr[i] != ~0UL)
			max_long = i;

	bytes = (max_long + 1) * sizeof(unsigned long);
	bytes_updated = max(bytes, t->io_bitmap_max);

	t->io_bitmap_max = bytes;

	/* Update the TSS: */
	memcpy(tss->io_bitmap, t->io_bitmap_ptr, bytes_updated);

	put_cpu();

	return 0;
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
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}
	regs->flags = (regs->flags & ~X86_EFLAGS_IOPL) |
		(level << X86_EFLAGS_IOPL_BIT);
	t->iopl = level << X86_EFLAGS_IOPL_BIT;
	set_iopl_mask(t->iopl);

	return 0;
}
