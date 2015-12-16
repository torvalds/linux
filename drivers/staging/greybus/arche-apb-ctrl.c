/*
 * Arche Platform driver to control APB.
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

enum apb_state {
	APB_STATE_OFF,
	APB_STATE_ACTIVE,
	APB_STATE_STANDBY,
};

struct arche_apb_ctrl_drvdata {
	/* Control GPIO signals to and from AP <=> AP Bridges */
	int wake_detect_gpio; /* bi-dir,maps to WAKE_MOD & WAKE_FRAME signals */
	int resetn_gpio;
	int boot_ret_gpio;
	int pwroff_gpio;
	int wake_in_gpio;
	int wake_out_gpio;
	int pwrdn_gpio;

	unsigned int wake_detect_irq;
	enum apb_state state;

	struct regulator *vcore;
	struct regulator *vio;

	unsigned int clk_en_gpio;
	struct clk *clk;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;

	/* To protect concurrent access of GPIO registers, need protection */
	spinlock_t lock;
};

/*
 * Note that these low level api's are active high
 */
static inline void assert_gpio(unsigned int gpio)
{
	gpio_set_value(gpio, 1);
	msleep(500);
}

static inline void deassert_gpio(unsigned int gpio)
{
	gpio_set_value(gpio, 0);
}

