// SPDX-License-Identifier: GPL-2.0
/*
 * An I2C driver for the PCF85063 RTC
 * Copyright 2014 Rose Technology
 *
 * Author: Søren Andersen <san@rosetechnology.dk>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * Copyright (C) 2019 Micro Crystal AG
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 */
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>

/*
 * Information for this driver was pulled from the following datasheets.
 *
 *  http://www.nxp.com/documents/data_sheet/PCF85063A.pdf
 *  http://www.nxp.com/documents/data_sheet/PCF85063TP.pdf
 *
 *  PCF85063A -- Rev. 6 — 18 November 2015
 *  PCF85063TP -- Rev. 4 — 6 May 2015
 *
 *  https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-8263-C7_App-Manual.pdf
 *  RV8263 -- Rev. 1.0 — January 2019
 */

#define PCF85063_REG_CTRL1		0x00 /* status */
#define PCF85063_REG_CTRL1_CAP_SEL	BIT(0)
#define PCF85063_REG_CTRL1_STOP		BIT(5)

#define PCF85063_REG_CTRL2		0x01
#define PCF85063_CTRL2_AF		BIT(6)
#define PCF85063_CTRL2_AIE		BIT(7)

#define PCF85063_REG_OFFSET		0x02
#define PCF85063_OFFSET_SIGN_BIT	6	/* 2's complement sign bit */
#define PCF85063_OFFSET_MODE		BIT(7)
#define PCF85063_OFFSET_STEP0		4340
#define PCF85063_OFFSET_STEP1		4069

#define PCF85063_REG_RAM		0x03

#define PCF85063_REG_SC			0x04 /* datetime */
#define PCF85063_REG_SC_OS		0x80

#define PCF85063_REG_ALM_S		0x0b
#define PCF85063_AEN			BIT(7)

struct pcf85063_config {
	struct regmap_config regmap;
	unsigned has_alarms:1;
	unsigned force_cap_7000:1;
};

struct pcf85063 {
	struct rtc_device	*rtc;
	struct regmap		*regmap;
};

static int pcf85063_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	int rc;
	u8 regs[7];

	/*
	 * while reading, the time/date registers are blocked and not updated
	 * anymore until the access is finished. To not lose a second
	 * event, the access must be finished within one second. So, read all
	 * time/date registers in one turn.
	 */
	rc = regmap_bulk_read(pcf85063->regmap, PCF85063_REG_SC, regs,
			      sizeof(regs));
	if (rc)
		return rc;

	/* if the clock has lost its power it makes no sense to use its time */
	if (regs[0] & PCF85063_REG_SC_OS) {
		dev_warn(&pcf85063->rtc->dev, "Power loss detected, invalid time\n");
		return -EINVAL;
	}

	tm->tm_sec = bcd2bin(regs[0] & 0x7F);
	tm->tm_min = bcd2bin(regs[1] & 0x7F);
	tm->tm_hour = bcd2bin(regs[2] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(regs[3] & 0x3F);
	tm->tm_wday = regs[4] & 0x07;
	tm->tm_mon = bcd2bin(regs[5] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(regs[6]);
	tm->tm_year += 100;

	return 0;
}

static int pcf85063_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	int rc;
	u8 regs[7];

	/*
	 * to accurately set the time, reset the divider chain and keep it in
	 * reset state until all time/date registers are written
	 */
	rc = regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL1,
				PCF85063_REG_CTRL1_STOP,
				PCF85063_REG_CTRL1_STOP);
	if (rc)
		return rc;

	/* hours, minutes and seconds */
	regs[0] = bin2bcd(tm->tm_sec) & 0x7F; /* clear OS flag */

	regs[1] = bin2bcd(tm->tm_min);
	regs[2] = bin2bcd(tm->tm_hour);

	/* Day of month, 1 - 31 */
	regs[3] = bin2bcd(tm->tm_mday);

	/* Day, 0 - 6 */
	regs[4] = tm->tm_wday & 0x07;

	/* month, 1 - 12 */
	regs[5] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	regs[6] = bin2bcd(tm->tm_year - 100);

	/* write all registers at once */
	rc = regmap_bulk_write(pcf85063->regmap, PCF85063_REG_SC,
			       regs, sizeof(regs));
	if (rc)
		return rc;

	/*
	 * Write the control register as a separate action since the size of
	 * the register space is different between the PCF85063TP and
	 * PCF85063A devices.  The rollover point can not be used.
	 */
	return regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL1,
				  PCF85063_REG_CTRL1_STOP, 0);
}

