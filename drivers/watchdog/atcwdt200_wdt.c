// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Andes ATCWDT200 watchdog timer driver.
 *
 * Copyright (C) 2025 Andes Technology Corporation
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

/* Register definitions */
#define REG_CTRL		0x10
#define REG_RESTART		0x14
#define REG_WRITE_EN		0x18
#define REG_STATUS		0x1C

/* Control Register */
#define CTRL_RST_TIME_MSK	GENMASK(10, 8)
#define CTRL_RST_TIME_SET(x)	FIELD_PREP(CTRL_RST_TIME_MSK, x)
#define CTRL_INT_TIME_MSK	GENMASK(7, 4)
#define CTRL_INT_TIME_SET(x)	FIELD_PREP(CTRL_INT_TIME_MSK, x)
#define CTRL_INT_TIME_GET(x)	FIELD_GET(CTRL_INT_TIME_MSK, x)
#define CTRL_RST_EN		BIT(3)
#define CTRL_CLK_SEL		BIT(1)
#define CTRL_CLK_SEL_PCLK	1
#define CTRL_CLK_SEL_SET(x)	FIELD_PREP(CTRL_CLK_SEL, x)
#define CTRL_WDT_EN		BIT(0)

/* Restart Register */
#define RESTART_MAGIC		0xCAFE

/* Write Enable Register */
#define WRITE_EN_MAGIC		0x5AA5

/* Status Register */
#define STATUS_INT_EXPIRED	BIT(1)

/* The default timeout value in seconds */
#define ATCWDT_TIMEOUT		4

/* Define the array size for each timer type */
#define TMR_SZ_RST		8
#define TMR_SZ_INT_16		8
#define TMR_SZ_INT_32		16

#define DRV_NAME		"atcwdt200"
/**
 * enum timer_type - Supported timer types for ATCWDT200 watchdog driver
 * @TMR_RST:      Reset timer (non-interrupt).
 * @TMR_INT_16:   16-bit interrupt timer supported by hardware.
 * @TMR_INT_32:   32-bit interrupt timer supported by hardware.
 * @TMR_UNKNOWN:  Timer type cannot be determined.
 */
enum timer_type {
	TMR_RST,
	TMR_INT_16,
	TMR_INT_32,
	TMR_UNKNOWN
};

static unsigned int timeout = ATCWDT_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;

/**
 * struct atcwdt_drv - ATCWDT200 watchdog driver private data
 * @wdt_dev:        Watchdog device used by the watchdog framework.
 * @regmap:         Register map for accessing hardware registers.
 * @clk:            Hardware clock used by the watchdog timer.
 * @lock:           Spinlock protecting register accesses and driver state.
 * @clk_freq:       Input clock frequency of the ATCWDT200.
 * @clk_src:        Selected clock source for the watchdog timer.
 * @int_timer_type: Detected interrupt timer type (16-bit, 32-bit, or unknown).
 */
struct atcwdt_drv {
	struct watchdog_device	wdt_dev;
	struct regmap		*regmap;
	struct clk		*clk;
	spinlock_t		lock;
	unsigned int		clk_freq;
	unsigned char		clk_src;
	unsigned char		int_timer_type;
};

static const struct watchdog_info atcwdt_info = {
	.identity = DRV_NAME,
	.options = WDIOF_SETTIMEOUT |
		   WDIOF_KEEPALIVEPING |
		   WDIOF_MAGICCLOSE,
};

/**
 * atcwdt_get_index - Get the interval value for the specified timer type
 * @index: The index of the interval in the array
 * @timer_type: The type of timer, which can be TMR_RST, TMR_INT_16, or
 *              TMR_INT_32.
 *
 * This function retrieves the interval value based on the timer type and
 * ensures the index stays within the valid range for the given timer type.
 * For TMR_RST:
 *  - The maximum array size is 8 (index range: 0-7).
 * For TMR_INT_16:
 *  - The maximum array size is 8 (index range: 0-7).
 * For TMR_INT_32:
 *  - The maximum array size is 16 (index range: 0-15).
 *
 * If the index exceeds the maximum array size, the function will return
 * the last element of the respective array.
 */
static inline unsigned char atcwdt_get_index(unsigned char index,
					     enum timer_type timer_type)
{
	static const unsigned char rst_timer_interval[TMR_SZ_RST] = {
		7, 8, 9, 10, 11, 12, 13, 14};
	static const unsigned char int_timer_interval[TMR_SZ_INT_32] = {
		6, 8, 10, 11, 12, 13, 14, 15, 17, 19, 21, 23, 25, 27, 29, 31};
	unsigned char array_index;

