/***************************************************************************
 *   Copyright (C) 2006 by Hans Edgington <hans@edgington.nl>              *
 *   Copyright (C) 2007-2009 Hans de Goede <hdegoede@redhat.com>           *
 *   Copyright (C) 2010 Giel van Schijndel <me@mortis.eu>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>

#define DRVNAME "f71808e_wdt"

#define SIO_F71808FG_LD_WDT	0x07	/* Watchdog timer logical device */
#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to disable Super-I/O */

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* Device revision */
#define SIO_REG_MANID		0x23	/* Fintek ID (2 bytes) */
#define SIO_REG_ROM_ADDR_SEL	0x27	/* ROM address select */
#define SIO_F81866_REG_PORT_SEL	0x27	/* F81866 Multi-Function Register */
#define SIO_REG_MFUNCT1		0x29	/* Multi function select 1 */
#define SIO_REG_MFUNCT2		0x2a	/* Multi function select 2 */
#define SIO_REG_MFUNCT3		0x2b	/* Multi function select 3 */
#define SIO_F81866_REG_GPIO1	0x2c	/* F81866 GPIO1 Enable Register */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_FINTEK_ID		0x1934	/* Manufacturers ID */
#define SIO_F71808_ID		0x0901	/* Chipset ID */
#define SIO_F71858_ID		0x0507	/* Chipset ID */
#define SIO_F71862_ID		0x0601	/* Chipset ID */
#define SIO_F71868_ID		0x1106	/* Chipset ID */
#define SIO_F71869_ID		0x0814	/* Chipset ID */
#define SIO_F71869A_ID		0x1007	/* Chipset ID */
#define SIO_F71882_ID		0x0541	/* Chipset ID */
#define SIO_F71889_ID		0x0723	/* Chipset ID */
#define SIO_F81865_ID		0x0704	/* Chipset ID */
#define SIO_F81866_ID		0x1010	/* Chipset ID */

#define F71808FG_REG_WDO_CONF		0xf0
#define F71808FG_REG_WDT_CONF		0xf5
#define F71808FG_REG_WD_TIME		0xf6

#define F71808FG_FLAG_WDOUT_EN		7

#define F71808FG_FLAG_WDTMOUT_STS	6
#define F71808FG_FLAG_WD_EN		5
#define F71808FG_FLAG_WD_PULSE		4
#define F71808FG_FLAG_WD_UNIT		3

#define F81865_REG_WDO_CONF		0xfa
#define F81865_FLAG_WDOUT_EN		0

/* Default values */
#define WATCHDOG_TIMEOUT	60	/* 1 minute default timeout */
#define WATCHDOG_MAX_TIMEOUT	(60 * 255)
#define WATCHDOG_PULSE_WIDTH	125	/* 125 ms, default pulse width for
					   watchdog signal */
#define WATCHDOG_F71862FG_PIN	63	/* default watchdog reset output
					   pin number 63 */

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static const int max_timeout = WATCHDOG_MAX_TIMEOUT;
static int timeout = WATCHDOG_TIMEOUT;	/* default timeout in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. 1<= timeout <="
			__MODULE_STRING(WATCHDOG_MAX_TIMEOUT) " (default="
			__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static unsigned int pulse_width = WATCHDOG_PULSE_WIDTH;
module_param(pulse_width, uint, 0);
MODULE_PARM_DESC(pulse_width,
	"Watchdog signal pulse width. 0(=level), 1, 25, 30, 125, 150, 5000 or 6000 ms"
			" (default=" __MODULE_STRING(WATCHDOG_PULSE_WIDTH) ")");

static unsigned int f71862fg_pin = WATCHDOG_F71862FG_PIN;
module_param(f71862fg_pin, uint, 0);
MODULE_PARM_DESC(f71862fg_pin,
	"Watchdog f71862fg reset output pin configuration. Choose pin 56 or 63"
			" (default=" __MODULE_STRING(WATCHDOG_F71862FG_PIN)")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0444);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

static unsigned int start_withtimeout;
module_param(start_withtimeout, uint, 0);
MODULE_PARM_DESC(start_withtimeout, "Start watchdog timer on module load with"
	" given initial timeout. Zero (default) disables this feature.");

enum chips { f71808fg, f71858fg, f71862fg, f71868, f71869, f71882fg, f71889fg,
	     f81865, f81866};

static const char *f71808e_names[] = {
	"f71808fg",
	"f71858fg",
	"f71862fg",
	"f71868",
	"f71869",
	"f71882fg",
	"f71889fg",
	"f81865",
	"f81866",
};

/* Super-I/O Function prototypes */
static inline int superio_inb(int base, int reg);
static inline int superio_inw(int base, int reg);
static inline void superio_outb(int base, int reg, u8 val);
static inline void superio_set_bit(int base, int reg, int bit);
static inline void superio_clear_bit(int base, int reg, int bit);
static inline int superio_enter(int base);
static inline void superio_select(int base, int ld);
static inline void superio_exit(int base);

