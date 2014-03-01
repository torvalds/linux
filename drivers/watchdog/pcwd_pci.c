/*
 *	Berkshire PCI-PC Watchdog Card Driver
 *
 *	(c) Copyright 2003-2007 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	Based on source code of the following authors:
 *	  Ken Hollis <kenji@bitgate.com>,
 *	  Lindsay Harris <lindsay@bluegum.com>,
 *	  Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Wim Van Sebroeck nor Iguana vzw. admit liability nor
 *	provide warranty for any of this software. This material is
 *	provided "AS-IS" and at no charge.
 */

/*
 *	A bells and whistles driver is available from:
 *	http://www.kernel.org/pub/linux/kernel/people/wim/pcwd/pcwd_pci/
 *
 *	More info available at
 *	http://www.berkprod.com/ or http://www.pcwatchdog.com/
 */

/*
 *	Includes, defines, variables, module parameters, ...
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>	/* For module specific items */
#include <linux/moduleparam.h>	/* For new moduleparam's */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/delay.h>	/* For mdelay function */
#include <linux/miscdevice.h>	/* For struct miscdevice */
#include <linux/watchdog.h>	/* For the watchdog specific items */
#include <linux/notifier.h>	/* For notifier support */
#include <linux/reboot.h>	/* For reboot_notifier stuff */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/fs.h>		/* For file operations */
#include <linux/pci.h>		/* For pci functions */
#include <linux/ioport.h>	/* For io-port access */
#include <linux/spinlock.h>	/* For spin_lock/spin_unlock/... */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */
#include <linux/io.h>		/* For inb/outb/... */

/* Module and version information */
#define WATCHDOG_VERSION "1.03"
#define WATCHDOG_DRIVER_NAME "PCI-PC Watchdog"
#define WATCHDOG_NAME "pcwd_pci"
#define DRIVER_VERSION WATCHDOG_DRIVER_NAME " driver, v" WATCHDOG_VERSION

/* Stuff for the PCI ID's  */
#ifndef PCI_VENDOR_ID_QUICKLOGIC
#define PCI_VENDOR_ID_QUICKLOGIC    0x11e3
#endif

#ifndef PCI_DEVICE_ID_WATCHDOG_PCIPCWD
#define PCI_DEVICE_ID_WATCHDOG_PCIPCWD 0x5030
#endif

/*
 * These are the defines that describe the control status bits for the
 * PCI-PC Watchdog card.
 */
/* Port 1 : Control Status #1 */
#define WD_PCI_WTRP		0x01	/* Watchdog Trip status */
#define WD_PCI_HRBT		0x02	/* Watchdog Heartbeat */
#define WD_PCI_TTRP		0x04	/* Temperature Trip status */
#define WD_PCI_RL2A		0x08	/* Relay 2 Active */
#define WD_PCI_RL1A		0x10	/* Relay 1 Active */
#define WD_PCI_R2DS		0x40	/* Relay 2 Disable Temperature-trip /
									reset */
#define WD_PCI_RLY2		0x80	/* Activate Relay 2 on the board */
/* Port 2 : Control Status #2 */
#define WD_PCI_WDIS		0x10	/* Watchdog Disable */
#define WD_PCI_ENTP		0x20	/* Enable Temperature Trip Reset */
#define WD_PCI_WRSP		0x40	/* Watchdog wrote response */
#define WD_PCI_PCMD		0x80	/* PC has sent command */

/* according to documentation max. time to process a command for the pci
 * watchdog card is 100 ms, so we give it 150 ms to do it's job */
#define PCI_COMMAND_TIMEOUT	150

/* Watchdog's internal commands */
#define CMD_GET_STATUS				0x04
#define CMD_GET_FIRMWARE_VERSION		0x08
#define CMD_READ_WATCHDOG_TIMEOUT		0x18
#define CMD_WRITE_WATCHDOG_TIMEOUT		0x19
#define CMD_GET_CLEAR_RESET_COUNT		0x84

