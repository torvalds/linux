// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip USB3.0 and PCIE COMBPHY with Innosilicon IP block driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <dt-bindings/phy/phy.h>

#define BIT_WRITEABLE_SHIFT		16

struct rockchip_combphy_priv;

enum rockchip_combphy_rst {
	OTG_RSTN	= 0,
	PHY_POR_RSTN	= 1,
	PHY_APB_RSTN	= 2,
	PHY_PIPE_RSTN	= 3,
	PHY_RESET_MAX	= 4,
};

struct combphy_reg {
	u32	offset;
	u32	bitend;
	u32	bitstart;
	u32	disable;
	u32	enable;
};

struct rockchip_combphy_grfcfg {
	struct combphy_reg	pipe_l1_sel;
	struct combphy_reg	pipe_l1_set;
	struct combphy_reg	pipe_l1pd_sel;
	struct combphy_reg	pipe_l1pd_p3;
	struct combphy_reg	pipe_l0pd_sel;
	struct combphy_reg	pipe_l0pd_p3;
	struct combphy_reg	pipe_clk_sel;
	struct combphy_reg	pipe_clk_set;
	struct combphy_reg	pipe_rate_sel;
	struct combphy_reg	pipe_rate_set;
	struct combphy_reg	pipe_mode_sel;
	struct combphy_reg	pipe_mode_set;
	struct combphy_reg	pipe_txrx_sel;
	struct combphy_reg	pipe_txrx_set;
	struct combphy_reg	pipe_width_sel;
	struct combphy_reg	pipe_width_set;
	struct combphy_reg	pipe_usb3_sel;
	struct combphy_reg	pipe_pll_lock;
	struct combphy_reg	pipe_status_l0;
	struct combphy_reg	pipe_l0rxterm_sel;
	struct combphy_reg	pipe_l1rxterm_sel;
	struct combphy_reg	pipe_l0rxterm_set;
	struct combphy_reg	pipe_l1rxterm_set;
	struct combphy_reg	pipe_l0rxelec_set;
	struct combphy_reg	u3_port_disable;
	struct combphy_reg      u3_port_num;
};

struct rockchip_combphy_cfg {
	const struct rockchip_combphy_grfcfg grfcfg;
	int (*combphy_u3_cp_test)(struct rockchip_combphy_priv *priv);
	int (*combphy_cfg)(struct rockchip_combphy_priv *priv);
	int (*combphy_low_power_ctrl)(struct rockchip_combphy_priv *priv,
				      bool en);
};

struct rockchip_combphy_priv {
	bool phy_initialized;
	bool phy_suspended;
	u8 phy_type;
	void __iomem *mmio;
	struct device *dev;
	struct clk *ref_clk;
	struct phy *phy;
	struct regmap *combphy_grf;
	struct regmap *usb_pcie_grf;
	struct reset_control *rsts[PHY_RESET_MAX];
	const struct rockchip_combphy_cfg *cfg;
};

static const char *get_reset_name(enum rockchip_combphy_rst rst)
{
	switch (rst) {
	case OTG_RSTN:
		return "otg-rst";
	case PHY_POR_RSTN:
		return "combphy-por";
	case PHY_APB_RSTN:
		return "combphy-apb";
	case PHY_PIPE_RSTN:
		return "combphy-pipe";
	default:
		return "invalid";
	}
}

static inline bool param_read(struct regmap *base,
			      const struct combphy_reg *reg, u32 val)
{
	int ret;
	u32 mask, orig, tmp;

	ret = regmap_read(base, reg->offset, &orig);
	if (ret)
		return false;

	mask = GENMASK(reg->bitend, reg->bitstart);
	tmp = (orig & mask) >> reg->bitstart;

	return tmp == val;
}

static inline int param_write(struct regmap *base,
			      const struct combphy_reg *reg, bool en)
{
	u32 val, mask, tmp;

	tmp = en ? reg->enable : reg->disable;
	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);

	return regmap_write(base, reg->offset, val);
}

static inline bool param_exped(void __iomem *base,
			       const struct combphy_reg *reg,
			       unsigned int value)
{
	int ret;
	unsigned int tmp, orig;
	unsigned int mask = GENMASK(reg->bitend, reg->bitstart);

	ret = regmap_read(base, reg->offset, &orig);
	if (ret)
		return false;

	tmp = (orig & mask) >> reg->bitstart;

	return tmp == value;
}

