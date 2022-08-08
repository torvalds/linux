// SPDX-License-Identifier: GPL-2.0-only
/*
 * An I2C driver for the Philips PCF8563 RTC
 * Copyright 2005-06 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * based on the other drivers in this same directory.
 *
 * https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf
 */

#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/err.h>

#define PCF8563_REG_ST1		0x00 /* status */
#define PCF8563_REG_ST2		0x01
#define PCF8563_BIT_AIE		BIT(1)
#define PCF8563_BIT_AF		BIT(3)
#define PCF8563_BITS_ST2_N	(7 << 5)

#define PCF8563_REG_SC		0x02 /* datetime */
#define PCF8563_REG_MN		0x03
#define PCF8563_REG_HR		0x04
#define PCF8563_REG_DM		0x05
#define PCF8563_REG_DW		0x06
#define PCF8563_REG_MO		0x07
#define PCF8563_REG_YR		0x08

#define PCF8563_REG_AMN		0x09 /* alarm */

#define PCF8563_REG_CLKO		0x0D /* clock out */
#define PCF8563_REG_CLKO_FE		0x80 /* clock out enabled */
#define PCF8563_REG_CLKO_F_MASK		0x03 /* frequenc mask */
#define PCF8563_REG_CLKO_F_32768HZ	0x00
#define PCF8563_REG_CLKO_F_1024HZ	0x01
#define PCF8563_REG_CLKO_F_32HZ		0x02
#define PCF8563_REG_CLKO_F_1HZ		0x03

#define PCF8563_REG_TMRC	0x0E /* timer control */
#define PCF8563_TMRC_ENABLE	BIT(7)
#define PCF8563_TMRC_4096	0
#define PCF8563_TMRC_64		1
#define PCF8563_TMRC_1		2
#define PCF8563_TMRC_1_60	3
#define PCF8563_TMRC_MASK	3

#define PCF8563_REG_TMR		0x0F /* timer */

#define PCF8563_SC_LV		0x80 /* low voltage */
#define PCF8563_MO_C		0x80 /* century */

static struct i2c_driver pcf8563_driver;

struct pcf8563 {
	struct rtc_device *rtc;
	/*
	 * The meaning of MO_C bit varies by the chip type.
	 * From PCF8563 datasheet: this bit is toggled when the years
	 * register overflows from 99 to 00
	 *   0 indicates the century is 20xx
	 *   1 indicates the century is 19xx
	 * From RTC8564 datasheet: this bit indicates change of
	 * century. When the year digit data overflows from 99 to 00,
	 * this bit is set. By presetting it to 0 while still in the
	 * 20th century, it will be set in year 2000, ...
	 * There seems no reliable way to know how the system use this
	 * bit.  So let's do it heuristically, assuming we are live in
	 * 1970...2069.
	 */
	int c_polarity;	/* 0: MO_C=1 means 19xx, otherwise MO_C=1 means 20xx */

	struct i2c_client *client;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw		clkout_hw;
#endif
};

