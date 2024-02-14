// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * phy-ti-pipe3 - PIPE3 PHY driver.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/phy/phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/phy/omap_control_phy.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define	PLL_STATUS		0x00000004
#define	PLL_GO			0x00000008
#define	PLL_CONFIGURATION1	0x0000000C
#define	PLL_CONFIGURATION2	0x00000010
#define	PLL_CONFIGURATION3	0x00000014
#define	PLL_CONFIGURATION4	0x00000020

#define	PLL_REGM_MASK		0x001FFE00
#define	PLL_REGM_SHIFT		0x9
#define	PLL_REGM_F_MASK		0x0003FFFF
#define	PLL_REGM_F_SHIFT	0x0
#define	PLL_REGN_MASK		0x000001FE
#define	PLL_REGN_SHIFT		0x1
#define	PLL_SELFREQDCO_MASK	0x0000000E
#define	PLL_SELFREQDCO_SHIFT	0x1
#define	PLL_SD_MASK		0x0003FC00
#define	PLL_SD_SHIFT		10
#define	SET_PLL_GO		0x1
#define PLL_LDOPWDN		BIT(15)
#define PLL_TICOPWDN		BIT(16)
#define	PLL_LOCK		0x2
#define	PLL_IDLE		0x1

#define SATA_PLL_SOFT_RESET	BIT(18)

#define PIPE3_PHY_PWRCTL_CLK_CMD_MASK	GENMASK(21, 14)
#define PIPE3_PHY_PWRCTL_CLK_CMD_SHIFT	14

#define PIPE3_PHY_PWRCTL_CLK_FREQ_MASK	GENMASK(31, 22)
#define PIPE3_PHY_PWRCTL_CLK_FREQ_SHIFT	22

#define PIPE3_PHY_RX_POWERON       (0x1 << PIPE3_PHY_PWRCTL_CLK_CMD_SHIFT)
#define PIPE3_PHY_TX_POWERON       (0x2 << PIPE3_PHY_PWRCTL_CLK_CMD_SHIFT)

#define PCIE_PCS_MASK			0xFF0000
#define PCIE_PCS_DELAY_COUNT_SHIFT	0x10

#define PIPE3_PHY_RX_ANA_PROGRAMMABILITY	0x0000000C
#define INTERFACE_MASK			GENMASK(31, 27)
#define INTERFACE_SHIFT			27
#define INTERFACE_MODE_USBSS		BIT(4)
#define INTERFACE_MODE_SATA_1P5		BIT(3)
#define INTERFACE_MODE_SATA_3P0		BIT(2)
#define INTERFACE_MODE_PCIE		BIT(0)

#define LOSD_MASK			GENMASK(17, 14)
#define LOSD_SHIFT			14
#define MEM_PLLDIV			GENMASK(6, 5)

#define PIPE3_PHY_RX_TRIM		0x0000001C
#define MEM_DLL_TRIM_SEL_MASK		GENMASK(31, 30)
#define MEM_DLL_TRIM_SHIFT		30

#define PIPE3_PHY_RX_DLL		0x00000024
#define MEM_DLL_PHINT_RATE_MASK		GENMASK(31, 30)
#define MEM_DLL_PHINT_RATE_SHIFT	30

#define PIPE3_PHY_RX_DIGITAL_MODES		0x00000028
#define MEM_HS_RATE_MASK		GENMASK(28, 27)
#define MEM_HS_RATE_SHIFT		27
#define MEM_OVRD_HS_RATE		BIT(26)
#define MEM_OVRD_HS_RATE_SHIFT		26
#define MEM_CDR_FASTLOCK		BIT(23)
#define MEM_CDR_FASTLOCK_SHIFT		23
#define MEM_CDR_LBW_MASK		GENMASK(22, 21)
#define MEM_CDR_LBW_SHIFT		21
#define MEM_CDR_STEPCNT_MASK		GENMASK(20, 19)
#define MEM_CDR_STEPCNT_SHIFT		19
#define MEM_CDR_STL_MASK		GENMASK(18, 16)
#define MEM_CDR_STL_SHIFT		16
#define MEM_CDR_THR_MASK		GENMASK(15, 13)
#define MEM_CDR_THR_SHIFT		13
#define MEM_CDR_THR_MODE		BIT(12)
#define MEM_CDR_THR_MODE_SHIFT		12
#define MEM_CDR_2NDO_SDM_MODE		BIT(11)
#define MEM_CDR_2NDO_SDM_MODE_SHIFT	11

