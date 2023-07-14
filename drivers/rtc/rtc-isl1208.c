// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Intersil ISL1208 rtc class driver
 *
 * Copyright 2005,2006 Hebert Valerio Riedel <hvr@gnu.org>
 */

#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/rtc.h>

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
#define ISL1208_REG_SR_EVT     (1<<3)	/* event */
#define ISL1208_REG_SR_ALM     (1<<2)	/* alarm */
#define ISL1208_REG_SR_BAT     (1<<1)	/* battery */
#define ISL1208_REG_SR_RTCF    (1<<0)	/* rtc fail */
#define ISL1208_REG_INT 0x08
#define ISL1208_REG_INT_ALME   (1<<6)   /* alarm enable */
#define ISL1208_REG_INT_IM     (1<<7)   /* interrupt/alarm mode */
#define ISL1219_REG_EV  0x09
#define ISL1219_REG_EV_EVEN    (1<<4)   /* event detection enable */
#define ISL1219_REG_EV_EVIENB  (1<<7)   /* event in pull-up disable */
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

/* event section */
#define ISL1219_REG_SCT 0x14
#define ISL1219_REG_MNT 0x15
#define ISL1219_REG_HRT 0x16
#define ISL1219_REG_DTT 0x17
#define ISL1219_REG_MOT 0x18
#define ISL1219_REG_YRT 0x19
#define ISL1219_EVT_SECTION_LEN 6

static struct i2c_driver isl1208_driver;

/* Chip capabilities table */
struct isl1208_config {
	unsigned int	nvmem_length;
	unsigned	has_tamper:1;
	unsigned	has_timestamp:1;
	unsigned	has_inverted_osc_bit:1;
};

static const struct isl1208_config config_isl1208 = {
	.nvmem_length = 2,
	.has_tamper = false,
	.has_timestamp = false
};

static const struct isl1208_config config_isl1209 = {
	.nvmem_length = 2,
	.has_tamper = true,
	.has_timestamp = false
};

static const struct isl1208_config config_isl1218 = {
	.nvmem_length = 8,
	.has_tamper = false,
	.has_timestamp = false
};

static const struct isl1208_config config_isl1219 = {
	.nvmem_length = 2,
	.has_tamper = true,
	.has_timestamp = true
};

static const struct isl1208_config config_raa215300_a0 = {
	.nvmem_length = 2,
	.has_tamper = false,
	.has_timestamp = false,
	.has_inverted_osc_bit = true
};

static const struct i2c_device_id isl1208_id[] = {
	{ "isl1208", .driver_data = (kernel_ulong_t)&config_isl1208 },
	{ "isl1209", .driver_data = (kernel_ulong_t)&config_isl1209 },
	{ "isl1218", .driver_data = (kernel_ulong_t)&config_isl1218 },
	{ "isl1219", .driver_data = (kernel_ulong_t)&config_isl1219 },
	{ "raa215300_a0", .driver_data = (kernel_ulong_t)&config_raa215300_a0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl1208_id);

static const __maybe_unused struct of_device_id isl1208_of_match[] = {
	{ .compatible = "isil,isl1208", .data = &config_isl1208 },
	{ .compatible = "isil,isl1209", .data = &config_isl1209 },
	{ .compatible = "isil,isl1218", .data = &config_isl1218 },
	{ .compatible = "isil,isl1219", .data = &config_isl1219 },
	{ }
};
MODULE_DEVICE_TABLE(of, isl1208_of_match);

/* Device state */
struct isl1208_state {
	struct nvmem_config nvmem_config;
	struct rtc_device *rtc;
	const struct isl1208_config *config;
};

/* block read */
static int
isl1208_i2c_read_regs(struct i2c_client *client, u8 reg, u8 buf[],
		      unsigned len)
{
	int ret;

	WARN_ON(reg > ISL1219_REG_YRT);
	WARN_ON(reg + len > ISL1219_REG_YRT + 1);

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);
	return (ret < 0) ? ret : 0;
}

