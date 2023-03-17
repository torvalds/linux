// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinctrl-mtmips.h"

#define MT7620_GPIO_MODE_UART0_SHIFT	2
#define MT7620_GPIO_MODE_UART0_MASK	0x7
#define MT7620_GPIO_MODE_UART0(x)	((x) << MT7620_GPIO_MODE_UART0_SHIFT)
#define MT7620_GPIO_MODE_UARTF		0x0
#define MT7620_GPIO_MODE_PCM_UARTF	0x1
#define MT7620_GPIO_MODE_PCM_I2S	0x2
#define MT7620_GPIO_MODE_I2S_UARTF	0x3
#define MT7620_GPIO_MODE_PCM_GPIO	0x4
#define MT7620_GPIO_MODE_GPIO_UARTF	0x5
#define MT7620_GPIO_MODE_GPIO_I2S	0x6
#define MT7620_GPIO_MODE_GPIO		0x7

#define MT7620_GPIO_MODE_NAND		0
#define MT7620_GPIO_MODE_SD		1
#define MT7620_GPIO_MODE_ND_SD_GPIO	2
#define MT7620_GPIO_MODE_ND_SD_MASK	0x3
#define MT7620_GPIO_MODE_ND_SD_SHIFT	18

#define MT7620_GPIO_MODE_PCIE_RST	0
#define MT7620_GPIO_MODE_PCIE_REF	1
#define MT7620_GPIO_MODE_PCIE_GPIO	2
#define MT7620_GPIO_MODE_PCIE_MASK	0x3
#define MT7620_GPIO_MODE_PCIE_SHIFT	16

#define MT7620_GPIO_MODE_WDT_RST	0
#define MT7620_GPIO_MODE_WDT_REF	1
#define MT7620_GPIO_MODE_WDT_GPIO	2
#define MT7620_GPIO_MODE_WDT_MASK	0x3
#define MT7620_GPIO_MODE_WDT_SHIFT	21

#define MT7620_GPIO_MODE_MDIO		0
#define MT7620_GPIO_MODE_MDIO_REFCLK	1
#define MT7620_GPIO_MODE_MDIO_GPIO	2
#define MT7620_GPIO_MODE_MDIO_MASK	0x3
#define MT7620_GPIO_MODE_MDIO_SHIFT	7

#define MT7620_GPIO_MODE_I2C		0
#define MT7620_GPIO_MODE_UART1		5
#define MT7620_GPIO_MODE_RGMII1		9
#define MT7620_GPIO_MODE_RGMII2		10
#define MT7620_GPIO_MODE_SPI		11
#define MT7620_GPIO_MODE_SPI_REF_CLK	12
#define MT7620_GPIO_MODE_WLED		13
#define MT7620_GPIO_MODE_JTAG		15
#define MT7620_GPIO_MODE_EPHY		15
#define MT7620_GPIO_MODE_PA		20

static struct mtmips_pmx_func i2c_grp[] =  { FUNC("i2c", 0, 1, 2) };
static struct mtmips_pmx_func spi_grp[] = { FUNC("spi", 0, 3, 4) };
static struct mtmips_pmx_func uartlite_grp[] = { FUNC("uartlite", 0, 15, 2) };
static struct mtmips_pmx_func mdio_grp[] = {
	FUNC("mdio", MT7620_GPIO_MODE_MDIO, 22, 2),
	FUNC("refclk", MT7620_GPIO_MODE_MDIO_REFCLK, 22, 2),
};
static struct mtmips_pmx_func rgmii1_grp[] = { FUNC("rgmii1", 0, 24, 12) };
static struct mtmips_pmx_func refclk_grp[] = { FUNC("spi refclk", 0, 37, 3) };
static struct mtmips_pmx_func ephy_grp[] = { FUNC("ephy", 0, 40, 5) };
static struct mtmips_pmx_func rgmii2_grp[] = { FUNC("rgmii2", 0, 60, 12) };
static struct mtmips_pmx_func wled_grp[] = { FUNC("wled", 0, 72, 1) };
static struct mtmips_pmx_func pa_grp[] = { FUNC("pa", 0, 18, 4) };
static struct mtmips_pmx_func uartf_grp[] = {
	FUNC("uartf", MT7620_GPIO_MODE_UARTF, 7, 8),
	FUNC("pcm uartf", MT7620_GPIO_MODE_PCM_UARTF, 7, 8),
	FUNC("pcm i2s", MT7620_GPIO_MODE_PCM_I2S, 7, 8),
	FUNC("i2s uartf", MT7620_GPIO_MODE_I2S_UARTF, 7, 8),
	FUNC("pcm gpio", MT7620_GPIO_MODE_PCM_GPIO, 11, 4),
	FUNC("gpio uartf", MT7620_GPIO_MODE_GPIO_UARTF, 7, 4),
	FUNC("gpio i2s", MT7620_GPIO_MODE_GPIO_I2S, 7, 4),
};
static struct mtmips_pmx_func wdt_grp[] = {
	FUNC("wdt rst", 0, 17, 1),
	FUNC("wdt refclk", 0, 17, 1),
	};
