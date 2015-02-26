/*
 * Copyright (c) 2013-2015, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "phy-qcom-ufs-i.h"

#define MAX_PROP_NAME              32
#define VDDA_PHY_MIN_UV            1000000
#define VDDA_PHY_MAX_UV            1000000
#define VDDA_PLL_MIN_UV            1800000
#define VDDA_PLL_MAX_UV            1800000
#define VDDP_REF_CLK_MIN_UV        1200000
#define VDDP_REF_CLK_MAX_UV        1200000

static int __ufs_qcom_phy_init_vreg(struct phy *, struct ufs_qcom_phy_vreg *,
				    const char *, bool);
static int ufs_qcom_phy_init_vreg(struct phy *, struct ufs_qcom_phy_vreg *,
				  const char *);
static int ufs_qcom_phy_base_init(struct platform_device *pdev,
				  struct ufs_qcom_phy *phy_common);

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

struct phy *ufs_qcom_phy_generic_probe(struct platform_device *pdev,
				struct ufs_qcom_phy *common_cfg,
				struct phy_ops *ufs_qcom_phy_gen_ops,
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
		goto out;
	}

	common_cfg->phy_spec_ops = phy_spec_ops;
	common_cfg->dev = dev;

out:
	return generic_phy;
}

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

static
int ufs_qcom_phy_base_init(struct platform_device *pdev,
			   struct ufs_qcom_phy *phy_common)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int err = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_mem");
	if (!res) {
		dev_err(dev, "%s: phy_mem resource not found\n", __func__);
		err = -ENOMEM;
		goto out;
	}

	phy_common->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const *)phy_common->mmio)) {
		err = PTR_ERR((void const *)phy_common->mmio);
		phy_common->mmio = NULL;
		dev_err(dev, "%s: ioremap for phy_mem resource failed %d\n",
			__func__, err);
		goto out;
	}

	/* "dev_ref_clk_ctrl_mem" is optional resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dev_ref_clk_ctrl_mem");
	if (!res) {
		dev_dbg(dev, "%s: dev_ref_clk_ctrl_mem resource not found\n",
			__func__);
		goto out;
	}

	phy_common->dev_ref_clk_ctrl_mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const *)phy_common->dev_ref_clk_ctrl_mmio)) {
		err = PTR_ERR((void const *)phy_common->dev_ref_clk_ctrl_mmio);
		phy_common->dev_ref_clk_ctrl_mmio = NULL;
		dev_err(dev, "%s: ioremap for dev_ref_clk_ctrl_mem resource failed %d\n",
			__func__, err);
	}

out:
	return err;
}

static int __ufs_qcom_phy_clk_get(struct phy *phy,
			 const char *name, struct clk **clk_out, bool err_print)
{
	struct clk *clk;
	int err = 0;
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(phy);
	struct device *dev = ufs_qcom_phy->dev;

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

static
int ufs_qcom_phy_clk_get(struct phy *phy,
			 const char *name, struct clk **clk_out)
{
	return __ufs_qcom_phy_clk_get(phy, name, clk_out, true);
}

int
ufs_qcom_phy_init_clks(struct phy *generic_phy,
		       struct ufs_qcom_phy *phy_common)
{
	int err;

	err = ufs_qcom_phy_clk_get(generic_phy, "tx_iface_clk",
				   &phy_common->tx_iface_clk);
	if (err)
		goto out;

	err = ufs_qcom_phy_clk_get(generic_phy, "rx_iface_clk",
				   &phy_common->rx_iface_clk);
	if (err)
		goto out;

	err = ufs_qcom_phy_clk_get(generic_phy, "ref_clk_src",
				   &phy_common->ref_clk_src);
	if (err)
		goto out;

	/*
	 * "ref_clk_parent" is optional hence don't abort init if it's not
	 * found.
	 */
	__ufs_qcom_phy_clk_get(generic_phy, "ref_clk_parent",
				   &phy_common->ref_clk_parent, false);

	err = ufs_qcom_phy_clk_get(generic_phy, "ref_clk",
				   &phy_common->ref_clk);

out:
	return err;
}

int
ufs_qcom_phy_init_vregulators(struct phy *generic_phy,
			      struct ufs_qcom_phy *phy_common)
{
	int err;

	err = ufs_qcom_phy_init_vreg(generic_phy, &phy_common->vdda_pll,
		"vdda-pll");
	if (err)
		goto out;

	err = ufs_qcom_phy_init_vreg(generic_phy, &phy_common->vdda_phy,
		"vdda-phy");

	if (err)
		goto out;

	/* vddp-ref-clk-* properties are optional */
	__ufs_qcom_phy_init_vreg(generic_phy, &phy_common->vddp_ref_clk,
				 "vddp-ref-clk", true);
out:
	return err;
}

