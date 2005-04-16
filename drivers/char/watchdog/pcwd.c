/*
 * PC Watchdog Driver
 * by Ken Hollis (khollis@bitgate.com)
 *
 * Permission granted from Simon Machell (73244.1270@compuserve.com)
 * Written for the Linux Kernel, and GPLed by Ken Hollis
 *
 * 960107	Added request_region routines, modulized the whole thing.
 * 960108	Fixed end-of-file pointer (Thanks to Dan Hollis), added
 *		WD_TIMEOUT define.
 * 960216	Added eof marker on the file, and changed verbose messages.
 * 960716	Made functional and cosmetic changes to the source for
 *		inclusion in Linux 2.0.x kernels, thanks to Alan Cox.
 * 960717	Removed read/seek routines, replaced with ioctl.  Also, added
 *		check_region command due to Alan's suggestion.
 * 960821	Made changes to compile in newer 2.0.x kernels.  Added
 *		"cold reboot sense" entry.
 * 960825	Made a few changes to code, deleted some defines and made
 *		typedefs to replace them.  Made heartbeat reset only available
 *		via ioctl, and removed the write routine.
 * 960828	Added new items for PC Watchdog Rev.C card.
 * 960829	Changed around all of the IOCTLs, added new features,
 *		added watchdog disable/re-enable routines.  Added firmware
 *		version reporting.  Added read routine for temperature.
 *		Removed some extra defines, added an autodetect Revision
 *		routine.
 * 961006       Revised some documentation, fixed some cosmetic bugs.  Made
 *              drivers to panic the system if it's overheating at bootup.
 * 961118	Changed some verbiage on some of the output, tidied up
 *		code bits, and added compatibility to 2.1.x.
 * 970912       Enabled board on open and disable on close.
 * 971107	Took account of recent VFS changes (broke read).
 * 971210       Disable board on initialisation in case board already ticking.
 * 971222       Changed open/close for temperature handling
 *              Michael Meskes <meskes@debian.org>.
 * 980112       Used minor numbers from include/linux/miscdevice.h
 * 990403       Clear reset status after reading control status register in
 *              pcwd_showprevstate(). [Marc Boucher <marc@mbsi.ca>]
 * 990605	Made changes to code to support Firmware 1.22a, added
 *		fairly useless proc entry.
 * 990610	removed said useless proc code for the merge <alan>
 * 000403	Removed last traces of proc code. <davej>
 * 011214	Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT <Matt_Domsch@dell.com>
 *              Added timeout module option to override default
 */

/*
 *	A bells and whistles driver is available from http://www.pcwd.de/
 *	More info available at http://www.berkprod.com/ or http://www.pcwatchdog.com/
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/config.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#define WD_VER                  "1.16 (06/12/2004)"
#define PFX			"pcwd: "

/*
 * It should be noted that PCWD_REVISION_B was removed because A and B
 * are essentially the same types of card, with the exception that B
 * has temperature reporting.  Since I didn't receive a Rev.B card,
 * the Rev.B card is not supported.  (It's a good thing too, as they
 * are no longer in production.)
 */
#define	PCWD_REVISION_A		1
#define	PCWD_REVISION_C		2

/*
 * These are the defines that describe the control status bits for the
 * PC Watchdog card, revision A.
 */
#define WD_WDRST                0x01	/* Previously reset state */
#define WD_T110                 0x02	/* Temperature overheat sense */
#define WD_HRTBT                0x04	/* Heartbeat sense */
#define WD_RLY2                 0x08	/* External relay triggered */
#define WD_SRLY2                0x80	/* Software external relay triggered */

/*
 * These are the defines that describe the control status bits for the
 * PC Watchdog card, revision C.
 */
#define WD_REVC_WTRP            0x01	/* Watchdog Trip status */
#define WD_REVC_HRBT            0x02	/* Watchdog Heartbeat */
#define WD_REVC_TTRP            0x04	/* Temperature Trip status */

/* max. time we give an ISA watchdog card to process a command */
/* 500ms for each 4 bit response (according to spec.) */
#define ISA_COMMAND_TIMEOUT     1000

