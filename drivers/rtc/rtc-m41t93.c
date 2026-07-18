// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Driver for ST M41T93 SPI RTC
 *
 * (c) 2010 Nikolaus Voss, Weinmann Medical GmbH
 */

#include <linux/bcd.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/clk-provider.h>
#include <linux/watchdog.h>

#define M41T93_REG_SSEC			0
#define M41T93_REG_ST_SEC		1
#define M41T93_REG_MIN			2
#define M41T93_REG_CENT_HOUR		3
#define M41T93_REG_WDAY			4
#define M41T93_REG_DAY			5
#define M41T93_REG_MON			6
#define M41T93_REG_YEAR			7
#define M41T93_REG_AL1_MONTH		0xa
#define M41T93_REG_AL1_DATE		0xb
#define M41T93_REG_AL1_HOUR		0xc
#define M41T93_REG_AL1_MIN		0xd
#define M41T93_REG_AL1_SEC		0xe
#define M41T93_BIT_A1IE                 BIT(7)
#define M41T93_BIT_ABE                  BIT(5)
#define M41T93_FLAG_AF1                 BIT(6)
#define M41T93_SRAM_BASE		0x19
#define M41T93_REG_SQW			0x13
#define M41T93_SQW_RS_MASK		0xf0
#define M41T93_SQW_RS_SHIFT		4
#define M41T93_BIT_SQWE                 BIT(6)
#define M41T93_REG_WATCHDOG		0x9
#define M41T93_WDT_RB_MASK		0x3
#define M41T93_WDT_BMB_MASK		0x7c
#define M41T93_WDT_BMB_SHIFT		2

#define M41T93_REG_ALM_HOUR_HT		0xc
#define M41T93_REG_FLAGS		0xf

#define M41T93_FLAG_ST			(1 << 7)
#define M41T93_FLAG_OF			(1 << 2)
#define M41T93_FLAG_BL			(1 << 4)
#define M41T93_FLAG_HT			(1 << 6)

struct m41t93_data {
	struct rtc_device *rtc;
	struct regmap *regmap;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw clks;
#endif
#ifdef CONFIG_WATCHDOG
	struct watchdog_device wdd;
#endif
};

static int m41t93_set_time(struct device *dev, struct rtc_time *tm)
{
	struct m41t93_data *m41t93 = dev_get_drvdata(dev);
	int tmp, ret;
	u8 buf[8] = {0};        /* 8 data bytes */
	u8 * const data = &buf[0]; /* ptr to first data byte */

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", tm->tm_sec, tm->tm_min,
		tm->tm_hour, tm->tm_mday,
		tm->tm_mon, tm->tm_year, tm->tm_wday);

	if (tm->tm_year < 100) {
		dev_warn(dev, "unsupported date (before 2000-01-01).\n");
		return -EINVAL;
	}

	ret = regmap_read(m41t93->regmap, M41T93_REG_FLAGS, &tmp);
	if (ret < 0)
		return ret;

	if (tmp & M41T93_FLAG_OF) {
		dev_warn(dev, "OF bit is set, resetting.\n");
		regmap_write(m41t93->regmap, M41T93_REG_FLAGS, tmp & ~M41T93_FLAG_OF);

		ret = regmap_read(m41t93->regmap, M41T93_REG_FLAGS, &tmp);
		if (ret < 0) {
			return ret;
		} else if (tmp & M41T93_FLAG_OF) {
			/* OF cannot be immediately reset: oscillator has to be
			 * restarted. */
			u8 reset_osc = buf[M41T93_REG_ST_SEC] | M41T93_FLAG_ST;

			dev_warn(dev,
				 "OF bit is still set, kickstarting clock.\n");
			regmap_write(m41t93->regmap, M41T93_REG_ST_SEC, reset_osc);
			reset_osc &= ~M41T93_FLAG_ST;
			regmap_write(m41t93->regmap, M41T93_REG_ST_SEC, reset_osc);
		}
	}

	data[M41T93_REG_SSEC]		= 0;
	data[M41T93_REG_ST_SEC]		= bin2bcd(tm->tm_sec);
	data[M41T93_REG_MIN]		= bin2bcd(tm->tm_min);
	data[M41T93_REG_CENT_HOUR]	= bin2bcd(tm->tm_hour) |
						((tm->tm_year/100-1) << 6);
	data[M41T93_REG_DAY]		= bin2bcd(tm->tm_mday);
	data[M41T93_REG_WDAY]		= bin2bcd(tm->tm_wday + 1);
	data[M41T93_REG_MON]		= bin2bcd(tm->tm_mon + 1);
	data[M41T93_REG_YEAR]		= bin2bcd(tm->tm_year % 100);

	return regmap_bulk_write(m41t93->regmap, M41T93_REG_SSEC, buf, sizeof(buf));
}


