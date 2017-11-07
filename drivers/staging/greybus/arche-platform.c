// SPDX-License-Identifier: GPL-2.0
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/suspend.h>
#include <linux/time.h>
#include "arche_platform.h"
#include "greybus.h"

#if IS_ENABLED(CONFIG_USB_HSIC_USB3613)
#include <linux/usb/usb3613.h>
#else
static inline int usb3613_hub_mode_ctrl(bool unused)
{
	return 0;
}
#endif

#define WD_COLDBOOT_PULSE_WIDTH_MS	30

enum svc_wakedetect_state {
	WD_STATE_IDLE,			/* Default state = pulled high/low */
	WD_STATE_BOOT_INIT,		/* WD = falling edge (low) */
	WD_STATE_COLDBOOT_TRIG,		/* WD = rising edge (high), > 30msec */
	WD_STATE_STANDBYBOOT_TRIG,	/* As of now not used ?? */
	WD_STATE_COLDBOOT_START,	/* Cold boot process started */
	WD_STATE_STANDBYBOOT_START,	/* Not used */
};

struct arche_platform_drvdata {
	/* Control GPIO signals to and from AP <=> SVC */
	int svc_reset_gpio;
	bool is_reset_act_hi;
	int svc_sysboot_gpio;
	int wake_detect_gpio; /* bi-dir,maps to WAKE_MOD & WAKE_FRAME signals */

	enum arche_platform_state state;

	int svc_refclk_req;
	struct clk *svc_ref_clk;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;

	int num_apbs;

	enum svc_wakedetect_state wake_detect_state;
	int wake_detect_irq;
	spinlock_t wake_lock;			/* Protect wake_detect_state */
	struct mutex platform_state_mutex;	/* Protect state */
	unsigned long wake_detect_start;
	struct notifier_block pm_notifier;

	struct device *dev;
};

/* Requires calling context to hold arche_pdata->platform_state_mutex */
static void arche_platform_set_state(struct arche_platform_drvdata *arche_pdata,
				     enum arche_platform_state state)
{
	arche_pdata->state = state;
}

/* Requires arche_pdata->wake_lock is held by calling context */
static void arche_platform_set_wake_detect_state(
				struct arche_platform_drvdata *arche_pdata,
				enum svc_wakedetect_state state)
{
	arche_pdata->wake_detect_state = state;
}

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

static int apb_poweroff(struct device *dev, void *data)
{
	apb_ctrl_poweroff(dev);

	/* Enable HUB3613 into HUB mode. */
	if (usb3613_hub_mode_ctrl(false))
		dev_warn(dev, "failed to control hub device\n");

	return 0;
}

static void arche_platform_wd_irq_en(struct arche_platform_drvdata *arche_pdata)
{
	/* Enable interrupt here, to read event back from SVC */
	gpio_direction_input(arche_pdata->wake_detect_gpio);
	enable_irq(arche_pdata->wake_detect_irq);
}

