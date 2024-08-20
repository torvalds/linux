// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "phy-qcom-ufs-i.h"

#define MAX_PROP_NAME              32
#define VDDA_PHY_MIN_UV            880000
#define VDDA_PHY_MAX_UV            925000
#define VDDA_PLL_MIN_UV            1200000
#define VDDA_PLL_MAX_UV            1800000
#define VDDP_REF_CLK_MIN_UV        1200000
#define VDDP_REF_CLK_MAX_UV        1200000
#define VDDA_QREF_MIN_UV	   880000
#define VDDA_QREF_MAX_UV	   912000

#define UFS_PHY_DEFAULT_LANES_PER_DIRECTION	1

/**
 * struct ufs_qcom_phy_regs - record the info of ufs qcom phy register domain.
 * @list_head: the list to find all ufs phy register domins.
 * @prefix: the name of this register domain.
 * @ptr: the pointer to memory address which save the register value.
 * @len: the size of this register domain.
 */
struct ufs_qcom_phy_regs {
	struct list_head list;
	const char *prefix;
	u32 *ptr;
	size_t len;
};

static int ufs_qcom_phy_start_serdes(struct ufs_qcom_phy *ufs_qcom_phy);
static int ufs_qcom_phy_is_pcs_ready(struct ufs_qcom_phy *ufs_qcom_phy);

void ufs_qcom_phy_write_tbl(struct ufs_qcom_phy *ufs_qcom_phy,
			    struct ufs_qcom_phy_calibration *tbl,
			    int tbl_size)
{
	int i;

	for (i = 0; i < tbl_size; i++)
		writel_relaxed(tbl[i].cfg_value,
			       ufs_qcom_phy->mmio + tbl[i].reg_offset);
}
EXPORT_SYMBOL(ufs_qcom_phy_write_tbl);

int ufs_qcom_phy_calibrate(struct ufs_qcom_phy *ufs_qcom_phy,
			   struct ufs_qcom_phy_calibration *tbl_A,
			   int tbl_size_A,
			   struct ufs_qcom_phy_calibration *tbl_B,
			   int tbl_size_B, bool is_rate_B)
{
	struct device *dev = ufs_qcom_phy->dev;
	int ret = 0;

	ret = reset_control_assert(ufs_qcom_phy->ufs_reset);
	if (ret) {
		dev_err(dev, "Failed to assert UFS PHY reset %d\n", ret);
		goto out;
	}

	if (!tbl_A) {
		dev_err(dev, "%s: tbl_A is NULL\n", __func__);
		ret = EINVAL;
		goto out;
	}

	ufs_qcom_phy_write_tbl(ufs_qcom_phy, tbl_A, tbl_size_A);

	/*
	 * In case we would like to work in rate B, we need
	 * to override a registers that were configured in rate A table
	 * with registers of rate B table.
	 * table.
	 */
	if (is_rate_B) {
		if (!tbl_B) {
			dev_err(dev, "%s: tbl_B is NULL\n",
				__func__);
			ret = EINVAL;
			goto out;
		}

		ufs_qcom_phy_write_tbl(ufs_qcom_phy, tbl_B, tbl_size_B);
	}

	/* flush buffered writes */
	mb();

	ret = reset_control_deassert(ufs_qcom_phy->ufs_reset);
	if (ret)
		dev_err(dev, "Failed to deassert UFS PHY reset %d\n", ret);

	ret = ufs_qcom_phy_start_serdes(ufs_qcom_phy);
	if (ret)
		goto out;

	ret = ufs_qcom_phy_is_pcs_ready(ufs_qcom_phy);

out:
	return ret;
}
EXPORT_SYMBOL(ufs_qcom_phy_calibrate);

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
EXPORT_SYMBOL(get_ufs_qcom_phy);

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

	if (of_property_read_u32(dev->of_node, "lanes-per-direction",
				 &common_cfg->lanes_per_direction))
		common_cfg->lanes_per_direction =
			UFS_PHY_DEFAULT_LANES_PER_DIRECTION;

	/*
	 * UFS PHY power management is managed by its parent (UFS host
	 * controller) hence set the no runtime PM callbacks flag
	 * on UFS PHY device to avoid any accidental attempt to call the
	 * PM callbacks for PHY device.
	 */
	pm_runtime_no_callbacks(&generic_phy->dev);

	common_cfg->phy_spec_ops = phy_spec_ops;
	common_cfg->dev = dev;

	/*
	 * Init PHY register domain list. We use it to manage the memory space which be used
	 * to save UFS PHY register value.
	 */
	INIT_LIST_HEAD(&common_cfg->regs_list_head);

