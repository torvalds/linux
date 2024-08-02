// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/qcom_scm.h>
#include <linux/arm-smccc.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/usb/phy.h>
#include <linux/usb/dwc3-msm.h>
#include <linux/reset.h>

#define QUSB2PHY_PLL_PWR_CTL		0x18
#define REF_BUF_EN			BIT(0)
#define REXT_EN				BIT(1)
#define PLL_BYPASSNL			BIT(2)
#define REXT_TRIM_0			BIT(4)

#define QUSB2PHY_PLL_AUTOPGM_CTL1	0x1C
#define PLL_RESET_N_CNT_5		0x5
#define PLL_RESET_N			BIT(4)
#define PLL_AUTOPGM_EN			BIT(7)

#define QUSB2PHY_PLL_STATUS	0x38
#define QUSB2PHY_PLL_LOCK	BIT(5)

#define QUSB2PHY_PORT_QC1	0x70
#define VDM_SRC_EN		BIT(4)
#define VDP_SRC_EN		BIT(2)

#define QUSB2PHY_PORT_QC2	0x74
#define RDM_UP_EN		BIT(1)
#define RDP_UP_EN		BIT(3)
#define RPUM_LOW_EN		BIT(4)
#define RPUP_LOW_EN		BIT(5)

#define QUSB2PHY_PORT_POWERDOWN		0xB4
#define CLAMP_N_EN			BIT(5)
#define FREEZIO_N			BIT(1)
#define POWER_DOWN			BIT(0)

#define QUSB2PHY_PORT_TEST_CTRL		0xB8

#define QUSB2PHY_PWR_CTRL1		0x210
#define PWR_CTRL1_CLAMP_N_EN		BIT(1)
#define PWR_CTRL1_POWR_DOWN		BIT(0)

#define QUSB2PHY_PLL_COMMON_STATUS_ONE	0x1A0
#define CORE_READY_STATUS		BIT(0)

#define QUSB2PHY_PORT_UTMI_CTRL1	0xC0
#define SUSPEND_N			BIT(5)
#define TERM_SELECT			BIT(4)
#define XCVR_SELECT_FS			BIT(2)
#define OP_MODE_NON_DRIVE		BIT(0)

#define QUSB2PHY_PORT_UTMI_CTRL2	0xC4
#define UTMI_ULPI_SEL			BIT(7)
#define UTMI_TEST_MUX_SEL		BIT(6)

#define QUSB2PHY_PLL_AUTOPGM_CTL1	0x1C
#define QUSB2PHY_PLL_PWR_CTL		0x18

#define QUSB2PHY_PLL_TEST		0x04
#define CLK_REF_SEL			BIT(7)

#define QUSB2PHY_PORT_TUNE1             0x80
#define QUSB2PHY_PORT_TUNE2             0x84
#define QUSB2PHY_PORT_TUNE3             0x88
#define QUSB2PHY_PORT_TUNE4             0x8C
#define QUSB2PHY_PORT_TUNE5             0x90

/* Get TUNE2's high nibble value read from efuse */
#define TUNE2_HIGH_NIBBLE_VAL(val, pos, mask)	((val >> pos) & mask)

#define QUSB2PHY_PORT_INTR_CTRL         0xBC
#define CHG_DET_INTR_EN                 BIT(4)
#define DMSE_INTR_HIGH_SEL              BIT(3)
#define DMSE_INTR_EN                    BIT(2)
#define DPSE_INTR_HIGH_SEL              BIT(1)
#define DPSE_INTR_EN                    BIT(0)

#define QUSB2PHY_PORT_INTR_STATUS	0xF0
#define DPSE_INTR_HIGH			BIT(0)

#define QUSB2PHY_PORT_UTMI_STATUS	0xF4
#define LINESTATE_DP			BIT(0)
#define LINESTATE_DM			BIT(1)


#define QUSB2PHY_1P8_VOL_MIN           1800000 /* uV */
#define QUSB2PHY_1P8_VOL_MAX           1800000 /* uV */
#define QUSB2PHY_1P8_HPM_LOAD          30000   /* uA */

#define QUSB2PHY_3P3_VOL_MIN		3075000 /* uV */
#define QUSB2PHY_3P3_VOL_MAX		3200000 /* uV */
#define QUSB2PHY_3P3_HPM_LOAD		30000	/* uA */

#define QUSB2PHY_REFCLK_ENABLE		BIT(0)

#define HSTX_TRIMSIZE			4

enum port_state {
	PORT_UNKNOWN,
	PORT_DISCONNECTED,
	PORT_DCD_IN_PROGRESS,
	PORT_PRIMARY_IN_PROGRESS,
	PORT_SECONDARY_IN_PROGRESS,
	PORT_CHG_DET_DONE,
	PORT_HOST_MODE,
};

enum chg_det_state {
	STATE_UNKNOWN,
	STATE_DCD,
	STATE_PRIMARY,
	STATE_SECONDARY,
};

struct qusb_phy {
	struct usb_phy		phy;
	void __iomem		*base;
	void __iomem		*tune2_efuse_reg;
	void __iomem		*ref_clk_base;
	void __iomem		*tcsr_clamp_dig_n;
	void __iomem		*tcsr_conn_box_spare;
	void __iomem		*eud_enable_reg;

	struct clk		*ref_clk_src;
	struct clk		*ref_clk;
	struct clk		*cfg_ahb_clk;
	struct reset_control	*phy_reset;
	struct clk		*iface_clk;
	struct clk		*core_clk;

	struct regulator	*gdsc;
	struct regulator	*vdd;
	struct regulator	*vdda33;
	struct regulator	*vdda18;
	int			vdd_levels[3]; /* none, low, high */
	int			init_seq_len;
	int			*qusb_phy_init_seq;
	u32			major_rev;
	u32			usb_hs_ac_bitmask;
	u32			usb_hs_ac_value;

	u32			tune2_val;
	int			tune2_efuse_bit_pos;
	int			tune2_efuse_num_of_bits;
	int			tune2_efuse_correction;
	bool			power_enabled;
	bool			clk_enabled;
	bool			cable_connected;
	bool			suspended;
	bool			ulpi_mode;
	bool			dpdm_enable;
	bool			is_se_clk;
	bool			scm_lvl_shifter;

	struct regulator_desc	dpdm_rdesc;
	struct regulator_dev	*dpdm_rdev;

	bool			put_into_high_z_state;
	struct mutex		phy_lock;

	struct extcon_dev	*usb_extcon;
	bool			vbus_active;
	bool			id_state;
	struct power_supply	*usb_psy;
	struct delayed_work	port_det_w;
	enum port_state		port_state;
	unsigned int		dcd_timeout;

	/* debugfs entries */
	struct dentry		*root;
	u8			tune1;
	u8			tune2;
	u8			tune3;
	u8			tune4;
	u8			tune5;
};

static void qusb_phy_update_tcsr_level_shifter(struct qusb_phy *qphy,
						u32 val)
{
	if (qphy->tcsr_clamp_dig_n) {
		writel_relaxed(val, qphy->tcsr_clamp_dig_n);
		dev_dbg(qphy->phy.dev, "update tcsr level shifter: %d\n", val);
	} else if (qphy->scm_lvl_shifter) {
		dev_dbg(qphy->phy.dev, "update scm level shifter: %d\n", val);
		qcom_scm_phy_update_scm_level_shifter(val);
	}
}

static void qusb_phy_enable_clocks(struct qusb_phy *qphy, bool on)
{
	dev_dbg(qphy->phy.dev, "%s(): on:%d\n", __func__, on);
	if (qphy->clk_enabled == on) {
		dev_dbg(qphy->phy.dev, "%s(): clock is already %d\n", __func__, on);
		return;
	}

	if (on) {
		clk_prepare_enable(qphy->ref_clk_src);
		clk_prepare_enable(qphy->ref_clk);
		clk_prepare_enable(qphy->iface_clk);
		clk_prepare_enable(qphy->core_clk);
		clk_prepare_enable(qphy->cfg_ahb_clk);
	} else {
		clk_disable_unprepare(qphy->cfg_ahb_clk);
		/*
		 * FSM depedency beween iface_clk and core_clk.
		 * Hence turned off core_clk before iface_clk.
		 */
		clk_disable_unprepare(qphy->core_clk);
		clk_disable_unprepare(qphy->iface_clk);
		clk_disable_unprepare(qphy->ref_clk);
		clk_disable_unprepare(qphy->ref_clk_src);
	}
	qphy->clk_enabled = on;

}