static ssize_t u3phy_mode_show(struct device *device,
			       struct device_attribute *attr,
			       char *buf)
{
	struct rockchip_combphy_priv *priv = dev_get_drvdata(device);

	if (param_exped(priv->usb_pcie_grf,
			&priv->cfg->grfcfg.u3_port_num, 0))
		return sprintf(buf, "u2\n");
	else
		return sprintf(buf, "u3\n");
}

static ssize_t u3phy_mode_store(struct device *device,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rockchip_combphy_priv *priv = dev_get_drvdata(device);

	if (!strncmp(buf, "u3", 2) &&
	    param_exped(priv->usb_pcie_grf,
			&priv->cfg->grfcfg.u3_port_num, 0)) {
		/*
		 * Enable USB 3.0 rx termination, need to select
		 * pipe_l0_rxtermination from USB 3.0 controller.
		 */
		param_write(priv->combphy_grf,
			    &priv->cfg->grfcfg.pipe_l0rxterm_sel, false);
		/* Set xHCI USB 3.0 port number to 1 */
		param_write(priv->usb_pcie_grf,
			    &priv->cfg->grfcfg.u3_port_num, true);
		/* Enable xHCI USB 3.0 port */
		param_write(priv->usb_pcie_grf,
			    &priv->cfg->grfcfg.u3_port_disable, false);
		dev_info(priv->dev, "Set usb3.0 and usb2.0 mode successfully\n");
	} else if (!strncmp(buf, "u2", 2) &&
		   param_exped(priv->usb_pcie_grf,
			       &priv->cfg->grfcfg.u3_port_num, 1)) {
		/*
		 * Disable USB 3.0 rx termination, need to select
		 * pipe_l0_rxtermination from grf and remove rx
		 * termimation by grf.
		 */
		param_write(priv->combphy_grf,
			    &priv->cfg->grfcfg.pipe_l0rxterm_set, false);
		param_write(priv->combphy_grf,
			    &priv->cfg->grfcfg.pipe_l0rxterm_sel, true);
		/* Set xHCI USB 3.0 port number to 0 */
		param_write(priv->usb_pcie_grf,
			    &priv->cfg->grfcfg.u3_port_num, false);
		/* Disable xHCI USB 3.0 port */
		param_write(priv->usb_pcie_grf,
			    &priv->cfg->grfcfg.u3_port_disable, true);
		/*
		 * Note:
		 * Don't disable the USB 3.0 PIPE pclk here(set reg
		 * pipe_usb3_sel to false), because USB 3.0 PHY depend
		 * on this clk, if we disable it, we need to reinit
		 * the USB 3.0 PHY when use USB 3.0 mode, in order to
		 * simplify the process, don't disable this PIPE pclk.
		 */
		dev_info(priv->dev, "Set usb2.0 only mode successfully\n");
	} else {
		dev_info(priv->dev, "Same or illegal mode\n");
	}

	return count;
}

static DEVICE_ATTR_RW(u3phy_mode);

static struct attribute *rockchip_combphy_u3phy_mode_attrs[] = {
	&dev_attr_u3phy_mode.attr,
	NULL,
};

static struct attribute_group rockchip_combphy_u3phy_mode_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = rockchip_combphy_u3phy_mode_attrs,
};

static u32 rockchip_combphy_pll_lock(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *grfcfg;
	u32 mask, val;

	grfcfg = &priv->cfg->grfcfg;
	mask = GENMASK(grfcfg->pipe_pll_lock.bitend,
		       grfcfg->pipe_pll_lock.bitstart);

	regmap_read(priv->combphy_grf, grfcfg->pipe_pll_lock.offset, &val);
	val = (val & mask) >> grfcfg->pipe_pll_lock.bitstart;

	return val;
}

static u32 rockchip_combphy_is_ready(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *grfcfg;
	u32 mask, val;

	grfcfg = &priv->cfg->grfcfg;
	mask = GENMASK(grfcfg->pipe_status_l0.bitend,
		       grfcfg->pipe_status_l0.bitstart);

	regmap_read(priv->combphy_grf, grfcfg->pipe_status_l0.offset, &val);
	val = (val & mask) >> grfcfg->pipe_status_l0.bitstart;

	return val;
}