static int pcf8563_read_block_data(struct i2c_client *client, unsigned char reg,
				   unsigned char length, unsigned char *buf)
{
	struct i2c_msg msgs[] = {
		{/* setup read ptr */
			.addr = client->addr,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = buf
		},
	};

	if ((i2c_transfer(client->adapter, msgs, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	return 0;
}

static int pcf8563_write_block_data(struct i2c_client *client,
				   unsigned char reg, unsigned char length,
				   unsigned char *buf)
{
	int i, err;

	for (i = 0; i < length; i++) {
		unsigned char data[2] = { reg + i, buf[i] };

		err = i2c_master_send(client, data, sizeof(data));
		if (err != sizeof(data)) {
			dev_err(&client->dev,
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}

	return 0;
}

static int pcf8563_set_alarm_mode(struct i2c_client *client, bool on)
{
	unsigned char buf;
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_ST2, 1, &buf);
	if (err < 0)
		return err;

	if (on)
		buf |= PCF8563_BIT_AIE;
	else
		buf &= ~PCF8563_BIT_AIE;

	buf &= ~(PCF8563_BIT_AF | PCF8563_BITS_ST2_N);

	err = pcf8563_write_block_data(client, PCF8563_REG_ST2, 1, &buf);
	if (err < 0) {
		dev_err(&client->dev, "%s: write error\n", __func__);
		return -EIO;
	}

	return 0;
}

static int pcf8563_get_alarm_mode(struct i2c_client *client, unsigned char *en,
				  unsigned char *pen)
{
	unsigned char buf;
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_ST2, 1, &buf);
	if (err)
		return err;

	if (en)
		*en = !!(buf & PCF8563_BIT_AIE);
	if (pen)
		*pen = !!(buf & PCF8563_BIT_AF);

	return 0;
}

static irqreturn_t pcf8563_irq(int irq, void *dev_id)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(dev_id);
	int err;
	char pending;

	err = pcf8563_get_alarm_mode(pcf8563->client, NULL, &pending);
	if (err)
		return IRQ_NONE;

	if (pending) {
		rtc_update_irq(pcf8563->rtc, 1, RTC_IRQF | RTC_AF);
		pcf8563_set_alarm_mode(pcf8563->client, 1);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * In the routines that deal directly with the pcf8563 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf8563_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	unsigned char buf[9];
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_ST1, 9, buf);
	if (err)
		return err;

	if (buf[PCF8563_REG_SC] & PCF8563_SC_LV) {
		dev_err(&client->dev,
			"low voltage detected, date/time is not reliable.\n");
		return -EINVAL;
	}

	dev_dbg(&client->dev,
		"%s: raw data is st1=%02x, st2=%02x, sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__,
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7],
		buf[8]);


	tm->tm_sec = bcd2bin(buf[PCF8563_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[PCF8563_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[PCF8563_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(buf[PCF8563_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF8563_REG_DW] & 0x07;
	tm->tm_mon = bcd2bin(buf[PCF8563_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(buf[PCF8563_REG_YR]) + 100;
	/* detect the polarity heuristically. see note above. */
	pcf8563->c_polarity = (buf[PCF8563_REG_MO] & PCF8563_MO_C) ?
		(tm->tm_year >= 100) : (tm->tm_year < 100);

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int pcf8563_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	unsigned char buf[9];

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[PCF8563_REG_SC] = bin2bcd(tm->tm_sec);
	buf[PCF8563_REG_MN] = bin2bcd(tm->tm_min);
	buf[PCF8563_REG_HR] = bin2bcd(tm->tm_hour);

	buf[PCF8563_REG_DM] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[PCF8563_REG_MO] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	buf[PCF8563_REG_YR] = bin2bcd(tm->tm_year - 100);
	if (pcf8563->c_polarity ? (tm->tm_year >= 100) : (tm->tm_year < 100))
		buf[PCF8563_REG_MO] |= PCF8563_MO_C;

	buf[PCF8563_REG_DW] = tm->tm_wday & 0x07;

	return pcf8563_write_block_data(client, PCF8563_REG_SC,
				9 - PCF8563_REG_SC, buf + PCF8563_REG_SC);
}

static int pcf8563_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	switch (cmd) {
	case RTC_VL_READ:
		ret = i2c_smbus_read_byte_data(client, PCF8563_REG_SC);
		if (ret < 0)
			return ret;

		return put_user(ret & PCF8563_SC_LV ? RTC_VL_DATA_INVALID : 0,
				(unsigned int __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
}

static int pcf8563_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[4];
	int err;

	err = pcf8563_read_block_data(client, PCF8563_REG_AMN, 4, buf);
	if (err)
		return err;

	dev_dbg(&client->dev,
		"%s: raw data is min=%02x, hr=%02x, mday=%02x, wday=%02x\n",
		__func__, buf[0], buf[1], buf[2], buf[3]);

	tm->time.tm_sec = 0;
	tm->time.tm_min = bcd2bin(buf[0] & 0x7F);
	tm->time.tm_hour = bcd2bin(buf[1] & 0x3F);
	tm->time.tm_mday = bcd2bin(buf[2] & 0x3F);
	tm->time.tm_wday = bcd2bin(buf[3] & 0x7);

	err = pcf8563_get_alarm_mode(client, &tm->enabled, &tm->pending);
	if (err < 0)
		return err;

	dev_dbg(&client->dev, "%s: tm is mins=%d, hours=%d, mday=%d, wday=%d,"
		" enabled=%d, pending=%d\n", __func__, tm->time.tm_min,
		tm->time.tm_hour, tm->time.tm_mday, tm->time.tm_wday,
		tm->enabled, tm->pending);

	return 0;
}

static int pcf8563_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[4];
	int err;

	/* The alarm has no seconds, round up to nearest minute */
	if (tm->time.tm_sec) {
		time64_t alarm_time = rtc_tm_to_time64(&tm->time);

		alarm_time += 60 - tm->time.tm_sec;
		rtc_time64_to_tm(alarm_time, &tm->time);
	}

	dev_dbg(dev, "%s, min=%d hour=%d wday=%d mday=%d "
		"enabled=%d pending=%d\n", __func__,
		tm->time.tm_min, tm->time.tm_hour, tm->time.tm_wday,
		tm->time.tm_mday, tm->enabled, tm->pending);

	buf[0] = bin2bcd(tm->time.tm_min);
	buf[1] = bin2bcd(tm->time.tm_hour);
	buf[2] = bin2bcd(tm->time.tm_mday);
	buf[3] = tm->time.tm_wday & 0x07;

	err = pcf8563_write_block_data(client, PCF8563_REG_AMN, 4, buf);
	if (err)
		return err;

	return pcf8563_set_alarm_mode(client, !!tm->enabled);
}

static int pcf8563_irq_enable(struct device *dev, unsigned int enabled)
{
	dev_dbg(dev, "%s: en=%d\n", __func__, enabled);
	return pcf8563_set_alarm_mode(to_i2c_client(dev), !!enabled);
}

#ifdef CONFIG_COMMON_CLK
/*
 * Handling of the clkout
 */

#define clkout_hw_to_pcf8563(_hw) container_of(_hw, struct pcf8563, clkout_hw)

static const int clkout_rates[] = {
	32768,
	1024,
	32,
	1,
};

static unsigned long pcf8563_clkout_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct pcf8563 *pcf8563 = clkout_hw_to_pcf8563(hw);
	struct i2c_client *client = pcf8563->client;
	unsigned char buf;
	int ret = pcf8563_read_block_data(client, PCF8563_REG_CLKO, 1, &buf);

	if (ret < 0)
		return 0;

	buf &= PCF8563_REG_CLKO_F_MASK;
	return clkout_rates[buf];
}

static long pcf8563_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *prate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clkout_rates); i++)
		if (clkout_rates[i] <= rate)
			return clkout_rates[i];

	return 0;
}

static int pcf8563_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct pcf8563 *pcf8563 = clkout_hw_to_pcf8563(hw);
	struct i2c_client *client = pcf8563->client;
	unsigned char buf;
	int ret = pcf8563_read_block_data(client, PCF8563_REG_CLKO, 1, &buf);
	int i;

	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(clkout_rates); i++)
		if (clkout_rates[i] == rate) {
			buf &= ~PCF8563_REG_CLKO_F_MASK;
			buf |= i;
			ret = pcf8563_write_block_data(client,
						       PCF8563_REG_CLKO, 1,
						       &buf);
			return ret;
		}

	return -EINVAL;
}

static int pcf8563_clkout_control(struct clk_hw *hw, bool enable)
{
	struct pcf8563 *pcf8563 = clkout_hw_to_pcf8563(hw);
	struct i2c_client *client = pcf8563->client;
	unsigned char buf;
	int ret = pcf8563_read_block_data(client, PCF8563_REG_CLKO, 1, &buf);

	if (ret < 0)
		return ret;

	if (enable)
		buf |= PCF8563_REG_CLKO_FE;
	else
		buf &= ~PCF8563_REG_CLKO_FE;

	ret = pcf8563_write_block_data(client, PCF8563_REG_CLKO, 1, &buf);
	return ret;
}

static int pcf8563_clkout_prepare(struct clk_hw *hw)
{
	return pcf8563_clkout_control(hw, 1);
}

static void pcf8563_clkout_unprepare(struct clk_hw *hw)
{
	pcf8563_clkout_control(hw, 0);
}

static int pcf8563_clkout_is_prepared(struct clk_hw *hw)
{
	struct pcf8563 *pcf8563 = clkout_hw_to_pcf8563(hw);
	struct i2c_client *client = pcf8563->client;
	unsigned char buf;
	int ret = pcf8563_read_block_data(client, PCF8563_REG_CLKO, 1, &buf);

	if (ret < 0)
		return ret;

	return !!(buf & PCF8563_REG_CLKO_FE);
}

static const struct clk_ops pcf8563_clkout_ops = {
	.prepare = pcf8563_clkout_prepare,
	.unprepare = pcf8563_clkout_unprepare,
	.is_prepared = pcf8563_clkout_is_prepared,
	.recalc_rate = pcf8563_clkout_recalc_rate,
	.round_rate = pcf8563_clkout_round_rate,
	.set_rate = pcf8563_clkout_set_rate,
};

static struct clk *pcf8563_clkout_register_clk(struct pcf8563 *pcf8563)
{
	struct i2c_client *client = pcf8563->client;
	struct device_node *node = client->dev.of_node;
	struct clk *clk;
	struct clk_init_data init;
	int ret;
	unsigned char buf;

	/* disable the clkout output */
	buf = 0;
	ret = pcf8563_write_block_data(client, PCF8563_REG_CLKO, 1, &buf);
	if (ret < 0)
		return ERR_PTR(ret);

	init.name = "pcf8563-clkout";
	init.ops = &pcf8563_clkout_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;
	pcf8563->clkout_hw.init = &init;

	/* optional override of the clockname */
	of_property_read_string(node, "clock-output-names", &init.name);

	/* register the clock */
	clk = devm_clk_register(&client->dev, &pcf8563->clkout_hw);

	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return clk;
}
#endif

static const struct rtc_class_ops pcf8563_rtc_ops = {
	.ioctl		= pcf8563_rtc_ioctl,
	.read_time	= pcf8563_rtc_read_time,
	.set_time	= pcf8563_rtc_set_time,
	.read_alarm	= pcf8563_rtc_read_alarm,
	.set_alarm	= pcf8563_rtc_set_alarm,
	.alarm_irq_enable = pcf8563_irq_enable,
};

static int pcf8563_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pcf8563 *pcf8563;
	int err;
	unsigned char buf;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf8563 = devm_kzalloc(&client->dev, sizeof(struct pcf8563),
				GFP_KERNEL);
	if (!pcf8563)
		return -ENOMEM;

	i2c_set_clientdata(client, pcf8563);
	pcf8563->client = client;
	device_set_wakeup_capable(&client->dev, 1);

	/* Set timer to lowest frequency to save power (ref Haoyu datasheet) */
	buf = PCF8563_TMRC_1_60;
	err = pcf8563_write_block_data(client, PCF8563_REG_TMRC, 1, &buf);
	if (err < 0) {
		dev_err(&client->dev, "%s: write error\n", __func__);
		return err;
	}

	/* Clear flags and disable interrupts */
	buf = 0;
	err = pcf8563_write_block_data(client, PCF8563_REG_ST2, 1, &buf);
	if (err < 0) {
		dev_err(&client->dev, "%s: write error\n", __func__);
		return err;
	}

	pcf8563->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(pcf8563->rtc))
		return PTR_ERR(pcf8563->rtc);

	pcf8563->rtc->ops = &pcf8563_rtc_ops;
	/* the pcf8563 alarm only supports a minute accuracy */
	pcf8563->rtc->uie_unsupported = 1;
	pcf8563->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	pcf8563->rtc->range_max = RTC_TIMESTAMP_END_2099;
	pcf8563->rtc->set_start_time = true;

	if (client->irq > 0) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, pcf8563_irq,
				IRQF_SHARED | IRQF_ONESHOT | IRQF_TRIGGER_LOW,
				pcf8563_driver.driver.name, client);
		if (err) {
			dev_err(&client->dev, "unable to request IRQ %d\n",
								client->irq);
			return err;
		}
	}

	err = devm_rtc_register_device(pcf8563->rtc);
	if (err)
		return err;

#ifdef CONFIG_COMMON_CLK
	/* register clk in common clk framework */
	pcf8563_clkout_register_clk(pcf8563);
#endif

	return 0;
}

static const struct i2c_device_id pcf8563_id[] = {
	{ "pcf8563", 0 },
	{ "rtc8564", 0 },
	{ "pca8565", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8563_id);

#ifdef CONFIG_OF
static const struct of_device_id pcf8563_of_match[] = {
	{ .compatible = "nxp,pcf8563" },
	{ .compatible = "epson,rtc8564" },
	{ .compatible = "microcrystal,rv8564" },
	{ .compatible = "nxp,pca8565" },
	{}
};
MODULE_DEVICE_TABLE(of, pcf8563_of_match);
#endif

static struct i2c_driver pcf8563_driver = {
	.driver		= {
		.name	= "rtc-pcf8563",
		.of_match_table = of_match_ptr(pcf8563_of_match),
	},
	.probe		= pcf8563_probe,
	.id_table	= pcf8563_id,
};

module_i2c_driver(pcf8563_driver);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Philips PCF8563/Epson RTC8564 RTC driver");
MODULE_LICENSE("GPL");
