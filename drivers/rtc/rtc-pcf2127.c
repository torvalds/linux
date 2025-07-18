// SPDX-License-Identifier: GPL-2.0-only
/*
 * An I2C and SPI driver for the NXP PCF2127/29/31 RTC
 * Copyright 2013 Til-Technologies
 *
 * Author: Renaud Cerrato <r.cerrato@til-technologies.fr>
 *
 * Watchdog and tamper functions
 * Author: Bruno Thomsen <bruno.thomsen@gmail.com>
 *
 * PCF2131 support
 * Author: Hugo Villeneuve <hvilleneuve@dimonoff.com>
 *
 * based on the other drivers in this same directory.
 *
 * Datasheets: https://www.nxp.com/docs/en/data-sheet/PCF2127.pdf
 *             https://www.nxp.com/docs/en/data-sheet/PCF2131DS.pdf
 */

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

/* Control register 1 */
#define PCF2127_REG_CTRL1		0x00
#define PCF2127_BIT_CTRL1_POR_OVRD		BIT(3)
#define PCF2127_BIT_CTRL1_TSF1			BIT(4)
#define PCF2127_BIT_CTRL1_STOP			BIT(5)
/* Control register 2 */
#define PCF2127_REG_CTRL2		0x01
#define PCF2127_BIT_CTRL2_AIE			BIT(1)
#define PCF2127_BIT_CTRL2_TSIE			BIT(2)
#define PCF2127_BIT_CTRL2_AF			BIT(4)
#define PCF2127_BIT_CTRL2_TSF2			BIT(5)
#define PCF2127_BIT_CTRL2_WDTF			BIT(6)
/* Control register 3 */
#define PCF2127_REG_CTRL3		0x02
#define PCF2127_BIT_CTRL3_BLIE			BIT(0)
#define PCF2127_BIT_CTRL3_BIE			BIT(1)
#define PCF2127_BIT_CTRL3_BLF			BIT(2)
#define PCF2127_BIT_CTRL3_BF			BIT(3)
#define PCF2127_BIT_CTRL3_BTSE			BIT(4)
#define PCF2127_CTRL3_PM			GENMASK(7, 5)
/* Time and date registers */
#define PCF2127_REG_TIME_BASE		0x03
#define PCF2127_BIT_SC_OSF			BIT(7)
/* Alarm registers */
#define PCF2127_REG_ALARM_BASE		0x0A
#define PCF2127_BIT_ALARM_AE			BIT(7)
/* CLKOUT control register */
#define PCF2127_REG_CLKOUT		0x0f
#define PCF2127_BIT_CLKOUT_OTPR			BIT(5)
/* Watchdog registers */
#define PCF2127_REG_WD_CTL		0x10
#define PCF2127_BIT_WD_CTL_TF0			BIT(0)
#define PCF2127_BIT_WD_CTL_TF1			BIT(1)
#define PCF2127_BIT_WD_CTL_CD0			BIT(6)
#define PCF2127_BIT_WD_CTL_CD1			BIT(7)
#define PCF2127_REG_WD_VAL		0x11
/* Tamper timestamp1 registers */
#define PCF2127_REG_TS1_BASE		0x12
#define PCF2127_BIT_TS_CTRL_TSOFF		BIT(6)
#define PCF2127_BIT_TS_CTRL_TSM			BIT(7)
/*
 * RAM registers
 * PCF2127 has 512 bytes general-purpose static RAM (SRAM) that is
 * battery backed and can survive a power outage.
 * PCF2129/31 doesn't have this feature.
 */
#define PCF2127_REG_RAM_ADDR_MSB	0x1A
#define PCF2127_REG_RAM_WRT_CMD		0x1C
#define PCF2127_REG_RAM_RD_CMD		0x1D

/* Watchdog timer value constants */
#define PCF2127_WD_VAL_STOP		0
/* PCF2127/29 watchdog timer value constants */
#define PCF2127_WD_CLOCK_HZ_X1000	1000 /* 1Hz */
#define PCF2127_WD_MIN_HW_HEARTBEAT_MS	500
/* PCF2131 watchdog timer value constants */
#define PCF2131_WD_CLOCK_HZ_X1000	250  /* 1/4Hz */
#define PCF2131_WD_MIN_HW_HEARTBEAT_MS	4000

#define PCF2127_WD_DEFAULT_TIMEOUT_S	60

/* Mask for currently enabled interrupts */
#define PCF2127_CTRL1_IRQ_MASK (PCF2127_BIT_CTRL1_TSF1)
#define PCF2127_CTRL2_IRQ_MASK ( \
		PCF2127_BIT_CTRL2_AF | \
		PCF2127_BIT_CTRL2_WDTF | \
		PCF2127_BIT_CTRL2_TSF2)

#define PCF2127_MAX_TS_SUPPORTED	4

/* Control register 4 */
#define PCF2131_REG_CTRL4		0x03
#define PCF2131_BIT_CTRL4_TSF4			BIT(4)
#define PCF2131_BIT_CTRL4_TSF3			BIT(5)
#define PCF2131_BIT_CTRL4_TSF2			BIT(6)
#define PCF2131_BIT_CTRL4_TSF1			BIT(7)
/* Control register 5 */
#define PCF2131_REG_CTRL5		0x04
#define PCF2131_BIT_CTRL5_TSIE4			BIT(4)
#define PCF2131_BIT_CTRL5_TSIE3			BIT(5)
#define PCF2131_BIT_CTRL5_TSIE2			BIT(6)
#define PCF2131_BIT_CTRL5_TSIE1			BIT(7)
/* Software reset register */
#define PCF2131_REG_SR_RESET		0x05
#define PCF2131_SR_RESET_READ_PATTERN	(BIT(2) | BIT(5))
#define PCF2131_SR_RESET_CPR_CMD	(PCF2131_SR_RESET_READ_PATTERN | BIT(7))
/* Time and date registers */
#define PCF2131_REG_TIME_BASE		0x07
/* Alarm registers */
#define PCF2131_REG_ALARM_BASE		0x0E
/* CLKOUT control register */
#define PCF2131_REG_CLKOUT		0x13
/* Watchdog registers */
#define PCF2131_REG_WD_CTL		0x35
#define PCF2131_REG_WD_VAL		0x36
/* Tamper timestamp1 registers */
#define PCF2131_REG_TS1_BASE		0x14
/* Tamper timestamp2 registers */
#define PCF2131_REG_TS2_BASE		0x1B
/* Tamper timestamp3 registers */
#define PCF2131_REG_TS3_BASE		0x22
/* Tamper timestamp4 registers */
#define PCF2131_REG_TS4_BASE		0x29
/* Interrupt mask registers */
#define PCF2131_REG_INT_A_MASK1		0x31
#define PCF2131_REG_INT_A_MASK2		0x32
#define PCF2131_REG_INT_B_MASK1		0x33
#define PCF2131_REG_INT_B_MASK2		0x34
#define PCF2131_BIT_INT_BLIE		BIT(0)
#define PCF2131_BIT_INT_BIE		BIT(1)
#define PCF2131_BIT_INT_AIE		BIT(2)
#define PCF2131_BIT_INT_WD_CD		BIT(3)
#define PCF2131_BIT_INT_SI		BIT(4)
#define PCF2131_BIT_INT_MI		BIT(5)
#define PCF2131_CTRL2_IRQ_MASK ( \
		PCF2127_BIT_CTRL2_AF | \
		PCF2127_BIT_CTRL2_WDTF)