#define PIPE3_PHY_RX_EQUALIZER		0x00000038
#define MEM_EQLEV_MASK			GENMASK(31, 16)
#define MEM_EQLEV_SHIFT			16
#define MEM_EQFTC_MASK			GENMASK(15, 11)
#define MEM_EQFTC_SHIFT			11
#define MEM_EQCTL_MASK			GENMASK(10, 7)
#define MEM_EQCTL_SHIFT			7
#define MEM_OVRD_EQLEV			BIT(2)
#define MEM_OVRD_EQLEV_SHIFT		2
#define MEM_OVRD_EQFTC			BIT(1)
#define MEM_OVRD_EQFTC_SHIFT		1

#define SATA_PHY_RX_IO_AND_A2D_OVERRIDES	0x44
#define MEM_CDR_LOS_SOURCE_MASK		GENMASK(10, 9)
#define MEM_CDR_LOS_SOURCE_SHIFT	9

/*
 * This is an Empirical value that works, need to confirm the actual
 * value required for the PIPE3PHY_PLL_CONFIGURATION2.PLL_IDLE status
 * to be correctly reflected in the PIPE3PHY_PLL_STATUS register.
 */
#define PLL_IDLE_TIME	100	/* in milliseconds */
#define PLL_LOCK_TIME	100	/* in milliseconds */

enum pipe3_mode { PIPE3_MODE_PCIE = 1,
		  PIPE3_MODE_SATA,
		  PIPE3_MODE_USBSS };

struct pipe3_dpll_params {
	u16	m;
	u8	n;
	u8	freq:3;
	u8	sd;
	u32	mf;
};

struct pipe3_dpll_map {
	unsigned long rate;
	struct pipe3_dpll_params params;
};

struct pipe3_settings {
	u8 ana_interface;
	u8 ana_losd;
	u8 dig_fastlock;
	u8 dig_lbw;
	u8 dig_stepcnt;
	u8 dig_stl;
	u8 dig_thr;
	u8 dig_thr_mode;
	u8 dig_2ndo_sdm_mode;
	u8 dig_hs_rate;
	u8 dig_ovrd_hs_rate;
	u8 dll_trim_sel;
	u8 dll_phint_rate;
	u8 eq_lev;
	u8 eq_ftc;
	u8 eq_ctl;
	u8 eq_ovrd_lev;
	u8 eq_ovrd_ftc;
};

struct ti_pipe3 {
	void __iomem		*pll_ctrl_base;
	void __iomem		*phy_rx;
	void __iomem		*phy_tx;
	struct device		*dev;
	struct device		*control_dev;
	struct clk		*wkupclk;
	struct clk		*sys_clk;
	struct clk		*refclk;
	struct clk		*div_clk;
	struct pipe3_dpll_map	*dpll_map;
	struct regmap		*phy_power_syscon; /* ctrl. reg. acces */
	struct regmap		*pcs_syscon; /* ctrl. reg. acces */
	struct regmap		*dpll_reset_syscon; /* ctrl. reg. acces */
	unsigned int		dpll_reset_reg; /* reg. index within syscon */
	unsigned int		power_reg; /* power reg. index within syscon */
	unsigned int		pcie_pcs_reg; /* pcs reg. index in syscon */
	bool			sata_refclk_enabled;
	enum pipe3_mode		mode;
	struct pipe3_settings	settings;
};

static struct pipe3_dpll_map dpll_map_usb[] = {
	{12000000, {1250, 5, 4, 20, 0} },	/* 12 MHz */
	{16800000, {3125, 20, 4, 20, 0} },	/* 16.8 MHz */
	{19200000, {1172, 8, 4, 20, 65537} },	/* 19.2 MHz */
	{20000000, {1000, 7, 4, 10, 0} },	/* 20 MHz */
	{26000000, {1250, 12, 4, 20, 0} },	/* 26 MHz */
	{38400000, {3125, 47, 4, 20, 92843} },	/* 38.4 MHz */
	{ },					/* Terminator */
};

