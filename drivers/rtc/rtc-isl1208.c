/*
 * Intersil ISL1208 rtc class driver
 *
 * Copyright 2005,2006 Hebert Valerio Riedel <hvr@gnu.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>

#define DRV_VERSION "0.3"

/* Register map */
/* rtc section */
#define ISL1208_REG_SC  0x00
#define ISL1208_REG_MN  0x01
#define ISL1208_REG_HR  0x02
#define ISL1208_REG_HR_MIL     (1<<7)	/* 24h/12h mode */
#define ISL1208_REG_HR_PM      (1<<5)	/* PM/AM bit in 12h mode */
#define ISL1208_REG_DT  0x03
#define ISL1208_REG_MO  0x04
#define ISL1208_REG_YR  0x05
#define ISL1208_REG_DW  0x06
#define ISL1208_RTC_SECTION_LEN 7

/* control/status section */
#define ISL1208_REG_SR  0x07
#define ISL1208_REG_SR_ARST    (1<<7)	/* auto reset */
#define ISL1208_REG_SR_XTOSCB  (1<<6)	/* crystal oscillator */
#define ISL1208_REG_SR_WRTC    (1<<4)	/* write rtc */
#define ISL1208_REG_SR_ALM     (1<<2)	/* alarm */
#define ISL1208_REG_SR_BAT     (1<<1)	/* battery */
#define ISL1208_REG_SR_RTCF    (1<<0)	/* rtc fail */
#define ISL1208_REG_INT 0x08
#define ISL1208_REG_INT_ALME   (1<<6)   /* alarm enable */
#define ISL1208_REG_INT_IM     (1<<7)   /* interrupt/alarm mode */
#define ISL1208_REG_09  0x09	/* reserved */
#define ISL1208_REG_ATR 0x0a
#define ISL1208_REG_DTR 0x0b

/* alarm section */
#define ISL1208_REG_SCA 0x0c
#define ISL1208_REG_MNA 0x0d
#define ISL1208_REG_HRA 0x0e
#define ISL1208_REG_DTA 0x0f
#define ISL1208_REG_MOA 0x10
#define ISL1208_REG_DWA 0x11
#define ISL1208_ALARM_SECTION_LEN 6

/* user section */
#define ISL1208_REG_USR1 0x12
#define ISL1208_REG_USR2 0x13
#define ISL1208_USR_SECTION_LEN 2

static struct i2c_driver isl1208_driver;

/* block read */
static int
isl1208_i2c_read_regs(struct i2c_client *client, u8 reg, u8 buf[],
		      unsigned len)
{
	u8 reg_addr[1] = { reg };
	struct i2c_msg msgs[2] = {
		{client->addr, 0, sizeof(reg_addr), reg_addr}
		,
		{client->addr, I2C_M_RD, len, buf}
	};
	int ret;

	BUG_ON(reg > ISL1208_REG_USR2);
	BUG_ON(reg + len > ISL1208_REG_USR2 + 1);

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret > 0)
		ret = 0;
	return ret;
}

/* block write */
static int
isl1208_i2c_set_regs(struct i2c_client *client, u8 reg, u8 const buf[],
		     unsigned len)
{
	u8 i2c_buf[ISL1208_REG_USR2 + 2];
	struct i2c_msg msgs[1] = {
		{client->addr, 0, len + 1, i2c_buf}
	};
	int ret;

	BUG_ON(reg > ISL1208_REG_USR2);
	BUG_ON(reg + len > ISL1208_REG_USR2 + 1);

	i2c_buf[0] = reg;
	memcpy(&i2c_buf[1], &buf[0], len);

	ret = i2c_transfer(client->adapter, msgs, 1);
	if (ret > 0)
		ret = 0;
	return ret;
}