out:
	return generic_phy;
}
EXPORT_SYMBOL(ufs_qcom_phy_generic_probe);

int ufs_qcom_phy_get_reset(struct ufs_qcom_phy *phy_common)
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
EXPORT_SYMBOL(ufs_qcom_phy_get_reset);

static int __ufs_qcom_phy_clk_get(struct device *dev,
			 const char *name, struct clk **clk_out, bool err_print)
{
	struct clk *clk;
	int err = 0;

	clk = devm_clk_get(dev, name);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		if (err_print)
			dev_err(dev, "failed get %s, %d\n", name, err);
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

	if (!phy_common) {
		pr_err("%s: Defering the probe: Failed to get the required phy handle!!\n",
			__func__);
		return -EPROBE_DEFER;
	}

	if (of_device_is_compatible(phy_common->dev->of_node,
				"qcom,msm8996-ufs-phy-qmp-14nm"))
		goto skip_txrx_clk;
	/*
	 * tx_iface_clk does not exist in newer version of ufs-phy HW,
	 * so don't return error if it is not found
	 */
	__ufs_qcom_phy_clk_get(phy_common->dev, "tx_iface_clk",
				   &phy_common->tx_iface_clk, false);

	/*
	 * rx_iface_clk does not exist in newer version of ufs-phy HW,
	 * so don't return error if it is not found
	 */
	__ufs_qcom_phy_clk_get(phy_common->dev, "rx_iface_clk",
				   &phy_common->rx_iface_clk, false);

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

	/*
	 * "ref_clk_pad_en" is only required in case where UFS_PHY and
	 * UFS_REF_CLK_BSM both needs to be enabled for REF clock supply
	 * to card. Hence don't abort init if it's not found.
	 */
	__ufs_qcom_phy_clk_get(phy_common->dev, "ref_clk_pad_en",
				&phy_common->ref_clk_pad_en, false);

	/*
	 * Some platforms may not have the ON/OFF control for reference clock,
	 * hence this clock may be optional.
	 */
	__ufs_qcom_phy_clk_get(phy_common->dev, "ref_clk",
				   &phy_common->ref_clk, false);

	/*
	 * "ref_aux_clk" is optional and only supported by certain
	 * phy versions, don't abort init if it's not found.
	 */
	 __ufs_qcom_phy_clk_get(phy_common->dev, "ref_aux_clk",
				   &phy_common->ref_aux_clk, false);

	 /*
	  * "qref_clk_signal" is optional. It is needed for certain platforms.
	  * No need to abort if it's not present.
	  */
	 __ufs_qcom_phy_clk_get(phy_common->dev, "qref_clk",
				   &phy_common->qref_clk, false);

	 __ufs_qcom_phy_clk_get(phy_common->dev, "rx_sym0_mux_clk",
				   &phy_common->rx_sym0_mux_clk, false);
	 __ufs_qcom_phy_clk_get(phy_common->dev, "rx_sym1_mux_clk",
				   &phy_common->rx_sym1_mux_clk, false);
	 __ufs_qcom_phy_clk_get(phy_common->dev, "tx_sym0_mux_clk",
				   &phy_common->tx_sym0_mux_clk, false);
	 __ufs_qcom_phy_clk_get(phy_common->dev, "rx_sym0_phy_clk",
				   &phy_common->rx_sym0_phy_clk, false);
	 __ufs_qcom_phy_clk_get(phy_common->dev, "rx_sym1_phy_clk",
				   &phy_common->rx_sym1_phy_clk, false);
	 __ufs_qcom_phy_clk_get(phy_common->dev, "tx_sym0_phy_clk",
				   &phy_common->tx_sym0_phy_clk, false);
	if (!phy_common->rx_sym0_mux_clk ||
		!phy_common->rx_sym1_mux_clk ||
		!phy_common->tx_sym0_mux_clk ||
		!phy_common->ref_clk_src ||
		!phy_common->rx_sym0_phy_clk ||
		!phy_common->rx_sym1_phy_clk ||
		!phy_common->tx_sym0_phy_clk)
		dev_err(phy_common->dev, "%s: null clock\n", __func__);
out:
	return err;
}
EXPORT_SYMBOL(ufs_qcom_phy_init_clks);

