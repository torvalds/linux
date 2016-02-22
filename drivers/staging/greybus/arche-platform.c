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

#include <linux/usb/usb3613.h>

struct arche_platform_drvdata {
	/* Control GPIO signals to and from AP <=> SVC */
	int svc_reset_gpio;
	bool is_reset_act_hi;
	int svc_sysboot_gpio;
	int wake_detect_gpio; /* bi-dir,maps to WAKE_MOD & WAKE_FRAME signals */

	enum arche_platform_state state;

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

static int apb_cold_boot(struct device *dev, void *data)
{
	int ret;

	ret = apb_ctrl_coldboot(dev);
	if (ret)
		dev_warn(dev, "failed to coldboot\n");

	/*Child nodes are independent, so do not exit coldboot operation */
	return 0;
}

static int apb_fw_flashing_state(struct device *dev, void *data)
{
	int ret;

	ret = apb_ctrl_fw_flashing(dev);
	if (ret)
		dev_warn(dev, "failed to switch to fw flashing state\n");

	/*Child nodes are independent, so do not exit coldboot operation */
	return 0;
}

static int apb_poweroff(struct device *dev, void *data)
{
	apb_ctrl_poweroff(dev);

	return 0;
}

/**
 * svc_delayed_work - Time to give SVC to boot.
 */
static void svc_delayed_work(struct work_struct *work)
{
	struct arche_platform_drvdata *arche_pdata =
		container_of(work, struct arche_platform_drvdata, delayed_work.work);
	int timeout = 50;

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

	if (timeout < 0) {
		/* FIXME: We may want to limit retries here */
		dev_warn(arche_pdata->dev,
			"Timed out on wake/detect, rescheduling handshake\n");
		gpio_direction_output(arche_pdata->wake_detect_gpio, 0);
		schedule_delayed_work(&arche_pdata->delayed_work, msecs_to_jiffies(2000));
		return;
	}

	/* Bring APB out of reset: cold boot sequence */
	device_for_each_child(arche_pdata->dev, NULL, apb_cold_boot);

	/* re-assert wake_detect to confirm SVC WAKE_OUT */
	gpio_direction_output(arche_pdata->wake_detect_gpio, 1);

	/* Enable HUB3613 into HUB mode. */
	if (usb3613_hub_mode_ctrl(true))
		dev_warn(arche_pdata->dev, "failed to control hub device\n");
}

static int arche_platform_coldboot_seq(struct arche_platform_drvdata *arche_pdata)
{
	int ret;

	if (arche_pdata->state == ARCHE_PLATFORM_STATE_ACTIVE)
		return 0;

	dev_info(arche_pdata->dev, "Booting from cold boot state\n");

	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			arche_pdata->is_reset_act_hi);

	gpio_set_value(arche_pdata->svc_sysboot_gpio, 0);
	usleep_range(100, 200);

	ret = clk_prepare_enable(arche_pdata->svc_ref_clk);
	if (ret) {
		dev_err(arche_pdata->dev, "failed to enable svc_ref_clk: %d\n",
				ret);
		return ret;
	}

	/* bring SVC out of reset */
	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			!arche_pdata->is_reset_act_hi);

	arche_pdata->state = ARCHE_PLATFORM_STATE_ACTIVE;

	return 0;
}

static void arche_platform_fw_flashing_seq(struct arche_platform_drvdata *arche_pdata)
{
	if (arche_pdata->state == ARCHE_PLATFORM_STATE_FW_FLASHING)
		return;

	dev_info(arche_pdata->dev, "Switching to FW flashing state\n");

	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			arche_pdata->is_reset_act_hi);

	gpio_set_value(arche_pdata->svc_sysboot_gpio, 1);

	usleep_range(100, 200);
	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			!arche_pdata->is_reset_act_hi);

	arche_pdata->state = ARCHE_PLATFORM_STATE_FW_FLASHING;

}

static void arche_platform_poweroff_seq(struct arche_platform_drvdata *arche_pdata)
{
	if (arche_pdata->state == ARCHE_PLATFORM_STATE_OFF)
		return;

	/* Send disconnect/detach event to SVC */
	gpio_set_value(arche_pdata->wake_detect_gpio, 0);
	usleep_range(100, 200);

	clk_disable_unprepare(arche_pdata->svc_ref_clk);
	/* As part of exit, put APB back in reset state */
	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			arche_pdata->is_reset_act_hi);

	arche_pdata->state = ARCHE_PLATFORM_STATE_OFF;
}

