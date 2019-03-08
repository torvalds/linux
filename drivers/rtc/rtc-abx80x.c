/*
 * A driver for the I2C members of the Abracon AB x8xx RTC family,
 * and compatible: AB 1805 and AB 0805
 *
 * Copyright 2014-2015 Macq S.A.
 *
 * Author: Philippe De Muyter <phdm@macqel.be>
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/watchdog.h>

#define ABX8XX_REG_HTH		0x00
#define ABX8XX_REG_SC		0x01
#define ABX8XX_REG_MN		0x02
#define ABX8XX_REG_HR		0x03
#define ABX8XX_REG_DA		0x04
#define ABX8XX_REG_MO		0x05
#define ABX8XX_REG_YR		0x06
#define ABX8XX_REG_WD		0x07

#define ABX8XX_REG_AHTH		0x08
#define ABX8XX_REG_ASC		0x09
#define ABX8XX_REG_AMN		0x0a
#define ABX8XX_REG_AHR		0x0b
#define ABX8XX_REG_ADA		0x0c
#define ABX8XX_REG_AMO		0x0d
#define ABX8XX_REG_AWD		0x0e

#define ABX8XX_REG_STATUS	0x0f
#define ABX8XX_STATUS_AF	BIT(2)
#define ABX8XX_STATUS_BLF	BIT(4)
#define ABX8XX_STATUS_WDT	BIT(6)

#define ABX8XX_REG_CTRL1	0x10
#define ABX8XX_CTRL_WRITE	BIT(0)
#define ABX8XX_CTRL_ARST	BIT(2)
#define ABX8XX_CTRL_12_24	BIT(6)

#define ABX8XX_REG_CTRL2	0x11
#define ABX8XX_CTRL2_RSVD	BIT(5)

#define ABX8XX_REG_IRQ		0x12
#define ABX8XX_IRQ_AIE		BIT(2)
#define ABX8XX_IRQ_IM_1_4	(0x3 << 5)

#define ABX8XX_REG_CD_TIMER_CTL	0x18

#define ABX8XX_REG_OSC		0x1c
#define ABX8XX_OSC_FOS		BIT(3)
#define ABX8XX_OSC_BOS		BIT(4)
#define ABX8XX_OSC_ACAL_512	BIT(5)
#define ABX8XX_OSC_ACAL_1024	BIT(6)

#define ABX8XX_OSC_OSEL		BIT(7)

#define ABX8XX_REG_OSS		0x1d
#define ABX8XX_OSS_OF		BIT(1)
#define ABX8XX_OSS_OMODE	BIT(4)

#define ABX8XX_REG_WDT		0x1b
#define ABX8XX_WDT_WDS		BIT(7)
#define ABX8XX_WDT_BMB_MASK	0x7c
#define ABX8XX_WDT_BMB_SHIFT	2
#define ABX8XX_WDT_MAX_TIME	(ABX8XX_WDT_BMB_MASK >> ABX8XX_WDT_BMB_SHIFT)
#define ABX8XX_WDT_WRB_MASK	0x03
#define ABX8XX_WDT_WRB_1HZ	0x02

#define ABX8XX_REG_CFG_KEY	0x1f
#define ABX8XX_CFG_KEY_OSC	0xa1
#define ABX8XX_CFG_KEY_MISC	0x9d

#define ABX8XX_REG_ID0		0x28

#define ABX8XX_REG_OUT_CTRL	0x30
#define ABX8XX_OUT_CTRL_EXDS	BIT(4)

#define ABX8XX_REG_TRICKLE	0x20
#define ABX8XX_TRICKLE_CHARGE_ENABLE	0xa0
#define ABX8XX_TRICKLE_STANDARD_DIODE	0x8
#define ABX8XX_TRICKLE_SCHOTTKY_DIODE	0x4

static u8 trickle_resistors[] = {0, 3, 6, 11};

enum abx80x_chip {AB0801, AB0803, AB0804, AB0805,
	AB1801, AB1803, AB1804, AB1805, RV1805, ABX80X};

struct abx80x_cap {
	u16 pn;
	bool has_tc;
	bool has_wdog;
};

static struct abx80x_cap abx80x_caps[] = {
	[AB0801] = {.pn = 0x0801},
	[AB0803] = {.pn = 0x0803},
	[AB0804] = {.pn = 0x0804, .has_tc = true, .has_wdog = true},
	[AB0805] = {.pn = 0x0805, .has_tc = true, .has_wdog = true},
	[AB1801] = {.pn = 0x1801},
	[AB1803] = {.pn = 0x1803},
	[AB1804] = {.pn = 0x1804, .has_tc = true, .has_wdog = true},
	[AB1805] = {.pn = 0x1805, .has_tc = true, .has_wdog = true},
	[RV1805] = {.pn = 0x1805, .has_tc = true, .has_wdog = true},
	[ABX80X] = {.pn = 0}
};

struct abx80x_priv {
	struct rtc_device *rtc;
	struct i2c_client *client;
	struct watchdog_device wdog;
};

static int abx80x_is_rc_mode(struct i2c_client *client)
{
	int flags = 0;

	flags =  i2c_smbus_read_byte_data(client, ABX8XX_REG_OSS);
	if (flags < 0) {
		dev_err(&client->dev,
			"Failed to read autocalibration attribute\n");
		return flags;
	}

	return (flags & ABX8XX_OSS_OMODE) ? 1 : 0;
}

static int abx80x_enable_trickle_charger(struct i2c_client *client,
					 u8 trickle_cfg)
{
	int err;

	/*
	 * Write the configuration key register to enable access to the Trickle
	 * register
	 */
	err = i2c_smbus_write_byte_data(client, ABX8XX_REG_CFG_KEY,
					ABX8XX_CFG_KEY_MISC);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write configuration key\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, ABX8XX_REG_TRICKLE,
					ABX8XX_TRICKLE_CHARGE_ENABLE |
					trickle_cfg);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write trickle register\n");
		return -EIO;
	}

	return 0;
}