static int ufs_qcom_phy_init_vreg(struct device *dev,
				  struct ufs_qcom_phy_vreg *vreg,
				  const char *name)
{
	int err = 0;

	char prop_name[MAX_PROP_NAME];

	if (dev->of_node) {
		snprintf(prop_name, MAX_PROP_NAME, "%s-supply", name);
		if (!of_parse_phandle(dev->of_node, prop_name, 0)) {
			dev_dbg(dev, "No vreg data found for %s\n", prop_name);
			return -ENODATA;
		}
	}

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
			if (!vreg->max_uA) {
				err = 0;
			} else if (regulator_count_voltages(vreg->reg) > 0) {
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

		snprintf(prop_name, MAX_PROP_NAME, "%s-min-microvolt", name);
		of_property_read_u32(dev->of_node,
				     prop_name, &vreg->min_uV);
		if (vreg->min_uV < VDDA_PHY_MIN_UV ||
			vreg->min_uV > VDDA_PHY_MAX_UV) {
			dev_err(dev, "%s: ufs vdda-phy invalid min_uV=%duV\n",
				__func__, vreg->min_uV);
			vreg->min_uV = VDDA_PHY_MIN_UV;
		}
	} else if (!strcmp(name, "vddp-ref-clk")) {
		vreg->max_uV = VDDP_REF_CLK_MAX_UV;
		vreg->min_uV = VDDP_REF_CLK_MIN_UV;
	} else if (!strcmp(name, "vdda-qref")) {
		vreg->max_uV = VDDA_QREF_MAX_UV;
		vreg->min_uV = VDDA_QREF_MIN_UV;
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

	ufs_qcom_phy_init_vreg(phy_common->dev, &phy_common->vddp_ref_clk,
				     "vddp-ref-clk");

	ufs_qcom_phy_init_vreg(phy_common->dev, &phy_common->vdd_phy_gdsc,
			       "vdd-phy-gdsc");

	ufs_qcom_phy_init_vreg(phy_common->dev, &phy_common->vdda_qref,
			       "vdda-qref");

out:
	return err;
}
EXPORT_SYMBOL(ufs_qcom_phy_init_vregulators);

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
	 * "ref_clk_pad_en" is only required if UFS_PHY and UFS_REF_CLK_BSM
	 * both needs to be enabled. Hence make sure that clk reference
	 * is available before trying to enable the clock.
	 */
	if (phy->ref_clk_pad_en) {
		ret = clk_prepare_enable(phy->ref_clk_pad_en);
		if (ret) {
			dev_err(phy->dev, "%s: ref_clk_pad_en enable failed %d\n",
				__func__, ret);
			goto out;
	}
}

	/* qref clk signal is optional */
	if (phy->qref_clk)
		clk_prepare_enable(phy->qref_clk);
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

	/*
	 * "ref_clk" is optional clock hence make sure that clk reference
	 * is available before trying to enable the clock.
	 */
	if (phy->ref_clk) {
		ret = clk_prepare_enable(phy->ref_clk);
		if (ret) {
			dev_err(phy->dev, "%s: ref_clk enable failed %d\n",
					__func__, ret);
			goto out_disable_parent;
		}
	}

	/*
	 * "ref_aux_clk" is optional clock and only supported by certain
	 * phy versions, hence make sure that clk reference is available
	 * before trying to enable the clock.
	 */
	if (phy->ref_aux_clk) {
		ret = clk_prepare_enable(phy->ref_aux_clk);
		if (ret) {
			dev_err(phy->dev, "%s: ref_aux_clk enable failed %d\n",
					__func__, ret);
			goto out_disable_ref;
		}
	}

	phy->is_ref_clk_enabled = true;
	goto out;

out_disable_ref:
	if (phy->ref_clk)
		clk_disable_unprepare(phy->ref_clk);
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
		/*
		 * "ref_aux_clk" is optional clock and only supported by
		 * certain phy versions, hence make sure that clk reference
		 * is available before trying to disable the clock.
		 */
		if (phy->ref_aux_clk)
			clk_disable_unprepare(phy->ref_aux_clk);

		/*
		 * "ref_clk" is optional clock hence make sure that clk
		 * reference is available before trying to disable the clock.
		 */
		if (phy->ref_clk)
			clk_disable_unprepare(phy->ref_clk);

		/*
		 * "ref_clk_parent" is optional clock hence make sure that clk
		 * reference is available before trying to disable the clock.
		 */
		if (phy->ref_clk_parent)
			clk_disable_unprepare(phy->ref_clk_parent);
		clk_disable_unprepare(phy->ref_clk_src);

		/*
		 * "ref_clk_pad_en" is optional clock hence make sure that clk
		 * reference is available before trying to disable the clock.
		 */
		if (phy->ref_clk_pad_en)
			clk_disable_unprepare(phy->ref_clk_pad_en);

		/* qref clk signal is optional */
		if (phy->qref_clk)
			clk_disable_unprepare(phy->qref_clk);

		phy->is_ref_clk_enabled = false;
	}
}

