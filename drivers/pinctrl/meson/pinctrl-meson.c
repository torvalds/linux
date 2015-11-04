/*
 * Pin controller and GPIO driver for Amlogic Meson SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The available pins are organized in banks (A,B,C,D,E,X,Y,Z,AO,
 * BOOT,CARD for meson6, X,Y,DV,H,Z,AO,BOOT,CARD for meson8 and
 * X,Y,DV,H,AO,BOOT,CARD,DIF for meson8b) and each bank has a
 * variable number of pins.
 *
 * The AO bank is special because it belongs to the Always-On power
 * domain which can't be powered off; the bank also uses a set of
 * registers different from the other banks.
 *
 * For each of the two power domains (regular and always-on) there are
 * 4 different register ranges that control the following properties
 * of the pins:
 *  1) pin muxing
 *  2) pull enable/disable
 *  3) pull up/down
 *  4) GPIO direction, output value, input value
 *
 * In some cases the register ranges for pull enable and pull
 * direction are the same and thus there are only 3 register ranges.
 *
 * Every pinmux group can be enabled by a specific bit in the first
 * register range of the domain; when all groups for a given pin are
 * disabled the pin acts as a GPIO.
 *
 * For the pull and GPIO configuration every bank uses a contiguous
 * set of bits in the register sets described above; the same register
 * can be shared by more banks with different offsets.
 *
 * In addition to this there are some registers shared between all
 * banks that control the IRQ functionality. This feature is not
 * supported at the moment by the driver.
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "pinctrl-meson.h"

/**
 * meson_get_bank() - find the bank containing a given pin
 *
 * @domain:	the domain containing the pin
 * @pin:	the pin number
 * @bank:	the found bank
 *
 * Return:	0 on success, a negative value on error
 */
static int meson_get_bank(struct meson_domain *domain, unsigned int pin,
			  struct meson_bank **bank)
{
	int i;

	for (i = 0; i < domain->data->num_banks; i++) {
		if (pin >= domain->data->banks[i].first &&
		    pin <= domain->data->banks[i].last) {
			*bank = &domain->data->banks[i];
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * meson_get_domain_and_bank() - find domain and bank containing a given pin
 *
 * @pc:		Meson pin controller device
 * @pin:	the pin number
 * @domain:	the found domain
 * @bank:	the found bank
 *
 * Return:	0 on success, a negative value on error
 */
static int meson_get_domain_and_bank(struct meson_pinctrl *pc, unsigned int pin,
				     struct meson_domain **domain,
				     struct meson_bank **bank)
{
	struct meson_domain *d;
	int i;

	for (i = 0; i < pc->data->num_domains; i++) {
		d = &pc->domains[i];
		if (pin >= d->data->pin_base &&
		    pin < d->data->pin_base + d->data->num_pins) {
			*domain = d;
			return meson_get_bank(d, pin, bank);
		}
	}

	return -EINVAL;
}

/**
 * meson_calc_reg_and_bit() - calculate register and bit for a pin
 *
 * @bank:	the bank containing the pin
 * @pin:	the pin number
 * @reg_type:	the type of register needed (pull-enable, pull, etc...)
 * @reg:	the computed register offset
 * @bit:	the computed bit
 */
static void meson_calc_reg_and_bit(struct meson_bank *bank, unsigned int pin,
				   enum meson_reg_type reg_type,
				   unsigned int *reg, unsigned int *bit)
{
	struct meson_reg_desc *desc = &bank->regs[reg_type];

	*reg = desc->reg * 4;
	*bit = desc->bit + pin - bank->first;
}

static int meson_get_groups_count(struct pinctrl_dev *pcdev)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	return pc->data->num_groups;
}

static const char *meson_get_group_name(struct pinctrl_dev *pcdev,
					unsigned selector)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	return pc->data->groups[selector].name;
}

static int meson_get_group_pins(struct pinctrl_dev *pcdev, unsigned selector,
				const unsigned **pins, unsigned *num_pins)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	*pins = pc->data->groups[selector].pins;
	*num_pins = pc->data->groups[selector].num_pins;

	return 0;
}

static void meson_pin_dbg_show(struct pinctrl_dev *pcdev, struct seq_file *s,
			       unsigned offset)
{
	seq_printf(s, " %s", dev_name(pcdev->dev));
}