static int m41t93_get_time(struct device *dev, struct rtc_time *tm)
{
	struct m41t93_data *m41t93 = dev_get_drvdata(dev);
	u8 buf[8];
	int century_after_1900;
	int tmp;
	int ret = 0;

	/* Check status of clock. Two states must be considered:
	   1. halt bit (HT) is set: the clock is running but update of readout
	      registers has been disabled due to power failure. This is normal
	      case after poweron. Time is valid after resetting HT bit.
	   2. oscillator fail bit (OF) is set: time is invalid.
	*/
	ret = regmap_read(m41t93->regmap, M41T93_REG_ALM_HOUR_HT, &tmp);
	if (ret < 0)
		return ret;

	if (tmp & M41T93_FLAG_HT) {
		dev_dbg(dev, "HT bit is set, reenable clock update.\n");
		regmap_write(m41t93->regmap, M41T93_REG_ALM_HOUR_HT,
			     tmp & ~M41T93_FLAG_HT);
	}

	ret = regmap_read(m41t93->regmap, M41T93_REG_FLAGS, &tmp);
	if (ret < 0)
		return ret;

	if (tmp & M41T93_FLAG_OF) {
		ret = -EINVAL;
		dev_warn(dev, "OF bit is set, write time to restart.\n");
	}

	if (tmp & M41T93_FLAG_BL)
		dev_warn(dev, "BL bit is set, replace battery.\n");

	/* read actual time/date */
	ret = regmap_bulk_read(m41t93->regmap, M41T93_REG_SSEC, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	tm->tm_sec	= bcd2bin(buf[M41T93_REG_ST_SEC]);
	tm->tm_min	= bcd2bin(buf[M41T93_REG_MIN]);
	tm->tm_hour	= bcd2bin(buf[M41T93_REG_CENT_HOUR] & 0x3f);
	tm->tm_mday	= bcd2bin(buf[M41T93_REG_DAY]);
	tm->tm_mon	= bcd2bin(buf[M41T93_REG_MON]) - 1;
	tm->tm_wday	= bcd2bin(buf[M41T93_REG_WDAY] & 0x0f) - 1;

	century_after_1900 = (buf[M41T93_REG_CENT_HOUR] >> 6) + 1;
	tm->tm_year = bcd2bin(buf[M41T93_REG_YEAR]) + century_after_1900 * 100;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", tm->tm_sec, tm->tm_min,
		tm->tm_hour, tm->tm_mday,
		tm->tm_mon, tm->tm_year, tm->tm_wday);

	return ret;
}

