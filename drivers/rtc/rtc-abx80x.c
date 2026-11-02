// SPDX-License-Identifier: GPL-2.0
/*
 * A driver for the I2C and SPI members of the Abracon AB x8xx RTC family,
 * and compatible: AB 1805, AB 0805, AB 1815 and AB 0815
 *
 * Copyright 2014-2015 Macq S.A.
 *
 * Author: Philippe De Muyter <phdm@macqel.be>
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 *
 */

#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
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
#define ABX8XX_STATUS_WDT	BIT(5)

#define ABX8XX_REG_CTRL1	0x10
#define ABX8XX_CTRL_WRITE	BIT(0)
#define ABX8XX_CTRL_ARST	BIT(2)
#define ABX8XX_CTRL_12_24	BIT(6)

#define ABX8XX_REG_CTRL2	0x11
#define ABX8XX_CTRL2_RSVD	BIT(5)

#define ABX8XX_REG_IRQ		0x12
#define ABX8XX_IRQ_EX1E		BIT(0)
#define ABX8XX_IRQ_EX2E		BIT(1)
#define ABX8XX_IRQ_AIE		BIT(2)
#define ABX8XX_IRQ_TIE		BIT(3)
#define ABX8XX_IRQ_BLIE		BIT(4)
#define ABX8XX_IRQ_IM_1_4	(0x3 << 5)

#define ABX8XX_REG_CD_TIMER_CTL	0x18

#define ABX8XX_REG_OSC		0x1c
#define ABX8XX_OSC_ACIE		BIT(0)
#define ABX8XX_OSC_OFIE		BIT(1)
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

#define ABX8XX_REG_EXTRAM	0x3f
#define ABX8XX_EXTRAM_XADS	GENMASK(1, 0)

#define ABX8XX_SRAM_BASE	0x40
#define ABX8XX_SRAM_WIN_SIZE	0x40
#define ABX8XX_RAM_SIZE		256

#define NVMEM_ADDR_LOWER	GENMASK(5, 0)
#define NVMEM_ADDR_UPPER	GENMASK(7, 6)

static u8 trickle_resistors[] = {0, 3, 6, 11};

enum abx80x_chip {AB0801, AB0803, AB0804, AB0805, AB0815,
	AB1801, AB1803, AB1804, AB1805, AB1815, RV1805, ABX80X};

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
	[AB0815] = {.pn = 0x0815, .has_tc = true, .has_wdog = true},
	[AB1801] = {.pn = 0x1801},
	[AB1803] = {.pn = 0x1803},
	[AB1804] = {.pn = 0x1804, .has_tc = true, .has_wdog = true},
	[AB1805] = {.pn = 0x1805, .has_tc = true, .has_wdog = true},
	[AB1815] = {.pn = 0x1815, .has_tc = true, .has_wdog = true},
	[RV1805] = {.pn = 0x1805, .has_tc = true, .has_wdog = true},
	[ABX80X] = {.pn = 0}
};

struct abx80x_priv {
	struct rtc_device *rtc;
	struct regmap *regmap;
	struct watchdog_device wdog;
	int irq;
};

static int abx80x_write_config_key(struct device *dev, u8 key)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);

	if (regmap_write(priv->regmap, ABX8XX_REG_CFG_KEY, key) < 0) {
		dev_err(dev, "Unable to write configuration key\n");
		return -EIO;
	}

	return 0;
}

static int abx80x_is_rc_mode(struct device *dev)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned int flags = 0;
	int err;

	err = regmap_read(priv->regmap, ABX8XX_REG_OSS, &flags);
	if (err < 0) {
		dev_err(dev, "Failed to read autocalibration attribute\n");
		return err;
	}

	return (flags & ABX8XX_OSS_OMODE) ? 1 : 0;
}