static struct mtmips_pmx_func pcie_rst_grp[] = {
	FUNC("pcie rst", MT7620_GPIO_MODE_PCIE_RST, 36, 1),
	FUNC("pcie refclk", MT7620_GPIO_MODE_PCIE_REF, 36, 1)
};
static struct mtmips_pmx_func nd_sd_grp[] = {
	FUNC("nand", MT7620_GPIO_MODE_NAND, 45, 15),
	FUNC("sd", MT7620_GPIO_MODE_SD, 47, 13)
};

static struct mtmips_pmx_group mt7620a_pinmux_data[] = {
	GRP("i2c", i2c_grp, 1, MT7620_GPIO_MODE_I2C),
	GRP("uartf", uartf_grp, MT7620_GPIO_MODE_UART0_MASK,
		MT7620_GPIO_MODE_UART0_SHIFT),
	GRP("spi", spi_grp, 1, MT7620_GPIO_MODE_SPI),
	GRP("uartlite", uartlite_grp, 1, MT7620_GPIO_MODE_UART1),
	GRP_G("wdt", wdt_grp, MT7620_GPIO_MODE_WDT_MASK,
		MT7620_GPIO_MODE_WDT_GPIO, MT7620_GPIO_MODE_WDT_SHIFT),
	GRP_G("mdio", mdio_grp, MT7620_GPIO_MODE_MDIO_MASK,
		MT7620_GPIO_MODE_MDIO_GPIO, MT7620_GPIO_MODE_MDIO_SHIFT),
	GRP("rgmii1", rgmii1_grp, 1, MT7620_GPIO_MODE_RGMII1),
	GRP("spi refclk", refclk_grp, 1, MT7620_GPIO_MODE_SPI_REF_CLK),
	GRP_G("pcie", pcie_rst_grp, MT7620_GPIO_MODE_PCIE_MASK,
		MT7620_GPIO_MODE_PCIE_GPIO, MT7620_GPIO_MODE_PCIE_SHIFT),
	GRP_G("nd_sd", nd_sd_grp, MT7620_GPIO_MODE_ND_SD_MASK,
		MT7620_GPIO_MODE_ND_SD_GPIO, MT7620_GPIO_MODE_ND_SD_SHIFT),
	GRP("rgmii2", rgmii2_grp, 1, MT7620_GPIO_MODE_RGMII2),
	GRP("wled", wled_grp, 1, MT7620_GPIO_MODE_WLED),
	GRP("ephy", ephy_grp, 1, MT7620_GPIO_MODE_EPHY),
	GRP("pa", pa_grp, 1, MT7620_GPIO_MODE_PA),
	{ 0 }
};

static int mt7620_pinctrl_probe(struct platform_device *pdev)
{
	return mtmips_pinctrl_init(pdev, mt7620a_pinmux_data);
}

static const struct of_device_id mt7620_pinctrl_match[] = {
	{ .compatible = "ralink,mt7620-pinctrl" },
	{ .compatible = "ralink,rt2880-pinmux" },
	{}
};
MODULE_DEVICE_TABLE(of, mt7620_pinctrl_match);

static struct platform_driver mt7620_pinctrl_driver = {
	.probe = mt7620_pinctrl_probe,
	.driver = {
		.name = "mt7620-pinctrl",
		.of_match_table = mt7620_pinctrl_match,
	},
};

static int __init mt7620_pinctrl_init(void)
{
	return platform_driver_register(&mt7620_pinctrl_driver);
}
core_initcall_sync(mt7620_pinctrl_init);