static int qusb_phy_gdsc(struct qusb_phy *qphy, bool on)
{
	int ret;

	if (IS_ERR_OR_NULL(qphy->gdsc))
		return -EPERM;

	if (on) {
		dev_dbg(qphy->phy.dev, "TURNING ON GDSC\n");
		ret = regulator_enable(qphy->gdsc);
		if (ret) {
			dev_err(qphy->phy.dev, "unable to enable gdsc\n");
			return ret;
		}
	} else {
		dev_dbg(qphy->phy.dev, "TURNING OFF GDSC\n");
		ret = regulator_disable(qphy->gdsc);
		if (ret) {
			dev_err(qphy->phy.dev, "unable to disable gdsc\n");
			return ret;
		}
	}

	return ret;
}

static int qusb_phy_config_vdd(struct qusb_phy *qphy, int high)
{
	int min, ret;

	min = high ? 1 : 0; /* low or none? */
	ret = regulator_set_voltage(qphy->vdd, qphy->vdd_levels[min],
						qphy->vdd_levels[2]);
	if (ret) {
		dev_err(qphy->phy.dev, "unable to set voltage for qusb vdd\n");
		return ret;
	}

	dev_dbg(qphy->phy.dev, "min_vol:%d max_vol:%d\n",
			qphy->vdd_levels[min], qphy->vdd_levels[2]);
	return ret;
}

static int qusb_phy_enable_power(struct qusb_phy *qphy, bool on)
{
	int ret = 0;

	dev_dbg(qphy->phy.dev, "%s turn %s regulators\n",
			__func__, on ? "on" : "off");
	if (qphy->power_enabled == on) {
		dev_dbg(qphy->phy.dev, "qphy->power_enabled is already %d\n", on);
		return ret;
	}

	if (!on)
		goto disable_vdda33;

	ret = qusb_phy_config_vdd(qphy, true);
	if (ret) {
		dev_err(qphy->phy.dev, "Unable to config VDD:%d\n",
							ret);
		goto err_vdd;
	}

	ret = regulator_enable(qphy->vdd);
	if (ret) {
		dev_err(qphy->phy.dev, "Unable to enable VDD\n");
		goto unconfig_vdd;
	}

	ret = regulator_set_load(qphy->vdda18, QUSB2PHY_1P8_HPM_LOAD);
	if (ret < 0) {
		dev_err(qphy->phy.dev, "Unable to set HPM of vdda18:%d\n", ret);
		goto disable_vdd;
	}

	ret = regulator_set_voltage(qphy->vdda18, QUSB2PHY_1P8_VOL_MIN,
						QUSB2PHY_1P8_VOL_MAX);
	if (ret) {
		dev_err(qphy->phy.dev,
				"Unable to set voltage for vdda18:%d\n", ret);
		goto put_vdda18_lpm;
	}

	ret = regulator_enable(qphy->vdda18);
	if (ret) {
		dev_err(qphy->phy.dev, "Unable to enable vdda18:%d\n", ret);
		goto unset_vdda18;
	}

	ret = regulator_set_load(qphy->vdda33, QUSB2PHY_3P3_HPM_LOAD);
	if (ret < 0) {
		dev_err(qphy->phy.dev, "Unable to set HPM of vdda33:%d\n", ret);
		goto disable_vdda18;
	}

	ret = regulator_set_voltage(qphy->vdda33, QUSB2PHY_3P3_VOL_MIN,
						QUSB2PHY_3P3_VOL_MAX);
	if (ret) {
		dev_err(qphy->phy.dev,
				"Unable to set voltage for vdda33:%d\n", ret);
		goto put_vdda33_lpm;
	}

	ret = regulator_enable(qphy->vdda33);
	if (ret) {
		dev_err(qphy->phy.dev, "Unable to enable vdda33:%d\n", ret);
		goto unset_vdd33;
	}

	pr_debug("%s(): QUSB PHY's regulators are turned ON.\n", __func__);
	qphy->power_enabled = true;
	return ret;

disable_vdda33:
	ret = regulator_disable(qphy->vdda33);
	if (ret)
		dev_err(qphy->phy.dev, "Unable to disable vdda33:%d\n", ret);

unset_vdd33:
	ret = regulator_set_voltage(qphy->vdda33, 0, QUSB2PHY_3P3_VOL_MAX);
	if (ret)
		dev_err(qphy->phy.dev,
			"Unable to set (0) voltage for vdda33:%d\n", ret);

put_vdda33_lpm:
	ret = regulator_set_load(qphy->vdda33, 0);
	if (ret < 0)
		dev_err(qphy->phy.dev, "Unable to set (0) HPM of vdda33\n");

disable_vdda18:
	ret = regulator_disable(qphy->vdda18);
	if (ret)
		dev_err(qphy->phy.dev, "Unable to disable vdda18:%d\n", ret);

unset_vdda18:
	ret = regulator_set_voltage(qphy->vdda18, 0, QUSB2PHY_1P8_VOL_MAX);
	if (ret)
		dev_err(qphy->phy.dev,
			"Unable to set (0) voltage for vdda18:%d\n", ret);

put_vdda18_lpm:
	ret = regulator_set_load(qphy->vdda18, 0);
	if (ret < 0)
		dev_err(qphy->phy.dev, "Unable to set LPM of vdda18\n");

disable_vdd:
	ret = regulator_disable(qphy->vdd);
	if (ret)
		dev_err(qphy->phy.dev, "Unable to disable vdd:%d\n",
								ret);

unconfig_vdd:
	ret = qusb_phy_config_vdd(qphy, false);
	if (ret)
		dev_err(qphy->phy.dev, "Unable unconfig VDD:%d\n",
								ret);
err_vdd:
	dev_dbg(qphy->phy.dev, "QUSB PHY's regulators are turned OFF.\n");
	qphy->power_enabled = false;
	return ret;
}

static void qusb_phy_get_tune2_param(struct qusb_phy *qphy)
{
	u32 bit_mask = 1;
	u8 reg_val;

	pr_debug("%s(): num_of_bits:%d bit_pos:%d\n", __func__,
				qphy->tune2_efuse_num_of_bits,
				qphy->tune2_efuse_bit_pos);

	/* get bit mask based on number of bits to use with efuse reg */
	bit_mask = (bit_mask << qphy->tune2_efuse_num_of_bits) - 1;

	/*
	 * Read EFUSE register having TUNE2 parameter's high nibble.
	 * If efuse register shows value as 0x0, then use previous value
	 * as it is. Otherwise use efuse register based value for this purpose.
	 */
	if (qphy->tune2_efuse_num_of_bits < HSTX_TRIMSIZE) {
		qphy->tune2_val =
		     TUNE2_HIGH_NIBBLE_VAL(readl_relaxed(qphy->tune2_efuse_reg),
		     qphy->tune2_efuse_bit_pos, bit_mask);
		bit_mask =
		     (1 << (HSTX_TRIMSIZE - qphy->tune2_efuse_num_of_bits)) - 1;
		qphy->tune2_val |=
		 TUNE2_HIGH_NIBBLE_VAL(readl_relaxed(qphy->tune2_efuse_reg + 4),
				0, bit_mask) << qphy->tune2_efuse_num_of_bits;
	} else {
		qphy->tune2_val = readl_relaxed(qphy->tune2_efuse_reg);
		qphy->tune2_val = TUNE2_HIGH_NIBBLE_VAL(qphy->tune2_val,
					qphy->tune2_efuse_bit_pos, bit_mask);
	}

	pr_debug("%s(): efuse based tune2 value:%d\n",
				__func__, qphy->tune2_val);

	/* Update higher nibble of TUNE2 value for better rise/fall times */
	if (qphy->tune2_efuse_correction && qphy->tune2_val) {
		if (qphy->tune2_efuse_correction > 5 ||
				qphy->tune2_efuse_correction < -10)
			pr_warn("Correction value is out of range : %d\n",
					qphy->tune2_efuse_correction);
		else
			qphy->tune2_val = qphy->tune2_val +
						qphy->tune2_efuse_correction;
	}

	reg_val = readb_relaxed(qphy->base + QUSB2PHY_PORT_TUNE2);
	if (qphy->tune2_val) {
		reg_val  &= 0x0f;
		reg_val |= (qphy->tune2_val << 4);
	}

	qphy->tune2_val = reg_val;
}

