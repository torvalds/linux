/* riowd.c - driver for hw watchdog inside Super I/O of RIO
 *
 * Copyright (C) 2001, 2008 David S. Miller (davem@davemloft.net)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/slab.h>


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

MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_DESCRIPTION("Hardware watchdog driver for Sun RIO");
MODULE_SUPPORTED_DEVICE("watchdog");
MODULE_LICENSE("GPL");

#define DRIVER_NAME	"riowd"
#define PFX		DRIVER_NAME ": "

struct riowd {
	void __iomem		*regs;
	spinlock_t		lock;
};

static struct riowd *riowd_device;

#define WDTO_INDEX	0x05

static int riowd_timeout = 1;		/* in minutes */
module_param(riowd_timeout, int, 0);
MODULE_PARM_DESC(riowd_timeout, "Watchdog timeout in minutes");

static void riowd_writereg(struct riowd *p, u8 val, int index)
{
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	writeb(index, p->regs + 0);
	writeb(val, p->regs + 1);
	spin_unlock_irqrestore(&p->lock, flags);
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

static long riowd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	static const struct watchdog_info info = {
		.options		= WDIOF_SETTIMEOUT,
		.firmware_version	= 1,
		.identity		= DRIVER_NAME,
	};
	void __user *argp = (void __user *)arg;
	struct riowd *p = riowd_device;
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
		riowd_writereg(p, riowd_timeout, WDTO_INDEX);
		break;

	case WDIOC_SETOPTIONS:
		if (copy_from_user(&options, argp, sizeof(options)))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD)
			riowd_writereg(p, 0, WDTO_INDEX);
		else if (options & WDIOS_ENABLECARD)
			riowd_writereg(p, riowd_timeout, WDTO_INDEX);
		else
			return -EINVAL;

		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *)argp))
			return -EFAULT;
		if ((new_margin < 60) || (new_margin > (255 * 60)))
			return -EINVAL;
		riowd_timeout = (new_margin + 59) / 60;
		riowd_writereg(p, riowd_timeout, WDTO_INDEX);
		/* Fall */

	case WDIOC_GETTIMEOUT:
		return put_user(riowd_timeout * 60, (int __user *)argp);

	default:
		return -EINVAL;
	};

	return 0;
}

static ssize_t riowd_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct riowd *p = riowd_device;

	if (count) {
		riowd_writereg(p, riowd_timeout, WDTO_INDEX);
		return 1;
	}

	return 0;
}

static const struct file_operations riowd_fops = {
	.owner =		THIS_MODULE,
	.llseek =		no_llseek,
	.unlocked_ioctl =	riowd_ioctl,
	.open =			riowd_open,
	.write =		riowd_write,
	.release =		riowd_release,
};

static struct miscdevice riowd_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &riowd_fops
};

static int __devinit riowd_probe(struct platform_device *op)
{
	struct riowd *p;
	int err = -EINVAL;

	if (riowd_device)
		goto out;

	err = -ENOMEM;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		goto out;

	spin_lock_init(&p->lock);

	p->regs = of_ioremap(&op->resource[0], 0, 2, DRIVER_NAME);
	if (!p->regs) {
		pr_err("Cannot map registers\n");
		goto out_free;
	}
	/* Make miscdev useable right away */
	riowd_device = p;

	err = misc_register(&riowd_miscdev);
	if (err) {
		pr_err("Cannot register watchdog misc device\n");
		goto out_iounmap;
	}

	pr_info("Hardware watchdog [%i minutes], regs at %p\n",
		riowd_timeout, p->regs);

	dev_set_drvdata(&op->dev, p);
	return 0;

out_iounmap:
	riowd_device = NULL;
	of_iounmap(&op->resource[0], p->regs, 2);

out_free:
	kfree(p);

out:
	return err;
}

static int __devexit riowd_remove(struct platform_device *op)
{
	struct riowd *p = dev_get_drvdata(&op->dev);

	misc_deregister(&riowd_miscdev);
	of_iounmap(&op->resource[0], p->regs, 2);
	kfree(p);

	return 0;
}

static const struct of_device_id riowd_match[] = {
	{
		.name = "pmc",
	},
	{},
};
MODULE_DEVICE_TABLE(of, riowd_match);

static struct platform_driver riowd_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = riowd_match,
	},
	.probe		= riowd_probe,
	.remove		= __devexit_p(riowd_remove),
};

module_platform_driver(riowd_driver);
