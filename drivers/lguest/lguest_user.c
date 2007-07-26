/*P:200 This contains all the /dev/lguest code, whereby the userspace launcher
 * controls and communicates with the Guest.  For example, the first write will
 * tell us the memory size, pagetable, entry point and kernel address offset.
 * A read will run the Guest until a signal is pending (-EINTR), or the Guest
 * does a DMA out to the Launcher.  Writes are also used to get a DMA buffer
 * registered by the Guest and to send the Guest an interrupt. :*/
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include "lg.h"

/*L:030 setup_regs() doesn't really belong in this file, but it gives us an
 * early glimpse deeper into the Host so it's worth having here.
 *
 * Most of the Guest's registers are left alone: we used get_zeroed_page() to
 * allocate the structure, so they will be 0. */
static void setup_regs(struct lguest_regs *regs, unsigned long start)
{
	/* There are four "segment" registers which the Guest needs to boot:
	 * The "code segment" register (cs) refers to the kernel code segment
	 * __KERNEL_CS, and the "data", "extra" and "stack" segment registers
	 * refer to the kernel data segment __KERNEL_DS.
	 *
	 * The privilege level is packed into the lower bits.  The Guest runs
	 * at privilege level 1 (GUEST_PL).*/
	regs->ds = regs->es = regs->ss = __KERNEL_DS|GUEST_PL;
	regs->cs = __KERNEL_CS|GUEST_PL;

	/* The "eflags" register contains miscellaneous flags.  Bit 1 (0x002)
	 * is supposed to always be "1".  Bit 9 (0x200) controls whether
	 * interrupts are enabled.  We always leave interrupts enabled while
	 * running the Guest. */
	regs->eflags = 0x202;

	/* The "Extended Instruction Pointer" register says where the Guest is
	 * running. */
	regs->eip = start;

	/* %esi points to our boot information, at physical address 0, so don't
	 * touch it. */
}

/*L:310 To send DMA into the Guest, the Launcher needs to be able to ask for a
 * DMA buffer.  This is done by writing LHREQ_GETDMA and the key to
 * /dev/lguest. */
static long user_get_dma(struct lguest *lg, const u32 __user *input)
{
	unsigned long key, udma, irq;

	/* Fetch the key they wrote to us. */
	if (get_user(key, input) != 0)
		return -EFAULT;
	/* Look for a free Guest DMA buffer bound to that key. */
	udma = get_dma_buffer(lg, key, &irq);
	if (!udma)
		return -ENOENT;

	/* We need to tell the Launcher what interrupt the Guest expects after
	 * the buffer is filled.  We stash it in udma->used_len. */
	lgwrite_u32(lg, udma + offsetof(struct lguest_dma, used_len), irq);

	/* The (guest-physical) address of the DMA buffer is returned from
	 * the write(). */
	return udma;
}

/*L:315 To force the Guest to stop running and return to the Launcher, the
 * Waker sets writes LHREQ_BREAK and the value "1" to /dev/lguest.  The
 * Launcher then writes LHREQ_BREAK and "0" to release the Waker. */
static int break_guest_out(struct lguest *lg, const u32 __user *input)
{
	unsigned long on;

	/* Fetch whether they're turning break on or off.. */
	if (get_user(on, input) != 0)
		return -EFAULT;

	if (on) {
		lg->break_out = 1;
		/* Pop it out (may be running on different CPU) */
		wake_up_process(lg->tsk);
		/* Wait for them to reset it */
		return wait_event_interruptible(lg->break_wq, !lg->break_out);
	} else {
		lg->break_out = 0;
		wake_up(&lg->break_wq);
		return 0;
	}
}

/*L:050 Sending an interrupt is done by writing LHREQ_IRQ and an interrupt
 * number to /dev/lguest. */
static int user_send_irq(struct lguest *lg, const u32 __user *input)
{
	u32 irq;

	if (get_user(irq, input) != 0)
		return -EFAULT;
	if (irq >= LGUEST_IRQS)
		return -EINVAL;
	/* Next time the Guest runs, the core code will see if it can deliver
	 * this interrupt. */
	set_bit(irq, lg->irqs_pending);
	return 0;
}

/*L:040 Once our Guest is initialized, the Launcher makes it run by reading
 * from /dev/lguest. */
