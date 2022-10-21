// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C client/driver for the ST M41T80 family of i2c rtc chips.
 *
 * Author: Alexander Bigga <ab@mycable.de>
 *
 * Based on m41t00.c by Mark A. Greer <mgreer@mvista.com>
 *
 * 2006 (c) mycable GmbH
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bcd.h>
#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#ifdef CONFIG_RTC_DRV_M41T80_WDT
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>
#endif

#define M41T80_REG_SSEC		0x00
#define M41T80_REG_SEC		0x01
#define M41T80_REG_MIN		0x02
#define M41T80_REG_HOUR		0x03
#define M41T80_REG_WDAY		0x04
#define M41T80_REG_DAY		0x05
#define M41T80_REG_MON		0x06
#define M41T80_REG_YEAR		0x07
#define M41T80_REG_ALARM_MON	0x0a
#define M41T80_REG_ALARM_DAY	0x0b
#define M41T80_REG_ALARM_HOUR	0x0c
#define M41T80_REG_ALARM_MIN	0x0d
#define M41T80_REG_ALARM_SEC	0x0e
#define M41T80_REG_FLAGS	0x0f
#define M41T80_REG_SQW		0x13

#define M41T80_DATETIME_REG_SIZE	(M41T80_REG_YEAR + 1)
#define M41T80_ALARM_REG_SIZE	\
	(M41T80_REG_ALARM_SEC + 1 - M41T80_REG_ALARM_MON)

#define M41T80_SQW_MAX_FREQ	32768

#define M41T80_SEC_ST		BIT(7)	/* ST: Stop Bit */
#define M41T80_ALMON_AFE	BIT(7)	/* AFE: AF Enable Bit */
#define M41T80_ALMON_SQWE	BIT(6)	/* SQWE: SQW Enable Bit */
#define M41T80_ALHOUR_HT	BIT(6)	/* HT: Halt Update Bit */
#define M41T80_FLAGS_OF		BIT(2)	/* OF: Oscillator Failure Bit */
#define M41T80_FLAGS_AF		BIT(6)	/* AF: Alarm Flag Bit */
#define M41T80_FLAGS_BATT_LOW	BIT(4)	/* BL: Battery Low Bit */
#define M41T80_WATCHDOG_RB2	BIT(7)	/* RB: Watchdog resolution */
#define M41T80_WATCHDOG_RB1	BIT(1)	/* RB: Watchdog resolution */
#define M41T80_WATCHDOG_RB0	BIT(0)	/* RB: Watchdog resolution */

#define M41T80_FEATURE_HT	BIT(0)	/* Halt feature */
#define M41T80_FEATURE_BL	BIT(1)	/* Battery low indicator */
#define M41T80_FEATURE_SQ	BIT(2)	/* Squarewave feature */
#define M41T80_FEATURE_WD	BIT(3)	/* Extra watchdog resolution */
#define M41T80_FEATURE_SQ_ALT	BIT(4)	/* RSx bits are in reg 4 */

static const struct i2c_device_id m41t80_id[] = {
	{ "m41t62", M41T80_FEATURE_SQ | M41T80_FEATURE_SQ_ALT },
	{ "m41t65", M41T80_FEATURE_HT | M41T80_FEATURE_WD },
	{ "m41t80", M41T80_FEATURE_SQ },
	{ "m41t81", M41T80_FEATURE_HT | M41T80_FEATURE_SQ},
	{ "m41t81s", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41t82", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41t83", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41st84", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41st85", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "m41st87", M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ },
	{ "rv4162", M41T80_FEATURE_SQ | M41T80_FEATURE_WD | M41T80_FEATURE_SQ_ALT },
	{ }
};
MODULE_DEVICE_TABLE(i2c, m41t80_id);

