// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2021 Samin Guo <samin.guo@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/*
 * TempSensor reset. The RSTN can be de-asserted once the analog core has
 * powered up. Trst(min 100ns)
 * 0:reset  1:de-assert
 */
#define SFCTEMP_RSTN	BIT(0)

/*
 * TempSensor analog core power down. The analog core will be powered up
 * Tpu(min 50us) after PD is de-asserted. RSTN should be held low until the
 * analog core is powered up.
 * 0:power up  1:power down
 */
#define SFCTEMP_PD	BIT(1)

/*
 * TempSensor start conversion enable.
 * 0:disable  1:enable
 */
#define SFCTEMP_RUN	BIT(2)

/*
 * TempSensor conversion value output.
 * Temp(C)=DOUT*Y/4094 - K
 */
#define SFCTEMP_DOUT_POS	16
#define SFCTEMP_DOUT_MSK	GENMASK(27, 16)

/* DOUT to Celcius conversion constants */
#define SFCTEMP_Y1000	237500L
#define SFCTEMP_Z	4094L
#define SFCTEMP_K1000	81100L

struct sfctemp {
	/* serialize access to hardware register and enabled below */
	struct mutex lock;
	void __iomem *regs;
	struct clk *clk_sense;
	struct clk *clk_bus;
	struct reset_control *rst_sense;
	struct reset_control *rst_bus;
	bool enabled;
};

static void sfctemp_power_up(struct sfctemp *sfctemp)
{
	/* make sure we're powered down first */
	writel(SFCTEMP_PD, sfctemp->regs);
	udelay(1);

	writel(0, sfctemp->regs);
	/* wait t_pu(50us) + t_rst(100ns) */
	usleep_range(60, 200);

	/* de-assert reset */
	writel(SFCTEMP_RSTN, sfctemp->regs);
	udelay(1); /* wait t_su(500ps) */
}

static void sfctemp_power_down(struct sfctemp *sfctemp)
{
	writel(SFCTEMP_PD, sfctemp->regs);
}

static void sfctemp_run(struct sfctemp *sfctemp)
{
	writel(SFCTEMP_RSTN | SFCTEMP_RUN, sfctemp->regs);
	udelay(1);
}

static void sfctemp_stop(struct sfctemp *sfctemp)
{
	writel(SFCTEMP_RSTN, sfctemp->regs);
}

static int sfctemp_enable(struct sfctemp *sfctemp)
{
	int ret = 0;

	mutex_lock(&sfctemp->lock);
	if (sfctemp->enabled)
		goto done;

	ret = clk_prepare_enable(sfctemp->clk_bus);
	if (ret)
		goto err;
	ret = reset_control_deassert(sfctemp->rst_bus);
	if (ret)
		goto err_disable_bus;

	ret = clk_prepare_enable(sfctemp->clk_sense);
	if (ret)
		goto err_assert_bus;
	ret = reset_control_deassert(sfctemp->rst_sense);
	if (ret)
		goto err_disable_sense;

	sfctemp_power_up(sfctemp);
	sfctemp_run(sfctemp);
	sfctemp->enabled = true;
done:
	mutex_unlock(&sfctemp->lock);
	return ret;

err_disable_sense:
	clk_disable_unprepare(sfctemp->clk_sense);
err_assert_bus:
	reset_control_assert(sfctemp->rst_bus);
err_disable_bus:
	clk_disable_unprepare(sfctemp->clk_bus);
err:
	mutex_unlock(&sfctemp->lock);
	return ret;
}

static int sfctemp_disable(struct sfctemp *sfctemp)
{
	mutex_lock(&sfctemp->lock);
	if (!sfctemp->enabled)
		goto done;

	sfctemp_stop(sfctemp);
	sfctemp_power_down(sfctemp);
	reset_control_assert(sfctemp->rst_sense);
	clk_disable_unprepare(sfctemp->clk_sense);
	reset_control_assert(sfctemp->rst_bus);
	clk_disable_unprepare(sfctemp->clk_bus);
	sfctemp->enabled = false;
done:
	mutex_unlock(&sfctemp->lock);
	return 0;
}

static void sfctemp_disable_action(void *data)
{
	sfctemp_disable(data);
}