/* Turn ON M-PHY RMMI interface clocks */
static int ufs_qcom_phy_enable_iface_clk(struct ufs_qcom_phy *phy)
{
	int ret = 0;

	if (phy->is_iface_clk_enabled)
		goto out;

	if (!phy->tx_iface_clk)
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
	if (!phy->tx_iface_clk)
		return;

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
		ret = -EOPNOTSUPP;
	} else {
		ufs_qcom_phy->phy_spec_ops->start_serdes(ufs_qcom_phy);
	}

	return ret;
}

void ufs_qcom_phy_set_tx_lane_enable(struct phy *generic_phy, u32 tx_lanes)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->set_tx_lane_enable)
		ufs_qcom_phy->phy_spec_ops->set_tx_lane_enable(ufs_qcom_phy,
							       tx_lanes);
}
EXPORT_SYMBOL(ufs_qcom_phy_set_tx_lane_enable);

int ufs_qcom_phy_save_controller_version(struct phy *generic_phy,
					  u8 major, u16 minor, u16 step)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (!ufs_qcom_phy)
		return -EPROBE_DEFER;

	ufs_qcom_phy->host_ctrl_rev_major = major;
	ufs_qcom_phy->host_ctrl_rev_minor = minor;
	ufs_qcom_phy->host_ctrl_rev_step = step;
	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_save_controller_version);

void ufs_qcom_phy_set_src_clk_h8_enter(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (!ufs_qcom_phy->rx_sym0_mux_clk || !ufs_qcom_phy->rx_sym1_mux_clk ||
		!ufs_qcom_phy->tx_sym0_mux_clk || !ufs_qcom_phy->ref_clk_src)
		return;

	/*
	 * Before entering hibernate, select xo as source of symbol
	 * clocks according to the UFS Host Controller Hardware
	 * Programming Guide's "Hibernate enter with power collapse".
	 */
	clk_set_parent(ufs_qcom_phy->rx_sym0_mux_clk, ufs_qcom_phy->ref_clk_src);
	clk_set_parent(ufs_qcom_phy->rx_sym1_mux_clk, ufs_qcom_phy->ref_clk_src);
	clk_set_parent(ufs_qcom_phy->tx_sym0_mux_clk, ufs_qcom_phy->ref_clk_src);
}
EXPORT_SYMBOL(ufs_qcom_phy_set_src_clk_h8_enter);

void ufs_qcom_phy_set_src_clk_h8_exit(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (!ufs_qcom_phy->rx_sym0_mux_clk ||
		!ufs_qcom_phy->rx_sym1_mux_clk ||
		!ufs_qcom_phy->tx_sym0_mux_clk ||
		!ufs_qcom_phy->rx_sym0_phy_clk ||
		!ufs_qcom_phy->rx_sym1_phy_clk ||
		!ufs_qcom_phy->tx_sym0_phy_clk)
		return;

	/*
	 * Refer to the UFS Host Controller Hardware Programming Guide's
	 * section "Hibernate exit from power collapse". Select phy clocks
	 * as source of the PHY symbol clocks.
	 */
	clk_set_parent(ufs_qcom_phy->rx_sym0_mux_clk, ufs_qcom_phy->rx_sym0_phy_clk);
	clk_set_parent(ufs_qcom_phy->rx_sym1_mux_clk, ufs_qcom_phy->rx_sym1_phy_clk);
	clk_set_parent(ufs_qcom_phy->tx_sym0_mux_clk, ufs_qcom_phy->tx_sym0_phy_clk);
}
EXPORT_SYMBOL(ufs_qcom_phy_set_src_clk_h8_exit);

static int ufs_qcom_phy_is_pcs_ready(struct ufs_qcom_phy *ufs_qcom_phy)
{
	if (!ufs_qcom_phy->phy_spec_ops->is_physical_coding_sublayer_ready) {
		dev_err(ufs_qcom_phy->dev, "%s: is_physical_coding_sublayer_ready() callback is not supported\n",
			__func__);
		return -EOPNOTSUPP;
	}

	return ufs_qcom_phy->phy_spec_ops->is_physical_coding_sublayer_ready(ufs_qcom_phy);
}