static int m41t93_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct m41t93_data *m41t93 = dev_get_drvdata(dev);
	int ret;
	unsigned int val;
	u8 alarm_vals[5] = {0};

	ret = regmap_bulk_write(m41t93->regmap, M41T93_REG_AL1_DATE, alarm_vals, 4);
	if (ret)
		return ret;

	/* Set alarm values */
	alarm_vals[0] = bin2bcd(alrm->time.tm_mon + 1) & 0x1f;
	alarm_vals[1] = bin2bcd(alrm->time.tm_mday) & 0x3f;
	alarm_vals[2] = bin2bcd(alrm->time.tm_hour) & 0x3f;
	alarm_vals[3] = bin2bcd(alrm->time.tm_min) & 0x7f;
	alarm_vals[4] = bin2bcd(alrm->time.tm_sec) & 0x7f;

	if (alrm->enabled) {
		/* Enable alarm IRQ generation */
		alarm_vals[0] |= M41T93_BIT_A1IE | M41T93_BIT_ABE;
	}

	/* Preserve SQWE bit */
	ret = regmap_read(m41t93->regmap, M41T93_REG_AL1_MONTH, &val);
	if (ret)
		return ret;

	alarm_vals[0] |= val & 0x40;

	ret = regmap_bulk_write(m41t93->regmap, M41T93_REG_AL1_MONTH,
				alarm_vals, sizeof(alarm_vals));
	if (ret)
		return ret;

	/* Device address pointer is now at FLAG register, move it to other location
	 * to finish setting alarm, as recommended by the datasheet.
	 * We do read of AL1_MONTH register to achieve this.
	 */
	ret = regmap_read(m41t93->regmap, M41T93_REG_AL1_MONTH, &val);
	if (ret)
		return ret;

	if (bcd2bin(val & 0x1f) == (alrm->time.tm_mon & 0x1f))
		dev_notice(dev, "Alarm set successfully\n");

	return 0;
}

static int m41t93_get_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct m41t93_data *m41t93 = dev_get_drvdata(dev);
	int ret;
	unsigned int val;
	u8 alarm_vals[5] = {0};

	ret = regmap_bulk_read(m41t93->regmap, M41T93_REG_AL1_MONTH,
			       alarm_vals, sizeof(alarm_vals));
	if (ret)
		return ret;

	alrm->time.tm_mon = bcd2bin(alarm_vals[0] & 0x1f) - 1;
	alrm->time.tm_mday = bcd2bin(alarm_vals[1] & 0x3f);
	alrm->time.tm_hour = bcd2bin(alarm_vals[2] & 0x3f);
	alrm->time.tm_min = bcd2bin(alarm_vals[3] & 0x7f);
	alrm->time.tm_sec = bcd2bin(alarm_vals[4] & 0x7f);

	alrm->enabled =  !!(alarm_vals[0] & M41T93_BIT_A1IE);

	ret = regmap_read(m41t93->regmap, M41T93_REG_FLAGS, &val);
	if (ret)
		return ret;

	alrm->pending = (val & M41T93_FLAG_AF1) && alrm->enabled;

	return 0;
}

static int m41t93_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct m41t93_data *m41t93 = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	val = enabled ? M41T93_BIT_A1IE | M41T93_BIT_ABE : 0;

	ret = regmap_update_bits(m41t93->regmap, M41T93_REG_AL1_MONTH,
				 M41T93_BIT_A1IE | M41T93_BIT_ABE, val);
	if (ret)
		return ret;

	return 0;
}


static const struct rtc_class_ops m41t93_rtc_ops = {
	.read_time	= m41t93_get_time,
	.set_time	= m41t93_set_time,
	.set_alarm	= m41t93_set_alarm,
	.read_alarm	= m41t93_get_alarm,
	.alarm_irq_enable = m41t93_alarm_irq_enable,
};

#ifdef CONFIG_COMMON_CLK
#define clk_sqw_to_m41t93_data(clk)	\
	container_of(clk, struct m41t93_data, clks)

/* m41t93 RTC clock output support */
static unsigned long m41t93_clk_rates[] = {
	0,
	32768, /* RS3:RS0 = 0b0001 */
	8192,
	4096,
	2048,
	1024,
	512,
	256,
	128,
	64,
	32,
	16,
	8,
	4,
	2,
	1, /* RS3:RS0 = 0b1111 */
};

static unsigned long m41t93_clk_sqw_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	int ret;
	unsigned int rate_id;
	struct m41t93_data *m41t93 = clk_sqw_to_m41t93_data(hw);

	ret = regmap_read(m41t93->regmap, M41T93_REG_SQW, &rate_id);
	if (ret)
		return ret;

	rate_id &= M41T93_SQW_RS_MASK;
	rate_id >>= M41T93_SQW_RS_SHIFT;

	return m41t93_clk_rates[rate_id];
}

static int m41t93_clk_sqw_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(m41t93_clk_rates); i++) {
		if (req->rate >= m41t93_clk_rates[i]) {
			req->rate = m41t93_clk_rates[i];
			return 0;
		}
	}

	return 0;
}