#define PCF2131_CTRL4_IRQ_MASK ( \
		PCF2131_BIT_CTRL4_TSF4 | \
		PCF2131_BIT_CTRL4_TSF3 | \
		PCF2131_BIT_CTRL4_TSF2 | \
		PCF2131_BIT_CTRL4_TSF1)

enum pcf21xx_type {
	PCF2127,
	PCF2129,
	PCF2131,
	PCF21XX_LAST_ID
};

struct pcf21xx_ts_config {
	u8 reg_base; /* Base register to read timestamp values. */

	/*
	 * If the TS input pin is driven to GND, an interrupt can be generated
	 * (supported by all variants).
	 */
	u8 gnd_detect_reg; /* Interrupt control register address. */
	u8 gnd_detect_bit; /* Interrupt bit. */

	/*
	 * If the TS input pin is driven to an intermediate level between GND
	 * and supply, an interrupt can be generated (optional feature depending
	 * on variant).
	 */
	u8 inter_detect_reg; /* Interrupt control register address. */
	u8 inter_detect_bit; /* Interrupt bit. */

	u8 ie_reg; /* Interrupt enable control register. */
	u8 ie_bit; /* Interrupt enable bit. */
};

struct pcf21xx_config {
	int type; /* IC variant */
	int max_register;
	unsigned int has_nvmem:1;
	unsigned int has_bit_wd_ctl_cd0:1;
	unsigned int wd_val_reg_readable:1; /* If watchdog value register can be read. */
	unsigned int has_int_a_b:1; /* PCF2131 supports two interrupt outputs. */
	u8 reg_time_base; /* Time/date base register. */
	u8 regs_alarm_base; /* Alarm function base registers. */
	u8 reg_wd_ctl; /* Watchdog control register. */
	u8 reg_wd_val; /* Watchdog value register. */
	u8 reg_clkout; /* Clkout register. */
	int wdd_clock_hz_x1000; /* Watchdog clock in Hz multiplicated by 1000 */
	int wdd_min_hw_heartbeat_ms;
	unsigned int ts_count;
	struct pcf21xx_ts_config ts[PCF2127_MAX_TS_SUPPORTED];
	struct attribute_group attribute_group;
};

struct pcf2127 {
	struct rtc_device *rtc;
	struct watchdog_device wdd;
	struct regmap *regmap;
	const struct pcf21xx_config *cfg;
	bool irq_enabled;
	time64_t ts[PCF2127_MAX_TS_SUPPORTED]; /* Timestamp values. */
	bool ts_valid[PCF2127_MAX_TS_SUPPORTED];  /* Timestamp valid indication. */
};

/*
 * In the routines that deal directly with the pcf2127 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf2127_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	unsigned char buf[7];
	int ret;

	/*
	 * Avoid reading CTRL2 register as it causes WD_VAL register
	 * value to reset to 0 which means watchdog is stopped.
	 */
	ret = regmap_bulk_read(pcf2127->regmap, pcf2127->cfg->reg_time_base,
			       buf, sizeof(buf));
	if (ret) {
		dev_err(dev, "%s: read error\n", __func__);
		return ret;
	}

	/* Clock integrity is not guaranteed when OSF flag is set. */
	if (buf[0] & PCF2127_BIT_SC_OSF) {
		/*
		 * no need clear the flag here,
		 * it will be cleared once the new date is saved
		 */
		dev_warn(dev,
			 "oscillator stop detected, date/time is not reliable\n");
		return -EINVAL;
	}

	dev_dbg(dev,
		"%s: raw data is sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

	tm->tm_sec = bcd2bin(buf[0] & 0x7F);
	tm->tm_min = bcd2bin(buf[1] & 0x7F);
	tm->tm_hour = bcd2bin(buf[2] & 0x3F);
	tm->tm_mday = bcd2bin(buf[3] & 0x3F);
	tm->tm_wday = buf[4] & 0x07;
	tm->tm_mon = bcd2bin(buf[5] & 0x1F) - 1;
	tm->tm_year = bcd2bin(buf[6]);
	tm->tm_year += 100;

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int pcf2127_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	unsigned char buf[7];
	int i = 0, err;

	dev_dbg(dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[i++] = bin2bcd(tm->tm_sec);	/* this will also clear OSF flag */
	buf[i++] = bin2bcd(tm->tm_min);
	buf[i++] = bin2bcd(tm->tm_hour);
	buf[i++] = bin2bcd(tm->tm_mday);
	buf[i++] = tm->tm_wday & 0x07;

	/* month, 1 - 12 */
	buf[i++] = bin2bcd(tm->tm_mon + 1);

	/* year */
	buf[i++] = bin2bcd(tm->tm_year - 100);

	/* Write access to time registers:
	 * PCF2127/29: no special action required.
	 * PCF2131:    requires setting the STOP and CPR bits. STOP bit needs to
	 *             be cleared after time registers are updated.
	 */
	if (pcf2127->cfg->type == PCF2131) {
		err = regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL1,
					 PCF2127_BIT_CTRL1_STOP,
					 PCF2127_BIT_CTRL1_STOP);
		if (err) {
			dev_dbg(dev, "setting STOP bit failed\n");
			return err;
		}

		err = regmap_write(pcf2127->regmap, PCF2131_REG_SR_RESET,
				   PCF2131_SR_RESET_CPR_CMD);
		if (err) {
			dev_dbg(dev, "sending CPR cmd failed\n");
			return err;
		}
	}

	/* write time register's data */
	err = regmap_bulk_write(pcf2127->regmap, pcf2127->cfg->reg_time_base, buf, i);
	if (err) {
		dev_dbg(dev, "%s: err=%d", __func__, err);
		return err;
	}

	if (pcf2127->cfg->type == PCF2131) {
		/* Clear STOP bit (PCF2131 only) after write is completed. */
		err = regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL1,
					 PCF2127_BIT_CTRL1_STOP, 0);
		if (err) {
			dev_dbg(dev, "clearing STOP bit failed\n");
			return err;
		}
	}

	return 0;
}