/* Watchdog's Dip Switch heartbeat values */
static const int heartbeat_tbl[] = {
	5,	/* OFF-OFF-OFF	=  5 Sec  */
	10,	/* OFF-OFF-ON	= 10 Sec  */
	30,	/* OFF-ON-OFF	= 30 Sec  */
	60,	/* OFF-ON-ON	=  1 Min  */
	300,	/* ON-OFF-OFF	=  5 Min  */
	600,	/* ON-OFF-ON	= 10 Min  */
	1800,	/* ON-ON-OFF	= 30 Min  */
	3600,	/* ON-ON-ON	=  1 hour */
};

/* We can only use 1 card due to the /dev/watchdog restriction */
static int cards_found;

/* internal variables */
static int temp_panic;
static unsigned long is_active;
static char expect_release;
/* this is private data for each PCI-PC watchdog card */
static struct {
	/* Wether or not the card has a temperature device */
	int supports_temp;
	/* The card's boot status */
	int boot_status;
	/* The cards I/O address */
	unsigned long io_addr;
	/* the lock for io operations */
	spinlock_t io_lock;
	/* the PCI-device */
	struct pci_dev *pdev;
} pcipcwd_private;

/* module parameters */
#define QUIET	0	/* Default */
#define VERBOSE	1	/* Verbose */
#define DEBUG	2	/* print fancy stuff too */
static int debug = QUIET;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level: 0=Quiet, 1=Verbose, 2=Debug (default=0)");

#define WATCHDOG_HEARTBEAT 0	/* default heartbeat =
						delay-time from dip-switches */
static int heartbeat = WATCHDOG_HEARTBEAT;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. "
	"(0<heartbeat<65536 or 0=delay-time from dip-switches, default="
				__MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
					__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 *	Internal functions
 */

static int send_command(int cmd, int *msb, int *lsb)
{
	int got_response, count;

	if (debug >= DEBUG)
		pr_debug("sending following data cmd=0x%02x msb=0x%02x lsb=0x%02x\n",
			 cmd, *msb, *lsb);

	spin_lock(&pcipcwd_private.io_lock);
	/* If a command requires data it should be written first.
	 * Data for commands with 8 bits of data should be written to port 4.
	 * Commands with 16 bits of data, should be written as LSB to port 4
	 * and MSB to port 5.
	 * After the required data has been written then write the command to
	 * port 6. */
	outb_p(*lsb, pcipcwd_private.io_addr + 4);
	outb_p(*msb, pcipcwd_private.io_addr + 5);
	outb_p(cmd, pcipcwd_private.io_addr + 6);

	/* wait till the pci card processed the command, signaled by
	 * the WRSP bit in port 2 and give it a max. timeout of
	 * PCI_COMMAND_TIMEOUT to process */
	got_response = inb_p(pcipcwd_private.io_addr + 2) & WD_PCI_WRSP;
	for (count = 0; (count < PCI_COMMAND_TIMEOUT) && (!got_response);
								count++) {
		mdelay(1);
		got_response = inb_p(pcipcwd_private.io_addr + 2) & WD_PCI_WRSP;
	}

	if (debug >= DEBUG) {
		if (got_response) {
			pr_debug("time to process command was: %d ms\n",
				 count);
		} else {
			pr_debug("card did not respond on command!\n");
		}
	}

	if (got_response) {
		/* read back response */
		*lsb = inb_p(pcipcwd_private.io_addr + 4);
		*msb = inb_p(pcipcwd_private.io_addr + 5);

		/* clear WRSP bit */
		inb_p(pcipcwd_private.io_addr + 6);

		if (debug >= DEBUG)
			pr_debug("received following data for cmd=0x%02x: msb=0x%02x lsb=0x%02x\n",
				 cmd, *msb, *lsb);
	}

	spin_unlock(&pcipcwd_private.io_lock);

	return got_response;
}

