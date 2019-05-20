/*
 * PC Watchdog Driver
 * by Ken Hollis (khollis@bitgate.com)
 *
 * Permission granted from Simon Machell (smachell@berkprod.com)
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
 * 961006	Revised some documentation, fixed some cosmetic bugs.  Made
 *		drivers to panic the system if it's overheating at bootup.
 * 961118	Changed some verbiage on some of the output, tidied up
 *		code bits, and added compatibility to 2.1.x.
 * 970912	Enabled board on open and disable on close.
 * 971107	Took account of recent VFS changes (broke read).
 * 971210	Disable board on initialisation in case board already ticking.
 * 971222	Changed open/close for temperature handling
 *		Michael Meskes <meskes@debian.org>.
 * 980112	Used minor numbers from include/linux/miscdevice.h
 * 990403	Clear reset status after reading control status register in
 *		pcwd_showprevstate(). [Marc Boucher <marc@mbsi.ca>]
 * 990605	Made changes to code to support Firmware 1.22a, added
 *		fairly useless proc entry.
 * 990610	removed said useless proc code for the merge <alan>
 * 000403	Removed last traces of proc code. <davej>
 * 011214	Added nowayout module option to override
 *		CONFIG_WATCHDOG_NOWAYOUT <Matt_Domsch@dell.com>
 *		Added timeout module option to override default
 */

/*
 *	A bells and whistles driver is available from http://www.pcwd.de/
 *	More info available at http://www.berkprod.com/ or
 *	http://www.pcwatchdog.com/
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>	/* For module specific items */
#include <linux/moduleparam.h>	/* For new moduleparam's */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/delay.h>	/* For mdelay function */
#include <linux/timer.h>	/* For timer related operations */
#include <linux/jiffies.h>	/* For jiffies stuff */
#include <linux/miscdevice.h>	/* For struct miscdevice */
#include <linux/watchdog.h>	/* For the watchdog specific items */
#include <linux/reboot.h>	/* For kernel_power_off() */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/fs.h>		/* For file operations */
#include <linux/isa.h>		/* For isa devices */
#include <linux/ioport.h>	/* For io-port access */
#include <linux/spinlock.h>	/* For spin_lock/spin_unlock/... */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */
#include <linux/io.h>		/* For inb/outb/... */

/* Module and version information */
#define WATCHDOG_VERSION "1.20"
#define WATCHDOG_DATE "18 Feb 2007"
#define WATCHDOG_DRIVER_NAME "ISA-PC Watchdog"
#define WATCHDOG_NAME "pcwd"
#define DRIVER_VERSION WATCHDOG_DRIVER_NAME " driver, v" WATCHDOG_VERSION "\n"

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
 * These are the auto-probe addresses available.
 *
 * Revision A only uses ports 0x270 and 0x370.  Revision C introduced 0x350.
 * Revision A has an address range of 2 addresses, while Revision C has 4.
 */
#define PCWD_ISA_NR_CARDS	3
static int pcwd_ioports[] = { 0x270, 0x350, 0x370, 0x000 };

/*
 * These are the defines that describe the control status bits for the
 * PCI-PC Watchdog card.
*/
/* Port 1 : Control Status #1 for the PC Watchdog card, revision A. */
#define WD_WDRST		0x01	/* Previously reset state */
#define WD_T110			0x02	/* Temperature overheat sense */
#define WD_HRTBT		0x04	/* Heartbeat sense */
#define WD_RLY2			0x08	/* External relay triggered */
#define WD_SRLY2		0x80	/* Software external relay triggered */
/* Port 1 : Control Status #1 for the PC Watchdog card, revision C. */
#define WD_REVC_WTRP		0x01	/* Watchdog Trip status */
#define WD_REVC_HRBT		0x02	/* Watchdog Heartbeat */
#define WD_REVC_TTRP		0x04	/* Temperature Trip status */
#define WD_REVC_RL2A		0x08	/* Relay 2 activated by
							on-board processor */