static irqreturn_t arche_platform_wd_irq_thread(int irq, void *devid)
{
	struct arche_platform_drvdata *arche_pdata = devid;
	unsigned long flags;

	spin_lock_irqsave(&arche_pdata->wake_lock, flags);
	if (arche_pdata->wake_detect_state != WD_STATE_COLDBOOT_TRIG) {
		/* Something is wrong */
		spin_unlock_irqrestore(&arche_pdata->wake_lock, flags);
		return IRQ_HANDLED;
	}

	arche_platform_set_wake_detect_state(arche_pdata,
					     WD_STATE_COLDBOOT_START);
	spin_unlock_irqrestore(&arche_pdata->wake_lock, flags);

	/* It should complete power cycle, so first make sure it is poweroff */
	device_for_each_child(arche_pdata->dev, NULL, apb_poweroff);

	/* Bring APB out of reset: cold boot sequence */
	device_for_each_child(arche_pdata->dev, NULL, apb_cold_boot);

	/* Enable HUB3613 into HUB mode. */
	if (usb3613_hub_mode_ctrl(true))
		dev_warn(arche_pdata->dev, "failed to control hub device\n");

	spin_lock_irqsave(&arche_pdata->wake_lock, flags);
	arche_platform_set_wake_detect_state(arche_pdata, WD_STATE_IDLE);
	spin_unlock_irqrestore(&arche_pdata->wake_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t arche_platform_wd_irq(int irq, void *devid)
{
	struct arche_platform_drvdata *arche_pdata = devid;
	unsigned long flags;

	spin_lock_irqsave(&arche_pdata->wake_lock, flags);

	if (gpio_get_value(arche_pdata->wake_detect_gpio)) {
		/* wake/detect rising */

		/*
		 * If wake/detect line goes high after low, within less than
		 * 30msec, then standby boot sequence is initiated, which is not
		 * supported/implemented as of now. So ignore it.
		 */
		if (arche_pdata->wake_detect_state == WD_STATE_BOOT_INIT) {
			if (time_before(jiffies,
					arche_pdata->wake_detect_start +
					msecs_to_jiffies(WD_COLDBOOT_PULSE_WIDTH_MS))) {
				arche_platform_set_wake_detect_state(arche_pdata,
								     WD_STATE_IDLE);
			} else {
				/*
				 * Check we are not in middle of irq thread
				 * already
				 */
				if (arche_pdata->wake_detect_state !=
						WD_STATE_COLDBOOT_START) {
					arche_platform_set_wake_detect_state(arche_pdata,
									     WD_STATE_COLDBOOT_TRIG);
					spin_unlock_irqrestore(
						&arche_pdata->wake_lock,
						flags);
					return IRQ_WAKE_THREAD;
				}
			}
		}
	} else {
		/* wake/detect falling */
		if (arche_pdata->wake_detect_state == WD_STATE_IDLE) {
			arche_pdata->wake_detect_start = jiffies;
			/*
			 * In the beginning, when wake/detect goes low
			 * (first time), we assume it is meant for coldboot
			 * and set the flag. If wake/detect line stays low
			 * beyond 30msec, then it is coldboot else fallback
			 * to standby boot.
			 */
			arche_platform_set_wake_detect_state(arche_pdata,
							     WD_STATE_BOOT_INIT);
		}
	}

	spin_unlock_irqrestore(&arche_pdata->wake_lock, flags);

	return IRQ_HANDLED;
}

/*
 * Requires arche_pdata->platform_state_mutex to be held
 */
static int
arche_platform_coldboot_seq(struct arche_platform_drvdata *arche_pdata)
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

	arche_platform_set_state(arche_pdata, ARCHE_PLATFORM_STATE_ACTIVE);

	return 0;
}

/*
 * Requires arche_pdata->platform_state_mutex to be held
 */
static int
arche_platform_fw_flashing_seq(struct arche_platform_drvdata *arche_pdata)
{
	int ret;

	if (arche_pdata->state == ARCHE_PLATFORM_STATE_FW_FLASHING)
		return 0;

	dev_info(arche_pdata->dev, "Switching to FW flashing state\n");

	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			arche_pdata->is_reset_act_hi);

	gpio_set_value(arche_pdata->svc_sysboot_gpio, 1);

	usleep_range(100, 200);

	ret = clk_prepare_enable(arche_pdata->svc_ref_clk);
	if (ret) {
		dev_err(arche_pdata->dev, "failed to enable svc_ref_clk: %d\n",
				ret);
		return ret;
	}

	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			!arche_pdata->is_reset_act_hi);

	arche_platform_set_state(arche_pdata, ARCHE_PLATFORM_STATE_FW_FLASHING);

	return 0;
}

/*
 * Requires arche_pdata->platform_state_mutex to be held
 */
static void
arche_platform_poweroff_seq(struct arche_platform_drvdata *arche_pdata)
{
	unsigned long flags;

	if (arche_pdata->state == ARCHE_PLATFORM_STATE_OFF)
		return;

	/* If in fw_flashing mode, then no need to repeate things again */
	if (arche_pdata->state != ARCHE_PLATFORM_STATE_FW_FLASHING) {
		disable_irq(arche_pdata->wake_detect_irq);

		spin_lock_irqsave(&arche_pdata->wake_lock, flags);
		arche_platform_set_wake_detect_state(arche_pdata,
						     WD_STATE_IDLE);
		spin_unlock_irqrestore(&arche_pdata->wake_lock, flags);
	}

	clk_disable_unprepare(arche_pdata->svc_ref_clk);

	/* As part of exit, put APB back in reset state */
	svc_reset_onoff(arche_pdata->svc_reset_gpio,
			arche_pdata->is_reset_act_hi);

	arche_platform_set_state(arche_pdata, ARCHE_PLATFORM_STATE_OFF);
}