static inline void pcipcwd_check_temperature_support(void)
{
	if (inb_p(pcipcwd_private.io_addr) != 0xF0)
		pcipcwd_private.supports_temp = 1;
}

static int pcipcwd_get_option_switches(void)
{
	int option_switches;

	option_switches = inb_p(pcipcwd_private.io_addr + 3);
	return option_switches;
}

static void pcipcwd_show_card_info(void)
{
	int got_fw_rev, fw_rev_major, fw_rev_minor;
	char fw_ver_str[20];		/* The cards firmware version */
	int option_switches;

	got_fw_rev = send_command(CMD_GET_FIRMWARE_VERSION, &fw_rev_major,
								&fw_rev_minor);
	if (got_fw_rev)
		sprintf(fw_ver_str, "%u.%02u", fw_rev_major, fw_rev_minor);
	else
		sprintf(fw_ver_str, "<card no answer>");

	/* Get switch settings */
	option_switches = pcipcwd_get_option_switches();

	pr_info("Found card at port 0x%04x (Firmware: %s) %s temp option\n",
		(int) pcipcwd_private.io_addr, fw_ver_str,
		(pcipcwd_private.supports_temp ? "with" : "without"));

	pr_info("Option switches (0x%02x): Temperature Reset Enable=%s, Power On Delay=%s\n",
		option_switches,
		((option_switches & 0x10) ? "ON" : "OFF"),
		((option_switches & 0x08) ? "ON" : "OFF"));

	if (pcipcwd_private.boot_status & WDIOF_CARDRESET)
		pr_info("Previous reset was caused by the Watchdog card\n");

	if (pcipcwd_private.boot_status & WDIOF_OVERHEAT)
		pr_info("Card sensed a CPU Overheat\n");

	if (pcipcwd_private.boot_status == 0)
		pr_info("No previous trip detected - Cold boot or reset\n");
}

static int pcipcwd_start(void)
{
	int stat_reg;

	spin_lock(&pcipcwd_private.io_lock);
	outb_p(0x00, pcipcwd_private.io_addr + 3);
	udelay(1000);

	stat_reg = inb_p(pcipcwd_private.io_addr + 2);
	spin_unlock(&pcipcwd_private.io_lock);

	if (stat_reg & WD_PCI_WDIS) {
		pr_err("Card timer not enabled\n");
		return -1;
	}

	if (debug >= VERBOSE)
		pr_debug("Watchdog started\n");

	return 0;
}

static int pcipcwd_stop(void)
{
	int stat_reg;

	spin_lock(&pcipcwd_private.io_lock);
	outb_p(0xA5, pcipcwd_private.io_addr + 3);
	udelay(1000);

	outb_p(0xA5, pcipcwd_private.io_addr + 3);
	udelay(1000);

	stat_reg = inb_p(pcipcwd_private.io_addr + 2);
	spin_unlock(&pcipcwd_private.io_lock);

	if (!(stat_reg & WD_PCI_WDIS)) {
		pr_err("Card did not acknowledge disable attempt\n");
		return -1;
	}

	if (debug >= VERBOSE)
		pr_debug("Watchdog stopped\n");

	return 0;
}

static int pcipcwd_keepalive(void)
{
	/* Re-trigger watchdog by writing to port 0 */
	spin_lock(&pcipcwd_private.io_lock);
	outb_p(0x42, pcipcwd_private.io_addr);	/* send out any data */
	spin_unlock(&pcipcwd_private.io_lock);

	if (debug >= DEBUG)
		pr_debug("Watchdog keepalive signal send\n");

	return 0;
}

