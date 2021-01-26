// SPDX-License-Identifier: GPL-2.0+
/*
 * rtc-ab-b5ze-s3 - Driver for Abracon AB-RTCMC-32.768Khz-B5ZE-S3
 *                  I2C RTC / Alarm chip
 *
 * Copyright (C) 2014, Arnaud EBALARD <arno@natisbad.org>
 *
 * Detailed datasheet of the chip is available here:
 *
 *  https://www.abracon.com/realtimeclock/AB-RTCMC-32.768kHz-B5ZE-S3-Application-Manual.pdf
 *
 * This work is based on ISL12057 driver (drivers/rtc/rtc-isl12057.c).
 *
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>

#define DRV_NAME "rtc-ab-b5ze-s3"

/* Control section */
#define ABB5ZES3_REG_CTRL1	   0x00	   /* Control 1 register */
#define ABB5ZES3_REG_CTRL1_CIE	   BIT(0)  /* Pulse interrupt enable */
#define ABB5ZES3_REG_CTRL1_AIE	   BIT(1)  /* Alarm interrupt enable */
#define ABB5ZES3_REG_CTRL1_SIE	   BIT(2)  /* Second interrupt enable */
#define ABB5ZES3_REG_CTRL1_PM	   BIT(3)  /* 24h/12h mode */
#define ABB5ZES3_REG_CTRL1_SR	   BIT(4)  /* Software reset */
#define ABB5ZES3_REG_CTRL1_STOP	   BIT(5)  /* RTC circuit enable */
#define ABB5ZES3_REG_CTRL1_CAP	   BIT(7)

#define ABB5ZES3_REG_CTRL2	   0x01	   /* Control 2 register */
#define ABB5ZES3_REG_CTRL2_CTBIE   BIT(0)  /* Countdown timer B int. enable */
#define ABB5ZES3_REG_CTRL2_CTAIE   BIT(1)  /* Countdown timer A int. enable */
#define ABB5ZES3_REG_CTRL2_WTAIE   BIT(2)  /* Watchdog timer A int. enable */
#define ABB5ZES3_REG_CTRL2_AF	   BIT(3)  /* Alarm interrupt status */
#define ABB5ZES3_REG_CTRL2_SF	   BIT(4)  /* Second interrupt status */
#define ABB5ZES3_REG_CTRL2_CTBF	   BIT(5)  /* Countdown timer B int. status */
#define ABB5ZES3_REG_CTRL2_CTAF	   BIT(6)  /* Countdown timer A int. status */
#define ABB5ZES3_REG_CTRL2_WTAF	   BIT(7)  /* Watchdog timer A int. status */

#define ABB5ZES3_REG_CTRL3	   0x02	   /* Control 3 register */
#define ABB5ZES3_REG_CTRL3_PM2	   BIT(7)  /* Power Management bit 2 */
#define ABB5ZES3_REG_CTRL3_PM1	   BIT(6)  /* Power Management bit 1 */
#define ABB5ZES3_REG_CTRL3_PM0	   BIT(5)  /* Power Management bit 0 */
#define ABB5ZES3_REG_CTRL3_BSF	   BIT(3)  /* Battery switchover int. status */
#define ABB5ZES3_REG_CTRL3_BLF	   BIT(2)  /* Battery low int. status */
#define ABB5ZES3_REG_CTRL3_BSIE	   BIT(1)  /* Battery switchover int. enable */
#define ABB5ZES3_REG_CTRL3_BLIE	   BIT(0)  /* Battery low int. enable */

#define ABB5ZES3_CTRL_SEC_LEN	   3

/* RTC section */
#define ABB5ZES3_REG_RTC_SC	   0x03	   /* RTC Seconds register */
#define ABB5ZES3_REG_RTC_SC_OSC	   BIT(7)  /* Clock integrity status */
#define ABB5ZES3_REG_RTC_MN	   0x04	   /* RTC Minutes register */
#define ABB5ZES3_REG_RTC_HR	   0x05	   /* RTC Hours register */
#define ABB5ZES3_REG_RTC_HR_PM	   BIT(5)  /* RTC Hours PM bit */
#define ABB5ZES3_REG_RTC_DT	   0x06	   /* RTC Date register */
#define ABB5ZES3_REG_RTC_DW	   0x07	   /* RTC Day of the week register */
#define ABB5ZES3_REG_RTC_MO	   0x08	   /* RTC Month register */
#define ABB5ZES3_REG_RTC_YR	   0x09	   /* RTC Year register */

#define ABB5ZES3_RTC_SEC_LEN	   7

