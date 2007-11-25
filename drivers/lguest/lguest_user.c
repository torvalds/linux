/*P:200 This contains all the /dev/lguest code, whereby the userspace launcher
 * controls and communicates with the Guest.  For example, the first write will
 * tell us the Guest's memory layout, pagetable, entry point and kernel address
 * offset.  A read will run the Guest until something happens, such as a signal
 * or the Guest doing a NOTIFY out to the Launcher. :*/
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include "lg.h"

/*L:055 When something happens, the Waker process needs a way to stop the
 * kernel running the Guest and return to the Launcher.  So the Waker writes
 * LHREQ_BREAK and the value "1" to /dev/lguest to do this.  Once the Launcher
 * has done whatever needs attention, it writes LHREQ_BREAK and "0" to release
 * the Waker. */
static int break_guest_out(struct lguest *lg, const unsigned long __user *input)
{
	unsigned long on;

	/* Fetch whether they're turning break on or off. */
	if (get_user(on, input) != 0)
		return -EFAULT;

	if (on) {
		lg->break_out = 1;
		/* Pop it out of the Guest (may be running on different CPU) */
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
static int user_send_irq(struct lguest *lg, const unsigned long __user *input)
{
	unsigned long irq;

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

	/* If you're not the task which owns the Guest, go away. */
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

	/* If we returned from read() last time because the Guest notified,
	 * clear the flag. */
	if (lg->pending_notify)
		lg->pending_notify = 0;

	/* Run the Guest until something interesting happens. */
	return run_guest(lg, (unsigned long __user *)user);
}

/*L:020 The initialization write supplies 4 pointer sized (32 or 64 bit)
 * values (in addition to the LHREQ_INITIALIZE value).  These are:
 *
 * base: The start of the Guest-physical memory inside the Launcher memory.
 *
 * pfnlimit: The highest (Guest-physical) page number the Guest should be
 * allowed to access.  The Guest memory lives inside the Launcher, so it sets
 * this to ensure the Guest can only reach its own memory.
 *
 * pgdir: The (Guest-physical) address of the top of the initial Guest
 * pagetables (which are set up by the Launcher).
 *
 * start: The first instruction to execute ("eip" in x86-speak).
 */
static int initialize(struct file *file, const unsigned long __user *input)
{
	/* "struct lguest" contains everything we (the Host) know about a
	 * Guest. */
	struct lguest *lg;
	int err;
	unsigned long args[4];

	/* We grab the Big Lguest lock, which protects against multiple
	 * simultaneous initializations. */
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

	lg = kzalloc(sizeof(*lg), GFP_KERNEL);
	if (!lg) {
		err = -ENOMEM;
		goto unlock;
	}

	/* Populate the easy fields of our "struct lguest" */
	lg->mem_base = (void __user *)(long)args[0];
	lg->pfn_limit = args[1];

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
	err = init_guest_pagetable(lg, args[2]);
	if (err)
		goto free_regs;

	/* Now we initialize the Guest's registers, handing it the start
	 * address. */
	lguest_arch_setup_regs(lg, args[3]);

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
	kfree(lg);
unlock:
	mutex_unlock(&lguest_lock);
	return err;
}

/*L:010 The first operation the Launcher does must be a write.  All writes
 * start with an unsigned long number: for the first write this must be
 * LHREQ_INITIALIZE to set up the Guest.  After that the Launcher can use
 * writes of other values to send interrupts. */
static ssize_t write(struct file *file, const char __user *in,
		     size_t size, loff_t *off)
{
	/* Once the guest is initialized, we hold the "struct lguest" in the
	 * file private data. */
	struct lguest *lg = file->private_data;
	const unsigned long __user *input = (const unsigned long __user *)in;
	unsigned long req;

	if (get_user(req, input) != 0)
		return -EFAULT;
	input++;

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
		return initialize(file, input);
	case LHREQ_IRQ:
		return user_send_irq(lg, input);
	case LHREQ_BREAK:
		return break_guest_out(lg, input);
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
 * the Guest, but the Guest can't know that.
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
