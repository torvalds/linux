// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <linux/of_address.h>

#include "sun8i_dw_hdmi.h"

#define SUN8I_HDMI_PHY_DBG_CTRL_REG	0x0000
#define SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK		BIT(0)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK	GENMASK(15, 8)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL(val)	(val << 8)
#define SUN8I_HDMI_PHY_DBG_CTRL_ADDR_MASK	GENMASK(23, 16)
#define SUN8I_HDMI_PHY_DBG_CTRL_ADDR(addr)	(addr << 16)

#define SUN8I_HDMI_PHY_REXT_CTRL_REG	0x0004
#define SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN	BIT(31)

#define SUN8I_HDMI_PHY_READ_EN_REG	0x0010
#define SUN8I_HDMI_PHY_READ_EN_MAGIC		0x54524545

#define SUN8I_HDMI_PHY_UNSCRAMBLE_REG	0x0014
#define SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC		0x42494E47

/*
 * Address can be actually any value. Here is set to same value as
 * it is set in BSP driver.
 */
#define I2C_ADDR	0x69

static int sun8i_hdmi_phy_config(struct dw_hdmi *hdmi, void *data,
				 struct drm_display_mode *mode)
{
	struct sun8i_hdmi_phy *phy = (struct sun8i_hdmi_phy *)data;
	u32 val = 0;

	if ((mode->flags & DRM_MODE_FLAG_NHSYNC) &&
	    (mode->flags & DRM_MODE_FLAG_NHSYNC)) {
		val = 0x03;
	}

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK,
			   SUN8I_HDMI_PHY_DBG_CTRL_POL(val));

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN);

	/* power down */
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	dw_hdmi_phy_reset(hdmi);

	dw_hdmi_phy_gen2_pddq(hdmi, 0);

	dw_hdmi_phy_i2c_set_addr(hdmi, I2C_ADDR);

	/*
	 * Values are taken from BSP HDMI driver. Although AW didn't
	 * release any documentation, explanation of this values can
	 * be found in i.MX 6Dual/6Quad Reference Manual.
	 */
	if (mode->crtc_clock <= 27000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x01e0, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x08da, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0007, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0318, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8009, 0x09);
	} else if (mode->crtc_clock <= 74250) {
		dw_hdmi_phy_i2c_write(hdmi, 0x0540, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x0005, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0007, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x02b5, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8009, 0x09);
	} else if (mode->crtc_clock <= 148500) {
		dw_hdmi_phy_i2c_write(hdmi, 0x04a0, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x000a, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0002, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0021, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8029, 0x09);
	} else {
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x000f, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0002, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x802b, 0x09);
	}

	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x1e);
	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x13);
	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x17);

	dw_hdmi_phy_gen2_txpwron(hdmi, 1);

	return 0;
};

static void sun8i_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = (struct sun8i_hdmi_phy *)data;

	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN, 0);
}

static const struct dw_hdmi_phy_ops sun8i_hdmi_phy_ops = {
	.init = &sun8i_hdmi_phy_config,
	.disable = &sun8i_hdmi_phy_disable,
	.read_hpd = &dw_hdmi_phy_read_hpd,
	.update_hpd = &dw_hdmi_phy_update_hpd,
	.setup_hpd = &dw_hdmi_phy_setup_hpd,
};

void sun8i_hdmi_phy_init(struct sun8i_hdmi_phy *phy)
{
	/* enable read access to HDMI controller */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_READ_EN_REG,
		     SUN8I_HDMI_PHY_READ_EN_MAGIC);

	/* unscramble register offsets */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_UNSCRAMBLE_REG,
		     SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK,
			   SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK);

	/*
	 * Set PHY I2C address. It must match to the address set by
	 * dw_hdmi_phy_set_slave_addr().
	 */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_ADDR_MASK,
			   SUN8I_HDMI_PHY_DBG_CTRL_ADDR(I2C_ADDR));
}

const struct dw_hdmi_phy_ops *sun8i_hdmi_phy_get_ops(void)
{
	return &sun8i_hdmi_phy_ops;
}

static struct regmap_config sun8i_hdmi_phy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN8I_HDMI_PHY_UNSCRAMBLE_REG,
	.name		= "phy"
};

static const struct of_device_id sun8i_hdmi_phy_of_table[] = {
	{ .compatible = "allwinner,sun8i-a83t-hdmi-phy" },
	{ /* sentinel */ }
};

int sun8i_hdmi_phy_probe(struct sun8i_dw_hdmi *hdmi, struct device_node *node)
{
	struct device *dev = hdmi->dev;
	struct sun8i_hdmi_phy *phy;
	struct resource res;
	void __iomem *regs;
	int ret;

	if (!of_match_node(sun8i_hdmi_phy_of_table, node)) {
		dev_err(dev, "Incompatible HDMI PHY\n");
		return -EINVAL;
	}

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "phy: Couldn't get our resources\n");
		return ret;
	}

	regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(regs)) {
		dev_err(dev, "Couldn't map the HDMI PHY registers\n");
		return PTR_ERR(regs);
	}

	phy->regs = devm_regmap_init_mmio(dev, regs,
					  &sun8i_hdmi_phy_regmap_config);
	if (IS_ERR(phy->regs)) {
		dev_err(dev, "Couldn't create the HDMI PHY regmap\n");
		return PTR_ERR(phy->regs);
	}

	phy->clk_bus = of_clk_get_by_name(node, "bus");
	if (IS_ERR(phy->clk_bus)) {
		dev_err(dev, "Could not get bus clock\n");
		return PTR_ERR(phy->clk_bus);
	}

	phy->clk_mod = of_clk_get_by_name(node, "mod");
	if (IS_ERR(phy->clk_mod)) {
		dev_err(dev, "Could not get mod clock\n");
		ret = PTR_ERR(phy->clk_mod);
		goto err_put_clk_bus;
	}

	phy->rst_phy = of_reset_control_get_shared(node, "phy");
	if (IS_ERR(phy->rst_phy)) {
		dev_err(dev, "Could not get phy reset control\n");
		ret = PTR_ERR(phy->rst_phy);
		goto err_put_clk_mod;
	}

	ret = reset_control_deassert(phy->rst_phy);
	if (ret) {
		dev_err(dev, "Cannot deassert phy reset control: %d\n", ret);
		goto err_put_rst_phy;
	}

	ret = clk_prepare_enable(phy->clk_bus);
	if (ret) {
		dev_err(dev, "Cannot enable bus clock: %d\n", ret);
		goto err_deassert_rst_phy;
	}

	ret = clk_prepare_enable(phy->clk_mod);
	if (ret) {
		dev_err(dev, "Cannot enable mod clock: %d\n", ret);
		goto err_disable_clk_bus;
	}

	hdmi->phy = phy;

	return 0;

err_disable_clk_bus:
	clk_disable_unprepare(phy->clk_bus);
err_deassert_rst_phy:
	reset_control_assert(phy->rst_phy);
err_put_rst_phy:
	reset_control_put(phy->rst_phy);
err_put_clk_mod:
	clk_put(phy->clk_mod);
err_put_clk_bus:
	clk_put(phy->clk_bus);

	return ret;
}

void sun8i_hdmi_phy_remove(struct sun8i_dw_hdmi *hdmi)
{
	struct sun8i_hdmi_phy *phy = hdmi->phy;

	clk_disable_unprepare(phy->clk_mod);
	clk_disable_unprepare(phy->clk_bus);

	reset_control_assert(phy->rst_phy);

	reset_control_put(phy->rst_phy);

	clk_put(phy->clk_mod);
	clk_put(phy->clk_bus);
}