struct watchdog_data {
	unsigned short	sioaddr;
	enum chips	type;
	unsigned long	opened;
	struct mutex	lock;
	char		expect_close;
	struct watchdog_info ident;

	unsigned short	timeout;
	u8		timer_val;	/* content for the wd_time register */
	char		minutes_mode;
	u8		pulse_val;	/* pulse width flag */
	char		pulse_mode;	/* enable pulse output mode? */
	char		caused_reboot;	/* last reboot was by the watchdog */
};

static struct watchdog_data watchdog = {
	.lock = __MUTEX_INITIALIZER(watchdog.lock),
};

/* Super I/O functions */
static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int superio_inw(int base, int reg)
{
	int val;
	val  = superio_inb(base, reg) << 8;
	val |= superio_inb(base, reg + 1);
	return val;
}

static inline void superio_outb(int base, int reg, u8 val)
{
	outb(reg, base);
	outb(val, base + 1);
}

static inline void superio_set_bit(int base, int reg, int bit)
{
	unsigned long val = superio_inb(base, reg);
	__set_bit(bit, &val);
	superio_outb(base, reg, val);
}

static inline void superio_clear_bit(int base, int reg, int bit)
{
	unsigned long val = superio_inb(base, reg);
	__clear_bit(bit, &val);
	superio_outb(base, reg, val);
}

static inline int superio_enter(int base)
{
	/* Don't step on other drivers' I/O space by accident */
	if (!request_muxed_region(base, 2, DRVNAME)) {
		pr_err("I/O address 0x%04x already in use\n", (int)base);
		return -EBUSY;
	}

	/* according to the datasheet the key must be sent twice! */
	outb(SIO_UNLOCK_KEY, base);
	outb(SIO_UNLOCK_KEY, base);

	return 0;
}

static inline void superio_select(int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
	release_region(base, 2);
}

static int watchdog_set_timeout(int timeout)
{
	if (timeout <= 0
	 || timeout >  max_timeout) {
		pr_err("watchdog timeout out of range\n");
		return -EINVAL;
	}

	mutex_lock(&watchdog.lock);

	watchdog.timeout = timeout;
	if (timeout > 0xff) {
		watchdog.timer_val = DIV_ROUND_UP(timeout, 60);
		watchdog.minutes_mode = true;
	} else {
		watchdog.timer_val = timeout;
		watchdog.minutes_mode = false;
	}

	mutex_unlock(&watchdog.lock);

	return 0;
}

static int watchdog_set_pulse_width(unsigned int pw)
{
	int err = 0;
	unsigned int t1 = 25, t2 = 125, t3 = 5000;

	if (watchdog.type == f71868) {
		t1 = 30;
		t2 = 150;
		t3 = 6000;
	}

	mutex_lock(&watchdog.lock);

	if        (pw <=  1) {
		watchdog.pulse_val = 0;
	} else if (pw <= t1) {
		watchdog.pulse_val = 1;
	} else if (pw <= t2) {
		watchdog.pulse_val = 2;
	} else if (pw <= t3) {
		watchdog.pulse_val = 3;
	} else {
		pr_err("pulse width out of range\n");
		err = -EINVAL;
		goto exit_unlock;
	}

	watchdog.pulse_mode = pw;

exit_unlock:
	mutex_unlock(&watchdog.lock);
	return err;
}

