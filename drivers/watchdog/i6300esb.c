/*
 *	i6300esb:	Watchdog timer driver for Intel 6300ESB chipset
 *
 *	(c) Copyright 2004 Google Inc.
 *	(c) Copyright 2005 David Härdeman <david@2gen.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *      based on i810-tco.c which is in turn based on softdog.c
 *
 * 	The timer is implemented in the following I/O controller hubs:
 * 	(See the intel documentation on http://developer.intel.com.)
 * 	6300ESB chip : document number 300641-003
 *
 *  2004YYZZ Ross Biro
 *	Initial version 0.01
 *  2004YYZZ Ross Biro
 *  	Version 0.02
 *  20050210 David Härdeman <david@2gen.com>
 *      Ported driver to kernel 2.6
 */

/*
 *      Includes, defines, variables, module parameters, ...
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ioport.h>

#include <asm/uaccess.h>
#include <asm/io.h>

/* Module and version information */
#define ESB_VERSION "0.03"
#define ESB_MODULE_NAME "i6300ESB timer"
#define ESB_DRIVER_NAME ESB_MODULE_NAME ", v" ESB_VERSION
#define PFX ESB_MODULE_NAME ": "

/* PCI configuration registers */
#define ESB_CONFIG_REG  0x60            /* Config register                   */
#define ESB_LOCK_REG    0x68            /* WDT lock register                 */

/* Memory mapped registers */
#define ESB_TIMER1_REG  BASEADDR + 0x00 /* Timer1 value after each reset     */
#define ESB_TIMER2_REG  BASEADDR + 0x04 /* Timer2 value after each reset     */
#define ESB_GINTSR_REG  BASEADDR + 0x08 /* General Interrupt Status Register */
#define ESB_RELOAD_REG  BASEADDR + 0x0c /* Reload register                   */

/* Lock register bits */
#define ESB_WDT_FUNC    ( 0x01 << 2 )   /* Watchdog functionality            */
#define ESB_WDT_ENABLE  ( 0x01 << 1 )   /* Enable WDT                        */
#define ESB_WDT_LOCK    ( 0x01 << 0 )   /* Lock (nowayout)                   */

/* Config register bits */
#define ESB_WDT_REBOOT  ( 0x01 << 5 )   /* Enable reboot on timeout          */
#define ESB_WDT_FREQ    ( 0x01 << 2 )   /* Decrement frequency               */
#define ESB_WDT_INTTYPE ( 0x11 << 0 )   /* Interrupt type on timer1 timeout  */

/* Reload register bits */
#define ESB_WDT_RELOAD ( 0x01 << 8 )    /* prevent timeout                   */

/* Magic constants */
#define ESB_UNLOCK1     0x80            /* Step 1 to unlock reset registers  */
#define ESB_UNLOCK2     0x86            /* Step 2 to unlock reset registers  */

/* internal variables */
static void __iomem *BASEADDR;
static DEFINE_SPINLOCK(esb_lock); /* Guards the hardware */
static unsigned long timer_alive;
static struct pci_dev *esb_pci;
static unsigned short triggered; /* The status of the watchdog upon boot */
static char esb_expect_close;

