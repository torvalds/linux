// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/usb/phy.h>
#include <linux/usb/dwc3-msm.h>
#include <linux/reset.h>
#include <linux/debugfs.h>
#include <linux/qcom_scm.h>
#include <linux/types.h>

#define USB2_PHY_USB_PHY_UTMI_CTRL0		(0x3c)
#define OPMODE_MASK				(0x3 << 3)
#define OPMODE_NONDRIVING			(0x1 << 3)
#define SLEEPM					BIT(0)

#define USB2_PHY_USB_PHY_UTMI_CTRL5		(0x50)
#define POR					BIT(1)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0	(0x54)
#define SIDDQ					BIT(2)
#define RETENABLEN				BIT(3)
#define FSEL_MASK				(0x7 << 4)
#define FSEL_DEFAULT				(0x3 << 4)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1	(0x58)
#define VBUSVLDEXTSEL0				BIT(4)
#define PLLBTUNE				BIT(5)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2	(0x5c)
#define VREGBYPASS				BIT(0)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL1		(0x60)
#define VBUSVLDEXT0				BIT(0)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL2		(0x64)
#define USB2_AUTO_RESUME			BIT(0)
#define USB2_SUSPEND_N				BIT(2)
#define USB2_SUSPEND_N_SEL			BIT(3)

#define USB2_PHY_USB_PHY_CFG0			(0x94)
#define UTMI_PHY_DATAPATH_CTRL_OVERRIDE_EN	BIT(0)
#define UTMI_PHY_CMN_CTRL_OVERRIDE_EN		BIT(1)

#define USB2_PHY_USB_PHY_REFCLK_CTRL		(0xa0)
#define REFCLK_SEL_MASK				(0x3 << 0)
#define REFCLK_SEL_DEFAULT			(0x2 << 0)

#define USB2_PHY_USB_PHY_PWRDOWN_CTRL		(0xa4)
#define PWRDOWN_B				BIT(0)

#define USB2PHY_USB_PHY_RTUNE_SEL		(0xb4)
#define RTUNE_SEL				BIT(0)

#define TXPREEMPAMPTUNE0(x)			(x << 6)
#define TXPREEMPAMPTUNE0_MASK			(BIT(7) | BIT(6))
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X0	0x6c
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X1	0x70
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X2	0x74
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X3	0x78
#define TXVREFTUNE0_MASK			0xF
#define PARAM_OVRD_MASK			0xFF

#define USB_HSPHY_3P3_VOL_MIN			3050000 /* uV */
#define USB_HSPHY_3P3_VOL_MAX			3300000 /* uV */
#define USB_HSPHY_3P3_HPM_LOAD			16000	/* uA */
#define USB_HSPHY_3P3_VOL_FSHOST		3150000 /* uV */

#define USB_HSPHY_1P8_VOL_MIN			1704000 /* uV */
#define USB_HSPHY_1P8_VOL_MAX			1800000 /* uV */
#define USB_HSPHY_1P8_HPM_LOAD			19000	/* uA */

#define USB2PHY_REFGEN_HPM_LOAD			1200000  /* uA */
#define USB_HSPHY_VDD_HPM_LOAD			30000	/* uA */


/* struct hs_phy_priv_data - target specific private data */
struct hs_phy_priv_data {
	bool limit_control_vdd;
	bool limit_control_vdda_18;
	bool limit_control_vdda33;
};

struct msm_hsphy {
	struct usb_phy		phy;
	void __iomem		*base;
	phys_addr_t		eud_reg;
	void __iomem		*eud_enable_reg;
	bool			re_enable_eud;

	struct clk		*ref_clk_src;
	struct clk		*cfg_ahb_clk;
	struct clk		*ref_clk;
	struct reset_control	*phy_reset;

	struct regulator	*vdd;
	struct regulator	*vdda33;
	struct regulator	*vdda18;
	struct regulator        *refgen;
	int			vdd_levels[3]; /* none, low, high */
	int			refgen_levels[3]; /* 0, REFGEN_VOL_MIN, REFGEN_VOL_MAX */

	bool			clocks_enabled;
	bool			power_enabled;
	bool			suspended;
	bool			cable_connected;
	bool			dpdm_enable;

	int			*param_override_seq;
	int			param_override_seq_cnt;

	void __iomem		*phy_rcal_reg;
	u32			rcal_mask;

	struct mutex		phy_lock;
	struct regulator_desc	dpdm_rdesc;
	struct regulator_dev	*dpdm_rdev;

	struct power_supply	*usb_psy;
	unsigned int		vbus_draw;
	struct work_struct	vbus_draw_work;

