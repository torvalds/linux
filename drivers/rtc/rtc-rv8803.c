// SPDX-License-Identifier: GPL-2.0
/*
 * RTC driver for the Micro Crystal RV8803
 *
 * Copyright (C) 2015 Micro Crystal SA
 * Alexandre Belloni <alexandre.belloni@bootlin.com>
 *
 */

#include <linux/bcd.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/rtc.h>

#define RV8803_I2C_TRY_COUNT		4

#define RV8803_SEC			0x00
#define RV8803_MIN			0x01
#define RV8803_HOUR			0x02
#define RV8803_WEEK			0x03
#define RV8803_DAY			0x04
#define RV8803_MONTH			0x05
#define RV8803_YEAR			0x06
#define RV8803_RAM			0x07
#define RV8803_ALARM_MIN		0x08
#define RV8803_ALARM_HOUR		0x09
#define RV8803_ALARM_WEEK_OR_DAY	0x0A
#define RV8803_EXT			0x0D
#define RV8803_FLAG			0x0E
#define RV8803_CTRL			0x0F

#define RV8803_EXT_WADA			BIT(6)

#define RV8803_FLAG_V1F			BIT(0)
#define RV8803_FLAG_V2F			BIT(1)
#define RV8803_FLAG_AF			BIT(3)
#define RV8803_FLAG_TF			BIT(4)
#define RV8803_FLAG_UF			BIT(5)

#define RV8803_CTRL_RESET		BIT(0)

#define RV8803_CTRL_EIE			BIT(2)
#define RV8803_CTRL_AIE			BIT(3)
#define RV8803_CTRL_TIE			BIT(4)
#define RV8803_CTRL_UIE			BIT(5)

#define RX8900_BACKUP_CTRL		0x18
#define RX8900_FLAG_SWOFF		BIT(2)
#define RX8900_FLAG_VDETOFF		BIT(3)

enum rv8803_type {
	rv_8803,
	rx_8900
};

struct rv8803_data {
	struct i2c_client *client;
	struct rtc_device *rtc;
	struct mutex flags_lock;
	u8 ctrl;
	enum rv8803_type type;
};

static int rv8803_read_reg(const struct i2c_client *client, u8 reg)
{
	int try = RV8803_I2C_TRY_COUNT;
	s32 ret;

	/*
	 * There is a 61µs window during which the RTC does not acknowledge I2C
	 * transfers. In that case, ensure that there are multiple attempts.
	 */
	do
		ret = i2c_smbus_read_byte_data(client, reg);
	while ((ret == -ENXIO || ret == -EIO) && --try);
	if (ret < 0)
		dev_err(&client->dev, "Unable to read register 0x%02x\n", reg);

	return ret;
}

