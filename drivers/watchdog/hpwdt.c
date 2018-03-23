/*
 *	HPE WatchDog Driver
 *	based on
 *
 *	SoftDog	0.05:	A Software Watchdog Device
 *
 *	(c) Copyright 2015 Hewlett Packard Enterprise Development LP
 *	Thomas Mingarelli <thomas.mingarelli@hpe.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <asm/nmi.h>

#define HPWDT_VERSION			"1.4.0"
#define SECS_TO_TICKS(secs)		((secs) * 1000 / 128)
#define TICKS_TO_SECS(ticks)		((ticks) * 128 / 1000)
#define HPWDT_MAX_TIMER			TICKS_TO_SECS(65535)
#define DEFAULT_MARGIN			30

static unsigned int soft_margin = DEFAULT_MARGIN;	/* in seconds */
static unsigned int reload;			/* the computed soft_margin */
static bool nowayout = WATCHDOG_NOWAYOUT;
#ifdef CONFIG_HPWDT_NMI_DECODING
static unsigned int allow_kdump = 1;
#endif
static char expect_release;
static unsigned long hpwdt_is_open;

static void __iomem *pci_mem_addr;		/* the PCI-memory address */
static unsigned long __iomem *hpwdt_nmistat;
static unsigned long __iomem *hpwdt_timer_reg;
static unsigned long __iomem *hpwdt_timer_con;

static const struct pci_device_id hpwdt_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_COMPAQ, 0xB203) },	/* iLO2 */
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, 0x3306) },	/* iLO3 */
	{0},			/* terminate list */
};
MODULE_DEVICE_TABLE(pci, hpwdt_devices);


/*
 *	Watchdog operations
 */
static void hpwdt_start(void)
{
	reload = SECS_TO_TICKS(soft_margin);
	iowrite16(reload, hpwdt_timer_reg);
	iowrite8(0x85, hpwdt_timer_con);
}

static void hpwdt_stop(void)
{
	unsigned long data;

	data = ioread8(hpwdt_timer_con);
	data &= 0xFE;
	iowrite8(data, hpwdt_timer_con);
}

static void hpwdt_ping(void)
{
	iowrite16(reload, hpwdt_timer_reg);
}

static int hpwdt_change_timer(int new_margin)
{
	if (new_margin < 1 || new_margin > HPWDT_MAX_TIMER) {
		pr_warn("New value passed in is invalid: %d seconds\n",
			new_margin);
		return -EINVAL;
	}

	soft_margin = new_margin;
	pr_debug("New timer passed in is %d seconds\n", new_margin);
	reload = SECS_TO_TICKS(soft_margin);

	return 0;
}

static int hpwdt_time_left(void)
{
	return TICKS_TO_SECS(ioread16(hpwdt_timer_reg));
}

#ifdef CONFIG_HPWDT_NMI_DECODING
static int hpwdt_my_nmi(void)
{
	return ioread8(hpwdt_nmistat) & 0x6;
}

/*
 *	NMI Handler
 */
static int hpwdt_pretimeout(unsigned int ulReason, struct pt_regs *regs)
{
	if ((ulReason == NMI_UNKNOWN) && !hpwdt_my_nmi())
		return NMI_DONE;

	if (allow_kdump)
		hpwdt_stop();

	nmi_panic(regs, "An NMI occurred. Depending on your system the reason "
		"for the NMI is logged in any one of the following "
		"resources:\n"
		"1. Integrated Management Log (IML)\n"
		"2. OA Syslog\n"
		"3. OA Forward Progress Log\n"
		"4. iLO Event Log");

	return NMI_HANDLED;
}
#endif /* CONFIG_HPWDT_NMI_DECODING */

/*
 *	/dev/watchdog handling
 */
static int hpwdt_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &hpwdt_is_open))
		return -EBUSY;

	/* Start the watchdog */
	hpwdt_start();
	hpwdt_ping();

	return nonseekable_open(inode, file);
}

static int hpwdt_release(struct inode *inode, struct file *file)
{
	/* Stop the watchdog */
	if (expect_release == 42) {
		hpwdt_stop();
	} else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		hpwdt_ping();
	}

	expect_release = 0;

	/* /dev/watchdog is being closed, make sure it can be re-opened */
	clear_bit(0, &hpwdt_is_open);

	return 0;
}

static ssize_t hpwdt_write(struct file *file, const char __user *data,
	size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			expect_release = 0;

			/* scan to see whether or not we got the magic char. */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		hpwdt_ping();
	}

	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT |
		   WDIOF_KEEPALIVEPING |
		   WDIOF_MAGICCLOSE,
	.identity = "HPE iLO2+ HW Watchdog Timer",
};

static long hpwdt_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_margin, options;
	int ret = -ENOTTY;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = 0;
		if (copy_to_user(argp, &ident, sizeof(ident)))
			ret = -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, p);
		break;

	case WDIOC_KEEPALIVE:
		hpwdt_ping();
		ret = 0;
		break;

	case WDIOC_SETOPTIONS:
		ret = get_user(options, p);
		if (ret)
			break;

		if (options & WDIOS_DISABLECARD)
			hpwdt_stop();

		if (options & WDIOS_ENABLECARD) {
			hpwdt_start();
			hpwdt_ping();
		}
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(new_margin, p);
		if (ret)
			break;

		ret = hpwdt_change_timer(new_margin);
		if (ret)
			break;

		hpwdt_ping();
		/* Fall */
	case WDIOC_GETTIMEOUT:
		ret = put_user(soft_margin, p);
		break;

	case WDIOC_GETTIMELEFT:
		ret = put_user(hpwdt_time_left(), p);
		break;
	}
	return ret;
}