	/* debugfs entries */
	struct dentry		*root;
	u8			txvref_tune0;
	u8			pre_emphasis;
	u8			param_ovrd0;
	u8			param_ovrd1;
	u8			param_ovrd2;
	u8			param_ovrd3;
	const struct hs_phy_priv_data *phy_priv_data;
};

static void msm_hsphy_enable_clocks(struct msm_hsphy *phy, bool on)
{
	dev_dbg(phy->phy.dev, "%s(): clocks_enabled:%d on:%d\n",
			__func__, phy->clocks_enabled, on);

	if (!phy->clocks_enabled && on) {
		clk_prepare_enable(phy->ref_clk_src);

		if (phy->ref_clk)
			clk_prepare_enable(phy->ref_clk);

		if (phy->cfg_ahb_clk)
			clk_prepare_enable(phy->cfg_ahb_clk);

		phy->clocks_enabled = true;
	}

	if (phy->clocks_enabled && !on) {

		if (phy->ref_clk)
			clk_disable_unprepare(phy->ref_clk);

		if (phy->cfg_ahb_clk)
			clk_disable_unprepare(phy->cfg_ahb_clk);

		clk_disable_unprepare(phy->ref_clk_src);
		phy->clocks_enabled = false;
	}

}

static int vdd_phy_enable_disable(struct msm_hsphy *phy, bool on)
{
	int ret = 0;

	if (!on)
		goto disable_vdd;

	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdd) {
		ret = regulator_set_load(phy->vdd, USB_HSPHY_VDD_HPM_LOAD);
		if (ret < 0) {
			dev_err(phy->phy.dev, "Unable to set HPM of vdd:%d\n", ret);
			goto err_vdd;
		}

		ret = regulator_set_voltage(phy->vdd, phy->vdd_levels[1],
					    phy->vdd_levels[2]);
		if (ret) {
			dev_err(phy->phy.dev, "unable to set voltage for hsusb vdd\n");
			goto put_vdd_lpm;
		}
	}

	ret = regulator_enable(phy->vdd);
	if (ret) {
		dev_err(phy->phy.dev, "Unable to enable VDD\n");
		goto unconfig_vdd;
	}

	dev_dbg(phy->phy.dev, "%s(): HSUSB PHY's vdd turned ON.\n", __func__);

	return ret;

disable_vdd:
	ret = regulator_disable(phy->vdd);
	if (ret)
		dev_err(phy->phy.dev, "Unable to disable vdd:%d\n", ret);

unconfig_vdd:
	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdd) {
		ret = regulator_set_voltage(phy->vdd, phy->vdd_levels[0],
					    phy->vdd_levels[2]);
		if (ret)
			dev_err(phy->phy.dev, "unable to set voltage for hsusb vdd\n");
	}

put_vdd_lpm:
	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdd) {
		ret = regulator_set_load(phy->vdd, 0);
		if (ret < 0)
			dev_err(phy->phy.dev, "Unable to set LPM of vdd\n");
	}

err_vdd:
	return ret;
}

static int vdda18_phy_enable_disable(struct msm_hsphy *phy, bool on)
{
	int ret = 0;

	if (!on)
		goto disable_vdda18;

	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdda_18) {
		ret = regulator_set_load(phy->vdda18, USB_HSPHY_1P8_HPM_LOAD);
		if (ret < 0) {
			dev_err(phy->phy.dev, "Unable to set HPM of vdda18:%d\n", ret);
			goto err_vdda18;
		}

		ret = regulator_set_voltage(phy->vdda18, USB_HSPHY_1P8_VOL_MIN,
							USB_HSPHY_1P8_VOL_MAX);
		if (ret) {
			dev_err(phy->phy.dev,
					"Unable to set voltage for vdda18:%d\n", ret);
			goto put_vdda18_lpm;
		}
	}

	ret = regulator_enable(phy->vdda18);
	if (ret) {
		dev_err(phy->phy.dev, "Unable to enable vdda18:%d\n", ret);
		goto unset_vdda18;
	}

	dev_dbg(phy->phy.dev, "%s(): HSUSB PHY's vdda18 turned ON.\n", __func__);

	return ret;

disable_vdda18:
	ret = regulator_disable(phy->vdda18);
	if (ret)
		dev_err(phy->phy.dev, "Unable to disable vdda18:%d\n", ret);

unset_vdda18:
	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdda_18) {
		ret = regulator_set_voltage(phy->vdda18, 0, USB_HSPHY_1P8_VOL_MAX);
		if (ret)
			dev_err(phy->phy.dev,
				"Unable to set (0) voltage for vdda18:%d\n", ret);
	}

put_vdda18_lpm:
	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdda_18) {
		ret = regulator_set_load(phy->vdda18, 0);
		if (ret < 0)
			dev_err(phy->phy.dev, "Unable to set LPM of vdda18\n");
	}