static void qusb_phy_write_seq(void __iomem *base, u32 *seq, int cnt,
		unsigned long delay)
{
	int i;

	pr_debug("Seq count:%d\n", cnt);
	for (i = 0; i < cnt; i = i+2) {
		pr_debug("write 0x%02x to 0x%02x\n", seq[i], seq[i+1]);
		writel_relaxed(seq[i], base + seq[i+1]);
		if (delay)
			usleep_range(delay, (delay + 2000));
	}
}

static int qusb_phy_init(struct usb_phy *phy)
{
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);
	int ret, reset_val = 0;
	u8 reg;
	bool pll_lock_fail = false;

	if (qphy->eud_enable_reg && readl_relaxed(qphy->eud_enable_reg)) {
		dev_err(qphy->phy.dev, "eud is enabled\n");
		return 0;
	}

	/*
	 * ref clock is enabled by default after power on reset. Linux clock
	 * driver will disable this clock as part of late init if peripheral
	 * driver(s) does not explicitly votes for it. Linux clock driver also
	 * does not disable the clock until late init even if peripheral
	 * driver explicitly requests it and cannot defer the probe until late
	 * init. Hence, Explicitly disable the clock using register write to
	 * allow QUSB PHY PLL to lock properly.
	 */
	if (qphy->ref_clk_base) {
		writel_relaxed((readl_relaxed(qphy->ref_clk_base) &
					~QUSB2PHY_REFCLK_ENABLE),
					qphy->ref_clk_base);
		/* Make sure that above write complete to get ref clk OFF */
		wmb();
	}

	/* Perform phy reset */
	ret = reset_control_assert(qphy->phy_reset);
	if (ret)
		dev_err(phy->dev, "%s: phy_reset assert failed\n", __func__);
	usleep_range(100, 150);
	ret = reset_control_deassert(qphy->phy_reset);
	if (ret)
		dev_err(phy->dev, "%s: phy_reset deassert failed\n", __func__);

	qusb_phy_enable_power(qphy, true);
	qusb_phy_enable_clocks(qphy, true);

	/* Disable the PHY */
	if (qphy->major_rev < 2)
		writel_relaxed(CLAMP_N_EN | FREEZIO_N | POWER_DOWN,
				qphy->base + QUSB2PHY_PORT_POWERDOWN);
	else
		writel_relaxed(readl_relaxed(qphy->base + QUSB2PHY_PWR_CTRL1) |
				PWR_CTRL1_POWR_DOWN,
				qphy->base + QUSB2PHY_PWR_CTRL1);

	/* configure for ULPI mode if requested */
	if (qphy->ulpi_mode)
		writel_relaxed(0x0, qphy->base + QUSB2PHY_PORT_UTMI_CTRL2);

	/* save reset value to override based on clk scheme */
	if (qphy->ref_clk_base)
		reset_val = readl_relaxed(qphy->base + QUSB2PHY_PLL_TEST);

	if (qphy->qusb_phy_init_seq)
		qusb_phy_write_seq(qphy->base, qphy->qusb_phy_init_seq,
				qphy->init_seq_len, 0);

	/*
	 * Check for EFUSE value only if tune2_efuse_reg is available
	 * and try to read EFUSE value only once i.e. not every USB
	 * cable connect case.
	 */
	if (qphy->tune2_efuse_reg && !qphy->tune2) {
		if (!qphy->tune2_val)
			qusb_phy_get_tune2_param(qphy);

		pr_debug("%s(): Programming TUNE2 parameter as:%x\n", __func__,
				qphy->tune2_val);
		writel_relaxed(qphy->tune2_val,
				qphy->base + QUSB2PHY_PORT_TUNE2);
	}

	/* If tune modparam set, override tune value */
	if (qphy->tune1) {
		writel_relaxed(qphy->tune1,
				qphy->base + QUSB2PHY_PORT_TUNE1);
	}

	if (qphy->tune2) {
		writel_relaxed(qphy->tune2,
				qphy->base + QUSB2PHY_PORT_TUNE2);
	}

	if (qphy->tune3) {
		writel_relaxed(qphy->tune3,
				qphy->base + QUSB2PHY_PORT_TUNE3);
	}

	if (qphy->tune4) {
		writel_relaxed(qphy->tune4,
				qphy->base + QUSB2PHY_PORT_TUNE4);
	}

	if (qphy->tune5) {
		writel_relaxed(qphy->tune5,
				qphy->base + QUSB2PHY_PORT_TUNE5);
	}

	/* ensure above writes are completed before re-enabling PHY */
	wmb();

	/* Enable the PHY */
	if (qphy->major_rev < 2)
		writel_relaxed(CLAMP_N_EN | FREEZIO_N,
				qphy->base + QUSB2PHY_PORT_POWERDOWN);
	else
		writel_relaxed(readl_relaxed(qphy->base + QUSB2PHY_PWR_CTRL1) &
				~PWR_CTRL1_POWR_DOWN,
				qphy->base + QUSB2PHY_PWR_CTRL1);

	/* Ensure above write is completed before turning ON ref clk */
	wmb();

	/* Require to get phy pll lock successfully */
	usleep_range(150, 160);

	/* Turn on phy ref_clk if DIFF_CLK else select SE_CLK */
	if (qphy->ref_clk_base) {
		if (!qphy->is_se_clk) {
			reset_val &= ~CLK_REF_SEL;
			writel_relaxed((readl_relaxed(qphy->ref_clk_base) |
					QUSB2PHY_REFCLK_ENABLE),
					qphy->ref_clk_base);
		} else {
			reset_val |= CLK_REF_SEL;
			writel_relaxed(reset_val,
					qphy->base + QUSB2PHY_PLL_TEST);
		}

		/* Make sure above write is completed to get PLL source clock */
		wmb();

		/* Required to get PHY PLL lock successfully */
		usleep_range(100, 110);
	}

	if (qphy->major_rev < 2) {
		reg = readb_relaxed(qphy->base + QUSB2PHY_PLL_STATUS);
		dev_dbg(phy->dev, "QUSB2PHY_PLL_STATUS:%x\n", reg);
		if (!(reg & QUSB2PHY_PLL_LOCK))
			pll_lock_fail = true;
	} else {
		reg = readb_relaxed(qphy->base +
				QUSB2PHY_PLL_COMMON_STATUS_ONE);
		dev_dbg(phy->dev, "QUSB2PHY_PLL_COMMON_STATUS_ONE:%x\n", reg);
		if (!(reg & CORE_READY_STATUS))
			pll_lock_fail = true;
	}

	if (pll_lock_fail)
		dev_err(phy->dev, "QUSB PHY PLL LOCK fails:%x\n", reg);

	return 0;
}

static void qusb_phy_shutdown(struct usb_phy *phy)
{
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);

	if (qphy->eud_enable_reg && readl_relaxed(qphy->eud_enable_reg)) {
		dev_err(qphy->phy.dev, "eud is enabled\n");
		return;
	}

	qusb_phy_enable_clocks(qphy, true);

	/* Disable the PHY */
	if (qphy->major_rev < 2)
		writel_relaxed(CLAMP_N_EN | FREEZIO_N | POWER_DOWN,
				qphy->base + QUSB2PHY_PORT_POWERDOWN);
	else
		writel_relaxed(readl_relaxed(qphy->base + QUSB2PHY_PWR_CTRL1) |
				PWR_CTRL1_POWR_DOWN,
				qphy->base + QUSB2PHY_PWR_CTRL1);

	/* Make sure above write complete before turning off clocks */
	wmb();

	qusb_phy_enable_clocks(qphy, false);
}
/**
 * Performs QUSB2 PHY suspend/resume functionality.
 *
 * @uphy - usb phy pointer.
 * @suspend - to enable suspend or not. 1 - suspend, 0 - resume
 *
 */