static int pcf2127_param_get(struct device *dev, struct rtc_param *param)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	u32 value;
	int ret;

	switch (param->param) {
	case RTC_PARAM_BACKUP_SWITCH_MODE:
		ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL3, &value);
		if (ret < 0)
			return ret;

		value = FIELD_GET(PCF2127_CTRL3_PM, value);

		if (value < 0x3)
			param->uvalue = RTC_BSM_LEVEL;
		else if (value < 0x6)
			param->uvalue = RTC_BSM_DIRECT;
		else
			param->uvalue = RTC_BSM_DISABLED;

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int pcf2127_param_set(struct device *dev, struct rtc_param *param)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	u8 mode = 0;
	u32 value;
	int ret;

	switch (param->param) {
	case RTC_PARAM_BACKUP_SWITCH_MODE:
		ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL3, &value);
		if (ret < 0)
			return ret;

		value = FIELD_GET(PCF2127_CTRL3_PM, value);

		if (value > 5)
			value -= 5;
		else if (value > 2)
			value -= 3;

		switch (param->uvalue) {
		case RTC_BSM_LEVEL:
			break;
		case RTC_BSM_DIRECT:
			mode = 3;
			break;
		case RTC_BSM_DISABLED:
			if (value == 0)
				value = 1;
			mode = 5;
			break;
		default:
			return -EINVAL;
		}

		return regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL3,
					  PCF2127_CTRL3_PM,
					  FIELD_PREP(PCF2127_CTRL3_PM, mode + value));

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int pcf2127_rtc_ioctl(struct device *dev,
				unsigned int cmd, unsigned long arg)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	int val, touser = 0;
	int ret;

	switch (cmd) {
	case RTC_VL_READ:
		ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL3, &val);
		if (ret)
			return ret;

		if (val & PCF2127_BIT_CTRL3_BLF)
			touser |= RTC_VL_BACKUP_LOW;

		if (val & PCF2127_BIT_CTRL3_BF)
			touser |= RTC_VL_BACKUP_SWITCH;

		return put_user(touser, (unsigned int __user *)arg);

	case RTC_VL_CLR:
		return regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL3,
					  PCF2127_BIT_CTRL3_BF, 0);

	default:
		return -ENOIOCTLCMD;
	}
}

static int pcf2127_nvmem_read(void *priv, unsigned int offset,
			      void *val, size_t bytes)
{
	struct pcf2127 *pcf2127 = priv;
	int ret;
	unsigned char offsetbuf[] = { offset >> 8, offset };

	ret = regmap_bulk_write(pcf2127->regmap, PCF2127_REG_RAM_ADDR_MSB,
				offsetbuf, 2);
	if (ret)
		return ret;

	return regmap_bulk_read(pcf2127->regmap, PCF2127_REG_RAM_RD_CMD,
				val, bytes);
}

static int pcf2127_nvmem_write(void *priv, unsigned int offset,
			       void *val, size_t bytes)
{
	struct pcf2127 *pcf2127 = priv;
	int ret;
	unsigned char offsetbuf[] = { offset >> 8, offset };

	ret = regmap_bulk_write(pcf2127->regmap, PCF2127_REG_RAM_ADDR_MSB,
				offsetbuf, 2);
	if (ret)
		return ret;

	return regmap_bulk_write(pcf2127->regmap, PCF2127_REG_RAM_WRT_CMD,
				 val, bytes);
}

/* watchdog driver */

static int pcf2127_wdt_ping(struct watchdog_device *wdd)
{
	int wd_val;
	struct pcf2127 *pcf2127 = watchdog_get_drvdata(wdd);

	/*
	 * Compute counter value of WATCHDG_TIM_VAL to obtain desired period
	 * in seconds, depending on the source clock frequency.
	 */
	wd_val = ((wdd->timeout * pcf2127->cfg->wdd_clock_hz_x1000) / 1000) + 1;

	return regmap_write(pcf2127->regmap, pcf2127->cfg->reg_wd_val, wd_val);
}

/*
 * Restart watchdog timer if feature is active.
 *
 * Note: Reading CTRL2 register causes watchdog to stop which is unfortunate,
 * since register also contain control/status flags for other features.
 * Always call this function after reading CTRL2 register.
 */
static int pcf2127_wdt_active_ping(struct watchdog_device *wdd)
{
	int ret = 0;

	if (watchdog_active(wdd)) {
		ret = pcf2127_wdt_ping(wdd);
		if (ret)
			dev_err(wdd->parent,
				"%s: watchdog restart failed, ret=%d\n",
				__func__, ret);
	}

	return ret;
}

static int pcf2127_wdt_start(struct watchdog_device *wdd)
{
	return pcf2127_wdt_ping(wdd);
}

static int pcf2127_wdt_stop(struct watchdog_device *wdd)
{
	struct pcf2127 *pcf2127 = watchdog_get_drvdata(wdd);

	return regmap_write(pcf2127->regmap, pcf2127->cfg->reg_wd_val,
			    PCF2127_WD_VAL_STOP);
}

static int pcf2127_wdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int new_timeout)
{
	dev_dbg(wdd->parent, "new watchdog timeout: %is (old: %is)\n",
		new_timeout, wdd->timeout);

	wdd->timeout = new_timeout;

	return pcf2127_wdt_active_ping(wdd);
}

static const struct watchdog_info pcf2127_wdt_info = {
	.identity = "NXP PCF2127/PCF2129 Watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
};

static const struct watchdog_ops pcf2127_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = pcf2127_wdt_start,
	.stop = pcf2127_wdt_stop,
	.ping = pcf2127_wdt_ping,
	.set_timeout = pcf2127_wdt_set_timeout,
};

/*
 * Compute watchdog period, t, in seconds, from the WATCHDG_TIM_VAL register
 * value, n, and the clock frequency, f1000, in Hz x 1000.
 *
 * The PCF2127/29 datasheet gives t as:
 *   t = n / f
 * The PCF2131 datasheet gives t as:
 *   t = (n - 1) / f
 * For both variants, the watchdog is triggered when the WATCHDG_TIM_VAL reaches
 * the value 1, and not zero. Consequently, the equation from the PCF2131
 * datasheet seems to be the correct one for both variants.
 */
static int pcf2127_watchdog_get_period(int n, int f1000)
{
	return (1000 * (n - 1)) / f1000;
}

