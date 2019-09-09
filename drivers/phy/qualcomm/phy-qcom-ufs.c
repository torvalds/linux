// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2015, Linux Foundation. All rights reserved.
 */

#include "phy-qcom-ufs-i.h"

#define MAX_PROP_NAME              32
#define VDDA_PHY_MIN_UV            1000000
#define VDDA_PHY_MAX_UV            1000000
#define VDDA_PLL_MIN_UV            1800000
#define VDDA_PLL_MAX_UV            1800000
#define VDDP_REF_CLK_MIN_UV        1200000
#define VDDP_REF_CLK_MAX_UV        1200000

int ufs_qcom_phy_calibrate(struct ufs_qcom_phy *ufs_qcom_phy,
			   struct ufs_qcom_phy_calibration *tbl_A,
			   int tbl_size_A,
			   struct ufs_qcom_phy_calibration *tbl_B,
			   int tbl_size_B, bool is_rate_B)
{
	int i;
	int ret = 0;

	if (!tbl_A) {
		dev_err(ufs_qcom_phy->dev, "%s: tbl_A is NULL", __func__);
		ret = EINVAL;
		goto out;
	}

	for (i = 0; i < tbl_size_A; i++)
		writel_relaxed(tbl_A[i].cfg_value,
			       ufs_qcom_phy->mmio + tbl_A[i].reg_offset);

	/*
	 * In case we would like to work in rate B, we need
	 * to override a registers that were configured in rate A table
	 * with registers of rate B table.
	 * table.
	 */
	if (is_rate_B) {
		if (!tbl_B) {
			dev_err(ufs_qcom_phy->dev, "%s: tbl_B is NULL",
				__func__);
			ret = EINVAL;
			goto out;
		}

		for (i = 0; i < tbl_size_B; i++)
			writel_relaxed(tbl_B[i].cfg_value,
				ufs_qcom_phy->mmio + tbl_B[i].reg_offset);
	}

	/* flush buffered writes */
	mb();

out:
	return ret;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_calibrate);

/*
 * This assumes the embedded phy structure inside generic_phy is of type
 * struct ufs_qcom_phy. In order to function properly it's crucial
 * to keep the embedded struct "struct ufs_qcom_phy common_cfg"
 * as the first inside generic_phy.
 */
struct ufs_qcom_phy *get_ufs_qcom_phy(struct phy *generic_phy)
{
	return (struct ufs_qcom_phy *)phy_get_drvdata(generic_phy);
}
EXPORT_SYMBOL_GPL(get_ufs_qcom_phy);

