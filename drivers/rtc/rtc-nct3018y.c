// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Nuvoton Technology Corporation

#include <linux/bcd.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/slab.h>

#define NCT3018Y_REG_SC		0x00 /* seconds */
#define NCT3018Y_REG_SCA	0x01 /* alarm */
#define NCT3018Y_REG_MN		0x02
#define NCT3018Y_REG_MNA	0x03 /* alarm */
#define NCT3018Y_REG_HR		0x04
#define NCT3018Y_REG_HRA	0x05 /* alarm */
#define NCT3018Y_REG_DW		0x06
#define NCT3018Y_REG_DM		0x07
#define NCT3018Y_REG_MO		0x08
#define NCT3018Y_REG_YR		0x09
#define NCT3018Y_REG_CTRL	0x0A /* timer control */
#define NCT3018Y_REG_ST		0x0B /* status */
#define NCT3018Y_REG_CLKO	0x0C /* clock out */

#define NCT3018Y_BIT_AF		BIT(7)
#define NCT3018Y_BIT_ST		BIT(7)
#define NCT3018Y_BIT_DM		BIT(6)
#define NCT3018Y_BIT_HF		BIT(5)
#define NCT3018Y_BIT_DSM	BIT(4)
#define NCT3018Y_BIT_AIE	BIT(3)
#define NCT3018Y_BIT_OFIE	BIT(2)
#define NCT3018Y_BIT_CIE	BIT(1)
#define NCT3018Y_BIT_TWO	BIT(0)

#define NCT3018Y_REG_BAT_MASK		0x07
#define NCT3018Y_REG_CLKO_F_MASK	0x03 /* frequenc mask */
#define NCT3018Y_REG_CLKO_CKE		0x80 /* clock out enabled */

struct nct3018y {
	struct rtc_device *rtc;
	struct i2c_client *client;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw clkout_hw;
#endif
};

static int nct3018y_set_alarm_mode(struct i2c_client *client, bool on)
{
	int err, flags;

	dev_dbg(&client->dev, "%s:on:%d\n", __func__, on);

	flags =  i2c_smbus_read_byte_data(client, NCT3018Y_REG_CTRL);
	if (flags < 0) {
		dev_dbg(&client->dev,
			"Failed to read NCT3018Y_REG_CTRL\n");
		return flags;
	}

	if (on)
		flags |= NCT3018Y_BIT_AIE;
	else
		flags &= ~NCT3018Y_BIT_AIE;

	flags |= NCT3018Y_BIT_CIE;
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_CTRL, flags);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_CTRL\n");
		return err;
	}

	flags =  i2c_smbus_read_byte_data(client, NCT3018Y_REG_ST);
	if (flags < 0) {
		dev_dbg(&client->dev,
			"Failed to read NCT3018Y_REG_ST\n");
		return flags;
	}

	flags &= ~(NCT3018Y_BIT_AF);
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_ST, flags);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_ST\n");
		return err;
	}

	return 0;
}

static int nct3018y_get_alarm_mode(struct i2c_client *client, unsigned char *alarm_enable,
				   unsigned char *alarm_flag)
{
	int flags;

	if (alarm_enable) {
		dev_dbg(&client->dev, "%s:NCT3018Y_REG_CTRL\n", __func__);
		flags =  i2c_smbus_read_byte_data(client, NCT3018Y_REG_CTRL);
		if (flags < 0)
			return flags;
		*alarm_enable = flags & NCT3018Y_BIT_AIE;
		dev_dbg(&client->dev, "%s:alarm_enable:%x\n", __func__, *alarm_enable);

	}

	if (alarm_flag) {
		dev_dbg(&client->dev, "%s:NCT3018Y_REG_ST\n", __func__);
		flags =  i2c_smbus_read_byte_data(client, NCT3018Y_REG_ST);
		if (flags < 0)
			return flags;
		*alarm_flag = flags & NCT3018Y_BIT_AF;
		dev_dbg(&client->dev, "%s:alarm_flag:%x\n", __func__, *alarm_flag);
	}

	return 0;
}