static int abx80x_enable_trickle_charger(struct device *dev, u8 trickle_cfg)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	int err;

	/*
	 * Write the configuration key register to enable access to the Trickle
	 * register
	 */
	if (abx80x_write_config_key(dev, ABX8XX_CFG_KEY_MISC) < 0)
		return -EIO;

	err = regmap_write(priv->regmap, ABX8XX_REG_TRICKLE,
			   ABX8XX_TRICKLE_CHARGE_ENABLE | trickle_cfg);
	if (err < 0) {
		dev_err(dev, "Unable to write trickle register\n");
		return -EIO;
	}

	return 0;
}

static int abx80x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned char buf[8];
	unsigned int flags;
	int err, rc_mode = 0;

	/* Read the Oscillator Failure only in XT mode */
	rc_mode = abx80x_is_rc_mode(dev);
	if (rc_mode < 0)
		return rc_mode;

	if (!rc_mode) {
		err = regmap_read(priv->regmap, ABX8XX_REG_OSS, &flags);
		if (err < 0)
			return err;

		if (flags & ABX8XX_OSS_OF) {
			dev_err(dev, "Oscillator failure, data is invalid.\n");
			return -EINVAL;
		}
	}

	err = regmap_bulk_read(priv->regmap, ABX8XX_REG_HTH, buf, sizeof(buf));
	if (err < 0) {
		dev_err(dev, "Unable to read date\n");
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
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned char buf[8];
	int err;

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

	err = regmap_bulk_write(priv->regmap, ABX8XX_REG_HTH, buf,
				sizeof(buf));
	if (err < 0) {
		dev_err(dev, "Unable to write to date registers\n");
		return -EIO;
	}

	/* Clear the OF bit of Oscillator Status Register */
	err = regmap_update_bits(priv->regmap, ABX8XX_REG_OSS, ABX8XX_OSS_OF, 0);
	if (err < 0)
		dev_err(dev, "Unable to write oscillator status register\n");

	return err;
}

static irqreturn_t abx80x_handle_irq(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	struct rtc_device *rtc = priv->rtc;
	unsigned int status, status_flags_clear;
	int err;

	guard(rtc_ops)(rtc);

	err = regmap_read(priv->regmap, ABX8XX_REG_STATUS, &status);
	if (err < 0)
		return IRQ_NONE;

	if (status & ABX8XX_STATUS_AF)
		rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF);

	/*
	 * It is unclear if we'll get an interrupt before the external
	 * reset kicks in.
	 */
	if (status & ABX8XX_STATUS_WDT)
		dev_alert(dev, "watchdog timeout interrupt.\n");

	status_flags_clear = status & (ABX8XX_STATUS_AF | ABX8XX_STATUS_WDT);

	if (status_flags_clear) {
		regmap_write(priv->regmap, ABX8XX_REG_STATUS,
			     status & ~status_flags_clear);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int abx80x_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned char buf[7];

	unsigned int irq_mask;
	int err;

	if (priv->irq <= 0)
		return -EINVAL;

	err = regmap_bulk_read(priv->regmap, ABX8XX_REG_ASC, buf, sizeof(buf));
	if (err)
		return err;

	err = regmap_read(priv->regmap, ABX8XX_REG_IRQ, &irq_mask);
	if (err < 0)
		return err;

	t->time.tm_sec = bcd2bin(buf[0] & 0x7F);
	t->time.tm_min = bcd2bin(buf[1] & 0x7F);
	t->time.tm_hour = bcd2bin(buf[2] & 0x3F);
	t->time.tm_mday = bcd2bin(buf[3] & 0x3F);
	t->time.tm_mon = bcd2bin(buf[4] & 0x1F) - 1;
	t->time.tm_wday = buf[5] & 0x7;

	t->enabled = !!(irq_mask & ABX8XX_IRQ_AIE);
	t->pending = (buf[6] & ABX8XX_STATUS_AF) && t->enabled;

	return 0;
}

