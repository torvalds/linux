// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96745 pin control driver
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/max96745.h>

#include "core.h"
#include "pinconf.h"
#include "pinmux.h"

struct max96745_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct regmap *regmap;
};

struct max96745_function_data {
	u8 gpio_out_dis:1;
	u8 gpio_io_rx_en:1;
	u8 gpio_tx_en_a:1;
	u8 gpio_tx_en_b:1;
	u8 gpio_rx_en_a:1;
	u8 gpio_rx_en_b:1;
	u8 gpio_tx_id;
	u8 gpio_rx_id;
};

static int max96745_pinmux_set_mux(struct pinctrl_dev *pctldev,
				   unsigned int function, unsigned int group)
{
	struct max96745_pinctrl *mpctl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *grp;
	int i;

	func = pinmux_generic_get_function(pctldev, function);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	if (func->data) {
		struct max96745_function_data *data = func->data;

		for (i = 0; i < grp->num_pins; i++) {
			regmap_update_bits(mpctl->regmap,
					   GPIO_A_REG(grp->pins[i]), GPIO_OUT_DIS,
					   FIELD_PREP(GPIO_OUT_DIS, data->gpio_out_dis));
			if (data->gpio_tx_en_a || data->gpio_tx_en_b)
				regmap_update_bits(mpctl->regmap,
						   GPIO_B_REG(grp->pins[i]),
						   GPIO_TX_ID,
						   FIELD_PREP(GPIO_TX_ID, data->gpio_tx_id));
			if (data->gpio_rx_en_a || data->gpio_rx_en_b)
				regmap_update_bits(mpctl->regmap,
						   GPIO_C_REG(grp->pins[i]),
						   GPIO_RX_ID,
						   FIELD_PREP(GPIO_RX_ID, data->gpio_rx_id));
			regmap_update_bits(mpctl->regmap,
					   GPIO_D_REG(grp->pins[i]),
					   GPIO_TX_EN_A | GPIO_TX_EN_B | GPIO_IO_RX_EN |
					   GPIO_RX_EN_A | GPIO_RX_EN_B,
					   FIELD_PREP(GPIO_TX_EN_A, data->gpio_tx_en_a) |
					   FIELD_PREP(GPIO_TX_EN_B, data->gpio_tx_en_b) |
					   FIELD_PREP(GPIO_RX_EN_A, data->gpio_rx_en_a) |
					   FIELD_PREP(GPIO_RX_EN_B, data->gpio_rx_en_b) |
					   FIELD_PREP(GPIO_IO_RX_EN, data->gpio_io_rx_en));
		}
	}

	return 0;
}

static const struct pinmux_ops max96745_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = max96745_pinmux_set_mux,
};

static const struct pinctrl_ops max96745_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const struct pinctrl_pin_desc max96745_pins_desc[] = {
	PINCTRL_PIN(0, "MFP0"),
	PINCTRL_PIN(1, "MFP1"),
	PINCTRL_PIN(2, "MFP2"),
	PINCTRL_PIN(3, "MFP3"),
	PINCTRL_PIN(4, "MFP4"),
	PINCTRL_PIN(5, "MFP5"),
	PINCTRL_PIN(6, "MFP6"),
	PINCTRL_PIN(7, "MFP7"),
	PINCTRL_PIN(8, "MFP8"),
	PINCTRL_PIN(9, "MFP9"),
	PINCTRL_PIN(10, "MFP10"),
	PINCTRL_PIN(11, "MFP11"),
	PINCTRL_PIN(12, "MFP12"),
	PINCTRL_PIN(13, "MFP13"),
	PINCTRL_PIN(14, "MFP14"),
	PINCTRL_PIN(15, "MFP15"),
	PINCTRL_PIN(16, "MFP16"),
	PINCTRL_PIN(17, "MFP17"),
	PINCTRL_PIN(18, "MFP18"),
	PINCTRL_PIN(19, "MFP19"),
	PINCTRL_PIN(20, "MFP20"),
	PINCTRL_PIN(21, "MFP21"),
	PINCTRL_PIN(22, "MFP22"),
	PINCTRL_PIN(23, "MFP23"),
	PINCTRL_PIN(24, "MFP24"),
	PINCTRL_PIN(25, "MFP25"),
};

