// SPDX-License-Identifier: GPL-2.0-only
/*
 * An I2C driver for Ricoh RS5C372, R2025S/D and RV5C38[67] RTCs
 *
 * Copyright (C) 2005 Pavel Mironchik <pmironchik@optifacio.net>
 * Copyright (C) 2006 Tower Technologies
 * Copyright (C) 2008 Paul Mundt
 */

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_device.h>

/*
 * Ricoh has a family of I2C based RTCs, which differ only slightly from
 * each other.  Differences center on pinout (e.g. how many interrupts,
 * output clock, etc) and how the control registers are used.  The '372
 * is significant only because that's the one this driver first supported.
 */
#define RS5C372_REG_SECS	0
#define RS5C372_REG_MINS	1
#define RS5C372_REG_HOURS	2
#define RS5C372_REG_WDAY	3
#define RS5C372_REG_DAY		4
#define RS5C372_REG_MONTH	5
#define RS5C372_REG_YEAR	6
#define RS5C372_REG_TRIM	7
#	define RS5C372_TRIM_XSL		0x80
#	define RS5C372_TRIM_MASK	0x7F

#define RS5C_REG_ALARM_A_MIN	8			/* or ALARM_W */
#define RS5C_REG_ALARM_A_HOURS	9
#define RS5C_REG_ALARM_A_WDAY	10

#define RS5C_REG_ALARM_B_MIN	11			/* or ALARM_D */
#define RS5C_REG_ALARM_B_HOURS	12
#define RS5C_REG_ALARM_B_WDAY	13			/* (ALARM_B only) */

#define RS5C_REG_CTRL1		14
#	define RS5C_CTRL1_AALE		(1 << 7)	/* or WALE */
#	define RS5C_CTRL1_BALE		(1 << 6)	/* or DALE */
#	define RV5C387_CTRL1_24		(1 << 5)
#	define RS5C372A_CTRL1_SL1	(1 << 5)
#	define RS5C_CTRL1_CT_MASK	(7 << 0)
#	define RS5C_CTRL1_CT0		(0 << 0)	/* no periodic irq */
#	define RS5C_CTRL1_CT4		(4 << 0)	/* 1 Hz level irq */
#define RS5C_REG_CTRL2		15
#	define RS5C372_CTRL2_24		(1 << 5)
#	define RS5C_CTRL2_XSTP		(1 << 4)	/* only if !R2x2x */
#	define R2x2x_CTRL2_VDET		(1 << 6)	/* only if  R2x2x */
#	define R2x2x_CTRL2_XSTP		(1 << 5)	/* only if  R2x2x */
#	define R2x2x_CTRL2_PON		(1 << 4)	/* only if  R2x2x */
#	define RS5C_CTRL2_CTFG		(1 << 2)
#	define RS5C_CTRL2_AAFG		(1 << 1)	/* or WAFG */
#	define RS5C_CTRL2_BAFG		(1 << 0)	/* or DAFG */


/* to read (style 1) or write registers starting at R */
#define RS5C_ADDR(R)		(((R) << 4) | 0)


enum rtc_type {
	rtc_undef = 0,
	rtc_r2025sd,
	rtc_r2221tl,
	rtc_rs5c372a,
	rtc_rs5c372b,
	rtc_rv5c386,
	rtc_rv5c387a,
};

static const struct i2c_device_id rs5c372_id[] = {
	{ "r2025sd", rtc_r2025sd },
	{ "r2221tl", rtc_r2221tl },
	{ "rs5c372a", rtc_rs5c372a },
	{ "rs5c372b", rtc_rs5c372b },
	{ "rv5c386", rtc_rv5c386 },
	{ "rv5c387a", rtc_rv5c387a },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rs5c372_id);

static const struct of_device_id rs5c372_of_match[] = {
	{
		.compatible = "ricoh,r2025sd",
		.data = (void *)rtc_r2025sd
	},
	{
		.compatible = "ricoh,r2221tl",
		.data = (void *)rtc_r2221tl
	},
	{
		.compatible = "ricoh,rs5c372a",
		.data = (void *)rtc_rs5c372a
	},
	{
		.compatible = "ricoh,rs5c372b",
		.data = (void *)rtc_rs5c372b
	},
	{
		.compatible = "ricoh,rv5c386",
		.data = (void *)rtc_rv5c386
	},
	{
		.compatible = "ricoh,rv5c387a",
		.data = (void *)rtc_rv5c387a
	},
	{ }
};
MODULE_DEVICE_TABLE(of, rs5c372_of_match);