static struct pipe3_dpll_map dpll_map_sata[] = {
	{12000000, {625, 4, 4, 6, 0} },	/* 12 MHz */
	{16800000, {625, 6, 4, 7, 0} },		/* 16.8 MHz */
	{19200000, {625, 7, 4, 6, 0} },		/* 19.2 MHz */
	{20000000, {750, 9, 4, 6, 0} },		/* 20 MHz */
	{26000000, {750, 12, 4, 6, 0} },	/* 26 MHz */
	{38400000, {625, 15, 4, 6, 0} },	/* 38.4 MHz */
	{ },					/* Terminator */
};

struct pipe3_data {
	enum pipe3_mode mode;
	struct pipe3_dpll_map *dpll_map;
	struct pipe3_settings settings;
};

static struct pipe3_data data_usb = {
	.mode = PIPE3_MODE_USBSS,
	.dpll_map = dpll_map_usb,
	.settings = {
	/* DRA75x TRM Table 26-17 Preferred USB3_PHY_RX SCP Register Settings */
		.ana_interface = INTERFACE_MODE_USBSS,
		.ana_losd = 0xa,
		.dig_fastlock = 1,
		.dig_lbw = 3,
		.dig_stepcnt = 0,
		.dig_stl = 0x3,
		.dig_thr = 1,
		.dig_thr_mode = 1,
		.dig_2ndo_sdm_mode = 0,
		.dig_hs_rate = 0,
		.dig_ovrd_hs_rate = 1,
		.dll_trim_sel = 0x2,
		.dll_phint_rate = 0x3,
		.eq_lev = 0,
		.eq_ftc = 0,
		.eq_ctl = 0x9,
		.eq_ovrd_lev = 0,
		.eq_ovrd_ftc = 0,
	},
};

static struct pipe3_data data_sata = {
	.mode = PIPE3_MODE_SATA,
	.dpll_map = dpll_map_sata,
	.settings = {
	/* DRA75x TRM Table 26-9 Preferred SATA_PHY_RX SCP Register Settings */
		.ana_interface = INTERFACE_MODE_SATA_3P0,
		.ana_losd = 0x5,
		.dig_fastlock = 1,
		.dig_lbw = 3,
		.dig_stepcnt = 0,
		.dig_stl = 0x3,
		.dig_thr = 1,
		.dig_thr_mode = 1,
		.dig_2ndo_sdm_mode = 0,
		.dig_hs_rate = 0,	/* Not in TRM preferred settings */
		.dig_ovrd_hs_rate = 0,	/* Not in TRM preferred settings */
		.dll_trim_sel = 0x1,
		.dll_phint_rate = 0x2,	/* for 1.5 GHz DPLL clock */
		.eq_lev = 0,
		.eq_ftc = 0x1f,
		.eq_ctl = 0,
		.eq_ovrd_lev = 1,
		.eq_ovrd_ftc = 1,
	},
};

static struct pipe3_data data_pcie = {
	.mode = PIPE3_MODE_PCIE,
	.settings = {
	/* DRA75x TRM Table 26-62 Preferred PCIe_PHY_RX SCP Register Settings */
		.ana_interface = INTERFACE_MODE_PCIE,
		.ana_losd = 0xa,
		.dig_fastlock = 1,
		.dig_lbw = 3,
		.dig_stepcnt = 0,
		.dig_stl = 0x3,
		.dig_thr = 1,
		.dig_thr_mode = 1,
		.dig_2ndo_sdm_mode = 0,
		.dig_hs_rate = 0,
		.dig_ovrd_hs_rate = 0,
		.dll_trim_sel = 0x2,
		.dll_phint_rate = 0x3,
		.eq_lev = 0,
		.eq_ftc = 0x1f,
		.eq_ctl = 1,
		.eq_ovrd_lev = 0,
		.eq_ovrd_ftc = 0,
	},
};

static inline u32 ti_pipe3_readl(void __iomem *addr, unsigned offset)
{
	return __raw_readl(addr + offset);
}

static inline void ti_pipe3_writel(void __iomem *addr, unsigned offset,
	u32 data)
{
	__raw_writel(data, addr + offset);
}

static struct pipe3_dpll_params *ti_pipe3_get_dpll_params(struct ti_pipe3 *phy)
{
	unsigned long rate;
	struct pipe3_dpll_map *dpll_map = phy->dpll_map;

	rate = clk_get_rate(phy->sys_clk);

	for (; dpll_map->rate; dpll_map++) {
		if (rate == dpll_map->rate)
			return &dpll_map->params;
	}

	dev_err(phy->dev, "No DPLL configuration for %lu Hz SYS CLK\n", rate);

