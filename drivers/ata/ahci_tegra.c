/*
 * drivers/ata/ahci_tegra.c
 *
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Mikko Perttunen <mperttunen@nvidia.com>
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

#include <linux/ahci_platform.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <soc/tegra/fuse.h>
#include <soc/tegra/pmc.h>

#include "ahci.h"

#define DRV_NAME "tegra-ahci"

#define SATA_CONFIGURATION_0				0x180
#define SATA_CONFIGURATION_0_EN_FPCI			BIT(0)
#define SATA_CONFIGURATION_0_CLK_OVERRIDE			BIT(31)

#define SCFG_OFFSET					0x1000

#define T_SATA0_CFG_1					0x04
#define T_SATA0_CFG_1_IO_SPACE				BIT(0)
#define T_SATA0_CFG_1_MEMORY_SPACE			BIT(1)
#define T_SATA0_CFG_1_BUS_MASTER			BIT(2)
#define T_SATA0_CFG_1_SERR				BIT(8)

#define T_SATA0_CFG_9					0x24
#define T_SATA0_CFG_9_BASE_ADDRESS			0x40020000

#define SATA_FPCI_BAR5					0x94
#define SATA_FPCI_BAR5_START_MASK			(0xfffffff << 4)
#define SATA_FPCI_BAR5_START				(0x0040020 << 4)
#define SATA_FPCI_BAR5_ACCESS_TYPE			(0x1)

#define SATA_INTR_MASK					0x188
#define SATA_INTR_MASK_IP_INT_MASK			BIT(16)

#define T_SATA0_CFG_35					0x94
#define T_SATA0_CFG_35_IDP_INDEX_MASK			(0x7ff << 2)
#define T_SATA0_CFG_35_IDP_INDEX			(0x2a << 2)

#define T_SATA0_AHCI_IDP1				0x98
#define T_SATA0_AHCI_IDP1_DATA				(0x400040)

#define T_SATA0_CFG_PHY_1				0x12c
#define T_SATA0_CFG_PHY_1_PADS_IDDQ_EN			BIT(23)
#define T_SATA0_CFG_PHY_1_PAD_PLL_IDDQ_EN		BIT(22)

#define T_SATA0_NVOOB                                   0x114
#define T_SATA0_NVOOB_COMMA_CNT_MASK                    (0xff << 16)
#define T_SATA0_NVOOB_COMMA_CNT                         (0x07 << 16)
#define T_SATA0_NVOOB_SQUELCH_FILTER_MODE_MASK          (0x3 << 24)
#define T_SATA0_NVOOB_SQUELCH_FILTER_MODE               (0x1 << 24)
#define T_SATA0_NVOOB_SQUELCH_FILTER_LENGTH_MASK        (0x3 << 26)
#define T_SATA0_NVOOB_SQUELCH_FILTER_LENGTH             (0x3 << 26)

#define T_SATA_CFG_PHY_0                                0x120
#define T_SATA_CFG_PHY_0_USE_7BIT_ALIGN_DET_FOR_SPD     BIT(11)
#define T_SATA_CFG_PHY_0_MASK_SQUELCH                   BIT(24)

#define T_SATA0_CFG2NVOOB_2				0x134
#define T_SATA0_CFG2NVOOB_2_COMWAKE_IDLE_CNT_LOW_MASK	(0x1ff << 18)
#define T_SATA0_CFG2NVOOB_2_COMWAKE_IDLE_CNT_LOW	(0xc << 18)

#define T_SATA0_AHCI_HBA_CAP_BKDR			0x300
#define T_SATA0_AHCI_HBA_CAP_BKDR_PARTIAL_ST_CAP	BIT(13)
#define T_SATA0_AHCI_HBA_CAP_BKDR_SLUMBER_ST_CAP	BIT(14)
#define T_SATA0_AHCI_HBA_CAP_BKDR_SALP			BIT(26)
#define T_SATA0_AHCI_HBA_CAP_BKDR_SUPP_PM		BIT(17)
#define T_SATA0_AHCI_HBA_CAP_BKDR_SNCQ			BIT(30)

#define T_SATA0_BKDOOR_CC				0x4a4
#define T_SATA0_BKDOOR_CC_CLASS_CODE_MASK		(0xffff << 16)
#define T_SATA0_BKDOOR_CC_CLASS_CODE			(0x0106 << 16)
#define T_SATA0_BKDOOR_CC_PROG_IF_MASK			(0xff << 8)
#define T_SATA0_BKDOOR_CC_PROG_IF			(0x01 << 8)

#define T_SATA0_CFG_SATA				0x54c
#define T_SATA0_CFG_SATA_BACKDOOR_PROG_IF_EN		BIT(12)

#define T_SATA0_CFG_MISC				0x550

#define T_SATA0_INDEX					0x680

#define T_SATA0_CHX_PHY_CTRL1_GEN1			0x690
#define T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_MASK		0xff
#define T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_SHIFT		0
#define T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_MASK		(0xff << 8)
#define T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_SHIFT	8

#define T_SATA0_CHX_PHY_CTRL1_GEN2			0x694
#define T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_MASK		0xff
#define T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_SHIFT		0
#define T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_MASK		(0xff << 12)
#define T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_SHIFT	12

#define T_SATA0_CHX_PHY_CTRL2				0x69c
#define T_SATA0_CHX_PHY_CTRL2_CDR_CNTL_GEN1		0x23

#define T_SATA0_CHX_PHY_CTRL11				0x6d0
#define T_SATA0_CHX_PHY_CTRL11_GEN2_RX_EQ		(0x2800 << 16)

#define T_SATA0_CHX_PHY_CTRL17_0			0x6e8
#define T_SATA0_CHX_PHY_CTRL17_0_RX_EQ_CTRL_L_GEN1	0x55010000
#define T_SATA0_CHX_PHY_CTRL18_0			0x6ec
#define T_SATA0_CHX_PHY_CTRL18_0_RX_EQ_CTRL_L_GEN2	0x55010000
#define T_SATA0_CHX_PHY_CTRL20_0			0x6f4
#define T_SATA0_CHX_PHY_CTRL20_0_RX_EQ_CTRL_H_GEN1	0x1
#define T_SATA0_CHX_PHY_CTRL21_0			0x6f8
#define T_SATA0_CHX_PHY_CTRL21_0_RX_EQ_CTRL_H_GEN2	0x1

/* AUX Registers */
#define SATA_AUX_MISC_CNTL_1_0				0x8
#define SATA_AUX_MISC_CNTL_1_0_DEVSLP_OVERRIDE		BIT(17)
#define SATA_AUX_MISC_CNTL_1_0_SDS_SUPPORT		BIT(13)
#define SATA_AUX_MISC_CNTL_1_0_DESO_SUPPORT		BIT(15)

