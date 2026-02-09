// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
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

#include "pinctrl-utils.h"

#define PIC64GX_PINMUX_REG 0x0

static const struct regmap_config pic64gx_gpio2_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = 0x0,
};

struct pic64gx_gpio2_pinctrl {
	struct pinctrl_dev *pctrl;
	struct device *dev;
	struct regmap *regmap;
	struct pinctrl_desc desc;
};

struct pic64gx_gpio2_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int num_pins;
	u32 mask;
	u32 setting;
};

struct pic64gx_gpio2_function {
	const char *name;
	const char * const *groups;
	const unsigned int num_groups;
};

static const struct pinctrl_pin_desc pic64gx_gpio2_pins[] = {
	PINCTRL_PIN(0, "E14"),
	PINCTRL_PIN(1, "E15"),
	PINCTRL_PIN(2, "F16"),
	PINCTRL_PIN(3, "F17"),
	PINCTRL_PIN(4, "D19"),
	PINCTRL_PIN(5, "B18"),
	PINCTRL_PIN(6, "B10"),
	PINCTRL_PIN(7, "C14"),
	PINCTRL_PIN(8, "E18"),
	PINCTRL_PIN(9, "D18"),
	PINCTRL_PIN(10, "E19"),
	PINCTRL_PIN(11, "C7"),
	PINCTRL_PIN(12, "D6"),
	PINCTRL_PIN(13, "D7"),
	PINCTRL_PIN(14, "C9"),
	PINCTRL_PIN(15, "C10"),
	PINCTRL_PIN(16, "A5"),
	PINCTRL_PIN(17, "A6"),
	PINCTRL_PIN(18, "D8"),
	PINCTRL_PIN(19, "D9"),
	PINCTRL_PIN(20, "B8"),
	PINCTRL_PIN(21, "A8"),
	PINCTRL_PIN(22, "C12"),
	PINCTRL_PIN(23, "B12"),
	PINCTRL_PIN(24, "A11"),
	PINCTRL_PIN(25, "A10"),
	PINCTRL_PIN(26, "D11"),
	PINCTRL_PIN(27, "C11"),
	PINCTRL_PIN(28, "B9"),
};

static const unsigned int pic64gx_gpio2_mdio0_pins[] = {
	0, 1
};

static const unsigned int pic64gx_gpio2_mdio1_pins[] = {
	2, 3
};

static const unsigned int pic64gx_gpio2_spi0_pins[] = {
	4, 5, 10, 11
};

static const unsigned int pic64gx_gpio2_can0_pins[] = {
	6, 24, 28
};

static const unsigned int pic64gx_gpio2_pcie_pins[] = {
	7, 8, 9
};

static const unsigned int pic64gx_gpio2_qspi_pins[] = {
	12, 13, 14, 15, 16, 17
};

static const unsigned int pic64gx_gpio2_uart3_pins[] = {
	18, 19
};

static const unsigned int pic64gx_gpio2_uart4_pins[] = {
	20, 21
};

static const unsigned int pic64gx_gpio2_can1_pins[] = {
	22, 23, 25
};

static const unsigned int pic64gx_gpio2_uart2_pins[] = {
	26, 27
};

