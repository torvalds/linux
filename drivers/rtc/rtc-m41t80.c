/*
 * I2C client/driver for the ST M41T80 family of i2c rtc chips.
 *
 * Author: Alexander Bigga <ab@mycable.de>
 *
 * Based on m41t00.c by Mark A. Greer <mgreer@mvista.com>
 *
 * 2006 (c) mycable GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#ifdef CONFIG_RTC_DRV_M41T80_WDT
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>
#endif

#define M41T80_REG_SSEC	0
#define M41T80_REG_SEC	1
#define M41T80_REG_MIN	2
#define M41T80_REG_HOUR	3
#define M41T80_REG_WDAY	4
#define M41T80_REG_DAY	5
#define M41T80_REG_MON	6
#define M41T80_REG_YEAR	7
#define M41T80_REG_ALARM_MON	0xa
#define M41T80_REG_ALARM_DAY	0xb
#define M41T80_REG_ALARM_HOUR	0xc
#define M41T80_REG_ALARM_MIN	0xd
#define M41T80_REG_ALARM_SEC	0xe
#define M41T80_REG_FLAGS	0xf
#define M41T80_REG_SQW	0x13

#define M41T80_DATETIME_REG_SIZE	(M41T80_REG_YEAR + 1)
#define M41T80_ALARM_REG_SIZE	\
	(M41T80_REG_ALARM_SEC + 1 - M41T80_REG_ALARM_MON)

#define M41T80_SEC_ST		(1 << 7)	/* ST: Stop Bit */
#define M41T80_ALMON_AFE	(1 << 7)	/* AFE: AF Enable Bit */
#define M41T80_ALMON_SQWE	(1 << 6)	/* SQWE: SQW Enable Bit */
#define M41T80_ALHOUR_HT	(1 << 6)	/* HT: Halt Update Bit */
#define M41T80_FLAGS_AF		(1 << 6)	/* AF: Alarm Flag Bit */
#define M41T80_FLAGS_BATT_LOW	(1 << 4)	/* BL: Battery Low Bit */
#define M41T80_WATCHDOG_RB2	(1 << 7)	/* RB: Watchdog resolution */
#define M41T80_WATCHDOG_RB1	(1 << 1)	/* RB: Watchdog resolution */
#define M41T80_WATCHDOG_RB0	(1 << 0)	/* RB: Watchdog resolution */

#define M41T80_FEATURE_HT	(1 << 0)	/* Halt feature */
#define M41T80_FEATURE_BL	(1 << 1)	/* Battery low indicator */
#define M41T80_FEATURE_SQ	(1 << 2)	/* Squarewave feature */
#define M41T80_FEATURE_WD	(1 << 3)	/* Extra watchdog resolution */
#define M41T80_FEATURE_SQ_ALT	(1 << 4)	/* RSx bits are in reg 4 */

static DEFINE_MUTEX(m41t80_rtc_mutex);
static const struct i2c_device_id m41t80_id[] = {
	{ "m41t62", M41T80_FEATURE_SQ | M41T80_FEATURE_SQ_ALT },
	{ "m41t65", M41T80_FEATURE_HT | M41T80_FEATURE_WD },
	{ "m41t80", M41T80_FEATURE_SQ },
	{ "m41t81", M41T80_FEATURE_HT | M41T80_FEATURE_SQ},
	{ "m41t81s", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41t82", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41t83", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41st84", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41st85", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41st87", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "rv4162", M41T80_FEATURE_SQ | M41T80_FEATURE_WD | M41T80_FEATURE_SQ_ALT },
	{ }
};
MODULE_DEVICE_TABLE(i2c, m41t80_id);

struct m41t80_data {
	u8 features;
	struct rtc_device *rtc;
};

static int m41t80_get_datetime(struct i2c_client *client,
			       struct rtc_time *tm)
{
	u8 buf[M41T80_DATETIME_REG_SIZE], dt_addr[1] = { M41T80_REG_SEC };
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= dt_addr,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= M41T80_DATETIME_REG_SIZE - M41T80_REG_SEC,
			.buf	= buf + M41T80_REG_SEC,
		},
	};

	if (i2c_transfer(client->adapter, msgs, 2) < 0) {
		dev_err(&client->dev, "read error\n");
		return -EIO;
	}

	tm->tm_sec = bcd2bin(buf[M41T80_REG_SEC] & 0x7f);
	tm->tm_min = bcd2bin(buf[M41T80_REG_MIN] & 0x7f);
	tm->tm_hour = bcd2bin(buf[M41T80_REG_HOUR] & 0x3f);
	tm->tm_mday = bcd2bin(buf[M41T80_REG_DAY] & 0x3f);
	tm->tm_wday = buf[M41T80_REG_WDAY] & 0x07;
	tm->tm_mon = bcd2bin(buf[M41T80_REG_MON] & 0x1f) - 1;

	/* assume 20YY not 19YY, and ignore the Century Bit */
	tm->tm_year = bcd2bin(buf[M41T80_REG_YEAR]) + 100;
	return rtc_valid_tm(tm);
}

