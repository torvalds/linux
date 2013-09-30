/*
 * RTC driver for Maxim MAX77686/802
 *
 * Copyright (C) 2013 Google, Inc
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 *
 *  based on rtc-max8997.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77xxx-private.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

/* RTC Control Register */
#define BCD_EN_SHIFT			0
#define BCD_EN_MASK			(1 << BCD_EN_SHIFT)
#define MODEL24_SHIFT			1
#define MODEL24_MASK			(1 << MODEL24_SHIFT)
/* RTC Update Register1 */
#define RTC_UDR_SHIFT			0
#define RTC_UDR_MASK			(1 << RTC_UDR_SHIFT)
#define RTC_RBUDR_SHIFT			4
#define RTC_RBUDR_MASK			(1 << RTC_RBUDR_SHIFT)
/* WTSR and SMPL Register */
#define WTSRT_SHIFT			0
#define SMPLT_SHIFT			2
#define WTSR_EN_SHIFT			6
#define SMPL_EN_SHIFT			7
#define WTSRT_MASK			(3 << WTSRT_SHIFT)
#define SMPLT_MASK			(3 << SMPLT_SHIFT)
#define WTSR_EN_MASK			(1 << WTSR_EN_SHIFT)
#define SMPL_EN_MASK			(1 << SMPL_EN_SHIFT)
/* RTC Hour register */
#define HOUR_PM_SHIFT			6
#define HOUR_PM_MASK			(1 << HOUR_PM_SHIFT)
/* RTC Alarm Enable */
#define ALARM_ENABLE_SHIFT		7
#define ALARM_ENABLE_MASK		(1 << ALARM_ENABLE_SHIFT)

/* For the RTCAE1 register, we write this value to enable the alarm */
#define ALARM_ENABLE_VALUE		0x77

#define MAX77686_RTC_UPDATE_DELAY_US	16000
#define MAX77802_RTC_UPDATE_DELAY_US	200
#undef MAX77XXX_RTC_WTSR_SMPL

enum {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WEEKDAY,
	RTC_MONTH,
	RTC_YEAR,
	RTC_DATE,
	RTC_NR_TIME
};

enum {
	/*
	 * If this flag is set, the PMIC has a separate alarm enable
	 * register instead of using one bit out of each of the
	 * hour/min/sec/etc. registers.
	 *
	 * This also means that we get a full 8-bits for the year, so can
	 * report up to 199 years instead of 99.
	 */
	RTC_FLAG_HAS_ALARM_REG	= 1 << 0,
};

struct max77xxx_rtc_info {
	struct device		*dev;
	struct max77xxx_dev	*max77xxx;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;

	struct regmap		*regmap;
	const u8 *reg;	/* Indirection from MAX77XXX_RTC_... to reg num */

	int virq;
	int rtc_24hr_mode;
	int flags;
};

enum MAX77XXX_RTC_OP {
	MAX77XXX_RTC_WRITE,
	MAX77XXX_RTC_READ,
};

static inline int max77xxx_rtc_calculate_wday(u8 shifted)
{
	int counter = -1;
	while (shifted) {
		shifted >>= 1;
		counter++;
	}
	return counter;
}

static void max77xxx_rtc_data_to_tm(struct max77xxx_rtc_info *info, u8 *data,
				    struct rtc_time *tm, int rtc_24hr_mode)
{
	int mask = info->flags & RTC_FLAG_HAS_ALARM_REG ? 0xff : 0x7f;

	tm->tm_sec = data[RTC_SEC] & mask;
	tm->tm_min = data[RTC_MIN] & mask;
	if (rtc_24hr_mode)
		tm->tm_hour = data[RTC_HOUR] & 0x1f;
	else {
		tm->tm_hour = data[RTC_HOUR] & 0x0f;
		if (data[RTC_HOUR] & HOUR_PM_MASK)
			tm->tm_hour += 12;
	}

	tm->tm_wday = max77xxx_rtc_calculate_wday(data[RTC_WEEKDAY] & mask);
	tm->tm_mday = data[RTC_DATE] & 0x1f;
	tm->tm_mon = (data[RTC_MONTH] & 0x0f) - 1;