static ssize_t read(struct file *file, char __user *user, size_t size,loff_t*o)
{
	struct lguest *lg = file->private_data;

	/* You must write LHREQ_INITIALIZE first! */
	if (!lg)
		return -EINVAL;

	/* If you're not the task which owns the guest, go away. */
	if (current != lg->tsk)
		return -EPERM;

	/* If the guest is already dead, we indicate why */
	if (lg->dead) {
		size_t len;

		/* lg->dead either contains an error code, or a string. */
		if (IS_ERR(lg->dead))
			return PTR_ERR(lg->dead);

		/* We can only return as much as the buffer they read with. */
		len = min(size, strlen(lg->dead)+1);
		if (copy_to_user(user, lg->dead, len) != 0)
			return -EFAULT;
		return len;
	}

	/* If we returned from read() last time because the Guest sent DMA,
	 * clear the flag. */
	if (lg->dma_is_pending)
		lg->dma_is_pending = 0;

	/* Run the Guest until something interesting happens. */
	return run_guest(lg, (unsigned long __user *)user);
}

/*L:020 The initialization write supplies 4 32-bit values (in addition to the
 * 32-bit LHREQ_INITIALIZE value).  These are:
 *
 * pfnlimit: The highest (Guest-physical) page number the Guest should be
 * allowed to access.  The Launcher has to live in Guest memory, so it sets
 * this to ensure the Guest can't reach it.
 *
 * pgdir: The (Guest-physical) address of the top of the initial Guest
 * pagetables (which are set up by the Launcher).
 *
 * start: The first instruction to execute ("eip" in x86-speak).
 *
 * page_offset: The PAGE_OFFSET constant in the Guest kernel.  We should
 * probably wean the code off this, but it's a very useful constant!  Any
 * address above this is within the Guest kernel, and any kernel address can
 * quickly converted from physical to virtual by adding PAGE_OFFSET.  It's
 * 0xC0000000 (3G) by default, but it's configurable at kernel build time.
 */
static int initialize(struct file *file, const u32 __user *input)
{
	/* "struct lguest" contains everything we (the Host) know about a
	 * Guest. */
	struct lguest *lg;
	int err, i;
	u32 args[4];

	/* We grab the Big Lguest lock, which protects the global array
	 * "lguests" and multiple simultaneous initializations. */
	mutex_lock(&lguest_lock);
	/* You can't initialize twice!  Close the device and start again... */
	if (file->private_data) {
		err = -EBUSY;
		goto unlock;
	}

	if (copy_from_user(args, input, sizeof(args)) != 0) {
		err = -EFAULT;
		goto unlock;
	}

	/* Find an unused guest. */
	i = find_free_guest();
	if (i < 0) {
		err = -ENOSPC;
		goto unlock;
	}
	/* OK, we have an index into the "lguest" array: "lg" is a convenient
	 * pointer. */
	lg = &lguests[i];

	/* Populate the easy fields of our "struct lguest" */
	lg->guestid = i;
	lg->pfn_limit = args[0];
	lg->page_offset = args[3];

	/* We need a complete page for the Guest registers: they are accessible
	 * to the Guest and we can only grant it access to whole pages. */
	lg->regs_page = get_zeroed_page(GFP_KERNEL);
	if (!lg->regs_page) {
		err = -ENOMEM;
		goto release_guest;
	}
	/* We actually put the registers at the bottom of the page. */
	lg->regs = (void *)lg->regs_page + PAGE_SIZE - sizeof(*lg->regs);

	/* Initialize the Guest's shadow page tables, using the toplevel
	 * address the Launcher gave us.  This allocates memory, so can
	 * fail. */
	err = init_guest_pagetable(lg, args[1]);
	if (err)
		goto free_regs;

	/* Now we initialize the Guest's registers, handing it the start
	 * address. */
	setup_regs(lg->regs, args[2]);

	/* There are a couple of GDT entries the Guest expects when first
	 * booting. */
	setup_guest_gdt(lg);

	/* The timer for lguest's clock needs initialization. */
	init_clockdev(lg);

	/* We keep a pointer to the Launcher task (ie. current task) for when
	 * other Guests want to wake this one (inter-Guest I/O). */
	lg->tsk = current;
	/* We need to keep a pointer to the Launcher's memory map, because if
	 * the Launcher dies we need to clean it up.  If we don't keep a
	 * reference, it is destroyed before close() is called. */
	lg->mm = get_task_mm(lg->tsk);

	/* Initialize the queue for the waker to wait on */
	init_waitqueue_head(&lg->break_wq);

	/* We remember which CPU's pages this Guest used last, for optimization
	 * when the same Guest runs on the same CPU twice. */
	lg->last_pages = NULL;

	/* We keep our "struct lguest" in the file's private_data. */
	file->private_data = lg;

	mutex_unlock(&lguest_lock);

	/* And because this is a write() call, we return the length used. */
	return sizeof(args);

free_regs:
	free_page(lg->regs_page);
release_guest:
	memset(lg, 0, sizeof(*lg));
unlock:
	mutex_unlock(&lguest_lock);
	return err;
}