#define SATA_AUX_RX_STAT_INT_0				0xc
#define SATA_AUX_RX_STAT_INT_0_SATA_DEVSLP		BIT(7)

#define SATA_AUX_SPARE_CFG0_0				0x18
#define SATA_AUX_SPARE_CFG0_0_MDAT_TIMER_AFTER_PG_VALID	BIT(14)

#define FUSE_SATA_CALIB					0x124
#define FUSE_SATA_CALIB_MASK				0x3

struct sata_pad_calibration {
	u8 gen1_tx_amp;
	u8 gen1_tx_peak;
	u8 gen2_tx_amp;
	u8 gen2_tx_peak;
};

static const struct sata_pad_calibration tegra124_pad_calibration[] = {
	{0x18, 0x04, 0x18, 0x0a},
	{0x0e, 0x04, 0x14, 0x0a},
	{0x0e, 0x07, 0x1a, 0x0e},
	{0x14, 0x0e, 0x1a, 0x0e},
};

struct tegra_ahci_ops {
	int (*init)(struct ahci_host_priv *hpriv);
};

struct tegra_ahci_soc {
	const char *const		*supply_names;
	u32				num_supplies;
	bool				supports_devslp;
	const struct tegra_ahci_ops	*ops;
};

struct tegra_ahci_priv {
	struct platform_device	   *pdev;
	void __iomem		   *sata_regs;
	void __iomem		   *sata_aux_regs;
	struct reset_control	   *sata_rst;
	struct reset_control	   *sata_oob_rst;
	struct reset_control	   *sata_cold_rst;
	/* Needs special handling, cannot use ahci_platform */
	struct clk		   *sata_clk;
	struct regulator_bulk_data *supplies;
	const struct tegra_ahci_soc *soc;
};

