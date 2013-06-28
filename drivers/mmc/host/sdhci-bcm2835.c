/*
 * BCM2835 SDHCI
 * Copyright (C) 2012 Stephen Warren
 * Based on U-Boot's MMC driver for the BCM2835 by Oleksandr Tymoshenko & me
 * Portions of the code there were obviously based on the Linux kernel at:
 * git://github.com/raspberrypi/linux.git rpi-3.6.y
 * commit f5b930b "Main bcm2708 linux port" signed-off-by Dom Cobley.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include "sdhci-pltfm.h"

/*
 * 400KHz is max freq for card ID etc. Use that as min card clock. We need to
 * know the min to enable static calculation of max BCM2835_SDHCI_WRITE_DELAY.
 */
#define MIN_FREQ 400000

/*
 * The Arasan has a bugette whereby it may lose the content of successive
 * writes to registers that are within two SD-card clock cycles of each other
 * (a clock domain crossing problem). It seems, however, that the data
 * register does not have this problem, which is just as well - otherwise we'd
 * have to nobble the DMA engine too.
 *
 * This should probably be dynamically calculated based on the actual card
 * frequency. However, this is the longest we'll have to wait, and doesn't
 * seem to slow access down too much, so the added complexity doesn't seem
 * worth it for now.
 *
 * 1/MIN_FREQ is (max) time per tick of eMMC clock.
 * 2/MIN_FREQ is time for two ticks.
 * Multiply by 1000000 to get uS per two ticks.
 * *1000000 for uSecs.
 * +1 for hack rounding.
 */
#define BCM2835_SDHCI_WRITE_DELAY	(((2 * 1000000) / MIN_FREQ) + 1)

struct bcm2835_sdhci {
	u32 shadow;
};

static void bcm2835_sdhci_writel(struct sdhci_host *host, u32 val, int reg)
{
	writel(val, host->ioaddr + reg);

	udelay(BCM2835_SDHCI_WRITE_DELAY);
}

static inline u32 bcm2835_sdhci_readl(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	if (reg == SDHCI_CAPABILITIES)
		val |= SDHCI_CAN_VDD_330;

	return val;
}

static void bcm2835_sdhci_writew(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct bcm2835_sdhci *bcm2835_host = pltfm_host->priv;
	u32 oldval = (reg == SDHCI_COMMAND) ? bcm2835_host->shadow :
		bcm2835_sdhci_readl(host, reg & ~3);
	u32 word_num = (reg >> 1) & 1;
	u32 word_shift = word_num * 16;
	u32 mask = 0xffff << word_shift;
	u32 newval = (oldval & ~mask) | (val << word_shift);

	if (reg == SDHCI_TRANSFER_MODE)
		bcm2835_host->shadow = newval;
	else
		bcm2835_sdhci_writel(host, newval, reg & ~3);
}

static u16 bcm2835_sdhci_readw(struct sdhci_host *host, int reg)
{
	u32 val = bcm2835_sdhci_readl(host, (reg & ~3));
	u32 word_num = (reg >> 1) & 1;
	u32 word_shift = word_num * 16;
	u32 word = (val >> word_shift) & 0xffff;

	return word;
}

static void bcm2835_sdhci_writeb(struct sdhci_host *host, u8 val, int reg)
{
	u32 oldval = bcm2835_sdhci_readl(host, reg & ~3);
	u32 byte_num = reg & 3;
	u32 byte_shift = byte_num * 8;
	u32 mask = 0xff << byte_shift;
	u32 newval = (oldval & ~mask) | (val << byte_shift);

	bcm2835_sdhci_writel(host, newval, reg & ~3);
}

static u8 bcm2835_sdhci_readb(struct sdhci_host *host, int reg)
{
	u32 val = bcm2835_sdhci_readl(host, (reg & ~3));
	u32 byte_num = reg & 3;
	u32 byte_shift = byte_num * 8;
	u32 byte = (val >> byte_shift) & 0xff;

	return byte;
}

unsigned int bcm2835_sdhci_get_min_clock(struct sdhci_host *host)
{
	return MIN_FREQ;
}

static const struct sdhci_ops bcm2835_sdhci_ops = {
	.write_l = bcm2835_sdhci_writel,
	.write_w = bcm2835_sdhci_writew,
	.write_b = bcm2835_sdhci_writeb,
	.read_l = bcm2835_sdhci_readl,
	.read_w = bcm2835_sdhci_readw,
	.read_b = bcm2835_sdhci_readb,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_min_clock = bcm2835_sdhci_get_min_clock,
};

static const struct sdhci_pltfm_data bcm2835_sdhci_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,
	.ops = &bcm2835_sdhci_ops,
};

static int bcm2835_sdhci_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct bcm2835_sdhci *bcm2835_host;
	struct sdhci_pltfm_host *pltfm_host;
	int ret;

	host = sdhci_pltfm_init(pdev, &bcm2835_sdhci_pdata);
	if (IS_ERR(host))
		return PTR_ERR(host);

	bcm2835_host = devm_kzalloc(&pdev->dev, sizeof(*bcm2835_host),
					GFP_KERNEL);
	if (!bcm2835_host) {
		dev_err(mmc_dev(host->mmc),
			"failed to allocate bcm2835_sdhci\n");
		return -ENOMEM;
	}

	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = bcm2835_host;

	pltfm_host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pltfm_host->clk)) {
		ret = PTR_ERR(pltfm_host->clk);
		goto err;
	}

	return sdhci_add_host(host);

err:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int bcm2835_sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	return 0;
}

static const struct of_device_id bcm2835_sdhci_of_match[] = {
	{ .compatible = "brcm,bcm2835-sdhci" },
	{ }
};
MODULE_DEVICE_TABLE(of, bcm2835_sdhci_of_match);

static struct platform_driver bcm2835_sdhci_driver = {
	.driver = {
		.name = "sdhci-bcm2835",
		.owner = THIS_MODULE,
		.of_match_table = bcm2835_sdhci_of_match,
		.pm = SDHCI_PLTFM_PMOPS,
	},
	.probe = bcm2835_sdhci_probe,
	.remove = bcm2835_sdhci_remove,
};
module_platform_driver(bcm2835_sdhci_driver);

MODULE_DESCRIPTION("BCM2835 SDHCI driver");
MODULE_AUTHOR("Stephen Warren");
MODULE_LICENSE("GPL v2");
