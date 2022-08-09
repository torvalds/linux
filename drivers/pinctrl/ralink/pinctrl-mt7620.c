// SPDX-License-Identifier: GPL-2.0-only

#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7620.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinmux.h"

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

static struct rt2880_pmx_func i2c_grp[] =  { FUNC("i2c", 0, 1, 2) };
static struct rt2880_pmx_func spi_grp[] = { FUNC("spi", 0, 3, 4) };
static struct rt2880_pmx_func uartlite_grp[] = { FUNC("uartlite", 0, 15, 2) };
static struct rt2880_pmx_func mdio_grp[] = {
	FUNC("mdio", MT7620_GPIO_MODE_MDIO, 22, 2),
	FUNC("refclk", MT7620_GPIO_MODE_MDIO_REFCLK, 22, 2),
};
static struct rt2880_pmx_func rgmii1_grp[] = { FUNC("rgmii1", 0, 24, 12) };
static struct rt2880_pmx_func refclk_grp[] = { FUNC("spi refclk", 0, 37, 3) };
static struct rt2880_pmx_func ephy_grp[] = { FUNC("ephy", 0, 40, 5) };
static struct rt2880_pmx_func rgmii2_grp[] = { FUNC("rgmii2", 0, 60, 12) };
static struct rt2880_pmx_func wled_grp[] = { FUNC("wled", 0, 72, 1) };
static struct rt2880_pmx_func pa_grp[] = { FUNC("pa", 0, 18, 4) };
static struct rt2880_pmx_func uartf_grp[] = {
	FUNC("uartf", MT7620_GPIO_MODE_UARTF, 7, 8),
	FUNC("pcm uartf", MT7620_GPIO_MODE_PCM_UARTF, 7, 8),
	FUNC("pcm i2s", MT7620_GPIO_MODE_PCM_I2S, 7, 8),
	FUNC("i2s uartf", MT7620_GPIO_MODE_I2S_UARTF, 7, 8),
	FUNC("pcm gpio", MT7620_GPIO_MODE_PCM_GPIO, 11, 4),
	FUNC("gpio uartf", MT7620_GPIO_MODE_GPIO_UARTF, 7, 4),
	FUNC("gpio i2s", MT7620_GPIO_MODE_GPIO_I2S, 7, 4),
};
static struct rt2880_pmx_func wdt_grp[] = {
	FUNC("wdt rst", 0, 17, 1),
	FUNC("wdt refclk", 0, 17, 1),
	};
static struct rt2880_pmx_func pcie_rst_grp[] = {
	FUNC("pcie rst", MT7620_GPIO_MODE_PCIE_RST, 36, 1),
	FUNC("pcie refclk", MT7620_GPIO_MODE_PCIE_REF, 36, 1)
};
static struct rt2880_pmx_func nd_sd_grp[] = {
	FUNC("nand", MT7620_GPIO_MODE_NAND, 45, 15),
	FUNC("sd", MT7620_GPIO_MODE_SD, 47, 13)
};