err_vdda18:
	return ret;
}

static int vdda33_phy_enable_disable(struct msm_hsphy *phy, bool on)
{
	int ret = 0;

	if (!on) {
		if (phy->refgen)
			goto disable_refgen;
		else
			goto disable_vdda33;
	}

	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdda33) {
		ret = regulator_set_load(phy->vdda33, USB_HSPHY_3P3_HPM_LOAD);
		if (ret < 0) {
			dev_err(phy->phy.dev, "Unable to set HPM of vdda33:%d\n", ret);
			goto err_vdda33;
		}

		ret = regulator_set_voltage(phy->vdda33, USB_HSPHY_3P3_VOL_MIN,
							USB_HSPHY_3P3_VOL_MAX);
		if (ret) {
			dev_err(phy->phy.dev,
					"Unable to set voltage for vdda33:%d\n", ret);
			goto put_vdda33_lpm;
		}
	}

	ret = regulator_enable(phy->vdda33);
	if (ret) {
		dev_err(phy->phy.dev, "Unable to enable vdda33:%d\n", ret);
		goto unset_vdd33;
	}

	if (phy->refgen) {
		ret = regulator_set_load(phy->refgen, USB2PHY_REFGEN_HPM_LOAD);
		if (ret < 0) {
			dev_err(phy->phy.dev, "Unable to set HPM of refgen:%d\n", ret);
			goto disable_vdda33;
		}

		ret = regulator_set_voltage(phy->refgen, phy->refgen_levels[1],
						phy->refgen_levels[2]);
		if (ret) {
			dev_err(phy->phy.dev,
					"Unable to set voltage for refgen:%d\n", ret);
			goto put_refgen_lpm;
		}

		ret = regulator_enable(phy->refgen);
		if (ret) {
			dev_err(phy->phy.dev, "Unable to enable refgen:%d\n", ret);
			goto unset_refgen;
		}
	}

	dev_dbg(phy->phy.dev, "%s(): HSUSB PHY's vdda33 turned ON.\n", __func__);

	return ret;

disable_refgen:
	ret = regulator_disable(phy->refgen);
	if (ret)
		dev_err(phy->phy.dev, "Unable to disable refgen:%d\n", ret);

unset_refgen:
	ret = regulator_set_voltage(phy->refgen, phy->refgen_levels[0], phy->refgen_levels[2]);
	if (ret)
		dev_err(phy->phy.dev,
				"Unable to set (0) voltage for refgen:%d\n", ret);

put_refgen_lpm:
	ret = regulator_set_load(phy->refgen, 0);
	if (ret < 0)
		dev_err(phy->phy.dev, "Unable to set (0) HPM of refgen\n");

disable_vdda33:
	ret = regulator_disable(phy->vdda33);
	if (ret)
		dev_err(phy->phy.dev, "Unable to disable vdda33:%d\n", ret);

unset_vdd33:
	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdda33) {
		ret = regulator_set_voltage(phy->vdda33, 0, USB_HSPHY_3P3_VOL_MAX);
		if (ret)
			dev_err(phy->phy.dev,
				"Unable to set (0) voltage for vdda33:%d\n", ret);
	}

put_vdda33_lpm:
	if (phy->phy_priv_data == NULL || !phy->phy_priv_data->limit_control_vdda33) {
		ret = regulator_set_load(phy->vdda33, 0);
		if (ret < 0)
			dev_err(phy->phy.dev, "Unable to set (0) HPM of vdda33\n");
	}

err_vdda33:
	return ret;
}

static int msm_hsphy_enable_power(struct msm_hsphy *phy, bool on)
{
	int ret = 0;

	dev_dbg(phy->phy.dev, "%s turn %s regulators. power_enabled:%d\n",
			__func__, on ? "on" : "off", phy->power_enabled);

	if (phy->power_enabled == on) {
		dev_dbg(phy->phy.dev, "PHYs' regulators are already ON.\n");
		return 0;
	}

	ret = vdd_phy_enable_disable(phy, on);
	if (ret < 0)
		goto err_hs_reg;

	ret = vdda18_phy_enable_disable(phy, on);
	if (ret < 0)
		goto err_hs_reg;

	ret = vdda33_phy_enable_disable(phy, on);
	if (ret < 0)
		goto err_hs_reg;

	if (on)
		phy->power_enabled = true;
	else
		phy->power_enabled = false;

	return ret;

err_hs_reg:
	dev_err(phy->phy.dev, "HSUSB PHY's regulators set/unset failed\n");
	dev_err(phy->phy.dev, "Some or all HSUSB PHY's regulators are turned OFF\n");
	return ret;
}

