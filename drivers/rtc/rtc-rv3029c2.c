/*
 * Micro Crystal RV-3029C2 rtc class driver
 *
 * Author: Gregory Hermant <gregory.hermant@calao-systems.com>
 *
 * based on previously existing rtc class drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOTE: Currently this driver only supports the bare minimum for read
 * and write the RTC and alarms. The extra features provided by this chip
 * (trickle charger, eeprom, T° compensation) are unavailable.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>

/* Register map */
/* control section */
#define RV3029C2_ONOFF_CTRL		0x00
#define RV3029C2_IRQ_CTRL		0x01
#define RV3029C2_IRQ_CTRL_AIE		(1 << 0)
#define RV3029C2_IRQ_FLAGS		0x02
#define RV3029C2_IRQ_FLAGS_AF		(1 << 0)
#define RV3029C2_STATUS			0x03
#define RV3029C2_STATUS_VLOW1		(1 << 2)
#define RV3029C2_STATUS_VLOW2		(1 << 3)
#define RV3029C2_STATUS_SR		(1 << 4)
#define RV3029C2_STATUS_PON		(1 << 5)
#define RV3029C2_STATUS_EEBUSY		(1 << 7)
#define RV3029C2_RST_CTRL		0x04
#define RV3029C2_CONTROL_SECTION_LEN	0x05

/* watch section */
#define RV3029C2_W_SEC			0x08
#define RV3029C2_W_MINUTES		0x09
#define RV3029C2_W_HOURS		0x0A
#define RV3029C2_REG_HR_12_24		(1<<6)  /* 24h/12h mode */
#define RV3029C2_REG_HR_PM		(1<<5)  /* PM/AM bit in 12h mode */
#define RV3029C2_W_DATE			0x0B
#define RV3029C2_W_DAYS			0x0C
#define RV3029C2_W_MONTHS		0x0D
#define RV3029C2_W_YEARS		0x0E
#define RV3029C2_WATCH_SECTION_LEN	0x07

/* alarm section */
#define RV3029C2_A_SC			0x10
#define RV3029C2_A_MN			0x11
#define RV3029C2_A_HR			0x12
#define RV3029C2_A_DT			0x13
#define RV3029C2_A_DW			0x14
#define RV3029C2_A_MO			0x15
#define RV3029C2_A_YR			0x16
#define RV3029C2_ALARM_SECTION_LEN	0x07

/* timer section */
#define RV3029C2_TIMER_LOW		0x18
#define RV3029C2_TIMER_HIGH		0x19

/* temperature section */
#define RV3029C2_TEMP_PAGE		0x20

/* eeprom data section */
#define RV3029C2_E2P_EEDATA1		0x28
#define RV3029C2_E2P_EEDATA2		0x29

/* eeprom control section */
#define RV3029C2_CONTROL_E2P_EECTRL	0x30
#define RV3029C2_TRICKLE_1K		(1<<0)  /*  1K resistance */
#define RV3029C2_TRICKLE_5K		(1<<1)  /*  5K resistance */
#define RV3029C2_TRICKLE_20K		(1<<2)  /* 20K resistance */
#define RV3029C2_TRICKLE_80K		(1<<3)  /* 80K resistance */
#define RV3029C2_CONTROL_E2P_XTALOFFSET	0x31
#define RV3029C2_CONTROL_E2P_QCOEF	0x32
#define RV3029C2_CONTROL_E2P_TURNOVER	0x33

/* user ram section */
#define RV3029C2_USR1_RAM_PAGE		0x38
#define RV3029C2_USR1_SECTION_LEN	0x04
#define RV3029C2_USR2_RAM_PAGE		0x3C
#define RV3029C2_USR2_SECTION_LEN	0x04

static int
rv3029c2_i2c_read_regs(struct i2c_client *client, u8 reg, u8 *buf,
	unsigned len)
{
	int ret;

	if ((reg > RV3029C2_USR1_RAM_PAGE + 7) ||
		(reg + len > RV3029C2_USR1_RAM_PAGE + 8))
		return -EINVAL;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);
	if (ret < 0)
		return ret;
	if (ret < len)
		return -EIO;
	return 0;
}

