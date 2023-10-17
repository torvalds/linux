// SPDX-License-Identifier: GPL-2.0+
/*
 * Loongson-2 PM Support
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/pm_wakeirq.h>
#include <linux/platform_device.h>
#include <asm/bootinfo.h>
#include <asm/suspend.h>

#define LOONGSON2_PM1_CNT_REG		0x14
#define LOONGSON2_PM1_STS_REG		0x0c
#define LOONGSON2_PM1_ENA_REG		0x10
#define LOONGSON2_GPE0_STS_REG		0x28
#define LOONGSON2_GPE0_ENA_REG		0x2c

#define LOONGSON2_PM1_PWRBTN_STS	BIT(8)
#define LOONGSON2_PM1_PCIEXP_WAKE_STS	BIT(14)
#define LOONGSON2_PM1_WAKE_STS		BIT(15)
#define LOONGSON2_PM1_CNT_INT_EN	BIT(0)
#define LOONGSON2_PM1_PWRBTN_EN		LOONGSON2_PM1_PWRBTN_STS

static struct loongson2_pm {
	void __iomem			*base;
	struct input_dev		*dev;
	bool				suspended;
} loongson2_pm;

#define loongson2_pm_readw(reg)		readw(loongson2_pm.base + reg)
#define loongson2_pm_readl(reg)		readl(loongson2_pm.base + reg)
#define loongson2_pm_writew(val, reg)	writew(val, loongson2_pm.base + reg)
#define loongson2_pm_writel(val, reg)	writel(val, loongson2_pm.base + reg)

static void loongson2_pm_status_clear(void)
{
	u16 value;

	value = loongson2_pm_readw(LOONGSON2_PM1_STS_REG);
	value |= (LOONGSON2_PM1_PWRBTN_STS | LOONGSON2_PM1_PCIEXP_WAKE_STS |
		  LOONGSON2_PM1_WAKE_STS);
	loongson2_pm_writew(value, LOONGSON2_PM1_STS_REG);
	loongson2_pm_writel(loongson2_pm_readl(LOONGSON2_GPE0_STS_REG), LOONGSON2_GPE0_STS_REG);
}

static void loongson2_pm_irq_enable(void)
{
	u16 value;

	value = loongson2_pm_readw(LOONGSON2_PM1_CNT_REG);
	value |= LOONGSON2_PM1_CNT_INT_EN;
	loongson2_pm_writew(value, LOONGSON2_PM1_CNT_REG);

	value = loongson2_pm_readw(LOONGSON2_PM1_ENA_REG);
	value |= LOONGSON2_PM1_PWRBTN_EN;
	loongson2_pm_writew(value, LOONGSON2_PM1_ENA_REG);
}

static int loongson2_suspend_enter(suspend_state_t state)
{
	loongson2_pm_status_clear();
	loongarch_common_suspend();
	loongarch_suspend_enter();
	loongarch_common_resume();
	loongson2_pm_irq_enable();
	pm_set_resume_via_firmware();

	return 0;
}

static int loongson2_suspend_begin(suspend_state_t state)
{
	pm_set_suspend_via_firmware();

	return 0;
}

static int loongson2_suspend_valid_state(suspend_state_t state)
{
	return (state == PM_SUSPEND_MEM);
}

static const struct platform_suspend_ops loongson2_suspend_ops = {
	.valid	= loongson2_suspend_valid_state,
	.begin	= loongson2_suspend_begin,
	.enter	= loongson2_suspend_enter,
};

static int loongson2_power_button_init(struct device *dev, int irq)
{
	int ret;
	struct input_dev *button;

	button = input_allocate_device();
	if (!dev)
		return -ENOMEM;

	button->name = "Power Button";
	button->phys = "pm/button/input0";
	button->id.bustype = BUS_HOST;
	button->dev.parent = NULL;
	input_set_capability(button, EV_KEY, KEY_POWER);

	ret = input_register_device(button);
	if (ret)
		goto free_dev;

	dev_pm_set_wake_irq(&button->dev, irq);
	device_set_wakeup_capable(&button->dev, true);
	device_set_wakeup_enable(&button->dev, true);

	loongson2_pm.dev = button;
	dev_info(dev, "Power Button: Init successful!\n");

	return 0;

free_dev:
	input_free_device(button);

	return ret;
}

static irqreturn_t loongson2_pm_irq_handler(int irq, void *dev_id)
{
	u16 status = loongson2_pm_readw(LOONGSON2_PM1_STS_REG);

	if (!loongson2_pm.suspended && (status & LOONGSON2_PM1_PWRBTN_STS)) {
		pr_info("Power Button pressed...\n");
		input_report_key(loongson2_pm.dev, KEY_POWER, 1);
		input_sync(loongson2_pm.dev);
		input_report_key(loongson2_pm.dev, KEY_POWER, 0);
		input_sync(loongson2_pm.dev);
	}

	loongson2_pm_status_clear();

	return IRQ_HANDLED;
}

static int __maybe_unused loongson2_pm_suspend(struct device *dev)
{
	loongson2_pm.suspended = true;

	return 0;
}

static int __maybe_unused loongson2_pm_resume(struct device *dev)
{
	loongson2_pm.suspended = false;

	return 0;
}
static SIMPLE_DEV_PM_OPS(loongson2_pm_ops, loongson2_pm_suspend, loongson2_pm_resume);

static int loongson2_pm_probe(struct platform_device *pdev)
{
	int irq, retval;
	u64 suspend_addr;
	struct device *dev = &pdev->dev;

	loongson2_pm.base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(loongson2_pm.base))
		return PTR_ERR(loongson2_pm.base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (!device_property_read_u64(dev, "loongson,suspend-address", &suspend_addr))
		loongson_sysconf.suspend_addr = (u64)phys_to_virt(suspend_addr);
	else
		dev_err(dev, "No loongson,suspend-address, could not support S3!\n");

	if (loongson2_power_button_init(dev, irq))
		return -EINVAL;

	retval = devm_request_irq(&pdev->dev, irq, loongson2_pm_irq_handler,
				  IRQF_SHARED, "pm_irq", &loongson2_pm);
	if (retval)
		return retval;

	loongson2_pm_irq_enable();
	loongson2_pm_status_clear();

	if (loongson_sysconf.suspend_addr)
		suspend_set_ops(&loongson2_suspend_ops);

	/* Populate children */
	retval = devm_of_platform_populate(dev);
	if (retval)
		dev_err(dev, "Error populating children, reboot and poweroff might not work properly\n");

	return 0;
}

static const struct of_device_id loongson2_pm_match[] = {
	{ .compatible = "loongson,ls2k0500-pmc", },
	{},
};

static struct platform_driver loongson2_pm_driver = {
	.driver = {
		.name = "ls2k-pm",
		.pm = &loongson2_pm_ops,
		.of_match_table = loongson2_pm_match,
	},
	.probe = loongson2_pm_probe,
};
module_platform_driver(loongson2_pm_driver);

MODULE_DESCRIPTION("Loongson-2 PM driver");
MODULE_LICENSE("GPL");