static const __maybe_unused struct of_device_id m41t80_of_match[] = {
	{
		.compatible = "st,m41t62",
		.data = (void *)(M41T80_FEATURE_SQ | M41T80_FEATURE_SQ_ALT)
	},
	{
		.compatible = "st,m41t65",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_WD)
	},
	{
		.compatible = "st,m41t80",
		.data = (void *)(M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t81",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t81s",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t82",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t83",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t84",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t85",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "st,m41t87",
		.data = (void *)(M41T80_FEATURE_HT | M41T80_FEATURE_BL | M41T80_FEATURE_SQ)
	},
	{
		.compatible = "microcrystal,rv4162",
		.data = (void *)(M41T80_FEATURE_SQ | M41T80_FEATURE_WD | M41T80_FEATURE_SQ_ALT)
	},
	/* DT compatibility only, do not use compatibles below: */
	{
		.compatible = "st,rv4162",
		.data = (void *)(M41T80_FEATURE_SQ | M41T80_FEATURE_WD | M41T80_FEATURE_SQ_ALT)
	},
	{
		.compatible = "rv4162",
		.data = (void *)(M41T80_FEATURE_SQ | M41T80_FEATURE_WD | M41T80_FEATURE_SQ_ALT)
	},
	{ }
};
MODULE_DEVICE_TABLE(of, m41t80_of_match);

struct m41t80_data {
	unsigned long features;
	struct i2c_client *client;
	struct rtc_device *rtc;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw sqw;
	unsigned long freq;
	unsigned int sqwe;
#endif
};

static irqreturn_t m41t80_handle_irq(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct m41t80_data *m41t80 = i2c_get_clientdata(client);
	unsigned long events = 0;
	int flags, flags_afe;

	rtc_lock(m41t80->rtc);

	flags_afe = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_MON);
	if (flags_afe < 0) {
		rtc_unlock(m41t80->rtc);
		return IRQ_NONE;
	}

	flags = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
	if (flags <= 0) {
		rtc_unlock(m41t80->rtc);
		return IRQ_NONE;
	}

	if (flags & M41T80_FLAGS_AF) {
		flags &= ~M41T80_FLAGS_AF;
		flags_afe &= ~M41T80_ALMON_AFE;
		events |= RTC_AF;
	}

	if (events) {
		rtc_update_irq(m41t80->rtc, 1, events);
		i2c_smbus_write_byte_data(client, M41T80_REG_FLAGS, flags);
		i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON,
					  flags_afe);
	}

	rtc_unlock(m41t80->rtc);

	return IRQ_HANDLED;
}

static int m41t80_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[8];
	int err, flags;

	flags = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
	if (flags < 0)
		return flags;

	if (flags & M41T80_FLAGS_OF) {
		dev_err(&client->dev, "Oscillator failure, data is invalid.\n");
		return -EINVAL;
	}

	err = i2c_smbus_read_i2c_block_data(client, M41T80_REG_SSEC,
					    sizeof(buf), buf);
	if (err < 0) {
		dev_err(&client->dev, "Unable to read date\n");
		return err;
	}

	tm->tm_sec = bcd2bin(buf[M41T80_REG_SEC] & 0x7f);
	tm->tm_min = bcd2bin(buf[M41T80_REG_MIN] & 0x7f);
	tm->tm_hour = bcd2bin(buf[M41T80_REG_HOUR] & 0x3f);
	tm->tm_mday = bcd2bin(buf[M41T80_REG_DAY] & 0x3f);
	tm->tm_wday = buf[M41T80_REG_WDAY] & 0x07;
	tm->tm_mon = bcd2bin(buf[M41T80_REG_MON] & 0x1f) - 1;

	/* assume 20YY not 19YY, and ignore the Century Bit */
	tm->tm_year = bcd2bin(buf[M41T80_REG_YEAR]) + 100;
	return 0;
}

static int m41t80_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct m41t80_data *clientdata = i2c_get_clientdata(client);
	unsigned char buf[8];
	int err, flags;

	buf[M41T80_REG_SSEC] = 0;
	buf[M41T80_REG_SEC] = bin2bcd(tm->tm_sec);
	buf[M41T80_REG_MIN] = bin2bcd(tm->tm_min);
	buf[M41T80_REG_HOUR] = bin2bcd(tm->tm_hour);
	buf[M41T80_REG_DAY] = bin2bcd(tm->tm_mday);
	buf[M41T80_REG_MON] = bin2bcd(tm->tm_mon + 1);
	buf[M41T80_REG_YEAR] = bin2bcd(tm->tm_year - 100);
	buf[M41T80_REG_WDAY] = tm->tm_wday;

	/* If the square wave output is controlled in the weekday register */
	if (clientdata->features & M41T80_FEATURE_SQ_ALT) {
		int val;

		val = i2c_smbus_read_byte_data(client, M41T80_REG_WDAY);
		if (val < 0)
			return val;

		buf[M41T80_REG_WDAY] |= (val & 0xf0);
	}

	err = i2c_smbus_write_i2c_block_data(client, M41T80_REG_SSEC,
					     sizeof(buf), buf);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write to date registers\n");
		return err;
	}

	/* Clear the OF bit of Flags Register */
	flags = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
	if (flags < 0)
		return flags;

	err = i2c_smbus_write_byte_data(client, M41T80_REG_FLAGS,
					flags & ~M41T80_FLAGS_OF);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write flags register\n");
		return err;
	}

	return err;
}

