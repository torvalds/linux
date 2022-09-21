// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Avionic Design GmbH
 */

#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/pm_wakeirq.h>

#define PCF8523_REG_CONTROL1 0x00
#define PCF8523_CONTROL1_CAP_SEL BIT(7)
#define PCF8523_CONTROL1_STOP    BIT(5)
#define PCF8523_CONTROL1_AIE    BIT(1)

#define PCF8523_REG_CONTROL2 0x01
#define PCF8523_CONTROL2_AF BIT(3)

#define PCF8523_REG_CONTROL3 0x02
#define PCF8523_CONTROL3_PM  GENMASK(7,5)
#define PCF8523_PM_STANDBY   0x7
#define PCF8523_CONTROL3_BLF BIT(2) /* battery low bit, read-only */
#define PCF8523_CONTROL3_BSF BIT(3)

#define PCF8523_REG_SECONDS  0x03
#define PCF8523_SECONDS_OS BIT(7)

#define PCF8523_REG_MINUTES  0x04
#define PCF8523_REG_HOURS    0x05
#define PCF8523_REG_DAYS     0x06
#define PCF8523_REG_WEEKDAYS 0x07
#define PCF8523_REG_MONTHS   0x08
#define PCF8523_REG_YEARS    0x09

#define PCF8523_REG_MINUTE_ALARM	0x0a
#define PCF8523_REG_HOUR_ALARM		0x0b
#define PCF8523_REG_DAY_ALARM		0x0c
#define PCF8523_REG_WEEKDAY_ALARM	0x0d
#define ALARM_DIS BIT(7)

#define PCF8523_REG_OFFSET   0x0e
#define PCF8523_OFFSET_MODE BIT(7)

#define PCF8523_TMR_CLKOUT_CTRL 0x0f

struct pcf8523 {
	struct rtc_device *rtc;
	struct regmap *regmap;
};

static int pcf8523_load_capacitance(struct pcf8523 *pcf8523, struct device_node *node)
{
	u32 load, value = 0;

	load = 12500;
	of_property_read_u32(node, "quartz-load-femtofarads", &load);

	switch (load) {
	default:
		dev_warn(&pcf8523->rtc->dev, "Unknown quartz-load-femtofarads value: %d. Assuming 12500",
			 load);
		fallthrough;
	case 12500:
		value |= PCF8523_CONTROL1_CAP_SEL;
		break;
	case 7000:
		break;
	}

	return regmap_update_bits(pcf8523->regmap, PCF8523_REG_CONTROL1,
				  PCF8523_CONTROL1_CAP_SEL, value);
}

static irqreturn_t pcf8523_irq(int irq, void *dev_id)
{
	struct pcf8523 *pcf8523 = dev_id;
	u32 value;
	int err;

	err = regmap_read(pcf8523->regmap, PCF8523_REG_CONTROL2, &value);
	if (err < 0)
		return IRQ_HANDLED;

	if (value & PCF8523_CONTROL2_AF) {
		value &= ~PCF8523_CONTROL2_AF;
		regmap_write(pcf8523->regmap, PCF8523_REG_CONTROL2, value);
		rtc_update_irq(pcf8523->rtc, 1, RTC_IRQF | RTC_AF);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int pcf8523_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	u8 regs[7];
	int err;

	err = regmap_bulk_read(pcf8523->regmap, PCF8523_REG_SECONDS, regs,
			       sizeof(regs));
	if (err < 0)
		return err;

	if (regs[0] & PCF8523_SECONDS_OS)
		return -EINVAL;

	tm->tm_sec = bcd2bin(regs[0] & 0x7f);
	tm->tm_min = bcd2bin(regs[1] & 0x7f);
	tm->tm_hour = bcd2bin(regs[2] & 0x3f);
	tm->tm_mday = bcd2bin(regs[3] & 0x3f);
	tm->tm_wday = regs[4] & 0x7;
	tm->tm_mon = bcd2bin(regs[5] & 0x1f) - 1;
	tm->tm_year = bcd2bin(regs[6]) + 100;

	return 0;
}

static int pcf8523_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	u8 regs[7];
	int err;

	err = regmap_update_bits(pcf8523->regmap, PCF8523_REG_CONTROL1,
				 PCF8523_CONTROL1_STOP, PCF8523_CONTROL1_STOP);
	if (err < 0)
		return err;

	/* This will purposely overwrite PCF8523_SECONDS_OS */
	regs[0] = bin2bcd(tm->tm_sec);
	regs[1] = bin2bcd(tm->tm_min);
	regs[2] = bin2bcd(tm->tm_hour);
	regs[3] = bin2bcd(tm->tm_mday);
	regs[4] = tm->tm_wday;
	regs[5] = bin2bcd(tm->tm_mon + 1);
	regs[6] = bin2bcd(tm->tm_year - 100);

	err = regmap_bulk_write(pcf8523->regmap, PCF8523_REG_SECONDS, regs,
				sizeof(regs));
	if (err < 0) {
		/*
		 * If the time cannot be set, restart the RTC anyway. Note
		 * that errors are ignored if the RTC cannot be started so
		 * that we have a chance to propagate the original error.
		 */
		regmap_update_bits(pcf8523->regmap, PCF8523_REG_CONTROL1,
				   PCF8523_CONTROL1_STOP, 0);
		return err;
	}

	return regmap_update_bits(pcf8523->regmap, PCF8523_REG_CONTROL1,
				 PCF8523_CONTROL1_STOP, 0);
}

