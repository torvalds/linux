// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinctrl-utils.h"
#include "pinconf.h"
#include "pinmux.h"

#define MPFS_IOMUX0_REG 0x200

struct mpfs_iomux0_pinctrl {
	struct pinctrl_dev *pctrl;
	struct device *dev;
	struct regmap *regmap;
	struct pinctrl_desc desc;
};

struct mpfs_iomux0_pin_group {
	const char *name;
	const unsigned int *pins;
	u32 mask;
	u32 setting;
};

struct mpfs_iomux0_function {
	const char *name;
	const char * const *groups;
};

static const struct pinctrl_pin_desc mpfs_iomux0_pins[] = {
	PINCTRL_PIN(0, "spi0"),
	PINCTRL_PIN(1, "spi1"),
	PINCTRL_PIN(2, "i2c0"),
	PINCTRL_PIN(3, "i2c1"),
	PINCTRL_PIN(4, "can0"),
	PINCTRL_PIN(5, "can1"),
	PINCTRL_PIN(6, "qspi"),
	PINCTRL_PIN(7, "uart0"),
	PINCTRL_PIN(8, "uart1"),
	PINCTRL_PIN(9, "uart2"),
	PINCTRL_PIN(10, "uart3"),
	PINCTRL_PIN(11, "uart4"),
	PINCTRL_PIN(12, "mdio0"),
	PINCTRL_PIN(13, "mdio1"),
};

static const unsigned int mpfs_iomux0_spi0_pins[] = { 0 };
static const unsigned int mpfs_iomux0_spi1_pins[] = { 1 };
static const unsigned int mpfs_iomux0_i2c0_pins[] = { 2 };
static const unsigned int mpfs_iomux0_i2c1_pins[] = { 3 };
static const unsigned int mpfs_iomux0_can0_pins[] = { 4 };
static const unsigned int mpfs_iomux0_can1_pins[] = { 5 };
static const unsigned int mpfs_iomux0_qspi_pins[] = { 6 };
static const unsigned int mpfs_iomux0_uart0_pins[] = { 7 };
static const unsigned int mpfs_iomux0_uart1_pins[] = { 8 };
static const unsigned int mpfs_iomux0_uart2_pins[] = { 9 };
static const unsigned int mpfs_iomux0_uart3_pins[] = { 10 };
static const unsigned int mpfs_iomux0_uart4_pins[] = { 11 };
static const unsigned int mpfs_iomux0_mdio0_pins[] = { 12 };
static const unsigned int mpfs_iomux0_mdio1_pins[] = { 13 };

#define MPFS_IOMUX0_GROUP(_name, _mask) { \
	.name = #_name "_mssio",	\
	.pins = mpfs_iomux0_##_name##_pins,	\
	.mask = _mask,	\
	.setting = 0x0,	\
}, { \
	.name = #_name "_fabric",	\
	.pins = mpfs_iomux0_##_name##_pins,	\
	.mask = _mask,	\
	.setting = _mask,	\
}

static const struct mpfs_iomux0_pin_group mpfs_iomux0_pin_groups[] = {
	MPFS_IOMUX0_GROUP(spi0, BIT(0)),
	MPFS_IOMUX0_GROUP(spi1, BIT(1)),
	MPFS_IOMUX0_GROUP(i2c0, BIT(2)),
	MPFS_IOMUX0_GROUP(i2c1, BIT(3)),
	MPFS_IOMUX0_GROUP(can0, BIT(4)),
	MPFS_IOMUX0_GROUP(can1, BIT(5)),
	MPFS_IOMUX0_GROUP(qspi, BIT(6)),
	MPFS_IOMUX0_GROUP(uart0, BIT(7)),
	MPFS_IOMUX0_GROUP(uart1, BIT(8)),
	MPFS_IOMUX0_GROUP(uart2, BIT(9)),
	MPFS_IOMUX0_GROUP(uart3, BIT(10)),
	MPFS_IOMUX0_GROUP(uart4, BIT(11)),
	MPFS_IOMUX0_GROUP(mdio0, BIT(12)),
	MPFS_IOMUX0_GROUP(mdio1, BIT(13)),
};

static const char * const mpfs_iomux0_spi0_groups[] = { "spi0_mssio", "spi0_fabric" };
static const char * const mpfs_iomux0_spi1_groups[] = { "spi1_mssio", "spi1_fabric" };
static const char * const mpfs_iomux0_i2c0_groups[] = { "i2c0_mssio", "i2c0_fabric" };
static const char * const mpfs_iomux0_i2c1_groups[] = { "i2c1_mssio", "i2c1_fabric" };
static const char * const mpfs_iomux0_can0_groups[] = { "can0_mssio", "can0_fabric" };
static const char * const mpfs_iomux0_can1_groups[] = { "can1_mssio", "can1_fabric" };
static const char * const mpfs_iomux0_qspi_groups[] = { "qspi_mssio", "qspi_fabric" };
static const char * const mpfs_iomux0_uart0_groups[] = { "uart0_mssio", "uart0_fabric" };
static const char * const mpfs_iomux0_uart1_groups[] = { "uart1_mssio", "uart1_fabric" };
static const char * const mpfs_iomux0_uart2_groups[] = { "uart2_mssio", "uart2_fabric" };
static const char * const mpfs_iomux0_uart3_groups[] = { "uart3_mssio", "uart3_fabric" };
static const char * const mpfs_iomux0_uart4_groups[] = { "uart4_mssio", "uart4_fabric" };
static const char * const mpfs_iomux0_mdio0_groups[] = { "mdio0_mssio", "mdio0_fabric" };
static const char * const mpfs_iomux0_mdio1_groups[] = { "mdio1_mssio", "mdio1_fabric" };

