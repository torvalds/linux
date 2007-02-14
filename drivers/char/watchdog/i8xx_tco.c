/*
 *	i8xx_tco:	TCO timer driver for i8xx chipsets
 *
 *	(c) Copyright 2000 kernel concepts <nils@kernelconcepts.de>, All Rights Reserved.
 *				http://www.kernelconcepts.de
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither kernel concepts nor Nils Faerber admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 2000	kernel concepts <nils@kernelconcepts.de>
 *				developed for
 *                              Jentro AG, Haar/Munich (Germany)
 *
 *	TCO timer driver for i8xx chipsets
 *	based on softdog.c by Alan Cox <alan@redhat.com>
 *
 *	The TCO timer is implemented in the following I/O controller hubs:
 *	(See the intel documentation on http://developer.intel.com.)
 *	82801AA  (ICH)    : document number 290655-003, 290677-014,
 *	82801AB  (ICHO)   : document number 290655-003, 290677-014,
 *	82801BA  (ICH2)   : document number 290687-002, 298242-027,
 *	82801BAM (ICH2-M) : document number 290687-002, 298242-027,
 *	82801CA  (ICH3-S) : document number 290733-003, 290739-013,
 *	82801CAM (ICH3-M) : document number 290716-001, 290718-007,
 *	82801DB  (ICH4)   : document number 290744-001, 290745-020,
 *	82801DBM (ICH4-M) : document number 252337-001, 252663-005,
 *	82801E   (C-ICH)  : document number 273599-001, 273645-002,
 *	82801EB  (ICH5)   : document number 252516-001, 252517-003,
 *	82801ER  (ICH5R)  : document number 252516-001, 252517-003,
 *
 *  20000710 Nils Faerber
 *	Initial Version 0.01
 *  20000728 Nils Faerber
 *	0.02 Fix for SMI_EN->TCO_EN bit, some cleanups
 *  20011214 Matt Domsch <Matt_Domsch@dell.com>
 *	0.03 Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *	     Didn't add timeout option as i810_margin already exists.
 *  20020224 Joel Becker, Wim Van Sebroeck
 *	0.04 Support for 82801CA(M) chipset, timer margin needs to be > 3,
 *	     add support for WDIOC_SETTIMEOUT and WDIOC_GETTIMEOUT.
 *  20020412 Rob Radez <rob@osinvestor.com>, Wim Van Sebroeck
 *	0.05 Fix possible timer_alive race, add expect close support,
 *	     clean up ioctls (WDIOC_GETSTATUS, WDIOC_GETBOOTSTATUS and
 *	     WDIOC_SETOPTIONS), made i810tco_getdevice __init,
 *	     removed boot_status, removed tco_timer_read,
 *	     added support for 82801DB and 82801E chipset,
 *	     added support for 82801EB and 8280ER chipset,
 *	     general cleanup.
 *  20030921 Wim Van Sebroeck <wim@iguana.be>
 *	0.06 change i810_margin to heartbeat, use module_param,
 *	     added notify system support, renamed module to i8xx_tco.
 *  20050128 Wim Van Sebroeck <wim@iguana.be>
 *	0.07 Added support for the ICH4-M, ICH6, ICH6R, ICH6-M, ICH6W and ICH6RW
 *	     chipsets. Also added support for the "undocumented" ICH7 chipset.
 *  20050807 Wim Van Sebroeck <wim@iguana.be>
 *	0.08 Make sure that the watchdog is only "armed" when started.
 *	     (Kernel Bug 4251)
 *  20060416 Wim Van Sebroeck <wim@iguana.be>
 *	0.09 Remove support for the ICH6, ICH6R, ICH6-M, ICH6W and ICH6RW and
 *	     ICH7 chipsets. (See Kernel Bug 6031 - other code will support these
 *	     chipsets)
 */