/* Sets the given date and time to the real time clock. */
static int m41t80_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	u8 wbuf[1 + M41T80_DATETIME_REG_SIZE];
	u8 *buf = &wbuf[1];
	u8 dt_addr[1] = { M41T80_REG_SEC };
	struct i2c_msg msgs_in[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= dt_addr,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= M41T80_DATETIME_REG_SIZE - M41T80_REG_SEC,
			.buf	= buf + M41T80_REG_SEC,
		},
	};
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1 + M41T80_DATETIME_REG_SIZE,
			.buf	= wbuf,
		 },
	};

	/* Read current reg values into buf[1..7] */
	if (i2c_transfer(client->adapter, msgs_in, 2) < 0) {
		dev_err(&client->dev, "read error\n");
		return -EIO;
	}

	wbuf[0] = 0; /* offset into rtc's regs */
	/* Merge time-data and register flags into buf[0..7] */
	buf[M41T80_REG_SSEC] = 0;
	buf[M41T80_REG_SEC] =
		bin2bcd(tm->tm_sec) | (buf[M41T80_REG_SEC] & ~0x7f);
	buf[M41T80_REG_MIN] =
		bin2bcd(tm->tm_min) | (buf[M41T80_REG_MIN] & ~0x7f);
	buf[M41T80_REG_HOUR] =
		bin2bcd(tm->tm_hour) | (buf[M41T80_REG_HOUR] & ~0x3f);
	buf[M41T80_REG_WDAY] =
		(tm->tm_wday & 0x07) | (buf[M41T80_REG_WDAY] & ~0x07);
	buf[M41T80_REG_DAY] =
		bin2bcd(tm->tm_mday) | (buf[M41T80_REG_DAY] & ~0x3f);
	buf[M41T80_REG_MON] =
		bin2bcd(tm->tm_mon + 1) | (buf[M41T80_REG_MON] & ~0x1f);

	/* assume 20YY not 19YY */
	if (tm->tm_year < 100 || tm->tm_year > 199) {
		dev_err(&client->dev, "Year must be between 2000 and 2099. It's %d.\n",
			tm->tm_year + 1900);
		return -EINVAL;
	}
	buf[M41T80_REG_YEAR] = bin2bcd(tm->tm_year % 100);

	if (i2c_transfer(client->adapter, msgs, 1) != 1) {
		dev_err(&client->dev, "write error\n");
		return -EIO;
	}
	return 0;
}

#if defined(CONFIG_RTC_INTF_PROC) || defined(CONFIG_RTC_INTF_PROC_MODULE)
static int m41t80_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct m41t80_data *clientdata = i2c_get_clientdata(client);
	u8 reg;

	if (clientdata->features & M41T80_FEATURE_BL) {
		reg = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
		seq_printf(seq, "battery\t\t: %s\n",
			   (reg & M41T80_FLAGS_BATT_LOW) ? "exhausted" : "ok");
	}
	return 0;
}
#else
#define m41t80_rtc_proc NULL
#endif

static int m41t80_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return m41t80_get_datetime(to_i2c_client(dev), tm);
}

static int m41t80_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return m41t80_set_datetime(to_i2c_client(dev), tm);
}

/*
 * XXX - m41t80 alarm functionality is reported broken.
 * until it is fixed, don't register alarm functions.
 */