/* Alarm section (enable bits are all active low) */
#define ABB5ZES3_REG_ALRM_MN	   0x0A	   /* Alarm - minute register */
#define ABB5ZES3_REG_ALRM_MN_AE	   BIT(7)  /* Minute enable */
#define ABB5ZES3_REG_ALRM_HR	   0x0B	   /* Alarm - hours register */
#define ABB5ZES3_REG_ALRM_HR_AE	   BIT(7)  /* Hour enable */
#define ABB5ZES3_REG_ALRM_DT	   0x0C	   /* Alarm - date register */
#define ABB5ZES3_REG_ALRM_DT_AE	   BIT(7)  /* Date (day of the month) enable */
#define ABB5ZES3_REG_ALRM_DW	   0x0D	   /* Alarm - day of the week reg. */
#define ABB5ZES3_REG_ALRM_DW_AE	   BIT(7)  /* Day of the week enable */

#define ABB5ZES3_ALRM_SEC_LEN	   4

/* Frequency offset section */
#define ABB5ZES3_REG_FREQ_OF	   0x0E	   /* Frequency offset register */
#define ABB5ZES3_REG_FREQ_OF_MODE  0x0E	   /* Offset mode: 2 hours / minute */

/* CLOCKOUT section */
#define ABB5ZES3_REG_TIM_CLK	   0x0F	   /* Timer & Clockout register */
#define ABB5ZES3_REG_TIM_CLK_TAM   BIT(7)  /* Permanent/pulsed timer A/int. 2 */
#define ABB5ZES3_REG_TIM_CLK_TBM   BIT(6)  /* Permanent/pulsed timer B */
#define ABB5ZES3_REG_TIM_CLK_COF2  BIT(5)  /* Clkout Freq bit 2 */
#define ABB5ZES3_REG_TIM_CLK_COF1  BIT(4)  /* Clkout Freq bit 1 */
#define ABB5ZES3_REG_TIM_CLK_COF0  BIT(3)  /* Clkout Freq bit 0 */
#define ABB5ZES3_REG_TIM_CLK_TAC1  BIT(2)  /* Timer A: - 01 : countdown */
#define ABB5ZES3_REG_TIM_CLK_TAC0  BIT(1)  /*	       - 10 : timer	*/
#define ABB5ZES3_REG_TIM_CLK_TBC   BIT(0)  /* Timer B enable */

/* Timer A Section */
#define ABB5ZES3_REG_TIMA_CLK	   0x10	   /* Timer A clock register */
#define ABB5ZES3_REG_TIMA_CLK_TAQ2 BIT(2)  /* Freq bit 2 */
#define ABB5ZES3_REG_TIMA_CLK_TAQ1 BIT(1)  /* Freq bit 1 */
#define ABB5ZES3_REG_TIMA_CLK_TAQ0 BIT(0)  /* Freq bit 0 */
#define ABB5ZES3_REG_TIMA	   0x11	   /* Timer A register */

#define ABB5ZES3_TIMA_SEC_LEN	   2

/* Timer B Section */
#define ABB5ZES3_REG_TIMB_CLK	   0x12	   /* Timer B clock register */
#define ABB5ZES3_REG_TIMB_CLK_TBW2 BIT(6)
#define ABB5ZES3_REG_TIMB_CLK_TBW1 BIT(5)
#define ABB5ZES3_REG_TIMB_CLK_TBW0 BIT(4)
#define ABB5ZES3_REG_TIMB_CLK_TAQ2 BIT(2)
#define ABB5ZES3_REG_TIMB_CLK_TAQ1 BIT(1)
#define ABB5ZES3_REG_TIMB_CLK_TAQ0 BIT(0)
#define ABB5ZES3_REG_TIMB	   0x13	   /* Timer B register */
#define ABB5ZES3_TIMB_SEC_LEN	   2

#define ABB5ZES3_MEM_MAP_LEN	   0x14

struct abb5zes3_rtc_data {
	struct rtc_device *rtc;
	struct regmap *regmap;

	int irq;

	bool battery_low;
	bool timer_alarm; /* current alarm is via timer A */
};

/*
 * Try and match register bits w/ fixed null values to see whether we
 * are dealing with an ABB5ZES3.
 */
static int abb5zes3_i2c_validate_chip(struct regmap *regmap)
{
	u8 regs[ABB5ZES3_MEM_MAP_LEN];
	static const u8 mask[ABB5ZES3_MEM_MAP_LEN] = { 0x00, 0x00, 0x10, 0x00,
						       0x80, 0xc0, 0xc0, 0xf8,
						       0xe0, 0x00, 0x00, 0x40,
						       0x40, 0x78, 0x00, 0x00,
						       0xf8, 0x00, 0x88, 0x00 };
	int ret, i;

	ret = regmap_bulk_read(regmap, 0, regs, ABB5ZES3_MEM_MAP_LEN);
	if (ret)
		return ret;

	for (i = 0; i < ABB5ZES3_MEM_MAP_LEN; ++i) {
		if (regs[i] & mask[i]) /* check if bits are cleared */
			return -ENODEV;
	}

	return 0;
}

