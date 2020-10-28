// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Epson RTC module RX-8010 SJ
 *
 * Copyright(C) Timesys Corporation 2015
 * Copyright(C) General Electric Company 2015
 */

#include <linux/bcd.h>
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define RX8010_SEC		0x10
#define RX8010_MIN		0x11
#define RX8010_HOUR		0x12
#define RX8010_WDAY		0x13
#define RX8010_MDAY		0x14
#define RX8010_MONTH		0x15
#define RX8010_YEAR		0x16
#define RX8010_RESV17		0x17
#define RX8010_ALMIN		0x18
#define RX8010_ALHOUR		0x19
#define RX8010_ALWDAY		0x1A
#define RX8010_TCOUNT0		0x1B
#define RX8010_TCOUNT1		0x1C
#define RX8010_EXT		0x1D
#define RX8010_FLAG		0x1E
#define RX8010_CTRL		0x1F
/* 0x20 to 0x2F are user registers */
#define RX8010_RESV30		0x30
#define RX8010_RESV31		0x31
#define RX8010_IRQ		0x32

#define RX8010_EXT_WADA		BIT(3)

#define RX8010_FLAG_VLF		BIT(1)
#define RX8010_FLAG_AF		BIT(3)
#define RX8010_FLAG_TF		BIT(4)
#define RX8010_FLAG_UF		BIT(5)

#define RX8010_CTRL_AIE		BIT(3)
#define RX8010_CTRL_UIE		BIT(5)
#define RX8010_CTRL_STOP	BIT(6)
#define RX8010_CTRL_TEST	BIT(7)

#define RX8010_ALARM_AE		BIT(7)

static const struct i2c_device_id rx8010_id[] = {
	{ "rx8010", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rx8010_id);

static const struct of_device_id rx8010_of_match[] = {
	{ .compatible = "epson,rx8010" },
	{ }
};
MODULE_DEVICE_TABLE(of, rx8010_of_match);

struct rx8010_data {
	struct regmap *regs;
	struct rtc_device *rtc;
	u8 ctrlreg;
};

static irqreturn_t rx8010_irq_1_handler(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct rx8010_data *rx8010 = i2c_get_clientdata(client);
	int flagreg, err;

	mutex_lock(&rx8010->rtc->ops_lock);

	err = regmap_read(rx8010->regs, RX8010_FLAG, &flagreg);
	if (err) {
		mutex_unlock(&rx8010->rtc->ops_lock);
		return IRQ_NONE;
	}

	if (flagreg & RX8010_FLAG_VLF)
		dev_warn(&client->dev, "Frequency stop detected\n");

	if (flagreg & RX8010_FLAG_TF) {
		flagreg &= ~RX8010_FLAG_TF;
		rtc_update_irq(rx8010->rtc, 1, RTC_PF | RTC_IRQF);
	}

	if (flagreg & RX8010_FLAG_AF) {
		flagreg &= ~RX8010_FLAG_AF;
		rtc_update_irq(rx8010->rtc, 1, RTC_AF | RTC_IRQF);
	}

	if (flagreg & RX8010_FLAG_UF) {
		flagreg &= ~RX8010_FLAG_UF;
		rtc_update_irq(rx8010->rtc, 1, RTC_UF | RTC_IRQF);
	}

	err = regmap_write(rx8010->regs, RX8010_FLAG, flagreg);
	mutex_unlock(&rx8010->rtc->ops_lock);
	return err ? IRQ_NONE : IRQ_HANDLED;
}

static int rx8010_get_time(struct device *dev, struct rtc_time *dt)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 date[RX8010_YEAR - RX8010_SEC + 1];
	int flagreg, err;

	err = regmap_read(rx8010->regs, RX8010_FLAG, &flagreg);
	if (err)
		return err;

	if (flagreg & RX8010_FLAG_VLF) {
		dev_warn(dev, "Frequency stop detected\n");
		return -EINVAL;
	}

	err = regmap_bulk_read(rx8010->regs, RX8010_SEC, date, sizeof(date));
	if (err)
		return err;

	dt->tm_sec = bcd2bin(date[RX8010_SEC - RX8010_SEC] & 0x7f);
	dt->tm_min = bcd2bin(date[RX8010_MIN - RX8010_SEC] & 0x7f);
	dt->tm_hour = bcd2bin(date[RX8010_HOUR - RX8010_SEC] & 0x3f);
	dt->tm_mday = bcd2bin(date[RX8010_MDAY - RX8010_SEC] & 0x3f);
	dt->tm_mon = bcd2bin(date[RX8010_MONTH - RX8010_SEC] & 0x1f) - 1;
	dt->tm_year = bcd2bin(date[RX8010_YEAR - RX8010_SEC]) + 100;
	dt->tm_wday = ffs(date[RX8010_WDAY - RX8010_SEC] & 0x7f);

	return 0;
}