static int phy_pcie_init(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *grfcfg;
	u32 val;
	int ret = 0;

	grfcfg = &priv->cfg->grfcfg;

	reset_control_deassert(priv->rsts[PHY_POR_RSTN]);
	/* Wait PHY power on stable */
	udelay(5);
	reset_control_deassert(priv->rsts[PHY_APB_RSTN]);
	udelay(5);

	/* Set rxtermination for lane0 */
	param_write(priv->combphy_grf, &grfcfg->pipe_l0rxterm_set, true);
	/* Set rxtermination for lane1 */
	param_write(priv->combphy_grf, &grfcfg->pipe_l1rxterm_set, true);
	/* Select pipe_l0_rxtermination from grf */
	param_write(priv->combphy_grf, &grfcfg->pipe_l0rxterm_sel, true);
	/* Select pipe_l1_rxtermination from grf */
	param_write(priv->combphy_grf, &grfcfg->pipe_l1rxterm_sel, true);
	/* Select rxelecidle_disable and txcommonmode from PCIe controller */
	param_write(priv->combphy_grf, &grfcfg->pipe_txrx_sel, false);

	/* Start to configurate PHY registers for PCIE. */
	if (priv->cfg->combphy_cfg) {
		ret = priv->cfg->combphy_cfg(priv);
		if (ret)
			goto error;
	}

	/* Wait Tx PLL lock */
	usleep_range(300, 350);
	ret = readx_poll_timeout_atomic(rockchip_combphy_pll_lock, priv, val,
					val == grfcfg->pipe_pll_lock.enable,
					10, 1000);
	if (ret) {
		dev_err(priv->dev, "wait phy PLL lock timeout\n");
		goto error;
	}

	reset_control_deassert(priv->rsts[PHY_PIPE_RSTN]);
error:
	return ret;
}

static int phy_u3_init(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *grfcfg;
	u32 val;
	int ret = 0;

	grfcfg = &priv->cfg->grfcfg;

	/* Reset the USB3 controller first. */
	reset_control_assert(priv->rsts[OTG_RSTN]);

	reset_control_deassert(priv->rsts[PHY_POR_RSTN]);
	/* Wait PHY power on stable. */
	udelay(5);

	reset_control_deassert(priv->rsts[PHY_APB_RSTN]);
	udelay(5);

	/*
	 * Start to configurate PHY registers for USB3.
	 * Note: set operation must be done before corresponding
	 * sel operation, otherwise, the PIPE PHY status lane0
	 * may be unable to get ready.
	 */

	/* Disable PHY lane1 which isn't needed for USB3 */
	param_write(priv->combphy_grf, &grfcfg->pipe_l1_set, true);
	param_write(priv->combphy_grf, &grfcfg->pipe_l1_sel, true);

	/* Set PHY Tx and Rx for USB3 */
	param_write(priv->combphy_grf, &grfcfg->pipe_txrx_set, true);
	param_write(priv->combphy_grf, &grfcfg->pipe_txrx_sel, true);

	/* Set PHY PIPE MAC pclk request */
	param_write(priv->combphy_grf, &grfcfg->pipe_clk_set, true);
	param_write(priv->combphy_grf, &grfcfg->pipe_clk_sel, true);

	/* Set PHY PIPE rate for USB3 */
	param_write(priv->combphy_grf, &grfcfg->pipe_rate_set, true);
	param_write(priv->combphy_grf, &grfcfg->pipe_rate_sel, true);

	/* Set PHY mode for USB3 */
	param_write(priv->combphy_grf, &grfcfg->pipe_mode_set, true);
	param_write(priv->combphy_grf, &grfcfg->pipe_mode_sel, true);

	/* Set PHY data bus width for USB3 */
	param_write(priv->combphy_grf, &grfcfg->pipe_width_set, true);
	param_write(priv->combphy_grf, &grfcfg->pipe_width_sel, true);

	/* Select PIPE for USB3 */
	param_write(priv->combphy_grf, &grfcfg->pipe_usb3_sel, true);

	if (priv->cfg->combphy_cfg) {
		ret = priv->cfg->combphy_cfg(priv);
		if (ret)
			goto error;
	}

	/* Wait Tx PLL lock */
	usleep_range(300, 350);
	ret = readx_poll_timeout_atomic(rockchip_combphy_pll_lock, priv, val,
					val == grfcfg->pipe_pll_lock.enable,
					10, 1000);
	if (ret) {
		dev_err(priv->dev, "wait phy PLL lock timeout\n");
		goto error;
	}

	reset_control_deassert(priv->rsts[PHY_PIPE_RSTN]);

	/* Wait PIPE PHY status lane0 ready */
	ret = readx_poll_timeout_atomic(rockchip_combphy_is_ready, priv, val,
					val == grfcfg->pipe_status_l0.enable,
					10, 1000);
	if (ret) {
		dev_err(priv->dev, "wait phy status lane0 ready timeout\n");
		goto error;
	}

	reset_control_deassert(priv->rsts[OTG_RSTN]);

error:
	return ret;
}