static int qusb_phy_set_suspend(struct usb_phy *phy, int suspend)
{
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);
	u32 linestate = 0, intr_mask = 0;

	if (qphy->suspended == suspend) {
		dev_dbg(phy->dev, "%s: USB PHY is already suspended\n",
			__func__);
		return 0;
	}

	if (suspend) {
		/* Bus suspend case */
		if (qphy->cable_connected) {
			/* Clear all interrupts */
			writel_relaxed(0x00,
				qphy->base + QUSB2PHY_PORT_INTR_CTRL);

			linestate = readl_relaxed(qphy->base +
					QUSB2PHY_PORT_UTMI_STATUS);

			/*
			 * D+/D- interrupts are level-triggered, but we are
			 * only interested if the line state changes, so enable
			 * the high/low trigger based on current state. In
			 * other words, enable the triggers _opposite_ of what
			 * the current D+/D- levels are.
			 * e.g. if currently D+ high, D- low (HS 'J'/Suspend),
			 * configure the mask to trigger on D+ low OR D- high
			 */
			intr_mask = DPSE_INTR_EN | DMSE_INTR_EN;
			if (!(linestate & LINESTATE_DP)) /* D+ low */
				intr_mask |= DPSE_INTR_HIGH_SEL;
			if (!(linestate & LINESTATE_DM)) /* D- low */
				intr_mask |= DMSE_INTR_HIGH_SEL;

			writel_relaxed(intr_mask,
				qphy->base + QUSB2PHY_PORT_INTR_CTRL);

			if (linestate & (LINESTATE_DP | LINESTATE_DM)) {
				/* enable phy auto-resume */
				writel_relaxed(0x0C,
					qphy->base + QUSB2PHY_PORT_TEST_CTRL);
				/* flush the previous write before next write */
				wmb();
				writel_relaxed(0x04,
					qphy->base + QUSB2PHY_PORT_TEST_CTRL);
			}


			dev_dbg(phy->dev, "%s: intr_mask = %x\n",
			__func__, intr_mask);

			/* Makes sure that above write goes through */
			wmb();

			qusb_phy_enable_clocks(qphy, false);
		} else { /* Disconnect case */
			mutex_lock(&qphy->phy_lock);
			/* Disable all interrupts */
			writel_relaxed(0x00,
				qphy->base + QUSB2PHY_PORT_INTR_CTRL);

			if (!qphy->eud_enable_reg ||
					!readl_relaxed(qphy->eud_enable_reg)) {
				if (!(qphy->phy.flags & PHY_HOST_MODE)) {
					/* Disable PHY */
					writel_relaxed(POWER_DOWN |
						readl_relaxed(qphy->base +
						QUSB2PHY_PORT_POWERDOWN),
						qphy->base + QUSB2PHY_PORT_POWERDOWN);
					/* Make sure that above write is completed */
					wmb();

					qusb_phy_update_tcsr_level_shifter(qphy, 0);
				}
			}

			qusb_phy_enable_clocks(qphy, false);
			qusb_phy_enable_power(qphy, false);
			mutex_unlock(&qphy->phy_lock);

			/*
			 * Set put_into_high_z_state to true so next USB
			 * cable connect, DPF_DMF request performs PHY
			 * reset and put it into high-z state. For bootup
			 * with or without USB cable, it doesn't require
			 * to put QUSB PHY into high-z state.
			 */
			qphy->put_into_high_z_state = true;
		}
		qphy->suspended = true;
	} else {
		/* Bus suspend case */
		if (qphy->cable_connected) {
			qusb_phy_enable_clocks(qphy, true);
			/* Clear all interrupts on resume */
			writel_relaxed(0x00,
				qphy->base + QUSB2PHY_PORT_INTR_CTRL);
		} else {
			qusb_phy_enable_power(qphy, true);
			qusb_phy_update_tcsr_level_shifter(qphy, 1);
			qusb_phy_enable_clocks(qphy, true);
		}
		qphy->suspended = false;
	}

	return 0;
}

static int qusb_phy_notify_connect(struct usb_phy *phy,
					enum usb_device_speed speed)
{
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);

	qphy->cable_connected = true;

	dev_dbg(phy->dev, "QUSB PHY: connect notification cable_connected=%d\n",
							qphy->cable_connected);
	return 0;
}

static int qusb_phy_notify_disconnect(struct usb_phy *phy,
					enum usb_device_speed speed)
{
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);

	qphy->cable_connected = false;

	dev_dbg(phy->dev, "QUSB PHY: connect notification cable_connected=%d\n",
							qphy->cable_connected);
	return 0;
}
#define DP_PULSE_WIDTH_MSEC 200
static enum usb_charger_type qusb_phy_drive_dp_pulse(struct usb_phy *phy)
{
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);
	int ret;

	dev_dbg(qphy->phy.dev, "connected to a CDP, drive DP up\n");
	ret = qusb_phy_enable_power(qphy, true);
	if (ret < 0) {
		dev_dbg(qphy->phy.dev,
			"dpdm regulator enable failed:%d\n", ret);
		return 0;
	}
	qusb_phy_gdsc(qphy, true);
	qusb_phy_enable_clocks(qphy, true);

	ret = reset_control_assert(qphy->phy_reset);
	if (ret)
		dev_err(qphy->phy.dev, "phyassert failed\n");
	usleep_range(100, 150);
	ret = reset_control_deassert(qphy->phy_reset);
	if (ret)
		dev_err(qphy->phy.dev, "deassert failed\n");

	/* Configure PHY to enable control on DP/DM lines */
	writel_relaxed(CLAMP_N_EN | FREEZIO_N | POWER_DOWN,
				qphy->base + QUSB2PHY_PORT_POWERDOWN);

	writel_relaxed(TERM_SELECT | XCVR_SELECT_FS | OP_MODE_NON_DRIVE |
			SUSPEND_N, qphy->base + QUSB2PHY_PORT_UTMI_CTRL1);

	writel_relaxed(UTMI_ULPI_SEL | UTMI_TEST_MUX_SEL,
				qphy->base + QUSB2PHY_PORT_UTMI_CTRL2);

	writel_relaxed(PLL_RESET_N_CNT_5,
			qphy->base + QUSB2PHY_PLL_AUTOPGM_CTL1);

	writel_relaxed(CLAMP_N_EN | FREEZIO_N,
			qphy->base + QUSB2PHY_PORT_POWERDOWN);

	writel_relaxed(REF_BUF_EN | REXT_EN | PLL_BYPASSNL | REXT_TRIM_0,
			qphy->base + QUSB2PHY_PLL_PWR_CTL);

	usleep_range(5, 10);

	writel_relaxed(0x15, qphy->base + QUSB2PHY_PLL_AUTOPGM_CTL1);
	writel_relaxed(PLL_RESET_N | PLL_RESET_N_CNT_5,
			qphy->base + QUSB2PHY_PLL_AUTOPGM_CTL1);

	writel_relaxed(0x00, qphy->base + QUSB2PHY_PORT_QC1);
	writel_relaxed(0x00, qphy->base + QUSB2PHY_PORT_QC2);

	usleep_range(50, 60);
	/* Enable Rdp_en to pull DP up to 3V */
	writel_relaxed(RDP_UP_EN, qphy->base + QUSB2PHY_PORT_QC2);
	msleep(DP_PULSE_WIDTH_MSEC);

	/* Put the PHY and DP back to normal state */
	writel_relaxed(CLAMP_N_EN | FREEZIO_N | POWER_DOWN,
			qphy->base + QUSB2PHY_PORT_POWERDOWN);  /* 23 */

	writel_relaxed(PLL_AUTOPGM_EN | PLL_RESET_N | PLL_RESET_N_CNT_5,
			qphy->base + QUSB2PHY_PLL_AUTOPGM_CTL1);

	writel_relaxed(UTMI_ULPI_SEL, qphy->base + QUSB2PHY_PORT_UTMI_CTRL2);

	writel_relaxed(TERM_SELECT, qphy->base + QUSB2PHY_PORT_UTMI_CTRL1);

	qusb_phy_enable_clocks(qphy, false);
	qusb_phy_gdsc(qphy, false);

	ret = qusb_phy_enable_power(qphy, false);
	if (ret < 0) {
		dev_dbg(qphy->phy.dev,
			"dpdm regulator disable failed:%d\n", ret);
	}

	return 0;
}