/* Clear alarm status bit. */
static int _abb5zes3_rtc_clear_alarm(struct device *dev)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(data->regmap, ABB5ZES3_REG_CTRL2,
				 ABB5ZES3_REG_CTRL2_AF, 0);
	if (ret)
		dev_err(dev, "%s: clearing alarm failed (%d)\n", __func__, ret);

	return ret;
}

/* Enable or disable alarm (i.e. alarm interrupt generation) */
static int _abb5zes3_rtc_update_alarm(struct device *dev, bool enable)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(data->regmap, ABB5ZES3_REG_CTRL1,
				 ABB5ZES3_REG_CTRL1_AIE,
				 enable ? ABB5ZES3_REG_CTRL1_AIE : 0);
	if (ret)
		dev_err(dev, "%s: writing alarm INT failed (%d)\n",
			__func__, ret);

	return ret;
}

/* Enable or disable timer (watchdog timer A interrupt generation) */
static int _abb5zes3_rtc_update_timer(struct device *dev, bool enable)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(data->regmap, ABB5ZES3_REG_CTRL2,
				 ABB5ZES3_REG_CTRL2_WTAIE,
				 enable ? ABB5ZES3_REG_CTRL2_WTAIE : 0);
	if (ret)
		dev_err(dev, "%s: writing timer INT failed (%d)\n",
			__func__, ret);

	return ret;
}

/*
 * Note: we only read, so regmap inner lock protection is sufficient, i.e.
 * we do not need driver's main lock protection.
 */
static int _abb5zes3_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	u8 regs[ABB5ZES3_REG_RTC_SC + ABB5ZES3_RTC_SEC_LEN];
	int ret = 0;

	/*
	 * As we need to read CTRL1 register anyway to access 24/12h
	 * mode bit, we do a single bulk read of both control and RTC
	 * sections (they are consecutive). This also ease indexing
	 * of register values after bulk read.
	 */
	ret = regmap_bulk_read(data->regmap, ABB5ZES3_REG_CTRL1, regs,
			       sizeof(regs));
	if (ret) {
		dev_err(dev, "%s: reading RTC time failed (%d)\n",
			__func__, ret);
		return ret;
	}

	/* If clock integrity is not guaranteed, do not return a time value */
	if (regs[ABB5ZES3_REG_RTC_SC] & ABB5ZES3_REG_RTC_SC_OSC)
		return -ENODATA;

	tm->tm_sec = bcd2bin(regs[ABB5ZES3_REG_RTC_SC] & 0x7F);
	tm->tm_min = bcd2bin(regs[ABB5ZES3_REG_RTC_MN]);

	if (regs[ABB5ZES3_REG_CTRL1] & ABB5ZES3_REG_CTRL1_PM) { /* 12hr mode */
		tm->tm_hour = bcd2bin(regs[ABB5ZES3_REG_RTC_HR] & 0x1f);
		if (regs[ABB5ZES3_REG_RTC_HR] & ABB5ZES3_REG_RTC_HR_PM) /* PM */
			tm->tm_hour += 12;
	} else {						/* 24hr mode */
		tm->tm_hour = bcd2bin(regs[ABB5ZES3_REG_RTC_HR]);
	}

	tm->tm_mday = bcd2bin(regs[ABB5ZES3_REG_RTC_DT]);
	tm->tm_wday = bcd2bin(regs[ABB5ZES3_REG_RTC_DW]);
	tm->tm_mon  = bcd2bin(regs[ABB5ZES3_REG_RTC_MO]) - 1; /* starts at 1 */
	tm->tm_year = bcd2bin(regs[ABB5ZES3_REG_RTC_YR]) + 100;

	return ret;
}

static int abb5zes3_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	u8 regs[ABB5ZES3_REG_RTC_SC + ABB5ZES3_RTC_SEC_LEN];
	int ret;

	regs[ABB5ZES3_REG_RTC_SC] = bin2bcd(tm->tm_sec); /* MSB=0 clears OSC */
	regs[ABB5ZES3_REG_RTC_MN] = bin2bcd(tm->tm_min);
	regs[ABB5ZES3_REG_RTC_HR] = bin2bcd(tm->tm_hour); /* 24-hour format */
	regs[ABB5ZES3_REG_RTC_DT] = bin2bcd(tm->tm_mday);
	regs[ABB5ZES3_REG_RTC_DW] = bin2bcd(tm->tm_wday);
	regs[ABB5ZES3_REG_RTC_MO] = bin2bcd(tm->tm_mon + 1);
	regs[ABB5ZES3_REG_RTC_YR] = bin2bcd(tm->tm_year - 100);

	ret = regmap_bulk_write(data->regmap, ABB5ZES3_REG_RTC_SC,
				regs + ABB5ZES3_REG_RTC_SC,
				ABB5ZES3_RTC_SEC_LEN);

	return ret;
}