static const struct pinctrl_ops meson_pctrl_ops = {
	.get_groups_count	= meson_get_groups_count,
	.get_group_name		= meson_get_group_name,
	.get_group_pins		= meson_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinctrl_utils_dt_free_map,
	.pin_dbg_show		= meson_pin_dbg_show,
};

/**
 * meson_pmx_disable_other_groups() - disable other groups using a given pin
 *
 * @pc:		meson pin controller device
 * @pin:	number of the pin
 * @sel_group:	index of the selected group, or -1 if none
 *
 * The function disables all pinmux groups using a pin except the
 * selected one. If @sel_group is -1 all groups are disabled, leaving
 * the pin in GPIO mode.
 */
static void meson_pmx_disable_other_groups(struct meson_pinctrl *pc,
					   unsigned int pin, int sel_group)
{
	struct meson_pmx_group *group;
	struct meson_domain *domain;
	int i, j;

	for (i = 0; i < pc->data->num_groups; i++) {
		group = &pc->data->groups[i];
		if (group->is_gpio || i == sel_group)
			continue;

		for (j = 0; j < group->num_pins; j++) {
			if (group->pins[j] == pin) {
				/* We have found a group using the pin */
				domain = &pc->domains[group->domain];
				regmap_update_bits(domain->reg_mux,
						   group->reg * 4,
						   BIT(group->bit), 0);
			}
		}
	}
}

static int meson_pmx_set_mux(struct pinctrl_dev *pcdev, unsigned func_num,
			     unsigned group_num)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);
	struct meson_pmx_func *func = &pc->data->funcs[func_num];
	struct meson_pmx_group *group = &pc->data->groups[group_num];
	struct meson_domain *domain = &pc->domains[group->domain];
	int i, ret = 0;

	dev_dbg(pc->dev, "enable function %s, group %s\n", func->name,
		group->name);

	/*
	 * Disable groups using the same pin.
	 * The selected group is not disabled to avoid glitches.
	 */
	for (i = 0; i < group->num_pins; i++)
		meson_pmx_disable_other_groups(pc, group->pins[i], group_num);

	/* Function 0 (GPIO) doesn't need any additional setting */
	if (func_num)
		ret = regmap_update_bits(domain->reg_mux, group->reg * 4,
					 BIT(group->bit), BIT(group->bit));

	return ret;
}

static int meson_pmx_request_gpio(struct pinctrl_dev *pcdev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	meson_pmx_disable_other_groups(pc, range->pin_base + offset, -1);

	return 0;
}

static int meson_pmx_get_funcs_count(struct pinctrl_dev *pcdev)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	return pc->data->num_funcs;
}

static const char *meson_pmx_get_func_name(struct pinctrl_dev *pcdev,
					   unsigned selector)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	return pc->data->funcs[selector].name;
}

static int meson_pmx_get_groups(struct pinctrl_dev *pcdev, unsigned selector,
				const char * const **groups,
				unsigned * const num_groups)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);

	*groups = pc->data->funcs[selector].groups;
	*num_groups = pc->data->funcs[selector].num_groups;

	return 0;
}

static const struct pinmux_ops meson_pmx_ops = {
	.set_mux = meson_pmx_set_mux,
	.get_functions_count = meson_pmx_get_funcs_count,
	.get_function_name = meson_pmx_get_func_name,
	.get_function_groups = meson_pmx_get_groups,
	.gpio_request_enable = meson_pmx_request_gpio,
};