	return NULL;
}

static int ti_pipe3_enable_clocks(struct ti_pipe3 *phy);
static void ti_pipe3_disable_clocks(struct ti_pipe3 *phy);

static int ti_pipe3_power_off(struct phy *x)
{
	int ret;
	struct ti_pipe3 *phy = phy_get_drvdata(x);

	if (!phy->phy_power_syscon) {
		omap_control_phy_power(phy->control_dev, 0);
		return 0;
	}

	ret = regmap_update_bits(phy->phy_power_syscon, phy->power_reg,
				 PIPE3_PHY_PWRCTL_CLK_CMD_MASK, 0);
	return ret;
}

static void ti_pipe3_calibrate(struct ti_pipe3 *phy);

static int ti_pipe3_power_on(struct phy *x)
{
	u32 val;
	u32 mask;
	unsigned long rate;
	struct ti_pipe3 *phy = phy_get_drvdata(x);
	bool rx_pending = false;

	if (!phy->phy_power_syscon) {
		omap_control_phy_power(phy->control_dev, 1);
		return 0;
	}

	rate = clk_get_rate(phy->sys_clk);
	if (!rate) {
		dev_err(phy->dev, "Invalid clock rate\n");
		return -EINVAL;
	}
	rate = rate / 1000000;
	mask = OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_FREQ_MASK;
	val = rate << OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_FREQ_SHIFT;
	regmap_update_bits(phy->phy_power_syscon, phy->power_reg,
			   mask, val);
	/*
	 * For PCIe, TX and RX must be powered on simultaneously.
	 * For USB and SATA, TX must be powered on before RX
	 */
	mask = OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_MASK;
	if (phy->mode == PIPE3_MODE_SATA || phy->mode == PIPE3_MODE_USBSS) {
		val = PIPE3_PHY_TX_POWERON;
		rx_pending = true;
	} else {
		val = PIPE3_PHY_TX_POWERON | PIPE3_PHY_RX_POWERON;
	}

	regmap_update_bits(phy->phy_power_syscon, phy->power_reg,
			   mask, val);

	if (rx_pending) {
		val = PIPE3_PHY_TX_POWERON | PIPE3_PHY_RX_POWERON;
		regmap_update_bits(phy->phy_power_syscon, phy->power_reg,
				   mask, val);
	}

	if (phy->mode == PIPE3_MODE_PCIE)
		ti_pipe3_calibrate(phy);

	return 0;
}

static int ti_pipe3_dpll_wait_lock(struct ti_pipe3 *phy)
{
	u32		val;
	unsigned long	timeout;

	timeout = jiffies + msecs_to_jiffies(PLL_LOCK_TIME);
	do {
		cpu_relax();
		val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_STATUS);
		if (val & PLL_LOCK)
			return 0;
	} while (!time_after(jiffies, timeout));

	dev_err(phy->dev, "DPLL failed to lock\n");
	return -EBUSY;
}

static int ti_pipe3_dpll_program(struct ti_pipe3 *phy)
{
	u32			val;
	struct pipe3_dpll_params *dpll_params;

	dpll_params = ti_pipe3_get_dpll_params(phy);
	if (!dpll_params)
		return -EINVAL;

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION1);
	val &= ~PLL_REGN_MASK;
	val |= dpll_params->n << PLL_REGN_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION1, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION2);
	val &= ~PLL_SELFREQDCO_MASK;
	val |= dpll_params->freq << PLL_SELFREQDCO_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION2, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION1);
	val &= ~PLL_REGM_MASK;
	val |= dpll_params->m << PLL_REGM_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION1, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION4);
	val &= ~PLL_REGM_F_MASK;
	val |= dpll_params->mf << PLL_REGM_F_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION4, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION3);
	val &= ~PLL_SD_MASK;
	val |= dpll_params->sd << PLL_SD_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION3, val);

	ti_pipe3_writel(phy->pll_ctrl_base, PLL_GO, SET_PLL_GO);

	return ti_pipe3_dpll_wait_lock(phy);
}

