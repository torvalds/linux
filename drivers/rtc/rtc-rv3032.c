// SPDX-License-Identifier: GPL-2.0
/*
 * RTC driver for the Micro Crystal RV3032
 *
 * Copyright (C) 2020 Micro Crystal SA
 *
 * Alexandre Belloni <alexandre.belloni@bootlin.com>
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define RV3032_SEC			0x01
#define RV3032_MIN			0x02
#define RV3032_HOUR			0x03
#define RV3032_WDAY			0x04
#define RV3032_DAY			0x05
#define RV3032_MONTH			0x06
#define RV3032_YEAR			0x07
#define RV3032_ALARM_MIN		0x08
#define RV3032_ALARM_HOUR		0x09
#define RV3032_ALARM_DAY		0x0A
#define RV3032_STATUS			0x0D
#define RV3032_TLSB			0x0E
#define RV3032_TMSB			0x0F
#define RV3032_CTRL1			0x10
#define RV3032_CTRL2			0x11
#define RV3032_CTRL3			0x12
#define RV3032_TS_CTRL			0x13
#define RV3032_CLK_IRQ			0x14
#define RV3032_EEPROM_ADDR		0x3D
#define RV3032_EEPROM_DATA		0x3E
#define RV3032_EEPROM_CMD		0x3F
#define RV3032_RAM1			0x40
#define RV3032_PMU			0xC0
#define RV3032_OFFSET			0xC1
#define RV3032_CLKOUT1			0xC2
#define RV3032_CLKOUT2			0xC3
#define RV3032_TREF0			0xC4
#define RV3032_TREF1			0xC5

#define RV3032_STATUS_VLF		BIT(0)
#define RV3032_STATUS_PORF		BIT(1)
#define RV3032_STATUS_EVF		BIT(2)
#define RV3032_STATUS_AF		BIT(3)
#define RV3032_STATUS_TF		BIT(4)
#define RV3032_STATUS_UF		BIT(5)
#define RV3032_STATUS_TLF		BIT(6)
#define RV3032_STATUS_THF		BIT(7)

#define RV3032_TLSB_CLKF		BIT(1)
#define RV3032_TLSB_EEBUSY		BIT(2)
#define RV3032_TLSB_TEMP		GENMASK(7, 4)

#define RV3032_CLKOUT2_HFD_MSK		GENMASK(4, 0)
#define RV3032_CLKOUT2_FD_MSK		GENMASK(6, 5)
#define RV3032_CLKOUT2_OS		BIT(7)

#define RV3032_CTRL1_EERD		BIT(3)
#define RV3032_CTRL1_WADA		BIT(5)

#define RV3032_CTRL2_STOP		BIT(0)
#define RV3032_CTRL2_EIE		BIT(2)
#define RV3032_CTRL2_AIE		BIT(3)
#define RV3032_CTRL2_TIE		BIT(4)
#define RV3032_CTRL2_UIE		BIT(5)
#define RV3032_CTRL2_CLKIE		BIT(6)
#define RV3032_CTRL2_TSE		BIT(7)

#define RV3032_PMU_TCM			GENMASK(1, 0)
#define RV3032_PMU_TCR			GENMASK(3, 2)
#define RV3032_PMU_BSM			GENMASK(5, 4)
#define RV3032_PMU_NCLKE		BIT(6)

#define RV3032_PMU_BSM_DSM		1
#define RV3032_PMU_BSM_LSM		2

#define RV3032_OFFSET_MSK		GENMASK(5, 0)

#define RV3032_EVT_CTRL_TSR		BIT(2)

#define RV3032_EEPROM_CMD_UPDATE	0x11
#define RV3032_EEPROM_CMD_WRITE		0x21
#define RV3032_EEPROM_CMD_READ		0x22

#define RV3032_EEPROM_USER		0xCB

#define RV3032_EEBUSY_POLL		10000
#define RV3032_EEBUSY_TIMEOUT		100000

#define OFFSET_STEP_PPT			238419

struct rv3032_data {
	struct regmap *regmap;
	struct rtc_device *rtc;
	bool trickle_charger_set;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw clkout_hw;
#endif
};

static u16 rv3032_trickle_resistors[] = {1000, 2000, 7000, 11000};
static u16 rv3032_trickle_voltages[] = {0, 1750, 3000, 4400};

static int rv3032_exit_eerd(struct rv3032_data *rv3032, u32 eerd)
{
	if (eerd)
		return 0;

	return regmap_update_bits(rv3032->regmap, RV3032_CTRL1, RV3032_CTRL1_EERD, 0);
}

static int rv3032_enter_eerd(struct rv3032_data *rv3032, u32 *eerd)
{
	u32 ctrl1, status;
	int ret;

	ret = regmap_read(rv3032->regmap, RV3032_CTRL1, &ctrl1);
	if (ret)
		return ret;

	*eerd = ctrl1 & RV3032_CTRL1_EERD;
	if (*eerd)
		return 0;

	ret = regmap_update_bits(rv3032->regmap, RV3032_CTRL1,
				 RV3032_CTRL1_EERD, RV3032_CTRL1_EERD);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(rv3032->regmap, RV3032_TLSB, status,
				       !(status & RV3032_TLSB_EEBUSY),
				       RV3032_EEBUSY_POLL, RV3032_EEBUSY_TIMEOUT);
	if (ret) {
		rv3032_exit_eerd(rv3032, *eerd);

		return ret;
	}

	return 0;
}

static int rv3032_update_cfg(struct rv3032_data *rv3032, unsigned int reg,
			     unsigned int mask, unsigned int val)
{
	u32 status, eerd;
	int ret;

	ret = rv3032_enter_eerd(rv3032, &eerd);
	if (ret)
		return ret;

	ret = regmap_update_bits(rv3032->regmap, reg, mask, val);
	if (ret)
		goto exit_eerd;

	ret = regmap_write(rv3032->regmap, RV3032_EEPROM_CMD, RV3032_EEPROM_CMD_UPDATE);
	if (ret)
		goto exit_eerd;

	usleep_range(46000, RV3032_EEBUSY_TIMEOUT);

	ret = regmap_read_poll_timeout(rv3032->regmap, RV3032_TLSB, status,
				       !(status & RV3032_TLSB_EEBUSY),
				       RV3032_EEBUSY_POLL, RV3032_EEBUSY_TIMEOUT);

exit_eerd:
	rv3032_exit_eerd(rv3032, eerd);

	return ret;
}

static irqreturn_t rv3032_handle_irq(int irq, void *dev_id)
{
	struct rv3032_data *rv3032 = dev_id;
	unsigned long events = 0;
	u32 status = 0, ctrl = 0;

	if (regmap_read(rv3032->regmap, RV3032_STATUS, &status) < 0 ||
	    status == 0) {
		return IRQ_NONE;
	}

	if (status & RV3032_STATUS_TF) {
		status |= RV3032_STATUS_TF;
		ctrl |= RV3032_CTRL2_TIE;
		events |= RTC_PF;
	}

	if (status & RV3032_STATUS_AF) {
		status |= RV3032_STATUS_AF;
		ctrl |= RV3032_CTRL2_AIE;
		events |= RTC_AF;
	}

	if (status & RV3032_STATUS_UF) {
		status |= RV3032_STATUS_UF;
		ctrl |= RV3032_CTRL2_UIE;
		events |= RTC_UF;
	}

	if (events) {
		rtc_update_irq(rv3032->rtc, 1, events);
		regmap_update_bits(rv3032->regmap, RV3032_STATUS, status, 0);
		regmap_update_bits(rv3032->regmap, RV3032_CTRL2, ctrl, 0);
	}

	return IRQ_HANDLED;
}

static int rv3032_get_time(struct device *dev, struct rtc_time *tm)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	u8 date[7];
	int ret, status;

	ret = regmap_read(rv3032->regmap, RV3032_STATUS, &status);
	if (ret < 0)
		return ret;

	if (status & (RV3032_STATUS_PORF | RV3032_STATUS_VLF))
		return -EINVAL;

	ret = regmap_bulk_read(rv3032->regmap, RV3032_SEC, date, sizeof(date));
	if (ret)
		return ret;

	tm->tm_sec  = bcd2bin(date[0] & 0x7f);
	tm->tm_min  = bcd2bin(date[1] & 0x7f);
	tm->tm_hour = bcd2bin(date[2] & 0x3f);
	tm->tm_wday = date[3] & 0x7;
	tm->tm_mday = bcd2bin(date[4] & 0x3f);
	tm->tm_mon  = bcd2bin(date[5] & 0x1f) - 1;
	tm->tm_year = bcd2bin(date[6]) + 100;

	return 0;
}

static int rv3032_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	u8 date[7];
	int ret;

	date[0] = bin2bcd(tm->tm_sec);
	date[1] = bin2bcd(tm->tm_min);
	date[2] = bin2bcd(tm->tm_hour);
	date[3] = tm->tm_wday;
	date[4] = bin2bcd(tm->tm_mday);
	date[5] = bin2bcd(tm->tm_mon + 1);
	date[6] = bin2bcd(tm->tm_year - 100);

	ret = regmap_bulk_write(rv3032->regmap, RV3032_SEC, date,
				sizeof(date));
	if (ret)
		return ret;

	ret = regmap_update_bits(rv3032->regmap, RV3032_STATUS,
				 RV3032_STATUS_PORF | RV3032_STATUS_VLF, 0);

	return ret;
}

static int rv3032_get_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	u8 alarmvals[3];
	int status, ctrl, ret;

	ret = regmap_bulk_read(rv3032->regmap, RV3032_ALARM_MIN, alarmvals,
			       sizeof(alarmvals));
	if (ret)
		return ret;

	ret = regmap_read(rv3032->regmap, RV3032_STATUS, &status);
	if (ret < 0)
		return ret;

	ret = regmap_read(rv3032->regmap, RV3032_CTRL2, &ctrl);
	if (ret < 0)
		return ret;

	alrm->time.tm_sec  = 0;
	alrm->time.tm_min  = bcd2bin(alarmvals[0] & 0x7f);
	alrm->time.tm_hour = bcd2bin(alarmvals[1] & 0x3f);
	alrm->time.tm_mday = bcd2bin(alarmvals[2] & 0x3f);

	alrm->enabled = !!(ctrl & RV3032_CTRL2_AIE);
	alrm->pending = (status & RV3032_STATUS_AF) && alrm->enabled;

	return 0;
}

static int rv3032_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	u8 alarmvals[3];
	u8 ctrl = 0;
	int ret;

	ret = regmap_update_bits(rv3032->regmap, RV3032_CTRL2,
				 RV3032_CTRL2_AIE | RV3032_CTRL2_UIE, 0);
	if (ret)
		return ret;

	alarmvals[0] = bin2bcd(alrm->time.tm_min);
	alarmvals[1] = bin2bcd(alrm->time.tm_hour);
	alarmvals[2] = bin2bcd(alrm->time.tm_mday);

	ret = regmap_update_bits(rv3032->regmap, RV3032_STATUS,
				 RV3032_STATUS_AF, 0);
	if (ret)
		return ret;

	ret = regmap_bulk_write(rv3032->regmap, RV3032_ALARM_MIN, alarmvals,
				sizeof(alarmvals));
	if (ret)
		return ret;

	if (alrm->enabled) {
		if (rv3032->rtc->uie_rtctimer.enabled)
			ctrl |= RV3032_CTRL2_UIE;
		if (rv3032->rtc->aie_timer.enabled)
			ctrl |= RV3032_CTRL2_AIE;
	}

	ret = regmap_update_bits(rv3032->regmap, RV3032_CTRL2,
				 RV3032_CTRL2_UIE | RV3032_CTRL2_AIE, ctrl);

	return ret;
}

static int rv3032_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	int ctrl = 0, ret;

	if (enabled) {
		if (rv3032->rtc->uie_rtctimer.enabled)
			ctrl |= RV3032_CTRL2_UIE;
		if (rv3032->rtc->aie_timer.enabled)
			ctrl |= RV3032_CTRL2_AIE;
	}

	ret = regmap_update_bits(rv3032->regmap, RV3032_STATUS,
				 RV3032_STATUS_AF | RV3032_STATUS_UF, 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(rv3032->regmap, RV3032_CTRL2,
				 RV3032_CTRL2_UIE | RV3032_CTRL2_AIE, ctrl);
	if (ret)
		return ret;

	return 0;
}

static int rv3032_read_offset(struct device *dev, long *offset)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	int ret, value, steps;

	ret = regmap_read(rv3032->regmap, RV3032_OFFSET, &value);
	if (ret < 0)
		return ret;

	steps = sign_extend32(FIELD_GET(RV3032_OFFSET_MSK, value), 5);

	*offset = DIV_ROUND_CLOSEST(steps * OFFSET_STEP_PPT, 1000);

	return 0;
}

static int rv3032_set_offset(struct device *dev, long offset)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);

	offset = clamp(offset, -7629L, 7391L) * 1000;
	offset = DIV_ROUND_CLOSEST(offset, OFFSET_STEP_PPT);

	return rv3032_update_cfg(rv3032, RV3032_OFFSET, RV3032_OFFSET_MSK,
				 FIELD_PREP(RV3032_OFFSET_MSK, offset));
}

static int rv3032_param_get(struct device *dev, struct rtc_param *param)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	int ret;

	switch(param->param) {
		u32 value;

	case RTC_PARAM_BACKUP_SWITCH_MODE:
		ret = regmap_read(rv3032->regmap, RV3032_PMU, &value);
		if (ret < 0)
			return ret;

		value = FIELD_GET(RV3032_PMU_BSM, value);

		switch(value) {
		case RV3032_PMU_BSM_DSM:
			param->uvalue = RTC_BSM_DIRECT;
			break;
		case RV3032_PMU_BSM_LSM:
			param->uvalue = RTC_BSM_LEVEL;
			break;
		default:
			param->uvalue = RTC_BSM_DISABLED;
		}

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int rv3032_param_set(struct device *dev, struct rtc_param *param)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);

	switch(param->param) {
		u8 mode;
	case RTC_PARAM_BACKUP_SWITCH_MODE:
		if (rv3032->trickle_charger_set)
			return -EINVAL;

		switch (param->uvalue) {
		case RTC_BSM_DISABLED:
			mode = 0;
			break;
		case RTC_BSM_DIRECT:
			mode = RV3032_PMU_BSM_DSM;
			break;
		case RTC_BSM_LEVEL:
			mode = RV3032_PMU_BSM_LSM;
			break;
		default:
			return -EINVAL;
		}

		return rv3032_update_cfg(rv3032, RV3032_PMU, RV3032_PMU_BSM,
					 FIELD_PREP(RV3032_PMU_BSM, mode));

	default:
		return -EINVAL;
	}

	return 0;
}

static int rv3032_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	int status, val = 0, ret = 0;

	switch (cmd) {
	case RTC_VL_READ:
		ret = regmap_read(rv3032->regmap, RV3032_STATUS, &status);
		if (ret < 0)
			return ret;

		if (status & (RV3032_STATUS_PORF | RV3032_STATUS_VLF))
			val = RTC_VL_DATA_INVALID;
		return put_user(val, (unsigned int __user *)arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static int rv3032_nvram_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	return regmap_bulk_write(priv, RV3032_RAM1 + offset, val, bytes);
}

static int rv3032_nvram_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	return regmap_bulk_read(priv, RV3032_RAM1 + offset, val, bytes);
}

static int rv3032_eeprom_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct rv3032_data *rv3032 = priv;
	u32 status, eerd;
	int i, ret;
	u8 *buf = val;

	ret = rv3032_enter_eerd(rv3032, &eerd);
	if (ret)
		return ret;

	for (i = 0; i < bytes; i++) {
		ret = regmap_write(rv3032->regmap, RV3032_EEPROM_ADDR,
				   RV3032_EEPROM_USER + offset + i);
		if (ret)
			goto exit_eerd;

		ret = regmap_write(rv3032->regmap, RV3032_EEPROM_DATA, buf[i]);
		if (ret)
			goto exit_eerd;

		ret = regmap_write(rv3032->regmap, RV3032_EEPROM_CMD,
				   RV3032_EEPROM_CMD_WRITE);
		if (ret)
			goto exit_eerd;

		usleep_range(RV3032_EEBUSY_POLL, RV3032_EEBUSY_TIMEOUT);

		ret = regmap_read_poll_timeout(rv3032->regmap, RV3032_TLSB, status,
					       !(status & RV3032_TLSB_EEBUSY),
					       RV3032_EEBUSY_POLL, RV3032_EEBUSY_TIMEOUT);
		if (ret)
			goto exit_eerd;
	}

exit_eerd:
	rv3032_exit_eerd(rv3032, eerd);

	return ret;
}

static int rv3032_eeprom_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct rv3032_data *rv3032 = priv;
	u32 status, eerd, data;
	int i, ret;
	u8 *buf = val;

	ret = rv3032_enter_eerd(rv3032, &eerd);
	if (ret)
		return ret;

	for (i = 0; i < bytes; i++) {
		ret = regmap_write(rv3032->regmap, RV3032_EEPROM_ADDR,
				   RV3032_EEPROM_USER + offset + i);
		if (ret)
			goto exit_eerd;

		ret = regmap_write(rv3032->regmap, RV3032_EEPROM_CMD,
				   RV3032_EEPROM_CMD_READ);
		if (ret)
			goto exit_eerd;

		ret = regmap_read_poll_timeout(rv3032->regmap, RV3032_TLSB, status,
					       !(status & RV3032_TLSB_EEBUSY),
					       RV3032_EEBUSY_POLL, RV3032_EEBUSY_TIMEOUT);
		if (ret)
			goto exit_eerd;

		ret = regmap_read(rv3032->regmap, RV3032_EEPROM_DATA, &data);
		if (ret)
			goto exit_eerd;
		buf[i] = data;
	}

exit_eerd:
	rv3032_exit_eerd(rv3032, eerd);

	return ret;
}

static int rv3032_trickle_charger_setup(struct device *dev, struct rv3032_data *rv3032)
{
	u32 val, ohms, voltage;
	int i;

	val = FIELD_PREP(RV3032_PMU_TCM, 1) | FIELD_PREP(RV3032_PMU_BSM, RV3032_PMU_BSM_DSM);
	if (!device_property_read_u32(dev, "trickle-voltage-millivolt", &voltage)) {
		for (i = 0; i < ARRAY_SIZE(rv3032_trickle_voltages); i++)
			if (voltage == rv3032_trickle_voltages[i])
				break;
		if (i < ARRAY_SIZE(rv3032_trickle_voltages))
			val = FIELD_PREP(RV3032_PMU_TCM, i) |
			      FIELD_PREP(RV3032_PMU_BSM, RV3032_PMU_BSM_LSM);
	}

	if (device_property_read_u32(dev, "trickle-resistor-ohms", &ohms))
		return 0;

	for (i = 0; i < ARRAY_SIZE(rv3032_trickle_resistors); i++)
		if (ohms == rv3032_trickle_resistors[i])
			break;

	if (i >= ARRAY_SIZE(rv3032_trickle_resistors)) {
		dev_warn(dev, "invalid trickle resistor value\n");

		return 0;
	}

	rv3032->trickle_charger_set = true;

	return rv3032_update_cfg(rv3032, RV3032_PMU,
				 RV3032_PMU_TCR | RV3032_PMU_TCM | RV3032_PMU_BSM,
				 val | FIELD_PREP(RV3032_PMU_TCR, i));
}

#ifdef CONFIG_COMMON_CLK
#define clkout_hw_to_rv3032(hw) container_of(hw, struct rv3032_data, clkout_hw)

static int clkout_xtal_rates[] = {
	32768,
	1024,
	64,
	1,
};

#define RV3032_HFD_STEP 8192

static unsigned long rv3032_clkout_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	int clkout, ret;
	struct rv3032_data *rv3032 = clkout_hw_to_rv3032(hw);

	ret = regmap_read(rv3032->regmap, RV3032_CLKOUT2, &clkout);
	if (ret < 0)
		return 0;

	if (clkout & RV3032_CLKOUT2_OS) {
		unsigned long rate = FIELD_GET(RV3032_CLKOUT2_HFD_MSK, clkout) << 8;

		ret = regmap_read(rv3032->regmap, RV3032_CLKOUT1, &clkout);
		if (ret < 0)
			return 0;

		rate += clkout + 1;

		return rate * RV3032_HFD_STEP;
	}

	return clkout_xtal_rates[FIELD_GET(RV3032_CLKOUT2_FD_MSK, clkout)];
}

static long rv3032_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	int i, hfd;

	if (rate < RV3032_HFD_STEP)
		for (i = 0; i < ARRAY_SIZE(clkout_xtal_rates); i++)
			if (clkout_xtal_rates[i] <= rate)
				return clkout_xtal_rates[i];

	hfd = DIV_ROUND_CLOSEST(rate, RV3032_HFD_STEP);

	return RV3032_HFD_STEP * clamp(hfd, 0, 8192);
}

static int rv3032_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct rv3032_data *rv3032 = clkout_hw_to_rv3032(hw);
	u32 status, eerd;
	int i, hfd, ret;

	for (i = 0; i < ARRAY_SIZE(clkout_xtal_rates); i++) {
		if (clkout_xtal_rates[i] == rate) {
			return rv3032_update_cfg(rv3032, RV3032_CLKOUT2, 0xff,
						 FIELD_PREP(RV3032_CLKOUT2_FD_MSK, i));
		}
	}

	hfd = DIV_ROUND_CLOSEST(rate, RV3032_HFD_STEP);
	hfd = clamp(hfd, 1, 8192) - 1;

	ret = rv3032_enter_eerd(rv3032, &eerd);
	if (ret)
		return ret;

	ret = regmap_write(rv3032->regmap, RV3032_CLKOUT1, hfd & 0xff);
	if (ret)
		goto exit_eerd;

	ret = regmap_write(rv3032->regmap, RV3032_CLKOUT2, RV3032_CLKOUT2_OS |
			    FIELD_PREP(RV3032_CLKOUT2_HFD_MSK, hfd >> 8));
	if (ret)
		goto exit_eerd;

	ret = regmap_write(rv3032->regmap, RV3032_EEPROM_CMD, RV3032_EEPROM_CMD_UPDATE);
	if (ret)
		goto exit_eerd;

	usleep_range(46000, RV3032_EEBUSY_TIMEOUT);

	ret = regmap_read_poll_timeout(rv3032->regmap, RV3032_TLSB, status,
				       !(status & RV3032_TLSB_EEBUSY),
				       RV3032_EEBUSY_POLL, RV3032_EEBUSY_TIMEOUT);

exit_eerd:
	rv3032_exit_eerd(rv3032, eerd);

	return ret;
}

static int rv3032_clkout_prepare(struct clk_hw *hw)
{
	struct rv3032_data *rv3032 = clkout_hw_to_rv3032(hw);

	return rv3032_update_cfg(rv3032, RV3032_PMU, RV3032_PMU_NCLKE, 0);
}

static void rv3032_clkout_unprepare(struct clk_hw *hw)
{
	struct rv3032_data *rv3032 = clkout_hw_to_rv3032(hw);

	rv3032_update_cfg(rv3032, RV3032_PMU, RV3032_PMU_NCLKE, RV3032_PMU_NCLKE);
}

static int rv3032_clkout_is_prepared(struct clk_hw *hw)
{
	int val, ret;
	struct rv3032_data *rv3032 = clkout_hw_to_rv3032(hw);

	ret = regmap_read(rv3032->regmap, RV3032_PMU, &val);
	if (ret < 0)
		return ret;

	return !(val & RV3032_PMU_NCLKE);
}

static const struct clk_ops rv3032_clkout_ops = {
	.prepare = rv3032_clkout_prepare,
	.unprepare = rv3032_clkout_unprepare,
	.is_prepared = rv3032_clkout_is_prepared,
	.recalc_rate = rv3032_clkout_recalc_rate,
	.round_rate = rv3032_clkout_round_rate,
	.set_rate = rv3032_clkout_set_rate,
};

static int rv3032_clkout_register_clk(struct rv3032_data *rv3032,
				      struct i2c_client *client)
{
	int ret;
	struct clk *clk;
	struct clk_init_data init;
	struct device_node *node = client->dev.of_node;

	ret = regmap_update_bits(rv3032->regmap, RV3032_TLSB, RV3032_TLSB_CLKF, 0);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(rv3032->regmap, RV3032_CTRL2, RV3032_CTRL2_CLKIE, 0);
	if (ret < 0)
		return ret;

	ret = regmap_write(rv3032->regmap, RV3032_CLK_IRQ, 0);
	if (ret < 0)
		return ret;

	init.name = "rv3032-clkout";
	init.ops = &rv3032_clkout_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;
	rv3032->clkout_hw.init = &init;

	of_property_read_string(node, "clock-output-names", &init.name);

	clk = devm_clk_register(&client->dev, &rv3032->clkout_hw);
	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return 0;
}
#endif

static int rv3032_hwmon_read_temp(struct device *dev, long *mC)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);
	u8 buf[2];
	int temp, prev = 0;
	int ret;

	ret = regmap_bulk_read(rv3032->regmap, RV3032_TLSB, buf, sizeof(buf));
	if (ret)
		return ret;

	temp = sign_extend32(buf[1], 7) << 4;
	temp |= FIELD_GET(RV3032_TLSB_TEMP, buf[0]);

	/* No blocking or shadowing on RV3032_TLSB and RV3032_TMSB */
	do {
		prev = temp;

		ret = regmap_bulk_read(rv3032->regmap, RV3032_TLSB, buf, sizeof(buf));
		if (ret)
			return ret;

		temp = sign_extend32(buf[1], 7) << 4;
		temp |= FIELD_GET(RV3032_TLSB_TEMP, buf[0]);
	} while (temp != prev);

	*mC = (temp * 1000) / 16;

	return 0;
}