/* REVISIT:  this assumes that:
 *  - we're in the 21st century, so it's safe to ignore the century
 *    bit for rv5c38[67] (REG_MONTH bit 7);
 *  - we should use ALARM_A not ALARM_B (may be wrong on some boards)
 */
struct rs5c372 {
	struct i2c_client	*client;
	struct rtc_device	*rtc;
	enum rtc_type		type;
	unsigned		time24:1;
	unsigned		has_irq:1;
	unsigned		smbus:1;
	char			buf[17];
	char			*regs;
};

static int rs5c_get_regs(struct rs5c372 *rs5c)
{
	struct i2c_client	*client = rs5c->client;
	struct i2c_msg		msgs[] = {
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(rs5c->buf),
			.buf = rs5c->buf
		},
	};

	/* This implements the third reading method from the datasheet, using
	 * an internal address that's reset after each transaction (by STOP)
	 * to 0x0f ... so we read extra registers, and skip the first one.
	 *
	 * The first method doesn't work with the iop3xx adapter driver, on at
	 * least 80219 chips; this works around that bug.
	 *
	 * The third method on the other hand doesn't work for the SMBus-only
	 * configurations, so we use the the first method there, stripping off
	 * the extra register in the process.
	 */
	if (rs5c->smbus) {
		int addr = RS5C_ADDR(RS5C372_REG_SECS);
		int size = sizeof(rs5c->buf) - 1;

		if (i2c_smbus_read_i2c_block_data(client, addr, size,
						  rs5c->buf + 1) != size) {
			dev_warn(&client->dev, "can't read registers\n");
			return -EIO;
		}
	} else {
		if ((i2c_transfer(client->adapter, msgs, 1)) != 1) {
			dev_warn(&client->dev, "can't read registers\n");
			return -EIO;
		}
	}

	dev_dbg(&client->dev,
		"%3ph (%02x) %3ph (%02x), %3ph, %3ph; %02x %02x\n",
		rs5c->regs + 0, rs5c->regs[3],
		rs5c->regs + 4, rs5c->regs[7],
		rs5c->regs + 8, rs5c->regs + 11,
		rs5c->regs[14], rs5c->regs[15]);

	return 0;
}

static unsigned rs5c_reg2hr(struct rs5c372 *rs5c, unsigned reg)
{
	unsigned	hour;

	if (rs5c->time24)
		return bcd2bin(reg & 0x3f);

	hour = bcd2bin(reg & 0x1f);
	if (hour == 12)
		hour = 0;
	if (reg & 0x20)
		hour += 12;
	return hour;
}

static unsigned rs5c_hr2reg(struct rs5c372 *rs5c, unsigned hour)
{
	if (rs5c->time24)
		return bin2bcd(hour);

	if (hour > 12)
		return 0x20 | bin2bcd(hour - 12);
	if (hour == 12)
		return 0x20 | bin2bcd(12);
	if (hour == 0)
		return bin2bcd(12);
	return bin2bcd(hour);
}