int ufs_qcom_phy_power_on(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);
	struct device *dev = phy_common->dev;
	int err;

	if (phy_common->vdd_phy_gdsc.reg) {
		err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdd_phy_gdsc);
		if (err) {
			dev_err(dev, "%s enable phy_gdsc failed, err=%d\n",
				__func__, err);
			goto out;
		}
	}

	if (phy_common->vdda_qref.reg) {
		err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdda_qref);
		if (err) {
			dev_err(dev, "%s enable vdda_qref failed, err=%d\n",
				__func__, err);
			goto out;
		}
	}

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
EXPORT_SYMBOL(ufs_qcom_phy_power_on);

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
	if (phy_common->vdda_qref.reg)
		ufs_qcom_phy_disable_vreg(phy_common->dev, &phy_common->vdda_qref);
	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_power_off);

void ufs_qcom_phy_ctrl_rx_linecfg(struct phy *generic_phy, bool ctrl)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->ctrl_rx_linecfg)
		ufs_qcom_phy->phy_spec_ops->ctrl_rx_linecfg(ufs_qcom_phy, ctrl);
}
EXPORT_SYMBOL(ufs_qcom_phy_ctrl_rx_linecfg);

int ufs_qcom_phy_get_tx_hs_equalizer(struct phy *generic_phy, u32 gear, u32 *val)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (!ufs_qcom_phy->phy_spec_ops->get_tx_hs_equalizer)
		return -EOPNOTSUPP;

	*val = ufs_qcom_phy->phy_spec_ops->get_tx_hs_equalizer(ufs_qcom_phy, gear);
	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_get_tx_hs_equalizer);

int ufs_qcom_phy_dump_regs(struct ufs_qcom_phy *phy, int offset,
		int len, char *prefix)
{
	u32 *regs;
	size_t pos;

	if (offset % 4 != 0 || len % 4 != 0) /* keep readl happy */
		return -EINVAL;

	regs = kzalloc(len, GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	for (pos = 0; pos < len; pos += 4)
		regs[pos / 4] = readl_relaxed(phy->mmio + offset + pos);

	print_hex_dump(KERN_ERR, prefix,
			len > 4 ? DUMP_PREFIX_OFFSET : DUMP_PREFIX_NONE,
			16, 4, regs, len, false);
	kfree(regs);

	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_dump_regs);

/**
 * ufs_qcom_phy_save_regs - save specified domain of ufs phy registers to memory
 * @phy - pointer to ufs qcom phy
 * @offset - register address offset
 * @len - size of this domain
 * @prefix - name of this domain
 */
int ufs_qcom_phy_save_regs(struct ufs_qcom_phy *phy, int offset,
		int len, char *prefix)
{
	struct ufs_qcom_phy_regs *regs = NULL;
	struct list_head *head = &phy->regs_list_head;
	size_t pos;
	unsigned int noio_flag;

	if (offset % 4 != 0 || len % 4 != 0)
		return -EINVAL;

	/* find the node if this register domain has been saved before */
	list_for_each_entry(regs, head, list)
		if (regs->prefix && !strcmp(regs->prefix, prefix))
			break;

	/* create a new node and add it to list if this domain never been written */
	if (&regs->list == head) {
		/*
		 * use memalloc_noio_save() here as GFP_ATOMIC should not be invoked
		 * in an IO error context
		 */
		noio_flag = memalloc_noio_save();
		regs = devm_kzalloc(phy->dev, sizeof(*regs), GFP_ATOMIC);
		if (!regs)
			goto out;
		regs->ptr = devm_kzalloc(phy->dev, len, GFP_ATOMIC);
		if (!regs->ptr)
			goto out;
		memalloc_noio_restore(noio_flag);
		regs->prefix = prefix;
		regs->len = len;
		list_add_tail(&regs->list, &phy->regs_list_head);
	}

	for (pos = 0; pos < len; pos += 4)
		regs->ptr[pos / 4] = readl_relaxed(phy->mmio + offset + pos);
	return 0;

out:
	memalloc_noio_restore(noio_flag);
	return -ENOMEM;
}
EXPORT_SYMBOL(ufs_qcom_phy_save_regs);

void ufs_qcom_phy_dbg_register_dump(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->dbg_register_dump)
		ufs_qcom_phy->phy_spec_ops->dbg_register_dump(ufs_qcom_phy);
}
EXPORT_SYMBOL(ufs_qcom_phy_dbg_register_dump);

void ufs_qcom_phy_dbg_register_save(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->dbg_register_save)
		ufs_qcom_phy->phy_spec_ops->dbg_register_save(ufs_qcom_phy);

}
EXPORT_SYMBOL(ufs_qcom_phy_dbg_register_save);

MODULE_DESCRIPTION("Universal Flash Storage (UFS) QCOM PHY");
MODULE_LICENSE("GPL v2");
