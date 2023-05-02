// SPDX-License-Identifier: GPL-2.0-only

#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/rt305x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinctrl-mtmips.h"

#define RT305X_GPIO_MODE_UART0_SHIFT	2
#define RT305X_GPIO_MODE_UART0_MASK	0x7
#define RT305X_GPIO_MODE_UART0(x)	((x) << RT305X_GPIO_MODE_UART0_SHIFT)
#define RT305X_GPIO_MODE_UARTF		0
#define RT305X_GPIO_MODE_PCM_UARTF	1
#define RT305X_GPIO_MODE_PCM_I2S	2
#define RT305X_GPIO_MODE_I2S_UARTF	3
#define RT305X_GPIO_MODE_PCM_GPIO	4
#define RT305X_GPIO_MODE_GPIO_UARTF	5
#define RT305X_GPIO_MODE_GPIO_I2S	6
#define RT305X_GPIO_MODE_GPIO		7

#define RT305X_GPIO_MODE_I2C		0
#define RT305X_GPIO_MODE_SPI		1
#define RT305X_GPIO_MODE_UART1		5
#define RT305X_GPIO_MODE_JTAG		6
#define RT305X_GPIO_MODE_MDIO		7
#define RT305X_GPIO_MODE_SDRAM		8
#define RT305X_GPIO_MODE_RGMII		9
#define RT5350_GPIO_MODE_PHY_LED	14
#define RT5350_GPIO_MODE_SPI_CS1	21
#define RT3352_GPIO_MODE_LNA		18
#define RT3352_GPIO_MODE_PA		20

static struct mtmips_pmx_func i2c_grp[] =  { FUNC("i2c", 0, 1, 2) };
static struct mtmips_pmx_func spi_grp[] = { FUNC("spi", 0, 3, 4) };
static struct mtmips_pmx_func uartf_grp[] = {
	FUNC("uartf", RT305X_GPIO_MODE_UARTF, 7, 8),
	FUNC("pcm uartf", RT305X_GPIO_MODE_PCM_UARTF, 7, 8),
	FUNC("pcm i2s", RT305X_GPIO_MODE_PCM_I2S, 7, 8),
	FUNC("i2s uartf", RT305X_GPIO_MODE_I2S_UARTF, 7, 8),
	FUNC("pcm gpio", RT305X_GPIO_MODE_PCM_GPIO, 11, 4),
	FUNC("gpio uartf", RT305X_GPIO_MODE_GPIO_UARTF, 7, 4),
	FUNC("gpio i2s", RT305X_GPIO_MODE_GPIO_I2S, 7, 4),
};
static struct mtmips_pmx_func uartlite_grp[] = { FUNC("uartlite", 0, 15, 2) };
static struct mtmips_pmx_func jtag_grp[] = { FUNC("jtag", 0, 17, 5) };
static struct mtmips_pmx_func mdio_grp[] = { FUNC("mdio", 0, 22, 2) };
static struct mtmips_pmx_func rt5350_led_grp[] = { FUNC("led", 0, 22, 5) };
static struct mtmips_pmx_func rt5350_cs1_grp[] = {
	FUNC("spi_cs1", 0, 27, 1),
	FUNC("wdg_cs1", 1, 27, 1),
};
static struct mtmips_pmx_func sdram_grp[] = { FUNC("sdram", 0, 24, 16) };
static struct mtmips_pmx_func rt3352_rgmii_grp[] = {
	FUNC("rgmii", 0, 24, 12)
};
static struct mtmips_pmx_func rgmii_grp[] = { FUNC("rgmii", 0, 40, 12) };
static struct mtmips_pmx_func rt3352_lna_grp[] = { FUNC("lna", 0, 36, 2) };
static struct mtmips_pmx_func rt3352_pa_grp[] = { FUNC("pa", 0, 38, 2) };
static struct mtmips_pmx_func rt3352_led_grp[] = { FUNC("led", 0, 40, 5) };
static struct mtmips_pmx_func rt3352_cs1_grp[] = {
	FUNC("spi_cs1", 0, 45, 1),
	FUNC("wdg_cs1", 1, 45, 1),
};

