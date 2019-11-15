// SPDX-License-Identifier: GPL-2.0-only
/*
 * Micro Crystal RV-3029 / RV-3049 rtc class driver
 *
 * Author: Gregory Hermant <gregory.hermant@calao-systems.com>
 *         Michael Buesch <m@bues.ch>
 *
 * based on previously existing rtc class drivers
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/regmap.h>

/* Register map */
/* control section */
#define RV3029_ONOFF_CTRL		0x00
#define RV3029_ONOFF_CTRL_WE		BIT(0)
#define RV3029_ONOFF_CTRL_TE		BIT(1)
#define RV3029_ONOFF_CTRL_TAR		BIT(2)
#define RV3029_ONOFF_CTRL_EERE		BIT(3)
#define RV3029_ONOFF_CTRL_SRON		BIT(4)
#define RV3029_ONOFF_CTRL_TD0		BIT(5)
#define RV3029_ONOFF_CTRL_TD1		BIT(6)
#define RV3029_ONOFF_CTRL_CLKINT	BIT(7)
#define RV3029_IRQ_CTRL			0x01
#define RV3029_IRQ_CTRL_AIE		BIT(0)
#define RV3029_IRQ_CTRL_TIE		BIT(1)
#define RV3029_IRQ_CTRL_V1IE		BIT(2)
#define RV3029_IRQ_CTRL_V2IE		BIT(3)
#define RV3029_IRQ_CTRL_SRIE		BIT(4)
#define RV3029_IRQ_FLAGS		0x02
#define RV3029_IRQ_FLAGS_AF		BIT(0)
#define RV3029_IRQ_FLAGS_TF		BIT(1)
#define RV3029_IRQ_FLAGS_V1IF		BIT(2)
#define RV3029_IRQ_FLAGS_V2IF		BIT(3)
#define RV3029_IRQ_FLAGS_SRF		BIT(4)
#define RV3029_STATUS			0x03
#define RV3029_STATUS_VLOW1		BIT(2)
#define RV3029_STATUS_VLOW2		BIT(3)
#define RV3029_STATUS_SR		BIT(4)
#define RV3029_STATUS_PON		BIT(5)
#define RV3029_STATUS_EEBUSY		BIT(7)
#define RV3029_RST_CTRL			0x04
#define RV3029_RST_CTRL_SYSR		BIT(4)
#define RV3029_CONTROL_SECTION_LEN	0x05

/* watch section */
#define RV3029_W_SEC			0x08
#define RV3029_W_MINUTES		0x09
#define RV3029_W_HOURS			0x0A
#define RV3029_REG_HR_12_24		BIT(6) /* 24h/12h mode */
#define RV3029_REG_HR_PM		BIT(5) /* PM/AM bit in 12h mode */
#define RV3029_W_DATE			0x0B
#define RV3029_W_DAYS			0x0C
#define RV3029_W_MONTHS			0x0D
#define RV3029_W_YEARS			0x0E
#define RV3029_WATCH_SECTION_LEN	0x07

/* alarm section */
#define RV3029_A_SC			0x10
#define RV3029_A_MN			0x11
#define RV3029_A_HR			0x12
#define RV3029_A_DT			0x13
#define RV3029_A_DW			0x14
#define RV3029_A_MO			0x15
#define RV3029_A_YR			0x16
#define RV3029_A_AE_X			BIT(7)
#define RV3029_ALARM_SECTION_LEN	0x07

/* timer section */
#define RV3029_TIMER_LOW		0x18
#define RV3029_TIMER_HIGH		0x19

/* temperature section */
#define RV3029_TEMP_PAGE		0x20

/* eeprom data section */
#define RV3029_E2P_EEDATA1		0x28
#define RV3029_E2P_EEDATA2		0x29
#define RV3029_E2PDATA_SECTION_LEN	0x02

