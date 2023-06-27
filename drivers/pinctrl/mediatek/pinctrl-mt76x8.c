// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinctrl-mtmips.h"

#define MT76X8_GPIO_MODE_MASK		0x3

#define MT76X8_GPIO_MODE_P4LED_KN	58
#define MT76X8_GPIO_MODE_P3LED_KN	56
#define MT76X8_GPIO_MODE_P2LED_KN	54
#define MT76X8_GPIO_MODE_P1LED_KN	52
#define MT76X8_GPIO_MODE_P0LED_KN	50
#define MT76X8_GPIO_MODE_WLED_KN	48
#define MT76X8_GPIO_MODE_P4LED_AN	42
#define MT76X8_GPIO_MODE_P3LED_AN	40
#define MT76X8_GPIO_MODE_P2LED_AN	38
#define MT76X8_GPIO_MODE_P1LED_AN	36
#define MT76X8_GPIO_MODE_P0LED_AN	34
#define MT76X8_GPIO_MODE_WLED_AN	32
#define MT76X8_GPIO_MODE_PWM1		30
#define MT76X8_GPIO_MODE_PWM0		28
#define MT76X8_GPIO_MODE_UART2		26
#define MT76X8_GPIO_MODE_UART1		24
#define MT76X8_GPIO_MODE_I2C		20
#define MT76X8_GPIO_MODE_REFCLK		18
#define MT76X8_GPIO_MODE_PERST		16
#define MT76X8_GPIO_MODE_WDT		14
#define MT76X8_GPIO_MODE_SPI		12
#define MT76X8_GPIO_MODE_SDMODE		10
#define MT76X8_GPIO_MODE_UART0		8
#define MT76X8_GPIO_MODE_I2S		6
#define MT76X8_GPIO_MODE_CS1		4
#define MT76X8_GPIO_MODE_SPIS		2
#define MT76X8_GPIO_MODE_GPIO		0

static struct mtmips_pmx_func pwm1_grp[] = {
	FUNC("sdxc d6", 3, 19, 1),
	FUNC("utif", 2, 19, 1),
	FUNC("gpio", 1, 19, 1),
	FUNC("pwm1", 0, 19, 1),
};

static struct mtmips_pmx_func pwm0_grp[] = {
	FUNC("sdxc d7", 3, 18, 1),
	FUNC("utif", 2, 18, 1),
	FUNC("gpio", 1, 18, 1),
	FUNC("pwm0", 0, 18, 1),
};

static struct mtmips_pmx_func uart2_grp[] = {
	FUNC("sdxc d5 d4", 3, 20, 2),
	FUNC("pwm", 2, 20, 2),
	FUNC("gpio", 1, 20, 2),
	FUNC("uart2", 0, 20, 2),
};

static struct mtmips_pmx_func uart1_grp[] = {
	FUNC("sw_r", 3, 45, 2),
	FUNC("pwm", 2, 45, 2),
	FUNC("gpio", 1, 45, 2),
	FUNC("uart1", 0, 45, 2),
};

static struct mtmips_pmx_func i2c_grp[] = {
	FUNC("-", 3, 4, 2),
	FUNC("debug", 2, 4, 2),
	FUNC("gpio", 1, 4, 2),
	FUNC("i2c", 0, 4, 2),
};

static struct mtmips_pmx_func refclk_grp[] = { FUNC("refclk", 0, 37, 1) };
static struct mtmips_pmx_func perst_grp[] = { FUNC("perst", 0, 36, 1) };
static struct mtmips_pmx_func wdt_grp[] = { FUNC("wdt", 0, 38, 1) };
static struct mtmips_pmx_func spi_grp[] = { FUNC("spi", 0, 7, 4) };

static struct mtmips_pmx_func sd_mode_grp[] = {
	FUNC("jtag", 3, 22, 8),
	FUNC("utif", 2, 22, 8),
	FUNC("gpio", 1, 22, 8),
	FUNC("sdxc", 0, 22, 8),
};

