/*
 * Power key driver for SiRF PrimaII
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/of.h>

struct sirfsoc_pwrc_drvdata {
	u32			pwrc_base;
	struct input_dev	*input;
};

#define PWRC_ON_KEY_BIT			(1 << 0)

#define PWRC_INT_STATUS			0xc
#define PWRC_INT_MASK			0x10

static irqreturn_t sirfsoc_pwrc_isr(int irq, void *dev_id)
{
	struct sirfsoc_pwrc_drvdata *pwrcdrv = dev_id;
	u32 int_status;

	int_status = sirfsoc_rtc_iobrg_readl(pwrcdrv->pwrc_base +
							PWRC_INT_STATUS);
	sirfsoc_rtc_iobrg_writel(int_status & ~PWRC_ON_KEY_BIT,
				 pwrcdrv->pwrc_base + PWRC_INT_STATUS);

	/*
	 * For a typical Linux system, we report KEY_SUSPEND to trigger apm-power.c
	 * to queue a SUSPEND APM event
	 */
	input_event(pwrcdrv->input, EV_PWR, KEY_SUSPEND, 1);
	input_sync(pwrcdrv->input);

	/*
	 * Todo: report KEY_POWER event for Android platforms, Android PowerManager
	 * will handle the suspend and powerdown/hibernation
	 */

	return IRQ_HANDLED;
}

static const struct of_device_id sirfsoc_pwrc_of_match[] = {
	{ .compatible = "sirf,prima2-pwrc" },
	{},
}
MODULE_DEVICE_TABLE(of, sirfsoc_pwrc_of_match);

static int sirfsoc_pwrc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sirfsoc_pwrc_drvdata *pwrcdrv;
	int irq;
	int error;

	pwrcdrv = devm_kzalloc(&pdev->dev, sizeof(struct sirfsoc_pwrc_drvdata),
			       GFP_KERNEL);
	if (!pwrcdrv) {
		dev_info(&pdev->dev, "Not enough memory for the device data\n");
		return -ENOMEM;
	}

	/*
	 * we can't use of_iomap because pwrc is not mapped in memory,
	 * the so-called base address is only offset in rtciobrg
	 */
	error = of_property_read_u32(np, "reg", &pwrcdrv->pwrc_base);
	if (error) {
		dev_err(&pdev->dev,
			"unable to find base address of pwrc node in dtb\n");
		return error;
	}

	pwrcdrv->input = devm_input_allocate_device(&pdev->dev);
	if (!pwrcdrv->input)
		return -ENOMEM;

	pwrcdrv->input->name = "sirfsoc pwrckey";
	pwrcdrv->input->phys = "pwrc/input0";
	pwrcdrv->input->evbit[0] = BIT_MASK(EV_PWR);

	irq = platform_get_irq(pdev, 0);
	error = devm_request_irq(&pdev->dev, irq,
				 sirfsoc_pwrc_isr, IRQF_SHARED,
				 "sirfsoc_pwrc_int", pwrcdrv);
	if (error) {
		dev_err(&pdev->dev, "unable to claim irq %d, error: %d\n",
			irq, error);
		return error;
	}

	sirfsoc_rtc_iobrg_writel(
		sirfsoc_rtc_iobrg_readl(pwrcdrv->pwrc_base + PWRC_INT_MASK) |
			PWRC_ON_KEY_BIT,
		pwrcdrv->pwrc_base + PWRC_INT_MASK);

	error = input_register_device(pwrcdrv->input);
	if (error) {
		dev_err(&pdev->dev,
			"unable to register input device, error: %d\n",
			error);
		return error;
	}

	platform_set_drvdata(pdev, pwrcdrv);
	device_init_wakeup(&pdev->dev, 1);

	return 0;
}

static int sirfsoc_pwrc_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pwrc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pwrc_drvdata *pwrcdrv = platform_get_drvdata(pdev);

	/*
	 * Do not mask pwrc interrupt as we want pwrc work as a wakeup source
	 * if users touch X_ONKEY_B, see arch/arm/mach-prima2/pm.c
	 */
	sirfsoc_rtc_iobrg_writel(
		sirfsoc_rtc_iobrg_readl(
		pwrcdrv->pwrc_base + PWRC_INT_MASK) | PWRC_ON_KEY_BIT,
		pwrcdrv->pwrc_base + PWRC_INT_MASK);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sirfsoc_pwrc_pm_ops, NULL, pwrc_resume);

static struct platform_driver sirfsoc_pwrc_driver = {
	.probe		= sirfsoc_pwrc_probe,
	.remove		= sirfsoc_pwrc_remove,
	.driver		= {
		.name	= "sirfsoc-pwrc",
		.owner	= THIS_MODULE,
		.pm	= &sirfsoc_pwrc_pm_ops,
		.of_match_table = sirfsoc_pwrc_of_match,
	}
};

module_platform_driver(sirfsoc_pwrc_driver);

MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("Binghua Duan <Binghua.Duan@csr.com>, Xianglong Du <Xianglong.Du@csr.com>");
MODULE_DESCRIPTION("CSR Prima2 PWRC Driver");
MODULE_ALIAS("platform:sirfsoc-pwrc");