static int rs5c372_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rs5c372	*rs5c = i2c_get_clientdata(client);
	int		status = rs5c_get_regs(rs5c);
	unsigned char ctrl2 = rs5c->regs[RS5C_REG_CTRL2];

	if (status < 0)
		return status;

	switch (rs5c->type) {
	case rtc_r2025sd:
	case rtc_r2221tl:
		if ((rs5c->type == rtc_r2025sd && !(ctrl2 & R2x2x_CTRL2_XSTP)) ||
		    (rs5c->type == rtc_r2221tl &&  (ctrl2 & R2x2x_CTRL2_XSTP))) {
			dev_warn(&client->dev, "rtc oscillator interruption detected. Please reset the rtc clock.\n");
			return -EINVAL;
		}
		break;
	default:
		if (ctrl2 & RS5C_CTRL2_XSTP) {
			dev_warn(&client->dev, "rtc oscillator interruption detected. Please reset the rtc clock.\n");
			return -EINVAL;
		}
	}

	tm->tm_sec = bcd2bin(rs5c->regs[RS5C372_REG_SECS] & 0x7f);
	tm->tm_min = bcd2bin(rs5c->regs[RS5C372_REG_MINS] & 0x7f);
	tm->tm_hour = rs5c_reg2hr(rs5c, rs5c->regs[RS5C372_REG_HOURS]);

	tm->tm_wday = bcd2bin(rs5c->regs[RS5C372_REG_WDAY] & 0x07);
	tm->tm_mday = bcd2bin(rs5c->regs[RS5C372_REG_DAY] & 0x3f);

	/* tm->tm_mon is zero-based */
	tm->tm_mon = bcd2bin(rs5c->regs[RS5C372_REG_MONTH] & 0x1f) - 1;

	/* year is 1900 + tm->tm_year */
	tm->tm_year = bcd2bin(rs5c->regs[RS5C372_REG_YEAR]) + 100;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int rs5c372_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rs5c372	*rs5c = i2c_get_clientdata(client);
	unsigned char	buf[7];
	unsigned char	ctrl2;
	int		addr;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	addr   = RS5C_ADDR(RS5C372_REG_SECS);
	buf[0] = bin2bcd(tm->tm_sec);
	buf[1] = bin2bcd(tm->tm_min);
	buf[2] = rs5c_hr2reg(rs5c, tm->tm_hour);
	buf[3] = bin2bcd(tm->tm_wday);
	buf[4] = bin2bcd(tm->tm_mday);
	buf[5] = bin2bcd(tm->tm_mon + 1);
	buf[6] = bin2bcd(tm->tm_year - 100);

	if (i2c_smbus_write_i2c_block_data(client, addr, sizeof(buf), buf) < 0) {
		dev_dbg(&client->dev, "%s: write error in line %i\n",
			__func__, __LINE__);
		return -EIO;
	}

	addr = RS5C_ADDR(RS5C_REG_CTRL2);
	ctrl2 = i2c_smbus_read_byte_data(client, addr);

	/* clear rtc warning bits */
	switch (rs5c->type) {
	case rtc_r2025sd:
	case rtc_r2221tl:
		ctrl2 &= ~(R2x2x_CTRL2_VDET | R2x2x_CTRL2_PON);
		if (rs5c->type == rtc_r2025sd)
			ctrl2 |= R2x2x_CTRL2_XSTP;
		else
			ctrl2 &= ~R2x2x_CTRL2_XSTP;
		break;
	default:
		ctrl2 &= ~RS5C_CTRL2_XSTP;
		break;
	}

	if (i2c_smbus_write_byte_data(client, addr, ctrl2) < 0) {
		dev_dbg(&client->dev, "%s: write error in line %i\n",
			__func__, __LINE__);
		return -EIO;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_RTC_INTF_PROC)
#define	NEED_TRIM
#endif

#if IS_ENABLED(CONFIG_RTC_INTF_SYSFS)
#define	NEED_TRIM
#endif

#ifdef	NEED_TRIM
static int rs5c372_get_trim(struct i2c_client *client, int *osc, int *trim)
{
	struct rs5c372 *rs5c372 = i2c_get_clientdata(client);
	u8 tmp = rs5c372->regs[RS5C372_REG_TRIM];

	if (osc)
		*osc = (tmp & RS5C372_TRIM_XSL) ? 32000 : 32768;

	if (trim) {
		dev_dbg(&client->dev, "%s: raw trim=%x\n", __func__, tmp);
		tmp &= RS5C372_TRIM_MASK;
		if (tmp & 0x3e) {
			int t = tmp & 0x3f;

			if (tmp & 0x40)
				t = (~t | (s8)0xc0) + 1;
			else
				t = t - 1;

			tmp = t * 2;
		} else
			tmp = 0;
		*trim = tmp;
	}

	return 0;
}
#endif