static int rockchip_combphy_set_phy_type(struct rockchip_combphy_priv *priv)
{
	int ret = 0;

	if (priv->phy_initialized)
		return ret;

	switch (priv->phy_type) {
	case PHY_TYPE_PCIE:
		ret = phy_pcie_init(priv);
		break;
	case PHY_TYPE_USB3:
		ret = phy_u3_init(priv);
		if (ret)
			return ret;

		/* Attributes */
		ret = sysfs_create_group(&priv->dev->kobj,
					 &rockchip_combphy_u3phy_mode_attr_group);
		break;
	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		return -EINVAL;
	}

	return ret;
}

static int rockchip_combphy_init(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(priv->ref_clk);
	if (ret) {
		dev_err(priv->dev, "failed to enable ref_clk\n");
		return ret;
	}

	ret = rockchip_combphy_set_phy_type(priv);
	if (ret) {
		dev_err(priv->dev, "failed to set phy type\n");
		return ret;
	}

	priv->phy_initialized = true;

	return 0;
}

static int rockchip_combphy_exit(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);

	/*
	 * Note: don't assert PHY reset here, because
	 * we set many phy configurations during phy
	 * init to reduce PHY power consumption, if we
	 * assert PHY reset here, these configurations
	 * will be lost, and increase power consumption.
	 */
	clk_disable_unprepare(priv->ref_clk);

	return 0;
}

static int rockchip_combphy_power_on(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	const struct rockchip_combphy_grfcfg *grfcfg;

	if (!priv->phy_suspended)
		return 0;

	grfcfg = &priv->cfg->grfcfg;

	if (priv->phy_type == PHY_TYPE_USB3) {
		if (priv->cfg->combphy_low_power_ctrl)
			priv->cfg->combphy_low_power_ctrl(priv, false);

		/* Enable lane 0 squelch detection  */
		param_write(priv->combphy_grf, &grfcfg->pipe_l0rxelec_set,
			    false);

		/*
		 * Check if lane 0 powerdown is already
		 * controlled by USB 3.0 controller.
		 */
		if (param_read(priv->combphy_grf,
			       &grfcfg->pipe_l0pd_sel, 0))
			goto done;

		/* Exit to P0 from P3 */
		param_write(priv->combphy_grf, &grfcfg->pipe_l0pd_p3, false);
		usleep_range(250, 300);

		/*
		 * Set lane 0 powerdown to be controlled
		 * by USB 3.0 controller.
		 */
		param_write(priv->combphy_grf, &grfcfg->pipe_l0pd_sel, false);
	}

done:
	priv->phy_suspended = false;
	return 0;
}

static int rockchip_combphy_power_off(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	const struct rockchip_combphy_grfcfg *grfcfg;

	if (priv->phy_suspended)
		return 0;

	grfcfg = &priv->cfg->grfcfg;

	if (priv->phy_type == PHY_TYPE_USB3) {
		/*
		 * Check if lane 0 powerdown is already
		 * controlled by grf and in P3 state.
		 */
		if (param_read(priv->combphy_grf,
			       &grfcfg->pipe_l0pd_sel, 1) &&
		    param_read(priv->combphy_grf,
			       &grfcfg->pipe_l0pd_p3, 3))
			goto done;

		/* Exit to P0 */
		param_write(priv->combphy_grf, &grfcfg->pipe_l0pd_p3, false);
		param_write(priv->combphy_grf, &grfcfg->pipe_l0pd_sel, true);
		udelay(1);

		/* Enter to P3 from P0 */
		param_write(priv->combphy_grf, &grfcfg->pipe_l0pd_p3, true);
		udelay(2);

		/*
		 * Disable lane 0 squelch detection.
		 * Note: if squelch detection is disabled,
		 * the PHY can't detect LFPS.
		 */
		param_write(priv->combphy_grf, &grfcfg->pipe_l0rxelec_set,
			    true);

		if (priv->cfg->combphy_low_power_ctrl)
			priv->cfg->combphy_low_power_ctrl(priv, true);
	}

done:
	priv->phy_suspended = true;
	return 0;
}