static int watchdog_keepalive(void)
{
	int err = 0;

	mutex_lock(&watchdog.lock);
	err = superio_enter(watchdog.sioaddr);
	if (err)
		goto exit_unlock;
	superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);

	if (watchdog.minutes_mode)
		/* select minutes for timer units */
		superio_set_bit(watchdog.sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_UNIT);
	else
		/* select seconds for timer units */
		superio_clear_bit(watchdog.sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_UNIT);

	/* Set timer value */
	superio_outb(watchdog.sioaddr, F71808FG_REG_WD_TIME,
			   watchdog.timer_val);

	superio_exit(watchdog.sioaddr);

exit_unlock:
	mutex_unlock(&watchdog.lock);
	return err;
}

static int f71862fg_pin_configure(unsigned short ioaddr)
{
	/* When ioaddr is non-zero the calling function has to take care of
	   mutex handling and superio preparation! */

	if (f71862fg_pin == 63) {
		if (ioaddr) {
			/* SPI must be disabled first to use this pin! */
			superio_clear_bit(ioaddr, SIO_REG_ROM_ADDR_SEL, 6);
			superio_set_bit(ioaddr, SIO_REG_MFUNCT3, 4);
		}
	} else if (f71862fg_pin == 56) {
		if (ioaddr)
			superio_set_bit(ioaddr, SIO_REG_MFUNCT1, 1);
	} else {
		pr_err("Invalid argument f71862fg_pin=%d\n", f71862fg_pin);
		return -EINVAL;
	}
	return 0;
}

static int watchdog_start(void)
{
	/* Make sure we don't die as soon as the watchdog is enabled below */
	int err = watchdog_keepalive();
	if (err)
		return err;

	mutex_lock(&watchdog.lock);
	err = superio_enter(watchdog.sioaddr);
	if (err)
		goto exit_unlock;
	superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);

	/* Watchdog pin configuration */
	switch (watchdog.type) {
	case f71808fg:
		/* Set pin 21 to GPIO23/WDTRST#, then to WDTRST# */
		superio_clear_bit(watchdog.sioaddr, SIO_REG_MFUNCT2, 3);
		superio_clear_bit(watchdog.sioaddr, SIO_REG_MFUNCT3, 3);
		break;

	case f71862fg:
		err = f71862fg_pin_configure(watchdog.sioaddr);
		if (err)
			goto exit_superio;
		break;

	case f71868:
	case f71869:
		/* GPIO14 --> WDTRST# */
		superio_clear_bit(watchdog.sioaddr, SIO_REG_MFUNCT1, 4);
		break;

	case f71882fg:
		/* Set pin 56 to WDTRST# */
		superio_set_bit(watchdog.sioaddr, SIO_REG_MFUNCT1, 1);
		break;

	case f71889fg:
		/* set pin 40 to WDTRST# */
		superio_outb(watchdog.sioaddr, SIO_REG_MFUNCT3,
			superio_inb(watchdog.sioaddr, SIO_REG_MFUNCT3) & 0xcf);
		break;

	case f81865:
		/* Set pin 70 to WDTRST# */
		superio_clear_bit(watchdog.sioaddr, SIO_REG_MFUNCT3, 5);
		break;

	case f81866:
		/* Set pin 70 to WDTRST# */
		superio_clear_bit(watchdog.sioaddr, SIO_F81866_REG_PORT_SEL,
				  BIT(3) | BIT(0));
		superio_set_bit(watchdog.sioaddr, SIO_F81866_REG_PORT_SEL,
				BIT(2));
		/*
		 * GPIO1 Control Register when 27h BIT3:2 = 01 & BIT0 = 0.
		 * The PIN 70(GPIO15/WDTRST) is controlled by 2Ch:
		 *     BIT5: 0 -> WDTRST#
		 *           1 -> GPIO15
		 */
		superio_clear_bit(watchdog.sioaddr, SIO_F81866_REG_GPIO1,
				  BIT(5));
		break;

	default:
		/*
		 * 'default' label to shut up the compiler and catch
		 * programmer errors
		 */
		err = -ENODEV;
		goto exit_superio;
	}

	superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);
	superio_set_bit(watchdog.sioaddr, SIO_REG_ENABLE, 0);

	if (watchdog.type == f81865 || watchdog.type == f81866)
		superio_set_bit(watchdog.sioaddr, F81865_REG_WDO_CONF,
				F81865_FLAG_WDOUT_EN);
	else
		superio_set_bit(watchdog.sioaddr, F71808FG_REG_WDO_CONF,
				F71808FG_FLAG_WDOUT_EN);

	superio_set_bit(watchdog.sioaddr, F71808FG_REG_WDT_CONF,
			F71808FG_FLAG_WD_EN);

	if (watchdog.pulse_mode) {
		/* Select "pulse" output mode with given duration */
		u8 wdt_conf = superio_inb(watchdog.sioaddr,
				F71808FG_REG_WDT_CONF);

		/* Set WD_PSWIDTH bits (1:0) */
		wdt_conf = (wdt_conf & 0xfc) | (watchdog.pulse_val & 0x03);
		/* Set WD_PULSE to "pulse" mode */
		wdt_conf |= BIT(F71808FG_FLAG_WD_PULSE);

		superio_outb(watchdog.sioaddr, F71808FG_REG_WDT_CONF,
				wdt_conf);
	} else {
		/* Select "level" output mode */
		superio_clear_bit(watchdog.sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_PULSE);
	}