static int pcf8523_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	u8 regs[4];
	u32 value;
	int err;

	err = regmap_bulk_read(pcf8523->regmap, PCF8523_REG_MINUTE_ALARM, regs,
			       sizeof(regs));
	if (err < 0)
		return err;

	tm->time.tm_sec = 0;
	tm->time.tm_min = bcd2bin(regs[0] & 0x7F);
	tm->time.tm_hour = bcd2bin(regs[1] & 0x3F);
	tm->time.tm_mday = bcd2bin(regs[2] & 0x3F);
	tm->time.tm_wday = bcd2bin(regs[3] & 0x7);

	err = regmap_read(pcf8523->regmap, PCF8523_REG_CONTROL1, &value);
	if (err < 0)
		return err;
	tm->enabled = !!(value & PCF8523_CONTROL1_AIE);

	err = regmap_read(pcf8523->regmap, PCF8523_REG_CONTROL2, &value);
	if (err < 0)
		return err;
	tm->pending = !!(value & PCF8523_CONTROL2_AF);

	return 0;
}

static int pcf8523_irq_enable(struct device *dev, unsigned int enabled)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);

	return regmap_update_bits(pcf8523->regmap, PCF8523_REG_CONTROL1,
				  PCF8523_CONTROL1_AIE, enabled ?
				  PCF8523_CONTROL1_AIE : 0);
}

static int pcf8523_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	u8 regs[5];
	int err;

	err = pcf8523_irq_enable(dev, 0);
	if (err)
		return err;

	err = regmap_write(pcf8523->regmap, PCF8523_REG_CONTROL2, 0);
	if (err < 0)
		return err;

	regs[0] = bin2bcd(tm->time.tm_min);
	regs[1] = bin2bcd(tm->time.tm_hour);
	regs[2] = bin2bcd(tm->time.tm_mday);
	regs[3] = ALARM_DIS;

	err = regmap_bulk_write(pcf8523->regmap, PCF8523_REG_MINUTE_ALARM, regs,
				sizeof(regs));
	if (err < 0)
		return err;

	if (tm->enabled)
		return pcf8523_irq_enable(dev, tm->enabled);

	return 0;
}