static struct rtc_class_ops m41t80_rtc_ops = {
	.read_time = m41t80_rtc_read_time,
	.set_time = m41t80_rtc_set_time,
	.proc = m41t80_rtc_proc,
};

#if defined(CONFIG_RTC_INTF_SYSFS) || defined(CONFIG_RTC_INTF_SYSFS_MODULE)
static ssize_t m41t80_sysfs_show_flags(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int val;

	val = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
	if (val < 0)
		return val;
	return sprintf(buf, "%#x\n", val);
}
static DEVICE_ATTR(flags, S_IRUGO, m41t80_sysfs_show_flags, NULL);

static ssize_t m41t80_sysfs_show_sqwfreq(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct m41t80_data *clientdata = i2c_get_clientdata(client);
	int val, reg_sqw;

	if (!(clientdata->features & M41T80_FEATURE_SQ))
		return -EINVAL;

	reg_sqw = M41T80_REG_SQW;
	if (clientdata->features & M41T80_FEATURE_SQ_ALT)
		reg_sqw = M41T80_REG_WDAY;
	val = i2c_smbus_read_byte_data(client, reg_sqw);
	if (val < 0)
		return val;
	val = (val >> 4) & 0xf;
	switch (val) {
	case 0:
		break;
	case 1:
		val = 32768;
		break;
	default:
		val = 32768 >> val;
	}
	return sprintf(buf, "%d\n", val);
}
static ssize_t m41t80_sysfs_set_sqwfreq(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct m41t80_data *clientdata = i2c_get_clientdata(client);
	int almon, sqw, reg_sqw, rc;
	int val = simple_strtoul(buf, NULL, 0);

	if (!(clientdata->features & M41T80_FEATURE_SQ))
		return -EINVAL;

	if (val) {
		if (!is_power_of_2(val))
			return -EINVAL;
		val = ilog2(val);
		if (val == 15)
			val = 1;
		else if (val < 14)
			val = 15 - val;
		else
			return -EINVAL;
	}
	/* disable SQW, set SQW frequency & re-enable */
	almon = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_MON);
	if (almon < 0)
		return almon;
	reg_sqw = M41T80_REG_SQW;
	if (clientdata->features & M41T80_FEATURE_SQ_ALT)
		reg_sqw = M41T80_REG_WDAY;
	sqw = i2c_smbus_read_byte_data(client, reg_sqw);
	if (sqw < 0)
		return sqw;
	sqw = (sqw & 0x0f) | (val << 4);

	rc = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON,
				      almon & ~M41T80_ALMON_SQWE);
	if (rc < 0)
		return rc;

	if (val) {
		rc = i2c_smbus_write_byte_data(client, reg_sqw, sqw);
		if (rc < 0)
			return rc;

		rc = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON,
					     almon | M41T80_ALMON_SQWE);
		if (rc <0)
			return rc;
	}
	return count;
}
static DEVICE_ATTR(sqwfreq, S_IRUGO | S_IWUSR,
		   m41t80_sysfs_show_sqwfreq, m41t80_sysfs_set_sqwfreq);

static struct attribute *attrs[] = {
	&dev_attr_flags.attr,
	&dev_attr_sqwfreq.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int m41t80_sysfs_register(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &attr_group);
}
#else
static int m41t80_sysfs_register(struct device *dev)
{
	return 0;
}
#endif

#ifdef CONFIG_RTC_DRV_M41T80_WDT
/*
 *****************************************************************************
 *
 * Watchdog Driver
 *
 *****************************************************************************
 */
static struct i2c_client *save_client;

/* Default margin */
#define WD_TIMO 60		/* 1..31 seconds */

static int wdt_margin = WD_TIMO;
module_param(wdt_margin, int, 0);
MODULE_PARM_DESC(wdt_margin, "Watchdog timeout in seconds (default 60s)");

static unsigned long wdt_is_open;
static int boot_flag;

/**
 *	wdt_ping:
 *
 *	Reload counter one with the watchdog timeout. We don't bother reloading
 *	the cascade counter.
 */