exit_superio:
	superio_exit(watchdog.sioaddr);
exit_unlock:
	mutex_unlock(&watchdog.lock);

	return err;
}

static int watchdog_stop(void)
{
	int err = 0;

	mutex_lock(&watchdog.lock);
	err = superio_enter(watchdog.sioaddr);
	if (err)
		goto exit_unlock;
	superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);

	superio_clear_bit(watchdog.sioaddr, F71808FG_REG_WDT_CONF,
			F71808FG_FLAG_WD_EN);

	superio_exit(watchdog.sioaddr);

exit_unlock:
	mutex_unlock(&watchdog.lock);

	return err;
}

static int watchdog_get_status(void)
{
	int status = 0;

	mutex_lock(&watchdog.lock);
	status = (watchdog.caused_reboot) ? WDIOF_CARDRESET : 0;
	mutex_unlock(&watchdog.lock);

	return status;
}

static bool watchdog_is_running(void)
{
	/*
	 * if we fail to determine the watchdog's status assume it to be
	 * running to be on the safe side
	 */
	bool is_running = true;

	mutex_lock(&watchdog.lock);
	if (superio_enter(watchdog.sioaddr))
		goto exit_unlock;
	superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);

	is_running = (superio_inb(watchdog.sioaddr, SIO_REG_ENABLE) & BIT(0))
		&& (superio_inb(watchdog.sioaddr, F71808FG_REG_WDT_CONF)
			& BIT(F71808FG_FLAG_WD_EN));

	superio_exit(watchdog.sioaddr);

exit_unlock:
	mutex_unlock(&watchdog.lock);
	return is_running;
}

/* /dev/watchdog api */

static int watchdog_open(struct inode *inode, struct file *file)
{
	int err;

	/* If the watchdog is alive we don't need to start it again */
	if (test_and_set_bit(0, &watchdog.opened))
		return -EBUSY;

	err = watchdog_start();
	if (err) {
		clear_bit(0, &watchdog.opened);
		return err;
	}

	if (nowayout)
		__module_get(THIS_MODULE);

	watchdog.expect_close = 0;
	return nonseekable_open(inode, file);
}

static int watchdog_release(struct inode *inode, struct file *file)
{
	clear_bit(0, &watchdog.opened);

	if (!watchdog.expect_close) {
		watchdog_keepalive();
		pr_crit("Unexpected close, not stopping watchdog!\n");
	} else if (!nowayout) {
		watchdog_stop();
	}
	return 0;
}

