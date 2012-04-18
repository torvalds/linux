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

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mmc/host.h>

#include "sdhci-pltfm.h"

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
	u32 ret;

	switch (reg) {
	case SDHCI_CAPABILITIES:
		ret = readl(host->ioaddr + reg);
		/* Mask the support for 3.0V */
		ret &= ~SDHCI_CAN_VDD_300;
		break;
	default:
		ret = readl(host->ioaddr + reg);
	}
	return ret;
}

static struct sdhci_ops sdhci_dove_ops = {
	.read_w	= sdhci_dove_readw,
	.read_l	= sdhci_dove_readl,
};

static struct sdhci_pltfm_data sdhci_dove_pdata = {
	.ops	= &sdhci_dove_ops,
	.quirks	= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER |
		  SDHCI_QUIRK_NO_BUSY_IRQ |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_FORCE_DMA,
};

static int __devinit sdhci_dove_probe(struct platform_device *pdev)
{
	return sdhci_pltfm_register(pdev, &sdhci_dove_pdata);
}

static int __devexit sdhci_dove_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static struct platform_driver sdhci_dove_driver = {
	.driver		= {
		.name	= "sdhci-dove",
		.owner	= THIS_MODULE,
		.pm	= SDHCI_PLTFM_PMOPS,
	},
	.probe		= sdhci_dove_probe,
	.remove		= __devexit_p(sdhci_dove_remove),
};

module_platform_driver(sdhci_dove_driver);

MODULE_DESCRIPTION("SDHCI driver for Dove");
MODULE_AUTHOR("Saeed Bishara <saeed@marvell.com>, "
	      "Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL v2");