static irqreturn_t apb_ctrl_wake_detect_irq(int irq, void *devid)
{
	struct arche_apb_ctrl_drvdata *apb = devid;
	unsigned long flags;

	/*
	 * TODO:
	 * Since currently SoC GPIOs are being used we are safe here
	 * But ideally we should create a workqueue and process the control
	 * signals, especially when we start using GPIOs over slow
	 * buses like I2C.
	 */
	if (!gpio_is_valid(apb->wake_detect_gpio) &&
			!gpio_is_valid(apb->resetn_gpio))
		return IRQ_HANDLED; /* Should it be IRQ_NONE ?? */

	spin_lock_irqsave(&apb->lock, flags);

	if (apb->state != APB_STATE_ACTIVE) {
		/* Bring bridge out of reset on this event */
		gpio_set_value(apb->resetn_gpio, 1);
		apb->state = APB_STATE_ACTIVE;
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

	spin_unlock_irqrestore(&apb->lock, flags);

	return IRQ_HANDLED;
}

/*
 * Note: Please do not modify the below sequence, as it is as per the spec
 */
static int apb_ctrl_init_seq(struct platform_device *pdev,
		struct arche_apb_ctrl_drvdata *apb)
{
	struct device *dev = &pdev->dev;
	int ret;

	/* On DB3 clock was not mandatory */
	if (gpio_is_valid(apb->clk_en_gpio)) {
		ret = devm_gpio_request(dev, apb->clk_en_gpio, "apb_clk_en");
		if (ret)
			dev_err(dev, "Failed requesting APB clock en gpio %d\n",
					apb->clk_en_gpio);
		ret = gpio_direction_output(apb->clk_en_gpio, 1);
		if (ret)
			dev_err(dev, "failed to set APB clock en gpio dir:%d\n", ret);
	}
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
	if (apb->vcore) {
		ret = regulator_enable(apb->vcore);
		if (ret) {
			dev_err(dev, "failed to enable core regulator\n");
			return ret;
		}
	}
	if (apb->vio) {
		ret = regulator_enable(apb->vio);
		if (ret) {
			dev_err(dev, "failed to enable IO regulator\n");
			return ret;
		}
	}

	ret = devm_gpio_request_one(dev, apb->boot_ret_gpio,
			GPIOF_OUT_INIT_LOW, "boot retention");
	if (ret) {
		dev_err(dev, "Failed requesting bootret gpio %d\n",
				apb->boot_ret_gpio);
		return ret;
	}
	gpio_set_value(apb->boot_ret_gpio, 0);
	udelay(50);

	ret = devm_gpio_request(dev, apb->wake_detect_gpio, "wake detect");
	if (ret)
		dev_err(dev, "Failed requesting wake_detect gpio %d\n",
				apb->wake_detect_gpio);

	return ret;
}

static int apb_ctrl_get_devtree_data(struct platform_device *pdev,
		struct arche_apb_ctrl_drvdata *apb)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	apb->wake_detect_gpio = of_get_named_gpio(np, "wake-detect-gpios", 0);
	if (!gpio_is_valid(apb->wake_detect_gpio)) {
		dev_err(dev, "failed to get wake detect gpio\n");
		return apb->wake_detect_gpio;
	}

	apb->resetn_gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (!gpio_is_valid(apb->resetn_gpio)) {
		dev_err(dev, "failed to get reset gpio\n");
		return apb->resetn_gpio;
	}

	apb->boot_ret_gpio = of_get_named_gpio(np, "boot-ret-gpios", 0);
	if (!gpio_is_valid(apb->boot_ret_gpio)) {
		dev_err(dev, "failed to get boot retention gpio\n");
		return apb->boot_ret_gpio;
	}

	/* It's not mandatory to support power management interface */
	apb->pwroff_gpio = of_get_named_gpio(np, "pwr-off-gpios", 0);
	if (!gpio_is_valid(apb->pwroff_gpio)) {
		dev_info(dev, "failed to get power off gpio\n");
		return apb->pwroff_gpio;
	}

	/* Do not make clock mandatory as of now (for DB3) */
	apb->clk_en_gpio = of_get_named_gpio(np, "clock-en-gpio", 0);
	if (!gpio_is_valid(apb->clk_en_gpio))
		dev_err(dev, "failed to get clock en gpio\n");

	apb->pwrdn_gpio = of_get_named_gpio(np, "pwr-down-gpios", 0);
	if (!gpio_is_valid(apb->pwrdn_gpio))
		dev_info(dev, "failed to get power down gpio\n");

	/* Regulators are optional, as we may have fixed supply coming in */
	apb->vcore = devm_regulator_get(dev, "vcore");
	if (IS_ERR_OR_NULL(apb->vcore)) {
		dev_info(dev, "no core regulator found\n");
		apb->vcore = NULL;
	}

	apb->vio = devm_regulator_get(dev, "vio");
	if (IS_ERR_OR_NULL(apb->vio)) {
		dev_info(dev, "no IO regulator found\n");
		apb->vio = NULL;
	}

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
	unsigned long flags;

	if (apb->vcore && regulator_is_enabled(apb->vcore) > 0)
		regulator_disable(apb->vcore);

	if (apb->vio && regulator_is_enabled(apb->vio) > 0)
		regulator_disable(apb->vio);

	spin_lock_irqsave(&apb->lock, flags);
	/* As part of exit, put APB back in reset state */
	if (gpio_is_valid(apb->resetn_gpio))
		gpio_set_value(apb->resetn_gpio, 0);

	apb->state = APB_STATE_OFF;
	spin_unlock_irqrestore(&apb->lock, flags);

	/* TODO: May have to send an event to SVC about this exit */
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

	ret = apb_ctrl_init_seq(pdev, apb);
	if (ret) {
		dev_err(dev, "failed to set init state of control signal %d\n",
				ret);
		goto exit;
	}

	spin_lock_init(&apb->lock);

	apb->state = APB_STATE_OFF;
	/*
	 * Assert AP module detect signal by pulling wake_detect low
	 */
	assert_gpio(apb->wake_detect_gpio);

	/*
	 * In order to receive an interrupt, the GPIO must be set to input mode
	 */
	gpio_direction_input(apb->wake_detect_gpio);

	ret = devm_request_irq(dev, gpio_to_irq(apb->wake_detect_gpio),
			apb_ctrl_wake_detect_irq, IRQF_TRIGGER_FALLING,
			"wake detect", apb);
	if (ret) {
		dev_err(dev, "failed to request wake detect IRQ\n");
		goto exit;
	}

	platform_set_drvdata(pdev, apb);

	assert_gpio(apb->resetn_gpio);

	dev_info(&pdev->dev, "Device registered successfully\n");
	return 0;

exit:
	apb_ctrl_cleanup(apb);
	return ret;
}

static int arche_apb_ctrl_remove(struct platform_device *pdev)
{
	struct arche_apb_ctrl_drvdata *apb = platform_get_drvdata(pdev);

	if (apb)
		apb_ctrl_cleanup(apb);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int arche_apb_ctrl_suspend(struct device *dev)
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

static SIMPLE_DEV_PM_OPS(arche_apb_ctrl_pm_ops,
			arche_apb_ctrl_suspend,
			arche_apb_ctrl_resume);

static struct of_device_id arche_apb_ctrl_of_match[] = {
	{ .compatible = "usbffff,2", },
	{ },
};
MODULE_DEVICE_TABLE(of, arche_apb_ctrl_of_match);

static struct platform_driver arche_apb_ctrl_device_driver = {
	.probe		= arche_apb_ctrl_probe,
	.remove		= arche_apb_ctrl_remove,
	.driver		= {
		.name	= "arche-apb-ctrl",
		.pm	= &arche_apb_ctrl_pm_ops,
		.of_match_table = of_match_ptr(arche_apb_ctrl_of_match),
	}
};

module_platform_driver(arche_apb_ctrl_device_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vaibhav Hiremath <vaibhav.hiremath@linaro.org>");
MODULE_DESCRIPTION("Arche APB control Driver");