static int rx8010_set_time(struct device *dev, struct rtc_time *dt)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 date[RX8010_YEAR - RX8010_SEC + 1];
	int err;

	/* set STOP bit before changing clock/calendar */
	err = regmap_set_bits(rx8010->regs, RX8010_CTRL, RX8010_CTRL_STOP);
	if (err)
		return err;

	date[RX8010_SEC - RX8010_SEC] = bin2bcd(dt->tm_sec);
	date[RX8010_MIN - RX8010_SEC] = bin2bcd(dt->tm_min);
	date[RX8010_HOUR - RX8010_SEC] = bin2bcd(dt->tm_hour);
	date[RX8010_MDAY - RX8010_SEC] = bin2bcd(dt->tm_mday);
	date[RX8010_MONTH - RX8010_SEC] = bin2bcd(dt->tm_mon + 1);
	date[RX8010_YEAR - RX8010_SEC] = bin2bcd(dt->tm_year - 100);
	date[RX8010_WDAY - RX8010_SEC] = bin2bcd(1 << dt->tm_wday);

	err = regmap_bulk_write(rx8010->regs, RX8010_SEC, date, sizeof(date));
	if (err)
		return err;

	/* clear STOP bit after changing clock/calendar */
	err = regmap_clear_bits(rx8010->regs, RX8010_CTRL, RX8010_CTRL_STOP);
	if (err)
		return err;

	err = regmap_clear_bits(rx8010->regs, RX8010_FLAG, RX8010_FLAG_VLF);
	if (err)
		return err;

	return 0;
}

static int rx8010_init(struct device *dev)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 ctrl[2];
	int need_clear = 0, err;

	/* Initialize reserved registers as specified in datasheet */
	err = regmap_write(rx8010->regs, RX8010_RESV17, 0xD8);
	if (err)
		return err;

	err = regmap_write(rx8010->regs, RX8010_RESV30, 0x00);
	if (err)
		return err;

	err = regmap_write(rx8010->regs, RX8010_RESV31, 0x08);
	if (err)
		return err;

	err = regmap_write(rx8010->regs, RX8010_IRQ, 0x00);
	if (err)
		return err;

	err = regmap_bulk_read(rx8010->regs, RX8010_FLAG, ctrl, 2);
	if (err)
		return err;

	if (ctrl[0] & RX8010_FLAG_VLF)
		dev_warn(dev, "Frequency stop was detected\n");

	if (ctrl[0] & RX8010_FLAG_AF) {
		dev_warn(dev, "Alarm was detected\n");
		need_clear = 1;
	}

	if (ctrl[0] & RX8010_FLAG_TF)
		need_clear = 1;

	if (ctrl[0] & RX8010_FLAG_UF)
		need_clear = 1;

	if (need_clear) {
		ctrl[0] &= ~(RX8010_FLAG_AF | RX8010_FLAG_TF | RX8010_FLAG_UF);
		err = regmap_write(rx8010->regs, RX8010_FLAG, ctrl[0]);
		if (err)
			return err;
	}

	rx8010->ctrlreg = (ctrl[1] & ~RX8010_CTRL_TEST);

	return 0;
}

static int rx8010_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 alarmvals[3];
	int flagreg, err;

	err = regmap_bulk_read(rx8010->regs, RX8010_ALMIN, alarmvals, 3);
	if (err)
		return err;

	err = regmap_read(rx8010->regs, RX8010_FLAG, &flagreg);
	if (err)
		return err;

	t->time.tm_sec = 0;
	t->time.tm_min = bcd2bin(alarmvals[0] & 0x7f);
	t->time.tm_hour = bcd2bin(alarmvals[1] & 0x3f);

	if (!(alarmvals[2] & RX8010_ALARM_AE))
		t->time.tm_mday = bcd2bin(alarmvals[2] & 0x7f);

	t->enabled = !!(rx8010->ctrlreg & RX8010_CTRL_AIE);
	t->pending = (flagreg & RX8010_FLAG_AF) && t->enabled;

	return 0;
}

static int rx8010_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	u8 alarmvals[3];
	int err;

	if (rx8010->ctrlreg & (RX8010_CTRL_AIE | RX8010_CTRL_UIE)) {
		rx8010->ctrlreg &= ~(RX8010_CTRL_AIE | RX8010_CTRL_UIE);
		err = regmap_write(rx8010->regs, RX8010_CTRL, rx8010->ctrlreg);
		if (err)
			return err;
	}

	err = regmap_clear_bits(rx8010->regs, RX8010_FLAG, RX8010_FLAG_AF);
	if (err)
		return err;

	alarmvals[0] = bin2bcd(t->time.tm_min);
	alarmvals[1] = bin2bcd(t->time.tm_hour);
	alarmvals[2] = bin2bcd(t->time.tm_mday);

	err = regmap_bulk_write(rx8010->regs, RX8010_ALMIN, alarmvals, 2);
	if (err)
		return err;

	err = regmap_clear_bits(rx8010->regs, RX8010_EXT, RX8010_EXT_WADA);
	if (err)
		return err;

	if (alarmvals[2] == 0)
		alarmvals[2] |= RX8010_ALARM_AE;

	err = regmap_write(rx8010->regs, RX8010_ALWDAY, alarmvals[2]);
	if (err)
		return err;

	if (t->enabled) {
		if (rx8010->rtc->uie_rtctimer.enabled)
			rx8010->ctrlreg |= RX8010_CTRL_UIE;
		if (rx8010->rtc->aie_timer.enabled)
			rx8010->ctrlreg |=
				(RX8010_CTRL_AIE | RX8010_CTRL_UIE);

		err = regmap_write(rx8010->regs, RX8010_CTRL, rx8010->ctrlreg);
		if (err)
			return err;
	}

	return 0;
}