static int pcf85063_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	u8 buf[4];
	unsigned int val;
	int ret;

	ret = regmap_bulk_read(pcf85063->regmap, PCF85063_REG_ALM_S,
			       buf, sizeof(buf));
	if (ret)
		return ret;

	alrm->time.tm_sec = bcd2bin(buf[0]);
	alrm->time.tm_min = bcd2bin(buf[1]);
	alrm->time.tm_hour = bcd2bin(buf[2]);
	alrm->time.tm_mday = bcd2bin(buf[3]);

	ret = regmap_read(pcf85063->regmap, PCF85063_REG_CTRL2, &val);
	if (ret)
		return ret;

	alrm->enabled =  !!(val & PCF85063_CTRL2_AIE);

	return 0;
}

static int pcf85063_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	u8 buf[5];
	int ret;

	buf[0] = bin2bcd(alrm->time.tm_sec);
	buf[1] = bin2bcd(alrm->time.tm_min);
	buf[2] = bin2bcd(alrm->time.tm_hour);
	buf[3] = bin2bcd(alrm->time.tm_mday);
	buf[4] = PCF85063_AEN; /* Do not match on week day */

	ret = regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL2,
				 PCF85063_CTRL2_AIE | PCF85063_CTRL2_AF, 0);
	if (ret)
		return ret;

	ret = regmap_bulk_write(pcf85063->regmap, PCF85063_REG_ALM_S,
				buf, sizeof(buf));
	if (ret)
		return ret;

	return regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL2,
				  PCF85063_CTRL2_AIE | PCF85063_CTRL2_AF,
				  alrm->enabled ? PCF85063_CTRL2_AIE | PCF85063_CTRL2_AF : PCF85063_CTRL2_AF);
}

static int pcf85063_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);

	return regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL2,
				  PCF85063_CTRL2_AIE,
				  enabled ? PCF85063_CTRL2_AIE : 0);
}

static irqreturn_t pcf85063_rtc_handle_irq(int irq, void *dev_id)
{
	struct pcf85063 *pcf85063 = dev_id;
	unsigned int val;
	int err;

	err = regmap_read(pcf85063->regmap, PCF85063_REG_CTRL2, &val);
	if (err)
		return IRQ_NONE;

	if (val & PCF85063_CTRL2_AF) {
		rtc_update_irq(pcf85063->rtc, 1, RTC_IRQF | RTC_AF);
		regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL2,
				   PCF85063_CTRL2_AIE | PCF85063_CTRL2_AF,
				   0);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int pcf85063_read_offset(struct device *dev, long *offset)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	long val;
	u32 reg;
	int ret;

	ret = regmap_read(pcf85063->regmap, PCF85063_REG_OFFSET, &reg);
	if (ret < 0)
		return ret;

	val = sign_extend32(reg & ~PCF85063_OFFSET_MODE,
			    PCF85063_OFFSET_SIGN_BIT);

	if (reg & PCF85063_OFFSET_MODE)
		*offset = val * PCF85063_OFFSET_STEP1;
	else
		*offset = val * PCF85063_OFFSET_STEP0;

	return 0;
}