static struct mtmips_pmx_group rt3050_pinmux_data[] = {
	GRP("i2c", i2c_grp, 1, RT305X_GPIO_MODE_I2C),
	GRP("spi", spi_grp, 1, RT305X_GPIO_MODE_SPI),
	GRP("uartf", uartf_grp, RT305X_GPIO_MODE_UART0_MASK,
		RT305X_GPIO_MODE_UART0_SHIFT),
	GRP("uartlite", uartlite_grp, 1, RT305X_GPIO_MODE_UART1),
	GRP("jtag", jtag_grp, 1, RT305X_GPIO_MODE_JTAG),
	GRP("mdio", mdio_grp, 1, RT305X_GPIO_MODE_MDIO),
	GRP("rgmii", rgmii_grp, 1, RT305X_GPIO_MODE_RGMII),
	GRP("sdram", sdram_grp, 1, RT305X_GPIO_MODE_SDRAM),
	{ 0 }
};

static struct mtmips_pmx_group rt3352_pinmux_data[] = {
	GRP("i2c", i2c_grp, 1, RT305X_GPIO_MODE_I2C),
	GRP("spi", spi_grp, 1, RT305X_GPIO_MODE_SPI),
	GRP("uartf", uartf_grp, RT305X_GPIO_MODE_UART0_MASK,
		RT305X_GPIO_MODE_UART0_SHIFT),
	GRP("uartlite", uartlite_grp, 1, RT305X_GPIO_MODE_UART1),
	GRP("jtag", jtag_grp, 1, RT305X_GPIO_MODE_JTAG),
	GRP("mdio", mdio_grp, 1, RT305X_GPIO_MODE_MDIO),
	GRP("rgmii", rt3352_rgmii_grp, 1, RT305X_GPIO_MODE_RGMII),
	GRP("lna", rt3352_lna_grp, 1, RT3352_GPIO_MODE_LNA),
	GRP("pa", rt3352_pa_grp, 1, RT3352_GPIO_MODE_PA),
	GRP("led", rt3352_led_grp, 1, RT5350_GPIO_MODE_PHY_LED),
	GRP("spi_cs1", rt3352_cs1_grp, 2, RT5350_GPIO_MODE_SPI_CS1),
	{ 0 }
};

static struct mtmips_pmx_group rt5350_pinmux_data[] = {
	GRP("i2c", i2c_grp, 1, RT305X_GPIO_MODE_I2C),
	GRP("spi", spi_grp, 1, RT305X_GPIO_MODE_SPI),
	GRP("uartf", uartf_grp, RT305X_GPIO_MODE_UART0_MASK,
		RT305X_GPIO_MODE_UART0_SHIFT),
	GRP("uartlite", uartlite_grp, 1, RT305X_GPIO_MODE_UART1),
	GRP("jtag", jtag_grp, 1, RT305X_GPIO_MODE_JTAG),
	GRP("led", rt5350_led_grp, 1, RT5350_GPIO_MODE_PHY_LED),
	GRP("spi_cs1", rt5350_cs1_grp, 2, RT5350_GPIO_MODE_SPI_CS1),
	{ 0 }
};

static int rt305x_pinctrl_probe(struct platform_device *pdev)
{
	if (soc_is_rt5350())
		return mtmips_pinctrl_init(pdev, rt5350_pinmux_data);
	else if (soc_is_rt305x() || soc_is_rt3350())
		return mtmips_pinctrl_init(pdev, rt3050_pinmux_data);
	else if (soc_is_rt3352())
		return mtmips_pinctrl_init(pdev, rt3352_pinmux_data);
	else
		return -EINVAL;
}

static const struct of_device_id rt305x_pinctrl_match[] = {
	{ .compatible = "ralink,rt305x-pinctrl" },
	{ .compatible = "ralink,rt3352-pinctrl" },
	{ .compatible = "ralink,rt5350-pinctrl" },
	{ .compatible = "ralink,rt2880-pinmux" },
	{}
};
MODULE_DEVICE_TABLE(of, rt305x_pinctrl_match);

static struct platform_driver rt305x_pinctrl_driver = {
	.probe = rt305x_pinctrl_probe,
	.driver = {
		.name = "rt305x-pinctrl",
		.of_match_table = rt305x_pinctrl_match,
	},
};

static int __init rt305x_pinctrl_init(void)
{
	return platform_driver_register(&rt305x_pinctrl_driver);
}
core_initcall_sync(rt305x_pinctrl_init);
