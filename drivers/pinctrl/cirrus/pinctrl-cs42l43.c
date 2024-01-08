// SPDX-License-Identifier: GPL-2.0
//
// CS42L43 Pinctrl and GPIO driver
//
// Copyright (c) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/string_helpers.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

#include "../pinctrl-utils.h"

#define CS42L43_NUM_GPIOS 3

struct cs42l43_pin {
	struct gpio_chip gpio_chip;

	struct device *dev;
	struct regmap *regmap;
	bool shutters_locked;
};

struct cs42l43_pin_data {
	unsigned int reg;
	unsigned int shift;
	unsigned int mask;
};

#define CS42L43_PIN(_number, _name, _reg, _field) { \
	.number = _number, .name = _name, \
	.drv_data = &((struct cs42l43_pin_data){ \
		.reg = CS42L43_##_reg, \
		.shift = CS42L43_##_field##_DRV_SHIFT, \
		.mask = CS42L43_##_field##_DRV_MASK, \
	}), \
}

static const struct pinctrl_pin_desc cs42l43_pin_pins[] = {
	CS42L43_PIN(0,	"gpio1",	DRV_CTRL4,	GPIO1),
	CS42L43_PIN(1,	"gpio2",	DRV_CTRL4,	GPIO2),
	CS42L43_PIN(2,	"gpio3",	DRV_CTRL4,	GPIO3),
	CS42L43_PIN(3,	"asp_dout",	DRV_CTRL1,	ASP_DOUT),
	CS42L43_PIN(4,	"asp_fsync",	DRV_CTRL1,	ASP_FSYNC),
	CS42L43_PIN(5,	"asp_bclk",	DRV_CTRL1,	ASP_BCLK),
	CS42L43_PIN(6,	"pdmout2_clk",	DRV_CTRL3,	PDMOUT2_CLK),
	CS42L43_PIN(7,	"pdmout2_data",	DRV_CTRL3,	PDMOUT2_DATA),
	CS42L43_PIN(8,	"pdmout1_clk",	DRV_CTRL3,	PDMOUT1_CLK),
	CS42L43_PIN(9,	"pdmout1_data",	DRV_CTRL3,	PDMOUT1_DATA),
	CS42L43_PIN(10,	"i2c_sda",	DRV_CTRL3,	I2C_SDA),
	CS42L43_PIN(11,	"i2c_scl",	DRV_CTRL_5,	I2C_SCL),
	CS42L43_PIN(12,	"spi_miso",	DRV_CTRL3,	SPI_MISO),
	CS42L43_PIN(13,	"spi_sck",	DRV_CTRL_5,	SPI_SCK),
	CS42L43_PIN(14,	"spi_ssb",	DRV_CTRL_5,	SPI_SSB),
};

static const unsigned int cs42l43_pin_gpio1_pins[] = { 0 };
static const unsigned int cs42l43_pin_gpio2_pins[] = { 1 };
static const unsigned int cs42l43_pin_gpio3_pins[] = { 2 };
static const unsigned int cs42l43_pin_asp_pins[] = { 3, 4, 5 };
static const unsigned int cs42l43_pin_pdmout2_pins[] = { 6, 7 };
static const unsigned int cs42l43_pin_pdmout1_pins[] = { 8, 9 };
static const unsigned int cs42l43_pin_i2c_pins[] = { 10, 11 };
static const unsigned int cs42l43_pin_spi_pins[] = { 12, 13, 14 };

#define CS42L43_PINGROUP(_name) \
	PINCTRL_PINGROUP(#_name, cs42l43_pin_##_name##_pins, \
			 ARRAY_SIZE(cs42l43_pin_##_name##_pins))

static const struct pingroup cs42l43_pin_groups[] = {
	CS42L43_PINGROUP(gpio1),
	CS42L43_PINGROUP(gpio2),
	CS42L43_PINGROUP(gpio3),
	CS42L43_PINGROUP(asp),
	CS42L43_PINGROUP(pdmout2),
	CS42L43_PINGROUP(pdmout1),
	CS42L43_PINGROUP(i2c),
	CS42L43_PINGROUP(spi),
};

static int cs42l43_pin_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(cs42l43_pin_groups);
}

static const char *cs42l43_pin_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned int group_idx)
{
	return cs42l43_pin_groups[group_idx].name;
}

