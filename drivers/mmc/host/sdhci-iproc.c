// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2014 Broadcom Corporation

/*
 * iProc SDHCI platform driver
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "sdhci-pltfm.h"

struct sdhci_iproc_data {
	const struct sdhci_pltfm_data *pdata;
	u32 caps;
	u32 caps1;
	u32 mmc_caps;
	bool missing_caps;
};

struct sdhci_iproc_host {
	const struct sdhci_iproc_data *data;
	u32 shadow_cmd;
	u32 shadow_blk;
	bool is_cmd_shadowed;
	bool is_blk_shadowed;
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
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_iproc_host *iproc_host = sdhci_pltfm_priv(pltfm_host);
	u32 val;
	u16 word;

	if ((reg == SDHCI_TRANSFER_MODE) && iproc_host->is_cmd_shadowed) {
		/* Get the saved transfer mode */
		val = iproc_host->shadow_cmd;
	} else if ((reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) &&
		   iproc_host->is_blk_shadowed) {
		/* Get the saved block info */
		val = iproc_host->shadow_blk;
	} else {
		val = sdhci_iproc_readl(host, (reg & ~3));
	}
	word = val >> REG_OFFSET_IN_BITS(reg) & 0xffff;
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
	struct sdhci_iproc_host *iproc_host = sdhci_pltfm_priv(pltfm_host);
	u32 word_shift = REG_OFFSET_IN_BITS(reg);
	u32 mask = 0xffff << word_shift;
	u32 oldval, newval;

	if (reg == SDHCI_COMMAND) {
		/* Write the block now as we are issuing a command */
		if (iproc_host->is_blk_shadowed) {
			sdhci_iproc_writel(host, iproc_host->shadow_blk,
				SDHCI_BLOCK_SIZE);
			iproc_host->is_blk_shadowed = false;
		}
		oldval = iproc_host->shadow_cmd;
		iproc_host->is_cmd_shadowed = false;
	} else if ((reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) &&
		   iproc_host->is_blk_shadowed) {
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
		iproc_host->is_cmd_shadowed = true;
	} else if (reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) {
		/* Save the block info until the command is issued */
		iproc_host->shadow_blk = newval;
		iproc_host->is_blk_shadowed = true;
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

static unsigned int sdhci_iproc_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	if (pltfm_host->clk)
		return sdhci_pltfm_clk_get_max_clock(host);
	else
		return pltfm_host->clock;
}

/*
 * There is a known bug on BCM2711's SDHCI core integration where the
 * controller will hang when the difference between the core clock and the bus
 * clock is too great. Specifically this can be reproduced under the following
 * conditions:
 *
 *  - No SD card plugged in, polling thread is running, probing cards at
 *    100 kHz.
 *  - BCM2711's core clock configured at 500MHz or more
 *
 * So we set 200kHz as the minimum clock frequency available for that SoC.
 */
static unsigned int sdhci_iproc_bcm2711_get_min_clock(struct sdhci_host *host)
{
	return 200000;
}