static int MFP0_pins[] = {0};
static int MFP1_pins[] = {1};
static int MFP2_pins[] = {2};
static int MFP3_pins[] = {3};
static int MFP4_pins[] = {4};
static int MFP5_pins[] = {5};
static int MFP6_pins[] = {6};
static int MFP7_pins[] = {7};
static int MFP8_pins[] = {8};
static int MFP9_pins[] = {9};
static int MFP10_pins[] = {10};
static int MFP11_pins[] = {11};
static int MFP12_pins[] = {12};
static int MFP13_pins[] = {13};
static int MFP14_pins[] = {14};
static int MFP15_pins[] = {15};
static int MFP16_pins[] = {16};
static int MFP17_pins[] = {17};
static int MFP18_pins[] = {18};
static int MFP19_pins[] = {19};
static int MFP20_pins[] = {20};
static int MFP21_pins[] = {21};
static int MFP22_pins[] = {22};
static int MFP23_pins[] = {23};
static int MFP24_pins[] = {24};
static int MFP25_pins[] = {25};
static int I2C_pins[] = {3, 7};
static int UART_pins[] = {3, 7};

#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

static const struct group_desc max96745_groups[] = {
	GROUP_DESC(MFP0),
	GROUP_DESC(MFP1),
	GROUP_DESC(MFP2),
	GROUP_DESC(MFP3),
	GROUP_DESC(MFP4),
	GROUP_DESC(MFP5),
	GROUP_DESC(MFP6),
	GROUP_DESC(MFP7),
	GROUP_DESC(MFP8),
	GROUP_DESC(MFP9),
	GROUP_DESC(MFP10),
	GROUP_DESC(MFP11),
	GROUP_DESC(MFP12),
	GROUP_DESC(MFP13),
	GROUP_DESC(MFP14),
	GROUP_DESC(MFP15),
	GROUP_DESC(MFP16),
	GROUP_DESC(MFP17),
	GROUP_DESC(MFP18),
	GROUP_DESC(MFP19),
	GROUP_DESC(MFP20),
	GROUP_DESC(MFP21),
	GROUP_DESC(MFP22),
	GROUP_DESC(MFP23),
	GROUP_DESC(MFP24),
	GROUP_DESC(MFP25),
	GROUP_DESC(I2C),
	GROUP_DESC(UART),
};

static const char *MFP_groups[] = {
	"MFP0", "MFP1", "MFP2", "MFP3", "MFP4",
	"MFP5", "MFP6", "MFP7", "MFP8", "MFP9",
	"MFP10", "MFP11", "MFP12", "MFP13", "MFP14",
	"MFP15", "MFP16", "MFP17", "MFP18", "MFP19",
	"MFP20", "MFP21", "MFP22", "MFP23", "MFP24",
	"MFP25",
};
static const char *I2C_groups[] = { "I2C" };
static const char *UART_groups[] = { "UART" };