static int
rv3029c2_i2c_write_regs(struct i2c_client *client, u8 reg, u8 const buf[],
			unsigned len)
{
	if ((reg > RV3029C2_USR1_RAM_PAGE + 7) ||
		(reg + len > RV3029C2_USR1_RAM_PAGE + 8))
		return -EINVAL;

	return i2c_smbus_write_i2c_block_data(client, reg, len, buf);
}

static int
rv3029c2_i2c_get_sr(struct i2c_client *client, u8 *buf)
{
	int ret = rv3029c2_i2c_read_regs(client, RV3029C2_STATUS, buf, 1);

	if (ret < 0)
		return -EIO;
	dev_dbg(&client->dev, "status = 0x%.2x (%d)\n", buf[0], buf[0]);
	return 0;
}

static int
rv3029c2_i2c_set_sr(struct i2c_client *client, u8 val)
{
	u8 buf[1];
	int sr;

	buf[0] = val;
	sr = rv3029c2_i2c_write_regs(client, RV3029C2_STATUS, buf, 1);
	dev_dbg(&client->dev, "status = 0x%.2x (%d)\n", buf[0], buf[0]);
	if (sr < 0)
		return -EIO;
	return 0;
}

static int
rv3029c2_i2c_read_time(struct i2c_client *client, struct rtc_time *tm)
{
	u8 buf[1];
	int ret;
	u8 regs[RV3029C2_WATCH_SECTION_LEN] = { 0, };

	ret = rv3029c2_i2c_get_sr(client, buf);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}

	ret = rv3029c2_i2c_read_regs(client, RV3029C2_W_SEC , regs,
					RV3029C2_WATCH_SECTION_LEN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reading RTC section failed\n",
			__func__);
		return ret;
	}

	tm->tm_sec = bcd2bin(regs[RV3029C2_W_SEC-RV3029C2_W_SEC]);
	tm->tm_min = bcd2bin(regs[RV3029C2_W_MINUTES-RV3029C2_W_SEC]);

	/* HR field has a more complex interpretation */
	{
		const u8 _hr = regs[RV3029C2_W_HOURS-RV3029C2_W_SEC];
		if (_hr & RV3029C2_REG_HR_12_24) {
			/* 12h format */
			tm->tm_hour = bcd2bin(_hr & 0x1f);
			if (_hr & RV3029C2_REG_HR_PM)	/* PM flag set */
				tm->tm_hour += 12;
		} else /* 24h format */
			tm->tm_hour = bcd2bin(_hr & 0x3f);
	}

	tm->tm_mday = bcd2bin(regs[RV3029C2_W_DATE-RV3029C2_W_SEC]);
	tm->tm_mon = bcd2bin(regs[RV3029C2_W_MONTHS-RV3029C2_W_SEC]) - 1;
	tm->tm_year = bcd2bin(regs[RV3029C2_W_YEARS-RV3029C2_W_SEC]) + 100;
	tm->tm_wday = bcd2bin(regs[RV3029C2_W_DAYS-RV3029C2_W_SEC]) - 1;

	return 0;
}

static int rv3029c2_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return rv3029c2_i2c_read_time(to_i2c_client(dev), tm);
}

static int
rv3029c2_i2c_read_alarm(struct i2c_client *client, struct rtc_wkalrm *alarm)
{
	struct rtc_time *const tm = &alarm->time;
	int ret;
	u8 regs[8];

	ret = rv3029c2_i2c_get_sr(client, regs);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}

	ret = rv3029c2_i2c_read_regs(client, RV3029C2_A_SC, regs,
					RV3029C2_ALARM_SECTION_LEN);

	if (ret < 0) {
		dev_err(&client->dev, "%s: reading alarm section failed\n",
			__func__);
		return ret;
	}

	tm->tm_sec = bcd2bin(regs[RV3029C2_A_SC-RV3029C2_A_SC] & 0x7f);
	tm->tm_min = bcd2bin(regs[RV3029C2_A_MN-RV3029C2_A_SC] & 0x7f);
	tm->tm_hour = bcd2bin(regs[RV3029C2_A_HR-RV3029C2_A_SC] & 0x3f);
	tm->tm_mday = bcd2bin(regs[RV3029C2_A_DT-RV3029C2_A_SC] & 0x3f);
	tm->tm_mon = bcd2bin(regs[RV3029C2_A_MO-RV3029C2_A_SC] & 0x1f) - 1;
	tm->tm_year = bcd2bin(regs[RV3029C2_A_YR-RV3029C2_A_SC] & 0x7f) + 100;
	tm->tm_wday = bcd2bin(regs[RV3029C2_A_DW-RV3029C2_A_SC] & 0x07) - 1;

	return 0;
}