#define WD_REVC_RL1A		0x10	/* Relay 1 active */
#define WD_REVC_R2DS		0x40	/* Relay 2 disable */
#define WD_REVC_RLY2		0x80	/* Relay 2 activated? */
/* Port 2 : Control Status #2 */
#define WD_WDIS			0x10	/* Watchdog Disabled */
#define WD_ENTP			0x20	/* Watchdog Enable Temperature Trip */
#define WD_SSEL			0x40	/* Watchdog Switch Select
							(1:SW1 <-> 0:SW2) */
#define WD_WCMD			0x80	/* Watchdog Command Mode */

/* max. time we give an ISA watchdog card to process a command */
/* 500ms for each 4 bit response (according to spec.) */
#define ISA_COMMAND_TIMEOUT     1000

/* Watchdog's internal commands */
#define CMD_ISA_IDLE			0x00
#define CMD_ISA_VERSION_INTEGER		0x01
#define CMD_ISA_VERSION_TENTH		0x02
#define CMD_ISA_VERSION_HUNDRETH	0x03
#define CMD_ISA_VERSION_MINOR		0x04
#define CMD_ISA_SWITCH_SETTINGS		0x05
#define CMD_ISA_RESET_PC		0x06
#define CMD_ISA_ARM_0			0x07
#define CMD_ISA_ARM_30			0x08
#define CMD_ISA_ARM_60			0x09
#define CMD_ISA_DELAY_TIME_2SECS	0x0A
#define CMD_ISA_DELAY_TIME_4SECS	0x0B
#define CMD_ISA_DELAY_TIME_8SECS	0x0C
#define CMD_ISA_RESET_RELAYS		0x0D

/* Watchdog's Dip Switch heartbeat values */
static const int heartbeat_tbl[] = {
	20,	/* OFF-OFF-OFF	= 20 Sec  */
	40,	/* OFF-OFF-ON	= 40 Sec  */
	60,	/* OFF-ON-OFF	=  1 Min  */
	300,	/* OFF-ON-ON	=  5 Min  */
	600,	/* ON-OFF-OFF	= 10 Min  */
	1800,	/* ON-OFF-ON	= 30 Min  */
	3600,	/* ON-ON-OFF	=  1 Hour */
	7200,	/* ON-ON-ON	=  2 hour */
};

/*
 * We are using an kernel timer to do the pinging of the watchdog
 * every ~500ms. We try to set the internal heartbeat of the
 * watchdog to 2 ms.
 */

#define WDT_INTERVAL (HZ/2+1)

/* We can only use 1 card due to the /dev/watchdog restriction */
static int cards_found;

/* internal variables */
static unsigned long open_allowed;
static char expect_close;
static int temp_panic;

/* this is private data for each ISA-PC watchdog card */
static struct {
	char fw_ver_str[6];		/* The cards firmware version */
	int revision;			/* The card's revision */
	int supports_temp;		/* Whether or not the card has
						a temperature device */
	int command_mode;		/* Whether or not the card is in
						command mode */
	int boot_status;		/* The card's boot status */
	int io_addr;			/* The cards I/O address */
	spinlock_t io_lock;		/* the lock for io operations */
	struct timer_list timer;	/* The timer that pings the watchdog */
	unsigned long next_heartbeat;	/* the next_heartbeat for the timer */
} pcwd_private;

/* module parameters */
#define QUIET	0	/* Default */
#define VERBOSE	1	/* Verbose */
#define DEBUG	2	/* print fancy stuff too */
static int debug = QUIET;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug,
		"Debug level: 0=Quiet, 1=Verbose, 2=Debug (default=0)");

/* default heartbeat = delay-time from dip-switches */
#define WATCHDOG_HEARTBEAT 0
static int heartbeat = WATCHDOG_HEARTBEAT;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. "
	"(2 <= heartbeat <= 7200 or 0=delay-time from dip-switches, default="
				__MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 *	Internal functions
 */