static void msm_usb_write_readback(void __iomem *base, u32 offset,
					const u32 mask, u32 val)
{
	u32 write_val, tmp = readl_relaxed(base + offset);

	tmp &= ~mask;		/* retain other bits */
	write_val = tmp | val;

	writel_relaxed(write_val, base + offset);

	/* Read back to see if val was written */
	tmp = readl_relaxed(base + offset);
	tmp &= mask;		/* clear other bits */

	if (tmp != val)
		pr_err("%s: write: %x to QSCRATCH: %x FAILED\n",
			__func__, val, offset);
}

static void msm_hsphy_reset(struct msm_hsphy *phy)
{
	int ret;

	ret = reset_control_assert(phy->phy_reset);
	if (ret)
		dev_err(phy->phy.dev, "%s: phy_reset assert failed\n",
								__func__);
	usleep_range(100, 150);

	ret = reset_control_deassert(phy->phy_reset);
	if (ret)
		dev_err(phy->phy.dev, "%s: phy_reset deassert failed\n",
							__func__);
}

static void hsusb_phy_write_seq(void __iomem *base, u32 *seq, int cnt,
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

#define EUD_EN2 BIT(0)
static int msm_hsphy_init(struct usb_phy *uphy)
{
	struct msm_hsphy *phy = container_of(uphy, struct msm_hsphy, phy);
	int ret;
	u32 rcal_code = 0, eud_csr_reg = 0;

	dev_dbg(uphy->dev, "%s phy_flags:0x%x\n", __func__, phy->phy.flags);
	if (phy->eud_enable_reg) {
		eud_csr_reg = readl_relaxed(phy->eud_enable_reg);
		if (eud_csr_reg & EUD_EN2) {
			dev_dbg(phy->phy.dev, "csr:0x%x eud is enabled\n",
							eud_csr_reg);
			/* if in host mode, disable EUD */
			if (phy->phy.flags & PHY_HOST_MODE) {
				qcom_scm_io_writel(phy->eud_reg, 0x0);
				phy->re_enable_eud = true;
			} else {
				msm_hsphy_enable_power(phy, true);
				msm_hsphy_enable_clocks(phy, true);
				return 0;
			}
		}
	}

	ret = msm_hsphy_enable_power(phy, true);
	if (ret)
		return ret;

	msm_hsphy_enable_clocks(phy, true);

	msm_hsphy_reset(phy);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_CFG0,
				UTMI_PHY_CMN_CTRL_OVERRIDE_EN,
				UTMI_PHY_CMN_CTRL_OVERRIDE_EN);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_UTMI_CTRL5,
				POR, POR);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0,
				FSEL_MASK, 0);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1,
				PLLBTUNE, PLLBTUNE);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_REFCLK_CTRL,
				REFCLK_SEL_MASK, REFCLK_SEL_DEFAULT);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1,
				VBUSVLDEXTSEL0, VBUSVLDEXTSEL0);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL1,
				VBUSVLDEXT0, VBUSVLDEXT0);

	/* set parameter ovrride  if needed */
	if (phy->param_override_seq)
		hsusb_phy_write_seq(phy->base, phy->param_override_seq,
				phy->param_override_seq_cnt, 0);

	if (phy->pre_emphasis) {
		u8 val = TXPREEMPAMPTUNE0(phy->pre_emphasis) &
				TXPREEMPAMPTUNE0_MASK;
		if (val)
			msm_usb_write_readback(phy->base,
				USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X1,
				TXPREEMPAMPTUNE0_MASK, val);
	}

	if (phy->txvref_tune0) {
		u8 val = phy->txvref_tune0 & TXVREFTUNE0_MASK;

		msm_usb_write_readback(phy->base,
			USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X1,
			TXVREFTUNE0_MASK, val);
	}

	if (phy->param_ovrd0) {
		msm_usb_write_readback(phy->base,
			USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X0,
			PARAM_OVRD_MASK, phy->param_ovrd0);
	}

	if (phy->param_ovrd1) {
		msm_usb_write_readback(phy->base,
			USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X1,
			PARAM_OVRD_MASK, phy->param_ovrd1);
	}

	if (phy->param_ovrd2) {
		msm_usb_write_readback(phy->base,
			USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X2,
			PARAM_OVRD_MASK, phy->param_ovrd2);
	}

	if (phy->param_ovrd3) {
		msm_usb_write_readback(phy->base,
			USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X3,
			PARAM_OVRD_MASK, phy->param_ovrd3);
	}

	dev_dbg(uphy->dev, "x0:%08x x1:%08x x2:%08x x3:%08x\n",
	readl_relaxed(phy->base + USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X0),
	readl_relaxed(phy->base + USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X1),
	readl_relaxed(phy->base + USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X2),
	readl_relaxed(phy->base + USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X3));

	if (phy->phy_rcal_reg) {
		rcal_code = readl_relaxed(phy->phy_rcal_reg) & phy->rcal_mask;

		dev_dbg(uphy->dev, "rcal_mask:%08x reg:%pK code:%08x\n",
				phy->rcal_mask, phy->phy_rcal_reg, rcal_code);
	}

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2,
				VREGBYPASS, VREGBYPASS);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
				USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
				USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_UTMI_CTRL0,
				SLEEPM, SLEEPM);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0,
				SIDDQ, 0);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_UTMI_CTRL5,
				POR, 0);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
				USB2_SUSPEND_N_SEL, 0);

	msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_CFG0,
				UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 0);

	return 0;
}