static int cs42l43_pin_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned int group_idx,
				      const unsigned int **pins,
				      unsigned int *num_pins)
{
	*pins = cs42l43_pin_groups[group_idx].pins;
	*num_pins = cs42l43_pin_groups[group_idx].npins;

	return 0;
}

static const struct pinctrl_ops cs42l43_pin_group_ops = {
	.get_groups_count = cs42l43_pin_get_groups_count,
	.get_group_name = cs42l43_pin_get_group_name,
	.get_group_pins = cs42l43_pin_get_group_pins,
#if IS_ENABLED(CONFIG_OF)
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
#endif
};

enum cs42l43_pin_funcs {
	CS42L43_FUNC_GPIO,
	CS42L43_FUNC_SPDIF,
	CS42L43_FUNC_IRQ,
	CS42L43_FUNC_MIC_SHT,
	CS42L43_FUNC_SPK_SHT,
	CS42L43_FUNC_MAX
};

static const char * const cs42l43_pin_funcs[] = {
	"gpio", "spdif", "irq", "mic-shutter", "spk-shutter",
};

static const char * const cs42l43_pin_gpio_groups[] = { "gpio1", "gpio3" };
static const char * const cs42l43_pin_spdif_groups[] = { "gpio3" };
static const char * const cs42l43_pin_irq_groups[] = { "gpio1" };
static const char * const cs42l43_pin_shutter_groups[] = { "gpio1", "gpio2", "gpio3" };

static const struct pinfunction cs42l43_pin_func_groups[] = {
	PINCTRL_PINFUNCTION("gpio", cs42l43_pin_gpio_groups,
			    ARRAY_SIZE(cs42l43_pin_gpio_groups)),
	PINCTRL_PINFUNCTION("spdif", cs42l43_pin_spdif_groups,
			    ARRAY_SIZE(cs42l43_pin_spdif_groups)),
	PINCTRL_PINFUNCTION("irq",  cs42l43_pin_irq_groups,
			    ARRAY_SIZE(cs42l43_pin_irq_groups)),
	PINCTRL_PINFUNCTION("mic-shutter", cs42l43_pin_shutter_groups,
			    ARRAY_SIZE(cs42l43_pin_shutter_groups)),
	PINCTRL_PINFUNCTION("spk-shutter", cs42l43_pin_shutter_groups,
			    ARRAY_SIZE(cs42l43_pin_shutter_groups)),
};

static_assert(ARRAY_SIZE(cs42l43_pin_funcs) == CS42L43_FUNC_MAX);
static_assert(ARRAY_SIZE(cs42l43_pin_func_groups) == CS42L43_FUNC_MAX);

static int cs42l43_pin_get_func_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(cs42l43_pin_funcs);
}

static const char *cs42l43_pin_get_func_name(struct pinctrl_dev *pctldev,
					     unsigned int func_idx)
{
	return cs42l43_pin_funcs[func_idx];
}

static int cs42l43_pin_get_func_groups(struct pinctrl_dev *pctldev,
				       unsigned int func_idx,
				       const char * const **groups,
				       unsigned int * const num_groups)
{
	*groups = cs42l43_pin_func_groups[func_idx].groups;
	*num_groups = cs42l43_pin_func_groups[func_idx].ngroups;

	return 0;
}