/* simple check to see wether we have a isl1208 */
static int
isl1208_i2c_validate_client(struct i2c_client *client)
{
	u8 regs[ISL1208_RTC_SECTION_LEN] = { 0, };
	u8 zero_mask[ISL1208_RTC_SECTION_LEN] = {
		0x80, 0x80, 0x40, 0xc0, 0xe0, 0x00, 0xf8
	};
	int i;
	int ret;

	ret = isl1208_i2c_read_regs(client, 0, regs, ISL1208_RTC_SECTION_LEN);
	if (ret < 0)
		return ret;

	for (i = 0; i < ISL1208_RTC_SECTION_LEN; ++i) {
		if (regs[i] & zero_mask[i])	/* check if bits are cleared */
			return -ENODEV;
	}

	return 0;
}

static int
isl1208_i2c_get_sr(struct i2c_client *client)
{
	int sr = i2c_smbus_read_byte_data(client, ISL1208_REG_SR);
	if (sr < 0)
		return -EIO;

	return sr;
}

static int
isl1208_i2c_get_atr(struct i2c_client *client)
{
	int atr = i2c_smbus_read_byte_data(client, ISL1208_REG_ATR);
	if (atr < 0)
		return atr;

	/* The 6bit value in the ATR register controls the load
	 * capacitance C_load * in steps of 0.25pF
	 *
	 * bit (1<<5) of the ATR register is inverted
	 *
	 * C_load(ATR=0x20) =  4.50pF
	 * C_load(ATR=0x00) = 12.50pF
	 * C_load(ATR=0x1f) = 20.25pF
	 *
	 */

	atr &= 0x3f;		/* mask out lsb */
	atr ^= 1 << 5;		/* invert 6th bit */
	atr += 2 * 9;		/* add offset of 4.5pF; unit[atr] = 0.25pF */

	return atr;
}

static int
isl1208_i2c_get_dtr(struct i2c_client *client)
{
	int dtr = i2c_smbus_read_byte_data(client, ISL1208_REG_DTR);
	if (dtr < 0)
		return -EIO;

	/* dtr encodes adjustments of {-60,-40,-20,0,20,40,60} ppm */
	dtr = ((dtr & 0x3) * 20) * (dtr & (1 << 2) ? -1 : 1);

	return dtr;
}

static int
isl1208_i2c_get_usr(struct i2c_client *client)
{
	u8 buf[ISL1208_USR_SECTION_LEN] = { 0, };
	int ret;

	ret = isl1208_i2c_read_regs(client, ISL1208_REG_USR1, buf,
				    ISL1208_USR_SECTION_LEN);
	if (ret < 0)
		return ret;

	return (buf[1] << 8) | buf[0];
}

static int
isl1208_i2c_set_usr(struct i2c_client *client, u16 usr)
{
	u8 buf[ISL1208_USR_SECTION_LEN];

	buf[0] = usr & 0xff;
	buf[1] = (usr >> 8) & 0xff;

	return isl1208_i2c_set_regs(client, ISL1208_REG_USR1, buf,
				    ISL1208_USR_SECTION_LEN);
}

static int
isl1208_rtc_toggle_alarm(struct i2c_client *client, int enable)
{
	int icr = i2c_smbus_read_byte_data(client, ISL1208_REG_INT);

	if (icr < 0) {
		dev_err(&client->dev, "%s: reading INT failed\n", __func__);
		return icr;
	}

	if (enable)
		icr |= ISL1208_REG_INT_ALME | ISL1208_REG_INT_IM;
	else
		icr &= ~(ISL1208_REG_INT_ALME | ISL1208_REG_INT_IM);

	icr = i2c_smbus_write_byte_data(client, ISL1208_REG_INT, icr);
	if (icr < 0) {
		dev_err(&client->dev, "%s: writing INT failed\n", __func__);
		return icr;
	}

	return 0;
}