static int rs5c_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct i2c_client	*client = to_i2c_client(dev);
	struct rs5c372		*rs5c = i2c_get_clientdata(client);
	unsigned char		buf;
	int			status, addr;

	buf = rs5c->regs[RS5C_REG_CTRL1];

	if (!rs5c->has_irq)
		return -EINVAL;

	status = rs5c_get_regs(rs5c);
	if (status < 0)
		return status;

	addr = RS5C_ADDR(RS5C_REG_CTRL1);
	if (enabled)
		buf |= RS5C_CTRL1_AALE;
	else
		buf &= ~RS5C_CTRL1_AALE;

	if (i2c_smbus_write_byte_data(client, addr, buf) < 0) {
		dev_warn(dev, "can't update alarm\n");
		status = -EIO;
	} else
		rs5c->regs[RS5C_REG_CTRL1] = buf;

	return status;
}


/* NOTE:  Since RTC_WKALM_{RD,SET} were originally defined for EFI,
 * which only exposes a polled programming interface; and since
 * these calls map directly to those EFI requests; we don't demand
 * we have an IRQ for this chip when we go through this API.
 *
 * The older x86_pc derived RTC_ALM_{READ,SET} calls require irqs
 * though, managed through RTC_AIE_{ON,OFF} requests.
 */

static int rs5c_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct i2c_client	*client = to_i2c_client(dev);
	struct rs5c372		*rs5c = i2c_get_clientdata(client);
	int			status;

	status = rs5c_get_regs(rs5c);
	if (status < 0)
		return status;

	/* report alarm time */
	t->time.tm_sec = 0;
	t->time.tm_min = bcd2bin(rs5c->regs[RS5C_REG_ALARM_A_MIN] & 0x7f);
	t->time.tm_hour = rs5c_reg2hr(rs5c, rs5c->regs[RS5C_REG_ALARM_A_HOURS]);

	/* ... and status */
	t->enabled = !!(rs5c->regs[RS5C_REG_CTRL1] & RS5C_CTRL1_AALE);
	t->pending = !!(rs5c->regs[RS5C_REG_CTRL2] & RS5C_CTRL2_AAFG);

	return 0;
}

static int rs5c_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct i2c_client	*client = to_i2c_client(dev);
	struct rs5c372		*rs5c = i2c_get_clientdata(client);
	int			status, addr, i;
	unsigned char		buf[3];

	/* only handle up to 24 hours in the future, like RTC_ALM_SET */
	if (t->time.tm_mday != -1
			|| t->time.tm_mon != -1
			|| t->time.tm_year != -1)
		return -EINVAL;

	/* REVISIT: round up tm_sec */

	/* if needed, disable irq (clears pending status) */
	status = rs5c_get_regs(rs5c);
	if (status < 0)
		return status;
	if (rs5c->regs[RS5C_REG_CTRL1] & RS5C_CTRL1_AALE) {
		addr = RS5C_ADDR(RS5C_REG_CTRL1);
		buf[0] = rs5c->regs[RS5C_REG_CTRL1] & ~RS5C_CTRL1_AALE;
		if (i2c_smbus_write_byte_data(client, addr, buf[0]) < 0) {
			dev_dbg(dev, "can't disable alarm\n");
			return -EIO;
		}
		rs5c->regs[RS5C_REG_CTRL1] = buf[0];
	}

	/* set alarm */
	buf[0] = bin2bcd(t->time.tm_min);
	buf[1] = rs5c_hr2reg(rs5c, t->time.tm_hour);
	buf[2] = 0x7f;	/* any/all days */

	for (i = 0; i < sizeof(buf); i++) {
		addr = RS5C_ADDR(RS5C_REG_ALARM_A_MIN + i);
		if (i2c_smbus_write_byte_data(client, addr, buf[i]) < 0) {
			dev_dbg(dev, "can't set alarm time\n");
			return -EIO;
		}
	}

	/* ... and maybe enable its irq */
	if (t->enabled) {
		addr = RS5C_ADDR(RS5C_REG_CTRL1);
		buf[0] = rs5c->regs[RS5C_REG_CTRL1] | RS5C_CTRL1_AALE;
		if (i2c_smbus_write_byte_data(client, addr, buf[0]) < 0)
			dev_warn(dev, "can't enable alarm\n");
		rs5c->regs[RS5C_REG_CTRL1] = buf[0];
	}

	return 0;
}

#if IS_ENABLED(CONFIG_RTC_INTF_PROC)