static int send_isa_command(int cmd)
{
	int i;
	int control_status;
	int port0, last_port0;	/* Double read for stabilising */

	if (debug >= DEBUG)
		pr_debug("sending following data cmd=0x%02x\n", cmd);

	/* The WCMD bit must be 1 and the command is only 4 bits in size */
	control_status = (cmd & 0x0F) | WD_WCMD;
	outb_p(control_status, pcwd_private.io_addr + 2);
	udelay(ISA_COMMAND_TIMEOUT);

	port0 = inb_p(pcwd_private.io_addr);
	for (i = 0; i < 25; ++i) {
		last_port0 = port0;
		port0 = inb_p(pcwd_private.io_addr);

		if (port0 == last_port0)
			break;	/* Data is stable */

		udelay(250);
	}

	if (debug >= DEBUG)
		pr_debug("received following data for cmd=0x%02x: port0=0x%02x last_port0=0x%02x\n",
			 cmd, port0, last_port0);

	return port0;
}

static int set_command_mode(void)
{
	int i, found = 0, count = 0;

	/* Set the card into command mode */
	spin_lock(&pcwd_private.io_lock);
	while ((!found) && (count < 3)) {
		i = send_isa_command(CMD_ISA_IDLE);

		if (i == 0x00)
			found = 1;
		else if (i == 0xF3) {
			/* Card does not like what we've done to it */
			outb_p(0x00, pcwd_private.io_addr + 2);
			udelay(1200);	/* Spec says wait 1ms */
			outb_p(0x00, pcwd_private.io_addr + 2);
			udelay(ISA_COMMAND_TIMEOUT);
		}
		count++;
	}
	spin_unlock(&pcwd_private.io_lock);
	pcwd_private.command_mode = found;

	if (debug >= DEBUG)
		pr_debug("command_mode=%d\n", pcwd_private.command_mode);

	return found;
}

static void unset_command_mode(void)
{
	/* Set the card into normal mode */
	spin_lock(&pcwd_private.io_lock);
	outb_p(0x00, pcwd_private.io_addr + 2);
	udelay(ISA_COMMAND_TIMEOUT);
	spin_unlock(&pcwd_private.io_lock);

	pcwd_private.command_mode = 0;

	if (debug >= DEBUG)
		pr_debug("command_mode=%d\n", pcwd_private.command_mode);
}

static inline void pcwd_check_temperature_support(void)
{
	if (inb(pcwd_private.io_addr) != 0xF0)
		pcwd_private.supports_temp = 1;
}

static inline void pcwd_get_firmware(void)
{
	int one, ten, hund, minor;

	strcpy(pcwd_private.fw_ver_str, "ERROR");

	if (set_command_mode()) {
		one = send_isa_command(CMD_ISA_VERSION_INTEGER);
		ten = send_isa_command(CMD_ISA_VERSION_TENTH);
		hund = send_isa_command(CMD_ISA_VERSION_HUNDRETH);
		minor = send_isa_command(CMD_ISA_VERSION_MINOR);
		sprintf(pcwd_private.fw_ver_str, "%c.%c%c%c",
					one, ten, hund, minor);
	}
	unset_command_mode();

	return;
}

static inline int pcwd_get_option_switches(void)
{
	int option_switches = 0;

	if (set_command_mode()) {
		/* Get switch settings */
		option_switches = send_isa_command(CMD_ISA_SWITCH_SETTINGS);
	}

	unset_command_mode();
	return option_switches;
}