static void tegra_ahci_handle_quirks(struct ahci_host_priv *hpriv)
{
	struct tegra_ahci_priv *tegra = hpriv->plat_data;
	u32 val;

	if (tegra->sata_aux_regs && !tegra->soc->supports_devslp) {
		val = readl(tegra->sata_aux_regs + SATA_AUX_MISC_CNTL_1_0);
		val &= ~SATA_AUX_MISC_CNTL_1_0_SDS_SUPPORT;
		writel(val, tegra->sata_aux_regs + SATA_AUX_MISC_CNTL_1_0);
	}
}

static int tegra124_ahci_init(struct ahci_host_priv *hpriv)
{
	struct tegra_ahci_priv *tegra = hpriv->plat_data;
	struct sata_pad_calibration calib;
	int ret;
	u32 val;

	/* Pad calibration */
	ret = tegra_fuse_readl(FUSE_SATA_CALIB, &val);
	if (ret)
		return ret;

	calib = tegra124_pad_calibration[val & FUSE_SATA_CALIB_MASK];

	writel(BIT(0), tegra->sata_regs + SCFG_OFFSET + T_SATA0_INDEX);

	val = readl(tegra->sata_regs +
		    SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL1_GEN1);
	val &= ~T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_MASK;
	val &= ~T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_MASK;
	val |= calib.gen1_tx_amp << T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_SHIFT;
	val |= calib.gen1_tx_peak << T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_SHIFT;
	writel(val, tegra->sata_regs + SCFG_OFFSET +
	       T_SATA0_CHX_PHY_CTRL1_GEN1);

	val = readl(tegra->sata_regs +
		    SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL1_GEN2);
	val &= ~T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_MASK;
	val &= ~T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_MASK;
	val |= calib.gen2_tx_amp << T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_SHIFT;
	val |= calib.gen2_tx_peak << T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_SHIFT;
	writel(val, tegra->sata_regs + SCFG_OFFSET +
	       T_SATA0_CHX_PHY_CTRL1_GEN2);

	writel(T_SATA0_CHX_PHY_CTRL11_GEN2_RX_EQ,
	       tegra->sata_regs + SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL11);
	writel(T_SATA0_CHX_PHY_CTRL2_CDR_CNTL_GEN1,
	       tegra->sata_regs + SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL2);

	writel(0, tegra->sata_regs + SCFG_OFFSET + T_SATA0_INDEX);

	return 0;
}

static int tegra_ahci_power_on(struct ahci_host_priv *hpriv)
{
	struct tegra_ahci_priv *tegra = hpriv->plat_data;
	int ret;

	ret = regulator_bulk_enable(tegra->soc->num_supplies,
				    tegra->supplies);
	if (ret)
		return ret;

	ret = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_SATA,
						tegra->sata_clk,
						tegra->sata_rst);
	if (ret)
		goto disable_regulators;

	reset_control_assert(tegra->sata_oob_rst);
	reset_control_assert(tegra->sata_cold_rst);

	ret = ahci_platform_enable_resources(hpriv);
	if (ret)
		goto disable_power;

	reset_control_deassert(tegra->sata_cold_rst);
	reset_control_deassert(tegra->sata_oob_rst);

	return 0;

