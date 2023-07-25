// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * maxim-nca9539.c  --  I2C register interface access for nca9539 chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "../core.h"

#define REG_NCA9539_INPUT_PORT0 0x00 //Read only default 1111 1111
#define REG_NCA9539_INPUT_PORT1 0x01 //Read only default 1111 1111
#define REG_NCA9539_OUT_LEVEL_PORT0 0x02 //Read/write default 1111 1111
#define REG_NCA9539_OUT_LEVEL_PORT1 0x03 //Read/write default 1111 1111
#define REG_NCA9539_INVER_PORT0 0x04 //Read/write default 0000 0000
#define REG_NCA9539_INVER_PORT1 0x05 //Read/write default 0000 0000
#define REG_NCA9539_DIR_PORT0 0x06 //Read/write byte 1111 1111
#define REG_NCA9539_DIR_PORT1 0x07 //Read/write byte 1111 1111

static bool nca9539_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static struct regmap_config nca9539_regmap_config = {
	.name = "nca9539",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0007,
	.volatile_reg = nca9539_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static int NCA9539_GPIO0_pins[] = {0};
static int NCA9539_GPIO1_pins[] = {1};
static int NCA9539_GPIO2_pins[] = {2};
static int NCA9539_GPIO3_pins[] = {3};
static int NCA9539_GPIO4_pins[] = {4};
static int NCA9539_GPIO5_pins[] = {5};
static int NCA9539_GPIO6_pins[] = {6};
static int NCA9539_GPIO7_pins[] = {7};

static int NCA9539_GPIO8_pins[] = {8};
static int NCA9539_GPIO9_pins[] = {9};
static int NCA9539_GPIO10_pins[] = {10};
static int NCA9539_GPIO11_pins[] = {11};
static int NCA9539_GPIO12_pins[] = {12};
static int NCA9539_GPIO13_pins[] = {13};
static int NCA9539_GPIO14_pins[] = {14};
static int NCA9539_GPIO15_pins[] = {15};


#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

static const char *serdes_gpio_groups[] = {
	"NCA9539_GPIO0", "NCA9539_GPIO1", "NCA9539_GPIO2", "NCA9539_GPIO3",
	"NCA9539_GPIO4", "NCA9539_GPIO5", "NCA9539_GPIO6", "NCA9539_GPIO7",
	"NCA9539_GPIO8", "NCA9539_GPIO9", "NCA9539_GPIO10", "NCA9539_GPIO11",
	"NCA9539_GPIO12", "NCA9539_GPIO13", "NCA9539_GPIO14", "NCA9539_GPIO15",
};

/*des -> ser -> soc*/
#define FUNCTION_DESC_GPIO_INPUT(id) \
{ \
	.name = "GPIO"#id"_INPUT", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
} \

/*soc -> ser -> des*/
#define FUNCTION_DESC_GPIO_OUTPUT(id) \
{ \
	.name = "GPIO"#id"_OUTPUT", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
} \


static struct pinctrl_pin_desc nca9539_pins_desc[] = {
	PINCTRL_PIN(NOVO_NCA9539_GPIO0, "NCA9539_GPIO0"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO1, "NCA9539_GPIO1"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO2, "NCA9539_GPIO2"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO3, "NCA9539_GPIO3"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO4, "NCA9539_GPIO4"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO5, "NCA9539_GPIO5"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO6, "NCA9539_GPIO6"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO7, "NCA9539_GPIO7"),

	PINCTRL_PIN(NOVO_NCA9539_GPIO8, "NCA9539_GPIO8"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO9, "NCA9539_GPIO9"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO10, "NCA9539_GPIO10"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO11, "NCA9539_GPIO11"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO12, "NCA9539_GPIO12"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO13, "NCA9539_GPIO13"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO14, "NCA9539_GPIO14"),
	PINCTRL_PIN(NOVO_NCA9539_GPIO15, "NCA9539_GPIO15"),
};

static struct group_desc nca9539_groups_desc[] = {
	GROUP_DESC(NCA9539_GPIO0),
	GROUP_DESC(NCA9539_GPIO1),
	GROUP_DESC(NCA9539_GPIO2),
	GROUP_DESC(NCA9539_GPIO3),
	GROUP_DESC(NCA9539_GPIO4),
	GROUP_DESC(NCA9539_GPIO5),
	GROUP_DESC(NCA9539_GPIO6),
	GROUP_DESC(NCA9539_GPIO7),

	GROUP_DESC(NCA9539_GPIO8),
	GROUP_DESC(NCA9539_GPIO9),
	GROUP_DESC(NCA9539_GPIO10),
	GROUP_DESC(NCA9539_GPIO11),
	GROUP_DESC(NCA9539_GPIO12),
	GROUP_DESC(NCA9539_GPIO13),
	GROUP_DESC(NCA9539_GPIO14),
	GROUP_DESC(NCA9539_GPIO15),
};

static struct function_desc nca9539_functions_desc[] = {
	FUNCTION_DESC_GPIO_INPUT(0),
	FUNCTION_DESC_GPIO_INPUT(1),
	FUNCTION_DESC_GPIO_INPUT(2),
	FUNCTION_DESC_GPIO_INPUT(3),
	FUNCTION_DESC_GPIO_INPUT(4),
	FUNCTION_DESC_GPIO_INPUT(5),
	FUNCTION_DESC_GPIO_INPUT(6),
	FUNCTION_DESC_GPIO_INPUT(7),
	FUNCTION_DESC_GPIO_INPUT(8),
	FUNCTION_DESC_GPIO_INPUT(9),
	FUNCTION_DESC_GPIO_INPUT(10),
	FUNCTION_DESC_GPIO_INPUT(11),
	FUNCTION_DESC_GPIO_INPUT(12),
	FUNCTION_DESC_GPIO_INPUT(13),
	FUNCTION_DESC_GPIO_INPUT(14),
	FUNCTION_DESC_GPIO_INPUT(15),

	FUNCTION_DESC_GPIO_OUTPUT(0),
	FUNCTION_DESC_GPIO_OUTPUT(1),
	FUNCTION_DESC_GPIO_OUTPUT(2),
	FUNCTION_DESC_GPIO_OUTPUT(3),
	FUNCTION_DESC_GPIO_OUTPUT(4),
	FUNCTION_DESC_GPIO_OUTPUT(5),
	FUNCTION_DESC_GPIO_OUTPUT(6),
	FUNCTION_DESC_GPIO_OUTPUT(7),
	FUNCTION_DESC_GPIO_OUTPUT(8),
	FUNCTION_DESC_GPIO_OUTPUT(9),
	FUNCTION_DESC_GPIO_OUTPUT(10),
	FUNCTION_DESC_GPIO_OUTPUT(11),
	FUNCTION_DESC_GPIO_OUTPUT(12),
	FUNCTION_DESC_GPIO_OUTPUT(13),
	FUNCTION_DESC_GPIO_OUTPUT(14),
	FUNCTION_DESC_GPIO_OUTPUT(15),
};

static struct serdes_chip_pinctrl_info nca9539_pinctrl_info = {
	.pins = nca9539_pins_desc,
	.num_pins = ARRAY_SIZE(nca9539_pins_desc),
	.groups = nca9539_groups_desc,
	.num_groups = ARRAY_SIZE(nca9539_groups_desc),
	.functions = nca9539_functions_desc,
	.num_functions = ARRAY_SIZE(nca9539_functions_desc),
};

static int nca9539_pinctrl_config_get(struct serdes *serdes,
				      unsigned int pin, unsigned long *config)
{
	return 0;
}

static int nca9539_pinctrl_config_set(struct serdes *serdes,
				      unsigned int pin, unsigned long *configs,
				      unsigned int num_configs)
{
	return 0;
}

static int nca9539_pinctrl_set_mux(struct serdes *serdes,
				   unsigned int function, unsigned int group)
{
	return 0;
}

static struct serdes_chip_pinctrl_ops nca9539_pinctrl_ops = {
	.pin_config_get = nca9539_pinctrl_config_get,
	.pin_config_set = nca9539_pinctrl_config_set,
	.set_mux = nca9539_pinctrl_set_mux,
};

static int nca9539_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int nca9539_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	struct device *dev = serdes->dev;
	unsigned char port, pin, dir_reg, level_reg;
	int ret1 = 0, ret2 = 0;

	port = (gpio / 8) & 0x01;
	pin = (gpio % 8) & 0x7;
	dir_reg = (port == 0) ? REG_NCA9539_DIR_PORT0 : REG_NCA9539_DIR_PORT1;
	level_reg = (port == 0) ? REG_NCA9539_OUT_LEVEL_PORT0 : REG_NCA9539_OUT_LEVEL_PORT1;

	switch (pin) {
	case 0:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(0), FIELD_PREP(BIT(0), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(0), FIELD_PREP(BIT(0), value & 0x01));
		break;
	case 1:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(1), FIELD_PREP(BIT(1), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(1), FIELD_PREP(BIT(1), value & 0x01));
		break;
	case 2:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(2), FIELD_PREP(BIT(2), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(2), FIELD_PREP(BIT(2), value & 0x01));
		break;
	case 3:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(3), FIELD_PREP(BIT(3), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(3), FIELD_PREP(BIT(3), value & 0x01));
		break;
	case 4:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(4), FIELD_PREP(BIT(4), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(4), FIELD_PREP(BIT(4), value & 0x01));
		break;
	case 5:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(5), FIELD_PREP(BIT(5), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(5), FIELD_PREP(BIT(5), value & 0x01));
		break;
	case 6:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(6), FIELD_PREP(BIT(6), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(6), FIELD_PREP(BIT(6), value & 0x01));
		break;
	case 7:
		ret1 = serdes_set_bits(serdes, dir_reg, BIT(7), FIELD_PREP(BIT(7), 0));
		ret2 = serdes_set_bits(serdes, level_reg, BIT(7), FIELD_PREP(BIT(7), value & 0x01));
		break;
	default:
		break;
	}

	if (ret1 || ret2)
		dev_err(dev, "%s reg 0x%04x write error, pin=%d\n", __func__, level_reg, pin);

	return 0;
}