static ssize_t state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct arche_platform_drvdata *arche_pdata = platform_get_drvdata(pdev);
	int ret = 0;

	mutex_lock(&arche_pdata->platform_state_mutex);

	if (sysfs_streq(buf, "off")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_OFF)
			goto exit;

		/*  If SVC goes down, bring down APB's as well */
		device_for_each_child(arche_pdata->dev, NULL, apb_poweroff);

		arche_platform_poweroff_seq(arche_pdata);

	} else if (sysfs_streq(buf, "active")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_ACTIVE)
			goto exit;

		/* First we want to make sure we power off everything
		 * and then activate back again
		 */
		device_for_each_child(arche_pdata->dev, NULL, apb_poweroff);
		arche_platform_poweroff_seq(arche_pdata);

		arche_platform_wd_irq_en(arche_pdata);
		ret = arche_platform_coldboot_seq(arche_pdata);
		if (ret)
			goto exit;

	} else if (sysfs_streq(buf, "standby")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_STANDBY)
			goto exit;

		dev_warn(arche_pdata->dev, "standby state not supported\n");
	} else if (sysfs_streq(buf, "fw_flashing")) {
		if (arche_pdata->state == ARCHE_PLATFORM_STATE_FW_FLASHING)
			goto exit;

		/*
		 * Here we only control SVC.
		 *
		 * In case of FW_FLASHING mode we do not want to control
		 * APBs, as in case of V2, SPI bus is shared between both
		 * the APBs. So let user chose which APB he wants to flash.
		 */
		arche_platform_poweroff_seq(arche_pdata);

		ret = arche_platform_fw_flashing_seq(arche_pdata);
		if (ret)
			goto exit;
	} else {
		dev_err(arche_pdata->dev, "unknown state\n");
		ret = -EINVAL;
	}

exit:
	mutex_unlock(&arche_pdata->platform_state_mutex);
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

static int arche_platform_pm_notifier(struct notifier_block *notifier,
				      unsigned long pm_event, void *unused)
{
	struct arche_platform_drvdata *arche_pdata =
		container_of(notifier, struct arche_platform_drvdata,
			     pm_notifier);
	int ret = NOTIFY_DONE;

	mutex_lock(&arche_pdata->platform_state_mutex);
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		if (arche_pdata->state != ARCHE_PLATFORM_STATE_ACTIVE) {
			ret = NOTIFY_STOP;
			break;
		}
		device_for_each_child(arche_pdata->dev, NULL, apb_poweroff);
		arche_platform_poweroff_seq(arche_pdata);
		break;
	case PM_POST_SUSPEND:
		if (arche_pdata->state != ARCHE_PLATFORM_STATE_OFF)
			break;

		arche_platform_wd_irq_en(arche_pdata);
		arche_platform_coldboot_seq(arche_pdata);
		break;
	default:
		break;
	}
	mutex_unlock(&arche_pdata->platform_state_mutex);

	return ret;
}