/* eeprom control section */
#define RV3029_CONTROL_E2P_EECTRL	0x30
#define RV3029_EECTRL_THP		BIT(0) /* temp scan interval */
#define RV3029_EECTRL_THE		BIT(1) /* thermometer enable */
#define RV3029_EECTRL_FD0		BIT(2) /* CLKOUT */
#define RV3029_EECTRL_FD1		BIT(3) /* CLKOUT */
#define RV3029_TRICKLE_1K		BIT(4) /* 1.5K resistance */
#define RV3029_TRICKLE_5K		BIT(5) /* 5K   resistance */
#define RV3029_TRICKLE_20K		BIT(6) /* 20K  resistance */
#define RV3029_TRICKLE_80K		BIT(7) /* 80K  resistance */
#define RV3029_TRICKLE_MASK		(RV3029_TRICKLE_1K |\
					 RV3029_TRICKLE_5K |\
					 RV3029_TRICKLE_20K |\
					 RV3029_TRICKLE_80K)
#define RV3029_TRICKLE_SHIFT		4
#define RV3029_CONTROL_E2P_XOFFS	0x31 /* XTAL offset */
#define RV3029_CONTROL_E2P_XOFFS_SIGN	BIT(7) /* Sign: 1->pos, 0->neg */
#define RV3029_CONTROL_E2P_QCOEF	0x32 /* XTAL temp drift coef */
#define RV3029_CONTROL_E2P_TURNOVER	0x33 /* XTAL turnover temp (in *C) */
#define RV3029_CONTROL_E2P_TOV_MASK	0x3F /* XTAL turnover temp mask */

/* user ram section */
#define RV3029_USR1_RAM_PAGE		0x38
#define RV3029_USR1_SECTION_LEN		0x04
#define RV3029_USR2_RAM_PAGE		0x3C
#define RV3029_USR2_SECTION_LEN		0x04

struct rv3029_data {
	struct device		*dev;
	struct rtc_device	*rtc;
	struct regmap		*regmap;
	int irq;
};

static int rv3029_read_regs(struct device *dev, u8 reg, u8 *buf,
			    unsigned int len)
{
	struct rv3029_data *rv3029 = dev_get_drvdata(dev);

	if ((reg > RV3029_USR1_RAM_PAGE + 7) ||
	    (reg + len > RV3029_USR1_RAM_PAGE + 8))
		return -EINVAL;

	return regmap_bulk_read(rv3029->regmap, reg, buf, len);
}

static int rv3029_write_regs(struct device *dev, u8 reg, u8 const buf[],
			     unsigned int len)
{
	struct rv3029_data *rv3029 = dev_get_drvdata(dev);

	if ((reg > RV3029_USR1_RAM_PAGE + 7) ||
	    (reg + len > RV3029_USR1_RAM_PAGE + 8))
		return -EINVAL;

	return regmap_bulk_write(rv3029->regmap, reg, buf, len);
}