static int qusb_phy_dpdm_regulator_enable(struct regulator_dev *rdev)
{
	int ret = 0;
	struct qusb_phy *qphy = rdev_get_drvdata(rdev);

	dev_dbg(qphy->phy.dev, "%s dpdm_enable:%d\n",
				__func__, qphy->dpdm_enable);

	if (qphy->eud_enable_reg && readl_relaxed(qphy->eud_enable_reg)) {
		dev_err(qphy->phy.dev, "eud is enabled\n");
		return 0;
	}

	mutex_lock(&qphy->phy_lock);
	if (!qphy->dpdm_enable) {
		ret = qusb_phy_enable_power(qphy, true);
		if (ret < 0) {
			dev_dbg(qphy->phy.dev,
				"dpdm regulator enable failed:%d\n", ret);
			mutex_unlock(&qphy->phy_lock);
			return ret;
		}
		qphy->dpdm_enable = true;
		if (qphy->put_into_high_z_state) {
			qusb_phy_update_tcsr_level_shifter(qphy, 1);

			qusb_phy_gdsc(qphy, true);
			qusb_phy_enable_clocks(qphy, true);

			dev_dbg(qphy->phy.dev, "RESET QUSB PHY\n");
			ret = reset_control_assert(qphy->phy_reset);
			if (ret)
				dev_err(qphy->phy.dev, "phyassert failed\n");
			usleep_range(100, 150);
			ret = reset_control_deassert(qphy->phy_reset);
			if (ret)
				dev_err(qphy->phy.dev, "deassert failed\n");

			/*
			 * Phy in non-driving mode leaves Dp and Dm
			 * lines in high-Z state. Controller power
			 * collapse is not switching phy to non-driving
			 * mode causing charger detection failure. Bring
			 * phy to non-driving mode by overriding
			 * controller output via UTMI interface.
			 */
			writel_relaxed(TERM_SELECT | XCVR_SELECT_FS |
				OP_MODE_NON_DRIVE,
				qphy->base + QUSB2PHY_PORT_UTMI_CTRL1);
			writel_relaxed(UTMI_ULPI_SEL |
				UTMI_TEST_MUX_SEL,
				qphy->base + QUSB2PHY_PORT_UTMI_CTRL2);


			/* Disable PHY */
			writel_relaxed(CLAMP_N_EN | FREEZIO_N |
					POWER_DOWN,
					qphy->base + QUSB2PHY_PORT_POWERDOWN);
			/* Make sure that above write is completed */
			wmb();

			qusb_phy_enable_clocks(qphy, false);
			qusb_phy_gdsc(qphy, false);
		}
	}
	mutex_unlock(&qphy->phy_lock);

	return ret;
}

static int qusb_phy_dpdm_regulator_disable(struct regulator_dev *rdev)
{
	int ret = 0;
	struct qusb_phy *qphy = rdev_get_drvdata(rdev);

	dev_dbg(qphy->phy.dev, "%s dpdm_enable:%d\n",
				__func__, qphy->dpdm_enable);

	mutex_lock(&qphy->phy_lock);
	if (qphy->dpdm_enable) {
		/* If usb core is active, rely on set_suspend to clamp phy */
		if (!qphy->cable_connected)
			qusb_phy_update_tcsr_level_shifter(qphy, 0);
		ret = qusb_phy_enable_power(qphy, false);
		if (ret < 0) {
			dev_dbg(qphy->phy.dev,
				"dpdm regulator disable failed:%d\n", ret);
			mutex_unlock(&qphy->phy_lock);
			return ret;
		}
		qphy->dpdm_enable = false;
	}
	mutex_unlock(&qphy->phy_lock);

	return ret;
}

static int qusb_phy_dpdm_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct qusb_phy *qphy = rdev_get_drvdata(rdev);

	dev_dbg(qphy->phy.dev, "%s qphy->dpdm_enable = %d\n", __func__,
					qphy->dpdm_enable);
	return qphy->dpdm_enable;
}

static const struct regulator_ops qusb_phy_dpdm_regulator_ops = {
	.enable		= qusb_phy_dpdm_regulator_enable,
	.disable	= qusb_phy_dpdm_regulator_disable,
	.is_enabled	= qusb_phy_dpdm_regulator_is_enabled,
};

static int qusb_phy_regulator_init(struct qusb_phy *qphy)
{
	struct device *dev = qphy->phy.dev;
	struct regulator_config cfg = {};
	struct regulator_init_data *init_data;

	init_data = devm_kzalloc(dev, sizeof(*init_data), GFP_KERNEL);
	if (!init_data)
		return -ENOMEM;

	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_STATUS;
	qphy->dpdm_rdesc.owner = THIS_MODULE;
	qphy->dpdm_rdesc.type = REGULATOR_VOLTAGE;
	qphy->dpdm_rdesc.ops = &qusb_phy_dpdm_regulator_ops;
	qphy->dpdm_rdesc.name = kbasename(dev->of_node->full_name);

	cfg.dev = dev;
	cfg.init_data = init_data;
	cfg.driver_data = qphy;
	cfg.of_node = dev->of_node;

	qphy->dpdm_rdev = devm_regulator_register(dev, &qphy->dpdm_rdesc, &cfg);
	return PTR_ERR_OR_ZERO(qphy->dpdm_rdev);

}

static void qusb_phy_create_debugfs(struct qusb_phy *qphy)
{
	qphy->root = debugfs_create_dir(dev_name(qphy->phy.dev), NULL);
	debugfs_create_x8("tune1", 0644, qphy->root, &qphy->tune1);
	debugfs_create_x8("tune2", 0644, qphy->root, &qphy->tune2);
	debugfs_create_x8("tune3", 0644, qphy->root, &qphy->tune3);
	debugfs_create_x8("tune4", 0644, qphy->root, &qphy->tune4);
	debugfs_create_x8("tune5", 0644, qphy->root, &qphy->tune5);
}

static int qusb_phy_vbus_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct usb_phy *phy = container_of(nb, struct usb_phy, vbus_nb);
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);

	if (!qphy || !data) {
		pr_err("Failed to get PHY for vbus_notifier\n");
		return NOTIFY_DONE;
	}

	qphy->vbus_active = !!event;
	dev_dbg(qphy->phy.dev, "Got VBUS notification: %u\n", event);
	queue_delayed_work(system_freezable_wq, &qphy->port_det_w, 0);

	return NOTIFY_DONE;
}

static int qusb_phy_id_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct usb_phy *phy = container_of(nb, struct usb_phy, id_nb);
	struct qusb_phy *qphy = container_of(phy, struct qusb_phy, phy);

	if (!qphy || !data) {
		pr_err("Failed to get PHY for vbus_notifier\n");
		return NOTIFY_DONE;
	}

	qphy->id_state = !event;
	dev_dbg(qphy->phy.dev, "Got id notification: %u\n", event);
	queue_delayed_work(system_freezable_wq, &qphy->port_det_w, 0);

	return NOTIFY_DONE;
}

static const unsigned int qusb_phy_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static int qusb_phy_notify_charger(struct qusb_phy *qphy,
					enum power_supply_type charger_type)
{
	union power_supply_propval pval = {0};

	dev_dbg(qphy->phy.dev, "Notify charger type: %d\n", charger_type);

	if (!qphy->usb_psy) {
		qphy->usb_psy = power_supply_get_by_name("usb");
		if (!qphy->usb_psy) {
			dev_err(qphy->phy.dev, "Could not get usb psy\n");
			return -ENODEV;
		}
	}

	pval.intval = charger_type;
	power_supply_set_property(qphy->usb_psy, POWER_SUPPLY_PROP_USB_TYPE,
									&pval);
	return 0;
}