static void ti_pipe3_calibrate(struct ti_pipe3 *phy)
{
	u32 val;
	struct pipe3_settings *s = &phy->settings;

	val = ti_pipe3_readl(phy->phy_rx, PIPE3_PHY_RX_ANA_PROGRAMMABILITY);
	val &= ~(INTERFACE_MASK | LOSD_MASK | MEM_PLLDIV);
	val |= (s->ana_interface << INTERFACE_SHIFT | s->ana_losd << LOSD_SHIFT);
	ti_pipe3_writel(phy->phy_rx, PIPE3_PHY_RX_ANA_PROGRAMMABILITY, val);

	val = ti_pipe3_readl(phy->phy_rx, PIPE3_PHY_RX_DIGITAL_MODES);
	val &= ~(MEM_HS_RATE_MASK | MEM_OVRD_HS_RATE | MEM_CDR_FASTLOCK |
		 MEM_CDR_LBW_MASK | MEM_CDR_STEPCNT_MASK | MEM_CDR_STL_MASK |
		 MEM_CDR_THR_MASK | MEM_CDR_THR_MODE | MEM_CDR_2NDO_SDM_MODE);
	val |= s->dig_hs_rate << MEM_HS_RATE_SHIFT |
		s->dig_ovrd_hs_rate << MEM_OVRD_HS_RATE_SHIFT |
		s->dig_fastlock << MEM_CDR_FASTLOCK_SHIFT |
		s->dig_lbw << MEM_CDR_LBW_SHIFT |
		s->dig_stepcnt << MEM_CDR_STEPCNT_SHIFT |
		s->dig_stl << MEM_CDR_STL_SHIFT |
		s->dig_thr << MEM_CDR_THR_SHIFT |
		s->dig_thr_mode << MEM_CDR_THR_MODE_SHIFT |
		s->dig_2ndo_sdm_mode << MEM_CDR_2NDO_SDM_MODE_SHIFT;
	ti_pipe3_writel(phy->phy_rx, PIPE3_PHY_RX_DIGITAL_MODES, val);

	val = ti_pipe3_readl(phy->phy_rx, PIPE3_PHY_RX_TRIM);
	val &= ~MEM_DLL_TRIM_SEL_MASK;
	val |= s->dll_trim_sel << MEM_DLL_TRIM_SHIFT;
	ti_pipe3_writel(phy->phy_rx, PIPE3_PHY_RX_TRIM, val);

	val = ti_pipe3_readl(phy->phy_rx, PIPE3_PHY_RX_DLL);
	val &= ~MEM_DLL_PHINT_RATE_MASK;
	val |= s->dll_phint_rate << MEM_DLL_PHINT_RATE_SHIFT;
	ti_pipe3_writel(phy->phy_rx, PIPE3_PHY_RX_DLL, val);

	val = ti_pipe3_readl(phy->phy_rx, PIPE3_PHY_RX_EQUALIZER);
	val &= ~(MEM_EQLEV_MASK | MEM_EQFTC_MASK | MEM_EQCTL_MASK |
		 MEM_OVRD_EQLEV | MEM_OVRD_EQFTC);
	val |= s->eq_lev << MEM_EQLEV_SHIFT |
		s->eq_ftc << MEM_EQFTC_SHIFT |
		s->eq_ctl << MEM_EQCTL_SHIFT |
		s->eq_ovrd_lev << MEM_OVRD_EQLEV_SHIFT |
		s->eq_ovrd_ftc << MEM_OVRD_EQFTC_SHIFT;
	ti_pipe3_writel(phy->phy_rx, PIPE3_PHY_RX_EQUALIZER, val);

	if (phy->mode == PIPE3_MODE_SATA) {
		val = ti_pipe3_readl(phy->phy_rx,
				     SATA_PHY_RX_IO_AND_A2D_OVERRIDES);
		val &= ~MEM_CDR_LOS_SOURCE_MASK;
		ti_pipe3_writel(phy->phy_rx, SATA_PHY_RX_IO_AND_A2D_OVERRIDES,
				val);
	}
}