static int
isl1208_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct i2c_client *const client = to_i2c_client(dev);
	int sr, dtr, atr, usr;

	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return sr;
	}

	seq_printf(seq, "status_reg\t:%s%s%s%s%s%s (0x%.2x)\n",
		   (sr & ISL1208_REG_SR_RTCF) ? " RTCF" : "",
		   (sr & ISL1208_REG_SR_BAT) ? " BAT" : "",
		   (sr & ISL1208_REG_SR_ALM) ? " ALM" : "",
		   (sr & ISL1208_REG_SR_WRTC) ? " WRTC" : "",
		   (sr & ISL1208_REG_SR_XTOSCB) ? " XTOSCB" : "",
		   (sr & ISL1208_REG_SR_ARST) ? " ARST" : "", sr);

	seq_printf(seq, "batt_status\t: %s\n",
		   (sr & ISL1208_REG_SR_RTCF) ? "bad" : "okay");

	dtr = isl1208_i2c_get_dtr(client);
	if (dtr >= 0 - 1)
		seq_printf(seq, "digital_trim\t: %d ppm\n", dtr);

	atr = isl1208_i2c_get_atr(client);
	if (atr >= 0)
		seq_printf(seq, "analog_trim\t: %d.%.2d pF\n",
			   atr >> 2, (atr & 0x3) * 25);

	usr = isl1208_i2c_get_usr(client);
	if (usr >= 0)
		seq_printf(seq, "user_data\t: 0x%.4x\n", usr);

	return 0;
}

static int
isl1208_i2c_read_time(struct i2c_client *client, struct rtc_time *tm)
{
	int sr;
	u8 regs[ISL1208_RTC_SECTION_LEN] = { 0, };

	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}

	sr = isl1208_i2c_read_regs(client, 0, regs, ISL1208_RTC_SECTION_LEN);
	if (sr < 0) {
		dev_err(&client->dev, "%s: reading RTC section failed\n",
			__func__);
		return sr;
	}

	tm->tm_sec = bcd2bin(regs[ISL1208_REG_SC]);
	tm->tm_min = bcd2bin(regs[ISL1208_REG_MN]);

	/* HR field has a more complex interpretation */
	{
		const u8 _hr = regs[ISL1208_REG_HR];
		if (_hr & ISL1208_REG_HR_MIL)	/* 24h format */
			tm->tm_hour = bcd2bin(_hr & 0x3f);
		else {
			/* 12h format */
			tm->tm_hour = bcd2bin(_hr & 0x1f);
			if (_hr & ISL1208_REG_HR_PM)	/* PM flag set */
				tm->tm_hour += 12;
		}
	}

	tm->tm_mday = bcd2bin(regs[ISL1208_REG_DT]);
	tm->tm_mon = bcd2bin(regs[ISL1208_REG_MO]) - 1;	/* rtc starts at 1 */
	tm->tm_year = bcd2bin(regs[ISL1208_REG_YR]) + 100;
	tm->tm_wday = bcd2bin(regs[ISL1208_REG_DW]);

	return 0;
}

static int
isl1208_i2c_read_alarm(struct i2c_client *client, struct rtc_wkalrm *alarm)
{
	struct rtc_time *const tm = &alarm->time;
	u8 regs[ISL1208_ALARM_SECTION_LEN] = { 0, };
	int icr, yr, sr = isl1208_i2c_get_sr(client);

	if (sr < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return sr;
	}

	sr = isl1208_i2c_read_regs(client, ISL1208_REG_SCA, regs,
				   ISL1208_ALARM_SECTION_LEN);
	if (sr < 0) {
		dev_err(&client->dev, "%s: reading alarm section failed\n",
			__func__);
		return sr;
	}

	/* MSB of each alarm register is an enable bit */
	tm->tm_sec = bcd2bin(regs[ISL1208_REG_SCA - ISL1208_REG_SCA] & 0x7f);
	tm->tm_min = bcd2bin(regs[ISL1208_REG_MNA - ISL1208_REG_SCA] & 0x7f);
	tm->tm_hour = bcd2bin(regs[ISL1208_REG_HRA - ISL1208_REG_SCA] & 0x3f);
	tm->tm_mday = bcd2bin(regs[ISL1208_REG_DTA - ISL1208_REG_SCA] & 0x3f);
	tm->tm_mon =
		bcd2bin(regs[ISL1208_REG_MOA - ISL1208_REG_SCA] & 0x1f) - 1;
	tm->tm_wday = bcd2bin(regs[ISL1208_REG_DWA - ISL1208_REG_SCA] & 0x03);

	/* The alarm doesn't store the year so get it from the rtc section */
	yr = i2c_smbus_read_byte_data(client, ISL1208_REG_YR);
	if (yr < 0) {
		dev_err(&client->dev, "%s: reading RTC YR failed\n", __func__);
		return yr;
	}
	tm->tm_year = bcd2bin(yr) + 100;

	icr = i2c_smbus_read_byte_data(client, ISL1208_REG_INT);
	if (icr < 0) {
		dev_err(&client->dev, "%s: reading INT failed\n", __func__);
		return icr;
	}
	alarm->enabled = !!(icr & ISL1208_REG_INT_ALME);

	return 0;
}

