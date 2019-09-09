// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core MFD support for Cirrus Logic Madera codecs
 *
 * Copyright (C) 2015-2018 Cirrus Logic
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/registers.h>

#include "madera.h"

#define CS47L15_SILICON_ID	0x6370
#define CS47L35_SILICON_ID	0x6360
#define CS47L85_SILICON_ID	0x6338
#define CS47L90_SILICON_ID	0x6364
#define CS47L92_SILICON_ID	0x6371

#define MADERA_32KZ_MCLK2	1

static const char * const madera_core_supplies[] = {
	"AVDD",
	"DBVDD1",
};

static const struct mfd_cell madera_ldo1_devs[] = {
	{ .name = "madera-ldo1" },
};

static const char * const cs47l15_supplies[] = {
	"MICVDD",
	"CPVDD1",
	"SPKVDD",
};

static const struct mfd_cell cs47l15_devs[] = {
	{ .name = "madera-pinctrl", },
	{ .name = "madera-irq" },
	{ .name = "madera-gpio" },
	{
		.name = "madera-extcon",
		.parent_supplies = cs47l15_supplies,
		.num_parent_supplies = 1, /* We only need MICVDD */
	},
	{
		.name = "cs47l15-codec",
		.parent_supplies = cs47l15_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs47l15_supplies),
	},
};

static const char * const cs47l35_supplies[] = {
	"MICVDD",
	"DBVDD2",
	"CPVDD1",
	"CPVDD2",
	"SPKVDD",
};

static const struct mfd_cell cs47l35_devs[] = {
	{ .name = "madera-pinctrl", },
	{ .name = "madera-irq", },
	{ .name = "madera-micsupp", },
	{ .name = "madera-gpio", },
	{
		.name = "madera-extcon",
		.parent_supplies = cs47l35_supplies,
		.num_parent_supplies = 1, /* We only need MICVDD */
	},
	{
		.name = "cs47l35-codec",
		.parent_supplies = cs47l35_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs47l35_supplies),
	},
};

static const char * const cs47l85_supplies[] = {
	"MICVDD",
	"DBVDD2",
	"DBVDD3",
	"DBVDD4",
	"CPVDD1",
	"CPVDD2",
	"SPKVDDL",
	"SPKVDDR",
};

static const struct mfd_cell cs47l85_devs[] = {
	{ .name = "madera-pinctrl", },
	{ .name = "madera-irq", },
	{ .name = "madera-micsupp" },
	{ .name = "madera-gpio", },
	{
		.name = "madera-extcon",
		.parent_supplies = cs47l85_supplies,
		.num_parent_supplies = 1, /* We only need MICVDD */
	},
	{
		.name = "cs47l85-codec",
		.parent_supplies = cs47l85_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs47l85_supplies),
	},
};

static const char * const cs47l90_supplies[] = {
	"MICVDD",
	"DBVDD2",
	"DBVDD3",
	"DBVDD4",
	"CPVDD1",
	"CPVDD2",
};

static const struct mfd_cell cs47l90_devs[] = {
	{ .name = "madera-pinctrl", },
	{ .name = "madera-irq", },
	{ .name = "madera-micsupp", },
	{ .name = "madera-gpio", },
	{
		.name = "madera-extcon",
		.parent_supplies = cs47l90_supplies,
		.num_parent_supplies = 1, /* We only need MICVDD */
	},
	{
		.name = "cs47l90-codec",
		.parent_supplies = cs47l90_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs47l90_supplies),
	},
};

static const char * const cs47l92_supplies[] = {
	"MICVDD",
	"CPVDD1",
	"CPVDD2",
};

static const struct mfd_cell cs47l92_devs[] = {
	{ .name = "madera-pinctrl" },
	{ .name = "madera-irq", },
	{ .name = "madera-micsupp", },
	{ .name = "madera-gpio" },
	{
		.name = "madera-extcon",
		.parent_supplies = cs47l92_supplies,
		.num_parent_supplies = 1, /* We only need MICVDD */
	},
	{
		.name = "cs47l92-codec",
		.parent_supplies = cs47l92_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs47l92_supplies),
	},
};