/* Watchdog's internal commands */
#define CMD_ISA_IDLE                    0x00
#define CMD_ISA_VERSION_INTEGER         0x01
#define CMD_ISA_VERSION_TENTH           0x02
#define CMD_ISA_VERSION_HUNDRETH        0x03
#define CMD_ISA_VERSION_MINOR           0x04
#define CMD_ISA_SWITCH_SETTINGS         0x05
#define CMD_ISA_DELAY_TIME_2SECS        0x0A
#define CMD_ISA_DELAY_TIME_4SECS        0x0B
#define CMD_ISA_DELAY_TIME_8SECS        0x0C

/*
 * We are using an kernel timer to do the pinging of the watchdog
 * every ~500ms. We try to set the internal heartbeat of the
 * watchdog to 2 ms.
 */

#define WDT_INTERVAL (HZ/2+1)

/* We can only use 1 card due to the /dev/watchdog restriction */
static int cards_found;

/* internal variables */
static atomic_t open_allowed = ATOMIC_INIT(1);
static char expect_close;
static struct timer_list timer;
static unsigned long next_heartbeat;
static int temp_panic;
static int revision;			/* The card's revision */
static int supports_temp;		/* Wether or not the card has a temperature device */
static int command_mode;		/* Wether or not the card is in command mode */
static int initial_status;		/* The card's boot status */
static int current_readport;		/* The cards I/O address */
static spinlock_t io_lock;

