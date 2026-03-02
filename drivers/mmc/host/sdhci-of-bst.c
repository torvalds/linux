// SPDX-License-Identifier: GPL-2.0+
/*
 * SDHCI driver for Black Sesame Technologies C1200 controller
 *
 * Copyright (c) 2025 Black Sesame Technologies
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include "sdhci.h"
#include "sdhci-pltfm.h"

/* SDHCI register extensions */
#define SDHCI_CLOCK_PLL_EN		0x0008
#define SDHCI_VENDOR_PTR_R		0xE8

/* BST-specific tuning parameters */
#define BST_TUNING_COUNT		0x20

/* Synopsys vendor specific registers */
#define SDHC_EMMC_CTRL_R_OFFSET		0x2C
#define MBIU_CTRL			0x510

/* MBIU burst control bits */
#define BURST_INCR16_EN			BIT(3)
#define BURST_INCR8_EN			BIT(2)
#define BURST_INCR4_EN			BIT(1)
#define BURST_EN			(BURST_INCR16_EN | BURST_INCR8_EN | BURST_INCR4_EN)
#define MBIU_BURST_MASK			GENMASK(3, 0)

/* CRM (Clock/Reset/Management) register offsets */
#define SDEMMC_CRM_BCLK_DIV_CTRL	0x08
#define SDEMMC_CRM_TIMER_DIV_CTRL	0x0C
#define SDEMMC_CRM_RX_CLK_CTRL		0x14
#define SDEMMC_CRM_VOL_CTRL		0x1C
#define REG_WR_PROTECT			0x88
#define DELAY_CHAIN_SEL			0x94

/* CRM register values and bit definitions */
#define REG_WR_PROTECT_KEY		0x1234abcd
#define BST_VOL_STABLE_ON		BIT(7)
#define BST_TIMER_DIV_MASK		GENMASK(7, 0)
#define BST_TIMER_DIV_VAL		0x20
#define BST_TIMER_LOAD_BIT		BIT(8)
#define BST_BCLK_EN_BIT			BIT(10)
#define BST_RX_UPDATE_BIT		BIT(11)
#define BST_EMMC_CTRL_RST_N		BIT(2)	/* eMMC card reset control */

/* Clock frequency limits */
#define BST_DEFAULT_MAX_FREQ		200000000UL	/* 200 MHz */
#define BST_DEFAULT_MIN_FREQ		400000UL	/* 400 kHz */

/* Clock control bit definitions */
#define BST_CLOCK_DIV_MASK		GENMASK(7, 0)
#define BST_CLOCK_DIV_SHIFT		8
#define BST_BCLK_DIV_MASK		GENMASK(9, 0)

/* Clock frequency thresholds */
#define BST_CLOCK_THRESHOLD_LOW		1500

/* Clock stability polling parameters */
#define BST_CLK_STABLE_POLL_US		1000	/* Poll interval in microseconds */
#define BST_CLK_STABLE_TIMEOUT_US	20000	/* Timeout for internal clock stabilization (us) */

struct sdhci_bst_priv {
	void __iomem *crm_reg_base;
};

union sdhci_bst_rx_ctrl {
	struct {
		u32 rx_revert:1,
		    rx_clk_sel_sec:1,
		    rx_clk_div:4,
		    rx_clk_phase_inner:2,
		    rx_clk_sel_first:1,
		    rx_clk_phase_out:2,
		    rx_clk_en:1,
		    res0:20;
	};
	u32 reg;
};

static u32 sdhci_bst_crm_read(struct sdhci_pltfm_host *pltfm_host, u32 offset)
{
	struct sdhci_bst_priv *priv = sdhci_pltfm_priv(pltfm_host);

	return readl(priv->crm_reg_base + offset);
}

static void sdhci_bst_crm_write(struct sdhci_pltfm_host *pltfm_host, u32 offset, u32 value)
{
	struct sdhci_bst_priv *priv = sdhci_pltfm_priv(pltfm_host);

	writel(value, priv->crm_reg_base + offset);
}

static int sdhci_bst_wait_int_clk(struct sdhci_host *host)
{
	u16 clk;

	if (read_poll_timeout(sdhci_readw, clk, (clk & SDHCI_CLOCK_INT_STABLE),
			      BST_CLK_STABLE_POLL_US, BST_CLK_STABLE_TIMEOUT_US, false,
			      host, SDHCI_CLOCK_CONTROL))
		return -EBUSY;
	return 0;
}