/*
 *	Includes, defines, variables, module parameters, ...
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/ioport.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include "i8xx_tco.h"

/* Module and version information */
#define TCO_VERSION "0.09"
#define TCO_MODULE_NAME "i8xx TCO timer"
#define TCO_DRIVER_NAME   TCO_MODULE_NAME ", v" TCO_VERSION
#define PFX TCO_MODULE_NAME ": "

/* internal variables */
static unsigned int ACPIBASE;
static spinlock_t tco_lock;	/* Guards the hardware */
static unsigned long timer_alive;
static char tco_expect_close;
static struct pci_dev *i8xx_tco_pci;

/* module parameters */
#define WATCHDOG_HEARTBEAT 30	/* 30 sec default heartbeat (2<heartbeat<39) */
static int heartbeat = WATCHDOG_HEARTBEAT;  /* in seconds */
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (2<heartbeat<39, default=" __MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 * Some TCO specific functions
 */

static inline unsigned char seconds_to_ticks(int seconds)
{
	/* the internal timer is stored as ticks which decrement
	 * every 0.6 seconds */
	return (seconds * 10) / 6;
}

static int tco_timer_start (void)
{
	unsigned char val;

	spin_lock(&tco_lock);

	/* disable chipset's NO_REBOOT bit */
	pci_read_config_byte (i8xx_tco_pci, 0xd4, &val);
	val &= 0xfd;
	pci_write_config_byte (i8xx_tco_pci, 0xd4, val);

	/* Bit 11: TCO Timer Halt -> 0 = The TCO timer is enabled to count */
	val = inb (TCO1_CNT + 1);
	val &= 0xf7;
	outb (val, TCO1_CNT + 1);
	val = inb (TCO1_CNT + 1);

	spin_unlock(&tco_lock);

	if (val & 0x08)
		return -1;
	return 0;
}

static int tco_timer_stop (void)
{
	unsigned char val, val1;

	spin_lock(&tco_lock);
	/* Bit 11: TCO Timer Halt -> 1 = The TCO timer is disabled */
	val = inb (TCO1_CNT + 1);
	val |= 0x08;
	outb (val, TCO1_CNT + 1);
	val = inb (TCO1_CNT + 1);

	/* Set the NO_REBOOT bit to prevent later reboots, just for sure */
	pci_read_config_byte (i8xx_tco_pci, 0xd4, &val1);
	val1 |= 0x02;
	pci_write_config_byte (i8xx_tco_pci, 0xd4, val1);

	spin_unlock(&tco_lock);

	if ((val & 0x08) == 0)
		return -1;
	return 0;
}

static int tco_timer_keepalive (void)
{
	spin_lock(&tco_lock);
	/* Reload the timer by writing to the TCO Timer Reload register */
	outb (0x01, TCO1_RLD);
	spin_unlock(&tco_lock);
	return 0;
}

static int tco_timer_set_heartbeat (int t)
{
	unsigned char val;
	unsigned char tmrval;

	tmrval = seconds_to_ticks(t);
	/* from the specs: */
	/* "Values of 0h-3h are ignored and should not be attempted" */
	if (tmrval > 0x3f || tmrval < 0x04)
		return -EINVAL;

	/* Write new heartbeat to watchdog */
	spin_lock(&tco_lock);
	val = inb (TCO1_TMR);
	val &= 0xc0;
	val |= tmrval;
	outb (val, TCO1_TMR);
	val = inb (TCO1_TMR);
	spin_unlock(&tco_lock);

	if ((val & 0x3f) != tmrval)
		return -EINVAL;

	heartbeat = t;
	return 0;
}

static int tco_timer_get_timeleft (int *time_left)
{
	unsigned char val;

	spin_lock(&tco_lock);

	/* read the TCO Timer */
	val = inb (TCO1_RLD);
	val &= 0x3f;

	spin_unlock(&tco_lock);

	*time_left = (int)((val * 6) / 10);

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static int i8xx_tco_open (struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &timer_alive))
		return -EBUSY;

	/*
	 *      Reload and activate timer
	 */
	tco_timer_keepalive ();
	tco_timer_start ();
	return nonseekable_open(inode, file);
}