static int nca9539_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int nca9539_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	struct device *dev = serdes->dev;
	unsigned char port, pin, level_reg;
	int ret = 0;

	port = (gpio / 8) & 0x01;
	pin = (gpio % 8) & 0x7;
	level_reg = (port == 0) ? REG_NCA9539_OUT_LEVEL_PORT0 : REG_NCA9539_OUT_LEVEL_PORT1;

	switch (pin) {
	case 0:
		ret = serdes_set_bits(serdes, level_reg, BIT(0), FIELD_PREP(BIT(0), value & 0x01));
		break;
	case 1:
		ret = serdes_set_bits(serdes, level_reg, BIT(1), FIELD_PREP(BIT(1), value & 0x01));
		break;
	case 2:
		ret = serdes_set_bits(serdes, level_reg, BIT(2), FIELD_PREP(BIT(2), value & 0x01));
		break;
	case 3:
		ret = serdes_set_bits(serdes, level_reg, BIT(3), FIELD_PREP(BIT(3), value & 0x01));
		break;
	case 4:
		ret = serdes_set_bits(serdes, level_reg, BIT(4), FIELD_PREP(BIT(4), value & 0x01));
		break;
	case 5:
		ret = serdes_set_bits(serdes, level_reg, BIT(5), FIELD_PREP(BIT(5), value & 0x01));
		break;
	case 6:
		ret = serdes_set_bits(serdes, level_reg, BIT(6), FIELD_PREP(BIT(6), value & 0x01));
		break;
	case 7:
		ret = serdes_set_bits(serdes, level_reg, BIT(7), FIELD_PREP(BIT(7), value & 0x01));
		break;
	default:
		break;
	}

	if (ret)
		dev_err(dev, "%s reg 0x%04x write error, pin=%d\n", __func__, level_reg, pin);

	SERDES_DBG_CHIP("%s: serdes chip %s gpio=%d value=%d\n",
			__func__, serdes->chip_data->name, gpio, value);

	return 0;
}