static int pcipcwd_set_heartbeat(int t)
{
	int t_msb = t / 256;
	int t_lsb = t % 256;

	if ((t < 0x0001) || (t > 0xFFFF))
		return -EINVAL;

	/* Write new heartbeat to watchdog */
	send_command(CMD_WRITE_WATCHDOG_TIMEOUT, &t_msb, &t_lsb);

	heartbeat = t;
	if (debug >= VERBOSE)
		pr_debug("New heartbeat: %d\n", heartbeat);

	return 0;
}

static int pcipcwd_get_status(int *status)
{
	int control_status;

	*status = 0;
	control_status = inb_p(pcipcwd_private.io_addr + 1);
	if (control_status & WD_PCI_WTRP)
		*status |= WDIOF_CARDRESET;
	if (control_status & WD_PCI_TTRP) {
		*status |= WDIOF_OVERHEAT;
		if (temp_panic)
			panic(KBUILD_MODNAME ": Temperature overheat trip!\n");
	}

	if (debug >= DEBUG)
		pr_debug("Control Status #1: 0x%02x\n", control_status);

	return 0;
}

static int pcipcwd_clear_status(void)
{
	int control_status;
	int msb;
	int reset_counter;

	if (debug >= VERBOSE)
		pr_info("clearing watchdog trip status & LED\n");

	control_status = inb_p(pcipcwd_private.io_addr + 1);

	if (debug >= DEBUG) {
		pr_debug("status was: 0x%02x\n", control_status);
		pr_debug("sending: 0x%02x\n",
			 (control_status & WD_PCI_R2DS) | WD_PCI_WTRP);
	}

	/* clear trip status & LED and keep mode of relay 2 */
	outb_p((control_status & WD_PCI_R2DS) | WD_PCI_WTRP,
						pcipcwd_private.io_addr + 1);

	/* clear reset counter */
	msb = 0;
	reset_counter = 0xff;
	send_command(CMD_GET_CLEAR_RESET_COUNT, &msb, &reset_counter);

	if (debug >= DEBUG) {
		pr_debug("reset count was: 0x%02x\n", reset_counter);
	}

	return 0;
}

static int pcipcwd_get_temperature(int *temperature)
{
	*temperature = 0;
	if (!pcipcwd_private.supports_temp)
		return -ENODEV;

	spin_lock(&pcipcwd_private.io_lock);
	*temperature = inb_p(pcipcwd_private.io_addr);
	spin_unlock(&pcipcwd_private.io_lock);

	/*
	 * Convert celsius to fahrenheit, since this was
	 * the decided 'standard' for this return value.
	 */
	*temperature = (*temperature * 9 / 5) + 32;

	if (debug >= DEBUG) {
		pr_debug("temperature is: %d F\n", *temperature);
	}

	return 0;
}

static int pcipcwd_get_timeleft(int *time_left)
{
	int msb;
	int lsb;

	/* Read the time that's left before rebooting */
	/* Note: if the board is not yet armed then we will read 0xFFFF */
	send_command(CMD_READ_WATCHDOG_TIMEOUT, &msb, &lsb);

	*time_left = (msb << 8) + lsb;

	if (debug >= VERBOSE)
		pr_debug("Time left before next reboot: %d\n", *time_left);

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static ssize_t pcipcwd_write(struct file *file, const char __user *data,
			     size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			expect_release = 0;

			/* scan to see whether or not we got the
			 * magic character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		pcipcwd_keepalive();
	}
	return len;
}

static long pcipcwd_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options =		WDIOF_OVERHEAT |
					WDIOF_CARDRESET |
					WDIOF_KEEPALIVEPING |
					WDIOF_SETTIMEOUT |
					WDIOF_MAGICCLOSE,
		.firmware_version =	1,
		.identity =		WATCHDOG_DRIVER_NAME,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	{
		int status;
		pcipcwd_get_status(&status);
		return put_user(status, p);
	}

	case WDIOC_GETBOOTSTATUS:
		return put_user(pcipcwd_private.boot_status, p);

	case WDIOC_GETTEMP:
	{
		int temperature;

		if (pcipcwd_get_temperature(&temperature))
			return -EFAULT;

		return put_user(temperature, p);
	}

	case WDIOC_SETOPTIONS:
	{
		int new_options, retval = -EINVAL;

		if (get_user(new_options, p))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD) {
			if (pcipcwd_stop())
				return -EIO;
			retval = 0;
		}

		if (new_options & WDIOS_ENABLECARD) {
			if (pcipcwd_start())
				return -EIO;
			retval = 0;
		}

		if (new_options & WDIOS_TEMPPANIC) {
			temp_panic = 1;
			retval = 0;
		}

		return retval;
	}

	case WDIOC_KEEPALIVE:
		pcipcwd_keepalive();
		return 0;

	case WDIOC_SETTIMEOUT:
	{
		int new_heartbeat;

		if (get_user(new_heartbeat, p))
			return -EFAULT;

		if (pcipcwd_set_heartbeat(new_heartbeat))
			return -EINVAL;

		pcipcwd_keepalive();
		/* Fall */
	}

	case WDIOC_GETTIMEOUT:
		return put_user(heartbeat, p);

	case WDIOC_GETTIMELEFT:
	{
		int time_left;

		if (pcipcwd_get_timeleft(&time_left))
			return -EFAULT;

		return put_user(time_left, p);
	}

	default:
		return -ENOTTY;
	}
}