static
int ufs_qcom_phy_base_init(struct platform_device *pdev,
			   struct ufs_qcom_phy *phy_common)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int err = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_mem");
	phy_common->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const *)phy_common->mmio)) {
		err = PTR_ERR((void const *)phy_common->mmio);
		phy_common->mmio = NULL;
		dev_err(dev, "%s: ioremap for phy_mem resource failed %d\n",
			__func__, err);
		return err;
	}

	/* "dev_ref_clk_ctrl_mem" is optional resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dev_ref_clk_ctrl_mem");
	phy_common->dev_ref_clk_ctrl_mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const *)phy_common->dev_ref_clk_ctrl_mmio))
		phy_common->dev_ref_clk_ctrl_mmio = NULL;

	return 0;
}

struct phy *ufs_qcom_phy_generic_probe(struct platform_device *pdev,
				struct ufs_qcom_phy *common_cfg,
				const struct phy_ops *ufs_qcom_phy_gen_ops,
				struct ufs_qcom_phy_specific_ops *phy_spec_ops)
{
	int err;
	struct device *dev = &pdev->dev;
	struct phy *generic_phy = NULL;
	struct phy_provider *phy_provider;

	err = ufs_qcom_phy_base_init(pdev, common_cfg);
	if (err) {
		dev_err(dev, "%s: phy base init failed %d\n", __func__, err);
		goto out;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		err = PTR_ERR(phy_provider);
		dev_err(dev, "%s: failed to register phy %d\n", __func__, err);
		goto out;
	}

	generic_phy = devm_phy_create(dev, NULL, ufs_qcom_phy_gen_ops);
	if (IS_ERR(generic_phy)) {
		err =  PTR_ERR(generic_phy);
		dev_err(dev, "%s: failed to create phy %d\n", __func__, err);
		generic_phy = NULL;
		goto out;
	}

	common_cfg->phy_spec_ops = phy_spec_ops;
	common_cfg->dev = dev;

out:
	return generic_phy;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_generic_probe);

static int ufs_qcom_phy_get_reset(struct ufs_qcom_phy *phy_common)
{
	struct reset_control *reset;

	if (phy_common->ufs_reset)
		return 0;

	reset = devm_reset_control_get_exclusive_by_index(phy_common->dev, 0);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	phy_common->ufs_reset = reset;
	return 0;
}

static int __ufs_qcom_phy_clk_get(struct device *dev,
			 const char *name, struct clk **clk_out, bool err_print)
{
	struct clk *clk;
	int err = 0;

	clk = devm_clk_get(dev, name);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		if (err_print)
			dev_err(dev, "failed to get %s err %d", name, err);
	} else {
		*clk_out = clk;
	}

	return err;
}

static int ufs_qcom_phy_clk_get(struct device *dev,
			 const char *name, struct clk **clk_out)
{
	return __ufs_qcom_phy_clk_get(dev, name, clk_out, true);
}

int ufs_qcom_phy_init_clks(struct ufs_qcom_phy *phy_common)
{
	int err;

	if (of_device_is_compatible(phy_common->dev->of_node,
				"qcom,msm8996-ufs-phy-qmp-14nm"))
		goto skip_txrx_clk;

	err = ufs_qcom_phy_clk_get(phy_common->dev, "tx_iface_clk",
				   &phy_common->tx_iface_clk);
	if (err)
		goto out;

	err = ufs_qcom_phy_clk_get(phy_common->dev, "rx_iface_clk",
				   &phy_common->rx_iface_clk);
	if (err)
		goto out;

skip_txrx_clk:
	err = ufs_qcom_phy_clk_get(phy_common->dev, "ref_clk_src",
				   &phy_common->ref_clk_src);
	if (err)
		goto out;

	/*
	 * "ref_clk_parent" is optional hence don't abort init if it's not
	 * found.
	 */
	__ufs_qcom_phy_clk_get(phy_common->dev, "ref_clk_parent",
				   &phy_common->ref_clk_parent, false);

	err = ufs_qcom_phy_clk_get(phy_common->dev, "ref_clk",
				   &phy_common->ref_clk);

out:
	return err;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_init_clks);

static int ufs_qcom_phy_init_vreg(struct device *dev,
				  struct ufs_qcom_phy_vreg *vreg,
				  const char *name)
{
	int err = 0;

	char prop_name[MAX_PROP_NAME];

	vreg->name = name;
	vreg->reg = devm_regulator_get(dev, name);
	if (IS_ERR(vreg->reg)) {
		err = PTR_ERR(vreg->reg);
		dev_err(dev, "failed to get %s, %d\n", name, err);
		goto out;
	}

	if (dev->of_node) {
		snprintf(prop_name, MAX_PROP_NAME, "%s-max-microamp", name);
		err = of_property_read_u32(dev->of_node,
					prop_name, &vreg->max_uA);
		if (err && err != -EINVAL) {
			dev_err(dev, "%s: failed to read %s\n",
					__func__, prop_name);
			goto out;
		} else if (err == -EINVAL || !vreg->max_uA) {
			if (regulator_count_voltages(vreg->reg) > 0) {
				dev_err(dev, "%s: %s is mandatory\n",
						__func__, prop_name);
				goto out;
			}
			err = 0;
		}
	}

	if (!strcmp(name, "vdda-pll")) {
		vreg->max_uV = VDDA_PLL_MAX_UV;
		vreg->min_uV = VDDA_PLL_MIN_UV;
	} else if (!strcmp(name, "vdda-phy")) {
		vreg->max_uV = VDDA_PHY_MAX_UV;
		vreg->min_uV = VDDA_PHY_MIN_UV;
	} else if (!strcmp(name, "vddp-ref-clk")) {
		vreg->max_uV = VDDP_REF_CLK_MAX_UV;
		vreg->min_uV = VDDP_REF_CLK_MIN_UV;
	}

out:
	return err;
}