static void qusb_phy_notify_extcon(struct qusb_phy *qphy,
						int extcon_id, int event)
{
	struct extcon_dev *edev = qphy->phy.edev;
	union extcon_property_value val;
	int ret;

	dev_dbg(qphy->phy.dev, "Notify event: %d for extcon_id: %d\n",
					event, extcon_id);

	if (event) {
		ret = extcon_get_property(edev, extcon_id,
					EXTCON_PROP_USB_TYPEC_POLARITY, &val);
		if (ret)
			dev_err(qphy->phy.dev, "Failed to get TYPEC POLARITY\n");

		extcon_set_property(qphy->usb_extcon, extcon_id,
					EXTCON_PROP_USB_TYPEC_POLARITY, val);

		ret = extcon_get_property(edev, extcon_id,
						EXTCON_PROP_USB_SS, &val);
		if (ret)
			dev_err(qphy->phy.dev, "Failed to get USB_SS property\n");

		extcon_set_property(qphy->usb_extcon, extcon_id,
						EXTCON_PROP_USB_SS, val);
	}

	extcon_set_state_sync(qphy->usb_extcon, extcon_id, event);
}

static bool qusb_phy_chg_det_status(struct qusb_phy *qphy,
						enum chg_det_state state)
{
	u32 reg, status;

	reg = readl_relaxed(qphy->base + QUSB2PHY_PORT_INTR_STATUS);
	dev_dbg(qphy->phy.dev, "state: %d reg: 0x%x\n", state, reg);

	status = reg & 0xff;

	switch (state) {
	case STATE_DCD:
		return (status != DPSE_INTR_HIGH);
	case STATE_PRIMARY:
		return (status && (status != DPSE_INTR_HIGH));
	case STATE_SECONDARY:
		return status;
	case STATE_UNKNOWN:
	default:
		break;
	}

	return false;
}

/*
 * Different circuit blocks are enabled on DP and DM lines as part
 * of different phases of charger detection. Then the state of
 * DP and DM lines are monitored to identify different type of
 * chargers.
 * These circuit blocks can be enabled with the configuration of
 * the QUICKCHARGE1 and QUICKCHARGE2 registers and the DP/DM lines
 * can be monitored with the status of the INTR_STATUS register.
 */
static void qusb_phy_chg_det_enable_seq(struct qusb_phy *qphy, int state)
{
	dev_dbg(qphy->phy.dev, "state: %d\n", state);
	/* Power down the PHY*/
	writel_relaxed(0x23, qphy->base + QUSB2PHY_PORT_POWERDOWN);

	/* Put the PHY in non driving mode */
	writel_relaxed(0x35, qphy->base + QUSB2PHY_PORT_UTMI_CTRL1);

	/* Set the PHY to register mode */
	writel_relaxed(0xC0, qphy->base + QUSB2PHY_PORT_UTMI_CTRL2);

	/* Keep PLL in reset */
	writel_relaxed(0x05, qphy->base + QUSB2PHY_PLL_AUTOPGM_CTL1);

	/* Enable  PHY */
	writel_relaxed(0x22, qphy->base + QUSB2PHY_PORT_POWERDOWN);

	writel_relaxed(0x17, qphy->base + QUSB2PHY_PLL_PWR_CTL);

	usleep_range(5, 10);

	writel_relaxed(0x15, qphy->base + QUSB2PHY_PLL_AUTOPGM_CTL1);

	writel_relaxed(0x00, qphy->base + QUSB2PHY_PORT_QC1);
	writel_relaxed(0x00, qphy->base + QUSB2PHY_PORT_QC2);

	usleep_range(50, 60);

	switch (state) {
	case STATE_DCD:
		/* Enable IDP_SRC */
		writel_relaxed(0x08, qphy->base + QUSB2PHY_PORT_QC1);
		/* Enable RDM_UP */
		writel_relaxed(0x01, qphy->base + QUSB2PHY_PORT_QC2);

		writel_relaxed(0x1F, qphy->base + QUSB2PHY_PORT_INTR_CTRL);
		break;
	case STATE_PRIMARY:
		/* Enable VDAT_REF_DM, VDP_SRC and IDM_SINK */
		writel_relaxed(0x25, qphy->base + QUSB2PHY_PORT_QC1);
		writel_relaxed(0x00, qphy->base + QUSB2PHY_PORT_QC2);

		writel_relaxed(0x1F, qphy->base + QUSB2PHY_PORT_INTR_CTRL);
		break;
	case STATE_SECONDARY:
		/* Enable VDAT_REF_DP, VDAT_REF_DM, VDP_SRC and IDM_SINK */
		writel_relaxed(0x72, qphy->base + QUSB2PHY_PORT_QC1);
		writel_relaxed(0x00, qphy->base + QUSB2PHY_PORT_QC2);

		writel_relaxed(0x1F, qphy->base + QUSB2PHY_PORT_INTR_CTRL);
		break;
	case STATE_UNKNOWN:
	default:
		break;
	}
}

#define CHG_DCD_TIMEOUT_MSEC		750
#define CHG_DCD_POLL_TIME_MSEC		50
#define CHG_PRIMARY_DET_TIME_MSEC	100
#define CHG_SECONDARY_DET_TIME_MSEC	100

static int qusb_phy_enable_phy(struct qusb_phy *qphy)
{
	int ret;

	ret = qusb_phy_enable_power(qphy, true);
	if (ret)
		return ret;

	if (qphy->tcsr_clamp_dig_n)
		writel_relaxed(0x1, qphy->tcsr_clamp_dig_n);
	qusb_phy_enable_clocks(qphy, true);

	return 0;
}

static void qusb_phy_disable_phy(struct qusb_phy *qphy)
{
	int ret;

	ret = reset_control_assert(qphy->phy_reset);
	if (ret)
		dev_err(qphy->phy.dev, "phyassert failed\n");

	usleep_range(100, 150);

	ret = reset_control_deassert(qphy->phy_reset);
	if (ret)
		dev_err(qphy->phy.dev, "deassert failed\n");

	qusb_phy_enable_clocks(qphy, false);
	if (qphy->tcsr_clamp_dig_n)
		writel_relaxed(0x0, qphy->tcsr_clamp_dig_n);
	qusb_phy_enable_power(qphy, false);
}