static int meson_pinconf_set(struct pinctrl_dev *pcdev, unsigned int pin,
			     unsigned long *configs, unsigned num_configs)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);
	struct meson_domain *domain;
	struct meson_bank *bank;
	enum pin_config_param param;
	unsigned int reg, bit;
	int i, ret;
	u16 arg;

	ret = meson_get_domain_and_bank(pc, pin, &domain, &bank);
	if (ret)
		return ret;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			dev_dbg(pc->dev, "pin %u: disable bias\n", pin);

			meson_calc_reg_and_bit(bank, pin, REG_PULL, &reg, &bit);
			ret = regmap_update_bits(domain->reg_pull, reg,
						 BIT(bit), 0);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			dev_dbg(pc->dev, "pin %u: enable pull-up\n", pin);

			meson_calc_reg_and_bit(bank, pin, REG_PULLEN,
					       &reg, &bit);
			ret = regmap_update_bits(domain->reg_pullen, reg,
						 BIT(bit), BIT(bit));
			if (ret)
				return ret;

			meson_calc_reg_and_bit(bank, pin, REG_PULL, &reg, &bit);
			ret = regmap_update_bits(domain->reg_pull, reg,
						 BIT(bit), BIT(bit));
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			dev_dbg(pc->dev, "pin %u: enable pull-down\n", pin);

			meson_calc_reg_and_bit(bank, pin, REG_PULLEN,
					       &reg, &bit);
			ret = regmap_update_bits(domain->reg_pullen, reg,
						 BIT(bit), BIT(bit));
			if (ret)
				return ret;

			meson_calc_reg_and_bit(bank, pin, REG_PULL, &reg, &bit);
			ret = regmap_update_bits(domain->reg_pull, reg,
						 BIT(bit), 0);
			if (ret)
				return ret;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int meson_pinconf_get_pull(struct meson_pinctrl *pc, unsigned int pin)
{
	struct meson_domain *domain;
	struct meson_bank *bank;
	unsigned int reg, bit, val;
	int ret, conf;

	ret = meson_get_domain_and_bank(pc, pin, &domain, &bank);
	if (ret)
		return ret;

	meson_calc_reg_and_bit(bank, pin, REG_PULLEN, &reg, &bit);

	ret = regmap_read(domain->reg_pullen, reg, &val);
	if (ret)
		return ret;

	if (!(val & BIT(bit))) {
		conf = PIN_CONFIG_BIAS_DISABLE;
	} else {
		meson_calc_reg_and_bit(bank, pin, REG_PULL, &reg, &bit);

		ret = regmap_read(domain->reg_pull, reg, &val);
		if (ret)
			return ret;

		if (val & BIT(bit))
			conf = PIN_CONFIG_BIAS_PULL_UP;
		else
			conf = PIN_CONFIG_BIAS_PULL_DOWN;
	}

	return conf;
}

static int meson_pinconf_get(struct pinctrl_dev *pcdev, unsigned int pin,
			     unsigned long *config)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u16 arg;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		if (meson_pinconf_get_pull(pc, pin) == param)
			arg = 1;
		else
			return -EINVAL;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	dev_dbg(pc->dev, "pinconf for pin %u is %lu\n", pin, *config);

	return 0;
}

static int meson_pinconf_group_set(struct pinctrl_dev *pcdev,
				   unsigned int num_group,
				   unsigned long *configs, unsigned num_configs)
{
	struct meson_pinctrl *pc = pinctrl_dev_get_drvdata(pcdev);
	struct meson_pmx_group *group = &pc->data->groups[num_group];
	int i;

	dev_dbg(pc->dev, "set pinconf for group %s\n", group->name);

	for (i = 0; i < group->num_pins; i++) {
		meson_pinconf_set(pcdev, group->pins[i], configs,
				  num_configs);
	}

	return 0;
}

static int meson_pinconf_group_get(struct pinctrl_dev *pcdev,
				   unsigned int group, unsigned long *config)
{
	return -ENOSYS;
}

static const struct pinconf_ops meson_pinconf_ops = {
	.pin_config_get		= meson_pinconf_get,
	.pin_config_set		= meson_pinconf_set,
	.pin_config_group_get	= meson_pinconf_group_get,
	.pin_config_group_set	= meson_pinconf_group_set,
	.is_generic		= true,
};

static inline struct meson_domain *to_meson_domain(struct gpio_chip *chip)
{
	return container_of(chip, struct meson_domain, chip);
}

static int meson_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	return pinctrl_request_gpio(chip->base + gpio);
}

static void meson_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	struct meson_domain *domain = to_meson_domain(chip);

	pinctrl_free_gpio(domain->data->pin_base + gpio);
}

static int meson_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct meson_domain *domain = to_meson_domain(chip);
	unsigned int reg, bit, pin;
	struct meson_bank *bank;
	int ret;

	pin = domain->data->pin_base + gpio;
	ret = meson_get_bank(domain, pin, &bank);
	if (ret)
		return ret;

	meson_calc_reg_and_bit(bank, pin, REG_DIR, &reg, &bit);

	return regmap_update_bits(domain->reg_gpio, reg, BIT(bit), BIT(bit));
}

