/* $Id: riowatchdog.c,v 1.3.2.2 2002/01/23 18:48:02 davem Exp $
 * riowatchdog.c - driver for hw watchdog inside Super I/O of RIO
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/miscdevice.h>

#include <asm/io.h>
#include <asm/ebus.h>
#include <asm/bbc.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>

#include <asm/watchdog.h>

/* RIO uses the NatSemi Super I/O power management logical device
 * as its' watchdog.
 *
 * When the watchdog triggers, it asserts a line to the BBC (Boot Bus
 * Controller) of the machine.  The BBC can only be configured to
 * trigger a power-on reset when the signal is asserted.  The BBC
 * can be configured to ignore the signal entirely as well.
 *
 * The only Super I/O device register we care about is at index
 * 0x05 (WDTO_INDEX) which is the watchdog time-out in minutes (1-255).
 * If set to zero, this disables the watchdog.  When set, the system
 * must periodically (before watchdog expires) clear (set to zero) and
 * re-set the watchdog else it will trigger.
 *
 * There are two other indexed watchdog registers inside this Super I/O
 * logical device, but they are unused.  The first, at index 0x06 is
 * the watchdog control and can be used to make the watchdog timer re-set
 * when the PS/2 mouse or serial lines show activity.  The second, at
 * index 0x07 is merely a sampling of the line from the watchdog to the
 * BBC.
 *
 * The watchdog device generates no interrupts.
 */

MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("Hardware watchdog driver for Sun RIO");
MODULE_SUPPORTED_DEVICE("watchdog");
MODULE_LICENSE("GPL");

#define RIOWD_NAME	"pmc"
#define RIOWD_MINOR	215

static DEFINE_SPINLOCK(riowd_lock);

static void __iomem *bbc_regs;
static void __iomem *riowd_regs;
#define WDTO_INDEX	0x05

static int riowd_timeout = 1;		/* in minutes */
module_param(riowd_timeout, int, 0);
MODULE_PARM_DESC(riowd_timeout, "Watchdog timeout in minutes");

#if 0 /* Currently unused. */
static u8 riowd_readreg(int index)
{
	unsigned long flags;
	u8 ret;

	spin_lock_irqsave(&riowd_lock, flags);
	writeb(index, riowd_regs + 0);
	ret = readb(riowd_regs + 1);
	spin_unlock_irqrestore(&riowd_lock, flags);

	return ret;
}
#endif

static void riowd_writereg(u8 val, int index)
{
	unsigned long flags;

	spin_lock_irqsave(&riowd_lock, flags);
	writeb(index, riowd_regs + 0);
	writeb(val, riowd_regs + 1);
	spin_unlock_irqrestore(&riowd_lock, flags);
}

static void riowd_pingtimer(void)
{
	riowd_writereg(riowd_timeout, WDTO_INDEX);
}

static void riowd_stoptimer(void)
{
	u8 val;

	riowd_writereg(0, WDTO_INDEX);

	val = readb(bbc_regs + BBC_WDACTION);
	val &= ~BBC_WDACTION_RST;
	writeb(val, bbc_regs + BBC_WDACTION);
}

static void riowd_starttimer(void)
{
	u8 val;

	riowd_writereg(riowd_timeout, WDTO_INDEX);

	val = readb(bbc_regs + BBC_WDACTION);
	val |= BBC_WDACTION_RST;
	writeb(val, bbc_regs + BBC_WDACTION);
}

static int riowd_open(struct inode *inode, struct file *filp)
{
	nonseekable_open(inode, filp);
	return 0;
}

static int riowd_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int riowd_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	static struct watchdog_info info = {
	       	WDIOF_SETTIMEOUT, 0, "Natl. Semiconductor PC97317"
	};
	void __user *argp = (void __user *)arg;
	unsigned int options;
	int new_margin;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		if (put_user(0, (int __user *)argp))
			return -EFAULT;
		break;

	case WDIOC_KEEPALIVE:
		riowd_pingtimer();
		break;

	case WDIOC_SETOPTIONS:
		if (copy_from_user(&options, argp, sizeof(options)))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD)
			riowd_stoptimer();
		else if (options & WDIOS_ENABLECARD)
			riowd_starttimer();
		else
			return -EINVAL;

		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *)argp))
			return -EFAULT;
		if ((new_margin < 60) || (new_margin > (255 * 60)))
		    return -EINVAL;
		riowd_timeout = (new_margin + 59) / 60;
		riowd_pingtimer();
		/* Fall */

	case WDIOC_GETTIMEOUT:
		return put_user(riowd_timeout * 60, (int __user *)argp);

	default:
		return -EINVAL;
	};

	return 0;
}

static ssize_t riowd_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count) {
		riowd_pingtimer();
		return 1;
	}

	return 0;
}

static struct file_operations riowd_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	riowd_ioctl,
	.open =		riowd_open,
	.write =	riowd_write,
	.release =	riowd_release,
};

static struct miscdevice riowd_miscdev = { RIOWD_MINOR, RIOWD_NAME, &riowd_fops };

static int __init riowd_bbc_init(void)
{
	struct 	linux_ebus *ebus = NULL;
	struct 	linux_ebus_device *edev = NULL;
	u8 val;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "bbc"))
				goto found_bbc;
		}
	}

found_bbc:
	if (!edev)
		return -ENODEV;
	bbc_regs = ioremap(edev->resource[0].start, BBC_REGS_SIZE);
	if (!bbc_regs)
		return -ENODEV;

	/* Turn it off. */
	val = readb(bbc_regs + BBC_WDACTION);
	val &= ~BBC_WDACTION_RST;
	writeb(val, bbc_regs + BBC_WDACTION);

	return 0;
}

static int __init riowd_init(void)
{
	struct 	linux_ebus *ebus = NULL;
	struct 	linux_ebus_device *edev = NULL;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, RIOWD_NAME))
				goto ebus_done;
		}
	}

ebus_done:
	if (!edev)
		goto fail;

	riowd_regs = ioremap(edev->resource[0].start, 2);
	if (riowd_regs == NULL) {
		printk(KERN_ERR "pmc: Cannot map registers.\n");
		return -ENODEV;
	}

	if (riowd_bbc_init()) {
		printk(KERN_ERR "pmc: Failure initializing BBC config.\n");
		goto fail;
	}

	if (misc_register(&riowd_miscdev)) {
		printk(KERN_ERR "pmc: Cannot register watchdog misc device.\n");
		goto fail;
	}

	printk(KERN_INFO "pmc: Hardware watchdog [%i minutes], "
	       "regs at %p\n", riowd_timeout, riowd_regs);

	return 0;

fail:
	if (riowd_regs) {
		iounmap(riowd_regs);
		riowd_regs = NULL;
	}
	if (bbc_regs) {
		iounmap(bbc_regs);
		bbc_regs = NULL;
	}
	return -ENODEV;
}

static void __exit riowd_cleanup(void)
{
	misc_deregister(&riowd_miscdev);
	iounmap(riowd_regs);
	riowd_regs = NULL;
	iounmap(bbc_regs);
	bbc_regs = NULL;
}

module_init(riowd_init);
module_exit(riowd_cleanup);