	/*
	 * TODO(sjg@chromium.org): Needs testing: possibly we should not
	 * add 100 here for the 802 since it can count up to 199. With this
	 * implementation we use the extra range to extend up to 2199.
	 */
	tm->tm_year = (data[RTC_YEAR] & mask) + 100;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;
}

static int max77xxx_rtc_tm_to_data(struct max77xxx_rtc_info *info,
				   struct rtc_time *tm, u8 *data)
{
	data[RTC_SEC] = tm->tm_sec;
	data[RTC_MIN] = tm->tm_min;
	data[RTC_HOUR] = tm->tm_hour;
	data[RTC_WEEKDAY] = 1 << tm->tm_wday;
	data[RTC_DATE] = tm->tm_mday;
	data[RTC_MONTH] = tm->tm_mon + 1;
	data[RTC_YEAR] = tm->tm_year >= 100 ? (tm->tm_year - 100) : 0;

	if (tm->tm_year < 100) {
		pr_warn("%s: MAX77XXX RTC cannot handle the year %d. Assume it's 2000.\n",
			__func__, 1900 + tm->tm_year);
		return -EINVAL;
	}

	return 0;
}

static int max77xxx_rtc_update(struct max77xxx_rtc_info *info,
	enum MAX77XXX_RTC_OP op)
{
	int ret;
	unsigned int data;

	if (op == MAX77XXX_RTC_WRITE)
		data = 1 << RTC_UDR_SHIFT;
	else
		data = 1 << RTC_RBUDR_SHIFT;

	ret = regmap_update_bits(info->max77xxx->rtc_regmap,
				 info->reg[MAX77XXX_RTC_UPDATE0], data, data);
	if (ret < 0)
		dev_err(info->dev, "%s: fail to write update reg(ret=%d, data=0x%x)\n",
				__func__, ret, data);
	else {
		/* Minimum delay required before RTC update. */
		int delay = info->max77xxx->type == TYPE_MAX77686 ?
				MAX77686_RTC_UPDATE_DELAY_US :
				MAX77802_RTC_UPDATE_DELAY_US;

		usleep_range(delay, delay * 2);
	}

	return ret;
}

static int max77xxx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max77xxx_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	mutex_lock(&info->lock);

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77xxx->rtc_regmap,
				info->reg[MAX77XXX_RTC_SEC], data, RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read time reg(%d)\n", __func__,
			ret);
		goto out;
	}

	max77xxx_rtc_data_to_tm(info, data, tm, info->rtc_24hr_mode);

	ret = rtc_valid_tm(tm);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77xxx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max77xxx_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max77xxx_rtc_tm_to_data(info, tm, data);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = regmap_bulk_write(info->max77xxx->rtc_regmap,
				 info->reg[MAX77XXX_RTC_SEC], data,
				 RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write time reg(%d)\n",
			__func__, ret);
		goto out;
	}

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77xxx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77xxx_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	unsigned int val;
	int i, ret;

	mutex_lock(&info->lock);

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77xxx->rtc_regmap,
			       info->reg[MAX77XXX_ALARM1_SEC], data,
			       RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	max77xxx_rtc_data_to_tm(info, data, &alrm->time, info->rtc_24hr_mode);

	alrm->enabled = 0;
	if (info->flags & RTC_FLAG_HAS_ALARM_REG) {
		ret = regmap_read(info->max77xxx->regmap,
				  info->reg[MAX77XXX_RTC_AE1], &val);
		if (ret < 0) {
			dev_err(info->dev, "%s:%d fail to read alarm enable(%d)\n",
				__func__, __LINE__, ret);
			goto out;
		}
		if (val)
			alrm->enabled = 1;
	} else {
		for (i = 0; i < RTC_NR_TIME; i++) {
			if (data[i] & ALARM_ENABLE_MASK) {
				alrm->enabled = 1;
				break;
			}
		}
	}

	alrm->pending = 0;
	ret = regmap_read(info->max77xxx->regmap, MAX77XXX_REG_STATUS2, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read status1 reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	if (val & (1 << 2)) /* RTCA1 */
		alrm->pending = 1;

out:
	mutex_unlock(&info->lock);
	return 0;
}