static int arche_platform_probe(struct platform_device *pdev)
{
	struct arche_platform_drvdata *arche_pdata;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	arche_pdata = devm_kzalloc(&pdev->dev, sizeof(*arche_pdata),
				   GFP_KERNEL);
	if (!arche_pdata)
		return -ENOMEM;

	/* setup svc reset gpio */
	arche_pdata->is_reset_act_hi = of_property_read_bool(np,
					"svc,reset-active-high");
	arche_pdata->svc_reset_gpio = of_get_named_gpio(np,
							"svc,reset-gpio",
							0);
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
	arche_platform_set_state(arche_pdata, ARCHE_PLATFORM_STATE_OFF);

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
	ret = devm_gpio_request(dev, arche_pdata->svc_refclk_req,
				"svc-clk-req");
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

	arche_pdata->wake_detect_gpio = of_get_named_gpio(np,
							  "svc,wake-detect-gpio",
							  0);
	if (arche_pdata->wake_detect_gpio < 0) {
		dev_err(dev, "failed to get wake detect gpio\n");
		return arche_pdata->wake_detect_gpio;
	}

	ret = devm_gpio_request(dev, arche_pdata->wake_detect_gpio,
				"wake detect");
	if (ret) {
		dev_err(dev, "Failed requesting wake_detect gpio %d\n",
				arche_pdata->wake_detect_gpio);
		return ret;
	}

	arche_platform_set_wake_detect_state(arche_pdata, WD_STATE_IDLE);

	arche_pdata->dev = &pdev->dev;

	spin_lock_init(&arche_pdata->wake_lock);
	mutex_init(&arche_pdata->platform_state_mutex);
	arche_pdata->wake_detect_irq =
		gpio_to_irq(arche_pdata->wake_detect_gpio);

	ret = devm_request_threaded_irq(dev, arche_pdata->wake_detect_irq,
					arche_platform_wd_irq,
					arche_platform_wd_irq_thread,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(dev), arche_pdata);
	if (ret) {
		dev_err(dev, "failed to request wake detect IRQ %d\n", ret);
		return ret;
	}
	disable_irq(arche_pdata->wake_detect_irq);

	ret = device_create_file(dev, &dev_attr_state);
	if (ret) {
		dev_err(dev, "failed to create state file in sysfs\n");
		return ret;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to populate child nodes %d\n", ret);
		goto err_device_remove;
	}

	arche_pdata->pm_notifier.notifier_call = arche_platform_pm_notifier;
	ret = register_pm_notifier(&arche_pdata->pm_notifier);

	if (ret) {
		dev_err(dev, "failed to register pm notifier %d\n", ret);
		goto err_device_remove;
	}

	/* Explicitly power off if requested */
	if (!of_property_read_bool(pdev->dev.of_node, "arche,init-off")) {
		mutex_lock(&arche_pdata->platform_state_mutex);
		ret = arche_platform_coldboot_seq(arche_pdata);
		if (ret) {
			dev_err(dev, "Failed to cold boot svc %d\n", ret);
			goto err_coldboot;
		}
		arche_platform_wd_irq_en(arche_pdata);
		mutex_unlock(&arche_pdata->platform_state_mutex);
	}

	dev_info(dev, "Device registered successfully\n");
	return 0;

err_coldboot:
	mutex_unlock(&arche_pdata->platform_state_mutex);
err_device_remove:
	device_remove_file(&pdev->dev, &dev_attr_state);
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

	unregister_pm_notifier(&arche_pdata->pm_notifier);
	device_remove_file(&pdev->dev, &dev_attr_state);
	device_for_each_child(&pdev->dev, NULL, arche_remove_child);
	arche_platform_poweroff_seq(arche_pdata);

	if (usb3613_hub_mode_ctrl(false))
		dev_warn(arche_pdata->dev, "failed to control hub device\n");
		/* TODO: Should we do anything more here ?? */
	return 0;
}

static __maybe_unused int arche_platform_suspend(struct device *dev)
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

static __maybe_unused int arche_platform_resume(struct device *dev)
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

static void arche_platform_shutdown(struct platform_device *pdev)
{
	struct arche_platform_drvdata *arche_pdata = platform_get_drvdata(pdev);

	arche_platform_poweroff_seq(arche_pdata);

	usb3613_hub_mode_ctrl(false);
}

static SIMPLE_DEV_PM_OPS(arche_platform_pm_ops,
			arche_platform_suspend,
			arche_platform_resume);

static const struct of_device_id arche_platform_of_match[] = {
	/* Use PID/VID of SVC device */
	{ .compatible = "google,arche-platform", },
	{ },
};

static const struct of_device_id arche_combined_id[] = {
	/* Use PID/VID of SVC device */
	{ .compatible = "google,arche-platform", },
	{ .compatible = "usbffff,2", },
	{ },
};
MODULE_DEVICE_TABLE(of, arche_combined_id);

static struct platform_driver arche_platform_device_driver = {
	.probe		= arche_platform_probe,
	.remove		= arche_platform_remove,
	.shutdown	= arche_platform_shutdown,
	.driver		= {
		.name	= "arche-platform-ctrl",
		.pm	= &arche_platform_pm_ops,
		.of_match_table = arche_platform_of_match,
	}
};

static int __init arche_init(void)
{
	int retval;

	retval = platform_driver_register(&arche_platform_device_driver);
	if (retval)
		return retval;

	retval = arche_apb_init();
	if (retval)
		platform_driver_unregister(&arche_platform_device_driver);

	return retval;
}
module_init(arche_init);

static void __exit arche_exit(void)
{
	arche_apb_exit();
	platform_driver_unregister(&arche_platform_device_driver);
}
module_exit(arche_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vaibhav Hiremath <vaibhav.hiremath@linaro.org>");
MODULE_DESCRIPTION("Arche Platform Driver");