static irqreturn_t nct3018y_irq(int irq, void *dev_id)
{
	struct nct3018y *nct3018y = i2c_get_clientdata(dev_id);
	struct i2c_client *client = nct3018y->client;
	int err;
	unsigned char alarm_flag;
	unsigned char alarm_enable;

	dev_dbg(&client->dev, "%s:irq:%d\n", __func__, irq);
	err = nct3018y_get_alarm_mode(nct3018y->client, &alarm_enable, &alarm_flag);
	if (err)
		return IRQ_NONE;

	if (alarm_flag) {
		dev_dbg(&client->dev, "%s:alarm flag:%x\n",
			__func__, alarm_flag);
		rtc_update_irq(nct3018y->rtc, 1, RTC_IRQF | RTC_AF);
		nct3018y_set_alarm_mode(nct3018y->client, 0);
		dev_dbg(&client->dev, "%s:IRQ_HANDLED\n", __func__);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * In the routines that deal directly with the nct3018y hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int nct3018y_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[10];
	int err;

	err = i2c_smbus_read_i2c_block_data(client, NCT3018Y_REG_ST, 1, buf);
	if (err < 0)
		return err;

	if (!buf[0]) {
		dev_dbg(&client->dev, " voltage <=1.7, date/time is not reliable.\n");
		return -EINVAL;
	}

	err = i2c_smbus_read_i2c_block_data(client, NCT3018Y_REG_SC, sizeof(buf), buf);
	if (err < 0)
		return err;

	tm->tm_sec = bcd2bin(buf[0] & 0x7F);
	tm->tm_min = bcd2bin(buf[2] & 0x7F);
	tm->tm_hour = bcd2bin(buf[4] & 0x3F);
	tm->tm_wday = buf[6] & 0x07;
	tm->tm_mday = bcd2bin(buf[7] & 0x3F);
	tm->tm_mon = bcd2bin(buf[8] & 0x1F) - 1;
	tm->tm_year = bcd2bin(buf[9]) + 100;

	return 0;
}

static int nct3018y_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[4] = {0};
	int err;

	buf[0] = bin2bcd(tm->tm_sec);
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_SC, buf[0]);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_SC\n");
		return err;
	}

	buf[0] = bin2bcd(tm->tm_min);
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_MN, buf[0]);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_MN\n");
		return err;
	}

	buf[0] = bin2bcd(tm->tm_hour);
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_HR, buf[0]);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_HR\n");
		return err;
	}

	buf[0] = tm->tm_wday & 0x07;
	buf[1] = bin2bcd(tm->tm_mday);
	buf[2] = bin2bcd(tm->tm_mon + 1);
	buf[3] = bin2bcd(tm->tm_year - 100);
	err = i2c_smbus_write_i2c_block_data(client, NCT3018Y_REG_DW,
					     sizeof(buf), buf);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write for day and mon and year\n");
		return -EIO;
	}

	return err;
}

static int nct3018y_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[5];
	int err;

	err = i2c_smbus_read_i2c_block_data(client, NCT3018Y_REG_SCA,
					    sizeof(buf), buf);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to read date\n");
		return -EIO;
	}

	dev_dbg(&client->dev, "%s: raw data is sec=%02x, min=%02x hr=%02x\n",
		__func__, buf[0], buf[2], buf[4]);

	tm->time.tm_sec = bcd2bin(buf[0] & 0x7F);
	tm->time.tm_min = bcd2bin(buf[2] & 0x7F);
	tm->time.tm_hour = bcd2bin(buf[4] & 0x3F);

	err = nct3018y_get_alarm_mode(client, &tm->enabled, &tm->pending);
	if (err < 0)
		return err;

	dev_dbg(&client->dev, "%s:s=%d m=%d, hr=%d, enabled=%d, pending=%d\n",
		__func__, tm->time.tm_sec, tm->time.tm_min,
		tm->time.tm_hour, tm->enabled, tm->pending);

	return 0;
}

static int nct3018y_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	int err;

	dev_dbg(dev, "%s, sec=%d, min=%d hour=%d tm->enabled:%d\n",
		__func__, tm->time.tm_sec, tm->time.tm_min, tm->time.tm_hour,
		tm->enabled);

	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_SCA, bin2bcd(tm->time.tm_sec));
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_SCA\n");
		return err;
	}

	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_MNA, bin2bcd(tm->time.tm_min));
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_MNA\n");
		return err;
	}

	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_HRA, bin2bcd(tm->time.tm_hour));
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_HRA\n");
		return err;
	}

	return nct3018y_set_alarm_mode(client, tm->enabled);
}