static int pcf2127_watchdog_init(struct device *dev, struct pcf2127 *pcf2127)
{
	int ret;

	if (!IS_ENABLED(CONFIG_WATCHDOG) ||
	    !device_property_read_bool(dev, "reset-source"))
		return 0;

	pcf2127->wdd.parent = dev;
	pcf2127->wdd.info = &pcf2127_wdt_info;
	pcf2127->wdd.ops = &pcf2127_watchdog_ops;

	pcf2127->wdd.min_timeout =
		pcf2127_watchdog_get_period(
			2, pcf2127->cfg->wdd_clock_hz_x1000);
	pcf2127->wdd.max_timeout =
		pcf2127_watchdog_get_period(
			255, pcf2127->cfg->wdd_clock_hz_x1000);
	pcf2127->wdd.timeout = PCF2127_WD_DEFAULT_TIMEOUT_S;

	dev_dbg(dev, "%s clock = %d Hz / 1000\n", __func__,
		pcf2127->cfg->wdd_clock_hz_x1000);

	pcf2127->wdd.min_hw_heartbeat_ms = pcf2127->cfg->wdd_min_hw_heartbeat_ms;
	pcf2127->wdd.status = WATCHDOG_NOWAYOUT_INIT_STATUS;

	watchdog_set_drvdata(&pcf2127->wdd, pcf2127);

	/* Test if watchdog timer is started by bootloader */
	if (pcf2127->cfg->wd_val_reg_readable) {
		u32 wdd_timeout;

		ret = regmap_read(pcf2127->regmap, pcf2127->cfg->reg_wd_val,
				  &wdd_timeout);
		if (ret)
			return ret;

		if (wdd_timeout)
			set_bit(WDOG_HW_RUNNING, &pcf2127->wdd.status);
	}

	return devm_watchdog_register_device(dev, &pcf2127->wdd);
}

/* Alarm */
static int pcf2127_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	u8 buf[5];
	unsigned int ctrl2;
	int ret;

	ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL2, &ctrl2);
	if (ret)
		return ret;

	ret = pcf2127_wdt_active_ping(&pcf2127->wdd);
	if (ret)
		return ret;

	ret = regmap_bulk_read(pcf2127->regmap, pcf2127->cfg->regs_alarm_base,
			       buf, sizeof(buf));
	if (ret)
		return ret;

	alrm->enabled = ctrl2 & PCF2127_BIT_CTRL2_AIE;
	alrm->pending = ctrl2 & PCF2127_BIT_CTRL2_AF;

	alrm->time.tm_sec = bcd2bin(buf[0] & 0x7F);
	alrm->time.tm_min = bcd2bin(buf[1] & 0x7F);
	alrm->time.tm_hour = bcd2bin(buf[2] & 0x3F);
	alrm->time.tm_mday = bcd2bin(buf[3] & 0x3F);

	return 0;
}

static int pcf2127_rtc_alarm_irq_enable(struct device *dev, u32 enable)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL2,
				 PCF2127_BIT_CTRL2_AIE,
				 enable ? PCF2127_BIT_CTRL2_AIE : 0);
	if (ret)
		return ret;

	return pcf2127_wdt_active_ping(&pcf2127->wdd);
}

static int pcf2127_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	uint8_t buf[5];
	int ret;

	ret = regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL2,
				 PCF2127_BIT_CTRL2_AF, 0);
	if (ret)
		return ret;

	ret = pcf2127_wdt_active_ping(&pcf2127->wdd);
	if (ret)
		return ret;

	buf[0] = bin2bcd(alrm->time.tm_sec);
	buf[1] = bin2bcd(alrm->time.tm_min);
	buf[2] = bin2bcd(alrm->time.tm_hour);
	buf[3] = bin2bcd(alrm->time.tm_mday);
	buf[4] = PCF2127_BIT_ALARM_AE; /* Do not match on week day */

	ret = regmap_bulk_write(pcf2127->regmap, pcf2127->cfg->regs_alarm_base,
				buf, sizeof(buf));
	if (ret)
		return ret;

	return pcf2127_rtc_alarm_irq_enable(dev, alrm->enabled);
}

/*
 * This function reads one timestamp function data, caller is responsible for
 * calling pcf2127_wdt_active_ping()
 */
static int pcf2127_rtc_ts_read(struct device *dev, time64_t *ts,
			       int ts_id)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	struct rtc_time tm;
	int ret;
	unsigned char data[7];

	ret = regmap_bulk_read(pcf2127->regmap, pcf2127->cfg->ts[ts_id].reg_base,
			       data, sizeof(data));
	if (ret) {
		dev_err(dev, "%s: read error ret=%d\n", __func__, ret);
		return ret;
	}

	dev_dbg(dev,
		"%s: raw data is ts_sc=%02x, ts_mn=%02x, ts_hr=%02x, ts_dm=%02x, ts_mo=%02x, ts_yr=%02x\n",
		__func__, data[1], data[2], data[3], data[4], data[5], data[6]);

	tm.tm_sec = bcd2bin(data[1] & 0x7F);
	tm.tm_min = bcd2bin(data[2] & 0x7F);
	tm.tm_hour = bcd2bin(data[3] & 0x3F);
	tm.tm_mday = bcd2bin(data[4] & 0x3F);
	/* TS_MO register (month) value range: 1-12 */
	tm.tm_mon = bcd2bin(data[5] & 0x1F) - 1;
	tm.tm_year = bcd2bin(data[6]);
	if (tm.tm_year < 70)
		tm.tm_year += 100; /* assume we are in 1970...2069 */

	ret = rtc_valid_tm(&tm);
	if (ret) {
		dev_err(dev, "Invalid timestamp. ret=%d\n", ret);
		return ret;
	}

	*ts = rtc_tm_to_time64(&tm);
	return 0;
};

static void pcf2127_rtc_ts_snapshot(struct device *dev, int ts_id)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	int ret;

	if (ts_id >= pcf2127->cfg->ts_count)
		return;

	/* Let userspace read the first timestamp */
	if (pcf2127->ts_valid[ts_id])
		return;

	ret = pcf2127_rtc_ts_read(dev, &pcf2127->ts[ts_id], ts_id);
	if (!ret)
		pcf2127->ts_valid[ts_id] = true;
}