static int pcf85063_set_offset(struct device *dev, long offset)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	s8 mode0, mode1, reg;
	unsigned int error0, error1;

	if (offset > PCF85063_OFFSET_STEP0 * 63)
		return -ERANGE;
	if (offset < PCF85063_OFFSET_STEP0 * -64)
		return -ERANGE;

	mode0 = DIV_ROUND_CLOSEST(offset, PCF85063_OFFSET_STEP0);
	mode1 = DIV_ROUND_CLOSEST(offset, PCF85063_OFFSET_STEP1);

	error0 = abs(offset - (mode0 * PCF85063_OFFSET_STEP0));
	error1 = abs(offset - (mode1 * PCF85063_OFFSET_STEP1));
	if (mode1 > 63 || mode1 < -64 || error0 < error1)
		reg = mode0 & ~PCF85063_OFFSET_MODE;
	else
		reg = mode1 | PCF85063_OFFSET_MODE;

	return regmap_write(pcf85063->regmap, PCF85063_REG_OFFSET, reg);
}

static int pcf85063_ioctl(struct device *dev, unsigned int cmd,
			  unsigned long arg)
{
	struct pcf85063 *pcf85063 = dev_get_drvdata(dev);
	int status, ret = 0;

	switch (cmd) {
	case RTC_VL_READ:
		ret = regmap_read(pcf85063->regmap, PCF85063_REG_SC, &status);
		if (ret < 0)
			return ret;

		if (status & PCF85063_REG_SC_OS)
			dev_warn(&pcf85063->rtc->dev, "Voltage low, data loss detected.\n");

		status &= PCF85063_REG_SC_OS;

		if (copy_to_user((void __user *)arg, &status, sizeof(int)))
			return -EFAULT;

		return 0;

	case RTC_VL_CLR:
		ret = regmap_update_bits(pcf85063->regmap, PCF85063_REG_SC,
					 PCF85063_REG_SC_OS, 0);

		return ret;

	default:
		return -ENOIOCTLCMD;
	}
}

static const struct rtc_class_ops pcf85063_rtc_ops = {
	.read_time	= pcf85063_rtc_read_time,
	.set_time	= pcf85063_rtc_set_time,
	.read_offset	= pcf85063_read_offset,
	.set_offset	= pcf85063_set_offset,
	.ioctl		= pcf85063_ioctl,
};

static const struct rtc_class_ops pcf85063_rtc_ops_alarm = {
	.read_time	= pcf85063_rtc_read_time,
	.set_time	= pcf85063_rtc_set_time,
	.read_offset	= pcf85063_read_offset,
	.set_offset	= pcf85063_set_offset,
	.read_alarm	= pcf85063_rtc_read_alarm,
	.set_alarm	= pcf85063_rtc_set_alarm,
	.alarm_irq_enable = pcf85063_rtc_alarm_irq_enable,
	.ioctl		= pcf85063_ioctl,
};

static int pcf85063_nvmem_read(void *priv, unsigned int offset,
			       void *val, size_t bytes)
{
	return regmap_read(priv, PCF85063_REG_RAM, val);
}

static int pcf85063_nvmem_write(void *priv, unsigned int offset,
				void *val, size_t bytes)
{
	return regmap_write(priv, PCF85063_REG_RAM, *(u8 *)val);
}

static int pcf85063_load_capacitance(struct pcf85063 *pcf85063,
				     const struct device_node *np,
				     unsigned int force_cap)
{
	u32 load = 7000;
	u8 reg = 0;

	if (force_cap)
		load = force_cap;
	else
		of_property_read_u32(np, "quartz-load-femtofarads", &load);

	switch (load) {
	default:
		dev_warn(&pcf85063->rtc->dev, "Unknown quartz-load-femtofarads value: %d. Assuming 7000",
			 load);
		/* fall through */
	case 7000:
		break;
	case 12500:
		reg = PCF85063_REG_CTRL1_CAP_SEL;
		break;
	}

	return regmap_update_bits(pcf85063->regmap, PCF85063_REG_CTRL1,
				  PCF85063_REG_CTRL1_CAP_SEL, reg);
}

static const struct pcf85063_config pcf85063a_config = {
	.regmap = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x11,
	},
	.has_alarms = 1,
};