static unsigned int sdhci_bst_get_max_clock(struct sdhci_host *host)
{
	return BST_DEFAULT_MAX_FREQ;
}

static unsigned int sdhci_bst_get_min_clock(struct sdhci_host *host)
{
	return BST_DEFAULT_MIN_FREQ;
}

static void sdhci_bst_enable_clk(struct sdhci_host *host, unsigned int clk)
{
	struct sdhci_pltfm_host *pltfm_host;
	unsigned int div;
	u32 val;
	union sdhci_bst_rx_ctrl rx_reg;

	pltfm_host = sdhci_priv(host);

	/* Calculate clock divider based on target frequency */
	if (clk == 0) {
		div = 0;
	} else if (clk < BST_DEFAULT_MIN_FREQ) {
		/* Below minimum: use max divider to get closest to min freq */
		div = BST_DEFAULT_MAX_FREQ / BST_DEFAULT_MIN_FREQ;
	} else if (clk <= BST_DEFAULT_MAX_FREQ) {
		/* Normal range: calculate divider directly */
		div = BST_DEFAULT_MAX_FREQ / clk;
	} else {
		/* Above maximum: no division needed */
		div = 1;
	}

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	clk &= ~SDHCI_CLOCK_PLL_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_TIMER_DIV_CTRL);
	val &= ~BST_TIMER_LOAD_BIT;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_TIMER_DIV_CTRL, val);

	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_TIMER_DIV_CTRL);
	val &= ~BST_TIMER_DIV_MASK;
	val |= BST_TIMER_DIV_VAL;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_TIMER_DIV_CTRL, val);

	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_TIMER_DIV_CTRL);
	val |= BST_TIMER_LOAD_BIT;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_TIMER_DIV_CTRL, val);

	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL);
	val &= ~BST_RX_UPDATE_BIT;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL, val);

	rx_reg.reg = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL);

	rx_reg.rx_revert = 0;
	rx_reg.rx_clk_sel_sec = 1;
	rx_reg.rx_clk_div = 4;
	rx_reg.rx_clk_phase_inner = 2;
	rx_reg.rx_clk_sel_first = 0;
	rx_reg.rx_clk_phase_out = 2;

	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL, rx_reg.reg);

	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL);
	val |= BST_RX_UPDATE_BIT;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL, val);

	/* Disable clock first */
	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL);
	val &= ~BST_BCLK_EN_BIT;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL, val);

	/* Setup clock divider */
	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL);
	val &= ~BST_BCLK_DIV_MASK;
	val |= div;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL, val);

	/* Enable clock */
	val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL);
	val |= BST_BCLK_EN_BIT;
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL, val);

	/* RMW the clock divider bits to avoid clobbering other fields */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~(BST_CLOCK_DIV_MASK << BST_CLOCK_DIV_SHIFT);
	clk |= (div & BST_CLOCK_DIV_MASK) << BST_CLOCK_DIV_SHIFT;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk |= SDHCI_CLOCK_PLL_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	clk |= SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}

static void sdhci_bst_set_clock(struct sdhci_host *host, unsigned int clock)
{
	/* Turn off card/internal/PLL clocks when clock==0 to avoid idle power */
	u32 clk_reg = sdhci_readw(host, SDHCI_CLOCK_CONTROL);

	if (!clock) {
		clk_reg &= ~(SDHCI_CLOCK_CARD_EN | SDHCI_CLOCK_INT_EN | SDHCI_CLOCK_PLL_EN);
		sdhci_writew(host, clk_reg, SDHCI_CLOCK_CONTROL);
		return;
	}
	sdhci_bst_enable_clk(host, clock);
}

/*
 * sdhci_bst_reset - Reset the SDHCI host controller with special
 * handling for eMMC card reset control.
 */
static void sdhci_bst_reset(struct sdhci_host *host, u8 mask)
{
	u16 vendor_ptr, emmc_ctrl_reg;
	u32 reg;

	if (host->mmc->caps2 & MMC_CAP2_NO_SD) {
		vendor_ptr = sdhci_readw(host, SDHCI_VENDOR_PTR_R);
		emmc_ctrl_reg = vendor_ptr + SDHC_EMMC_CTRL_R_OFFSET;

		reg = sdhci_readw(host, emmc_ctrl_reg);
		reg &= ~BST_EMMC_CTRL_RST_N;
		sdhci_writew(host, reg, emmc_ctrl_reg);
		sdhci_reset(host, mask);
		usleep_range(10, 20);
		reg = sdhci_readw(host, emmc_ctrl_reg);
		reg |= BST_EMMC_CTRL_RST_N;
		sdhci_writew(host, reg, emmc_ctrl_reg);
	} else {
		sdhci_reset(host, mask);
	}
}