static irqreturn_t pcf2127_rtc_irq(int irq, void *dev)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	unsigned int ctrl2;
	int ret = 0;

	ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL2, &ctrl2);
	if (ret)
		return IRQ_NONE;

	if (pcf2127->cfg->ts_count == 1) {
		/* PCF2127/29 */
		unsigned int ctrl1;

		ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL1, &ctrl1);
		if (ret)
			return IRQ_NONE;

		if (!(ctrl1 & PCF2127_CTRL1_IRQ_MASK || ctrl2 & PCF2127_CTRL2_IRQ_MASK))
			return IRQ_NONE;

		if (ctrl1 & PCF2127_BIT_CTRL1_TSF1 || ctrl2 & PCF2127_BIT_CTRL2_TSF2)
			pcf2127_rtc_ts_snapshot(dev, 0);

		if (ctrl1 & PCF2127_CTRL1_IRQ_MASK)
			regmap_write(pcf2127->regmap, PCF2127_REG_CTRL1,
				     ctrl1 & ~PCF2127_CTRL1_IRQ_MASK);

		if (ctrl2 & PCF2127_CTRL2_IRQ_MASK)
			regmap_write(pcf2127->regmap, PCF2127_REG_CTRL2,
				     ctrl2 & ~PCF2127_CTRL2_IRQ_MASK);
	} else {
		/* PCF2131. */
		unsigned int ctrl4;

		ret = regmap_read(pcf2127->regmap, PCF2131_REG_CTRL4, &ctrl4);
		if (ret)
			return IRQ_NONE;

		if (!(ctrl4 & PCF2131_CTRL4_IRQ_MASK || ctrl2 & PCF2131_CTRL2_IRQ_MASK))
			return IRQ_NONE;

		if (ctrl4 & PCF2131_CTRL4_IRQ_MASK) {
			int i;
			int tsf_bit = PCF2131_BIT_CTRL4_TSF1; /* Start at bit 7. */

			for (i = 0; i < pcf2127->cfg->ts_count; i++) {
				if (ctrl4 & tsf_bit)
					pcf2127_rtc_ts_snapshot(dev, i);

				tsf_bit = tsf_bit >> 1;
			}

			regmap_write(pcf2127->regmap, PCF2131_REG_CTRL4,
				     ctrl4 & ~PCF2131_CTRL4_IRQ_MASK);
		}

		if (ctrl2 & PCF2131_CTRL2_IRQ_MASK)
			regmap_write(pcf2127->regmap, PCF2127_REG_CTRL2,
				     ctrl2 & ~PCF2131_CTRL2_IRQ_MASK);
	}

	if (ctrl2 & PCF2127_BIT_CTRL2_AF)
		rtc_update_irq(pcf2127->rtc, 1, RTC_IRQF | RTC_AF);

	pcf2127_wdt_active_ping(&pcf2127->wdd);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops pcf2127_rtc_ops = {
	.ioctl            = pcf2127_rtc_ioctl,
	.read_time        = pcf2127_rtc_read_time,
	.set_time         = pcf2127_rtc_set_time,
	.read_alarm       = pcf2127_rtc_read_alarm,
	.set_alarm        = pcf2127_rtc_set_alarm,
	.alarm_irq_enable = pcf2127_rtc_alarm_irq_enable,
	.param_get        = pcf2127_param_get,
	.param_set        = pcf2127_param_set,
};

/* sysfs interface */

static ssize_t timestamp_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count, int ts_id)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev->parent);
	int ret;

	if (ts_id >= pcf2127->cfg->ts_count)
		return 0;

	if (pcf2127->irq_enabled) {
		pcf2127->ts_valid[ts_id] = false;
	} else {
		/* Always clear GND interrupt bit. */
		ret = regmap_update_bits(pcf2127->regmap,
					 pcf2127->cfg->ts[ts_id].gnd_detect_reg,
					 pcf2127->cfg->ts[ts_id].gnd_detect_bit,
					 0);

		if (ret) {
			dev_err(dev, "%s: update TS gnd detect ret=%d\n", __func__, ret);
			return ret;
		}

		if (pcf2127->cfg->ts[ts_id].inter_detect_bit) {
			/* Clear intermediate level interrupt bit if supported. */
			ret = regmap_update_bits(pcf2127->regmap,
						 pcf2127->cfg->ts[ts_id].inter_detect_reg,
						 pcf2127->cfg->ts[ts_id].inter_detect_bit,
						 0);
			if (ret) {
				dev_err(dev, "%s: update TS intermediate level detect ret=%d\n",
					__func__, ret);
				return ret;
			}
		}

		ret = pcf2127_wdt_active_ping(&pcf2127->wdd);
		if (ret)
			return ret;
	}

	return count;
}

static ssize_t timestamp0_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return timestamp_store(dev, attr, buf, count, 0);
};

static ssize_t timestamp1_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return timestamp_store(dev, attr, buf, count, 1);
};

static ssize_t timestamp2_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return timestamp_store(dev, attr, buf, count, 2);
};

static ssize_t timestamp3_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return timestamp_store(dev, attr, buf, count, 3);
};

static ssize_t timestamp_show(struct device *dev,
			      struct device_attribute *attr, char *buf,
			      int ts_id)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev->parent);
	int ret;
	time64_t ts;

	if (ts_id >= pcf2127->cfg->ts_count)
		return 0;

	if (pcf2127->irq_enabled) {
		if (!pcf2127->ts_valid[ts_id])
			return 0;
		ts = pcf2127->ts[ts_id];
	} else {
		u8 valid_low = 0;
		u8 valid_inter = 0;
		unsigned int ctrl;

		/* Check if TS input pin is driven to GND, supported by all
		 * variants.
		 */
		ret = regmap_read(pcf2127->regmap,
				  pcf2127->cfg->ts[ts_id].gnd_detect_reg,
				  &ctrl);
		if (ret)
			return 0;

		valid_low = ctrl & pcf2127->cfg->ts[ts_id].gnd_detect_bit;

		if (pcf2127->cfg->ts[ts_id].inter_detect_bit) {
			/* Check if TS input pin is driven to intermediate level
			 * between GND and supply, if supported by variant.
			 */
			ret = regmap_read(pcf2127->regmap,
					  pcf2127->cfg->ts[ts_id].inter_detect_reg,
					  &ctrl);
			if (ret)
				return 0;

			valid_inter = ctrl & pcf2127->cfg->ts[ts_id].inter_detect_bit;
		}

		if (!valid_low && !valid_inter)
			return 0;

		ret = pcf2127_rtc_ts_read(dev->parent, &ts, ts_id);
		if (ret)
			return 0;

		ret = pcf2127_wdt_active_ping(&pcf2127->wdd);
		if (ret)
			return ret;
	}
	return sprintf(buf, "%llu\n", (unsigned long long)ts);
}

static ssize_t timestamp0_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return timestamp_show(dev, attr, buf, 0);
};

static ssize_t timestamp1_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return timestamp_show(dev, attr, buf, 1);
};

static ssize_t timestamp2_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return timestamp_show(dev, attr, buf, 2);
};

static ssize_t timestamp3_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return timestamp_show(dev, attr, buf, 3);
};

static DEVICE_ATTR_RW(timestamp0);
static DEVICE_ATTR_RW(timestamp1);
static DEVICE_ATTR_RW(timestamp2);
static DEVICE_ATTR_RW(timestamp3);

static struct attribute *pcf2127_attrs[] = {
	&dev_attr_timestamp0.attr,
	NULL
};

static struct attribute *pcf2131_attrs[] = {
	&dev_attr_timestamp0.attr,
	&dev_attr_timestamp1.attr,
	&dev_attr_timestamp2.attr,
	&dev_attr_timestamp3.attr,
	NULL
};

