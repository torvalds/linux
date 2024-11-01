// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinctrl-ralink.h"

#define RT2880_GPIO_MODE_I2C		BIT(0)
#define RT2880_GPIO_MODE_UART0		BIT(1)
#define RT2880_GPIO_MODE_SPI		BIT(2)
#define RT2880_GPIO_MODE_UART1		BIT(3)
#define RT2880_GPIO_MODE_JTAG		BIT(4)
#define RT2880_GPIO_MODE_MDIO		BIT(5)
#define RT2880_GPIO_MODE_SDRAM		BIT(6)
#define RT2880_GPIO_MODE_PCI		BIT(7)

static struct ralink_pmx_func i2c_func[] = { FUNC("i2c", 0, 1, 2) };
static struct ralink_pmx_func spi_func[] = { FUNC("spi", 0, 3, 4) };
static struct ralink_pmx_func uartlite_func[] = { FUNC("uartlite", 0, 7, 8) };
static struct ralink_pmx_func jtag_func[] = { FUNC("jtag", 0, 17, 5) };
static struct ralink_pmx_func mdio_func[] = { FUNC("mdio", 0, 22, 2) };
static struct ralink_pmx_func sdram_func[] = { FUNC("sdram", 0, 24, 16) };
static struct ralink_pmx_func pci_func[] = { FUNC("pci", 0, 40, 32) };

static struct ralink_pmx_group rt2880_pinmux_data_act[] = {
	GRP("i2c", i2c_func, 1, RT2880_GPIO_MODE_I2C),
	GRP("spi", spi_func, 1, RT2880_GPIO_MODE_SPI),
	GRP("uartlite", uartlite_func, 1, RT2880_GPIO_MODE_UART0),
	GRP("jtag", jtag_func, 1, RT2880_GPIO_MODE_JTAG),
	GRP("mdio", mdio_func, 1, RT2880_GPIO_MODE_MDIO),
	GRP("sdram", sdram_func, 1, RT2880_GPIO_MODE_SDRAM),
	GRP("pci", pci_func, 1, RT2880_GPIO_MODE_PCI),
	{ 0 }
};

static int rt2880_pinctrl_probe(struct platform_device *pdev)
{
	return ralink_pinctrl_init(pdev, rt2880_pinmux_data_act);
}

static const struct of_device_id rt2880_pinctrl_match[] = {
	{ .compatible = "ralink,rt2880-pinctrl" },
	{ .compatible = "ralink,rt2880-pinmux" },
	{}
};
MODULE_DEVICE_TABLE(of, rt2880_pinctrl_match);

static struct platform_driver rt2880_pinctrl_driver = {
	.probe = rt2880_pinctrl_probe,
	.driver = {
		.name = "rt2880-pinctrl",
		.of_match_table = rt2880_pinctrl_match,
	},
};

static int __init rt2880_pinctrl_init(void)
{
	return platform_driver_register(&rt2880_pinctrl_driver);
}
core_initcall_sync(rt2880_pinctrl_init);