static int rs5c372_rtc_proc(struct device *dev, struct seq_file *seq)
{
	int err, osc, trim;

	err = rs5c372_get_trim(to_i2c_client(dev), &osc, &trim);
	if (err == 0) {
		seq_printf(seq, "crystal\t\t: %d.%03d KHz\n",
				osc / 1000, osc % 1000);
		seq_printf(seq, "trim\t\t: %d\n", trim);
	}

	return 0;
}

#else
#define	rs5c372_rtc_proc	NULL
#endif

static const struct rtc_class_ops rs5c372_rtc_ops = {
	.proc		= rs5c372_rtc_proc,
	.read_time	= rs5c372_rtc_read_time,
	.set_time	= rs5c372_rtc_set_time,
	.read_alarm	= rs5c_read_alarm,
	.set_alarm	= rs5c_set_alarm,
	.alarm_irq_enable = rs5c_rtc_alarm_irq_enable,
};

#if IS_ENABLED(CONFIG_RTC_INTF_SYSFS)

static ssize_t rs5c372_sysfs_show_trim(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int err, trim;

	err = rs5c372_get_trim(to_i2c_client(dev), NULL, &trim);
	if (err)
		return err;

	return sprintf(buf, "%d\n", trim);
}
static DEVICE_ATTR(trim, S_IRUGO, rs5c372_sysfs_show_trim, NULL);

static ssize_t rs5c372_sysfs_show_osc(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int err, osc;

	err = rs5c372_get_trim(to_i2c_client(dev), &osc, NULL);
	if (err)
		return err;

	return sprintf(buf, "%d.%03d KHz\n", osc / 1000, osc % 1000);
}
static DEVICE_ATTR(osc, S_IRUGO, rs5c372_sysfs_show_osc, NULL);

static int rs5c_sysfs_register(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_trim);
	if (err)
		return err;
	err = device_create_file(dev, &dev_attr_osc);
	if (err)
		device_remove_file(dev, &dev_attr_trim);

	return err;
}

static void rs5c_sysfs_unregister(struct device *dev)
{
	device_remove_file(dev, &dev_attr_trim);
	device_remove_file(dev, &dev_attr_osc);
}

#else
static int rs5c_sysfs_register(struct device *dev)
{
	return 0;
}

static void rs5c_sysfs_unregister(struct device *dev)
{
	/* nothing */
}
#endif	/* SYSFS */

static struct i2c_driver rs5c372_driver;

static int rs5c_oscillator_setup(struct rs5c372 *rs5c372)
{
	unsigned char buf[2];
	int addr, i, ret = 0;

	addr   = RS5C_ADDR(RS5C_REG_CTRL1);
	buf[0] = rs5c372->regs[RS5C_REG_CTRL1];
	buf[1] = rs5c372->regs[RS5C_REG_CTRL2];

	switch (rs5c372->type) {
	case rtc_r2025sd:
		if (buf[1] & R2x2x_CTRL2_XSTP)
			return ret;
		break;
	case rtc_r2221tl:
		if (!(buf[1] & R2x2x_CTRL2_XSTP))
			return ret;
		break;
	default:
		if (!(buf[1] & RS5C_CTRL2_XSTP))
			return ret;
		break;
	}

	/* use 24hr mode */
	switch (rs5c372->type) {
	case rtc_rs5c372a:
	case rtc_rs5c372b:
		buf[1] |= RS5C372_CTRL2_24;
		rs5c372->time24 = 1;
		break;
	case rtc_r2025sd:
	case rtc_r2221tl:
	case rtc_rv5c386:
	case rtc_rv5c387a:
		buf[0] |= RV5C387_CTRL1_24;
		rs5c372->time24 = 1;
		break;
	default:
		/* impossible */
		break;
	}

	for (i = 0; i < sizeof(buf); i++) {
		addr = RS5C_ADDR(RS5C_REG_CTRL1 + i);
		ret = i2c_smbus_write_byte_data(rs5c372->client, addr, buf[i]);
		if (unlikely(ret < 0))
			return ret;
	}

	rs5c372->regs[RS5C_REG_CTRL1] = buf[0];
	rs5c372->regs[RS5C_REG_CTRL2] = buf[1];

	return 0;
}

