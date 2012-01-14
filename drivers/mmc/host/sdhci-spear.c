/*
 * drivers/mmc/host/sdhci-spear.c
 *
 * Support of SDHCI platform devices for spear soc family
 *
 * Copyright (C) 2010 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
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

	BUG_ON(pdev == NULL);

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "memory resource not defined\n");
		goto err;
	}

	if (!request_mem_region(iomem->start, resource_size(iomem),
				"spear-sdhci")) {
		ret = -EBUSY;
		dev_dbg(&pdev->dev, "cannot request region\n");
		goto err;
	}

	sdhci = kzalloc(sizeof(*sdhci), GFP_KERNEL);
	if (!sdhci) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "cannot allocate memory for sdhci\n");
		goto err_kzalloc;
	}

	/* clk enable */
	sdhci->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sdhci->clk)) {
		ret = PTR_ERR(sdhci->clk);
		dev_dbg(&pdev->dev, "Error getting clock\n");
		goto err_clk_get;
	}

	ret = clk_enable(sdhci->clk);
	if (ret) {
		dev_dbg(&pdev->dev, "Error enabling clock\n");
		goto err_clk_enb;
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
		goto err_alloc_host;
	}

	host->hw_name = "sdhci";
	host->ops = &sdhci_pltfm_ops;
	host->irq = platform_get_irq(pdev, 0);
	host->quirks = SDHCI_QUIRK_BROKEN_ADMA;

	host->ioaddr = ioremap(iomem->start, resource_size(iomem));
	if (!host->ioaddr) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "failed to remap registers\n");
		goto err_ioremap;
	}

	ret = sdhci_add_host(host);
	if (ret) {
		dev_dbg(&pdev->dev, "error adding host\n");
		goto err_add_host;
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

		ret = gpio_request(sdhci->data->card_power_gpio, "sdhci");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "gpio request fail: %d\n",
					sdhci->data->card_power_gpio);
			goto err_pgpio_request;
		}

		if (sdhci->data->power_always_enb)
			val = sdhci->data->power_active_high;
		else
			val = !sdhci->data->power_active_high;

		ret = gpio_direction_output(sdhci->data->card_power_gpio, val);
		if (ret) {
			dev_dbg(&pdev->dev, "gpio set direction fail: %d\n",
					sdhci->data->card_power_gpio);
			goto err_pgpio_direction;
		}
	}

	if (sdhci->data->card_int_gpio >= 0) {
		ret = gpio_request(sdhci->data->card_int_gpio, "sdhci");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "gpio request fail: %d\n",
					sdhci->data->card_int_gpio);
			goto err_igpio_request;
		}

		ret = gpio_direction_input(sdhci->data->card_int_gpio);
		if (ret) {
			dev_dbg(&pdev->dev, "gpio set direction fail: %d\n",
					sdhci->data->card_int_gpio);
			goto err_igpio_direction;
		}
		ret = request_irq(gpio_to_irq(sdhci->data->card_int_gpio),
				sdhci_gpio_irq, IRQF_TRIGGER_LOW,
				mmc_hostname(host->mmc), pdev);
		if (ret) {
			dev_dbg(&pdev->dev, "gpio request irq fail: %d\n",
					sdhci->data->card_int_gpio);
			goto err_igpio_request_irq;
		}

	}

	return 0;

err_igpio_request_irq:
err_igpio_direction:
	if (sdhci->data->card_int_gpio >= 0)
		gpio_free(sdhci->data->card_int_gpio);
err_igpio_request:
err_pgpio_direction:
	if (sdhci->data->card_power_gpio >= 0)
		gpio_free(sdhci->data->card_power_gpio);
err_pgpio_request:
	platform_set_drvdata(pdev, NULL);
	sdhci_remove_host(host, 1);
err_add_host:
	iounmap(host->ioaddr);
err_ioremap:
	sdhci_free_host(host);
err_alloc_host:
	clk_disable(sdhci->clk);
err_clk_enb:
	clk_put(sdhci->clk);
err_clk_get:
	kfree(sdhci);
err_kzalloc:
	release_mem_region(iomem->start, resource_size(iomem));
err:
	dev_err(&pdev->dev, "spear-sdhci probe failed: %d\n", ret);
	return ret;
}

static int __devexit sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct resource *iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct spear_sdhci *sdhci = dev_get_platdata(&pdev->dev);
	int dead;
	u32 scratch;

	if (sdhci->data) {
		if (sdhci->data->card_int_gpio >= 0) {
			free_irq(gpio_to_irq(sdhci->data->card_int_gpio), pdev);
			gpio_free(sdhci->data->card_int_gpio);
		}

		if (sdhci->data->card_power_gpio >= 0)
			gpio_free(sdhci->data->card_power_gpio);
	}

	platform_set_drvdata(pdev, NULL);
	dead = 0;
	scratch = readl(host->ioaddr + SDHCI_INT_STATUS);
	if (scratch == (u32)-1)
		dead = 1;

	sdhci_remove_host(host, dead);
	iounmap(host->ioaddr);
	sdhci_free_host(host);
	clk_disable(sdhci->clk);
	clk_put(sdhci->clk);
	kfree(sdhci);
	if (iomem)
		release_mem_region(iomem->start, resource_size(iomem));

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

const struct dev_pm_ops sdhci_pm_ops = {
	.suspend	= sdhci_suspend,
	.resume		= sdhci_resume,
};
#endif

static struct platform_driver sdhci_driver = {
	.driver = {
		.name	= "sdhci",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &sdhci_pm_ops,
#endif
	},
	.probe		= sdhci_probe,
	.remove		= __devexit_p(sdhci_remove),
};

module_platform_driver(sdhci_driver);

MODULE_DESCRIPTION("SPEAr Secure Digital Host Controller Interface driver");
MODULE_AUTHOR("Viresh Kumar <viresh.kumar@st.com>");
MODULE_LICENSE("GPL v2");