static int rockchip_combphy_u3_cp_test(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	/*
	 * When do USB3 compliance test, we may connect the oscilloscope
	 * front panel Aux Out to the DUT SSRX+, the Aux Out of the
	 * oscilloscope outputs a negative pulse whose width is between
	 * 300- 400 ns which may trigger some DUTs to change the CP test
	 * pattern.
	 *
	 * The Inno USB3 PHY disable the function to detect the negative
	 * pulse in SSRX+ by default, so we need to enable the function
	 * to toggle the CP test pattern before do USB3 compliance test.
	 */
	if (priv->cfg->combphy_u3_cp_test)
		ret = priv->cfg->combphy_u3_cp_test(priv);

	return ret;
}

static int rockchip_combphy_set_mode(struct phy *phy, enum phy_mode mode)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	u32 reg;

	if (priv->phy_type != PHY_TYPE_PCIE)
		return -EINVAL;

	reg = readl(priv->mmio + 0x21a8);

	if (PHY_MODE_PCIE_EP == mode)
		reg |= (0x1 << 2);
	else if (PHY_MODE_PCIE_RC == mode)
		reg &= ~(0x1 << 2);
	else
		return -EINVAL;

	writel(reg, priv->mmio + 0x21a8);
	return 0;
}

static const struct phy_ops rockchip_combphy_ops = {
	.init		= rockchip_combphy_init,
	.exit		= rockchip_combphy_exit,
	.power_on	= rockchip_combphy_power_on,
	.power_off	= rockchip_combphy_power_off,
	.set_mode       = rockchip_combphy_set_mode,
	.cp_test	= rockchip_combphy_u3_cp_test,
	.owner		= THIS_MODULE,
};

static struct phy *rockchip_combphy_xlate(struct device *dev,
					  struct of_phandle_args *args)
{
	struct rockchip_combphy_priv *priv = dev_get_drvdata(dev);

	if (args->args_count < 1) {
		dev_err(dev, "invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	if (priv->phy_type != PHY_NONE && priv->phy_type != args->args[0]) {
		dev_err(dev, "type select %d overwriting phy type %d\n",
			args->args[0], priv->phy_type);
		return ERR_PTR(-ENODEV);
	}

	priv->phy_type = args->args[0];

	if (priv->phy_type < PHY_TYPE_SATA || priv->phy_type > PHY_TYPE_USB3) {
		dev_err(dev, "invalid phy type select argument\n");
		return ERR_PTR(-EINVAL);
	}

	return priv->phy;
}

static int rockchip_combphy_parse_dt(struct device *dev,
				     struct rockchip_combphy_priv *priv)
{
	u32 i;

	priv->combphy_grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							    "rockchip,combphygrf");
	if (IS_ERR(priv->combphy_grf)) {
		dev_err(dev, "failed to find combphy grf regmap\n");
		return PTR_ERR(priv->combphy_grf);
	}

	priv->usb_pcie_grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							 "rockchip,usbpciegrf");
	if (IS_ERR(priv->usb_pcie_grf)) {
		dev_err(dev, "failed to find usb_pcie_grf regmap\n");
		return PTR_ERR(priv->usb_pcie_grf);
	}

	priv->ref_clk = devm_clk_get(dev, "refclk");
	if (IS_ERR(priv->ref_clk)) {
		dev_err(dev, "failed to find ref clock\n");
		return PTR_ERR(priv->ref_clk);
	}

	for (i = 0; i < PHY_RESET_MAX; i++) {
		priv->rsts[i] = devm_reset_control_get(dev, get_reset_name(i));
		if (IS_ERR(priv->rsts[i])) {
			dev_warn(dev, "no %s reset control specified\n",
				 get_reset_name(i));
			priv->rsts[i] = NULL;
		}
	}

	return 0;
}