static int msm_hsphy_set_suspend(struct usb_phy *uphy, int suspend)
{
	struct msm_hsphy *phy = container_of(uphy, struct msm_hsphy, phy);
	bool eud_active = false;

	if (phy->suspended && suspend) {
		if (phy->phy.flags & PHY_SUS_OVERRIDE)
			goto suspend;

		dev_dbg(uphy->dev, "%s: USB PHY is already suspended\n",
								__func__);
		return 0;
	}

	if (phy->eud_enable_reg && readl_relaxed(phy->eud_enable_reg))
		eud_active = true;

suspend:
	if (suspend) { /* Bus suspend */
	       /*
		* The HUB class drivers calls usb_phy_notify_disconnect() upon a device
		* disconnect. Consider a scenario where a USB device is disconnected without
		* detaching the OTG cable. phy->cable_connected is marked false due to above
		* mentioned call path. Now, while entering low power mode (host bus suspend),
		* we come here and turn off regulators thinking no cable is connected. Prevent
		* this by not turning off regulators while in host mode.
		*/
		if (phy->cable_connected || (phy->phy.flags & PHY_HOST_MODE)) {
			/* Enable auto-resume functionality during host mode
			 * bus suspend with some FS/HS peripheral connected.
			 */
			if ((phy->phy.flags & PHY_HOST_MODE) &&
				(phy->phy.flags & PHY_HSFS_MODE)) {
				/* Enable auto-resume functionality by pulsing
				 * signal
				 */
				msm_usb_write_readback(phy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_AUTO_RESUME, USB2_AUTO_RESUME);
				usleep_range(500, 1000);
				msm_usb_write_readback(phy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_AUTO_RESUME, 0);
			}
			msm_hsphy_enable_clocks(phy, false);
		} else {/* Cable disconnect */
			mutex_lock(&phy->phy_lock);
			dev_dbg(uphy->dev, "phy->flags:0x%x\n", phy->phy.flags);
			if (phy->re_enable_eud) {
				dev_dbg(uphy->dev, "re-enabling EUD\n");
				qcom_scm_io_writel(phy->eud_reg, 0x1);
				phy->re_enable_eud = false;
			}

			if (!phy->dpdm_enable && !eud_active) {
				if (!(phy->phy.flags & EUD_SPOOF_DISCONNECT)) {
					dev_dbg(uphy->dev, "turning off clocks/ldo\n");
					if (!(phy->phy.flags & PHY_HOST_MODE)) {
						msm_usb_write_readback(phy->base,
							USB2_PHY_USB_PHY_PWRDOWN_CTRL,
							PWRDOWN_B, 0);
					}
					msm_hsphy_enable_clocks(phy, false);
					msm_hsphy_enable_power(phy, false);
				}
			} else {
				dev_dbg(uphy->dev, "dpdm reg still active.  Keep clocks/ldo ON\n");
			}
			mutex_unlock(&phy->phy_lock);
		}
		phy->suspended = true;
	} else { /* Bus resume and cable connect */
		msm_hsphy_enable_power(phy, true);
		msm_hsphy_enable_clocks(phy, true);
		phy->suspended = false;
	}

	return 0;
}

static int msm_hsphy_notify_connect(struct usb_phy *uphy,
				    enum usb_device_speed speed)
{
	struct msm_hsphy *phy = container_of(uphy, struct msm_hsphy, phy);

	phy->cable_connected = true;

	return 0;
}

static int msm_hsphy_notify_disconnect(struct usb_phy *uphy,
				       enum usb_device_speed speed)
{
	struct msm_hsphy *phy = container_of(uphy, struct msm_hsphy, phy);

	phy->cable_connected = false;

	return 0;
}

