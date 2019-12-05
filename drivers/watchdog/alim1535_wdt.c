// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Watchdog for the 7101 PMU version found in the ALi M1535 chipsets
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#define WATCHDOG_NAME "ALi_M1535"
#define WATCHDOG_TIMEOUT 60	/* 60 sec default timeout */

/* internal variables */
static unsigned long ali_is_open;
static char ali_expect_release;
static struct pci_dev *ali_pci;
static u32 ali_timeout_bits;		/* stores the computed timeout */
static DEFINE_SPINLOCK(ali_lock);	/* Guards the hardware */

/* module parameters */
static int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in seconds. (0 < timeout < 18000, default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 *	ali_start	-	start watchdog countdown
 *
 *	Starts the timer running providing the timer has a counter
 *	configuration set.
 */

static void ali_start(void)
{
	u32 val;

	spin_lock(&ali_lock);

	pci_read_config_dword(ali_pci, 0xCC, &val);
	val &= ~0x3F;	/* Mask count */
	val |= (1 << 25) | ali_timeout_bits;
	pci_write_config_dword(ali_pci, 0xCC, val);

	spin_unlock(&ali_lock);
}

/*
 *	ali_stop	-	stop the timer countdown
 *
 *	Stop the ALi watchdog countdown
 */

static void ali_stop(void)
{
	u32 val;

	spin_lock(&ali_lock);

	pci_read_config_dword(ali_pci, 0xCC, &val);
	val &= ~0x3F;		/* Mask count to zero (disabled) */
	val &= ~(1 << 25);	/* and for safety mask the reset enable */
	pci_write_config_dword(ali_pci, 0xCC, val);

	spin_unlock(&ali_lock);
}

/*
 *	ali_keepalive	-	send a keepalive to the watchdog
 *
 *	Send a keepalive to the timer (actually we restart the timer).
 */

static void ali_keepalive(void)
{
	ali_start();
}

/*
 *	ali_settimer	-	compute the timer reload value
 *	@t: time in seconds
 *
 *	Computes the timeout values needed
 */

static int ali_settimer(int t)
{
	if (t < 0)
		return -EINVAL;
	else if (t < 60)
		ali_timeout_bits = t|(1 << 6);
	else if (t < 3600)
		ali_timeout_bits = (t / 60)|(1 << 7);
	else if (t < 18000)
		ali_timeout_bits = (t / 300)|(1 << 6)|(1 << 7);
	else
		return -EINVAL;

	timeout = t;
	return 0;
}

/*
 *	/dev/watchdog handling
 */

/*
 *	ali_write	-	writes to ALi watchdog
 *	@file: file from VFS
 *	@data: user address of data
 *	@len: length of data
 *	@ppos: pointer to the file offset
 *
 *	Handle a write to the ALi watchdog. Writing to the file pings
 *	the watchdog and resets it. Writing the magic 'V' sequence allows
 *	the next close to turn off the watchdog.
 */

static ssize_t ali_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the
			   magic character five months ago... */
			ali_expect_release = 0;

			/* scan to see whether or not we got
			   the magic character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					ali_expect_release = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		ali_start();
	}
	return len;
}

/*
 *	ali_ioctl	-	handle watchdog ioctls
 *	@file: VFS file pointer
 *	@cmd: ioctl number
 *	@arg: arguments to the ioctl
 *
 *	Handle the watchdog ioctls supported by the ALi driver. Really
 *	we want an extension to enable irq ack monitoring and the like
 */

static long ali_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options =		WDIOF_KEEPALIVEPING |
					WDIOF_SETTIMEOUT |
					WDIOF_MAGICCLOSE,
		.firmware_version =	0,
		.identity =		"ALi M1535 WatchDog Timer",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_SETOPTIONS:
	{
		int new_options, retval = -EINVAL;

		if (get_user(new_options, p))
			return -EFAULT;
		if (new_options & WDIOS_DISABLECARD) {
			ali_stop();
			retval = 0;
		}
		if (new_options & WDIOS_ENABLECARD) {
			ali_start();
			retval = 0;
		}
		return retval;
	}
	case WDIOC_KEEPALIVE:
		ali_keepalive();
		return 0;
	case WDIOC_SETTIMEOUT:
	{
		int new_timeout;
		if (get_user(new_timeout, p))
			return -EFAULT;
		if (ali_settimer(new_timeout))
			return -EINVAL;
		ali_keepalive();
	}
		/* fall through */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);
	default:
		return -ENOTTY;
	}
}

/*
 *	ali_open	-	handle open of ali watchdog
 *	@inode: inode from VFS
 *	@file: file from VFS
 *
 *	Open the ALi watchdog device. Ensure only one person opens it
 *	at a time. Also start the watchdog running.
 */

static int ali_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &ali_is_open))
		return -EBUSY;

	/* Activate */
	ali_start();
	return stream_open(inode, file);
}