static void pcwd_show_card_info(void)
{
	int option_switches;

	/* Get some extra info from the hardware (in command/debug/diag mode) */
	if (pcwd_private.revision == PCWD_REVISION_A)
		pr_info("ISA-PC Watchdog (REV.A) detected at port 0x%04x\n",
			pcwd_private.io_addr);
	else if (pcwd_private.revision == PCWD_REVISION_C) {
		pcwd_get_firmware();
		pr_info("ISA-PC Watchdog (REV.C) detected at port 0x%04x (Firmware version: %s)\n",
			pcwd_private.io_addr, pcwd_private.fw_ver_str);
		option_switches = pcwd_get_option_switches();
		pr_info("Option switches (0x%02x): Temperature Reset Enable=%s, Power On Delay=%s\n",
			option_switches,
			((option_switches & 0x10) ? "ON" : "OFF"),
			((option_switches & 0x08) ? "ON" : "OFF"));

		/* Reprogram internal heartbeat to 2 seconds */
		if (set_command_mode()) {
			send_isa_command(CMD_ISA_DELAY_TIME_2SECS);
			unset_command_mode();
		}
	}

	if (pcwd_private.supports_temp)
		pr_info("Temperature Option Detected\n");

	if (pcwd_private.boot_status & WDIOF_CARDRESET)
		pr_info("Previous reboot was caused by the card\n");

	if (pcwd_private.boot_status & WDIOF_OVERHEAT) {
		pr_emerg("Card senses a CPU Overheat. Panicking!\n");
		pr_emerg("CPU Overheat\n");
	}

	if (pcwd_private.boot_status == 0)
		pr_info("No previous trip detected - Cold boot or reset\n");
}

static void pcwd_timer_ping(struct timer_list *unused)
{
	int wdrst_stat;

	/* If we got a heartbeat pulse within the WDT_INTERVAL
	 * we agree to ping the WDT */
	if (time_before(jiffies, pcwd_private.next_heartbeat)) {
		/* Ping the watchdog */
		spin_lock(&pcwd_private.io_lock);
		if (pcwd_private.revision == PCWD_REVISION_A) {
			/*  Rev A cards are reset by setting the
			    WD_WDRST bit in register 1 */
			wdrst_stat = inb_p(pcwd_private.io_addr);
			wdrst_stat &= 0x0F;
			wdrst_stat |= WD_WDRST;

			outb_p(wdrst_stat, pcwd_private.io_addr + 1);
		} else {
			/* Re-trigger watchdog by writing to port 0 */
			outb_p(0x00, pcwd_private.io_addr);
		}

		/* Re-set the timer interval */
		mod_timer(&pcwd_private.timer, jiffies + WDT_INTERVAL);

		spin_unlock(&pcwd_private.io_lock);
	} else {
		pr_warn("Heartbeat lost! Will not ping the watchdog\n");
	}
}

static int pcwd_start(void)
{
	int stat_reg;

	pcwd_private.next_heartbeat = jiffies + (heartbeat * HZ);

	/* Start the timer */
	mod_timer(&pcwd_private.timer, jiffies + WDT_INTERVAL);

	/* Enable the port */
	if (pcwd_private.revision == PCWD_REVISION_C) {
		spin_lock(&pcwd_private.io_lock);
		outb_p(0x00, pcwd_private.io_addr + 3);
		udelay(ISA_COMMAND_TIMEOUT);
		stat_reg = inb_p(pcwd_private.io_addr + 2);
		spin_unlock(&pcwd_private.io_lock);
		if (stat_reg & WD_WDIS) {
			pr_info("Could not start watchdog\n");
			return -EIO;
		}
	}

	if (debug >= VERBOSE)
		pr_debug("Watchdog started\n");

	return 0;
}

static int pcwd_stop(void)
{
	int stat_reg;

	/* Stop the timer */
	del_timer(&pcwd_private.timer);

	/*  Disable the board  */
	if (pcwd_private.revision == PCWD_REVISION_C) {
		spin_lock(&pcwd_private.io_lock);
		outb_p(0xA5, pcwd_private.io_addr + 3);
		udelay(ISA_COMMAND_TIMEOUT);
		outb_p(0xA5, pcwd_private.io_addr + 3);
		udelay(ISA_COMMAND_TIMEOUT);
		stat_reg = inb_p(pcwd_private.io_addr + 2);
		spin_unlock(&pcwd_private.io_lock);
		if ((stat_reg & WD_WDIS) == 0) {
			pr_info("Could not stop watchdog\n");
			return -EIO;
		}
	}

	if (debug >= VERBOSE)
		pr_debug("Watchdog stopped\n");

	return 0;
}