static int i8xx_tco_release (struct inode *inode, struct file *file)
{
	/*
	 *      Shut off the timer.
	 */
	if (tco_expect_close == 42) {
		tco_timer_stop ();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		tco_timer_keepalive ();
	}
	clear_bit(0, &timer_alive);
	tco_expect_close = 0;
	return 0;
}

static ssize_t i8xx_tco_write (struct file *file, const char __user *data,
			      size_t len, loff_t * ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			tco_expect_close = 0;

			/* scan to see whether or not we got the magic character */
			for (i = 0; i != len; i++) {
				char c;
				if(get_user(c, data+i))
					return -EFAULT;
				if (c == 'V')
					tco_expect_close = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		tco_timer_keepalive ();
	}
	return len;
}

static int i8xx_tco_ioctl (struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	int new_options, retval = -EINVAL;
	int new_heartbeat;
	int time_left;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT |
					WDIOF_KEEPALIVEPING |
					WDIOF_MAGICCLOSE,
		.firmware_version =	0,
		.identity =		TCO_MODULE_NAME,
	};

	switch (cmd) {
		case WDIOC_GETSUPPORT:
			return copy_to_user(argp, &ident,
				sizeof (ident)) ? -EFAULT : 0;

		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user (0, p);

		case WDIOC_KEEPALIVE:
			tco_timer_keepalive ();
			return 0;

		case WDIOC_SETOPTIONS:
		{
			if (get_user (new_options, p))
				return -EFAULT;

			if (new_options & WDIOS_DISABLECARD) {
				tco_timer_stop ();
				retval = 0;
			}

			if (new_options & WDIOS_ENABLECARD) {
				tco_timer_keepalive ();
				tco_timer_start ();
				retval = 0;
			}

			return retval;
		}

		case WDIOC_SETTIMEOUT:
		{
			if (get_user(new_heartbeat, p))
				return -EFAULT;

			if (tco_timer_set_heartbeat(new_heartbeat))
				return -EINVAL;

			tco_timer_keepalive ();
			/* Fall */
		}

		case WDIOC_GETTIMEOUT:
			return put_user(heartbeat, p);

		case WDIOC_GETTIMELEFT:
		{
			if (tco_timer_get_timeleft(&time_left))
				return -EINVAL;

			return put_user(time_left, p);
		}

		default:
			return -ENOTTY;
	}
}

/*
 *	Notify system
 */

static int i8xx_tco_notify_sys (struct notifier_block *this, unsigned long code, void *unused)
{
	if (code==SYS_DOWN || code==SYS_HALT) {
		/* Turn the WDT off */
		tco_timer_stop ();
	}

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations i8xx_tco_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.write =	i8xx_tco_write,
	.ioctl =	i8xx_tco_ioctl,
	.open =		i8xx_tco_open,
	.release =	i8xx_tco_release,
};

static struct miscdevice i8xx_tco_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&i8xx_tco_fops,
};

static struct notifier_block i8xx_tco_notifier = {
	.notifier_call =	i8xx_tco_notify_sys,
};

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static struct pci_device_id i8xx_tco_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_10) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_12) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_12) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801E_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_1) },
	{ },			/* End of list */
};
MODULE_DEVICE_TABLE (pci, i8xx_tco_pci_tbl);

/*
 *	Init & exit routines
 */

