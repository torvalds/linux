// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinctrl-ralink.h"

#define MT7621_GPIO_MODE_UART1		1
#define MT7621_GPIO_MODE_I2C		2
#define MT7621_GPIO_MODE_UART3_MASK	0x3
#define MT7621_GPIO_MODE_UART3_SHIFT	3
#define MT7621_GPIO_MODE_UART3_GPIO	1
#define MT7621_GPIO_MODE_UART2_MASK	0x3
#define MT7621_GPIO_MODE_UART2_SHIFT	5
#define MT7621_GPIO_MODE_UART2_GPIO	1
#define MT7621_GPIO_MODE_JTAG		7
#define MT7621_GPIO_MODE_WDT_MASK	0x3
#define MT7621_GPIO_MODE_WDT_SHIFT	8
#define MT7621_GPIO_MODE_WDT_GPIO	1
#define MT7621_GPIO_MODE_PCIE_RST	0
#define MT7621_GPIO_MODE_PCIE_REF	2
#define MT7621_GPIO_MODE_PCIE_MASK	0x3
#define MT7621_GPIO_MODE_PCIE_SHIFT	10
#define MT7621_GPIO_MODE_PCIE_GPIO	1
#define MT7621_GPIO_MODE_MDIO_MASK	0x3
#define MT7621_GPIO_MODE_MDIO_SHIFT	12
#define MT7621_GPIO_MODE_MDIO_GPIO	1
#define MT7621_GPIO_MODE_RGMII1		14
#define MT7621_GPIO_MODE_RGMII2		15
#define MT7621_GPIO_MODE_SPI_MASK	0x3
#define MT7621_GPIO_MODE_SPI_SHIFT	16
#define MT7621_GPIO_MODE_SPI_GPIO	1
#define MT7621_GPIO_MODE_SDHCI_MASK	0x3
#define MT7621_GPIO_MODE_SDHCI_SHIFT	18
#define MT7621_GPIO_MODE_SDHCI_GPIO	1

static struct ralink_pmx_func uart1_grp[] =  { FUNC("uart1", 0, 1, 2) };
static struct ralink_pmx_func i2c_grp[] =  { FUNC("i2c", 0, 3, 2) };
static struct ralink_pmx_func uart3_grp[] = {
	FUNC("uart3", 0, 5, 4),
	FUNC("i2s", 2, 5, 4),
	FUNC("spdif3", 3, 5, 4),
};
static struct ralink_pmx_func uart2_grp[] = {
	FUNC("uart2", 0, 9, 4),
	FUNC("pcm", 2, 9, 4),
	FUNC("spdif2", 3, 9, 4),
};
static struct ralink_pmx_func jtag_grp[] = { FUNC("jtag", 0, 13, 5) };
static struct ralink_pmx_func wdt_grp[] = {
	FUNC("wdt rst", 0, 18, 1),
	FUNC("wdt refclk", 2, 18, 1),
};
static struct ralink_pmx_func pcie_rst_grp[] = {
	FUNC("pcie rst", MT7621_GPIO_MODE_PCIE_RST, 19, 1),
	FUNC("pcie refclk", MT7621_GPIO_MODE_PCIE_REF, 19, 1)
};
static struct ralink_pmx_func mdio_grp[] = { FUNC("mdio", 0, 20, 2) };
static struct ralink_pmx_func rgmii2_grp[] = { FUNC("rgmii2", 0, 22, 12) };
static struct ralink_pmx_func spi_grp[] = {
	FUNC("spi", 0, 34, 7),
	FUNC("nand1", 2, 34, 7),
};
static struct ralink_pmx_func sdhci_grp[] = {
	FUNC("sdhci", 0, 41, 8),
	FUNC("nand2", 2, 41, 8),
};
static struct ralink_pmx_func rgmii1_grp[] = { FUNC("rgmii1", 0, 49, 12) };

static struct ralink_pmx_group mt7621_pinmux_data[] = {
	GRP("uart1", uart1_grp, 1, MT7621_GPIO_MODE_UART1),
	GRP("i2c", i2c_grp, 1, MT7621_GPIO_MODE_I2C),
	GRP_G("uart3", uart3_grp, MT7621_GPIO_MODE_UART3_MASK,
		MT7621_GPIO_MODE_UART3_GPIO, MT7621_GPIO_MODE_UART3_SHIFT),
	GRP_G("uart2", uart2_grp, MT7621_GPIO_MODE_UART2_MASK,
		MT7621_GPIO_MODE_UART2_GPIO, MT7621_GPIO_MODE_UART2_SHIFT),
	GRP("jtag", jtag_grp, 1, MT7621_GPIO_MODE_JTAG),
	GRP_G("wdt", wdt_grp, MT7621_GPIO_MODE_WDT_MASK,
		MT7621_GPIO_MODE_WDT_GPIO, MT7621_GPIO_MODE_WDT_SHIFT),
	GRP_G("pcie", pcie_rst_grp, MT7621_GPIO_MODE_PCIE_MASK,
		MT7621_GPIO_MODE_PCIE_GPIO, MT7621_GPIO_MODE_PCIE_SHIFT),
	GRP_G("mdio", mdio_grp, MT7621_GPIO_MODE_MDIO_MASK,
		MT7621_GPIO_MODE_MDIO_GPIO, MT7621_GPIO_MODE_MDIO_SHIFT),
	GRP("rgmii2", rgmii2_grp, 1, MT7621_GPIO_MODE_RGMII2),
	GRP_G("spi", spi_grp, MT7621_GPIO_MODE_SPI_MASK,
		MT7621_GPIO_MODE_SPI_GPIO, MT7621_GPIO_MODE_SPI_SHIFT),
	GRP_G("sdhci", sdhci_grp, MT7621_GPIO_MODE_SDHCI_MASK,
		MT7621_GPIO_MODE_SDHCI_GPIO, MT7621_GPIO_MODE_SDHCI_SHIFT),
	GRP("rgmii1", rgmii1_grp, 1, MT7621_GPIO_MODE_RGMII1),
	{ 0 }
};

static int mt7621_pinctrl_probe(struct platform_device *pdev)
{
	return ralink_pinctrl_init(pdev, mt7621_pinmux_data);
}

static const struct of_device_id mt7621_pinctrl_match[] = {
	{ .compatible = "ralink,mt7621-pinctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, mt7621_pinctrl_match);

static struct platform_driver mt7621_pinctrl_driver = {
	.probe = mt7621_pinctrl_probe,
	.driver = {
		.name = "mt7621-pinctrl",
		.of_match_table = mt7621_pinctrl_match,
	},
};

static int __init mt7621_pinctrl_init(void)
{
	return platform_driver_register(&mt7621_pinctrl_driver);
}
core_initcall_sync(mt7621_pinctrl_init);