disable_power:
	clk_disable_unprepare(tegra->sata_clk);

	tegra_powergate_power_off(TEGRA_POWERGATE_SATA);

disable_regulators:
	regulator_bulk_disable(tegra->soc->num_supplies, tegra->supplies);

	return ret;
}

static void tegra_ahci_power_off(struct ahci_host_priv *hpriv)
{
	struct tegra_ahci_priv *tegra = hpriv->plat_data;

	ahci_platform_disable_resources(hpriv);

	reset_control_assert(tegra->sata_rst);
	reset_control_assert(tegra->sata_oob_rst);
	reset_control_assert(tegra->sata_cold_rst);

	clk_disable_unprepare(tegra->sata_clk);
	tegra_powergate_power_off(TEGRA_POWERGATE_SATA);

	regulator_bulk_disable(tegra->soc->num_supplies, tegra->supplies);
}

static int tegra_ahci_controller_init(struct ahci_host_priv *hpriv)
{
	struct tegra_ahci_priv *tegra = hpriv->plat_data;
	int ret;
	u32 val;

	ret = tegra_ahci_power_on(hpriv);
	if (ret) {
		dev_err(&tegra->pdev->dev,
			"failed to power on AHCI controller: %d\n", ret);
		return ret;
	}

	/*
	 * Program the following SATA IPFS registers to allow SW accesses to
	 * SATA's MMIO register range.
	 */
	val = readl(tegra->sata_regs + SATA_FPCI_BAR5);
	val &= ~(SATA_FPCI_BAR5_START_MASK | SATA_FPCI_BAR5_ACCESS_TYPE);
	val |= SATA_FPCI_BAR5_START | SATA_FPCI_BAR5_ACCESS_TYPE;
	writel(val, tegra->sata_regs + SATA_FPCI_BAR5);

	/* Program the following SATA IPFS register to enable the SATA */
	val = readl(tegra->sata_regs + SATA_CONFIGURATION_0);
	val |= SATA_CONFIGURATION_0_EN_FPCI;
	writel(val, tegra->sata_regs + SATA_CONFIGURATION_0);

	/* Electrical settings for better link stability */
	val = T_SATA0_CHX_PHY_CTRL17_0_RX_EQ_CTRL_L_GEN1;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL17_0);
	val = T_SATA0_CHX_PHY_CTRL18_0_RX_EQ_CTRL_L_GEN2;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL18_0);
	val = T_SATA0_CHX_PHY_CTRL20_0_RX_EQ_CTRL_H_GEN1;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL20_0);
	val = T_SATA0_CHX_PHY_CTRL21_0_RX_EQ_CTRL_H_GEN2;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL21_0);

	/* For SQUELCH Filter & Gen3 drive getting detected as Gen1 drive */

	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA_CFG_PHY_0);
	val |= T_SATA_CFG_PHY_0_MASK_SQUELCH;
	val &= ~T_SATA_CFG_PHY_0_USE_7BIT_ALIGN_DET_FOR_SPD;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA_CFG_PHY_0);

	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_NVOOB);
	val &= ~(T_SATA0_NVOOB_COMMA_CNT_MASK |
		 T_SATA0_NVOOB_SQUELCH_FILTER_LENGTH_MASK |
		 T_SATA0_NVOOB_SQUELCH_FILTER_MODE_MASK);
	val |= (T_SATA0_NVOOB_COMMA_CNT |
		T_SATA0_NVOOB_SQUELCH_FILTER_LENGTH |
		T_SATA0_NVOOB_SQUELCH_FILTER_MODE);
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_NVOOB);

	/*
	 * Change CFG2NVOOB_2_COMWAKE_IDLE_CNT_LOW from 83.3 ns to 58.8ns
	 */
	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG2NVOOB_2);
	val &= ~T_SATA0_CFG2NVOOB_2_COMWAKE_IDLE_CNT_LOW_MASK;
	val |= T_SATA0_CFG2NVOOB_2_COMWAKE_IDLE_CNT_LOW;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG2NVOOB_2);

	if (tegra->soc->ops && tegra->soc->ops->init)
		tegra->soc->ops->init(hpriv);

	/*
	 * Program the following SATA configuration registers to
	 * initialize SATA
	 */
	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_1);
	val |= (T_SATA0_CFG_1_IO_SPACE | T_SATA0_CFG_1_MEMORY_SPACE |
		T_SATA0_CFG_1_BUS_MASTER | T_SATA0_CFG_1_SERR);
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_1);
	val = T_SATA0_CFG_9_BASE_ADDRESS;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_9);

	/* Program Class Code and Programming interface for SATA */
	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_SATA);
	val |= T_SATA0_CFG_SATA_BACKDOOR_PROG_IF_EN;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_SATA);

	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_BKDOOR_CC);
	val &=
	    ~(T_SATA0_BKDOOR_CC_CLASS_CODE_MASK |
	      T_SATA0_BKDOOR_CC_PROG_IF_MASK);
	val |= T_SATA0_BKDOOR_CC_CLASS_CODE | T_SATA0_BKDOOR_CC_PROG_IF;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_BKDOOR_CC);

	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_SATA);
	val &= ~T_SATA0_CFG_SATA_BACKDOOR_PROG_IF_EN;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_SATA);

	/* Enabling LPM capabilities through Backdoor Programming */
	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_AHCI_HBA_CAP_BKDR);
	val |= (T_SATA0_AHCI_HBA_CAP_BKDR_PARTIAL_ST_CAP |
		T_SATA0_AHCI_HBA_CAP_BKDR_SLUMBER_ST_CAP |
		T_SATA0_AHCI_HBA_CAP_BKDR_SALP |
		T_SATA0_AHCI_HBA_CAP_BKDR_SUPP_PM);
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_AHCI_HBA_CAP_BKDR);

	/* SATA Second Level Clock Gating configuration
	 * Enabling Gating of Tx/Rx clocks and driving Pad IDDQ and Lane
	 * IDDQ Signals
	 */
	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_35);
	val &= ~T_SATA0_CFG_35_IDP_INDEX_MASK;
	val |= T_SATA0_CFG_35_IDP_INDEX;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_35);

	val = T_SATA0_AHCI_IDP1_DATA;
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_AHCI_IDP1);

	val = readl(tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_PHY_1);
	val |= (T_SATA0_CFG_PHY_1_PADS_IDDQ_EN |
		T_SATA0_CFG_PHY_1_PAD_PLL_IDDQ_EN);
	writel(val, tegra->sata_regs + SCFG_OFFSET + T_SATA0_CFG_PHY_1);

	/* Enabling IPFS Clock Gating */
	val = readl(tegra->sata_regs + SATA_CONFIGURATION_0);
	val &= ~SATA_CONFIGURATION_0_CLK_OVERRIDE;
	writel(val, tegra->sata_regs + SATA_CONFIGURATION_0);

	tegra_ahci_handle_quirks(hpriv);

	/* Unmask SATA interrupts */

	val = readl(tegra->sata_regs + SATA_INTR_MASK);
	val |= SATA_INTR_MASK_IP_INT_MASK;
	writel(val, tegra->sata_regs + SATA_INTR_MASK);

	return 0;
}