/* Used by madera-i2c and madera-spi drivers */
const char *madera_name_from_type(enum madera_type type)
{
	switch (type) {
	case CS47L15:
		return "CS47L15";
	case CS47L35:
		return "CS47L35";
	case CS47L85:
		return "CS47L85";
	case CS47L90:
		return "CS47L90";
	case CS47L91:
		return "CS47L91";
	case CS42L92:
		return "CS42L92";
	case CS47L92:
		return "CS47L92";
	case CS47L93:
		return "CS47L93";
	case WM1840:
		return "WM1840";
	default:
		return "Unknown";
	}
}
EXPORT_SYMBOL_GPL(madera_name_from_type);

#define MADERA_BOOT_POLL_INTERVAL_USEC		5000
#define MADERA_BOOT_POLL_TIMEOUT_USEC		25000

static int madera_wait_for_boot(struct madera *madera)
{
	ktime_t timeout;
	unsigned int val = 0;
	int ret = 0;

	/*
	 * We can't use an interrupt as we need to runtime resume to do so,
	 * so we poll the status bit. This won't race with the interrupt
	 * handler because it will be blocked on runtime resume.
	 * The chip could NAK a read request while it is booting so ignore
	 * errors from regmap_read.
	 */
	timeout = ktime_add_us(ktime_get(), MADERA_BOOT_POLL_TIMEOUT_USEC);
	regmap_read(madera->regmap, MADERA_IRQ1_RAW_STATUS_1, &val);
	while (!(val & MADERA_BOOT_DONE_STS1) &&
	       !ktime_after(ktime_get(), timeout)) {
		usleep_range(MADERA_BOOT_POLL_INTERVAL_USEC / 2,
			     MADERA_BOOT_POLL_INTERVAL_USEC);
		regmap_read(madera->regmap, MADERA_IRQ1_RAW_STATUS_1, &val);
	}

	if (!(val & MADERA_BOOT_DONE_STS1)) {
		dev_err(madera->dev, "Polling BOOT_DONE_STS timed out\n");
		ret = -ETIMEDOUT;
	}

	/*
	 * BOOT_DONE defaults to unmasked on boot so we must ack it.
	 * Do this even after a timeout to avoid interrupt storms.
	 */
	regmap_write(madera->regmap, MADERA_IRQ1_STATUS_1,
		     MADERA_BOOT_DONE_EINT1);

	pm_runtime_mark_last_busy(madera->dev);

	return ret;
}

static int madera_soft_reset(struct madera *madera)
{
	int ret;

	ret = regmap_write(madera->regmap, MADERA_SOFTWARE_RESET, 0);
	if (ret != 0) {
		dev_err(madera->dev, "Failed to soft reset device: %d\n", ret);
		return ret;
	}

	/* Allow time for internal clocks to startup after reset */
	usleep_range(1000, 2000);

	return 0;
}

static void madera_enable_hard_reset(struct madera *madera)
{
	if (!madera->pdata.reset)
		return;

	/*
	 * There are many existing out-of-tree users of these codecs that we
	 * can't break so preserve the expected behaviour of setting the line
	 * low to assert reset.
	 */
	gpiod_set_raw_value_cansleep(madera->pdata.reset, 0);
}

static void madera_disable_hard_reset(struct madera *madera)
{
	if (!madera->pdata.reset)
		return;

	gpiod_set_raw_value_cansleep(madera->pdata.reset, 1);
	usleep_range(1000, 2000);
}

static int __maybe_unused madera_runtime_resume(struct device *dev)
{
	struct madera *madera = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "Leaving sleep mode\n");

	ret = regulator_enable(madera->dcvdd);
	if (ret) {
		dev_err(dev, "Failed to enable DCVDD: %d\n", ret);
		return ret;
	}

	regcache_cache_only(madera->regmap, false);
	regcache_cache_only(madera->regmap_32bit, false);

	ret = madera_wait_for_boot(madera);
	if (ret)
		goto err;

	ret = regcache_sync(madera->regmap);
	if (ret) {
		dev_err(dev, "Failed to restore 16-bit register cache\n");
		goto err;
	}

	ret = regcache_sync(madera->regmap_32bit);
	if (ret) {
		dev_err(dev, "Failed to restore 32-bit register cache\n");
		goto err;
	}

	return 0;

err:
	regcache_cache_only(madera->regmap_32bit, true);
	regcache_cache_only(madera->regmap, true);
	regulator_disable(madera->dcvdd);

	return ret;
}