static ssize_t state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct arche_platform_drvdata *arche_pdata = platform_get_drvdata(pdev);
	int ret = 0;

	if (sysfs_streq(buf, "off")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_OFF)
			return count;

		/*  If SVC goes down, bring down APB's as well */
		device_for_each_child(arche_pdata->dev, NULL, apb_poweroff);

		arche_platform_poweroff_seq(arche_pdata);

		ret = usb3613_hub_mode_ctrl(false);
		if (ret)
			dev_warn(arche_pdata->dev, "failed to control hub device\n");
			/* TODO: Should we do anything more here ?? */
	} else if (sysfs_streq(buf, "active")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_ACTIVE)
			return count;

		ret = arche_platform_coldboot_seq(arche_pdata);
		/* Give enough time for SVC to boot and then handshake with SVC */
		schedule_delayed_work(&arche_pdata->delayed_work, msecs_to_jiffies(2000));
	} else if (sysfs_streq(buf, "standby")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_STANDBY)
			return count;

		dev_warn(arche_pdata->dev, "standby state not supported\n");
	} else if (sysfs_streq(buf, "fw_flashing")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_FW_FLASHING)
			return count;

		/* First we want to make sure we power off everything
		 * and then enter FW flashing state */
		device_for_each_child(arche_pdata->dev, NULL, apb_poweroff);

		arche_platform_poweroff_seq(arche_pdata);

		ret = usb3613_hub_mode_ctrl(false);
		if (ret)
			dev_warn(arche_pdata->dev, "failed to control hub device\n");
			/* TODO: Should we do anything more here ?? */

		arche_platform_fw_flashing_seq(arche_pdata);

		device_for_each_child(arche_pdata->dev, NULL, apb_fw_flashing_state);
	} else {
		dev_err(arche_pdata->dev, "unknown state\n");
		ret = -EINVAL;
	}

	return ret ? ret : count;
}

static ssize_t state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arche_platform_drvdata *arche_pdata = dev_get_drvdata(dev);

	switch (arche_pdata->state) {
	case ARCHE_PLATFORM_STATE_OFF:
		return sprintf(buf, "off\n");
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
	arche_pdata->state = ARCHE_PLATFORM_STATE_OFF;

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

	platform_set_drvdata(pdev, arche_pdata);

	arche_pdata->num_apbs = of_get_child_count(np);
	dev_dbg(dev, "Number of APB's available - %d\n", arche_pdata->num_apbs);

	arche_pdata->wake_detect_gpio = of_get_named_gpio(np, "svc,wake-detect-gpio", 0);
	if (arche_pdata->wake_detect_gpio < 0) {
		dev_err(dev, "failed to get wake detect gpio\n");
		ret = arche_pdata->wake_detect_gpio;
		return ret;
	}

	ret = devm_gpio_request(dev, arche_pdata->wake_detect_gpio, "wake detect");
	if (ret) {
		dev_err(dev, "Failed requesting wake_detect gpio %d\n",
				arche_pdata->wake_detect_gpio);
		return ret;
	}
	/* deassert wake detect */
	gpio_direction_output(arche_pdata->wake_detect_gpio, 0);

	arche_pdata->dev = &pdev->dev;

	ret = device_create_file(dev, &dev_attr_state);
	if (ret) {
		dev_err(dev, "failed to create state file in sysfs\n");
		return ret;
	}

	ret = arche_platform_coldboot_seq(arche_pdata);
	if (ret) {
		dev_err(dev, "Failed to cold boot svc %d\n", ret);
		return ret;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to populate child nodes %d\n", ret);
		return ret;
	}

	INIT_DELAYED_WORK(&arche_pdata->delayed_work, svc_delayed_work);
	schedule_delayed_work(&arche_pdata->delayed_work, msecs_to_jiffies(2000));

	dev_info(dev, "Device registered successfully\n");
	return 0;
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

	device_remove_file(&pdev->dev, &dev_attr_state);
	cancel_delayed_work_sync(&arche_pdata->delayed_work);
	device_for_each_child(&pdev->dev, NULL, arche_remove_child);
	arche_platform_poweroff_seq(arche_pdata);
	platform_set_drvdata(pdev, NULL);

	if (usb3613_hub_mode_ctrl(false))
		dev_warn(arche_pdata->dev, "failed to control hub device\n");
		/* TODO: Should we do anything more here ?? */
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
