/*
 * Watchdog implementation based on z/VM Watchdog Timer API
 *
 * The user space watchdog daemon can use this driver as
 * /dev/vmwatchdog to have z/VM execute the specified CP
 * command when the timeout expires. The default command is
 * "IPL", which which cause an immediate reboot.
 */
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/watchdog.h>

#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define MAX_CMDLEN 240
#define MIN_INTERVAL 15
static char vmwdt_cmd[MAX_CMDLEN] = "IPL";
static int vmwdt_conceal;

static int vmwdt_nowayout = WATCHDOG_NOWAYOUT;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
MODULE_DESCRIPTION("z/VM Watchdog Timer");
module_param_string(cmd, vmwdt_cmd, MAX_CMDLEN, 0644);
MODULE_PARM_DESC(cmd, "CP command that is run when the watchdog triggers");
module_param_named(conceal, vmwdt_conceal, bool, 0644);
MODULE_PARM_DESC(conceal, "Enable the CONCEAL CP option while the watchdog "
		" is active");
module_param_named(nowayout, vmwdt_nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started"
		" (default=CONFIG_WATCHDOG_NOWAYOUT)");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

static unsigned int vmwdt_interval = 60;
static unsigned long vmwdt_is_open;
static int vmwdt_expect_close;

enum vmwdt_func {
	/* function codes */
	wdt_init   = 0,
	wdt_change = 1,
	wdt_cancel = 2,
	/* flags */
	wdt_conceal = 0x80000000,
};

static int __diag288(enum vmwdt_func func, unsigned int timeout,
			    char *cmd, size_t len)
{
	register unsigned long __func asm("2") = func;
	register unsigned long __timeout asm("3") = timeout;
	register unsigned long __cmdp asm("4") = virt_to_phys(cmd);
	register unsigned long __cmdl asm("5") = len;
	int err;

	err = -EINVAL;
	asm volatile(
		"	diag	%1,%3,0x288\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (err) : "d"(__func), "d"(__timeout),
		  "d"(__cmdp), "d"(__cmdl) : "1", "cc");
	return err;
}

static int vmwdt_keepalive(void)
{
	/* we allocate new memory every time to avoid having
	 * to track the state. static allocation is not an
	 * option since that might not be contiguous in real
	 * storage in case of a modular build */
	static char *ebc_cmd;
	size_t len;
	int ret;
	unsigned int func;

	ebc_cmd = kmalloc(MAX_CMDLEN, GFP_KERNEL);
	if (!ebc_cmd)
		return -ENOMEM;

	len = strlcpy(ebc_cmd, vmwdt_cmd, MAX_CMDLEN);
	ASCEBC(ebc_cmd, MAX_CMDLEN);
	EBC_TOUPPER(ebc_cmd, MAX_CMDLEN);

	func = vmwdt_conceal ? (wdt_init | wdt_conceal) : wdt_init;
	ret = __diag288(func, vmwdt_interval, ebc_cmd, len);
	kfree(ebc_cmd);

	if (ret) {
		printk(KERN_WARNING "%s: problem setting interval %d, "
			"cmd %s\n", __func__, vmwdt_interval,
			vmwdt_cmd);
	}
	return ret;
}

static int vmwdt_disable(void)
{
	int ret = __diag288(wdt_cancel, 0, "", 0);
	if (ret) {
		printk(KERN_WARNING "%s: problem disabling watchdog\n",
			__func__);
	}
	return ret;
}

static int __init vmwdt_probe(void)
{
	/* there is no real way to see if the watchdog is supported,
	 * so we try initializing it with a NOP command ("BEGIN")
	 * that won't cause any harm even if the following disable
	 * fails for some reason */
	static char __initdata ebc_begin[] = {
		194, 197, 199, 201, 213
	};
	if (__diag288(wdt_init, 15, ebc_begin, sizeof(ebc_begin)) != 0) {
		printk(KERN_INFO "z/VM watchdog not available\n");
		return -EINVAL;
	}
	return vmwdt_disable();
}

static int vmwdt_open(struct inode *i, struct file *f)
{
	int ret;
	if (test_and_set_bit(0, &vmwdt_is_open))
		return -EBUSY;
	ret = vmwdt_keepalive();
	if (ret)
		clear_bit(0, &vmwdt_is_open);
	return ret ? ret : nonseekable_open(i, f);
}

static int vmwdt_close(struct inode *i, struct file *f)
{
	if (vmwdt_expect_close == 42)
		vmwdt_disable();
	vmwdt_expect_close = 0;
	clear_bit(0, &vmwdt_is_open);
	return 0;
}

static struct watchdog_info vmwdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.firmware_version = 0,
	.identity = "z/VM Watchdog Timer",
};

static int vmwdt_ioctl(struct inode *i, struct file *f,
			  unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user((void __user *)arg, &vmwdt_info,
					sizeof(vmwdt_info)))
			return -EFAULT;
		return 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int __user *)arg);
	case WDIOC_GETTEMP:
		return -EINVAL;
	case WDIOC_SETOPTIONS:
		{
			int options, ret;
			if (get_user(options, (int __user *)arg))
				return -EFAULT;
			ret = -EINVAL;
			if (options & WDIOS_DISABLECARD) {
				ret = vmwdt_disable();
				if (ret)
					return ret;
			}
			if (options & WDIOS_ENABLECARD) {
				ret = vmwdt_keepalive();
			}
			return ret;
		}
	case WDIOC_GETTIMEOUT:
		return put_user(vmwdt_interval, (int __user *)arg);
	case WDIOC_SETTIMEOUT:
		{
			int interval;
			if (get_user(interval, (int __user *)arg))
				return -EFAULT;
			if (interval < MIN_INTERVAL)
				return -EINVAL;
			vmwdt_interval = interval;
		}
		return vmwdt_keepalive();
	case WDIOC_KEEPALIVE:
		return vmwdt_keepalive();
	}

	return -EINVAL;
}

static ssize_t vmwdt_write(struct file *f, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if(count) {
		if (!vmwdt_nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			vmwdt_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf+i))
					return -EFAULT;
				if (c == 'V')
					vmwdt_expect_close = 42;
			}
		}
		/* someone wrote to us, we should restart timer */
		vmwdt_keepalive();
	}
	return count;
}

static const struct file_operations vmwdt_fops = {
	.open    = &vmwdt_open,
	.release = &vmwdt_close,
	.ioctl   = &vmwdt_ioctl,
	.write   = &vmwdt_write,
	.owner   = THIS_MODULE,
};

static struct miscdevice vmwdt_dev = {
	.minor      = WATCHDOG_MINOR,
	.name       = "watchdog",
	.fops       = &vmwdt_fops,
};

static int __init vmwdt_init(void)
{
	int ret;

	ret = vmwdt_probe();
	if (ret)
		return ret;
	return misc_register(&vmwdt_dev);
}
module_init(vmwdt_init);

static void __exit vmwdt_exit(void)
{
	WARN_ON(misc_deregister(&vmwdt_dev) != 0);
}
module_exit(vmwdt_exit);