	if (timer_type == TMR_RST) {
		array_index = min(index, TMR_SZ_RST - 1);
		return rst_timer_interval[array_index];
	}

	if (timer_type == TMR_INT_32)
		array_index = min(index, TMR_SZ_INT_32 - 1);
	else
		array_index = min(index, TMR_SZ_INT_16 - 1);

	return int_timer_interval[array_index];
}

/**
 * atcwdt_get_clock_period - Calculate the closest clock period based on a
 *                           given tick count
 * @tick: The target tick count to match
 * @timer_type: The type of timer, which can be TMR_RST, TMR_INT_16, or
 *              TMR_INT_32.
 * @index: Pointer to store the index of the selected parameter
 *
 * This function calculates the closest clock period to the given tick count
 * by iterating through the timer parameters and selecting the one that
 * minimizes the difference between the target tick count and the calculated
 * clock period. The function determines the index of the closest parameter
 * and returns the difference between the target tick count and the selected
 * clock period.
 *
 * Return: The difference between the target tick count and the selected
 * clock period.
 */
static long long atcwdt_get_clock_period(long long tick,
					 enum timer_type timer_type,
					 unsigned char *index)
{
	long long result;
	unsigned char size;
	char i;

	if (timer_type == TMR_RST)
		size = TMR_SZ_RST;
	else if (timer_type == TMR_INT_32)
		size = TMR_SZ_INT_32;
	else
		size = TMR_SZ_INT_16;

	*index = size - 1;
	for (i = 0; i < size; i++) {
		result = tick - (1LL << atcwdt_get_index(i, timer_type));

		if (result <= 1) {
			*index = i;
			break;
		}
	}

	return result;
}

/**
 * atcwdt_get_timeout_params - Calculate optimal parameters for Watchdog Timer
 * @drv_data: Pointer to the Watchdog driver data structure
 * @timeout: Desired timeout value (in seconds)
 * @int_timer_params: Pointer to store the calculated interrupt timer
 *                    parameter index
 * @rst_timer_params: Pointer to store the calculated reset timer parameter
 *                    index
 *
 * This function calculates the optimal parameter combination for the
 * interrupt timer and reset timer of the Watchdog Timer to achieve a
 * timeout value closest to, but not less than the specified timeout.
 *
 * Algorithm:
 * 1. The parameters for both the interrupt timer and reset timer are
 *    predefined as a series of options represented as powers of 2.
 * 2. The function first determines the interrupt timer's parameter index
 *    that provides a time closest to and not exceeding the desired timeout.
 * 3. Based on the selected interrupt timer, it calculates the required
 *    reset timer parameter to ensure the total timeout matches the target.
 *
 * Return: The calculated parameter indices are stored in the provided
 *         pointers.
 */
static void atcwdt_get_timeout_params(struct atcwdt_drv *drv_data,
				      unsigned int timeout,
				      unsigned char *int_timer_params,
				      unsigned char *rst_timer_params)
{
	long long rest_time_ms;
	long long result;
	long long tick;
	unsigned char rst_index;
	unsigned char int_index;
	unsigned char above;
	unsigned char below;

	tick = (long long)timeout * drv_data->clk_freq;
	result = atcwdt_get_clock_period(tick,
					 drv_data->int_timer_type,
					 &above);
	if (result == 0 || above == 0) {
		*int_timer_params = above;
		*rst_timer_params = 0;
		return;
	}
	below = above - 1;

	int_index = atcwdt_get_index(below, drv_data->int_timer_type);
	rest_time_ms = timeout * 1000LL
		       - div64_s64(1000LL << int_index, drv_data->clk_freq);

	result = atcwdt_get_clock_period(rest_time_ms * drv_data->clk_freq,
					 TMR_RST,
					 &rst_index);

	if (result > 1) {
		*int_timer_params = above;
		*rst_timer_params = 0;
	} else {
		*int_timer_params = below;
		*rst_timer_params = rst_index;
	}
}

/**
 * atcwdt_get_int_timer_type - Get the supported interrupt timer type.
 * @drv_data: Pointer to the watchdog driver data structure.
 *
 * This function tests the writable bits in the IntTime field of the control
 * register to determine the interrupt timer type supported by the hardware.
 *
 * Note: This function must only be called when the ATCWDT200 watchdog is
 * disabled. If the watchdog is enabled, this function returns TMR_UNKNOWN.
 *
 * Returns: The interrupt timer type supported by the hardware.
 */