static void wdt_ping(void)
{
	unsigned char i2c_data[2];
	struct i2c_msg msgs1[1] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= i2c_data,
		},
	};
	struct m41t80_data *clientdata = i2c_get_clientdata(save_client);

	i2c_data[0] = 0x09;		/* watchdog register */

	if (wdt_margin > 31)
		i2c_data[1] = (wdt_margin & 0xFC) | 0x83; /* resolution = 4s */
	else
		/*
		 * WDS = 1 (0x80), mulitplier = WD_TIMO, resolution = 1s (0x02)
		 */
		i2c_data[1] = wdt_margin<<2 | 0x82;

	/*
	 * M41T65 has three bits for watchdog resolution.  Don't set bit 7, as
	 * that would be an invalid resolution.
	 */
	if (clientdata->features & M41T80_FEATURE_WD)
		i2c_data[1] &= ~M41T80_WATCHDOG_RB2;

	i2c_transfer(save_client->adapter, msgs1, 1);
}

/**
 *	wdt_disable:
 *
 *	disables watchdog.
 */
static void wdt_disable(void)
{
	unsigned char i2c_data[2], i2c_buf[0x10];
	struct i2c_msg msgs0[2] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= i2c_data,
		},
		{
			.addr	= save_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= i2c_buf,
		},
	};
	struct i2c_msg msgs1[1] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= i2c_data,
		},
	};

	i2c_data[0] = 0x09;
	i2c_transfer(save_client->adapter, msgs0, 2);

	i2c_data[0] = 0x09;
	i2c_data[1] = 0x00;
	i2c_transfer(save_client->adapter, msgs1, 1);
}

/**
 *	wdt_write:
 *	@file: file handle to the watchdog
 *	@buf: buffer to write (unused as data does not matter here
 *	@count: count of bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we we don't define content meaning.
 */
static ssize_t wdt_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	if (count) {
		wdt_ping();
		return 1;
	}
	return 0;
}

static ssize_t wdt_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	return 0;
}

/**
 *	wdt_ioctl:
 *	@inode: inode of the device
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features. We only actually usefully support
 *	querying capabilities and current status.
 */
static int wdt_ioctl(struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	int new_margin, rv;
	static struct watchdog_info ident = {
		.options = WDIOF_POWERUNDER | WDIOF_KEEPALIVEPING |
			WDIOF_SETTIMEOUT,
		.firmware_version = 1,
		.identity = "M41T80 WTD"
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info __user *)arg, &ident,
				    sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(boot_flag, (int __user *)arg);
	case WDIOC_KEEPALIVE:
		wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *)arg))
			return -EFAULT;
		/* Arbitrary, can't find the card's limits */
		if (new_margin < 1 || new_margin > 124)
			return -EINVAL;
		wdt_margin = new_margin;
		wdt_ping();
		/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(wdt_margin, (int __user *)arg);

	case WDIOC_SETOPTIONS:
		if (copy_from_user(&rv, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		if (rv & WDIOS_DISABLECARD) {
			pr_info("disable watchdog\n");
			wdt_disable();
		}

		if (rv & WDIOS_ENABLECARD) {
			pr_info("enable watchdog\n");
			wdt_ping();
		}

		return -EINVAL;
	}
	return -ENOTTY;
}

static long wdt_unlocked_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	int ret;

	mutex_lock(&m41t80_rtc_mutex);
	ret = wdt_ioctl(file, cmd, arg);
	mutex_unlock(&m41t80_rtc_mutex);

	return ret;
}

/**
 *	wdt_open:
 *	@inode: inode of device
 *	@file: file handle to device
 *
 */
static int wdt_open(struct inode *inode, struct file *file)
{
	if (MINOR(inode->i_rdev) == WATCHDOG_MINOR) {
		mutex_lock(&m41t80_rtc_mutex);
		if (test_and_set_bit(0, &wdt_is_open)) {
			mutex_unlock(&m41t80_rtc_mutex);
			return -EBUSY;
		}
		/*
		 *	Activate
		 */
		wdt_is_open = 1;
		mutex_unlock(&m41t80_rtc_mutex);
		return nonseekable_open(inode, file);
	}
	return -ENODEV;
}

/**
 *	wdt_close:
 *	@inode: inode to board
 *	@file: file handle to board
 *
 */
static int wdt_release(struct inode *inode, struct file *file)
{
	if (MINOR(inode->i_rdev) == WATCHDOG_MINOR)
		clear_bit(0, &wdt_is_open);
	return 0;
}