static int meson_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
				       int value)
{
	struct meson_domain *domain = to_meson_domain(chip);
	unsigned int reg, bit, pin;
	struct meson_bank *bank;
	int ret;

	pin = domain->data->pin_base + gpio;
	ret = meson_get_bank(domain, pin, &bank);
	if (ret)
		return ret;

	meson_calc_reg_and_bit(bank, pin, REG_DIR, &reg, &bit);
	ret = regmap_update_bits(domain->reg_gpio, reg, BIT(bit), 0);
	if (ret)
		return ret;

	meson_calc_reg_and_bit(bank, pin, REG_OUT, &reg, &bit);
	return regmap_update_bits(domain->reg_gpio, reg, BIT(bit),
				  value ? BIT(bit) : 0);
}

static void meson_gpio_set(struct gpio_chip *chip, unsigned gpio, int value)
{
	struct meson_domain *domain = to_meson_domain(chip);
	unsigned int reg, bit, pin;
	struct meson_bank *bank;
	int ret;

	pin = domain->data->pin_base + gpio;
	ret = meson_get_bank(domain, pin, &bank);
	if (ret)
		return;

	meson_calc_reg_and_bit(bank, pin, REG_OUT, &reg, &bit);
	regmap_update_bits(domain->reg_gpio, reg, BIT(bit),
			   value ? BIT(bit) : 0);
}

static int meson_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	struct meson_domain *domain = to_meson_domain(chip);
	unsigned int reg, bit, val, pin;
	struct meson_bank *bank;
	int ret;

	pin = domain->data->pin_base + gpio;
	ret = meson_get_bank(domain, pin, &bank);
	if (ret)
		return ret;

	meson_calc_reg_and_bit(bank, pin, REG_IN, &reg, &bit);
	regmap_read(domain->reg_gpio, reg, &val);

	return !!(val & BIT(bit));
}

static const struct of_device_id meson_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,meson8-pinctrl",
		.data = &meson8_pinctrl_data,
	},
	{
		.compatible = "amlogic,meson8b-pinctrl",
		.data = &meson8b_pinctrl_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, meson_pinctrl_dt_match);

static int meson_gpiolib_register(struct meson_pinctrl *pc)
{
	struct meson_domain *domain;
	int i, ret;

	for (i = 0; i < pc->data->num_domains; i++) {
		domain = &pc->domains[i];

		domain->chip.label = domain->data->name;
		domain->chip.parent = pc->dev;
		domain->chip.request = meson_gpio_request;
		domain->chip.free = meson_gpio_free;
		domain->chip.direction_input = meson_gpio_direction_input;
		domain->chip.direction_output = meson_gpio_direction_output;
		domain->chip.get = meson_gpio_get;
		domain->chip.set = meson_gpio_set;
		domain->chip.base = domain->data->pin_base;
		domain->chip.ngpio = domain->data->num_pins;
		domain->chip.can_sleep = false;
		domain->chip.of_node = domain->of_node;
		domain->chip.of_gpio_n_cells = 2;

		ret = gpiochip_add(&domain->chip);
		if (ret) {
			dev_err(pc->dev, "can't add gpio chip %s\n",
				domain->data->name);
			goto fail;
		}

		ret = gpiochip_add_pin_range(&domain->chip, dev_name(pc->dev),
					     0, domain->data->pin_base,
					     domain->chip.ngpio);
		if (ret) {
			dev_err(pc->dev, "can't add pin range\n");
			goto fail;
		}
	}

	return 0;
fail:
	for (i--; i >= 0; i--)
		gpiochip_remove(&pc->domains[i].chip);

	return ret;
}

static struct meson_domain_data *meson_get_domain_data(struct meson_pinctrl *pc,
						       struct device_node *np)
{
	int i;

	for (i = 0; i < pc->data->num_domains; i++) {
		if (!strcmp(np->name, pc->data->domain_data[i].name))
			return &pc->data->domain_data[i];
	}

	return NULL;
}

static struct regmap_config meson_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static struct regmap *meson_map_resource(struct meson_pinctrl *pc,
					 struct device_node *node, char *name)
{
	struct resource res;
	void __iomem *base;
	int i;

	i = of_property_match_string(node, "reg-names", name);
	if (of_address_to_resource(node, i, &res))
		return ERR_PTR(-ENOENT);