static int rv8803_read_regs(const struct i2c_client *client,
			    u8 reg, u8 count, u8 *values)
{
	int try = RV8803_I2C_TRY_COUNT;
	s32 ret;

	do
		ret = i2c_smbus_read_i2c_block_data(client, reg, count, values);
	while ((ret == -ENXIO || ret == -EIO) && --try);
	if (ret != count) {
		dev_err(&client->dev,
			"Unable to read registers 0x%02x..0x%02x\n",
			reg, reg + count - 1);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int rv8803_write_reg(const struct i2c_client *client, u8 reg, u8 value)
{
	int try = RV8803_I2C_TRY_COUNT;
	s32 ret;

	do
		ret = i2c_smbus_write_byte_data(client, reg, value);
	while ((ret == -ENXIO || ret == -EIO) && --try);
	if (ret)
		dev_err(&client->dev, "Unable to write register 0x%02x\n", reg);

	return ret;
}

static int rv8803_write_regs(const struct i2c_client *client,
			     u8 reg, u8 count, const u8 *values)
{
	int try = RV8803_I2C_TRY_COUNT;
	s32 ret;

	do
		ret = i2c_smbus_write_i2c_block_data(client, reg, count,
						     values);
	while ((ret == -ENXIO || ret == -EIO) && --try);
	if (ret)
		dev_err(&client->dev,
			"Unable to write registers 0x%02x..0x%02x\n",
			reg, reg + count - 1);

	return ret;
}

static irqreturn_t rv8803_handle_irq(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct rv8803_data *rv8803 = i2c_get_clientdata(client);
	unsigned long events = 0;
	int flags;

	mutex_lock(&rv8803->flags_lock);

	flags = rv8803_read_reg(client, RV8803_FLAG);
	if (flags <= 0) {
		mutex_unlock(&rv8803->flags_lock);
		return IRQ_NONE;
	}

	if (flags & RV8803_FLAG_V1F)
		dev_warn(&client->dev, "Voltage low, temperature compensation stopped.\n");

	if (flags & RV8803_FLAG_V2F)
		dev_warn(&client->dev, "Voltage low, data loss detected.\n");

	if (flags & RV8803_FLAG_TF) {
		flags &= ~RV8803_FLAG_TF;
		rv8803->ctrl &= ~RV8803_CTRL_TIE;
		events |= RTC_PF;
	}

	if (flags & RV8803_FLAG_AF) {
		flags &= ~RV8803_FLAG_AF;
		rv8803->ctrl &= ~RV8803_CTRL_AIE;
		events |= RTC_AF;
	}

	if (flags & RV8803_FLAG_UF) {
		flags &= ~RV8803_FLAG_UF;
		rv8803->ctrl &= ~RV8803_CTRL_UIE;
		events |= RTC_UF;
	}

	if (events) {
		rtc_update_irq(rv8803->rtc, 1, events);
		rv8803_write_reg(client, RV8803_FLAG, flags);
		rv8803_write_reg(rv8803->client, RV8803_CTRL, rv8803->ctrl);
	}

	mutex_unlock(&rv8803->flags_lock);

	return IRQ_HANDLED;
}

static int rv8803_get_time(struct device *dev, struct rtc_time *tm)
{
	struct rv8803_data *rv8803 = dev_get_drvdata(dev);
	u8 date1[7];
	u8 date2[7];
	u8 *date = date1;
	int ret, flags;

	flags = rv8803_read_reg(rv8803->client, RV8803_FLAG);
	if (flags < 0)
		return flags;

	if (flags & RV8803_FLAG_V2F) {
		dev_warn(dev, "Voltage low, data is invalid.\n");
		return -EINVAL;
	}

	ret = rv8803_read_regs(rv8803->client, RV8803_SEC, 7, date);
	if (ret)
		return ret;

	if ((date1[RV8803_SEC] & 0x7f) == bin2bcd(59)) {
		ret = rv8803_read_regs(rv8803->client, RV8803_SEC, 7, date2);
		if (ret)
			return ret;

		if ((date2[RV8803_SEC] & 0x7f) != bin2bcd(59))
			date = date2;
	}

	tm->tm_sec  = bcd2bin(date[RV8803_SEC] & 0x7f);
	tm->tm_min  = bcd2bin(date[RV8803_MIN] & 0x7f);
	tm->tm_hour = bcd2bin(date[RV8803_HOUR] & 0x3f);
	tm->tm_wday = ilog2(date[RV8803_WEEK] & 0x7f);
	tm->tm_mday = bcd2bin(date[RV8803_DAY] & 0x3f);
	tm->tm_mon  = bcd2bin(date[RV8803_MONTH] & 0x1f) - 1;
	tm->tm_year = bcd2bin(date[RV8803_YEAR]) + 100;

	return 0;
}

static int rv8803_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rv8803_data *rv8803 = dev_get_drvdata(dev);
	u8 date[7];
	int ctrl, flags, ret;

	ctrl = rv8803_read_reg(rv8803->client, RV8803_CTRL);
	if (ctrl < 0)
		return ctrl;

	/* Stop the clock */
	ret = rv8803_write_reg(rv8803->client, RV8803_CTRL,
			       ctrl | RV8803_CTRL_RESET);
	if (ret)
		return ret;

	date[RV8803_SEC]   = bin2bcd(tm->tm_sec);
	date[RV8803_MIN]   = bin2bcd(tm->tm_min);
	date[RV8803_HOUR]  = bin2bcd(tm->tm_hour);
	date[RV8803_WEEK]  = 1 << (tm->tm_wday);
	date[RV8803_DAY]   = bin2bcd(tm->tm_mday);
	date[RV8803_MONTH] = bin2bcd(tm->tm_mon + 1);
	date[RV8803_YEAR]  = bin2bcd(tm->tm_year - 100);

	ret = rv8803_write_regs(rv8803->client, RV8803_SEC, 7, date);
	if (ret)
		return ret;

	/* Restart the clock */
	ret = rv8803_write_reg(rv8803->client, RV8803_CTRL,
			       ctrl & ~RV8803_CTRL_RESET);
	if (ret)
		return ret;

	mutex_lock(&rv8803->flags_lock);

	flags = rv8803_read_reg(rv8803->client, RV8803_FLAG);
	if (flags < 0) {
		mutex_unlock(&rv8803->flags_lock);
		return flags;
	}

	ret = rv8803_write_reg(rv8803->client, RV8803_FLAG,
			       flags & ~(RV8803_FLAG_V1F | RV8803_FLAG_V2F));

	mutex_unlock(&rv8803->flags_lock);

	return ret;
}

static int rv8803_get_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rv8803_data *rv8803 = dev_get_drvdata(dev);
	struct i2c_client *client = rv8803->client;
	u8 alarmvals[3];
	int flags, ret;

	ret = rv8803_read_regs(client, RV8803_ALARM_MIN, 3, alarmvals);
	if (ret)
		return ret;

	flags = rv8803_read_reg(client, RV8803_FLAG);
	if (flags < 0)
		return flags;

	alrm->time.tm_sec  = 0;
	alrm->time.tm_min  = bcd2bin(alarmvals[0] & 0x7f);
	alrm->time.tm_hour = bcd2bin(alarmvals[1] & 0x3f);
	alrm->time.tm_mday = bcd2bin(alarmvals[2] & 0x3f);

	alrm->enabled = !!(rv8803->ctrl & RV8803_CTRL_AIE);
	alrm->pending = (flags & RV8803_FLAG_AF) && alrm->enabled;

	return 0;
}