static struct pcf21xx_config pcf21xx_cfg[] = {
	[PCF2127] = {
		.type = PCF2127,
		.max_register = 0x1d,
		.has_nvmem = 1,
		.has_bit_wd_ctl_cd0 = 1,
		.wd_val_reg_readable = 1,
		.has_int_a_b = 0,
		.reg_time_base = PCF2127_REG_TIME_BASE,
		.regs_alarm_base = PCF2127_REG_ALARM_BASE,
		.reg_wd_ctl = PCF2127_REG_WD_CTL,
		.reg_wd_val = PCF2127_REG_WD_VAL,
		.reg_clkout = PCF2127_REG_CLKOUT,
		.wdd_clock_hz_x1000 = PCF2127_WD_CLOCK_HZ_X1000,
		.wdd_min_hw_heartbeat_ms = PCF2127_WD_MIN_HW_HEARTBEAT_MS,
		.ts_count = 1,
		.ts[0] = {
			.reg_base  = PCF2127_REG_TS1_BASE,
			.gnd_detect_reg = PCF2127_REG_CTRL1,
			.gnd_detect_bit = PCF2127_BIT_CTRL1_TSF1,
			.inter_detect_reg = PCF2127_REG_CTRL2,
			.inter_detect_bit = PCF2127_BIT_CTRL2_TSF2,
			.ie_reg    = PCF2127_REG_CTRL2,
			.ie_bit    = PCF2127_BIT_CTRL2_TSIE,
		},
		.attribute_group = {
			.attrs	= pcf2127_attrs,
		},
	},
	[PCF2129] = {
		.type = PCF2129,
		.max_register = 0x19,
		.has_nvmem = 0,
		.has_bit_wd_ctl_cd0 = 0,
		.wd_val_reg_readable = 1,
		.has_int_a_b = 0,
		.reg_time_base = PCF2127_REG_TIME_BASE,
		.regs_alarm_base = PCF2127_REG_ALARM_BASE,
		.reg_wd_ctl = PCF2127_REG_WD_CTL,
		.reg_wd_val = PCF2127_REG_WD_VAL,
		.reg_clkout = PCF2127_REG_CLKOUT,
		.wdd_clock_hz_x1000 = PCF2127_WD_CLOCK_HZ_X1000,
		.wdd_min_hw_heartbeat_ms = PCF2127_WD_MIN_HW_HEARTBEAT_MS,
		.ts_count = 1,
		.ts[0] = {
			.reg_base  = PCF2127_REG_TS1_BASE,
			.gnd_detect_reg = PCF2127_REG_CTRL1,
			.gnd_detect_bit = PCF2127_BIT_CTRL1_TSF1,
			.inter_detect_reg = PCF2127_REG_CTRL2,
			.inter_detect_bit = PCF2127_BIT_CTRL2_TSF2,
			.ie_reg    = PCF2127_REG_CTRL2,
			.ie_bit    = PCF2127_BIT_CTRL2_TSIE,
		},
		.attribute_group = {
			.attrs	= pcf2127_attrs,
		},
	},
	[PCF2131] = {
		.type = PCF2131,
		.max_register = 0x36,
		.has_nvmem = 0,
		.has_bit_wd_ctl_cd0 = 0,
		.wd_val_reg_readable = 0,
		.has_int_a_b = 1,
		.reg_time_base = PCF2131_REG_TIME_BASE,
		.regs_alarm_base = PCF2131_REG_ALARM_BASE,
		.reg_wd_ctl = PCF2131_REG_WD_CTL,
		.reg_wd_val = PCF2131_REG_WD_VAL,
		.reg_clkout = PCF2131_REG_CLKOUT,
		.wdd_clock_hz_x1000 = PCF2131_WD_CLOCK_HZ_X1000,
		.wdd_min_hw_heartbeat_ms = PCF2131_WD_MIN_HW_HEARTBEAT_MS,
		.ts_count = 4,
		.ts[0] = {
			.reg_base  = PCF2131_REG_TS1_BASE,
			.gnd_detect_reg = PCF2131_REG_CTRL4,
			.gnd_detect_bit = PCF2131_BIT_CTRL4_TSF1,
			.inter_detect_bit = 0,
			.ie_reg    = PCF2131_REG_CTRL5,
			.ie_bit    = PCF2131_BIT_CTRL5_TSIE1,
		},
		.ts[1] = {
			.reg_base  = PCF2131_REG_TS2_BASE,
			.gnd_detect_reg = PCF2131_REG_CTRL4,
			.gnd_detect_bit = PCF2131_BIT_CTRL4_TSF2,
			.inter_detect_bit = 0,
			.ie_reg    = PCF2131_REG_CTRL5,
			.ie_bit    = PCF2131_BIT_CTRL5_TSIE2,
		},
		.ts[2] = {
			.reg_base  = PCF2131_REG_TS3_BASE,
			.gnd_detect_reg = PCF2131_REG_CTRL4,
			.gnd_detect_bit = PCF2131_BIT_CTRL4_TSF3,
			.inter_detect_bit = 0,
			.ie_reg    = PCF2131_REG_CTRL5,
			.ie_bit    = PCF2131_BIT_CTRL5_TSIE3,
		},
		.ts[3] = {
			.reg_base  = PCF2131_REG_TS4_BASE,
			.gnd_detect_reg = PCF2131_REG_CTRL4,
			.gnd_detect_bit = PCF2131_BIT_CTRL4_TSF4,
			.inter_detect_bit = 0,
			.ie_reg    = PCF2131_REG_CTRL5,
			.ie_bit    = PCF2131_BIT_CTRL5_TSIE4,
		},
		.attribute_group = {
			.attrs	= pcf2131_attrs,
		},
	},
};

/*
 * Enable timestamp function and corresponding interrupt(s).
 */
static int pcf2127_enable_ts(struct device *dev, int ts_id)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	int ret;

	if (ts_id >= pcf2127->cfg->ts_count) {
		dev_err(dev, "%s: invalid tamper detection ID (%d)\n",
			__func__, ts_id);
		return -EINVAL;
	}

	/* Enable timestamp function. */
	ret = regmap_update_bits(pcf2127->regmap,
				 pcf2127->cfg->ts[ts_id].reg_base,
				 PCF2127_BIT_TS_CTRL_TSOFF |
				 PCF2127_BIT_TS_CTRL_TSM,
				 PCF2127_BIT_TS_CTRL_TSM);
	if (ret) {
		dev_err(dev, "%s: tamper detection config (ts%d_ctrl) failed\n",
			__func__, ts_id);
		return ret;
	}

	/*
	 * Enable interrupt generation when TSF timestamp flag is set.
	 * Interrupt signals are open-drain outputs and can be left floating if
	 * unused.
	 */
	ret = regmap_update_bits(pcf2127->regmap, pcf2127->cfg->ts[ts_id].ie_reg,
				 pcf2127->cfg->ts[ts_id].ie_bit,
				 pcf2127->cfg->ts[ts_id].ie_bit);
	if (ret) {
		dev_err(dev, "%s: tamper detection TSIE%d config failed\n",
			__func__, ts_id);
		return ret;
	}

	return ret;
}

