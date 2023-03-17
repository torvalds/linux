// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "pinctrl-ralink.h"

#define RT3883_GPIO_MODE_UART0_SHIFT	2
#define RT3883_GPIO_MODE_UART0_MASK	0x7
#define RT3883_GPIO_MODE_UART0(x)	((x) << RT3883_GPIO_MODE_UART0_SHIFT)
#define RT3883_GPIO_MODE_UARTF		0x0
#define RT3883_GPIO_MODE_PCM_UARTF	0x1
#define RT3883_GPIO_MODE_PCM_I2S	0x2
#define RT3883_GPIO_MODE_I2S_UARTF	0x3
#define RT3883_GPIO_MODE_PCM_GPIO	0x4
#define RT3883_GPIO_MODE_GPIO_UARTF	0x5
#define RT3883_GPIO_MODE_GPIO_I2S	0x6
#define RT3883_GPIO_MODE_GPIO		0x7

#define RT3883_GPIO_MODE_I2C		0
#define RT3883_GPIO_MODE_SPI		1
#define RT3883_GPIO_MODE_UART1		5
#define RT3883_GPIO_MODE_JTAG		6
#define RT3883_GPIO_MODE_MDIO		7
#define RT3883_GPIO_MODE_GE1		9
#define RT3883_GPIO_MODE_GE2		10

#define RT3883_GPIO_MODE_PCI_SHIFT	11
#define RT3883_GPIO_MODE_PCI_MASK	0x7
#define RT3883_GPIO_MODE_PCI		(RT3883_GPIO_MODE_PCI_MASK << RT3883_GPIO_MODE_PCI_SHIFT)
#define RT3883_GPIO_MODE_LNA_A_SHIFT	16
#define RT3883_GPIO_MODE_LNA_A_MASK	0x3
#define _RT3883_GPIO_MODE_LNA_A(_x)	((_x) << RT3883_GPIO_MODE_LNA_A_SHIFT)
#define RT3883_GPIO_MODE_LNA_A_GPIO	0x3
#define RT3883_GPIO_MODE_LNA_A		_RT3883_GPIO_MODE_LNA_A(RT3883_GPIO_MODE_LNA_A_MASK)
#define RT3883_GPIO_MODE_LNA_G_SHIFT	18
#define RT3883_GPIO_MODE_LNA_G_MASK	0x3
#define _RT3883_GPIO_MODE_LNA_G(_x)	((_x) << RT3883_GPIO_MODE_LNA_G_SHIFT)
#define RT3883_GPIO_MODE_LNA_G_GPIO	0x3
#define RT3883_GPIO_MODE_LNA_G		_RT3883_GPIO_MODE_LNA_G(RT3883_GPIO_MODE_LNA_G_MASK)

static struct ralink_pmx_func i2c_grp[] =  { FUNC("i2c", 0, 1, 2) };
static struct ralink_pmx_func spi_grp[] = { FUNC("spi", 0, 3, 4) };
static struct ralink_pmx_func uartf_grp[] = {
	FUNC("uartf", RT3883_GPIO_MODE_UARTF, 7, 8),
	FUNC("pcm uartf", RT3883_GPIO_MODE_PCM_UARTF, 7, 8),
	FUNC("pcm i2s", RT3883_GPIO_MODE_PCM_I2S, 7, 8),
	FUNC("i2s uartf", RT3883_GPIO_MODE_I2S_UARTF, 7, 8),
	FUNC("pcm gpio", RT3883_GPIO_MODE_PCM_GPIO, 11, 4),
	FUNC("gpio uartf", RT3883_GPIO_MODE_GPIO_UARTF, 7, 4),
	FUNC("gpio i2s", RT3883_GPIO_MODE_GPIO_I2S, 7, 4),
};
static struct ralink_pmx_func uartlite_grp[] = { FUNC("uartlite", 0, 15, 2) };
static struct ralink_pmx_func jtag_grp[] = { FUNC("jtag", 0, 17, 5) };
static struct ralink_pmx_func mdio_grp[] = { FUNC("mdio", 0, 22, 2) };
static struct ralink_pmx_func lna_a_grp[] = { FUNC("lna a", 0, 32, 3) };
static struct ralink_pmx_func lna_g_grp[] = { FUNC("lna g", 0, 35, 3) };
static struct ralink_pmx_func pci_grp[] = {
	FUNC("pci-dev", 0, 40, 32),
	FUNC("pci-host2", 1, 40, 32),
	FUNC("pci-host1", 2, 40, 32),
	FUNC("pci-fnc", 3, 40, 32)
};
static struct ralink_pmx_func ge1_grp[] = { FUNC("ge1", 0, 72, 12) };
static struct ralink_pmx_func ge2_grp[] = { FUNC("ge2", 0, 84, 12) };

static struct ralink_pmx_group rt3883_pinmux_data[] = {
	GRP("i2c", i2c_grp, 1, RT3883_GPIO_MODE_I2C),
	GRP("spi", spi_grp, 1, RT3883_GPIO_MODE_SPI),
	GRP("uartf", uartf_grp, RT3883_GPIO_MODE_UART0_MASK,
		RT3883_GPIO_MODE_UART0_SHIFT),
	GRP("uartlite", uartlite_grp, 1, RT3883_GPIO_MODE_UART1),
	GRP("jtag", jtag_grp, 1, RT3883_GPIO_MODE_JTAG),
	GRP("mdio", mdio_grp, 1, RT3883_GPIO_MODE_MDIO),
	GRP("lna a", lna_a_grp, 1, RT3883_GPIO_MODE_LNA_A),
	GRP("lna g", lna_g_grp, 1, RT3883_GPIO_MODE_LNA_G),
	GRP("pci", pci_grp, RT3883_GPIO_MODE_PCI_MASK,
		RT3883_GPIO_MODE_PCI_SHIFT),
	GRP("ge1", ge1_grp, 1, RT3883_GPIO_MODE_GE1),
	GRP("ge2", ge2_grp, 1, RT3883_GPIO_MODE_GE2),
	{ 0 }
};

static int rt3883_pinctrl_probe(struct platform_device *pdev)
{
	return ralink_pinctrl_init(pdev, rt3883_pinmux_data);
}

static const struct of_device_id rt3883_pinctrl_match[] = {
	{ .compatible = "ralink,rt3883-pinctrl" },
	{ .compatible = "ralink,rt2880-pinmux" },
	{}
};
MODULE_DEVICE_TABLE(of, rt3883_pinctrl_match);

static struct platform_driver rt3883_pinctrl_driver = {
	.probe = rt3883_pinctrl_probe,
	.driver = {
		.name = "rt3883-pinctrl",
		.of_match_table = rt3883_pinctrl_match,
	},
};

static int __init rt3883_pinctrl_init(void)
{
	return platform_driver_register(&rt3883_pinctrl_driver);
}
core_initcall_sync(rt3883_pinctrl_init);