static int rv8803_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rv8803_data *rv8803 = dev_get_drvdata(dev);
	u8 alarmvals[3];
	u8 ctrl[2];
	int ret, err;

	/* The alarm has no seconds, round up to nearest minute */
	if (alrm->time.tm_sec) {
		time64_t alarm_time = rtc_tm_to_time64(&alrm->time);

		alarm_time += 60 - alrm->time.tm_sec;
		rtc_time64_to_tm(alarm_time, &alrm->time);
	}

	mutex_lock(&rv8803->flags_lock);

	ret = rv8803_read_regs(client, RV8803_FLAG, 2, ctrl);
	if (ret) {
		mutex_unlock(&rv8803->flags_lock);
		return ret;
	}

	alarmvals[0] = bin2bcd(alrm->time.tm_min);
	alarmvals[1] = bin2bcd(alrm->time.tm_hour);
	alarmvals[2] = bin2bcd(alrm->time.tm_mday);

	if (rv8803->ctrl & (RV8803_CTRL_AIE | RV8803_CTRL_UIE)) {
		rv8803->ctrl &= ~(RV8803_CTRL_AIE | RV8803_CTRL_UIE);
		err = rv8803_write_reg(rv8803->client, RV8803_CTRL,
				       rv8803->ctrl);
		if (err) {
			mutex_unlock(&rv8803->flags_lock);
			return err;
		}
	}

	ctrl[1] &= ~RV8803_FLAG_AF;
	err = rv8803_write_reg(rv8803->client, RV8803_FLAG, ctrl[1]);
	mutex_unlock(&rv8803->flags_lock);
	if (err)
		return err;

	err = rv8803_write_regs(rv8803->client, RV8803_ALARM_MIN, 3, alarmvals);
	if (err)
		return err;

	if (alrm->enabled) {
		if (rv8803->rtc->uie_rtctimer.enabled)
			rv8803->ctrl |= RV8803_CTRL_UIE;
		if (rv8803->rtc->aie_timer.enabled)
			rv8803->ctrl |= RV8803_CTRL_AIE;

		err = rv8803_write_reg(rv8803->client, RV8803_CTRL,
				       rv8803->ctrl);
		if (err)
			return err;
	}

	return 0;
}