static int __maybe_unused madera_runtime_suspend(struct device *dev)
{
	struct madera *madera = dev_get_drvdata(dev);

	dev_dbg(madera->dev, "Entering sleep mode\n");

	regcache_cache_only(madera->regmap, true);
	regcache_mark_dirty(madera->regmap);
	regcache_cache_only(madera->regmap_32bit, true);
	regcache_mark_dirty(madera->regmap_32bit);

	regulator_disable(madera->dcvdd);

	return 0;
}

const struct dev_pm_ops madera_pm_ops = {
	SET_RUNTIME_PM_OPS(madera_runtime_suspend,
			   madera_runtime_resume,
			   NULL)
};
EXPORT_SYMBOL_GPL(madera_pm_ops);

const struct of_device_id madera_of_match[] = {
	{ .compatible = "cirrus,cs47l15", .data = (void *)CS47L15 },
	{ .compatible = "cirrus,cs47l35", .data = (void *)CS47L35 },
	{ .compatible = "cirrus,cs47l85", .data = (void *)CS47L85 },
	{ .compatible = "cirrus,cs47l90", .data = (void *)CS47L90 },
	{ .compatible = "cirrus,cs47l91", .data = (void *)CS47L91 },
	{ .compatible = "cirrus,cs42l92", .data = (void *)CS42L92 },
	{ .compatible = "cirrus,cs47l92", .data = (void *)CS47L92 },
	{ .compatible = "cirrus,cs47l93", .data = (void *)CS47L93 },
	{ .compatible = "cirrus,wm1840", .data = (void *)WM1840 },
	{}
};
MODULE_DEVICE_TABLE(of, madera_of_match);
EXPORT_SYMBOL_GPL(madera_of_match);

static int madera_get_reset_gpio(struct madera *madera)
{
	struct gpio_desc *reset;
	int ret;

	if (madera->pdata.reset)
		return 0;

	reset = devm_gpiod_get_optional(madera->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset)) {
		ret = PTR_ERR(reset);
		if (ret != -EPROBE_DEFER)
			dev_err(madera->dev, "Failed to request /RESET: %d\n",
				ret);
		return ret;
	}

	/*
	 * A hard reset is needed for full reset of the chip. We allow running
	 * without hard reset only because it can be useful for early
	 * prototyping and some debugging, but we need to warn it's not ideal.
	 */
	if (!reset)
		dev_warn(madera->dev,
			 "Running without reset GPIO is not recommended\n");

	madera->pdata.reset = reset;

	return 0;
}

static void madera_set_micbias_info(struct madera *madera)
{
	/*
	 * num_childbias is an array because future codecs can have different
	 * childbiases for each micbias. Unspecified values default to 0.
	 */
	switch (madera->type) {
	case CS47L15:
		madera->num_micbias = 1;
		madera->num_childbias[0] = 3;
		return;
	case CS47L35:
		madera->num_micbias = 2;
		madera->num_childbias[0] = 2;
		madera->num_childbias[1] = 2;
		return;
	case CS47L85:
	case WM1840:
		madera->num_micbias = 4;
		/* no child biases */
		return;
	case CS47L90:
	case CS47L91:
		madera->num_micbias = 2;
		madera->num_childbias[0] = 4;
		madera->num_childbias[1] = 4;
		return;
	case CS42L92:
	case CS47L92:
	case CS47L93:
		madera->num_micbias = 2;
		madera->num_childbias[0] = 4;
		madera->num_childbias[1] = 2;
		return;
	default:
		return;
	}
}