static int nct3018y_irq_enable(struct device *dev, unsigned int enabled)
{
	dev_dbg(dev, "%s: alarm enable=%d\n", __func__, enabled);

	return nct3018y_set_alarm_mode(to_i2c_client(dev), enabled);
}

static int nct3018y_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	int status, flags = 0;

	switch (cmd) {
	case RTC_VL_READ:
		status = i2c_smbus_read_byte_data(client, NCT3018Y_REG_ST);
		if (status < 0)
			return status;

		if (!(status & NCT3018Y_REG_BAT_MASK))
			flags |= RTC_VL_DATA_INVALID;

		return put_user(flags, (unsigned int __user *)arg);

	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMMON_CLK
/*
 * Handling of the clkout
 */

#define clkout_hw_to_nct3018y(_hw) container_of(_hw, struct nct3018y, clkout_hw)

static const int clkout_rates[] = {
	32768,
	1024,
	32,
	1,
};

static unsigned long nct3018y_clkout_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct nct3018y *nct3018y = clkout_hw_to_nct3018y(hw);
	struct i2c_client *client = nct3018y->client;
	int flags;

	flags = i2c_smbus_read_byte_data(client, NCT3018Y_REG_CLKO);
	if (flags < 0)
		return 0;

	flags &= NCT3018Y_REG_CLKO_F_MASK;
	return clkout_rates[flags];
}

static long nct3018y_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *prate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clkout_rates); i++)
		if (clkout_rates[i] <= rate)
			return clkout_rates[i];

	return 0;
}

static int nct3018y_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct nct3018y *nct3018y = clkout_hw_to_nct3018y(hw);
	struct i2c_client *client = nct3018y->client;
	int i, flags;

	flags = i2c_smbus_read_byte_data(client, NCT3018Y_REG_CLKO);
	if (flags < 0)
		return flags;

	for (i = 0; i < ARRAY_SIZE(clkout_rates); i++)
		if (clkout_rates[i] == rate) {
			flags &= ~NCT3018Y_REG_CLKO_F_MASK;
			flags |= i;
			return i2c_smbus_write_byte_data(client, NCT3018Y_REG_CLKO, flags);
		}

	return -EINVAL;
}

static int nct3018y_clkout_control(struct clk_hw *hw, bool enable)
{
	struct nct3018y *nct3018y = clkout_hw_to_nct3018y(hw);
	struct i2c_client *client = nct3018y->client;
	int flags;

	flags = i2c_smbus_read_byte_data(client, NCT3018Y_REG_CLKO);
	if (flags < 0)
		return flags;

	if (enable)
		flags |= NCT3018Y_REG_CLKO_CKE;
	else
		flags &= ~NCT3018Y_REG_CLKO_CKE;

	return i2c_smbus_write_byte_data(client, NCT3018Y_REG_CLKO, flags);
}

static int nct3018y_clkout_prepare(struct clk_hw *hw)
{
	return nct3018y_clkout_control(hw, 1);
}

static void nct3018y_clkout_unprepare(struct clk_hw *hw)
{
	nct3018y_clkout_control(hw, 0);
}

static int nct3018y_clkout_is_prepared(struct clk_hw *hw)
{
	struct nct3018y *nct3018y = clkout_hw_to_nct3018y(hw);
	struct i2c_client *client = nct3018y->client;
	int flags;

	flags = i2c_smbus_read_byte_data(client, NCT3018Y_REG_CLKO);
	if (flags < 0)
		return flags;

	return flags & NCT3018Y_REG_CLKO_CKE;
}

static const struct clk_ops nct3018y_clkout_ops = {
	.prepare = nct3018y_clkout_prepare,
	.unprepare = nct3018y_clkout_unprepare,
	.is_prepared = nct3018y_clkout_is_prepared,
	.recalc_rate = nct3018y_clkout_recalc_rate,
	.round_rate = nct3018y_clkout_round_rate,
	.set_rate = nct3018y_clkout_set_rate,
};