/*
 *	ali_release	-	close an ALi watchdog
 *	@inode: inode from VFS
 *	@file: file from VFS
 *
 *	Close the ALi watchdog device. Actual shutdown of the timer
 *	only occurs if the magic sequence has been set.
 */

static int ali_release(struct inode *inode, struct file *file)
{
	/*
	 *      Shut off the timer.
	 */
	if (ali_expect_release == 42)
		ali_stop();
	else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		ali_keepalive();
	}
	clear_bit(0, &ali_is_open);
	ali_expect_release = 0;
	return 0;
}

/*
 *	ali_notify_sys	-	System down notifier
 *
 *	Notifier for system down
 */


static int ali_notify_sys(struct notifier_block *this,
					unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		ali_stop();		/* Turn the WDT off */
	return NOTIFY_DONE;
}

/*
 *	Data for PCI driver interface
 *
 *	This data only exists for exporting the supported
 *	PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 *	register a pci_driver, because someone else might one day
 *	want to register another driver on the same PCI id.
 */

static const struct pci_device_id ali_pci_tbl[] __used = {
	{ PCI_VENDOR_ID_AL, 0x1533, PCI_ANY_ID, PCI_ANY_ID,},
	{ PCI_VENDOR_ID_AL, 0x1535, PCI_ANY_ID, PCI_ANY_ID,},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, ali_pci_tbl);

/*
 *	ali_find_watchdog	-	find a 1535 and 7101
 *
 *	Scans the PCI hardware for a 1535 series bridge and matching 7101
 *	watchdog device. This may be overtight but it is better to be safe
 */

static int __init ali_find_watchdog(void)
{
	struct pci_dev *pdev;
	u32 wdog;

	/* Check for a 1533/1535 series bridge */
	pdev = pci_get_device(PCI_VENDOR_ID_AL, 0x1535, NULL);
	if (pdev == NULL)
		pdev = pci_get_device(PCI_VENDOR_ID_AL, 0x1533, NULL);
	if (pdev == NULL)
		return -ENODEV;
	pci_dev_put(pdev);

	/* Check for the a 7101 PMU */
	pdev = pci_get_device(PCI_VENDOR_ID_AL, 0x7101, NULL);
	if (pdev == NULL)
		return -ENODEV;

	if (pci_enable_device(pdev)) {
		pci_dev_put(pdev);
		return -EIO;
	}

	ali_pci = pdev;

	/*
	 *	Initialize the timer bits
	 */
	pci_read_config_dword(pdev, 0xCC, &wdog);

	/* Timer bits */
	wdog &= ~0x3F;
	/* Issued events */
	wdog &= ~((1 << 27)|(1 << 26)|(1 << 25)|(1 << 24));
	/* No monitor bits */
	wdog &= ~((1 << 16)|(1 << 13)|(1 << 12)|(1 << 11)|(1 << 10)|(1 << 9));

	pci_write_config_dword(pdev, 0xCC, wdog);

	return 0;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations ali_fops = {
	.owner		=	THIS_MODULE,
	.llseek		=	no_llseek,
	.write		=	ali_write,
	.unlocked_ioctl =	ali_ioctl,
	.compat_ioctl	= 	compat_ptr_ioctl,
	.open		=	ali_open,
	.release	=	ali_release,
};

static struct miscdevice ali_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&ali_fops,
};

static struct notifier_block ali_notifier = {
	.notifier_call =	ali_notify_sys,
};

/*
 *	watchdog_init	-	module initialiser
 *
 *	Scan for a suitable watchdog and if so initialize it. Return an error
 *	if we cannot, the error causes the module to unload
 */

static int __init watchdog_init(void)
{
	int ret;

	/* Check whether or not the hardware watchdog is there */
	if (ali_find_watchdog() != 0)
		return -ENODEV;

	/* Check that the timeout value is within it's range;
	   if not reset to the default */
	if (timeout < 1 || timeout >= 18000) {
		timeout = WATCHDOG_TIMEOUT;
		pr_info("timeout value must be 0 < timeout < 18000, using %d\n",
			timeout);
	}

	/* Calculate the watchdog's timeout */
	ali_settimer(timeout);

	ret = register_reboot_notifier(&ali_notifier);
	if (ret != 0) {
		pr_err("cannot register reboot notifier (err=%d)\n", ret);
		goto out;
	}

	ret = misc_register(&ali_miscdev);
	if (ret != 0) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, ret);
		goto unreg_reboot;
	}

	pr_info("initialized. timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

out:
	return ret;
unreg_reboot:
	unregister_reboot_notifier(&ali_notifier);
	goto out;
}

/*
 *	watchdog_exit	-	module de-initialiser
 *
 *	Called while unloading a successfully installed watchdog module.
 */

static void __exit watchdog_exit(void)
{
	/* Stop the timer before we leave */
	ali_stop();

	/* Deregister */
	misc_deregister(&ali_miscdev);
	unregister_reboot_notifier(&ali_notifier);
	pci_dev_put(ali_pci);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("ALi M1535 PMU Watchdog Timer driver");
MODULE_LICENSE("GPL");