static int pcwd_keepalive(void)
{
	/* user land ping */
	pcwd_private.next_heartbeat = jiffies + (heartbeat * HZ);

	if (debug >= DEBUG)
		pr_debug("Watchdog keepalive signal send\n");

	return 0;
}

static int pcwd_set_heartbeat(int t)
{
	if (t < 2 || t > 7200) /* arbitrary upper limit */
		return -EINVAL;

	heartbeat = t;

	if (debug >= VERBOSE)
		pr_debug("New heartbeat: %d\n", heartbeat);

	return 0;
}

static int pcwd_get_status(int *status)
{
	int control_status;

	*status = 0;
	spin_lock(&pcwd_private.io_lock);
	if (pcwd_private.revision == PCWD_REVISION_A)
		/* Rev A cards return status information from
		 * the base register, which is used for the
		 * temperature in other cards. */
		control_status = inb(pcwd_private.io_addr);
	else {
		/* Rev C cards return card status in the base
		 * address + 1 register. And use different bits
		 * to indicate a card initiated reset, and an
		 * over-temperature condition. And the reboot
		 * status can be reset. */
		control_status = inb(pcwd_private.io_addr + 1);
	}
	spin_unlock(&pcwd_private.io_lock);

	if (pcwd_private.revision == PCWD_REVISION_A) {
		if (control_status & WD_WDRST)
			*status |= WDIOF_CARDRESET;

		if (control_status & WD_T110) {
			*status |= WDIOF_OVERHEAT;
			if (temp_panic) {
				pr_info("Temperature overheat trip!\n");
				kernel_power_off();
			}
		}
	} else {
		if (control_status & WD_REVC_WTRP)
			*status |= WDIOF_CARDRESET;

		if (control_status & WD_REVC_TTRP) {
			*status |= WDIOF_OVERHEAT;
			if (temp_panic) {
				pr_info("Temperature overheat trip!\n");
				kernel_power_off();
			}
		}
	}

	return 0;
}

static int pcwd_clear_status(void)
{
	int control_status;

	if (pcwd_private.revision == PCWD_REVISION_C) {
		spin_lock(&pcwd_private.io_lock);

		if (debug >= VERBOSE)
			pr_info("clearing watchdog trip status\n");

		control_status = inb_p(pcwd_private.io_addr + 1);

		if (debug >= DEBUG) {
			pr_debug("status was: 0x%02x\n", control_status);
			pr_debug("sending: 0x%02x\n",
				 (control_status & WD_REVC_R2DS));
		}

		/* clear reset status & Keep Relay 2 disable state as it is */
		outb_p((control_status & WD_REVC_R2DS),
						pcwd_private.io_addr + 1);

		spin_unlock(&pcwd_private.io_lock);
	}
	return 0;
}

