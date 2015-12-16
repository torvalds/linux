/*
 * Arche Platform driver to enable Unipro link.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>

struct arche_platform_drvdata {
	/* Control GPIO signals to and from AP <=> SVC */
	int svc_reset_gpio;
	bool is_reset_act_hi;
	int svc_sysboot_gpio;

	unsigned int svc_refclk_req;
	struct clk *svc_ref_clk;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;

	int num_apbs;
};

static inline void svc_reset_onoff(unsigned int gpio, bool onoff)
{
	gpio_set_value(gpio, onoff);
}

static void arche_platform_cleanup(struct arche_platform_drvdata *arche_pdata)
{
	/* As part of exit, put APB back in reset state */
	if (gpio_is_valid(arche_pdata->svc_reset_gpio))
		svc_reset_onoff(arche_pdata->svc_reset_gpio,
				arche_pdata->is_reset_act_hi);
}

static int arche_platform_probe(struct platform_device *pdev)
{
	struct arche_platform_drvdata *arche_pdata;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	arche_pdata = devm_kzalloc(&pdev->dev, sizeof(*arche_pdata), GFP_KERNEL);
	if (!arche_pdata)
		return -ENOMEM;

	/* setup svc reset gpio */
	arche_pdata->is_reset_act_hi = of_property_read_bool(np,
					"svc,reset-active-high");
	arche_pdata->svc_reset_gpio = of_get_named_gpio(np, "svc,reset-gpio", 0);
	if (arche_pdata->svc_reset_gpio < 0) {
		dev_err(dev, "failed to get reset-gpio\n");
		return -ENODEV;
	}
	ret = devm_gpio_request(dev, arche_pdata->svc_reset_gpio, "svc-reset");
	if (ret) {
		dev_err(dev, "failed to request svc-reset gpio:%d\n", ret);
		return ret;
	}
	ret = gpio_direction_output(arche_pdata->svc_reset_gpio,
					arche_pdata->is_reset_act_hi);
	if (ret) {
		dev_err(dev, "failed to set svc-reset gpio dir:%d\n", ret);
		return ret;
	}

	arche_pdata->svc_sysboot_gpio = of_get_named_gpio(np,
					"svc,sysboot-gpio", 0);
	if (arche_pdata->svc_sysboot_gpio < 0) {
		dev_err(dev, "failed to get sysboot gpio\n");
		return -ENODEV;
	}
	ret = devm_gpio_request(dev, arche_pdata->svc_sysboot_gpio, "sysboot0");
	if (ret) {
		dev_err(dev, "failed to request sysboot0 gpio:%d\n", ret);
		return ret;
	}
	ret = gpio_direction_output(arche_pdata->svc_sysboot_gpio, 0);
	if (ret) {
		dev_err(dev, "failed to set svc-reset gpio dir:%d\n", ret);
		return ret;
	}

	/* setup the clock request gpio first */
	arche_pdata->svc_refclk_req = of_get_named_gpio(np,
					"svc,refclk-req-gpio", 0);
	if (arche_pdata->svc_refclk_req < 0) {
		dev_err(dev, "failed to get svc clock-req gpio\n");
		return -ENODEV;
	}
	ret = devm_gpio_request(dev, arche_pdata->svc_refclk_req, "svc-clk-req");
	if (ret) {
		dev_err(dev, "failed to request svc-clk-req gpio: %d\n", ret);
		return ret;
	}
	ret = gpio_direction_input(arche_pdata->svc_refclk_req);
	if (ret) {
		dev_err(dev, "failed to set svc-clk-req gpio dir :%d\n", ret);
		return ret;
	}

	/* setup refclk2 to follow the pin */
	arche_pdata->svc_ref_clk = devm_clk_get(dev, "svc_ref_clk");
	if (IS_ERR(arche_pdata->svc_ref_clk)) {
		ret = PTR_ERR(arche_pdata->svc_ref_clk);
		dev_err(dev, "failed to get svc_ref_clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(arche_pdata->svc_ref_clk);
	if (ret) {
		dev_err(dev, "failed to enable svc_ref_clk: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, arche_pdata);

	/* bring SVC out of reset */
	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			!arche_pdata->is_reset_act_hi);

	arche_pdata->num_apbs = of_get_child_count(np);
	dev_dbg(dev, "Number of APB's available - %d\n", arche_pdata->num_apbs);

	/* probe all childs here */
	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		dev_err(dev, "no child node found\n");

	dev_info(dev, "Device registered successfully\n");
	return ret;
}

static int arche_platform_remove(struct platform_device *pdev)
{
	struct arche_platform_drvdata *arche_pdata = platform_get_drvdata(pdev);

	if (arche_pdata)
		arche_platform_cleanup(arche_pdata);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int arche_platform_suspend(struct device *dev)
{
	/*
	 * If timing profile premits, we may shutdown bridge
	 * completely
	 *
	 * TODO: sequence ??
	 *
	 * Also, need to make sure we meet precondition for unipro suspend
	 * Precondition: Definition ???
	 */
	return 0;
}

static int arche_platform_resume(struct device *dev)
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

static SIMPLE_DEV_PM_OPS(arche_platform_pm_ops,
			arche_platform_suspend,
			arche_platform_resume);

static struct of_device_id arche_platform_of_match[] = {
	{ .compatible = "google,arche-platform", }, /* Use PID/VID of SVC device */
	{ },
};
MODULE_DEVICE_TABLE(of, arche_platform_of_match);

static struct platform_driver arche_platform_device_driver = {
	.probe		= arche_platform_probe,
	.remove		= arche_platform_remove,
	.driver		= {
		.name	= "arche-platform-ctrl",
		.pm	= &arche_platform_pm_ops,
		.of_match_table = of_match_ptr(arche_platform_of_match),
	}
};

module_platform_driver(arche_platform_device_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vaibhav Hiremath <vaibhav.hiremath@linaro.org>");
MODULE_DESCRIPTION("Arche Platform Driver");