/* block write */
static int
isl1208_i2c_set_regs(struct i2c_client *client, u8 reg, u8 const buf[],
		     unsigned len)
{
	int ret;

	WARN_ON(reg > ISL1219_REG_YRT);
	WARN_ON(reg + len > ISL1219_REG_YRT + 1);

	ret = i2c_smbus_write_i2c_block_data(client, reg, len, buf);
	return (ret < 0) ? ret : 0;
}

/* simple check to see whether we have a isl1208 */
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

static int isl1208_set_xtoscb(struct i2c_client *client, int sr, int xtosb_val)
{
	/* Do nothing if bit is already set to desired value */
	if ((sr & ISL1208_REG_SR_XTOSCB) == xtosb_val)
		return 0;

	if (xtosb_val)
		sr |= ISL1208_REG_SR_XTOSCB;
	else
		sr &= ~ISL1208_REG_SR_XTOSCB;

	return i2c_smbus_write_byte_data(client, ISL1208_REG_SR, sr);
}

static int
isl1208_i2c_get_sr(struct i2c_client *client)
{
	return i2c_smbus_read_byte_data(client, ISL1208_REG_SR);
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

/* returns adjustment value + 100 */
static int
isl1208_i2c_get_dtr(struct i2c_client *client)
{
	int dtr = i2c_smbus_read_byte_data(client, ISL1208_REG_DTR);
	if (dtr < 0)
		return -EIO;

	/* dtr encodes adjustments of {-60,-40,-20,0,20,40,60} ppm */
	dtr = ((dtr & 0x3) * 20) * (dtr & (1 << 2) ? -1 : 1);

	return dtr + 100;
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
	if (dtr >= 0)
		seq_printf(seq, "digital_trim\t: %d ppm\n", dtr - 100);

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
	struct rtc_time rtc_tm;
	int err, enable;

	err = isl1208_i2c_read_time(client, &rtc_tm);
	if (err)
		return err;

	/* If the alarm time is before the current time disable the alarm */
	if (!alarm->enabled || rtc_tm_sub(alarm_tm, &rtc_tm) <= 0)
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
	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(&client->dev, "%s: reading SR failed\n", __func__);
		return sr;
	}
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

static ssize_t timestamp0_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	int sr;

	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return sr;
	}

	sr &= ~ISL1208_REG_SR_EVT;

	sr = i2c_smbus_write_byte_data(client, ISL1208_REG_SR, sr);
	if (sr < 0)
		dev_err(dev, "%s: writing SR failed\n",
			__func__);

	return count;
};

static ssize_t timestamp0_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	u8 regs[ISL1219_EVT_SECTION_LEN] = { 0, };
	struct rtc_time tm;
	int sr;

	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return sr;
	}

	if (!(sr & ISL1208_REG_SR_EVT))
		return 0;

	sr = isl1208_i2c_read_regs(client, ISL1219_REG_SCT, regs,
				   ISL1219_EVT_SECTION_LEN);
	if (sr < 0) {
		dev_err(dev, "%s: reading event section failed\n",
			__func__);
		return 0;
	}

	/* MSB of each alarm register is an enable bit */
	tm.tm_sec = bcd2bin(regs[ISL1219_REG_SCT - ISL1219_REG_SCT] & 0x7f);
	tm.tm_min = bcd2bin(regs[ISL1219_REG_MNT - ISL1219_REG_SCT] & 0x7f);
	tm.tm_hour = bcd2bin(regs[ISL1219_REG_HRT - ISL1219_REG_SCT] & 0x3f);
	tm.tm_mday = bcd2bin(regs[ISL1219_REG_DTT - ISL1219_REG_SCT] & 0x3f);
	tm.tm_mon =
		bcd2bin(regs[ISL1219_REG_MOT - ISL1219_REG_SCT] & 0x1f) - 1;
	tm.tm_year = bcd2bin(regs[ISL1219_REG_YRT - ISL1219_REG_SCT]) + 100;

	sr = rtc_valid_tm(&tm);
	if (sr)
		return sr;

	return sprintf(buf, "%llu\n",
				(unsigned long long)rtc_tm_to_time64(&tm));
};

static DEVICE_ATTR_RW(timestamp0);