static int abx80x_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	u8 alarm[6];
	int err;

	if (priv->irq <= 0)
		return -EINVAL;

	alarm[0] = 0x0;
	alarm[1] = bin2bcd(t->time.tm_sec);
	alarm[2] = bin2bcd(t->time.tm_min);
	alarm[3] = bin2bcd(t->time.tm_hour);
	alarm[4] = bin2bcd(t->time.tm_mday);
	alarm[5] = bin2bcd(t->time.tm_mon + 1);

	err = regmap_bulk_write(priv->regmap, ABX8XX_REG_AHTH,
				alarm, sizeof(alarm));
	if (err < 0) {
		dev_err(dev, "Unable to write alarm registers\n");
		return -EIO;
	}

	if (t->enabled) {
		err = regmap_write(priv->regmap, ABX8XX_REG_IRQ,
				   ABX8XX_IRQ_IM_1_4 | ABX8XX_IRQ_AIE);
		if (err)
			return err;
	}

	return 0;
}

static int abx80x_rtc_set_autocalibration(struct device *dev,
					  int autocalibration)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned int flags = 0;

	if ((autocalibration != 0) && (autocalibration != 1024) &&
	    (autocalibration != 512)) {
		dev_err(dev, "autocalibration value outside permitted range\n");
		return -EINVAL;
	}

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

	guard(rtc_ops)(priv->rtc);

	/* Unlock write access to Oscillator Control Register */
	if (abx80x_write_config_key(dev, ABX8XX_CFG_KEY_OSC) < 0)
		return -EIO;

	return regmap_write_bits(priv->regmap, ABX8XX_REG_OSC,
				 ABX8XX_OSC_ACAL_1024 | ABX8XX_OSC_ACAL_512,
				 flags);
}

static int abx80x_rtc_get_autocalibration(struct device *dev)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned int flags = 0;
	int autocalibration, err;

	err = regmap_read(priv->regmap, ABX8XX_REG_OSC, &flags);
	if (err < 0)
		return err;

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

	retval = abx80x_rtc_set_autocalibration(dev->parent, autocalibration);

	return retval ? retval : count;
}

static ssize_t autocalibration_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int autocalibration = 0;

	autocalibration = abx80x_rtc_get_autocalibration(dev->parent);
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
	struct abx80x_priv *priv = dev_get_drvdata(dev->parent);
	int retval, rc_mode = 0;

	if (strncmp(buf, "rc", 2) == 0) {
		rc_mode = 1;
	} else if (strncmp(buf, "xtal", 4) == 0) {
		rc_mode = 0;
	} else {
		dev_err(dev, "Oscillator selection value outside permitted ones\n");
		return -EINVAL;
	}

	guard(rtc_ops)(priv->rtc);

	/* Unlock write access on Oscillator Control register */
	if (abx80x_write_config_key(dev->parent, ABX8XX_CFG_KEY_OSC) < 0)
		return -EIO;

	retval = regmap_write_bits(priv->regmap, ABX8XX_REG_OSC, ABX8XX_OSC_OSEL,
				   rc_mode == 0 ? 0 : (ABX8XX_OSC_OSEL));
	if (retval < 0)
		dev_err(dev, "Failed to write Oscillator Control register\n");

	return retval ? retval : count;
}

static ssize_t oscillator_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc_mode = 0;

	rc_mode = abx80x_is_rc_mode(dev->parent);

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
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	int err;

	if (enabled)
		err = regmap_write(priv->regmap, ABX8XX_REG_IRQ,
				   ABX8XX_IRQ_IM_1_4 | ABX8XX_IRQ_AIE);
	else
		err = regmap_write(priv->regmap, ABX8XX_REG_IRQ,
				   ABX8XX_IRQ_IM_1_4);
	return err;
}