static int cs42l43_pin_set_mux(struct pinctrl_dev *pctldev,
			       unsigned int func_idx, unsigned int group_idx)
{
	struct cs42l43_pin *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int reg, mask, val;

	dev_dbg(priv->dev, "Setting %s to %s\n",
		cs42l43_pin_groups[group_idx].name, cs42l43_pin_funcs[func_idx]);

	switch (func_idx) {
	case CS42L43_FUNC_MIC_SHT:
		reg = CS42L43_SHUTTER_CONTROL;
		mask = CS42L43_MIC_SHUTTER_CFG_MASK;
		val = 0x2 << (group_idx + CS42L43_MIC_SHUTTER_CFG_SHIFT);
		break;
	case CS42L43_FUNC_SPK_SHT:
		reg = CS42L43_SHUTTER_CONTROL;
		mask = CS42L43_SPK_SHUTTER_CFG_MASK;
		val = 0x2 << (group_idx + CS42L43_SPK_SHUTTER_CFG_SHIFT);
		break;
	default:
		reg = CS42L43_GPIO_FN_SEL;
		mask = BIT(group_idx + CS42L43_GPIO1_FN_SEL_SHIFT);
		val = (func_idx == CS42L43_FUNC_GPIO) ?
				(0x1 << (group_idx + CS42L43_GPIO1_FN_SEL_SHIFT)) : 0;
		break;
	}

	if (priv->shutters_locked && reg == CS42L43_SHUTTER_CONTROL) {
		dev_err(priv->dev, "Shutter configuration not available\n");
		return -EPERM;
	}

	return regmap_update_bits(priv->regmap, reg, mask, val);
}

static int cs42l43_gpio_set_direction(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset, bool input)
{
	struct cs42l43_pin *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int shift = offset + CS42L43_GPIO1_DIR_SHIFT;
	int ret;

	dev_dbg(priv->dev, "Setting gpio%d to %s\n",
		offset + 1, input ? "input" : "output");

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for direction: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, CS42L43_GPIO_CTRL1,
				 BIT(shift), !!input << shift);
	if (ret)
		dev_err(priv->dev, "Failed to set gpio%d direction: %d\n",
			offset + 1, ret);

	pm_runtime_put(priv->dev);

	return ret;
}

static int cs42l43_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned int offset)
{
	return cs42l43_pin_set_mux(pctldev, 0, offset);
}

static void cs42l43_gpio_disable_free(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	cs42l43_gpio_set_direction(pctldev, range, offset, true);
}

static const struct pinmux_ops cs42l43_pin_mux_ops = {
	.get_functions_count	= cs42l43_pin_get_func_count,
	.get_function_name	= cs42l43_pin_get_func_name,
	.get_function_groups	= cs42l43_pin_get_func_groups,

	.set_mux		= cs42l43_pin_set_mux,

	.gpio_request_enable	= cs42l43_gpio_request_enable,
	.gpio_disable_free	= cs42l43_gpio_disable_free,
	.gpio_set_direction	= cs42l43_gpio_set_direction,

	.strict			= true,
};

static const unsigned int cs42l43_pin_drv_str_ma[] = { 1, 2, 4, 8, 9, 10, 12, 16 };

static inline int cs42l43_pin_get_drv_str(struct cs42l43_pin *priv, unsigned int pin)
{
	const struct cs42l43_pin_data *pdat = cs42l43_pin_pins[pin].drv_data;
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, pdat->reg, &val);
	if (ret)
		return ret;

	return cs42l43_pin_drv_str_ma[(val & pdat->mask) >> pdat->shift];
}

static inline int cs42l43_pin_set_drv_str(struct cs42l43_pin *priv, unsigned int pin,
					  unsigned int ma)
{
	const struct cs42l43_pin_data *pdat = cs42l43_pin_pins[pin].drv_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(cs42l43_pin_drv_str_ma); i++) {
		if (ma == cs42l43_pin_drv_str_ma[i]) {
			if ((i << pdat->shift) > pdat->mask)
				goto err;

			dev_dbg(priv->dev, "Set drive strength for %s to %d mA\n",
				cs42l43_pin_pins[pin].name, ma);

			return regmap_update_bits(priv->regmap, pdat->reg,
						  pdat->mask, i << pdat->shift);
		}
	}

err:
	dev_err(priv->dev, "Invalid drive strength for %s: %d mA\n",
		cs42l43_pin_pins[pin].name, ma);
	return -EINVAL;
}