/*
 *      watchdog_write:
 *      @file: file handle to the watchdog
 *      @buf: buffer to write
 *      @count: count of bytes
 *      @ppos: pointer to the position to write. No seeks allowed
 *
 *      A write to a watchdog device is defined as a keepalive signal. Any
 *      write of data will do, as we we don't define content meaning.
 */

static ssize_t watchdog_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			bool expect_close = false;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				expect_close = (c == 'V');
			}

			/* Properly order writes across fork()ed processes */
			mutex_lock(&watchdog.lock);
			watchdog.expect_close = expect_close;
			mutex_unlock(&watchdog.lock);
		}

		/* someone wrote to us, we should restart timer */
		watchdog_keepalive();
	}
	return count;
}

/*
 *      watchdog_ioctl:
 *      @inode: inode of the device
 *      @file: file handle to the device
 *      @cmd: watchdog command
 *      @arg: argument pointer
 *
 *      The watchdog API defines a common set of functions for all watchdogs
 *      according to their available features.
 */
static long watchdog_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	int status;
	int new_options;
	int new_timeout;
	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	uarg.i = (int __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(uarg.ident, &watchdog.ident,
			sizeof(watchdog.ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
		status = watchdog_get_status();
		if (status < 0)
			return status;
		return put_user(status, uarg.i);

	case WDIOC_GETBOOTSTATUS:
		return put_user(0, uarg.i);

	case WDIOC_SETOPTIONS:
		if (get_user(new_options, uarg.i))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD)
			watchdog_stop();

		if (new_options & WDIOS_ENABLECARD)
			return watchdog_start();


	case WDIOC_KEEPALIVE:
		watchdog_keepalive();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, uarg.i))
			return -EFAULT;

		if (watchdog_set_timeout(new_timeout))
			return -EINVAL;

		watchdog_keepalive();
		/* Fall */

	case WDIOC_GETTIMEOUT:
		return put_user(watchdog.timeout, uarg.i);

	default:
		return -ENOTTY;

	}
}

static int watchdog_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		watchdog_stop();
	return NOTIFY_DONE;
}

static const struct file_operations watchdog_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= watchdog_open,
	.release	= watchdog_release,
	.write		= watchdog_write,
	.unlocked_ioctl	= watchdog_ioctl,
};

static struct miscdevice watchdog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &watchdog_fops,
};

static struct notifier_block watchdog_notifier = {
	.notifier_call = watchdog_notify_sys,
};

static int __init watchdog_init(int sioaddr)
{
	int wdt_conf, err = 0;

	/* No need to lock watchdog.lock here because no entry points
	 * into the module have been registered yet.
	 */
	watchdog.sioaddr = sioaddr;
	watchdog.ident.options = WDIOC_SETTIMEOUT
				| WDIOF_MAGICCLOSE
				| WDIOF_KEEPALIVEPING;

	snprintf(watchdog.ident.identity,
		sizeof(watchdog.ident.identity), "%s watchdog",
		f71808e_names[watchdog.type]);

	err = superio_enter(sioaddr);
	if (err)
		return err;
	superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);

	wdt_conf = superio_inb(sioaddr, F71808FG_REG_WDT_CONF);
	watchdog.caused_reboot = wdt_conf & BIT(F71808FG_FLAG_WDTMOUT_STS);

	superio_exit(sioaddr);

	err = watchdog_set_timeout(timeout);
	if (err)
		return err;
	err = watchdog_set_pulse_width(pulse_width);
	if (err)
		return err;

	err = register_reboot_notifier(&watchdog_notifier);
	if (err)
		return err;

	err = misc_register(&watchdog_miscdev);
	if (err) {
		pr_err("cannot register miscdev on minor=%d\n",
		       watchdog_miscdev.minor);
		goto exit_reboot;
	}

	if (start_withtimeout) {
		if (start_withtimeout <= 0
		 || start_withtimeout >  max_timeout) {
			pr_err("starting timeout out of range\n");
			err = -EINVAL;
			goto exit_miscdev;
		}

		err = watchdog_start();
		if (err) {
			pr_err("cannot start watchdog timer\n");
			goto exit_miscdev;
		}

		mutex_lock(&watchdog.lock);
		err = superio_enter(sioaddr);
		if (err)
			goto exit_unlock;
		superio_select(watchdog.sioaddr, SIO_F71808FG_LD_WDT);

		if (start_withtimeout > 0xff) {
			/* select minutes for timer units */
			superio_set_bit(sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_UNIT);
			superio_outb(sioaddr, F71808FG_REG_WD_TIME,
				DIV_ROUND_UP(start_withtimeout, 60));
		} else {
			/* select seconds for timer units */
			superio_clear_bit(sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_UNIT);
			superio_outb(sioaddr, F71808FG_REG_WD_TIME,
				start_withtimeout);
		}

		superio_exit(sioaddr);
		mutex_unlock(&watchdog.lock);

		if (nowayout)
			__module_get(THIS_MODULE);

		pr_info("watchdog started with initial timeout of %u sec\n",
			start_withtimeout);
	}

	return 0;