	base = devm_ioremap_resource(pc->dev, &res);
	if (IS_ERR(base))
		return ERR_CAST(base);

	meson_regmap_config.max_register = resource_size(&res) - 4;
	meson_regmap_config.name = devm_kasprintf(pc->dev, GFP_KERNEL,
						  "%s-%s", node->name,
						  name);
	if (!meson_regmap_config.name)
		return ERR_PTR(-ENOMEM);

	return devm_regmap_init_mmio(pc->dev, base, &meson_regmap_config);
}

static int meson_pinctrl_parse_dt(struct meson_pinctrl *pc,
				  struct device_node *node)
{
	struct device_node *np;
	struct meson_domain *domain;
	int i = 0, num_domains = 0;

	for_each_child_of_node(node, np) {
		if (!of_find_property(np, "gpio-controller", NULL))
			continue;
		num_domains++;
	}

	if (num_domains != pc->data->num_domains) {
		dev_err(pc->dev, "wrong number of subnodes\n");
		return -EINVAL;
	}

	pc->domains = devm_kzalloc(pc->dev, num_domains *
				   sizeof(struct meson_domain), GFP_KERNEL);
	if (!pc->domains)
		return -ENOMEM;

	for_each_child_of_node(node, np) {
		if (!of_find_property(np, "gpio-controller", NULL))
			continue;

		domain = &pc->domains[i];

		domain->data = meson_get_domain_data(pc, np);
		if (!domain->data) {
			dev_err(pc->dev, "domain data not found for node %s\n",
				np->name);
			return -ENODEV;
		}

		domain->of_node = np;

		domain->reg_mux = meson_map_resource(pc, np, "mux");
		if (IS_ERR(domain->reg_mux)) {
			dev_err(pc->dev, "mux registers not found\n");
			return PTR_ERR(domain->reg_mux);
		}

		domain->reg_pull = meson_map_resource(pc, np, "pull");
		if (IS_ERR(domain->reg_pull)) {
			dev_err(pc->dev, "pull registers not found\n");
			return PTR_ERR(domain->reg_pull);
		}

		domain->reg_pullen = meson_map_resource(pc, np, "pull-enable");
		/* Use pull region if pull-enable one is not present */
		if (IS_ERR(domain->reg_pullen))
			domain->reg_pullen = domain->reg_pull;

		domain->reg_gpio = meson_map_resource(pc, np, "gpio");
		if (IS_ERR(domain->reg_gpio)) {
			dev_err(pc->dev, "gpio registers not found\n");
			return PTR_ERR(domain->reg_gpio);
		}

		i++;
	}

	return 0;
}

static int meson_pinctrl_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct meson_pinctrl *pc;
	int ret;

	pc = devm_kzalloc(dev, sizeof(struct meson_pinctrl), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->dev = dev;
	match = of_match_node(meson_pinctrl_dt_match, pdev->dev.of_node);
	pc->data = (struct meson_pinctrl_data *)match->data;

	ret = meson_pinctrl_parse_dt(pc, pdev->dev.of_node);
	if (ret)
		return ret;

	pc->desc.name		= "pinctrl-meson";
	pc->desc.owner		= THIS_MODULE;
	pc->desc.pctlops	= &meson_pctrl_ops;
	pc->desc.pmxops		= &meson_pmx_ops;
	pc->desc.confops	= &meson_pinconf_ops;
	pc->desc.pins		= pc->data->pins;
	pc->desc.npins		= pc->data->num_pins;

	pc->pcdev = pinctrl_register(&pc->desc, pc->dev, pc);
	if (IS_ERR(pc->pcdev)) {
		dev_err(pc->dev, "can't register pinctrl device");
		return PTR_ERR(pc->pcdev);
	}

	ret = meson_gpiolib_register(pc);
	if (ret) {
		pinctrl_unregister(pc->pcdev);
		return ret;
	}

	return 0;
}

static struct platform_driver meson_pinctrl_driver = {
	.probe		= meson_pinctrl_probe,
	.driver = {
		.name	= "meson-pinctrl",
		.of_match_table = meson_pinctrl_dt_match,
	},
};
module_platform_driver(meson_pinctrl_driver);

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("Amlogic Meson pinctrl driver");
MODULE_LICENSE("GPL v2");