static int abx80x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[8];
	int err, flags, rc_mode = 0;

	/* Read the Oscillator Failure only in XT mode */
	rc_mode = abx80x_is_rc_mode(client);
	if (rc_mode < 0)
		return rc_mode;

	if (!rc_mode) {
		flags = i2c_smbus_read_byte_data(client, ABX8XX_REG_OSS);
		if (flags < 0)
			return flags;

		if (flags & ABX8XX_OSS_OF) {
			dev_err(dev, "Oscillator failure, data is invalid.\n");
			return -EINVAL;
		}
	}

	err = i2c_smbus_read_i2c_block_data(client, ABX8XX_REG_HTH,
					    sizeof(buf), buf);
	if (err < 0) {
		dev_err(&client->dev, "Unable to read date\n");
		return -EIO;
	}

	tm->tm_sec = bcd2bin(buf[ABX8XX_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[ABX8XX_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[ABX8XX_REG_HR] & 0x3F);
	tm->tm_wday = buf[ABX8XX_REG_WD] & 0x7;
	tm->tm_mday = bcd2bin(buf[ABX8XX_REG_DA] & 0x3F);
	tm->tm_mon = bcd2bin(buf[ABX8XX_REG_MO] & 0x1F) - 1;
	tm->tm_year = bcd2bin(buf[ABX8XX_REG_YR]) + 100;

	return 0;
}

static int abx80x_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[8];
	int err, flags;

	if (tm->tm_year < 100)
		return -EINVAL;

	buf[ABX8XX_REG_HTH] = 0;
	buf[ABX8XX_REG_SC] = bin2bcd(tm->tm_sec);
	buf[ABX8XX_REG_MN] = bin2bcd(tm->tm_min);
	buf[ABX8XX_REG_HR] = bin2bcd(tm->tm_hour);
	buf[ABX8XX_REG_DA] = bin2bcd(tm->tm_mday);
	buf[ABX8XX_REG_MO] = bin2bcd(tm->tm_mon + 1);
	buf[ABX8XX_REG_YR] = bin2bcd(tm->tm_year - 100);
	buf[ABX8XX_REG_WD] = tm->tm_wday;

	err = i2c_smbus_write_i2c_block_data(client, ABX8XX_REG_HTH,
					     sizeof(buf), buf);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write to date registers\n");
		return -EIO;
	}

	/* Clear the OF bit of Oscillator Status Register */
	flags = i2c_smbus_read_byte_data(client, ABX8XX_REG_OSS);
	if (flags < 0)
		return flags;

	err = i2c_smbus_write_byte_data(client, ABX8XX_REG_OSS,
					flags & ~ABX8XX_OSS_OF);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write oscillator status register\n");
		return err;
	}

	return 0;
}