static int max77xxx_rtc_stop_alarm(struct max77xxx_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret, i;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n",
			 __func__);

	/*
	 * This matches the code in MAX77686 but it doesn't seem like
	 * it should be necessary to update when all we are doing is
	 * updating alarm registers. The buffered values should already
	 * be correct.
	 */
	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_READ);
	if (ret < 0)
		goto out;

	if (info->flags & RTC_FLAG_HAS_ALARM_REG) {
		ret = regmap_write(info->max77xxx->rtc_regmap,
				   info->reg[MAX77XXX_RTC_AE1], 0);
		if (ret < 0) {
			dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
			goto out;
		}
	} else {
		ret = regmap_bulk_read(info->max77xxx->rtc_regmap,
					info->reg[MAX77XXX_ALARM1_SEC], data,
			 RTC_NR_TIME);
		if (ret < 0) {
			dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
			goto out;
		}

		for (i = 0; i < RTC_NR_TIME; i++)
			data[i] &= ~ALARM_ENABLE_MASK;

		ret = regmap_bulk_write(info->max77xxx->rtc_regmap,
					info->reg[MAX77XXX_ALARM1_SEC], data,
					RTC_NR_TIME);
		if (ret < 0) {
			dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
			goto out;
		}
	}

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);
out:
	return ret;
}

static int max77xxx_rtc_start_alarm(struct max77xxx_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n",
			 __func__);

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_READ);
	if (ret < 0)
		goto out;

	if (info->flags & RTC_FLAG_HAS_ALARM_REG) {
		ret = regmap_write(info->max77xxx->rtc_regmap,
				   info->reg[MAX77XXX_RTC_AE1],
				   ALARM_ENABLE_VALUE);
	} else {
		int i;

		ret = regmap_bulk_read(info->max77xxx->rtc_regmap,
					info->reg[MAX77XXX_ALARM1_SEC], data,
					RTC_NR_TIME);
		if (ret < 0) {
			dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
					__func__, ret);
			goto out;
		}

		for (i = 0; i < RTC_NR_TIME; i++)
			data[i] |= 1 << ALARM_ENABLE_SHIFT;
		data[RTC_WEEKDAY] &= ~ALARM_ENABLE_MASK;

		ret = regmap_bulk_write(info->max77xxx->rtc_regmap,
					info->reg[MAX77XXX_ALARM1_SEC], data,
					RTC_NR_TIME);
	}
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);
out:
	return ret;
}

static int max77xxx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77xxx_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	/*
	 * As noted by dianders@chromium.org:
	 *
	 * This code is very inefficient, especially for 77686. We read all
	 * the registers, clear the high bit, write them all back, wait for
	 * an update, then set all new values (with the high bit cleared)
	 * and wait for an update. ...and if the alarm was enabled then we
	 * do it all a 3rd time. Whoa!
	 *
	 * Ideally this whole function could just have a single
	 * max77xxx_rtc_update() call.
	 */
	ret = max77xxx_rtc_tm_to_data(info, &alrm->time, data);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = max77xxx_rtc_stop_alarm(info);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_write(info->max77xxx->rtc_regmap,
				 info->reg[MAX77XXX_ALARM1_SEC], data,
				 RTC_NR_TIME);

	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);
	if (ret < 0)
		goto out;

	if (alrm->enabled)
		ret = max77xxx_rtc_start_alarm(info);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77xxx_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct max77xxx_rtc_info *info = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&info->lock);
	if (enabled)
		ret = max77xxx_rtc_start_alarm(info);
	else
		ret = max77xxx_rtc_stop_alarm(info);
	mutex_unlock(&info->lock);

	return ret;
}