static int ti_pipe3_init(struct phy *x)
{
	struct ti_pipe3 *phy = phy_get_drvdata(x);
	u32 val;
	int ret = 0;

	ti_pipe3_enable_clocks(phy);
	/*
	 * Set pcie_pcs register to 0x96 for proper functioning of phy
	 * as recommended in AM572x TRM SPRUHZ6, section 18.5.2.2, table
	 * 18-1804.
	 */
	if (phy->mode == PIPE3_MODE_PCIE) {
		if (!phy->pcs_syscon) {
			omap_control_pcie_pcs(phy->control_dev, 0x96);
			return 0;
		}

		val = 0x96 << OMAP_CTRL_PCIE_PCS_DELAY_COUNT_SHIFT;
		ret = regmap_update_bits(phy->pcs_syscon, phy->pcie_pcs_reg,
					 PCIE_PCS_MASK, val);
		return ret;
	}

	/* Bring it out of IDLE if it is IDLE */
	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION2);
	if (val & PLL_IDLE) {
		val &= ~PLL_IDLE;
		ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION2, val);
		ret = ti_pipe3_dpll_wait_lock(phy);
	}

	/* SATA has issues if re-programmed when locked */
	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_STATUS);
	if ((val & PLL_LOCK) && phy->mode == PIPE3_MODE_SATA)
		return ret;

	/* Program the DPLL */
	ret = ti_pipe3_dpll_program(phy);
	if (ret) {
		ti_pipe3_disable_clocks(phy);
		return -EINVAL;
	}

	ti_pipe3_calibrate(phy);

	return ret;
}

static int ti_pipe3_exit(struct phy *x)
{
	struct ti_pipe3 *phy = phy_get_drvdata(x);
	u32 val;
	unsigned long timeout;

	/* If dpll_reset_syscon is not present we wont power down SATA DPLL
	 * due to Errata i783
	 */
	if (phy->mode == PIPE3_MODE_SATA && !phy->dpll_reset_syscon)
		return 0;

	/* PCIe doesn't have internal DPLL */
	if (phy->mode != PIPE3_MODE_PCIE) {
		/* Put DPLL in IDLE mode */
		val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION2);
		val |= PLL_IDLE;
		ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION2, val);

		/* wait for LDO and Oscillator to power down */
		timeout = jiffies + msecs_to_jiffies(PLL_IDLE_TIME);
		do {
			cpu_relax();
			val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_STATUS);
			if ((val & PLL_TICOPWDN) && (val & PLL_LDOPWDN))
				break;
		} while (!time_after(jiffies, timeout));

		if (!(val & PLL_TICOPWDN) || !(val & PLL_LDOPWDN)) {
			dev_err(phy->dev, "Failed to power down: PLL_STATUS 0x%x\n",
				val);
			return -EBUSY;
		}
	}

	/* i783: SATA needs control bit toggle after PLL unlock */
	if (phy->mode == PIPE3_MODE_SATA) {
		regmap_update_bits(phy->dpll_reset_syscon, phy->dpll_reset_reg,
				   SATA_PLL_SOFT_RESET, SATA_PLL_SOFT_RESET);
		regmap_update_bits(phy->dpll_reset_syscon, phy->dpll_reset_reg,
				   SATA_PLL_SOFT_RESET, 0);
	}

	ti_pipe3_disable_clocks(phy);

	return 0;
}
static const struct phy_ops ops = {
	.init		= ti_pipe3_init,
	.exit		= ti_pipe3_exit,
	.power_on	= ti_pipe3_power_on,
	.power_off	= ti_pipe3_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id ti_pipe3_id_table[];

static int ti_pipe3_get_clk(struct ti_pipe3 *phy)
{
	struct clk *clk;
	struct device *dev = phy->dev;

	phy->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(phy->refclk)) {
		dev_err(dev, "unable to get refclk\n");
		/* older DTBs have missing refclk in SATA PHY
		 * so don't bail out in case of SATA PHY.
		 */
		if (phy->mode != PIPE3_MODE_SATA)
			return PTR_ERR(phy->refclk);
	}

	if (phy->mode != PIPE3_MODE_SATA) {
		phy->wkupclk = devm_clk_get(dev, "wkupclk");
		if (IS_ERR(phy->wkupclk)) {
			dev_err(dev, "unable to get wkupclk\n");
			return PTR_ERR(phy->wkupclk);
		}
	} else {
		phy->wkupclk = ERR_PTR(-ENODEV);
	}

	if (phy->mode != PIPE3_MODE_PCIE || phy->phy_power_syscon) {
		phy->sys_clk = devm_clk_get(dev, "sysclk");
		if (IS_ERR(phy->sys_clk)) {
			dev_err(dev, "unable to get sysclk\n");
			return -EINVAL;
		}
	}

	if (phy->mode == PIPE3_MODE_PCIE) {
		clk = devm_clk_get(dev, "dpll_ref");
		if (IS_ERR(clk)) {
			dev_err(dev, "unable to get dpll ref clk\n");
			return PTR_ERR(clk);
		}
		clk_set_rate(clk, 1500000000);

		clk = devm_clk_get(dev, "dpll_ref_m2");
		if (IS_ERR(clk)) {
			dev_err(dev, "unable to get dpll ref m2 clk\n");
			return PTR_ERR(clk);
		}
		clk_set_rate(clk, 100000000);

		clk = devm_clk_get(dev, "phy-div");
		if (IS_ERR(clk)) {
			dev_err(dev, "unable to get phy-div clk\n");
			return PTR_ERR(clk);
		}
		clk_set_rate(clk, 100000000);

		phy->div_clk = devm_clk_get(dev, "div-clk");
		if (IS_ERR(phy->div_clk)) {
			dev_err(dev, "unable to get div-clk\n");
			return PTR_ERR(phy->div_clk);
		}
	} else {
		phy->div_clk = ERR_PTR(-ENODEV);
	}

	return 0;
}