static int
rv3029c2_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	return rv3029c2_i2c_read_alarm(to_i2c_client(dev), alarm);
}

static int rv3029c2_rtc_i2c_alarm_set_irq(struct i2c_client *client,
					int enable)
{
	int ret;
	u8 buf[1];

	/* enable AIE irq */
	ret = rv3029c2_i2c_read_regs(client, RV3029C2_IRQ_CTRL,	buf, 1);
	if (ret < 0) {
		dev_err(&client->dev, "can't read INT reg\n");
		return ret;
	}
	if (enable)
		buf[0] |= RV3029C2_IRQ_CTRL_AIE;
	else
		buf[0] &= ~RV3029C2_IRQ_CTRL_AIE;

	ret = rv3029c2_i2c_write_regs(client, RV3029C2_IRQ_CTRL, buf, 1);
	if (ret < 0) {
		dev_err(&client->dev, "can't set INT reg\n");
		return ret;
	}

	return 0;
}

static int rv3029c2_rtc_i2c_set_alarm(struct i2c_client *client,
					struct rtc_wkalrm *alarm)
{
	struct rtc_time *const tm = &alarm->time;
	int ret;
	u8 regs[8];

	/*
	 * The clock has an 8 bit wide bcd-coded register (they never learn)
	 * for the year. tm_year is an offset from 1900 and we are interested
	 * in the 2000-2099 range, so any value less than 100 is invalid.
	*/
	if (tm->tm_year < 100)
		return -EINVAL;

	ret = rv3029c2_i2c_get_sr(client, regs);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}
	regs[RV3029C2_A_SC-RV3029C2_A_SC] = bin2bcd(tm->tm_sec & 0x7f);
	regs[RV3029C2_A_MN-RV3029C2_A_SC] = bin2bcd(tm->tm_min & 0x7f);
	regs[RV3029C2_A_HR-RV3029C2_A_SC] = bin2bcd(tm->tm_hour & 0x3f);
	regs[RV3029C2_A_DT-RV3029C2_A_SC] = bin2bcd(tm->tm_mday & 0x3f);
	regs[RV3029C2_A_MO-RV3029C2_A_SC] = bin2bcd((tm->tm_mon & 0x1f) - 1);
	regs[RV3029C2_A_DW-RV3029C2_A_SC] = bin2bcd((tm->tm_wday & 7) - 1);
	regs[RV3029C2_A_YR-RV3029C2_A_SC] = bin2bcd((tm->tm_year & 0x7f) - 100);

	ret = rv3029c2_i2c_write_regs(client, RV3029C2_A_SC, regs,
					RV3029C2_ALARM_SECTION_LEN);
	if (ret < 0)
		return ret;

	if (alarm->enabled) {
		u8 buf[1];

		/* clear AF flag */
		ret = rv3029c2_i2c_read_regs(client, RV3029C2_IRQ_FLAGS,
						buf, 1);
		if (ret < 0) {
			dev_err(&client->dev, "can't read alarm flag\n");
			return ret;
		}
		buf[0] &= ~RV3029C2_IRQ_FLAGS_AF;
		ret = rv3029c2_i2c_write_regs(client, RV3029C2_IRQ_FLAGS,
						buf, 1);
		if (ret < 0) {
			dev_err(&client->dev, "can't set alarm flag\n");
			return ret;
		}
		/* enable AIE irq */
		ret = rv3029c2_rtc_i2c_alarm_set_irq(client, 1);
		if (ret)
			return ret;

		dev_dbg(&client->dev, "alarm IRQ armed\n");
	} else {
		/* disable AIE irq */
		ret = rv3029c2_rtc_i2c_alarm_set_irq(client, 1);
		if (ret)
			return ret;

		dev_dbg(&client->dev, "alarm IRQ disabled\n");
	}

	return 0;
}