static int pcwd_get_temperature(int *temperature)
{
	/* check that port 0 gives temperature info and no command results */
	if (pcwd_private.command_mode)
		return -1;

	*temperature = 0;
	if (!pcwd_private.supports_temp)
		return -ENODEV;

	/*
	 * Convert celsius to fahrenheit, since this was
	 * the decided 'standard' for this return value.
	 */
	spin_lock(&pcwd_private.io_lock);
	*temperature = ((inb(pcwd_private.io_addr)) * 9 / 5) + 32;
	spin_unlock(&pcwd_private.io_lock);

	if (debug >= DEBUG) {
		pr_debug("temperature is: %d F\n", *temperature);
	}

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static long pcwd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rv;
	int status;
	int temperature;
	int new_heartbeat;
	int __user *argp = (int __user *)arg;
	static const struct watchdog_info ident = {
		.options =		WDIOF_OVERHEAT |
					WDIOF_CARDRESET |
					WDIOF_KEEPALIVEPING |
					WDIOF_SETTIMEOUT |
					WDIOF_MAGICCLOSE,
		.firmware_version =	1,
		.identity =		"PCWD",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
		pcwd_get_status(&status);
		return put_user(status, argp);

	case WDIOC_GETBOOTSTATUS:
		return put_user(pcwd_private.boot_status, argp);

	case WDIOC_GETTEMP:
		if (pcwd_get_temperature(&temperature))
			return -EFAULT;

		return put_user(temperature, argp);

	case WDIOC_SETOPTIONS:
		if (pcwd_private.revision == PCWD_REVISION_C) {
			if (get_user(rv, argp))
				return -EFAULT;

			if (rv & WDIOS_DISABLECARD) {
				status = pcwd_stop();
				if (status < 0)
					return status;
			}
			if (rv & WDIOS_ENABLECARD) {
				status = pcwd_start();
				if (status < 0)
					return status;
			}
			if (rv & WDIOS_TEMPPANIC)
				temp_panic = 1;
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

	default:
		return -ENOTTY;
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
	if (test_and_set_bit(0, &open_allowed))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);
	/* Activate */
	pcwd_start();
	pcwd_keepalive();
	return stream_open(inode, file);
}

static int pcwd_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42)
		pcwd_stop();
	else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		pcwd_keepalive();
	}
	expect_close = 0;
	clear_bit(0, &open_allowed);
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
	if (!pcwd_private.supports_temp)
		return -ENODEV;

	return stream_open(inode, file);
}

static int pcwd_temp_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations pcwd_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= pcwd_write,
	.unlocked_ioctl	= pcwd_ioctl,
	.open		= pcwd_open,
	.release	= pcwd_close,
};

static struct miscdevice pcwd_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&pcwd_fops,
};