static int
isl1208_i2c_set_alarm(struct i2c_client *client, struct rtc_wkalrm *alarm)
{
	struct rtc_time *alarm_tm = &alarm->time;
	u8 regs[ISL1208_ALARM_SECTION_LEN] = { 0, };
	const int offs = ISL1208_REG_SCA;
	unsigned long rtc_secs, alarm_secs;
	struct rtc_time rtc_tm;
	int err, enable;

	err = isl1208_i2c_read_time(client, &rtc_tm);
	if (err)
		return err;
	err = rtc_tm_to_time(&rtc_tm, &rtc_secs);
	if (err)
		return err;
	err = rtc_tm_to_time(alarm_tm, &alarm_secs);
	if (err)
		return err;

	/* If the alarm time is before the current time disable the alarm */
	if (!alarm->enabled || alarm_secs <= rtc_secs)
		enable = 0x00;
	else
		enable = 0x80;

	/* Program the alarm and enable it for each setting */
	regs[ISL1208_REG_SCA - offs] = bin2bcd(alarm_tm->tm_sec) | enable;
	regs[ISL1208_REG_MNA - offs] = bin2bcd(alarm_tm->tm_min) | enable;
	regs[ISL1208_REG_HRA - offs] = bin2bcd(alarm_tm->tm_hour) |
		ISL1208_REG_HR_MIL | enable;

	regs[ISL1208_REG_DTA - offs] = bin2bcd(alarm_tm->tm_mday) | enable;
	regs[ISL1208_REG_MOA - offs] = bin2bcd(alarm_tm->tm_mon + 1) | enable;
	regs[ISL1208_REG_DWA - offs] = bin2bcd(alarm_tm->tm_wday & 7) | enable;

	/* write ALARM registers */
	err = isl1208_i2c_set_regs(client, offs, regs,
				  ISL1208_ALARM_SECTION_LEN);
	if (err < 0) {
		dev_err(&client->dev, "%s: writing ALARM section failed\n",
			__func__);
		return err;
	}

	err = isl1208_rtc_toggle_alarm(client, enable);
	if (err)
		return err;

	return 0;
}

static int
isl1208_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return isl1208_i2c_read_time(to_i2c_client(dev), tm);
}