#define MPFS_IOMUX0_FUNCTION(_name) { \
	.name = #_name,	\
	.groups = mpfs_iomux0_##_name##_groups,	\
}

static const struct mpfs_iomux0_function mpfs_iomux0_functions[] = {
	MPFS_IOMUX0_FUNCTION(spi0),
	MPFS_IOMUX0_FUNCTION(spi1),
	MPFS_IOMUX0_FUNCTION(i2c0),
	MPFS_IOMUX0_FUNCTION(i2c1),
	MPFS_IOMUX0_FUNCTION(can0),
	MPFS_IOMUX0_FUNCTION(can1),
	MPFS_IOMUX0_FUNCTION(qspi),
	MPFS_IOMUX0_FUNCTION(uart0),
	MPFS_IOMUX0_FUNCTION(uart1),
	MPFS_IOMUX0_FUNCTION(uart2),
	MPFS_IOMUX0_FUNCTION(uart3),
	MPFS_IOMUX0_FUNCTION(uart4),
	MPFS_IOMUX0_FUNCTION(mdio0),
	MPFS_IOMUX0_FUNCTION(mdio1),
};

static void mpfs_iomux0_pin_dbg_show(struct pinctrl_dev *pctrl_dev, struct seq_file *seq,
				     unsigned int pin)
{
	struct mpfs_iomux0_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 val;

	seq_printf(seq, "reg: %x, pin: %u ", MPFS_IOMUX0_REG, pin);

	regmap_read(pctrl->regmap, MPFS_IOMUX0_REG, &val);
	val = (val & BIT(pin)) >> pin;

	seq_printf(seq, "val: %x\n", val);
}

static int mpfs_iomux0_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(mpfs_iomux0_pin_groups);
}

static const char *mpfs_iomux0_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return mpfs_iomux0_pin_groups[selector].name;
}

static int mpfs_iomux0_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				  const unsigned int **pins, unsigned int *num_pins)
{
	*pins = mpfs_iomux0_pin_groups[selector].pins;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops mpfs_iomux0_pinctrl_ops = {
	.get_groups_count = mpfs_iomux0_groups_count,
	.get_group_name = mpfs_iomux0_group_name,
	.get_group_pins = mpfs_iomux0_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
	.pin_dbg_show = mpfs_iomux0_pin_dbg_show,
};

static int mpfs_iomux0_pinmux_set_mux(struct pinctrl_dev *pctrl_dev, unsigned int fsel,
				      unsigned int gsel)
{
	struct mpfs_iomux0_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	struct device *dev = pctrl->dev;
	const struct mpfs_iomux0_pin_group *group;
	const struct mpfs_iomux0_function *function;

	group = &mpfs_iomux0_pin_groups[gsel];
	function = &mpfs_iomux0_functions[fsel];

	dev_dbg(dev, "Setting func %s mask %x setting %x\n",
		function->name, group->mask, group->setting);
	regmap_assign_bits(pctrl->regmap, MPFS_IOMUX0_REG, group->mask, group->setting);

	return 0;
}

static int mpfs_iomux0_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(mpfs_iomux0_functions);
}

static const char *mpfs_iomux0_pinmux_get_func_name(struct pinctrl_dev *pctldev,
						    unsigned int selector)
{
	return mpfs_iomux0_functions[selector].name;
}

static int mpfs_iomux0_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned int selector,
					 const char * const **groups,
					 unsigned int * const num_groups)
{
	*groups = mpfs_iomux0_functions[selector].groups;
	*num_groups = 2;

	return 0;
}

static const struct pinmux_ops mpfs_iomux0_pinmux_ops = {
	.get_functions_count = mpfs_iomux0_pinmux_get_funcs_count,
	.get_function_name = mpfs_iomux0_pinmux_get_func_name,
	.get_function_groups = mpfs_iomux0_pinmux_get_groups,
	.set_mux = mpfs_iomux0_pinmux_set_mux,
};

static int mpfs_iomux0_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_iomux0_pinctrl *pctrl;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->regmap = device_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(pctrl->regmap))
		dev_err_probe(dev, PTR_ERR(pctrl->regmap), "Failed to find syscon regmap\n");

	pctrl->desc.name = dev_name(dev);
	pctrl->desc.pins = mpfs_iomux0_pins;
	pctrl->desc.npins = ARRAY_SIZE(mpfs_iomux0_pins);
	pctrl->desc.pctlops = &mpfs_iomux0_pinctrl_ops;
	pctrl->desc.pmxops = &mpfs_iomux0_pinmux_ops;
	pctrl->desc.owner = THIS_MODULE;

	pctrl->dev = dev;

	platform_set_drvdata(pdev, pctrl);

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pctrl))
		return PTR_ERR(pctrl->pctrl);

	return 0;
}

static const struct of_device_id mpfs_iomux0_of_match[] = {
	{ .compatible = "microchip,mpfs-pinctrl-iomux0" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_iomux0_of_match);

static struct platform_driver mpfs_iomux0_driver = {
	.driver = {
		.name = "mpfs-pinctrl-iomux0",
		.of_match_table = mpfs_iomux0_of_match,
	},
	.probe = mpfs_iomux0_probe,
};
module_platform_driver(mpfs_iomux0_driver);

MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("Polarfire SoC iomux0 pinctrl driver");
MODULE_LICENSE("GPL");