#define PIC64GX_PINCTRL_GROUP(_name, _mask) { \
	.name = "gpio_" #_name,	\
	.pins = pic64gx_gpio2_##_name##_pins,	\
	.num_pins = ARRAY_SIZE(pic64gx_gpio2_##_name##_pins), \
	.mask = _mask,	\
	.setting = 0x0,	\
}, { \
	.name = #_name,	\
	.pins = pic64gx_gpio2_##_name##_pins,	\
	.num_pins = ARRAY_SIZE(pic64gx_gpio2_##_name##_pins), \
	.mask = _mask,	\
	.setting = _mask,	\
}

static const struct pic64gx_gpio2_pin_group pic64gx_gpio2_pin_groups[] = {
	PIC64GX_PINCTRL_GROUP(mdio0, BIT(0) | BIT(1)),
	PIC64GX_PINCTRL_GROUP(mdio1, BIT(2) | BIT(3)),
	PIC64GX_PINCTRL_GROUP(spi0, BIT(4) | BIT(5) | BIT(10) | BIT(11)),
	PIC64GX_PINCTRL_GROUP(can0, BIT(6) | BIT(24) | BIT(28)),
	PIC64GX_PINCTRL_GROUP(pcie, BIT(7) | BIT(8) | BIT(9)),
	PIC64GX_PINCTRL_GROUP(qspi, GENMASK(17, 12)),
	PIC64GX_PINCTRL_GROUP(uart3, BIT(18) | BIT(19)),
	PIC64GX_PINCTRL_GROUP(uart4, BIT(20) | BIT(21)),
	PIC64GX_PINCTRL_GROUP(can1, BIT(22) | BIT(23) | BIT(25)),
	PIC64GX_PINCTRL_GROUP(uart2, BIT(26) | BIT(27)),
};

static const char * const pic64gx_gpio2_gpio_groups[] = {
	"gpio_mdio0", "gpio_mdio1", "gpio_spi0", "gpio_can0", "gpio_pcie",
	"gpio_qspi", "gpio_uart3", "gpio_uart4", "gpio_can1", "gpio_uart2"
};

static const char * const pic64gx_gpio2_mdio0_groups[] = {
	"mdio0"
};

static const char * const pic64gx_gpio2_mdio1_groups[] = {
	"mdio1"
};

static const char * const pic64gx_gpio2_spi0_groups[] = {
	"spi0"
};

static const char * const pic64gx_gpio2_can0_groups[] = {
	"can0"
};

static const char * const pic64gx_gpio2_pcie_groups[] = {
	"pcie"
};

static const char * const pic64gx_gpio2_qspi_groups[] = {
	"qspi"
};

static const char * const pic64gx_gpio2_uart3_groups[] = {
	"uart3"
};

static const char * const pic64gx_gpio2_uart4_groups[] = {
	"uart4"
};

static const char * const pic64gx_gpio2_can1_groups[] = {
	"can1"
};

static const char * const pic64gx_gpio2_uart2_groups[] = {
	"uart2"
};

#define PIC64GX_PINCTRL_FUNCTION(_name) { \
	.name = #_name,	\
	.groups = pic64gx_gpio2_##_name##_groups,	\
	.num_groups = ARRAY_SIZE(pic64gx_gpio2_##_name##_groups), \
}

static const struct pic64gx_gpio2_function pic64gx_gpio2_functions[] = {
	PIC64GX_PINCTRL_FUNCTION(gpio),
	PIC64GX_PINCTRL_FUNCTION(mdio0),
	PIC64GX_PINCTRL_FUNCTION(mdio1),
	PIC64GX_PINCTRL_FUNCTION(spi0),
	PIC64GX_PINCTRL_FUNCTION(can0),
	PIC64GX_PINCTRL_FUNCTION(pcie),
	PIC64GX_PINCTRL_FUNCTION(qspi),
	PIC64GX_PINCTRL_FUNCTION(uart3),
	PIC64GX_PINCTRL_FUNCTION(uart4),
	PIC64GX_PINCTRL_FUNCTION(can1),
	PIC64GX_PINCTRL_FUNCTION(uart2),
};

static void pic64gx_gpio2_pin_dbg_show(struct pinctrl_dev *pctrl_dev, struct seq_file *seq,
				       unsigned int pin)
{
	struct pic64gx_gpio2_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 val;

	regmap_read(pctrl->regmap, PIC64GX_PINMUX_REG, &val);
	val = (val & BIT(pin)) >> pin;
	seq_printf(seq, "pin: %u val: %x\n", pin, val);
}

static int pic64gx_gpio2_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pic64gx_gpio2_pin_groups);
}

static const char *pic64gx_gpio2_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return pic64gx_gpio2_pin_groups[selector].name;
}