static unsigned char __init i8xx_tco_getdevice (void)
{
	struct pci_dev *dev = NULL;
	u8 val1, val2;
	u16 badr;
	/*
	 *      Find the PCI device
	 */

	for_each_pci_dev(dev)
		if (pci_match_id(i8xx_tco_pci_tbl, dev)) {
			i8xx_tco_pci = dev;
			break;
		}

	if (i8xx_tco_pci) {
		/*
		 *      Find the ACPI base I/O address which is the base
		 *      for the TCO registers (TCOBASE=ACPIBASE + 0x60)
		 *      ACPIBASE is bits [15:7] from 0x40-0x43
		 */
		pci_read_config_byte (i8xx_tco_pci, 0x40, &val1);
		pci_read_config_byte (i8xx_tco_pci, 0x41, &val2);
		badr = ((val2 << 1) | (val1 >> 7)) << 7;
		ACPIBASE = badr;
		/* Something's wrong here, ACPIBASE has to be set */
		if (badr == 0x0001 || badr == 0x0000) {
			printk (KERN_ERR PFX "failed to get TCOBASE address\n");
			pci_dev_put(i8xx_tco_pci);
			return 0;
		}

		/* Check chipset's NO_REBOOT bit */
		pci_read_config_byte (i8xx_tco_pci, 0xd4, &val1);
		if (val1 & 0x02) {
			val1 &= 0xfd;
			pci_write_config_byte (i8xx_tco_pci, 0xd4, val1);
			pci_read_config_byte (i8xx_tco_pci, 0xd4, &val1);
			if (val1 & 0x02) {
				printk (KERN_ERR PFX "failed to reset NO_REBOOT flag, reboot disabled by hardware\n");
				pci_dev_put(i8xx_tco_pci);
				return 0;	/* Cannot reset NO_REBOOT bit */
			}
		}
		/* Disable reboots untill the watchdog starts */
		val1 |= 0x02;
		pci_write_config_byte (i8xx_tco_pci, 0xd4, val1);

		/* Set the TCO_EN bit in SMI_EN register */
		if (!request_region (SMI_EN + 1, 1, "i8xx TCO")) {
			printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
				SMI_EN + 1);
			pci_dev_put(i8xx_tco_pci);
			return 0;
		}
		val1 = inb (SMI_EN + 1);
		val1 &= 0xdf;
		outb (val1, SMI_EN + 1);
		release_region (SMI_EN + 1, 1);
		return 1;
	}
	return 0;
}

static int __init watchdog_init (void)
{
	int ret;

	spin_lock_init(&tco_lock);

	/* Check whether or not the hardware watchdog is there */
	if (!i8xx_tco_getdevice () || i8xx_tco_pci == NULL)
		return -ENODEV;

	if (!request_region (TCOBASE, 0x10, "i8xx TCO")) {
		printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
			TCOBASE);
		ret = -EIO;
		goto out;
	}

	/* Clear out the (probably old) status */
	outb (0, TCO1_STS);
	outb (3, TCO2_STS);

	/* Check that the heartbeat value is within it's range ; if not reset to the default */
	if (tco_timer_set_heartbeat (heartbeat)) {
		heartbeat = WATCHDOG_HEARTBEAT;
		tco_timer_set_heartbeat (heartbeat);
		printk(KERN_INFO PFX "heartbeat value must be 2<heartbeat<39, using %d\n",
			heartbeat);
	}

	ret = register_reboot_notifier(&i8xx_tco_notifier);
	if (ret != 0) {
		printk(KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		goto unreg_region;
	}

	ret = misc_register(&i8xx_tco_miscdev);
	if (ret != 0) {
		printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto unreg_notifier;
	}

	tco_timer_stop ();

	printk (KERN_INFO PFX "initialized (0x%04x). heartbeat=%d sec (nowayout=%d)\n",
		TCOBASE, heartbeat, nowayout);

	return 0;

unreg_notifier:
	unregister_reboot_notifier(&i8xx_tco_notifier);
unreg_region:
	release_region (TCOBASE, 0x10);
out:
	pci_dev_put(i8xx_tco_pci);
	return ret;
}

static void __exit watchdog_cleanup (void)
{
	/* Stop the timer before we leave */
	if (!nowayout)
		tco_timer_stop ();

	/* Deregister */
	misc_deregister (&i8xx_tco_miscdev);
	unregister_reboot_notifier(&i8xx_tco_notifier);
	release_region (TCOBASE, 0x10);

	pci_dev_put(i8xx_tco_pci);
}

module_init(watchdog_init);
module_exit(watchdog_cleanup);

MODULE_AUTHOR("Nils Faerber");
MODULE_DESCRIPTION("TCO timer driver for i8xx chipsets");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