static int rv8803_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rv8803_data *rv8803 = dev_get_drvdata(dev);
	int ctrl, flags, err;

	ctrl = rv8803->ctrl;

	if (enabled) {
		if (rv8803->rtc->uie_rtctimer.enabled)
			ctrl |= RV8803_CTRL_UIE;
		if (rv8803->rtc->aie_timer.enabled)
			ctrl |= RV8803_CTRL_AIE;
	} else {
		if (!rv8803->rtc->uie_rtctimer.enabled)
			ctrl &= ~RV8803_CTRL_UIE;
		if (!rv8803->rtc->aie_timer.enabled)
			ctrl &= ~RV8803_CTRL_AIE;
	}

	mutex_lock(&rv8803->flags_lock);
	flags = rv8803_read_reg(client, RV8803_FLAG);
	if (flags < 0) {
		mutex_unlock(&rv8803->flags_lock);
		return flags;
	}
	flags &= ~(RV8803_FLAG_AF | RV8803_FLAG_UF);
	err = rv8803_write_reg(client, RV8803_FLAG, flags);
	mutex_unlock(&rv8803->flags_lock);
	if (err)
		return err;

	if (ctrl != rv8803->ctrl) {
		rv8803->ctrl = ctrl;
		err = rv8803_write_reg(client, RV8803_CTRL, rv8803->ctrl);
		if (err)
			return err;
	}

	return 0;
}