int madera_dev_init(struct madera *madera)
{
	struct device *dev = madera->dev;
	unsigned int hwid;
	int (*patch_fn)(struct madera *) = NULL;
	const struct mfd_cell *mfd_devs;
	int n_devs = 0;
	int i, ret;

	dev_set_drvdata(madera->dev, madera);
	BLOCKING_INIT_NOTIFIER_HEAD(&madera->notifier);
	mutex_init(&madera->dapm_ptr_lock);

	madera_set_micbias_info(madera);

	/*
	 * We need writable hw config info that all children can share.
	 * Simplest to take one shared copy of pdata struct.
	 */
	if (dev_get_platdata(madera->dev)) {
		memcpy(&madera->pdata, dev_get_platdata(madera->dev),
		       sizeof(madera->pdata));
	}

	ret = madera_get_reset_gpio(madera);
	if (ret)
		return ret;

	regcache_cache_only(madera->regmap, true);
	regcache_cache_only(madera->regmap_32bit, true);

	for (i = 0; i < ARRAY_SIZE(madera_core_supplies); i++)
		madera->core_supplies[i].supply = madera_core_supplies[i];

	madera->num_core_supplies = ARRAY_SIZE(madera_core_supplies);

	/*
	 * On some codecs DCVDD could be supplied by the internal LDO1.
	 * For those we must add the LDO1 driver before requesting DCVDD
	 * No devm_ because we need to control shutdown order of children.
	 */
	switch (madera->type) {
	case CS47L15:
	case CS47L35:
	case CS47L90:
	case CS47L91:
	case CS42L92:
	case CS47L92:
	case CS47L93:
		break;
	case CS47L85:
	case WM1840:
		ret = mfd_add_devices(madera->dev, PLATFORM_DEVID_NONE,
				      madera_ldo1_devs,
				      ARRAY_SIZE(madera_ldo1_devs),
				      NULL, 0, NULL);
		if (ret) {
			dev_err(dev, "Failed to add LDO1 child: %d\n", ret);
			return ret;
		}
		break;
	default:
		/* No point continuing if the type is unknown */
		dev_err(madera->dev, "Unknown device type %d\n", madera->type);
		return -ENODEV;
	}

	ret = devm_regulator_bulk_get(dev, madera->num_core_supplies,
				      madera->core_supplies);
	if (ret) {
		dev_err(dev, "Failed to request core supplies: %d\n", ret);
		goto err_devs;
	}

	/*
	 * Don't use devres here. If the regulator is one of our children it
	 * will already have been removed before devres cleanup on this mfd
	 * driver tries to call put() on it. We need control of shutdown order.
	 */
	madera->dcvdd = regulator_get(madera->dev, "DCVDD");
	if (IS_ERR(madera->dcvdd)) {
		ret = PTR_ERR(madera->dcvdd);
		dev_err(dev, "Failed to request DCVDD: %d\n", ret);
		goto err_devs;
	}

	ret = regulator_bulk_enable(madera->num_core_supplies,
				    madera->core_supplies);
	if (ret) {
		dev_err(dev, "Failed to enable core supplies: %d\n", ret);
		goto err_dcvdd;
	}

	ret = regulator_enable(madera->dcvdd);
	if (ret) {
		dev_err(dev, "Failed to enable DCVDD: %d\n", ret);
		goto err_enable;
	}

	madera_disable_hard_reset(madera);

	regcache_cache_only(madera->regmap, false);
	regcache_cache_only(madera->regmap_32bit, false);

	/*
	 * Now we can power up and verify that this is a chip we know about
	 * before we start doing any writes to its registers.
	 */
	ret = regmap_read(madera->regmap, MADERA_SOFTWARE_RESET, &hwid);
	if (ret) {
		dev_err(dev, "Failed to read ID register: %d\n", ret);
		goto err_reset;
	}

	switch (hwid) {
	case CS47L15_SILICON_ID:
		if (IS_ENABLED(CONFIG_MFD_CS47L15)) {
			switch (madera->type) {
			case CS47L15:
				patch_fn = &cs47l15_patch;
				mfd_devs = cs47l15_devs;
				n_devs = ARRAY_SIZE(cs47l15_devs);
				break;
			default:
				break;
			}
		}
		break;
	case CS47L35_SILICON_ID:
		if (IS_ENABLED(CONFIG_MFD_CS47L35)) {
			switch (madera->type) {
			case CS47L35:
				patch_fn = cs47l35_patch;
				mfd_devs = cs47l35_devs;
				n_devs = ARRAY_SIZE(cs47l35_devs);
				break;
			default:
				break;
			}
		}
		break;
	case CS47L85_SILICON_ID:
		if (IS_ENABLED(CONFIG_MFD_CS47L85)) {
			switch (madera->type) {
			case CS47L85:
			case WM1840:
				patch_fn = cs47l85_patch;
				mfd_devs = cs47l85_devs;
				n_devs = ARRAY_SIZE(cs47l85_devs);
				break;
			default:
				break;
			}
		}
		break;
	case CS47L90_SILICON_ID:
		if (IS_ENABLED(CONFIG_MFD_CS47L90)) {
			switch (madera->type) {
			case CS47L90:
			case CS47L91:
				patch_fn = cs47l90_patch;
				mfd_devs = cs47l90_devs;
				n_devs = ARRAY_SIZE(cs47l90_devs);
				break;
			default:
				break;
			}
		}
		break;
	case CS47L92_SILICON_ID:
		if (IS_ENABLED(CONFIG_MFD_CS47L92)) {
			switch (madera->type) {
			case CS42L92:
			case CS47L92:
			case CS47L93:
				patch_fn = cs47l92_patch;
				mfd_devs = cs47l92_devs;
				n_devs = ARRAY_SIZE(cs47l92_devs);
				break;
			default:
				break;
			}
		}
		break;
	default:
		dev_err(madera->dev, "Unknown device ID: %x\n", hwid);
		ret = -EINVAL;
		goto err_reset;
	}

	if (!n_devs) {
		dev_err(madera->dev, "Device ID 0x%x not a %s\n", hwid,
			madera->type_name);
		ret = -ENODEV;
		goto err_reset;
	}

	/*
	 * It looks like a device we support. If we don't have a hard reset
	 * we can now attempt a soft reset.
	 */
	if (!madera->pdata.reset) {
		ret = madera_soft_reset(madera);
		if (ret)
			goto err_reset;
	}

	ret = madera_wait_for_boot(madera);
	if (ret) {
		dev_err(madera->dev, "Device failed initial boot: %d\n", ret);
		goto err_reset;
	}

	ret = regmap_read(madera->regmap, MADERA_HARDWARE_REVISION,
			  &madera->rev);
	if (ret) {
		dev_err(dev, "Failed to read revision register: %d\n", ret);
		goto err_reset;
	}
	madera->rev &= MADERA_HW_REVISION_MASK;

	dev_info(dev, "%s silicon revision %d\n", madera->type_name,
		 madera->rev);

	/* Apply hardware patch */
	if (patch_fn) {
		ret = patch_fn(madera);
		if (ret) {
			dev_err(madera->dev, "Failed to apply patch %d\n", ret);
			goto err_reset;
		}
	}

	/* Init 32k clock sourced from MCLK2 */
	ret = regmap_update_bits(madera->regmap,
			MADERA_CLOCK_32K_1,
			MADERA_CLK_32K_ENA_MASK | MADERA_CLK_32K_SRC_MASK,
			MADERA_CLK_32K_ENA | MADERA_32KZ_MCLK2);
	if (ret) {
		dev_err(madera->dev, "Failed to init 32k clock: %d\n", ret);
		goto err_reset;
	}

	pm_runtime_set_active(madera->dev);
	pm_runtime_enable(madera->dev);
	pm_runtime_set_autosuspend_delay(madera->dev, 100);
	pm_runtime_use_autosuspend(madera->dev);

	/* No devm_ because we need to control shutdown order of children */
	ret = mfd_add_devices(madera->dev, PLATFORM_DEVID_NONE,
			      mfd_devs, n_devs,
			      NULL, 0, NULL);
	if (ret) {
		dev_err(madera->dev, "Failed to add subdevices: %d\n", ret);
		goto err_pm_runtime;
	}

	return 0;

err_pm_runtime:
	pm_runtime_disable(madera->dev);
err_reset:
	madera_enable_hard_reset(madera);
	regulator_disable(madera->dcvdd);
err_enable:
	regulator_bulk_disable(madera->num_core_supplies,
			       madera->core_supplies);
err_dcvdd:
	regulator_put(madera->dcvdd);
err_devs:
	mfd_remove_devices(dev);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_dev_init);

int madera_dev_exit(struct madera *madera)
{
	/* Prevent any IRQs being serviced while we clean up */
	disable_irq(madera->irq);

	/*
	 * DCVDD could be supplied by a child node, we must disable it before
	 * removing the children, and prevent PM runtime from turning it back on
	 */
	pm_runtime_disable(madera->dev);

	regulator_disable(madera->dcvdd);
	regulator_put(madera->dcvdd);

	mfd_remove_devices(madera->dev);
	madera_enable_hard_reset(madera);

	regulator_bulk_disable(madera->num_core_supplies,
			       madera->core_supplies);
	return 0;
}
EXPORT_SYMBOL_GPL(madera_dev_exit);

MODULE_DESCRIPTION("Madera core MFD driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
