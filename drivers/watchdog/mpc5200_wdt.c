#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>
#include <asm/mpc52xx.h>


#define GPT_MODE_WDT		(1 << 15)
#define GPT_MODE_CE		(1 << 12)
#define GPT_MODE_MS_TIMER	(0x4)


struct mpc5200_wdt {
	unsigned count;	/* timer ticks before watchdog kicks in */
	long ipb_freq;
	struct miscdevice miscdev;
	struct resource mem;
	struct mpc52xx_gpt __iomem *regs;
	spinlock_t io_lock;
};

/* is_active stores wether or not the /dev/watchdog device is opened */
static unsigned long is_active;

/* misc devices don't provide a way, to get back to 'dev' or 'miscdev' from
 * file operations, which sucks. But there can be max 1 watchdog anyway, so...
 */
static struct mpc5200_wdt *wdt_global;


/* helper to calculate timeout in timer counts */
static void mpc5200_wdt_set_timeout(struct mpc5200_wdt *wdt, int timeout)
{
	/* use biggest prescaler of 64k */
	wdt->count = (wdt->ipb_freq + 0xffff) / 0x10000 * timeout;

	if (wdt->count > 0xffff)
		wdt->count = 0xffff;
}
/* return timeout in seconds (calculated from timer count) */
static int mpc5200_wdt_get_timeout(struct mpc5200_wdt *wdt)
{
	return wdt->count * 0x10000 / wdt->ipb_freq;
}


/* watchdog operations */
static int mpc5200_wdt_start(struct mpc5200_wdt *wdt)
{
	spin_lock(&wdt->io_lock);
	/* disable */
	out_be32(&wdt->regs->mode, 0);
	/* set timeout, with maximum prescaler */
	out_be32(&wdt->regs->count, 0x0 | wdt->count);
	/* enable watchdog */
	out_be32(&wdt->regs->mode, GPT_MODE_CE | GPT_MODE_WDT |
						GPT_MODE_MS_TIMER);
	spin_unlock(&wdt->io_lock);

	return 0;
}
static int mpc5200_wdt_ping(struct mpc5200_wdt *wdt)
{
	spin_lock(&wdt->io_lock);
	/* writing A5 to OCPW resets the watchdog */
	out_be32(&wdt->regs->mode, 0xA5000000 |
				(0xffffff & in_be32(&wdt->regs->mode)));
	spin_unlock(&wdt->io_lock);
	return 0;
}
static int mpc5200_wdt_stop(struct mpc5200_wdt *wdt)
{
	spin_lock(&wdt->io_lock);
	/* disable */
	out_be32(&wdt->regs->mode, 0);
	spin_unlock(&wdt->io_lock);
	return 0;
}


/* file operations */
static ssize_t mpc5200_wdt_write(struct file *file, const char __user *data,
		size_t len, loff_t *ppos)
{
	struct mpc5200_wdt *wdt = file->private_data;
	mpc5200_wdt_ping(wdt);
	return 0;
}
static struct watchdog_info mpc5200_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity	= "mpc5200 watchdog on GPT0",
};
static long mpc5200_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct mpc5200_wdt *wdt = file->private_data;
	int __user *data = (int __user *)arg;
	int timeout;
	int ret = 0;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user(data, &mpc5200_wdt_info,
						sizeof(mpc5200_wdt_info));
		if (ret)
			ret = -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, data);
		break;

	case WDIOC_KEEPALIVE:
		mpc5200_wdt_ping(wdt);
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(timeout, data);
		if (ret)
			break;
		mpc5200_wdt_set_timeout(wdt, timeout);
		mpc5200_wdt_start(wdt);
		/* fall through and return the timeout */

	case WDIOC_GETTIMEOUT:
		timeout = mpc5200_wdt_get_timeout(wdt);
		ret = put_user(timeout, data);
		break;

	default:
		ret = -ENOTTY;
	}
	return ret;
}

static int mpc5200_wdt_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &is_active))
		return -EBUSY;

	/* Set and activate the watchdog */
	mpc5200_wdt_set_timeout(wdt_global, 30);
	mpc5200_wdt_start(wdt_global);
	file->private_data = wdt_global;
	return nonseekable_open(inode, file);
}
static int mpc5200_wdt_release(struct inode *inode, struct file *file)
{
#if WATCHDOG_NOWAYOUT == 0
	struct mpc5200_wdt *wdt = file->private_data;
	mpc5200_wdt_stop(wdt);
	wdt->count = 0;		/* == disabled */
#endif
	clear_bit(0, &is_active);
	return 0;
}

