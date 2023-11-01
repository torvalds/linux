// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Baylibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mdio-mux.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define ETH_REG2		0x0
#define  REG2_PHYID		GENMASK(21, 0)
#define   EPHY_GXL_ID		0x110181
#define  REG2_LEDACT		GENMASK(23, 22)
#define  REG2_LEDLINK		GENMASK(25, 24)
#define  REG2_DIV4SEL		BIT(27)
#define  REG2_ADCBYPASS		BIT(30)
#define  REG2_CLKINSEL		BIT(31)
#define ETH_REG3		0x4
#define  REG3_ENH		BIT(3)
#define  REG3_CFGMODE		GENMASK(6, 4)
#define  REG3_AUTOMDIX		BIT(7)
#define  REG3_PHYADDR		GENMASK(12, 8)
#define  REG3_PWRUPRST		BIT(21)
#define  REG3_PWRDOWN		BIT(22)
#define  REG3_LEDPOL		BIT(23)
#define  REG3_PHYMDI		BIT(26)
#define  REG3_CLKINEN		BIT(29)
#define  REG3_PHYIP		BIT(30)
#define  REG3_PHYEN		BIT(31)
#define ETH_REG4		0x8
#define  REG4_PWRUPRSTSIG	BIT(0)

#define MESON_GXL_MDIO_EXTERNAL_ID 0
#define MESON_GXL_MDIO_INTERNAL_ID 1

struct gxl_mdio_mux {
	void __iomem *regs;
	void *mux_handle;
};

static void gxl_enable_internal_mdio(struct gxl_mdio_mux *priv)
{
	u32 val;

	/* Setup the internal phy */
	val = (REG3_ENH |
	       FIELD_PREP(REG3_CFGMODE, 0x7) |
	       REG3_AUTOMDIX |
	       FIELD_PREP(REG3_PHYADDR, 8) |
	       REG3_LEDPOL |
	       REG3_PHYMDI |
	       REG3_CLKINEN |
	       REG3_PHYIP);

	writel(REG4_PWRUPRSTSIG, priv->regs + ETH_REG4);
	writel(val, priv->regs + ETH_REG3);
	mdelay(10);

	/* NOTE: The HW kept the phy id configurable at runtime.
	 * The id below is arbitrary. It is the one used in the vendor code.
	 * The only constraint is that it must match the one in
	 * drivers/net/phy/meson-gxl.c to properly match the PHY.
	 */
	writel(FIELD_PREP(REG2_PHYID, EPHY_GXL_ID),
	       priv->regs + ETH_REG2);

	/* Enable the internal phy */
	val |= REG3_PHYEN;
	writel(val, priv->regs + ETH_REG3);
	writel(0, priv->regs + ETH_REG4);

	/* The phy needs a bit of time to power up */
	mdelay(10);
}

static void gxl_enable_external_mdio(struct gxl_mdio_mux *priv)
{
	/* Reset the mdio bus mux to the external phy */
	writel(0, priv->regs + ETH_REG3);
}

static int gxl_mdio_switch_fn(int current_child, int desired_child,
			      void *data)
{
	struct gxl_mdio_mux *priv = dev_get_drvdata(data);

	if (current_child == desired_child)
		return 0;

	switch (desired_child) {
	case MESON_GXL_MDIO_EXTERNAL_ID:
		gxl_enable_external_mdio(priv);
		break;
	case MESON_GXL_MDIO_INTERNAL_ID:
		gxl_enable_internal_mdio(priv);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id gxl_mdio_mux_match[] = {
	{ .compatible = "amlogic,gxl-mdio-mux", },
	{},
};
MODULE_DEVICE_TABLE(of, gxl_mdio_mux_match);

static int gxl_mdio_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gxl_mdio_mux *priv;
	struct clk *rclk;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	rclk = devm_clk_get_enabled(dev, "ref");
	if (IS_ERR(rclk))
		return dev_err_probe(dev, PTR_ERR(rclk),
				     "failed to get reference clock\n");

	ret = mdio_mux_init(dev, dev->of_node, gxl_mdio_switch_fn,
			    &priv->mux_handle, dev, NULL);
	if (ret)
		dev_err_probe(dev, ret, "mdio multiplexer init failed\n");

	return ret;
}

static void gxl_mdio_mux_remove(struct platform_device *pdev)
{
	struct gxl_mdio_mux *priv = platform_get_drvdata(pdev);

	mdio_mux_uninit(priv->mux_handle);
}

static struct platform_driver gxl_mdio_mux_driver = {
	.probe		= gxl_mdio_mux_probe,
	.remove_new	= gxl_mdio_mux_remove,
	.driver		= {
		.name	= "gxl-mdio-mux",
		.of_match_table = gxl_mdio_mux_match,
	},
};
module_platform_driver(gxl_mdio_mux_driver);

MODULE_DESCRIPTION("Amlogic GXL MDIO multiplexer driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