static irqreturn_t
isl1208_rtc_interrupt(int irq, void *data)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	struct i2c_client *client = data;
	struct isl1208_state *isl1208 = i2c_get_clientdata(client);
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

		rtc_update_irq(isl1208->rtc, 1, RTC_IRQF | RTC_AF);

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

	if (isl1208->config->has_tamper && (sr & ISL1208_REG_SR_EVT)) {
		dev_warn(&client->dev, "event detected");
		handled = 1;
		if (isl1208->config->has_timestamp)
			sysfs_notify(&isl1208->rtc->dev.kobj, NULL,
				     dev_attr_timestamp0.attr.name);
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
	int atr = isl1208_i2c_get_atr(to_i2c_client(dev->parent));
	if (atr < 0)
		return atr;

	return sprintf(buf, "%d.%.2d pF\n", atr >> 2, (atr & 0x3) * 25);
}

static DEVICE_ATTR(atrim, S_IRUGO, isl1208_sysfs_show_atrim, NULL);

static ssize_t
isl1208_sysfs_show_dtrim(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	int dtr = isl1208_i2c_get_dtr(to_i2c_client(dev->parent));
	if (dtr < 0)
		return dtr;

	return sprintf(buf, "%d ppm\n", dtr - 100);
}

static DEVICE_ATTR(dtrim, S_IRUGO, isl1208_sysfs_show_dtrim, NULL);