int ufs_qcom_phy_init_vregulators(struct ufs_qcom_phy *phy_common)
{
	int err;

	err = ufs_qcom_phy_init_vreg(phy_common->dev, &phy_common->vdda_pll,
		"vdda-pll");
	if (err)
		goto out;

	err = ufs_qcom_phy_init_vreg(phy_common->dev, &phy_common->vdda_phy,
		"vdda-phy");

	if (err)
		goto out;

	err = ufs_qcom_phy_init_vreg(phy_common->dev, &phy_common->vddp_ref_clk,
				     "vddp-ref-clk");

out:
	return err;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_init_vregulators);

static int ufs_qcom_phy_cfg_vreg(struct device *dev,
			  struct ufs_qcom_phy_vreg *vreg, bool on)
{
	int ret = 0;
	struct regulator *reg = vreg->reg;
	const char *name = vreg->name;
	int min_uV;
	int uA_load;

	if (regulator_count_voltages(reg) > 0) {
		min_uV = on ? vreg->min_uV : 0;
		ret = regulator_set_voltage(reg, min_uV, vreg->max_uV);
		if (ret) {
			dev_err(dev, "%s: %s set voltage failed, err=%d\n",
					__func__, name, ret);
			goto out;
		}
		uA_load = on ? vreg->max_uA : 0;
		ret = regulator_set_load(reg, uA_load);
		if (ret >= 0) {
			/*
			 * regulator_set_load() returns new regulator
			 * mode upon success.
			 */
			ret = 0;
		} else {
			dev_err(dev, "%s: %s set optimum mode(uA_load=%d) failed, err=%d\n",
					__func__, name, uA_load, ret);
			goto out;
		}
	}
out:
	return ret;
}

static int ufs_qcom_phy_enable_vreg(struct device *dev,
			     struct ufs_qcom_phy_vreg *vreg)
{
	int ret = 0;

	if (!vreg || vreg->enabled)
		goto out;

	ret = ufs_qcom_phy_cfg_vreg(dev, vreg, true);
	if (ret) {
		dev_err(dev, "%s: ufs_qcom_phy_cfg_vreg() failed, err=%d\n",
			__func__, ret);
		goto out;
	}

	ret = regulator_enable(vreg->reg);
	if (ret) {
		dev_err(dev, "%s: enable failed, err=%d\n",
				__func__, ret);
		goto out;
	}

	vreg->enabled = true;
out:
	return ret;
}

static int ufs_qcom_phy_enable_ref_clk(struct ufs_qcom_phy *phy)
{
	int ret = 0;

	if (phy->is_ref_clk_enabled)
		goto out;

	/*
	 * reference clock is propagated in a daisy-chained manner from
	 * source to phy, so ungate them at each stage.
	 */
	ret = clk_prepare_enable(phy->ref_clk_src);
	if (ret) {
		dev_err(phy->dev, "%s: ref_clk_src enable failed %d\n",
				__func__, ret);
		goto out;
	}

	/*
	 * "ref_clk_parent" is optional clock hence make sure that clk reference
	 * is available before trying to enable the clock.
	 */
	if (phy->ref_clk_parent) {
		ret = clk_prepare_enable(phy->ref_clk_parent);
		if (ret) {
			dev_err(phy->dev, "%s: ref_clk_parent enable failed %d\n",
					__func__, ret);
			goto out_disable_src;
		}
	}

	ret = clk_prepare_enable(phy->ref_clk);
	if (ret) {
		dev_err(phy->dev, "%s: ref_clk enable failed %d\n",
				__func__, ret);
		goto out_disable_parent;
	}

	phy->is_ref_clk_enabled = true;
	goto out;

out_disable_parent:
	if (phy->ref_clk_parent)
		clk_disable_unprepare(phy->ref_clk_parent);
out_disable_src:
	clk_disable_unprepare(phy->ref_clk_src);
out:
	return ret;
}