exit_unlock:
	mutex_unlock(&watchdog.lock);
exit_miscdev:
	misc_deregister(&watchdog_miscdev);
exit_reboot:
	unregister_reboot_notifier(&watchdog_notifier);

	return err;
}

static int __init f71808e_find(int sioaddr)
{
	u16 devid;
	int err = superio_enter(sioaddr);
	if (err)
		return err;

	devid = superio_inw(sioaddr, SIO_REG_MANID);
	if (devid != SIO_FINTEK_ID) {
		pr_debug("Not a Fintek device\n");
		err = -ENODEV;
		goto exit;
	}

	devid = force_id ? force_id : superio_inw(sioaddr, SIO_REG_DEVID);
	switch (devid) {
	case SIO_F71808_ID:
		watchdog.type = f71808fg;
		break;
	case SIO_F71862_ID:
		watchdog.type = f71862fg;
		err = f71862fg_pin_configure(0); /* validate module parameter */
		break;
	case SIO_F71868_ID:
		watchdog.type = f71868;
		break;
	case SIO_F71869_ID:
	case SIO_F71869A_ID:
		watchdog.type = f71869;
		break;
	case SIO_F71882_ID:
		watchdog.type = f71882fg;
		break;
	case SIO_F71889_ID:
		watchdog.type = f71889fg;
		break;
	case SIO_F71858_ID:
		/* Confirmed (by datasheet) not to have a watchdog. */
		err = -ENODEV;
		goto exit;
	case SIO_F81865_ID:
		watchdog.type = f81865;
		break;
	case SIO_F81866_ID:
		watchdog.type = f81866;
		break;
	default:
		pr_info("Unrecognized Fintek device: %04x\n",
			(unsigned int)devid);
		err = -ENODEV;
		goto exit;
	}

	pr_info("Found %s watchdog chip, revision %d\n",
		f71808e_names[watchdog.type],
		(int)superio_inb(sioaddr, SIO_REG_DEVREV));
exit:
	superio_exit(sioaddr);
	return err;
}

static int __init f71808e_init(void)
{
	static const unsigned short addrs[] = { 0x2e, 0x4e };
	int err = -ENODEV;
	int i;

	for (i = 0; i < ARRAY_SIZE(addrs); i++) {
		err = f71808e_find(addrs[i]);
		if (err == 0)
			break;
	}
	if (i == ARRAY_SIZE(addrs))
		return err;

	return watchdog_init(addrs[i]);
}

static void __exit f71808e_exit(void)
{
	if (watchdog_is_running()) {
		pr_warn("Watchdog timer still running, stopping it\n");
		watchdog_stop();
	}
	misc_deregister(&watchdog_miscdev);
	unregister_reboot_notifier(&watchdog_notifier);
}

MODULE_DESCRIPTION("F71808E Watchdog Driver");
MODULE_AUTHOR("Giel van Schijndel <me@mortis.eu>");
MODULE_LICENSE("GPL");

module_init(f71808e_init);
module_exit(f71808e_exit);