/* Set timeout control register to maximum value (0xE) */
static void sdhci_bst_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	sdhci_writeb(host, 0xE, SDHCI_TIMEOUT_CONTROL);
}

/*
 * sdhci_bst_set_power - Set power mode and voltage, also configures
 * MBIU burst mode control based on power state.
 */
static void sdhci_bst_set_power(struct sdhci_host *host, unsigned char mode,
				unsigned short vdd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	u32 reg;
	u32 val;

	sdhci_set_power(host, mode, vdd);

	if (mode == MMC_POWER_OFF) {
		/* Disable MBIU burst mode */
		reg = sdhci_readw(host, MBIU_CTRL);
		reg &= ~BURST_EN; /* Clear all burst enable bits */
		sdhci_writew(host, reg, MBIU_CTRL);

		/* Disable CRM BCLK */
		val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL);
		val &= ~BST_BCLK_EN_BIT;
		sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_BCLK_DIV_CTRL, val);

		/* Disable RX clock */
		val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL);
		val &= ~BST_RX_UPDATE_BIT;
		sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_RX_CLK_CTRL, val);

		/* Turn off voltage stable power */
		val = sdhci_bst_crm_read(pltfm_host, SDEMMC_CRM_VOL_CTRL);
		val &= ~BST_VOL_STABLE_ON;
		sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_VOL_CTRL, val);
	} else {
		/* Configure burst mode only when powered on */
		reg = sdhci_readw(host, MBIU_CTRL);
		reg &= ~MBIU_BURST_MASK; /* Clear burst related bits */
		reg |= BURST_EN; /* Enable burst mode for better bandwidth */
		sdhci_writew(host, reg, MBIU_CTRL);
	}
}

/*
 * sdhci_bst_execute_tuning - Execute tuning procedure by trying different
 * delay chain values and selecting the optimal one.
 */
static int sdhci_bst_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host;
	int ret = 0, error;
	int first_start = -1, first_end = -1, best = 0;
	int second_start = -1, second_end = -1, has_failure = 0;
	int i;

	pltfm_host = sdhci_priv(host);

	for (i = 0; i < BST_TUNING_COUNT; i++) {
		/* Protected write */
		sdhci_bst_crm_write(pltfm_host, REG_WR_PROTECT, REG_WR_PROTECT_KEY);
		/* Write tuning value */
		sdhci_bst_crm_write(pltfm_host, DELAY_CHAIN_SEL, (1ul << i) - 1);

		/* Wait for internal clock stable before tuning */
		if (sdhci_bst_wait_int_clk(host)) {
			dev_err(mmc_dev(host->mmc), "Internal clock never stabilised\n");
			return -EBUSY;
		}

		ret = mmc_send_tuning(host->mmc, opcode, &error);
		if (ret != 0) {
			has_failure = 1;
		} else {
			if (has_failure == 0) {
				if (first_start == -1)
					first_start = i;
				first_end = i;
			} else {
				if (second_start == -1)
					second_start = i;
				second_end = i;
			}
		}
	}

	/* Calculate best tuning value */
	if (first_end - first_start >= second_end - second_start)
		best = ((first_end - first_start) >> 1) + first_start;
	else
		best = ((second_end - second_start) >> 1) + second_start;

	if (best < 0)
		best = 0;

	sdhci_bst_crm_write(pltfm_host, DELAY_CHAIN_SEL, (1ul << best) - 1);
	/* Confirm internal clock stable after setting best tuning value */
	if (sdhci_bst_wait_int_clk(host)) {
		dev_err(mmc_dev(host->mmc), "Internal clock never stabilised\n");
		return -EBUSY;
	}

	return 0;
}

/* Enable voltage stable power for voltage switch */
static void sdhci_bst_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	/* Enable voltage stable power */
	sdhci_bst_crm_write(pltfm_host, SDEMMC_CRM_VOL_CTRL, BST_VOL_STABLE_ON);
}