static struct rt2880_pmx_group mt7620a_pinmux_data[] = {
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

static struct rt2880_pmx_func pwm1_grp_mt7628[] = {
	FUNC("sdxc d6", 3, 19, 1),
	FUNC("utif", 2, 19, 1),
	FUNC("gpio", 1, 19, 1),
	FUNC("pwm1", 0, 19, 1),
};

static struct rt2880_pmx_func pwm0_grp_mt7628[] = {
	FUNC("sdxc d7", 3, 18, 1),
	FUNC("utif", 2, 18, 1),
	FUNC("gpio", 1, 18, 1),
	FUNC("pwm0", 0, 18, 1),
};

static struct rt2880_pmx_func uart2_grp_mt7628[] = {
	FUNC("sdxc d5 d4", 3, 20, 2),
	FUNC("pwm", 2, 20, 2),
	FUNC("gpio", 1, 20, 2),
	FUNC("uart2", 0, 20, 2),
};

static struct rt2880_pmx_func uart1_grp_mt7628[] = {
	FUNC("sw_r", 3, 45, 2),
	FUNC("pwm", 2, 45, 2),
	FUNC("gpio", 1, 45, 2),
	FUNC("uart1", 0, 45, 2),
};

static struct rt2880_pmx_func i2c_grp_mt7628[] = {
	FUNC("-", 3, 4, 2),
	FUNC("debug", 2, 4, 2),
	FUNC("gpio", 1, 4, 2),
	FUNC("i2c", 0, 4, 2),
};

static struct rt2880_pmx_func refclk_grp_mt7628[] = { FUNC("refclk", 0, 37, 1) };
static struct rt2880_pmx_func perst_grp_mt7628[] = { FUNC("perst", 0, 36, 1) };
static struct rt2880_pmx_func wdt_grp_mt7628[] = { FUNC("wdt", 0, 38, 1) };
static struct rt2880_pmx_func spi_grp_mt7628[] = { FUNC("spi", 0, 7, 4) };

static struct rt2880_pmx_func sd_mode_grp_mt7628[] = {
	FUNC("jtag", 3, 22, 8),
	FUNC("utif", 2, 22, 8),
	FUNC("gpio", 1, 22, 8),
	FUNC("sdxc", 0, 22, 8),
};

static struct rt2880_pmx_func uart0_grp_mt7628[] = {
	FUNC("-", 3, 12, 2),
	FUNC("-", 2, 12, 2),
	FUNC("gpio", 1, 12, 2),
	FUNC("uart0", 0, 12, 2),
};

static struct rt2880_pmx_func i2s_grp_mt7628[] = {
	FUNC("antenna", 3, 0, 4),
	FUNC("pcm", 2, 0, 4),
	FUNC("gpio", 1, 0, 4),
	FUNC("i2s", 0, 0, 4),
};

static struct rt2880_pmx_func spi_cs1_grp_mt7628[] = {
	FUNC("-", 3, 6, 1),
	FUNC("refclk", 2, 6, 1),
	FUNC("gpio", 1, 6, 1),
	FUNC("spi cs1", 0, 6, 1),
};

static struct rt2880_pmx_func spis_grp_mt7628[] = {
	FUNC("pwm_uart2", 3, 14, 4),
	FUNC("utif", 2, 14, 4),
	FUNC("gpio", 1, 14, 4),
	FUNC("spis", 0, 14, 4),
};

static struct rt2880_pmx_func gpio_grp_mt7628[] = {
	FUNC("pcie", 3, 11, 1),
	FUNC("refclk", 2, 11, 1),
	FUNC("gpio", 1, 11, 1),
	FUNC("gpio", 0, 11, 1),
};

static struct rt2880_pmx_func p4led_kn_grp_mt7628[] = {
	FUNC("jtag", 3, 30, 1),
	FUNC("utif", 2, 30, 1),
	FUNC("gpio", 1, 30, 1),
	FUNC("p4led_kn", 0, 30, 1),
};

static struct rt2880_pmx_func p3led_kn_grp_mt7628[] = {
	FUNC("jtag", 3, 31, 1),
	FUNC("utif", 2, 31, 1),
	FUNC("gpio", 1, 31, 1),
	FUNC("p3led_kn", 0, 31, 1),
};

static struct rt2880_pmx_func p2led_kn_grp_mt7628[] = {
	FUNC("jtag", 3, 32, 1),
	FUNC("utif", 2, 32, 1),
	FUNC("gpio", 1, 32, 1),
	FUNC("p2led_kn", 0, 32, 1),
};

static struct rt2880_pmx_func p1led_kn_grp_mt7628[] = {
	FUNC("jtag", 3, 33, 1),
	FUNC("utif", 2, 33, 1),
	FUNC("gpio", 1, 33, 1),
	FUNC("p1led_kn", 0, 33, 1),
};

static struct rt2880_pmx_func p0led_kn_grp_mt7628[] = {
	FUNC("jtag", 3, 34, 1),
	FUNC("rsvd", 2, 34, 1),
	FUNC("gpio", 1, 34, 1),
	FUNC("p0led_kn", 0, 34, 1),
};

static struct rt2880_pmx_func wled_kn_grp_mt7628[] = {
	FUNC("rsvd", 3, 35, 1),
	FUNC("rsvd", 2, 35, 1),
	FUNC("gpio", 1, 35, 1),
	FUNC("wled_kn", 0, 35, 1),
};

static struct rt2880_pmx_func p4led_an_grp_mt7628[] = {
	FUNC("jtag", 3, 39, 1),
	FUNC("utif", 2, 39, 1),
	FUNC("gpio", 1, 39, 1),
	FUNC("p4led_an", 0, 39, 1),
};

static struct rt2880_pmx_func p3led_an_grp_mt7628[] = {
	FUNC("jtag", 3, 40, 1),
	FUNC("utif", 2, 40, 1),
	FUNC("gpio", 1, 40, 1),
	FUNC("p3led_an", 0, 40, 1),
};

static struct rt2880_pmx_func p2led_an_grp_mt7628[] = {
	FUNC("jtag", 3, 41, 1),
	FUNC("utif", 2, 41, 1),
	FUNC("gpio", 1, 41, 1),
	FUNC("p2led_an", 0, 41, 1),
};

static struct rt2880_pmx_func p1led_an_grp_mt7628[] = {
	FUNC("jtag", 3, 42, 1),
	FUNC("utif", 2, 42, 1),
	FUNC("gpio", 1, 42, 1),
	FUNC("p1led_an", 0, 42, 1),
};

static struct rt2880_pmx_func p0led_an_grp_mt7628[] = {
	FUNC("jtag", 3, 43, 1),
	FUNC("rsvd", 2, 43, 1),
	FUNC("gpio", 1, 43, 1),
	FUNC("p0led_an", 0, 43, 1),
};

static struct rt2880_pmx_func wled_an_grp_mt7628[] = {
	FUNC("rsvd", 3, 44, 1),
	FUNC("rsvd", 2, 44, 1),
	FUNC("gpio", 1, 44, 1),
	FUNC("wled_an", 0, 44, 1),
};

#define MT7628_GPIO_MODE_MASK		0x3

#define MT7628_GPIO_MODE_P4LED_KN	58
#define MT7628_GPIO_MODE_P3LED_KN	56
#define MT7628_GPIO_MODE_P2LED_KN	54
#define MT7628_GPIO_MODE_P1LED_KN	52
#define MT7628_GPIO_MODE_P0LED_KN	50
#define MT7628_GPIO_MODE_WLED_KN	48
#define MT7628_GPIO_MODE_P4LED_AN	42
#define MT7628_GPIO_MODE_P3LED_AN	40
#define MT7628_GPIO_MODE_P2LED_AN	38
#define MT7628_GPIO_MODE_P1LED_AN	36
#define MT7628_GPIO_MODE_P0LED_AN	34
#define MT7628_GPIO_MODE_WLED_AN	32
#define MT7628_GPIO_MODE_PWM1		30
#define MT7628_GPIO_MODE_PWM0		28
#define MT7628_GPIO_MODE_UART2		26
#define MT7628_GPIO_MODE_UART1		24
#define MT7628_GPIO_MODE_I2C		20
#define MT7628_GPIO_MODE_REFCLK		18
#define MT7628_GPIO_MODE_PERST		16
#define MT7628_GPIO_MODE_WDT		14
#define MT7628_GPIO_MODE_SPI		12
#define MT7628_GPIO_MODE_SDMODE		10
#define MT7628_GPIO_MODE_UART0		8
#define MT7628_GPIO_MODE_I2S		6
#define MT7628_GPIO_MODE_CS1		4
#define MT7628_GPIO_MODE_SPIS		2
#define MT7628_GPIO_MODE_GPIO		0

static struct rt2880_pmx_group mt7628an_pinmux_data[] = {
	GRP_G("pwm1", pwm1_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_PWM1),
	GRP_G("pwm0", pwm0_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_PWM0),
	GRP_G("uart2", uart2_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_UART2),
	GRP_G("uart1", uart1_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_UART1),
	GRP_G("i2c", i2c_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_I2C),
	GRP("refclk", refclk_grp_mt7628, 1, MT7628_GPIO_MODE_REFCLK),
	GRP("perst", perst_grp_mt7628, 1, MT7628_GPIO_MODE_PERST),
	GRP("wdt", wdt_grp_mt7628, 1, MT7628_GPIO_MODE_WDT),
	GRP("spi", spi_grp_mt7628, 1, MT7628_GPIO_MODE_SPI),
	GRP_G("sdmode", sd_mode_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_SDMODE),
	GRP_G("uart0", uart0_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_UART0),
	GRP_G("i2s", i2s_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_I2S),
	GRP_G("spi cs1", spi_cs1_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_CS1),
	GRP_G("spis", spis_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_SPIS),
	GRP_G("gpio", gpio_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_GPIO),
	GRP_G("wled_an", wled_an_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_WLED_AN),
	GRP_G("p0led_an", p0led_an_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P0LED_AN),
	GRP_G("p1led_an", p1led_an_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P1LED_AN),
	GRP_G("p2led_an", p2led_an_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P2LED_AN),
	GRP_G("p3led_an", p3led_an_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P3LED_AN),
	GRP_G("p4led_an", p4led_an_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P4LED_AN),
	GRP_G("wled_kn", wled_kn_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_WLED_KN),
	GRP_G("p0led_kn", p0led_kn_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P0LED_KN),
	GRP_G("p1led_kn", p1led_kn_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P1LED_KN),
	GRP_G("p2led_kn", p2led_kn_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P2LED_KN),
	GRP_G("p3led_kn", p3led_kn_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P3LED_KN),
	GRP_G("p4led_kn", p4led_kn_grp_mt7628, MT7628_GPIO_MODE_MASK,
				1, MT7628_GPIO_MODE_P4LED_KN),
	{ 0 }
};

static int mt7620_pinmux_probe(struct platform_device *pdev)
{
	if (is_mt76x8())
		return rt2880_pinmux_init(pdev, mt7628an_pinmux_data);
	else
		return rt2880_pinmux_init(pdev, mt7620a_pinmux_data);
}

static const struct of_device_id mt7620_pinmux_match[] = {
	{ .compatible = "ralink,rt2880-pinmux" },
	{}
};
MODULE_DEVICE_TABLE(of, mt7620_pinmux_match);

static struct platform_driver mt7620_pinmux_driver = {
	.probe = mt7620_pinmux_probe,
	.driver = {
		.name = "rt2880-pinmux",
		.of_match_table = mt7620_pinmux_match,
	},
};

static int __init mt7620_pinmux_init(void)
{
	return platform_driver_register(&mt7620_pinmux_driver);
}
core_initcall_sync(mt7620_pinmux_init);