static const struct pcf85063_config pcf85063tp_config = {
	.regmap = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x0a,
	},
};

static const struct pcf85063_config rv8263_config = {
	.regmap = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x11,
	},
	.has_alarms = 1,
	.force_cap_7000 = 1,
};

static int pcf85063_probe(struct i2c_client *client)
{
	struct pcf85063 *pcf85063;
	unsigned int tmp;
	int err;
	const struct pcf85063_config *config = &pcf85063tp_config;
	const void *data = of_device_get_match_data(&client->dev);
	struct nvmem_config nvmem_cfg = {
		.name = "pcf85063_nvram",
		.reg_read = pcf85063_nvmem_read,
		.reg_write = pcf85063_nvmem_write,
		.type = NVMEM_TYPE_BATTERY_BACKED,
		.size = 1,
	};

	dev_dbg(&client->dev, "%s\n", __func__);

	pcf85063 = devm_kzalloc(&client->dev, sizeof(struct pcf85063),
				GFP_KERNEL);
	if (!pcf85063)
		return -ENOMEM;

	if (data)
		config = data;

	pcf85063->regmap = devm_regmap_init_i2c(client, &config->regmap);
	if (IS_ERR(pcf85063->regmap))
		return PTR_ERR(pcf85063->regmap);

	i2c_set_clientdata(client, pcf85063);

	err = regmap_read(pcf85063->regmap, PCF85063_REG_CTRL1, &tmp);
	if (err) {
		dev_err(&client->dev, "RTC chip is not present\n");
		return err;
	}

	pcf85063->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(pcf85063->rtc))
		return PTR_ERR(pcf85063->rtc);

	err = pcf85063_load_capacitance(pcf85063, client->dev.of_node,
					config->force_cap_7000 ? 7000 : 0);
	if (err < 0)
		dev_warn(&client->dev, "failed to set xtal load capacitance: %d",
			 err);

	pcf85063->rtc->ops = &pcf85063_rtc_ops;
	pcf85063->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	pcf85063->rtc->range_max = RTC_TIMESTAMP_END_2099;
	pcf85063->rtc->uie_unsupported = 1;

	if (config->has_alarms && client->irq > 0) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, pcf85063_rtc_handle_irq,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"pcf85063", pcf85063);
		if (err) {
			dev_warn(&pcf85063->rtc->dev,
				 "unable to request IRQ, alarms disabled\n");
		} else {
			pcf85063->rtc->ops = &pcf85063_rtc_ops_alarm;
			device_init_wakeup(&client->dev, true);
			err = dev_pm_set_wake_irq(&client->dev, client->irq);
			if (err)
				dev_err(&pcf85063->rtc->dev,
					"failed to enable irq wake\n");
		}
	}

	nvmem_cfg.priv = pcf85063->regmap;
	rtc_nvmem_register(pcf85063->rtc, &nvmem_cfg);

	return rtc_register_device(pcf85063->rtc);
}

#ifdef CONFIG_OF
static const struct of_device_id pcf85063_of_match[] = {
	{ .compatible = "nxp,pcf85063", .data = &pcf85063tp_config },
	{ .compatible = "nxp,pcf85063tp", .data = &pcf85063tp_config },
	{ .compatible = "nxp,pcf85063a", .data = &pcf85063a_config },
	{ .compatible = "microcrystal,rv8263", .data = &rv8263_config },
	{}
};
MODULE_DEVICE_TABLE(of, pcf85063_of_match);
#endif

static struct i2c_driver pcf85063_driver = {
	.driver		= {
		.name	= "rtc-pcf85063",
		.of_match_table = of_match_ptr(pcf85063_of_match),
	},
	.probe_new	= pcf85063_probe,
};

module_i2c_driver(pcf85063_driver);

MODULE_AUTHOR("Søren Andersen <san@rosetechnology.dk>");
MODULE_DESCRIPTION("PCF85063 RTC driver");
MODULE_LICENSE("GPL");