/* module parameters */
#define WATCHDOG_HEARTBEAT 60		/* 60 sec default heartbeat */
static int heartbeat = WATCHDOG_HEARTBEAT;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (2<=heartbeat<=7200, default=" __MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 *	Internal functions
 */

static int send_isa_command(int cmd)
{
	int i;
	int control_status;
	int port0, last_port0;	/* Double read for stabilising */

	/* The WCMD bit must be 1 and the command is only 4 bits in size */
	control_status = (cmd & 0x0F) | 0x80;
	outb_p(control_status, current_readport + 2);
	udelay(ISA_COMMAND_TIMEOUT);

	port0 = inb_p(current_readport);
	for (i = 0; i < 25; ++i) {
		last_port0 = port0;
		port0 = inb_p(current_readport);

		if (port0 == last_port0)
			break;	/* Data is stable */

		udelay (250);
	}

	return port0;
}

static int set_command_mode(void)
{
	int i, found=0, count=0;

	/* Set the card into command mode */
	spin_lock(&io_lock);
	while ((!found) && (count < 3)) {
		i = send_isa_command(CMD_ISA_IDLE);

		if (i == 0x00)
			found = 1;
		else if (i == 0xF3) {
			/* Card does not like what we've done to it */
			outb_p(0x00, current_readport + 2);
			udelay(1200);	/* Spec says wait 1ms */
			outb_p(0x00, current_readport + 2);
			udelay(ISA_COMMAND_TIMEOUT);
		}
		count++;
	}
	spin_unlock(&io_lock);
	command_mode = found;

	return(found);
}

static void unset_command_mode(void)
{
	/* Set the card into normal mode */
	spin_lock(&io_lock);
	outb_p(0x00, current_readport + 2);
	udelay(ISA_COMMAND_TIMEOUT);
	spin_unlock(&io_lock);

	command_mode = 0;
}

static void pcwd_timer_ping(unsigned long data)
{
	int wdrst_stat;

	/* If we got a heartbeat pulse within the WDT_INTERVAL
	 * we agree to ping the WDT */
	if(time_before(jiffies, next_heartbeat)) {
		/* Ping the watchdog */
		spin_lock(&io_lock);
		if (revision == PCWD_REVISION_A) {
			/*  Rev A cards are reset by setting the WD_WDRST bit in register 1 */
			wdrst_stat = inb_p(current_readport);
			wdrst_stat &= 0x0F;
			wdrst_stat |= WD_WDRST;

			outb_p(wdrst_stat, current_readport + 1);
		} else {
			/* Re-trigger watchdog by writing to port 0 */
			outb_p(0x00, current_readport);
		}

		/* Re-set the timer interval */
		mod_timer(&timer, jiffies + WDT_INTERVAL);

		spin_unlock(&io_lock);
	} else {
		printk(KERN_WARNING PFX "Heartbeat lost! Will not ping the watchdog\n");
	}
}

static int pcwd_start(void)
{
	int stat_reg;

	next_heartbeat = jiffies + (heartbeat * HZ);

	/* Start the timer */
	mod_timer(&timer, jiffies + WDT_INTERVAL);

	/* Enable the port */
	if (revision == PCWD_REVISION_C) {
		spin_lock(&io_lock);
		outb_p(0x00, current_readport + 3);
		udelay(ISA_COMMAND_TIMEOUT);
		stat_reg = inb_p(current_readport + 2);
		spin_unlock(&io_lock);
		if (stat_reg & 0x10) {
			printk(KERN_INFO PFX "Could not start watchdog\n");
			return -EIO;
		}
	}
	return 0;
}

static int pcwd_stop(void)
{
	int stat_reg;

	/* Stop the timer */
	del_timer(&timer);

	/*  Disable the board  */
	if (revision == PCWD_REVISION_C) {
		spin_lock(&io_lock);
		outb_p(0xA5, current_readport + 3);
		udelay(ISA_COMMAND_TIMEOUT);
		outb_p(0xA5, current_readport + 3);
		udelay(ISA_COMMAND_TIMEOUT);
		stat_reg = inb_p(current_readport + 2);
		spin_unlock(&io_lock);
		if ((stat_reg & 0x10) == 0) {
			printk(KERN_INFO PFX "Could not stop watchdog\n");
			return -EIO;
		}
	}
	return 0;
}

static int pcwd_keepalive(void)
{
	/* user land ping */
	next_heartbeat = jiffies + (heartbeat * HZ);
	return 0;
}

static int pcwd_set_heartbeat(int t)
{
	if ((t < 2) || (t > 7200)) /* arbitrary upper limit */
		return -EINVAL;

	heartbeat = t;
	return 0;
}

static int pcwd_get_status(int *status)
{
	int card_status;

	*status=0;
	spin_lock(&io_lock);
	if (revision == PCWD_REVISION_A)
		/* Rev A cards return status information from
		 * the base register, which is used for the
		 * temperature in other cards. */
		card_status = inb(current_readport);
	else {
		/* Rev C cards return card status in the base
		 * address + 1 register. And use different bits
		 * to indicate a card initiated reset, and an
		 * over-temperature condition. And the reboot
		 * status can be reset. */
		card_status = inb(current_readport + 1);
	}
	spin_unlock(&io_lock);

	if (revision == PCWD_REVISION_A) {
		if (card_status & WD_WDRST)
			*status |= WDIOF_CARDRESET;

		if (card_status & WD_T110) {
			*status |= WDIOF_OVERHEAT;
			if (temp_panic) {
				printk (KERN_INFO PFX "Temperature overheat trip!\n");
				machine_power_off();
			}
		}
	} else {
		if (card_status & WD_REVC_WTRP)
			*status |= WDIOF_CARDRESET;

		if (card_status & WD_REVC_TTRP) {
			*status |= WDIOF_OVERHEAT;
			if (temp_panic) {
				printk (KERN_INFO PFX "Temperature overheat trip!\n");
				machine_power_off();
			}
		}
	}

	return 0;
}

static int pcwd_clear_status(void)
{
	if (revision == PCWD_REVISION_C) {
		spin_lock(&io_lock);
		outb_p(0x00, current_readport + 1); /* clear reset status */
		spin_unlock(&io_lock);
	}
	return 0;
}

static int pcwd_get_temperature(int *temperature)
{
	/* check that port 0 gives temperature info and no command results */
	if (command_mode)
		return -1;

	*temperature = 0;
	if (!supports_temp)
		return -ENODEV;

	/*
	 * Convert celsius to fahrenheit, since this was
	 * the decided 'standard' for this return value.
	 */
	spin_lock(&io_lock);
	*temperature = ((inb(current_readport)) * 9 / 5) + 32;
	spin_unlock(&io_lock);

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static int pcwd_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	int rv;
	int status;
	int temperature;
	int new_heartbeat;
	int __user *argp = (int __user *)arg;
	static struct watchdog_info ident = {
		.options =		WDIOF_OVERHEAT |
					WDIOF_CARDRESET |
					WDIOF_KEEPALIVEPING |
					WDIOF_SETTIMEOUT |
					WDIOF_MAGICCLOSE,
		.firmware_version =	1,
		.identity =		"PCWD",
	};

	switch(cmd) {
	default:
		return -ENOIOCTLCMD;

	case WDIOC_GETSUPPORT:
		if(copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
		pcwd_get_status(&status);
		return put_user(status, argp);

	case WDIOC_GETBOOTSTATUS:
		return put_user(initial_status, argp);

	case WDIOC_GETTEMP:
		if (pcwd_get_temperature(&temperature))
			return -EFAULT;

		return put_user(temperature, argp);

	case WDIOC_SETOPTIONS:
		if (revision == PCWD_REVISION_C)
		{
			if(copy_from_user(&rv, argp, sizeof(int)))
				return -EFAULT;

			if (rv & WDIOS_DISABLECARD)
			{
				return pcwd_stop();
			}

			if (rv & WDIOS_ENABLECARD)
			{
				return pcwd_start();
			}

			if (rv & WDIOS_TEMPPANIC)
			{
				temp_panic = 1;
			}
		}
		return -EINVAL;

	case WDIOC_KEEPALIVE:
		pcwd_keepalive();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_heartbeat, argp))
			return -EFAULT;

		if (pcwd_set_heartbeat(new_heartbeat))
			return -EINVAL;

		pcwd_keepalive();
		/* Fall */

	case WDIOC_GETTIMEOUT:
		return put_user(heartbeat, argp);
	}

	return 0;
}

