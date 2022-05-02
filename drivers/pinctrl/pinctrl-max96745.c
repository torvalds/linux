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

	dev_info(mpctl->dev, "enable function %s group %s\n",
		 func->name, grp->name);

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
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
};

static int gpio0_pins[] = {0};
static int gpio1_pins[] = {1};
static int gpio2_pins[] = {2};
static int gpio3_pins[] = {3};
static int gpio4_pins[] = {4};
static int gpio5_pins[] = {5};
static int gpio6_pins[] = {6};
static int gpio7_pins[] = {7};
static int gpio8_pins[] = {8};
static int gpio9_pins[] = {9};
static int gpio10_pins[] = {10};
static int gpio11_pins[] = {11};
static int gpio12_pins[] = {12};
static int gpio13_pins[] = {13};
static int gpio14_pins[] = {14};
static int gpio15_pins[] = {15};
static int gpio16_pins[] = {16};
static int gpio17_pins[] = {17};
static int gpio18_pins[] = {18};
static int gpio19_pins[] = {19};
static int gpio20_pins[] = {20};
static int gpio21_pins[] = {21};
static int gpio22_pins[] = {22};
static int gpio23_pins[] = {23};
static int gpio24_pins[] = {24};
static int gpio25_pins[] = {25};
static int i2c_pins[] = {3, 7};
static int uart_pins[] = {3, 7};

#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

static const struct group_desc max96745_groups[] = {
	GROUP_DESC(gpio0),
	GROUP_DESC(gpio1),
	GROUP_DESC(gpio2),
	GROUP_DESC(gpio3),
	GROUP_DESC(gpio4),
	GROUP_DESC(gpio5),
	GROUP_DESC(gpio6),
	GROUP_DESC(gpio7),
	GROUP_DESC(gpio8),
	GROUP_DESC(gpio9),
	GROUP_DESC(gpio10),
	GROUP_DESC(gpio11),
	GROUP_DESC(gpio12),
	GROUP_DESC(gpio13),
	GROUP_DESC(gpio14),
	GROUP_DESC(gpio15),
	GROUP_DESC(gpio16),
	GROUP_DESC(gpio17),
	GROUP_DESC(gpio18),
	GROUP_DESC(gpio19),
	GROUP_DESC(gpio20),
	GROUP_DESC(gpio21),
	GROUP_DESC(gpio22),
	GROUP_DESC(gpio23),
	GROUP_DESC(gpio24),
	GROUP_DESC(gpio25),
	GROUP_DESC(i2c),
	GROUP_DESC(uart),
};

static const char *gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4",
	"gpio5", "gpio6", "gpio7", "gpio8", "gpio9",
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19",
	"gpio20", "gpio21", "gpio22", "gpio23", "gpio24",
	"gpio25",
};
static const char *i2c_groups[] = { "i2c" };
static const char *uart_groups[] = { "uart" };

#define FUNCTION_DESC_GPIO_TX_A(id) \
{ \
	.name = "GPIO_TX_A_"#id, \
	.group_names = gpio_groups, \
	.num_group_names = ARRAY_SIZE(gpio_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en_a = 1, \
		  .gpio_io_rx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_TX_B(id) \
{ \
	.name = "GPIO_TX_B_"#id, \
	.group_names = gpio_groups, \
	.num_group_names = ARRAY_SIZE(gpio_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en_b = 1, \
		  .gpio_io_rx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_RX_A(id) \
{ \
	.name = "GPIO_RX_A_"#id, \
	.group_names = gpio_groups, \
	.num_group_names = ARRAY_SIZE(gpio_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_rx_en_a = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_RX_B(id) \
{ \
	.name = "GPIO_RX_B_"#id, \
	.group_names = gpio_groups, \
	.num_group_names = ARRAY_SIZE(gpio_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ .gpio_rx_en_b = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO() \
{ \
	.name = "GPIO", \
	.group_names = gpio_groups, \
	.num_group_names = ARRAY_SIZE(gpio_groups), \
	.data = (void *)(const struct max96745_function_data []) { \
		{ } \
	}, \
} \

#define FUNCTION_DESC(fname, gname) \
{ \
	.name = #fname, \
	.group_names = gname##_groups, \
	.num_group_names = ARRAY_SIZE(gname##_groups), \
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
	FUNCTION_DESC(I2C, i2c),
	FUNCTION_DESC(UART, uart),
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