static int
isl1208_i2c_set_time(struct i2c_client *client, struct rtc_time const *tm)
{
	int sr;
	u8 regs[ISL1208_RTC_SECTION_LEN] = { 0, };

	/* The clock has an 8 bit wide bcd-coded register (they never learn)
	 * for the year. tm_year is an offset from 1900 and we are interested
	 * in the 2000-2099 range, so any value less than 100 is invalid.
	 */
	if (tm->tm_year < 100)
		return -EINVAL;

	regs[ISL1208_REG_SC] = bin2bcd(tm->tm_sec);
	regs[ISL1208_REG_MN] = bin2bcd(tm->tm_min);
	regs[ISL1208_REG_HR] = bin2bcd(tm->tm_hour) | ISL1208_REG_HR_MIL;

	regs[ISL1208_REG_DT] = bin2bcd(tm->tm_mday);
	regs[ISL1208_REG_MO] = bin2bcd(tm->tm_mon + 1);
	regs[ISL1208_REG_YR] = bin2bcd(tm->tm_year - 100);

	regs[ISL1208_REG_DW] = bin2bcd(tm->tm_wday & 7);

	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return sr;
	}

	/* set WRTC */
	sr = i2c_smbus_write_byte_data(client, ISL1208_REG_SR,
				       sr | ISL1208_REG_SR_WRTC);
	if (sr < 0) {
		dev_err(&client->dev, "%s: writing SR failed\n", __func__);
		return sr;
	}

	/* write RTC registers */
	sr = isl1208_i2c_set_regs(client, 0, regs, ISL1208_RTC_SECTION_LEN);
	if (sr < 0) {
		dev_err(&client->dev, "%s: writing RTC section failed\n",
			__func__);
		return sr;
	}

	/* clear WRTC again */
	sr = i2c_smbus_write_byte_data(client, ISL1208_REG_SR,
				       sr & ~ISL1208_REG_SR_WRTC);
	if (sr < 0) {
		dev_err(&client->dev, "%s: writing SR failed\n", __func__);
		return sr;
	}

	return 0;
}


static int
isl1208_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return isl1208_i2c_set_time(to_i2c_client(dev), tm);
}

static int
isl1208_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	return isl1208_i2c_read_alarm(to_i2c_client(dev), alarm);
}

static int
isl1208_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	return isl1208_i2c_set_alarm(to_i2c_client(dev), alarm);
}

static irqreturn_t
isl1208_rtc_interrupt(int irq, void *data)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	struct i2c_client *client = data;
	struct rtc_device *rtc = i2c_get_clientdata(client);
	int handled = 0, sr, err;

	/*
	 * I2C reads get NAK'ed if we read straight away after an interrupt?
	 * Using a mdelay/msleep didn't seem to help either, so we work around
	 * this by continually trying to read the register for a short time.
	 */
	while (1) {
		sr = isl1208_i2c_get_sr(client);
		if (sr >= 0)
			break;

		if (time_after(jiffies, timeout)) {
			dev_err(&client->dev, "%s: reading SR failed\n",
				__func__);
			return sr;
		}
	}

	if (sr & ISL1208_REG_SR_ALM) {
		dev_dbg(&client->dev, "alarm!\n");

		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);

		/* Clear the alarm */
		sr &= ~ISL1208_REG_SR_ALM;
		sr = i2c_smbus_write_byte_data(client, ISL1208_REG_SR, sr);
		if (sr < 0)
			dev_err(&client->dev, "%s: writing SR failed\n",
				__func__);
		else
			handled = 1;

		/* Disable the alarm */
		err = isl1208_rtc_toggle_alarm(client, 0);
		if (err)
			return err;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static const struct rtc_class_ops isl1208_rtc_ops = {
	.proc = isl1208_rtc_proc,
	.read_time = isl1208_rtc_read_time,
	.set_time = isl1208_rtc_set_time,
	.read_alarm = isl1208_rtc_read_alarm,
	.set_alarm = isl1208_rtc_set_alarm,
};

/* sysfs interface */

static ssize_t
isl1208_sysfs_show_atrim(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	int atr = isl1208_i2c_get_atr(to_i2c_client(dev));
	if (atr < 0)
		return atr;

	return sprintf(buf, "%d.%.2d pF\n", atr >> 2, (atr & 0x3) * 25);
}

static DEVICE_ATTR(atrim, S_IRUGO, isl1208_sysfs_show_atrim, NULL);

static ssize_t
isl1208_sysfs_show_dtrim(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	int dtr = isl1208_i2c_get_dtr(to_i2c_client(dev));
	if (dtr < 0)
		return dtr;

	return sprintf(buf, "%d ppm\n", dtr);
}