static int __ufs_qcom_phy_init_vreg(struct phy *phy,
		struct ufs_qcom_phy_vreg *vreg, const char *name, bool optional)
{
	int err = 0;
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(phy);
	struct device *dev = ufs_qcom_phy->dev;

	char prop_name[MAX_PROP_NAME];

	vreg->name = kstrdup(name, GFP_KERNEL);
	if (!vreg->name) {
		err = -ENOMEM;
		goto out;
	}

	vreg->reg = devm_regulator_get(dev, name);
	if (IS_ERR(vreg->reg)) {
		err = PTR_ERR(vreg->reg);
		vreg->reg = NULL;
		if (!optional)
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
		snprintf(prop_name, MAX_PROP_NAME, "%s-always-on", name);
		if (of_get_property(dev->of_node, prop_name, NULL))
			vreg->is_always_on = true;
		else
			vreg->is_always_on = false;
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
	if (err)
		kfree(vreg->name);
	return err;
}

static int ufs_qcom_phy_init_vreg(struct phy *phy,
			struct ufs_qcom_phy_vreg *vreg, const char *name)
{
	return __ufs_qcom_phy_init_vreg(phy, vreg, name, false);
}

static
int ufs_qcom_phy_cfg_vreg(struct phy *phy,
			  struct ufs_qcom_phy_vreg *vreg, bool on)
{
	int ret = 0;
	struct regulator *reg = vreg->reg;
	const char *name = vreg->name;
	int min_uV;
	int uA_load;
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(phy);
	struct device *dev = ufs_qcom_phy->dev;

	BUG_ON(!vreg);

	if (regulator_count_voltages(reg) > 0) {
		min_uV = on ? vreg->min_uV : 0;
		ret = regulator_set_voltage(reg, min_uV, vreg->max_uV);
		if (ret) {
			dev_err(dev, "%s: %s set voltage failed, err=%d\n",
					__func__, name, ret);
			goto out;
		}
		uA_load = on ? vreg->max_uA : 0;
		ret = regulator_set_optimum_mode(reg, uA_load);
		if (ret >= 0) {
			/*
			 * regulator_set_optimum_mode() returns new regulator
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

static
int ufs_qcom_phy_enable_vreg(struct phy *phy,
			     struct ufs_qcom_phy_vreg *vreg)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(phy);
	struct device *dev = ufs_qcom_phy->dev;
	int ret = 0;

	if (!vreg || vreg->enabled)
		goto out;

	ret = ufs_qcom_phy_cfg_vreg(phy, vreg, true);
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

int ufs_qcom_phy_enable_ref_clk(struct phy *generic_phy)
{
	int ret = 0;
	struct ufs_qcom_phy *phy = get_ufs_qcom_phy(generic_phy);

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

static
int ufs_qcom_phy_disable_vreg(struct phy *phy,
			      struct ufs_qcom_phy_vreg *vreg)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(phy);
	struct device *dev = ufs_qcom_phy->dev;
	int ret = 0;

	if (!vreg || !vreg->enabled || vreg->is_always_on)
		goto out;

	ret = regulator_disable(vreg->reg);

	if (!ret) {
		/* ignore errors on applying disable config */
		ufs_qcom_phy_cfg_vreg(phy, vreg, false);
		vreg->enabled = false;
	} else {
		dev_err(dev, "%s: %s disable failed, err=%d\n",
				__func__, vreg->name, ret);
	}
out:
	return ret;
}

void ufs_qcom_phy_disable_ref_clk(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy = get_ufs_qcom_phy(generic_phy);

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

#define UFS_REF_CLK_EN	(1 << 5)

static void ufs_qcom_phy_dev_ref_clk_ctrl(struct phy *generic_phy, bool enable)
{
	struct ufs_qcom_phy *phy = get_ufs_qcom_phy(generic_phy);

	if (phy->dev_ref_clk_ctrl_mmio &&
	    (enable ^ phy->is_dev_ref_clk_enabled)) {
		u32 temp = readl_relaxed(phy->dev_ref_clk_ctrl_mmio);

		if (enable)
			temp |= UFS_REF_CLK_EN;
		else
			temp &= ~UFS_REF_CLK_EN;

		/*
		 * If we are here to disable this clock immediately after
		 * entering into hibern8, we need to make sure that device
		 * ref_clk is active atleast 1us after the hibern8 enter.
		 */
		if (!enable)
			udelay(1);

		writel_relaxed(temp, phy->dev_ref_clk_ctrl_mmio);
		/* ensure that ref_clk is enabled/disabled before we return */
		wmb();
		/*
		 * If we call hibern8 exit after this, we need to make sure that
		 * device ref_clk is stable for atleast 1us before the hibern8
		 * exit command.
		 */
		if (enable)
			udelay(1);

		phy->is_dev_ref_clk_enabled = enable;
	}
}

void ufs_qcom_phy_enable_dev_ref_clk(struct phy *generic_phy)
{
	ufs_qcom_phy_dev_ref_clk_ctrl(generic_phy, true);
}

void ufs_qcom_phy_disable_dev_ref_clk(struct phy *generic_phy)
{
	ufs_qcom_phy_dev_ref_clk_ctrl(generic_phy, false);
}

/* Turn ON M-PHY RMMI interface clocks */
int ufs_qcom_phy_enable_iface_clk(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy = get_ufs_qcom_phy(generic_phy);
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
void ufs_qcom_phy_disable_iface_clk(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy = get_ufs_qcom_phy(generic_phy);

	if (phy->is_iface_clk_enabled) {
		clk_disable_unprepare(phy->tx_iface_clk);
		clk_disable_unprepare(phy->rx_iface_clk);
		phy->is_iface_clk_enabled = false;
	}
}

int ufs_qcom_phy_start_serdes(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);
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

void ufs_qcom_phy_save_controller_version(struct phy *generic_phy,
					  u8 major, u16 minor, u16 step)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	ufs_qcom_phy->host_ctrl_rev_major = major;
	ufs_qcom_phy->host_ctrl_rev_minor = minor;
	ufs_qcom_phy->host_ctrl_rev_step = step;
}

int ufs_qcom_phy_calibrate_phy(struct phy *generic_phy, bool is_rate_B)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);
	int ret = 0;

