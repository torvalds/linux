// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Conexant Digicolor General Purpose Pin Mapping
 *
 * Author: Baruch Siach <baruch@tkos.co.il>
 *
 * Copyright (C) 2015 Paradox Innovation Ltd.
 *
 * TODO:
 * - GPIO interrupt support
 * - Pin pad configuration (pull up/down, strength)
 */

#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-utils.h"

#define DRIVER_NAME	"pinctrl-digicolor"

#define GP_CLIENTSEL(clct)	((clct)*8 + 0x20)
#define GP_DRIVE0(clct)		(GP_CLIENTSEL(clct) + 2)
#define GP_OUTPUT0(clct)	(GP_CLIENTSEL(clct) + 3)
#define GP_INPUT(clct)		(GP_CLIENTSEL(clct) + 6)

#define PIN_COLLECTIONS		('R' - 'A' + 1)
#define PINS_PER_COLLECTION	8
#define PINS_COUNT		(PIN_COLLECTIONS * PINS_PER_COLLECTION)

struct dc_pinmap {
	void __iomem		*regs;
	struct device		*dev;
	struct pinctrl_dev	*pctl;

	struct pinctrl_desc	*desc;
	const char		*pin_names[PINS_COUNT];

	struct gpio_chip	chip;
	spinlock_t		lock;
};

static int dc_get_groups_count(struct pinctrl_dev *pctldev)
{
	return PINS_COUNT;
}

static const char *dc_get_group_name(struct pinctrl_dev *pctldev,
				     unsigned selector)
{
	struct dc_pinmap *pmap = pinctrl_dev_get_drvdata(pctldev);

	/* Exactly one group per pin */
	return pmap->desc->pins[selector].name;
}

static int dc_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			     const unsigned **pins,
			     unsigned *num_pins)
{
	struct dc_pinmap *pmap = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pmap->desc->pins[selector].number;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops dc_pinctrl_ops = {
	.get_groups_count	= dc_get_groups_count,
	.get_group_name		= dc_get_group_name,
	.get_group_pins		= dc_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_pin,
	.dt_free_map		= pinctrl_utils_free_map,
};

static const char *const dc_functions[] = {
	"gpio",
	"client_a",
	"client_b",
	"client_c",
};

static int dc_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(dc_functions);
}

static const char *dc_get_fname(struct pinctrl_dev *pctldev, unsigned selector)
{
	return dc_functions[selector];
}

static int dc_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			 const char * const **groups,
			 unsigned * const num_groups)
{
	struct dc_pinmap *pmap = pinctrl_dev_get_drvdata(pctldev);

	*groups = pmap->pin_names;
	*num_groups = PINS_COUNT;

	return 0;
}

static void dc_client_sel(int pin_num, int *reg, int *bit)
{
	*bit = (pin_num % PINS_PER_COLLECTION) * 2;
	*reg = GP_CLIENTSEL(pin_num/PINS_PER_COLLECTION);

	if (*bit >= PINS_PER_COLLECTION) {
		*bit -= PINS_PER_COLLECTION;
		*reg += 1;
	}
}

static int dc_set_mux(struct pinctrl_dev *pctldev, unsigned selector,
		      unsigned group)
{
	struct dc_pinmap *pmap = pinctrl_dev_get_drvdata(pctldev);
	int bit_off, reg_off;
	u8 reg;

	dc_client_sel(group, &reg_off, &bit_off);

	reg = readb_relaxed(pmap->regs + reg_off);
	reg &= ~(3 << bit_off);
	reg |= (selector << bit_off);
	writeb_relaxed(reg, pmap->regs + reg_off);

	return 0;
}

static int dc_pmx_request_gpio(struct pinctrl_dev *pcdev,
			       struct pinctrl_gpio_range *range,
			       unsigned offset)
{
	struct dc_pinmap *pmap = pinctrl_dev_get_drvdata(pcdev);
	int bit_off, reg_off;
	u8 reg;

	dc_client_sel(offset, &reg_off, &bit_off);

	reg = readb_relaxed(pmap->regs + reg_off);
	if ((reg & (3 << bit_off)) != 0)
		return -EBUSY;

	return 0;
}

static const struct pinmux_ops dc_pmxops = {
	.get_functions_count	= dc_get_functions_count,
	.get_function_name	= dc_get_fname,
	.get_function_groups	= dc_get_groups,
	.set_mux		= dc_set_mux,
	.gpio_request_enable	= dc_pmx_request_gpio,
};

static int dc_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct dc_pinmap *pmap = gpiochip_get_data(chip);
	int reg_off = GP_DRIVE0(gpio/PINS_PER_COLLECTION);
	int bit_off = gpio % PINS_PER_COLLECTION;
	u8 drive;
	unsigned long flags;

	spin_lock_irqsave(&pmap->lock, flags);
	drive = readb_relaxed(pmap->regs + reg_off);
	drive &= ~BIT(bit_off);
	writeb_relaxed(drive, pmap->regs + reg_off);
	spin_unlock_irqrestore(&pmap->lock, flags);

	return 0;
}

static int dc_gpio_set(struct gpio_chip *chip, unsigned int gpio, int value);

