/* Userspace control of the guest, via /dev/lguest. */
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include "lg.h"

static void setup_regs(struct lguest_regs *regs, unsigned long start)
{
	/* Write out stack in format lguest expects, so we can switch to it. */
	regs->ds = regs->es = regs->ss = __KERNEL_DS|GUEST_PL;
	regs->cs = __KERNEL_CS|GUEST_PL;
	regs->eflags = 0x202; 	/* Interrupts enabled. */
	regs->eip = start;
	/* esi points to our boot information (physical address 0) */
}

/* + addr */
static long user_get_dma(struct lguest *lg, const u32 __user *input)
{
	unsigned long key, udma, irq;

	if (get_user(key, input) != 0)
		return -EFAULT;
	udma = get_dma_buffer(lg, key, &irq);
	if (!udma)
		return -ENOENT;

	/* We put irq number in udma->used_len. */
	lgwrite_u32(lg, udma + offsetof(struct lguest_dma, used_len), irq);
	return udma;
}

/* To force the Guest to stop running and return to the Launcher, the
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

/* + irq */
static int user_send_irq(struct lguest *lg, const u32 __user *input)
{
	u32 irq;

	if (get_user(irq, input) != 0)
		return -EFAULT;
	if (irq >= LGUEST_IRQS)
		return -EINVAL;
	set_bit(irq, lg->irqs_pending);
	return 0;
}

static ssize_t read(struct file *file, char __user *user, size_t size,loff_t*o)
{
	struct lguest *lg = file->private_data;

	if (!lg)
		return -EINVAL;

	/* If you're not the task which owns the guest, go away. */
	if (current != lg->tsk)
		return -EPERM;

	if (lg->dead) {
		size_t len;

		if (IS_ERR(lg->dead))
			return PTR_ERR(lg->dead);

		len = min(size, strlen(lg->dead)+1);
		if (copy_to_user(user, lg->dead, len) != 0)
			return -EFAULT;
		return len;
	}

	if (lg->dma_is_pending)
		lg->dma_is_pending = 0;

	return run_guest(lg, (unsigned long __user *)user);
}

/* Take: pfnlimit, pgdir, start, pageoffset. */
static int initialize(struct file *file, const u32 __user *input)
{
	struct lguest *lg;
	int err, i;
	u32 args[4];

	/* We grab the Big Lguest lock, which protects the global array
	 * "lguests" and multiple simultaneous initializations. */
	mutex_lock(&lguest_lock);

	if (file->private_data) {
		err = -EBUSY;
		goto unlock;
	}

	if (copy_from_user(args, input, sizeof(args)) != 0) {
		err = -EFAULT;
		goto unlock;
	}

	i = find_free_guest();
	if (i < 0) {
		err = -ENOSPC;
		goto unlock;
	}
	lg = &lguests[i];
	lg->guestid = i;
	lg->pfn_limit = args[0];
	lg->page_offset = args[3];
	lg->regs_page = get_zeroed_page(GFP_KERNEL);
	if (!lg->regs_page) {
		err = -ENOMEM;
		goto release_guest;
	}
	lg->regs = (void *)lg->regs_page + PAGE_SIZE - sizeof(*lg->regs);

	err = init_guest_pagetable(lg, args[1]);
	if (err)
		goto free_regs;

	setup_regs(lg->regs, args[2]);
	setup_guest_gdt(lg);
	init_clockdev(lg);
	lg->tsk = current;
	lg->mm = get_task_mm(lg->tsk);
	init_waitqueue_head(&lg->break_wq);
	lg->last_pages = NULL;
	file->private_data = lg;

	mutex_unlock(&lguest_lock);

	return sizeof(args);

free_regs:
	free_page(lg->regs_page);
release_guest:
	memset(lg, 0, sizeof(*lg));
unlock:
	mutex_unlock(&lguest_lock);
	return err;
}

static ssize_t write(struct file *file, const char __user *input,
		     size_t size, loff_t *off)
{
	struct lguest *lg = file->private_data;
	u32 req;

	if (get_user(req, input) != 0)
		return -EFAULT;
	input += sizeof(req);

	if (req != LHREQ_INITIALIZE && !lg)
		return -EINVAL;
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

static int close(struct inode *inode, struct file *file)
{
	struct lguest *lg = file->private_data;

	if (!lg)
		return 0;

	mutex_lock(&lguest_lock);
	/* Cancels the hrtimer set via LHCALL_SET_CLOCKEVENT. */
	hrtimer_cancel(&lg->hrt);
	release_all_dma(lg);
	free_guest_pagetable(lg);
	mmput(lg->mm);
	if (!IS_ERR(lg->dead))
		kfree(lg->dead);
	free_page(lg->regs_page);
	memset(lg, 0, sizeof(*lg));
	mutex_unlock(&lguest_lock);
	return 0;
}

static struct file_operations lguest_fops = {
	.owner	 = THIS_MODULE,
	.release = close,
	.write	 = write,
	.read	 = read,
};
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