/**
 *	notify_sys:
 *	@this: our notifier block
 *	@code: the event being reported
 *	@unused: unused
 *
 *	Our notifier is called on system shutdowns. We want to turn the card
 *	off at reboot otherwise the machine will reboot again during memory
 *	test or worse yet during the following fsck. This would suck, in fact
 *	trust me - if it happens it does suck.
 */
static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
			  void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Disable Watchdog */
		wdt_disable();
	return NOTIFY_DONE;
}

static const struct file_operations wdt_fops = {
	.owner	= THIS_MODULE,
	.read	= wdt_read,
	.unlocked_ioctl = wdt_unlocked_ioctl,
	.write	= wdt_write,
	.open	= wdt_open,
	.release = wdt_release,
	.llseek = no_llseek,
};

static struct miscdevice wdt_dev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */
static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};
#endif /* CONFIG_RTC_DRV_M41T80_WDT */

/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */
static int m41t80_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int rc = 0;
	struct rtc_device *rtc = NULL;
	struct rtc_time tm;
	struct m41t80_data *clientdata = NULL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C
				     | I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	clientdata = devm_kzalloc(&client->dev, sizeof(*clientdata),
				GFP_KERNEL);
	if (!clientdata)
		return -ENOMEM;

	clientdata->features = id->driver_data;
	i2c_set_clientdata(client, clientdata);

	rtc = devm_rtc_device_register(&client->dev, client->name,
					&m41t80_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	clientdata->rtc = rtc;

	/* Make sure HT (Halt Update) bit is cleared */
	rc = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_HOUR);

	if (rc >= 0 && rc & M41T80_ALHOUR_HT) {
		if (clientdata->features & M41T80_FEATURE_HT) {
			m41t80_get_datetime(client, &tm);
			dev_info(&client->dev, "HT bit was set!\n");
			dev_info(&client->dev,
				 "Power Down at "
				 "%04i-%02i-%02i %02i:%02i:%02i\n",
				 tm.tm_year + 1900,
				 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
				 tm.tm_min, tm.tm_sec);
		}
		rc = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_HOUR,
					      rc & ~M41T80_ALHOUR_HT);
	}

	if (rc < 0) {
		dev_err(&client->dev, "Can't clear HT bit\n");
		return rc;
	}

	/* Make sure ST (stop) bit is cleared */
	rc = i2c_smbus_read_byte_data(client, M41T80_REG_SEC);

	if (rc >= 0 && rc & M41T80_SEC_ST)
		rc = i2c_smbus_write_byte_data(client, M41T80_REG_SEC,
					      rc & ~M41T80_SEC_ST);
	if (rc < 0) {
		dev_err(&client->dev, "Can't clear ST bit\n");
		return rc;
	}

	rc = m41t80_sysfs_register(&client->dev);
	if (rc)
		return rc;

#ifdef CONFIG_RTC_DRV_M41T80_WDT
	if (clientdata->features & M41T80_FEATURE_HT) {
		save_client = client;
		rc = misc_register(&wdt_dev);
		if (rc)
			return rc;
		rc = register_reboot_notifier(&wdt_notifier);
		if (rc) {
			misc_deregister(&wdt_dev);
			return rc;
		}
	}
#endif
	return 0;
}

static int m41t80_remove(struct i2c_client *client)
{
#ifdef CONFIG_RTC_DRV_M41T80_WDT
	struct m41t80_data *clientdata = i2c_get_clientdata(client);

	if (clientdata->features & M41T80_FEATURE_HT) {
		misc_deregister(&wdt_dev);
		unregister_reboot_notifier(&wdt_notifier);
	}
#endif

	return 0;
}

static struct i2c_driver m41t80_driver = {
	.driver = {
		.name = "rtc-m41t80",
	},
	.probe = m41t80_probe,
	.remove = m41t80_remove,
	.id_table = m41t80_id,
};

module_i2c_driver(m41t80_driver);

MODULE_AUTHOR("Alexander Bigga <ab@mycable.de>");
MODULE_DESCRIPTION("ST Microelectronics M41T80 series RTC I2C Client Driver");
MODULE_LICENSE("GPL");