static void qusb_phy_port_state_work(struct work_struct *w)
{
	struct qusb_phy *qphy = container_of(w, struct qusb_phy,
							port_det_w.work);
	unsigned long delay = 0;
	int status, ret;

	dev_dbg(qphy->phy.dev, "state: %d\n", qphy->port_state);

	switch (qphy->port_state) {
	case PORT_UNKNOWN:
		if (!qphy->id_state) {
			qphy->port_state = PORT_HOST_MODE;
			qusb_phy_notify_extcon(qphy, EXTCON_USB_HOST, 1);
			return;
		}

		if (qphy->vbus_active) {
			/* Enable DCD sequence */
			ret = qusb_phy_enable_phy(qphy);
			if (ret)
				return;

			qusb_phy_chg_det_enable_seq(qphy, STATE_DCD);
			qphy->port_state = PORT_DCD_IN_PROGRESS;
			qphy->dcd_timeout = 0;
			delay = CHG_DCD_POLL_TIME_MSEC;
			break;
		}
		return;
	case PORT_DISCONNECTED:
		qusb_phy_disable_phy(qphy);
		qphy->port_state = PORT_UNKNOWN;
		break;
	case PORT_DCD_IN_PROGRESS:
		if (!qphy->vbus_active) {
			/* Disable PHY sequence */
			qphy->port_state = PORT_DISCONNECTED;
			break;
		}

		status = qusb_phy_chg_det_status(qphy, STATE_DCD);
		if (!status && qphy->dcd_timeout < CHG_DCD_TIMEOUT_MSEC) {
			delay = CHG_DCD_POLL_TIME_MSEC;
			qphy->dcd_timeout += delay;
		} else if (status) {
			qusb_phy_chg_det_enable_seq(qphy, STATE_PRIMARY);
			qphy->port_state = PORT_PRIMARY_IN_PROGRESS;
			delay = CHG_PRIMARY_DET_TIME_MSEC;
		} else if (qphy->dcd_timeout >= CHG_DCD_TIMEOUT_MSEC) {
			qusb_phy_notify_charger(qphy,
						POWER_SUPPLY_TYPE_USB_DCP);
			qusb_phy_disable_phy(qphy);
			qphy->port_state = PORT_CHG_DET_DONE;
		}
		break;
	case PORT_PRIMARY_IN_PROGRESS:
		if (!qphy->vbus_active) {
			qphy->port_state = PORT_DISCONNECTED;
			break;
		}

		status = qusb_phy_chg_det_status(qphy, STATE_PRIMARY);
		if (status) {
			qusb_phy_chg_det_enable_seq(qphy, STATE_SECONDARY);
			qphy->port_state = PORT_SECONDARY_IN_PROGRESS;
			delay = CHG_SECONDARY_DET_TIME_MSEC;

		} else {
			qusb_phy_disable_phy(qphy);
			qusb_phy_notify_charger(qphy, POWER_SUPPLY_TYPE_USB);
			qusb_phy_notify_extcon(qphy, EXTCON_USB, 1);
			qphy->port_state = PORT_CHG_DET_DONE;
		}
		break;
	case PORT_SECONDARY_IN_PROGRESS:
		if (!qphy->vbus_active) {
			qphy->port_state = PORT_DISCONNECTED;
			break;
		}

		status = qusb_phy_chg_det_status(qphy, STATE_SECONDARY);
		if (status) {
			qusb_phy_notify_charger(qphy,
						POWER_SUPPLY_TYPE_USB_DCP);
		} else {
			qusb_phy_notify_charger(qphy,
						POWER_SUPPLY_TYPE_USB_CDP);
			qusb_phy_notify_extcon(qphy, EXTCON_USB, 1);
		}

		qusb_phy_disable_phy(qphy);
		qphy->port_state = PORT_CHG_DET_DONE;
		break;
	case PORT_CHG_DET_DONE:
		if (!qphy->vbus_active) {
			qphy->port_state = PORT_UNKNOWN;
			qusb_phy_notify_extcon(qphy, EXTCON_USB, 0);
		}

		return;
	case PORT_HOST_MODE:
		if (qphy->id_state) {
			qphy->port_state = PORT_UNKNOWN;
			qusb_phy_notify_extcon(qphy, EXTCON_USB_HOST, 0);
		}

		if (!qphy->vbus_active)
			return;

		break;
	default:
		return;
	}

	queue_delayed_work(system_freezable_wq,
			&qphy->port_det_w, msecs_to_jiffies(delay));
}