static int pcipcwd_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &is_active)) {
		if (debug >= VERBOSE)
			pr_err("Attempt to open already opened device\n");
		return -EBUSY;
	}

	/* Activate */
	pcipcwd_start();
	pcipcwd_keepalive();
	return nonseekable_open(inode, file);
}

static int pcipcwd_release(struct inode *inode, struct file *file)
{
	/*
	 *      Shut off the timer.
	 */
	if (expect_release == 42) {
		pcipcwd_stop();
	} else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		pcipcwd_keepalive();
	}
	expect_release = 0;
	clear_bit(0, &is_active);
	return 0;
}

/*
 *	/dev/temperature handling
 */

static ssize_t pcipcwd_temp_read(struct file *file, char __user *data,
				size_t len, loff_t *ppos)
{
	int temperature;

	if (pcipcwd_get_temperature(&temperature))
		return -EFAULT;

	if (copy_to_user(data, &temperature, 1))
		return -EFAULT;

	return 1;
}

static int pcipcwd_temp_open(struct inode *inode, struct file *file)
{
	if (!pcipcwd_private.supports_temp)
		return -ENODEV;

	return nonseekable_open(inode, file);
}

static int pcipcwd_temp_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	Notify system
 */

static int pcipcwd_notify_sys(struct notifier_block *this, unsigned long code,
								void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		pcipcwd_stop();	/* Turn the WDT off */

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations pcipcwd_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.write =	pcipcwd_write,
	.unlocked_ioctl = pcipcwd_ioctl,
	.open =		pcipcwd_open,
	.release =	pcipcwd_release,
};

static struct miscdevice pcipcwd_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&pcipcwd_fops,
};

static const struct file_operations pcipcwd_temp_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		pcipcwd_temp_read,
	.open =		pcipcwd_temp_open,
	.release =	pcipcwd_temp_release,
};

static struct miscdevice pcipcwd_temp_miscdev = {
	.minor =	TEMP_MINOR,
	.name =		"temperature",
	.fops =		&pcipcwd_temp_fops,
};

static struct notifier_block pcipcwd_notifier = {
	.notifier_call =	pcipcwd_notify_sys,
};

/*
 *	Init & exit routines
 */