static ssize_t pcwd_write(struct file *file, const char __user *buf, size_t len,
			  loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		pcwd_keepalive();
	}
	return len;
}

static int pcwd_open(struct inode *inode, struct file *file)
{
	if (!atomic_dec_and_test(&open_allowed) ) {
		atomic_inc( &open_allowed );
		return -EBUSY;
	}

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate */
	pcwd_start();
	pcwd_keepalive();
	return nonseekable_open(inode, file);
}

static int pcwd_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		pcwd_stop();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		pcwd_keepalive();
	}
	expect_close = 0;
	atomic_inc( &open_allowed );
	return 0;
}

/*
 *	/dev/temperature handling
 */

static ssize_t pcwd_temp_read(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	int temperature;

	if (pcwd_get_temperature(&temperature))
		return -EFAULT;

	if (copy_to_user(buf, &temperature, 1))
		return -EFAULT;

	return 1;
}

static int pcwd_temp_open(struct inode *inode, struct file *file)
{
	if (!supports_temp)
		return -ENODEV;

	return nonseekable_open(inode, file);
}

static int pcwd_temp_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	Notify system
 */

static int pcwd_notify_sys(struct notifier_block *this, unsigned long code, void *unused)
{
	if (code==SYS_DOWN || code==SYS_HALT) {
		/* Turn the WDT off */
		pcwd_stop();
	}

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct file_operations pcwd_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= pcwd_write,
	.ioctl		= pcwd_ioctl,
	.open		= pcwd_open,
	.release	= pcwd_close,
};

static struct miscdevice pcwd_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&pcwd_fops,
};

static struct file_operations pcwd_temp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= pcwd_temp_read,
	.open		= pcwd_temp_open,
	.release	= pcwd_temp_close,
};

static struct miscdevice temp_miscdev = {
	.minor =	TEMP_MINOR,
	.name =		"temperature",
	.fops =		&pcwd_temp_fops,
};

static struct notifier_block pcwd_notifier = {
	.notifier_call =	pcwd_notify_sys,
};

/*
 *	Init & exit routines
 */

static inline void get_support(void)
{
	if (inb(current_readport) != 0xF0)
		supports_temp = 1;
}

static inline int get_revision(void)
{
	int r = PCWD_REVISION_C;

	spin_lock(&io_lock);
	/* REV A cards use only 2 io ports; test
	 * presumes a floating bus reads as 0xff. */
	if ((inb(current_readport + 2) == 0xFF) ||
	    (inb(current_readport + 3) == 0xFF))
		r=PCWD_REVISION_A;
	spin_unlock(&io_lock);

	return r;
}