static int dc_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
				    int value)
{
	struct dc_pinmap *pmap = gpiochip_get_data(chip);
	int reg_off = GP_DRIVE0(gpio/PINS_PER_COLLECTION);
	int bit_off = gpio % PINS_PER_COLLECTION;
	u8 drive;
	unsigned long flags;

	dc_gpio_set(chip, gpio, value);

	spin_lock_irqsave(&pmap->lock, flags);
	drive = readb_relaxed(pmap->regs + reg_off);
	drive |= BIT(bit_off);
	writeb_relaxed(drive, pmap->regs + reg_off);
	spin_unlock_irqrestore(&pmap->lock, flags);

	return 0;
}

static int dc_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	struct dc_pinmap *pmap = gpiochip_get_data(chip);
	int reg_off = GP_INPUT(gpio/PINS_PER_COLLECTION);
	int bit_off = gpio % PINS_PER_COLLECTION;
	u8 input;

	input = readb_relaxed(pmap->regs + reg_off);

	return !!(input & BIT(bit_off));
}

static int dc_gpio_set(struct gpio_chip *chip, unsigned int gpio, int value)
{
	struct dc_pinmap *pmap = gpiochip_get_data(chip);
	int reg_off = GP_OUTPUT0(gpio/PINS_PER_COLLECTION);
	int bit_off = gpio % PINS_PER_COLLECTION;
	u8 output;
	unsigned long flags;

	spin_lock_irqsave(&pmap->lock, flags);
	output = readb_relaxed(pmap->regs + reg_off);
	if (value)
		output |= BIT(bit_off);
	else
		output &= ~BIT(bit_off);
	writeb_relaxed(output, pmap->regs + reg_off);
	spin_unlock_irqrestore(&pmap->lock, flags);

	return 0;
}

static int dc_gpiochip_add(struct dc_pinmap *pmap)
{
	struct gpio_chip *chip = &pmap->chip;
	int ret;

	chip->label		= DRIVER_NAME;
	chip->parent		= pmap->dev;
	chip->request		= gpiochip_generic_request;
	chip->free		= gpiochip_generic_free;
	chip->direction_input	= dc_gpio_direction_input;
	chip->direction_output	= dc_gpio_direction_output;
	chip->get		= dc_gpio_get;
	chip->set		= dc_gpio_set;
	chip->base		= -1;
	chip->ngpio		= PINS_COUNT;

	spin_lock_init(&pmap->lock);

	ret = gpiochip_add_data(chip, pmap);
	if (ret < 0)
		return ret;

	ret = gpiochip_add_pin_range(chip, dev_name(pmap->dev), 0, 0,
				     PINS_COUNT);
	if (ret < 0) {
		gpiochip_remove(chip);
		return ret;
	}

	return 0;
}

static int dc_pinctrl_probe(struct platform_device *pdev)
{
	struct dc_pinmap *pmap;
	struct pinctrl_pin_desc *pins;
	struct pinctrl_desc *pctl_desc;
	char *pin_names;
	int name_len = strlen("GP_xx") + 1;
	int i, j;

	pmap = devm_kzalloc(&pdev->dev, sizeof(*pmap), GFP_KERNEL);
	if (!pmap)
		return -ENOMEM;

	pmap->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmap->regs))
		return PTR_ERR(pmap->regs);

	pins = devm_kcalloc(&pdev->dev, PINS_COUNT, sizeof(*pins),
			    GFP_KERNEL);
	if (!pins)
		return -ENOMEM;
	pin_names = devm_kcalloc(&pdev->dev, PINS_COUNT, name_len,
				 GFP_KERNEL);
	if (!pin_names)
		return -ENOMEM;

	for (i = 0; i < PIN_COLLECTIONS; i++) {
		for (j = 0; j < PINS_PER_COLLECTION; j++) {
			int pin_id = i*PINS_PER_COLLECTION + j;
			char *name = &pin_names[pin_id * name_len];

			snprintf(name, name_len, "GP_%c%c", 'A'+i, '0'+j);

			pins[pin_id].number = pin_id;
			pins[pin_id].name = name;
			pmap->pin_names[pin_id] = name;
		}
	}

	pctl_desc = devm_kzalloc(&pdev->dev, sizeof(*pctl_desc), GFP_KERNEL);
	if (!pctl_desc)
		return -ENOMEM;

	pctl_desc->name	= DRIVER_NAME,
	pctl_desc->owner = THIS_MODULE,
	pctl_desc->pctlops = &dc_pinctrl_ops,
	pctl_desc->pmxops = &dc_pmxops,
	pctl_desc->npins = PINS_COUNT;
	pctl_desc->pins = pins;
	pmap->desc = pctl_desc;

	pmap->dev = &pdev->dev;

	pmap->pctl = devm_pinctrl_register(&pdev->dev, pctl_desc, pmap);
	if (IS_ERR(pmap->pctl)) {
		dev_err(&pdev->dev, "pinctrl driver registration failed\n");
		return PTR_ERR(pmap->pctl);
	}

	return dc_gpiochip_add(pmap);
}

static const struct of_device_id dc_pinctrl_ids[] = {
	{ .compatible = "cnxt,cx92755-pinctrl" },
	{ /* sentinel */ }
};

static struct platform_driver dc_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = dc_pinctrl_ids,
		.suppress_bind_attrs = true,
	},
	.probe = dc_pinctrl_probe,
};
builtin_platform_driver(dc_pinctrl_driver);