static int m41t80_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct m41t80_data *clientdata = i2c_get_clientdata(client);
	int reg;

	if (clientdata->features & M41T80_FEATURE_BL) {
		reg = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
		if (reg < 0)
			return reg;
		seq_printf(seq, "battery\t\t: %s\n",
			   (reg & M41T80_FLAGS_BATT_LOW) ? "exhausted" : "ok");
	}
	return 0;
}

static int m41t80_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct i2c_client *client = to_i2c_client(dev);
	int flags, retval;

	flags = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_MON);
	if (flags < 0)
		return flags;

	if (enabled)
		flags |= M41T80_ALMON_AFE;
	else
		flags &= ~M41T80_ALMON_AFE;

	retval = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON, flags);
	if (retval < 0) {
		dev_err(dev, "Unable to enable alarm IRQ %d\n", retval);
		return retval;
	}
	return 0;
}

static int m41t80_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 alarmvals[5];
	int ret, err;

	alarmvals[0] = bin2bcd(alrm->time.tm_mon + 1);
	alarmvals[1] = bin2bcd(alrm->time.tm_mday);
	alarmvals[2] = bin2bcd(alrm->time.tm_hour);
	alarmvals[3] = bin2bcd(alrm->time.tm_min);
	alarmvals[4] = bin2bcd(alrm->time.tm_sec);

	/* Clear AF and AFE flags */
	ret = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_MON);
	if (ret < 0)
		return ret;
	err = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON,
					ret & ~(M41T80_ALMON_AFE));
	if (err < 0) {
		dev_err(dev, "Unable to clear AFE bit\n");
		return err;
	}

	/* Keep SQWE bit value */
	alarmvals[0] |= (ret & M41T80_ALMON_SQWE);

	ret = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
	if (ret < 0)
		return ret;

	err = i2c_smbus_write_byte_data(client, M41T80_REG_FLAGS,
					ret & ~(M41T80_FLAGS_AF));
	if (err < 0) {
		dev_err(dev, "Unable to clear AF bit\n");
		return err;
	}

	/* Write the alarm */
	err = i2c_smbus_write_i2c_block_data(client, M41T80_REG_ALARM_MON,
					     5, alarmvals);
	if (err)
		return err;

	/* Enable the alarm interrupt */
	if (alrm->enabled) {
		alarmvals[0] |= M41T80_ALMON_AFE;
		err = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON,
						alarmvals[0]);
		if (err)
			return err;
	}

	return 0;
}

static int m41t80_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 alarmvals[5];
	int flags, ret;

	ret = i2c_smbus_read_i2c_block_data(client, M41T80_REG_ALARM_MON,
					    5, alarmvals);
	if (ret != 5)
		return ret < 0 ? ret : -EIO;

	flags = i2c_smbus_read_byte_data(client, M41T80_REG_FLAGS);
	if (flags < 0)
		return flags;

	alrm->time.tm_sec  = bcd2bin(alarmvals[4] & 0x7f);
	alrm->time.tm_min  = bcd2bin(alarmvals[3] & 0x7f);
	alrm->time.tm_hour = bcd2bin(alarmvals[2] & 0x3f);
	alrm->time.tm_mday = bcd2bin(alarmvals[1] & 0x3f);
	alrm->time.tm_mon  = bcd2bin(alarmvals[0] & 0x3f) - 1;

	alrm->enabled = !!(alarmvals[0] & M41T80_ALMON_AFE);
	alrm->pending = (flags & M41T80_FLAGS_AF) && alrm->enabled;

	return 0;
}

