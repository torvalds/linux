// SPDX-License-Identifier: GPL-2.0
/*
 * Arche Platform driver to control APB.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include "arche_platform.h"

static void apb_bootret_deassert(struct device *dev);

struct arche_apb_ctrl_drvdata {
	/* Control GPIO signals to and from AP <=> AP Bridges */
	struct gpio_desc *resetn;
	struct gpio_desc *boot_ret;
	struct gpio_desc *pwroff;
	struct gpio_desc *wake_in;
	struct gpio_desc *wake_out;
	struct gpio_desc *pwrdn;

	enum arche_platform_state state;
	bool init_disabled;

	struct regulator *vcore;
	struct regulator *vio;

	struct gpio_desc *clk_en;
	struct clk *clk;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;

	/* V2: SPI Bus control  */
	struct gpio_desc *spi_en;
	bool spi_en_polarity_high;
};

/*
 * Note that these low level api's are active high
 */
static inline void deassert_reset(struct gpio_desc *gpio)
{
	gpiod_set_raw_value(gpio, 1);
}

static inline void assert_reset(struct gpio_desc *gpio)
{
	gpiod_set_raw_value(gpio, 0);
}

/*
 * Note: Please do not modify the below sequence, as it is as per the spec
 */
static int coldboot_seq(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);
	int ret;

	if (apb->init_disabled ||
	    apb->state == ARCHE_PLATFORM_STATE_ACTIVE)
		return 0;

	/* Hold APB in reset state */
	assert_reset(apb->resetn);

	if (apb->state == ARCHE_PLATFORM_STATE_FW_FLASHING && apb->spi_en)
		devm_gpiod_put(dev, apb->spi_en);

	/* Enable power to APB */
	if (!IS_ERR(apb->vcore)) {
		ret = regulator_enable(apb->vcore);
		if (ret) {
			dev_err(dev, "failed to enable core regulator\n");
			return ret;
		}
	}

	if (!IS_ERR(apb->vio)) {
		ret = regulator_enable(apb->vio);
		if (ret) {
			dev_err(dev, "failed to enable IO regulator\n");
			return ret;
		}
	}

	apb_bootret_deassert(dev);

	/* On DB3 clock was not mandatory */
	if (apb->clk_en)
		gpiod_set_value(apb->clk_en, 1);

	usleep_range(100, 200);

	/* deassert reset to APB : Active-low signal */
	deassert_reset(apb->resetn);

	apb->state = ARCHE_PLATFORM_STATE_ACTIVE;

	return 0;
}

static int fw_flashing_seq(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);
	int ret;

	if (apb->init_disabled ||
	    apb->state == ARCHE_PLATFORM_STATE_FW_FLASHING)
		return 0;

	ret = regulator_enable(apb->vcore);
	if (ret) {
		dev_err(dev, "failed to enable core regulator\n");
		return ret;
	}

	ret = regulator_enable(apb->vio);
	if (ret) {
		dev_err(dev, "failed to enable IO regulator\n");
		return ret;
	}

	if (apb->spi_en) {
		unsigned long flags;

		if (apb->spi_en_polarity_high)
			flags = GPIOD_OUT_HIGH;
		else
			flags = GPIOD_OUT_LOW;

		apb->spi_en = devm_gpiod_get(dev, "spi-en", flags);
		if (IS_ERR(apb->spi_en)) {
			ret = PTR_ERR(apb->spi_en);
			dev_err(dev, "Failed requesting SPI bus en GPIO: %d\n",
				ret);
			return ret;
		}
	}

	/* for flashing device should be in reset state */
	assert_reset(apb->resetn);
	apb->state = ARCHE_PLATFORM_STATE_FW_FLASHING;

	return 0;
}

static int standby_boot_seq(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);

	if (apb->init_disabled)
		return 0;

	/*
	 * Even if it is in OFF state,
	 * then we do not want to change the state
	 */
	if (apb->state == ARCHE_PLATFORM_STATE_STANDBY ||
	    apb->state == ARCHE_PLATFORM_STATE_OFF)
		return 0;

	if (apb->state == ARCHE_PLATFORM_STATE_FW_FLASHING && apb->spi_en)
		devm_gpiod_put(dev, apb->spi_en);

	/*
	 * As per WDM spec, do nothing
	 *
	 * Pasted from WDM spec,
	 *  - A falling edge on POWEROFF_L is detected (a)
	 *  - WDM enters standby mode, but no output signals are changed
	 */

	/* TODO: POWEROFF_L is input to WDM module  */
	apb->state = ARCHE_PLATFORM_STATE_STANDBY;
	return 0;
}

