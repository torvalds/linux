/*
 * Arche Platform driver to control APB.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include "arche_platform.h"

enum apb_state {
	APB_STATE_OFF,
	APB_STATE_ACTIVE,
	APB_STATE_STANDBY,
};

struct arche_apb_ctrl_drvdata {
	/* Control GPIO signals to and from AP <=> AP Bridges */
	int resetn_gpio;
	int boot_ret_gpio;
	int pwroff_gpio;
	int wake_in_gpio;
	int wake_out_gpio;
	int pwrdn_gpio;

	enum apb_state state;

	struct regulator *vcore;
	struct regulator *vio;

	unsigned int clk_en_gpio;
	struct clk *clk;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;
};

/*
 * Note that these low level api's are active high
 */
static inline void deassert_reset(unsigned int gpio)
{
	gpio_set_value(gpio, 1);
	msleep(500);
}

static inline void assert_reset(unsigned int gpio)
{
	gpio_set_value(gpio, 0);
}

/* Export gpio's to user space */
static void export_gpios(struct arche_apb_ctrl_drvdata *apb)
{
	gpio_export(apb->resetn_gpio, false);
}

static void unexport_gpios(struct arche_apb_ctrl_drvdata *apb)
{
	gpio_unexport(apb->resetn_gpio);
}

/*
 * Note: Please do not modify the below sequence, as it is as per the spec
 */
static int apb_ctrl_init_seq(struct platform_device *pdev,
		struct arche_apb_ctrl_drvdata *apb)
{
	struct device *dev = &pdev->dev;
	int ret;

	/* Hold APB in reset state */
	ret = devm_gpio_request(dev, apb->resetn_gpio, "apb-reset");
	if (ret) {
		dev_err(dev, "Failed requesting reset gpio %d\n",
				apb->resetn_gpio);
		return ret;
	}
	ret = gpio_direction_output(apb->resetn_gpio, 0);
	if (ret) {
		dev_err(dev, "failed to set reset gpio dir:%d\n", ret);
		return ret;
	}

	ret = devm_gpio_request(dev, apb->pwroff_gpio, "pwroff_n");
	if (ret) {
		dev_err(dev, "Failed requesting pwroff_n gpio %d\n",
				apb->pwroff_gpio);
		return ret;
	}
	ret = gpio_direction_input(apb->pwroff_gpio);
	if (ret) {
		dev_err(dev, "failed to set pwroff gpio dir:%d\n", ret);
		return ret;
	}

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
			goto out_vcore_disable;
		}
	}

	ret = devm_gpio_request_one(dev, apb->boot_ret_gpio,
			GPIOF_OUT_INIT_LOW, "boot retention");
	if (ret) {
		dev_err(dev, "Failed requesting bootret gpio %d\n",
				apb->boot_ret_gpio);
		goto out_vio_disable;
	}
	gpio_set_value(apb->boot_ret_gpio, 0);

	/* On DB3 clock was not mandatory */
	if (gpio_is_valid(apb->clk_en_gpio)) {
		ret = devm_gpio_request(dev, apb->clk_en_gpio, "apb_clk_en");
		if (ret) {
			dev_warn(dev, "Failed requesting APB clock en gpio %d\n",
				 apb->clk_en_gpio);
		} else {
			ret = gpio_direction_output(apb->clk_en_gpio, 1);
			if (ret)
				dev_warn(dev, "failed to set APB clock en gpio dir:%d\n",
					 ret);
		}
	}

	usleep_range(100, 200);

	return 0;

out_vio_disable:
	if (!IS_ERR(apb->vio))
		regulator_disable(apb->vio);
out_vcore_disable:
	if (!IS_ERR(apb->vcore))
		regulator_disable(apb->vcore);

	return ret;
}

static int apb_ctrl_get_devtree_data(struct platform_device *pdev,
		struct arche_apb_ctrl_drvdata *apb)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	apb->resetn_gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (apb->resetn_gpio < 0) {
		dev_err(dev, "failed to get reset gpio\n");
		return apb->resetn_gpio;
	}

	apb->boot_ret_gpio = of_get_named_gpio(np, "boot-ret-gpios", 0);
	if (apb->boot_ret_gpio < 0) {
		dev_err(dev, "failed to get boot retention gpio\n");
		return apb->boot_ret_gpio;
	}

	/* It's not mandatory to support power management interface */
	apb->pwroff_gpio = of_get_named_gpio(np, "pwr-off-gpios", 0);
	if (apb->pwroff_gpio < 0) {
		dev_err(dev, "failed to get power off gpio\n");
		return apb->pwroff_gpio;
	}

	/* Do not make clock mandatory as of now (for DB3) */
	apb->clk_en_gpio = of_get_named_gpio(np, "clock-en-gpio", 0);
	if (apb->clk_en_gpio < 0)
		dev_warn(dev, "failed to get clock en gpio\n");

	apb->pwrdn_gpio = of_get_named_gpio(np, "pwr-down-gpios", 0);
	if (apb->pwrdn_gpio < 0)
		dev_warn(dev, "failed to get power down gpio\n");

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

	return 0;
}

static void apb_ctrl_cleanup(struct arche_apb_ctrl_drvdata *apb)
{
	/* disable the clock */
	if (gpio_is_valid(apb->clk_en_gpio))
		gpio_set_value(apb->clk_en_gpio, 0);

	if (!IS_ERR(apb->vcore) && regulator_is_enabled(apb->vcore) > 0)
		regulator_disable(apb->vcore);

	if (!IS_ERR(apb->vio) && regulator_is_enabled(apb->vio) > 0)
		regulator_disable(apb->vio);

	/* As part of exit, put APB back in reset state */
	assert_reset(apb->resetn_gpio);
	apb->state = APB_STATE_OFF;

	/* TODO: May have to send an event to SVC about this exit */
}

int arche_apb_ctrl_probe(struct platform_device *pdev)
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

	ret = apb_ctrl_init_seq(pdev, apb);
	if (ret) {
		dev_err(dev, "failed to set init state of control signal %d\n",
				ret);
		return ret;
	}

	/* deassert reset to APB : Active-low signal */
	deassert_reset(apb->resetn_gpio);
	apb->state = APB_STATE_ACTIVE;

	platform_set_drvdata(pdev, apb);

	export_gpios(apb);

	dev_info(&pdev->dev, "Device registered successfully\n");
	return 0;
}

int arche_apb_ctrl_remove(struct platform_device *pdev)
{
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);

	apb_ctrl_cleanup(apb);
	platform_set_drvdata(pdev, NULL);
	unexport_gpios(apb);

	return 0;
}

static int arche_apb_ctrl_suspend(struct device *dev)
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

static int arche_apb_ctrl_resume(struct device *dev)
{
	/*
	 * Atleast for ES2 we have to meet the delay requirement between
	 * unipro switch and AP bridge init, depending on whether bridge is in
	 * OFF state or standby state.
	 *
	 * Based on whether bridge is in standby or OFF state we may have to
	 * assert multiple signals. Please refer to WDM spec, for more info.
	 *
	 */
	return 0;
}

SIMPLE_DEV_PM_OPS(arche_apb_ctrl_pm_ops,
		  arche_apb_ctrl_suspend,
		  arche_apb_ctrl_resume);
