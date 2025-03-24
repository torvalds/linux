// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Nuvoton Technology Corp.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* USB PHY Miscellaneous Control Register */
#define MA35_SYS_REG_USBPMISCR	0x60
#define PHY0POR			BIT(0)  /* PHY Power-On Reset Control Bit */
#define PHY0SUSPEND			BIT(1)  /* PHY Suspend; 0: suspend, 1: operaion */
#define PHY0COMN			BIT(2)  /* PHY Common Block Power-Down Control */
#define PHY0DEVCKSTB			BIT(10) /* PHY 60 MHz UTMI clock stable bit */

struct ma35_usb_phy {
	struct clk *clk;
	struct device *dev;
	struct regmap *sysreg;
};

static int ma35_usb_phy_power_on(struct phy *phy)
{
	struct ma35_usb_phy *p_phy = phy_get_drvdata(phy);
	unsigned int val;
	int ret;

	ret = clk_prepare_enable(p_phy->clk);
	if (ret < 0) {
		dev_err(p_phy->dev, "Failed to enable PHY clock: %d\n", ret);
		return ret;
	}

	regmap_read(p_phy->sysreg, MA35_SYS_REG_USBPMISCR, &val);
	if (val & PHY0SUSPEND) {
		/*
		 * USB PHY0 is in operation mode already
		 * make sure USB PHY 60 MHz UTMI Interface Clock ready
		 */
		ret = regmap_read_poll_timeout(p_phy->sysreg, MA35_SYS_REG_USBPMISCR, val,
						val & PHY0DEVCKSTB, 10, 1000);
		if (ret == 0)
			return 0;
	}

	/*
	 * reset USB PHY0.
	 * wait until USB PHY0 60 MHz UTMI Interface Clock ready
	 */
	regmap_update_bits(p_phy->sysreg, MA35_SYS_REG_USBPMISCR, 0x7, (PHY0POR | PHY0SUSPEND));
	udelay(20);

	/* make USB PHY0 enter operation mode */
	regmap_update_bits(p_phy->sysreg, MA35_SYS_REG_USBPMISCR, 0x7, PHY0SUSPEND);

	/* make sure USB PHY 60 MHz UTMI Interface Clock ready */
	ret = regmap_read_poll_timeout(p_phy->sysreg, MA35_SYS_REG_USBPMISCR, val,
					val & PHY0DEVCKSTB, 10, 1000);
	if (ret == -ETIMEDOUT) {
		dev_err(p_phy->dev, "Check PHY clock, Timeout: %d\n", ret);
		clk_disable_unprepare(p_phy->clk);
		return ret;
	}

	return 0;
}

static int ma35_usb_phy_power_off(struct phy *phy)
{
	struct ma35_usb_phy *p_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(p_phy->clk);
	return 0;
}

static const struct phy_ops ma35_usb_phy_ops = {
	.power_on = ma35_usb_phy_power_on,
	.power_off = ma35_usb_phy_power_off,
	.owner = THIS_MODULE,
};

static int ma35_usb_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *provider;
	struct ma35_usb_phy *p_phy;
	struct phy *phy;

	p_phy = devm_kzalloc(&pdev->dev, sizeof(*p_phy), GFP_KERNEL);
	if (!p_phy)
		return -ENOMEM;

	p_phy->dev = &pdev->dev;
	platform_set_drvdata(pdev, p_phy);

	p_phy->sysreg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "nuvoton,sys");
	if (IS_ERR(p_phy->sysreg))
		return dev_err_probe(&pdev->dev, PTR_ERR(p_phy->sysreg),
				     "Failed to get SYS registers\n");

	p_phy->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(p_phy->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(p_phy->clk),
				     "failed to find usb_phy clock\n");

	phy = devm_phy_create(&pdev->dev, NULL, &ma35_usb_phy_ops);
	if (IS_ERR(phy))
		return dev_err_probe(&pdev->dev, PTR_ERR(phy), "Failed to create PHY\n");

	phy_set_drvdata(phy, p_phy);

	provider = devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(&pdev->dev, PTR_ERR(provider),
				     "Failed to register PHY provider\n");
	return 0;
}

static const struct of_device_id ma35_usb_phy_of_match[] = {
	{ .compatible = "nuvoton,ma35d1-usb2-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, ma35_usb_phy_of_match);

static struct platform_driver ma35_usb_phy_driver = {
	.probe		= ma35_usb_phy_probe,
	.driver	= {
		.name	= "ma35d1-usb2-phy",
		.of_match_table = ma35_usb_phy_of_match,
	},
};
module_platform_driver(ma35_usb_phy_driver);

MODULE_DESCRIPTION("Nuvoton ma35d1 USB2.0 PHY driver");
MODULE_AUTHOR("Hui-Ping Chen <hpchen0nvt@gmail.com>");
MODULE_LICENSE("GPL");