/*
 * Set provided TAQ and Timer A registers (TIMA_CLK and TIMA) based on
 * given number of seconds.
 */
static inline void sec_to_timer_a(u8 secs, u8 *taq, u8 *timer_a)
{
	*taq = ABB5ZES3_REG_TIMA_CLK_TAQ1; /* 1Hz */
	*timer_a = secs;
}

/*
 * Return current number of seconds in Timer A. As we only use
 * timer A with a 1Hz freq, this is what we expect to have.
 */
static inline int sec_from_timer_a(u8 *secs, u8 taq, u8 timer_a)
{
	if (taq != ABB5ZES3_REG_TIMA_CLK_TAQ1) /* 1Hz */
		return -EINVAL;

	*secs = timer_a;

	return 0;
}

/*
 * Read alarm currently configured via a watchdog timer using timer A. This
 * is done by reading current RTC time and adding remaining timer time.
 */
static int _abb5zes3_rtc_read_timer(struct device *dev,
				    struct rtc_wkalrm *alarm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	struct rtc_time rtc_tm, *alarm_tm = &alarm->time;
	u8 regs[ABB5ZES3_TIMA_SEC_LEN + 1];
	unsigned long rtc_secs;
	unsigned int reg;
	u8 timer_secs;
	int ret;

	/*
	 * Instead of doing two separate calls, because they are consecutive,
	 * we grab both clockout register and Timer A section. The latter is
	 * used to decide if timer A is enabled (as a watchdog timer).
	 */
	ret = regmap_bulk_read(data->regmap, ABB5ZES3_REG_TIM_CLK, regs,
			       ABB5ZES3_TIMA_SEC_LEN + 1);
	if (ret) {
		dev_err(dev, "%s: reading Timer A section failed (%d)\n",
			__func__, ret);
		return ret;
	}

	/* get current time ... */
	ret = _abb5zes3_rtc_read_time(dev, &rtc_tm);
	if (ret)
		return ret;

	/* ... convert to seconds ... */
	rtc_secs = rtc_tm_to_time64(&rtc_tm);

	/* ... add remaining timer A time ... */
	ret = sec_from_timer_a(&timer_secs, regs[1], regs[2]);
	if (ret)
		return ret;

	/* ... and convert back. */
	rtc_time64_to_tm(rtc_secs + timer_secs, alarm_tm);

	ret = regmap_read(data->regmap, ABB5ZES3_REG_CTRL2, &reg);
	if (ret) {
		dev_err(dev, "%s: reading ctrl reg failed (%d)\n",
			__func__, ret);
		return ret;
	}

	alarm->enabled = !!(reg & ABB5ZES3_REG_CTRL2_WTAIE);

	return 0;
}

/* Read alarm currently configured via a RTC alarm registers. */
static int _abb5zes3_rtc_read_alarm(struct device *dev,
				    struct rtc_wkalrm *alarm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	struct rtc_time rtc_tm, *alarm_tm = &alarm->time;
	unsigned long rtc_secs, alarm_secs;
	u8 regs[ABB5ZES3_ALRM_SEC_LEN];
	unsigned int reg;
	int ret;

	ret = regmap_bulk_read(data->regmap, ABB5ZES3_REG_ALRM_MN, regs,
			       ABB5ZES3_ALRM_SEC_LEN);
	if (ret) {
		dev_err(dev, "%s: reading alarm section failed (%d)\n",
			__func__, ret);
		return ret;
	}

	alarm_tm->tm_sec  = 0;
	alarm_tm->tm_min  = bcd2bin(regs[0] & 0x7f);
	alarm_tm->tm_hour = bcd2bin(regs[1] & 0x3f);
	alarm_tm->tm_mday = bcd2bin(regs[2] & 0x3f);
	alarm_tm->tm_wday = -1;

	/*
	 * The alarm section does not store year/month. We use the ones in rtc
	 * section as a basis and increment month and then year if needed to get
	 * alarm after current time.
	 */
	ret = _abb5zes3_rtc_read_time(dev, &rtc_tm);
	if (ret)
		return ret;

	alarm_tm->tm_year = rtc_tm.tm_year;
	alarm_tm->tm_mon = rtc_tm.tm_mon;

	rtc_secs = rtc_tm_to_time64(&rtc_tm);
	alarm_secs = rtc_tm_to_time64(alarm_tm);

	if (alarm_secs < rtc_secs) {
		if (alarm_tm->tm_mon == 11) {
			alarm_tm->tm_mon = 0;
			alarm_tm->tm_year += 1;
		} else {
			alarm_tm->tm_mon += 1;
		}
	}

	ret = regmap_read(data->regmap, ABB5ZES3_REG_CTRL1, &reg);
	if (ret) {
		dev_err(dev, "%s: reading ctrl reg failed (%d)\n",
			__func__, ret);
		return ret;
	}

	alarm->enabled = !!(reg & ABB5ZES3_REG_CTRL1_AIE);

	return 0;
}