static int rv3029_update_bits(struct device *dev, u8 reg, u8 mask, u8 set)
{
	u8 buf;
	int ret;

	ret = rv3029_read_regs(dev, reg, &buf, 1);
	if (ret < 0)
		return ret;
	buf &= ~mask;
	buf |= set & mask;
	ret = rv3029_write_regs(dev, reg, &buf, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int rv3029_get_sr(struct device *dev, u8 *buf)
{
	int ret = rv3029_read_regs(dev, RV3029_STATUS, buf, 1);

	if (ret < 0)
		return -EIO;
	dev_dbg(dev, "status = 0x%.2x (%d)\n", buf[0], buf[0]);
	return 0;
}

static int rv3029_set_sr(struct device *dev, u8 val)
{
	u8 buf[1];
	int sr;

	buf[0] = val;
	sr = rv3029_write_regs(dev, RV3029_STATUS, buf, 1);
	dev_dbg(dev, "status = 0x%.2x (%d)\n", buf[0], buf[0]);
	if (sr < 0)
		return -EIO;
	return 0;
}

static int rv3029_eeprom_busywait(struct device *dev)
{
	int i, ret;
	u8 sr;

	for (i = 100; i > 0; i--) {
		ret = rv3029_get_sr(dev, &sr);
		if (ret < 0)
			break;
		if (!(sr & RV3029_STATUS_EEBUSY))
			break;
		usleep_range(1000, 10000);
	}
	if (i <= 0) {
		dev_err(dev, "EEPROM busy wait timeout.\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static int rv3029_eeprom_exit(struct device *dev)
{
	/* Re-enable eeprom refresh */
	return rv3029_update_bits(dev, RV3029_ONOFF_CTRL,
				  RV3029_ONOFF_CTRL_EERE,
				  RV3029_ONOFF_CTRL_EERE);
}

static int rv3029_eeprom_enter(struct device *dev)
{
	int ret;
	u8 sr;

	/* Check whether we are in the allowed voltage range. */
	ret = rv3029_get_sr(dev, &sr);
	if (ret < 0)
		return ret;
	if (sr & (RV3029_STATUS_VLOW1 | RV3029_STATUS_VLOW2)) {
		/* We clear the bits and retry once just in case
		 * we had a brown out in early startup.
		 */
		sr &= ~RV3029_STATUS_VLOW1;
		sr &= ~RV3029_STATUS_VLOW2;
		ret = rv3029_set_sr(dev, sr);
		if (ret < 0)
			return ret;
		usleep_range(1000, 10000);
		ret = rv3029_get_sr(dev, &sr);
		if (ret < 0)
			return ret;
		if (sr & (RV3029_STATUS_VLOW1 | RV3029_STATUS_VLOW2)) {
			dev_err(dev,
				"Supply voltage is too low to safely access the EEPROM.\n");
			return -ENODEV;
		}
	}

	/* Disable eeprom refresh. */
	ret = rv3029_update_bits(dev, RV3029_ONOFF_CTRL, RV3029_ONOFF_CTRL_EERE,
				 0);
	if (ret < 0)
		return ret;

	/* Wait for any previous eeprom accesses to finish. */
	ret = rv3029_eeprom_busywait(dev);
	if (ret < 0)
		rv3029_eeprom_exit(dev);

	return ret;
}

static int rv3029_eeprom_read(struct device *dev, u8 reg,
			      u8 buf[], size_t len)
{
	int ret, err;

	err = rv3029_eeprom_enter(dev);
	if (err < 0)
		return err;

	ret = rv3029_read_regs(dev, reg, buf, len);

	err = rv3029_eeprom_exit(dev);
	if (err < 0)
		return err;

	return ret;
}

static int rv3029_eeprom_write(struct device *dev, u8 reg,
			       u8 const buf[], size_t len)
{
	int ret, err;
	size_t i;
	u8 tmp;

	err = rv3029_eeprom_enter(dev);
	if (err < 0)
		return err;

	for (i = 0; i < len; i++, reg++) {
		ret = rv3029_read_regs(dev, reg, &tmp, 1);
		if (ret < 0)
			break;
		if (tmp != buf[i]) {
			ret = rv3029_write_regs(dev, reg, &buf[i], 1);
			if (ret < 0)
				break;
		}
		ret = rv3029_eeprom_busywait(dev);
		if (ret < 0)
			break;
	}

	err = rv3029_eeprom_exit(dev);
	if (err < 0)
		return err;

	return ret;
}

static int rv3029_eeprom_update_bits(struct device *dev,
				     u8 reg, u8 mask, u8 set)
{
	u8 buf;
	int ret;

	ret = rv3029_eeprom_read(dev, reg, &buf, 1);
	if (ret < 0)
		return ret;
	buf &= ~mask;
	buf |= set & mask;
	ret = rv3029_eeprom_write(dev, reg, &buf, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static irqreturn_t rv3029_handle_irq(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct rv3029_data *rv3029 = dev_get_drvdata(dev);
	struct mutex *lock = &rv3029->rtc->ops_lock;
	unsigned long events = 0;
	u8 flags, controls;
	int ret;

	mutex_lock(lock);

	ret = rv3029_read_regs(dev, RV3029_IRQ_CTRL, &controls, 1);
	if (ret) {
		dev_warn(dev, "Read IRQ Control Register error %d\n", ret);
		mutex_unlock(lock);
		return IRQ_NONE;
	}

	ret = rv3029_read_regs(dev, RV3029_IRQ_FLAGS, &flags, 1);
	if (ret) {
		dev_warn(dev, "Read IRQ Flags Register error %d\n", ret);
		mutex_unlock(lock);
		return IRQ_NONE;
	}

	if (flags & RV3029_IRQ_FLAGS_AF) {
		flags &= ~RV3029_IRQ_FLAGS_AF;
		controls &= ~RV3029_IRQ_CTRL_AIE;
		events |= RTC_AF;
	}

	if (events) {
		rtc_update_irq(rv3029->rtc, 1, events);
		rv3029_write_regs(dev, RV3029_IRQ_FLAGS, &flags, 1);
		rv3029_write_regs(dev, RV3029_IRQ_CTRL, &controls, 1);
	}
	mutex_unlock(lock);

	return IRQ_HANDLED;
}

static int rv3029_read_time(struct device *dev, struct rtc_time *tm)
{
	u8 buf[1];
	int ret;
	u8 regs[RV3029_WATCH_SECTION_LEN] = { 0, };

	ret = rv3029_get_sr(dev, buf);
	if (ret < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}

	ret = rv3029_read_regs(dev, RV3029_W_SEC, regs,
			       RV3029_WATCH_SECTION_LEN);
	if (ret < 0) {
		dev_err(dev, "%s: reading RTC section failed\n", __func__);
		return ret;
	}

	tm->tm_sec = bcd2bin(regs[RV3029_W_SEC - RV3029_W_SEC]);
	tm->tm_min = bcd2bin(regs[RV3029_W_MINUTES - RV3029_W_SEC]);

	/* HR field has a more complex interpretation */
	{
		const u8 _hr = regs[RV3029_W_HOURS - RV3029_W_SEC];

		if (_hr & RV3029_REG_HR_12_24) {
			/* 12h format */
			tm->tm_hour = bcd2bin(_hr & 0x1f);
			if (_hr & RV3029_REG_HR_PM)	/* PM flag set */
				tm->tm_hour += 12;
		} else /* 24h format */
			tm->tm_hour = bcd2bin(_hr & 0x3f);
	}

	tm->tm_mday = bcd2bin(regs[RV3029_W_DATE - RV3029_W_SEC]);
	tm->tm_mon = bcd2bin(regs[RV3029_W_MONTHS - RV3029_W_SEC]) - 1;
	tm->tm_year = bcd2bin(regs[RV3029_W_YEARS - RV3029_W_SEC]) + 100;
	tm->tm_wday = bcd2bin(regs[RV3029_W_DAYS - RV3029_W_SEC]) - 1;

	return 0;
}

static int rv3029_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rtc_time *const tm = &alarm->time;
	int ret;
	u8 regs[8], controls, flags;

	ret = rv3029_get_sr(dev, regs);
	if (ret < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}

	ret = rv3029_read_regs(dev, RV3029_A_SC, regs,
			       RV3029_ALARM_SECTION_LEN);

	if (ret < 0) {
		dev_err(dev, "%s: reading alarm section failed\n", __func__);
		return ret;
	}

	ret = rv3029_read_regs(dev, RV3029_IRQ_CTRL, &controls, 1);
	if (ret) {
		dev_err(dev, "Read IRQ Control Register error %d\n", ret);
		return ret;
	}
	ret = rv3029_read_regs(dev, RV3029_IRQ_FLAGS, &flags, 1);
	if (ret < 0) {
		dev_err(dev, "Read IRQ Flags Register error %d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(regs[RV3029_A_SC - RV3029_A_SC] & 0x7f);
	tm->tm_min = bcd2bin(regs[RV3029_A_MN - RV3029_A_SC] & 0x7f);
	tm->tm_hour = bcd2bin(regs[RV3029_A_HR - RV3029_A_SC] & 0x3f);
	tm->tm_mday = bcd2bin(regs[RV3029_A_DT - RV3029_A_SC] & 0x3f);
	tm->tm_mon = bcd2bin(regs[RV3029_A_MO - RV3029_A_SC] & 0x1f) - 1;
	tm->tm_year = bcd2bin(regs[RV3029_A_YR - RV3029_A_SC] & 0x7f) + 100;
	tm->tm_wday = bcd2bin(regs[RV3029_A_DW - RV3029_A_SC] & 0x07) - 1;

	alarm->enabled = !!(controls & RV3029_IRQ_CTRL_AIE);
	alarm->pending = (flags & RV3029_IRQ_FLAGS_AF) && alarm->enabled;

	return 0;
}

static int rv3029_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	int ret;
	u8 controls;

	ret = rv3029_read_regs(dev, RV3029_IRQ_CTRL, &controls, 1);
	if (ret < 0) {
		dev_warn(dev, "Read IRQ Control Register error %d\n", ret);
		return ret;
	}

	/* enable/disable AIE irq */
	if (enable)
		controls |= RV3029_IRQ_CTRL_AIE;
	else
		controls &= ~RV3029_IRQ_CTRL_AIE;

	ret = rv3029_write_regs(dev, RV3029_IRQ_CTRL, &controls, 1);
	if (ret < 0) {
		dev_err(dev, "can't update INT reg\n");
		return ret;
	}

	return 0;
}

static int rv3029_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
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

	ret = rv3029_get_sr(dev, regs);
	if (ret < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return -EIO;
	}

	/* Activate all the alarms with AE_x bit */
	regs[RV3029_A_SC - RV3029_A_SC] = bin2bcd(tm->tm_sec) | RV3029_A_AE_X;
	regs[RV3029_A_MN - RV3029_A_SC] = bin2bcd(tm->tm_min) | RV3029_A_AE_X;
	regs[RV3029_A_HR - RV3029_A_SC] = (bin2bcd(tm->tm_hour) & 0x3f)
		| RV3029_A_AE_X;
	regs[RV3029_A_DT - RV3029_A_SC] = (bin2bcd(tm->tm_mday) & 0x3f)
		| RV3029_A_AE_X;
	regs[RV3029_A_MO - RV3029_A_SC] = (bin2bcd(tm->tm_mon + 1) & 0x1f)
		| RV3029_A_AE_X;
	regs[RV3029_A_DW - RV3029_A_SC] = (bin2bcd(tm->tm_wday + 1) & 0x7)
		| RV3029_A_AE_X;
	regs[RV3029_A_YR - RV3029_A_SC] = (bin2bcd(tm->tm_year - 100))
		| RV3029_A_AE_X;

	/* Write the alarm */
	ret = rv3029_write_regs(dev, RV3029_A_SC, regs,
				RV3029_ALARM_SECTION_LEN);
	if (ret < 0)
		return ret;

	if (alarm->enabled) {
		/* enable AIE irq */
		ret = rv3029_alarm_irq_enable(dev, 1);
		if (ret)
			return ret;
	} else {
		/* disable AIE irq */
		ret = rv3029_alarm_irq_enable(dev, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int rv3029_set_time(struct device *dev, struct rtc_time *tm)
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

	regs[RV3029_W_SEC - RV3029_W_SEC] = bin2bcd(tm->tm_sec);
	regs[RV3029_W_MINUTES - RV3029_W_SEC] = bin2bcd(tm->tm_min);
	regs[RV3029_W_HOURS - RV3029_W_SEC] = bin2bcd(tm->tm_hour);
	regs[RV3029_W_DATE - RV3029_W_SEC] = bin2bcd(tm->tm_mday);
	regs[RV3029_W_MONTHS - RV3029_W_SEC] = bin2bcd(tm->tm_mon + 1);
	regs[RV3029_W_DAYS - RV3029_W_SEC] = bin2bcd(tm->tm_wday + 1) & 0x7;
	regs[RV3029_W_YEARS - RV3029_W_SEC] = bin2bcd(tm->tm_year - 100);

	ret = rv3029_write_regs(dev, RV3029_W_SEC, regs,
				RV3029_WATCH_SECTION_LEN);
	if (ret < 0)
		return ret;

	ret = rv3029_get_sr(dev, regs);
	if (ret < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return ret;
	}
	/* clear PON bit */
	ret = rv3029_set_sr(dev, (regs[0] & ~RV3029_STATUS_PON));
	if (ret < 0) {
		dev_err(dev, "%s: reading SR failed\n", __func__);
		return ret;
	}

	return 0;
}

static const struct rv3029_trickle_tab_elem {
	u32 r;		/* resistance in ohms */
	u8 conf;	/* trickle config bits */
} rv3029_trickle_tab[] = {
	{
		.r	= 1076,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_5K |
			  RV3029_TRICKLE_20K | RV3029_TRICKLE_80K,
	}, {
		.r	= 1091,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_5K |
			  RV3029_TRICKLE_20K,
	}, {
		.r	= 1137,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_5K |
			  RV3029_TRICKLE_80K,
	}, {
		.r	= 1154,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_5K,
	}, {
		.r	= 1371,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_20K |
			  RV3029_TRICKLE_80K,
	}, {
		.r	= 1395,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_20K,
	}, {
		.r	= 1472,
		.conf	= RV3029_TRICKLE_1K | RV3029_TRICKLE_80K,
	}, {
		.r	= 1500,
		.conf	= RV3029_TRICKLE_1K,
	}, {
		.r	= 3810,
		.conf	= RV3029_TRICKLE_5K | RV3029_TRICKLE_20K |
			  RV3029_TRICKLE_80K,
	}, {
		.r	= 4000,
		.conf	= RV3029_TRICKLE_5K | RV3029_TRICKLE_20K,
	}, {
		.r	= 4706,
		.conf	= RV3029_TRICKLE_5K | RV3029_TRICKLE_80K,
	}, {
		.r	= 5000,
		.conf	= RV3029_TRICKLE_5K,
	}, {
		.r	= 16000,
		.conf	= RV3029_TRICKLE_20K | RV3029_TRICKLE_80K,
	}, {
		.r	= 20000,
		.conf	= RV3029_TRICKLE_20K,
	}, {
		.r	= 80000,
		.conf	= RV3029_TRICKLE_80K,
	},
};

static void rv3029_trickle_config(struct device *dev)
{
	struct device_node *of_node = dev->of_node;
	const struct rv3029_trickle_tab_elem *elem;
	int i, err;
	u32 ohms;
	u8 trickle_set_bits;

	if (!of_node)
		return;

	/* Configure the trickle charger. */
	err = of_property_read_u32(of_node, "trickle-resistor-ohms", &ohms);
	if (err) {
		/* Disable trickle charger. */
		trickle_set_bits = 0;
	} else {
		/* Enable trickle charger. */
		for (i = 0; i < ARRAY_SIZE(rv3029_trickle_tab); i++) {
			elem = &rv3029_trickle_tab[i];
			if (elem->r >= ohms)
				break;
		}
		trickle_set_bits = elem->conf;
		dev_info(dev,
			 "Trickle charger enabled at %d ohms resistance.\n",
			 elem->r);
	}
	err = rv3029_eeprom_update_bits(dev, RV3029_CONTROL_E2P_EECTRL,
					RV3029_TRICKLE_MASK,
					trickle_set_bits);
	if (err < 0)
		dev_err(dev, "Failed to update trickle charger config\n");
}

#ifdef CONFIG_RTC_DRV_RV3029_HWMON

static int rv3029_read_temp(struct device *dev, int *temp_mC)
{
	int ret;
	u8 temp;

	ret = rv3029_read_regs(dev, RV3029_TEMP_PAGE, &temp, 1);
	if (ret < 0)
		return ret;

	*temp_mC = ((int)temp - 60) * 1000;

	return 0;
}

static ssize_t rv3029_hwmon_show_temp(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int ret, temp_mC;

	ret = rv3029_read_temp(dev, &temp_mC);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", temp_mC);
}

static ssize_t rv3029_hwmon_set_update_interval(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	unsigned long interval_ms;
	int ret;
	u8 th_set_bits = 0;

	ret = kstrtoul(buf, 10, &interval_ms);
	if (ret < 0)
		return ret;

	if (interval_ms != 0) {
		th_set_bits |= RV3029_EECTRL_THE;
		if (interval_ms >= 16000)
			th_set_bits |= RV3029_EECTRL_THP;
	}
	ret = rv3029_eeprom_update_bits(dev, RV3029_CONTROL_E2P_EECTRL,
					RV3029_EECTRL_THE | RV3029_EECTRL_THP,
					th_set_bits);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t rv3029_hwmon_show_update_interval(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	int ret, interval_ms;
	u8 eectrl;

	ret = rv3029_eeprom_read(dev, RV3029_CONTROL_E2P_EECTRL,
				 &eectrl, 1);
	if (ret < 0)
		return ret;

	if (eectrl & RV3029_EECTRL_THE) {
		if (eectrl & RV3029_EECTRL_THP)
			interval_ms = 16000;
		else
			interval_ms = 1000;
	} else {
		interval_ms = 0;
	}

	return sprintf(buf, "%d\n", interval_ms);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, rv3029_hwmon_show_temp,
			  NULL, 0);
static SENSOR_DEVICE_ATTR(update_interval, S_IWUSR | S_IRUGO,
			  rv3029_hwmon_show_update_interval,
			  rv3029_hwmon_set_update_interval, 0);

static struct attribute *rv3029_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_update_interval.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rv3029_hwmon);

static void rv3029_hwmon_register(struct device *dev, const char *name)
{
	struct rv3029_data *rv3029 = dev_get_drvdata(dev);
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, name, rv3029,
							   rv3029_hwmon_groups);
	if (IS_ERR(hwmon_dev)) {
		dev_warn(dev, "unable to register hwmon device %ld\n",
			 PTR_ERR(hwmon_dev));
	}
}

#else /* CONFIG_RTC_DRV_RV3029_HWMON */

static void rv3029_hwmon_register(struct device *dev, const char *name)
{
}

#endif /* CONFIG_RTC_DRV_RV3029_HWMON */

static struct rtc_class_ops rv3029_rtc_ops = {
	.read_time	= rv3029_read_time,
	.set_time	= rv3029_set_time,
};

static int rv3029_probe(struct device *dev, struct regmap *regmap, int irq,
			const char *name)
{
	struct rv3029_data *rv3029;
	int rc = 0;
	u8 buf[1];

	rv3029 = devm_kzalloc(dev, sizeof(*rv3029), GFP_KERNEL);
	if (!rv3029)
		return -ENOMEM;

	rv3029->regmap = regmap;
	rv3029->irq = irq;
	rv3029->dev = dev;
	dev_set_drvdata(dev, rv3029);

	rc = rv3029_get_sr(dev, buf);
	if (rc < 0) {
		dev_err(dev, "reading status failed\n");
		return rc;
	}

	rv3029_trickle_config(dev);
	rv3029_hwmon_register(dev, name);

	rv3029->rtc = devm_rtc_device_register(dev, name, &rv3029_rtc_ops,
					       THIS_MODULE);
	if (IS_ERR(rv3029->rtc)) {
		dev_err(dev, "unable to register the class device\n");
		return PTR_ERR(rv3029->rtc);
	}

	if (rv3029->irq > 0) {
		rc = devm_request_threaded_irq(dev, rv3029->irq,
					       NULL, rv3029_handle_irq,
					       IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					       "rv3029", dev);
		if (rc) {
			dev_warn(dev, "unable to request IRQ, alarms disabled\n");
			rv3029->irq = 0;
		} else {
			rv3029_rtc_ops.read_alarm = rv3029_read_alarm;
			rv3029_rtc_ops.set_alarm = rv3029_set_alarm;
			rv3029_rtc_ops.alarm_irq_enable = rv3029_alarm_irq_enable;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_I2C)

static int rv3029_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK |
				     I2C_FUNC_SMBUS_BYTE)) {
		dev_err(&client->dev, "Adapter does not support SMBUS_I2C_BLOCK or SMBUS_I2C_BYTE\n");
		return -ENODEV;
	}

	regmap = devm_regmap_init_i2c(client, &config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return rv3029_probe(&client->dev, regmap, client->irq, client->name);
}

static const struct i2c_device_id rv3029_id[] = {
	{ "rv3029", 0 },
	{ "rv3029c2", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rv3029_id);

static const struct of_device_id rv3029_of_match[] = {
	{ .compatible = "microcrystal,rv3029" },
	/* Backward compatibility only, do not use compatibles below: */
	{ .compatible = "rv3029" },
	{ .compatible = "rv3029c2" },
	{ .compatible = "mc,rv3029c2" },
	{ }
};
MODULE_DEVICE_TABLE(of, rv3029_of_match);

static struct i2c_driver rv3029_driver = {
	.driver = {
		.name = "rtc-rv3029c2",
		.of_match_table = of_match_ptr(rv3029_of_match),
	},
	.probe		= rv3029_i2c_probe,
	.id_table	= rv3029_id,
};

static int rv3029_register_driver(void)
{
	return i2c_add_driver(&rv3029_driver);
}

static void rv3029_unregister_driver(void)
{
	i2c_del_driver(&rv3029_driver);
}

#else

static int rv3029_register_driver(void)
{
	return 0;
}

static void rv3029_unregister_driver(void)
{
}

#endif

#if IS_ENABLED(CONFIG_SPI_MASTER)

static int rv3049_probe(struct spi_device *spi)
{
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return rv3029_probe(&spi->dev, regmap, spi->irq, "rv3049");
}

static struct spi_driver rv3049_driver = {
	.driver = {
		.name    = "rv3049",
	},
	.probe   = rv3049_probe,
};

static int rv3049_register_driver(void)
{
	return spi_register_driver(&rv3049_driver);
}

static void rv3049_unregister_driver(void)
{
	spi_unregister_driver(&rv3049_driver);
}

#else

static int rv3049_register_driver(void)
{
	return 0;
}

static void rv3049_unregister_driver(void)
{
}

#endif

static int __init rv30x9_init(void)
{
	int ret;

	ret = rv3029_register_driver();
	if (ret) {
		pr_err("Failed to register rv3029 driver: %d\n", ret);
		return ret;
	}

	ret = rv3049_register_driver();
	if (ret) {
		pr_err("Failed to register rv3049 driver: %d\n", ret);
		rv3029_unregister_driver();
	}

	return ret;
}
module_init(rv30x9_init)

static void __exit rv30x9_exit(void)
{
	rv3049_unregister_driver();
	rv3029_unregister_driver();
}
module_exit(rv30x9_exit)

MODULE_AUTHOR("Gregory Hermant <gregory.hermant@calao-systems.com>");
MODULE_AUTHOR("Michael Buesch <m@bues.ch>");
MODULE_DESCRIPTION("Micro Crystal RV3029/RV3049 RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rv3049");