static inline int cs42l43_pin_get_db(struct cs42l43_pin *priv, unsigned int pin)
{
	unsigned int val;
	int ret;

	if (pin >= CS42L43_NUM_GPIOS)
		return -ENOTSUPP;

	ret = regmap_read(priv->regmap, CS42L43_GPIO_CTRL2, &val);
	if (ret)
		return ret;

	if (val & (CS42L43_GPIO1_DEGLITCH_BYP_MASK << pin))
		return 0;

	return 85; // Debounce is roughly 85uS
}

static inline int cs42l43_pin_set_db(struct cs42l43_pin *priv, unsigned int pin,
				     unsigned int us)
{
	if (pin >= CS42L43_NUM_GPIOS)
		return -ENOTSUPP;

	dev_dbg(priv->dev, "Set debounce %s for %s\n",
		str_on_off(us), cs42l43_pin_pins[pin].name);

	return regmap_update_bits(priv->regmap, CS42L43_GPIO_CTRL2,
				  CS42L43_GPIO1_DEGLITCH_BYP_MASK << pin,
				  !!us << pin);
}

static int cs42l43_pin_config_get(struct pinctrl_dev *pctldev,
				  unsigned int pin, unsigned long *config)
{
	struct cs42l43_pin *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	int ret;

	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = cs42l43_pin_get_drv_str(priv, pin);
		if (ret < 0)
			return ret;
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		ret = cs42l43_pin_get_db(priv, pin);
		if (ret < 0)
			return ret;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, ret);

	return 0;
}

static int cs42l43_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				  unsigned long *configs, unsigned int num_configs)
{
	struct cs42l43_pin *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int val;
	int ret;

	while (num_configs) {
		val = pinconf_to_config_argument(*configs);

		switch (pinconf_to_config_param(*configs)) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = cs42l43_pin_set_drv_str(priv, pin, val);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_INPUT_DEBOUNCE:
			ret = cs42l43_pin_set_db(priv, pin, val);
			if (ret)
				return ret;
			break;
		default:
			return -ENOTSUPP;
		}

		configs++;
		num_configs--;
	}

	return 0;
}

static int cs42l43_pin_config_group_get(struct pinctrl_dev *pctldev,
					unsigned int selector, unsigned long *config)
{
	int i, ret;

	for (i = 0; i < cs42l43_pin_groups[selector].npins; ++i) {
		ret = cs42l43_pin_config_get(pctldev,
					     cs42l43_pin_groups[selector].pins[i],
					     config);
		if (ret)
			return ret;
	}

	return 0;
}

static int cs42l43_pin_config_group_set(struct pinctrl_dev *pctldev,
					unsigned int selector,
					unsigned long *configs,
					unsigned int num_configs)
{
	int i, ret;

	for (i = 0; i < cs42l43_pin_groups[selector].npins; ++i) {
		ret = cs42l43_pin_config_set(pctldev,
					     cs42l43_pin_groups[selector].pins[i],
					     configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops cs42l43_pin_conf_ops = {
	.is_generic		= true,

	.pin_config_get		= cs42l43_pin_config_get,
	.pin_config_set		= cs42l43_pin_config_set,
	.pin_config_group_get	= cs42l43_pin_config_group_get,
	.pin_config_group_set	= cs42l43_pin_config_group_set,
};

static struct pinctrl_desc cs42l43_pin_desc = {
	.name		= "cs42l43-pinctrl",
	.owner		= THIS_MODULE,

	.pins		= cs42l43_pin_pins,
	.npins		= ARRAY_SIZE(cs42l43_pin_pins),

	.pctlops	= &cs42l43_pin_group_ops,
	.pmxops		= &cs42l43_pin_mux_ops,
	.confops	= &cs42l43_pin_conf_ops,
};

static int cs42l43_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct cs42l43_pin *priv = gpiochip_get_data(chip);
	unsigned int val;
	int ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for get: %d\n", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, CS42L43_GPIO_STS, &val);
	if (ret)
		dev_err(priv->dev, "Failed to get gpio%d: %d\n", offset + 1, ret);
	else
		ret = !!(val & BIT(offset + CS42L43_GPIO1_STS_SHIFT));

	pm_runtime_put(priv->dev);

	return ret;
}

static void cs42l43_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct cs42l43_pin *priv = gpiochip_get_data(chip);
	unsigned int shift = offset + CS42L43_GPIO1_LVL_SHIFT;
	int ret;

	dev_dbg(priv->dev, "Setting gpio%d to %s\n",
		offset + 1, value ? "high" : "low");

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for set: %d\n", ret);
		return;
	}

	ret = regmap_update_bits(priv->regmap, CS42L43_GPIO_CTRL1,
				 BIT(shift), value << shift);
	if (ret)
		dev_err(priv->dev, "Failed to set gpio%d: %d\n", offset + 1, ret);

	pm_runtime_put(priv->dev);
}