static int ti_pipe3_get_sysctrl(struct ti_pipe3 *phy)
{
	struct device *dev = phy->dev;
	struct device_node *node = dev->of_node;
	struct device_node *control_node;
	struct platform_device *control_pdev;

	phy->phy_power_syscon = syscon_regmap_lookup_by_phandle(node,
							"syscon-phy-power");
	if (IS_ERR(phy->phy_power_syscon)) {
		dev_dbg(dev,
			"can't get syscon-phy-power, using control device\n");
		phy->phy_power_syscon = NULL;
	} else {
		if (of_property_read_u32_index(node,
					       "syscon-phy-power", 1,
					       &phy->power_reg)) {
			dev_err(dev, "couldn't get power reg. offset\n");
			return -EINVAL;
		}
	}

	if (!phy->phy_power_syscon) {
		control_node = of_parse_phandle(node, "ctrl-module", 0);
		if (!control_node) {
			dev_err(dev, "Failed to get control device phandle\n");
			return -EINVAL;
		}

		control_pdev = of_find_device_by_node(control_node);
		of_node_put(control_node);
		if (!control_pdev) {
			dev_err(dev, "Failed to get control device\n");
			return -EINVAL;
		}

		phy->control_dev = &control_pdev->dev;
	}

	if (phy->mode == PIPE3_MODE_PCIE) {
		phy->pcs_syscon = syscon_regmap_lookup_by_phandle(node,
								  "syscon-pcs");
		if (IS_ERR(phy->pcs_syscon)) {
			dev_dbg(dev,
				"can't get syscon-pcs, using omap control\n");
			phy->pcs_syscon = NULL;
		} else {
			if (of_property_read_u32_index(node,
						       "syscon-pcs", 1,
						       &phy->pcie_pcs_reg)) {
				dev_err(dev,
					"couldn't get pcie pcs reg. offset\n");
				return -EINVAL;
			}
		}
	}

	if (phy->mode == PIPE3_MODE_SATA) {
		phy->dpll_reset_syscon = syscon_regmap_lookup_by_phandle(node,
							"syscon-pllreset");
		if (IS_ERR(phy->dpll_reset_syscon)) {
			dev_info(dev,
				 "can't get syscon-pllreset, sata dpll won't idle\n");
			phy->dpll_reset_syscon = NULL;
		} else {
			if (of_property_read_u32_index(node,
						       "syscon-pllreset", 1,
						       &phy->dpll_reset_reg)) {
				dev_err(dev,
					"couldn't get pllreset reg. offset\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int ti_pipe3_get_tx_rx_base(struct ti_pipe3 *phy)
{
	struct device *dev = phy->dev;
	struct platform_device *pdev = to_platform_device(dev);

	phy->phy_rx = devm_platform_ioremap_resource_byname(pdev, "phy_rx");
	if (IS_ERR(phy->phy_rx))
		return PTR_ERR(phy->phy_rx);

	phy->phy_tx = devm_platform_ioremap_resource_byname(pdev, "phy_tx");

	return PTR_ERR_OR_ZERO(phy->phy_tx);
}

static int ti_pipe3_get_pll_base(struct ti_pipe3 *phy)
{
	struct device *dev = phy->dev;
	struct platform_device *pdev = to_platform_device(dev);

	if (phy->mode == PIPE3_MODE_PCIE)
		return 0;

	phy->pll_ctrl_base =
		devm_platform_ioremap_resource_byname(pdev, "pll_ctrl");
	return PTR_ERR_OR_ZERO(phy->pll_ctrl_base);
}

static int ti_pipe3_probe(struct platform_device *pdev)
{
	struct ti_pipe3 *phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	int ret;
	const struct of_device_id *match;
	struct pipe3_data *data;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	match = of_match_device(ti_pipe3_id_table, dev);
	if (!match)
		return -EINVAL;

	data = (struct pipe3_data *)match->data;
	if (!data) {
		dev_err(dev, "no driver data\n");
		return -EINVAL;
	}

	phy->dev = dev;
	phy->mode = data->mode;
	phy->dpll_map = data->dpll_map;
	phy->settings = data->settings;

	ret = ti_pipe3_get_pll_base(phy);
	if (ret)
		return ret;

	ret = ti_pipe3_get_tx_rx_base(phy);
	if (ret)
		return ret;

	ret = ti_pipe3_get_sysctrl(phy);
	if (ret)
		return ret;

	ret = ti_pipe3_get_clk(phy);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, phy);
	pm_runtime_enable(dev);

	/*
	 * Prevent auto-disable of refclk for SATA PHY due to Errata i783
	 */
	if (phy->mode == PIPE3_MODE_SATA) {
		if (!IS_ERR(phy->refclk)) {
			clk_prepare_enable(phy->refclk);
			phy->sata_refclk_enabled = true;
		}
	}

	generic_phy = devm_phy_create(dev, NULL, &ops);
	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, phy);

	ti_pipe3_power_off(generic_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static int ti_pipe3_remove(struct platform_device *pdev)
{
	struct ti_pipe3 *phy = platform_get_drvdata(pdev);

	if (phy->mode == PIPE3_MODE_SATA) {
		clk_disable_unprepare(phy->refclk);
		phy->sata_refclk_enabled = false;
	}
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int ti_pipe3_enable_clocks(struct ti_pipe3 *phy)
{
	int ret = 0;

	if (!IS_ERR(phy->refclk)) {
		ret = clk_prepare_enable(phy->refclk);
		if (ret) {
			dev_err(phy->dev, "Failed to enable refclk %d\n", ret);
			return ret;
		}
	}

	if (!IS_ERR(phy->wkupclk)) {
		ret = clk_prepare_enable(phy->wkupclk);
		if (ret) {
			dev_err(phy->dev, "Failed to enable wkupclk %d\n", ret);
			goto disable_refclk;
		}
	}

	if (!IS_ERR(phy->div_clk)) {
		ret = clk_prepare_enable(phy->div_clk);
		if (ret) {
			dev_err(phy->dev, "Failed to enable div_clk %d\n", ret);
			goto disable_wkupclk;
		}
	}

	return 0;

disable_wkupclk:
	if (!IS_ERR(phy->wkupclk))
		clk_disable_unprepare(phy->wkupclk);

disable_refclk:
	if (!IS_ERR(phy->refclk))
		clk_disable_unprepare(phy->refclk);

	return ret;
}

static void ti_pipe3_disable_clocks(struct ti_pipe3 *phy)
{
	if (!IS_ERR(phy->wkupclk))
		clk_disable_unprepare(phy->wkupclk);
	if (!IS_ERR(phy->refclk))
		clk_disable_unprepare(phy->refclk);
	if (!IS_ERR(phy->div_clk))
		clk_disable_unprepare(phy->div_clk);
}

static const struct of_device_id ti_pipe3_id_table[] = {
	{
		.compatible = "ti,phy-usb3",
		.data = &data_usb,
	},
	{
		.compatible = "ti,omap-usb3",
		.data = &data_usb,
	},
	{
		.compatible = "ti,phy-pipe3-sata",
		.data = &data_sata,
	},
	{
		.compatible = "ti,phy-pipe3-pcie",
		.data = &data_pcie,
	},
	{}
};
MODULE_DEVICE_TABLE(of, ti_pipe3_id_table);

static struct platform_driver ti_pipe3_driver = {
	.probe		= ti_pipe3_probe,
	.remove		= ti_pipe3_remove,
	.driver		= {
		.name	= "ti-pipe3",
		.of_match_table = ti_pipe3_id_table,
	},
};

module_platform_driver(ti_pipe3_driver);

MODULE_ALIAS("platform:ti_pipe3");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TI PIPE3 phy driver");
MODULE_LICENSE("GPL v2");
