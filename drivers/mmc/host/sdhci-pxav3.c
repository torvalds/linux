/*
 * Copyright (C) 2010 Marvell International Ltd.
 *		Zhangfei Gao <zhangfei.gao@marvell.com>
 *		Kevin Wang <dwang4@marvell.com>
 *		Mingwei Wang <mwwang@marvell.com>
 *		Philip Rakity <prakity@marvell.com>
 *		Mark Brown <markb@marvell.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "sdhci.h"
#include "sdhci-pltfm.h"

#define SD_CLOCK_BURST_SIZE_SETUP		0x10A
#define SDCLK_SEL	0x100
#define SDCLK_DELAY_SHIFT	9
#define SDCLK_DELAY_MASK	0x1f

#define SD_CFG_FIFO_PARAM       0x100
#define SDCFG_GEN_PAD_CLK_ON	(1<<6)
#define SDCFG_GEN_PAD_CLK_CNT_MASK	0xFF
#define SDCFG_GEN_PAD_CLK_CNT_SHIFT	24

#define SD_SPI_MODE          0x108
#define SD_CE_ATA_1          0x10C

#define SD_CE_ATA_2          0x10E
#define SDCE_MISC_INT		(1<<2)
#define SDCE_MISC_INT_EN	(1<<1)

static void pxav3_set_private_registers(struct sdhci_host *host, u8 mask)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (mask == SDHCI_RESET_ALL) {
		/*
		 * tune timing of read data/command when crc error happen
		 * no performance impact
		 */
		if (pdata && 0 != pdata->clk_delay_cycles) {
			u16 tmp;

			tmp = readw(host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);
			tmp |= (pdata->clk_delay_cycles & SDCLK_DELAY_MASK)
				<< SDCLK_DELAY_SHIFT;
			tmp |= SDCLK_SEL;
			writew(tmp, host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);
		}
	}
}

#define MAX_WAIT_COUNT 5
static void pxav3_gen_init_74_clocks(struct sdhci_host *host, u8 power_mode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	u16 tmp;
	int count;

	if (pxa->power_mode == MMC_POWER_UP
			&& power_mode == MMC_POWER_ON) {

		dev_dbg(mmc_dev(host->mmc),
				"%s: slot->power_mode = %d,"
				"ios->power_mode = %d\n",
				__func__,
				pxa->power_mode,
				power_mode);

		/* set we want notice of when 74 clocks are sent */
		tmp = readw(host->ioaddr + SD_CE_ATA_2);
		tmp |= SDCE_MISC_INT_EN;
		writew(tmp, host->ioaddr + SD_CE_ATA_2);

		/* start sending the 74 clocks */
		tmp = readw(host->ioaddr + SD_CFG_FIFO_PARAM);
		tmp |= SDCFG_GEN_PAD_CLK_ON;
		writew(tmp, host->ioaddr + SD_CFG_FIFO_PARAM);

		/* slowest speed is about 100KHz or 10usec per clock */
		udelay(740);
		count = 0;

		while (count++ < MAX_WAIT_COUNT) {
			if ((readw(host->ioaddr + SD_CE_ATA_2)
						& SDCE_MISC_INT) == 0)
				break;
			udelay(10);
		}

		if (count == MAX_WAIT_COUNT)
			dev_warn(mmc_dev(host->mmc), "74 clock interrupt not cleared\n");

		/* clear the interrupt bit if posted */
		tmp = readw(host->ioaddr + SD_CE_ATA_2);
		tmp |= SDCE_MISC_INT;
		writew(tmp, host->ioaddr + SD_CE_ATA_2);
	}
	pxa->power_mode = power_mode;
}

static int pxav3_set_uhs_signaling(struct sdhci_host *host, unsigned int uhs)
{
	u16 ctrl_2;

	/*
	 * Set V18_EN -- UHS modes do not work without this.
	 * does not change signaling voltage
	 */
	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	switch (uhs) {
	case MMC_TIMING_UHS_SDR12:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_SDR104:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_DDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50 | SDHCI_CTRL_VDD_180;
		break;
	}

	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	dev_dbg(mmc_dev(host->mmc),
		"%s uhs = %d, ctrl_2 = %04X\n",
		__func__, uhs, ctrl_2);

	return 0;
}

static struct sdhci_ops pxav3_sdhci_ops = {
	.platform_reset_exit = pxav3_set_private_registers,
	.set_uhs_signaling = pxav3_set_uhs_signaling,
	.platform_send_init_74_clocks = pxav3_gen_init_74_clocks,
};

static int __devinit sdhci_pxav3_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = NULL;
	struct sdhci_pxa *pxa = NULL;
	int ret;
	struct clk *clk;

	pxa = kzalloc(sizeof(struct sdhci_pxa), GFP_KERNEL);
	if (!pxa)
		return -ENOMEM;

	host = sdhci_pltfm_init(pdev, NULL);
	if (IS_ERR(host)) {
		kfree(pxa);
		return PTR_ERR(host);
	}
	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = pxa;

	clk = clk_get(dev, "PXA-SDHCLK");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get io clock\n");
		ret = PTR_ERR(clk);
		goto err_clk_get;
	}
	pltfm_host->clk = clk;
	clk_enable(clk);

	host->quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL
		| SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC
		| SDHCI_QUIRK_32BIT_ADMA_SIZE;

	/* enable 1/8V DDR capable */
	host->mmc->caps |= MMC_CAP_1_8V_DDR;

	if (pdata) {
		if (pdata->flags & PXA_FLAG_CARD_PERMANENT) {
			/* on-chip device */
			host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
			host->mmc->caps |= MMC_CAP_NONREMOVABLE;
		}

		/* If slot design supports 8 bit data, indicate this to MMC. */
		if (pdata->flags & PXA_FLAG_SD_8_BIT_CAPABLE_SLOT)
			host->mmc->caps |= MMC_CAP_8_BIT_DATA;

		if (pdata->quirks)
			host->quirks |= pdata->quirks;
		if (pdata->host_caps)
			host->mmc->caps |= pdata->host_caps;
		if (pdata->pm_caps)
			host->mmc->pm_caps |= pdata->pm_caps;
	}

	host->ops = &pxav3_sdhci_ops;

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "failed to add host\n");
		goto err_add_host;
	}

	platform_set_drvdata(pdev, host);

	return 0;

err_add_host:
	clk_disable(clk);
	clk_put(clk);
err_clk_get:
	sdhci_pltfm_free(pdev);
	kfree(pxa);
	return ret;
}

static int __devexit sdhci_pxav3_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;

	sdhci_remove_host(host, 1);

	clk_disable(pltfm_host->clk);
	clk_put(pltfm_host->clk);
	sdhci_pltfm_free(pdev);
	kfree(pxa);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver sdhci_pxav3_driver = {
	.driver		= {
		.name	= "sdhci-pxav3",
		.owner	= THIS_MODULE,
	},
	.probe		= sdhci_pxav3_probe,
	.remove		= __devexit_p(sdhci_pxav3_remove),
#ifdef CONFIG_PM
	.suspend	= sdhci_pltfm_suspend,
	.resume		= sdhci_pltfm_resume,
#endif
};
static int __init sdhci_pxav3_init(void)
{
	return platform_driver_register(&sdhci_pxav3_driver);
}

static void __exit sdhci_pxav3_exit(void)
{
	platform_driver_unregister(&sdhci_pxav3_driver);
}

module_init(sdhci_pxav3_init);
module_exit(sdhci_pxav3_exit);

MODULE_DESCRIPTION("SDHCI driver for pxav3");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL v2");