static void msm_hsphy_vbus_draw_work(struct work_struct *w)
{
	struct msm_hsphy *phy = container_of(w, struct msm_hsphy,
			vbus_draw_work);
	union power_supply_propval val = {0};
	int ret;

	if (!phy->usb_psy) {
		phy->usb_psy = power_supply_get_by_name("usb");
		if (!phy->usb_psy) {
			dev_err(phy->phy.dev, "Could not get usb psy\n");
			return;
		}
	}

	dev_info(phy->phy.dev, "Avail curr from USB = %u\n", phy->vbus_draw);

	/* Set max current limit in uA */
	val.intval = 1000 * phy->vbus_draw;
	ret = power_supply_set_property(phy->usb_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret) {
		dev_dbg(phy->phy.dev, "Error (%d) setting input current limit\n", ret);
		return;
	}
}

static int msm_hsphy_set_power(struct usb_phy *uphy, unsigned int mA)
{
	struct msm_hsphy *phy = container_of(uphy, struct msm_hsphy, phy);

	if (phy->cable_connected && (mA == 0))
		return 0;

	phy->vbus_draw = mA;
	schedule_work(&phy->vbus_draw_work);

	return 0;
}

static int msm_hsphy_dpdm_regulator_enable(struct regulator_dev *rdev)
{
	int ret = 0;
	struct msm_hsphy *phy = rdev_get_drvdata(rdev);

	dev_dbg(phy->phy.dev, "%s dpdm_enable:%d\n",
				__func__, phy->dpdm_enable);

	if (phy->eud_enable_reg && readl_relaxed(phy->eud_enable_reg)) {
		dev_err(phy->phy.dev, "eud is enabled\n");
		return 0;
	}

	mutex_lock(&phy->phy_lock);
	if (!phy->dpdm_enable) {
		ret = msm_hsphy_enable_power(phy, true);
		if (ret) {
			mutex_unlock(&phy->phy_lock);
			return ret;
		}

		msm_hsphy_enable_clocks(phy, true);

		msm_hsphy_reset(phy);

		/*
		 * For PMIC charger detection, place PHY in UTMI non-driving
		 * mode which leaves Dp and Dm lines in high-Z state.
		 */
		msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
					USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);
		msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_UTMI_CTRL0,
					OPMODE_MASK, OPMODE_NONDRIVING);
		msm_usb_write_readback(phy->base, USB2_PHY_USB_PHY_CFG0,
					UTMI_PHY_DATAPATH_CTRL_OVERRIDE_EN,
					UTMI_PHY_DATAPATH_CTRL_OVERRIDE_EN);

		phy->dpdm_enable = true;
	}
	mutex_unlock(&phy->phy_lock);

	return ret;
}

static int msm_hsphy_dpdm_regulator_disable(struct regulator_dev *rdev)
{
	int ret = 0;
	struct msm_hsphy *phy = rdev_get_drvdata(rdev);

	dev_dbg(phy->phy.dev, "%s dpdm_enable:%d\n",
				__func__, phy->dpdm_enable);

	mutex_lock(&phy->phy_lock);
	if (phy->dpdm_enable) {
		if (!phy->cable_connected) {
			msm_hsphy_enable_clocks(phy, false);
			ret = msm_hsphy_enable_power(phy, false);
			if (ret < 0) {
				mutex_unlock(&phy->phy_lock);
				return ret;
			}
		}
		phy->dpdm_enable = false;
	}
	mutex_unlock(&phy->phy_lock);

	return ret;
}

static int msm_hsphy_dpdm_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct msm_hsphy *phy = rdev_get_drvdata(rdev);

	dev_dbg(phy->phy.dev, "%s dpdm_enable:%d\n",
			__func__, phy->dpdm_enable);

	return phy->dpdm_enable;
}

static const struct regulator_ops msm_hsphy_dpdm_regulator_ops = {
	.enable		= msm_hsphy_dpdm_regulator_enable,
	.disable	= msm_hsphy_dpdm_regulator_disable,
	.is_enabled	= msm_hsphy_dpdm_regulator_is_enabled,
};

static int msm_hsphy_regulator_init(struct msm_hsphy *phy)
{
	struct device *dev = phy->phy.dev;
	struct regulator_config cfg = {};
	struct regulator_init_data *init_data;

	init_data = devm_kzalloc(dev, sizeof(*init_data), GFP_KERNEL);
	if (!init_data)
		return -ENOMEM;

	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_STATUS;
	phy->dpdm_rdesc.owner = THIS_MODULE;
	phy->dpdm_rdesc.type = REGULATOR_VOLTAGE;
	phy->dpdm_rdesc.ops = &msm_hsphy_dpdm_regulator_ops;
	phy->dpdm_rdesc.name = kbasename(dev->of_node->full_name);

	cfg.dev = dev;
	cfg.init_data = init_data;
	cfg.driver_data = phy;
	cfg.of_node = dev->of_node;

	phy->dpdm_rdev = devm_regulator_register(dev, &phy->dpdm_rdesc, &cfg);
	return PTR_ERR_OR_ZERO(phy->dpdm_rdev);
}