static irqreturn_t abx80x_handle_irq(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct abx80x_priv *priv = i2c_get_clientdata(client);
	struct rtc_device *rtc = priv->rtc;
	int status;

	status = i2c_smbus_read_byte_data(client, ABX8XX_REG_STATUS);
	if (status < 0)
		return IRQ_NONE;

	if (status & ABX8XX_STATUS_AF)
		rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF);

	/*
	 * It is unclear if we'll get an interrupt before the external
	 * reset kicks in.
	 */
	if (status & ABX8XX_STATUS_WDT)
		dev_alert(&client->dev, "watchdog timeout interrupt.\n");

	i2c_smbus_write_byte_data(client, ABX8XX_REG_STATUS, 0);

	return IRQ_HANDLED;
}

static int abx80x_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[7];

	int irq_mask, err;

	if (client->irq <= 0)
		return -EINVAL;

	err = i2c_smbus_read_i2c_block_data(client, ABX8XX_REG_ASC,
					    sizeof(buf), buf);
	if (err)
		return err;

	irq_mask = i2c_smbus_read_byte_data(client, ABX8XX_REG_IRQ);
	if (irq_mask < 0)
		return irq_mask;

	t->time.tm_sec = bcd2bin(buf[0] & 0x7F);
	t->time.tm_min = bcd2bin(buf[1] & 0x7F);
	t->time.tm_hour = bcd2bin(buf[2] & 0x3F);
	t->time.tm_mday = bcd2bin(buf[3] & 0x3F);
	t->time.tm_mon = bcd2bin(buf[4] & 0x1F) - 1;
	t->time.tm_wday = buf[5] & 0x7;

	t->enabled = !!(irq_mask & ABX8XX_IRQ_AIE);
	t->pending = (buf[6] & ABX8XX_STATUS_AF) && t->enabled;

	return err;
}

static int abx80x_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 alarm[6];
	int err;

	if (client->irq <= 0)
		return -EINVAL;

	alarm[0] = 0x0;
	alarm[1] = bin2bcd(t->time.tm_sec);
	alarm[2] = bin2bcd(t->time.tm_min);
	alarm[3] = bin2bcd(t->time.tm_hour);
	alarm[4] = bin2bcd(t->time.tm_mday);
	alarm[5] = bin2bcd(t->time.tm_mon + 1);

	err = i2c_smbus_write_i2c_block_data(client, ABX8XX_REG_AHTH,
					     sizeof(alarm), alarm);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write alarm registers\n");
		return -EIO;
	}

	if (t->enabled) {
		err = i2c_smbus_write_byte_data(client, ABX8XX_REG_IRQ,
						(ABX8XX_IRQ_IM_1_4 |
						 ABX8XX_IRQ_AIE));
		if (err)
			return err;
	}

	return 0;
}