static int ufs_qcom_phy_disable_vreg(struct device *dev,
			      struct ufs_qcom_phy_vreg *vreg)
{
	int ret = 0;

	if (!vreg || !vreg->enabled)
		goto out;

	ret = regulator_disable(vreg->reg);

	if (!ret) {
		/* ignore errors on applying disable config */
		ufs_qcom_phy_cfg_vreg(dev, vreg, false);
		vreg->enabled = false;
	} else {
		dev_err(dev, "%s: %s disable failed, err=%d\n",
				__func__, vreg->name, ret);
	}
out:
	return ret;
}

static void ufs_qcom_phy_disable_ref_clk(struct ufs_qcom_phy *phy)
{
	if (phy->is_ref_clk_enabled) {
		clk_disable_unprepare(phy->ref_clk);
		/*
		 * "ref_clk_parent" is optional clock hence make sure that clk
		 * reference is available before trying to disable the clock.
		 */
		if (phy->ref_clk_parent)
			clk_disable_unprepare(phy->ref_clk_parent);
		clk_disable_unprepare(phy->ref_clk_src);
		phy->is_ref_clk_enabled = false;
	}
}

/* Turn ON M-PHY RMMI interface clocks */
static int ufs_qcom_phy_enable_iface_clk(struct ufs_qcom_phy *phy)
{
	int ret = 0;

	if (phy->is_iface_clk_enabled)
		goto out;

	ret = clk_prepare_enable(phy->tx_iface_clk);
	if (ret) {
		dev_err(phy->dev, "%s: tx_iface_clk enable failed %d\n",
				__func__, ret);
		goto out;
	}
	ret = clk_prepare_enable(phy->rx_iface_clk);
	if (ret) {
		clk_disable_unprepare(phy->tx_iface_clk);
		dev_err(phy->dev, "%s: rx_iface_clk enable failed %d. disabling also tx_iface_clk\n",
				__func__, ret);
		goto out;
	}
	phy->is_iface_clk_enabled = true;

out:
	return ret;
}

/* Turn OFF M-PHY RMMI interface clocks */
static void ufs_qcom_phy_disable_iface_clk(struct ufs_qcom_phy *phy)
{
	if (phy->is_iface_clk_enabled) {
		clk_disable_unprepare(phy->tx_iface_clk);
		clk_disable_unprepare(phy->rx_iface_clk);
		phy->is_iface_clk_enabled = false;
	}
}

static int ufs_qcom_phy_start_serdes(struct ufs_qcom_phy *ufs_qcom_phy)
{
	int ret = 0;

	if (!ufs_qcom_phy->phy_spec_ops->start_serdes) {
		dev_err(ufs_qcom_phy->dev, "%s: start_serdes() callback is not supported\n",
			__func__);
		ret = -ENOTSUPP;
	} else {
		ufs_qcom_phy->phy_spec_ops->start_serdes(ufs_qcom_phy);
	}

	return ret;
}

int ufs_qcom_phy_set_tx_lane_enable(struct phy *generic_phy, u32 tx_lanes)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);
	int ret = 0;

	if (!ufs_qcom_phy->phy_spec_ops->set_tx_lane_enable) {
		dev_err(ufs_qcom_phy->dev, "%s: set_tx_lane_enable() callback is not supported\n",
			__func__);
		ret = -ENOTSUPP;
	} else {
		ufs_qcom_phy->phy_spec_ops->set_tx_lane_enable(ufs_qcom_phy,
							       tx_lanes);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_set_tx_lane_enable);

void ufs_qcom_phy_save_controller_version(struct phy *generic_phy,
					  u8 major, u16 minor, u16 step)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	ufs_qcom_phy->host_ctrl_rev_major = major;
	ufs_qcom_phy->host_ctrl_rev_minor = minor;
	ufs_qcom_phy->host_ctrl_rev_step = step;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_save_controller_version);