static void poweroff_seq(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);

	if (apb->init_disabled || apb->state == ARCHE_PLATFORM_STATE_OFF)
		return;

	if (apb->state == ARCHE_PLATFORM_STATE_FW_FLASHING && apb->spi_en)
		devm_gpiod_put(dev, apb->spi_en);

	/* disable the clock */
	if (apb->clk_en)
		gpiod_set_value(apb->clk_en, 0);

	if (!IS_ERR(apb->vcore) && regulator_is_enabled(apb->vcore) > 0)
		regulator_disable(apb->vcore);

	if (!IS_ERR(apb->vio) && regulator_is_enabled(apb->vio) > 0)
		regulator_disable(apb->vio);

	/* As part of exit, put APB back in reset state */
	assert_reset(apb->resetn);
	apb->state = ARCHE_PLATFORM_STATE_OFF;

	/* TODO: May have to send an event to SVC about this exit */
}

static void apb_bootret_deassert(struct device *dev)
{
	struct arche_apb_ctrl_drvdata *apb = dev_get_drvdata(dev);

	gpiod_set_value(apb->boot_ret, 0);
}

int apb_ctrl_coldboot(struct device *dev)
{
	return coldboot_seq(to_platform_device(dev));
}

int apb_ctrl_fw_flashing(struct device *dev)
{
	return fw_flashing_seq(to_platform_device(dev));
}

int apb_ctrl_standby_boot(struct device *dev)
{
	return standby_boot_seq(to_platform_device(dev));
}

void apb_ctrl_poweroff(struct device *dev)
{
	poweroff_seq(to_platform_device(dev));
}

static ssize_t state_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);
	int ret = 0;
	bool is_disabled;

	if (sysfs_streq(buf, "off")) {
		if (apb->state == ARCHE_PLATFORM_STATE_OFF)
			return count;

		poweroff_seq(pdev);
	} else if (sysfs_streq(buf, "active")) {
		if (apb->state == ARCHE_PLATFORM_STATE_ACTIVE)
			return count;

		poweroff_seq(pdev);
		is_disabled = apb->init_disabled;
		apb->init_disabled = false;
		ret = coldboot_seq(pdev);
		if (ret)
			apb->init_disabled = is_disabled;
	} else if (sysfs_streq(buf, "standby")) {
		if (apb->state == ARCHE_PLATFORM_STATE_STANDBY)
			return count;

		ret = standby_boot_seq(pdev);
	} else if (sysfs_streq(buf, "fw_flashing")) {
		if (apb->state == ARCHE_PLATFORM_STATE_FW_FLASHING)
			return count;

		/*
		 * First we want to make sure we power off everything
		 * and then enter FW flashing state
		 */
		poweroff_seq(pdev);
		ret = fw_flashing_seq(pdev);
	} else {
		dev_err(dev, "unknown state\n");
		ret = -EINVAL;
	}

	return ret ? ret : count;
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct arche_apb_ctrl_drvdata *apb = dev_get_drvdata(dev);

	switch (apb->state) {
	case ARCHE_PLATFORM_STATE_OFF:
		return sprintf(buf, "off%s\n",
				apb->init_disabled ? ",disabled" : "");
	case ARCHE_PLATFORM_STATE_ACTIVE:
		return sprintf(buf, "active\n");
	case ARCHE_PLATFORM_STATE_STANDBY:
		return sprintf(buf, "standby\n");
	case ARCHE_PLATFORM_STATE_FW_FLASHING:
		return sprintf(buf, "fw_flashing\n");
	default:
		return sprintf(buf, "unknown state\n");
	}
}

static DEVICE_ATTR_RW(state);

static int apb_ctrl_get_devtree_data(struct platform_device *pdev,
				     struct arche_apb_ctrl_drvdata *apb)
{
	struct device *dev = &pdev->dev;
	int ret;