/*
 * As the Alarm mechanism supported by the chip is only accurate to the
 * minute, we use the watchdog timer mechanism provided by timer A
 * (up to 256 seconds w/ a second accuracy) for low alarm values (below
 * 4 minutes). Otherwise, we use the common alarm mechanism provided
 * by the chip. In order for that to work, we keep track of currently
 * configured timer type via 'timer_alarm' flag in our private data
 * structure.
 */
static int abb5zes3_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->timer_alarm)
		ret = _abb5zes3_rtc_read_timer(dev, alarm);
	else
		ret = _abb5zes3_rtc_read_alarm(dev, alarm);

	return ret;
}

/*
 * Set alarm using chip alarm mechanism. It is only accurate to the
 * minute (not the second). The function expects alarm interrupt to
 * be disabled.
 */
static int _abb5zes3_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	struct rtc_time *alarm_tm = &alarm->time;
	u8 regs[ABB5ZES3_ALRM_SEC_LEN];
	struct rtc_time rtc_tm;
	int ret, enable = 1;

	if (!alarm->enabled) {
		enable = 0;
	} else {
		unsigned long rtc_secs, alarm_secs;

		/*
		 * Chip only support alarms up to one month in the future. Let's
		 * return an error if we get something after that limit.
		 * Comparison is done by incrementing rtc_tm month field by one
		 * and checking alarm value is still below.
		 */
		ret = _abb5zes3_rtc_read_time(dev, &rtc_tm);
		if (ret)
			return ret;

		if (rtc_tm.tm_mon == 11) { /* handle year wrapping */
			rtc_tm.tm_mon = 0;
			rtc_tm.tm_year += 1;
		} else {
			rtc_tm.tm_mon += 1;
		}

		rtc_secs = rtc_tm_to_time64(&rtc_tm);
		alarm_secs = rtc_tm_to_time64(alarm_tm);

		if (alarm_secs > rtc_secs) {
			dev_err(dev, "%s: alarm maximum is one month in the future (%d)\n",
				__func__, ret);
			return -EINVAL;
		}
	}

	/*
	 * Program all alarm registers but DW one. For each register, setting
	 * MSB to 0 enables associated alarm.
	 */
	regs[0] = bin2bcd(alarm_tm->tm_min) & 0x7f;
	regs[1] = bin2bcd(alarm_tm->tm_hour) & 0x3f;
	regs[2] = bin2bcd(alarm_tm->tm_mday) & 0x3f;
	regs[3] = ABB5ZES3_REG_ALRM_DW_AE; /* do not match day of the week */

	ret = regmap_bulk_write(data->regmap, ABB5ZES3_REG_ALRM_MN, regs,
				ABB5ZES3_ALRM_SEC_LEN);
	if (ret < 0) {
		dev_err(dev, "%s: writing ALARM section failed (%d)\n",
			__func__, ret);
		return ret;
	}

	/* Record currently configured alarm is not a timer */
	data->timer_alarm = 0;

	/* Enable or disable alarm interrupt generation */
	return _abb5zes3_rtc_update_alarm(dev, enable);
}

/*
 * Set alarm using timer watchdog (via timer A) mechanism. The function expects
 * timer A interrupt to be disabled.
 */
static int _abb5zes3_rtc_set_timer(struct device *dev, struct rtc_wkalrm *alarm,
				   u8 secs)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	u8 regs[ABB5ZES3_TIMA_SEC_LEN];
	u8 mask = ABB5ZES3_REG_TIM_CLK_TAC0 | ABB5ZES3_REG_TIM_CLK_TAC1;
	int ret = 0;

	/* Program given number of seconds to Timer A registers */
	sec_to_timer_a(secs, &regs[0], &regs[1]);
	ret = regmap_bulk_write(data->regmap, ABB5ZES3_REG_TIMA_CLK, regs,
				ABB5ZES3_TIMA_SEC_LEN);
	if (ret < 0) {
		dev_err(dev, "%s: writing timer section failed\n", __func__);
		return ret;
	}

	/* Configure Timer A as a watchdog timer */
	ret = regmap_update_bits(data->regmap, ABB5ZES3_REG_TIM_CLK,
				 mask, ABB5ZES3_REG_TIM_CLK_TAC1);
	if (ret)
		dev_err(dev, "%s: failed to update timer\n", __func__);

	/* Record currently configured alarm is a timer */
	data->timer_alarm = 1;

	/* Enable or disable timer interrupt generation */
	return _abb5zes3_rtc_update_timer(dev, alarm->enabled);
}

/*
 * The chip has an alarm which is only accurate to the minute. In order to
 * handle alarms below that limit, we use the watchdog timer function of
 * timer A. More precisely, the timer method is used for alarms below 240
 * seconds.
 */