static int nca9539_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int nca9539_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops nca9539_gpio_ops = {
	.direction_input = nca9539_gpio_direction_input,
	.direction_output = nca9539_gpio_direction_output,
	.get_level = nca9539_gpio_get_level,
	.set_level = nca9539_gpio_set_level,
	.set_config = nca9539_gpio_set_config,
	.to_irq = nca9539_gpio_to_irq,
};

static int nca9539_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int nca9539_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops nca9539_pm_ops = {
	.suspend = nca9539_pm_suspend,
	.resume = nca9539_pm_resume,
};

static int nca9539_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int nca9539_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops nca9539_irq_ops = {
	.lock_handle = nca9539_irq_lock_handle,
	.err_handle = nca9539_irq_err_handle,
};

struct serdes_chip_data serdes_nca9539_data = {
	.name		= "nca9539",
	.serdes_type	= TYPE_OTHER,
	.serdes_id	= NOVO_ID_NCA9539,
	.sequence_init = 1,
	.regmap_config	= &nca9539_regmap_config,
	.pinctrl_info	= &nca9539_pinctrl_info,
	.pinctrl_ops	= &nca9539_pinctrl_ops,
	.gpio_ops	= &nca9539_gpio_ops,
	.pm_ops		= &nca9539_pm_ops,
	.irq_ops	= &nca9539_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_nca9539_data);

MODULE_LICENSE("GPL");