static const struct rtc_class_ops m41t80_rtc_ops = {
	.read_time = m41t80_rtc_read_time,
	.set_time = m41t80_rtc_set_time,
	.proc = m41t80_rtc_proc,
	.read_alarm = m41t80_read_alarm,
	.set_alarm = m41t80_set_alarm,
	.alarm_irq_enable = m41t80_alarm_irq_enable,
};

#ifdef CONFIG_PM_SLEEP
static int m41t80_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client->irq >= 0 && device_may_wakeup(dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int m41t80_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client->irq >= 0 && device_may_wakeup(dev))
		disable_irq_wake(client->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(m41t80_pm, m41t80_suspend, m41t80_resume);

#ifdef CONFIG_COMMON_CLK
#define sqw_to_m41t80_data(_hw) container_of(_hw, struct m41t80_data, sqw)

static unsigned long m41t80_decode_freq(int setting)
{
	return (setting == 0) ? 0 : (setting == 1) ? M41T80_SQW_MAX_FREQ :
		M41T80_SQW_MAX_FREQ >> setting;
}

static unsigned long m41t80_get_freq(struct m41t80_data *m41t80)
{
	struct i2c_client *client = m41t80->client;
	int reg_sqw = (m41t80->features & M41T80_FEATURE_SQ_ALT) ?
		M41T80_REG_WDAY : M41T80_REG_SQW;
	int ret = i2c_smbus_read_byte_data(client, reg_sqw);

	if (ret < 0)
		return 0;
	return m41t80_decode_freq(ret >> 4);
}

static unsigned long m41t80_sqw_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	return sqw_to_m41t80_data(hw)->freq;
}

static long m41t80_sqw_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	if (rate >= M41T80_SQW_MAX_FREQ)
		return M41T80_SQW_MAX_FREQ;
	if (rate >= M41T80_SQW_MAX_FREQ / 4)
		return M41T80_SQW_MAX_FREQ / 4;
	if (!rate)
		return 0;
	return 1 << ilog2(rate);
}

static int m41t80_sqw_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct m41t80_data *m41t80 = sqw_to_m41t80_data(hw);
	struct i2c_client *client = m41t80->client;
	int reg_sqw = (m41t80->features & M41T80_FEATURE_SQ_ALT) ?
		M41T80_REG_WDAY : M41T80_REG_SQW;
	int reg, ret, val = 0;

	if (rate >= M41T80_SQW_MAX_FREQ)
		val = 1;
	else if (rate >= M41T80_SQW_MAX_FREQ / 4)
		val = 2;
	else if (rate)
		val = 15 - ilog2(rate);

	reg = i2c_smbus_read_byte_data(client, reg_sqw);
	if (reg < 0)
		return reg;

	reg = (reg & 0x0f) | (val << 4);

	ret = i2c_smbus_write_byte_data(client, reg_sqw, reg);
	if (!ret)
		m41t80->freq = m41t80_decode_freq(val);
	return ret;
}

static int m41t80_sqw_control(struct clk_hw *hw, bool enable)
{
	struct m41t80_data *m41t80 = sqw_to_m41t80_data(hw);
	struct i2c_client *client = m41t80->client;
	int ret = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_MON);

	if (ret < 0)
		return ret;

	if (enable)
		ret |= M41T80_ALMON_SQWE;
	else
		ret &= ~M41T80_ALMON_SQWE;

	ret = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON, ret);
	if (!ret)
		m41t80->sqwe = enable;
	return ret;
}

static int m41t80_sqw_prepare(struct clk_hw *hw)
{
	return m41t80_sqw_control(hw, 1);
}

static void m41t80_sqw_unprepare(struct clk_hw *hw)
{
	m41t80_sqw_control(hw, 0);
}

static int m41t80_sqw_is_prepared(struct clk_hw *hw)
{
	return sqw_to_m41t80_data(hw)->sqwe;
}