static int m41t93_clk_sqw_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	int id, ret;
	struct m41t93_data *m41t93 = clk_sqw_to_m41t93_data(hw);

	for (id = 0; id < ARRAY_SIZE(m41t93_clk_rates); id++) {
		if (m41t93_clk_rates[id] == rate)
			break;
	}

	if (id >= ARRAY_SIZE(m41t93_clk_rates))
		return -EINVAL;

	ret = regmap_update_bits(m41t93->regmap, M41T93_REG_SQW,
				 M41T93_SQW_RS_MASK, id << M41T93_SQW_RS_SHIFT);

	return ret;
}

static int m41t93_clk_sqw_prepare(struct clk_hw *hw)
{
	int ret;
	struct m41t93_data *m41t93 = clk_sqw_to_m41t93_data(hw);

	ret = regmap_update_bits(m41t93->regmap, M41T93_REG_AL1_MONTH,
				 M41T93_BIT_SQWE, M41T93_BIT_SQWE);

	return ret;
}

static void m41t93_clk_sqw_unprepare(struct clk_hw *hw)
{
	struct m41t93_data *m41t93 = clk_sqw_to_m41t93_data(hw);

	regmap_update_bits(m41t93->regmap, M41T93_REG_AL1_MONTH,
			   M41T93_BIT_SQWE, 0);
}

static int m41t93_clk_sqw_is_prepared(struct clk_hw *hw)
{
	int ret;
	struct m41t93_data *m41t93 = clk_sqw_to_m41t93_data(hw);
	unsigned int status;

	ret = regmap_read(m41t93->regmap, M41T93_REG_AL1_MONTH, &status);
	if (ret)
		return ret;

	return !!(status & M41T93_BIT_SQWE);
}

static const struct clk_ops m41t93_clk_sqw_ops = {
	.prepare = m41t93_clk_sqw_prepare,
	.unprepare = m41t93_clk_sqw_unprepare,
	.is_prepared = m41t93_clk_sqw_is_prepared,
	.recalc_rate = m41t93_clk_sqw_recalc_rate,
	.set_rate = m41t93_clk_sqw_set_rate,
	.determine_rate = m41t93_clk_sqw_determine_rate,
};

static int rtc_m41t93_clks_register(struct device *dev, struct m41t93_data *m41t93)
{
	struct device_node *node = dev->of_node;
	struct clk *clk;
	struct clk_init_data init = {0};

	init.name = "m41t93_clk_sqw";
	init.ops = &m41t93_clk_sqw_ops;

	m41t93->clks.init = &init;

	/* Register the clock with CCF */
	clk = devm_clk_register(dev, &m41t93->clks);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (node)
		of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return 0;
}
#endif

#ifdef CONFIG_WATCHDOG
static int m41t93_wdt_ping(struct watchdog_device *wdd)
{
	u8 resolution, mult;
	u8 val = 0;
	int ret;
	struct m41t93_data *m41t93 = watchdog_get_drvdata(wdd);

	/*  Resolution supported by hardware
	 *  0b00 : 1/16 seconds
	 *  0b01 : 1/4 second
	 *  0b10 : 1 second
	 *  0b11 : 4 seconds
	 */
	resolution = 0x2; /* hardcode resolution to 1s */
	mult = wdd->timeout;
	val = resolution | (mult << M41T93_WDT_BMB_SHIFT &  M41T93_WDT_BMB_MASK);

	ret = regmap_write_bits(m41t93->regmap, M41T93_REG_WATCHDOG,
				M41T93_WDT_RB_MASK | M41T93_WDT_BMB_MASK, val);

	return ret;
}

static int m41t93_wdt_start(struct watchdog_device *wdd)
{
	return m41t93_wdt_ping(wdd);
}

static int m41t93_wdt_stop(struct watchdog_device *wdd)
{
	struct m41t93_data *m41t93 = watchdog_get_drvdata(wdd);

	/* Write 0 to watchdog register */
	return regmap_write_bits(m41t93->regmap, M41T93_REG_WATCHDOG,
				  M41T93_WDT_RB_MASK | M41T93_WDT_BMB_MASK, 0);
}