static umode_t rv3032_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static int rv3032_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *temp)
{
	int err;

	switch (attr) {
	case hwmon_temp_input:
		err = rv3032_hwmon_read_temp(dev, temp);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static const struct hwmon_channel_info * const rv3032_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST),
	NULL
};

static const struct hwmon_ops rv3032_hwmon_hwmon_ops = {
	.is_visible = rv3032_hwmon_is_visible,
	.read = rv3032_hwmon_read,
};

static const struct hwmon_chip_info rv3032_hwmon_chip_info = {
	.ops = &rv3032_hwmon_hwmon_ops,
	.info = rv3032_hwmon_info,
};

static void rv3032_hwmon_register(struct device *dev)
{
	struct rv3032_data *rv3032 = dev_get_drvdata(dev);

	if (!IS_REACHABLE(CONFIG_HWMON))
		return;

	devm_hwmon_device_register_with_info(dev, "rv3032", rv3032, &rv3032_hwmon_chip_info, NULL);
}

static const struct rtc_class_ops rv3032_rtc_ops = {
	.read_time = rv3032_get_time,
	.set_time = rv3032_set_time,
	.read_offset = rv3032_read_offset,
	.set_offset = rv3032_set_offset,
	.ioctl = rv3032_ioctl,
	.read_alarm = rv3032_get_alarm,
	.set_alarm = rv3032_set_alarm,
	.alarm_irq_enable = rv3032_alarm_irq_enable,
	.param_get = rv3032_param_get,
	.param_set = rv3032_param_set,
};

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xCA,
};