static struct mtmips_pmx_func uart0_grp[] = {
	FUNC("-", 3, 12, 2),
	FUNC("-", 2, 12, 2),
	FUNC("gpio", 1, 12, 2),
	FUNC("uart0", 0, 12, 2),
};

static struct mtmips_pmx_func i2s_grp[] = {
	FUNC("antenna", 3, 0, 4),
	FUNC("pcm", 2, 0, 4),
	FUNC("gpio", 1, 0, 4),
	FUNC("i2s", 0, 0, 4),
};

static struct mtmips_pmx_func spi_cs1_grp[] = {
	FUNC("-", 3, 6, 1),
	FUNC("refclk", 2, 6, 1),
	FUNC("gpio", 1, 6, 1),
	FUNC("spi cs1", 0, 6, 1),
};

static struct mtmips_pmx_func spis_grp[] = {
	FUNC("pwm_uart2", 3, 14, 4),
	FUNC("utif", 2, 14, 4),
	FUNC("gpio", 1, 14, 4),
	FUNC("spis", 0, 14, 4),
};

static struct mtmips_pmx_func gpio_grp[] = {
	FUNC("pcie", 3, 11, 1),
	FUNC("refclk", 2, 11, 1),
	FUNC("gpio", 1, 11, 1),
	FUNC("gpio", 0, 11, 1),
};

static struct mtmips_pmx_func p4led_kn_grp[] = {
	FUNC("jtag", 3, 30, 1),
	FUNC("utif", 2, 30, 1),
	FUNC("gpio", 1, 30, 1),
	FUNC("p4led_kn", 0, 30, 1),
};

static struct mtmips_pmx_func p3led_kn_grp[] = {
	FUNC("jtag", 3, 31, 1),
	FUNC("utif", 2, 31, 1),
	FUNC("gpio", 1, 31, 1),
	FUNC("p3led_kn", 0, 31, 1),
};

static struct mtmips_pmx_func p2led_kn_grp[] = {
	FUNC("jtag", 3, 32, 1),
	FUNC("utif", 2, 32, 1),
	FUNC("gpio", 1, 32, 1),
	FUNC("p2led_kn", 0, 32, 1),
};

static struct mtmips_pmx_func p1led_kn_grp[] = {
	FUNC("jtag", 3, 33, 1),
	FUNC("utif", 2, 33, 1),
	FUNC("gpio", 1, 33, 1),
	FUNC("p1led_kn", 0, 33, 1),
};

static struct mtmips_pmx_func p0led_kn_grp[] = {
	FUNC("jtag", 3, 34, 1),
	FUNC("rsvd", 2, 34, 1),
	FUNC("gpio", 1, 34, 1),
	FUNC("p0led_kn", 0, 34, 1),
};

static struct mtmips_pmx_func wled_kn_grp[] = {
	FUNC("rsvd", 3, 35, 1),
	FUNC("rsvd", 2, 35, 1),
	FUNC("gpio", 1, 35, 1),
	FUNC("wled_kn", 0, 35, 1),
};

static struct mtmips_pmx_func p4led_an_grp[] = {
	FUNC("jtag", 3, 39, 1),
	FUNC("utif", 2, 39, 1),
	FUNC("gpio", 1, 39, 1),
	FUNC("p4led_an", 0, 39, 1),
};

static struct mtmips_pmx_func p3led_an_grp[] = {
	FUNC("jtag", 3, 40, 1),
	FUNC("utif", 2, 40, 1),
	FUNC("gpio", 1, 40, 1),
	FUNC("p3led_an", 0, 40, 1),
};

static struct mtmips_pmx_func p2led_an_grp[] = {
	FUNC("jtag", 3, 41, 1),
	FUNC("utif", 2, 41, 1),
	FUNC("gpio", 1, 41, 1),
	FUNC("p2led_an", 0, 41, 1),
};