static const struct clk_ops m41t80_sqw_ops = {
	.prepare = m41t80_sqw_prepare,
	.unprepare = m41t80_sqw_unprepare,
	.is_prepared = m41t80_sqw_is_prepared,
	.recalc_rate = m41t80_sqw_recalc_rate,
	.round_rate = m41t80_sqw_round_rate,
	.set_rate = m41t80_sqw_set_rate,
};

static struct clk *m41t80_sqw_register_clk(struct m41t80_data *m41t80)
{
	struct i2c_client *client = m41t80->client;
	struct device_node *node = client->dev.of_node;
	struct device_node *fixed_clock;
	struct clk *clk;
	struct clk_init_data init;
	int ret;

	fixed_clock = of_get_child_by_name(node, "clock");
	if (fixed_clock) {
		/*
		 * skip registering square wave clock when a fixed
		 * clock has been registered. The fixed clock is
		 * registered automatically when being referenced.
		 */
		of_node_put(fixed_clock);
		return NULL;
	}

	/* First disable the clock */
	ret = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_MON);
	if (ret < 0)
		return ERR_PTR(ret);
	ret = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_MON,
					ret & ~(M41T80_ALMON_SQWE));
	if (ret < 0)
		return ERR_PTR(ret);

	init.name = "m41t80-sqw";
	init.ops = &m41t80_sqw_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;
	m41t80->sqw.init = &init;
	m41t80->freq = m41t80_get_freq(m41t80);

	/* optional override of the clockname */
	of_property_read_string(node, "clock-output-names", &init.name);

	/* register the clock */
	clk = clk_register(&client->dev, &m41t80->sqw);
	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return clk;
}
#endif

#ifdef CONFIG_RTC_DRV_M41T80_WDT
/*
 *****************************************************************************
 *
 * Watchdog Driver
 *
 *****************************************************************************
 */
static DEFINE_MUTEX(m41t80_rtc_mutex);
static struct i2c_client *save_client;

/* Default margin */
#define WD_TIMO 60		/* 1..31 seconds */

static int wdt_margin = WD_TIMO;
module_param(wdt_margin, int, 0);
MODULE_PARM_DESC(wdt_margin, "Watchdog timeout in seconds (default 60s)");

static unsigned long wdt_is_open;
static int boot_flag;

/**
 *	wdt_ping - Reload counter one with the watchdog timeout.
 *	We don't bother reloading the cascade counter.
 */
static void wdt_ping(void)
{
	unsigned char i2c_data[2];
	struct i2c_msg msgs1[1] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= i2c_data,
		},
	};
	struct m41t80_data *clientdata = i2c_get_clientdata(save_client);

	i2c_data[0] = 0x09;		/* watchdog register */

	if (wdt_margin > 31)
		i2c_data[1] = (wdt_margin & 0xFC) | 0x83; /* resolution = 4s */
	else
		/*
		 * WDS = 1 (0x80), mulitplier = WD_TIMO, resolution = 1s (0x02)
		 */
		i2c_data[1] = wdt_margin << 2 | 0x82;

	/*
	 * M41T65 has three bits for watchdog resolution.  Don't set bit 7, as
	 * that would be an invalid resolution.
	 */
	if (clientdata->features & M41T80_FEATURE_WD)
		i2c_data[1] &= ~M41T80_WATCHDOG_RB2;

	i2c_transfer(save_client->adapter, msgs1, 1);
}

/**
 *	wdt_disable - disables watchdog.
 */
static void wdt_disable(void)
{
	unsigned char i2c_data[2], i2c_buf[0x10];
	struct i2c_msg msgs0[2] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= i2c_data,
		},
		{
			.addr	= save_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= i2c_buf,
		},
	};
	struct i2c_msg msgs1[1] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= i2c_data,
		},
	};

	i2c_data[0] = 0x09;
	i2c_transfer(save_client->adapter, msgs0, 2);

	i2c_data[0] = 0x09;
	i2c_data[1] = 0x00;
	i2c_transfer(save_client->adapter, msgs1, 1);
}

/**
 *	wdt_write - write to watchdog.
 *	@file: file handle to the watchdog
 *	@buf: buffer to write (unused as data does not matter here
 *	@count: count of bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we we don't define content meaning.
 */
static ssize_t wdt_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	if (count) {
		wdt_ping();
		return 1;
	}
	return 0;
}