/* Route all interrupt sources to INT A pin. */
static int pcf2127_configure_interrupt_pins(struct device *dev)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	int ret;

	/* Mask bits need to be cleared to enable corresponding
	 * interrupt source.
	 */
	ret = regmap_write(pcf2127->regmap,
			   PCF2131_REG_INT_A_MASK1, 0);
	if (ret)
		return ret;

	ret = regmap_write(pcf2127->regmap,
			   PCF2131_REG_INT_A_MASK2, 0);
	if (ret)
		return ret;

	return ret;
}

static int pcf2127_probe(struct device *dev, struct regmap *regmap,
			 int alarm_irq, const struct pcf21xx_config *config)
{
	struct pcf2127 *pcf2127;
	int ret = 0;
	unsigned int val;

	dev_dbg(dev, "%s\n", __func__);

	pcf2127 = devm_kzalloc(dev, sizeof(*pcf2127), GFP_KERNEL);
	if (!pcf2127)
		return -ENOMEM;

	pcf2127->regmap = regmap;
	pcf2127->cfg = config;

	dev_set_drvdata(dev, pcf2127);

	pcf2127->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(pcf2127->rtc))
		return PTR_ERR(pcf2127->rtc);

	pcf2127->rtc->ops = &pcf2127_rtc_ops;
	pcf2127->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	pcf2127->rtc->range_max = RTC_TIMESTAMP_END_2099;
	pcf2127->rtc->set_start_time = true; /* Sets actual start to 1970 */

	/*
	 * PCF2127/29 do not work correctly when setting alarms at 1s intervals.
	 * PCF2131 is ok.
	 */
	if (pcf2127->cfg->type == PCF2127 || pcf2127->cfg->type == PCF2129) {
		set_bit(RTC_FEATURE_ALARM_RES_2S, pcf2127->rtc->features);
		clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, pcf2127->rtc->features);
	}

	clear_bit(RTC_FEATURE_ALARM, pcf2127->rtc->features);

	if (alarm_irq > 0) {
		unsigned long flags;

		/*
		 * If flags = 0, devm_request_threaded_irq() will use IRQ flags
		 * obtained from device tree.
		 */
		if (dev_fwnode(dev))
			flags = 0;
		else
			flags = IRQF_TRIGGER_LOW;

		ret = devm_request_threaded_irq(dev, alarm_irq, NULL,
						pcf2127_rtc_irq,
						flags | IRQF_ONESHOT,
						dev_name(dev), dev);
		if (ret) {
			dev_err(dev, "failed to request alarm irq\n");
			return ret;
		}
		pcf2127->irq_enabled = true;
	}

	if (alarm_irq > 0 || device_property_read_bool(dev, "wakeup-source")) {
		device_init_wakeup(dev, true);
		set_bit(RTC_FEATURE_ALARM, pcf2127->rtc->features);
	}

	if (pcf2127->cfg->has_int_a_b) {
		/* Configure int A/B pins, independently of alarm_irq. */
		ret = pcf2127_configure_interrupt_pins(dev);
		if (ret) {
			dev_err(dev, "failed to configure interrupt pins\n");
			return ret;
		}
	}

	if (pcf2127->cfg->has_nvmem) {
		struct nvmem_config nvmem_cfg = {
			.priv = pcf2127,
			.reg_read = pcf2127_nvmem_read,
			.reg_write = pcf2127_nvmem_write,
			.size = 512,
		};

		ret = devm_rtc_nvmem_register(pcf2127->rtc, &nvmem_cfg);
	}

	/*
	 * The "Power-On Reset Override" facility prevents the RTC to do a reset
	 * after power on. For normal operation the PORO must be disabled.
	 */
	ret = regmap_clear_bits(pcf2127->regmap, PCF2127_REG_CTRL1,
				PCF2127_BIT_CTRL1_POR_OVRD);
	if (ret < 0)
		return ret;

	ret = regmap_read(pcf2127->regmap, pcf2127->cfg->reg_clkout, &val);
	if (ret < 0)
		return ret;

	if (!(val & PCF2127_BIT_CLKOUT_OTPR)) {
		ret = regmap_set_bits(pcf2127->regmap, pcf2127->cfg->reg_clkout,
				      PCF2127_BIT_CLKOUT_OTPR);
		if (ret < 0)
			return ret;

		msleep(100);
	}

	/*
	 * Watchdog timer enabled and reset pin /RST activated when timed out.
	 * Select 1Hz clock source for watchdog timer (1/4Hz for PCF2131).
	 * Note: Countdown timer disabled and not available.
	 * For pca2129, pcf2129 and pcf2131, only bit[7] is for Symbol WD_CD
	 * of register watchdg_tim_ctl. The bit[6] is labeled
	 * as T. Bits labeled as T must always be written with
	 * logic 0.
	 */
	ret = regmap_update_bits(pcf2127->regmap, pcf2127->cfg->reg_wd_ctl,
				 PCF2127_BIT_WD_CTL_CD1 |
				 PCF2127_BIT_WD_CTL_CD0 |
				 PCF2127_BIT_WD_CTL_TF1 |
				 PCF2127_BIT_WD_CTL_TF0,
				 PCF2127_BIT_WD_CTL_CD1 |
				 (pcf2127->cfg->has_bit_wd_ctl_cd0 ? PCF2127_BIT_WD_CTL_CD0 : 0) |
				 PCF2127_BIT_WD_CTL_TF1);
	if (ret) {
		dev_err(dev, "%s: watchdog config (wd_ctl) failed\n", __func__);
		return ret;
	}

	pcf2127_watchdog_init(dev, pcf2127);

	/*
	 * Disable battery low/switch-over timestamp and interrupts.
	 * Clear battery interrupt flags which can block new trigger events.
	 * Note: This is the default chip behaviour but added to ensure
	 * correct tamper timestamp and interrupt function.
	 */
	ret = regmap_update_bits(pcf2127->regmap, PCF2127_REG_CTRL3,
				 PCF2127_BIT_CTRL3_BTSE |
				 PCF2127_BIT_CTRL3_BIE |
				 PCF2127_BIT_CTRL3_BLIE, 0);
	if (ret) {
		dev_err(dev, "%s: interrupt config (ctrl3) failed\n",
			__func__);
		return ret;
	}

	/*
	 * Enable timestamp functions 1 to 4.
	 */
	for (int i = 0; i < pcf2127->cfg->ts_count; i++) {
		ret = pcf2127_enable_ts(dev, i);
		if (ret)
			return ret;
	}

	ret = rtc_add_group(pcf2127->rtc, &pcf2127->cfg->attribute_group);
	if (ret) {
		dev_err(dev, "%s: tamper sysfs registering failed\n",
			__func__);
		return ret;
	}

	return devm_rtc_register_device(pcf2127->rtc);
}