static const struct sdhci_ops sdhci_iproc_ops = {
	.set_clock = sdhci_set_clock,
	.get_max_clock = sdhci_iproc_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_ops sdhci_iproc_32only_ops = {
	.read_l = sdhci_iproc_readl,
	.read_w = sdhci_iproc_readw,
	.read_b = sdhci_iproc_readb,
	.write_l = sdhci_iproc_writel,
	.write_w = sdhci_iproc_writew,
	.write_b = sdhci_iproc_writeb,
	.set_clock = sdhci_set_clock,
	.get_max_clock = sdhci_iproc_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_iproc_cygnus_pltfm_data = {
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_NO_HISPD_BIT,
	.quirks2 = SDHCI_QUIRK2_ACMD23_BROKEN | SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.ops = &sdhci_iproc_32only_ops,
};

static const struct sdhci_iproc_data iproc_cygnus_data = {
	.pdata = &sdhci_iproc_cygnus_pltfm_data,
	.caps = ((0x1 << SDHCI_MAX_BLOCK_SHIFT)
			& SDHCI_MAX_BLOCK_MASK) |
		SDHCI_CAN_VDD_330 |
		SDHCI_CAN_VDD_180 |
		SDHCI_CAN_DO_SUSPEND |
		SDHCI_CAN_DO_HISPD |
		SDHCI_CAN_DO_ADMA2 |
		SDHCI_CAN_DO_SDMA,
	.caps1 = SDHCI_DRIVER_TYPE_C |
		 SDHCI_DRIVER_TYPE_D |
		 SDHCI_SUPPORT_DDR50,
	.mmc_caps = MMC_CAP_1_8V_DDR,
};

static const struct sdhci_pltfm_data sdhci_iproc_pltfm_data = {
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12 |
		  SDHCI_QUIRK_NO_HISPD_BIT,
	.quirks2 = SDHCI_QUIRK2_ACMD23_BROKEN,
	.ops = &sdhci_iproc_ops,
};

static const struct sdhci_iproc_data iproc_data = {
	.pdata = &sdhci_iproc_pltfm_data,
	.caps = ((0x1 << SDHCI_MAX_BLOCK_SHIFT)
			& SDHCI_MAX_BLOCK_MASK) |
		SDHCI_CAN_VDD_330 |
		SDHCI_CAN_VDD_180 |
		SDHCI_CAN_DO_SUSPEND |
		SDHCI_CAN_DO_HISPD |
		SDHCI_CAN_DO_ADMA2 |
		SDHCI_CAN_DO_SDMA,
	.caps1 = SDHCI_DRIVER_TYPE_C |
		 SDHCI_DRIVER_TYPE_D |
		 SDHCI_SUPPORT_DDR50,
};

static const struct sdhci_pltfm_data sdhci_bcm2835_pltfm_data = {
	.quirks = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_NO_HISPD_BIT,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &sdhci_iproc_32only_ops,
};

static const struct sdhci_iproc_data bcm2835_data = {
	.pdata = &sdhci_bcm2835_pltfm_data,
	.caps = ((0x1 << SDHCI_MAX_BLOCK_SHIFT)
			& SDHCI_MAX_BLOCK_MASK) |
		SDHCI_CAN_VDD_330 |
		SDHCI_CAN_DO_HISPD,
	.caps1 = SDHCI_DRIVER_TYPE_A |
		 SDHCI_DRIVER_TYPE_C,
	.mmc_caps = 0x00000000,
	.missing_caps = true,
};

static const struct sdhci_ops sdhci_iproc_bcm2711_ops = {
	.read_l = sdhci_iproc_readl,
	.read_w = sdhci_iproc_readw,
	.read_b = sdhci_iproc_readb,
	.write_l = sdhci_iproc_writel,
	.write_w = sdhci_iproc_writew,
	.write_b = sdhci_iproc_writeb,
	.set_clock = sdhci_set_clock,
	.set_power = sdhci_set_power_and_bus_voltage,
	.get_max_clock = sdhci_iproc_get_max_clock,
	.get_min_clock = sdhci_iproc_bcm2711_get_min_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_bcm2711_pltfm_data = {
	.quirks = SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.ops = &sdhci_iproc_bcm2711_ops,
};

static const struct sdhci_iproc_data bcm2711_data = {
	.pdata = &sdhci_bcm2711_pltfm_data,
	.mmc_caps = MMC_CAP_3_3V_DDR,
};

static const struct sdhci_pltfm_data sdhci_bcm7211a0_pltfm_data = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_BROKEN_DMA |
		SDHCI_QUIRK_BROKEN_ADMA,
	.ops = &sdhci_iproc_ops,
};

#define BCM7211A0_BASE_CLK_MHZ 100
static const struct sdhci_iproc_data bcm7211a0_data = {
	.pdata = &sdhci_bcm7211a0_pltfm_data,
	.caps = ((BCM7211A0_BASE_CLK_MHZ / 2) << SDHCI_TIMEOUT_CLK_SHIFT) |
		(BCM7211A0_BASE_CLK_MHZ << SDHCI_CLOCK_BASE_SHIFT) |
		((0x2 << SDHCI_MAX_BLOCK_SHIFT)
			& SDHCI_MAX_BLOCK_MASK) |
		SDHCI_CAN_VDD_330 |
		SDHCI_CAN_VDD_180 |
		SDHCI_CAN_DO_SUSPEND |
		SDHCI_CAN_DO_HISPD,
	.caps1 = SDHCI_DRIVER_TYPE_C |
		 SDHCI_DRIVER_TYPE_D,
	.missing_caps = true,
};

static const struct of_device_id sdhci_iproc_of_match[] = {
	{ .compatible = "brcm,bcm2835-sdhci", .data = &bcm2835_data },
	{ .compatible = "brcm,bcm2711-emmc2", .data = &bcm2711_data },
	{ .compatible = "brcm,sdhci-iproc-cygnus", .data = &iproc_cygnus_data},
	{ .compatible = "brcm,sdhci-iproc", .data = &iproc_data },
	{ .compatible = "brcm,bcm7211a0-sdhci", .data = &bcm7211a0_data },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_iproc_of_match);

#ifdef CONFIG_ACPI
/*
 * This is a duplicate of bcm2835_(pltfrm_)data without caps quirks
 * which are provided by the ACPI table.
 */
static const struct sdhci_pltfm_data sdhci_bcm_arasan_data = {
	.quirks = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_NO_HISPD_BIT,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &sdhci_iproc_32only_ops,
};

static const struct sdhci_iproc_data bcm_arasan_data = {
	.pdata = &sdhci_bcm_arasan_data,
};

static const struct acpi_device_id sdhci_iproc_acpi_ids[] = {
	{ .id = "BRCM5871", .driver_data = (kernel_ulong_t)&iproc_cygnus_data },
	{ .id = "BRCM5872", .driver_data = (kernel_ulong_t)&iproc_data },
	{ .id = "BCM2847",  .driver_data = (kernel_ulong_t)&bcm_arasan_data },
	{ .id = "BRCME88C", .driver_data = (kernel_ulong_t)&bcm2711_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, sdhci_iproc_acpi_ids);
#endif

static int sdhci_iproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sdhci_iproc_data *iproc_data = NULL;
	struct sdhci_host *host;
	struct sdhci_iproc_host *iproc_host;
	struct sdhci_pltfm_host *pltfm_host;
	int ret;

	iproc_data = device_get_match_data(dev);
	if (!iproc_data)
		return -ENODEV;

	host = sdhci_pltfm_init(pdev, iproc_data->pdata, sizeof(*iproc_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	iproc_host = sdhci_pltfm_priv(pltfm_host);

	iproc_host->data = iproc_data;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err;

	sdhci_get_property(pdev);

	host->mmc->caps |= iproc_host->data->mmc_caps;

	if (dev->of_node) {
		pltfm_host->clk = devm_clk_get_enabled(dev, NULL);
		if (IS_ERR(pltfm_host->clk)) {
			ret = PTR_ERR(pltfm_host->clk);
			goto err;
		}
	}

	if (iproc_host->data->missing_caps) {
		__sdhci_read_caps(host, NULL,
				  &iproc_host->data->caps,
				  &iproc_host->data->caps1);
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err;

	return 0;

err:
	sdhci_pltfm_free(pdev);
	return ret;
}

static void sdhci_iproc_shutdown(struct platform_device *pdev)
{
	sdhci_pltfm_suspend(&pdev->dev);
}

static struct platform_driver sdhci_iproc_driver = {
	.driver = {
		.name = "sdhci-iproc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sdhci_iproc_of_match,
		.acpi_match_table = ACPI_PTR(sdhci_iproc_acpi_ids),
		.pm = &sdhci_pltfm_pmops,
	},
	.probe = sdhci_iproc_probe,
	.remove_new = sdhci_pltfm_remove,
	.shutdown = sdhci_iproc_shutdown,
};
module_platform_driver(sdhci_iproc_driver);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("IPROC SDHCI driver");
MODULE_LICENSE("GPL v2");
