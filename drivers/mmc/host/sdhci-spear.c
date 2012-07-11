/*
 * drivers/mmc/host/sdhci-spear.c
 *
 * Support of SDHCI platform devices for spear soc family
 *
 * Copyright (C) 2010 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * Inspired by sdhci-pltfm.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci-spear.h>
#include <linux/io.h>
#include "sdhci.h"

struct spear_sdhci {
	struct clk *clk;
	struct sdhci_plat_data *data;
};

/* sdhci ops */
static struct sdhci_ops sdhci_pltfm_ops = {
	/* Nothing to do for now. */
};

/* gpio card detection interrupt handler */
static irqreturn_t sdhci_gpio_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct spear_sdhci *sdhci = dev_get_platdata(&pdev->dev);
	unsigned long gpio_irq_type;
	int val;

	val = gpio_get_value(sdhci->data->card_int_gpio);

	/* val == 1 -> card removed, val == 0 -> card inserted */
	/* if card removed - set irq for low level, else vice versa */
	gpio_irq_type = val ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH;
	irq_set_irq_type(irq, gpio_irq_type);

	if (sdhci->data->card_power_gpio >= 0) {
		if (!sdhci->data->power_always_enb) {
			/* if card inserted, give power, otherwise remove it */
			val = sdhci->data->power_active_high ? !val : val ;
			gpio_set_value(sdhci->data->card_power_gpio, val);
		}
	}

	/* inform sdhci driver about card insertion/removal */
	tasklet_schedule(&host->card_tasklet);

	return IRQ_HANDLED;
}

static int __devinit sdhci_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct resource *iomem;
	struct spear_sdhci *sdhci;
	int ret;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "memory resource not defined\n");
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev, iomem->start,
				resource_size(iomem), "spear-sdhci")) {
		ret = -EBUSY;
		dev_dbg(&pdev->dev, "cannot request region\n");
		goto err;
	}

	sdhci = devm_kzalloc(&pdev->dev, sizeof(*sdhci), GFP_KERNEL);
	if (!sdhci) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "cannot allocate memory for sdhci\n");
		goto err;
	}

	/* clk enable */
	sdhci->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sdhci->clk)) {
		ret = PTR_ERR(sdhci->clk);
		dev_dbg(&pdev->dev, "Error getting clock\n");
		goto err;
	}

	ret = clk_enable(sdhci->clk);
	if (ret) {
		dev_dbg(&pdev->dev, "Error enabling clock\n");
		goto put_clk;
	}

	/* overwrite platform_data */
	sdhci->data = dev_get_platdata(&pdev->dev);
	pdev->dev.platform_data = sdhci;

	if (pdev->dev.parent)
		host = sdhci_alloc_host(pdev->dev.parent, 0);
	else
		host = sdhci_alloc_host(&pdev->dev, 0);

	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		dev_dbg(&pdev->dev, "error allocating host\n");
		goto disable_clk;
	}

	host->hw_name = "sdhci";
	host->ops = &sdhci_pltfm_ops;
	host->irq = platform_get_irq(pdev, 0);
	host->quirks = SDHCI_QUIRK_BROKEN_ADMA;

	host->ioaddr = devm_ioremap(&pdev->dev, iomem->start,
			resource_size(iomem));
	if (!host->ioaddr) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "failed to remap registers\n");
		goto free_host;
	}

	ret = sdhci_add_host(host);
	if (ret) {
		dev_dbg(&pdev->dev, "error adding host\n");
		goto free_host;
	}

	platform_set_drvdata(pdev, host);

	/*
	 * It is optional to use GPIOs for sdhci Power control & sdhci card
	 * interrupt detection. If sdhci->data is NULL, then use original sdhci
	 * lines otherwise GPIO lines.
	 * If GPIO is selected for power control, then power should be disabled
	 * after card removal and should be enabled when card insertion
	 * interrupt occurs
	 */
	if (!sdhci->data)
		return 0;

	if (sdhci->data->card_power_gpio >= 0) {
		int val = 0;

		ret = devm_gpio_request(&pdev->dev,
				sdhci->data->card_power_gpio, "sdhci");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "gpio request fail: %d\n",
					sdhci->data->card_power_gpio);
			goto set_drvdata;
		}

		if (sdhci->data->power_always_enb)
			val = sdhci->data->power_active_high;
		else
			val = !sdhci->data->power_active_high;

		ret = gpio_direction_output(sdhci->data->card_power_gpio, val);
		if (ret) {
			dev_dbg(&pdev->dev, "gpio set direction fail: %d\n",
					sdhci->data->card_power_gpio);
			goto set_drvdata;
		}
	}

	if (sdhci->data->card_int_gpio >= 0) {
		ret = devm_gpio_request(&pdev->dev, sdhci->data->card_int_gpio,
				"sdhci");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "gpio request fail: %d\n",
					sdhci->data->card_int_gpio);
			goto set_drvdata;
		}

		ret = gpio_direction_input(sdhci->data->card_int_gpio);
		if (ret) {
			dev_dbg(&pdev->dev, "gpio set direction fail: %d\n",
					sdhci->data->card_int_gpio);
			goto set_drvdata;
		}
		ret = devm_request_irq(&pdev->dev,
				gpio_to_irq(sdhci->data->card_int_gpio),
				sdhci_gpio_irq, IRQF_TRIGGER_LOW,
				mmc_hostname(host->mmc), pdev);
		if (ret) {
			dev_dbg(&pdev->dev, "gpio request irq fail: %d\n",
					sdhci->data->card_int_gpio);
			goto set_drvdata;
		}

	}

	return 0;