static int rx8010_alarm_irq_enable(struct device *dev,
				   unsigned int enabled)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	int err;
	u8 ctrl;

	ctrl = rx8010->ctrlreg;

	if (enabled) {
		if (rx8010->rtc->uie_rtctimer.enabled)
			ctrl |= RX8010_CTRL_UIE;
		if (rx8010->rtc->aie_timer.enabled)
			ctrl |= (RX8010_CTRL_AIE | RX8010_CTRL_UIE);
	} else {
		if (!rx8010->rtc->uie_rtctimer.enabled)
			ctrl &= ~RX8010_CTRL_UIE;
		if (!rx8010->rtc->aie_timer.enabled)
			ctrl &= ~RX8010_CTRL_AIE;
	}

	err = regmap_clear_bits(rx8010->regs, RX8010_FLAG, RX8010_FLAG_AF);
	if (err)
		return err;

	if (ctrl != rx8010->ctrlreg) {
		rx8010->ctrlreg = ctrl;
		err = regmap_write(rx8010->regs, RX8010_CTRL, rx8010->ctrlreg);
		if (err)
			return err;
	}

	return 0;
}

static int rx8010_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct rx8010_data *rx8010 = dev_get_drvdata(dev);
	int tmp, flagreg, err;

	switch (cmd) {
	case RTC_VL_READ:
		err = regmap_read(rx8010->regs, RX8010_FLAG, &flagreg);
		if (err)
			return err;

		tmp = flagreg & RX8010_FLAG_VLF ? RTC_VL_DATA_INVALID : 0;
		return put_user(tmp, (unsigned int __user *)arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static const struct rtc_class_ops rx8010_rtc_ops_default = {
	.read_time = rx8010_get_time,
	.set_time = rx8010_set_time,
	.ioctl = rx8010_ioctl,
};

static const struct rtc_class_ops rx8010_rtc_ops_alarm = {
	.read_time = rx8010_get_time,
	.set_time = rx8010_set_time,
	.ioctl = rx8010_ioctl,
	.read_alarm = rx8010_read_alarm,
	.set_alarm = rx8010_set_alarm,
	.alarm_irq_enable = rx8010_alarm_irq_enable,
};

static const struct regmap_config rx8010_regmap_config = {
	.name = "rx8010-rtc",
	.reg_bits = 8,
	.val_bits = 8,
};

static int rx8010_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct rx8010_data *rx8010;
	int err = 0;

	rx8010 = devm_kzalloc(dev, sizeof(*rx8010), GFP_KERNEL);
	if (!rx8010)
		return -ENOMEM;

	i2c_set_clientdata(client, rx8010);

	rx8010->regs = devm_regmap_init_i2c(client, &rx8010_regmap_config);
	if (IS_ERR(rx8010->regs))
		return PTR_ERR(rx8010->regs);

	err = rx8010_init(dev);
	if (err)
		return err;

	rx8010->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rx8010->rtc))
		return PTR_ERR(rx8010->rtc);

	if (client->irq > 0) {
		dev_info(dev, "IRQ %d supplied\n", client->irq);
		err = devm_request_threaded_irq(dev, client->irq, NULL,
						rx8010_irq_1_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"rx8010", client);
		if (err) {
			dev_err(dev, "unable to request IRQ\n");
			return err;
		}

		rx8010->rtc->ops = &rx8010_rtc_ops_alarm;
	} else {
		rx8010->rtc->ops = &rx8010_rtc_ops_default;
	}

	rx8010->rtc->max_user_freq = 1;
	rx8010->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rx8010->rtc->range_max = RTC_TIMESTAMP_END_2099;

	return rtc_register_device(rx8010->rtc);
}

static struct i2c_driver rx8010_driver = {
	.driver = {
		.name = "rtc-rx8010",
		.of_match_table = of_match_ptr(rx8010_of_match),
	},
	.probe_new	= rx8010_probe,
	.id_table	= rx8010_id,
};

module_i2c_driver(rx8010_driver);

MODULE_AUTHOR("Akshay Bhat <akshay.bhat@timesys.com>");
MODULE_DESCRIPTION("Epson RX8010SJ RTC driver");
MODULE_LICENSE("GPL v2");