static struct clk *nct3018y_clkout_register_clk(struct nct3018y *nct3018y)
{
	struct i2c_client *client = nct3018y->client;
	struct device_node *node = client->dev.of_node;
	struct clk *clk;
	struct clk_init_data init;

	init.name = "nct3018y-clkout";
	init.ops = &nct3018y_clkout_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;
	nct3018y->clkout_hw.init = &init;

	/* optional override of the clockname */
	of_property_read_string(node, "clock-output-names", &init.name);

	/* register the clock */
	clk = devm_clk_register(&client->dev, &nct3018y->clkout_hw);

	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return clk;
}
#endif

static const struct rtc_class_ops nct3018y_rtc_ops = {
	.read_time	= nct3018y_rtc_read_time,
	.set_time	= nct3018y_rtc_set_time,
	.read_alarm	= nct3018y_rtc_read_alarm,
	.set_alarm	= nct3018y_rtc_set_alarm,
	.alarm_irq_enable = nct3018y_irq_enable,
	.ioctl		= nct3018y_ioctl,
};

static int nct3018y_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct nct3018y *nct3018y;
	int err, flags;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_BYTE |
				     I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	nct3018y = devm_kzalloc(&client->dev, sizeof(struct nct3018y),
				GFP_KERNEL);
	if (!nct3018y)
		return -ENOMEM;

	i2c_set_clientdata(client, nct3018y);
	nct3018y->client = client;
	device_set_wakeup_capable(&client->dev, 1);

	flags = i2c_smbus_read_byte_data(client, NCT3018Y_REG_CTRL);
	if (flags < 0) {
		dev_dbg(&client->dev, "%s: read error\n", __func__);
		return flags;
	} else if (flags & NCT3018Y_BIT_TWO) {
		dev_dbg(&client->dev, "%s: NCT3018Y_BIT_TWO is set\n", __func__);
	}

	flags = NCT3018Y_BIT_TWO;
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_CTRL, flags);
	if (err < 0) {
		dev_dbg(&client->dev, "Unable to write NCT3018Y_REG_CTRL\n");
		return err;
	}

	flags = 0;
	err = i2c_smbus_write_byte_data(client, NCT3018Y_REG_ST, flags);
	if (err < 0) {
		dev_dbg(&client->dev, "%s: write error\n", __func__);
		return err;
	}

	nct3018y->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(nct3018y->rtc))
		return PTR_ERR(nct3018y->rtc);

	nct3018y->rtc->ops = &nct3018y_rtc_ops;
	nct3018y->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	nct3018y->rtc->range_max = RTC_TIMESTAMP_END_2099;

	if (client->irq > 0) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, nct3018y_irq,
						IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
						"nct3018y", client);
		if (err) {
			dev_dbg(&client->dev, "unable to request IRQ %d\n", client->irq);
			return err;
		}
	} else {
		clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, nct3018y->rtc->features);
		clear_bit(RTC_FEATURE_ALARM, nct3018y->rtc->features);
	}

#ifdef CONFIG_COMMON_CLK
	/* register clk in common clk framework */
	nct3018y_clkout_register_clk(nct3018y);
#endif

	return devm_rtc_register_device(nct3018y->rtc);
}

static const struct i2c_device_id nct3018y_id[] = {
	{ "nct3018y", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nct3018y_id);

static const struct of_device_id nct3018y_of_match[] = {
	{ .compatible = "nuvoton,nct3018y" },
	{}
};
MODULE_DEVICE_TABLE(of, nct3018y_of_match);

static struct i2c_driver nct3018y_driver = {
	.driver		= {
		.name	= "rtc-nct3018y",
		.of_match_table = of_match_ptr(nct3018y_of_match),
	},
	.probe		= nct3018y_probe,
	.id_table	= nct3018y_id,
};

module_i2c_driver(nct3018y_driver);

MODULE_AUTHOR("Medad CChien <ctcchien@nuvoton.com>");
MODULE_AUTHOR("Mia Lin <mimi05633@gmail.com>");
MODULE_DESCRIPTION("Nuvoton NCT3018Y RTC driver");
MODULE_LICENSE("GPL");