static void msm_hsphy_create_debugfs(struct msm_hsphy *phy)
{
	phy->root = debugfs_create_dir(dev_name(phy->phy.dev), NULL);
	debugfs_create_x8("pre_emphasis", 0644, phy->root, &phy->pre_emphasis);
	debugfs_create_x8("txvref_tune0", 0644, phy->root, &phy->txvref_tune0);
	debugfs_create_x8("param_ovrd0", 0644, phy->root, &phy->param_ovrd0);
	debugfs_create_x8("param_ovrd1", 0644, phy->root, &phy->param_ovrd1);
	debugfs_create_x8("param_ovrd2", 0644, phy->root, &phy->param_ovrd2);
	debugfs_create_x8("param_ovrd3", 0644, phy->root, &phy->param_ovrd3);
}

static int usb2_get_regulators(struct msm_hsphy *phy)
{
	struct device *dev = phy->phy.dev;
	int ret = 0;

	phy->refgen = NULL;

	phy->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(phy->vdd)) {
		ret = PTR_ERR(phy->vdd);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "unable to get vdd supply\n");
		return ret;
	}

	phy->vdda33 = devm_regulator_get(dev, "vdda33");
	if (IS_ERR(phy->vdda33)) {
		ret = PTR_ERR(phy->vdda33);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "unable to get vdda33 supply\n");
		return ret;
	}

	phy->vdda18 = devm_regulator_get(dev, "vdda18");
	if (IS_ERR(phy->vdda18)) {
		ret = PTR_ERR(phy->vdda18);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "unable to get vdda18 supply\n");
		return ret;
	}

	if (of_property_read_bool(dev->of_node, "refgen-supply")) {
		phy->refgen = devm_regulator_get_optional(dev, "refgen");
		if (IS_ERR(phy->refgen))
			dev_err(dev, "unable to get refgen supply\n");
	}

	return 0;
}