static irqreturn_t max77xxx_rtc_alarm_irq(int irq, void *data)
{
	struct max77xxx_rtc_info *info = data;

	dev_info(info->dev, "%s:irq(%d)\n", __func__, irq);

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops max77xxx_rtc_ops = {
	.read_time = max77xxx_rtc_read_time,
	.set_time = max77xxx_rtc_set_time,
	.read_alarm = max77xxx_rtc_read_alarm,
	.set_alarm = max77xxx_rtc_set_alarm,
	.alarm_irq_enable = max77xxx_rtc_alarm_irq_enable,
};

#ifdef MAX77XXX_RTC_WTSR_SMPL
static void max77xxx_rtc_enable_wtsr(struct max77xxx_rtc_info *info,
				     bool enable)
{
	int ret;
	unsigned int val, mask;

	if (enable)
		val = (1 << WTSR_EN_SHIFT) | (3 << WTSRT_SHIFT);
	else
		val = 0;

	mask = WTSR_EN_MASK | WTSRT_MASK;

	dev_info(info->dev, "%s: %s WTSR\n", __func__,
			enable ? "enable" : "disable");

	ret = regmap_update_bits(info->max77xxx->rtc_regmap,
				 info->reg[MAX77XXX_WTSR_SMPL_CNTL], mask, val);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update WTSR reg(%d)\n",
				__func__, ret);
		return;
	}

	max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);
}

static void max77xxx_rtc_enable_smpl(struct max77xxx_rtc_info *info,
				     bool enable)
{
	int ret;
	unsigned int val, mask;

	if (enable)
		val = (1 << SMPL_EN_SHIFT) | (0 << SMPLT_SHIFT);
	else
		val = 0;

	mask = SMPL_EN_MASK | SMPLT_MASK;

	dev_info(info->dev, "%s: %s SMPL\n", __func__,
			enable ? "enable" : "disable");

	ret = regmap_update_bits(info->max77xxx->rtc_regmap,
				 info->reg[MAX77XXX_WTSR_SMPL_CNTL], mask,
				 val);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update SMPL reg(%d)\n",
				__func__, ret);
		return;
	}

	max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);

	val = 0;
	regmap_read(info->max77xxx->rtc_regmap,
		    info->reg[MAX77XXX_WTSR_SMPL_CNTL], &val);
	pr_info("%s: WTSR_SMPL(0x%02x)\n", __func__, val);
}
#endif /* MAX77XXX_RTC_WTSR_SMPL */

static int max77xxx_rtc_init_reg(struct max77xxx_rtc_info *info)
{
	u8 data[2];
	int ret;

	max77xxx_rtc_update(info, MAX77XXX_RTC_READ);

	/* Set RTC control register : Binary mode, 24hour mdoe */
	data[0] = (1 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);
	data[1] = (0 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);

	info->rtc_24hr_mode = 1;

	ret = regmap_bulk_write(info->max77xxx->rtc_regmap,
				info->reg[MAX77XXX_RTC_CONTROLM], data, 2);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write controlm reg(%d)\n",
				__func__, ret);
		return ret;
	}

	max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);

	/* Mask control register */
	max77xxx_rtc_update(info, MAX77XXX_RTC_READ);

	ret = regmap_update_bits(info->max77xxx->rtc_regmap,
				 info->reg[MAX77XXX_RTC_CONTROLM], 0x0, 0x3);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to mask CONTROLM reg(%d)\n",
				__func__, ret);
		return ret;
	}

	ret = max77xxx_rtc_update(info, MAX77XXX_RTC_WRITE);

	return ret;
}

/*
 * Sadly these two chips don't quite share register numbers. The elements
 * of these arrays must be in the same order, starting with the register
 * for MAX77XXX_RTC_CONTROLM in the first element.
 */
static const u8 max77686_map[MAX77XXX_REG_COUNT] = {
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
	0x1a, 0x1b, MAX77XXX_REG_NONE, MAX77XXX_REG_NONE,
};

