// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

#define PON_INT_RT_STS			0x10
#define PMIC_WD_BARK_STS_BIT		BIT(6)

#define PON_PMIC_WD_RESET_S1_TIMER	0x54
#define PON_PMIC_WD_RESET_S2_TIMER	0x55

#define PON_PMIC_WD_RESET_S2_CTL	0x56
#define RESET_TYPE_WARM			0x01
#define RESET_TYPE_SHUTDOWN		0x04
#define RESET_TYPE_HARD			0x07

#define PON_PMIC_WD_RESET_S2_CTL2	0x57
#define S2_RESET_EN_BIT			BIT(7)

#define PON_PMIC_WD_RESET_PET		0x58
#define WATCHDOG_PET_BIT		BIT(0)

#define PM8916_WDT_DEFAULT_TIMEOUT	32
#define PM8916_WDT_MIN_TIMEOUT		1
#define PM8916_WDT_MAX_TIMEOUT		127

struct pm8916_wdt {
	struct regmap *regmap;
	struct watchdog_device wdev;
	u32 baseaddr;
};

static int pm8916_wdt_start(struct watchdog_device *wdev)
{
	struct pm8916_wdt *wdt = watchdog_get_drvdata(wdev);

	return regmap_update_bits(wdt->regmap,
				  wdt->baseaddr + PON_PMIC_WD_RESET_S2_CTL2,
				  S2_RESET_EN_BIT, S2_RESET_EN_BIT);
}

static int pm8916_wdt_stop(struct watchdog_device *wdev)
{
	struct pm8916_wdt *wdt = watchdog_get_drvdata(wdev);

	return regmap_update_bits(wdt->regmap,
				  wdt->baseaddr + PON_PMIC_WD_RESET_S2_CTL2,
				  S2_RESET_EN_BIT, 0);
}

static int pm8916_wdt_ping(struct watchdog_device *wdev)
{
	struct pm8916_wdt *wdt = watchdog_get_drvdata(wdev);

	return regmap_update_bits(wdt->regmap,
				  wdt->baseaddr + PON_PMIC_WD_RESET_PET,
				  WATCHDOG_PET_BIT, WATCHDOG_PET_BIT);
}

static int pm8916_wdt_configure_timers(struct watchdog_device *wdev)
{
	struct pm8916_wdt *wdt = watchdog_get_drvdata(wdev);
	int err;

	err = regmap_write(wdt->regmap,
			   wdt->baseaddr + PON_PMIC_WD_RESET_S1_TIMER,
			   wdev->timeout - wdev->pretimeout);
	if (err)
		return err;

	return regmap_write(wdt->regmap,
			    wdt->baseaddr + PON_PMIC_WD_RESET_S2_TIMER,
			    wdev->pretimeout);
}

static int pm8916_wdt_set_timeout(struct watchdog_device *wdev,
				  unsigned int timeout)
{
	wdev->timeout = timeout;

	return pm8916_wdt_configure_timers(wdev);
}

static int pm8916_wdt_set_pretimeout(struct watchdog_device *wdev,
				     unsigned int pretimeout)
{
	wdev->pretimeout = pretimeout;

	return pm8916_wdt_configure_timers(wdev);
}

static irqreturn_t pm8916_wdt_isr(int irq, void *arg)
{
	struct pm8916_wdt *wdt = arg;
	int err, sts;

	err = regmap_read(wdt->regmap, wdt->baseaddr + PON_INT_RT_STS, &sts);
	if (err)
		return IRQ_HANDLED;

	if (sts & PMIC_WD_BARK_STS_BIT)
		watchdog_notify_pretimeout(&wdt->wdev);

	return IRQ_HANDLED;
}

static const struct watchdog_info pm8916_wdt_ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "QCOM PM8916 PON WDT",
};

static const struct watchdog_info pm8916_wdt_pt_ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE |
		   WDIOF_PRETIMEOUT,
	.identity = "QCOM PM8916 PON WDT",
};

static const struct watchdog_ops pm8916_wdt_ops = {
	.owner = THIS_MODULE,
	.start = pm8916_wdt_start,
	.stop = pm8916_wdt_stop,
	.ping = pm8916_wdt_ping,
	.set_timeout = pm8916_wdt_set_timeout,
	.set_pretimeout = pm8916_wdt_set_pretimeout,
};

static int pm8916_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pm8916_wdt *wdt;
	struct device *parent;
	int err, irq;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	parent = dev->parent;

	/*
	 * The pm8916-pon-wdt is a child of the pon device, which is a child
	 * of the pm8916 mfd device. We want access to the pm8916 registers.
	 * Retrieve regmap from pm8916 (parent->parent) and base address
	 * from pm8916-pon (pon).
	 */
	wdt->regmap = dev_get_regmap(parent->parent, NULL);
	if (!wdt->regmap) {
		dev_err(dev, "failed to locate regmap\n");
		return -ENODEV;
	}

	err = device_property_read_u32(parent, "reg", &wdt->baseaddr);
	if (err) {
		dev_err(dev, "failed to get pm8916-pon address\n");
		return err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		err = devm_request_irq(dev, irq, pm8916_wdt_isr, 0,
				       "pm8916_wdt", wdt);
		if (err)
			return err;

		wdt->wdev.info = &pm8916_wdt_pt_ident;
	} else {
		if (irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		wdt->wdev.info = &pm8916_wdt_ident;
	}

	/* Configure watchdog to hard-reset mode */
	err = regmap_write(wdt->regmap,
			   wdt->baseaddr + PON_PMIC_WD_RESET_S2_CTL,
			   RESET_TYPE_HARD);
	if (err) {
		dev_err(dev, "failed configure watchdog\n");
		return err;
	}

	wdt->wdev.ops = &pm8916_wdt_ops,
	wdt->wdev.parent = dev;
	wdt->wdev.min_timeout = PM8916_WDT_MIN_TIMEOUT;
	wdt->wdev.max_timeout = PM8916_WDT_MAX_TIMEOUT;
	wdt->wdev.timeout = PM8916_WDT_DEFAULT_TIMEOUT;
	wdt->wdev.pretimeout = 0;
	watchdog_set_drvdata(&wdt->wdev, wdt);
	platform_set_drvdata(pdev, wdt);

	watchdog_init_timeout(&wdt->wdev, 0, dev);
	pm8916_wdt_configure_timers(&wdt->wdev);

	return devm_watchdog_register_device(dev, &wdt->wdev);
}

static int __maybe_unused pm8916_wdt_suspend(struct device *dev)
{
	struct pm8916_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdev))
		return pm8916_wdt_stop(&wdt->wdev);

	return 0;
}

static int __maybe_unused pm8916_wdt_resume(struct device *dev)
{
	struct pm8916_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdev))
		return pm8916_wdt_start(&wdt->wdev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pm8916_wdt_pm_ops, pm8916_wdt_suspend,
			 pm8916_wdt_resume);

static const struct of_device_id pm8916_wdt_id_table[] = {
	{ .compatible = "qcom,pm8916-wdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8916_wdt_id_table);

static struct platform_driver pm8916_wdt_driver = {
	.probe = pm8916_wdt_probe,
	.driver = {
		.name = "pm8916-wdt",
		.of_match_table = of_match_ptr(pm8916_wdt_id_table),
		.pm = &pm8916_wdt_pm_ops,
	},
};
module_platform_driver(pm8916_wdt_driver);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("Qualcomm pm8916 watchdog driver");
MODULE_LICENSE("GPL v2");