static const struct file_operations pcwd_temp_fops = {
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

/*
 *	Init & exit routines
 */

static inline int get_revision(void)
{
	int r = PCWD_REVISION_C;

	spin_lock(&pcwd_private.io_lock);
	/* REV A cards use only 2 io ports; test
	 * presumes a floating bus reads as 0xff. */
	if ((inb(pcwd_private.io_addr + 2) == 0xFF) ||
	    (inb(pcwd_private.io_addr + 3) == 0xFF))
		r = PCWD_REVISION_A;
	spin_unlock(&pcwd_private.io_lock);

	return r;
}

/*
 *  The ISA cards have a heartbeat bit in one of the registers, which
 *  register is card dependent.  The heartbeat bit is monitored, and if
 *  found, is considered proof that a Berkshire card has been found.
 *  The initial rate is once per second at board start up, then twice
 *  per second for normal operation.
 */
static int pcwd_isa_match(struct device *dev, unsigned int id)
{
	int base_addr = pcwd_ioports[id];
	int port0, last_port0;	/* Reg 0, in case it's REV A */
	int port1, last_port1;	/* Register 1 for REV C cards */
	int i;
	int retval;

	if (debug >= DEBUG)
		pr_debug("pcwd_isa_match id=%d\n", id);

	if (!request_region(base_addr, 4, "PCWD")) {
		pr_info("Port 0x%04x unavailable\n", base_addr);
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
	release_region(base_addr, 4);

	return retval;
}

static int pcwd_isa_probe(struct device *dev, unsigned int id)
{
	int ret;

	if (debug >= DEBUG)
		pr_debug("pcwd_isa_probe id=%d\n", id);

	cards_found++;
	if (cards_found == 1)
		pr_info("v%s Ken Hollis (kenji@bitgate.com)\n",
							WATCHDOG_VERSION);

	if (cards_found > 1) {
		pr_err("This driver only supports 1 device\n");
		return -ENODEV;
	}

	if (pcwd_ioports[id] == 0x0000) {
		pr_err("No I/O-Address for card detected\n");
		return -ENODEV;
	}
	pcwd_private.io_addr = pcwd_ioports[id];

	spin_lock_init(&pcwd_private.io_lock);

	/* Check card's revision */
	pcwd_private.revision = get_revision();

	if (!request_region(pcwd_private.io_addr,
		(pcwd_private.revision == PCWD_REVISION_A) ? 2 : 4, "PCWD")) {
		pr_err("I/O address 0x%04x already in use\n",
		       pcwd_private.io_addr);
		ret = -EIO;
		goto error_request_region;
	}

	/* Initial variables */
	pcwd_private.supports_temp = 0;
	temp_panic = 0;
	pcwd_private.boot_status = 0x0000;

	/* get the boot_status */
	pcwd_get_status(&pcwd_private.boot_status);

	/* clear the "card caused reboot" flag */
	pcwd_clear_status();

	timer_setup(&pcwd_private.timer, pcwd_timer_ping, 0);

	/*  Disable the board  */
	pcwd_stop();

	/*  Check whether or not the card supports the temperature device */
	pcwd_check_temperature_support();

	/* Show info about the card itself */
	pcwd_show_card_info();

	/* If heartbeat = 0 then we use the heartbeat from the dip-switches */
	if (heartbeat == 0)
		heartbeat = heartbeat_tbl[(pcwd_get_option_switches() & 0x07)];

	/* Check that the heartbeat value is within it's range;
	   if not reset to the default */
	if (pcwd_set_heartbeat(heartbeat)) {
		pcwd_set_heartbeat(WATCHDOG_HEARTBEAT);
		pr_info("heartbeat value must be 2 <= heartbeat <= 7200, using %d\n",
			WATCHDOG_HEARTBEAT);
	}

	if (pcwd_private.supports_temp) {
		ret = misc_register(&temp_miscdev);
		if (ret) {
			pr_err("cannot register miscdev on minor=%d (err=%d)\n",
			       TEMP_MINOR, ret);
			goto error_misc_register_temp;
		}
	}

	ret = misc_register(&pcwd_miscdev);
	if (ret) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, ret);
		goto error_misc_register_watchdog;
	}

	pr_info("initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;

error_misc_register_watchdog:
	if (pcwd_private.supports_temp)
		misc_deregister(&temp_miscdev);
error_misc_register_temp:
	release_region(pcwd_private.io_addr,
			(pcwd_private.revision == PCWD_REVISION_A) ? 2 : 4);
error_request_region:
	pcwd_private.io_addr = 0x0000;
	cards_found--;
	return ret;
}

static int pcwd_isa_remove(struct device *dev, unsigned int id)
{
	if (debug >= DEBUG)
		pr_debug("pcwd_isa_remove id=%d\n", id);

	if (!pcwd_private.io_addr)
		return 1;

	/*  Disable the board  */
	if (!nowayout)
		pcwd_stop();

	/* Deregister */
	misc_deregister(&pcwd_miscdev);
	if (pcwd_private.supports_temp)
		misc_deregister(&temp_miscdev);
	release_region(pcwd_private.io_addr,
			(pcwd_private.revision == PCWD_REVISION_A) ? 2 : 4);
	pcwd_private.io_addr = 0x0000;
	cards_found--;

	return 0;
}

static void pcwd_isa_shutdown(struct device *dev, unsigned int id)
{
	if (debug >= DEBUG)
		pr_debug("pcwd_isa_shutdown id=%d\n", id);

	pcwd_stop();
}

static struct isa_driver pcwd_isa_driver = {
	.match		= pcwd_isa_match,
	.probe		= pcwd_isa_probe,
	.remove		= pcwd_isa_remove,
	.shutdown	= pcwd_isa_shutdown,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= WATCHDOG_NAME,
	},
};

module_isa_driver(pcwd_isa_driver, PCWD_ISA_NR_CARDS);

MODULE_AUTHOR("Ken Hollis <kenji@bitgate.com>, "
		"Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("Berkshire ISA-PC Watchdog driver");
MODULE_VERSION(WATCHDOG_VERSION);
MODULE_LICENSE("GPL");