	apb->resetn = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(apb->resetn)) {
		ret = PTR_ERR(apb->resetn);
		dev_err(dev, "Failed requesting reset GPIO: %d\n", ret);
		return ret;
	}

	apb->boot_ret = devm_gpiod_get(dev, "boot-ret", GPIOD_OUT_LOW);
	if (IS_ERR(apb->boot_ret)) {
		ret = PTR_ERR(apb->boot_ret);
		dev_err(dev, "Failed requesting bootret GPIO: %d\n", ret);
		return ret;
	}

	/* It's not mandatory to support power management interface */
	apb->pwroff = devm_gpiod_get_optional(dev, "pwr-off", GPIOD_IN);
	if (IS_ERR(apb->pwroff)) {
		ret = PTR_ERR(apb->pwroff);
		dev_err(dev, "Failed requesting pwroff_n GPIO: %d\n", ret);
		return ret;
	}

	/* Do not make clock mandatory as of now (for DB3) */
	apb->clk_en = devm_gpiod_get_optional(dev, "clock-en", GPIOD_OUT_LOW);
	if (IS_ERR(apb->clk_en)) {
		ret = PTR_ERR(apb->clk_en);
		dev_err(dev, "Failed requesting APB clock en GPIO: %d\n", ret);
		return ret;
	}

	apb->pwrdn = devm_gpiod_get(dev, "pwr-down", GPIOD_OUT_LOW);
	if (IS_ERR(apb->pwrdn)) {
		ret = PTR_ERR(apb->pwrdn);
		dev_warn(dev, "Failed requesting power down GPIO: %d\n", ret);
		return ret;
	}

	/* Regulators are optional, as we may have fixed supply coming in */
	apb->vcore = devm_regulator_get(dev, "vcore");
	if (IS_ERR(apb->vcore))
		dev_warn(dev, "no core regulator found\n");

	apb->vio = devm_regulator_get(dev, "vio");
	if (IS_ERR(apb->vio))
		dev_warn(dev, "no IO regulator found\n");

	apb->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(apb->pinctrl)) {
		dev_err(&pdev->dev, "could not get pinctrl handle\n");
		return PTR_ERR(apb->pinctrl);
	}
	apb->pin_default = pinctrl_lookup_state(apb->pinctrl, "default");
	if (IS_ERR(apb->pin_default)) {
		dev_err(&pdev->dev, "could not get default pin state\n");
		return PTR_ERR(apb->pin_default);
	}

	/* Only applicable for platform >= V2 */
	if (of_property_read_bool(pdev->dev.of_node, "gb,spi-en-active-high"))
		apb->spi_en_polarity_high = true;

	return 0;
}

static int arche_apb_ctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct arche_apb_ctrl_drvdata *apb;
	struct device *dev = &pdev->dev;

	apb = devm_kzalloc(&pdev->dev, sizeof(*apb), GFP_KERNEL);
	if (!apb)
		return -ENOMEM;

	ret = apb_ctrl_get_devtree_data(pdev, apb);
	if (ret) {
		dev_err(dev, "failed to get apb devicetree data %d\n", ret);
		return ret;
	}

	/* Initially set APB to OFF state */
	apb->state = ARCHE_PLATFORM_STATE_OFF;
	/* Check whether device needs to be enabled on boot */
	if (of_property_read_bool(pdev->dev.of_node, "arche,init-disable"))
		apb->init_disabled = true;

	platform_set_drvdata(pdev, apb);

	/* Create sysfs interface to allow user to change state dynamically */
	ret = device_create_file(dev, &dev_attr_state);
	if (ret) {
		dev_err(dev, "failed to create state file in sysfs\n");
		return ret;
	}

	dev_info(&pdev->dev, "Device registered successfully\n");
	return 0;
}

static int arche_apb_ctrl_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_state);
	poweroff_seq(pdev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int __maybe_unused arche_apb_ctrl_suspend(struct device *dev)
{
	/*
	 * If timing profile permits, we may shutdown bridge
	 * completely
	 *
	 * TODO: sequence ??
	 *
	 * Also, need to make sure we meet precondition for unipro suspend
	 * Precondition: Definition ???
	 */
	return 0;
}

static int __maybe_unused arche_apb_ctrl_resume(struct device *dev)
{
	/*
	 * At least for ES2 we have to meet the delay requirement between
	 * unipro switch and AP bridge init, depending on whether bridge is in
	 * OFF state or standby state.
	 *
	 * Based on whether bridge is in standby or OFF state we may have to
	 * assert multiple signals. Please refer to WDM spec, for more info.
	 *
	 */
	return 0;
}

static void arche_apb_ctrl_shutdown(struct platform_device *pdev)
{
	apb_ctrl_poweroff(&pdev->dev);
}

static SIMPLE_DEV_PM_OPS(arche_apb_ctrl_pm_ops, arche_apb_ctrl_suspend,
			 arche_apb_ctrl_resume);

static const struct of_device_id arche_apb_ctrl_of_match[] = {
	{ .compatible = "usbffff,2", },
	{ },
};
MODULE_DEVICE_TABLE(of, arche_apb_ctrl_of_match);

static struct platform_driver arche_apb_ctrl_device_driver = {
	.probe		= arche_apb_ctrl_probe,
	.remove		= arche_apb_ctrl_remove,
	.shutdown	= arche_apb_ctrl_shutdown,
	.driver		= {
		.name	= "arche-apb-ctrl",
		.pm	= &arche_apb_ctrl_pm_ops,
		.of_match_table = arche_apb_ctrl_of_match,
	}
};

int __init arche_apb_init(void)
{
	return platform_driver_register(&arche_apb_ctrl_device_driver);
}

void __exit arche_apb_exit(void)
{
	platform_driver_unregister(&arche_apb_ctrl_device_driver);
}