static ssize_t wdt_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	return 0;
}

/**
 *	wdt_ioctl - ioctl handler to set watchdog.
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features. We only actually usefully support
 *	querying capabilities and current status.
 */
static int wdt_ioctl(struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	int new_margin, rv;
	static struct watchdog_info ident = {
		.options = WDIOF_POWERUNDER | WDIOF_KEEPALIVEPING |
			WDIOF_SETTIMEOUT,
		.firmware_version = 1,
		.identity = "M41T80 WTD"
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info __user *)arg, &ident,
				    sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(boot_flag, (int __user *)arg);
	case WDIOC_KEEPALIVE:
		wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *)arg))
			return -EFAULT;
		/* Arbitrary, can't find the card's limits */
		if (new_margin < 1 || new_margin > 124)
			return -EINVAL;
		wdt_margin = new_margin;
		wdt_ping();
		fallthrough;
	case WDIOC_GETTIMEOUT:
		return put_user(wdt_margin, (int __user *)arg);

	case WDIOC_SETOPTIONS:
		if (copy_from_user(&rv, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		if (rv & WDIOS_DISABLECARD) {
			pr_info("disable watchdog\n");
			wdt_disable();
		}

		if (rv & WDIOS_ENABLECARD) {
			pr_info("enable watchdog\n");
			wdt_ping();
		}

		return -EINVAL;
	}
	return -ENOTTY;
}

static long wdt_unlocked_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	int ret;

	mutex_lock(&m41t80_rtc_mutex);
	ret = wdt_ioctl(file, cmd, arg);
	mutex_unlock(&m41t80_rtc_mutex);

	return ret;
}

/**
 *	wdt_open - open a watchdog.
 *	@inode: inode of device
 *	@file: file handle to device
 *
 */
static int wdt_open(struct inode *inode, struct file *file)
{
	if (iminor(inode) == WATCHDOG_MINOR) {
		mutex_lock(&m41t80_rtc_mutex);
		if (test_and_set_bit(0, &wdt_is_open)) {
			mutex_unlock(&m41t80_rtc_mutex);
			return -EBUSY;
		}
		/*
		 *	Activate
		 */
		wdt_is_open = 1;
		mutex_unlock(&m41t80_rtc_mutex);
		return stream_open(inode, file);
	}
	return -ENODEV;
}

/**
 *	wdt_release - release a watchdog.
 *	@inode: inode to board
 *	@file: file handle to board
 *
 */
static int wdt_release(struct inode *inode, struct file *file)
{
	if (iminor(inode) == WATCHDOG_MINOR)
		clear_bit(0, &wdt_is_open);
	return 0;
}

/**
 *	wdt_notify_sys - notify to watchdog.
 *	@this: our notifier block
 *	@code: the event being reported
 *	@unused: unused
 *
 *	Our notifier is called on system shutdowns. We want to turn the card
 *	off at reboot otherwise the machine will reboot again during memory
 *	test or worse yet during the following fsck. This would suck, in fact
 *	trust me - if it happens it does suck.
 */
static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
			  void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Disable Watchdog */
		wdt_disable();
	return NOTIFY_DONE;
}

static const struct file_operations wdt_fops = {
	.owner	= THIS_MODULE,
	.read	= wdt_read,
	.unlocked_ioctl = wdt_unlocked_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.write	= wdt_write,
	.open	= wdt_open,
	.release = wdt_release,
	.llseek = no_llseek,
};

static struct miscdevice wdt_dev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */
static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};
#endif /* CONFIG_RTC_DRV_M41T80_WDT */

/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */

static int m41t80_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int rc = 0;
	struct rtc_time tm;
	struct m41t80_data *m41t80_data = NULL;
	bool wakeup_source = false;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK |
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev, "doesn't support I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_I2C_BLOCK\n");
		return -ENODEV;
	}

	m41t80_data = devm_kzalloc(&client->dev, sizeof(*m41t80_data),
				   GFP_KERNEL);
	if (!m41t80_data)
		return -ENOMEM;

	m41t80_data->client = client;
	if (client->dev.of_node) {
		m41t80_data->features = (unsigned long)
			of_device_get_match_data(&client->dev);
	} else {
		const struct i2c_device_id *id = i2c_match_id(m41t80_id, client);
		m41t80_data->features = id->driver_data;
	}
	i2c_set_clientdata(client, m41t80_data);

	m41t80_data->rtc =  devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(m41t80_data->rtc))
		return PTR_ERR(m41t80_data->rtc);