static void tegra_ahci_controller_deinit(struct ahci_host_priv *hpriv)
{
	tegra_ahci_power_off(hpriv);
}

static void tegra_ahci_host_stop(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;

	tegra_ahci_controller_deinit(hpriv);
}

static struct ata_port_operations ahci_tegra_port_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= tegra_ahci_host_stop,
};

static const struct ata_port_info ahci_tegra_port_info = {
	.flags		= AHCI_FLAG_COMMON | ATA_FLAG_NO_DIPM,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_tegra_port_ops,
};

static const char *const tegra124_supply_names[] = {
	"avdd", "hvdd", "vddio", "target-5v", "target-12v"
};

static const struct tegra_ahci_ops tegra124_ahci_ops = {
	.init = tegra124_ahci_init,
};

static const struct tegra_ahci_soc tegra124_ahci_soc = {
	.supply_names = tegra124_supply_names,
	.num_supplies = ARRAY_SIZE(tegra124_supply_names),
	.supports_devslp = false,
	.ops = &tegra124_ahci_ops,
};

static const struct tegra_ahci_soc tegra210_ahci_soc = {
	.supports_devslp = false,
};

static const struct of_device_id tegra_ahci_of_match[] = {
	{
		.compatible = "nvidia,tegra124-ahci",
		.data = &tegra124_ahci_soc
	},
	{
		.compatible = "nvidia,tegra210-ahci",
		.data = &tegra210_ahci_soc
	},
	{}
};
MODULE_DEVICE_TABLE(of, tegra_ahci_of_match);

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int tegra_ahci_probe(struct platform_device *pdev)
{
	struct ahci_host_priv *hpriv;
	struct tegra_ahci_priv *tegra;
	struct resource *res;
	int ret;
	unsigned int i;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	hpriv->plat_data = tegra;

	tegra->pdev = pdev;
	tegra->soc = of_device_get_match_data(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	tegra->sata_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tegra->sata_regs))
		return PTR_ERR(tegra->sata_regs);

	/*
	 * AUX registers is optional.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (res) {
		tegra->sata_aux_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(tegra->sata_aux_regs))
			return PTR_ERR(tegra->sata_aux_regs);
	}

	tegra->sata_rst = devm_reset_control_get(&pdev->dev, "sata");
	if (IS_ERR(tegra->sata_rst)) {
		dev_err(&pdev->dev, "Failed to get sata reset\n");
		return PTR_ERR(tegra->sata_rst);
	}

	tegra->sata_oob_rst = devm_reset_control_get(&pdev->dev, "sata-oob");
	if (IS_ERR(tegra->sata_oob_rst)) {
		dev_err(&pdev->dev, "Failed to get sata-oob reset\n");
		return PTR_ERR(tegra->sata_oob_rst);
	}

	tegra->sata_cold_rst = devm_reset_control_get(&pdev->dev, "sata-cold");
	if (IS_ERR(tegra->sata_cold_rst)) {
		dev_err(&pdev->dev, "Failed to get sata-cold reset\n");
		return PTR_ERR(tegra->sata_cold_rst);
	}

	tegra->sata_clk = devm_clk_get(&pdev->dev, "sata");
	if (IS_ERR(tegra->sata_clk)) {
		dev_err(&pdev->dev, "Failed to get sata clock\n");
		return PTR_ERR(tegra->sata_clk);
	}

	tegra->supplies = devm_kcalloc(&pdev->dev,
				       tegra->soc->num_supplies,
				       sizeof(*tegra->supplies), GFP_KERNEL);
	if (!tegra->supplies)
		return -ENOMEM;

	for (i = 0; i < tegra->soc->num_supplies; i++)
		tegra->supplies[i].supply = tegra->soc->supply_names[i];

	ret = devm_regulator_bulk_get(&pdev->dev,
				      tegra->soc->num_supplies,
				      tegra->supplies);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get regulators\n");
		return ret;
	}

	ret = tegra_ahci_controller_init(hpriv);
	if (ret)
		return ret;

	ret = ahci_platform_init_host(pdev, hpriv, &ahci_tegra_port_info,
				      &ahci_platform_sht);
	if (ret)
		goto deinit_controller;

	return 0;

deinit_controller:
	tegra_ahci_controller_deinit(hpriv);

	return ret;
};

static struct platform_driver tegra_ahci_driver = {
	.probe = tegra_ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = tegra_ahci_of_match,
	},
	/* LP0 suspend support not implemented */
};
module_platform_driver(tegra_ahci_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_DESCRIPTION("Tegra AHCI SATA driver");
MODULE_LICENSE("GPL v2");