static DEVICE_ATTR(dtrim, S_IRUGO, isl1208_sysfs_show_dtrim, NULL);

static ssize_t
isl1208_sysfs_show_usr(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	int usr = isl1208_i2c_get_usr(to_i2c_client(dev));
	if (usr < 0)
		return usr;

	return sprintf(buf, "0x%.4x\n", usr);
}

static ssize_t
isl1208_sysfs_store_usr(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int usr = -1;

	if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
		if (sscanf(buf, "%x", &usr) != 1)
			return -EINVAL;
	} else {
		if (sscanf(buf, "%d", &usr) != 1)
			return -EINVAL;
	}

	if (usr < 0 || usr > 0xffff)
		return -EINVAL;

	return isl1208_i2c_set_usr(to_i2c_client(dev), usr) ? -EIO : count;
}

static DEVICE_ATTR(usr, S_IRUGO | S_IWUSR, isl1208_sysfs_show_usr,
		   isl1208_sysfs_store_usr);

static struct attribute *isl1208_rtc_attrs[] = {
	&dev_attr_atrim.attr,
	&dev_attr_dtrim.attr,
	&dev_attr_usr.attr,
	NULL
};

static const struct attribute_group isl1208_rtc_sysfs_files = {
	.attrs	= isl1208_rtc_attrs,
};

static int
isl1208_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = 0;
	struct rtc_device *rtc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	if (isl1208_i2c_validate_client(client) < 0)
		return -ENODEV;

	dev_info(&client->dev,
		 "chip found, driver version " DRV_VERSION "\n");

	if (client->irq > 0) {
		rc = request_threaded_irq(client->irq, NULL,
					  isl1208_rtc_interrupt,
					  IRQF_SHARED,
					  isl1208_driver.driver.name, client);
		if (!rc) {
			device_init_wakeup(&client->dev, 1);
			enable_irq_wake(client->irq);
		} else {
			dev_err(&client->dev,
				"Unable to request irq %d, no alarm support\n",
				client->irq);
			client->irq = 0;
		}
	}

	rtc = rtc_device_register(isl1208_driver.driver.name,
				  &client->dev, &isl1208_rtc_ops,
				  THIS_MODULE);
	if (IS_ERR(rtc)) {
		rc = PTR_ERR(rtc);
		goto exit_free_irq;
	}

	i2c_set_clientdata(client, rtc);

	rc = isl1208_i2c_get_sr(client);
	if (rc < 0) {
		dev_err(&client->dev, "reading status failed\n");
		goto exit_unregister;
	}

	if (rc & ISL1208_REG_SR_RTCF)
		dev_warn(&client->dev, "rtc power failure detected, "
			 "please set clock.\n");

	rc = sysfs_create_group(&client->dev.kobj, &isl1208_rtc_sysfs_files);
	if (rc)
		goto exit_unregister;

	return 0;

exit_unregister:
	rtc_device_unregister(rtc);
exit_free_irq:
	if (client->irq)
		free_irq(client->irq, client);

	return rc;
}

static int
isl1208_remove(struct i2c_client *client)
{
	struct rtc_device *rtc = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &isl1208_rtc_sysfs_files);
	rtc_device_unregister(rtc);
	if (client->irq)
		free_irq(client->irq, client);

	return 0;
}

static const struct i2c_device_id isl1208_id[] = {
	{ "isl1208", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl1208_id);

static struct i2c_driver isl1208_driver = {
	.driver = {
		   .name = "rtc-isl1208",
		   },
	.probe = isl1208_probe,
	.remove = isl1208_remove,
	.id_table = isl1208_id,
};

module_i2c_driver(isl1208_driver);

MODULE_AUTHOR("Herbert Valerio Riedel <hvr@gnu.org>");
MODULE_DESCRIPTION("Intersil ISL1208 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