static inline char *get_firmware(void)
{
	int one, ten, hund, minor;
	char *ret;

	ret = kmalloc(6, GFP_KERNEL);
	if(ret == NULL)
		return NULL;

	if (set_command_mode()) {
		one = send_isa_command(CMD_ISA_VERSION_INTEGER);
		ten = send_isa_command(CMD_ISA_VERSION_TENTH);
		hund = send_isa_command(CMD_ISA_VERSION_HUNDRETH);
		minor = send_isa_command(CMD_ISA_VERSION_MINOR);
		sprintf(ret, "%c.%c%c%c", one, ten, hund, minor);
	}
	else
		sprintf(ret, "ERROR");

	unset_command_mode();
	return(ret);
}

static inline int get_option_switches(void)
{
	int rv=0;

	if (set_command_mode()) {
		/* Get switch settings */
		rv = send_isa_command(CMD_ISA_SWITCH_SETTINGS);
	}

	unset_command_mode();
	return(rv);
}

static int __devinit pcwatchdog_init(int base_addr)
{
	int ret;
	char *firmware;
	int option_switches;

	cards_found++;
	if (cards_found == 1)
		printk(KERN_INFO PFX "v%s Ken Hollis (kenji@bitgate.com)\n", WD_VER);

	if (cards_found > 1) {
		printk(KERN_ERR PFX "This driver only supports 1 device\n");
		return -ENODEV;
	}

	if (base_addr == 0x0000) {
		printk(KERN_ERR PFX "No I/O-Address for card detected\n");
		return -ENODEV;
	}
	current_readport = base_addr;

	/* Check card's revision */
	revision = get_revision();

	if (!request_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4, "PCWD")) {
		printk(KERN_ERR PFX "I/O address 0x%04x already in use\n",
			current_readport);
		current_readport = 0x0000;
		return -EIO;
	}

	/* Initial variables */
	supports_temp = 0;
	temp_panic = 0;
	initial_status = 0x0000;

	/* get the boot_status */
	pcwd_get_status(&initial_status);

	/* clear the "card caused reboot" flag */
	pcwd_clear_status();

	init_timer(&timer);
	timer.function = pcwd_timer_ping;
	timer.data = 0;

	/*  Disable the board  */
	pcwd_stop();

	/*  Check whether or not the card supports the temperature device */
	get_support();

	/* Get some extra info from the hardware (in command/debug/diag mode) */
	if (revision == PCWD_REVISION_A)
		printk(KERN_INFO PFX "ISA-PC Watchdog (REV.A) detected at port 0x%04x\n", current_readport);
	else if (revision == PCWD_REVISION_C) {
		firmware = get_firmware();
		printk(KERN_INFO PFX "ISA-PC Watchdog (REV.C) detected at port 0x%04x (Firmware version: %s)\n",
			current_readport, firmware);
		kfree(firmware);
		option_switches = get_option_switches();
		printk(KERN_INFO PFX "Option switches (0x%02x): Temperature Reset Enable=%s, Power On Delay=%s\n",
			option_switches,
			((option_switches & 0x10) ? "ON" : "OFF"),
			((option_switches & 0x08) ? "ON" : "OFF"));

		/* Reprogram internal heartbeat to 2 seconds */
		if (set_command_mode()) {
			send_isa_command(CMD_ISA_DELAY_TIME_2SECS);
			unset_command_mode();
		}
	} else {
		/* Should NEVER happen, unless get_revision() fails. */
		printk(KERN_INFO PFX "Unable to get revision\n");
		release_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4);
		current_readport = 0x0000;
		return -1;
	}

	if (supports_temp)
		printk(KERN_INFO PFX "Temperature Option Detected\n");

	if (initial_status & WDIOF_CARDRESET)
		printk(KERN_INFO PFX "Previous reboot was caused by the card\n");

	if (initial_status & WDIOF_OVERHEAT) {
		printk(KERN_EMERG PFX "Card senses a CPU Overheat. Panicking!\n");
		printk(KERN_EMERG PFX "CPU Overheat\n");
	}

	if (initial_status == 0)
		printk(KERN_INFO PFX "No previous trip detected - Cold boot or reset\n");

	/* Check that the heartbeat value is within it's range ; if not reset to the default */
        if (pcwd_set_heartbeat(heartbeat)) {
                pcwd_set_heartbeat(WATCHDOG_HEARTBEAT);
                printk(KERN_INFO PFX "heartbeat value must be 2<=heartbeat<=7200, using %d\n",
                        WATCHDOG_HEARTBEAT);
	}

	ret = register_reboot_notifier(&pcwd_notifier);
	if (ret) {
		printk(KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		release_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4);
		current_readport = 0x0000;
		return ret;
	}

	if (supports_temp) {
		ret = misc_register(&temp_miscdev);
		if (ret) {
			printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
				TEMP_MINOR, ret);
			unregister_reboot_notifier(&pcwd_notifier);
			release_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4);
			current_readport = 0x0000;
			return ret;
		}
	}

	ret = misc_register(&pcwd_miscdev);
	if (ret) {
		printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		if (supports_temp)
			misc_deregister(&temp_miscdev);
		unregister_reboot_notifier(&pcwd_notifier);
		release_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4);
		current_readport = 0x0000;
		return ret;
	}

	printk(KERN_INFO PFX "initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;
}