static int rs5c372_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err = 0;
	int smbus_mode = 0;
	struct rs5c372 *rs5c372;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_I2C_BLOCK)) {
		/*
		 * If we don't have any master mode adapter, try breaking
		 * it down in to the barest of capabilities.
		 */
		if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA |
				I2C_FUNC_SMBUS_I2C_BLOCK))
			smbus_mode = 1;
		else {
			/* Still no good, give up */
			err = -ENODEV;
			goto exit;
		}
	}

	rs5c372 = devm_kzalloc(&client->dev, sizeof(struct rs5c372),
				GFP_KERNEL);
	if (!rs5c372) {
		err = -ENOMEM;
		goto exit;
	}

	rs5c372->client = client;
	i2c_set_clientdata(client, rs5c372);
	if (client->dev.of_node)
		rs5c372->type = (enum rtc_type)
			of_device_get_match_data(&client->dev);
	else
		rs5c372->type = id->driver_data;

	/* we read registers 0x0f then 0x00-0x0f; skip the first one */
	rs5c372->regs = &rs5c372->buf[1];
	rs5c372->smbus = smbus_mode;

	err = rs5c_get_regs(rs5c372);
	if (err < 0)
		goto exit;

	/* clock may be set for am/pm or 24 hr time */
	switch (rs5c372->type) {
	case rtc_rs5c372a:
	case rtc_rs5c372b:
		/* alarm uses ALARM_A; and nINTRA on 372a, nINTR on 372b.
		 * so does periodic irq, except some 327a modes.
		 */
		if (rs5c372->regs[RS5C_REG_CTRL2] & RS5C372_CTRL2_24)
			rs5c372->time24 = 1;
		break;
	case rtc_r2025sd:
	case rtc_r2221tl:
	case rtc_rv5c386:
	case rtc_rv5c387a:
		if (rs5c372->regs[RS5C_REG_CTRL1] & RV5C387_CTRL1_24)
			rs5c372->time24 = 1;
		/* alarm uses ALARM_W; and nINTRB for alarm and periodic
		 * irq, on both 386 and 387
		 */
		break;
	default:
		dev_err(&client->dev, "unknown RTC type\n");
		goto exit;
	}

	/* if the oscillator lost power and no other software (like
	 * the bootloader) set it up, do it here.
	 *
	 * The R2025S/D does this a little differently than the other
	 * parts, so we special case that..
	 */
	err = rs5c_oscillator_setup(rs5c372);
	if (unlikely(err < 0)) {
		dev_err(&client->dev, "setup error\n");
		goto exit;
	}

	dev_info(&client->dev, "%s found, %s\n",
			({ char *s; switch (rs5c372->type) {
			case rtc_r2025sd:	s = "r2025sd"; break;
			case rtc_r2221tl:	s = "r2221tl"; break;
			case rtc_rs5c372a:	s = "rs5c372a"; break;
			case rtc_rs5c372b:	s = "rs5c372b"; break;
			case rtc_rv5c386:	s = "rv5c386"; break;
			case rtc_rv5c387a:	s = "rv5c387a"; break;
			default:		s = "chip"; break;
			}; s;}),
			rs5c372->time24 ? "24hr" : "am/pm"
			);

	/* REVISIT use client->irq to register alarm irq ... */
	rs5c372->rtc = devm_rtc_device_register(&client->dev,
					rs5c372_driver.driver.name,
					&rs5c372_rtc_ops, THIS_MODULE);

	if (IS_ERR(rs5c372->rtc)) {
		err = PTR_ERR(rs5c372->rtc);
		goto exit;
	}

	err = rs5c_sysfs_register(&client->dev);
	if (err)
		goto exit;

	return 0;

exit:
	return err;
}

static int rs5c372_remove(struct i2c_client *client)
{
	rs5c_sysfs_unregister(&client->dev);
	return 0;
}

static struct i2c_driver rs5c372_driver = {
	.driver		= {
		.name	= "rtc-rs5c372",
		.of_match_table = of_match_ptr(rs5c372_of_match),
	},
	.probe		= rs5c372_probe,
	.remove		= rs5c372_remove,
	.id_table	= rs5c372_id,
};

module_i2c_driver(rs5c372_driver);

MODULE_AUTHOR(
		"Pavel Mironchik <pmironchik@optifacio.net>, "
		"Alessandro Zummo <a.zummo@towertech.it>, "
		"Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("Ricoh RS5C372 RTC driver");
MODULE_LICENSE("GPL");