static struct mtmips_pmx_func p1led_an_grp[] = {
	FUNC("jtag", 3, 42, 1),
	FUNC("utif", 2, 42, 1),
	FUNC("gpio", 1, 42, 1),
	FUNC("p1led_an", 0, 42, 1),
};

static struct mtmips_pmx_func p0led_an_grp[] = {
	FUNC("jtag", 3, 43, 1),
	FUNC("rsvd", 2, 43, 1),
	FUNC("gpio", 1, 43, 1),
	FUNC("p0led_an", 0, 43, 1),
};

static struct mtmips_pmx_func wled_an_grp[] = {
	FUNC("rsvd", 3, 44, 1),
	FUNC("rsvd", 2, 44, 1),
	FUNC("gpio", 1, 44, 1),
	FUNC("wled_an", 0, 44, 1),
};

static struct mtmips_pmx_group mt76x8_pinmux_data[] = {
	GRP_G("pwm1", pwm1_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_PWM1),
	GRP_G("pwm0", pwm0_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_PWM0),
	GRP_G("uart2", uart2_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_UART2),
	GRP_G("uart1", uart1_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_UART1),
	GRP_G("i2c", i2c_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_I2C),
	GRP("refclk", refclk_grp, 1, MT76X8_GPIO_MODE_REFCLK),
	GRP("perst", perst_grp, 1, MT76X8_GPIO_MODE_PERST),
	GRP("wdt", wdt_grp, 1, MT76X8_GPIO_MODE_WDT),
	GRP("spi", spi_grp, 1, MT76X8_GPIO_MODE_SPI),
	GRP_G("sdmode", sd_mode_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_SDMODE),
	GRP_G("uart0", uart0_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_UART0),
	GRP_G("i2s", i2s_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_I2S),
	GRP_G("spi cs1", spi_cs1_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_CS1),
	GRP_G("spis", spis_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_SPIS),
	GRP_G("gpio", gpio_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_GPIO),
	GRP_G("wled_an", wled_an_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_WLED_AN),
	GRP_G("p0led_an", p0led_an_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P0LED_AN),
	GRP_G("p1led_an", p1led_an_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P1LED_AN),
	GRP_G("p2led_an", p2led_an_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P2LED_AN),
	GRP_G("p3led_an", p3led_an_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P3LED_AN),
	GRP_G("p4led_an", p4led_an_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P4LED_AN),
	GRP_G("wled_kn", wled_kn_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_WLED_KN),
	GRP_G("p0led_kn", p0led_kn_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P0LED_KN),
	GRP_G("p1led_kn", p1led_kn_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P1LED_KN),
	GRP_G("p2led_kn", p2led_kn_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P2LED_KN),
	GRP_G("p3led_kn", p3led_kn_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P3LED_KN),
	GRP_G("p4led_kn", p4led_kn_grp, MT76X8_GPIO_MODE_MASK,
				1, MT76X8_GPIO_MODE_P4LED_KN),
	{ 0 }
};

static int mt76x8_pinctrl_probe(struct platform_device *pdev)
{
	return mtmips_pinctrl_init(pdev, mt76x8_pinmux_data);
}

static const struct of_device_id mt76x8_pinctrl_match[] = {
	{ .compatible = "ralink,mt76x8-pinctrl" },
	{ .compatible = "ralink,mt7620-pinctrl" },
	{ .compatible = "ralink,rt2880-pinmux" },
	{}
};
MODULE_DEVICE_TABLE(of, mt76x8_pinctrl_match);

static struct platform_driver mt76x8_pinctrl_driver = {
	.probe = mt76x8_pinctrl_probe,
	.driver = {
		.name = "mt76x8-pinctrl",
		.of_match_table = mt76x8_pinctrl_match,
	},
};

static int __init mt76x8_pinctrl_init(void)
{
	return platform_driver_register(&mt76x8_pinctrl_driver);
}
core_initcall_sync(mt76x8_pinctrl_init);
