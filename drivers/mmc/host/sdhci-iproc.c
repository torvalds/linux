/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * iProc SDHCI platform driver
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include "sdhci-pltfm.h"

struct sdhci_iproc_data {
	const struct sdhci_pltfm_data *pdata;
	u32 caps;
	u32 caps1;
};

struct sdhci_iproc_host {
	const struct sdhci_iproc_data *data;
	u32 shadow_cmd;
	u32 shadow_blk;
};

#define REG_OFFSET_IN_BITS(reg) ((reg) << 3 & 0x18)

static inline u32 sdhci_iproc_readl(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	pr_debug("%s: readl [0x%02x] 0x%08x\n",
		 mmc_hostname(host->mmc), reg, val);
	return val;
}

static u16 sdhci_iproc_readw(struct sdhci_host *host, int reg)
{
	u32 val = sdhci_iproc_readl(host, (reg & ~3));
	u16 word = val >> REG_OFFSET_IN_BITS(reg) & 0xffff;
	return word;
}

static u8 sdhci_iproc_readb(struct sdhci_host *host, int reg)
{
	u32 val = sdhci_iproc_readl(host, (reg & ~3));
	u8 byte = val >> REG_OFFSET_IN_BITS(reg) & 0xff;
	return byte;
}

static inline void sdhci_iproc_writel(struct sdhci_host *host, u32 val, int reg)
{
	pr_debug("%s: writel [0x%02x] 0x%08x\n",
		 mmc_hostname(host->mmc), reg, val);

	writel(val, host->ioaddr + reg);

	if (host->clock <= 400000) {
		/* Round up to micro-second four SD clock delay */
		if (host->clock)
			udelay((4 * 1000000 + host->clock - 1) / host->clock);
		else
			udelay(10);
	}
}

/*
 * The Arasan has a bugette whereby it may lose the content of successive
 * writes to the same register that are within two SD-card clock cycles of
 * each other (a clock domain crossing problem). The data
 * register does not have this problem, which is just as well - otherwise we'd
 * have to nobble the DMA engine too.
 *
 * This wouldn't be a problem with the code except that we can only write the
 * controller with 32-bit writes.  So two different 16-bit registers are
 * written back to back creates the problem.
 *
 * In reality, this only happens when SDHCI_BLOCK_SIZE and SDHCI_BLOCK_COUNT
 * are written followed by SDHCI_TRANSFER_MODE and SDHCI_COMMAND.
 * The BLOCK_SIZE and BLOCK_COUNT are meaningless until a command issued so
 * the work around can be further optimized. We can keep shadow values of
 * BLOCK_SIZE, BLOCK_COUNT, and TRANSFER_MODE until a COMMAND is issued.
 * Then, write the BLOCK_SIZE+BLOCK_COUNT in a single 32-bit write followed
 * by the TRANSFER+COMMAND in another 32-bit write.
 */
static void sdhci_iproc_writew(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_iproc_host *iproc_host = pltfm_host->priv;
	u32 word_shift = REG_OFFSET_IN_BITS(reg);
	u32 mask = 0xffff << word_shift;
	u32 oldval, newval;

	if (reg == SDHCI_COMMAND) {
		/* Write the block now as we are issuing a command */
		if (iproc_host->shadow_blk != 0) {
			sdhci_iproc_writel(host, iproc_host->shadow_blk,
				SDHCI_BLOCK_SIZE);
			iproc_host->shadow_blk = 0;
		}
		oldval = iproc_host->shadow_cmd;
	} else if (reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) {
		/* Block size and count are stored in shadow reg */
		oldval = iproc_host->shadow_blk;
	} else {
		/* Read reg, all other registers are not shadowed */
		oldval = sdhci_iproc_readl(host, (reg & ~3));
	}
	newval = (oldval & ~mask) | (val << word_shift);

	if (reg == SDHCI_TRANSFER_MODE) {
		/* Save the transfer mode until the command is issued */
		iproc_host->shadow_cmd = newval;
	} else if (reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) {
		/* Save the block info until the command is issued */
		iproc_host->shadow_blk = newval;
	} else {
		/* Command or other regular 32-bit write */
		sdhci_iproc_writel(host, newval, reg & ~3);
	}
}

static void sdhci_iproc_writeb(struct sdhci_host *host, u8 val, int reg)
{
	u32 oldval = sdhci_iproc_readl(host, (reg & ~3));
	u32 byte_shift = REG_OFFSET_IN_BITS(reg);
	u32 mask = 0xff << byte_shift;
	u32 newval = (oldval & ~mask) | (val << byte_shift);

	sdhci_iproc_writel(host, newval, reg & ~3);
}

static const struct sdhci_ops sdhci_iproc_ops = {
	.read_l = sdhci_iproc_readl,
	.read_w = sdhci_iproc_readw,
	.read_b = sdhci_iproc_readb,
	.write_l = sdhci_iproc_writel,
	.write_w = sdhci_iproc_writew,
	.write_b = sdhci_iproc_writeb,
	.set_clock = sdhci_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_iproc_pltfm_data = {
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,
	.quirks2 = SDHCI_QUIRK2_ACMD23_BROKEN,
	.ops = &sdhci_iproc_ops,
};

static const struct sdhci_iproc_data iproc_data = {
	.pdata = &sdhci_iproc_pltfm_data,
	.caps = 0x05E90000,
	.caps1 = 0x00000064,
};

static const struct of_device_id sdhci_iproc_of_match[] = {
	{ .compatible = "brcm,sdhci-iproc-cygnus", .data = &iproc_data },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_iproc_of_match);

static int sdhci_iproc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sdhci_iproc_data *iproc_data;
	struct sdhci_host *host;
	struct sdhci_iproc_host *iproc_host;
	struct sdhci_pltfm_host *pltfm_host;
	int ret;

	match = of_match_device(sdhci_iproc_of_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	iproc_data = match->data;

	host = sdhci_pltfm_init(pdev, iproc_data->pdata, sizeof(*iproc_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	iproc_host = sdhci_pltfm_priv(pltfm_host);

	iproc_host->data = iproc_data;

	mmc_of_parse(host->mmc);
	sdhci_get_of_property(pdev);

	/* Enable EMMC 1/8V DDR capable */
	host->mmc->caps |= MMC_CAP_1_8V_DDR;

	pltfm_host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pltfm_host->clk)) {
		ret = PTR_ERR(pltfm_host->clk);
		goto err;
	}

	if (iproc_host->data->pdata->quirks & SDHCI_QUIRK_MISSING_CAPS) {
		host->caps = iproc_host->data->caps;
		host->caps1 = iproc_host->data->caps1;
	}

	return sdhci_add_host(host);

err:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_iproc_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static struct platform_driver sdhci_iproc_driver = {
	.driver = {
		.name = "sdhci-iproc",
		.of_match_table = sdhci_iproc_of_match,
		.pm = SDHCI_PLTFM_PMOPS,
	},
	.probe = sdhci_iproc_probe,
	.remove = sdhci_iproc_remove,
};
module_platform_driver(sdhci_iproc_driver);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("IPROC SDHCI driver");
MODULE_LICENSE("GPL v2");