static int sfctemp_convert(struct sfctemp *sfctemp, long *val)
{
	int ret;

	mutex_lock(&sfctemp->lock);
	if (!sfctemp->enabled) {
		ret = -ENODATA;
		goto out;
	}

	/* calculate temperature in milli Celcius */
	*val = (long)((readl(sfctemp->regs) & SFCTEMP_DOUT_MSK) >> SFCTEMP_DOUT_POS)
		* SFCTEMP_Y1000 / SFCTEMP_Z - SFCTEMP_K1000;

	ret = 0;
out:
	mutex_unlock(&sfctemp->lock);
	return ret;
}

static umode_t sfctemp_is_visible(const void *data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			return 0644;
		case hwmon_temp_input:
			return 0444;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static int sfctemp_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct sfctemp *sfctemp = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			*val = sfctemp->enabled;
			return 0;
		case hwmon_temp_input:
			return sfctemp_convert(sfctemp, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int sfctemp_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct sfctemp *sfctemp = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			if (val == 0)
				return sfctemp_disable(sfctemp);
			if (val == 1)
				return sfctemp_enable(sfctemp);
			return -EINVAL;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct hwmon_channel_info *sfctemp_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_ENABLE | HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops sfctemp_hwmon_ops = {
	.is_visible = sfctemp_is_visible,
	.read = sfctemp_read,
	.write = sfctemp_write,
};

static const struct hwmon_chip_info sfctemp_chip_info = {
	.ops = &sfctemp_hwmon_ops,
	.info = sfctemp_info,
};

static int sfctemp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	struct sfctemp *sfctemp;
	int ret;

	sfctemp = devm_kzalloc(dev, sizeof(*sfctemp), GFP_KERNEL);
	if (!sfctemp)
		return -ENOMEM;

	dev_set_drvdata(dev, sfctemp);
	mutex_init(&sfctemp->lock);

	sfctemp->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sfctemp->regs))
		return PTR_ERR(sfctemp->regs);

	sfctemp->clk_sense = devm_clk_get(dev, "sense");
	if (IS_ERR(sfctemp->clk_sense))
		return dev_err_probe(dev, PTR_ERR(sfctemp->clk_sense),
				     "error getting sense clock\n");

	sfctemp->clk_bus = devm_clk_get(dev, "bus");
	if (IS_ERR(sfctemp->clk_bus))
		return dev_err_probe(dev, PTR_ERR(sfctemp->clk_bus),
				     "error getting bus clock\n");

	sfctemp->rst_sense = devm_reset_control_get_exclusive(dev, "sense");
	if (IS_ERR(sfctemp->rst_sense))
		return dev_err_probe(dev, PTR_ERR(sfctemp->rst_sense),
				     "error getting sense reset\n");

	sfctemp->rst_bus = devm_reset_control_get_exclusive(dev, "bus");
	if (IS_ERR(sfctemp->rst_bus))
		return dev_err_probe(dev, PTR_ERR(sfctemp->rst_bus),
				     "error getting busreset\n");

	ret = reset_control_assert(sfctemp->rst_sense);
	if (ret)
		return dev_err_probe(dev, ret, "error asserting sense reset\n");

	ret = reset_control_assert(sfctemp->rst_bus);
	if (ret)
		return dev_err_probe(dev, ret, "error asserting bus reset\n");

	ret = devm_add_action(dev, sfctemp_disable_action, sfctemp);
	if (ret)
		return ret;

	ret = sfctemp_enable(sfctemp);
	if (ret)
		return dev_err_probe(dev, ret, "error enabling temperature sensor\n");

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "sfctemp", sfctemp,
							 &sfctemp_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id sfctemp_of_match[] = {
	{ .compatible = "starfive,jh7100-temp" },
	{ .compatible = "starfive,jh7110-temp" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sfctemp_of_match);

static struct platform_driver sfctemp_driver = {
	.probe  = sfctemp_probe,
	.driver = {
		.name = "sfctemp",
		.of_match_table = sfctemp_of_match,
	},
};
module_platform_driver(sfctemp_driver);

MODULE_AUTHOR("Emil Renner Berthing");
MODULE_DESCRIPTION("StarFive JH71x0 temperature sensor driver");
MODULE_LICENSE("GPL");