	if (!ufs_qcom_phy->phy_spec_ops->calibrate_phy) {
		dev_err(ufs_qcom_phy->dev, "%s: calibrate_phy() callback is not supported\n",
			__func__);
		ret = -ENOTSUPP;
	} else {
		ret = ufs_qcom_phy->phy_spec_ops->
				calibrate_phy(ufs_qcom_phy, is_rate_B);
		if (ret)
			dev_err(ufs_qcom_phy->dev, "%s: calibrate_phy() failed %d\n",
				__func__, ret);
	}

	return ret;
}

int ufs_qcom_phy_remove(struct phy *generic_phy,
			struct ufs_qcom_phy *ufs_qcom_phy)
{
	phy_power_off(generic_phy);

	kfree(ufs_qcom_phy->vdda_pll.name);
	kfree(ufs_qcom_phy->vdda_phy.name);

	return 0;
}

int ufs_qcom_phy_exit(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->is_powered_on)
		phy_power_off(generic_phy);

	return 0;
}

int ufs_qcom_phy_is_pcs_ready(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

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
	int err;

	err = ufs_qcom_phy_enable_vreg(generic_phy, &phy_common->vdda_phy);
	if (err) {
		dev_err(dev, "%s enable vdda_phy failed, err=%d\n",
			__func__, err);
		goto out;
	}

	phy_common->phy_spec_ops->power_control(phy_common, true);

	/* vdda_pll also enables ref clock LDOs so enable it first */
	err = ufs_qcom_phy_enable_vreg(generic_phy, &phy_common->vdda_pll);
	if (err) {
		dev_err(dev, "%s enable vdda_pll failed, err=%d\n",
			__func__, err);
		goto out_disable_phy;
	}

	err = ufs_qcom_phy_enable_ref_clk(generic_phy);
	if (err) {
		dev_err(dev, "%s enable phy ref clock failed, err=%d\n",
			__func__, err);
		goto out_disable_pll;
	}

	/* enable device PHY ref_clk pad rail */
	if (phy_common->vddp_ref_clk.reg) {
		err = ufs_qcom_phy_enable_vreg(generic_phy,
					       &phy_common->vddp_ref_clk);
		if (err) {
			dev_err(dev, "%s enable vddp_ref_clk failed, err=%d\n",
				__func__, err);
			goto out_disable_ref_clk;
		}
	}

	phy_common->is_powered_on = true;
	goto out;

out_disable_ref_clk:
	ufs_qcom_phy_disable_ref_clk(generic_phy);
out_disable_pll:
	ufs_qcom_phy_disable_vreg(generic_phy, &phy_common->vdda_pll);
out_disable_phy:
	ufs_qcom_phy_disable_vreg(generic_phy, &phy_common->vdda_phy);
out:
	return err;
}

int ufs_qcom_phy_power_off(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);

	phy_common->phy_spec_ops->power_control(phy_common, false);

	if (phy_common->vddp_ref_clk.reg)
		ufs_qcom_phy_disable_vreg(generic_phy,
					  &phy_common->vddp_ref_clk);
	ufs_qcom_phy_disable_ref_clk(generic_phy);

	ufs_qcom_phy_disable_vreg(generic_phy, &phy_common->vdda_pll);
	ufs_qcom_phy_disable_vreg(generic_phy, &phy_common->vdda_phy);
	phy_common->is_powered_on = false;

	return 0;
}