set_drvdata:
	platform_set_drvdata(pdev, NULL);
	sdhci_remove_host(host, 1);
free_host:
	sdhci_free_host(host);
disable_clk:
	clk_disable(sdhci->clk);
put_clk:
	clk_put(sdhci->clk);
err:
	dev_err(&pdev->dev, "spear-sdhci probe failed: %d\n", ret);
	return ret;
}

static int __devexit sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct spear_sdhci *sdhci = dev_get_platdata(&pdev->dev);
	int dead = 0;
	u32 scratch;

	platform_set_drvdata(pdev, NULL);
	scratch = readl(host->ioaddr + SDHCI_INT_STATUS);
	if (scratch == (u32)-1)
		dead = 1;

	sdhci_remove_host(host, dead);
	sdhci_free_host(host);
	clk_disable(sdhci->clk);
	clk_put(sdhci->clk);

	return 0;
}

#ifdef CONFIG_PM
static int sdhci_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spear_sdhci *sdhci = dev_get_platdata(dev);
	int ret;

	ret = sdhci_suspend_host(host);
	if (!ret)
		clk_disable(sdhci->clk);

	return ret;
}

static int sdhci_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spear_sdhci *sdhci = dev_get_platdata(dev);
	int ret;

	ret = clk_enable(sdhci->clk);
	if (ret) {
		dev_dbg(dev, "Resume: Error enabling clock\n");
		return ret;
	}

	return sdhci_resume_host(host);
}
#endif

static SIMPLE_DEV_PM_OPS(sdhci_pm_ops, sdhci_suspend, sdhci_resume);

static struct platform_driver sdhci_driver = {
	.driver = {
		.name	= "sdhci",
		.owner	= THIS_MODULE,
		.pm	= &sdhci_pm_ops,
	},
	.probe		= sdhci_probe,
	.remove		= __devexit_p(sdhci_remove),
};

module_platform_driver(sdhci_driver);

MODULE_DESCRIPTION("SPEAr Secure Digital Host Controller Interface driver");
MODULE_AUTHOR("Viresh Kumar <viresh.linux@gmail.com>");
MODULE_LICENSE("GPL v2");