static int msm_hsphy_probe(struct platform_device *pdev)
{
	struct msm_hsphy *phy;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct hs_phy_priv_data *driver_data;
	int ret = 0;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		ret = -ENOMEM;
		goto err_ret;
	}

	driver_data = of_device_get_match_data(dev);
	phy->phy_priv_data = driver_data;
	phy->phy.dev = dev;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"hsusb_phy_base");
	if (!res) {
		dev_err(dev, "missing memory base resource\n");
		ret = -ENODEV;
		goto err_ret;
	}

	phy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy->base)) {
		dev_err(dev, "ioremap failed\n");
		ret = -ENODEV;
		goto err_ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"phy_rcal_reg");
	if (res) {
		phy->phy_rcal_reg = devm_ioremap(dev,
					res->start, resource_size(res));
		if (IS_ERR(phy->phy_rcal_reg)) {
			dev_err(dev, "couldn't ioremap phy_rcal_reg\n");
			phy->phy_rcal_reg = NULL;
		}
		if (of_property_read_u32(dev->of_node,
					"qcom,rcal-mask", &phy->rcal_mask)) {
			dev_err(dev, "unable to read phy rcal mask\n");
			phy->phy_rcal_reg = NULL;
		}
		dev_dbg(dev, "rcal_mask:%08x reg:%pK\n", phy->rcal_mask,
				phy->phy_rcal_reg);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
			"eud_enable_reg");
	if (res) {
		phy->eud_enable_reg = devm_ioremap_resource(dev, res);
		if (IS_ERR(phy->eud_enable_reg)) {
			dev_err(dev, "err getting eud_enable_reg address\n");
			return PTR_ERR(phy->eud_enable_reg);
		}
		phy->eud_reg = res->start;
	}

	/* ref_clk_src is needed irrespective of SE_CLK or DIFF_CLK usage */
	phy->ref_clk_src = devm_clk_get(dev, "ref_clk_src");
	if (IS_ERR(phy->ref_clk_src)) {
		dev_dbg(dev, "clk get failed for ref_clk_src\n");
		ret = PTR_ERR(phy->ref_clk_src);
		return ret;
	}

	phy->ref_clk = devm_clk_get_optional(dev, "ref_clk");
	if (IS_ERR(phy->ref_clk)) {
		dev_dbg(dev, "clk get failed for ref_clk\n");
		ret = PTR_ERR(phy->ref_clk);
		return ret;
	}

	if (of_property_match_string(pdev->dev.of_node,
				"clock-names", "cfg_ahb_clk") >= 0) {
		phy->cfg_ahb_clk = devm_clk_get(dev, "cfg_ahb_clk");
		if (IS_ERR(phy->cfg_ahb_clk)) {
			ret = PTR_ERR(phy->cfg_ahb_clk);
			if (ret != -EPROBE_DEFER)
				dev_err(dev,
				"clk get failed for cfg_ahb_clk ret %d\n", ret);
			return ret;
		}
	}

	phy->phy_reset = devm_reset_control_get(dev, "phy_reset");
	if (IS_ERR(phy->phy_reset))
		return PTR_ERR(phy->phy_reset);

	phy->param_override_seq_cnt = of_property_count_elems_of_size(
					dev->of_node,
					"qcom,param-override-seq",
					sizeof(*phy->param_override_seq));
	if (phy->param_override_seq_cnt > 0) {
		phy->param_override_seq = devm_kcalloc(dev,
					phy->param_override_seq_cnt,
					sizeof(*phy->param_override_seq),
					GFP_KERNEL);
		if (!phy->param_override_seq)
			return -ENOMEM;

		if (phy->param_override_seq_cnt % 2) {
			dev_err(dev, "invalid param_override_seq_len\n");
			return -EINVAL;
		}

		ret = of_property_read_u32_array(dev->of_node,
				"qcom,param-override-seq",
				phy->param_override_seq,
				phy->param_override_seq_cnt);
		if (ret) {
			dev_err(dev, "qcom,param-override-seq read failed %d\n",
				ret);
			return ret;
		}
	}

	ret = of_property_read_u32_array(dev->of_node, "qcom,vdd-voltage-level",
					 (u32 *) phy->vdd_levels,
					 ARRAY_SIZE(phy->vdd_levels));
	if (ret) {
		dev_err(dev, "error reading qcom,vdd-voltage-level property\n");
		goto err_ret;
	}

	ret = of_property_read_u32_array(dev->of_node, "qcom,refgen-voltage-level",
					(u32 *) phy->refgen_levels,
					ARRAY_SIZE(phy->refgen_levels));
	if (ret)
		dev_err(dev, "error reading qcom,refgen-voltage-level property\n");

	ret = usb2_get_regulators(phy);
	if (ret)
		return ret;

	mutex_init(&phy->phy_lock);
	platform_set_drvdata(pdev, phy);

	phy->phy.init			= msm_hsphy_init;
	phy->phy.set_suspend		= msm_hsphy_set_suspend;
	phy->phy.notify_connect		= msm_hsphy_notify_connect;
	phy->phy.notify_disconnect	= msm_hsphy_notify_disconnect;
	phy->phy.set_power		= msm_hsphy_set_power;
	phy->phy.type			= USB_PHY_TYPE_USB2;

	ret = msm_hsphy_regulator_init(phy);
	if (ret)
		goto err_ret;

	INIT_WORK(&phy->vbus_draw_work, msm_hsphy_vbus_draw_work);
	msm_hsphy_create_debugfs(phy);

	/*
	 * EUD may be enable in boot loader and to keep EUD session alive across
	 * kernel boot till USB phy driver is initialized based on cable status,
	 * keep LDOs on here.
	 */
	if (phy->eud_enable_reg && readl_relaxed(phy->eud_enable_reg)) {
		msm_hsphy_enable_power(phy, true);
		msm_hsphy_enable_clocks(phy, true);
	}

	/* Placed at the end to ensure the probe is complete */
	ret = usb_add_phy_dev(&phy->phy);

err_ret:
	return ret;
}

static int msm_hsphy_remove(struct platform_device *pdev)
{
	struct msm_hsphy *phy = platform_get_drvdata(pdev);

	if (!phy)
		return 0;

	if (phy->usb_psy)
		power_supply_put(phy->usb_psy);

	debugfs_remove_recursive(phy->root);

	usb_remove_phy(&phy->phy);
	clk_disable_unprepare(phy->ref_clk_src);

	msm_hsphy_enable_clocks(phy, false);
	msm_hsphy_enable_power(phy, false);
	return 0;
}

static const struct hs_phy_priv_data priv_data_lemans = {
	.limit_control_vdda_18 = true,
};

static const struct of_device_id msm_usb_id_table[] = {
	{
		.compatible = "qcom,usb-hsphy-snps-femto",
	},
	{
		.compatible = "qcom,usb-hsphy-snps-femto-lemans",
		.data = &priv_data_lemans,
	},
	{ },
};

MODULE_DEVICE_TABLE(of, msm_usb_id_table);

static struct platform_driver msm_hsphy_driver = {
	.probe		= msm_hsphy_probe,
	.remove		= msm_hsphy_remove,
	.driver = {
		.name	= "msm-usb-hsphy",
		.of_match_table = of_match_ptr(msm_usb_id_table),
	},
};

module_platform_driver(msm_hsphy_driver);

MODULE_DESCRIPTION("MSM USB HS PHY driver");
MODULE_LICENSE("GPL v2");