/*
 *	Kernel interfaces
 */
static const struct file_operations hpwdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = hpwdt_write,
	.unlocked_ioctl = hpwdt_ioctl,
	.open = hpwdt_open,
	.release = hpwdt_release,
};

static struct miscdevice hpwdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &hpwdt_fops,
};

/*
 *	Init & Exit
 */


static int hpwdt_init_nmi_decoding(struct pci_dev *dev)
{
#ifdef CONFIG_HPWDT_NMI_DECODING
	int retval;
	/*
	 * Only one function can register for NMI_UNKNOWN
	 */
	retval = register_nmi_handler(NMI_UNKNOWN, hpwdt_pretimeout, 0, "hpwdt");
	if (retval)
		goto error;
	retval = register_nmi_handler(NMI_SERR, hpwdt_pretimeout, 0, "hpwdt");
	if (retval)
		goto error1;
	retval = register_nmi_handler(NMI_IO_CHECK, hpwdt_pretimeout, 0, "hpwdt");
	if (retval)
		goto error2;

	dev_info(&dev->dev,
			"HPE Watchdog Timer Driver: NMI decoding initialized"
			", allow kernel dump: %s (default = 1/ON)\n",
			(allow_kdump == 0) ? "OFF" : "ON");
	return 0;

error2:
	unregister_nmi_handler(NMI_SERR, "hpwdt");
error1:
	unregister_nmi_handler(NMI_UNKNOWN, "hpwdt");
error:
	dev_warn(&dev->dev,
		"Unable to register a die notifier (err=%d).\n",
		retval);
	return retval;
#endif	/* CONFIG_HPWDT_NMI_DECODING */
	return 0;
}

static void hpwdt_exit_nmi_decoding(void)
{
#ifdef CONFIG_HPWDT_NMI_DECODING
	unregister_nmi_handler(NMI_UNKNOWN, "hpwdt");
	unregister_nmi_handler(NMI_SERR, "hpwdt");
	unregister_nmi_handler(NMI_IO_CHECK, "hpwdt");
#endif
}

static int hpwdt_init_one(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	int retval;

	/*
	 * First let's find out if we are on an iLO2+ server. We will
	 * not run on a legacy ASM box.
	 * So we only support the G5 ProLiant servers and higher.
	 */
	if (dev->subsystem_vendor != PCI_VENDOR_ID_HP &&
	    dev->subsystem_vendor != PCI_VENDOR_ID_HP_3PAR) {
		dev_warn(&dev->dev,
			"This server does not have an iLO2+ ASIC.\n");
		return -ENODEV;
	}

	/*
	 * Ignore all auxilary iLO devices with the following PCI ID
	 */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_HP &&
	    dev->subsystem_device == 0x1979)
		return -ENODEV;

	if (pci_enable_device(dev)) {
		dev_warn(&dev->dev,
			"Not possible to enable PCI Device: 0x%x:0x%x.\n",
			ent->vendor, ent->device);
		return -ENODEV;
	}

	pci_mem_addr = pci_iomap(dev, 1, 0x80);
	if (!pci_mem_addr) {
		dev_warn(&dev->dev,
			"Unable to detect the iLO2+ server memory.\n");
		retval = -ENOMEM;
		goto error_pci_iomap;
	}
	hpwdt_nmistat	= pci_mem_addr + 0x6e;
	hpwdt_timer_reg = pci_mem_addr + 0x70;
	hpwdt_timer_con = pci_mem_addr + 0x72;

	/* Make sure that timer is disabled until /dev/watchdog is opened */
	hpwdt_stop();

	/* Make sure that we have a valid soft_margin */
	if (hpwdt_change_timer(soft_margin))
		hpwdt_change_timer(DEFAULT_MARGIN);

	/* Initialize NMI Decoding functionality */
	retval = hpwdt_init_nmi_decoding(dev);
	if (retval != 0)
		goto error_init_nmi_decoding;

	retval = misc_register(&hpwdt_miscdev);
	if (retval < 0) {
		dev_warn(&dev->dev,
			"Unable to register miscdev on minor=%d (err=%d).\n",
			WATCHDOG_MINOR, retval);
		goto error_misc_register;
	}

	dev_info(&dev->dev, "HPE Watchdog Timer Driver: %s"
			", timer margin: %d seconds (nowayout=%d).\n",
			HPWDT_VERSION, soft_margin, nowayout);
	return 0;

error_misc_register:
	hpwdt_exit_nmi_decoding();
error_init_nmi_decoding:
	pci_iounmap(dev, pci_mem_addr);
error_pci_iomap:
	pci_disable_device(dev);
	return retval;
}

static void hpwdt_exit(struct pci_dev *dev)
{
	if (!nowayout)
		hpwdt_stop();

	misc_deregister(&hpwdt_miscdev);
	hpwdt_exit_nmi_decoding();
	pci_iounmap(dev, pci_mem_addr);
	pci_disable_device(dev);
}

static struct pci_driver hpwdt_driver = {
	.name = "hpwdt",
	.id_table = hpwdt_devices,
	.probe = hpwdt_init_one,
	.remove = hpwdt_exit,
};

MODULE_AUTHOR("Tom Mingarelli");
MODULE_DESCRIPTION("hp watchdog driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(HPWDT_VERSION);

module_param(soft_margin, int, 0);
MODULE_PARM_DESC(soft_margin, "Watchdog timeout in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#ifdef CONFIG_HPWDT_NMI_DECODING
module_param(allow_kdump, int, 0);
MODULE_PARM_DESC(allow_kdump, "Start a kernel dump after NMI occurs");
#endif /* CONFIG_HPWDT_NMI_DECODING */

module_pci_driver(hpwdt_driver);