static int pic64gx_gpio2_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				    const unsigned int **pins, unsigned int *num_pins)
{
	*pins = pic64gx_gpio2_pin_groups[selector].pins;
	*num_pins = pic64gx_gpio2_pin_groups[selector].num_pins;

	return 0;
}

static const struct pinctrl_ops pic64gx_gpio2_pinctrl_ops = {
	.get_groups_count = pic64gx_gpio2_groups_count,
	.get_group_name = pic64gx_gpio2_group_name,
	.get_group_pins = pic64gx_gpio2_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
	.pin_dbg_show = pic64gx_gpio2_pin_dbg_show,
};

static int pic64gx_gpio2_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pic64gx_gpio2_functions);
}

static const char *pic64gx_gpio2_pinmux_get_func_name(struct pinctrl_dev *pctldev,
						      unsigned int selector)
{
	return pic64gx_gpio2_functions[selector].name;
}

static int pic64gx_gpio2_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned int selector,
					   const char * const **groups,
					   unsigned int * const num_groups)
{
	*groups = pic64gx_gpio2_functions[selector].groups;
	*num_groups = pic64gx_gpio2_functions[selector].num_groups;

	return 0;
}

static int pic64gx_gpio2_pinmux_set_mux(struct pinctrl_dev *pctrl_dev, unsigned int fsel,
					unsigned int gsel)
{
	struct pic64gx_gpio2_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	struct device *dev = pctrl->dev;
	const struct pic64gx_gpio2_pin_group *group;
	const struct pic64gx_gpio2_function *function;

	group = &pic64gx_gpio2_pin_groups[gsel];
	function = &pic64gx_gpio2_functions[fsel];

	dev_dbg(dev, "Setting func %s mask %x setting %x\n",
		function->name, group->mask, group->setting);
	regmap_assign_bits(pctrl->regmap, PIC64GX_PINMUX_REG, group->mask, group->setting);

	return 0;
}

static const struct pinmux_ops pic64gx_gpio2_pinmux_ops = {
	.get_functions_count = pic64gx_gpio2_pinmux_get_funcs_count,
	.get_function_name = pic64gx_gpio2_pinmux_get_func_name,
	.get_function_groups = pic64gx_gpio2_pinmux_get_groups,
	.set_mux = pic64gx_gpio2_pinmux_set_mux,
};

static int pic64gx_gpio2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pic64gx_gpio2_pinctrl *pctrl;
	void __iomem *base;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "Failed get resource\n");
		return PTR_ERR(base);
	}

	pctrl->regmap = devm_regmap_init_mmio(dev, base, &pic64gx_gpio2_regmap_config);
	if (IS_ERR(pctrl->regmap)) {
		dev_err(dev, "Failed to map regmap\n");
		return PTR_ERR(pctrl->regmap);
	}

	pctrl->desc.name = dev_name(dev);
	pctrl->desc.pins = pic64gx_gpio2_pins;
	pctrl->desc.npins = ARRAY_SIZE(pic64gx_gpio2_pins);
	pctrl->desc.pctlops = &pic64gx_gpio2_pinctrl_ops;
	pctrl->desc.pmxops = &pic64gx_gpio2_pinmux_ops;
	pctrl->desc.owner = THIS_MODULE;

	pctrl->dev = dev;

	platform_set_drvdata(pdev, pctrl);

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pctrl))
		return PTR_ERR(pctrl->pctrl);

	return 0;
}

static const struct of_device_id pic64gx_gpio2_of_match[] = {
	{ .compatible = "microchip,pic64gx-pinctrl-gpio2" },
	{ }
};
MODULE_DEVICE_TABLE(of, pic64gx_gpio2_of_match);

static struct platform_driver pic64gx_gpio2_driver = {
	.driver = {
		.name = "pic64gx-pinctrl-gpio2",
		.of_match_table = pic64gx_gpio2_of_match,
	},
	.probe = pic64gx_gpio2_probe,
};
module_platform_driver(pic64gx_gpio2_driver);

MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("pic64gx gpio2 pinctrl driver");
MODULE_LICENSE("GPL");