#define FUNCTION_DESC_GPIO_TX_A(id) \
{ \
	.name = "GPIO_TX_A_"#id, \
	.group_names = MFP_groups, \
	.num_group_names = ARRAY_SIZE(MFP_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en_a = 1, \
		  .gpio_io_rx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_TX_B(id) \
{ \
	.name = "GPIO_TX_B_"#id, \
	.group_names = MFP_groups, \
	.num_group_names = ARRAY_SIZE(MFP_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en_b = 1, \
		  .gpio_io_rx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_RX_A(id) \
{ \
	.name = "GPIO_RX_A_"#id, \
	.group_names = MFP_groups, \
	.num_group_names = ARRAY_SIZE(MFP_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_rx_en_a = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_RX_B(id) \
{ \
	.name = "GPIO_RX_B_"#id, \
	.group_names = MFP_groups, \
	.num_group_names = ARRAY_SIZE(MFP_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_rx_en_b = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO() \
{ \
	.name = "GPIO", \
	.group_names = MFP_groups, \
	.num_group_names = ARRAY_SIZE(MFP_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ } \
	}, \
} \

#define FUNCTION_DESC(nm) \
{ \
	.name = #nm, \
	.group_names = nm##_groups, \
	.num_group_names = ARRAY_SIZE(nm##_groups), \
} \

static const struct function_desc max96745_functions[] = {
	FUNCTION_DESC_GPIO_TX_A(0),
	FUNCTION_DESC_GPIO_TX_A(1),
	FUNCTION_DESC_GPIO_TX_A(2),
	FUNCTION_DESC_GPIO_TX_A(3),
	FUNCTION_DESC_GPIO_TX_A(4),
	FUNCTION_DESC_GPIO_TX_A(5),
	FUNCTION_DESC_GPIO_TX_A(6),
	FUNCTION_DESC_GPIO_TX_A(7),
	FUNCTION_DESC_GPIO_TX_A(8),
	FUNCTION_DESC_GPIO_TX_A(9),
	FUNCTION_DESC_GPIO_TX_A(10),
	FUNCTION_DESC_GPIO_TX_A(11),
	FUNCTION_DESC_GPIO_TX_A(12),
	FUNCTION_DESC_GPIO_TX_A(13),
	FUNCTION_DESC_GPIO_TX_A(14),
	FUNCTION_DESC_GPIO_TX_A(15),
	FUNCTION_DESC_GPIO_TX_A(16),
	FUNCTION_DESC_GPIO_TX_A(17),
	FUNCTION_DESC_GPIO_TX_A(18),
	FUNCTION_DESC_GPIO_TX_A(19),
	FUNCTION_DESC_GPIO_TX_A(20),
	FUNCTION_DESC_GPIO_TX_A(21),
	FUNCTION_DESC_GPIO_TX_A(22),
	FUNCTION_DESC_GPIO_TX_A(23),
	FUNCTION_DESC_GPIO_TX_A(24),
	FUNCTION_DESC_GPIO_TX_A(25),
	FUNCTION_DESC_GPIO_TX_A(26),
	FUNCTION_DESC_GPIO_TX_A(27),
	FUNCTION_DESC_GPIO_TX_A(28),
	FUNCTION_DESC_GPIO_TX_A(29),
	FUNCTION_DESC_GPIO_TX_A(30),
	FUNCTION_DESC_GPIO_TX_A(31),
	FUNCTION_DESC_GPIO_TX_B(0),
	FUNCTION_DESC_GPIO_TX_B(1),
	FUNCTION_DESC_GPIO_TX_B(2),
	FUNCTION_DESC_GPIO_TX_B(3),
	FUNCTION_DESC_GPIO_TX_B(4),
	FUNCTION_DESC_GPIO_TX_B(5),
	FUNCTION_DESC_GPIO_TX_B(6),
	FUNCTION_DESC_GPIO_TX_B(7),
	FUNCTION_DESC_GPIO_TX_B(8),
	FUNCTION_DESC_GPIO_TX_B(9),
	FUNCTION_DESC_GPIO_TX_B(10),
	FUNCTION_DESC_GPIO_TX_B(11),
	FUNCTION_DESC_GPIO_TX_B(12),
	FUNCTION_DESC_GPIO_TX_B(13),
	FUNCTION_DESC_GPIO_TX_B(14),
	FUNCTION_DESC_GPIO_TX_B(15),
	FUNCTION_DESC_GPIO_TX_B(16),
	FUNCTION_DESC_GPIO_TX_B(17),
	FUNCTION_DESC_GPIO_TX_B(18),
	FUNCTION_DESC_GPIO_TX_B(19),
	FUNCTION_DESC_GPIO_TX_B(20),
	FUNCTION_DESC_GPIO_TX_B(21),
	FUNCTION_DESC_GPIO_TX_B(22),
	FUNCTION_DESC_GPIO_TX_B(23),
	FUNCTION_DESC_GPIO_TX_B(24),
	FUNCTION_DESC_GPIO_TX_B(25),
	FUNCTION_DESC_GPIO_TX_B(26),
	FUNCTION_DESC_GPIO_TX_B(27),
	FUNCTION_DESC_GPIO_TX_B(28),
	FUNCTION_DESC_GPIO_TX_B(29),
	FUNCTION_DESC_GPIO_TX_B(30),
	FUNCTION_DESC_GPIO_TX_B(31),
	FUNCTION_DESC_GPIO_RX_A(0),
	FUNCTION_DESC_GPIO_RX_A(1),
	FUNCTION_DESC_GPIO_RX_A(2),
	FUNCTION_DESC_GPIO_RX_A(3),
	FUNCTION_DESC_GPIO_RX_A(4),
	FUNCTION_DESC_GPIO_RX_A(5),
	FUNCTION_DESC_GPIO_RX_A(6),
	FUNCTION_DESC_GPIO_RX_A(7),
	FUNCTION_DESC_GPIO_RX_A(8),
	FUNCTION_DESC_GPIO_RX_A(9),
	FUNCTION_DESC_GPIO_RX_A(10),
	FUNCTION_DESC_GPIO_RX_A(11),
	FUNCTION_DESC_GPIO_RX_A(12),
	FUNCTION_DESC_GPIO_RX_A(13),
	FUNCTION_DESC_GPIO_RX_A(14),
	FUNCTION_DESC_GPIO_RX_A(15),
	FUNCTION_DESC_GPIO_RX_A(16),
	FUNCTION_DESC_GPIO_RX_A(17),
	FUNCTION_DESC_GPIO_RX_A(18),
	FUNCTION_DESC_GPIO_RX_A(19),
	FUNCTION_DESC_GPIO_RX_A(20),
	FUNCTION_DESC_GPIO_RX_A(21),
	FUNCTION_DESC_GPIO_RX_A(22),
	FUNCTION_DESC_GPIO_RX_A(23),
	FUNCTION_DESC_GPIO_RX_A(24),
	FUNCTION_DESC_GPIO_RX_A(25),
	FUNCTION_DESC_GPIO_RX_A(26),
	FUNCTION_DESC_GPIO_RX_A(27),
	FUNCTION_DESC_GPIO_RX_A(28),
	FUNCTION_DESC_GPIO_RX_A(29),
	FUNCTION_DESC_GPIO_RX_A(30),
	FUNCTION_DESC_GPIO_RX_A(31),
	FUNCTION_DESC_GPIO_RX_B(0),
	FUNCTION_DESC_GPIO_RX_B(1),
	FUNCTION_DESC_GPIO_RX_B(2),
	FUNCTION_DESC_GPIO_RX_B(3),
	FUNCTION_DESC_GPIO_RX_B(4),
	FUNCTION_DESC_GPIO_RX_B(5),
	FUNCTION_DESC_GPIO_RX_B(6),
	FUNCTION_DESC_GPIO_RX_B(7),
	FUNCTION_DESC_GPIO_RX_B(8),
	FUNCTION_DESC_GPIO_RX_B(9),
	FUNCTION_DESC_GPIO_RX_B(10),
	FUNCTION_DESC_GPIO_RX_B(11),
	FUNCTION_DESC_GPIO_RX_B(12),
	FUNCTION_DESC_GPIO_RX_B(13),
	FUNCTION_DESC_GPIO_RX_B(14),
	FUNCTION_DESC_GPIO_RX_B(15),
	FUNCTION_DESC_GPIO_RX_B(16),
	FUNCTION_DESC_GPIO_RX_B(17),
	FUNCTION_DESC_GPIO_RX_B(18),
	FUNCTION_DESC_GPIO_RX_B(19),
	FUNCTION_DESC_GPIO_RX_B(20),
	FUNCTION_DESC_GPIO_RX_B(21),
	FUNCTION_DESC_GPIO_RX_B(22),
	FUNCTION_DESC_GPIO_RX_B(23),
	FUNCTION_DESC_GPIO_RX_B(24),
	FUNCTION_DESC_GPIO_RX_B(25),
	FUNCTION_DESC_GPIO_RX_B(26),
	FUNCTION_DESC_GPIO_RX_B(27),
	FUNCTION_DESC_GPIO_RX_B(28),
	FUNCTION_DESC_GPIO_RX_B(29),
	FUNCTION_DESC_GPIO_RX_B(30),
	FUNCTION_DESC_GPIO_RX_B(31),
	FUNCTION_DESC_GPIO(),
	FUNCTION_DESC(I2C),
	FUNCTION_DESC(UART),
};

static int max96745_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96745_pinctrl *mpctl;
	struct pinctrl_desc *pctl_desc;
	int i, ret;

	mpctl = devm_kzalloc(dev, sizeof(*mpctl), GFP_KERNEL);
	if (!mpctl)
		return -ENOMEM;

	mpctl->dev = dev;
	platform_set_drvdata(pdev, mpctl);

	mpctl->regmap = dev_get_regmap(dev->parent, NULL);
	if (!mpctl->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	pctl_desc = devm_kzalloc(dev, sizeof(*pctl_desc), GFP_KERNEL);
	if (!pctl_desc)
		return -ENOMEM;

	pctl_desc->name = dev_name(dev);
	pctl_desc->owner = THIS_MODULE;
	pctl_desc->pctlops = &max96745_pinctrl_ops;
	pctl_desc->pmxops = &max96745_pinmux_ops;
	pctl_desc->pins = max96745_pins_desc;
	pctl_desc->npins = ARRAY_SIZE(max96745_pins_desc);

	ret = devm_pinctrl_register_and_init(dev, pctl_desc, mpctl,
					     &mpctl->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register pinctrl\n");

	for (i = 0; i < ARRAY_SIZE(max96745_groups); i++) {
		const struct group_desc *group = &max96745_groups[i];

		ret = pinctrl_generic_add_group(mpctl->pctl, group->name,
						group->pins, group->num_pins,
						group->data);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to register group %s\n",
					     group->name);
	}

	for (i = 0; i < ARRAY_SIZE(max96745_functions); i++) {
		const struct function_desc *func = &max96745_functions[i];

		ret = pinmux_generic_add_function(mpctl->pctl, func->name,
						  func->group_names,
						  func->num_group_names,
						  func->data);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to register function %s\n",
					     func->name);
	}

	return pinctrl_enable(mpctl->pctl);
}

static const struct of_device_id max96745_pinctrl_of_match[] = {
	{ .compatible = "maxim,max96745-pinctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, max96745_pinctrl_of_match);

static struct platform_driver max96745_pinctrl_driver = {
	.driver = {
		.name = "max96745-pinctrl",
		.of_match_table = max96745_pinctrl_of_match,
	},
	.probe = max96745_pinctrl_probe,
};

module_platform_driver(max96745_pinctrl_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96745 pin control driver");
MODULE_LICENSE("GPL");