static ssize_t
isl1208_sysfs_show_usr(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	int usr = isl1208_i2c_get_usr(to_i2c_client(dev->parent));
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

	if (isl1208_i2c_set_usr(to_i2c_client(dev->parent), usr))
		return -EIO;

	return count;
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

static struct attribute *isl1219_rtc_attrs[] = {
	&dev_attr_timestamp0.attr,
	NULL
};

static const struct attribute_group isl1219_rtc_sysfs_files = {
	.attrs	= isl1219_rtc_attrs,
};

static int isl1208_nvmem_read(void *priv, unsigned int off, void *buf,
			      size_t count)
{
	struct isl1208_state *isl1208 = priv;
	struct i2c_client *client = to_i2c_client(isl1208->rtc->dev.parent);
	int ret;

	/* nvmem sanitizes offset/count for us, but count==0 is possible */
	if (!count)
		return count;
	ret = isl1208_i2c_read_regs(client, ISL1208_REG_USR1 + off, buf,
				    count);
	return ret == 0 ? count : ret;
}

static int isl1208_nvmem_write(void *priv, unsigned int off, void *buf,
			       size_t count)
{
	struct isl1208_state *isl1208 = priv;
	struct i2c_client *client = to_i2c_client(isl1208->rtc->dev.parent);
	int ret;

	/* nvmem sanitizes off/count for us, but count==0 is possible */
	if (!count)
		return count;
	ret = isl1208_i2c_set_regs(client, ISL1208_REG_USR1 + off, buf,
				   count);

	return ret == 0 ? count : ret;
}

static const struct nvmem_config isl1208_nvmem_config = {
	.name = "isl1208_nvram",
	.word_size = 1,
	.stride = 1,
	/* .size from chip specific config */
	.reg_read = isl1208_nvmem_read,
	.reg_write = isl1208_nvmem_write,
};

static int isl1208_setup_irq(struct i2c_client *client, int irq)
{
	int rc = devm_request_threaded_irq(&client->dev, irq, NULL,
					isl1208_rtc_interrupt,
					IRQF_SHARED | IRQF_ONESHOT,
					isl1208_driver.driver.name,
					client);
	if (!rc) {
		device_init_wakeup(&client->dev, 1);
		enable_irq_wake(irq);
	} else {
		dev_err(&client->dev,
			"Unable to request irq %d, no alarm support\n",
			irq);
	}
	return rc;
}

static int
isl1208_clk_present(struct i2c_client *client, const char *name)
{
	struct clk *clk;

	clk = devm_clk_get_optional(&client->dev, name);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return !!clk;
}

static int
isl1208_probe(struct i2c_client *client)
{
	struct isl1208_state *isl1208;
	int evdet_irq = -1;
	int xtosb_val = 0;
	int rc = 0;
	int sr;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	if (isl1208_i2c_validate_client(client) < 0)
		return -ENODEV;

	/* Allocate driver state, point i2c client data to it */
	isl1208 = devm_kzalloc(&client->dev, sizeof(*isl1208), GFP_KERNEL);
	if (!isl1208)
		return -ENOMEM;
	i2c_set_clientdata(client, isl1208);

	/* Determine which chip we have */
	if (client->dev.of_node) {
		isl1208->config = of_device_get_match_data(&client->dev);
		if (!isl1208->config)
			return -ENODEV;
	} else {
		const struct i2c_device_id *id = i2c_match_id(isl1208_id, client);

		if (!id)
			return -ENODEV;
		isl1208->config = (struct isl1208_config *)id->driver_data;
	}

	rc = isl1208_clk_present(client, "xin");
	if (rc < 0)
		return rc;

	if (!rc) {
		rc = isl1208_clk_present(client, "clkin");
		if (rc < 0)
			return rc;

		if (rc)
			xtosb_val = 1;
	}

	isl1208->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(isl1208->rtc))
		return PTR_ERR(isl1208->rtc);

	isl1208->rtc->ops = &isl1208_rtc_ops;

	/* Setup nvmem configuration in driver state struct */
	isl1208->nvmem_config = isl1208_nvmem_config;
	isl1208->nvmem_config.size = isl1208->config->nvmem_length;
	isl1208->nvmem_config.priv = isl1208;

	sr = isl1208_i2c_get_sr(client);
	if (sr < 0) {
		dev_err(&client->dev, "reading status failed\n");
		return sr;
	}

	if (isl1208->config->has_inverted_osc_bit)
		xtosb_val = !xtosb_val;

	rc = isl1208_set_xtoscb(client, sr, xtosb_val);
	if (rc)
		return rc;

	if (sr & ISL1208_REG_SR_RTCF)
		dev_warn(&client->dev, "rtc power failure detected, "
			 "please set clock.\n");

	if (isl1208->config->has_tamper) {
		struct device_node *np = client->dev.of_node;
		u32 evienb;

		rc = i2c_smbus_read_byte_data(client, ISL1219_REG_EV);
		if (rc < 0) {
			dev_err(&client->dev, "failed to read EV reg\n");
			return rc;
		}
		rc |= ISL1219_REG_EV_EVEN;
		if (!of_property_read_u32(np, "isil,ev-evienb", &evienb)) {
			if (evienb)
				rc |= ISL1219_REG_EV_EVIENB;
			else
				rc &= ~ISL1219_REG_EV_EVIENB;
		}
		rc = i2c_smbus_write_byte_data(client, ISL1219_REG_EV, rc);
		if (rc < 0) {
			dev_err(&client->dev, "could not enable tamper detection\n");
			return rc;
		}
		evdet_irq = of_irq_get_byname(np, "evdet");
	}
	if (isl1208->config->has_timestamp) {
		rc = rtc_add_group(isl1208->rtc, &isl1219_rtc_sysfs_files);
		if (rc)
			return rc;
	}

	rc = rtc_add_group(isl1208->rtc, &isl1208_rtc_sysfs_files);
	if (rc)
		return rc;

	if (client->irq > 0) {
		rc = isl1208_setup_irq(client, client->irq);
		if (rc)
			return rc;

	} else {
		clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, isl1208->rtc->features);
	}

	if (evdet_irq > 0 && evdet_irq != client->irq)
		rc = isl1208_setup_irq(client, evdet_irq);
	if (rc)
		return rc;

	rc = devm_rtc_nvmem_register(isl1208->rtc, &isl1208->nvmem_config);
	if (rc)
		return rc;

	return devm_rtc_register_device(isl1208->rtc);
}

static struct i2c_driver isl1208_driver = {
	.driver = {
		.name = "rtc-isl1208",
		.of_match_table = of_match_ptr(isl1208_of_match),
	},
	.probe = isl1208_probe,
	.id_table = isl1208_id,
};

module_i2c_driver(isl1208_driver);

MODULE_AUTHOR("Herbert Valerio Riedel <hvr@gnu.org>");
MODULE_DESCRIPTION("Intersil ISL1208 RTC driver");
MODULE_LICENSE("GPL");