static int qusb_phy_extcon_register(struct qusb_phy *qphy)
{
	int ret;

	/* Register extcon for notifications from charger driver */
	qphy->phy.vbus_nb.notifier_call = qusb_phy_vbus_notifier;

	qphy->phy.id_nb.notifier_call = qusb_phy_id_notifier;

	/* Register extcon to notify USB driver */
	qphy->usb_extcon = devm_extcon_dev_allocate(qphy->phy.dev,
						qusb_phy_extcon_cable);
	if (IS_ERR(qphy->usb_extcon)) {
		dev_err(qphy->phy.dev, "failed to allocate extcon device\n");
		return PTR_ERR(qphy->usb_extcon);
	}

	ret = devm_extcon_dev_register(qphy->phy.dev, qphy->usb_extcon);
	if (ret) {
		dev_err(qphy->phy.dev, "failed to register extcon device\n");
		return ret;
	}

	extcon_set_property_capability(qphy->usb_extcon, EXTCON_USB,
			EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(qphy->usb_extcon, EXTCON_USB,
			EXTCON_PROP_USB_SS);
	extcon_set_property_capability(qphy->usb_extcon, EXTCON_USB_HOST,
			EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(qphy->usb_extcon, EXTCON_USB_HOST,
			EXTCON_PROP_USB_SS);
	return 0;
}

static int qusb_phy_probe(struct platform_device *pdev)
{
	struct qusb_phy *qphy;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret = 0, size = 0;
	const char *phy_type;
	bool hold_phy_reset;
	u32 temp;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->phy.dev = dev;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"qusb_phy_base");
	qphy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qphy->base))
		return PTR_ERR(qphy->base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"tune2_efuse_addr");
	if (res) {
		qphy->tune2_efuse_reg = devm_ioremap(dev, res->start,
							resource_size(res));
		if (!IS_ERR_OR_NULL(qphy->tune2_efuse_reg)) {
			ret = of_property_read_u32(dev->of_node,
					"qcom,tune2-efuse-bit-pos",
					&qphy->tune2_efuse_bit_pos);
			if (!ret) {
				ret = of_property_read_u32(dev->of_node,
						"qcom,tune2-efuse-num-bits",
						&qphy->tune2_efuse_num_of_bits);
			}
			of_property_read_u32(dev->of_node,
						"qcom,tune2-efuse-correction",
						&qphy->tune2_efuse_correction);

			if (ret) {
				dev_err(dev, "DT Value for tune2 efuse is invalid.\n");
				return -EINVAL;
			}
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"eud_enable_reg");
	if (res) {
		qphy->eud_enable_reg = devm_ioremap_resource(dev, res);
		if (IS_ERR(qphy->eud_enable_reg)) {
			dev_err(dev, "err getting eud_enable_reg address\n");
			return PTR_ERR(qphy->eud_enable_reg);
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"ref_clk_addr");
	if (res) {
		qphy->ref_clk_base = devm_ioremap(dev,
				res->start, resource_size(res));
		if (IS_ERR(qphy->ref_clk_base)) {
			dev_dbg(dev, "ref_clk_address is not available.\n");
			return PTR_ERR(qphy->ref_clk_base);
		}

		ret = of_property_read_string(dev->of_node,
				"qcom,phy-clk-scheme", &phy_type);
		if (ret) {
			dev_err(dev, "error need qsub_phy_clk_scheme.\n");
			return ret;
		}

		if (!strcasecmp(phy_type, "cml")) {
			qphy->is_se_clk = false;
		} else if (!strcasecmp(phy_type, "cmos")) {
			qphy->is_se_clk = true;
		} else {
			dev_err(dev, "erro invalid qusb_phy_clk_scheme\n");
			return -EINVAL;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"tcsr_clamp_dig_n_1p8");
	if (res) {
		qphy->tcsr_clamp_dig_n = devm_ioremap(dev,
				res->start, resource_size(res));
		if (IS_ERR(qphy->tcsr_clamp_dig_n)) {
			dev_err(dev, "err reading tcsr_clamp_dig_n\n");
			qphy->tcsr_clamp_dig_n = NULL;
		}
	}

	qphy->scm_lvl_shifter = of_property_read_bool(dev->of_node,
					"qcom,secure-level-shifter");

	ret = of_property_read_u32(dev->of_node, "qcom,usb-hs-ac-bitmask",
					&qphy->usb_hs_ac_bitmask);
	if (!ret) {
		ret = of_property_read_u32(dev->of_node, "qcom,usb-hs-ac-value",
						&qphy->usb_hs_ac_value);
		if (ret) {
			dev_err(dev, "%s usb_hs_ac_value not passed\n", __func__);
			return ret;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"tcsr_conn_box_spare_0");
		if (!res) {
			dev_err(dev, "%s tcsr_conn_box_spare_0 not passed\n",
								__func__);
			return -ENOENT;
		}

		qphy->tcsr_conn_box_spare = devm_ioremap(dev,
						res->start, resource_size(res));
		if (IS_ERR(qphy->tcsr_conn_box_spare)) {
			dev_err(dev, "err reading tcsr_conn_box_spare\n");
			return PTR_ERR(qphy->tcsr_conn_box_spare);
		}
	}

	qphy->ref_clk_src = devm_clk_get(dev, "ref_clk_src");
	if (IS_ERR(qphy->ref_clk_src)) {
		qphy->ref_clk_src = NULL;
		dev_dbg(dev, "clk get failed for ref_clk_src\n");
	}

	/* ref_clk is needed only for DIFF_CLK case, hence make it optional. */
	if (of_property_match_string(pdev->dev.of_node,
				"clock-names", "ref_clk") >= 0) {
		qphy->ref_clk = devm_clk_get(dev, "ref_clk");
		if (IS_ERR(qphy->ref_clk)) {
			ret = PTR_ERR(qphy->ref_clk);
			if (ret != -EPROBE_DEFER)
				dev_dbg(dev,
					"clk get failed for ref_clk\n");
			return ret;
		}

		clk_set_rate(qphy->ref_clk, 19200000);
	}

	qphy->cfg_ahb_clk = devm_clk_get(dev, "cfg_ahb_clk");
	if (IS_ERR(qphy->cfg_ahb_clk))
		return PTR_ERR(qphy->cfg_ahb_clk);

	qphy->phy_reset = devm_reset_control_get(dev, "phy_reset");
	if (IS_ERR(qphy->phy_reset))
		return PTR_ERR(qphy->phy_reset);

	if (of_property_match_string(dev->of_node,
		"clock-names", "iface_clk") >= 0) {
		qphy->iface_clk = devm_clk_get(dev, "iface_clk");
		if (IS_ERR(qphy->iface_clk)) {
			ret = PTR_ERR(qphy->iface_clk);
			qphy->iface_clk = NULL;
			if (ret == -EPROBE_DEFER)
				return ret;
			dev_err(dev, "couldn't get iface_clk(%d)\n", ret);
		}
	}

	if (of_property_match_string(dev->of_node,
		"clock-names", "core_clk") >= 0) {
		qphy->core_clk = devm_clk_get(dev, "core_clk");
		if (IS_ERR(qphy->core_clk)) {
			ret = PTR_ERR(qphy->core_clk);
			qphy->core_clk = NULL;
			if (ret == -EPROBE_DEFER)
				return ret;
			dev_err(dev, "couldn't get core_clk(%d)\n", ret);
		}
	}

	qphy->gdsc = devm_regulator_get(dev, "USB3_GDSC");
	if (IS_ERR(qphy->gdsc))
		qphy->gdsc = NULL;

	size = 0;
	of_get_property(dev->of_node, "qcom,qusb-phy-init-seq", &size);
	if (size) {
		qphy->qusb_phy_init_seq = devm_kzalloc(dev,
						size, GFP_KERNEL);
		if (qphy->qusb_phy_init_seq) {
			qphy->init_seq_len =
				(size / sizeof(*qphy->qusb_phy_init_seq));
			if (qphy->init_seq_len % 2) {
				dev_err(dev, "invalid init_seq_len\n");
				return -EINVAL;
			}

			of_property_read_u32_array(dev->of_node,
				"qcom,qusb-phy-init-seq",
				qphy->qusb_phy_init_seq,
				qphy->init_seq_len);
		} else {
			dev_err(dev, "error allocating memory for phy_init_seq\n");
		}
	}

	qphy->ulpi_mode = false;
	ret = of_property_read_string(dev->of_node, "phy_type", &phy_type);

	if (!ret) {
		if (!strcasecmp(phy_type, "ulpi"))
			qphy->ulpi_mode = true;
	} else {
		dev_err(dev, "error reading phy_type property\n");
		return ret;
	}

	hold_phy_reset = of_property_read_bool(dev->of_node, "qcom,hold-reset");

	/* use default major revision as 2 */
	qphy->major_rev = 2;
	ret = of_property_read_u32(dev->of_node, "qcom,major-rev",
						&qphy->major_rev);

	ret = of_property_read_u32_array(dev->of_node, "qcom,vdd-voltage-level",
					 (u32 *) qphy->vdd_levels,
					 ARRAY_SIZE(qphy->vdd_levels));
	if (ret) {
		dev_err(dev, "error reading qcom,vdd-voltage-level property\n");
		return ret;
	}

	qphy->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(qphy->vdd)) {
		dev_err(dev, "unable to get vdd supply\n");
		return PTR_ERR(qphy->vdd);
	}

	qphy->vdda33 = devm_regulator_get(dev, "vdda33");
	if (IS_ERR(qphy->vdda33)) {
		dev_err(dev, "unable to get vdda33 supply\n");
		return PTR_ERR(qphy->vdda33);
	}

	qphy->vdda18 = devm_regulator_get(dev, "vdda18");
	if (IS_ERR(qphy->vdda18)) {
		dev_err(dev, "unable to get vdda18 supply\n");
		return PTR_ERR(qphy->vdda18);
	}

	mutex_init(&qphy->phy_lock);
	platform_set_drvdata(pdev, qphy);

	qphy->phy.label			= "msm-qusb-phy";
	qphy->phy.init			= qusb_phy_init;
	qphy->phy.set_suspend           = qusb_phy_set_suspend;
	qphy->phy.shutdown		= qusb_phy_shutdown;
	qphy->phy.type			= USB_PHY_TYPE_USB2;
	qphy->phy.notify_connect        = qusb_phy_notify_connect;
	qphy->phy.notify_disconnect     = qusb_phy_notify_disconnect;
	qphy->phy.charger_detect	= qusb_phy_drive_dp_pulse;

	/*
	 * On some platforms multiple QUSB PHYs are available. If QUSB PHY is
	 * not used, there is leakage current seen with QUSB PHY related voltage
	 * rail. Hence keep QUSB PHY into reset state explicitly here.
	 */
	if (hold_phy_reset) {
		ret = reset_control_assert(qphy->phy_reset);
		if (ret)
			dev_err(dev, "%s:phy_reset assert failed\n", __func__);
	}

	if (of_property_read_bool(dev->of_node, "extcon")) {
		INIT_DELAYED_WORK(&qphy->port_det_w, qusb_phy_port_state_work);

		ret = qusb_phy_extcon_register(qphy);
		if (ret)
			return ret;

	}

	ret = usb_add_phy_dev(&qphy->phy);
	if (ret)
		return ret;

	ret = qusb_phy_regulator_init(qphy);
	if (ret)
		usb_remove_phy(&qphy->phy);

	/* de-assert clamp dig n to reduce leakage on 1p8 upon boot up */
	if (qphy->tcsr_clamp_dig_n)
		writel_relaxed(0x0, qphy->tcsr_clamp_dig_n);

	/*
	 * Write the usb_hs_ac_value to usb_hs_ac_bitmask of tcsr_conn_box_spare
	 * reg to enable AC/DC coupling
	 */
	if (qphy->tcsr_conn_box_spare) {
		temp = readl_relaxed(qphy->tcsr_conn_box_spare) &
						~qphy->usb_hs_ac_bitmask;
		writel_relaxed(temp | qphy->usb_hs_ac_value,
						qphy->tcsr_conn_box_spare);
	}

	qphy->suspended = true;

	if (of_property_read_bool(dev->of_node, "extcon")) {
		qphy->id_state = true;
		qphy->vbus_active = false;

		if (extcon_get_state(qphy->phy.edev, EXTCON_USB_HOST)) {
			qusb_phy_id_notifier(&qphy->phy.id_nb,
							1, qphy->phy.edev);
		} else if (extcon_get_state(qphy->phy.edev, EXTCON_USB)) {
			qusb_phy_vbus_notifier(&qphy->phy.vbus_nb,
							1, qphy->phy.edev);
		}
	}

	qusb_phy_create_debugfs(qphy);

	return ret;
}

static int qusb_phy_remove(struct platform_device *pdev)
{
	struct qusb_phy *qphy = platform_get_drvdata(pdev);

	debugfs_remove_recursive(qphy->root);
	usb_remove_phy(&qphy->phy);
	qphy->cable_connected = false;
	qusb_phy_set_suspend(&qphy->phy, true);

	return 0;
}

static const struct of_device_id qusb_phy_id_table[] = {
	{ .compatible = "qcom,qusb2phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, qusb_phy_id_table);

static struct platform_driver qusb_phy_driver = {
	.probe		= qusb_phy_probe,
	.remove		= qusb_phy_remove,
	.driver = {
		.name	= "msm-qusb-phy",
		.of_match_table = of_match_ptr(qusb_phy_id_table),
	},
};

module_platform_driver(qusb_phy_driver);

MODULE_DESCRIPTION("MSM QUSB2 PHY driver");
MODULE_LICENSE("GPL");
