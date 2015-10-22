/*
 * DB3 Platform driver for AP bridge control interface.
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
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>

enum apb_state {
	APB_STATE_OFF,
	APB_STATE_ACTIVE,
	APB_STATE_STANDBY,
};

/*
 * Control GPIO signals to and from AP <=> AP Bridges
 */
struct apb_ctrl_gpios {
	int wake_detect; /* bi-dir, maps to WAKE_MOD and WAKE_FRAME signals */
	int reset;
	int boot_ret;
	int pwroff;
	int wake_in;
	int wake_out;
	int pwrdn;
};

struct apb_ctrl_drvdata {
	struct apb_ctrl_gpios ctrl;

	unsigned int wake_detect_irq;
	enum apb_state state;

	struct regulator *vcore;
	struct regulator *vio;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;

	/* To protect concurrent access of GPIO registers, need protection */
	spinlock_t lock;
};

static inline void assert_wake(unsigned int wake_detect)
{
	gpio_set_value(wake_detect, 1);
}

static inline void deassert_wake(unsigned int wake_detect)
{
	gpio_set_value(wake_detect, 0);
}

static irqreturn_t apb_ctrl_wake_detect_irq(int irq, void *devid)
{
	struct apb_ctrl_drvdata *apb_data = devid;
	unsigned long flags;

	/*
	 * TODO:
	 * Since currently SoC GPIOs are being used we are safe here
	 * But ideally we should create a workqueue and process the control
	 * signals, especially when we start using GPIOs over slow
	 * buses like I2C.
	 */
	if (!gpio_is_valid(apb_data->ctrl.wake_detect) &&
			!gpio_is_valid(apb_data->ctrl.reset))
		return IRQ_HANDLED; /* Should it be IRQ_NONE ?? */

	spin_lock_irqsave(&apb_data->lock, flags);

	if (apb_data->state != APB_STATE_ACTIVE) {
		/* Bring bridge out of reset on this event */
		gpio_set_value(apb_data->ctrl.reset, 0);
		apb_data->state = APB_STATE_ACTIVE;
	} else {
		/*
		 * Assert Wake_OUT signal to APB
		 * It would resemble WakeDetect module's signal pass-through
		 */
		/*
		 * We have to generate the pulse, so we may need to schedule
		 * workqueue here.
		 *
		 * Also, since we are using both rising and falling edge for
		 * interrupt trigger, we may not need workqueue. Just pass
		 * through the value to bridge.
		 * Just read GPIO value and pass it to the bridge
		 */
	}

	spin_unlock_irqrestore(&apb_data->lock, flags);

	return IRQ_HANDLED;
}

static void apb_ctrl_cleanup(struct apb_ctrl_drvdata *apb_data)
{
	if (apb_data->vcore && regulator_is_enabled(apb_data->vcore) > 0)
		regulator_disable(apb_data->vcore);

	if (apb_data->vio && regulator_is_enabled(apb_data->vio) > 0)
		regulator_disable(apb_data->vio);

	/* As part of exit, put APB back in reset state */
	if (gpio_is_valid(apb_data->ctrl.reset))
		gpio_set_value(apb_data->ctrl.reset, 1);

	/* TODO: May have to send an event to SVC about this exit */
}

/*
 * Note: Please do not modify the below sequence, as it is as per the spec
 */
static int apb_ctrl_init_seq(struct device *dev,
		struct apb_ctrl_drvdata *apb_data)
{
	int ret;

	pinctrl_select_state(apb_data->pinctrl, apb_data->pin_default);

	/* Hold APB in reset state */
	ret = devm_gpio_request_one(dev, apb_data->ctrl.reset,
			GPIOF_OUT_INIT_LOW, "reset");
	if (ret) {
		dev_err(dev, "Failed requesting reset gpio %d\n",
				apb_data->ctrl.reset);
		return ret;
	}

	/* Enable power to APB */
	if (apb_data->vcore) {
		ret = regulator_enable(apb_data->vcore);
		if (ret) {
			dev_err(dev, "failed to enable core regulator\n");
			return ret;
		}
	}

	if (apb_data->vio) {
		ret = regulator_enable(apb_data->vio);
		if (ret) {
			dev_err(dev, "failed to enable IO regulator\n");
			return ret;
		}
	}

	/*
	 * We should be safe here to deassert boot retention signal, as
	 * we are only supporting cold boot as of now.
	 */
	ret = devm_gpio_request_one(dev, apb_data->ctrl.boot_ret,
			GPIOF_OUT_INIT_LOW, "boot retention");
	if (ret) {
		dev_err(dev, "Failed requesting reset gpio %d\n",
				apb_data->ctrl.boot_ret);
		return ret;
	}

	ret = devm_gpio_request(dev, apb_data->ctrl.wake_detect, "wake detect");
	if (ret)
		dev_err(dev, "Failed requesting wake_detect gpio %d\n",
				apb_data->ctrl.wake_detect);

	return ret;
}

static int apb_ctrl_get_devtree_data(struct device *dev,
		struct apb_ctrl_drvdata *apb_data)
{
	struct device_node *np = dev->of_node;

	/* fetch control signals */
	apb_data->ctrl.wake_detect = of_get_named_gpio(np, "wake-detect-gpios", 0);
	if (!gpio_is_valid(apb_data->ctrl.wake_detect)) {
		dev_err(dev, "failed to get wake detect gpio\n");
		return apb_data->ctrl.wake_detect;
	}