static int rv3029c2_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	return rv3029c2_rtc_i2c_set_alarm(to_i2c_client(dev), alarm);
}

static int
rv3029c2_i2c_set_time(struct i2c_client *client, struct rtc_time const *tm)
{
	u8 regs[8];
	int ret;

	/*
	 * The clock has an 8 bit wide bcd-coded register (they never learn)
	 * for the year. tm_year is an offset from 1900 and we are interested
	 * in the 2000-2099 range, so any value less than 100 is invalid.
	*/
	if (tm->tm_year < 100)
		return -EINVAL;

	regs[RV3029C2_W_SEC-RV3029C2_W_SEC] = bin2bcd(tm->tm_sec);
	regs[RV3029C2_W_MINUTES-RV3029C2_W_SEC] = bin2bcd(tm->tm_min);
	regs[RV3029C2_W_HOURS-RV3029C2_W_SEC] = bin2bcd(tm->tm_hour);
	regs[RV3029C2_W_DATE-RV3029C2_W_SEC] = bin2bcd(tm->tm_mday);
	regs[RV3029C2_W_MONTHS-RV3029C2_W_SEC] = bin2bcd(tm->tm_mon+1);
	regs[RV3029C2_W_DAYS-RV3029C2_W_SEC] = bin2bcd((tm->tm_wday & 7)+1);
	regs[RV3029C2_W_YEARS-RV3029C2_W_SEC] = bin2bcd(tm->tm_year - 100);

	ret = rv3029c2_i2c_write_regs(client, RV3029C2_W_SEC, regs,
					RV3029C2_WATCH_SECTION_LEN);
	if (ret < 0)
		return ret;

	ret = rv3029c2_i2c_get_sr(client, regs);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return ret;
	}
	/* clear PON bit */
	ret = rv3029c2_i2c_set_sr(client, (regs[0] & ~RV3029C2_STATUS_PON));
	if (ret < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return ret;
	}

	return 0;
}

static int rv3029c2_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return rv3029c2_i2c_set_time(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops rv3029c2_rtc_ops = {
	.read_time	= rv3029c2_rtc_read_time,
	.set_time	= rv3029c2_rtc_set_time,
	.read_alarm	= rv3029c2_rtc_read_alarm,
	.set_alarm	= rv3029c2_rtc_set_alarm,
};

static struct i2c_device_id rv3029c2_id[] = {
	{ "rv3029c2", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rv3029c2_id);

static int rv3029c2_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct rtc_device *rtc;
	int rc = 0;
	u8 buf[1];

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_EMUL))
		return -ENODEV;

	rtc = rtc_device_register(client->name,
				&client->dev, &rv3029c2_rtc_ops,
				THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	rc = rv3029c2_i2c_get_sr(client, buf);
	if (rc < 0) {
		dev_err(&client->dev, "reading status failed\n");
		goto exit_unregister;
	}

	return 0;

exit_unregister:
	rtc_device_unregister(rtc);

	return rc;
}

static int rv3029c2_remove(struct i2c_client *client)
{
	struct rtc_device *rtc = i2c_get_clientdata(client);

	rtc_device_unregister(rtc);

	return 0;
}

static struct i2c_driver rv3029c2_driver = {
	.driver = {
		.name = "rtc-rv3029c2",
	},
	.probe = rv3029c2_probe,
	.remove = rv3029c2_remove,
	.id_table = rv3029c2_id,
};

module_i2c_driver(rv3029c2_driver);

MODULE_AUTHOR("Gregory Hermant <gregory.hermant@calao-systems.com>");
MODULE_DESCRIPTION("Micro Crystal RV3029C2 RTC driver");
MODULE_LICENSE("GPL");
