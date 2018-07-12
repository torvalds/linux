// SPDX-License-Identifier: GPL-2.0
/*
 * Gemini power management controller
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Inspired by code from the SL3516 board support by Jason Lee
 * Inspired by code from Janos Laube <janos.dev@gmail.com>
 */
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/reboot.h>

#define GEMINI_PWC_ID		0x00010500
#define	GEMINI_PWC_IDREG	0x00
#define	GEMINI_PWC_CTRLREG	0x04
#define	GEMINI_PWC_STATREG	0x08

#define GEMINI_CTRL_SHUTDOWN	BIT(0)
#define GEMINI_CTRL_ENABLE	BIT(1)
#define GEMINI_CTRL_IRQ_CLR	BIT(2)

#define GEMINI_STAT_CIR		BIT(4)
#define	GEMINI_STAT_RTC		BIT(5)
#define	GEMINI_STAT_POWERBUTTON	BIT(6)

struct gemini_powercon {
        struct device           *dev;
        void __iomem            *base;
};

static irqreturn_t gemini_powerbutton_interrupt(int irq, void *data)
{
	struct gemini_powercon *gpw = data;
	u32 val;

	/* ACK the IRQ */
	val = readl(gpw->base + GEMINI_PWC_CTRLREG);
	val |= GEMINI_CTRL_IRQ_CLR;
	writel(val, gpw->base + GEMINI_PWC_CTRLREG);

	val = readl(gpw->base + GEMINI_PWC_STATREG);
	val &= 0x70U;
	switch (val) {
	case GEMINI_STAT_CIR:
		/*
		 * We do not yet have a driver for the infrared
		 * controller so it can cause spurious poweroff
		 * events. Ignore those for now.
		 */
		dev_info(gpw->dev, "infrared poweroff - ignored\n");
		break;
	case GEMINI_STAT_RTC:
		dev_info(gpw->dev, "RTC poweroff\n");
		orderly_poweroff(true);
		break;
	case GEMINI_STAT_POWERBUTTON:
		dev_info(gpw->dev, "poweroff button pressed\n");
		orderly_poweroff(true);
		break;
	default:
		dev_info(gpw->dev, "other power management IRQ\n");
		break;
	}

	return IRQ_HANDLED;
}

/* This callback needs this static local as it has void as argument */
static struct gemini_powercon *gpw_poweroff;

static void gemini_poweroff(void)
{
	struct gemini_powercon *gpw = gpw_poweroff;
	u32 val;

	dev_crit(gpw->dev, "Gemini power off\n");
	val = readl(gpw->base + GEMINI_PWC_CTRLREG);
	val |= GEMINI_CTRL_ENABLE | GEMINI_CTRL_IRQ_CLR;
	writel(val, gpw->base + GEMINI_PWC_CTRLREG);

	val &= ~GEMINI_CTRL_ENABLE;
	val |= GEMINI_CTRL_SHUTDOWN;
	writel(val, gpw->base + GEMINI_PWC_CTRLREG);
}

static int gemini_poweroff_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct gemini_powercon *gpw;
	u32 val;
	int irq;
	int ret;

	gpw = devm_kzalloc(dev, sizeof(*gpw), GFP_KERNEL);
	if (!gpw)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpw->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(gpw->base))
		return PTR_ERR(gpw->base);

	irq = platform_get_irq(pdev, 0);
	if (!irq)
		return -EINVAL;

	gpw->dev = dev;

	val = readl(gpw->base + GEMINI_PWC_IDREG);
	val &= 0xFFFFFF00U;
	if (val != GEMINI_PWC_ID) {
		dev_err(dev, "wrong power controller ID: %08x\n",
			val);
		return -ENODEV;
	}

	/*
	 * Enable the power controller. This is crucial on Gemini
	 * systems: if this is not done, pressing the power button
	 * will result in unconditional poweroff without any warning.
	 * This makes the kernel handle the poweroff.
	 */
	val = readl(gpw->base + GEMINI_PWC_CTRLREG);
	val |= GEMINI_CTRL_ENABLE;
	writel(val, gpw->base + GEMINI_PWC_CTRLREG);

	/* Now that the state machine is active, clear the IRQ */
	val = readl(gpw->base + GEMINI_PWC_CTRLREG);
	val |= GEMINI_CTRL_IRQ_CLR;
	writel(val, gpw->base + GEMINI_PWC_CTRLREG);

	ret = devm_request_irq(dev, irq, gemini_powerbutton_interrupt, 0,
			       "poweroff", gpw);
	if (ret)
		return ret;

	pm_power_off = gemini_poweroff;
	gpw_poweroff = gpw;

	dev_info(dev, "Gemini poweroff driver registered\n");

	return 0;
}

static const struct of_device_id gemini_poweroff_of_match[] = {
	{
		.compatible = "cortina,gemini-power-controller",
	},
	{}
};

static struct platform_driver gemini_poweroff_driver = {
	.probe = gemini_poweroff_probe,
	.driver = {
		.name = "gemini-poweroff",
		.of_match_table = gemini_poweroff_of_match,
	},
};
builtin_platform_driver(gemini_poweroff_driver);