static int pcf8523_param_get(struct device *dev, struct rtc_param *param)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	int ret;
	u32 value;

	switch(param->param) {

	case RTC_PARAM_BACKUP_SWITCH_MODE:
		ret = regmap_read(pcf8523->regmap, PCF8523_REG_CONTROL3, &value);
		if (ret < 0)
			return ret;

		value = FIELD_GET(PCF8523_CONTROL3_PM, value);

		switch(value) {
		case 0x0:
		case 0x4:
			param->uvalue = RTC_BSM_LEVEL;
			break;
		case 0x1:
		case 0x5:
			param->uvalue = RTC_BSM_DIRECT;
			break;
		case PCF8523_PM_STANDBY:
			param->uvalue = RTC_BSM_STANDBY;
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

static int pcf8523_param_set(struct device *dev, struct rtc_param *param)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	u8 mode;

	switch(param->param) {
	case RTC_PARAM_BACKUP_SWITCH_MODE:
		switch (param->uvalue) {
		case RTC_BSM_DISABLED:
			mode = 0x2;
			break;
		case RTC_BSM_DIRECT:
			mode = 0x1;
			break;
		case RTC_BSM_LEVEL:
			mode = 0x0;
			break;
		case RTC_BSM_STANDBY:
			mode = PCF8523_PM_STANDBY;
			break;
		default:
			return -EINVAL;
		}

		return regmap_update_bits(pcf8523->regmap, PCF8523_REG_CONTROL3,
					  PCF8523_CONTROL3_PM,
					  FIELD_PREP(PCF8523_CONTROL3_PM, mode));

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int pcf8523_rtc_ioctl(struct device *dev, unsigned int cmd,
			     unsigned long arg)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	unsigned int flags = 0;
	u32 value;
	int ret;

	switch (cmd) {
	case RTC_VL_READ:
		ret = regmap_read(pcf8523->regmap, PCF8523_REG_CONTROL3, &value);
		if (ret < 0)
			return ret;

		if (value & PCF8523_CONTROL3_BLF)
			flags |= RTC_VL_BACKUP_LOW;

		ret = regmap_read(pcf8523->regmap, PCF8523_REG_SECONDS, &value);
		if (ret < 0)
			return ret;

		if (value & PCF8523_SECONDS_OS)
			flags |= RTC_VL_DATA_INVALID;

		return put_user(flags, (unsigned int __user *)arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static int pcf8523_rtc_read_offset(struct device *dev, long *offset)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	int err;
	u32 value;
	s8 val;

	err = regmap_read(pcf8523->regmap, PCF8523_REG_OFFSET, &value);
	if (err < 0)
		return err;

	/* sign extend the 7-bit offset value */
	val = value << 1;
	*offset = (value & PCF8523_OFFSET_MODE ? 4069 : 4340) * (val >> 1);

	return 0;
}

static int pcf8523_rtc_set_offset(struct device *dev, long offset)
{
	struct pcf8523 *pcf8523 = dev_get_drvdata(dev);
	long reg_m0, reg_m1;
	u32 value;

	reg_m0 = clamp(DIV_ROUND_CLOSEST(offset, 4340), -64L, 63L);
	reg_m1 = clamp(DIV_ROUND_CLOSEST(offset, 4069), -64L, 63L);

	if (abs(reg_m0 * 4340 - offset) < abs(reg_m1 * 4069 - offset))
		value = reg_m0 & 0x7f;
	else
		value = (reg_m1 & 0x7f) | PCF8523_OFFSET_MODE;

	return regmap_write(pcf8523->regmap, PCF8523_REG_OFFSET, value);
}

static const struct rtc_class_ops pcf8523_rtc_ops = {
	.read_time = pcf8523_rtc_read_time,
	.set_time = pcf8523_rtc_set_time,
	.read_alarm = pcf8523_rtc_read_alarm,
	.set_alarm = pcf8523_rtc_set_alarm,
	.alarm_irq_enable = pcf8523_irq_enable,
	.ioctl = pcf8523_rtc_ioctl,
	.read_offset = pcf8523_rtc_read_offset,
	.set_offset = pcf8523_rtc_set_offset,
	.param_get = pcf8523_param_get,
	.param_set = pcf8523_param_set,
};

static const struct regmap_config regmap_config = {
        .reg_bits = 8,
        .val_bits = 8,
        .max_register = 0x13,
};

static int pcf8523_probe(struct i2c_client *client)
{
	struct pcf8523 *pcf8523;
	struct rtc_device *rtc;
	bool wakeup_source = false;
	u32 value;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf8523 = devm_kzalloc(&client->dev, sizeof(struct pcf8523), GFP_KERNEL);
	if (!pcf8523)
		return -ENOMEM;

	pcf8523->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(pcf8523->regmap))
		return PTR_ERR(pcf8523->regmap);

	i2c_set_clientdata(client, pcf8523);

	rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);
	pcf8523->rtc = rtc;

	err = pcf8523_load_capacitance(pcf8523, client->dev.of_node);
	if (err < 0)
		dev_warn(&client->dev, "failed to set xtal load capacitance: %d",
			 err);

	err = regmap_read(pcf8523->regmap, PCF8523_REG_SECONDS, &value);
	if (err < 0)
		return err;

	if (value & PCF8523_SECONDS_OS) {
		err = regmap_read(pcf8523->regmap, PCF8523_REG_CONTROL3, &value);
		if (err < 0)
			return err;

		if (FIELD_GET(PCF8523_CONTROL3_PM, value) == PCF8523_PM_STANDBY) {
			err = regmap_write(pcf8523->regmap, PCF8523_REG_CONTROL3,
					   value & ~PCF8523_CONTROL3_PM);
			if (err < 0)
				return err;
		}
	}

	rtc->ops = &pcf8523_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;
	set_bit(RTC_FEATURE_ALARM_RES_MINUTE, rtc->features);
	clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->features);

	if (client->irq > 0) {
		err = regmap_write(pcf8523->regmap, PCF8523_TMR_CLKOUT_CTRL, 0x38);
		if (err < 0)
			return err;

		err = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, pcf8523_irq,
						IRQF_SHARED | IRQF_ONESHOT | IRQF_TRIGGER_LOW,
						dev_name(&rtc->dev), pcf8523);
		if (err)
			return err;

		dev_pm_set_wake_irq(&client->dev, client->irq);
	}

	wakeup_source = of_property_read_bool(client->dev.of_node, "wakeup-source");
	if (client->irq > 0 || wakeup_source)
		device_init_wakeup(&client->dev, true);

	return devm_rtc_register_device(rtc);
}

static const struct i2c_device_id pcf8523_id[] = {
	{ "pcf8523", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8523_id);

static const struct of_device_id pcf8523_of_match[] = {
	{ .compatible = "nxp,pcf8523" },
	{ .compatible = "microcrystal,rv8523" },
	{ }
};
MODULE_DEVICE_TABLE(of, pcf8523_of_match);

static struct i2c_driver pcf8523_driver = {
	.driver = {
		.name = "rtc-pcf8523",
		.of_match_table = pcf8523_of_match,
	},
	.probe_new = pcf8523_probe,
	.id_table = pcf8523_id,
};
module_i2c_driver(pcf8523_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("NXP PCF8523 RTC driver");
MODULE_LICENSE("GPL v2");