/*L:010 The first operation the Launcher does must be a write.  All writes
 * start with a 32 bit number: for the first write this must be
 * LHREQ_INITIALIZE to set up the Guest.  After that the Launcher can use
 * writes of other values to get DMA buffers and send interrupts. */
static ssize_t write(struct file *file, const char __user *input,
		     size_t size, loff_t *off)
{
	/* Once the guest is initialized, we hold the "struct lguest" in the
	 * file private data. */
	struct lguest *lg = file->private_data;
	u32 req;

	if (get_user(req, input) != 0)
		return -EFAULT;
	input += sizeof(req);

	/* If you haven't initialized, you must do that first. */
	if (req != LHREQ_INITIALIZE && !lg)
		return -EINVAL;

	/* Once the Guest is dead, all you can do is read() why it died. */
	if (lg && lg->dead)
		return -ENOENT;

	/* If you're not the task which owns the Guest, you can only break */
	if (lg && current != lg->tsk && req != LHREQ_BREAK)
		return -EPERM;

	switch (req) {
	case LHREQ_INITIALIZE:
		return initialize(file, (const u32 __user *)input);
	case LHREQ_GETDMA:
		return user_get_dma(lg, (const u32 __user *)input);
	case LHREQ_IRQ:
		return user_send_irq(lg, (const u32 __user *)input);
	case LHREQ_BREAK:
		return break_guest_out(lg, (const u32 __user *)input);
	default:
		return -EINVAL;
	}
}

/*L:060 The final piece of interface code is the close() routine.  It reverses
 * everything done in initialize().  This is usually called because the
 * Launcher exited.
 *
 * Note that the close routine returns 0 or a negative error number: it can't
 * really fail, but it can whine.  I blame Sun for this wart, and K&R C for
 * letting them do it. :*/
static int close(struct inode *inode, struct file *file)
{
	struct lguest *lg = file->private_data;

	/* If we never successfully initialized, there's nothing to clean up */
	if (!lg)
		return 0;

	/* We need the big lock, to protect from inter-guest I/O and other
	 * Launchers initializing guests. */
	mutex_lock(&lguest_lock);
	/* Cancels the hrtimer set via LHCALL_SET_CLOCKEVENT. */
	hrtimer_cancel(&lg->hrt);
	/* Free any DMA buffers the Guest had bound. */
	release_all_dma(lg);
	/* Free up the shadow page tables for the Guest. */
	free_guest_pagetable(lg);
	/* Now all the memory cleanups are done, it's safe to release the
	 * Launcher's memory management structure. */
	mmput(lg->mm);
	/* If lg->dead doesn't contain an error code it will be NULL or a
	 * kmalloc()ed string, either of which is ok to hand to kfree(). */
	if (!IS_ERR(lg->dead))
		kfree(lg->dead);
	/* We can free up the register page we allocated. */
	free_page(lg->regs_page);
	/* We clear the entire structure, which also marks it as free for the
	 * next user. */
	memset(lg, 0, sizeof(*lg));
	/* Release lock and exit. */
	mutex_unlock(&lguest_lock);

	return 0;
}

/*L:000
 * Welcome to our journey through the Launcher!
 *
 * The Launcher is the Host userspace program which sets up, runs and services
 * the Guest.  In fact, many comments in the Drivers which refer to "the Host"
 * doing things are inaccurate: the Launcher does all the device handling for
 * the Guest.  The Guest can't tell what's done by the the Launcher and what by
 * the Host.
 *
 * Just to confuse you: to the Host kernel, the Launcher *is* the Guest and we
 * shall see more of that later.
 *
 * We begin our understanding with the Host kernel interface which the Launcher
 * uses: reading and writing a character device called /dev/lguest.  All the
 * work happens in the read(), write() and close() routines: */
static struct file_operations lguest_fops = {
	.owner	 = THIS_MODULE,
	.release = close,
	.write	 = write,
	.read	 = read,
};

/* This is a textbook example of a "misc" character device.  Populate a "struct
 * miscdevice" and register it with misc_register(). */
static struct miscdevice lguest_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "lguest",
	.fops	= &lguest_fops,
};

int __init lguest_device_init(void)
{
	return misc_register(&lguest_dev);
}

void __exit lguest_device_remove(void)
{
	misc_deregister(&lguest_dev);
}
