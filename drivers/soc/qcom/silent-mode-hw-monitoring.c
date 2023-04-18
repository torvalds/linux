// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>
#include <linux/pm.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/kernel.h>

#include <asm/arch_timer.h>
#include <asm/div64.h>

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/silent_mode.h>


/**
 * struct silent_mode_data : silent mode context data
 * @gpio_number: GPIO Instance for silent mode monitoring
 * @irq_number: IRQ Number assigned dynamically for silent mode
 * @pdev: platform device from device/driver call
 * @pm_nb: power manager notification block
 *
 * Silent mode context data holding all the information needed
 * by the driver for device/driver interactions
 */
struct silent_mode_info {
	int sgpio;
	int sirq;
	bool active_low;
	struct platform_device *pdev;
	struct gpio_desc *gpiod;
	struct notifier_block pm_nb;
	struct delayed_work work;
};

static irqreturn_t silent_mode_irq(int irq, void *priv)
{
	struct silent_mode_info *info = priv;

	mod_delayed_work(system_wq, &info->work, msecs_to_jiffies(200));

	return IRQ_HANDLED;
}

static void silent_mode_handler_work_func(struct work_struct *work)
{
	int irq_trig;
	struct silent_mode_info *info =
			container_of(work, struct silent_mode_info, work.work);

	irq_trig = gpiod_get_value(info->gpiod);
	dev_info(&info->pdev->dev, "Irq_trig %d\n", irq_trig);
	pm_silentmode_update(irq_trig, NULL, 0);
}

/**
 * silent_mode_init_monitor() - Initialize the Silent Mode monitoring function
 * @info: pointer to silent_mode_info structure
 *
 * This function returns 0 if it succeeds. If an error occurs an
 * negative number is returned.
 *
 * This function will:
 *  - Request GPIO
 *  - Set the GPIO direction as input
 *  - Request a dual-edge  GPIO interrupt
 *
 *  Return:
 *  - 0: if Init passes
 *  - ERRNO: if Init fails
 */

static int silent_mode_init_monitor(struct silent_mode_info *info)
{
	int result = -EINVAL;

	dev_info(&info->pdev->dev, "GPIO for Silent boot: %d\n",
		info->sgpio);

	if (!gpio_is_valid(info->sgpio)) {
		dev_info(&info->pdev->dev, "Invalid gpio Request %d\n",
			info->sgpio);
		return result;
	}

	info->gpiod = gpio_to_desc(info->sgpio);
	info->active_low = gpiod_is_active_low(info->gpiod);
	info->sirq = gpiod_to_irq(info->gpiod);
	result = gpiod_set_debounce(info->gpiod, 15000);
	if (result < 0) {
		dev_err(&info->pdev->dev, "%s: GPIO debounce ret %d\n",
			__func__, result);
		return result;
	}

	INIT_DELAYED_WORK(&info->work, silent_mode_handler_work_func);

	result = devm_request_any_context_irq(&info->pdev->dev,
				info->sirq,
				silent_mode_irq,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"silent_boot_GPIO_handler",
				info);

	if (result < 0)
		dev_err(&info->pdev->dev, "IRQ request %d\n", result);

	return result;

}

static int silent_mode_enable_monitor(struct silent_mode_info *info)
{
	int result = 0;

	dev_info(&info->pdev->dev, "silent_mode:%s\n", __func__);
	if (info != NULL) {
		result = gpio_get_value(info->sgpio);
		if (result != 0)
			enable_irq(info->sirq);
		else
			return -EEXIST;
	}
	return 0;
}

static int silent_mode_disable_monitor(struct silent_mode_info *info)
{
	dev_info(&info->pdev->dev, "silent_mode:%s\n", __func__);
	if (info != NULL)
		disable_irq(info->sirq);

	return 0;
}

static int forced_mode_enforced(void)
{
	if (pm_silentmode_get_mode() < 0)
		return 1;

	return 0;
}


static int silent_mode_probe(struct platform_device *pdev)
{
	int result = 0;
	unsigned int flags = GPIOF_IN;
	struct silent_mode_info *info;

	if  (forced_mode_enforced()) {
		dev_info(&pdev->dev, "forced_mode: No HW State monitoring\n");
		return result;
	}

	dev_info(&pdev->dev, "MODE_SILENT/MODE_NON_SILENT\n");

	info = devm_kzalloc(&pdev->dev,
			sizeof(struct silent_mode_info),
			GFP_KERNEL);
	if (!info) {
		dev_info(&pdev->dev, "%s: FAILED: cannot alloc info data\n",
			__func__);
		result = -ENOMEM;
		return result;
	}

	info->pdev = pdev;
	platform_set_drvdata(pdev, info);

	info->sgpio = of_get_named_gpio(pdev->dev.of_node,
					"qcom,silent-boot-gpio",
					0);

	result = devm_gpio_request_one(&pdev->dev,
					info->sgpio,
					flags,
					"qti-silent-mode");
	if (result < 0) {
		dev_err(&pdev->dev, "Failed to request GPIO %d, error %d\n",
			info->sgpio, result);
		result = -EINVAL;
	}

	result = silent_mode_init_monitor(info);
	if (result < 0) {
		dev_err(&pdev->dev, "Failed Init: %d\n", result);
		result = -EINVAL;
	}

	return result;
}

#ifdef CONFIG_PM
static int silent_mode_suspend_late(struct device *dev)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct silent_mode_info *info = platform_get_drvdata(pdev);

	ret = silent_mode_disable_monitor(info);
	dev_info(&info->pdev->dev, "silent_mode:%s\n", __func__);
	return ret;
}

static int silent_mode_resume_early(struct device *dev)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct silent_mode_info *info = platform_get_drvdata(pdev);

	ret = silent_mode_enable_monitor(info);
	dev_info(&info->pdev->dev, "silent_mode:%s\n", __func__);
	return ret;
}

static const struct dev_pm_ops silent_mode_pm_ops = {
	.suspend_late = silent_mode_suspend_late,
	.resume_early = silent_mode_resume_early,
};
#endif

static int silent_mode_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "silent_mode: Entered %s\n", __func__);
	return 0;
}

static const struct of_device_id silent_mode_match_table[] = {
	{.compatible = "qcom,silent-mode" },
	{ }
};

static struct platform_driver silent_mode_driver = {
	.probe  = silent_mode_probe,
	.remove = silent_mode_remove,
	.driver = {
		.name = "qcom-silent-monitoring-mode",
		.of_match_table = silent_mode_match_table,
#ifdef CONFIG_PM
		.pm = &silent_mode_pm_ops,
#endif
	},
};

module_platform_driver(silent_mode_driver);

MODULE_DESCRIPTION("Silent Mode Monitoring Driver");
MODULE_LICENSE("GPL");