static int abx80x_rtc_set_autocalibration(struct device *dev,
					  int autocalibration)
{
	struct i2c_client *client = to_i2c_client(dev);
	int retval, flags = 0;

	if ((autocalibration != 0) && (autocalibration != 1024) &&
	    (autocalibration != 512)) {
		dev_err(dev, "autocalibration value outside permitted range\n");
		return -EINVAL;
	}

	flags = i2c_smbus_read_byte_data(client, ABX8XX_REG_OSC);
	if (flags < 0)
		return flags;

	if (autocalibration == 0) {
		flags &= ~(ABX8XX_OSC_ACAL_512 | ABX8XX_OSC_ACAL_1024);
	} else if (autocalibration == 1024) {
		/* 1024 autocalibration is 0x10 */
		flags |= ABX8XX_OSC_ACAL_1024;
		flags &= ~(ABX8XX_OSC_ACAL_512);
	} else {
		/* 512 autocalibration is 0x11 */
		flags |= (ABX8XX_OSC_ACAL_1024 | ABX8XX_OSC_ACAL_512);
	}

	/* Unlock write access to Oscillator Control Register */
	retval = i2c_smbus_write_byte_data(client, ABX8XX_REG_CFG_KEY,
					   ABX8XX_CFG_KEY_OSC);
	if (retval < 0) {
		dev_err(dev, "Failed to write CONFIG_KEY register\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(client, ABX8XX_REG_OSC, flags);

	return retval;
}

static int abx80x_rtc_get_autocalibration(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	int flags = 0, autocalibration;

	flags =  i2c_smbus_read_byte_data(client, ABX8XX_REG_OSC);
	if (flags < 0)
		return flags;

	if (flags & ABX8XX_OSC_ACAL_512)
		autocalibration = 512;
	else if (flags & ABX8XX_OSC_ACAL_1024)
		autocalibration = 1024;
	else
		autocalibration = 0;

	return autocalibration;
}

static ssize_t autocalibration_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int retval;
	unsigned long autocalibration = 0;

	retval = kstrtoul(buf, 10, &autocalibration);
	if (retval < 0) {
		dev_err(dev, "Failed to store RTC autocalibration attribute\n");
		return -EINVAL;
	}

	retval = abx80x_rtc_set_autocalibration(dev, autocalibration);

	return retval ? retval : count;
}

static ssize_t autocalibration_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int autocalibration = 0;

	autocalibration = abx80x_rtc_get_autocalibration(dev);
	if (autocalibration < 0) {
		dev_err(dev, "Failed to read RTC autocalibration\n");
		sprintf(buf, "0\n");
		return autocalibration;
	}

	return sprintf(buf, "%d\n", autocalibration);
}

static DEVICE_ATTR_RW(autocalibration);

static ssize_t oscillator_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int retval, flags, rc_mode = 0;

	if (strncmp(buf, "rc", 2) == 0) {
		rc_mode = 1;
	} else if (strncmp(buf, "xtal", 4) == 0) {
		rc_mode = 0;
	} else {
		dev_err(dev, "Oscillator selection value outside permitted ones\n");
		return -EINVAL;
	}

	flags =  i2c_smbus_read_byte_data(client, ABX8XX_REG_OSC);
	if (flags < 0)
		return flags;

	if (rc_mode == 0)
		flags &= ~(ABX8XX_OSC_OSEL);
	else
		flags |= (ABX8XX_OSC_OSEL);

	/* Unlock write access on Oscillator Control register */
	retval = i2c_smbus_write_byte_data(client, ABX8XX_REG_CFG_KEY,
					   ABX8XX_CFG_KEY_OSC);
	if (retval < 0) {
		dev_err(dev, "Failed to write CONFIG_KEY register\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(client, ABX8XX_REG_OSC, flags);
	if (retval < 0) {
		dev_err(dev, "Failed to write Oscillator Control register\n");
		return retval;
	}

	return retval ? retval : count;
}

static ssize_t oscillator_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc_mode = 0;
	struct i2c_client *client = to_i2c_client(dev);

	rc_mode = abx80x_is_rc_mode(client);

	if (rc_mode < 0) {
		dev_err(dev, "Failed to read RTC oscillator selection\n");
		sprintf(buf, "\n");
		return rc_mode;
	}

	if (rc_mode)
		return sprintf(buf, "rc\n");
	else
		return sprintf(buf, "xtal\n");
}

static DEVICE_ATTR_RW(oscillator);

static struct attribute *rtc_calib_attrs[] = {
	&dev_attr_autocalibration.attr,
	&dev_attr_oscillator.attr,
	NULL,
};

static const struct attribute_group rtc_calib_attr_group = {
	.attrs		= rtc_calib_attrs,
};