/* module parameters */
#define WATCHDOG_HEARTBEAT 30   /* 30 sec default heartbeat (1<heartbeat<2*1023) */
static int heartbeat = WATCHDOG_HEARTBEAT;  /* in seconds */
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (1<heartbeat<2046, default=" __MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 * Some i6300ESB specific functions
 */

/*
 * Prepare for reloading the timer by unlocking the proper registers.
 * This is performed by first writing 0x80 followed by 0x86 to the
 * reload register. After this the appropriate registers can be written
 * to once before they need to be unlocked again.
 */
static inline void esb_unlock_registers(void) {
        writeb(ESB_UNLOCK1, ESB_RELOAD_REG);
        writeb(ESB_UNLOCK2, ESB_RELOAD_REG);
}

static void esb_timer_start(void)
{
	u8 val;

	/* Enable or Enable + Lock? */
	val = 0x02 | (nowayout ? 0x01 : 0x00);

        pci_write_config_byte(esb_pci, ESB_LOCK_REG, val);
}

static int esb_timer_stop(void)
{
	u8 val;

	spin_lock(&esb_lock);
	/* First, reset timers as suggested by the docs */
	esb_unlock_registers();
	writew(ESB_WDT_RELOAD, ESB_RELOAD_REG);
	/* Then disable the WDT */
	pci_write_config_byte(esb_pci, ESB_LOCK_REG, 0x0);
	pci_read_config_byte(esb_pci, ESB_LOCK_REG, &val);
	spin_unlock(&esb_lock);

	/* Returns 0 if the timer was disabled, non-zero otherwise */
	return (val & 0x01);
}

static void esb_timer_keepalive(void)
{
	spin_lock(&esb_lock);
	esb_unlock_registers();
	writew(ESB_WDT_RELOAD, ESB_RELOAD_REG);
        /* FIXME: Do we need to flush anything here? */
	spin_unlock(&esb_lock);
}

static int esb_timer_set_heartbeat(int time)
{
	u32 val;

	if (time < 0x1 || time > (2 * 0x03ff))
		return -EINVAL;

	spin_lock(&esb_lock);

	/* We shift by 9, so if we are passed a value of 1 sec,
	 * val will be 1 << 9 = 512, then write that to two
	 * timers => 2 * 512 = 1024 (which is decremented at 1KHz)
	 */
	val = time << 9;

	/* Write timer 1 */
	esb_unlock_registers();
	writel(val, ESB_TIMER1_REG);

	/* Write timer 2 */
	esb_unlock_registers();
        writel(val, ESB_TIMER2_REG);

        /* Reload */
	esb_unlock_registers();
	writew(ESB_WDT_RELOAD, ESB_RELOAD_REG);

	/* FIXME: Do we need to flush everything out? */

	/* Done */
	heartbeat = time;
	spin_unlock(&esb_lock);
	return 0;
}

static int esb_timer_read (void)
{
       	u32 count;

	/* This isn't documented, and doesn't take into
         * acount which stage is running, but it looks
         * like a 20 bit count down, so we might as well report it.
         */
        pci_read_config_dword(esb_pci, 0x64, &count);
        return (int)count;
}

/*
 * 	/dev/watchdog handling
 */

static int esb_open (struct inode *inode, struct file *file)
{
        /* /dev/watchdog can only be opened once */
        if (test_and_set_bit(0, &timer_alive))
                return -EBUSY;

        /* Reload and activate timer */
        esb_timer_keepalive ();
        esb_timer_start ();

	return nonseekable_open(inode, file);
}

static int esb_release (struct inode *inode, struct file *file)
{
        /* Shut off the timer. */
        if (esb_expect_close == 42) {
                esb_timer_stop ();
        } else {
                printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
                esb_timer_keepalive ();
        }
        clear_bit(0, &timer_alive);
        esb_expect_close = 0;
        return 0;
}

static ssize_t esb_write (struct file *file, const char __user *data,
			  size_t len, loff_t * ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
        if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			esb_expect_close = 0;

			/* scan to see whether or not we got the magic character */
			for (i = 0; i != len; i++) {
				char c;
				if(get_user(c, data+i))
					return -EFAULT;
				if (c == 'V')
					esb_expect_close = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		esb_timer_keepalive ();
	}
	return len;
}

static int esb_ioctl (struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	int new_options, retval = -EINVAL;
	int new_heartbeat;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options =              WDIOF_SETTIMEOUT |
					WDIOF_KEEPALIVEPING |
					WDIOF_MAGICCLOSE,
		.firmware_version =     0,
		.identity =             ESB_MODULE_NAME,
	};

	switch (cmd) {
		case WDIOC_GETSUPPORT:
			return copy_to_user(argp, &ident,
					    sizeof (ident)) ? -EFAULT : 0;

		case WDIOC_GETSTATUS:
			return put_user (esb_timer_read(), p);

		case WDIOC_GETBOOTSTATUS:
			return put_user (triggered, p);

                case WDIOC_KEEPALIVE:
                        esb_timer_keepalive ();
                        return 0;

                case WDIOC_SETOPTIONS:
                {
                        if (get_user (new_options, p))
                                return -EFAULT;

                        if (new_options & WDIOS_DISABLECARD) {
                                esb_timer_stop ();
                                retval = 0;
                        }

                        if (new_options & WDIOS_ENABLECARD) {
                                esb_timer_keepalive ();
                                esb_timer_start ();
                                retval = 0;
                        }

                        return retval;
                }

                case WDIOC_SETTIMEOUT:
                {
                        if (get_user(new_heartbeat, p))
                                return -EFAULT;

                        if (esb_timer_set_heartbeat(new_heartbeat))
                            return -EINVAL;

                        esb_timer_keepalive ();
                        /* Fall */
                }

                case WDIOC_GETTIMEOUT:
                        return put_user(heartbeat, p);

                default:
                        return -ENOTTY;
        }
}

/*
 *      Notify system
 */

static int esb_notify_sys (struct notifier_block *this, unsigned long code, void *unused)
{
        if (code==SYS_DOWN || code==SYS_HALT) {
                /* Turn the WDT off */
                esb_timer_stop ();
        }

        return NOTIFY_DONE;
}

/*
 *      Kernel Interfaces
 */

static const struct file_operations esb_fops = {
        .owner =        THIS_MODULE,
        .llseek =       no_llseek,
        .write =        esb_write,
        .ioctl =        esb_ioctl,
        .open =         esb_open,
        .release =      esb_release,
};

static struct miscdevice esb_miscdev = {
        .minor =        WATCHDOG_MINOR,
        .name =         "watchdog",
        .fops =         &esb_fops,
};