static const struct sdhci_ops sdhci_bst_ops = {
	.set_clock		= sdhci_bst_set_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.set_uhs_signaling	= sdhci_set_uhs_signaling,
	.get_min_clock		= sdhci_bst_get_min_clock,
	.get_max_clock		= sdhci_bst_get_max_clock,
	.reset			= sdhci_bst_reset,
	.set_power		= sdhci_bst_set_power,
	.set_timeout		= sdhci_bst_set_timeout,
	.platform_execute_tuning = sdhci_bst_execute_tuning,
	.voltage_switch		= sdhci_bst_voltage_switch,
};

static const struct sdhci_pltfm_data sdhci_bst_pdata = {
	.ops = &sdhci_bst_ops,
	.quirks = SDHCI_QUIRK_BROKEN_ADMA |
		  SDHCI_QUIRK_DELAY_AFTER_POWER |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_INVERTED_WRITE_PROTECT,
	.quirks2 = SDHCI_QUIRK2_BROKEN_DDR50 |
		   SDHCI_QUIRK2_TUNING_WORK_AROUND |
		   SDHCI_QUIRK2_ACMD23_BROKEN,
};

static void sdhci_bst_free_bounce_buffer(struct sdhci_host *host)
{
	if (host->bounce_buffer) {
		dma_free_coherent(mmc_dev(host->mmc), host->bounce_buffer_size,
				  host->bounce_buffer, host->bounce_addr);
		host->bounce_buffer = NULL;
	}
	of_reserved_mem_device_release(mmc_dev(host->mmc));
}

static int sdhci_bst_alloc_bounce_buffer(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	unsigned int bounce_size;
	int ret;

	/* Fixed SRAM bounce size to 32KB: verified config under 32-bit DMA addressing limit */
	bounce_size = SZ_32K;

	ret = of_reserved_mem_device_init_by_idx(mmc_dev(mmc), mmc_dev(mmc)->of_node, 0);
	if (ret) {
		dev_err(mmc_dev(mmc), "Failed to initialize reserved memory\n");
		return ret;
	}

	host->bounce_buffer = dma_alloc_coherent(mmc_dev(mmc), bounce_size,
						 &host->bounce_addr, GFP_KERNEL);
	if (!host->bounce_buffer) {
		of_reserved_mem_device_release(mmc_dev(mmc));
		return -ENOMEM;
	}

	host->bounce_buffer_size = bounce_size;

	return 0;
}

static int sdhci_bst_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	struct sdhci_bst_priv *priv;
	int err;

	host = sdhci_pltfm_init(pdev, &sdhci_bst_pdata, sizeof(struct sdhci_bst_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host); /* Get platform private data */

	err = mmc_of_parse(host->mmc);
	if (err)
		return err;

	sdhci_get_of_property(pdev);

	/* Get CRM registers from the second reg entry */
	priv->crm_reg_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->crm_reg_base)) {
		err = PTR_ERR(priv->crm_reg_base);
		return err;
	}

	/*
	 * Silicon constraints for BST C1200:
	 * - System RAM base is 0x800000000 (above 32-bit addressable range)
	 * - The eMMC controller DMA engine is limited to 32-bit addressing
	 * - SMMU cannot be used on this path due to hardware design flaws
	 * - These are fixed in silicon and cannot be changed in software
	 *
	 * Bus/controller mapping:
	 * - No registers are available to reprogram the address mapping
	 * - The 32-bit DMA limit is a hard constraint of the controller IP
	 *
	 * Given these constraints, an SRAM-based bounce buffer in the 32-bit
	 * address space is required to enable eMMC DMA on this platform.
	 */
	err = sdhci_bst_alloc_bounce_buffer(host);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate bounce buffer: %d\n", err);
		return err;
	}

	err = sdhci_add_host(host);
	if (err)
		goto err_free_bounce_buffer;

	return 0;

err_free_bounce_buffer:
	sdhci_bst_free_bounce_buffer(host);

	return err;
}

static void sdhci_bst_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);

	sdhci_bst_free_bounce_buffer(host);
	sdhci_pltfm_remove(pdev);
}

static const struct of_device_id sdhci_bst_ids[] = {
	{ .compatible = "bst,c1200-sdhci" },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_bst_ids);

static struct platform_driver sdhci_bst_driver = {
	.driver = {
		.name = "sdhci-bst",
		.of_match_table = sdhci_bst_ids,
	},
	.probe = sdhci_bst_probe,
	.remove = sdhci_bst_remove,
};
module_platform_driver(sdhci_bst_driver);

MODULE_DESCRIPTION("Black Sesame Technologies SDHCI driver (BST)");
MODULE_AUTHOR("Black Sesame Technologies Co., Ltd.");
MODULE_LICENSE("GPL");