static int abb5zes3_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	struct rtc_time *alarm_tm = &alarm->time;
	unsigned long rtc_secs, alarm_secs;
	struct rtc_time rtc_tm;
	int ret;

	ret = _abb5zes3_rtc_read_time(dev, &rtc_tm);
	if (ret)
		return ret;

	rtc_secs = rtc_tm_to_time64(&rtc_tm);
	alarm_secs = rtc_tm_to_time64(alarm_tm);

	/* Let's first disable both the alarm and the timer interrupts */
	ret = _abb5zes3_rtc_update_alarm(dev, false);
	if (ret < 0) {
		dev_err(dev, "%s: unable to disable alarm (%d)\n", __func__,
			ret);
		return ret;
	}
	ret = _abb5zes3_rtc_update_timer(dev, false);
	if (ret < 0) {
		dev_err(dev, "%s: unable to disable timer (%d)\n", __func__,
			ret);
		return ret;
	}

	data->timer_alarm = 0;

	/*
	 * Let's now configure the alarm; if we are expected to ring in
	 * more than 240s, then we setup an alarm. Otherwise, a timer.
	 */
	if ((alarm_secs > rtc_secs) && ((alarm_secs - rtc_secs) <= 240))
		ret = _abb5zes3_rtc_set_timer(dev, alarm,
					      alarm_secs - rtc_secs);
	else
		ret = _abb5zes3_rtc_set_alarm(dev, alarm);

	if (ret)
		dev_err(dev, "%s: unable to configure alarm (%d)\n", __func__,
			ret);

	return ret;
}

/* Enable or disable battery low irq generation */
static inline int _abb5zes3_rtc_battery_low_irq_enable(struct regmap *regmap,
						       bool enable)
{
	return regmap_update_bits(regmap, ABB5ZES3_REG_CTRL3,
				  ABB5ZES3_REG_CTRL3_BLIE,
				  enable ? ABB5ZES3_REG_CTRL3_BLIE : 0);
}

/*
 * Check current RTC status and enable/disable what needs to be. Return 0 if
 * everything went ok and a negative value upon error.
 */