#ifdef CONFIG_OF
static const struct of_device_id pcf2127_of_match[] = {
	{ .compatible = "nxp,pcf2127", .data = &pcf21xx_cfg[PCF2127] },
	{ .compatible = "nxp,pcf2129", .data = &pcf21xx_cfg[PCF2129] },
	{ .compatible = "nxp,pca2129", .data = &pcf21xx_cfg[PCF2129] },
	{ .compatible = "nxp,pcf2131", .data = &pcf21xx_cfg[PCF2131] },
	{}
};
MODULE_DEVICE_TABLE(of, pcf2127_of_match);
#endif

#if IS_ENABLED(CONFIG_I2C)

static int pcf2127_i2c_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = i2c_master_send(client, data, count);
	if (ret != count)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int pcf2127_i2c_gather_write(void *context,
				const void *reg, size_t reg_size,
				const void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;
	void *buf;

	if (WARN_ON(reg_size != 1))
		return -EINVAL;

	buf = kmalloc(val_size + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, reg, 1);
	memcpy(buf + 1, val, val_size);

	ret = i2c_master_send(client, buf, val_size + 1);

	kfree(buf);

	if (ret != val_size + 1)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int pcf2127_i2c_read(void *context, const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	if (WARN_ON(reg_size != 1))
		return -EINVAL;

	ret = i2c_master_send(client, reg, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;

	ret = i2c_master_recv(client, val, val_size);
	if (ret != val_size)
		return ret < 0 ? ret : -EIO;

	return 0;
}

/*
 * The reason we need this custom regmap_bus instead of using regmap_init_i2c()
 * is that the STOP condition is required between set register address and
 * read register data when reading from registers.
 */
static const struct regmap_bus pcf2127_i2c_regmap = {
	.write = pcf2127_i2c_write,
	.gather_write = pcf2127_i2c_gather_write,
	.read = pcf2127_i2c_read,
};

static struct i2c_driver pcf2127_i2c_driver;

static const struct i2c_device_id pcf2127_i2c_id[] = {
	{ "pcf2127", PCF2127 },
	{ "pcf2129", PCF2129 },
	{ "pca2129", PCF2129 },
	{ "pcf2131", PCF2131 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf2127_i2c_id);

static int pcf2127_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	static struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	const struct pcf21xx_config *variant;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	if (client->dev.of_node) {
		variant = of_device_get_match_data(&client->dev);
		if (!variant)
			return -ENODEV;
	} else {
		enum pcf21xx_type type =
			i2c_match_id(pcf2127_i2c_id, client)->driver_data;

		if (type >= PCF21XX_LAST_ID)
			return -ENODEV;
		variant = &pcf21xx_cfg[type];
	}

	config.max_register = variant->max_register,

	regmap = devm_regmap_init(&client->dev, &pcf2127_i2c_regmap,
					&client->dev, &config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return pcf2127_probe(&client->dev, regmap, client->irq, variant);
}

static struct i2c_driver pcf2127_i2c_driver = {
	.driver		= {
		.name	= "rtc-pcf2127-i2c",
		.of_match_table = of_match_ptr(pcf2127_of_match),
	},
	.probe		= pcf2127_i2c_probe,
	.id_table	= pcf2127_i2c_id,
};

static int pcf2127_i2c_register_driver(void)
{
	return i2c_add_driver(&pcf2127_i2c_driver);
}

static void pcf2127_i2c_unregister_driver(void)
{
	i2c_del_driver(&pcf2127_i2c_driver);
}

#else

static int pcf2127_i2c_register_driver(void)
{
	return 0;
}

static void pcf2127_i2c_unregister_driver(void)
{
}

#endif

#if IS_ENABLED(CONFIG_SPI_MASTER)

static struct spi_driver pcf2127_spi_driver;
static const struct spi_device_id pcf2127_spi_id[];

static int pcf2127_spi_probe(struct spi_device *spi)
{
	static struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.read_flag_mask = 0xa0,
		.write_flag_mask = 0x20,
	};
	struct regmap *regmap;
	const struct pcf21xx_config *variant;

	if (spi->dev.of_node) {
		variant = of_device_get_match_data(&spi->dev);
		if (!variant)
			return -ENODEV;
	} else {
		enum pcf21xx_type type = spi_get_device_id(spi)->driver_data;

		if (type >= PCF21XX_LAST_ID)
			return -ENODEV;
		variant = &pcf21xx_cfg[type];
	}

	if (variant->type == PCF2131) {
		config.read_flag_mask = 0x0;
		config.write_flag_mask = 0x0;
	}

	config.max_register = variant->max_register;

	regmap = devm_regmap_init_spi(spi, &config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return pcf2127_probe(&spi->dev, regmap, spi->irq, variant);
}

static const struct spi_device_id pcf2127_spi_id[] = {
	{ "pcf2127", PCF2127 },
	{ "pcf2129", PCF2129 },
	{ "pca2129", PCF2129 },
	{ "pcf2131", PCF2131 },
	{ }
};
MODULE_DEVICE_TABLE(spi, pcf2127_spi_id);

static struct spi_driver pcf2127_spi_driver = {
	.driver		= {
		.name	= "rtc-pcf2127-spi",
		.of_match_table = of_match_ptr(pcf2127_of_match),
	},
	.probe		= pcf2127_spi_probe,
	.id_table	= pcf2127_spi_id,
};

static int pcf2127_spi_register_driver(void)
{
	return spi_register_driver(&pcf2127_spi_driver);
}

static void pcf2127_spi_unregister_driver(void)
{
	spi_unregister_driver(&pcf2127_spi_driver);
}

#else

static int pcf2127_spi_register_driver(void)
{
	return 0;
}

static void pcf2127_spi_unregister_driver(void)
{
}

#endif

static int __init pcf2127_init(void)
{
	int ret;

	ret = pcf2127_i2c_register_driver();
	if (ret) {
		pr_err("Failed to register pcf2127 i2c driver: %d\n", ret);
		return ret;
	}

	ret = pcf2127_spi_register_driver();
	if (ret) {
		pr_err("Failed to register pcf2127 spi driver: %d\n", ret);
		pcf2127_i2c_unregister_driver();
	}

	return ret;
}
module_init(pcf2127_init)

static void __exit pcf2127_exit(void)
{
	pcf2127_spi_unregister_driver();
	pcf2127_i2c_unregister_driver();
}
module_exit(pcf2127_exit)

MODULE_AUTHOR("Renaud Cerrato <r.cerrato@til-technologies.fr>");
MODULE_DESCRIPTION("NXP PCF2127/29/31 RTC driver");
MODULE_LICENSE("GPL v2");