static int ufs_qcom_phy_is_pcs_ready(struct ufs_qcom_phy *ufs_qcom_phy)
{
	if (!ufs_qcom_phy->phy_spec_ops->is_physical_coding_sublayer_ready) {
		dev_err(ufs_qcom_phy->dev, "%s: is_physical_coding_sublayer_ready() callback is not supported\n",
			__func__);
		return -ENOTSUPP;
	}

	return ufs_qcom_phy->phy_spec_ops->
			is_physical_coding_sublayer_ready(ufs_qcom_phy);
}

int ufs_qcom_phy_power_on(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);
	struct device *dev = phy_common->dev;
	bool is_rate_B = false;
	int err;

	err = ufs_qcom_phy_get_reset(phy_common);
	if (err)
		return err;

	err = reset_control_assert(phy_common->ufs_reset);
	if (err)
		return err;

	if (phy_common->mode == PHY_MODE_UFS_HS_B)
		is_rate_B = true;

	err = phy_common->phy_spec_ops->calibrate(phy_common, is_rate_B);
	if (err)
		return err;

	err = reset_control_deassert(phy_common->ufs_reset);
	if (err) {
		dev_err(dev, "Failed to assert UFS PHY reset");
		return err;
	}

	err = ufs_qcom_phy_start_serdes(phy_common);
	if (err)
		return err;

	err = ufs_qcom_phy_is_pcs_ready(phy_common);
	if (err)
		return err;

	err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdda_phy);
	if (err) {
		dev_err(dev, "%s enable vdda_phy failed, err=%d\n",
			__func__, err);
		goto out;
	}

	phy_common->phy_spec_ops->power_control(phy_common, true);

	/* vdda_pll also enables ref clock LDOs so enable it first */
	err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdda_pll);
	if (err) {
		dev_err(dev, "%s enable vdda_pll failed, err=%d\n",
			__func__, err);
		goto out_disable_phy;
	}

	err = ufs_qcom_phy_enable_iface_clk(phy_common);
	if (err) {
		dev_err(dev, "%s enable phy iface clock failed, err=%d\n",
			__func__, err);
		goto out_disable_pll;
	}

	err = ufs_qcom_phy_enable_ref_clk(phy_common);
	if (err) {
		dev_err(dev, "%s enable phy ref clock failed, err=%d\n",
			__func__, err);
		goto out_disable_iface_clk;
	}

	/* enable device PHY ref_clk pad rail */
	if (phy_common->vddp_ref_clk.reg) {
		err = ufs_qcom_phy_enable_vreg(dev,
					       &phy_common->vddp_ref_clk);
		if (err) {
			dev_err(dev, "%s enable vddp_ref_clk failed, err=%d\n",
				__func__, err);
			goto out_disable_ref_clk;
		}
	}

	goto out;

out_disable_ref_clk:
	ufs_qcom_phy_disable_ref_clk(phy_common);
out_disable_iface_clk:
	ufs_qcom_phy_disable_iface_clk(phy_common);
out_disable_pll:
	ufs_qcom_phy_disable_vreg(dev, &phy_common->vdda_pll);
out_disable_phy:
	ufs_qcom_phy_disable_vreg(dev, &phy_common->vdda_phy);
out:
	return err;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_power_on);

int ufs_qcom_phy_power_off(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);

	phy_common->phy_spec_ops->power_control(phy_common, false);

	if (phy_common->vddp_ref_clk.reg)
		ufs_qcom_phy_disable_vreg(phy_common->dev,
					  &phy_common->vddp_ref_clk);
	ufs_qcom_phy_disable_ref_clk(phy_common);
	ufs_qcom_phy_disable_iface_clk(phy_common);

	ufs_qcom_phy_disable_vreg(phy_common->dev, &phy_common->vdda_pll);
	ufs_qcom_phy_disable_vreg(phy_common->dev, &phy_common->vdda_phy);
	reset_control_assert(phy_common->ufs_reset);
	return 0;
}
EXPORT_SYMBOL_GPL(ufs_qcom_phy_power_off);

MODULE_AUTHOR("Yaniv Gardi <ygardi@codeaurora.org>");
MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Universal Flash Storage (UFS) QCOM PHY");
MODULE_LICENSE("GPL v2");