static int pcipcwd_card_init(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	int ret = -EIO;

	cards_found++;
	if (cards_found == 1)
		pr_info("%s\n", DRIVER_VERSION);

	if (cards_found > 1) {
		pr_err("This driver only supports 1 device\n");
		return -ENODEV;
	}

	if (pci_enable_device(pdev)) {
		pr_err("Not possible to enable PCI Device\n");
		return -ENODEV;
	}

	if (pci_resource_start(pdev, 0) == 0x0000) {
		pr_err("No I/O-Address for card detected\n");
		ret = -ENODEV;
		goto err_out_disable_device;
	}

	spin_lock_init(&pcipcwd_private.io_lock);
	pcipcwd_private.pdev = pdev;
	pcipcwd_private.io_addr = pci_resource_start(pdev, 0);

	if (pci_request_regions(pdev, WATCHDOG_NAME)) {
		pr_err("I/O address 0x%04x already in use\n",
		       (int) pcipcwd_private.io_addr);
		ret = -EIO;
		goto err_out_disable_device;
	}

	/* get the boot_status */
	pcipcwd_get_status(&pcipcwd_private.boot_status);

	/* clear the "card caused reboot" flag */
	pcipcwd_clear_status();

	/* disable card */
	pcipcwd_stop();

	/* Check whether or not the card supports the temperature device */
	pcipcwd_check_temperature_support();

	/* Show info about the card itself */
	pcipcwd_show_card_info();

	/* If heartbeat = 0 then we use the heartbeat from the dip-switches */
	if (heartbeat == 0)
		heartbeat =
			heartbeat_tbl[(pcipcwd_get_option_switches() & 0x07)];

	/* Check that the heartbeat value is within it's range ;
	 * if not reset to the default */
	if (pcipcwd_set_heartbeat(heartbeat)) {
		pcipcwd_set_heartbeat(WATCHDOG_HEARTBEAT);
		pr_info("heartbeat value must be 0<heartbeat<65536, using %d\n",
			WATCHDOG_HEARTBEAT);
	}

	ret = register_reboot_notifier(&pcipcwd_notifier);
	if (ret != 0) {
		pr_err("cannot register reboot notifier (err=%d)\n", ret);
		goto err_out_release_region;
	}

	if (pcipcwd_private.supports_temp) {
		ret = misc_register(&pcipcwd_temp_miscdev);
		if (ret != 0) {
			pr_err("cannot register miscdev on minor=%d (err=%d)\n",
			       TEMP_MINOR, ret);
			goto err_out_unregister_reboot;
		}
	}

	ret = misc_register(&pcipcwd_miscdev);
	if (ret != 0) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, ret);
		goto err_out_misc_deregister;
	}

	pr_info("initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;

err_out_misc_deregister:
	if (pcipcwd_private.supports_temp)
		misc_deregister(&pcipcwd_temp_miscdev);
err_out_unregister_reboot:
	unregister_reboot_notifier(&pcipcwd_notifier);
err_out_release_region:
	pci_release_regions(pdev);
err_out_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void pcipcwd_card_exit(struct pci_dev *pdev)
{
	/* Stop the timer before we leave */
	if (!nowayout)
		pcipcwd_stop();

	/* Deregister */
	misc_deregister(&pcipcwd_miscdev);
	if (pcipcwd_private.supports_temp)
		misc_deregister(&pcipcwd_temp_miscdev);
	unregister_reboot_notifier(&pcipcwd_notifier);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	cards_found--;
}

static const struct pci_device_id pcipcwd_pci_tbl[] = {
	{ PCI_VENDOR_ID_QUICKLOGIC, PCI_DEVICE_ID_WATCHDOG_PCIPCWD,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0 },			/* End of list */
};
MODULE_DEVICE_TABLE(pci, pcipcwd_pci_tbl);

static struct pci_driver pcipcwd_driver = {
	.name		= WATCHDOG_NAME,
	.id_table	= pcipcwd_pci_tbl,
	.probe		= pcipcwd_card_init,
	.remove		= pcipcwd_card_exit,
};

module_pci_driver(pcipcwd_driver);

MODULE_AUTHOR("Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("Berkshire PCI-PC Watchdog driver");
MODULE_LICENSE("GPL");