	apb_data->ctrl.reset = of_get_named_gpio(np, "reset-gpios", 0);
	if (!gpio_is_valid(apb_data->ctrl.reset)) {
		dev_err(dev, "failed to get reset gpio\n");
		return apb_data->ctrl.reset;
	}

	apb_data->ctrl.boot_ret = of_get_named_gpio(np, "boot-ret-gpios", 0);
	if (!gpio_is_valid(apb_data->ctrl.boot_ret)) {
		dev_err(dev, "failed to get boot retention gpio\n");
		return apb_data->ctrl.boot_ret;
	}

	/* It's not mandatory to support power management interface */
	apb_data->ctrl.pwroff = of_get_named_gpio(np, "pwr-off-gpios", 0);
	if (!gpio_is_valid(apb_data->ctrl.pwroff))
		dev_info(dev, "failed to get power off gpio\n");

	apb_data->ctrl.pwrdn = of_get_named_gpio(np, "pwr-down-gpios", 0);
	if (!gpio_is_valid(apb_data->ctrl.pwrdn))
		dev_info(dev, "failed to get power down gpio\n");

	/* Regulators are optional, as we may have fixed supply coming in */
	apb_data->vcore = devm_regulator_get(dev, "vcore");
	if (IS_ERR_OR_NULL(apb_data->vcore)) {
		dev_info(dev, "no core regulator found\n");
		apb_data->vcore = NULL;
	}

	apb_data->vio = devm_regulator_get(dev, "vio");
	if (IS_ERR_OR_NULL(apb_data->vio)) {
		dev_info(dev, "no IO regulator found\n");
		apb_data->vio = NULL;
	}

	apb_data->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(apb_data->pinctrl)) {
		dev_err(dev, "could not get pinctrl handle\n");
		return PTR_ERR(apb_data->pinctrl);
	}
	apb_data->pin_default = pinctrl_lookup_state(apb_data->pinctrl, "default");
	if (IS_ERR(apb_data->pin_default)) {
		dev_err(dev, "could not get default pin state\n");
		return PTR_ERR(apb_data->pin_default);
	}

	return 0;
}

static int apb_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct apb_ctrl_drvdata *apb_data;
	int ret;

	apb_data = devm_kzalloc(&pdev->dev, sizeof(*apb_data), GFP_KERNEL);
	if (!apb_data)
		return -ENOMEM;

	ret = apb_ctrl_get_devtree_data(dev, apb_data);
	if (ret) {
		dev_err(dev, "failed to get devicetree data %d\n", ret);
		return ret;
	}

	ret = apb_ctrl_init_seq(dev, apb_data);
	if (ret) {
		dev_err(dev, "failed to set init state of control signal %d\n",
				ret);
		goto exit;
	}

	platform_set_drvdata(pdev, apb_data);

	apb_data->state = APB_STATE_OFF;
	/*
	 * Assert AP module detect signal by pulling wake_detect low
	 */
	deassert_wake(apb_data->ctrl.wake_detect);

	/*
	 * In order to receive an interrupt, the GPIO must be set to input mode
	 *
	 * As per WDM spec, for the cold boot, the wake pulse must be
	 * >= 5000 usec, but at this stage it is power up sequence,
	 * so we always treat it as cold boot.
	 */
	gpio_direction_input(apb_data->ctrl.wake_detect);

	ret = devm_request_irq(dev, gpio_to_irq(apb_data->ctrl.wake_detect),
			apb_ctrl_wake_detect_irq,
			IRQF_TRIGGER_RISING,
			"wake detect", apb_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to request wake detect IRQ\n");
		goto exit;
	}

	/*
	 * Interrupt handling for WAKE_IN (from bridge) signal is required
	 *
	 * Assumption here is, AP already would have woken up and in the
	 * WAKE_IN/WAKE_FRAME event from bridge, as AP would pass-through event
	 * to SVC.
	 *
	 * Not sure anything else needs to take care here.
	 */
	dev_info(dev, "Device registered successfully \n");
	return 0;

exit:
	apb_ctrl_cleanup(apb_data);
	return ret;
}

static int apb_ctrl_remove(struct platform_device *pdev)
{
	struct apb_ctrl_drvdata *apb_data = platform_get_drvdata(pdev);

	if (apb_data)
		apb_ctrl_cleanup(apb_data);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int apb_ctrl_suspend(struct device *dev)
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

static int apb_ctrl_resume(struct device *dev)
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

static SIMPLE_DEV_PM_OPS(apb_ctrl_pm_ops, apb_ctrl_suspend, apb_ctrl_resume);

static struct of_device_id apb_ctrl_of_match[] = {
	{ .compatible = "usbffff,2", },
	{ },
};
MODULE_DEVICE_TABLE(of, apb_ctrl_of_match);

static struct platform_driver apb_ctrl_device_driver = {
	.probe		= apb_ctrl_probe,
	.remove		= apb_ctrl_remove,
	.driver		= {
		.name	= "unipro-APbridge",
		.pm	= &apb_ctrl_pm_ops,
		.of_match_table = of_match_ptr(apb_ctrl_of_match),
	}
};

module_platform_driver(apb_ctrl_device_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vaibhav Hiremath <vaibhav.hiremath@linaro.org>");
MODULE_DESCRIPTION("AP Bridge Control Driver for DB3 platform");
