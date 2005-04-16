/*
 * mpc8xx_wdt.c - MPC8xx watchdog userspace interface
 *
 * Author: Florian Schirmer <jolt@tuxbox.org>
 *
 * 2002 (c) Florian Schirmer <jolt@tuxbox.org> This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <asm/8xx_immap.h>
#include <asm/uaccess.h>
#include <syslib/m8xx_wdt.h>

static unsigned long wdt_opened;
static int wdt_status;

static void mpc8xx_wdt_handler_disable(void)
{
	volatile immap_t *imap = (volatile immap_t *)IMAP_ADDR;

	imap->im_sit.sit_piscr &= ~(PISCR_PIE | PISCR_PTE);

	printk(KERN_NOTICE "mpc8xx_wdt: keep-alive handler deactivated\n");
}

static void mpc8xx_wdt_handler_enable(void)
{
	volatile immap_t *imap = (volatile immap_t *)IMAP_ADDR;

	imap->im_sit.sit_piscr |= PISCR_PIE | PISCR_PTE;

	printk(KERN_NOTICE "mpc8xx_wdt: keep-alive handler activated\n");
}

static int mpc8xx_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &wdt_opened))
		return -EBUSY;

	m8xx_wdt_reset();
	mpc8xx_wdt_handler_disable();

	return 0;
}

static int mpc8xx_wdt_release(struct inode *inode, struct file *file)
{
	m8xx_wdt_reset();

#if !defined(CONFIG_WATCHDOG_NOWAYOUT)
	mpc8xx_wdt_handler_enable();
#endif

	clear_bit(0, &wdt_opened);

	return 0;
}

static ssize_t mpc8xx_wdt_write(struct file *file, const char *data, size_t len,
				loff_t * ppos)
{
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (len)
		m8xx_wdt_reset();

	return len;
}

static int mpc8xx_wdt_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	int timeout;
	static struct watchdog_info info = {
		.options = WDIOF_KEEPALIVEPING,
		.firmware_version = 0,
		.identity = "MPC8xx watchdog",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		if (put_user(wdt_status, (int *)arg))
			return -EFAULT;
		wdt_status &= ~WDIOF_KEEPALIVEPING;
		break;

	case WDIOC_GETTEMP:
		return -EOPNOTSUPP;

	case WDIOC_SETOPTIONS:
		return -EOPNOTSUPP;

	case WDIOC_KEEPALIVE:
		m8xx_wdt_reset();
		wdt_status |= WDIOF_KEEPALIVEPING;
		break;

	case WDIOC_SETTIMEOUT:
		return -EOPNOTSUPP;

	case WDIOC_GETTIMEOUT:
		timeout = m8xx_wdt_get_timeout();
		if (put_user(timeout, (int *)arg))
			return -EFAULT;
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static struct file_operations mpc8xx_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = mpc8xx_wdt_write,
	.ioctl = mpc8xx_wdt_ioctl,
	.open = mpc8xx_wdt_open,
	.release = mpc8xx_wdt_release,
};

static struct miscdevice mpc8xx_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &mpc8xx_wdt_fops,
};

static int __init mpc8xx_wdt_init(void)
{
	return misc_register(&mpc8xx_wdt_miscdev);
}

static void __exit mpc8xx_wdt_exit(void)
{
	misc_deregister(&mpc8xx_wdt_miscdev);

	m8xx_wdt_reset();
	mpc8xx_wdt_handler_enable();
}

module_init(mpc8xx_wdt_init);
module_exit(mpc8xx_wdt_exit);

MODULE_AUTHOR("Florian Schirmer <jolt@tuxbox.org>");
MODULE_DESCRIPTION("MPC8xx watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