static int atcwdt_get_int_timer_type(struct atcwdt_drv *drv_data)
{
	struct device *dev = drv_data->wdt_dev.parent;
	unsigned int val;
	int ret  = 0;

	spin_lock(&drv_data->lock);
	regmap_read(drv_data->regmap, REG_CTRL, &val);
	if (val & CTRL_WDT_EN) {
		spin_unlock(&drv_data->lock);
		return TMR_UNKNOWN;
	}

	/*
	 * Configures the IntTime field with the maximum mask value
	 * (CTRL_INT_TIME_MSK), reads its value from the control register
	 * to identify the maximum writable bits.
	 */
	regmap_write(drv_data->regmap, REG_WRITE_EN, WRITE_EN_MAGIC);
	regmap_write(drv_data->regmap, REG_CTRL, CTRL_INT_TIME_MSK);
	regmap_read(drv_data->regmap, REG_CTRL, &val);
	spin_unlock(&drv_data->lock);

	val = CTRL_INT_TIME_GET(val);
	switch (val) {
	case 7:
		drv_data->int_timer_type = TMR_INT_16;
		break;
	case 15:
		drv_data->int_timer_type = TMR_INT_32;
		break;
	default:
		drv_data->int_timer_type = TMR_UNKNOWN;
		ret = dev_err_probe(dev, -ENODEV,
				    "Failed to detect interrupt timer type\n");
	}

	return ret;
}

static int atcwdt_ping(struct watchdog_device *wdt_dev)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WRITE_EN, WRITE_EN_MAGIC);
	regmap_write(drv_data->regmap, REG_RESTART, RESTART_MAGIC);
	regmap_update_bits(drv_data->regmap, REG_STATUS, STATUS_INT_EXPIRED,
			   STATUS_INT_EXPIRED);
	spin_unlock(&drv_data->lock);

	return 0;
}

static int atcwdt_set_timeout(struct watchdog_device *wdt_dev,
			      unsigned int timeout)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);
	unsigned int value;
	unsigned char  rst_val;
	unsigned char  int_val;

	wdt_dev->timeout = timeout;
	atcwdt_get_timeout_params(drv_data, timeout, &int_val, &rst_val);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WRITE_EN, WRITE_EN_MAGIC);

	value = CTRL_RST_TIME_SET(rst_val) |
		CTRL_INT_TIME_SET(int_val) |
		CTRL_CLK_SEL_SET(drv_data->clk_src);
	regmap_update_bits(drv_data->regmap,
			   REG_CTRL,
			   CTRL_RST_TIME_MSK |
			   CTRL_INT_TIME_MSK |
			   CTRL_CLK_SEL,
			   value);

	spin_unlock(&drv_data->lock);
	atcwdt_ping(wdt_dev);

	return 0;
}

static int atcwdt_start(struct watchdog_device *wdt_dev)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	atcwdt_set_timeout(wdt_dev, wdt_dev->timeout);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WRITE_EN, WRITE_EN_MAGIC);
	regmap_update_bits(drv_data->regmap,
			   REG_CTRL,
			   CTRL_RST_EN | CTRL_WDT_EN,
			   CTRL_RST_EN | CTRL_WDT_EN);

	spin_unlock(&drv_data->lock);

	return 0;
}

static int atcwdt_stop(struct watchdog_device *wdt_dev)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WRITE_EN, WRITE_EN_MAGIC);
	regmap_update_bits(drv_data->regmap,
			   REG_CTRL,
			   CTRL_RST_EN | CTRL_WDT_EN,
			   0);
	spin_unlock(&drv_data->lock);

	return 0;
}

static int atcwdt_restart(struct watchdog_device *wdt_dev,
			  unsigned long action, void *data)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	atcwdt_set_timeout(wdt_dev, 0);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WRITE_EN, WRITE_EN_MAGIC);
	regmap_update_bits(drv_data->regmap,
			   REG_CTRL,
			   CTRL_RST_EN | CTRL_WDT_EN,
			   CTRL_RST_EN | CTRL_WDT_EN);
	spin_unlock(&drv_data->lock);

	return 0;
}

static const struct watchdog_ops atcwdt_ops = {
	.owner = THIS_MODULE,
	.start = atcwdt_start,
	.stop = atcwdt_stop,
	.ping = atcwdt_ping,
	.set_timeout = atcwdt_set_timeout,
	.restart = atcwdt_restart,
};