static void __devexit pcwatchdog_exit(void)
{
	/*  Disable the board  */
	if (!nowayout)
		pcwd_stop();

	/* Deregister */
	misc_deregister(&pcwd_miscdev);
	if (supports_temp)
		misc_deregister(&temp_miscdev);
	unregister_reboot_notifier(&pcwd_notifier);
	release_region(current_readport, (revision == PCWD_REVISION_A) ? 2 : 4);
	current_readport = 0x0000;
}

/*
 *  The ISA cards have a heartbeat bit in one of the registers, which
 *  register is card dependent.  The heartbeat bit is monitored, and if
 *  found, is considered proof that a Berkshire card has been found.
 *  The initial rate is once per second at board start up, then twice
 *  per second for normal operation.
 */
static int __init pcwd_checkcard(int base_addr)
{
	int port0, last_port0;	/* Reg 0, in case it's REV A */
	int port1, last_port1;	/* Register 1 for REV C cards */
	int i;
	int retval;

	if (!request_region (base_addr, 4, "PCWD")) {
		printk (KERN_INFO PFX "Port 0x%04x unavailable\n", base_addr);
		return 0;
	}

	retval = 0;

	port0 = inb_p(base_addr);	/* For REV A boards */
	port1 = inb_p(base_addr + 1);	/* For REV C boards */
	if (port0 != 0xff || port1 != 0xff) {
		/* Not an 'ff' from a floating bus, so must be a card! */
		for (i = 0; i < 4; ++i) {

			msleep(500);

			last_port0 = port0;
			last_port1 = port1;

			port0 = inb_p(base_addr);
			port1 = inb_p(base_addr + 1);

			/* Has either hearbeat bit changed?  */
			if ((port0 ^ last_port0) & WD_HRTBT ||
			    (port1 ^ last_port1) & WD_REVC_HRBT) {
				retval = 1;
				break;
			}
		}
	}
	release_region (base_addr, 4);

	return retval;
}

/*
 * These are the auto-probe addresses available.
 *
 * Revision A only uses ports 0x270 and 0x370.  Revision C introduced 0x350.
 * Revision A has an address range of 2 addresses, while Revision C has 4.
 */
static int pcwd_ioports[] = { 0x270, 0x350, 0x370, 0x000 };

static int __init pcwd_init_module(void)
{
	int i, found = 0;

	spin_lock_init(&io_lock);

	for (i = 0; pcwd_ioports[i] != 0; i++) {
		if (pcwd_checkcard(pcwd_ioports[i])) {
			if (!(pcwatchdog_init(pcwd_ioports[i])))
				found++;
		}
	}

	if (!found) {
		printk (KERN_INFO PFX "No card detected, or port not available\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit pcwd_cleanup_module(void)
{
	if (current_readport)
		pcwatchdog_exit();
	return;
}

module_init(pcwd_init_module);
module_exit(pcwd_cleanup_module);

MODULE_AUTHOR("Ken Hollis <kenji@bitgate.com>");
MODULE_DESCRIPTION("Berkshire ISA-PC Watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS_MISCDEV(TEMP_MINOR);