static struct notifier_block esb_notifier = {
        .notifier_call =        esb_notify_sys,
};

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static struct pci_device_id esb_pci_tbl[] = {
        { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_9), },
        { 0, },                 /* End of list */
};
MODULE_DEVICE_TABLE (pci, esb_pci_tbl);

/*
 *      Init & exit routines
 */

static unsigned char __init esb_getdevice (void)
{
	u8 val1;
	unsigned short val2;

        struct pci_dev *dev = NULL;
        /*
         *      Find the PCI device
         */

        for_each_pci_dev(dev) {
                if (pci_match_id(esb_pci_tbl, dev)) {
                        esb_pci = dev;
                        break;
                }
	}

        if (esb_pci) {
        	if (pci_enable_device(esb_pci)) {
			printk (KERN_ERR PFX "failed to enable device\n");
			goto err_devput;
		}

		if (pci_request_region(esb_pci, 0, ESB_MODULE_NAME)) {
			printk (KERN_ERR PFX "failed to request region\n");
			goto err_disable;
		}

		BASEADDR = ioremap(pci_resource_start(esb_pci, 0),
				   pci_resource_len(esb_pci, 0));
		if (BASEADDR == NULL) {
                	/* Something's wrong here, BASEADDR has to be set */
			printk (KERN_ERR PFX "failed to get BASEADDR\n");
                        goto err_release;
                }

		/*
		 * The watchdog has two timers, it can be setup so that the
		 * expiry of timer1 results in an interrupt and the expiry of
		 * timer2 results in a reboot. We set it to not generate
		 * any interrupts as there is not much we can do with it
		 * right now.
		 *
		 * We also enable reboots and set the timer frequency to
		 * the PCI clock divided by 2^15 (approx 1KHz).
		 */
		pci_write_config_word(esb_pci, ESB_CONFIG_REG, 0x0003);

		/* Check that the WDT isn't already locked */
		pci_read_config_byte(esb_pci, ESB_LOCK_REG, &val1);
		if (val1 & ESB_WDT_LOCK)
			printk (KERN_WARNING PFX "nowayout already set\n");

		/* Set the timer to watchdog mode and disable it for now */
		pci_write_config_byte(esb_pci, ESB_LOCK_REG, 0x00);

		/* Check if the watchdog was previously triggered */
		esb_unlock_registers();
		val2 = readw(ESB_RELOAD_REG);
		triggered = (val2 & (0x01 << 9) >> 9);

		/* Reset trigger flag and timers */
		esb_unlock_registers();
		writew((0x11 << 8), ESB_RELOAD_REG);

		/* Done */
		return 1;

err_release:
		pci_release_region(esb_pci, 0);
err_disable:
		pci_disable_device(esb_pci);
err_devput:
		pci_dev_put(esb_pci);
	}
	return 0;
}

static int __init watchdog_init (void)
{
        int ret;

        /* Check whether or not the hardware watchdog is there */
        if (!esb_getdevice () || esb_pci == NULL)
                return -ENODEV;

        /* Check that the heartbeat value is within it's range ; if not reset to the default */
        if (esb_timer_set_heartbeat (heartbeat)) {
                esb_timer_set_heartbeat (WATCHDOG_HEARTBEAT);
                printk(KERN_INFO PFX "heartbeat value must be 1<heartbeat<2046, using %d\n",
		       heartbeat);
        }

        ret = register_reboot_notifier(&esb_notifier);
        if (ret != 0) {
                printk(KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
                        ret);
                goto err_unmap;
        }

        ret = misc_register(&esb_miscdev);
        if (ret != 0) {
                printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
                        WATCHDOG_MINOR, ret);
                goto err_notifier;
        }

        esb_timer_stop ();

        printk (KERN_INFO PFX "initialized (0x%p). heartbeat=%d sec (nowayout=%d)\n",
                BASEADDR, heartbeat, nowayout);

        return 0;

err_notifier:
        unregister_reboot_notifier(&esb_notifier);
err_unmap:
	iounmap(BASEADDR);
/* err_release: */
	pci_release_region(esb_pci, 0);
/* err_disable: */
	pci_disable_device(esb_pci);
/* err_devput: */
	pci_dev_put(esb_pci);
        return ret;
}

static void __exit watchdog_cleanup (void)
{
	/* Stop the timer before we leave */
	if (!nowayout)
		esb_timer_stop ();

	/* Deregister */
	misc_deregister(&esb_miscdev);
        unregister_reboot_notifier(&esb_notifier);
	iounmap(BASEADDR);
	pci_release_region(esb_pci, 0);
	pci_disable_device(esb_pci);
	pci_dev_put(esb_pci);
}

module_init(watchdog_init);
module_exit(watchdog_cleanup);

MODULE_AUTHOR("Ross Biro and David Härdeman");
MODULE_DESCRIPTION("Watchdog driver for Intel 6300ESB chipsets");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