static int abx80x_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);
	unsigned int status;
	int err, tmp;

	switch (cmd) {
	case RTC_VL_READ:
		err = regmap_read(priv->regmap, ABX8XX_REG_STATUS, &status);
		if (err < 0)
			return err;

		tmp = status & ABX8XX_STATUS_BLF ? RTC_VL_BACKUP_LOW : 0;

		return put_user(tmp, (unsigned int __user *)arg);

	case RTC_VL_CLR:
		err = regmap_update_bits(priv->regmap, ABX8XX_REG_STATUS,
					 ABX8XX_STATUS_BLF, 0);

		return err;

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

static int abx80x_dt_trickle_cfg(struct device *dev)
{
	struct device_node *np = dev->of_node;
	const char *diode;
	int trickle_cfg = 0;
	int i, ret;
	u32 tmp;

	ret = of_property_read_string(np, "abracon,tc-diode", &diode);
	if (ret)
		return ret;

	if (!strcmp(diode, "standard")) {
		trickle_cfg |= ABX8XX_TRICKLE_STANDARD_DIODE;
	} else if (!strcmp(diode, "schottky")) {
		trickle_cfg |= ABX8XX_TRICKLE_SCHOTTKY_DIODE;
	} else {
		dev_dbg(dev, "Invalid tc-diode value: %s\n", diode);
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "abracon,tc-resistor", &tmp);
	if (ret)
		return ret;

	for (i = 0; i < sizeof(trickle_resistors); i++)
		if (trickle_resistors[i] == tmp)
			break;

	if (i == sizeof(trickle_resistors)) {
		dev_dbg(dev, "Invalid tc-resistor value: %u\n", tmp);
		return -EINVAL;
	}

	return (trickle_cfg | i);
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

	guard(rtc_ops)(priv->rtc);

	/*
	 * Writing any timeout to the WDT register resets the watchdog timer.
	 * Writing 0 disables it.
	 */
	return regmap_write(priv->regmap, ABX8XX_REG_WDT, val);
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

static int abx80x_setup_watchdog(struct device *dev)
{
	struct abx80x_priv *priv = dev_get_drvdata(dev);

	priv->wdog.parent = dev;
	priv->wdog.ops = &abx80x_wdog_ops;
	priv->wdog.info = &abx80x_wdog_info;
	priv->wdog.min_timeout = 1;
	priv->wdog.max_timeout = ABX8XX_WDT_MAX_TIME;
	priv->wdog.timeout = ABX8XX_WDT_MAX_TIME;

	watchdog_set_drvdata(&priv->wdog, priv);

	return devm_watchdog_register_device(dev, &priv->wdog);
}
#else
static int abx80x_setup_watchdog(struct device *dev)
{
	return 0;
}
#endif

static int abx80x_nvmem_xfer(struct abx80x_priv *priv, unsigned int offset,
			     void *val, size_t bytes, bool write)
{
	int ret;

	while (bytes) {
		u8 reg, len, lower, upper;

		lower = FIELD_GET(NVMEM_ADDR_LOWER, offset);
		upper = FIELD_GET(NVMEM_ADDR_UPPER, offset);
		reg = ABX8XX_SRAM_BASE + lower;
		len = min(lower + bytes, (size_t)ABX8XX_SRAM_WIN_SIZE) - lower;
		len = min_t(u8, len, I2C_SMBUS_BLOCK_MAX);

		guard(rtc_ops)(priv->rtc);

		ret = regmap_update_bits(priv->regmap, ABX8XX_REG_EXTRAM,
					 ABX8XX_EXTRAM_XADS, upper);
		if (ret)
			return ret;

		if (write) {
			ret = regmap_bulk_write(priv->regmap, reg, val, len);
			if (ret)
				return ret;
		} else {
			ret = regmap_bulk_read(priv->regmap, reg, val, len);
			if (ret)
				return ret;
		}

		offset += len;
		val += len;
		bytes -= len;
	}

	return 0;
}

static int abx80x_nvmem_read(void *priv, unsigned int offset, void *val,
			     size_t bytes)
{
	return abx80x_nvmem_xfer(priv, offset, val, bytes, false);
}

static int abx80x_nvmem_write(void *priv, unsigned int offset, void *val,
			      size_t bytes)
{
	return abx80x_nvmem_xfer(priv, offset, val, bytes, true);
}

static int abx80x_setup_nvmem(struct abx80x_priv *priv)
{
	struct nvmem_config config = {
		.type = NVMEM_TYPE_BATTERY_BACKED,
		.reg_read = abx80x_nvmem_read,
		.reg_write = abx80x_nvmem_write,
		.size = ABX8XX_RAM_SIZE,
		.priv = priv,
	};

	return devm_rtc_nvmem_register(priv->rtc, &config);
}

static const struct regmap_range abx80x_no_read_ranges[] = {
	regmap_reg_range(0x1e, 0x1e),
	regmap_reg_range(0x22, 0x25),
	regmap_reg_range(0x31, 0x3e),
};

static const struct regmap_range abx80x_no_write_ranges[] = {
	regmap_reg_range(0x1e, 0x1e),
	regmap_reg_range(0x22, 0x25),
	regmap_reg_range(ABX8XX_REG_ID0, ABX8XX_REG_ID0 + 6),
	regmap_reg_range(0x31, 0x3e),
};

static const struct regmap_access_table abx80x_read_table = {
	.no_ranges = abx80x_no_read_ranges,
	.n_no_ranges = ARRAY_SIZE(abx80x_no_read_ranges),
};

static const struct regmap_access_table abx80x_write_table = {
	.no_ranges = abx80x_no_write_ranges,
	.n_no_ranges = ARRAY_SIZE(abx80x_no_write_ranges),
};

static int abx80x_probe(struct device *dev, struct regmap *regmap, int irq,
			struct device_node *np, unsigned int part)
{
	struct abx80x_priv *priv;
	int i, err, trickle_cfg = -EINVAL;
	char buf[7];
	unsigned int partnumber;
	unsigned int majrev, minrev;
	unsigned int lot;
	unsigned int wafer;
	unsigned int uid;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(priv->rtc))
		return PTR_ERR(priv->rtc);

	priv->rtc->ops = &abx80x_rtc_ops;
	priv->irq = irq;
	priv->regmap = regmap;

	dev_set_drvdata(dev, priv);

	err = regmap_bulk_read(regmap, ABX8XX_REG_ID0, buf, sizeof(buf));
	if (err < 0) {
		dev_err(dev, "Unable to read partnumber\n");
		return -EIO;
	}

	partnumber = (buf[0] << 8) | buf[1];
	majrev = buf[2] >> 3;
	minrev = buf[2] & 0x7;
	lot = ((buf[4] & 0x80) << 2) | ((buf[6] & 0x80) << 1) | buf[3];
	uid = ((buf[4] & 0x7f) << 8) | buf[5];
	wafer = (buf[6] & 0x7c) >> 2;
	dev_info(dev, "model %04x, revision %u.%u, lot %x, wafer %x, uid %x\n",
		 partnumber, majrev, minrev, lot, wafer, uid);

	err = regmap_update_bits(regmap, ABX8XX_REG_CTRL1,
				 ABX8XX_CTRL_12_24 | ABX8XX_CTRL_ARST | ABX8XX_CTRL_WRITE,
				 ABX8XX_CTRL_WRITE);
	if (err < 0) {
		dev_err(dev, "Unable to write control register\n");
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
		err = regmap_update_bits(regmap, ABX8XX_REG_CTRL2,
					 ABX8XX_CTRL2_RSVD, 0);
		if (err < 0) {
			dev_err(dev, "Unable to write control2 register\n");
			return -EIO;
		}

		/*
		 * Write the configuration key register to enable access to
		 * the config2 register
		 */
		if (abx80x_write_config_key(dev, ABX8XX_CFG_KEY_MISC) < 0)
			return -EIO;

		/*
		 * Avoid extra power leakage. The RV1805 uses smaller
		 * 10pin package and the EXTI input is not present.
		 * Disable it to avoid leakage.
		 */
		err = regmap_write_bits(regmap, ABX8XX_REG_OUT_CTRL,
					ABX8XX_OUT_CTRL_EXDS,
					ABX8XX_OUT_CTRL_EXDS);
		if (err < 0) {
			dev_err(dev,
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
			dev_err(dev, "Unknown part: %04x\n", partnumber);
			return -EINVAL;
		}
		part = i;
	}

	if (partnumber != abx80x_caps[part].pn) {
		dev_err(dev, "partnumber mismatch %04x != %04x\n",
			partnumber, abx80x_caps[part].pn);
		return -EINVAL;
	}

	if (np && abx80x_caps[part].has_tc)
		trickle_cfg = abx80x_dt_trickle_cfg(dev);

	if (trickle_cfg > 0) {
		dev_info(dev, "Enabling trickle charger: %02x\n", trickle_cfg);
		abx80x_enable_trickle_charger(dev, trickle_cfg);
	}

	err = regmap_write(regmap, ABX8XX_REG_CD_TIMER_CTL, BIT(2));
	if (err)
		return err;

	/* Disable unused interrupts */
	err = regmap_update_bits(regmap, ABX8XX_REG_IRQ,
				 ABX8XX_IRQ_EX1E | ABX8XX_IRQ_EX2E |
				 ABX8XX_IRQ_TIE | ABX8XX_IRQ_BLIE, 0);
	if (err < 0) {
		dev_err(dev, "Unable to update irq register\n");
		return -EIO;
	}

	/* Unlock write access to Oscillator Control Register */
	if (abx80x_write_config_key(dev, ABX8XX_CFG_KEY_OSC) < 0)
		return -EIO;

	err = regmap_write_bits(regmap, ABX8XX_REG_OSC,
				ABX8XX_OSC_ACIE | ABX8XX_OSC_OFIE, 0);
	if (err < 0) {
		dev_err(dev, "Unable to update Oscillator Control register\n");
		return -EIO;
	}

	if (abx80x_caps[part].has_wdog) {
		err = abx80x_setup_watchdog(dev);
		if (err)
			return err;
	}

	err = abx80x_setup_nvmem(priv);
	if (err)
		return err;

	if (priv->irq > 0) {
		dev_info(dev, "IRQ %d supplied\n", priv->irq);
		err = devm_request_threaded_irq(dev, priv->irq, NULL,
						abx80x_handle_irq,
						IRQF_SHARED | IRQF_ONESHOT,
						"abx8xx",
						dev);
		if (err) {
			dev_err(dev, "unable to request IRQ, alarms disabled\n");
			priv->irq = 0;
		}
	}
	if (priv->irq <= 0)
		clear_bit(RTC_FEATURE_ALARM, priv->rtc->features);

	err = rtc_add_group(priv->rtc, &rtc_calib_attr_group);
	if (err) {
		dev_err(dev, "Failed to create sysfs group: %d\n", err);
		return err;
	}

	return devm_rtc_register_device(priv->rtc);
}

#if IS_ENABLED(CONFIG_I2C)

static const struct regmap_config abx80x_regmap_config_i2c = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ABX8XX_SRAM_BASE + ABX8XX_SRAM_WIN_SIZE - 1,

	.rd_table = &abx80x_read_table,
	.wr_table = &abx80x_write_table,
};

static const struct i2c_device_id abx80x_id[] = {
	{ .name = "abx80x", .driver_data = ABX80X },
	{ .name = "ab0801", .driver_data = AB0801 },
	{ .name = "ab0803", .driver_data = AB0803 },
	{ .name = "ab0804", .driver_data = AB0804 },
	{ .name = "ab0805", .driver_data = AB0805 },
	{ .name = "ab1801", .driver_data = AB1801 },
	{ .name = "ab1803", .driver_data = AB1803 },
	{ .name = "ab1804", .driver_data = AB1804 },
	{ .name = "ab1805", .driver_data = AB1805 },
	{ .name = "rv1805", .driver_data = RV1805 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, abx80x_id);

#ifdef CONFIG_OF
static const struct of_device_id abx80x_of_match[] = {
	{
		.compatible = "abracon,abx80x",
		.data = (void *)ABX80X
	},
	{
		.compatible = "abracon,ab0801",
		.data = (void *)AB0801
	},
	{
		.compatible = "abracon,ab0803",
		.data = (void *)AB0803
	},
	{
		.compatible = "abracon,ab0804",
		.data = (void *)AB0804
	},
	{
		.compatible = "abracon,ab0805",
		.data = (void *)AB0805
	},
	{
		.compatible = "abracon,ab1801",
		.data = (void *)AB1801
	},
	{
		.compatible = "abracon,ab1803",
		.data = (void *)AB1803
	},
	{
		.compatible = "abracon,ab1804",
		.data = (void *)AB1804
	},
	{
		.compatible = "abracon,ab1805",
		.data = (void *)AB1805
	},
	{
		.compatible = "microcrystal,rv1805",
		.data = (void *)RV1805
	},
	{ }
};
MODULE_DEVICE_TABLE(of, abx80x_of_match);
#endif

static int abx80x_i2c_probe(struct i2c_client *client)
{
	unsigned int part = (uintptr_t)i2c_get_match_data(client);
	struct regmap *regmap;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	regmap = devm_regmap_init_i2c(client, &abx80x_regmap_config_i2c);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Unable to allocate regmap\n");
		return PTR_ERR(regmap);
	}

	return abx80x_probe(&client->dev, regmap, client->irq,
			    client->dev.of_node, part);
}

static struct i2c_driver abx80x_driver = {
	.driver		= {
		.name	= "rtc-abx80x",
		.of_match_table = of_match_ptr(abx80x_of_match),
	},
	.probe		= abx80x_i2c_probe,
	.id_table	= abx80x_id,
};

static int abx80x_register_driver(void)
{
	return i2c_add_driver(&abx80x_driver);
}

static void abx80x_unregister_driver(void)
{
	i2c_del_driver(&abx80x_driver);
}

#else

static int abx80x_register_driver(void)
{
	return 0;
}

static void abx80x_unregister_driver(void)
{
}

#endif /* IS_ENABLED(CONFIG_I2C) */

#if IS_ENABLED(CONFIG_SPI_MASTER)

static const struct regmap_config abx80x_regmap_config_spi = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ABX8XX_SRAM_BASE + ABX8XX_SRAM_WIN_SIZE - 1,

	.rd_table = &abx80x_read_table,
	.wr_table = &abx80x_write_table,

	.write_flag_mask = BIT(7),
};