static int cs42l43_gpio_direction_out(struct gpio_chip *chip,
				      unsigned int offset, int value)
{
	cs42l43_gpio_set(chip, offset, value);

	return pinctrl_gpio_direction_output(chip, offset);
}

static int cs42l43_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct cs42l43_pin *priv = gpiochip_get_data(chip);
	int ret;

	ret = gpiochip_add_pin_range(&priv->gpio_chip, priv->gpio_chip.label,
				     0, 0, CS42L43_NUM_GPIOS);
	if (ret)
		dev_err(priv->dev, "Failed to add GPIO pin range: %d\n", ret);

	return ret;
}

static int cs42l43_pin_probe(struct platform_device *pdev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(pdev->dev.parent);
	struct cs42l43_pin *priv;
	struct pinctrl_dev *pctldev;
	struct fwnode_handle *fwnode = dev_fwnode(cs42l43->dev);
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap = cs42l43->regmap;

	priv->shutters_locked = cs42l43->hw_lock;

	priv->gpio_chip.request = gpiochip_generic_request;
	priv->gpio_chip.free = gpiochip_generic_free;
	priv->gpio_chip.direction_input = pinctrl_gpio_direction_input;
	priv->gpio_chip.direction_output = cs42l43_gpio_direction_out;
	priv->gpio_chip.add_pin_ranges = cs42l43_gpio_add_pin_ranges;
	priv->gpio_chip.get = cs42l43_gpio_get;
	priv->gpio_chip.set = cs42l43_gpio_set;
	priv->gpio_chip.label = dev_name(priv->dev);
	priv->gpio_chip.parent = priv->dev;
	priv->gpio_chip.can_sleep = true;
	priv->gpio_chip.base = -1;
	priv->gpio_chip.ngpio = CS42L43_NUM_GPIOS;

	if (is_of_node(fwnode)) {
		fwnode = fwnode_get_named_child_node(fwnode, "pinctrl");

		if (fwnode && !fwnode->dev)
			fwnode->dev = priv->dev;
	}

	priv->gpio_chip.fwnode = fwnode;

	device_set_node(priv->dev, fwnode);

	devm_pm_runtime_enable(priv->dev);
	pm_runtime_idle(priv->dev);

	pctldev = devm_pinctrl_register(priv->dev, &cs42l43_pin_desc, priv);
	if (IS_ERR(pctldev))
		return dev_err_probe(priv->dev, PTR_ERR(pctldev),
				     "Failed to register pinctrl\n");

	ret = devm_gpiochip_add_data(priv->dev, &priv->gpio_chip, priv);
	if (ret)
		return dev_err_probe(priv->dev, ret,
				     "Failed to register gpiochip\n");

	return 0;
}

static const struct platform_device_id cs42l43_pin_id_table[] = {
	{ "cs42l43-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(platform, cs42l43_pin_id_table);

static struct platform_driver cs42l43_pin_driver = {
	.driver = {
		.name	= "cs42l43-pinctrl",
	},
	.probe		= cs42l43_pin_probe,
	.id_table	= cs42l43_pin_id_table,
};
module_platform_driver(cs42l43_pin_driver);

MODULE_DESCRIPTION("CS42L43 Pinctrl Driver");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