static int atcwdt_init_resource(struct platform_device *pdev,
				struct atcwdt_drv *drv_data)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	const struct regmap_config cfg = {
		.name = "atcwdt",
		.reg_bits = 32,
		.val_bits = 32,
		.cache_type = REGCACHE_NONE,
		.reg_stride = 4,
		.max_register = REG_STATUS,
	};

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "Failed to ioremap I/O resource\n");

	drv_data->regmap = devm_regmap_init_mmio(dev, base, &cfg);
	if (IS_ERR(drv_data->regmap))
		return dev_err_probe(dev, PTR_ERR(drv_data->regmap),
				     "Failed to create regmap\n");

	return 0;
}

static int atcwdt_enable_clk(struct atcwdt_drv *drv_data)
{
	struct device *dev = drv_data->wdt_dev.parent;
	unsigned int val;
	int clk_src;

	drv_data->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(drv_data->clk))
		return dev_err_probe(dev, PTR_ERR(drv_data->clk),
				     "Failed to get watchdog clock\n");

	drv_data->clk_freq = clk_get_rate(drv_data->clk);
	if (!drv_data->clk_freq)
		return dev_err_probe(dev, -EINVAL,
				     "Failed to get clock rate\n");

	clk_src = device_property_read_u32(dev, "andestech,clock-source", &val);
	drv_data->clk_src = (!clk_src && val != 0) ? CTRL_CLK_SEL_PCLK : 0;

	return 0;
}

static int atcwdt_init_wdt_device(struct device *dev,
				  struct atcwdt_drv *drv_data)
{
	struct watchdog_device *wdd = &drv_data->wdt_dev;

	wdd->parent = dev;
	wdd->info = &atcwdt_info;
	wdd->ops = &atcwdt_ops;
	wdd->timeout = ATCWDT_TIMEOUT;
	wdd->min_timeout = 1;

	watchdog_set_nowayout(wdd, nowayout);
	watchdog_set_drvdata(wdd, drv_data);

	return 0;
}

static void atcwdt_calc_max_timeout(struct atcwdt_drv *drv_data)
{
	unsigned char rst_idx = atcwdt_get_index(0xFF, TMR_RST);
	unsigned char int_idx = atcwdt_get_index(0xFF,
						 drv_data->int_timer_type);

	drv_data->wdt_dev.max_timeout =
		((1U << rst_idx) + (1U << int_idx)) / drv_data->clk_freq;
}

static int atcwdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct atcwdt_drv *drv_data;
	int ret;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, drv_data);
	spin_lock_init(&drv_data->lock);

	ret = atcwdt_init_wdt_device(dev, drv_data);
	if (ret)
		return ret;

	ret = atcwdt_init_resource(pdev, drv_data);
	if (ret)
		return ret;

	ret = atcwdt_enable_clk(drv_data);
	if (ret)
		return ret;

	ret = atcwdt_get_int_timer_type(drv_data);
	if (ret)
		return ret;

	atcwdt_calc_max_timeout(drv_data);

	ret = devm_watchdog_register_device(dev, &drv_data->wdt_dev);

	return ret;
}

static int atcwdt_suspend(struct device *dev)
{
	struct atcwdt_drv *drv_data = dev_get_drvdata(dev);

	if (watchdog_active(&drv_data->wdt_dev)) {
		atcwdt_stop(&drv_data->wdt_dev);
		clk_disable_unprepare(drv_data->clk);
	}

	return 0;
}

static int atcwdt_resume(struct device *dev)
{
	struct atcwdt_drv *drv_data = dev_get_drvdata(dev);
	int ret = 0;

	if (watchdog_active(&drv_data->wdt_dev)) {
		ret = clk_prepare_enable(drv_data->clk);
		if (ret)
			return ret;
		atcwdt_start(&drv_data->wdt_dev);
		atcwdt_ping(&drv_data->wdt_dev);
	}

	return ret;
}

static const struct of_device_id atcwdt_match[] = {
	{ .compatible = "andestech,ae350-wdt" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, atcwdt_match);

static DEFINE_SIMPLE_DEV_PM_OPS(atcwdt_pm_ops, atcwdt_suspend, atcwdt_resume);

static struct platform_driver atcwdt_driver = {
	.probe = atcwdt_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = atcwdt_match,
		.pm = pm_sleep_ptr(&atcwdt_pm_ops),
	},
};

module_platform_driver(atcwdt_driver);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default="
		 __MODULE_STRING(ATCWDT_TIMEOUT) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CL Wang <cl634@andestech.com>");
MODULE_DESCRIPTION("Andes ATCWDT200 Watchdog timer driver");