static const struct spi_device_id abx81x_id[] = {
	{ "ab0815", AB0815 },
	{ "ab1815", AB1815 },
	{ }
};
MODULE_DEVICE_TABLE(spi, abx81x_id);

#ifdef CONFIG_OF
static const struct of_device_id abx81x_of_match[] = {
	{
		.compatible = "abracon,ab0815",
		.data = (void *)AB0815
	},
	{
		.compatible = "abracon,ab1815",
		.data = (void *)AB1815
	},
	{ }
};
MODULE_DEVICE_TABLE(of, abx81x_of_match);
#endif

static int abx81x_spi_probe(struct spi_device *spi)
{
	unsigned int part = (uintptr_t)spi_get_device_match_data(spi);
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &abx80x_regmap_config_spi);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Unable to allocate regmap\n");
		return PTR_ERR(regmap);
	}

	return abx80x_probe(&spi->dev, regmap, spi->irq,
			    spi->dev.of_node, part);
}

static struct spi_driver abx81x_driver = {
	.driver		= {
		.name	= "rtc-abx81x",
		.of_match_table = of_match_ptr(abx81x_of_match),
	},
	.probe		= abx81x_spi_probe,
	.id_table	= abx81x_id,
};

static int abx81x_register_driver(void)
{
	return spi_register_driver(&abx81x_driver);
}

static void abx81x_unregister_driver(void)
{
	spi_unregister_driver(&abx81x_driver);
}

#else

static int abx81x_register_driver(void)
{
	return 0;
}

static void abx81x_unregister_driver(void)
{
}

#endif /* IS_ENABLED(CONFIG_SPI_MASTER) */

static int __init abx80x_init(void)
{
	int ret;

	ret = abx80x_register_driver();
	if (ret)
		return ret;

	ret = abx81x_register_driver();
	if (ret)
		abx80x_unregister_driver();

	return ret;
}
module_init(abx80x_init);

static void __exit abx80x_exit(void)
{
	abx81x_unregister_driver();
	abx80x_unregister_driver();
}
module_exit(abx80x_exit);

MODULE_AUTHOR("Philippe De Muyter <phdm@macqel.be>");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_DESCRIPTION("Abracon ABX80X RTC driver");
MODULE_LICENSE("GPL v2");