static int abb5zes3_rtc_check_setup(struct device *dev)
{
	struct abb5zes3_rtc_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int reg;
	int ret;
	u8 mask;

	/*
	 * By default, the devices generates a 32.768KHz signal on IRQ#1 pin. It
	 * is disabled here to prevent polluting the interrupt line and
	 * uselessly triggering the IRQ handler we install for alarm and battery
	 * low events. Note: this is done before clearing int. status below
	 * in this function.
	 * We also disable all timers and set timer interrupt to permanent (not
	 * pulsed).
	 */
	mask = (ABB5ZES3_REG_TIM_CLK_TBC | ABB5ZES3_REG_TIM_CLK_TAC0 |
		ABB5ZES3_REG_TIM_CLK_TAC1 | ABB5ZES3_REG_TIM_CLK_COF0 |
		ABB5ZES3_REG_TIM_CLK_COF1 | ABB5ZES3_REG_TIM_CLK_COF2 |
		ABB5ZES3_REG_TIM_CLK_TBM | ABB5ZES3_REG_TIM_CLK_TAM);
	ret = regmap_update_bits(regmap, ABB5ZES3_REG_TIM_CLK, mask,
				 ABB5ZES3_REG_TIM_CLK_COF0 |
				 ABB5ZES3_REG_TIM_CLK_COF1 |
				 ABB5ZES3_REG_TIM_CLK_COF2);
	if (ret < 0) {
		dev_err(dev, "%s: unable to initialize clkout register (%d)\n",
			__func__, ret);
		return ret;
	}

	/*
	 * Each component of the alarm (MN, HR, DT, DW) can be enabled/disabled
	 * individually by clearing/setting MSB of each associated register. So,
	 * we set all alarm enable bits to disable current alarm setting.
	 */
	mask = (ABB5ZES3_REG_ALRM_MN_AE | ABB5ZES3_REG_ALRM_HR_AE |
		ABB5ZES3_REG_ALRM_DT_AE | ABB5ZES3_REG_ALRM_DW_AE);
	ret = regmap_update_bits(regmap, ABB5ZES3_REG_CTRL2, mask, mask);
	if (ret < 0) {
		dev_err(dev, "%s: unable to disable alarm setting (%d)\n",
			__func__, ret);
		return ret;
	}

	/* Set Control 1 register (RTC enabled, 24hr mode, all int. disabled) */
	mask = (ABB5ZES3_REG_CTRL1_CIE | ABB5ZES3_REG_CTRL1_AIE |
		ABB5ZES3_REG_CTRL1_SIE | ABB5ZES3_REG_CTRL1_PM |
		ABB5ZES3_REG_CTRL1_CAP | ABB5ZES3_REG_CTRL1_STOP);
	ret = regmap_update_bits(regmap, ABB5ZES3_REG_CTRL1, mask, 0);
	if (ret < 0) {
		dev_err(dev, "%s: unable to initialize CTRL1 register (%d)\n",
			__func__, ret);
		return ret;
	}

	/*
	 * Set Control 2 register (timer int. disabled, alarm status cleared).
	 * WTAF is read-only and cleared automatically by reading the register.
	 */
	mask = (ABB5ZES3_REG_CTRL2_CTBIE | ABB5ZES3_REG_CTRL2_CTAIE |
		ABB5ZES3_REG_CTRL2_WTAIE | ABB5ZES3_REG_CTRL2_AF |
		ABB5ZES3_REG_CTRL2_SF | ABB5ZES3_REG_CTRL2_CTBF |
		ABB5ZES3_REG_CTRL2_CTAF);
	ret = regmap_update_bits(regmap, ABB5ZES3_REG_CTRL2, mask, 0);
	if (ret < 0) {
		dev_err(dev, "%s: unable to initialize CTRL2 register (%d)\n",
			__func__, ret);
		return ret;
	}

	/*
	 * Enable battery low detection function and battery switchover function
	 * (standard mode). Disable associated interrupts. Clear battery
	 * switchover flag but not battery low flag. The latter is checked
	 * later below.
	 */
	mask = (ABB5ZES3_REG_CTRL3_PM0  | ABB5ZES3_REG_CTRL3_PM1 |
		ABB5ZES3_REG_CTRL3_PM2  | ABB5ZES3_REG_CTRL3_BLIE |
		ABB5ZES3_REG_CTRL3_BSIE | ABB5ZES3_REG_CTRL3_BSF);
	ret = regmap_update_bits(regmap, ABB5ZES3_REG_CTRL3, mask, 0);
	if (ret < 0) {
		dev_err(dev, "%s: unable to initialize CTRL3 register (%d)\n",
			__func__, ret);
		return ret;
	}

	/* Check oscillator integrity flag */
	ret = regmap_read(regmap, ABB5ZES3_REG_RTC_SC, &reg);
	if (ret < 0) {
		dev_err(dev, "%s: unable to read osc. integrity flag (%d)\n",
			__func__, ret);
		return ret;
	}

	if (reg & ABB5ZES3_REG_RTC_SC_OSC) {
		dev_err(dev, "clock integrity not guaranteed. Osc. has stopped or has been interrupted.\n");
		dev_err(dev, "change battery (if not already done) and then set time to reset osc. failure flag.\n");
	}

	/*
	 * Check battery low flag at startup: this allows reporting battery
	 * is low at startup when IRQ line is not connected. Note: we record
	 * current status to avoid reenabling this interrupt later in probe
	 * function if battery is low.
	 */
	ret = regmap_read(regmap, ABB5ZES3_REG_CTRL3, &reg);
	if (ret < 0) {
		dev_err(dev, "%s: unable to read battery low flag (%d)\n",
			__func__, ret);
		return ret;
	}

	data->battery_low = reg & ABB5ZES3_REG_CTRL3_BLF;
	if (data->battery_low) {
		dev_err(dev, "RTC battery is low; please, consider changing it!\n");

		ret = _abb5zes3_rtc_battery_low_irq_enable(regmap, false);
		if (ret)
			dev_err(dev, "%s: disabling battery low interrupt generation failed (%d)\n",
				__func__, ret);
	}

	return ret;
}

static int abb5zes3_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enable)
{
	struct abb5zes3_rtc_data *rtc_data = dev_get_drvdata(dev);
	int ret = 0;

	if (rtc_data->irq) {
		if (rtc_data->timer_alarm)
			ret = _abb5zes3_rtc_update_timer(dev, enable);
		else
			ret = _abb5zes3_rtc_update_alarm(dev, enable);
	}

	return ret;
}