static int rv3032_probe(struct i2c_client *client)
{
	struct rv3032_data *rv3032;
	int ret, status;
	struct nvmem_config nvmem_cfg = {
		.name = "rv3032_nvram",
		.word_size = 1,
		.stride = 1,
		.size = 16,
		.type = NVMEM_TYPE_BATTERY_BACKED,
		.reg_read = rv3032_nvram_read,
		.reg_write = rv3032_nvram_write,
	};
	struct nvmem_config eeprom_cfg = {
		.name = "rv3032_eeprom",
		.word_size = 1,
		.stride = 1,
		.size = 32,
		.type = NVMEM_TYPE_EEPROM,
		.reg_read = rv3032_eeprom_read,
		.reg_write = rv3032_eeprom_write,
	};

	rv3032 = devm_kzalloc(&client->dev, sizeof(struct rv3032_data),
			      GFP_KERNEL);
	if (!rv3032)
		return -ENOMEM;

	rv3032->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(rv3032->regmap))
		return PTR_ERR(rv3032->regmap);

	i2c_set_clientdata(client, rv3032);

	ret = regmap_read(rv3032->regmap, RV3032_STATUS, &status);
	if (ret < 0)
		return ret;

	rv3032->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(rv3032->rtc))
		return PTR_ERR(rv3032->rtc);

	if (client->irq > 0) {
		unsigned long irqflags = IRQF_TRIGGER_LOW;

		if (dev_fwnode(&client->dev))
			irqflags = 0;

		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, rv3032_handle_irq,
						irqflags | IRQF_ONESHOT,
						"rv3032", rv3032);
		if (ret) {
			dev_warn(&client->dev, "unable to request IRQ, alarms disabled\n");
			client->irq = 0;
		}
	}
	if (!client->irq)
		clear_bit(RTC_FEATURE_ALARM, rv3032->rtc->features);

	ret = regmap_update_bits(rv3032->regmap, RV3032_CTRL1,
				 RV3032_CTRL1_WADA, RV3032_CTRL1_WADA);
	if (ret)
		return ret;

	rv3032_trickle_charger_setup(&client->dev, rv3032);

	set_bit(RTC_FEATURE_BACKUP_SWITCH_MODE, rv3032->rtc->features);
	set_bit(RTC_FEATURE_ALARM_RES_MINUTE, rv3032->rtc->features);

	rv3032->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rv3032->rtc->range_max = RTC_TIMESTAMP_END_2099;
	rv3032->rtc->ops = &rv3032_rtc_ops;
	ret = devm_rtc_register_device(rv3032->rtc);
	if (ret)
		return ret;

	nvmem_cfg.priv = rv3032->regmap;
	devm_rtc_nvmem_register(rv3032->rtc, &nvmem_cfg);
	eeprom_cfg.priv = rv3032;
	devm_rtc_nvmem_register(rv3032->rtc, &eeprom_cfg);

	rv3032->rtc->max_user_freq = 1;

#ifdef CONFIG_COMMON_CLK
	rv3032_clkout_register_clk(rv3032, client);
#endif

	rv3032_hwmon_register(&client->dev);

	return 0;
}

static const struct acpi_device_id rv3032_i2c_acpi_match[] = {
	{ "MCRY3032" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, rv3032_i2c_acpi_match);

static const __maybe_unused struct of_device_id rv3032_of_match[] = {
	{ .compatible = "microcrystal,rv3032", },
	{ }
};
MODULE_DEVICE_TABLE(of, rv3032_of_match);

static struct i2c_driver rv3032_driver = {
	.driver = {
		.name = "rtc-rv3032",
		.acpi_match_table = rv3032_i2c_acpi_match,
		.of_match_table = of_match_ptr(rv3032_of_match),
	},
	.probe		= rv3032_probe,
};
module_i2c_driver(rv3032_driver);

MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_DESCRIPTION("Micro Crystal RV3032 RTC driver");
MODULE_LICENSE("GPL v2");