static int rv8803_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rv8803_data *rv8803 = dev_get_drvdata(dev);
	int flags, ret = 0;

	switch (cmd) {
	case RTC_VL_READ:
		flags = rv8803_read_reg(client, RV8803_FLAG);
		if (flags < 0)
			return flags;

		if (flags & RV8803_FLAG_V1F)
			dev_warn(&client->dev, "Voltage low, temperature compensation stopped.\n");

		if (flags & RV8803_FLAG_V2F)
			dev_warn(&client->dev, "Voltage low, data loss detected.\n");

		flags &= RV8803_FLAG_V1F | RV8803_FLAG_V2F;

		if (copy_to_user((void __user *)arg, &flags, sizeof(int)))
			return -EFAULT;

		return 0;

	case RTC_VL_CLR:
		mutex_lock(&rv8803->flags_lock);
		flags = rv8803_read_reg(client, RV8803_FLAG);
		if (flags < 0) {
			mutex_unlock(&rv8803->flags_lock);
			return flags;
		}

		flags &= ~(RV8803_FLAG_V1F | RV8803_FLAG_V2F);
		ret = rv8803_write_reg(client, RV8803_FLAG, flags);
		mutex_unlock(&rv8803->flags_lock);
		if (ret)
			return ret;

		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

static int rv8803_nvram_write(void *priv, unsigned int offset, void *val,
			      size_t bytes)
{
	int ret;

	ret = rv8803_write_reg(priv, RV8803_RAM, *(u8 *)val);
	if (ret)
		return ret;

	return 0;
}

static int rv8803_nvram_read(void *priv, unsigned int offset,
			     void *val, size_t bytes)
{
	int ret;

	ret = rv8803_read_reg(priv, RV8803_RAM);
	if (ret < 0)
		return ret;

	*(u8 *)val = ret;

	return 0;
}

static struct rtc_class_ops rv8803_rtc_ops = {
	.read_time = rv8803_get_time,
	.set_time = rv8803_set_time,
	.ioctl = rv8803_ioctl,
};

static int rx8900_trickle_charger_init(struct rv8803_data *rv8803)
{
	struct i2c_client *client = rv8803->client;
	struct device_node *node = client->dev.of_node;
	int err;
	u8 flags;

	if (!node)
		return 0;

	if (rv8803->type != rx_8900)
		return 0;

	err = i2c_smbus_read_byte_data(rv8803->client, RX8900_BACKUP_CTRL);
	if (err < 0)
		return err;

	flags = ~(RX8900_FLAG_VDETOFF | RX8900_FLAG_SWOFF) & (u8)err;

	if (of_property_read_bool(node, "epson,vdet-disable"))
		flags |= RX8900_FLAG_VDETOFF;

	if (of_property_read_bool(node, "trickle-diode-disable"))
		flags |= RX8900_FLAG_SWOFF;

	return i2c_smbus_write_byte_data(rv8803->client, RX8900_BACKUP_CTRL,
					 flags);
}

static int rv8803_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct rv8803_data *rv8803;
	int err, flags;
	struct nvmem_config nvmem_cfg = {
		.name = "rv8803_nvram",
		.word_size = 1,
		.stride = 1,
		.size = 1,
		.reg_read = rv8803_nvram_read,
		.reg_write = rv8803_nvram_write,
		.priv = client,
	};

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&adapter->dev, "doesn't support I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_I2C_BLOCK\n");
		return -EIO;
	}

	rv8803 = devm_kzalloc(&client->dev, sizeof(struct rv8803_data),
			      GFP_KERNEL);
	if (!rv8803)
		return -ENOMEM;

	mutex_init(&rv8803->flags_lock);
	rv8803->client = client;
	if (client->dev.of_node)
		rv8803->type = (enum rv8803_type)
			of_device_get_match_data(&client->dev);
	else
		rv8803->type = id->driver_data;
	i2c_set_clientdata(client, rv8803);

	flags = rv8803_read_reg(client, RV8803_FLAG);
	if (flags < 0)
		return flags;

	if (flags & RV8803_FLAG_V1F)
		dev_warn(&client->dev, "Voltage low, temperature compensation stopped.\n");

	if (flags & RV8803_FLAG_V2F)
		dev_warn(&client->dev, "Voltage low, data loss detected.\n");

	if (flags & RV8803_FLAG_AF)
		dev_warn(&client->dev, "An alarm maybe have been missed.\n");

	rv8803->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(rv8803->rtc)) {
		return PTR_ERR(rv8803->rtc);
	}

	if (client->irq > 0) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, rv8803_handle_irq,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"rv8803", client);
		if (err) {
			dev_warn(&client->dev, "unable to request IRQ, alarms disabled\n");
			client->irq = 0;
		} else {
			rv8803_rtc_ops.read_alarm = rv8803_get_alarm;
			rv8803_rtc_ops.set_alarm = rv8803_set_alarm;
			rv8803_rtc_ops.alarm_irq_enable = rv8803_alarm_irq_enable;
		}
	}

	err = rv8803_write_reg(rv8803->client, RV8803_EXT, RV8803_EXT_WADA);
	if (err)
		return err;

	err = rx8900_trickle_charger_init(rv8803);
	if (err) {
		dev_err(&client->dev, "failed to init charger\n");
		return err;
	}

	rv8803->rtc->ops = &rv8803_rtc_ops;
	rv8803->rtc->nvram_old_abi = true;
	rv8803->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rv8803->rtc->range_max = RTC_TIMESTAMP_END_2099;
	err = rtc_register_device(rv8803->rtc);
	if (err)
		return err;

	rtc_nvmem_register(rv8803->rtc, &nvmem_cfg);

	rv8803->rtc->max_user_freq = 1;

	return 0;
}

static const struct i2c_device_id rv8803_id[] = {
	{ "rv8803", rv_8803 },
	{ "rx8803", rv_8803 },
	{ "rx8900", rx_8900 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rv8803_id);

static const struct of_device_id rv8803_of_match[] = {
	{
		.compatible = "microcrystal,rv8803",
		.data = (void *)rv_8803
	},
	{
		.compatible = "epson,rx8803",
		.data = (void *)rv_8803
	},
	{
		.compatible = "epson,rx8900",
		.data = (void *)rx_8900
	},
	{ }
};
MODULE_DEVICE_TABLE(of, rv8803_of_match);

static struct i2c_driver rv8803_driver = {
	.driver = {
		.name = "rtc-rv8803",
		.of_match_table = of_match_ptr(rv8803_of_match),
	},
	.probe		= rv8803_probe,
	.id_table	= rv8803_id,
};
module_i2c_driver(rv8803_driver);

MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_DESCRIPTION("Micro Crystal RV8803 RTC driver");
MODULE_LICENSE("GPL v2");