#ifdef CONFIG_OF
	wakeup_source = of_property_read_bool(client->dev.of_node,
					      "wakeup-source");
#endif
	if (client->irq > 0) {
		rc = devm_request_threaded_irq(&client->dev, client->irq,
					       NULL, m41t80_handle_irq,
					       IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					       "m41t80", client);
		if (rc) {
			dev_warn(&client->dev, "unable to request IRQ, alarms disabled\n");
			client->irq = 0;
			wakeup_source = false;
		}
	}
	if (client->irq > 0 || wakeup_source)
		device_init_wakeup(&client->dev, true);
	else
		clear_bit(RTC_FEATURE_ALARM, m41t80_data->rtc->features);

	m41t80_data->rtc->ops = &m41t80_rtc_ops;
	m41t80_data->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	m41t80_data->rtc->range_max = RTC_TIMESTAMP_END_2099;

	if (client->irq <= 0)
		clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, m41t80_data->rtc->features);

	/* Make sure HT (Halt Update) bit is cleared */
	rc = i2c_smbus_read_byte_data(client, M41T80_REG_ALARM_HOUR);

	if (rc >= 0 && rc & M41T80_ALHOUR_HT) {
		if (m41t80_data->features & M41T80_FEATURE_HT) {
			m41t80_rtc_read_time(&client->dev, &tm);
			dev_info(&client->dev, "HT bit was set!\n");
			dev_info(&client->dev, "Power Down at %ptR\n", &tm);
		}
		rc = i2c_smbus_write_byte_data(client, M41T80_REG_ALARM_HOUR,
					       rc & ~M41T80_ALHOUR_HT);
	}

	if (rc < 0) {
		dev_err(&client->dev, "Can't clear HT bit\n");
		return rc;
	}

	/* Make sure ST (stop) bit is cleared */
	rc = i2c_smbus_read_byte_data(client, M41T80_REG_SEC);

	if (rc >= 0 && rc & M41T80_SEC_ST)
		rc = i2c_smbus_write_byte_data(client, M41T80_REG_SEC,
					       rc & ~M41T80_SEC_ST);
	if (rc < 0) {
		dev_err(&client->dev, "Can't clear ST bit\n");
		return rc;
	}

#ifdef CONFIG_RTC_DRV_M41T80_WDT
	if (m41t80_data->features & M41T80_FEATURE_HT) {
		save_client = client;
		rc = misc_register(&wdt_dev);
		if (rc)
			return rc;
		rc = register_reboot_notifier(&wdt_notifier);
		if (rc) {
			misc_deregister(&wdt_dev);
			return rc;
		}
	}
#endif
#ifdef CONFIG_COMMON_CLK
	if (m41t80_data->features & M41T80_FEATURE_SQ)
		m41t80_sqw_register_clk(m41t80_data);
#endif

	rc = devm_rtc_register_device(m41t80_data->rtc);
	if (rc)
		return rc;

	return 0;
}

static void m41t80_remove(struct i2c_client *client)
{
#ifdef CONFIG_RTC_DRV_M41T80_WDT
	struct m41t80_data *clientdata = i2c_get_clientdata(client);

	if (clientdata->features & M41T80_FEATURE_HT) {
		misc_deregister(&wdt_dev);
		unregister_reboot_notifier(&wdt_notifier);
	}
#endif
}

static struct i2c_driver m41t80_driver = {
	.driver = {
		.name = "rtc-m41t80",
		.of_match_table = of_match_ptr(m41t80_of_match),
		.pm = &m41t80_pm,
	},
	.probe_new = m41t80_probe,
	.remove = m41t80_remove,
	.id_table = m41t80_id,
};

module_i2c_driver(m41t80_driver);

MODULE_AUTHOR("Alexander Bigga <ab@mycable.de>");
MODULE_DESCRIPTION("ST Microelectronics M41T80 series RTC I2C Client Driver");
MODULE_LICENSE("GPL");
