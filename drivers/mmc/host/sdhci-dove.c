/*
 * sdhci-dove.c Support for SDHCI on Marvell's Dove SoC
 *
 * Author: Saeed Bishara <saeed@marvell.com>
 *	   Mike Rapoport <mike@compulab.co.il>
 * Based on sdhci-cns3xxx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "sdhci-pltfm.h"

struct sdhci_dove_priv {
	struct clk *clk;
	int gpio_cd;
};

static irqreturn_t sdhci_dove_carddetect_irq(int irq, void *data)
{
	struct sdhci_host *host = data;

	tasklet_schedule(&host->card_tasklet);
	return IRQ_HANDLED;
}

static u16 sdhci_dove_readw(struct sdhci_host *host, int reg)
{
	u16 ret;

	switch (reg) {
	case SDHCI_HOST_VERSION:
	case SDHCI_SLOT_INT_STATUS:
		/* those registers don't exist */
		return 0;
	default:
		ret = readw(host->ioaddr + reg);
	}
	return ret;
}

static u32 sdhci_dove_readl(struct sdhci_host *host, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_dove_priv *priv = pltfm_host->priv;
	u32 ret;

	ret = readl(host->ioaddr + reg);

	switch (reg) {
	case SDHCI_CAPABILITIES:
		/* Mask the support for 3.0V */
		ret &= ~SDHCI_CAN_VDD_300;
		break;
	case SDHCI_PRESENT_STATE:
		if (gpio_is_valid(priv->gpio_cd)) {
			if (gpio_get_value(priv->gpio_cd) == 0)
				ret |= SDHCI_CARD_PRESENT;
			else
				ret &= ~SDHCI_CARD_PRESENT;
		}
		break;
	}
	return ret;
}

static const struct sdhci_ops sdhci_dove_ops = {
	.read_w	= sdhci_dove_readw,
	.read_l	= sdhci_dove_readl,
};

static const struct sdhci_pltfm_data sdhci_dove_pdata = {
	.ops	= &sdhci_dove_ops,
	.quirks	= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER |
		  SDHCI_QUIRK_NO_BUSY_IRQ |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_FORCE_DMA |
		  SDHCI_QUIRK_NO_HISPD_BIT,
};

static int sdhci_dove_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_dove_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct sdhci_dove_priv),
			    GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to allocate private data");
		return -ENOMEM;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);

	if (pdev->dev.of_node) {
		priv->gpio_cd = of_get_named_gpio(pdev->dev.of_node,
						  "cd-gpios", 0);
	} else {
		priv->gpio_cd = -EINVAL;
	}

	if (gpio_is_valid(priv->gpio_cd)) {
		ret = gpio_request(priv->gpio_cd, "sdhci-cd");
		if (ret) {
			dev_err(&pdev->dev, "card detect gpio request failed: %d\n",
				ret);
			return ret;
		}
		gpio_direction_input(priv->gpio_cd);
	}

	host = sdhci_pltfm_init(pdev, &sdhci_dove_pdata, 0);
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto err_sdhci_pltfm_init;
	}

	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = priv;

	if (!IS_ERR(priv->clk))
		clk_prepare_enable(priv->clk);

	sdhci_get_of_property(pdev);

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	/*
	 * We must request the IRQ after sdhci_add_host(), as the tasklet only
	 * gets setup in sdhci_add_host() and we oops.
	 */
	if (gpio_is_valid(priv->gpio_cd)) {
		ret = request_irq(gpio_to_irq(priv->gpio_cd),
				  sdhci_dove_carddetect_irq,
				  IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				  mmc_hostname(host->mmc), host);
		if (ret) {
			dev_err(&pdev->dev, "card detect irq request failed: %d\n",
				ret);
			goto err_request_irq;
		}
	}

	return 0;

err_request_irq:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);
	sdhci_pltfm_free(pdev);
err_sdhci_pltfm_init:
	if (gpio_is_valid(priv->gpio_cd))
		gpio_free(priv->gpio_cd);
	return ret;
}

static int sdhci_dove_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_dove_priv *priv = pltfm_host->priv;

	sdhci_pltfm_unregister(pdev);

	if (gpio_is_valid(priv->gpio_cd)) {
		free_irq(gpio_to_irq(priv->gpio_cd), host);
		gpio_free(priv->gpio_cd);
	}

	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id sdhci_dove_of_match_table[] = {
	{ .compatible = "marvell,dove-sdhci", },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_dove_of_match_table);

static struct platform_driver sdhci_dove_driver = {
	.driver		= {
		.name	= "sdhci-dove",
		.owner	= THIS_MODULE,
		.pm	= SDHCI_PLTFM_PMOPS,
		.of_match_table = sdhci_dove_of_match_table,
	},
	.probe		= sdhci_dove_probe,
	.remove		= sdhci_dove_remove,
};

module_platform_driver(sdhci_dove_driver);

MODULE_DESCRIPTION("SDHCI driver for Dove");
MODULE_AUTHOR("Saeed Bishara <saeed@marvell.com>, "
	      "Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL v2");