static int m41t93_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int new_timeout)
{
	wdd->timeout = new_timeout;

	return 0;
}

static const struct watchdog_info m41t93_wdt_info = {
	.identity = "m41t93 rtc Watchdog",
	.options = WDIOF_ALARMONLY | WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops m41t93_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = m41t93_wdt_start,
	.stop = m41t93_wdt_stop,
	.ping = m41t93_wdt_ping,
	.set_timeout = m41t93_wdt_set_timeout,
};

static int m41t93_watchdog_register(struct device *dev, struct m41t93_data *m41t93)
{
	int ret;

	m41t93->wdd.parent = dev;
	m41t93->wdd.info = &m41t93_wdt_info;
	m41t93->wdd.ops = &m41t93_watchdog_ops;
	m41t93->wdd.min_timeout = 0;
	m41t93->wdd.max_timeout = 10;
	m41t93->wdd.timeout = 3; /* Default timeout is 3 sec */
	m41t93->wdd.status = WATCHDOG_NOWAYOUT_INIT_STATUS;

	watchdog_set_drvdata(&m41t93->wdd, m41t93);

	ret = devm_watchdog_register_device(dev, &m41t93->wdd);
	if (ret) {
		dev_warn(dev, "Failed to register watchdog\n");
		return ret;
	}

	/* Disable watchdog at start */
	ret = m41t93_wdt_stop(&m41t93->wdd);

	return ret;
}
#endif

static struct spi_driver m41t93_driver;

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.read_flag_mask = 0x00,
	.write_flag_mask = 0x80,
	.zero_flag_mask = true,
};

static int m41t93_probe(struct spi_device *spi)
{
	int res, ret;
	struct m41t93_data *m41t93;

	spi->bits_per_word = 8;
	spi_setup(spi);

	m41t93 = devm_kzalloc(&spi->dev, sizeof(struct m41t93_data), GFP_KERNEL);

	if (!m41t93)
		return -ENOMEM;

	/* Set up regmap to access device registers*/
	m41t93->regmap = devm_regmap_init_spi(spi, &regmap_config);
	if (IS_ERR(m41t93->regmap)) {
		dev_err(&spi->dev, "regmap init failure\n");
		return PTR_ERR(m41t93->regmap);
	}

	ret = regmap_read(m41t93->regmap, M41T93_REG_WDAY, &res);
	if (ret < 0) {
		dev_err(&spi->dev, "IO error\n");
		return -EIO;
	}

	if (res < 0 || (res & 0xf8) != 0) {
		dev_err(&spi->dev, "not found 0x%x.\n", res);
		return -ENODEV;
	}

	spi_set_drvdata(spi, m41t93);

	m41t93->rtc = devm_rtc_device_register(&spi->dev, m41t93_driver.driver.name,
					       &m41t93_rtc_ops, THIS_MODULE);
	if (IS_ERR(m41t93->rtc))
		return PTR_ERR(m41t93->rtc);

#ifdef CONFIG_COMMON_CLK
	ret = rtc_m41t93_clks_register(&spi->dev, m41t93);
	if (ret)
		dev_warn(&spi->dev, "Unable to register clock\n");
#endif
#ifdef CONFIG_WATCHDOG
	ret = m41t93_watchdog_register(&spi->dev, m41t93);
	if (ret)
		dev_warn(&spi->dev, "Unable to register watchdog\n");
#endif

	return 0;
}

static const struct of_device_id m41t93_dt_match[] = {
	{ .compatible = "st,m41t93" },
	{ }
};
MODULE_DEVICE_TABLE(of, m41t93_dt_match);

static struct spi_driver m41t93_driver = {
	.driver = {
		.name	= "rtc-m41t93",
		.of_match_table = m41t93_dt_match,
	},
	.probe	= m41t93_probe,
};

module_spi_driver(m41t93_driver);

MODULE_AUTHOR("Nikolaus Voss <n.voss@weinmann.de>");
MODULE_DESCRIPTION("Driver for ST M41T93 SPI RTC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-m41t93");