static const u8 max77802_map[MAX77XXX_REG_COUNT] = {
	0xc2, 0xc3, 0xc4, MAX77XXX_REG_NONE, 0xc6, 0xc7, 0xc8, 0xc9,
	0xca, 0xcb, 0xcc, 0xcd, 0xcf, 0xd0, 0xd1, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
	0xdc, 0xdd, 0xce, 0xd6,
};

static bool max77686_rtc_is_accessible_reg(struct device *dev,
					   unsigned int reg)
{
	return (reg >= MAX77686_RTC_INT && reg < MAX77686_RTC_END);
}

static bool max77802_rtc_is_accessible_reg(struct device *dev,
					   unsigned int reg)
{
	return (reg >= MAX77802_RTC_INT && reg < MAX77802_RTC_END);
}

static bool max77xxx_rtc_is_precious_reg(enum max77xxx_types type,
					 unsigned int reg)
{
	const u8 *regs;

	if (type == TYPE_MAX77686)
		regs = max77686_map;
	else
		regs = max77802_map;
	return ((type == TYPE_MAX77686 && reg == MAX77686_RTC_INT) ||
		(type == TYPE_MAX77802 && reg == MAX77802_RTC_INT) ||
		reg == regs[MAX77XXX_RTC_UPDATE0] ||
		reg == regs[MAX77XXX_RTC_UPDATE1]);
}

static bool max77686_rtc_is_precious_reg(struct device *dev,
					 unsigned int reg)
{
	return max77xxx_rtc_is_precious_reg(TYPE_MAX77686, reg);
}

static bool max77802_rtc_is_precious_reg(struct device *dev,
					 unsigned int reg)
{
	return max77xxx_rtc_is_precious_reg(TYPE_MAX77802, reg);
}

static bool max77xxx_rtc_is_volatile_reg(enum max77xxx_types type,
					 unsigned int reg)
{
	const u8 *regs;

	if (type == TYPE_MAX77686)
		regs = max77686_map;
	else
		regs = max77802_map;
	return (max77xxx_rtc_is_precious_reg(type, reg) ||
		reg == regs[MAX77XXX_RTC_SEC] ||
		reg == regs[MAX77XXX_RTC_MIN] ||
		reg == regs[MAX77XXX_RTC_HOUR] ||
		reg == regs[MAX77XXX_RTC_WEEKDAY] ||
		reg == regs[MAX77XXX_RTC_MONTH] ||
		reg == regs[MAX77XXX_RTC_YEAR] ||
		reg == regs[MAX77XXX_RTC_DATE]);
}

static bool max77686_rtc_is_volatile_reg(struct device *dev,
					 unsigned int reg)
{
	return max77xxx_rtc_is_volatile_reg(TYPE_MAX77686, reg);
}

static bool max77802_rtc_is_volatile_reg(struct device *dev,
					 unsigned int reg)
{
	return max77xxx_rtc_is_volatile_reg(TYPE_MAX77802, reg);
}