static int abx80x_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct i2c_client *client = to_i2c_client(dev);
	int err;

	if (enabled)
		err = i2c_smbus_write_byte_data(client, ABX8XX_REG_IRQ,
						(ABX8XX_IRQ_IM_1_4 |
						 ABX8XX_IRQ_AIE));
	else
		err = i2c_smbus_write_byte_data(client, ABX8XX_REG_IRQ,
						ABX8XX_IRQ_IM_1_4);
	return err;
}

static int abx80x_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	int status, tmp;

	switch (cmd) {
	case RTC_VL_READ:
		status = i2c_smbus_read_byte_data(client, ABX8XX_REG_STATUS);
		if (status < 0)
			return status;

		tmp = !!(status & ABX8XX_STATUS_BLF);

		if (copy_to_user((void __user *)arg, &tmp, sizeof(int)))
			return -EFAULT;

		return 0;

	case RTC_VL_CLR:
		status = i2c_smbus_read_byte_data(client, ABX8XX_REG_STATUS);
		if (status < 0)
			return status;

		status &= ~ABX8XX_STATUS_BLF;

		tmp = i2c_smbus_write_byte_data(client, ABX8XX_REG_STATUS, 0);
		if (tmp < 0)
			return tmp;

		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

static const struct rtc_class_ops abx80x_rtc_ops = {
	.read_time	= abx80x_rtc_read_time,
	.set_time	= abx80x_rtc_set_time,
	.read_alarm	= abx80x_read_alarm,
	.set_alarm	= abx80x_set_alarm,
	.alarm_irq_enable = abx80x_alarm_irq_enable,
	.ioctl		= abx80x_ioctl,
};

static int abx80x_dt_trickle_cfg(struct device_node *np)
{
	const char *diode;
	int trickle_cfg = 0;
	int i, ret;
	u32 tmp;

	ret = of_property_read_string(np, "abracon,tc-diode", &diode);
	if (ret)
		return ret;

	if (!strcmp(diode, "standard"))
		trickle_cfg |= ABX8XX_TRICKLE_STANDARD_DIODE;
	else if (!strcmp(diode, "schottky"))
		trickle_cfg |= ABX8XX_TRICKLE_SCHOTTKY_DIODE;
	else
		return -EINVAL;

	ret = of_property_read_u32(np, "abracon,tc-resistor", &tmp);
	if (ret)
		return ret;

	for (i = 0; i < sizeof(trickle_resistors); i++)
		if (trickle_resistors[i] == tmp)
			break;

	if (i == sizeof(trickle_resistors))
		return -EINVAL;

	return (trickle_cfg | i);
}

static void rtc_calib_remove_sysfs_group(void *_dev)
{
	struct device *dev = _dev;

	sysfs_remove_group(&dev->kobj, &rtc_calib_attr_group);
}

#ifdef CONFIG_WATCHDOG

static inline u8 timeout_bits(unsigned int timeout)
{
	return ((timeout << ABX8XX_WDT_BMB_SHIFT) & ABX8XX_WDT_BMB_MASK) |
		 ABX8XX_WDT_WRB_1HZ;
}

static int __abx80x_wdog_set_timeout(struct watchdog_device *wdog,
				     unsigned int timeout)
{
	struct abx80x_priv *priv = watchdog_get_drvdata(wdog);
	u8 val = ABX8XX_WDT_WDS | timeout_bits(timeout);

	/*
	 * Writing any timeout to the WDT register resets the watchdog timer.
	 * Writing 0 disables it.
	 */
	return i2c_smbus_write_byte_data(priv->client, ABX8XX_REG_WDT, val);
}

static int abx80x_wdog_set_timeout(struct watchdog_device *wdog,
				   unsigned int new_timeout)
{
	int err = 0;

	if (watchdog_hw_running(wdog))
		err = __abx80x_wdog_set_timeout(wdog, new_timeout);

	if (err == 0)
		wdog->timeout = new_timeout;

	return err;
}

static int abx80x_wdog_ping(struct watchdog_device *wdog)
{
	return __abx80x_wdog_set_timeout(wdog, wdog->timeout);
}

static int abx80x_wdog_start(struct watchdog_device *wdog)
{
	return __abx80x_wdog_set_timeout(wdog, wdog->timeout);
}

static int abx80x_wdog_stop(struct watchdog_device *wdog)
{
	return __abx80x_wdog_set_timeout(wdog, 0);
}

static const struct watchdog_info abx80x_wdog_info = {
	.identity = "abx80x watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops abx80x_wdog_ops = {
	.owner = THIS_MODULE,
	.start = abx80x_wdog_start,
	.stop = abx80x_wdog_stop,
	.ping = abx80x_wdog_ping,
	.set_timeout = abx80x_wdog_set_timeout,
};

static int abx80x_setup_watchdog(struct abx80x_priv *priv)
{
	priv->wdog.parent = &priv->client->dev;
	priv->wdog.ops = &abx80x_wdog_ops;
	priv->wdog.info = &abx80x_wdog_info;
	priv->wdog.min_timeout = 1;
	priv->wdog.max_timeout = ABX8XX_WDT_MAX_TIME;
	priv->wdog.timeout = ABX8XX_WDT_MAX_TIME;

	watchdog_set_drvdata(&priv->wdog, priv);

	return devm_watchdog_register_device(&priv->client->dev, &priv->wdog);
}
#else
static int abx80x_setup_watchdog(struct abx80x_priv *priv)
{
	return 0;
}
#endif

static int abx80x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct abx80x_priv *priv;
	int i, data, err, trickle_cfg = -EINVAL;
	char buf[7];
	unsigned int part = id->driver_data;
	unsigned int partnumber;
	unsigned int majrev, minrev;
	unsigned int lot;
	unsigned int wafer;
	unsigned int uid;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	err = i2c_smbus_read_i2c_block_data(client, ABX8XX_REG_ID0,
					    sizeof(buf), buf);
	if (err < 0) {
		dev_err(&client->dev, "Unable to read partnumber\n");
		return -EIO;
	}

	partnumber = (buf[0] << 8) | buf[1];
	majrev = buf[2] >> 3;
	minrev = buf[2] & 0x7;
	lot = ((buf[4] & 0x80) << 2) | ((buf[6] & 0x80) << 1) | buf[3];
	uid = ((buf[4] & 0x7f) << 8) | buf[5];
	wafer = (buf[6] & 0x7c) >> 2;
	dev_info(&client->dev, "model %04x, revision %u.%u, lot %x, wafer %x, uid %x\n",
		 partnumber, majrev, minrev, lot, wafer, uid);

	data = i2c_smbus_read_byte_data(client, ABX8XX_REG_CTRL1);
	if (data < 0) {
		dev_err(&client->dev, "Unable to read control register\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, ABX8XX_REG_CTRL1,
					((data & ~(ABX8XX_CTRL_12_24 |
						   ABX8XX_CTRL_ARST)) |
					 ABX8XX_CTRL_WRITE));
	if (err < 0) {
		dev_err(&client->dev, "Unable to write control register\n");
		return -EIO;
	}

	/* Configure RV1805 specifics */
	if (part == RV1805) {
		/*
		 * Avoid accidentally entering test mode. This can happen
		 * on the RV1805 in case the reserved bit 5 in control2
		 * register is set. RV-1805-C3 datasheet indicates that
		 * the bit should be cleared in section 11h - Control2.
		 */
		data = i2c_smbus_read_byte_data(client, ABX8XX_REG_CTRL2);
		if (data < 0) {
			dev_err(&client->dev,
				"Unable to read control2 register\n");
			return -EIO;
		}

		err = i2c_smbus_write_byte_data(client, ABX8XX_REG_CTRL2,
						data & ~ABX8XX_CTRL2_RSVD);
		if (err < 0) {
			dev_err(&client->dev,
				"Unable to write control2 register\n");
			return -EIO;
		}

		/*
		 * Avoid extra power leakage. The RV1805 uses smaller
		 * 10pin package and the EXTI input is not present.
		 * Disable it to avoid leakage.
		 */
		data = i2c_smbus_read_byte_data(client, ABX8XX_REG_OUT_CTRL);
		if (data < 0) {
			dev_err(&client->dev,
				"Unable to read output control register\n");
			return -EIO;
		}

		/*
		 * Write the configuration key register to enable access to
		 * the config2 register
		 */
		err = i2c_smbus_write_byte_data(client, ABX8XX_REG_CFG_KEY,
						ABX8XX_CFG_KEY_MISC);
		if (err < 0) {
			dev_err(&client->dev,
				"Unable to write configuration key\n");
			return -EIO;
		}

		err = i2c_smbus_write_byte_data(client, ABX8XX_REG_OUT_CTRL,
						data | ABX8XX_OUT_CTRL_EXDS);
		if (err < 0) {
			dev_err(&client->dev,
				"Unable to write output control register\n");
			return -EIO;
		}
	}

	/* part autodetection */
	if (part == ABX80X) {
		for (i = 0; abx80x_caps[i].pn; i++)
			if (partnumber == abx80x_caps[i].pn)
				break;
		if (abx80x_caps[i].pn == 0) {
			dev_err(&client->dev, "Unknown part: %04x\n",
				partnumber);
			return -EINVAL;
		}
		part = i;
	}

	if (partnumber != abx80x_caps[part].pn) {
		dev_err(&client->dev, "partnumber mismatch %04x != %04x\n",
			partnumber, abx80x_caps[part].pn);
		return -EINVAL;
	}

	if (np && abx80x_caps[part].has_tc)
		trickle_cfg = abx80x_dt_trickle_cfg(np);

	if (trickle_cfg > 0) {
		dev_info(&client->dev, "Enabling trickle charger: %02x\n",
			 trickle_cfg);
		abx80x_enable_trickle_charger(client, trickle_cfg);
	}

	err = i2c_smbus_write_byte_data(client, ABX8XX_REG_CD_TIMER_CTL,
					BIT(2));
	if (err)
		return err;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(priv->rtc))
		return PTR_ERR(priv->rtc);

	priv->rtc->ops = &abx80x_rtc_ops;
	priv->client = client;

	i2c_set_clientdata(client, priv);

	if (abx80x_caps[part].has_wdog) {
		err = abx80x_setup_watchdog(priv);
		if (err)
			return err;
	}

	if (client->irq > 0) {
		dev_info(&client->dev, "IRQ %d supplied\n", client->irq);
		err = devm_request_threaded_irq(&client->dev, client->irq, NULL,
						abx80x_handle_irq,
						IRQF_SHARED | IRQF_ONESHOT,
						"abx8xx",
						client);
		if (err) {
			dev_err(&client->dev, "unable to request IRQ, alarms disabled\n");
			client->irq = 0;
		}
	}

	/* Export sysfs entries */
	err = sysfs_create_group(&(&client->dev)->kobj, &rtc_calib_attr_group);
	if (err) {
		dev_err(&client->dev, "Failed to create sysfs group: %d\n",
			err);
		return err;
	}

	err = devm_add_action_or_reset(&client->dev,
				       rtc_calib_remove_sysfs_group,
				       &client->dev);
	if (err) {
		dev_err(&client->dev,
			"Failed to add sysfs cleanup action: %d\n",
			err);
		return err;
	}

	err = rtc_register_device(priv->rtc);

	return err;
}

static int abx80x_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id abx80x_id[] = {
	{ "abx80x", ABX80X },
	{ "ab0801", AB0801 },
	{ "ab0803", AB0803 },
	{ "ab0804", AB0804 },
	{ "ab0805", AB0805 },
	{ "ab1801", AB1801 },
	{ "ab1803", AB1803 },
	{ "ab1804", AB1804 },
	{ "ab1805", AB1805 },
	{ "rv1805", RV1805 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, abx80x_id);

static struct i2c_driver abx80x_driver = {
	.driver		= {
		.name	= "rtc-abx80x",
	},
	.probe		= abx80x_probe,
	.remove		= abx80x_remove,
	.id_table	= abx80x_id,
};

module_i2c_driver(abx80x_driver);

MODULE_AUTHOR("Philippe De Muyter <phdm@macqel.be>");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_DESCRIPTION("Abracon ABX80X RTC driver");
MODULE_LICENSE("GPL v2");