static int rockchip_combphy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct rockchip_combphy_priv *priv;
	struct resource *res;
	const struct rockchip_combphy_cfg *phy_cfg;
	int ret;

	phy_cfg = of_device_get_match_data(dev);
	if (!phy_cfg) {
		dev_err(dev, "No OF match data provided\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->mmio)) {
		ret = PTR_ERR(priv->mmio);
		return ret;
	}

	ret = rockchip_combphy_parse_dt(dev, priv);
	if (ret) {
		dev_err(dev, "parse dt failed, ret(%d)\n", ret);
		return ret;
	}

	reset_control_assert(priv->rsts[PHY_POR_RSTN]);
	reset_control_assert(priv->rsts[PHY_APB_RSTN]);
	reset_control_assert(priv->rsts[PHY_PIPE_RSTN]);

	priv->phy_type = PHY_NONE;
	priv->dev = dev;
	priv->cfg = phy_cfg;
	priv->phy = devm_phy_create(dev, NULL, &rockchip_combphy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create combphy\n");
		return PTR_ERR(priv->phy);
	}

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);

	phy_provider = devm_of_phy_provider_register(dev,
						     rockchip_combphy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static int rockchip_combphy_remove(struct platform_device *pdev)
{
	struct rockchip_combphy_priv *priv = platform_get_drvdata(pdev);

	if (priv->phy_type == PHY_TYPE_USB3 && priv->phy_initialized)
		sysfs_remove_group(&priv->dev->kobj,
				   &rockchip_combphy_u3phy_mode_attr_group);

	return 0;
}

static int rk1808_combphy_u3_cp_test(struct rockchip_combphy_priv *priv)
{
	if (priv->phy_type != PHY_TYPE_USB3) {
		dev_err(priv->dev, "failed to set cp test for phy type %d\n",
			priv->phy_type);
		return -EINVAL;
	}

	writel(0x0c, priv->mmio + 0x4008);

	return 0;
}

static int rk1808_combphy_cfg(struct rockchip_combphy_priv *priv)
{
	unsigned long rate;
	u32 reg;
	bool ssc_en = false;

	rate = clk_get_rate(priv->ref_clk);

	/* Configure PHY reference clock frequency */
	switch (rate) {
	case 24000000:
		/*
		 * The default PHY refclk frequency
		 * configuration is 24MHz.
		 */
		break;
	case 25000000:
		writel(0x00, priv->mmio + 0x2118);
		writel(0x64, priv->mmio + 0x211c);
		writel(0x01, priv->mmio + 0x2020);
		writel(0x64, priv->mmio + 0x2028);
		writel(0x21, priv->mmio + 0x2030);

		if (priv->phy_type == PHY_TYPE_PCIE) {
			writel(0x1,  priv->mmio + 0x3020);
			writel(0x64, priv->mmio + 0x3028);
			writel(0x21, priv->mmio + 0x3030);
		}

		break;
	case 50000000:
		writel(0x00, priv->mmio + 0x2118);
		writel(0x32, priv->mmio + 0x211c);
		writel(0x01, priv->mmio + 0x2020);
		writel(0x32, priv->mmio + 0x2028);
		writel(0x21, priv->mmio + 0x2030);
		break;
	default:
		dev_err(priv->dev, "Unsupported rate: %lu\n", rate);
		return -EINVAL;
	}

	if (priv->phy_type == PHY_TYPE_PCIE) {
		/* Adjust Lane 0 Rx interface timing */
		writel(0x20, priv->mmio + 0x20ac);
		writel(0x12, priv->mmio + 0x20c8);
		writel(0x76, priv->mmio + 0x2150);

		/* Adjust Lane 1 Rx interface timing */
		writel(0x20, priv->mmio + 0x30ac);
		writel(0x12, priv->mmio + 0x30c8);
		writel(0x76, priv->mmio + 0x3150);
		/* Set PHY output refclk path */
		writel(0x0, priv->mmio + 0x21a4);
		writel(0x0, priv->mmio + 0x21a8);
		writel(0xb, priv->mmio + 0x21ec);

		/* Physical ordered set for PCIe */
		writel(0x02, priv->mmio + 0x45c0);
		writel(0x83, priv->mmio + 0x45c4);
		writel(0x03, priv->mmio + 0x45c8);
		writel(0x43, priv->mmio + 0x45cc);
		writel(0x00, priv->mmio + 0x45d0);
		writel(0xbc, priv->mmio + 0x45d4);

		/* Boost pre-emphasis */
		writel(0x8b, priv->mmio + 0x21b8);
		writel(0x8b, priv->mmio + 0x31b8);
	} else if (priv->phy_type == PHY_TYPE_USB3) {
		/*
		 * Disable PHY Lane 1 which isn't needed
		 * for USB3 to reduce power consumption.
		 */
		/* Lane 1 cdr power down */
		writel(0x09, priv->mmio + 0x3148);

		/* Lane 1 rx bias disable */
		writel(0x01, priv->mmio + 0x21cc);

		/* Lane 1 cdr disable */
		writel(0x08, priv->mmio + 0x30c4);
		writel(0x08, priv->mmio + 0x20f4);

		/* Lane 1 rx lock disable and tx bias disable */
		writel(0x12, priv->mmio + 0x3150);

		/* Lane 1 rx termination disable, and tx_cmenb disable */
		writel(0x04, priv->mmio + 0x3080);

		/* Lane 1 tx termination disable */
		writel(0x1d, priv->mmio + 0x3090);

		/* Lane 1 tx driver disable */
		writel(0x50, priv->mmio + 0x21c4);
		writel(0x10, priv->mmio + 0x2050);

		/* Lane 1 txldo_refsel disable */
		writel(0x81, priv->mmio + 0x31a8);

		/* Lane 1 txdetrx_en disable */
		writel(0x00, priv->mmio + 0x31e8);

		/* Lane 1 rxcm_en disable */
		writel(0x08, priv->mmio + 0x30c0);

		/* Adjust Lane 0 Rx interface timing */
		writel(0x20, priv->mmio + 0x20ac);

		/* Set and enable SSC */
		switch (rate) {
		case 24000000:
			/* Set SSC rate to 31.25KHz */
			reg = readl(priv->mmio + 0x2108);
			reg = (reg & ~0xf) | 0x1;
			writel(reg, priv->mmio + 0x2108);
			ssc_en = true;
			break;
		case 25000000:
			/* Set SSC rate to 32.55KHz */
			reg = readl(priv->mmio + 0x2108);
			reg = (reg & ~0xf) | 0x6;
			writel(reg, priv->mmio + 0x2108);
			ssc_en = true;
			break;
		default:
			dev_warn(priv->dev,
				 "failed to set SSC on rate: %lu\n", rate);
			break;
		}

		if (ssc_en) {
			/* Enable SSC */
			reg = readl(priv->mmio + 0x2120);
			reg &= ~BIT(4);
			writel(reg, priv->mmio + 0x2120);

			reg = readl(priv->mmio + 0x2000);
			reg &= ~0x6;
			writel(reg, priv->mmio + 0x2000);
		}

		/*
		 * Tuning Tx:
		 * offset 0x21b8 bit[7:4]: lane 0 TX driver swing
		 * tuning bits with weight, "1111" represents the
		 * largest swing and "0000" the smallest.
		 */
		reg = readl(priv->mmio + 0x21b8);
		reg = (reg & ~0xf0) | 0xa0;
		writel(reg, priv->mmio + 0x21b8);

		/*
		 * Tuning Rx for RJTL:
		 * Decrease CDR Chump Bump current.
		 */
		reg = readl(priv->mmio + 0x20c8);
		reg = (reg & ~0x6) | BIT(1);
		writel(reg, priv->mmio + 0x20c8);
		reg = readl(priv->mmio + 0x2150);
		reg |= BIT(2);
		writel(reg, priv->mmio + 0x2150);
	} else {
		dev_err(priv->dev, "failed to cfg incompatible PHY type\n");
		return -EINVAL;
	}

	return 0;
}

static int rk1808_combphy_low_power_control(struct rockchip_combphy_priv *priv,
					    bool en)
{
	if (priv->phy_type != PHY_TYPE_USB3)
		return -EINVAL;

	if (en) {
		/* Lane 0 tx_biasen disable */
		writel(0x36, priv->mmio + 0x2150);

		/* Lane 0 txdetrx_en disable */
		writel(0x02, priv->mmio + 0x21e8);

		/* Lane 0 tx_cmenb disable */
		writel(0x0c, priv->mmio + 0x2080);

		/* Lane 0 rxcm_en disable */
		writel(0x08, priv->mmio + 0x20c0);

		/* Lane 0 and Lane 1 bg_pwrdn */
		writel(0x10, priv->mmio + 0x2044);

		/* Lane 0 and Lane 1 rcomp_osenseampen disable */
		writel(0x08, priv->mmio + 0x2058);

		/* Lane 0 txldo_refsel disable and LDO disable */
		writel(0x91, priv->mmio + 0x21a8);

		/* Lane 1 LDO disable */
		writel(0x91, priv->mmio + 0x31a8);
	} else {
		/* Lane 0 tx_biasen enable */
		writel(0x76, priv->mmio + 0x2150);

		/* Lane 0 txdetrx_en enable */
		writel(0x02, priv->mmio + 0x21e8);

		/* Lane 0 tx_cmenb enable */
		writel(0x08, priv->mmio + 0x2080);

		/* Lane 0 rxcm_en enable */
		writel(0x18, priv->mmio + 0x20c0);

		/* Lane 0 and Lane 1 bg_pwrdn */
		writel(0x00, priv->mmio + 0x2044);

		/* Lane 0 and Lane 1 rcomp_osenseampen enable */
		writel(0x28, priv->mmio + 0x2058);

		/* Lane 0 txldo_refsel enable and LDO enable */
		writel(0x01, priv->mmio + 0x21a8);

		/* Lane 1 LDO enable */
		writel(0x81, priv->mmio + 0x31a8);
	}

	return 0;
}

static const struct rockchip_combphy_cfg rk1808_combphy_cfgs = {
	.grfcfg	= {
		.pipe_l1_sel	= { 0x0000, 15, 11, 0x00, 0x1f },
		.pipe_l1_set	= { 0x0008, 13, 8, 0x00, 0x13 },
		.pipe_l1rxterm_sel = { 0x0000, 12, 12, 0x0, 0x1 },
		.pipe_l1pd_sel	= { 0x0000, 11, 11, 0x0, 0x1},
		.pipe_l1pd_p3	= { 0x0008, 9, 8, 0x0, 0x3 },
		.pipe_l0rxterm_sel = { 0x0000, 7, 7, 0x0, 0x1 },
		.pipe_l0pd_sel	= { 0x0000, 6, 6, 0x0, 0x1 },
		.pipe_l0pd_p3	= { 0x0008, 1, 0, 0x0, 0x3 },
		.pipe_clk_sel	= { 0x0000, 3, 3, 0x0, 0x1 },
		.pipe_clk_set	= { 0x0004, 7, 6, 0x1, 0x0 },
		.pipe_rate_sel	= { 0x0000, 2, 2, 0x0, 0x1 },
		.pipe_rate_set	= { 0x0004, 5, 4, 0x0, 0x1 },
		.pipe_mode_sel	= { 0x0000, 1, 1, 0x0, 0x1 },
		.pipe_mode_set	= { 0x0004, 3, 2, 0x0, 0x1 },
		.pipe_txrx_sel	= { 0x0004, 15, 8, 0x10, 0x2f },
		.pipe_txrx_set	= { 0x0008, 15, 14, 0x0, 0x3 },
		.pipe_l1rxterm_set = { 0x0008, 10, 10, 0x0, 0x1 },
		.pipe_l0rxterm_set = { 0x0008, 2, 2, 0x0, 0x1 },
		.pipe_l0rxelec_set = { 0x0008, 6, 6, 0x0, 0x1 },
		.pipe_width_sel	= { 0x0000, 0, 0, 0x0, 0x1 },
		.pipe_width_set	= { 0x0004, 1, 0, 0x2, 0x0 },
		.pipe_usb3_sel	= { 0x000c, 0, 0, 0x0, 0x1 },
		.pipe_pll_lock	= { 0x0034, 14, 14, 0x0, 0x1 },
		.pipe_status_l0	= { 0x0034, 7, 7, 0x1, 0x0 },
		.u3_port_disable = { 0x0434, 0, 0, 0, 1},
		.u3_port_num	= { 0x0434, 15, 12, 0, 1},
	},
	.combphy_u3_cp_test	= rk1808_combphy_u3_cp_test,
	.combphy_cfg		= rk1808_combphy_cfg,
	.combphy_low_power_ctrl	= rk1808_combphy_low_power_control,
};

static const struct of_device_id rockchip_combphy_of_match[] = {
	{
		.compatible = "rockchip,rk1808-combphy",
		.data = &rk1808_combphy_cfgs,
	},
	{ },
};

MODULE_DEVICE_TABLE(of, rockchip_combphy_of_match);

static struct platform_driver rockchip_combphy_driver = {
	.probe	= rockchip_combphy_probe,
	.remove = rockchip_combphy_remove,
	.driver = {
		.name = "rockchip-combphy",
		.of_match_table = rockchip_combphy_of_match,
	},
};
module_platform_driver(rockchip_combphy_driver);

MODULE_AUTHOR("William Wu <william.wu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB3.0 and PCIE COMBPHY driver");
MODULE_LICENSE("GPL v2");