static const struct regmap_config max77686_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = max77686_rtc_is_accessible_reg,
	.readable_reg = max77686_rtc_is_accessible_reg,
	.precious_reg = max77686_rtc_is_precious_reg,
	.volatile_reg = max77686_rtc_is_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_config max77802_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = max77802_rtc_is_accessible_reg,
	.readable_reg = max77802_rtc_is_accessible_reg,
	.precious_reg = max77802_rtc_is_precious_reg,
	.volatile_reg = max77802_rtc_is_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static int max77xxx_rtc_probe(struct platform_device *pdev)
{
	struct max77xxx_dev *max77xxx = dev_get_drvdata(pdev->dev.parent);
	struct max77xxx_rtc_info *info;
	int ret, virq;
	const struct regmap_config *config;

	dev_info(&pdev->dev, "%s\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(struct max77xxx_rtc_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->max77xxx = max77xxx;
	info->rtc = max77xxx->rtc;
	if (!info->rtc)
		info->rtc = max77xxx->i2c;
	if (max77xxx->type == TYPE_MAX77802) {
		info->reg = max77802_map;
		config = &max77802_rtc_regmap_config;
	} else {
		info->reg = max77686_map;
		config = &max77686_rtc_regmap_config;
	}
	info->max77xxx->rtc_regmap = regmap_init_i2c(info->rtc, config);
	if (IS_ERR(info->max77xxx->rtc_regmap)) {
		ret = PTR_ERR(info->max77xxx->rtc_regmap);
		dev_err(&pdev->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}
	platform_set_drvdata(pdev, info);
	if (max77xxx->type == TYPE_MAX77802)
		info->flags |= RTC_FLAG_HAS_ALARM_REG;

	ret = max77xxx_rtc_init_reg(info);

	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		return ret;
	}

#ifdef MAX77XXX_RTC_WTSR_SMPL
	max77xxx_rtc_enable_wtsr(info, true);
	max77xxx_rtc_enable_smpl(info, true);
#endif

	device_init_wakeup(&pdev->dev, 1);

	info->rtc_dev = rtc_device_register("max77xxx-rtc", &pdev->dev,
			&max77xxx_rtc_ops, THIS_MODULE);

	if (IS_ERR(info->rtc_dev)) {
		dev_info(&pdev->dev, "%s: fail\n", __func__);

		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n",
			ret);
		if (ret == 0)
			ret = -EINVAL;
		return ret;
	}
	virq = irq_create_mapping(max77xxx->irq_domain, MAX77XXX_RTCIRQ_RTCA1);
	if (!virq)
		goto err_irq;
	info->virq = virq;

	ret = request_threaded_irq(virq, NULL, max77xxx_rtc_alarm_irq, 0,
				   "rtc-alarm1", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request alarm1 IRQ: %d: %d\n",
			info->virq, ret);
		goto err_irq;
	}

	return 0;

err_irq:
	rtc_device_unregister(info->rtc_dev);
	return ret;
}

static int max77xxx_rtc_remove(struct platform_device *pdev)
{
	struct max77xxx_rtc_info *info = platform_get_drvdata(pdev);

	free_irq(info->virq, info);
	rtc_device_unregister(info->rtc_dev);

	return 0;
}

static void max77xxx_rtc_shutdown(struct platform_device *pdev)
{
#ifdef MAX77XXX_RTC_WTSR_SMPL
	struct max77xxx_rtc_info *info = platform_get_drvdata(pdev);
	int i;
	u8 val = 0;

	for (i = 0; i < 3; i++) {
		max77xxx_rtc_enable_wtsr(info, false);
		regmap_read(info->max77xxx->rtc_regmap,
			    info->reg[MAX77XXX_WTSR_SMPL_CNTL], &val);
		pr_info("%s: WTSR_SMPL reg(0x%02x)\n", __func__, val);
		if (val & WTSR_EN_MASK)
			pr_emerg("%s: fail to disable WTSR\n", __func__);
		else {
			pr_info("%s: success to disable WTSR\n", __func__);
			break;
		}
	}

	/* Disable SMPL when power off */
	max77xxx_rtc_enable_smpl(info, false);
#endif /* MAX77XXX_RTC_WTSR_SMPL */
}

static const struct platform_device_id rtc_id[] = {
	{ "max77686-rtc", TYPE_MAX77686 },
	{ "max77802-rtc", TYPE_MAX77802 },
	{},
};

static struct platform_driver max77xxx_rtc_driver = {
	.driver		= {
		.name	= "max77xxx-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= max77xxx_rtc_probe,
	.remove		= max77xxx_rtc_remove,
	.shutdown	= max77xxx_rtc_shutdown,
	.id_table	= rtc_id,
};

static int __init max77xxx_rtc_init(void)
{
	return platform_driver_register(&max77xxx_rtc_driver);
}
module_init(max77xxx_rtc_init);

static void __exit max77xxx_rtc_exit(void)
{
	platform_driver_unregister(&max77xxx_rtc_driver);
}
module_exit(max77xxx_rtc_exit);

MODULE_DESCRIPTION("Maxim MAX77XXX RTC driver");
MODULE_AUTHOR("Simon Glass <sjg@chromium.org>");
MODULE_LICENSE("GPL");