static irqreturn_t _abb5zes3_rtc_interrupt(int irq, void *data)
{
	struct i2c_client *client = data;
	struct device *dev = &client->dev;
	struct abb5zes3_rtc_data *rtc_data = dev_get_drvdata(dev);
	struct rtc_device *rtc = rtc_data->rtc;
	u8 regs[ABB5ZES3_CTRL_SEC_LEN];
	int ret, handled = IRQ_NONE;

	ret = regmap_bulk_read(rtc_data->regmap, 0, regs,
			       ABB5ZES3_CTRL_SEC_LEN);
	if (ret) {
		dev_err(dev, "%s: unable to read control section (%d)!\n",
			__func__, ret);
		return handled;
	}

	/*
	 * Check battery low detection flag and disable battery low interrupt
	 * generation if flag is set (interrupt can only be cleared when
	 * battery is replaced).
	 */
	if (regs[ABB5ZES3_REG_CTRL3] & ABB5ZES3_REG_CTRL3_BLF) {
		dev_err(dev, "RTC battery is low; please change it!\n");

		_abb5zes3_rtc_battery_low_irq_enable(rtc_data->regmap, false);

		handled = IRQ_HANDLED;
	}

	/* Check alarm flag */
	if (regs[ABB5ZES3_REG_CTRL2] & ABB5ZES3_REG_CTRL2_AF) {
		dev_dbg(dev, "RTC alarm!\n");

		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);

		/* Acknowledge and disable the alarm */
		_abb5zes3_rtc_clear_alarm(dev);
		_abb5zes3_rtc_update_alarm(dev, 0);

		handled = IRQ_HANDLED;
	}

	/* Check watchdog Timer A flag */
	if (regs[ABB5ZES3_REG_CTRL2] & ABB5ZES3_REG_CTRL2_WTAF) {
		dev_dbg(dev, "RTC timer!\n");

		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);

		/*
		 * Acknowledge and disable the alarm. Note: WTAF
		 * flag had been cleared when reading CTRL2
		 */
		_abb5zes3_rtc_update_timer(dev, 0);

		rtc_data->timer_alarm = 0;

		handled = IRQ_HANDLED;
	}

	return handled;
}

static const struct rtc_class_ops rtc_ops = {
	.read_time = _abb5zes3_rtc_read_time,
	.set_time = abb5zes3_rtc_set_time,
	.read_alarm = abb5zes3_rtc_read_alarm,
	.set_alarm = abb5zes3_rtc_set_alarm,
	.alarm_irq_enable = abb5zes3_rtc_alarm_irq_enable,
};

static const struct regmap_config abb5zes3_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int abb5zes3_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct abb5zes3_rtc_data *data = NULL;
	struct device *dev = &client->dev;
	struct regmap *regmap;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	regmap = devm_regmap_init_i2c(client, &abb5zes3_rtc_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "%s: regmap allocation failed: %d\n",
			__func__, ret);
		return ret;
	}

	ret = abb5zes3_i2c_validate_chip(regmap);
	if (ret)
		return ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;
	dev_set_drvdata(dev, data);

	ret = abb5zes3_rtc_check_setup(dev);
	if (ret)
		return ret;

	data->rtc = devm_rtc_allocate_device(dev);
	ret = PTR_ERR_OR_ZERO(data->rtc);
	if (ret) {
		dev_err(dev, "%s: unable to allocate RTC device (%d)\n",
			__func__, ret);
		return ret;
	}

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						_abb5zes3_rtc_interrupt,
						IRQF_SHARED | IRQF_ONESHOT,
						DRV_NAME, client);
		if (!ret) {
			device_init_wakeup(dev, true);
			data->irq = client->irq;
			dev_dbg(dev, "%s: irq %d used by RTC\n", __func__,
				client->irq);
		} else {
			dev_err(dev, "%s: irq %d unavailable (%d)\n",
				__func__, client->irq, ret);
			goto err;
		}
	}

	data->rtc->ops = &rtc_ops;
	data->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	data->rtc->range_max = RTC_TIMESTAMP_END_2099;

	/* Enable battery low detection interrupt if battery not already low */
	if (!data->battery_low && data->irq) {
		ret = _abb5zes3_rtc_battery_low_irq_enable(regmap, true);
		if (ret) {
			dev_err(dev, "%s: enabling battery low interrupt generation failed (%d)\n",
				__func__, ret);
			goto err;
		}
	}

	ret = rtc_register_device(data->rtc);

err:
	if (ret && data->irq)
		device_init_wakeup(dev, false);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int abb5zes3_rtc_suspend(struct device *dev)
{
	struct abb5zes3_rtc_data *rtc_data = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		return enable_irq_wake(rtc_data->irq);

	return 0;
}

static int abb5zes3_rtc_resume(struct device *dev)
{
	struct abb5zes3_rtc_data *rtc_data = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		return disable_irq_wake(rtc_data->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(abb5zes3_rtc_pm_ops, abb5zes3_rtc_suspend,
			 abb5zes3_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id abb5zes3_dt_match[] = {
	{ .compatible = "abracon,abb5zes3" },
	{ },
};
MODULE_DEVICE_TABLE(of, abb5zes3_dt_match);
#endif

static const struct i2c_device_id abb5zes3_id[] = {
	{ "abb5zes3", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, abb5zes3_id);

static struct i2c_driver abb5zes3_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &abb5zes3_rtc_pm_ops,
		.of_match_table = of_match_ptr(abb5zes3_dt_match),
	},
	.probe	  = abb5zes3_probe,
	.id_table = abb5zes3_id,
};
module_i2c_driver(abb5zes3_driver);

MODULE_AUTHOR("Arnaud EBALARD <arno@natisbad.org>");
MODULE_DESCRIPTION("Abracon AB-RTCMC-32.768kHz-B5ZE-S3 RTC/Alarm driver");
MODULE_LICENSE("GPL");
