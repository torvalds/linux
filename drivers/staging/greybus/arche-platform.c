/*
 * Arche Platform driver to enable Unipro link.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include "arche_platform.h"

struct arche_platform_drvdata {
	/* Control GPIO signals to and from AP <=> SVC */
	int svc_reset_gpio;
	bool is_reset_act_hi;
	int svc_sysboot_gpio;
	int wake_detect_gpio; /* bi-dir,maps to WAKE_MOD & WAKE_FRAME signals */

	unsigned int svc_refclk_req;
	struct clk *svc_ref_clk;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;

	int num_apbs;

	struct delayed_work delayed_work;
	struct device *dev;
};

static inline void svc_reset_onoff(unsigned int gpio, bool onoff)
{
	gpio_set_value(gpio, onoff);
}

/**
 * svc_delayed_work - Time to give SVC to boot.
 */
static void svc_delayed_work(struct work_struct *work)
{
	struct arche_platform_drvdata *arche_pdata =
		container_of(work, struct arche_platform_drvdata, delayed_work.work);
	struct device *dev = arche_pdata->dev;
	struct device_node *np = dev->of_node;
	int timeout = 50;
	int ret;

	/*
	 * 1.   SVC and AP boot independently, with AP<-->SVC wake/detect pin
	 *      deasserted (LOW in this case)
	 * 2.1. SVC allows 360 milliseconds to elapse after switch boots to work
	 *      around bug described in ENG-330.
	 * 2.2. AP asserts wake/detect pin (HIGH) (this can proceed in parallel with 2.1)
	 * 3.   SVC detects assertion of wake/detect pin, and sends "wake out" signal to AP
	 * 4.   AP receives "wake out" signal, takes AP Bridges through their power
	 *      on reset sequence as defined in the bridge ASIC reference manuals
	 * 5. AP takes USB3613 through its power on reset sequence
	 * 6. AP enumerates AP Bridges
	 */
	gpio_set_value(arche_pdata->wake_detect_gpio, 1);
	gpio_direction_input(arche_pdata->wake_detect_gpio);
	do {
		/* Read the wake_detect GPIO, for WAKE_OUT event from SVC */
		if (gpio_get_value(arche_pdata->wake_detect_gpio) == 0)
			break;

		msleep(100);
	} while(timeout--);

	if (timeout >= 0) {
		ret = of_platform_populate(np, NULL, NULL, dev);
		if (!ret) {
			/* re-assert wake_detect to confirm SVC WAKE_OUT */
			gpio_direction_output(arche_pdata->wake_detect_gpio, 1);
			return;
		}
	}

	/* FIXME: We may want to limit retries here */
	gpio_direction_output(arche_pdata->wake_detect_gpio, 0);
	schedule_delayed_work(&arche_pdata->delayed_work, msecs_to_jiffies(2000));
}

/* Export gpio's to user space */
static void export_gpios(struct arche_platform_drvdata *arche_pdata)
{
	gpio_export(arche_pdata->svc_reset_gpio, false);
	gpio_export(arche_pdata->svc_sysboot_gpio, false);
}

static void unexport_gpios(struct arche_platform_drvdata *arche_pdata)
{
	gpio_unexport(arche_pdata->svc_reset_gpio);
	gpio_unexport(arche_pdata->svc_sysboot_gpio);
}

static void arche_platform_cleanup(struct arche_platform_drvdata *arche_pdata)
{
	clk_disable_unprepare(arche_pdata->svc_ref_clk);
	/* As part of exit, put APB back in reset state */
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
		return arche_pdata->svc_reset_gpio;
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
		return arche_pdata->svc_sysboot_gpio;
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
		return arche_pdata->svc_refclk_req;
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

	arche_pdata->num_apbs = of_get_child_count(np);
	dev_dbg(dev, "Number of APB's available - %d\n", arche_pdata->num_apbs);

	arche_pdata->wake_detect_gpio = of_get_named_gpio(np, "svc,wake-detect-gpio", 0);
	if (arche_pdata->wake_detect_gpio < 0) {
		dev_err(dev, "failed to get wake detect gpio\n");
		ret = arche_pdata->wake_detect_gpio;
		goto exit;
	}

	ret = devm_gpio_request(dev, arche_pdata->wake_detect_gpio, "wake detect");
	if (ret) {
		dev_err(dev, "Failed requesting wake_detect gpio %d\n",
				arche_pdata->wake_detect_gpio);
		goto exit;
	}
	/* deassert wake detect */
	gpio_direction_output(arche_pdata->wake_detect_gpio, 0);

	/* bring SVC out of reset */
	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			!arche_pdata->is_reset_act_hi);

	arche_pdata->dev = &pdev->dev;
	INIT_DELAYED_WORK(&arche_pdata->delayed_work, svc_delayed_work);
	schedule_delayed_work(&arche_pdata->delayed_work, msecs_to_jiffies(2000));

	export_gpios(arche_pdata);

	dev_info(dev, "Device registered successfully\n");
	return 0;

exit:
	arche_platform_cleanup(arche_pdata);
	return ret;
}

static int arche_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int arche_platform_remove(struct platform_device *pdev)
{
	struct arche_platform_drvdata *arche_pdata = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, arche_remove_child);
	arche_platform_cleanup(arche_pdata);
	platform_set_drvdata(pdev, NULL);
	unexport_gpios(arche_pdata);

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

static struct of_device_id arche_apb_ctrl_of_match[] = {
	{ .compatible = "usbffff,2", },
	{ },
};

static struct of_device_id arche_combined_id[] = {
	{ .compatible = "google,arche-platform", }, /* Use PID/VID of SVC device */
	{ .compatible = "usbffff,2", },
	{ },
};
MODULE_DEVICE_TABLE(of, arche_combined_id);

static struct platform_driver arche_platform_device_driver = {
	.probe		= arche_platform_probe,
	.remove		= arche_platform_remove,
	.driver		= {
		.name	= "arche-platform-ctrl",
		.pm	= &arche_platform_pm_ops,
		.of_match_table = arche_platform_of_match,
	}
};

static struct platform_driver arche_apb_ctrl_device_driver = {
	.probe		= arche_apb_ctrl_probe,
	.remove		= arche_apb_ctrl_remove,
	.driver		= {
		.name	= "arche-apb-ctrl",
		.pm	= &arche_apb_ctrl_pm_ops,
		.of_match_table = arche_apb_ctrl_of_match,
	}
};

static int __init arche_init(void)
{
	int retval;

	retval = platform_driver_register(&arche_platform_device_driver);
	if (retval)
		return retval;

	retval = platform_driver_register(&arche_apb_ctrl_device_driver);
	if (retval)
		platform_driver_unregister(&arche_platform_device_driver);

	return retval;
}
module_init(arche_init);

static void __exit arche_exit(void)
{
	platform_driver_unregister(&arche_apb_ctrl_device_driver);
	platform_driver_unregister(&arche_platform_device_driver);
}
module_exit(arche_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vaibhav Hiremath <vaibhav.hiremath@linaro.org>");
MODULE_DESCRIPTION("Arche Platform Driver");