static const struct file_operations mpc5200_wdt_fops = {
	.owner	= THIS_MODULE,
	.write	= mpc5200_wdt_write,
	.unlocked_ioctl	= mpc5200_wdt_ioctl,
	.open	= mpc5200_wdt_open,
	.release = mpc5200_wdt_release,
};

/* module operations */
static int mpc5200_wdt_probe(struct of_device *op,
					const struct of_device_id *match)
{
	struct mpc5200_wdt *wdt;
	int err;
	const void *has_wdt;
	int size;

	has_wdt = of_get_property(op->node, "has-wdt", NULL);
	if (!has_wdt)
		has_wdt = of_get_property(op->node, "fsl,has-wdt", NULL);
	if (!has_wdt)
		return -ENODEV;

	wdt = kzalloc(sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->ipb_freq = mpc5xxx_get_bus_frequency(op->node);

	err = of_address_to_resource(op->node, 0, &wdt->mem);
	if (err)
		goto out_free;
	size = wdt->mem.end - wdt->mem.start + 1;
	if (!request_mem_region(wdt->mem.start, size, "mpc5200_wdt")) {
		err = -ENODEV;
		goto out_free;
	}
	wdt->regs = ioremap(wdt->mem.start, size);
	if (!wdt->regs) {
		err = -ENODEV;
		goto out_release;
	}

	dev_set_drvdata(&op->dev, wdt);
	spin_lock_init(&wdt->io_lock);

	wdt->miscdev = (struct miscdevice) {
		.minor	= WATCHDOG_MINOR,
		.name	= "watchdog",
		.fops	= &mpc5200_wdt_fops,
		.parent = &op->dev,
	};
	wdt_global = wdt;
	err = misc_register(&wdt->miscdev);
	if (!err)
		return 0;

	iounmap(wdt->regs);
out_release:
	release_mem_region(wdt->mem.start, size);
out_free:
	kfree(wdt);
	return err;
}

static int mpc5200_wdt_remove(struct of_device *op)
{
	struct mpc5200_wdt *wdt = dev_get_drvdata(&op->dev);

	mpc5200_wdt_stop(wdt);
	misc_deregister(&wdt->miscdev);
	iounmap(wdt->regs);
	release_mem_region(wdt->mem.start, wdt->mem.end - wdt->mem.start + 1);
	kfree(wdt);

	return 0;
}
static int mpc5200_wdt_suspend(struct of_device *op, pm_message_t state)
{
	struct mpc5200_wdt *wdt = dev_get_drvdata(&op->dev);
	mpc5200_wdt_stop(wdt);
	return 0;
}
static int mpc5200_wdt_resume(struct of_device *op)
{
	struct mpc5200_wdt *wdt = dev_get_drvdata(&op->dev);
	if (wdt->count)
		mpc5200_wdt_start(wdt);
	return 0;
}
static int mpc5200_wdt_shutdown(struct of_device *op)
{
	struct mpc5200_wdt *wdt = dev_get_drvdata(&op->dev);
	mpc5200_wdt_stop(wdt);
	return 0;
}

static struct of_device_id mpc5200_wdt_match[] = {
	{ .compatible = "mpc5200-gpt", },
	{ .compatible = "fsl,mpc5200-gpt", },
	{},
};
static struct of_platform_driver mpc5200_wdt_driver = {
	.owner		= THIS_MODULE,
	.name		= "mpc5200-gpt-wdt",
	.match_table	= mpc5200_wdt_match,
	.probe		= mpc5200_wdt_probe,
	.remove		= mpc5200_wdt_remove,
	.suspend	= mpc5200_wdt_suspend,
	.resume		= mpc5200_wdt_resume,
	.shutdown	= mpc5200_wdt_shutdown,
};


static int __init mpc5200_wdt_init(void)
{
	return of_register_platform_driver(&mpc5200_wdt_driver);
}

static void __exit mpc5200_wdt_exit(void)
{
	of_unregister_platform_driver(&mpc5200_wdt_driver);
}

module_init(mpc5200_wdt_init);
module_exit(mpc5200_wdt_exit);

MODULE_AUTHOR("Domen Puncer <domen.puncer@telargo.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
